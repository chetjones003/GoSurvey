#pragma once

/// "Did this frame take long enough to be the freeze the user is reporting?" — the detector behind
/// GitHub issue #168 (cursor disappears / app freezes while building a multi-object selection).
///
/// Pure and dependency-free (`<cmath>` only), like `util/hoverpickgate.hpp` beside it, so
/// `GoSurveyTests` links it without a GL context or the GUI stack. It is a concrete free function
/// over a plain aggregate, not an abstraction: architecture §11.4 is not engaged.
///
/// **Why it exists.** Issue #168 is intermittent and cannot be reproduced by automated input (the
/// GUI hover/cursor path needs a real hovered frame). Prior per-frame-cost bugs in this codebase
/// (issues #166 / #173) presented the same way — a multi-second recoverable stall, not a true
/// deadlock — and were only pinned down once a number was attached to the stalling subsystem. This
/// watch is that instrument: the frame loop feeds it the frame-to-frame wall clock, and it reports
/// the edges of a stall (began / ended) so the caller can write ONE diagnostic snapshot per episode
/// rather than flooding a log during a long hang.
///
/// **Limitation.** This runs on the main thread at the top of the frame loop, so it only sees a
/// stall once control returns to the loop. It catches a slow frame and a multi-second recoverable
/// stall; it cannot catch a true infinite loop that never returns. If a capture comes back empty
/// after a confirmed freeze, the freeze is a genuine deadlock and needs a separate watchdog thread.

#include <cmath>

namespace framewatch {

/// A frame slower than this is treated as a stall. Well above any normal frame (vsync caps a healthy
/// frame near the refresh interval, ~16 ms at 60 Hz; a file open or a surface regen is tens of ms),
/// and well below the "app is frozen" range the issue describes (hundreds of ms and up).
constexpr double kDefaultThresholdMs = 350.0;

/// Caller state. Owned by the frame loop; one instance for the process.
struct FrameWatch {
  double thresholdMs = kDefaultThresholdMs;
  bool   inStall = false;      ///< true between the first slow frame and the first healthy one after
  int    stalledFrames = 0;    ///< slow frames in the current episode
  double stalledMs = 0.0;      ///< summed frame time of the current episode
};

enum class Event {
  None,            ///< a healthy frame outside any stall
  StallBegan,      ///< this frame is the first slow frame of an episode
  StallContinued,  ///< another slow frame in an episode already open
  StallEnded,      ///< this frame is healthy and closed an open episode
};

struct Tick {
  Event  event = Event::None;
  int    stalledFrames = 0;  ///< episode frame count (at Began: 1; at Ended: the final total)
  double stalledMs = 0.0;    ///< episode summed frame time
  double frameMs = 0.0;      ///< the frame time just measured
};

/// Feed one frame-to-frame measurement; get back where in a stall episode it sits.
///
/// A non-finite \p frameMs (a bad clock read) is treated as healthy — the watch never fabricates a
/// stall from garbage input, matching the fail-open stance of `HoverPickGateShouldRun`.
inline Tick FrameWatchTick(FrameWatch* w, double frameMs) {
  Tick t;
  t.frameMs = frameMs;
  if (!w)
    return t;

  const bool slow = std::isfinite(frameMs) && frameMs > w->thresholdMs;
  if (slow) {
    w->stalledFrames += 1;
    w->stalledMs += frameMs;
    t.event = w->inStall ? Event::StallContinued : Event::StallBegan;
    w->inStall = true;
    t.stalledFrames = w->stalledFrames;
    t.stalledMs = w->stalledMs;
    return t;
  }

  if (w->inStall) {
    t.event = Event::StallEnded;
    t.stalledFrames = w->stalledFrames;
    t.stalledMs = w->stalledMs;
    w->inStall = false;
    w->stalledFrames = 0;
    w->stalledMs = 0.0;
  }
  return t;
}

}  // namespace framewatch
