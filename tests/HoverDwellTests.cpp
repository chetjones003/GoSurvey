// REQ-089 — the dwell rule behind the surface rollover readout.
//
// This is the only genuinely new logic in the feature, which is why it is the only new test file.
// Everything the readout says is computed by code that is already pinned: containment and the
// interpolated elevation by TinQueryTests (REQ-074), and the style-name fallback by
// SurfaceStyleTests (REQ-070). Re-asserting those here would test the same functions twice and say
// nothing about this feature.
//
// What CAN be wrong here is the timing, and every failure of it is a bug a user would report as
// "the tooltip flickers", "the tooltip never comes back", or "the drawing goes sluggish over a
// surface" — that last one being the expensive one, since REQ-089 makes "the query runs ONCE per
// rest" an acceptance condition against REQ-100's surface profile. So the one-shot is asserted as
// hard as the threshold is.

#include <catch2/catch_test_macros.hpp>

#include "util/hoverdwell.hpp"

namespace {

/// The values the call site uses (CadUi.cpp), so the tests exercise the shipped configuration
/// rather than a convenient one.
constexpr double kDwell = 0.5;
constexpr float  kMoveTolPx = 2.f;

} // namespace

TEST_CASE("A rest fires once the dwell has elapsed, and not before", "[hover][req089]") {
  HoverDwell d;
  ResetHoverDwell(&d, 100.f, 100.f, 0.0);

  // Still, but short of the threshold: nothing yet.
  REQUIRE_FALSE(UpdateHoverDwell(&d, 100.f, 100.f, 0.25, kMoveTolPx, kDwell).elapsed);
  REQUIRE_FALSE(UpdateHoverDwell(&d, 100.f, 100.f, 0.49, kMoveTolPx, kDwell).elapsed);

  // Exactly at the threshold counts — the comparison is >=, so a frame landing precisely on it
  // fires rather than waiting a whole extra frame.
  REQUIRE(UpdateHoverDwell(&d, 100.f, 100.f, 0.50, kMoveTolPx, kDwell).elapsed);
}

TEST_CASE("One rest fires exactly once, however long it lasts", "[hover][req089]") {
  // REQ-089: "the covering-surface query runs once, when the dwell elapses, and its result is
  // latched — never re-run per frame." A cursor left on a surface for ten seconds is 600 frames;
  // this asserts that it is one query, not 600.
  HoverDwell d;
  ResetHoverDwell(&d, 10.f, 20.f, 0.0);

  int fired = 0;
  for (int frame = 1; frame <= 600; ++frame) {
    const double now = frame / 60.0;
    if (UpdateHoverDwell(&d, 10.f, 20.f, now, kMoveTolPx, kDwell).elapsed)
      ++fired;
  }
  REQUIRE(fired == 1);
}

TEST_CASE("Sub-pixel jitter does not restart the dwell", "[hover][req089]") {
  // A mouse physically at rest still reports drift on a high-resolution device. With a zero
  // tolerance the timer would restart forever and the readout would never appear at all — the
  // failure this tolerance exists to prevent, so it is asserted rather than assumed.
  HoverDwell d;
  ResetHoverDwell(&d, 300.f, 400.f, 0.0);

  bool elapsed = false;
  for (int frame = 1; frame <= 60; ++frame) {
    const double now = frame / 60.0;
    const float wobble = (frame % 2 == 0) ? 1.f : -1.f;  // ±1 px, inside the 2 px tolerance
    const HoverDwellTick tick = UpdateHoverDwell(&d, 300.f + wobble, 400.f, now, kMoveTolPx, kDwell);
    REQUIRE_FALSE(tick.moved);
    elapsed = elapsed || tick.elapsed;
  }
  REQUIRE(elapsed);
}

