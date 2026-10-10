// glTF 2.0 / GLB import (REQ-065).
//
// Every fixture here is built byte-by-byte in memory rather than checked in as a binary, so what is
// being tested is visible in the test: which accessor type, which node transform, which truncation.
// A checked-in .glb would test the same code and explain none of it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "util/gltfimport.hpp"

using Catch::Approx;
using json = nlohmann::json;

namespace {

constexpr double kReq101 = 0.01;

/// Append raw bytes of a POD value.
template <typename T>
void Put(std::vector<std::uint8_t>* v, T x) {
  const size_t n = v->size();
  v->resize(n + sizeof(T));
  std::memcpy(v->data() + n, &x, sizeof(T));
}

/// Wrap a JSON document + binary blob into a GLB container (spec §4.4).
std::vector<std::uint8_t> MakeGlb(const json& doc, const std::vector<std::uint8_t>& bin) {
  std::string js = doc.dump();
  while (js.size() % 4 != 0)
    js.push_back(' ');  // chunks are 4-byte aligned; JSON pads with spaces
  std::vector<std::uint8_t> binPad = bin;
  while (binPad.size() % 4 != 0)
    binPad.push_back(0);

  std::vector<std::uint8_t> out;
  const std::uint32_t total = static_cast<std::uint32_t>(12 + 8 + js.size() + (binPad.empty() ? 0 : 8 + binPad.size()));
  Put<std::uint32_t>(&out, 0x46546C67u);  // 'glTF'
  Put<std::uint32_t>(&out, 2u);
  Put<std::uint32_t>(&out, total);
  Put<std::uint32_t>(&out, static_cast<std::uint32_t>(js.size()));
  Put<std::uint32_t>(&out, 0x4E4F534Au);  // 'JSON'
  out.insert(out.end(), js.begin(), js.end());
  if (!binPad.empty()) {
    Put<std::uint32_t>(&out, static_cast<std::uint32_t>(binPad.size()));
    Put<std::uint32_t>(&out, 0x004E4942u);  // 'BIN'
    out.insert(out.end(), binPad.begin(), binPad.end());
  }
  return out;
}

/// One triangle: positions at the given glTF-space (Y-up) coordinates, uint16 indices.
struct SimpleAsset {
  json doc;
  std::vector<std::uint8_t> bin;
};

SimpleAsset MakeTriangleAsset(const std::vector<float>& positionsYUp, bool withNormals = false,
                              const json& extraNodes = json::array()) {
  SimpleAsset a;
  std::vector<std::uint8_t> bin;
  const size_t posOffset = 0;
  for (float f : positionsYUp)
    Put<float>(&bin, f);
  const size_t nrmOffset = bin.size();
  if (withNormals)
    for (size_t i = 0; i < positionsYUp.size() / 3; ++i) {
      Put<float>(&bin, 0.f);
      Put<float>(&bin, 1.f);
      Put<float>(&bin, 0.f);  // +Y in glTF = "up" = +Z in CAD
    }
  while (bin.size() % 4 != 0) bin.push_back(0);
  const size_t idxOffset = bin.size();
  const size_t vcount = positionsYUp.size() / 3;
  for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(vcount); ++i)
    Put<std::uint16_t>(&bin, i);
  while (bin.size() % 4 != 0) bin.push_back(0);

  json accessors = json::array();
  accessors.push_back({{"bufferView", 0}, {"componentType", 5126}, {"count", vcount}, {"type", "VEC3"}});
  if (withNormals)
    accessors.push_back({{"bufferView", 1}, {"componentType", 5126}, {"count", vcount}, {"type", "VEC3"}});
  accessors.push_back({{"bufferView", withNormals ? 2 : 1}, {"componentType", 5123}, {"count", vcount},
                       {"type", "SCALAR"}});

  json views = json::array();
  views.push_back({{"buffer", 0}, {"byteOffset", posOffset}, {"byteLength", positionsYUp.size() * 4}});
  if (withNormals)
    views.push_back({{"buffer", 0}, {"byteOffset", nrmOffset}, {"byteLength", vcount * 12}});
  views.push_back({{"buffer", 0}, {"byteOffset", idxOffset}, {"byteLength", vcount * 2}});

