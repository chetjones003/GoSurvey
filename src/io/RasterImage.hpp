#pragma once

#include "commands/PaperSpace.hpp"

#include <string>

/// REQ-457 / REQ-173 / issue #1010: check that a JPEG, PNG or BMP can be fully decoded. False when the file is
/// missing, truncated or not an image; \p why then says why. \p pixelW and \p pixelH receive the pixel size.
bool ProbeRasterImage(const std::string& path, int* pixelW, int* pixelH, std::string* why);

/// REQ-457: a sheet image placed in paper inches from a file that ProbeRasterImage accepts. False, and
/// \p out untouched, when the file is refused or the size is not positive.
bool MakeSheetRasterImage(const std::string& path, float insX, float insY, float width, float height,
                          float rotationRad, CadRasterImage* out, std::string* why);
