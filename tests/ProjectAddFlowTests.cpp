// REQ-378 (issue #696 P5) — Add Drawing to Project end to end, and the whole-drawing Convert it
// offers (D-2026-10-05-g). Domain only: AppCommandState, real DWG files in a temp folder, CS-MAP; no
// window. The "original untouched" and "cancel changes nothing" claims are checked on disk.

#include "CadCommands.hpp"
#include "ConvertDrawing.hpp"
#include "DwgIo.hpp"
#include "ProjectAddFlow.hpp"
#include "ProjectPoints.hpp"
#include "ProjectSettings.hpp"
#include "geo/CoordinateSystems.hpp"
#include "geo/DrawingConversion.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Catch::Approx;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-addflow-test-") + stem);
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void LoadShippedDictionary() {
  if (!geo::DictionariesLoaded())
    REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
}

SurveyPoint Pt(int id, double e, double n, double z, const char* desc = "EG") {
  SurveyPoint p;
  p.id = id;
  p.easting = e;
  p.northing = n;
  p.elevation = z;
  p.description = desc;
  p.labelStyle = SurveyPointLabelStyle::None;  // no label MTEXT: these tests are about the points
  return p;
}

std::string Bytes(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

size_t FileCount(const fs::path& dir) {
  size_t n = 0;
  std::error_code ec;
  if (fs::exists(dir, ec))
    for (const auto& e : fs::recursive_directory_iterator(dir, ec))
      if (e.is_regular_file())
        ++n;
  return n;
}

/// A project in \p dir with its own folder layout, opened as session uid 7 in \p st.
AppCommandState::ProjectSession& OpenTestProject(AppCommandState& st, const fs::path& dir, ProjectSettings ps = {},
                                                 bool readOnly = false) {
  AppCommandState::ProjectSession s;
  s.uid = 7;
  std::string err;
  REQUIRE(gsproj::Create(dir, "Job", &s.project, &err));
  s.readOnly = readOnly;
  s.settings = std::make_shared<ProjectSettings>(ps);
  std::vector<std::string> log;
  OpenProjectPointDb(s, log);
  REQUIRE(s.points);
  st.openProjects.push_back(std::move(s));
  return st.openProjects.back();
}

/// Saves a drawing with \p points (local to origin (ox, oy)) and a line to \p file.
void SaveSourceDrawing(const fs::path& file, const std::vector<SurveyPoint>& points, int insUnits = 2,
                       const std::string& zone = std::string(), double ox = 0.0, double oy = 0.0,
                       float plotScale = 50.f) {
  AppCommandState d;
  d.drawingInsUnits = insUnits;
  d.drawingSettings.zoneCode = zone;
  d.worldDocumentOriginX = ox;
  d.worldDocumentOriginY = oy;
  d.modelUnitsPerPlottedInch = plotScale;
  d.surveyPoints = points;
  d.userLinesFlat = {0, 0, 0, 100, 0, 0};
  d.userLineAttrs.resize(1);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(d, file.u8string().c_str(), log));
}

}  // namespace

TEST_CASE("req378 summary, copy-in, tagging, rules; the original stays byte-identical", "[req378]") {
  LoadShippedDictionary();
  TempDir d("copyin");
  AppCommandState st;
  auto& s = OpenTestProject(st, d.path / "proj");
  const fs::path src = d.path / "EG.dwg";
  SaveSourceDrawing(src, {Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3)});
  const std::string before = Bytes(src);

  std::vector<std::string> log;
  AddDrawingPlan plan;
  PrepareAddDrawing(st, 7, src.u8string(), &plan, log);
  REQUIRE(plan.error.empty());
  CHECK(plan.summary.total == 3);
  CHECK(plan.summary.fresh == 3);
  CHECK_FALSE(plan.Blocked());
  // Preview wrote nothing anywhere.
  CHECK(FileCount(s.project.Folder() / "Drawings") == 0);
  CHECK(s.points->points.empty());

  AddDrawingResult res;
  REQUIRE(CommitAddDrawing(st, plan, 1.0, &res, log));
  CHECK(res.destRel == "Drawings/EG.dwg");
  CHECK(fs::exists(fs::u8path(res.destPath)));
  CHECK(Bytes(src) == before);  // the original is untouched
  REQUIRE(s.points->points.size() == 3);
  for (const auto& e : s.points->points)
    CHECK(e.sourceDrawing == "Drawings/EG.dwg");  // clause 3: tagged with the copied drawing
  CHECK(s.points->dirty);

  // The copy opens with its points owned by the project and rules that show exactly them.
  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(OpenDrawingDocument(back, res.destPath.c_str(), openLog));
  back.drawingTabs[1].projectUid = 7;
  CHECK(back.surveyPoints.empty());  // not duplicated inside the DWG
  CHECK(back.pointVisibility.idRanges == "1-3");
}

