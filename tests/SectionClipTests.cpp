// Live section clip plane tests (REQ-341 / ADR-058, GitHub issue #149 acceptance 6).
//
// The single most important test here is ANCHOR REBASING, and it is worth saying why before the
// code: `ViewportRenderer` does not upload world coordinates. Vertices arrive with XY relative to
// the view anchor and Z absolute, and the anchor IS the pan point. A clip plane handed to the
// shader in world coordinates is therefore in the wrong frame — and probe P7 measured exactly what
// that costs:
//
//   at the origin ..................................... exact
//   at easting 2,196,000 .............................. 2,196,000 ft out
//   view panned 250 ft ................................ the clip moves 250 ft
//   a HORIZONTAL cut (n = +Z) ......................... exact, in BOTH versions
//
// That last line is the reason these tests exist rather than a comment: the anchoring covers X and
// Y only, so a level cut cannot expose the bug and neither can any test written at the origin. The
// cases below are therefore deliberately at state-plane coordinates, on a tilted frame, and across
// a pan — the three things a plausible-looking wrong implementation still passes without.
//
// Every case checks the CPU predicate (`KeepsWorldPoint`) and the packed shader vec4 AGAINST EACH
// OTHER, evaluating the vec4 the way the vertex shader does. Two statements of one rule that must
// agree, rather than one statement asserted twice.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "render/SectionClip.hpp"
#include "util/ucs.hpp"

using Catch::Approx;

namespace {

/// Exactly what `kLineVs` computes: `dot(uClipPlane.xyz, aPos) + uClipPlane.w`, in float, on a
/// vertex uploaded the way the renderer uploads one (XY anchor-relative and narrowed, Z absolute).
float ShaderClipDistance(const float v4[4], double wx, double wy, double wz, double anchorX,
                         double anchorY) {
  const float ax = static_cast<float>(wx - anchorX);
  const float ay = static_cast<float>(wy - anchorY);
  const float az = static_cast<float>(wz);
  return v4[0] * ax + v4[1] * ay + v4[2] * az + v4[3];
}

bool ShaderKeeps(const float v4[4], double wx, double wy, double wz, double anchorX, double anchorY) {
  return ShaderClipDistance(v4, wx, wy, wz, anchorX, anchorY) >= 0.f;
}

/// The wrong version: the plane stated in world coordinates and handed straight to the shader,
/// which is what you write if you do not know the vertices are anchor-relative. Present so the
/// tests can assert it FAILS where the correct one passes, rather than only asserting success.
void NaiveWorldVec4(const SectionClipPlane& p, float* out4) {
  out4[0] = static_cast<float>(-p.nx);
  out4[1] = static_cast<float>(-p.ny);
  out4[2] = static_cast<float>(-p.nz);
  out4[3] = static_cast<float>(p.c);
}

/// Recover the world X at which a packed plane actually starts clipping, by bisection along the X
/// axis — the same instrument probe P7 used, and the reason it is a measurement rather than a
/// boolean: "these two answers differ" is satisfied by accident far too easily, where "the plane is
/// at 4,392,000 instead of 2,196,000" says what went wrong. Assumes the plane faces +X, which every
/// caller below arranges.
double FindPlaneWorldX(const float v4[4], double anchorX, double anchorY, double wy, double lo,
                       double hi) {
  if (!ShaderKeeps(v4, lo, wy, 0.0, anchorX, anchorY))
    return std::nan("");  // nothing survives even at the low end
  if (ShaderKeeps(v4, hi, wy, 0.0, anchorX, anchorY))
    return std::nan("");  // nothing is clipped even at the high end
  for (int i = 0; i < 200; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (ShaderKeeps(v4, mid, wy, 0.0, anchorX, anchorY))
      lo = mid;
    else
      hi = mid;
  }
  return 0.5 * (lo + hi);
}

constexpr double kSurveyE = 2196000.0;
constexpr double kSurveyN = 1400000.0;

}  // namespace

TEST_CASE("An inactive clip keeps every point", "[sectionclip][req341][req149]") {
  const SectionClipPlane off{};
  REQUIRE_FALSE(off.active);
  CHECK(off.KeepsWorldPoint(0.0, 0.0, 0.0));
  CHECK(off.KeepsWorldPoint(kSurveyE, kSurveyN, 5000.0));
  CHECK(off.KeepsWorldPoint(-1e9, 1e9, -1e9));

  // And the neutral vec4 keeps every vertex through the shader path too, which is what the renderer
  // relies on when it writes gl_ClipDistance unconditionally.
  float v4[4];
  SectionClipDisabledVec4(v4);
  CHECK(ShaderKeeps(v4, 0.0, 0.0, 0.0, 0.0, 0.0));
  CHECK(ShaderKeeps(v4, kSurveyE, kSurveyN, 5000.0, kSurveyE, kSurveyN));
  CHECK(ShaderKeeps(v4, -1e6, 1e6, -1e6, 0.0, 0.0));
}

TEST_CASE("The plane is the UCS plane, and the offset slides it along the UCS Z",
          "[sectionclip][req341][req149]") {
  const ucs::Ucs world{};

  const SectionClipPlane at0 = SectionClipFromUcs(world, 0.0, false);
  CHECK(at0.active);
  CHECK(at0.nx == Approx(0.0));
  CHECK(at0.ny == Approx(0.0));
  CHECK(at0.nz == Approx(1.0));
  CHECK(at0.c == Approx(0.0));
  // The half kept is the one +Z points AWAY from: material in front of the plane disappears.
  CHECK(at0.KeepsWorldPoint(0.0, 0.0, -1.0));
  CHECK_FALSE(at0.KeepsWorldPoint(0.0, 0.0, 1.0));

  const SectionClipPlane at12 = SectionClipFromUcs(world, 12.0, false);
  CHECK(at12.c == Approx(12.0));
  CHECK(at12.KeepsWorldPoint(0.0, 0.0, 11.9));
  CHECK_FALSE(at12.KeepsWorldPoint(0.0, 0.0, 12.1));

  // A negative offset is a plane below the UCS, not a refusal.
  const SectionClipPlane belowUcs = SectionClipFromUcs(world, -4.0, false);
  CHECK(belowUcs.c == Approx(-4.0));
  CHECK(belowUcs.KeepsWorldPoint(0.0, 0.0, -4.1));
  CHECK_FALSE(belowUcs.KeepsWorldPoint(0.0, 0.0, -3.9));
}

