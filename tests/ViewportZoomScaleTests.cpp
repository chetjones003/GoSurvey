#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "ViewportZoomScale.hpp"
#include "GsIo.hpp"
#include "CadCommandsInternal.hpp"
#include "io/GsAnnotationJson.hpp"
#include "io/LibreDwgCad.hpp"

#include <algorithm>
#include <limits>
#include <filesystem>
#include <vector>

TEST_CASE("Viewport zoom scale presets and zoom math", "[req458]") {
  AppCommandState st;
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  REQUIRE(!st.viewportZoomScales.empty());
  const float zoom50 = ViewportZoomForModelUnitsPerInch(50.f, 800.f, 96.f);
  ApplyViewportZoomScaleIndex(st, 4, 800.f, 96.f);  // 1:50 is index 4 in defaults
  CHECK(st.viewportZoom == Catch::Approx(zoom50).margin(1e-4f));
  CHECK(st.currentViewportZoomScaleIndex == 4);
}

TEST_CASE("Status bar zoom preset stays user-selected after wheel zoom", "[req458]") {
  AppCommandState st;
  ApplyViewportZoomScaleIndex(st, 4, 800.f, 96.f);
  const int chosen = st.currentViewportZoomScaleIndex;
  st.viewportZoom *= 1.37f;
  EnsureViewportZoomScaleIndexForDisplay(st);
  CHECK(st.currentViewportZoomScaleIndex == chosen);
}

TEST_CASE("Status bar scale selection does not change viewport zoom", "[req458]") {
  AppCommandState st;
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  st.viewportZoom = 2.25f;
  SetCurrentViewportZoomScaleIndex(st, 4);
  CHECK(st.currentViewportZoomScaleIndex == 4);
  CHECK(st.viewportZoom == Catch::Approx(2.25f));
  SetCurrentViewportZoomScaleIndex(st, 0);
  CHECK(st.currentViewportZoomScaleIndex == 0);
  CHECK(st.viewportZoom == Catch::Approx(2.25f));
}

TEST_CASE("Custom viewport zoom scale parses and adds", "[req458]") {
  AppCommandState st;
  float paper = 0.f, drawing = 0.f;
  REQUIRE(TryParseViewportZoomRatio("1:75", &paper, &drawing));
  CHECK(paper == Catch::Approx(1.f));
  CHECK(drawing == Catch::Approx(75.f));
  REQUIRE(AddViewportZoomScale(st, "1:75", paper, drawing));
  CHECK(st.currentViewportZoomScaleIndex >= 0);
  const auto it = std::find_if(st.viewportZoomScales.begin(), st.viewportZoomScales.end(),
                                 [](const CadAnnotationScale& s) { return s.name == "1:75"; });
  REQUIRE(it != st.viewportZoomScales.end());
}

TEST_CASE("Viewport zoom scales round-trip in drawing JSON", "[req458]") {
  AppCommandState st;
  AddViewportZoomScale(st, "1:75", 1.f, 75.f);
  st.currentViewportZoomScaleIndex = static_cast<int>(st.viewportZoomScales.size()) - 1;
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, log));
  REQUIRE(std::any_of(loaded.viewportZoomScales.begin(), loaded.viewportZoomScales.end(),
                        [](const CadAnnotationScale& s) { return s.name == "1:75"; }));
}

TEST_CASE("Custom viewport zoom ratios reject malformed and nonfinite input", "[req458]") {
  for (const char* text : {"1:50junk", "1junk:50", "1:50:2", "1:inf", "inf:50", "1:nan",
                           "0:50", "1:-50", "1:1e100", "1e-30:1e30"}) {
    float paper = 2.f, drawing = 3.f;
    INFO(text);
    CHECK_FALSE(TryParseViewportZoomRatio(text, &paper, &drawing));
    CHECK(paper == 2.f);
    CHECK(drawing == 3.f);
  }
  AppCommandState st;
  CHECK_FALSE(AddViewportZoomScale(st, "   ", 1.f, 50.f));
  CHECK_FALSE(AddViewportZoomScale(st, "invalid", 1.f, std::numeric_limits<float>::infinity()));
  CHECK(st.viewportZoomScales.empty());
}

