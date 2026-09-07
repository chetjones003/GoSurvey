// REQ-154 / issue #371 — ORTHO must square to the active UCS's axes even when the work plane
// stands on edge to world Z (a Front/Left/Right-style UCS made with `UCS X 90`, `UCS Y 90`, etc.).
//
// Before this fix, ApplyOrthoConstrainFromAnchor's UCS branch re-derived a point's Z by SOLVING the
// work-plane equation for (x, y) alone. That only has an answer while the plane's normal has a
// meaningful world-Z component; for a plane standing on edge (normal near world-Z-orthogonal, e.g.
// UCS X 90's normal (0,-1,0)) the solve degenerated to a constant (the frame origin's Z), throwing
// away the cursor's real elevation and feeding ConstrainToUcsOrtho a point that was not actually
// where the user was pointing. The fix passes the ALREADY-RESOLVED real Z of the anchor and the
// cursor (AppCommandState::anchorZ / resolvedPointZ, set by real ray x plane intersection) straight
// through instead of re-deriving it.
//
// Linked into GoSurveySnapTests: needs gosurvey_domain for ApplyOrthoConstrainFromAnchor /
// CadCommands.cpp, like ViewportUcsTests.cpp beside it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {
// UCS X 90 (REQ-312's own example): work plane is world y = 0, normal (0, -1, 0) — a plane on edge
// to world Z, mapping UCS +Y onto world +Z. UcsToWorld(u, v, w) = (u, -w, v).
AppCommandState MakeFrontViewUcsDrawing() {
  AppCommandState st;
  st.activeUcs = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);
  return st;
}
}  // namespace

TEST_CASE("ORTHO squares to a Front-view UCS's vertical (world-Z) axis (issue #371)", "[ucs][ortho][req154]") {
  AppCommandState st = MakeFrontViewUcsDrawing();

  // Anchor at the UCS origin: world (0, 0, 0).
  const float anchorX = 0.f, anchorY = 0.f, anchorZ = 0.f;
  // Cursor mostly ALONG the UCS +Y axis (world +Z), with a small stray X offset — the rubber-band
  // pick a real mouse move would produce. World (2, 0, 10): lx=2 (world X), ly=0 (world Y),
  // targetZ=10 (world Z) is the UCS's free axis here, not derivable from (lx, ly) alone.
  float wx = 2.f, wy = 0.f;
  const float targetZ = 10.f;

  ApplyOrthoConstrainFromAnchor(st, anchorX, anchorY, &wx, &wy, /*ortho=*/true, anchorZ, targetZ);

  // Farther along UCS Y (10) than UCS X (2), so ORTHO should lock world X back to the anchor's (0)
  // and leave world Y on the plane (0) — matching what "squares to the UCS axes" has to mean here.
  REQUIRE(wx == Approx(0.f).margin(1e-4));
  REQUIRE(wy == Approx(0.f).margin(1e-4));
}

TEST_CASE("ORTHO on a Front-view UCS locks the horizontal (world-X) axis when the cursor is nearer it",
         "[ucs][ortho][req154]") {
  AppCommandState st = MakeFrontViewUcsDrawing();

  const float anchorX = 0.f, anchorY = 0.f, anchorZ = 0.f;
  // Cursor mostly along UCS +X (world +X), with a small stray Z: world (10, 0, 2).
  float wx = 10.f, wy = 0.f;
  const float targetZ = 2.f;

  ApplyOrthoConstrainFromAnchor(st, anchorX, anchorY, &wx, &wy, /*ortho=*/true, anchorZ, targetZ);

  // Farther along UCS X (10) than UCS Y (2): world X stays free at the cursor's value, world Y
  // (always 0 on this plane) is untouched.
  REQUIRE(wx == Approx(10.f).margin(1e-4));
  REQUIRE(wy == Approx(0.f).margin(1e-4));
}

TEST_CASE("ORTHO off is still a no-op under a vertical UCS (REQ-047 under REQ-154)", "[ucs][ortho][req154]") {
  AppCommandState st = MakeFrontViewUcsDrawing();
  float wx = 2.f, wy = 0.f;
  ApplyOrthoConstrainFromAnchor(st, 0.f, 0.f, &wx, &wy, /*ortho=*/false, 0.f, 10.f);
  REQUIRE(wx == Approx(2.f));
  REQUIRE(wy == Approx(0.f));
}

