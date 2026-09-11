#pragma once

/// "Should the per-frame viewport hover pick actually re-run this frame?" — the throttle behind
/// GitHub issue #166.
///
/// Pure and dependency-free (`<cmath>` / `<cstdint>` only), like `util/hoverdwell.hpp` beside it,
/// so `GoSurveyTests` links it without a GL context or the GUI stack. It lives in `util/` rather
/// than beside the UI because \ref HoverPickGate is stored on `AppCommandState`, and the Commands
/// layer may not include a UI header (architecture §11.1). It is a concrete free function over a
/// plain aggregate, not an abstraction: §11.4 is not engaged.
///
/// **Why it exists.** The viewport hover pick — `PickClosestCadEntity` plus the annotation, table
/// and filled-region picks beside it in `CadUi.cpp` — is a linear scan over every entity in the
/// drawing. It runs on every rendered frame the cursor is over the viewport: when idle, and also
/// during the TRIM / EXTEND / BREAK / LENGTHEN entity-selection phases, which are the only commands
/// that keep hover feedback on (REQ-056). On a dense drawing that scan is a visible slice of the
/// frame budget (REQ-100), and the render loop is unconditional, so it shows up directly as a lower
/// frame rate for the whole command.
///
/// The pick's result only changes when the cursor moves, the view moves, or the geometry changes.
/// This gate lets the one call site reuse the previous frame's result until one of those happens —
/// and, while the cursor is sweeping, caps the re-run rate so a high-refresh monitor does not pay
/// the scan hundreds of times a second. A stale hover highlight for a few milliseconds while the
/// cursor is in motion is imperceptible; a stuttering viewport is not.

#include <cmath>
#include <cstdint>

/// The view state the hover pick depends on: any change to it can move geometry under the cursor
/// without the cursor itself moving (pan, zoom, orbit). Compared for exact equality — the question
/// is only "did the view change at all since the last pick".
struct HoverPickView {
  double panX = 0.0;
  double panY = 0.0;
  double panZ = 0.0;
  float  zoom = 0.f;
  float  azimuthDeg = 0.f;
  float  elevationDeg = 0.f;
  float  rollDeg = 0.f;

  bool operator==(const HoverPickView& o) const {
    return panX == o.panX && panY == o.panY && panZ == o.panZ && zoom == o.zoom &&
           azimuthDeg == o.azimuthDeg && elevationDeg == o.elevationDeg && rollDeg == o.rollDeg;
  }
  bool operator!=(const HoverPickView& o) const { return !(*this == o); }
};

/// What the last hover pick was measured against. Owned by the caller (`AppCommandState`).
struct HoverPickGate {
  bool          primed = false;      ///< false until the first run — a fresh gate always runs
  float         x = 0.f;             ///< cursor position (viewport pixels) at the last run
  float         y = 0.f;
  double        lastRunSec = 0.0;    ///< caller's clock at the last run; only differences are used
  HoverPickView view{};              ///< view state at the last run
  std::uint32_t revision = 0;        ///< geometry revision (cadGpuRevision) at the last run
};

/// Decide whether the caller should run the expensive hover pick this frame, recording the frame's
/// inputs when it says yes so the next call measures against them.
///
/// \param g               the caller's gate; a null gate always runs (fail-open).
/// \param x, y            current cursor position, in the same units \ref HoverPickGate::x is.
/// \param now             current time on the caller's clock.
/// \param view, revision  current view state and geometry revision.
/// \param moveTolPx       drift below this counts as "cursor still" — a resting mouse jitters
///                        sub-pixel on a high-DPI device, and a zero tolerance would re-run forever.
/// \param minIntervalSec  while the cursor IS moving, the shortest gap between re-runs.
/// \param maxIdleSec      re-run at least this often even when nothing tracked here changed — a
///                        cheap safety net for an invalidation input not folded into \p view /
///                        \p revision (a UCS change, a layer freeze).
inline bool HoverPickGateShouldRun(HoverPickGate* g, float x, float y, double now,
                                   const HoverPickView& view, std::uint32_t revision, float moveTolPx,
                                   double minIntervalSec, double maxIdleSec) {
  if (!g)
    return true;

  const auto record = [&] {
    g->primed = true;
    g->x = x;
    g->y = y;
    g->lastRunSec = now;
    g->view = view;
    g->revision = revision;
  };

  // A first run, a changed view or geometry, or a clock that jumped backwards: re-run now. The
  // clock check also re-bases `lastRunSec`, so a backward jump costs one extra run, not a stall.
  if (!g->primed || view != g->view || revision != g->revision || now < g->lastRunSec) {
    record();
    return true;
  }

  const float dx = x - g->x;
  const float dy = y - g->y;
  const bool moved = std::sqrt(dx * dx + dy * dy) > moveTolPx;
  const double sinceRun = now - g->lastRunSec;

  // Moving: re-run once the minimum interval has passed (the highlight tracks the cursor at that
  // rate). Still: re-run only on the idle safety net. Drift is measured from the last run, not the
  // previous frame, so many sub-tolerance steps still accumulate into a move.
  if (moved ? (sinceRun >= minIntervalSec) : (sinceRun >= maxIdleSec)) {
    record();
    return true;
  }
  return false;
}
