#include "GpuPreference.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace platform {
namespace {

constexpr const wchar_t* kKey = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

/// The full path of the running executable — the value name Windows keys the preference by.
///
/// Must be the same string Windows itself would write, so the Settings page and this code edit one
/// setting rather than two that disagree. `GetModuleFileNameW(nullptr, …)` returns exactly that.
std::wstring ThisExePath() {
  std::wstring buf(512, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0)
      return std::wstring();
    if (n < buf.size()) {
      buf.resize(n);
      return buf;
    }
    if (buf.size() > 32768)  // far past MAX_PATH's long-path ceiling; something is wrong
      return std::wstring();
    buf.resize(buf.size() * 2);
  }
}

std::string LastErrorText(const char* what) {
  return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

} // namespace

GpuPreference ReadGpuPreferenceForThisExe() {
  const std::wstring exe = ThisExePath();
  if (exe.empty())
    return GpuPreference::SystemDefault;

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    return GpuPreference::SystemDefault;

  wchar_t data[128] = {};
  DWORD bytes = sizeof(data) - sizeof(wchar_t);  // leave room to terminate a non-terminated value
  DWORD type = 0;
  const LSTATUS st = RegQueryValueExW(key, exe.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(data), &bytes);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || type != REG_SZ)
    return GpuPreference::SystemDefault;

  // The value is a semicolon-separated property list, e.g. "GpuPreference=2;". Parsed by search
  // rather than by assuming the layout: Windows has added other properties to it over time
  // (AppStatus, for one — an existing entry on the reference machine carries exactly that), and a
  // parser that assumed one property would misread them.
  const std::wstring value(data, wcsnlen(data, sizeof(data) / sizeof(wchar_t)));
  const size_t at = value.find(L"GpuPreference=");
  if (at == std::wstring::npos)
    return GpuPreference::SystemDefault;
  switch (value[at + 14]) {
    case L'1': return GpuPreference::PowerSaving;
    case L'2': return GpuPreference::HighPerformance;
    default:   return GpuPreference::SystemDefault;
  }
}

bool SetGpuPreferenceForThisExe(GpuPreference pref, std::string* error) {
  const std::wstring exe = ThisExePath();
  if (exe.empty()) {
    if (error) *error = LastErrorText("Reading the executable path");
    return false;
  }

  HKEY key = nullptr;
  DWORD disposition = 0;
  LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                               KEY_SET_VALUE, nullptr, &key, &disposition);
  if (st != ERROR_SUCCESS) {
    if (error) *error = "Opening the graphics preference key failed (Windows error " + std::to_string(st) + ")";
    return false;
  }

  if (pref == GpuPreference::SystemDefault) {
    // Remove the value rather than writing "GpuPreference=0;": absent is how Windows spells "let
    // Windows decide", and a leftover value would keep this application listed on the Settings
    // page as though it had an opinion.
    st = RegDeleteValueW(key, exe.c_str());
    RegCloseKey(key);
    if (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND)
      return true;
    if (error) *error = "Clearing the graphics preference failed (Windows error " + std::to_string(st) + ")";
    return false;
  }

  const std::wstring data = (pref == GpuPreference::PowerSaving) ? L"GpuPreference=1;" : L"GpuPreference=2;";
  st = RegSetValueExW(key, exe.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                      static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  if (st != ERROR_SUCCESS) {
    if (error) *error = "Saving the graphics preference failed (Windows error " + std::to_string(st) + ")";
    return false;
  }
  return true;
}

} // namespace platform

#else

namespace platform {
GpuPreference ReadGpuPreferenceForThisExe() { return GpuPreference::SystemDefault; }
bool SetGpuPreferenceForThisExe(GpuPreference, std::string*) { return true; }
} // namespace platform

#endif
