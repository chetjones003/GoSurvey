#pragma once

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QTimer>
#include <functional>
#include <memory>
#include "CadCommands.hpp"
#include "CadOnlineMap.hpp"
#include "CadSnap.hpp"

class ViewportRenderer;

class EditorViewport final : public QOpenGLWidget {
public:
  EditorViewport(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  ~EditorViewport() override;
  std::function<void()> changed;
  std::function<void()> thumbnailReady;
  std::function<void(double, double, double)> cursorChanged;
  std::function<void(const QString&)> textEntered;
  /// The right-click verdict (REQ-084) is "open the shortcut menu"; the window builds the menu.
  std::function<void(const QPoint& globalPosition)> contextMenuRequested;
  void SetOrientation(float azimuth, float elevation);
  void SetGrid(bool enabled) { m_Grid = enabled; update(); }
  void InvalidateGeometry();
  bool IsRendererReady() const { return m_Ready; }
protected:
  void initializeGL() override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
private:
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
  QTimer m_MapTimer;
  std::size_t m_LastMapLogSize = 0;
  int m_LastCaptureGathered = -1;
  int m_LastCaptureTotal = -1;
  QPointF m_Last;
  ray3d::Vec3 m_Cursor;
  CadSnap::Hit m_Snap;
  bool m_Ready = false;
  bool m_Grid = true;
  std::uint32_t m_RenderRevision = 0;
  QString m_Error;
  /// Time-sensitive right-click (REQ-084 (b)): a press is decided by how long the button is held.
  bool m_RightPending = false;
  QElapsedTimer m_RightHeld;
  QTimer m_RightTimer;
  QPoint m_RightGlobal;
};
