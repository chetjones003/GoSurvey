#pragma once

#include <cstdint>

#include "CadCommands.hpp"

/// When building a selection window (fence), picks should use **unsnapped** device-mapped world coordinates so the
/// rectangle matches the drag. Drawing commands use snapped \p outCursor world coordinates instead.
inline bool ViewportUseRawWorldForSelectionRectPick(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using RP = AppCommandState::RotatePhase;
  using MirP = AppCommandState::MirrorPhase;
  return cmd.active == K::None || cmd.active == K::Delete || cmd.active == K::Join || cmd.active == K::Trim ||
         cmd.active == K::Zoom || (cmd.active == K::Move && cmd.modifyPhase == MP::PickSelection) ||
         (cmd.active == K::Copy && cmd.modifyPhase == MP::PickSelection) ||
         (cmd.active == K::Scale && cmd.modifyPhase == MP::PickSelection) ||
         // REQ-103: MIRROR and STRETCH open with the same window-select MOVE/COPY/SCALE do, so their
         // fence corners must come from the same unsnapped drag coordinates (TASK-099 F1).
         (cmd.active == K::Mirror && cmd.mirrorPhase == MirP::PickSelection) ||
         (cmd.active == K::Stretch && cmd.modifyPhase == MP::PickSelection) ||
         (cmd.active == K::Rotate && cmd.rotatePhase == RP::PickSelection) ||
         // REQ-305: ARRAY opens with the same window-select MOVE/COPY/SCALE do.
         (cmd.active == K::Array && cmd.arrayPhase == AppCommandState::ArrayPhase::PickSelection) ||
         // REQ-121: ALIGN was MISSING here. It routes its PickSelection phase to
         // `SelectionAccumulate` alongside its six siblings (see `ViewportClickRouteFor` below), so
         // its fence corners must come from the same unsnapped drag coordinates they do — but this
         // list was never extended when ALIGN joined them, so ALIGN alone built its box from SNAPPED
         // coordinates. Nobody decided that; it is the per-command accident an unstated rule
         // produces, and closing it is part of REQ-121 rather than a separate fix.
         (cmd.active == K::Align && cmd.alignPhase == AppCommandState::AlignPhase::PickSelection);
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
};

