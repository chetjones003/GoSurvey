// The REQ-100 bench scene and its statistics.
//
// A benchmark whose scene drifts between runs measures nothing, and a percentile that is off by one
// makes a passing build look failing. Both are cheap to pin down and neither is visible in the
// number the bench prints — which is exactly why they are tested here rather than eyeballed.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "util/benchscene.hpp"

using Catch::Approx;

namespace {

int SegmentsIn(const std::vector<int>& offsets) {
  int n = 0;
  for (size_t i = 0; i + 1 < offsets.size(); ++i)
    n += (offsets[i + 1] - offsets[i]) - 1;  // an open polyline of V vertices has V-1 segments
  return n;
}

} // namespace

TEST_CASE("The bench scene contains exactly the requested segment count", "[bench]") {
  // REQ-100 names 250,000 segments. If the generator rounded to whole contours the benchmark would
  // quietly measure a different density than the requirement specifies.
  std::vector<float> verts;
  std::vector<int> offsets;
  std::vector<std::uint8_t> closed;
  for (int target : {1, 2, 499, 500, 501, 12345, 250000}) {
    const int produced = benchscene::BuildContourScene(target, &verts, &offsets, &closed);
    INFO("target = " << target);
    CHECK(produced == target);
    CHECK(SegmentsIn(offsets) == target);
    CHECK(closed.size() + 1 == offsets.size());
    CHECK(verts.size() % 3 == 0);
    CHECK(static_cast<int>(verts.size() / 3) == offsets.back());
  }
}

TEST_CASE("The bench scene is byte-identical across runs", "[bench]") {
  // "Committed bench scene" only means something if regenerating it reproduces it exactly.
  std::vector<float> v1, v2;
  std::vector<int> o1, o2;
  std::vector<std::uint8_t> c1, c2;
  benchscene::BuildContourScene(20000, &v1, &o1, &c1);
  benchscene::BuildContourScene(20000, &v2, &o2, &c2);
  REQUIRE(v1.size() == v2.size());
  for (size_t i = 0; i < v1.size(); ++i)
    REQUIRE(v1[i] == v2[i]);  // exact equality is the point here
  REQUIRE(o1 == o2);
  REQUIRE(c1 == c2);
}

TEST_CASE("Bench contours are separated in elevation", "[bench]") {
  // Orbit is the worst case only if the scene has depth to reveal. Flat contours would let the
  // benchmark pass on a renderer that ignored Z entirely — which is the bug TASK-036 just fixed.
  std::vector<float> verts;
  std::vector<int> offsets;
  std::vector<std::uint8_t> closed;
  benchscene::BuildContourScene(5000, &verts, &offsets, &closed);
  REQUIRE(offsets.size() > 2);

  float zFirst = verts[2];
  float zLast = verts[static_cast<size_t>(offsets[offsets.size() - 2]) * 3 + 2];
  CHECK(zFirst != zLast);

  // Each contour is an ISO-elevation line: constant Z within the polyline.
  for (int v = offsets[0]; v < offsets[1]; ++v)
    REQUIRE(verts[static_cast<size_t>(v) * 3 + 2] == zFirst);
}

TEST_CASE("Segment count changes scene DENSITY, not its extent", "[bench]") {
  // The bug this pins down was real and silently flattering: contours spaced a constant distance
  // apart made a 250k-segment scene physically larger than the viewport could frame, so the GPU
  // rasterised only part of it and 250k benchmarked FASTER than 20k. A benchmark that measures less
  // geometry as you ask for more is worse than none. Extent must not grow with count.
  auto extentOf = [](int segs) {
    std::vector<float> v;
    std::vector<int> o;
    std::vector<std::uint8_t> c;
    benchscene::BuildContourScene(segs, &v, &o, &c);
    float mnX = v[0], mxX = v[0], mnY = v[1], mxY = v[1];
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
      mnX = std::min(mnX, v[i]);     mxX = std::max(mxX, v[i]);
      mnY = std::min(mnY, v[i + 1]); mxY = std::max(mxY, v[i + 1]);
    }
    return std::pair<float, float>(mxX - mnX, mxY - mnY);
  };
  const auto small = extentOf(5000);
  const auto large = extentOf(250000);
  // Within 15%: the wandering term makes the exact bound depend a little on how many contours are
  // sampled, but the extent must not scale with the count.
  CHECK(large.first == Approx(small.first).epsilon(0.15));
  CHECK(large.second == Approx(small.second).epsilon(0.15));
}

