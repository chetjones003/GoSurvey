// Unit tests for GoSurvey/Document/EntityKindOps.hpp (ADR-0011 / GitHub issue #1577 phase 1).
//
// Exercises per-kind transforms, exact 3D bounding boxes, duplication, EntityRef dispatch,
// deduplication, and store-wide kind/attribute iteration for lines, circles, arcs, polylines,
// splines, and CAD points.

#include <GoSurvey/Document/EntityKindOps.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace doc = GoSurvey::Document;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 0.5 * kPi;

void CheckVecNear(const ray3d::Vec3& a, const ray3d::Vec3& b, double tol = 1e-5) {
  CHECK(a.x == Catch::Approx(b.x).margin(tol));
  CHECK(a.y == Catch::Approx(b.y).margin(tol));
  CHECK(a.z == Catch::Approx(b.z).margin(tol));
}

void CheckBoundsNear(const doc::EntityBounds3d& b, const ray3d::Vec3& expMin,
                     const ray3d::Vec3& expMax, double tol = 1e-5) {
  REQUIRE(b.valid);
  CheckVecNear(b.min, expMin, tol);
  CheckVecNear(b.max, expMax, tol);
}

}  // namespace

TEST_CASE("Line transform, bounds, and duplicate", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  store.pushLine(CadLineRecord{10.0, 12.0, 3.0, 20.0, 16.0, 4.0});
  EntityAttributes a;
  a.id = 42;
  a.layer = "ROADS";
  a.color = "Red";
  store.userLineAttrs.push_back(a);

  CheckBoundsNear(doc::LineBounds(store, 0), {10.0, 12.0, 3.0}, {20.0, 16.0, 4.0});
  CHECK_FALSE(doc::LineBounds(store, 1).valid);

  // Duplicate preserves geometry and layer/color, clears id to 0.
  const EntityRef copyRef = doc::DuplicateLine(store, 0);
  REQUIRE(copyRef == EntityRef{EntityKind::Line, 1});
  REQUIRE(store.lineCount() == 2);
  REQUIRE(store.userLineAttrs.size() == 2);
  CHECK(store.userLineAttrs[1].id == 0);
  CHECK(store.userLineAttrs[1].layer == "ROADS");
  CHECK(store.userLineAttrs[1].color == "Red");

  // Transform copy in 3D: +90 deg about +Y at origin -> (x,y,z) -> (z,y,-x), then translate.
  const auto xf = doc::SimilarityTransform::Compose(
      doc::SimilarityTransform::Translation({1.0, 2.0, 3.0}),
      doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, kHalfPi));
  REQUIRE(doc::TransformLine(store, 1, xf));

  const CadLineRecord orig = store.line(0);
  CheckVecNear({orig.x0, orig.y0, orig.z0}, {10.0, 12.0, 3.0});
  CheckVecNear({orig.x1, orig.y1, orig.z1}, {20.0, 16.0, 4.0});

  const CadLineRecord moved = store.line(1);
  CheckVecNear({moved.x0, moved.y0, moved.z0}, {4.0, 14.0, -7.0});
  CheckVecNear({moved.x1, moved.y1, moved.z1}, {5.0, 18.0, -17.0});

  // Invalid index / transform fail cleanly.
  CHECK_FALSE(doc::TransformLine(store, 99, xf));
  CHECK_FALSE(doc::TransformLine(store, 0, doc::SimilarityTransform::Invalid()));
  CHECK_FALSE(doc::DuplicateLine(store, 99).valid());
}

