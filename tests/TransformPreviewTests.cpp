// The transform preview is the commit applied to a scratch copy (ADR-0011 / issue #1700 phase 3).
//
// Each case draws the preview at a cursor position, commits a pick at the same position, and checks
// that the ghost is the wireframe of what the commit left behind: the selection highlight of the
// transformed (or newly copied) entities. A preview that keeps its own maths fails here as soon as
// it disagrees with the commit.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "viewport/TransformPreview.hpp"

namespace {

constexpr double kTol = 1e-3;

void SubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  char buf[512] = {};
  std::snprintf(buf, sizeof(buf), "%s", line.c_str());
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), st, log);
}

struct Picture {
  std::vector<ray3d::Vec3> vertices;        // line-segment endpoints
  std::vector<std::array<double, 4>> rings;  // flat circles: cx, cy, z, r
};

Picture FromBuffers(const std::vector<float>& lines, const std::vector<float>& circles) {
  Picture p;
  for (size_t i = 0; i + 2 < lines.size(); i += 3)
    p.vertices.push_back({lines[i], lines[i + 1], lines[i + 2]});
  for (size_t i = 0; i + 3 < circles.size(); i += 4)
    p.rings.push_back({circles[i], circles[i + 1], circles[i + 2], circles[i + 3]});
  return p;
}

Picture Preview(const AppCommandState& st, float x, float y) {
  std::vector<float> lines, circles;
  BuildTransformPreview(st, x, y, &lines, &circles, 100.f, 900);
  return FromBuffers(lines, circles);
}

/// The selection highlight of \p entities, which is the wireframe the preview draws for each copy.
Picture Wireframe(AppCommandState& st, const std::vector<SelectedEntity>& entities) {
  const std::vector<SelectedEntity> keep = st.selection;
  st.selection = entities;
  std::vector<double> lines;
  std::vector<float> circles;
  BuildSelectionHighlight(st, &lines, &circles);
  st.selection = keep;
  std::vector<float> asFloat(lines.begin(), lines.end());
  return FromBuffers(asFloat, circles);
}

double Dist(const ray3d::Vec3& a, const ray3d::Vec3& b) { return ray3d::Length(ray3d::Sub(a, b)); }

/// Every vertex of \p a lies on a vertex of \p b (within \ref kTol), and every ring matches a ring.
bool Covers(const Picture& a, const Picture& b) {
  for (const ray3d::Vec3& v : a.vertices) {
    bool hit = false;
    for (const ray3d::Vec3& w : b.vertices)
      if (Dist(v, w) < kTol) {
        hit = true;
        break;
      }
    if (!hit)
      return false;
  }
  for (const auto& r : a.rings) {
    bool hit = false;
    for (const auto& s : b.rings)
      if (std::fabs(r[0] - s[0]) < kTol && std::fabs(r[1] - s[1]) < kTol && std::fabs(r[2] - s[2]) < kTol &&
          std::fabs(r[3] - s[3]) < kTol) {
        hit = true;
        break;
      }
    if (!hit)
      return false;
  }
  return true;
}

void CheckSamePicture(const Picture& preview, const Picture& committed) {
  REQUIRE_FALSE((committed.vertices.empty() && committed.rings.empty()));
  CHECK(preview.vertices.size() == committed.vertices.size());
  CHECK(preview.rings.size() == committed.rings.size());
  CHECK(Covers(preview, committed));
  CHECK(Covers(committed, preview));
}

void AddLine(AppCommandState& st, CadLineRecord l) {
  st.drawing.pushLine(l);
  st.drawing.userLineAttrs.push_back(EntityAttributes{});
}

void AddCircle(AppCommandState& st, double cx, double cy, double cz, double r) {
  st.drawing.pushCircle(CadCircleRecord{cx, cy, cz, r});
  st.drawing.userCircleAttrs.push_back(EntityAttributes{});
  PushCircleNormal(st.drawing.userCircleNormals, kFlatNormalX, kFlatNormalY, kFlatNormalZ);
}

void AddArc(AppCommandState& st) {
  CadArc a;
  a.cx = 4.f;
  a.cy = 4.f;
  a.z = 1.f;
  a.r = 2.f;
  a.startRad = 0.3f;
  a.sweepRad = 2.0f;
  st.drawing.userArcs.push_back(a);
  st.drawing.userArcAttrs.push_back(EntityAttributes{});
}

