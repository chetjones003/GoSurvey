// REQ-480..485, 496, 501 / GitHub issue #121 Phase 1 - alignment commands and persistence, driven through
// ProcessCommandLineSubmit (the path a typed command uses) and the document serializer.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "CadSnap.hpp"
#include "GsIo.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
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
  return st;
}

void PushLine(AppCommandState& st, double x0, double y0, double x1, double y1) {
  for (double v : {x0, y0, 0.0, x1, y1, 0.0}) st.userLinesFlat.push_back(v);
  st.userLineAttrs.push_back(EntityAttributes{});
}

}  // namespace

TEST_CASE("ALIGNMENT creates a named empty alignment with metadata", "[req480][req485][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Main Road, Two lane road, 5+00, 0, Standard", log);
  REQUIRE(st.alignments.size() == 1);
  const alignment::Alignment& a = st.alignments[0];
  CHECK(a.name == "Main Road");
  CHECK(a.description == "Two lane road");
  CHECK(a.startingStation == Approx(500.0));
  CHECK(a.layer == "0");
  CHECK(a.style == "Standard");
  CHECK(a.Empty());
  CHECK(a.id != 0);
  CHECK(LogHas(log, "created"));

  log.clear();
  Submit(st, "ALIGNMENT main road", log);  // names are unique, case-insensitively
  CHECK(st.alignments.size() == 1);
  CHECK(LogHas(log, "already exists"));
  log.clear();
  Submit(st, "ALIGNMENT Bad, , nonsense", log);
  CHECK(st.alignments.size() == 1);
  CHECK(LogHas(log, "not a station"));
}

TEST_CASE("ALIGNMENTADD builds tangents and curves and ALIGNMENTLIST reports them", "[req481][req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT A1", log);
  Submit(st, "ALIGNMENTADD A1, TANGENT, 1000, 2000, 45, 500", log);
  Submit(st, "ALIGNMENTADD A1, CURVE, 500, 15, RIGHT", log);
  Submit(st, "ALIGNMENTADD A1, TANGENT, 200", log);
  REQUIRE(st.alignments.size() == 1);
  const alignment::Alignment& a = st.alignments[0];
  std::string joined;
  for (const std::string& l : log) joined += l + " | ";
  INFO(joined);
  REQUIRE(a.Entities().size() == 3);
  CHECK(a.CurveCount() == 1);
  CHECK(a.Length() == Approx(500.0 + 500.0 * 15.0 * alignment::kPi / 180.0 + 200.0));

  log.clear();
  Submit(st, "ALIGNMENTLIST A1", log);
  CHECK(LogHas(log, "ALIGNMENT: A1"));
  CHECK(LogHas(log, "ENTITY 3"));
  CHECK(LogHas(log, "Curve right"));
  CHECK(LogHas(log, "Entities: 3"));

  log.clear();
  Submit(st, "ALIGNMENTADD A1, CURVE, -5, 15, LEFT", log);  // refused, nothing changes
  CHECK(LogHas(log, "refused"));
  CHECK(st.alignments[0].Entities().size() == 3);
  log.clear();
  Submit(st, "ALIGNMENTADD Nope, TANGENT, 5", log);
  CHECK(LogHas(log, "no alignment named"));
  log.clear();
  Submit(st, "ALIGNMENTADD A1, TANGENT, 0, 0, 90, 10", log);  // already has geometry
  CHECK(LogHas(log, "already has geometry"));
}

TEST_CASE("Station/offset commands answer in world coordinates", "[req484][alignment]") {
  AppCommandState st = Drawing();
  st.worldDocumentOriginX = 1'000'000.0;  // the local-storage invariant: world = local + origin (REQ-499)
  st.worldDocumentOriginY = 2'000'000.0;
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTADD Road, TANGENT, 1000500, 2000000, 90, 1000", log);  // due east from local (500, 0)
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].StartPoint().x == Approx(500.0).margin(1e-6));
  CHECK(st.alignments[0].StartPoint().y == Approx(0.0).margin(1e-6));

  log.clear();
  Submit(st, "GETSTATIONOFFSET Road, 1000800, 2000020", log);  // 300 along, 20 north of an eastbound road = LEFT
  REQUIRE(LogHas(log, "Station = 3+00.00"));
  CHECK(LogHas(log, "(left)"));
  CHECK(LogHas(log, "Elevation: no profile"));

  log.clear();
  Submit(st, "GETPOINTFROMSTATIONOFFSET Road, 3+00, -20", log);  // offset -20 = left
  CHECK(LogHas(log, "X = 1000800.00"));
  CHECK(LogHas(log, "Y = 2000020.00"));

  log.clear();
  Submit(st, "GETPOINTFROMSTATIONOFFSET Road, 99+00, 0", log);  // beyond the end
  CHECK(LogHas(log, "outside"));
  log.clear();
  Submit(st, "GETSTATIONOFFSET Road, 1000800", log);
  CHECK(LogHas(log, "usage"));
}

