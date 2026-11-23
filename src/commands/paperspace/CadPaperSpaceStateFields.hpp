  // -------------------------------------------------------------------------
  // PAPER SPACE (REQ-025/026/031) — active drawing's layouts; mirrored per tab in DrawingDocument.
  int activeSpaceIndex = kModelSpaceIndex;   ///< -1 = model space; else index into paperLayouts.
  int lastPaperLayoutIndex = 0;              ///< layout the MODEL/PAPER toggle returns to.
  int selectedViewportLayout = -1;           ///< layout owning the primary selected viewport (REQ-027), or -1.
  int selectedViewportIndex = -1;            ///< primary selected viewport (popup edit/grips), or -1.
  std::vector<int> selectedViewports;        ///< all selected viewports in the active layout (REQ-035).
  // Rectangular viewport command draft (REQ-033): 0 = need first corner, 1 = need second.
  int   paperVpPhase = 0;
  float paperVpFirstXIn = 0.f;
  float paperVpFirstYIn = 0.f;
  int   paperVpMode = 0;  ///< REQ-490: 0 = rectangle, 1 = awaiting closed polyline Object pick.
  // Paper-space grip edit (REQ-035): grabbed grip on selectedViewportIndex (click-grab, move, click-commit).
  int   paperGripCorner = -2;                ///< -2 none, -1 move (whole viewport), 0..3 resize corner.
  // Paper-space MOVE/COPY of selected viewports (REQ-035): 0 idle, 1 need base, 2 need destination.
  int   paperMovePhase = 0;
  bool  paperMoveIsCopy = false;
  float paperMoveBaseXIn = 0.f;
  float paperMoveBaseYIn = 0.f;
  // REQ-307 (GitHub #106): paper-space MOVE/COPY/DELETE are pick-first by default (act on whatever
  // is already selected), but starting one with NOTHING selected now opens a real selection step
  // instead of refusing — the paper-space counterpart of REQ-121's model-space treatment. Separate
  // bools rather than folding into \c paperMovePhase: that field's 1/2 values (base/destination) are
  // consulted by several `!= 0` checks meaning "a MOVE/COPY gesture is in progress", and a selecting
  // step is not that gesture yet (raw cursor, no snapped base point).
  bool  paperMoveWaitingSelection = false;
  bool  paperDeleteWaitingSelection = false;
  // Paper-space window selection box (REQ-035).
  bool  paperSelBoxActive = false;
  float paperSelBoxX0In = 0.f;
  float paperSelBoxY0In = 0.f;
  // Paper-space native geometry selection + edit (REQ-037, ADR-009). Indices into the ACTIVE layout's
  // paperLines (line index = segment, i.e. flat offset/6) and paperTexts. Coexists with viewport selection.
  std::vector<PaperEntityRef> selectedPaperEntities;
  // Paper-space ROTATE of selected paper entities: 0 idle, 1 need base point, 2 need rotation angle.
  int   paperRotatePhase = 0;
  float paperRotateBaseXIn = 0.f;
  float paperRotateBaseYIn = 0.f;
  // Paper-space MIRROR of selected paper entities (REQ-103): 0 idle, 1 need first mirror-line point,
  // 2 need second point (commits immediately — no erase-source prompt in paper space; see
  // MirrorSelectedPaperEntities's comment).
  // Paper-space SCALE of selected paper entities (#764): 0 idle, 1 need base point, 2 need scale factor
  // (a typed number, or a point whose distance from the base in paper inches is the factor).
  int   paperScalePhase = 0;
  float paperScaleBaseXIn = 0.f;
  float paperScaleBaseYIn = 0.f;
  int   paperMirrorPhase = 0;
  float paperMirrorP1XIn = 0.f;
  float paperMirrorP1YIn = 0.f;
  // Paper-space LENGTHEN of native paper entities (REQ-103): 0 idle, 1 waiting to pick the next
  // object. No mode/value prompt here — the pure-paper click flow has no text-entry surface (same
  // limitation MIRROR's paper path documents), so paper-space LENGTHEN reuses whatever
  // lengthenMode/lengthenDeltaValue/etc. was last configured through the model-space command; a
  // pick before any value has ever been set is refused with a message rather than applying 0.
  int   paperLengthenPhase = 0;
  // Paper-space EXTEND of native paper entities (REQ-103 step 3, TASK-096): 0 idle, 1 collecting
  // boundary edges, 2 picking targets. Unlike MIRROR/LENGTHEN's paper paths, EXTEND needs no typed
  // value at all — only two rounds of clicking — so it is NOT simplified away; the Enter key
  // (checked alongside the existing Escape handling in the same paper-click block) advances phase
  // 1->2, the same "done picking edges" signal model-space TRIM/EXTEND get from a real Enter.
  int   paperExtendPhase = 0;
  std::vector<PaperEntityRef> paperExtendBoundaries;
  // Paper-space BREAK of native paper entities (REQ-103 step 4): 0 idle, 1 waiting for the
  // entity+first-point pick, 2 waiting for the second point (applies, loops back to 1). Pure click
  // flow like paper EXTEND — no typed value, so not simplified away.
  int   paperBreakPhase = 0;
  PaperEntityRef paperBreakEntity{};
  BreakPoint paperBreakP1{};
  // Paper-space STRETCH of native paper entities (REQ-103 step 5, TASK-098): 0 idle, 1 need base
  // point, 2 need destination. Unlike MIRROR/LENGTHEN/EXTEND/BREAK, STRETCH does NOT drive its own
  // box-select — paper box-select is ambient (any plain click starts one, `CadUi.cpp`'s
  // `closePaperSelBox`, regardless of active command) and ROTATE/SCALE already consume whatever is
  // pre-selected, so STRETCH follows that same "pre-select, then invoke" convention: it requires
  // \ref selectedPaperEntities non-empty at start, same as paper ROTATE/SCALE.
  int   paperStretchPhase = 0;
  float paperStretchBaseXIn = 0.f;
  float paperStretchBaseYIn = 0.f;
  /// True iff \ref selectedPaperEntities was populated by a box-select (not a plain click) and the
  /// rect below is that box, in paper inches. Set at the end of `closePaperSelBox` regardless of
  /// which command (if any) is active; cleared by `ClearPaperEntitySelection`/
  /// `TogglePaperEntitySelection` (the shared funnel every non-box selection change goes through),
  /// so a plain click-select never leaves a stale rect behind. When false, paper STRETCH degrades to
  /// a whole-entity translate of the current selection — matching AutoCAD's own degradation for a
  /// non-crossing pickfirst set.
  bool  paperSelBoxLastValid = false;
  // Paper-space FILLET of native paper entities (REQ-103 step 6a): 0 idle, 1 waiting for the first
  // curve pick, 2 waiting for the second (applies, loops back to 1). Pure click flow like paper
  // EXTEND/BREAK — no typed value at the pick itself; radius/trim mode are set through the
  // model-space command line, the same way paper LENGTHEN reuses model-set values.
  int   paperFilletPhase = 0;
  PaperEntityRef paperFilletFirstEntity{};
  int   paperFilletFirstPolySeg = -1;
  float paperFilletFirstPickX = 0.f;
  float paperFilletFirstPickY = 0.f;
  bool  paperFilletAwaitingRadius = false;
  bool  paperFilletAwaitingTrim = false;
  // Paper-space CHAMFER of native paper entities (REQ-103 step 6b): same shape as paper FILLET
  // above.
  int   paperChamferPhase = 0;
  PaperEntityRef paperChamferFirstEntity{};
  int   paperChamferFirstPolySeg = -1;
  float paperChamferFirstPickX = 0.f;
  float paperChamferFirstPickY = 0.f;
  bool  paperChamferAwaitingFirstValue = false;
  bool  paperChamferAwaitingSecondDist = false;
  bool  paperChamferAwaitingAngle = false;
  bool  paperChamferAwaitingTrim = false;
  float paperSelBoxLastMnXIn = 0.f, paperSelBoxLastMxXIn = 0.f;
  float paperSelBoxLastMnYIn = 0.f, paperSelBoxLastMxYIn = 0.f;
  // Floating model space (REQ-036): edit the model IN PLACE through a viewport. The active space stays
  // the paper layout (sheet + viewports stay visible); model edit/snap/draw is routed through the viewport.
  int    floatingViewportLayout = -1;   ///< paper layout of the floating viewport, or -1 if not floating.
  int    floatingViewportIndex = -1;    ///< viewport being edited in place, or -1.
  /// REQ-155 (issue #155): while floating model space is entered, \ref activeUcs holds that
  /// VIEWPORT's active UCS (so coordinate entry / grid / ORTHO / readout / UCSFOLLOW resolve
  /// against the viewport's frame). The drawing-scoped UCS is parked here for the duration and
  /// restored on exit. \ref CadDrawingScopedUcs reads the right one; persistence (`internal document payload` save,
  /// per-tab snapshot) must go through it so a save/tab-switch WHILE floating records the
  /// drawing's frame, not the viewport's. Session-only — never written to `internal document payload`.
  ucs::Ucs drawingActiveUcsStash;
  bool     floatingUcsSwapActive = false;
  /// Viewport zoom lock (user request): when ON, pan/zoom always targets the sheet; when OFF and editing a
  /// viewport in place, pan/zoom adjusts that viewport's model framing (scale/center).
  bool   viewportZoomLocked = false;
  // Saved model-space view so switching Model<->Paper keeps each space's own pan/zoom (each layout saves
  // its own in PaperLayout::view*). Fixes the new layout opening on the model's zoomed-out view.
  double modelViewPanX = 0.0;
  double modelViewPanY = 0.0;
  float  modelViewZoom = 1.f;
  bool   modelViewSaved = false;

  // Page setups + layout-tab dialogs (right-click menu → Rename / Move-Copy / Page Setup Manager / Delete).
  // savedPageSetups (drawing-wide named page setups; "Standard" ensured) is drawing content.
  bool showPageSetupManager = false;
  bool showNewPageSetup     = false;
  bool showPageSetupEditor  = false;             ///< the big "Modify" page-setup editor.
  bool showMoveCopyLayout   = false;
  int  pageSetupLayoutIdx   = -1;                ///< layout the dialogs target.
  int  pageSetupManagerSel  = -1;                ///< Page Setup Manager selection: -1 = layout's current, >=0 saved idx.
  bool pageSetupDisplayOnNew = false;            ///< "Display when creating a new layout".
  int  pageSetupEditorTarget = -1;               ///< editor edits: -1 = layout's current, >=0 = saved setup idx.
  PageSetup pageSetupEditorDraft;                ///< working copy while the editor is open.
  char newPageSetupName[64] = "Setup1";
  int  newPageSetupStartWith = 3;                ///< index into the New "Start with" list.
  int  moveCopyBeforeSel    = 0;                 ///< Move-or-Copy "Before layout" selection (== count → move to end).
  bool moveCopyCreateCopy   = false;
  int  layoutRenameIdx      = -1;                ///< layout being renamed inline, or -1.
  char layoutRenameBuf[64]  = "";
  bool showViewportsWindow  = false;             ///< Viewports manager window (moved off the status bar).
  bool showBatchPlotDialog  = false;             ///< Batch-plot dialog (select layouts → multi-page PDF).
  std::vector<int> batchPlotSelected;            ///< layout indices ticked in the batch-plot dialog.

  // -------------------------------------------------------------------------
  // TRAVERSE EDITOR state
  // -------------------------------------------------------------------------
  bool showTraverseEditorWindow = false;
  TraverseData traverseData;
  /// When true, traverseData must be recomputed before the next panel draw.
  bool traverseDataDirty = true;
  /// REQ-403 (#834): last path saved/loaded via the Traverse Editor, "" until one exists. Plain
  /// "Save" writes here again without reprompting; "Save As…" always prompts and updates it.
  std::string traverseFilePath;

  /// Closure-analysis window (unadjusted vs least-squares, REQ-014).
  bool        showTraverseClosureWindow = false;
  LsaWeights  traverseLsaWeights;          ///< Editable a-priori standard errors.
  LsaResult   traverseLsaResult;           ///< Last computed adjustment.
  bool        traverseLsaComputed = false; ///< True once a result has been produced.
  bool        traverseLsaAccepted = false; ///< User accepted the LSA result.

  /// Index of the leg whose per-leg observation editor is expanded (REQ-018),
  /// or -1 when none. Accordion: at most one leg is expanded at a time.
  int         traverseExpandedLeg = -1;

  /// GitHub issue #845 (Phase A) — command-style text view, two-way synced with the
  /// form panel through the same traverseData/traverseLsaWeights.
  bool        traverseCommandViewActive = false;  ///< Form tab vs. Command tab.
  std::string traverseCommandText;                ///< Editable buffer (GSTC syntax).
  /// True once the text has been generated for the currently-active edit session;
  /// while editing, the form side must not regenerate and clobber unapplied text.
  bool        traverseCommandTextLoaded = false;
  std::string traverseCommandError;               ///< Parse error from the last Apply, if any.
  /// LSA Editor: last path opened/saved as a plain-text `.gsdat` command document
  /// (the GSTC source itself — distinct from the structured `.gstrav` save format),
  /// "" until one exists. Plain "Save" writes here again; "Save As…" always prompts.
  std::string traverseGsdatFilePath;
  /// Issue #848 follow-up: one entry per raw-data file imported into the current
  /// traverse, `{legIndex, "IMPORT \"path\""}` — legIndex is the 0-based index (at
  /// import time) of the first leg that file contributed, so the Command tab can
  /// re-insert a `# IMPORT "path"` comment line at the right spot every time it
  /// regenerates the GSTC text from traverseData (travcmd::Parse ignores comments,
  /// so this can't round-trip through the text itself). Cleared on Load/Open/Clear.
  std::vector<std::pair<int, std::string>> traverseImportMarkers;

  // -------------------------------------------------------------------------
  // CLIPBOARD (COPYCLIP / PASTECLIP)
  // -------------------------------------------------------------------------
  CadClipboard clipboard;
