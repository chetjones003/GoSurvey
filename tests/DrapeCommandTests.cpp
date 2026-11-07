// GitHub issue #150 (3D Phase 7) — DRAPE lays geometry on the ground.
//
// Driven through ProcessCommandLineSubmit, the way a user types it, so the dispatch wiring is under
// test alongside the geometry. The surface is a TIN whose elevation is an exact plane, z = 2 + x/10,
// so every draped vertex has a hand-computable answer and REQ-101's +/-0.002 ft is a real assertion
// rather than a comparison against the same code that produced it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <filesystem>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "DwgIo.hpp"
#include "commands/ProjectFiles.hpp"
#include "Project.hpp"

using Catch::Approx;

namespace {

/// A square of ground from (0,0) to (100,100), two triangles, tilted so that z = 2 + x/10.
/// Flat in Y, so a vertex's elevation depends only on its X — easy to state and easy to check.
std::shared_ptr<CadTin> SlopingGround() {
  auto tin = std::make_shared<CadTin>();
  const double z0 = 2.0;
  tin->vertsXyz = {
      0.0,   0.0,   z0 + 0.0,    // 0
      100.0, 0.0,   z0 + 10.0,   // 1
      100.0, 100.0, z0 + 10.0,   // 2
      0.0,   100.0, z0 + 0.0,    // 3
  };
  tin->indices = {0, 1, 2, 0, 2, 3};
  return tin;
}

/// The elevation the plane above gives at \p x — what a correct drape must land on.
double GroundZ(double x) { return 2.0 + x / 10.0; }

void AddGround(AppCommandState& st, const std::string& name) {
  CadSurface s;
  s.name = name;
  s.tin = SlopingGround();
  st.cadSurfaces.push_back(std::move(s));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
}

/// A polyline of \p n vertices along y = 50, all at elevation 0, spaced across the ground.
void AddFlatPolylineAcross(AppCommandState& st, const std::vector<double>& xs) {
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  for (double x : xs) {
    st.userPolylineVerts.push_back(x);
    st.userPolylineVerts.push_back(50.0);
    st.userPolylineVerts.push_back(0.0);
  }
  st.userPolylineOffsets.push_back(static_cast<int>(st.userPolylineVerts.size() / 3));
  st.userPolylineClosed.push_back(0);
  st.userPolylineAttrs.push_back(EntityAttributes{});
}

void Type(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), st, log);
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& l : log)
    if (l.find(needle) != std::string::npos)
      return true;
  return false;
}

}  // namespace

TEST_CASE("DRAPE puts every vertex on the ground under it", "[drape][issue150][phase7]") {
  AppCommandState st;
  AddGround(st, "EG");
  const std::vector<double> xs = {0.0, 10.0, 25.0, 60.0, 100.0};
  AddFlatPolylineAcross(st, xs);
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);

  INFO("last log line: " << (log.empty() ? std::string("(none)") : log.back()));
  REQUIRE(st.userPolylineVerts.size() == xs.size() * 3);
  for (size_t i = 0; i < xs.size(); ++i) {
    INFO("vertex " << i << " at x = " << xs[i]);
    // REQ-101: +/-0.002 ft against the plane's own arithmetic, not against the drape's.
    CHECK(st.userPolylineVerts[i * 3 + 2] == Approx(GroundZ(xs[i])).margin(0.002));
    CHECK(st.userPolylineVerts[i * 3] == Approx(xs[i]));      // X unmoved
    CHECK(st.userPolylineVerts[i * 3 + 1] == Approx(50.0));   // Y unmoved
  }
  CHECK(LogHas(log, "draped onto \"EG\""));
}

TEST_CASE("DRAPE is one undo step", "[drape][issue150][phase7]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0, 30.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);
  REQUIRE(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));

  DoUndo(st, log);
  for (int i = 0; i < 3; ++i) {
    INFO("vertex " << i);
    CHECK(st.userPolylineVerts[static_cast<size_t>(i) * 3 + 2] == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("Move to Surface moves selected block references atomically", "[surface][req512]") {
  AppCommandState st;
  AddGround(st, "EG");
  CadBlockRef onGround;
  onGround.defName = "Marker";
  onGround.xf.x = 10.f;
  onGround.xf.y = 50.f;
  onGround.xf.z = -5.f;
  st.cadBlockRefs.push_back(onGround);
  CadBlockRef outside = onGround;
  outside.xf.x = 110.f;
  st.cadBlockRefs.push_back(outside);
  st.selection.push_back({SelectedEntity::Type::BlockRef, 0});
  st.selection.push_back({SelectedEntity::Type::BlockRef, 1});
  std::vector<std::string> log;

  CHECK_FALSE(MoveSelectedBlockRefsToSurface(st, 0, log));
  CHECK(st.cadBlockRefs[0].xf.z == -5.f);
  CHECK(st.cadBlockRefs[1].xf.z == -5.f);
  CHECK(LogHas(log, "no blocks were moved"));

  st.selection.pop_back();
  log.clear();
  st.activeDocSavedRevision = st.cadGpuRevision;
  REQUIRE(MoveSelectedBlockRefsToSurface(st, 0, log));
  CHECK(st.cadBlockRefs[0].xf.z == Approx(3.f));
  CHECK(st.cadBlockRefs[0].xf.x == 10.f);
  CHECK(st.cadBlockRefs[0].xf.y == 50.f);
  CHECK(st.cadGpuRevision != st.activeDocSavedRevision);
  CHECK(LogHas(log, "moved 1 block reference"));
}

TEST_CASE("Surface LOD choice persists in the drawing document", "[surface][req508]") {
  AppCommandState source;
  AddGround(source, "EG");
  source.activeDocSavedRevision = source.cadGpuRevision;
  REQUIRE(SetSurfaceDisplayLod(source, 0, true));
  CHECK(source.cadGpuRevision != source.activeDocSavedRevision);
  CHECK_FALSE(SetSurfaceDisplayLod(source, 0, true));
  const std::string json = SerializeGoSurveyJson(source);
  AppCommandState reopened;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, json, log));
  REQUIRE(reopened.cadSurfaces.size() == 1);
  CHECK(reopened.cadSurfaces[0].reducedDisplayLod);
}

