#pragma once

#include <filesystem>

struct GLFWwindow;

struct AppLogoGpu {
  unsigned int texture = 0;
  int width = 0;
  int height = 0;
};

/// Loads PNG (RGBA), sets the GLFW window/taskbar icon, and creates an OpenGL texture for ImGui.
/// When \p keyNearWhiteBackground is true, pixels close to white become transparent (for logos on white backdrops).
bool LoadAppLogoFromPngFile(GLFWwindow* window, const std::filesystem::path& pngPath, AppLogoGpu* out,
                            bool keyNearWhiteBackground = false);

/// Same GPU upload as \p LoadAppLogoFromPngFile but does not change the GLFW window icon (for large splash art).
bool LoadAppTextureFromPngFile(const std::filesystem::path& pngPath, AppLogoGpu* out,
                               bool keyNearWhiteBackground = false);

void DestroyAppLogoGpu(AppLogoGpu* io);

/// Loads an RGBA PNG into an OpenGL texture for ImGui WITHOUT flipping rows
/// (top-down, matching ImGui's uv(0,0)=top-left). Returns the GL texture name,
/// or 0 on failure. Intended for small UI icons. Requires a current GL context.
unsigned int LoadIconTextureRgba(const std::filesystem::path& pngPath, int* outW = nullptr, int* outH = nullptr);

/// Directory containing the executable (Windows), or empty if unknown.
std::filesystem::path AppExecutableDirectory();

/// Per-user application data directory (e.g. %APPDATA%\GoSurvey on Windows).
/// Returns an empty path if it cannot be determined.
std::filesystem::path UserDataDirectory();

/// Tries `<exe>/relativePath` then `<cwd>/relativePath`. \p relativePath may include subdirs (e.g. `icons/logo.png`).
std::filesystem::path ResolveBundledAssetPath(const std::filesystem::path& relativePath);

/// Bundled app logo for splash, title bar, and window icon: `icons/bitmap.png`, then `bitmap.png` beside the exe or cwd.
std::filesystem::path ResolveAppLogoPngPath();

/// Default startup workspace: `resources/default-template.gs` beside the executable (or cwd), from build copy.
[[nodiscard]] std::filesystem::path ResolveDefaultWorkspaceTemplateGsPath();