TEST_CASE("FLIP keeps the other half and moves nothing else", "[sectionclip][req341][req149]") {
  const ucs::Ucs world{};
  const SectionClipPlane a = SectionClipFromUcs(world, 12.0, false);
  const SectionClipPlane b = SectionClipFromUcs(world, 12.0, true);

  // Same plane: a point ON it is on it either way. What changes is which side survives.
  CHECK(a.nx == Approx(-b.nx));
  CHECK(a.ny == Approx(-b.ny));
  CHECK(a.nz == Approx(-b.nz));
  CHECK(a.c == Approx(-b.c));

  for (const double z : {0.0, 5.0, 11.9, 12.1, 30.0}) {
    // Exactly one of the two keeps any point off the plane -- that is what "the other half" means.
    const bool ka = a.KeepsWorldPoint(3.0, 4.0, z);
    const bool kb = b.KeepsWorldPoint(3.0, 4.0, z);
    INFO("z = " << z);
    if (std::fabs(z - 12.0) > 1e-9)
      CHECK(ka != kb);
  }
}

TEST_CASE("The plane follows a UCS that has been moved and turned", "[sectionclip][req341][req149]") {
  // A frame tilted about X and then moved: neither the normal nor the constant is an axis value any
  // more, which is the case an axis-aligned test cannot distinguish from a wrong one.
  const ucs::Ucs tilted =
      ucs::WithOrigin(ucs::RotatedAboutX(ucs::Ucs{}, 30.0), ray3d::Vec3{10.0, 20.0, 30.0});
  REQUIRE(ucs::IsRightHandedOrthonormal(tilted));

  const SectionClipPlane p = SectionClipFromUcs(tilted, 0.0, false);
  // The normal IS the frame's Z axis.
  CHECK(p.nx == Approx(tilted.zAxis.x));
  CHECK(p.ny == Approx(tilted.zAxis.y));
  CHECK(p.nz == Approx(tilted.zAxis.z));
  // The UCS origin lies ON the plane at offset 0, whatever the tilt.
  CHECK(p.nx * tilted.origin.x + p.ny * tilted.origin.y + p.nz * tilted.origin.z == Approx(p.c));

  // A point one unit along the frame's own +Z is cut; one unit along -Z survives. Stated in the
  // frame's axes, so it holds under any rotation.
  const double px = tilted.origin.x + tilted.zAxis.x;
  const double py = tilted.origin.y + tilted.zAxis.y;
  const double pz = tilted.origin.z + tilted.zAxis.z;
  CHECK_FALSE(p.KeepsWorldPoint(px, py, pz));
  CHECK(p.KeepsWorldPoint(tilted.origin.x - tilted.zAxis.x, tilted.origin.y - tilted.zAxis.y,
                          tilted.origin.z - tilted.zAxis.z));

  // The offset moves it along the frame's Z by exactly the offset, tilt included.
  const SectionClipPlane moved = SectionClipFromUcs(tilted, 1.0, false);
  CHECK(moved.c == Approx(p.c + 1.0));
  CHECK(moved.KeepsWorldPoint(px, py, pz));
}

TEST_CASE("The shader packing agrees with the CPU predicate at the origin",
          "[sectionclip][req341][req149]") {
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 12.0, false);
  float v4[4];
  SectionClipToShaderVec4(p, 0.0, 0.0, v4);
  for (const double z : {-5.0, 0.0, 11.0, 11.999, 12.001, 20.0}) {
    INFO("z = " << z);
    CHECK(ShaderKeeps(v4, 3.0, 4.0, z, 0.0, 0.0) == p.KeepsWorldPoint(3.0, 4.0, z));
  }
}

TEST_CASE("The clip lands in the right place at survey coordinate magnitudes",
          "[sectionclip][req341][req149][req101]") {
  // The UCS sits at state-plane coordinates and is tilted, so the plane normal has real X and Y
  // components and the anchor term is worth millions of feet. This is the case P7 measured.
  const ucs::Ucs frame = ucs::WithOrigin(ucs::RotatedAboutY(ucs::RotatedAboutX(ucs::Ucs{}, 20.0), 35.0),
                                         ray3d::Vec3{kSurveyE, kSurveyN, 800.0});
  const SectionClipPlane p = SectionClipFromUcs(frame, 0.0, false);

  // The renderer's anchor is the pan point; put it on the work, as it would be.
  const double anchorX = kSurveyE;
  const double anchorY = kSurveyN;
  float v4[4];
  SectionClipToShaderVec4(p, anchorX, anchorY, v4);

  // Points either side of the plane, measured ALONG the plane normal from the UCS origin so the
  // expected answer is known independently of the frame's numbers.
  struct Probe { double d; bool keep; };
  const Probe probes[] = {{-50.0, true}, {-0.5, true}, {-0.01, true},
                          {0.01, false}, {0.5, false}, {50.0, false}};
  for (const Probe& pr : probes) {
    const double x = frame.origin.x + p.nx * pr.d;
    const double y = frame.origin.y + p.ny * pr.d;
    const double z = frame.origin.z + p.nz * pr.d;
    INFO("distance along the normal = " << pr.d);
    CHECK(p.KeepsWorldPoint(x, y, z) == pr.keep);
    CHECK(ShaderKeeps(v4, x, y, z, anchorX, anchorY) == pr.keep);
  }
}