TEST_CASE("Circle transform, bounds (flat and tilted), and duplicate", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  store.pushCircle(CadCircleRecord{16.0, 12.0, 3.0, 2.0});
  store.userCircleAttrs.push_back(EntityAttributes{7, "CIRCLES"});
  PushCircleNormal(store.userCircleNormals, 0.f, 0.f, 1.f);

  // Flat circle bounds: exact in XY, flat in Z.
  CheckBoundsNear(doc::CircleBounds(store, 0), {14.0, 10.0, 3.0}, {18.0, 14.0, 3.0});

  // Plan mirror keeps normal exact +Z.
  const EntityRef mirRef = doc::DuplicateCircle(store, 0);
  REQUIRE(mirRef == EntityRef{EntityKind::Circle, 1});
  CHECK(store.userCircleAttrs[1].id == 0);
  CHECK(store.userCircleAttrs[1].layer == "CIRCLES");
  REQUIRE(store.userCircleNormals.size() == 6);

  REQUIRE(doc::TransformCircle(store, 1, doc::SimilarityTransform::ReflectionAcrossLineXY(0.0, 0.0, 0.0, 10.0)));
  const CadCircleRecord mc = store.circle(1);
  CheckVecNear({mc.cx, mc.cy, mc.cz}, {-16.0, 12.0, 3.0});
  CHECK(mc.r == Catch::Approx(2.0));
  CHECK(IsFlatNormal(store.userCircleNormals[3], store.userCircleNormals[4], store.userCircleNormals[5]));

  // 3D rotation +90 deg about +Y tips normal (0,0,1) -> (1,0,0) (circle now lies in YZ plane)
  // and uniform scale x2 doubles the radius.
  const auto xf3d = doc::SimilarityTransform::Compose(
      doc::SimilarityTransform::UniformScale({0.0, 0.0, 0.0}, 2.0),
      doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, kHalfPi));
  REQUIRE(doc::TransformCircle(store, 0, xf3d));
  const CadCircleRecord tc = store.circle(0);
  CheckVecNear({tc.cx, tc.cy, tc.cz}, {6.0, 24.0, -32.0});
  CHECK(tc.r == Catch::Approx(4.0));
  CheckVecNear({store.userCircleNormals[0], store.userCircleNormals[1], store.userCircleNormals[2]},
               {1.0, 0.0, 0.0});
  // Exact 3D AABB of a YZ-plane circle: flat in X, radius 4 in Y and Z.
  CheckBoundsNear(doc::CircleBounds(store, 0), {6.0, 20.0, -36.0}, {6.0, 28.0, -28.0});

  // Tilting a circle when the normals sidecar was omitted populates the sidecar rather than losing the tilt.
  CadEntityStore bare;
  bare.pushCircle(CadCircleRecord{0.0, 0.0, 0.0, 5.0});
  REQUIRE(doc::TransformCircle(
      bare, 0, doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, kHalfPi)));
  REQUIRE(bare.userCircleNormals.size() == 3);
  CheckVecNear({bare.userCircleNormals[0], bare.userCircleNormals[1], bare.userCircleNormals[2]},
               {1.0, 0.0, 0.0});

  // Zero-radius circle has invalid bounds.
  bare.pushCircle(CadCircleRecord{1.0, 2.0, 3.0, 0.0});
  CHECK_FALSE(doc::CircleBounds(bare, 1).valid);
}

