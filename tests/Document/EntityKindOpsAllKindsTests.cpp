// Unit tests for the remaining Document entity-kind operations (ADR-0011 / GitHub issue #1577):
// ellipses, annotations, tables, block references, hatch fills, meshes, solids, pipe runs,
// multileaders and Position Markers.
//
// Expected values marked "AutoCAD 2025" were read back from accoreconsole.exe running the same
// command on the same entity (tools/autocad-transform-oracle/). Everything else is a property
// check: the transformed entity's points are the transform of the original's points.

#include <GoSurvey/Document/EntityKindOps.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace doc = GoSurvey::Document;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 0.5 * kPi;
constexpr double kDeg = kPi / 180.0;

void CheckVecNear(const ray3d::Vec3& a, const ray3d::Vec3& b, double tol = 1e-4) {
  INFO("actual=(" << a.x << "," << a.y << "," << a.z << ") expected=(" << b.x << "," << b.y << "," << b.z << ")");
  CHECK(a.x == Catch::Approx(b.x).margin(tol));
  CHECK(a.y == Catch::Approx(b.y).margin(tol));
  CHECK(a.z == Catch::Approx(b.z).margin(tol));
}

void CheckAngleNear(double a, double b, double tol = 1e-5) {
  const double d = std::remainder(a - b, 2.0 * kPi);
  INFO("actual=" << a << " expected=" << b);
  CHECK(std::fabs(d) <= tol);
}

// ROTATE "-90" about the origin (AutoCAD's ROTATE angle is CCW).
doc::SimilarityTransform RotMinus90() { return doc::SimilarityTransform::RotationZ(0.0, 0.0, -kHalfPi); }
// MIRROR across the world Y axis, (0,0)-(0,10).
doc::SimilarityTransform MirrorY() { return doc::SimilarityTransform::ReflectionAcrossLineXY(0.0, 0.0, 0.0, 10.0); }
// SCALE 2 about the origin.
doc::SimilarityTransform Scale2() { return doc::SimilarityTransform::UniformScale({0.0, 0.0, 0.0}, 2.0); }
// ROTATE3D about the world Y axis by 90 degrees: (x, y, z) -> (z, y, -x).
doc::SimilarityTransform Rot3dY90() {
  return doc::SimilarityTransform::RotationAboutAxis({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, kHalfPi);
}

// A composite that exercises every part at once: rotation about a tilted axis, scale, reflection
// across a tilted plane and a translation at state-plane magnitude.
doc::SimilarityTransform General3d() {
  using X = doc::SimilarityTransform;
  return X::Compose(X::Translation({2.0e6, -1.0e5, 7.0}),
                    X::Compose(X::ReflectionAcrossPlane({1.0, 2.0, 3.0}, {1.0, -2.0, 0.5}),
                               X::Compose(X::UniformScale({4.0, 5.0, 6.0}, 1.75),
                                          X::RotationAboutAxis({2.0, 3.0, 4.0}, {0.3, 1.0, 0.7}, 0.9))));
}

// A planar composite: rotate 37 degrees, scale 1.5, mirror across a slanted line, move.
doc::SimilarityTransform GeneralPlanar() {
  using X = doc::SimilarityTransform;
  return X::Compose(X::Translation({12.0, -7.0, 2.0}),
                    X::Compose(X::ReflectionAcrossLineXY(1.0, 2.0, 4.0, 7.0),
                               X::Compose(X::UniformScale({3.0, 1.0, 0.5}, 1.5), X::RotationZ(2.0, -1.0, 37.0 * kDeg))));
}

CadAnnotation MakeText(float x, float y, float rotRad, const char* text = "AB") {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Text;
  a.insX = x;
  a.insY = y;
  a.rotationRad = rotRad;
  a.plottedHeightInches = 1.f;
  a.text = text;
  return a;
}

}  // namespace

TEST_CASE("SupportsEntityTransform declares what each kind can represent", "[document][entitykindops]") {
  const auto planar = RotMinus90();
  const auto tilted = Rot3dY90();
  const auto move = doc::SimilarityTransform::Translation({1.0, 2.0, 3.0});
  for (EntityKind k : {EntityKind::Annotation, EntityKind::Table, EntityKind::Multileader}) {
    CHECK(doc::SupportsEntityTransform(k, planar));
    CHECK(doc::SupportsEntityTransform(k, MirrorY()));
    CHECK_FALSE(doc::SupportsEntityTransform(k, tilted));
  }
  CHECK(doc::SupportsEntityTransform(EntityKind::PositionMarker, move));
  CHECK_FALSE(doc::SupportsEntityTransform(EntityKind::PositionMarker, planar));
  for (EntityKind k : {EntityKind::FeatureLine, EntityKind::Surface, EntityKind::PointCloud})
    CHECK_FALSE(doc::SupportsEntityTransform(k, move));
  for (EntityKind k : {EntityKind::Ellipse, EntityKind::BlockRef, EntityKind::FilledRegion, EntityKind::Mesh,
                       EntityKind::Solid, EntityKind::PipeRun})
    CHECK(doc::SupportsEntityTransform(k, General3d()));
  CHECK_FALSE(doc::SupportsEntityTransform(EntityKind::Line, doc::SimilarityTransform::Invalid()));
  for (EntityKind k : doc::kDocumentEntityKinds)
    CHECK(doc::HasEntityKindOps(k));
}