  json attrs = {{"POSITION", 0}};
  if (withNormals)
    attrs["NORMAL"] = 1;
  json prim = {{"attributes", attrs}, {"indices", withNormals ? 2 : 1}, {"mode", 4}};

  a.doc = {
      {"asset", {{"version", "2.0"}}},
      {"buffers", json::array({{{"byteLength", bin.size()}}})},
      {"bufferViews", views},
      {"accessors", accessors},
      {"meshes", json::array({{{"primitives", json::array({prim})}}})},
  };
  json nodes = json::array({{{"mesh", 0}, {"name", "tri"}}});
  for (const auto& n : extraNodes)
    nodes.push_back(n);
  a.doc["nodes"] = nodes;
  a.doc["scenes"] = json::array({{{"nodes", json::array({0})}}});
  a.doc["scene"] = 0;
  a.bin = std::move(bin);
  return a;
}

gltf::ImportResult ImportAsset(const SimpleAsset& a, const gltf::ImportOptions& opt = {}) {
  return gltf::ImportGltfBytes(MakeGlb(a.doc, a.bin), true, "", opt);
}

} // namespace

// --- coordinate system --------------------------------------------------------------------------

TEST_CASE("glTF Y-up becomes CAD Z-up", "[gltf]") {
  // The single most consequential line in the importer: get it wrong and every model arrives lying
  // on its side, which reads as a modelling error rather than an importer bug.
  double x = 0, y = 0, z = 0;
  gltf::GltfYUpToCadZUp(0, 1, 0, &x, &y, &z);  // "up" in glTF
  CHECK(x == Approx(0.0)); CHECK(y == Approx(0.0)); CHECK(z == Approx(1.0));   // → up in CAD
  gltf::GltfYUpToCadZUp(0, 0, 1, &x, &y, &z);  // toward the viewer in glTF
  CHECK(x == Approx(0.0)); CHECK(y == Approx(-1.0)); CHECK(z == Approx(0.0));  // → south in CAD
  gltf::GltfYUpToCadZUp(1, 0, 0, &x, &y, &z);  // right stays right
  CHECK(x == Approx(1.0)); CHECK(y == Approx(0.0)); CHECK(z == Approx(0.0));
}

TEST_CASE("A model's height arrives along Z, not Y", "[gltf]") {
  // A triangle 10 units "tall" in glTF must be 10 units tall in elevation.
  const SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 10, 0});
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  REQUIRE(r.vertexCount() == 3);
  CHECK(r.vertsXyz[8] == Approx(10.f).margin(kReq101));  // third vertex, Z
  CHECK(r.vertsXyz[7] == Approx(0.f).margin(kReq101));   // ...and not in Y
}

// --- geometry, counts, scale --------------------------------------------------------------------

TEST_CASE("Triangle count and bounding box match the source after unit scale", "[gltf]") {
  // A 12-unit triangle imported at 1/12 scale (inches → feet) must measure 1 foot.
  const SimpleAsset a = MakeTriangleAsset({0, 0, 0, 12, 0, 0, 0, 0, 12});
  gltf::ImportOptions opt;
  opt.unitScale = 1.0 / 12.0;
  const gltf::ImportResult r = ImportAsset(a, opt);
  REQUIRE(r.ok);
  CHECK(r.triangleCount() == 1);
  float mnX = r.vertsXyz[0], mxX = r.vertsXyz[0], mnY = r.vertsXyz[1], mxY = r.vertsXyz[1];
  for (size_t i = 0; i + 2 < r.vertsXyz.size(); i += 3) {
    mnX = std::min(mnX, r.vertsXyz[i]);     mxX = std::max(mxX, r.vertsXyz[i]);
    mnY = std::min(mnY, r.vertsXyz[i + 1]); mxY = std::max(mxY, r.vertsXyz[i + 1]);
  }
  CHECK(mxX - mnX == Approx(1.0).margin(kReq101));
  CHECK(mxY - mnY == Approx(1.0).margin(kReq101));  // the glTF +Z extent became CAD -Y
}

