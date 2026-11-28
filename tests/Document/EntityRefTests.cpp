// Pure Document unit tests for EntityKind, EntityRef, EntityKindCapabilities, EntityBounds3d,
// and SimilarityTransform (ADR-0011 / GitHub issue #1577 phase 1).
//
// Runs inside GoSurveyDocumentTests: links GoSurvey::Document and Catch2 only, with no Qt, OpenGL,
// or application-private include roots.

#include <GoSurvey/Document/EntityRef.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>

namespace doc = GoSurvey::Document;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 0.5 * kPi;

void CheckVecNear(const ray3d::Vec3& a, const ray3d::Vec3& b, double tol = 1e-11) {
  CHECK(a.x == Catch::Approx(b.x).margin(tol));
  CHECK(a.y == Catch::Approx(b.y).margin(tol));
  CHECK(a.z == Catch::Approx(b.z).margin(tol));
}

}  // namespace

TEST_CASE("EntityRef validity and equality", "[document][entitykindops]") {
  const EntityRef invalid{};
  CHECK_FALSE(invalid.valid());
  CHECK(invalid.kind == EntityKind::Line);
  CHECK(invalid.index == -1);

  const EntityRef a{EntityKind::Arc, 3};
  const EntityRef b{EntityKind::Arc, 3};
  const EntityRef c{EntityKind::Circle, 3};
  CHECK(a.valid());
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a != invalid);
}

TEST_CASE("EntityKind arrays cover every kind without duplicates", "[document][entitykindops]") {
  std::set<std::uint8_t> all;
  for (EntityKind k : doc::kAllEntityKinds)
    CHECK(all.insert(static_cast<std::uint8_t>(k)).second);
  CHECK(all.size() == doc::kAllEntityKinds.size());

  std::size_t inStoreCount = 0;
  std::size_t inBlockCount = 0;
  for (EntityKind k : doc::kAllEntityKinds) {
    const doc::EntityKindCapabilities cap = doc::CapabilitiesForKind(k);
    if (cap.inEntityStore)
      ++inStoreCount;
    if (cap.inBlockStore)
      ++inBlockCount;
  }
  CHECK(inStoreCount == doc::kDocumentEntityKinds.size());
  CHECK(inBlockCount == doc::kBlockStoreEntityKinds.size());

  for (EntityKind k : doc::kDocumentEntityKinds)
    CHECK(doc::CapabilitiesForKind(k).inEntityStore);
  for (EntityKind k : doc::kBlockStoreEntityKinds) {
    CHECK(doc::CapabilitiesForKind(k).inEntityStore);
    CHECK(doc::CapabilitiesForKind(k).inBlockStore);
  }
}