TEST_CASE("A world-stated plane is exact at the origin and wrong at survey magnitude",
          "[sectionclip][req341][req149][req101]") {
  // The finding P7 measured, as an assertion: the naive packing is INDISTINGUISHABLE from the
  // correct one at the origin, which is why a test written there proves nothing.
  {
    const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 3.0, false);
    float good[4], naive[4];
    SectionClipToShaderVec4(p, 0.0, 0.0, good);
    NaiveWorldVec4(p, naive);
    for (int i = 0; i < 4; ++i) {
      INFO("component " << i);
      CHECK(good[i] == naive[i]);  // identical at the origin -- the trap
    }
  }

  // Away from the origin they differ, and the difference is the anchor. A vertical cut is used
  // because a HORIZONTAL one (n = +Z) is exact in both versions -- the anchoring covers X and Y
  // only -- so a level plane could never catch this.
  const ucs::Ucs vertical =
      ucs::WithOrigin(ucs::RotatedAboutY(ucs::Ucs{}, 90.0), ray3d::Vec3{kSurveyE, kSurveyN, 0.0});
  const SectionClipPlane p = SectionClipFromUcs(vertical, 0.0, false);
  REQUIRE(std::fabs(p.nx) == Approx(1.0).margin(1e-9));  // the cut plane faces east

  float good[4], naive[4];
  SectionClipToShaderVec4(p, kSurveyE, kSurveyN, good);
  NaiveWorldVec4(p, naive);

  // A point 10 ft WEST of the plane must survive; 10 ft east must not.
  const double westX = kSurveyE - 10.0 * p.nx;
  const double eastX = kSurveyE + 10.0 * p.nx;
  CHECK(ShaderKeeps(good, westX, kSurveyN, 0.0, kSurveyE, kSurveyN));
  CHECK_FALSE(ShaderKeeps(good, eastX, kSurveyN, 0.0, kSurveyE, kSurveyN));

  // Where is each plane, really? The correct one is at the easting it was asked for. The naive one
  // is at `anchor + c` -- twice the easting -- so it sits over four million feet away and the clip
  // silently does NOTHING: both test points survive it.
  const double goodAt = FindPlaneWorldX(good, kSurveyE, kSurveyN, kSurveyN, kSurveyE - 1.0e6, kSurveyE + 1.0e6);
  const double naiveAt = FindPlaneWorldX(naive, kSurveyE, kSurveyN, kSurveyN, kSurveyE - 1.0e6, 6.0e6);
  INFO("correct plane at " << goodAt << ", naive plane at " << naiveAt);
  CHECK(goodAt == Approx(kSurveyE).margin(0.002));            // REQ-101
  CHECK(naiveAt == Approx(kSurveyE + kSurveyE).margin(1.0));  // anchor + c
  CHECK(std::fabs(naiveAt - kSurveyE) > 2.0e6);               // out by the easting

  // Which is a wrong answer in the direction that hides: nothing is cut, so a user would see the
  // command apparently do nothing rather than see something obviously broken.
  CHECK(ShaderKeeps(naive, westX, kSurveyN, 0.0, kSurveyE, kSurveyN));
  CHECK(ShaderKeeps(naive, eastX, kSurveyN, 0.0, kSurveyE, kSurveyN));
}

TEST_CASE("Panning the view does not move the clip plane", "[sectionclip][req341][req149][req101]") {
  // The anchor is the pan point, so this is the assertion that a correct implementation makes and
  // a naive one fails: same drawing, same plane, same test point, two different view anchors.
  const ucs::Ucs vertical =
      ucs::WithOrigin(ucs::RotatedAboutY(ucs::Ucs{}, 90.0), ray3d::Vec3{1000.0, 0.0, 0.0});
  const SectionClipPlane p = SectionClipFromUcs(vertical, 0.0, false);

  const double testX = 1000.0 - 10.0 * p.nx;  // 10 ft on the surviving side
  const double testY = 0.0;

  bool firstAnswer = false;
  bool first = true;
  for (const double anchorX : {0.0, 250.0, 1000.0, 1250.0, 50000.0}) {
    float v4[4];
    SectionClipToShaderVec4(p, anchorX, 0.0, v4);
    const bool keep = ShaderKeeps(v4, testX, testY, 0.0, anchorX, 0.0);
    INFO("view anchor X = " << anchorX);
    if (first) {
      firstAnswer = keep;
      first = false;
    }
    CHECK(keep == firstAnswer);
    CHECK(keep == p.KeepsWorldPoint(testX, testY, 0.0));
  }

  // And the naive version is proven to bite rather than assumed to, by MEASURING where its plane
  // ends up: it sits at `anchor + c`, so it slides one foot for every foot the view pans. That is
  // the defect stated as a number — the clip is nailed to the screen instead of to the drawing.
  float naive[4];
  NaiveWorldVec4(p, naive);
  for (const double anchorX : {0.0, 250.0, 1000.0, 1250.0}) {
    const double at = FindPlaneWorldX(naive, anchorX, 0.0, testY, -1.0e5, 1.0e5);
    INFO("view anchor X = " << anchorX);
    CHECK(at == Approx(anchorX + p.c).margin(0.01));

    // The correct one does not move, at the same anchors, measured the same way.
    float good[4];
    SectionClipToShaderVec4(p, anchorX, 0.0, good);
    const double goodAt = FindPlaneWorldX(good, anchorX, 0.0, testY, -1.0e5, 1.0e5);
    CHECK(goodAt == Approx(p.c).margin(0.002));  // REQ-101
  }
}

