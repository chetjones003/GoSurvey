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
#include <array>

#include <QDateTime>
#include <QKeyEvent>
#include <QLineF>
#include <QInputDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QIcon>
#include <QOpenGLContext>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTextureBlitter>
#include <QOpenGLVertexArrayObject>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cassert>
#include "CadCommands.hpp"
#include "pdf/PdfAttach.hpp"

namespace {

template <class Project>
static QPainterPath PaperViewportShapePath(const Viewport& viewport, Project&& project) {
  QPainterPath path;
  if (viewport.clipBoundary.size() < 3) {
    const QPointF a = project(viewport.paperXIn, viewport.paperYIn);
    const QPointF b = project(viewport.paperXIn + viewport.paperWIn, viewport.paperYIn + viewport.paperHIn);
    path.addRect(QRectF(a, b).normalized());
    return path;
  }
  const auto& first = viewport.clipBoundary.front();
  path.moveTo(project(first.xIn, first.yIn));
  constexpr double kPi = 3.14159265358979323846;
  for (size_t i = 0; i < viewport.clipBoundary.size(); ++i) {
    const Viewport::ClipVertex& a = viewport.clipBoundary[i];
    const Viewport::ClipVertex& b = viewport.clipBoundary[(i + 1) % viewport.clipBoundary.size()];
    const BulgeArcSpan arc = BulgeArc(a.xIn, a.yIn, b.xIn, b.yIn, a.bulge);
    if (!arc.valid) {
      path.lineTo(project(b.xIn, b.yIn));
      continue;
    }
    const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(arc.sweep) / (kPi / 36.0))), 1, 720);
    for (int step = 1; step <= steps; ++step) {
      const double angle = arc.startAngle + arc.sweep * static_cast<double>(step) / steps;
      path.lineTo(project(arc.cx + arc.radius * std::cos(angle), arc.cy + arc.radius * std::sin(angle)));
    }
  }
  path.closeSubpath();
  return path;
}

// RenderScene reads only the raster/transform fields. Snap geometry and lookup grids stay borrowed
// in the committed attachment rather than being copied on each cursor-move frame.
static PdfAttachment PdfRenderCopy(const PdfAttachment& source) {
  PdfAttachment result;
  result.insertX = source.insertX;
  result.insertY = source.insertY;
  result.scale = source.scale;
  result.rotationDeg = source.rotationDeg;
  result.pageWidthPts = source.pageWidthPts;
  result.pageHeightPts = source.pageHeightPts;
  result.glTexId = source.glTexId;
  result.fade = source.fade;
  result.showBackground = source.showBackground;
  result.snapVisDark = source.snapVisDark;
  return result;
}

static void RotatePdfInsertAroundBase(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float dx = *x - bx;
  const float dy = *y - by;
  *x = bx + dx * c - dy * s;
  *y = by + dx * s + dy * c;
}

static void AppendDashedPreviewSegment(std::vector<float>& lines, double ax, double ay, double az,
                                       double bx, double by, double bz, double orthoHalfH, int heightPx) {
  const double dx = bx - ax, dy = by - ay, dz = bz - az;
  const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!(length > 1e-12) || heightPx <= 0)
    return;
  const double worldPerPixel = 2.0 * orthoHalfH / heightPx;
  const double dash = 8.0 * worldPerPixel;
  const double period = 14.0 * worldPerPixel;
  for (double start = 0.0; start < length; start += period) {
    const double end = std::min(length, start + dash);
    const double t0 = start / length, t1 = end / length;
    lines.insert(lines.end(), {static_cast<float>(ax + dx * t0), static_cast<float>(ay + dy * t0),
                               static_cast<float>(az + dz * t0), static_cast<float>(ax + dx * t1),
                               static_cast<float>(ay + dy * t1), static_cast<float>(az + dz * t1)});
  }
}

static void ScalePdfInsertAroundBase(float bx, float by, float sc, float* x, float* y) {
  *x = bx + sc * (*x - bx);
  *y = by + sc * (*y - by);
}

/// REQ-356: dismiss the line-end grip menu after the pointer leaves it for one second.
class GripMenuDismissFilter final : public QObject {
public:
  explicit GripMenuDismissFilter(QMenu* menu) : QObject(menu), m_Menu(menu) {
    m_Timer.setSingleShot(true);
    m_Timer.setInterval(1000);
    m_Timer.setParent(menu);
    connect(&m_Timer, &QTimer::timeout, menu, &QMenu::close);
    menu->installEventFilter(this);
  }
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched != m_Menu)
      return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::Leave)
      m_Timer.start();
    else if (event->type() == QEvent::Enter)
      m_Timer.stop();
    return false;
  }

private:
  QMenu* m_Menu;
  QTimer m_Timer;
};

static void AppendHighlightFloatVerts(const std::vector<float>& batch, std::vector<double>* out) {
  out->reserve(out->size() + batch.size());
  for (float v : batch)
    out->push_back(static_cast<double>(v));
}

static bool SelectionIncludesPdfUnderlay(const AppCommandState& st, int index) {
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::PdfUnderlay && e.index == index)
      return true;
  }
  return false;
}

static void AppendModifyPdfGhostPreviews(const AppCommandState& st, float curX, float curY,
                                         std::vector<PdfAttachment>* ghosts) {
  assert(ghosts != nullptr);
  if (ghosts == nullptr)
    return;
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  const int count = static_cast<int>(st.pdfAttachments.size());
  if (st.active == K::Move || st.active == K::Copy) {
    if (st.modifyPhase != MP::NeedDestination)
      return;
    const float dx = curX - st.modifyBaseX;
    const float dy = curY - st.modifyBaseY;
    for (int pi = 0; pi < count; ++pi) {
      if (!SelectionIncludesPdfUnderlay(st, pi))
        continue;
      PdfAttachment ghost = PdfRenderCopy(st.pdfAttachments[static_cast<std::size_t>(pi)]);
      ghost.insertX += dx;
      ghost.insertY += dy;
      ghosts->push_back(ghost);
    }
    return;
  }
  if (st.active == K::Rotate) {
    float theta = 0.f;
    if (!CadRotatePreviewTheta(st, curX, curY, &theta))
      return;
    const float bx = st.rotateBaseX;
    const float by = st.rotateBaseY;
    constexpr float kPdfRadToDeg = 180.f / 3.14159265f;
    for (int pi = 0; pi < count; ++pi) {
      if (!SelectionIncludesPdfUnderlay(st, pi))
        continue;
      PdfAttachment ghost = PdfRenderCopy(st.pdfAttachments[static_cast<std::size_t>(pi)]);
      RotatePdfInsertAroundBase(bx, by, theta, &ghost.insertX, &ghost.insertY);
      ghost.rotationDeg += theta * kPdfRadToDeg;
      ghosts->push_back(ghost);
    }
    return;
  }
  if (st.active == K::Scale) {
    if (st.modifyPhase != MP::NeedDestination)
      return;
    float sc = 1.f;
    if (!CadScalePreviewFactor(st, curX, curY, &sc))
      return;
    const float bx = st.modifyBaseX;
    const float by = st.modifyBaseY;
    for (int pi = 0; pi < count; ++pi) {
      if (!SelectionIncludesPdfUnderlay(st, pi))
        continue;
      PdfAttachment ghost = PdfRenderCopy(st.pdfAttachments[static_cast<std::size_t>(pi)]);
      ScalePdfInsertAroundBase(bx, by, sc, &ghost.insertX, &ghost.insertY);
      ghost.scale = std::max(ghost.scale * sc, 1e-9f);
      ghosts->push_back(ghost);
    }
  }
}

}  // namespace

