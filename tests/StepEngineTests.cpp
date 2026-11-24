// ADR-0012 / Issue #1578 (Track B0 + B1): Scripted-input tests for the shared step engine
// (one test per ADR-0012 step kind + all BlankEnterPolicy modes + session lifecycle) and
// the Modify family pilot (MOVE, COPY, ROTATE, SCALE, MIRROR, STRETCH).

#include "catch_amalgamated.hpp"

#include "CadCommands.hpp"
#include "CadDynInput.hpp"
#include "ViewportPickPolicy.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "modify/CadModifySessions.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

void SubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  char buf[512] = {};
  std::snprintf(buf, sizeof(buf), "%s", line.c_str());
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), st, log);
}

void AddLine(AppCommandState& st, double x0, double y0, double x1, double y1) {
  st.drawing.pushLine(CadLineRecord{x0, y0, 0.0, x1, y1, 0.0});
  st.drawing.userLineAttrs.push_back(EntityAttributes{});
}

} // namespace

// ===========================================================================
// Track B0 — One scripted-input test per ADR-0012 step kind
// ===========================================================================

TEST_CASE("StepEngine: SelectObjects step accumulates selection and advances on blank Enter", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, 0.0, 0.0, 10.0, 0.0);

  bool completed = false;
  stepengine::ScriptedStepSpec s;
  s.kind = stepengine::StepKind::SelectObjects;
  s.blankEnter = stepengine::BlankEnterPolicy::Finish;
  s.prompt = kSelectObjectsPrompt;
  s.verbUpper = "TESTSEL";

  auto session = std::make_unique<stepengine::ScriptedStepSession>(
      CadCommandKind::Move, "TESTSEL canceled.", std::vector<stepengine::ScriptedStepSpec>{s},
      [&](AppCommandState&, std::vector<std::string>&) { completed = true; });
  stepengine::BeginSession(st, std::move(session));

  REQUIRE(st.activeSession != nullptr);
  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionAccumulate);
  REQUIRE(ViewportUseRawWorldForSelectionRectPick(st));
  REQUIRE(ActiveCommandPromptHint(st) == kSelectObjectsPrompt);

  // Blank Enter with empty selection stays on step and prompts.
  SubmitLine(st, "", log);
  REQUIRE_FALSE(completed);
  REQUIRE(st.activeSession != nullptr);

  // Drag a selection window around the line; session stays on SelectObjects until Enter.
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = -1.f;
  st.selBoxAnchorY = -1.f;
  SubmitViewportPick(st, 11.0, 1.0, log, false, true);
  REQUIRE(st.selection.size() == 1);
  REQUIRE_FALSE(completed);

  // One Enter confirms selection and finishes the single-step session (dropping activeSession).
  SubmitLine(st, "", log);
  REQUIRE(completed);
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.active == CadCommandKind::None);
}

TEST_CASE("StepEngine: Point step accepts typed absolute, relative, and viewport picks", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::vector<ray3d::Vec3> pts;

  stepengine::ScriptedStepSpec p1;
  p1.kind = stepengine::StepKind::Point;
  p1.prompt = "First point:";
  p1.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    pts.push_back(v.point);
    return true;
  };

  stepengine::ScriptedStepSpec p2;
  p2.kind = stepengine::StepKind::Point;
  p2.prompt = "Second point:";
  p2.allowRelativePoint = true;
  p2.hasPointBase = true;
  p2.pointBaseLocal = ray3d::Vec3{10.0, 20.0, 0.0};
  p2.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    pts.push_back(v.point);
    return true;
  };

  stepengine::ScriptedStepSpec p3;
  p3.kind = stepengine::StepKind::Point;
  p3.prompt = "Third point (pick):";
  p3.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    pts.push_back(v.point);
    return true;
  };

  stepengine::BeginSession(st, std::make_unique<stepengine::ScriptedStepSession>(
                                   CadCommandKind::Line, "Canceled.",
                                   std::vector<stepengine::ScriptedStepSpec>{p1, p2, p3}));

  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  REQUIRE(CadCommandExpectsPointEntry(st));

  SubmitLine(st, "10,20", log);
  REQUIRE(pts.size() == 1);
  REQUIRE(pts[0].x == Catch::Approx(10.0));
  REQUIRE(pts[0].y == Catch::Approx(20.0));

  const CadDynInputPrompt dyn = CadDynInputPromptFor(st);
  REQUIRE(dyn.pointEntry);
  REQUIRE(dyn.haveBase);
  REQUIRE(dyn.baseWorld.x == Catch::Approx(10.0));
  REQUIRE(dyn.baseWorld.y == Catch::Approx(20.0));

  SubmitLine(st, "@5,-3", log);
  REQUIRE(pts.size() == 2);
  REQUIRE(pts[1].x == Catch::Approx(15.0));
  REQUIRE(pts[1].y == Catch::Approx(17.0));

  SubmitViewportPick(st, 42.0, -7.0, log);
  REQUIRE(pts.size() == 3);
  REQUIRE(pts[2].x == Catch::Approx(42.0));
  REQUIRE(pts[2].y == Catch::Approx(-7.0));
  REQUIRE(st.activeSession == nullptr);
}

