#pragma once

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QPoint>
#include <QPointF>
#include <QTimer>
#include <functional>
#include <memory>
#include <unordered_map>
#include "CadCommands.hpp"
#include "CadOnlineMap.hpp"
#include "CadSnap.hpp"
#include "ViewCube.hpp"
#include "EditorPipePalette.hpp"

class ViewportRenderer;
class QPainter;
class QPainterPath;
class QLabel;
class QOpenGLBuffer;
class QOpenGLShaderProgram;
class QOpenGLTextureBlitter;
class QOpenGLVertexArrayObject;

class EditorViewport final : public QOpenGLWidget {
  friend class QtEditorTests;
public:
  EditorViewport(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  ~EditorViewport() override;
  std::function<void()> changed;
  std::function<void()> thumbnailReady;
  /// The Pipe Fittings palette's part pictures changed (REQ-350 (e)); called from inside a paint.
  std::function<void()> partThumbnailsChanged;
  /// Called after one live grading optimization sample has been drawn into the viewport.
  std::function<void(std::uint64_t)> gradingPreviewFrameDrawn;
  /// Part pictures rendered for the Pipe Fittings palette, by block name.
  const PartThumbnailImages& PartThumbnails() const { return m_PartThumbs; }
  std::function<void(double, double, double)> cursorChanged;
  std::function<void(const QString&)> textEntered;
  /// The right-click verdict (REQ-084) is "open the shortcut menu"; the window builds the menu.
  std::function<void(const QPoint& globalPosition)> contextMenuRequested;
  std::function<void(const QPoint& globalPosition)> snapMenuRequested;
  std::function<bool()> submitLockedInput;
  void SetOrientation(float azimuth, float elevation);
  /// Refreshes the native cursor after command state changes outside a mouse event.
  void RefreshCommandCursor();
  /// Eases the view to an orientation over a fraction of a second (REQ-059); SetOrientation snaps.
  void AnimateToOrientation(float azimuth, float elevation);
  /// A press at 'point' on the ViewCube (face, arrow or home) sets the view it stands for.
  void ClickViewCube(QPointF point);
  void SetGrid(bool enabled) { m_Grid = enabled; update(); }
  void InvalidateGeometry();
  bool BeginGradingOptimizationPreview(std::uint64_t surfaceId,
                                       std::vector<float> sourceLines,
                                       std::uint64_t sequence, double offset);
  bool UpdateGradingOptimizationPreview(std::uint64_t sequence, double offset);
  bool GradingOptimizationPreviewActive() const { return m_GradingPreviewActive; }
  std::uint64_t GradingPreviewInvalidationSerial() const { return m_GradingPreviewInvalidationSerial; }
  bool GradingOptimizationPreviewCanPaint() const {
    if (!m_Ready || (m_State.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(m_State)))
      return false;
    if (m_GradingPreviewSurfaceId == 0) return true;
    const int surfaceIndex = FindSurfaceIndexById(m_State, m_GradingPreviewSurfaceId);
    return surfaceIndex >= 0 && SurfaceVisible(m_State, static_cast<std::size_t>(surfaceIndex));
  }
  void ClearGradingOptimizationPreview();
  void CancelSelectionBox();
  bool CancelGripEdit();
  bool IsRendererReady() const { return m_Ready; }
  void BeginWindowResizePreview();
  void EndWindowResizePreview();
  void UpdateWindowResizePreviewGeometry();
  QPoint CursorPosition() const { return m_Last.toPoint(); }
protected:
  void initializeGL() override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
private:
  void DrawCursor(QPainter& painter);
  viewcube::Scene CubeScene() const;
  void StartViewAnimationClock();
  void StepViewAnimation();
  void Cleanup();
  bool WorldPoint(QPointF point, ray3d::Vec3& world) const;
  bool DrawPaperViewportClipMask(const QPainterPath& path, int framebufferWidth, int framebufferHeight);
  bool ViewPoint(QPointF point, ray3d::Vec3& world) const;
  void CommitPendingIdleSelectionClick(Qt::KeyboardModifiers modifiers);
  void ClearTransientInput();
  void RightPress(const QPoint& globalPosition);
  void PerformRightClick(RightClickAction action, const QPoint& globalPosition);
  void RightClickAsEnter();
  AppCommandState& m_State;
  std::vector<std::string>& m_Log;
  std::unique_ptr<ViewportRenderer> m_Renderer;
  std::unique_ptr<QOpenGLBuffer> m_ClipMaskBuffer;
  std::unique_ptr<QOpenGLShaderProgram> m_ClipMaskProgram;
  std::unique_ptr<QOpenGLTextureBlitter> m_TextureBlitter;
  std::unique_ptr<QOpenGLVertexArrayObject> m_ClipMaskVao;
  std::unique_ptr<OnlineMapController> m_OnlineMap;
  struct RasterDrapeCache {
    std::uint64_t imageId = 0;
    unsigned int texture = 0;
    std::vector<double> xyuv;
  };
  std::unordered_map<std::string, unsigned int> m_RasterTextures;
  std::vector<RasterDrapeCache> m_RasterDrapeCache;
  std::vector<MapTileDraw> m_RenderTiles;
  std::vector<float> m_GradingPreviewGhostLines;
  CadSurfaceDisplayGeometry m_GradingPreviewOtherSurfaces;
  std::vector<std::vector<float>> m_GradingPreviewOtherLineStorage;
  std::vector<std::vector<float>> m_GradingPreviewOtherBandStorage;
  double m_GradingPreviewOffset = 0.0;
  std::uint64_t m_GradingPreviewSequence = 0;
  std::uint64_t m_GradingPreviewPaintedSequence = 0;
  std::uint64_t m_GradingPreviewSurfaceId = 0;
  std::uint64_t m_GradingPreviewToken = 0;
  std::uint64_t m_GradingPreviewTokenSerial = 0;
  std::uint64_t m_GradingPreviewInvalidationSerial = 0;
  bool m_GradingPreviewActive = false;
  std::uint32_t m_RasterDrapeRevision = 0;
  std::uint32_t m_RasterDrapeRenderRevision = 0;
  PartThumbnailImages m_PartThumbs;
  QTimer m_MapTimer;
  QTimer m_ViewAnimTimer;
  QTimer m_GripMenuTimer;
  QElapsedTimer m_ViewAnimClock;
  bool m_CubePressed = false;  ///< The press in progress was consumed by the ViewCube.
  std::size_t m_LastMapLogSize = 0;
  int m_LastCaptureGathered = -1;
  int m_LastCaptureTotal = -1;
  QPointF m_Last;
  ray3d::Vec3 m_Cursor;
  CadSnap::Hit m_Snap;
  bool m_Ready = false;
  bool m_CleanupComplete = false;
  bool m_ResizePreview = false;
  QLabel* m_ResizePreviewOverlay = nullptr;
  QSize m_ResizePreviewMinimumSize;
  QSize m_ResizePreviewMaximumSize;
  bool m_PointerInside = false;
  bool m_Grid = true;
  bool m_LeftDragSelection = false;
  bool m_PendingIdleSelectionClick = false;
  bool m_IdleBoxFirstClickPending = false;
  bool m_HasSelectionBoxSnapshot = false;
  std::vector<SelectedEntity> m_SelectionBeforeBox;
  std::vector<int> m_SurveySelectionBeforeBox;
  std::vector<SelectedEntity> m_SelectionBeforePendingClick;
  std::vector<int> m_SurveySelectionBeforePendingClick;
  std::vector<SelectedSubObject> m_SubObjectSelectionBeforePendingClick;
  ViewportPickResult m_PendingIdlePick{};
  bool m_GripMovedWithButton = false;
  QPointF m_SelectionPress;
  std::uint32_t m_RenderRevision = 0;
  QString m_Error;
  /// Time-sensitive right-click (REQ-084 (b)): a press is decided by how long the button is held.
  bool m_RightPending = false;
  QElapsedTimer m_RightHeld;
  QTimer m_RightTimer;
  QPoint m_RightGlobal;
};