TEST_CASE("Arc transform (3D rotate, scale, vertical & horizontal mirror) and exact bounds",
          "[document][entitykindops]") {
  auto makeQuarterArc = []() {
    CadArc a;
    a.cx = 10.0;
    a.cy = 12.0;
    a.z = 3.0;
    a.r = 6.0;
    a.startRad = 0.f;
    a.sweepRad = static_cast<float>(kHalfPi);
    return a;
  };

  CadDrawingEntityStore store;
  store.userArcs.push_back(makeQuarterArc());
  store.userArcAttrs.push_back(EntityAttributes{9, "ARCS"});

  // Exact bounds of quarter arc [0..pi/2]: [10..16] x [12..18] x [3..3].
  CheckBoundsNear(doc::ArcBounds(store, 0), {10.0, 12.0, 3.0}, {16.0, 18.0, 3.0});

  // Arc crossing cardinal extremum (start = pi/4, sweep = pi/2 -> passes top at pi/2).
  CadArc topArc = makeQuarterArc();
  topArc.startRad = static_cast<float>(0.25 * kPi);
  topArc.sweepRad = static_cast<float>(kHalfPi);
  store.userArcs.push_back(topArc);
  store.userArcAttrs.push_back(EntityAttributes{});
  const double d = 6.0 * std::cos(0.25 * kPi);
  CheckBoundsNear(doc::ArcBounds(store, 1), {10.0 - d, 12.0 + d, 3.0}, {10.0 + d, 18.0, 3.0});

  // Negative sweep arc covering the exact same points produces the exact same bounds.
  CadArc negArc = topArc;
  negArc.startRad = static_cast<float>(0.75 * kPi);
  negArc.sweepRad = static_cast<float>(-kHalfPi);
  store.userArcs.push_back(negArc);
  store.userArcAttrs.push_back(EntityAttributes{});
  CheckBoundsNear(doc::ArcBounds(store, 2), {10.0 - d, 12.0 + d, 3.0}, {10.0 + d, 18.0, 3.0});

  // Check mirror across vertical plane (Y axis: x -> -x), horizontal plane (z -> -z), and oblique plane.
  const std::vector<doc::SimilarityTransform> transforms = {
      doc::SimilarityTransform::Translation({10.0, 20.0, 5.0}),
      doc::SimilarityTransform::RotationZ(0.0, 0.0, -kHalfPi),
      doc::SimilarityTransform::RotationAboutAxis({1.0, 2.0, 3.0}, {1.0, 2.0, -1.0}, 1.1),
      doc::SimilarityTransform::UniformScale({2.0, 3.0, 4.0}, 2.5),
      doc::SimilarityTransform::ReflectionAcrossLineXY(0.0, 0.0, 0.0, 10.0),
      doc::SimilarityTransform::ReflectionAcrossPlane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}),
      doc::SimilarityTransform::ReflectionAcrossPlane({1.0, 2.0, 3.0}, {1.0, -2.0, 3.0}),
  };

  for (const auto& xf : transforms) {
    CadDrawingEntityStore s;
    s.userArcs.push_back(makeQuarterArc());
    s.userArcAttrs.push_back(EntityAttributes{11, "ARCS"});
    const CadArc orig = s.userArcs[0];
    const ray3d::Vec3 pStart = CurveWorldPointOnArc(orig, static_cast<double>(orig.startRad));
    const ray3d::Vec3 pMid =
        CurveWorldPointOnArc(orig, static_cast<double>(orig.startRad) + 0.5 * static_cast<double>(orig.sweepRad));
    const ray3d::Vec3 pEnd =
        CurveWorldPointOnArc(orig, static_cast<double>(orig.startRad) + static_cast<double>(orig.sweepRad));

    REQUIRE(doc::TransformArc(s, 0, xf));
    const CadArc& tr = s.userArcs[0];
    const ray3d::Vec3 qStart = CurveWorldPointOnArc(tr, static_cast<double>(tr.startRad));
    const ray3d::Vec3 qMid =
        CurveWorldPointOnArc(tr, static_cast<double>(tr.startRad) + 0.5 * static_cast<double>(tr.sweepRad));
    const ray3d::Vec3 qEnd =
        CurveWorldPointOnArc(tr, static_cast<double>(tr.startRad) + static_cast<double>(tr.sweepRad));

    CheckVecNear({tr.cx, tr.cy, tr.z}, xf.applyPoint({orig.cx, orig.cy, orig.z}));
    CHECK(tr.r == Catch::Approx(orig.r * xf.scale()).margin(1e-5));
    // Midpoint always maps to midpoint; start & end swap under reflection.
    CheckVecNear(qMid, xf.applyPoint(pMid), 1e-4);
    if (xf.isReflection()) {
      CheckVecNear(qStart, xf.applyPoint(pEnd), 1e-4);
      CheckVecNear(qEnd, xf.applyPoint(pStart), 1e-4);
    } else {
      CheckVecNear(qStart, xf.applyPoint(pStart), 1e-4);
      CheckVecNear(qEnd, xf.applyPoint(pEnd), 1e-4);
    }

    // Bounds of the transformed arc always contain its start, mid and end points.
    const doc::EntityBounds3d tb = doc::ArcBounds(s, 0);
    REQUIRE(tb.valid);
    for (const ray3d::Vec3& pt : {qStart, qMid, qEnd}) {
      CHECK(pt.x >= tb.min.x - 1e-5);
      CHECK(pt.x <= tb.max.x + 1e-5);
      CHECK(pt.y >= tb.min.y - 1e-5);
      CHECK(pt.y <= tb.max.y + 1e-5);
      CHECK(pt.z >= tb.min.z - 1e-5);
      CHECK(pt.z <= tb.max.z + 1e-5);
    }
  }

  // Zero-radius arc keeps its startRad intact under rotation and reports invalid bounds.
  {
    CadEntityStore deg;
    CadArc zeroArc;
    zeroArc.cx = 5.0;
    zeroArc.cy = 6.0;
    zeroArc.z = 1.0;
    zeroArc.r = 0.0;
    zeroArc.startRad = 0.75f;
    zeroArc.sweepRad = 1.0f;
    deg.userArcs.push_back(zeroArc);
    CHECK_FALSE(doc::ArcBounds(deg, 0).valid);
    REQUIRE(doc::TransformArc(deg, 0, doc::SimilarityTransform::RotationZ(0.0, 0.0, kHalfPi)));
    CHECK(deg.userArcs[0].startRad == Catch::Approx(0.75f));
  }
}

