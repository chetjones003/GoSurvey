// Issue #125 — DIMANGULAR must round-trip through .gs (kind + angle vertex).
//
// GsIo.cpp is not linked here. These tests exercise the same JSON helpers Save/LoadGoSurveyFile
// call (`GsAnnotationJson.hpp`), which is where the defect lived: Kind::DimAngular fell through
// to "text" and dimAngVertexX/Y were never written.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "GsAnnotationJson.hpp"

#include <nlohmann/json.hpp>

using nlohmann::json;

TEST_CASE("DIMANGULAR kind and vertex survive annotation JSON (issue #125)", "[gs][dimangular]") {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAngular;
  a.insX = 12.5f;
  a.insY = 8.25f;
  a.plottedHeightInches = 0.1f;
  a.fontFamily = "Arial";
  a.text = "90.0000\xc2\xb0";
  a.dimExt1X = 50.f;
  a.dimExt1Y = 0.f;
  a.dimExt2X = 0.f;
  a.dimExt2Y = 50.f;
  a.dimAngVertexX = 10.f;
  a.dimAngVertexY = 20.f;
  a.dimSignedOffset = 15.f;

  json o;
  CadAnnotationToJson(a, o);
  REQUIRE(o.at("kind") == "dimangular");
  REQUIRE(o.at("dimAngVertexX").get<float>() == Catch::Approx(10.f));
  REQUIRE(o.at("dimAngVertexY").get<float>() == Catch::Approx(20.f));

  const CadAnnotation b = CadAnnotationFromJson(o);
  REQUIRE(b.kind == CadAnnotation::Kind::DimAngular);
  REQUIRE(b.dimAngVertexX == Catch::Approx(10.f));
  REQUIRE(b.dimAngVertexY == Catch::Approx(20.f));
  REQUIRE(b.dimExt1X == Catch::Approx(50.f));
  REQUIRE(b.dimExt2Y == Catch::Approx(50.f));
  REQUIRE(b.text == a.text);
  REQUIRE(b.fontFamily == "Arial");
  REQUIRE(b.dimSignedOffset == Catch::Approx(15.f));
}

TEST_CASE("kind text stays TEXT and is not promoted to DIMANGULAR (issue #125)", "[gs][dimangular]") {
  json o;
  o["kind"] = "text";
  o["text"] = "just a label";
  o["insX"] = 1.f;
  o["insY"] = 2.f;
  o["dimAngVertexX"] = 99.f;
  o["dimAngVertexY"] = 88.f;

  const CadAnnotation a = CadAnnotationFromJson(o);
  REQUIRE(a.kind == CadAnnotation::Kind::Text);
  REQUIRE(a.text == "just a label");
  REQUIRE(AnnotationKindTag(a.kind) == std::string("text"));
}

TEST_CASE("unknown annotation kind tag loads as TEXT without crashing (issue #125)", "[gs][dimangular]") {
  json o;
  o["kind"] = "not-a-real-kind";
  const CadAnnotation a = CadAnnotationFromJson(o);
  REQUIRE(a.kind == CadAnnotation::Kind::Text);
}
