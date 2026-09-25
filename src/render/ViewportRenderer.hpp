#pragma once

#include "CadCommands.hpp"
#include "CadSnap.hpp"
#include "PdfAttach.hpp"
#include "gizmooverlay.hpp"
#include "render/SectionClip.hpp"
#include "util/pointcloudcache.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

/// Render-time tuning sourced from Settings → Display / System (AutoCAD Options analog).
/// Defaults match prior unconditional behavior so call sites that omit this stay backward-compatible.
struct RenderTuning {
  int arcCircleSmoothnessCap = 512; ///< Display → Display resolution: max segments per full circle (VIEWRES).
  bool hardwareAcceleration = true; ///< System → Hardware Acceleration: when off, MSAA path is skipped.
  bool smoothLineDisplay = true;    ///< Graphics Performance → Smooth line display: GL_LINE_SMOOTH + MSAA.
  float bgR = 0.f;                  ///< Display → Window Elements: viewport background (clear) color. Default black preserves prior behavior.
  float bgG = 0.f;
  float bgB = 0.f;
  /// REQ-064. Defaulting to Wireframe2D is what keeps every existing call site — and the pixel
  /// output it produces — unchanged: that style takes the same depth-off path as before.
  VisualStyle visualStyle = VisualStyle::Wireframe2D;
  /// REQ-341 / ADR-058 — the live section clip. Stated in WORLD coordinates; the renderer rebases
  /// it onto the view anchor, because the anchor is the renderer's own float-precision device and
  /// no caller should have to know it exists. Default-inactive, so every existing call site renders
  /// exactly what it rendered before.
  ///
  /// It lives in `RenderTuning` rather than becoming parameter 31 for the reason this struct was
  /// created: `RenderScene`'s signature is already 30 long.
  SectionClipPlane sectionClip{};
  /// REQ-341 — the rectangle drawn to SHOW where \ref sectionClip cuts. Built by the caller, which
  /// is the side that knows how big the drawing is; the renderer only draws it. Invalid means draw
  /// nothing, which is what every existing call site gets by default.
  SectionClipIndicator sectionClipIndicator{};
  /// REQ-342 — the hatch and section line that make that rectangle findable at a glance. Built from
  /// the indicator by `SectionPlaneGraphicsFor`, on the same side and for the same reason. Invalid
  /// means the plane draws as REQ-341 shipped it, fill and outline only.
  SectionPlaneGraphics sectionPlaneGraphics{};
  /// REQ-343 — the handles on a SELECTED section plane. Invalid when it is not selected, which is
  /// the only "should these be drawn?" test there is.
  SectionPlaneGrips sectionPlaneGrips{};
  /// Which handle is under the cursor, and which is being dragged (`SectionPlaneGrip`, -1 for
  /// none). Both are drawn brighter and larger, so the handle that lights up is the handle that
  /// grabs — the rule REQ-318's sub-object hover already follows.
  int sectionPlaneGripHover = -1;
  int sectionPlaneGripDrag = -1;
};

class ViewportRenderer {
public:
  bool Init();
  void Shutdown();

  void SetSize(int width, int height);

