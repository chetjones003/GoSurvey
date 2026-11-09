// Test-only: makes a test executable unable to play Windows sounds.
//
// Windows plays program sounds for the signed-in user session, not for a desktop. So a test that calls
// MessageBeep (Qt's QApplication::beep does), a MessageBox with an icon, PlaySound, Beep, MCI, or a
// balloon through Shell_NotifyIcon makes a sound even when it runs on a hidden desktop. Linking this
// file into a test executable replaces those entry points once, when the process starts. Each
// replacement counts the call and returns success without playing anything. The change is limited to
// this process: other programs keep their sounds, and the registry and sound scheme are not touched.
//
// A replacement that cannot be installed stops the process at startup, with a message on standard
// error. A test run must not continue with sounds that are not blocked.
//
// Set GOSURVEY_BLOCKED_SOUND_LOG to a file path to also write every blocked call there, with the call
// stack that made it. That shows what tried to make a sound, and where. The path may use any characters
// that Windows accepts.

#include "TestSoundGuard.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>

#pragma comment(lib, "dbghelp.lib")

namespace testsound {
namespace {

// Exit code of a process that was stopped because a replacement could not be installed.
constexpr UINT kStartupFailureExitCode = 3;

// The longest path Windows accepts, in wide characters, including the terminating null.
constexpr DWORD kMaxWindowsPathLength = 32768;

struct BlockedEntry {
  const wchar_t* module;  // the DLL that exports the function; loaded first if it is not loaded yet
  const char* name;
  std::uint32_t result;   // what the replacement returns: TRUE, IDOK, MMSYSERR_NODRIVER, ...
};

constexpr BlockedEntry kBlocked[] = {
    {L"user32.dll", "MessageBeep", 1},
    {L"user32.dll", "MessageBoxW", 1},
    {L"user32.dll", "MessageBoxA", 1},
    {L"user32.dll", "MessageBoxExW", 1},
    {L"user32.dll", "MessageBoxExA", 1},
    {L"user32.dll", "MessageBoxIndirectW", 1},
    {L"user32.dll", "MessageBoxIndirectA", 1},
    {L"winmm.dll", "PlaySoundW", 1},
    {L"winmm.dll", "PlaySoundA", 1},
    {L"winmm.dll", "sndPlaySoundW", 1},
    {L"winmm.dll", "sndPlaySoundA", 1},
    {L"winmm.dll", "mciSendStringW", 0},
    {L"winmm.dll", "mciSendStringA", 0},
    {L"winmm.dll", "waveOutOpen", 6},  // MMSYSERR_NODRIVER: no audio device is opened
    {L"kernel32.dll", "Beep", 1},
    {L"shell32.dll", "Shell_NotifyIconW", 1},
    {L"shell32.dll", "Shell_NotifyIconA", 1},
};

std::atomic<std::uint64_t> s_BlockedCalls{0};
std::atomic<std::size_t> s_ReplacedEntries{0};
std::mutex s_LogMutex;   // guards s_Log, the log writes, and dbghelp, which is not thread-safe
HANDLE s_Log = nullptr;  // set only while a blocked-call log is open
bool s_HasReportedLogWriteFailure = false;
bool s_IsSymbolsReady = false;

// Converts between UTF-8, which the log and standard error use, and UTF-16, which Windows paths and the
// debugger use. A failed conversion gives "?", so the message is still written.
std::string ToUtf8(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) return "?";
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                      nullptr);
  return out;
}

