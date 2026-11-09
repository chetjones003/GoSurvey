#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <fstream>
#include <iterator>
#include <string>

#include "TestSoundGuard.hpp"

// Opening a log replaces the one that is open, including the one that GOSURVEY_BLOCKED_SOUND_LOG opened at
// startup. This reopens that log when it goes out of scope, so the tests after it still log their calls.
class RestoreEnvironmentLog {
 public:
  RestoreEnvironmentLog() {
    const DWORD size = GetEnvironmentVariableW(kVariable, nullptr, 0);
    if (size <= 1) return;  // not set, or set but empty: there is no log to restore
    path_.resize(size);
    const DWORD length = GetEnvironmentVariableW(kVariable, path_.data(), size);
    path_.resize(length < size ? length : 0);
  }
  ~RestoreEnvironmentLog() {
    if (!path_.empty()) testsound::OpenBlockedCallLog(path_);  // a failure is reported by the guard
  }
  RestoreEnvironmentLog(const RestoreEnvironmentLog&) = delete;
  RestoreEnvironmentLog& operator=(const RestoreEnvironmentLog&) = delete;

 private:
  static constexpr wchar_t kVariable[] = L"GOSURVEY_BLOCKED_SOUND_LOG";
  std::wstring path_;
};

// The guard replaces the Windows sound entry points when the test executable starts, so both calls
// below return without making a sound. Without the guard, each would play a beep.
TEST_CASE("test processes cannot play sounds: MessageBeep and Beep are blocked", "[test-sound]") {
  const std::uint64_t before = testsound::BlockedCallCount();
  REQUIRE(MessageBeep(MB_OK));
  REQUIRE(Beep(1000, 1));
  REQUIRE(testsound::BlockedCallCount() == before + 2);
}

TEST_CASE("every sound entry point was replaced when the test process started", "[test-sound]") {
  REQUIRE(testsound::AllEntryPointsReplaced());
}

// Address 1 is not mapped, so VirtualProtect fails and nothing is written. The failure has to reach the
// caller, which stops the process rather than continuing with sounds that are not blocked.
TEST_CASE("PatchEntryPoint reports an entry point it cannot change", "[test-sound]") {
  REQUIRE_FALSE(testsound::PatchEntryPoint(reinterpret_cast<void*>(1), reinterpret_cast<void*>(2)));
}

// The directory and the file name contain characters outside the ANSI code page. They are written as
// UTF-16 escapes so that this file stays ASCII. The environment log is reopened when the test ends.
TEST_CASE("the blocked-call log opens at a path with non-ASCII characters", "[test-sound]") {
  RestoreEnvironmentLog restoreEnvironmentLog;
  wchar_t tempDir[MAX_PATH + 1] = {};
  REQUIRE(GetTempPathW(MAX_PATH + 1, tempDir) > 0);
  const std::wstring folder =
      std::wstring(tempDir) + L"GoSurveyGuard-\u65e5\u672c-" + std::to_wstring(GetCurrentProcessId());
  REQUIRE(CreateDirectoryW(folder.c_str(), nullptr));
  const std::wstring logPath = folder + L"\\blocked-\u65e5\u672c.log";
  REQUIRE(testsound::OpenBlockedCallLog(logPath));
  REQUIRE(MessageBeep(MB_OK));
  testsound::CloseBlockedCallLog();

  std::ifstream in(logPath, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE(text.find("MessageBeep") != std::string::npos);
  in.close();
  REQUIRE(DeleteFileW(logPath.c_str()));
  REQUIRE(RemoveDirectoryW(folder.c_str()));
}

// The folder is never created, so the log cannot be opened. The failure is reported, the log that was open
// (such as the one from GOSURVEY_BLOCKED_SOUND_LOG) stays open, and the call is still counted.
TEST_CASE("a blocked-call log that cannot be opened leaves sounds blocked", "[test-sound]") {
  wchar_t tempDir[MAX_PATH + 1] = {};
  REQUIRE(GetTempPathW(MAX_PATH + 1, tempDir) > 0);
  const std::wstring missingFolder =
      std::wstring(tempDir) + L"GoSurveyGuard-missing-" + std::to_wstring(GetCurrentProcessId());
  REQUIRE_FALSE(testsound::OpenBlockedCallLog(missingFolder + L"\\blocked.log"));
  const std::uint64_t before = testsound::BlockedCallCount();
  REQUIRE(MessageBeep(MB_OK));
  REQUIRE(testsound::BlockedCallCount() == before + 1);
}