  /// \param circlesCxCyZR (cx, cy, r) triplets; drawn as line loops in the XY plane.
  /// \param rubberLines GL_LINES vertex data (x,y,z pairs of endpoints) for transient previews.
  /// \param snapOverlay Active object snap glyph (green); nullptr or invalid — skip.
  /// \param snapGlyphHalfPx Screen-space half-extent (pixels) for snap glyph geometry (see Settings → Object snap).
  /// \param previewLines / previewCircles transient geometry (same layout as user geometry).
  /// \param highlightLines / highlightCircles selected entities redrawn on top (accent stroke).
  /// \param cadGpuRevision from AppCommandState — bumps invalidate GPU caches for committed geometry.
  /// \param lineEntityAttrs / circleEntityAttrs parallel to segments/circles; nullptr uses fixed defaults.
  /// \param extended Optional arcs / ellipses / polylines (same shader batch as lines).
  /// \param showGrid draws the minor grid in model space (toggle from UI).
  /// \param cam The model camera (REQ-058 / ADR-025 (c)). Replaces the former panX/panY/zoom
  ///            triple — the target IS the pan and \c orthoHalfH IS the zoom, so this is a net
  ///            parameter reduction rather than an addition to an already long signature.
  ///            A plan-view camera reproduces the pre-3D pipeline exactly.
  void RenderScene(const Camera& cam, int fbWidth, int fbHeight,
                   const std::vector<double>& userLines, const std::vector<double>& circlesCxCyZR,
                   std::uint32_t cadGpuRevision, const std::vector<float>& rubberLines,
                   const CadSnap::Hit* snapOverlay, float snapGlyphHalfPx,
                   const std::vector<float>* previewLines,
                   const std::vector<float>* previewCircles, const std::vector<float>* highlightLines,
                   const std::vector<float>* highlightCircles, const std::vector<float>* hoverLines,
                   const std::vector<float>* hoverCircles, const std::vector<float>* surveyMarkers,
                   const std::vector<EntityAttributes>* lineEntityAttrs,
                   const std::vector<EntityAttributes>* circleEntityAttrs,
                   const CadExtendedGeometryInput* extended, bool showGrid,
                   const std::vector<CadLayerRow>* drawingLayers, const RenderTuning& tuning = RenderTuning{},
                   const std::vector<PdfAttachment>* pdfAttachments = nullptr,
                   // When >= 0, the view is in paper space; GL geometry is skipped and the paper-space
                   // sheet/viewports are drawn by the ImGui overlay (CadUi::DrawDrawingViewport).
                   int activeSpaceIndex = -1,
                   // Solid-filled regions (ADR-011) drawn under the linework via stencil even-odd fill.
                   const std::vector<CadFilledRegion>* filledRegions = nullptr,
                   const std::vector<EntityAttributes>* filledRegionAttrs = nullptr,
                   // Imported meshes (REQ-063). Shaded style fills them; the wireframe styles draw
                   // nothing for them — a triangle soup rendered as edges is unreadable, and there
                   // is no "mesh wireframe" behaviour any requirement asks for.
                   const std::vector<std::shared_ptr<const CadMesh>>* meshes = nullptr,
                   const std::vector<EntityAttributes>* meshAttrs = nullptr,
                   // B-rep solids (REQ-313 / ADR-045), as caller-assembled batches: the cached
                   // tessellation for the faces and the solid's real edges for the wireframe, both
                   // already filtered for layer visibility and isolation with colours resolved.
                   //
                   // Solids draw in EVERY visual style — the opposite of the mesh rule above, and
                   // for the reason ADR-026 (c) records: a solid HAS edges, where a mesh's "edges"
                   // are artefacts of an exporter's resolution. In Hidden the faces are written to
                   // the depth buffer only, which is what makes "Hidden" mean anything for a solid
                   // rather than being wireframe with extra steps.
                   const CadSolidDisplayGeometry* solidGeometry = nullptr,
                   // Generated surface display geometry (REQ-068 / REQ-070, ADR-036 (h)) — the
                   // triangle edges, contours and border each visible surface's style asks for, as
                   // coloured batches of flat world-space line vertices (x,y,z per endpoint, two
                   // endpoints per segment, exactly like \p surveyMarkers).
                   //
                   // Lines rather than shaded faces, and drawn in EVERY visual style: a TIN's
                   // triangles and contours are the thing a surveyor reads, and the default style is
                   // 2D Wireframe — a surface visible only in Shaded would be invisible in the view
                   // users spend most of their time in. This is the opposite of the mesh rule above
                   // for the opposite reason: a mesh is an imported solid, a TIN is a network.
                   //
                   // Caller-side, regenerated only when a surface's triangulation or its style
                   // changes, already filtered for layer visibility and isolation (REQ-068,
                   // REQ-084 (d)), and with every component's colour and lineweight already resolved
                   // — so the renderer draws what it is given and decides nothing.
                   const CadSurfaceDisplayGeometry* surfaceGeometry = nullptr,
                   // REQ-073 amendment's Volume Dashboard cut/fill map (TASK-095 §6 step 5) — a
                   // separate struct from surfaceGeometry above, see VolumeMapDisplayGeometry's own
                   // comment for why. Drawn over the band fills, under the wireframe, same reasoning
                   // as the bands: an opaque comparison overlay reads better under the linework than
                   // over it.
                   const VolumeMapDisplayGeometry* volumeMap = nullptr,
                   // REQ-103 BREAK (TASK-101) — material a pending edit is about to REMOVE, and the
                   // markers bounding it. Its own channel rather than part of \p previewLines
                   // because it is the one preview drawn ON TOP of the object it describes: the
                   // transform batch is translucent at ordinary line width, which reads correctly
                   // for a ghost of geometry somewhere it is not yet and reads as nearly nothing
                   // when washed over a full-opacity line underneath. Drawn opaque, in a warning
                   // colour, at highlight width — "this is what disappears" has to be unmistakable.
                   const std::vector<float>* removalLines = nullptr,
                   const std::vector<float>* removalMarkers = nullptr,
                   // The active UCS, for the grid (REQ-154). The grid is a drafting aid, and a
                   // drafting aid that stays squared to the world while entry, ORTHO and the
                   // crosshair have all moved to a rotated frame is actively misleading — it reads
                   // as the drawing's alignment.
                   //
                   // Null means the WCS and takes the original world-XY code path unchanged, which
                   // is what every drawing that never touches UCS continues to get.
                   const ucs::Ucs* gridFrame = nullptr,
                   // REQ-318 items 11 and 14 — the tinted fills over a selected solid FACE and over
                   // the one a `Ctrl` click WOULD take. Storage coordinates, already filtered and
                   // resolved by the caller like every other overlay here.
                   //
                   // Its own channel, and the only overlays in this signature that are DEPTH-TESTED.
                   // The blanket rule a few hundred lines down — "overlays are UI, never occluded:
                   // a selection highlight that hides behind the object it is highlighting is a
                   // bug" — was written for 2D linework and gives the wrong answer for one face of
                   // a closed volume: never-occluded, a selected back face glows through the body.
                   // The sub-object selection's EDGE and VERTEX linework keeps the ordinary
                   // never-occluded treatment and arrives through \p highlightLines, because a line
                   // one pixel wide sunk into the surface it lies on is invisible (D-2026-09-04-a).
                   //
                   // In 2D Wireframe there is no depth buffer content to be occluded by — solids
                   // draw no faces there — so the tint simply draws, which is the only way a face
                   // selection can be shown in the default style.
                   const CadSubObjectOverlay* subObjectOverlay = nullptr,
                   // The translate gizmo's handles (REQ-060, GitHub issue #148 Phase 5 slice 4b).
                   //
                   // Its own channel because it is the only overlay here that is not one colour:
                   // X red, Y green, Z blue is what every 3D application already draws, and a widget
                   // whose three handles shared a colour would have to be read rather than seen.
                   const CadGizmoOverlay* gizmoOverlay = nullptr,
                   // Point clouds (REQ-171/172, ADR-042/060). Appended at the very end, unlike
                   // every other pointer above, because this call is entirely POSITIONAL at its one
                   // call site (main.cpp) — inserting anywhere but the end would silently reassign
                   // every argument after it to the wrong parameter. Drawn in EVERY visual style —
                   // a point cloud has no faces to shade or hide, so "Shaded only" would make it
                   // invisible in the default 2D Wireframe view, the same reasoning REQ-068 gives
                   // for TIN surfaces. No LOD yet (flat draw of every resident point): REQ-100
                   // profile (e) has not been measured, and implementation-rules.md §5 is explicit
                   // that unmeasured optimisation is not added speculatively.
                   const std::vector<std::shared_ptr<const CadPointCloud>>* pointClouds = nullptr,
                   const std::vector<EntityAttributes>* pointCloudAttrs = nullptr,
                   // REQ-171 (part 14): session-global point size / LOD target / colour scheme.
                   // Pointer so a caller that can't easily reach it degrades to the old hardcoded
                   // defaults rather than failing to compile or crashing.
                   const PointCloudDisplaySettings* pointCloudDisplay = nullptr);

