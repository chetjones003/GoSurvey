#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include "TestSoundGuard.hpp"

// The guard replaces the Windows sound entry points when the test executable starts, so both calls
// below return without making a sound. Without the guard, each would play a beep.
TEST_CASE("test processes cannot play sounds: MessageBeep and Beep are blocked", "[test-sound]") {
  const std::uint64_t before = testsound::BlockedCallCount();
  REQUIRE(MessageBeep(MB_OK));
  REQUIRE(Beep(1000, 1));
  REQUIRE(testsound::BlockedCallCount() == before + 2);
}
