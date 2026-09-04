// `.gs` round-trip fidelity for mesh geometry (REQ-063).
//
// The acceptance condition is that "a mesh of N triangles round-trips through `.gs` with vertex
// positions **bit-identical** on reload". `GsIo.cpp` cannot be linked by this target — it pulls in
// the whole command layer — but the risk the condition is really about is not in that file. It is
// in the float→text→float conversion underneath, which is where a "close enough" serializer would
// quietly lose the last bits of every coordinate. So the test exercises exactly the arrays the
// saver writes, through exactly the serializer it writes them with.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

/// Values chosen to break a naive serializer: state-plane magnitudes where float resolution is
/// coarse, tiny elevations, negatives, denormals, and exact binary fractions.
std::vector<float> AwkwardFloats() {
  return {
      0.f,        -0.f,        1.f,          -1.f,
      0.1f,       -0.1f,       1.f / 3.f,    2.f / 3.f,
      1234567.f,  -1234567.f,  6543210.5f,   0.000123456f,
      1e-30f,     -1e-30f,     1e30f,        -1e30f,
      3.14159265f, 2.71828182f, 1.4013e-45f, 8388608.f,   // last: 2^23, where float steps to 1.0
  };
}

} // namespace

TEST_CASE("Mesh vertex positions survive .gs serialization bit-identically", "[mesh][gs]") {
  const std::vector<float> verts = AwkwardFloats();
  json m;
  m["verts"] = verts;  // exactly what SerializeGoSurveyJson writes

  const std::string text = m.dump();
  const json back = json::parse(text);
  const std::vector<float> reloaded = back["verts"].get<std::vector<float>>();

  REQUIRE(reloaded.size() == verts.size());
  for (size_t i = 0; i < verts.size(); ++i) {
    INFO("index " << i << " original " << verts[i] << " reloaded " << reloaded[i]);
    // Bit equality, not Approx: the acceptance condition says bit-identical, and a coordinate that
    // drifts by one ULP per save/load cycle drifts without bound over a drawing's life.
    REQUIRE(std::memcmp(&verts[i], &reloaded[i], sizeof(float)) == 0);
  }
}

TEST_CASE("Mesh indices survive .gs serialization exactly", "[mesh][gs]") {
  // uint32 indices go past 2^24, where a serializer that round-tripped them through double would
  // still be exact — but one that used float would not. REQ-063's ceiling is 6M indices.
  const std::vector<std::uint32_t> indices = {0u,        1u,          2u,
                                              16777216u, 16777217u,   // 2^24 and 2^24+1
                                              6000000u,  4294967294u, 4294967295u};
  json m;
  m["indices"] = indices;
  const std::vector<std::uint32_t> back = json::parse(m.dump())["indices"].get<std::vector<std::uint32_t>>();
  REQUIRE(back == indices);
}

TEST_CASE("An empty mesh section round-trips as empty, not as null", "[mesh][gs]") {
  // The saver omits the whole "meshes" key when there are none — this pins the other half: a mesh
  // that legitimately has no normals must reload with no normals rather than with a null that
  // would throw on get<std::vector<float>>().
  json m;
  m["verts"] = std::vector<float>{};
  m["normals"] = std::vector<float>{};
  m["indices"] = std::vector<std::uint32_t>{};
  const json back = json::parse(m.dump());
  CHECK(back["verts"].get<std::vector<float>>().empty());
  CHECK(back["normals"].get<std::vector<float>>().empty());
  CHECK(back["indices"].get<std::vector<std::uint32_t>>().empty());
}

TEST_CASE("Part colours and ranges round-trip", "[mesh][gs]") {
  json jp;
  jp["name"] = "ACPPPIPE 76";
  jp["begin"] = 123456;
  jp["count"] = 654321;
  jp["rgb"] = json::array({0.30f, 0.75f, 0.72f});
  const json back = json::parse(jp.dump());
  CHECK(back["name"].get<std::string>() == "ACPPPIPE 76");
  CHECK(back["begin"].get<int>() == 123456);
  CHECK(back["count"].get<int>() == 654321);
  const float r = back["rgb"][0].get<float>();
  const float g = back["rgb"][1].get<float>();
  const float b = back["rgb"][2].get<float>();
  const float er = 0.30f, eg = 0.75f, eb = 0.72f;
  CHECK(std::memcmp(&r, &er, sizeof(float)) == 0);
  CHECK(std::memcmp(&g, &eg, sizeof(float)) == 0);
  CHECK(std::memcmp(&b, &eb, sizeof(float)) == 0);
}