TEST_CASE("req378 conflicts: counts and each choice reach the database", "[req378]") {
  LoadShippedDictionary();
  TempDir d("conflicts");
  AppCommandState st;
  auto& s = OpenTestProject(st, d.path / "proj");
  s.points->points.push_back({Pt(1, 10, 10, 1), "Drawings/Old.dwg"});  // identical to the drawing's 1
  s.points->points.push_back({Pt(2, 99, 99, 2), "Drawings/Old.dwg"});  // differs
  s.points->points.push_back({Pt(3, 77, 77, 3), "Drawings/Old.dwg"});  // differs
  const fs::path src = d.path / "FG.dwg";
  SaveSourceDrawing(src, {Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3), Pt(4, 40, 40, 4)});

  std::vector<std::string> log;
  AddDrawingPlan plan;
  PrepareAddDrawing(st, 7, src.u8string(), &plan, log);
  REQUIRE(plan.error.empty());
  CHECK(plan.summary.total == 4);
  CHECK(plan.summary.fresh == 1);
  CHECK(plan.summary.identical == 1);
  CHECK(plan.summary.differing == 2);
  CHECK(plan.summary.Existing() == 3);

  plan.choices[2] = projadd::Choice::Overwrite;
  plan.choices[3] = projadd::Choice::Renumber;
  AddDrawingResult res;
  REQUIRE(CommitAddDrawing(st, plan, 1.0, &res, log));
  CHECK(res.outcome.added == 1);
  CHECK(res.outcome.shared == 1);
  CHECK(res.outcome.overwritten == 1);
  CHECK(res.outcome.renumbered == 1);
  const auto& pts = s.points->points;
  REQUIRE(pts.size() == 5);
  CHECK(pts[1].point.easting == 20.0);  // overwritten
  CHECK(pts[2].point.easting == 77.0);  // the project's own 3 is kept
  const auto renumbered = std::find_if(pts.begin(), pts.end(), [](const projpts::Entry& e) { return e.point.id == 5; });
  REQUIRE(renumbered != pts.end());  // the drawing's 3, renumbered above its own 4
  CHECK(renumbered->point.easting == 30.0);

  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(OpenDrawingDocument(back, res.destPath.c_str(), openLog));
  CHECK(back.pointVisibility.idRanges == "1-2,4-5");
}

TEST_CASE("req378 cancel changes nothing, and a read-only project refuses", "[req378]") {
  LoadShippedDictionary();
  TempDir d("cancel");
  AppCommandState st;
  auto& s = OpenTestProject(st, d.path / "proj");
  const fs::path src = d.path / "EG.dwg";
  SaveSourceDrawing(src, {Pt(1, 10, 10, 1)});
  const std::string before = Bytes(src);
  std::vector<std::string> log;
  {
    AddDrawingPlan plan;  // prepared, shown, then dropped = the user pressed Cancel
    PrepareAddDrawing(st, 7, src.u8string(), &plan, log);
    REQUIRE(plan.error.empty());
  }
  CHECK(FileCount(s.project.Folder() / "Drawings") == 0);
  CHECK(s.points->points.empty());
  CHECK_FALSE(s.points->dirty);
  CHECK(Bytes(src) == before);

  AppCommandState ro;
  auto& rs = OpenTestProject(ro, d.path / "romine", {}, /*readOnly=*/true);
  AddDrawingPlan plan;
  PrepareAddDrawing(ro, 7, src.u8string(), &plan, log);
  CHECK_FALSE(plan.error.empty());
  CHECK(plan.error.find("read-only") != std::string::npos);
  CHECK(rs.points->points.empty());

  AddDrawingPlan missing;
  PrepareAddDrawing(st, 7, (d.path / "nope.dwg").u8string(), &missing, log);
  CHECK_FALSE(missing.error.empty());
}