TEST_CASE("Insertion and scale preserve REQ-101 at drawing-local coordinates", "[gltf]") {
  // The insertion point is a LOCAL coordinate (world = local + worldDocumentOrigin), and local
  // coordinates stay small precisely so float32 geometry keeps its precision.
  //
  // An earlier version of this test used a 6.5-million-foot insertion with a 1-unit edge and
  // passed — but only because 1.0 happens to be exactly two float ULPs at that magnitude. It was
  // asserting luck, not precision. The real limit is pinned in StlImportTests
  // ("cannot hold raw state-plane coordinates"), and the state-plane magnitude is the document
  // origin's job, in double, where it never reaches a vertex.
  const SimpleAsset a = MakeTriangleAsset({0, 0, 0, 12, 0, 0, 0, 0, 12});
  gltf::ImportOptions opt;
  opt.unitScale = 1.0 / 12.0;  // inches -> feet
  opt.insertX = 5000.0;
  opt.insertY = -2500.0;
  const gltf::ImportResult r = ImportAsset(a, opt);
  REQUIRE(r.ok);
  const float span = r.vertsXyz[3] - r.vertsXyz[0];  // the 12-inch edge = 1 ft
  CHECK(span == Approx(1.0).margin(kReq101));
  CHECK(r.vertsXyz[0] == Approx(5000.0).margin(kReq101));
  CHECK(r.vertsXyz[1] == Approx(-2500.0).margin(kReq101));
}

// --- node hierarchy ------------------------------------------------------------------------------

TEST_CASE("A doubly-nested node transform lands where hand computation says", "[gltf]") {
  // parent translate (100,0,0) → child translate (0,5,0) → grandchild holding the mesh at (0,0,7).
  // Composed in glTF space that is (100, 5, 7); in CAD space (100, -7, 5).
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["nodes"] = json::array({
      {{"name", "parent"}, {"translation", json::array({100.0, 0.0, 0.0})}, {"children", json::array({1})}},
      {{"name", "child"}, {"translation", json::array({0.0, 5.0, 0.0})}, {"children", json::array({2})}},
      {{"name", "grandchild"}, {"translation", json::array({0.0, 0.0, 7.0})}, {"mesh", 0}},
  });
  a.doc["scenes"] = json::array({{{"nodes", json::array({0})}}});
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  REQUIRE(r.vertexCount() == 3);
  CHECK(r.vertsXyz[0] == Approx(100.0).margin(kReq101));
  CHECK(r.vertsXyz[1] == Approx(-7.0).margin(kReq101));
  CHECK(r.vertsXyz[2] == Approx(5.0).margin(kReq101));
  REQUIRE(r.parts.size() == 1);
  CHECK(r.parts[0].name == "grandchild");
}

TEST_CASE("A node matrix is read as column-major", "[gltf]") {
  // glTF stores `matrix` column-major (spec §3.5.2). Reading it row-major transposes the rotation
  // and puts the translation in the wrong place — a mistake that looks like "the model is scattered".
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["nodes"] = json::array({{{"name", "m"},
                                 {"matrix", json::array({1, 0, 0, 0,
                                                         0, 1, 0, 0,
                                                         0, 0, 1, 0,
                                                         20, 30, 40, 1})},  // translation in the LAST column
                                 {"mesh", 0}}});
  a.doc["scenes"] = json::array({{{"nodes", json::array({0})}}});
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  CHECK(r.vertsXyz[0] == Approx(20.0).margin(kReq101));
  CHECK(r.vertsXyz[1] == Approx(-40.0).margin(kReq101));  // glTF +Z → CAD −Y
  CHECK(r.vertsXyz[2] == Approx(30.0).margin(kReq101));   // glTF +Y → CAD +Z
}

// --- names and colours ---------------------------------------------------------------------------

TEST_CASE("Node names and base colours survive", "[gltf]") {
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["materials"] = json::array({{{"name", "steel"},
                                     {"pbrMetallicRoughness",
                                      {{"baseColorFactor", json::array({0.25, 0.5, 0.75, 1.0})}}}}});
  a.doc["meshes"][0]["primitives"][0]["material"] = 0;
  a.doc["nodes"][0]["name"] = "PIPE-101";
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  REQUIRE(r.parts.size() == 1);
  CHECK(r.parts[0].name == "PIPE-101");
  CHECK(r.parts[0].r == Approx(0.25f));
  CHECK(r.parts[0].g == Approx(0.5f));
  CHECK(r.parts[0].b == Approx(0.75f));
}

