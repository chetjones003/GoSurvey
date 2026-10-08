#pragma once

#include <string>
#include <string_view>

namespace crashReporter {

std::string ExceptionCodeToWireReason(unsigned long exceptionCode);

#ifdef _WIN32
bool TryRunCrashReportUiMode();
void InstallHandler();
void SetReportChannel(std::string_view channel);
bool CommandLineHasTestCrash();
[[noreturn]] void TriggerTestCrash();
#else
inline bool TryRunCrashReportUiMode() { return false; }
inline void InstallHandler() {}
inline void SetReportChannel(std::string_view) {}
inline bool CommandLineHasTestCrash() { return false; }
[[noreturn]] inline void TriggerTestCrash() { std::abort(); }
#endif

}  // namespace crashReporter
