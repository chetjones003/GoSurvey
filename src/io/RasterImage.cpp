#include "RasterImage.hpp"

#include <stb_image.h>

bool ProbeRasterImage(const std::string& path, int* pixelW, int* pixelH, std::string* why) {
  int w = 0;
  int h = 0;
  int channels = 0;
  unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
  if (pixels == nullptr) {
    if (why != nullptr)
      *why = stbi_failure_reason() != nullptr ? stbi_failure_reason() : "not an image";
    return false;
  }
  stbi_image_free(pixels);
  if (pixelW != nullptr)
    *pixelW = w;
  if (pixelH != nullptr)
    *pixelH = h;
  return true;
}

bool MakeSheetRasterImage(const std::string& path, float insX, float insY, float width, float height,
                          float rotationRad, CadRasterImage* out, std::string* why) {
  if (!(width > 0.f) || !(height > 0.f)) {
    if (why != nullptr)
      *why = "image size must be positive";
    return false;
  }
  CadRasterImage im;
  if (!ProbeRasterImage(path, &im.pixelW, &im.pixelH, why))
    return false;
  im.path = path;
  im.insX = insX;
  im.insY = insY;
  im.width = width;
  im.height = height;
  im.rotationRad = rotationRad;
  if (out != nullptr)
    *out = im;
  return true;
}