TEST_CASE("Drape Image refuses unreadable files and persists a stable surface association", "[surface][req511]") {
  AppCommandState st;
  AddGround(st, "EG");
  st.cadSurfaceAttrs[0].id = 42;
  std::vector<std::string> log;
  CHECK_FALSE(AttachModelRasterImage(st, "missing-issue-807-image.png", 0, 0, 10, 10, 0, log));
  CHECK(st.cadRasterImages.empty());
  st.currentLayer = "Imagery";
  const std::string validImage = std::string(GOSURVEY_SAMPLES_DIR) + "/01-main-window.png";
  REQUIRE(AttachModelRasterImage(st, validImage, 2, 3, 40, 30, 0.25f, log));
  REQUIRE(st.cadRasterImages.size() == 1);
  REQUIRE(st.cadRasterImages[0].id != 0);
  REQUIRE(SetModelRasterDrape(st, 0, 0, log));
  REQUIRE(st.cadSurfaceAttrs[0].id != 0);
  CHECK(st.cadRasterImages[0].surfaceId == st.cadSurfaceAttrs[0].id);

  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, SerializeGoSurveyJson(st), log));
  REQUIRE(reopened.cadRasterImages.size() == 1);
  CHECK(reopened.cadRasterImages[0].id == st.cadRasterImages[0].id);
  CHECK(reopened.cadRasterImages[0].surfaceId == st.cadSurfaceAttrs[0].id);
  CHECK(reopened.cadRasterImages[0].path == validImage);
  CHECK(reopened.cadRasterImages[0].layer == "Imagery");
  CHECK(reopened.cadRasterImages[0].insX == Approx(2.f));
  CHECK(reopened.cadRasterImages[0].width == Approx(40.f));
  CHECK(reopened.cadRasterImages[0].rotationRad == Approx(0.25f));
}

TEST_CASE("Project data shortcuts create read-only references and refresh from saved source drawings", "[surface][req514]") {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "gosurvey-issue807-shortcut-tests";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  gsproj::Project project;
  std::string error;
  REQUIRE(gsproj::Create(root, "Issue807", &project, &error));
  const fs::path drawingPath = project.Folder() / "drawings" / "Source.dwg";
  fs::create_directories(drawingPath.parent_path(), ec);

  AppCommandState source;
  source.activeDrawingIdx = 1;
  source.activeDocFilePath = drawingPath.u8string();
  source.worldDocumentOriginX = 10000.0;
  source.worldDocumentOriginY = 20000.0;
  AddGround(source, "EG");
  source.cadSurfaceAttrs[0].id = 4200;
  std::vector<std::string> log;
  REQUIRE(SaveDrawingDocument(source, drawingPath.u8string().c_str(), log));
  source.activeDocSavedRevision = source.cadGpuRevision;

  AppCommandState::ProjectSession session;
  session.uid = 7;
  session.project = project;
  source.openProjects.push_back(session);
  source.drawingTabs[1].projectUid = 7;
  source.cadSurfaces[0].name = "Unsaved EG";
  BumpCadGpuCache(source);
  CHECK_FALSE(CreateSurfaceDataShortcut(source, 0, log));
  CHECK(LogHas(log, "save the source drawing and its surface changes"));
  REQUIRE(SaveDrawingDocument(source, drawingPath.u8string().c_str(), log));
  source.activeDocSavedRevision = source.cadGpuRevision;
  const std::uint64_t savedSurfaceId = source.cadSurfaceAttrs[0].id;
  source.cadSurfaceAttrs[0].id = 0;
  CHECK_FALSE(CreateSurfaceDataShortcut(source, 0, log));
  CHECK(LogHas(log, "no saved stable identity"));
  source.cadSurfaceAttrs[0].id = savedSurfaceId;
  REQUIRE(CreateSurfaceDataShortcut(source, 0, log));

  AppCommandState reference;
  reference.activeDrawingIdx = 1;
  reference.worldDocumentOriginX = 5000.0;
  reference.worldDocumentOriginY = 7000.0;
  reference.drawingTabs[1].projectUid = 7;
  reference.openProjects.push_back(source.openProjects[0]);
  const bool referenceCreated = CreateSurfaceReference(reference, "drawings/Source.dwg", 4200, "Existing Ground", log);
  if (!referenceCreated) for (const std::string& entry : log) UNSCOPED_INFO(entry);
  REQUIRE(referenceCreated);
  REQUIRE(reference.cadSurfaces.size() == 1);
  CHECK(reference.cadSurfaces[0].dataShortcutReadOnly);
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[0] == Approx(5000.0));
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[1] == Approx(13000.0));
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[2] == Approx(2.0));
  CHECK(reference.cadSurfaces[0].breaklines.empty());
  Type(reference, "SURFLABEL Existing Ground, SPOT, 10050, 20050", log);
  REQUIRE(reference.cadAnnotations.size() == 1);
  CHECK(reference.cadAnnotations[0].text == "Elev 7.0000");
  alignment::Alignment referenceAlignment;
  referenceAlignment.id = 7301;
  referenceAlignment.name = "BASELINE";
  REQUIRE(referenceAlignment.AppendTangent({5000.0, 13050.0}, 90.0 * alignment::kPi / 180.0, 100.0));
  reference.alignments.push_back(referenceAlignment);
  Type(reference, "CREATEPROFILEFROMSURFACE Existing Ground, BASELINE, Reference Profile, 25, 10000, 20000, 1, 2", log);
  REQUIRE(reference.surfaceProfiles.size() == 1);
  CHECK(reference.surfaceProfiles[0].elevations[2] == Approx(7.0));
  CadTable staleLegend;
  staleLegend.cols = 1;
  staleLegend.surfaceLegendForId = reference.cadSurfaceAttrs[0].id;
  staleLegend.surfaceLegendKind = "watershed";
  staleLegend.cells = {"stale"};
  reference.cadTables.push_back(staleLegend);
  reference.cadTableAttrs.push_back(EntityAttributes{});
  AddFlatPolylineAcross(reference, {5025.0, 5050.0, 5075.0});
  for (size_t i = 1; i < reference.userPolylineVerts.size(); i += 3) reference.userPolylineVerts[i] += 13000.0f;
  reference.selection.push_back({SelectedEntity::Type::Polyline, 0});
  Type(reference, "DRAPE Existing Ground, LINK", log);
  CHECK(reference.userPolylineVerts[5] == Approx(7.0));
  const auto heldTin = reference.cadSurfaces[0].tin;
  CHECK_FALSE(BuildSurfaceFromSources(reference, reference.cadSurfaces[0], log));
  CHECK(reference.cadSurfaces[0].tin == heldTin);

  source.cadSurfaces[0].tin = std::make_shared<CadTin>(*source.cadSurfaces[0].tin);
  auto changedTin = std::const_pointer_cast<CadTin>(source.cadSurfaces[0].tin);
  changedTin->vertsXyz[2] = 12.0;
  REQUIRE(SaveDrawingDocument(source, drawingPath.u8string().c_str(), log));
  reference.openProjects[0].readOnly = true;
  RefreshProjectSurfaceReferences(reference, 7, log);
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[2] == Approx(2.0));
  CHECK(LogHas(log, "project is read-only"));
  reference.openProjects[0].readOnly = false;
  RefreshProjectSurfaceReferences(reference, 7, log);
  CHECK(reference.cadSurfaces[0].name == "Existing Ground");
  CHECK(reference.cadSurfaces[0].dataShortcutReadOnly);
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[0] == Approx(5000.0));
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[2] == Approx(12.0));
  CHECK(reference.cadAnnotations[0].text == "Elev 12.0000");
  CHECK(reference.surfaceProfiles[0].elevations[2] == Approx(12.0));
  CHECK(reference.cadTables[0].cells[0] == "Range");
  CHECK(reference.userPolylineVerts[5] == Approx(12.0));
  TickSurfaceRebuilds(reference, log);
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[2] == Approx(12.0));
  const double lastGoodElevation = reference.cadSurfaces[0].tin->vertsXyz[2];
  fs::remove(drawingPath, ec);
  RefreshProjectSurfaceReferences(reference, 7, log);
  CHECK(reference.cadSurfaces[0].tin->vertsXyz[2] == Approx(lastGoodElevation));
  CHECK(LogHas(log, "last saved display retained"));
  fs::remove_all(root, ec);
}