TEST_CASE("req378 the same drawing added twice gets a second file name", "[req378]") {
  LoadShippedDictionary();
  TempDir d("twice");
  AppCommandState st;
  OpenTestProject(st, d.path / "proj");
  const fs::path src = d.path / "EG.dwg";
  SaveSourceDrawing(src, {Pt(1, 10, 10, 1)});
  std::vector<std::string> log;
  AddDrawingResult a, b;
  AddDrawingPlan p1, p2;
  PrepareAddDrawing(st, 7, src.u8string(), &p1, log);
  REQUIRE(CommitAddDrawing(st, p1, 1.0, &a, log));
  PrepareAddDrawing(st, 7, src.u8string(), &p2, log);
  CHECK(p2.summary.identical == 1);  // the second add finds its point already in the project
  REQUIRE(CommitAddDrawing(st, p2, 2.0, &b, log));
  CHECK(a.destRel == "Drawings/EG.dwg");
  CHECK(b.destRel == "Drawings/EG (2).dwg");
  CHECK(st.openProjects[0].points->points.size() == 1);  // shared, not duplicated
  CHECK(b.outcome.shared == 1);
}

TEST_CASE("req378 settings that differ from the project's defaults become overrides", "[req378]") {
  LoadShippedDictionary();
  TempDir d("overrides");
  AppCommandState st;
  ProjectSettings ps;
  ps.hasDefaults = true;
  ps.plotScale = 50.f;
  OpenTestProject(st, d.path / "proj", ps);
  const fs::path src = d.path / "EG.dwg";
  SaveSourceDrawing(src, {Pt(1, 10, 10, 1)}, 2, "", 0, 0, /*plotScale=*/100.f);
  std::vector<std::string> log;
  AddDrawingPlan plan;
  PrepareAddDrawing(st, 7, src.u8string(), &plan, log);
  REQUIRE(plan.error.empty());
  REQUIRE(plan.overrides.size() == 1);
  CHECK(plan.overrides[0] == "plotScale");
  AddDrawingResult res;
  REQUIRE(CommitAddDrawing(st, plan, 1.0, &res, log));

  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(OpenDrawingDocument(back, res.destPath.c_str(), openLog));
  CHECK(back.drawingSettings.IsOverridden(ProjectDefaultKey::PlotScale));
  CHECK(back.modelUnitsPerPlottedInch == 100.f);
  CHECK_FALSE(back.drawingSettings.IsOverridden(ProjectDefaultKey::AngularUnits));
}

