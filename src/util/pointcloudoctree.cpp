#include "util/pointcloudoctree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pointcloud {

namespace {

Bounds ComputeBounds(const std::vector<double> &pointsXyz) {
  Bounds b;
  b.minX = b.minY = b.minZ = std::numeric_limits<double>::max();
  b.maxX = b.maxY = b.maxZ = std::numeric_limits<double>::lowest();
  const std::int64_t n = static_cast<std::int64_t>(pointsXyz.size() / 3);
  for (std::int64_t i = 0; i < n; ++i) {
    const double x = pointsXyz[i * 3 + 0];
    const double y = pointsXyz[i * 3 + 1];
    const double z = pointsXyz[i * 3 + 2];
    b.minX = std::min(b.minX, x);
    b.minY = std::min(b.minY, y);
    b.minZ = std::min(b.minZ, z);
    b.maxX = std::max(b.maxX, x);
    b.maxY = std::max(b.maxY, y);
    b.maxZ = std::max(b.maxZ, z);
  }
  if (n == 0) {
    b = Bounds{};
  }
  return b;
}

/// Which of the 8 octants `(x,y,z)` falls in relative to `bounds`'s center — bit 0 = X half, bit
/// 1 = Y half, bit 2 = Z half. A point exactly on a boundary goes to the lower half (architecture
/// note in the header: no point is double-counted).
int OctantOf(const Bounds &bounds, double x, double y, double z) {
  int octant = 0;
  if (x >= bounds.centerX()) octant |= 1;
  if (y >= bounds.centerY()) octant |= 2;
  if (z >= bounds.centerZ()) octant |= 4;
  return octant;
}

Bounds ChildBounds(const Bounds &parent, int octant) {
  Bounds c = parent;
  const double cx = parent.centerX(), cy = parent.centerY(), cz = parent.centerZ();
  if (octant & 1) c.minX = cx; else c.maxX = cx;
  if (octant & 2) c.minY = cy; else c.maxY = cy;
  if (octant & 4) c.minZ = cz; else c.maxZ = cz;
  return c;
}

/// Recursively splits `[begin,end)` of `indices` (into `pointsXyz`) under `node`, appending new
/// nodes to `nodes` as needed. Returns the index of `node` within `nodes` (already pushed by the
/// caller before recursing into children, so children can reference it if ever needed).
std::int32_t BuildRecursive(const std::vector<double> &pointsXyz, std::vector<std::int64_t> &indices,
                             std::int64_t begin, std::int64_t end, const Bounds &bounds, int depth,
                             const BuildParams &params, std::vector<Node> &nodes) {
  Node node;
  node.bounds = bounds;
  const std::int64_t count = end - begin;

  const bool mustSplit = count > params.maxPointsPerLeaf && depth < params.maxDepth;
  if (!mustSplit) {
    node.pointIndexBegin = begin;
    node.pointCount = count;
    nodes.push_back(node);
    return static_cast<std::int32_t>(nodes.size() - 1);
  }

  // Partition [begin,end) into 8 contiguous octant buckets (counting sort by octant, stable
  // enough for this purpose and O(n) rather than 8 separate std::partition passes).
  std::array<std::vector<std::int64_t>, 8> buckets;
  for (std::int64_t i = begin; i < end; ++i) {
    const std::int64_t p = indices[i];
    const double x = pointsXyz[p * 3 + 0];
    const double y = pointsXyz[p * 3 + 1];
    const double z = pointsXyz[p * 3 + 2];
    buckets[OctantOf(bounds, x, y, z)].push_back(p);
  }
  std::int64_t write = begin;
  std::array<std::int64_t, 8> childBegin{}, childEnd{};
  for (int oct = 0; oct < 8; ++oct) {
    childBegin[oct] = write;
    for (std::int64_t p : buckets[oct]) indices[write++] = p;
    childEnd[oct] = write;
  }

  const std::int32_t selfIndex = static_cast<std::int32_t>(nodes.size());
  nodes.push_back(node);  // placeholder; children[] filled in below and node copied back at end.

  std::array<std::int32_t, 8> children{};
  for (int oct = 0; oct < 8; ++oct) {
    if (childBegin[oct] == childEnd[oct]) {
      children[oct] = -1;
      continue;
    }
    children[oct] = BuildRecursive(pointsXyz, indices, childBegin[oct], childEnd[oct],
                                    ChildBounds(bounds, oct), depth + 1, params, nodes);
  }
  nodes[selfIndex].children = children;
  return selfIndex;
}

/// Does a sphere at (`x`,`y`,`z`) with `radius` intersect `b`? Closest-point-on-box test.
bool SphereIntersectsBounds(const Bounds &b, double x, double y, double z, double radius) {
  const double cx = std::clamp(x, b.minX, b.maxX);
  const double cy = std::clamp(y, b.minY, b.maxY);
  const double cz = std::clamp(z, b.minZ, b.maxZ);
  const double dx = cx - x, dy = cy - y, dz = cz - z;
  return (dx * dx + dy * dy + dz * dz) <= radius * radius;
}

void CollectLeaves(const Octree &tree, std::int32_t nodeIndex, double x, double y, double z,
                    double radius, std::vector<std::int32_t> &out) {
  if (nodeIndex < 0) return;
  const Node &node = tree.nodes[nodeIndex];
  if (!SphereIntersectsBounds(node.bounds, x, y, z, radius)) return;
  if (node.isLeaf()) {
    out.push_back(nodeIndex);
    return;
  }
  for (std::int32_t child : node.children) CollectLeaves(tree, child, x, y, z, radius, out);
}

/// Conservative bounds-vs-infinite-cylinder test: true unless `b` provably cannot intersect the
/// cylinder of `lateralRadius` around the line through (`fx`,`fy`,`fz`) along unit vector
/// (`dx`,`dy`,`dz`). Uses the box's center and its own bounding radius (half the space diagonal) as
/// a stand-in for its true lateral extent — never tighter than the real answer, so pruning with it
/// cannot drop a node that might actually intersect.
bool CylinderIntersectsBounds(const Bounds &b, double fx, double fy, double fz, double dx, double dy,
                               double dz, double lateralRadius) {
  const double ox = b.centerX() - fx, oy = b.centerY() - fy, oz = b.centerZ() - fz;
  const double depth = ox * dx + oy * dy + oz * dz;
  const double perpX = ox - depth * dx, perpY = oy - depth * dy, perpZ = oz - depth * dz;
  const double lateralDist = std::sqrt(perpX * perpX + perpY * perpY + perpZ * perpZ);
  const double hx = (b.maxX - b.minX) * 0.5, hy = (b.maxY - b.minY) * 0.5, hz = (b.maxZ - b.minZ) * 0.5;
  const double boundingRadius = std::sqrt(hx * hx + hy * hy + hz * hz);
  return lateralDist - boundingRadius <= lateralRadius;
}

void CollectLeavesInCylinder(const Octree &tree, std::int32_t nodeIndex, double fx, double fy,
                              double fz, double dx, double dy, double dz, double lateralRadius,
                              std::vector<std::int32_t> &out) {
  if (nodeIndex < 0) return;
  const Node &node = tree.nodes[nodeIndex];
  if (!CylinderIntersectsBounds(node.bounds, fx, fy, fz, dx, dy, dz, lateralRadius)) return;
  if (node.isLeaf()) {
    out.push_back(nodeIndex);
    return;
  }
  for (std::int32_t child : node.children)
    CollectLeavesInCylinder(tree, child, fx, fy, fz, dx, dy, dz, lateralRadius, out);
}

}  // namespace

