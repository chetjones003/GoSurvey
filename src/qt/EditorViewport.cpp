// GLEW must precede Qt's OpenGL headers.
#include <GL/glew.h>
#include "EditorViewport.hpp"
#include "ViewportRenderer.hpp"
#include "CadRubberPreview.hpp"
#include "TransformPreview.hpp"
#include "MtextRichFormat.hpp"
#include "CadColor.hpp"
#include "CadCoordinateFrame.hpp"
#include "QtAnnotations.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>

EditorViewport::EditorViewport(AppCommandState& state, std::vector<std::string>& log, QWidget* parent)
    : QOpenGLWidget(parent), m_State(state), m_Log(log), m_Renderer(std::make_unique<ViewportRenderer>()) {
  setObjectName("modelViewport");
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumSize(240, 180);
  setCursor(Qt::CrossCursor);
}

EditorViewport::~EditorViewport() { Cleanup(); }

void EditorViewport::Cleanup() {
  if (!m_Ready || !context()) return;
  makeCurrent();
  m_Renderer->Shutdown();
  m_Ready = false;
  doneCurrent();
}

void EditorViewport::initializeGL() {
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    m_Error = "OpenGL initialization failed.";
    return;
  }
  while (glGetError() != GL_NO_ERROR) {}
  std::string error;
  m_Ready = m_Renderer->Init(&error);
  m_Error = QString::fromStdString(error);
  connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { Cleanup(); }, Qt::DirectConnection);
}

void EditorViewport::InvalidateGeometry() {
  ClearTransientInput();
  ++m_RenderRevision; // A different document can have the same domain revision.
  update();
}

void EditorViewport::ClearTransientInput() {
  m_Snap = {};
  m_State.viewportSnapPickValid = false;
  m_State.resolvedPointZValid = false;
}