void AddEllipse(AppCommandState& st) {
  CadEllipse e;
  e.cx = -4.f;
  e.cy = 2.f;
  e.z = 0.5f;
  e.majVx = 3.f;
  e.majVy = 1.f;
  e.ratio = 0.5f;
  st.drawing.userEllipses.push_back(e);
  st.drawing.userEllAttrs.push_back(EntityAttributes{});
}

void AddBulgedPolyline(AppCommandState& st) {
  if (st.drawing.userPolylineOffsets.empty())
    st.drawing.userPolylineOffsets.push_back(0);
  const int base = st.drawing.userPolylineOffsets.back();
  for (const CadPolyVertex& v : {CadPolyVertex{0, -3, 2}, CadPolyVertex{3, -3, 2}, CadPolyVertex{3, -6, 2}})
    st.drawing.pushVertex(v);
  st.drawing.userPolylineVertsBulge.resize(st.drawing.vertexCount(), 0.f);
  st.drawing.userPolylineVertsBulge[static_cast<size_t>(base)] = 0.4f;
  st.drawing.userPolylineOffsets.push_back(base + 3);
  st.drawing.userPolylineClosed.push_back(0);
  st.drawing.userPolylineAttrs.push_back(EntityAttributes{});
}

void AddHatch(AppCommandState& st) {
  CadFilledRegion fr;
  fr.vertsXyz = {6.0, -2.0, 0.0, 9.0, -2.0, 0.0, 6.0, 1.0, 0.0};
  fr.loopStart = {0};
  fr.patternName = "ANSI31";
  st.drawing.cadFilledRegions.push_back(fr);
  st.drawing.cadFilledRegionAttrs.push_back(EntityAttributes{});
}

void AddBlockWithInsert(AppCommandState& st, float x, float y) {
  CadBlockDefinition def;
  def.name = "TICK";
  def.content.pushLine(CadLineRecord{0.0, 0.0, 0.0, 1.0, 0.5, 0.0});
  def.content.userLineAttrs.push_back(EntityAttributes{});
  st.drawing.blockDefs.push_back(def);
  CadBlockRef ref;
  ref.defName = "TICK";
  ref.xf.x = x;
  ref.xf.y = y;
  st.drawing.cadBlockRefs.push_back(ref);
  st.drawing.cadBlockRefAttrs.push_back(EntityAttributes{});
}

std::vector<SelectedEntity> SelectEverything(const AppCommandState& st) {
  std::vector<SelectedEntity> sel;
  using T = SelectedEntity::Type;
  for (size_t i = 0; i < st.drawing.lineCount(); ++i) sel.push_back({T::LineSeg, static_cast<int>(i)});
  for (size_t i = 0; i < st.drawing.circleCount(); ++i) sel.push_back({T::Circle, static_cast<int>(i)});
  for (size_t i = 0; i < st.drawing.userArcs.size(); ++i) sel.push_back({T::Arc, static_cast<int>(i)});
  for (size_t i = 0; i < st.drawing.userEllipses.size(); ++i) sel.push_back({T::Ellipse, static_cast<int>(i)});
  for (size_t i = 0; i + 1 < st.drawing.userPolylineOffsets.size(); ++i)
    sel.push_back({T::Polyline, static_cast<int>(i)});
  for (size_t i = 0; i < st.drawing.cadFilledRegions.size(); ++i)
    sel.push_back({T::FilledRegion, static_cast<int>(i)});
  for (size_t i = 0; i < st.drawing.cadBlockRefs.size(); ++i) sel.push_back({T::BlockRef, static_cast<int>(i)});
  return sel;
}

}  // namespace

TEST_CASE("SCALE's preview scales Z as the commit does", "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, CadLineRecord{2.0, 0.0, 4.0, 4.0, 0.0, 6.0});
  st.selection = SelectEverything(st);
  REQUIRE(DispatchByPrimaryForTest("scale", st, log));
  SubmitViewportPick(st, 0.0, 0.0, log);
  const Picture before = Preview(st, 6.f, 0.f);
  SubmitViewportPick(st, 6.0, 0.0, log);
  REQUIRE(st.drawing.line(0).z0 > 4.5);  // the commit scaled Z
  CheckSamePicture(before, Wireframe(st, SelectEverything(st)));
}

