// GitHub issue #166 — the throttle behind the per-frame viewport hover pick.
//
// The hover pick (`PickClosestCadEntity` and the annotation/table/filled-region picks beside it) is
// a full entity scan run on every rendered frame the cursor is over the viewport, including through
// the TRIM/EXTEND/BREAK/LENGTHEN entity-selection phases (REQ-056). This gate is the only new logic
// added to fix the frame-rate drop: it decides whether that scan re-runs this frame. What can go
// wrong here is exactly what the user reported — the scan running too often (viewport stutters) or
// a real change being missed (the highlight goes stale) — so both are pinned.

#include <catch2/catch_test_macros.hpp>

#include "util/hoverpickgate.hpp"

namespace {

// The values CadUi.cpp passes, so the tests exercise the shipped configuration.
constexpr float  kMoveTolPx = 1.f;
constexpr double kMinInterval = 1.0 / 30.0;
constexpr double kMaxIdle = 0.25;

bool Run(HoverPickGate* g, float x, float y, double now, const HoverPickView& v = {},
         std::uint32_t rev = 0) {
  return HoverPickGateShouldRun(g, x, y, now, v, rev, kMoveTolPx, kMinInterval, kMaxIdle);
}

} // namespace

TEST_CASE("A fresh gate runs on the first frame", "[hover][issue166]") {
  HoverPickGate g;
  REQUIRE(Run(&g, 100.f, 100.f, 0.0));
}

TEST_CASE("A still cursor with nothing changed does not re-run every frame", "[hover][issue166]") {
  // The regression: 240 frames of a resting cursor must not be 240 scans. It re-runs only on the
  // idle safety net (every 0.25 s), so over 1 s at 240 FPS that is 1 initial + 4 = 5, not 240.
  HoverPickGate g;
  int runs = 0;
  for (int frame = 0; frame < 240; ++frame) {
    const double now = frame / 240.0;  // 1 second at 240 FPS
    if (Run(&g, 50.f, 50.f, now))
      ++runs;
  }
  REQUIRE(runs <= 6);
  REQUIRE(runs >= 4);  // the idle ceiling still fires, so a UCS/layer change is picked up within 0.25 s
}

TEST_CASE("Cursor movement past the tolerance re-runs, but not faster than the interval",
          "[hover][issue166]") {
  HoverPickGate g;
  REQUIRE(Run(&g, 0.f, 0.f, 0.0));

  // Moving every frame at 240 FPS for 1 s: capped near 30 Hz, so ~30 runs, not ~240.
  int runs = 0;
  for (int frame = 1; frame <= 240; ++frame) {
    const double now = frame / 240.0;
    if (Run(&g, static_cast<float>(frame), 0.f, now))  // 1 px/frame — always past the tolerance
      ++runs;
  }
  REQUIRE(runs >= 25);
  REQUIRE(runs <= 35);
}

TEST_CASE("Sub-pixel jitter counts as still", "[hover][issue166]") {
  HoverPickGate g;
  REQUIRE(Run(&g, 300.f, 400.f, 0.0));

  int runs = 0;
  for (int frame = 1; frame <= 60; ++frame) {
    const double now = frame / 60.0;
    const float wobble = (frame % 2 == 0) ? 0.4f : -0.4f;  // inside the 1 px tolerance
    if (Run(&g, 300.f + wobble, 400.f, now))
      ++runs;
  }
  REQUIRE(runs <= 5);  // idle ceiling only — the jitter never re-arms it
}

TEST_CASE("Drift accumulates from the last run, not the previous frame", "[hover][issue166]") {
  // Many sub-tolerance steps must eventually count as a move, or a slow drag would keep a stale
  // highlight the whole way across the viewport.
  HoverPickGate g;
  REQUIRE(Run(&g, 0.f, 0.f, 0.0));
  REQUIRE_FALSE(Run(&g, 0.6f, 0.f, 0.001));  // < 1 px from the last run
  REQUIRE_FALSE(Run(&g, 0.9f, 0.f, 0.002));  // still < 1 px, and inside the interval anyway
  // 1.2 px from the last run AND past the interval: a move.
  REQUIRE(Run(&g, 1.2f, 0.f, 0.05));
}

TEST_CASE("A view change re-runs immediately, even on a still cursor mid-interval",
          "[hover][issue166]") {
  // Pan/zoom/orbit moves geometry under a stationary cursor. The highlight must not lag a frame
  // behind the pan.
  HoverPickGate g;
  const HoverPickView a{};
  REQUIRE(Run(&g, 10.f, 10.f, 0.0, a));
  REQUIRE_FALSE(Run(&g, 10.f, 10.f, 0.001, a));  // nothing changed

  HoverPickView panned = a;
  panned.panX = 5.0;
  REQUIRE(Run(&g, 10.f, 10.f, 0.002, panned));

  HoverPickView orbited = panned;
  orbited.azimuthDeg = 12.f;
  REQUIRE(Run(&g, 10.f, 10.f, 0.003, orbited));
}

TEST_CASE("A geometry-revision bump re-runs immediately", "[hover][issue166]") {
  // Deleting the hovered entity bumps cadGpuRevision; the stale highlight must clear that frame.
  HoverPickGate g;
  REQUIRE(Run(&g, 0.f, 0.f, 0.0, {}, 7));
  REQUIRE_FALSE(Run(&g, 0.f, 0.f, 0.001, {}, 7));
  REQUIRE(Run(&g, 0.f, 0.f, 0.002, {}, 8));
}

TEST_CASE("A clock that jumps backwards costs one run, not a stall", "[hover][issue166]") {
  HoverPickGate g;
  REQUIRE(Run(&g, 0.f, 0.f, 100.0));
  REQUIRE(Run(&g, 0.f, 0.f, 90.0));           // jump back: re-run and re-base
  REQUIRE_FALSE(Run(&g, 0.f, 0.f, 90.001));   // re-based, back to normal throttling
}

TEST_CASE("A null gate always runs (fail-open)", "[hover][issue166]") {
  REQUIRE(Run(nullptr, 0.f, 0.f, 0.0));
}