TEST_CASE("Surface LOD and Move to Surface have typed command routes", "[surface][req508][req512]") {
  AppCommandState st;
  AddGround(st, "EG");
  std::vector<std::string> log;

  Type(st, "SURFLODLOW EG", log);
  CHECK(st.cadSurfaces[0].reducedDisplayLod);
  Type(st, "SURFLODHIGH EG", log);
  CHECK_FALSE(st.cadSurfaces[0].reducedDisplayLod);

  CadBlockRef block;
  block.defName = "Marker";
  block.xf.x = 50.f;
  block.xf.y = 50.f;
  block.xf.z = -10.f;
  st.cadBlockRefs.push_back(block);
  st.selection.push_back({SelectedEntity::Type::BlockRef, 0});
  Type(st, "MOVETOSURFACE EG", log);
  CHECK(st.cadBlockRefs[0].xf.z == Approx(7.f));
  CHECK(LogHas(log, "moved 1 block reference"));
}

TEST_CASE("Surface LOD changes only the generated viewport triangle geometry", "[surface][req508]") {
  AppCommandState st;
  AddGround(st, "EG");
  SurfaceStyles::EnsureStandard(st.surfaceStyles);
  st.surfaceStyles[0].triangles.visible = true;
  st.cadSurfaces[0].styleName = st.surfaceStyles[0].name;
  EnsureEntityIds(st);
  RefreshSurfaceDisplayGeometry(st);
  REQUIRE(st.surfaceDisplayCache.size() == 1);
  const auto fullDetailEdges = st.surfaceDisplayCache[0].triangleEdges;
  REQUIRE(!fullDetailEdges.empty());

  std::vector<std::string> log;
  Type(st, "SURFLODLOW EG", log);
  RefreshSurfaceDisplayGeometry(st);
  REQUIRE(st.surfaceDisplayCache.size() == 1);
  CHECK(st.surfaceDisplayCache[0].triangleEdges.size() < fullDetailEdges.size());
  CHECK(st.cadSurfaces[0].tin->indices.size() == 6);

  Type(st, "SURFLODHIGH EG", log);
  RefreshSurfaceDisplayGeometry(st);
  CHECK(st.surfaceDisplayCache[0].triangleEdges == fullDetailEdges);
  CHECK(st.cadSurfaces[0].tin->indices.size() == 6);
}

TEST_CASE("Surface visibility commands draw LOS and ZVI results without changing the TIN",
          "[surface][req510]") {
  AppCommandState st;
  AddGround(st, "EG");
  const auto tin = st.cadSurfaces[0].tin;
  std::vector<std::string> log;
  Type(st, "SURFACEVISIBILITY EG, LOS, 10, 50, 1, 90, 50, 1", log);
  CHECK(st.userLinesFlat.size() == 6);
  CHECK(LogHas(log, "target is visible"));
  CHECK(st.cadSurfaces[0].tin == tin);

  const size_t before = st.cadFilledRegions.size();
  Type(st, "SURFACEVISIBILITY EG, ZVI, 50, 50, 2, 1, 10", log);
  CHECK(st.cadFilledRegions.size() > before);
  CHECK(LogHas(log, "ZVI"));
  CHECK(st.cadSurfaces[0].tin == tin);
  for (const CadFilledRegion& region : st.cadFilledRegions)
    for (size_t i = 0; i + 2 < region.vertsXyz.size(); i += 3)
      CHECK(std::hypot(region.vertsXyz[i] - 50.0, region.vertsXyz[i + 1] - 50.0) <= 10.0 + 1.e-8);
}

TEST_CASE("Surface visibility refuses a queryable grid surface without a TIN", "[surface][req510]") {
  AppCommandState st;
  CadSurface surface;
  surface.name = "Grid";
  surface.kind = SurfaceKind::Grid;
  surface.gridOriginX = 0.0;
  surface.gridOriginY = 0.0;
  surface.gridSpacingX = 1.0;
  surface.gridSpacingY = 1.0;
  surface.gridCols = 2;
  surface.gridRows = 2;
  surface.gridZ = {1.0, 1.0, 1.0, 1.0};
  st.cadSurfaces.push_back(surface);
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  Type(st, "SURFACEVISIBILITY Grid, LOS, 0, 0, 1, 1, 1, 1", log);
  CHECK(st.userLinesFlat.empty());
  CHECK(LogHas(log, "no built TIN"));
}

TEST_CASE("Linked surface legends become unavailable after their source is deleted", "[surface][req507]") {
  AppCommandState st;
  AddGround(st, "EG");
  st.surfaceStyles[0].analysisMode = SurfaceAnalysisMode::Elevation;
  st.surfaceStyles[0].bands = {{5.0, "#00ff00"}};
  st.cadSurfaces[0].styleName = st.surfaceStyles[0].name;
  std::vector<std::string> log;
  Type(st, "SURFACELEGEND EG, ELEVATION, 10, 10", log);
  REQUIRE(st.cadTables.size() == 1);
  REQUIRE(st.cadTables[0].cells.size() > 3);
  Type(st, "SURFACEDELETE EG", log);
  TickSurfaceRebuilds(st, log);
  CHECK((st.cadTables[0].cells == std::vector<std::string>{"Analysis", "Surface value unavailable", "—"}));
}

