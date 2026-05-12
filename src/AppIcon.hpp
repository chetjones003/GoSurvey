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

/// Directory containing the executable (Windows), or empty if unknown.
std::filesystem::path AppExecutableDirectory();

/// Tries `<exe>/relativePath` then `<cwd>/relativePath`. \p relativePath may include subdirs (e.g. `icons/logo.png`).
std::filesystem::path ResolveBundledAssetPath(const std::filesystem::path& relativePath);
