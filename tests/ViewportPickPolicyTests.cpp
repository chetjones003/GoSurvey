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

#include <string>
#include <utility>

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
      K::IdPoint, K::SurveyInverse, K::SurfaceElevGrade, K::WaterDrop, K::Catchment, K::SwapTinEdge,
      K::AddTinPoint, K::DelTinPoint, K::MoveTinPoint, K::DelTinLine, K::QuickProfile, K::Paste,
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

TEST_CASE("REQ-121: DELETE and JOIN accumulate too, leaving ZOOM and STRETCH on the box route",
          "[viewport][pick][req121]") {
  // GitHub #91 review, D-2026-08-26-d. PR #102 gave every object-selection step the shared prompt
  // "Select objects, ENTER to continue". For these two it was FALSE: both were fixed two-click-box
  // commands whose box executed on close, and whose Enter handler explicitly refused ("finish
  // window-select in the viewport (two clicks)"). The behaviour was corrected to match the prompt
  // rather than the prompt weakened to match the behaviour, so rule (3) stays a rule.
  //
  // Routing is the half that is testable here; that Enter now ACTS on the accumulated selection is
  // `headless.req121-delete-join-accumulate`, which drives this same function through the CLICK
  // verb.
  REQUIRE(ViewportClickRouteFor(AtFirstPrompt(K::Delete)) == ViewportClickRoute::SelectionAccumulate);
  REQUIRE(ViewportClickRouteFor(AtFirstPrompt(K::Join)) == ViewportClickRoute::SelectionAccumulate);

  // Both remain selection steps for the cursor/OSNAP/prompt rules — the route changed, the answer
  // to "is this a selection step?" did not.
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Delete)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Join)));

  // What is left on `SelectionBox` afterwards, asserted so the route does not quietly empty out:
  // STRETCH (a selection step whose box is load-bearing geometry) and ZOOM (not a selection step
  // at all). Those two are the whole reason the enumerator still exists.
  AppCommandState str = AtFirstPrompt(K::Stretch);
  str.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportClickRouteFor(str) == ViewportClickRoute::SelectionBox);
  REQUIRE(ViewportClickRouteFor(AtFirstPrompt(K::Zoom)) == ViewportClickRoute::SelectionBox);
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

// ---------------------------------------------------------------------------------------------
// REQ-121 — object selection is a visibly distinct mode.
// ---------------------------------------------------------------------------------------------

TEST_CASE("REQ-121: ALIGN's selection box uses UNSNAPPED corners like its six siblings",
          "[viewport][pick][req121]") {
  // THE RED-BEFORE CASE. ALIGN routes its PickSelection phase to SelectionAccumulate alongside
  // MOVE/COPY/SCALE/ROTATE/MIRROR/ARRAY, but was missing from
  // `ViewportUseRawWorldForSelectionRectPick` — so ALIGN alone built its fence from SNAPPED
  // coordinates while every sibling used raw. Nobody decided that; it is the per-command accident
  // an unstated rule produces, and it is the concrete defect REQ-121 was argued from.
  //
  // Asserted against the siblings rather than alone, because "ALIGN is raw" is only meaningful as
  // "ALIGN is raw *like the others*".
  const std::pair<K, AppCommandState::AlignPhase> kAlignSel{K::Align,
                                                            AppCommandState::AlignPhase::PickSelection};
  AppCommandState al = AtFirstPrompt(kAlignSel.first);
  al.alignPhase = kAlignSel.second;
  REQUIRE(ViewportUseRawWorldForSelectionRectPick(al));

  AppCommandState mv = AtFirstPrompt(K::Move);
  mv.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportUseRawWorldForSelectionRectPick(mv));

  AppCommandState ar = AtFirstPrompt(K::Array);
  ar.arrayPhase = AppCommandState::ArrayPhase::PickSelection;
  REQUIRE(ViewportUseRawWorldForSelectionRectPick(ar));
}

