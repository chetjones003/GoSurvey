// GLEW must precede Qt's OpenGL headers.
#include <GL/glew.h>
#include "EditorViewport.hpp"
#include "ViewportRenderer.hpp"
#include "CadRubberPreview.hpp"
#include "CadDynInput.hpp"
#include "TransformPreview.hpp"
#include "MtextRichFormat.hpp"
#include "CadColor.hpp"
#include "CadCoordinateFrame.hpp"
#include "QtAnnotations.hpp"
#include "SectionClip.hpp"
#include "ViewportPickPolicy.hpp"
#include "util/gizmooverlay.hpp"
#include "AppPaths.hpp"
#include "RecentDrawings.hpp"
#include "ThumbnailCache.hpp"
#include "MapTileService.hpp"
#include <QDateTime>
#include <QKeyEvent>
#include <QLineF>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
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
  m_RightTimer.setSingleShot(true);
  m_MapTimer.setInterval(100);
  connect(&m_MapTimer, &QTimer::timeout, this, [this] {
    const bool needed = m_State.drawingSettings.Geolocated() &&
        (m_State.drawingSettings.onlineMap != DrawingSettings::OnlineMap::Off ||
         !m_State.drawingSettings.capturedAreas.empty() ||
         m_State.active == AppCommandState::Kind::GeoCaptureArea);
    if (!needed) { m_MapTimer.stop(); return; }
    update();
    if (m_LastMapLogSize != m_Log.size() || m_LastCaptureGathered != m_State.mapCapture.gathered ||
        m_LastCaptureTotal != m_State.mapCapture.total) {
      m_LastMapLogSize = m_Log.size();
      m_LastCaptureGathered = m_State.mapCapture.gathered;
      m_LastCaptureTotal = m_State.mapCapture.total;
      if (changed) changed();
    }
  });
  m_MapTimer.start();
  // Still held when the threshold passes: the menu appears at the press point without waiting.
  QObject::connect(&m_RightTimer, &QTimer::timeout, this, [this] {
    if (!m_RightPending) return;
    m_RightPending = false;
    PerformRightClick(RightClickAction::ShortcutMenu, m_RightGlobal);
    if (changed) changed();
    update();
  });
}

EditorViewport::~EditorViewport() { Cleanup(); }

