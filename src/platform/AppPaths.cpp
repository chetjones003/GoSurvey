#include "AppPaths.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <cstdlib>
#else
#include <unistd.h>
#include <limits.h>
#include <cstdlib>
#endif

// Moved verbatim from AppIcon.cpp by TASK-056 — see AppPaths.hpp for why. No behaviour change:
// the bodies below are the originals, and AppIcon.cpp no longer defines them.

std::filesystem::path AppExecutableDirectory() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (!n || n >= MAX_PATH)
    return {};
  return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
  char buf[PATH_MAX];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0)
    return {};
  return std::filesystem::path(buf).parent_path();
#else
  char selfPath[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
  if (len <= 0)
    return {};
  selfPath[len] = '\0';
  return std::filesystem::path(selfPath).parent_path();
#endif
}

std::filesystem::path UserDataDirectory() {
#ifdef _WIN32
  wchar_t appdata[MAX_PATH];
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0)
    return std::filesystem::path(appdata) / "GoSurvey";
#elif defined(__APPLE__)
  if (const char* home = getenv("HOME"))
    return std::filesystem::path(home) / "Library" / "Application Support" / "GoSurvey";
#else
  if (const char* xdg = getenv("XDG_CONFIG_HOME"); xdg && xdg[0])
    return std::filesystem::path(xdg) / "GoSurvey";
  if (const char* home = getenv("HOME"))
    return std::filesystem::path(home) / ".config" / "GoSurvey";
#endif
  return {};
}

std::filesystem::path ResolveBundledAssetPath(const std::filesystem::path& relativePath) {
  namespace fs = std::filesystem;
  const fs::path exeDir = AppExecutableDirectory();
  if (!exeDir.empty()) {
    fs::path p = exeDir / relativePath;
    if (fs::exists(p))
      return p;
  }
  fs::path p = fs::current_path() / relativePath;
  if (fs::exists(p))
    return p;
  return {};
}

std::filesystem::path ResolveAppLogoPngPath() {
  namespace fs = std::filesystem;
  if (fs::path p = ResolveBundledAssetPath(fs::path("resources") / "icons" / "app.png"); !p.empty())
    return p;
  if (fs::path p = ResolveBundledAssetPath(fs::path("icons") / "app.png"); !p.empty())
    return p;
  if (fs::path p = ResolveBundledAssetPath(fs::path("app.png")); !p.empty())
    return p;
  return {};
}

std::filesystem::path ResolveDefaultWorkspaceTemplateGsPath() {
  return ResolveBundledAssetPath(std::filesystem::path("resources") / "default-template.gs");
}
