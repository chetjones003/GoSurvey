// REQ-061 — the per-viewport camera must survive SaveGoSurveyTemplateFile / LoadGoSurveyTemplateFile, and a
// legacy document JSON payload (no camera keys) must load with every viewport in plan view. Linked here
// (GoSurveySnapTests) because GoSurveyTests cannot link GsIo.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "GsIoTestSupport.hpp"

using Catch::Approx;

namespace {
std::filesystem::path UniqueGsPath(const char* stem) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-req061";
  std::filesystem::create_directories(dir);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return dir / (std::string(stem) + "-" + std::to_string(stamp) + ".gst");
}
}  // namespace

TEST_CASE("Per-viewport camera round-trips through document JSON payload (REQ-061)", "[payload][req061]") {
  AppCommandState st;
  PaperLayout L;
  L.name = "Sheet";
  Viewport plan;  // left as the default plan view
  Viewport iso;
  iso.camAzimuthDeg = 45.f;
  iso.camElevationDeg = 35.26439f;
  iso.camRollDeg = 0.f;
  iso.camPerspective = true;
  iso.camFovDeg = 30.f;
  L.viewports.push_back(plan);
  L.viewports.push_back(iso);
  st.paperLayouts.push_back(L);

  const std::filesystem::path path = UniqueGsPath("vpcam");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.paperLayouts.size() == 1);
  REQUIRE(loaded.paperLayouts[0].viewports.size() == 2);

  const Viewport& lp = loaded.paperLayouts[0].viewports[0];
  const Viewport& li = loaded.paperLayouts[0].viewports[1];
  REQUIRE(lp.cameraIsPlan());
  REQUIRE(li.camAzimuthDeg == Approx(45.f));
  REQUIRE(li.camElevationDeg == Approx(35.26439f));
  REQUIRE(li.camPerspective);
  REQUIRE(li.camFovDeg == Approx(30.f));
  REQUIRE_FALSE(li.cameraIsPlan());
}

TEST_CASE("Per-viewport visual style round-trips through document JSON payload (REQ-371)", "[payload][req371][issue624]") {
  AppCommandState st;
  PaperLayout L;
  Viewport wire;
  Viewport hidden;
  hidden.visualStyle = VisualStyle::Hidden;
  Viewport shaded;
  shaded.visualStyle = VisualStyle::Shaded;
  L.viewports.push_back(wire);
  L.viewports.push_back(hidden);
  L.viewports.push_back(shaded);
  st.paperLayouts.push_back(L);

  const std::filesystem::path path = UniqueGsPath("vpvis");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.paperLayouts.size() == 1);
  REQUIRE(loaded.paperLayouts[0].viewports.size() == 3);
  CHECK(loaded.paperLayouts[0].viewports[0].visualStyle == VisualStyle::Wireframe2D);
  CHECK(loaded.paperLayouts[0].viewports[1].visualStyle == VisualStyle::Hidden);
  CHECK(loaded.paperLayouts[0].viewports[2].visualStyle == VisualStyle::Shaded);
}

TEST_CASE("Closed-polyline viewport clipping boundary round-trips through document JSON (REQ-490)",
          "[payload][req490]") {
  AppCommandState st;
  PaperLayout layout;
  Viewport vp;
  vp.clipBoundary = {{1.f, 2.f, 0.5f}, {8.f, 2.f, 0.f}, {4.f, 9.f, -0.25f}};
  layout.viewports.push_back(vp);
  st.paperLayouts.push_back(layout);
  const auto path = UniqueGsPath("vpclip");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));
  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.paperLayouts.size() == 1);
  REQUIRE(loaded.paperLayouts[0].viewports.size() == 1);
  const auto& boundary = loaded.paperLayouts[0].viewports[0].clipBoundary;
  REQUIRE(boundary.size() == 3);
  CHECK(boundary[0].xIn == Approx(1.f));
  CHECK(boundary[0].yIn == Approx(2.f));
  CHECK(boundary[0].bulge == Approx(0.5f));
  CHECK(boundary[2].bulge == Approx(-0.25f));
}

TEST_CASE("A legacy document JSON payload with no camera keys loads every viewport in plan view (REQ-061)", "[payload][req061]") {
  AppCommandState st;
  PaperLayout L;
  Viewport vp;
  vp.paperWIn = 8.f;
  vp.modelCenterX = 500.0;
  L.viewports.push_back(vp);
  st.paperLayouts.push_back(L);

  const std::filesystem::path path = UniqueGsPath("legacy");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));

  // Strip the camera keys from the saved JSON to simulate a file written before REQ-061.
  std::string raw;
  {
    std::ifstream in(path, std::ios::binary);
    raw.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  for (const char* key : {"camAzimuthDeg", "camElevationDeg", "camRollDeg", "camPerspective", "camFovDeg"})
    REQUIRE(raw.find(key) != std::string::npos);  // sanity: the writer emitted them
  auto stripKey = [&](const std::string& key) {
    for (;;) {
      const size_t k = raw.find("\"" + key + "\"");
      if (k == std::string::npos)
        break;
      size_t end = raw.find_first_of(",}", k);
      // drop a trailing comma if we removed a middle element, else a leading one
      size_t start = k;
      if (raw[end] == ',')
        ++end;
      else if (start > 0) {
        size_t p = raw.find_last_not_of(" \n\r\t", start - 1);
        if (p != std::string::npos && raw[p] == ',')
          start = p;
      }
      raw.erase(start, end - start);
    }
  };
  for (const char* key : {"camAzimuthDeg", "camElevationDeg", "camRollDeg", "camPerspective", "camFovDeg"})
    stripKey(key);
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << raw;
  }

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.paperLayouts.size() == 1);
  REQUIRE(loaded.paperLayouts[0].viewports.size() == 1);
  REQUIRE(loaded.paperLayouts[0].viewports[0].cameraIsPlan());
  REQUIRE(loaded.paperLayouts[0].viewports[0].paperWIn == Approx(8.f));  // other fields still parsed
}
