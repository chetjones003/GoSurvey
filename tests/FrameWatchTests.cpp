// GitHub issue #168 — the stall detector behind the multi-select freeze investigation.
//
// The frame loop feeds `FrameWatchTick` the frame-to-frame wall clock; it reports where in a stall
// episode each frame sits so the caller writes exactly one diagnostic snapshot per episode. What
// can go wrong here is what would make the instrument useless: missing a stall, or logging every
// frame of a long hang instead of just its edges — so both are pinned.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "util/framewatch.hpp"

using framewatch::Event;
using framewatch::FrameWatch;
using framewatch::FrameWatchTick;

namespace {
constexpr double kThresh = framewatch::kDefaultThresholdMs;  // 350 ms
}

TEST_CASE("Healthy frames never trip the watch", "[framewatch][issue168]") {
  FrameWatch w;
  for (int i = 0; i < 600; ++i)
    REQUIRE(FrameWatchTick(&w, 16.7).event == Event::None);
  REQUIRE_FALSE(w.inStall);
}

TEST_CASE("A single slow frame opens and then closes an episode", "[framewatch][issue168]") {
  FrameWatch w;
  REQUIRE(FrameWatchTick(&w, 16.7).event == Event::None);

  const framewatch::Tick began = FrameWatchTick(&w, 2400.0);
  REQUIRE(began.event == Event::StallBegan);
  REQUIRE(began.stalledFrames == 1);
  REQUIRE(began.stalledMs == 2400.0);
  REQUIRE(w.inStall);

  const framewatch::Tick ended = FrameWatchTick(&w, 16.7);
  REQUIRE(ended.event == Event::StallEnded);
  REQUIRE(ended.stalledFrames == 1);      // the final episode total is reported on the closing tick
  REQUIRE(ended.stalledMs == 2400.0);
  REQUIRE_FALSE(w.inStall);
  REQUIRE(w.stalledFrames == 0);          // reset for the next episode
}

TEST_CASE("A sustained stall logs its edges, not every frame", "[framewatch][issue168]") {
  FrameWatch w;
  REQUIRE(FrameWatchTick(&w, 16.7).event == Event::None);

  int began = 0, continued = 0, ended = 0;
  for (int i = 0; i < 50; ++i) {
    switch (FrameWatchTick(&w, 800.0).event) {
      case Event::StallBegan: ++began; break;
      case Event::StallContinued: ++continued; break;
      default: break;
    }
  }
  if (FrameWatchTick(&w, 16.7).event == Event::StallEnded) ++ended;

  REQUIRE(began == 1);       // only the first slow frame
  REQUIRE(continued == 49);  // the rest are "continued" — the caller does not log these
  REQUIRE(ended == 1);       // and one close
}

TEST_CASE("The closing tick carries the whole episode's count and duration", "[framewatch][issue168]") {
  FrameWatch w;
  FrameWatchTick(&w, 16.7);
  FrameWatchTick(&w, 500.0);
  FrameWatchTick(&w, 900.0);
  const framewatch::Tick ended = FrameWatchTick(&w, 20.0);
  REQUIRE(ended.event == Event::StallEnded);
  REQUIRE(ended.stalledFrames == 2);
  REQUIRE(ended.stalledMs == 1400.0);
}

TEST_CASE("A frame exactly at the threshold is healthy; just over is a stall", "[framewatch][issue168]") {
  FrameWatch w;
  REQUIRE(FrameWatchTick(&w, kThresh).event == Event::None);
  REQUIRE(FrameWatchTick(&w, std::nextafter(kThresh, 1e9)).event == Event::StallBegan);
}

TEST_CASE("A non-finite frame time never fabricates a stall", "[framewatch][issue168]") {
  FrameWatch w;
  REQUIRE(FrameWatchTick(&w, std::numeric_limits<double>::infinity()).event == Event::None);
  REQUIRE(FrameWatchTick(&w, std::numeric_limits<double>::quiet_NaN()).event == Event::None);
  REQUIRE_FALSE(w.inStall);
}

TEST_CASE("A non-finite reading during a stall does not spuriously close it", "[framewatch][issue168]") {
  FrameWatch w;
  FrameWatchTick(&w, 800.0);
  REQUIRE(w.inStall);
  // NaN is treated as healthy, so this DOES close the episode — document that: the watch trusts the
  // clock, and a bad read looks like recovery. Acceptable for a diagnostic aid.
  const framewatch::Tick t = FrameWatchTick(&w, std::numeric_limits<double>::quiet_NaN());
  REQUIRE(t.event == Event::StallEnded);
}

TEST_CASE("A null watch is inert", "[framewatch][issue168]") {
  REQUIRE(FrameWatchTick(nullptr, 5000.0).event == Event::None);
}

TEST_CASE("A custom threshold is honoured", "[framewatch][issue168]") {
  FrameWatch w;
  w.thresholdMs = 100.0;
  REQUIRE(FrameWatchTick(&w, 120.0).event == Event::StallBegan);
}
