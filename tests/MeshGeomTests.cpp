// Mesh validation, bounds and normals (REQ-063).
//
// This module guards the boundary between an imported file and the GPU. An index that overruns the
// vertex array is not a wrong picture — it is an out-of-bounds read inside the driver, which does
// not fail politely and does not fail reproducibly. So the validator is tested at its edges rather
// than on happy paths.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "util/meshgeom.hpp"

using Catch::Approx;
using meshgeom::MeshProblem;

namespace {

/// A unit square in the XY plane as two triangles, at elevation \p z.
void UnitQuad(std::vector<float>* v, std::vector<std::uint32_t>* i, float z = 0.f) {
  *v = {0.f, 0.f, z, 1.f, 0.f, z, 1.f, 1.f, z, 0.f, 1.f, z};
  *i = {0, 1, 2, 0, 2, 3};
}

} // namespace

TEST_CASE("Bounds cover every vertex, in three dimensions", "[mesh]") {
  const std::vector<float> v = {1.f, 2.f, 3.f, -4.f, 10.f, 0.f, 7.f, -1.f, 6.f};
  const meshgeom::Bounds b = meshgeom::ComputeBounds(v);
  REQUIRE(b.valid);
  CHECK(b.mnX == Approx(-4.f));  CHECK(b.mxX == Approx(7.f));
  CHECK(b.mnY == Approx(-1.f));  CHECK(b.mxY == Approx(10.f));
  CHECK(b.mnZ == Approx(0.f));   CHECK(b.mxZ == Approx(6.f));
}

TEST_CASE("An empty mesh has INVALID bounds, not bounds at the origin", "[mesh]") {
  // The distinction matters: REQ-063 puts meshes into zoom-extents, and a mesh reporting a valid
  // (0,0,0) box would silently drag the framing of every drawing that contains one to the origin.
  CHECK_FALSE(meshgeom::ComputeBounds({}).valid);
  CHECK_FALSE(meshgeom::ComputeBounds({1.f, 2.f}).valid);       // not a whole triple
  CHECK_FALSE(meshgeom::ComputeBounds({1.f, 2.f, 3.f, 4.f}).valid);
}

TEST_CASE("ExpandBounds merges, and ignores invalid operands", "[mesh]") {
  meshgeom::Bounds acc;
  meshgeom::ExpandBounds(&acc, meshgeom::ComputeBounds({}));
  CHECK_FALSE(acc.valid);  // merging nothing into nothing is still nothing
  meshgeom::ExpandBounds(&acc, meshgeom::ComputeBounds({0.f, 0.f, 0.f}));
  REQUIRE(acc.valid);
  meshgeom::ExpandBounds(&acc, meshgeom::ComputeBounds({5.f, -5.f, 2.f}));
  CHECK(acc.mxX == Approx(5.f));
  CHECK(acc.mnY == Approx(-5.f));
  meshgeom::ExpandBounds(&acc, meshgeom::Bounds{});  // invalid: must not reset the accumulator
  CHECK(acc.mxX == Approx(5.f));
}

TEST_CASE("A well-formed mesh validates", "[mesh]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  UnitQuad(&v, &i);
  std::vector<float> n(v.size(), 0.f);
  CHECK(meshgeom::ValidateMesh(v, n, i, {{0, 6}}) == MeshProblem::Ok);
  CHECK(meshgeom::ValidateMesh(v, {}, i, {}) == MeshProblem::Ok);  // normals may be absent
}

TEST_CASE("An index past the last vertex is rejected", "[mesh]") {
  // The case that matters most: this is an out-of-bounds GPU read if it gets through.
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  UnitQuad(&v, &i);
  i[3] = 4;  // only vertices 0..3 exist
  CHECK(meshgeom::ValidateMesh(v, {}, i, {}) == MeshProblem::IndexOutOfRange);
  i[3] = 0xFFFFFFFFu;  // and the value a truncated file most often produces
  CHECK(meshgeom::ValidateMesh(v, {}, i, {}) == MeshProblem::IndexOutOfRange);
}