TEST_CASE("The clip offset keeps REQ-101 precision at survey magnitude",
          "[sectionclip][req341][req149][req101]") {
  // The plane constant at state-plane coordinates is ~2.2e6. Computing `c - anchor` in float would
  // quantize it to about 0.25 ft -- 125x REQ-101's +/-0.002 ft -- so the subtraction is done in
  // double and narrowed once. This case walks the plane in 0.002 ft steps and requires each one to
  // be resolved, which a float-computed constant cannot do.
  const ucs::Ucs vertical =
      ucs::WithOrigin(ucs::RotatedAboutY(ucs::Ucs{}, 90.0), ray3d::Vec3{kSurveyE, kSurveyN, 0.0});

  const double step = 0.002;  // REQ-101
  for (int i = 1; i <= 5; ++i) {
    const double offset = step * static_cast<double>(i);
    const SectionClipPlane p = SectionClipFromUcs(vertical, offset, false);
    float v4[4];
    SectionClipToShaderVec4(p, kSurveyE, kSurveyN, v4);

    // A point just inside the moved plane must survive, and one just outside must not. If the
    // constant had been rounded to a quarter of a foot, both land on the same side.
    const double insideD = offset - step * 0.25;
    const double outsideD = offset + step * 0.25;
    const double ix = kSurveyE + p.nx * insideD;
    const double ox = kSurveyE + p.nx * outsideD;
    INFO("offset = " << offset);
    CHECK(ShaderKeeps(v4, ix, kSurveyN, 0.0, kSurveyE, kSurveyN));
    CHECK_FALSE(ShaderKeeps(v4, ox, kSurveyN, 0.0, kSurveyE, kSurveyN));
  }
}

// --- The visible plane indicator (REQ-341) -----------------------------------------------------
//
// The rectangle drawn to show the user WHERE the clip cuts. It exists because without it the
// command has no visible effect at all in the default view: a level cut seen from directly above
// leaves the solid's outline in exactly the same place on screen. Two captures of that case came
// back byte-identical before this was added.

TEST_CASE("An inactive clip has no indicator to draw", "[sectionclip][req341][req149]") {
  const SectionClipPlane off{};
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(off, ray3d::Vec3{0, 0, 0}, ray3d::Vec3{10, 10, 10});
  CHECK_FALSE(ind.valid);
}

TEST_CASE("The indicator lies ON the plane it describes", "[sectionclip][req341][req149]") {
  // Every corner must satisfy the plane equation, or the rectangle is drawn somewhere the cut is
  // not — which is worse than drawing nothing, because it would be believed.
  const ucs::Ucs tilted =
      ucs::WithOrigin(ucs::RotatedAboutY(ucs::RotatedAboutX(ucs::Ucs{}, 25.0), 40.0),
                      ray3d::Vec3{3.0, -7.0, 11.0});
  for (const double offset : {-6.0, 0.0, 4.5}) {
    const SectionClipPlane p = SectionClipFromUcs(tilted, offset, false);
    const SectionClipIndicator ind =
        SectionClipIndicatorQuad(p, ray3d::Vec3{-20, -14, -2}, ray3d::Vec3{20, 14, 30});
    REQUIRE(ind.valid);
    for (int i = 0; i < 4; ++i) {
      INFO("offset " << offset << " corner " << i);
      CHECK(p.nx * ind.corner[i].x + p.ny * ind.corner[i].y + p.nz * ind.corner[i].z ==
            Approx(p.c).margin(1e-9));
    }
  }
}

TEST_CASE("The indicator is a rectangle, and it covers the model", "[sectionclip][req341][req149]") {
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 5.0, false);
  const ray3d::Vec3 mn{-20, -14, 0};
  const ray3d::Vec3 mx{20, 14, 12};
  const SectionClipIndicator ind = SectionClipIndicatorQuad(p, mn, mx, 0.15);
  REQUIRE(ind.valid);

  // Opposite sides equal and adjacent sides perpendicular — a rectangle, not a general quad.
  auto sub = [](const ray3d::Vec3& a, const ray3d::Vec3& b) { return ray3d::Sub(a, b); };
  const ray3d::Vec3 e0 = sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 e1 = sub(ind.corner[2], ind.corner[1]);
  const ray3d::Vec3 e2 = sub(ind.corner[3], ind.corner[2]);
  CHECK(ray3d::Length(e0) == Approx(ray3d::Length(e2)));
  CHECK(ray3d::Dot(e0, e1) == Approx(0.0).margin(1e-9));

  // And it COVERS the model, stated the only way that is basis-independent: every corner of the
  // model box projects inside the rectangle's own two edge directions.
  //
  // Not "edge 0 is longer than the model's X span" — the in-plane axes are built from the normal
  // and are NOT world X and Y. For a level cut they come out as (-Y, +X), so that assertion compares
  // the edge running along Y against the model's X extent and fails on a rectangle that is perfectly
  // correct. It did.
  const double len0 = ray3d::Length(e0);
  const double len1 = ray3d::Length(e1);
  const ray3d::Vec3 d0 = ray3d::Normalize(e0);
  const ray3d::Vec3 d1 = ray3d::Normalize(e1);
  for (int i = 0; i < 8; ++i) {
    const ray3d::Vec3 c{(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z};
    const ray3d::Vec3 rel = ray3d::Sub(c, ind.corner[0]);
    // The out-of-plane component does not disturb these: both edge directions lie IN the plane, so
    // a corner's height above it contributes nothing to either dot product.
    const double t0 = ray3d::Dot(rel, d0);
    const double t1 = ray3d::Dot(rel, d1);
    INFO("model corner " << i << " at (" << t0 << ", " << t1 << ") in a " << len0 << " x " << len1
                         << " rectangle");
    CHECK(t0 > 0.0);
    CHECK(t0 < len0);
    CHECK(t1 > 0.0);
    CHECK(t1 < len1);
  }
}

TEST_CASE("A level cut still gets an indicator, which is the case that needed one",
          "[sectionclip][req341][req149]") {
  // n = +Z is the default cut and the one that shows nothing on screen without this. It is also
  // the orientation that breaks a naive in-plane basis built from a fixed helper axis.
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 6.0, false);
  REQUIRE(p.nz == Approx(1.0));
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-20, -14, 0}, ray3d::Vec3{20, 14, 12});
  REQUIRE(ind.valid);
  for (int i = 0; i < 4; ++i) {
    INFO("corner " << i);
    CHECK(ind.corner[i].z == Approx(6.0));  // flat, at the cut height
  }
  // Non-degenerate: a zero-area rectangle would draw as nothing and read as "no indicator".
  CHECK(ray3d::Length(ray3d::Sub(ind.corner[1], ind.corner[0])) > 1.0);
  CHECK(ray3d::Length(ray3d::Sub(ind.corner[2], ind.corner[1])) > 1.0);
}