TEST_CASE("Ellipse: AutoCAD 3D rotate, mirrored arc span, and exact bounds", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadEllipse el;
  el.cx = 16.0;
  el.cy = 12.0;
  el.majVx = 3.f;
  el.majVy = 0.f;
  el.ratio = 0.5f;
  store.userEllipses.push_back(el);
  store.userEllAttrs.push_back(EntityAttributes{7, "E"});

  SECTION("AutoCAD 2025: ROTATE3D Y 90 -> centre (0,12,-16), major (0,0,-3), normal (1,0,0)") {
    REQUIRE(doc::TransformEntity(store, {EntityKind::Ellipse, 0}, Rot3dY90()));
    const CadEllipse& e = store.userEllipses[0];
    CheckVecNear({e.cx, e.cy, e.z}, {0.0, 12.0, -16.0});
    CheckVecNear({e.nx, e.ny, e.nz}, {1.0, 0.0, 0.0});
    ray3d::Vec3 u{}, v{};
    doc::entityops_detail::EllipseWorldAxes(e, &u, &v);
    CheckVecNear(u, {0.0, 0.0, -3.0});
    CHECK(e.ratio == Catch::Approx(0.5f));
  }

  SECTION("Every transform maps the drawn points; a mirror reverses the span") {
    CadEllipse& e = store.userEllipses[0];
    e.startRad = 0.3f;
    e.sweepRad = 2.0f;
    e.nx = 0.3f;
    e.ny = -0.4f;
    e.nz = static_cast<float>(std::sqrt(1.0 - 0.25));
    const CadEllipse before = e;
    for (const doc::SimilarityTransform& xf : {General3d(), GeneralPlanar(), MirrorY(), Rot3dY90()}) {
      store.userEllipses[0] = before;
      REQUIRE(doc::TransformEntity(store, {EntityKind::Ellipse, 0}, xf));
      const CadEllipse& after = store.userEllipses[0];
      CHECK(after.sweepRad == Catch::Approx(before.sweepRad));
      for (double f : {0.0, 0.25, 0.5, 1.0}) {
        const double t = before.startRad + before.sweepRad * f;
        const double t2 = xf.isReflection() ? -t : t;
        CheckVecNear(EllipseWorldPointAt(after, t2), xf.applyPoint(EllipseWorldPointAt(before, t)), 2e-3);
      }
      // The drawn span is the image of the old one: its two ends swap under a mirror.
      const ray3d::Vec3 s0 = EllipseWorldPointAt(after, after.startRad);
      const ray3d::Vec3 e0 = xf.applyPoint(EllipseWorldPointAt(before, before.startRad + before.sweepRad));
      if (xf.isReflection())
        CheckVecNear(s0, e0, 2e-3);
    }
  }

  SECTION("Bounds are exact and enclose sampled points") {
    CadEllipse& e = store.userEllipses[0];
    e.startRad = 0.4f;
    e.sweepRad = 3.5f;
    REQUIRE(doc::TransformEntity(store, {EntityKind::Ellipse, 0}, General3d()));
    const doc::EntityBounds3d b = doc::EntityBounds(store, {EntityKind::Ellipse, 0}, {});
    REQUIRE(b.valid);
    doc::EntityBounds3d sampled;
    for (int i = 0; i <= 4000; ++i)
      sampled.expand(EllipseWorldPointAt(store.userEllipses[0], e.startRad + e.sweepRad * (i / 4000.0)));
    CheckVecNear(b.min, sampled.min, 1e-3);
    CheckVecNear(b.max, sampled.max, 1e-3);
  }

  SECTION("Duplicate keeps attributes but not the id") {
    const EntityRef copy = doc::DuplicateEntity(store, {EntityKind::Ellipse, 0});
    REQUIRE(copy == EntityRef{EntityKind::Ellipse, 1});
    CHECK(store.userEllAttrs[1].id == 0u);
    CHECK(store.userEllAttrs[1].layer == "E");
  }
}

TEST_CASE("TEXT follows AutoCAD under ROTATE, SCALE and MIRROR (MIRRTEXT 0)", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  store.cadAnnotations.push_back(MakeText(16.f, 12.f, 0.f));
  store.cadAnnotationAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::Annotation, 0};

  SECTION("AutoCAD 2025: ROTATE -90 -> insertion (12,-16), rotation 270 deg") {
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    const CadAnnotation& a = store.cadAnnotations[0];
    CheckVecNear({a.insX, a.insY, a.insZ}, {12.0, -16.0, 0.0});
    CheckAngleNear(a.rotationRad, 270.0 * kDeg);
  }

  SECTION("AutoCAD 2025: SCALE 2 at elevation 3 -> insertion (32,24,6), height x2") {
    store.cadAnnotations[0].insZ = 3.f;
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    const CadAnnotation& a = store.cadAnnotations[0];
    CheckVecNear({a.insX, a.insY, a.insZ}, {32.0, 24.0, 6.0});
    CHECK(a.plottedHeightInches == Catch::Approx(2.0));
  }

  SECTION("AutoCAD 2025: MIRROR of 30-degree text reads forwards from the mirrored far end") {
    // AutoCAD: (16,12) at 30 deg -> (-17.4846, 12.8571) at 330 deg, i.e. the far end of the
    // 1.7143-wide string mirrored. The width here is GoSurvey's estimate (0.55 * h per glyph).
    store.cadAnnotations[0].rotationRad = static_cast<float>(30.0 * kDeg);
    const double w = CadTextEstimatedWidth("AB", 1.f);
    doc::EntityTransformContext ctx;
    ctx.textUnitsPerPlottedInch = 1.0;
    REQUIRE(doc::TransformEntity(store, ref, MirrorY(), ctx));
    const CadAnnotation& a = store.cadAnnotations[0];
    CheckVecNear({a.insX, a.insY, 0.0}, {-(16.0 + w * std::cos(30.0 * kDeg)), 12.0 + w * std::sin(30.0 * kDeg), 0.0});
    CheckAngleNear(a.rotationRad, 330.0 * kDeg);
  }

  SECTION("A tilted axis is refused and leaves the text unchanged") {
    CHECK_FALSE(doc::TransformEntity(store, ref, Rot3dY90()));
    CHECK(store.cadAnnotations[0].insX == 16.f);
  }
}