TEST_CASE("StepEngine: Distance step validates numbers and supports TakeDefault and RepeatLast", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  double rememberedDist = 12.5;
  std::vector<double> got;

  stepengine::ScriptedStepSpec d1;
  d1.kind = stepengine::StepKind::Distance;
  d1.blankEnter = stepengine::BlankEnterPolicy::TakeDefault;
  d1.defaultDistance = 5.0;
  d1.prompt = "Distance <5>:";
  d1.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    got.push_back(v.scalar);
    return true;
  };

  stepengine::ScriptedStepSpec d2;
  d2.kind = stepengine::StepKind::Distance;
  d2.blankEnter = stepengine::BlankEnterPolicy::RepeatLast;
  d2.lastDistanceSlot = &rememberedDist;
  d2.prompt = "Repeat distance:";
  d2.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    got.push_back(v.scalar);
    return true;
  };

  stepengine::ScriptedStepSpec d3;
  d3.kind = stepengine::StepKind::Distance;
  d3.blankEnter = stepengine::BlankEnterPolicy::Finish;
  d3.lastDistanceSlot = &rememberedDist;
  d3.prompt = "Typed distance:";
  d3.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    got.push_back(v.scalar);
    return true;
  };

  stepengine::BeginSession(st, std::make_unique<stepengine::ScriptedStepSession>(
                                   CadCommandKind::Offset, "Canceled.",
                                   std::vector<stepengine::ScriptedStepSpec>{d1, d2, d3}));

  // Invalid negative distance is rejected and stays on step 0.
  SubmitLine(st, "-3", log);
  REQUIRE(got.empty());
  REQUIRE(st.activeSession != nullptr);

  // Blank Enter takes default (5.0).
  SubmitLine(st, "", log);
  REQUIRE(got.size() == 1);
  REQUIRE(got[0] == Catch::Approx(5.0));

  // Blank Enter repeats last external slot (12.5).
  SubmitLine(st, "", log);
  REQUIRE(got.size() == 2);
  REQUIRE(got[1] == Catch::Approx(12.5));

  // Typed positive distance updates external slot and finishes.
  SubmitLine(st, "7.25", log);
  REQUIRE(got.size() == 3);
  REQUIRE(got[2] == Catch::Approx(7.25));
  REQUIRE(rememberedDist == Catch::Approx(7.25));
  REQUIRE(st.activeSession == nullptr);
}

TEST_CASE("StepEngine: Angle step parses decimal and DMS and supports TakeDefault", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::vector<double> degs;

  stepengine::ScriptedStepSpec a1;
  a1.kind = stepengine::StepKind::Angle;
  a1.blankEnter = stepengine::BlankEnterPolicy::TakeDefault;
  a1.defaultAngleDeg = 90.0;
  a1.prompt = "Angle 1:";
  a1.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    degs.push_back(v.scalar);
    return true;
  };

  stepengine::ScriptedStepSpec a2 = a1;
  a2.prompt = "Angle 2 (DMS):";

  stepengine::BeginSession(st, std::make_unique<stepengine::ScriptedStepSession>(
                                   CadCommandKind::Rotate, "Canceled.",
                                   std::vector<stepengine::ScriptedStepSpec>{a1, a2}));

  SubmitLine(st, "", log);
  REQUIRE(degs.size() == 1);
  REQUIRE(degs[0] == Catch::Approx(90.0));

  SubmitLine(st, "45d30m0s", log);
  REQUIRE(degs.size() == 2);
  REQUIRE(degs[1] == Catch::Approx(45.5));
  REQUIRE(st.activeSession == nullptr);
}

