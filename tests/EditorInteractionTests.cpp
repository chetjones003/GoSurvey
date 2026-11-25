#include "CadCommands.hpp"
#include "CadDynInput.hpp"
#include "CadRubberPreview.hpp"
#include "pipe/CadPipeCommands.hpp"
#include "draw/CadDrawSessions.hpp"
#include "engine/StepEngine.hpp"
#include "modify/CadModifyRestSessions.hpp"
#include "modify/CadModifySessions.hpp"
#include "pipe/CadPipeSiteSessions.hpp"
#include <memory>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE("TRIM records and repeats its command", "[editor][issue1624]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartTrimCommand(st, log);
  CancelActiveCommand(st, log);
  RepeatLastCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::Trim);
}

TEST_CASE("MTEXT box preview retains both axes with ORTHO on", "[editor][issue1624]") {
  AppCommandState st;
  auto session = std::make_unique<stepengine::MtextSession>();
  session->phase = stepengine::MtextPhase::WaitCorner2;
  session->x1 = 1; session->y1 = 2;
  stepengine::BeginSession(st, std::move(session), false);
  st.orthoMode = true;
  std::vector<float> lines;
  AppendCadDraftRubberLines(st, 8, 6, true, 0, 0, 20, 800, lines);
  REQUIRE(lines.size() == 24);
  bool foundOpposite = false;
  for (std::size_t i = 0; i + 2 < lines.size(); i += 3)
    if (lines[i] == 8 && lines[i + 1] == 6) foundOpposite = true;
  CHECK(foundOpposite);
}

TEST_CASE("MIRROR mouse axis respects ORTHO and publishes its relative base", "[editor][issue1624]") {
  AppCommandState st;
  std::vector<std::string> log;
  stepengine::BeginSession(st, std::make_unique<stepengine::MirrorSession>(stepengine::MirrorPhase::NeedP1), false);
  st.orthoMode = true;
  SubmitViewportPick(st, 2, 3, log);
  const auto prompt = CadDynInputPromptFor(st);
  REQUIRE(prompt.haveBase);
  CHECK(prompt.baseWorld.x == 2);
  CHECK(prompt.baseWorld.y == 3);
  SubmitViewportPick(st, 10, 5, log);
  const auto* mirror = st.sessionAs<stepengine::MirrorSession>();
  REQUIRE(mirror);
  CHECK(mirror->p2X == 10);
  CHECK(mirror->p2Y == 3);
}

TEST_CASE("Rotating MTEXT preserves wrapping and rotates its local box", "[editor][issue1624]") {
  AppCommandState st;
  CadAnnotation text;
  text.kind = CadAnnotation::Kind::Mtext;
  text.boxMinX = 2; text.boxMaxX = 12;
  text.boxMinY = 1; text.boxMaxY = 5;
  st.drawing.cadAnnotations.push_back(text);
  st.selection.push_back({SelectedEntity::Type::Annotation, 0});
  std::vector<std::string> log;
  stepengine::BeginSession(st, std::make_unique<stepengine::RotateSession>(stepengine::RotatePhase::NeedAngleOrReference), false);
  std::vector<CadAnnotation> preview;
  CadAnnotationCollectTransformPreviews(st, -1, 0, &preview);
  REQUIRE(preview.size() == 1);
  ApplyRotationAboutUcsZ(st, 0, 0, 0, 1.57079632679f, log);
  const auto& rotated = st.drawing.cadAnnotations.front();
  CHECK(rotated.boxMaxX - rotated.boxMinX == Catch::Approx(10));
  CHECK(rotated.boxMaxY - rotated.boxMinY == Catch::Approx(4));
  CHECK(rotated.boxMinX == Catch::Approx(-5));
  CHECK(rotated.boxMaxY == Catch::Approx(2));
  CHECK(rotated.rotationRad == Catch::Approx(1.57079632679f));
  CHECK(preview[0].boxMinX == Catch::Approx(rotated.boxMinX));
  CHECK(preview[0].boxMaxY == Catch::Approx(rotated.boxMaxY));
  CHECK(preview[0].rotationRad == Catch::Approx(rotated.rotationRad));
}

TEST_CASE("Rotated text picking and crossing boxes reject empty AABB corners", "[editor][issue1624]") {
  AppCommandState st;
  CadAnnotation text;
  text.kind = CadAnnotation::Kind::Text;
  text.text = "ABCDEFGHIJ";
  text.plottedHeightInches = 1;
  text.rotationRad = .78539816339f;
  st.drawing.modelUnitsPerPlottedInch = 1;
  st.drawing.cadAnnotations.push_back(text);
  // Actual quad is diagonal; (0,3) lies in its AABB but outside the text.
  CHECK(PickCadAnnotationAt(0, 3, st, 1, 10000) == -1);
  CHECK(PickCadAnnotationAt(2, 2.5f, st, 1, 10000) == 0);
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = -.1f; st.selBoxAnchorY = 2.9f;
  UpdateSelectionBoxPreview(st, .1f, 3.1f, false, nullptr, 0, 0);
  CHECK(st.selBoxPreview.empty());
  st.selBoxAnchorX = 1.9f; st.selBoxAnchorY = 2.4f;
  UpdateSelectionBoxPreview(st, 2.1f, 2.6f, false, nullptr, 0, 0);
  CHECK(st.selBoxPreview.size() == 1);
  // A window around only part of the text cannot fully contain it.
  UpdateSelectionBoxPreview(st, 2.1f, 2.6f, true, nullptr, 0, 0);
  CHECK(st.selBoxPreview.empty());
}

