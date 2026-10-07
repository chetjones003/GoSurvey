#include "CrashReporter.hpp"

#include "AppPaths.hpp"
#include "FailureReportUi.hpp"
#include "HttpFetch.hpp"
#include "TelemetryPing.hpp"
#include "UserPrefs.hpp"
#include "Version.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace crashReporter {
namespace {

#ifdef _WIN32
constexpr wchar_t kCrashReportArgPrefix[] = L"--gosurvey-crash-report=";
constexpr wchar_t kTestCrashArg[]         = L"--gosurvey-test-crash";

char g_reportChannel[16] = "stable";

std::string ExceptionCodeToHumanSummary(unsigned long code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:
    return "Access violation — the program tried to read or write memory it does not own.";
  case EXCEPTION_STACK_OVERFLOW:
    return "Stack overflow — likely infinite recursion or too much data on the stack.";
  default:
    break;
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "Unhandled exception (code 0x%08lX).", code);
  return std::string(buf);
}

std::string FormatAddress(void* address) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%p", address);
  return std::string(buf);
}

void AppendModuleForAddress(void* address, std::string& out) {
  HMODULE module = nullptr;
  if (!::GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(address), &module) ||
      !module) {
    out += "  at ";
    out += FormatAddress(address);
    out += " (unknown module)\n";
    return;
  }

  wchar_t modulePath[MAX_PATH] = L"";
  const DWORD pathLen          = ::GetModuleFileNameW(module, modulePath, MAX_PATH);
  char        moduleUtf8[MAX_PATH * 3] = "";
  if (pathLen > 0) {
    ::WideCharToMultiByte(CP_UTF8, 0, modulePath, static_cast<int>(pathLen), moduleUtf8,
                          static_cast<int>(sizeof(moduleUtf8)), nullptr, nullptr);
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(module);
  const uintptr_t pc   = reinterpret_cast<uintptr_t>(address);
  const uintptr_t rva  = (pc >= base) ? (pc - base) : 0;

  out += "  at ";
  out += FormatAddress(address);
  out += " in ";
  out += moduleUtf8[0] ? moduleUtf8 : "(module)";
  char rvaBuf[32];
  std::snprintf(rvaBuf, sizeof(rvaBuf), " + 0x%llX", static_cast<unsigned long long>(rva));
  out += rvaBuf;
  out += "\n";
}

void AppendStackTrace(EXCEPTION_POINTERS* ep, std::string& out) {
  if (!ep || !ep->ContextRecord)
    return;

  const HANDLE process = ::GetCurrentProcess();
  const HANDLE thread  = ::GetCurrentThread();
  ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!::SymInitialize(process, nullptr, TRUE)) {
    out += "  (stack trace unavailable — SymInitialize failed)\n";
    return;
  }

  STACKFRAME64 frame{};
#if defined(_M_X64) || defined(__x86_64__)
  frame.AddrPC.Offset    = ep->ContextRecord->Rip;
  frame.AddrStack.Offset = ep->ContextRecord->Rsp;
  frame.AddrFrame.Offset = ep->ContextRecord->Rbp;
  const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
  frame.AddrPC.Offset    = ep->ContextRecord->Eip;
  frame.AddrStack.Offset = ep->ContextRecord->Esp;
  frame.AddrFrame.Offset = ep->ContextRecord->Ebp;
  const DWORD machineType = IMAGE_FILE_MACHINE_I386;
#else
  out += "  (stack trace unavailable on this CPU architecture)\n";
  ::SymCleanup(process);
  return;
#endif
  frame.AddrPC.Mode    = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;

  CONTEXT context = *ep->ContextRecord;
  out += "Stack trace (most recent call first):\n";
  for (int frameIndex = 0; frameIndex < 32; ++frameIndex) {
    if (!::StackWalk64(machineType, process, thread, &frame, &context, nullptr,
                       ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr)) {
      break;
    }
    if (frame.AddrPC.Offset == 0)
      break;
    void* pc = reinterpret_cast<void*>(static_cast<uintptr_t>(frame.AddrPC.Offset));
    AppendModuleForAddress(pc, out);
  }
  ::SymCleanup(process);
}