void EditorViewport::Cleanup() {
  if (!m_Ready || !context()) return;
  m_MapTimer.stop();
  makeCurrent();
  m_OnlineMap.reset();
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
  if (m_Ready) {
    m_OnlineMap = std::make_unique<OnlineMapController>(
        std::make_unique<MapTileService>(MapTileCacheDirectory(), FetchMapTileOverWinHttp, 6),
        OnlineMapTextureHooks{ViewportRenderer::CreateMapTileTexture, ViewportRenderer::DeleteMapTileTexture});
  }
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
  const bool paperSpace = m_State.activeSpaceIndex >= 0 &&
      m_State.activeSpaceIndex < static_cast<int>(m_State.paperLayouts.size()) && !InFloatingModelSpace(m_State);
  if (m_Ready && !paperSpace) {
    const Camera camera = CadViewCamera(m_State);
    std::vector<float> rubber, preview, previewCircles, highlight, highlightCircles;
    AppendCadDraftRubberLines(m_State, m_Cursor.x, m_Cursor.y, m_State.orthoMode,
        camera.targetX, camera.targetY, camera.orthoHalfH, h, rubber);
    BuildTransformPreview(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
        &preview, &previewCircles, camera.orthoHalfH, h);
    BuildSelectionHighlight(m_State, &highlight, &highlightCircles);
    CadGizmoOverlay gizmoOverlay;
    BuildGizmoOverlay(m_State, &gizmoOverlay);
    if (m_State.gizmoDragActive) {
      std::vector<float> ghostLines, ghostCircles;
      BuildGizmoDragGhost(m_State, &ghostLines, &ghostCircles);
      preview.insert(preview.end(), ghostLines.begin(), ghostLines.end());
      previewCircles.insert(previewCircles.end(), ghostCircles.begin(), ghostCircles.end());
    }
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
    if (!qApp->property("cadLightTheme").toBool()) {
      tuning.bgR = 4.f / 255.f;
      tuning.bgG = 18.f / 255.f;
      tuning.bgB = 32.f / 255.f;
      tuning.gridR = 0.055f;
      tuning.gridG = 0.11f;
      tuning.gridB = 0.16f;
      tuning.gridA = 0.36f;
    }
    if (m_State.viewportSectionClip) {
      tuning.sectionClip = CadSectionClipPlane(m_State);
      tuning.sectionClipIndicator = CadSectionClipIndicator(m_State);
      tuning.sectionPlaneGraphics = SectionPlaneGraphicsFor(tuning.sectionClipIndicator);
      tuning.sectionPlaneGrips = CadSectionPlaneGrips(m_State);
      tuning.sectionPlaneGripHover = m_State.sectionPlaneGripHover;
      tuning.sectionPlaneGripDrag = m_State.sectionPlaneGripDrag;
    }
    SectionClipIndicator sectionPreview{};
    if (CadSectionPlanePreviewIndicator(m_State, &sectionPreview)) {
      tuning.sectionClipIndicator = sectionPreview;
      tuning.sectionPlaneGraphics = SectionPlaneGraphicsFor(sectionPreview);
      tuning.sectionPlaneGrips = SectionPlaneGrips{};
      tuning.sectionPlaneGripHover = -1;
      tuning.sectionPlaneGripDrag = -1;
    }
    if (m_State.drawingSettings.Geolocated() &&
        (m_State.drawingSettings.onlineMap != DrawingSettings::OnlineMap::Off ||
         !m_State.drawingSettings.capturedAreas.empty() ||
         m_State.active == AppCommandState::Kind::GeoCaptureArea))
      m_MapTimer.start();
    m_OnlineMap->Update(m_State, m_State.activeSpaceIndex == kModelSpaceIndex, camera, w, h, m_Log);
    tuning.mapTiles = &m_OnlineMap->DrawList();
    m_Renderer->SetSize(w, h);
    m_Renderer->RenderScene(camera, w, h, m_State.userLinesFlat, m_State.userCirclesCxCyZR,
        m_State.cadGpuRevision + m_RenderRevision, rubber, &m_Snap, 6.f,
        &preview, &previewCircles, &highlight, &highlightCircles, nullptr, nullptr, nullptr,
        &m_State.userLineAttrs, &m_State.userCircleAttrs, &ext, m_Grid, &m_State.drawingLayerTable, tuning,
        &m_State.pdfAttachments, -1, &m_State.cadFilledRegions, &m_State.cadFilledRegionAttrs,
        &m_State.cadMeshes, &m_State.cadMeshAttrs, &m_State.solidDisplayGeometry,
        &m_State.surfaceDisplayGeometry, nullptr, nullptr, nullptr, nullptr, nullptr,
        gizmoOverlay.empty() ? nullptr : &gizmoOverlay,
        &m_State.cadPointClouds, &m_State.cadPointCloudAttrs, &m_State.pointCloudDisplay);
    const bool capturePending = m_State.pendingThumbnailTabIdx == m_State.activeDrawingIdx &&
                                !m_State.pendingThumbnailPath.empty();
    if (capturePending) {
      const auto path = m_State.pendingThumbnailPath;
      m_State.pendingThumbnailTabIdx = -1;
      m_State.pendingThumbnailPath.clear();
      const auto dataDir = UserDataDirectory().empty() ? std::filesystem::path(".") : UserDataDirectory();
      const auto thumbnailDir = dataDir / "thumbnails";
      std::error_code ec;
      std::filesystem::create_directories(thumbnailDir, ec);
      const auto fileName = thumbs::ThumbFileName(path);
      if (!ec && m_Renderer->CaptureThumbnailBmp((thumbnailDir / fileName).u8string().c_str(), 512)) {
        thumbs::EvictThumbnails(thumbnailDir, 64);
        recent::Note(dataDir / "gosurvey-recent.json", path, fileName, QDateTime::currentSecsSinceEpoch());
        if (thumbnailReady) thumbnailReady();
      }
    }
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
  } else if (m_Ready && paperSpace) {
    // Render each layout viewport with the same CAD renderer as model space, then composite its
    // color texture into the widget's framebuffer. QPainter draws the paper entities afterward.
    const auto& layout = m_State.paperLayouts[static_cast<std::size_t>(m_State.activeSpaceIndex)];
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, w, h);
    glClearColor(0.09f, 0.105f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const Camera paperCamera = CadViewCamera(m_State);
    auto paperPoint = [&](double x, double y) {
      float sx = 0.f, sy = 0.f;
      paperCamera.WorldToScreen(x, y, 0.f, static_cast<float>(width()), static_cast<float>(height()), &sx, &sy);
      return QPointF(sx, sy);
    };
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
    for (const auto& viewport : layout.viewports) {
      const QPointF q0 = paperPoint(viewport.paperXIn, viewport.paperYIn);
      const QPointF q1 = paperPoint(viewport.paperXIn + viewport.paperWIn, viewport.paperYIn + viewport.paperHIn);
      const QRectF rectPx(QPointF(std::min(q0.x(), q1.x()), std::min(q0.y(), q1.y())),
                          QPointF(std::max(q0.x(), q1.x()), std::max(q0.y(), q1.y())));
      const int left = std::clamp(qRound(rectPx.left() * devicePixelRatioF()), 0, w);
      const int right = std::clamp(qRound(rectPx.right() * devicePixelRatioF()), 0, w);
      const int bottom = std::clamp(qRound((height() - rectPx.bottom()) * devicePixelRatioF()), 0, h);
      const int top = std::clamp(qRound((height() - rectPx.top()) * devicePixelRatioF()), 0, h);
      const int viewW = right - left, viewH = top - bottom;
      if (viewW <= 0 || viewH <= 0) continue;
      Camera camera = Camera::Plan(viewport.modelCenterX - m_State.worldDocumentOriginX,
          viewport.modelCenterY - m_State.worldDocumentOriginY,
          viewport.safeScale() * viewport.paperHIn * 0.5f);
      camera.azimuthDeg = viewport.camAzimuthDeg;
      camera.elevationDeg = viewport.camElevationDeg;
      camera.rollDeg = viewport.camRollDeg;
      camera.projection = viewport.camPerspective ? Camera::Projection::Perspective : Camera::Projection::Orthographic;
      camera.fovDeg = viewport.camFovDeg;
      RenderTuning tuning;
      tuning.bgR = 0.f; tuning.bgG = 0.f; tuning.bgB = 0.f;
      tuning.visualStyle = viewport.visualStyle;
      m_Renderer->SetSize(viewW, viewH);
      m_Renderer->RenderScene(camera, viewW, viewH, m_State.userLinesFlat, m_State.userCirclesCxCyZR,
          m_State.cadGpuRevision + m_RenderRevision, {}, nullptr, 6.f, nullptr, nullptr, nullptr, nullptr,
          nullptr, nullptr, nullptr, &m_State.userLineAttrs, &m_State.userCircleAttrs, &ext, false,
          &m_State.drawingLayerTable, tuning, &m_State.pdfAttachments, -1, &m_State.cadFilledRegions,
          &m_State.cadFilledRegionAttrs, &m_State.cadMeshes, &m_State.cadMeshAttrs,
          &m_State.solidDisplayGeometry, &m_State.surfaceDisplayGeometry, nullptr, nullptr, nullptr,
          nullptr, nullptr, nullptr, &m_State.cadPointClouds, &m_State.cadPointCloudAttrs, &m_State.pointCloudDisplay);
      GLuint readFbo = 0;
      glGenFramebuffers(1, &readFbo);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
          m_Renderer->ColorTexture(), 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
      glBlitFramebuffer(0, 0, viewW, viewH, left, bottom, right, top, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
      glDeleteFramebuffers(1, &readFbo);
    }
  }
  QPainter painter(this);
  const Camera overlayCamera = CadViewCamera(m_State);
  if (paperSpace) {
    const auto& layout = m_State.paperLayouts[static_cast<std::size_t>(m_State.activeSpaceIndex)];
    auto project = [&](double x, double y) {
      float sx = 0, sy = 0;
      overlayCamera.WorldToScreen(x, y, 0, static_cast<float>(width()), static_cast<float>(height()), &sx, &sy);
      return QPointF(sx, sy);
    };
    const QPointF p0 = project(0, 0), p1 = project(layout.sheetWidthIn(), layout.sheetHeightIn());
    const QRectF sheetRect(QPointF(std::min(p0.x(), p1.x()), std::min(p0.y(), p1.y())),
                           QPointF(std::max(p0.x(), p1.x()), std::max(p0.y(), p1.y())));
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 80));
    painter.drawRect(sheetRect.translated(5, 5));
    painter.setBrush(QColor("#f4f4f2"));
    painter.setPen(QPen(QColor("#33383d"), 1));
    QPainterPath sheetPath;
    sheetPath.addRect(sheetRect);
    QPainterPath viewportHoles;
    for (const auto& viewport : layout.viewports) {
      const QRectF viewportRect(project(viewport.paperXIn, viewport.paperYIn),
          project(viewport.paperXIn + viewport.paperWIn, viewport.paperYIn + viewport.paperHIn));
      viewportHoles.addRect(viewportRect.normalized());
    }
    sheetPath = sheetPath.subtracted(viewportHoles);
    painter.drawPath(sheetPath);
    painter.setClipRect(sheetRect.intersected(rect()));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#252a30"), 1));
    for (std::size_t i = 0; i + 5 < layout.paperLines.size(); i += 6)
      painter.drawLine(project(layout.paperLines[i], layout.paperLines[i + 1]),
                       project(layout.paperLines[i + 3], layout.paperLines[i + 4]));
    for (std::size_t i = 0; i + 2 < layout.paperCircles.size(); i += 3) {
      const QPointF center = project(layout.paperCircles[i], layout.paperCircles[i + 1]);
      const QPointF edge = project(layout.paperCircles[i] + layout.paperCircles[i + 2], layout.paperCircles[i + 1]);
      const double radius = QLineF(center, edge).length();
      painter.drawEllipse(center, radius, radius);
    }
    for (std::size_t p = 0; p + 1 < layout.paperPolyOffsets.size(); ++p) {
      const int begin = layout.paperPolyOffsets[p], end = layout.paperPolyOffsets[p + 1];
      QPolygonF polyline;
      for (int v = begin; v < end; ++v) {
        const std::size_t offset = static_cast<std::size_t>(v) * 3;
        if (offset + 1 < layout.paperPolyVerts.size())
          polyline << project(layout.paperPolyVerts[offset], layout.paperPolyVerts[offset + 1]);
      }
      if (polyline.size() > 1) {
        const bool closed = p < layout.paperPolyClosed.size() && layout.paperPolyClosed[p] != 0;
        if (closed) polyline << polyline.front();
        painter.drawPolyline(polyline);
      }
    }
    const float pxPerInch = static_cast<float>(std::hypot(p1.x() - p0.x(), p1.y() - p0.y()) /
        std::max<double>(layout.sheetWidthIn(), 1.e-6));
    for (const auto& annotation : layout.paperTexts) {
      const QPointF origin = project(annotation.kind == CadAnnotation::Kind::Mtext ? annotation.boxMinX : annotation.insX,
                                      annotation.kind == CadAnnotation::Kind::Mtext ? annotation.boxMaxY : annotation.insY);
      const float fontPx = std::clamp(annotation.plottedHeightInches * pxPerInch, 1.f, 160.f);
      if (annotation.kind == CadAnnotation::Kind::Table && annotation.tableCols > 0) {
        const QRectF box(project(annotation.boxMinX, annotation.boxMinY), project(annotation.boxMaxX, annotation.boxMaxY));
        const int rows = std::max(1, static_cast<int>((annotation.tableCells.size() + annotation.tableCols - 1) / annotation.tableCols));
        painter.setPen(QPen(QColor("#303941"), 1));
        painter.drawRect(box.normalized());
        for (int c = 1; c < annotation.tableCols; ++c) {
          const double x = box.left() + box.width() * c / annotation.tableCols;
          painter.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
        }
        for (int r = 1; r < rows; ++r) {
          const double y = box.top() + box.height() * r / rows;
          painter.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        }
        painter.setFont(QFont("Segoe UI", std::max(1, qRound(fontPx * 0.75f))));
        for (int i = 0; i < static_cast<int>(annotation.tableCells.size()); ++i) {
          const int row = i / annotation.tableCols, col = i % annotation.tableCols;
          const QRectF cell(box.left() + box.width() * col / annotation.tableCols,
                            box.top() + box.height() * row / rows,
                            box.width() / annotation.tableCols, box.height() / rows);
          painter.drawText(cell.adjusted(2, 1, -1, -1), Qt::AlignLeft | Qt::AlignVCenter,
                           QString::fromUtf8(annotation.tableCells[static_cast<std::size_t>(i)].c_str()));
        }
      } else if (annotation.kind == CadAnnotation::Kind::Text || annotation.kind == CadAnnotation::Kind::Mtext) {
        painter.setPen(QColor("#20252a"));
        painter.setFont(QFont(QString::fromUtf8(annotation.fontFamily.c_str()), std::max(1, qRound(fontPx * 0.75f))));
        painter.drawText(origin, QString::fromUtf8(annotation.text.c_str()));
      }
    }
    for (const auto& viewport : layout.viewports) {
      const QRectF viewportRect(project(viewport.paperXIn, viewport.paperYIn),
          project(viewport.paperXIn + viewport.paperWIn, viewport.paperYIn + viewport.paperHIn));
      const QRectF clippedViewport = viewportRect.normalized().intersected(sheetRect).intersected(rect());
      painter.save();
      painter.setClipRect(clippedViewport);
      auto modelProject = [&](double x, double y) {
        float paperX = 0.f, paperY = 0.f;
        ModelToPaperIn(viewport, x + m_State.worldDocumentOriginX,
            y + m_State.worldDocumentOriginY, &paperX, &paperY);
        return project(paperX, paperY);
      };
      painter.setPen(QPen(QColor("#394d5a"), 1));
      for (const auto& table : m_State.cadTables) {
        QPointF corners[4];
        for (int c = 0; c < 4; ++c) {
          float x = 0.f, y = 0.f;
          CadTableWorldCorner(table, c, &x, &y);
          corners[c] = modelProject(x, y);
        }
        painter.drawPolygon(QPolygonF{corners[0], corners[1], corners[2], corners[3]});
        const int rows = CadTableRowCount(table);
        for (int c = 1; c < table.cols; ++c) {
          float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
          const float columnOffset = table.width * static_cast<float>(c) / static_cast<float>(table.cols);
          CadTableLocalToWorld(table, columnOffset, 0.f, &ax, &ay);
          CadTableLocalToWorld(table, columnOffset, table.height, &bx, &by);
          painter.drawLine(modelProject(ax, ay), modelProject(bx, by));
        }
        for (int r = 1; r < rows; ++r) {
          float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
          const float rowOffset = table.height * static_cast<float>(r) / static_cast<float>(rows);
          CadTableLocalToWorld(table, 0.f, rowOffset, &ax, &ay);
          CadTableLocalToWorld(table, table.width, rowOffset, &bx, &by);
          painter.drawLine(modelProject(ax, ay), modelProject(bx, by));
        }
        const float textPx = std::clamp(table.plottedHeightInches * pxPerInch, 1.f, 100.f);
        painter.setFont(QFont(QString::fromUtf8(table.fontFamily.c_str()), std::max(1, qRound(textPx * 0.75f))));
        for (int i = 0; i < static_cast<int>(table.cells.size()); ++i) {
          const int row = i / std::max(1, table.cols), col = i % std::max(1, table.cols);
          float x = 0.f, y = 0.f;
          const float columnOffset = table.width * static_cast<float>(col) /
              static_cast<float>(std::max(1, table.cols));
          const float rowOffset = table.height * static_cast<float>(row) / static_cast<float>(rows);
          CadTableLocalToWorld(table, columnOffset, rowOffset, &x, &y);
          painter.drawText(modelProject(x, y), QString::fromUtf8(table.cells[static_cast<std::size_t>(i)].c_str()));
        }
      }
      painter.restore();
      painter.setPen(QPen(QColor("#4d738b"), 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(viewportRect.normalized());
    }
    painter.restore();
  } else {
    DrawQtModelAnnotations(painter, m_State, static_cast<float>(width()), static_cast<float>(height()));
  }
  if (!paperSpace) DrawQtSurveyPoints(painter, m_State, static_cast<float>(width()), static_cast<float>(height()));
  if (m_State.selBoxWaitingSecond) {
    const bool window = m_Last.x() >= m_State.selBoxAnchorScreenX;
    const QColor color = window ? QColor(70, 140, 220) : QColor(65, 190, 125);
    painter.setPen(QPen(color, 1, window ? Qt::SolidLine : Qt::DashLine));
    painter.setBrush(QColor(color.red(), color.green(), color.blue(), 35));
    painter.drawRect(QRectF(QPointF(m_State.selBoxAnchorScreenX, m_State.selBoxAnchorScreenY), m_Last).normalized());
  }
  painter.setPen(QColor("#bcc6d3"));
  // The guide uses the same UCS constraint as the shared preview/commit path.
  const auto inputPrompt = CadDynInputPromptFor(m_State);
  const bool pipeCompass = m_State.active == AppCommandState::Kind::PipeRun && m_State.pipeRunCompassOn;
  if (!paperSpace && inputPrompt.haveBase && (m_State.polarMode || pipeCompass)) {
    const ray3d::Vec3 anchor{inputPrompt.baseWorld.x - m_State.worldDocumentOriginX,
        inputPrompt.baseWorld.y - m_State.worldDocumentOriginY, inputPrompt.baseWorld.z};
    float x = static_cast<float>(m_Cursor.x), y = static_cast<float>(m_Cursor.y), z = static_cast<float>(m_Cursor.z);
    if (pipeCompass) ApplyPipeRunCompassFromAnchor(m_State, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
        &x, &y, true, static_cast<float>(anchor.z), z, &z);
    else ApplyPolarConstrainFromAnchor(m_State, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
        &x, &y, true, static_cast<float>(anchor.z), z, &z);
    const auto camera = CadViewCamera(m_State);
    float ax = 0, ay = 0, bx = 0, by = 0;
    camera.WorldToScreen(anchor.x, anchor.y, anchor.z, width(), height(), &ax, &ay);
    camera.WorldToScreen(x, y, z, width(), height(), &bx, &by);
    const QLineF line(QPointF(ax, ay), QPointF(bx, by));
    if (line.length() > 0.1) {
      const QPointF direction = (line.p2() - line.p1()) / line.length();
      painter.setPen(QPen(QColor("#78eb8c"), 1, Qt::DashLine));
      const double reach = std::hypot(width(), height());
      painter.drawLine(line.p1() - direction * reach, line.p1() + direction * reach);
    }
  }
  painter.setPen(QColor("#bcc6d3"));
  painter.drawText(16, 26, paperSpace ? "[Paper]" : "[Model]  [Orthographic]");
  if (!paperSpace && m_OnlineMap && m_OnlineMap->Drawing()) {
    painter.fillRect(QRectF(width() - 268, height() - 36, 252, 26), QColor(20, 24, 28, 190));
    painter.setPen(QColor("#f4f4f4"));
    painter.drawText(QRectF(width() - 260, height() - 34, 246, 24), Qt::AlignRight | Qt::AlignVCenter,
                     "Map: USGS The National Map");
  }
  if (!m_Ready) painter.drawText(rect(), Qt::AlignCenter, m_Error);
  painter.setPen(QColor("#7593ad"));
  if (!paperSpace) painter.drawText(18, height() - 20, "WCS");
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
    if ((event->modifiers() & Qt::ShiftModifier) && snapMenuRequested) {
      snapMenuRequested(event->globalPosition().toPoint());
      event->accept(); return;
    }
    if (m_State.sectionPlaneGripDrag >= 0) {
      AbortSectionPlaneGripDrag(m_State);
      m_Log.push_back("Section plane drag cancelled.");
    } else if (m_State.gizmoDragActive && !IsGizmoCommandKind(m_State.active)) {
      CancelGizmoDrag(m_State);
      m_Log.push_back("Gizmo drag cancelled.");
    } else {
      RightPress(event->globalPosition().toPoint());
    }
    if (changed) changed();
    update();
    event->accept();
    return;
  } else if (event->button() == Qt::LeftButton && (WorldPoint(m_Last, m_Cursor) ||
             (m_State.active == AppCommandState::Kind::None && ViewPoint(m_Last, m_Cursor)))) {
    const Camera camera = CadViewCamera(m_State);
    auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    const auto overrideKind = static_cast<CadSnap::Kind>(m_State.objectSnapKindOverrideKind);
    const auto* onlyKind = m_State.objectSnapKindOverrideValid ? &overrideKind : nullptr;
    m_Snap = m_State.active != AppCommandState::Kind::None ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State,
        true, camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), {},
        CadViewIsPlan(m_State) ? nullptr : &ray, onlyKind) : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    m_State.viewportSnapPickValid = m_Snap.valid;
    if (m_Snap.valid) m_State.viewportSnapPickKind = static_cast<int>(m_Snap.kind);
    m_State.viewportSnapPickLocalX = m_Cursor.x;
    m_State.viewportSnapPickLocalY = m_Cursor.y;
    m_State.viewportSnapPickLocalZ = m_Cursor.z;
    m_State.resolvedPointZValid = true;
    m_State.resolvedPointZ = static_cast<float>(m_Cursor.z);
    m_State.uiCursorWorldX = static_cast<float>(CadCoord::WorldXFromLocal(m_State, m_Cursor.x));
    m_State.uiCursorWorldY = static_cast<float>(CadCoord::WorldYFromLocal(m_State, m_Cursor.y));
    m_State.uiCursorWorldZ = static_cast<float>(m_Cursor.z);
    if (cursorChanged) cursorChanged(m_Cursor.x, m_Cursor.y, m_Cursor.z);
    if (submitLockedInput && submitLockedInput()) { event->accept(); update(); return; }
    const auto route = ViewportClickRouteFor(m_State);
    bool overlayHandled = false;
    if (route == ViewportClickRoute::SubObjectFacePick &&
        m_State.active == AppCommandState::Kind::SectionPlane) {
      if (m_State.sectionPlanePhase == AppCommandState::SectionPlanePhase::WaitThroughPoint) {
        SubmitSectionPlanePointPick(m_State, static_cast<float>(m_Cursor.x),
            static_cast<float>(m_Cursor.y), m_Log);
        overlayHandled = true;
      } else {
        solidpick::Tolerance faceTol;
        faceTol.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(m_State));
        faceTol.edge = faceTol.vertex;
        overlayHandled = SubmitSectionPlaneFacePick(m_State, ray, faceTol, m_Log);
      }
    }
    if (!overlayHandled && (route == ViewportClickRoute::IdleSelection ||
                            route == ViewportClickRoute::GizmoHandlePick) &&
        (m_State.gizmoDragActive || !(event->modifiers() & Qt::ControlModifier))) {
      overlayHandled = SubmitGizmoClick(m_State, ray,
          static_cast<double>(CadSnap::WorldToleranceFromPixels(
              static_cast<float>(height()), camera.orthoHalfH, kGizmoHandleGrabPx)), m_Log);
    }
    if (!overlayHandled && route == ViewportClickRoute::IdleSelection &&
        !(event->modifiers() & Qt::ControlModifier)) {
      overlayHandled = SubmitSectionPlaneClick(m_State, ray,
          static_cast<double>(CadSnap::WorldToleranceFromPixels(
              static_cast<float>(height()), camera.orthoHalfH, kGizmoHandleGrabPx)), m_Log);
    }
    if (overlayHandled || route == ViewportClickRoute::GizmoHandlePick ||
        route == ViewportClickRoute::SubObjectFacePick) {
      if (changed) changed();
      update();
      event->accept();
      return;
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
    } else if (m_State.active != AppCommandState::Kind::None || m_State.selBoxWaitingSecond) {
      SubmitViewportPick(m_State, m_Cursor.x, m_Cursor.y, m_Log, false,
          m_Last.x() >= m_State.selBoxAnchorScreenX, CadViewIsPlan(m_State) ? nullptr : &ray);
    } else {
      SelectedEntity picked;
      float distance = 0.f;
      if (!(event->modifiers() & Qt::ShiftModifier)) ClearSelection(m_State);
      if (PickClosestCadEntity(m_State, m_Cursor.x, m_Cursor.y,
          camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), &picked, &distance, CadViewIsPlan(m_State) ? nullptr : &ray)) {
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

void EditorViewport::mouseDoubleClickEvent(QMouseEvent* event) {
  if (m_State.activeSpaceIndex >= 0 && m_State.activeSpaceIndex < static_cast<int>(m_State.paperLayouts.size()) &&
      !InFloatingModelSpace(m_State)) {
    ray3d::Vec3 point;
    if (WorldPoint(event->position(), point)) {
      auto& layout = m_State.paperLayouts[static_cast<std::size_t>(m_State.activeSpaceIndex)];
      for (auto& annotation : layout.paperTexts) {
        if (annotation.kind != CadAnnotation::Kind::Table || annotation.tableCols <= 0) continue;
        std::vector<CadTableCellRect> cells;
        CadTableLayoutCells(annotation.boxMinX, annotation.boxMinY, annotation.boxMaxX, annotation.boxMaxY,
            annotation.tableCols, annotation.tableCells, &cells);
        for (std::size_t i = 0; i < cells.size() && i < annotation.tableCells.size(); ++i) {
          const auto& cell = cells[i];
          if (point.x < cell.x0 || point.x > cell.x1 || point.y < cell.y0 || point.y > cell.y1) continue;
          const int row = static_cast<int>(i) / annotation.tableCols + 1;
          const int column = static_cast<int>(i) % annotation.tableCols + 1;
          const auto uid = m_State.activeDrawingIdx > 0 && m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size())
              ? m_State.drawingTabs[m_State.activeDrawingIdx].projectUid : 0;
          const auto readOnly = std::find_if(m_State.openProjects.begin(), m_State.openProjects.end(),
              [uid](const auto& session) { return uid && session.uid == uid && session.readOnly; });
          if (readOnly != m_State.openProjects.end()) {
            QMessageBox::warning(this, "Read-only project", "This project was opened read-only. The table cell cannot be changed.");
            event->accept();
            return;
          }
          bool accepted = false;
          const QString value = QInputDialog::getText(this, "Edit table cell",
              QString("Row %1, column %2:").arg(row).arg(column), QLineEdit::Normal,
              QString::fromUtf8(annotation.tableCells[i].c_str()), &accepted);
          if (accepted && value.toUtf8().toStdString() != annotation.tableCells[i]) {
            PushUndoSnapshot(m_State, "Edit paper table cell");
            annotation.tableCells[i] = value.toUtf8().toStdString();
            BumpCadGpuCache(m_State);
            if (changed) changed();
            update();
          }
          event->accept();
          return;
        }
      }
    }
  }
  QOpenGLWidget::mouseDoubleClickEvent(event);
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
    const bool sectionGripDrag = m_State.sectionPlaneGripDrag >= 0;
    const auto overrideKind = static_cast<CadSnap::Kind>(m_State.objectSnapKindOverrideKind);
    const auto* onlyKind = m_State.objectSnapKindOverrideValid ? &overrideKind : nullptr;
    m_Snap = (m_State.active != AppCommandState::Kind::None || sectionGripDrag)
        ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State, true,
            camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), {},
            CadViewIsPlan(m_State) ? nullptr : &ray, onlyKind)
        : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    m_State.viewportSnapPickValid = m_Snap.valid;
    m_State.viewportSnapPickLocalX = m_Cursor.x;
    m_State.viewportSnapPickLocalY = m_Cursor.y;
    m_State.viewportSnapPickLocalZ = m_Cursor.z;
    if (m_Snap.valid) m_State.viewportSnapPickKind = static_cast<int>(m_Snap.kind);
    m_State.uiCursorWorldX = static_cast<float>(CadCoord::WorldXFromLocal(m_State, m_Cursor.x));
    m_State.uiCursorWorldY = static_cast<float>(CadCoord::WorldYFromLocal(m_State, m_Cursor.y));
    m_State.uiCursorWorldZ = static_cast<float>(m_Cursor.z);
    if (m_State.selBoxWaitingSecond) UpdateSelectionBoxPreview(m_State, static_cast<float>(m_Cursor.x),
        static_cast<float>(m_Cursor.y), m_Last.x() >= m_State.selBoxAnchorScreenX,
        CadViewIsPlan(m_State) ? nullptr : &camera, static_cast<float>(width()), static_cast<float>(height()));
    const auto route = ViewportClickRouteFor(m_State);
    if (sectionGripDrag && m_State.activeSpaceIndex == kModelSpaceIndex) {
      const bool namedFeature = m_Snap.valid &&
          CadSnap::SnapClass(m_Snap.kind) == 1;
      const ray3d::Vec3 snapLocal{m_Snap.x, m_Snap.y, m_Snap.z};
      UpdateSectionPlaneGripDrag(m_State, ray, namedFeature ? &snapLocal : nullptr);
    } else if (m_State.activeSpaceIndex == kModelSpaceIndex &&
               m_State.viewportSectionClip && route == ViewportClickRoute::IdleSelection &&
               !(event->modifiers() & Qt::ControlModifier)) {
      UpdateSectionPlaneGripHover(m_State, ray,
          static_cast<double>(CadSnap::WorldToleranceFromPixels(
              static_cast<float>(height()), camera.orthoHalfH, kGizmoHandleGrabPx)));
    } else if (m_State.sectionPlaneGripHover >= 0 && !sectionGripDrag) {
      m_State.sectionPlaneGripHover = -1;
    }
    if (m_State.activeSpaceIndex == kModelSpaceIndex &&
        (m_State.gizmoDragActive || (route == ViewportClickRoute::IdleSelection ||
                                     route == ViewportClickRoute::GizmoHandlePick) &&
                                    !(event->modifiers() & Qt::ControlModifier))) {
      if (m_State.gizmoDragActive) UpdateGizmoDrag(m_State, ray);
      else UpdateGizmoHover(m_State, ray,
          static_cast<double>(CadSnap::WorldToleranceFromPixels(
              static_cast<float>(height()), camera.orthoHalfH, kGizmoHandleGrabPx)));
    } else if (m_State.gizmoHoverAxis >= 0 && !m_State.gizmoDragActive) {
      m_State.gizmoHoverAxis = -1;
    }
    if (m_State.active == AppCommandState::Kind::SectionPlane &&
        m_State.sectionPlanePhase == AppCommandState::SectionPlanePhase::WaitThroughPoint) {
      solidpick::Tolerance previewTol;
      previewTol.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(m_State));
      previewTol.edge = previewTol.vertex;
      UpdateSectionPlanePreview(m_State, ray, previewTol);
    }
    if (cursorChanged) cursorChanged(m_Cursor.x, m_Cursor.y, m_Cursor.z);
  }
  update();
}

void EditorViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const auto camera = CadViewCamera(m_State);
    const auto ray = camera.ScreenRay(static_cast<float>(event->position().x()),
        static_cast<float>(event->position().y()), static_cast<float>(width()),
        static_cast<float>(height()));
    const double tol = CadSnap::WorldToleranceFromPixels(
        static_cast<float>(height()), camera.orthoHalfH, kGizmoHandleGrabPx);
    if (m_State.sectionPlaneGripDrag >= 0)
      (void)SubmitSectionPlaneClick(m_State, ray, tol, m_Log);
    else if (m_State.gizmoDragActive)
      (void)SubmitGizmoClick(m_State, ray, tol, m_Log);
    if (changed) changed();
    update();
  }
  if (event->button() == Qt::RightButton && m_RightPending) {
    m_RightPending = false;
    m_RightTimer.stop();
    PerformRightClick(CadRightClickHoldIsMenu(static_cast<double>(m_RightHeld.elapsed()), m_State.rightClickLongerClickMs)
                          ? RightClickAction::ShortcutMenu : RightClickAction::Enter, m_RightGlobal);
    if (changed) changed();
    update();
    return;
  }
  QOpenGLWidget::mouseReleaseEvent(event);
}

void EditorViewport::RightPress(const QPoint& globalPosition) {
  const bool hasSelection = !m_State.selection.empty() || !m_State.selectedSurveyPointIndices.empty();
  if (CadRightClickIsTimed(m_State, hasSelection)) {
    // Quick click = ENTER, held past the threshold = the menu, which appears as soon as the
    // threshold passes if the button is still down.
    m_RightPending = true;
    m_RightGlobal = globalPosition;
    m_RightHeld.start();
    m_RightTimer.start(m_State.rightClickLongerClickMs);
    return;
  }
  PerformRightClick(CadClassifyRightClick(m_State, hasSelection), globalPosition);
}