TEST_CASE("Separate nodes become separate parts", "[gltf]") {
  // The property that keeps an imported model from being one undifferentiated blob (REQ-065).
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["nodes"] = json::array({
      {{"name", "A"}, {"mesh", 0}},
      {{"name", "B"}, {"mesh", 0}, {"translation", json::array({50.0, 0.0, 0.0})}},
  });
  a.doc["scenes"] = json::array({{{"nodes", json::array({0, 1})}}});
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  REQUIRE(r.parts.size() == 2);
  CHECK(r.parts[0].name == "A");
  CHECK(r.parts[1].name == "B");
  CHECK(r.parts[0].indexCount == 3);
  CHECK(r.parts[1].indexCount == 3);
  CHECK(r.vertexCount() == 6);  // each node instances its own copy of the mesh
}

// --- reporting what was skipped -------------------------------------------------------------------

TEST_CASE("Textures and animation are imported-around and REPORTED", "[gltf]") {
  // REQ-065/REQ-201: geometry comes in, and the log says what did not.
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["animations"] = json::array({json::object(), json::object()});
  a.doc["textures"] = json::array({json::object()});
  a.doc["images"] = json::array({json::object()});
  a.doc["cameras"] = json::array({json::object()});
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);                 // the geometry still imports
  CHECK(r.triangleCount() == 1);
  const std::string all = [&] {
    std::string s;
    for (const auto& k : r.skipped) s += k + ";";
    return s;
  }();
  INFO("skipped: " << all);
  CHECK(all.find("animations") != std::string::npos);
  CHECK(all.find("textures") != std::string::npos);
  CHECK(all.find("cameras") != std::string::npos);
}

// --- rejection, with a reason ---------------------------------------------------------------------

TEST_CASE("A truncated GLB is rejected with a specific reason", "[gltf]") {
  const SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  std::vector<std::uint8_t> glb = MakeGlb(a.doc, a.bin);
  glb.resize(glb.size() / 2);  // cut it in half
  const gltf::ImportResult r = gltf::ImportGltfBytes(glb, true, "", {});
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
  INFO("error: " << r.error);
  CHECK(r.error.find("truncated") != std::string::npos);
  CHECK(r.vertsXyz.empty());  // nothing partial survives
}

TEST_CASE("Non-glTF bytes are rejected without reading past the end", "[gltf]") {
  for (const std::vector<std::uint8_t>& bad :
       {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{1, 2, 3},
        std::vector<std::uint8_t>{'n', 'o', 'p', 'e', 0, 0, 0, 0, 0, 0, 0, 0}}) {
    const gltf::ImportResult r = gltf::ImportGltfBytes(bad, true, "", {});
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
  }
}

TEST_CASE("An out-of-range index is caught by the importer, not the GPU", "[gltf]") {
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  // Claim four indices from an accessor that only has three vertices' worth behind it.
  a.doc["accessors"][1]["count"] = 6;
  const gltf::ImportResult r = ImportAsset(a);
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
}

TEST_CASE("A cyclic node hierarchy is refused rather than recursed", "[gltf]") {
  // Recursion on a cycle is a stack overflow, which is a crash rather than a message.
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["nodes"] = json::array({
      {{"name", "a"}, {"children", json::array({1})}},
      {{"name", "b"}, {"children", json::array({0})}, {"mesh", 0}},
  });
  a.doc["scenes"] = json::array({{{"nodes", json::array({0})}}});
  const gltf::ImportResult r = ImportAsset(a);
  CHECK_FALSE(r.ok);
  INFO("error: " << r.error);
  CHECK(r.error.find("cycle") != std::string::npos);
}

TEST_CASE("A file with no triangle geometry is rejected, not imported empty", "[gltf]") {
  json doc = {{"asset", {{"version", "2.0"}}}, {"nodes", json::array()}, {"scenes", json::array()}};
  const gltf::ImportResult r = gltf::ImportGltfBytes(MakeGlb(doc, {}), true, "", {});
  CHECK_FALSE(r.ok);
  CHECK(r.error.find("no triangle geometry") != std::string::npos);
}

