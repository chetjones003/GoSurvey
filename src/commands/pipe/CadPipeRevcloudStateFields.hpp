  // --- REQ-444 REVCLOUD: a scalloped closed outline ----------------------------------------------

  enum class RevcloudPhase : std::uint8_t {
    WaitFirstPoint,  ///< the first corner, or `O` to convert a closed shape already drawn
    WaitObject,      ///< `O` was given: the next click names the closed polyline or circle
    WaitNextPoint    ///< a path is under way; each further point adds a corner
  } revcloudPhase = RevcloudPhase::WaitFirstPoint;

  /// The corners picked so far, as x,y pairs in storage coordinates.
  std::vector<double> revcloudDraft;
  /// Elevation the cloud commits at: the work plane's, read when the first corner is picked.
  double revcloudZ = 0.0;
  /// Remembered between invocations (REQ-444 item 3): the size of each scallop, whether they bulge
  /// the other way, and whether Object mode erases the shape it converted.
  double revcloudArcLength = 0.5;
  bool revcloudReverse = false;
  bool revcloudDeleteOriginal = false;
  /// A letter typed on its own, waiting for its value on the next line (`A` then `1`). 0 = none.
  char revcloudPending = 0;

  /// Remembered between invocations, the way AutoCAD remembers PSOLWIDTH and PSOLHEIGHT: a wall is
  /// almost always drawn at the same size as the last one, and re-typing it every time is the
  /// friction that makes a command feel wrong. Saved with the drawing.
  double polysolidWidth = 0.25;
  double polysolidHeight = 4.0;
  brep::Justify polysolidJustify = brep::Justify::Center;

  // --- PIPERUN: interactive CadPipeRun routing (issue #486 increment B2 / REQ-345) --------------

  enum class PipeRunPhase : std::uint8_t {
    WaitNominalSize,     ///< first prompt of a run: nominal size, optional pressure class
    WaitWallThickness,   ///< size known; the wall the pipe is hollow to (D-2026-09-23-a)
    WaitFirstPoint,      ///< size and wall known; the next click/point is the run's start
    WaitNextPoint,       ///< a run is under way; each further point commits a straight segment
  } pipeRunPhase = PipeRunPhase::WaitNominalSize;

  /// The path so far, storage-coordinate xyz triples (REQ-057) — exactly the form `CadPipeRun`
  /// itself stores, so commit is a plain copy rather than a second representation to keep in step.
  std::vector<double> pipeRunDraftVerts;
  /// Remembered across runs the way POLYSOLID remembers width/height/justify (`polysolidWidth`
  /// etc. above): a pipe run is almost always drawn at the same size as the last one. Starts at 4in
  /// rather than empty (D-2026-09-28-k) so the Modeling ribbon's size dropdown always shows a size and
  /// its PIPERUN button never has to stop and ask (REQ-355).
  std::string pipeRunNominalSize = "4in";
  std::string pipeRunPressureClassTag;
  /// Index into `cadPipeRuns` of the PROVISIONAL run PIPERUN materialises while routing, or -1
  /// (D-2026-09-24-d). The route exists in the drawing from the second click so it can be seen,
  /// snapped to and spliced into while the command is still open; END retires it and rebuilds the
  /// finished route (with auto-elbows and branch tees), and Esc removes it.
  ///
  /// Always the LAST element of `cadPipeRuns` while it is set — nothing else appends a run while
  /// PIPERUN holds the command — which is what lets it be withdrawn without renumbering anything
  /// (architecture invariant §11.9: an index is not a name).
  int pipeRunLiveIndex = -1;
  /// Per-run cache behind `pipeRunWorldSolids`: one content signature and one solid list per entry of
  /// `cadPipeRuns`, so a rebuild only re-sweeps the runs that actually changed.
  ///
  /// The array-level signature alone made every click while routing re-sweep EVERY run in the
  /// drawing, and a run's swept tube is the most expensive thing the command does — measured at
  /// ~5.2 ms for two points and ~11 ms more per point after that. With live routing (D-2026-09-24-d)
  /// that cost landed on every click; this keeps it to the one run being drawn.
  std::vector<std::uint64_t> pipeRunSolidCacheSigs;
  std::vector<std::vector<CadSolidPtr>> pipeRunSolidCache;
  /// Wall thickness in INCHES for the run being drafted (D-2026-09-23-a). Unlike the size and class
  /// beside it this is NOT remembered across runs: blank Enter at its prompt takes the schedule-40
  /// wall for whatever size was just chosen, so the offered default follows the size rather than
  /// quietly carrying a one-off thickness onto a different pipe. 0 while no run is in progress.
  double pipeRunWallThicknessIn = 0.0;
  /// Ortho/polar compass (REQ-346): while \c true, the next-vertex preview and pick snap to
  /// REQ-108's angle set (\ref polarIncrementDeg / \ref polarExtraAnglesDeg) measured from the run's
  /// last committed vertex. Its own toggle, independent of \ref polarMode, so PIPERUN keeps snapping
  /// even with ordinary POLAR tracking off (and vice versa). On by default, matching AutoCAD Plant
  /// 3D's own compass default. Not saved with the drawing — a per-command UI setting, like
  /// \ref orthoMode / \ref polarMode.
  bool pipeRunCompassOn = true;

  // --- PIPEFIT: manual fitting placement on an existing run (issue #486 increment B7) -----------

  /// Index into `cadPipeRuns` the command is splicing into, fixed at `StartPipeFitCommand` time
  /// (from the caller's own single-pipe-run selection) — never re-resolved from `st.selection`
  /// again, so a selection change mid-command can't silently retarget a different run.
  int pipeFitRunIndex = -1;
  /// The catalog part type to splice in (e.g. Valve, Flange, Reducer), fixed at start time too.
  CadPipePartType pipeFitPartType = CadPipePartType::None;

  // --- PIPESPLIT: split an existing run at a station, no fitting inserted (issue #486 inc B8) ----

  /// Same fixed-at-start convention as `pipeFitRunIndex` — set by `StartPipeSplitCommand` from the
  /// caller's own single-pipe-run selection, never re-resolved from `st.selection` mid-command.
  int pipeSplitRunIndex = -1;
