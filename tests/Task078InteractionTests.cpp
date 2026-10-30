// TASK-0.7.8 — Qt interaction bug batch: shared command/viewport behavior guards.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "ViewportPickPolicy.hpp"

namespace {

void ParkCamera(AppCommandState& st) {
  st.viewportPanX = 0.0;
  st.viewportPanY = 0.0;
  st.viewportZoom = 1.f;
  st.uiViewportWidthPx = 1280.f;
  st.uiViewportHeightPx = 800.f;
}

}  // namespace

TEST_CASE("MOVE NeedDestination publishes annotation transform previews", "[task078]") {
  AppCommandState st;
  st.active = AppCommandState::Kind::Move;
  st.modifyPhase = AppCommandState::ModifyPhase::NeedDestination;
  st.modifyBaseX = 0.f;
  st.modifyBaseY = 0.f;
  CadAnnotation ann{};
  ann.kind = CadAnnotation::Kind::Mtext;
  ann.insX = 5.f;
  ann.insY = 10.f;
  ann.boxMinX = 5.f;
  ann.boxMinY = 10.f;
  ann.boxMaxX = 25.f;
  ann.boxMaxY = 20.f;
  st.cadAnnotations.push_back(ann);
  st.selection.push_back({SelectedEntity::Type::Annotation, 0});

  std::vector<CadAnnotation> previews;
  CadAnnotationCollectTransformPreviews(st, 100.f, 50.f, &previews);
  REQUIRE(previews.size() == 1);
  CHECK(previews[0].insX == Catch::Approx(105.f));
  CHECK(previews[0].insY == Catch::Approx(60.f));
  CHECK(previews[0].boxMinX == Catch::Approx(105.f));
  CHECK(previews[0].boxMinY == Catch::Approx(60.f));
}

TEST_CASE("SelectionAccumulate beginBoxOnMiss false toggles entity without re-arming box", "[task078]") {
  AppCommandState st;
  ParkCamera(st);
  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  st.userLineAttrs.resize(1);
  st.active = AppCommandState::Kind::Move;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = -5.f;
  st.selBoxAnchorY = -5.f;
  std::vector<std::string> log;

  REQUIRE(SubmitViewportSelectionAccumulateClick(st, 5.f, 0.f, 640.f, 400.f, false, false, nullptr,
                                                 st.uiViewportWidthPx, st.uiViewportHeightPx,
                                                 CadViewCamera(st).orthoHalfH, log, false));
  REQUIRE(st.selection.size() == 1);
  CHECK(st.selection.front().type == SelectedEntity::Type::LineSeg);
  CHECK(st.selection.front().index == 0);

  st.selection.clear();
  st.selBoxWaitingSecond = true;
  REQUIRE(!SubmitViewportSelectionAccumulateClick(st, -50.f, -50.f, 100.f, 100.f, false, false, nullptr,
                                                  st.uiViewportWidthPx, st.uiViewportHeightPx,
                                                  CadViewCamera(st).orthoHalfH, log, false));
  CHECK(st.selection.empty());
}

TEST_CASE("Empty MOVE logs REQ-121 selection prompt", "[task078][req121]") {
  AppCommandState st;
  std::vector<std::string> log;
  char buf[] = "MOVE";
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
  REQUIRE(st.active == AppCommandState::Kind::Move);
  REQUIRE(st.modifyPhase == AppCommandState::ModifyPhase::PickSelection);
  REQUIRE(std::any_of(log.begin(), log.end(), [](const std::string& line) {
    return line.find(kSelectObjectsPrompt) != std::string::npos;
  }));
}
