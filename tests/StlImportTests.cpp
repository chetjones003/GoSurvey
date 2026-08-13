// STL import (REQ-065, the DWG conversion route).
//
// STL's two encodings are the whole risk here. An ASCII file starts with "solid" — and so can a
// BINARY file's 80-byte comment header, which real tools do write. Sniffing the prefix therefore
// misreads real files, and the misread is silent: a binary file parsed as ASCII yields zero
// triangles and looks like an empty model rather than an error.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "util/stlimport.hpp"

using Catch::Approx;

namespace {

template <typename T>
void Put(std::vector<std::uint8_t>* v, T x) {
  const size_t n = v->size();
  v->resize(n + sizeof(T));
  std::memcpy(v->data() + n, &x, sizeof(T));
}

/// Binary STL of one triangle, with an 80-byte header the caller chooses.
std::vector<std::uint8_t> BinaryStl(const std::string& header80, int triangles = 1) {
  std::vector<std::uint8_t> b;
  std::string h = header80;
  h.resize(80, '\0');
  b.insert(b.end(), h.begin(), h.end());
  Put<std::uint32_t>(&b, static_cast<std::uint32_t>(triangles));
  for (int t = 0; t < triangles; ++t) {
    Put<float>(&b, 0.f); Put<float>(&b, 0.f); Put<float>(&b, 1.f);          // normal +Z
    Put<float>(&b, 0.f); Put<float>(&b, 0.f); Put<float>(&b, 0.f);          // v0
    Put<float>(&b, 10.f); Put<float>(&b, 0.f); Put<float>(&b, 0.f);         // v1
    Put<float>(&b, 0.f); Put<float>(&b, 6.f); Put<float>(&b, 0.f);          // v2
    Put<std::uint16_t>(&b, 0);
  }
  return b;
}

std::vector<std::uint8_t> Bytes(const std::string& s) {
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

const char* kAsciiOneTriangle =
    "solid test\n"
    "  facet normal 0 0 1\n"
    "    outer loop\n"
    "      vertex 0 0 0\n"
    "      vertex 10 0 0\n"
    "      vertex 0 6 0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid test\n";

} // namespace

TEST_CASE("Binary and ASCII STL of the same triangle agree", "[stl]") {
  const modelimport::Result b = stl::ImportStlBytes(BinaryStl("binary header"), {});
  const modelimport::Result a = stl::ImportStlBytes(Bytes(kAsciiOneTriangle), {});
  REQUIRE(b.ok);
  REQUIRE(a.ok);
  CHECK(b.triangleCount() == 1);
  CHECK(a.triangleCount() == 1);
  REQUIRE(a.vertsXyz.size() == b.vertsXyz.size());
  for (size_t i = 0; i < a.vertsXyz.size(); ++i)
    CHECK(a.vertsXyz[i] == Approx(b.vertsXyz[i]).margin(1e-5));
}

TEST_CASE("A BINARY STL whose header begins \"solid\" is not misread as ASCII", "[stl]") {
  // The trap this module exists to avoid. Detection is by the length identity
  // (84 + 50·triangles == filesize), not by the prefix.
  const modelimport::Result r = stl::ImportStlBytes(BinaryStl("solid produced by some exporter", 2), {});
  REQUIRE(r.ok);
  CHECK(r.triangleCount() == 2);
}

TEST_CASE("Facet normals are normalised and preserved", "[stl]") {
  const modelimport::Result r = stl::ImportStlBytes(BinaryStl(""), {});
  REQUIRE(r.ok);
  REQUIRE(r.normalsXyz.size() == r.vertsXyz.size());
  for (size_t i = 0; i + 2 < r.normalsXyz.size(); i += 3) {
    CHECK(r.normalsXyz[i + 2] == Approx(1.f).margin(1e-5));
    CHECK(std::sqrt(r.normalsXyz[i]*r.normalsXyz[i] + r.normalsXyz[i+1]*r.normalsXyz[i+1] +
                    r.normalsXyz[i+2]*r.normalsXyz[i+2]) == Approx(1.f).margin(1e-4));
  }
}

TEST_CASE("Unit scale and insertion preserve REQ-101 at drawing-local coordinates", "[stl]") {
  // The insertion point is a LOCAL coordinate (world = local + worldDocumentOrigin). A survey
  // drawing's local coordinates stay small precisely so float32 geometry keeps its precision; the
  // state-plane magnitude lives in the document origin, in double, and never reaches a vertex.
  modelimport::Options opt;
  opt.unitScale = 1.0 / 12.0;   // inches -> feet, the Plant 3D case
  opt.insertX = 5000.0;         // a realistic local offset
  opt.insertY = -2500.0;
  const modelimport::Result r = stl::ImportStlBytes(BinaryStl(""), opt);
  REQUIRE(r.ok);
  const float span = r.vertsXyz[3] - r.vertsXyz[0];  // the 10-unit edge, scaled
  CHECK(span == Approx(10.0 / 12.0).margin(0.01));
  CHECK(r.vertsXyz[0] == Approx(5000.0).margin(0.01));
  CHECK(r.vertsXyz[1] == Approx(-2500.0).margin(0.01));
}

TEST_CASE("Mesh vertices cannot hold raw state-plane coordinates — and that is the origin's job",
          "[stl][precision]") {
  // This test documents a LIMIT rather than a feature, because the limit is invisible and the
  // failure it causes looks like a modelling error.
  //
  // float32 near a 6.5-million-foot easting has a spacing of 0.5 ft — fifty times REQ-101's
  // 0.01 ft. So a sub-foot feature CANNOT survive being stored at a raw state-plane coordinate, no
  // matter how carefully the import arithmetic is done in double. GoSurvey's answer is the
  // local-storage invariant: geometry is stored local and the big number lives once, in double, in
  // worldDocumentOrigin.
  //
  // It is asserted here so that nobody later "fixes" an import by passing a state-plane value as an
  // insertion point and concludes the importer is lossy.
  modelimport::Options opt;
  opt.unitScale = 1.0 / 12.0;
  opt.insertX = 6543210.0;  // deliberately wrong usage: a WORLD value as a LOCAL insertion point
  const modelimport::Result r = stl::ImportStlBytes(BinaryStl(""), opt);
  REQUIRE(r.ok);
  const float span = r.vertsXyz[3] - r.vertsXyz[0];
  // 0.8333 ft quantises to a whole ULP here. The point is that it does NOT meet REQ-101 —
  // if a future change made this pass, coordinates would have grown a wider type and this test
  // should be revisited rather than deleted.
  CHECK(std::fabs(span - 10.0 / 12.0) > 0.01);
}

TEST_CASE("STL reports that it carries no colours or names", "[stl]") {
  // REQ-201 / REQ-065: the format's real limitation is stated at import rather than discovered
  // when the model turns out to be one grey blob.
  const modelimport::Result r = stl::ImportStlBytes(BinaryStl(""), {});
  REQUIRE(r.ok);
  REQUIRE(r.parts.size() == 1);
  bool mentioned = false;
  for (const auto& s : r.skipped)
    mentioned = mentioned || s.find("colour") != std::string::npos;
  CHECK(mentioned);
}

TEST_CASE("Truncated and degenerate STL is refused with a reason", "[stl]") {
  // A binary file cut short no longer satisfies the length identity, so it is parsed as ASCII and
  // yields nothing — which must be an error, not an empty success.
  std::vector<std::uint8_t> cut = BinaryStl("", 4);
  cut.resize(cut.size() - 30);
  const modelimport::Result t = stl::ImportStlBytes(cut, {});
  CHECK_FALSE(t.ok);
  CHECK_FALSE(t.error.empty());

  CHECK_FALSE(stl::ImportStlBytes({}, {}).ok);
  CHECK_FALSE(stl::ImportStlBytes(Bytes("solid empty\nendsolid empty\n"), {}).ok);

  // An ASCII facet that ends mid-vertex must not silently produce a partial triangle.
  const modelimport::Result p = stl::ImportStlBytes(
      Bytes("solid s\n facet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n"), {});
  CHECK_FALSE(p.ok);
}

TEST_CASE("A large binary STL reads every triangle", "[stl]") {
  // Guards the index/offset arithmetic at a size where an off-by-one in the 50-byte record stride
  // would drift visibly rather than land on a valid-looking triangle.
  const modelimport::Result r = stl::ImportStlBytes(BinaryStl("", 5000), {});
  REQUIRE(r.ok);
  CHECK(r.triangleCount() == 5000);
  CHECK(r.vertexCount() == 15000);
  CHECK(r.parts[0].indexCount == 15000);
}