TEST_CASE("TRIM previews the hovered removal before the first fence point", "[editor][issue1624]") {
  AppCommandState st;
  st.drawing.userLinesFlat = {0,0,0, 10,0,0, 5,-5,0, 5,5,0};
  st.viewportLastSurveyLayoutHeightPx = 1000;
  st.viewportLastSurveyLayoutOrthoHalfH = 10;
  std::vector<std::string> log;
  StartTrimCommand(st, log);
  std::vector<float> preview;
  CadTrimAppendCutLineRemovedPreview(st, 2, 0, 2, 0, 2, 0, &preview);
  REQUIRE(preview.size() == 6);
  CHECK(preview[0] == 0);
  CHECK(preview[3] == 5);
  CHECK(st.drawing.line(0).x0 == 0);
  CHECK(st.drawing.line(0).x1 == 10);
  const auto* trim = st.sessionAs<stepengine::TrimSession>();
  REQUIRE(trim);
  CHECK(trim->phase == stepengine::TrimPhase::CuttingLine_WaitP1);
  preview.clear();
  CadTrimAppendCutLineRemovedPreview(st, 2, 3, 2, 3, 2, 3, &preview);
  CHECK(preview.empty());
}

TEST_CASE("Box selection excludes off and frozen layers", "[editor][issue1624]") {
  AppCommandState st;
  st.drawing.userLinesFlat = {0,0,0, 1,1,0};
  EntityAttributes attrs; attrs.layer = "Hidden";
  st.drawing.userLineAttrs.push_back(attrs);
  CadLayerRow layer; layer.name = "Hidden"; layer.on = false;
  st.drawing.drawingLayerTable.push_back(layer);
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = -1; st.selBoxAnchorY = -1;
  UpdateSelectionBoxPreview(st, 2, 2, true, nullptr, 0, 0);
  CHECK(st.selBoxPreview.empty());
}

TEST_CASE("Text picking uses the supplied font measurement", "[editor][issue1624]") {
  AppCommandState st;
  CadAnnotation text;
  text.kind = CadAnnotation::Kind::Text; text.text = "WWW";
  text.plottedHeightInches = 1;
  st.drawing.modelUnitsPerPlottedInch = 1;
  st.drawing.cadAnnotations.push_back(text);
  st.measureAnnotationText = [](float, const std::string&, const std::string&, float* w, float* h) {
    *w = 300; *h = 100;
  };
  CHECK(PickCadAnnotationAt(2.8f, .5f, st, 1, 10000) == 0);
  CHECK(PickCadAnnotationAt(3.2f, .5f, st, 1, 10000) == -1);
}

TEST_CASE("PIPERUN accepts its size on the command line", "[editor][issue1624]") {
  AppCommandState st;
  std::vector<std::string> log;
  char input[] = "PIPERUN 4in";
  ProcessCommandLineSubmit(input, sizeof input, st, log);
  CHECK(st.active == AppCommandState::Kind::PipeRun);
  CHECK(st.pipeRunNominalSize == "4in");
  const auto* pipe = st.sessionAs<stepengine::PipeRunSession>();
  REQUIRE(pipe);
  CHECK(pipe->phase == stepengine::PipeRunPhase::WaitWallThickness);
}

TEST_CASE("PIPERUN typed length follows a live object snap, not the compass", "[editor][issue1624]") {
  AppCommandState st;
  std::vector<std::string> log;
  auto session = std::make_unique<stepengine::PipeRunSession>();
  session->phase = stepengine::PipeRunPhase::WaitNextPoint;
  session->draftVerts = {0.0, 0.0, 0.0};
  stepengine::BeginSession(st, std::move(session), false);
  st.pipeRunCompassOn = true;
  st.polarIncrementDeg = 45.0;
  // A snapped point at 36.87 degrees — between compass spokes, so a compass lock would move it.
  st.viewportSnapPickValid = true;
  st.viewportSnapPickLocalX = 8.0;
  st.viewportSnapPickLocalY = 6.0;
  REQUIRE(HandlePipeRunTextInput("5", st, log));
  const auto* pipe = st.sessionAs<stepengine::PipeRunSession>();
  REQUIRE(pipe);
  REQUIRE(pipe->draftVerts.size() == 6);
  CHECK(pipe->draftVerts[3] == Catch::Approx(4.0).margin(1e-4));
  CHECK(pipe->draftVerts[4] == Catch::Approx(3.0).margin(1e-4));
}
