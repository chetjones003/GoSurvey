  // -------------------------------------------------------------------------
  // Open drawings tab bar
  // -------------------------------------------------------------------------
  struct DrawingTab {
    std::string  name;
    uint32_t     uid = 0;  ///< Stable per-tab ID used to prevent label collisions.
    uint32_t     projectUid = 0;  ///< REQ-374: ProjectSession::uid this drawing belongs to; 0 = standalone.
    /// REQ-376 (#696 P3): how this tab's survey points relate to the project database. Unattached = not
    /// decided yet (decided on the first frame after the tab is built); Shared = the tab reads and
    /// writes the project's database; Detached = the drawing arrived carrying points of its own, which
    /// stay in the DWG untouched until Add Drawing to Project (P5) merges them.
    enum class PointsMode : uint8_t { Unattached, Shared, Detached };
    PointsMode   pointsMode = PointsMode::Unattached;
    /// Shared: the points (WORLD coordinates) this tab last agreed with the database, and the database
    /// revision that was. The diff against them is what a frame's edits are.
    std::vector<SurveyPoint> pointsBaseWorld;
    uint64_t     pointsRevision = 0;
    /// The rules (REQ-377) the tab last built its point view with; a different live set means "rebuild".
    projpts::Rules pointsRulesApplied;
  };
  /// REQ-374 / REQ-382 (#696 P1): one entry per open project. Each drawing tab belongs to exactly one
  /// session (or none). A session with no tabs left is closed and its lock released (ServiceProjects).
  struct ProjectSession {
    uint32_t         uid = 0;
    gsproj::Project  project;
    gsproj::LockInfo me;           ///< who we are in the lock file
    bool             readOnly = false;  ///< opened read-only: nothing in the project may be written
    /// REQ-375: project.settingsJson, parsed once when the project opens and again when Project
    /// Settings changes it. Never null for an open session.
    std::shared_ptr<ProjectSettings> settings;
    /// REQ-376 / ADR-065: the project's one survey point database, shared by every tab of the project.
    /// Null when the file could not be read (pointsError says why) — then the project's tabs keep their
    /// points in the DWG and the damaged file is never written over.
    std::shared_ptr<projpts::Db> points;
    std::string                  pointsError;
  };
  std::vector<ProjectSession> openProjects;
  uint32_t                    nextProjectUid = 1u;
  /// A question the user must answer before a project open can continue (damaged marker, or the lock
  /// is held). Drawn as a modal by DrawProjectDialogs; the continuation re-enters the open path.
  struct ProjectPrompt {
    enum class Kind : std::uint8_t { None, Damaged, Locked } kind = Kind::None;
    std::string      gsprojPath;
    std::string      dwgPath;      ///< the drawing being opened, or empty for Open Project
    std::string      message;      ///< why the marker is damaged
    gsproj::LockInfo holder;       ///< Locked: who holds it
    bool             stale = false;
    bool             openRequested = false;  ///< ask the UI to open the modal on its next update
  } projectPrompt;
  bool showNewProjectDialog = false;  ///< REQ-374 clause 1; File > New Project / Start screen
  /// REQ-375: the Project Settings window is open for ProjectSession::uid == this (0 = closed).
  uint32_t projectSettingsUid = 0;
  /// REQ-378 (#696 P5): Add Drawing to Project was asked for in project \c ProjectSession::uid == this
  /// (0 = no request). DrawProjectDialogs browses for the drawing, builds \ref addDrawingPlan, and shows
  /// the preview dialog; nothing is written until the user confirms.
  uint32_t addDrawingToProjectUid = 0;
  std::shared_ptr<AddDrawingPlan> addDrawingPlan;
  /// REQ-379 (#696 P6): the Copy / Link / Cancel question asked when a point cloud or PDF from outside
  /// the project is attached in a project drawing. Drawn as a modal by DrawProjectDialogs; the answer
  /// goes to ResolveProjectAttach and then on to the attach that asked.
  struct ProjectAttachPrompt {
    enum class Kind : std::uint8_t { None, PointCloud, Pdf, PdfTrack } kind = Kind::None;  ///< PdfTrack: Add PDF to project (REQ-379 cl. 5), no placement
    uint32_t    projectUid = 0;
    std::string sourcePath;          ///< UTF-8
    std::string destRel;             ///< where a copy would go, project-relative
    std::uint64_t sizeBytes = 0;
    bool        reuse = false;       ///< an identical copy is already in the project
    bool        openRequested = false;
  } projectAttachPrompt;
  /// REQ-379 clause 6: the answer to Refresh. Filled only when the user pressed Refresh and new files exist.
  struct ProjectRefreshPrompt {
    uint32_t                 projectUid = 0;
    std::vector<std::string> files;   ///< project-relative, `/`
    std::vector<char>        picked;  ///< one tick per file
    bool                     openRequested = false;
    bool                     open = false;
  } projectRefreshPrompt;
  /// REQ-383 (#696 P9): a point edit in a project drawing that needs the user's answer before it reaches
  /// the shared database. SyncProjectPoints fills it in and waits; DrawProjectDialogs asks; the answer
  /// is stored here and the next SyncProjectPoints frame carries it out. Cleared once the edit is
  /// applied, undone, or the user leaves the tab.
  struct PointEditPrompt {
    bool        active = false;       ///< an unanswered question is showing (or waiting to show)
    uint32_t    projectUid = 0;
    int         tabIdx = 0;
    std::string projectName;
    std::vector<int> removed;         ///< numbers the drawing deleted
    std::vector<int> conflicts;       ///< numbers that already exist in the project, hidden in this drawing
    int         othersOpenShowing = 0;   ///< other OPEN drawings of the project that show a deleted point
    int         othersClosedMaybe = 0;   ///< drawings of the project that are not open (rules unread)
    bool        deletePending = false;   ///< the delete question (not only the number-conflict one) is showing
    bool        clipboardPaste = false;  ///< number conflict came from a pending clipboard paste.
    std::vector<std::string> otherNames; ///< names of those open drawings
    /// The answers. None = not asked yet. Delete: Proceed / HideHere / Cancel. Conflict: Proceed
    /// (overwrite) / Renumber / Cancel.
    enum class Answer : uint8_t { None, Proceed, HideHere, Renumber, Cancel };
    Answer      deleteAnswer = Answer::None;
    Answer      conflictAnswer = Answer::None;
  } pointEditPrompt;
  bool clipboardPasteResolving = false; ///< the user answered a clipboard point-number question; bypass preflight once.
  bool clipboardPasteOverwrite = false;
  bool clipboardPasteOriginal = false;
  /// REQ-383 clauses 1 and 3: a paste that crosses a project / coordinate-system / units boundary.
  /// `block` = coordinate-system mismatch (only Cancel); otherwise Paste anyway / Cancel.
  struct PastePrompt {
    bool        active = false;
    bool        block = false;
    bool        original = false;    ///< PASTEORIG rather than PASTE
    std::string text;
    bool        openRequested = false;
    bool        canConvert = false;  ///< the dialog offers "Convert and paste" (D-2026-10-07-a)
  } pastePrompt;
  bool pasteWarningAnswered = false;   ///< "Paste anyway" was chosen: the next paste start skips the check
  /// REQ-383 clause 6: closing a project's drawing tab while its point database cannot be written.
  /// `tabIdx` < 0 = no question.
  struct CloseTabPrompt {
    int         tabIdx = -1;
    std::string text;
    bool        openRequested = false;
    bool        confirmed = false;   ///< "Close anyway": the tab loop closes it next frame
  } closeTabPrompt;
  uint32_t projectHealthUid = 0;     ///< REQ-379 clause 4: the Project Health window is open for this project
  /// REQ-380 (#696 P7): the Pack Project window. `projectUid` is the project being packed (0 = closed);
  /// the plan is what a pack would hold, refreshed when the window opens and after "Copy links in".
  struct ProjectPackPrompt {
    uint32_t         projectUid = 0;
    bool             planned = false;
    bool             planTried = false;  ///< a failed plan is not retried every frame
    gspack::PackPlan plan;
    std::string      planError;
    bool             excludePointClouds = false;
    bool             packAnyway = false;  ///< the user accepted the Health problems
  } projectPackPrompt;
  /// REQ-381 (#696 P8): the Create Turnover window. `projectUid` is the project (0 = closed). Every
  /// tracked file starts ticked; `unticked` holds the ones the user cleared.
  struct ProjectTurnoverPrompt {
    uint32_t              projectUid = 0;
    std::string           recipient;
    std::set<std::string> unticked;
    bool                  acknowledged = false;  ///< the user accepted the Health problems
  } projectTurnoverPrompt;
  bool openPackRequested = false;    ///< REQ-380: OPENPACK asked for Open Packed Project (the UI browses)
  /// REQ-308 / D-2026-08-30-a: drawingTabs[0] is the **Start screen** — a non-closable, pinned-first
  /// sentinel that backs no document. documents[0]/viewportRenderers[0] exist for index alignment
  /// but are never meaningful. Real drawings start at FirstDrawingTabIndex().
  std::vector<DrawingTab>     drawingTabs{{"Start", 0u}, {"Drawing 1", 1u}};
  int      activeDrawingIdx   = 0;   ///< 0 = Start screen on launch.
  int      nextDrawingNumber  = 2;    ///< Auto-incremented for "Drawing N" naming.
  uint32_t nextTabUid         = 2u;   ///< Monotonically increasing; each new tab gets a unique uid.
  bool pendingDrawingTabSwitch = false; ///< Set for one frame after a programmatic tab change.
  bool propertiesPanelActive  = false; ///< True when Properties is the selected tab in its dock node.
  int  prevDrawingIdx         = 0;     ///< Authoritative "last active" idx; used by main.cpp for switch detection.
  int  pendingTabErase        = -1;    ///< If >= 0, main.cpp must shut down + erase viewportRenderers[this index].
  /// REQ-308: after a drawing is opened or saved, the main loop renders it once then captures a
  /// thumbnail for the Recent list. Set together; serviced and cleared after RenderScene when
  /// pendingThumbnailTabIdx == activeDrawingIdx.
  std::string pendingThumbnailPath;
  int         pendingThumbnailTabIdx = -1;
  /// Per-drawing snapshots — one entry per open tab.  Active tab's live data lives in the fields
  /// above; this vector is read/written by SaveDocumentToSnapshot / RestoreDocumentFromSnapshot.
  std::vector<DrawingDocument> documents{2};  ///< [0] pairs with the Start sentinel tab (unused); see drawingTabs.

  // --- Active-document dirty/path tracking (mirrors DrawingDocument fields for the live tab) ---
  uint32_t    activeDocSavedRevision = 0;   ///< cadGpuRevision when the active doc was last saved.
  std::string activeDocFilePath;            ///< Absolute path to the active doc's .dwg drawing file.
  int undoHistoryMaxSize = 50; ///< Maximum undo frames per drawing tab (0 = unlimited). Settings → User Preferences.

  // --- Close confirmation ---
  bool confirmCloseModal = false;  ///< Set by the main loop to open the "Unsaved Changes" dialog.
  /// REQ-383 clause 6: projects whose database could not be written, found when the quit prompt was raised.
  std::vector<UnsavedProject> closeUnsavedProjects;
  bool closeConfirmed    = false;  ///< Set by the dialog to signal the main loop to exit.

  // --- DWG export confirmation (REQ-052) ---
  /// Set when the user has chosen a DWG save path; the dialog states what Phase 1 export drops
  /// before anything is written, because a DWG save can overwrite a drawing GoSurvey did not author.
  bool        dwgLossyExportModal = false;
  std::string dwgPendingExportPath;  ///< Destination chosen in the save dialog, written only on confirm.
  DwgSaveVersion dwgExportVersion = DwgSaveVersion::R2018;  ///< Format for the pending Export DWG (issue #600, D-2026-10-08-c).

  // -------------------------------------------------------------------------
  // ALIGN results dialog state (survives after ALIGN session ends)
  // -------------------------------------------------------------------------
  struct AlignControlPt { float srcX = 0.f, srcY = 0.f, dstX = 0.f, dstY = 0.f; };
  std::vector<AlignControlPt> alignControlPts;

  struct HelmertResult {
    bool  valid = false;
    float a  = 1.f, b  = 0.f;   ///< X' = a*x - b*y + tx
    float tx = 0.f, ty = 0.f;
    float scale = 1.f;
    float rotationCwNorthDeg = 0.f;
    std::vector<float> pairResiduals; ///< per-pair distance residual (destination units)
    float rms = 0.f;
    int   nPairs = 0;
  } alignLastResult;

  bool showAlignResultsWindow = false;
  /// Snapshot of selection committed at ALIGN PickSelection → PickSrc transition.
  std::vector<SelectedEntity> alignSelectionSnapshot;
  std::vector<int> alignSurveySnapshot;
  bool alignHasSelection = false; ///< true = only transform snapshotted entities; false = all

  // -------------------------------------------------------------------------
  // PDFATTACH command state
  // -------------------------------------------------------------------------
  enum class PdfAttachPhase : std::uint8_t {
    WaitDialog,       ///< Dialog is open; user browses / configures
    Building,         ///< Async rasterize running in background; dialog shows spinner
    WaitInsertPoint,  ///< User picks insertion point in viewport
    WaitScaleRef,     ///< User picks second point to define scale interactively
    WaitRotationPt,   ///< User picks rotation reference point
  } pdfAttachPhase = PdfAttachPhase::WaitDialog;

  bool pdfAttachDialogOpen = false;

  /// REQ-387 (#732): requests the PDF Viewer window consumes on its next frame. The typed command
  /// has no window of its own; it only asks. Empty path + pick = show the file dialog.
  std::string pdfViewerOpenRequest;
  bool pdfViewerPickRequest = false;
  bool pdfSplitRequest = false;  ///< PDFSPLIT (REQ-389): open the Split dialog on the focused viewer
  std::string pdfViewBenchPath;  ///< `BENCH PDFVIEW <file>`: time this real PDF's first page and thumbnail strip
  int pdfViewBenchPages = 0; ///< `BENCH PDFVIEW [pages]`: build a synthetic PDF this long and time scrolling it
  bool pdfDiffBench = false; ///< `BENCH PDFDIFF` (REQ-393): align and compare a generated 36 x 24 in sheet pair
  int pdfCompareBenchPages = 0; ///< `BENCH PDFCOMPARE [pages]` (REQ-392): build two synthetic PDFs this long and time panning their overlay

  // -------------------------------------------------------------------------
  // INSERT dialog (issue #124)
  // -------------------------------------------------------------------------
  enum class InsertBlockPhase : std::uint8_t {
    WaitDialog,
    WaitInsertPoint,
    WaitScale,
    WaitRotation,
    WaitAlignFace,       ///< Pick a planar solid face to orient the fitting (issue #475 inc3)
    WaitConnectorTarget, ///< Pick a target connection port on a placed block (issue #475 inc5)
    WaitAttributes,
  } insertBlockPhase = InsertBlockPhase::WaitDialog;

  bool insertBlockDialogOpen = false;
  char insertBlockName[256]{};
  char insertBlockPath[4096]{};
  char insertBlockAngleBuf[64]{};
  float insertBlockX = 0.f;
  float insertBlockY = 0.f;
  float insertBlockZ = 0.f;
  float insertBlockSx = 1.f;
  float insertBlockSy = 1.f;
  float insertBlockSz = 1.f;
  float insertBlockRotDeg = 0.f;
  float insertBlockRotXDeg = 0.f;
  float insertBlockRotYDeg = 0.f;
  char insertBlockRotXBuf[64]{};
  char insertBlockRotYBuf[64]{};
  bool insertBlockSpecifyPoint = true;
  bool insertBlockSpecifyScale = false;
  bool insertBlockSpecifyRot = true;
  bool insertBlockSpecifyAlignFace = false;
  bool insertBlockSpecifyConnectorSnap = false;
  char insertBlockConnectorName[64]{};
  /// The pipe run whose END the connector snap fitted the part onto, or -1 (REQ-353): the placed
  /// part — an end flange, a cap — takes that run's layer and colour. Consumed by the placement.
  int insertBlockSnappedPipeRun = -1;
  /// INSERT dialog override for block insertion units (issue #475 inc6). Empty uses the definition.
  char insertBlockUnitsBuf[32]{};
  bool insertBlockUniformScale = true;
  bool insertBlockExplode = false;
  /// REQ-350 (f) — set when this INSERT was armed from the Pipe Fittings palette. It changes exactly
  /// one thing: a pick that lands ON a pipe run splices the part into that run with the engagement
  /// cutback (the PIPEFIT path) instead of placing a free block reference. A pick anywhere else
  /// behaves as an ordinary INSERT, which is why this is a flag on INSERT rather than a command of
  /// its own — the ghost, the snapping, the click routing and ESC are all already correct.
  bool insertBlockPipeSpliceArmed = false;
  bool insertBlockAttrDialogOpen = false;
  /// Library pane filters (issue #486 increment A5). `None` = no filter on that axis. Size is a
  /// free-text substring match against \ref CadBlockLibraryEntry::nominalSize.
  CadPipePartType insertLibFilterPartType = CadPipePartType::None;
  CadPipePressureClass insertLibFilterPressureClass = CadPipePressureClass::None;
  char insertLibFilterSizeBuf[32]{};
  int insertBlockAttrRefIndex = -1;
  bool insertBlockAttrPaper = false;
  char insertBlockAttrBuf[8][128]{};

  // Block Create dialog (ribbon Create button) — mirrors Insert dialog (issue #475 follow-up)
  enum class BlockCreatePhase : std::uint8_t { WaitDialog, WaitBasePoint } blockCreatePhase = BlockCreatePhase::WaitDialog;
  bool blockCreateDialogOpen = false;
  char blockCreateName[256]{};
  float blockCreateBaseX = 0.f;
  float blockCreateBaseY = 0.f;
  float blockCreateBaseZ = 0.f;
  bool blockCreateSpecifyBase = true;
  int blockCreateConvertMode = 1; // 0 retain, 1 convert, 2 delete
  char blockCreateDescription[256]{};
  char blockCreateUnits[32]{};

  /// WBLOCK save dialog (user request 2026-09-23). `WBLOCK <name>, <path.dwg>` still writes the file
  /// directly; the bare `WBLOCK` verb, which used to answer with a usage line, opens this instead.
  bool wblockDialogOpen = false;
  char wblockName[256]{};
  char wblockPath[1024]{};

  char pdfAttachFilePath[1024]{};
  int  pdfAttachSelectedPage = 0;
  float pdfAttachInsertX  = 0.f;
  float pdfAttachInsertY  = 0.f;
  float pdfAttachScale    = 1.f;
  float pdfAttachRotDeg   = 0.f;
  /// DPI used to rasterize the final attached page texture.
  float pdfAttachRasterDpi = 150.f;
  bool pdfAttachSpecifyInsert = true;
  bool pdfAttachSpecifyScale  = false;
  bool pdfAttachSpecifyRot    = false;
  bool pdfAttachSnapLines   = true;
  bool pdfAttachSnapCircles = true;
  bool pdfAttachSnapText    = false; // text positions cause spurious endpoint snaps; disabled by default
  /// Opaque per-document draft cache (owned; freed when command ends or file changes).
  PdfDraftCache* pdfDraftCache = nullptr;

  // --- Async build (Building phase) ------------------------------------
  // Background thread rasterizes the page; main thread uploads the GL texture
  // when done.  Heap-allocated so atomic members don't affect copyability.
  struct AsyncBuild {
    std::thread           thread;
    std::atomic<bool>     done{false};
    PdfAttachPixelResult  result;
    bool                  specifyInsert = false; ///< captured at click time
  };
  std::unique_ptr<AsyncBuild> pdfAttachAsync;   ///< non-null while Building

  /// Preview attachment built during WaitInsertPoint (cursor-follows).
  PdfAttachment pdfAttachPreview;
  bool          pdfAttachPreviewReady = false;
