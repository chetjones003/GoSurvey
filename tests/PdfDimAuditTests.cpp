// REQ-395: the automatic scale audit - pairing dimension text with the dimension line it labels, the verdicts,
// the consensus scale and the offenders. Pure matching, no PDF, no window.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfDimAudit.hpp"

#include <cmath>
#include <string>

using namespace pdfview;

namespace {

// 1 pt = 0.5 ft
PageScale HalfFoot() { return ScaleFromCalibration(100.0, 50.0, Unit::Foot); }

std::string FeetText(int feet) { return std::to_string(feet) + "'-0\""; }

struct Sheet {
  std::vector<DimText> texts;
  std::vector<DimSeg> segs;

  // A horizontal dimension: line, an extension line at each end, text above. \p drawnPts is what is drawn.
  void Horizontal(float x, float y, double drawnPts, int statedFeet) {
    const float x1 = x + static_cast<float>(drawnPts);
    segs.push_back({x, y, x1, y});
    segs.push_back({x, y - 3.f, x, y + 8.f});
    segs.push_back({x1, y - 3.f, x1, y + 8.f});
    const float cx = (x + x1) * 0.5f;
    texts.push_back({FeetText(statedFeet), cx - 15.f, y + 3.f, cx + 15.f, y + 11.f});
  }
  // A vertical dimension with the text turned (a tall box) to its left.
  void Vertical(float x, float y, double drawnPts, int statedFeet) {
    const float y1 = y + static_cast<float>(drawnPts);
    segs.push_back({x, y, x, y1});
    segs.push_back({x - 3.f, y, x + 8.f, y});
    segs.push_back({x - 3.f, y1, x + 8.f, y1});
    const float cy = (y + y1) * 0.5f;
    texts.push_back({FeetText(statedFeet), x - 12.f, cy - 15.f, x - 4.f, cy + 15.f});
  }
};

// n horizontal and n vertical dimensions at the scale of HalfFoot (a stated N feet is drawn 2N points long).
Sheet MakeSheet(int n, int oneLongIdx = -1, double longFactor = 1.01) {
  Sheet s;
  for (int i = 0; i < n; ++i) {
    const int feet = 20 + 6 * i; // 20, 26, ... : drawn 40, 52, ... points
    const double pts = 2.0 * feet * (i == oneLongIdx ? longFactor : 1.0);
    s.Horizontal(30.f, 40.f + 40.f * static_cast<float>(i), pts, feet);
  }
  for (int i = 0; i < n; ++i) {
    const int feet = 24 + 4 * i;
    s.Vertical(600.f + 60.f * static_cast<float>(i), 40.f, 2.0 * feet, feet);
  }
  return s;
}

bool Never() { return false; }

} // namespace

TEST_CASE("audit matches every dimension and finds the true scale", "[pdfdimaudit][req395][issue732]") {
  const Sheet s = MakeSheet(12);
  DimMatchSet set;
  REQUIRE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, Never, set));
  CHECK(set.matches.size() == 24);
  CHECK(set.textsRead == 24);
  CHECK(set.unmatchedText == 0);
  CHECK(set.unmatchedLines == 0);

  const PageScale sc = HalfFoot();
  const DimAudit a = AuditDimensions(std::move(set), sc, CheckLimits{});
  REQUIRE(a.consensusValid);
  const double trueMetresPerPt = 0.5 * 0.3048;
  CHECK(std::fabs(a.consensusMetresPerPt / trueMetresPerPt - 1.0) < 1e-4);
  CHECK(a.good == 24);
  CHECK(a.offenders.empty());
  CHECK(a.message.empty());
}

TEST_CASE("audit reports a dimension drawn 1 percent long and does not move the consensus", "[pdfdimaudit][req395][issue732]") {
  const Sheet s = MakeSheet(12, 5, 1.01);
  DimMatchSet set;
  REQUIRE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, Never, set));
  const DimAudit a = AuditDimensions(std::move(set), HalfFoot(), CheckLimits{});
  REQUIRE(a.consensusValid);
  CHECK(std::fabs(a.consensusMetresPerPt / (0.5 * 0.3048) - 1.0) < 1e-4);
  REQUIRE(a.offenders.size() == 1);
  const DimMatch& worst = a.set.matches[static_cast<size_t>(a.offenders[0])];
  CHECK(worst.text == FeetText(20 + 6 * 5));
  CHECK(a.blunder == 1);
  CHECK(a.good == 23);
}