TEST_CASE("Polyline transform, bulge reflection, tilted normal rotation, bounds, and duplicate",
          "[document][entitykindops]") {
  CadDrawingEntityStore store;
  // Open 2-vertex semicircular bulged polyline from (0,0,0) to (2,0,0) with bulge +1 (CCW -> dips to y = -1).
  store.userPolylineOffsets = {0, 2};
  store.pushVertex({0.0, 0.0, 0.0});
  store.pushVertex({2.0, 0.0, 0.0});
  store.userPolylineVertsBulge = {1.f, 0.f};
  store.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  store.userPolylineClosed = {0};
  store.userPolylineAttrs.push_back(EntityAttributes{55, "PLINE"});

  // Exact bounds include the bowed semicircle at y = -1!
  CheckBoundsNear(doc::PolylineBounds(store, 0), {0.0, -1.0, 0.0}, {2.0, 0.0, 0.0});

  // Duplicate preserves bulge, normal, closed flag, and layer; zeroes id.
  const EntityRef copyRef = doc::DuplicatePolyline(store, 0);
  REQUIRE(copyRef == EntityRef{EntityKind::Polyline, 1});
  REQUIRE(store.userPolylineOffsets.size() == 3);
  REQUIRE(store.vertexCount() == 4);
  REQUIRE(store.userPolylineVertsBulge.size() == 4);
  REQUIRE(store.userPolylineVertsNormal.size() == 12);
  CHECK(store.userPolylineVertsBulge[2] == Catch::Approx(1.f));
  CHECK(store.userPolylineAttrs[1].id == 0);
  CHECK(store.userPolylineAttrs[1].layer == "PLINE");

  // Mirror the copy across Y axis (x -> -x): vertices become (0,0)->(-2,0), bulge negates to -1
  // so the semicircle still bows down to y = -1 (not up to y = +1!).
  REQUIRE(doc::TransformPolyline(
      store, 1, doc::SimilarityTransform::ReflectionAcrossLineXY(0.0, 0.0, 0.0, 10.0)));
  CHECK(store.userPolylineVertsBulge[2] == Catch::Approx(-1.f));
  CheckBoundsNear(doc::PolylineBounds(store, 1), {-2.0, -1.0, 0.0}, {0.0, 0.0, 0.0});

  // Rotate the original +90 deg about +X: (x,y,z) -> (x,-z,y), so (0,0,0)->(2,0,0) stays along X,
  // bulge normal (0,0,1) rotates to (0,-1,0), and the semicircle now bows to z = -1!
  REQUIRE(doc::TransformPolyline(
      store, 0, doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, kHalfPi)));
  CheckVecNear({store.userPolylineVertsNormal[0], store.userPolylineVertsNormal[1],
                store.userPolylineVertsNormal[2]},
               {0.0, -1.0, 0.0});
  // Straight trailing vertex keeps default +Z normal.
  CHECK(IsFlatNormal(store.userPolylineVertsNormal[3], store.userPolylineVertsNormal[4],
                     store.userPolylineVertsNormal[5]));
  CheckBoundsNear(doc::PolylineBounds(store, 0), {0.0, 0.0, -1.0}, {2.0, 0.0, 0.0});

  // Malformed CSR tail (offset back != vertexCount) is refused rather than corrupting offsets.
  CadEntityStore malformed;
  malformed.userPolylineOffsets = {0, 2};
  malformed.pushVertex({0.0, 0.0, 0.0});
  malformed.pushVertex({1.0, 0.0, 0.0});
  malformed.pushVertex({2.0, 0.0, 0.0});
  CHECK_FALSE(doc::DuplicatePolyline(malformed, 0).valid());
}

TEST_CASE("Spline and CAD Point transform, bounds, and duplicate", "[document][entitykindops]") {
  CadDrawingEntityStore store;

  // Degree-1 spline with fit points.
  CadSpline sp;
  sp.degree = 1;
  sp.ctrl = {{10.0, 12.0, 3.0}, {20.0, 14.0, 4.0}};
  sp.knots = {0.0, 0.0, 1.0, 1.0};
  sp.fit = {{10.0, 12.0, 3.0}, {15.0, 13.0, 3.5}, {20.0, 14.0, 4.0}};
  store.cadSplines.push_back(sp);
  store.cadSplineAttrs.push_back(EntityAttributes{77, "SPLINES"});

  store.cadPoints.push_back(CadPoint{16.0, 12.0, 3.0});
  store.cadPointAttrs.push_back(EntityAttributes{88, "POINTS"});

  CheckBoundsNear(doc::SplineBounds(store, 0), {10.0, 12.0, 3.0}, {20.0, 14.0, 4.0});
  CheckBoundsNear(doc::PointBounds(store, 0), {16.0, 12.0, 3.0}, {16.0, 12.0, 3.0});

  const EntityRef spCopy = doc::DuplicateSpline(store, 0);
  const EntityRef ptCopy = doc::DuplicatePoint(store, 0);
  REQUIRE(spCopy == EntityRef{EntityKind::Spline, 1});
  REQUIRE(ptCopy == EntityRef{EntityKind::Point, 1});
  CHECK(store.cadSplineAttrs[1].id == 0);
  CHECK(store.cadSplineAttrs[1].layer == "SPLINES");
  CHECK(store.cadPointAttrs[1].id == 0);
  CHECK(store.cadPointAttrs[1].layer == "POINTS");

  const auto xf = doc::SimilarityTransform::UniformScale({0.0, 0.0, 0.0}, 2.0);
  REQUIRE(doc::TransformSpline(store, 1, xf));
  REQUIRE(doc::TransformPoint(store, 1, xf));

  CheckVecNear(store.cadSplines[1].ctrl[0], {20.0, 24.0, 6.0});
  CheckVecNear(store.cadSplines[1].ctrl[1], {40.0, 28.0, 8.0});
  CheckVecNear(store.cadSplines[1].fit[1], {30.0, 26.0, 7.0});
  CheckVecNear({store.cadPoints[1].x, store.cadPoints[1].y, store.cadPoints[1].z}, {32.0, 24.0, 6.0});
}

