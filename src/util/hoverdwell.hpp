#pragma once

/// "The cursor has been resting here" — the dwell rule behind the surface rollover readout
/// (REQ-089).
///
/// Pure and dependency-free — `<cmath>` only — so `GoSurveyTests` links it without a GL context or
/// the GUI stack, beside `tinbuild` and `surfaceanalysis` and for the same reason. It lives in
/// `util/` rather than beside the UI that uses it because \ref HoverDwell is stored on
/// `AppCommandState`, and the Commands layer may not include a UI header (architecture §11.1). It
/// is a concrete free function over a plain aggregate, not an abstraction: §11.4 is not engaged.
///
/// **The one-shot is the point of this file.** REQ-089 makes "the covering-surface query runs once,
/// when the dwell elapses" an *acceptance condition* rather than a note, because `TinElevationAt` is
/// a linear scan over every triangle and REQ-100's surface profile is the one profile near budget
/// and CPU-bound. Putting the one-shot here — in \ref HoverDwell::armed, cleared the instant it
/// fires — makes "query once per rest" a property of the timer instead of a discipline the call site
/// has to remember. A caller that simply asks "has the dwell elapsed?" every frame and queries on
/// every `true` is the regression this shape prevents.

#include <cmath>

/// Where the cursor has been resting, and since when. Owned by the caller (`AppCommandState`).
///
/// Positions are in whatever units the caller measures movement in — screen pixels at the one call
/// site, which is why the tolerance is spelled `moveTolPx`. Time is likewise the caller's clock;
/// only differences are used.
struct HoverDwell {
  float  x = 0.f;            ///< the resting position this timer is measured from
  float  y = 0.f;
  double stillSince = 0.0;   ///< when the cursor arrived there
  /// False once this rest has already fired. Re-armed by movement, so one rest fires exactly once
  /// however long it lasts.
  bool   armed = false;
};

/// What one frame of dwell did.
struct HoverDwellTick {
  /// The cursor left the resting position — anything latched from the previous rest is now about a
  /// place the cursor no longer is, and must be dropped.
  bool moved = false;
  /// The dwell threshold was crossed **this frame**. True on exactly one frame per rest: this is
  /// the signal to run the expensive query, and it will not come again until the cursor moves.
  bool elapsed = false;
};

/// Begin (or restart) a rest at (\p x, \p y) as of \p now, armed to fire after a full dwell.
///
/// The call site uses this whenever the readout is suppressed — the viewport is not hovered, a
/// command is running, a gesture is in progress — so that returning to an idle cursor costs a fresh
/// dwell rather than firing immediately on a timer that kept running while the readout was hidden.
inline void ResetHoverDwell(HoverDwell* d, float x, float y, double now) {
  if (!d)
    return;
  d->x = x;
  d->y = y;
  d->stillSince = now;
  d->armed = true;
}

/// Advance the timer by one frame.
///
/// \param moveTolPx  how far the cursor may drift and still count as still. Not zero: a mouse at
///                   rest reports sub-pixel jitter on a high-resolution device, and a zero
///                   tolerance would restart the timer forever and the readout would never appear.
/// \param dwellSeconds how long the cursor must rest before \ref HoverDwellTick::elapsed fires.
inline HoverDwellTick UpdateHoverDwell(HoverDwell* d, float x, float y, double now, float moveTolPx,
                                       double dwellSeconds) {
  HoverDwellTick tick;
  if (!d)
    return tick;

  const float dx = x - d->x;
  const float dy = y - d->y;
  if (std::sqrt(dx * dx + dy * dy) > moveTolPx) {
    // Movement restarts the rest AND re-arms it, which together are what make one rest fire once:
    // `elapsed` cannot come back without passing through here.
    ResetHoverDwell(d, x, y, now);
    tick.moved = true;
    return tick;
  }

  // A clock that goes backwards would otherwise make `now - stillSince` negative for as long as the
  // difference lasts, and the readout would simply stop appearing with nothing to show why. Re-base
  // instead: the user pays one more dwell, which is the smallest observable consequence available.
  if (now < d->stillSince) {
    d->stillSince = now;
    return tick;
  }

  if (d->armed && now - d->stillSince >= dwellSeconds) {
    d->armed = false;
    tick.elapsed = true;
  }
  return tick;
}
