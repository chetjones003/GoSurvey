#pragma once
// Public IO seam for raster image probing and sheet/model image construction.

#include <string>

struct CadRasterImage;

bool ProbeRasterImage(const std::string& path, int* pixelW, int* pixelH, std::string* why);
bool MakeSheetRasterImage(const std::string& path, float insX, float insY, float width, float height,
                          float rotationRad, CadRasterImage* out, std::string* why);