void EditorViewport::paintGL() {
  const int w = qRound(width() * devicePixelRatioF());
  const int h = qRound(height() * devicePixelRatioF());
  if (w <= 0 || h <= 0) return;
  m_State.uiViewportWidthPx = static_cast<float>(width());
  m_State.uiViewportHeightPx = static_cast<float>(height());
  if (m_Ready) {
    const Camera camera = CadViewCamera(m_State);
    std::vector<float> rubber, preview, previewCircles, highlight, highlightCircles;
    AppendCadDraftRubberLines(m_State, m_Cursor.x, m_Cursor.y, m_State.orthoMode,
        camera.targetX, camera.targetY, camera.orthoHalfH, h, rubber);
    BuildTransformPreview(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
        &preview, &previewCircles, camera.orthoHalfH, h);
    BuildSelectionHighlight(m_State, &highlight, &highlightCircles);
    RefreshSolidDisplayGeometry(m_State);
    RefreshSurfaceDisplayGeometry(m_State);
    CadExtendedGeometryInput ext;
    ext.arcs = &m_State.userArcs; ext.arcAttrs = &m_State.userArcAttrs;
    ext.circleNormals = &m_State.userCircleNormals;
    ext.ellipses = &m_State.userEllipses; ext.ellAttrs = &m_State.userEllAttrs;
    ext.polylineVerts = &m_State.userPolylineVerts;
    ext.polylineOffsets = &m_State.userPolylineOffsets;
    ext.polylineClosed = &m_State.userPolylineClosed;
    ext.polylineAttrs = &m_State.userPolylineAttrs;
    ext.polylineBulge = &m_State.userPolylineVertsBulge;
    ext.polylineNormal = &m_State.userPolylineVertsNormal;
    ext.drawingLayers = &m_State.drawingLayerTable;
    ext.featureLineVerts = &m_State.featureLineVerts;
    ext.featureLineOffsets = &m_State.featureLineOffsets;
    ext.featureLineClosed = &m_State.featureLineClosed;
    ext.featureLineAttrs = &m_State.featureLineAttrs;
    ext.blockDefs = &m_State.blockDefs;
    ext.blockRefs = &m_State.cadBlockRefs;
    ext.blockRefAttrs = &m_State.cadBlockRefAttrs;
    RenderTuning tuning;
    tuning.bgR = tuning.bgG = tuning.bgB = 0.f;
    m_Renderer->SetSize(w, h);
    m_Renderer->RenderScene(camera, w, h, m_State.userLinesFlat, m_State.userCirclesCxCyZR,
        m_State.cadGpuRevision + m_RenderRevision, rubber, &m_Snap, 6.f,
        &preview, &previewCircles, &highlight, &highlightCircles, nullptr, nullptr, nullptr,
        &m_State.userLineAttrs, &m_State.userCircleAttrs, &ext, m_Grid, &m_State.drawingLayerTable, tuning,
        &m_State.pdfAttachments, -1, &m_State.cadFilledRegions, &m_State.cadFilledRegionAttrs,
        &m_State.cadMeshes, &m_State.cadMeshAttrs, &m_State.solidDisplayGeometry,
        &m_State.surfaceDisplayGeometry, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &m_State.cadPointClouds, &m_State.cadPointCloudAttrs, &m_State.pointCloudDisplay);
    // RenderScene uses its own FBO. Qt owns the destination FBO and can recreate it on resize.
    // RenderScene unbinds its FBO. Attach the published color texture for a GPU-only copy.
    GLuint readFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        m_Renderer->ColorTexture(), 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glDeleteFramebuffers(1, &readFbo);
  }
  QPainter painter(this);
  const Camera overlayCamera = CadViewCamera(m_State);
  DrawQtModelAnnotations(painter, m_State, static_cast<float>(width()), static_cast<float>(height()));
  painter.setPen(QColor("#a7c9d9"));
  for (const auto& point : m_State.surveyPoints) {
    float x = 0, y = 0;
    overlayCamera.WorldToScreen(point.easting, point.northing, point.elevation,
        static_cast<float>(width()), static_cast<float>(height()), &x, &y);
    if (!std::isfinite(x) || !std::isfinite(y) || !rect().contains(qRound(x), qRound(y))) continue;
    painter.drawLine(QPointF(x - 4, y - 4), QPointF(x + 4, y + 4));
    painter.drawLine(QPointF(x - 4, y + 4), QPointF(x + 4, y - 4));
    if (m_State.surveyPointShowIdInViewport) painter.drawText(QPointF(x + 7, y - 5), QString::number(point.id));
  }
  if (m_State.selBoxWaitingSecond) {
    const bool window = m_Last.x() >= m_State.selBoxAnchorScreenX;
    const QColor color = window ? QColor(70, 140, 220) : QColor(65, 190, 125);
    painter.setPen(QPen(color, 1, window ? Qt::SolidLine : Qt::DashLine));
    painter.setBrush(QColor(color.red(), color.green(), color.blue(), 35));
    painter.drawRect(QRectF(QPointF(m_State.selBoxAnchorScreenX, m_State.selBoxAnchorScreenY), m_Last).normalized());
  }
  painter.setPen(QColor("#bcc6d3"));
  painter.drawText(16, 26, "[Model]  [Orthographic]");
  if (!m_Ready) painter.drawText(rect(), Qt::AlignCenter, m_Error);
  painter.setPen(QColor("#7593ad"));
  painter.drawText(18, height() - 20, "WCS");
  const QPointF c(width() - 80, 76);
  const QPolygonF top({c + QPointF(0, -34), c + QPointF(42, -12), c + QPointF(0, 10), c + QPointF(-42, -12)});
  const QPolygonF front({c + QPointF(-42, -12), c + QPointF(0, 10), c + QPointF(0, 56), c + QPointF(-42, 32)});
  const QPolygonF right({c + QPointF(0, 10), c + QPointF(42, -12), c + QPointF(42, 32), c + QPointF(0, 56)});
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QColor("#8ca5ba"));
  for (const auto& face : {top, front, right}) {
    painter.setBrush(face.containsPoint(m_Last, Qt::OddEvenFill) ? QColor("#4e7d9e") : QColor("#384753"));
    painter.drawPolygon(face);
  }
  painter.setPen(QColor("#e1e9ef"));
  painter.drawText(QRectF(c.x() - 30, c.y() - 23, 60, 24), Qt::AlignCenter, "TOP");
  painter.drawText(QRectF(c.x() - 42, c.y() + 13, 42, 24), Qt::AlignCenter, "FRONT");
  painter.drawText(QRectF(c.x(), c.y() + 13, 42, 24), Qt::AlignCenter, "RIGHT");
}

bool EditorViewport::WorldPoint(QPointF point, ray3d::Vec3& world) const {
  const auto ray = CadViewCamera(m_State).ScreenRay(static_cast<float>(point.x()),
      static_cast<float>(point.y()), static_cast<float>(width()), static_cast<float>(height()));
  return ray3d::RayPlaneIntersect(ray, CadActiveWorkPlane(m_State), &world);
}