EditorViewport::EditorViewport(AppCommandState& state, std::vector<std::string>& log, QWidget* parent)
    : QOpenGLWidget(parent), m_State(state), m_Log(log), m_Renderer(std::make_unique<ViewportRenderer>()) {
  setObjectName("modelViewport");
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumSize(240, 180);
  setCursor(Qt::CrossCursor);
  m_RightTimer.setSingleShot(true);
  m_GripMenuTimer.setSingleShot(true);
  m_GripMenuTimer.setInterval(500);
  connect(&m_GripMenuTimer, &QTimer::timeout, this, [this] {
    const int entityIndex = m_State.gripMenuHoverEntityIndex;
    const int which = m_State.gripMenuHoverWhich;
    if (entityIndex < 0 || which < 0 || m_State.active != AppCommandState::Kind::None ||
        m_State.selection.empty() || !m_PointerInside) return;
    const std::uint64_t projectUid = m_State.activeDrawingIdx > 0 &&
        m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size())
        ? m_State.drawingTabs[static_cast<size_t>(m_State.activeDrawingIdx)].projectUid : 0u;
    const bool readOnly = projectUid && std::any_of(m_State.openProjects.begin(), m_State.openProjects.end(),
        [projectUid](const auto& project) { return project.uid == projectUid && project.readOnly; });
    if (readOnly) return;
    const size_t k = static_cast<size_t>(entityIndex) * 6;
    if (k + 5 >= m_State.userLinesFlat.size()) return;
    const float gx = static_cast<float>(m_State.userLinesFlat[k + (which == 0 ? 0 : 3)]);
    const float gy = static_cast<float>(m_State.userLinesFlat[k + (which == 0 ? 1 : 4)]);
    m_State.gripMenuPendingEntityIndex = entityIndex;
    m_State.gripMenuPendingWhich = which;
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* lengthen = menu->addAction("Lengthen");
    QAction* stretch = menu->addAction("Stretch");
    const auto arm = [this, entityIndex, gx, gy](bool alongLine) {
      const Camera camera = CadViewCamera(m_State);
      const float tolerance = camera.orthoHalfH * 12.f / static_cast<float>(std::max(1, height()));
      if (!TryBeginEntityGripAtLocal(m_State, gx, gy, tolerance) ||
          m_State.entityGripType != SelectedEntity::Type::LineSeg ||
          m_State.entityGripEntityIndex != entityIndex) return;
      m_State.entityGripLengthenMode = alongLine;
      if (alongLine) {
        const size_t line = static_cast<size_t>(entityIndex) * 6;
        const bool startGrip = m_State.entityGripWhich == 0;
        const float dx = static_cast<float>(m_State.userLinesFlat[line + (startGrip ? 0 : 3)] -
                                            m_State.userLinesFlat[line + (startGrip ? 3 : 0)]);
        const float dy = static_cast<float>(m_State.userLinesFlat[line + (startGrip ? 1 : 4)] -
                                            m_State.userLinesFlat[line + (startGrip ? 4 : 1)]);
        const float length = std::hypot(dx, dy);
        if (length > 1e-8f) {
          m_State.entityGripLengthenDirX = dx / length;
          m_State.entityGripLengthenDirY = dy / length;
        }
      }
      m_GripMovedWithButton = false;
      if (changed) changed();
      update();
    };
    connect(lengthen, &QAction::triggered, this, [arm] { arm(true); });
    connect(stretch, &QAction::triggered, this, [arm] { arm(false); });
    connect(menu, &QMenu::aboutToHide, this, [this] {
      m_State.gripMenuPendingEntityIndex = -1;
      m_State.gripMenuPendingWhich = -1;
    });
    new GripMenuDismissFilter(menu);
    menu->popup(QCursor::pos());
  });
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
  if (!context() || m_CleanupComplete) return;
  m_MapTimer.stop();
  m_ViewAnimTimer.stop();
  makeCurrent();
  if (m_TextureBlitter && m_TextureBlitter->isCreated()) m_TextureBlitter->destroy();
  if (m_ClipMaskVao && m_ClipMaskVao->isCreated()) m_ClipMaskVao->destroy();
  if (m_ClipMaskBuffer && m_ClipMaskBuffer->isCreated()) m_ClipMaskBuffer->destroy();
  m_TextureBlitter.reset();
  m_ClipMaskVao.reset();
  m_ClipMaskBuffer.reset();
  m_ClipMaskProgram.reset();
  m_OnlineMap.reset();
  for (const auto& [path, texture] : m_RasterTextures) {
    (void)path;
    if (texture) ViewportRenderer::DeleteMapTileTexture(texture);
  }
  m_RasterTextures.clear();
  m_RasterDrapeCache.clear();
  m_RenderTiles.clear();
  if (m_Ready && m_Renderer) m_Renderer->Shutdown();
  m_Ready = false;
  m_CleanupComplete = true;
  doneCurrent();
}

void EditorViewport::initializeGL() {
  m_CleanupComplete = false;
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    m_Error = "OpenGL initialization failed.";
    return;
  }
  while (glGetError() != GL_NO_ERROR) {}
  std::string error;
  m_Ready = m_Renderer->Init(&error);
  if (m_Ready) {
    m_TextureBlitter = std::make_unique<QOpenGLTextureBlitter>();
    m_ClipMaskProgram = std::make_unique<QOpenGLShaderProgram>();
    m_ClipMaskBuffer = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_ClipMaskVao = std::make_unique<QOpenGLVertexArrayObject>();
    const char* vertex = "#version 330 core\nlayout(location=0) in vec2 position;\n"
                         "void main(){ gl_Position=vec4(position,0.0,1.0); }\n";
    const char* fragment = "#version 330 core\nout vec4 color;\nvoid main(){ color=vec4(0.0); }\n";
    const bool ready = m_TextureBlitter->create() &&
        m_ClipMaskProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) &&
        m_ClipMaskProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) &&
        m_ClipMaskProgram->link() && m_ClipMaskBuffer->create() && m_ClipMaskVao->create();
    if (ready) {
      m_ClipMaskVao->bind();
      m_ClipMaskBuffer->bind();
      m_ClipMaskProgram->bind();
      m_ClipMaskProgram->enableAttributeArray(0);
      m_ClipMaskProgram->setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(float) * 2);
      m_ClipMaskProgram->release();
      m_ClipMaskBuffer->release();
      m_ClipMaskVao->release();
    } else {
      error = "Paper viewport clip renderer initialization failed.";
      m_Ready = false;
      m_Renderer->Shutdown();
    }
  }
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

