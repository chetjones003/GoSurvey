// ZOOM EXTENTS in an orbited view (GitHub issue #564 §1, D-2026-09-28-d, REQ-122 amended).
//
// Written against what the user sees: after ZOOM EXTENTS, every corner of the model's 3D box must
// land on screen inside the margin, and the binding edge must actually reach it — a frame can be
// "safe" by being far too loose, which is the other half of the complaint. The projection used to
// check is the CAMERA's own `WorldToScreen`, not the framing math re-derived, so the test and the
// code cannot agree by sharing a mistake.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CadCommands.hpp"
#include "ZoomFraming.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

constexpr int kW = 1200;
constexpr int kH = 700;
constexpr float kAspect = static_cast<float>(kW) / static_cast<float>(kH);

void AddBoxSolid(AppCommandState& st, double cx, double cy, double l, double w, double h) {
  ucs::Ucs frame;
  frame.origin = {cx, cy, 0.0};
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(frame, l, w, h, &s, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
  st.cadSolidAttrs.push_back(EntityAttributes{});
}

void RunZoomExtents(AppCommandState& st, std::vector<std::string>& log) {
  st.pendingZoomExtents = true;
  ProcessPendingViewportZoom(st, nullptr, nullptr, nullptr, kW, kH, kAspect, log);
}

/// The screen-space rectangle the 3D box's eight corners occupy, in pixels.
struct PxRect {
  float mnX = 1e30f, mnY = 1e30f, mxX = -1e30f, mxY = -1e30f;
};
PxRect ProjectBox(const AppCommandState& st, const ray3d::Vec3& mn, const ray3d::Vec3& mx) {
  const Camera cam = CadViewCamera(st);
  PxRect r;
  for (int k = 0; k < 8; ++k) {
    float px = 0.f, py = 0.f;
    cam.WorldToScreen((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z,
                      static_cast<float>(kW), static_cast<float>(kH), &px, &py);
    r.mnX = std::min(r.mnX, px);
    r.mxX = std::max(r.mxX, px);
    r.mnY = std::min(r.mnY, py);
    r.mxY = std::max(r.mxY, py);
  }
  return r;
}

/// Inside the viewport with the REQ-122 margin, centred, and TIGHT on the binding axis.
void CheckFramed(const PxRect& r) {
  const float m = zoomframing::kMarginFraction;
  const float loX = 0.5f * m * kW - 0.5f, hiX = (1.f - 0.5f * m) * kW + 0.5f;
  const float loY = 0.5f * m * kH - 0.5f, hiY = (1.f - 0.5f * m) * kH + 0.5f;
  CHECK(r.mnX >= loX);
  CHECK(r.mxX <= hiX);
  CHECK(r.mnY >= loY);
  CHECK(r.mxY <= hiY);
  // Centred on screen (to a pixel) — the box centre is the camera target.
  CHECK(0.5f * (r.mnX + r.mxX) == Approx(0.5f * kW).margin(1.0));
  CHECK(0.5f * (r.mnY + r.mxY) == Approx(0.5f * kH).margin(1.0));
  // Tight: one axis spans the full (1 - margin) of the viewport, within a pixel.
  const float fillX = (r.mxX - r.mnX) / ((1.f - m) * kW);
  const float fillY = (r.mxY - r.mnY) / ((1.f - m) * kH);
  CHECK(std::max(fillX, fillY) == Approx(1.0).margin(2.0 / kH));
}

}  // namespace

TEST_CASE("ZOOM EXTENTS frames an orbited model by its 3D silhouette (issue #564 s1)",
          "[zoom][issue564]") {
  std::vector<std::string> log;
  AppCommandState st;
  AddBoxSolid(st, 100.0, 50.0, 40.0, 20.0, 12.0);
  for (const float az : {0.f, 30.f, 135.f, 250.f}) {
    for (const float el : {0.f, 20.f, 45.f, 80.f}) {
      st.viewportAzimuthDeg = az;
      st.viewportElevationDeg = el;
      st.viewportZoom = 3.7f;  // start somewhere arbitrary: the result must not depend on it
      st.viewportPanX = -500.0;
      RunZoomExtents(st, log);
      INFO("az " << az << " el " << el);
      CheckFramed(ProjectBox(st, {80.0, 40.0, 0.0}, {120.0, 60.0, 12.0}));
    }
  }
}

TEST_CASE("ZOOM EXTENTS frames a TALL model by its height, not its footprint (issue #564 s1)",
          "[zoom][issue564]") {
  std::vector<std::string> log;
  AppCommandState st;
  AddBoxSolid(st, 0.0, 0.0, 4.0, 4.0, 300.0);  // a mast: 4 x 4 footprint, 300 tall
  st.viewportAzimuthDeg = 30.f;
  st.viewportElevationDeg = 10.f;
  RunZoomExtents(st, log);
  const PxRect r = ProjectBox(st, {-2.0, -2.0, 0.0}, {2.0, 2.0, 300.0});
  CheckFramed(r);
  // Height binds: the mast spans the viewport's usable height.
  CHECK((r.mxY - r.mnY) == Approx((1.f - zoomframing::kMarginFraction) * kH).margin(2.0));
}

TEST_CASE("Repeated orbited ZOOM EXTENTS is stable - it does not creep (issue #564 s1)",
          "[zoom][issue564]") {
  std::vector<std::string> log;
  AppCommandState st;
  AddBoxSolid(st, 10.0, 10.0, 30.0, 8.0, 5.0);
  st.viewportAzimuthDeg = 60.f;
  st.viewportElevationDeg = 25.f;
  RunZoomExtents(st, log);
  const double px = st.viewportPanX, py = st.viewportPanY, pz = st.viewportPanZ;
  const float z = st.viewportZoom;
  for (int i = 0; i < 5; ++i)
    RunZoomExtents(st, log);
  CHECK(st.viewportPanX == px);
  CHECK(st.viewportPanY == py);
  CHECK(st.viewportPanZ == pz);
  CHECK(st.viewportZoom == z);
}

TEST_CASE("Perspective ZOOM EXTENTS keeps every corner on screen (issue #564 s1)", "[zoom][issue564]") {
  std::vector<std::string> log;
  AppCommandState st;
  AddBoxSolid(st, 0.0, 0.0, 50.0, 30.0, 20.0);
  st.viewportProjection = Camera::Projection::Perspective;
  st.viewportAzimuthDeg = 40.f;
  st.viewportElevationDeg = 30.f;
  RunZoomExtents(st, log);
  const PxRect r = ProjectBox(st, {-25.0, -15.0, 0.0}, {25.0, 15.0, 20.0});
  const float m = zoomframing::kMarginFraction;
  CHECK(r.mnX >= 0.5f * m * kW - 0.5f);
  CHECK(r.mxX <= (1.f - 0.5f * m) * kW + 0.5f);
  CHECK(r.mnY >= 0.5f * m * kH - 0.5f);
  CHECK(r.mxY <= (1.f - 0.5f * m) * kH + 0.5f);
  // Tight: some corner reaches the margin (perspective is not symmetric, so per side, not per span).
  const float slack = std::min({r.mnX - 0.5f * m * kW, (1.f - 0.5f * m) * kW - r.mxX,
                                r.mnY - 0.5f * m * kH, (1.f - 0.5f * m) * kH - r.mxY});
  CHECK(slack == Approx(0.0).margin(1.5));
}

TEST_CASE("A drawing of solids or pipe runs alone has something to frame (issue #564 s1)",
          "[zoom][issue564]") {
  std::vector<std::string> log;
  SECTION("pipe runs only, in PLAN") {
    AppCommandState st;
    CadPipeRun run;
    run.vertsXyz = {0.0, 0.0, 5.0, 100.0, 0.0, 5.0, 100.0, 60.0, 5.0};
    run.nominalSize = "4";
    st.cadPipeRuns.push_back(run);
    RunZoomExtents(st, log);
    REQUIRE_FALSE(log.empty());
    CHECK(log.back().find("nothing to frame") == std::string::npos);
    CHECK(st.viewportPanX == Approx(50.0).margin(0.5));
    CHECK(st.viewportPanY == Approx(30.0).margin(0.5));
  }
  SECTION("pipe runs only, ORBITED") {
    AppCommandState st;
    CadPipeRun run;
    run.vertsXyz = {0.0, 0.0, 0.0, 0.0, 0.0, 80.0};  // a riser
    run.nominalSize = "4";
    st.cadPipeRuns.push_back(run);
    st.viewportAzimuthDeg = 20.f;
    st.viewportElevationDeg = 15.f;
    RunZoomExtents(st, log);
    CHECK(log.back().find("nothing to frame") == std::string::npos);
    CHECK(st.viewportPanZ == Approx(40.0).margin(0.5));
  }
  SECTION("an empty orbited drawing still says so and changes nothing") {
    AppCommandState st;
    st.viewportAzimuthDeg = 20.f;
    st.viewportElevationDeg = 15.f;
    st.viewportZoom = 2.f;
    RunZoomExtents(st, log);
    CHECK(log.back().find("nothing to frame") != std::string::npos);
    CHECK(st.viewportZoom == 2.f);
  }
}

TEST_CASE("Plan-view ZOOM EXTENTS is the pre-change framing, unchanged (issue #564 s1)",
          "[zoom][issue564]") {
  // The plan path must be byte-identical: the same FrameWorldRect over the same robust 2D extents.
  std::vector<std::string> log;
  AppCommandState st;
  st.userLinesFlat = {0.f, 0.f, 0.f, 120.f, 40.f, 900.f, -30.f, 15.f, 0.f, 10.f, -60.f, 0.f};
  st.userLineAttrs.assign(2, EntityAttributes{});
  AddBoxSolid(st, 50.0, 50.0, 10.0, 10.0, 500.0);
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  int skipped = 0;
  REQUIRE(ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped));
  double wantX = 0., wantY = 0.;
  float wantZoom = 0.f;
  REQUIRE(zoomframing::FrameWorldRect(mnX, mxX, mnY, mxY, kAspect, &wantX, &wantY, &wantZoom));
  st.viewportPanZ = 7.0;
  RunZoomExtents(st, log);
  CHECK(st.viewportPanX == wantX);
  CHECK(st.viewportPanY == wantY);
  CHECK(st.viewportZoom == wantZoom);
  CHECK(st.viewportPanZ == 7.0);  // plan framing never touched the target elevation
}

TEST_CASE("FrameBoxInView refuses a box that is not finite (REQ-122 (3))", "[zoom][issue564]") {
  const double right[3] = {1, 0, 0}, up[3] = {0, 1, 0}, back[3] = {0, 0, 1};
  const double mn[3] = {0, 0, 0};
  const double mxBad[3] = {1, std::nan(""), 1};
  double target[3] = {123, 456, 789};
  float zoom = 9.f;
  CHECK_FALSE(zoomframing::FrameBoxInView(mn, mxBad, right, up, back, kAspect, false, 60.f, target, &zoom));
  CHECK(target[0] == 123);
  CHECK(zoom == 9.f);
}