TEST_CASE("MTEXT follows AutoCAD: mirrored frame stays readable", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.mtextAttach = 1;  // top-left
  a.insX = 16.f;
  a.insY = 15.f;
  a.boxMinX = 16.f;
  a.boxMaxX = 20.f;
  a.boxMinY = 12.f;
  a.boxMaxY = 15.f;
  a.plottedHeightInches = 0.2f;
  a.text = "NOTE";
  store.cadAnnotations.push_back(a);
  store.cadAnnotationAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::Annotation, 0};

  SECTION("AutoCAD 2025: MIRROR -> insertion (-16,15), attachment TL becomes TR, direction unchanged") {
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    const CadAnnotation& m = store.cadAnnotations[0];
    CHECK(m.mtextAttach == 3);
    CheckVecNear({m.insX, m.insY, 0.0}, {-16.0, 15.0, 0.0});
    CheckAngleNear(m.rotationRad, 0.0);
    CHECK(m.boxMinX == Catch::Approx(-20.f));
    CHECK(m.boxMaxX == Catch::Approx(-16.f));
    CHECK(m.boxMinY == Catch::Approx(12.f));
    CHECK(m.boxMaxY == Catch::Approx(15.f));
  }

  SECTION("AutoCAD 2025: ROTATE -90 -> insertion (15,-16), rotation 270 deg") {
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    const CadAnnotation& m = store.cadAnnotations[0];
    CheckVecNear({m.insX, m.insY, 0.0}, {15.0, -16.0, 0.0});
    CheckAngleNear(m.rotationRad, 270.0 * kDeg);
    CHECK(m.mtextAttach == 1);
  }

  SECTION("Mirroring twice restores the original frame") {
    REQUIRE(doc::TransformEntity(store, ref, GeneralPlanar()));
    REQUIRE(doc::TransformEntity(store, ref, GeneralPlanar().inverse()));
    const CadAnnotation& m = store.cadAnnotations[0];
    CHECK(m.mtextAttach == 1);
    CHECK(m.boxMinX == Catch::Approx(16.f).margin(1e-3));
    CHECK(m.boxMaxY == Catch::Approx(15.f).margin(1e-3));
    CheckAngleNear(m.rotationRad, 0.0, 1e-5);
  }
}

TEST_CASE("Dimensions follow AutoCAD under ROTATE, SCALE and MIRROR", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadAnnotation d;
  d.kind = CadAnnotation::Kind::DimLinear;
  d.dimExt1X = 16.0;
  d.dimExt1Y = 12.0;
  d.dimExt2X = 20.0;
  d.dimExt2Y = 12.0;
  d.dimSignedOffset = 2.0;
  d.insX = 18.f;
  d.insY = 14.f;
  d.plottedHeightInches = 0.1f;
  store.cadAnnotations.push_back(d);
  store.cadAnnotationAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::Annotation, 0};

  SECTION("AutoCAD 2025: DIMLINEAR ROTATE -90 measures vertically, ext (12,-16) and (12,-20)") {
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    const CadAnnotation& a = store.cadAnnotations[0];
    CHECK(a.dimLinearVertical);
    CheckVecNear({a.dimExt1X, a.dimExt1Y, 0.0}, {12.0, -16.0, 0.0});
    CheckVecNear({a.dimExt2X, a.dimExt2Y, 0.0}, {12.0, -20.0, 0.0});
    // AutoCAD's dimension line passes through (14,-20): x = chord x 12 + offset 2.
    CHECK(a.dimSignedOffset == Catch::Approx(2.0));
    CheckVecNear({a.insX, a.insY, 0.0}, {14.0, -18.0, 0.0});
    CheckAngleNear(a.rotationRad, kHalfPi);
  }

  SECTION("AutoCAD 2025: DIMLINEAR MIRROR moves the text with the dimension") {
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    const CadAnnotation& a = store.cadAnnotations[0];
    CHECK_FALSE(a.dimLinearVertical);
    CheckVecNear({a.dimExt1X, a.dimExt1Y, 0.0}, {-16.0, 12.0, 0.0});
    CheckVecNear({a.dimExt2X, a.dimExt2Y, 0.0}, {-20.0, 12.0, 0.0});
    CheckVecNear({a.insX, a.insY, 0.0}, {-18.0, 14.0, 0.0});
    CHECK(a.dimSignedOffset == Catch::Approx(2.0));
  }

  SECTION("AutoCAD 2025: DIMLINEAR SCALE 2 doubles every point and the offset") {
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    const CadAnnotation& a = store.cadAnnotations[0];
    CheckVecNear({a.dimExt1X, a.dimExt1Y, 0.0}, {32.0, 24.0, 0.0});
    CheckVecNear({a.dimExt2X, a.dimExt2Y, 0.0}, {40.0, 24.0, 0.0});
    CHECK(a.dimSignedOffset == Catch::Approx(4.0));
    CHECK(a.plottedHeightInches == Catch::Approx(0.2));
  }

  SECTION("DIMALIGNED: a mirror keeps the dimension line on the mirrored side") {
    CadAnnotation& a = store.cadAnnotations[0];
    a.kind = CadAnnotation::Kind::DimAligned;
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    // Chord (-16,12)->(-20,12): t = (-1,0), n = (0,-1); the line stays at y = 14, so offset = -2.
    CHECK(a.dimSignedOffset == Catch::Approx(-2.0));
    const double tx = a.dimExt2X - a.dimExt1X, ty = a.dimExt2Y - a.dimExt1Y;
    const double len = std::hypot(tx, ty);
    const double lineY = 0.5 * (a.dimExt1Y + a.dimExt2Y) + (tx / len) * a.dimSignedOffset;
    CHECK(lineY == Catch::Approx(14.0));
    CheckAngleNear(a.rotationRad, 0.0);
  }

  SECTION("DIMANGULAR: the vertex, rays and text all turn with ROTATE") {
    CadAnnotation& a = store.cadAnnotations[0];
    a.kind = CadAnnotation::Kind::DimAngular;
    a.dimAngVertexX = 10.0;
    a.dimAngVertexY = 10.0;
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    CheckVecNear({a.dimAngVertexX, a.dimAngVertexY, 0.0}, {10.0, -10.0, 0.0});
    CheckVecNear({a.dimExt1X, a.dimExt1Y, 0.0}, {12.0, -16.0, 0.0});
    CheckVecNear({a.insX, a.insY, 0.0}, {14.0, -18.0, 0.0});
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    CHECK(a.dimSignedOffset == Catch::Approx(4.0));
  }
}

