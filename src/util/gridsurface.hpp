#pragma once

/// Grid storage helpers and display-TIN tessellation (REQ-137).

#include <cstdint>
#include <string>
#include <vector>

#include "tinbuild.hpp"

[[nodiscard]] bool GridIndexValid(int cols, int rows, const std::vector<float>& z);

/// Bilinear elevation. False when the point is outside the grid rectangle or the grid is malformed.
[[nodiscard]] bool GridElevationAt(double originX, double originY, double spacingX, double spacingY, int cols,
                                   int rows, const std::vector<float>& z, double x, double y, double* outZ);

/// Two triangles per cell. Empty result when the grid is smaller than 2×2 or Z is the wrong length.
void GridBuildDisplayTin(double originX, double originY, double spacingX, double spacingY, int cols, int rows,
                         const std::vector<float>& z, TinBuildResult* out);

/// Comparison minus base at shared nodes. False when layouts disagree or no pair of finite Z values
/// overlap.
[[nodiscard]] bool GridVolumeSubtract(double originX, double originY, double spacingX, double spacingY, int cols,
                                      int rows, const std::vector<float>& baseZ, const std::vector<float>& compZ,
                                      std::vector<float>* outZ, std::string* why);
