#include "gridsurface.hpp"
#include "surfacequery.hpp"

#include <cmath>
#include <limits>

bool GridIndexValid(int cols, int rows, const std::vector<float>& z) {
  if (cols < 2 || rows < 2)
    return false;
  return z.size() == static_cast<size_t>(cols) * static_cast<size_t>(rows);
}

bool GridElevationAt(double originX, double originY, double spacingX, double spacingY, int cols, int rows,
                     const std::vector<float>& z, double x, double y, double* outZ) {
  const GridSurfaceQuery q(originX, originY, spacingX, spacingY, cols, rows, z);
  return q.elevationAt(x, y, outZ);
}

void GridBuildDisplayTin(double originX, double originY, double spacingX, double spacingY, int cols, int rows,
                         const std::vector<float>& z, TinBuildResult* out) {
  if (!out)
    return;
  *out = {};
  if (!GridIndexValid(cols, rows, z) || spacingX <= 0.0 || spacingY <= 0.0) {
    out->message = "Grid is not 2×2 or larger, or spacing is not positive.";
    return;
  }
  const int n = cols * rows;
  out->vertsXyz.resize(static_cast<size_t>(n) * 3);
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < cols; ++i) {
      const int vi = j * cols + i;
      out->vertsXyz[static_cast<size_t>(vi) * 3 + 0] = static_cast<float>(originX + static_cast<double>(i) * spacingX);
      out->vertsXyz[static_cast<size_t>(vi) * 3 + 1] = static_cast<float>(originY + static_cast<double>(j) * spacingY);
      out->vertsXyz[static_cast<size_t>(vi) * 3 + 2] = z[static_cast<size_t>(vi)];
    }
  }
  out->indices.reserve(static_cast<size_t>(cols - 1) * static_cast<size_t>(rows - 1) * 6);
  for (int j = 0; j < rows - 1; ++j) {
    for (int i = 0; i < cols - 1; ++i) {
      const std::uint32_t a = static_cast<std::uint32_t>(j * cols + i);
      const std::uint32_t b = a + 1;
      const std::uint32_t c = static_cast<std::uint32_t>((j + 1) * cols + i);
      const std::uint32_t d = c + 1;
      out->indices.push_back(a);
      out->indices.push_back(b);
      out->indices.push_back(d);
      out->indices.push_back(a);
      out->indices.push_back(d);
      out->indices.push_back(c);
    }
  }
  out->status = TinBuildStatus::Ok;
  out->message = "Grid display TIN.";
}

bool GridVolumeSubtract(double originX, double originY, double spacingX, double spacingY, int cols, int rows,
                        const std::vector<float>& baseZ, const std::vector<float>& compZ, std::vector<float>* outZ,
                        std::string* why) {
  (void)originX;
  (void)originY;
  (void)spacingX;
  (void)spacingY;
  if (!outZ)
    return false;
  outZ->clear();
  if (!GridIndexValid(cols, rows, baseZ) || !GridIndexValid(cols, rows, compZ) || baseZ.size() != compZ.size()) {
    if (why)
      *why = "grid-volume parents must be grids with the same node layout";
    return false;
  }
  outZ->resize(baseZ.size());
  int overlap = 0;
  for (size_t i = 0; i < baseZ.size(); ++i) {
    const float a = baseZ[i];
    const float b = compZ[i];
    if (std::isfinite(a) && std::isfinite(b)) {
      (*outZ)[i] = b - a;
      ++overlap;
    } else {
      (*outZ)[i] = std::numeric_limits<float>::quiet_NaN();
    }
  }
  if (overlap == 0) {
    if (why)
      *why = "grid-volume has no overlapping nodes";
    outZ->clear();
    return false;
  }
  return true;
}