TEST_CASE("req378 a units mismatch blocks, then Convert scales the copy to the project's unit", "[req378]") {
  LoadShippedDictionary();
  TempDir d("units");
  AppCommandState st;
  ProjectSettings ps;
  ps.insUnits = 6;  // meters
  auto& s = OpenTestProject(st, d.path / "proj", ps);
  const fs::path src = d.path / "FEET.dwg";
  SaveSourceDrawing(src, {Pt(1, 100, 200, 10)}, /*insUnits=*/2);  // feet
  std::vector<std::string> log;
  AddDrawingPlan plan;
  PrepareAddDrawing(st, 7, src.u8string(), &plan, log);
  REQUIRE(plan.error.empty());
  CHECK(plan.conversion.unitsDiffer);
  CHECK_FALSE(plan.conversion.zoneDiffers);
  CHECK(plan.Blocked());
  CHECK(plan.CanConvert());

  AddDrawingResult res;
  CHECK_FALSE(CommitAddDrawing(st, plan, 1.0, &res, log));  // blocked: nothing happens
  CHECK(s.points->points.empty());
  CHECK(FileCount(s.project.Folder() / "Drawings") == 0);

  REQUIRE(ChooseAddConvert(st, &plan, log));
  CHECK_FALSE(plan.Blocked());
  CHECK(plan.drawing->drawingInsUnits == 6);
  const double ft = 1200.0 / 3937.0;  // the drawing's foot is the US survey foot
  REQUIRE(plan.pointsWorld.size() == 1);
  CHECK(plan.pointsWorld[0].easting == Approx(100 * ft).margin(1e-6));
  CHECK(plan.pointsWorld[0].northing == Approx(200 * ft).margin(1e-6));
  CHECK(plan.pointsWorld[0].elevation == Approx(10 * ft).margin(1e-6));  // a unit change scales heights too
  REQUIRE(CommitAddDrawing(st, plan, 1.0, &res, log));
  CHECK(s.points->points[0].point.easting == Approx(100 * ft).margin(1e-6));

  // The converted copy was re-saved, so its body agrees: the line (100 ft long) is 100 ft in metres.
  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(OpenDrawingDocument(back, res.destPath.c_str(), openLog));
  CHECK(back.drawingInsUnits == 6);
  REQUIRE(back.userLinesFlat.size() >= 6);
  CHECK(back.userLinesFlat[3] == Approx(100 * ft).margin(1e-6));
}

TEST_CASE("req378 a coordinate-system mismatch converts to the project's zone within tolerance", "[req378]") {
  LoadShippedDictionary();
  TempDir d("zone");
  AppCommandState st;
  ProjectSettings ps;
  ps.zoneCode = "HARN/TX.TX-N";  // Texas North, metres
  ps.insUnits = 6;
  OpenTestProject(st, d.path / "proj", ps);
  const double ox = 3115000.0, oy = 10077000.0;  // Texas Central (US feet), near Austin
  const fs::path src = d.path / "CENTRAL.dwg";
  SaveSourceDrawing(src, {Pt(1, 243.14, 391.26, 150.0), Pt(2, 743.14, 391.26, 160.0), Pt(3, 243.14, 891.26, 170.0)},
                    /*insUnits=*/2, "HARN/TX.TX-CF", ox, oy);
  std::vector<std::string> log;
  AddDrawingPlan plan;
  PrepareAddDrawing(st, 7, src.u8string(), &plan, log);
  REQUIRE(plan.error.empty());
  REQUIRE(plan.conversion.zoneDiffers);
  REQUIRE(plan.conversion.ok);
  CHECK(plan.conversion.residualMeters < geo::kMaxResidualMeters);
  CHECK(std::fabs(plan.conversion.transform.rotationRad) > 1e-4);  // grid convergence differs between zones
  REQUIRE(plan.CanConvert());
  REQUIRE(ChooseAddConvert(st, &plan, log));
  CHECK(plan.drawing->drawingSettings.zoneCode == "HARN/TX.TX-N");

  // Every converted point lies within the tolerance of CS-MAP's own answer for it.
  const std::vector<SurveyPoint> orig = {Pt(1, ox + 243.14, oy + 391.26, 150.0), Pt(2, ox + 743.14, oy + 391.26, 160.0),
                                         Pt(3, ox + 243.14, oy + 891.26, 170.0)};
  REQUIRE(plan.pointsWorld.size() == 3);
  const double ft = 1200.0 / 3937.0;
  for (size_t i = 0; i < orig.size(); ++i) {
    const geo::GeoResult ll = geo::GridToLatLong("HARN/TX.TX-CF", orig[i].easting, orig[i].northing);
    REQUIRE(ll.ok);
    const geo::GeoResult dt = geo::ConvertLatLong("HARN/TX.TX-CF", "HARN/TX.TX-N", ll.x, ll.y);
    REQUIRE(dt.ok);
    const geo::GeoResult g = geo::LatLongToGrid("HARN/TX.TX-N", dt.x, dt.y);
    REQUIRE(g.ok);
    CHECK(std::hypot(plan.pointsWorld[i].easting - g.x, plan.pointsWorld[i].northing - g.y) <
          geo::kMaxResidualMeters);
    CHECK(plan.pointsWorld[i].elevation == Approx(orig[i].elevation * ft).margin(1e-6));
  }
  // One similarity: every distance is the drawing's distance times the same scale (the grid factor of the
  // two zones here, times feet-to-metres), so shapes stay exact.
  const double dFt = std::hypot(orig[1].easting - orig[0].easting, orig[1].northing - orig[0].northing);
  const double dNew = std::hypot(plan.pointsWorld[1].easting - plan.pointsWorld[0].easting,
                                 plan.pointsWorld[1].northing - plan.pointsWorld[0].northing);
  CHECK(dNew == Approx(dFt * plan.conversion.transform.scale).epsilon(1e-9));
  CHECK(plan.conversion.transform.scale / ft == Approx(1.0).margin(0.01));  // a grid factor, not a blunder

  AddDrawingResult res;
  REQUIRE(CommitAddDrawing(st, plan, 1.0, &res, log));
  CHECK(st.openProjects[0].points->points.size() == 3);
}

