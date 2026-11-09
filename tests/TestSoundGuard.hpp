// Test-only. Linking TestSoundGuard.cpp makes the test executable unable to play Windows sounds; see
// that file for how and why.
#pragma once

#include <cstdint>
#include <string>

namespace testsound {

// How many Windows sound calls this process has blocked so far. Zero when the guard is not linked.
std::uint64_t BlockedCallCount();

// True once every sound entry point has been replaced. The process is stopped at startup when one
// cannot be replaced, so in a running test this is always true.
bool AllEntryPointsReplaced();

// Opens the blocked-call log at `path`, replacing the log that is open, if any. Returns false, and reports
// the Windows error, when the file cannot be opened; the log that was open then stays open. Sounds stay
// blocked either way.
bool OpenBlockedCallLog(const std::wstring& path);
void CloseBlockedCallLog();

// Overwrites the start of `entryPoint` so that it jumps to `replacement`. Returns false, with nothing
// written, when the memory cannot be made writable. Failing to restore the memory protection afterwards
// is reported but does not change the result, because the replacement is already in place. The guard uses
// this itself; it is exposed so that its failure path can be tested.
bool PatchEntryPoint(void* entryPoint, void* replacement);

}  // namespace testsound