TEST_CASE("Movement past the tolerance reports moved, and re-arms the dwell", "[hover][req089]") {
  HoverDwell d;
  ResetHoverDwell(&d, 0.f, 0.f, 0.0);
  REQUIRE(UpdateHoverDwell(&d, 0.f, 0.f, 1.0, kMoveTolPx, kDwell).elapsed);

  // A real move: the latched readout is about a place the cursor has left.
  const HoverDwellTick moved = UpdateHoverDwell(&d, 50.f, 0.f, 1.1, kMoveTolPx, kDwell);
  REQUIRE(moved.moved);
  REQUIRE_FALSE(moved.elapsed);

  // The new rest is measured from the move, not from the original arrival: at 1.4 s the cursor has
  // been at its new position for 0.3 s, so it must not fire yet even though 1.4 s of wall clock has
  // passed since the timer started.
  REQUIRE_FALSE(UpdateHoverDwell(&d, 50.f, 0.f, 1.4, kMoveTolPx, kDwell).elapsed);
  REQUIRE(UpdateHoverDwell(&d, 50.f, 0.f, 1.6, kMoveTolPx, kDwell).elapsed);
}

TEST_CASE("Drift accumulates: many small steps eventually count as a move", "[hover][req089]") {
  // Movement is measured from the resting position, never from the previous frame. Measuring
  // frame-to-frame would let a cursor creep across the whole viewport one pixel at a time while the
  // readout kept describing where it started.
  HoverDwell d;
  ResetHoverDwell(&d, 0.f, 0.f, 0.0);

  REQUIRE_FALSE(UpdateHoverDwell(&d, 1.f, 0.f, 0.01, kMoveTolPx, kDwell).moved);
  REQUIRE_FALSE(UpdateHoverDwell(&d, 2.f, 0.f, 0.02, kMoveTolPx, kDwell).moved);
  REQUIRE(UpdateHoverDwell(&d, 3.f, 0.f, 0.03, kMoveTolPx, kDwell).moved);
}

TEST_CASE("A clock that goes backwards costs one dwell, not the feature", "[hover][req089]") {
  // Without the re-base, `now - stillSince` stays negative for as long as the jump lasts and the
  // readout silently stops working with nothing on screen to explain it.
  HoverDwell d;
  ResetHoverDwell(&d, 5.f, 5.f, 100.0);

  REQUIRE_FALSE(UpdateHoverDwell(&d, 5.f, 5.f, 90.0, kMoveTolPx, kDwell).elapsed);  // clock jumps back
  REQUIRE_FALSE(UpdateHoverDwell(&d, 5.f, 5.f, 90.2, kMoveTolPx, kDwell).elapsed);  // re-based, still waiting
  REQUIRE(UpdateHoverDwell(&d, 5.f, 5.f, 90.5, kMoveTolPx, kDwell).elapsed);        // one dwell later
}

TEST_CASE("Suppressing the readout costs a fresh dwell on return", "[hover][req089]") {
  // ResetHoverDwell is what the call site runs while a command is active or the viewport is not
  // hovered. If the timer were merely left alone, the readout would appear the instant the command
  // ended — on a cursor that had not just come to rest.
  HoverDwell d;
  ResetHoverDwell(&d, 7.f, 7.f, 0.0);
  REQUIRE(UpdateHoverDwell(&d, 7.f, 7.f, 1.0, kMoveTolPx, kDwell).elapsed);

  ResetHoverDwell(&d, 7.f, 7.f, 5.0);  // suppressed, then idle again at t = 5
  REQUIRE_FALSE(UpdateHoverDwell(&d, 7.f, 7.f, 5.2, kMoveTolPx, kDwell).elapsed);
  REQUIRE(UpdateHoverDwell(&d, 7.f, 7.f, 5.5, kMoveTolPx, kDwell).elapsed);
}

TEST_CASE("The dwell functions tolerate a null timer", "[hover][req089]") {
  ResetHoverDwell(nullptr, 0.f, 0.f, 0.0);
  const HoverDwellTick tick = UpdateHoverDwell(nullptr, 0.f, 0.f, 1.0, kMoveTolPx, kDwell);
  REQUIRE_FALSE(tick.moved);
  REQUIRE_FALSE(tick.elapsed);
}
