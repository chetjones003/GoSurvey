  // --- Survey / COGO points (the points and their styles are DrawingContent) ---
  // createPointsOpts and createPointsNextId are drawing content (DrawingContent).
  bool showCreatePointsWindow = false;
  /// Status-bar toggle: when on, overlapping picks show a cursor marker and open a pick list on click.
  bool multiSelectionEnabled = false;
  /// Floating Selection panel (checkbox list), opened from the right-click menu — not from the toggle.
  bool showSelectionCyclingWindow = false;
  /// Stable snapshot of the selection taken when the Selection panel is opened; entities remain listed even after deselection.
  std::vector<SelectedEntity> selectionCycleEntities;
  std::vector<int> selectionCycleSurveyPoints;
  /// All CAD entities currently under the idle pick aperture (from the throttled hover scan).
  std::vector<CadPickCandidate> viewportPickCandidates;
  /// True when \ref viewportPickCandidates holds more than one entry.
  bool viewportPickAmbiguous = false;
  /// Cursor-anchored pick list opened by a click on an ambiguous pick (SEL toggle on).
  bool pickDisambiguationPopupOpen = false;
  std::vector<CadPickCandidate> pickDisambiguationCandidates;
  float pickDisambiguationScreenX = 0.f;
  float pickDisambiguationScreenY = 0.f;
  bool pickDisambiguationShiftClick = false;

  /// REQ-075: the description / name the Add Breaklines and Add Boundaries dialogs collected before
  /// the pick, stamped onto the definition item on commit. Empty when the command was typed rather
  /// than started from the panel, which is the ordinary case for the command line.
  std::string designateBreaklineDescription;
  std::string designateBoundaryName;

  /// Elevation peeled off a typed `x,y,z` for the vertex being submitted, consumed by
  /// \ref SubmitPolylineVertex / FEATURELINE and cleared there.
  bool  polylineTypedZValid = false;
  bool  polylineTypedZRelative = false;  ///< `@dx,dy,dz` — dz is relative to the previous vertex.
  float polylineTypedZ = 0.f;

  bool showViewPointsWindow = false;
  bool showSettingsWindow = false;
  bool showQuickSelectWindow = false;

  /// Select Color dialog (ACI palette + true colour) — layer manager, properties, quick select.
  enum class SelectColorTarget : uint8_t {
    None = 0,
    LayerTable,
    VpLayerColor,
    EntitySelection,
    QuickSelectValue,
    RibbonColor,  ///< REQ-356: the ribbon colour combo's "More colors..." (CadRibbonPickColor)
  };
  bool showSelectColorPopup = false;
  std::string selectColorInitial;
  SelectColorTarget selectColorTarget = SelectColorTarget::None;
  bool selectColorAllowByLayer = false;
  bool selectColorAllowByBlock = false;
  size_t selectColorLayerRowIndex = 0;
  std::string selectColorVpLayerName;

  /// Quick Select filter state (QUICKSELECT / QS command).
  enum class QsApplyTo    : uint8_t { EntireDrawing = 0, CurrentSelection = 1 };
  enum class QsObjectType : uint8_t { All=0, Line, Circle, Arc, Ellipse, Polyline, Text, Mtext, DimAligned, DimLinear, DimAngular, SurveyPoint };
  enum class QsProperty   : uint8_t { Layer=0, Color, Length, Radius, Closed, Content, Id, Elevation, Easting, Northing, Description };
  enum class QsOperator   : uint8_t { Equals=0, NotEquals, LessThan, GreaterThan, SelectAll };
  enum class QsInclude    : uint8_t { Include=0, Exclude };
  QsApplyTo    qsApplyTo          = QsApplyTo::EntireDrawing;
  QsObjectType qsObjectType       = QsObjectType::All;
  QsProperty   qsProperty         = QsProperty::Layer;
  QsOperator   qsOperator         = QsOperator::Equals;
  char         qsValueBuf[256]    = {};
  QsInclude    qsIncludeMode      = QsInclude::Include;
  bool         qsAppendToExisting = false;
  /// Layer manager (LAYER / ribbon LAY). Rows are synced with geometry-used names.
  bool showLayerManagerWindow = false;
  /// Text style manager (STYLE / ribbon). Create / rename / delete / edit named text styles (REQ-044).
  bool showTextStyleManagerWindow = false;
  /// Surface Style editor (REQ-070 / ADR-036 (i)) — opened by SURFSTYLE and by the Surface Manager.
  bool showSurfaceStyleWindow = false;
  /// When set, the next Surface Style window draw selects this named row (Surface Manager Edit...).
  std::string surfaceStyleEditorFocusName;
  /// When true, the style/analysis editor uses the title "Surfaces" (Toolspace / Survey ribbon).
  bool surfaceStyleUseSurfacesTitle = false;
  bool showPointGroupManagerWindow = false;  ///< Point Group manager (REQ-067).
  /// When set, the Point Group manager selects this named group on the next draw.
  std::string pointGroupManagerFocusName;
  bool showSurveyPointGridWindow = false;  ///< Survey Point Database grid (REQ-400).
  /// REQ-400 clause 5: a grid edit that retagged a point to a number already in the database, waiting
  /// on the user's overwrite / renumber / cancel answer. Cleared once answered or the window closes.
  struct SurveyPointGridConflict {
    bool        active = false;
    int         oldNumber = 0;    ///< the row being edited (its number before the edit)
    int         newNumber = 0;    ///< the number the user typed, already taken
    SurveyPoint pendingValues;    ///< the rest of the edit, applied once the conflict is resolved
  } surveyPointGridConflict;
  bool showSurfaceManagerWindow = false;     ///< Surfaces panel (REQ-068). definition edits live in Toolspace.
  /// Create Surface dialog (Toolspace Surfaces ▸ Create Surface...). Session-only.
  bool showCreateSurfaceWindow = false;
  /// Surface Properties (Toolspace named surface ▸ Surface Properties...). Session-only.
  bool showSurfacePropertiesWindow = false;
  int surfacePropertiesIndex = -1;
  bool showFeatureLineElevWindow = false;    ///< Feature line elevation editor (REQ-088).
  /// REQ-142 Toolspace (Prospector / Settings). Session-only; not written to `internal document payload`.
  enum class ToolspaceTab : std::uint8_t { Prospector = 0, Settings = 1, Project = 2 };
  bool showToolspaceWindow = true;
  /// View Manager (REQ-106) — the dialog half of "a VIEW command/dialog". Session-only, like the
  /// other manager windows: which panels are open is not a property of the drawing.
  bool showViewManagerWindow = false;
  /// The New View name prompt, raised from the ribbon button. Separate from the manager window so
  /// "save what I am looking at" stays one click rather than opening a dialog to find a button.
  bool showViewManagerNewPrompt = false;
  char newViewNameBuf[96] = {};
  ToolspaceTab toolspaceTab = ToolspaceTab::Prospector;
  /// Which feature line the elevation editor is showing, 0-based. Held here rather than as a static
  /// in the panel so that opening the editor from a selected feature line can aim it, and so it
  /// survives the window being closed and reopened.
  int featureLineElevIndex = 0;
  // currentLayer, currentColor, alignmentStyleCurrent and activeTextStyleName are drawing content.
  DimensionStyle dimStyleDraft = DimensionStyles::Default();
  bool showDimStyleDialog = false;
  MultileaderStyle mleaderStyleDraft = MultileaderStyles::Default();
  bool showMleaderStyleDialog = false;
  bool showTableStyleDialog = false;
  /// Viewport CAD crosshair (Drawing1): RGB 0–1, arm length as fraction of viewport width/height, pickbox half-size in px.
  float viewportCrosshairR = 1.f;
  float viewportCrosshairG = 0.8392157f;
  float viewportCrosshairB = 0.f;
  float viewportCrosshairArmFracX = 0.03f;
  float viewportCrosshairArmFracY = 0.05f;
  float viewportCrosshairPickHalfPxX = 4.f;
  float viewportCrosshairPickHalfPxY = 4.f;
  float viewportCrosshairHairPx = 1.f;
  /// 3D crosshair cursor (REQ-310): draw the active UCS's X/Y/Z axes instead of two screen-aligned
  /// arms, so the cursor shows which way the drawing plane runs under an orbited view or a rotated
  /// UCS. **Off by default** — on, the cursor changes colour and orientation, and a display change
  /// no one asked for is the one thing REQ-064 was careful to avoid when it added visual styles.
  bool viewportCrosshair3d = false;
  /// Live section clipping (REQ-341 / ADR-058, GitHub issue #149 acceptance 6): hide everything on
  /// the far side of a plane so the inside of a model can be looked at, updating as the plane moves.
  ///
  /// **The plane is the active UCS plane**, slid along its own Z by \ref viewportSectionClipOffset —
  /// the same decision `SECTION` made (D-2026-09-09-i) and for the same reason, so the two commands
  /// cut on the same plane and a user can section exactly what they are looking into.
  ///
  /// **Deliberately NOT persisted to `internal document payload`**, unlike \ref viewportProjection which sits beside a
  /// named view. This is an inspection mode, not a property of the drawing: opening a file to find
  /// half of it invisible, with the reason three menus away, is the failure this avoids. REQ-341
  /// records persistence as a possible increment rather than an oversight.
  ///
  /// It IS per tab: `SaveDocumentToSnapshot` / `RestoreDocumentFromSnapshot` carry these three with
  /// the camera, so another open drawing never inherits the clip (D-2026-09-16-b).
  bool viewportSectionClip = false;
  /// Offset of the clip plane from the UCS origin, along the UCS Z, in drawing units.
  double viewportSectionClipOffset = 0.0;
  /// Which half survives. False keeps the half the UCS +Z points AWAY from — so the material in
  /// front of the plane is what disappears, which is the direction that reads as "cut towards me".
  bool viewportSectionClipFlip = false;
  /// **One clip plane, two ways to aim it** (REQ-342 / D-2026-09-11-b, GitHub issue #479).
  ///
  /// When false the plane is the active UCS plane, exactly as REQ-341 shipped it: derived every
  /// frame, so it follows the work plane. When true it is \ref viewportSectionClipFrame, which
  /// `SECTIONPLANE` set from a solid's face and which does NOT follow the UCS.
  ///
  /// Two independent planes were the alternative and were rejected: they can disagree, and the
  /// first thing a user would do is turn one on while the other was already cutting and see a
  /// result neither command explains. \ref viewportSectionClipOffset and
  /// \ref viewportSectionClipFlip apply to whichever frame is in force, so `SECTIONCLIP OFF`,
  /// `FLIP` and a typed offset keep working on a face-defined plane without a second vocabulary.
  bool viewportSectionClipFrameValid = false;
  /// The face-derived clip frame. Its Z is the face's **outward** normal (`brep::Surface::frame`,
  /// measured across every primitive, Boolean and oblique-slice result in probe P1/P2), so at
  /// offset 0 the whole solid is on the kept side and nothing disappears — the plane simply
  /// appears on the face it was made from, which is what AutoCAD does and what the user asked for.
  ucs::Ucs viewportSectionClipFrame{};
  /// The rectangle's size, once the user has stretched it (REQ-343). Invalid means "derive it from
  /// the model", which is what a freshly placed plane uses. Reset whenever the plane is re-aimed,
  /// for the reason the offset is: it was measured against a face that is no longer in force.
  SectionPlaneExtent viewportSectionClipExtent{};
  /// True while the section plane is SELECTED and showing its handles (REQ-343).
  ///
  /// Not a `SelectedEntity`. The plane is still a view state — it has no layer, no attributes and
  /// no place in `internal document payload` — so putting it in `selection` would put a branch for it in every consumer
  /// of that vector (MOVE, DELETE, DXF export, the property panel, the highlight walk), and the
  /// first one that forgot would be a view setting silently exported or erased. That is the same
  /// argument `SelectedSubObject` records for keeping its own store.
  bool sectionPlaneSelected = false;
  /// The handle currently being dragged, or `None`. Cast to \ref SectionPlaneGrip.
  int sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  /// The handle under the cursor, for the pre-highlight. `None` when there is none.
  int sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  /// Where the drag started along its own axis, and the plane's state at that moment — so the drag
  /// is a DELTA from the grab rather than an absolute reading of the cursor, and the handle does
  /// not jump to the cursor on the first frame.
  double sectionPlaneGripStartParam = 0.0;
  double sectionPlaneGripStartOffset = 0.0;
  SectionPlaneExtent sectionPlaneGripStartExtent{};
  /// Whether the plane was ALREADY user-sized when the handle was grabbed (REQ-343).
  ///
  /// \ref sectionPlaneGripStartExtent is seeded from the drawn rectangle when there was no stored
  /// extent, because the stretch arithmetic needs a valid one to work from — so it cannot itself
  /// answer "was this plane user-sized before?". ESC needs that answer: restoring the seeded extent
  /// after an aborted first stretch would leave the plane pinned at that size and no longer
  /// tracking the model, which is a state the user never asked for and cannot see.
  bool sectionPlaneGripStartExtentWasValid = false;
  /// The drag axis, FROZEN at the moment of the grab (REQ-343).
  ///
  /// It has to be frozen, and this is not a refinement. The handle sits on the plane, so dragging
  /// the plane moves the handle — re-deriving the axis each frame measures every frame's delta from
  /// where the plane has already got to, which makes the delta collapse to zero on the second
  /// frame. Held still, the cursor would then snap the plane back to where it was grabbed, and
  /// moving would oscillate. Found by a test asserting that the click which DROPS a drag changes
  /// nothing.
  ray3d::Vec3 sectionPlaneGripAnchor{};
  ray3d::Vec3 sectionPlaneGripAxis{};
  /// Viewport background (model-space clear color): RGB 0–1. Default #141A24 steel-blue tint.
  float viewportBgR = 0.08f;
  float viewportBgG = 0.10f;
  float viewportBgB = 0.14f;

  // ---------------------------------------------------------------------------------------------------------
  // Settings (AutoCAD-style Options dialog). Live tab is preserved across opens/closes via settingsActiveTabIdx.
  // The fields below are persistent UI/render preferences accessed from DrawSettingsPanel and consumed by the
  // renderer + tessellator. Names try to mirror AutoCAD VIEWRES/Options labels so they read naturally.
  // ---------------------------------------------------------------------------------------------------------
  int settingsActiveTabIdx = 1; ///< 0=Files 1=Display 2=Open and Save 3=Plot 4=System 5=User Prefs 6=Drafting 7=3D 8=Selection 9=Profiles 10=AEC

  // Display tab — Display resolution. Wired: arcCircleSmoothness caps CircleTessellationSegmentCount.
  int displayArcCircleSmoothness = 1000;       ///< Max segments per full circle (1..20000). AutoCAD VIEWRES analog.
  int displayPolylineCurveSegments = 8;        ///< Placeholder; current pipeline uses pixel-chord adaptive segments.
  float displayRenderedObjectSmoothness = 0.5f;///< Placeholder (3D-style smoothness factor).
  int displayContourLinesPerSurface = 4;       ///< Placeholder.

  // Display tab — Display performance. Placeholders (UI only).
  bool displayPanZoomWithRaster = false;
  bool displayHighlightRasterFrameOnly = true;
  bool displayApplySolidFill = true;
  bool displayShowTextBoundaryFrameOnly = false;
  bool displayDrawTrueSilhouettes = false;

  // Display tab — Window Elements (placeholders + theme tag).
  int displayColorThemeIdx = 0; ///< 0=Dark (only theme for now).
  bool displayScrollbars = false;
  bool displayLargeToolbarButtons = false;
  bool displayResizeRibbonIcons = true;
  bool displayShowTooltips = true;
  float displayTooltipDelaySec = 1.0f;
  bool displayShowShortcutKeysInTooltips = true;
  bool displayShowExtendedTooltips = true;
  float displayExtendedTooltipDelaySec = 2.0f;
  bool displayShowRolloverTooltips = true;
  bool displayShowFileTabs = true;

  // Display tab — Layout elements (placeholders).
  bool displayLayoutAndModelTabs = true;
  bool displayPrintableArea = true;
  bool displayPaperBackground = true;
  bool displayPaperShadow = true;
  bool displayPageSetupOnNewLayouts = false;
  bool displayCreateViewportInNewLayouts = true;

  // Display tab — Crosshair size (1..100, % of viewport min axis). Mirrors AutoCAD CURSORSIZE.
  int displayCrosshairSizePct = 5;

  // Decimal places shown for all non-survey user-facing coordinate/length
  // readouts (status bar, ID/INVERSE, dimensions, properties). Display only —
  // stored values keep full precision. Owned by the Drawing Units dialog
  // (UNITS command); see REQ-020.
  int displayLinearPrecision = 4;

  // Drawing Units dialog (UNITS command) visibility. REQ-020.
  bool showUnitsWindow = false;

  // Angle DISPLAY settings owned by the Drawing Units dialog (REQ-021, ADR-004).
  // Display only: the stored/compute convention (CW from north) and angle entry
  // are unchanged. Defaults reproduce the pre-feature bearing format.
  int    angleDisplayType = 1;        ///< 0=Decimal Degrees, 1=Deg/Min/Sec, 2=Surveyor's Units
  int    angleDisplayPrecision = 1;   ///< decimals on the smallest unit (deg for DD; sec for DMS/Surveyor)
  bool   angleDisplayClockwise = true;
  double angleDisplayBaseDeg = 0.0;   ///< canonical CW-from-north degrees of the 0° direction (N=0,E=90,S=180,W=270)

  // Display tab — Zoom. Wheel zoom factor per notch (AutoCAD ZOOMFACTOR analog). 1.10 = 10% per notch.
  // Clamped 1.01..3.0 at the call site; higher = faster zoom, lower = finer control.
  float displayWheelZoomFactor = 1.15f;

  // Display tab — Fade control (placeholders).
  int displayFadeXref = 50;
  int displayFadeInPlace = 70;

  // System tab — Hardware acceleration toggle, drives MSAA + line smoothing.
  bool systemHardwareAcceleration = true;
  /// BUG-013: opt back to the integrated GPU on a hybrid laptop, for battery life in the field.
  /// Default false — a CAD application should ask for the capable GPU, and until 2026-08-15 this
  /// one asked for nothing and silently got the other one. Applied by writing Windows' own
  /// per-application preference, so it takes effect at the **next launch**, not this one.
  bool systemPreferIntegratedGpu = false;
  bool systemAutoCheckCertificationUpdate = true;
  bool systemDisplayOLETextSizeDialog = true;
  bool systemBeepOnError = false;
  bool systemAllowLongSymbolNames = true;
  bool systemAccessOnlineContent = true;
  bool systemStoreLinksIndexInDrawing = true;
  bool systemOpenTablesReadOnly = false;
  int systemLayoutRegenOption = 0; ///< 0=Regen on switch, 1=Cache model+last, 2=Cache model+all.

  /// Frame-time diagnostic HUD (issue #166 investigation). Toggled by the `PERFHUD` command. The
  /// millisecond fields are written each frame by whoever owns that section — `perfRenderMs` in the
  /// app frame loop, the rest in the viewport draw — and read only by the overlay.
  /// PIPERUN live-routing profiler (user request 2026-09-24), reported by the `PIPEPERF` command.
  ///
  /// Routing a pipe builds real geometry on every click (D-2026-09-24-d) and the swept tube is the
  /// most expensive thing the command does, so these are the four places that time can go. Reset
  /// automatically when PIPERUN starts, so the report always describes the run just drawn.
  ///
  /// `mutable` because the rubber preview takes the state by const reference — it draws, it does not
  /// edit — and a diagnostic counter is exactly the "does not change observable behaviour" case
  /// `mutable` is for. Nothing reads these except the report.
  struct PipeRunPerfStat {
    double totalMs = 0.0;
    double maxMs = 0.0;
    int calls = 0;
    void Add(double ms) {
      totalMs += ms;
      if (ms > maxMs)
        maxMs = ms;
      ++calls;
    }
    void Reset() { *this = PipeRunPerfStat{}; }
    [[nodiscard]] double AvgMs() const { return calls > 0 ? totalMs / static_cast<double>(calls) : 0.0; }
  };
  struct PipeRunPerf {
    PipeRunPerfStat sweep;      ///< one run's swept tube (CadBuildPipeRunSolids), inside the rebuild
    PipeRunPerfStat rebuild;    ///< the whole RebuildPipeRunWorldSolids pass
    PipeRunPerfStat tessellate; ///< one solid's display tessellation (faces + edges + isolines)
    PipeRunPerfStat ghost;      ///< the routing rubber preview, per frame
    PipeRunPerfStat tessFaces;  ///< brep::Tessellate  (the surface triangles)
    PipeRunPerfStat tessEdges;  ///< brep::TessellateEdges
    PipeRunPerfStat tessIso;    ///< brep::TessellateIsolines
    int runsSwept = 0;          ///< runs re-swept by a rebuild (cache miss)
    int runsReused = 0;         ///< runs served from the per-run solid cache
    int clicks = 0;             ///< vertices added while routing
    int lastRunVerts = 0;       ///< vertices in the run at the last rebuild, for reading the curve
    long long tessTriangles = 0;///< triangles produced by the display tessellation
    void Reset() { *this = PipeRunPerf{}; }
  };
  mutable PipeRunPerf pipeRunPerf;

  bool   perfHudVisible = false;
  double perfFrameMs = 0.0;        ///< whole frame, wall-clock frame-to-frame
  double perfRenderMs = 0.0;       ///< the GL RenderScene call
  /// REQ-363 item 9: online map tiles were drawn last frame, so the viewport shows the attribution.
  /// Runtime only (set by the app's map controller each frame), never saved.
  bool onlineMapDrawing = false;
  double perfHoverPickMs = 0.0;    ///< the viewport entity hover-pick block (issue #166)
  bool   perfHoverPickRan = false; ///< did the hover pick actually run this frame, or reuse cache
  double perfSnapMs = 0.0;         ///< the object-snap FindBest block
  double perfViewportUiMs = 0.0;   ///< the whole DrawDrawingViewport call

  // System → Graphics Performance sub-dialog.
  bool showGraphicsPerformanceDialog = false;
  bool gfxSmoothLineDisplay = true;            ///< Wired: GL_LINE_SMOOTH + MSAA when systemHardwareAcceleration on.
  int gfxVideoMemoryCachingLevel = 5;          ///< Placeholder 1..5.
  bool gfx3dFastShadedMode = true;             ///< Placeholders (no 3D pipeline).
  bool gfx3dAdvancedMaterialEffects = true;
  bool gfx3dFullShadowDisplay = true;
  bool gfx3dPerPixelLighting = true;

  /// Editable ID strings for VIEWPOINTS table rows (synced from point IDs when empty).
  std::vector<std::string> surveyPointIdBuffers;
  bool showImportPointsWindow = false;
  bool showExportPointsWindow = false;
  char surveyImportCsvPath[512]{};
  char surveyExportCsvPath[512]{};
  /// UTF-8 path to optional startup .gst template (Settings → Startup). Empty = use bundled resources/default-template.gst.
  char defaultWorkspaceTemplatePathUtf8[768]{};
  char defaultDrawingTemplatePathUtf8[768]{};
  /// Active UI layout stem (file resources/layouts/<stem>.ini). See View → Layout.
  char activeUiLayoutNameUtf8[64]{"default"};
  bool openSaveLayoutAsPopup = false;
  char saveLayoutAsNameBufUtf8[64]{};
  bool pendingBuiltinDockLayoutReset = false;
  int surveyImportCsvLayoutIdx = 0;
  int surveyExportCsvLayoutIdx = 0;
  bool surveyImportCsvSkipFirstRow = false;
  bool surveyExportCsvWriteHeader = true;
  bool surveyImportPreviewDirty = true;
  std::string surveyImportPreviewText;
  std::string surveyImportPreviewValidation;
  /// REQ-041: a file-level problem (missing/empty/locked/no valid rows) blocks import.
  bool surveyImportFileBlocked = true;
  /// REQ-041: rows that would import vs. rows that would be skipped (parse error / duplicate ID).
  int surveyImportValidRowCount = 0;
  int surveyImportBadRowCount = 0;
  /// REQ-041 rev 3 (BUG-014): an import has run and \ref surveyImportPreviewValidation holds its
  /// outcome, not a validation verdict. Import is blocked because the rows are already in the
  /// drawing — which is a success, so the panel must not colour it as an error. Cleared by
  /// SurveyCsvRefreshImportPreview, i.e. by any change the user makes to path/layout/header.
  bool surveyImportJustImported = false;
  std::vector<std::pair<std::string, std::string>> surveyReportTabs;
  int surveyReportSelectedTab = 0;
  bool surveyReportSelectLatestPending = false;
  /// Viewport-picked survey rows (indices into \ref surveyPoints). Additive clicks; Shift removes.
  std::vector<int> selectedSurveyPointIndices;
  /// COPY placed CAD duplicates; modal collects policy before duplicating selected survey points.
  bool copySurveyDupModalOpen = false;
  bool copySurveyDupModalOpenRequested = false;
  double pendingCopyDx = 0.0;
  double pendingCopyDy = 0.0;
  float pendingCopyDz = 0.f;  // REQ-329 increment 1: COPY under a UCS carries a Z delta to survey points
  SurveyDuplicatePolicy copySurveyDuplicatePolicy = SurveyDuplicatePolicy::Renumber;
  /// DXF import merges its embedded survey points with existing ones. Points whose ID collides are held
  /// here (in WORLD coordinates) until the user resolves them via the conflict modal.
  std::vector<SurveyPoint> pendingDxfConflictPoints;
  bool dxfPointConflictModalOpen = false;
  bool dxfPointConflictModalOpenRequested = false;
  int  dxfPointConflictOffset = 0;
  /// LandXML import (REQ-506) holds point numbers that already exist in the drawing, in WORLD coordinates,
  /// until the Qt editor asks Overwrite or Offset. A headless run leaves them here and logs them.
  std::vector<SurveyPoint> pendingLandXmlConflictPoints;
  /// True while the viewport command palette should mirror the command line (hover latched until idle / mouse away).
  bool viewportCmdPaletteEngaged = false;
  /// True when the viewport command palette is visible — command line defers its InputText to avoid duplicate focus.
  bool viewportDrawingHovered = false;
