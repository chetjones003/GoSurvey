// REQ-498 / GitHub issue #121 Phase 5 - feature lines made from an alignment, refreshed from it, and used as a source for
// CREATEALIGNMENTFROMOBJECTS. Z comes from a constant or from a plane surface (Z = 0.1 X + 50).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentFeatureLine.hpp"

using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[1024];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos) return true;
  }
  return false;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  st.displayLinearPrecision = 4;
  return st;
}

/// Vertex k of feature line `line` as (x, y, z).
void Vertex(const AppCommandState& st, size_t line, size_t k, double* x, double* y, double* z) {
  const size_t v = static_cast<size_t>(st.featureLineOffsets[line]) + k;
  *x = st.featureLineVerts[v * 3];
  *y = st.featureLineVerts[v * 3 + 1];
  *z = st.featureLineVerts[v * 3 + 2];
}

size_t VertexCount(const AppCommandState& st, size_t line) {
  return static_cast<size_t>(st.featureLineOffsets[line + 1] - st.featureLineOffsets[line]);
}

/// The plane-surface setup: an 11 x 2 grid over X 0..100, Y 0..10, Z = 0.1 X + 50.
void MakePlane(AppCommandState& st, std::vector<std::string>& log, const char* name, int cols) {
  std::string z;
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < cols; ++i) {
      char buf[32];
      std::snprintf(buf, sizeof buf, ", %g", 0.1 * 10.0 * i + 50.0);
      z += buf;
    }
  }
  Submit(st, std::string("SURFACECREATEGRID ") + name + ", 0, 0, 10, 10, " + std::to_string(cols) + ", 2" + z, log);
}

}  // namespace

TEST_CASE("Feature line stations include the PC and PT and keep every curve chord within the tolerance", "[req498][alignment]") {
  alignment::Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendCurveByDelta(100.0, 90.0 * kDeg, true));
  REQUIRE(a.AppendTangent(50.0));
  const std::vector<double> s = alignment::FeatureLineStations(a, 0.0, 0.01);
  const double pt = 100.0 + 100.0 * 90.0 * kDeg;
  bool hasPc = false, hasPt = false;
  for (const double v : s) {
    if (std::fabs(v - 100.0) < 1e-9) hasPc = true;
    if (std::fabs(v - pt) < 1e-9) hasPt = true;
  }
  CHECK(hasPc);  // REQ-498 AC1: PC
  CHECK(hasPt);  // REQ-498 AC1: PT
  // Every chord between neighbouring curve stations lies within the tolerance of the arc: R - chord midpoint distance.
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i - 1] < 100.0 - 1e-9 || s[i] > pt + 1e-9) continue;
    const alignment::Point2 p = a.GetPointAtStation(s[i - 1]);
    const alignment::Point2 q = a.GetPointAtStation(s[i]);
    const alignment::Point2 mid{(p.x + q.x) * 0.5, (p.y + q.y) * 0.5};
    // The line starts heading north (azimuth 0) and turns right, so the arc's centre is 100 to the east, at (100, 0).
    const double dist = std::hypot(mid.x - 100.0, mid.y);
    CHECK(100.0 - dist <= 0.01 + 1e-9);
  }
}

TEST_CASE("FEATURELINEFROMALIGNMENT makes a linked line with a constant Z, and it refreshes after a radius edit",
          "[req498][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT FL1, , 0+00", log);
  Submit(st, "ALIGNMENTADD FL1, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENTADD FL1, CURVE, 100, 90, RIGHT", log);
  log.clear();
  Submit(st, "FEATURELINEFROMALIGNMENT FL1, INTERVAL=50, TOL=0.01, Z=12", log);
  REQUIRE(st.featureLineInfo.size() == 1);
  CadFeatureLineInfo& info = st.featureLineInfo[0];
  CHECK(info.sourceAlignmentId == st.alignments[0].id);
  CHECK(LogHas(log, "constant Z"));
  // The first vertex is the start, the last the end, and the PT is among them.
  const double endX = st.alignments[0].EndPoint().x, endY = st.alignments[0].EndPoint().y;
  const size_t n = VertexCount(st, 0);
  double x = 0, y = 0, z = 0;
  Vertex(st, 0, n - 1, &x, &y, &z);
  CHECK(x == Approx(endX).margin(1e-6));
  CHECK(y == Approx(endY).margin(1e-6));
  CHECK(z == Approx(12.0));
  bool hasPt = false;
  for (size_t k = 0; k < n; ++k) {
    Vertex(st, 0, k, &x, &y, &z);
    if (std::hypot(x - endX, y - endY) < 1e-6) hasPt = true;
  }
  CHECK(hasPt);

  // REQ-498 AC2: a radius edit rebuilds the line; the vertices follow the new geometry.
  log.clear();
  Submit(st, "ALIGNMENTEDIT FL1, 2, RADIUS, 200", log);
  const double newEndX = st.alignments[0].EndPoint().x, newEndY = st.alignments[0].EndPoint().y;
  REQUIRE(std::hypot(newEndX - endX, newEndY - endY) > 1.0);  // the edit did move the end
  const size_t n2 = VertexCount(st, 0);
  Vertex(st, 0, n2 - 1, &x, &y, &z);
  CHECK(x == Approx(newEndX).margin(1e-6));
  CHECK(y == Approx(newEndY).margin(1e-6));
}

