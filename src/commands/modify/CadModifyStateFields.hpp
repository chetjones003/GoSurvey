  // --- MOVE / COPY ---
  enum class ModifyPhase : std::uint8_t { PickSelection, NeedBase, NeedDestination } modifyPhase = ModifyPhase::PickSelection;

  float modifyBaseX = 0.f;
  float modifyBaseY = 0.f;
  /// REQ-322: the base point's elevation, so a typed MOVE can carry a Z. Zero when the base was
  /// typed without one, which is every drawing that predates 3D MOVE.
  float modifyBaseZ = 0.f;
  /// \c Kind::Scale — after base pick: reference length (world) so scale = new length / this value (or distance /
  /// base-to-cursor over this value in \c ScalePhase::FactorPick).
  float scaleRefDist = 1.f;
  /// \c Kind::Scale — sub-state while \c modifyPhase == NeedDestination (after base).
  enum class ScalePhase : std::uint8_t {
    FactorPick, ///< Default: \c scaleRefDist from selection vs base; scale = dist(base,cursor)/ref or typed factor.
    Ref_WaitP1, ///< First point of explicit reference length segment.
    Ref_WaitP2, ///< Second point sets \c scaleRefDist.
    NewLength_WaitTypedOrP1, ///< Type new length, or pick first point of new-length segment.
    NewLength_WaitP2, ///< Second pick completes scale = dist(new seg)/\c scaleRefDist.
  } scalePhase = ScalePhase::FactorPick;
  float scaleRefP1X = 0.f, scaleRefP1Y = 0.f;
  float scaleNewLenP1X = 0.f, scaleNewLenP1Y = 0.f;

  // --- ROTATE ---
  enum class RotatePhase : std::uint8_t {
    PickSelection,
    NeedBase,
    NeedAngleOrReference, ///< decimal/DMS **clockwise from north**, or R reference
    Ref_WaitP1,
    Ref_WaitP2,
    AfterReference_WaitAngleOrP, ///< numeric/DMS or "p" for new angle via two-point line (bearing)
    AnglePoints_WaitP1,
    AnglePoints_WaitP2,
  } rotatePhase = RotatePhase::PickSelection;

  float rotateBaseX = 0.f;
  float rotateBaseY = 0.f;
  float rotateBaseZ = 0.f;  // REQ-329 increment 2: the rotation axis passes through this elevation
  float rotateRefX1 = 0.f, rotateRefY1 = 0.f;
  float rotateRefX2 = 0.f, rotateRefY2 = 0.f;
  float rotateAnglePt1X = 0.f, rotateAnglePt1Y = 0.f;
  /// After base point: \p C / \p COPY toggles rotate–copy (keep originals); cleared when rotate finishes or draft resets.
  bool rotateCopyMode = false;
  /// COPY modal: when true, duplicate survey selection by pending rotation instead of translation.
  bool pendingSurveyDupIsRotate = false;
  float pendingRotateCopyBx = 0.f, pendingRotateCopyBy = 0.f, pendingRotateCopyRad = 0.f;

  // --- MIRROR (REQ-103 step 1) ---
  enum class MirrorPhase : std::uint8_t {
    PickSelection,
    NeedP1,          ///< first point of the mirror line
    NeedP2,          ///< second point of the mirror line
    NeedEraseAnswer, ///< "Erase source objects? [Yes/No] <N>", default No
  } mirrorPhase = MirrorPhase::PickSelection;

  float mirrorP1X = 0.f, mirrorP1Y = 0.f;
  float mirrorP2X = 0.f, mirrorP2Y = 0.f;
  /// REQ-329 increment 5: each mirror-line point's elevation on the active work plane, so a tilted
  /// UCS reflects across the plane that contains the line rather than a world-vertical plane.
  float mirrorP1Z = 0.f, mirrorP2Z = 0.f;
  /// COPY modal: when true, duplicate survey selection by pending reflection instead of
  /// translation/rotation. Checked ahead of \ref pendingSurveyDupIsRotate.
  bool pendingSurveyDupIsMirror = false;
  float pendingMirrorX0 = 0.f, pendingMirrorY0 = 0.f, pendingMirrorX1 = 0.f, pendingMirrorY1 = 0.f;
  /// MIRROR erase-source, survey half: the CAD-side erase already ran (or didn't) synchronously in
  /// \c FinishMirrorCommand; this says whether the ORIGINAL survey points selected must also be
  /// removed once the duplicate-policy modal resolves (the duplicate itself is necessarily deferred
  /// — the modal decides its id policy — but "erase the source" is not, so it is applied here rather
  /// than left racing the modal).
  bool mirrorEraseSourcePending = false;

  // --- ARRAY (REQ-305) ---
  enum class ArrayType : std::uint8_t { Rectangular, Polar } arrayType = ArrayType::Rectangular;
  enum class ArrayPhase : std::uint8_t {
    PickSelection,
    WaitType,               ///< "[R]ectangular / [P]olar"
    Rect_WaitColumns,       ///< typed integer only — not spatial
    Rect_WaitColumnSpacing, ///< typed number, or click (horizontal distance from the anchor)
    Rect_WaitRows,          ///< typed integer only
    Rect_WaitRowSpacing,    ///< typed number, or click (vertical distance from the anchor)
    Rect_WaitLevels,        ///< typed integer only (GitHub issue #400 inc 2); Enter/0/1 = 2D, commits immediately
    Rect_WaitLevelSpacing,  ///< typed number ONLY — no click (REQ-305 acceptance 12); commits on completion
    Polar_WaitCenter,       ///< click / typed X,Y / object snap — the normal point-input path
    Polar_WaitItemCount,    ///< typed integer only — TOTAL instances including the original
    Polar_WaitAngle,        ///< typed degrees, or click (angle from center to cursor)
    Polar_WaitRotateAnswer, ///< "[Y]es / [N]o"; commits on completion
  } arrayPhase = ArrayPhase::PickSelection;
  /// True while ARRAY is operating on native entities in the active paper layout.
  bool arrayOnPaper = false;

  int arrayCols = 0;
  int arrayRows = 0;
  float arrayColSpacing = 0.f;
  float arrayRowSpacing = 0.f;
  /// GitHub issue #400 increment 2 / REQ-305 acceptance 12. 1 level (the default) is the pre-#400
  /// 2D grid; the level-spacing prompt is skipped entirely at 0 or 1, so an ordinary 2D rectangular
  /// array types no more than it always has.
  int arrayLevels = 1;
  /// Distance between levels along the active UCS Z axis. Typed-number entry only (no interactive
  /// click) — see REQ-305 acceptance 12 for why a viewport click cannot express a distance along the
  /// work plane's own normal.
  float arrayLevelSpacing = 0.f;
  /// Anchor for interactive column/row-spacing entry (Rect_WaitColumnSpacing/RowSpacing read the
  /// cursor's distance from this point) — the same point \ref FirstSelectionAnchorPoint computes,
  /// cached once at WaitType so it does not shift while spacing is being dragged.
  float arrayAnchorX = 0.f, arrayAnchorY = 0.f;
  /// Z of the anchor above (GitHub issue #400 increment 1) — needed to build the UCS work plane
  /// column/row spacing is measured in when the active UCS is not World.
  float arrayAnchorZ = 0.f;

  float arrayCenterX = 0.f, arrayCenterY = 0.f;
  /// Z of the polar center above (GitHub issue #400 increment 1). Only meaningful for logging /
  /// the work-plane anchor — polar rotation itself is about a vertical (world-Z-parallel) axis
  /// through (arrayCenterX, arrayCenterY) regardless of Z, per REQ-305 acceptance 11.
  float arrayCenterZ = 0.f;
  int arrayItemCount = 0;
  float arrayFillAngleDeg = 360.f;
  bool arrayRotateItems = true;

  // --- LENGTHEN (REQ-103 step 2) ---
  /// Default sub-mode is **Total**, not AutoCAD's DElta (D-2026-08-24-f). Paired with the
  /// pick-first entry, that makes the out-of-the-box interaction the one people actually reach
  /// for: pick a line, read what it measures, type what you want it to measure. DElta's "add 50
  /// to whatever this is" needs the current length as context anyway, which a bare prompt before
  /// the pick cannot give.
  enum class LengthenMode : std::uint8_t { Delta, Percent, Total, Dynamic } lengthenMode = LengthenMode::Total;
  enum class LengthenPhase : std::uint8_t {
    WaitSelectOrMode,  ///< pick an object (applies current mode+value), or type DE/P/T/DY to (re)set the mode
    WaitDeltaValue,    ///< typed signed length to add/subtract
    WaitPercentValue,  ///< typed percentage of current length (100 = unchanged)
    WaitTotalValue,    ///< typed total length
    WaitDynamicTarget, ///< object picked in Dynamic mode; waiting for the new-length pick/type
  } lengthenPhase = LengthenPhase::WaitSelectOrMode;
  float lengthenDeltaValue = 0.f;
  float lengthenPercentValue = 100.f;
  float lengthenTotalValue = 0.f;
  /// Whether a value has ever been set for the CURRENT \ref lengthenMode this session — a bare pick
  /// at \c WaitSelectOrMode before any mode value exists is refused with a prompt to type one first,
  /// rather than silently applying a stray default (REQ-201).
  bool lengthenModeValueSet = false;
  /// Dynamic mode: the object picked at \ref LengthenPhase::WaitDynamicTarget, and which of its two
  /// stored endpoints is nearest that pick — true selects the FIRST endpoint in storage order
  /// (a Line's (x0,y0); an Arc's start angle; a Polyline's first vertex in its range), false the
  /// second. One bool covers all three entity kinds because each already orders its two ends the
  /// same "first/second in storage" way.
  SelectedEntity lengthenPendingEntity{};
  bool lengthenPendingNearFirst = false;
  float lengthenPendingCurrentLength = 0.f;
  /// REQ-103 LENGTHEN, pick-first entry (TASK-100). Set when a pick arrived before the active
  /// sub-mode had a value: the entity is latched here and the mode's value prompt is opened, so
  /// the value that follows applies to THAT object immediately rather than only arming the mode.
  /// Without it, a pick with no value set was refused outright and the ribbon button was a dead
  /// end — the command could only be reached by typing DE/P/T *and* a number first.
  bool lengthenPendingApplyOnValue = false;

  // --- EXTEND (REQ-103 step 3) ---
  enum class ExtendPhase : std::uint8_t {
    SelectBoundaries, ///< picking boundary edges; Enter (needs >=1) advances to SelectTargets
    SelectTargets,    ///< picking objects to extend, one per click, loops until Enter/Esc
  } extendPhase = ExtendPhase::SelectBoundaries;
  /// Boundary edges picked so far — read as a selection while being picked (TRIM's own precedent
  /// for `trimCutters`, copy-adapted rather than shared).
  std::vector<SelectedEntity> extendBoundaries;

  // --- BREAK (REQ-103 step 4) --- (BreakPoint is file-scope; see its definition above)
  enum class BreakPhase : std::uint8_t {
    SelectFirstPoint,  ///< pick selects the entity AND supplies break point 1
    SelectSecondPoint, ///< pick supplies break point 2, projected onto the same entity; applies, loops
  } breakPhase = BreakPhase::SelectFirstPoint;
  SelectedEntity breakEntity{};
  BreakPoint breakP1{};

  // --- STRETCH (REQ-103 step 5) ---
  /// Crossing/window box captured when the box-select that started this STRETCH invocation closed
  /// (world XY, plain — not camera-projected; see REQ-103's STRETCH acceptance simplification
  /// note). Read at apply time to decide which of each selected entity's definition points move.
  /// STRETCH always runs its own fresh box-select (never consumes a pre-existing \ref selection),
  /// so this is always populated together with the selection it describes.
  float stretchRectMnX = 0.f, stretchRectMxX = 0.f, stretchRectMnY = 0.f, stretchRectMxY = 0.f;
  /// REQ-329 increment 4: true when the rect above is in the active UCS plane's local 2D frame
  /// (a tilted work plane) rather than world XY. `ApplyStretchToSelection` projects each candidate
  /// vertex through `ucs::WorldToPlane` before the box test when this is set.
  bool stretchRectInUcsPlane = false;

  // --- FILLET (REQ-103 step 6a) ---
  enum class FilletPhase : std::uint8_t {
    WaitFirstEntity,  ///< pick the first curve (or type R/T to set radius/trim mode)
    WaitSecondEntity, ///< pick the second curve; applies immediately, loops back to WaitFirstEntity
  } filletPhase = FilletPhase::WaitFirstEntity;
  /// The first curve picked, latched until the second pick completes the fillet. `polySeg` is the
  /// 0-based Polyline EDGE the pick landed on (-1 for Line/Arc, where the whole entity already IS
  /// the curve) — SelectedEntity itself carries no such sub-index (confirmed by research: nothing
  /// in this codebase addresses one segment of a multi-vertex polyline today; the closest analogues
  /// are BREAK's own BreakPoint::segIndex and TRIM's TrimTargetEdge, neither shared).
  SelectedEntity filletFirstEntity{};
  int filletFirstPolySeg = -1;
  float filletFirstPickX = 0.f, filletFirstPickY = 0.f;
  /// issue #373 follow-up: the first pick's own camera ray (default-constructed/invalid in plan
  /// view or paper space, matching `ray3d::Ray::valid()`). The 3D fillet solve's candidate-side
  /// disambiguation needs the click's TRUE off-line position — projecting a pick exactly onto the
  /// curve it is nearest to (the only option without a ray) throws away the one signal that tells
  /// two mathematically valid tangent arcs apart, tying the choice to iteration order instead of
  /// where the user actually clicked (a real report: a small-radius fillet rounding the OUTSIDE of
  /// a corner instead of the inside).
  ray3d::Ray filletFirstPickRay{};
  /// Persisted app-level (gosurvey-user.json), like `trimState` — NOT per-drawing (D-2026-08-24-g:
  /// a generalized "system variable registry" was considered and explicitly declined here; see that
  /// decision's rationale for why). Default 0.5, matching AutoCAD's own FILLETRAD default.
  float filletRadius = 0.5f;
  /// Shared with CHAMFER (AutoCAD's own shared TRIMMODE variable — one toggle governs both
  /// commands, not two). 1 = Trim (default), 0 = No trim. Also persisted app-level.
  int cornerTrimMode = 1;
  /// Transient (not persisted): true while `HandleFilletText`/`HandleChamferText` is waiting for
  /// the NUMBER that follows a typed `R`/`T` sub-command, at `WaitFirstEntity` only.
  bool filletTextAwaitingRadius = false;
  bool filletTextAwaitingTrim = false;
  /// True while a solid-edge FILLET is asking for its radius (REQ-323; the prompted form).
  ///
  /// Its own flag rather than \ref filletTextAwaitingRadius: that one SETS the stored radius and
  /// returns to "select first object", which is the 2D command's loop. This one is the whole
  /// command — the edges are already chosen, so the radius is the last thing needed and answering it
  /// applies the fillet.
  ///
  /// It exists because the argument-only form was a dead end: a bare `FILLET` printed usage and
  /// entered no command state, so the next keystroke went to the IDLE command line, where `R` — the
  /// 2D fillet's Radius option, and the natural thing to press — matched the `RECT` command instead
  /// (user report with screenshot, 2026-09-08).
  bool filletSolidAwaitingRadius = false;

  // --- CHAMFER (REQ-103 step 6b) ---
  enum class ChamferPhase : std::uint8_t { WaitFirstEntity, WaitSecondEntity } chamferPhase = ChamferPhase::WaitFirstEntity;
  /// Same shape as `filletFirstEntity`/`filletFirstPolySeg` — see that field's comment.
  SelectedEntity chamferFirstEntity{};
  int chamferFirstPolySeg = -1;
  float chamferFirstPickX = 0.f, chamferFirstPickY = 0.f;
  /// Persisted app-level (gosurvey-user.json), like `filletRadius`. Default 0.5 each, matching
  /// AutoCAD's own CHAMFERA/CHAMFERB defaults. `chamferDist1` doubles as the single Distance/Angle-
  /// mode distance (AutoCAD's own CHAMFERC).
  float chamferDist1 = 0.5f;
  float chamferDist2 = 0.5f;
  /// Persisted, degrees — AutoCAD's own CHAMFERD, stored here in degrees (not radians) since it is
  /// only ever read/written as a typed value, matching every other user-facing angle in this
  /// codebase's command layer.
  float chamferAngle = 45.f;
  /// 0 = Distance/Distance (default), 1 = Distance/Angle. Persisted.
  int chamferMode = 0;
  /// Transient (not persisted): true while `HandleChamferText` is waiting for the NUMBER that
  /// follows a typed `D`/`A` sub-command, at `WaitFirstEntity` only. `chamferTextAwaitingSecondDist`
  /// distinguishes Distance/Distance's two prompts (first distance, then second).
  bool chamferTextAwaitingFirstValue = false;
  bool chamferTextAwaitingSecondDist = false;
  bool chamferTextAwaitingAngle = false;
  bool chamferTextAwaitingTrim = false;
  /// Transient: true while a bare `CHAMFER` with solid edges selected is waiting for the distance
  /// that applies the bevel (REQ-331). The exact twin of `filletSolidAwaitingRadius`, and it exists
  /// for the reason that field records: an argument-only form is a dead end, because the command
  /// enters no state and the next keystroke reaches the IDLE command line.
  ///
  /// The remembered value is `chamferDist1`, reused rather than duplicated — the way `filletRadius`
  /// serves both the 2D and the solid fillet. REQ-331's increment 1 is a single SYMMETRIC distance,
  /// so `chamferDist2` and `chamferMode` have no meaning here; the two-distance form is deferred
  /// (D-2026-09-08-f item 13), and when it arrives this is where it picks them up.
  bool chamferSolidAwaitingDistance = false;
