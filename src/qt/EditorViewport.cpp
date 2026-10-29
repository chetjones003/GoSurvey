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
#include "QtViewCube.hpp"
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
#include <QMenu>
#include <QIcon>
#include <QOpenGLContext>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QLabel>
#include <QLayout>
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
  m_ViewAnimTimer.setInterval(16);
  connect(&m_ViewAnimTimer, &QTimer::timeout, this, [this] { StepViewAnimation(); });
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
  m_ViewAnimTimer.stop();
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
  // REQ-450: the viewport draws its own cursor, so the operating-system one is hidden - but only
  // once drawing works, so a failed renderer still leaves a visible pointer.
  if (m_Ready) setCursor(Qt::BlankCursor);
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

// Escape or right-click during a grip drag puts the object back exactly as it was. Returns whether a grip
// edit was in progress, so the caller can stop there rather than also cancelling the command.
bool EditorViewport::CancelGripEdit() {
  if (m_State.entityGripMoveActive) {
    RestoreEntityGripOriginal(m_State);
    ClearEntityGripInteraction(m_State);
  } else if (m_State.mtextGripMoveActive) {
    AbortMtextGripInteraction(m_State);
  } else if (m_State.dimGripMoveActive) {
    if (m_State.dimGripAnnotationIndex >= 0 && static_cast<std::size_t>(m_State.dimGripAnnotationIndex) < m_State.cadAnnotations.size()) {
      CadAnnotation& ann = m_State.cadAnnotations[static_cast<std::size_t>(m_State.dimGripAnnotationIndex)];
      ann.dimExt1X = m_State.dimGripOrigExt1X; ann.dimExt1Y = m_State.dimGripOrigExt1Y;
      ann.dimExt2X = m_State.dimGripOrigExt2X; ann.dimExt2Y = m_State.dimGripOrigExt2Y;
      ann.dimSignedOffset = m_State.dimGripOrigSignedOffset; ann.insX = m_State.dimGripOrigInsX; ann.insY = m_State.dimGripOrigInsY;
      CadDimRefreshMeasurementText(&ann, m_State.displayLinearPrecision, CadAngleDisplaySettings(m_State));
    }
    ClearDimGripInteraction(m_State);
  } else {
    return false;
  }
  m_GripMovedWithButton = false;
  BumpCadGpuCache(m_State);
  return true;
}

void EditorViewport::ClearTransientInput() {
  m_Snap = {};
  m_State.viewportSnapPickValid = false;
  m_State.resolvedPointZValid = false;
}

void EditorViewport::BeginWindowResizePreview() {
  if (!m_Ready || m_ResizePreview || m_State.activeSpaceIndex != kModelSpaceIndex || !parentWidget()) return;
  const QImage frame = grabFramebuffer();
  if (frame.isNull()) return;

  m_ResizePreviewMinimumSize = minimumSize();
  m_ResizePreviewMaximumSize = maximumSize();
  m_ResizePreview = true;
  setUpdatesEnabled(false);
  setFixedSize(size());

  m_ResizePreviewOverlay = new QLabel(parentWidget());
  m_ResizePreviewOverlay->setObjectName("viewportResizePreview");
  m_ResizePreviewOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_ResizePreviewOverlay->setScaledContents(true);
  m_ResizePreviewOverlay->setPixmap(QPixmap::fromImage(frame));
  UpdateWindowResizePreviewGeometry();
  m_ResizePreviewOverlay->show();
  m_ResizePreviewOverlay->raise();
}

void EditorViewport::EndWindowResizePreview() {
  if (!m_ResizePreview) return;
  m_ResizePreview = false;
  delete m_ResizePreviewOverlay;
  m_ResizePreviewOverlay = nullptr;
  setUpdatesEnabled(true);
  setMinimumSize(m_ResizePreviewMinimumSize);
  setMaximumSize(m_ResizePreviewMaximumSize);
  if (parentWidget() && parentWidget()->layout()) parentWidget()->layout()->activate();
  updateGeometry();
  update();
}

