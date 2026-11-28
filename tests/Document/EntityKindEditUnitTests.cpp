// Unit tests for GoSurvey/Document/EntityKindEdit.hpp: per-kind ERASE and STRETCH (ADR-0011,
// issue #1700 phase 4). The STRETCH cases quote AutoCAD 2025's results from
// tools/autocad-transform-oracle/ (crossing window [14, 25] x [5, 25], displacement (5, 3, 0)).

#include <GoSurvey/Document/EntityKindEdit.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace doc = GoSurvey::Document;

namespace {

void CheckVecNear(const ray3d::Vec3& a, const ray3d::Vec3& b, double tol = 1e-5) {
  CHECK(a.x == Catch::Approx(b.x).margin(tol));
  CHECK(a.y == Catch::Approx(b.y).margin(tol));
  CHECK(a.z == Catch::Approx(b.z).margin(tol));
}

/// The oracle's STRETCH: crossing window [14, 25] x [5, 25] in plan, displacement (5, 3, 0).
doc::StretchWindow OracleWindow() {
  doc::StretchWindow w;
  w.contains = [](const ray3d::Vec3& p) { return p.x >= 14.0 && p.x <= 25.0 && p.y >= 5.0 && p.y <= 25.0; };
  w.delta = {5.0, 3.0, 0.0};
  return w;
}

void AddPolyline(CadDrawingEntityStore& s, std::vector<CadPolyVertex> verts, bool closed, float bulge) {
  if (s.userPolylineOffsets.empty())
    s.userPolylineOffsets.push_back(0);
  for (const CadPolyVertex& v : verts) {
    s.pushVertex(v);
    s.userPolylineVertsBulge.push_back(bulge);
    s.userPolylineVertsNormal.insert(s.userPolylineVertsNormal.end(), {0.f, 0.f, 1.f});
  }
  s.userPolylineOffsets.push_back(s.userPolylineOffsets.back() + static_cast<int>(verts.size()));
  s.userPolylineClosed.push_back(closed ? 1 : 0);
  EntityAttributes a;
  a.layer = "P" + std::to_string(s.userPolylineAttrs.size());
  s.userPolylineAttrs.push_back(a);
}

}  // namespace

TEST_CASE("ErasePolyline removes one polyline and its per-vertex side-cars", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  AddPolyline(s, {{0, 0, 0}, {1, 0, 0}}, false, 0.1f);
  AddPolyline(s, {{5, 5, 0}, {6, 5, 0}, {6, 6, 0}}, true, 0.2f);
  AddPolyline(s, {{9, 9, 1}, {10, 9, 1}}, false, 0.3f);

  REQUIRE(doc::EraseEntity(s, {EntityKind::Polyline, 1}));
  REQUIRE(s.userPolylineOffsets == std::vector<int>{0, 2, 4});
  CHECK(s.vertexCount() == 4);
  CHECK(s.vertex(2).x == Catch::Approx(9.0));
  CHECK(s.userPolylineVertsBulge == std::vector<float>{0.1f, 0.1f, 0.3f, 0.3f});
  CHECK(s.userPolylineVertsNormal.size() == 12);
  CHECK(s.userPolylineClosed == std::vector<uint8_t>{0, 0});
  REQUIRE(s.userPolylineAttrs.size() == 2);
  CHECK(s.userPolylineAttrs[1].layer == "P2");

  // Erasing the last one leaves an empty table, not {0} (issue #60).
  REQUIRE(doc::EraseEntities(s, {{EntityKind::Polyline, 0}, {EntityKind::Polyline, 1}}) == 2);
  CHECK(s.userPolylineOffsets.empty());
  CHECK(s.vertexCount() == 0);
  CHECK(s.userPolylineVertsBulge.empty());
  CHECK_FALSE(doc::EraseEntity(s, {EntityKind::Polyline, 0}));
}

