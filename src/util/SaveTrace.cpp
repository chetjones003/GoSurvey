#include "SaveTrace.hpp"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void ClearSaveTrace() {
#if defined(_WIN32)
  wchar_t tmp[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, tmp) == 0)
    return;
  const std::wstring path = std::wstring(tmp) + L"gosurvey-save-trace.log";
  DeleteFileW(path.c_str());
#else
  // no-op on non-Windows builds
#endif
}

void AppendSaveTrace(const char* step) {
  if (step == nullptr)
    return;
#if defined(_WIN32)
  wchar_t tmp[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, tmp) == 0)
    return;
  const std::wstring path = std::wstring(tmp) + L"gosurvey-save-trace.log";
  std::FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"a") != 0 || f == nullptr)
    return;
  SYSTEMTIME st{};
  GetLocalTime(&st);
  std::fprintf(f, "%02u:%02u:%02u %s\n", st.wHour, st.wMinute, st.wSecond, step);
  std::fclose(f);
  OutputDebugStringA(("[save] " + std::string(step) + "\n").c_str());
#else
  (void)step;
#endif
}
