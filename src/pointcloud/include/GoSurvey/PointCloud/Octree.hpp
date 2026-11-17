#pragma once

#include <array>
#include <cstdint>
#include <vector>

/// Point-cloud out-of-core octree (REQ-171/REQ-172, ADR-060).
///
/// Pure Domain code: builds and queries an octree over a point set already in memory. It knows
/// nothing about files, `.gscloud`, or the GPU — `src/io/CadPointCloudCache.*` serializes what
/// this module builds, and the Renderer decides which nodes to draw from what this module
/// returns. That split is what keeps this header a single, easily-tested concrete type rather
/// than a format/IO/render abstraction (architecture §11.4 — no new abstraction without ≥2
/// present-day concrete uses; this module has exactly one job).
namespace pointcloud {

/// Axis-aligned bounds of one octree node, in the same local-storage frame as the point payload
/// (REQ-101 / `worldDocumentOrigin`).
struct Bounds {
  double minX = 0, minY = 0, minZ = 0;
  double maxX = 0, maxY = 0, maxZ = 0;

  [[nodiscard]] double centerX() const { return (minX + maxX) * 0.5; }
  [[nodiscard]] double centerY() const { return (minY + maxY) * 0.5; }
  [[nodiscard]] double centerZ() const { return (minZ + maxZ) * 0.5; }
  [[nodiscard]] bool contains(double x, double y, double z) const {
    return x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;
  }
};

/// One octree node. Interior nodes have all eight `children` set (index into `Octree::nodes`,
/// `-1` for "no child"); leaf nodes own a contiguous run of `Octree::pointIndices`.
struct Node {
  Bounds bounds;
  std::array<std::int32_t, 8> children{-1, -1, -1, -1, -1, -1, -1, -1};
  /// Offset into `Octree::pointIndices` where this node's points begin (leaf nodes only).
  std::int64_t pointIndexBegin = 0;
  /// Number of points owned directly by this node (leaf nodes only; interior nodes are 0 — their
  /// points live in their children).
  std::int64_t pointCount = 0;

  /// **Not** `children[0] < 0` — an interior node whose octant 0 happens to hold zero points
  /// (routine for a real scan's spatial distribution) also has `children[0] == -1`, which would
  /// misclassify a real interior node as an empty leaf and silently orphan every point in its
  /// other seven octants. A genuine leaf never populates ANY child, so all eight must be checked.
  [[nodiscard]] bool isLeaf() const {
    for (int c : children)
      if (c >= 0) return false;
    return true;
  }
};

/// A built octree over one point set. `pointIndices` is a permutation of `0..N-1` into the
/// caller's original XYZ array — the octree never copies point data, only reorders indices, so
/// building it does not double the memory footprint of a resident point buffer.
struct Octree {
  std::vector<Node> nodes;         ///< nodes[0] is the root.
  std::vector<std::int64_t> pointIndices;
  std::int64_t totalPointCount = 0;
};

/// Build parameters. `maxPointsPerLeaf` bounds how many points a leaf may hold before it splits
/// into eight children; `maxDepth` is a hard ceiling so a degenerate input (e.g. many coincident
/// points) cannot recurse without bound.
struct BuildParams {
  std::int64_t maxPointsPerLeaf = 50'000;
  int maxDepth = 16;
};

/// Builds an octree over `pointsXyz` (interleaved x,y,z, architecture §11.8). Points exactly on a
/// split boundary are assigned to the lower octant on each axis (`< center`), so every point is
/// owned by exactly one leaf with no double-counting.
[[nodiscard]] Octree BuildOctree(const std::vector<double> &pointsXyz,
                                  const BuildParams &params = {});

/// Depth-first list of leaf node indices whose bounds intersect a sphere of `radius` centered at
/// (`x`,`y`,`z`) — the query the LOD renderer uses to pick nodes near the camera. A leaf's actual
/// distance to the camera, not just its bounds, decides the LOD level the renderer draws it at;
/// this function only narrows the candidate set.
[[nodiscard]] std::vector<std::int32_t> QueryLeavesNearPoint(const Octree &tree, double x,
                                                              double y, double z, double radius);

/// One leaf chosen by \ref SelectLodLeavesInCylinder, nearest-to-the-eye first.
struct CylinderLodLeaf {
  std::int32_t leafNodeIndex = -1;
  /// Signed distance along `dir` from `focus` to this leaf's bounds center — NOT 3D distance to
  /// `focus`. More negative means closer to the eye, given `dir` points from eye toward the scene
  /// (`Camera::ForwardWorld`'s convention); this is what nearest-first sorts by.
  double depthAlongView = 0.0;
};

/// Picks up to `maxLeaves` leaves whose bounds lie within `lateralRadius` of the infinite line
/// through `focus` along unit vector `dir`, ordered nearest-to-the-eye-first (ascending
/// `depthAlongView`) rather than nearest to `focus` in 3D (REQ-171/172, ADR-060).
///
/// This is a CYLINDER test, not a sphere, because the two axes need different treatment: lateral
/// (perpendicular-to-view) extent is what "on screen" means and should shrink as the camera zooms
/// in, while depth along the view axis must NOT be bounded by zoom — a point cloud has real depth
/// extent (a near-facing surface vs. whatever is behind it), unrelated to how far the user has
/// zoomed in laterally. An isotropic sphere conflates the two: shrunk to track zoom, it can exclude
/// near-facing geometry that simply sits at a different depth than `focus` (an orbited camera's pan
/// target is not pinned to the visible surface), while left large enough to reach that geometry it
/// stops tracking zoom at all and costs most of the tree. Leaves ARE ordered by depth precisely so
/// that when a caller then applies a point budget, the near-facing (visible) side wins ties over
/// whatever sits farther back along the same line of sight — the bug this function exists to fix
/// (TASK-270 part 9): a sphere search picked leaves nearest to the pan target's raw 3D position,
/// which could be the far side of a structure, over the near side the camera is actually looking at.
///
/// The bounds-vs-cylinder test is conservative (a node's center-to-axis lateral distance minus its
/// own bounding radius), so it never wrongly excludes a node that might truly intersect — at the
/// cost of occasionally admitting a few extra candidates near the boundary, which only costs sort
/// time, not correctness.
[[nodiscard]] std::vector<CylinderLodLeaf> SelectLodLeavesInCylinder(const Octree &tree,
                                                                      double focusX, double focusY,
                                                                      double focusZ, double dirX,
                                                                      double dirY, double dirZ,
                                                                      double lateralRadius,
                                                                      int maxLeaves);

}  // namespace pointcloud
