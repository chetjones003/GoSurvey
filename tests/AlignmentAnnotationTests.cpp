// Issue #121 - alignment label sets, label styles, segment labels and style markers (REQ-491, REQ-492, REQ-493).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "AlignmentLabelSet.hpp"
#include "AlignmentStyle.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentTables.hpp"

using alignment::Alignment;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  std::vector<char> buf(text.begin(), text.end());
  buf.resize(std::max<size_t>(buf.size() + 1, 512), '\0');
  ProcessCommandLineSubmit(buf.data(), buf.size(), st, log);
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  st.displayLinearPrecision = 2;
  return st;
}

AppCommandState Reopened(const AppCommandState& st) {
  AppCommandState back = Drawing();
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(st), log));
  return back;
}

/// T 250 N45E, curve R500 Δ15 right, T 200 (the REQ-492 / REQ-493 fixture), entered by command.
void ThreeEntity(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT " + name, log);
  Submit(st, "ALIGNMENTADD " + name + ", TANGENT, 0, 0, 45, 250", log);
  Submit(st, "ALIGNMENTADD " + name + ", CURVE, 500, 15, RIGHT", log);
  Submit(st, "ALIGNMENTADD " + name + ", TANGENT, 200", log);
}

}  // namespace

TEST_CASE("The fixture's curve label reads as the spec writes it", "[req492][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 45.0 * kDeg, 250.0));
  REQUIRE(a.AppendCurveByDelta(500.0, 15.0 * kDeg, true));
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  // REQ-492 acceptance 3: R, Δ (degrees-minutes-seconds), true arc length and tangent, in the drawing's format.
  AngleDisplaySettings whole;
  whole.precision = 0;
  CHECK(alignment::CurveLabelText(cd, 2, whole) == "R = 500.00' \xCE\x94 = 15\xC2\xB0" "0'0\" L = 130.90' T = 65.83'");
}

TEST_CASE("Label style precision, prefix, suffix and orientation are set, listed and saved", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLABELSTYLE NEW, Fine", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, PRECISION, 3", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, PREFIX, STA", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, SUFFIX, (calc)", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, ORIENTATION, VIEW", log);
  const AlignmentLabelStyle* s = AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Fine");
  REQUIRE(s != nullptr);
  CHECK(s->precision == 3);
  CHECK(s->prefix == "STA");
  CHECK(s->suffix == "(calc)");
  CHECK(s->viewOriented);
  CHECK(AlignmentLabelStyles::Precision(*s, 2) == 3);
  CHECK(AlignmentLabelStyles::StationFormatFor(*s, alignment::StationFormat{}).decimals == 3);
  CHECK(AlignmentLabelStyles::Precision(AlignmentLabelStyles::StandardLabelStyle(), 2) == 2);  // drawing precision

  const AppCommandState back = Reopened(st);
  const AlignmentLabelStyle* r = AlignmentLabelStyles::Find(back.alignmentLabelStyles, "Fine");
  REQUIRE(r != nullptr);
  CHECK(*r == *s);

  log.clear();
  Submit(st, "ALIGNMENTLABELSTYLE LIST", log);
  CHECK(LogHas(log, "precision=3"));
  CHECK(LogHas(log, "orientation=view"));
  log.clear();
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, PRECISION, 12", log);
  CHECK(LogHas(log, "precision must be 0 to 8"));
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, PREFIX, NONE", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Fine, PRECISION, DRAWING", log);
  CHECK(AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Fine")->prefix.empty());
  CHECK(AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Fine")->precision == -1);
}

TEST_CASE("A Geometry Points item repeats with its own codes and style", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLABELSTYLE NEW, Ends", log);
  Submit(st, "ALIGNMENTLABELSET GEOMETRY, Standard, CODES, PI PC PT", log);
  Submit(st, "ALIGNMENTLABELSET GEOMETRY, Standard, ADD, Ends, bp ep", log);
  const AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, "Standard");
  REQUIRE(s != nullptr);
  CHECK(s->geometryCodes == std::vector<std::string>{"PI", "PC", "PT"});
  REQUIRE(s->extraGeometryItems.size() == 1);
  CHECK(s->extraGeometryItems[0].style == "Ends");
  CHECK(s->extraGeometryItems[0].codes == std::vector<std::string>{"BP", "EP"});
  CHECK(AlignmentLabelStyles::ItemLabelsCode(s->geometryCodes, "PI"));
  CHECK_FALSE(AlignmentLabelStyles::ItemLabelsCode(s->geometryCodes, "BP"));
  CHECK(AlignmentLabelStyles::ItemLabelsCode({}, "BP"));  // an empty list labels every code

  log.clear();
  Submit(st, "ALIGNMENTLABELSTYLE DELETE, Ends", log);  // the further item still uses it
  CHECK(LogHas(log, "cannot be deleted"));
  log.clear();
  Submit(st, "ALIGNMENTLABELSET GEOMETRY, Standard, ADD, Ends, XX", log);
  CHECK(LogHas(log, "\"XX\" is not a geometry point code"));
  CHECK(AlignmentLabelSets::Find(st.alignmentLabelSets, "Standard")->extraGeometryItems.size() == 1);

  const AppCommandState back = Reopened(st);
  CHECK(*AlignmentLabelSets::Find(back.alignmentLabelSets, "Standard") == *s);

  Submit(st, "ALIGNMENTLABELSET GEOMETRY, Standard, REMOVE, 1", log);
  Submit(st, "ALIGNMENTLABELSET GEOMETRY, Standard, CODES, ALL", log);
  CHECK(AlignmentLabelSets::Find(st.alignmentLabelSets, "Standard")->extraGeometryItems.empty());
  CHECK(AlignmentLabelSets::Find(st.alignmentLabelSets, "Standard")->geometryCodes.empty());
}

