#pragma once

#include <algorithm>
#include <cstdint>

#include "CadCommands.hpp"

/// When building a selection window (fence), picks should use **unsnapped** device-mapped world coordinates so the
/// rectangle matches the drag. Drawing commands use snapped \p outCursor world coordinates instead.
inline bool ViewportUseRawWorldForSelectionRectPick(const AppCommandState& cmd) {
  return stepengine::ActiveUseRawWorldForSelectionRect(cmd);
}

/// What a left-click in the **model-space** viewport means for the currently active command.
///
/// TASK-099. This decision used to live inline in `DrawDrawingViewport` as a hundred-line
/// `if / else if` whitelist on `cmd.active`, which no test could reach — the headless driver's
/// `PICK` verb calls `SubmitViewportPick` directly and never sees it. A command omitted from that
/// whitelist silently discarded every click and appeared to hang on its first prompt. It happened
/// to RECT, then to FEATURELINE (TASK-082 BUG-1), then to all five of REQ-103's MIRROR / LENGTHEN
/// / EXTEND / BREAK / STRETCH at once.
///
/// Two things stop it happening a fourth time, and both depend on this staying one pure function:
///   1. the `switch` below is exhaustive over `AppCommandState::Kind` with **no `default:` label**,
///      so adding a Kind without a routing decision raises C4062 (MSVC /W4) / `-Wswitch`
///      (GCC/Clang) at the point the enum grows;
///   2. `ViewportPickPolicyTests` asserts every pick-driven command routes somewhere, and the
///      headless `CLICK` verb drives transcripts through this same function.
///
/// `Ignore` is a real answer, not a gap: PAN/ORBIT are drag-driven, TRIMSTATE/ELEV are text
/// prompts, and PaperRectViewport is paper-space-only. Each is listed explicitly and says why.
enum class ViewportClickRoute : std::uint8_t {
  /// The click means nothing to this command in model space; fall through to nothing.
  Ignore,
  /// No command is running: grips, entity click-select, box-select close-out.
  IdleSelection,
  /// Fence: arm the first corner if none is armed, otherwise close the box.
  SelectionBox,
  /// MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN/ARRAY's "select objects" step (this fix, GitHub issue
  /// report "fix the array command"): a click toggles one entity/annotation/fill/survey point into
  /// the accumulating selection (additive; Shift removes), same as idle click-select but without
  /// idle's grip/double-click-edit side effects; a click on empty space arms or closes the
  /// window/crossing fence, which merges into the same accumulating selection instead of closing
  /// out the phase — the phase advances only on Enter (`ProcessCommandLineSubmit`'s per-command
  /// PickSelection branch). STRETCH is deliberately NOT part of this: its crossing box is
  /// load-bearing geometry (REQ-103 step 5 tests each entity's definition points against the box
  /// to decide what moves), not just an object filter, so it keeps the plain two-corner-only
  /// \c SelectionBox shape.
  SelectionAccumulate,
  /// Entity pick — hit-tested against the **raw, unsnapped** cursor, the way
  /// `PickClosestCadEntity` expects. An OSNAP-adjusted point would hit-test somewhere the user
  /// is not pointing.
  RawEntityPick,
  /// Coordinate pick — the OSNAP-adjusted commit point.
  SnappedPointPick,
  /// TRIM owns its own entry point (`SubmitTrimViewportPick`) and its own tolerance.
  TrimPick,
  /// HATCH traces a boundary from the click rather than feeding the command state machine.
  HatchPick,
  /// PDFATTACH's insertion point has its own commit function.
  PdfAttachInsertPoint,
  /// INSERT's on-screen insertion point, scale, or rotation pick.
  InsertBlockPick,
  /// INSERT align-to-face: ray pick on a planar solid face (issue #475 inc3).
  InsertBlockAlignFacePick,
  /// BCONNECT face pick while BEDIT is open (issue #475 inc5).
  BconnectFacePick,
  /// EXTRACTCENTERLINE (REQ-347) owns its own entry point (`SubmitExtractCenterlineViewportPick`):
  /// the click commits whatever cylinder fit the hover already computed, not a fresh coordinate or
  /// entity pick — the same "own entry point" shape TRIM and HATCH already have above.
  ExtractCenterlinePick,
  /// A command is asking for one **face of a solid** — `SECTIONPLANE`'s only step (REQ-342).
  ///
  /// A sub-object pick, not an entity pick, and until now it existed only as `Ctrl`+click in
  /// `CadUi.cpp`, dispatched ABOVE this table and invisible to it. That is exactly how REQ-335's
  /// selection step shipped broken twice: the click fell through to ordinary entity selection and
  /// pulled the user out of the command, and then the face did not pre-highlight because the hover
  /// carries its own separate `Ctrl` gate. Naming the step here is what lets a test assert it —
  /// `HeadlessDriver`'s `PICK` verb calls `SubmitViewportPick` directly and never reaches the
  /// routing layer at all, so no transcript can cover this, however many steps it has.
  SubObjectFacePick,
  /// 3DMOVE / 3DROTATE / 3DSCALE past their selection step (GitHub issue #564 section 3): the click
  /// grabs or drops a gizmo handle through `SubmitGizmoClick`, and a click off every handle is not
  /// a selection change — the command is holding that selection for its handles.
  GizmoHandlePick,
};