TEST_CASE("TABLE follows AutoCAD: ROTATE turns it, MIRROR keeps it readable", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadTable t;
  t.insX = 16.f;
  t.insY = 12.f;
  t.width = 5.f;
  t.height = 2.f;
  store.cadTables.push_back(t);
  store.cadTableAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::Table, 0};

  SECTION("AutoCAD 2025: ROTATE -90 -> insertion (12,-16), direction (0,-1)") {
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    CheckVecNear({store.cadTables[0].insX, store.cadTables[0].insY, 0.0}, {12.0, -16.0, 0.0});
    CheckAngleNear(store.cadTables[0].rotationRad, -kHalfPi);
  }

  SECTION("AutoCAD 2025: MIRROR -> insertion (-21,12), direction (1,0), not mirror-imaged") {
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    const CadTable& m = store.cadTables[0];
    CheckVecNear({m.insX, m.insY, 0.0}, {-21.0, 12.0, 0.0});
    CheckAngleNear(m.rotationRad, 0.0);
    CHECK_FALSE(m.localYFlipped);
    const doc::EntityBounds3d b = doc::EntityBounds(store, ref, {});
    CheckVecNear(b.min, {-21.0, 10.0, 0.0});
    CheckVecNear(b.max, {-16.0, 12.0, 0.0});
  }

  SECTION("SCALE scales the size and the text") {
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    CHECK(store.cadTables[0].width == Catch::Approx(10.f));
    CHECK(store.cadTables[0].height == Catch::Approx(4.f));
    CHECK(store.cadTables[0].plottedHeightInches == Catch::Approx(0.25f));
  }
}

TEST_CASE("Block reference follows AutoCAD under MIRROR and 3D rotation", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadBlockRef br;
  br.defName = "BB";
  br.xf.x = 16.0;
  br.xf.y = 12.0;
  br.xf.rotZ = 30.0 * kDeg;
  store.cadBlockRefs.push_back(br);
  store.cadBlockRefAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::BlockRef, 0};

  SECTION("AutoCAD 2025: MIRROR of a 30-degree insert -> (-16,12), X scale -1, rotation 330 deg") {
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    const CadBlockXform& x = store.cadBlockRefs[0].xf;
    CheckVecNear({x.x, x.y, x.z}, {-16.0, 12.0, 0.0});
    CHECK(x.sx == Catch::Approx(-1.f));
    CHECK(x.sy == Catch::Approx(1.f));
    CheckAngleNear(x.rotZ, 330.0 * kDeg);
    CHECK(x.rotX == 0.f);
    CHECK(x.rotY == 0.f);
  }

  SECTION("Every transform maps the insert's local geometry exactly") {
    const CadBlockXform before = store.cadBlockRefs[0].xf;
    const ray3d::Vec3 locals[] = {{1.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {0.5, -1.0, 3.0}};
    for (const doc::SimilarityTransform& xf : {Rot3dY90(), General3d(), GeneralPlanar(), MirrorY(), Scale2()}) {
      store.cadBlockRefs[0].xf = before;
      REQUIRE(doc::TransformEntity(store, ref, xf));
      for (const ray3d::Vec3& l : locals) {
        ray3d::Vec3 w0{}, w1{};
        CadBlockXformPoint(before, l.x, l.y, l.z, &w0.x, &w0.y, &w0.z);
        CadBlockXformPoint(store.cadBlockRefs[0].xf, l.x, l.y, l.z, &w1.x, &w1.y, &w1.z);
        CheckVecNear(w1, xf.applyPoint(w0), 2e-3);
      }
    }
  }

  SECTION("Plan edits never tilt an upright insert") {
    REQUIRE(doc::TransformEntity(store, ref, GeneralPlanar()));
    CHECK(store.cadBlockRefs[0].xf.rotX == 0.f);
    CHECK(store.cadBlockRefs[0].xf.rotY == 0.f);
  }
}