TEST_CASE("REQ-121: every object-selection step is recognised as one", "[viewport][pick][req121]") {
  // The predicate all three of REQ-121's rules consult. A command missing from it keeps the
  // crosshair, keeps OSNAP jumping the cursor, and keeps its own prompt wording — which is the
  // whole defect, one command at a time.
  AppCommandState mv = AtFirstPrompt(K::Move);
  mv.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportIsObjectSelectionStep(mv));

  AppCommandState mi = AtFirstPrompt(K::Mirror);
  mi.mirrorPhase = AppCommandState::MirrorPhase::PickSelection;
  REQUIRE(ViewportIsObjectSelectionStep(mi));

  AppCommandState st = AtFirstPrompt(K::Stretch);
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  REQUIRE(ViewportIsObjectSelectionStep(st));

  // The one-entity-per-click loops. These pick objects too, and TRIM owning its own click entry
  // point (`SubmitTrimViewportPick`) does not change what its phase MEANS.
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Delete)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Join)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Trim)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Extend)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Fillet)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Chamfer)));
  REQUIRE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Offset)));
}

TEST_CASE("REQ-121: a point pick, idle, and ZOOM are NOT selection steps", "[viewport][pick][req121]") {
  // Each of these is a deliberate exclusion with its own reason, and each would be a visible defect
  // if it flipped — so they are asserted rather than left to the absence of a test.

  // A coordinate phase of a command whose FIRST phase is a selection step. The predicate must track
  // the phase, not the command: MOVE stops being a selection step the moment it has its objects.
  AppCommandState mvBase = AtFirstPrompt(K::Move);
  mvBase.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
  REQUIRE_FALSE(ViewportIsObjectSelectionStep(mvBase));

  // Idle. Excluded 2026-08-26: the treatment is a mode signal, and idle is the default it signals
  // against. A user who never starts a command must not be able to tell REQ-121 shipped.
  REQUIRE_FALSE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::None)));

  // ZOOM. #91 lists it, and it is excluded anyway: its box picks a REGION of the view to fit, not
  // objects. "Select objects" would be a prompt that lies, and a pickbox would say "click a thing"
  // while the user drags a rectangle. It shares `SelectionBox` with STRETCH — which IS a selection
  // step — so this is the assertion that pins the one exception the route alone cannot express.
  // (DELETE and JOIN shared that route until D-2026-08-26-d moved them to SelectionAccumulate.)
  REQUIRE_FALSE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Zoom)));

  // A pure point command, for contrast.
  REQUIRE_FALSE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Line)));
  // HATCH clicks a point INSIDE a region to trace a boundary — a location, not an object.
  REQUIRE_FALSE(ViewportIsObjectSelectionStep(AtFirstPrompt(K::Hatch)));
}

// ---------------------------------------------------------------------------------------------
// REQ-307 (GitHub #106) — the paper-space counterpart of REQ-121's predicate.
// ---------------------------------------------------------------------------------------------

TEST_CASE("REQ-307: PaperIsObjectSelectionStep is true only in the two new paper selection flags",
          "[viewport][pick][req307]") {
  // Neither flag set — the ordinary pick-first state (idle, or mid-command with a real selection
  // already made) — is not a selection step. This is the default REQ-307 must not disturb: a paper
  // MOVE/COPY/DELETE that already had something selected never touches these flags at all.
  AppCommandState idle;
  REQUIRE_FALSE(PaperIsObjectSelectionStep(idle));

  AppCommandState moving;
  moving.paperMovePhase = 1;  // base-point phase — a MOVE gesture IS in progress, but not selecting
  REQUIRE_FALSE(PaperIsObjectSelectionStep(moving));

  AppCommandState moveSelecting;
  moveSelecting.paperMoveWaitingSelection = true;
  REQUIRE(PaperIsObjectSelectionStep(moveSelecting));

  AppCommandState deleteSelecting;
  deleteSelecting.paperDeleteWaitingSelection = true;
  REQUIRE(PaperIsObjectSelectionStep(deleteSelecting));
}