/// \see ViewportClickRoute. Model space (and floating model space) only — pure paper space has its
/// own click block, and is not routed through here.
inline ViewportClickRoute ViewportClickRouteFor(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  using R = ViewportClickRoute;
  using MP = AppCommandState::ModifyPhase;

  switch (cmd.active) {
  case K::None:
    return R::IdleSelection;

  // --- Point-picking draw commands: the click is a coordinate, handed to the state machine. ---
  case K::Line:
  case K::Circle:
  case K::Polyline:
  case K::FeatureLine:
  case K::Rect:
  case K::Arc:
  case K::Ellipse:
  case K::Text:
  case K::Mtext:
  case K::DimAligned:
  case K::DimLinear:
  case K::DimAngular:
  case K::IdPoint:
  case K::SurveyInverse:
  case K::Paste:
  case K::SurfaceElevGrade:
    return R::SnappedPointPick;

  // --- Entity-pick commands: raw cursor, hit-tested by PickClosestCadEntity. ---
  case K::Offset:
  case K::DesignateBreakline:
  case K::DesignateBoundary:
    return R::RawEntityPick;

  // --- Select-then-point modify commands: window-select first, then coordinates. ---
  case K::Move:
  case K::Copy:
  case K::Scale:
    return cmd.modifyPhase == MP::PickSelection ? R::SelectionAccumulate : R::SnappedPointPick;
  case K::Stretch:
    // REQ-103 step 5: the crossing/window box IS the operation's data (which vertices move), not
    // just an object filter, so STRETCH keeps the plain two-corner \c SelectionBox shape rather
    // than gaining click-select-and-accumulate the way its siblings just did.
    return cmd.modifyPhase == MP::PickSelection ? R::SelectionBox : R::SnappedPointPick;
  case K::Rotate:
    return cmd.rotatePhase == AppCommandState::RotatePhase::PickSelection ? R::SelectionAccumulate
                                                                          : R::SnappedPointPick;
  case K::Align:
    return cmd.alignPhase == AppCommandState::AlignPhase::PickSelection ? R::SelectionAccumulate
                                                                       : R::SnappedPointPick;
  case K::Mirror:
    // REQ-103 step 1. NeedEraseAnswer is a Yes/No text prompt, but routing its click to the state
    // machine is harmless (SubmitViewportPickImpl ignores it) and keeps this branch phase-simple.
    return cmd.mirrorPhase == AppCommandState::MirrorPhase::PickSelection ? R::SelectionAccumulate
                                                                         : R::SnappedPointPick;
  case K::Array: {
    // REQ-305. PickSelection is the click-or-window-select MOVE/COPY/ROTATE share. The three
    // spatial phases (column/row spacing, polar center, fill angle) commit from a click, same
    // shape as OFFSET's distance-or-click. The typed-only phases (type choice, the two item
    // counts, rotate Yes/No) take no viewport click — SubmitViewportPickImpl has no branch for
    // them either, so routing anything but Ignore there would be a click that goes nowhere.
    using APh = AppCommandState::ArrayPhase;
    switch (cmd.arrayPhase) {
    case APh::PickSelection:
      return R::SelectionAccumulate;
    case APh::Rect_WaitColumnSpacing:
    case APh::Rect_WaitRowSpacing:
    case APh::Polar_WaitCenter:
    case APh::Polar_WaitAngle:
      return R::SnappedPointPick;
    case APh::WaitType:
    case APh::Rect_WaitColumns:
    case APh::Rect_WaitRows:
    case APh::Polar_WaitItemCount:
    case APh::Polar_WaitRotateAnswer:
      return R::Ignore;
    }
    return R::Ignore;
  }

  // --- Pure fence commands. ---
  case K::Delete:
  case K::Join:
  case K::Zoom:
    return R::SelectionBox;

  // --- REQ-103 loop-style commands: one entity per click, no selection set. ---
  case K::Lengthen:
    // DYnamic's second click is a coordinate resolved to a length; every other phase searches for
    // an entity under the cursor (ASSUMPTION-2 in TASK-099).
    return cmd.lengthenPhase == AppCommandState::LengthenPhase::WaitDynamicTarget
               ? R::SnappedPointPick
               : R::RawEntityPick;
  case K::Extend:
    // Both phases (boundary edges, then targets) are entity searches — TRIM's shape.
    return R::RawEntityPick;
  case K::Break:
    // Phase 1 searches for the entity; phase 2 is a point on the entity already chosen, projected
    // by ClosestPointOnEntity either way, so it snaps (ASSUMPTION-1 in TASK-099).
    return cmd.breakPhase == AppCommandState::BreakPhase::SelectFirstPoint ? R::RawEntityPick
                                                                          : R::SnappedPointPick;
  case K::Fillet:
  case K::Chamfer:
    // Both phases (first curve, then second curve) are entity searches — EXTEND's shape, never a
    // bare coordinate.
    return R::RawEntityPick;

  // --- Commands owning their own click handling. ---
  case K::Trim:
    return R::TrimPick;
  case K::Hatch:
    return R::HatchPick;
  case K::PdfAttach:
    return cmd.pdfAttachPhase == AppCommandState::PdfAttachPhase::WaitInsertPoint
               ? R::PdfAttachInsertPoint
               : R::Ignore;  // dialog / async build / scale / rotation phases take no viewport click

  // --- Deliberately click-less in model space. ---
  case K::Pan:
  case K::Orbit:
    return R::Ignore;  // drag-driven; a click alone means nothing
  case K::TrimState:
  case K::Elev:
    return R::Ignore;  // system-variable text prompts, answered on the command line
  case K::VpFreeze:
  case K::VpThaw:
    return R::Ignore;  // REQ-046: layer freezing is per-viewport, so these pick inside a floating
                       // viewport only — model space has no viewport to freeze a layer in
  case K::PaperRectViewport:
    return R::Ignore;  // REQ-033: paper space only, handled by the paper click block
  }
  // Unreachable for any handled Kind. Kept (rather than left to fall off the end) so a Kind added
  // without a case is a compiler warning and a failing ViewportPickPolicyTests case, not UB.
  return R::Ignore;
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
  // `SelectionBox` conflates two different meanings: DELETE/JOIN/STRETCH drag a box to choose
  // OBJECTS, ZOOM drags one to choose a REGION OF THE VIEW to fit. Nothing is selected by the
  // latter, so a pickbox would say "click a thing" while the user drags a rectangle, and the
  // standardized prompt would be a sentence that is simply untrue.
  //
  // Splitting `SelectionBox` into two enumerators would say this in the type system, and was
  // considered and declined: the route is consumed by a switch in the click handler, so a new
  // enumerator changes click routing to fix a cursor question. One named exception with a reason is
  // the smaller change (CLAUDE.md rule 1). If a second region-picking command ever appears, split it
  // then — that is the point at which the abstraction earns its keep.
  if (cmd.active == AppCommandState::Kind::Zoom)
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
    return true;

  // "Which point?", or nothing at all.
  case R::IdleSelection:
  case R::SnappedPointPick:
  case R::HatchPick:
  case R::PdfAttachInsertPoint:
  case R::Ignore:
    return false;
  }
  return false;  // unreachable; see ViewportClickRouteFor's tail note
}