TEST_CASE("ZVI refuses when no clipped cell has complete surface coverage", "[surface][req510]") {
  AppCommandState st;
  AddGround(st, "EG");
  auto tiny = std::make_shared<CadTin>();
  tiny->vertsXyz = {49.9,49.9,2, 50.1,49.9,2, 50.1,50.1,2, 49.9,50.1,2};
  tiny->indices = {0,1,2, 0,2,3};
  st.cadSurfaces[0].tin = tiny;
  const std::uint32_t revision = st.cadGpuRevision;
  std::vector<std::string> log;
  Type(st, "SURFACEVISIBILITY EG, ZVI, 50, 50, 2, 1, 100", log);
  CHECK(st.cadFilledRegions.empty());
  CHECK(st.cadGpuRevision == revision);
  CHECK(LogHas(log, "requested area has no surface coverage"));
}

TEST_CASE("Surface LOS reports the first terrain intersection on a TIN face", "[surface][req510]") {
  AppCommandState st;
  CadSurface surface;
  surface.name = "Ridge";
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0,0,0, 50.1,0,1, 50.1,100,1, 0,100,0, 100,0,0, 100,100,0};
  tin->indices = {0,1,2, 0,2,3, 1,4,5, 1,5,2};
  surface.tin = tin;
  st.cadSurfaces.push_back(surface);
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  Type(st, "SURFACEVISIBILITY Ridge, LOS, 0, 50, 0.5, 100, 50, 0.5", log);
  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[3] == Approx(25.1502).margin(0.003));
}

TEST_CASE("Crossing breakline decisions are local, undoable and persistent", "[surface][req509]") {
  AppCommandState st;
  st.worldDocumentOriginX = 10000.0;
  st.worldDocumentOriginY = 20000.0;
  st.userLinesFlat = {0, 50, 0, 100, 50, 0, 50, 0, 10, 50, 100, 10};
  st.userLineAttrs.resize(2);
  EnsureEntityIds(st);
  CadSurface surface;
  surface.name = "EG";
  surface.breaklines = {{st.userLineAttrs[0].id, "Horizontal"}, {st.userLineAttrs[1].id, "Vertical"}};
  st.cadSurfaces.push_back(surface);
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(BuildSurfaceFromSources(st, st.cadSurfaces[0], log));
  const auto sourceLines = st.userLinesFlat;
  Type(st, "SURFCROSSBL EG, LIST", log);
  CHECK(LogHas(log, "breakline IDs"));
  CHECK(LogHas(log, "10050.000000, 20050.000000"));

  Type(st, "SURFCROSSBL EG, RESOLVE, 10050, 20050, AVERAGE", log);
  REQUIRE(st.cadSurfaces[0].crossingCorrections.size() == 1);
  CHECK(st.cadSurfaces[0].crossingCorrections[0].kind == CadSurfaceCrossingCorrection::Kind::Average);
  CHECK(st.userLinesFlat == sourceLines);
  CHECK(st.cadSurfaces[0].tin != nullptr);
  double crossingElevation = 0.0;
  REQUIRE(TinElevationAt(st.cadSurfaces[0].tin->vertsXyz, st.cadSurfaces[0].tin->indices,
                         50.0, 50.0, &crossingElevation));
  CHECK(crossingElevation == Approx(5.0).margin(0.002));

  // Source edits can move the crossing slightly while the saved correction still identifies it.
  st.userLinesFlat[6] = 50.02f;
  st.userLinesFlat[9] = 50.02f;
  REQUIRE(BuildSurfaceFromSources(st, st.cadSurfaces[0], log));
  bool splitAtUpdatedCrossing = false;
  for (size_t i = 0; i + 2 < st.cadSurfaces[0].tin->vertsXyz.size(); i += 3) {
    if (std::abs(st.cadSurfaces[0].tin->vertsXyz[i] - 50.02) < 0.002 &&
        std::abs(st.cadSurfaces[0].tin->vertsXyz[i + 1] - 50.0) < 0.002 &&
        std::abs(st.cadSurfaces[0].tin->vertsXyz[i + 2] - 5.0) < 0.002) {
      splitAtUpdatedCrossing = true;
      break;
    }
  }
  CHECK(splitAtUpdatedCrossing);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, json, log));
  REQUIRE(reopened.cadSurfaces.size() == 1);
  REQUIRE(reopened.cadSurfaces[0].crossingCorrections.size() == 1);
  CHECK(reopened.cadSurfaces[0].crossingCorrections[0].x == Approx(50.0));
}

TEST_CASE("Surface labels query and track their source surface", "[surface][req506]") {
  AppCommandState st;
  AddGround(st, "EG");
  std::vector<std::string> log;
  Type(st, "SURFLABEL EG, SPOT, 25, 50", log);
  REQUIRE(st.cadAnnotations.size() == 1);
  CHECK(st.cadAnnotations[0].text == "Elev 4.5000");
  CHECK(st.cadAnnotations[0].surfaceLabelForId != 0);
  Type(st, "SURFLABEL EG, SLOPE, 10, 50, 90, 50", log);
  REQUIRE(st.cadAnnotations.size() == 2);
  CHECK(st.cadAnnotations[1].text == "Slope 10.0000%");
  Type(st, "SURFLABEL EG, WATERSHED", log);
  REQUIRE(st.cadAnnotations.size() >= 3);
  CHECK(LogHas(log, "linked label(s) added"));

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, json, log));
  CHECK(reopened.cadAnnotations[0].surfaceLabelForId == st.cadAnnotations[0].surfaceLabelForId);
  CHECK(reopened.cadAnnotations[0].surfaceLabelValues == st.cadAnnotations[0].surfaceLabelValues);
}

TEST_CASE("Contour labels can repeat where a guide crosses the requested contour", "[surface][req506]") {
  AppCommandState st;
  AddGround(st, "EG");
  std::vector<std::string> log;
  Type(st, "SURFLABEL EG, CONTOUR, 7, 50, 50", log);
  REQUIRE(st.cadAnnotations.size() == 1);
  CHECK(st.cadAnnotations[0].text == "Elev 7.0000");
  Type(st, "SURFLABEL EG, CONTOUR, 7, GUIDE, 0, 50, 100, 50, 0, 30", log);
  REQUIRE(st.cadAnnotations.size() == 3);
  for (const CadAnnotation& label : st.cadAnnotations) {
    CHECK(label.surfaceLabelKind == 3);
    CHECK(label.surfaceLabelValues[0] == Approx(7.0));
  }
  auto raisedTin = std::make_shared<CadTin>(*st.cadSurfaces[0].tin);
  for (size_t i = 2; i < raisedTin->vertsXyz.size(); i += 3) raisedTin->vertsXyz[i] += 1.0;
  st.cadSurfaces[0].tin = std::move(raisedTin);
  BumpCadGpuCache(st);
  TickSurfaceRebuilds(st, log);
  for (const CadAnnotation& label : st.cadAnnotations)
    CHECK(label.text == "Surface value unavailable");
}

