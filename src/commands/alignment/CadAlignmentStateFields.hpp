  /// REQ-336 — What's New billboard (Help → About opens the same window).
  bool        showWhatsNewWindow = false;
  bool        whatsNewAutoOpenedThisLaunch = false;  ///< suppresses re-auto-open within one launch
  bool        whatsNewDontShowChecked = false;       ///< checkbox state while the window is open
  std::string whatsNewDismissedVersion;              ///< prefs: suppress auto-open for this version
  bool        whatsNewOpeningPending = false;        ///< auto/manual open requested; spinner until modal shows
  bool        whatsNewModalVisible = false;          ///< set each frame while BeginPopupModal is active
  /// Shipped user manual (GitHub wiki mirror). F1 and Help → User Manual open this window.
  bool        showWikiWindow = false;
  std::string wikiCurrentPage = "Home";
  std::string wikiScrollToCommand;  ///< lowercase primary; scroll to matching ## heading once
  std::vector<SelectedEntity> trimCutters;
  /// Draft endpoints while TRIM \p L waits for second point (rubber band). First shot completes trim and clears TRIM.
  float trimCutInfP1x = 0.f, trimCutInfP1y = 0.f, trimCutInfP2x = 0.f, trimCutInfP2y = 0.f;
  /// issue #399 increment 4: the drawn trim-line's two points carry a real elevation when the pick
  /// arrives through a camera ray (orbited view / non-world UCS), so smart TRIM resolves in true 3D
  /// the same way the classic "click the piece to remove" path already does. Left 0 on the plan-view
  /// path, which is byte-for-byte unchanged.
  float trimCutInfP1z = 0.f, trimCutInfP2z = 0.f;
  /// OFFSET: distance (or T for a through point) first, then select object, then side/through pick;
  /// on a successful commit it loops back to WaitSelectEntity with the same distance/mode retained,
  /// until Enter/Esc ends the command (REQ-103's documented TRIM/OFFSET per-target loop pattern).
  enum class OffsetPhase : std::uint8_t {
    WaitDistanceOrThrough,
    WaitSelectEntity,
    WaitSidePick,
    WaitThroughPick,
  } offsetPhase = OffsetPhase::WaitDistanceOrThrough;
  bool offsetEntityValid = false;
  SelectedEntity offsetEntity{};
  /// Typed offset distance (always positive); combined with side pick for sign. Persists across the
  /// select/side loop until OFFSET ends or a new distance is typed.
  float offsetTypedDistance = 0.f;
  /// True once the user has typed T for through-point mode instead of a fixed distance; persists
  /// across the loop the same way offsetTypedDistance does.
  bool offsetThroughMode = false;
  /// While OFFSET waits for the first pick, entity under cursor (for highlight).
  bool offsetHoverHighlightValid = false;
  SelectedEntity offsetHoverEntity{};
  /// Paper layout captured by OFFSET when its paper object is picked; guards the side-pick from
  /// ever resolving the stored index against a different layout after a tab switch.
  int offsetPaperLayoutIndex = -1;
  /// Bumped when CAD geometry or per-entity viewport styling changes; GPU vertex caches invalidate when stale.
  uint32_t cadGpuRevision = 0;
  /// Renderer-only invalidation. Unlike cadGpuRevision, this does not dirty the drawing or cancel surface workers.
  uint32_t cadGpuCacheRevision = 0;

  /// REQ-171 point-cloud display settings (session-global, not per-cloud — CadPointCloud's payload
  /// is immutable, architecture §11.5, so mutable display prefs live here instead, same reasoning as
  /// SurfaceStyle living apart from CadSurface).
  PointCloudDisplaySettings pointCloudDisplay;
  /// REQ-171 (part 14): true while a point cloud is selected and its contextual ribbon tab is armed
  /// (mirrors `surfaceContextualRibbonArmed`).
  bool pointCloudContextualRibbonArmed = false;
  /// The ribbon tab that was active before the point-cloud contextual tab armed, restored on
  /// disarm (mirrors `ribbonTabBeforeSurfaceCtx`).
  int ribbonTabBeforePointCloudCtx = -1;

  /// The per-solid tessellation cache (#120: "do not regenerate a solid's render mesh every
  /// frame"). Rebuilt by \ref RefreshSolidDisplayGeometry only when a solid or the tessellation
  /// quality has actually changed, and — like the surface display cache it is modelled on (ADR-036
  /// (e)) — deliberately **outside** every undo snapshot, because it is derived from the solids.
  std::vector<CadSolidTessellation> solidDisplayCache;
  /// What the renderer is handed for solids this frame: a small number of COALESCED batches, each the
  /// merged geometry of every visible solid sharing a resolved colour and edge lineweight, already
  /// filtered for layer visibility and object isolation. Turns a per-solid draw call (GitHub #194)
  /// into a per-appearance one. Rebuilt only when \ref solidDisplayAssemblySig changes.
  CadSolidDisplayGeometry solidDisplayGeometry;
  /// A fingerprint of everything \ref RefreshSolidDisplayGeometry's assembly pass reads — the visible
  /// solids in order, their cache buffer sizes, their resolved colours and lineweights, and the
  /// regen count. Unchanged means the merged buffers in \ref solidDisplayGeometry are still current
  /// and the (now vertex-copying) concatenation can be skipped — the §11 invariant 7 early-out, so
  /// an orbit that changes only the camera does not re-merge a million triangles every frame.
  std::uint64_t solidDisplayAssemblySig = 0;
  /// How many times \ref RefreshSolidDisplayGeometry has actually (re)tessellated a solid — the solid
  /// twin of \ref surfaceDisplayRegenCount. #120 asks that the render mesh not be regenerated every
  /// frame; `BENCH SOLID` takes a baseline at the first timed frame and this must not grow during a
  /// scripted orbit.
  std::uint64_t solidDisplayRegenCount = 0;
  /// Transformed B-rep solids from model-space block references (issue #475 increment 2). Derived
  /// display data — not snapshotted on undo, like \ref solidDisplayCache.
  std::vector<CadSolidPtr> blockRefWorldSolids;
  std::vector<EntityAttributes> blockRefWorldSolidAttrs;
  std::uint64_t blockRefWorldSolidsSig = 0;

  std::vector<CadSolidPtr> pipeRunWorldSolids;
  std::vector<EntityAttributes> pipeRunWorldSolidAttrs;
  /// Which `cadPipeRuns` index each `pipeRunWorldSolids` entry came from (issue #486, selection
  /// follow-up) — NOT the same as the entry's own position, because a run that fails to build
  /// (unresolvable size, unfillable corner) contributes nothing, so the two arrays can diverge in
  /// length and offset. Selection/highlight/hover map a picked solid back to its owning run through
  /// this, the same reason `CadBlockWorldSolid::blockRefIndex`/`CadBlockWorldConnection::blockRefIndex`
  /// exist for block refs.
  std::vector<int> pipeRunWorldSolidOwnerIndex;
  std::uint64_t pipeRunWorldSolidsSig = 0;

  /// TABLE and ADDPOINTTABLE prompt state (REQ-466, REQ-474).
  int tableStage = 0;
  std::string tablePendingStyle;
  int tablePendingCols = 3;
  int tablePendingRows = 2;
  float tablePendingColumnWidthInches = 1.5f;
  float tablePendingRowHeightInches = 0.375f;
  bool tableInsertDialogRequested = false;
  int pointTableStage = 0;
  std::string pointTableStyle;
  std::string pointTableLayer;
  bool pointTableSplitEnabled = true;
  int pointTableMaxRows = 20;
  int pointTableMaxStack = 3;
  double pointTableOffset = 0.3;
  bool pointTableTileAcross = true;
  bool pointTableDynamic = true;
  std::vector<int> pointTableIds;
  /// Set by the Qt editor. ADDPOINTTABLE then opens the dialog instead of typed prompts.
  bool addPointTableDialogAvailable = false;
  bool addPointTableDialogRequested = false;
  /// REQ-486.1: EDITALIGNMENT asks the Qt editor to open the Alignment Layout table on \ref alignmentLayoutName
  /// (empty: the selected or first alignment). A headless run only reports it.
  bool alignmentLayoutRequested = false;
  std::string alignmentLayoutName;
  /// REQ-491: EDITALIGNMENTSTYLE asks the Qt editor to open the style dialog on
  /// The alignment style name is stored in alignmentStyleEditorName.
  bool alignmentStyleEditorRequested = false;
  std::string alignmentStyleEditorName;
  /// REQ-485: set by the Qt editor. ALIGNMENT / CREATEALIGNMENT with no arguments then ask it for the Create Alignment
  /// dialog, and CREATEALIGNMENTFROMOBJECTS asks for its dialog once the objects and direction are picked.
  bool createAlignmentDialogAvailable = false;
  bool createAlignmentDialogRequested = false;
  bool alignmentFromObjectsDialogRequested = false;
  /// REQ-485.1 Curve and Spiral Settings, kept for the session like Civil 3D's layout settings. A radius of 0 means
  /// the drawing's default (100 ft, or 30 m in a metric drawing). Tangent-Tangent starts without curves, as Civil 3D's
  /// layout toolbar does; the last mode chosen is kept.
  alignment::CurveSettings alignmentCurveSettings{false, 0.0, 0.0, 0.0};
  /// ALIGNMENTLAYOUT (Kind::AlignmentLayout): the PIs picked so far and the alignment as it was before the session, so
  /// every PI rebuilds the tail from scratch and `U` can take one back.
  struct AlignmentLayoutSession {
    std::string name;
    std::vector<alignment::Point2> pis;  ///< Drawing-local.
    alignment::Alignment base;
    bool snapshotTaken = false;
    bool fixedDirection = false;  ///< The first tangent keeps the dialog's starting direction.
    double directionAzimuth = 0.0;
    size_t warningsShown = 0;
  } alignmentLayout;
  /// CREATEALIGNMENTFROMOBJECTS picks (Kind::AlignmentFromObjects). The picked objects live in `selection`, so the
  /// command that finally creates the alignment reads them from there.
  struct AlignmentFromObjectsPick {
    enum class Phase : std::uint8_t { WaitFirst, WaitMore, WaitDirection } phase = Phase::WaitFirst;
    bool hasPick = false;
    double pickX = 0.0;  ///< Drawing-local point of the first pick: the alignment starts at the end nearest it.
    double pickY = 0.0;
    bool reverse = false;
  } alignmentFromObjects;
  /// REQ-485.1: the Alignment Layout Tools window follows this alignment while it is open.
  std::string alignmentLayoutToolsName;

  std::string blockEditorName;
  CadBlockDefinition blockEditorSnapshot;
  bool blockEditorDirty = false;
  bool blockEditPickerOpen = false;
  char blockEditPickerName[256]{};
  /// In-place block editing (REQ-107 / ADR-043). While active, the model arrays hold the
  /// definition's primitive geometry in local coords and \c blockEditModelStash holds the real
  /// drawing. Session-only — never in \ref DrawingDocument, never in `internal document payload`.
  bool blockEditActive = false;
  bool bconnectAwaitingFace = false;
  char bconnectNameBuf[128]{};
  char bconnectSizeBuf[64]{};
  /// Role/engagement queued from a prompt-form BCONNECT, applied once the face pick resolves
  /// (issue #486 increment A2).
  CadBlockConnectionRole bconnectRolePending = CadBlockConnectionRole::Inlet;
  float bconnectEngagementPending = 0.f;
  /// Compatibility tag queued from the BCONNECT wizard, applied once the point resolves (typed
  /// coordinates or a face pick) — issue #496.
  std::string bconnectCompatTagPending;
