// Live section clip plane tests (REQ-336 / ADR-056, GitHub issue #149 acceptance 6).
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

TEST_CASE("An inactive clip keeps every point", "[sectionclip][req336][req149]") {
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
          "[sectionclip][req336][req149]") {
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

TEST_CASE("FLIP keeps the other half and moves nothing else", "[sectionclip][req336][req149]") {
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

TEST_CASE("The plane follows a UCS that has been moved and turned", "[sectionclip][req336][req149]") {
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
          "[sectionclip][req336][req149]") {
  const SectionClipPlane p = SectionClipFromUcs(ucs::Ucs{}, 12.0, false);
  float v4[4];
  SectionClipToShaderVec4(p, 0.0, 0.0, v4);
  for (const double z : {-5.0, 0.0, 11.0, 11.999, 12.001, 20.0}) {
    INFO("z = " << z);
    CHECK(ShaderKeeps(v4, 3.0, 4.0, z, 0.0, 0.0) == p.KeepsWorldPoint(3.0, 4.0, z));
  }
}

TEST_CASE("The clip lands in the right place at survey coordinate magnitudes",
          "[sectionclip][req336][req149][req101]") {
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
          "[sectionclip][req336][req149][req101]") {
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

TEST_CASE("Panning the view does not move the clip plane", "[sectionclip][req336][req149][req101]") {
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
          "[sectionclip][req336][req149][req101]") {
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