TEST_CASE("Templates omit attached model images and their surface links", "[io][template][req424][req511]") {
  namespace fs = std::filesystem;
  AppCommandState st;
  AddGround(st, "EG");
  EnsureEntityIds(st);
  CadRasterImage image;
  image.id = 8001;
  image.path = "source-drawing-ortho.png";
  image.surfaceId = st.cadSurfaceAttrs[0].id;
  st.cadRasterImages.push_back(image);
  const fs::path path = fs::temp_directory_path() /
      ("gosurvey-issue807-template-modelimages-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gst");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyTemplateFile(st, path.string().c_str(), log));
  AppCommandState reopened;
  REQUIRE(LoadGoSurveyTemplateFile(reopened, path.string().c_str(), log));
  CHECK(reopened.cadRasterImages.empty());
  CHECK(reopened.cadSurfaces.empty());
  std::error_code ec;
  fs::remove(path, ec);
}

TEST_CASE("Surface labels show unavailable after their TIN is removed", "[surface][req506]") {
  AppCommandState st;
  AddGround(st, "EG");
  std::vector<std::string> log;
  Type(st, "SURFLABEL EG, SPOT, 25, 50", log);
  REQUIRE(st.cadAnnotations.size() == 1);
  st.cadSurfaces[0].tin.reset();
  const std::uint32_t drawingRevision = st.cadGpuRevision;
  TickSurfaceRebuilds(st, log);
  CHECK(st.cadAnnotations[0].text == "Surface value unavailable");
  CHECK(st.cadGpuRevision == drawingRevision);
}

TEST_CASE("Surface analysis legends reflect configured ranges and persist their link", "[surface][req507]") {
  AppCommandState st;
  st.worldDocumentOriginX = 10000.0;
  st.worldDocumentOriginY = 20000.0;
  AddGround(st, "EG");
  st.surfaceStyles[0].analysisMode = SurfaceAnalysisMode::Elevation;
  st.surfaceStyles[0].bands = {{5.0, "#00ff00"}, {10.0, "#ff0000"}};
  st.cadSurfaces[0].styleName = st.surfaceStyles[0].name;
  std::vector<std::string> log;
  Type(st, "SURFACELEGEND EG, ELEVATION, 10005, 20005", log);
  REQUIRE(st.cadTables.size() == 1);
  CHECK(st.cadTables[0].insX == Approx(5.f));
  CHECK(st.cadTables[0].insY == Approx(5.f));
  CHECK(st.cadTables[0].cells.size() == 9);
  CHECK(st.cadTables[0].cells[4] == "5.0000");
  CHECK(st.cadTables[0].surfaceLegendForId != 0);
  st.surfaceStyles[0].bands = {{2.0, "#00ff00"}, {4.0, "#ff0000"}};
  RefreshSurfaceLegendTablesForStyle(st, st.surfaceStyles[0].name);
  CHECK(st.cadTables[0].cells[4] == "2.0000");
  CHECK(st.cadTables[0].cells[7] == "4.0000");
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, json, log));
  REQUIRE(reopened.cadTables.size() == 1);
  CHECK(reopened.cadTables[0].surfaceLegendForId == st.cadTables[0].surfaceLegendForId);
}

TEST_CASE("Create Profile samples an alignment and persists its placed graph", "[surface][req513]") {
  AppCommandState st;
  AddGround(st, "EG");
  alignment::Alignment baseline;
  baseline.id = 7001;
  baseline.name = "BASELINE";
  REQUIRE(baseline.AppendTangent({0.0, 50.0}, 90.0 * alignment::kPi / 180.0, 100.0));
  st.alignments.push_back(baseline);
  std::vector<std::string> log;
  Type(st, "CREATEPROFILEFROMSURFACE EG, BASELINE, EG Profile, 25, 100000000, 200000000, 0.001, 2", log);
  REQUIRE(st.surfaceProfiles.size() == 1);
  const CadSurfaceProfile& profile = st.surfaceProfiles[0];
  REQUIRE(profile.stations.size() == 5);
  CHECK(profile.elevations[0] == Approx(GroundZ(0)).margin(0.002));
  CHECK(profile.elevations[2] == Approx(GroundZ(50)).margin(0.002));
  CHECK(profile.elevations[4] == Approx(GroundZ(100)).margin(0.002));
  CHECK(profile.viewEntityIds.size() >= 6);
  CHECK(st.userLineAttrs.size() == profile.viewEntityIds.size());
  CHECK(st.userLinesFlat[3] - st.userLinesFlat[0] == Approx(0.1).margin(1.e-6));

  st.userLinesFlat.insert(st.userLinesFlat.end(), {200.f, 200.f, 0.f, 210.f, 200.f, 0.f});
  EntityAttributes unrelated;
  unrelated.id = st.nextEntityId++;
  const std::uint64_t unrelatedId = unrelated.id;
  st.userLineAttrs.push_back(unrelated);
  st.selection.push_back({SelectedEntity::Type::LineSeg, static_cast<int>(st.userLineAttrs.size() - 1)});

  auto rebuiltTin = SlopingGround();
  for (size_t i = 2; i < rebuiltTin->vertsXyz.size(); i += 3) rebuiltTin->vertsXyz[i] += 1.0;
  st.cadSurfaces[0].tin = rebuiltTin;
  Type(st, "PROFILEVIEW EG Profile, 0, 0, 1, 2", log);
  CHECK(st.surfaceProfiles[0].elevations[2] == Approx(GroundZ(50) + 1.0).margin(0.002));
  REQUIRE(st.selection.size() == 1);
  CHECK(st.selection[0].index == 0);
  CHECK(st.userLineAttrs[static_cast<size_t>(st.selection[0].index)].id == unrelatedId);

  // Alignment edits refresh saved profiles even when the source is a fixed imported TIN.
  Type(st, "ALIGNMENTADD BASELINE, TANGENT, 10", log);
  CHECK(st.surfaceProfiles[0].stations.back() == Approx(110.0));

  st.alignments.clear();
  const size_t oldSampleCount = st.surfaceProfiles[0].stations.size();
  TickSurfaceRebuilds(st, log);
  CHECK(st.surfaceProfiles[0].stations.size() == oldSampleCount);
  CHECK(st.surfaceProfiles[0].sourceStatus == "Source alignment unavailable.");
  CHECK(LogHas(log, "Profile \"EG Profile\" unavailable"));
  auto changedWhileUnavailable = std::make_shared<CadTin>(*st.cadSurfaces[0].tin);
  for (size_t i = 2; i < changedWhileUnavailable->vertsXyz.size(); i += 3)
    changedWhileUnavailable->vertsXyz[i] += 2.0;
  st.cadSurfaces[0].tin = std::move(changedWhileUnavailable);
  const std::uint32_t cacheRevisionBeforeRestore = st.cadGpuCacheRevision;
  st.alignments.push_back(baseline);
  TickSurfaceRebuilds(st, log);
  CHECK(st.surfaceProfiles[0].sourceStatus.empty());
  CHECK(st.surfaceProfiles[0].elevations[2] == Approx(GroundZ(50) + 3.0).margin(0.002));
  CHECK(st.cadGpuCacheRevision > cacheRevisionBeforeRestore);
  CHECK(LogHas(log, "source is available again"));

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, json, log));
  REQUIRE(reopened.surfaceProfiles.size() == 1);
  CHECK(reopened.surfaceProfiles[0].alignmentId == 7001);
  CHECK(reopened.surfaceProfiles[0].stations.size() == 5);
  CHECK(reopened.surfaceProfiles[0].viewEntityIds == profile.viewEntityIds);
}