  [[nodiscard]] unsigned int ColorTexture() const { return colorTex_; }

  /// REQ-308 — write the last-rendered viewport image as a 24-bit BMP, downscaled so its longer
  /// side is at most \p maxDim. Best-effort: returns false and writes nothing on any failure.
  /// Call right after RenderScene for the drawing being pictured.
  [[nodiscard]] bool CaptureThumbnailBmp(const char* pathUtf8, int maxDim) const;

  /// One library part's geometry, ready to be drawn as a palette thumbnail (ADR-062 / REQ-350).
  ///
  /// Plain float arrays rather than a domain type, because §7 of the architecture spec asks the
  /// render API to take submitted data: the renderer should not know what a "fitting" is, only how
  /// to light some triangles and draw some lines. Coordinates are the part's own local/storage
  /// coordinates — the thumbnail centres and fits them itself, so no view anchor applies.
  struct PartThumbnailInput {
    const std::vector<float>* triVerts = nullptr;    ///< `GL_TRIANGLES`, nine floats per triangle.
    const std::vector<float>* triNormals = nullptr;  ///< one unit normal per vertex, parallel to triVerts.
    const std::vector<float>* edgeVerts = nullptr;   ///< `GL_LINES`, six floats per segment.
    /// Connection ports as points: seven floats each — x, y, z, r, g, b, a. Coloured per port by the
    /// caller (the role colours the BEDIT gizmo uses), because the renderer does not know what a
    /// role is.
    const std::vector<float>* portMarkers = nullptr;
    float rgba[4] = {0.78f, 0.80f, 0.84f, 1.f};  ///< the part's body colour.
  };