TEST_CASE("Viewport zoom presets stay with their drawing tab", "[req458]") {
  AppCommandState st;
  st.documents.resize(3);
  REQUIRE(AddViewportZoomScale(st, "1:75", 1.f, 75.f));
  SaveDocumentToSnapshot(st, 1);
  RestoreDocumentFromSnapshot(st, 2);
  CHECK(st.viewportZoomScales.empty());
  CHECK(st.currentViewportZoomScaleIndex == -1);
  REQUIRE(AddViewportZoomScale(st, "1:125", 1.f, 125.f));
  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  CHECK(std::any_of(st.viewportZoomScales.begin(), st.viewportZoomScales.end(),
                    [](const auto& scale) { return scale.name == "1:75"; }));
  CHECK(std::none_of(st.viewportZoomScales.begin(), st.viewportZoomScales.end(),
                     [](const auto& scale) { return scale.name == "1:125"; }));
  ClearCadGeometry(st);
  CHECK(st.viewportZoomScales.empty());
  CHECK(st.currentViewportZoomScaleIndex == -1);
}

TEST_CASE("Native DWG and DXF preserve zoom presets independently from annotation scales", "[req458]") {
  const auto dir = std::filesystem::path(GOSURVEY_TEST_DATA_DIR).parent_path() /
      "build" / "artifacts" / "viewport-zoom-roundtrip";
  std::filesystem::create_directories(dir);
  for (bool asDxf : {false, true}) {
    INFO((asDxf ? "DXF" : "DWG"));
    AppCommandState st;
    REQUIRE(AddViewportZoomScale(st, "1:75", 1.f, 75.f));
    st.annotationScales.push_back({"1:25", 1.f, 25.f});
    st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
    st.userLineAttrs.resize(1);
    const auto path = (dir / (asDxf ? "zoom.dxf" : "zoom.dwg")).u8string();
    std::vector<std::string> log;
    REQUIRE(ExportLibreCadFile(st, path.c_str(), log, asDxf));
    AppCommandState loaded;
    REQUIRE(ImportLibreCadFile(loaded, path.c_str(), log, asDxf));
    const auto custom = std::find_if(loaded.viewportZoomScales.begin(), loaded.viewportZoomScales.end(),
                                    [](const auto& scale) { return scale.name == "1:75"; });
    REQUIRE(custom != loaded.viewportZoomScales.end());
    CHECK(custom->drawingUnits / custom->paperUnits == Catch::Approx(75.f));
    CHECK(std::none_of(loaded.annotationScales.begin(), loaded.annotationScales.end(), [](const auto& scale) {
      return scale.name == "1:75" || scale.name.find(kViewportZoomScaleDwgPrefix) == 0;
    }));
    CHECK(std::any_of(loaded.annotationScales.begin(), loaded.annotationScales.end(),
                      [](const auto& scale) { return scale.name == "1:25"; }));
  }
}

TEST_CASE("Standard annotation scale list is AutoCAD's default list", "[req458]") {
  // D-2026-11-06-a: a drawing with no annotation scales gets AutoCAD's Default Scale List, in AutoCAD's order.
  std::vector<CadAnnotationScale> metric;
  EnsureStandardAnnotationScales(metric, 4);  // mm: 1:50 is 25.4 * 50 = 1270 mm per plotted inch
  REQUIRE(metric.size() == 17);
  CHECK(metric.front().name == "1:1");
  CHECK(metric[10].name == "1:50");
  CHECK(metric.back().name == "100:1");
  CHECK(CadAnnotationScaleModelUnitsPerPlottedInch(metric[10]) == Catch::Approx(1270.0f));
  CHECK(CadAnnotationScaleModelUnitsPerPlottedInch(metric.back()) == Catch::Approx(0.254f));

  std::vector<CadAnnotationScale> metricMeters;
  EnsureStandardAnnotationScales(metricMeters, 6);  // m: 1:50 is 0.0254 * 50 = 1.27 m per plotted inch
  REQUIRE(metricMeters.size() == 17);
  CHECK(CadAnnotationScaleModelUnitsPerPlottedInch(metricMeters[10]) == Catch::Approx(1.27f));

  std::vector<CadAnnotationScale> imperial;
  EnsureStandardAnnotationScales(imperial, 1);  // inches
  REQUIRE(imperial.size() == 19);
  CHECK(imperial.front().name == "1\" = 1\"");
  CHECK(imperial[1].name == "1\" = 5'");
  CHECK(CadAnnotationScaleModelUnitsPerPlottedInch(imperial[1]) == Catch::Approx(60.0f));
  CHECK(imperial.back().name == "1\" = 5000'");

  // Feet drawings (INSUNITS 2, the default) use the same labels but 1 foot = 12 inches of drawing units.
  std::vector<CadAnnotationScale> feet;
  EnsureStandardAnnotationScales(feet, 2);
  REQUIRE(feet.size() == 19);
  CHECK(feet[1].name == "1\" = 5'");
  CHECK(CadAnnotationScaleModelUnitsPerPlottedInch(feet[1]) == Catch::Approx(5.0f));
  CHECK(CadAnnotationScaleModelUnitsPerPlottedInch(feet.back()) == Catch::Approx(5000.0f));

  // A list that already has entries is kept as it is.
  std::vector<CadAnnotationScale> kept{{"1:25", 1.f, 25.f}};
  EnsureStandardAnnotationScales(kept, 2);
  REQUIRE(kept.size() == 1);
  CHECK(kept.front().name == "1:25");
}