TEST_CASE("Hatch fill follows AutoCAD: pattern turns with ROTATE, keeps its angle under MIRROR",
          "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadFilledRegion fr;
  fr.vertsXyz = {16.0, 12.0, 0.0, 20.0, 12.0, 0.0, 16.0, 15.0, 0.0};
  fr.loopStart = {0};
  fr.patternName = "ANSI31";
  fr.patternAngleDeg = 15.f;
  fr.patternScale = 2.f;
  store.cadFilledRegions.push_back(fr);
  store.cadFilledRegionAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::FilledRegion, 0};
  auto vert = [&](int i) {
    const auto& v = store.cadFilledRegions[0].vertsXyz;
    return ray3d::Vec3{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]};
  };

  SECTION("AutoCAD 2025: ROTATE -90 -> (12,-16), (12,-20), (15,-16); angle -75 deg; scale 2") {
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    CheckVecNear(vert(0), {12.0, -16.0, 0.0});
    CheckVecNear(vert(1), {12.0, -20.0, 0.0});
    CheckVecNear(vert(2), {15.0, -16.0, 0.0});
    CHECK(store.cadFilledRegions[0].patternAngleDeg == Catch::Approx(-75.f));
    CHECK(store.cadFilledRegions[0].patternScale == Catch::Approx(2.f));
  }

  SECTION("AutoCAD 2025: SCALE 2 -> doubled boundary, pattern scale 4") {
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    CheckVecNear(vert(1), {40.0, 24.0, 0.0});
    CHECK(store.cadFilledRegions[0].patternScale == Catch::Approx(4.f));
    CHECK(store.cadFilledRegions[0].patternAngleDeg == Catch::Approx(15.f));
  }

  SECTION("AutoCAD 2025: MIRROR -> boundary mirrored, pattern angle still 15 deg") {
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    CheckVecNear(vert(0), {-16.0, 12.0, 0.0});
    CheckVecNear(vert(1), {-20.0, 12.0, 0.0});
    CHECK(store.cadFilledRegions[0].patternAngleDeg == Catch::Approx(15.f));
  }

  SECTION("AutoCAD 2025: ROTATE3D Y 90 maps the boundary") {
    REQUIRE(doc::TransformEntity(store, ref, Rot3dY90()));
    CheckVecNear(vert(0), {0.0, 12.0, -16.0});
    CheckVecNear(vert(1), {0.0, 12.0, -20.0});
  }

  SECTION("Bounds and duplicate") {
    CheckVecNear(doc::EntityBounds(store, ref, {}).max, {20.0, 15.0, 0.0});
    REQUIRE(doc::DuplicateEntity(store, ref) == EntityRef{EntityKind::FilledRegion, 1});
    CHECK(store.cadFilledRegions[1].patternName == "ANSI31");
  }
}