TEST_CASE("A wrong glTF version is named in the error", "[gltf]") {
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  a.doc["asset"]["version"] = "1.0";
  const gltf::ImportResult r = ImportAsset(a);
  CHECK_FALSE(r.ok);
  CHECK(r.error.find("1.0") != std::string::npos);
}

// --- normals --------------------------------------------------------------------------------------

TEST_CASE("Supplied normals are rotated into CAD space", "[gltf]") {
  const SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 0, 1}, /*withNormals=*/true);
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  REQUIRE(r.normalsXyz.size() == r.vertsXyz.size());
  // The fixture's normals are +Y in glTF, i.e. "up" — which must arrive as +Z.
  CHECK(r.normalsXyz[2] == Approx(1.f).margin(1e-5));
  CHECK(r.normalsXyz[1] == Approx(0.f).margin(1e-5));
}

TEST_CASE("Missing normals are computed rather than left black", "[gltf]") {
  // A zero normal reaches REQ-064's shader as an unlit fragment, which reads as a hole in the model.
  const SimpleAsset a = MakeTriangleAsset({0, 0, 0, 10, 0, 0, 0, 0, 10}, /*withNormals=*/false);
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  REQUIRE(r.normalsXyz.size() == r.vertsXyz.size());
  for (size_t i = 0; i + 2 < r.normalsXyz.size(); i += 3) {
    const float len = std::sqrt(r.normalsXyz[i] * r.normalsXyz[i] + r.normalsXyz[i + 1] * r.normalsXyz[i + 1] +
                                r.normalsXyz[i + 2] * r.normalsXyz[i + 2]);
    CHECK(len == Approx(1.0f).margin(1e-4));
  }
}

// --- accessor variety --------------------------------------------------------------------------------

TEST_CASE("uint32 indices are read as well as uint16", "[gltf]") {
  // Real exports of large models use uint32; a reader that assumed uint16 would silently produce
  // garbage triangles rather than fail.
  std::vector<std::uint8_t> bin;
  for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
    Put<float>(&bin, f);
  const size_t idxOff = bin.size();
  for (std::uint32_t i : {0u, 1u, 2u})
    Put<std::uint32_t>(&bin, i);

  json doc = {
      {"asset", {{"version", "2.0"}}},
      {"buffers", json::array({{{"byteLength", bin.size()}}})},
      {"bufferViews", json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                   {{"buffer", 0}, {"byteOffset", idxOff}, {"byteLength", 12}}})},
      {"accessors", json::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                                 {{"bufferView", 1}, {"componentType", 5125}, {"count", 3}, {"type", "SCALAR"}}})},
      {"meshes", json::array({{{"primitives", json::array({{{"attributes", {{"POSITION", 0}}}, {"indices", 1},
                                                            {"mode", 4}}})}}})},
      {"nodes", json::array({{{"mesh", 0}, {"name", "u32"}}})},
      {"scenes", json::array({{{"nodes", json::array({0})}}})},
  };
  const gltf::ImportResult r = gltf::ImportGltfBytes(MakeGlb(doc, bin), true, "", {});
  REQUIRE(r.ok);
  CHECK(r.triangleCount() == 1);
}

TEST_CASE("Point and line primitives are skipped and reported, not drawn as triangles", "[gltf]") {
  SimpleAsset a = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  SimpleAsset b = MakeTriangleAsset({0, 0, 0, 1, 0, 0, 0, 1, 0});
  b.doc["meshes"][0]["primitives"][0]["mode"] = 1;  // LINES
  const gltf::ImportResult rl = ImportAsset(b);
  CHECK_FALSE(rl.ok);  // nothing but lines → no geometry at all
  CHECK(rl.error.find("no triangle geometry") != std::string::npos);

  // A mesh with BOTH a triangle and a line primitive keeps the triangle and reports the rest.
  a.doc["meshes"][0]["primitives"].push_back(a.doc["meshes"][0]["primitives"][0]);
  a.doc["meshes"][0]["primitives"][1]["mode"] = 0;  // POINTS
  const gltf::ImportResult r = ImportAsset(a);
  REQUIRE(r.ok);
  CHECK(r.triangleCount() == 1);
  bool mentioned = false;
  for (const auto& s : r.skipped)
    mentioned = mentioned || s.find("point/line") != std::string::npos;
  CHECK(mentioned);
}
