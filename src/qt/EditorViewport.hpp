#pragma once

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QPoint>
#include <QPointF>
#include <QTimer>
#include <functional>
#include <memory>
#include "CadCommands.hpp"
#include "CadOnlineMap.hpp"
#include "CadSnap.hpp"
#include "ViewCube.hpp"
#include "EditorPipePalette.hpp"

class ViewportRenderer;
class QPainter;
class QLabel;

class EditorViewport final : public QOpenGLWidget {
  friend class QtEditorTests;
public:
  EditorViewport(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  ~EditorViewport() override;
  std::function<void()> changed;
  std::function<void()> thumbnailReady;
  /// The Pipe Fittings palette's part pictures changed (REQ-350 (e)); called from inside a paint.
  std::function<void()> partThumbnailsChanged;
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
  bool ViewPoint(QPointF point, ray3d::Vec3& world) const;
  void ClearTransientInput();
  void RightPress(const QPoint& globalPosition);
  void PerformRightClick(RightClickAction action, const QPoint& globalPosition);
  void RightClickAsEnter();
  AppCommandState& m_State;
  std::vector<std::string>& m_Log;
  std::unique_ptr<ViewportRenderer> m_Renderer;
  std::unique_ptr<OnlineMapController> m_OnlineMap;
  PartThumbnailImages m_PartThumbs;
  QTimer m_MapTimer;
  QTimer m_ViewAnimTimer;
  QElapsedTimer m_ViewAnimClock;
  bool m_CubePressed = false;  ///< The press in progress was consumed by the ViewCube.
  std::size_t m_LastMapLogSize = 0;
  int m_LastCaptureGathered = -1;
  int m_LastCaptureTotal = -1;
  QPointF m_Last;
  ray3d::Vec3 m_Cursor;
  CadSnap::Hit m_Snap;
  bool m_Ready = false;
  bool m_ResizePreview = false;
  QLabel* m_ResizePreviewOverlay = nullptr;
  QSize m_ResizePreviewMinimumSize;
  QSize m_ResizePreviewMaximumSize;
  bool m_PointerInside = false;
  bool m_Grid = true;
  bool m_LeftDragSelection = false;
  bool m_HasSelectionBoxSnapshot = false;
  std::vector<SelectedEntity> m_SelectionBeforeBox;
  std::vector<int> m_SurveySelectionBeforeBox;
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