  /// Render \p in as a \p px by \p px shaded thumbnail and cache it under \p key, or do nothing (and
  /// return true) when \p key is already cached at that size — a part is drawn ONCE, which is what
  /// keeps REQ-350's palette inside REQ-100's frame budget. Returns false when there is nothing to
  /// draw or a GL object could not be created; the caller then shows the row without a picture
  /// rather than failing.
  ///
  /// Must be called at a point in the frame where binding another framebuffer is safe — i.e. AFTER
  /// RenderScene, the same constraint (and the same reason) CaptureThumbnailBmp has.
  bool EnsurePartThumbnail(const std::string& key, const PartThumbnailInput& in, int px);

  /// The cached texture for \p key, or 0 when nothing is cached — which is the palette's cue to draw
  /// a placeholder. Opaque to the caller: no GL call is needed to use it beyond handing it to ImGui.
  [[nodiscard]] unsigned int PartThumbnailTexture(const std::string& key) const;

  /// Drop \p key's cached thumbnail, so the next request re-renders it. Called when a block
  /// definition is edited (ADR-062 (c) — the one way a part's geometry changes in a session).
  void InvalidatePartThumbnail(const std::string& key);

private:
  bool EnsureFramebuffer(int w, int h);
  void DestroyFramebuffer();
  bool EnsureMultisamplePass(int w, int h);
  void DestroyMultisamplePass();
  bool EnsureShader();
  void DestroyShader();
  static void Ortho(float left, float right, float bottom, float top, float nearp, float farp,
                    float* outColMajor);

  /// ADR-062 — one cached part thumbnail. Each owns its OWN framebuffer and colour texture, rather
  /// than sharing one small FBO and copying out: `ImGui::Image` records a texture id and samples it
  /// after every UI call has run, so a shared target would make every row in the palette display
  /// whichever part was rendered last.
  struct PartThumbnailEntry {
    std::string key;
    unsigned int fbo = 0;
    unsigned int tex = 0;
    unsigned int depthRbo = 0;
    int px = 0;
    /// Monotonic use stamp, for evicting the least recently asked-for entry when the cache is full.
    std::uint64_t stamp = 0;
  };
  void ReleasePartThumbnails();
  void DestroyPartThumbnailEntry(PartThumbnailEntry& e);