TEST_CASE("Mesh, solid and pipe run take any similarity transform", "[document][entitykindops]") {
  CadDrawingEntityStore store;

  SECTION("Mesh: vertices map, normals turn, a mirror reverses winding, the source payload is untouched") {
    auto m = std::make_shared<CadMesh>();
    m->vertsXyz = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    m->normalsXyz = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    m->indices = {0, 1, 2};
    const std::shared_ptr<const CadMesh> original = m;
    store.cadMeshes.push_back(original);
    store.cadMeshAttrs.push_back(EntityAttributes{});
    const EntityRef ref{EntityKind::Mesh, 0};
    const EntityRef copy = doc::DuplicateEntity(store, ref);
    REQUIRE(copy.valid());
    REQUIRE(doc::TransformEntity(store, copy, MirrorY()));
    CHECK(original->vertsXyz[3] == 1.f);  // never written through
    const std::shared_ptr<const CadMesh> mirrored = store.cadMeshes[1];
    CHECK(mirrored->vertsXyz[3] == Catch::Approx(-1.f));
    CHECK(mirrored->indices == std::vector<std::uint32_t>{0, 2, 1});
    REQUIRE(doc::TransformEntity(store, copy, Rot3dY90()));
    CHECK(store.cadMeshes[1] != mirrored);  // replaced, not written through
    const CadMesh& m2 = *store.cadMeshes[1];
    CheckVecNear({m2.normalsXyz[0], m2.normalsXyz[1], m2.normalsXyz[2]}, {1.0, 0.0, 0.0});
    CheckVecNear(doc::EntityBounds(store, ref, {}).max, {1.0, 1.0, 0.0});
  }

  SECTION("Solid: every vertex is the transform of an original vertex") {
    brep::Solid box;
    brep::Problem why = brep::Problem::Ok;
    ucs::Ucs frame;
    frame.origin = {18.0, 13.0, 3.0};
    REQUIRE(brep::MakeBox(frame, 4.0, 2.0, 2.0, &box, &why));
    store.cadSolids.push_back(std::make_shared<const brep::Solid>(box));
    store.cadSolidAttrs.push_back(EntityAttributes{});
    const EntityRef ref{EntityKind::Solid, 0};
    for (const doc::SimilarityTransform& xf : {General3d(), GeneralPlanar(), MirrorY(), Rot3dY90(), Scale2()}) {
      store.cadSolids[0] = std::make_shared<const brep::Solid>(box);
      REQUIRE(doc::TransformEntity(store, ref, xf));
      const brep::Solid& s = *store.cadSolids[0];
      REQUIRE(s.vertices.size() == box.vertices.size());
      for (const brep::Vertex& v : box.vertices) {
        const ray3d::Vec3 want = xf.applyPoint(v.p);
        double best = 1e300;
        for (const brep::Vertex& w : s.vertices)
          best = std::min(best, ray3d::Length(ray3d::Sub(w.p, want)));
        CHECK(best < 1e-3 * std::max(1.0, ray3d::Length(want) * 1e-6));
      }
    }
    const doc::EntityBounds3d b = doc::EntityBounds(store, ref, {});
    CheckVecNear(b.min, {32.0, 24.0, 6.0});
    CheckVecNear(b.max, {40.0, 28.0, 10.0});
  }

  SECTION("Pipe run: the path maps and the size does not scale") {
    CadPipeRun run;
    run.vertsXyz = {10.0, 12.0, 3.0, 20.0, 12.0, 4.0};
    run.nominalSize = "4in";
    store.cadPipeRuns.push_back(run);
    store.cadPipeRunAttrs.push_back(EntityAttributes{});
    const EntityRef ref{EntityKind::PipeRun, 0};
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    const CadPipeRun& r = store.cadPipeRuns[0];
    CheckVecNear({r.vertsXyz[3], r.vertsXyz[4], r.vertsXyz[5]}, {40.0, 24.0, 8.0});
    CHECK(r.nominalSize == "4in");
    const doc::EntityBounds3d b = doc::EntityBounds(store, ref, {});
    CheckVecNear(b.min, {20.0 - 0.1875, 24.0 - 0.1875, 6.0 - 0.1875});
  }
}

TEST_CASE("Multileader follows AutoCAD; Position Marker only moves", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  CadMultileader ml;
  ml.pathXyz = {16.f, 12.f, 0.f, 19.f, 14.f, 0.f};
  ml.extraLeaderPaths = {{17.f, 10.f, 0.f, 19.f, 14.f, 0.f}};
  ml.arrowSizeWorld = 0.18f;
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.mtextAttach = 1;
  ml.label.insX = 19.36f;
  ml.label.insY = 14.f;
  ml.label.boxMinX = 19.36f;
  ml.label.boxMaxX = 20.36f;
  ml.label.boxMinY = 13.8f;
  ml.label.boxMaxY = 14.f;
  ml.label.plottedHeightInches = 0.18f;
  store.cadMultileaders.push_back(ml);
  store.cadMultileaderAttrs.push_back(EntityAttributes{});
  const EntityRef ref{EntityKind::Multileader, 0};

  SECTION("AutoCAD 2025: ROTATE -90 -> tip (12,-16), landing (14,-19), text direction (0,-1)") {
    REQUIRE(doc::TransformEntity(store, ref, RotMinus90()));
    const CadMultileader& m = store.cadMultileaders[0];
    CheckVecNear({m.pathXyz[0], m.pathXyz[1], m.pathXyz[2]}, {12.0, -16.0, 0.0});
    CheckVecNear({m.pathXyz[3], m.pathXyz[4], m.pathXyz[5]}, {14.0, -19.0, 0.0});
    CheckVecNear({m.extraLeaderPaths[0][0], m.extraLeaderPaths[0][1], 0.0}, {10.0, -17.0, 0.0});
    CheckAngleNear(m.label.rotationRad, -kHalfPi);
    CheckVecNear({m.label.insX, m.label.insY, 0.0}, {14.0, -19.36, 0.0});
  }

  SECTION("AutoCAD 2025: SCALE 2 -> tip (32,24), arrow and text doubled") {
    REQUIRE(doc::TransformEntity(store, ref, Scale2()));
    const CadMultileader& m = store.cadMultileaders[0];
    CheckVecNear({m.pathXyz[0], m.pathXyz[1], 0.0}, {32.0, 24.0, 0.0});
    CHECK(m.arrowSizeWorld == Catch::Approx(0.36f));
    CHECK(m.label.plottedHeightInches == Catch::Approx(0.36f));
  }

  SECTION("AutoCAD 2025: MIRROR -> landing (-19,14), label on the other side and readable") {
    REQUIRE(doc::TransformEntity(store, ref, MirrorY()));
    const CadMultileader& m = store.cadMultileaders[0];
    CheckVecNear({m.pathXyz[3], m.pathXyz[4], 0.0}, {-19.0, 14.0, 0.0});
    CheckAngleNear(m.label.rotationRad, 0.0);
    CHECK(m.label.mtextAttach == 3);
    CHECK(m.label.boxMaxX == Catch::Approx(-19.36f));
    CHECK(m.label.boxMinX == Catch::Approx(-20.36f));
  }

  SECTION("A tilted axis is refused") {
    CHECK_FALSE(doc::TransformEntity(store, ref, Rot3dY90()));
    CHECK(store.cadMultileaders[0].pathXyz[0] == 16.f);
  }

  SECTION("Position Marker: MOVE carries the label; ROTATE is refused (REQ-359)") {
    CadPositionMarker pm;
    pm.x = 16.0;
    pm.y = 12.0;
    pm.label.kind = CadAnnotation::Kind::Mtext;
    pm.label.insX = 17.f;
    pm.label.boxMinX = 17.f;
    store.cadPositionMarkers.push_back(pm);
    store.cadPositionMarkerAttrs.push_back(EntityAttributes{});
    const EntityRef pmRef{EntityKind::PositionMarker, 0};
    REQUIRE(doc::TransformEntity(store, pmRef, doc::SimilarityTransform::Translation({10.0, 20.0, 5.0})));
    const CadPositionMarker& p = store.cadPositionMarkers[0];
    CheckVecNear({p.x, p.y, p.z}, {26.0, 32.0, 5.0});
    CHECK(p.label.insX == Catch::Approx(27.f));
    CHECK(p.label.boxMinX == Catch::Approx(27.f));
    CHECK(p.label.insZ == Catch::Approx(5.f));
    CHECK_FALSE(doc::TransformEntity(store, pmRef, RotMinus90()));
    CHECK(p.x == Catch::Approx(26.0));
  }

  SECTION("Position Marker: a label at state-plane coordinates rounds once, not per displacement") {
    CadPositionMarker pm;
    pm.x = 1000000.0;
    pm.label.kind = CadAnnotation::Kind::Mtext;
    pm.label.insX = 1000000.f;
    pm.label.boxMinX = 1000000.f;
    store.cadPositionMarkers.push_back(pm);
    store.cadPositionMarkerAttrs.push_back(EntityAttributes{});
    const EntityRef pmRef{EntityKind::PositionMarker, static_cast<int>(store.cadPositionMarkers.size() - 1)};
    REQUIRE(doc::TransformEntity(store, pmRef, doc::SimilarityTransform::Translation({-999999.975, 0.0, 0.0})));
    const CadPositionMarker& p = store.cadPositionMarkers.back();
    CHECK(p.x == Catch::Approx(0.025).margin(1e-9));
    CHECK(p.label.insX == Catch::Approx(0.025f).margin(1e-6));
    CHECK(p.label.boxMinX == Catch::Approx(0.025f).margin(1e-6));
  }
}