TEST_CASE("EraseEntities ignores duplicates and erases highest index first", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  for (int i = 0; i < 4; ++i) {
    CadPoint p;
    p.x = i;
    s.cadPoints.push_back(p);
    s.cadPointAttrs.push_back(EntityAttributes{});
  }
  const size_t n = doc::EraseEntities(
      s, {{EntityKind::Point, 1}, {EntityKind::Point, 3}, {EntityKind::Point, 1}, {EntityKind::Point, 9},
          {EntityKind::FeatureLine, 0}});
  CHECK(n == 2);
  REQUIRE(s.cadPoints.size() == 2);
  CHECK(s.cadPoints[0].x == 0.0);
  CHECK(s.cadPoints[1].x == 2.0);
  CHECK(s.cadPointAttrs.size() == 2);
}

TEST_CASE("Erasing a circle takes its plane normal with it", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  for (int i = 0; i < 3; ++i) {
    s.pushCircle(CadCircleRecord{static_cast<double>(i), 0.0, 0.0, 1.0});
    s.userCircleAttrs.push_back(EntityAttributes{});
    PushCircleNormal(s.userCircleNormals, 0.f, static_cast<float>(i), 1.f);
  }
  REQUIRE(doc::EraseEntity(s, {EntityKind::Circle, 1}));
  REQUIRE(s.circleCount() == 2);
  CHECK(s.circle(1).cx == 2.0);
  REQUIRE(s.userCircleNormals.size() == 6);
  CHECK(s.userCircleNormals[4] == 2.f);
}

TEST_CASE("Erasing a pipe run renumbers its piping networks", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  s.cadPipeRuns.resize(3);
  s.cadPipeRunAttrs.resize(3);
  CadPipingSystem sys;
  sys.name = "W";
  sys.pipeRunIndices = {0, 1, 2};
  s.cadPipingSystems.push_back(sys);
  REQUIRE(doc::EraseEntity(s, {EntityKind::PipeRun, 1}));
  CHECK(s.cadPipeRuns.size() == 2);
  CHECK(s.cadPipingSystems[0].pipeRunIndices == std::vector<int>{0, 1});
}

TEST_CASE("STRETCH moves an insert whose insertion point is inside the window", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadBlockRef in;
  in.xf.x = 16.0;
  in.xf.y = 12.0;
  CadBlockRef out;
  out.xf.x = 13.5;
  out.xf.y = 12.0;
  s.cadBlockRefs = {in, out};
  s.cadBlockRefAttrs.resize(2);
  const doc::StretchWindow w = OracleWindow();
  // Oracle insert-stretch-in: (16,12) -> (21,15). insert-stretch-out: (13.5,12) stays.
  CHECK(doc::StretchEntity(s, {EntityKind::BlockRef, 0}, w));
  CHECK_FALSE(doc::StretchEntity(s, {EntityKind::BlockRef, 1}, w));
  CheckVecNear({s.cadBlockRefs[0].xf.x, s.cadBlockRefs[0].xf.y, s.cadBlockRefs[0].xf.z}, {21.0, 15.0, 0.0});
  CheckVecNear({s.cadBlockRefs[1].xf.x, s.cadBlockRefs[1].xf.y, s.cadBlockRefs[1].xf.z}, {13.5, 12.0, 0.0});
}

TEST_CASE("STRETCH moves a fit spline's fit points inside the window and refits it", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadSpline sp;
  std::string why;
  REQUIRE(spline::MakeFit({{10, 12, 0}, {15, 14, 0}, {20, 12, 0}}, false, &sp, &why));
  s.cadSplines.push_back(sp);
  s.cadSplineAttrs.push_back(EntityAttributes{});
  REQUIRE(doc::StretchEntity(s, {EntityKind::Spline, 0}, OracleWindow()));
  // Oracle spline-stretch: fit points (10,12) (20,17) (25,15).
  const CadSpline& r = s.cadSplines[0];
  REQUIRE(r.fit.size() == 3);
  CheckVecNear(r.fit[0], {10, 12, 0});
  CheckVecNear(r.fit[1], {20, 17, 0});
  CheckVecNear(r.fit[2], {25, 15, 0});
  // The curve still passes through its ends.
  CheckVecNear(spline::Evaluate(r, spline::DomainMin(r)), {10, 12, 0}, 1e-6);
  CheckVecNear(spline::Evaluate(r, spline::DomainMax(r)), {25, 15, 0}, 1e-6);
}

