#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "font/CadFontName.hpp"
#include "commands/DimensionStyle.hpp"
#include "commands/CadEntities.hpp"
#include "commands/PaperSpace.hpp"

TEST_CASE("SHX font names are classified by suffix, case-insensitive", "[cadfont]") {
  REQUIRE(cadfont::IsShxFontName("romans.shx"));
  REQUIRE(cadfont::IsShxFontName("SIMPLEX.SHX"));
  REQUIRE_FALSE(cadfont::IsShxFontName("Arial"));
  REQUIRE_FALSE(cadfont::IsShxFontName(""));
  REQUIRE_FALSE(cadfont::IsShxFontName("shx"));
}

TEST_CASE("Degree-bearing dimension text must not use SHX strokes", "[cadfont]") {
  REQUIRE(cadfont::ContainsUtf8Degree("90\xc2\xb0"));
  REQUIRE_FALSE(cadfont::ContainsUtf8Degree("90.00"));
  REQUIRE(cadfont::PreferShxStrokes("romans.shx", "12.50"));
  REQUIRE_FALSE(cadfont::PreferShxStrokes("romans.shx", "90\xc2\xb0"));
  REQUIRE_FALSE(cadfont::PreferShxStrokes("Arial", "12.50"));
  REQUIRE_FALSE(cadfont::PreferShxStrokes("", "12.50"));
}

TEST_CASE("DIMSTY bakes font onto each dimension kind immediately", "[cadfont]") {
  DimensionStyle sty;
  sty.textFont = "Times New Roman";
  sty.textSizeInches = 0.20f;
  CadAnnotation lin;
  lin.kind = CadAnnotation::Kind::DimLinear;
  lin.fontFamily = "Arial";
  lin.plottedHeightInches = 0.10f;
  CadAnnotation ang;
  ang.kind = CadAnnotation::Kind::DimAngular;
  CadAnnotation txt;
  txt.kind = CadAnnotation::Kind::Text;
  txt.fontFamily = "Courier New";
  DimensionStyles::BakeTextOntoDimension(lin, sty);
  DimensionStyles::BakeTextOntoDimension(ang, sty);
  DimensionStyles::BakeTextOntoDimension(txt, sty);
  REQUIRE(lin.fontFamily == "Times New Roman");
  REQUIRE(lin.plottedHeightInches == Catch::Approx(0.20f));
  REQUIRE(ang.fontFamily == "Times New Roman");
  REQUIRE(txt.fontFamily == "Courier New");  // TEXT is not a dimension
}

TEST_CASE("Model TEXT maps through a viewport like dimension labels", "[cadfont]") {
  Viewport vp;
  vp.paperXIn = 1.f;
  vp.paperYIn = 1.f;
  vp.paperWIn = 10.f;
  vp.paperHIn = 8.f;
  vp.modelCenterX = 50.0;
  vp.modelCenterY = 25.0;
  vp.scaleModelPerPaperIn = 50.f;
  CadAnnotation t;
  t.kind = CadAnnotation::Kind::Text;
  t.insX = 50.f;
  t.insY = 25.f;
  t.fontFamily = "Arial";
  t.text = "Aa";
  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, static_cast<double>(t.insX), static_cast<double>(t.insY), &px, &py);
  REQUIRE(px == Catch::Approx(6.f));
  REQUIRE(py == Catch::Approx(5.f));
  REQUIRE(t.fontFamily == "Arial");
}