TEST_CASE("Surface profile gaps survive save and reopen as gaps", "[surface][req513]") {
  AppCommandState st;
  AddGround(st, "EG");
  alignment::Alignment baseline;
  baseline.id = 7001;
  baseline.name = "BASELINE";
  REQUIRE(baseline.AppendTangent({0.0, 50.0}, 90.0 * alignment::kPi / 180.0, 150.0));
  st.alignments.push_back(baseline);
  std::vector<std::string> log;
  Type(st, "CREATEPROFILEFROMSURFACE EG, BASELINE, Partial, 25, 0, 0, 1, 2", log);
  REQUIRE(st.surfaceProfiles.size() == 1);
  REQUIRE(st.surfaceProfiles[0].elevations.size() == 7);
  CHECK(std::isfinite(st.surfaceProfiles[0].elevations[4]));
  CHECK_FALSE(std::isfinite(st.surfaceProfiles[0].elevations[5]));
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, json, log));
  REQUIRE(reopened.surfaceProfiles.size() == 1);
  CHECK(std::isfinite(reopened.surfaceProfiles[0].elevations[4]));
  CHECK_FALSE(std::isfinite(reopened.surfaceProfiles[0].elevations[5]));
}

TEST_CASE("Asynchronous surface rebuild refreshes linked labels and profiles", "[surface][req506][req513]") {
  AppCommandState st;
  CadSurface surface;
  surface.name = "EG";
  surface.addedPointXyz = {0,0,2, 100,0,12, 100,100,12, 0,100,2};
  st.cadSurfaces.push_back(surface);
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  EnsureEntityIds(st);
  std::vector<std::string> log;
  REQUIRE(BuildSurfaceFromSources(st, st.cadSurfaces[0], log));
  alignment::Alignment baseline;
  baseline.id = 7001;
  baseline.name = "BASELINE";
  REQUIRE(baseline.AppendTangent({0.0, 50.0}, 90.0 * alignment::kPi / 180.0, 100.0));
  st.alignments.push_back(baseline);
  Type(st, "SURFLABEL EG, SPOT, 25, 50", log);
  Type(st, "CREATEPROFILEFROMSURFACE EG, BASELINE, EG Profile, 25, 0, 0, 1, 2", log);
  REQUIRE(st.cadAnnotations.size() == 1);
  REQUIRE(st.surfaceProfiles.size() == 1);
  CHECK(st.cadAnnotations[0].text == "Elev 4.5000");
  CHECK(st.surfaceProfiles[0].elevations[2] == Approx(7.0).margin(0.002));

  for (size_t i = 2; i < st.cadSurfaces[0].addedPointXyz.size(); i += 3)
    st.cadSurfaces[0].addedPointXyz[i] += 5.0;
  CadSurface second;
  second.name = "FG";
  for (int row = 0; row <= 12; ++row)
    for (int column = 0; column <= 12; ++column)
      second.addedPointXyz.insert(second.addedPointXyz.end(),
                                  {column * (100.0 / 12.0), row * (100.0 / 12.0),
                                   20.0 + column * (10.0 / 12.0)});
  st.cadSurfaces.push_back(std::move(second));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  EnsureEntityIds(st);
  REQUIRE(BuildSurfaceFromSources(st, st.cadSurfaces[1], log));
  Type(st, "SURFLABEL FG, SPOT, 25, 50", log);
  for (size_t i = 2; i < st.cadSurfaces[1].addedPointXyz.size(); i += 3)
    st.cadSurfaces[1].addedPointXyz[i] += 5.0;
  BumpCadGpuCache(st);
  for (int attempt = 0; attempt < 400; ++attempt) {
    TickSurfaceRebuilds(st, log);
    if (st.surfaceRebuildAsync.empty() &&
        st.cadSurfaces[0].builtAtRevision == st.cadGpuRevision &&
        st.cadSurfaces[1].builtAtRevision == st.cadGpuRevision) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(st.surfaceRebuildAsync.empty());
  CHECK(st.cadAnnotations[0].text == "Elev 9.5000");
  REQUIRE(st.cadAnnotations.size() == 2);
  CHECK(st.cadAnnotations[1].text == "Elev 27.5000");
  CHECK(st.surfaceProfiles[0].elevations[2] == Approx(12.0).margin(0.002));
}

TEST_CASE("A vertex off the surface refuses its whole entity, by name and count",
          "[drape][issue150][phase7]") {
  // ADR-069 (f). TinElevationAt never extrapolates (REQ-074), so there is no elevation for a vertex
  // beyond the ground. Draping the rest would leave a shape that is neither the original nor the
  // ground — the plausible-looking wrong answer REQ-201 forbids.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 50.0, 500.0});  // the last one is well off the square
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);

  CHECK(LogHas(log, "not draped"));
  CHECK(LogHas(log, "1 of 3 vertices are off"));
  // Nothing moved — not even the two vertices that WERE over the ground.
  for (int i = 0; i < 3; ++i) {
    INFO("vertex " << i);
    CHECK(st.userPolylineVerts[static_cast<size_t>(i) * 3 + 2] == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("DRAPE names what it cannot drape, and still drapes the rest", "[drape][issue150][phase7]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  // A circle is not a list of vertices: draping sampled points would leave something that is no
  // longer a circle, so it is refused by name rather than silently skipped (REQ-201).
  st.userCirclesCxCyZR = {50.0, 50.0, 0.0, 5.0};
  st.userCircleAttrs.push_back(EntityAttributes{});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  st.selection.push_back({SelectedEntity::Type::Circle, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);

  CHECK(LogHas(log, "could not be draped"));
  CHECK(LogHas(log, "circle"));
  CHECK(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));  // the polyline still went
  CHECK(st.userCirclesCxCyZR[2] == Approx(0.0).margin(1e-9));             // the circle did not move
}

TEST_CASE("DRAPE refuses clearly when it has nothing to work with", "[drape][issue150][phase7]") {
  SECTION("no surfaces at all") {
    AppCommandState st;
    std::vector<std::string> log;
    Type(st, "DRAPE", log);
    CHECK(LogHas(log, "no surfaces"));
  }
  SECTION("a name that is not a surface") {
    AppCommandState st;
    AddGround(st, "EG");
    std::vector<std::string> log;
    Type(st, "DRAPE Nope", log);
    CHECK(LogHas(log, "no surface named"));
  }
  SECTION("nothing selected") {
    AppCommandState st;
    AddGround(st, "EG");
    std::vector<std::string> log;
    Type(st, "DRAPE EG", log);
    CHECK(LogHas(log, "select the objects"));
  }
  SECTION("several surfaces are listed rather than guessed") {
    AppCommandState st;
    AddGround(st, "EG");
    AddGround(st, "FG");
    AddFlatPolylineAcross(st, {10.0});
    st.selection.push_back({SelectedEntity::Type::Polyline, 0});
    std::vector<std::string> log;
    Type(st, "DRAPE", log);
    CHECK(LogHas(log, "Surfaces: EG, FG"));
    CHECK(st.userPolylineVerts[2] == Approx(0.0).margin(1e-9));  // and nothing was touched
  }
}

TEST_CASE("DRAPE holds REQ-101 at survey coordinate magnitudes", "[drape][issue150][phase7]") {
  // Phase 7 acceptance: "everything remains stable at survey coordinate magnitudes." The ground and
  // the geometry both sit out at state-plane easting, where a float would have lost a quarter of a
  // foot before the drape even started.
  AppCommandState st;
  const double e0 = 2196000.0;
  const double n0 = 1400000.0;
  CadSurface s;
  s.name = "EG";
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {
      e0,         n0,         250.0,
      e0 + 100.0, n0,         260.0,
      e0 + 100.0, n0 + 100.0, 260.0,
      e0,         n0 + 100.0, 250.0,
  };
  tin->indices = {0, 1, 2, 0, 2, 3};
  s.tin = tin;
  st.cadSurfaces.push_back(std::move(s));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});

  st.userPolylineOffsets.push_back(0);
  const std::vector<double> xs = {e0 + 10.0, e0 + 37.5, e0 + 90.0};
  for (double x : xs) {
    st.userPolylineVerts.push_back(x);
    st.userPolylineVerts.push_back(n0 + 50.0);
    st.userPolylineVerts.push_back(0.0);
  }
  st.userPolylineOffsets.push_back(3);
  st.userPolylineClosed.push_back(0);
  st.userPolylineAttrs.push_back(EntityAttributes{});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});

  std::vector<std::string> log;
  Type(st, "DRAPE EG", log);

  for (size_t i = 0; i < xs.size(); ++i) {
    const double want = 250.0 + (xs[i] - e0) / 10.0;
    INFO("vertex " << i);
    CHECK(st.userPolylineVerts[i * 3 + 2] == Approx(want).margin(0.002));
  }
}