bool EditorViewport::DrawPaperViewportClipMask(const QPainterPath& path, int framebufferWidth, int framebufferHeight) {
  if (!m_ClipMaskProgram || !m_ClipMaskBuffer || !m_ClipMaskVao || framebufferWidth <= 0 || framebufferHeight <= 0)
    return false;
  std::vector<float> vertices;
  const QList<QPolygonF> polygons = path.toFillPolygons();
  const float dpr = static_cast<float>(devicePixelRatioF());
  auto pushPoint = [&](const QPointF& point) {
    const float x = static_cast<float>(point.x()) * dpr;
    const float y = static_cast<float>(point.y()) * dpr;
    vertices.push_back(2.f * x / static_cast<float>(framebufferWidth) - 1.f);
    vertices.push_back(1.f - 2.f * y / static_cast<float>(framebufferHeight));
  };
  for (QPolygonF polygon : polygons) {
    if (polygon.size() > 1 && QLineF(polygon.front(), polygon.back()).length() < 1.e-4)
      polygon.removeLast();
    if (polygon.size() < 3)
      continue;
    auto cross = [](const QPointF& a, const QPointF& b, const QPointF& c) {
      return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
    };
    double twiceArea = 0.0;
    for (int i = 0; i < polygon.size(); ++i) {
      const QPointF& a = polygon[i];
      const QPointF& b = polygon[(i + 1) % polygon.size()];
      twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    if (std::fabs(twiceArea) < 1.e-6)
      continue;
    const double orientation = twiceArea > 0.0 ? 1.0 : -1.0;
    std::vector<int> remaining;
    remaining.reserve(static_cast<size_t>(polygon.size()));
    for (int i = 0; i < polygon.size(); ++i)
      remaining.push_back(i);
    auto insideTriangle = [&](const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) {
      const double ab = orientation * cross(a, b, p);
      const double bc = orientation * cross(b, c, p);
      const double ca = orientation * cross(c, a, p);
      return ab >= -1.e-6 && bc >= -1.e-6 && ca >= -1.e-6;
    };
    size_t guard = 0;
    while (remaining.size() > 3 && guard++ < static_cast<size_t>(polygon.size()) * polygon.size()) {
      bool clipped = false;
      for (size_t i = 0; i < remaining.size(); ++i) {
        const int ia = remaining[(i + remaining.size() - 1) % remaining.size()];
        const int ib = remaining[i];
        const int ic = remaining[(i + 1) % remaining.size()];
        const QPointF& a = polygon[ia];
        const QPointF& b = polygon[ib];
        const QPointF& c = polygon[ic];
        if (orientation * cross(a, b, c) <= 1.e-7)
          continue;
        bool containsVertex = false;
        for (int candidate : remaining) {
          if (candidate == ia || candidate == ib || candidate == ic)
            continue;
          if (insideTriangle(polygon[candidate], a, b, c)) {
            containsVertex = true;
            break;
          }
        }
        if (containsVertex)
          continue;
        pushPoint(a);
        pushPoint(b);
        pushPoint(c);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
        clipped = true;
        break;
      }
      if (!clipped)
        break;
    }
    if (remaining.size() != 3)
      return false;
    if (remaining.size() == 3) {
      pushPoint(polygon[remaining[0]]);
      pushPoint(polygon[remaining[1]]);
      pushPoint(polygon[remaining[2]]);
    }
  }
  if (vertices.empty())
    return false;

  glDisable(GL_DEPTH_TEST);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); // ear clipping yields non-overlapping concave triangles
  m_ClipMaskVao->bind();
  m_ClipMaskBuffer->bind();
  m_ClipMaskBuffer->allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
  m_ClipMaskProgram->bind();
  m_ClipMaskProgram->enableAttributeArray(0);
  m_ClipMaskProgram->setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(float) * 2);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 2));
  m_ClipMaskProgram->release();
  m_ClipMaskBuffer->release();
  m_ClipMaskVao->release();
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilMask(0x00);
  glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  return true;
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
  m_IdleBoxFirstClickPending = false;
  if (changed) changed();
  update();
}