TEST_CASE("StepEngine: KeywordOptions step matches aliases case-insensitively and handles defaults", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::vector<std::string> chosen;

  stepengine::ScriptedStepSpec k1;
  k1.kind = stepengine::StepKind::KeywordOptions;
  k1.blankEnter = stepengine::BlankEnterPolicy::TakeDefault;
  k1.defaultKeyword = "No";
  k1.keywords = {{"Yes", {"y", "yes"}}, {"No", {"n", "no"}}};
  k1.prompt = "Erase? [Yes/No] <N>:";
  k1.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    chosen.push_back(v.text);
    return true;
  };

  stepengine::ScriptedStepSpec k2 = k1;
  stepengine::BeginSession(st, std::make_unique<stepengine::ScriptedStepSession>(
                                   CadCommandKind::Mirror, "Canceled.",
                                   std::vector<stepengine::ScriptedStepSpec>{k1, k2}));

  // Unknown option rejected.
  SubmitLine(st, "maybe", log);
  REQUIRE(chosen.empty());

  // Alias match.
  SubmitLine(st, "Y", log);
  REQUIRE(chosen.size() == 1);
  REQUIRE(chosen[0] == "Yes");

  // Blank Enter takes default "No" and finishes.
  SubmitLine(st, "", log);
  REQUIRE(chosen.size() == 2);
  REQUIRE(chosen[1] == "No");
  REQUIRE(st.activeSession == nullptr);
}

TEST_CASE("StepEngine: Text step captures arbitrary text and BlankEnterPolicy::Finish ends immediately", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::string captured;
  bool finished = false;

  stepengine::ScriptedStepSpec t1;
  t1.kind = stepengine::StepKind::Text;
  t1.blankEnter = stepengine::BlankEnterPolicy::TakeDefault;
  t1.defaultText = "DEFAULT";
  t1.prompt = "Label:";
  t1.onAccept = [&](AppCommandState&, const stepengine::StepValue& v, std::vector<std::string>&) {
    captured = v.text;
    return true;
  };

  stepengine::ScriptedStepSpec t2;
  t2.kind = stepengine::StepKind::Text;
  t2.blankEnter = stepengine::BlankEnterPolicy::Finish;
  t2.prompt = "Optional second line (Enter to finish):";

  stepengine::BeginSession(st, std::make_unique<stepengine::ScriptedStepSession>(
                                   CadCommandKind::Text, "Canceled.",
                                   std::vector<stepengine::ScriptedStepSpec>{t1, t2},
                                   [&](AppCommandState&, std::vector<std::string>&) { finished = true; }));

  SubmitLine(st, "Bench Mark 101", log);
  REQUIRE(captured == "Bench Mark 101");
  REQUIRE_FALSE(finished);

  // One blank Enter finishes the command immediately.
  SubmitLine(st, "", log);
  REQUIRE(finished);
  REQUIRE(st.activeSession == nullptr);
}

TEST_CASE("StepEngine: EntityPick step routes raw entity clicks and drops session on Esc", "[stepengine]") {
  AppCommandState st;
  std::vector<std::string> log;
  int picks = 0;

  stepengine::ScriptedStepSpec e1;
  e1.kind = stepengine::StepKind::EntityPick;
  e1.blankEnter = stepengine::BlankEnterPolicy::Finish;
  e1.prompt = "Pick entity:";
  e1.onAccept = [&](AppCommandState&, const stepengine::StepValue&, std::vector<std::string>&) {
    ++picks;
    return false; // stay on step for looping picks
  };

  stepengine::BeginSession(st, std::make_unique<stepengine::ScriptedStepSession>(
                                   CadCommandKind::Fillet, "PICK canceled.",
                                   std::vector<stepengine::ScriptedStepSpec>{e1}));

  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::RawEntityPick);
  SubmitViewportPick(st, 3.0, 4.0, log);
  REQUIRE(picks == 1);
  REQUIRE(st.activeSession != nullptr);

  CancelActiveCommand(st, log);
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.active == CadCommandKind::None);
  REQUIRE(!log.empty());
  REQUIRE(log.back() == "PICK canceled.");
}

// ===========================================================================
// Track B1 Pilot — Modify family (MOVE, COPY, ROTATE, SCALE, MIRROR)
// ===========================================================================

