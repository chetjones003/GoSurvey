// Test-only. Linking TestSoundGuard.cpp makes the test executable unable to play Windows sounds; see
// that file for how and why.
#pragma once

#include <cstdint>

namespace testsound {

// How many Windows sound calls this process has blocked so far. Zero when the guard is not linked.
std::uint64_t BlockedCallCount();

}  // namespace testsound
