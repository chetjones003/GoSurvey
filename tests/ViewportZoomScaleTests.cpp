#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "ViewportZoomScale.hpp"
#include "GsIo.hpp"
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
  const auto dir = std::filesystem::path(GOSURVEY_SOURCE_DIR) / "build" / "artifacts" / "viewport-zoom-roundtrip";
  std::filesystem::create_directories(dir);
  for (bool asDxf : {false, true}) {
    INFO(asDxf ? "DXF" : "DWG");
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