TEST_CASE("Capabilities declare refusal reasons for every unsupported op", "[document][entitykindops]") {
  // Core transformable kinds support everything.
  for (EntityKind k : {EntityKind::Line, EntityKind::Circle, EntityKind::Arc, EntityKind::Polyline}) {
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Move).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Copy).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::RotatePlan).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Rotate3d).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Scale).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Mirror).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Stretch).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Array).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Erase).supported);
  }

  // AutoCAD parity (issue #1577): ellipses, hatch fills, imported models, inserts, splines, points,
  // solids and pipe runs take every whole-object transform, including a tilted ROTATE and MIRROR.
  for (EntityKind k : {EntityKind::Ellipse, EntityKind::FilledRegion, EntityKind::Mesh, EntityKind::BlockRef,
                       EntityKind::Spline, EntityKind::Point, EntityKind::Solid, EntityKind::PipeRun}) {
    for (doc::EntityOp op : {doc::EntityOp::Move, doc::EntityOp::Copy, doc::EntityOp::RotatePlan,
                             doc::EntityOp::Rotate3d, doc::EntityOp::Scale, doc::EntityOp::Mirror,
                             doc::EntityOp::Array, doc::EntityOp::Erase})
      CHECK(doc::CheckOpSupport(k, op).supported);
  }

  // Surface and PointCloud are erasable only and state their refusal reason.
  for (EntityKind k : {EntityKind::Surface, EntityKind::PointCloud}) {
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Erase).supported);
    for (doc::EntityOp op : {doc::EntityOp::Move, doc::EntityOp::Copy, doc::EntityOp::RotatePlan,
                             doc::EntityOp::Rotate3d, doc::EntityOp::Scale, doc::EntityOp::Mirror,
                             doc::EntityOp::Stretch, doc::EntityOp::Array}) {
      const doc::OpSupport s = doc::CheckOpSupport(k, op);
      CHECK_FALSE(s.supported);
      CHECK_FALSE(s.refusalReason.empty());
    }
  }

  // A Position Marker moves, copies and erases only (REQ-359).
  CHECK(doc::CheckOpSupport(EntityKind::PositionMarker, doc::EntityOp::Move).supported);
  CHECK(doc::CheckOpSupport(EntityKind::PositionMarker, doc::EntityOp::Copy).supported);
  CHECK(doc::CheckOpSupport(EntityKind::PositionMarker, doc::EntityOp::Erase).supported);
  for (doc::EntityOp op : {doc::EntityOp::RotatePlan, doc::EntityOp::Rotate3d, doc::EntityOp::Scale,
                           doc::EntityOp::Mirror, doc::EntityOp::Stretch, doc::EntityOp::Array}) {
    const doc::OpSupport s = doc::CheckOpSupport(EntityKind::PositionMarker, op);
    CHECK_FALSE(s.supported);
    CHECK_FALSE(s.refusalReason.empty());
  }

  // Kinds with no plane normal rotate, scale and mirror in plan but refuse a tilted ROTATE, by name.
  for (EntityKind k : {EntityKind::Annotation, EntityKind::Table, EntityKind::Multileader}) {
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::RotatePlan).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Scale).supported);
    CHECK(doc::CheckOpSupport(k, doc::EntityOp::Mirror).supported);
    const doc::OpSupport tilt = doc::CheckOpSupport(k, doc::EntityOp::Rotate3d);
    CHECK_FALSE(tilt.supported);
    CHECK(tilt.refusalReason.find("plane normal") != std::string_view::npos);
  }

  // STRETCH moves part of an object; kinds that can only move whole say so.
  for (EntityKind k : {EntityKind::Solid, EntityKind::PipeRun, EntityKind::Spline, EntityKind::BlockRef,
                       EntityKind::Mesh, EntityKind::Multileader}) {
    const doc::OpSupport stretch = doc::CheckOpSupport(k, doc::EntityOp::Stretch);
    CHECK_FALSE(stretch.supported);
    CHECK_FALSE(stretch.refusalReason.empty());
  }
  CHECK(doc::CheckOpSupport(EntityKind::Point, doc::EntityOp::Stretch).supported);
}

TEST_CASE("EntityBounds3d expands points, ignores non-finite values, and merges boxes",
          "[document][entitykindops]") {
  doc::EntityBounds3d b;
  CHECK_FALSE(b.valid);
  CheckVecNear(b.center(), {0.0, 0.0, 0.0});

  const double nan = std::numeric_limits<double>::quiet_NaN();
  b.expand({nan, 1.0, 2.0});
  CHECK_FALSE(b.valid);

  b.expand({10.0, -5.0, 3.0});
  REQUIRE(b.valid);
  CheckVecNear(b.min, {10.0, -5.0, 3.0});
  CheckVecNear(b.max, {10.0, -5.0, 3.0});

  doc::EntityBounds3d other;
  other.expand({20.0, 15.0, -1.0});
  b.merge(other);
  CheckVecNear(b.min, {10.0, -5.0, -1.0});
  CheckVecNear(b.max, {20.0, 15.0, 3.0});
  CheckVecNear(b.center(), {15.0, 5.0, 1.0});
}

