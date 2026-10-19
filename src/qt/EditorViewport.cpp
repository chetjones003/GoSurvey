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
#include <QMenu>
#include <QIcon>
#include <QOpenGLContext>
#include <QPainter>
#include <QPolygonF>
#include <QPixmap>
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

void EditorViewport::CancelSelectionBox() {
  if (m_HasSelectionBoxSnapshot) {
    m_State.selection = m_SelectionBeforeBox;
    m_State.selectedSurveyPointIndices = m_SurveySelectionBeforeBox;
  }
  m_HasSelectionBoxSnapshot = false;
  m_SelectionBeforeBox.clear();
  m_SurveySelectionBeforeBox.clear();
  m_State.selBoxWaitingSecond = false;
  m_State.selBoxPreview.clear();
  m_LeftDragSelection = false;
  if (changed) changed();
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
    CadSubObjectOverlay subObjectOverlay;
    std::vector<float> subObjectLines;
    BuildSubObjectHighlight(m_State, &subObjectOverlay.selectedFaceTris,
        &subObjectOverlay.selectedFaceEdges, &subObjectLines);
    highlight.insert(highlight.end(), subObjectLines.begin(), subObjectLines.end());
    std::vector<float> subHoverLines;
    BuildSubObjectHoverHighlight(m_State, &subObjectOverlay.hoverFaceTris,
        &subObjectOverlay.hoverFaceEdges, &subHoverLines);
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
    ext.hiddenEntityIds = &m_State.hiddenEntityIds;
    ext.blockDefs = &m_State.blockDefs;
    ext.blockRefs = &m_State.cadBlockRefs;
    ext.blockRefAttrs = &m_State.cadBlockRefAttrs;
    ext.annotationScales = &m_State.annotationScales;
    ext.currentAnnotationScaleIndex = m_State.currentAnnotationScaleIndex;
    RenderTuning tuning;
    tuning.bgR = tuning.bgG = tuning.bgB = 0.f;
    m_Renderer->SetSize(w, h);
    m_Renderer->RenderScene(camera, w, h, m_State.userLinesFlat, m_State.userCirclesCxCyZR,
        m_State.cadGpuRevision + m_RenderRevision, rubber, &m_Snap, 6.f,
        &preview, &previewCircles, &highlight, &highlightCircles, &subHoverLines, nullptr, nullptr,
        &m_State.userLineAttrs, &m_State.userCircleAttrs, &ext, m_Grid, &m_State.drawingLayerTable, tuning,
        &m_State.pdfAttachments, -1, &m_State.cadFilledRegions, &m_State.cadFilledRegionAttrs,
        &m_State.cadMeshes, &m_State.cadMeshAttrs, &m_State.solidDisplayGeometry,
        &m_State.surfaceDisplayGeometry, nullptr, nullptr, nullptr, nullptr, &subObjectOverlay, nullptr,
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
  const Camera gripCamera = CadViewCamera(m_State);
  for (const SelectedEntity& selected : m_State.selection) {
    std::vector<ray3d::Vec3> grips;
    switch (selected.type) {
      case SelectedEntity::Type::LineSeg: {
        const std::size_t k = static_cast<std::size_t>(selected.index) * 6;
        if (selected.index >= 0 && k + 5 < m_State.userLinesFlat.size()) {
          grips.push_back({m_State.userLinesFlat[k], m_State.userLinesFlat[k + 1], m_State.userLinesFlat[k + 2]});
          grips.push_back({m_State.userLinesFlat[k + 3], m_State.userLinesFlat[k + 4], m_State.userLinesFlat[k + 5]});
        }
        break;
      }
      case SelectedEntity::Type::Circle: {
        const std::size_t k = static_cast<std::size_t>(selected.index) * 4;
        if (selected.index >= 0 && k + 3 < m_State.userCirclesCxCyZR.size()) {
          const auto& c = m_State.userCirclesCxCyZR;
          grips.push_back({c[k], c[k + 1], c[k + 2]});
          grips.push_back({c[k] + c[k + 3], c[k + 1], c[k + 2]});
        }
        break;
      }
      case SelectedEntity::Type::Polyline: {
        if (selected.index >= 0 && static_cast<std::size_t>(selected.index + 1) < m_State.userPolylineOffsets.size()) {
          const int first = m_State.userPolylineOffsets[static_cast<std::size_t>(selected.index)];
          const int last = m_State.userPolylineOffsets[static_cast<std::size_t>(selected.index + 1)];
          for (int vertex = first; vertex < last; ++vertex) {
            const std::size_t k = static_cast<std::size_t>(vertex) * 3;
            if (k + 2 < m_State.userPolylineVerts.size())
              grips.push_back({m_State.userPolylineVerts[k], m_State.userPolylineVerts[k + 1], m_State.userPolylineVerts[k + 2]});
          }
          CadForEachPolylineArcMidGrip(m_State, selected.index,
              [&](int, float x, float y, float z) { grips.push_back({x, y, z}); });
        }
        break;
      }
      case SelectedEntity::Type::Arc: {
        if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.userArcs.size()) {
          const CadArc& arc = m_State.userArcs[static_cast<std::size_t>(selected.index)];
          const double end = arc.startRad + arc.sweepRad;
          grips.push_back({arc.cx, arc.cy, arc.z});
          grips.push_back({arc.cx + arc.r * std::cos(arc.startRad), arc.cy + arc.r * std::sin(arc.startRad), arc.z});
          grips.push_back({arc.cx + arc.r * std::cos(end), arc.cy + arc.r * std::sin(end), arc.z});
        }
        break;
      }
      case SelectedEntity::Type::Ellipse: {
        if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.userEllipses.size()) {
          const CadEllipse& ellipse = m_State.userEllipses[static_cast<std::size_t>(selected.index)];
          if (EllipseHasGrips(ellipse)) {
            ray3d::Vec3 points[3]; EllipseGripPoints(ellipse, points);
            grips.assign(std::begin(points), std::end(points));
          }
        }
        break;
      }
      case SelectedEntity::Type::BlockRef: {
        if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.cadBlockRefs.size()) {
          const auto& ref = m_State.cadBlockRefs[static_cast<std::size_t>(selected.index)];
          const int defIndex = CadBlockFindDef(m_State.blockDefs, ref.defName);
          if (defIndex >= 0) {
            const auto& def = m_State.blockDefs[static_cast<std::size_t>(defIndex)];
            for (int grip = 0; grip < CadBlockDynGripCount(def); ++grip) {
              float x = 0.f, y = 0.f, z = 0.f;
              if (CadBlockDynGripShownOnInsert(grip) && CadBlockDynGripWorld(def, ref, grip, &x, &y, &z))
                grips.push_back({x, y, z});
            }
          }
        }
        break;
      }
      case SelectedEntity::Type::Annotation: {
        if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.cadAnnotations.size()) {
          const CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(selected.index)];
          if (ann.kind == CadAnnotation::Kind::Mtext || ann.kind == CadAnnotation::Kind::Table) {
            grips = {{ann.boxMinX, ann.boxMinY, 0}, {ann.boxMaxX, ann.boxMinY, 0},
                     {ann.boxMaxX, ann.boxMaxY, 0}, {ann.boxMinX, ann.boxMaxY, 0}};
            if (ann.surveyPointLabelForId >= 0) grips.push_back({ann.insX, ann.insY, 0});
          } else if (ann.kind == CadAnnotation::Kind::DimAligned || ann.kind == CadAnnotation::Kind::DimLinear) {
            float sx1=0, sy1=0, sx2=0, sy2=0, tx=0, ty=0, nx=0, ny=0, length=0;
            if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &length)) {
              const float offset = ann.dimSignedOffset;
              grips = {{ann.dimExt1X, ann.dimExt1Y, 0}, {ann.dimExt2X, ann.dimExt2Y, 0},
                       {sx1 + nx * offset, sy1 + ny * offset, 0}, {sx2 + nx * offset, sy2 + ny * offset, 0},
                       {ann.insX, ann.insY, 0}};
            }
          }
        }
        break;
      }
      default: break;
    }
    for (std::size_t i = 0; i < grips.size(); ++i) {
      float x = 0.f, y = 0.f;
      gripCamera.WorldToScreen(grips[i].x, grips[i].y, grips[i].z,
          static_cast<float>(width()), static_cast<float>(height()), &x, &y);
      if (!std::isfinite(x) || !std::isfinite(y)) continue;
      const bool entityHot = m_State.entityGripMoveActive && m_State.entityGripType == selected.type &&
          m_State.entityGripEntityIndex == selected.index && m_State.entityGripWhich == static_cast<int>(i);
      const bool dimHot = selected.type == SelectedEntity::Type::Annotation && m_State.dimGripMoveActive &&
          m_State.dimGripAnnotationIndex == selected.index && m_State.dimGripWhich == static_cast<int>(i);
      const bool mtextHot = selected.type == SelectedEntity::Type::Annotation && m_State.mtextGripMoveActive &&
          m_State.mtextGripAnnotationIndex == selected.index && m_State.mtextGripCorner == static_cast<int>(i);
      const bool hot = entityHot || dimHot || mtextHot;
      painter.setPen(QPen(Qt::white, 1));
      painter.setBrush(hot ? QColor("#f57842") : QColor("#3b82f6"));
      painter.drawRect(QRectF(x - 4, y - 4, 8, 8));
    }
  }
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
  if (m_State.multiSelectionEnabled && m_State.viewportPickAmbiguous && m_State.active == AppCommandState::Kind::None) {
    painter.setPen(QPen(QColor("#f0c75e"), 1.5));
    const QPointF p = m_Last;
    painter.drawRect(QRectF(p.x() + 5, p.y() - 13, 10, 10));
    painter.drawRect(QRectF(p.x() + 9, p.y() - 9, 10, 10));
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
  const auto projectReadOnly = [this] {
    const auto uid = m_State.activeDrawingIdx >= 0 && static_cast<std::size_t>(m_State.activeDrawingIdx) < m_State.drawingTabs.size()
        ? m_State.drawingTabs[static_cast<std::size_t>(m_State.activeDrawingIdx)].projectUid : 0u;
    return uid != 0 && std::any_of(m_State.openProjects.begin(), m_State.openProjects.end(),
        [uid](const auto& project) { return project.uid == uid && project.readOnly; });
  };
  if (event->button() == Qt::LeftButton &&
      (m_State.entityGripMoveActive || m_State.dimGripMoveActive || m_State.mtextGripMoveActive)) {
    ClearEntityGripInteraction(m_State); ClearDimGripInteraction(m_State); ClearMtextGripInteraction(m_State);
    m_GripMovedWithButton = false;
    if (changed) changed(); update(); return;
  }
  if (event->button() == Qt::LeftButton) {
    const QPointF c(width() - 80, 76);
    const QPolygonF top({c + QPointF(0, -34), c + QPointF(42, -12), c + QPointF(0, 10), c + QPointF(-42, -12)});
    const QPolygonF front({c + QPointF(-42, -12), c + QPointF(0, 10), c + QPointF(0, 56), c + QPointF(-42, 32)});
    const QPolygonF right({c + QPointF(0, 10), c + QPointF(42, -12), c + QPointF(42, 32), c + QPointF(0, 56)});
    if (top.containsPoint(m_Last, Qt::OddEvenFill)) { SetOrientation(0, 90); return; }
    if (front.containsPoint(m_Last, Qt::OddEvenFill)) { SetOrientation(0, 0); return; }
    if (right.containsPoint(m_Last, Qt::OddEvenFill)) { SetOrientation(90, 0); return; }
  }
  if (event->button() == Qt::RightButton && m_State.entityGripMoveActive) {
    RestoreEntityGripOriginal(m_State);
    ClearEntityGripInteraction(m_State);
    BumpCadGpuCache(m_State);
  } else if (event->button() == Qt::RightButton && m_State.mtextGripMoveActive) {
    AbortMtextGripInteraction(m_State); BumpCadGpuCache(m_State);
  } else if (event->button() == Qt::RightButton && m_State.dimGripMoveActive) {
    if (m_State.dimGripAnnotationIndex >= 0 && static_cast<std::size_t>(m_State.dimGripAnnotationIndex) < m_State.cadAnnotations.size()) {
      CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(m_State.dimGripAnnotationIndex)];
      ann.dimExt1X = m_State.dimGripOrigExt1X; ann.dimExt1Y = m_State.dimGripOrigExt1Y;
      ann.dimExt2X = m_State.dimGripOrigExt2X; ann.dimExt2Y = m_State.dimGripOrigExt2Y;
      ann.dimSignedOffset = m_State.dimGripOrigSignedOffset; ann.insX = m_State.dimGripOrigInsX; ann.insY = m_State.dimGripOrigInsY;
      CadDimRefreshMeasurementText(&ann, m_State.displayLinearPrecision, CadAngleDisplaySettings(m_State));
    }
    ClearDimGripInteraction(m_State); BumpCadGpuCache(m_State);
  } else if (event->button() == Qt::RightButton) {
    if (m_State.selBoxWaitingSecond) {
      CancelSelectionBox();
      if (changed) changed(); update(); return;
    }
    if (m_State.active == AppCommandState::Kind::None &&
        (!m_State.selection.empty() || !m_State.selectedSurveyPointIndices.empty()) && selectionListRequested) {
      QMenu menu(this);
      QAction* selectionAction = menu.addAction("Selection…");
      if (menu.exec(event->globalPosition().toPoint()) == selectionAction) selectionListRequested();
      return;
    }
    char empty[1]{};
    ProcessCommandLineSubmit(empty, 1, m_State, m_Log);
  } else if (event->button() == Qt::LeftButton && (WorldPoint(m_Last, m_Cursor) ||
             (m_State.active == AppCommandState::Kind::None && ViewPoint(m_Last, m_Cursor)))) {
    const Camera camera = CadViewCamera(m_State);
    auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    const bool gripActive = m_State.entityGripMoveActive || m_State.dimGripMoveActive || m_State.mtextGripMoveActive;
    CadSnap::SnapExclude exclude{};
    if (m_State.entityGripMoveActive && m_State.entityGripEntityIndex >= 0) {
      exclude.valid = true; exclude.type = m_State.entityGripType; exclude.index = m_State.entityGripEntityIndex;
    }
    m_Snap = (m_State.active != AppCommandState::Kind::None || gripActive) ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State,
        true, camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), exclude, CadViewIsPlan(m_State) ? nullptr : &ray) : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    else if (m_State.entityGripMoveActive) {
      float orthoX = static_cast<float>(m_Cursor.x), orthoY = static_cast<float>(m_Cursor.y);
      ApplyOrthoConstrainFromAnchor(m_State, m_State.entityGripAnchorX, m_State.entityGripAnchorY,
          &orthoX, &orthoY, m_State.orthoMode);
      m_Cursor.x = orthoX; m_Cursor.y = orthoY;
    }
    m_State.viewportSnapPickValid = m_Snap.valid;
    m_State.viewportSnapPickLocalX = m_Cursor.x;
    m_State.viewportSnapPickLocalY = m_Cursor.y;
    m_State.viewportSnapPickLocalZ = m_Cursor.z;
    m_State.resolvedPointZValid = true;
    m_State.resolvedPointZ = static_cast<float>(m_Cursor.z);
    m_State.uiCursorWorldX = static_cast<float>(CadCoord::WorldXFromLocal(m_State, m_Cursor.x));
    m_State.uiCursorWorldY = static_cast<float>(CadCoord::WorldYFromLocal(m_State, m_Cursor.y));
    m_State.uiCursorWorldZ = static_cast<float>(m_Cursor.z);
    if (m_State.active == AppCommandState::Kind::None && m_State.selection.size() == 1 &&
        m_State.selection.front().type == SelectedEntity::Type::Annotation &&
        !(event->modifiers() & Qt::ShiftModifier) && !projectReadOnly()) {
      const int annotationIndex = m_State.selection.front().index;
      if (annotationIndex >= 0 && static_cast<std::size_t>(annotationIndex) < m_State.cadAnnotations.size()) {
        CadAnnotation& annotation = m_State.cadAnnotations[static_cast<std::size_t>(annotationIndex)];
        auto screenDistanceSq = [&](double x, double y) {
          float sx = 0.f, sy = 0.f;
          camera.WorldToScreen(x, y, 0.f, static_cast<float>(width()), static_cast<float>(height()), &sx, &sy);
          return std::pow(sx - m_Last.x(), 2) + std::pow(sy - m_Last.y(), 2);
        };
        if (annotation.kind == CadAnnotation::Kind::Mtext || annotation.kind == CadAnnotation::Kind::Table) {
          const float xs[] = {annotation.boxMinX, annotation.boxMaxX, annotation.boxMaxX, annotation.boxMinX};
          const float ys[] = {annotation.boxMinY, annotation.boxMinY, annotation.boxMaxY, annotation.boxMaxY};
          int grip = -1;
          double nearest = 100.0;
          for (int i = 0; i < 4; ++i) {
            const double distance = screenDistanceSq(xs[i], ys[i]);
            if (distance < nearest) { nearest = distance; grip = i; }
          }
          if (annotation.surveyPointLabelForId >= 0) {
            const double distance = screenDistanceSq(annotation.insX, annotation.insY);
            if (distance < nearest) { nearest = distance; grip = 4; }
          }
          if (grip >= 0 && nearest <= 100.0) {
            PushUndoSnapshot(m_State, "Grip edit");
            m_State.mtextGripOrigBoxMinX = annotation.boxMinX; m_State.mtextGripOrigBoxMaxX = annotation.boxMaxX;
            m_State.mtextGripOrigBoxMinY = annotation.boxMinY; m_State.mtextGripOrigBoxMaxY = annotation.boxMaxY;
            m_State.mtextGripAnnotationIndex = annotationIndex; m_State.mtextGripCorner = grip;
            m_State.mtextGripDownWorldX = static_cast<float>(m_Cursor.x); m_State.mtextGripDownWorldY = static_cast<float>(m_Cursor.y);
            if (grip < 4) {
              const int opposite[] = {2, 3, 0, 1};
              m_State.mtextGripFixedCornerX = xs[opposite[grip]]; m_State.mtextGripFixedCornerY = ys[opposite[grip]];
            }
            m_State.mtextGripMoveActive = true;
            m_GripMovedWithButton = false;
            if (changed) changed(); update(); return;
          }
        } else if (annotation.kind == CadAnnotation::Kind::DimAligned || annotation.kind == CadAnnotation::Kind::DimLinear) {
          float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, measure = 0.f;
          if (CadDimAnyGeometry(annotation, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &measure)) {
            const float offset = annotation.dimSignedOffset;
            const float gx[] = {annotation.dimExt1X, annotation.dimExt2X, sx1 + nx * offset, sx2 + nx * offset, annotation.insX};
            const float gy[] = {annotation.dimExt1Y, annotation.dimExt2Y, sy1 + ny * offset, sy2 + ny * offset, annotation.insY};
            int grip = -1; double nearest = 100.0;
            for (int i = 0; i < 5; ++i) { const double d = screenDistanceSq(gx[i], gy[i]); if (d < nearest) { nearest = d; grip = i; } }
            if (grip >= 0 && nearest <= 100.0) {
              PushUndoSnapshot(m_State, "Grip edit");
              m_State.dimGripAnnotationIndex = annotationIndex; m_State.dimGripWhich = grip;
              m_State.dimGripOrigSignedOffset = annotation.dimSignedOffset;
              m_State.dimGripOrigExt1X = annotation.dimExt1X; m_State.dimGripOrigExt1Y = annotation.dimExt1Y;
              m_State.dimGripOrigExt2X = annotation.dimExt2X; m_State.dimGripOrigExt2Y = annotation.dimExt2Y;
              m_State.dimGripOrigInsX = annotation.insX; m_State.dimGripOrigInsY = annotation.insY;
              m_State.dimGripDragNx = nx; m_State.dimGripDragNy = ny;
              const float dmx = .5f * (sx1 + sx2), dmy = .5f * (sy1 + sy2);
              m_State.dimGripTextAlongN = (annotation.insX - dmx) * nx + (annotation.insY - dmy) * ny;
              m_State.dimGripTextAlongT = (annotation.insX - dmx) * tx + (annotation.insY - dmy) * ty;
              m_State.dimGripDownWorldX = static_cast<float>(m_Cursor.x); m_State.dimGripDownWorldY = static_cast<float>(m_Cursor.y);
              m_State.dimGripMoveActive = true;
              m_GripMovedWithButton = false;
              if (changed) changed(); update(); return;
            }
          }
        }
      }
    }
    if (m_State.active == AppCommandState::Kind::None && !m_State.selection.empty() &&
        !(event->modifiers() & Qt::ShiftModifier)) {
      const bool readOnly = projectReadOnly();
      if (!readOnly && TryBeginEntityGripAtLocal(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
          camera.orthoHalfH * 12.f / static_cast<float>(std::max(1, height())))) {
        m_GripMovedWithButton = false;
        if (changed) changed();
        update();
        return;
      }
    }
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
    } else if ((event->modifiers() & Qt::ControlModifier) && m_State.activeSpaceIndex == kModelSpaceIndex) {
      solidpick::Tolerance tolerance{};
      const double aperture = static_cast<double>(CadOffsetEntityPickTolWorld(m_State));
      tolerance.vertex = aperture;
      tolerance.edge = aperture;
      SubmitSubObjectPick(m_State, ray, tolerance, event->modifiers() & Qt::ShiftModifier, m_Log);
    } else if (m_State.active != AppCommandState::Kind::None || m_State.selBoxWaitingSecond) {
      SubmitViewportPick(m_State, m_Cursor.x, m_Cursor.y, m_Log, false,
          m_Last.x() >= m_State.selBoxAnchorScreenX, CadViewIsPlan(m_State) ? nullptr : &ray);
      m_LeftDragSelection = false;
      m_HasSelectionBoxSnapshot = false;
      m_SelectionBeforeBox.clear();
      m_SurveySelectionBeforeBox.clear();
    } else {
      if (!m_State.subObjectSelection.empty()) m_State.subObjectSelection.clear();
      ViewportPickRequest request;
      request.rawX = m_Cursor.x; request.rawY = m_Cursor.y;
      request.orbitRay = CadViewIsPlan(m_State) ? nullptr : &ray;
      request.eyeRay = ray; request.eyeRayValid = true;
      request.lineTol = camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height()));
      request.orthoHalfH = camera.orthoHalfH; request.viewportHeightPx = static_cast<float>(height());
      request.modelSpace = m_State.activeSpaceIndex == kModelSpaceIndex;
      const ViewportPickResult hit = ResolveViewportClickPick(m_State, request, request.lineTol * .5f);
      if (hit.family != ViewportPickFamily::None) {
        SelectedEntity picked = hit.entity;
        if (hit.family == ViewportPickFamily::Linework && hit.candidates.size() > 1 && m_State.multiSelectionEnabled) {
          auto* menu = new QMenu(this);
          menu->setAttribute(Qt::WA_DeleteOnClose);
          menu->setStyleSheet("QMenu { min-width: 240px; }");
          for (const CadPickCandidate& candidate : hit.candidates) {
            const char* kind = "Object";
            switch (candidate.entity.type) {
              case SelectedEntity::Type::LineSeg: kind = "Line"; break;
              case SelectedEntity::Type::Circle: kind = "Circle"; break;
              case SelectedEntity::Type::Polyline: kind = "Polyline"; break;
              case SelectedEntity::Type::Arc: kind = "Arc"; break;
              case SelectedEntity::Type::Ellipse: kind = "Ellipse"; break;
              default: break;
            }
            QAction* action = menu->addAction(QString("%1 %2").arg(kind).arg(candidate.entity.index + 1));
            const EntityAttributes* attr = nullptr;
            const auto index = static_cast<std::size_t>(candidate.entity.index);
            switch (candidate.entity.type) {
              case SelectedEntity::Type::LineSeg: if (index < m_State.userLineAttrs.size()) attr = &m_State.userLineAttrs[index]; break;
              case SelectedEntity::Type::Circle: if (index < m_State.userCircleAttrs.size()) attr = &m_State.userCircleAttrs[index]; break;
              case SelectedEntity::Type::Polyline: if (index < m_State.userPolylineAttrs.size()) attr = &m_State.userPolylineAttrs[index]; break;
              case SelectedEntity::Type::Arc: if (index < m_State.userArcAttrs.size()) attr = &m_State.userArcAttrs[index]; break;
              case SelectedEntity::Type::Ellipse: if (index < m_State.userEllAttrs.size()) attr = &m_State.userEllAttrs[index]; break;
              default: break;
            }
            if (attr) {
              float rgba[4]{};
              ResolveEntityRgbaForViewport(*attr, FindDrawingLayerRowCi(m_State, attr->layer), .8f, .8f, .8f, rgba);
              QPixmap swatch(14, 14); swatch.fill(QColor::fromRgbF(rgba[0], rgba[1], rgba[2], rgba[3]));
              action->setIcon(QIcon(swatch));
              action->setText(QString("%1 %2 — Layer %3").arg(kind).arg(candidate.entity.index + 1).arg(QString::fromStdString(attr->layer)));
            }
            const SelectedEntity candidateEntity = candidate.entity;
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            connect(action, &QAction::triggered, this, [this, candidateEntity, modifiers] {
              auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(), [&](const SelectedEntity& entry) {
                return entry.type == candidateEntity.type && entry.index == candidateEntity.index;
              });
              if (modifiers & Qt::ShiftModifier) {
                if (it != m_State.selection.end()) m_State.selection.erase(it);
              } else if (it == m_State.selection.end()) m_State.selection.push_back(candidateEntity);
              if (changed) changed();
              update();
            });
          }
          menu->popup(event->globalPosition().toPoint());
          return;
        }
        if (hit.family == ViewportPickFamily::Solid) {
          m_State.subObjectSelection.clear();
          const auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(),
              [&](const SelectedEntity& entry) { return entry.type == hit.entity.type && entry.index == hit.entity.index; });
          if (event->modifiers() & Qt::ShiftModifier) {
            if (it != m_State.selection.end()) m_State.selection.erase(it);
          } else if (it == m_State.selection.end()) m_State.selection.push_back(hit.entity);
        } else if (hit.family == ViewportPickFamily::Annotation && hit.entity.index >= 0 &&
                   static_cast<std::size_t>(hit.entity.index) < m_State.cadAnnotations.size()) {
          const CadAnnotation& annotation = m_State.cadAnnotations[static_cast<std::size_t>(hit.entity.index)];
          const bool linked = annotation.kind == CadAnnotation::Kind::Mtext && annotation.surveyPointLabelForId >= 0;
          if (event->modifiers() & Qt::ShiftModifier) {
            auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(), [&](const SelectedEntity& entry) {
              return entry.type == hit.entity.type && entry.index == hit.entity.index;
            });
            if (it == m_State.selection.end()) m_State.selection.push_back(hit.entity);
            else m_State.selection.erase(it);
            if (linked) ApplyLinkedSurveyForAnnotationPick(m_State, hit.entity.index, true);
          } else {
            ClearCadSelection(m_State);
            if (linked) ApplyLinkedSurveyForAnnotationPick(m_State, hit.entity.index, false);
            else m_State.selectedSurveyPointIndices.clear();
            m_State.selection.push_back(hit.entity);
            if (linked) SyncSurveyPointLinkedMtextSelection(m_State,
                SurveyPointIndexForId(m_State, annotation.surveyPointLabelForId));
          }
        } else {
          auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(),
              [&](const SelectedEntity& entry) { return entry.type == picked.type && entry.index == picked.index; });
          if (event->modifiers() & Qt::ShiftModifier) {
            if (it != m_State.selection.end()) m_State.selection.erase(it);
          } else if (it == m_State.selection.end()) m_State.selection.push_back(picked);
        }
      } else {
        m_SelectionBeforeBox = m_State.selection;
        m_SurveySelectionBeforeBox = m_State.selectedSurveyPointIndices;
        m_HasSelectionBoxSnapshot = true;
        if (!(event->modifiers() & Qt::ShiftModifier)) ClearSelection(m_State);
        m_SelectionPress = m_Last;
        m_LeftDragSelection = true;
        BeginSelectionBoxCorner(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
            static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()));
      }
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
        true, camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), {}, CadViewIsPlan(m_State) ? nullptr : &ray) : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    m_State.uiCursorWorldX = static_cast<float>(CadCoord::WorldXFromLocal(m_State, m_Cursor.x));
    m_State.uiCursorWorldY = static_cast<float>(CadCoord::WorldYFromLocal(m_State, m_Cursor.y));
    m_State.uiCursorWorldZ = static_cast<float>(m_Cursor.z);
    if (m_State.selBoxWaitingSecond) UpdateSelectionBoxPreview(m_State, static_cast<float>(m_Cursor.x),
        static_cast<float>(m_Cursor.y), m_Last.x() >= m_State.selBoxAnchorScreenX,
        CadViewIsPlan(m_State) ? nullptr : &camera, static_cast<float>(width()), static_cast<float>(height()));
    if (m_State.active == AppCommandState::Kind::None && !m_State.entityGripMoveActive &&
        (event->modifiers() & Qt::ControlModifier) && m_State.activeSpaceIndex == kModelSpaceIndex) {
      m_State.viewportHoverEntityValid = false;
      const auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
          static_cast<float>(width()), static_cast<float>(height()));
      solidpick::Tolerance tolerance;
      tolerance.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(m_State));
      tolerance.edge = tolerance.vertex;
      SelectedSubObject hovered;
      m_State.subObjectHoverValid = PickSubObjectAcrossSolids(m_State, ray, tolerance, &hovered);
      if (m_State.subObjectHoverValid) m_State.subObjectHover = hovered;
    } else if (m_State.active == AppCommandState::Kind::None && !m_State.entityGripMoveActive) {
      m_State.subObjectHoverValid = false;
      const auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
          static_cast<float>(width()), static_cast<float>(height()));
      ViewportPickRequest request;
      request.rawX = m_Cursor.x; request.rawY = m_Cursor.y;
      request.orbitRay = CadViewIsPlan(m_State) ? nullptr : &ray;
      request.eyeRay = ray; request.eyeRayValid = true;
      request.lineTol = camera.orthoHalfH * 8.f / static_cast<float>(std::max(1, height()));
      request.orthoHalfH = camera.orthoHalfH; request.viewportHeightPx = static_cast<float>(height());
      request.modelSpace = m_State.activeSpaceIndex == kModelSpaceIndex;
      const auto hover = ResolveViewportPick(m_State, request);
      m_State.viewportHoverEntityValid = hover.family != ViewportPickFamily::None;
      if (m_State.viewportHoverEntityValid) m_State.viewportHoverEntity = hover.entity;
      m_State.viewportPickCandidates = hover.family == ViewportPickFamily::Linework ? hover.candidates : std::vector<CadPickCandidate>{};
      m_State.viewportPickAmbiguous = m_State.viewportPickCandidates.size() > 1;
    }
    if (m_State.entityGripMoveActive) {
      if (event->buttons() & Qt::LeftButton) m_GripMovedWithButton = true;
      ApplyEntityGripPoint(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
                           static_cast<float>(m_Cursor.z));
      BumpCadGpuCache(m_State);
    }
    if (m_State.mtextGripMoveActive && m_State.mtextGripAnnotationIndex >= 0 &&
        static_cast<std::size_t>(m_State.mtextGripAnnotationIndex) < m_State.cadAnnotations.size()) {
      CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(m_State.mtextGripAnnotationIndex)];
      if (event->buttons() & Qt::LeftButton) m_GripMovedWithButton = true;
      if (m_State.mtextGripCorner == 4 && ann.surveyPointLabelForId >= 0) {
        const float dx = static_cast<float>(m_Cursor.x) - m_State.mtextGripDownWorldX;
        const float dy = static_cast<float>(m_Cursor.y) - m_State.mtextGripDownWorldY;
        ann.boxMinX = m_State.mtextGripOrigBoxMinX + dx; ann.boxMaxX = m_State.mtextGripOrigBoxMaxX + dx;
        ann.boxMinY = m_State.mtextGripOrigBoxMinY + dy; ann.boxMaxY = m_State.mtextGripOrigBoxMaxY + dy;
      } else {
        ann.boxMinX = std::min(m_State.mtextGripFixedCornerX, static_cast<float>(m_Cursor.x));
        ann.boxMaxX = std::max(m_State.mtextGripFixedCornerX, static_cast<float>(m_Cursor.x));
        ann.boxMinY = std::min(m_State.mtextGripFixedCornerY, static_cast<float>(m_Cursor.y));
        ann.boxMaxY = std::max(m_State.mtextGripFixedCornerY, static_cast<float>(m_Cursor.y));
      }
      ann.insX = ann.boxMinX; ann.insY = ann.boxMinY; BumpCadGpuCache(m_State);
    }
    if (m_State.dimGripMoveActive && m_State.dimGripAnnotationIndex >= 0 &&
        static_cast<std::size_t>(m_State.dimGripAnnotationIndex) < m_State.cadAnnotations.size()) {
      CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(m_State.dimGripAnnotationIndex)];
      if (event->buttons() & Qt::LeftButton) m_GripMovedWithButton = true;
      const float x = static_cast<float>(m_Cursor.x), y = static_cast<float>(m_Cursor.y);
      switch (m_State.dimGripWhich) {
        case 0: ann.dimExt1X = x; ann.dimExt1Y = y; CadDimAlignedApplyInsFromLocalOffset(&ann, m_State.dimGripTextAlongN, m_State.dimGripTextAlongT); break;
        case 1: ann.dimExt2X = x; ann.dimExt2Y = y; CadDimAlignedApplyInsFromLocalOffset(&ann, m_State.dimGripTextAlongN, m_State.dimGripTextAlongT); break;
        case 2: case 3:
          ann.dimSignedOffset = m_State.dimGripOrigSignedOffset + (x - m_State.dimGripDownWorldX) * m_State.dimGripDragNx + (y - m_State.dimGripDownWorldY) * m_State.dimGripDragNy;
          CadDimAlignedApplyInsFromLocalOffset(&ann, m_State.dimGripTextAlongN, m_State.dimGripTextAlongT); break;
        case 4: ann.insX = m_State.dimGripOrigInsX + (x - m_State.dimGripDownWorldX); ann.insY = m_State.dimGripOrigInsY + (y - m_State.dimGripDownWorldY); break;
        default: break;
      }
      CadDimRefreshMeasurementText(&ann, m_State.displayLinearPrecision, CadAngleDisplaySettings(m_State));
      float sx1=0, sy1=0, sx2=0, sy2=0, tx=0, ty=0, nx=0, ny=0, length=0;
      if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &length)) ann.rotationRad = std::atan2(ty, tx);
      BumpCadGpuCache(m_State);
    }
    if (cursorChanged) cursorChanged(m_Cursor.x, m_Cursor.y, m_Cursor.z);
  }
  update();
}

void EditorViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && m_GripMovedWithButton && m_State.entityGripMoveActive) {
    ClearEntityGripInteraction(m_State);
  }
  if (event->button() == Qt::LeftButton && m_GripMovedWithButton && m_State.dimGripMoveActive) ClearDimGripInteraction(m_State);
  if (event->button() == Qt::LeftButton && m_GripMovedWithButton && m_State.mtextGripMoveActive) {
    const int index = m_State.mtextGripAnnotationIndex;
    if (index >= 0 && static_cast<std::size_t>(index) < m_State.cadAnnotations.size()) {
      CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(index)];
      if (ann.surveyPointLabelForId >= 0 && m_State.mtextGripCorner == 4) {
        const int pointIndex = SurveyPointIndexForId(m_State, ann.surveyPointLabelForId);
        if (pointIndex >= 0) {
          const SurveyPoint& point = m_State.surveyPoints[static_cast<std::size_t>(pointIndex)];
          ann.surveyLabelUserOffsetEast = static_cast<float>(ann.boxMinX - point.easting);
          ann.surveyLabelUserOffsetNorth = static_cast<float>(.5 * (ann.boxMinY + ann.boxMaxY) - point.northing);
          ann.surveyLabelHasUserOffset = true;
        }
      }
    }
    ClearMtextGripInteraction(m_State);
  }
  if (event->button() == Qt::LeftButton && m_GripMovedWithButton) m_GripMovedWithButton = false;
  if (event->button() == Qt::LeftButton && m_LeftDragSelection) {
    const QPointF delta = event->position() - m_SelectionPress;
    if (std::abs(delta.x()) + std::abs(delta.y()) > 3.0) {
      ray3d::Vec3 point;
      if (WorldPoint(event->position(), point)) {
        m_State.uiCursorWorldZ = static_cast<float>(point.z);
        const auto camera = CadViewCamera(m_State);
        const auto ray = camera.ScreenRay(static_cast<float>(event->position().x()), static_cast<float>(event->position().y()),
            static_cast<float>(width()), static_cast<float>(height()));
        SubmitViewportPick(m_State, point.x, point.y, m_Log, event->modifiers() & Qt::ShiftModifier,
            event->position().x() >= m_SelectionPress.x(), CadViewIsPlan(m_State) ? nullptr : &ray);
        m_State.selBoxWaitingSecond = false;
        m_HasSelectionBoxSnapshot = false;
        m_SelectionBeforeBox.clear();
        m_SurveySelectionBeforeBox.clear();
      }
      m_LeftDragSelection = false;
    }
  }
  if (changed) changed();
  update();
  QOpenGLWidget::mouseReleaseEvent(event);
}

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
  if (event->key() == Qt::Key_Escape) {
    if (m_State.entityGripMoveActive) { RestoreEntityGripOriginal(m_State); ClearEntityGripInteraction(m_State); BumpCadGpuCache(m_State); }
    if (m_State.mtextGripMoveActive) { AbortMtextGripInteraction(m_State); BumpCadGpuCache(m_State); }
    if (m_State.dimGripMoveActive) {
      if (m_State.dimGripAnnotationIndex >= 0 && static_cast<std::size_t>(m_State.dimGripAnnotationIndex) < m_State.cadAnnotations.size()) {
        CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(m_State.dimGripAnnotationIndex)];
        ann.dimExt1X = m_State.dimGripOrigExt1X; ann.dimExt1Y = m_State.dimGripOrigExt1Y;
        ann.dimExt2X = m_State.dimGripOrigExt2X; ann.dimExt2Y = m_State.dimGripOrigExt2Y;
        ann.dimSignedOffset = m_State.dimGripOrigSignedOffset; ann.insX = m_State.dimGripOrigInsX; ann.insY = m_State.dimGripOrigInsY;
        CadDimRefreshMeasurementText(&ann, m_State.displayLinearPrecision, CadAngleDisplaySettings(m_State));
      }
      ClearDimGripInteraction(m_State); BumpCadGpuCache(m_State);
    }
    if (m_State.selBoxWaitingSecond) CancelSelectionBox();
    m_State.showCreatePointsWindow = false; CancelActiveCommand(m_State, m_Log); ClearTransientInput(); m_LeftDragSelection = false; m_GripMovedWithButton = false;
  }
  else if (event->key() == Qt::Key_Control && m_State.active == AppCommandState::Kind::None &&
           m_State.activeSpaceIndex == kModelSpaceIndex) {
    const Camera camera = CadViewCamera(m_State);
    const auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    solidpick::Tolerance tolerance;
    tolerance.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(m_State));
    tolerance.edge = tolerance.vertex;
    SelectedSubObject hovered;
    m_State.subObjectHoverValid = PickSubObjectAcrossSolids(m_State, ray, tolerance, &hovered);
    if (m_State.subObjectHoverValid) m_State.subObjectHover = hovered;
    update(); event->accept(); return;
  }
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

void EditorViewport::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Control) {
    m_State.subObjectHoverValid = false;
    update();
    event->accept();
    return;
  }
  QOpenGLWidget::keyReleaseEvent(event);
}

void EditorViewport::SetOrientation(float azimuth, float elevation) {
  m_State.viewportAzimuthDeg = azimuth;
  m_State.viewportElevationDeg = elevation;
  m_State.viewportRollDeg = 0.f;
  update();
}