// Issue #371 follow-up: ApplyOrthoConstrainFromAnchor's UCS branch correctly computed the locked
// world Z internally (ConstrainToUcsOrtho's z component) but had no channel to report it — only wx/wy
// went back to the caller. Every caller that commits or previews Z independently (CadCommitElevation /
// uiCursorWorldZ) kept using the cursor's raw, un-locked elevation, so a Front/Left/Right-UCS ORTHO
// line rendered/committed diagonally even though wx/wy alone looked correct. Fixed by adding an
// optional wz out-param that the caller must thread back into its own Z source.
TEST_CASE("ORTHO reports the locked world Z through wz when squaring to the UCS's vertical axis",
         "[ucs][ortho][req154]") {
  AppCommandState st = MakeFrontViewUcsDrawing();

  // Same repro as the first UCS test above: anchor at the UCS origin, cursor mostly along UCS +Y
  // (world +Z). This time the anchor is NOT at world Z=0, matching the real bug report where the
  // anchor is an OSNAP CENTRE on geometry off the current work plane (e.g. a circle drawn under a
  // different coordinate system) — world (10, 10, 0) mapped into this UCS's origin-relative test.
  const float anchorX = 0.f, anchorY = 0.f, anchorZ = 3.f;
  float wx = 2.f, wy = 0.f;
  const float targetZ = 10.f;
  float wz = -999.f;  // sentinel: must be overwritten when the axis locks onto world Z

  ApplyOrthoConstrainFromAnchor(st, anchorX, anchorY, &wx, &wy, /*ortho=*/true, anchorZ, targetZ, &wz);

  // Dominant axis is UCS Y (world Z): wx locks back to the anchor's world X, and the caller's Z
  // source must be told to render/commit at the CURSOR's world Z (10), not the anchor's (3) — ORTHO
  // constrains the in-plane offset, it does not flatten the segment onto one elevation.
  REQUIRE(wx == Approx(0.f).margin(1e-4));
  REQUIRE(wz == Approx(10.f).margin(1e-4));
}

TEST_CASE("ORTHO reports the anchor's world Z through wz when the horizontal axis is dominant",
         "[ucs][ortho][req154]") {
  AppCommandState st = MakeFrontViewUcsDrawing();

  const float anchorX = 0.f, anchorY = 0.f, anchorZ = 3.f;
  float wx = 10.f, wy = 0.f;
  const float targetZ = 2.f;
  float wz = -999.f;

  ApplyOrthoConstrainFromAnchor(st, anchorX, anchorY, &wx, &wy, /*ortho=*/true, anchorZ, targetZ, &wz);

  // Dominant axis is UCS X (world X): world Z locks back to the ANCHOR's (3), not the cursor's raw
  // targetZ (2) — this is the exact lock that was previously computed and then discarded, letting a
  // caller's independently-derived elevation (CadCommitElevation / uiCursorWorldZ) draw diagonally.
  REQUIRE(wx == Approx(10.f).margin(1e-4));
  REQUIRE(wz == Approx(3.f).margin(1e-4));
}

TEST_CASE("ORTHO leaves wz untouched under the World UCS", "[ucs][ortho][req154]") {
  AppCommandState st;  // World UCS by default
  float wx = 5.f, wy = 0.f;
  float wz = 42.f;  // sentinel: World-UCS ORTHO never adjusts Z, so this must survive unchanged
  ApplyOrthoConstrainFromAnchor(st, 0.f, 0.f, &wx, &wy, /*ortho=*/true, 0.f, 0.f, &wz);
  REQUIRE(wz == Approx(42.f));
}

// Issue #371 THIRD follow-up: comparing raw UCS-delta magnitude (ConstrainToUcsOrtho's decision)
// only tells you which axis the cursor is "farther along" while the camera is a plan view of the
// UCS. Once the camera is orbited (any UCSFOLLOW=0 drag with a Front/Left/Right-style UCS is
// exactly this — the camera never had to move for the coordinate system to change), one screen
// direction generically maps onto a MIX of both in-plane UCS axes, so the raw-delta comparison can
// lock the wrong one even though the user is visibly dragging along a single screen direction.
// Confirmed against AutoCAD/Civil3D: an ORTHO drag under a Front UCS renders perfectly
// screen-vertical from any orbit angle, because AutoCAD's decision is a SCREEN one.
namespace {
// The same Front UCS as above, plus a live, ORBITED (non-plan) camera and a published viewport
// size — the two things ApplyOrthoConstrainFromAnchor needs to take the screen-aware path instead
// of the world-space fallback.
AppCommandState MakeOrbitedFrontViewUcsDrawing() {
  AppCommandState st = [] {
    AppCommandState s;
    s.activeUcs = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);
    return s;
  }();
  st.viewportAzimuthDeg = 60.f;
  st.viewportElevationDeg = 20.f;  // not 90 -> orbited, not a plan view (CadViewIsPlan is false)
  st.viewportZoom = 1.f;
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  return st;
}
}  // namespace