TEST_CASE("A flat drawing still gets a drawable indicator", "[sectionclip][req341][req149]") {
  // Every solid at one elevation gives a box with zero Z span. A vertical cut through it has zero
  // extent in one in-plane direction, and a rectangle of zero width would look like nothing was
  // added at all.
  const ucs::Ucs vertical = ucs::RotatedAboutY(ucs::Ucs{}, 90.0);
  const SectionClipPlane p = SectionClipFromUcs(vertical, 0.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-30, -20, 4}, ray3d::Vec3{30, 20, 4});
  REQUIRE(ind.valid);
  CHECK(ray3d::Length(ray3d::Sub(ind.corner[1], ind.corner[0])) > 1e-3);
  CHECK(ray3d::Length(ray3d::Sub(ind.corner[2], ind.corner[1])) > 1e-3);
}

TEST_CASE("The indicator holds at survey coordinate magnitudes",
          "[sectionclip][req341][req149][req101]") {
  const ucs::Ucs frame = ucs::WithOrigin(ucs::RotatedAboutX(ucs::Ucs{}, 15.0),
                                         ray3d::Vec3{kSurveyE, kSurveyN, 850.0});
  const SectionClipPlane p = SectionClipFromUcs(frame, 3.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{kSurveyE - 40, kSurveyN - 25, 840},
                               ray3d::Vec3{kSurveyE + 40, kSurveyN + 25, 890});
  REQUIRE(ind.valid);
  for (int i = 0; i < 4; ++i) {
    INFO("corner " << i);
    // On the plane to REQ-101, at a magnitude where a careless formulation loses its low bits.
    CHECK(p.nx * ind.corner[i].x + p.ny * ind.corner[i].y + p.nz * ind.corner[i].z ==
          Approx(p.c).margin(0.002));
  }
}

// -------------------------------------------------------------------------------------------
// REQ-342 / ADR-059 (GitHub issue #479 acceptance 3) — how the plane is DRAWN.
//
// The hatch is what makes the plane findable, and it is geometry rebuilt every frame from the
// rectangle, so the two failures worth pinning are "it is not on the plane" and "it does not stay
// inside the rectangle". A hatch line that escapes its quad draws a stray line across the model
// with nothing to explain it; one off the plane draws a shape that is believed and is wrong.
// -------------------------------------------------------------------------------------------

TEST_CASE("An invalid rectangle produces no plane graphics", "[sectionplane][req342][req479]") {
  const SectionPlaneGraphics g = SectionPlaneGraphicsFor(SectionClipIndicator{});
  CHECK_FALSE(g.valid);
  CHECK(g.hatch.empty());
}

TEST_CASE("Every hatch endpoint lies ON the plane", "[sectionplane][req342][req479]") {
  // Same requirement the indicator corners have, and for the same reason: this is drawn unclipped
  // and will be believed. A tilted frame, because an axis-aligned one is satisfied by arithmetic
  // that has dropped a basis vector entirely.
  const ucs::Ucs tilted =
      ucs::WithOrigin(ucs::RotatedAboutY(ucs::RotatedAboutX(ucs::Ucs{}, 25.0), 40.0),
                      ray3d::Vec3{3.0, -7.0, 11.0});
  for (const double offset : {-6.0, 0.0, 4.5}) {
    const SectionClipPlane p = SectionClipFromUcs(tilted, offset, false);
    const SectionClipIndicator ind =
        SectionClipIndicatorQuad(p, ray3d::Vec3{-20, -14, -2}, ray3d::Vec3{20, 14, 30});
    REQUIRE(ind.valid);
    const SectionPlaneGraphics g = SectionPlaneGraphicsFor(ind);
    REQUIRE(g.valid);
    REQUIRE_FALSE(g.hatch.empty());
    for (size_t i = 0; i < g.hatch.size(); ++i) {
      INFO("offset " << offset << " hatch point " << i);
      CHECK(p.nx * g.hatch[i].x + p.ny * g.hatch[i].y + p.nz * g.hatch[i].z ==
            Approx(p.c).margin(1e-9));
    }
    // The section line is an edge of the rectangle, so it is on the plane too.
    CHECK(p.nx * g.lineA.x + p.ny * g.lineA.y + p.nz * g.lineA.z == Approx(p.c).margin(1e-9));
    CHECK(p.nx * g.lineB.x + p.ny * g.lineB.y + p.nz * g.lineB.z == Approx(p.c).margin(1e-9));
  }
}

TEST_CASE("Every hatch endpoint stays inside the rectangle", "[sectionplane][req342][req479]") {
  // Measured in the rectangle's OWN axes rather than in world XY, so this holds for a tilted plane
  // — and a tilted plane is where a clipping mistake would otherwise hide.
  const ucs::Ucs tilted = ucs::RotatedAboutX(ucs::Ucs{}, 35.0);
  const SectionClipPlane p = SectionClipFromUcs(tilted, 2.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-30, -9, 0}, ray3d::Vec3{30, 9, 14});
  REQUIRE(ind.valid);
  const SectionPlaneGraphics g = SectionPlaneGraphicsFor(ind);
  REQUIRE(g.valid);

  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const double lu = ray3d::Length(eu);
  const double lv = ray3d::Length(ev);
  REQUIRE(lu > 1e-9);
  REQUIRE(lv > 1e-9);
  const ray3d::Vec3 u = ray3d::Scale(eu, 1.0 / lu);
  const ray3d::Vec3 v = ray3d::Scale(ev, 1.0 / lv);
  for (size_t i = 0; i < g.hatch.size(); ++i) {
    const ray3d::Vec3 d = ray3d::Sub(g.hatch[i], ind.corner[0]);
    const double s = ray3d::Dot(d, u);
    const double t = ray3d::Dot(d, v);
    INFO("hatch point " << i << " at (s,t) = (" << s << ", " << t << ") in a "
                        << lu << " x " << lv << " rectangle");
    CHECK(s >= -1e-9);
    CHECK(s <= lu + 1e-9);
    CHECK(t >= -1e-9);
    CHECK(t <= lv + 1e-9);
  }
}