TEST_CASE("Profile and superelevation items are placeholders that are kept", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLABELSET SHOW, Standard, PROFILE", log);
  CHECK(LogHas(log, "placeholder"));
  Submit(st, "ALIGNMENTLABELSET SHOW, Standard, SUPERELEVATION", log);
  const AppCommandState back = Reopened(st);
  const AlignmentLabelSet* s = AlignmentLabelSets::Find(back.alignmentLabelSets, "Standard");
  REQUIRE(s != nullptr);
  CHECK(s->profileGeometryPointsOn);
  CHECK(s->superelevationPointsOn);
}

TEST_CASE("A new label set in a metric drawing labels every 20 m", "[req492][alignment]") {
  AppCommandState st = Drawing();
  st.drawingInsUnits = 6;  // metres
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLABELSET NEW, Metric", log);
  const AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, "Metric");
  REQUIRE(s != nullptr);
  CHECK(s->majorInterval == 20.0);
  CHECK(s->minorInterval == 4.0);
  st.drawingInsUnits = 2;  // feet
  Submit(st, "ALIGNMENTLABELSET NEW, Imperial", log);
  CHECK(AlignmentLabelSets::Find(st.alignmentLabelSets, "Imperial")->majorInterval == 100.0);
}

TEST_CASE("A single segment label stays on its segment and a table lists only labelled segments",
          "[req492][req493][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  ThreeEntity(st, "SS", log);
  log.clear();
  Submit(st, "ALIGNMENTLABEL SS, SEGMENT, 3", log);
  CHECK(LogHas(log, "entity 3 of \"SS\" labelled"));
  const Alignment& a = st.alignments[0];
  CHECK_FALSE(alignment::SegmentIsLabelled(a, 0));
  CHECK_FALSE(alignment::SegmentIsLabelled(a, 1));
  CHECK(alignment::SegmentIsLabelled(a, 2));
  CHECK(alignment::HasSegmentLabels(a));

  Submit(st, "ALIGNMENTTABLE SS, LINE, DYNAMIC", log);
  REQUIRE(st.cadTables.size() == 1);
  CHECK(st.cadTables[0].cells.size() == 10);  // header + the one labelled line
  CHECK(st.cadTables[0].cells[5] == "L2");

  Submit(st, "REVERSEALIGNMENT SS", log);  // the label follows the segment to the front
  CHECK(alignment::SegmentIsLabelled(st.alignments[0], 0));
  CHECK_FALSE(alignment::SegmentIsLabelled(st.alignments[0], 2));

  const AppCommandState back = Reopened(st);
  CHECK(alignment::SegmentIsLabelled(back.alignments[0], 0));
  CHECK_FALSE(alignment::SegmentIsLabelled(back.alignments[0], 1));

  log.clear();
  Submit(st, "ALIGNMENTLABEL SS, SEGMENT, 9", log);
  CHECK(LogHas(log, "has entities 1 to 3"));
  Submit(st, "ALIGNMENTLABEL SS, CLEAR", log);
  CHECK_FALSE(alignment::HasSegmentLabels(st.alignments[0]));
}

TEST_CASE("Style markers are set per geometry point code and saved", "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTSTYLE NEW, Marked", log);
  Submit(st, "ALIGNMENTSTYLE MARKER, Marked, PI, CIRCLE, 3, red", log);
  Submit(st, "ALIGNMENTSTYLE MARKER, Marked, BP, SQUARE", log);
  const AlignmentStyle* s = AlignmentStyles::Find(st.alignmentStyles, "Marked");
  REQUIRE(s != nullptr);
  REQUIRE(s->codeMarkers.size() == 2);
  CHECK(s->codeMarkers[0].code == "PI");
  CHECK(s->codeMarkers[0].shape == AlignmentMarkerShape::Circle);
  CHECK(s->codeMarkers[0].halfSize == 3.0);
  CHECK(s->codeMarkers[0].color != "ByLayer");
  CHECK(s->codeMarkers[1].halfSize == s->markerHalfSize);  // no size given: the style's marker size

  const AppCommandState back = Reopened(st);
  CHECK(*AlignmentStyles::Find(back.alignmentStyles, "Marked") == *s);

  log.clear();
  Submit(st, "ALIGNMENTSTYLE MARKER, Marked, PI, TRIANGLE", log);
  CHECK(LogHas(log, "MARKER needs a shape"));
  Submit(st, "ALIGNMENTSTYLE MARKER, Marked, PI, DEFAULT", log);
  CHECK(AlignmentStyles::Find(st.alignmentStyles, "Marked")->codeMarkers.size() == 1);
}

TEST_CASE("New alignments use the current alignment style, which cannot be deleted", "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTSTYLE NEW, Road", log);
  Submit(st, "ALIGNMENTSTYLE CURRENT, Road", log);
  CHECK(st.alignmentStyleCurrent == "Road");
  Submit(st, "ALIGNMENT R1", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].style == "Road");
  Submit(st, "ALIGNMENTSTYLE ASSIGN, R1, Standard", log);
  log.clear();
  Submit(st, "ALIGNMENTSTYLE DELETE, Road", log);
  CHECK(LogHas(log, "current style and cannot be deleted"));
  CHECK(AlignmentStyles::Find(st.alignmentStyles, "Road") != nullptr);
  CHECK(Reopened(st).alignmentStyleCurrent == "Road");
}
