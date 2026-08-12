// Per-vertex elevation through the linetype tessellator (REQ-057/058, TASK-036 D1).
//
// A POLYLINE is the one committed entity whose vertices each carry their own Z. Before this, the
// renderer handed the chain a single flat elevation and dropped the per-vertex Z on the floor, so a
// polyline drawn up a slope — or imported from DXF with per-vertex elevations — was drawn level.
// These cover both the interpolation and the flat path it must not disturb.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "commands/CadLinetype.hpp"

#include <vector>

namespace {

constexpr size_t kStride = 7;  ///< x,y,z,r,g,b,a per vertex
const float kWhite[4] = {1.f, 1.f, 1.f, 1.f};

/// Z of vertex \p i in a tessellated buffer.
float VertZ(const std::vector<float>& v, size_t i) { return v[i * kStride + 2]; }
/// X of vertex \p i.
float VertX(const std::vector<float>& v, size_t i) { return v[i * kStride + 0]; }

size_t VertCount(const std::vector<float>& v) { return v.size() / kStride; }

} // namespace

TEST_CASE("Chain tessellation carries per-vertex Z on a solid linetype", "[linetype][z]") {
  // Two edges rising 0 → 10 → 30 along +X.
  const float xy[6] = {0.f, 0.f, 100.f, 0.f, 200.f, 0.f};
  const float zs[3] = {0.f, 10.f, 30.f};
  std::vector<float> out;
  CadTessellateLinetypeChainVc(xy, 3, 0.f, /*closed=*/false, "Continuous", 1.f, kWhite, &out, zs);

  REQUIRE(VertCount(out) == 4);  // 2 edges × 2 vertices
  CHECK_THAT(VertZ(out, 0), Catch::Matchers::WithinAbs(0.0, 1e-6));
  CHECK_THAT(VertZ(out, 1), Catch::Matchers::WithinAbs(10.0, 1e-6));
  CHECK_THAT(VertZ(out, 2), Catch::Matchers::WithinAbs(10.0, 1e-6));
  CHECK_THAT(VertZ(out, 3), Catch::Matchers::WithinAbs(30.0, 1e-6));
}

TEST_CASE("A closed chain wraps the last vertex's Z back to the first", "[linetype][z]") {
  // A RECT commits as a closed polyline, so the closing edge must carry both real elevations.
  const float xy[8] = {0.f, 0.f, 10.f, 0.f, 10.f, 10.f, 0.f, 10.f};
  const float zs[4] = {1.f, 2.f, 3.f, 4.f};
  std::vector<float> out;
  CadTessellateLinetypeChainVc(xy, 4, 0.f, /*closed=*/true, "Continuous", 1.f, kWhite, &out, zs);

  REQUIRE(VertCount(out) == 8);  // 4 edges (incl. the wrap) × 2
  CHECK_THAT(VertZ(out, 6), Catch::Matchers::WithinAbs(4.0, 1e-6));  // closing edge starts at v3
  CHECK_THAT(VertZ(out, 7), Catch::Matchers::WithinAbs(1.0, 1e-6));  // and ends back at v0
}

TEST_CASE("Dashes on a sloped segment interpolate their own Z", "[linetype][z]") {
  // One 100-unit edge rising 0 → 100, so Z and X advance together: every emitted vertex must have
  // z == x. A dash that took the segment's start Z for both ends would stair-step instead.
  const float xy[4] = {0.f, 0.f, 100.f, 0.f};
  const float zs[2] = {0.f, 100.f};
  std::vector<float> out;
  CadTessellateLinetypeChainVc(xy, 2, 0.f, /*closed=*/false, "DASHED", 2.f, kWhite, &out, zs);

  REQUIRE(VertCount(out) > 2);  // the pattern actually broke the edge into dashes
  for (size_t i = 0; i < VertCount(out); ++i)
    CHECK_THAT(VertZ(out, i), Catch::Matchers::WithinAbs(static_cast<double>(VertX(out, i)), 1e-3));
}

TEST_CASE("A null per-vertex array reproduces the flat result exactly", "[linetype][z]") {
  // The failure mode that matters most: circle / arc / ellipse are planar and still pass one z.
  // Widening the signature must not move a single vertex for them.
  const float xy[6] = {0.f, 0.f, 50.f, 25.f, 90.f, -12.f};
  std::vector<float> flat;
  std::vector<float> viaArray;
  const float zs[3] = {7.5f, 7.5f, 7.5f};

  for (const char* lt : {"Continuous", "DASHED", "CENTER", "NoSuchLinetype"}) {
    flat.clear();
    viaArray.clear();
    CadTessellateLinetypeChainVc(xy, 3, 7.5f, false, lt, 2.f, kWhite, &flat, nullptr);
    CadTessellateLinetypeChainVc(xy, 3, 0.f, false, lt, 2.f, kWhite, &viaArray, zs);
    REQUIRE(flat.size() == viaArray.size());
    for (size_t i = 0; i < flat.size(); ++i)
      REQUIRE(flat[i] == viaArray[i]);  // bit-for-bit: a constant Z array is the flat case
  }
}

TEST_CASE("Flat tessellation is unchanged when no Z array is supplied", "[linetype][z]") {
  // Guards the planar producers directly: every vertex sits on the one elevation passed in.
  const float xy[6] = {0.f, 0.f, 10.f, 0.f, 10.f, 10.f};
  std::vector<float> out;
  CadTessellateLinetypeChainVc(xy, 3, 42.f, /*closed=*/false, "Continuous", 1.f, kWhite, &out);

  REQUIRE(VertCount(out) == 4);
  for (size_t i = 0; i < VertCount(out); ++i)
    CHECK_THAT(VertZ(out, i), Catch::Matchers::WithinAbs(42.0, 1e-6));
}