  std::vector<PartThumbnailEntry> partThumbs_;
  std::uint64_t partThumbStamp_ = 0;
  /// Scratch VAO/VBO shared by every thumbnail render. Deliberately NOT the scene's `vaoLines_` /
  /// `vboShaded_`: those hold committed geometry whose re-upload is gated on `cadGpuRevision`, so
  /// streaming a thumbnail through them would blank the drawing until an unrelated edit bumped it.
  unsigned int partThumbVao_ = 0;
  unsigned int partThumbVbo_ = 0;

  unsigned int fbo_ = 0;
  unsigned int colorTex_ = 0;
  unsigned int rbo_ = 0;
  int fbW_ = 0;
  int fbH_ = 0;

  /// Multisampled pass → blit to \p colorTex_ (reduces aliasing / "sparkle" when zoomed).
  unsigned int msFbo_ = 0;
  unsigned int msColorRbo_ = 0;
  unsigned int msDepthRbo_ = 0;
  int msFbW_ = 0;
  int msFbH_ = 0;
  bool msaaAvailable_ = false;

  unsigned int lineProgram_ = 0;
  unsigned int vcLineProgram_ = 0;
  /// `uClipPlane` in each clipped program (REQ-341), looked up once at link rather than every frame.
  int clipLocLine_ = -1;
  int clipLocVcLine_ = -1;
  int clipLocShaded_ = -1;
  int clipLocTex_ = -1;
  /// Diffuse-lit triangles for the Shaded style (REQ-064). Its own VAO because its vertex layout is
  /// position + normal, unlike every other program here.
  unsigned int shadedProgram_ = 0;
  unsigned int vaoShaded_ = 0;
  unsigned int vboShaded_ = 0;
  std::vector<float> cpuShadedTris_;  ///< x,y,z,nx,ny,nz per vertex; scratch for the filled-region quad.

  /// One mesh's GPU residency (REQ-063). Meshes are **immutable** (ADR-026 (c)), so the buffers
  /// only ever need rebuilding when the view ANCHOR drifts far enough to cost float precision —
  /// never because the geometry changed. That is what makes an indexed, uploaded-once draw correct
  /// here, where the linework cache has to also watch a revision counter.
  struct MeshGpuEntry {
    std::weak_ptr<const CadMesh> mesh;  ///< identity AND liveness: an expired entry is evicted.
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    double anchorX = 0.0;  ///< view anchor the vertex positions are relative to.
    double anchorY = 0.0;
    int indexCount = 0;
  };
  std::vector<MeshGpuEntry> meshGpu_;
  void ReleaseMeshGpu();

  /// One point cloud's GPU residency (REQ-171/172, ADR-060). Immutable payload (architecture
  /// §11.5), so the same "upload once, re-upload only when the view anchor drifts" rule as
  /// \ref MeshGpuEntry applies. No index buffer: points have no connectivity, so \ref vbo alone
  /// is drawn with `glDrawArrays(GL_POINTS, ...)`.
  ///
  /// Reuses \ref vcLineProgram_ rather than a new shader — its vertex-colour line program already
  /// takes the exact layout a point needs (position + RGBA, with REQ-341 clip-plane support built
  /// in), just fed `GL_POINTS` instead of `GL_LINES`. A cloud with no source colour gets a uniform
  /// grey written into every vertex at upload time, rather than a second shader variant.
  struct PointCloudGpuEntry {
    std::weak_ptr<const CadPointCloud> cloud;  ///< identity AND liveness, same as MeshGpuEntry::mesh.
    unsigned int vao = 0;
    unsigned int vbo = 0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    int pointCount = 0;