TEST_CASE("MOVE's preview is the committed result for every kind it draws", "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, CadLineRecord{0.0, 0.0, 0.0, 2.0, 1.0, 3.0});
  AddCircle(st, 1.0, 5.0, 0.0, 1.5);
  AddArc(st);
  AddEllipse(st);
  AddBulgedPolyline(st);
  AddHatch(st);
  AddBlockWithInsert(st, -2.f, -2.f);
  st.selection = SelectEverything(st);
  REQUIRE(DispatchByPrimaryForTest("move", st, log));
  SubmitViewportPick(st, 0.0, 0.0, log);
  const Picture before = Preview(st, 5.f, 3.f);
  SubmitViewportPick(st, 5.0, 3.0, log);
  CHECK(st.drawing.line(0).x0 == Catch::Approx(5.0));
  CheckSamePicture(before, Wireframe(st, SelectEverything(st)));
}

TEST_CASE("ROTATE's preview turns arcs, ellipses and inserts as the commit does", "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddArc(st);
  AddEllipse(st);
  AddBulgedPolyline(st);
  AddBlockWithInsert(st, 3.f, 1.f);
  st.selection = SelectEverything(st);
  REQUIRE(DispatchByPrimaryForTest("rotate", st, log));
  SubmitViewportPick(st, 1.0, 1.0, log);
  const Picture before = Preview(st, -2.f, 3.f);
  SubmitViewportPick(st, -2.0, 3.0, log);
  CheckSamePicture(before, Wireframe(st, SelectEverything(st)));
}

TEST_CASE("MIRROR's preview is the mirrored copy", "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, CadLineRecord{1.0, 1.0, 0.0, 3.0, 2.0, 0.0});
  AddArc(st);
  AddEllipse(st);
  AddHatch(st);
  AddBlockWithInsert(st, 2.f, -1.f);
  st.selection = SelectEverything(st);
  REQUIRE(DispatchByPrimaryForTest("mirror", st, log));
  SubmitViewportPick(st, 0.0, 0.0, log);
  const Picture before = Preview(st, 1.f, 3.f);
  // The preview draws the mirror line too; the copy is everything else.
  REQUIRE(before.vertices.size() >= 2);
  Picture ghost = before;
  ghost.vertices.erase(ghost.vertices.begin(), ghost.vertices.begin() + 2);
  SubmitViewportPick(st, 1.0, 3.0, log);
  SubmitLine(st, "n", log);
  using T = SelectedEntity::Type;
  CheckSamePicture(ghost, Wireframe(st, {{T::LineSeg, 1},
                                         {T::Arc, 1},
                                         {T::Ellipse, 1},
                                         {T::FilledRegion, 1},
                                         {T::BlockRef, 1}}));
}

TEST_CASE("polar ARRAY's preview about a tilted axis includes ellipses and inserts", "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddEllipse(st);
  AddBlockWithInsert(st, 3.f, 1.f);
  st.selection = SelectEverything(st);
  REQUIRE(DispatchByPrimaryForTest("ucs", st, log));
  SubmitLine(st, "x", log);
  SubmitLine(st, "30", log);
  REQUIRE_FALSE(CadWorkPlaneIsWorldXy(st));
  REQUIRE(DispatchByPrimaryForTest("array", st, log));
  SubmitLine(st, "p", log);
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "3", log);
  SubmitLine(st, "180", log);
  const Picture before = Preview(st, 0.f, 0.f);
  SubmitLine(st, "y", log);
  REQUIRE(st.drawing.userEllipses.size() == 3);
  REQUIRE(st.drawing.cadBlockRefs.size() == 3);
  using T = SelectedEntity::Type;
  CheckSamePicture(before, Wireframe(st, {{T::Ellipse, 1}, {T::BlockRef, 1}, {T::Ellipse, 2}, {T::BlockRef, 2}}));
}

TEST_CASE("the gizmo's scale ghost is the committed result", "[issue1700][preview][gizmo]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, CadLineRecord{1.0, 0.0, 2.0, 3.0, 0.0, 2.0});
  AddCircle(st, 0.0, 4.0, 1.0, 1.0);
  st.selection = SelectEverything(st);
  st.gizmoOp = CadGizmoOp::Scale;
  st.gizmoDragActive = true;
  st.gizmoDragAxis = 0;
  st.gizmoAnchor = {1.0, 1.0, 0.0};
  st.gizmoAxisDir = {1.0, 0.0, 0.0};
  st.gizmoDragDistance = 2.0;
  std::vector<float> lines, circles;
  BuildGizmoDragGhost(st, &lines, &circles);
  const Picture ghost = FromBuffers(lines, circles);
  REQUIRE(CommitGizmoDrag(st, log));
  CHECK(st.drawing.line(0).z0 == Catch::Approx(4.0));
  CheckSamePicture(ghost, Wireframe(st, SelectEverything(st)));
}

