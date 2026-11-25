  // --- Remembered settings for REVCLOUD, POLYSOLID, and PIPERUN ----------------------------------
  // Per-invocation phases and drafts live in stepengine::RevcloudSession, PolysolidSession,
  // PipeRunSession, PipeFitSession, and PipeSplitSession.

  /// Remembered between invocations (REQ-444 item 3): the size of each scallop, whether they bulge
  /// the other way, and whether Object mode erases the shape it converted.
  double revcloudArcLength = 0.5;
  bool revcloudReverse = false;
  bool revcloudDeleteOriginal = false;

  /// Remembered between invocations, the way AutoCAD remembers PSOLWIDTH and PSOLHEIGHT: a wall is
  /// almost always drawn at the same size as the last one, and re-typing it every time is the
  /// friction that makes a command feel wrong. Saved with the drawing.
  double polysolidWidth = 0.25;
  double polysolidHeight = 4.0;
  brep::Justify polysolidJustify = brep::Justify::Center;

  /// Remembered across runs the way POLYSOLID remembers width/height/justify (`polysolidWidth`
  /// etc. above): a pipe run is almost always drawn at the same size as the last one. Starts at 4in
  /// rather than empty (D-2026-09-28-k) so the Modeling ribbon's size dropdown always shows a size and
  /// its PIPERUN button never has to stop and ask (REQ-355).
  std::string pipeRunNominalSize = "4in";
  std::string pipeRunPressureClassTag;
  /// Per-run cache behind `pipeRunWorldSolids`: one content signature and one solid list per entry of
  /// `cadPipeRuns`, so a rebuild only re-sweeps the runs that actually changed.
  std::vector<std::uint64_t> pipeRunSolidCacheSigs;
  std::vector<std::vector<CadSolidPtr>> pipeRunSolidCache;
  /// Wall thickness in INCHES for the run being drafted (D-2026-09-23-a).
  double pipeRunWallThicknessIn = 0.0;
  /// Ortho/polar compass (REQ-346).
  bool pipeRunCompassOn = true;
