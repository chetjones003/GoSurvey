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

// AppExecutableDirectory / UserDataDirectory / ResolveBundledAssetPath / ResolveAppLogoPngPath /
// ResolveDefaultWorkspaceTemplateGsPath moved to platform/AppPaths.cpp (TASK-056). They are pure
// path logic and were only here by accident of first use; this file pulls in GLEW and GLFW, which
// made "where is %APPDATA%?" depend on a GPU. AppIcon.hpp includes AppPaths.hpp, so callers of this
// header are unaffected.

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

unsigned int LoadIconTextureRgba(const std::filesystem::path& pngPath, int* outW, int* outH) {
  const std::string pathUtf8 = pngPath.u8string();
  int w = 0, h = 0, ch = 0;
  stbi_uc* rgba = stbi_load(pathUtf8.c_str(), &w, &h, &ch, 4);
  if (!rgba || w <= 0 || h <= 0) {
    if (rgba) stbi_image_free(rgba);
    return 0;
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex) { stbi_image_free(rgba); return 0; }
  glBindTexture(GL_TEXTURE_2D, tex);
  // These PNGs are authored large (128-150 px) and drawn small (16-32 px in the toolspace tree,
  // ribbon, palettes). A plain GL_LINEAR minify takes only 4 taps and drops the thin line art;
  // a mipmap chain gives a properly box-filtered downscale so small icons stay legible.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);  // top-down, no flip
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(rgba);
  if (outW) *outW = w;
  if (outH) *outH = h;
  return static_cast<unsigned int>(tex);
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