TEST_CASE("Annotative objects follow the annotation scale in model space only (D-2026-11-06-a)", "[req458]") {
  const std::vector<CadAnnotationScale> scales{{"1:50", 1.f, 50.f}, {"1:10", 1.f, 10.f}};
  CHECK(ModelObjectUnitsPerPlottedInch(true, true, 80.f, scales, 1) == Catch::Approx(10.f));
  CHECK(ModelObjectUnitsPerPlottedInch(false, true, 80.f, scales, 1) == Catch::Approx(80.f));
  CHECK(ModelObjectUnitsPerPlottedInch(true, false, 80.f, scales, 1) == Catch::Approx(80.f));
  CHECK(ModelObjectUnitsPerPlottedInch(true, true, 80.f, scales, -1) == Catch::Approx(80.f));
}

TEST_CASE("Survey markers and labels are annotative by default (D-2026-11-06-a)", "[req458]") {
  CHECK(SurveyPointStyleDefinition{}.annotative);

  AppCommandState st;
  SurveyPoint point;
  point.id = 7;
  point.easting = 100.0;
  point.northing = 200.0;
  point.elevation = 1.0;
  point.description = "EG";
  st.surveyPoints.push_back(point);
  std::vector<std::string> log;
  EnsureSurveyPointLabelMtext(st, 0, &log);
  REQUIRE_FALSE(st.cadAnnotations.empty());
  CHECK(st.cadAnnotations.back().annotative);
  CHECK(st.cadAnnotations.back().surveyPointLabelForId == 7);
}

TEST_CASE("Survey labels saved before the annotative flag load annotative (D-2026-11-06-a)", "[req458]") {
  CadAnnotation surveyLabel;
  surveyLabel.kind = CadAnnotation::Kind::Mtext;
  surveyLabel.surveyPointLabelForId = 7;
  nlohmann::json saved;
  CadAnnotationToJson(surveyLabel, saved);
  saved.erase("annotative");
  CHECK(CadAnnotationFromJson(saved).annotative);

  CadAnnotation plain;
  plain.kind = CadAnnotation::Kind::Mtext;
  nlohmann::json plainSaved;
  CadAnnotationToJson(plain, plainSaved);
  CHECK_FALSE(CadAnnotationFromJson(plainSaved).annotative);
}

TEST_CASE("New text takes the Annotative setting of its text style (D-2026-11-06-a)", "[req458]") {
  AppCommandState st;
  TextStyles::EnsureStandard(st.textStyles);
  TextStyle annotativeStyle;
  annotativeStyle.name = "Annotative Text";
  annotativeStyle.props["annotative"] = "1";  // what the style editor's checkbox stores when ticked
  st.textStyles.push_back(annotativeStyle);

  st.activeTextStyleName = "Annotative Text";
  CadAnnotation text;
  text.kind = CadAnnotation::Kind::Mtext;
  StampActiveTextStyleOnNewText(st, text);
  CHECK(text.annotative);

  st.activeTextStyleName = TextStyles::kStandardName;
  CadAnnotation plain;
  plain.kind = CadAnnotation::Kind::Mtext;
  StampActiveTextStyleOnNewText(st, plain);
  CHECK_FALSE(plain.annotative);
}