void EditorViewport::RightClickAsEnter() {
  if (m_State.active != AppCommandState::Kind::None) {
    char empty[2]{};
    ProcessCommandLineSubmit(empty, sizeof(empty), m_State, m_Log);
  } else if (m_State.lastCommand != AppCommandState::Kind::None) {
    RepeatLastCommand(m_State, m_Log);
  }
}

void EditorViewport::PerformRightClick(RightClickAction action, const QPoint& globalPosition) {
  switch (action) {
  case RightClickAction::Enter: RightClickAsEnter(); break;
  case RightClickAction::RepeatLast: RepeatLastCommand(m_State, m_Log); break;
  case RightClickAction::ShortcutMenu: if (contextMenuRequested) contextMenuRequested(globalPosition); break;
  }
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
    m_State.showCreatePointsWindow = false;
    if (m_State.sectionPlaneGripDrag >= 0) {
      AbortSectionPlaneGripDrag(m_State);
      m_Log.push_back("Section plane drag cancelled.");
    } else if (m_State.gizmoDragActive && !IsGizmoCommandKind(m_State.active)) {
      CancelGizmoDrag(m_State);
      m_Log.push_back("Gizmo drag cancelled.");
    } else {
      CancelActiveCommand(m_State, m_Log);
    }
    ClearTransientInput();
  }
  else if (event->key() == Qt::Key_Delete) ExecuteDeleteSelection(m_State, m_Log);
  else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
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