Octree BuildOctree(const std::vector<double> &pointsXyz, const BuildParams &params) {
  Octree tree;
  const std::int64_t n = static_cast<std::int64_t>(pointsXyz.size() / 3);
  tree.totalPointCount = n;
  tree.pointIndices.resize(static_cast<std::size_t>(n));
  for (std::int64_t i = 0; i < n; ++i) tree.pointIndices[static_cast<std::size_t>(i)] = i;
  if (n == 0) {
    Node root;
    tree.nodes.push_back(root);
    return tree;
  }
  const Bounds rootBounds = ComputeBounds(pointsXyz);
  BuildRecursive(pointsXyz, tree.pointIndices, 0, n, rootBounds, 0, params, tree.nodes);
  return tree;
}

std::vector<std::int32_t> QueryLeavesNearPoint(const Octree &tree, double x, double y, double z,
                                                double radius) {
  std::vector<std::int32_t> out;
  if (!tree.nodes.empty()) CollectLeaves(tree, 0, x, y, z, radius, out);
  return out;
}

std::vector<CylinderLodLeaf> SelectLodLeavesInCylinder(const Octree &tree, double focusX,
                                                        double focusY, double focusZ, double dirX,
                                                        double dirY, double dirZ,
                                                        double lateralRadius, int maxLeaves) {
  std::vector<std::int32_t> candidates;
  if (!tree.nodes.empty())
    CollectLeavesInCylinder(tree, 0, focusX, focusY, focusZ, dirX, dirY, dirZ, lateralRadius,
                             candidates);
  std::vector<CylinderLodLeaf> out;
  out.reserve(candidates.size());
  for (std::int32_t idx : candidates) {
    const Bounds &b = tree.nodes[static_cast<std::size_t>(idx)].bounds;
    const double ox = b.centerX() - focusX, oy = b.centerY() - focusY, oz = b.centerZ() - focusZ;
    out.push_back(CylinderLodLeaf{idx, ox * dirX + oy * dirY + oz * dirZ});
  }
  std::sort(out.begin(), out.end(), [](const CylinderLodLeaf &a, const CylinderLodLeaf &b) {
    return a.depthAlongView < b.depthAlongView;
  });
  if (maxLeaves >= 0 && static_cast<std::size_t>(maxLeaves) < out.size())
    out.resize(static_cast<std::size_t>(maxLeaves));
  return out;
}

}  // namespace pointcloud