    /// Out-of-core LOD detail (`.gscloud`, ADR-060): a bounded set of leaves near the camera focus,
    /// paged in at full density on top of the flat preview above. Opened lazily, once, from
    /// `CadPointCloud::cloudCachePath`; `diskCacheTried` distinguishes "not attempted yet" from "open
    /// failed" so a missing/corrupt cache is not retried every frame.
    bool diskCacheTried = false;
    pointcloudcache::OpenCache diskCache;

    struct LeafGpuEntry {
      std::int32_t leafNodeIndex = -1;
      unsigned int vao = 0;
      unsigned int vbo = 0;
      int pointCount = 0;
    };
    std::vector<LeafGpuEntry> leafGpu;  ///< currently resident LOD leaves, keyed by leafNodeIndex.
    /// The leaf indices the CURRENT selection wants (TASK-270 §14 part 17) — kept separately from
    /// `leafGpu` because a request can be in flight for a leaf that isn't resident yet, and a
    /// background result can land for a leaf that was evicted while it was in flight (dropped, not
    /// an error).
    std::unordered_set<std::int32_t> wantedLeaves;

    /// One leaf disk read as the background worker below produced it — RAW (undecimated) point
    /// data, since decimation depends on \ref lodStride, which can change again before this result
    /// is drained; deferring it to drain time means a read that was already in flight never needs
    /// re-issuing just because the stride changed underneath it.
    struct LeafReadResult {
      std::int32_t leafNodeIndex = -1;
      bool ok = false;
      std::vector<double> xyz;
      std::vector<float> colors;
      std::vector<float> intensity;
    };
    /// A leaf's `pointcloudcache::ReadLeafPoints` call, moved off the render thread (TASK-270 §14
    /// part 17). Before this existed, a leaf not yet resident was read synchronously inside
    /// `RenderScene` — real, measured disk-stall frames (`BENCH POINTCLOUD`: 42/900 frames, up to
    /// 1.1s, on the user's real 7.8GB file) that p95 alone did not catch. One worker per point
    /// cloud, started lazily alongside `diskCache`; requests and results are exchanged through a
    /// mutex-guarded queue, the same "worker computes, render thread applies" split every other
    /// background job in this codebase uses (`PointCloudImportAsync`, `SurfaceRebuildAsync`).
    struct LeafPrefetch {
      std::thread thread;
      std::mutex mutex;
      std::condition_variable cv;
      std::deque<std::int32_t> wanted;              ///< guarded by mutex; worker consumes FIFO.
      std::unordered_set<std::int32_t> inFlight;    ///< guarded by mutex; requested-not-yet-drained.
      std::vector<LeafReadResult> completed;         ///< guarded by mutex; render thread drains.
      std::atomic<bool> stop{false};
      /// The worker's own copy of the cache metadata (octree + cachePath) — small, read-only once
      /// set, so no locking is needed to share it with the worker thread. `ReadLeafPoints` opens its
      /// own file handle per call, so two threads reading the same `.gscloud` concurrently (a
      /// request racing an anchor-stale main-thread path, if one still existed) would be safe too,
      /// though this design never does that — every leaf read now goes through this one worker.
      pointcloudcache::OpenCache cache;

