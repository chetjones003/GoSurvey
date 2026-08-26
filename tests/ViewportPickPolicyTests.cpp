// TASK-099 — every pick-driven command must be routed by the viewport.
//
// This exists because of a specific bug and would have caught it. The decision "what does a click
// in the model-space viewport mean right now?" lived inline in `DrawDrawingViewport` as an
// `if / else if` whitelist on `cmd.active`. A command missing from that whitelist did not error,
// did not log, and did not draw — it silently discarded every click and appeared to hang on its
// first prompt.
//
// It happened to RECT. Then to FEATURELINE (TASK-082 BUG-1). Then, at once, to all five of
// REQ-103's MIRROR, LENGTHEN, EXTEND, BREAK and STRETCH — which shipped with green headless
// transcripts throughout, because the driver's `PICK` verb calls `SubmitViewportPick` directly and
// never touches the routing layer at all.
//
// Two guards now stand between that bug and the next command. `ViewportClickRouteFor` is an
// exhaustive `switch` with no `default:`, so the compiler objects when `Kind` grows. And this
// file: a command that reaches its first prompt and routes to `Ignore` is, by definition, a
// command whose clicks go nowhere.

#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "ViewportPickPolicy.hpp"

using K = AppCommandState::Kind;

namespace {

// The command's first prompt — the state a user is in the instant after typing its name, which is
// where a routing omission bites. Set by hand rather than by calling Start*Command, so this test
// keeps linking nothing from the command layer (ADR-002: AppCommandState is plain data).
AppCommandState AtFirstPrompt(K kind) {
  AppCommandState st;
  st.active = kind;
  return st;
}

} // namespace

TEST_CASE("Every pick-driven command is routed by the model-space viewport", "[viewport][pick][req103][task099]") {
  // Each of these takes a viewport click at its FIRST prompt. If any routes to Ignore, that
  // command hangs in the real application no matter how correct its state machine is.
  const K kPickDriven[] = {
      // Draw commands.
      K::Line, K::Circle, K::Polyline, K::FeatureLine, K::Rect, K::Arc, K::Ellipse,
      K::Text, K::Mtext, K::DimAligned, K::DimLinear, K::DimAngular,
      // Inquiry / placement.
      K::IdPoint, K::SurveyInverse, K::SurfaceElevGrade, K::Paste,
      // Modify commands.
      K::Move, K::Copy, K::Rotate, K::Scale, K::Array, K::Delete, K::Join, K::Trim, K::Offset, K::Align,
      // REQ-103 — the five this test was written for.
      K::Mirror, K::Lengthen, K::Extend, K::Break, K::Stretch,
      // Entity designators and view tools that take a click.
      K::DesignateBreakline, K::DesignateBoundary, K::Zoom, K::Hatch,
  };

  for (K kind : kPickDriven) {
    const AppCommandState st = AtFirstPrompt(kind);
    INFO("command: " << AppCommandState::KindName(kind));
    REQUIRE(ViewportClickRouteFor(st) != ViewportClickRoute::Ignore);
  }
}

TEST_CASE("REQ-103's modify commands route the way their picks need", "[viewport][pick][req103][task099]") {
  SECTION("MIRROR window-selects first, then takes mirror-line points") {
    AppCommandState st = AtFirstPrompt(K::Mirror);
    st.mirrorPhase = AppCommandState::MirrorPhase::PickSelection;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionAccumulate);
    st.mirrorPhase = AppCommandState::MirrorPhase::NeedP1;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
    st.mirrorPhase = AppCommandState::MirrorPhase::NeedP2;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  }

  SECTION("LENGTHEN searches for an entity, except in DYnamic's second pick") {
    AppCommandState st = AtFirstPrompt(K::Lengthen);
    st.lengthenPhase = AppCommandState::LengthenPhase::WaitSelectOrMode;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::RawEntityPick);
    st.lengthenPhase = AppCommandState::LengthenPhase::WaitDynamicTarget;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  }

  SECTION("EXTEND searches for an entity in both phases") {
    AppCommandState st = AtFirstPrompt(K::Extend);
    st.extendPhase = AppCommandState::ExtendPhase::SelectBoundaries;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::RawEntityPick);
    st.extendPhase = AppCommandState::ExtendPhase::SelectTargets;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::RawEntityPick);
  }

  SECTION("BREAK searches for the entity, then takes a point on it") {
    AppCommandState st = AtFirstPrompt(K::Break);
    st.breakPhase = AppCommandState::BreakPhase::SelectFirstPoint;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::RawEntityPick);
    st.breakPhase = AppCommandState::BreakPhase::SelectSecondPoint;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  }

  SECTION("ARRAY click-or-box-selects first, then routes its spatial phases and ignores its typed-only ones") {
    using AP = AppCommandState::ArrayPhase;
    AppCommandState st = AtFirstPrompt(K::Array);
    st.arrayPhase = AP::PickSelection;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionAccumulate);
    st.arrayPhase = AP::WaitType;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);
    st.arrayPhase = AP::Rect_WaitColumns;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);
    st.arrayPhase = AP::Rect_WaitColumnSpacing;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
    st.arrayPhase = AP::Rect_WaitRows;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);
    st.arrayPhase = AP::Rect_WaitRowSpacing;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
    st.arrayPhase = AP::Polar_WaitCenter;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
    st.arrayPhase = AP::Polar_WaitItemCount;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);
    st.arrayPhase = AP::Polar_WaitAngle;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
    st.arrayPhase = AP::Polar_WaitRotateAnswer;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);
  }

  SECTION("STRETCH box-selects first, then takes base and destination") {
    AppCommandState st = AtFirstPrompt(K::Stretch);
    st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionBox);
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
    st.modifyPhase = AppCommandState::ModifyPhase::NeedDestination;
    REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  }
}