TEST_CASE("audit leaves text without a line and lines without text unmatched", "[pdfdimaudit][req395][issue732]") {
  Sheet s = MakeSheet(6);
  s.texts.push_back({"12'-0\"", 200.f, 900.f, 230.f, 908.f}); // a length with no line anywhere near it
  s.texts.push_back({"A-101", 300.f, 40.f, 330.f, 48.f});    // not a length
  s.texts.push_back({"12", 300.f, 80.f, 330.f, 88.f});      // a bare number is not taken
  s.segs.push_back({900.f, 900.f, 960.f, 900.f});            // a dimension line with extension lines but no text
  s.segs.push_back({900.f, 897.f, 900.f, 908.f});
  s.segs.push_back({960.f, 897.f, 960.f, 908.f});
  DimMatchSet set;
  REQUIRE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, Never, set));
  CHECK(set.matches.size() == 12);
  CHECK(set.textsRead == 13);
  CHECK(set.unmatchedText == 1);
  CHECK(set.unmatchedLines == 1); // the line at (900, 900) has lost its text
}

TEST_CASE("audit needs extension lines and a text aligned with the line", "[pdfdimaudit][req395][issue732]") {
  Sheet s;
  s.segs.push_back({30.f, 40.f, 130.f, 40.f}); // a plain line, no extension lines
  s.texts.push_back({"50'-0\"", 65.f, 43.f, 95.f, 51.f});
  s.Horizontal(30.f, 200.f, 100.0, 50);
  s.texts.push_back({"50'-0\"", 5.f, 190.f, 13.f, 220.f}); // tall text beside a horizontal line: not aligned with it
  DimMatchSet set;
  REQUIRE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, Never, set));
  REQUIRE(set.matches.size() == 1);
  CHECK(set.matches[0].y0 == 200.f);
  CHECK(set.unmatchedText == 2);
}

TEST_CASE("audit measures between the extension lines, not the line's overshoot", "[pdfdimaudit][req395][issue732]") {
  Sheet s;
  s.segs.push_back({26.f, 40.f, 134.f, 40.f}); // the line runs 4 pt past each extension line
  s.segs.push_back({30.f, 37.f, 30.f, 48.f});
  s.segs.push_back({130.f, 37.f, 130.f, 48.f});
  s.texts.push_back({"50'-0\"", 65.f, 43.f, 95.f, 51.f});
  DimMatchSet set;
  REQUIRE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, Never, set));
  REQUIRE(set.matches.size() == 1);
  CHECK(std::fabs(set.matches[0].lengthPt - 100.0) < 1e-3);
}

TEST_CASE("audit with no text or too few matches gives no consensus", "[pdfdimaudit][req395][issue732]") {
  { // a page with no text
    DimMatchSet set;
    REQUIRE(MatchDimensions({}, {}, DimMatchParams{}, Never, set));
    const DimAudit a = AuditDimensions(std::move(set), HalfFoot(), CheckLimits{});
    CHECK_FALSE(a.consensusValid);
    CHECK(a.message.find("No dimension text") != std::string::npos);
  }
  { // four matches: one short of five
    const Sheet s = MakeSheet(2);
    DimMatchSet set;
    REQUIRE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, Never, set));
    REQUIRE(set.matches.size() == 4);
    const DimAudit a = AuditDimensions(std::move(set), HalfFoot(), CheckLimits{});
    CHECK_FALSE(a.consensusValid);
    CHECK_FALSE(a.message.empty());
  }
}

TEST_CASE("cancelling the audit returns no result", "[pdfdimaudit][req395][issue732]") {
  const Sheet s = MakeSheet(12);
  DimMatchSet set;
  CHECK_FALSE(MatchDimensions(s.texts, s.segs, DimMatchParams{}, [] { return true; }, set));
  CHECK(set.matches.empty());
}

TEST_CASE("the consensus scale replaces the page scale and remembers the original", "[pdfdimaudit][req395][issue732]") {
  const PageScale prev = HalfFoot();
  const PageScale s = ScaleFromConsensus(0.5 * 0.3048 * 1.002, 9, prev);
  CHECK(s.note.find("9 dimensions") != std::string::npos);
  CHECK(s.originalRealValue > 0.0);
  CHECK(std::fabs(s.RealPerPoint() / (prev.RealPerPoint() * 1.002) - 1.0) < 1e-9);
}