TEST_CASE("COPY over every Document kind = DuplicateEntities + TransformEntities", "[document][entitykindops]") {
  CadDrawingEntityStore store;
  store.userEllipses.push_back(CadEllipse{});
  store.userEllAttrs.push_back(EntityAttributes{1, "0"});
  CadAnnotation label = MakeText(1.f, 2.f, 0.f);
  label.surveyPointLabelForId = 42;
  store.cadAnnotations.push_back(label);
  store.cadAnnotationAttrs.push_back(EntityAttributes{2, "0"});
  store.cadTables.push_back(CadTable{});
  store.cadTableAttrs.push_back(EntityAttributes{3, "0"});
  store.cadBlockRefs.push_back(CadBlockRef{});
  store.cadBlockRefAttrs.push_back(EntityAttributes{4, "0"});
  CadFilledRegion fr;
  fr.vertsXyz = {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
  fr.loopStart = {0};
  store.cadFilledRegions.push_back(fr);
  store.cadFilledRegionAttrs.push_back(EntityAttributes{5, "0"});
  CadMultileader ml;
  ml.pathXyz = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f};
  store.cadMultileaders.push_back(ml);
  store.cadMultileaderAttrs.push_back(EntityAttributes{6, "0"});
  store.cadPositionMarkers.push_back(CadPositionMarker{});
  store.cadPositionMarkerAttrs.push_back(EntityAttributes{7, "0"});
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  store.cadPipeRuns.push_back(run);
  store.cadPipeRunAttrs.push_back(EntityAttributes{8, "0"});

  const std::vector<EntityRef> refs = {
      {EntityKind::Ellipse, 0},     {EntityKind::Annotation, 0},  {EntityKind::Table, 0},
      {EntityKind::BlockRef, 0},    {EntityKind::FilledRegion, 0}, {EntityKind::Multileader, 0},
      {EntityKind::PositionMarker, 0}, {EntityKind::PipeRun, 0},  {EntityKind::FeatureLine, 0},
  };
  const std::vector<EntityRef> copies = doc::DuplicateEntities(store, refs);
  REQUIRE(copies.size() == 8);  // the feature line belongs to another system
  for (const EntityRef& c : copies) {
    CHECK(c.index == 1);
    const std::vector<EntityAttributes>* attrs = doc::EntityAttrsForKind(store, c.kind);
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->size() == 2);
    CHECK((*attrs)[1].id == 0u);
  }
  CHECK(store.cadAnnotations[1].surveyPointLabelForId == -1);
  CHECK(store.cadAnnotations[0].surveyPointLabelForId == 42);

  const auto move = doc::SimilarityTransform::Translation({5.0, 6.0, 7.0});
  CHECK(doc::TransformEntities(store, copies, move) == 8);
  CHECK(store.cadAnnotations[1].insX == Catch::Approx(6.f));
  CHECK(store.cadAnnotations[1].insZ == Catch::Approx(7.f));
  CHECK(store.cadTables[1].insZ == Catch::Approx(7.f));
  CHECK(store.cadBlockRefs[1].xf.z == Catch::Approx(7.0));
  CHECK(store.cadPositionMarkers[1].z == Catch::Approx(7.f));
  CHECK(store.cadMultileaders[1].pathXyz[2] == Catch::Approx(7.f));
  CHECK(store.cadPipeRuns[1].vertsXyz[2] == Catch::Approx(7.0));
  CHECK(store.userEllipses[1].z == Catch::Approx(7.0));
  CHECK(store.cadAnnotations[0].insX == 1.f);  // the originals stay put
}

