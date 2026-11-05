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
#include "alignment/AlignmentDisplayCache.hpp"
#include "alignment/AlignmentTables.hpp"

#include <catch2/catch_approx.hpp>
#include <cmath>

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
  CHECK(alignment::CurveLabelText(cd, 2, whole) == "R = 500.00' \xCE\x94 = 15\xC2\xB0" "00'00\" L = 130.90' T = 65.83'");
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

// --- REQ-495: display sampling, the display cache and the bench scene ---

namespace {

/// The largest sagitta between consecutive samples of a circular arc of radius `r`.
double WorstArcSagitta(const std::vector<alignment::Point2>& pts, double r) {
  double worst = 0.0;
  for (size_t i = 1; i < pts.size(); ++i) {
    const double c = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    worst = std::max(worst, r - std::sqrt(std::max(0.0, r * r - 0.25 * c * c)));
  }
  return worst;
}

}  // namespace

TEST_CASE("Small and large arcs stay within a quarter pixel at 1:1 and 1000x zoomed out", "[req495][alignment]") {
  for (const double radius : {5.0, 5000.0}) {
    Alignment a;
    REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 10.0));
    REQUIRE(a.AppendCurve(radius, radius * 1.5, true));  // 86 degrees of arc
    for (const double pixelWorld : {0.01, 10.0}) {  // drawing units per pixel at 1:1 and zoomed 1000x out
      const double h = 0.25 * pixelWorld;
      const std::vector<alignment::Point2> pts = a.SampleEntityForDisplay(1, h);
      REQUIRE(pts.size() >= 2);
      CHECK(WorstArcSagitta(pts, radius) <= h + 1e-12);
      alignment::AlignmentDisplayCache cache;
      const std::vector<alignment::Point2>& cached = cache.Samples(a, h)[1];
      CHECK(WorstArcSagitta(cached, radius) <= h + 1e-12);
    }
  }
}

TEST_CASE("Spiral samples hug the clothoid, with curvature rising along it", "[req495][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 10.0));
  REQUIRE(a.AppendSpiral(300.0, 0.0, 200.0, true));
  const double h = 0.0025;  // a quarter pixel at 0.01 units per pixel
  const std::vector<alignment::Point2> pts = a.SampleEntityForDisplay(1, h);
  REQUIRE(pts.size() >= 3);
  CHECK(pts.size() < 300.0 / h / 10.0);  // far fewer than a fixed step of the tolerance would make
  double worst = 0.0;
  for (size_t i = 1; i < pts.size(); ++i) {  // the true curve between two samples stays within h of their chord
    const alignment::StationOffset so0 = a.GetStationOffset(pts[i - 1].x, pts[i - 1].y);
    const alignment::StationOffset so1 = a.GetStationOffset(pts[i].x, pts[i].y);
    const alignment::Point2 mid = a.GetPointAtStation(0.5 * (so0.internalStation + so1.internalStation));
    const double dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
    worst = std::max(worst, std::fabs((mid.x - pts[i - 1].x) * dy - (mid.y - pts[i - 1].y) * dx) / std::hypot(dx, dy));
  }
  CHECK(worst <= h * 1.01);
  double last = -1.0;
  bool rising = true;
  for (size_t i = 2; i < pts.size(); ++i) {  // turning per unit length between successive chords never falls
    const double a0 = std::atan2(pts[i - 1].y - pts[i - 2].y, pts[i - 1].x - pts[i - 2].x);
    const double a1 = std::atan2(pts[i].y - pts[i - 1].y, pts[i].x - pts[i - 1].x);
    const double len = 0.5 * (std::hypot(pts[i - 1].x - pts[i - 2].x, pts[i - 1].y - pts[i - 2].y) +
                              std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y));
    const double k = std::fabs(std::remainder(a1 - a0, 2.0 * alignment::kPi)) / len;
    if (k < last * (1.0 - 1e-6)) rising = false;
    last = k;
  }
  CHECK(rising);
}

