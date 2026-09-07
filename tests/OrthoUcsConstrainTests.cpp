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