TEST_CASE("SimilarityTransform primitive factories and queries", "[document][entitykindops]") {
  SECTION("Identity") {
    const auto id = doc::SimilarityTransform::Identity();
    CHECK(id.valid());
    CHECK(id.isIdentity());
    CHECK(id.isTranslationOnly());
    CHECK(id.isIdentityRotation());
    CHECK_FALSE(id.isReflection());
    CHECK(id.isPlanarZ());
    CHECK(id.scale() == Catch::Approx(1.0));
    CHECK(id.orthogonalDeterminant() == Catch::Approx(1.0));
    CheckVecNear(id.applyPoint({3.0, -4.0, 5.0}), {3.0, -4.0, 5.0});
  }

  SECTION("Translation") {
    const auto t = doc::SimilarityTransform::Translation({10.0, -20.0, 5.0});
    CHECK(t.valid());
    CHECK_FALSE(t.isIdentity());
    CHECK(t.isTranslationOnly());
    CHECK(t.isPlanarZ());
    CheckVecNear(t.applyPoint({1.0, 2.0, 3.0}), {11.0, -18.0, 8.0});
    CheckVecNear(t.applyVector({1.0, 2.0, 3.0}), {1.0, 2.0, 3.0});
    CheckVecNear(t.applyUnitDirection({0.0, 0.0, 1.0}), {0.0, 0.0, 1.0});
  }

  SECTION("Plan rotation about Z") {
    const auto r = doc::SimilarityTransform::RotationZ(10.0, 20.0, kHalfPi);
    CHECK(r.valid());
    CHECK_FALSE(r.isTranslationOnly());
    CHECK_FALSE(r.isReflection());
    CHECK(r.isPlanarZ());
    CHECK(r.orthogonalDeterminant() == Catch::Approx(1.0));
    CheckVecNear(r.applyPoint({10.0, 20.0, 7.0}), {10.0, 20.0, 7.0});
    CheckVecNear(r.applyPoint({15.0, 20.0, 7.0}), {10.0, 25.0, 7.0});
    CheckVecNear(r.applyUnitDirection({1.0, 0.0, 0.0}), {0.0, 1.0, 0.0});
  }

  SECTION("3D rotation about a tilted axis") {
    // +90 deg about +Y through (0,0,0): (x,y,z) -> (z,y,-x)
    const auto r = doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, kHalfPi);
    CHECK(r.valid());
    CHECK_FALSE(r.isPlanarZ());
    CHECK_FALSE(r.isReflection());
    CHECK(r.orthogonalDeterminant() == Catch::Approx(1.0));
    CheckVecNear(r.applyPoint({10.0, 12.0, 3.0}), {3.0, 12.0, -10.0});
    CheckVecNear(r.applyUnitDirection({0.0, 0.0, 1.0}), {1.0, 0.0, 0.0});
  }

  SECTION("Uniform scale") {
    const auto s = doc::SimilarityTransform::UniformScale({10.0, 20.0, 30.0}, 2.5);
    CHECK(s.valid());
    CHECK(s.isIdentityRotation());
    CHECK_FALSE(s.isTranslationOnly());
    CHECK_FALSE(s.isReflection());
    CHECK(s.isPlanarZ());
    CHECK(s.scale() == Catch::Approx(2.5));
    CHECK(s.determinant() == Catch::Approx(15.625));
    CheckVecNear(s.applyPoint({12.0, 24.0, 32.0}), {15.0, 30.0, 35.0});
    CheckVecNear(s.applyVector({2.0, 0.0, 0.0}), {5.0, 0.0, 0.0});
    CheckVecNear(s.applyUnitDirection({1.0, 0.0, 0.0}), {1.0, 0.0, 0.0});
  }

  SECTION("Reflection across vertical line and arbitrary 3D plane") {
    const auto mXy = doc::SimilarityTransform::ReflectionAcrossLineXY(0.0, 0.0, 0.0, 10.0);
    CHECK(mXy.valid());
    CHECK(mXy.isReflection());
    CHECK(mXy.isPlanarZ());
    CHECK(mXy.orthogonalDeterminant() == Catch::Approx(-1.0));
    CheckVecNear(mXy.applyPoint({16.0, 12.0, 3.0}), {-16.0, 12.0, 3.0});
    CheckVecNear(mXy.applyUnitDirection({0.0, 0.0, 1.0}), {0.0, 0.0, 1.0});

    // Horizontal plane z = 5
    const auto mZ = doc::SimilarityTransform::ReflectionAcrossPlane({0.0, 0.0, 5.0}, {0.0, 0.0, 1.0});
    CHECK(mZ.valid());
    CHECK(mZ.isReflection());
    CHECK_FALSE(mZ.isPlanarZ());
    CheckVecNear(mZ.applyPoint({1.0, 2.0, 8.0}), {1.0, 2.0, 2.0});
    CheckVecNear(mZ.applyUnitDirection({0.0, 0.0, 1.0}), {0.0, 0.0, -1.0});
  }
}