TEST_CASE("the text ghost is the annotation the commit leaves", "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  CadAnnotation text;
  text.kind = CadAnnotation::Kind::Text;
  text.insX = 2.f;
  text.insY = 1.f;
  text.rotationRad = 0.25f;
  text.text = "GHOST";
  st.drawing.cadAnnotations.push_back(text);
  st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
  st.selection = {{SelectedEntity::Type::Annotation, 0}};

  SECTION("ROTATE") {
    REQUIRE(DispatchByPrimaryForTest("rotate", st, log));
    SubmitViewportPick(st, 0.0, 0.0, log);
    std::vector<CadAnnotation> ghosts;
    CadAnnotationCollectTransformPreviews(st, -3.f, 2.f, &ghosts);
    SubmitViewportPick(st, -3.0, 2.0, log);
    REQUIRE(ghosts.size() == 1);
    const CadAnnotation& done = st.drawing.cadAnnotations[0];
    CHECK(ghosts[0].insX == Catch::Approx(done.insX).margin(kTol));
    CHECK(ghosts[0].insY == Catch::Approx(done.insY).margin(kTol));
    CHECK(ghosts[0].rotationRad == Catch::Approx(done.rotationRad).margin(kTol));
  }
  SECTION("MIRROR keeps it readable") {
    REQUIRE(DispatchByPrimaryForTest("mirror", st, log));
    SubmitViewportPick(st, 0.0, 0.0, log);
    std::vector<CadAnnotation> ghosts;
    CadAnnotationCollectTransformPreviews(st, 0.f, 5.f, &ghosts);
    SubmitViewportPick(st, 0.0, 5.0, log);
    SubmitLine(st, "n", log);
    REQUIRE(ghosts.size() == 1);
    REQUIRE(st.drawing.cadAnnotations.size() == 2);
    const CadAnnotation& copy = st.drawing.cadAnnotations[1];
    CHECK(ghosts[0].insX == Catch::Approx(copy.insX).margin(kTol));
    CHECK(ghosts[0].insY == Catch::Approx(copy.insY).margin(kTol));
    CHECK(ghosts[0].rotationRad == Catch::Approx(copy.rotationRad).margin(kTol));
  }
}

TEST_CASE("the ghost of a tilted bulged segment stays on its own plane", "[issue1700][preview]") {
  // Second segment: a half circle from (2,0,0) to (4,0,0) in the upright plane y = 0. The normal
  // array holds three floats per vertex, so the segment's plane is read from floats 3..5.
  AppCommandState st;
  std::vector<std::string> log;
  st.drawing.userPolylineOffsets = {0, 3};
  for (const CadPolyVertex& v : {CadPolyVertex{0, 0, 0}, CadPolyVertex{2, 0, 0}, CadPolyVertex{4, 0, 0}})
    st.drawing.pushVertex(v);
  st.drawing.userPolylineVertsBulge = {0.f, 1.f, 0.f};
  st.drawing.userPolylineVertsNormal = {1.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 1.f};
  st.drawing.userPolylineClosed = {0};
  st.drawing.userPolylineAttrs = {EntityAttributes{}};
  st.selection = {{SelectedEntity::Type::Polyline, 0}};
  REQUIRE(DispatchByPrimaryForTest("move", st, log));
  SubmitViewportPick(st, 0.0, 0.0, log);
  const Picture ghost = Preview(st, 10.f, 5.f);
  REQUIRE(ghost.vertices.size() > 4);  // the arc is traced, not one chord
  for (size_t i = 2; i < ghost.vertices.size(); ++i) {
    const ray3d::Vec3& v = ghost.vertices[i];
    CHECK(v.y == Catch::Approx(5.0).margin(kTol));
    CHECK(Dist(v, {13.0, 5.0, 0.0}) == Catch::Approx(1.0).margin(kTol));
  }
}