TEST_CASE("ModifyPilot: MOVE session owns state, translates selection, and drops cleanly on Esc", "[stepengine][modify]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, 0.0, 0.0, 10.0, 0.0);
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});

  SubmitLine(st, "move", log);
  REQUIRE(st.active == CadCommandKind::Move);
  REQUIRE(st.sessionAs<stepengine::MoveCopySession>() != nullptr);
  REQUIRE(st.sessionAs<stepengine::MoveCopySession>()->phase == stepengine::ModifyPhase::NeedBase);
  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  REQUIRE(CadCommandExpectsPointEntry(st));

  SubmitLine(st, "0,0", log);
  REQUIRE(st.sessionAs<stepengine::MoveCopySession>()->phase == stepengine::ModifyPhase::NeedDestination);
  REQUIRE(CadDynInputPromptFor(st).haveBase);

  SubmitLine(st, "5,7", log);
  REQUIRE(st.drawing.line(0).x0 == Catch::Approx(5.0));
  REQUIRE(st.drawing.line(0).y0 == Catch::Approx(7.0));
  REQUIRE(st.drawing.line(0).x1 == Catch::Approx(15.0));
  REQUIRE(st.drawing.line(0).y1 == Catch::Approx(7.0));

  CancelActiveCommand(st, log);
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.active == CadCommandKind::None);
}

TEST_CASE("ModifyPilot: MIRROR blank Enter at erase prompt takes default No and drops session", "[stepengine][modify]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, 2.0, 0.0, 2.0, 10.0);
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});

  SubmitLine(st, "mirror", log);
  REQUIRE(st.sessionAs<stepengine::MirrorSession>() != nullptr);
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "0,10", log);
  REQUIRE(st.sessionAs<stepengine::MirrorSession>()->phase == stepengine::MirrorPhase::NeedEraseAnswer);
  REQUIRE_FALSE(CadCommandExpectsPointEntry(st));

  // One Enter accepts <N> default, keeps original + adds reflected copy, drops session.
  SubmitLine(st, "", log);
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.active == CadCommandKind::None);
  REQUIRE(st.drawing.lineCount() == 2);
  REQUIRE(st.drawing.line(1).x0 == Catch::Approx(-2.0));
  REQUIRE(st.drawing.line(1).x1 == Catch::Approx(-2.0));
}

TEST_CASE("ModifyPilot: SCALE and ROTATE sessions complete and drop session state", "[stepengine][modify]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, 0.0, 0.0, 10.0, 0.0);
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});

  SubmitLine(st, "scale", log);
  REQUIRE(st.sessionAs<stepengine::ScaleSession>() != nullptr);
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "2", log);
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.drawing.line(0).x1 == Catch::Approx(20.0));

  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});
  SubmitLine(st, "rotate", log);
  REQUIRE(st.sessionAs<stepengine::RotateSession>() != nullptr);
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "90", log); // 90 deg CW from north -> (20,0) -> (0,-20)
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.drawing.line(0).x1 == Catch::Approx(0.0).margin(1e-4));
  REQUIRE(st.drawing.line(0).y1 == Catch::Approx(-20.0).margin(1e-4));

  // RepeatLastCommand dispatches through CommandRegistry for repeatable commands.
  RepeatLastCommand(st, log);
  REQUIRE(st.active == CadCommandKind::Rotate);
  REQUIRE(st.sessionAs<stepengine::RotateSession>() != nullptr);
  CancelActiveCommand(st, log);
  REQUIRE(st.activeSession == nullptr);
}

TEST_CASE("CommandRegistry: UCS polar prompt, ZOOM region box, and GIZMO setting route via step engine",
          "[stepengine][ucs][view][gizmo]") {
  AppCommandState st;
  std::vector<std::string> log;

  SubmitLine(st, "ucs", log);
  REQUIRE(st.active == CadCommandKind::Ucs);
  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  REQUIRE(CadCommandExpectsPointEntry(st));
  SubmitLine(st, "10,20", log);
  ray3d::Vec3 base{};
  REQUIRE(CadUcsPolarPromptBase(st, &base));
  REQUIRE(base.x == Catch::Approx(10.0));
  REQUIRE(base.y == Catch::Approx(20.0));
  SubmitLine(st, "", log); // blank Enter accepts origin-only UCS
  REQUIRE(st.activeSession == nullptr);
  REQUIRE(st.active == CadCommandKind::None);

  SubmitLine(st, "zoomwindow", log);
  REQUIRE(st.active == CadCommandKind::Zoom);
  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionBox);
  REQUIRE(ViewportUseRawWorldForSelectionRectPick(st));
  REQUIRE_FALSE(ViewportIsObjectSelectionStep(st));
  CancelActiveCommand(st, log);
  REQUIRE(st.activeSession == nullptr);

  SubmitLine(st, "gizmo move", log);
  REQUIRE(st.gizmoPersistent);
  REQUIRE(st.gizmoOp == CadGizmoOp::Translate);
  SubmitLine(st, "gizmo off", log);
  REQUIRE_FALSE(st.gizmoPersistent);
}

