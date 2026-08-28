#pragma once

/// Elevation / slope / aspect queries shared by TIN interpolation and grid bilinear (REQ-137).
///
/// Pure util: no document types. Callers that have a `CadSurface` construct the matching
/// implementation. Two concrete classes satisfy REQ-301.

#include "surfacevolume.hpp"

#include <cstdint>
#include <vector>

class ISurfaceQuery {
public:
  virtual ~ISurfaceQuery() = default;
  [[nodiscard]] virtual bool elevationAt(double x, double y, double* outZ) const = 0;
  [[nodiscard]] virtual bool slopePercentAt(double x, double y, double* outPct) const = 0;
  [[nodiscard]] virtual bool slopeAngleDegAt(double x, double y, double* outDeg) const = 0;
  [[nodiscard]] virtual bool aspectDegAt(double x, double y, double* outDeg) const = 0;
};

class TinSurfaceQuery final : public ISurfaceQuery {
public:
  TinSurfaceQuery(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices);
  [[nodiscard]] bool elevationAt(double x, double y, double* outZ) const override;
  [[nodiscard]] bool slopePercentAt(double x, double y, double* outPct) const override;
  [[nodiscard]] bool slopeAngleDegAt(double x, double y, double* outDeg) const override;
  [[nodiscard]] bool aspectDegAt(double x, double y, double* outDeg) const override;

private:
  const std::vector<float>* verts_ = nullptr;
  const std::vector<std::uint32_t>* indices_ = nullptr;
  TinSpatialIndex index_;
  [[nodiscard]] bool triangleAt(double x, double y, double* outZ, size_t* outTri) const;
};

class GridSurfaceQuery final : public ISurfaceQuery {
public:
  GridSurfaceQuery(double originX, double originY, double spacingX, double spacingY, int cols, int rows,
                   const std::vector<float>& z);
  [[nodiscard]] bool elevationAt(double x, double y, double* outZ) const override;
  [[nodiscard]] bool slopePercentAt(double x, double y, double* outPct) const override;
  [[nodiscard]] bool slopeAngleDegAt(double x, double y, double* outDeg) const override;
  [[nodiscard]] bool aspectDegAt(double x, double y, double* outDeg) const override;

private:
  double originX_ = 0.0, originY_ = 0.0, spacingX_ = 1.0, spacingY_ = 1.0;
  int cols_ = 0, rows_ = 0;
  const std::vector<float>* z_ = nullptr;
  [[nodiscard]] bool sampleCell(double x, double y, double* z00, double* z10, double* z01, double* z11,
                                double* fx, double* fy) const;
};
