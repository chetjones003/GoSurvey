#include "CadCommands.hpp"
#include "CadDynInput.hpp"
#include "CadPropertyEdit.hpp"
#include "CadRubberPreview.hpp"
#include "pipe/CadPipeCommands.hpp"
#include "draw/CadDrawSessions.hpp"
#include "engine/StepEngine.hpp"
#include "modify/CadModifyRestSessions.hpp"
#include "modify/CadModifySessions.hpp"
#include "pipe/CadPipeSiteSessions.hpp"
#include <limits>
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

TEST_CASE("Dimension size overrides edit, clear, undo, and track drawn label placement (issue #1676)",
          "[editor][dimension]") {
  AppCommandState st;
  st.drawing.modelUnitsPerPlottedInch = 120.f;
  st.drawing.annotationScales = {{"1\" = 10'", 1.f, 120.f}, {"1\" = 40'", 1.f, 480.f}};
  st.currentAnnotationScaleIndex = 1;  // 1" = 40'
  st.viewportDimArrowScale = 1.5f;
  st.drawing.activeDimensionStyle.textSizeInches = 0.1f;
  st.drawing.activeDimensionStyle.arrowSizeInches = 0.1f;
  st.drawing.activeDimensionStyle.props["annotative"] = "1";
  st.drawing.dimensionStyles = {st.drawing.activeDimensionStyle};

  CadAnnotation dim;
  dim.kind = CadAnnotation::Kind::DimAligned;
  dim.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  dim.dimExt1X = 0.0; dim.dimExt1Y = 0.0;
  dim.dimExt2X = 500.0; dim.dimExt2Y = 0.0;
  dim.dimSignedOffset = 20.f;
  dim.insX = 250.f; dim.insY = 30.f;  // stale stored spot from 1"=10'
  dim.text = "500.00";
  dim.dimTextUserPlaced = false;
  st.drawing.cadAnnotations.push_back(dim);

  // Derived readout reflects annotation scale and arrow multiplier before overrides.
  const auto base = propedit::ReadDimensionSizes(st, st.drawing.cadAnnotations[0]);
  CHECK(base.textHeightModel == Catch::Approx(48.0));
  CHECK(base.arrowSizeModel == Catch::Approx(72.0));
  CHECK(base.labelY == Catch::Approx(20.0 + 1.08 * 48.0).epsilon(1e-3));

  // Picking hits the lifted auto-placed label at 1"=40', not only the old stored insY.
  CHECK(PickCadAnnotationAt(static_cast<float>(base.labelX), static_cast<float>(base.labelY), st, 100.f, 1000.f) == 0);

  // Setting overrides applies and can be undone.
  std::vector<std::string> log;
  const auto setText = propedit::SetTextNumber(st, 0, propedit::TextField::DimTextHeightModel, 3.5);
  CHECK(setText.result == propedit::Result::Applied);
  const auto setArrow = propedit::SetTextNumber(st, 0, propedit::TextField::DimArrowSizeModel, 2.25);
  CHECK(setArrow.result == propedit::Result::Applied);
  const auto overridden = propedit::ReadDimensionSizes(st, st.drawing.cadAnnotations[0]);
  CHECK(overridden.textHeightModel == Catch::Approx(3.5));
  CHECK(overridden.arrowSizeModel == Catch::Approx(2.25));

  REQUIRE(DoUndo(st, log));
  CHECK(st.drawing.cadAnnotations[0].dimArrowSizeModel == 0.0);
  CHECK(st.drawing.cadAnnotations[0].dimTextHeightModel == Catch::Approx(3.5));
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawing.cadAnnotations[0].dimTextHeightModel == 0.0);

  // Zero and negative clear back to "follow the style"; NaN is rejected.
  REQUIRE(propedit::SetTextNumber(st, 0, propedit::TextField::DimTextHeightModel, 4.0).result ==
          propedit::Result::Applied);
  CHECK(propedit::SetTextNumber(st, 0, propedit::TextField::DimTextHeightModel, 0.0).result ==
        propedit::Result::Applied);
  CHECK(st.drawing.cadAnnotations[0].dimTextHeightModel == 0.0);
  CHECK(propedit::SetTextNumber(st, 0, propedit::TextField::DimArrowSizeModel, -5.0).result ==
        propedit::Result::Unchanged);
  CHECK(propedit::SetTextNumber(st, 0, propedit::TextField::DimTextHeightModel,
                                std::numeric_limits<double>::quiet_NaN()).result == propedit::Result::Rejected);

  // Editing Insertion X on an auto-placed dimension marks it user-placed without jumping Y back to stale insY.
  const auto beforeMove = propedit::ReadDimensionSizes(st, st.drawing.cadAnnotations[0]);
  REQUIRE(propedit::SetTextNumber(st, 0, propedit::TextField::InsertionX, 260.0).result ==
          propedit::Result::Applied);
  CHECK(st.drawing.cadAnnotations[0].dimTextUserPlaced);
  CHECK(st.drawing.cadAnnotations[0].insX == Catch::Approx(260.f));
  CHECK(st.drawing.cadAnnotations[0].insY == Catch::Approx(beforeMove.labelY).epsilon(1e-3));
  REQUIRE(DoUndo(st, log));
  CHECK_FALSE(st.drawing.cadAnnotations[0].dimTextUserPlaced);

  // Non-dimension annotations reject dimension size overrides.
  CadAnnotation txt;
  txt.kind = CadAnnotation::Kind::Text;
  st.drawing.cadAnnotations.push_back(txt);
  CHECK(propedit::SetTextNumber(st, 1, propedit::TextField::DimTextHeightModel, 2.0).result ==
        propedit::Result::Rejected);
}

TEST_CASE("MTEXT frame: the height follows the wrapped content, so grips enclose the text", "[editor][issue1667]") {
  // The reported case: "TEST MTEXT" in a 0.458-wide frame dragged at 1" = 5', drawn at 1" = 50'.
  AppCommandState st;
  st.drawing.modelUnitsPerPlottedInch = 50;  // 0.125 in -> 6.25 model units
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.text = "TEST MTEXT";
  a.plottedHeightInches = 0.125f;
  a.insX = 6; a.insY = 28;
  CadMtextPlaceFrame(a, 0.458f, 1.5f);
  const float h = 6.25f;
  CHECK(CadMtextContentHeight(st, a, false) == Catch::Approx(h * (1.3f + 1.4f)));  // two lines
  CadMtextFitHeight(st, a, false);
  CHECK(CadMtextFrameHeight(a) == Catch::Approx(h * 2.7f));
  CHECK(CadMtextFrameWidth(a) == Catch::Approx(0.458f));
  CHECK(a.insX == Catch::Approx(6));  // top-left attachment stays where it was placed
  CHECK(a.insY == Catch::Approx(28));
  // Wide enough for one line: the frame shrinks back to one line.
  CadMtextPlaceFrame(a, 100.f, CadMtextFrameHeight(a));
  CadMtextFitHeight(st, a, false);
  CHECK(CadMtextFrameHeight(a) == Catch::Approx(h * 1.3f));
}