TEST_CASE("A feature line made from an alignment takes Z from a surface at each station (REQ-498 AC1)",
          "[req498][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakePlane(st, log, "PLANE", 11);
  REQUIRE(st.cadSurfaces.size() == 1);
  Submit(st, "ALIGNMENT FL2, , 0+00", log);
  Submit(st, "ALIGNMENTADD FL2, TANGENT, 0, 5, 90, 100", log);
  log.clear();
  Submit(st, "FEATURELINEFROMALIGNMENT FL2, INTERVAL=25, SURFACE=PLANE", log);
  REQUIRE(st.featureLineInfo.size() == 1);
  CHECK(LogHas(log, "Z from \"PLANE\""));
  const size_t n = VertexCount(st, 0);
  CHECK(n == 5);  // 0, 25, 50, 75, 100
  for (size_t k = 0; k < n; ++k) {
    double x = 0, y = 0, z = 0;
    Vertex(st, 0, k, &x, &y, &z);
    CHECK(z == Approx(0.1 * x + 50.0).margin(1e-4));
  }
}

TEST_CASE("A station with no ground refuses the feature line and makes nothing (REQ-497 AC2)", "[req498][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakePlane(st, log, "SMALL", 11);  // covers X 0..100 only
  Submit(st, "ALIGNMENT FL3, , 0+00", log);
  Submit(st, "ALIGNMENTADD FL3, TANGENT, 0, 5, 90, 150", log);  // runs past the surface
  log.clear();
  Submit(st, "FEATURELINEFROMALIGNMENT FL3, SURFACE=SMALL", log);
  CHECK(LogHas(log, "no ground"));
  CHECK(LogHas(log, "No feature line was made"));
  CHECK(st.featureLineInfo.empty());
}

TEST_CASE("The link survives save and reopen, and erasing the alignment drops it with a log line (REQ-498 AC3)",
          "[req498][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT FL4, , 0+00", log);
  Submit(st, "ALIGNMENTADD FL4, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "FEATURELINEFROMALIGNMENT FL4, INTERVAL=20, Z=3", log);
  REQUIRE(st.featureLineInfo.size() == 1);
  const std::uint64_t id = st.featureLineInfo[0].sourceAlignmentId;
  REQUIRE(id != 0);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.featureLineInfo.size() == 1);
  CHECK(back.featureLineInfo[0].sourceAlignmentId == id);
  CHECK(back.featureLineInfo[0].sampleInterval == Approx(20.0));
  CHECK_FALSE(back.featureLineInfo[0].hasSurface);

  // Erasing the alignment: the feature line stays as an ordinary line, and the log says so.
  log.clear();
  st.selection = {{SelectedEntity::Type::Alignment, 0}};
  ExecuteDeleteSelection(st, log);
  CHECK(st.alignments.empty());
  CHECK(st.featureLineInfo[0].sourceAlignmentId == 0);
  CHECK(LogHas(log, "lost its alignment"));
  CHECK(st.featureLineOffsets.size() == 2);  // the line itself is still there
}

TEST_CASE("A feature line is a valid source for CREATEALIGNMENTFROMOBJECTS and its Z is ignored (REQ-498 AC3 / 485)",
          "[req498][req485][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "FEATURELINEFROMALIGNMENT ,, Z=7", log);  // no alignment named: the usage line, nothing made
  CHECK(LogHas(log, "usage"));
  // Build a three-vertex feature line by hand (plan 0,0 -> 100,0 -> 100,100) with Z values that must be ignored.
  st.featureLineVerts = {0.0, 0.0, 5.0, 100.0, 0.0, 9.0, 100.0, 100.0, 1.0};
  st.featureLineOffsets = {0, 3};
  st.featureLineClosed = {0};
  st.featureLineElevPt = {1, 1, 1};
  CadFeatureLineInfo info;
  info.name = "Hand";
  st.featureLineInfo.push_back(info);
  st.featureLineAttrs.resize(1);
  SelectedEntity se;
  se.type = SelectedEntity::Type::FeatureLine;
  se.index = 0;
  st.selection = {se};
  log.clear();
  Submit(st, "CREATEALIGNMENTFROMOBJECTS FromFL", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].Entities().size() == 2);
  CHECK(st.alignments[0].Length() == Approx(200.0).margin(1e-6));
  CHECK(st.alignments[0].EndPoint().x == Approx(100.0).margin(1e-6));
  CHECK(st.alignments[0].EndPoint().y == Approx(100.0).margin(1e-6));
}