// --- The opt-in link (ADR-069 (b)-(e)) ----------------------------------------------------------

namespace {

/// The same ground, raised by `lift` — what a surface edit looks like to a linked drape.
std::shared_ptr<CadTin> SlopingGroundRaised(double lift) {
  auto tin = SlopingGround();
  for (size_t i = 2; i < tin->vertsXyz.size(); i += 3)
    tin->vertsXyz[i] += lift;
  return tin;
}

std::uint64_t SurfaceIdOf(AppCommandState& st, size_t si) {
  EnsureEntityIds(st);
  return st.cadSurfaceAttrs[si].id;
}

}  // namespace

TEST_CASE("A baked drape stores no link; LINK stores the surface's stable id",
          "[drape][issue150][phase7][drapelink]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  SECTION("baked leaves nothing behind") {
    Type(st, "DRAPE EG", log);
    CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == 0u);
  }
  SECTION("LINK stores the id, not the index and not the name") {
    Type(st, "DRAPE EG, LINK", log);
    const std::uint64_t id = SurfaceIdOf(st, 0);
    CHECK(id != 0u);
    CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == id);
    CHECK(LogHas(log, "linked"));
  }
  SECTION("re-draping without LINK clears the link") {
    Type(st, "DRAPE EG, LINK", log);
    REQUIRE(st.userPolylineAttrs[0].drapedOnSurfaceId != 0u);
    Type(st, "DRAPE EG", log);
    CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == 0u);
  }
  SECTION("an unknown option is refused rather than ignored") {
    Type(st, "DRAPE EG, WOBBLE", log);
    CHECK(LogHas(log, "Unknown option"));
    CHECK(st.userPolylineVerts[2] == Approx(0.0).margin(1e-9));  // and nothing moved
  }
}

TEST_CASE("Linked geometry follows a rebuilt surface; baked geometry does not",
          "[drape][issue150][phase7][drapelink]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});   // polyline 0 - will be LINKED
  AddFlatPolylineAcross(st, {30.0, 40.0});   // polyline 1 - will be BAKED
  std::vector<std::string> log;

  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  Type(st, "DRAPE EG, LINK", log);
  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 1});
  Type(st, "DRAPE EG", log);

  REQUIRE(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));
  REQUIRE(st.userPolylineVerts[8] == Approx(GroundZ(30.0)).margin(0.002));

  // The ground rises by 5 and the surface is rebuilt.
  st.cadSurfaces[0].tin = SlopingGroundRaised(5.0);
  log.clear();
  ReDrapeLinkedToSurface(st, 0, log);

  // The linked one followed...
  CHECK(st.userPolylineVerts[2] == Approx(GroundZ(10.0) + 5.0).margin(0.002));
  CHECK(st.userPolylineVerts[5] == Approx(GroundZ(20.0) + 5.0).margin(0.002));
  // ...and the baked one did not move at all. This is the whole point of the default.
  CHECK(st.userPolylineVerts[8] == Approx(GroundZ(30.0)).margin(0.002));
  CHECK(st.userPolylineVerts[11] == Approx(GroundZ(40.0)).margin(0.002));
  CHECK(LogHas(log, "1 linked object(s) re-draped"));
}