TEST_CASE("SimilarityTransform rejects degenerate and non-finite inputs", "[document][entitykindops]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  CHECK_FALSE(doc::SimilarityTransform::Invalid().valid());
  CHECK_FALSE(doc::SimilarityTransform::Translation({nan, 0.0, 0.0}).valid());
  CHECK_FALSE(doc::SimilarityTransform::Translation({0.0, inf, 0.0}).valid());
  CHECK_FALSE(doc::SimilarityTransform::RotationAboutAxis({0, 0, 0}, {0, 0, 0}, 1.0).valid());
  CHECK_FALSE(doc::SimilarityTransform::RotationAboutAxis({0, 0, 0}, {0, 0, 1}, nan).valid());
  CHECK_FALSE(doc::SimilarityTransform::UniformScale({0, 0, 0}, 0.0).valid());
  CHECK_FALSE(doc::SimilarityTransform::UniformScale({0, 0, 0}, -2.0).valid());
  CHECK_FALSE(doc::SimilarityTransform::UniformScale({0, 0, 0}, nan).valid());
  CHECK_FALSE(doc::SimilarityTransform::ReflectionAcrossPlane({0, 0, 0}, {0, 0, 0}).valid());
  CHECK_FALSE(doc::SimilarityTransform::ReflectionAcrossLineXY(5.0, 5.0, 5.0, 5.0).valid());
  CHECK_FALSE(doc::SimilarityTransform::Compose(doc::SimilarityTransform::Invalid(),
                                                doc::SimilarityTransform::Identity())
                  .valid());
  CHECK_FALSE(doc::SimilarityTransform::Invalid().inverse().valid());
}

TEST_CASE("SimilarityTransform composition, inverse, and state-plane precision",
          "[document][entitykindops]") {
  // Compose: scale x2 about (1,2,3), then rotate 90 deg about tilted axis (1,1,1) at (4,5,6),
  // then reflect across plane x+y=10, then translate by (-3,7,-2).
  const auto s = doc::SimilarityTransform::UniformScale({1.0, 2.0, 3.0}, 2.0);
  const auto r = doc::SimilarityTransform::RotationAboutAxis({4.0, 5.0, 6.0}, {1.0, 1.0, 1.0}, kHalfPi);
  const auto m = doc::SimilarityTransform::ReflectionAcrossPlane({5.0, 5.0, 0.0}, {1.0, 1.0, 0.0});
  const auto t = doc::SimilarityTransform::Translation({-3.0, 7.0, -2.0});

  const auto composed =
      doc::SimilarityTransform::Compose(t, doc::SimilarityTransform::Compose(m, doc::SimilarityTransform::Compose(r, s)));
  REQUIRE(composed.valid());
  CHECK(composed.isReflection());
  CHECK(composed.scale() == Catch::Approx(2.0));
  CHECK(composed.orthogonalDeterminant() == Catch::Approx(-1.0));
  CHECK(composed.determinant() == Catch::Approx(-8.0));

  const ray3d::Vec3 p{13.5, -4.25, 8.75};
  const ray3d::Vec3 stepByStep = t.applyPoint(m.applyPoint(r.applyPoint(s.applyPoint(p))));
  const ray3d::Vec3 oneShot = composed.applyPoint(p);
  CheckVecNear(oneShot, stepByStep, 1e-10);

  // Two reflections compose back to a proper motion.
  const auto doubleRefl = doc::SimilarityTransform::Compose(m, m);
  CHECK(doubleRefl.valid());
  CHECK_FALSE(doubleRefl.isReflection());
  CHECK(doubleRefl.orthogonalDeterminant() == Catch::Approx(1.0));
  CheckVecNear(doubleRefl.applyPoint(p), p, 1e-10);

  // Inverse undoes the composed similarity transform exactly.
  const auto inv = composed.inverse();
  REQUIRE(inv.valid());
  CheckVecNear(inv.applyPoint(oneShot), p, 1e-10);
  CHECK(doc::SimilarityTransform::Compose(inv, composed).isIdentity(1e-10));

  // State-plane coordinate precision (~2e6 ft): rotation about a local pivot stays accurate well
  // below REQ-101's 0.001 ft threshold.
  const ray3d::Vec3 base{2'450'123.456, 1'875'432.125, 512.75};
  const ray3d::Vec3 pt{base.x + 10.0, base.y, base.z};
  const auto spRot = doc::SimilarityTransform::RotationAboutAxis(base, {0.0, 0.0, 1.0}, kHalfPi);
  CheckVecNear(spRot.applyPoint(pt), {base.x, base.y + 10.0, base.z}, 1e-9);
}
