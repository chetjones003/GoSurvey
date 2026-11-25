#include <GoSurvey/Renderer/ViewportRenderer.hpp>

#include <GL/glew.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

bool ViewportRenderer::CaptureThumbnailBmp(const char* pathUtf8, int maxDim) const {
  if (!pathUtf8 || !pathUtf8[0] || fbo_ == 0 || viewW_ < 4 || viewH_ < 4 || maxDim < 8)
    return false;

  const int srcW = viewW_;
  const int srcH = viewH_;
  std::vector<unsigned char> src(static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 4u);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

  // Target size: preserve aspect, longer side == maxDim (never upscale).
  double scale = static_cast<double>(maxDim) / std::max(srcW, srcH);
  if (scale > 1.0)
    scale = 1.0;
  const int dstW = std::max(1, static_cast<int>(srcW * scale));
  const int dstH = std::max(1, static_cast<int>(srcH * scale));

  // Bottom-up 24-bit BGR rows, DWORD-aligned — the exact layout a Windows BMP stores, and the same
  // bottom-up order glReadPixels already returns, so no vertical flip is needed.
  const int rowB = (dstW * 3 + 3) & ~3;
  std::vector<unsigned char> bmp(static_cast<size_t>(rowB) * static_cast<size_t>(dstH), 0);
  for (int dy = 0; dy < dstH; ++dy) {
    const int sy = std::min(srcH - 1, static_cast<int>(dy / scale));
    unsigned char* dstRow = bmp.data() + static_cast<size_t>(dy) * rowB;
    for (int dx = 0; dx < dstW; ++dx) {
      const int sx = std::min(srcW - 1, static_cast<int>(dx / scale));
      const unsigned char* s = src.data() + (static_cast<size_t>(sy) * srcW + sx) * 4u;
      dstRow[dx * 3 + 0] = s[2];  // B
      dstRow[dx * 3 + 1] = s[1];  // G
      dstRow[dx * 3 + 2] = s[0];  // R
    }
  }

  std::FILE* f = nullptr;
#if defined(_WIN32)
  if (fopen_s(&f, pathUtf8, "wb") != 0)
    f = nullptr;
#else
  f = std::fopen(pathUtf8, "wb");
#endif
  if (!f)
    return false;

  const unsigned int dataSz = static_cast<unsigned int>(bmp.size());
  const unsigned int fileSz = 54u + dataSz;
  unsigned char hdr[54] = {};
  hdr[0] = 'B';
  hdr[1] = 'M';
  std::memcpy(&hdr[2], &fileSz, 4);
  hdr[10] = 54;
  hdr[14] = 40;
  const int w = dstW, h = dstH;
  std::memcpy(&hdr[18], &w, 4);
  std::memcpy(&hdr[22], &h, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  std::memcpy(&hdr[34], &dataSz, 4);
  const bool ok = std::fwrite(hdr, 1, 54, f) == 54 &&
                  std::fwrite(bmp.data(), 1, bmp.size(), f) == bmp.size();
  std::fclose(f);
  return ok;
}