TEST_CASE("An empty or degenerate request produces an empty scene", "[bench]") {
  std::vector<float> verts;
  std::vector<int> offsets;
  std::vector<std::uint8_t> closed;
  CHECK(benchscene::BuildContourScene(0, &verts, &offsets, &closed) == 0);
  CHECK(verts.empty());
  CHECK(benchscene::BuildContourScene(-5, &verts, &offsets, &closed) == 0);
  CHECK(benchscene::BuildContourScene(10, nullptr, &offsets, &closed) == 0);
}

TEST_CASE("The mesh bench scene contains exactly the requested triangle count", "[bench]") {
  // REQ-100 (b) names 2,000,000 triangles (decided 2026-08-15). A generator that emitted whole grid
  // cells and returned whatever that came to would measure a density nobody chose, and the
  // difference would be invisible in the reported p95.
  std::vector<float> verts, normals;
  std::vector<std::uint32_t> indices;
  for (int target : {1, 2, 3, 7, 800, 12345, 2000000}) {
    const int produced = benchscene::BuildMeshScene(target, &verts, &normals, &indices);
    INFO("target = " << target);
    CHECK(produced == target);
    CHECK(indices.size() == static_cast<size_t>(target) * 3);
    CHECK(verts.size() % 3 == 0);
    CHECK(normals.size() == verts.size());  // one normal per vertex, parallel arrays
  }
}

TEST_CASE("The mesh bench scene is byte-identical across runs", "[bench]") {
  std::vector<float> v1, n1, v2, n2;
  std::vector<std::uint32_t> i1, i2;
  benchscene::BuildMeshScene(20000, &v1, &n1, &i1);
  benchscene::BuildMeshScene(20000, &v2, &n2, &i2);
  REQUIRE(v1.size() == v2.size());
  for (size_t i = 0; i < v1.size(); ++i)
    REQUIRE(v1[i] == v2[i]);  // exact equality is the point
  REQUIRE(n1.size() == n2.size());
  for (size_t i = 0; i < n1.size(); ++i)
    REQUIRE(n1[i] == n2[i]);
  REQUIRE(i1 == i2);
}

TEST_CASE("Triangle count changes mesh DENSITY, not its extent", "[bench]") {
  // The same trap the contour scene fell into: a mesh that grows past the viewport would let the
  // GPU rasterise less of it as the count rises, and the benchmark would improve with size.
  auto extentOf = [](int tris) {
    std::vector<float> v, n;
    std::vector<std::uint32_t> idx;
    benchscene::BuildMeshScene(tris, &v, &n, &idx);
    float mnX = v[0], mxX = v[0], mnY = v[1], mxY = v[1];
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
      mnX = std::min(mnX, v[i]);     mxX = std::max(mxX, v[i]);
      mnY = std::min(mnY, v[i + 1]); mxY = std::max(mxY, v[i + 1]);
    }
    return std::pair<float, float>(mxX - mnX, mxY - mnY);
  };
  const auto small = extentOf(5000);
  const auto large = extentOf(500000);
  // Exact here, unlike the contour case: the grid spans the plan extent whatever its resolution.
  CHECK(large.first == Approx(small.first));
  CHECK(large.second == Approx(small.second));
}

TEST_CASE("Mesh bench normals are unit length and its indices are in range", "[bench]") {
  // A non-unit normal shades wrong, and an out-of-range index is an out-of-bounds read on the GPU
  // — neither is visible in a frame time, which is why they are asserted here.
  std::vector<float> verts, normals;
  std::vector<std::uint32_t> indices;
  const int tris = benchscene::BuildMeshScene(20000, &verts, &normals, &indices);
  REQUIRE(tris == 20000);
  const std::uint32_t vertexCount = static_cast<std::uint32_t>(verts.size() / 3);
  for (std::uint32_t idx : indices)
    REQUIRE(idx < vertexCount);
  for (size_t i = 0; i + 2 < normals.size(); i += 3) {
    const double len = std::sqrt(static_cast<double>(normals[i]) * normals[i] +
                                 static_cast<double>(normals[i + 1]) * normals[i + 1] +
                                 static_cast<double>(normals[i + 2]) * normals[i + 2]);
    REQUIRE(len == Approx(1.0).epsilon(1e-5));
  }
}