void EditorViewport::UpdateWindowResizePreviewGeometry() {
  if (!m_ResizePreviewOverlay || !parentWidget()) return;
  const QRect geometry = parentWidget()->rect();
  if (m_ResizePreviewOverlay->geometry() != geometry)
    m_ResizePreviewOverlay->setGeometry(geometry);
}

void EditorViewport::paintGL() {
  const int w = qRound(width() * devicePixelRatioF());
  const int h = qRound(height() * devicePixelRatioF());
  if (w <= 0 || h <= 0) return;
  // New entities still have id zero after their command's pre-edit undo snapshot.
  // Assign identities before display caches and selection overlays consume them (REQ-076).
  EnsureEntityIds(m_State);
  // A named-view restore starts an orientation animation without going through this widget; make
  // sure something is ticking it (REQ-059).
  if (m_State.viewAnimActive && !m_ViewAnimTimer.isActive()) StartViewAnimationClock();
  m_State.uiViewportWidthPx = static_cast<float>(width());
  m_State.uiViewportHeightPx = static_cast<float>(height());
  // ZOOMEXTENTS / ZOOM WINDOW only set a pending flag; the camera write needs the real viewport
  // size, so it happens here, before the camera below is built from the result (REQ-120, REQ-123).
  // The pan/zoom out-pointers are optional: the function writes them into the state itself.
  // Imports fit the drawing to the last painted size (DxfIo/LibreDwgCad read these).
  m_State.viewportLastFbW = w;
  m_State.viewportLastFbH = h;
  ProcessPendingViewportZoom(m_State, nullptr, nullptr, nullptr, w, h,
      static_cast<float>(width()) / static_cast<float>(std::max(1, height())), m_Log);
  const bool paperSpace = m_State.activeSpaceIndex >= 0 &&
      m_State.activeSpaceIndex < static_cast<int>(m_State.paperLayouts.size()) && !InFloatingModelSpace(m_State);
  const bool useResizePreview = m_Ready && !paperSpace && m_ResizePreview && m_Renderer->HasRenderedFrame();
  if (useResizePreview) {
    m_Renderer->BlitRenderedFrame(defaultFramebufferObject(), w, h);
  } else if (m_Ready && !paperSpace) {
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
    ext.splines = &m_State.cadSplines; ext.splineAttrs = &m_State.cadSplineAttrs;
    ext.points = &m_State.cadPoints; ext.pointAttrs = &m_State.cadPointAttrs;
    ext.pdMode = m_State.pdMode;
    ext.pdSize = m_State.pdSize;
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
    if (!qApp->property("cadLightTheme").toBool()) {
      tuning.bgR = 32.f / 255.f;
      tuning.bgG = 36.f / 255.f;
      tuning.bgB = 40.f / 255.f;
      tuning.gridR = 0.12f;
      tuning.gridG = 0.20f;
      tuning.gridB = 0.20f;
      tuning.gridA = 0.40f;
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
        &preview, &previewCircles, &highlight, &highlightCircles, &subHoverLines, nullptr, nullptr,
        &m_State.userLineAttrs, &m_State.userCircleAttrs, &ext, m_Grid, &m_State.drawingLayerTable, tuning,
        &m_State.pdfAttachments, -1, &m_State.cadFilledRegions, &m_State.cadFilledRegionAttrs,
        &m_State.cadMeshes, &m_State.cadMeshAttrs, &m_State.solidDisplayGeometry,
        &m_State.surfaceDisplayGeometry, nullptr, nullptr, nullptr, nullptr, &subObjectOverlay,
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
    ext.splines = &m_State.cadSplines; ext.splineAttrs = &m_State.cadSplineAttrs;
    ext.points = &m_State.cadPoints; ext.pointAttrs = &m_State.cadPointAttrs;
    ext.pdMode = m_State.pdMode;
    ext.pdSize = m_State.pdSize;
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
  // REQ-350 (e): the Pipe Fittings palette's part pictures are rendered here, after the scene pass
  // (the one point in a paint where binding another framebuffer is safe), a few per paint.
  if (m_Ready && (!m_State.pipeFittingThumbRequests.empty() || !m_State.pipeFittingThumbStale.empty())) {
    const bool changed = ServicePipeFittingThumbnails(m_State, *m_Renderer, m_PartThumbs);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, w, h);
    if (!m_State.pipeFittingThumbRequests.empty()) update();
    if (changed && partThumbnailsChanged) partThumbnailsChanged();
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
    // Sheet fills (issue #768): one even-odd path per region, under the linework like the plot does.
    for (std::size_t fi = 0; fi < layout.paperFilledRegions.size(); ++fi) {
      const CadFilledRegion& fill = layout.paperFilledRegions[fi];
      QPainterPath path;
      path.setFillRule(Qt::OddEvenFill);
      for (std::size_t lp = 0; lp < fill.loopStart.size(); ++lp) {
        const int begin = fill.loopStart[lp], count = fill.loopCount(lp);
        if (count < 3) continue;
        QPolygonF loop;
        for (int k = 0; k < count; ++k) {
          const std::size_t vi = static_cast<std::size_t>(begin + k) * 3;
          if (vi + 1 < fill.vertsXyz.size()) loop << project(fill.vertsXyz[vi], fill.vertsXyz[vi + 1]);
        }
        path.addPolygon(loop);
        path.closeSubpath();
      }
      QColor color("#d9d9d9");
      if (fi < layout.paperFilledRegionAttrs.size()) {
        const QColor entityColor(QString::fromUtf8(layout.paperFilledRegionAttrs[fi].color.c_str()));
        if (entityColor.isValid()) color = entityColor;
      }
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawPath(path);
    }
    painter.setBrush(Qt::NoBrush);
    const auto isTrimHighlighted = [&](PaperEntityRef::Type type, int index) {
      if (m_State.paperHoverValid && m_State.paperHover.type == type && m_State.paperHover.index == index)
        return true;
      return std::find_if(m_State.paperTrimCutters.begin(), m_State.paperTrimCutters.end(),
          [&](const PaperEntityRef& cutter) { return cutter.type == type && cutter.index == index; }) !=
          m_State.paperTrimCutters.end();
    };
    for (std::size_t i = 0; i + 5 < layout.paperLines.size(); i += 6) {
      const int index = static_cast<int>(i / 6);
      painter.setPen(QPen(isTrimHighlighted(PaperEntityRef::Type::Line, index) ? QColor("#00a8ff") : QColor("#252a30"),
                          isTrimHighlighted(PaperEntityRef::Type::Line, index) ? 2.5 : 1));
      painter.drawLine(project(layout.paperLines[i], layout.paperLines[i + 1]),
                       project(layout.paperLines[i + 3], layout.paperLines[i + 4]));
    }
    for (std::size_t i = 0; i + 2 < layout.paperCircles.size(); i += 3) {
      const int index = static_cast<int>(i / 3);
      const QPointF center = project(layout.paperCircles[i], layout.paperCircles[i + 1]);
      const QPointF edge = project(layout.paperCircles[i] + layout.paperCircles[i + 2], layout.paperCircles[i + 1]);
      const double radius = QLineF(center, edge).length();
      const bool highlighted = isTrimHighlighted(PaperEntityRef::Type::Circle, index);
      painter.setPen(QPen(highlighted ? QColor("#00a8ff") : QColor("#252a30"), highlighted ? 2.5 : 1));
      painter.drawEllipse(center, radius, radius);
    }
    for (std::size_t i = 0; i < layout.paperArcs.size(); ++i) {
      const auto& arc = layout.paperArcs[i];
      const bool highlighted = isTrimHighlighted(PaperEntityRef::Type::Arc, static_cast<int>(i));
      painter.setPen(QPen(highlighted ? QColor("#00a8ff") : QColor("#252a30"), highlighted ? 2.5 : 1));
      QPolygonF curve;
      constexpr int samples = 48;
      for (int step = 0; step <= samples; ++step) {
        const double t = static_cast<double>(step) / samples;
        const double angle = arc.startRad + arc.sweepRad * t;
        curve << project(arc.cx + arc.r * std::cos(angle), arc.cy + arc.r * std::sin(angle));
      }
      painter.drawPolyline(curve);
    }
    for (std::size_t i = 0; i < layout.paperEllipses.size(); ++i) {
      const auto& ellipse = layout.paperEllipses[i];
      const bool highlighted = isTrimHighlighted(PaperEntityRef::Type::Ellipse, static_cast<int>(i));
      painter.setPen(QPen(highlighted ? QColor("#00a8ff") : QColor("#252a30"), highlighted ? 2.5 : 1));
      QPolygonF curve;
      constexpr int samples = 64;
      for (int step = 0; step <= samples; ++step) {
        const double angle = 6.283185307179586 * static_cast<double>(step) / samples;
        const double majorX = ellipse.majVx * std::cos(angle);
        const double majorY = ellipse.majVy * std::cos(angle);
        const double minorScale = ellipse.ratio * std::sin(angle);
        curve << project(ellipse.cx + majorX - ellipse.majVy * minorScale,
                         ellipse.cy + majorY + ellipse.majVx * minorScale);
      }
      painter.drawPolyline(curve);
    }
    for (std::size_t si = 0; si < layout.paperSplines.size(); ++si) {
      const bool highlighted = isTrimHighlighted(PaperEntityRef::Type::Spline, static_cast<int>(si));
      painter.setPen(QPen(highlighted ? QColor("#00a8ff") : QColor("#252a30"), highlighted ? 2.5 : 1));
      std::vector<ray3d::Vec3> pts;
      spline::Tessellate(layout.paperSplines[si], kSplineSampleTolerance, &pts);
      QPolygonF curve;
      for (const auto& v : pts)
        curve << project(v.x, v.y);
      if (curve.size() > 1)
        painter.drawPolyline(curve);
    }
    {
      const double viewH = std::max(1.0, static_cast<double>(layout.sheetHeightIn()));
      const double halfSize = CadPointMarkerSize(m_State.pdSize, viewH);
      std::vector<CadPointStroke> strokes;
      for (std::size_t pi = 0; pi < layout.paperPoints.size(); ++pi) {
        const bool highlighted = isTrimHighlighted(PaperEntityRef::Type::Point, static_cast<int>(pi));
        painter.setPen(QPen(highlighted ? QColor("#00a8ff") : QColor("#252a30"), highlighted ? 2.5 : 1));
        CadPointDisplayStrokes(layout.paperPoints[pi], m_State.pdMode, halfSize, &strokes);
        for (const CadPointStroke& s : strokes)
          painter.drawLine(project(s.x0, s.y0), project(s.x1, s.y1));
      }
    }
    for (std::size_t p = 0; p + 1 < layout.paperPolyOffsets.size(); ++p) {
      const bool highlighted = isTrimHighlighted(PaperEntityRef::Type::Polyline, static_cast<int>(p));
      painter.setPen(QPen(highlighted ? QColor("#00a8ff") : QColor("#252a30"), highlighted ? 2.5 : 1));
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
    for (const auto& table : layout.paperTables) {  // issue #768: first-class sheet tables
      painter.setPen(QPen(QColor("#303941"), 1));
      float cx[4], cy[4];
      QPolygonF outline;
      for (int c = 0; c < 4; ++c) {
        CadTableWorldCorner(table, c, &cx[c], &cy[c]);
        outline << project(cx[c], cy[c]);
      }
      painter.drawPolygon(outline);
      const float tw = std::max(table.width, 1.e-3f), th = std::max(table.height, 1.e-3f);
      const int rows = CadTableRowCount(table);
      for (int c = 1; c < table.cols; ++c) {
        float ax, ay, bx, by;
        const float lx = tw * static_cast<float>(c) / static_cast<float>(table.cols);
        CadTableLocalToWorld(table, lx, 0.f, &ax, &ay);
        CadTableLocalToWorld(table, lx, th, &bx, &by);
        painter.drawLine(project(ax, ay), project(bx, by));
      }
      for (int r = 1; r < rows; ++r) {
        float ax, ay, bx, by;
        const float ly = th * static_cast<float>(r) / static_cast<float>(rows);
        CadTableLocalToWorld(table, 0.f, ly, &ax, &ay);
        CadTableLocalToWorld(table, tw, ly, &bx, &by);
        painter.drawLine(project(ax, ay), project(bx, by));
      }
      std::vector<CadTableCellRect> cells;
      CadTableLayoutWorldCells(table, &cells);
      painter.setPen(QColor("#20252a"));
      painter.setFont(QFont(QString::fromUtf8(table.fontFamily.c_str()),
          std::max(1, qRound(std::clamp(table.plottedHeightInches * pxPerInch, 1.f, 160.f) * 0.75f))));
      for (std::size_t i = 0; i < cells.size() && i < table.cells.size(); ++i) {
        const QPointF a = project(cells[i].x0, cells[i].y1), b = project(cells[i].x1, cells[i].y0);
        painter.drawText(QRectF(a, b).normalized().adjusted(2, 1, -1, -1), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8(table.cells[i].c_str()));
      }
    }
    for (const auto& leader : layout.paperMultileaders) {  // issue #768: leader paths + label
      auto drawPath = [&](const std::vector<float>& pts) {
        for (std::size_t i = 0; i + 5 < pts.size(); i += 3)
          painter.drawLine(project(pts[i], pts[i + 1]), project(pts[i + 3], pts[i + 4]));
      };
      painter.setPen(QPen(QColor("#252a30"), 1));
      drawPath(leader.pathXyz);
      for (const auto& branch : leader.extraLeaderPaths)
        drawPath(branch);
      const auto& label = leader.label;
      painter.setPen(QColor("#20252a"));
      painter.setFont(QFont(QString::fromUtf8(label.fontFamily.c_str()),
          std::max(1, qRound(std::clamp(label.plottedHeightInches * pxPerInch, 1.f, 160.f) * 0.75f))));
      painter.drawText(project(label.boxMinX, label.boxMaxY), QString::fromUtf8(label.text.c_str()));
    }
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
        case SelectedEntity::Type::Spline: {  // REQ-443: fit points, or control vertices for a CV spline
          if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.cadSplines.size()) {
            const CadSpline& sp = m_State.cadSplines[static_cast<std::size_t>(selected.index)];
            for (std::size_t i = 0; i < spline::EditablePointCount(sp); ++i) {
              const spline::Vec3 v = spline::EditablePoint(sp, i);
              grips.push_back({v.x, v.y, v.z});
            }
          }
          break;
        }
        case SelectedEntity::Type::Point: {
          if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.cadPoints.size()) {
            const CadPoint& p = m_State.cadPoints[static_cast<std::size_t>(selected.index)];
            grips.push_back({p.x, p.y, p.z});
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
  }
  if (!paperSpace) DrawQtSurveyPoints(painter, m_State, static_cast<float>(width()), static_cast<float>(height()));
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
  const viewcube::Scene cube = CubeScene();
  PaintViewCube(painter, cube, m_PointerInside ? viewcube::HitTest(cube, m_Last.x(), m_Last.y()) : viewcube::Hit{});
  if (m_Ready && m_PointerInside && !viewcube::Contains(cube, m_Last.x(), m_Last.y())) DrawCursor(painter);
}

// REQ-450: crosshair arms plus a central pickbox; an object-selection step keeps the pickbox only.
void EditorViewport::DrawCursor(QPainter& painter) {
  const ViewportCursorShape shape = ViewportCursorShapeFor(m_State, static_cast<float>(width()), static_cast<float>(height()));
  const QColor color = QColor::fromRgbF(std::clamp(m_State.viewportCrosshairR, 0.f, 1.f),
                                        std::clamp(m_State.viewportCrosshairG, 0.f, 1.f),
                                        std::clamp(m_State.viewportCrosshairB, 0.f, 1.f));
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(QPen(color, 1.0));
  painter.setBrush(Qt::NoBrush);
  const double x = std::floor(m_Last.x()) + 0.5, y = std::floor(m_Last.y()) + 0.5;
  const double half = static_cast<double>(shape.pickHalfPx);
  if (shape.arms) {
    const double arm = static_cast<double>(shape.armLengthPx) * 0.5;
    painter.drawLine(QPointF(x - arm, y), QPointF(x + arm, y));
    painter.drawLine(QPointF(x, y - arm), QPointF(x, y + arm));
  }
  painter.drawRect(QRectF(x - half, y - half, half * 2.0, half * 2.0));
  painter.restore();
}

void EditorViewport::enterEvent(QEnterEvent* event) {
  m_PointerInside = true;
  m_Last = event->position();
  RefreshCommandCursor();
  update();
  QOpenGLWidget::enterEvent(event);
}

void EditorViewport::leaveEvent(QEvent* event) {
  setCursor(m_Ready ? Qt::BlankCursor : Qt::CrossCursor);  // the painted crosshair stands in for the native one
  m_PointerInside = false;
  update();
  QOpenGLWidget::leaveEvent(event);
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
  // REQ-059: a press anywhere on the ViewCube (face, arrow, home or the gaps between) belongs to the
  // widget and never reaches the viewport behind it. Middle stays navigation (pan / orbit).
  if (event->button() != Qt::MiddleButton && viewcube::Contains(CubeScene(), m_Last.x(), m_Last.y())) {
    m_CubePressed = true;
    if (event->button() == Qt::LeftButton) ClickViewCube(m_Last);
    event->accept();
    update();
    return;
  }
  if (event->button() == Qt::LeftButton &&
      (m_State.active == AppCommandState::Kind::Pan || m_State.active == AppCommandState::Kind::Orbit)) {
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton &&
      (m_State.entityGripMoveActive || m_State.dimGripMoveActive || m_State.mtextGripMoveActive)) {
    ClearEntityGripInteraction(m_State); ClearDimGripInteraction(m_State); ClearMtextGripInteraction(m_State);
    m_GripMovedWithButton = false;
    if (changed) changed(); update(); return;
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
    } else if (CancelGripEdit()) {
    } else if (m_State.selBoxWaitingSecond) {
      CancelSelectionBox();
    } else {
      RightPress(event->globalPosition().toPoint());
    }
    if (changed) changed();
    update();
    event->accept();
    return;
  } else if (event->button() == Qt::LeftButton && (WorldPoint(m_Last, m_Cursor) ||
             (m_State.active == AppCommandState::Kind::None && ViewPoint(m_Last, m_Cursor)))) {
    const ray3d::Vec3 rawCursor = m_Cursor;
    const Camera camera = CadViewCamera(m_State);
    auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    const auto overrideKind = static_cast<CadSnap::Kind>(m_State.objectSnapKindOverrideKind);
    const auto* onlyKind = m_State.objectSnapKindOverrideValid ? &overrideKind : nullptr;
    const bool gripActive = m_State.entityGripMoveActive || m_State.dimGripMoveActive || m_State.mtextGripMoveActive;
    CadSnap::SnapExclude exclude{};
    if (m_State.entityGripMoveActive && m_State.entityGripEntityIndex >= 0) {
      exclude.valid = true; exclude.type = m_State.entityGripType; exclude.index = m_State.entityGripEntityIndex;
    }
    const bool paperModifyActive = m_State.paperLengthenPhase || m_State.paperExtendPhase || m_State.paperBreakPhase ||
        m_State.paperStretchPhase || m_State.paperFilletPhase || m_State.paperChamferPhase;
    m_Snap = (m_State.active != AppCommandState::Kind::None || gripActive || paperModifyActive) ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State,
        true, camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), exclude,
        CadViewIsPlan(m_State) ? nullptr : &ray, onlyKind) : CadSnap::Hit{};
    if (m_Snap.valid) m_Cursor = {m_Snap.x, m_Snap.y, m_Snap.z};
    else if (m_State.entityGripMoveActive) {
      float orthoX = static_cast<float>(m_Cursor.x), orthoY = static_cast<float>(m_Cursor.y);
      ApplyOrthoConstrainFromAnchor(m_State, m_State.entityGripAnchorX, m_State.entityGripAnchorY,
          &orthoX, &orthoY, m_State.orthoMode);
      m_Cursor.x = orthoX; m_Cursor.y = orthoY;
    }
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
    if (m_State.active == AppCommandState::Kind::None && paperModifyActive &&
        m_State.activeSpaceIndex >= 0 && !InFloatingModelSpace(m_State)) {
      SubmitViewportPick(m_State, m_Cursor.x, m_Cursor.y, m_Log, event->modifiers() & Qt::ShiftModifier,
          false, CadViewIsPlan(m_State) ? nullptr : &ray);
      if (changed) changed();
      update();
      event->accept();
      return;
    }
    const auto route = ViewportClickRouteFor(m_State);
    if (route == ViewportClickRoute::SelectionBox && m_State.active == AppCommandState::Kind::Zoom &&
        !m_State.selBoxWaitingSecond) {
      m_State.uiCursorWorldZ = static_cast<float>(rawCursor.z);
      BeginSelectionBoxCorner(m_State, static_cast<float>(rawCursor.x), static_cast<float>(rawCursor.y),
          static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()));
      if (changed) changed();
      update();
      event->accept();
      return;
    }
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
      const double pickX = m_State.active == AppCommandState::Kind::Zoom ? rawCursor.x : m_Cursor.x;
      const double pickY = m_State.active == AppCommandState::Kind::Zoom ? rawCursor.y : m_Cursor.y;
      SubmitViewportPick(m_State, pickX, pickY, m_Log, false,
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

void EditorViewport::mouseDoubleClickEvent(QMouseEvent* event) {
  // REQ-120: a middle double-click zooms to extents, even mid-command and even over the ViewCube
  // (a middle press passes through the cube to orbit/pan, so this does too). It writes the camera
  // only (paintGL consumes the flag), so the active command's phase, points and draft are untouched.
  if (event->button() == Qt::MiddleButton) {
    RequestZoomExtents(m_State);
    event->accept();
    update();
    return;
  }
  // Qt delivers a quick second press as a double click; over the ViewCube it is just another click.
  if (viewcube::Contains(CubeScene(), event->position().x(), event->position().y())) {
    mousePressEvent(event);
    return;
  }
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
  const bool leftCommandDrag = (event->buttons() & Qt::LeftButton) &&
      (m_State.active == AppCommandState::Kind::Pan || m_State.active == AppCommandState::Kind::Orbit);
  const bool commandOrbitDrag = leftCommandDrag && m_State.active == AppCommandState::Kind::Orbit;
  const bool middleOrbitDrag = (event->buttons() & Qt::MiddleButton) &&
      (event->modifiers() & Qt::ShiftModifier);
  const bool panDrag = (leftCommandDrag && m_State.active == AppCommandState::Kind::Pan) ||
      ((event->buttons() & Qt::MiddleButton) && !(event->modifiers() & Qt::ShiftModifier));
  if (middleOrbitDrag || (commandOrbitDrag && m_State.activeSpaceIndex == kModelSpaceIndex)) {
    m_State.viewAnimActive = false;  // A manual ORBIT takes over from any ViewCube animation (REQ-059).
    m_State.viewportAzimuthDeg += static_cast<float>(delta.x()) * 0.4f;
    m_State.viewportElevationDeg = std::clamp(m_State.viewportElevationDeg +
        static_cast<float>(delta.y()) * 0.4f, -89.9f, 90.f);
  } else if (panDrag) {
    ray3d::Vec3 before, after;
    if (ViewPoint(m_Last, before) && ViewPoint(event->position(), after)) {
      m_State.viewportPanX += before.x - after.x;
      m_State.viewportPanY += before.y - after.y;
      m_State.viewportPanZ += before.z - after.z;
    }
  }
  m_Last = event->position();
  // Over the ViewCube the pointer is the widget's, not the drawing's: arrow cursor, hover highlight on
  // the control, and nothing picked or snapped behind it. A held button (box select, grip drag) keeps
  // tracking so a gesture that started in the drawing is not interrupted.
  const bool overCube = viewcube::Contains(CubeScene(), m_Last.x(), m_Last.y());
  RefreshCommandCursor();
  if (overCube && event->buttons() == Qt::NoButton) {
    update();
    return;
  }
  if (leftCommandDrag) {
    m_Last = event->position();
    if (changed) changed();
    update();
    event->accept();
    return;
  }
  ClearTransientInput();
  if (WorldPoint(m_Last, m_Cursor)) {
    const Camera camera = CadViewCamera(m_State);
    const bool paperTrimHover = m_State.active == AppCommandState::Kind::Trim &&
        m_State.activeSpaceIndex >= 0 &&
        m_State.activeSpaceIndex < static_cast<int>(m_State.paperLayouts.size()) &&
        !InFloatingModelSpace(m_State);
    if (paperTrimHover) {
      const auto& layout = m_State.paperLayouts[static_cast<std::size_t>(m_State.activeSpaceIndex)];
      m_State.paperHoverValid = PickPaperEntityAt(layout, static_cast<float>(m_Cursor.x),
          static_cast<float>(m_Cursor.y), camera.orthoHalfH * 8.f /
              static_cast<float>(std::max(1, height())), &m_State.paperHover);
    } else {
      m_State.paperHoverValid = false;
    }
    auto ray = camera.ScreenRay(static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()),
        static_cast<float>(width()), static_cast<float>(height()));
    const bool sectionGripDrag = m_State.sectionPlaneGripDrag >= 0;
    const auto overrideKind = static_cast<CadSnap::Kind>(m_State.objectSnapKindOverrideKind);
    const auto* onlyKind = m_State.objectSnapKindOverrideValid ? &overrideKind : nullptr;
    const bool paperModifyActive = m_State.paperLengthenPhase || m_State.paperExtendPhase || m_State.paperBreakPhase ||
        m_State.paperStretchPhase || m_State.paperFilletPhase || m_State.paperChamferPhase;
    m_Snap = (m_State.active != AppCommandState::Kind::None || sectionGripDrag || paperModifyActive)
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
  if (m_CubePressed && event->button() != Qt::MiddleButton) {  // the press was the ViewCube's
    m_CubePressed = false;
    event->accept();
    return;
  }
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
      std::pow(static_cast<double>(m_State.displayWheelZoomFactor), event->angleDelta().y() / 120.0)), 1.e-8f, 1.e8f);
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
    CancelGripEdit();
    if (m_State.selBoxWaitingSecond) CancelSelectionBox();
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
    m_LeftDragSelection = false; m_GripMovedWithButton = false;
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
  else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
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