TEST_CASE("The hatch comes in drawable pairs and is bounded", "[sectionplane][req342][req479]") {
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 0.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-40, -30, 0}, ray3d::Vec3{40, 30, 12});
  REQUIRE(ind.valid);
  const SectionPlaneGraphics g = SectionPlaneGraphicsFor(ind);
  REQUIRE(g.valid);

  // GL_LINES order: an odd count would draw one endpoint into whatever followed it in the buffer.
  CHECK(g.hatch.size() % 2 == 0);
  CHECK(static_cast<int>(g.hatch.size() / 2) <= kSectionPlaneHatchMaxSegments);
  // Enough lines to read as a hatch rather than as a couple of stray diagonals.
  CHECK(g.hatch.size() / 2 >= 8);
  // No zero-length segments: they cost an upload and draw nothing.
  for (size_t i = 0; i + 1 < g.hatch.size(); i += 2)
    CHECK(ray3d::Length(ray3d::Sub(g.hatch[i + 1], g.hatch[i])) > 1e-6);
}

TEST_CASE("The hatch is scale-invariant, not distance-dependent", "[sectionplane][req342][req479]") {
  // Density comes from the rectangle's own diagonal, so a 4 ft fitting and a 900 ft parcel get the
  // same NUMBER of lines. Were it a world spacing instead, one of those two would be either a solid
  // block of ink or a single line, and the segment count at survey scale could run away.
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 0.0, false);
  const SectionPlaneGraphics small = SectionPlaneGraphicsFor(
      SectionClipIndicatorQuad(p, ray3d::Vec3{-2, -2, 0}, ray3d::Vec3{2, 2, 1}));
  const SectionPlaneGraphics large = SectionPlaneGraphicsFor(
      SectionClipIndicatorQuad(p, ray3d::Vec3{-450, -450, 0}, ray3d::Vec3{450, 450, 40}));
  REQUIRE(small.valid);
  REQUIRE(large.valid);
  CHECK(small.hatch.size() == large.hatch.size());
}

TEST_CASE("The hatch holds at survey coordinate magnitudes", "[sectionplane][req342][req479][req101]") {
  // The whole feature's hazard is that everything is exact at the origin. The pattern is built from
  // the rectangle's corners, which at E 2,196,000 are large numbers whose DIFFERENCES are small —
  // the same shape as the anchor-rebasing bug, so it is checked rather than assumed.
  const ucs::Ucs frame = ucs::WithOrigin(ucs::Ucs{}, ray3d::Vec3{2196000.0, 1400000.0, 0.0});
  const SectionClipPlane p = SectionClipFromUcs(frame, 6.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{2195980.0, 1399985.0, 0.0},
                               ray3d::Vec3{2196020.0, 1400015.0, 12.0});
  REQUIRE(ind.valid);
  const SectionPlaneGraphics g = SectionPlaneGraphicsFor(ind);
  REQUIRE(g.valid);
  REQUIRE_FALSE(g.hatch.empty());
  for (size_t i = 0; i < g.hatch.size(); ++i) {
    INFO("hatch point " << i);
    // REQ-101's own tolerance, not a looser one: the point of the case is that nothing is lost.
    CHECK(p.nx * g.hatch[i].x + p.ny * g.hatch[i].y + p.nz * g.hatch[i].z ==
          Approx(p.c).margin(0.002));
  }
  // And the count matches the same rectangle drawn at the origin — the pattern is a function of
  // the shape, not of where it sits.
  const SectionClipPlane atOrigin = SectionClipFromUcs(ucs::Ucs{}, 6.0, false);
  const SectionPlaneGraphics og = SectionPlaneGraphicsFor(
      SectionClipIndicatorQuad(atOrigin, ray3d::Vec3{-20, -15, 0}, ray3d::Vec3{20, 15, 12}));
  REQUIRE(og.valid);
  CHECK(og.hatch.size() == g.hatch.size());
}

TEST_CASE("The section line runs through the middle of the plane", "[sectionplane][req343][req479]") {
  // REQ-343 moved it here from the lowest EDGE, which REQ-342 had used. An edge coincides with the
  // rectangle's own outline, so it added no information and left the middle — where the handles
  // have to be — unmarked. This is the bright line across the centre of AutoCAD's section plane.
  const ucs::Ucs upright = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);  // normal now horizontal
  const SectionClipPlane p = SectionClipFromUcs(upright, 0.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-20, -15, 0}, ray3d::Vec3{20, 15, 25});
  REQUIRE(ind.valid);
  const SectionPlaneGraphics g = SectionPlaneGraphicsFor(ind);
  REQUIRE(g.valid);

  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const ray3d::Vec3 centre{ind.corner[0].x + 0.5 * (eu.x + ev.x),
                           ind.corner[0].y + 0.5 * (eu.y + ev.y),
                           ind.corner[0].z + 0.5 * (eu.z + ev.z)};

  // Its midpoint IS the rectangle's centre.
  const ray3d::Vec3 mid{0.5 * (g.lineA.x + g.lineB.x), 0.5 * (g.lineA.y + g.lineB.y),
                        0.5 * (g.lineA.z + g.lineB.z)};
  CHECK(ray3d::Length(ray3d::Sub(mid, centre)) == Approx(0.0).margin(1e-9));

  // It spans the full width, along u — not a diagonal, and not a fraction of the way across.
  const ray3d::Vec3 line = ray3d::Sub(g.lineB, g.lineA);
  CHECK(ray3d::Length(line) == Approx(ray3d::Length(eu)).margin(1e-9));
  const ray3d::Vec3 cross = ray3d::Cross(ray3d::Normalize(line), ray3d::Normalize(eu));
  CHECK(ray3d::Length(cross) == Approx(0.0).margin(1e-9));  // parallel to the u edge

  // And both ends are on the plane and on the rectangle's boundary, not floating inside it.
  CHECK(p.nx * g.lineA.x + p.ny * g.lineA.y + p.nz * g.lineA.z == Approx(p.c).margin(1e-9));
  CHECK(p.nx * g.lineB.x + p.ny * g.lineB.y + p.nz * g.lineB.z == Approx(p.c).margin(1e-9));
}