TEST_CASE("req378 a drawing holding objects that cannot be converted is blocked and says which", "[req378]") {
  LoadShippedDictionary();
  TempDir d("blockers");
  AppCommandState st;
  ProjectSettings ps;
  ps.insUnits = 6;
  OpenTestProject(st, d.path / "proj", ps);

  AppCommandState src;
  src.drawingInsUnits = 2;
  src.userLinesFlat = {0, 0, 0, 10, 0, 0};
  src.userLineAttrs.resize(1);
  CadSurface surf;
  surf.name = "EG";
  src.cadSurfaces.push_back(surf);
  src.cadSurfaceAttrs.resize(1);
  const std::vector<std::string> kinds = UnconvertibleKinds(src, false);
  REQUIRE(kinds.size() == 1);
  CHECK(kinds[0] == "1 surface");

  // A tilted arc only blocks when the transform turns the drawing.
  AppCommandState arc;
  CadArc a;
  a.nx = 0.5f;
  a.nz = 0.866f;
  arc.userArcs.push_back(a);
  CHECK(UnconvertibleKinds(arc, false).empty());
  CHECK(UnconvertibleKinds(arc, true).size() == 1);
}

TEST_CASE("req378 Convert moves every supported object by the one transform", "[req378]") {
  AppCommandState st;
  st.worldDocumentOriginX = 1000.0;
  st.worldDocumentOriginY = 2000.0;
  st.userLinesFlat = {1, 2, 3, 11, 2, 3};
  st.userLineAttrs.resize(1);
  st.userCirclesCxCyZR = {5, 5, 1, 2};
  st.userCircleAttrs.resize(1);
  CadArc arc;
  arc.cx = 3;
  arc.cy = 4;
  arc.r = 2;
  arc.startRad = 0.5f;
  arc.sweepRad = 1.f;
  st.userArcs.push_back(arc);
  st.userArcAttrs.resize(1);
  st.surveyPoints = {Pt(1, 10, 20, 5)};
  CadBlockRef br;
  br.defName = "B";
  br.xf.x = 7;
  br.xf.y = 8;
  st.cadBlockRefs.push_back(br);
  st.cadBlockRefAttrs.resize(1);

  geo::Similarity t;
  t.scale = 2.0;
  t.rotationRad = 0.5;
  t.shiftX = 100.0;
  t.shiftY = -50.0;
  t.scaleZ = 2.0;
  auto world = [&](double lx, double ly) { return std::make_pair(lx + 1000.0, ly + 2000.0); };
  double ex, ey;
  t.Apply(world(11, 2).first, world(11, 2).second, &ex, &ey);  // where the line's far end must land

  std::vector<std::string> log;
  ApplyDrawingConversion(st, t, log);
  // The world-space similarity lands in world space; the storage frame is free to be anywhere.
  CHECK((st.userLinesFlat[3] + st.worldDocumentOriginX) == Approx(ex).margin(1e-6));
  CHECK((st.userLinesFlat[4] + st.worldDocumentOriginY) == Approx(ey).margin(1e-6));
  CHECK(st.userLinesFlat[5] == Approx(6.0));  // z scaled
  CHECK(st.userCirclesCxCyZR[3] == Approx(4.0));  // radius scaled by the plan scale
  CHECK(st.userArcs[0].r == Approx(4.0));
  CHECK(st.userArcs[0].startRad == Approx(0.5f + 0.5f));  // turned with the drawing
  t.Apply(10 + 1000.0, 20 + 2000.0, &ex, &ey);
  CHECK((st.surveyPoints[0].easting + st.worldDocumentOriginX) == Approx(ex).margin(1e-6));
  CHECK((st.surveyPoints[0].northing + st.worldDocumentOriginY) == Approx(ey).margin(1e-6));
  CHECK(st.surveyPoints[0].elevation == Approx(10.0));
  CHECK(st.cadBlockRefs[0].xf.sx == Approx(2.0));
  CHECK(st.cadBlockRefs[0].xf.rotZ == Approx(0.5));
}

