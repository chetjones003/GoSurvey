#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "font/CadFontName.hpp"
#include "commands/DimensionStyle.hpp"
#include "commands/CadEntities.hpp"
#include "commands/PaperSpace.hpp"
#include "commands/CadCommands.hpp"

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

TEST_CASE("Viewport TEXT overlay plan maps insertion and keeps drawing-scale height plus fontFamily",
          "[cadfont]") {
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
  t.plottedHeightInches = 0.25f;
  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, static_cast<double>(t.insX), static_cast<double>(t.insY), &px, &py);
  REQUIRE(px == Catch::Approx(6.f));
  REQUIRE(py == Catch::Approx(5.f));

  const float drawingMup = 100.f;
  const ViewportTextOverlayPlan plan = PlanViewportTextOverlay(t, false, vp, drawingMup);
  REQUIRE_FALSE(plan.skipHidden);
  REQUIRE(plan.fontFamily == "Arial");
  REQUIRE(plan.fontFamily != std::string());
  REQUIRE(plan.modelUnitsPerPlottedInch == Catch::Approx(drawingMup));
  REQUIRE(MtextScaleThroughViewport(t, vp, drawingMup) == Catch::Approx(drawingMup));
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

TEST_CASE("Viewport overlay plan sizes plain MTEXT by the viewport and passes its fontFamily", "[cadfont]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 25.f;
  CadAnnotation m;
  m.kind = CadAnnotation::Kind::Mtext;
  m.plottedHeightInches = 0.25f;
  m.fontFamily = "Times New Roman";
  m.text = "MODEL MTEXT";
  const float drawingMup = 50.f;
  const ViewportTextOverlayPlan plan = PlanViewportTextOverlay(m, false, vp, drawingMup);
  REQUIRE_FALSE(plan.skipHidden);
  REQUIRE(plan.fontFamily == "Times New Roman");
  REQUIRE(plan.modelUnitsPerPlottedInch == Catch::Approx(25.f));
  REQUIRE(cadfont::PreferShxStrokes(plan.fontFamily, m.text) == false);

  CadAnnotation shx = m;
  shx.fontFamily = "romans.shx";
  const ViewportTextOverlayPlan shxPlan = PlanViewportTextOverlay(shx, false, vp, drawingMup);
  REQUIRE(shxPlan.fontFamily == "romans.shx");
  REQUIRE(cadfont::PreferShxStrokes(shxPlan.fontFamily, shx.text));
}

TEST_CASE("Viewport overlay plan skips isolated annotations (REQ-084 (d))", "[cadfont]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 50.f;
  CadAnnotation m;
  m.kind = CadAnnotation::Kind::Mtext;
  m.fontFamily = "Arial";
  EntityAttributes aa;
  aa.id = 7;
  const std::vector<std::uint64_t> hidden{7};
  const bool isHidden = CadEntityIdHidden(&hidden, aa.id);
  REQUIRE(isHidden);
  const ViewportTextOverlayPlan skipped = PlanViewportTextOverlay(m, isHidden, vp, 100.f);
  REQUIRE(skipped.skipHidden);
  const ViewportTextOverlayPlan shown = PlanViewportTextOverlay(m, CadEntityIdHidden(&hidden, 3), vp, 100.f);
  REQUIRE_FALSE(shown.skipHidden);
  REQUIRE_FALSE(CadEntityIdHidden(&hidden, 0));
}

TEST_CASE("font-demo.gs opens without a BOM and uses explicit 1:25 / 1:100 viewports", "[cadfont]") {
  const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/font-demo.gs";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(raw.empty());
  REQUIRE(static_cast<unsigned char>(raw[0]) != 0xEFu);

  const nlohmann::json root = nlohmann::json::parse(raw);
  REQUIRE(root.at("format") == "gosurvey");
  const nlohmann::json& vps = root.at("document").at("paperLayouts").at(0).at("viewports");
  REQUIRE(vps.size() >= 2);
  REQUIRE(vps.at(0).at("scaleModelPerPaperIn").get<float>() == Catch::Approx(25.f));
  REQUIRE(vps.at(1).at("scaleModelPerPaperIn").get<float>() == Catch::Approx(100.f));

  bool foundMtext = false;
  bool foundDimAngular = false;
  for (const nlohmann::json& a : root.at("document").at("annotations")) {
    if (a.at("kind") == "mtext") {
      foundMtext = true;
      REQUIRE(a.contains("fontFamily"));
      REQUIRE_FALSE(a.at("fontFamily").get<std::string>().empty());
    }
    if (a.at("kind") == "dimangular") {
      foundDimAngular = true;
      REQUIRE(a.contains("dimAngVertexX"));
      REQUIRE(a.contains("dimAngVertexY"));
    }
  }
  REQUIRE(foundMtext);
  REQUIRE(foundDimAngular);

  REQUIRE(root.at("document").at("annotationAttrs").at(0).at("id").get<std::uint64_t>() != 0);
}