inline ViewportClickRoute ViewportClickRouteFromStepRoute(stepengine::StepClickRoute r) {
  using S = stepengine::StepClickRoute;
  using R = ViewportClickRoute;
  switch (r) {
  case S::Ignore:                   return R::Ignore;
  case S::IdleSelection:            return R::IdleSelection;
  case S::SelectionBox:             return R::SelectionBox;
  case S::SelectionAccumulate:      return R::SelectionAccumulate;
  case S::RawEntityPick:            return R::RawEntityPick;
  case S::SnappedPointPick:         return R::SnappedPointPick;
  case S::TrimPick:                 return R::TrimPick;
  case S::HatchPick:                return R::HatchPick;
  case S::PdfAttachInsertPoint:     return R::PdfAttachInsertPoint;
  case S::InsertBlockPick:          return R::InsertBlockPick;
  case S::InsertBlockAlignFacePick: return R::InsertBlockAlignFacePick;
  case S::BconnectFacePick:         return R::BconnectFacePick;
  case S::ExtractCenterlinePick:    return R::ExtractCenterlinePick;
  case S::SubObjectFacePick:        return R::SubObjectFacePick;
  case S::GizmoHandlePick:          return R::GizmoHandlePick;
  }
  return R::Ignore;
}

/// \see ViewportClickRoute. Model space (and floating model space) only — pure paper space has its
/// own click block, and is not routed through here.
///
/// ADR-0012: Every interactive command declares its click route on its active step descriptor, so
/// this delegates directly to the shared step engine.
inline ViewportClickRoute ViewportClickRouteFor(const AppCommandState& cmd) {
  return ViewportClickRouteFromStepRoute(stepengine::ActiveClickRoute(cmd));
}

/// Is the active command asking **which objects** rather than **which point**? (REQ-121)
///
/// This is the single predicate REQ-121 requires all three of its rules to consult — OSNAP
/// suppression, the pickbox cursor, and the standardized prompt. Having one answer, derived here,
/// is the point: the alternative is each rule maintaining its own list of commands, which is how
/// ALIGN ended up missing from `ViewportUseRawWorldForSelectionRectPick` above.
///
/// It is derived from the ROUTE rather than from `cmd.active`, so it inherits
/// `ViewportClickRouteFor`'s exhaustiveness for free: a new command Kind must first be given a
/// route (or the `default:`-less switch above fails to compile), and that route already answers
/// this question. A command therefore cannot be added to the program and silently omitted from the
/// selection treatment — the failure is at build time, which is REQ-121's own acceptance condition.
///
/// The `switch` below is likewise `default:`-less, so adding a ROUTE forces a decision here too.
///
/// **Idle is deliberately false** (REQ-121, settled 2026-08-26). The treatment is a mode signal,
/// and a mode signal only means something against a default — idle is that default. Note the
/// existing snap gate already reaches the same conclusion by a different road: snapping is keyed on
/// `midCmd`, so with no command running nothing snaps anyway.
///
/// `HatchPick` is false and is worth saying why: HATCH clicks a point INSIDE a region to trace a
/// boundary from it. That is a location, not an object — REQ-121's list does not include HATCH.
inline bool ViewportIsObjectSelectionStep(const AppCommandState& cmd) {
  using R = ViewportClickRoute;

  // ZOOM is the one command the route cannot answer for, and it is excluded (REQ-121).
  //
  // `SelectionBox` conflates two different meanings: STRETCH drags a box to choose OBJECTS (and
  // to define which vertices move), ZOOM drags one to choose a REGION OF THE VIEW to fit. Nothing
  // is selected by the latter, so a pickbox would say "click a thing" while the user drags a
  // rectangle, and the standardized prompt would be a sentence that is simply untrue.
  //
  // DELETE and JOIN used to share this route and this ambiguity; they now accumulate until Enter
  // like every other object-selection step (D-2026-08-26-d), which leaves STRETCH as the only
  // object-selecting user of `SelectionBox`.
  //
  // Splitting `SelectionBox` into two enumerators would say this in the type system, and was
  // considered and declined: the route is consumed by a switch in the click handler, so a new
  // enumerator changes click routing to fix a cursor question. One named exception with a reason is
  // the smaller change (CLAUDE.md rule 1). If a second region-picking command ever appears, split it
  // then — that is the point at which the abstraction earns its keep.
  if (stepengine::ActiveIsZoomRegionBox(cmd))
    return false;

  switch (ViewportClickRouteFor(cmd)) {
  // "Which objects?" — the fence commands, the accumulating select-objects step, the one-entity-
  // per-click loops (EXTEND / FILLET / CHAMFER / LENGTHEN / BREAK's first phase / OFFSET), and TRIM.
  // TRIM owning a separate click entry point (`SubmitTrimViewportPick`) does not matter here: this
  // asks what the PHASE means, not which function consumes the click.
  case R::SelectionBox:
  case R::SelectionAccumulate:
  case R::RawEntityPick:
  case R::TrimPick:
  // A face of a solid IS an object to point at, so the pickbox cursor and the OSNAP suppression
  // that go with a selection step are both right here (REQ-342). What it is NOT is an ENTITY
  // selection — see ViewportIsFacePickStep, which is what the click and hover gates test.
  case R::SubObjectFacePick:
    return true;

  // "Which point?", or nothing at all.
  case R::IdleSelection:
  case R::SnappedPointPick:
  case R::HatchPick:
  // EXTRACTCENTERLINE clicks the location the hover already fit, not an object (REQ-347) — same
  // reasoning as HatchPick just above.
  case R::ExtractCenterlinePick:
  case R::PdfAttachInsertPoint:
  case R::InsertBlockPick:
  case R::InsertBlockAlignFacePick:
  case R::BconnectFacePick:
  // A gizmo handle is a widget to drag, not an object to select (GitHub issue #564).
  case R::GizmoHandlePick:
  case R::Ignore:
    return false;
  }
  return false;  // unreachable; see ViewportClickRouteFor's tail note
}