TEST_CASE("ConstrainToUcsOrthoOnScreen can pick the OPPOSITE axis from the world-space decision "
         "under an orbited camera",
         "[ucs][ortho][req154]") {
  const ucs::Ucs frame = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);
  const Camera cam = [] {
    Camera c = Camera::Plan(0.0, 0.0, 50.f);
    c.azimuthDeg = 60.f;
    c.elevationDeg = 20.f;
    return c;
  }();

  const ray3d::Vec3 anchor{0.0, 0.0, 0.0};
  // A cursor hit whose raw UCS X offset (6) is bigger than its raw UCS Y / world-Z offset (4): the
  // world-space decision (ConstrainToUcsOrtho) locks Z and leaves world X free — (6, 0, 0).
  const ray3d::Vec3 target{6.0, 0.0, 4.0};

  const ray3d::Vec3 worldDecision = ConstrainToUcsOrtho(frame, anchor, target);
  const ray3d::Vec3 screenDecision =
      ConstrainToUcsOrthoOnScreen(frame, anchor, target, cam, 1200.f, 700.f);

  // The world-space decision locks Z (world X free): (6, 0, 0).
  REQUIRE(worldDecision.x == Approx(6.0).margin(1e-6));
  REQUIRE(worldDecision.z == Approx(0.0).margin(1e-6));

  // Whichever candidate the screen decision picks, it must be the one whose SCREEN projection is
  // actually closer to the raw cursor hit's own screen projection — not whatever the world-space
  // magnitude comparison says. Verify that invariant directly, camera-orientation-agnostic.
  float tx = 0.f, ty = 0.f, sx = 0.f, sy = 0.f, wxs = 0.f, wys = 0.f;
  cam.WorldToScreen(target.x, target.y, target.z, 1200.f, 700.f, &tx, &ty);
  cam.WorldToScreen(screenDecision.x, screenDecision.y, screenDecision.z, 1200.f, 700.f, &sx, &sy);
  cam.WorldToScreen(worldDecision.x, worldDecision.y, worldDecision.z, 1200.f, 700.f, &wxs, &wys);
  const double distScreenDecision = std::hypot(tx - sx, ty - sy);
  const double distWorldDecision = std::hypot(tx - wxs, ty - wys);
  REQUIRE(distScreenDecision <= distWorldDecision + 1e-6);

  // For this specific camera (az=60, el=20 — an asymmetric oblique orbit), the two decisions
  // actually diverge: the screen-nearer candidate is NOT the world-space decision's answer.
  REQUIRE(distScreenDecision < distWorldDecision - 1e-6);
}

TEST_CASE("ApplyOrthoConstrainFromAnchor takes the screen-aware path once a live viewport is "
         "published",
         "[ucs][ortho][req154]") {
  AppCommandState st = MakeOrbitedFrontViewUcsDrawing();

  const float anchorX = 0.f, anchorY = 0.f, anchorZ = 0.f;
  float wx = 6.f, wy = 0.f;
  const float targetZ = 4.f;  // matches the divergent case above: world (6, 0, 4)
  float wz = -999.f;

  ApplyOrthoConstrainFromAnchor(st, anchorX, anchorY, &wx, &wy, /*ortho=*/true, anchorZ, targetZ, &wz);

  // The world-space fallback (used when no viewport is published) would answer (6, 0) with wz=0.
  // With the viewport published, this must NOT be that answer: the screen-aware path picked the
  // other candidate, confirmed against ConstrainToUcsOrthoOnScreen directly in the test above.
  const bool matchesWorldFallback = (std::fabs(wx - 6.f) < 1e-4f) && (std::fabs(wz - 0.f) < 1e-4f);
  REQUIRE_FALSE(matchesWorldFallback);
}
