// Test-only: makes a test executable unable to play Windows sounds.
//
// Windows plays program sounds for the signed-in user session, not for a desktop. So a test that calls
// MessageBeep (Qt's QApplication::beep does), a MessageBox with an icon, PlaySound, Beep, MCI, or a
// balloon through Shell_NotifyIcon makes a sound even when it runs on a hidden desktop. Linking this
// file into a test executable replaces those entry points once, when the process starts. Each
// replacement counts the call and returns success without playing anything. The change is limited to
// this process: other programs keep their sounds, and the registry and sound scheme are not touched.
//
// Set GOSURVEY_BLOCKED_SOUND_LOG to a file path to also write every blocked call there, with the call
// stack that made it. That shows what tried to make a sound, and where.

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

std::atomic<std::uint64_t> gBlockedCalls{0};
std::mutex gLogMutex;      // the log file and dbghelp are used from one thread at a time
HANDLE gLog = nullptr;     // set only when GOSURVEY_BLOCKED_SOUND_LOG names a file

void WriteLogText(const std::string& text) {
  DWORD written = 0;
  WriteFile(gLog, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// "module!symbol+0x offset" when the PDB is available, otherwise "module+0x rva".
std::string DescribeFrame(void* address) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCWSTR>(address), &module)) {
    return "unknown";
  }
  char fullPath[MAX_PATH] = {};
  GetModuleFileNameA(module, fullPath, MAX_PATH);
  const char* fileName = std::strrchr(fullPath, '\\');
  fileName = fileName ? fileName + 1 : fullPath;

  const auto addr = reinterpret_cast<std::uintptr_t>(address);
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  char text[512] = {};
  if (SymFromAddr(GetCurrentProcess(), static_cast<DWORD64>(addr), &displacement, symbol)) {
    std::snprintf(text, sizeof text, "%s!%s+0x%llx", fileName, symbol->Name,
                  static_cast<unsigned long long>(displacement));
  } else {
    std::snprintf(text, sizeof text, "%s+0x%llx", fileName, static_cast<unsigned long long>(addr - base));
  }
  return text;
}

__declspec(noinline) void RecordBlockedCall(std::size_t index) {
  gBlockedCalls.fetch_add(1, std::memory_order_relaxed);
  if (gLog == nullptr) return;
  void* frames[16] = {};
  // Skipping this function leaves frames[0] = the replacement; frames[1] onward is the caller chain.
  const USHORT captured = CaptureStackBackTrace(1, 16, frames, nullptr);
  std::lock_guard<std::mutex> lock(gLogMutex);
  std::string text = std::string(kBlocked[index].name) + "\r\n";
  for (USHORT i = 1; i < captured; ++i) text += "    " + DescribeFrame(frames[i]) + "\r\n";
  WriteLogText(text);
}

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

// Overwrites the start of `target` with: mov rax, <replacement>; jmp rax (12 bytes). RAX is not an
// argument register on x64, so using it here changes nothing for the caller. Every module that calls
// the function then reaches the replacement, because they all jump to the same code.
void Redirect(void* target, void* replacement) {
  std::uint8_t jump[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
  const auto address = reinterpret_cast<std::uintptr_t>(replacement);
  std::memcpy(jump + 2, &address, sizeof address);
  auto* code = static_cast<std::uint8_t*>(target);
  DWORD oldProtect = 0;
  if (!VirtualProtect(code, sizeof jump, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
  std::memcpy(code, jump, sizeof jump);
  DWORD ignored = 0;
  VirtualProtect(code, sizeof jump, oldProtect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), code, sizeof jump);
}

void OpenLog() {
  char path[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableA("GOSURVEY_BLOCKED_SOUND_LOG", path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return;
  const HANDLE log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log == INVALID_HANDLE_VALUE) return;
  gLog = log;
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
}

void Install() {
  OpenLog();
  const auto replacements = HandlerTable(std::make_index_sequence<std::size(kBlocked)>{});
  for (std::size_t i = 0; i < std::size(kBlocked); ++i) {
    const BlockedEntry& entry = kBlocked[i];
    HMODULE module = LoadLibraryW(entry.module);
    FARPROC target = module ? GetProcAddress(module, entry.name) : nullptr;
    if (target) Redirect(reinterpret_cast<void*>(target), replacements[i]);
    if (gLog != nullptr) {
      std::lock_guard<std::mutex> lock(gLogMutex);
      WriteLogText(std::string(target ? "replaced " : "not found ") + entry.name + "\r\n");
    }
  }
}

struct Installer {
  Installer() { Install(); }
};

// Constructed before main() in every executable that links this file.
const Installer kInstaller;

}  // namespace

std::uint64_t BlockedCallCount() { return gBlockedCalls.load(); }

}  // namespace testsound
