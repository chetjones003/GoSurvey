  /// GRADING (REQ-398): remembered between runs in a drawing (per-invocation phase/ids live in
  /// stepengine::GradingSession; FEATURELINE draft lives in stepengine::FeatureLineSession).
  double gradingCutRun = 2.0;      ///< run of run:rise where the design sits BELOW ground
  double gradingFillRun = 3.0;     ///< ...and ABOVE it. Separate values, per D-2026-10-05-b.
  std::string gradingSurface;      ///< target surface name, remembered between runs
  bool gradingSideLeft = true;     ///< open baselines only; meaningless for a closed one
  bool gradingSideChosen = false;  ///< an open baseline must be told, so this starts false
  CadGradingDefinition::Target gradingTarget = CadGradingDefinition::Target::Surface;
  double gradingTargetValue = 0.0;
  double gradingDistanceGradePercent = 0.0;

  /// \c TRIMSTATE: 0 = smart line trim (default), 1 = pick cutting edges first. Persisted in user prefs.
  int trimState = 0;
  /// REQ-302: which top-level ribbon tab is showing. Persisted in user prefs, same shape as
  /// \c trimState. Values match \c kRibbonTabHome.. \c kRibbonTabModeling above; an out-of-range value
  /// loaded from a hand-edited prefs file is clamped back into range rather than left invalid.
  /// REQ-143 / REQ-153 may set this to a contextual tab for the session only.
  int activeRibbonTab = 0;
  /// Permanent tab to restore when the last selected surface is cleared (REQ-143). Session-only.
  int ribbonTabBeforeSurfaceCtx = 0;
  /// True while a surface selection has already switched (or could switch) to the contextual tab.
  bool surfaceContextualRibbonArmed = false;
  /// Permanent tab to restore when the last selected survey point is cleared (REQ-153). Session-only.
  int ribbonTabBeforeSurveyPointCtx = 0;
  /// True while a survey-point selection has armed the SURVEY Point(s) contextual tab.
  bool surveyPointContextualRibbonArmed = false;
  /// Permanent tab to restore when BEDIT closes. Session-only.
  int ribbonTabBeforeBlockEditor = 0;
  bool blockEditorContextualRibbonArmed = false;
  bool blockAuthoringPaletteOpen = false;
  /// Connection Modes window (issue #496 follow-up): a graphical alternative to the BCONNECTMODE
  /// text wizard, for the same "which mode applies to which snap target" authoring.
  bool showConnectionModesWindow = false;
  int blockAuthoringPaletteTab = 0;  ///< 0 Parameters, 1 Actions, 2 Parameter Sets, 3 Constraints
  /// REQ-350 — the Pipe Fittings palette. Session state beside `blockAuthoringPaletteOpen` and for
  /// the same reasons: the window's POSITION persists in its settings, and which
  /// palette happened to be open is not worth a key in the settings file. Opened automatically by
  /// PIPERUN and by the PIPEPALETTE command; stays open when the run finishes (REQ-350 (a)).
  bool pipeFittingPaletteOpen = false;
  int pipeFittingPaletteTab = 0;  ///< a `CadPipePaletteCategory`: 0 Fittings, 1 Flanges, 2 Valves, 3 Nozzles, 4 Other
  /// The size the palette last filtered on, so it can notice the routed size changing and refresh
  /// without being reopened (REQ-350's live re-filter acceptance condition).
  std::string pipeFittingPaletteShownSize;
  /// REQ-350 (e) / ADR-062 — thumbnail plumbing. The palette RECORDS which parts it wants a picture
  /// of while it draws; the frame's render pass services them afterwards, which is the one point in a
  /// frame where binding another framebuffer is safe (the same constraint `ServicePendingThumbnail`
  /// has, for the same reason).
  std::vector<std::string> pipeFittingThumbRequests;
  /// Parts that produced no thumbnail — no solid or 2D geometry to draw, no GL object available, or
  /// not imported into this drawing yet. Remembered so such a row is asked about ONCE rather than
  /// re-attempted every frame; cleared whenever the routed size changes.
  std::vector<std::string> pipeFittingThumbUnavailable;
  /// The fittings library as the palette last read it, and whether that reading still stands.
  ///
  /// Cached because reading it means walking three directories and parsing a JSON sidecar per part —
  /// real file I/O, which architecture invariant §11.7 does not allow on a per-frame path without a
  /// profile to justify it. The INSERT dialog rescans every frame, but it is a modal the user closes
  /// in seconds; this palette stays open for a whole routing session. Invalidated when the palette
  /// opens, when the routed size changes, when a part is imported, and by the Refresh button — which
  /// is also the answer for a file copied into the library folder from outside the application.
  std::vector<CadBlockLibraryEntry> pipeFittingLibraryCache;
  bool pipeFittingLibraryCacheValid = false;
  /// Parts whose cached thumbnail is STALE and must be re-rendered — a definition edited in BEDIT is
  /// the one way a part's geometry changes within a session (ADR-062 (c)). The command layer appends a
  /// name here rather than calling the renderer, which it must not do: Commands sits BELOW Renderer,
  /// and the frame's service pass drains this list on the UI side.
  std::vector<std::string> pipeFittingThumbStale;