TEST_CASE("CREATEALIGNMENTFROMOBJECTS converts selected lines and refuses gaps", "[req485][alignment]") {
  AppCommandState st = Drawing();
  PushLine(st, 0, 0, 0, 100);
  PushLine(st, 0, 100, 100, 100);
  for (int i = 0; i < 2; ++i) {
    SelectedEntity se;
    se.type = SelectedEntity::Type::LineSeg;
    se.index = i;
    st.selection.push_back(se);
  }
  std::vector<std::string> log;
  Submit(st, "CREATEALIGNMENTFROMOBJECTS FromLines, START=1+00, FILLET=20", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].Entities().size() == 3);  // tangent, fillet curve, tangent
  CHECK(st.alignments[0].CurveCount() == 1);
  CHECK(st.alignments[0].startingStation == Approx(100.0));
  CHECK(LogHas(log, "created"));

  // A line 2 units away from the chain is refused with its object number, and nothing is created. Creating clears the
  // selection (as Civil 3D does), so the chain is selected again.
  CHECK(st.selection.empty());
  for (int i = 0; i < 2; ++i) {
    SelectedEntity se;
    se.type = SelectedEntity::Type::LineSeg;
    se.index = i;
    st.selection.push_back(se);
  }
  PushLine(st, 102, 100, 200, 100);
  SelectedEntity far;
  far.type = SelectedEntity::Type::LineSeg;
  far.index = 2;
  st.selection.push_back(far);
  log.clear();
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Gapped", log);
  CHECK(st.alignments.size() == 1);
  CHECK(LogHas(log, "No alignment was created"));

  st.selection.clear();
  log.clear();
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Empty", log);
  CHECK(LogHas(log, "select"));
}

TEST_CASE("Alignments survive a document save and reload exactly", "[req496][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Persist, A description, 2+50, 0, Standard", log);
  Submit(st, "ALIGNMENTADD Persist, TANGENT, 6500000.123456, 2100000.654321, 33.5, 750.25", log);
  Submit(st, "ALIGNMENTADD Persist, CURVE, 612.5, 41.25, LEFT", log);
  Submit(st, "ALIGNMENTADD Persist, TANGENT, 100", log);
  Submit(st, "ALIGNMENT Empty One", log);
  REQUIRE(st.alignments.size() == 2);
  alignment::StationEquation eq;
  eq.rawBack = 600.0;
  eq.ahead = 700.0;
  eq.increasing = true;
  REQUIRE(st.alignments[0].AddStationEquation(eq));

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 2);

  const alignment::Alignment& a = st.alignments[0];
  const alignment::Alignment& b = back.alignments[0];
  CHECK(b.name == a.name);
  CHECK(b.description == a.description);
  CHECK(b.layer == a.layer);
  CHECK(b.style == a.style);
  CHECK(b.id == a.id);
  CHECK(b.startingStation == a.startingStation);
  REQUIRE(b.Entities().size() == a.Entities().size());
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    CHECK(b.Entities()[i].start.x == a.Entities()[i].start.x);  // bit-exact: doubles round-trip losslessly
    CHECK(b.Entities()[i].start.y == a.Entities()[i].start.y);
    CHECK(b.Entities()[i].startAzimuth == a.Entities()[i].startAzimuth);
    CHECK(b.Entities()[i].length == a.Entities()[i].length);
    CHECK(b.Entities()[i].radius == a.Entities()[i].radius);
    CHECK(b.Entities()[i].right == a.Entities()[i].right);
  }
  REQUIRE(b.equations.size() == 1);
  CHECK(b.equations[0].ahead == 700.0);
  CHECK(back.alignments[1].name == "Empty One");
  CHECK(back.alignments[1].Empty());

  // A legacy document with no alignments loads with none.
  AppCommandState plain = Drawing();
  const std::string plainJson = SerializeGoSurveyJson(plain);
  CHECK(plainJson.find("\"alignments\"") == std::string::npos);
  AppCommandState loaded = Drawing();
  loaded.alignments.push_back(alignment::Alignment{});
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, plainJson, loadLog));
  CHECK(loaded.alignments.empty());
}