std::wstring ToWide(const std::string& text) {
  if (text.empty()) return std::wstring();
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return L"?";
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

std::string WindowsError(DWORD code) { return "Windows error " + std::to_string(code); }

std::string Hex(std::uint64_t value) {
  char text[32] = {};
  std::snprintf(text, sizeof text, "0x%llx", static_cast<unsigned long long>(value));
  return text;
}

// Writes to standard error, which ctest captures, and to the debugger. Neither takes s_LogMutex, so this
// can be called while that mutex is held. The write to standard error is best effort: nothing can report
// its failure, so a stopped process still fails through its exit code.
void Report(const std::string& message) {
  const std::string line = "TestSoundGuard: " + message + "\r\n";
  const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
  if (error != nullptr && error != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(error, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  }
  OutputDebugStringW(ToWide(line).c_str());
}

[[noreturn]] void StopProcess(const std::string& message) {
  Report(message + "; the process is stopped so that no sound can play unblocked");
  ExitProcess(kStartupFailureExitCode);
}

// Caller holds s_LogMutex.
void WriteLogLocked(const std::string& text) {
  if (s_Log == nullptr) return;
  DWORD written = 0;
  if (!WriteFile(s_Log, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
      !s_HasReportedLogWriteFailure) {
    s_HasReportedLogWriteFailure = true;
    Report("cannot write the blocked-call log (" + WindowsError(GetLastError()) +
           "); further write failures are not reported");
  }
}

void LogLine(const std::string& text) {
  std::lock_guard<std::mutex> lock(s_LogMutex);
  WriteLogLocked(text);
}

// Caller holds s_LogMutex. A failed CloseHandle is reported; the log counts as closed either way.
void CloseLogLocked() {
  if (s_Log == nullptr) return;
  if (!CloseHandle(s_Log)) {
    Report("cannot close the blocked-call log (" + WindowsError(GetLastError()) + ")");
  }
  s_Log = nullptr;
}

// "module!symbol+0xoffset" when the PDB is available, otherwise "module+0xrva". Caller holds s_LogMutex.
std::string DescribeFrame(void* address) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCWSTR>(address), &module)) {
    return "unknown";
  }
  std::wstring fullPath(kMaxWindowsPathLength, L'\0');
  const DWORD length = GetModuleFileNameW(module, fullPath.data(), kMaxWindowsPathLength);
  fullPath.resize(length);
  const std::size_t slash = fullPath.find_last_of(L'\\');
  const std::string fileName =
      length == 0 ? "?" : ToUtf8(slash == std::wstring::npos ? fullPath : fullPath.substr(slash + 1));

  const auto addr = reinterpret_cast<std::uintptr_t>(address);
  alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  if (s_IsSymbolsReady && SymFromAddr(GetCurrentProcess(), static_cast<DWORD64>(addr), &displacement, symbol)) {
    return fileName + "!" + symbol->Name + "+" + Hex(displacement);
  }
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  return fileName + "+" + Hex(addr - base);
}

__declspec(noinline) void RecordBlockedCall(std::size_t index) {
  s_BlockedCalls.fetch_add(1, std::memory_order_relaxed);
  void* frames[16] = {};
  // Skipping this function leaves frames[0] = the replacement; frames[1] onward is the caller chain.
  const USHORT captured = CaptureStackBackTrace(1, 16, frames, nullptr);
  std::lock_guard<std::mutex> lock(s_LogMutex);
  if (s_Log == nullptr) return;
  std::string text = std::string(kBlocked[index].name) + "\r\n";
  for (USHORT i = 1; i < captured; ++i) text += "    " + DescribeFrame(frames[i]) + "\r\n";
  WriteLogLocked(text);
}

}  // namespace