TEST_CASE("EntityBounds boxes annotations, inserts, multileaders and Position Markers", "[document][entitykindops]") {
  for (EntityKind kind : doc::kDocumentEntityKinds)
    CHECK(doc::HasEntityBounds(kind));

  CadDrawingEntityStore store;
  CadAnnotation text = MakeText(16.f, 12.f, 0.f);
  text.insZ = 3.f;
  store.cadAnnotations.push_back(text);
  store.cadAnnotationAttrs.push_back(EntityAttributes{});

  CadBlockDefinition def;
  def.name = "UNITBLK";
  def.content.pushLine(CadLineRecord{0.0, 0.0, 0.0, 4.0, 2.0, 0.0});
  def.content.userLineAttrs.push_back(EntityAttributes{});
  store.blockDefs.push_back(def);
  CadBlockRef br;
  br.defName = "UNITBLK";
  br.xf.x = 16.0;
  br.xf.y = 12.0;
  br.xf.z = 3.0;
  store.cadBlockRefs.push_back(br);
  store.cadBlockRefAttrs.push_back(EntityAttributes{});

  CadMultileader ml;
  ml.pathXyz = {16.f, 12.f, 0.f, 19.f, 14.f, 0.f};
  ml.extraLeaderPaths = {{17.f, 10.f, 2.f, 19.f, 14.f, 0.f}};
  ml.label.insZ = 1.f;
  ml.label.boxMinX = 19.36f;
  ml.label.boxMaxX = 20.36f;
  ml.label.boxMinY = 13.8f;
  ml.label.boxMaxY = 14.f;
  store.cadMultileaders.push_back(ml);
  store.cadMultileaderAttrs.push_back(EntityAttributes{});

  CadPositionMarker pm;
  pm.x = 5.0;
  pm.y = 6.0;
  pm.z = 2.0;
  pm.label.boxMinX = 5.5f;
  pm.label.boxMaxX = 8.f;
  pm.label.boxMinY = 5.8f;
  pm.label.boxMaxY = 6.2f;
  store.cadPositionMarkers.push_back(pm);
  store.cadPositionMarkerAttrs.push_back(EntityAttributes{});

  doc::EntityBoundsContext ctx;
  ctx.modelUnitsPerPlottedInch = 2.f;
  ctx.positionMarkerRadius = 1.f;

  SECTION("an annotation takes the hit-testing box at its elevation, sized by the drawing scale") {
    const doc::EntityBounds3d b = doc::EntityBounds(store, {EntityKind::Annotation, 0}, ctx);
    REQUIRE(b.valid);
    float mnX = 0.f, mnY = 0.f, mxX = 0.f, mxY = 0.f;
    CadAnnotationRoughBounds(text, 2.f, &mnX, &mnY, &mxX, &mxY);
    CheckVecNear(b.min, {mnX, mnY, 3.0});
    CheckVecNear(b.max, {mxX, mxY, 3.0});
    doc::EntityBoundsContext bigger = ctx;
    bigger.modelUnitsPerPlottedInch = 4.f;
    const doc::EntityBounds3d b2 = doc::EntityBounds(store, {EntityKind::Annotation, 0}, bigger);
    CHECK(b2.max.y - b2.min.y == Catch::Approx(2.0 * (b.max.y - b.min.y)));
  }

  SECTION("an insert takes its block content at the insertion elevation") {
    const doc::EntityBounds3d b = doc::EntityBounds(store, {EntityKind::BlockRef, 0}, ctx);
    REQUIRE(b.valid);
    CheckVecNear(b.min, {16.0, 12.0, 3.0});
    CheckVecNear(b.max, {20.0, 14.0, 3.0});
  }

  SECTION("a multileader takes its paths, branches and label over all their elevations") {
    const doc::EntityBounds3d b = doc::EntityBounds(store, {EntityKind::Multileader, 0}, ctx);
    REQUIRE(b.valid);
    CheckVecNear(b.min, {16.0, 10.0, 0.0});
    CheckVecNear(b.max, {20.36, 14.0, 2.0});
  }

  SECTION("a Position Marker takes its circle, at the context's radius, and its label") {
    const doc::EntityBounds3d b = doc::EntityBounds(store, {EntityKind::PositionMarker, 0}, ctx);
    REQUIRE(b.valid);
    CheckVecNear(b.min, {4.0, 5.0, 2.0});
    CheckVecNear(b.max, {8.0, 7.0, 2.0});
  }

  SECTION("an index past the end has no box") {
    CHECK_FALSE(doc::EntityBounds(store, {EntityKind::Annotation, 1}, ctx).valid);
    CHECK_FALSE(doc::EntityBounds(store, {EntityKind::BlockRef, 1}, ctx).valid);
    CHECK_FALSE(doc::EntityBounds(store, {EntityKind::Multileader, 1}, ctx).valid);
    CHECK_FALSE(doc::EntityBounds(store, {EntityKind::PositionMarker, 1}, ctx).valid);
  }
}
