// REQ-155 (GitHub issue #155) — per-viewport active UCS and UCSFOLLOW isolation.
//
// A paper-space Viewport carries its own active work plane (Viewport::activeUcs). While the user is
// in FLOATING MODEL SPACE (REQ-036) inside that viewport, coordinate entry / the grid / ORTHO / the
// readout / UCSFOLLOW all resolve against the viewport's frame, not the drawing's — implemented by
// swapping AppCommandState::activeUcs on floating enter/exit. These tests prove:
//   T1  a point typed while floating lands at the coordinates the VIEWPORT's frame implies;
//   T2  UCSFOLLOW=1 re-plans only that viewport's camera, never a sibling or the model view;
//   T3  a viewport's frame field is independent of its siblings and of the drawing UCS;
//   T4  the per-viewport frame round-trips .gs; a legacy file loads every viewport in World;
//   T5  the readout inside a floating viewport resolves in that viewport's frame;
//   T6  saving .gs WHILE floating records the drawing's frame, not the viewport's.
//
// Linked into GoSurveySnapTests (it needs gosurvey_domain + GsIo.cpp, like GsIoViewportCameraTests).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"

using Catch::Approx;

namespace {

// A UCS rotated +90 deg about world Z with its origin at (ox,oy): +X_ucs -> world +Y, +Y_ucs -> -X.
ucs::Ucs RotatedAt(double ox, double oy, double deg) {
  ucs::Ucs u;
  u.origin = {ox, oy, 0.0};
  return ucs::RotatedAboutZ(u, deg);
}

// A drawing with one layout and \p n default viewports.
AppCommandState MakeDrawing(int n) {
  AppCommandState st;
  PaperLayout L;
  L.name = "Sheet";
  for (int i = 0; i < n; ++i) {
    Viewport v;
    v.paperXIn = 1.f + 6.f * static_cast<float>(i);
    v.paperWIn = 5.f;
    v.paperHIn = 4.f;
    L.viewports.push_back(v);
  }
  st.paperLayouts.push_back(L);
  st.activeSpaceIndex = 0;
  return st;
}

std::filesystem::path UniqueGsPath(const char* stem) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-req155";
  std::filesystem::create_directories(dir);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return dir / (std::string(stem) + "-" + std::to_string(stamp) + ".gs");
}

}  // namespace

// T1 -------------------------------------------------------------------------------------------
TEST_CASE("A point typed in floating model space resolves in the viewport's UCS (REQ-155)", "[req155]") {
  AppCommandState st = MakeDrawing(2);
  st.paperLayouts[0].viewports[0].activeUcs = RotatedAt(100.0, 50.0, 90.0);  // viewport A: rotated
  // viewport B left as World.
  std::vector<std::string> log;

  EnterFloatingModelSpace(st, 0, 0, log);
  REQUIRE(st.floatingUcsSwapActive);
  REQUIRE_FALSE(CadUcsIsWorld(st));  // the active frame is now the viewport's

  float lx = 0.f, ly = 0.f;
  REQUIRE(ParseStoragePoint(st, "10,0", &lx, &ly, false, 0.f, 0.f));
  // (10,0) in a frame rotated +90 about Z at (100,50) -> world (100, 60). No document origin, so
  // storage-local == world.
  REQUIRE(lx == Approx(100.0).margin(1e-4));
  REQUIRE(ly == Approx(60.0).margin(1e-4));

  ExitFloatingModelSpace(st, log);
  REQUIRE_FALSE(st.floatingUcsSwapActive);
  REQUIRE(CadUcsIsWorld(st));  // drawing frame restored

  EnterFloatingModelSpace(st, 0, 1, log);  // viewport B: World
  REQUIRE(ParseStoragePoint(st, "10,0", &lx, &ly, false, 0.f, 0.f));
  REQUIRE(lx == Approx(10.0).margin(1e-4));
  REQUIRE(ly == Approx(0.0).margin(1e-4));
  ExitFloatingModelSpace(st, log);
}

// T2 -------------------------------------------------------------------------------------------
TEST_CASE("UCSFOLLOW while floating re-plans only the active viewport's camera (REQ-155)", "[req155]") {
  AppCommandState st = MakeDrawing(2);
  st.ucsFollow = true;
  std::vector<std::string> log;

  const float sibAz = st.paperLayouts[0].viewports[1].camAzimuthDeg;
  const float sibEl = st.paperLayouts[0].viewports[1].camElevationDeg;
  const float drawAz = st.viewportAzimuthDeg;
  const float drawEl = st.viewportElevationDeg;

  EnterFloatingModelSpace(st, 0, 0, log);
  SetActiveUcs(st, RotatedAt(0.0, 0.0, 90.0), log);  // UCSFOLLOW fires -> ApplyPlanViewOf

  const Viewport& a = st.paperLayouts[0].viewports[0];
  const Viewport& b = st.paperLayouts[0].viewports[1];
  REQUIRE_FALSE(a.cameraIsPlan());                       // viewport A re-planned to the new frame
  REQUIRE(b.camAzimuthDeg == Approx(sibAz));             // sibling untouched
  REQUIRE(b.camElevationDeg == Approx(sibEl));
  REQUIRE(st.viewportAzimuthDeg == Approx(drawAz));      // drawing model-view camera untouched
  REQUIRE(st.viewportElevationDeg == Approx(drawEl));

  ExitFloatingModelSpace(st, log);
}