bool EditorViewport::ViewPoint(QPointF point, ray3d::Vec3& world) const {
  const auto camera = CadViewCamera(m_State);
  const auto ray = camera.ScreenRay(static_cast<float>(point.x()), static_cast<float>(point.y()),
      static_cast<float>(width()), static_cast<float>(height()));
  return ray3d::RayPlaneIntersect(ray, {{camera.targetX, camera.targetY, camera.targetZ}, camera.ForwardWorld()}, &world);
}

void EditorViewport::mousePressEvent(QMouseEvent* event) {
  setFocus();
  m_Last = event->position();
  if (event->button() == Qt::LeftButton) {
    const QPointF c(width() - 80, 76);
    const QPolygonF top({c + QPointF(0, -34), c + QPointF(42, -12), c + QPointF(0, 10), c + QPointF(-42, -12)});
    const QPolygonF front({c + QPointF(-42, -12), c + QPointF(0, 10), c + QPointF(0, 56), c + QPointF(-42, 32)});
    const QPolygonF right({c + QPointF(0, 10), c + QPointF(42, -12), c + QPointF(42, 32), c + QPointF(0, 56)});
    if (top.containsPoint(m_Last, Qt::OddEvenFill)) { SetOrientation(0, 90); return; }
    if (front.containsPoint(m_Last, Qt::OddEvenFill)) { SetOrientation(0, 0); return; }
    if (right.containsPoint(m_Last, Qt::OddEvenFill)) { SetOrientation(90, 0); return; }
  }
  if (event->button() == Qt::RightButton) {
    char empty[1]{};
    ProcessCommandLineSubmit(empty, 1, m_State, m_Log);
  } else if (event->button() == Qt::LeftButton && (WorldPoint(m_Last, m_Cursor) ||
             (m_State.active == AppCommandState::Kind::None && ViewPoint(m_Last, m_Cursor)))) {
    const Camera camera = CadViewCamera(m_State);
    auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    m_Snap = m_State.active != AppCommandState::Kind::None ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State,
        true, camera.orthoHalfH * 16.f / std::max(1, height()), {}, CadViewIsPlan(m_State) ? nullptr : &ray) : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    m_State.viewportSnapPickValid = m_Snap.valid;
    m_State.viewportSnapPickLocalX = m_Cursor.x;
    m_State.viewportSnapPickLocalY = m_Cursor.y;
    m_State.viewportSnapPickLocalZ = m_Cursor.z;
    m_State.resolvedPointZValid = true;
    m_State.resolvedPointZ = static_cast<float>(m_Cursor.z);
    m_State.uiCursorWorldX = static_cast<float>(CadCoord::WorldXFromLocal(m_State, m_Cursor.x));
    m_State.uiCursorWorldY = static_cast<float>(CadCoord::WorldYFromLocal(m_State, m_Cursor.y));
    m_State.uiCursorWorldZ = static_cast<float>(m_Cursor.z);
    if (m_State.showCreatePointsWindow && m_State.active == AppCommandState::Kind::None) {
      int hit = -1;
      double nearest = 100.0;
      for (std::size_t i = 0; i < m_State.surveyPoints.size(); ++i) {
        const auto& point = m_State.surveyPoints[i];
        float x = 0, y = 0;
        camera.WorldToScreen(point.easting, point.northing, point.elevation,
            static_cast<float>(width()), static_cast<float>(height()), &x, &y);
        const double distance = std::pow(x - m_Last.x(), 2) + std::pow(y - m_Last.y(), 2);
        if (std::isfinite(distance) && distance < nearest) { nearest = distance; hit = static_cast<int>(i); }
      }
      if (hit >= 0) {
        ClearCadSelection(m_State);
        ApplySurveyPointClickSelection(m_State, hit, event->modifiers() & Qt::ShiftModifier, &m_Log);
        for (int index : m_State.selectedSurveyPointIndices) SyncSurveyPointLinkedMtextSelection(m_State, index);
      } else {
        const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
        const bool readOnly = std::any_of(m_State.openProjects.begin(), m_State.openProjects.end(),
            [uid](const auto& project) { return project.uid == uid && project.readOnly; });
        if (readOnly) m_Log.push_back("CREATEPOINTS: the project is read-only; no point placed.");
        else TryPlaceSurveyPoint(m_State, m_Cursor.x, m_Cursor.y, m_State.createPointsOpts.defaultElevation, m_Log);
      }
    } else if (m_State.active != AppCommandState::Kind::None || m_State.selBoxWaitingSecond) {
      SubmitViewportPick(m_State, m_Cursor.x, m_Cursor.y, m_Log, false,
          m_Last.x() >= m_State.selBoxAnchorScreenX, CadViewIsPlan(m_State) ? nullptr : &ray);
    } else {
      SelectedEntity picked;
      float distance = 0.f;
      if (!(event->modifiers() & Qt::ShiftModifier)) ClearSelection(m_State);
      if (PickClosestCadEntity(m_State, m_Cursor.x, m_Cursor.y,
          camera.orthoHalfH * 16.f / std::max(1, height()), &picked, &distance, CadViewIsPlan(m_State) ? nullptr : &ray)) {
        const auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(),
            [&](const SelectedEntity& entry) { return entry.type == picked.type && entry.index == picked.index; });
        if (it == m_State.selection.end()) m_State.selection.push_back(picked);
        else m_State.selection.erase(it);
      } else BeginSelectionBoxCorner(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
          static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()));
    }
  }
  if (changed) changed();
  update();
}

void EditorViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPointF delta = event->position() - m_Last;
  if (event->buttons() & Qt::MiddleButton) {
    if (event->modifiers() & Qt::ShiftModifier) {
      m_State.viewportAzimuthDeg += static_cast<float>(delta.x()) * 0.4f;
      m_State.viewportElevationDeg = std::clamp(m_State.viewportElevationDeg + static_cast<float>(delta.y()) * 0.4f, -89.9f, 90.f);
    } else {
      ray3d::Vec3 before, after;
      if (ViewPoint(m_Last, before) && ViewPoint(event->position(), after)) {
        m_State.viewportPanX += before.x - after.x;
        m_State.viewportPanY += before.y - after.y;
        m_State.viewportPanZ += before.z - after.z;
      }
    }
  }
  m_Last = event->position();
  ClearTransientInput();
  if (WorldPoint(m_Last, m_Cursor)) {
    const Camera camera = CadViewCamera(m_State);
    auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    m_Snap = m_State.active != AppCommandState::Kind::None ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State,
        true, camera.orthoHalfH * 16.f / std::max(1, height()), {}, CadViewIsPlan(m_State) ? nullptr : &ray) : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    m_State.uiCursorWorldX = static_cast<float>(CadCoord::WorldXFromLocal(m_State, m_Cursor.x));
    m_State.uiCursorWorldY = static_cast<float>(CadCoord::WorldYFromLocal(m_State, m_Cursor.y));
    m_State.uiCursorWorldZ = static_cast<float>(m_Cursor.z);
    if (m_State.selBoxWaitingSecond) UpdateSelectionBoxPreview(m_State, static_cast<float>(m_Cursor.x),
        static_cast<float>(m_Cursor.y), m_Last.x() >= m_State.selBoxAnchorScreenX,
        CadViewIsPlan(m_State) ? nullptr : &camera, static_cast<float>(width()), static_cast<float>(height()));
    if (cursorChanged) cursorChanged(m_Cursor.x, m_Cursor.y, m_Cursor.z);
  }
  update();
}

void EditorViewport::mouseReleaseEvent(QMouseEvent* event) { QOpenGLWidget::mouseReleaseEvent(event); }

void EditorViewport::wheelEvent(QWheelEvent* event) {
  ray3d::Vec3 before, after;
  const bool anchor = ViewPoint(event->position(), before);
  m_State.viewportZoom = std::clamp(m_State.viewportZoom * static_cast<float>(
      std::pow(1.15, event->angleDelta().y() / 120.0)), 1.e-8f, 1.e8f);
  if (anchor && ViewPoint(event->position(), after)) {
    m_State.viewportPanX += before.x - after.x;
    m_State.viewportPanY += before.y - after.y;
    m_State.viewportPanZ += before.z - after.z;
  }
  update();
  event->accept();
}

void EditorViewport::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) { m_State.showCreatePointsWindow = false; CancelActiveCommand(m_State, m_Log); ClearTransientInput(); }
  else if (event->key() == Qt::Key_Delete) ExecuteDeleteSelection(m_State, m_Log);
  else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Space) {
    char empty[1]{};
    ProcessCommandLineSubmit(empty, 1, m_State, m_Log);
  } else if (!event->text().isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) && textEntered) {
    textEntered(event->text()); event->accept(); return;
  } else { QOpenGLWidget::keyPressEvent(event); return; }
  if (changed) changed();
  update();
}

void EditorViewport::SetOrientation(float azimuth, float elevation) {
  m_State.viewportAzimuthDeg = azimuth;
  m_State.viewportElevationDeg = elevation;
  m_State.viewportRollDeg = 0.f;
  update();
}