      void Run() {
        for (;;) {
          std::int32_t idx = -1;
          {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return stop.load(std::memory_order_acquire) || !wanted.empty(); });
            if (wanted.empty()) {
              if (stop.load(std::memory_order_acquire)) return;
              continue;
            }
            idx = wanted.front();
            wanted.pop_front();
          }
          LeafReadResult r;
          r.leafNodeIndex = idx;
          r.ok = pointcloudcache::ReadLeafPoints(cache, idx, r.xyz, r.colors, r.intensity);
          {
            std::lock_guard<std::mutex> lock(mutex);
            inFlight.erase(idx);
            completed.push_back(std::move(r));
          }
        }
      }
      /// No-op if `idx` is already in flight — the render thread calls this every frame a leaf is
      /// still wanted, not just once, so the de-dupe has to live here rather than at every call site.
      void Request(std::int32_t idx) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!inFlight.insert(idx).second) return;
        wanted.push_back(idx);
        cv.notify_one();
      }
      std::vector<LeafReadResult> Drain() {
        std::vector<LeafReadResult> out;
        std::lock_guard<std::mutex> lock(mutex);
        out.swap(completed);
        return out;
      }
      /// Same shape as `PointCloudImportAsync`'s destructor: without this, destroying a job whose
      /// thread is still running calls `std::terminate` (closing the app, or the cloud's GPU entry
      /// being evicted, mid-read would abort the process instead of exiting/continuing cleanly).
      ~LeafPrefetch() {
        stop.store(true, std::memory_order_release);
        cv.notify_all();
        if (thread.joinable()) thread.join();
      }
    };
    std::unique_ptr<LeafPrefetch> prefetch;
    /// The uniform decimation stride every resident leaf was uploaded at (1 = every point). All
    /// resident leaves share one stride so the LOD reads as evenly sparse across its whole covered
    /// area, never a "these leaves are full density, those are empty" cliff at the coverage edge —
    /// see TASK-270 part 10. 0 = nothing uploaded yet.
    int lodStride = 0;

    /// The colour scheme the CURRENTLY UPLOADED vertex data (both the preview buffer and every
    /// resident LOD leaf) was baked with. Neither the preview's `anchorStale` gate nor the LOD
    /// leaves' camera-movement hysteresis (`lodSelectionValid` below) has anything to do with the
    /// user flipping the ribbon's colour-scheme combo — without this, changing Solid/Elevation/
    /// Intensity would sit invisible until the camera happened to move far enough to force a
    /// rebuild anyway (TASK-270 part 14 bugfix).
    PointCloudColorScheme lastColorScheme = PointCloudColorScheme::Rgb;
    bool lastColorSchemeValid = false;

    /// The camera state the CURRENT `leafGpu` set/stride was selected for (TASK-270 part 11) — NOT
    /// re-evaluated every frame. Re-running the cylinder query and, on a stride/leaf-set change,
    /// re-reading and re-uploading up to `kMaxLodLeafCandidates` leaves from disk is real I/O cost;
    /// doing it every frame during a smooth orbit/zoom (the view direction and pan target both
    /// change a little EVERY frame) turned a bounded, occasional cost into a per-frame one, which is
    /// what read as "noticeably laggier." The selection is re-run only once the camera has moved far
    /// enough from this snapshot to plausibly want different leaves.
    bool lodSelectionValid = false;
    double lodFocusX = 0.0, lodFocusY = 0.0, lodFocusZ = 0.0;
    double lodDirX = 0.0, lodDirY = 0.0, lodDirZ = 1.0;
    double lodLateralRadius = 0.0;
    /// Wall-clock throttle on top of the movement thresholds above (TASK-270 part 13): a fast orbit
    /// drag crosses the movement thresholds on nearly every frame, and a reselect itself takes real
    /// time (up to `kMaxLodLeafCandidates` disk reads) — without a time floor, a slower frame makes
    /// the SAME mouse speed cross the threshold sooner (more camera delta per frame), which costs
    /// more time, which makes the next frame slower still. A minimum real-time interval between
    /// reselects bounds the cost per second regardless of frame rate or how fast the user orbits,
    /// which is what breaks that feedback loop.
    std::chrono::steady_clock::time_point lodLastReselectTime{};
    /// The LOD point-count target the CURRENT `leafGpu`/`lodStride` selection was computed against
    /// (TASK-270 part 14 bugfix). `needsReselect` below is purely camera-movement hysteresis, so
    /// without tracking this separately, dragging the ribbon's LOD Target slider would sit inert
    /// until the camera happened to move enough to trigger a reselect anyway.
    std::int64_t lastLodTargetPoints = 0;
  };
  std::vector<PointCloudGpuEntry> pointCloudGpu_;
  void ReleasePointCloudGpu();
  std::vector<float> cpuPointCloudVerts_;  ///< x,y,z,r,g,b,a per point; scratch, reused across clouds.

  /// REQ-100 profile (e) instrument (TASK-270 §14). Originally counted synchronous
  /// `pointcloudcache::ReadLeafPoints` calls made directly on the render thread — a real stall,
  /// confirmed by `BENCH POINTCLOUD` against the user's own 7.8GB file (42/900 frames, up to 1.1s).
  /// Every leaf read now goes through `PointCloudGpuEntry::LeafPrefetch` (part 17) instead, so this
  /// stays 0 in normal operation; kept as the bench's stall signal so a regression that somehow
  /// reintroduced a synchronous read on this thread would still be caught, not left silently unmet.
  std::uint64_t pointCloudLeafDiskReadsThisFrame_ = 0;

 public:
  std::uint64_t PointCloudLeafDiskReadsThisFrame() const { return pointCloudLeafDiskReadsThisFrame_; }
  /// Current resident bytes across every LOD leaf of every point cloud (the out-of-core node
  /// cache's footprint) — REQ-100 profile (e)'s "peak resident node-cache memory" is the running
  /// max of this, sampled by the bench harness once per timed frame.
  std::size_t PointCloudResidentLeafBytes() const {
    constexpr std::size_t kBytesPerVertex = 7 * sizeof(float);  // pos(3) + rgba(4), same as upload
    std::size_t total = 0;
    for (const PointCloudGpuEntry& e : pointCloudGpu_)
      for (const PointCloudGpuEntry::LeafGpuEntry& leaf : e.leafGpu)
        total += static_cast<std::size_t>(leaf.pointCount) * kBytesPerVertex;
    return total;
  }

 private:

  /// One coalesced solid batch's GPU residency (REQ-313 / GitHub issue #194). Unlike a mesh, a solid
  /// batch has no stable pointer identity — `RefreshSolidDisplayGeometry` rebuilds the batch list
  /// whenever a solid, its appearance or its visibility changes — so the whole set is keyed on the
  /// assembly signature (\ref solidGpuSig_) rather than per-entry. Within one signature the batches
  /// are immutable, so the vertex buffers only re-upload when the view ANCHOR drifts far enough to
  /// cost float precision, exactly as the mesh path does — which is what keeps a 400-solid orbit off
  /// the per-frame CPU-transform + stream-upload path that missed REQ-100 profile (d).
  struct SolidGpuBatch {
    unsigned int faceVao = 0;
    unsigned int faceVbo = 0;
    unsigned int edgeVao = 0;
    unsigned int edgeVbo = 0;
    int faceVertCount = 0;
    int edgeVertCount = 0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    float rgba[4] = {1.f, 1.f, 1.f, 1.f};
    float lineweightMm = -1.f;
  };
  std::vector<SolidGpuBatch> solidGpu_;
  std::uint64_t solidGpuSig_ = 0;  ///< assembly signature solidGpu_ was built from; 0 = not built
  void ReleaseSolidGpu();
  unsigned int vaoLines_ = 0;
  unsigned int vboLines_ = 0;

  unsigned int vaoVcLines_ = 0;
  unsigned int vboVcLines_ = 0;
  unsigned int vaoVcCircles_ = 0;
  unsigned int vboVcCircles_ = 0;

  std::vector<float> cpuVcLines_;
  std::vector<float> cpuVcCircles_;
  struct VcLineBatch {
    int first = 0;
    int count = 0;
    float widthPx = 1.35f;
  };
  std::vector<VcLineBatch> vcLineBatches_;
  std::vector<VcLineBatch> vcCircleBatches_;
  std::uint32_t cachedCadGpuRevision_ = 0xffffffffu;
  double cachedViewAnchorX_ = 0.;
  double cachedViewAnchorY_ = 0.;
  double cachedHalfHd_ = -1.;
  int cachedFbHeight_ = -1;

  unsigned int gridProgram_ = 0;
  unsigned int vaoGrid_ = 0;
  unsigned int vboGrid_ = 0;
  int gridVertexCount_ = 0;

  // Textured-quad program for PDF underlays
  unsigned int texProgram_ = 0;
  unsigned int vaoTex_     = 0;
  unsigned int vboTex_     = 0;
};
