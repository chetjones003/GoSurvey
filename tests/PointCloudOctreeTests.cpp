// Point-cloud out-of-core octree (REQ-171/REQ-172, ADR-060).
//
// Pinned without a window because the octree is pure Domain code with no file IO and no GL: given
// an in-memory point set, does the tree (1) own every point exactly once, (2) never lose a point
// to double-counting or drop it at a boundary, (3) return the leaves that actually intersect a
// query sphere. A renderer LOD bug hiding behind a wrong node/point count would otherwise show up
// only as "some scan points are invisible," which is exactly the class of silent-wrong-result
// REQ-201 exists to prevent.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <vector>

#include "util/pointcloudoctree.hpp"

using Catch::Approx;
using pointcloud::BuildOctree;
using pointcloud::BuildParams;
using pointcloud::Octree;
using pointcloud::QueryLeavesNearPoint;

namespace {

std::vector<double> GridPoints(int n, double spacing) {
  std::vector<double> pts;
  pts.reserve(static_cast<std::size_t>(n) * n * n * 3);
  for (int x = 0; x < n; ++x)
    for (int y = 0; y < n; ++y)
      for (int z = 0; z < n; ++z) {
        pts.push_back(x * spacing);
        pts.push_back(y * spacing);
        pts.push_back(z * spacing);
      }
  return pts;
}

/// Sums leaf point counts across the whole tree by walking every node.
std::int64_t SumLeafPointCounts(const Octree &tree) {
  std::int64_t total = 0;
  for (const auto &node : tree.nodes)
    if (node.isLeaf()) total += node.pointCount;
  return total;
}

}  // namespace

TEST_CASE("BuildOctree: empty input produces a single empty root", "[pointcloud][octree]") {
  const Octree tree = BuildOctree({});
  REQUIRE(tree.totalPointCount == 0);
  REQUIRE(tree.nodes.size() == 1);
  CHECK(tree.nodes[0].isLeaf());
  CHECK(tree.nodes[0].pointCount == 0);
}

TEST_CASE("BuildOctree: every point is owned by exactly one leaf, none lost or duplicated",
          "[pointcloud][octree]") {
  const std::vector<double> pts = GridPoints(10, 1.0);  // 1000 points
  const std::int64_t n = static_cast<std::int64_t>(pts.size() / 3);

  BuildParams params;
  params.maxPointsPerLeaf = 37;  // force several splits at an awkward, non-power-of-two size
  const Octree tree = BuildOctree(pts, params);

  REQUIRE(tree.totalPointCount == n);
  REQUIRE(static_cast<std::int64_t>(tree.pointIndices.size()) == n);
  CHECK(SumLeafPointCounts(tree) == n);

  // pointIndices must be a permutation of 0..n-1 — no point index missing, none repeated.
  std::vector<std::int64_t> sorted = tree.pointIndices;
  std::sort(sorted.begin(), sorted.end());
  for (std::int64_t i = 0; i < n; ++i) REQUIRE(sorted[static_cast<std::size_t>(i)] == i);

  // No leaf exceeds maxPointsPerLeaf unless it is at maxDepth (not the case for this small grid).
  for (const auto &node : tree.nodes)
    if (node.isLeaf()) CHECK(node.pointCount <= params.maxPointsPerLeaf);
}

TEST_CASE("BuildOctree: root bounds exactly enclose the input point set", "[pointcloud][octree]") {
  const std::vector<double> pts = {0, 0, 0, 5, 5, 5, -2, 3, 1};
  const Octree tree = BuildOctree(pts);
  const auto &root = tree.nodes[0].bounds;
  CHECK(root.minX == Approx(-2));
  CHECK(root.minY == Approx(0));
  CHECK(root.minZ == Approx(0));
  CHECK(root.maxX == Approx(5));
  CHECK(root.maxY == Approx(5));
  CHECK(root.maxZ == Approx(5));
}

TEST_CASE("BuildOctree: a single-point cloud never splits, regardless of maxPointsPerLeaf",
          "[pointcloud][octree]") {
  const std::vector<double> pts = {1.0, 2.0, 3.0};
  BuildParams params;
  params.maxPointsPerLeaf = 1;
  const Octree tree = BuildOctree(pts, params);
  REQUIRE(tree.nodes.size() == 1);
  CHECK(tree.nodes[0].isLeaf());
  CHECK(tree.nodes[0].pointCount == 1);
}