/// True while a command is asking for one **face of a solid** (REQ-342, GitHub issue #479).
///
/// Two gates in `CadUi.cpp` decide whether a sub-object is picked and whether one pre-highlights,
/// and both required `Ctrl` to be held:
///
///   - the click, `subObjectClick` — without this predicate the click falls straight through to
///     ordinary entity selection, which pulls the user out of the command they are in the middle
///     of. Reported against `SECTION` on 2026-09-10: *"it takes me out of the section and just
///     selects the object by itself."*
///   - the hover, `subObjectHovering` — a SEPARATE gate, so fixing only the click gives a command
///     whose clicks work and whose faces never light up. Reported immediately after the first fix:
///     *"that click works, it is just not highlighting the object."*
///
/// Both bugs were found by hand in the running app, twice, because nothing automated could see
/// them. This predicate is the thing a test can hold on to.
[[nodiscard]] inline bool ViewportIsFacePickStep(const AppCommandState& cmd) {
  return ViewportClickRouteFor(cmd) == ViewportClickRoute::SubObjectFacePick;
}

/// Paper-space counterpart of \ref ViewportIsObjectSelectionStep (REQ-307, GitHub #106).
///
/// Paper-space MOVE/COPY/DELETE are NOT routed through `ViewportClickRouteFor` at all — that
/// function is model-space (and floating model space) only; pure paper space has its own ambient
/// click block in `CadUi.cpp` and never sets `cmd.active` for these three commands. They stay
/// pick-first by default (act on whatever is already selected in idle mode), so unlike model space
/// there is usually no selection STEP to ask about. This predicate answers true only during the one
/// case that changed: `StartPaperMoveCopyViewports`/`StartDeleteCommand`'s paper branch opened a
/// real selection step because nothing was selected yet when the command started (the paper-space
/// analog of REQ-121's model-space treatment — same pickbox cursor, same OSNAP suppression, same
/// shared prompt, reached from `PaperIsObjectSelectionStep(cmd) || ViewportIsObjectSelectionStep(cmd)`
/// at each of REQ-121's three call sites).
inline bool PaperIsObjectSelectionStep(const AppCommandState& cmd) {
  return cmd.paperMoveWaitingSelection || cmd.paperDeleteWaitingSelection;
}

/// What the viewport cursor looks like right now (REQ-450).
///
/// Normal use is a crosshair with a central pickbox; an object-selection step (REQ-121, model or
/// paper space) shows the pickbox alone. Kept as a pure function of the command state so the rule
/// is testable without a window. Lengths are in screen pixels.
struct ViewportCursorShape {
  bool  arms = true;       ///< False in an object-selection step: pickbox only.
  float armLengthPx = 0.f; ///< Total length of each arm line, centred on the pointer.
  float pickHalfPx = 4.f;  ///< Half the pickbox square's width.
};

inline ViewportCursorShape ViewportCursorShapeFor(const AppCommandState& cmd, float viewW, float viewH) {
  ViewportCursorShape shape;
  shape.arms = !(ViewportIsObjectSelectionStep(cmd) || PaperIsObjectSelectionStep(cmd));
  const float pct = std::clamp(static_cast<float>(cmd.displayCrosshairSizePct), 1.f, 100.f);
  shape.armLengthPx = pct * std::min(viewW, viewH) / 100.f;
  shape.pickHalfPx = std::clamp(cmd.viewportCrosshairPickHalfPxX, 2.f, 20.f);
  return shape;
}