struct CrashReportBundle {
  std::string wireReason;
  std::string reportUtf8;
};

std::filesystem::path CrashLogPath() {
  const std::filesystem::path dir = UserDataDirectory();
  if (dir.empty())
    return std::filesystem::path("crash.log");
  return dir / "crash.log";
}

CrashReportBundle BuildCrashReport(unsigned long exceptionCode, EXCEPTION_POINTERS* ep,
                                   const char* fallbackReasonWire) {
  assert(fallbackReasonWire != nullptr);

  CrashReportBundle bundle;
  bundle.wireReason = fallbackReasonWire;
  if (exceptionCode != 0)
    bundle.wireReason = ExceptionCodeToWireReason(exceptionCode);

  std::string body;
  body += "GoSurvey stopped because of an unexpected error.\n\n";
  body += "Summary:\n";
  if (exceptionCode != 0) {
    body += ExceptionCodeToHumanSummary(exceptionCode);
    body += "\n";
    char codeLine[64];
    std::snprintf(codeLine, sizeof(codeLine), "Exception code: 0x%08lX\n", exceptionCode);
    body += codeLine;
  } else {
    body += "The process aborted without a structured exception record.\n";
  }

  if (ep && ep->ExceptionRecord) {
    body += "Fault address: ";
    body += FormatAddress(ep->ExceptionRecord->ExceptionAddress);
    body += "\n";
    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
      const ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
      const ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
      body += op == 0 ? "Access: read at " : (op == 1 ? "Access: write at " : "Access: execute at ");
      body += FormatAddress(reinterpret_cast<void*>(addr));
      body += "\n";
    }
  }

  body += "\nversion: ";
  body += GOSURVEY_VERSION_FULL;
  body += "\nchannel: ";
  body += g_reportChannel;
  body += "\n\n";
  AppendStackTrace(ep, body);

  body += "\nA copy of this report was appended to:\n";
  body += CrashLogPath().u8string();
  body += "\n\nUse \"Send This Report\" to copy this text and open GitHub to file an issue.";

  bundle.reportUtf8 = std::move(body);
  return bundle;
}

void AppendCrashLog(const std::string& wireReason, const std::string& reportUtf8) {
  const std::filesystem::path logPath = CrashLogPath();
  std::error_code             ec;
  if (!logPath.parent_path().empty())
    std::filesystem::create_directories(logPath.parent_path(), ec);

  std::ofstream out(logPath, std::ios::app);
  if (!out)
    return;

  const std::time_t t = std::time(nullptr);
  char              timeBuf[32] = "0000-00-00 00:00:00";
  struct tm         tmInfo{};
  if (localtime_s(&tmInfo, &t) == 0)
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

  out << "---- " << timeBuf << " ----\n";
  out << "reason: " << wireReason << "\n";
  out << reportUtf8 << "\n\n";
}

void TryPostCrashReportSilent(const std::string& wireReason, const std::string& reportUtf8) {
  if (!HasInternetConnectivity())
    return;

  std::string report = reportUtf8;
  constexpr size_t kMaxReport = 16384;
  if (report.size() > kMaxReport)
    report.resize(kMaxReport);

  const TelemetryIds ids = GetTelemetryIds();
  nlohmann::json body;
  body["installId"] = ids.installId.empty() ? "unknown" : ids.installId;
  body["version"]   = GOSURVEY_VERSION_FULL;
  body["channel"]   = g_reportChannel;
  body["os"]        = "windows";
  body["reason"]    = wireReason;
  body["report"]    = report;

  std::string error;
  std::string response;
  (void)HttpPostJson(CrashReportEndpoint, body.dump(), 8000, error, &response);
}

