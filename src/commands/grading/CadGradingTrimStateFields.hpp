  enum class CirclePhase : std::uint8_t {
    WaitCenterOrMode, ///< Pick center, or type 3P for three-point circle
    WaitRadius,       ///< Center set: radius click, number, or D + diameter
    ThreeP_WaitP1,
    ThreeP_WaitP2,
    ThreeP_WaitP3,
  } circlePhase = CirclePhase::WaitCenterOrMode;

  float circleCx = 0.f;
  float circleCy = 0.f;

  float c3p1x = 0.f, c3p1y = 0.f;
  float c3p2x = 0.f, c3p2y = 0.f;
  /// Each draft pick keeps the work-plane elevation it was made at (REQ-312).
  ///
  /// A tilted work plane makes Z vary from point to point, and a VERTICAL one makes (x, y) stop
  /// determining Z at all -- two picks on a wall can share an (x, y) and differ only in height. So
  /// the elevation cannot be recovered at commit time from the coordinates; it has to be kept with
  /// the pick that produced it. Under the WCS every one of these is the single commit elevation and
  /// nothing reads them.
  float circleCz = 0.f;
  float c3p1z = 0.f, c3p2z = 0.f;

  /// GRADING (REQ-398): pick the baseline feature line, then sit on an options prompt offering the
  /// cut slope, the fill slope, the target surface and — for an open baseline only — which side to
  /// grade. Enter grades. Every value is remembered between runs in a drawing, because a site is
  /// normally graded to one surface at one pair of slopes and only the baseline changes.
  enum class GradingPhase : std::uint8_t {
    WaitBaseline,
    WaitOptions,
    WaitCutSlope,
    WaitFillSlope,
    WaitSurfaceName,
    WaitSide,
    WaitTargetKind,
    WaitTargetValue,
    WaitDistanceGrade,
  } gradingPhase = GradingPhase::WaitBaseline;
  /// The baseline, held by **stable entity id** and never by array index — REQ-076's own words,
  /// which say "never an array index, so an edit that compacts the store cannot silently retarget
  /// the reference". Resolved to an index only at the moment it is used.
  std::uint64_t gradingBaselineId = 0;
  double gradingCutRun = 2.0;      ///< run of run:rise where the design sits BELOW ground
  double gradingFillRun = 3.0;     ///< ...and ABOVE it. Separate values, per D-2026-10-05-b.
  std::string gradingSurface;      ///< target surface name, remembered between runs
  bool gradingSideLeft = true;     ///< open baselines only; meaningless for a closed one
  bool gradingSideChosen = false;  ///< an open baseline must be told, so this starts false
  CadGradingDefinition::Target gradingTarget = CadGradingDefinition::Target::Surface;
  double gradingTargetValue = 0.0;
  double gradingDistanceGradePercent = 0.0;
  /// Issue #997: the stable id of the grading result being EDITED, or 0 when GRADING is creating a
  /// new one. Set when GRADING starts on a selected result; Enter then rebuilds that line in place.
  std::uint64_t gradingEditId = 0;

  /// FEATURELINE command draft — XYZ vertices, and the elevation-point flag for each.
  std::vector<float> featureLineDraftVerts;
  std::vector<uint8_t> featureLineDraftElevPt;
  /// The name typed when FEATURELINE started, stamped on the line at commit.
  std::string featureLineDraftName;

  /// FEATURELINE two-phase vertex entry (TASK-082). A click supplies X and Y only, so the point is
  /// held here while the command prompts for its elevation — the difference between this and
  /// POLYLINE/3DPOLY, which take Z from the work plane without asking.
  ///
  /// A point that already carries an elevation (typed `X,Y,Z`) never lands here; it commits
  /// straight away, because prompting would be asking a question already answered.
  bool featureLinePendingPoint = false;
  float featureLinePendingX = 0.f;
  float featureLinePendingY = 0.f;
  /// What a bare Enter accepts: the snapped point's Z if an object snap was active, else the
  /// previous vertex's elevation, else the work plane. See TASK-082 ASSUMPTION-1.
  float featureLinePendingDefaultZ = 0.f;
  /// Armed by a bare `E` at the FEATURELINE prompt, consumed by the next point. Before clicking
  /// worked, `E` had to be followed by coordinates on the same line; with a click supplying them,
  /// the flag has to survive until the click arrives.
  bool featureLineNextIsElevPoint = false;

  /// POLYLINE command draft — XYZ vertices (two or more before commit).
  std::vector<float> polylineDraftVerts;
  /// REQ-316 / ADR-047: per-draft-vertex bulge, parallel to polylineDraftVerts (one per vertex;
  /// the bulge of the segment LEAVING that vertex). Same length as the vertex count.
  std::vector<float> polylineDraftBulge;
  /// REQ-316: while true, the next POLYLINE segment is a circular arc (keyword `Arc`/`A`; `Line`/`L`
  /// switches back). The arc is tangent to the previous segment unless a radius or included angle
  /// is given for the next pick.
  bool polylineArcMode = false;
  float polylineArcRadius = 0.f;      ///< REQ-316: radius for the next arc segment (0 = unset)
  float polylineArcAngleDeg = 0.f;    ///< REQ-316: included angle (deg) for the next arc segment
  bool polylineArcAngleValid = false; ///< REQ-316: an included angle was typed for the next pick
  /// TRIM has two modes, chosen by the \c TRIMSTATE system variable (REQ-056):
  ///   0 (default) — smart trim: two clicks draw a line across the pieces to remove, no edges to pick;
  ///   1           — classic: pick cutting edges, Enter, then click the pieces to trim.
  /// Within a run, \p T switches to picking cutting edges and \p L back to the drawn line, so either mode
  /// is reachable whatever TRIMSTATE says.
  enum class TrimPhase : std::uint8_t {
    SelectCuttingEdges,
    CuttingLine_WaitP1,
    CuttingLine_WaitP2,
    SelectTrimTargets,
  } trimPhase = TrimPhase::SelectCuttingEdges;
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
