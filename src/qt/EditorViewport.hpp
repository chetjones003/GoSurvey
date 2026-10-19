#pragma once

#include <QOpenGLWidget>
#include <QPointF>
#include <functional>
#include <memory>
#include "CadCommands.hpp"
#include "CadSnap.hpp"

class ViewportRenderer;

class EditorViewport final : public QOpenGLWidget {
public:
  EditorViewport(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  ~EditorViewport() override;
  std::function<void()> changed;
  std::function<void(double, double, double)> cursorChanged;
  std::function<void(const QString&)> textEntered;
  std::function<void()> selectionListRequested;
  void SetOrientation(float azimuth, float elevation);
  void SetGrid(bool enabled) { m_Grid = enabled; update(); }
  void InvalidateGeometry();
  void CancelSelectionBox();
  bool IsRendererReady() const { return m_Ready; }
protected:
  void initializeGL() override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
private:
  void Cleanup();
  bool WorldPoint(QPointF point, ray3d::Vec3& world) const;
  bool ViewPoint(QPointF point, ray3d::Vec3& world) const;
  void ClearTransientInput();
  AppCommandState& m_State;
  std::vector<std::string>& m_Log;
  std::unique_ptr<ViewportRenderer> m_Renderer;
  QPointF m_Last;
  ray3d::Vec3 m_Cursor;
  CadSnap::Hit m_Snap;
  bool m_Ready = false;
  bool m_Grid = true;
  bool m_LeftDragSelection = false;
  bool m_HasSelectionBoxSnapshot = false;
  std::vector<SelectedEntity> m_SelectionBeforeBox;
  std::vector<int> m_SurveySelectionBeforeBox;
  bool m_GripMovedWithButton = false;
  QPointF m_SelectionPress;
  std::uint32_t m_RenderRevision = 0;
  QString m_Error;
};