std::wstring Utf8PathToWide(const std::filesystem::path& path) {
  const std::string utf8 = path.u8string();
  if (utf8.empty())
    return std::wstring();
  const int need =
      ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
  if (need <= 0)
    return std::wstring();
  std::wstring out(static_cast<size_t>(need), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
  return out;
}

bool WriteCrashBundleFile(const std::filesystem::path& path, const std::string& wireReason,
                          const std::string& reportUtf8) {
  std::error_code ec;
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  out << "GOSURVEY_CRASH_META reason=" << wireReason << " channel=" << g_reportChannel << "\n";
  out << "---\n";
  out << reportUtf8;
  return static_cast<bool>(out);
}

bool SpawnCrashReportUi(const std::filesystem::path& bundlePath) {
  wchar_t exePath[MAX_PATH] = L"";
  if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
    return false;

  const std::wstring bundleWide = Utf8PathToWide(bundlePath);
  if (bundleWide.empty())
    return false;

  std::wstring cmdLine = L"\"";
  cmdLine += exePath;
  cmdLine += L"\" ";
  cmdLine += kCrashReportArgPrefix;
  cmdLine += bundleWide;

  std::vector<wchar_t> cmdMutable(cmdLine.begin(), cmdLine.end());
  cmdMutable.push_back(L'\0');

  STARTUPINFOW        si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  if (!::CreateProcessW(nullptr, cmdMutable.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &si, &pi)) {
    return false;
  }
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return true;
}

std::filesystem::path MakePendingCrashBundlePath() {
  wchar_t tempDir[MAX_PATH + 1] = L"";
  const DWORD tempLen           = ::GetTempPathW(MAX_PATH, tempDir);
  if (tempLen == 0 || tempLen > MAX_PATH)
    return std::filesystem::path();

  const DWORD       pid = ::GetCurrentProcessId();
  const std::time_t t   = std::time(nullptr);
  wchar_t           fileName[128];
  swprintf_s(fileName, L"gosurvey-crash-%lu-%lld.txt", static_cast<unsigned long>(pid),
             static_cast<long long>(t));

  std::wstring full(tempDir);
  full += fileName;
  return std::filesystem::path(full);
}

void HandleCrash(unsigned long exceptionCode, EXCEPTION_POINTERS* ep, const char* fallbackReasonWire) {
  static volatile LONG handled = 0;
  if (::InterlockedExchange(&handled, 1) != 0)
    return;

  const CrashReportBundle bundle = BuildCrashReport(exceptionCode, ep, fallbackReasonWire);
  AppendCrashLog(bundle.wireReason, bundle.reportUtf8);

  const std::filesystem::path bundlePath = MakePendingCrashBundlePath();
  if (!bundlePath.empty() && WriteCrashBundleFile(bundlePath, bundle.wireReason, bundle.reportUtf8)) {
    (void)SpawnCrashReportUi(bundlePath);
  }
}

LONG WINAPI UnhandledExceptionFilterHandler(EXCEPTION_POINTERS* ep) {
  unsigned long code = 0;
  if (ep && ep->ExceptionRecord)
    code = ep->ExceptionRecord->ExceptionCode;
  HandleCrash(code, ep, "unhandled_exception");
  return EXCEPTION_EXECUTE_HANDLER;
}

void AbortSignalHandler(int) {
  HandleCrash(0, nullptr, "abort_signal");
  std::_Exit(3);
}

void TerminateHandler() {
  HandleCrash(0, nullptr, "cpp_terminate");
  std::_Exit(3);
}

struct ParsedBundle {
  std::string wireReason;
  std::string channel;
  std::string reportUtf8;
};

bool ParseBundleFile(const std::filesystem::path& path, ParsedBundle& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;

  std::string line;
  if (!std::getline(in, line))
    return false;
  const std::string metaPrefix = "GOSURVEY_CRASH_META ";
  if (line.compare(0, metaPrefix.size(), metaPrefix) != 0)
    return false;

  out.wireReason = "unhandled_exception";
  out.channel    = "stable";
  const std::string rest = line.substr(metaPrefix.size());
  size_t             pos = 0;
  while (pos < rest.size()) {
    while (pos < rest.size() && rest[pos] == ' ')
      ++pos;
    const size_t eq = rest.find('=', pos);
    if (eq == std::string::npos)
      break;
    const std::string key = rest.substr(pos, eq - pos);
    size_t              valStart = eq + 1;
    size_t              valEnd   = rest.find(' ', valStart);
    if (valEnd == std::string::npos)
      valEnd = rest.size();
    const std::string value = rest.substr(valStart, valEnd - valStart);
    if (key == "reason")
      out.wireReason = value;
    else if (key == "channel")
      out.channel = value;
    pos = valEnd + 1;
  }

  if (!std::getline(in, line) || line != "---")
    return false;

  std::ostringstream body;
  body << in.rdbuf();
  out.reportUtf8 = body.str();
  return !out.reportUtf8.empty();
}

bool CommandLineHasArg(const wchar_t* arg) {
  int     argc  = 0;
  LPWSTR* argvW = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argvW)
    return false;
  bool found = false;
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argvW[i], arg) == 0) {
      found = true;
      break;
    }
  }
  ::LocalFree(argvW);
  return found;
}
#endif

}  // namespace

