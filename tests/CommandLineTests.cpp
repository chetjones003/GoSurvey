#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/CommandBar.hpp"

using Catch::Approx;

// REQ-040: the recent-history overlay is fully opaque until the idle delay, then
// fades to zero over the fade duration.
TEST_CASE("Command-bar history fade alpha", "[commandline]") {
  const double delay = 4.0;
  const double dur = 1.0;

  // Before and at the delay: fully opaque.
  REQUIRE(cmdbar::HistoryAlpha(0.0, delay, dur) == Approx(1.0f));
  REQUIRE(cmdbar::HistoryAlpha(4.0, delay, dur) == Approx(1.0f));

  // Mid-fade: linear ramp down.
  REQUIRE(cmdbar::HistoryAlpha(4.5, delay, dur) == Approx(0.5f));

  // At/after the end of the fade: fully transparent.
  REQUIRE(cmdbar::HistoryAlpha(5.0, delay, dur) == Approx(0.0f));
  REQUIRE(cmdbar::HistoryAlpha(9.0, delay, dur) == Approx(0.0f));

  // Failure mode: a zero-length fade snaps to 0 the instant the delay passes.
  REQUIRE(cmdbar::HistoryAlpha(4.0, delay, 0.0) == Approx(1.0f));
  REQUIRE(cmdbar::HistoryAlpha(4.0001, delay, 0.0) == Approx(0.0f));
}

// REQ-040: the recent-tail / F2-console start index shows the last N lines.
TEST_CASE("Command-log tail start index", "[commandline]") {
  // Fewer lines than the window: show everything from the top.
  REQUIRE(cmdbar::LogTailStart(2, 3) == 0);
  REQUIRE(cmdbar::LogTailStart(3, 3) == 0);

  // More lines than the window: skip the oldest.
  REQUIRE(cmdbar::LogTailStart(10, 3) == 7);

  // Empty log.
  REQUIRE(cmdbar::LogTailStart(0, 3) == 0);

  // Failure mode: a non-positive window shows nothing (start == total).
  REQUIRE(cmdbar::LogTailStart(10, 0) == 10);
  REQUIRE(cmdbar::LogTailStart(10, -1) == 10);
}