TEST_CASE("The display cache holds across pans and restyles and refreshes on a geometry edit or zoom bucket",
          "[req495][alignment]") {
  Alignment a;
  a.id = 7;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendCurve(300.0, 200.0, true));
  alignment::AlignmentDisplayCache cache;
  (void)cache.Samples(a, 0.01);
  CHECK(cache.Rebuilds() == 1);
  (void)cache.Samples(a, 0.01);    // a pan: same zoom
  a.style = "Other";               // a restyle: same geometry
  (void)cache.Samples(a, 0.0105);  // the same half-octave bucket
  CHECK(cache.Rebuilds() == 1);
  CHECK(alignment::AlignmentDisplayCache::BucketTolerance(alignment::AlignmentDisplayCache::Bucket(0.0105)) <= 0.0105);
  (void)cache.Samples(a, 0.1);  // zoomed out: another bucket
  CHECK(cache.Rebuilds() == 2);
  (void)cache.Samples(a, 0.01);  // back again: still cached
  CHECK(cache.Rebuilds() == 2);
  REQUIRE(a.AppendTangent(50.0));  // a geometry edit
  CHECK(cache.Samples(a, 0.01).size() == 3);
  CHECK(cache.Rebuilds() == 3);
  cache.Retain({});
  (void)cache.Samples(a, 0.01);
  CHECK(cache.Rebuilds() == 4);  // dropped with its alignment
}

TEST_CASE("BENCH ALIGNMENT installs the alignment scene and restores the drawing", "[req495][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  ThreeEntity(st, "KEEP", log);
  Submit(st, "BENCH ALIGNMENT 12 9 5", log);
  REQUIRE(st.bench.active);
  REQUIRE(st.alignments.size() == 12);
  CHECK(st.alignments[0].Entities().size() == 9);
  CHECK(LogHas(log, "12 alignments x 9 entities"));
  st.bench.frameMs = {1.0, 2.0};
  FinishFrameBudgetBench(st, log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].name == "KEEP");
  CHECK(LogHas(log, "BENCH (alignments)"));
}

// --- REQ-481.5a: curve and spiral definitions ---

TEST_CASE("A curve can be sized by degree, tangent, chord, external or middle ordinate, keeping its delta",
          "[req481][alignment]") {
  struct Case {
    alignment::EditParam param;
    double value;
    const char* name;
  };
  const std::vector<Case> cases{{alignment::EditParam::CurveDegree, 10.0, "Degree of curve, delta"},
                                {alignment::EditParam::CurveTangent, 80.0, "Tangent length, delta"},
                                {alignment::EditParam::CurveChord, 150.0, "Chord length, delta"},
                                {alignment::EditParam::CurveExternal, 6.0, "External, delta"},
                                {alignment::EditParam::CurveMiddleOrdinate, 5.0, "Middle ordinate, delta"}};
  for (const Case& c : cases) {
    Alignment a;
    REQUIRE(a.AppendTangent({0.0, 0.0}, 45.0 * kDeg, 250.0));
    REQUIRE(a.AppendCurveByDelta(500.0, 15.0 * kDeg, true));
    REQUIRE(a.AppendTangent(200.0));
    REQUIRE(a.EditEntity(1, c.param, c.value));
    alignment::CurveData cd;
    REQUIRE(a.CurveDataFor(1, &cd));
    CHECK(cd.delta == Catch::Approx(15.0 * kDeg).epsilon(1e-12));
    const double got = c.param == alignment::EditParam::CurveDegree    ? cd.degreeOfCurveArc
                       : c.param == alignment::EditParam::CurveTangent ? cd.tangentLength
                       : c.param == alignment::EditParam::CurveChord   ? cd.chord
                       : c.param == alignment::EditParam::CurveExternal ? cd.external
                                                                        : cd.midOrdinate;
    CHECK(got == Catch::Approx(c.value).epsilon(1e-10));
    CHECK(std::string(alignment::ParameterConstraintName(a.Entities()[1])) == c.name);
    CHECK(alignment::ParameterEditable(a.Entities()[1], c.param));
  }
  double r = 0.0;
  CHECK_FALSE(alignment::RadiusForCurveParameter(alignment::EditParam::CurveTangent, -1.0, 0.2, &r));
  CHECK_FALSE(alignment::RadiusForCurveParameter(alignment::EditParam::LineLength, 10.0, 0.2, &r));
}

