#pragma once

/// REQ-136: a TIN volume surface — comparison elevation minus base elevation at shared plan
/// positions, triangulated as an ordinary TIN. Pure and GL-free (ADR-028 (c), ADR-039 (a)).

#include "tinbuild.hpp"

#include <cstdint>
#include <vector>

/// Builds a difference TIN from two triangulations in the **same local storage frame**.
///
/// Each unique plan vertex of either surface that is covered by **both** triangulations becomes
/// a point whose Z is `zComparison - zBase` (Civil 3D's volume-surface convention: positive is
/// fill). The result is unconstrained Delaunay of those points (world XY if \p originX/\p originY
/// are the document origin — same as \ref BuildTin callers).
///
/// An empty result is `TooFewPoints` when fewer than three overlapping samples exist — never a
/// crash, never a silent zero TIN.
[[nodiscard]] TinBuildResult BuildTinVolumeSurface(const std::vector<float>& baseVertsXyz,
                                                   const std::vector<std::uint32_t>& baseIndices,
                                                   const std::vector<float>& comparisonVertsXyz,
                                                   const std::vector<std::uint32_t>& comparisonIndices,
                                                   double originX, double originY);