// -------------------------------------------------------------------------------------------
// REQ-343 (GitHub issue #479 acceptance 5-7) — the handles, and the stored size they write.
//
// Everything here is geometry with no window and no command state, which is the whole reason
// `SectionClip.hpp` is header-only and GL-free (ADR-002). The command-layer half — what a click
// and a drag DO — is in `SubObjectSelectionTests` under `[sectionplanegrip]`.
// -------------------------------------------------------------------------------------------

TEST_CASE("An invalid rectangle has no handles", "[sectionplane][req343][req479]") {
  const SectionPlaneGrips g = SectionPlaneGripsFor(SectionClipIndicator{}, SectionClipPlane{});
  CHECK_FALSE(g.valid);
}

TEST_CASE("Every handle lies on the plane, and on the rectangle", "[sectionplane][req343][req479]") {
  // A handle off the plane is a handle that cannot be aimed at: the pick tests a ray against the
  // handle POINT, so if it is drawn somewhere the arithmetic does not put it, clicking it misses.
  const ucs::Ucs tilted =
      ucs::WithOrigin(ucs::RotatedAboutY(ucs::RotatedAboutX(ucs::Ucs{}, 25.0), 40.0),
                      ray3d::Vec3{3.0, -7.0, 11.0});
  const SectionClipPlane p = SectionClipFromUcs(tilted, 3.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-20, -14, -2}, ray3d::Vec3{20, 14, 30});
  REQUIRE(ind.valid);
  const SectionPlaneGrips g = SectionPlaneGripsFor(ind, p);
  REQUIRE(g.valid);

  ray3d::Vec3 u{}, v{};
  REQUIRE(SectionClipPlaneBasis(p, &u, &v));
  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const double lu = ray3d::Length(eu);
  const double lv = ray3d::Length(ev);

  for (int i = 0; i < kSectionPlaneGripCount; ++i) {
    INFO("handle " << i);
    CHECK(p.nx * g.at[i].x + p.ny * g.at[i].y + p.nz * g.at[i].z == Approx(p.c).margin(1e-9));
    const ray3d::Vec3 d = ray3d::Sub(g.at[i], ind.corner[0]);
    const double s = ray3d::Dot(d, u);
    const double t = ray3d::Dot(d, v);
    CHECK(s >= -1e-9);
    CHECK(s <= lu + 1e-9);
    CHECK(t >= -1e-9);
    CHECK(t <= lv + 1e-9);
  }
}

TEST_CASE("The handles sit where their job says they should", "[sectionplane][req343][req479]") {
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 0.0, false);
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-40, -30, 0}, ray3d::Vec3{40, 30, 12});
  REQUIRE(ind.valid);
  const SectionPlaneGrips g = SectionPlaneGripsFor(ind, p);
  REQUIRE(g.valid);
  const SectionPlaneGraphics gfx = SectionPlaneGraphicsFor(ind);
  REQUIRE(gfx.valid);

  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const ray3d::Vec3 centre{ind.corner[0].x + 0.5 * (eu.x + ev.x),
                           ind.corner[0].y + 0.5 * (eu.y + ev.y),
                           ind.corner[0].z + 0.5 * (eu.z + ev.z)};

  const auto at = [&](SectionPlaneGrip k) { return g.at[static_cast<int>(k)]; };
  const auto dir = [&](SectionPlaneGrip k) { return g.dir[static_cast<int>(k)]; };

  // Move is at the centre and drags along the NORMAL — the gesture the whole feature exists for.
  CHECK(ray3d::Length(ray3d::Sub(at(SectionPlaneGrip::Move), centre)) == Approx(0.0).margin(1e-9));
  CHECK(dir(SectionPlaneGrip::Move).z == Approx(1.0));

  // The length pair sits at the section line's two ENDS, so the line is what they visibly resize.
  CHECK(ray3d::Length(ray3d::Sub(at(SectionPlaneGrip::LengthNeg), gfx.lineA)) ==
        Approx(0.0).margin(1e-9));
  CHECK(ray3d::Length(ray3d::Sub(at(SectionPlaneGrip::LengthPos), gfx.lineB)) ==
        Approx(0.0).margin(1e-9));

  // The height pair is on the two u-parallel edges, half a rectangle apart.
  CHECK(ray3d::Length(ray3d::Sub(at(SectionPlaneGrip::HeightPos), at(SectionPlaneGrip::HeightNeg))) ==
        Approx(ray3d::Length(ev)).margin(1e-9));

  // Flip is NOT at the centre — it must not be grabbable by accident when the user means to slide
  // the plane — and it is a click, so it has no drag direction at all.
  CHECK(ray3d::Length(ray3d::Sub(at(SectionPlaneGrip::Flip), centre)) > 1e-6);
  CHECK(ray3d::Length(dir(SectionPlaneGrip::Flip)) == Approx(0.0).margin(1e-12));

  // Every draggable handle's direction is a unit vector, or the drag parameter would be scaled.
  for (const SectionPlaneGrip k : {SectionPlaneGrip::Move, SectionPlaneGrip::LengthNeg,
                                   SectionPlaneGrip::LengthPos, SectionPlaneGrip::HeightNeg,
                                   SectionPlaneGrip::HeightPos}) {
    INFO("handle " << static_cast<int>(k));
    CHECK(ray3d::Length(dir(k)) == Approx(1.0).margin(1e-9));
  }
  // The two of each pair point OPPOSITE ways, which is what makes "positive delta grows it" true
  // for both without the caller having to know which end it grabbed.
  CHECK(ray3d::Dot(dir(SectionPlaneGrip::LengthNeg), dir(SectionPlaneGrip::LengthPos)) ==
        Approx(-1.0).margin(1e-9));
  CHECK(ray3d::Dot(dir(SectionPlaneGrip::HeightNeg), dir(SectionPlaneGrip::HeightPos)) ==
        Approx(-1.0).margin(1e-9));
}

