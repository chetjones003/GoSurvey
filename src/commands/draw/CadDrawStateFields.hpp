  /// PADSOLID (GitHub #150): remembered surface name (per-invocation phase/id live in
  /// stepengine::PadSolidSession).
  std::string padSolidSurface;

  /// Multiline MTEXT editor over the box (new placement or double-click edit). Not command-line text.
  bool mtextRichEditorOpen = false;
  bool mtextRichEditorPlacement = false;
  /// Editor target (REQ-039 phase 2): when \c mtextRichEditorPaper, \c mtextRichEditorAnnIndex indexes
  /// \c paperLayouts[mtextRichEditorPaperLayout].paperTexts; otherwise it indexes \c cadAnnotations.
  bool mtextRichEditorPaper = false;
  int mtextRichEditorPaperLayout = -1;
  /// Single-line \c Kind::Text edit (no MTEXT rich tags): plain content box.
  bool mtextRichEditorPlain = false;
  int mtextRichEditorAnnIndex = -1;
  std::string mtextRichEditorBuf;
  bool mtextRichEditorFocusRequest = false;
  int mtextRichEditorCursor = 0;
  int mtextRichEditorSelStart = 0;
  int mtextRichEditorSelEnd = 0;
  bool mtextRichEditorTypingAllCaps = false;

  // --- WYSIWYG editor state (ADR-023) ---
  // The caret and selection anchor are **visible character indices** (tags are not characters); the widget
  // publishes the matching raw byte offsets into mtextRichEditorSelStart/End above, so the formatting
  // toolbar keeps working on byte ranges exactly as it did. Plain members, no new type: the ui/ widget
  // must not become a dependency of the commands layer.
  int mtextEditCaret = 0;
  int mtextEditAnchor = 0;            ///< caret == anchor means no selection
  /// The editor owns keyboard focus. Keeping that ownership local prevents one input control from
  /// blocking pointer interaction with the rest of the application. The
  /// widget only takes ActiveID for the duration of a drag-select, exactly as a stock widget does.
  bool mtextEditFocused = false;
  bool mtextEditMouseSelecting = false;
  float mtextEditScrollY = 0.f;       ///< px scrolled when the text outgrows the box's height cap
  std::vector<std::string> mtextEditUndo;  ///< in-editor Ctrl+Z snapshots (drawing undo is separate)
  std::vector<std::string> mtextEditRedo;
  float mtextEditLastHeight = 0.f;    ///< height the wrapped layout used last frame (positions the box)
  /// Fix a word typed with Caps Lock inverted ("hELLO" → "Hello") when it is finished. Persisted.
  bool mtextEditAutocorrectCapsLock = false;
  // Find and Replace (the Options menu). Buffers are small fixed arrays to match the other UI text fields.
  bool mtextFindReplaceOpen = false;
  char mtextFindBuf[128] = {0};
  char mtextReplaceBuf[128] = {0};
  bool mtextFindMatchCase = false;
  std::string mtextFindStatus;        ///< "3 replaced" / "not found", shown under the fields

  // --- MTEXT "Text Formatting" panel chrome (REQ-051) ---
  // Unlike the fields above, this is not per-edit state: it survives closing the editor and is persisted
  // (UserPrefs, the \c cmdBar* pattern of REQ-040), so the panel reopens where the user left it.
  // \ref CloseMtextRichEditorUi deliberately does not touch it.
  bool mtextPanelAnchorValid = false;    ///< false → place near the MTEXT box this frame. Persisted.
  float mtextPanelAnchorX = 0.f;         ///< panel top-LEFT anchor in screen px. Persisted.
  float mtextPanelAnchorY = 0.f;
  bool mtextPanelRulerVisible = true;    ///< column ruler shown above the in-place box. Persisted.
  bool mtextPanelRow2Visible = true;     ///< second toolbar row shown (the expand control). Persisted.
  unsigned int mtextPanelRunColor = 0xFFFFFFu;  ///< last colour picked for the per-selection swatch (RGB).
  /// Panel size measured last frame (the panel auto-sizes to its content). Used to clamp the anchor and to
  /// span the caption before this frame's size is known — session-only, deliberately not persisted, since a
  /// stale size from a different font/DPI would misplace the panel for one frame.
  float mtextPanelMeasuredW = 0.f;
  float mtextPanelMeasuredH = 0.f;
  /// True while the ruler's width marker is being dragged (so the undo snapshot is pushed exactly once).
  bool mtextRulerDragActive = false;

  float anchorX = 0.f;
  float anchorY = 0.f;
  /// Elevation the anchor was committed at (REQ-058). Recorded per-vertex rather than taken from
  /// the work plane at commit time, because a line's two ends can legitimately differ: the anchor
  /// may have snapped to something on the datum while the far end snaps to something elevated.
  float anchorZ = 0.f;
  /// From UI — ortho constrains LINE segment picks / typed ortho distances toward cursor.
  bool orthoMode = false;
  /// POLAR tracking (issue #154, REQ-154). Mutually exclusive with \ref orthoMode, as in AutoCAD:
  /// enabling one clears the other. When on, rubber-band picks snap to the nearest polar ray —
  /// a multiple of \ref polarIncrementDeg, or one of \ref polarExtraAnglesDeg — measured in the
  /// active UCS's XY plane from its +X, so a rotated frame rotates the rays with it.
  bool polarMode = false;
  double polarIncrementDeg = 90.0;
  std::vector<double> polarExtraAnglesDeg;
  /// Last drawing viewport cursor (world), updated each frame for LINE ortho distance entry.
  std::string alignmentCursorReadout;  ///< REQ-501.2: station/offset/bearing under the cursor; UI-only, never saved
  float uiCursorWorldX = 0.f;
  float uiCursorWorldY = 0.f;
  /// Drawing viewport pan/zoom (local coordinates; pan is view center in storage space).
  double viewportPanX = 0.;
  double viewportPanY = 0.;
  /// Camera target elevation (REQ-058). Pan is a 3D point once the view can tilt: dragging up in
  /// an orbited view moves the target along the camera's UP axis, which has a Z component, so a
  /// target constrained to Z = 0 cannot follow the cursor and the pan stops feeling 1:1.
  /// Always 0 in plan view, where the up axis lies in the XY plane.
  double viewportPanZ = 0.;
  float viewportZoom = 1.f;
  /// Camera orientation about the pan point (REQ-058 / ADR-025 (c)). Pan and zoom remain the
  /// single source of truth for WHERE the camera looks and HOW FAR — these two add only the
  /// direction, so a \ref Camera built by \ref CadViewCamera cannot drift from the view.
  /// Defaults are plan view, which reproduces the pre-3D pipeline exactly.
  float viewportAzimuthDeg = 0.f;
  float viewportElevationDeg = 90.f;
  /// Screen roll about the view axis (GitHub #153). Zero for plan view, every ViewCube orientation
  /// and every hand orbit; set only by `PLAN` of a UCS whose Z is tilted off world +Z, so that the
  /// UCS +Y comes out up the screen. Persisted per drawing alongside azimuth/elevation.
  float viewportRollDeg = 0.f;

  /// How the view projects (REQ-309 / D-2026-08-31-g). `Camera` has implemented both projections
  /// since REQ-058; these two fields are what finally *select* between them — before them nothing
  /// in the application ever assigned `Camera::projection`, so perspective was unreachable.
  ///
  /// They sit here, beside azimuth/elevation, because they share exactly that lifetime: per
  /// drawing, saved per tab, persisted in `internal document payload`, restored by a named view. **Orthographic is the
  /// default everywhere**, which is what keeps REQ-058's plan-view parity guarantee intact.
  Camera::Projection viewportProjection = Camera::Projection::Orthographic;
  float viewportFovDeg = kDefaultFovDeg;  ///< Perspective vertical FOV; ignored when orthographic.

  /// ViewCube orientation animation (REQ-059). A face/arrow/home press sets a target and the view
  /// eases to it over \ref kViewAnimSeconds instead of snapping, so the user keeps their bearings —
  /// a hard jump makes it easy to lose track of which way the model turned. Orbiting by hand
  /// cancels any animation in flight so the drag is never fighting an interpolation.
  bool  viewAnimActive = false;
  float viewAnimFromAz = 0.f, viewAnimFromEl = 90.f, viewAnimFromRoll = 0.f;
  float viewAnimToAz = 0.f, viewAnimToEl = 90.f, viewAnimToRoll = 0.f;
  float viewAnimT = 0.f;  ///< 0..1 progress.

  /// The active User Coordinate System (REQ-058 / ADR-025 (e); REQ-154, GitHub #126).
  ///
  /// One frame, replacing the origin/normal/azimuth triple that stood in for it before the UCS
  /// command existed: that shape could express a plane and a spin but not a basis, so nothing could
  /// ask it "which way is UCS +X?". Its XY plane is the work plane a click resolves against, and its
  /// axes are the frame typed coordinates, ORTHO and the grid are interpreted in. The default is the
  /// WCS, under which every pre-UCS behaviour is unchanged.
  ///
  /// **It never moves geometry.** Entities stay in WCS; this only changes how the user's input is
  /// read and how coordinates are reported back.
  ucs::Ucs activeUcs;

  /// `UCS Previous` history, oldest first. Bounded by \ref kUcsPreviousDepth — AutoCAD keeps a
  /// limited stack too, and an unbounded one would grow for the life of the session.
  std::vector<ucs::Ucs> ucsPrevious;

  /// Saved UCSs (\ref DrawingContent::ucsNamed) and views (\ref DrawingContent::namedViews) live in
  /// drawing content. "World" is reserved and is never stored there. \ref activeViewName is which
  /// saved view the camera matches: empty whenever the camera has been moved since a restore — that
  /// is the "Unsaved View" the ribbon shows, and a statement about the CAMERA, not about the drawing.
  std::string activeViewName;

  /// UCSFOLLOW: when set, any change to \ref activeUcs immediately switches the view to a PLAN view
  /// of the new UCS. 0 leaves the camera alone. Per drawing tab, which is as per-viewport as this
  /// application currently gets — there is one model view per tab (see the requirement's note).
  bool ucsFollow = false;

  // UCS and PLAN per-invocation phase and scratch picks live in stepengine::UcsSession and
  // stepengine::PlanSession (src/commands/ucs/CadUcsViewSessions.hpp).
  /// Elevation of the cursor's work-plane intersection, published alongside the existing
  /// \c uiCursorWorldX/Y so readouts and future 3D-aware commands can see it.
  float uiCursorWorldZ = 0.f;

  /// The Z of the point CURRENTLY being resolved, when that point carries its own (REQ-154).
  ///
  /// On a UCS parallel to world XY every point on the work plane shares one elevation, so the UCS
  /// origin's Z describes them all — which is why a single constant sufficed before. **A tilted UCS
  /// breaks that**: the plane's Z varies across it, and a point's elevation is a property of the
  /// point, not of the plane. Rather than teach ~29 geometry-creation sites about the UCS, the two
  /// places a point is actually resolved publish its Z here and \ref CadWorkPlaneElevation reads it.
  ///
  /// Set at point resolution (a viewport click's ray x plane hit, or a typed UCS coordinate mapped
  /// through the frame) and cleared when point entry ends, so a stale value can never leak into a
  /// later command. Under a flat UCS the published value equals the origin's Z, so this channel
  /// changes nothing there.
  bool  resolvedPointZValid = false;
  float resolvedPointZ = 0.f;
  /// The published Z was TYPED (`x,y,z`, REQ-354), not read off the work plane — so the next typed
  /// point without a Z clears it instead of inheriting it.
  bool  resolvedPointZTyped = false;
  /// Model viewport size in pixels, published by the UI each frame. The command layer needs it to
  /// project geometry to screen for box-selection under an orbited camera (REQ-058); it has no
  /// other way to know the viewport's aspect. Zero means "not yet known" — callers fall back to
  /// the plan-space test, which is correct whenever the view is unrotated anyway.
  float uiViewportWidthPx = 0.f;
  float uiViewportHeightPx = 0.f;

  /// Next stable entity id to hand out for this drawing (REQ-076 / ADR-027). Monotonic, persisted
  /// in `internal document payload`, and **never rewound** — in particular it is deliberately NOT part of
  /// \ref DrawingGeometrySnapshot, so draw → undo → draw gives the second entity a *new* id rather
  /// than reusing the undone one's. Reuse is the one thing an identity must not do.
  std::uint64_t nextEntityId = 1;
  /// \ref cadGpuRevision at the last \ref EnsureEntityIds sweep. The sweep early-outs when geometry
  /// has not changed since, which is what makes it safe to call unconditionally every frame — the
  /// common case is one integer compare, not a walk of every entity (architecture §11.7).
  ///
  /// Deliberately **64-bit against a 32-bit revision**, so \ref kEntityIdSweepNever can be a value
  /// the revision cannot reach. A 32-bit sentinel would collide once every 2^32 edits and skip the
  /// sweep for that drawing — rare enough to never be reproduced, and silent when it happened.
  std::uint64_t entityIdSweepRevision = kEntityIdSweepNever;
  /// `CadOffsetEntityPickTolWorld`'s drawing-size floor, remembered for one `cadGpuRevision`. The
  /// robust extents behind it box every entity, inserts included, which is too slow to repeat on
  /// every hover (about 30 ms for 5,000 inserts; issue #1700 phase 4). 64-bit for the same reason as
  /// \ref entityIdSweepRevision; reset with it when the drawing is swapped or cleared.
  mutable std::uint64_t offsetPickTolGeomRevision = kEntityIdSweepNever;
  mutable float offsetPickTolGeom = 1e-3f;
