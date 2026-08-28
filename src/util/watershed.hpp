#pragma once

/// Watershed / water-drop / catchment on a TIN (REQ-132…134 / ADR-039 (j)).
///
/// Pure — triangulation arrays only — so Catch2 links it without GL, beside tinbuild / surfacestats.
/// Display geometry is derived here as GL_LINES buffers; it is never stored in `.gs`.

#include <cstdint>
#include <string>
#include <vector>

enum class DrainKind : std::uint8_t { Boundary, Depression, Flat };

struct WatershedBasin {
  int id = 0;
  DrainKind drain = DrainKind::Boundary;
  double area2d = 0.0;
  double drainX = 0.0, drainY = 0.0, drainZ = 0.0;
};

struct WatershedResult {
  bool ok = false;  ///< False for a null / empty TIN; \ref error names the refusal.
  std::string error;
  std::vector<int> triangleBasinId;
  std::vector<int> successor;  ///< Neighbour triangle ordinal, or -1 at a drain.
  std::vector<WatershedBasin> basins;
};

struct WaterDropResult {
  bool ok = false;
  bool outside = false;
  DrainKind terminal = DrainKind::Boundary;
  std::vector<float> pathXyz;  ///< Interleaved x,y,z along the fall line.
};

struct CatchmentResult {
  bool ok = false;
  bool outside = false;
  double area2d = 0.0;
  double minZ = 0.0, maxZ = 0.0;
  std::vector<int> triangleIds;
};

/// Drain graph and basins. Empty indices → \c ok false, error "null TIN".
[[nodiscard]] WatershedResult ComputeWatershed(const std::vector<float>& vertsXyz,
                                               const std::vector<std::uint32_t>& indices);

/// Downhill walk from plan (\p x, \p y) until a REQ-132 drain. Outside → \c outside true, empty path.
[[nodiscard]] WaterDropResult ComputeWaterDrop(const std::vector<float>& vertsXyz,
                                               const std::vector<std::uint32_t>& indices, double x, double y);

/// Reverse-flow set of every triangle covering (\p x, \p y). Shared-edge (ridge) picks union both.
[[nodiscard]] CatchmentResult ComputeCatchment(const std::vector<float>& vertsXyz,
                                               const std::vector<std::uint32_t>& indices, double x, double y);

/// Unique edges between different basin ids, plus remaining border edges. `GL_LINES` (six floats/seg).
void AppendWatershedBasinOutlines(const WatershedResult& w, const std::vector<float>& vertsXyz,
                                  const std::vector<std::uint32_t>& indices, std::vector<float>* out);

/// Border of the catchment triangle set. `GL_LINES`.
void AppendCatchmentBoundary(const CatchmentResult& c, const std::vector<float>& vertsXyz,
                             const std::vector<std::uint32_t>& indices, std::vector<float>* out);

/// Path as `GL_LINES` segments (skip if fewer than two vertices).
void AppendPathAsLines(const std::vector<float>& pathXyz, std::vector<float>* out);
