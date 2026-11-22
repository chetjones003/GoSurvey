  /// One in-flight background rebuild per surface currently being retriangulated (REQ-069's dynamic
  /// rebuild — architecture §8's one-shot worker pattern, its second concrete use after
  /// \ref pdfAttachAsync and the first to implement the full contract: rules 4 and 5, generation
  /// staleness and cooperative cancellation, which pdfAttachAsync's own struct does not itself carry).
  /// Heap-allocated so the atomic/thread members don't affect this struct's own copyability — the
  /// same reason \ref AsyncBuild is. Never part of \ref DrawingGeometrySnapshot or \ref
  /// DrawingDocument: a background thread is live-only state, not drawing content.
  struct SurfaceRebuildAsync {
    /// Which CadSurface this is for, by **stable entity id** (REQ-076 / ADR-036 (a)) — never by name
    /// and never by index.
    ///
    /// This was `std::string surfaceName` until surfaces gained an `EntityKind`, and the name key had
    /// two failure modes that the id does not. A **rename** while a rebuild is in flight orphaned the
    /// result: the reap looked the old name up, found nothing, discarded a completed triangulation,
    /// and — because the in-flight check also matched on name — immediately dispatched a second
    /// worker for the same surface. Worse, **erase-then-recreate under the same name** made the reap
    /// find the *new* surface and apply the *old* one's triangulation to it. An id is not reused
    /// within a drawing (REQ-076), so neither is reachable.
    std::uint64_t      surfaceId = 0;
    std::thread        thread;
    std::atomic<bool>  done{false};
    std::atomic<bool>  cancel{false}; ///< cooperative; checked before the worker starts real work
    std::uint32_t      generation = 0;  ///< cadGpuRevision at dispatch — a mismatch on completion means discard
    double             originX = 0.0, originY = 0.0;
    TinBuildResult      result;

    /// Joins the worker if it is still running. Without this, destroying a job whose thread is
    /// still joinable calls std::terminate — and the ONLY other join site (\ref TickSurfaceRebuilds)
    /// is reachable only once `done` is already set, so every path that drops a job mid-flight hit
    /// that: closing the application with a rebuild in flight aborted the process instead of
    /// exiting, and since cadGpuRevision moves on every drawing mutation, "edit then close" was
    /// enough to trigger it. Joining after the reap path's own join is a no-op (not joinable).
    ~SurfaceRebuildAsync() {
      cancel.store(true, std::memory_order_release);  // lets a not-yet-started worker return at once
      if (thread.joinable())
        thread.join();
    }
  };
  std::vector<std::unique_ptr<SurfaceRebuildAsync>> surfaceRebuildAsync;

  /// One in-flight `.gscloud` out-of-core cache build (REQ-171/172, ADR-060) — architecture §8's
  /// one-shot-worker contract again, same shape as \ref SurfaceRebuildAsync. At most one exists at
  /// a time: `POINTCLOUDATTACH` refuses to start a second import while one is already running,
  /// rather than queuing (the user asked for a progress bar with an ETA, which only makes sense
  /// for a single tracked job).
  ///
  /// The worker thread touches ONLY this struct's own fields (plus read-only source/cache paths) —
  /// never `AppCommandState` directly. Committing the result into `st.drawing.cadPointClouds` (undo
  /// snapshot, entity id assignment, GPU-cache bump) happens on the main thread, in
  /// `TickPointCloudImport`, once `done` is observed — the same "worker computes, main thread
  /// applies" split `SurfaceRebuildAsync`/`TinBuildResult` already uses.
  struct PointCloudImportAsync {
    /// Which reader/cache-builder pair to use, decided once from `sourcePath`'s extension when the
    /// job is created (`StartPointCloudImportAsync`) — REQ-172's "share the async plumbing across
    /// formats instead of duplicating the worker" shape. Append a new value, never reorder/remove.
    enum class SourceFormat : std::uint8_t { E57, Pts, Ptx, Las, Laz };
    SourceFormat format = SourceFormat::E57;
    std::string sourcePath;
    std::string cachePath;
    std::thread thread;
    std::atomic<bool> done{false};
    std::atomic<bool> cancelRequested{false};
    /// Points written to disk during the streaming pass (phase 1 of the build).
    std::atomic<std::int64_t> pointsStreamed{0};
    /// Points that have landed in a completed octree leaf during the recursive split (phase 2) —
    /// reaches \ref totalPointsEstimate exactly when the build finishes.
    std::atomic<std::int64_t> pointsFinalized{0};
    /// Leaves read back while assembling the preview sample (phase 3 — `BuildPreviewFromCache`).
    /// This phase reads the WHOLE cache back off disk (every leaf, to subsample it), a full pass
    /// with its own real cost that earlier had no progress reporting at all — the exact cause of
    /// a bar stuck at 100% while the worker was still genuinely working (TASK-270 log).
    std::atomic<std::int64_t> previewLeavesRead{0};
    /// Total leaf count, set once (by the worker) right after the cache is opened for the preview
    /// pass — 0 until then, which the UI reads as "phase 3 hasn't started yet."
    std::atomic<std::int64_t> previewTotalLeaves{0};
    /// From `pointcloud_e57::QuickPointCountEstimate`, `pointcloud_pts::QuickPointCountEstimate`,
    /// `pointcloud_ptx::QuickPointCountEstimate`, `pointcloud_las::QuickPointCountEstimate`, or
    /// `pointcloud_laz::QuickPointCountEstimate`
    /// (per `format`), read before dispatch (cheap: metadata only). 0 means unknown — the
    /// progress UI falls back to an indeterminate bar in that case.
    std::int64_t totalPointsEstimate = 0;
    std::chrono::steady_clock::time_point startTime;
    /// UI-thread-only cosmetic state for the progress dialog: the ETA text is recomputed at most
    /// once a second (TASK-270 — it was ticking every frame, which read as jittery rather than a
    /// countdown), never touched by the worker thread.
    std::chrono::steady_clock::time_point lastEtaUpdate{};
    std::string cachedEtaText = "estimating...";

    // Worker-produced result — read only after `done` is observed true.
    bool ok = false;
    std::string errorMessage;
    std::vector<double> previewXyz;
    std::vector<float> previewColors;
    std::vector<float> previewIntensity;
    pointcloud::Octree octree;
    std::int64_t totalPointCount = 0;

    /// Same reasoning as `SurfaceRebuildAsync`'s own destructor: without this, destroying a job
    /// whose thread is still running calls `std::terminate` (closing the app mid-import would
    /// abort the process instead of exiting).
    ~PointCloudImportAsync() {
      cancelRequested.store(true, std::memory_order_release);
      if (thread.joinable()) thread.join();
    }
  };
  std::unique_ptr<PointCloudImportAsync> pointCloudImportAsync;

  /// One in-flight Volume Dashboard recompute (REQ-073's 2026-08-23 amendment, TASK-095 §6 step 3) —
  /// architecture §8's one-shot-worker contract again, in the same shape \ref SurfaceRebuildAsync
  /// already has, rather than a second staleness mechanism for the same problem (D-2026-08-23-k).
  /// At most one exists at a time: there is one dashboard, not one per surface.
  struct VolumeDashboardAsync {
    std::uint64_t baseSurfaceId = 0;
    std::uint64_t comparisonSurfaceId = 0;
    std::uint64_t clipEntityId = 0;
    /// Strong references to the exact triangulations this job computes against — captured on the UI
    /// thread at dispatch, read only on the worker thread. Safe with no locking because a `CadTin` is
    /// immutable once built and only ever REPLACED, never written through (architecture §11.5): the
    /// worker's copy of the pointer cannot be invalidated by whatever the UI thread does to the
    /// surface's OWN pointer while this runs.
    std::shared_ptr<const CadTin> tinBase;
    std::shared_ptr<const CadTin> tinComparison;
    /// REQ-131 clip ring in TIN-local XY, captured at dispatch. Empty = no clip.
    std::vector<std::pair<double, double>> clipRingXy;
    /// Whether the cut/fill map was wanted AT DISPATCH — captured so toggling the map on after a
    /// mapless result already landed is detected as staleness (the landed result has no map to show).
    bool wantMap = false;
    std::thread thread;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::uint32_t generation = 0;  ///< cadGpuRevision at dispatch — a mismatch on completion means discard
    SurfaceVolumeResult result;
    /// REQ-073's cut/fill map — `GL_TRIANGLES` layout, populated only when \c wantMap is true (empty,
    /// not "no map": REQ-073's "shows nothing outside the common area" already covers a genuinely
    /// empty cut or fill side of a self-comparison).
    std::vector<float> cutTrianglesXyz;
    std::vector<float> fillTrianglesXyz;

    ~VolumeDashboardAsync() {
      cancel.store(true, std::memory_order_release);
      if (thread.joinable())
        thread.join();
    }
  };

  /// The Volume Dashboard's own state (REQ-073 amendment). **UI/session-only** — never in
  /// \ref DrawingGeometrySnapshot, never in \ref DrawingDocument, never in `internal document payload` — the amendment's
  /// own rule, matching REQ-075's Surface Manager for the same reason: this is a VIEW onto document
  /// state (which two surfaces, and their last computed comparison), not document state itself.
  struct VolumeDashboardState {
    bool open = false;
    bool showMap = false;

    /// 0 = none picked. By stable entity id (REQ-076), never by index or name — the same reason
    /// \ref SurfaceRebuildAsync keys on id rather than a name that a rename could orphan.
    std::uint64_t baseSurfaceId = 0;
    std::uint64_t comparisonSurfaceId = 0;
    /// 0 = no clip (full overlap). Otherwise a closed polyline's entity id (REQ-131).
    std::uint64_t clipEntityId = 0;

    /// The last landed result, and what it was computed FOR — a revision plus the two ids, so a
    /// change to EITHER a surface's triangulation OR the panel's own pick is detected as staleness by
    /// the same comparison (`workflow.md`/REQ-073: "the panel's surface pick changed... the in-flight
    /// result is discarded").
    bool hasResult = false;
    SurfaceVolumeResult lastResult;
    std::uint32_t resultForRevision = 0xFFFFFFFFu;
    std::uint64_t resultForBaseSurfaceId = 0;
    std::uint64_t resultForComparisonSurfaceId = 0;
    std::uint64_t resultForClipEntityId = 0;
    bool resultHasMap = false;  ///< was `showMap` on when `lastResult` was computed?
    /// REQ-073's cut/fill map for `lastResult`, `GL_TRIANGLES` layout — empty unless `resultHasMap`.
    std::vector<float> mapCutTrianglesXyz;
    std::vector<float> mapFillTrianglesXyz;

    struct AnalysisRow {
      std::string label;
      std::uint64_t baseSurfaceId = 0;
      std::uint64_t comparisonSurfaceId = 0;
      std::uint64_t clipEntityId = 0;
      SurfaceVolumeResult result;
      bool hasResult = false;
    };
    std::vector<AnalysisRow> rows;

    std::unique_ptr<VolumeDashboardAsync> job;  ///< null when nothing is in flight
  };
  VolumeDashboardState volumeDashboard;
  std::string lastVolumeReportText;  ///< Last successful VOLUMES / dashboard numbers for VOLREPORT.
  bool hasLastVolumeResult = false;
  SurfaceVolumeResult lastVolumeResult;
  std::string lastVolumeBaseName;
  std::string lastVolumeComparisonName;

  /// REQ-145 Quick Profile — session graph only, never `internal document payload` (same rule as Volume Dashboard).
  struct QuickProfileState {
    bool open = false;
    bool hasResult = false;
    std::string surfaceName;
    double length = 0.0;
    int onSurfaceCount = 0;
    double minZ = 0.0;
    double maxZ = 0.0;
    std::vector<SurfaceProfileSample> samples;
  };
  QuickProfileState quickProfile;

  /// Generated display geometry for one surface — ADR-036 (e).
  ///
  /// **Not a member of \ref CadSurface, deliberately.** `cadSurfaces` is assigned wholesale into
  /// \ref DrawingDocument and into every \ref DrawingGeometrySnapshot, so a field on the surface
  /// would be deep-copied into all 50 undo frames — the exact cost this cache exists to avoid,
  /// arriving through the one door nobody thinks to check. A parallel container keyed by stable id
  /// cannot be dragged along by those assignments.
  ///
  /// **Keyed by stable entity id, not by array index** (§11 invariant 9): `cadSurfaces` compacts on
  /// erase, so an index key would start drawing one surface's border over another's triangulation
  /// after a delete.
  struct SurfaceDisplayCacheEntry {
    std::uint64_t surfaceId = 0;

    /// The triangulation this geometry was generated from — the staleness key. A rebuild REPLACES
    /// the pointer wholesale (ADR-028 (a)), so pointer identity IS triangulation identity.
    ///
    /// `weak_ptr`, not a raw pointer, for the reason the mesh GPU cache already documents: a raw
    /// pointer key can be matched by a NEW allocation at the freed address, which would silently
    /// serve one surface's geometry for another's. An expired weak_ptr simply misses and regenerates.
    /// Not a `shared_ptr` either — that would keep a superseded 7 MB triangulation alive for as long
    /// as the cache entry lived.
    std::weak_ptr<const CadTin> builtFrom;

    /// The style this geometry was generated from — the second half of the staleness key.
    ///
    /// The **resolved style by value**, not a revision counter. ADR-036 (e) names the key
    /// "(tin pointer, style revision)"; a counter has to be bumped by every route that can change the
    /// table, and undo restore, `internal document payload` load, tab switch and DXF import are four of them, so the one
    /// that gets forgotten leaves stale contours on screen with nothing to point at. Comparing the
    /// resolved value cannot be forgotten. A style is five small structs and two doubles, so the
    /// comparison is cheaper than the allocation it prevents.
    SurfaceStyle style;
    bool reducedDisplayLod = false;

    /// All generated geometry, each in `GL_LINES` layout: flat x,y,z, six floats per segment.
    /// Regenerated together, only on a staleness miss, and borrowed by
    /// \ref CadSurfaceDisplayGeometry rather than copied into it.
    std::vector<float> triangleEdges;  ///< Display triangulation edges (sampled in Reduced LOD mode).
    std::vector<float> borderEdges;    ///< The outline: edges belonging to one triangle (\c TinBorderEdges).
    std::vector<float> minorContours;  ///< Minor levels, excluding those that are also major.
    std::vector<float> majorContours;
    struct ContourLabel {
      float x = 0.f, y = 0.f, z = 0.f;
      double level = 0.0;
    };
    std::vector<ContourLabel> contourLabels;

    /// The style asked for more contour levels than the display path will generate, so the contour
    /// buffers above are empty for a reason that has nothing to do with the style's toggles.
    ///
    /// Carried rather than left implicit because REQ-201 forbids absorbing a refusal: an interval of
    /// 0.0001 ft over a 1,000 ft surface is a million contours, and a surface that simply stopped
    /// showing any would look like a defect. The Surface Manager reads this and says so.
    bool contoursSuppressed = false;
    /// How many levels were asked for, for that message. 0 when nothing was suppressed.
    int suppressedLevelCount = 0;

    /// REQ-072 band fills, `GL_TRIANGLES` layout, one buffer per `style.bands` entry **plus one
    /// extra buffer at index `bands.size()`** for a triangle `AssignBand` placed in no band (its
    /// value fell above the table's top). That extra bucket is drawn in the surface's plain
    /// `triangles` colour rather than dropped, so a range table that does not span the surface's
    /// full elevation/slope range does not silently blank the part above it (REQ-201: reported by
    /// being visibly still there, not absorbed into nothing). Empty when `analysisMode` is `None`.
    std::vector<std::vector<float>> bandTriangleBuffers;

    /// REQ-072 slope arrows, `GL_LINES` layout (shaft + two head barbs per arrow), bucketed by
    /// `style.arrowBands` the same way \ref bandTriangleBuffers is bucketed by `style.bands` — index
    /// `arrowBands.size()` holds arrows with no matching band (the table is empty, or the arrow's
    /// grade fell above its top), drawn in the surface's own colour. Empty when `slopeArrowsOn` is
    /// false, or every triangle is flat.
    std::vector<std::vector<float>> arrowLineBuffers;
  };

  /// The display-geometry cache. Live-only: never in \ref DrawingGeometrySnapshot, never in
  /// \ref DrawingDocument, never in `internal document payload` — which is what makes REQ-070's "never stored in `internal document payload`"
  /// and "adds no entity to the drawing" structurally true rather than a rule someone must remember.
  std::vector<SurfaceDisplayCacheEntry> surfaceDisplayCache;

  /// REQ-126 / ADR-039 (c): per-surface spatial index for elevation queries. Live-only, same
  /// staleness key as the display cache (id + TIN pointer). `mutable` because SURFELEV/hover take a
  /// const state and the index is derived, not document content.
  struct SurfaceQueryCacheEntry {
    std::uint64_t surfaceId = 0;
    std::weak_ptr<const CadTin> builtFrom;
    TinSpatialIndex index;
  };
  mutable std::vector<SurfaceQueryCacheEntry> surfaceQueryCache;

  /// REQ-132…134 / ADR-039 (j): live-only drain graph and preview lines. Never in `internal document payload`.
  struct SurfaceWatershedCacheEntry {
    std::uint64_t surfaceId = 0;
    std::weak_ptr<const CadTin> builtFrom;
    WatershedResult analysis;
    std::vector<float> basinOutlines;
    std::vector<float> waterDropLines;
    std::vector<float> catchmentLines;
  };
  std::vector<SurfaceWatershedCacheEntry> surfaceWatershedCache;
  std::string waterDropSurfaceName;
  std::string catchmentSurfaceName;
  std::string swapEdgeSurfaceName;
  std::string addPointSurfaceName;
  std::string delPointSurfaceName;
  std::string movePointSurfaceName;
  std::string delLineSurfaceName;
  enum class MoveTinPointPhase : std::uint8_t { WaitFrom, WaitTo } moveTinPointPhase = MoveTinPointPhase::WaitFrom;
  double moveTinFromX = 0.0;
  double moveTinFromY = 0.0;
  std::vector<float> lastWaterDropPathXyz;
  std::vector<float> lastCatchmentPathXyz;
  /// GL_LINES preview rebuilt each display pass from the last* paths. Not tied to TIN identity, so
  /// a REQ-069 rebuild cannot erase a water-drop the user just computed (REQ-133).
  std::vector<float> waterDropPreviewLines;
  std::vector<float> catchmentPreviewLines;

  /// What the renderer is handed for surfaces this frame — batches borrowing the buffers above,
  /// filtered for layer/isolation visibility and carrying each component's resolved colour and
  /// lineweight. Rebuilt in the same pass as the cache, so a batch never outlives its buffer.
  ///
  /// Live-only for the same reason the cache is: nothing here is ever snapshotted or written.
  CadSurfaceDisplayGeometry surfaceDisplayGeometry;

  /// How many times \ref RefreshSurfaceDisplayGeometry has actually REGENERATED a surface's geometry
  /// since the process started, as opposed to taking the early-out.
  ///
  /// Exists for one reason: ADR-036 (e)'s REQ-100 obligation is to prove **the cache holds across
  /// frames**, not that a single generation is fast. A profile that timed one regeneration would pass
  /// while the defect it exists to catch — regenerating 200,000 triangles' worth of contours every
  /// frame — was fully present. BENCH reports the delta over the run: one per surface means the cache
  /// held; one per frame means it did not.
  ///
  /// Live-only and never reset by a document change, so it counts work rather than describing state.
  std::uint64_t surfaceDisplayRegenCount = 0;

  // --- HATCH command (REQ-043) ---
  /// Boundary loop traced under the cursor while HATCH is active (flat local x,y); valid drives the preview.
  std::vector<float> hatchPreviewLoop;
  bool hatchPreviewValid = false;
  /// Live appearance from the HATCH ribbon (ADR-019). color/transparency/layer apply now; angle/scale are
  /// stored for the pattern render path (Phase 3). Empty pattern = SOLID.
  float hatchColorRgb[3] = {0.78f, 0.78f, 0.78f};
  float hatchTransparency01 = 0.f;
  std::string hatchLayer;            ///< empty → current layer (MakeNewEntityAttrs default)
  float hatchAngleDeg = 0.f;
  float hatchScale = 1.f;
  std::string hatchPatternName;      ///< "" or "SOLID" = solid fill; e.g. "ANSI31" for a line pattern

  // --- EXTRACTCENTERLINE command (REQ-347) ---
  /// Session-global, ribbon-exposed fit parameters (Point Cloud contextual tab, "Centerline"
  /// section) — same session-global shape as \ref pointCloudDisplay above, and for the same reason:
  /// a real scan's pipe size and noise level vary drawing to drawing (field-tested: the shipped
  /// default of 2.0 ft/0.15 was far too loose on a real 188M-point plant scan, catching 56,238
  /// points spanning more than one member before it was hand-tuned down to 0.6 ft/0.2 — a value
  /// that will not fit every scan either).
  double extractCenterlineSearchRadiusFt = 0.6;
  double extractCenterlineMaxResidualRatio = 0.2;
  /// The current hover's cylinder-axis fit, in LOCAL storage coordinates (world = local +
  /// worldDocumentOrigin, REQ-101) — the two endpoints a click commits as a LINE. \ref
  /// extractCenterlineHoverValid is false whenever the hover found no acceptable fit (too few
  /// points nearby, or the residual gate rejected the neighborhood); a click while false is a miss,
  /// logged and left for another try, exactly like HATCH's own no-boundary-found click above.
  bool extractCenterlineHoverValid = false;
  /// Why the last hover found no fit — surfaced on a miss-click (REQ-201: refusals are named, not
  /// silent). Empty only before the first hover update.
  std::string extractCenterlineHoverDiag;
  double extractCenterlineP0X = 0, extractCenterlineP0Y = 0, extractCenterlineP0Z = 0;
  double extractCenterlineP1X = 0, extractCenterlineP1Y = 0, extractCenterlineP1Z = 0;
  /// A real scan's bounded REQ-171 preview sample can be far too sparse for hovering to work at
  /// all — a multi-hundred-million-point plant scan capped to a ~2M preview strides through each
  /// octree leaf, so BOTH finding a point near the cursor at all AND the fit neighborhood around it
  /// can come up empty even sitting squarely on a visible pipe (field-tested twice on a real
  /// 188M-point scan: first the neighborhood alone was too sparse, then — after reading the
  /// neighborhood from the real cache — locating any point near the cursor in the first place was
  /// ALSO too sparse against the preview). So both steps read the out-of-core `.gscloud` cache
  /// (ADR-060) when a cloud has one: one open handle per cloud, opened lazily.
  ///
  /// Shared with the REQ-348 PointCloud object snap (`CadSnap::FindBest`), the second concrete use
  /// that widened this from an EXTRACTCENTERLINE-only cache to a general one — same lazy-open-and-
  /// keep shape, now keyed by \ref PointCloudOpenCacheEntry::cachePath too so a re-imported cloud
  /// reusing the same index does not silently answer from a stale handle (EXTRACTCENTERLINE's
  /// command-scoped `.clear()` masked this for its own single-command lifetime; a persistent snap
  /// query has no such natural clear point).
  struct PointCloudOpenCacheEntry {
    int cloudIndex = -1;
    bool ok = false;
    std::string cachePath;
    pointcloudcache::OpenCache cache;
  };
  std::vector<PointCloudOpenCacheEntry> pointCloudOpenCaches;
  /// Neighborhood-query hysteresis: re-reads the cache only when the hover has moved past a
  /// fraction of the search radius since the last read, reusing \ref extractCenterlineLastNeighborhood
  /// otherwise — so a nearly-still cursor does not re-hit disk every frame, keeping this nowhere
  /// near the renderer's own much larger per-frame LOD reselect cost.
  bool extractCenterlineLastQueryValid = false;
  int extractCenterlineLastQueryCloudIndex = -1;
  double extractCenterlineLastQueryX = 0, extractCenterlineLastQueryY = 0, extractCenterlineLastQueryZ = 0;
  std::vector<double> extractCenterlineLastNeighborhood;
