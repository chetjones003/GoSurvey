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

// REQ-050. These hold the rule the viewport text overlay applies, via the helper it calls — the
// previous "through viewport" case only asserted ModelToPaperIn and that a local struct still had the
// fontFamily it was given, so it passed whether or not the overlay used either one.
TEST_CASE("REQ-050: plain MTEXT through a viewport is sized by THAT viewport's scale", "[cadfont]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 50.f;  // 1" = 50'
  CadAnnotation m;
  m.kind = CadAnnotation::Kind::Mtext;
  m.plottedHeightInches = 0.25f;

  const float drawingMup = 100.f;  // deliberately NOT the viewport's scale
  REQUIRE(MtextScaleThroughViewport(m, vp, drawingMup) == Catch::Approx(50.f));

  // The point of the requirement: plotted height on the sheet is constant across viewport scales,
  // which means the MODEL height must track the viewport scale.
  Viewport vp100;
  vp100.scaleModelPerPaperIn = 100.f;
  const float h50 = m.plottedHeightInches * MtextScaleThroughViewport(m, vp, drawingMup);
  const float h100 = m.plottedHeightInches * MtextScaleThroughViewport(m, vp100, drawingMup);
  REQUIRE(h50 == Catch::Approx(12.5f));
  REQUIRE(h100 == Catch::Approx(25.f));
  REQUIRE(h100 == Catch::Approx(h50 * 2.f));
}

TEST_CASE("REQ-050: survey-point label MTEXT keeps the drawing scale through a viewport", "[cadfont]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 50.f;
  CadAnnotation label;
  label.kind = CadAnnotation::Kind::Mtext;
  label.plottedHeightInches = 0.25f;
  label.surveyPointLabelForId = 446;  // a survey label, not plain MTEXT

  const float drawingMup = 100.f;
  REQUIRE(MtextScaleThroughViewport(label, vp, drawingMup) == Catch::Approx(drawingMup));

  // And it is genuinely the exclusion doing the work, not a coincidence of the numbers: the same
  // annotation without the survey link takes the viewport's scale instead.
  CadAnnotation plain = label;
  plain.surveyPointLabelForId = -1;
  REQUIRE(MtextScaleThroughViewport(plain, vp, drawingMup) == Catch::Approx(50.f));
}

TEST_CASE("REQ-050: a degenerate viewport scale cannot produce a zero or negative text height",
          "[cadfont]") {
  CadAnnotation m;
  m.kind = CadAnnotation::Kind::Mtext;
  m.plottedHeightInches = 0.25f;
  Viewport bad;
  bad.scaleModelPerPaperIn = 0.f;  // a hand-edited .gs can carry this
  REQUIRE(MtextScaleThroughViewport(m, bad, 100.f) > 0.f);
  bad.scaleModelPerPaperIn = -25.f;
  REQUIRE(MtextScaleThroughViewport(m, bad, 100.f) > 0.f);
}