// T3 -------------------------------------------------------------------------------------------
TEST_CASE("A viewport's UCS field is independent of siblings and the drawing UCS (REQ-155)", "[req155]") {
  AppCommandState st = MakeDrawing(2);
  st.paperLayouts[0].viewports[0].activeUcs = RotatedAt(5.0, 5.0, 30.0);

  REQUIRE(ucs::IsWorld(st.paperLayouts[0].viewports[1].activeUcs));  // sibling still World
  REQUIRE(CadUcsIsWorld(st));                                        // drawing UCS still World
  REQUIRE_FALSE(st.floatingUcsSwapActive);                           // nothing swapped
}

// T4 -------------------------------------------------------------------------------------------
TEST_CASE("Per-viewport UCS round-trips .gs; a legacy file loads every viewport in World (REQ-155)", "[req155][gs]") {
  AppCommandState st = MakeDrawing(2);
  const ucs::Ucs frame = RotatedAt(250.0, -75.0, 90.0);
  st.paperLayouts[0].viewports[0].activeUcs = frame;

  const std::filesystem::path path = UniqueGsPath("vpucs");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyFile(st, path.string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyFile(loaded, path.string().c_str(), log));
  REQUIRE(loaded.paperLayouts.size() == 1);
  REQUIRE(loaded.paperLayouts[0].viewports.size() == 2);

  const ucs::Ucs& r0 = loaded.paperLayouts[0].viewports[0].activeUcs;
  REQUIRE(r0.origin.x == Approx(250.0));
  REQUIRE(r0.origin.y == Approx(-75.0));
  REQUIRE(r0.xAxis.x == Approx(frame.xAxis.x).margin(1e-9));
  REQUIRE(r0.xAxis.y == Approx(frame.xAxis.y).margin(1e-9));
  REQUIRE(ucs::IsWorld(loaded.paperLayouts[0].viewports[1].activeUcs));  // sibling defaulted to World

  // Strip the per-viewport "ucs" key -> a pre-REQ-155 file. Every viewport must load in World.
  std::string raw;
  {
    std::ifstream in(path, std::ios::binary);
    raw.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  const size_t k = raw.find("\"ucs\"");
  REQUIRE(k != std::string::npos);
  // Remove "ucs":{...} object (single-level braces).
  const size_t open = raw.find('{', k);
  int depth = 0;
  size_t end = open;
  for (; end < raw.size(); ++end) {
    if (raw[end] == '{') ++depth;
    else if (raw[end] == '}' && --depth == 0) { ++end; break; }
  }
  if (end < raw.size() && raw[end] == ',') ++end;
  raw.erase(k, end - k);
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << raw;
  }
  AppCommandState legacy;
  REQUIRE(LoadGoSurveyFile(legacy, path.string().c_str(), log));
  for (const Viewport& v : legacy.paperLayouts[0].viewports)
    REQUIRE(ucs::IsWorld(v.activeUcs));
}

// T5 -------------------------------------------------------------------------------------------
TEST_CASE("The readout inside a floating viewport resolves in that viewport's frame (REQ-155)", "[req155]") {
  AppCommandState st = MakeDrawing(1);
  st.paperLayouts[0].viewports[0].activeUcs = RotatedAt(100.0, 50.0, 90.0);
  std::vector<std::string> log;

  EnterFloatingModelSpace(st, 0, 0, log);
  REQUIRE_FALSE(CadUcsIsWorld(st));  // the ID readout labels this "current", not "World"
  // world (100,60) is (10,0) in the viewport frame — the numbers the readout would print.
  const ray3d::Vec3 shown = ucs::WorldToUcs(st.activeUcs, {100.0, 60.0, 0.0});
  REQUIRE(shown.x == Approx(10.0).margin(1e-4));
  REQUIRE(shown.y == Approx(0.0).margin(1e-4));
  ExitFloatingModelSpace(st, log);
}

// T6 -------------------------------------------------------------------------------------------
TEST_CASE("Saving .gs while floating records the drawing's UCS, not the viewport's (REQ-155)", "[req155][gs]") {
  AppCommandState st = MakeDrawing(2);
  const ucs::Ucs vpFrame = RotatedAt(10.0, 20.0, 90.0);
  st.paperLayouts[0].viewports[0].activeUcs = vpFrame;
  // drawing UCS stays World.
  std::vector<std::string> log;

  EnterFloatingModelSpace(st, 0, 0, log);
  REQUIRE_FALSE(CadUcsIsWorld(st));  // st.activeUcs is the viewport frame right now

  const std::filesystem::path path = UniqueGsPath("floatsave");
  REQUIRE(SaveGoSurveyFile(st, path.string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyFile(loaded, path.string().c_str(), log));
  REQUIRE(ucs::IsWorld(loaded.activeUcs));  // the DRAWING frame was saved, not the viewport's
  const ucs::Ucs& r0 = loaded.paperLayouts[0].viewports[0].activeUcs;
  REQUIRE(r0.origin.x == Approx(10.0));     // the viewport frame survived on the viewport
  REQUIRE(r0.origin.y == Approx(20.0));

  ExitFloatingModelSpace(st, log);
}
