// Issue #125 — DIMANGULAR must round-trip through document JSON payload (kind + angle vertex).
//
// GsIo.cpp is not linked here. These tests exercise the same JSON helpers SerializeGoSurveyJson/LoadGoSurveyFromJsonUtf8
// call (`GsAnnotationJson.hpp`), which is where the defect lived: Kind::DimAngular fell through
// to "text" and dimAngVertexX/Y were never written.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "CadDimStroke.hpp"
#include "GsAnnotationJson.hpp"

#include <nlohmann/json.hpp>

using nlohmann::json;

TEST_CASE("DIMANGULAR kind and vertex survive annotation JSON (issue #125)", "[payload][dimangular]") {
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

TEST_CASE("kind text stays TEXT and is not promoted to DIMANGULAR (issue #125)", "[payload][dimangular]") {
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

TEST_CASE("unknown annotation kind tag loads as TEXT without crashing (issue #125)", "[payload][dimangular]") {
  json o;
  o["kind"] = "not-a-real-kind";
  const CadAnnotation a = CadAnnotationFromJson(o);
  REQUIRE(a.kind == CadAnnotation::Kind::Text);
}

TEST_CASE("annotation annotative flag persists through JSON (issue #622)", "[payload][issue622]") {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.annotative = true;
  a.text = "Scale me";
  json o;
  CadAnnotationToJson(a, o);
  REQUIRE(o.at("annotative").get<bool>());
  const CadAnnotation b = CadAnnotationFromJson(o);
  REQUIRE(b.annotative);
}

TEST_CASE("annotation visible scale names persist through JSON (issue #622)", "[payload][issue622]") {
  CadAnnotation a;
  a.annotative = true;
  a.annotativeVisibleScaleNames = {"1:10", "1:50"};
  json o;
  CadAnnotationToJson(a, o);
  REQUIRE(o.at("annotativeVisibleScaleNames").size() == 2);
  const CadAnnotation b = CadAnnotationFromJson(o);
  REQUIRE(b.annotativeVisibleScaleNames.size() == 2);
  CHECK(b.annotativeVisibleScaleNames[0] == "1:10");
  CHECK(b.annotativeVisibleScaleNames[1] == "1:50");
}

// Issue #1676 — one-off dimension sizes and the "label placed by the user" flag are document data.
TEST_CASE("Dimension size overrides and label placement survive annotation JSON (issue #1676)",
          "[payload][dimension]") {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAligned;
  a.dimExt2X = 10.0;
  a.dimTextHeightModel = 0.35;
  a.dimArrowSizeModel = 0.2;
  a.dimTextUserPlaced = false;
  json o;
  CadAnnotationToJson(a, o);
  const CadAnnotation b = CadAnnotationFromJson(o);
  CHECK(b.dimTextHeightModel == 0.35);
  CHECK(b.dimArrowSizeModel == 0.2);
  CHECK_FALSE(b.dimTextUserPlaced);

  // A drawing saved before the fields existed: no overrides, and every label stays where it was saved.
  o.erase("dimTextHeightModel");
  o.erase("dimArrowSizeModel");
  o.erase("dimTextUserPlaced");
  const CadAnnotation legacy = CadAnnotationFromJson(o);
  CHECK(legacy.dimTextHeightModel == 0.0);
  CHECK(legacy.dimArrowSizeModel == 0.0);
  CHECK(legacy.dimTextUserPlaced);

  // A corrupt override means "follow the style", never a negative or NaN size.
  o["dimTextHeightModel"] = -3.0;
  CHECK(CadAnnotationFromJson(o).dimTextHeightModel == 0.0);
}

// Issue #1676 — text and arrows are sized by ONE scale, so they can no longer disagree.
TEST_CASE("Dimension text and arrows follow the same annotation scale; model overrides do not (issue #1676)",
          "[dimension]") {
  const std::vector<CadAnnotationScale> scales{{"1\" = 10'", 1.f, 120.f}, {"1\" = 40'", 1.f, 480.f}};
  DimensionStyle style;
  style.textSizeInches = 0.1f;
  style.arrowSizeInches = 0.1f;
  style.props["annotative"] = "1";  // annotative through the STYLE, not the dimension flag
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAligned;
  a.dimExt1X = 0.0;
  a.dimExt2X = 500.0;
  a.dimSignedOffset = 20.f;
  a.dimTextUserPlaced = false;

  const auto sizesAt = [&](int scaleIndex, float* text, float* arrowLen, float* labelLift) {
    CadDimStrokeParams p;
    p.modelUnitsPerPlottedInch = CadDimModelUnitsPerPlottedInch(a, &style, 120.f, &scales, scaleIndex, nullptr, true);
    p.arrowSizeInches = style.arrowSizeInches;
    p.arrowScale = 1.f;
    p.style = &style;
    *text = CadDimTextHeightWorld(a, &style, p.modelUnitsPerPlottedInch);
    CadDimWorldStrokes s;
    REQUIRE(CadDimBuildWorldStrokes(a, p, &s));
    REQUIRE_FALSE(s.arrows.empty());
    const CadDimWorldTri& t = s.arrows.front();  // tip (x0,y0), base corners (x1,y1),(x2,y2)
    *arrowLen = std::hypot(0.5f * (t.x1 + t.x2) - t.x0, 0.5f * (t.y1 + t.y2) - t.y0);
    *labelLift = s.labelY - a.dimSignedOffset;  // horizontal dimension: the label sits above the line
  };
  float text10 = 0, arrow10 = 0, lift10 = 0, text40 = 0, arrow40 = 0, lift40 = 0;
  sizesAt(0, &text10, &arrow10, &lift10);
  sizesAt(1, &text40, &arrow40, &lift40);
  CHECK(text10 == Catch::Approx(12.0));           // 0.1 in at 1" = 10'
  CHECK(text40 == Catch::Approx(48.0));           // the TEXT follows the scale, as the arrows always did
  CHECK(arrow40 == Catch::Approx(arrow10 * 4.0).epsilon(1e-3));
  CHECK(lift40 == Catch::Approx(lift10 * 4.0).epsilon(1e-3));  // label clears the line at both scales

  a.dimTextHeightModel = 2.0;  // one-off small text, model units
  a.dimArrowSizeModel = 1.5;
  sizesAt(0, &text10, &arrow10, &lift10);
  sizesAt(1, &text40, &arrow40, &lift40);
  CHECK(text10 == Catch::Approx(2.0));
  CHECK(text40 == Catch::Approx(2.0));
  CHECK(arrow10 == Catch::Approx(1.5).epsilon(1e-3));
  CHECK(arrow40 == Catch::Approx(1.5).epsilon(1e-3));

  // Non-annotative: drawing scale x Overall scale, for the text as well as the arrows.
  style.props.erase("annotative");
  style.props["overallScale"] = "2";
  a.dimTextHeightModel = 0.0;
  a.dimArrowSizeModel = 0.0;
  const float mup = CadDimModelUnitsPerPlottedInch(a, &style, 120.f, &scales, 1, nullptr, true);
  CHECK(mup == Catch::Approx(240.f));
  CHECK(CadDimTextHeightWorld(a, &style, mup) == Catch::Approx(24.0));

  // Overall scale <= 0 falls back to 1.0 (AutoCAD's "scale to layout").
  style.props["overallScale"] = "0";
  CHECK(CadDimModelUnitsPerPlottedInch(a, &style, 120.f, &scales, 1, nullptr, true) == Catch::Approx(120.f));
  style.props["overallScale"] = "-3";
  CHECK(CadDimModelUnitsPerPlottedInch(a, &style, 120.f, &scales, 1, nullptr, true) == Catch::Approx(120.f));
  style.props.erase("overallScale");

  // Annotative via the dimension flag itself (non-annotative style), on a sheet viewport.
  a.annotative = true;
  Viewport vp;
  vp.scaleModelPerPaperIn = 120.f;
  vp.annotationScaleName = "1\" = 40'";
  const float vpMup = CadDimModelUnitsPerPlottedInch(a, &style, 120.f, &scales, 0, &vp, false);
  CHECK(vpMup == Catch::Approx(480.f));
  CHECK(CadDimTextHeightWorld(a, &style, vpMup) == Catch::Approx(48.f));

  // Drawing-wide arrow multiplier scales arrows when no override is set; override wins over it.
  {
    const CadDimStrokeParams scaled = CadDimStrokeParamsFor(a, style, 120.f, &scales, 0, nullptr, true, 2.5f);
    CadDimWorldStrokes s;
    REQUIRE(CadDimBuildWorldStrokes(a, scaled, &s));
    REQUIRE_FALSE(s.arrows.empty());
    const CadDimWorldTri& t = s.arrows.front();
    const float len = std::hypot(0.5f * (t.x1 + t.x2) - t.x0, 0.5f * (t.y1 + t.y2) - t.y0);
    CHECK(len == Catch::Approx(style.arrowSizeInches * 120.f * 2.5f).epsilon(1e-3));

    a.dimArrowSizeModel = 3.0;
    REQUIRE(CadDimBuildWorldStrokes(a, scaled, &s));
    const CadDimWorldTri& tOv = s.arrows.front();
    const float lenOv = std::hypot(0.5f * (tOv.x1 + tOv.x2) - tOv.x0, 0.5f * (tOv.y1 + tOv.y2) - tOv.y0);
    CHECK(lenOv == Catch::Approx(3.0f).epsilon(1e-3));
    a.dimArrowSizeModel = 0.0;
  }

  // A user-placed label keeps its stored coordinates across annotation scales.
  a.insX = 250.f;
  a.insY = 77.f;
  a.dimTextUserPlaced = true;
  {
    const CadDimStrokeParams p40 = CadDimStrokeParamsFor(a, style, 120.f, &scales, 1, nullptr, true, 1.f);
    CadDimWorldStrokes s;
    REQUIRE(CadDimBuildWorldStrokes(a, p40, &s));
    CHECK(s.labelX == Catch::Approx(250.f));
    CHECK(s.labelY == Catch::Approx(77.f));
  }

  // DimAngular honours the same scale, model-unit overrides, and auto-placed label clearance.
  CadAnnotation ang;
  ang.kind = CadAnnotation::Kind::DimAngular;
  ang.annotative = true;
  ang.dimAngVertexX = 0.0; ang.dimAngVertexY = 0.0;
  ang.dimExt1X = 200.0; ang.dimExt1Y = 0.0;
  ang.dimExt2X = 0.0; ang.dimExt2Y = 200.0;
  ang.dimSignedOffset = 100.f;
  ang.dimTextUserPlaced = false;
  ang.dimTextHeightModel = 4.0;
  ang.dimArrowSizeModel = 2.5;
  {
    const CadDimStrokeParams p40 = CadDimStrokeParamsFor(ang, style, 120.f, &scales, 1, nullptr, true, 2.f);
    CHECK(CadDimTextHeightWorld(ang, &style, p40.modelUnitsPerPlottedInch) == Catch::Approx(4.f));
    CadDimWorldStrokes s;
    REQUIRE(CadDimBuildWorldStrokes(ang, p40, &s));
    REQUIRE_FALSE(s.arrows.empty());
    const CadDimWorldTri& t = s.arrows.front();
    const float len = std::hypot(0.5f * (t.x1 + t.x2) - t.x0, 0.5f * (t.y1 + t.y2) - t.y0);
    CHECK(len == Catch::Approx(2.5f).epsilon(1e-3));
    const float labelRadius = std::hypot(s.labelX, s.labelY);
    CHECK(labelRadius == Catch::Approx(100.f + 1.12f * 4.f).epsilon(1e-3));
  }
}