TEST_CASE("Structural malformations are each reported distinctly", "[mesh]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  UnitQuad(&v, &i);

  std::vector<float> shortVerts = v;
  shortVerts.pop_back();  // truncated mid-vertex
  CHECK(meshgeom::ValidateMesh(shortVerts, {}, {}, {}) == MeshProblem::VertsNotTriples);

  std::vector<float> partialNormals(v.size() - 3, 0.f);
  CHECK(meshgeom::ValidateMesh(v, partialNormals, i, {}) == MeshProblem::NormalsMismatch);

  std::vector<std::uint32_t> partialTri = {0, 1};
  CHECK(meshgeom::ValidateMesh(v, {}, partialTri, {}) == MeshProblem::IndicesNotTriangles);

  CHECK(meshgeom::ValidateMesh(v, {}, i, {{0, 9}}) == MeshProblem::PartRangeOutOfBounds);
  CHECK(meshgeom::ValidateMesh(v, {}, i, {{-3, 3}}) == MeshProblem::PartRangeOutOfBounds);
  CHECK(meshgeom::ValidateMesh(v, {}, i, {{0, 4}}) == MeshProblem::PartCountNotTriangles);

  // Every problem has text; a report of "unknown problem" would be a missed enum case.
  for (MeshProblem p : {MeshProblem::VertsNotTriples, MeshProblem::NormalsMismatch,
                        MeshProblem::IndicesNotTriangles, MeshProblem::IndexOutOfRange,
                        MeshProblem::PartRangeOutOfBounds, MeshProblem::PartCountNotTriangles})
    CHECK(std::string(meshgeom::MeshProblemText(p)) != "unknown problem");
}

TEST_CASE("Vertex normals point out of the surface they belong to", "[mesh]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  UnitQuad(&v, &i, 3.f);
  std::vector<float> n;
  meshgeom::ComputeVertexNormals(v, i, &n);
  REQUIRE(n.size() == v.size());
  for (size_t k = 0; k + 2 < n.size(); k += 3) {
    CHECK(n[k] == Approx(0.f).margin(1e-6));
    CHECK(n[k + 1] == Approx(0.f).margin(1e-6));
    CHECK(n[k + 2] == Approx(1.f).margin(1e-6));  // wound CCW seen from +Z
  }
}

TEST_CASE("Normals are unit length, including for unreferenced vertices", "[mesh]") {
  // A zero-length normal reaches the shader as a black fragment, which reads as a hole in the
  // model rather than as an unused vertex — so the degenerate case gets +Z, not zero.
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  UnitQuad(&v, &i);
  v.insert(v.end(), {9.f, 9.f, 9.f});  // a fifth vertex no triangle references
  std::vector<float> n;
  meshgeom::ComputeVertexNormals(v, i, &n);
  REQUIRE(n.size() == v.size());
  for (size_t k = 0; k + 2 < n.size(); k += 3)
    CHECK(std::sqrt(n[k] * n[k] + n[k + 1] * n[k + 1] + n[k + 2] * n[k + 2]) == Approx(1.0).margin(1e-5));
}

TEST_CASE("Normal computation is area-weighted", "[mesh]") {
  // Two triangles meeting at a shared vertex, one large and one small, in different planes. The
  // shared normal must lean toward the LARGE face — normalising each face first would give the
  // sliver an equal vote and shade a coarse tessellation wrong.
  //  v0 at origin; big triangle in XY (+Z normal), small triangle in XZ (-Y normal).
  std::vector<float> v = {0.f, 0.f, 0.f,   10.f, 0.f, 0.f,   0.f, 10.f, 0.f,
                          0.1f, 0.f, 0.1f, 0.f, 0.f, 0.1f};
  std::vector<std::uint32_t> i = {0, 1, 2, 0, 3, 4};
  std::vector<float> n;
  meshgeom::ComputeVertexNormals(v, i, &n);
  REQUIRE(n.size() == v.size());
  CHECK(std::fabs(n[2]) > 0.9f);  // v0's normal is dominated by the 50-area face, not the 0.005 one
}

TEST_CASE("Degenerate input cannot read out of bounds", "[mesh]") {
  std::vector<float> n;
  meshgeom::ComputeVertexNormals({}, {}, &n);
  CHECK(n.empty());
  meshgeom::ComputeVertexNormals({0.f, 0.f, 0.f}, {0, 1, 2}, &n);  // indices past the only vertex
  REQUIRE(n.size() == 3);
  meshgeom::ComputeVertexNormals({0.f, 0.f}, {0, 1, 2}, &n);       // not a whole vertex
  meshgeom::ComputeVertexNormals({0.f, 0.f, 0.f}, {0, 1}, nullptr);  // null out
  SUCCEED("no out-of-bounds access");
}
