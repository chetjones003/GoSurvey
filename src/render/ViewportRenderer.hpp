#pragma once

#include "CadCommands.hpp"
#include "CadSnap.hpp"
#include "PdfAttach.hpp"

#include <cstdint>
#include <memory>
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
                   const std::vector<float>& userLines, const std::vector<float>& circlesCxCyZR,
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
                   const ucs::Ucs* gridFrame = nullptr);

  [[nodiscard]] unsigned int ColorTexture() const { return colorTex_; }

  /// REQ-308 — write the last-rendered viewport image as a 24-bit BMP, downscaled so its longer
  /// side is at most \p maxDim. Best-effort: returns false and writes nothing on any failure.
  /// Call right after RenderScene for the drawing being pictured.
  [[nodiscard]] bool CaptureThumbnailBmp(const char* pathUtf8, int maxDim) const;

private:
  bool EnsureFramebuffer(int w, int h);
  void DestroyFramebuffer();
  bool EnsureMultisamplePass(int w, int h);
  void DestroyMultisamplePass();
  bool EnsureShader();
  void DestroyShader();
  static void Ortho(float left, float right, float bottom, float top, float nearp, float farp,
                    float* outColMajor);

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