TEST_CASE("SURFACEREBUILD re-drapes what is linked to it", "[drape][issue150][phase7][drapelink]") {
  // The hook, through the command a user actually types, rather than the helper directly.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  REQUIRE(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));

  st.cadSurfaces[0].tin = SlopingGroundRaised(3.0);
  log.clear();
  Type(st, "SURFACEREBUILD EG", log);
  // The rebuild has no sources to build from, so the TIN it finds is the one just set; what is
  // asserted here is that the re-drape ran at all off the command.
  CHECK(LogHas(log, "re-draped"));
}

TEST_CASE("A link to an erased surface resolves to nothing and the geometry stays put",
          "[drape][issue150][phase7][drapelink]") {
  // ADR-069 (e). The reference resolving to nothing is REQ-076's own rule; what the GEOMETRY does is
  // the decision - it is not deleted and not moved. Destroying drawn geometry because a surface was
  // erased would be far worse than a stale shape, and the stale shape is what a bake gives anyway.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  const std::uint64_t id = SurfaceIdOf(st, 0);
  const double zBefore = st.userPolylineVerts[2];
  REQUIRE(zBefore == Approx(GroundZ(10.0)).margin(0.002));

  st.cadSurfaces.clear();
  st.cadSurfaceAttrs.clear();

  // The id is still on the entity, and resolves to nothing rather than to whatever takes the slot.
  CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == id);
  CHECK(FindSurfaceIndexById(st, id) == -1);
  // And the shape is exactly where it was.
  CHECK(st.userPolylineVerts[2] == Approx(zBefore).margin(1e-12));
}

TEST_CASE("A linked entity that leaves the ground is left where it is, and said so",
          "[drape][issue150][phase7][drapelink]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  const double z0 = st.userPolylineVerts[2];
  const double z1 = st.userPolylineVerts[5];

  // The surface shrinks to a corner that no longer covers the polyline.
  auto small = std::make_shared<CadTin>();
  small->vertsXyz = {60.0, 60.0, 9.0, 70.0, 60.0, 9.0, 70.0, 70.0, 9.0};
  small->indices = {0, 1, 2};
  st.cadSurfaces[0].tin = small;

  log.clear();
  ReDrapeLinkedToSurface(st, 0, log);

  CHECK(LogHas(log, "left where they are"));
  CHECK(st.userPolylineVerts[2] == Approx(z0).margin(1e-12));
  CHECK(st.userPolylineVerts[5] == Approx(z1).margin(1e-12));
  // The link is KEPT, so it re-drapes on its own once the ground covers it again.
  CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId != 0u);
}

TEST_CASE("A refused drape pushes no undo step", "[drape][issue150][phase7][drapelink]") {
  // An undo after a drape that moved nothing must take back whatever the user did BEFORE it.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 5000.0});  // the second vertex is off the ground
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  // Something undoable first, so there is a step to come back to.
  PushUndoSnapshot(st, "Before");
  st.userPolylineVerts[2] = 42.0;

  Type(st, "DRAPE EG", log);
  REQUIRE(LogHas(log, "not draped"));

  DoUndo(st, log);
  CHECK(st.userPolylineVerts[2] == Approx(0.0).margin(1e-9));  // the "Before" step, not a no-op drape
}

// --- ADR-069 (d): the link is visible, not a hidden attribute -----------------------------------

TEST_CASE("DrapedOnSurfaceName reports what an object follows", "[drape][issue150][phase7][drapemark]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});   // 0 - linked
  AddFlatPolylineAcross(st, {30.0, 40.0});   // 1 - baked
  std::vector<std::string> log;

  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  Type(st, "DRAPE EG, LINK", log);
  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 1});
  Type(st, "DRAPE EG", log);

  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 0}) == "EG");
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 1}).empty());
  // A type that cannot be draped, and an index past the end, both answer "follows nothing" rather
  // than reading off the end of a store.
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Circle, 0}).empty());
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 99}).empty());
}

TEST_CASE("An object whose surface was erased reports following nothing",
          "[drape][issue150][phase7][drapemark]") {
  // ADR-069 (e) again, but from the USER's side: the geometry stays put, and the panel must not go
  // on claiming it follows something. "Never linked", "baked since" and "its surface is gone" are
  // the same answer to the person looking at it.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  REQUIRE(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 0}) == "EG");

  st.cadSurfaces.clear();
  st.cadSurfaceAttrs.clear();
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 0}).empty());
  // ...while the id itself is untouched, so undoing the erase brings the link back.
  CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId != 0u);
}

TEST_CASE("DRAPELINKS answers what moves when a surface is rebuilt",
          "[drape][issue150][phase7][drapemark]") {
  AppCommandState st;
  std::vector<std::string> log;

  SECTION("a drawing where nothing follows a surface says so") {
    AddGround(st, "EG");
    AddFlatPolylineAcross(st, {10.0, 20.0});
    Type(st, "DRAPELINKS", log);
    CHECK(LogHas(log, "nothing in the drawing follows a surface"));
  }

  SECTION("it names each object and its surface") {
    AddGround(st, "EG");
    AddFlatPolylineAcross(st, {10.0, 20.0});
    AddFlatPolylineAcross(st, {30.0, 40.0});
    st.selection.push_back({SelectedEntity::Type::Polyline, 0});
    Type(st, "DRAPE EG, LINK", log);
    log.clear();
    Type(st, "DRAPELINKS", log);
    CHECK(LogHas(log, "polyline 0 follows \"EG\""));
    CHECK(LogHas(log, "1 object(s) follow a surface"));
    CHECK_FALSE(LogHas(log, "polyline 1 follows"));  // the baked one is not listed
  }

  SECTION("an object naming a surface that is gone is reported apart, not as following it") {
    AddGround(st, "EG");
    AddFlatPolylineAcross(st, {10.0, 20.0});
    st.selection.push_back({SelectedEntity::Type::Polyline, 0});
    Type(st, "DRAPE EG, LINK", log);
    st.cadSurfaces.clear();
    st.cadSurfaceAttrs.clear();
    log.clear();
    Type(st, "DRAPELINKS", log);
    CHECK(LogHas(log, "name a surface that is gone"));
    CHECK(LogHas(log, "stay where they are"));
    CHECK_FALSE(LogHas(log, "follows \""));
  }
}