TEST_CASE("A free curve is fitted through a point and keeps that definition when saved", "[req481][alignment]") {
  AppCommandState st = Drawing();
  Alignment a;
  std::string built;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 500}, {400, 800}}, alignment::CurveSettings{true, 200.0, 0.0, 0.0}, &a, &built));
  a.name = "FC";
  REQUIRE(a.Entities()[1].constraint == alignment::Entity::Constraint::Free);
  // The middle of the same curve at a larger radius: the fit must find that radius again.
  alignment::Alignment bigger = a;
  REQUIRE(bigger.EditEntity(1, alignment::EditParam::CurveRadius, 260.0));
  const alignment::Point2 target = bigger.GetPointAtStation(bigger.EntityStartStation(1) + 0.5 * bigger.Entities()[1].length);
  REQUIRE(a.EditCurveThroughPoint(1, target));
  CHECK(a.Entities()[1].radius == Catch::Approx(260.0).epsilon(1e-9));
  CHECK(std::fabs(a.GetStationOffset(target.x, target.y).offset) <= 1e-7);
  CHECK(std::string(alignment::ParameterConstraintName(a.Entities()[1])) == "Through point (between tangents)");

  st.alignments.push_back(a);
  CHECK(Reopened(st).alignments[0].Entities()[1].sizeBy == alignment::Entity::SizeBy::ThroughPoint);

  std::string why;
  CHECK_FALSE(a.EditCurveThroughPoint(0, target, &why));  // a tangent
  CHECK(why.find("not a Free curve") != std::string::npos);
  CHECK_FALSE(a.EditCurveThroughPoint(1, {target.x + 5000.0, target.y - 5000.0}, &why));
}

TEST_CASE("A spiral can be entered by A", "[req481][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SA", log);
  Submit(st, "ALIGNMENTADD SA, TANGENT, 0, 0, 0, 100", log);
  Submit(st, "ALIGNMENTADD SA, SPIRAL, 60, 300, RIGHT", log);
  Submit(st, "ALIGNMENTEDIT SA, 2, A, 150", log);
  const Alignment& a = st.alignments[0];
  CHECK(a.Entities()[1].length == Catch::Approx(75.0).epsilon(1e-12));  // 150^2 / 300
  alignment::SpiralData sd;
  REQUIRE(alignment::SpiralDataFor(1, a, &sd));
  CHECK(sd.parameterA == Catch::Approx(150.0).epsilon(1e-12));
  CHECK(std::string(alignment::ParameterConstraintName(a.Entities()[1])) == "A, radii");
  log.clear();
  Submit(st, "ALIGNMENTEDIT SA, 1, A, 150", log);
  CHECK(LogHas(log, "is not a spiral"));
}

TEST_CASE("GETSTATIONOFFSET with only an alignment picks points until ESC", "[req484][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT PK", log);
  Submit(st, "ALIGNMENTADD PK, TANGENT, 0, 0, 0, 500", log);  // due north from the origin
  log.clear();
  Submit(st, "GETSTATIONOFFSET PK", log);
  CHECK(st.active == AppCommandState::Kind::StationOffsetPick);
  CHECK(LogHas(log, "pick a point"));
  log.clear();
  Submit(st, "10,120", log);
  CHECK(LogHas(log, "Station = 1+20.00"));
  CHECK(LogHas(log, "Offset = 10.00 (right)"));
  CHECK(st.active == AppCommandState::Kind::StationOffsetPick);  // ready for the next point
  log.clear();
  Submit(st, "-5,300", log);
  CHECK(LogHas(log, "Station = 3+00.00"));
  CHECK(LogHas(log, "(left)"));
  CancelActiveCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(LogHas(log, "GETSTATIONOFFSET ended."));
}