TEST_CASE("STRETCH leaves a spline unchanged when the moved fit points cannot define a curve",
          "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadSpline sp;
  std::string why;
  REQUIRE(spline::MakeFit({{0, 10, 0}, {15, 10, 0}, {20, 10, 0}}, false, &sp, &why));
  s.cadSplines.push_back(sp);
  s.cadSplineAttrs.push_back(EntityAttributes{});
  doc::StretchWindow w;
  w.contains = [](const ray3d::Vec3& p) { return p.x > 14.0 && p.x < 16.0; };
  w.delta = {5.0, 0.0, 0.0};  // (15,10) onto its neighbour (20,10)
  std::vector<std::string> log;
  CHECK_FALSE(doc::StretchEntity(s, {EntityKind::Spline, 0}, w, &log));
  REQUIRE(s.cadSplines[0].fit.size() == sp.fit.size());
  for (size_t i = 0; i < sp.fit.size(); ++i)
    CheckVecNear(s.cadSplines[0].fit[i], sp.fit[i], 0.0);
  REQUIRE(s.cadSplines[0].ctrl.size() == sp.ctrl.size());
  for (size_t i = 0; i < sp.ctrl.size(); ++i)
    CheckVecNear(s.cadSplines[0].ctrl[i], sp.ctrl[i], 0.0);
  REQUIRE(log.size() == 1);
  CHECK(log[0].find("spline left unchanged") != std::string::npos);
}

TEST_CASE("STRETCH moves a CV spline's control points inside the window", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadSpline sp;
  std::string why;
  REQUIRE(spline::MakeControl({{10, 12, 0}, {12, 14, 0}, {16, 14, 0}, {20, 12, 0}}, false, &sp, &why));
  s.cadSplines.push_back(sp);
  s.cadSplineAttrs.push_back(EntityAttributes{});
  REQUIRE(doc::StretchEntity(s, {EntityKind::Spline, 0}, OracleWindow()));
  const CadSpline& r = s.cadSplines[0];
  CheckVecNear(r.ctrl[0], {10, 12, 0});
  CheckVecNear(r.ctrl[1], {12, 14, 0});
  CheckVecNear(r.ctrl[2], {21, 17, 0});
  CheckVecNear(r.ctrl[3], {25, 15, 0});
}

TEST_CASE("STRETCH moves a multileader's points inside the window, and its label with the landing",
          "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadMultileader ml;
  ml.pathXyz = {10.f, 12.f, 0.f, 19.f, 14.f, 0.f};  // tip outside, landing inside
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.insX = 19.36f;
  ml.label.insY = 14.f;
  ml.label.boxMinX = 19.36f;
  ml.label.boxMaxX = 21.f;
  ml.label.boxMinY = 13.5f;
  ml.label.boxMaxY = 14.5f;
  s.cadMultileaders.push_back(ml);
  ml.pathXyz = {10.f, 12.f, 0.f, 12.f, 14.f, 0.f};  // nothing inside
  s.cadMultileaders.push_back(ml);
  s.cadMultileaderAttrs.resize(2);

  // Oracle mleader-stretch-landing: the tip stays at (10,12), the landing goes (19,14) -> (24,17),
  // and the text moves with it.
  REQUIRE(doc::StretchEntity(s, {EntityKind::Multileader, 0}, OracleWindow()));
  const CadMultileader& r = s.cadMultileaders[0];
  CheckVecNear({r.pathXyz[0], r.pathXyz[1], r.pathXyz[2]}, {10, 12, 0});
  CheckVecNear({r.pathXyz[3], r.pathXyz[4], r.pathXyz[5]}, {24, 17, 0});
  CHECK(r.label.insX == Catch::Approx(24.36f));
  CHECK(r.label.insY == Catch::Approx(17.f));
  CHECK(r.label.boxMaxX == Catch::Approx(26.f));
  CHECK(r.label.boxMinY == Catch::Approx(16.5f));

  CHECK_FALSE(doc::StretchEntity(s, {EntityKind::Multileader, 1}, OracleWindow()));
  CHECK(s.cadMultileaders[1].label.insX == Catch::Approx(19.36f));
}