void EditorViewport::RefreshCommandCursor() {
  const bool overCube = viewcube::Contains(CubeScene(), m_Last.x(), m_Last.y());
  setCursor(overCube ? Qt::ArrowCursor : (m_PointerInside && m_State.active == AppCommandState::Kind::Pan
      ? Qt::OpenHandCursor : (m_Ready ? Qt::BlankCursor : Qt::CrossCursor)));
}

// REQ-059: the widget's orientation changes ease to the target instead of snapping.
void EditorViewport::AnimateToOrientation(float azimuth, float elevation) {
  CadStartViewAnimation(m_State, azimuth, elevation);
  StartViewAnimationClock();
  update();
}

void EditorViewport::StartViewAnimationClock() {
  m_ViewAnimClock.start();
  m_ViewAnimTimer.start();
}

void EditorViewport::StepViewAnimation() {
  if (!m_State.viewAnimActive) { m_ViewAnimTimer.stop(); return; }
  CadTickViewAnimation(m_State, static_cast<float>(m_ViewAnimClock.restart()) / 1000.f);
  if (!m_State.viewAnimActive) m_ViewAnimTimer.stop();
  update();
}

viewcube::Scene EditorViewport::CubeScene() const {
  const viewcube::Point origin = viewcube::OriginFor(static_cast<float>(width()));
  return viewcube::BuildScene(CadViewCamera(m_State), origin.x, origin.y, CadUcsViewAzimuthOffsetDeg(m_State));
}

void EditorViewport::ClickViewCube(QPointF point) {
  const viewcube::Scene scene = CubeScene();
  viewcube::Target target;
  if (viewcube::TargetFor(viewcube::HitTest(scene, point.x(), point.y()), CadViewCamera(m_State),
                          scene.ucsAzimuthOffsetDeg, &target))
    AnimateToOrientation(target.azimuthDeg, target.elevationDeg);
}