TEST_CASE("MOVE/COPY/SCALE/ROTATE/ALIGN/ARRAY accumulate clicks and boxes; STRETCH stays box-only",
          "[viewport][pick][req305]") {
  // This fix (user report "fix the array command"): the modify commands' selection step used to
  // accept ONLY a two-corner window/crossing box (ViewportClickRoute::SelectionBox), with no way to
  // click individual objects and no way to keep adding after one box. D-2026-08-25-n extended the
  // click-or-box, accumulate-until-Enter shape ARRAY needed to its five siblings that share the
  // same PickSelection phase concept, EXCEPT STRETCH — its crossing box IS load-bearing geometry
  // (REQ-103 step 5: which vertices move), not just an object filter, so it deliberately keeps the
  // old SelectionBox-only shape.
  AppCommandState mv = AtFirstPrompt(K::Move);
  mv.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(mv) == ViewportClickRoute::SelectionAccumulate);

  AppCommandState cp = AtFirstPrompt(K::Copy);
  cp.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(cp) == ViewportClickRoute::SelectionAccumulate);

  AppCommandState sc = AtFirstPrompt(K::Scale);
  sc.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(sc) == ViewportClickRoute::SelectionAccumulate);

  AppCommandState rt = AtFirstPrompt(K::Rotate);
  rt.rotatePhase = AppCommandState::RotatePhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(rt) == ViewportClickRoute::SelectionAccumulate);

  AppCommandState al = AtFirstPrompt(K::Align);
  al.alignPhase = AppCommandState::AlignPhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(al) == ViewportClickRoute::SelectionAccumulate);

  AppCommandState st = AtFirstPrompt(K::Stretch);
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionBox);
}

TEST_CASE("Ignore is a decision, not an omission", "[viewport][pick][task099]") {
  // These deliberately take no model-space click. Pinned so that "routes to Ignore" stays a
  // statement someone made on purpose, and the test above stays meaningful.
  const K kClickless[] = {
      K::Pan,       // drag-driven
      K::Orbit,     // drag-driven
      K::TrimState, // system-variable text prompt
      K::Elev,      // text prompt
      K::VpFreeze,  // REQ-046: picks inside a floating viewport, not in model space
      K::VpThaw,
      K::PaperRectViewport, // REQ-033: paper space only
  };
  for (K kind : kClickless) {
    INFO("command: " << AppCommandState::KindName(kind));
    REQUIRE(ViewportClickRouteFor(AtFirstPrompt(kind)) == ViewportClickRoute::Ignore);
  }

  // PDFATTACH is the one command that is click-less in some phases and not others.
  AppCommandState pdf = AtFirstPrompt(K::PdfAttach);
  pdf.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
  REQUIRE(ViewportClickRouteFor(pdf) == ViewportClickRoute::Ignore);
  pdf.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitInsertPoint;
  REQUIRE(ViewportClickRouteFor(pdf) == ViewportClickRoute::PdfAttachInsertPoint);
}

TEST_CASE("No active command means idle selection, not nothing", "[viewport][pick][task099]") {
  // Grips, click-select and box-select all hang off this route; routing it to Ignore would make
  // the drawing unselectable.
  REQUIRE(ViewportClickRouteFor(AtFirstPrompt(K::None)) == ViewportClickRoute::IdleSelection);
}
