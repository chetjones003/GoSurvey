// REQ-068 / REQ-070 — the triangulation's border: every edge belonging to exactly one triangle.
//
// The border is what the selection highlight traces (ADR-036 (b)) and what REQ-070's "surface
// border" style component draws. Both consume it as geometry, so a wrong border is not a crash —
// it is a surface outlined in the wrong shape, which looks like a triangulation defect and is not
// one. That is exactly the kind of thing that has to be pinned against a HAND-COUNTED expectation
// rather than against the function's own output.
//
// The distinction under test is border-vs-hull. A concave outline and the void a REQ-069 hide
// boundary leaves behind are precisely the shapes a convex-hull test gets wrong, and they are the
// shapes real survey data produces.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "util/tinbuild.hpp"

namespace {

/// The border as an undirected vertex-index pair set, recovered by matching each emitted endpoint
/// back to its vertex. Comparing indices rather than floats is deliberate: the assertion is about
/// WHICH edges came out, and reading that off coordinates would turn a topology test into a
/// floating-point one.
std::vector<std::pair<int, int>> BorderPairs(const std::vector<float>& verts,
                                             const std::vector<std::uint32_t>& tris) {
  std::vector<float> out;
  TinBorderEdges(verts, tris, &out);

  const auto vertexAt = [&](float x, float y, float z) -> int {
    for (size_t i = 0; i + 2 < verts.size(); i += 3)
      if (verts[i] == x && verts[i + 1] == y && verts[i + 2] == z)
        return static_cast<int>(i / 3);
    return -1;
  };

  std::vector<std::pair<int, int>> pairs;
  for (size_t i = 0; i + 5 < out.size(); i += 6) {
    const int a = vertexAt(out[i], out[i + 1], out[i + 2]);
    const int b = vertexAt(out[i + 3], out[i + 4], out[i + 5]);
    pairs.emplace_back(std::min(a, b), std::max(a, b));
  }
  std::sort(pairs.begin(), pairs.end());
  return pairs;
}

} // namespace

TEST_CASE("A single triangle is all border", "[surface][req068][border]") {
  // Three edges, no neighbours, so every edge is border. The degenerate base case: if this were
  // wrong, nothing above it could be right.
  const std::vector<float> verts{0.f, 0.f, 10.f, 10.f, 0.f, 10.f, 0.f, 10.f, 10.f};
  const std::vector<std::uint32_t> tris{0, 1, 2};

  const auto pairs = BorderPairs(verts, tris);
  const std::vector<std::pair<int, int>> want{{0, 1}, {0, 2}, {1, 2}};
  CHECK(pairs == want);
}

TEST_CASE("The shared edge of two triangles is not border", "[surface][req068][border]") {
  // A unit square split along its diagonal 1–2. Four outer edges are border; the diagonal is shared
  // by both triangles and must not appear. This is the whole rule, at its smallest.
  //
  //   3---2
  //   | \ |
  //   0---1
  const std::vector<float> verts{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f};
  const std::vector<std::uint32_t> tris{0, 1, 2, 0, 2, 3};

  const auto pairs = BorderPairs(verts, tris);
  const std::vector<std::pair<int, int>> want{{0, 1}, {0, 3}, {1, 2}, {2, 3}};
  CHECK(pairs == want);
  // Named separately from the equality above, because this is the condition the whole function
  // exists for and a reader should see it fail by name.
  CHECK(std::find(pairs.begin(), pairs.end(), std::make_pair(0, 2)) == pairs.end());
}

TEST_CASE("A hole's rim is border, so the border is not the convex hull", "[surface][req068][border]") {
  // Eight vertices: an outer square 0-3 and an inner square 4-7, triangulated as a ring with the
  // middle removed — the shape a REQ-069 hide boundary leaves. The border is therefore TWO closed
  // loops: four outer edges plus four inner ones.
  //
  // A convex-hull implementation returns the four outer edges and calls it done, which is why this
  // test exists: the outer loop alone would look entirely correct on screen, right up until someone
  // cut a hole in a surface and saw no outline around it.
  //
  //   3-------2
  //   | 7---6 |
  //   | |   | |
  //   | 4---5 |
  //   0-------1
  const std::vector<float> verts{
      0.f, 0.f, 0.f,  3.f, 0.f, 0.f,  3.f, 3.f, 0.f,  0.f, 3.f, 0.f,   // outer 0..3
      1.f, 1.f, 0.f,  2.f, 1.f, 0.f,  2.f, 2.f, 0.f,  1.f, 2.f, 0.f};  // inner 4..7
  // The ring: two triangles per side, each side of the outer square paired with the matching side
  // of the inner one.
  const std::vector<std::uint32_t> tris{
      0, 1, 5,  0, 5, 4,   // bottom
      1, 2, 6,  1, 6, 5,   // right
      2, 3, 7,  2, 7, 6,   // top
      3, 0, 4,  3, 4, 7};  // left

  const auto pairs = BorderPairs(verts, tris);
  const std::vector<std::pair<int, int>> want{
      {0, 1}, {0, 3}, {1, 2}, {2, 3},   // outer loop
      {4, 5}, {4, 7}, {5, 6}, {6, 7}};  // the hole's rim
  CHECK(pairs == want);
  CHECK(pairs.size() == 8);
}

TEST_CASE("A concave outline keeps its notch", "[surface][req068][border]") {
  // An L-shape from two triangles sharing edge 0–2. Its border includes the reflex corner, which a
  // hull would cut straight across — the same distinction TinElevationAt documents for containment,
  // and the reason survey data needs a real border rather than a hull.
  //
  //   1
  //   | \
  //   0---2
  //       | \
  //       3---4      (0-1-2 and 2-3-4 meet only at vertex 2, so nothing is shared)
  const std::vector<float> verts{0.f, 0.f, 0.f, 0.f, 2.f, 0.f, 2.f, 0.f, 0.f,
                                 2.f, -2.f, 0.f, 4.f, -2.f, 0.f};
  const std::vector<std::uint32_t> tris{0, 1, 2, 2, 3, 4};

  const auto pairs = BorderPairs(verts, tris);
  // Two triangles touching at a single VERTEX share no EDGE, so all six edges are border.
  CHECK(pairs.size() == 6);
}

TEST_CASE("An empty triangulation produces an empty border", "[surface][req068][border]") {
  std::vector<float> out{1.f, 2.f, 3.f};  // pre-filled, to prove the function clears it
  TinBorderEdges({}, {}, &out);
  CHECK(out.empty());
}

TEST_CASE("Border extraction survives a real constrained build", "[surface][req068][border]") {
  // End to end against BuildTin rather than a hand-written index list: an unconstrained Delaunay
  // build's border IS its convex hull, so the two agree HERE — and that agreement is the control
  // for the hole and notch cases above, where they must not.
  //
  // Four corners of a square plus a centre point gives four triangles and a four-edge hull.
  std::vector<TinInputPoint> pts{
      {0.0, 0.0, 100.f}, {10.0, 0.0, 101.f}, {10.0, 10.0, 102.f}, {0.0, 10.0, 103.f},
      {5.0, 5.0, 104.f}};
  const TinBuildResult r = BuildTin(pts);
  REQUIRE(r.ok());
  REQUIRE(r.triangleCount() == 4);

  std::vector<float> out;
  TinBorderEdges(r.vertsXyz, r.indices, &out);
  // Six floats per segment, and the hull of a square is four segments.
  CHECK(out.size() % 6 == 0);
  CHECK(out.size() / 6 == 4);
}