TEST_CASE("BuildOctree: an interior split whose octant 0 is empty still owns every point "
          "(regression: isLeaf() must not key off children[0] alone)",
          "[pointcloud][octree]") {
  // Every point has x >= the bounds' x-midpoint, so octant 0 (the -x,-y,-z octant, bit0=0 meaning
  // x < center) is EMPTY at the root split. Before the isLeaf() fix, a node like this — interior,
  // but with children[0] == -1 because octant 0 has no points — was misclassified as a leaf with
  // no data, silently dropping every point in its other seven (non-empty) octants.
  std::vector<double> pts;
  for (int y = 0; y < 20; ++y)
    for (int z = 0; z < 20; ++z) {
      pts.push_back(100.0 + (y % 2) * 0.5);  // x always on the "upper" side of the split
      pts.push_back(static_cast<double>(y));
      pts.push_back(static_cast<double>(z));
    }
  const std::int64_t n = static_cast<std::int64_t>(pts.size() / 3);

  BuildParams params;
  params.maxPointsPerLeaf = 17;  // force splitting well past a single leaf
  const Octree tree = BuildOctree(pts, params);

  REQUIRE(tree.totalPointCount == n);
  CHECK(SumLeafPointCounts(tree) == n);  // would undercount under the old buggy isLeaf()

  std::vector<std::int64_t> sorted = tree.pointIndices;
  std::sort(sorted.begin(), sorted.end());
  for (std::int64_t i = 0; i < n; ++i) REQUIRE(sorted[static_cast<std::size_t>(i)] == i);
}

TEST_CASE("QueryLeavesNearPoint: returns only leaves whose bounds intersect the query sphere",
          "[pointcloud][octree]") {
  // Two well-separated clusters, so a small-radius query centered on one must not return the
  // other's leaf.
  std::vector<double> pts;
  for (int i = 0; i < 20; ++i) {
    pts.push_back(0.0 + i * 0.01);
    pts.push_back(0.0);
    pts.push_back(0.0);
  }
  for (int i = 0; i < 20; ++i) {
    pts.push_back(100.0 + i * 0.01);
    pts.push_back(100.0);
    pts.push_back(100.0);
  }
  BuildParams params;
  params.maxPointsPerLeaf = 5;  // force both clusters to split into multiple leaves
  const Octree tree = BuildOctree(pts, params);

  const auto near = QueryLeavesNearPoint(tree, 0.0, 0.0, 0.0, 1.0);
  REQUIRE_FALSE(near.empty());
  for (std::int32_t idx : near) {
    const auto &b = tree.nodes[static_cast<std::size_t>(idx)].bounds;
    // Every returned leaf's bounds must lie within the near cluster's neighbourhood, not the far
    // one 100 units away.
    CHECK(b.minX < 50.0);
  }

  const auto far = QueryLeavesNearPoint(tree, 100.0, 100.0, 100.0, 1.0);
  REQUIRE_FALSE(far.empty());
  for (std::int32_t idx : far) {
    const auto &b = tree.nodes[static_cast<std::size_t>(idx)].bounds;
    CHECK(b.maxX > 50.0);
  }

  // A query far from both clusters returns nothing.
  const auto none = QueryLeavesNearPoint(tree, 500.0, 500.0, 500.0, 1.0);
  CHECK(none.empty());
}

TEST_CASE("SelectLodLeavesInCylinder: depth along the view axis is unbounded, lateral is not",
          "[pointcloud][octree][lod]") {
  // A: on-axis, near. B: on-axis, FAR (would be excluded by an isotropic sphere sized for A's
  // neighbourhood, but a cylinder must still reach it). C: off-axis, moderate depth — must be
  // excluded by the lateral radius despite being closer in raw 3D distance than B.
  std::vector<double> pts;
  const auto addCluster = [&](double cx, double cy, double cz) {
    for (int i = 0; i < 10; ++i) {
      pts.push_back(cx + i * 0.001);
      pts.push_back(cy);
      pts.push_back(cz);
    }
  };
  addCluster(10.0, 0.0, 0.0);    // A
  addCluster(200.0, 0.0, 0.0);   // B
  addCluster(50.0, 100.0, 0.0);  // C — well outside a lateral radius of 5

  BuildParams params;
  params.maxPointsPerLeaf = 20;  // > each 10-point cluster, so a cluster stays exactly one leaf
  const Octree tree = BuildOctree(pts, params);

  // Line along +X through the origin, lateral radius 5: reaches A and B (any depth), not C.
  const auto sel = pointcloud::SelectLodLeavesInCylinder(tree, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                                          /*lateralRadius=*/5.0, /*maxLeaves=*/100);
  REQUIRE(sel.size() == 2);
  // Nearest-to-eye first: A's leaf (near x=10) before B's leaf (near x=200) — not the other way
  // around. (Leaf BOUNDS centers, not point centroids, so exact values aren't asserted here.)
  CHECK(sel[0].depthAlongView < sel[1].depthAlongView);
  CHECK(sel[1].depthAlongView - sel[0].depthAlongView > 50.0);  // clearly A-then-B, not noise

  // Budgeted to 1 leaf keeps only the nearest-to-eye one (A), never the far one (B) — the bug this
  // function replaces a sphere query to fix (TASK-270 part 9).
  const auto budgeted = pointcloud::SelectLodLeavesInCylinder(tree, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                                               5.0, /*maxLeaves=*/1);
  REQUIRE(budgeted.size() == 1);
  CHECK(budgeted[0].depthAlongView == Approx(sel[0].depthAlongView));

  // A lateral radius reaching nothing returns an empty selection, not a crash.
  const auto none =
      pointcloud::SelectLodLeavesInCylinder(tree, 1000.0, 1000.0, 1000.0, 1.0, 0.0, 0.0, 1.0, 100);
  CHECK(none.empty());
}