TEST_CASE("EntityRef dispatch: COPY = DuplicateEntities + TransformEntities, deduplication, and round-trip",
          "[document][entitykindops]") {
  CadDrawingEntityStore store;
  store.pushLine({1.0, 2.0, 3.0, 5.0, 6.0, 7.0});
  store.userLineAttrs.push_back(EntityAttributes{1, "L"});
  store.pushCircle({10.0, 20.0, 5.0, 3.0});
  store.userCircleAttrs.push_back(EntityAttributes{2, "C"});
  PushCircleNormal(store.userCircleNormals, 0.f, 0.f, 1.f);
  CadArc arc;
  arc.cx = 4.0;
  arc.cy = 5.0;
  arc.z = 1.0;
  arc.r = 2.0;
  arc.startRad = 0.2f;
  arc.sweepRad = 1.4f;
  store.userArcs.push_back(arc);
  store.userArcAttrs.push_back(EntityAttributes{3, "A"});
  store.userPolylineOffsets = {0, 2};
  store.pushVertex({0.0, 0.0, 1.0});
  store.pushVertex({4.0, 0.0, 1.0});
  store.userPolylineVertsBulge = {0.5f, 0.f};
  store.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  store.userPolylineClosed = {0};
  store.userPolylineAttrs.push_back(EntityAttributes{4, "P"});
  CadSpline sp;
  sp.degree = 1;
  sp.ctrl = {{0.0, 1.0, 2.0}, {3.0, 4.0, 5.0}};
  sp.knots = {0.0, 0.0, 1.0, 1.0};
  store.cadSplines.push_back(sp);
  store.cadSplineAttrs.push_back(EntityAttributes{5, "S"});
  store.cadPoints.push_back(CadPoint{7.0, 8.0, 9.0});
  store.cadPointAttrs.push_back(EntityAttributes{6, "PT"});

  // Include duplicate refs and an invalid ref to verify deduplication & safety.
  const std::vector<EntityRef> refs = {
      {EntityKind::Line, 0},     {EntityKind::Line, 0}, {EntityKind::Circle, 0},
      {EntityKind::Arc, 0},      {EntityKind::Polyline, 0}, {EntityKind::Spline, 0},
      {EntityKind::Point, 0},    EntityRef{},           {EntityKind::Line, 99},
  };

  // COPY = DuplicateEntities, then TransformEntities on the returned handles.
  const std::vector<EntityRef> copies = doc::DuplicateEntities(store, refs);
  REQUIRE(copies.size() == 6);

  // Full composed 3D similarity transform (rotate + scale + mirror + translate).
  const auto xf = doc::SimilarityTransform::Compose(
      doc::SimilarityTransform::Translation({10.0, -5.0, 3.0}),
      doc::SimilarityTransform::Compose(
          doc::SimilarityTransform::ReflectionAcrossPlane({1.0, 2.0, 3.0}, {1.0, 1.0, 1.0}),
          doc::SimilarityTransform::Compose(
              doc::SimilarityTransform::UniformScale({0.0, 0.0, 0.0}, 1.5),
              doc::SimilarityTransform::RotationAboutAxis({2.0, 3.0, 4.0}, {0.0, 1.0, 1.0}, 0.7))));

  // Pass copies twice to verify TransformEntities deduplicates too.
  std::vector<EntityRef> doubledCopies = copies;
  doubledCopies.insert(doubledCopies.end(), copies.begin(), copies.end());
  CHECK(doc::TransformEntities(store, doubledCopies, xf) == 6);

  // Applying inverse restores every copied entity to match the untouched original!
  CHECK(doc::TransformEntities(store, copies, xf.inverse()) == 6);

  for (size_t i = 0; i < copies.size(); ++i) {
    const EntityRef origRef{copies[i].kind, 0};
    const doc::EntityBounds3d origB = doc::EntityBounds(store, origRef, {});
    const doc::EntityBounds3d copyB = doc::EntityBounds(store, copies[i], {});
    REQUIRE(origB.valid);
    REQUIRE(copyB.valid);
    CheckVecNear(copyB.min, origB.min, 1e-4);
    CheckVecNear(copyB.max, origB.max, 1e-4);
  }
}