TEST_CASE("Alignment edits undo in one step", "[req480][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT U1", log);
  Submit(st, "ALIGNMENTADD U1, TANGENT, 0, 0, 0, 100", log);
  Submit(st, "UNDO", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].Empty());
  Submit(st, "UNDO", log);
  CHECK(st.alignments.empty());
}

TEST_CASE("Alignment geometry points snap: PC, PT, PI, centre and nearest", "[req490][alignment]") {
  using Kind = CadSnap::Kind;
  AppCommandState st = Drawing();
  st.objectSnapEnabled = true;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = true;
  st.objectSnapCenter = true;
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT S1", log);
  Submit(st, "ALIGNMENTADD S1, TANGENT, 0, 0, 0, 100", log);   // due north, ends (0,100)
  Submit(st, "ALIGNMENTADD S1, CURVE, 100, 90, RIGHT", log);   // quarter circle, centre (100,100), PT (100,200)
  REQUIRE(st.alignments.size() == 1);
  const float tol = 2.0f;
  const auto pc = CadSnap::FindBest(0.5f, 100.5f, st, true, tol);
  REQUIRE(pc.valid);
  CHECK(pc.kind == Kind::Endpoint);
  CHECK(pc.x == Approx(0.0f).margin(1e-4));
  CHECK(pc.y == Approx(100.0f).margin(1e-4));
  const auto pt = CadSnap::FindBest(100.3f, 199.6f, st, true, tol);
  REQUIRE(pt.valid);
  CHECK(pt.x == Approx(100.0f).margin(1e-3));
  CHECK(pt.y == Approx(200.0f).margin(1e-3));
  const auto pi = CadSnap::FindBest(0.4f, 199.7f, st, true, tol);  // the PI: tangents meet at (0, 200), no geometry there
  REQUIRE(pi.valid);
  CHECK(pi.x == Approx(0.0f).margin(1e-3));
  CHECK(pi.y == Approx(200.0f).margin(1e-3));
  const auto centre = CadSnap::FindBest(100.2f, 100.2f, st, true, tol);
  REQUIRE(centre.valid);
  CHECK(centre.kind == Kind::Center);
  CHECK(centre.x == Approx(100.0f).margin(1e-3));
  CHECK(centre.y == Approx(100.0f).margin(1e-3));
}

TEST_CASE("STATION snap resolves to GetPointAtStation", "[req490][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT STA", log);
  Submit(st, "ALIGNMENTADD STA, TANGENT, 0, 0, 0, 100", log);
  REQUIRE(st.alignments.size() == 1);
  const alignment::Alignment& al = st.alignments[0];
  const alignment::Point2 expected = al.GetPointAtStation(50.0);
  float lx = 0.f;
  float ly = 0.f;
  REQUIRE(TryParseAlignmentStationSnapStoragePoint(st, "STATION 0+50", &lx, &ly, nullptr, false, 0.f, 0.f));
  CHECK(lx == Approx(static_cast<float>(expected.x)).margin(1e-4f));
  CHECK(ly == Approx(static_cast<float>(expected.y)).margin(1e-4f));
  CHECK(st.objectSnapStationDisplayedValid);
  CHECK(st.objectSnapStationDisplayed == Approx(50.0).margin(1e-9));

  st.objectSnapEnabled = true;
  st.objectSnapStation = true;
  const CadSnap::Kind onlyStation = CadSnap::Kind::Station;
  const auto hit = CadSnap::FindBest(static_cast<float>(expected.x), static_cast<float>(expected.y + 0.5f), st, true,
                                     2.0f, {}, nullptr, &onlyStation);
  REQUIRE(hit.valid);
  CHECK(hit.kind == CadSnap::Kind::Station);
  CHECK(hit.x == Approx(static_cast<float>(expected.x)).margin(1e-3f));
  CHECK(hit.y == Approx(static_cast<float>(expected.y)).margin(1e-3f));
}

