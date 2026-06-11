#pragma once

#include "CadCommands.hpp"
#include "CadSnap.hpp"
#include "PdfAttach.hpp"

#include <cstdint>
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
};

class ViewportRenderer {
public:
  bool Init();
  void Shutdown();

  void SetSize(int width, int height);

  /// \param circlesCxCyR (cx, cy, r) triplets; drawn as line loops in the XY plane.
  /// \param rubberLines GL_LINES vertex data (x,y,z pairs of endpoints) for transient previews.
  /// \param snapOverlay Active object snap glyph (green); nullptr or invalid — skip.
  /// \param snapGlyphHalfPx Screen-space half-extent (pixels) for snap glyph geometry (see Settings → Object snap).
  /// \param selectionFillRect axis-aligned window in world XY: minX, maxX, minY, maxY; nullptr skips.
  /// \param previewLines / previewCircles transient geometry (same layout as user geometry).
  /// \param highlightLines / highlightCircles selected entities redrawn on top (accent stroke).
  /// \param cadGpuRevision from AppCommandState — bumps invalidate GPU caches for committed geometry.
  /// \param lineEntityAttrs / circleEntityAttrs parallel to segments/circles; nullptr uses fixed defaults.
  /// \param extended Optional arcs / ellipses / polylines (same shader batch as lines).
  /// \param showGrid draws the minor grid in model space (toggle from UI).
  void RenderScene(double panX, double panY, float zoom, int fbWidth, int fbHeight,
                   const std::vector<float>& userLines, const std::vector<float>& circlesCxCyR,
                   std::uint32_t cadGpuRevision, const std::vector<float>& rubberLines,
                   const CadSnap::Hit* snapOverlay, float snapGlyphHalfPx, const float* selectionFillRect,
                   const std::vector<float>* previewLines,
                   const std::vector<float>* previewCircles, const std::vector<float>* highlightLines,
                   const std::vector<float>* highlightCircles, const std::vector<float>* hoverLines,
                   const std::vector<float>* hoverCircles, const std::vector<float>* surveyMarkers,
                   const std::vector<EntityAttributes>* lineEntityAttrs,
                   const std::vector<EntityAttributes>* circleEntityAttrs,
                   const CadExtendedGeometryInput* extended, bool showGrid,
                   const std::vector<CadLayerRow>* drawingLayers, const RenderTuning& tuning = RenderTuning{},
                   const std::vector<PdfAttachment>* pdfAttachments = nullptr);

  [[nodiscard]] unsigned int ColorTexture() const { return colorTex_; }

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