#ifdef _WIN32
void SetReportChannel(std::string_view channel) {
  assert(!channel.empty());
  const std::string ch(channel);
  if (ch == "beta") {
    std::strncpy(g_reportChannel, "beta", sizeof(g_reportChannel) - 1);
    g_reportChannel[sizeof(g_reportChannel) - 1] = '\0';
  } else {
    std::strncpy(g_reportChannel, "stable", sizeof(g_reportChannel) - 1);
    g_reportChannel[sizeof(g_reportChannel) - 1] = '\0';
  }
}

void InstallHandler() {
  assert(g_reportChannel[0] != '\0');
  ::SetUnhandledExceptionFilter(UnhandledExceptionFilterHandler);
  std::signal(SIGABRT, AbortSignalHandler);
  std::set_terminate(TerminateHandler);
}

bool CommandLineHasTestCrash() { return CommandLineHasArg(kTestCrashArg); }

[[noreturn]] void TriggerTestCrash() {
  volatile int* invalid = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x1));
  *invalid              = 0;
}

bool TryRunCrashReportUiMode() {
  int     argc  = 0;
  LPWSTR* argvW = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argvW)
    return false;

  std::filesystem::path bundlePath;
  for (int i = 1; i < argc; ++i) {
    const wchar_t* arg = argvW[i];
    const size_t   prefixLen = wcslen(kCrashReportArgPrefix);
    if (wcsncmp(arg, kCrashReportArgPrefix, prefixLen) == 0) {
      bundlePath = std::filesystem::path(arg + prefixLen);
      break;
    }
  }
  ::LocalFree(argvW);

  if (bundlePath.empty())
    return false;

  ParsedBundle parsed;
  if (!ParseBundleFile(bundlePath, parsed)) {
    failureReportUi::ShowReportDialog(L"GoSurvey crash report",
                                      "GoSurvey could not read the crash report file.\n");
    std::error_code ec;
    std::filesystem::remove(bundlePath, ec);
    return true;
  }

  std::strncpy(g_reportChannel, parsed.channel.c_str(), sizeof(g_reportChannel) - 1);
  g_reportChannel[sizeof(g_reportChannel) - 1] = '\0';

  TryPostCrashReportSilent(parsed.wireReason, parsed.reportUtf8);
  failureReportUi::ShowReportDialog(L"GoSurvey encountered a problem", parsed.reportUtf8);

  std::error_code ec;
  std::filesystem::remove(bundlePath, ec);
  return true;
}
#endif

}  // namespace crashReporter