TEST_CASE("The mesh bench scene is genuinely curved", "[bench]") {
  // A flat grid would shade as one gradient band, never self-occlude, and give a number that says
  // nothing about a real shaded model — while still passing every other test here.
  std::vector<float> verts, normals;
  std::vector<std::uint32_t> indices;
  benchscene::BuildMeshScene(20000, &verts, &normals, &indices);
  REQUIRE(normals.size() > 300);
  bool normalsVary = false, elevationVaries = false;
  for (size_t i = 3; i + 2 < normals.size(); i += 3) {
    if (normals[i] != normals[0] || normals[i + 1] != normals[1])
      normalsVary = true;
    if (verts[i + 2] != verts[2])
      elevationVaries = true;
  }
  CHECK(normalsVary);
  CHECK(elevationVaries);
}

TEST_CASE("An empty or degenerate mesh request produces an empty scene", "[bench]") {
  std::vector<float> verts, normals;
  std::vector<std::uint32_t> indices;
  CHECK(benchscene::BuildMeshScene(0, &verts, &normals, &indices) == 0);
  CHECK(verts.empty());
  CHECK(indices.empty());
  CHECK(benchscene::BuildMeshScene(-5, &verts, &normals, &indices) == 0);
  CHECK(benchscene::BuildMeshScene(10, nullptr, &normals, &indices) == 0);
  CHECK(benchscene::BuildMeshScene(10, &verts, nullptr, &indices) == 0);
  CHECK(benchscene::BuildMeshScene(10, &verts, &normals, nullptr) == 0);
}

TEST_CASE("Nearest-rank percentile picks a real sample", "[bench]") {
  // 1..100: the 95th percentile is the 95th value, not an interpolation between 95 and 96.
  std::vector<double> s;
  for (int i = 1; i <= 100; ++i)
    s.push_back(static_cast<double>(i));
  CHECK(benchscene::Percentile(s, 95.0) == Approx(95.0));
  CHECK(benchscene::Percentile(s, 50.0) == Approx(50.0));
  CHECK(benchscene::Percentile(s, 0.0) == Approx(1.0));
  CHECK(benchscene::Percentile(s, 100.0) == Approx(100.0));
}

TEST_CASE("Percentile is order-independent and handles tiny samples", "[bench]") {
  const std::vector<double> shuffled = {9.0, 1.0, 7.0, 3.0, 5.0};
  CHECK(benchscene::Percentile(shuffled, 100.0) == Approx(9.0));
  CHECK(benchscene::Percentile(shuffled, 0.0) == Approx(1.0));
  CHECK(benchscene::Percentile({}, 95.0) == Approx(0.0));
  CHECK(benchscene::Percentile({42.0}, 95.0) == Approx(42.0));
}

TEST_CASE("Summarize reports the frame a p95 verdict rests on", "[bench]") {
  // Nineteen 10 ms frames and one 40 ms stall: the p95 is the stall, and REQ-100's verdict must
  // turn on it rather than on the flattering mean.
  std::vector<double> f(19, 10.0);
  f.push_back(40.0);
  const benchscene::FrameStats s = benchscene::Summarize(f);
  CHECK(s.frames == 20);
  CHECK(s.minMs == Approx(10.0));
  CHECK(s.medianMs == Approx(10.0));
  CHECK(s.p95Ms == Approx(10.0));   // rank = ceil(0.95*20) = 19 → still a 10 ms frame
  CHECK(s.p99Ms == Approx(40.0));   // rank = ceil(0.99*20) = 20 → the stall
  CHECK(s.maxMs == Approx(40.0));
  CHECK(s.meanMs == Approx(11.5));
}