TEST_CASE("Session ownership: UCS, PLAN, 3DMOVE, CHPROP, MATCHPROP, BCONNECT, PDFATTACH, and MVIEW own their state",
          "[stepengine][sessions]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, 0.0, 0.0, 10.0, 0.0);

  // UCS session owns phase & pending picks and drops cleanly on Esc.
  SubmitLine(st, "ucs", log);
  REQUIRE(st.sessionAs<stepengine::UcsSession>() != nullptr);
  CHECK(st.sessionAs<stepengine::UcsSession>()->phase == stepengine::UcsPhase::WaitOriginOrOption);
  SubmitLine(st, "5,6", log);
  REQUIRE(st.sessionAs<stepengine::UcsSession>()->phase == stepengine::UcsPhase::WaitXAxisPoint);
  CHECK(st.sessionAs<stepengine::UcsSession>()->pendingOrigin.x == Catch::Approx(5.0));
  CancelActiveCommand(st, log);
  CHECK(st.activeSession == nullptr);

  // PLAN session owns phase and drops cleanly on finish.
  SubmitLine(st, "plan", log);
  REQUIRE(st.sessionAs<stepengine::PlanSession>() != nullptr);
  CHECK(st.sessionAs<stepengine::PlanSession>()->phase == stepengine::PlanPhase::WaitOption);
  SubmitLine(st, "world", log);
  CHECK(st.activeSession == nullptr);

  // 3DMOVE session owns GizmoCmdPhase and restores gizmoOp on Esc.
  st.gizmoOp = CadGizmoOp::Rotate;
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});
  SubmitLine(st, "3dmove", log);
  REQUIRE(st.sessionAs<stepengine::GizmoCmdSession>() != nullptr);
  CHECK(st.sessionAs<stepengine::GizmoCmdSession>()->phase == stepengine::GizmoCmdPhase::BasePoint);
  CHECK(st.gizmoOp == CadGizmoOp::Translate);
  CancelActiveCommand(st, log);
  CHECK(st.activeSession == nullptr);
  CHECK(st.gizmoOp == CadGizmoOp::Rotate);

  // CHPROP session owns PropCmdPhase and ChPropProperty.
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});
  SubmitLine(st, "chprop", log);
  REQUIRE(st.sessionAs<stepengine::PropCmdSession>() != nullptr);
  CHECK(st.sessionAs<stepengine::PropCmdSession>()->phase == stepengine::PropCmdPhase::WaitProperty);
  SubmitLine(st, "color", log);
  CHECK(st.sessionAs<stepengine::PropCmdSession>()->phase == stepengine::PropCmdPhase::WaitValue);
  CHECK(st.sessionAs<stepengine::PropCmdSession>()->chPropProperty == stepengine::ChPropProperty::Color);
  SubmitLine(st, "red", log);
  CHECK(st.sessionAs<stepengine::PropCmdSession>()->phase == stepengine::PropCmdPhase::WaitProperty);
  SubmitLine(st, "", log);
  CHECK(st.activeSession == nullptr);

  // BCONNECT session owns awaitingFace and routes BconnectFacePick through StepEngine.
  CadBlockDefinition def;
  def.name = "PORTFIT";
  st.drawing.blockDefs.push_back(def);
  SubmitLine(st, "bedit PORTFIT", log);
  REQUIRE(st.blockEditActive);
  SubmitLine(st, "bconnect P1", log);
  REQUIRE(st.sessionAs<stepengine::BConnectSession>() != nullptr);
  CHECK(st.sessionAs<stepengine::BConnectSession>()->name == "P1");
  SubmitLine(st, "4in", log);
  SubmitLine(st, "", log);
  SubmitLine(st, "", log);
  SubmitLine(st, "", log);
  REQUIRE(st.sessionAs<stepengine::BConnectSession>()->awaitingFace);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::BconnectFacePick);
  CancelActiveCommand(st, log);
  CHECK(st.activeSession == nullptr);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::IdleSelection);

  // PDFATTACH session owns PdfAttachPhase and drops on Esc.
  SubmitLine(st, "pdfattach", log);
  REQUIRE(st.sessionAs<stepengine::PdfAttachSession>() != nullptr);
  CHECK(st.sessionAs<stepengine::PdfAttachSession>()->phase == stepengine::PdfAttachPhase::WaitDialog);
  CancelActiveCommand(st, log);
  CHECK(st.activeSession == nullptr);
  CHECK_FALSE(st.pdfAttachDialogOpen);
}
