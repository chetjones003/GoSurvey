#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace crashReporter {

std::string ExceptionCodeToWireReason(unsigned long exceptionCode);

/// REQ-424 §2: a crash report read back from the bundle file the crash handler wrote.
struct CrashReport {
  std::string wireReason;
  std::string channel;
  std::string reportUtf8;
};

#ifdef _WIN32
/// Reads a crash bundle and deletes the file, readable or not. False when it is malformed.
bool ReadCrashBundle(const std::filesystem::path& bundlePath, CrashReport& out);

/// REQ-424 §2: when this process was started with `--gosurvey-crash-report=<bundle>`, reads the
/// bundle, sends it once in the background (silently, REQ-424 §5), deletes it, and returns true
/// with the window title and text to show. False when this is an ordinary launch. Each front end
/// then shows its own report window.
bool TakeCrashReportFromCommandLine(std::string& titleUtf8, std::string& reportUtf8);

/// The ImGui build's crash-report mode: TakeCrashReportFromCommandLine + the Win32 window.
bool TryRunCrashReportUiMode();
void InstallHandler();
void SetReportChannel(std::string_view channel);
bool CommandLineHasTestCrash();
[[noreturn]] void TriggerTestCrash();
#else
inline bool TakeCrashReportFromCommandLine(std::string&, std::string&) { return false; }
inline bool TryRunCrashReportUiMode() { return false; }
inline void InstallHandler() {}
inline void SetReportChannel(std::string_view) {}
inline bool CommandLineHasTestCrash() { return false; }
[[noreturn]] inline void TriggerTestCrash() { std::abort(); }
#endif

}  // namespace crashReporter
