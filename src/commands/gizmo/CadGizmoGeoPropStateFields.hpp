  // --- The translate gizmo (REQ-060 + GitHub issue #148 acceptance 4, Phase 5 slices 4b and 4c) ---
  //
  // ONE widget, two subjects. With entities selected it shows three handles along the active UCS
  // and commits through `ApplyTranslationToSelection`; with exactly one solid FACE selected it
  // shows one handle along that face's own normal and commits through `CadApplyPushPull`. Which of
  // the two is derived from the selection (\ref CadGizmoModeFor), never stored, because the two
  // selections are already mutually exclusive (D-2026-09-04-a) — a stored mode would be a third
  // thing that can disagree with them.
  //
  // The face mode REPLACED the separate face grip REQ-319 increment 2 first shipped: two handles
  // for one operation is worse than either, and the gizmo brings the arrow, the pre-highlight, the
  // live ghost and the shared skew-line solve the grip's screen-space dot did not have.

  /// Which axis handle the cursor is over right now, or -1. In entity mode 0/1/2 are the active
  /// UCS X, Y and Z; in face mode 0 is the face normal and there is no other.
  ///
  /// Pre-highlight only. It is refreshed by the same hit test the click uses, so the handle that
  /// lights up is the handle that grabs - the rule REQ-318's sub-object hover already follows, and
  /// for the same reason: two code paths that agree by inspection stop agreeing.
  int gizmoHoverAxis = -1;

  /// True between the click that grabs an axis handle and the click that commits or cancels it.
  ///
  /// Click-arm / click-commit rather than press-drag-release, because that is what every other grip
  /// in this viewport already does (\ref mtextGripMoveActive, \ref dimGripMoveActive) - and it is
  /// the only shape a transcript can drive, a headless run having no mouse to hold down.
  bool gizmoDragActive = false;
  int gizmoDragAxis = -1;

  /// Where the gizmo sat when the handle was grabbed, in WCS.
  ///
  /// Captured rather than recomputed per frame, and in face mode that is load-bearing: the anchor
  /// is the face's centroid, and re-deriving it mid-drag would derive it from geometry the drag is
  /// about to change.
  ray3d::Vec3 gizmoAnchor{0.0, 0.0, 0.0};
  /// The axis direction grabbed, in WCS - captured with the anchor, and for the same reason.
  ray3d::Vec3 gizmoAxisDir{1.0, 0.0, 0.0};
  /// Signed position along that axis, measured at the moment of the grab.
  ///
  /// The drag distance is the CHANGE in this, never its absolute value, so the handle does not jump
  /// to the cursor on the first mouse move - and so the anchor's own precision cannot affect the
  /// result: it appears in both terms and cancels.
  double gizmoGrabParam = 0.0;
  /// The live drag's value **in the current operation's own units** (TASK-232):
  ///
  /// | \ref gizmoOp | meaning | "no drag" |
  /// |---|---|---|
  /// | `Translate` | distance along \ref gizmoAxisDir, in drawing units | 0 |
  /// | `Rotate`    | angle about \ref gizmoAxisDir, in radians, CCW-positive by the right-hand rule | 0 |
  /// | `Scale`     | the uniform factor | 1 |
  ///
  /// One field rather than three, because three would be three things that could disagree about
  /// which drag is armed. Note the "no drag" column: the cancel test is per-operation, since a scale
  /// of zero is a collapse rather than a no-op.
  double gizmoDragDistance = 0.0;

  /// What the gizmo does — a user SETTING, not derived from the selection (\ref CadGizmoOp explains
  /// why this one is stored where \ref CadGizmoMode is not). Set by the `GIZMO` command, and for
  /// their own duration by 3DMOVE / 3DROTATE / 3DSCALE, which restore it on exit.
  CadGizmoOp gizmoOp = CadGizmoOp::Translate;

  /// True when the user asked for the gizmo to follow every selection (`GIZMO MOVE | ROTATE |
  /// SCALE`). False — the default — means a selection shows no gizmo, and only 3DMOVE / 3DROTATE /
  /// 3DSCALE summon one (GitHub issue #564 section 3, D-2026-09-28-a). `GIZMO OFF` clears it.
  /// Session state, like \ref gizmoOp.
  bool gizmoPersistent = false;

  // 3DMOVE / 3DROTATE / 3DSCALE, Geolocation, and CHPROP / MATCHPROP / LAYMCUR per-invocation
  // phase and scratch fields live in stepengine::GizmoCmdSession, GeoCmdSession, and PropCmdSession
  // (src/commands/gizmo/CadGizmoGeoPropSessions.hpp).

  // --- Geolocation tab state (REQ-359) ---
  /// REQ-360: what a running \ref Kind::DrawingSettingsPick picks, and its result for the Drawing
  /// Settings window (which reads and clears \c done). Points are one pick; ToNorth / LocalAzimuth
  /// are two (a direction), with the first in GeoCmdSession::firstA/B.
  struct DrawingSettingsPickState {
    enum class Target : std::uint8_t { None, ReferencePoint, RotationPoint, ToNorth, LocalAzimuth } target = Target::None;
    bool   done = false;         ///< A result is waiting for the window.
    double worldX = 0.0;         ///< The picked point (WORLD), for the point targets.
    double worldY = 0.0;
    int    pointNumber = 0;      ///< The survey point picked there, 0 = none.
    double azimuthDeg = 0.0;     ///< The picked direction, clockwise from north, for the angle targets.
  } drawingSettingsPick;
  /// The Position Marker whose label the MTEXT editor is editing (REQ-359 item 3), or -1. When set,
  /// \ref MtextRichEditorTargetAnnotation returns that marker's own label.
  int mtextRichEditorMarkerIndex = -1;
  /// The editor was opened by placing the marker: committing the label joins the placement's undo
  /// step rather than pushing a second one (one UNDO removes the marker it placed).
  bool mtextRichEditorMarkerJustPlaced = false;
  /// The multileader whose label the MTEXT editor is editing (REQ-367), or -1.
  int mtextRichEditorMultileaderIndex = -1;
  bool mtextRichEditorMultileaderJustPlaced = false;
  /// Edit Location asks the Drawing Settings window to show Units and Zone (REQ-359 item 2).
  bool drawingSettingsShowUnitsAndZone = false;
  // GEOCAPTUREAREA progress (mapCapture) lives in stepengine::GeoCmdSession.
  // 3DMOVE / 3DROTATE / 3DSCALE base point (baseValid, base) and saved gizmoOp (opBeforeCmd)
  // live in stepengine::GizmoCmdSession.
  /// The live TRANSLATE drag as a displacement: `gizmoDragDistance` along the axis for an axis
  /// handle, or the in-plane offset for a plane handle (D-2026-09-28-c), where no single distance
  /// says it. The ghost and the commit read this one field so the two cannot disagree.
  ray3d::Vec3 gizmoDragVec{0.0, 0.0, 0.0};
  /// Where a plane-handle grab met the plane — the drag is the hit's offset from here.
  ray3d::Vec3 gizmoGrabPoint{0.0, 0.0, 0.0};

  /// In face mode, WHICH face the armed drag is moving — captured at the grab like the anchor.
  ///
  /// The selection could be cleared or re-picked between arming and committing; the drag applies to
  /// the face the user actually grabbed, not to whatever is selected when they let go.
  bool gizmoDragIsSubObject = false;
  SelectedSubObject gizmoDragSubObject;

  /// Objects hidden by ISOLATEOBJECTS / HIDEOBJECTS, as **stable entity ids** (REQ-084 (d),
  /// ADR-034). Kept SORTED so the per-entity test is a `binary_search`; empty is the overwhelming
  /// case and every gate early-outs on it, so nothing is paid for a drawing with no isolation.
  ///
  /// Ids, not indices: erase compacts the entity arrays (architecture §11.9), so an index would
  /// come to name a different object. **Session state** — deliberately not written to `internal document payload`, so a
  /// drawing always opens showing everything.
  std::vector<std::uint64_t> hiddenEntityIds;

  /// Two-click axis-aligned box (world XY): first corner placed, waiting second.
  bool selBoxWaitingSecond = false;
  float selBoxAnchorX = 0.f;
  float selBoxAnchorY = 0.f;
  /// Elevation of the fence's first corner — the WORK PLANE's Z there, not the datum.
  ///
  /// Carried because a fence is projected to screen under an orbited camera, and a projection needs
  /// all three coordinates. Both the drawn rectangle and the hit test used to project the two drag
  /// corners at Z = 0, which is invisible in plan view (Z does not move a plan projection) and
  /// invisible on the world XY plane at elevation zero — but as soon as the work plane is TILTED by
  /// a UCS, or merely raised by `ELEV`, the corners project to pixels the mouse is nowhere near, and
  /// the box both draws and selects in the wrong place (user report, 2026-09-01).
  float selBoxAnchorZ = 0.f;
  /// Viewport-image XY (Drawing1 content coords) at fence first corner — compares with second-click mx for
  /// window vs crossing mode.
  float selBoxAnchorScreenX = 0.f;
  float selBoxAnchorScreenY = 0.f;
  /// REQ-370: what releasing the box now would select (live preview). Filled by
  /// `UpdateSelectionBoxPreview`; `selBoxPreviewKey` is the input set it was computed from, so an
  /// idle cursor costs one comparison per frame instead of a whole-drawing hit test.
  std::vector<SelectedEntity> selBoxPreview;
  std::array<double, 11> selBoxPreviewKey{};
  bool selBoxPreviewKeyValid = false;

  /// MTEXT box corner grips (viewport): two-click edit — fixed diagonal corner while resizing box.
  int mtextGripAnnotationIndex = -1;
  /// 0–3 = MTEXT box corners; 4 = survey-linked label center (move whole box).
  int mtextGripCorner = -1;
  float mtextGripFixedCornerX = 0.f;
  float mtextGripFixedCornerY = 0.f;
  /// True after first click on an MTEXT box grip until second click commits (or RMB / ESC cancels).
  bool mtextGripMoveActive = false;
  float mtextGripOrigBoxMinX = 0.f, mtextGripOrigBoxMaxX = 0.f, mtextGripOrigBoxMinY = 0.f,
      mtextGripOrigBoxMaxY = 0.f;
  /// World pick at MTEXT grip mousedown (whole-label drag uses delta from here).
  float mtextGripDownWorldX = 0.f;
  float mtextGripDownWorldY = 0.f;

  /// Aligned dimension grip drag (viewport): which grip on \ref dimGripAnnotationIndex.
  int dimGripAnnotationIndex = -1;
  int dimGripWhich = -1; ///< 0 ext1, 1 ext2, 2 dim foot 1, 3 dim foot 2, 4 text
  double dimGripDownWorldX = 0.0;
  double dimGripDownWorldY = 0.0;
  double dimGripOrigSignedOffset = 0.0;
  double dimGripOrigExt1X = 0.0, dimGripOrigExt1Y = 0.0, dimGripOrigExt2X = 0.0, dimGripOrigExt2Y = 0.0;  // issue #1668
  double dimGripOrigInsX = 0.0, dimGripOrigInsY = 0.f;
  bool dimGripOrigTextUserPlaced = false;  // issue #1676: CancelGripEdit restores auto-placement
  float dimGripDragNx = 0.f, dimGripDragNy = 0.f;
  /// True after first click on a dim grip until second click commits (or RMB cancels).
  bool dimGripMoveActive = false;
  /// Text position vs dimension mid in local (N,T) frame at grip pick — reapplied after ext / dim-line edits.
  float dimGripTextAlongN = 0.f;
  float dimGripTextAlongT = 0.f;