// Escape or right-click during a grip drag puts the object back exactly as it was. Returns whether a grip
// edit was in progress, so the caller can stop there rather than also cancelling the command.
bool EditorViewport::CancelGripEdit() {
  if (m_State.alignLabelDragActive) {  // REQ-492.5: put a dragged station label back where it was
    std::vector<std::string> cancelLog;
    FinishAlignmentLabelDrag(m_State, false, cancelLog);
    return true;
  }
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
      CadDimRefreshMeasurementText(&ann, CadDimensionStyleForAnnotation(m_State, ann), CadAngleDisplaySettings(m_State));
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
    std::vector<float> rubber, preview, previewCircles, highlightCircles;
    std::vector<double> highlight;
    AppendCadDraftRubberLines(m_State, m_Cursor.x, m_Cursor.y, m_State.orthoMode,
        camera.targetX, camera.targetY, camera.orthoHalfH, h, rubber);
    std::vector<float> trimRemovalLines;
    if (m_State.active == AppCommandState::Kind::Trim &&
        m_State.trimPhase == AppCommandState::TrimPhase::CuttingLine_WaitP2 &&
        (m_State.activeSpaceIndex == kModelSpaceIndex || InFloatingModelSpace(m_State))) {
      float p2x = static_cast<float>(m_Cursor.x);
      float p2y = static_cast<float>(m_Cursor.y);
      float p2z = m_State.viewportSnapPickValid ? static_cast<float>(m_State.viewportSnapPickLocalZ)
                                                : m_State.uiCursorWorldZ;
      const bool trim3d = !CadViewIsPlan(m_State);
      if (trim3d) {
        ApplyOrthoConstrainFromAnchor(m_State, m_State.trimCutInfP1x, m_State.trimCutInfP1y,
            &p2x, &p2y, m_State.orthoMode, m_State.trimCutInfP1z, p2z, &p2z);
      } else if (m_State.orthoMode) {
        const float dx = p2x - m_State.trimCutInfP1x;
        const float dy = p2y - m_State.trimCutInfP1y;
        if (std::fabs(dx) >= std::fabs(dy)) p2y = m_State.trimCutInfP1y;
        else p2x = m_State.trimCutInfP1x;
      }
      AppendDashedPreviewSegment(rubber, m_State.trimCutInfP1x, m_State.trimCutInfP1y,
          m_State.trimCutInfP1z, p2x, p2y, p2z, camera.orthoHalfH, h);
      if (trim3d) {
        CadTrimAppendCutLineRemovedPreview3D(m_State,
            {m_State.trimCutInfP1x, m_State.trimCutInfP1y, m_State.trimCutInfP1z},
            {p2x, p2y, p2z}, &trimRemovalLines);
      } else {
        CadTrimAppendCutLineRemovedPreview(m_State, m_State.trimCutInfP1x, m_State.trimCutInfP1y,
            p2x, p2y, .5f * (m_State.trimCutInfP1x + p2x),
            .5f * (m_State.trimCutInfP1y + p2y), &trimRemovalLines);
      }
    }
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
    AppendHighlightFloatVerts(subObjectLines, &highlight);
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
    ext.alignments = &m_State.alignments;
    ext.viewFrameGroups = &m_State.viewFrameGroups;
    ext.alignmentStyles = &m_State.alignmentStyles;
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
    m_RenderTiles = m_OnlineMap->DrawList();
    if (m_RasterDrapeRevision != m_State.cadGpuCacheRevision ||
        m_RasterDrapeRenderRevision != m_RenderRevision) {
      m_RasterDrapeCache.clear();
      m_RasterDrapeRevision = m_State.cadGpuCacheRevision;
      m_RasterDrapeRenderRevision = m_RenderRevision;
      for (const CadRasterImage& image : m_State.cadRasterImages) {
        if (image.surfaceId == 0 || image.id == 0 || image.width <= 0.f || image.height <= 0.f) continue;
        const CadLayerRow* imageLayer = FindDrawingLayerRowCi(m_State, image.layer);
        if (imageLayer && (!imageLayer->on || imageLayer->frozen)) continue;
        const auto surfaceIt = std::find_if(m_State.cadSurfaceAttrs.begin(), m_State.cadSurfaceAttrs.end(),
            [&](const EntityAttributes& attr) { return attr.id == image.surfaceId; });
        if (surfaceIt == m_State.cadSurfaceAttrs.end()) continue;
        const size_t surfaceIndex = static_cast<size_t>(surfaceIt - m_State.cadSurfaceAttrs.begin());
        if (surfaceIndex >= m_State.cadSurfaces.size() || !m_State.cadSurfaces[surfaceIndex].tin) continue;
        unsigned int texture = 0;
        const auto cachedTexture = m_RasterTextures.find(image.path);
        if (cachedTexture != m_RasterTextures.end()) texture = cachedTexture->second;
        else {
          QImage decoded(QString::fromUtf8(image.path.c_str()));
          if (decoded.isNull()) {
            m_Log.push_back("Drape Image — attached image \"" + image.path + "\" is unavailable.");
            continue;
          }
          decoded = decoded.convertToFormat(QImage::Format_RGBA8888);
          std::vector<unsigned char> rgba(static_cast<size_t>(decoded.width()) * decoded.height() * 4u);
          for (int row = 0; row < decoded.height(); ++row)
            std::copy_n(decoded.constScanLine(row), static_cast<size_t>(decoded.width()) * 4u,
                        rgba.begin() + static_cast<std::ptrdiff_t>(row) * decoded.width() * 4);
          texture = ViewportRenderer::CreateMapTileTexture(rgba, decoded.width(), decoded.height());
          if (!texture) continue;
          m_RasterTextures.emplace(image.path, texture);
        }
        RasterDrapeCache cache;
        cache.imageId = image.id;
        cache.texture = texture;
        const CadTin& tin = *m_State.cadSurfaces[surfaceIndex].tin;
        const double c = std::cos(image.rotationRad), s = std::sin(image.rotationRad);
        struct ClipVertex { double u, v, z; };
        auto clip = [&](std::vector<ClipVertex> polygon, int edge) {
          std::vector<ClipVertex> output;
          if (polygon.empty()) return output;
          auto distance = [&](const ClipVertex& p) {
            switch (edge) {
              case 0: return p.u;
              case 1: return static_cast<double>(image.width) - p.u;
              case 2: return p.v;
              default: return static_cast<double>(image.height) - p.v;
            }
          };
          ClipVertex previous = polygon.back();
          double dp = distance(previous);
          for (const ClipVertex& current : polygon) {
            const double dc = distance(current);
            const bool pin = dp >= -1.e-9, cin = dc >= -1.e-9;
            if (pin != cin) {
              const double t = dp / (dp - dc);
              output.push_back({previous.u + (current.u - previous.u) * t,
                                previous.v + (current.v - previous.v) * t,
                                previous.z + (current.z - previous.z) * t});
            }
            if (cin) output.push_back(current);
            previous = current; dp = dc;
          }
          return output;
        };
        const auto& vertices = tin.vertsXyz;
        const auto& indices = tin.indices;
        for (size_t ti = 0; ti + 2 < indices.size(); ti += 3) {
          std::vector<ClipVertex> polygon;
          polygon.reserve(7);
          for (int k = 0; k < 3; ++k) {
            const size_t ix = static_cast<size_t>(indices[ti + static_cast<size_t>(k)]) * 3;
            const double dx = vertices[ix] - image.insX, dy = vertices[ix + 1] - image.insY;
            polygon.push_back({dx * c + dy * s, -dx * s + dy * c, vertices[ix + 2]});
          }
          for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) polygon = clip(std::move(polygon), edge);
          for (size_t k = 1; k + 1 < polygon.size(); ++k) {
            for (const ClipVertex& p : {polygon[0], polygon[k], polygon[k + 1]}) {
              const double x = image.insX + p.u * c - p.v * s;
              const double y = image.insY + p.u * s + p.v * c;
              cache.xyuv.insert(cache.xyuv.end(), {x, y, p.z, p.u / image.width, 1.0 - p.v / image.height});
            }
          }
        }
        if (!cache.xyuv.empty()) m_RasterDrapeCache.push_back(std::move(cache));
      }
    }
    for (const RasterDrapeCache& cache : m_RasterDrapeCache)
      m_RenderTiles.push_back({cache.texture, &cache.xyuv, true});
    tuning.mapTiles = &m_RenderTiles;
    m_Renderer->SetSize(w, h);
    std::vector<PdfAttachment> pdfForRender;
    const std::vector<PdfAttachment>* pdfAttachments = &m_State.pdfAttachments;
    const bool placingPdf = m_State.active == AppCommandState::Kind::PdfAttach &&
        m_State.pdfAttachPhase == AppCommandState::PdfAttachPhase::WaitInsertPoint &&
        m_State.pdfAttachPreviewReady;
    const bool modifyingPdf = (m_State.active == AppCommandState::Kind::Move ||
        m_State.active == AppCommandState::Kind::Copy || m_State.active == AppCommandState::Kind::Scale ||
        m_State.active == AppCommandState::Kind::Rotate) &&
        std::any_of(m_State.selection.begin(), m_State.selection.end(), [](const SelectedEntity& e) {
          return e.type == SelectedEntity::Type::PdfUnderlay;
        });
    if (placingPdf || modifyingPdf) {
      pdfForRender.reserve(m_State.pdfAttachments.size() * 2 + 1);
      for (const auto& attachment : m_State.pdfAttachments)
        pdfForRender.push_back(PdfRenderCopy(attachment));
      if (placingPdf) {
        PdfAttachment ghost = PdfRenderCopy(m_State.pdfAttachPreview);
        ghost.insertX = static_cast<float>(m_Cursor.x);
        ghost.insertY = static_cast<float>(m_Cursor.y);
        ghost.scale = m_State.pdfAttachScale;
        ghost.rotationDeg = m_State.pdfAttachRotDeg;
        pdfForRender.push_back(std::move(ghost));
      }
      AppendModifyPdfGhostPreviews(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
                                   &pdfForRender);
      pdfAttachments = &pdfForRender;
    }
    m_Renderer->RenderScene(camera, w, h, m_State.userLinesFlat, m_State.userCirclesCxCyZR,
        m_State.cadGpuCacheRevision + m_RenderRevision, rubber, &m_Snap, m_State.objectSnapGlyphHalfPx,
        &preview, &previewCircles, &highlight, &highlightCircles, &subHoverLines, nullptr, nullptr,
        &m_State.userLineAttrs, &m_State.userCircleAttrs, &ext, m_Grid, &m_State.drawingLayerTable, tuning,
        pdfAttachments, -1, &m_State.cadFilledRegions, &m_State.cadFilledRegionAttrs,
        &m_State.cadMeshes, &m_State.cadMeshAttrs, &m_State.solidDisplayGeometry,
        &m_State.surfaceDisplayGeometry, nullptr, &trimRemovalLines, nullptr, nullptr, &subObjectOverlay,
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
    ext.alignments = &m_State.alignments;
    ext.viewFrameGroups = &m_State.viewFrameGroups;
    ext.alignmentStyles = &m_State.alignmentStyles;
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
      if (!viewport.on) continue;  // REQ-482: leave the paper frame, but do not render model contents.
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
      ext.viewportFrozenLayers = &viewport.frozenLayers;
      m_Renderer->SetSize(viewW, viewH);
      m_Renderer->RenderScene(camera, viewW, viewH, m_State.userLinesFlat, m_State.userCirclesCxCyZR,
          m_State.cadGpuCacheRevision + m_RenderRevision, {}, nullptr, 6.f, nullptr, nullptr, nullptr, nullptr,
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
      if (viewport.clipBoundary.size() >= 3 && m_TextureBlitter) {
        const QPainterPath clipPath = PaperViewportShapePath(viewport, paperPoint);
        if (DrawPaperViewportClipMask(clipPath, w, h)) {
          const QRectF target(static_cast<double>(left), static_cast<double>(bottom),
                              static_cast<double>(viewW), static_cast<double>(viewH));
          const QMatrix4x4 transform = QOpenGLTextureBlitter::targetTransform(target, QRect(0, 0, w, h));
          m_TextureBlitter->bind();
          m_TextureBlitter->blit(m_Renderer->ColorTexture(), transform, QOpenGLTextureBlitter::OriginBottomLeft);
          m_TextureBlitter->release();
        }
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
      } else {
        glBlitFramebuffer(0, 0, viewW, viewH, left, bottom, right, top, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      }
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
      if (viewport.on)
        viewportHoles.addPath(PaperViewportShapePath(viewport, project));
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
      const bool closed = p < layout.paperPolyClosed.size() && layout.paperPolyClosed[p] != 0;
      QPolygonF polyline;
      if (begin < end) {
        const std::size_t first = static_cast<std::size_t>(begin) * 3;
        if (first + 1 < layout.paperPolyVerts.size())
          polyline << project(layout.paperPolyVerts[first], layout.paperPolyVerts[first + 1]);
      }
      const int nVert = end - begin;
      const int nSeg = closed ? nVert : nVert - 1;
      for (int s = 0; s < nSeg; ++s) {
        const int ia = begin + s;
        const int ib = begin + ((s + 1) % nVert);
        const std::size_t a = static_cast<std::size_t>(ia) * 3;
        const std::size_t b = static_cast<std::size_t>(ib) * 3;
        if (b + 1 >= layout.paperPolyVerts.size())
          break;
        const float bulge = static_cast<std::size_t>(ia) < layout.paperPolyVertsBulge.size()
                                ? layout.paperPolyVertsBulge[static_cast<std::size_t>(ia)]
                                : 0.f;
        ForEachBulgeChord(layout.paperPolyVerts[a], layout.paperPolyVerts[a + 1], layout.paperPolyVerts[b],
                          layout.paperPolyVerts[b + 1], bulge, 16, [&](double, double, double bx, double by) {
                            polyline << project(bx, by);
                          });
      }
      if (polyline.size() > 1)
        painter.drawPolyline(polyline);
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
      const float arrowIn = std::max(m_State.activeMultileaderStyle.arrowSizeInches, 0.01f);
      auto drawPath = [&](const std::vector<float>& pts) {
        for (std::size_t i = 0; i + 5 < pts.size(); i += 3)
          painter.drawLine(project(pts[i], pts[i + 1]), project(pts[i + 3], pts[i + 4]));
        const CadLeaderArrowHead arrow = CadMultileaderArrow(leader, pts, arrowIn);
        if (!arrow.valid)
          return;
        painter.setBrush(QColor("#252a30"));
        painter.drawPolygon(QPolygonF{project(arrow.tipX, arrow.tipY), project(arrow.b1x, arrow.b1y),
                                      project(arrow.b2x, arrow.b2y)});
        painter.setBrush(Qt::NoBrush);
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
      const QPainterPath viewportPath = PaperViewportShapePath(viewport, project);
      const QRectF clippedViewport = viewportRect.normalized().intersected(sheetRect).intersected(rect());
      painter.save();
      painter.setClipPath(viewportPath);
      painter.setClipRect(clippedViewport, Qt::IntersectClip);
      if (viewport.on) {
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
      }
      painter.restore();
      painter.setPen(QPen(QColor("#4d738b"), 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(viewportPath);
    }
    painter.restore();
  } else {
    DrawQtModelAnnotations(painter, m_State, static_cast<float>(width()), static_cast<float>(height()));
    DrawQtTransformAnnotationPreviews(painter, m_State, static_cast<float>(m_Cursor.x),
        static_cast<float>(m_Cursor.y), static_cast<float>(width()), static_cast<float>(height()));
    const Camera gripCamera = CadViewCamera(m_State);
    // Objects picked for Create Alignment from Objects are highlighted, not offered for grip editing (as Civil 3D).
    const bool pickingSources = m_State.active == AppCommandState::Kind::AlignmentFromObjects;
    for (const SelectedEntity& selected : m_State.selection) {
      if (pickingSources) break;
      std::vector<ray3d::Vec3> grips;
      // REQ-487.1: alignment grips carry their own id and Civil 3D's shape (0 square, 1 circle, 2 triangle).
      std::vector<int> gripIds;
      std::vector<int> gripShapes;
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
        case SelectedEntity::Type::Alignment: {  // REQ-487.1
          if (selected.index >= 0 && static_cast<std::size_t>(selected.index) < m_State.alignments.size()) {
            for (const alignment::Grip& g : alignment::GripPoints(m_State.alignments[static_cast<std::size_t>(selected.index)])) {
              grips.push_back({g.point.x, g.point.y, 0.0});
              gripIds.push_back(g.which);
              using GK = alignment::GripKind;
              const bool circle = g.kind == GK::CurvePc || g.kind == GK::CurvePt || g.kind == GK::CurveMid ||
                                  g.kind == GK::CurveCentre || g.kind == GK::SpiralMid;
              gripShapes.push_back(g.kind == GK::Pi ? 2 : circle ? 1 : 0);
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
      if (selected.type == SelectedEntity::Type::Alignment && selected.index >= 0 &&
          static_cast<std::size_t>(selected.index) < m_State.alignments.size()) {
        // Like Civil 3D: dashed tangent extensions from each Free curve's PC and PT to its PI.
        const alignment::Alignment& al = m_State.alignments[static_cast<std::size_t>(selected.index)];
        painter.setPen(QPen(QColor("#3b82f6"), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        for (std::size_t e = 0; e < al.Entities().size(); ++e) {
          alignment::CurveData cd;
          if (al.Entities()[e].constraint != alignment::Entity::Constraint::Free ||
              !al.CurveDataFor(static_cast<int>(e), &cd) || !cd.hasPI)
            continue;
          std::array<QPointF, 3> s{};
          const std::array<alignment::Point2, 3> w{cd.pc, cd.pi, cd.pt};
          for (std::size_t k = 0; k < w.size(); ++k) {
            float sx = 0.f, sy = 0.f;
            gripCamera.WorldToScreen(w[k].x, w[k].y, 0.0, static_cast<float>(width()), static_cast<float>(height()), &sx, &sy);
            s[k] = QPointF(sx, sy);
          }
          painter.drawPolyline(s.data(), static_cast<int>(s.size()));
        }
      }
      for (std::size_t i = 0; i < grips.size(); ++i) {
        float x = 0.f, y = 0.f;
        gripCamera.WorldToScreen(grips[i].x, grips[i].y, grips[i].z,
            static_cast<float>(width()), static_cast<float>(height()), &x, &y);
        if (!std::isfinite(x) || !std::isfinite(y)) continue;
        const int gripId = i < gripIds.size() ? gripIds[i] : static_cast<int>(i);
        const int shape = i < gripShapes.size() ? gripShapes[i] : 0;
        const bool entityHot = m_State.entityGripMoveActive && m_State.entityGripType == selected.type &&
            m_State.entityGripEntityIndex == selected.index && m_State.entityGripWhich == gripId;
        const bool dimHot = selected.type == SelectedEntity::Type::Annotation && m_State.dimGripMoveActive &&
            m_State.dimGripAnnotationIndex == selected.index && m_State.dimGripWhich == static_cast<int>(i);
        const bool mtextHot = selected.type == SelectedEntity::Type::Annotation && m_State.mtextGripMoveActive &&
            m_State.mtextGripAnnotationIndex == selected.index && m_State.mtextGripCorner == static_cast<int>(i);
        const bool hot = entityHot || dimHot || mtextHot;
        painter.setPen(QPen(Qt::white, 1));
        painter.setBrush(hot ? QColor("#f57842") : QColor("#3b82f6"));
        if (shape == 1) {
          painter.drawEllipse(QPointF(x, y), 4.5, 4.5);
        } else if (shape == 2) {
          const std::array<QPointF, 3> tri{QPointF(x, y - 6), QPointF(x - 5.5, y + 4), QPointF(x + 5.5, y + 4)};
          painter.drawPolygon(tri.data(), static_cast<int>(tri.size()));
        } else {
          painter.drawRect(QRectF(x - 4, y - 4, 8, 8));
        }
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
  } else if (!paperSpace && inputPrompt.haveBase && m_State.orthoMode) {
    const ray3d::Vec3 anchor{inputPrompt.baseWorld.x - m_State.worldDocumentOriginX,
        inputPrompt.baseWorld.y - m_State.worldDocumentOriginY, inputPrompt.baseWorld.z};
    float x = static_cast<float>(m_Cursor.x), y = static_cast<float>(m_Cursor.y), z = static_cast<float>(m_Cursor.z);
    ApplyOrthoConstrainFromAnchor(m_State, static_cast<float>(anchor.x), static_cast<float>(anchor.y), &x, &y,
                                  true, static_cast<float>(anchor.z), z, &z);
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
  double x = std::floor(m_Last.x()) + 0.5;
  double y = std::floor(m_Last.y()) + 0.5;
  if (m_Snap.valid && !ViewportIsObjectSelectionStep(m_State)) {
    const Camera camera = CadViewCamera(m_State);
    float sx = 0.f, sy = 0.f;
    camera.WorldToScreen(m_Cursor.x, m_Cursor.y, m_Cursor.z,
        static_cast<float>(width()), static_cast<float>(height()), &sx, &sy);
    if (std::isfinite(sx) && std::isfinite(sy)) {
      x = std::floor(static_cast<double>(sx)) + 0.5;
      y = std::floor(static_cast<double>(sy)) + 0.5;
    }
  }
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

void EditorViewport::CommitPendingIdleSelectionClick(Qt::KeyboardModifiers modifiers) {
  if (!m_PendingIdleSelectionClick)
    return;
  const ViewportPickResult& hit = m_PendingIdlePick;
  if (hit.family == ViewportPickFamily::None)
    return;
  const SelectedEntity picked = hit.entity;
  if (hit.family == ViewportPickFamily::Solid) {
    m_State.subObjectSelection.clear();
    const auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(),
        [&](const SelectedEntity& entry) { return entry.type == hit.entity.type && entry.index == hit.entity.index; });
    if (modifiers & Qt::ShiftModifier) {
      if (it != m_State.selection.end()) m_State.selection.erase(it);
      else m_State.selection.push_back(hit.entity);
    } else {
      ClearCadSelection(m_State);
      m_State.selectedSurveyPointIndices.clear();
      m_State.selection.push_back(hit.entity);
    }
  } else if (hit.family == ViewportPickFamily::Annotation && hit.entity.index >= 0 &&
             static_cast<std::size_t>(hit.entity.index) < m_State.cadAnnotations.size()) {
    const CadAnnotation& annotation = m_State.cadAnnotations[static_cast<std::size_t>(hit.entity.index)];
    const bool linked = annotation.kind == CadAnnotation::Kind::Mtext && annotation.surveyPointLabelForId >= 0;
    if (modifiers & Qt::ShiftModifier) {
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
    if (modifiers & Qt::ShiftModifier) {
      if (it != m_State.selection.end()) m_State.selection.erase(it);
      else m_State.selection.push_back(picked);
    } else {
      ClearCadSelection(m_State);
      m_State.selectedSurveyPointIndices.clear();
      m_State.selection.push_back(picked);
    }
  }
  if (changed) changed();
}

void EditorViewport::mousePressEvent(QMouseEvent* event) {
  setFocus();
  if (event->button() == Qt::LeftButton) {
    m_PendingIdleSelectionClick = false;
    m_IdleBoxFirstClickPending = false;
  }
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
    const bool objectSelectionStep = ViewportIsObjectSelectionStep(m_State);
    const bool createPointsPlacement = m_State.showCreatePointsWindow &&
                                       m_State.active == AppCommandState::Kind::None;
    const bool midCmdUsesSnap = (m_State.active != AppCommandState::Kind::None || gripActive || paperModifyActive ||
                                 createPointsPlacement) && !objectSelectionStep;
    m_Snap = midCmdUsesSnap ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State,
        true, camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), exclude,
        CadViewIsPlan(m_State) ? nullptr : &ray, onlyKind) : CadSnap::Hit{};
    if (gripActive && !objectSelectionStep) {
      const CadSnap::Hit gripSnap = CadSnap::FindGripSnap(m_Cursor.x, m_Cursor.y, m_State,
          camera.orthoHalfH * m_State.objectSnapAperturePx / static_cast<float>(std::max(1, height())));
      if (gripSnap.valid && (!m_Snap.valid || CadSnap::Priority(gripSnap.kind) > CadSnap::Priority(m_Snap.kind)))
        m_Snap = gripSnap;
    }
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
    m_State.alignmentCursorReadout = AlignmentCursorReadout(m_State, m_Cursor.x, m_Cursor.y, camera.orthoHalfH * 0.1);
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
    if (m_State.active == AppCommandState::Kind::None && !(event->modifiers() & Qt::ShiftModifier) &&
        !projectReadOnly() &&
        TryBeginAlignmentLabelDrag(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y),
                                   camera.orthoHalfH * 12.f / static_cast<float>(std::max(1, height())))) {
      m_GripMovedWithButton = false;  // REQ-492.5: a station label was grabbed, so the drag moves it
      if (changed) changed();
      update();
      return;
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
    } else if (route == ViewportClickRoute::SelectionAccumulate) {
      const bool shift = event->modifiers() & Qt::ShiftModifier;
      const bool ctrl = event->modifiers() & Qt::ControlModifier;
      const bool fenceWindow = m_Last.x() >= m_State.selBoxAnchorScreenX;
      if (m_State.selBoxWaitingSecond) {
        if (SubmitViewportSelectionAccumulateClick(
                m_State, rawCursor.x, rawCursor.y, static_cast<float>(m_Last.x()),
                static_cast<float>(m_Last.y()), shift, ctrl, CadViewIsPlan(m_State) ? nullptr : &ray,
                static_cast<float>(width()), static_cast<float>(height()), camera.orthoHalfH, m_Log, false)) {
          CancelSelectionBox();
        } else {
          SubmitViewportPick(m_State, m_Cursor.x, m_Cursor.y, m_Log, shift, fenceWindow,
              CadViewIsPlan(m_State) ? nullptr : &ray);
        }
        m_LeftDragSelection = false;
        m_HasSelectionBoxSnapshot = false;
        m_SelectionBeforeBox.clear();
        m_SurveySelectionBeforeBox.clear();
      } else if (SubmitViewportSelectionAccumulateClick(
                     m_State, rawCursor.x, rawCursor.y, static_cast<float>(m_Last.x()),
                     static_cast<float>(m_Last.y()), shift, ctrl, CadViewIsPlan(m_State) ? nullptr : &ray,
                     static_cast<float>(width()), static_cast<float>(height()), camera.orthoHalfH, m_Log)) {
        m_State.selBoxWaitingSecond = false;
        m_LeftDragSelection = false;
      } else if (m_State.selBoxWaitingSecond) {
        m_SelectionPress = m_Last;
        m_LeftDragSelection = true;
      }
    } else if (route == ViewportClickRoute::SelectionBox) {
      const bool shift = event->modifiers() & Qt::ShiftModifier;
      const bool fenceWindow = m_Last.x() >= m_State.selBoxAnchorScreenX;
      if (m_State.selBoxWaitingSecond) {
        const double pickX = m_State.active == AppCommandState::Kind::Zoom ? rawCursor.x : m_Cursor.x;
        const double pickY = m_State.active == AppCommandState::Kind::Zoom ? rawCursor.y : m_Cursor.y;
        SubmitViewportPick(m_State, pickX, pickY, m_Log, shift, fenceWindow,
            CadViewIsPlan(m_State) ? nullptr : &ray);
        m_LeftDragSelection = false;
      } else {
        BeginSelectionBoxCorner(m_State, static_cast<float>(rawCursor.x), static_cast<float>(rawCursor.y),
            static_cast<float>(m_Last.x()), static_cast<float>(m_Last.y()));
        m_SelectionPress = m_Last;
        m_LeftDragSelection = true;
      }
    } else if (m_State.active == AppCommandState::Kind::None && m_State.selBoxWaitingSecond &&
               SubmitViewportSelectionAccumulateClick(
                   m_State, rawCursor.x, rawCursor.y, static_cast<float>(m_Last.x()),
                   static_cast<float>(m_Last.y()), event->modifiers() & Qt::ShiftModifier,
                   event->modifiers() & Qt::ControlModifier, CadViewIsPlan(m_State) ? nullptr : &ray,
                   static_cast<float>(width()), static_cast<float>(height()), camera.orthoHalfH, m_Log, false)) {
      // An ordinary object click after an empty click is a pick, not the opposite box corner.
      // Keep the new hit instead of restoring the pre-box snapshot through CancelSelectionBox.
      m_State.selBoxWaitingSecond = false;
      m_State.selBoxPreview.clear();
      m_LeftDragSelection = false;
      m_HasSelectionBoxSnapshot = false;
      m_SelectionBeforeBox.clear();
      m_SurveySelectionBeforeBox.clear();
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
                else m_State.selection.push_back(candidateEntity);
              } else {
                ClearCadSelection(m_State);
                m_State.selectedSurveyPointIndices.clear();
                m_State.selection.push_back(candidateEntity);
              }
              if (changed) changed();
              update();
            });
          }
          menu->popup(event->globalPosition().toPoint());
          return;
        }
        m_PendingIdleSelectionClick = true;
        m_SelectionPress = m_Last;
        m_PendingIdlePick = hit;
        m_SelectionBeforePendingClick = m_State.selection;
        m_SurveySelectionBeforePendingClick = m_State.selectedSurveyPointIndices;
        m_SubObjectSelectionBeforePendingClick = m_State.subObjectSelection;
      } else {
        m_SelectionBeforeBox = m_State.selection;
        m_SurveySelectionBeforeBox = m_State.selectedSurveyPointIndices;
        m_HasSelectionBoxSnapshot = true;
        m_IdleBoxFirstClickPending = true;
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
  // REQ-476 (D-2026-10-30-b): a double-click on a model-space table cell edits its text, as the paper cell does.
  if (m_State.activeSpaceIndex == kModelSpaceIndex) {
    const float wx = static_cast<float>(m_Cursor.x);
    const float wy = static_cast<float>(m_Cursor.y);
    for (int ti = static_cast<int>(m_State.cadTables.size()) - 1; ti >= 0; --ti) {
      const CadTable& table = m_State.cadTables[static_cast<std::size_t>(ti)];
      if (!m_State.hiddenEntityIds.empty() && static_cast<std::size_t>(ti) < m_State.cadTableAttrs.size() &&
          CadEntityIdHidden(&m_State.hiddenEntityIds, m_State.cadTableAttrs[static_cast<std::size_t>(ti)].id))
        continue;  // a hidden table cannot be picked or edited
      const int cell = CadTableCellAtWorld(table, wx, wy);
      if (cell < 0)
        continue;
      if (CadTableCellIsSurveyPointData(m_State.tableStyles, table, cell)) {
        QMessageBox::information(this, "Point table",
                                 "Point table values come from survey points and cannot be edited here.");
        event->accept();
        return;
      }
      const auto uid = m_State.activeDrawingIdx > 0 && m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size())
          ? m_State.drawingTabs[m_State.activeDrawingIdx].projectUid : 0;
      const auto readOnly = std::find_if(m_State.openProjects.begin(), m_State.openProjects.end(),
          [uid](const auto& session) { return uid && session.uid == uid && session.readOnly; });
      if (readOnly != m_State.openProjects.end()) {
        QMessageBox::warning(this, "Read-only project", "This project was opened read-only. The table cell cannot be changed.");
        event->accept();
        return;
      }
      OpenTableCellEditor(m_State, ti, cell);
      if (!m_State.tableCellEditorOpen) {
        event->accept();
        return;
      }
      bool accepted = false;
      const int row = cell / table.cols + 1;
      const int column = cell % table.cols + 1;
      const QString value = QInputDialog::getText(this, "Edit table cell",
          QString("Row %1, column %2:").arg(row).arg(column), QLineEdit::Normal,
          QString::fromUtf8(m_State.tableCellEditorBuf.c_str()), &accepted);
      if (accepted) {
        m_State.tableCellEditorBuf = value.toUtf8().toStdString();
        std::vector<std::string> cellLog;
        CommitTableCellEditor(m_State, cellLog);
        if (changed) changed();
      } else {
        CancelTableCellEditor(m_State);
      }
      update();
      event->accept();
      return;
    }
  }
  QOpenGLWidget::mouseDoubleClickEvent(event);
}

void EditorViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPointF delta = event->position() - m_Last;
  const Viewport* floatingViewport = CurrentViewport(m_State);
  const bool floatingLocked = InFloatingModelSpace(m_State) && floatingViewport != nullptr && floatingViewport->locked;
  const bool leftCommandDrag = (event->buttons() & Qt::LeftButton) &&
      (m_State.active == AppCommandState::Kind::Pan || m_State.active == AppCommandState::Kind::Orbit);
  const bool commandOrbitDrag = leftCommandDrag && m_State.active == AppCommandState::Kind::Orbit;
  const bool middleOrbitDrag = (event->buttons() & Qt::MiddleButton) &&
      (event->modifiers() & Qt::ShiftModifier);
  const bool panDrag = (leftCommandDrag && m_State.active == AppCommandState::Kind::Pan) ||
      ((event->buttons() & Qt::MiddleButton) && !(event->modifiers() & Qt::ShiftModifier));
  if (!floatingLocked && (middleOrbitDrag || (commandOrbitDrag && m_State.activeSpaceIndex == kModelSpaceIndex))) {
    m_State.viewAnimActive = false;  // A manual ORBIT takes over from any ViewCube animation (REQ-059).
    m_State.viewportAzimuthDeg += static_cast<float>(delta.x()) * 0.4f;
    m_State.viewportElevationDeg = std::clamp(m_State.viewportElevationDeg +
        static_cast<float>(delta.y()) * 0.4f, -89.9f, 90.f);
  } else if (panDrag && !floatingLocked) {
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
    const bool objectSelectionStep = ViewportIsObjectSelectionStep(m_State);
    // CREATEPOINTS is a modeless dialog: no AppCommandState::Kind is active, but placement still
    // follows the normal object-snap pipeline. Without this branch its click path receives raw cursor
    // coordinates and survey-point snaps appear disabled only while the dialog is open.
    const bool createPointsPlacement = m_State.showCreatePointsWindow &&
                                       m_State.active == AppCommandState::Kind::None;
    const bool gripDrag = m_State.entityGripMoveActive || m_State.dimGripMoveActive || m_State.mtextGripMoveActive;
    const bool midCmdUsesSnap = (m_State.active != AppCommandState::Kind::None || gripDrag || sectionGripDrag ||
                                 paperModifyActive || createPointsPlacement) && !objectSelectionStep;
    m_Snap = midCmdUsesSnap
        ? CadSnap::FindBest(m_Cursor.x, m_Cursor.y, m_State, true,
            camera.orthoHalfH * 16.f / static_cast<float>(std::max(1, height())), {},
            CadViewIsPlan(m_State) ? nullptr : &ray, onlyKind)
        : CadSnap::Hit{};
    if (!objectSelectionStep && !m_State.selection.empty()) {
      const CadSnap::Hit gripSnap = CadSnap::FindGripSnap(m_Cursor.x, m_Cursor.y, m_State,
          camera.orthoHalfH * m_State.objectSnapAperturePx / static_cast<float>(std::max(1, height())));
      if (gripSnap.valid && (!m_Snap.valid || CadSnap::Priority(gripSnap.kind) > CadSnap::Priority(m_Snap.kind)))
        m_Snap = gripSnap;
    }
    if (event->buttons() == Qt::NoButton && m_State.active == AppCommandState::Kind::None &&
        !m_State.entityGripMoveActive && !m_State.dimGripMoveActive && !m_State.mtextGripMoveActive) {
      int hoverEntity = -1, hoverWhich = -1;
      double nearest = std::pow(static_cast<double>(m_State.gripSizePx + 4.f), 2.0);
      for (const SelectedEntity& selected : m_State.selection) {
        if (selected.type != SelectedEntity::Type::LineSeg || selected.index < 0) continue;
        const size_t k = static_cast<size_t>(selected.index) * 6;
        if (k + 5 >= m_State.userLinesFlat.size()) continue;
        for (int which = 0; which < 2; ++which) {
          const double gx = m_State.userLinesFlat[k + (which == 0 ? 0 : 3)];
          const double gy = m_State.userLinesFlat[k + (which == 0 ? 1 : 4)];
          float sx = 0.f, sy = 0.f;
          camera.WorldToScreen(gx, gy, m_State.userLinesFlat[k + (which == 0 ? 2 : 5)],
              static_cast<float>(width()), static_cast<float>(height()), &sx, &sy);
          const double dx = sx - m_Last.x(), dy = sy - m_Last.y();
          const double d2 = dx * dx + dy * dy;
          if (std::isfinite(d2) && d2 <= nearest) {
            nearest = d2;
            hoverEntity = selected.index;
            hoverWhich = which;
          }
        }
      }
      if (hoverEntity != m_State.gripMenuHoverEntityIndex || hoverWhich != m_State.gripMenuHoverWhich) {
        m_GripMenuTimer.stop();
        m_State.gripMenuHoverEntityIndex = hoverEntity;
        m_State.gripMenuHoverWhich = hoverWhich;
        if (hoverEntity >= 0) m_GripMenuTimer.start();
        else m_State.gripMenuHoverDwell.armed = false;
      }
    } else {
      m_GripMenuTimer.stop();
      m_State.gripMenuHoverEntityIndex = -1;
      m_State.gripMenuHoverWhich = -1;
      m_State.gripMenuHoverDwell.armed = false;
    }
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
    if (m_State.alignLabelDragActive) {  // REQ-492.5: the label follows the cursor
      if (event->buttons() & Qt::LeftButton) m_GripMovedWithButton = true;
      ApplyAlignmentLabelDrag(m_State, static_cast<float>(m_Cursor.x), static_cast<float>(m_Cursor.y));
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
      CadDimRefreshMeasurementText(&ann, CadDimensionStyleForAnnotation(m_State, ann), CadAngleDisplaySettings(m_State));
      float sx1=0, sy1=0, sx2=0, sy2=0, tx=0, ty=0, nx=0, ny=0, length=0;
      if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &length)) ann.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
      BumpCadGpuCache(m_State);
    }
    m_State.alignmentCursorReadout = AlignmentCursorReadout(m_State, m_Cursor.x, m_Cursor.y, camera.orthoHalfH * 0.1);
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
  if (event->button() == Qt::LeftButton && m_State.alignLabelDragActive) {  // REQ-492.5
    FinishAlignmentLabelDrag(m_State, m_GripMovedWithButton, m_Log);
    m_GripMovedWithButton = false;
    if (changed) changed();
    update();
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
  if (event->button() == Qt::LeftButton && m_PendingIdleSelectionClick) {
    CommitPendingIdleSelectionClick(event->modifiers());
    m_PendingIdleSelectionClick = false;
    m_SelectionBeforePendingClick.clear();
    m_SurveySelectionBeforePendingClick.clear();
    m_SubObjectSelectionBeforePendingClick.clear();
    m_PendingIdlePick = {};
  }
  if (event->button() == Qt::LeftButton && m_IdleBoxFirstClickPending &&
      m_State.active == AppCommandState::Kind::None && !InFloatingModelSpace(m_State)) {
    m_IdleBoxFirstClickPending = false;
    ray3d::Vec3 point;
    if (WorldPoint(event->position(), point)) {
      BeginSelectionBoxCorner(m_State, static_cast<float>(point.x), static_cast<float>(point.y),
          static_cast<float>(event->position().x()), static_cast<float>(event->position().y()));
    } else {
      CancelSelectionBox();
    }
  }
  if (event->button() == Qt::LeftButton && m_LeftDragSelection) {
    const QPointF delta = event->position() - m_SelectionPress;
    if (std::abs(delta.x()) + std::abs(delta.y()) > 3.0 && m_State.selBoxWaitingSecond) {
      ray3d::Vec3 point;
      if (WorldPoint(event->position(), point)) {
        const Camera camera = CadViewCamera(m_State);
        const auto ray = camera.ScreenRay(event->position().x(), event->position().y(), width(), height());
        SubmitViewportPick(m_State, point.x, point.y, m_Log, event->modifiers() & Qt::ShiftModifier,
            event->position().x() >= m_SelectionPress.x(), CadViewIsPlan(m_State) ? nullptr : &ray);
        m_HasSelectionBoxSnapshot = false;
        m_SelectionBeforeBox.clear();
        m_SurveySelectionBeforeBox.clear();
      }
    }
    m_LeftDragSelection = false;
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
  const Viewport* floatingViewport = CurrentViewport(m_State);
  if (InFloatingModelSpace(m_State) && floatingViewport != nullptr && floatingViewport->locked) {
    event->accept();
    return;
  }
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
  const Viewport* floatingViewport = CurrentViewport(m_State);
  if (InFloatingModelSpace(m_State) && floatingViewport != nullptr && floatingViewport->locked)
    return;
  m_State.viewportAzimuthDeg = azimuth;
  m_State.viewportElevationDeg = elevation;
  m_State.viewportRollDeg = 0.f;
  update();
}

void EditorViewport::RefreshCommandCursor() {
  if (QWidget* bar = findChild<QWidget*>(QStringLiteral("floatingCommandBar"))) {
    if (bar->isVisible()) {
      const QPoint local = mapFromGlobal(QCursor::pos());
      if (bar->geometry().contains(local)) {
        QWidget* under = bar->childAt(bar->mapFromGlobal(QCursor::pos()));
        for (QWidget* widget = under; widget && widget != bar; widget = widget->parentWidget()) {
          if (qobject_cast<QLineEdit*>(widget) || qobject_cast<QPlainTextEdit*>(widget)) {
            setCursor(Qt::IBeamCursor);
            return;
          }
        }
        setCursor(Qt::ArrowCursor);
        return;
      }
    }
  }
  const bool overCube = viewcube::Contains(CubeScene(), m_Last.x(), m_Last.y());
  setCursor(overCube ? Qt::ArrowCursor : (m_PointerInside && m_State.active == AppCommandState::Kind::Pan
      ? Qt::OpenHandCursor : (m_Ready ? Qt::BlankCursor : Qt::CrossCursor)));
}

// REQ-059: the widget's orientation changes ease to the target instead of snapping.
void EditorViewport::AnimateToOrientation(float azimuth, float elevation) {
  const Viewport* floatingViewport = CurrentViewport(m_State);
  if (InFloatingModelSpace(m_State) && floatingViewport != nullptr && floatingViewport->locked)
    return;
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
