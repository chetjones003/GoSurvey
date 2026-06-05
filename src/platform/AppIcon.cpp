#include "AppIcon.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

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

static void FlipRgbaRowsTopToBottom(int w, int h, const unsigned char* src, unsigned char* dst) {
  const int rowBytes = w * 4;
  for (int y = 0; y < h; ++y) {
    const unsigned char* s = src + static_cast<size_t>(y) * rowBytes;
    unsigned char* d = dst + static_cast<size_t>(h - 1 - y) * rowBytes;
    std::memcpy(d, s, static_cast<size_t>(rowBytes));
  }
}

/// True if any pixel is not fully opaque — chroma-keying would fight real alpha.
static bool ImageUsesAlphaChannel(const stbi_uc* rgba, int w, int h) {
  const int n = w * h;
  for (int i = 0; i < n; ++i)
    if (rgba[static_cast<size_t>(i) * 4u + 3u] < 255)
      return true;
  return false;
}

/// Makes near-white backdrop transparent (straight alpha). Tuned for #fff / scan anti-alias fringes.
static void ApplyNearWhiteChromaKey(stbi_uc* rgba, int w, int h) {
  constexpr int kOpaqueBelow = 218; // min(R,G,B) at or below: leave pixel unchanged
  constexpr int kFullKey = 248;     // min(R,G,B) at or above: fully transparent
  const int span = (kFullKey - kOpaqueBelow) > 0 ? (kFullKey - kOpaqueBelow) : 1;
  const int n = w * h;
  for (int i = 0; i < n; ++i) {
    stbi_uc* p = rgba + static_cast<size_t>(i) * 4u;
    const int r = p[0], g = p[1], b = p[2];
    const int m = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (m >= kFullKey)
      p[3] = 0;
    else if (m > kOpaqueBelow) {
      const float f = static_cast<float>(m - kOpaqueBelow) / static_cast<float>(span);
      const int na = static_cast<int>(std::lround(static_cast<float>(p[3]) * (1.f - f)));
      p[3] = static_cast<stbi_uc>(na < 0 ? 0 : (na > 255 ? 255 : na));
    }
  }
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
  if (fs::path p = ResolveBundledAssetPath(fs::path("icons") / "bitmap.png"); !p.empty())
    return p;
  if (fs::path p = ResolveBundledAssetPath(fs::path("bitmap.png")); !p.empty())
    return p;
  return {};
}

std::filesystem::path ResolveDefaultWorkspaceTemplateGsPath() {
  return ResolveBundledAssetPath(std::filesystem::path("resources") / "default-template.gs");
}

static bool LoadPngToGpuTexture(const std::filesystem::path& pngPath, GLFWwindow* windowForIcon, AppLogoGpu* out,
                                bool keyNearWhiteBackground) {
  if (out) {
    out->texture = 0;
    out->width = 0;
    out->height = 0;
  }
  if (!out)
    return false;

  std::string pathUtf8 = pngPath.u8string();
  int w = 0, h = 0, ch = 0;
  stbi_uc* rgba = stbi_load(pathUtf8.c_str(), &w, &h, &ch, 4);
  if (!rgba || w <= 0 || h <= 0) {
    if (rgba)
      stbi_image_free(rgba);
    return false;
  }

  if (keyNearWhiteBackground && !ImageUsesAlphaChannel(rgba, w, h))
    ApplyNearWhiteChromaKey(rgba, w, h);

  if (windowForIcon) {
    GLFWimage icon{};
    icon.width = w;
    icon.height = h;
    icon.pixels = rgba;
    glfwSetWindowIcon(windowForIcon, 1, &icon);
  }

  std::vector<unsigned char> flipped(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
  FlipRgbaRowsTopToBottom(w, h, rgba, flipped.data());
  stbi_image_free(rgba);

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex)
    return false;
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  out->texture = tex;
  out->width = w;
  out->height = h;
  return true;
}

bool LoadAppLogoFromPngFile(GLFWwindow* window, const std::filesystem::path& pngPath, AppLogoGpu* out,
                            bool keyNearWhiteBackground) {
  if (!window)
    return false;
  return LoadPngToGpuTexture(pngPath, window, out, keyNearWhiteBackground);
}

bool LoadAppTextureFromPngFile(const std::filesystem::path& pngPath, AppLogoGpu* out, bool keyNearWhiteBackground) {
  return LoadPngToGpuTexture(pngPath, nullptr, out, keyNearWhiteBackground);
}

void DestroyAppLogoGpu(AppLogoGpu* io) {
  if (!io || !io->texture)
    return;
  GLuint t = static_cast<GLuint>(io->texture);
  glDeleteTextures(1, &t);
  io->texture = 0;
  io->width = 0;
  io->height = 0;
}