TEST_CASE("ForEachDocumentKind and ForEachEntityAttrs cover every Document-owned kind",
          "[document][entitykindops]") {
  CadDrawingEntityStore store;
  std::size_t kindVisits = 0;
  doc::ForEachDocumentKind([&](EntityKind k) {
    ++kindVisits;
    auto* attrs = doc::EntityAttrsForKind(store, k);
    REQUIRE(attrs != nullptr);
    attrs->push_back(EntityAttributes{0, "OLD"});
  });
  CHECK(kindVisits == doc::kDocumentEntityKinds.size());

  // Non-Document kinds return nullptr from EntityAttrsForKind.
  CHECK(doc::EntityAttrsForKind(store, EntityKind::FeatureLine) == nullptr);
  CHECK(doc::EntityAttrsForKind(store, EntityKind::Surface) == nullptr);
  CHECK(doc::EntityAttrsForKind(store, EntityKind::PointCloud) == nullptr);

  // Rename layer across every Document kind in one pass (the pattern #1555 will use).
  std::size_t renamed = 0;
  doc::ForEachEntityAttrs(store, [&](EntityKind, std::vector<EntityAttributes>& attrs) {
    for (EntityAttributes& a : attrs) {
      if (a.layer == "OLD") {
        a.layer = "NEW";
        ++renamed;
      }
    }
  });
  CHECK(renamed == doc::kDocumentEntityKinds.size());

  // Verify every kind's attribute was updated.
  doc::ForEachEntityAttrs(static_cast<const CadDrawingEntityStore&>(store),
                          [&](EntityKind, const std::vector<EntityAttributes>& attrs) {
                            REQUIRE(attrs.size() == 1);
                            CHECK(attrs[0].layer == "NEW");
                          });
}

