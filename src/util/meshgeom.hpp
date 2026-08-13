#pragma once

#include <cstdint>
#include <utility>
#include <vector>

/// Pure geometry helpers for the REQ-063 mesh entity.
///
/// Dependency-free (only `<vector>`/`<cstdint>`/`<string>` here) so the test target links it, like
/// `curveintersect` and `benchscene`. The validation below is the part that has to be right before
/// a mesh reaches the GPU: an out-of-range index is an out-of-bounds read in the driver, which does
/// not fail politely.
namespace meshgeom {

struct Bounds {
  bool valid = false;
  float mnX = 0.f, mnY = 0.f, mnZ = 0.f;
  float mxX = 0.f, mxY = 0.f, mxZ = 0.f;
};

/// Axis-aligned bounds of interleaved x,y,z vertices. `valid` is false for an empty or malformed
/// (non-multiple-of-3) array, so callers cannot mistake an empty mesh for one at the origin — which
/// is what would silently drag zoom-extents to 0,0 (REQ-063: meshes are included in extents).
[[nodiscard]] Bounds ComputeBounds(const std::vector<float>& vertsXyz);

/// Merge \p add into \p acc. An invalid operand contributes nothing.
void ExpandBounds(Bounds* acc, const Bounds& add);

/// Why a mesh was rejected. Anything but `Ok` must be reported, never silently repaired (REQ-201).
enum class MeshProblem {
  Ok,
  VertsNotTriples,      ///< vertsXyz.size() % 3 != 0
  NormalsMismatch,      ///< a normal per vertex is required; count disagrees
  IndicesNotTriangles,  ///< indices.size() % 3 != 0
  IndexOutOfRange,      ///< an index addresses a vertex that does not exist
  PartRangeOutOfBounds, ///< a part's [begin, begin+count) leaves the index array
  PartCountNotTriangles ///< a part covers a partial triangle
};

[[nodiscard]] const char* MeshProblemText(MeshProblem p);

/// Full structural check of a mesh before it is stored or drawn.
///
/// Deliberately *not* forgiving. A truncated or malformed model must be refused with a specific
/// reason (REQ-065 requires exactly that of the importer), and an index that overruns the vertex
/// array must be caught here rather than by the GPU.
[[nodiscard]] MeshProblem ValidateMesh(const std::vector<float>& vertsXyz, const std::vector<float>& normalsXyz,
                                       const std::vector<std::uint32_t>& indices,
                                       const std::vector<std::pair<int, int>>& partRanges);

/// Area-weighted vertex normals for a triangle list, written to \p outNormals (3 per vertex).
///
/// Used when a source file supplies positions but no normals — legal in glTF, and common in
/// exports. Area weighting (rather than normalising each face first) is what makes a coarse
/// tessellation shade smoothly, because a large face should influence a shared vertex more than a
/// sliver does. Degenerate triangles contribute a zero cross product and so drop out for free.
void ComputeVertexNormals(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                          std::vector<float>* outNormals);

} // namespace meshgeom