TEST_CASE("STRETCH refuses kinds that only move whole", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  s.cadPositionMarkers.resize(1);
  s.cadPositionMarkerAttrs.resize(1);
  s.cadPositionMarkers[0].x = 16.0;
  s.cadPositionMarkers[0].y = 12.0;
  CHECK_FALSE(doc::HasEntityStretch(EntityKind::PositionMarker));
  CHECK_FALSE(doc::HasEntityStretch(EntityKind::Solid));
  CHECK_FALSE(doc::HasEntityStretch(EntityKind::FeatureLine));
  CHECK_FALSE(doc::StretchEntity(s, {EntityKind::PositionMarker, 0}, OracleWindow()));
  CHECK(s.cadPositionMarkers[0].x == 16.0);
}

TEST_CASE("STRETCH moves an angular dimension whole when its text point is inside", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAngular;
  a.insX = 16.f;
  a.insY = 12.f;
  a.dimAngVertexX = 10.f;
  a.dimAngVertexY = 10.f;
  a.dimExt1X = 30.f;
  a.dimExt1Y = 10.f;
  a.dimExt2X = 10.f;
  a.dimExt2Y = 30.f;
  s.cadAnnotations.push_back(a);
  s.cadAnnotationAttrs.push_back(EntityAttributes{});
  REQUIRE(doc::StretchEntity(s, {EntityKind::Annotation, 0}, OracleWindow()));
  const CadAnnotation& r = s.cadAnnotations[0];
  CHECK(r.insX == Catch::Approx(21.f));
  CHECK(r.dimAngVertexX == Catch::Approx(15.f));
  CHECK(r.dimAngVertexY == Catch::Approx(13.f));
  CHECK(r.dimExt1X == Catch::Approx(35.f));
  CHECK(r.dimExt2Y == Catch::Approx(33.f));
}

TEST_CASE("STRETCH moves a tilted arc whole by its centre, and a flat arc takes Z only when whole",
          "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadArc tilted;
  tilted.cx = 16.0;
  tilted.cy = 12.0;
  tilted.r = 2.0;
  tilted.sweepRad = 1.f;
  tilted.nx = 1.f;
  tilted.nz = 0.f;
  CadArc flat;  // both endpoints well inside the window
  flat.cx = 18.0;
  flat.cy = 12.0;
  flat.r = 1.0;
  flat.startRad = 0.f;
  flat.sweepRad = 1.5f;
  CadArc partial = flat;  // start (25.5,12) outside, end inside
  partial.cx = 24.5;
  s.userArcs = {tilted, flat, partial};
  s.userArcAttrs.resize(3);
  doc::StretchWindow w = OracleWindow();
  w.delta.z = 2.0;
  REQUIRE(doc::StretchEntity(s, {EntityKind::Arc, 0}, w));
  CHECK(s.userArcs[0].cx == Catch::Approx(21.0));
  CHECK(s.userArcs[0].cy == Catch::Approx(15.0));
  CHECK(s.userArcs[0].r == Catch::Approx(2.0));
  CHECK(s.userArcs[0].z == Catch::Approx(2.0));
  CHECK(s.userArcs[0].nx == Catch::Approx(1.f));

  REQUIRE(doc::StretchEntity(s, {EntityKind::Arc, 1}, w));
  CHECK(s.userArcs[1].cx == Catch::Approx(23.0).margin(1e-4));
  CHECK(s.userArcs[1].z == Catch::Approx(2.0));

  REQUIRE(doc::StretchEntity(s, {EntityKind::Arc, 2}, w));
  CHECK(s.userArcs[2].z == Catch::Approx(0.0));  // one endpoint moved: the arc keeps its elevation
}

TEST_CASE("STRETCH reports a multileader changed when only an extra branch moves", "[document][entitykindedit]") {
  CadDrawingEntityStore s;
  CadMultileader ml;
  ml.pathXyz = {0.f, 0.f, 0.f, 2.f, 1.f, 0.f};
  ml.extraLeaderPaths = {{16.f, 12.f, 0.f, 2.f, 1.f, 0.f}};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  s.cadMultileaders.push_back(ml);
  s.cadMultileaderAttrs.resize(1);
  REQUIRE(doc::StretchEntity(s, {EntityKind::Multileader, 0}, OracleWindow()));
  CHECK(s.cadMultileaders[0].extraLeaderPaths[0][0] == Catch::Approx(21.f));
  CHECK(s.cadMultileaders[0].pathXyz[0] == Catch::Approx(0.f));
}