TEST_CASE("PR-Agent boundary regressions: SIZE_MAX index, near-axis circle bounds, DuplicateEntities snapshot, block-store bulged polyline, curved spline bounds",
          "[document][entitykindops]") {
  const auto idXf = doc::SimilarityTransform::Identity();

  SECTION("SIZE_MAX index never wraps or reads out of bounds") {
    const std::size_t maxIdx = std::numeric_limits<std::size_t>::max();
    CadDrawingEntityStore store;
    store.pushLine({0.0, 0.0, 0.0, 1.0, 0.0, 0.0});
    store.pushCircle({0.0, 0.0, 0.0, 1.0});
    store.userArcs.push_back(CadArc{0.0, 0.0, 1.0, 0.f, 1.f, 0.0, 0.f, 0.f, 1.f});
    store.userPolylineOffsets = {0, 2};
    store.pushVertex({0.0, 0.0, 0.0});
    store.pushVertex({1.0, 0.0, 0.0});
    store.cadSplines.push_back(CadSpline{1, false, {0.0, 0.0, 1.0, 1.0}, {{0, 0, 0}, {1, 0, 0}}, {}, {}});
    store.cadPoints.push_back(CadPoint{0.0, 0.0, 0.0});

    CHECK_FALSE(doc::TransformLine(store, maxIdx, idXf));
    CHECK_FALSE(doc::TransformCircle(store, maxIdx, idXf));
    CHECK_FALSE(doc::TransformArc(store, maxIdx, idXf));
    CHECK_FALSE(doc::TransformPolyline(static_cast<CadEntityStore&>(store), maxIdx, idXf));
    CHECK_FALSE(doc::TransformPolyline(store, maxIdx, idXf));
    CHECK_FALSE(doc::TransformSpline(store, maxIdx, idXf));
    CHECK_FALSE(doc::TransformPoint(store, maxIdx, idXf));

    CHECK_FALSE(doc::LineBounds(store, maxIdx).valid);
    CHECK_FALSE(doc::CircleBounds(store, maxIdx).valid);
    CHECK_FALSE(doc::ArcBounds(store, maxIdx).valid);
    CHECK_FALSE(doc::PolylineBounds(static_cast<const CadEntityStore&>(store), maxIdx).valid);
    CHECK_FALSE(doc::PolylineBounds(store, maxIdx).valid);
    CHECK_FALSE(doc::SplineBounds(store, maxIdx).valid);
    CHECK_FALSE(doc::PointBounds(store, maxIdx).valid);

    CHECK_FALSE(doc::DuplicateLine(store, maxIdx).valid());
    CHECK_FALSE(doc::DuplicateCircle(store, maxIdx).valid());
    CHECK_FALSE(doc::DuplicateArc(store, maxIdx).valid());
    CHECK_FALSE(doc::DuplicatePolyline(static_cast<CadEntityStore&>(store), maxIdx).valid());
    CHECK_FALSE(doc::DuplicatePolyline(store, maxIdx).valid());
    CHECK_FALSE(doc::DuplicateSpline(store, maxIdx).valid());
    CHECK_FALSE(doc::DuplicatePoint(store, maxIdx).valid());
  }

  SECTION("CircleBounds avoids cancellation for nearly axis-aligned normal") {
    CadEntityStore store;
    store.pushCircle(CadCircleRecord{0.0, 0.0, 100.0, 1.0e6});
    PushCircleNormal(store.userCircleNormals, 1.0e-8f, 0.f, 1.f);
    const doc::EntityBounds3d b = doc::CircleBounds(store, 0);
    REQUIRE(b.valid);
    const double expectedDz = 1.0e6 * static_cast<double>(1.0e-8f);
    CHECK(b.max.z - 100.0 == Catch::Approx(expectedDz).epsilon(1e-4));
    CHECK(100.0 - b.min.z == Catch::Approx(expectedDz).epsilon(1e-4));
  }

  SECTION("DuplicateEntities validates handles against original counts, not newly appended copies") {
    CadDrawingEntityStore store;
    store.pushLine({0.0, 0.0, 0.0, 5.0, 0.0, 0.0});
    store.userLineAttrs.push_back(EntityAttributes{1, "0"});

    // Index 1 is out of range before duplication; duplicating index 0 must NOT make index 1 valid.
    const std::vector<EntityRef> copies =
        doc::DuplicateEntities(store, {{EntityKind::Line, 0}, {EntityKind::Line, 1}});
    REQUIRE(copies.size() == 1);
    CHECK(copies[0] == EntityRef{EntityKind::Line, 1});
    CHECK(store.lineCount() == 2);
  }

  SECTION("CadEntityStore rejects out-of-plane transforms on bulged polylines; CadDrawingEntityStore handles them") {
    CadEntityStore blockStore;
    blockStore.userPolylineOffsets = {0, 2};
    blockStore.pushVertex({0.0, 0.0, 0.0});
    blockStore.pushVertex({2.0, 0.0, 0.0});
    blockStore.userPolylineVertsBulge = {1.f, 0.f};

    const auto rotX90 =
        doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, kHalfPi);
    const auto reflHoriz =
        doc::SimilarityTransform::ReflectionAcrossPlane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});

    CHECK_FALSE(doc::TransformPolyline(blockStore, 0, rotX90));
    CHECK_FALSE(doc::TransformPolyline(blockStore, 0, reflHoriz));
    CHECK(blockStore.userPolylineVertsBulge[0] == Catch::Approx(1.f));

    // Straight polyline in CadEntityStore still accepts out-of-plane 3D rotation.
    blockStore.userPolylineVertsBulge[0] = 0.f;
    CHECK(doc::TransformPolyline(blockStore, 0, rotX90));

    // Drawing store preserves exact geometry under horizontal reflection by flipping normal to -Z and bulge to -1.
    CadDrawingEntityStore drawStore;
    drawStore.userPolylineOffsets = {0, 2};
    drawStore.pushVertex({0.0, 0.0, 0.0});
    drawStore.pushVertex({2.0, 0.0, 0.0});
    drawStore.userPolylineVertsBulge = {1.f, 0.f};
    drawStore.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    drawStore.userPolylineClosed = {0};
    REQUIRE(doc::TransformPolyline(drawStore, 0, reflHoriz));
    CHECK(drawStore.userPolylineVertsBulge[0] == Catch::Approx(-1.f));
    CheckVecNear({drawStore.userPolylineVertsNormal[0], drawStore.userPolylineVertsNormal[1],
                  drawStore.userPolylineVertsNormal[2]},
                 {0.0, 0.0, -1.0});
    CheckBoundsNear(doc::PolylineBounds(drawStore, 0), {0.0, -1.0, 0.0}, {2.0, 0.0, 0.0});
  }

  SECTION("SplineBounds conservatively encloses curved degree-3 splines while staying exact in flat planes") {
    CadDrawingEntityStore store;
    CadSpline sp;
    std::string why;
    REQUIRE(spline::MakeControl({{0.0, 0.0, 5.0}, {5.0, 10.0, 5.0}, {15.0, 10.0, 5.0}, {20.0, 0.0, 5.0}},
                                false, &sp, &why));
    store.cadSplines.push_back(sp);

    const doc::EntityBounds3d b = doc::SplineBounds(store, 0);
    REQUIRE(b.valid);
    // Planar Z is clamped to the control hull: never inflated off z = 5.
    CHECK(b.min.z == Catch::Approx(5.0).margin(1e-12));
    CHECK(b.max.z == Catch::Approx(5.0).margin(1e-12));

    // Every densely evaluated point on the curve lies inside [b.min, b.max] (allowing 1e-12 for
    // De Boor floating-point rounding on the exact planar Z hull).
    const double t0 = spline::DomainMin(sp);
    const double t1 = spline::DomainMax(sp);
    for (int i = 0; i <= 1000; ++i) {
      const double t = t0 + (t1 - t0) * static_cast<double>(i) / 1000.0;
      const ray3d::Vec3 p = spline::Evaluate(sp, t);
      CHECK(p.x >= b.min.x - 1e-12);
      CHECK(p.x <= b.max.x + 1e-12);
      CHECK(p.y >= b.min.y - 1e-12);
      CHECK(p.y <= b.max.y + 1e-12);
      CHECK(p.z >= b.min.z - 1e-12);
      CHECK(p.z <= b.max.z + 1e-12);
    }
  }
}