// Overwrites the start of `entryPoint` with: mov rax, <replacement>; jmp rax (12 bytes). RAX is not an
// argument register on x64, so using it changes nothing for the caller. Every module that calls the
// entry point reaches the replacement, because they all jump through the same code. Returns false, with
// nothing written, when the memory cannot be made writable.
bool PatchEntryPoint(void* entryPoint, void* replacement) {
  static_assert(sizeof(void*) == 8, "the patch is an x64 mov rax, imm64 followed by jmp rax");
  std::uint8_t jump[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
  const auto address = reinterpret_cast<std::uintptr_t>(replacement);
  std::memcpy(jump + 2, &address, sizeof address);
  auto* code = static_cast<std::uint8_t*>(entryPoint);
  DWORD oldProtect = 0;
  if (!VirtualProtect(code, sizeof jump, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
  std::memcpy(code, jump, sizeof jump);
  DWORD protectBeforeRestore = 0;
  if (!VirtualProtect(code, sizeof jump, oldProtect, &protectBeforeRestore)) {
    // The replacement is already in place, so this only leaves the page writable.
    Report("cannot restore the protection of a replaced entry point (" + WindowsError(GetLastError()) + ")");
  }
  if (!FlushInstructionCache(GetCurrentProcess(), code, sizeof jump)) {
    Report("cannot flush the instruction cache (" + WindowsError(GetLastError()) + ")");
  }
  return true;
}

bool OpenBlockedCallLog(const std::wstring& path) {
  std::lock_guard<std::mutex> lock(s_LogMutex);
  const HANDLE log = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log == INVALID_HANDLE_VALUE) {
    // The log that was open, if any, stays open: a failed attempt must not stop diagnostics.
    Report("cannot open the blocked-call log " + ToUtf8(path) + " (" + WindowsError(GetLastError()) +
           "); sounds are still blocked");
    return false;
  }
  CloseLogLocked();
  s_Log = log;
  if (!s_IsSymbolsReady) {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    s_IsSymbolsReady = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    if (!s_IsSymbolsReady) {
      Report("symbols are unavailable (" + WindowsError(GetLastError()) + "); call stacks show module offsets");
    }
  }
  return true;
}

void CloseBlockedCallLog() {
  std::lock_guard<std::mutex> lock(s_LogMutex);
  CloseLogLocked();
}

std::uint64_t BlockedCallCount() { return s_BlockedCalls.load(); }

bool AllEntryPointsReplaced() { return s_ReplacedEntries.load() == std::size(kBlocked); }

namespace {

// One replacement per entry in kBlocked. Each has its own address, so the caller chain is its own too.
template <std::size_t Index>
__declspec(noinline) std::uint32_t BlockedHandler() {
  RecordBlockedCall(Index);
  return kBlocked[Index].result;
}

template <std::size_t... Index>
std::array<void*, sizeof...(Index)> HandlerTable(std::index_sequence<Index...>) {
  return {{reinterpret_cast<void*>(&BlockedHandler<Index>)...}};
}

// The log is diagnostics only. A setting that cannot be used is reported, and sounds stay blocked.
void OpenLogFromEnvironment() {
  constexpr wchar_t kVariable[] = L"GOSURVEY_BLOCKED_SOUND_LOG";
  const DWORD size = GetEnvironmentVariableW(kVariable, nullptr, 0);
  if (size == 0) {
    const DWORD error = GetLastError();
    if (error != ERROR_ENVVAR_NOT_FOUND) {
      Report("cannot read GOSURVEY_BLOCKED_SOUND_LOG (" + WindowsError(error) + ")");
    }
    return;  // not set: no log
  }
  if (size <= 1) return;  // set but empty: no log
  std::wstring path(size, L'\0');
  const DWORD length = GetEnvironmentVariableW(kVariable, path.data(), size);
  if (length == 0 || length >= size) {
    Report("cannot read GOSURVEY_BLOCKED_SOUND_LOG (" + WindowsError(GetLastError()) + ")");
    return;
  }
  path.resize(length);
  // OpenBlockedCallLog reports a log that cannot be opened. Sounds stay blocked either way.
  OpenBlockedCallLog(path);
}

void Install() {
  OpenLogFromEnvironment();
  const auto replacements = HandlerTable(std::make_index_sequence<std::size(kBlocked)>{});
  for (std::size_t i = 0; i < std::size(kBlocked); ++i) {
    const BlockedEntry& entry = kBlocked[i];
    const std::string name = entry.name;
    HMODULE module = LoadLibraryW(entry.module);
    if (module == nullptr) {
      StopProcess("cannot load " + ToUtf8(std::wstring(entry.module)) + " to replace " + name + " (" +
                  WindowsError(GetLastError()) + ")");
    }
    FARPROC target = GetProcAddress(module, entry.name);
    if (target == nullptr) {
      StopProcess("cannot find " + name + " (" + WindowsError(GetLastError()) + ")");
    }
    if (!PatchEntryPoint(reinterpret_cast<void*>(target), replacements[i])) {
      StopProcess("cannot replace " + name + " (" + WindowsError(GetLastError()) + ")");
    }
    s_ReplacedEntries.fetch_add(1, std::memory_order_relaxed);
    LogLine("replaced " + name + "\r\n");
  }
}

struct Installer {
  Installer() { Install(); }
};

// Constructed before main() in every executable that links this file.
const Installer kInstaller;

}  // namespace

}  // namespace testsound