TEST_CASE("req378 conversion planning: nothing to compare is not a mismatch", "[req378]") {
  geo::ConversionInput in;
  in.fromMetersPerUnit = 0.0;  // unitless drawing
  in.toMetersPerUnit = 1.0;
  CHECK_FALSE(geo::CompareSettings(in).Needed());
  in.fromMetersPerUnit = 0.3048;
  in.toMetersPerUnit = 0.0;  // the project fixes no unit
  CHECK_FALSE(geo::CompareSettings(in).Needed());
  in.toMetersPerUnit = 0.3048;
  in.fromZone = "HARN/TX.TX-C";
  CHECK_FALSE(geo::CompareSettings(in).Needed());  // the project has no zone
  in.toZone = "HARN/TX.TX-C";
  CHECK_FALSE(geo::CompareSettings(in).Needed());  // the same zone
  in.toZone = "HARN/TX.TX-N";
  CHECK(geo::CompareSettings(in).zoneDiffers);

  in = {};
  in.fromMetersPerUnit = 0.0254;
  in.toMetersPerUnit = 1.0;
  const geo::ConversionPlan p = geo::PlanConversion(in);
  CHECK(p.ok);
  CHECK(p.unitsDiffer);
  CHECK(p.transform.scale == Approx(0.0254));
  CHECK(p.transform.scaleZ == Approx(0.0254));
  CHECK(p.transform.rotationRad == 0.0);
}

TEST_CASE("req378 a conversion that one shift, turn and scale cannot do within tolerance is refused", "[req378]") {
  LoadShippedDictionary();
  geo::ConversionInput in;
  in.fromZone = "HARN/TX.TX-C";
  in.toZone = "HARN/TX.TX-N";
  in.fromMetersPerUnit = in.toMetersPerUnit = 1.0;
  const auto from = geo::FindCoordinateSystem(in.fromZone);
  REQUIRE(from);
  // A drawing the size of a state (about 800 km across): two different projections cannot agree to
  // 2 cm by one rigid-plus-scale transform over that distance.
  const geo::GeoResult c = geo::LatLongToGrid(in.fromZone, -100.33, 31.0);
  REQUIRE(c.ok);
  in.minX = c.x - 400000.0 / from->metersPerUnit;
  in.maxX = c.x + 400000.0 / from->metersPerUnit;
  in.minY = c.y - 400000.0 / from->metersPerUnit;
  in.maxY = c.y + 400000.0 / from->metersPerUnit;
  const geo::ConversionPlan p = geo::PlanConversion(in);
  CHECK_FALSE(p.ok);
  CHECK(p.residualMeters > geo::kMaxResidualMeters);
  CHECK(p.error.find("cannot be converted") != std::string::npos);
}