TEST_CASE("Sliding the plane moves the handles with it", "[sectionplane][req343][req479]") {
  // The handles are derived from the rectangle, which is derived from the plane, so this holds by
  // construction — and it is asserted because the alternative (handles cached when the plane was
  // selected) is the obvious implementation and would leave them behind on the first drag frame.
  const SectionClipIndicator a = SectionClipIndicatorQuad(SectionClipFromUcs(ucs::Ucs{}, 0.0, false),
                                                          ray3d::Vec3{-20, -15, 0},
                                                          ray3d::Vec3{20, 15, 12});
  const SectionClipPlane pb = SectionClipFromUcs(ucs::Ucs{}, 7.0, false);
  const SectionClipIndicator b =
      SectionClipIndicatorQuad(pb, ray3d::Vec3{-20, -15, 0}, ray3d::Vec3{20, 15, 12});
  const SectionPlaneGrips ga =
      SectionPlaneGripsFor(a, SectionClipFromUcs(ucs::Ucs{}, 0.0, false));
  const SectionPlaneGrips gb = SectionPlaneGripsFor(b, pb);
  REQUIRE(ga.valid);
  REQUIRE(gb.valid);
  for (int i = 0; i < kSectionPlaneGripCount; ++i) {
    INFO("handle " << i);
    CHECK(gb.at[i].z - ga.at[i].z == Approx(7.0).margin(1e-9));
    CHECK(gb.at[i].x - ga.at[i].x == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("A stored extent overrides the model-derived size", "[sectionplane][req343][req479]") {
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 0.0, false);
  const SectionClipIndicator derived =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-40, -30, 0}, ray3d::Vec3{40, 30, 12});
  REQUIRE(derived.valid);

  SectionPlaneExtent e = SectionPlaneExtentFromQuad(derived, p);
  REQUIRE(e.valid);

  // Round trip: the extent read off the derived rectangle rebuilds that same rectangle. This is
  // what lets the first stretch keep the size the user is looking at instead of snapping to a
  // default and then resizing, which reads as the plane jumping.
  const SectionClipIndicator same =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-40, -30, 0}, ray3d::Vec3{40, 30, 12}, 0.15, e);
  REQUIRE(same.valid);
  for (int i = 0; i < 4; ++i) {
    INFO("corner " << i);
    CHECK(ray3d::Length(ray3d::Sub(same.corner[i], derived.corner[i])) == Approx(0.0).margin(1e-9));
  }

  // Now stretch it, and the MODEL bounds stop mattering entirely — including bounds that would
  // have produced a completely different rectangle.
  e.halfU = 5.0;
  e.halfV = 2.0;
  const SectionClipIndicator stretched =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-900, -900, 0}, ray3d::Vec3{900, 900, 40}, 0.15, e);
  REQUIRE(stretched.valid);
  CHECK(ray3d::Length(ray3d::Sub(stretched.corner[1], stretched.corner[0])) == Approx(10.0));
  CHECK(ray3d::Length(ray3d::Sub(stretched.corner[3], stretched.corner[0])) == Approx(4.0));
}

TEST_CASE("The stored extent survives the plane sliding", "[sectionplane][req343][req479]") {
  // The extent is stated in the plane's own basis, and the basis depends only on the NORMAL — so an
  // offset change cannot touch it. Stored as four world corners it would have had to be rewritten
  // on every frame of a slide, and any path that forgot would leave the rectangle behind the cut.
  SectionPlaneExtent e;
  e.valid = true;
  e.cu = 3.0;
  e.cv = -2.0;
  e.halfU = 9.0;
  e.halfV = 4.0;
  const ray3d::Vec3 mn{-40, -30, 0}, mx{40, 30, 12};
  const SectionClipIndicator a =
      SectionClipIndicatorQuad(SectionClipFromUcs(ucs::Ucs{}, 0.0, false), mn, mx, 0.15, e);
  const SectionClipIndicator b =
      SectionClipIndicatorQuad(SectionClipFromUcs(ucs::Ucs{}, 6.0, false), mn, mx, 0.15, e);
  REQUIRE(a.valid);
  REQUIRE(b.valid);
  for (int i = 0; i < 4; ++i) {
    INFO("corner " << i);
    CHECK(b.corner[i].z - a.corner[i].z == Approx(6.0).margin(1e-9));
    CHECK(b.corner[i].x == Approx(a.corner[i].x).margin(1e-9));
    CHECK(b.corner[i].y == Approx(a.corner[i].y).margin(1e-9));
  }
}

TEST_CASE("A stretched plane still hatches and still has a centre line",
          "[sectionplane][req343][req479]") {
  // The appearance is derived from the rectangle, so a user-sized rectangle must produce a proper
  // one — including a hatch count that does not collapse on a long thin plane.
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 0.0, false);
  SectionPlaneExtent e;
  e.valid = true;
  e.halfU = 60.0;
  e.halfV = 1.5;  // deliberately long and thin
  const SectionClipIndicator ind =
      SectionClipIndicatorQuad(p, ray3d::Vec3{-10, -10, 0}, ray3d::Vec3{10, 10, 5}, 0.15, e);
  REQUIRE(ind.valid);
  const SectionPlaneGraphics g = SectionPlaneGraphicsFor(ind);
  REQUIRE(g.valid);
  CHECK(g.hatch.size() % 2 == 0);
  CHECK(g.hatch.size() >= 2);
  CHECK(static_cast<int>(g.hatch.size() / 2) <= kSectionPlaneHatchMaxSegments);
  CHECK(ray3d::Length(ray3d::Sub(g.lineB, g.lineA)) == Approx(120.0).margin(1e-9));
}