TEST_CASE("MOVE's preview draws tables, multileaders, points and Position Markers where the commit puts them",
          "[issue1700][preview]") {
  AppCommandState st;
  std::vector<std::string> log;
  CadTable table;
  table.insX = 1.f;
  table.insY = 8.f;
  table.insZ = 0.5f;
  table.width = 10.f;
  table.height = 4.f;
  table.rotationRad = 0.4f;
  table.cells = {"A", "B"};
  st.drawing.cadTables.push_back(table);
  st.drawing.cadTableAttrs.push_back(EntityAttributes{});
  CadMultileader ml;
  ml.pathXyz = {0.f, 0.f, 1.f, 2.f, 1.f, 1.f, 3.f, 1.f, 1.f};
  ml.extraLeaderPaths = {{1.f, -1.f, 1.f, 3.f, 1.f, 1.f}};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.text = "ML";
  st.drawing.cadMultileaders.push_back(ml);
  st.drawing.cadMultileaderAttrs.push_back(EntityAttributes{});
  st.drawing.cadPoints.push_back(CadPoint{-3.0, 2.0, 2.0});
  st.drawing.cadPointAttrs.push_back(EntityAttributes{});
  CadPositionMarker pm;
  pm.x = 6.0;
  pm.y = -4.0;
  pm.z = 1.5f;
  pm.label.kind = CadAnnotation::Kind::Mtext;
  pm.label.text = "PM";
  st.drawing.cadPositionMarkers.push_back(pm);
  st.drawing.cadPositionMarkerAttrs.push_back(EntityAttributes{});
  using T = SelectedEntity::Type;
  st.selection = {{T::Table, 0}, {T::Multileader, 0}, {T::Point, 0}, {T::PositionMarker, 0}};
  REQUIRE(DispatchByPrimaryForTest("move", st, log));
  SubmitViewportPick(st, 0.0, 0.0, log);
  const Picture before = Preview(st, 5.f, 3.f);
  SubmitViewportPick(st, 5.0, 3.0, log);

  // What the commit left, read from the moved entities themselves.
  Picture committed;
  const CadTable& t = st.drawing.cadTables[0];
  for (int c = 0; c < 4; ++c) {
    float x = 0.f, y = 0.f;
    CadTableWorldCorner(t, c, &x, &y);
    committed.vertices.push_back({x, y, t.insZ});
  }
  const CadMultileader& m = st.drawing.cadMultileaders[0];
  for (size_t i = 0; i + 2 < m.pathXyz.size(); i += 3)
    committed.vertices.push_back({m.pathXyz[i], m.pathXyz[i + 1], m.pathXyz[i + 2]});
  for (size_t i = 0; i + 2 < m.extraLeaderPaths[0].size(); i += 3)
    committed.vertices.push_back(
        {m.extraLeaderPaths[0][i], m.extraLeaderPaths[0][i + 1], m.extraLeaderPaths[0][i + 2]});
  CHECK(m.pathXyz[0] == Catch::Approx(5.0));
  const CadPoint& p = st.drawing.cadPoints[0];
  const CadPositionMarker& marker = st.drawing.cadPositionMarkers[0];
  CHECK(p.x == Catch::Approx(2.0));
  CHECK(marker.x == Catch::Approx(11.0));
  REQUIRE(Covers(committed, before));

  // The point's cross and the marker's ring and cross are centred on their moved positions.
  const auto hasVertexNear = [&](double x, double y, double z, double reach) {
    for (const ray3d::Vec3& v : before.vertices)
      if (std::fabs(v.z - z) < kTol && std::hypot(v.x - x, v.y - y) < reach)
        return true;
    return false;
  };
  // A plus sign: one arm along X through the point, one along Y.
  bool armX = false, armY = false;
  for (const ray3d::Vec3& v : before.vertices) {
    armX = armX || (std::fabs(v.y - p.y) < kTol && std::fabs(v.z - p.z) < kTol && std::fabs(v.x - p.x) > kTol);
    armY = armY || (std::fabs(v.x - p.x) < kTol && std::fabs(v.z - p.z) < kTol && std::fabs(v.y - p.y) > kTol);
  }
  CHECK(armX);
  CHECK(armY);
  bool ring = false;
  for (const auto& r : before.rings)
    ring = ring || (std::fabs(r[0] - marker.x) < kTol && std::fabs(r[1] - marker.y) < kTol &&
                    std::fabs(r[2] - marker.z) < kTol && r[3] > 0.0);
  CHECK(ring);
  // Nothing in the ghost is left at the original positions.
  CHECK_FALSE(hasVertexNear(-3.0, 2.0, 2.0, 1e-3));
  CHECK_FALSE(hasVertexNear(0.0, 0.0, 1.0, 1e-3));
}