TEST_CASE("CopyEntityInto copies one entity into another store", "[document][entitykindops][issue1700]") {
  CadDrawingEntityStore src;
  src.pushLine(CadLineRecord{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
  src.userLineAttrs.push_back(EntityAttributes{});
  src.userLineAttrs.back().layer = "L1";
  src.pushCircle(CadCircleRecord{10.0, 0.0, 2.0, 3.0});
  src.userCircleAttrs.push_back(EntityAttributes{});
  PushCircleNormal(src.userCircleNormals, 1.f, 0.f, 0.f);
  // Two polylines, so the copy reads the second one's own vertex range and side-cars.
  src.userPolylineOffsets = {0, 2, 5};
  for (const CadPolyVertex& v : {CadPolyVertex{0, 0, 0}, CadPolyVertex{1, 0, 0}, CadPolyVertex{5, 0, 1},
                                 CadPolyVertex{6, 1, 1}, CadPolyVertex{7, 0, 1}})
    src.pushVertex(v);
  src.userPolylineVertsBulge = {0.f, 0.f, 0.5f, 0.f, 0.f};
  src.userPolylineVertsNormal = {0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1};
  src.userPolylineClosed = {0, 1};
  src.userPolylineAttrs = {EntityAttributes{}, EntityAttributes{}};
  src.cadSplines.push_back(CadSpline{});
  src.cadSplineAttrs.push_back(EntityAttributes{});

  CadDrawingEntityStore dst;
  const EntityRef line = doc::CopyEntityInto(dst, src, {EntityKind::Line, 0});
  REQUIRE(line.valid());
  CHECK(dst.lineCount() == 1);
  CHECK(dst.line(0).z1 == 6.0);
  CHECK(dst.userLineAttrs.at(0).layer == "L1");

  const EntityRef circle = doc::CopyEntityInto(dst, src, {EntityKind::Circle, 0});
  REQUIRE(circle.valid());
  CHECK(dst.circle(0).r == 3.0);
  REQUIRE(dst.userCircleNormals.size() == 3);
  CHECK(dst.userCircleNormals[0] == 1.f);  // the plane comes with the circle

  const EntityRef poly = doc::CopyEntityInto(dst, src, {EntityKind::Polyline, 1});
  REQUIRE(poly.valid());
  CHECK(poly.index == 0);
  REQUIRE(dst.userPolylineOffsets == std::vector<int>{0, 3});
  CHECK(dst.vertex(0).x == 5.0);
  CHECK(dst.vertex(2).x == 7.0);
  CHECK(dst.userPolylineVertsBulge == std::vector<float>{0.5f, 0.f, 0.f});
  REQUIRE(dst.userPolylineVertsNormal.size() == 9);
  CHECK(dst.userPolylineVertsNormal[1] == 1.f);  // the bulged segment's tilted plane
  CHECK(dst.userPolylineClosed == std::vector<std::uint8_t>{1});

  // The copy can be transformed like any other entity, and the source is untouched.
  REQUIRE(doc::TransformEntity(dst, poly, doc::SimilarityTransform::Translation({0.0, 0.0, 10.0})));
  CHECK(dst.vertex(0).z == 11.0);
  CHECK(src.vertex(2).z == 1.0);

  CHECK(doc::CopyEntityInto(dst, src, {EntityKind::Spline, 0}).valid());
  CHECK_FALSE(doc::CopyEntityInto(dst, src, {EntityKind::Arc, 0}).valid());       // out of range
  CHECK_FALSE(doc::CopyEntityInto(dst, src, {EntityKind::Polyline, 2}).valid());  // out of range
  CHECK_FALSE(doc::CopyEntityInto(dst, src, {EntityKind::FeatureLine, 0}).valid());  // not Document's
}