TEST_CASE("Alignment quadrant snap on cardinal tangents only, not on curves", "[req490][alignment]") {
  using Kind = CadSnap::Kind;
  AppCommandState st = Drawing();
  st.objectSnapEnabled = true;
  st.objectSnapQuadrant = true;
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Q490", log);
  Submit(st, "ALIGNMENTADD Q490, TANGENT, 0, 0, 90, 200", log);  // due east (+X)
  Submit(st, "ALIGNMENTADD Q490, CURVE, 100, 90, RIGHT", log);
  REQUIRE(st.alignments.size() == 1);
  const Kind onlyQuadrant = Kind::Quadrant;
  const float tol = 2.0f;
  const auto onTangent = CadSnap::FindBest(100.0f, 0.4f, st, true, tol, {}, nullptr, &onlyQuadrant);
  REQUIRE(onTangent.valid);
  CHECK(onTangent.kind == Kind::Quadrant);
  CHECK(onTangent.x == Approx(100.0f).margin(1e-3));
  CHECK(onTangent.y == Approx(0.0f).margin(1e-3));
  const alignment::Alignment& al = st.alignments[0];
  REQUIRE(al.Entities().size() == 2);
  const double midSta = al.EntityStartStation(1) + al.Entities()[1].length * 0.5;
  const alignment::Point2 midCurve = al.GetPointAtStation(midSta);
  const auto onCurve = CadSnap::FindBest(static_cast<float>(midCurve.x), static_cast<float>(midCurve.y + 0.4), st,
                                         true, tol, {}, nullptr, &onlyQuadrant);
  CHECK_FALSE(onCurve.valid);
}

TEST_CASE("Intersection snap hits crossing alignments and alignment-line crosses", "[req490][alignment]") {
  using Kind = CadSnap::Kind;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = false;
  st.objectSnapCenter = false;
  st.objectSnapIntersection = true;
  Submit(st, "ALIGNMENT EW", log);
  Submit(st, "ALIGNMENTADD EW, TANGENT, -50, 0, 90, 100", log);
  Submit(st, "ALIGNMENT NS", log);
  Submit(st, "ALIGNMENTADD NS, TANGENT, 25, -50, 0, 100", log);
  const float tol = 2.0f;
  const Kind onlyInt = Kind::Intersection;
  const auto aa = CadSnap::FindBest(25.0f, 0.4f, st, true, tol, {}, nullptr, &onlyInt);
  REQUIRE(aa.valid);
  CHECK(aa.kind == Kind::Intersection);
  CHECK(aa.x == Catch::Approx(25.0).margin(0.05));
  CHECK(aa.y == Catch::Approx(0.0).margin(0.05));

  PushLine(st, 0.0, 40.0, 50.0, 40.0);
  const auto al = CadSnap::FindBest(25.0f, 40.4f, st, true, tol, {}, nullptr, &onlyInt);
  REQUIRE(al.valid);
  CHECK(al.kind == Kind::Intersection);
  CHECK(al.x == Catch::Approx(25.0).margin(0.05));
  CHECK(al.y == Catch::Approx(40.0).margin(0.05));
}

TEST_CASE("An alignment can be picked, boxed, listed and erased", "[req480][req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT P1", log);
  Submit(st, "ALIGNMENTADD P1, TANGENT, 0, 0, 90, 100", log);   // due east along y = 0
  Submit(st, "ALIGNMENTADD P1, CURVE, 50, 90, LEFT", log);      // curves north, centre (100, 50)
  REQUIRE(st.alignments.size() == 1);

  SelectedEntity hit{};
  float pickD2 = 0.f;
  REQUIRE(PickClosestCadEntity(st, 50.0, 0.4, 1.0f, &hit, &pickD2));  // on the tangent, 0.4 off
  CHECK(hit.type == SelectedEntity::Type::Alignment);
  CHECK(hit.index == 0);
  CHECK_FALSE(PickClosestCadEntity(st, 50.0, 5.0, 1.0f, &hit, &pickD2));  // far away
  // A click on the CURVE (the exact arc, not a chord): 50 from the centre at 45 deg of the sweep.
  const double cx = 100.0 + 50.0 * std::sin(45.0 * alignment::kPi / 180.0);
  const double cy = 50.0 - 50.0 * std::cos(45.0 * alignment::kPi / 180.0);
  REQUIRE(PickClosestCadEntity(st, cx, cy, 0.5f, &hit, &pickD2));
  CHECK(hit.type == SelectedEntity::Type::Alignment);

  st.selection.push_back(hit);
  log.clear();
  Submit(st, "LIST", log);
  CHECK(LogHas(log, "ALIGNMENT"));
  CHECK(LogHas(log, "P1"));

  Submit(st, "ERASE", log);
  CHECK(st.alignments.empty());
  Submit(st, "UNDO", log);
  CHECK(st.alignments.size() == 1);  // erase is one undo step
}
