#include "AdjustmentWindow.hpp"
#include "EditorWindow.hpp"
#include "io/DwgIo.hpp"
#include "io/DxfIo.hpp"
#include "Version.hpp"
#include "EditorViewport.hpp"
#include "ViewportRenderer.hpp"
#include "TransformPreview.hpp"
#include "EditorChrome.hpp"
#include "QtAppearance.hpp"
#include "QtSplash.hpp"
#include "QtAccent.hpp"
#include "TopographicArtwork.hpp"
#include "RibbonCatalog.hpp"
#include "TraverseActions.hpp"
#include "traverse/TraverseIo.hpp"
#include "traverse/FbkImport.hpp"
#include "traverse/TraverseCommandLang.hpp"
#include "PdfAttach.hpp"
#include "PdfWindow.hpp"
#include "PdfCanvas.hpp"
#include "PdfRibbon.hpp"
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>
#ifdef small
#undef small
#endif
#include "PdfViewerCore.hpp"
#include "QtAnnotations.hpp"
#include "util/gizmooverlay.hpp"
#include "GsIo.hpp"
#include "ProjectPoints.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPack.hpp"
#include "ProjectTurnover.hpp"
#include "ProjectSettings.hpp"
#include "geo/CoordinateSystems.hpp"
#include "SurveyCsv.hpp"
#include "io/ProjectPointDb.hpp"
#include "io/Project.hpp"
#include "GsMigrate.hpp"
#include "AppPaths.hpp"
#include "DwgIo.hpp"
#include "ProjectAddFlow.hpp"
#include "UserPrefs.hpp"
#include "platform/HttpFetch.hpp"
#include "update/UpdateService.hpp"
#include "CrashReporter.hpp"
#include "FailureReport.hpp"
#include "FailureReportUi.hpp"
#include "StartupFailure.hpp"
#include "CadSurfaceEdit.hpp"
#include "SurfaceStyle.hpp"
#include "io/CadPointCloudLas.hpp"
#include "io/CadPointCloudLaz.hpp"
#include "io/CadPointCloudPts.hpp"
#include "io/CadPointCloudPtx.hpp"
#include <E57SimpleData.h>
#include <E57SimpleWriter.h>
#include <array>
#include <QPdfWriter>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDockWidget>
#include <QListView>
#include <QScrollBar>
#include <QToolBar>
#include <QWheelEvent>
#include <QPainter>
#include <QtTest/QtTest>
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QPalette>
#include <QMessageBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QWheelEvent>
#include <QSizeGrip>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextBrowser>
#include <QLineEdit>
#include <QInputDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QSpinBox>
#include <QComboBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QGroupBox>
#include <QListWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QToolButton>
#include <QWidgetAction>
#include <QMenuBar>
#include <QStatusBar>
#include <QMenu>
#include <QBoxLayout>
#include <QStyledItemDelegate>
#include <QRadioButton>
#include <QFrame>
#include <QSettings>
#include <QTreeWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QClipboard>
#include <QScopeGuard>
#include <QStyle>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>

namespace {
// REQ-059 / #1034: the ViewCube as laid out for the viewport's current camera and UCS.
viewcube::Scene ViewCubeSceneOf(EditorViewport* viewport, const AppCommandState& state) {
  const viewcube::Point origin = viewcube::OriginFor(static_cast<float>(viewport->width()));
  return viewcube::BuildScene(CadViewCamera(state), origin.x, origin.y, CadUcsViewAzimuthOffsetDeg(state));
}
QPoint Px(const viewcube::Point& p) { return QPoint(qRound(p.x), qRound(p.y)); }

QString QtTestArtifactDirectory() {
  const QString override = qEnvironmentVariable("GOSURVEY_QT_TEST_ARTIFACT_DIR");
  return override.isEmpty() ? QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) : override;
}

struct QtPdfFileWriter { FPDF_FILEWRITE api{}; QFile file; };
int WriteQtPdf(FPDF_FILEWRITE* writer, const void* data, unsigned long size) {
  auto* output = reinterpret_cast<QtPdfFileWriter*>(writer);
  return output->file.write(static_cast<const char*>(data), static_cast<qint64>(size)) == static_cast<qint64>(size) ? 1 : 0;
}

bool WriteRotatedCopy(const QString& source, const QString& destination) {
  FPDF_InitLibrary();
  const QByteArray encoded = QFile::encodeName(source);
  FPDF_DOCUMENT document = FPDF_LoadDocument(encoded.constData(), nullptr);
  if (!document) return false;
  FPDF_PAGE page = FPDF_LoadPage(document, 0);
  if (!page) { FPDF_CloseDocument(document); return false; }
  FPDFPage_SetRotation(page, 1);
  FPDF_ClosePage(page);
  QtPdfFileWriter writer;
  writer.file.setFileName(destination);
  if (!writer.file.open(QIODevice::WriteOnly)) { FPDF_CloseDocument(document); return false; }
  writer.api.version = 1;
  writer.api.WriteBlock = &WriteQtPdf;
  const bool saved = FPDF_SaveAsCopy(document, &writer.api, 0) != 0;
  FPDF_CloseDocument(document);
  writer.file.close();
  return saved;
}

std::unique_ptr<AppCommandState> StateWithTraverse() {
  auto state = std::make_unique<AppCommandState>();
  state->activeDrawingIdx = state->prevDrawingIdx = 1;
  auto& data = state->traverseData;
  data.hasStartBearing = true;
  data.isClosedLoop = true;
  data.startStationId = 0;
  for (int station : {1, 2, 3, 0}) {
    TraverseLeg leg;
    leg.stationId = station;
    leg.hasHorizAngle = true; leg.horizAngleDeg = 90;
    leg.hasHorizDist = true; leg.horizDist = station == 1 ? 100.1 : 100;
    data.legs.push_back(leg);
  }
  return state;
}
QString OpenTraverseCommands() {
  return QStringLiteral(
      "STATION 0 0 0 0\n"
      "BEARING 0\n"
      "ENDPOINT 0 100 0\n"
      "LOOP OFF\n"
      "STN 0 1\n"
      "HANG REF 0 1 90\n"
      "HDIST 0 1 100\n"
      "STN 1 2\n"
      "HANG 0 1 2 90\n"
      "HDIST 1 2 100\n"
      "STN 2 3\n"
      "HANG 1 2 3 90\n"
      "HDIST 2 3 100\n");
}
QString Issue914Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Req453Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QAction* Action(QWidget& widget, const QString& text) {
  for (auto* action : widget.findChildren<QAction*>()) if (action->text() == text) return action;
  return nullptr;
}
/// Issue #911: a Texas Central (US feet) drawing whose Transformation tab resolves a real scale
/// factor, with sea-level reduction on so Combined and Grid only differ.
void GiveDrawingTexasScale(AppCommandState& state) {
  if (!geo::DictionariesLoaded())
    geo::LoadDictionaries(ResolveBundledAssetPath(std::filesystem::path("resources") / "csmap").string());
  state.drawingInsUnits = 2;
  state.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  auto& transform = state.drawingSettings.transform;
  transform.refGridE = 3115243.14; transform.refGridN = 10077391.26;
  transform.applySeaLevel = true; transform.elevation = 1000.0;
  transform.spheroidRadiusM = 20906000.0 * 1200.0 / 3937.0;
}
/// Runs the adjustment and waits for the worker; the solved traverse is then in `state.traverseData`.
void SolveAdjustment(AdjustmentWindow& window) {
  auto* run = Action(window, "Run adjustment");
  QVERIFY(run);
  run->trigger();
  QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);
}
void AnswerNextDialog(QMessageBox::StandardButton answer) {
  QTimer::singleShot(0, [answer] {
    for (auto* widget : QApplication::topLevelWidgets())
      if (auto* message = qobject_cast<QMessageBox*>(widget))
        if (auto* button = message->button(answer)) button->click();
  });
}
/// Clicks the named button of the project Copy / Link / Cancel question the moment it appears
/// ("" = close the window, as Escape does). `beforeClick` runs first, to change the world under it.
void AnswerAttachQuestion(EditorWindow& window, const char* button, std::function<void()> beforeClick = {}) {
  auto* poll = new QTimer(&window);
  poll->setInterval(10);
  QObject::connect(poll, &QTimer::timeout, poll, [&window, poll, button, beforeClick] {
    auto* dialog = window.findChild<QDialog*>("projectAttachDialog");
    if (!dialog) return;
    poll->stop(); poll->deleteLater();
    if (beforeClick) beforeClick();
    auto* target = *button ? dialog->findChild<QPushButton*>(button) : nullptr;
    if (target) target->click(); else dialog->reject();
  });
  poll->start();
}
/// Clicks the named button of the "Project in use" question the moment it appears ("" = close the
/// window, as Escape does). `inspect` runs first, with the dialog, to check what it says.
void AnswerLockQuestion(EditorWindow& window, const char* button, const std::function<void(QDialog&)>& inspect = {}) {
  auto* poll = new QTimer(&window);
  poll->setInterval(10);
  QObject::connect(poll, &QTimer::timeout, poll, [&window, poll, button, inspect] {
    auto* dialog = window.findChild<QDialog*>("projectLockDialog");
    if (!dialog) return;
    poll->stop(); poll->deleteLater();
    if (inspect) inspect(*dialog);
    auto* target = *button ? dialog->findChild<QPushButton*>(button) : nullptr;
    if (target) target->click(); else dialog->reject();
  });
  poll->start();
}
/// Dismisses any message box that appears and keeps what it said.
struct MessageCatcher {
  QTimer timer;
  QStringList texts;
  MessageCatcher() {
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &timer, [this] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
          texts << message->text();
          message->accept();
        }
    });
    timer.start();
  }
};
/// Points the per-user preferences file (read from %APPDATA%) at a scratch folder, so a test that
/// saves settings never touches the real ones.
struct IsolatedUserData {
  QTemporaryDir dir;
  QByteArray previous = qgetenv("APPDATA");
  bool hadPrevious = qEnvironmentVariableIsSet("APPDATA");
  explicit IsolatedUserData(const QString& appData = {}) {
    qputenv("APPDATA", QDir::toNativeSeparators(appData.isEmpty() ? dir.path() : appData).toUtf8());
  }
  ~IsolatedUserData() {
    if (hadPrevious) qputenv("APPDATA", previous); else qunsetenv("APPDATA");
  }
};
update::Manifest OfferedManifest(const std::string& version = "99.0.0") {
  update::Manifest manifest;
  manifest.version = version;
  manifest.installerUrl = "http://127.0.0.1:1/GoSurvey-Installer.exe";  // nothing listens on port 1
  manifest.sha256 = std::string(64, 'a');
  manifest.notes = "## Highlights\n- Faster point import";
  manifest.gsFormatVersion = kGsFormatVersion;
  return manifest;
}
QPushButton* UpdateButton(QDialog* dialog, const char* name) {
  return dialog->findChild<QPushButton*>(name);
}
// The Windows clipboard is shared with every other program in the session, and another program (a clipboard
// manager, a remote session) can hold it open so Qt's setText is silently dropped. A copy-to-clipboard check
// only means something when a probe round-trips, so those assertions are gated on this and the skip is reported.
bool ClipboardRoundTrips() {
  QClipboard* clipboard = QGuiApplication::clipboard();
  const QString probe = QStringLiteral("gosurvey-clipboard-probe");
  for (int attempt = 0; attempt < 10; ++attempt) {
    clipboard->setText(probe);
    if (clipboard->text() == probe) return true;
    QTest::qWait(100);
  }
  qWarning("The clipboard does not round-trip in this session; copy-to-clipboard assertions are skipped.");
  return false;
}
QString Issue886Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  const QString path = QDir(directory).filePath(QString::fromLatin1(name));
  QDir().mkpath(QFileInfo(path).path());
  return path;
}
}

namespace {
// Issue #893 fixtures: a drawing with six survey points, a point group over them and two surfaces.
void Issue893Fixture(AppCommandState& st, bool buildSurfaces = true) {
  for (int i = 0; i < 6; ++i) {
    SurveyPoint point;
    point.id = i + 1;
    const int column = i % 3, row = i / 3;
    point.easting = column * 10.0;
    point.northing = row * 10.0;
    point.elevation = 100.0 + i * 1.5;
    st.surveyPoints.push_back(point);
  }
  PointGroup ground;
  ground.name = "Ground";
  ground.rule.idRangesText = "1-6";
  st.pointGroups.push_back(ground);
  PointGroup curb;
  curb.name = "Curb";
  curb.rule.idRangesText = "1-3";
  st.pointGroups.push_back(curb);
  std::vector<std::string> log;
  SurfaceStyles::EnsureStandard(st.surfaceStyles);
  if (buildSurfaces) {
    CreateSurfaceFromPointGroups(st, "EG", {"Ground"}, log);
    CreateSurfaceFromPointGroups(st, "Proposed", {}, log);
  }
}
QString Issue912Shot(const QString& name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(name);
}
/// The default test traverse, solved for the plan, plus a side shot and a check shot off station 1.
std::unique_ptr<AppCommandState> Issue912State(bool withShots) {
  auto state = StateWithTraverse();
  if (!withShots) return state;
  std::string text = travcmd::Serialize(state->traverseData, state->traverseLsaWeights);
  if (!text.empty() && text.back() != '\n') text += '\n';
  text += "STN 1 9 SIDE\nHANG REF 1 9 45\nHDIST 1 9 25\nDESC 1 9 corner\n";
  text += "STN 1 10 CHECK\nHANG REF 1 10 135\nHDIST 1 10 30\n";
  std::string error;
  LsaWeights weights = state->traverseLsaWeights;
  TraverseData data;
  if (travcmd::Parse(text, data, weights, error)) state->traverseData = data;
  return state;
}
QString Issue893Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Issue897Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Issue905Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Issue907Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Issue908Shot(const char* name) {
  const QString directory = QtTestArtifactDirectory();
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Issue905WritePdf(QTemporaryDir& directory, const char* name, const std::string& bytes) {
  QString path = directory.filePath(QString::fromLatin1(name));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return {};
  file.write(bytes.data(), static_cast<qint64>(bytes.size()));
  return path;
}
std::string Issue908AuditPdf() {
  std::string content = "0.5 w\n";
  for (int i = 0; i < 5; ++i) {
    const int y = 100 + i * 60;
    content += "50 " + std::to_string(y) + " m 250 " + std::to_string(y) + " l S\n";
    content += "50 " + std::to_string(y - 5) + " m 50 " + std::to_string(y + 10) + " l S\n";
    content += "250 " + std::to_string(y - 5) + " m 250 " + std::to_string(y + 10) + " l S\n";
    content += "BT /F1 8 Tf 130 " + std::to_string(y + 12) + " Td (100'-0\") Tj ET\n";
  }
  std::vector<std::string> objects = {
      "<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Resources<</Font<</F1 5 0 R>>>>/Contents 4 0 R>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "endstream",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>"};
  std::string pdf = "%PDF-1.4\n";
  std::vector<size_t> offsets;
  for (size_t i = 0; i < objects.size(); ++i) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
  }
  const size_t xref = pdf.size();
  pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (size_t offset : offsets) {
    char line[32]; std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset); pdf += line;
  }
  pdf += "trailer\n<</Size " + std::to_string(objects.size() + 1) + "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
  return pdf;
}
// The current search match is orange over white (255, ~187, ~105); the other matches are yellow.
bool Issue905HasHighlight(const QImage& image) {
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const QColor c = image.pixelColor(x, y);
      if (c.red() > 240 && c.green() > 170 && c.green() < 205 && c.blue() < 125) return true;
    }
  return false;
}
// ---- Issue #906 / REQ-430: PDF calibration, scale checks and snapping ----
// One page of 400 x 300 pt with a 200 pt line (50,100)-(250,100) and three 144 pt lines (50,150/200/250)-(194,...):
// 144 pt = 2 in = 40 ft at 1" = 20'. Their ends are the snap points.
std::string Issue906Pdf(const std::string& pageKeys = "/MediaBox[0 0 400 300]",
                        const std::string& content = "50 100 m 250 100 l S 50 150 m 194 150 l S 50 200 m 194 200 l S 50 250 m 194 250 l S") {
  std::vector<std::string> objs = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/Contents 4 0 R" + pageKeys + ">>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
  };
  std::string out = "%PDF-1.4\n";
  std::vector<size_t> offsets;
  for (size_t i = 0; i < objs.size(); ++i) {
    offsets.push_back(out.size());
    out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
  }
  const size_t xref = out.size();
  out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
  for (size_t offset : offsets) {
    char line[32];
    std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
    out += line;
  }
  out += "trailer\n<</Size " + std::to_string(objs.size() + 1) + "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
  return out;
}
// Runs \p interact on the named dialog as soon as it is on screen (the call that opens it blocks in its own event loop).
template <class F>
void Issue906OnDialog(QWidget& parent, const char* name, const F& interact) {
  auto* timer = new QTimer(&parent);
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, &parent, [&parent, name, interact, timer] {
    auto* dialog = parent.findChild<QDialog*>(name);
    if (!dialog || !dialog->isVisible()) return;
    timer->stop();
    timer->deleteLater();
    interact(dialog);
  });
  timer->start();
  QTimer::singleShot(20000, &parent, [&parent] { for (auto* dialog : parent.findChildren<QDialog*>()) dialog->reject(); }); // a hung test cannot block the run
}
void Issue906Select(QComboBox* combo, const QString& text) {
  const int index = combo->findText(text);
  QVERIFY2(index >= 0, qPrintable(text));
  combo->setCurrentIndex(index);
}
QPoint Issue906Px(QWidget* canvas, double x, double y, double wPt = 400, double hPt = 300) {
  const double factor = canvas->width() / wPt;
  return QPoint(qRound(x * factor), qRound((hPt - y) * factor));
}
// A pointer move over the page (sent straight to the canvas, so it does not depend on where the window sits on screen).
void Issue906Move(QWidget* canvas, QPoint position) {
  QMouseEvent move(QEvent::MouseMove, QPointF(position), QPointF(canvas->mapToGlobal(position)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &move);
}
// Issue #1037: a wheel event and a mouse event with explicit buttons, sent the way the window system would.
void Issue1037Wheel(QWidget* target, QPoint position, int delta, Qt::KeyboardModifiers modifiers) {
  QWheelEvent wheel(QPointF(position), QPointF(target->mapToGlobal(position)), QPoint(), QPoint(0, delta), Qt::NoButton, modifiers,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(target, &wheel);
}
void Issue1037MouseGlobal(QWidget* target, QEvent::Type type, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons) {
  QMouseEvent event(type, QPointF(target->mapFromGlobal(global)), QPointF(global), button, buttons, Qt::NoModifier);
  QApplication::sendEvent(target, &event);
}
QAction* Issue906Action(PdfWindow& window, const QString& text) {
  for (auto* action : window.findChildren<QAction*>())
    if (action->text() == text && !action->isSeparator()) return action;
  return nullptr;
}
// Sets the preset \p label on the current page through the Set scale dialog.
void Issue906Preset(PdfWindow& window, const char* label) {
  Issue906OnDialog(window, "pdfSetScaleDialog", [label](QDialog* dialog) {
    Issue906Select(dialog->findChild<QComboBox*>("pdfScalePreset"), QString::fromUtf8(label));
    dialog->findChild<QPushButton*>("pdfScaleApply")->click();
  });
  window.findChild<QAction*>("pdfSetScaleAction")->trigger();
}
bool Issue906HasPixelNear(const QImage& image, const QColor& colour, int tolerance) {
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const QColor c = image.pixelColor(x, y);
      if (qAbs(c.red() - colour.red()) <= tolerance && qAbs(c.green() - colour.green()) <= tolerance && qAbs(c.blue() - colour.blue()) <= tolerance) return true;
    }
  return false;
}
// The snap marker is a bright green square (60,255,90); anti-aliasing blends its edge, so match the colour closely.
bool Issue906HasSnapMarker(const QImage& image) {
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const QColor c = image.pixelColor(x, y);
      if (c.red() < 90 && c.green() > 235 && c.blue() < 130) return true;
    }
  return false;
}
// Opens \p bytes in a new shown window and waits until the page is drawn.
bool Issue906Open(std::unique_ptr<PdfWindow>& holder, QTemporaryDir& directory, const std::string& bytes, const char* name = "sheet.pdf") {
  holder = std::make_unique<PdfWindow>(nullptr);
  holder->resize(1000, 800);
  holder->show();
  QString error;
  if (!holder->LoadFile(Issue905WritePdf(directory, name, bytes), error)) { qWarning() << error; return false; }
  for (int i = 0; i < 500 && holder->IsBusy(); ++i) QTest::qWait(20);
  return !holder->IsBusy();
}
// Runs \p interact once the modal dialog a call is about to open is on screen; a watchdog rejects every
// dialog if the interaction never closes it, so a failing test cannot hang the run.
template <class F>
void WhenDialogShows(EditorWindow& window, const F& interact) {
  QTimer::singleShot(0, &window, interact);
  QTimer::singleShot(20000, &window, [&window] {
    for (auto* dialog : window.findChildren<QDialog*>()) dialog->reject();
  });
}
}  // namespace

namespace {
// Issue #916 fixtures: scans of `count` distinct points that every writer stores exactly.
std::vector<double> Issue916Points(int count, double base = 0.0) {
  std::vector<double> xyz;
  for (int i = 0; i < count; ++i) { xyz.push_back(base + i * 1.5); xyz.push_back(100.0 + i * 0.25); xyz.push_back(10.0 + i * 0.125); }
  return xyz;
}
QString Issue916WriteScan(const QTemporaryDir& directory, const char* extension, int count, double base = 0.0) {
  QString path = directory.filePath(QString("scan-%1-%2-%3.%1").arg(extension).arg(count).arg(static_cast<int>(base)));
  const std::string utf8 = path.toUtf8().toStdString();
  const std::vector<double> xyz = Issue916Points(count, base);
  const std::string ext = extension;
  if (ext == "pts") { if (!pointcloud_pts::WritePtsFile(utf8, xyz, {}, {}).ok) return {}; }
  else if (ext == "ptx") { if (!pointcloud_ptx::WritePtxFile(utf8, xyz, {}, {}).ok) return {}; }
  else if (ext == "las") { if (!pointcloud_las::WriteLasFile(utf8, xyz, {}, {}).ok) return {}; }
  else if (ext == "laz") { if (!pointcloud_laz::WriteLazFile(utf8, xyz, {}, {}).ok) return {}; }
  else {
    e57::Writer writer(utf8, e57::WriterOptions{});
    e57::Data3D header;
    header.pointFields.cartesianXField = header.pointFields.cartesianYField = header.pointFields.cartesianZField = true;
    header.pointCount = static_cast<std::size_t>(count);
    const std::int64_t scan = writer.NewData3D(header);
    e57::Data3DPointsData_t<double> buffers(header);
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) { buffers.cartesianX[i] = xyz[3 * i]; buffers.cartesianY[i] = xyz[3 * i + 1]; buffers.cartesianZ[i] = xyz[3 * i + 2]; }
    e57::CompressedVectorWriter points = writer.SetUpData3DPointsData(scan, static_cast<std::size_t>(count), buffers);
    points.write(static_cast<std::size_t>(count));
    points.close();
    writer.Close();
  }
  return path;
}
/// Same points in any order (the octree reorders them), within REQ-101's tolerance.
bool Issue916SamePoints(std::vector<double> actual, std::vector<double> expected) {
  auto sorted = [](std::vector<double>& xyz) {
    std::vector<std::array<double, 3>> rows;
    for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) rows.push_back({xyz[i], xyz[i + 1], xyz[i + 2]});
    std::sort(rows.begin(), rows.end());
    return rows;
  };
  const auto a = sorted(actual), b = sorted(expected);
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    for (int axis = 0; axis < 3; ++axis) if (std::abs(a[i][axis] - b[i][axis]) > 0.003) return false;
  return true;
}
}  // namespace

class QtEditorTests final : public QObject {
  Q_OBJECT
  // ---- Issue #916 / REQ-172: Qt point-cloud attach, import progress and export ----
  static void AttachPointCloud(EditorWindow& window, const QString& path) {
    WhenDialogShows(window, [&window, path] {
      auto* dialog = window.findChild<QDialog*>("pointCloudAttachDialog");
      QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("pointCloudAttachPath")->setText(path);
      dialog->findChild<QPushButton*>("pointCloudAttachConfirm")->click();
    });
    window.ShowPointCloudAttach();
  }
  /// Opens the export dialog through the typed command, optionally lets `interact` look at it, then
  /// exports to `path` (the dialog adds the extension when it is missing).
  static void ExportPointCloud(EditorWindow& window, const char* command, const QString& path,
                               const std::function<void(QDialog&)>& interact = {}) {
    WhenDialogShows(window, [&window, path, interact] {
      auto* dialog = window.findChild<QDialog*>("pointCloudExportDialog");
      QVERIFY(dialog);
      if (interact) interact(*dialog);
      dialog->findChild<QLineEdit*>("pointCloudExportPath")->setText(path);
      dialog->findChild<QPushButton*>("pointCloudExportConfirm")->click();
    });
    window.Submit(command);
  }

private Q_SLOTS:
  void req453DevelopmentBranchAppearsInBothWindowTitlesAndSurvivesRefresh() {
    EditorWindow window(QStringLiteral("feature/window-label"));
    auto* brand = window.findChild<QLabel*>("windowBrandTitle");
    QVERIFY(brand);
    QCOMPARE(window.windowTitle(), QString("GoSurvey — feature/window-label"));
    QCOMPARE(brand->text(), window.windowTitle());

    window.CreateDrawingFromTemplate();
    window.Refresh();
    QCOMPARE(window.windowTitle(), QString("GoSurvey — feature/window-label"));
    QCOMPARE(brand->text(), window.windowTitle());
    window.resize(1440, 900);
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save(Req453Shot("req453-development-branch-title.png")));
  }

  void req453WindowTitleWithoutDevelopmentBranchStaysUnchanged() {
    EditorWindow window;
    auto* brand = window.findChild<QLabel*>("windowBrandTitle");
    QVERIFY(brand);
    QCOMPARE(window.windowTitle(), QString("GoSurvey"));
    QCOMPARE(brand->text(), window.windowTitle());

    window.CreateDrawingFromTemplate();
    window.Refresh();
    QCOMPARE(window.windowTitle(), QString("GoSurvey"));
    QCOMPARE(brand->text(), window.windowTitle());
    window.resize(1440, 900);
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.grab().save(Req453Shot("req453-normal-title.png")));
  }

  void issue803SplineRibbonPreviewAndSelectionScreenshots() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1440, 900);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.m_Viewport->IsRendererReady(), 10000);

    auto* splineAction = Action(window, "Spline");
    QVERIFY(splineAction);
    QVERIFY(splineAction->isEnabled());
    splineAction->trigger();
    QCOMPARE(window.State().active, AppCommandState::Kind::Spline);
    window.State().splineDraftPoints = {{-30, -10, 0}, {-10, 20, 0}, {12, -15, 0}};
    window.Refresh();
    window.m_Viewport->update();
    QTest::qWait(150);
    QVERIFY(window.grab().save(Issue914Shot("qt-803-spline-preview.png")));

    CadSpline committed;
    QVERIFY(spline::MakeFit(window.State().splineDraftPoints, false, &committed, nullptr));
    window.State().splineDraftPoints.clear();
    window.State().active = AppCommandState::Kind::None;
    window.State().cadSplines = {std::move(committed)};
    window.State().cadSplineAttrs = {EntityAttributes{}};
    window.State().selection = {{SelectedEntity::Type::Spline, 0}};
    window.Refresh();
    QTest::qWait(150);
    QVERIFY(window.grab().save(Issue914Shot("qt-803-spline-selected.png")));
  }

  void blockEditorPickerUsesQtDialog() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 800);
    window.show();
    QCoreApplication::processEvents();
    bool captured = false;
    QTimer::singleShot(250, [&] {
      auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QVERIFY(dialog->labelText().contains("block definition", Qt::CaseInsensitive));
      QImage dialogImage(dialog->size(), QImage::Format_ARGB32_Premultiplied);
      dialogImage.fill(dialog->palette().color(QPalette::Window));
      dialog->render(&dialogImage);
      QVERIFY(dialogImage.save(Issue907Shot("qt-966-block-editor-picker.png")));
      captured = true;
      dialog->reject();
    });
    window.Submit("BEDIT");
    QVERIFY(captured);
    QVERIFY(!window.State().blockEditPickerOpen);
  }

  void issue916AttachDialogValidatesThenImportsEveryFormat() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    MessageCatcher messages;

    // Nothing starts until the file is a readable scan of a supported type.
    WhenDialogShows(window, [&window, &directory] {
      auto* dialog = window.findChild<QDialog*>("pointCloudAttachDialog");
      QVERIFY(dialog);
      auto* path = dialog->findChild<QLineEdit*>("pointCloudAttachPath");
      auto* confirm = dialog->findChild<QPushButton*>("pointCloudAttachConfirm");
      auto* status = dialog->findChild<QLabel*>("pointCloudAttachStatus");
      QVERIFY(!confirm->isEnabled());
      path->setText(directory.filePath("missing.pts"));
      QVERIFY(!confirm->isEnabled());
      QVERIFY(status->text().contains("does not exist"));
      QFile other(directory.filePath("notes.xyz")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("1 2 3\n"); other.close();
      path->setText(other.fileName());
      QVERIFY(!confirm->isEnabled());
      QVERIFY(status->text().contains("cannot attach .xyz"));
      QFile empty(directory.filePath("empty.pts")); QVERIFY(empty.open(QIODevice::WriteOnly)); empty.close();
      path->setText(empty.fileName());
      QVERIFY(!confirm->isEnabled());
      QVERIFY(status->text().contains("empty"));
      path->setText(directory.path());
      QVERIFY(!confirm->isEnabled());
      QVERIFY(status->text().contains("folder"));
      path->setText(Issue916WriteScan(directory, "pts", 12));
      QVERIFY(confirm->isEnabled());
      QVERIFY(dialog->findChild<QLabel*>("pointCloudAttachDetails")->text().contains("PTS"));
      dialog->findChild<QPushButton*>("pointCloudAttachCancel")->click();
    });
    window.ShowPointCloudAttach();
    QVERIFY(!state.pointCloudImportAsync);
    QVERIFY(state.cadPointClouds.empty());

    // Every supported format imports the same points, one undo step each.
    const int count = 40;
    const auto expected = Issue916Points(count);
    std::size_t imported = 0;
    for (const char* extension : {"pts", "ptx", "las", "laz", "e57"}) {
      AttachPointCloud(window, Issue916WriteScan(directory, extension, count));
      ++imported;
      QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), imported, 20000);
      QVERIFY2(!state.pointCloudImportAsync, extension);
      const auto& cloud = *state.cadPointClouds.back();
      QCOMPARE(cloud.totalPointCount, std::int64_t(count));
      QVERIFY2(Issue916SamePoints(cloud.pointsXyz, expected), extension);
    }
    window.Submit("UNDO");
    QCOMPARE(state.cadPointClouds.size(), imported - 1);
    QVERIFY(messages.texts.isEmpty());
  }

  void issue916ImportFailureCancelAndReadOnlyLeaveTheDrawingUnchanged() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    const bool couldUndo = CanUndo(state);
    MessageCatcher messages;

    // A malformed scan is refused with the reason and adds nothing.
    QFile broken(directory.filePath("broken.e57")); QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("this is not an E57 scan"); broken.close();
    AttachPointCloud(window, broken.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(!messages.texts.isEmpty(), 20000);
    QVERIFY(!messages.texts.front().isEmpty());
    QVERIFY(state.cadPointClouds.empty());
    QTRY_VERIFY(!state.pointCloudImportAsync);
    QCOMPARE(CanUndo(state), couldUndo);

    // Cancel from the progress window stops the background import without a failure message.
    messages.texts.clear();
    AttachPointCloud(window, Issue916WriteScan(directory, "pts", 3000000));
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QDialog*>("pointCloudImportProgress"), 10000);
    auto* progress = window.findChild<QDialog*>("pointCloudImportProgress");
    QVERIFY(!progress->findChild<QLabel*>("pointCloudImportPhase")->text().isEmpty());
    QVERIFY(progress->findChild<QProgressBar*>("pointCloudImportBar"));
    progress->findChild<QPushButton*>("pointCloudImportCancel")->click();
    QTRY_VERIFY_WITH_TIMEOUT(!state.pointCloudImportAsync, 60000);
    QTRY_VERIFY(!window.findChild<QDialog*>("pointCloudImportProgress"));
    QVERIFY(state.cadPointClouds.empty());
    QVERIFY(messages.texts.isEmpty());
    QVERIFY(std::any_of(window.m_Log.begin(), window.m_Log.end(), [](const std::string& line) { return line.find("cancelled") != std::string::npos; }));
    QCOMPARE(CanUndo(state), couldUndo);
  }

  void issue916AttachRefusesReadOnlyProjectDrawingsAndAsksCopyOrLink() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    gsproj::Project created; std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Cloud Job", &created, &error), error.c_str());
    QTemporaryDir scans; QVERIFY(scans.isValid());
    EditorWindow window;
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    auto& state = window.State();
    auto& session = *std::find_if(state.openProjects.begin(), state.openProjects.end(), [uid](const auto& item) { return item.uid == uid; });
    const QString projectFolder = QString::fromStdWString(session.project.Folder().wstring());
    MessageCatcher messages;

    // Cancel at the Copy / Link question starts no import and copies nothing.
    const QString outside = Issue916WriteScan(scans, "pts", 10);
    AnswerAttachQuestion(window, "projectAttachCancel");
    AttachPointCloud(window, outside);
    QTRY_VERIFY(state.projectAttachPrompt.kind == AppCommandState::ProjectAttachPrompt::Kind::None);
    QVERIFY(!state.pointCloudImportAsync);
    QVERIFY(state.cadPointClouds.empty());
    QVERIFY(!QFileInfo::exists(projectFolder + "/PointClouds/" + QFileInfo(outside).fileName()));

    // Copy imports the project's copy.
    AnswerAttachQuestion(window, "projectAttachCopy");
    AttachPointCloud(window, outside);
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(1), 20000);
    QVERIFY(QFileInfo::exists(projectFolder + "/PointClouds/" + QFileInfo(outside).fileName()));

    // A read-only project drawing is not edited: neither the dialog nor the typed command imports.
    session.readOnly = true;
    window.ShowPointCloudAttach();
    QVERIFY(!window.findChild<QDialog*>("pointCloudAttachDialog"));
    QVERIFY(messages.texts.last().contains("read-only"));
    messages.texts.clear();
    window.Submit("POINTCLOUDATTACH \"" + QDir::toNativeSeparators(outside) + "\"");
    QVERIFY(messages.texts.last().contains("read-only"));
    QVERIFY(!state.pointCloudImportAsync);
    QCOMPARE(state.cadPointClouds.size(), std::size_t(1));
    session.readOnly = false;
  }

  void issue916ExportDialogWritesEveryFormatAndRoundTrips() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    MessageCatcher messages;

    window.Submit("EXPORTLAS");
    QVERIFY(messages.texts.last().contains("no point clouds"));
    QVERIFY(!window.findChild<QDialog*>("pointCloudExportDialog"));

    // Two clouds: A (20 points, PTS) and B (35 points, LAS).
    AttachPointCloud(window, Issue916WriteScan(directory, "pts", 20));
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(1), 20000);
    AttachPointCloud(window, Issue916WriteScan(directory, "las", 35, 1000.0));
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(2), 20000);
    const auto secondPoints = Issue916Points(35, 1000.0);

    // Nothing selected: the dialog says it exports everything, and refuses a bad destination.
    state.selection.clear();
    messages.texts.clear();
    WhenDialogShows(window, [&window, &directory] {
      auto* dialog = window.findChild<QDialog*>("pointCloudExportDialog");
      QVERIFY(dialog);
      QVERIFY(dialog->findChild<QLabel*>("pointCloudExportSummary")->text().contains("all 2"));
      QCOMPARE(dialog->findChild<QComboBox*>("pointCloudExportFormat")->currentIndex(), 0);
      QVERIFY(dialog->findChild<QLabel*>("pointCloudExportNote")->text().contains("E57"));
      auto* path = dialog->findChild<QLineEdit*>("pointCloudExportPath");
      auto* status = dialog->findChild<QLabel*>("pointCloudExportStatus");
      auto* confirm = dialog->findChild<QPushButton*>("pointCloudExportConfirm");
      path->clear(); confirm->click();
      QVERIFY(status->text().contains("Choose where"));
      path->setText(directory.filePath("nowhere/all.pts")); confirm->click();
      QVERIFY(status->text().contains("does not exist"));
      QVERIFY(dialog->isVisible());
      path->setText(directory.filePath("all"));  // no extension: the format's is added
      confirm->click();
    });
    window.Submit("EXPORTPTS");
    QVERIFY(QFileInfo::exists(directory.filePath("all.pts")));
    QTRY_VERIFY(!messages.texts.isEmpty());
    QVERIFY(messages.texts.last().contains("wrote 55 point(s) from 2 cloud(s)"));

    // The selected cloud alone, in each format, re-imports with the same count and coordinates.
    const char* commands[] = {"EXPORTPTS", "EXPORTPTX", "EXPORTLAS", "EXPORTLAZ"};
    const char* extensions[] = {"pts", "ptx", "las", "laz"};
    for (int i = 0; i < 4; ++i) {
      state.selection.assign(1, SelectedEntity{});
      state.selection.front().type = SelectedEntity::Type::PointCloud;
      state.selection.front().index = 1;
      const QString file = directory.filePath(QString("second-%1.%1").arg(extensions[i]));
      ExportPointCloud(window, commands[i], file, [i](QDialog& dialog) {
        QCOMPARE(dialog.findChild<QComboBox*>("pointCloudExportFormat")->currentIndex(), i);
        QVERIFY(dialog.findChild<QLabel*>("pointCloudExportSummary")->text().contains("1 selected point cloud"));
      });
      QVERIFY2(QFileInfo(file).size() > 0, extensions[i]);
      const std::size_t before = state.cadPointClouds.size();
      AttachPointCloud(window, file);
      QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), before + 1, 20000);
      const auto& cloud = *state.cadPointClouds.back();
      QCOMPARE(cloud.totalPointCount, std::int64_t(35));
      QVERIFY2(Issue916SamePoints(cloud.pointsXyz, secondPoints), extensions[i]);
      QTRY_VERIFY(!state.pointCloudImportAsync);
    }
    // Exporting is not a drawing edit: the two source clouds are unchanged, plus four re-imports.
    QCOMPARE(state.cadPointClouds.size(), std::size_t(6));
  }

  void issue916ExportConfirmsReplacementAndReportsWriteFailure() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    MessageCatcher messages;
    AttachPointCloud(window, Issue916WriteScan(directory, "pts", 10));
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(1), 20000);
    state.selection.clear();

    const QString target = directory.filePath("existing.pts");
    QFile existing(target); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("keep me"); existing.close();
    messages.texts.clear();
    // Declining the replacement leaves the file and keeps the dialog open.
    WhenDialogShows(window, [&window, target] {
      auto* dialog = window.findChild<QDialog*>("pointCloudExportDialog");
      QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("pointCloudExportPath")->setText(target);
      AnswerNextDialog(QMessageBox::Cancel);
      dialog->findChild<QPushButton*>("pointCloudExportConfirm")->click();
      QVERIFY(dialog->isVisible());
      QFile file(target); QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), QByteArray("keep me"));
      file.close();
      // A destination that cannot be written reports why and keeps the dialog open.
      QVERIFY(QFile::setPermissions(target, QFileDevice::ReadOwner));
      AnswerNextDialog(QMessageBox::Yes);
      dialog->findChild<QPushButton*>("pointCloudExportConfirm")->click();
      QVERIFY(dialog->isVisible());
      QVERIFY(!dialog->findChild<QLabel*>("pointCloudExportStatus")->text().isEmpty());
      QVERIFY(QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
      // And once writable, accepting the replacement writes the cloud.
      AnswerNextDialog(QMessageBox::Yes);
      dialog->findChild<QPushButton*>("pointCloudExportConfirm")->click();
    });
    window.Submit("EXPORTPTS");
    QVERIFY(QFileInfo(target).size() > 7);
    QCOMPARE(state.cadPointClouds.size(), std::size_t(1));
  }

  void issue916CaptureScreenshotOptions() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    auto& state = window.State();
    MessageCatcher messages;
    const QString scan = Issue916WriteScan(directory, "las", 250000);
    // The chosen look (option C, graphite with amber) is the dialogs' own style.
    const std::pair<const char*, QString> options[] = {{"final-graphite-amber", {}}};

    // Attach dialog: ready to attach, then the refusals.
    for (const auto& [name, sheet] : options) {
      WhenDialogShows(window, [&window, scan, name = QString(name), sheet] {
        auto* dialog = window.findChild<QDialog*>("pointCloudAttachDialog");
        QVERIFY(dialog);
        if (!sheet.isEmpty()) dialog->setStyleSheet(sheet);
        dialog->findChild<QLineEdit*>("pointCloudAttachPath")->setText(QDir::toNativeSeparators(scan));
        QVERIFY(dialog->grab().save(Issue907Shot(("qt-916-attach-" + name + ".png").toUtf8().constData())));
        dialog->findChild<QPushButton*>("pointCloudAttachCancel")->click();
      });
      window.ShowPointCloudAttach();
    }
    QFile notes(directory.filePath("notes.xyz")); QVERIFY(notes.open(QIODevice::WriteOnly)); notes.write("1 2 3\n"); notes.close();
    WhenDialogShows(window, [&window, &directory] {
      auto* dialog = window.findChild<QDialog*>("pointCloudAttachDialog");
      QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("pointCloudAttachPath")->setText(directory.filePath("notes.xyz"));
      QVERIFY(dialog->grab().save(Issue907Shot("qt-916-attach-error.png")));
      dialog->findChild<QLineEdit*>("pointCloudAttachPath")->clear();
      QVERIFY(dialog->grab().save(Issue907Shot("qt-916-attach-empty.png")));
      dialog->findChild<QPushButton*>("pointCloudAttachCancel")->click();
    });
    window.ShowPointCloudAttach();

    // Progress while a larger scan is indexed.
    const QString big = Issue916WriteScan(directory, "pts", 3000000);
    AttachPointCloud(window, big);
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QDialog*>("pointCloudImportProgress"), 10000);
    auto* progress = window.findChild<QDialog*>("pointCloudImportProgress");
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible(), 10000);
    for (const auto& [name, sheet] : options) {
      if (!sheet.isEmpty()) progress->setStyleSheet(sheet);
      QVERIFY(progress->grab().save(Issue907Shot(("qt-916-progress-" + QString(name) + ".png").toUtf8().constData())));
    }
    progress->findChild<QPushButton*>("pointCloudImportCancel")->click();
    QTRY_VERIFY_WITH_TIMEOUT(!state.pointCloudImportAsync, 60000);

    // Export dialog with two clouds in the drawing.
    AttachPointCloud(window, scan);
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(1), 30000);
    AttachPointCloud(window, Issue916WriteScan(directory, "pts", 1200, 500.0));
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(2), 30000);
    state.selection.clear();
    for (const auto& [name, sheet] : options) {
      WhenDialogShows(window, [&window, name = QString(name), sheet] {
        auto* dialog = window.findChild<QDialog*>("pointCloudExportDialog");
        QVERIFY(dialog);
        if (!sheet.isEmpty()) dialog->setStyleSheet(sheet);
        QVERIFY(dialog->grab().save(Issue907Shot(("qt-916-export-" + name + ".png").toUtf8().constData())));
        dialog->findChild<QPushButton*>("pointCloudExportCancel")->click();
      });
      window.Submit("EXPORTPTS");
    }
    // The ribbon entry points.
    auto* ribbon = window.findChild<QTabWidget*>("ribbon"); QVERIFY(ribbon);
    const char* pages[][2] = {{"ribbonPage_Insert", "ribbonPanel_Insert_Point Cloud"}, {"ribbonPage_Output", "ribbonPanel_Output_Export"}};
    for (const auto& entry : pages) {
      auto* page = window.findChild<QWidget*>(entry[0]); QVERIFY(page);
      ribbon->setCurrentWidget(page);
      QTest::qWait(150);
      auto* widget = window.findChild<QWidget*>(entry[1]); QVERIFY(widget);
      QVERIFY(widget->grab().save(Issue907Shot((QString("qt-916-") + entry[1] + ".png").replace(' ', '-').toUtf8().constData())));
    }
  }

  void issue916RibbonOffersAttachAndExport() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto* attach = window.findChild<QAction*>("ribbon_RibbonPcAttach"); QVERIFY(attach);
    QVERIFY(attach->isEnabled());
    QCOMPARE(attach->property("ribbonCommand").toString(), QString("POINTCLOUDATTACH"));
    auto* exporter = window.findChild<QAction*>("ribbon_RibbonExportPointCloud"); QVERIFY(exporter);
    QVERIFY(exporter->isEnabled());
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("pointCloudAttachDialog");
      QVERIFY(dialog);
      dialog->findChild<QPushButton*>("pointCloudAttachCancel")->click();
    });
    attach->trigger();
    QVERIFY(!window.findChild<QDialog*>("pointCloudAttachDialog"));
  }

private Q_SLOTS:
  void initTestCase() { QVERIFY(PdfAttach_Init()); }
  void cleanupTestCase() { PdfAttach_Shutdown(); }

  // ---- Issue #907 / REQ-399: Qt PDF coordinate extraction ----
  void pdfCoordinateReviewAcceptsAndDeniesThroughDrawingWorkflow() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "coordinate-review.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    EditorWindow editor;
    auto& state = editor.State();
    state.drawingTabs.push_back({"Drawing 1", state.nextTabUid++});
    state.documents.emplace_back();
    state.activeDrawingIdx = state.prevDrawingIdx = static_cast<int>(state.drawingTabs.size()) - 1;
    PdfWindow window(&editor);
    window.SetEditor(&editor);
    window.resize(1100, 820); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);

    pdfview::CoordCandidate candidate;
    candidate.northing = 303267.23; candidate.easting = 1887764.63;
    candidate.elevation = 99.35; candidate.description = "BRASS CAP CONTROL";
    candidate.sourceText = "BRASS CAP CONTROL\nN:303267.23\nE:1887764.63\nEL. 99.35";
    window.m_ExtractResult = pdfview::ExtractResult{pdfview::ExtractStatus::Ok, {candidate}, {}};
    window.m_ExtractStart = QPointF(350, 120); window.m_ExtractEnd = QPointF(710, 260);
    window.ShowExtractReview();
    auto* review = window.findChild<QDialog*>("pdfCoordinateReview"); QVERIFY(review);
    auto* table = review->findChild<QTableWidget*>("pdfCoordinateCandidates"); QVERIFY(table);
    QCOMPARE(table->rowCount(), 1);

    // Capture the selected deep navy review with the source PDF and viewer behind it.
    QTest::qWait(80);
    QImage composite(window.size(), QImage::Format_ARGB32_Premultiplied);
    composite.fill(QColor("#101722"));
    QPainter painter(&composite);
    painter.drawPixmap(0, 0, window.grab());
    painter.drawPixmap((window.width() - review->width()) / 2, 180, review->grab());
    painter.end();
    QVERIFY(composite.save(Issue907Shot("qt-907-review-deep-navy.png")));

    auto* accept = review->findChild<QPushButton*>("pdfCoordinateAccept_0"); QVERIFY(accept);
    accept->click();
    QCOMPARE(state.surveyPoints.size(), size_t(1));
    QCOMPARE(state.surveyPoints[0].id, 1);
    QCOMPARE(state.surveyPoints[0].easting, candidate.easting);
    QCOMPARE(state.surveyPoints[0].northing, candidate.northing);
    QVERIFY(state.surveyPoints[0].description == candidate.description);
    QVERIFY(state.documents[static_cast<std::size_t>(state.activeDrawingIdx)].undoStack.size() > 0);
    QVERIFY(DoUndo(state, editor.m_Log));
    QVERIFY(state.surveyPoints.empty());

    candidate.pointNumber = 7; candidate.sourceText = "N:303000\nE:1887000";
    candidate.description.clear(); candidate.elevation.reset();
    window.m_ExtractResult = pdfview::ExtractResult{pdfview::ExtractStatus::Ok, {candidate}, {}};
    window.ShowExtractReview();
    auto* deny = review->findChild<QPushButton*>("pdfCoordinateDeny_0"); QVERIFY(deny);
    deny->click();
    QVERIFY(state.surveyPoints.empty()); // Deny never touches the drawing.
  }

  void pdfCoordinateReviewAcceptAllAndDenyAll() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "coordinate-all.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    EditorWindow editor;
    auto& state = editor.State();
    state.drawingTabs.push_back({"Drawing 1", state.nextTabUid++});
    state.documents.emplace_back();
    state.activeDrawingIdx = state.prevDrawingIdx = static_cast<int>(state.drawingTabs.size()) - 1;
    PdfWindow window(&editor);
    window.SetEditor(&editor);
    window.resize(1100, 820); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    std::vector<pdfview::CoordCandidate> candidates;
    for (int i = 0; i < 4; ++i) {
      pdfview::CoordCandidate candidate;
      candidate.northing = 1000.0 + i; candidate.easting = 2000.0 + i; candidate.elevation = 50.0 + i;
      candidate.description = "CTRL"; candidate.pointNumber = 10 + i; candidate.sourceText = "row";
      candidates.push_back(candidate);
    }
    window.m_ExtractResult = pdfview::ExtractResult{pdfview::ExtractStatus::Ok, candidates, {}};
    window.ShowExtractReview();
    auto* review = window.findChild<QDialog*>("pdfCoordinateReview"); QVERIFY(review);
    auto* table = review->findChild<QTableWidget*>("pdfCoordinateCandidates");
    QCOMPARE(table->rowCount(), 4);
    review->findChild<QPushButton*>("pdfCoordinateAcceptAll")->click();
    QCOMPARE(state.surveyPoints.size(), size_t(4));
    QCOMPARE(table->rowCount(), 0);

    window.m_ExtractResult = pdfview::ExtractResult{pdfview::ExtractStatus::Ok, candidates, {}};
    window.ShowExtractReview();
    QCOMPARE(table->rowCount(), 4);
    review->findChild<QPushButton*>("pdfCoordinateDenyAll")->click();
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(state.surveyPoints.size(), size_t(4)); // Deny all never touches the drawing.
  }

  void pdfCoordinateAcceptAllStopsAtFirstUncreatableCandidate() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "coordinate-stop.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    EditorWindow editor;
    auto& state = editor.State();
    state.drawingTabs.push_back({"Drawing 1", state.nextTabUid++});
    state.documents.emplace_back();
    state.activeDrawingIdx = state.prevDrawingIdx = static_cast<int>(state.drawingTabs.size()) - 1;
    SurveyPoint existing{}; existing.id = 10; state.surveyPoints.push_back(existing);
    PdfWindow window(&editor); window.SetEditor(&editor); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    std::vector<pdfview::CoordCandidate> candidates;
    for (const int id : {11, 10, 12}) {
      pdfview::CoordCandidate candidate; candidate.pointNumber = id; candidate.northing = id * 10.0;
      candidate.easting = id * 20.0; candidate.elevation = 5.0; candidate.description = "CTRL"; candidate.sourceText = "row";
      candidates.push_back(candidate);
    }
    window.m_ExtractResult = pdfview::ExtractResult{pdfview::ExtractStatus::Ok, candidates, {}};
    window.ShowExtractReview();
    auto* review = window.findChild<QDialog*>("pdfCoordinateReview"); QVERIFY(review);
    QTimer::singleShot(0, [] {
      auto* conflict = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(conflict);
      QVERIFY(conflict->button(QMessageBox::Cancel));
      conflict->button(QMessageBox::Cancel)->click();
    });
    review->findChild<QPushButton*>("pdfCoordinateAcceptAll")->click();
    QCOMPARE(state.surveyPoints.size(), size_t(2));
    QCOMPARE(state.surveyPoints.back().id, 11);
    QCOMPARE(window.m_ExtractResult->candidates.size(), size_t(2));
    QCOMPARE(window.m_ExtractResult->candidates.front().pointNumber.value(), 10);
    QVERIFY(std::none_of(state.surveyPoints.begin(), state.surveyPoints.end(), [](const auto& point) { return point.id == 12; }));
  }

  void issue1037ActiveToolIsNamedOnTheStatusBarAndEscapeEndsIt() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "tool-flow.pdf", pdfview::MakeSyntheticPdf(2, 3, 0));
    PdfWindow window(nullptr); window.resize(1100, 800); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    auto* label = window.statusBar()->findChild<QLabel*>("pdfActiveTool");
    QVERIFY(label);
    QCOMPARE(label->text(), QString("Tool: Select"));
    QAction* line = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) if (action->text() == "Line") line = action;
    QVERIFY(line);
    line->trigger();
    QVERIFY(label->text().contains("Line") && label->text().contains("Esc"));
    QVERIFY(line->isChecked());
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(window.m_Tool, -1);
    QVERIFY(!label->text().contains("Esc"));
    window.findChild<QAction*>("pdfExtractCoordinates")->trigger();
    QVERIFY(label->text().contains("Extract coordinates") && label->text().contains("Esc"));
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!label->text().contains("Extract"));
    // The yellow source box stays while candidates are under review, and Esc or closing the review removes it.
    pdfview::CoordCandidate candidate; candidate.northing = 1; candidate.easting = 2; candidate.sourceText = "N1 E2";
    window.m_ExtractResult = pdfview::ExtractResult{pdfview::ExtractStatus::Ok, {candidate}, {}};
    window.ShowExtractReview();
    QVERIFY(window.m_ExtractBox);
    window.EscapePressed();
    QVERIFY(!window.m_ExtractBox);
    window.ShowExtractReview();
    QVERIFY(window.m_ExtractBox);
    window.findChild<QDialog*>("pdfCoordinateReview")->findChild<QPushButton*>("pdfCoordinateReviewClose")->click();
    QVERIFY(!window.m_ExtractBox);
  }

  // REQ-455: single-letter tool hotkeys; Esc returns to Browse; the callout and the revision cloud survive a save.
  void issue1037ToolHotkeysAndCalloutAndRevisionCloudRoundTrip() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "hotkeys.pdf", pdfview::MakeSyntheticPdf(2, 3, 0));
    PdfWindow window(nullptr); window.resize(1100, 800); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.activateWindow();
    const std::pair<Qt::Key, int> keys[] = {{Qt::Key_L, 1}, {Qt::Key_R, 2}, {Qt::Key_E, 3}, {Qt::Key_T, 0}, {Qt::Key_C, 4},
        {Qt::Key_K, 9}, {Qt::Key_D, 5}, {Qt::Key_P, 6}, {Qt::Key_A, 7}, {Qt::Key_G, 8}, {Qt::Key_H, PdfWindow::kToolCheck},
        {Qt::Key_S, PdfWindow::kToolSelect}, {Qt::Key_B, PdfWindow::kToolBrowse}};
    for (const auto& [key, tool] : keys) {
      QTest::keyClick(&window, key);
      QCOMPARE(window.m_Tool, tool);
    }
    const auto pageSize = window.m_Document->Sizes()[0];
    const auto canvasPoint = [&](float x, float y) {
      return QPointF(x * window.PageWidthPx() / pageSize.wPt,
                     (pageSize.hPt - y) * window.PageHeightPx() / pageSize.hPt);
    };
    // The revision-cloud button defaults to a two-click rectangle; polygon mode takes corners then Enter.
    QVERIFY(!window.m_CloudPolygon);
    window.m_CloudPolygon = true;
    window.SetTool(static_cast<int>(pdfview::Annot::Kind::RevCloud), QString());
    window.PageClick(canvasPoint(10, 10), Qt::LeftButton);
    window.PageClick(canvasPoint(60, 10), Qt::LeftButton);
    window.PageClick(canvasPoint(60, 60), Qt::LeftButton);
    const int itemsBefore = static_cast<int>(window.m_Annotations.Items().size());
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(static_cast<int>(window.m_Annotations.Items().size()), itemsBefore + 1);
    QCOMPARE(window.m_Annotations.Items().back().kind, pdfview::Annot::Kind::RevCloud);
    window.m_CloudPolygon = false;
    window.m_PageNumber->setValue(2);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    const auto rectPageSize = window.m_Document->Sizes()[1];
    const QPointF rectangleFirst(50.f * window.PageWidthPx() / rectPageSize.wPt,
                                 (rectPageSize.hPt - 50.f) * window.PageHeightPx() / rectPageSize.hPt);
    const QPointF rectangleSecond(250.f * window.PageWidthPx() / rectPageSize.wPt,
                                  (rectPageSize.hPt - 150.f) * window.PageHeightPx() / rectPageSize.hPt);
    window.SetTool(static_cast<int>(pdfview::Annot::Kind::RevCloud), QString());
    window.PageClick(rectangleFirst, Qt::LeftButton);
    window.PageClick(rectangleSecond, Qt::LeftButton); // the second corner finishes the rectangle
    QCOMPARE(static_cast<int>(window.m_Annotations.Items().back().pts.size()), 4);
    window.SetTool(static_cast<int>(pdfview::Annot::Kind::Leader), QString());
    window.PageClick(canvasPoint(100, 200), Qt::LeftButton); // first click is the target
    QTimer::singleShot(0, [] {
      auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
      QVERIFY(input);
      input->setTextValue("CHECK THIS");
      input->accept();
    });
    window.PageClick(canvasPoint(150, 260), Qt::LeftButton); // second click places the note box
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(window.m_Tool, -1); // Esc falls back to Browse
    pdfview::Annot cloud; cloud.kind = pdfview::Annot::Kind::RevCloud; cloud.page = 1; cloud.thickness = 1.f;
    cloud.pts = {{50.f, 50.f}, {250.f, 50.f}, {250.f, 150.f}, {50.f, 150.f}};
    QVERIFY(!pdfview::RevCloudArcs(cloud).empty());
    QVERIFY(!cloud.IsDimension() && cloud.UsesPoints());
    QCOMPARE(window.m_Annotations.Items().back().kind, pdfview::Annot::Kind::Leader);
    QCOMPARE(window.m_Annotations.Items().back().text, std::string("CHECK THIS"));
    const QString saved = directory.filePath("marked.pdf");
    QVERIFY(window.SaveFile(saved, error));
    const auto back = pdfview::ReadAnnotations(std::filesystem::path(saved.toStdWString()));
    bool sawCallout = false, sawCloud = false, sawPolygonCloud = false;
    for (const auto& a : back) {
      if (a.kind == pdfview::Annot::Kind::Leader) {
        sawCallout = true; QCOMPARE(a.text, std::string("CHECK THIS"));
        QVERIFY(!a.pts.empty()); QVERIFY(std::abs(a.pts[0].first - 100.f) < 1.f && std::abs(a.pts[0].second - 200.f) < 1.f);
      }
      if (a.kind == pdfview::Annot::Kind::RevCloud && a.page == 1) { sawCloud = true; QCOMPARE(static_cast<int>(a.pts.size()), 4); QCOMPARE(a.page, 1); }
      if (a.kind == pdfview::Annot::Kind::RevCloud && a.page == 0) { sawPolygonCloud = true; QCOMPARE(a.pts.size(), size_t(3)); }
    }
    QVERIFY(sawCallout); QVERIFY(sawCloud); QVERIFY(sawPolygonCloud);
    for (const auto& a : back)
      if (a.kind == pdfview::Annot::Kind::RevCloud && a.page == 1) { QVERIFY(a.x0 < a.x1 && a.y0 < a.y1); } // the corners' box is filled in
    // A cloud with too few corners (a malformed mark) draws nothing and does not stop the save.
    pdfview::Annot degenerate = cloud; degenerate.pts = {{10.f, 10.f}, {10.f, 10.f}};
    QVERIFY(window.AddAnnotation(degenerate, error));
    QVERIFY2(window.SaveFile(directory.filePath("degenerate.pdf"), error), qPrintable(error));
    // The ribbon's Undo drops the selection, so no grip is left on a mark that moved or went away.
    window.m_SelectedAnnotation = static_cast<int>(window.m_Annotations.Items().size()) - 1;
    window.m_SelectedMore = {0};
    window.m_AnnotationDragging = true;
    window.m_AnnotationHandle = 0;
    window.m_DragPreview = window.m_Annotations.Items().front();
    window.findChild<QAction*>("pdfUndo")->trigger();
    QCOMPARE(window.m_SelectedAnnotation, -1);
    QVERIFY(window.m_SelectedMore.empty());
    QVERIFY(!window.m_AnnotationDragging);
    QCOMPARE(window.m_AnnotationHandle, -1);
    QVERIFY(!window.m_DragPreview.has_value());
  }

  void issue1037RevisionCloudReadBackKeepsBoundsOnRotatedPages() {
    QTemporaryDir directory;
    const QString source = Issue905WritePdf(directory, "cloud-source.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    const QString rotated = directory.filePath("cloud-rotated.pdf");
    QVERIFY(WriteRotatedCopy(source, rotated));
    PdfWindow window(nullptr); window.resize(1100, 800); window.show();
    QString error;
    QVERIFY2(window.LoadFile(rotated, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    pdfview::Annot cloud; cloud.kind = pdfview::Annot::Kind::RevCloud; cloud.page = 0; cloud.thickness = 1.f;
    cloud.pts = {{40.f, 50.f}, {240.f, 50.f}, {240.f, 150.f}, {40.f, 150.f}};
    QVERIFY(window.AddAnnotation(cloud, error));
    const QString saved = directory.filePath("cloud-rotated-marked.pdf");
    QVERIFY2(window.SaveFile(saved, error), qPrintable(error));
    const auto annotations = pdfview::ReadAnnotations(std::filesystem::path(saved.toStdWString()));
    const auto found = std::find_if(annotations.begin(), annotations.end(), [](const auto& annotation) {
      return annotation.kind == pdfview::Annot::Kind::RevCloud;
    });
    QVERIFY(found != annotations.end());
    QVERIFY(found->x0 < found->x1);
    QVERIFY(found->y0 < found->y1);
    QCOMPARE(found->pts.size(), size_t(4));
  }

  // REQ-455 clauses 1, 11 and 13: the Tool Chest buttons, the revision-cloud drop-down, and a preview after the first click.
  void issue1037ToolChestCloudMenuAndPreviews() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "previews.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    PdfWindow window(nullptr); window.resize(1100, 800); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    auto* chest = window.findChild<QWidget*>("pdfToolChest");
    QVERIFY(chest);
    QVERIFY(chest->findChildren<QToolButton*>().size() >= 16); // Markup 8, Measurement 5, Review 3
    auto* cloud = window.findChild<QToolButton*>("pdfCloudButton");
    QVERIFY(cloud && cloud->menu());
    window.findChild<QTabWidget*>("pdfRibbon")->setCurrentIndex(1);
    QTest::qWait(50);
    QCOMPARE(cloud->menu()->actions().size(), 2); // rectangle (default) and polygon
    QCOMPARE(cloud->text(), QString("Rect cloud"));
    QVERIFY(cloud->toolTip().contains("click two opposite corners"));
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-cloud-default.png")));
    cloud->menu()->popup(cloud->mapToGlobal(QPoint(0, cloud->height())));
    QTRY_VERIFY_WITH_TIMEOUT(cloud->menu()->isVisible(), 1000);
    QVERIFY(cloud->menu()->grab().save(Issue905Shot("qt-1037-cloud-menu.png")));
    cloud->menu()->hide();
    cloud->menu()->actions().at(1)->trigger();
    QCOMPARE(cloud->text(), QString("Polygon cloud")); // the button shows, and uses, the last mode
    QVERIFY(window.m_CloudPolygon);
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-cloud-polygon-mode.png")));
    // After the first click a dashed preview follows the pointer: the page looks different with it than without.
    for (const int tool : {1, 2, 3, 4, 9}) {
      window.SetTool(tool, QString());
      window.m_Draft.clear();
      window.m_PointerIn = true; window.m_PointerPage = 0; window.m_LastPointer = QPointF(260, 220);
      const QImage without = window.m_Page->grab().toImage();
      window.m_Draft = {{40.f, 600.f}}; window.m_DraftPage = 0;
      const QImage with = window.m_Page->grab().toImage();
      QVERIFY2(without != with, qPrintable(QString("no preview for tool %1").arg(tool)));
    }
  }

  void issue1037PdfWindowIsTopLevelAndClosesWithEditor() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "owned-window.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    auto editor = std::make_unique<EditorWindow>();
    editor->ShowPdf();
    PdfWindow* viewer = nullptr;
    for (QWidget* candidate : QApplication::topLevelWidgets())
      if (auto* pdf = dynamic_cast<PdfWindow*>(candidate)) { viewer = pdf; break; }
    QVERIFY(viewer);
    QVERIFY(viewer->isWindow());
    QVERIFY(viewer->parentWidget() == nullptr);
    QVERIFY(viewer->windowFlags().testFlag(Qt::Window));
    QString error;
    QVERIFY2(viewer->LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!viewer->IsBusy(), 10000);
    pdfview::Annot mark; mark.kind = pdfview::Annot::Kind::Rect; mark.page = 0;
    mark.x0 = 10; mark.y0 = 10; mark.x1 = 40; mark.y1 = 40;
    QVERIFY(viewer->AddAnnotation(mark, error));
    QTimer::singleShot(0, [] {
      auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(prompt);
      prompt->button(QMessageBox::Cancel)->click();
    });
    viewer->close();
    QVERIFY(viewer->isVisible()); // declining to discard keeps the independent viewer open
    QTimer::singleShot(0, [] {
      auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(prompt);
      prompt->button(QMessageBox::Discard)->click();
    });
    editor.reset();
    QVERIFY(!viewer->isVisible());
    delete viewer;
  }

  // Select works like the drawing's box select: two clicks; left to right takes what the box encloses, right to left
  // takes what it touches.
  void issue1037SelectBoxWindowAndCrossing() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "box-select.pdf", pdfview::MakeSyntheticPdf(2, 3, 0));
    PdfWindow window(nullptr); window.resize(1000, 800); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    pdfview::Annot inside; inside.kind = pdfview::Annot::Kind::Rect; inside.page = 0; inside.x0 = 100; inside.y0 = 100; inside.x1 = 140; inside.y1 = 140;
    pdfview::Annot straddling = inside; straddling.x0 = 190; straddling.x1 = 260;
    QVERIFY(window.AddAnnotation(inside, error)); QVERIFY(window.AddAnnotation(straddling, error));
    window.SetTool(-2, QString());
    window.SelectInBox(QPointF(50, 50), QPointF(220, 200)); // window: only the first is fully inside
    QCOMPARE(window.m_SelectedAnnotation, 0); QVERIFY(window.m_SelectedMore.empty());
    window.SelectInBox(QPointF(220, 200), QPointF(50, 50)); // crossing: both
    QVERIFY(window.IsSelected(0) && window.IsSelected(1));
    pdfview::Annot cloud; cloud.kind = pdfview::Annot::Kind::RevCloud; cloud.page = 0; cloud.thickness = 1.f;
    cloud.pts = {{300.f, 300.f}, {400.f, 300.f}, {400.f, 350.f}, {300.f, 350.f}};
    QVERIFY(window.AddAnnotation(cloud, error));
    window.SelectInBox(QPointF(500, 200), QPointF(350, 320)); // a crossing box that only overlaps the cloud
    QCOMPARE(window.m_SelectedAnnotation, 2);
    window.SelectInBox(QPointF(290, 290), QPointF(410, 360)); // a window box around it
    QCOMPARE(window.m_SelectedAnnotation, 2);
    window.SelectInBox(QPointF(400, 400), QPointF(450, 450));
    QCOMPARE(window.m_SelectedAnnotation, -1);
  }

  void issue1037SingleClickAndBatchDeleteAndStyle() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "selection-actions.pdf", pdfview::MakeSyntheticPdf(1, 3, 0));
    PdfWindow window(nullptr); window.resize(1000, 800); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    pdfview::Annot first; first.kind = pdfview::Annot::Kind::Rect; first.page = 0; first.x0 = 100; first.y0 = 100; first.x1 = 160; first.y1 = 160;
    pdfview::Annot second = first; second.x0 = 200; second.x1 = 260;
    QVERIFY(window.AddAnnotation(first, error)); QVERIFY(window.AddAnnotation(second, error));
    window.SetTool(PdfWindow::kToolSelect, QString());
    const auto size = window.m_Document->Sizes()[0];
    const QPointF click(130.0 * window.PageWidthPx() / size.wPt,
                        (size.hPt - 130.0) * window.PageHeightPx() / size.hPt);
    window.PageClick(click, Qt::LeftButton);
    window.EndAnnotation(click, Qt::LeftButton);
    QCOMPARE(window.m_SelectedAnnotation, 0);
    QVERIFY(window.m_SelectedMore.empty()); // one click selects only the mark under it

    window.SelectInBox(QPointF(50, 50), QPointF(300, 200));
    QVERIFY(window.IsSelected(0) && window.IsSelected(1));
    window.m_AnnotationColor = 0x123456;
    window.findChild<QAction*>("pdfAnnotationApplyStyle")->trigger();
    QCOMPARE(window.m_Annotations.Items()[0].color, 0x123456u);
    QCOMPARE(window.m_Annotations.Items()[1].color, 0x123456u);
    QTest::keyClick(&window, Qt::Key_Delete);
    QVERIFY(window.m_Annotations.Items().empty()); // Delete acts on every mark in the box selection
  }

  // Dragging with the left button pans the page under Browse.
  void issue1037BrowseDragPansTheDocument() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "browse-pan.pdf", pdfview::MakeSyntheticPdf(2, 3, 0));
    PdfWindow window(nullptr); window.resize(900, 700); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.SetZoomPercent(300);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.SetTool(-1, QString());
    auto* bar = window.m_Scroll->verticalScrollBar();
    bar->setValue(bar->maximum() / 2);
    const int before = bar->value();
    QWidget* viewport = window.m_Scroll->viewport();
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(300, 400));
    QTest::mouseMove(viewport, QPoint(300, 300));
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(300, 300));
    QVERIFY2(bar->value() > before, "dragging up with the left button under Browse scrolls the page down");
  }

  void pdfCoordinateRegionCancelAndNoTextAreSafe() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "coordinate-scan.pdf",
        pdfview::MakeLineWorkPdf(612, 792, 50, 1, 0));
    PdfWindow window(nullptr); window.resize(1000, 760); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    auto* action = window.findChild<QAction*>("pdfExtractCoordinates"); QVERIFY(action);
    action->trigger();
    QVERIFY(window.m_ExtractMode);
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!window.m_ExtractMode && !window.m_ExtractDragging);
    QVERIFY(!window.m_ExtractResult.has_value());

    action->trigger();
    auto* canvas = window.findChild<QWidget*>("pdfCanvas"); QVERIFY(canvas);
    QVERIFY(canvas->width() > 20 && canvas->height() > 20);
    QString message;
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QTest::mouseMove(canvas, QPoint(canvas->width() - 2, canvas->height() - 2));
    QVERIFY(window.grab().save(Issue907Shot("qt-907-region-selection.png")));
    QTimer::singleShot(0, [&] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(box);
      message = box->text();
      QVERIFY(box->grab().save(Issue907Shot("qt-907-no-text-message.png")));
      box->accept();
    });
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier,
                        QPoint(canvas->width() - 2, canvas->height() - 2));
    QVERIFY(message.contains("scanned", Qt::CaseInsensitive));
    QVERIFY(message.contains("OCR", Qt::CaseInsensitive));
    QVERIFY(!window.m_ExtractResult.has_value());
  }

  void pdfCoordinateCommitHonorsReadOnlyAndPointConflicts() {
    EditorWindow editor;
    auto& state = editor.State();
    state.drawingTabs.push_back({"Drawing 1", state.nextTabUid++});
    state.documents.emplace_back();
    state.activeDrawingIdx = state.prevDrawingIdx = 1;
    pdfview::CoordCandidate candidate;
    candidate.northing = 400.0; candidate.easting = 500.0; candidate.elevation = 12.0;
    candidate.pointNumber = 1; candidate.description = "PDF CONTROL";

    AppCommandState::ProjectSession session; session.uid = 44; session.readOnly = true;
    state.drawingTabs[1].projectUid = session.uid;
    state.openProjects.push_back(session);
    QTimer::singleShot(0, [] {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->accept();
    });
    QVERIFY(!editor.CommitPdfCoordinate(candidate));
    QVERIFY(state.surveyPoints.empty());
    state.openProjects.clear();
    state.drawingTabs[1].projectUid = 0;

    SurveyPoint existing{}; existing.id = 1; existing.easting = 10.0; existing.northing = 20.0;
    existing.elevation = 1.0; existing.description = "EXISTING";
    state.surveyPoints.push_back(existing);
    QTimer::singleShot(0, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(box);
      for (auto* button : box->buttons()) if (button->text() == "Overwrite") { button->click(); return; }
      QFAIL("Overwrite choice not found");
    });
    QVERIFY(editor.CommitPdfCoordinate(candidate));
    QCOMPARE(state.surveyPoints.size(), size_t(1));
    QCOMPARE(state.surveyPoints.front().easting, candidate.easting);
    QCOMPARE(state.surveyPoints.front().description, candidate.description);
    QVERIFY(DoUndo(state, editor.m_Log));
    QCOMPARE(state.surveyPoints.front().description, existing.description);
  }

  void pdfCoordinateCircleLandsInTheDrawingAndZoomExtentsFramesIt() {
    EditorWindow editor;
    editor.CreateDrawingFromTemplate();
    editor.resize(1000, 700);
    editor.show();
    QTRY_VERIFY_WITH_TIMEOUT(editor.m_Viewport->IsRendererReady(), 10000);
    auto& state = editor.State();
    pdfview::CoordCandidate candidate; // a state-plane northing/easting with no description: becomes a circle
    candidate.northing = 303401.741; candidate.easting = 1887631.271;
    QVERIFY(editor.CommitPdfCoordinate(candidate));
    QCOMPARE(state.userCirclesCxCyZR.size(), size_t(4));
    QVERIFY(state.userCirclesCxCyZR[3] > 0.f);
    QTRY_VERIFY_WITH_TIMEOUT(!state.pendingZoomExtents, 10000);
    QStringList log;
    for (const auto& line : editor.m_Log) log << QString::fromStdString(line);
    QVERIFY2(log.join(" | ").contains("Zoom extents applied"), qPrintable(log.join(" | ")));
    // The view is now centred on the circle, so the circle is on screen rather than off in empty space.
    QVERIFY(std::abs(state.viewportPanX - state.userCirclesCxCyZR[0]) < 1.0 && std::abs(state.viewportPanY - state.userCirclesCxCyZR[1]) < 1.0);
  }

  void approvedTopographyStaysInPanelBackgrounds() {
    IsolatedUserData isolated;
    const auto oldPalette = qApp->palette(); const auto oldFont = qApp->font();
    const auto oldSheet = qApp->styleSheet(); const auto oldStyle = qApp->style()->objectName();
    const auto oldDefault = qApp->property("defaultQtStyle"), oldLight = qApp->property("cadLightTheme");
    auto restoreAppearance = qScopeGuard([&] {
      qApp->setStyle(oldStyle); qApp->setPalette(oldPalette); qApp->setFont(oldFont); qApp->setStyleSheet(oldSheet);
      qApp->setProperty("defaultQtStyle", oldDefault); qApp->setProperty("cadLightTheme", oldLight);
    });
    ApplyQtAppearance(*qApp, false);
    EditorWindow window;
    window.resize(1672, 941);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* tabs = window.findChild<QTabBar*>("drawingTabs"); QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    auto* properties = window.findChild<QDockWidget*>("propertiesDock"); QVERIFY(properties);
    auto* panel = properties->findChild<QScrollArea*>("propertiesPanel"); QVERIFY(panel);
    auto* tree = panel->widget(); QVERIFY(tree);
    auto* toolspace = window.findChild<QDockWidget*>("toolspaceDock"); QVERIFY(toolspace);
    window.resizeDocks({properties, toolspace}, {300, 290}, Qt::Horizontal);
    QTest::qWait(250);
    auto captureDrawing = [&](const char* name) {
      if (auto* recent = window.findChild<QLabel*>("commandRecent")) recent->hide();
      auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport");
      QImage shot = window.grab().toImage();
      if (viewport && viewport->isVisible()) {
        const auto frame = viewport->grabFramebuffer();
        QPainter painter(&shot);
        painter.drawImage(QRect(viewport->mapTo(&window, QPoint()), viewport->size()), frame);
        // grabFramebuffer excludes the floating Qt controls hosted by the viewport.
        for (auto* child : viewport->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
          if (child->isVisible())
            child->render(&painter, child->mapTo(&window, QPoint()), QRegion(), QWidget::DrawChildren);
      }
      return shot.save(Issue886Shot(name));
    };
    QVERIFY(captureDrawing("qt-topo-drawing.png"));
    QVERIFY(properties->grab().save(Issue886Shot("qt-topo-properties.png")));
    const int terrainHeight = qMin(tree->height(), tree->width() * 220 / 300);
    QVERIFY(tree->grab(QRect(0, tree->height() - terrainHeight,
                                        tree->width(), terrainHeight))
                .save(Issue886Shot("qt-topo-properties-detail.png")));
    QVERIFY(toolspace->grab().save(Issue886Shot("qt-topo-toolspace.png")));
    // Art belongs inside the tree's existing background, without consuming row space.
    const int gap = panel->viewport()->height() - tree->height();
    QVERIFY2(gap <= 8, qPrintable(QString("Contour decoration reserves %1 px below Properties").arg(gap)));
    const auto image = tree->grab().toImage();
    int amberPixels = 0;
    for (int y = image.height() * 2 / 3; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) {
        const auto c = image.pixelColor(x, y);
        amberPixels += c.red() > 30 && c.red() > c.green() * 1.25 && c.green() > c.blue();
      }
    QVERIFY2(amberPixels > 30, "The approved amber terrain contour must be visible in the Properties background");
    QImage art(300, 220, QImage::Format_ARGB32);
    art.fill(QColor("#061725"));
    { QPainter painter(&art); qtArtwork::DrawTerrainContours(painter, art.rect()); }
    auto amberNear = [&](int x, int y) {
      for (int yy = y - 4; yy <= y + 4; ++yy)
        for (int xx = x - 4; xx <= x + 4; ++xx) {
          const auto c = art.pixelColor(xx, yy);
          if (c.red() > 30 && c.red() > c.green() * 1.25 && c.green() > c.blue()) return true;
        }
      return false;
    };
    // The reference's amber ridge descends toward the lower right; it does not
    // turn upward like the repeated U-shaped wave that caused this regression.
    QVERIFY(amberNear(50, 93));
    QVERIFY(amberNear(123, 164));
    QVERIFY(amberNear(181, 213));
    QVERIFY(!amberNear(230, 100));
    QVERIFY(art.save(Issue886Shot("qt-topo-artwork.png")));
    window.resize(1672, 1100);
    tabs->setCurrentIndex(0); QTest::qWait(150);
    QVERIFY(window.grab().save(Issue886Shot("qt-topo-start.png")));
    auto* body = window.findChild<QWidget*>("startPageBody"); QVERIFY(body);
    const auto startImage = body->grab().toImage();
    int startAmber = 0;
    for (int y = startImage.height() / 2; y < startImage.height(); ++y)
      for (int x = 0; x < startImage.width() * 2 / 5; ++x) {
        const auto c = startImage.pixelColor(x, y);
        startAmber += c.red() > 30 && c.red() > c.green() * 1.25 && c.green() > c.blue();
      }
    QVERIFY2(startAmber > 80, "Recent drawings must not cover the Start-page terrain artwork");
    QVERIFY(window.findChild<QWidget*>("ribbonBar")->isVisible());
    QVERIFY(window.statusBar()->isVisible());
    tabs->setCurrentIndex(1);
    window.resize(1200, 800); QTest::qWait(150);
    // RefreshProperties replaces its tree widget when the active drawing changes. The earlier
    // pointer referred to the outgoing tab's deferred-deleted widget; reacquire it after refresh.
    tree = panel->widget(); QVERIFY(tree);
    QVERIFY(captureDrawing("qt-topo-drawing-compact.png"));
    ApplyQtAppearance(*qApp, true); QTest::qWait(100);
    QVERIFY(captureDrawing("qt-topo-light.png"));
    const auto light = tree->grab().toImage();
    amberPixels = 0;
    for (int y = light.height() * 2 / 3; y < light.height(); ++y)
      for (int x = 0; x < light.width(); ++x) {
        const auto c = light.pixelColor(x, y);
        amberPixels += c.red() > 30 && c.red() > c.green() * 1.25 && c.green() > c.blue();
      }
    QCOMPARE(amberPixels, 0);
  }

  void paperSheetShowsFillLeaderAndTable() {
    // Issue #768 (REQ-113): a sheet's own fill, multileader and table are drawn on the Qt sheet.
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    PaperLayout& sheet = window.State().paperLayouts[0];
    sheet.paperFilledRegions.push_back(CadFilledRegion{});
    CadFilledRegion& fill = sheet.paperFilledRegions.back();
    fill.loopStart = {0};
    fill.vertsXyz = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f, 3.f, 2.f, 0.f, 1.f, 2.f, 0.f};
    sheet.paperFilledRegionAttrs.push_back(EntityAttributes{});
    CadMultileader leader;
    leader.pathXyz = {4.f, 2.f, 0.f, 5.5f, 3.f, 0.f};
    leader.label.kind = CadAnnotation::Kind::Mtext;
    leader.label.text = "SEE NOTE 4";
    leader.label.plottedHeightInches = 0.2f;
    leader.label.insX = 5.5f; leader.label.insY = 3.f;
    leader.label.boxMinX = 5.5f; leader.label.boxMinY = 2.8f;
    leader.label.boxMaxX = 8.f; leader.label.boxMaxY = 3.2f;
    sheet.paperMultileaders.push_back(leader);
    sheet.paperMultileaderAttrs.push_back(EntityAttributes{});
    CadTable table;
    table.insX = 1.f; table.insY = 7.f; table.width = 4.f; table.height = 1.2f; table.cols = 2;
    table.cells = {"POINT", "ELEV", "101", "512.40"};
    table.plottedHeightInches = 0.18f;
    sheet.paperTables.push_back(table);
    sheet.paperTableAttrs.push_back(EntityAttributes{});
    SetActiveSpace(window.State(), 0);  // fits the sheet on first entry
    window.Refresh();
    window.resize(1280, 820); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(250);
    auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport"); QVERIFY(viewport);
    QVERIFY(viewport->grabFramebuffer().save(Issue907Shot("qt-768-paper-sheet.png")));
  }

  void layoutManagerCreatesAndSwitchesLayoutAndHonorsReadOnly() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    window.Refresh();
    window.resize(1280, 820); window.show(); QCoreApplication::processEvents();
    auto* strip = window.findChild<QWidget*>("paperSpaceBar"); QVERIFY(strip);
    QVERIFY(strip->grab().save(Issue897Shot("qt-897-layout-strip.png")));
    QVERIFY(window.grab().save(Issue897Shot("qt-897-layout-surface.png")));
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("layoutManager"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue897Shot("qt-897-layout-manager.png")));
      auto* add = dialog->findChild<QPushButton*>("layoutNew"); QVERIFY(add); add->click();
      auto* set = dialog->findChild<QPushButton*>("layoutSetCurrent"); QVERIFY(set); set->click();
      auto* close = dialog->findChild<QDialogButtonBox*>(); QVERIFY(close); close->button(QDialogButtonBox::Close)->click();
    });
    window.ShowLayoutManager();
    QCOMPARE(window.State().paperLayouts.size(), size_t(2));
    QCOMPARE(window.State().activeSpaceIndex, 1);
    QCOMPARE(window.findChild<QTabBar*>("paperSpaceTabs")->count(), 3);
    QVERIFY(DoUndo(window.State(), window.m_Log));
    QCOMPARE(window.State().paperLayouts.size(), size_t(1));

    window.State().drawingTabs[static_cast<size_t>(window.State().activeDrawingIdx)].projectUid = 44;
    AppCommandState::ProjectSession session; session.uid = 44; session.readOnly = true;
    window.State().openProjects.push_back(session);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("layoutManager"); QVERIFY(dialog);
      QVERIFY(!dialog->findChild<QPushButton*>("layoutNew")->isEnabled());
      dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close)->click();
    });
    window.ShowLayoutManager();
  }

  void paperTrimHighlightScreenshots() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    auto& state = window.State();
    state.activeSpaceIndex = 0;
    state.viewportZoom = 4.f;
    state.active = AppCommandState::Kind::Trim;
    auto& layout = state.paperLayouts[0];
    layout.paperLines = {2, 2, 0, 9, 2, 0, 5, 0, 0, 5, 4, 0};
    state.paperTrimCutters = {{PaperEntityRef::Type::Line, 1}};
    window.Refresh();
    window.resize(1280, 820);
    window.show();
    QCoreApplication::processEvents();
    auto* viewportWidget = window.findChild<QWidget*>("modelViewport");
    QVERIFY(viewportWidget);
    auto* viewport = static_cast<EditorViewport*>(viewportWidget);
    QTest::qWait(100);
    const QString artifacts = QtTestArtifactDirectory() + "/";
    QVERIFY(viewport->grabFramebuffer().save(artifacts + "qt-974-paper-trim-cutter-highlight.png"));
    state.paperHoverValid = true;
    state.paperHover = {PaperEntityRef::Type::Line, 0};
    viewport->update();
    QCoreApplication::processEvents();
    QVERIFY(viewport->grabFramebuffer().save(artifacts + "qt-974-paper-trim-target-prehighlight.png"));
  }

  void issue972PaperModifyCommandsRouteAndStayInPaperSpace() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    window.resize(1100, 760);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* viewport = dynamic_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport")); QVERIFY(viewport);
    QTRY_VERIFY(viewport->IsRendererReady());
    AddPaperLayout(window.State());
    auto& state = window.State();
    state.activeSpaceIndex = 0;
    state.userLinesFlat = {100, 100, 0, 110, 100, 0};
    EntityAttributes attrs; attrs.layer = "0";
    state.userLineAttrs = {attrs};
    auto& layout = state.paperLayouts[0];
    layout.paperLines = {0, 0, 0, 10, 0, 0};
    layout.paperLineAttrs = {attrs};
    const auto modelBefore = state.userLinesFlat;

    state.lengthenMode = AppCommandState::LengthenMode::Total;
    state.lengthenTotalValue = 20.f;
    state.lengthenModeValueSet = true;
    window.Submit("LENGTHEN");
    QCOMPARE(state.paperLengthenPhase, 1);
    const Camera paperCamera = CadViewCamera(state);
    float pickX = 0.f, pickY = 0.f;
    paperCamera.WorldToScreen(8, 0, 0, static_cast<float>(viewport->width()), static_cast<float>(viewport->height()),
        &pickX, &pickY);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(pickX), qRound(pickY)));
    QCOMPARE(state.paperLengthenPhase, 1);
    QVERIFY(std::abs(layout.paperLines[3] - 20.f) < 1e-4f);
    QCOMPARE(state.userLinesFlat, modelBefore);
    QVERIFY(DoUndo(state, window.m_Log));
    QVERIFY(std::abs(state.paperLayouts[0].paperLines[3] - 10.f) < 1e-4f);

    // BREAK's first pick latches the paper entity and point; Escape must discard that draft.
    window.Submit("BREAK");
    paperCamera.WorldToScreen(5, 0, 0, static_cast<float>(viewport->width()), static_cast<float>(viewport->height()),
        &pickX, &pickY);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(pickX), qRound(pickY)));
    QCOMPARE(state.paperBreakPhase, 2);
    const auto beforeCancel = state.paperLayouts[0].paperLines;
    window.CancelDraftingInput();
    QCOMPARE(state.paperBreakPhase, 0);
    QCOMPARE(state.paperLayouts[0].paperLines, beforeCancel);
    QCOMPARE(state.userLinesFlat, modelBefore);

    // A complete paper BREAK uses the same Qt command and pick entry points and is undoable.
    window.Submit("BREAK");
    SubmitViewportPick(state, 2, 0, window.m_Log);
    SubmitViewportPick(state, 8, 0, window.m_Log);
    QVERIFY(state.paperLayouts[0].paperLines.size() > beforeCancel.size());
    QCOMPARE(state.userLinesFlat, modelBefore);
    QVERIFY(DoUndo(state, window.m_Log));
    QCOMPARE(state.paperLayouts[0].paperLines, beforeCancel);

    // The other phase machines also start in paper space and consume their picks before idle
    // paper selection. Exercise phase advancement and cancellation without changing geometry.
    window.Submit("EXTEND");
    state.paperLayouts[0].paperLines.insert(state.paperLayouts[0].paperLines.end(), {0, 30, 0, 10, 30, 0,
                                                                                   15, 25, 0, 15, 35, 0});
    state.paperLayouts[0].paperLineAttrs.insert(state.paperLayouts[0].paperLineAttrs.end(), {attrs, attrs});
    const size_t beforeExtend = state.paperLayouts[0].paperLines.size();
    SubmitViewportPick(state, 15, 30, window.m_Log);
    QCOMPARE(state.paperExtendPhase, 1);
    window.Submit(""); // finish the boundary-edge pick set
    QCOMPARE(state.paperExtendPhase, 2);
    SubmitViewportPick(state, 9, 30, window.m_Log);
    QVERIFY(std::abs(state.paperLayouts[0].paperLines[beforeExtend - 12 + 3] - 15.f) < 1e-4f);
    QCOMPARE(state.userLinesFlat, modelBefore);
    QVERIFY(DoUndo(state, window.m_Log));
    QCOMPARE(state.paperLayouts[0].paperLines.size(), beforeExtend);

    PaperEntityRef paperLine{PaperEntityRef::Type::Line, 0};
    state.selectedPaperEntities = {paperLine};
    window.Submit("STRETCH");
    QCOMPARE(state.paperStretchPhase, 1);
    window.Submit("1,1");
    QCOMPARE(state.paperStretchPhase, 2);
    window.CancelDraftingInput();
    QCOMPARE(state.paperStretchPhase, 0);

    state.paperLayouts[0].paperLines.insert(state.paperLayouts[0].paperLines.end(), {0, 10, 0, 10, 10, 0,
                                                                                   10, 10, 0, 10, 0, 0});
    state.paperLayouts[0].paperLineAttrs.insert(state.paperLayouts[0].paperLineAttrs.end(), {attrs, attrs});
    window.Submit("FILLET");
    window.Submit("R"); window.Submit("1");
    SubmitViewportPick(state, 5, 10, window.m_Log);
    QCOMPARE(state.paperFilletPhase, 2);
    SubmitViewportPick(state, 10, 5, window.m_Log);
    QCOMPARE(state.paperFilletPhase, 1);
    QVERIFY(!state.paperLayouts[0].paperArcs.empty());
    QCOMPARE(state.userLinesFlat, modelBefore);
    QVERIFY(DoUndo(state, window.m_Log));

    window.Submit("CHAMFER");
    window.Submit("D"); window.Submit("1"); window.Submit("1");
    const size_t beforeChamfer = state.paperLayouts[0].paperLines.size();
    SubmitViewportPick(state, 5, 10, window.m_Log);
    QCOMPARE(state.paperChamferPhase, 2);
    SubmitViewportPick(state, 10, 5, window.m_Log);
    QCOMPARE(state.paperChamferPhase, 1);
    QVERIFY(state.paperLayouts[0].paperLines.size() > beforeChamfer);
    QCOMPARE(state.userLinesFlat, modelBefore);
    QVERIFY(DoUndo(state, window.m_Log));
    QCOMPARE(state.userLinesFlat, modelBefore);
  }

  void pageSetupManagerAndEditorPreserveCancelAndReadOnlyState() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    window.State().activeSpaceIndex = 0;
    const auto before = PageSetupFromLayout(window.State().paperLayouts[0], "");
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("pageSetupManager"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue897Shot("qt-897-page-setup-manager.png")));
      auto* close = dialog->findChild<QPushButton*>("pageSetupClose"); QVERIFY(close); close->click();
    });
    window.ShowPageSetupManager(0);
    QVERIFY(!window.State().savedPageSetups.empty());
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("pageSetupEditor"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue897Shot("qt-897-page-setup-editor.png")));
      auto* buttons = dialog->findChild<QDialogButtonBox*>(); QVERIFY(buttons);
      buttons->button(QDialogButtonBox::Cancel)->click();
      QTimer::singleShot(0, [&] {
        if (auto* manager = window.findChild<QDialog*>("pageSetupManager"))
          manager->findChild<QPushButton*>("pageSetupClose")->click();
      });
    });
    window.ShowPageSetupEditor(-1);
    QCOMPARE(window.State().paperLayouts[0].portraitWidthIn, before.portraitWidthIn);
    QCOMPARE(window.State().paperLayouts[0].portraitHeightIn, before.portraitHeightIn);
    window.State().drawingTabs[static_cast<size_t>(window.State().activeDrawingIdx)].projectUid = 45;
    AppCommandState::ProjectSession session; session.uid = 45; session.readOnly = true;
    window.State().openProjects.push_back(session);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("pageSetupManager"); QVERIFY(dialog);
      QVERIFY(!dialog->findChild<QPushButton*>("pageSetupNew")->isEnabled());
      dialog->findChild<QPushButton*>("pageSetupClose")->click();
    });
    window.ShowPageSetupManager(0);
  }

  void viewportScaleWorkflowValidatesAndSupportsUndo() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    window.State().annotationScales = {{"1:50", 1.f, 50.f}, {"1:100", 1.f, 100.f}};
    window.State().currentAnnotationScaleIndex = 0;
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    window.State().activeSpaceIndex = 0;
    const int viewport = AddViewport(window.State(), 0);
    QCOMPARE(QString::fromUtf8(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].annotationScaleName.c_str()),
             QString("1:50"));
    const float initial = window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn;
    window.Submit("VPSCALE 1:25");
    QVERIFY(std::fabs(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn - 25.f) < 1e-4f);
    window.Submit("VPSCALE not-a-scale");
    QVERIFY(std::fabs(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn - 25.f) < 1e-4f);
    QVERIFY(DoUndo(window.State(), window.m_Log));
    QCOMPARE(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn, initial);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("viewportsManager"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue897Shot("qt-1013-viewport-scale-default.png")));
      auto* annotationScale = dialog->findChild<QComboBox*>("viewportAnnotationScale"); QVERIFY(annotationScale);
      QCOMPARE(annotationScale->currentText(), QString("1:50"));
      annotationScale->setCurrentText("1:100");
      QVERIFY(dialog->grab().save(Issue897Shot("qt-1013-viewport-scale-selected.png")));
      QTimer::singleShot(0, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
          if (auto* scaleDialog = qobject_cast<QInputDialog*>(widget); scaleDialog && scaleDialog->windowTitle() == "Viewport Scale") {
            QVERIFY(scaleDialog->grab().save(Issue897Shot("qt-897-viewport-scale.png")));
            scaleDialog->reject();
            return;
          }
        QFAIL("Viewport Scale input dialog did not open");
      });
      dialog->findChild<QPushButton*>("viewportScale")->click();
      dialog->findChild<QPushButton*>("viewportClose")->click();
    });
    window.ShowViewportsManager();
    QCOMPARE(QString::fromUtf8(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].annotationScaleName.c_str()),
             QString("1:100"));
  }

  void paperSpaceRendersSheetTablesAndViewportModelContent() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    window.State().activeSpaceIndex = 0;
    window.State().viewportZoom = 3.5f;
    window.State().userLinesFlat = {0, 0, 0, 100, 100, 0};
    EntityAttributes lineAttributes;
    lineAttributes.layer = "0";
    window.State().userLineAttrs = {lineAttributes};
    window.Refresh();
    window.resize(1280, 820);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* viewport = dynamic_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport")); QVERIFY(viewport);
    QTRY_VERIFY(viewport->IsRendererReady());
    auto* spaces = window.findChild<QTabBar*>("paperSpaceTabs"); QVERIFY(spaces);
    QCOMPARE(window.State().activeSpaceIndex, 0);

    auto& layout = window.State().paperLayouts[0];
    layout.paperLines = {1, 1, 0, 5, 5, 0};
    CadArc paperArc;
    paperArc.cx = 8.; paperArc.cy = 6.; paperArc.r = 1.5; paperArc.startRad = 0.f; paperArc.sweepRad = 1.57079632679f;
    layout.paperArcs.push_back(paperArc);
    CadEllipse paperEllipse;
    paperEllipse.cx = 12.; paperEllipse.cy = 6.; paperEllipse.majVx = 1.2f; paperEllipse.majVy = 0.f;
    paperEllipse.ratio = 0.5f;
    layout.paperEllipses.push_back(paperEllipse);
    CadAnnotation table;
    table.kind = CadAnnotation::Kind::Table;
    table.boxMinX = 6; table.boxMinY = 2; table.boxMaxX = 10; table.boxMaxY = 4;
    table.tableCols = 2; table.tableCells = {"Station", "Northing", "1+00", "24.5"};
    layout.paperTexts.push_back(table);
    Viewport layoutViewport;
    layoutViewport.modelCenterX = 50.; layoutViewport.modelCenterY = 50.;
    layout.viewports.push_back(layoutViewport);

    const Camera paperCamera = CadViewCamera(window.State());
    float screenX = 0.f, screenY = 0.f;
    paperCamera.WorldToScreen(7.f, 3.5f, 0.f, static_cast<float>(viewport->width()),
        static_cast<float>(viewport->height()), &screenX, &screenY);
    QTimer::singleShot(0, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
          dialog->setTextValue("Control point");
          dialog->accept();
        }
    });
    QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(screenX), qRound(screenY)));
    QCOMPARE(window.State().paperLayouts[0].paperTexts[0].tableCells[0], std::string("Control point"));
    QVERIFY(DoUndo(window.State(), window.m_Log));
    QCOMPARE(window.State().paperLayouts[0].paperTexts[0].tableCells[0], std::string("Station"));
    QTimer::singleShot(0, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* dialog = qobject_cast<QInputDialog*>(widget)) dialog->reject();
    });
    QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(screenX), qRound(screenY)));
    QCOMPARE(window.State().paperLayouts[0].paperTexts[0].tableCells[0], std::string("Station"));

    window.Submit("OFFSET");
    QCOMPARE(window.State().active, AppCommandState::Kind::Offset);
    window.Submit("1");
    QCOMPARE(window.State().offsetPhase, AppCommandState::OffsetPhase::WaitSelectEntity);
    QCOMPARE(window.State().activeSpaceIndex, 0);
    SubmitViewportPick(window.State(), 3, 3, window.m_Log);
    QVERIFY2(window.State().offsetPhase == AppCommandState::OffsetPhase::WaitSidePick,
             window.m_Log.empty() ? "no OFFSET log" : window.m_Log.back().c_str());
    QCOMPARE(window.State().offsetPhase, AppCommandState::OffsetPhase::WaitSidePick);
    SubmitViewportPick(window.State(), 3, 5, window.m_Log);
    QCOMPARE(window.State().paperLayouts[0].paperLines.size(), size_t(12));
    QCOMPARE(window.State().userLinesFlat.size(), size_t(6));

    viewport->update();
    QTest::qWait(100);
    const QImage rendered = viewport->grabFramebuffer();
    const QString evidenceDir = QtTestArtifactDirectory();
    QVERIFY(QDir().mkpath(evidenceDir));
    QVERIFY(rendered.save(evidenceDir + "/req039-paper-offset-layout.png"));
    float viewportX = 0.f, viewportY = 0.f;
    paperCamera.WorldToScreen(layoutViewport.paperXIn + layoutViewport.paperWIn * 0.5,
        layoutViewport.paperYIn + layoutViewport.paperHIn * 0.5, 0.f, static_cast<float>(viewport->width()),
        static_cast<float>(viewport->height()), &viewportX, &viewportY);
    const QPoint modelCenter(qRound(viewportX * viewport->devicePixelRatioF()),
        qRound(viewportY * viewport->devicePixelRatioF()));
    int litCenterPixels = 0;
    for (int y = modelCenter.y() - 4; y <= modelCenter.y() + 4; ++y)
      for (int x = modelCenter.x() - 4; x <= modelCenter.x() + 4; ++x)
        if (rendered.rect().contains(x, y) && qGray(rendered.pixel(x, y)) > 40) ++litCenterPixels;
    QVERIFY(litCenterPixels > 0);
    QCOMPARE(window.State().paperLayouts[0].paperTexts[0].tableCells[3], std::string("24.5"));
    QVERIFY(spaces->count() >= 2);
  }

  void paperOffsetSupportsNativeGeometryAndThroughMode() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    auto& st = window.State();
    st.activeSpaceIndex = 0;
    auto& layout = st.paperLayouts[0];
    layout.paperLines = {2, 2, 0, 4, 2, 0};
    layout.paperCircles = {7, 2, 1};
    CadArc arc; arc.cx = 10; arc.cy = 2; arc.r = 1; arc.startRad = 0; arc.sweepRad = 3.14159265f;
    layout.paperArcs.push_back(arc);
    CadEllipse ellipse; ellipse.cx = 13; ellipse.cy = 2; ellipse.majVx = 1; ellipse.majVy = 0; ellipse.ratio = .5f;
    layout.paperEllipses.push_back(ellipse);
    layout.paperPolyOffsets = {0, 2};
    layout.paperPolyVerts = {1, 6, 0, 5, 6, 0};
    layout.paperPolyClosed = {0};

    bool typedOffsetStarted = false;
    auto offsetBySide = [&](double x, double y, double sideX, double sideY, SelectedEntity::Type expectedType) {
      if (!typedOffsetStarted) {
        window.Submit("OFFSET"); window.Submit("1");
        typedOffsetStarted = true;
      }
      SubmitViewportPick(st, x, y, window.m_Log);
      QVERIFY2(st.offsetEntityValid, window.m_Log.empty() ? "OFFSET did not latch a paper entity" : window.m_Log.back().c_str());
      QCOMPARE(st.offsetEntity.type, expectedType);
      SubmitViewportPick(st, sideX, sideY, window.m_Log);
    };
    offsetBySide(3, 2, 3, 4, SelectedEntity::Type::LineSeg);
    QCOMPARE(layout.paperLines.size(), size_t(12));
    QCOMPARE(st.userLinesFlat.size(), size_t(0));
    offsetBySide(8, 2, 9, 2, SelectedEntity::Type::Circle);
    QCOMPARE(layout.paperCircles.size(), size_t(6));
    offsetBySide(11, 2, 12, 2, SelectedEntity::Type::Arc);
    QVERIFY2(layout.paperArcs.size() == 2,
             window.m_Log.empty() ? "paper arc OFFSET did not append a copy" : window.m_Log.back().c_str());
    offsetBySide(14, 2, 15, 2, SelectedEntity::Type::Ellipse);
    QCOMPARE(layout.paperEllipses.size(), size_t(2));
    offsetBySide(3, 6, 3, 8, SelectedEntity::Type::Polyline);
    QCOMPARE(layout.paperPolyOffsets.size(), size_t(3));
    QCOMPARE(layout.paperPolyVerts.size(), size_t(12));

    window.CancelDraftingInput();
    CadAnnotation unsupportedText;
    unsupportedText.kind = CadAnnotation::Kind::Text;
    unsupportedText.insX = 18; unsupportedText.insY = 18;
    unsupportedText.plottedHeightInches = 0.5f;
    unsupportedText.text = "cannot offset";
    layout.paperTexts.push_back(unsupportedText);
    float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    PaperTextBoundsIn(unsupportedText, &bx0, &by0, &bx1, &by1);
    window.Submit("OFFSET"); window.Submit("1");
    SubmitViewportPick(st, (bx0 + bx1) * 0.5, (by0 + by1) * 0.5, window.m_Log);
    QCOMPARE(st.offsetPhase, AppCommandState::OffsetPhase::WaitSelectEntity);
    QVERIFY(!window.m_Log.empty());
    QVERIFY(QString::fromStdString(window.m_Log.back()).contains("paper text cannot be offset"));
    window.CancelDraftingInput();

    layout.paperLines.insert(layout.paperLines.end(), {1, 10, 0, 5, 10, 0});
    window.Submit("OFFSET"); window.Submit("T");
    SubmitViewportPick(st, 3, 10, window.m_Log);
    SubmitViewportPick(st, 3, 12, window.m_Log);
    QCOMPARE(layout.paperLines.size(), size_t(24));
    window.CancelDraftingInput();
    QVERIFY(DoUndo(st, window.m_Log));
    QCOMPARE(layout.paperLines.size(), size_t(18));
    QCOMPARE(st.userLinesFlat.size(), size_t(0));
  }

  void dwgExportWarningShowsLossAndOverwriteAndCancelPreservesFormat() {
    EditorWindow window;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("existing.dwg");
    QFile existing(path);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("original");
    existing.close();

    window.State().dwgExportVersion = DwgSaveVersion::R2018;
    window.State().cadPipeRuns.push_back(CadPipeRun{});
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("dwgExportWarning"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-884-dwg-loss-r2018.png")));
      auto* version = dialog->findChild<QComboBox*>("dwgExportVersion"); QVERIFY(version);
      auto* details = dialog->findChild<QLabel*>("dwgExportLossDetails"); QVERIFY(details);
      QVERIFY(details->text().contains("existing.dwg already exists"));
      QVERIFY(details->text().contains("AC1032"));
      version->setCurrentIndex(0);
      QVERIFY(details->text().contains("AC1015"));
      dialog->findChild<QPushButton*>("cancelDwgExport")->click();
    });
    QVERIFY(!window.ConfirmDwgExport(path));
    QCOMPARE(window.State().dwgExportVersion, DwgSaveVersion::R2018);
    QFile check(path);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("original"));

    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("dwgExportWarning"); QVERIFY(dialog);
      auto* version = dialog->findChild<QComboBox*>("dwgExportVersion"); QVERIFY(version);
      version->setCurrentIndex(0);
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-884-dwg-overwrite-r2000.png")));
      dialog->findChild<QPushButton*>("confirmDwgExport")->click();
    });
    QVERIFY(window.ConfirmDwgExport(path));
    QCOMPARE(window.State().dwgExportVersion, DwgSaveVersion::R2000);
  }

  void rawObservationsSurviveStructuredSessionSave() {
    auto state = StateWithTraverse();
    TraverseMeasSet raw;
    raw.setNo = 1; raw.hasF1 = true; raw.f1HzDec = 90; raw.f1Sd = 100.1;
    state->traverseData.legs[0].rawSets.push_back(raw);
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.show();
    QTRY_VERIFY(window.isVisible());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString path = directory.filePath("session.gstrav");
    QVERIFY2(window.SaveFile(path, error), qPrintable(error));
    TraverseData loaded;
    LsaWeights weights;
    std::string reason;
    QVERIFY2(traverseio::Load(path.toUtf8().toStdString(), loaded, weights, reason), reason.c_str());
    QCOMPARE(loaded.legs.size(), std::size_t(4));
    QCOMPARE(loaded.legs[0].rawSets.size(), std::size_t(1));
    QCOMPARE(loaded.legs[0].rawSets[0].f1Sd, 100.1);
    QVERIFY(!window.HasUnsavedChanges());
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QVERIFY(!window.HasUnsavedChanges());
  }

  void projectSettingsApplyPersistsAndCancelDiscardsStagedChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    gsproj::Project created;
    std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "North Ridge", &created, &error), error.c_str());

    EditorWindow window;
    const auto projectUid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(projectUid != 0);
    QAction* action = window.findChild<QAction*>("command_PROJECTSETTINGS");
    QVERIFY(action);
    QTimer::singleShot(15000, &window, [&window] {
      if (auto* dialog = window.findChild<QDialog*>("projectSettingsDialog")) dialog->reject();
    });
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectSettingsDialog");
      QVERIFY(dialog);
      auto* units = dialog->findChild<QComboBox*>("projectLinearUnits");
      auto* category = dialog->findChild<QComboBox*>("projectZoneCategory");
      auto* systems = dialog->findChild<QComboBox*>("projectCoordinateSystem");
      auto* angular = dialog->findChild<QComboBox*>("projectAngularUnits");
      auto* foot = dialog->findChild<QComboBox*>("projectFootDefinition");
      auto* scale = dialog->findChild<QComboBox*>("projectPlotScale");
      auto* customScale = dialog->findChild<QDoubleSpinBox*>("projectCustomScale");
      auto* inserted = dialog->findChild<QCheckBox*>("projectScaleInsertedObjects");
      auto* layer = dialog->findChild<QComboBox*>("projectLayerName0");
      auto* applyButton = dialog->findChild<QPushButton*>("projectSettingsApply");
      auto* buttons = dialog->findChild<QDialogButtonBox*>();
      auto* navigation = dialog->findChild<QListWidget*>("projectSettingsNavigation");
      auto* pages = dialog->findChild<QStackedWidget*>("projectSettingsPages");
      QVERIFY(units && category && systems && angular && foot && scale && customScale && inserted && layer && applyButton && buttons && navigation && pages);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-886-project-settings-units.png")));
      navigation->setCurrentRow(1);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-886-project-settings-object-layers.png")));
      navigation->setCurrentRow(0);
      units->setCurrentIndex(units->findData(6));
      QVERIFY(category->count() > 1);
      category->setCurrentIndex(1);
      QVERIFY(!systems->currentData().toString().isEmpty());
      angular->setCurrentIndex(1);
      foot->setCurrentIndex(1);
      scale->setCurrentIndex(scale->findData(-1.0));
      customScale->setValue(123.5);
      inserted->setChecked(false);
      layer->setCurrentText("C-PROJECT-POINT");
      applyButton->click();
      QVERIFY(dialog->isVisible());

      auto& session = *std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
          [projectUid](const auto& item) { return item.uid == projectUid; });
      QCOMPARE(session.settings->insUnits, 6);
      QCOMPARE(session.settings->defaults.angularUnits, DrawingSettings::AngularUnits::Radians);
      QCOMPARE(session.settings->defaults.footDefinition, DrawingSettings::FootDefinition::International);
      QVERIFY(!session.settings->defaults.scaleInsertedObjects);
      QCOMPARE(session.settings->zoneCode, systems->currentData().toString().toUtf8().toStdString());
      QCOMPARE(session.settings->defaults.objectLayers[0].layer, std::string("C-PROJECT-POINT"));
      QVERIFY(std::fabs(session.settings->plotScale - 123.5f) < 0.001f);

      gsproj::Project stored;
      QVERIFY2(gsproj::Load(created.file, &stored, &error), error.c_str());
      ProjectSettings reloaded;
      QVERIFY2(ParseProjectSettings(stored.settingsJson, &reloaded, &error), error.c_str());
      QCOMPARE(reloaded.insUnits, 6);
      QCOMPARE(reloaded.zoneCode, session.settings->zoneCode);
      QCOMPARE(reloaded.defaults.objectLayers[0].layer, std::string("C-PROJECT-POINT"));

      units->setCurrentIndex(units->findData(4));
      buttons->button(QDialogButtonBox::Cancel)->click();
      QCOMPARE(session.settings->insUnits, 6);
    });
    action->trigger();
  }

  void projectHealthPackAndTurnoverDialogsAreNativeAndDismissWithoutChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    gsproj::Project created;
    std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Project Workflows", &created, &error), error.c_str());
    EditorWindow window;
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    const auto tracked = created.Folder() / "PDFs" / "current-sheet.pdf";
    { std::ofstream output(tracked, std::ios::binary); output << "tracked pdf fixture"; }
    QVERIFY(TrackProjectFile(window.State(), uid, tracked.string(), window.m_Log));
    for (const char* command : {"command_PROJECTHEALTH", "command_PACKPROJECT", "command_OPENPACK", "command_TURNOVER", "command_REFRESH"})
      QVERIFY2(window.findChild<QAction*>(command), command);

    bool healthShown = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectHealthDialog"); QVERIFY(dialog);
      healthShown = dialog->findChild<QLabel*>("projectHealthStatus")->text().contains("issue(s)") &&
          dialog->findChild<QPlainTextEdit*>("projectHealthDetails")->toPlainText().contains("unsaved", Qt::CaseInsensitive);
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-a-health.png");
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-final-health.png");
      dialog->reject();
    });
    window.ShowProjectHealth(uid);
    QVERIFY(healthShown);

    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectHealthDialog"); if (!dialog) return;
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-b-health.png");
      dialog->reject();
    });
    window.ShowProjectHealth(uid);
    ApplyQtAppearance(*qApp, false);

    bool packShown = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectPackDialog"); if (!dialog) return;
      packShown = dialog->findChild<QLabel*>("projectPackSummary")->text().contains("file");
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-a-pack.png");
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-final-pack.png");
      dialog->reject();
    });
    window.ShowProjectPack(uid);
    QVERIFY(packShown);
    QVERIFY(!std::filesystem::exists(created.Folder() / "Project Workflows.gspack"));

    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectPackDialog"); if (!dialog) return;
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-b-pack.png");
      dialog->reject();
    });
    window.ShowProjectPack(uid);
    ApplyQtAppearance(*qApp, false);

    bool turnoverShown = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectTurnoverDialog"); if (!dialog) return;
      turnoverShown = dialog->findChild<QLineEdit*>("projectTurnoverRecipient") != nullptr;
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-a-turnover.png");
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-final-turnover.png");
      dialog->reject();
    });
    window.ShowProjectTurnover(uid);
    QVERIFY(turnoverShown);
    QVERIFY(projturn::List(created, nullptr).empty());

    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectTurnoverDialog"); if (!dialog) return;
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-b-turnover.png");
      dialog->reject();
    });
    window.ShowProjectTurnover(uid);
    ApplyQtAppearance(*qApp, false);

    const auto untracked = created.Folder() / "PDFs" / "new-sheet.pdf";
    { std::ofstream output(untracked, std::ios::binary); output << "new pdf"; }
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectRefreshDialog"); if (!dialog) return;
      auto* list = dialog->findChild<QListWidget*>("projectRefreshFiles");
      if (!list || list->count() != 1) { dialog->reject(); return; }
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-a-refresh.png");
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-final-refresh.png");
      dialog->findChild<QPushButton*>("projectRefreshAddSelected")->click();
    });
    window.RefreshProjectFiles();
    auto session = std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid; });
    QVERIFY(session != window.State().openProjects.end());
    QVERIFY(std::any_of(session->project.items.begin(), session->project.items.end(),
        [](const auto& item) { return item.path == "PDFs/new-sheet.pdf"; }));

    const auto secondUntracked = created.Folder() / "PDFs" / "new-sheet-light.pdf";
    { std::ofstream output(secondUntracked, std::ios::binary); output << "second new pdf"; }
    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectRefreshDialog"); if (!dialog) return;
      dialog->grab().save(QtTestArtifactDirectory() + "/qt-889-option-b-refresh.png");
      dialog->reject();
    });
    window.RefreshProjectFiles();
    ApplyQtAppearance(*qApp, false);

    const auto before = projturn::List(session->project, nullptr).size();
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectTurnoverDialog"); if (!dialog) return;
      dialog->findChild<QLineEdit*>("projectTurnoverRecipient")->setText("Acme Design");
      if (auto* acknowledge = dialog->findChild<QCheckBox*>("projectTurnoverAcknowledge")) acknowledge->setChecked(true);
      dialog->findChild<QPushButton*>("projectTurnoverCreate")->click();
    });
    window.ShowProjectTurnover(uid);
    session = std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid; });
    QVERIFY(session != window.State().openProjects.end());
    QCOMPARE(projturn::List(session->project, nullptr).size(), before + 1);
  }

  void projectSettingsValidationBlocksSaveAndCancelLeavesProjectUntouched() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    gsproj::Project created;
    std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Validation Job", &created, &error), error.c_str());

    EditorWindow window;
    const auto projectUid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(projectUid != 0);
    QTimer::singleShot(15000, &window, [&window] {
      if (auto* dialog = window.findChild<QDialog*>("projectSettingsDialog")) dialog->reject();
    });
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectSettingsDialog");
      QVERIFY(dialog);
      auto* layer = dialog->findChild<QComboBox*>("projectLayerName0");
      auto* apply = dialog->findChild<QPushButton*>("projectSettingsApply");
      auto* validation = dialog->findChild<QLabel*>("projectSettingsValidation");
      auto* navigation = dialog->findChild<QListWidget*>("projectSettingsNavigation");
      QVERIFY(layer && apply && validation);
      QVERIFY(navigation);
      navigation->setCurrentRow(1);
      layer->setCurrentText("");
      QVERIFY(!validation->text().isEmpty());
      QVERIFY(!apply->isEnabled());
      QVERIFY(dialog->grab().save(Issue886Shot("qt-886-project-settings-validation.png")));
      dialog->reject();
    });
    window.ShowProjectSettings(projectUid);
    auto& session = *std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [projectUid](const auto& item) { return item.uid == projectUid; });
    QVERIFY(!session.settings->hasDefaults);
    gsproj::Project stored;
    QVERIFY2(gsproj::Load(created.file, &stored, &error), error.c_str());
    ProjectSettings reloaded;
    QVERIFY(ParseProjectSettings(stored.settingsJson, &reloaded, &error));
    QVERIFY(!reloaded.hasDefaults);
  }

  void projectSettingsSaveFailureAndReadOnlyDoNotChangeSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    gsproj::Project created;
    std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Locked Job", &created, &error), error.c_str());

    EditorWindow window;
    const auto projectUid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(projectUid != 0);
    auto& session = *std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [projectUid](const auto& item) { return item.uid == projectUid; });
    const auto originalPath = session.project.file;
    session.project.file = originalPath.parent_path() / "missing-folder" / originalPath.filename();
    QTimer::singleShot(15000, &window, [&window] {
      if (auto* dialog = window.findChild<QDialog*>("projectSettingsDialog")) dialog->reject();
    });
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectSettingsDialog");
      QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-886-project-settings-save-error.png")));
      auto* units = dialog->findChild<QComboBox*>("projectLinearUnits");
      auto* apply = dialog->findChild<QPushButton*>("projectSettingsApply");
      QVERIFY(units && apply);
      units->setCurrentIndex(units->findData(6));
      QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets())
          if (auto* message = qobject_cast<QMessageBox*>(widget)) {
            message->grab().save(Issue886Shot("qt-886-project-settings-save-error-message.png"));
            message->accept();
          }
      });
      apply->click();
      QCOMPARE(session.settings->insUnits, -1);
      dialog->reject();
    });
    window.ShowProjectSettings(projectUid);
    session.project.file = originalPath;
    QVERIFY(!session.settings->hasDefaults);

    session.readOnly = true;
    QTimer::singleShot(15000, &window, [&window] {
      if (auto* dialog = window.findChild<QDialog*>("projectSettingsDialog")) dialog->reject();
    });
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectSettingsDialog");
      QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-886-project-settings-read-only.png")));
      auto* units = dialog->findChild<QComboBox*>("projectLinearUnits");
      auto* apply = dialog->findChild<QPushButton*>("projectSettingsApply");
      QVERIFY(units && apply);
      units->setCurrentIndex(units->findData(6));
      QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets())
          if (auto* message = qobject_cast<QMessageBox*>(widget)) {
            message->grab().save(Issue886Shot("qt-886-project-settings-read-only-message.png"));
            message->accept();
          }
      });
      apply->click();
      QCOMPARE(session.settings->insUnits, -1);
      dialog->reject();
    });
    window.ShowProjectSettings(projectUid);
    session.readOnly = false;
    QVERIFY(!session.settings->hasDefaults);
  }

  void addDrawingGuidedWorkflowConvertsReviewsConflictsAndCopiesIntoProject() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    gsproj::Project created;
    std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "North Ridge", &created, &error), error.c_str());
    const auto sourcePath = directory.path() + "/Site Plan.dwg";
    AppCommandState source;
    source.drawingInsUnits = 2; // feet; the project below uses inches to exercise Convert.
    SurveyPoint same; same.id = 11; same.easting = 10; same.northing = 20; same.elevation = 3;
    same.description = "EG"; same.labelStyle = SurveyPointLabelStyle::None;
    SurveyPoint conflict = same; conflict.id = 12; conflict.easting = 30; conflict.description = "FG";
    SurveyPoint fresh = same; fresh.id = 13; fresh.easting = 40; fresh.northing = 50; fresh.elevation = 6;
    source.surveyPoints = {same, conflict, fresh};
    std::vector<std::string> log;
    QVERIFY(ExportDwgFile(source, sourcePath.toUtf8().constData(), log));
    QFile original(sourcePath); QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll(); original.close();

    EditorWindow window;
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    auto project = std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid; });
    QVERIFY(project != window.State().openProjects.end());
    project->settings->insUnits = 1; // inches
    SurveyPoint projectSame = same; projectSame.easting *= 12; projectSame.northing *= 12; projectSame.elevation *= 12;
    project->points->points.push_back({projectSame, "Drawings/Existing.dwg"});
    SurveyPoint differing = conflict; differing.easting = 361; differing.northing *= 12; differing.elevation *= 12;
    project->points->points.push_back({differing, "Drawings/Existing.dwg"});
    QVERIFY(FlushProjectPointDb(*project, log));

    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("addDrawingDialog"); QVERIFY(dialog);
      auto* pages = dialog->findChild<QStackedWidget*>("addDrawingPages"); QVERIFY(pages);
      auto* convert = dialog->findChild<QPushButton*>("addDrawingConvert"); QVERIFY(convert);
      auto* next = dialog->findChild<QPushButton*>("addDrawingNext"); QVERIFY(next);
      auto* back = dialog->findChild<QPushButton*>("addDrawingBack"); QVERIFY(back);
      auto* steps = dialog->findChild<QLabel*>("addDrawingSteps"); QVERIFY(steps);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-align.png")));
      convert->click();
      QVERIFY(dialog->findChild<QLabel*>("addDrawingAlignment")->text().contains("Converted copy"));
      QTest::qWait(50);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-aligned.png")));
      next->click(); QCOMPARE(pages->currentIndex(), 1);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-points.png")));
      QCOMPARE(dialog->findChild<QLabel*>("addDrawingSummary")->text().contains("1 new"), true);
      auto* choice = dialog->findChild<QComboBox*>("addDrawingChoice_12"); QVERIFY(choice);
      choice->setCurrentIndex(2); // Renumber the differing point.
      next->click(); QCOMPARE(pages->currentIndex(), 2);
      QTest::qWait(50);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-confirm.png")));
      QVERIFY(dialog->findChild<QLabel*>("addDrawingImpact")->text().contains("Site Plan.dwg"));
      next->click();
    });
    window.ShowAddDrawing(uid, sourcePath);
    QVERIFY(std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid && value.points && value.points->points.size() == 4; }) != window.State().openProjects.end());
    QVERIFY(window.State().drawingTabs.size() >= 3);
    QVERIFY(window.State().activeDocFilePath.find("North Ridge") != std::string::npos);
    QVERIFY(std::filesystem::exists(std::filesystem::path(created.Folder()) / "Drawings" / "Site Plan.dwg"));
    original.setFileName(sourcePath); QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes); original.close();

    const auto tabCount = window.State().drawingTabs.size();
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("addDrawingDialog"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-cancel.png")));
      dialog->findChild<QPushButton*>("addDrawingCancel")->click();
    });
    window.ShowAddDrawing(uid, sourcePath);
    QCOMPARE(window.State().drawingTabs.size(), tabCount);
    project = std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid; });
    QVERIFY(project != window.State().openProjects.end());

    const auto blockedFolder = std::filesystem::path(created.Folder()) / "NoDirectory";
    { std::ofstream blocked(blockedFolder, std::ios::binary); blocked << "not a directory"; }
    project->project.layout["drawings"] = "NoDirectory";
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("addDrawingDialog"); QVERIFY(dialog);
      dialog->findChild<QPushButton*>("addDrawingConvert")->click();
      dialog->findChild<QPushButton*>("addDrawingNext")->click();
      dialog->findChild<QPushButton*>("addDrawingNext")->click();
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-failure.png")));
      dialog->findChild<QPushButton*>("addDrawingNext")->click();
      auto* validation = dialog->findChild<QLabel*>("addDrawingValidation"); QVERIFY(validation);
      QVERIFY(!validation->text().isEmpty());
      QTest::qWait(50);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-write-error.png")));
      dialog->findChild<QPushButton*>("addDrawingCancel")->click();
    });
    window.ShowAddDrawing(uid, sourcePath);
    QCOMPARE(window.State().drawingTabs.size(), tabCount);
    QVERIFY(std::filesystem::exists(blockedFolder));
    project = std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid; });
    QVERIFY(project != window.State().openProjects.end());
    QCOMPARE(project->points->points.size(), size_t(4));
    project->project.layout["drawings"] = "Drawings";

    const auto databasePath = std::filesystem::path(created.Folder()) / "Points" / "survey-points.gspdb";
    QVERIFY(std::filesystem::remove(databasePath));
    QVERIFY(std::filesystem::create_directory(databasePath)); // atomic replace cannot rename over this directory
    const auto databaseSourcePath = directory.path() + "/Database Failure.dwg";
    AppCommandState databaseSource;
    databaseSource.drawingInsUnits = 2;
    SurveyPoint databasePoint = fresh; databasePoint.id = 99;
    databaseSource.surveyPoints = {databasePoint};
    QVERIFY(ExportDwgFile(databaseSource, databaseSourcePath.toUtf8().constData(), log));
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("addDrawingDialog"); QVERIFY(dialog);
      dialog->findChild<QPushButton*>("addDrawingConvert")->click();
      dialog->findChild<QPushButton*>("addDrawingNext")->click();
      dialog->findChild<QPushButton*>("addDrawingNext")->click();
      dialog->findChild<QPushButton*>("addDrawingNext")->click();
      auto* validation = dialog->findChild<QLabel*>("addDrawingValidation"); QVERIFY(validation);
      QVERIFY(validation->text().contains("point database could not be saved"));
      QTest::qWait(50);
      QVERIFY(dialog->grab().save(Issue886Shot("qt-887-add-drawing-database-save-error.png")));
      dialog->findChild<QPushButton*>("addDrawingCancel")->click();
    });
    window.ShowAddDrawing(uid, databaseSourcePath);
    project = std::find_if(window.State().openProjects.begin(), window.State().openProjects.end(),
        [uid](const auto& value) { return value.uid == uid; });
    QVERIFY(project != window.State().openProjects.end());
    QCOMPARE(project->points->points.size(), size_t(4));
    QVERIFY(!std::filesystem::exists(std::filesystem::path(created.Folder()) / "Drawings" / "Database Failure.dwg"));

    project->readOnly = true;
    QTimer::singleShot(0, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = qobject_cast<QMessageBox*>(widget)) {
          message->grab().save(Issue886Shot("qt-887-add-drawing-read-only.png"));
          message->accept();
        }
    });
    window.ShowAddDrawing(uid, sourcePath);
    QCOMPARE(window.State().drawingTabs.size(), tabCount);
  }

  static QString SampleCrashReport() {
    return QString(
        "GoSurvey stopped because of an unexpected error.\n\n"
        "Summary:\n"
        "Access violation — the program tried to read or write memory it does not own.\n"
        "Exception code: 0xC0000005\n"
        "Fault address: 0x00007FF6A1B2C3D4\n"
        "Access: write at 0x0000000000000001\n\n"
        "version: ") + GOSURVEY_VERSION_FULL + "\nchannel: stable\n\n"
        "Stack trace (most recent call first):\n"
        "  at 0x00007FF6A1B2C3D4 in C:\\Program Files\\GoSurvey\\GoSurveyQt.exe + 0x12C3D4\n"
        "  at 0x00007FF6A1B0F120 in C:\\Program Files\\GoSurvey\\GoSurveyQt.exe + 0x10F120\n"
        "  at 0x00007FFC81E2257D in C:\\WINDOWS\\System32\\KERNEL32.DLL + 0x1257D\n"
        "  at 0x00007FFC83E8AF28 in C:\\WINDOWS\\SYSTEM32\\ntdll.dll + 0x5AF28\n\n"
        "A copy of this report was appended to:\n"
        "C:\\Users\\surveyor\\AppData\\Roaming\\GoSurvey\\crash.log\n\n"
        "Use \"Send This Report\" to copy this text and open GitHub to file an issue.";
  }

  static void CaptureNextMessage(QString& text) {
    QTimer::singleShot(0, [&text] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = qobject_cast<QMessageBox*>(widget)) {
          text = message->text();
          message->button(QMessageBox::Ok)->click();
        }
    });
  }

  void failureReportWindowSendsWithoutClosingAndCloseSendsNothing() {
    const QString report = SampleCrashReport();
    QList<QUrl> opened;
    bool browserWorks = true;
    std::unique_ptr<QDialog> dialog(CreateFailureReportDialog(
        "GoSurvey encountered a problem", report, nullptr,
        [&opened, &browserWorks](const QUrl& url) { opened.push_back(url); return browserWorks; }));
    dialog->show();
    QTRY_VERIFY(dialog->isVisible());
    QCOMPARE(dialog->windowTitle(), QString("GoSurvey encountered a problem"));
    QVERIFY(dialog->findChild<QLabel*>("failureReportHeading"));
    QVERIFY(dialog->findChild<QLabel*>("failureReportNextStepsHeading"));
    QVERIFY(dialog->findChild<QLabel*>("failureReportExplanation"));
    QVERIFY(dialog->findChild<QFrame*>("failureReportNextSteps"));
    QVERIFY(dialog->findChild<QFrame*>("failureReportHeader"));
    auto* text = dialog->findChild<QPlainTextEdit*>("failureReportText");
    QVERIFY(text);
    QVERIFY(text->isReadOnly());
    QCOMPARE(text->toPlainText(), report);
    auto* send = dialog->findChild<QPushButton*>("failureReportSend");
    auto* close = dialog->findChild<QPushButton*>("failureReportClose");
    QVERIFY(send && close);
    QCOMPARE(send->text(), QString("Send This Report"));
    QVERIFY(close->isDefault());

    const bool clipboardWorks = ClipboardRoundTrips();
    QApplication::clipboard()->clear();
    QString message;
    CaptureNextMessage(message);
    QTest::mouseClick(send, Qt::LeftButton);
    if (clipboardWorks) QCOMPARE(QApplication::clipboard()->text(), report);
    QCOMPARE(opened.size(), 1);
    const QUrl url = opened.front();
    QCOMPARE(url.host(), QString("github.com"));
    QCOMPARE(url.path(), QString("/chetjones003/GoSurvey/issues/new"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue("title", QUrl::FullyDecoded),
             QString("GoSurvey report (") + GOSURVEY_VERSION_FULL + ")");
    QVERIFY(query.queryItemValue("body", QUrl::FullyDecoded).contains("Exception code: 0xC0000005"));
    QCOMPARE(message, QString::fromUtf8(failureReportUi::SendOutcomeMessage(clipboardWorks, true).c_str()));
    QVERIFY2(dialog->isVisible(), "Send This Report leaves the window open");

    // A browser that cannot be opened is reported, with the address to use by hand.
    browserWorks = false;
    CaptureNextMessage(message);
    QTest::mouseClick(send, Qt::LeftButton);
    QCOMPARE(message, QString::fromUtf8(failureReportUi::SendOutcomeMessage(clipboardWorks, false).c_str()));
    QVERIFY(message.contains("github.com/chetjones003/GoSurvey/issues/new"));

    // Close is the cancel: the window goes and nothing more is opened or sent.
    QTest::mouseClick(close, Qt::LeftButton);
    QTRY_VERIFY(!dialog->isVisible());
    QCOMPARE(dialog->result(), int(QDialog::Accepted));
    QCOMPARE(opened.size(), 2);
  }

  void failureReportLinkCutsLongReportsToFitAUrl() {
    const std::string longReport(20000, 'x');
    const QUrl url = QUrl::fromEncoded(QByteArray::fromStdString(
        failureReportUi::GitHubIssueUrlForReport(longReport)));
    const QString body = QUrlQuery(url).queryItemValue("body", QUrl::FullyDecoded);
    QVERIFY(body.size() < 5400);
    QVERIFY(body.contains("full report copied to clipboard"));
  }

  void crashBundleReadsBackAndIsDeleted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("gosurvey-crash-1-2.txt");
    {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("GOSURVEY_CRASH_META reason=access_violation channel=beta\n---\nGoSurvey stopped.\nline two");
    }
    crashReporter::CrashReport report;
    QVERIFY(crashReporter::ReadCrashBundle(path.toStdWString(), report));
    QCOMPARE(report.wireReason, std::string("access_violation"));
    QCOMPARE(report.channel, std::string("beta"));
    QCOMPARE(report.reportUtf8, std::string("GoSurvey stopped.\nline two"));
    QVERIFY(!QFileInfo::exists(path));

    const QString malformed = directory.filePath("gosurvey-crash-3-4.txt");
    {
      QFile file(malformed);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("not a crash bundle\n");
    }
    const QString malformedDump = directory.filePath("gosurvey-crash-3-4.dmp");
    {
      QFile file(malformedDump);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("MDMP");
    }
    crashReporter::CrashReport rejected;
    QVERIFY(!crashReporter::ReadCrashBundle(malformed.toStdWString(), rejected));
    QVERIFY2(!QFileInfo::exists(malformed), "a malformed bundle is not left in the temp folder");
    QVERIFY2(!QFileInfo::exists(malformedDump), "a malformed bundle's paired dump is also removed");
  }

  void helperProcessWritesAStackFocusedMinidump() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("crash-test.dmp");
    QVERIFY(crashReporter::WriteMinidumpWithHelper(path.toStdWString(), nullptr));
    QFile dump(path);
    QVERIFY(dump.open(QIODevice::ReadOnly));
    QCOMPARE(dump.read(4), QByteArray("MDMP", 4));
    QVERIFY(dump.size() <= 10 * 1024 * 1024);
    dump.close();
    QVERIFY(QFile::remove(path));
  }

  void startupGraphicsCheckPassesOnAWorkingDriver() {
    // REQ-425 §4: the launch check must not refuse a machine that can draw. (The failure branch
    // needs a broken driver and is checked by hand.)
    startupFailure::Stage stage{};
    std::string detail;
    QVERIFY2(ProbeStartupGraphics(stage, detail), detail.c_str());
    QVERIFY(detail.empty());
  }

  /// Screenshots of the selected designs (report window B, Settings card B) for review.
  void surveyDisplayControlsApplyLiveAndPersistWithoutGeometryEdits() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    SurveyPoint point; point.id = 904; point.easting = 12.345678; point.northing = 6.123456;
    point.elevation = 3.456789; point.description = "Control";
    point.labelStyle = SurveyPointLabelStyle::NumberNorthEastElev;
    st.surveyPoints.push_back(point); EnsureSurveyPointLabelMtext(st, 0, nullptr);
    const auto undoCount = CadActiveUndoStackSize(st);
    AppCommandState::ProjectSession session; session.uid = 904; session.readOnly = true;
    st.openProjects.push_back(std::move(session));
    st.drawingTabs[st.activeDrawingIdx].projectUid = 904;
    window.ShowSettings();
    auto* settings = window.findChild<QDialog*>("applicationSettings"); QVERIFY(settings);
    settings->findChild<QPushButton*>("surveyPointDisplaySettings")->click();
    auto* dialog = window.findChild<QDialog*>("surveyPointDisplay"); QVERIFY(dialog);
    auto field = [dialog](const char* name) { return dialog->findChild<QDoubleSpinBox*>(name); };
    QVERIFY(field("surveyCrossSpan")->isEnabled());
    field("surveyCrossSpan")->setValue(.25);
    dialog->findChild<QCheckBox*>("surveyShowId")->setChecked(false);
    dialog->findChild<QSpinBox*>("surveyPrecision")->setValue(4);
    field("surveyLabelHeight")->setValue(.15);
    const int index = FindSurveyLabelAnnIndex(st, st.surveyPoints[0]); QVERIFY(index >= 0);
    const double oldX = st.cadAnnotations[index].boxMinX;
    field("surveyLabelEast")->setValue(.8);
    field("surveyLabelNorth")->setValue(.6);
    field("surveyLeaderWidth")->setValue(12);
    QVERIFY(st.cadAnnotations[index].boxMinX != oldX);
    QVERIFY(st.cadAnnotations[index].text.find("12.3457") != std::string::npos);
    QCOMPARE(st.surveyPointCrossSpanPlottedInches, .25f);
    QCOMPARE(st.surveyPointLabelPlottedHeightInches, .15f);
    QCOMPARE(st.surveyLabelLeaderArrowPx, 12.f);
    QCOMPARE(st.surveyPoints[0].easting, point.easting);
    QCOMPARE(st.surveyPoints[0].northing, point.northing);
    QCOMPARE(st.surveyPoints[0].elevation, point.elevation);
    QCOMPARE(CadActiveUndoStackSize(st), undoCount);
    field("surveyCrossSpan")->setValue(-1); QCOMPARE(field("surveyCrossSpan")->value(), .02);
    field("surveyLabelHeight")->setValue(10); QCOMPARE(field("surveyLabelHeight")->value(), .5);
    field("surveyLeaderWidth")->setValue(100); QCOMPARE(field("surveyLeaderWidth")->value(), 30.);
    dialog->findChild<QPushButton*>("surveyDisplaySave")->click();
    QCOMPARE(dialog->findChild<QLabel*>("surveyDisplaySaveStatus")->text(), QString("Preferences saved."));
    AppCommandState loaded; LoadUserStartupPrefs(loaded);
    QCOMPARE(loaded.surveyPointCrossSpanPlottedInches, st.surveyPointCrossSpanPlottedInches);
    QCOMPARE(loaded.surveyPointShowIdInViewport, false);
    QCOMPARE(loaded.surveyPointDisplayPrecision, 4);
    QCOMPARE(loaded.surveyPointLabelPlottedHeightInches, .5f);
    QCOMPARE(loaded.surveyLabelOffsetEastPlottedIn, .8f);
    QCOMPARE(loaded.surveyLabelOffsetNorthPlottedIn, .6f);
    QCOMPARE(loaded.surveyLabelLeaderArrowPx, 30.f);
    QVERIFY(dialog->grab().save(QtTestArtifactDirectory() + "/qt-904-saved-read-only.png"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QCOMPARE(st.surveyPointShowIdInViewport, false);
    QCOMPARE(CadActiveUndoStackSize(st), undoCount);
    // The same modeless window edits the current drawing, never a captured point vector.
    st.surveyPoints.clear(); st.cadAnnotations.clear(); st.cadAnnotationAttrs.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.ShowSurveyPointDisplay();
    dialog = window.findChild<QDialog*>("surveyPointDisplay"); QVERIFY(dialog);
    dialog->findChild<QSpinBox*>("surveyPrecision")->setValue(2);
    QVERIFY(st.surveyPoints.empty()); QVERIFY(st.cadAnnotations.empty());
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close)->click();
    QCOMPARE(st.surveyPointDisplayPrecision, 2);
  }

  void surveyDisplaySaveFailureIsVisibleAndRetainsLiveChanges() {
    IsolatedUserData userData;
    QVERIFY(QDir().mkpath(QDir(userData.dir.path()).filePath("GoSurvey/gosurvey-user.json")));
    EditorWindow window; window.CreateDrawingFromTemplate(); window.ShowSurveyPointDisplay();
    auto* dialog = window.findChild<QDialog*>("surveyPointDisplay"); QVERIFY(dialog);
    dialog->findChild<QDoubleSpinBox*>("surveyCrossSpan")->setValue(.4);
    const int dialogWidth = dialog->width();
    dialog->findChild<QPushButton*>("surveyDisplaySave")->click();
    QCOMPARE(dialog->width(), dialogWidth);
    QVERIFY(dialog->findChild<QLabel*>("surveyDisplaySaveStatus")->text().contains("could not be saved"));
    auto* status = dialog->findChild<QLabel*>("surveyDisplaySaveStatus");
    QVERIFY(status->height() >= status->heightForWidth(status->width()));
    QVERIFY(dialog->isVisible()); QCOMPARE(window.State().surveyPointCrossSpanPlottedInches, .4f);
    QVERIFY(dialog->grab().save(QtTestArtifactDirectory() + "/qt-904-save-error.png"));
  }

  void surveyDisplayRenderingHonorsSizeIdsLayerAndLeader() {
    AppCommandState st;
    SurveyPoint point; point.id = 904; point.labelStyle = SurveyPointLabelStyle::None; point.layer = "Points";
    st.surveyPoints.push_back(point); st.surveyPointShowIdInViewport = false;
    st.modelUnitsPerPlottedInch = 20;
    auto render = [&] {
      QImage result(640, 480, QImage::Format_ARGB32_Premultiplied); result.fill(Qt::black);
      QPainter painter(&result); DrawQtModelAnnotations(painter, st, 640, 480);
      DrawQtSurveyPoints(painter, st, 640, 480); return result;
    };
    auto ink = [](const QImage& image) {
      int count = 0;
      for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y) != QColor(Qt::black)) ++count;
      return count;
    };
    st.surveyPointCrossSpanPlottedInches = .1f; const auto small = render(); QVERIFY(ink(small) > 0);
    st.surveyPointCrossSpanPlottedInches = .5f; const auto large = render(); QVERIFY(ink(large) > ink(small));
    st.modelUnitsPerPlottedInch *= 2;
    QVERIFY(ink(render()) > ink(large));
    st.modelUnitsPerPlottedInch /= 2;
    st.viewportElevationDeg = 15; QVERIFY(ink(render()) > 0);
    st.viewportElevationDeg = 90;
    st.surveyPointShowIdInViewport = true; const auto ids = render(); QVERIFY(ids != large);
    CadLayerRow layer; layer.name = "Points"; layer.on = false; st.drawingLayerTable.push_back(layer);
    QCOMPARE(ink(render()), 0);
    st.drawingLayerTable.back().on = true; st.drawingLayerTable.back().frozen = true;
    QCOMPARE(ink(render()), 0);
    st.drawingLayerTable.back().frozen = false;
    st.surveyPoints[0].labelStyle = SurveyPointLabelStyle::NumberOnly;
    EnsureSurveyPointLabelMtext(st, 0, nullptr);
    const int ai = FindSurveyLabelAnnIndex(st, st.surveyPoints[0]); QVERIFY(ai >= 0);
    auto& label = st.cadAnnotations[ai];
    label.surveyLabelHasUserOffset = true;
    label.boxMinX = 10; label.boxMaxX = 15; label.boxMinY = 4; label.boxMaxY = 8;
    st.surveyLabelLeaderArrowPx = 2; const auto thin = render();
    st.surveyLabelLeaderArrowPx = 20; QVERIFY(render() != thin);
  }

  void surveyDisplayPrecisionFormatsGridWithoutRoundingEditorValues() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate();
    SurveyPoint point; point.id = 904; point.northing = 12.3456789;
    window.State().surveyPoints.push_back(point);
    window.State().surveyPointDisplayPrecision = 2;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("surveyPointGrid"); QVERIFY(dialog);
      const auto closeDialog = qScopeGuard([dialog] { dialog->reject(); });
      auto* table = dialog->findChild<QTableWidget*>("pointGrid"); QVERIFY(table);
      const auto* delegate = qobject_cast<QStyledItemDelegate*>(table->itemDelegateForColumn(1)); QVERIFY(delegate);
      QCOMPARE(delegate->displayText(table->item(0, 1)->data(Qt::DisplayRole), QLocale()), QString("12.35"));
      QVERIFY(std::abs(table->item(0, 1)->data(Qt::EditRole).toDouble() - point.northing) < 1.e-10);
      dialog->reject();
    });
    window.ShowPoints(false);
    QCOMPARE(window.State().surveyPoints[0].northing, point.northing);
  }

  void surveyDisplayMarkerTiming() {
    AppCommandState st; st.surveyPointShowIdInViewport = false;
    st.modelUnitsPerPlottedInch = 50;
    st.surveyPointCrossSpanPlottedInches = 1.f / 30.f; // 8 px span, matching the legacy cross.
    for (int i = 0; i < 1000; ++i) {
      const int column = i % 40; const int row = i / 40;
      SurveyPoint point; point.easting = column - 20.; point.northing = row - 12.;
      st.surveyPoints.push_back(point);
    }
    const auto camera = CadViewCamera(st);
    QImage image(640, 480, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::black);
    QPainter painter(&image);
    QElapsedTimer timer; timer.start();
    for (int frame = 0; frame < 30; ++frame) {
      painter.setPen(QColor("#a7c9d9"));
      for (const auto& point : st.surveyPoints) {
        float x = 0, y = 0;
        camera.WorldToScreen(point.easting, point.northing, point.elevation, 640, 480, &x, &y);
        painter.drawLine(QPointF(x - 4, y - 4), QPointF(x + 4, y + 4));
        painter.drawLine(QPointF(x - 4, y + 4), QPointF(x + 4, y - 4));
      }
    }
    const double before = static_cast<double>(timer.nsecsElapsed()) / 30000000.;
    timer.restart();
    for (int frame = 0; frame < 30; ++frame) DrawQtSurveyPoints(painter, st, 640, 480);
    qInfo("1000 survey markers, raster ms/frame: legacy %.3f; port %.3f", before, static_cast<double>(timer.nsecsElapsed()) / 30000000.);
  }

  void surveyDisplayScreenshotOptions() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 800); window.show();
    auto& st = window.State();
    st.modelUnitsPerPlottedInch = 20;
    SurveyPoint point; point.id = 101; point.description = "Control monument";
    point.easting = -12; point.northing = 4; st.surveyPoints.push_back(point);
    point.id = 102; point.easting = 12; point.northing = -6; point.description = "Boundary corner";
    st.surveyPoints.push_back(point);
    for (std::size_t i = 0; i < st.surveyPoints.size(); ++i) EnsureSurveyPointLabelMtext(st, i, nullptr);
    window.Refresh(); window.ShowSurveyPointDisplay();
    auto* dialog = window.findChild<QDialog*>("surveyPointDisplay"); QVERIFY(dialog);
    QTest::qWait(200);
    const QString dir = QtTestArtifactDirectory();
    QVERIFY(dialog->grab().save(dir + "/qt-904-option-a.png"));
    // Alternative integrated layout of the same working controls, for user selection.
    auto* groups = dialog->findChild<QBoxLayout*>("surveyDisplayGroups"); QVERIFY(groups);
    groups->setDirection(QBoxLayout::LeftToRight);
    dialog->layout()->activate(); QTest::qWait(50);
    dialog->resize(850, 380); QTest::qWait(100);
    QVERIFY(dialog->grab().save(dir + "/qt-904-option-b.png"));
    QVERIFY(window.m_Viewport->grabFramebuffer().save(dir + "/qt-904-viewport.png"));
    const QImage normalFrame = window.m_Viewport->grabFramebuffer();
    QVERIFY(normalFrame.copy(normalFrame.width() / 3, normalFrame.height() / 3,
        normalFrame.width() * 2 / 3, normalFrame.height() / 3).save(dir + "/qt-904-viewport-detail.png"));
    const int label = FindSurveyLabelAnnIndex(st, st.surveyPoints[0]); QVERIFY(label >= 0);
    st.cadAnnotations[label].surveyLabelHasUserOffset = true;
    st.cadAnnotations[label].surveyLabelUserOffsetEast = 5;
    st.cadAnnotations[label].surveyLabelUserOffsetNorth = 15;
    RepositionSurveyLabelMtextForPoint(st, 0);
    window.Refresh(); QTest::qWait(100);
    QVERIFY(window.m_Viewport->grabFramebuffer().save(dir + "/qt-904-leader.png"));
    const QImage leaderFrame = window.m_Viewport->grabFramebuffer();
    QVERIFY(leaderFrame.copy(leaderFrame.width() / 3, leaderFrame.height() / 4,
        leaderFrame.width() * 2 / 3, leaderFrame.height() / 2).save(dir + "/qt-904-leader-detail.png"));
  }

  void failureReportAndUsageNoticeScreenshots() {
    const QString dir = QtTestArtifactDirectory();
    for (const bool light : {false, true}) {
      ApplyQtAppearance(*qApp, light);
      const QString appearance = light ? "light" : "dark";
      std::unique_ptr<QDialog> dialog(CreateFailureReportDialog("GoSurvey encountered a problem",
                                                                SampleCrashReport()));
      dialog->resize(dialog->minimumSize());
      dialog->show();
      QTest::qWait(50);
      QVERIFY(dialog->grab().save(dir + "/qt-1004-crash-" + appearance + ".png"));
      std::unique_ptr<QDialog> startup(CreateFailureReportDialog(
          "GoSurvey could not start",
          "GoSurvey could not initialize OpenGL 3.3 for drawing.\n\nTechnical detail:\n"
          "OpenGL shader setup failed (driver may not support OpenGL 3.3 core)."));
      startup->resize(startup->minimumSize());
      startup->show();
      QTest::qWait(50);
      QVERIFY(startup->grab().save(dir + "/qt-1004-startup-" + appearance + ".png"));
    }
    ApplyQtAppearance(*qApp, false);
    EditorWindow window;
    window.ShowSettings();
    auto* settings = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(settings);
    QTest::qWait(250);  // let the dialog polish and the account card refresh its status
    settings->layout()->activate();
    // As opened: tall enough for its wrapped text after Qt has applied the visible style metrics.
    QVERIFY(settings->height() >= settings->heightForWidth(settings->width()));
    QVERIFY(settings->grab().save(dir + "/qt-883-settings-final.png"));
  }

  // ---- REQ-077 / REQ-078 software update (issue #882) ----------------------------------------

  static QDialog* OfferUpdate(EditorWindow& window, const update::Manifest& manifest,
                              bool requiresPerUserMigration = false) {
    window.m_Update->runningVersion = "0.7.4";
    window.m_Update->requiresPerUserMigration = requiresPerUserMigration;
    window.m_Update->available = manifest;
    window.m_Update->phase = update::Phase::UpdateReady;
    window.ServiceUpdate();
    return window.findChild<QDialog*>("softwareUpdate");
  }

  void updateOfferShowsBothVersionsAndWaitsForAnExplicitChoice() {
    EditorWindow window;
    auto manifest = OfferedManifest();
    manifest.gsFormatVersion = kGsFormatVersion + 1;
    auto* dialog = OfferUpdate(window, manifest);
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
    QVERIFY(!(dialog->windowFlags() & Qt::WindowCloseButtonHint));
    QCOMPARE(dialog->findChild<QLabel*>("updateHeadline")->text(), QString("GoSurvey 99.0.0 is available."));
    QCOMPARE(dialog->findChild<QLabel*>("updateDetail")->text(), QString("You are running 0.7.4."));
    for (const char* name : {"updateNowButton", "updateRemindButton", "updateSkipButton"})
      QVERIFY(UpdateButton(dialog, name)->isVisibleTo(dialog));
    for (const char* name : {"updateCancelButton", "updateInstallButton", "updateNotNowButton", "updateCloseButton"})
      QVERIFY(!UpdateButton(dialog, name)->isVisibleTo(dialog));
    // No window-close or Escape path may stand in for a decision; nothing starts without a click.
    dialog->close();
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(dialog->isVisible());
    QVERIFY(!window.m_Update->task);
    QCOMPARE(window.m_Update->phase, update::Phase::UpdateReady);
    // Notes are rendered, and the compatibility notice sits ABOVE them.
    auto* notes = dialog->findChild<QTextBrowser*>("updateNotes");
    auto* compatibility = dialog->findChild<QLabel*>("updateCompatibility");
    auto* migration = dialog->findChild<QLabel*>("updateMigrationNotice");
    QVERIFY(notes->isVisibleTo(dialog));
    QVERIFY(notes->toPlainText().contains("Faster point import"));
    QVERIFY(compatibility->isVisibleTo(dialog));
    QVERIFY(!migration->isVisibleTo(dialog));
    QVERIFY(compatibility->mapTo(dialog, QPoint()).y() < notes->mapTo(dialog, QPoint()).y());
    UpdateButton(dialog, "updateRemindButton")->click();
  }

  void updateOfferExplainsProgramFilesMigrationBeforeReleaseNotes() {
    EditorWindow legacyWindow;
    auto* legacyDialog = OfferUpdate(legacyWindow, OfferedManifest(), true);
    QVERIFY(legacyDialog);
    auto* legacyNotice = legacyDialog->findChild<QLabel*>("updateMigrationNotice");
    auto* legacyNotes = legacyDialog->findChild<QTextBrowser*>("updateNotes");
    QVERIFY(legacyNotice->isVisibleTo(legacyDialog));
    QVERIFY(legacyNotice->text().contains("%LOCALAPPDATA%\\Programs\\GoSurvey"));
    QVERIFY(legacyNotice->text().contains("ask once"));
    QVERIFY(legacyNotice->text().contains("settings and user data"));
    QVERIFY(legacyNotice->mapTo(legacyDialog, QPoint()).y() <
            legacyNotes->mapTo(legacyDialog, QPoint()).y());
    for (const char* name : {"updateNowButton", "updateRemindButton", "updateSkipButton"})
      QVERIFY(UpdateButton(legacyDialog, name)->isVisibleTo(legacyDialog));
    QTest::qWait(100);
    QVERIFY(legacyDialog->grab().save(Issue914Shot("qt-1106-update-migration-legacy.png")));
    UpdateButton(legacyDialog, "updateRemindButton")->click();
    QTRY_VERIFY(!legacyWindow.findChild<QDialog*>("softwareUpdate"));

    EditorWindow perUserWindow;
    auto* perUserDialog = OfferUpdate(perUserWindow, OfferedManifest(), false);
    QVERIFY(perUserDialog);
    auto* perUserNotice = perUserDialog->findChild<QLabel*>("updateMigrationNotice");
    QVERIFY(!perUserNotice->isVisibleTo(perUserDialog));
    QTest::qWait(100);
    QVERIFY(perUserDialog->grab().save(Issue914Shot("qt-1106-update-migration-per-user.png")));
    UpdateButton(perUserDialog, "updateRemindButton")->click();
    QTRY_VERIFY(!perUserWindow.findChild<QDialog*>("softwareUpdate"));
  }

  void updateCompatibilityNoticesAreDistinguishedAndAbsentWhenUndeclared() {
    EditorWindow window;
    struct Case { int format; bool breaking; bool shown; const char* text; };
    const Case cases[] = {
        {kGsFormatVersion, false, false, ""},                       // same format: nothing
        {0, false, false, ""},                                      // predates the field: nothing
        {kGsFormatVersion + 1, false, true, "changes the drawing file format"},
        {kGsFormatVersion, true, true, "cannot open your existing drawings"},
    };
    for (const auto& c : cases) {
      auto manifest = OfferedManifest();
      manifest.gsFormatVersion = c.format;
      manifest.breaksExistingDrawings = c.breaking;
      manifest.compatibilityWarning = "Old DWG layouts are dropped.";
      auto* dialog = OfferUpdate(window, manifest);
      QVERIFY(dialog);
      auto* label = dialog->findChild<QLabel*>("updateCompatibility");
      QCOMPARE(label->isVisibleTo(dialog), c.shown);
      if (c.shown) QVERIFY2(label->text().contains(c.text), qPrintable(label->text()));
      if (c.breaking) QVERIFY(label->text().contains("Old DWG layouts are dropped."));
      UpdateButton(dialog, "updateRemindButton")->click();
      QTRY_VERIFY(!window.findChild<QDialog*>("softwareUpdate"));
    }
  }

  void updateRemindLaterWritesNothingAndSkipSuppressesOnlyThatVersion() {
    IsolatedUserData userData;
    EditorWindow window;
    auto* dialog = OfferUpdate(window, OfferedManifest("99.0.0"));
    UpdateButton(dialog, "updateRemindButton")->click();
    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
    QVERIFY(window.State().updatePrefs.skippedVersion.empty());
    QTRY_VERIFY(!window.findChild<QDialog*>("softwareUpdate"));

    dialog = OfferUpdate(window, OfferedManifest("99.0.0"));
    UpdateButton(dialog, "updateSkipButton")->click();
    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
    QCOMPARE(window.State().updatePrefs.skippedVersion, std::string("99.0.0"));
    QTRY_VERIFY(!window.findChild<QDialog*>("softwareUpdate"));
    AppCommandState reloaded;
    LoadUserStartupPrefSettings(reloaded);
    QCOMPARE(reloaded.updatePrefs.skippedVersion, std::string("99.0.0"));
    std::string reason;
    const std::string skipped = reloaded.updatePrefs.skippedVersion;
    QCOMPARE(update::DecideUpdate("0.7.4", OfferedManifest("99.0.0"), skipped, reason), update::Decision::NoUpdate);
    QCOMPARE(update::DecideUpdate("0.7.4", OfferedManifest("99.0.1"), skipped, reason), update::Decision::Offer);
  }

  void updateDownloadFailureIsReportedAndClosesToNormalWork() {
    EditorWindow window;
    auto* dialog = OfferUpdate(window, OfferedManifest());
    UpdateButton(dialog, "updateNowButton")->click();
    QVERIFY(window.m_Update->phase == update::Phase::Downloading ||
            window.m_Update->phase == update::Phase::Failed);
    // The unreachable installer URL fails; the reason is stated, not swallowed (REQ-201).
    QTRY_COMPARE_WITH_TIMEOUT(window.m_Update->phase, update::Phase::Failed, 20000);
    QTRY_COMPARE(dialog->findChild<QLabel*>("updateHeadline")->text(), QString("The update could not be completed."));
    QTRY_VERIFY(dialog->findChild<QLabel*>("updateError")->isVisibleTo(dialog));
    QVERIFY(!dialog->findChild<QLabel*>("updateError")->text().isEmpty());
    QVERIFY(UpdateButton(dialog, "updateCloseButton")->isVisibleTo(dialog));
    QVERIFY(!UpdateButton(dialog, "updateNowButton")->isVisibleTo(dialog));
    UpdateButton(dialog, "updateCloseButton")->click();
    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
    QTRY_VERIFY(!window.findChild<QDialog*>("softwareUpdate"));
  }

  void updateDownloadShowsProgressAndCancelReturnsToTheOffer() {
    EditorWindow window;
    auto* dialog = OfferUpdate(window, OfferedManifest());
    // A synthetic in-flight download, so progress and cancellation are deterministic.
    window.m_Update->phase = update::Phase::Downloading;
    window.m_Update->task = std::make_unique<update::UpdateTask>();
    window.m_Update->task->bytesReceived = 12LL * 1024 * 1024;
    window.m_Update->task->bytesTotal = 24LL * 1024 * 1024;
    auto* progress = dialog->findChild<QProgressBar*>("updateProgress");
    QTRY_VERIFY(progress->isVisibleTo(dialog));
    QTRY_COMPARE(progress->format(), QString("12.0 MB of 24.0 MB"));
    QCOMPARE(progress->value(), 500);
    QVERIFY(UpdateButton(dialog, "updateCancelButton")->isVisibleTo(dialog));
    UpdateButton(dialog, "updateCancelButton")->click();
    QVERIFY(window.m_Update->task->cancel.load());
    window.m_Update->task->error = "cancelled";
    window.m_Update->task->done.store(true, std::memory_order_release);
    window.ServiceUpdate();
    QCOMPARE(window.m_Update->phase, update::Phase::UpdateReady);
    QTRY_VERIFY(UpdateButton(dialog, "updateNowButton")->isVisibleTo(dialog));
    UpdateButton(dialog, "updateRemindButton")->click();
  }

  void updateInstallRunsThroughUnsavedChangesAndCancelCancelsTheUpdate() {
    EditorWindow window;
    window.show();
    int launches = 0;
    window.m_LaunchInstaller = [&launches](update::UpdateState&) { ++launches; return true; };
    window.CreateDrawingFromTemplate();
    window.Submit("LINE"); window.Submit("0,0"); window.Submit("10,0");
    CancelActiveCommand(window.State(), window.m_Log);
    QVERIFY(window.State().cadGpuRevision != window.State().activeDocSavedRevision);

    window.m_Update->available = OfferedManifest();
    window.m_Update->available.installerUrl = "C:/verified/GoSurvey-99.0.0-Installer.exe";
    window.m_Update->phase = update::Phase::ReadyToLaunch;
    window.ServiceUpdate();
    auto* dialog = window.findChild<QDialog*>("softwareUpdate");
    QVERIFY(dialog);
    QTRY_VERIFY(UpdateButton(dialog, "updateInstallButton")->isVisibleTo(dialog));
    QVERIFY(UpdateButton(dialog, "updateNotNowButton")->isVisibleTo(dialog));

    // Cancelling the save prompt cancels the update: nothing launches, the app stays open, and
    // the verified installer is offered again.
    UpdateButton(dialog, "updateInstallButton")->click();
    QVERIFY(window.m_Update->awaitingUnsavedCheck);
    AnswerNextDialog(QMessageBox::Cancel);
    window.ServiceUpdate();
    QCOMPARE(launches, 0);
    QVERIFY(window.isVisible());
    QVERIFY(!window.m_UpdateExitPending);
    QCOMPARE(window.m_Update->phase, update::Phase::ReadyToLaunch);

    // Discarding settles the prompt, and only then does the installer start.
    QTRY_VERIFY(UpdateButton(dialog, "updateInstallButton")->isEnabled());
    UpdateButton(dialog, "updateInstallButton")->click();
    AnswerNextDialog(QMessageBox::Discard);
    window.ServiceUpdate();
    QCOMPARE(launches, 1);
    QVERIFY(!window.isVisible());
  }

  void updateInstallerThatCannotStartKeepsTheApplicationOpenAndSaysWhy() {
    EditorWindow window;
    window.show();
    window.m_LaunchInstaller = [](update::UpdateState& state) {
      state.lastError = "could not start the installer (error 5)";
      state.phase = update::Phase::Failed;
      return false;
    };
    window.m_Update->available = OfferedManifest();
    window.m_Update->phase = update::Phase::ReadyToLaunch;
    window.ServiceUpdate();
    auto* dialog = window.findChild<QDialog*>("softwareUpdate");
    QVERIFY(dialog);
    UpdateButton(dialog, "updateInstallButton")->click();
    window.ServiceUpdate();
    QVERIFY(window.isVisible());
    QVERIFY(!window.m_UpdateExitPending);
    QCOMPARE(window.m_Update->phase, update::Phase::Failed);
    QTRY_COMPARE(dialog->findChild<QLabel*>("updateError")->text(), QString("could not start the installer (error 5)"));
    UpdateButton(dialog, "updateCloseButton")->click();
    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
  }

  void updateDeclinedElevationOffersInstallAgainNotAnotherDownload() {
    EditorWindow window;
    window.show();
    window.m_LaunchInstaller = [](update::UpdateState& state) {
      state.phase = update::Phase::UpdateReady;   // what LaunchInstallerAndExit reports for UAC "No"
      return false;
    };
    window.m_Update->available = OfferedManifest();
    window.m_Update->available.installerUrl = "C:/verified/GoSurvey-99.0.0-Installer.exe";
    window.m_Update->phase = update::Phase::ReadyToLaunch;
    window.ServiceUpdate();
    auto* dialog = window.findChild<QDialog*>("softwareUpdate");
    QVERIFY(dialog);
    UpdateButton(dialog, "updateInstallButton")->click();
    window.ServiceUpdate();
    QVERIFY(window.isVisible());
    QCOMPARE(window.m_Update->phase, update::Phase::ReadyToLaunch);
    QCOMPARE(window.m_Update->available.installerUrl, std::string("C:/verified/GoSurvey-99.0.0-Installer.exe"));
    QTRY_VERIFY(UpdateButton(dialog, "updateInstallButton")->isVisibleTo(dialog));
    QVERIFY(!UpdateButton(dialog, "updateNowButton")->isVisibleTo(dialog));
    UpdateButton(dialog, "updateNotNowButton")->click();
  }

  void updateCheckGatesTheSessionOnlyWhileTheNetworkAnswerIsPending() {
    EditorWindow window;
    QVERIFY(!window.UpdateCheckRunning());
    window.m_Update->phase = update::Phase::Checking;
    window.m_Update->task = std::make_unique<update::UpdateTask>();
    QVERIFY(window.UpdateCheckRunning());
    window.m_Update->task->done.store(true, std::memory_order_release);
    QVERIFY(!window.UpdateCheckRunning());
    window.ServiceUpdate();   // consumes the finished (not newer) check
    QVERIFY(!window.UpdateBlocksLaunch());
  }

  void updateNotNowKeepsTheInstallerForNextLaunch() {
    EditorWindow window;
    window.m_Update->available = OfferedManifest();
    window.m_Update->phase = update::Phase::ReadyToLaunch;
    window.ServiceUpdate();
    auto* dialog = window.findChild<QDialog*>("softwareUpdate");
    QVERIFY(dialog);
    UpdateButton(dialog, "updateNotNowButton")->click();
    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
    QTRY_VERIFY(!window.findChild<QDialog*>("softwareUpdate"));
  }

  void updateCheckIsSilentWhenCurrentFailedOrDisabled() {
    EditorWindow window;
    // Disabled: no request is made at all.
    window.State().updatePrefs.enabled = false;
    window.StartUpdateCheck();
    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
    QVERIFY(!window.m_Update->task);
    QVERIFY(!window.UpdateBlocksLaunch());
    // A finished check that found nothing newer, or failed, shows nothing and never blocks work.
    for (const char* reason : {"0.7.4 is the latest", "HTTP 404", "malformed manifest"}) {
      window.m_Update->phase = update::Phase::Checking;
      window.m_Update->task = std::make_unique<update::UpdateTask>();
      QVERIFY(window.UpdateBlocksLaunch());
      window.m_Update->task->ok = false;
      window.m_Update->task->error = reason;
      window.m_Update->task->done.store(true, std::memory_order_release);
      window.ServiceUpdate();
      QCOMPARE(window.m_Update->phase, update::Phase::Idle);
      QVERIFY(!window.findChild<QDialog*>("softwareUpdate"));
      QVERIFY(!window.UpdateBlocksLaunch());
    }
    // A newer build found by the check becomes the offer dialog.
    window.m_Update->phase = update::Phase::Checking;
    window.m_Update->task = std::make_unique<update::UpdateTask>();
    window.m_Update->task->ok = true;
    window.m_Update->task->manifest = OfferedManifest();
    window.m_Update->task->done.store(true, std::memory_order_release);
    window.ServiceUpdate();
    QCOMPARE(window.m_Update->phase, update::Phase::UpdateReady);
    auto* dialog = window.findChild<QDialog*>("softwareUpdate");
    QVERIFY(dialog);
    QVERIFY(window.UpdateBlocksLaunch());
    UpdateButton(dialog, "updateRemindButton")->click();
  }

  void startupHasNoSignInGate() {
    EditorWindow window;
    window.show();
    QApplication::processEvents();
    QVERIFY(!window.findChild<QDialog*>("signInGate"));
    QVERIFY(window.grab().save(QtTestArtifactDirectory() + "/task-1043-startup.png"));
  }
  void settingsUpdatesAreStagedUntilApply() {
    IsolatedUserData userData;
    EditorWindow window;
    window.State().updatePrefs.skippedVersion = "99.0.0";
    auto* viewMenu = window.findChild<QMenu*>("viewMenu");
    QVERIFY(viewMenu);
    QAction* settings = nullptr;
    for (auto* action : viewMenu->actions()) if (action->text() == "Settings…") settings = action;
    QVERIFY(settings);
    settings->trigger();
    auto* dialog = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(dialog);
    auto* check = dialog->findChild<QCheckBox*>("updateCheckEnabled");
    auto* beta = dialog->findChild<QCheckBox*>("updateUseBeta");
    auto* skipped = dialog->findChild<QLabel*>("updateSkippedLabel");
    auto* clear = dialog->findChild<QPushButton*>("updateClearSkip");
    auto* apply = dialog->findChild<QPushButton*>("settingsApply");
    QVERIFY(check && beta && skipped && clear && apply);
    QVERIFY(dialog->findChild<QLabel*>("updateVersionLabel")->text().startsWith("Version "));
    QVERIFY(check->isChecked());
    QVERIFY(!beta->isChecked());
    QCOMPARE(skipped->text(), QString("Skipping 99.0.0"));

    // Edits are staged: nothing reaches the preferences until Apply.
    beta->setChecked(true);
    check->setChecked(false);
    clear->click();
    QVERIFY(!beta->isEnabled());   // the channel is moot while checking is off
    QVERIFY(window.State().updatePrefs.enabled);
    QVERIFY(!window.State().updatePrefs.useBetaChannel);
    QCOMPARE(window.State().updatePrefs.skippedVersion, std::string("99.0.0"));
    AppCommandState untouched;
    LoadUserStartupPrefSettings(untouched);
    QVERIFY(untouched.updatePrefs.enabled);

    // Apply saves and keeps the window open.
    apply->click();
    QVERIFY(window.findChild<QDialog*>("applicationSettings"));
    QVERIFY(!window.State().updatePrefs.enabled);
    QVERIFY(window.State().updatePrefs.useBetaChannel);
    QVERIFY(window.State().updatePrefs.skippedVersion.empty());
    AppCommandState reloaded;
    LoadUserStartupPrefSettings(reloaded);
    QVERIFY(!reloaded.updatePrefs.enabled);
    QVERIFY(reloaded.updatePrefs.useBetaChannel);
    QVERIFY(reloaded.updatePrefs.skippedVersion.empty());

    // A preferences file that cannot be written is reported, and the window stays open.
    QTemporaryFile blocker;
    QVERIFY(blocker.open());
    IsolatedUserData unwritable(blocker.fileName());   // APPDATA is a file, so the folder cannot exist
    check->setChecked(true);
    dialog->findChild<QPushButton*>("settingsOk")->click();
    QVERIFY(!dialog->findChild<QLabel*>("settingsSaveStatus")->text().isEmpty());
    QVERIFY(window.findChild<QDialog*>("applicationSettings"));
  }

  // REQ-449 / issue #1032.
  void issue1032SettingsWindowStagesAndPersists() {
    IsolatedUserData userData;
    EditorWindow window;
    window.ShowSettings();
    auto* dialog = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(dialog);
    auto* categories = dialog->findChild<QListWidget*>("settingsCategories");
    QVERIFY(categories);
    QVERIFY(!dialog->findChild<QLabel*>("crashDiagnosticsDisclosure"));
    QStringList names;
    for (int i = 0; i < categories->count(); ++i) names << categories->item(i)->text();
    QCOMPARE(names, (QStringList{"Updates", "Display", "Drafting", "Survey Points"}));
    QVERIFY(!dialog->findChild<QWidget*>("settingsPageAccount"));
    QVERIFY(!dialog->findChild<QWidget*>("usageDataSection"));
    auto* wheel = dialog->findChild<QDoubleSpinBox*>("settingsWheelZoomFactor");
    auto* undo = dialog->findChild<QSpinBox*>("settingsUndoHistory");
    auto* angle = dialog->findChild<QComboBox*>("settingsAngleType");
    auto* trim = dialog->findChild<QComboBox*>("settingsTrimState");
    auto* fillet = dialog->findChild<QDoubleSpinBox*>("settingsFilletRadius");
    QVERIFY(wheel && undo && angle && trim && fillet);
    QVERIFY(dialog->findChild<QSpinBox*>("settingsArcSmoothness"));
    QVERIFY(dialog->findChild<QSpinBox*>("settingsLinearPrecision"));
    QVERIFY(dialog->findChild<QDoubleSpinBox*>("settingsSnapAperture"));
    QVERIFY(dialog->findChild<QPushButton*>("settingsPolarTracking"));
    QVERIFY(dialog->findChild<QPushButton*>("surveyPointDisplaySettings"));
    // Screenshot evidence for the visualizer (written under build/artifacts, never committed).
    {
      const QString artifacts = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/";
      auto* stack = dialog->findChild<QStackedWidget*>("settingsPages");
      ApplyQtAppearance(*qApp, false);  // the app's real Graphite theme, as a user sees it
      dialog->resize(780, 600);
      for (int i = 0; i < categories->count(); ++i) {
        categories->setCurrentRow(i);
        QCoreApplication::processEvents();
        QVERIFY(dialog->grab().save(artifacts + QString("qt-1032-settings-%1.png").arg(i)));
      }
      QVERIFY(stack);
      categories->setCurrentRow(0);
      QVERIFY(dialog->grab().save(artifacts + "qt-1102-settings-without-crash-disclosure.png"));
    }
    const float wheelBefore = window.State().displayWheelZoomFactor;
    wheel->setValue(1.5);
    undo->setValue(12);
    angle->setCurrentIndex(2);
    trim->setCurrentIndex(1);
    fillet->setValue(2.25);

    // Staged: nothing changed yet.
    QCOMPARE(window.State().displayWheelZoomFactor, wheelBefore);
    // Apply persists and keeps the window open.
    dialog->findChild<QPushButton*>("settingsApply")->click();
    QVERIFY(window.findChild<QDialog*>("applicationSettings"));
    QCOMPARE(window.State().displayWheelZoomFactor, 1.5f);
    AppCommandState reloaded;
    LoadUserStartupPrefSettings(reloaded);
    QCOMPARE(reloaded.displayWheelZoomFactor, 1.5f);
    QCOMPARE(reloaded.undoHistoryMaxSize, 12);
    QCOMPARE(reloaded.angleDisplayType, 2);
    QCOMPARE(reloaded.trimState, 1);
    QCOMPARE(reloaded.filletRadius, 2.25f);

    // Cancel discards unapplied edits.
    wheel->setValue(2.0);
    dialog->findChild<QPushButton*>("settingsCancel")->click();
    QCOMPARE(window.State().displayWheelZoomFactor, 1.5f);
    AppCommandState afterCancel;
    LoadUserStartupPrefSettings(afterCancel);
    QCOMPARE(afterCancel.displayWheelZoomFactor, 1.5f);

    // OPTIONS opens the same window, and OK saves and closes.
    window.ShowSettings();
    dialog = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(dialog);
    dialog->findChild<QDoubleSpinBox*>("settingsWheelZoomFactor")->setValue(1.25);
    dialog->findChild<QPushButton*>("settingsOk")->click();
    QCOMPARE(window.State().displayWheelZoomFactor, 1.25f);
    AppCommandState afterOk;
    LoadUserStartupPrefSettings(afterOk);
    QCOMPARE(afterOk.displayWheelZoomFactor, 1.25f);
  }

  // REQ-450 / issue #1033: cursor controls preview live, Cancel restores, Apply/OK keep and persist.
  void issue1033CursorSettingsPreviewAndCancel() {
    IsolatedUserData userData;
    EditorWindow window;
    AppCommandState& state = window.State();
    // Defaults from the requirement.
    QCOMPARE(state.displayCrosshairSizePct, 5);
    QCOMPARE(state.viewportCrosshairPickHalfPxX, 4.f);
    QCOMPARE(state.objectSnapAperturePx, 14.f);
    window.ShowSettings();
    auto* dialog = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(dialog);
    auto* arm = dialog->findChild<QSlider*>("settingsCrosshairSize");
    auto* pickbox = dialog->findChild<QSpinBox*>("settingsPickboxSize");
    auto* aperture = dialog->findChild<QDoubleSpinBox*>("settingsSnapAperture");
    auto* colorButton = dialog->findChild<QPushButton*>("settingsCrosshairColor");
    QVERIFY(colorButton);
    QCOMPARE(colorButton->text(), QString("#FFA800"));   // default: amber (255, 168, 0)
    QVERIFY(arm && pickbox && aperture);
    QCOMPARE(arm->minimum(), 1); QCOMPARE(arm->maximum(), 100);
    QCOMPARE(pickbox->minimum(), 2); QCOMPARE(pickbox->maximum(), 20);
    QCOMPARE(aperture->minimum(), 4.0); QCOMPARE(aperture->maximum(), 64.0);

    // Live preview: the viewport state changes before Apply.
    arm->setValue(30);
    pickbox->setValue(9);
    QCOMPARE(state.displayCrosshairSizePct, 30);
    QCOMPARE(state.viewportCrosshairPickHalfPxX, 9.f);
    QCOMPARE(state.viewportCrosshairPickHalfPxY, 9.f);
    // Cancel restores the values the window opened with, and saves nothing.
    dialog->findChild<QPushButton*>("settingsCancel")->click();
    QCOMPARE(state.displayCrosshairSizePct, 5);
    QCOMPARE(state.viewportCrosshairPickHalfPxX, 4.f);
    AppCommandState afterCancel;
    LoadUserStartupPrefSettings(afterCancel);
    QCOMPARE(afterCancel.displayCrosshairSizePct, 5);

    // Apply keeps the preview and persists; a later Cancel only undoes edits made after the Apply.
    window.ShowSettings();
    dialog = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(dialog);
    dialog->findChild<QSlider*>("settingsCrosshairSize")->setValue(40);
    dialog->findChild<QSpinBox*>("settingsPickboxSize")->setValue(7);
    dialog->findChild<QDoubleSpinBox*>("settingsSnapAperture")->setValue(20);
    QCOMPARE(state.objectSnapAperturePx, 14.f);   // the aperture is staged until Apply
    dialog->findChild<QPushButton*>("settingsApply")->click();
    AppCommandState applied;
    LoadUserStartupPrefSettings(applied);
    QCOMPARE(applied.displayCrosshairSizePct, 40);
    QCOMPARE(applied.viewportCrosshairPickHalfPxX, 7.f);
    QCOMPARE(applied.objectSnapAperturePx, 20.f);
    dialog->findChild<QSlider*>("settingsCrosshairSize")->setValue(60);
    dialog->findChild<QPushButton*>("settingsCancel")->click();
    QCOMPARE(state.displayCrosshairSizePct, 40);
    QCOMPARE(state.viewportCrosshairPickHalfPxX, 7.f);

    // A new drawing from a template keeps the user's cursor, not the template's copy of it.
    state.viewportCrosshairR = 0.1f; state.viewportCrosshairG = 0.2f; state.viewportCrosshairB = 0.3f;
    state.objectSnapAperturePx = 33.f;
    window.CreateDrawingFromTemplate(0, QString::fromUtf8(GOSURVEY_SOURCE_DIR) + "/resources/default-template.gst");
    QCOMPARE(state.viewportCrosshairR, 0.1f);
    QCOMPARE(state.viewportCrosshairG, 0.2f);
    QCOMPARE(state.viewportCrosshairB, 0.3f);
    QCOMPARE(state.viewportCrosshairPickHalfPxX, 7.f);
    QCOMPARE(state.objectSnapAperturePx, 33.f);
  }

  // REQ-450: the viewport paints the configured crosshair + pickbox, and only the pickbox while selecting.
  void issue1033ViewportDrawsCrosshairAndPickbox() {
    IsolatedUserData userData;
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    AppCommandState& state = window.State();
    state.viewportCrosshairR = 1.f; state.viewportCrosshairG = 0.f; state.viewportCrosshairB = 1.f;
    state.displayCrosshairSizePct = 20;
    state.viewportCrosshairPickHalfPxX = state.viewportCrosshairPickHalfPxY = 6.f;
    window.resize(1280, 820); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(250);
    EditorViewport* viewport = window.m_Viewport;
    QVERIFY(viewport->IsRendererReady());
    const QPoint p(viewport->width() / 2, viewport->height() / 2);
    QEnterEvent enter(QPointF(p), QPointF(p), QPointF(viewport->mapToGlobal(p)));
    QApplication::sendEvent(viewport, &enter);
    const int arm = static_cast<int>(0.2f * static_cast<float>(std::min(viewport->width(), viewport->height())) / 2.f);
    QVERIFY(viewport->grabFramebuffer().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-1033-cursor-normal.png"));
    // The grab is in device pixels (the display may scale), and a small neighbourhood absorbs how a
    // one-pixel line on a half-pixel position is rasterised.
    auto drawn = [&](int dx, int dy) {
      const QImage frame = viewport->grabFramebuffer();
      const double scale = frame.devicePixelRatio();
      for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
          if (const QColor c = frame.pixelColor(static_cast<int>((p.x() + dx) * scale) + x, static_cast<int>((p.y() + dy) * scale) + y); c.red() > 180 && c.green() < 60 && c.blue() > 180)
            return true;  // the grab is not fully opaque, so match "clearly magenta" rather than exact bytes
      return false;
    };
    // Normal use: arm tips and the pickbox edge are drawn in the configured colour.
    QVERIFY(drawn(arm - 3, 0));
    QVERIFY(drawn(0, arm - 3));
    QVERIFY(drawn(-6, 2));
    // Selection step: pickbox only.
    state.active = AppCommandState::Kind::Move;
    state.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
    viewport->update();
    QVERIFY(!drawn(arm - 3, 0));
    QVERIFY(!drawn(0, arm - 3));
    QVERIFY(drawn(-6, 2));
    // The step ends: the arms return.
    state.active = AppCommandState::Kind::None;
    viewport->update();
    QVERIFY(drawn(arm - 3, 0));
  }

  void issue1033StoredCursorValuesAreClamped() {
    IsolatedUserData userData;
    AppCommandState state;
    state.displayCrosshairSizePct = 500;
    state.viewportCrosshairPickHalfPxX = 99.f;
    state.viewportCrosshairPickHalfPxY = 0.f;
    state.objectSnapAperturePx = 1000.f;
    state.viewportCrosshairR = 0.25f; state.viewportCrosshairG = 0.5f; state.viewportCrosshairB = 0.75f;
    QVERIFY(SaveUserStartupPrefs(state));
    AppCommandState loaded;
    LoadUserStartupPrefSettings(loaded);
    QCOMPARE(loaded.displayCrosshairSizePct, 100);
    QCOMPARE(loaded.viewportCrosshairPickHalfPxX, 20.f);
    QCOMPARE(loaded.viewportCrosshairPickHalfPxY, 2.f);
    QCOMPARE(loaded.objectSnapAperturePx, 64.f);
    QCOMPARE(loaded.viewportCrosshairR, 0.25f);
    QCOMPARE(loaded.viewportCrosshairB, 0.75f);
  }

  void issue1032WheelZoomUsesPreference() {
    EditorWindow window;
    window.State().displayWheelZoomFactor = 2.0f;
    window.State().viewportZoom = 1.0f;
    window.m_Viewport->show();
    QWheelEvent wheel(QPointF(40, 40), window.m_Viewport->mapToGlobal(QPointF(40, 40)), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(window.m_Viewport, &wheel);
    QCOMPARE(window.State().viewportZoom, 2.0f);
  }

  void qtStartScreenHasRecentDrawingSurfaceAndVersionGatedWhatsNew() {
    EditorWindow window;
    auto* tiles = window.findChild<QWidget*>("recentDrawingTiles");
    QVERIFY(tiles);
    QVERIFY(tiles->layout());
    auto* projectTiles = window.findChild<QWidget*>("recentProjectTiles");
    QVERIFY(projectTiles);
    QVERIFY(projectTiles->layout());
    QVERIFY(!window.findChild<QScrollArea*>("startScreenScrollArea"));
    QVERIFY(window.findChild<QPushButton*>("startNewProject"));
    QVERIFY(window.findChild<QPushButton*>("startOpenProject"));
    auto* gridMode = window.findChild<QToolButton*>("recentGridMode");
    auto* listMode = window.findChild<QToolButton*>("recentListMode");
    QVERIFY(gridMode);
    QVERIFY(listMode);
    QVERIFY(listMode->isChecked());
    listMode->click();
    QVERIFY(listMode->isChecked());
    QVERIFY(!gridMode->isChecked());
    gridMode->click();
    QVERIFY(gridMode->isChecked());
    QCOMPARE(window.findChild<QLabel*>("startWelcomeTitle")->text(), QString("Welcome to GoSurvey"));
    QVERIFY(window.findChild<QPushButton*>("startNewDrawing"));
    QVERIFY(window.findChild<QPushButton*>("startOpenDrawing"));
    window.State().whatsNewDismissedVersion = GOSURVEY_VERSION_FULL;
    window.InitializeStartup();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));

    EditorWindow firstRun;
    // What's New follows the Start-screen conditions without a sign-in gate.
    firstRun.State().whatsNewDismissedVersion.clear();
    firstRun.InitializeStartup();
    auto* whatsNew = firstRun.findChild<QDialog*>("whatsNewDialog");
    QVERIFY(whatsNew);
    firstRun.show();
    QApplication::processEvents();
    QVERIFY(whatsNew->isVisible());
    QDir().mkpath(QtTestArtifactDirectory());
    QVERIFY(whatsNew->grab().save(
        QDir(QtTestArtifactDirectory()).filePath("issue1029-whats-new-startup.png")));
    QVERIFY(whatsNew->findChild<QLabel*>("whatsNewHeading"));
    QVERIFY(whatsNew->findChild<QLabel*>("whatsNewVersion"));
    auto* notes = whatsNew->findChild<QTextBrowser*>("whatsNewReleaseNotes");
    QVERIFY(notes);
    const QStringList imageNames = {"projects.png", "maps.png", "pdf-viewer.png",
                                    "point-clouds.png", "piping.png", "surfaces-3d.png"};
    for (const auto& name : imageNames) {
      QVERIFY(QFile::exists(QtAsset("whats-new/" + name)));
      const QVariant resource = notes->document()->resource(
          QTextDocument::ImageResource, QUrl("whats-new:" + name));
      QVERIFY2(resource.canConvert<QImage>(), qPrintable(name));
      QVERIFY2(!resource.value<QImage>().isNull(), qPrintable(name));
    }
    notes->verticalScrollBar()->setValue(450);
    QApplication::processEvents();
    QVERIFY(notes->grab().save(
        QDir(QtTestArtifactDirectory()).filePath("issue1029-whats-new-projects-image.png")));
    notes->verticalScrollBar()->setValue(notes->verticalScrollBar()->maximum());
    QApplication::processEvents();
    QVERIFY(notes->toPlainText().contains("0.7.7"));
    QVERIFY(whatsNew->grab().save(
        QDir(QtTestArtifactDirectory()).filePath("issue-update-migration-whats-new-077.png")));
    QVERIFY(whatsNew->findChild<QLabel*>("whatsNewReleasesLink"));
    QVERIFY(whatsNew->findChild<QCheckBox*>("whatsNewDontShowAgain"));
    auto* about = firstRun.findChild<QAction*>("aboutGoSurveyAction");
    QVERIFY(about);
    about->trigger();
    QCOMPARE(firstRun.findChildren<QDialog*>("whatsNewDialog").size(), 1);
    whatsNew->hide();
  }

  void issue1079OpeningDwGWithoutProjectCreatesFocusedTab() {
    EditorWindow window;
    const auto previousTabCount = window.State().drawingTabs.size();
    const QString drawingPath = QString::fromUtf8(GOSURVEY_SOURCE_DIR) + "/samples/surface-demo.dwg";
    bool chooserShown = false;
    QTimer dialogPoll(&window);
    dialogPoll.setInterval(10);
    connect(&dialogPoll, &QTimer::timeout, &window, [&] {
      if (auto* chooser = qobject_cast<QDialog*>(QApplication::activeModalWidget());
          chooser && chooser->objectName() == "drawingTemplateDialog") {
        chooserShown = true;
        chooser->reject();
      } else if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) message->accept();
    });
    dialogPoll.start();

    window.OpenDrawing(drawingPath);
    dialogPoll.stop();

    QVERIFY2(!chooserShown, "Opening an existing drawing must not show the New Drawing chooser");
    QCOMPARE(window.State().drawingTabs.size(), previousTabCount + 1);
    QCOMPARE(window.State().activeDrawingIdx, static_cast<int>(previousTabCount));
    const auto& openedTab = window.State().drawingTabs[previousTabCount];
    QCOMPARE(QString::fromStdString(openedTab.name), QString("surface-demo"));
    QCOMPARE(openedTab.projectUid, std::uint32_t(0));
    QCOMPARE(QString::fromStdString(window.State().activeDocFilePath), drawingPath);
  }

  void issue1079FailedOpenKeepsPreviousDrawingAndTab() {
    EditorWindow window;
    const auto initialTabCount = window.State().drawingTabs.size();
    window.CreateDrawingFromTemplate();
    const std::vector<double> previousLines = {1.0, 2.0, 0.0, 3.0, 4.0, 0.0};
    window.State().userLinesFlat = previousLines;
    const auto previousIndex = window.State().activeDrawingIdx;
    const auto previousName = window.State().drawingTabs[static_cast<size_t>(previousIndex)].name;
    const auto previousActivePath = window.State().activeDocFilePath;
    const auto previousDocumentCount = window.State().documents.size();
    QTimer dialogPoll(&window);
    dialogPoll.setInterval(10);
    connect(&dialogPoll, &QTimer::timeout, &window, [] {
      if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) message->accept();
    });
    dialogPoll.start();

    window.OpenDrawing(QDir::temp().filePath("issue-1079-missing.dwg"));
    dialogPoll.stop();

    QCOMPARE(window.State().drawingTabs.size(), initialTabCount + 1);
    QCOMPARE(window.State().documents.size(), previousDocumentCount);
    QCOMPARE(window.State().activeDrawingIdx, previousIndex);
    QCOMPARE(window.State().drawingTabs[static_cast<size_t>(previousIndex)].name, previousName);
    QCOMPARE(window.State().activeDocFilePath, previousActivePath);
    QCOMPARE(window.State().userLinesFlat, previousLines);
  }

  void directDrawingStartupDoesNotAutoOpenWhatsNew() {
    EditorWindow window;
    window.CreateDrawingFromTemplate(0);
    window.InitializeStartup();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));
  }

  void updateOfferSuppressesWhatsNewForTheLaunch() {
    EditorWindow window;
    window.State().whatsNewDismissedVersion.clear();   // would otherwise auto-open
    auto* dialog = OfferUpdate(window, OfferedManifest());
    QVERIFY(dialog);
    window.InitializeStartup();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));
    UpdateButton(dialog, "updateRemindButton")->click();
    window.ServiceUpdate();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));   // still not later in the same launch
  }

  void completedUpdateCheckDoesNotSuppressWhatsNewStartup() {
    EditorWindow window;
    window.State().whatsNewDismissedVersion.clear();
    window.m_Update->phase = update::Phase::Checking;
    window.m_Update->task = std::make_unique<update::UpdateTask>();
    window.m_Update->task->ok = false;
    window.m_Update->task->error = "0.7.4 is the latest";
    window.m_Update->task->done.store(true, std::memory_order_release);

    // This is the real launch ordering: the startup check has completed, then startup
    // initialization must consume its result before deciding whether an offer has priority.
    window.InitializeStartup();

    QCOMPARE(window.m_Update->phase, update::Phase::Idle);
    QVERIFY(window.findChild<QDialog*>("whatsNewDialog"));
  }

  void malformedOpenAndSavePreserveExistingSession() {
    auto state = StateWithTraverse();
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    auto* editor = window.findChild<QPlainTextEdit*>("adjustmentCommands");
    QVERIFY(editor);
    const QString before = editor->toPlainText();
    QTemporaryDir directory;
    QFile malformed(directory.filePath("bad.gstrav"));
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    malformed.write("{not json}"); malformed.close();
    QString error;
    QVERIFY(!window.LoadFile(malformed.fileName(), error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(editor->toPlainText(), before);
    editor->setPlainText("not-a-command");
    QVERIFY(window.HasUnsavedChanges());
    QVERIFY(!window.SaveFile(directory.filePath("bad-save.gstrav"), error));
    QVERIFY(!QFile::exists(directory.filePath("bad-save.gstrav")));
    QVERIFY(!window.AcceptResult(error));
  }

  void issue913RawImportsMergeAndReportPerFileFailures() {
    auto state = StateWithTraverse();
    state->traverseData = {};
    AppCommandState::ProjectSession readOnlyProject;
    readOnlyProject.uid = 913;
    readOnlyProject.readOnly = true;
    state->openProjects.push_back(readOnlyProject);
    state->drawingTabs[1].projectUid = readOnlyProject.uid;
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    const QString fbk = QString::fromUtf8(GOSURVEY_SOURCE_DIR) + "/samples/traverse_sample.fbk";
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile bad(directory.filePath("bad.xml"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("<not-jobxml>"); bad.close();

    QStringList failures;
    QCOMPARE(window.ImportFiles({fbk, fbk}, failures), 2);
    QVERIFY(failures.empty());
    auto* files = window.findChild<QListWidget*>("issue913ImportedFiles");
    QVERIFY(files);
    QCOMPARE(files->count(), 2);
    QVERIFY(files->item(0)->text().startsWith("Imported"));
    QVERIFY(files->item(1)->text().startsWith("Imported"));
    window.resize(1220, 780);
    window.show();
    QCoreApplication::processEvents();
    auto* filesPanel = window.findChild<QWidget*>("adjustmentLeftPanel");
    QVERIFY(filesPanel);
    QVERIFY(filesPanel->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-913-files-success.png")));

    QCOMPARE(window.ImportFiles({bad.fileName()}, failures), 0);
    QCOMPARE(failures.size(), 1);
    QVERIFY(!failures.front().isEmpty());
    QCOMPARE(files->count(), 3);
    QVERIFY(files->item(2)->text().startsWith("Failed"));

    TraverseData oneImport;
    std::string reason;
    QVERIFY(FbkImport(fbk.toUtf8().constData(), oneImport, reason));
    const auto expectedLegs = oneImport.legs.size();
    const auto expectedPoints = state->surveyPoints.size();
    QVERIFY(!CanUndo(*state));
    QString saveError;
    const QString saved = directory.filePath("merged.gstrav");
    QVERIFY2(window.SaveFile(saved, saveError), qPrintable(saveError));
    TraverseData loaded;
    LsaWeights weights;
    QVERIFY(traverseio::Load(saved.toUtf8().toStdString(), loaded, weights, reason));
    QCOMPARE(loaded.legs.size(), expectedLegs * 2);
    QCOMPARE(loaded.importedFiles.size(), std::size_t(2));
    std::vector<int> stationIds;
    stationIds.reserve(loaded.legs.size());
    for (const auto& leg : loaded.legs) stationIds.push_back(leg.stationId);
    std::sort(stationIds.begin(), stationIds.end());
    QVERIFY(std::adjacent_find(stationIds.begin(), stationIds.end()) == stationIds.end());
    QCOMPARE(state->surveyPoints.size(), expectedPoints);
    QVERIFY(!CanUndo(*state));

    QCoreApplication::processEvents();
    QVERIFY(!files->item(2)->toolTip().isEmpty());
    QVERIFY(filesPanel->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-913-files-error.png")));
    window.hide();
  }

  void issue913JobXmlImportsThroughQtAndCancelKeepsTheSession() {
    auto state = StateWithTraverse();
    state->traverseData = {};
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    const QString jobXml = QString::fromUtf8(GOSURVEY_SOURCE_DIR) + "/samples/closed_loop_trav.jxl";
    QStringList failures;
    QCOMPARE(window.ImportFiles({jobXml}, failures), 1);
    QVERIFY(failures.empty());
    window.resize(1220, 780);
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.findChild<QWidget*>("adjustmentLeftPanel")->grab().save(
        QDir(QtTestArtifactDirectory()).filePath("qt-913-jobxml-import.png")));
    const QString beforeCancel = window.findChild<QPlainTextEdit*>("adjustmentCommands")->toPlainText();
    const int filesBeforeCancel = window.findChild<QListWidget*>("issue913ImportedFiles")->count();
    QCOMPARE(window.ImportFiles({}, failures), 0);
    QCOMPARE(window.findChild<QPlainTextEdit*>("adjustmentCommands")->toPlainText(), beforeCancel);
    QCOMPARE(window.findChild<QListWidget*>("issue913ImportedFiles")->count(), filesBeforeCancel);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString saveError;
    const QString saved = directory.filePath("jobxml.gstrav");
    QVERIFY2(window.SaveFile(saved, saveError), qPrintable(saveError));
    TraverseData loaded;
    LsaWeights weights;
    std::string reason;
    QVERIFY(traverseio::Load(saved.toUtf8().toStdString(), loaded, weights, reason));
    QCOMPARE(loaded.importedFiles.size(), std::size_t(1));
    QCOMPARE(QString::fromUtf8(loaded.importedFiles.front().c_str()), jobXml);
    QVERIFY(!loaded.legs.empty());
  }

  void asynchronousSolveAcceptanceCommitAndUndo() {
    auto state = StateWithTraverse();
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    auto* run = Action(window, "Run adjustment");
    QVERIFY(run);
    run->trigger();
    QVERIFY(window.IsBusy());
    QVERIFY(!run->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);
    QVERIFY(state->traverseLsaResult.ok);
    QVERIFY(state->traverseLsaResult.adjClosureLinear < 0.001);
    QString error;
    QVERIFY2(window.AcceptResult(error), qPrintable(error));
    QVERIFY(state->traverseLsaAccepted);
    const auto originalOptions = state->createPointsOpts;
    QCOMPARE(window.CommitToDrawing(), 4); // Closing observation is the same start monument.
    QCOMPARE(state->surveyPoints.size(), std::size_t(4));
    QCOMPARE(state->createPointsOpts.startNumber, originalOptions.startNumber);
    QVERIFY(DoUndo(*state, log));
    QVERIFY(state->surveyPoints.empty());
    QVERIFY(DoRedo(*state, log));
    QCOMPARE(state->surveyPoints.size(), std::size_t(4));
  }

  void issue914OpenTraverseSolveAndCommandErrorRecovery() {
    auto state = StateWithTraverse();
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    auto* editor = window.findChild<QPlainTextEdit*>("adjustmentCommands");
    auto* output = window.findChild<QPlainTextEdit*>("adjustmentOutput");
    auto* run = Action(window, "Run adjustment");
    QVERIFY(editor); QVERIFY(output); QVERIFY(run);

    editor->setPlainText("STAITON 0 0 0 0\n");
    run->trigger();
    QVERIFY(!window.IsBusy());
    QVERIFY(output->toPlainText().contains("line 1"));
    QVERIFY(output->toPlainText().contains("Did you mean STATION?"));
    QVERIFY(run->isEnabled());
    QVERIFY(!state->traverseLsaComputed);
    window.ValidateNow();
    QVERIFY(window.findChild<QLabel*>("adjustmentTextStatus")->text().contains("Did you mean STATION?"));
    QVERIFY(window.grab().save(Issue914Shot("qt-914-command-highlighting-fuzzy-match.png")));

    QString missingEndpoint = OpenTraverseCommands();
    missingEndpoint.remove("ENDPOINT 0 100 0\n");
    editor->setPlainText(missingEndpoint);
    run->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);
    QVERIFY(!state->traverseLsaResult.ok);
    QVERIFY(output->toPlainText().contains("known end-point"));
    QVERIFY(run->isEnabled());
    QVERIFY(window.grab().save(Issue914Shot("qt-914-open-traverse-failure.png")));

    editor->setPlainText(OpenTraverseCommands());
    run->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);
    QVERIFY(state->traverseLsaResult.ok);
    QCOMPARE(state->traverseLsaResult.redundancy, 2);
    QVERIFY(state->traverseLsaResult.adjClosureLinear < 1e-3);
    QVERIFY(output->toPlainText().contains("Adjusted"));
    QVERIFY(window.grab().save(Issue914Shot("qt-914-open-traverse-adjusted.png")));
    QString error;
    QVERIFY2(window.AcceptResult(error), qPrintable(error));
  }

  void issue914StopDiscardsSolveAndPreservesInputs() {
    auto state = StateWithTraverse();
    const double originalDistance = state->traverseData.legs.front().horizDist;
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.show();
    QTRY_VERIFY(window.isVisible());
    auto* editor = window.findChild<QPlainTextEdit*>("adjustmentCommands");
    auto* output = window.findChild<QPlainTextEdit*>("adjustmentOutput");
    auto* run = Action(window, "Run adjustment");
    auto* stop = Action(window, "Stop");
    QVERIFY(editor); QVERIFY(output); QVERIFY(run); QVERIFY(stop);
    editor->setPlainText(OpenTraverseCommands());
    const QString source = editor->toPlainText();

    run->trigger();
    QVERIFY(window.IsBusy());
    QVERIFY(stop->isEnabled());
    QVERIFY(window.grab().save(Issue914Shot("qt-914-adjustment-busy.png")));
    stop->trigger();
    QVERIFY(!stop->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);

    QVERIFY(output->toPlainText().contains("Adjustment cancelled"));
    QVERIFY(window.grab().save(Issue914Shot("qt-914-adjustment-cancelled.png")));
    QCOMPARE(editor->toPlainText(), source);
    QVERIFY(!state->traverseLsaComputed);
    QVERIFY(!state->traverseLsaAccepted);
    QCOMPARE(state->traverseData.legs.front().horizDist, originalDistance);
    QVERIFY(window.findChild<QTableWidget*>("adjustmentCoordinates")->rowCount() == 0);
    QVERIFY(window.findChild<QTableWidget*>("adjustmentResiduals")->rowCount() == 0);
    QVERIFY(run->isEnabled());
  }

  void invalidAdjustmentDoesNotMutateDrawing() {
    auto state = StateWithTraverse();
    ComputeTraverse(state->traverseData);
    auto result = ComputeTraverseLeastSquares(state->traverseData, state->traverseLsaWeights);
    QVERIFY(result.ok);
    result.adjEasting[0] = std::numeric_limits<double>::quiet_NaN();
    std::string reason;
    const double before = state->traverseData.legs[0].computedEasting;
    QVERIFY(!AcceptTraverseAdjustment(state->traverseData, result, reason));
    QCOMPARE(state->traverseData.legs[0].computedEasting, before);
    std::vector<std::string> log;
    QCOMPARE(CommitTraverseToDrawing(*state, state->traverseData, &result, log), 0);
    QVERIFY(state->surveyPoints.empty());
    QVERIFY(!CanUndo(*state));
  }

  void networkElevationsAreAcceptedWithoutGpsVectors() {
    auto state = StateWithTraverse();
    LsaResult result;
    result.ok = true;
    result.stationIds = {1}; result.adjEasting = {10}; result.adjNorthing = {20}; result.adjElevation = {123};
    LsaMeasResidual vertical;
    vertical.kind = LsaMeasKind::Vertical;
    result.measResiduals.push_back(vertical);
    std::string error;
    QVERIFY(TraverseAdjustmentHasElevations(result));
    QVERIFY(AcceptTraverseAdjustment(state->traverseData, result, error));
    QCOMPARE(state->traverseData.legs[0].computedElevation, 123.0);
    result.adjElevation.clear();
    QVERIFY(!AcceptTraverseAdjustment(state->traverseData, result, error));
    QCOMPARE(state->traverseData.legs[0].computedElevation, 123.0);
  }

  void networkCommitKeepsControlsAndUsesDrawingOrigin() {
    auto state = StateWithTraverse();
    state->traverseData = {};
    NetworkStation control;
    control.id = 10;
    control.easting = {1010, NetworkFix::Fixed, 0, true};
    control.northing = {2020, NetworkFix::Fixed, 0, true};
    control.elevation = {30, NetworkFix::Fixed, 0, true};
    state->traverseData.networkStations.push_back(control);
    state->worldDocumentOriginX = 1000; state->worldDocumentOriginY = 2000;
    LsaResult result;
    result.ok = true; result.stationIds = {20}; result.adjEasting = {1030}; result.adjNorthing = {2040}; result.adjElevation = {50};
    LsaMeasResidual vertical; vertical.kind = LsaMeasKind::Vertical; result.measResiduals.push_back(vertical);
    std::vector<std::string> log;
    QCOMPARE(CommitTraverseToDrawing(*state, state->traverseData, &result, log), 2);
    QCOMPARE(state->surveyPoints[0].id, 10);
    QCOMPARE(state->surveyPoints[0].easting, 10.0);
    QCOMPARE(state->surveyPoints[0].northing, 20.0);
    QCOMPARE(state->surveyPoints[1].id, 20);
    QCOMPARE(state->surveyPoints[1].easting, 30.0);
    QCOMPARE(state->surveyPoints[1].elevation, 50.0);
  }

  void unsavedAdjustmentCanCancelClosing() {
    auto state = StateWithTraverse();
    std::vector<std::string> log;
    auto* window = new AdjustmentWindow(*state, log, nullptr);
    window->show();
    auto* editor = window->findChild<QPlainTextEdit*>("adjustmentCommands");
    editor->appendPlainText("// unsaved edit");
    QVERIFY(window->HasUnsavedChanges());
    AnswerNextDialog(QMessageBox::Cancel);
    QVERIFY(!window->close());
    QVERIFY(window->isVisible());
    AnswerNextDialog(QMessageBox::Discard);
    QVERIFY(window->close());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  void issue911TraverseUnitLabelsFollowTheDrawingUnit() {
    auto state = StateWithTraverse();
    state->drawingInsUnits = 6;
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    auto* units = window.findChild<QLabel*>("adjustmentUnits");
    QVERIFY(units);
    QCOMPARE(units->text(), QString("Units: Meters"));
    auto* coordinates = window.findChild<QTableWidget*>();
    QVERIFY(coordinates);
    QCOMPARE(coordinates->horizontalHeaderItem(1)->text(), QString("Easting (m)"));
    QVERIFY(window.findChild<QLabel*>("adjustmentResidualNote")->text().contains(" m."));
    state->drawingInsUnits = 2;
    AdjustmentWindow feet(*state, log, nullptr);
    QCOMPARE(feet.findChild<QLabel*>("adjustmentUnits")->text(), QString("Units: Feet"));
    QCOMPARE(feet.findChild<QTableWidget*>()->horizontalHeaderItem(2)->text(), QString("Northing (ft)"));
  }

  void issue911ScaleFactorIsOffByDefaultAndLeavesTheTraverseUnchanged() {
    auto state = StateWithTraverse();
    GiveDrawingTexasScale(*state);
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    QVERIFY(!window.findChild<QCheckBox*>("adjustmentScaleFactor")->isChecked());
    QVERIFY(!window.findChild<QComboBox*>("adjustmentScaleKind")->isEnabled());
    SolveAdjustment(window);
    QVERIFY(state->traverseLsaResult.ok);
    QVERIFY(!state->traverseData.applyScaleFactor);
    QCOMPARE(state->traverseData.scaleFactor, 1.0);
    for (const auto& leg : state->traverseData.legs) QCOMPARE(leg.computedHorizDist, leg.computedGroundHorizDist);
  }

  void issue911ScaleFactorCombinedGridOnlyAndTextStayInStep() {
    auto state = StateWithTraverse();
    GiveDrawingTexasScale(*state);
    double grid = 0.0, combined = 0.0;
    QVERIFY(ResolveTraverseScaleFactor(*state, TraverseData::ScaleFactorKind::GridOnly, &grid).empty());
    QVERIFY(ResolveTraverseScaleFactor(*state, TraverseData::ScaleFactorKind::Combined, &combined).empty());
    QVERIFY(grid != combined);
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    auto* editor = window.findChild<QPlainTextEdit*>("adjustmentCommands");
    auto* check = window.findChild<QCheckBox*>("adjustmentScaleFactor");
    auto* kind = window.findChild<QComboBox*>("adjustmentScaleKind");
    auto* readout = window.findChild<QLabel*>("adjustmentScaleReadout");
    // Bar -> text: the checkbox rewrites the #SCALE line and the readout shows the resolved factor.
    check->setChecked(true);
    QVERIFY(editor->toPlainText().contains("#SCALE ON COMBINED"));
    QVERIFY(kind->isEnabled());
    QCOMPARE(readout->text(), QString("k = %1").arg(combined, 0, 'f', 6));
    SolveAdjustment(window);
    QVERIFY(state->traverseLsaResult.ok);
    QCOMPARE(state->traverseData.scaleFactor, combined);
    for (const auto& leg : state->traverseData.legs)
      QVERIFY(std::abs(leg.computedHorizDist - leg.computedGroundHorizDist * combined) < 1e-9);
    const double combinedPerimeter = state->traverseData.legs[1].computedHorizDist;
    // Grid only ignores the sea-level reduction and invalidates the previous solve.
    kind->setCurrentIndex(1);
    QVERIFY(editor->toPlainText().contains("#SCALE ON GRID"));
    QVERIFY(!state->traverseLsaComputed);
    QCOMPARE(readout->text(), QString("k = %1").arg(grid, 0, 'f', 6));
    SolveAdjustment(window);
    QCOMPARE(state->traverseData.scaleFactor, grid);
    QVERIFY(state->traverseData.legs[1].computedHorizDist != combinedPerimeter);
    // Text -> bar: hand-editing the directive moves the controls.
    editor->setPlainText(editor->toPlainText().replace("#SCALE ON GRID", "#SCALE OFF COMBINED"));
    QVERIFY(!check->isChecked());
    QCOMPARE(kind->currentIndex(), 0);
    SolveAdjustment(window);
    QCOMPARE(state->traverseData.scaleFactor, 1.0);
    // A hand-written file with no #SCALE line gets one added without disturbing the rest.
    editor->setPlainText(editor->toPlainText().replace(QRegularExpression("#SCALE[^\n]*\n"), ""));
    QVERIFY(!editor->toPlainText().contains("#SCALE"));
    const int lines = editor->document()->blockCount();
    check->setChecked(true);
    QVERIFY(editor->toPlainText().contains("#SCALE ON COMBINED"));
    QCOMPARE(editor->document()->blockCount(), lines + 1);
  }

  void issue911ScaleFactorReportsWhyItCannotBeApplied() {
    auto state = StateWithTraverse();
    state->drawingInsUnits = 2;   // no coordinate system: "No Datum, No Projection"
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.findChild<QCheckBox*>("adjustmentScaleFactor")->setChecked(true);
    const QString readout = window.findChild<QLabel*>("adjustmentScaleReadout")->text();
    QVERIFY(readout.startsWith("(") && readout.size() > 3);
    SolveAdjustment(window);   // still solves, unscaled
    QVERIFY(state->traverseLsaResult.ok);
    QCOMPARE(state->traverseData.scaleFactor, 1.0);
    for (const auto& leg : state->traverseData.legs) QCOMPARE(leg.computedHorizDist, leg.computedGroundHorizDist);
    QVERIFY(window.findChild<QPlainTextEdit*>("adjustmentCommands")->toPlainText().contains("#SCALE ON"));
    auto* output = window.findChild<QPlainTextEdit*>("adjustmentOutput");
    QVERIFY(output &&output->toPlainText().startsWith("Scale factor not applied"));
  }

  void issue911ScaleFactorSurvivesSaveAndLoad() {
    auto state = StateWithTraverse();
    GiveDrawingTexasScale(*state);
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.findChild<QCheckBox*>("adjustmentScaleFactor")->setChecked(true);
    window.findChild<QComboBox*>("adjustmentScaleKind")->setCurrentIndex(1);
    QTemporaryDir directory;
    QString error;
    const QString path = directory.filePath("scaled.gstrav");
    QVERIFY2(window.SaveFile(path, error), qPrintable(error));
    TraverseData loaded; LsaWeights weights; std::string reason;
    QVERIFY2(traverseio::Load(path.toUtf8().toStdString(), loaded, weights, reason), reason.c_str());
    QVERIFY(loaded.applyScaleFactor);
    QVERIFY(loaded.scaleFactorKind == TraverseData::ScaleFactorKind::GridOnly);
    double grid = 0.0;
    QVERIFY(ResolveTraverseScaleFactor(*state, TraverseData::ScaleFactorKind::GridOnly, &grid).empty());
    QCOMPARE(loaded.scaleFactor, grid);
    window.findChild<QCheckBox*>("adjustmentScaleFactor")->setChecked(false);
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QVERIFY(window.findChild<QCheckBox*>("adjustmentScaleFactor")->isChecked());
    QCOMPARE(window.findChild<QComboBox*>("adjustmentScaleKind")->currentIndex(), 1);
  }

  void issue911CurvatureRefractionControlsDriveElevations() {
    auto state = StateWithTraverse();
    for (auto& leg : state->traverseData.legs) { leg.hasVertAngle = true; leg.isZenithAngle = true; leg.vertAngleDeg = 89.5; }
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    auto* check = window.findChild<QCheckBox*>("adjustmentCurvRefr");
    auto* k = window.findChild<QDoubleSpinBox*>("adjustmentRefractionK");
    auto* warning = window.findChild<QLabel*>("adjustmentRefractionWarning");
    QVERIFY(check->isChecked());
    QCOMPARE(k->value(), 0.14);
    QVERIFY(k->isEnabled());
    QVERIFY(warning->isHidden());
    SolveAdjustment(window);
    const double defaultDz = state->traverseData.legs[1].computedDeltaZ;
    const double hd = state->traverseData.legs[1].computedHorizDist;
    constexpr double earthRadius = 6371000.0;
    // Off: plain trigonometry, and a previous solve is no longer committable.
    check->setChecked(false);
    QVERIFY(!k->isEnabled());
    QVERIFY(!state->traverseLsaComputed);
    SolveAdjustment(window);
    const double plainDz = state->traverseData.legs[1].computedDeltaZ;
    QVERIFY(std::abs((defaultDz - plainDz) - (1.0 - 0.14) * hd * hd / (2.0 * earthRadius)) < 1e-9);
    // On with a different k.
    check->setChecked(true);
    k->setValue(0.5);
    SolveAdjustment(window);
    QVERIFY(std::abs((state->traverseData.legs[1].computedDeltaZ - plainDz) - 0.5 * hd * hd / (2.0 * earthRadius)) < 1e-9);
    QVERIFY(warning->isHidden());
    // A scale-directive edit re-reads the text; the curvature setting must survive it.
    window.findChild<QCheckBox*>("adjustmentScaleFactor")->setChecked(true);
    SolveAdjustment(window);
    QVERIFY(state->traverseData.applyCurvRefCorr);
    QCOMPARE(state->traverseData.refractionCoeff, 0.5);
    // Out of the typical -2..+2 range warns, but still computes.
    k->setValue(3.0);
    QVERIFY(!warning->isHidden());
    SolveAdjustment(window);
    QVERIFY(state->traverseLsaResult.ok);
    check->setChecked(false);
    QVERIFY(warning->isHidden());
  }

  void issue915SettingsPaneLabelsLinearValuesInTheDrawingUnit() {
    auto state = StateWithTraverse();
    state->drawingInsUnits = 6;
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    Action(window, "Settings")->trigger();
    auto* settings = window.findChild<QScrollArea*>("adjustmentSettingsPane");
    QVERIFY(settings);
    QVERIFY(window.findChild<QDoubleSpinBox*>("issue915DistanceConstant"));
    QVERIFY(window.findChild<QDoubleSpinBox*>("issue915ElevationConstant"));
    const auto labels = settings->findChildren<QLabel*>();
    bool distanceUsesMeters = false;
    for (const auto* label : labels) distanceUsesMeters |= label->text().contains("Distance constant (m)");
    QVERIFY(distanceUsesMeters);
    QVERIFY(window.findChild<QDoubleSpinBox*>("issue915DistanceConstant")->minimum() > 0.0);
  }

  void issue915SettingsApplyDiscardValidationAndSessionPersistence() {
    auto state = StateWithTraverse();
    state->drawingInsUnits = 6;
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.resize(1280, 800);
    window.show();
    QTRY_VERIFY(window.isVisible());
    Action(window, "Settings")->trigger();
    auto* type = window.findChild<QComboBox*>("issue915AdjustmentType");
    auto* iterations = window.findChild<QSpinBox*>("issue915MaxIterations");
    auto* distancePpm = window.findChild<QDoubleSpinBox*>("issue915DistancePpm");
    auto* centering = window.findChild<QDoubleSpinBox*>("issue915VerticalCentering");
    auto* convergence = window.findChild<QDoubleSpinBox*>("issue915ConvergenceLimit");
    auto* refraction = window.findChild<QDoubleSpinBox*>("issue915RefractionCoefficient");
    auto* status = window.findChild<QLabel*>("issue915SettingsStatus");
    QVERIFY(type); QVERIFY(iterations); QVERIFY(distancePpm); QVERIFY(centering); QVERIFY(convergence); QVERIFY(refraction); QVERIFY(status);
    auto* contextTabs = window.findChild<QTabWidget*>("adjustmentContextTabs");
    QVERIFY(contextTabs);
    QVERIFY(contextTabs->grab().save(Issue914Shot("qt-915-settings-pane.png")));

    const LsaWeights original = state->traverseLsaWeights;
    distancePpm->setValue(12.5);
    window.findChild<QPushButton*>("issue915DiscardSettings")->click();
    QCOMPARE(distancePpm->value(), original.sigmaDistPpm);
    QCOMPARE(state->traverseLsaWeights.sigmaDistPpm, original.sigmaDistPpm);

    const auto validText = window.findChild<QPlainTextEdit*>("adjustmentCommands")->toPlainText();
    window.findChild<QPlainTextEdit*>("adjustmentCommands")->setPlainText("NOT_A_COMMAND 1 2");
    distancePpm->setValue(12.5);
    window.findChild<QPushButton*>("issue915ApplySettings")->click();
    QVERIFY(status->text().contains("Settings were not applied"));
    QCOMPARE(state->traverseLsaWeights.sigmaDistPpm, original.sigmaDistPpm);
    QVERIFY(contextTabs->grab().save(Issue914Shot("qt-915-settings-validation-error.png")));
    window.findChild<QPlainTextEdit*>("adjustmentCommands")->setPlainText(validText);

    type->setCurrentIndex(1);
    iterations->setValue(24);
    distancePpm->setValue(12.5);
    centering->setValue(.035);
    convergence->setValue(5.e-8);
    refraction->setValue(.21);
    window.findChild<QPushButton*>("issue915ApplySettings")->click();
    QVERIFY(status->text().contains("Save .gstrav"));
    QCOMPARE(state->traverseLsaWeights.adjustmentIs3D, true);
    QCOMPARE(state->traverseLsaWeights.maxIterations, 24);
    QCOMPARE(state->traverseLsaWeights.sigmaDistPpm, 12.5);
    QCOMPARE(state->traverseLsaWeights.centeringVerticalFt, .035);
    QCOMPARE(state->traverseLsaWeights.convergenceLimit, 5.e-8);
    QCOMPARE(state->traverseLsaWeights.refractionCoefficient, .21);
    QVERIFY(!state->traverseLsaComputed);
    QVERIFY(contextTabs->grab().save(Issue914Shot("qt-915-settings-applied.png")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const auto path = directory.filePath("adjustment.gstrav");
    QVERIFY2(window.SaveFile(path, error), qPrintable(error));
    TraverseData loaded;
    LsaWeights loadedWeights;
    std::string reason;
    QVERIFY2(traverseio::Load(path.toUtf8().toStdString(), loaded, loadedWeights, reason), reason.c_str());
    QCOMPARE(loadedWeights.adjustmentIs3D, true);
    QCOMPARE(loadedWeights.maxIterations, 24);
    QCOMPARE(loadedWeights.sigmaDistPpm, 12.5);
    QCOMPARE(loadedWeights.centeringVerticalFt, .035);
    QCOMPARE(loadedWeights.convergenceLimit, 5.e-8);
    QCOMPARE(loadedWeights.refractionCoefficient, .21);
  }

  void issue915ReportExportsAndStationPlotFollowSuccessfulAdjustment() {
    auto state = StateWithTraverse();
    state->traverseData.importedFiles = {"survey-a.dat", "survey-b.rw5"};
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.resize(1280, 800);
    window.show();
    QTRY_VERIFY(window.isVisible());
    SolveAdjustment(window);
    auto* output = window.findChild<QPlainTextEdit*>("adjustmentOutput");
    auto* plotWidget = window.findChild<QWidget*>("adjustmentResultPlan");
    auto* plot = static_cast<AdjustmentPlanView*>(plotWidget);
    auto* results = window.findChild<QTabWidget*>("adjustmentResults");
    QVERIFY(output); QVERIFY(plotWidget); QVERIFY(plot); QVERIFY(results);
    QVERIFY(state->traverseLsaResult.ok);
    QVERIFY(output->toPlainText().contains("survey-a.dat"));
    const auto text = output->toPlainText();
    const qsizetype filesAt = text.indexOf("Data File List:");
    const qsizetype optionsAt = text.indexOf("Project Option Settings");
    const qsizetype errorsAt = text.indexOf("Instrument Standard Error Settings");
    const qsizetype residualsAt = text.indexOf("Measurement Residuals");
    QVERIFY(filesAt >= 0 && filesAt < optionsAt && optionsAt < errorsAt && errorsAt < residualsAt);
    QCOMPARE(plot->StationCount(), 4);
    QCOMPARE(plot->LegCount(), 4);
    QVERIFY(plot->EllipseCount() > 0);
    QVERIFY(results->grab().save(Issue914Shot("qt-915-report-and-plot.png")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString txtPath = directory.filePath("adjustment.txt");
    const QString rtfPath = directory.filePath("adjustment.rtf");
    const QString pdfPath = directory.filePath("adjustment.pdf");
    QVERIFY2(window.ExportReportFile(txtPath, error), qPrintable(error));
    QVERIFY2(window.ExportReportFile(rtfPath, error), qPrintable(error));
    QVERIFY2(window.ExportReportFile(pdfPath, error), qPrintable(error));
    QFile txt(txtPath), rtf(rtfPath), pdf(pdfPath);
    QVERIFY(txt.open(QIODevice::ReadOnly));
    QVERIFY(rtf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QCOMPARE(txt.readAll(), text.toUtf8());
    QVERIFY(rtf.readAll().startsWith("{\\rtf"));
    QVERIFY(pdf.readAll().startsWith("%PDF-"));

    const QString beforeFailure = output->toPlainText();
    QVERIFY(!window.ExportReportFile(directory.filePath("missing/report.txt"), error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(output->toPlainText(), beforeFailure);
    QVERIFY(state->traverseLsaComputed);
  }

  // Issue #911: screenshots of the chosen layout (options bar under the toolbar) for review; they are
  // written to build/artifacts and never committed.
  void issue911CaptureTraverseOptionsBarScreenshots() {
    const QString artifacts = QtTestArtifactDirectory() + "/";
    auto capture = [&](const QString& name, bool texas, bool solve, double refraction) {
      auto state = StateWithTraverse();
      for (auto& leg : state->traverseData.legs) { leg.hasVertAngle = true; leg.isZenithAngle = true; leg.vertAngleDeg = 89.5; }
      if (texas) GiveDrawingTexasScale(*state);
      std::vector<std::string> log;
      AdjustmentWindow window(*state, log, nullptr);
      window.resize(1200, 520);
      window.show();
      window.findChild<QCheckBox*>("adjustmentScaleFactor")->setChecked(true);
      window.findChild<QDoubleSpinBox*>("adjustmentRefractionK")->setValue(refraction);
      if (solve) SolveAdjustment(window);
      QCoreApplication::processEvents();
      QVERIFY(window.grab().save(artifacts + "qt-911-" + name + ".png"));
    };
    capture("solved", true, true, 0.14);
    capture("no-coordinate-system", false, false, 0.14);
    capture("refraction-warning", true, false, 3.0);
    QVERIFY(QFileInfo(artifacts + "qt-911-solved.png").size() > 0);
  }

  void issue912HighlighterColoursEachTokenKindAndKeepsText() {
    const auto spans = ClassifyAdjustmentLine("STN 1 2.5 #LOOP ON 'TH CN");
    QCOMPARE(spans.size(), 6);
    QCOMPARE(int(spans[0].kind), int(AdjustmentToken::Keyword));
    QCOMPARE(int(spans[1].kind), int(AdjustmentToken::Number));
    QCOMPARE(int(spans[2].kind), int(AdjustmentToken::Number));
    QCOMPARE(int(spans[3].kind), int(AdjustmentToken::Option));
    QCOMPARE(int(spans[4].kind), int(AdjustmentToken::Keyword));
    QCOMPARE(int(spans[5].kind), int(AdjustmentToken::FieldCode));
    QCOMPARE(ClassifyAdjustmentLine("  // a note").size(), 1);
    QCOMPARE(int(ClassifyAdjustmentLine("  // a note")[0].kind), int(AdjustmentToken::Comment));
    const auto field = ClassifyAdjustmentLine("HDIST 1 2 100 'TH CN 5");
    QCOMPARE(int(field.last().kind), int(AdjustmentToken::FieldCode));
    QCOMPARE(field.last().start, 14);
    QCOMPARE(field.last().length, 8);
    QVERIFY(ClassifyAdjustmentLine("somename").isEmpty());

    for (const bool dark : {true, false}) {
      AdjustmentCodeEdit editor;
      editor.SetPalette(AdjustmentPalette(dark));
      const QString text = "STN 1 2\n// note\n#LOOP ON\nHDIST 1 2 100.5 'TH";
      editor.setPlainText(text);
      QCOMPARE(editor.toPlainText(), text);  // colours never change the text
      const auto& palette = editor.Palette();
      const QColor colours[] = {palette.keyword, palette.option, palette.number, palette.comment, palette.fieldCode};
      for (int i = 0; i < 5; ++i) {
        QVERIFY(colours[i] != palette.background);
        QVERIFY2(qAbs(colours[i].lightness() - palette.background.lightness()) > 60, "token colour is hard to read");
        for (int j = i + 1; j < 5; ++j) QVERIFY(colours[i] != colours[j]);
      }
      auto formatColour = [&](int block, int position) {
        const auto formats = editor.document()->findBlockByNumber(block).layout()->formats();
        for (const auto& range : formats) if (position >= range.start && position < range.start + range.length)
          return range.format.foreground().color();
        return QColor();
      };
      QCOMPARE(formatColour(0, 0), palette.keyword);
      QCOMPARE(formatColour(0, 4), palette.number);
      QCOMPARE(formatColour(1, 3), palette.comment);
      QCOMPARE(formatColour(2, 0), palette.option);
      QCOMPARE(formatColour(3, 17), palette.fieldCode);
      QVERIFY(!formatColour(0, 3).isValid());  // plain space keeps the text colour
    }
  }

  void issue912GutterNumbersLinesWidensScrollsAndMarksError() {
    AdjustmentCodeEdit editor;
    editor.resize(480, 240);
    editor.show();
    auto lines = [](int count) { QStringList list; for (int i = 0; i < count; ++i) list << QString("STN %1 %2").arg(i).arg(i + 1); return list.join('\n'); };
    editor.setPlainText(lines(9));
    const int nine = editor.GutterWidth();
    editor.setPlainText(lines(10));
    const int ten = editor.GutterWidth();
    QVERIFY(ten > nine);
    editor.setPlainText(lines(99));
    const int ninetyNine = editor.GutterWidth();
    editor.setPlainText(lines(100));
    QVERIFY(editor.GutterWidth() > ninetyNine);
    editor.setPlainText(lines(300));
    QTest::qWait(30);
    QCOMPARE(editor.Gutter()->width(), editor.GutterWidth());
    QCOMPARE(editor.FirstVisibleLine(), 1);
    editor.verticalScrollBar()->setValue(60);  // the gutter follows the text
    QCOMPARE(editor.FirstVisibleLine(), 61);

    editor.verticalScrollBar()->setValue(0);
    const auto palette = editor.Palette();
    auto hasPixel = [&](const QColor& colour) {
      const QImage image = editor.Gutter()->grab().toImage();
      for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (QColor(image.pixel(x, y)) == colour) return true;
      return false;
    };
    QVERIFY(!hasPixel(palette.error));
    editor.SetError(2, "bad value");
    QCOMPARE(editor.ErrorLine(), 2);
    QVERIFY2(hasPixel(palette.error), "the gutter marks the error line");
    editor.SetError(0, {});
    QVERIFY(!hasPixel(palette.error));
  }

  void issue912InlineErrorShowsReasonWithoutChangingAnything() {
    auto state = StateWithTraverse();
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.show();
    auto* editor = static_cast<AdjustmentCodeEdit*>(window.findChild<QPlainTextEdit*>("adjustmentCommands"));
    auto* status = window.findChild<QLabel*>("adjustmentTextStatus");
    auto* output = window.findChild<QPlainTextEdit*>("adjustmentOutput");
    QVERIFY(editor); QVERIFY(status); QVERIFY(output);
    QCOMPARE(editor->ErrorLine(), 0);
    QCOMPARE(status->text(), QString("Command text is valid"));
    const QString good = editor->toPlainText();
    const TraverseData before = state->traverseData;
    const LsaWeights weights = state->traverseLsaWeights;
    const int lineCount = editor->blockCount();

    editor->setPlainText(good + "\nBOGUS 1 2 3");
    QTRY_VERIFY_WITH_TIMEOUT(editor->ErrorLine() > 0, 3000);  // after the typing pause
    QVERIFY(editor->ErrorLine() >= lineCount);
    QVERIFY(!editor->ErrorMessage().isEmpty());
    QVERIFY(status->text().startsWith("Parse error"));
    QVERIFY(status->text().contains(QString("line %1").arg(editor->ErrorLine())));
    QCOMPARE(state->traverseData.legs.size(), before.legs.size());  // checking mutates nothing
    QCOMPARE(state->traverseData.startEasting, before.startEasting);
    QCOMPARE(state->traverseLsaWeights.sigmaAngleSec, weights.sigmaAngleSec);
    QVERIFY(!state->traverseLsaComputed);

    QTemporaryDir directory;
    QString error;
    QVERIFY(!window.SaveFile(directory.filePath("bad.gstrav"), error));
    QVERIFY(!QFile::exists(directory.filePath("bad.gstrav")));
    QVERIFY(status->text().endsWith(error));  // the same "line N: reason" as the status line
    Action(window, "Run adjustment")->trigger();
    QVERIFY(!window.IsBusy());
    QCOMPARE(output->toPlainText(), error);
    QVERIFY(!window.AcceptResult(error));

    editor->setPlainText(good);  // fixing the line clears everything
    QTRY_COMPARE_WITH_TIMEOUT(editor->ErrorLine(), 0, 3000);
    QVERIFY(editor->ErrorMessage().isEmpty());
    QCOMPARE(status->text(), QString("Command text is valid"));
  }

  void issue912PlanPreviewBuildsLegsClosingLegShotsAndEllipses() {
    auto state = Issue912State(true);
    ComputeTraverse(state->traverseData);
    AdjustmentPlanView view;
    view.resize(420, 320);
    view.SetTraverse(state->traverseData, nullptr);
    QCOMPARE(view.StationCount(), 4);
    QCOMPARE(view.LegCount(), 4);
    QCOMPARE(view.SideShotCount(), 2);
    QVERIFY(!view.HasClosingLeg());  // the last course already ends on the start monument
    QCOMPARE(view.EllipseCount(), 0);
    QVERIFY(view.TooltipAt(QPoint(-50, -50)).isEmpty());

    auto open = StateWithTraverse();
    open->traverseData.legs.back().stationId = 4;  // loop whose last station is not the start
    ComputeTraverse(open->traverseData);
    AdjustmentPlanView loop;
    loop.resize(420, 320);
    loop.SetTraverse(open->traverseData, nullptr);
    QVERIFY(loop.HasClosingLeg());

    AdjustmentPlanView empty;
    empty.SetTraverse(TraverseData{}, nullptr);
    QVERIFY(!empty.HasEnoughStations());

    auto result = ComputeTraverseLeastSquares(state->traverseData, state->traverseLsaWeights);
    QVERIFY2(result.ok, result.message.c_str());
    QVERIFY(!result.stationSemiMajorFt.empty());
    view.SetTraverse(state->traverseData, &result);
    QVERIFY(view.EllipseCount() > 0);
    QVERIFY(view.EllipseFactor() >= 1.0);
    bool hover = false;
    for (int y = 0; y < view.height() && !hover; y += 3) for (int x = 0; x < view.width() && !hover; x += 3) {
      const QString text = view.TooltipAt(QPoint(x, y));
      if (text.contains("Semi-major") && text.contains("Semi-minor") && text.contains("Azimuth")) hover = true;
    }
    QVERIFY2(hover, "hovering a station shows its ellipse values");
    view.SetTraverse(state->traverseData, nullptr);  // the text changed: the result is stale
    QCOMPARE(view.EllipseCount(), 0);

    const double fitted = view.Scale();
    view.ZoomAt(QPointF(210, 160), 3);
    QVERIFY(view.Scale() > fitted);
    const QPointF centre = view.Center();
    view.PanBy(QPointF(30, 0));
    QVERIFY(view.Center() != centre);
    QWheelEvent wheel(QPointF(210, 160), view.mapToGlobal(QPointF(210, 160)), {}, QPoint(0, -240), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    const double before = view.Scale();
    QApplication::sendEvent(&view, &wheel);
    QVERIFY(view.Scale() < before);
    view.ZoomExtents();
    QVERIFY(qAbs(view.Scale() - fitted) < fitted * 1e-9);
  }

  void issue912FilesSummaryFollowSolveAndEditsAndRefusedCommitSaysWhy() {
    auto state = StateWithTraverse();
    AppCommandState::ProjectSession readOnlyProject;
    readOnlyProject.uid = 912; readOnlyProject.readOnly = true;
    state->openProjects.push_back(readOnlyProject);
    state->drawingTabs[1].projectUid = 912;
    std::vector<std::string> log;
    AdjustmentWindow window(*state, log, nullptr);
    window.show();
    auto* files = window.findChild<QLabel*>("adjustmentFiles");
    auto* summary = window.findChild<QLabel*>("adjustmentSummary");
    auto* plan = static_cast<AdjustmentPlanView*>(window.findChild<QWidget*>("adjustmentPlan"));
    auto* editor = static_cast<AdjustmentCodeEdit*>(window.findChild<QPlainTextEdit*>("adjustmentCommands"));
    QVERIFY(files); QVERIFY(summary); QVERIFY(plan); QVERIFY(editor);
    QVERIFY(files->text().contains(".gstrav: (unsaved)"));
    QVERIFY(summary->text().contains("Stations: 4"));
    QVERIFY(summary->text().contains("Run adjustment"));
    QCOMPARE(plan->EllipseCount(), 0);

    Action(window, "Run adjustment")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);
    QVERIFY(summary->text().contains("Redundancy"));
    QVERIFY(summary->text().contains("Std dev of unit weight"));
    QVERIFY(plan->EllipseCount() > 0);

    QTemporaryDir directory;
    QString error;
    QVERIFY2(window.SaveFile(directory.filePath("shown.gstrav"), error), qPrintable(error));
    QVERIFY(files->text().contains("shown.gstrav"));

    QVERIFY2(window.AcceptResult(error), qPrintable(error));
    AnswerNextDialog(QMessageBox::Ok);
    QCOMPARE(window.CommitToDrawing(), 0);  // read-only project: nothing placed, and the user is told why
    QVERIFY(state->surveyPoints.empty());
    QVERIFY(!CanUndo(*state));
    QVERIFY(QString::fromStdString(log.back()).contains("read-only"));

    editor->appendPlainText("// edit");  // the result no longer matches the text
    QCOMPARE(plan->EllipseCount(), 0);
    QVERIFY(summary->text().contains("Run adjustment"));
  }

  void issue912SaveInsideProjectTracksOnlyWhenWritable() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    gsproj::Project created; std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Traverse Job", &created, &error), error.c_str());
    EditorWindow window; window.CreateDrawingFromTemplate();
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    auto& state = window.State();
    state.traverseData = StateWithTraverse()->traverseData;
    state.drawingTabs[state.activeDrawingIdx].projectUid = uid;
    auto& session = *std::find_if(state.openProjects.begin(), state.openProjects.end(), [uid](const auto& item) { return item.uid == uid; });
    const QString folder = QString::fromStdWString(session.project.Folder().wstring());
    auto tracked = [&](const char* name) {
      for (const auto& item : session.project.items) if (QString::fromStdString(item.path).endsWith(name)) return true;
      return false;
    };
    std::vector<std::string> log;
    AdjustmentWindow adjustment(state, log, nullptr);
    QString reason;
    QVERIFY2(adjustment.SaveFile(folder + "/writable.gstrav", reason), qPrintable(reason));
    QVERIFY(tracked("writable.gstrav"));
    const auto itemCount = session.project.items.size();
    session.readOnly = true;  // as 0.7.4: the file is still written, but the project's file list is not
    QVERIFY2(adjustment.SaveFile(folder + "/readonly.gstrav", reason), qPrintable(reason));
    QVERIFY(QFile::exists(folder + "/readonly.gstrav"));
    QVERIFY(!tracked("readonly.gstrav"));
    QCOMPARE(session.project.items.size(), itemCount);
    session.readOnly = false;
  }

  void issue912FinalScreenshotsDarkAndLight() {
    for (const bool light : {false, true}) {
      ApplyQtAppearance(*qApp, light);
      auto state = Issue912State(true);
      std::vector<std::string> log;
      AdjustmentWindow window(*state, log, nullptr);
      window.resize(1280, 760);
      window.show();
      auto* editor = static_cast<AdjustmentCodeEdit*>(window.findChild<QPlainTextEdit*>("adjustmentCommands"));
      QString text = editor->toPlainText();
      text.replace(QRegularExpression("(HDIST\\s+0\\s+1\\s+[0-9.]+)"), "\\1 'TH CN");
      editor->setPlainText("// Loop traverse - field notes\n" + text);
      window.ValidateNow();
      QCOMPARE(editor->ErrorLine(), 0);
      Action(window, "Run adjustment")->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 15000);
      const QString mode = light ? "light" : "dark";
      QVERIFY(window.grab().save(Issue912Shot(QString("qt-912-final-%1-solved.png").arg(mode))));
      editor->appendPlainText("BOGUS 1 2 3");
      editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
      window.ValidateNow();
      QVERIFY(editor->ErrorLine() > 0);
      QVERIFY(window.grab().save(Issue912Shot(QString("qt-912-final-%1-error.png").arg(mode))));
    }
    ApplyQtAppearance(*qApp, false);
  }

  void typingHistoryAndEscapeWorkFromViewport() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.show();
    auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport");
    auto* input = window.findChild<QLineEdit*>("commandInput");
    QVERIFY(viewport); QVERIFY(input);
    // A GL widget normally initialises on its first paint, but a window on an invisible desktop
    // (tools/run-qt-tests-hidden.ps1) never receives one. Rendering it directly creates the
    // context and runs initializeGL() without needing the window to be exposed.
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(static_cast<EditorViewport*>(viewport)->IsRendererReady(), 10000);
    viewport->setFocus();
    QTest::keyClicks(viewport, "l");
    QCOMPARE(input->text(), QString("l"));
    QTRY_VERIFY(window.m_CommandSuggestionModel->rowCount() > 0);
    window.m_CommandSuggestions->hide();
    input->setText("LINE");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(window.State().active, AppCommandState::Kind::Line);
    window.activateWindow(); input->setFocus();
    QTest::keyClick(input, Qt::Key_Escape);
    QTRY_COMPARE(window.State().active, AppCommandState::Kind::None);
    QTest::keyClick(input, Qt::Key_Up);
    QCOMPARE(input->text(), QString("LINE"));
  }

  void customTitleBarAndFloatingCommandLineWork() {
    QSettings settings; settings.setValue("editor/commandWidth", 400); settings.remove("editor/commandAnchor");
    const auto oldPalette = qApp->palette(); const auto oldFont = qApp->font();
    const auto oldSheet = qApp->styleSheet(); const auto oldStyle = qApp->style()->objectName();
    const auto oldDefault = qApp->property("defaultQtStyle"), oldLight = qApp->property("cadLightTheme");
    auto restoreAppearance = qScopeGuard([&] {
      qApp->setStyle(oldStyle); qApp->setPalette(oldPalette); qApp->setFont(oldFont); qApp->setStyleSheet(oldSheet);
      qApp->setProperty("defaultQtStyle", oldDefault); qApp->setProperty("cadLightTheme", oldLight);
    });
    ApplyQtAppearance(*qApp, false);
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.State().userLinesFlat = {-12.0, 0.0, 0.0, 12.0, 0.0, 0.0};
    ++window.State().cadGpuRevision;
    window.m_Viewport->InvalidateGeometry();
    window.resize(1200, 800);
    window.show(); window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(window.windowFlags() & Qt::FramelessWindowHint);
#ifdef _WIN32
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());
    const LONG_PTR nativeStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
    QVERIFY(nativeStyle & WS_THICKFRAME);
    QVERIFY(nativeStyle & WS_CAPTION);
    auto* titleForHitTest = window.findChild<QWidget*>("editorTitleBar"); QVERIFY(titleForHitTest);
    const QPoint captionPoint = titleForHitTest->mapToGlobal(QPoint(titleForHitTest->width() / 2, titleForHitTest->height() / 2));
    const qreal scale = window.devicePixelRatioF();
    QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(qRound(captionPoint.x() * scale), qRound(captionPoint.y() * scale))), static_cast<LRESULT>(HTCAPTION));
    const QPoint leftEdge = window.mapToGlobal(QPoint(0, window.height() / 2));
    QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(qRound(leftEdge.x() * scale), qRound(leftEdge.y() * scale))), static_cast<LRESULT>(HTLEFT));
    const auto hitAt = [&](QPoint local) {
      const QPoint global = window.mapToGlobal(local);
      return SendMessageW(hwnd, WM_NCHITTEST, 0,
          MAKELPARAM(qRound(global.x() * scale), qRound(global.y() * scale)));
    };
    QCOMPARE(hitAt(QPoint(0, 0)), static_cast<LRESULT>(HTTOPLEFT));
    QCOMPARE(hitAt(QPoint(window.width() - 1, 0)), static_cast<LRESULT>(HTTOPRIGHT));
    QCOMPARE(hitAt(QPoint(0, window.height() - 1)), static_cast<LRESULT>(HTBOTTOMLEFT));
    QCOMPARE(hitAt(QPoint(window.width() - 1, window.height() - 1)), static_cast<LRESULT>(HTBOTTOMRIGHT));
    QCOMPARE(hitAt(QPoint(window.width() / 2, 0)), static_cast<LRESULT>(HTTOP));
    QCOMPARE(hitAt(QPoint(window.width() / 2, window.height() - 1)), static_cast<LRESULT>(HTBOTTOM));
    QCOMPARE(hitAt(QPoint(0, window.height() / 2)), static_cast<LRESULT>(HTLEFT));
    QCOMPARE(hitAt(QPoint(window.width() - 1, window.height() / 2)), static_cast<LRESULT>(HTRIGHT));
#endif
    QVERIFY(!window.statusBar()->isSizeGripEnabled());
    auto* nativeSizeGrip = window.statusBar()->findChild<QSizeGrip*>(); QVERIFY(nativeSizeGrip);
    QTRY_VERIFY(nativeSizeGrip->isVisible());
    QVERIFY(nativeSizeGrip->width() >= 20 && nativeSizeGrip->height() >= 20);
    QVERIFY(window.grab().save(QtTestArtifactDirectory() + "/qt-1035-window-restored-resize-grip.png"));
    auto* viewport = window.m_Viewport; QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(viewport->m_Renderer->HasRenderedFrame(), 10000);
    QSize lastFrame(viewport->m_Renderer->RenderedWidth(), viewport->m_Renderer->RenderedHeight());
    const QSize startingSize = window.size();
    const QSize viewportSize = viewport->size();
#ifdef _WIN32
    SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
#else
    window.BeginInteractiveResize();
#endif
    QVERIFY(viewport->m_ResizePreview);
    auto* resizePreview = window.findChild<QLabel*>("viewportResizePreview"); QVERIFY(resizePreview);
    QCOMPARE(viewport->size(), viewportSize);
    QCOMPARE(viewport->m_Renderer->RenderedWidth(), lastFrame.width());
    QCOMPARE(viewport->m_Renderer->RenderedHeight(), lastFrame.height());
    const QSize previewParentSize = viewport->parentWidget()->size();
    for (int sample = 0; sample < 30; ++sample) {
      window.resize(startingSize + QSize(40 + (sample % 2) * 2, 32 + (sample % 2) * 2));
      QApplication::processEvents(QEventLoop::AllEvents, 10);
      QCOMPARE(viewport->size(), viewportSize);
      QCOMPARE(resizePreview->geometry(), viewport->parentWidget()->rect());
    }
    QVERIFY(resizePreview->size() != previewParentSize);
    window.resize(startingSize + QSize(40, 32));
    QApplication::processEvents();
    QVERIFY(window.grab().save(QtTestArtifactDirectory() + "/qt-1035-window-resize-preview.png"));
#ifdef _WIN32
    SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
#else
    window.EndInteractiveResize();
#endif
    QTRY_VERIFY_WITH_TIMEOUT(!viewport->m_ResizePreview, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(viewport->m_Renderer->RenderedWidth(),
        qRound(viewport->width() * viewport->devicePixelRatioF()), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(viewport->m_Renderer->RenderedHeight(),
        qRound(viewport->height() * viewport->devicePixelRatioF()), 5000);
    QVERIFY(window.width() >= startingSize.width() + 36);
    QVERIFY(window.height() >= startingSize.height() + 28);
    QVERIFY(window.grab().save(QtTestArtifactDirectory() + "/qt-1035-window-resized.png"));
    window.showMaximized(); QApplication::processEvents();
    QTRY_VERIFY(!nativeSizeGrip->isVisible());
#ifdef _WIN32
    const QPoint maximizedCaption = titleForHitTest->mapToGlobal(QPoint(titleForHitTest->width() / 2, titleForHitTest->height() / 2));
    QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(qRound(maximizedCaption.x() * scale), qRound(maximizedCaption.y() * scale))), static_cast<LRESULT>(HTCAPTION));
#endif
    QVERIFY(window.grab().save(QtTestArtifactDirectory() + "/qt-1035-window-maximized.png"));
    window.showNormal(); QApplication::processEvents();
    QTRY_VERIFY(nativeSizeGrip->isVisible());
    auto* title = window.findChild<QWidget*>("editorTitleBar"); QVERIFY(title);
    QVERIFY(title->geometry().top() >= 0);
    QVERIFY(title->geometry().bottom() < window.height());
    auto* logo = window.findChild<QToolButton*>("applicationMenu"); QVERIFY(logo); QVERIFY(logo->menu());
    QStringList categories; for (auto* action : logo->menu()->actions()) categories.append(QString(action->text()).remove('&'));
    QCOMPARE(categories, QStringList({"File", "Edit", "View", "Survey", "Help"}));
    bool opened = false;
    QTimer::singleShot(0, &window, [&] { opened = logo->menu()->isVisible(); logo->menu()->hide(); });
    logo->showMenu(); QVERIFY(opened);
    for (const auto& command : {"NEW", "OPEN", "SAVE", "UNDO", "REDO"}) {
      auto* button = window.findChild<QToolButton*>("quick_" + QString(command));
      QVERIFY(button); QVERIFY(button->defaultAction());
      QVERIFY(title->isAncestorOf(button));
    }
    QVERIFY(!window.findChild<QDockWidget*>("commandDock"));
    auto* viewportWidget = window.findChild<QOpenGLWidget*>("modelViewport"); QVERIFY(viewportWidget);
    auto* panel = dynamic_cast<FloatingCommandBar*>(window.findChild<QFrame*>("floatingCommandBar")); QVERIFY(panel);
    QCOMPARE(panel->parentWidget(), viewportWidget);
    auto* input = panel->Input();
    input->setFocus(); input->setText("LINE"); QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(window.State().active, AppCommandState::Kind::Line);
    auto* hint = panel->findChild<QLabel*>("commandHint"); QVERIFY(hint); QVERIFY(!hint->isHidden());
    // The floating command input starts LINE; use the same command submission seam for the
    // first point so this test's prompt assertion does not depend on hidden-desktop focus routing.
    window.Submit("0,0");
    QCOMPARE(window.State().linePhase, AppCommandState::LinePhase::NeedNextPoint);
    QVERIFY2(hint->text().contains("href"), qPrintable(QString("Expected a clickable azimuth option; footer was '%1', line phase %2, last command result '%3'")
        .arg(hint->text()).arg(static_cast<int>(window.State().linePhase))
        .arg(window.m_Log.empty() ? QString() : QString::fromUtf8(window.m_Log.back().c_str()))));
    QVERIFY(QMetaObject::invokeMethod(hint, "linkActivated", Qt::DirectConnection, Q_ARG(QString, "a")));
    QVERIFY(window.State().segmentAngleKeyboardAwaitBearing);
    window.CancelDraftingInput();
    window.Submit("LINE");
    window.Submit("0,0");
    window.Submit("10,0");
    window.CancelDraftingInput();
    QCOMPARE(window.State().userLinesFlat.size(), std::size_t(12));
    window.findChild<QToolButton*>("quick_UNDO")->click();
    QCOMPARE(window.State().userLinesFlat.size(), std::size_t(6));
    window.findChild<QToolButton*>("quick_REDO")->click();
    QCOMPARE(window.State().userLinesFlat.size(), std::size_t(12));
    QTest::keyClick(input, Qt::Key_F2);
    QTRY_VERIFY(panel->Transcript()->isVisible());
    QVERIFY(panel->Transcript()->toPlainText().contains("LINE", Qt::CaseInsensitive));
    QTest::keyClick(input, Qt::Key_F2); QTRY_VERIFY(panel->Transcript()->isHidden());
    QTest::keyClick(input, Qt::Key_9, Qt::ControlModifier); QTRY_VERIFY(panel->isHidden());
    QTest::keyClick(viewport, Qt::Key_9, Qt::ControlModifier); QTRY_VERIFY(panel->isVisible());
    auto drag = [&](const char* name, int dx) {
      auto* grip = panel->findChild<QWidget*>(name); QVERIFY(grip);
      const QPoint local = grip->rect().center(); const QPoint global = grip->mapToGlobal(local);
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(grip, &press);
      QMouseEvent move(QEvent::MouseMove, QPointF(local + QPoint(dx, 0)),
                       QPointF(global + QPoint(dx, 0)), Qt::NoButton, Qt::LeftButton,
                       Qt::NoModifier);
      QApplication::sendEvent(grip, &move);
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local + QPoint(dx, 0)),
                          QPointF(global + QPoint(dx, 0)), Qt::LeftButton, Qt::NoButton,
                          Qt::NoModifier);
      QApplication::sendEvent(grip, &release);
    };
    const int oldX = panel->x();
    const int moveX = oldX + panel->width() + 20 < viewport->width() ? 20 : -20;
    drag("commandDragGrip", moveX); QVERIFY(panel->x() != oldX);
    const int oldWidth = panel->width(); drag("commandResizeGrip", -40); QCOMPARE(panel->width(), oldWidth - 40);
    window.resize(950, 650); QTest::qWait(50);
    QVERIFY(viewport->rect().contains(panel->geometry()));
    QCOMPARE(viewport->height() - panel->geometry().bottom(), 13);
    auto* maximize = window.findChild<QToolButton*>("windowMaximize"); QVERIFY(maximize);
    maximize->click(); QTRY_VERIFY(window.isMaximized());
    maximize->click(); QTRY_VERIFY(!window.isMaximized());
    window.resize(1500, 950); QTest::qWait(100);
    QVERIFY(window.grab().save(Issue886Shot("qt-886-neutral-gray-app.png")));
    auto* tabs = window.findChild<QTabBar*>("drawingTabs"); QVERIFY(tabs);
    tabs->setCurrentIndex(0); QTest::qWait(100);
    QVERIFY(window.grab().save(Issue886Shot("qt-886-neutral-gray-start.png")));
    settings.remove("editor/commandWidth"); settings.remove("editor/commandAnchor");
  }

  void issue903DraftingModesAndCursorFields() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& state = window.State();
    state.objectSnapEnabled = false;
    window.Submit("LINE");
    auto* first = window.findChild<QLineEdit*>("dynamicField0");
    auto* second = window.findChild<QLineEdit*>("dynamicField1");
    QVERIFY(first && second && first->isVisible());
    first->setFocus(); first->selectAll(); QTest::keyClicks(first, "10");
    QTest::keyClick(first, Qt::Key_Tab); QVERIFY(second->hasFocus());
    QTest::keyClicks(second, "20"); QTest::keyClick(second, Qt::Key_Return);
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    QVERIFY(std::abs(state.anchorX - 10) < 1e-5); QVERIFY(std::abs(state.anchorY - 20) < 1e-5);
    first->setFocus(); first->selectAll();
    const QString relativePoint("@5,3");
    for (int i = 0; i < relativePoint.size(); ++i)
      QTest::keyClicks(qobject_cast<QLineEdit*>(QApplication::focusWidget()), relativePoint.mid(i, 1));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QCOMPARE(state.userLinesFlat.size(), size_t(6));
    QVERIFY(std::abs(state.userLinesFlat[3] - 15) < 1e-5);
    QVERIFY(std::abs(state.userLinesFlat[4] - 23) < 1e-5);
    QTest::keyClick(first, Qt::Key_F10);
    QVERIFY(state.polarMode); QVERIFY(!state.orthoMode);
    QVERIFY(window.findChild<QToolButton*>("draftPolar")->isChecked());
    QTest::keyClick(first, Qt::Key_F8);
    QVERIFY(state.orthoMode); QVERIFY(!state.polarMode);
    QVERIFY(!window.findChild<QToolButton*>("draftPolar")->isChecked());
    const bool snap3d = state.objectSnap3dEnabled;
    QTest::keyClick(first, Qt::Key_F4); QCOMPARE(state.objectSnap3dEnabled, !snap3d);
    QVERIFY(!state.objectSnapEnabled);
    QTest::keyClick(first, Qt::Key_F3); QVERIFY(state.objectSnapEnabled);
    QTest::keyClick(first, Qt::Key_Escape); QCOMPARE(state.active, AppCommandState::Kind::None);
    window.Submit("UNDO"); QVERIFY(state.userLinesFlat.empty());
    window.Submit("LINE"); first->setFocus(); first->selectAll(); QTest::keyClicks(first, "invalid");
    QTest::keyClick(first, Qt::Key_Return);
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedFirstPoint); QVERIFY(state.userLinesFlat.empty());
    window.CancelDraftingInput();
  }

  void issue1039CommandLineAndDynamicFieldsShareOneDraft() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& state = window.State();
    state.objectSnapEnabled = false;
    auto* command = window.findChild<QLineEdit*>("commandInput"); QVERIFY(command);
    auto* x = window.findChild<QLineEdit*>("dynamicField0"); QVERIFY(x);
    auto* y = window.findChild<QLineEdit*>("dynamicField1"); QVERIFY(y);

    // A command-line point answer is parsed into the same coordinate fields and Enter advances once.
    window.Submit("LINE");
    command->setFocus(); QTest::keyClicks(command, "12,34");
    QTRY_COMPARE(x->text(), QString("12"));
    QTRY_COMPARE(y->text(), QString("34"));
    QCOMPARE(command->text(), QString("12,34"));
    QVERIFY(window.m_DynamicInput->isVisible());
    QVERIFY(state.activeDrawingIdx > 0);
    QCOMPARE(window.DraftingInputText(), QString("12,34"));
    QVERIFY(window.grab().save(Issue886Shot("issue1039/point-commandline-draft.png")));
    QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(window.m_CommandHistory.last(), QString("12,34"));
    QVERIFY(!state.cmdEnteredHistory.empty());
    QCOMPARE(QString::fromStdString(state.cmdEnteredHistory.back()), QString("12,34"));
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    QVERIFY(std::abs(state.anchorX - 12) < 1e-5);
    QVERIFY(std::abs(state.anchorY - 34) < 1e-5);

    // A polar dynamic-input pair is shown in the command line using the same entered distance
    // and angle, while submission still composes the equivalent point for existing command parsing.
    x->setFocus(); x->selectAll(); QTest::keyClicks(x, "40");
    y->setFocus(); y->selectAll(); QTest::keyClicks(y, "45");
    QTRY_COMPARE(command->text(), QString("@40<45"));
    QCOMPARE(window.DraftingInputText(), QString("@28.284271,28.284271"));
    QVERIFY(window.grab().save(Issue886Shot("issue1039/polar-draft-shared.png")));
    command->setFocus(); QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedNextPoint);

    // A dynamic-field edit is reflected by the command line, whose Enter submits that same draft.
    x->setFocus(); x->selectAll(); QTest::keyClicks(x, "5");
    QTRY_VERIFY(!command->text().isEmpty());
    QVERIFY(window.grab().save(Issue886Shot("issue1039/point-dynamic-field-draft.png")));
    command->setFocus(); QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(state.active, AppCommandState::Kind::Line);
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    QCOMPARE(state.userLinesFlat.size(), std::size_t(12));

    // Enter in the dynamic field also submits once and keeps the LINE prompt active.
    x->setFocus(); x->selectAll(); QTest::keyClicks(x, "5");
    QTest::keyClick(x, Qt::Key_Return);
    QCOMPARE(state.active, AppCommandState::Kind::Line);
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    QCOMPARE(state.userLinesFlat.size(), std::size_t(18));

    // Cancel clears an unsubmitted draft; a later command starts with its own empty prompt state.
    command->setFocus(); QTest::keyClicks(command, "99,99");
    QTest::keyClick(command, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(command->text(), QString());
    command->setFocus(); command->setText("CIRCLE"); QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(state.active, AppCommandState::Kind::Circle);
    command->setFocus(); QTest::keyClicks(command, "0,0"); QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(state.circlePhase, AppCommandState::CirclePhase::WaitRadius);
    auto* radius = window.findChild<QLineEdit*>("dynamicField0"); QVERIFY(radius);
    command->setFocus(); QTest::keyClicks(command, "5");
    QTRY_COMPARE(radius->text(), QString("5"));
    QVERIFY(window.grab().save(Issue886Shot("issue1039/radius-commandline-draft.png")));
    radius->setFocus(); radius->selectAll(); QTest::keyClicks(radius, "6");
    QTRY_COMPARE(command->text(), QString("6"));
    QTest::keyClick(radius, Qt::Key_Return);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.userCirclesCxCyZR.size(), std::size_t(4));
    window.CancelDraftingInput();
  }

  void issue1040QtCommandFinderFuzzyKeyboardAndMouseSelection() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    auto* command = window.findChild<QLineEdit*>("commandInput"); QVERIFY(command);
    auto* popup = window.m_CommandSuggestions; QVERIFY(popup);
    auto* model = window.m_CommandSuggestionModel; QVERIFY(model);
    auto& state = window.State();

    // Real typing goes to the command field for the first character, then must keep going there
    // even after suggestions appear instead of being captured by a popup keyboard grab.
    command->setFocus(); QTest::keyClick(command, Qt::Key_T);
    QTRY_VERIFY(popup->isVisible());
    QWidget* typingTarget = QApplication::activePopupWidget();
    if (!typingTarget) typingTarget = QApplication::focusWidget();
    QVERIFY(typingTarget);
    QTest::keyClicks(typingTarget, "raverse");
    QCOMPARE(command->text(), QString("traverse"));
    QVERIFY(!FuzzyCommandSuggestions(command->text().toStdString(), 30).empty());
    QCOMPARE(QString::fromStdString(FuzzyCommandSuggestions(command->text().toStdString(), 30).front().name),
             QString("TRAVERSE"));
    QPixmap traverseAndSuggestions = window.grab();
    {
      QPainter painter(&traverseAndSuggestions);
      const QPoint popupPosition = window.mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
      painter.drawPixmap(popupPosition, popup->grab());
    }
    QVERIFY(traverseAndSuggestions.save(Issue886Shot("issue1040/traverse-query-kept.png")));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
    QVERIFY(!popup->isVisible());
    popup->hide(); command->clear();

    // Subsequence and prefix queries both rank LINE first, and rows carry icons.
    const auto typoSuggestions = FuzzyCommandSuggestions("lne", 30);
    QVERIFY(!typoSuggestions.empty());
    QCOMPARE(QString::fromStdString(typoSuggestions.front().name), QString("LINE"));
    const auto aliasSuggestions = FuzzyCommandSuggestions("l", 1);
    QVERIFY(!aliasSuggestions.empty());
    QCOMPARE(QString::fromStdString(aliasSuggestions.front().name), QString("LINE"));
    QVERIFY(aliasSuggestions.front().exactMatch);
    const auto prefixSuggestions = FuzzyCommandSuggestions("lin", 1);
    QVERIFY(!prefixSuggestions.empty());
    QVERIFY(!prefixSuggestions.front().exactMatch);
    QVERIFY(!window.m_DynamicInput->isVisible());
    command->setFocus(); QTest::keyClicks(command, "lne");
    QCOMPARE(command->text(), QString("lne"));
    QVERIFY(model->rowCount() > 0);
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(popup->currentIndex().data().toString(), QString("LINE"));
    QTest::keyClick(popup, Qt::Key_Down);
    QCOMPARE(popup->currentIndex().data().toString(), QString("MKLINE"));
    QTest::keyClick(popup, Qt::Key_Up);
    QCOMPARE(popup->currentIndex().data().toString(), QString("LINE"));
    const auto firstIcon = model->index(0, 0).data(Qt::DecorationRole);
    QVERIFY(!qvariant_cast<QIcon>(firstIcon).isNull());
    QVERIFY(popup->styleSheet().contains("border-radius"));
    QPixmap editorAndPopup = window.grab();
    {
      QPainter painter(&editorAndPopup);
      const QPoint popupPosition = window.mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
      painter.drawPixmap(popupPosition, popup->grab());
    }
    QVERIFY(editorAndPopup.save(Issue886Shot("issue1040/command-finder-fuzzy-results.png")));
    // A visible Qt::Popup takes the keyboard grab on the real UI path. Deliver Enter through
    // the popup itself as well as the line edit so this covers the native popup event route.
    QTest::keyClick(popup, Qt::Key_Return);
    QVERIFY(!window.m_CommandHistory.isEmpty());
    QCOMPARE(window.m_CommandHistory.last(), QString("LINE"));
    QCOMPARE(state.active, AppCommandState::Kind::Line);

    // A normal prefix query also ranks LINE first; Escape dismisses the finder without launching it.
    QTest::keyClick(command, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    command->setFocus(); QTest::keyClicks(command, "lin");
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(popup->currentIndex().data().toString(), QString("LINE"));
    QTest::keyClick(command, Qt::Key_Escape);
    QVERIFY(!popup->isVisible());
    QCOMPARE(state.active, AppCommandState::Kind::None);

    // A query with no ranked result hides the prior highlight; Enter submits the typed text unchanged.
    command->setFocus(); QTest::keyClicks(command, "zzzz");
    QVERIFY(!popup->isVisible());
    QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(window.m_CommandHistory.last(), QString("zzzz"));

    // The registered `l` alias ranks LINE first; scrolling and clicking launch it.
    command->setFocus(); QTest::keyClicks(command, "l");
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(popup->currentIndex().data().toString(), QString("LINE"));
    auto* scroll = popup->verticalScrollBar(); QVERIFY(scroll);
    QVERIFY(scroll->maximum() > 0);
    const int beforeScroll = scroll->value();
    QWheelEvent down(QPointF(popup->viewport()->rect().center()),
                     popup->viewport()->mapToGlobal(popup->viewport()->rect().center()), {}, QPoint(0, -240),
                     Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(popup->viewport(), &down);
    QVERIFY(scroll->value() > beforeScroll);
    const int afterScroll = scroll->value();
    QWheelEvent up(QPointF(popup->viewport()->rect().center()),
                   popup->viewport()->mapToGlobal(popup->viewport()->rect().center()), {}, QPoint(0, 240),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(popup->viewport(), &up);
    QVERIFY(scroll->value() < afterScroll);
    const QModelIndex line = model->index(0, 0);
    QCOMPARE(line.data().toString(), QString("LINE"));
    QTest::mouseClick(popup->viewport(), Qt::LeftButton, Qt::NoModifier, popup->visualRect(line).center());
    QTRY_COMPARE(state.active, AppCommandState::Kind::Line);
    QCOMPARE(window.m_CommandHistory.last(), QString("LINE"));
    window.CancelDraftingInput();

    // Empty input and an active drafting prompt must not leave a stale command result available.
    command->clear();
    QVERIFY(!popup->isVisible());
    window.Submit("LINE");
    command->setFocus(); QTest::keyClicks(command, "lne");
    QVERIFY(window.m_DynamicInput->isVisible());
    QVERIFY(!popup->isVisible());
    QTest::keyClick(command, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
  }

  void issue1081QtExactOpenWinsOverOpenPackSuggestion() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    auto* command = window.findChild<QLineEdit*>("commandInput"); QVERIFY(command);
    auto* popup = window.m_CommandSuggestions; QVERIFY(popup);
    auto* model = window.m_CommandSuggestionModel; QVERIFY(model);

    command->setFocus(); QTest::keyClicks(command, "OPEN");
    QTRY_VERIFY(popup->isVisible());
    const auto openSuggestions = FuzzyCommandSuggestions("OPEN", 1);
    QCOMPARE(QString::fromStdString(openSuggestions.front().name), QString("OPENPACK"));
    QVERIFY(!openSuggestions.front().exactMatch);
    QCOMPARE(model->index(0, 0).data().toString(), QString("OPENPACK"));
    QModelIndex packRow;
    for (int row = 0; row < model->rowCount(); ++row) {
      if (model->index(row, 0).data().toString() == QString("OPENPACK")) {
        packRow = model->index(row, 0);
        break;
      }
    }
    QVERIFY(packRow.isValid());
    popup->setCurrentIndex(packRow);

    QString dialogTitle;
    QStringList dialogFilters;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialogTitle = dialog->windowTitle();
      dialogFilters = dialog->nameFilters();
      dialog->reject();
    });
    QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(dialogTitle, QString("Open drawing"));
    QVERIFY(dialogFilters.contains(QString("Drawing (*.dwg)")));

    command->setFocus(); QTest::keyClicks(command, "LINE");
    QTRY_VERIFY(popup->isVisible());
    QModelIndex otherCommand;
    for (int row = 0; row < model->rowCount(); ++row) {
      if (model->index(row, 0).data().toString() == QString("MKLINE")) {
        otherCommand = model->index(row, 0);
        break;
      }
    }
    QVERIFY(otherCommand.isValid());
    popup->setCurrentIndex(otherCommand);
    QTest::keyClick(command, Qt::Key_Return);
    QCOMPARE(window.State().active, AppCommandState::Kind::Line);
    window.CancelDraftingInput();
  }

private:
  // GitHub #1024: helpers for the Qt zoom tests. Friends of EditorWindow, so they can reach its log
  // and cancel path; a free function could not.
  static constexpr std::size_t kFlatLineStride = 6;  // x0 y0 z0 x1 y1 z1
  static constexpr double kExtentsMargin = 0.08;      // REQ-122: the binding axis leaves 8% free
  static constexpr double kFitMin = 0.98, kFitMax = 1.001;  // "framed": the binding axis is within 2% of the margin
  static constexpr double kParkedPanX = -5000.0, kParkedPanY = 7000.0;  // a camera nothing could frame
  static constexpr double kLineW = 400.0, kLineH = 200.0;               // the line DrawLine("0,0", "400,200") makes
  static constexpr double kThinLineScale = 13.587;  // req123-viewport-zoom-extents.txt: 2 * (100 / 3.68) / 4 in

  static void DrawLine(EditorWindow& window, const char* from, const char* to) {
    for (const char* text : {"LINE", from, to}) window.Submit(text);
    window.CancelDraftingInput();
  }
  static void DrawWideLine(EditorWindow& window) { DrawLine(window, "0,0", "400,200"); }
  static void ParkCamera(AppCommandState& st) {
    st.viewportPanX = kParkedPanX; st.viewportPanY = kParkedPanY; st.viewportZoom = 1.f;
  }
  static bool LogHas(const std::vector<std::string>& log, const char* text) {
    return std::any_of(log.begin(), log.end(), [&](const std::string& e) { return e.find(text) != std::string::npos; });
  }
  // 1.0 means the box exactly fills the view minus the margin on its binding axis; > 1 clips it and
  // a value well under 1 means the view is wider than REQ-122 asks. Independent of the framing code.
  static double FrameFit(const AppCommandState& st, const QWidget& view, double spanX, double spanY) {
    const Camera camera = CadViewCamera(st);
    const double halfH = camera.orthoHalfH;
    const double halfW = halfH * view.width() / view.height();
    return std::max(0.5 * spanX / (halfW * (1.0 - kExtentsMargin)), 0.5 * spanY / (halfH * (1.0 - kExtentsMargin)));
  }
  static bool IsFramed(const AppCommandState& st, const QWidget& view, double spanX, double spanY) {
    const double fit = FrameFit(st, view, spanX, spanY);
    return fit > kFitMin && fit <= kFitMax;
  }
  static EditorViewport* PaintedViewport(EditorWindow& window) {
    window.m_Viewport->grabFramebuffer();
    return window.m_Viewport;
  }

private Q_SLOTS:
  // GitHub #1024 / REQ-120 / REQ-122: ZOOMEXTENTS only sets a pending flag, so the Qt viewport has to
  // consume it when it paints. The headless transcripts call the zoom function directly and cannot
  // see this; only the real viewport can. Wide, tall and ultra-wide windows move the binding axis.
  void issue1024QtZoomExtentsFramesTheDrawing() {
    for (const QSize size : {QSize(1280, 800), QSize(600, 900), QSize(1600, 500)}) {
      EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(size); window.show();
      QVERIFY(QTest::qWaitForWindowActive(&window));
      auto& st = window.State();
      DrawWideLine(window);
      QVERIFY(st.userLinesFlat.size() >= kFlatLineStride);
      const std::size_t o = st.userLinesFlat.size() - kFlatLineStride;
      const double cx = 0.5 * (st.userLinesFlat[o] + st.userLinesFlat[o + 3]);
      const double cy = 0.5 * (st.userLinesFlat[o + 1] + st.userLinesFlat[o + 4]);

      ParkCamera(st);
      window.Submit("ZOOMEXTENTS");
      QTRY_VERIFY(!st.pendingZoomExtents);
      QVERIFY(std::abs(st.viewportPanX - cx) < 1.0);
      QVERIFY(std::abs(st.viewportPanY - cy) < 1.0);
      QVERIFY(LogHas(window.m_Log, "Zoom extents applied"));
      // The whole line fits, and the axis that binds leaves 8% free (REQ-122).
      QVERIFY2(IsFramed(st, *window.m_Viewport, kLineW, kLineH),
               qPrintable(QString("window %1x%2").arg(size.width()).arg(size.height())));
    }
  }

  // REQ-122: an orbited camera frames the model's 3D box by its silhouette, not the plan rectangle.
  void issue1024QtZoomExtentsFramesTheDrawingWhenOrbited() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    DrawWideLine(window);
    PaintedViewport(window)->SetOrientation(30.f, 40.f);
    ParkCamera(st);
    window.Submit("ZOOMEXTENTS");
    QTRY_VERIFY(!st.pendingZoomExtents);
    QVERIFY(LogHas(window.m_Log, "Zoom extents applied (3D)"));
    QVERIFY(std::abs(st.viewportPanX - kLineW / 2) < 1.0);
    QVERIFY(std::abs(st.viewportPanY - kLineH / 2) < 1.0);
    QVERIFY(std::abs(st.viewportPanZ) < 1.0);
    QVERIFY(st.viewportZoom > 0.f && st.viewportZoom != 1.f);
  }

  // REQ-120: the middle double-click is the same operation as the typed command, is transparent
  // mid-command, and works over the ViewCube too (a middle press passes through the cube).
  void issue1024QtMiddleDoubleClickZoomsExtentsMidCommand() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    DrawWideLine(window);
    window.Submit("LINE"); window.Submit("10,10");
    QCOMPARE(st.active, AppCommandState::Kind::Line);
    QCOMPARE(st.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    const std::size_t lines = st.userLinesFlat.size();

    // The typed route stays refused mid-command; nothing about it is relaxed. (REQ-120 quotes the
    // older "Could not parse point" wording; today the LINE prompt names itself, same refusal.)
    ParkCamera(st);
    window.Submit("ZOOMEXTENTS");
    QApplication::processEvents(); QTest::qWait(50);
    QVERIFY(LogHas(window.m_Log, "still running"));
    QVERIFY(!st.pendingZoomExtents);
    QCOMPARE(st.viewportPanX, kParkedPanX);

    // Off the cube, and on its centre, home button and bottom-right corner.
    const viewcube::Scene scene = ViewCubeSceneOf(window.m_Viewport, st);
    const QPoint cubeCorner(qRound(scene.originX + scene.sizePx - 3), qRound(scene.originY + scene.sizePx - 3));
    for (const QPoint& at : {QPoint(300, 300), Px(scene.center), Px(scene.home), cubeCorner}) {
      ParkCamera(st);
      QTest::mouseDClick(window.m_Viewport, Qt::MiddleButton, Qt::NoModifier, at);
      QTRY_VERIFY2(std::abs(st.viewportPanX - kLineW / 2) < 1.0, qPrintable(QString("%1,%2").arg(at.x()).arg(at.y())));
      QVERIFY(std::abs(st.viewportPanY - kLineH / 2) < 1.0);
      QVERIFY(IsFramed(st, *window.m_Viewport, kLineW, kLineH));
    }
    // The LINE in progress kept its first point: the next point completes a segment from (10,10).
    QCOMPARE(st.active, AppCommandState::Kind::Line);
    QCOMPARE(st.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    window.Submit("50,60");
    QCOMPARE(st.userLinesFlat.size(), lines + kFlatLineStride);
    QCOMPARE(st.userLinesFlat[lines], 10.0);
    QCOMPARE(st.userLinesFlat[lines + 1], 10.0);
    QCOMPARE(st.userLinesFlat[lines + 3], 50.0);
    QCOMPARE(st.userLinesFlat[lines + 4], 60.0);
    window.CancelDraftingInput();
  }

  // REQ-122: a queued ZOOM WINDOW is applied by the viewport too. Its pending request was stranded
  // the same way. The request is queued directly: the Qt viewport never starts ZOOM WINDOW's first
  // corner from a click (a separate defect, #1100), so no click can queue it yet.
  void issue1024QtQueuedZoomWindowFramesTheRectangle() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    ParkCamera(st);
    st.pendingZoomMnX = 100.f; st.pendingZoomMxX = 300.f;
    st.pendingZoomMnY = 50.f; st.pendingZoomMxY = 150.f;
    st.pendingZoomWindow = true;
    window.m_Viewport->update();
    QTRY_VERIFY(!st.pendingZoomWindow);
    QVERIFY(LogHas(window.m_Log, "Zoom window applied"));
    QVERIFY(std::abs(st.viewportPanX - 200.0) < 0.5);
    QVERIFY(std::abs(st.viewportPanY - 100.0) < 0.5);
    QVERIFY(IsFramed(st, *window.m_Viewport, 200.0, 100.0));
  }

  // Issue #1100: the first Qt click starts the existing Zoom selection box; the second queues and
  // applies that rectangle through the same viewport path as the ImGui frontend.
  void issue1100QtZoomWindowClicksBothCorners() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    DrawWideLine(window);
    ParkCamera(st);
    window.Submit("ZW");
    QCOMPARE(st.active, AppCommandState::Kind::Zoom);
    QVERIFY(!st.selBoxWaitingSecond);

    st.objectSnapEnabled = true;
    const Camera camera = CadViewCamera(st);
    float firstX = 0.f, firstY = 0.f, secondX = 0.f, secondY = 0.f;
    camera.WorldToScreen(0.f, 0.f, 0.f, static_cast<float>(window.m_Viewport->width()),
        static_cast<float>(window.m_Viewport->height()), &firstX, &firstY);
    camera.WorldToScreen(400.f, 200.f, 0.f, static_cast<float>(window.m_Viewport->width()),
        static_cast<float>(window.m_Viewport->height()), &secondX, &secondY);
    const QPoint first(qRound(firstX) + 2, qRound(firstY) + 2);
    const QPoint second(qRound(secondX) + 2, qRound(secondY) + 2);
    ray3d::Vec3 rawFirst, rawSecond;
    QVERIFY(window.m_Viewport->WorldPoint(first, rawFirst));
    QVERIFY(window.m_Viewport->WorldPoint(second, rawSecond));
    QTest::mouseMove(window.m_Viewport, first);
    QTest::mouseClick(window.m_Viewport, Qt::LeftButton, Qt::NoModifier, first);
    QVERIFY(st.viewportSnapPickValid);
    QVERIFY(st.selBoxWaitingSecond);
    QCOMPARE(st.active, AppCommandState::Kind::Zoom);
    QVERIFY(std::abs(st.selBoxAnchorX - rawFirst.x) < 0.01);
    QVERIFY(std::abs(st.selBoxAnchorY - rawFirst.y) < 0.01);
    QCOMPARE(st.selBoxAnchorScreenX, static_cast<float>(first.x()));
    QCOMPARE(st.selBoxAnchorScreenY, static_cast<float>(first.y()));
    QVERIFY(!st.pendingZoomWindow);

    QTest::mouseMove(window.m_Viewport, second);
    QTest::mouseClick(window.m_Viewport, Qt::LeftButton, Qt::NoModifier, second);
    QVERIFY(st.viewportSnapPickValid);
    QVERIFY(std::abs(st.pendingZoomMnX - std::min(rawFirst.x, rawSecond.x)) < 0.01);
    QVERIFY(std::abs(st.pendingZoomMxX - std::max(rawFirst.x, rawSecond.x)) < 0.01);
    QVERIFY(std::abs(st.pendingZoomMnY - std::min(rawFirst.y, rawSecond.y)) < 0.01);
    QVERIFY(std::abs(st.pendingZoomMxY - std::max(rawFirst.y, rawSecond.y)) < 0.01);
    QTRY_VERIFY(!st.pendingZoomWindow);
    QVERIFY(LogHas(window.m_Log, "Zoom window applied"));
    QCOMPARE(st.active, AppCommandState::Kind::None);
    QVERIFY(IsFramed(st, *window.m_Viewport,
        std::abs(st.pendingZoomMxX - st.pendingZoomMnX),
        std::abs(st.pendingZoomMxY - st.pendingZoomMnY)));
  }

  // REQ-201: with nothing to frame, say so and leave the view alone.
  void issue1024QtZoomExtentsOnAnEmptyDrawingSaysWhyAndKeepsTheView() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    QVERIFY(st.userLinesFlat.empty());
    st.viewportPanX = 123.0; st.viewportPanY = 456.0; st.viewportZoom = 2.f;
    window.m_Log.clear();
    window.Submit("ZOOMEXTENTS");
    QTRY_VERIFY(!st.pendingZoomExtents);
    QCOMPARE(st.viewportPanX, 123.0);
    QCOMPARE(st.viewportPanY, 456.0);
    QCOMPARE(st.viewportZoom, 2.f);
    QVERIFY(LogHas(window.m_Log, "nothing to frame"));
  }

  // REQ-120: in paper space the gesture frames the SHEET, not the paper entities on it.
  void issue1024QtZoomExtentsInPaperSpaceFramesTheSheet() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    DrawWideLine(window);
    const int layout = AddPaperLayout(st);
    SetActiveSpace(st, layout);
    window.Refresh();
    const double sheetW = st.paperLayouts[static_cast<std::size_t>(layout)].sheetWidthIn();
    const double sheetH = st.paperLayouts[static_cast<std::size_t>(layout)].sheetHeightIn();
    ParkCamera(st);
    window.Submit("ZOOMEXTENTS");
    QTRY_VERIFY(!st.pendingZoomExtents);
    QVERIFY(std::abs(st.viewportPanX - 0.5 * sheetW) < 0.01);
    QVERIFY(std::abs(st.viewportPanY - 0.5 * sheetH) < 0.01);
    QVERIFY(IsFramed(st, *window.m_Viewport, sheetW, sheetH));
  }

  // REQ-123: inside an activated viewport the model is framed into the VIEWPORT's own rectangle, and
  // with the zoom lock on the sheet is framed instead.
  void issue1024QtZoomExtentsInAFloatingViewportFramesTheViewport() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    DrawLine(window, "0,0", "100,20");
    const int layout = AddPaperLayout(st);
    SetActiveSpace(st, layout);
    const int vpIndex = AddViewportRect(st, layout, 1.f, 1.f, 9.f, 5.f);  // 8 in x 4 in: aspect 2
    EnterFloatingModelSpace(st, layout, vpIndex, window.m_Log);
    QVERIFY(InFloatingModelSpace(st));
    window.Refresh();
    auto& vp = st.paperLayouts[static_cast<std::size_t>(layout)].viewports[static_cast<std::size_t>(vpIndex)];
    vp.modelCenterX = -900.0; vp.modelCenterY = 900.0; vp.scaleModelPerPaperIn = 1.f;
    window.Submit("ZOOMEXTENTS");
    QTRY_VERIFY(!st.pendingZoomExtents);
    QVERIFY(std::abs(vp.modelCenterX - 50.0) < 0.01);
    QVERIFY(std::abs(vp.modelCenterY - 10.0) < 0.01);
    QVERIFY(std::abs(vp.scaleModelPerPaperIn - kThinLineScale) < 0.01);

    st.viewportZoomLocked = true;
    ParkCamera(st);
    window.Submit("ZOOMEXTENTS");
    QTRY_VERIFY(!st.pendingZoomExtents);
    QVERIFY(std::abs(vp.scaleModelPerPaperIn - kThinLineScale) < 0.01);  // the viewport was left alone
    const auto& sheet = st.paperLayouts[static_cast<std::size_t>(layout)];
    QVERIFY(std::abs(st.viewportPanX - 0.5 * sheet.sheetWidthIn()) < 0.01);
    QVERIFY(std::abs(st.viewportPanY - 0.5 * sheet.sheetHeightIn()) < 0.01);
  }

  // Entering BEDIT queues a zoom extents of the block's own content; it was dead in Qt until the
  // viewport consumed the request, so the editor now opens framed on the block.
  void issue1024QtBlockEditFramesTheBlockContent() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    CadBlockDefinition block;
    block.name = "Issue1024Block";
    block.content.lines = {0.f, 0.f, 0.f, static_cast<float>(kLineW), static_cast<float>(kLineH), 0.f};
    block.content.lineAttrs.resize(1);
    block.content.lineVis.resize(1);
    st.blockDefs.push_back(block);
    ParkCamera(st);
    window.Submit("BEDIT Issue1024Block");
    QTRY_VERIFY(!st.pendingZoomExtents);
    QVERIFY(st.blockEditActive);
    QVERIFY(LogHas(window.m_Log, "Zoom extents applied"));
    QVERIFY(IsFramed(st, *window.m_Viewport, kLineW, kLineH));
  }

  // Imports fit the drawing to the size the viewport last painted at (DxfIo/LibreDwgCad read it);
  // before the Qt viewport published it they always fit to a stale 900 x 650.
  void issue1024QtViewportPublishesItsPixelSizeAndImportsFitToIt() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString path = directory.filePath("wide.dxf");
    {
      EditorWindow source; source.CreateDrawingFromTemplate(); source.resize(1280, 800); source.show();
      QVERIFY(QTest::qWaitForWindowActive(&source));
      DrawWideLine(source);
      std::vector<std::string> log;
      QVERIFY(ExportDxfFile(source.State(), path.toUtf8().constData(), log));
    }
    // Aspect 3.2: the wide line is height-bound here but width-bound at the stale 900 x 650 (1.38).
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1600, 500); window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto& st = window.State();
    PaintedViewport(window);
    QCOMPARE(st.viewportLastFbW, qRound(window.m_Viewport->width() * window.m_Viewport->devicePixelRatioF()));
    QCOMPARE(st.viewportLastFbH, qRound(window.m_Viewport->height() * window.m_Viewport->devicePixelRatioF()));
    std::vector<std::string> log;
    QVERIFY(ImportDxfFile(st, path.toUtf8().constData(), log));
    QVERIFY(IsFramed(st, *window.m_Viewport, kLineW, kLineH));
  }

  void issue1041EveryRegisteredCommandHasAnAssociatedFinderIcon() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    auto* command = window.findChild<QLineEdit*>("commandInput"); QVERIFY(command);
    auto* popup = window.m_CommandSuggestions; QVERIFY(popup);
    auto* model = window.m_CommandSuggestionModel; QVERIFY(model);
    const QImage generic = window.style()->standardIcon(QStyle::SP_CommandLink).pixmap(18, 18).toImage();

    for (const auto& primary : RegisteredCommandPrimariesForTest()) {
      const QString query = QString::fromStdString(primary);
      command->setText(query);
      QVERIFY(QMetaObject::invokeMethod(command, "textEdited", Qt::DirectConnection,
                                        Q_ARG(QString, query)));
      QCoreApplication::processEvents();
      QCOMPARE(model->rowCount() > 0, true);
      QCOMPARE(model->index(0, 0).data().toString(), query.toUpper());
      const QIcon icon = qvariant_cast<QIcon>(model->index(0, 0).data(Qt::DecorationRole));
      QVERIFY2(!icon.isNull(), primary.c_str());
      QVERIFY2(icon.pixmap(18, 18).toImage() != generic, primary.c_str());
    }

    command->setText("massprop");
    QVERIFY(QMetaObject::invokeMethod(command, "textEdited", Qt::DirectConnection,
                                      Q_ARG(QString, QString("massprop"))));
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(popup->currentIndex().data().toString(), QString("MASSPROP"));
    QPixmap generatedIconState = window.grab();
    {
      QPainter painter(&generatedIconState);
      const QPoint popupPosition = window.mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
      painter.drawPixmap(popupPosition, popup->grab());
    }
    QVERIFY(generatedIconState.save(Issue886Shot("issue1041/generated-massprop-icon.png")));
    QVERIFY(popup->grab().save(Issue886Shot("issue1041/generated-massprop-popup.png")));

    command->setText("l");
    QVERIFY(QMetaObject::invokeMethod(command, "textEdited", Qt::DirectConnection,
                                      Q_ARG(QString, QString("l"))));
    QTRY_VERIFY(popup->isVisible());
    QPixmap mixedIconState = window.grab();
    {
      QPainter painter(&mixedIconState);
      const QPoint popupPosition = window.mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
      painter.drawPixmap(popupPosition, popup->grab());
    }
    QVERIFY(mixedIconState.save(Issue886Shot("issue1041/mixed-command-icons.png")));
    QVERIFY(popup->grab().save(Issue886Shot("issue1041/mixed-command-icons-popup.png")));
  }

  void issue919TypedDraftingInAReadOnlyProjectDrawingCannotBeSaved() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show();
    auto& state = window.State();
    constexpr std::uint32_t projectUid = 919;
    state.drawingTabs[static_cast<std::size_t>(state.activeDrawingIdx)].projectUid = projectUid;
    AppCommandState::ProjectSession session;
    session.uid = projectUid;
    session.readOnly = true;
    state.openProjects.push_back(session);
    window.Submit("LINE"); window.Submit("0,0"); window.Submit("10,0"); window.Submit("");
    QCOMPARE(state.userLinesFlat.size(), std::size_t(6));
    QTimer::singleShot(0, &window, [&] {
      auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(warning);
      QVERIFY(warning->text().contains("read-only"));
      warning->accept();
    });
    QVERIFY(!window.SaveDrawing());
    QCOMPARE(state.userLinesFlat.size(), std::size_t(6));   // the typed work is kept, just not written
    QVERIFY(DoUndo(state, window.m_Log));
    QVERIFY(state.userLinesFlat.empty());
  }

  void issue903PolarSettingsAndOneShotSnap() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show();
    auto& state = window.State(); const double original = state.polarIncrementDeg;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("polarSettingsDialog"); QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("polarExtraAngles")->setText("bad, 45");
      auto* buttons = dialog->findChild<QDialogButtonBox*>(); buttons->button(QDialogButtonBox::Ok)->click();
      QVERIFY(dialog->isVisible()); QVERIFY(!dialog->findChild<QLabel*>("polarSettingsError")->text().isEmpty());
      QCOMPARE(state.polarIncrementDeg, original);
      QVERIFY(dialog->grab().save(Issue886Shot("issue903/polar-settings-validation.png")));
      dialog->reject();
    });
    window.ShowPolarSettings(); QCOMPARE(state.polarIncrementDeg, original);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("polarSettingsDialog"); QVERIFY(dialog);
      dialog->findChild<QComboBox*>("polarIncrement")->setCurrentIndex(
          dialog->findChild<QComboBox*>("polarIncrement")->findData(45.0));
      dialog->findChild<QLineEdit*>("polarExtraAngles")->setText("12.5, 67.5");
      dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    window.ShowPolarSettings();
    QCOMPARE(state.polarIncrementDeg, 45.0); QCOMPARE(state.polarExtraAnglesDeg, (std::vector<double>{12.5, 67.5}));
    {
      QTemporaryFile blocker; QVERIFY(blocker.open());
      IsolatedUserData unwritable(blocker.fileName());
      QTimer::singleShot(0, [&] {
        auto* dialog = window.findChild<QDialog*>("polarSettingsDialog"); QVERIFY(dialog);
        const int initialHeight = dialog->height();
        dialog->findChild<QComboBox*>("polarIncrement")->setCurrentIndex(
            dialog->findChild<QComboBox*>("polarIncrement")->findData(30.0));
        dialog->findChild<QLineEdit*>("polarExtraAngles")->setText("4.5");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->findChild<QLabel*>("polarSettingsError")->text().isEmpty());
        QVERIFY(dialog->height() > initialHeight);
        QCOMPARE(state.polarIncrementDeg, 45.0);
        QCOMPARE(state.polarExtraAnglesDeg, (std::vector<double>{12.5, 67.5}));
        QVERIFY(dialog->grab().save(Issue886Shot("issue903/polar-settings-save-error.png")));
        dialog->reject();
      });
      window.ShowPolarSettings();
    }
    window.Submit("LINE");
    QTimer::singleShot(0, [&] {
      auto* menu = window.findChild<QMenu*>("snapOverrideMenu"); QVERIFY(menu);
      QVERIFY(!Action(*menu, "Perpendicular"));
      auto* midpoint = Action(*menu, "Midpoint"); QVERIFY(midpoint); midpoint->trigger(); menu->close();
    });
    window.ShowSnapOverride(window.m_Viewport->mapToGlobal(QPoint(150, 100)));
    QVERIFY(state.objectSnapKindOverrideValid);
    QCOMPARE(state.objectSnapKindOverrideKind, static_cast<int>(CadSnap::Kind::Midpoint));
    window.CancelDraftingInput(); QVERIFY(!state.objectSnapKindOverrideValid);
  }

  void issue903SnapOverrideAppliesToViewportPick() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1200, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    QTRY_VERIFY(window.m_Viewport->IsRendererReady());
    auto& state = window.State();
    state.userLinesFlat = {0, 0, 0, 100, 0, 0};
    state.objectSnapEnabled = false;
    state.objectSnapMidpoint = false;
    window.Submit("LINE");
    QTimer::singleShot(0, [&] {
      auto* menu = window.findChild<QMenu*>("snapOverrideMenu"); QVERIFY(menu);
      auto* midpoint = Action(*menu, "Midpoint"); QVERIFY(midpoint);
      midpoint->trigger(); menu->close();
    });
    window.ShowSnapOverride(window.m_Viewport->mapToGlobal(QPoint(180, 120)));
    QVERIFY(state.objectSnapKindOverrideValid);
    QCOMPARE(state.objectSnapKindOverrideKind, static_cast<int>(CadSnap::Kind::Midpoint));

    const auto camera = CadViewCamera(state);
    float px = 0.f, py = 0.f;
    camera.WorldToScreen(50, 0, 0, static_cast<float>(window.m_Viewport->width()),
        static_cast<float>(window.m_Viewport->height()), &px, &py);
    const QPoint pick(qRound(px), qRound(py));
    QMouseEvent move(QEvent::MouseMove, pick, window.m_Viewport->mapToGlobal(pick),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window.m_Viewport, &move);
    QVERIFY(state.viewportSnapPickValid);
    QCOMPARE(state.viewportSnapPickKind, static_cast<int>(CadSnap::Kind::Midpoint));
    QMouseEvent press(QEvent::MouseButtonPress, pick, window.m_Viewport->mapToGlobal(pick),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.m_Viewport, &press);
    QCOMPARE(state.linePhase, AppCommandState::LinePhase::NeedNextPoint);
    QVERIFY(std::abs(state.anchorX - 50.0) < 1e-4);
    QVERIFY(std::abs(state.anchorY) < 1e-4);
    QVERIFY(!state.objectSnapKindOverrideValid);
    window.CancelDraftingInput();
  }

  void issue903OrthoAndPolarConstrainInActiveUcs() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    state.activeUcs.xAxis = {0, 1, 0}; state.activeUcs.yAxis = {-1, 0, 0};
    state.activeUcs.zAxis = {0, 0, 1}; window.Submit("LINE"); window.Submit("0,0");
    state.orthoMode = true;
    SubmitViewportPick(state, 10, 7, window.m_Log);
    QCOMPARE(state.userLinesFlat.size(), size_t(6));
    QVERIFY(std::abs(state.userLinesFlat[3] - 10) < 1e-5);
    QVERIFY(std::abs(state.userLinesFlat[4]) < 1e-5);
    window.CancelDraftingInput();
    window.Submit("LINE"); window.Submit("10,0");
    state.orthoMode = false; state.polarMode = true; state.polarIncrementDeg = 90;
    SubmitViewportPick(state, 1, 18, window.m_Log);
    QCOMPARE(state.userLinesFlat.size(), size_t(12));
    QVERIFY(std::abs(state.userLinesFlat[9]) < 1e-5);
    QVERIFY(std::abs(state.userLinesFlat[10] - (10.0 + std::sqrt(65.0))) < 1e-4);
    window.CancelDraftingInput();
  }

  void issue903VisualOptions() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1280, 850);
    window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    QTRY_VERIFY(window.m_Viewport->IsRendererReady());
    const QString directory = QtTestArtifactDirectory() + "/issue903";
    QVERIFY(QDir().mkpath(directory));
    window.Submit("LINE");
    const QPoint cursorPoint(240, 200);
    QMouseEvent cursorMove(QEvent::MouseMove, cursorPoint, window.m_Viewport->mapToGlobal(cursorPoint),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window.m_Viewport, &cursorMove);
    QCOMPARE(window.m_Viewport->CursorPosition(), cursorPoint);
    window.RefreshDraftingInput(); QTest::qWait(100);
    QVERIFY(window.grab().save(directory + "/dynamic-final-first-point.png"));
    auto saveCursorCrop = [&](const QString& name) {
      const QRect crop = window.m_DynamicInput->geometry().adjusted(-18, -14, 18, 14)
          .intersected(window.m_Viewport->rect());
      QVERIFY(window.m_Viewport->grab(crop).save(directory + "/" + name));
    };
    saveCursorCrop("dynamic-final-first-point-crop.png");
    QTimer::singleShot(0, [&] {
      auto* menu = window.findChild<QMenu*>("snapOverrideMenu"); QVERIFY(menu);
      QVERIFY(!Action(*menu, "Perpendicular"));
      QVERIFY(menu->grab().save(directory + "/snap-menu-final-no-reference.png"));
      menu->close();
    });
    window.ShowSnapOverride(window.m_Viewport->mapToGlobal(QPoint(240, 200)));
    window.Submit("0,0"); window.State().polarMode = true; window.Refresh();
    QTest::qWait(100);
    QVERIFY(window.grab().save(directory + "/dynamic-final-next-point.png"));
    saveCursorCrop("dynamic-final-next-point-crop.png");
    auto* distance = window.findChild<QLineEdit*>("dynamicField0"); QVERIFY(distance);
    distance->setFocus(); distance->selectAll(); QTest::keyClicks(distance, "12.5"); QTest::qWait(100);
    QVERIFY(window.grab().save(directory + "/dynamic-final-editing-value.png"));
    saveCursorCrop("dynamic-final-editing-value-crop.png");
    for (int option = 0; option < 2; ++option) {
      QTimer::singleShot(0, [&] {
        auto* dialog = window.findChild<QDialog*>("polarSettingsDialog"); QVERIFY(dialog);
        if (option) dialog->findChild<QLabel*>("polarSettingsSummary")->setStyleSheet(
            "background:#223e54; color:#ecf5fc; padding:16px; border-radius:5px;");
        dialog->adjustSize();
        QVERIFY(dialog->grab().save(directory + QString("/angles-%1.png").arg(option ? "B" : "A")));
        if (!option) QVERIFY(dialog->grab().save(directory + "/polar-settings-final.png"));
        dialog->reject();
      });
      window.ShowPolarSettings();
      QTimer::singleShot(0, [&] {
        auto* menu = window.findChild<QMenu*>("snapOverrideMenu"); QVERIFY(menu);
        if (option) menu->setStyleSheet("QMenu { padding:7px; } QMenu::item { padding:7px 25px; } QMenu::section { color:#72bce8; }");
        menu->adjustSize();
        QVERIFY(menu->grab().save(directory + QString("/snap-%1.png").arg(option ? "B" : "A")));
        if (!option) QVERIFY(menu->grab().save(directory + "/snap-menu-final-with-reference.png"));
        menu->close();
      });
      window.ShowSnapOverride(window.m_Viewport->mapToGlobal(QPoint(240, 200)));
    }
    window.CancelDraftingInput();
  }

  void tabsAppearInitiallyAndTypingRoutesFromEditorChrome() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto* tabs = window.findChild<QTabBar*>("drawingTabs"); QVERIFY(tabs);
    auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport"); QVERIFY(viewport);
    QTRY_VERIFY(tabs->isVisible() && tabs->height() >= tabs->fontMetrics().height());
    QVERIFY(tabs->count() >= 2);
    QVERIFY(!window.findChild<QWidget*>("viewCubeControls"));
    const int viewportTop = viewport->mapTo(window.centralWidget(), QPoint()).y();
    QVERIFY(viewportTop > tabs->geometry().bottom());
    QVERIFY(viewportTop - tabs->geometry().bottom() <= 3);
    auto* input = window.findChild<QLineEdit*>("commandInput"); QVERIFY(input);
    auto* tree = window.findChild<QTreeWidget*>(); QVERIFY(tree);
    tree->setFocus(); QTRY_VERIFY(tree->hasFocus());
    QTest::keyClicks(tree, "l");
    QCOMPARE(input->text(), QString("l")); QVERIFY(input->hasFocus());
    window.m_CommandSuggestions->hide(); input->clear();
    tabs->setFocus(); QTRY_VERIFY(tabs->hasFocus());
    QTest::keyClicks(tabs, "c");
    QCOMPARE(input->text(), QString("c")); QVERIFY(input->hasFocus());
    window.m_CommandSuggestions->hide(); input->clear();
    auto* tool = window.findChild<QToolButton*>("quick_NEW"); QVERIFY(tool);
    tool->setFocus(); QTRY_VERIFY(tool->hasFocus());
    QTest::keyClicks(tool, "p");
    QCOMPARE(input->text(), QString("p")); QVERIFY(input->hasFocus());
    window.m_CommandSuggestions->hide(); input->clear();
    // An editable property keeps text input rather than turning it into a CAD command.
    window.State().userLinesFlat = {0, 0, 0, 10, 0, 0};
    EnsureAttrCounts(window.State());
    window.State().selection = {{SelectedEntity::Type::LineSeg, 0}}; window.Refresh();
    auto* layer = window.findChild<QComboBox*>("propLayer"); QVERIFY(layer);
    auto* edit = layer->lineEdit(); QVERIFY(edit); edit->setFocus(); edit->selectAll();
    QTest::keyClicks(edit, "Survey");
    QCOMPARE(edit->text(), QString("Survey")); QVERIFY(edit->hasFocus()); QVERIFY(input->text().isEmpty());
    // Read-only history is not a text editor: typing there returns to command entry.
    auto* panel = dynamic_cast<FloatingCommandBar*>(window.findChild<QFrame*>("floatingCommandBar")); QVERIFY(panel);
    panel->ToggleConsole(); auto* console = panel->Transcript(); console->setFocus();
    QTRY_VERIFY(console->hasFocus()); QTest::keyClicks(console, "u");
    QCOMPARE(input->text(), QString("u")); QVERIFY(input->hasFocus());
    window.m_CommandSuggestions->hide(); input->clear();
    QDialog dialog(&window); auto* field = new QLineEdit(&dialog);
    dialog.setModal(true); dialog.show(); dialog.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&dialog)); field->setFocus();
    QTest::keyClicks(field, "notes");
    QCOMPARE(field->text(), QString("notes")); QVERIFY(input->text().isEmpty());
    dialog.close();
    window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
    auto* dock = window.findChild<QDockWidget*>("toolspaceDock"); QVERIFY(dock);
    dock->setFloating(true); dock->show(); dock->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(dock)); tree->setFocus();
    QTRY_VERIFY(tree->hasFocus()); QTest::keyClicks(tree, "z");
    QCOMPARE(input->text(), QString("z"));
    // Zoom commands open the command suggestion popup while the command edit receives the prefix.
    QTRY_VERIFY(input->hasFocus() || window.m_CommandSuggestions->isVisible());
    window.m_CommandSuggestions->hide(); input->clear();
    dock->setFloating(false);
  }

  void qtDrawingTemplateWorkflowDiscoversCreatesCancelsAndPreservesFailure() {
    IsolatedUserData userData;
    const auto userTemplateDir = UserDataDirectory() / "drawing-templates";
    std::filesystem::create_directories(userTemplateDir);
    auto writeTemplate = [&](const QString& name, double x) {
      AppCommandState source;
      source.userLinesFlat = {x, 2.0, 0.0, x + 10.0, 2.0, 0.0};
      source.userLineAttrs.push_back(EntityAttributes{});
      const auto path = userTemplateDir / name.toStdWString();
      const auto pathUtf8 = QString::fromStdWString(path.wstring()).toUtf8();
      std::vector<std::string> log;
      return SaveGoSurveyTemplateFile(source, pathUtf8.constData(), log) ?
          QString::fromStdWString(path.wstring()) : QString{};
    };
    const QString alphaPath = writeTemplate("Alpha.gst", 21.0);
    const QString userDuplicatePath = writeTemplate("Field Survey.gst", 31.0);
    QVERIFY(!alphaPath.isEmpty()); QVERIFY(!userDuplicatePath.isEmpty());
    QFile ignoredFile(QString::fromStdWString((userTemplateDir / L"ignore.txt").wstring()));
    QVERIFY(ignoredFile.open(QIODevice::WriteOnly)); ignoredFile.write("not a template"); ignoredFile.close();
    QFile sourceFile(userDuplicatePath); QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray sourceBytes = sourceFile.readAll(); sourceFile.close();

    EditorWindow window;
    window.SwitchDrawing(1);
    window.State().userLinesFlat = {1.0, 3.0, 0.0, 4.0, 3.0, 0.0};
    SaveDocumentToSnapshot(window.State(), window.State().activeDrawingIdx);
    const auto beforeLines = window.State().userLinesFlat;
    const auto beforeTabs = window.State().drawingTabs.size();

    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog"); QVERIFY(dialog);
      auto* list = dialog->findChild<QListWidget*>("drawingTemplateList"); QVERIFY(list);
      QVERIFY(list->count() >= 4);
      QCOMPARE(list->item(0)->text(), QString("Blank Drawing"));
      bool bundledDuplicate = false, userDuplicate = false, hasAlpha = false, hasIgnored = false;
      int userRow = -1;
      for (int row = 0; row < list->count(); ++row) {
        const auto* item = list->item(row);
        bundledDuplicate |= item->text() == "Field Survey — Bundled";
        if (item->text() == "Field Survey — Personal") { userDuplicate = true; userRow = row; }
        hasAlpha |= item->text() == "Alpha — Personal";
        hasIgnored |= item->text().contains("ignore");
      }
      QVERIFY(bundledDuplicate); QVERIFY(userDuplicate); QVERIFY(hasAlpha); QVERIFY(!hasIgnored);
      QVERIFY(userRow >= 0);
      list->setCurrentRow(userRow);
      dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")->button(QDialogButtonBox::Open)->click();
    });
    window.NewDrawing();
    QCOMPARE(window.State().drawingTabs.size(), beforeTabs + 1);
    QVERIFY(window.State().activeDrawingIdx > 1);
    QCOMPARE(window.State().userLinesFlat.size(), size_t(6));
    QCOMPARE(window.State().userLinesFlat[0], 31.0);
    QCOMPARE(window.State().userLinesFlat[3], 41.0);
    QCOMPARE(window.State().drawingTabs.back().projectUid, 0u);
    const auto drawingPath = QString::fromStdWString((userTemplateDir / L"Created.dwg").wstring());
    const QByteArray drawingPathUtf8 = drawingPath.toUtf8();
    std::vector<std::string> drawingLog;
    QVERIFY(SaveDrawingDocument(window.State(), drawingPathUtf8.constData(), drawingLog));
    AppCommandState reopened;
    QVERIFY(OpenDrawingDocument(reopened, drawingPathUtf8.constData(), drawingLog));
    QCOMPARE(reopened.userLinesFlat, window.State().userLinesFlat);
    sourceFile.setFileName(userDuplicatePath);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly)); QCOMPARE(sourceFile.readAll(), sourceBytes); sourceFile.close();

    const auto activeIdx = window.State().activeDrawingIdx;
    const auto activeLines = window.State().userLinesFlat;
    const auto tabCount = window.State().drawingTabs.size();
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog"); QVERIFY(dialog);
      dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")->button(QDialogButtonBox::Cancel)->click();
    });
    window.NewDrawing();
    QCOMPARE(window.State().drawingTabs.size(), tabCount);
    QCOMPARE(window.State().activeDrawingIdx, activeIdx);
    QCOMPARE(window.State().userLinesFlat, activeLines);

    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog"); QVERIFY(dialog);
      auto* list = dialog->findChild<QListWidget*>("drawingTemplateList"); QVERIFY(list);
      QCOMPARE(list->item(0)->text(), QString("Blank Drawing"));
      list->setCurrentRow(0);
      dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")->button(QDialogButtonBox::Open)->click();
    });
    window.NewDrawing();
    QCOMPARE(window.State().drawingTabs.size(), tabCount + 1);
    QVERIFY(window.State().userLinesFlat.empty());
    window.SwitchDrawing(activeIdx);

    const auto badPath = userTemplateDir / L"Broken.gst";
    { std::ofstream bad(badPath, std::ios::binary); bad << "{ malformed"; }
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog"); QVERIFY(dialog);
      auto* list = dialog->findChild<QListWidget*>("drawingTemplateList"); QVERIFY(list);
      int brokenRow = -1;
      for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->text() == "Broken — Personal") brokenRow = row;
      QVERIFY(brokenRow >= 0); list->setCurrentRow(brokenRow);
      QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets())
          if (auto* warning = qobject_cast<QMessageBox*>(widget))
            if (auto* ok = warning->button(QMessageBox::Ok)) ok->click();
      });
      dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")->button(QDialogButtonBox::Open)->click();
    });
    window.NewDrawing();
    QCOMPARE(window.State().drawingTabs.size(), tabCount + 1);
    QCOMPARE(window.State().activeDrawingIdx, activeIdx);
    QCOMPARE(window.State().userLinesFlat, activeLines);
  }

  void qtNewDrawingInProjectUsesDefaultsWithoutTemplateChooser() {
    EditorWindow window;
    auto& state = window.State();
    AppCommandState::ProjectSession session;
    session.uid = 92;
    session.readOnly = true;
    session.settings = std::make_shared<ProjectSettings>();
    session.settings->insUnits = 2;
    state.openProjects.push_back(session);
    state.drawingTabs[1].projectUid = session.uid;
    window.SwitchDrawing(1);
    const auto tabsBefore = state.drawingTabs.size();
    window.NewDrawing();
    QCOMPARE(state.drawingTabs.size(), tabsBefore + 1);
    QCOMPARE(state.drawingTabs.back().projectUid, session.uid);
    QCOMPARE(state.drawingInsUnits, 2);
    QVERIFY(state.userLinesFlat.empty());
    QVERIFY(!window.findChild<QDialog*>("drawingTemplateDialog"));
    QCOMPARE(state.openProjects.front().settings->insUnits, 2);
    QCOMPARE(state.openProjects.front().readOnly, true);
  }

  void qtOpenExistingDrawingBypassesTemplateChooserAndPreservesTabsOnFailure() {
    IsolatedUserData userData;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString drawingPath = directory.filePath("Existing.dwg");
    AppCommandState source;
    source.userLinesFlat = {12.0, 4.0, 0.0, 18.0, 4.0, 0.0};
    source.userLineAttrs.push_back(EntityAttributes{});
    std::vector<std::string> saveLog;
    const QByteArray drawingPathUtf8 = drawingPath.toUtf8();
    QVERIFY(SaveDrawingDocument(source, drawingPathUtf8.constData(), saveLog));

    EditorWindow window;
    const auto initialTabs = window.State().drawingTabs.size();
    QVERIFY(initialTabs >= 2);
    QCOMPARE(window.State().activeDrawingIdx, 0);
    bool chooserAppeared = false;
    QTimer::singleShot(0, [&] {
      if (auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog")) {
        chooserAppeared = true;
        dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")
            ->button(QDialogButtonBox::Cancel)->click();
      }
    });
    window.OpenDrawing(drawingPath);
    QVERIFY(!chooserAppeared);
    QVERIFY(!window.findChild<QDialog*>("drawingTemplateDialog"));
    QCOMPARE(window.State().drawingTabs.size(), initialTabs + 1);
    QCOMPARE(window.State().activeDrawingIdx, static_cast<int>(initialTabs));
    QCOMPARE(window.State().drawingTabs.front().name, std::string("Start"));
    QCOMPARE(window.State().drawingTabs[static_cast<std::size_t>(window.State().activeDrawingIdx)].name,
             std::string("Existing"));
    QCOMPARE(window.State().userLinesFlat, source.userLinesFlat);

    window.SwitchDrawing(1);
    const auto tabsBeforeSecondOpen = window.State().drawingTabs.size();
    bool secondChooserAppeared = false;
    QTimer::singleShot(0, [&] {
      if (auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog")) {
        secondChooserAppeared = true;
        dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")
            ->button(QDialogButtonBox::Cancel)->click();
      }
    });
    window.OpenDrawing(drawingPath);
    QVERIFY(!secondChooserAppeared);
    QCOMPARE(window.State().drawingTabs.size(), tabsBeforeSecondOpen + 1);
    QCOMPARE(window.State().activeDrawingIdx, static_cast<int>(tabsBeforeSecondOpen));
    QCOMPARE(window.State().drawingTabs[1].name, std::string("Drawing 1"));
    QCOMPARE(window.State().drawingTabs.back().name, std::string("Existing"));
    QCOMPARE(window.State().userLinesFlat, source.userLinesFlat);

    const auto activeIndex = window.State().activeDrawingIdx;
    const auto tabsBeforeFailure = window.State().drawingTabs.size();
    const auto linesBeforeFailure = window.State().userLinesFlat;
    QTimer::singleShot(0, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* warning = qobject_cast<QMessageBox*>(widget))
          if (auto* ok = warning->button(QMessageBox::Ok)) ok->click();
    });
    window.OpenDrawing(directory.filePath("Missing.dwg"));
    QCOMPARE(window.State().drawingTabs.size(), tabsBeforeFailure);
    QCOMPARE(window.State().activeDrawingIdx, activeIndex);
    QCOMPARE(window.State().userLinesFlat, linesBeforeFailure);
    QCOMPARE(window.State().drawingTabs.front().name, std::string("Start"));
  }

  void qtDrawingTemplateScreenshotEvidenceForIssue885() {
    IsolatedUserData userData;
    const auto templateDir = UserDataDirectory() / "drawing-templates";
    std::filesystem::create_directories(templateDir);
    AppCommandState source;
    source.userLinesFlat = {1.0, 2.0, 0.0, 5.0, 2.0, 0.0};
    source.userLineAttrs.push_back(EntityAttributes{});
    std::vector<std::string> log;
    const auto templatePath = QString::fromStdWString((templateDir / L"Example.gst").wstring()).toUtf8();
    QVERIFY(SaveGoSurveyTemplateFile(source, templatePath.constData(), log));
    { std::ofstream broken(templateDir / L"Broken.gst", std::ios::binary); broken << "{ malformed"; }

    EditorWindow window;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(QDir::current().filePath("qt-885-template-chooser.png")));
      auto* list = dialog->findChild<QListWidget*>("drawingTemplateList"); QVERIFY(list);
      int exampleRow = -1;
      for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->text() == "Example — Personal") exampleRow = row;
      QVERIFY(exampleRow >= 0); list->setCurrentRow(exampleRow);
      QVERIFY(dialog->grab().save(QDir::current().filePath("qt-885-template-chooser-selected.png")));
      dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")->button(QDialogButtonBox::Cancel)->click();
    });
    window.NewDrawing();

    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingTemplateDialog"); QVERIFY(dialog);
      auto* list = dialog->findChild<QListWidget*>("drawingTemplateList"); QVERIFY(list);
      int brokenRow = -1;
      for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->text() == "Broken — Personal") brokenRow = row;
      QVERIFY(brokenRow >= 0); list->setCurrentRow(brokenRow);
      QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets())
          if (auto* warning = qobject_cast<QMessageBox*>(widget)) {
            QVERIFY(warning->grab().save(QDir::current().filePath("qt-885-template-error.png")));
            if (auto* ok = warning->button(QMessageBox::Ok)) ok->click();
          }
      });
      dialog->findChild<QDialogButtonBox*>("drawingTemplateButtons")->button(QDialogButtonBox::Open)->click();
    });
    window.NewDrawing();
  }

  void appearanceChangesKeepDrawingAndToolspaceState() {
    const auto oldPalette = qApp->palette(); const auto oldFont = qApp->font();
    const auto oldSheet = qApp->styleSheet(); const auto oldStyle = qApp->style()->objectName();
    const auto oldDefault = qApp->property("defaultQtStyle"), oldLight = qApp->property("cadLightTheme");
    auto restore = qScopeGuard([&] {
      qApp->setStyle(oldStyle); qApp->setPalette(oldPalette); qApp->setFont(oldFont); qApp->setStyleSheet(oldSheet);
      qApp->setProperty("defaultQtStyle", oldDefault); qApp->setProperty("cadLightTheme", oldLight);
      QSettings().remove("editor/lightTheme");
    });
    {
      EditorWindow startWindow;
      auto* graphite = Action(startWindow, "Graphite"); QVERIFY(graphite); graphite->trigger();
      QVERIFY(startWindow.grab().save(Issue886Shot("qt-432-graphite-start.png")));
    }
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.Submit("LINE"); window.Submit("0,0"); window.Submit("10,0");
    std::vector<std::string> log; CancelActiveCommand(window.State(), log);
    const auto geometry = window.State().userLinesFlat;
    auto* tree = window.findChild<QTreeWidget*>(); QVERIFY(tree);
    auto drawings = [&] { return tree->findItems("Drawings", Qt::MatchExactly).front(); };
    auto blocks = [&] { return tree->findItems("Blocks", Qt::MatchExactly).front(); };
    drawings()->setExpanded(false); blocks()->setExpanded(true);
    QVERIFY(blocks()->childCount() > 0);
    const auto selected = blocks()->child(0)->text(0); tree->setCurrentItem(blocks()->child(0));
    for (const auto& theme : {QString("Graphite"), QString("Classic light")}) {
      auto* action = Action(window, theme); QVERIFY(action); action->trigger();
      QCOMPARE(window.State().userLinesFlat, geometry);
      QVERIFY(!drawings()->isExpanded()); QVERIFY(blocks()->isExpanded());
      QVERIFY(tree->currentItem()); QCOMPARE(tree->currentItem()->text(0), selected);
      QCOMPARE(qApp->property("cadLightTheme").toBool(), theme == "Classic light");
      if (theme == "Graphite") {
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#1c2024"));
        QCOMPARE(qApp->palette().color(QPalette::Button), QColor("#2a3035"));
        QVERIFY(window.grab().save(Issue886Shot("qt-432-graphite-drawing.png")));
      }
    }
    window.Submit("UNDO"); QVERIFY(window.State().userLinesFlat.empty());
    QVERIFY(!drawings()->isExpanded()); QVERIFY(blocks()->isExpanded());
  }

  void frontViewPanMovesInCameraPlane() {
    EditorWindow window;
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    // A GL widget normally initialises on its first paint, but a window on an invisible desktop
    // (tools/run-qt-tests-hidden.ps1) never receives one. Rendering it directly creates the
    // context and runs initializeGL() without needing the window to be exposed.
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    viewport->SetOrientation(0, 0);
    const QPoint start(viewport->width() / 2, viewport->height() / 2);
    QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, start);
    const QPoint finish = start + QPoint(25, 30);
    QMouseEvent move(QEvent::MouseMove, finish, viewport->mapToGlobal(finish), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::NoModifier, finish);
    QVERIFY(std::abs(window.State().viewportPanX) > 0.01);
    QVERIFY(std::abs(window.State().viewportPanZ) > 0.01);
    QVERIFY(std::abs(window.State().viewportPanY) < 0.001);
  }

  void orbitCommandLeftDragTumblesView() {
    AppCommandState state;
    std::vector<std::string> log;
    EditorViewport viewport(state, log, nullptr);
    viewport.resize(800, 600);
    StartOrbitCommand(state, log);
    QCOMPARE(state.active, AppCommandState::Kind::Orbit);
    const float azimuth = state.viewportAzimuthDeg;
    const float elevation = state.viewportElevationDeg;
    const QPoint start(viewport.width() / 2, viewport.height() / 2);
    const QPoint finish = start + QPoint(25, -20);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent move(QEvent::MouseMove, finish, finish, Qt::NoButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, finish, finish, Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &release);
    QVERIFY(std::abs(state.viewportAzimuthDeg - azimuth) > 0.1f);
    QVERIFY(std::abs(state.viewportElevationDeg - elevation) > 0.1f);

    state.activeSpaceIndex = 0;
    const float paperAzimuth = state.viewportAzimuthDeg;
    const float paperElevation = state.viewportElevationDeg;
    QMouseEvent paperPress(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &paperPress);
    QMouseEvent paperMove(QEvent::MouseMove, finish, finish, Qt::NoButton, Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &paperMove);
    QMouseEvent paperRelease(QEvent::MouseButtonRelease, finish, finish, Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &paperRelease);
    QCOMPARE(state.viewportAzimuthDeg, paperAzimuth);
    QCOMPARE(state.viewportElevationDeg, paperElevation);
  }

  void panCommandLeftDragMovesView() {
    AppCommandState state;
    std::vector<std::string> log;
    EditorViewport viewport(state, log, nullptr);
    viewport.resize(800, 600);
    const QPoint start(viewport.width() / 2, viewport.height() / 2);
    QEnterEvent enter(start, start, start);
    QApplication::sendEvent(&viewport, &enter);
    StartPanCommand(state, log);
    viewport.RefreshCommandCursor();
    QCOMPARE(state.active, AppCommandState::Kind::Pan);
    QCOMPARE(viewport.cursor().shape(), Qt::OpenHandCursor);
    const double panX = state.viewportPanX;
    const double panY = state.viewportPanY;
    const double panZ = state.viewportPanZ;
    const QPoint finish = start + QPoint(25, 20);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent move(QEvent::MouseMove, finish, finish, Qt::NoButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, finish, finish, Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &release);
    QVERIFY(std::abs(state.viewportPanX - panX) > 0.01);
    QVERIFY(std::abs(state.viewportPanY - panY) > 0.01 ||
        std::abs(state.viewportPanZ - panZ) > 0.01);
  }

  void viewportCubeIsVisibleInFramebuffer() {
    EditorWindow window;
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    // A GL widget normally initialises on its first paint, but a window on an invisible desktop
    // (tools/run-qt-tests-hidden.ps1) never receives one. Rendering it directly creates the
    // context and runs initializeGL() without needing the window to be exposed.
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    const auto image = viewport->grabFramebuffer();
    QVERIFY(!image.isNull());
    // Plan view (the startup default) must stay legible (REQ-059): the TOP face is drawn light and
    // carries its dark label, and the compass ring around it is drawn too.
    const qreal ratio = viewport->devicePixelRatioF();
    const viewcube::Scene scene = ViewCubeSceneOf(viewport, window.State());
    const viewcube::QuadFace* top = nullptr;
    for (const auto& q : scene.quads) if (q.visible) top = &q;
    QVERIFY(top && top->labelled);
    const QPoint corner(qRound((top->pts[0].x * 0.8 + top->pts[2].x * 0.2) * ratio),
                        qRound((top->pts[0].y * 0.8 + top->pts[2].y * 0.2) * ratio));
    QVERIFY2(image.pixelColor(corner).red() > 100, qPrintable(image.pixelColor(corner).name()));
    int darkLabelPixels = 0;
    const int cx = qRound(top->labelAt.x * ratio), cy = qRound(top->labelAt.y * ratio);
    for (int dy = -6; dy <= 6; ++dy)
      for (int dx = -12; dx <= 12; ++dx)
        if (image.pixelColor(cx + dx, cy + dy).red() < 90) ++darkLabelPixels;
    QVERIFY2(darkLabelPixels > 15, qPrintable(QString::number(darkLabelPixels)));
  }

  void qtViewportRendersGizmoAndSectionPlaneOverlays() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);

    auto& state = window.State();
    state.userLinesFlat = {-12.0, 0.0, 0.0, 12.0, 0.0, 0.0};
    state.userLineAttrs.push_back(EntityAttributes{});
    viewport->InvalidateGeometry();
    QTest::qWait(30);
    const QImage base = viewport->grabFramebuffer();
    QVERIFY(!base.isNull());

    SelectedEntity line;
    line.type = SelectedEntity::Type::LineSeg;
    line.index = 0;
    state.selection.push_back(line);
    state.gizmoPersistent = true;
    viewport->update();
    QTest::qWait(30);
    const QImage gizmo = viewport->grabFramebuffer();
    QVERIFY(gizmo != base);
    QVERIFY(gizmo.save(QtTestArtifactDirectory() + "/qt-viewport-option-a.png"));

    state.viewportSectionClip = true;
    state.gizmoPersistent = false;
    state.sectionPlaneSelected = true;
    viewport->update();
    QTest::qWait(30);
    const QImage sectionGrips = viewport->grabFramebuffer();
    QVERIFY(sectionGrips != gizmo);
    QVERIFY(sectionGrips.save(QtTestArtifactDirectory() + "/qt-viewport-option-b.png"));

    state.gizmoPersistent = true;
    state.viewportSectionClipOffset = 0.0;
    state.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::Move);
    viewport->update();
    QTest::qWait(30);
    const QImage combined = viewport->grabFramebuffer();
    QVERIFY(combined != sectionGrips);
    CadGizmoOverlay combinedOverlay;
    BuildGizmoOverlay(state, &combinedOverlay);
    QVERIFY(!combinedOverlay.empty());
    QVERIFY(combined.save(QtTestArtifactDirectory() + "/qt-viewport-option-c.png"));
  }

  void qtViewportGizmoDragCommitsOneUndoableTransform() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    viewport->SetOrientation(0.f, 90.f);

    auto& state = window.State();
    state.userLinesFlat = {-12.0, 0.0, 0.0, 12.0, 0.0, 0.0};
    state.userLineAttrs.push_back(EntityAttributes{});
    SelectedEntity line;
    line.type = SelectedEntity::Type::LineSeg;
    line.index = 0;
    state.selection.push_back(line);
    state.gizmoPersistent = true;
    viewport->InvalidateGeometry();

    CadGizmoOverlay overlay;
    BuildGizmoOverlay(state, &overlay);
    QVERIFY(!overlay.empty());
    const auto& axis = overlay.axis[0];
    QVERIFY(axis.size() >= 6);
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    const auto camera = CadViewCamera(state);
    camera.WorldToScreen(axis[0], axis[1], axis[2], static_cast<float>(viewport->width()),
        static_cast<float>(viewport->height()), &ax, &ay);
    camera.WorldToScreen(axis[3], axis[4], axis[5], static_cast<float>(viewport->width()),
        static_cast<float>(viewport->height()), &bx, &by);
    const double dx = bx - ax, dy = by - ay;
    const double length = std::hypot(dx, dy);
    QVERIFY(length > 8.0);
    const QPoint start(qRound((ax + bx) * 0.5), qRound((ay + by) * 0.5));
    const QPoint finish(start.x() + qRound(dx / length * 24.0),
                        start.y() + qRound(dy / length * 24.0));
    const auto before = state.userLinesFlat;
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent cancelledMove(QEvent::MouseMove, finish, viewport->mapToGlobal(finish),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &cancelledMove);
    QVERIFY(state.gizmoDragActive);
    QTest::mouseClick(viewport, Qt::RightButton);
    QVERIFY(!state.gizmoDragActive);
    QCOMPARE(state.userLinesFlat, before);

    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(state.gizmoDragActive);
    QMouseEvent move(QEvent::MouseMove, finish, viewport->mapToGlobal(finish),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, finish);
    QVERIFY(!state.gizmoDragActive);
    QVERIFY(state.userLinesFlat != before);
    window.Submit("UNDO");
    QCOMPARE(state.userLinesFlat, before);
  }

  void qtViewportSectionPlaneGripDragEndsOnReleaseAndUndoes() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    viewport->SetOrientation(32.f, 38.f);

    auto& state = window.State();
    state.userLinesFlat = {-20.0, -10.0, -2.0, 20.0, 10.0, 2.0};
    state.userLineAttrs.push_back(EntityAttributes{});
    state.viewportSectionClip = true;
    state.viewportSectionClipFrameValid = true;
    state.viewportSectionClipFrame = ucs::Ucs{};
    state.viewportSectionClipExtent = SectionPlaneExtent{true, 0.0, 0.0, 20.0, 12.0};
    state.sectionPlaneSelected = true;
    viewport->InvalidateGeometry();

    const auto grips = CadSectionPlaneGrips(state);
    QVERIFY(grips.valid);
    const auto camera = CadViewCamera(state);
    float x = 0.f, y = 0.f;
    const auto moveGrip = grips.at[static_cast<int>(SectionPlaneGrip::Move)];
    camera.WorldToScreen(moveGrip.x, moveGrip.y, moveGrip.z,
        static_cast<float>(viewport->width()), static_cast<float>(viewport->height()), &x, &y);
    const QPoint start(qRound(x), qRound(y));
    const QPoint finish = start + QPoint(12, -8);

    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(state.sectionPlaneGripDrag >= 0);
    QMouseEvent cancelledMove(QEvent::MouseMove, finish, viewport->mapToGlobal(finish),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &cancelledMove);
    QVERIFY(std::abs(state.viewportSectionClipOffset) > 1e-6);
    QTest::mouseClick(viewport, Qt::RightButton);
    QCOMPARE(state.sectionPlaneGripDrag, -1);
    QCOMPARE(state.viewportSectionClipOffset, 0.0);

    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(state.sectionPlaneGripDrag >= 0);
    QMouseEvent move(QEvent::MouseMove, finish, viewport->mapToGlobal(finish),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, finish);
    QCOMPARE(state.sectionPlaneGripDrag, -1);
    QVERIFY(std::abs(state.viewportSectionClipOffset) > 1e-6);
    window.Submit("UNDO");
    QCOMPARE(state.viewportSectionClipOffset, 0.0);
  }

  void nativeLayerManagerEditsAreUndoable() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    std::string error;
    QVERIFY(CadAddDrawingLayer(window.State(), "Survey", &error));
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("layerManager");
      if (!dialog) return;
      auto* table = dialog->findChild<QTableWidget*>("layerTable");
      if (table && table->rowCount() >= 2) table->item(1, 1)->setCheckState(Qt::Unchecked);
      dialog->reject();
    });
    window.Submit("LAYER");
    QVERIFY(!window.State().drawingLayerTable[1].on);
    window.Submit("UNDO");
    QVERIFY(window.State().drawingLayerTable[1].on);
  }

  void issue1036LayerManagerSortingActsOnTheShownRecord() {
    ApplyQtAppearance(*qApp, false);  // the app's Dark theme, whatever an earlier test left applied
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    std::string error;
    QVERIFY(CadAddDrawingLayer(state, "Survey", &error));
    QVERIFY(CadAddDrawingLayer(state, "Alpha", &error));
    state.currentLayer = "Survey";
    std::vector<std::string> storedOrder;
    storedOrder.reserve(state.drawingLayerTable.size());
    for (const auto& layer : state.drawingLayerTable) storedOrder.push_back(layer.name);
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("layerManager"); QVERIFY(dialog);
      QTimer::singleShot(20000, dialog, &QDialog::reject);
      auto* table = dialog->findChild<QTableWidget*>("layerTable"); QVERIFY(table);
      auto* header = table->horizontalHeader();
      auto names = [&] { QStringList list; for (int row = 0; row < table->rowCount(); ++row) list << table->item(row, 0)->text(); return list; };
      QCOMPARE(names(), QStringList({"0", "Survey", "Alpha"}));
      // Layout: the Name column takes the width the others leave, so the table fills the window.
      int columnsWidth = 0;
      for (int column = 0; column < table->columnCount(); ++column) if (!table->isColumnHidden(column)) columnsWidth += header->sectionSize(column);
      QVERIFY(columnsWidth >= table->viewport()->width() - 2);
      QVERIFY(header->sectionSize(0) > 150);
      // Swatch and linetype sample sit beside the stored text.
      QCOMPARE(table->item(0, 4)->text(), QString::fromStdString(state.drawingLayerTable[0].color));
      QVERIFY(table->item(0, 4)->data(Qt::DecorationRole).isValid());
      QVERIFY(table->item(0, 5)->data(Qt::DecorationRole).isValid());
      // The current layer is marked.
      QVERIFY(table->item(1, 0)->font().bold());
      QVERIFY(!table->item(2, 0)->font().bold());
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-1036-layer-manager-normal.png")));
      auto clickHeader = [&](int column) { QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(header->sectionViewportPosition(column) + 8, header->height() / 2)); };
      clickHeader(0);
      QCOMPARE(names(), QStringList({"0", "Alpha", "Survey"}));
      clickHeader(0);
      QCOMPARE(names(), QStringList({"Survey", "Alpha", "0"}));
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-1036-layer-manager-sorted.png")));
      // Row 0 is Survey now: the edit lands on Survey, not on the layer that used to be first.
      table->item(0, 1)->setCheckState(Qt::Unchecked);
      QCOMPARE(names(), QStringList({"Survey", "Alpha", "0"}));
      QVERIFY(!state.drawingLayerTable[1].on);
      QVERIFY(state.drawingLayerTable[0].on && state.drawingLayerTable[2].on);
      // Delete acts on the row shown.
      table->setCurrentCell(1, 0);
      dialog->findChild<QPushButton*>("layerDelete")->click();
      QCOMPARE(names(), QStringList({"Survey", "0"}));
      // Set current acts on the row shown.
      table->setCurrentCell(1, 0);
      dialog->findChild<QPushButton*>("layerSetCurrent")->click();
      QCOMPARE(state.currentLayer, std::string("0"));
      // Columns can be hidden and reordered; the checkbox cells are drawn in both states.
      header->setSectionHidden(3, true);
      header->moveSection(header->visualIndex(6), 1);
      QVERIFY(header->isSectionHidden(3));
      dialog->reject();
    });
    window.Submit("LAYER");
    // Display sorting never reordered the saved layers.
    QCOMPARE(state.drawingLayerTable.size(), std::size_t(2));
    QCOMPARE(state.drawingLayerTable[0].name, storedOrder[0]);
    QCOMPARE(state.drawingLayerTable[1].name, storedOrder[1]);
  }

  void issue1036LayerManagerViewportColumnsEditTheCurrentViewport() {
    ApplyQtAppearance(*qApp, false);  // the app's Dark theme, whatever an earlier test left applied
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    std::string error;
    QVERIFY(CadAddDrawingLayer(state, "Survey", &error));
    // Model space: no current viewport, so the VP columns are not shown.
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("layerManager"); QVERIFY(dialog);
      auto* table = dialog->findChild<QTableWidget*>("layerTable"); QVERIFY(table);
      QVERIFY(table->isColumnHidden(7));
      QVERIFY(table->isColumnHidden(8));
      dialog->reject();
    });
    window.Submit("LAYER");
    PaperLayout layout;
    layout.viewports.push_back(Viewport{});
    state.paperLayouts = {layout};
    state.activeSpaceIndex = 0;
    state.selectedViewportIndex = 0;
    state.selectedViewports = {0};
    QVERIFY(CurrentViewport(state) != nullptr);
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("layerManager"); QVERIFY(dialog);
      QTimer::singleShot(20000, dialog, &QDialog::reject);
      auto* table = dialog->findChild<QTableWidget*>("layerTable"); QVERIFY(table);
      QVERIFY(!table->isColumnHidden(7));
      QVERIFY(!table->isColumnHidden(8));
      table->item(1, 7)->setCheckState(Qt::Checked);
      QVERIFY(IsLayerFrozenInViewport(state.paperLayouts[0].viewports[0], "Survey"));
      QVERIFY(!state.drawingLayerTable[1].frozen);
      table->item(1, 8)->setText("Red");
      QVERIFY(ViewportLayerColorOverride(state.paperLayouts[0].viewports[0], "Survey") != nullptr);
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-1036-layer-manager-viewport.png")));
      table->item(1, 8)->setText("");
      QVERIFY(ViewportLayerColorOverride(state.paperLayouts[0].viewports[0], "Survey") == nullptr);
      table->item(1, 7)->setCheckState(Qt::Unchecked);
      QVERIFY(!IsLayerFrozenInViewport(state.paperLayouts[0].viewports[0], "Survey"));
      dialog->reject();
    });
    window.Submit("LAYER");
  }

  void pointGroupManagerCreatesAndUndoesFromEmptyState() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("pointGroupManager"); QVERIFY(dialog);
      QTimer::singleShot(20000, dialog, &QDialog::reject);
      auto* matches = dialog->findChild<QLabel*>("pointGroupMatchCount"); QVERIFY(matches);
      QVERIFY(matches->text().contains("No point group selected"));
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-894-point-groups-empty.png")));
      auto* create = dialog->findChild<QPushButton*>("pointGroupNew"); QVERIFY(create); create->click();
      QCOMPARE(window.State().pointGroups.size(), std::size_t(1));
      QCOMPARE(window.State().pointGroups.front().name, std::string("Point Group"));
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-894-point-groups-new.png")));
      dialog->findChild<QPushButton*>("pointGroupDelete")->click();
      QVERIFY(window.State().pointGroups.empty());
      dialog->findChild<QPushButton*>("pointGroupClose")->click();
    });
    window.RunRibbonAction("@pointGroups");
    QCOMPARE(window.State().pointGroups.size(), std::size_t(0));
    window.Submit("UNDO");
    QCOMPARE(window.State().pointGroups.size(), std::size_t(1));
    window.Submit("UNDO");
    QVERIFY(window.State().pointGroups.empty());
  }

  void pointGroupManagerEditsRulesValidatesAndPreviewsMembers() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    state.surveyPoints = {{1, 0, 0, 0, "EG CL", "EG", ""},
                          {2, 1, 0, 0, "EG TOE", "EG", ""},
                          {3, 2, 0, 0, "Road", "RD", ""}};
    AppCommandState::ProjectSession readOnlyProject; readOnlyProject.uid = 894; readOnlyProject.readOnly = true;
    state.openProjects.push_back(readOnlyProject);
    state.drawingTabs[1].projectUid = readOnlyProject.uid;
    PointGroup ground; ground.name = "Existing Ground"; ground.rule.descriptionMatch = "EG*";
    PointGroup curb; curb.name = "Curb Shots";
    state.pointGroups = {ground, curb};
    state.selectedSurveyPointIndices = {2};
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("pointGroupManager"); QVERIFY(dialog);
      QTimer::singleShot(20000, dialog, &QDialog::reject);
      auto* picker = dialog->findChild<QComboBox*>("pointGroupPicker"); QVERIFY(picker);
      QCOMPARE(picker->count(), 2);
      picker->setCurrentIndex(1);
      QCOMPARE(dialog->findChild<QLineEdit*>("pointGroupName")->text(), QString("Curb Shots"));
      picker->setCurrentIndex(0);
      auto* matches = dialog->findChild<QLabel*>("pointGroupMatchCount"); QVERIFY(matches);
      QTRY_VERIFY(matches->text().contains("2 matched"));
      auto* preview = dialog->findChild<QListWidget*>("pointGroupMemberPreview"); QVERIFY(preview);
      QCOMPARE(preview->count(), 2);
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-894-point-groups-populated.png")));

      auto* name = dialog->findChild<QLineEdit*>("pointGroupName"); QVERIFY(name);
      name->setText("Curb Shots");
      QVERIFY(QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection));
      QCOMPARE(state.pointGroups.front().name, std::string("Existing Ground"));
      auto* validation = dialog->findChild<QLabel*>("pointGroupValidation"); QVERIFY(validation);
      QVERIFY(validation->text().contains("already exists"));
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-894-point-groups-validation.png")));

      auto* ranges = dialog->findChild<QLineEdit*>("pointGroupRanges"); QVERIFY(ranges);
      ranges->setFocus(); ranges->selectAll(); QTest::keyClicks(ranges, "bad-range");
      QVERIFY(QMetaObject::invokeMethod(ranges, "editingFinished", Qt::DirectConnection));
      QVERIFY(dialog->findChild<QLabel*>("pointGroupRuleHint")->text().contains("Ignored invalid point number"));
      QVERIFY(std::any_of(window.m_Log.begin(), window.m_Log.end(), [](const auto& entry) {
        return entry.find("ignored unparseable id range") != std::string::npos;
      }));

      auto* description = dialog->findChild<QLineEdit*>("pointGroupDescription"); QVERIFY(description);
      description->setFocus(); description->selectAll(); QTest::keyClicks(description, "Road*");
      QVERIFY(QMetaObject::invokeMethod(description, "editingFinished", Qt::DirectConnection));
      QCOMPARE(state.pointGroups.front().rule.descriptionMatch, std::string("Road*"));
      QTRY_VERIFY(matches->text().contains("1 matched"));
      QCOMPARE(preview->count(), 1);
      auto* add = dialog->findChild<QPushButton*>("pointGroupAddSelection"); QVERIFY(add); add->click();
      QCOMPARE(state.pointGroups.front().rule.explicitIds, std::vector<int>{3});
      QTRY_VERIFY(matches->text().contains("1 matched"));
      auto* clear = dialog->findChild<QPushButton*>("pointGroupClearPicks"); QVERIFY(clear); clear->click();
      QVERIFY(state.pointGroups.front().rule.explicitIds.empty());

      description->setFocus(); description->selectAll(); QTest::keyClicks(description, "Nothing*");
      QVERIFY(QMetaObject::invokeMethod(description, "editingFinished", Qt::DirectConnection));
      QVERIFY(validation->text().isEmpty());
      QTRY_VERIFY(matches->text().contains("Matches 0 of 3"));
      QVERIFY(dialog->grab().save(QDir(QtTestArtifactDirectory()).filePath("qt-894-point-groups-no-match.png")));
      QTest::keyClick(dialog, Qt::Key_Escape);
    });
    window.RunRibbonAction("@pointGroups");
    QVERIFY(state.openProjects.front().readOnly);
    QCOMPARE(state.pointGroups.front().rule.descriptionMatch, std::string("Nothing*"));
    QVERIFY(state.pointGroups.front().rule.explicitIds.empty());
    window.Submit("UNDO");
    QCOMPARE(state.pointGroups.front().rule.descriptionMatch, std::string("Road*"));
  }

  void nativeUnitsApplyWithoutMovingGeometry() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.Submit("LINE"); window.Submit("0,0"); window.Submit("10,0");
    std::vector<std::string> log;
    CancelActiveCommand(window.State(), log);
    const auto before = window.State().userLinesFlat;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingSettingsDialog");
      if (!dialog) return;
      dialog->findChild<QComboBox*>("drawingUnit")->setCurrentIndex(4);
      auto* scale = dialog->findChild<QComboBox*>("drawingScaleChoice");
      scale->setCurrentIndex(scale->findData(-1));
      dialog->findChild<QDoubleSpinBox*>("drawingCustomScale")->setValue(100);
      auto* buttons = dialog->findChild<QDialogButtonBox*>();
      buttons->button(QDialogButtonBox::Ok)->click();
    });
    window.Submit("UNITS");
    QCOMPARE(window.State().drawingInsUnits, 6);
    QCOMPARE(window.State().modelUnitsPerPlottedInch, 100.f);
    QCOMPARE(window.State().userLinesFlat, before);
    window.Submit("UNDO");
    QCOMPARE(window.State().drawingInsUnits, 2);
    QCOMPARE(window.State().userLinesFlat, before);
  }

  void nativeQuickSelectFiltersAndCancelPreservesSelection() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    std::vector<std::string> log;
    for (const auto& endpoint : {"10,0", "20,0"}) {
      window.Submit("LINE"); window.Submit("0,0"); window.Submit(endpoint);
      CancelActiveCommand(window.State(), log);
    }
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("quickSelect");
      QVERIFY(dialog);
      dialog->findChild<QComboBox*>("selectionType")->setCurrentIndex(1);
      auto* property = dialog->findChild<QComboBox*>("selectionProperty");
      property->setCurrentIndex(property->findData(2));
      auto* operation = dialog->findChild<QComboBox*>("selectionOperator");
      operation->setCurrentIndex(operation->findData(3));
      dialog->findChild<QComboBox*>("selectionValue")->setEditText("15");
      dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    window.Submit("QUICKSELECT");
    QCOMPARE(window.State().selection.size(), std::size_t(1));
    QCOMPARE(window.State().selection.front().index, 1);
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("quickSelect");
      QVERIFY(dialog);
      dialog->findChild<QComboBox*>("selectionType")->setCurrentIndex(2);
      dialog->reject();
    });
    window.Submit("QUICKSELECT");
    QCOMPARE(window.State().selection.size(), std::size_t(1));
    QCOMPARE(window.State().selection.front().index, 1);
    QCOMPARE(window.State().qsObjectType, AppCommandState::QsObjectType::Line);
  }

  void qtSelectionListSnapshotAndMultiSelectionToggle() {
    EditorWindow window;
    window.resize(1100, 760);
    window.State().selection.push_back({SelectedEntity::Type::LineSeg, 0});
    QToolButton* multi = nullptr;
    for (auto* button : window.statusBar()->findChildren<QToolButton*>())
      if (button->text() == "Multi Selection") multi = button;
    QVERIFY(multi);
    multi->click();
    QVERIFY(window.State().multiSelectionEnabled);

    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QWidget*>("modelViewport"));
    QVERIFY(viewport);
    // The Selection list works from a snapshot taken when it is opened (the shortcut menu's "Selection...").
    window.State().selectionCycleEntities = window.State().selection;
    window.ShowSelectionList();
    auto* dialog = window.findChild<QDialog*>("selectionList");
    QVERIFY(dialog);
    auto* list = dialog->findChild<QListWidget*>("selectionListItems");
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    list->item(0)->setCheckState(Qt::Unchecked);
    QVERIFY(window.State().selection.empty());
    dialog->findChild<QPushButton*>("selectionListSelectAll")->click();
    QCOMPARE(window.State().selection.size(), std::size_t(1));
    QCOMPARE(window.State().selection.front().type, SelectedEntity::Type::LineSeg);
    dialog->findChild<QPushButton*>("selectionListRefresh")->click();
    QCOMPARE(list->count(), 1);
  }

  void qtViewportGripDragCommitsAndEscapeRestores() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1100, 760);
    AppCommandState& state = window.State();
    state.userLinesFlat = {0, 0, 0, 10, 0, 0};
    state.userLineAttrs.resize(1);
    state.selection.push_back({SelectedEntity::Type::LineSeg, 0});
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY(viewport->width() > 200);
    QTest::qWait(150);
    const Camera camera = CadViewCamera(state);
    float sx = 0, sy = 0, dx = 0, dy = 0;
    camera.WorldToScreen(0, 0, 0, static_cast<float>(viewport->width()), static_cast<float>(viewport->height()), &sx, &sy);
    camera.WorldToScreen(0, 5, 0, static_cast<float>(viewport->width()), static_cast<float>(viewport->height()), &dx, &dy);
    const QPointF start(sx, sy), destination(dx, dy);
    auto sendMouse = [viewport](QEvent::Type type, QPointF local, Qt::MouseButton button, Qt::MouseButtons buttons) {
      QMouseEvent event(type, local, local, button, buttons, Qt::NoModifier);
      QApplication::sendEvent(viewport, &event);
    };
    sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QVERIFY(std::abs(state.userLinesFlat[0]) < 0.02);
    QVERIFY(std::abs(state.userLinesFlat[1] - 5.0) < 0.1);
    window.Submit("UNDO");
    QCOMPARE(state.userLinesFlat[1], 0.0);

    camera.WorldToScreen(10, 0, 0, static_cast<float>(viewport->width()), static_cast<float>(viewport->height()), &sx, &sy);
    camera.WorldToScreen(10, -5, 0, static_cast<float>(viewport->width()), static_cast<float>(viewport->height()), &dx, &dy);
    sendMouse(QEvent::MouseButtonPress, QPointF(sx, sy), Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPointF(dx, dy), Qt::NoButton, Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewport, &escape);
    QCOMPARE(state.userLinesFlat[3], 10.0);
    QCOMPARE(state.userLinesFlat[4], 0.0);
  }

  void qtCtrlClickSelectsSolidSubObjectAndPlainClickSelectsWholeSolid() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1100, 760);
    AppCommandState& state = window.State();
    brep::Solid solid;
    brep::Problem problem{};
    QVERIFY(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 10.0, &solid, &problem));
    state.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
    state.cadSolidAttrs.emplace_back();
    RefreshSolidDisplayGeometry(state);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY(viewport->width() > 200);
    float x = 0, y = 0;
    CadViewCamera(state).WorldToScreen(10, 10, 10, static_cast<float>(viewport->width()),
        static_cast<float>(viewport->height()), &x, &y);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::ControlModifier, QPoint(qRound(x), qRound(y)));
    QVERIFY(!state.subObjectSelection.empty());
    QVERIFY(state.selection.empty());
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(x), qRound(y)));
    QVERIFY(state.subObjectSelection.empty());
    QCOMPARE(state.selection.size(), std::size_t(1));
    QCOMPARE(state.selection.front().type, SelectedEntity::Type::Solid);
    QCOMPARE(state.selection.front().index, 0);
  }

  void qtOverlappingPickMenuSelectsChosenVisibleEntity() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1100, 760);
    AppCommandState& state = window.State();
    state.userLinesFlat = {0, 0, 0, 10, 0, 0, 0, 0, 0, 10, 0, 0};
    state.userLineAttrs.resize(2);
    state.multiSelectionEnabled = true;
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY(viewport->width() > 200);
    float x = 0, y = 0;
    CadViewCamera(state).WorldToScreen(5, 0, 0, static_cast<float>(viewport->width()),
        static_cast<float>(viewport->height()), &x, &y);
    auto* menuPoll = new QTimer(&window);
    auto attempts = std::make_shared<int>(0);
    connect(menuPoll, &QTimer::timeout, &window, [menuPoll, attempts, &window] {
      for (QMenu* menu : window.findChildren<QMenu*>()) {
        for (QAction* action : menu->actions())
          if (action->text().startsWith("Line 2")) {
            menuPoll->stop(); action->trigger(); return;
          }
      }
      if (++*attempts > 300) {
        menuPoll->stop();
        for (QMenu* menu : window.findChildren<QMenu*>()) menu->close();
      }
    });
    menuPoll->start(10);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(x), qRound(y)));
    QTRY_COMPARE(state.selection.size(), std::size_t(1));
    QCOMPARE(state.selection.front().type, SelectedEntity::Type::LineSeg);
    QCOMPARE(state.selection.front().index, 1);
  }

  void qtCrossingSelectionDragAndEscapeCancellation() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1100, 760);
    AppCommandState& state = window.State();
    state.userLinesFlat = {0, 0, 0, 10, 0, 0};
    state.userLineAttrs.resize(1);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QWidget*>("modelViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY(viewport->width() > 200);
    const Camera camera = CadViewCamera(state);
    auto screenPoint = [&](double wx, double wy) {
      float x = 0, y = 0;
      camera.WorldToScreen(wx, wy, 0, static_cast<float>(viewport->width()),
          static_cast<float>(viewport->height()), &x, &y);
      return QPointF(x, y);
    };
    auto sendMouse = [viewport](QEvent::Type type, QPointF local, Qt::MouseButton button, Qt::MouseButtons buttons) {
      QMouseEvent event(type, local, local, button, buttons, Qt::NoModifier);
      QApplication::sendEvent(viewport, &event);
    };
    const QPointF from = screenPoint(12, 4), to = screenPoint(-2, -4);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!state.selection.empty());
    QVERIFY(!state.selBoxWaitingSecond);
    const auto selected = state.selection;
    const QPointF blank = screenPoint(-30, -20);  // clear of the ViewCube, which owns the top-right corner
    sendMouse(QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, blank + QPointF(12, 10), Qt::NoButton, Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewport, &escape);
    QCOMPARE(state.selection.size(), selected.size());
    QVERIFY(!state.selBoxWaitingSecond);
  }

  void nativePointTransferImportsExportsAndUndoes() {
    QTemporaryDir directory;
    const auto input = directory.filePath(QString::fromUtf8("points-\xc3\xa9.csv"));
    const auto output = directory.filePath("export.csv");
    QFile file(input); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\xef\xbb\xbfP,N,E,Z,D\n12,100.125,200.25,30.123456789,EG\n13,101,201,31,Tree\n"); file.close();
    EditorWindow window; window.CreateDrawingFromTemplate();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("importSurveyPoints"); QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("pointFilePath")->setText(input);
      dialog->findChild<QComboBox*>("pointColumnOrder")->setCurrentIndex(0);
      auto* header = dialog->findChild<QCheckBox*>("pointFileHeader"); header->setChecked(true);
      auto* apply = dialog->findChild<QPushButton*>("applyPointFile");
      QTRY_VERIFY(apply->isEnabled()); apply->click(); dialog->reject();
    });
    window.Submit("IMPORTPOINTS");
    QCOMPARE(window.State().surveyPoints.size(), std::size_t(2));
    QCOMPARE(window.State().surveyPoints[0].id, 12);
    QCOMPARE(window.State().surveyPoints[0].elevation, 30.123456789);
    QCOMPARE(window.State().surveyPoints[0].easting + window.State().worldDocumentOriginX, 200.25);
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("exportSurveyPoints"); QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("pointFilePath")->setText(output);
      auto* apply = dialog->findChild<QPushButton*>("applyPointFile");
      QTRY_VERIFY(apply->isEnabled()); apply->click(); dialog->reject();
    });
    window.Submit("EXPORTPOINTS");
    QFile exported(output); QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(exported.readAll().contains("200.25"));
    auto* reports = window.findChild<QDockWidget*>("surveyReports"); QVERIFY(reports);
    QCOMPARE(reports->findChild<QTabWidget*>("surveyReportTabs")->count(), 1);
    const bool clipboardWorks = ClipboardRoundTrips();
    reports->findChild<QPushButton*>("copySurveyReport")->click();
    if (clipboardWorks) QVERIFY(QApplication::clipboard()->text().contains("200.25"));
    window.Submit("UNDO"); QVERIFY(window.State().surveyPoints.empty());
    window.Submit("REDO"); QCOMPARE(window.State().surveyPoints.size(), std::size_t(2));
  }

  void pointFileValidationScansEntireFileAndFailedExportPreservesData() {
    QTemporaryDir directory; const auto path = directory.filePath("large.csv");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 1; i <= 20000; ++i) file.write(QByteArray::number(i) + ",100,200,30,Survey point description\n");
    file.close();
    EditorWindow window; window.CreateDrawingFromTemplate(); auto& state = window.State();
    qstrncpy(state.surveyImportCsvPath, path.toUtf8().constData(), sizeof(state.surveyImportCsvPath));
    state.surveyImportCsvLayoutIdx = 0; state.surveyImportCsvSkipFirstRow = false;
    SurveyCsvRefreshImportPreview(state); QCOMPARE(state.surveyImportValidRowCount, 20000);
    SurveyPoint point; point.id = 1; point.easting = std::numeric_limits<double>::infinity(); state.surveyPoints.push_back(point);
    qstrncpy(state.surveyExportCsvPath, path.toUtf8().constData(), sizeof(state.surveyExportCsvPath));
    const auto size = QFileInfo(path).size(); std::vector<std::string> log;
    QVERIFY(!SurveyCsvExportFile(state, log)); QCOMPARE(QFileInfo(path).size(), size);
    QVERIFY(state.surveyReportTabs.empty());
    QCOMPARE(QDir(directory.path()).entryList({"*.tmp-*"}, QDir::Files).size(), 0);
    state.surveyPoints[0].easting = 2;
    const auto blocked = directory.filePath("blocked.csv"); QVERIFY(QDir().mkpath(blocked));
    qstrncpy(state.surveyExportCsvPath, blocked.toUtf8().constData(), sizeof(state.surveyExportCsvPath));
    QVERIFY(!SurveyCsvExportFile(state, log)); QVERIFY(QFileInfo(blocked).isDir());
    QCOMPARE(QDir(directory.path()).entryList({"*.tmp-*"}, QDir::Files).size(), 0);
    AppCommandState::ProjectSession session; session.uid = 81; session.readOnly = true;
    state.openProjects.push_back(session); state.drawingTabs[state.activeDrawingIdx].projectUid = 81;
    QVERIFY(!SurveyCsvImportFile(state, log)); QCOMPARE(state.surveyPoints.size(), std::size_t(1));
    state.drawingTabs[state.activeDrawingIdx].projectUid = 0; state.openProjects.clear();
  }

  void nativePasteWarningBlocksUnsafePasteAndCancels() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.State().pastePrompt = {true, true, false, "Coordinate systems differ", true, false};
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QMessageBox*>("pasteWarning"); QVERIFY(dialog);
      for (auto* button : dialog->buttons()) QVERIFY(button->text() != "Paste anyway");
      dialog->button(QMessageBox::Cancel)->click();
    });
    window.Submit("");
    QVERIFY(!window.State().pastePrompt.active);
    QVERIFY(!window.State().pasteWarningAnswered);
  }

  void nativePasteWarningConvertsUnitsBeforePlacement() {
    EditorWindow window; window.CreateDrawingFromTemplate(); auto& state = window.State();
    state.userLinesFlat = {0, 0, 0, 1, 1, 0};
    state.selection.push_back({SelectedEntity::Type::LineSeg, 0});
    auto* copy = Action(window, "Copy"); QVERIFY(copy); copy->trigger();
    QCOMPARE(state.clipboard.lines.size(), std::size_t(6));
    state.clipboard.srcProjectUid = 0; state.clipboard.srcZone.clear();
    state.clipboard.srcMetersPerUnit = 1.0; state.drawingSettings.zoneCode.clear(); state.drawingInsUnits = 2;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QMessageBox*>("pasteWarning"); QVERIFY(dialog);
      for (auto* button : dialog->buttons()) if (button->text() == "Convert and paste") { button->click(); return; }
      dialog->button(QMessageBox::Cancel)->click(); QFAIL("Missing clipboard conversion action");
    });
    window.Submit("PASTECLIP");
    QCOMPARE(state.active, AppCommandState::Kind::Paste);
    QVERIFY(state.clipboard.lines[3] > 3.28f); QVERIFY(state.clipboard.lines[3] < 3.29f);
    QVERIFY(!state.pastePrompt.active); QVERIFY(!state.pasteWarningAnswered);
  }

  void releaseRibbonHasIconsAndReachableCollapsedActions() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show();
    auto* ribbon = window.findChild<QTabWidget*>("ribbon"); QVERIFY(ribbon);
    for (const auto& name : {"Home", "Insert", "Annotate", "View", "Manage", "Output", "Survey", "Modeling"})
      QVERIFY(window.findChild<QWidget*>(QString("ribbonPage_%1").arg(name)));
    int buttons = 0;
    for (const auto& entry : qtribbon::kButtons) {
      auto* action = window.findChild<QAction*>("ribbon_" + QString::fromUtf8(entry.id).mid(2));
      QVERIFY2(action, entry.id); QVERIFY2(!action->icon().isNull(), entry.icon);
      QCOMPARE(action->property("releasePlaceholder").toBool(), entry.placeholder);
      if (entry.placeholder) QVERIFY(!action->isEnabled());
      if (action->property("nativeDialogPending").toBool()) {
        QVERIFY(!action->isEnabled()); QVERIFY(action->toolTip().contains("dialog is not available"));
      }
      ++buttons;
    }
    QCOMPARE(buttons, 285);
    window.resize(850, 650); QTest::qWait(100);
    QVERIFY(ribbon->width() > 500); QVERIFY(ribbon->width() <= window.width());
    auto* page = window.findChild<QWidget*>("ribbonPage_Home"); QVERIFY(page);
    bool collapsed = false;
    for (auto* button : page->findChildren<QToolButton*>()) if (button->objectName().startsWith("collapsed_") && button->isVisible()) {
      collapsed = true; QVERIFY(button->menu());
      QVERIFY(button->mapTo(ribbon, QPoint(button->width(), 0)).x() <= ribbon->width());
    }
    QVERIFY(collapsed);
    window.Submit("CIRCLE");
    auto* line = window.findChild<QAction*>("ribbon_RibbonLine"); QVERIFY(line);
    line->trigger(); QCOMPARE(window.State().active, AppCommandState::Kind::Line);
    QVERIFY(line->associatedObjects().size() >= 2); // Same action in the panel and its native overflow menu.
    auto* draw = window.findChild<QToolButton*>("collapsed_ribbonPanel_Home_Draw"); QVERIFY(draw); QVERIFY(draw->menu()->actions().contains(line));
  }

  void compactRibbonDropdownButtonsOpenAtMinimumWindowWidth() {
    const auto oldPalette = qApp->palette(); const auto oldFont = qApp->font();
    const auto oldSheet = qApp->styleSheet(); const auto oldStyle = qApp->style()->objectName();
    const auto oldDefault = qApp->property("defaultQtStyle"), oldLight = qApp->property("cadLightTheme");
    auto restoreAppearance = qScopeGuard([&] {
      qApp->setStyle(oldStyle); qApp->setPalette(oldPalette); qApp->setFont(oldFont); qApp->setStyleSheet(oldSheet);
      qApp->setProperty("defaultQtStyle", oldDefault); qApp->setProperty("cadLightTheme", oldLight);
    });
    ApplyQtAppearance(*qApp, false);
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.resize(1200, 800); window.show(); QTest::qWait(100);
    window.resize(window.minimumSize()); QTest::qWait(100);
    auto* tabMenuButton = window.findChild<QToolButton*>("ribbonTabMenu"); QVERIFY(tabMenuButton);
    QVERIFY(tabMenuButton->menu()); QVERIFY(!tabMenuButton->menu()->actions().empty());
    QPointer<QAction> firstTabAction = tabMenuButton->menu()->actions().front();
    // Repeated size changes inside the same compact state must not destroy and
    // reconnect every tab action on each resize event.
    window.resize(window.width() + 2, window.height()); QCoreApplication::processEvents();
    QVERIFY(firstTabAction);
    auto* page = window.findChild<QWidget*>("ribbonPage_Home"); QVERIFY(page);
    QList<QToolButton*> visibleMenus;
    for (auto* button : page->findChildren<QToolButton*>())
      if (button->objectName().startsWith("collapsed_") && button->isVisible() && button->menu())
        visibleMenus.append(button);
    auto* more = page->findChild<QToolButton*>("ribbonMore");
    if (more && more->isVisible() && more->menu()) visibleMenus.append(more);
    QVERIFY(!visibleMenus.empty());
    QVERIFY(window.grab().save(QtTestArtifactDirectory() + "/qt-1035-ribbon-minimum-width.png"));
    for (auto* button : visibleMenus) {
      QVERIFY(!button->menu()->actions().empty());
      bool opened = false;
      connect(button->menu(), &QMenu::aboutToShow, &window, [&opened] { opened = true; });
      for (const QPoint position : {button->rect().center(), QPoint(button->width() / 2, button->height() - 5)}) {
        opened = false;
        const bool capture = button->objectName() == "collapsed_ribbonPanel_Home_Draw" &&
                             position == button->rect().center();
        QTimer::singleShot(0, button->menu(), [menu = button->menu(), capture] {
          if (capture) menu->grab().save(QtTestArtifactDirectory() + "/qt-1035-ribbon-minimum-width-menu.png");
          menu->hide();
        });
        QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, position);
        QVERIFY2(opened, qPrintable(button->objectName()));
      }
    }
    auto* draw = page->findChild<QToolButton*>("collapsed_ribbonPanel_Home_Draw"); QVERIFY(draw); QVERIFY(draw->isVisible());
    QAction* line = nullptr;
    for (auto* action : draw->menu()->actions())
      if (action->text().contains("Line", Qt::CaseInsensitive)) { line = action; break; }
    QVERIFY(line);
    bool drawMenuOpened = false;
    connect(draw->menu(), &QMenu::aboutToShow, &window, [&drawMenuOpened] { drawMenuOpened = true; });
    QTimer::singleShot(0, draw->menu(), [menu = draw->menu(), line] {
      line->trigger();
      menu->hide();
    });
    QTest::mouseClick(draw, Qt::LeftButton, Qt::NoModifier, draw->rect().center());
    QVERIFY(drawMenuOpened);
    QTRY_COMPARE(window.State().active, AppCommandState::Kind::Line);
    window.CancelDraftingInput();
  }

  void ribbonPanelCaptionsHaveBottomSpaceWithoutResizingButtons() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1400, 800); window.show();
    QTest::qWait(100);
    auto* ribbon = window.findChild<QTabWidget*>("ribbon"); QVERIFY(ribbon);
    auto* page = window.findChild<QWidget*>("ribbonPage_Home"); QVERIFY(page);
    auto* panel = window.findChild<QWidget*>("ribbonPanel_Home_Palettes"); QVERIFY(panel);
    auto* caption = panel->findChild<QLabel*>("ribbonPanelLabel"); QVERIFY(caption);
    auto* toolspace = panel->findChild<QToolButton*>("tool_RibbonToolspaceHome"); QVERIFY(toolspace);

    QCOMPARE(panel->height(), 98);
    QVERIFY(panel->height() - caption->geometry().bottom() - 1 >= 4);
    QCOMPARE(toolspace->minimumWidth(), 58);
    QCOMPARE(toolspace->height(), 73);
    QCOMPARE(page->height(), 102);
    QCOMPARE(ribbon->height(), 130);

    window.resize(850, 650); QTest::qWait(100);
    QVERIFY(ribbon->width() <= window.width());
    QVERIFY(page->width() <= ribbon->width());
    for (auto* scrollBar : ribbon->findChildren<QScrollBar*>()) QVERIFY(!scrollBar->isVisible());
  }

  void releaseRibbonContextTracksSelectionAndRestoresNormalTab() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show();
    auto* ribbon = window.findChild<QTabWidget*>("ribbon"); auto& state = window.State();
    auto index = [&](const char* name) { return ribbon->indexOf(window.findChild<QWidget*>(QString("ribbonPage_%1").arg(name))); };
    ribbon->setCurrentIndex(index("Modeling"));
    QVERIFY(!ribbon->isTabVisible(index("SurveyPointCtx")));
    SurveyPoint point; point.id = 42; state.surveyPoints.push_back(point); state.selectedSurveyPointIndices = {0};
    window.Submit(""); QVERIFY(ribbon->isTabVisible(index("SurveyPointCtx")));
    QCOMPARE(ribbon->currentIndex(), index("SurveyPointCtx")); QVERIFY(ribbon->tabText(index("SurveyPointCtx")).contains("42"));
    state.selectedSurveyPointIndices.clear(); window.Submit("");
    QVERIFY(!ribbon->isTabVisible(index("SurveyPointCtx"))); QCOMPARE(ribbon->currentIndex(), index("Modeling"));
    state.cadSurfaces.emplace_back(); state.cadSurfaces.back().name = "Existing Ground";
    state.selection.push_back({SelectedEntity::Type::Surface, 0}); window.Submit("");
    QCOMPARE(ribbon->currentIndex(), index("SurfaceCtx")); QVERIFY(ribbon->tabText(index("SurfaceCtx")).contains("Existing Ground"));
    state.selection.clear(); window.Submit(""); QCOMPARE(ribbon->currentIndex(), index("Modeling"));
    state.drawingSettings.zoneCode = "TX83-CF"; window.Submit(""); QVERIFY(ribbon->isTabVisible(index("GeolocationCtx")));
    QCOMPARE(ribbon->currentIndex(), index("Modeling")); // A location does not steal the active tab.
  }

  void issue899DrawingSettingsAndGeolocationRibbonUseSharedWorkflows() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    auto& state = window.State();
    if (!geo::DictionariesLoaded())
      QVERIFY(geo::LoadDictionaries(ResolveBundledAssetPath(std::filesystem::path("resources") / "csmap").string()));
    state.drawingSettings.transform.apply = false;
    const QString artifacts = QtTestArtifactDirectory() + "/";

    QTimer::singleShot(0, &window, [&] {
      auto* settings = window.findChild<QDialog*>("drawingSettingsDialog"); QVERIFY(settings);
      auto* navigation = settings->findChild<QListWidget*>("drawingSettingsNavigation"); QVERIFY(navigation);
      auto* pages = settings->findChild<QStackedWidget*>("drawingSettingsPages"); QVERIFY(pages);
      QVERIFY(settings->grab().save(artifacts + "qt-899-settings-units-zone.png"));
      navigation->setCurrentRow(1); QApplication::processEvents();
      QVERIFY(settings->grab().save(artifacts + "qt-899-settings-transformation.png"));
      navigation->setCurrentRow(2); QApplication::processEvents();
      QVERIFY(settings->grab().save(artifacts + "qt-899-settings-object-layers.png"));
      auto* table = settings->findChild<QTableWidget*>("drawingObjectLayers"); QVERIFY(table);
      QCOMPARE(table->columnCount(), 4);
      auto* lock = table->cellWidget(0, 3)->findChild<QCheckBox*>(); QVERIFY(lock);
      lock->setChecked(true);
      auto* apply = settings->findChild<QPushButton*>("drawingSettingsApply"); QVERIFY(apply);
      QTest::mouseClick(apply, Qt::LeftButton);
      settings->close();
    });
    window.ShowUnits();
    QVERIFY(state.drawingSettings.objectLayers[0].locked);
    window.show(); QTest::qWait(100);

    state.drawingSettings.zoneCode.clear();
    state.drawingSettings.onlineMap = DrawingSettings::OnlineMap::Off;
    window.Submit("");
    QVERIFY(!window.findChild<QDockWidget*>("mapTaskDock"));
    QVERIFY(!window.findChild<QWidget*>("mapTaskPanel"));
    auto* ribbon = window.findChild<QTabWidget*>("ribbon"); QVERIFY(ribbon);
    const int geoTab = ribbon->indexOf(window.findChild<QWidget*>("ribbonPage_GeolocationCtx")); QVERIFY(geoTab >= 0);
    QVERIFY(!ribbon->isTabVisible(geoTab));
    QVERIFY(window.grab().save(artifacts + "qt-1035-no-geolocation-ribbon.png"));
    state.drawingSettings.zoneCode = "HARN/TX.TX-CF";
    window.Submit("");
    QVERIFY(ribbon->isTabVisible(geoTab));
    QVERIFY(geoTab > ribbon->indexOf(window.findChild<QWidget*>("ribbonPage_Modeling")));
    ribbon->setCurrentIndex(geoTab); QApplication::processEvents();
    QVERIFY(window.grab().save(artifacts + "qt-1035-geolocation-ribbon.png"));
    auto* wrapper = window.findChild<QWidget*>("ribbonWrapper"); QVERIFY(wrapper);
    auto* nav = window.findChild<QWidget*>("ribbonTabMenuHeader"); QVERIFY(nav);
    auto* tabMenu = window.findChild<QToolButton*>("ribbonTabMenu"); QVERIFY(tabMenu); QVERIFY(tabMenu->menu());
    auto* sharedLayers = window.findChild<QWidget*>("ribbonSharedArea"); QVERIFY(sharedLayers);
    wrapper->setFixedWidth(700); QTest::qWait(100);
    QVERIFY(nav->isVisible()); QVERIFY(!ribbon->tabBar()->isVisible());
    QVERIFY(!sharedLayers->isVisible());
    auto* compactLayerButton = window.findChild<QToolButton*>("ribbonCompactLayers"); QVERIFY(compactLayerButton); QVERIFY(compactLayerButton->isVisible());
    QVERIFY(compactLayerButton->menu());
    compactLayerButton->menu()->show(); QApplication::processEvents();
    QVERIFY(compactLayerButton->menu()->findChildren<QComboBox*>().size() >= 2);
    QVERIFY(compactLayerButton->menu()->grab().save(artifacts + "qt-1035-contextual-tabs-layers-menu.png"));
    compactLayerButton->menu()->hide();
    ribbon->setCurrentIndex(ribbon->indexOf(window.findChild<QWidget*>("ribbonPage_Home")));
    tabMenu->menu()->show(); QApplication::processEvents();
    auto* geoMenuAction = tabMenu->menu()->findChild<QAction*>("ribbonTabMenuAction_GeolocationCtx"); QVERIFY(geoMenuAction);
    QCOMPARE(tabMenu->menu()->actions().last(), geoMenuAction);
    geoMenuAction->trigger(); QCOMPARE(ribbon->currentIndex(), geoTab);
    tabMenu->menu()->hide();
    tabMenu->menu()->show(); QApplication::processEvents();
    geoMenuAction = tabMenu->menu()->findChild<QAction*>("ribbonTabMenuAction_GeolocationCtx"); QVERIFY(geoMenuAction);
    QVERIFY(geoMenuAction->isChecked());
    QVERIFY(tabMenu->menu()->grab().save(artifacts + "qt-1035-contextual-tabs-menu-open.png"));
    tabMenu->menu()->hide();
    QVERIFY(wrapper->grab().save(artifacts + "qt-1035-contextual-tabs-compact-menu.png"));
    wrapper->setMinimumWidth(0); wrapper->setMaximumWidth(QWIDGETSIZE_MAX);
    window.resize(1600, 800); QTest::qWait(100);
    QVERIFY(!nav->isVisible()); QVERIFY(ribbon->tabBar()->isVisible());
    QCOMPARE(ribbon->currentIndex(), geoTab);
    auto* mapAction = window.findChild<QAction*>("ribbon_GeoMap"); QVERIFY(mapAction);
    auto* mapMenu = mapAction->menu(); QVERIFY(mapMenu);
    QAction* imagery = nullptr;
    for (auto* option : mapMenu->actions())
      if (option->property("ribbonCommand").toString() == QString("@map:%1").arg(static_cast<int>(DrawingSettings::OnlineMap::UsgsImagery))) imagery = option;
    QVERIFY(imagery); QVERIFY(imagery->isEnabled());
    imagery->trigger();
    QCOMPARE(state.drawingSettings.onlineMap, DrawingSettings::OnlineMap::UsgsImagery);
    auto* captureAction = window.findChild<QAction*>("ribbon_GeoCaptureArea"); QVERIFY(captureAction);
    auto* captureMenu = captureAction->menu(); QVERIFY(captureMenu);
    QAction* capture = nullptr;
    for (auto* option : captureMenu->actions())
      if (option->property("ribbonCommand").toString() == "@captureArea") capture = option;
    QVERIFY(capture); QVERIFY(capture->isEnabled());
    capture->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::GeoCaptureArea);
    QVERIFY(window.grab().save(artifacts + "qt-1035-geolocation-ribbon-capture.png"));
    std::vector<std::string> log;
    CancelActiveCommand(state, log);
    QCOMPARE(state.active, AppCommandState::Kind::None);
  }

  void issue1083CoordinateSystemDictionaryLoadsWithTheWindow() {
    // Issue #1083: the zone, grid readouts and the online map need the dictionary before Drawing
    // Settings has ever been opened. Start from an unloaded dictionary, as a fresh session does.
    QVERIFY(!geo::LoadDictionaries("C:/no/such/folder"));
    QVERIFY(!geo::DictionariesLoaded());
    EditorWindow window;
    QVERIFY2(geo::DictionariesLoaded(), geo::DictionaryError().c_str());
    window.CreateDrawingFromTemplate();
    auto& state = window.State();
    state.drawingInsUnits = 2;
    state.drawingSettings.zoneCode = "HARN/TX.TX-CF";
    const auto factors = ResolveDrawingTransform(state.drawingSettings, state.drawingInsUnits, false);
    QVERIFY2(factors.ok, factors.error.c_str());
  }

  void drawingSettingsFileMenuLoadsCoordinateSystemCategories() {
    EditorWindow window;
    auto* fileMenu = window.findChild<QMenu*>("fileMenu"); QVERIFY(fileMenu);
    auto* drawingSettings = window.findChild<QAction*>("command_DRAWINGSETTINGS"); QVERIFY(drawingSettings);
    QVERIFY(fileMenu->actions().contains(drawingSettings));
    QVERIFY(!drawingSettings->isEnabled());
    window.CreateDrawingFromTemplate();
    QVERIFY(drawingSettings->isEnabled());

    QVERIFY(!geo::LoadDictionaries("C:/no/such/folder"));
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingSettingsDialog"); QVERIFY(dialog);
      auto closeOnFailure = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
      auto saveDropdown = [dialog](QWidget* popup, const QString& path) {
        QPixmap capture = dialog->grab();
        const QPoint offset = popup->mapToGlobal(QPoint(0, 0)) - dialog->mapToGlobal(QPoint(0, 0));
        QPainter painter(&capture);
        painter.drawPixmap(offset, popup->grab());
        return capture.save(path);
      };
      auto* category = dialog->findChild<QComboBox*>("drawingZoneCategory"); QVERIFY(category);
      QVERIFY(category->count() > 2);
      QCOMPARE(category->style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, category), 0);
      QVERIFY(!category->view()->hasAutoScroll());
      QCOMPARE(category->view()->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
      category->showPopup(); QApplication::processEvents();
      auto* categoryPopup = category->view()->window();
      QTRY_VERIFY_WITH_TIMEOUT(categoryPopup->isVisible(), 2000);
      QVERIFY(categoryPopup->frameGeometry().top() >= category->mapToGlobal(QPoint(0, category->height())).y() - 2);
      QVERIFY(categoryPopup->frameGeometry().left() == category->mapToGlobal(QPoint(0, 0)).x());
      QVERIFY(categoryPopup->height() <= 240);
      QVERIFY(saveDropdown(categoryPopup, QtTestArtifactDirectory() + "/qt-1035-coordinate-category-dropdown.png"));
      category->hidePopup();
      QApplication::processEvents();
      const int texas = category->findData("USA, Texas"); QVERIFY(texas >= 0);
      category->setCurrentIndex(texas);
      auto* systems = dialog->findChild<QComboBox*>("drawingZoneSystem"); QVERIFY(systems);
      QVERIFY(!systems->view()->hasAutoScroll());
      QCOMPARE(systems->style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, systems), 0);
      const int central = systems->findData("HARN/TX.TX-C"); QVERIFY(central >= 0);
      systems->showPopup(); QApplication::processEvents();
      auto* systemsPopup = systems->view()->window();
      QTRY_VERIFY_WITH_TIMEOUT(systemsPopup->isVisible(), 2000);
      QVERIFY(systemsPopup->frameGeometry().top() >= systems->mapToGlobal(QPoint(0, systems->height())).y() - 2);
      QVERIFY(systemsPopup->frameGeometry().left() == systems->mapToGlobal(QPoint(0, 0)).x());
      QVERIFY(systemsPopup->height() <= 240);
      QVERIFY(saveDropdown(systemsPopup, QtTestArtifactDirectory() + "/qt-1035-coordinate-system-dropdown.png"));
      systems->hidePopup();
      systems->setCurrentIndex(central);
      auto* code = dialog->findChild<QLineEdit*>("drawingZoneCode"); QVERIFY(code);
      QCOMPARE(code->text(), QString("HARN/TX.TX-C"));
      const QString artifacts = QtTestArtifactDirectory() + "/";
      QVERIFY(dialog->grab().save(artifacts + "qt-1035-drawing-settings-coordinate-systems.png"));
      dialog->reject();
    });
    drawingSettings->trigger();
  }

  void drawingSettingsZoneDetailsDescribeTheDrawingsZoneOnOpen() {
    QVERIFY(geo::LoadDictionaries(ResolveBundledAssetPath(std::filesystem::path("resources") / "csmap").string()));
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.State().drawingSettings.zoneCode = "HARN/NB.NB";
    auto* drawingSettings = window.findChild<QAction*>("command_DRAWINGSETTINGS"); QVERIFY(drawingSettings);
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("drawingSettingsDialog"); QVERIFY(dialog);
      auto closeOnFailure = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
      auto* systems = dialog->findChild<QComboBox*>("drawingZoneSystem"); QVERIFY(systems);
      auto* code = dialog->findChild<QLineEdit*>("drawingZoneCode"); QVERIFY(code);
      auto* details = dialog->findChild<QLabel*>("drawingZoneDetails"); QVERIFY(details);
      QCOMPARE(systems->currentData().toString(), QString("HARN/NB.NB"));
      QCOMPARE(code->text(), QString("HARN/NB.NB"));
      const auto info = geo::FindCoordinateSystem("HARN/NB.NB"); QVERIFY(info);
      QCOMPARE(details->text(), QString("%1\nProjection: %2\nDatum: %3").arg(QString::fromUtf8(info->description.c_str()),
               QString::fromUtf8(info->projection.c_str()), QString::fromUtf8(info->datum.c_str())));
      QVERIFY(!details->text().contains("NAD27"));
      QVERIFY(dialog->grab().save(QtTestArtifactDirectory() + "/qt-1085-drawing-settings-zone-details.png"));
      dialog->reject();
    });
    drawingSettings->trigger();
  }

  void releaseRibbonSelectorsAndFittingMenuReachSharedServices() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1500, 950); window.show(); auto& state = window.State();
    if (auto* viewPage = window.findChild<QWidget*>("ribbonPage_View")) {
      if (auto* ribbon = window.findChild<QTabWidget*>("ribbon")) ribbon->setCurrentWidget(viewPage);
      QApplication::processEvents();
      const QString artifact = QtTestArtifactDirectory() + "/";
      for (const auto& panelName : {QString("View"), QString("Named Views"), QString("Coordinates")}) {
        auto* panel = viewPage->findChild<QWidget*>("ribbonPanel_View_" + panelName);
        QVERIFY(panel);
        QVERIFY(panel->grab().save(artifact + "qt-892-view-ribbon-" + panelName.toLower().replace(' ', '-') + ".png"));
      }
    }
    auto* style = window.findChild<QComboBox*>("ribbonVisualStyle"); QVERIFY(style);
    style->setCurrentIndex(2); QMetaObject::invokeMethod(style, "activated", Q_ARG(int, 2));
    QCOMPARE(state.viewportVisualStyle, VisualStyle::Shaded);
    auto* projection = window.findChild<QComboBox*>("ribbonProjection"); QVERIFY(projection);
    projection->setCurrentIndex(1); QMetaObject::invokeMethod(projection, "activated", Q_ARG(int, 1));
    QCOMPARE(state.viewportProjection, Camera::Projection::Perspective);
    auto* fov = window.findChild<QSpinBox*>("ribbonFov"); QVERIFY(fov); QVERIFY(fov->isEnabled());
    fov->setValue(75); QCOMPARE(state.viewportFovDeg, 75.f);
    window.Submit("PERSPECTIVE sideways"); QCOMPARE(state.viewportProjection, Camera::Projection::Perspective);
    window.Submit("FOV 180"); QCOMPARE(state.viewportFovDeg, 75.f);
    window.Submit("VIEW S Qt Perspective"); QCOMPARE(state.namedViews.size(), std::size_t(1));
    QCOMPARE(state.namedViews[0].projection, Camera::Projection::Perspective);
    AppCommandState reopened; std::vector<std::string> ioLog;
    QVERIFY(LoadGoSurveyFromJsonUtf8(reopened, SerializeGoSurveyJson(state), ioLog));
    QCOMPARE(reopened.viewportProjection, Camera::Projection::Perspective);
    QCOMPARE(reopened.viewportFovDeg, 75.f);
    QCOMPARE(reopened.namedViews.size(), std::size_t(1));
    QCOMPARE(reopened.namedViews[0].fovDeg, 75.f);
    auto* annotationScale = window.findChild<QComboBox*>("statusAnnotationScale"); QVERIFY(annotationScale);
    state.annotationScales = {{"1:50", 1.f, 50.f}, {"1:100", 1.f, 100.f}};
    window.Refresh(); QCOMPARE(annotationScale->count(), 2); QCOMPARE(annotationScale->currentIndex(), -1);
    annotationScale->setCurrentIndex(1); QMetaObject::invokeMethod(annotationScale, "activated", Q_ARG(int, 1));
    QCOMPARE(state.currentAnnotationScaleIndex, 1);
    auto* ucs = window.findChild<QComboBox*>("ribbonUcs"); QVERIFY(ucs);
    ucs->setCurrentIndex(3); QMetaObject::invokeMethod(ucs, "activated", Q_ARG(int, 3));
    QVERIFY(ucs::FramesMatch(state.activeUcs, ucs::OrthographicPresets()[2].frame));
    auto* fitting = window.findChild<QAction*>("ribbon_ModPipeFit"); QVERIFY(fitting); QVERIFY(fitting->menu());
    QCOMPARE(fitting->menu()->actions().size(), 9);
    QCOMPARE(fitting->menu()->actions().front()->property("ribbonCommand").toString(), QString("PIPEFIT elbow-90"));
    auto* layers = window.findChild<QToolButton*>("collapsed_ribbonPanel_Home_Layers"); QVERIFY(layers);
    QVERIFY(!layers->menu()->findChildren<QComboBox*>().empty());
    auto* size = window.findChild<QComboBox*>("ribbonPipeSize"); QVERIFY(size);
    size->setCurrentText("2in"); QMetaObject::invokeMethod(size, "textActivated", Q_ARG(QString, QString("2in")));
    QCOMPARE(state.pipeRunNominalSize, std::string("2in"));
    state.drawingInsUnits = 2;
    window.findChild<QAction*>("ribbon_ModPipeRun")->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::PipeRun);
    QCOMPARE(state.pipeRunPhase, AppCommandState::PipeRunPhase::WaitFirstPoint);
    QVERIFY(!size->isEnabled());
  }

  void releaseRibbonPipeNetworkRunsAndUndoesCompleteWorkflow() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto& state = window.State();
    const QString artifacts = QtTestArtifactDirectory() + "/";
    auto* groundPanel = window.findChild<QWidget*>("ribbonPanel_Home_Create Ground Data"); QVERIFY(groundPanel);
    auto* designPanel = window.findChild<QWidget*>("ribbonPanel_Home_Create Design"); QVERIFY(designPanel);
    QVERIFY(groundPanel->grab().save(artifacts + "qt-896-create-ground-data.png"));
    QVERIFY(designPanel->grab().save(artifacts + "qt-896-create-design.png"));
    auto* action = window.findChild<QAction*>("ribbon_CdPipeNetwork"); QVERIFY(action);
    QVERIFY(action->isEnabled());
    QCOMPARE(action->property("ribbonCommand").toString(), QString("PIPERUN"));

    action->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::PipeRun);
    QCOMPARE(state.pipeRunPhase, AppCommandState::PipeRunPhase::WaitNominalSize);
    auto* commandBar = window.findChild<QWidget*>("floatingCommandBar"); QVERIFY(commandBar);
    QApplication::processEvents();
    QVERIFY(commandBar->grab().save(artifacts + "qt-896-piperun-prompt.png"));
    const auto runsBefore = state.cadPipeRuns.size();
    window.Submit("not-a-size");
    QCOMPARE(state.pipeRunPhase, AppCommandState::PipeRunPhase::WaitNominalSize);
    QCOMPARE(state.cadPipeRuns.size(), runsBefore);
    auto* historyToggle = window.findChild<QToolButton*>("commandHistoryToggle"); QVERIFY(historyToggle);
    historyToggle->click();
    QApplication::processEvents();
    auto* commandConsole = window.findChild<QPlainTextEdit*>("commandConsole"); QVERIFY(commandConsole);
    QVERIFY(commandConsole->isVisible());
    QVERIFY(commandConsole->grab().save(artifacts + "qt-896-piperun-validation.png"));

    window.Submit("2in");
    QCOMPARE(state.pipeRunPhase, AppCommandState::PipeRunPhase::WaitWallThickness);
    window.Submit(""); // Schedule 40 default wall.
    QCOMPARE(state.pipeRunPhase, AppCommandState::PipeRunPhase::WaitFirstPoint);
    window.Submit("0,0,0");
    QCOMPARE(state.pipeRunPhase, AppCommandState::PipeRunPhase::WaitNextPoint);
    window.Submit("10,0,0");
    window.Submit("END");
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.cadPipeRuns.size(), runsBefore + 1);

    window.Submit("UNDO");
    QCOMPARE(state.cadPipeRuns.size(), runsBefore);

    action->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::PipeRun);
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus();
    QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.cadPipeRuns.size(), runsBefore);
  }

private:
  // Issue #1084 / REQ-350: the Pipe Fittings palette shows in the Qt editor. Names of the rows a tab lists.
  static QStringList PipePaletteRows(QDialog* palette, int tab) {
    QStringList names;
    auto* list = palette->findChild<QListWidget*>(QString("pipeFittingList%1").arg(tab));
    if (!list || !list->isVisible()) return names;
    for (int i = 0; i < list->count(); ++i) names << list->item(i)->data(Qt::UserRole).toString();
    return names;
  }
  // A real part picture, not the empty placeholder box: it has drawn pixels well inside its border.
  static bool PipePaletteRowHasPicture(QListWidget* list, int row) {
    const QImage image = list->item(row)->icon().pixmap(64, 64).toImage().convertToFormat(QImage::Format_ARGB32);
    int drawn = 0;
    for (int y = 8; y < 56; ++y)
      for (int x = 8; x < 56; ++x)
        if (qAlpha(image.pixel(x, y)) > 0) ++drawn;
    return drawn > 150;
  }

private Q_SLOTS:
  void issue1084PipeRunOpensPaletteWithSizeMatchedParts() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto& state = window.State();
    QVERIFY(!window.findChild<QDialog*>("pipeFittingPalette"));

    window.Submit("PIPERUN");  // opens the palette by itself (REQ-350 (a))
    QVERIFY(state.pipeFittingPaletteOpen);
    auto* palette = window.findChild<QDialog*>("pipeFittingPalette");
    QVERIFY(palette);
    QVERIFY(palette->isVisible());
    QVERIFY(!palette->isModal());  // a palette, never a modal
    window.Submit("2in");
    window.Submit("");  // schedule 40
    auto* tabs = palette->findChild<QTabWidget*>("pipeFittingTabs");
    QVERIFY(tabs);
    QCOMPARE(tabs->tabPosition(), QTabWidget::East);
    QCOMPARE(tabs->count(), 5);
    QCOMPARE(tabs->tabText(0), QString("Fittings"));
    QCOMPARE(tabs->tabText(1), QString("Flanges"));
    tabs->setCurrentIndex(1);

    // A 2in run lists the two 2in flanges and not the 4in one (size match is exact).
    const QStringList twoInch = PipePaletteRows(palette, 1);
    QVERIFY(twoInch.contains("2IN_BLIND_FLANGE"));
    QVERIFY(twoInch.contains("2in_WELD_NECK_FLANGE"));
    QVERIFY(!twoInch.contains("CJ_4in_WELD_NECK_FLANGE"));
    QCOMPARE(palette->findChild<QLabel*>("pipeFittingRun")->text(), QString("Pipe run: 2in"));

    // Route something and finish: the palette stays open after the run is committed.
    window.Submit("0,0,0");
    window.Submit("10,0,0");
    window.Submit("END");
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(palette->isVisible());

    // A 4in run re-filters the same window, live.
    window.Submit("PIPERUN");
    window.Submit("4in");
    QCOMPARE(window.findChild<QDialog*>("pipeFittingPalette"), palette);
    QCOMPARE(PipePaletteRows(palette, 1), QStringList{"CJ_4in_WELD_NECK_FLANGE"});

    // A category with nothing at this size says so, naming the size.
    tabs->setCurrentIndex(2);
    auto* empty = palette->findChild<QLabel*>("pipeFittingEmpty2");
    QVERIFY(empty && empty->isVisible());
    QVERIFY(empty->text().contains("4in"));
    QVERIFY(empty->text().contains("valves"));
    QVERIFY(PipePaletteRows(palette, 2).isEmpty());
    QCOMPARE(state.pipeFittingPaletteTab, 2);

    // Cancel the run in progress (the palette stays open), then: the close box closes it and the
    // reopen command shows it again where it was.
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus();
    QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(palette->isVisible());
    QVERIFY(state.pipeFittingPaletteOpen);
    const QPoint before = palette->pos();
    palette->close();
    QVERIFY(!palette->isVisible());
    QVERIFY(!state.pipeFittingPaletteOpen);
    window.Submit("PIPEPALETTE");
    QVERIFY(state.pipeFittingPaletteOpen);
    QCOMPARE(window.findChild<QDialog*>("pipeFittingPalette"), palette);
    QVERIFY(palette->isVisible());
    QCOMPARE(palette->pos(), before);
    window.Submit("PIPEPALETTE");  // the same command closes it again
    QVERIFY(!palette->isVisible());
  }

  void issue1084PaletteRowsShowShadedPicturesAndArmThePart() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(window.m_Viewport->IsRendererReady(), 10000);
    auto& state = window.State();
    window.Submit("PIPERUN");
    window.Submit("2in");
    auto* palette = window.findChild<QDialog*>("pipeFittingPalette");
    QVERIFY(palette);
    palette->findChild<QTabWidget*>("pipeFittingTabs")->setCurrentIndex(1);
    auto* list = palette->findChild<QListWidget*>("pipeFittingList1");
    QVERIFY(list && list->isVisible());
    QCOMPARE(list->count(), 2);

    // Each row's picture is rendered once by the renderer and then shown.
    for (int row = 0; row < list->count(); ++row)
      QTRY_VERIFY_WITH_TIMEOUT(PipePaletteRowHasPicture(list, row), 10000);
    QCOMPARE(window.m_Viewport->PartThumbnails().size(), std::size_t(2));
    QVERIFY(list->item(0)->text().contains('\n'));  // the name, with size and type beneath

    // Picking a row arms the part for the next viewport click and does not place anything yet.
    const QString name = list->item(0)->data(Qt::UserRole).toString();
    const auto refsBefore = state.cadBlockRefs.size();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
    QCOMPARE(state.active, AppCommandState::Kind::InsertBlock);
    QCOMPARE(QString::fromUtf8(state.insertBlockName), name);
    QVERIFY(state.insertBlockPipeSpliceArmed);
    QCOMPARE(state.cadBlockRefs.size(), refsBefore);

    // A click off every run places it as an ordinary block INSERT.
    window.Submit("500,500,0");  // the same placement a viewport click makes, typed
    QCOMPARE(state.cadBlockRefs.size(), refsBefore + 1);
    QCOMPARE(QString::fromUtf8(state.cadBlockRefs.back().defName.c_str()), name);
    QVERIFY(!state.insertBlockPipeSpliceArmed);

    // ESC disarms an armed part.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(1)).center());
    QCOMPARE(state.active, AppCommandState::Kind::InsertBlock);
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus();
    QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.cadBlockRefs.size(), refsBefore + 1);
  }

  void qt1084PipeFittingPaletteScreenshots() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.m_Viewport->IsRendererReady(), 10000);
    const QString artifacts = QtTestArtifactDirectory() + "/";
    window.Submit("PIPERUN");
    window.Submit("2in");
    auto* palette = window.findChild<QDialog*>("pipeFittingPalette");
    QVERIFY(palette);
    auto* tabs = palette->findChild<QTabWidget*>("pipeFittingTabs");
    auto shoot = [&](const QString& file) {
      QApplication::processEvents();
      QVERIFY(palette->grab().save(artifacts + file));
    };
    // Before any picture exists: name-only rows with placeholder boxes.
    tabs->setCurrentIndex(1);
    auto* flanges = palette->findChild<QListWidget*>("pipeFittingList1");
    QVERIFY(flanges);
    for (int row = 0; row < flanges->count(); ++row)
      QTRY_VERIFY_WITH_TIMEOUT(PipePaletteRowHasPicture(flanges, row), 10000);
    shoot("qt-1084-palette-2in-flanges.png");
    tabs->setCurrentIndex(2);
    shoot("qt-1084-palette-2in-valves-empty.png");
    window.Submit("");
    window.Submit("0,0,0");
    window.Submit("10,0,0");
    window.Submit("END");
    window.Submit("PIPERUN");
    window.Submit("4in");
    tabs->setCurrentIndex(1);
    auto* flanges4 = palette->findChild<QListWidget*>("pipeFittingList1");
    for (int row = 0; row < flanges4->count(); ++row)
      QTRY_VERIFY_WITH_TIMEOUT(PipePaletteRowHasPicture(flanges4, row), 10000);
    shoot("qt-1084-palette-4in-flanges.png");
    QVERIFY(window.grab().save(artifacts + "qt-1084-palette-in-window.png"));
  }

  void issue1002RibbonExportsLandXmlThroughTheCommand() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto* action = window.findChild<QAction*>("ribbon_RibbonExpLandXml"); QVERIFY(action);
    QVERIFY(action->isEnabled());
    QCOMPARE(action->property("ribbonCommand").toString(), QString("EXPORTLANDXML"));

    SurveyPoint point; point.id = 5; point.easting = 10; point.northing = 20; point.elevation = 30; point.description = "Hub";
    window.State().surveyPoints.push_back(point);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString path = directory.filePath("exported.xml");
    // The typed form carries its destination, so no file dialog opens; the ribbon's bare form asks first.
    window.Submit("EXPORTLANDXML \"" + QDir::toNativeSeparators(path) + "\"");
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();
    QVERIFY(xml.contains("LandXML-1.2"));
    QVERIFY(xml.contains("<CgPoint name=\"5\""));
    QVERIFY(xml.contains("desc=\"Hub\""));
  }

  // Issue #997 / REQ-398: a flat surface at 100 and a closed 120 ft pad at 105, so every station is
  // 5 ft of FILL and the daylight offset is 5 x the fill run - 15 ft at 3:1, 10 ft at 2:1.
  static void BuildGradingSite(EditorWindow& window, const char* secondSurface = nullptr) {
    auto& state = window.State();
    auto addSurface = [&](const QString& name) {
      window.Submit("SURFACECREATE " + name);
      window.Submit("SURFACEADDPOINT " + name + ", 0, 0, 100");
      window.Submit("SURFACEADDPOINT " + name + ", 200, 0, 100");
      window.Submit("SURFACEADDPOINT " + name + ", 200, 200, 100");
      window.Submit("SURFACEADDPOINT " + name + ", 0, 200, 100");
      window.Submit("SURFACEREBUILD " + name);
    };
    addSurface("Ground");
    if (secondSurface) addSurface(QString::fromUtf8(secondSurface));
    QTRY_VERIFY_WITH_TIMEOUT(!state.cadSurfaces.empty() && state.cadSurfaces[0].tin, 10000);
    if (secondSurface) QTRY_VERIFY_WITH_TIMEOUT(state.cadSurfaces.size() == 2 && state.cadSurfaces[1].tin, 10000);
    window.Submit("FEATURELINE Pad");
    window.Submit("40,40,105"); window.Submit("160,40,105");
    window.Submit("160,160,105"); window.Submit("40,160,105"); window.Submit("CLOSE");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(1));
  }

  // Plan distance from the baseline's first vertex to feature line `line`'s first vertex.
  static double FirstDaylightOffset(const AppCommandState& state, std::size_t line) {
    const auto v = static_cast<std::size_t>(state.featureLineOffsets[line]) * 3;
    return std::hypot(state.featureLineVerts[v] - 40.0, state.featureLineVerts[v + 1] - 40.0);
  }

  static std::size_t FeatureLineById(const AppCommandState& state, std::uint64_t id) {
    const int ix = FindEntityIndexById(state.featureLineAttrs, id);
    return ix < 0 ? std::size_t(-1) : static_cast<std::size_t>(ix);
  }

  static bool LogContains(const EditorWindow& window, const char* text) {
    for (const auto& line : window.m_Log) if (line.find(text) != std::string::npos) return true;
    return false;
  }

  void gradingStoresAnEditableDefinitionOnTheDaylightLine() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("C"); window.Submit("4:1");
    window.Submit("F"); window.Submit("3:1"); window.Submit("");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    QVERIFY(!state.featureLineInfo[0].grading);  // the baseline stays an ordinary feature line
    const auto& def = state.featureLineInfo[1].grading;
    QVERIFY(def.has_value());
    QCOMPARE(def->baselineId, state.featureLineAttrs[0].id);
    QCOMPARE(def->surfaceId, state.cadSurfaceAttrs[0].id);
    QVERIFY(def->surfaceId != 0);
    QCOMPARE(def->cutRun, 4.0);
    QCOMPARE(def->fillRun, 3.0);
    QVERIFY(std::abs(FirstDaylightOffset(state, 1) - 15.0) < 0.002);  // REQ-101
  }

  void gradingEditsTheSelectedResultWithoutCreatingAnotherDaylightLine() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("C"); window.Submit("4:1");
    window.Submit("F"); window.Submit("3:1"); window.Submit("");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    EnsureEntityIds(state);  // ids are assigned lazily, on the next sweep
    const auto daylightId = state.featureLineAttrs[1].id;
    const auto layer = state.featureLineAttrs[1].layer;
    const auto before = state.featureLineVerts;

    state.selection = {{SelectedEntity::Type::FeatureLine, 1}};
    window.Submit("GRADING");
    QVERIFY(LogContains(window, "editing \"Pad Daylight\""));
    QVERIFY(LogContains(window, "cut 4:1 (25.00%)"));  // the stored slopes come back as the prompt's
    window.Submit("F"); window.Submit("2:1"); window.Submit("");
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    QCOMPARE(state.featureLineAttrs[1].id, daylightId);
    QCOMPARE(state.featureLineAttrs[1].layer, layer);
    QCOMPARE(state.featureLineInfo[1].name, std::string("Pad Daylight"));
    QCOMPARE(state.featureLineInfo[1].grading->fillRun, 2.0);
    QCOMPARE(state.featureLineInfo[1].grading->cutRun, 4.0);
    QVERIFY(std::abs(FirstDaylightOffset(state, 1) - 10.0) < 0.002);
    QVERIFY(LogContains(window, "\"Pad Daylight\" rebuilt"));

    window.Submit("UNDO");
    QVERIFY(state.featureLineVerts == before);
    QCOMPARE(state.featureLineInfo[1].grading->fillRun, 3.0);
    QCOMPARE(state.featureLineAttrs[1].id, daylightId);
  }

  void gradingEditCancelOrFailureLeavesThePreviousResult() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    const auto verts = state.featureLineVerts;
    const auto offsets = state.featureLineOffsets;
    const auto fill = state.featureLineInfo[1].grading->fillRun;

    // Escape after changing a slope: nothing is rebuilt and the definition is untouched.
    state.selection = {{SelectedEntity::Type::FeatureLine, 1}};
    window.Submit("GRADING"); window.Submit("F"); window.Submit("2:1");
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus();
    QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(state.featureLineVerts == verts);
    QCOMPARE(state.featureLineInfo[1].grading->fillRun, fill);

    // A slope so flat it runs off the surface: the calculation fails and the old result stays.
    state.selection = {{SelectedEntity::Type::FeatureLine, 1}};
    window.Submit("GRADING"); window.Submit("F"); window.Submit("100:1"); window.Submit("");
    QVERIFY(LogContains(window, "too few for a line"));
    QVERIFY(state.featureLineVerts == verts);
    QVERIFY(state.featureLineOffsets == offsets);
    QCOMPARE(state.featureLineInfo[1].grading->fillRun, fill);
  }

  void gradingDefinitionSurvivesDwgSaveAndReopenAndRebuildsThere() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QByteArray path = directory.filePath("grading-997.dwg").toUtf8();
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("C"); window.Submit("4:1");
    window.Submit("F"); window.Submit("3:1"); window.Submit("");
    EnsureEntityIds(state);
    const auto baselineId = state.featureLineAttrs[0].id;
    const auto daylightId = state.featureLineAttrs[1].id;
    const auto surfaceId = state.cadSurfaceAttrs[0].id;
    std::vector<std::string> log;
    QVERIFY(SaveDrawingDocument(state, path.constData(), log));

    window.CreateDrawingFromTemplate();  // a fresh tab, as File > Open does
    QVERIFY(state.featureLineClosed.empty());
    QVERIFY(OpenDrawingDocument(state, path.constData(), log));
    const auto day = FeatureLineById(state, daylightId);
    QVERIFY(day != std::size_t(-1));
    QVERIFY(state.featureLineInfo[day].grading.has_value());
    QCOMPARE(state.featureLineInfo[day].grading->baselineId, baselineId);
    QCOMPARE(state.featureLineInfo[day].grading->surfaceId, surfaceId);
    QCOMPARE(state.featureLineInfo[day].grading->cutRun, 4.0);
    QCOMPARE(state.featureLineInfo[day].grading->fillRun, 3.0);
    QVERIFY(std::abs(FirstDaylightOffset(state, day) - 15.0) < 0.002);

    state.selection = {{SelectedEntity::Type::FeatureLine, static_cast<int>(day)}};
    window.Submit("GRADING"); window.Submit("F"); window.Submit("2:1"); window.Submit("");
    QVERIFY(LogContains(window, "rebuilt"));
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    QCOMPARE(state.featureLineAttrs[day].id, daylightId);
    QVERIFY(std::abs(FirstDaylightOffset(state, day) - 10.0) < 0.002);
  }

  void gradingPayloadWithoutDefinitionLoadsAsOrdinaryFeatureLine() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("");
    const std::string json = SerializeGoSurveyJson(state);
    const auto key = json.find("\"grading\":");
    QVERIFY(key != std::string::npos);
    QCOMPARE(json.find("\"grading\":", key + 1), std::string::npos);

    // A drawing written before #997 has no "grading" key: it opens as an ordinary feature line.
    std::string legacy = json; legacy.replace(key, 10, "\"legacyX\":");
    AppCommandState old; std::vector<std::string> log;
    QVERIFY(LoadGoSurveyFromJsonUtf8(old, legacy, log));
    QCOMPARE(old.featureLineInfo.size(), std::size_t(2));
    QVERIFY(!old.featureLineInfo[1].grading);

    // A definition with no baseline could never be rebuilt, so it is dropped rather than kept.
    std::string broken = json;
    const auto id = broken.find("\"baselineId\":", key);
    QVERIFY(id != std::string::npos);
    const auto idEnd = broken.find_first_of(",}", id);
    broken.replace(id, idEnd - id, "\"baselineId\":0");
    AppCommandState bad;
    QVERIFY(LoadGoSurveyFromJsonUtf8(bad, broken, log));
    QVERIFY(!bad.featureLineInfo[1].grading);

    AppCommandState good;
    QVERIFY(LoadGoSurveyFromJsonUtf8(good, json, log));
    QVERIFY(good.featureLineInfo[1].grading.has_value());
  }

  void gradingWithDeletedBaselineIsReportedAndLeftIntact() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("");
    EnsureEntityIds(state);  // ids are assigned lazily, on the next sweep
    const auto daylightId = state.featureLineAttrs[1].id;
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("ERASE");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(1));
    const auto day = FeatureLineById(state, daylightId);
    QCOMPARE(day, std::size_t(0));
    const auto verts = state.featureLineVerts;

    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING");
    QVERIFY(LogContains(window, "the baseline of \"Pad Daylight\" is gone"));
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(state.featureLineVerts == verts);
    QVERIFY(state.featureLineInfo[0].grading.has_value());  // kept, in case the baseline is undone back
  }

  void gradingWithDeletedTargetSurfaceAsksInsteadOfSubstituting() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    BuildGradingSite(window, "Other");
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    window.Submit("GRADING"); window.Submit("S"); window.Submit("Ground"); window.Submit("");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    const auto verts = state.featureLineVerts;
    window.Submit("SURFACEDELETE Ground");
    QCOMPARE(state.cadSurfaces.size(), std::size_t(1));

    state.selection = {{SelectedEntity::Type::FeatureLine, 1}};
    window.Submit("GRADING");
    QVERIFY(LogContains(window, "its target surface is gone"));
    QVERIFY(LogContains(window, "no surface chosen"));
    window.Submit("");  // Enter does not quietly regrade to the surface that is left
    QVERIFY(LogContains(window, "choose a surface first"));
    QVERIFY(state.featureLineVerts == verts);

    window.Submit("S"); window.Submit("Other"); window.Submit("");
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.featureLineInfo[1].grading->surfaceId, state.cadSurfaceAttrs[0].id);
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
  }

  void gradingRibbonToolsStartCreateAndEdit() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto& state = window.State();
    const QString artifacts = QtTestArtifactDirectory() + "/";
    for (const char* id : {"ribbon_CgdGrading", "ribbon_CdGrading", "ribbon_SvyGrading", "ribbon_TsGrade"}) {
      auto* action = window.findChild<QAction*>(id); QVERIFY2(action, id);
      QCOMPARE(action->property("ribbonCommand").toString(), QString("GRADING"));
      QVERIFY(!action->property("releasePlaceholder").toBool());
    }
    auto* optimize = window.findChild<QAction*>("ribbon_OptGrading"); QVERIFY(optimize);
    QVERIFY(!optimize->isEnabled());  // Grading Optimization is #813, still a placeholder

    auto* create = window.findChild<QAction*>("ribbon_CdGrading");
    QVERIFY(create->isEnabled());
    auto* commandBar = window.findChild<QWidget*>("floatingCommandBar"); QVERIFY(commandBar);
    // Error state: no feature lines yet.
    create->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(LogContains(window, "the drawing has no feature lines"));
    QApplication::processEvents();
    QVERIFY(commandBar->grab().save(artifacts + "qt-997-grading-no-feature-lines.png"));

    BuildGradingSite(window);
    state.selection = {{SelectedEntity::Type::FeatureLine, 0}};
    create->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::Grading);
    QApplication::processEvents();
    QVERIFY(commandBar->grab().save(artifacts + "qt-997-grading-create-prompt.png"));
    window.Submit("");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));

    state.selection = {{SelectedEntity::Type::FeatureLine, 1}};
    window.findChild<QAction*>("ribbon_SvyGrading")->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::Grading);
    QCOMPARE(state.gradingEditId, state.featureLineAttrs[1].id);
    QApplication::processEvents();
    QVERIFY(commandBar->grab().save(artifacts + "qt-997-grading-edit-prompt.png"));
    window.Submit("F"); window.Submit("2:1"); window.Submit("");
    QCOMPARE(state.featureLineClosed.size(), std::size_t(2));
    // Frame the 200 ft test site with the real ZOOMEXTENTS (GitHub #1024).
    state.selection.clear();
    window.Submit("ZOOMEXTENTS");
    QApplication::processEvents(); QTest::qWait(100);
    QVERIFY(window.grab().save(artifacts + "qt-997-grading-result-window.png"));
    QVERIFY(window.m_Viewport->grabFramebuffer().save(artifacts + "qt-997-grading-result-viewport.png"));
  }

  // REQ-444 / GitHub #803: both Revision Cloud buttons are live, run REVCLOUD, and the cloud they
  // make is an ordinary closed polyline of arcs that one UNDO removes.
  void ribbonRevisionCloudButtonsDrawACloud() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto& state = window.State();
    for (const char* name : {"ribbon_RibbonRevcloud", "ribbon_AnnRevcloud"}) {
      auto* action = window.findChild<QAction*>(name); QVERIFY2(action, name);
      QVERIFY(action->isEnabled());
      QVERIFY(!action->property("releasePlaceholder").toBool());
      QCOMPARE(action->property("ribbonCommand").toString(), QString("REVCLOUD"));
    }
    const auto polylinesBefore = state.userPolylineClosed.size();
    window.findChild<QAction*>("ribbon_RibbonRevcloud")->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::Revcloud);
    for (const char* line : {"A 1", "0,0", "10,0", "10,6", "0,6", ""}) window.Submit(line);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.userPolylineClosed.size(), polylinesBefore + 1);
    QCOMPARE(state.userPolylineClosed.back(), uint8_t(1));
    // 12 + 7 + 12 + 7 scallops, each a real arc.
    QCOMPARE(state.userPolylineVertsBulge.size(), state.userPolylineVerts.size() / 3);
    QCOMPARE(state.userPolylineOffsets.back() - state.userPolylineOffsets[state.userPolylineOffsets.size() - 2], 38);
    for (int i = state.userPolylineOffsets[state.userPolylineOffsets.size() - 2]; i < state.userPolylineOffsets.back(); ++i)
      QVERIFY(state.userPolylineVertsBulge[static_cast<std::size_t>(i)] != 0.f);
    window.Submit("UNDO");
    QCOMPARE(state.userPolylineClosed.size(), polylinesBefore);
    // Esc leaves nothing behind.
    window.findChild<QAction*>("ribbon_AnnRevcloud")->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::Revcloud);
    window.Submit("0,0"); window.Submit("5,0");
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus();
    QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.userPolylineClosed.size(), polylinesBefore);
  }

  // REQ-444 / GitHub #803: screenshots of the Revision Cloud work for the user's review, written to
  // build/artifacts and never committed.
  void revisionCloudScreenshotsForReview() {
    const QString artifacts = QtTestArtifactDirectory() + "/";
    QDir().mkpath(artifacts);
    {
      EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
      QTest::qWait(150);
      auto& state = window.State();
      auto* ribbon = window.findChild<QTabWidget*>("ribbon"); QVERIFY(ribbon);
      const QImage ribbonShot = ribbon->grab().toImage();
      QVERIFY(ribbonShot.copy(1090, 35, 250, 115).scaled(750, 345, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                  .save(artifacts + "qt-803-ribbon-draw-panel.png"));

      for (const char* line : {"REVCLOUD", "A 1", "0,0", "10,0", "10,6", "0,6", ""}) window.Submit(line);
      window.Submit("CIRCLE"); window.Submit("20,3"); window.Submit("3");
      window.Submit("REVCLOUD"); window.Submit("A 0.8"); window.Submit("O");
      SubmitRevcloudViewportPick(state, 23.f, 3.f, window.m_Log);
      QCOMPARE(state.active, AppCommandState::Kind::None);
      window.Submit("ZOOMEXTENTS");
      QTest::qWait(300);
      // The drawing is small in the extents view, so the capture is cropped to it and enlarged.
      const QImage frame = window.m_Viewport->grabFramebuffer();
      const QPoint mid(frame.width() / 2 + 95, frame.height() / 2 - 15);
      QVERIFY(frame.copy(QRect(mid.x() - 145, mid.y() - 48, 290, 96)).scaled(1450, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                  .save(artifacts + "qt-803-two-clouds-viewport.png"));
    }
    {
      EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
      QTest::qWait(150);
      const auto logCrop = [&](const char* name) {
        QTest::qWait(150);
        const QImage shot = window.grab().toImage();
        const double f = shot.width() / 1440.0;
        QVERIFY(shot.copy(QRect(int(385 * f), int(715 * f), int(700 * f), int(85 * f))).save(artifacts + name));
      };
      for (const char* line : {"REVCLOUD", "A 0"}) window.Submit(line);
      logCrop("qt-803-refusal-arclength.png");
      for (const char* line : {"A 1", "0,0", "5,0", "C"}) window.Submit(line);
      logCrop("qt-803-refusal-corners.png");
      window.Submit("");
    }
  }

  void releaseRibbonFeatureLineRunsAndCancelsCompleteWorkflow() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto& state = window.State();
    const QString artifacts = QtTestArtifactDirectory() + "/";
    auto* groundAction = window.findChild<QAction*>("ribbon_CgdFeatureLine"); QVERIFY(groundAction);
    auto* designAction = window.findChild<QAction*>("ribbon_CdFeatureLine"); QVERIFY(designAction);
    QVERIFY(groundAction->isEnabled()); QVERIFY(designAction->isEnabled());
    QCOMPARE(groundAction->property("ribbonCommand").toString(), QString("FEATURELINE"));
    QCOMPARE(designAction->property("ribbonCommand").toString(), QString("FEATURELINE"));

    groundAction->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::FeatureLine);
    const auto featureLinesBefore = state.featureLineClosed.size();
    window.Submit("bad point");
    QVERIFY(state.featureLineDraftVerts.empty());
    window.Submit("0,0");
    QVERIFY(state.featureLinePendingPoint);
    window.Submit("not-an-elevation");
    QVERIFY(state.featureLinePendingPoint); // Bad elevation leaves the pending point available.
    auto* commandBar = window.findChild<QWidget*>("floatingCommandBar"); QVERIFY(commandBar);
    QApplication::processEvents();
    QVERIFY(commandBar->grab().save(artifacts + "qt-896-featureline-elevation-error.png"));
    window.Submit("100");
    QVERIFY(!state.featureLinePendingPoint);
    QCOMPARE(state.featureLineDraftVerts.size(), std::size_t(3));
    window.Submit("10,0,110");
    QCOMPARE(state.featureLineDraftVerts.size(), std::size_t(6));
    window.Submit("END");
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QCOMPARE(state.featureLineClosed.size(), featureLinesBefore + 1);
    QCOMPARE(state.featureLineVerts.size(), std::size_t(6));

    window.Submit("UNDO");
    QCOMPARE(state.featureLineClosed.size(), featureLinesBefore);
    QCOMPARE(state.featureLineVerts.size(), std::size_t(0));

    designAction->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::FeatureLine);
    window.Submit("20,30,40");
    QVERIFY(!state.featureLineDraftVerts.empty());
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus();
    QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(state.featureLineDraftVerts.empty());
    QCOMPARE(state.featureLineClosed.size(), featureLinesBefore);
  }

  void pipeAndFeatureLineRibbonActionsRespectReadOnlyProjectSaveRules() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show();
    auto& state = window.State();
    constexpr std::uint32_t projectUid = 912;
    state.drawingTabs[static_cast<std::size_t>(state.activeDrawingIdx)].projectUid = projectUid;
    AppCommandState::ProjectSession session;
    session.uid = projectUid;
    session.readOnly = true;
    state.openProjects.push_back(session);

    auto* pipe = window.findChild<QAction*>("ribbon_CdPipeNetwork"); QVERIFY(pipe);
    pipe->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::PipeRun);
    auto* commandInput = window.findChild<QLineEdit*>("commandInput"); QVERIFY(commandInput);
    commandInput->setFocus(); QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);

    auto* feature = window.findChild<QAction*>("ribbon_CdFeatureLine"); QVERIFY(feature);
    feature->trigger();
    QCOMPARE(state.active, AppCommandState::Kind::FeatureLine);
    commandInput->setFocus(); QTest::keyClick(commandInput, Qt::Key_Escape);
    QCOMPARE(state.active, AppCommandState::Kind::None);

    QTimer::singleShot(0, &window, [&] {
      auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(warning);
      QVERIFY(warning->text().contains("read-only"));
      warning->accept();
    });
    QVERIFY(!window.SaveDrawing());
    QVERIFY(state.cadPipeRuns.empty());
    QVERIFY(state.featureLineVerts.empty());
  }

  void viewManagerRestoresUpdatesAndDeletesViewsAndNamedUcs() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); auto& state = window.State();
    NamedView savedView;
    savedView.name = "Site Overview";
    savedView.panX = 12.0;
    savedView.azimuthDeg = 45.0f;
    savedView.elevationDeg = 35.264f;
    savedView.projection = Camera::Projection::Perspective;
    savedView.fovDeg = 72.0f;
    state.namedViews.push_back(savedView);
    ucs::Ucs savedFrame = ucs::Ucs{};
    savedFrame.origin = {4.0, 5.0, 6.0};
    state.ucsNamed.push_back({"Lot Grid", savedFrame});
    bool dialogOpened = false;
    bool viewsShot = false;
    bool ucsShot = false;

    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("viewManagerDialog");
      if (!dialog) return;
      dialogOpened = true;
      auto* views = dialog->findChild<QListWidget*>("viewManagerSavedViews");
      auto* frames = dialog->findChild<QListWidget*>("viewManagerSavedUcs");
      auto* tabs = dialog->findChild<QTabWidget*>("viewManagerTabs");
      if (!views || !frames || !tabs) { dialog->accept(); return; }
      viewsShot = dialog->grab().save(QtTestArtifactDirectory() + "/qt-892-view-manager-views.png");
      views->setCurrentRow(0);
      if (auto* restore = dialog->findChild<QPushButton*>("viewManagerRestore")) restore->click();
      state.viewportPanX = 31.0;
      if (auto* update = dialog->findChild<QPushButton*>("viewManagerUpdate")) update->click();

      // Cancelling the nested name prompt leaves the saved-view collection untouched.
      QTimer::singleShot(0, &window, [&] {
        if (auto* prompt = window.findChild<QInputDialog*>()) prompt->reject();
      });
      if (auto* save = dialog->findChild<QPushButton*>("viewManagerNewView")) save->click();

      tabs->setCurrentIndex(1);
      ucsShot = dialog->grab().save(QtTestArtifactDirectory() + "/qt-892-view-manager-ucs.png");
      frames->setCurrentRow(0);
      if (auto* restore = dialog->findChild<QPushButton*>("viewManagerRestore")) restore->click();
      if (auto* remove = dialog->findChild<QPushButton*>("viewManagerDelete")) remove->click();
      tabs->setCurrentIndex(0);
      views->setCurrentRow(0);
      if (auto* remove = dialog->findChild<QPushButton*>("viewManagerDelete")) remove->click();
      dialog->accept();
    });
    window.Submit("VIEW");
    QVERIFY(dialogOpened);
    QVERIFY(viewsShot);
    QVERIFY(ucsShot);
    QVERIFY(state.namedViews.empty());
    QVERIFY(state.ucsNamed.empty());
    QCOMPARE(state.viewportPanX, 31.0);
    QCOMPARE(state.viewportProjection, Camera::Projection::Perspective);
    QVERIFY(ucs::FramesMatch(state.activeUcs, savedFrame));
  }

  void viewManagerShowsEmptyCollectionsAndClosePreservesState() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); auto& state = window.State();
    const auto originalUcs = state.activeUcs;
    bool sawEmptyLists = false;
    bool emptyShot = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("viewManagerDialog");
      if (!dialog) return;
      auto* views = dialog->findChild<QListWidget*>("viewManagerSavedViews");
      auto* frames = dialog->findChild<QListWidget*>("viewManagerSavedUcs");
      sawEmptyLists = views && frames && views->count() == 0 && frames->count() == 0;
      emptyShot = dialog->grab().save(QtTestArtifactDirectory() + "/qt-892-view-manager-empty.png");
      dialog->reject();
    });
    window.Submit("VIEW");
    QVERIFY(sawEmptyLists);
    QVERIFY(emptyShot);
    QVERIFY(state.namedViews.empty());
    QVERIFY(state.ucsNamed.empty());
    QVERIFY(ucs::FramesMatch(state.activeUcs, originalUcs));
  }

  void qtIsolationCommandsHideAndRestoreStableEntities() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); auto& state = window.State();
    state.userLinesFlat = {0, 0, 0, 10, 0, 0, 20, 0, 0, 30, 0, 0};
    state.userLineAttrs.resize(2);
    // Direct fixture edits must invalidate the revision-gated identity/display caches.
    BumpCadGpuCache(state);
    state.selection = {{SelectedEntity::Type::LineSeg, 0}};
    window.Submit("ISOLATEOBJECTS");
    QCOMPARE(state.hiddenEntityIds.size(), std::size_t(1));
    window.Submit("UNISOLATEOBJECTS");
    QVERIFY(state.hiddenEntityIds.empty());
    state.selection = {{SelectedEntity::Type::LineSeg, 1}};
    window.Submit("HIDEOBJECTS");
    QCOMPARE(state.hiddenEntityIds.size(), std::size_t(1));
    window.Submit("UNISOLATEOBJECTS");
    QVERIFY(state.hiddenEntityIds.empty());
  }

  void qtAnnotationScaleControlsApplyVisibilityAndStableIsolation() {
    AppCommandState state;
    state.uiViewportWidthPx = 640.f; state.uiViewportHeightPx = 480.f;
    state.annotationScales = {{"1:50", 1.f, 50.f}, {"1:100", 1.f, 100.f}};
    state.currentAnnotationScaleIndex = 0;
    CadAnnotation annotation; annotation.kind = CadAnnotation::Kind::Text; annotation.text = "Qt";
    annotation.annotative = true; annotation.annotativeVisibleScaleNames = {"1:50"};
    annotation.plottedHeightInches = 0.25f; state.cadAnnotations.push_back(annotation);
    EntityAttributes attrs; attrs.id = 42; state.cadAnnotationAttrs.push_back(attrs);
    auto render = [&] {
      QImage image(640, 480, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
      QPainter painter(&image); DrawQtModelAnnotations(painter, state, 640.f, 480.f); painter.end();
      return image;
    };
    const QImage visible = render();
    QVERIFY(std::any_of(visible.constBits(), visible.constBits() + visible.sizeInBytes(), [](uchar value) { return value != 0; }));
    state.currentAnnotationScaleIndex = 1;
    const QImage excludedScale = render();
    QVERIFY(std::all_of(excludedScale.constBits(), excludedScale.constBits() + excludedScale.sizeInBytes(), [](uchar value) { return value == 0; }));
    state.currentAnnotationScaleIndex = 0; state.hiddenEntityIds = {42};
    const QImage hidden = render();
    QVERIFY(std::all_of(hidden.constBits(), hidden.constBits() + hidden.sizeInBytes(), [](uchar value) { return value == 0; }));
  }

  void qtAnnotativePropertiesToggleAndUndoVisibleScales() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); auto& state = window.State();
    state.annotationScales = {{"1:50", 1.f, 50.f}, {"1:100", 1.f, 100.f}};
    CadAnnotation annotation; annotation.kind = CadAnnotation::Kind::Text; annotation.text = "Qt";
    state.cadAnnotations.push_back(annotation); state.cadAnnotationAttrs.emplace_back();
    state.selection = {{SelectedEntity::Type::Annotation, 0}}; window.Refresh();
    auto* annotative = window.findChild<QCheckBox*>("propertyAnnotative"); QVERIFY(annotative);
    annotative->setChecked(true); QVERIFY(state.cadAnnotations[0].annotative);
    auto* scales = window.findChild<QComboBox*>("propertyAnnotationScales"); QVERIFY(scales);
    auto* model = qobject_cast<QStandardItemModel*>(scales->model()); QVERIFY(model);
    model->item(1)->setCheckState(Qt::Unchecked);
    QCOMPARE(state.cadAnnotations[0].annotativeVisibleScaleNames, std::vector<std::string>{"1:50"});
    window.Submit("UNDO");
    QVERIFY(state.cadAnnotations[0].annotativeVisibleScaleNames.empty());
    state.cadFilledRegions.emplace_back(); state.cadFilledRegionAttrs.emplace_back();
    state.selection = {{SelectedEntity::Type::FilledRegion, 0}}; window.Refresh();
    annotative = window.findChild<QCheckBox*>("propertyAnnotative"); QVERIFY(annotative); annotative->setChecked(true);
    QVERIFY(state.cadFilledRegions[0].annotative);
    state.cadBlockRefs.emplace_back(); state.cadBlockRefAttrs.emplace_back();
    state.selection = {{SelectedEntity::Type::BlockRef, 0}}; window.Refresh();
    annotative = window.findChild<QCheckBox*>("propertyAnnotative"); QVERIFY(annotative); annotative->setChecked(true);
    QVERIFY(state.cadBlockRefs[0].annotative);
    state.cadMultileaders.emplace_back(); state.cadMultileaderAttrs.emplace_back();
    state.selection = {{SelectedEntity::Type::Multileader, 0}}; window.Refresh();
    annotative = window.findChild<QCheckBox*>("propertyAnnotative"); QVERIFY(annotative); annotative->setChecked(true);
    QVERIFY(state.cadMultileaders[0].annotative);
  }

  void releaseRibbonHatchDefaultsAndSelectionEditsAreSeparateAndUndoable() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); auto& state = window.State();
    auto* angle = window.findChild<QDoubleSpinBox*>("ribbonHatchAngle"); QVERIFY(angle);
    angle->setValue(30); QCOMPARE(state.hatchAngleDeg, 30.f);
    CadFilledRegion region; region.vertsXyz = {0, 0, 0, 10, 0, 0, 10, 10, 0}; region.loopStart = {0};
    state.cadFilledRegions.push_back(region); state.cadFilledRegionAttrs.emplace_back();
    state.selection.push_back({SelectedEntity::Type::FilledRegion, 0}); window.Submit("");
    auto* ribbon = window.findChild<QTabWidget*>("ribbon");
    QCOMPARE(ribbon->currentWidget()->objectName(), QString("ribbonPage_HatchCtx"));
    auto* pattern = window.findChild<QComboBox*>("ribbonHatchPattern"); QVERIFY(pattern->findText("ANSI31") >= 0);
    angle->setValue(45); QCOMPARE(state.cadFilledRegions[0].patternAngleDeg, 45.f); QCOMPARE(state.hatchAngleDeg, 30.f);
    window.Submit("UNDO"); QCOMPARE(state.cadFilledRegions[0].patternAngleDeg, 0.f);
    window.Submit("REDO"); QCOMPARE(state.cadFilledRegions[0].patternAngleDeg, 45.f);
  }

  void nativePointCreationPlacesSelectsAndUndoes() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.show();
    window.Submit("CREATEPOINTS");
    auto* dialog = window.findChild<QDialog*>("createPoints");
    QVERIFY(dialog); QVERIFY(!dialog->isModal());
    dialog->findChild<QSpinBox*>("nextPointNumber")->setValue(42);
    dialog->findChild<QDoubleSpinBox*>("pointElevation")->setValue(123.25);
    dialog->findChild<QPlainTextEdit*>("pointDescription")->setPlainText("EG");
    auto* viewport = dynamic_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::qWait(100);
    const QPoint pick(viewport->width() / 2, viewport->height() / 2);
    AppCommandState::ProjectSession lockedProject;
    lockedProject.uid = 77; lockedProject.readOnly = true;
    window.State().openProjects.push_back(lockedProject);
    window.State().drawingTabs[window.State().activeDrawingIdx].projectUid = 77;
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pick);
    QVERIFY(window.State().surveyPoints.empty());
    QCOMPARE(window.State().createPointsNextId, 42);
    window.State().drawingTabs[window.State().activeDrawingIdx].projectUid = 0;
    window.State().openProjects.clear();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pick);
    QCOMPARE(window.State().surveyPoints.size(), std::size_t(1));
    QCOMPARE(window.State().surveyPoints[0].id, 42);
    QCOMPARE(window.State().surveyPoints[0].description, std::string("EG"));
    QCOMPARE(window.State().surveyPoints[0].elevation, 123.25);
    QCOMPARE(window.State().createPointsNextId, 43);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pick);
    QCOMPARE(window.State().surveyPoints.size(), std::size_t(1));
    QCOMPARE(window.State().selectedSurveyPointIndices.size(), std::size_t(1));
    QTRY_COMPARE(dialog->findChild<QSpinBox*>("nextPointNumber")->value(), 43);
    const auto originalPoint = window.State().surveyPoints[0];
    dialog->findChild<QSpinBox*>("nextPointNumber")->setValue(42);
    dialog->findChild<QDoubleSpinBox*>("pointElevation")->setValue(200);
    dialog->findChild<QComboBox*>("pointDuplicatePolicy")->setCurrentIndex(2);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pick + QPoint(60, 40));
    QCOMPARE(window.State().surveyPoints.size(), std::size_t(1));
    QCOMPARE(window.State().surveyPoints[0].elevation, 200.0);
    window.Submit("UNDO");
    QCOMPARE(window.State().surveyPoints[0].easting, originalPoint.easting);
    QCOMPARE(window.State().surveyPoints[0].northing, originalPoint.northing);
    QCOMPARE(window.State().surveyPoints[0].elevation, 123.25);
    QCOMPARE(window.State().surveyPoints[0].description, std::string("EG"));
    QTest::keyClick(viewport, Qt::Key_Escape);
    QVERIFY(!window.State().showCreatePointsWindow);
    QTRY_VERIFY(!window.findChild<QDialog*>("createPoints"));
    window.Submit("UNDO");
    QVERIFY(window.State().surveyPoints.empty());
  }

  void nativePointGridPreservesPrecisionIdentityAndUndo() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    auto& state = window.State();
    state.worldDocumentOriginX = 6000000; state.worldDocumentOriginY = 4000000;
    std::vector<std::string> log;
    state.createPointsOpts.defaultDescription = "EG";
    state.createPointsNextId = 10; QVERIFY(TryPlaceSurveyPoint(state, 10, 20, 30, log));
    state.createPointsNextId = 2; QVERIFY(TryPlaceSurveyPoint(state, 40, 50, 60, log));
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("surveyPointGrid");
      QVERIFY(dialog);
      auto* table = dialog->findChild<QTableWidget*>("pointGrid");
      table->sortItems(0, Qt::DescendingOrder);
      QCOMPARE(table->item(0, 0)->text(), QString("10"));
      QCOMPARE(table->item(1, 0)->text(), QString("2"));
      table->item(1, 1)->setText("4000001.123456789");
      QCOMPARE(state.surveyPoints[0].id, 10);
      QCOMPARE(state.surveyPoints[1].id, 2);
      QVERIFY(std::abs(state.surveyPoints[1].northing - 1.123456789) < 1.e-9);
      table->item(1, 0)->setText("20");
      QCOMPARE(state.surveyPoints[1].id, 20);
      const int label = FindSurveyLabelAnnIndex(state, state.surveyPoints[1]);
      QVERIFY(label >= 0);
      QCOMPARE(state.cadAnnotations[label].surveyPointLabelForId, 20);
      QCOMPARE(state.surveyPoints[1].rawDescription, std::string("EG"));
      dialog->findChild<QLineEdit*>("pointFilter")->setText("20");
      // Select by the stable identity carried in each cell after the sort moved it.
      for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toInt() == 20) table->selectRow(row);
      dialog->findChild<QPushButton*>("selectGridPoints")->click();
    });
    window.Submit("VIEWPOINTS");
    QCOMPARE(state.selectedSurveyPointIndices.size(), std::size_t(1));
    QCOMPARE(state.selectedSurveyPointIndices.front(), 1);
    window.Submit("UNDO");
    QCOMPARE(state.surveyPoints[1].id, 2);
    window.Submit("UNDO");
    QCOMPARE(state.surveyPoints[1].northing, 50.0);
  }

  void surveyJsonRoundTripsUnicodeRawCodesAndWorldCoordinates() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QString::fromUtf8("測量-é.json"));
    EditorWindow source; source.CreateDrawingFromTemplate();
    auto& state = source.State();
    state.worldDocumentOriginX = 6000000; state.worldDocumentOriginY = 4000000;
    state.createPointsOpts.defaultDescription = "EG"; state.createPointsNextId = 0;
    std::vector<std::string> log;
    QVERIFY(TryPlaceSurveyPoint(state, 1.123456789, 2.987654321, 123.456789, log));
    state.surveyPoints[0].description = "Expanded {EG} \"quoted\"\n\t\x01";
    state.surveyPoints[0].layer = "測量";
    QVERIFY(SaveSurveyPointsToJsonFile(state, path.toUtf8().constData(), log));
    // Replacing an existing file must work, including on Windows.
    QVERIFY(SaveSurveyPointsToJsonFile(state, path.toUtf8().constData(), log));
    EditorWindow target; target.CreateDrawingFromTemplate();
    target.State().worldDocumentOriginX = 6000100; target.State().worldDocumentOriginY = 4000200;
    target.State().createPointsNextId = 99;
    QVERIFY(TryPlaceSurveyPoint(target.State(), 10, 20, 30, log));
    const auto oldLabel = target.State().surveyPoints[0].labelMtextAnnId;
    QVERIFY(LoadSurveyPointsFromJsonFile(target.State(), path.toUtf8().constData(), log));
    QCOMPARE(target.State().surveyPoints.size(), std::size_t(1));
    const auto& loaded = target.State().surveyPoints[0];
    QCOMPARE(loaded.id, 0);
    QCOMPARE(loaded.rawDescription, std::string("EG"));
    QCOMPARE(loaded.description, state.surveyPoints[0].description);
    QCOMPARE(loaded.layer, state.surveyPoints[0].layer);
    QVERIFY(std::abs(loaded.easting + 100 - state.surveyPoints[0].easting) < 1.e-12);
    QVERIFY(std::abs(loaded.northing + 200 - state.surveyPoints[0].northing) < 1.e-12);
    QCOMPARE(loaded.elevation, state.surveyPoints[0].elevation);
    QCOMPARE(target.State().cadAnnotations.size(), std::size_t(1));
    QCOMPARE(target.State().surveyPointIdBuffers[0], std::string("0"));
    target.Submit("UNDO");
    QCOMPARE(target.State().surveyPoints[0].id, 99);
    QCOMPARE(target.State().surveyPoints[0].labelMtextAnnId, oldLabel);
    QCOMPARE(target.State().cadAnnotations.size(), std::size_t(1));
  }

  void surveyJsonFailurePreservesDrawingAndExistingFile() {
    QTemporaryDir directory;
    const auto path = directory.filePath("points.json");
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State(); std::vector<std::string> log;
    QVERIFY(TryPlaceSurveyPoint(state, 10, 20, 30, log));
    const auto revision = state.cadGpuRevision;
    const auto undoCount = state.documents[state.activeDrawingIdx].undoStack.size();
    const auto label = state.surveyPoints[0].labelMtextAnnId;
    const QByteArray row = R"({"id":7,"easting":1,"northing":2,"elevation":3})";
    const QList<QByteArray> invalid{
        "broken", "{}", "{\"points\":[] } trailing", "{\"points\":[{}]}",
        "{\"points\":[" + row + "," + row + "]}",
        R"({"points":[{"id":2147483648,"easting":1,"northing":2,"elevation":3}]})",
        R"({"points":[{"id":1,"easting":1e999,"northing":2,"elevation":3}]})",
        R"({"points":[{"id":1,"easting":1,"northing":2,"elevation":3,"labelStyle":99}]})",
        R"({"formatVersion":99,"points":[]})"};
    for (const auto& bytes : invalid) {
      QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
      QVERIFY(!LoadSurveyPointsFromJsonFile(state, path.toUtf8().constData(), log));
      QCOMPARE(state.surveyPoints.size(), std::size_t(1));
      QCOMPARE(state.surveyPoints[0].easting, 10.0);
      QCOMPARE(state.surveyPoints[0].labelMtextAnnId, label);
      QCOMPARE(state.cadAnnotations.size(), std::size_t(1));
      QCOMPARE(state.cadGpuRevision, revision);
      QCOMPARE(state.documents[state.activeDrawingIdx].undoStack.size(), undoCount);
    }
    QVERIFY(SaveSurveyPointsToJsonFile(state, path.toUtf8().constData(), log));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
    state.surveyPoints[0].elevation = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(!SaveSurveyPointsToJsonFile(state, path.toUtf8().constData(), log));
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); file.close();
    state.surveyPoints[0].elevation = 30;
    // Replacing a directory fails after staging output; it must leave no temporary file.
    const auto blocked = directory.filePath("blocked.json");
    QVERIFY(QDir().mkpath(blocked));
    QVERIFY(!SaveSurveyPointsToJsonFile(state, blocked.toUtf8().constData(), log));
    QVERIFY(QFileInfo(blocked).isDir());
    QCOMPARE(QDir(directory.path()).entryList({"*.tmp-*"}, QDir::Files).size(), 0);
  }

  void surveyJsonLegacyWhitespaceAndEmptyReplacementAreUndoable() {
    QTemporaryDir directory; const auto path = directory.filePath("legacy.json");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({ "points" : [ { "id" : 2, "easting" : 1.5, "northing" : 2.5, "elevation" : 3.5, "description" : "EG\u00e9" } ] })"); file.close();
    EditorWindow window; window.CreateDrawingFromTemplate(); std::vector<std::string> log;
    QVERIFY(LoadSurveyPointsFromJsonFile(window.State(), path.toUtf8().constData(), log));
    QCOMPARE(window.State().surveyPoints[0].rawDescription, std::string("EGé"));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\"points\":[]}"); file.close();
    AppCommandState::ProjectSession session; session.uid = 88; session.readOnly = true;
    window.State().openProjects.push_back(session);
    window.State().drawingTabs[window.State().activeDrawingIdx].projectUid = 88;
    QVERIFY(!LoadSurveyPointsFromJsonFile(window.State(), path.toUtf8().constData(), log));
    QCOMPARE(window.State().surveyPoints[0].id, 2);
    window.State().drawingTabs[window.State().activeDrawingIdx].projectUid = 0;
    window.State().openProjects.clear();
    QVERIFY(LoadSurveyPointsFromJsonFile(window.State(), path.toUtf8().constData(), log));
    QVERIFY(window.State().surveyPoints.empty()); QVERIFY(window.State().cadAnnotations.empty());
    window.Submit("UNDO");
    QCOMPARE(window.State().surveyPoints[0].id, 2);
    QCOMPARE(window.State().cadAnnotations.size(), std::size_t(1));
  }

  void nativeDatabaseGridResolvesCollisionAndHonorsReadOnly() {
    QTemporaryDir directory;
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& state = window.State();
    AppCommandState::ProjectSession session;
    session.uid = 8;
    session.project.file = std::filesystem::path(directory.filePath("Grid.gsproj").toStdWString());
    session.points = std::make_shared<projpts::Db>();
    SurveyPoint a; a.id = 10; a.easting = 6000000.123456789; a.description = "EG";
    SurveyPoint b = a; b.id = 20;
    session.points->points = {{a, "DrawingA.dwg"}, {b, "DrawingB.dwg"}};
    session.points->revision = 1;
    state.openProjects.push_back(session);
    state.drawingTabs[state.activeDrawingIdx].projectUid = 8;
    state.drawingTabs[state.activeDrawingIdx].pointsMode = AppCommandState::DrawingTab::PointsMode::Shared;
    std::vector<std::string> log; SyncProjectPoints(state, log, 0);
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("surveyPointGrid"); QVERIFY(dialog);
      auto* table = dialog->findChild<QTableWidget*>("pointGrid");
      table->sortItems(0, Qt::AscendingOrder);
      QTimer::singleShot(0, [&] {
        for (auto* widget : QApplication::topLevelWidgets()) if (auto* question = qobject_cast<QMessageBox*>(widget))
          for (auto* button : question->buttons()) if (button->text() == "Next free number") button->click();
      });
      table->item(0, 0)->setText("20");
      QCOMPARE(session.points->points.size(), std::size_t(2));
      QCOMPARE(session.points->points[0].point.id, 1);
      QCOMPARE(session.points->points[1].point.id, 20);
      QCOMPARE(session.points->points[0].point.easting, a.easting);
      QVERIFY(!state.surveyPointGridConflict.active);
      AnswerNextDialog(QMessageBox::Cancel);
      table->item(0, 0)->setText("20");
      QCOMPARE(session.points->points[0].point.id, 1);
      QCOMPARE(session.points->points.size(), std::size_t(2));
      QTimer::singleShot(0, [&] {
        for (auto* widget : QApplication::topLevelWidgets()) if (auto* question = qobject_cast<QMessageBox*>(widget))
          for (auto* button : question->buttons()) if (button->text() == "Overwrite") button->click();
      });
      table->item(0, 0)->setText("20");
      QCOMPARE(session.points->points.size(), std::size_t(1));
      QCOMPARE(session.points->points[0].point.id, 20);
      QCOMPARE(session.points->points[0].sourceDrawing, std::string("DrawingA.dwg"));
      AnswerNextDialog(QMessageBox::Ok);
      table->item(0, 2)->setText("NaN");
      QCOMPARE(session.points->points[0].point.easting, a.easting);
      dialog->reject();
    });
    window.Submit("POINTDATABASE");
    state.openProjects[0].readOnly = true;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("surveyPointGrid"); QVERIFY(dialog);
      auto* table = dialog->findChild<QTableWidget*>("pointGrid");
      for (int row = 0; row < table->rowCount(); ++row)
        for (int column = 0; column < table->columnCount(); ++column)
          QVERIFY(!(table->item(row, column)->flags() & Qt::ItemIsEditable));
      dialog->reject();
    });
    window.Submit("POINTDATABASE");
  }

  void nativeProjectPointWarningsProtectSharedData() {
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(directory.filePath("Drawings")));
    QFile closedDrawing(directory.filePath("Drawings/Other.dwg"));
    QVERIFY(closedDrawing.open(QIODevice::WriteOnly)); closedDrawing.close();
    EditorWindow window; window.CreateDrawingFromTemplate(); auto& state = window.State();
    AppCommandState::ProjectSession session;
    session.uid = 18; session.project.name = "Shared test";
    session.project.file = std::filesystem::path(directory.filePath("Shared.gsproj").toStdWString());
    session.points = std::make_shared<projpts::Db>();
    SurveyPoint point; point.id = 10; point.easting = 1;
    SurveyPoint second = point; second.id = 20;
    session.points->points = {{point, "A.dwg"}, {second, "A.dwg"}}; session.points->revision = 1;
    state.openProjects.push_back(session);
    state.drawingTabs[state.activeDrawingIdx].projectUid = 18;
    state.drawingTabs[state.activeDrawingIdx].pointsMode = AppCommandState::DrawingTab::PointsMode::Shared;
    std::vector<std::string> log; SyncProjectPoints(state, log, 0);
    projpts::AddId(&state.pointVisibility.hidden, 10); SyncProjectPoints(state, log, 0);
    auto answer = [&](const QString& text) {
      QTimer::singleShot(0, &window, [&, text] {
        auto* message = window.findChild<QMessageBox*>("projectPointWarning");
        QVERIFY(message);
        for (auto* button : message->buttons()) if (button->text().remove('&') == text) { button->click(); return; }
        message->reject(); QFAIL("Expected project point warning action is missing");
      });
      window.Submit("REGEN");
      QVERIFY(!state.pointEditPrompt.active);
    };
    state.createPointsNextId = 10; QVERIFY(TryPlaceSurveyPoint(state, 100, 200, 300, log));
    answer("Cancel");
    QCOMPARE(session.points->points.size(), std::size_t(2));
    QCOMPARE(session.points->points[0].point.easting, 1.0);
    QCOMPARE(SurveyPointIndexForId(state, 10), -1);
    state.createPointsNextId = 10; QVERIFY(TryPlaceSurveyPoint(state, 100, 200, 300, log));
    answer("Renumber");
    QCOMPARE(session.points->points.size(), std::size_t(3));
    QCOMPARE(session.points->points[0].point.easting, 1.0);
    const int newNumber = session.points->points.back().point.id;
    QVERIFY(newNumber != 10 && newNumber != 20);
    RemoveSurveyPointAt(state, SurveyPointIndexForId(state, 20));
    answer("Hide in this drawing only");
    QCOMPARE(session.points->points.size(), std::size_t(3));
    QCOMPARE(SurveyPointIndexForId(state, 20), -1);
    RemoveSurveyPointAt(state, SurveyPointIndexForId(state, newNumber));
    answer("Cancel");
    QVERIFY(SurveyPointIndexForId(state, newNumber) >= 0);
    RemoveSurveyPointAt(state, SurveyPointIndexForId(state, newNumber));
    answer("Delete from project");
    QCOMPARE(session.points->points.size(), std::size_t(2));
    state.createPointsNextId = 10; QVERIFY(TryPlaceSurveyPoint(state, 500, 600, 700, log));
    answer("Overwrite");
    QCOMPARE(session.points->points.size(), std::size_t(2));
    QCOMPARE(session.points->points[0].point.easting, 500.0);
    QVERIFY(SurveyPointIndexForId(state, 10) >= 0);
  }

  void pdfAnnotationsSaveAsWithoutChangingSource() {
    QTemporaryDir directory;
    const auto source = directory.filePath("source.pdf");
    {
      QPdfWriter writer(source);
      QPainter painter(&writer);
      painter.drawText(100, 100, "PDF annotation test");
    }
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto originalBytes = original.readAll(); original.close();
    PdfWindow window(nullptr);
    QString error;
    QVERIFY2(window.LoadFile(source, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    pdfview::Annot annotation;
    annotation.kind = pdfview::Annot::Kind::Rect;
    annotation.x0 = 20; annotation.y0 = 30; annotation.x1 = 80; annotation.y1 = 100;
    QVERIFY2(window.AddAnnotation(annotation, error), qPrintable(error));
    QVERIFY(window.HasUnsavedChanges());
    QVERIFY(!window.SaveFile(source, error));
    QVERIFY(window.HasUnsavedChanges());
    const auto destination = directory.filePath("annotated.pdf");
    QVERIFY2(window.SaveFile(destination, error), qPrintable(error));
    QVERIFY(!window.HasUnsavedChanges());
    const auto annotations = pdfview::ReadAnnotations(std::filesystem::path(destination.toStdWString()));
    QCOMPARE(annotations.size(), std::size_t(1));
    QCOMPARE(annotations[0].kind, pdfview::Annot::Kind::Rect);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes);
    annotation.kind = pdfview::Annot::Kind::Length;
    annotation.pts = {{20, 30}, {80, 100}};
    QVERIFY(!window.AddAnnotation(annotation, error)); // No fabricated measurement without scale.
    annotation.kind = pdfview::Annot::Kind::Rect;
    annotation.x0 = std::numeric_limits<float>::quiet_NaN();
    QVERIFY(!window.AddAnnotation(annotation, error));
  }

  void pdfAnnotationSelectMoveResizeAndUndoAsSingleEdit() {
    QTemporaryDir directory;
    const auto source = directory.filePath("edit.pdf");
    { QPdfWriter writer(source); QPainter painter(&writer); painter.drawText(100, 100, "PDF annotation edit"); }
    PdfWindow window(nullptr); window.show();
    QString error;
    QVERIFY2(window.LoadFile(source, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    pdfview::Annot rect;
    rect.kind = pdfview::Annot::Kind::Rect;
    rect.x0 = 20; rect.y0 = 30; rect.x1 = 80; rect.y1 = 100;
    QVERIFY(window.AddAnnotation(rect, error));
    QVERIFY(window.findChild<QDoubleSpinBox*>("pdfAnnotationWidth"));
    const QPointF centre = window.PageToCanvas(50, 65);
    window.PageClick(centre, Qt::LeftButton);
    QCOMPARE(window.m_SelectedAnnotation, 0);
    window.MoveAnnotation(centre + QPointF(20, -10));
    QCOMPARE(window.m_Annotations.Items()[0].x0, 20.f); // drag is still only a preview
    window.EndAnnotation(centre + QPointF(20, -10), Qt::LeftButton);
    const float movedX = 20.f + static_cast<float>(20.0 * window.m_Document->Sizes()[0].wPt / window.m_Page->width());
    QVERIFY(std::abs(window.m_Annotations.Items()[0].x0 - movedX) < 1e-4f);
    QVERIFY(window.m_Annotations.Undo());
    QVERIFY(window.m_Annotations.Items()[0] == rect);
    QVERIFY(window.m_Annotations.Redo());
    QVERIFY(window.m_Annotations.Items()[0] != rect);
    window.m_SelectedAnnotation = 0;
    const auto beforeResize = window.m_Annotations.Items()[0];
    const QPointF grip = window.PageToCanvas(beforeResize.x0, beforeResize.y0);
    window.PageClick(grip, Qt::LeftButton);
    QVERIFY(window.m_AnnotationDragging);
    window.MoveAnnotation(grip + QPointF(12, 8));
    window.EndAnnotation(grip + QPointF(12, 8), Qt::LeftButton);
    QVERIFY(window.m_Annotations.Items()[0].x0 > beforeResize.x0);
    QVERIFY(window.m_Annotations.Items()[0].y0 < beforeResize.y0);
    QTest::keyClick(&window, Qt::Key_Delete);
    QVERIFY(window.m_Annotations.Items().empty());
    QVERIFY(window.m_Annotations.Undo());
    QCOMPARE(window.m_Annotations.Items().size(), std::size_t(1));
  }

  void pdfSaveAsRunsOffUiThreadAndKeepsEditsAfterFailure() {
    QTemporaryDir directory;
    const auto source = directory.filePath("async-source.pdf");
    { QPdfWriter writer(source); QPainter painter(&writer); painter.drawText(100, 100, "Async save"); }
    PdfWindow window(nullptr); window.show();
    QString error;
    QVERIFY2(window.LoadFile(source, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    pdfview::Annot annotation;
    annotation.kind = pdfview::Annot::Kind::Line;
    annotation.x0 = 20; annotation.y0 = 30; annotation.x1 = 90; annotation.y1 = 120;
    QVERIFY(window.AddAnnotation(annotation, error));
    const auto destination = directory.filePath("async-result.pdf");
    window.StartSaveAs(destination);
    QVERIFY(window.IsSaving());
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSaving(), 10000);
    QVERIFY(!window.HasUnsavedChanges());
    QCOMPARE(pdfview::ReadAnnotations(std::filesystem::path(destination.toStdWString())).size(), std::size_t(1));

    QVERIFY(window.AddAnnotation(annotation, error));
    QTimer::singleShot(100, &window, [] {
      if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) message->accept();
    });
    window.StartSaveAs(source); // saving over the source is refused by REQ-388
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSaving(), 10000);
    QVERIFY(window.HasUnsavedChanges());

    PdfWindow opening(nullptr); opening.show();
    opening.Open(source);
    QVERIFY(opening.IsBusy());
    QTRY_VERIFY_WITH_TIMEOUT(!opening.IsBusy() && opening.m_Document, 10000);
    QCOMPARE(QString::fromStdWString(opening.m_Document->Path().wstring()), source);
  }

  void pdfAnnotationsStayAlignedAndPersistOnRotatedPages() {
    QTemporaryDir directory;
    const auto base = directory.filePath("rotation-base.pdf");
    const auto rotated = directory.filePath("rotation-90.pdf");
    { QPdfWriter writer(base); QPainter painter(&writer); painter.drawText(100, 100, "Rotated page"); }
    QVERIFY(WriteRotatedCopy(base, rotated));
    PdfWindow window(nullptr); window.show();
    QString error;
    QVERIFY2(window.LoadFile(rotated, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    const QPointF first = window.PageToCanvas(100, 120);
    const QPointF second = window.PageToCanvas(220, 260);
    window.SetTool(static_cast<int>(pdfview::Annot::Kind::Rect), {});
    window.PageClick(first, Qt::LeftButton);
    window.PageClick(second, Qt::LeftButton);
    QCOMPARE(window.m_Annotations.Items().size(), std::size_t(1));
    const auto created = window.m_Annotations.Items()[0];
    QVERIFY(std::abs(created.x0 - 100.f) < 0.01f && std::abs(created.y0 - 120.f) < 0.01f);
    QVERIFY(std::abs(created.x1 - 220.f) < 0.01f && std::abs(created.y1 - 260.f) < 0.01f);
    const auto saved = directory.filePath("rotation-annotated.pdf");
    QVERIFY2(window.SaveFile(saved, error), qPrintable(error));
    const auto reopened = pdfview::ReadAnnotations(std::filesystem::path(saved.toStdWString()));
    QCOMPARE(reopened.size(), std::size_t(1));
    QVERIFY(std::abs(reopened[0].x0 - created.x0) < 0.01f && std::abs(reopened[0].y0 - created.y0) < 0.01f);
    QVERIFY(std::abs(reopened[0].x1 - created.x1) < 0.01f && std::abs(reopened[0].y1 - created.y1) < 0.01f);
    window.m_Annotations.SetScales({{0, pdfview::PageScale{1, pdfview::Unit::Inch, 20, pdfview::Unit::Foot}}});
    window.SetTool(static_cast<int>(pdfview::Annot::Kind::Length), {});
    window.PageClick(window.PageToCanvas(100, 150), Qt::LeftButton);
    window.PageClick(window.PageToCanvas(300, 150), Qt::LeftButton);
    QCOMPARE(window.m_Annotations.Items().size(), std::size_t(1)); // the third point is the dimension-line position
    window.PageClick(window.PageToCanvas(200, 200), Qt::LeftButton);
    QCOMPARE(window.m_Annotations.Items().size(), std::size_t(2));
    QCOMPARE(window.m_Annotations.Items()[1].kind, pdfview::Annot::Kind::Length);
    QVERIFY(std::abs(window.m_Annotations.Items()[1].offset - 50.f) < 0.02f);
  }

  void qtPdfAnnotationScreenshotOptionsForIssue910() {
    QTemporaryDir directory;
    const auto source = directory.filePath("options.pdf");
    { QPdfWriter writer(source); QPainter painter(&writer); painter.drawText(100, 100, "Annotation layout reference"); }
    PdfWindow window(nullptr);
    window.resize(1200, 900); window.show();
    QString error;
    QVERIFY2(window.LoadFile(source, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    pdfview::Annot rect;
    rect.kind = pdfview::Annot::Kind::Rect; rect.x0 = 90; rect.y0 = 690; rect.x1 = 330; rect.y1 = 790;
    rect.color = 0x29A9D6; rect.thickness = 2.0f;
    QVERIFY(window.AddAnnotation(rect, error));
    window.m_SelectedAnnotation = 0; window.m_Page->update();
    QCoreApplication::processEvents();
    const QImage surface = window.m_Page->grab().toImage();
    const QPointF gripAt = window.PageToCanvas(rect.x0, rect.y0) * surface.devicePixelRatio();
    bool visibleGrip = false;
    for (int y = qRound(gripAt.y()) - 5; y <= qRound(gripAt.y()) + 5; ++y)
      for (int x = qRound(gripAt.x()) - 5; x <= qRound(gripAt.x()) + 5; ++x)
        if (surface.rect().contains(x, y) && surface.pixelColor(x, y) == QColor(QtAccentFor(true).strong)) visibleGrip = true;
    QVERIFY(visibleGrip);
#ifdef GOSURVEY_TEST_ARTIFACT_DIR
    const QString dir = QtTestArtifactDirectory() + "/qt-910";
#else
    const QString dir = directory.path();
#endif
    QDir().mkpath(dir);
    const auto capture = [&](const QString& name) {
      QCoreApplication::processEvents();
      QVERIFY2(window.grab().save(dir + "/" + name), qPrintable(name));
    };
    capture("qt-910-final-deep-navy.png");
    window.setStyleSheet("QMainWindow{background:#f2f4f7;color:#202938;} QToolBar{background:#e4e9ef;border:0;spacing:5px;padding:3px;} QToolBar QToolButton{color:#202938;padding:4px 6px;} QMainWindow > QScrollArea{background:#cbd2db;}");
    capture("qt-910-option-a-light.png");
    window.setStyleSheet("QMainWindow{background:#10151f;} QToolBar{background:#1b2433;border:0;spacing:6px;padding:3px;} QToolBar QToolButton{color:#e8eef9;padding:4px 8px;border-radius:4px;} QToolBar QToolButton:hover{background:#2c3b57;} QToolBar QToolButton:checked{background:#315b9b;} QDockWidget{color:#e8eef9;} QDockWidget::title{background:#1b2433;padding:6px;} #pdfThumbnailList{background:#141b28;color:#cfd8ea;} QMainWindow > QScrollArea{background:#0e131c;}");
    capture("qt-910-option-b-deep-navy.png");
    window.setStyleSheet("QMainWindow{background:#18202b;color:#e5edf5;} QToolBar{background:#253343;border:0;border-bottom:2px solid #42c6b7;spacing:7px;padding:4px;} QToolBar QToolButton{color:#e5edf5;padding:5px 8px;border-radius:4px;} QToolBar QToolButton:checked{background:#176c69;} QDockWidget{color:#e5edf5;} QDockWidget::title{background:#253343;padding:6px;} #pdfThumbnailList{background:#1d2936;color:#d5e2ee;} QMainWindow > QScrollArea{background:#111820;}");
    capture("qt-910-option-c-slate-teal.png");
#ifdef GOSURVEY_TEST_ARTIFACT_DIR
    qInfo() << "Issue 910 screenshot options:" << dir;
#endif
  }

  // ---- GitHub #1034 / REQ-059: the ViewCube -------------------------------------------------------
  void issue1034ViewCubeControlsAnimateAndSettleWithinHalfASecond() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 820);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    auto& state = window.State();
    viewport->SetOrientation(0.f, 90.f);

    // The animation is the requirement's 0.5 s at most (a constant, so a slow CI machine cannot flake it),
    // does not snap (the click only starts it), and the real timer then drives it to the target.
    static_assert(kViewAnimSeconds <= 0.5f, "REQ-059: orientation changes settle within 0.5 s");
    const auto clickAndSettle = [&](const QPoint& at, float azimuth, float elevation, const char* what) {
      QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);
      QTRY_VERIFY2_WITH_TIMEOUT(!state.viewAnimActive,
          qPrintable(QString("%1: az %2 el %3 t %4").arg(what).arg(state.viewportAzimuthDeg).arg(state.viewportElevationDeg).arg(state.viewAnimT)), 10000);
      QVERIFY2(std::fabs(Camera::ShortestAzimuthDelta(state.viewportAzimuthDeg, azimuth)) < 0.01f, what);
      QVERIFY2(std::fabs(state.viewportElevationDeg - elevation) < 0.01f, what);
    };

    // Home -> SW isometric.
    clickAndSettle(Px(ViewCubeSceneOf(viewport, state).home), 45.f, 35.26439f, "home");
    // From the isometric view, clicking FRONT, LEFT and TOP sets each standard view.
    for (const char* label : {"FRONT", "LEFT", "TOP"}) {
      viewport->SetOrientation(45.f, 35.26439f);
      const viewcube::Scene scene = ViewCubeSceneOf(viewport, state);
      const viewcube::QuadFace* quad = nullptr;
      for (const auto& q : scene.quads)
        if (q.visible && std::string(viewcube::kFaces[q.face].label) == label) quad = &q;
      QVERIFY2(quad, label);
      const viewcube::Face& face = viewcube::kFaces[quad->face];
      const QPoint centre(qRound((quad->pts[0].x + quad->pts[1].x + quad->pts[2].x + quad->pts[3].x) / 4),
                          qRound((quad->pts[0].y + quad->pts[1].y + quad->pts[2].y + quad->pts[3].y) / 4));
      clickAndSettle(centre, face.azimuthDeg, face.elevationDeg, label);
    }
    // The arrow turns a quarter turn even though the view is already square, keeping the elevation.
    viewport->SetOrientation(0.f, 25.f);
    const viewcube::Scene scene = ViewCubeSceneOf(viewport, state);
    const float bearing = (viewcube::kArrowMidDeg - viewcube::kArrowGapDeg - 10.f) * 3.14159265f / 180.f;
    clickAndSettle(QPoint(qRound(scene.center.x + std::cos(bearing) * scene.arrowRadius),
                          qRound(scene.center.y - std::sin(bearing) * scene.arrowRadius)), 90.f, 25.f, "arrow");
  }

  void issue1034ViewCubeClicksNeverReachTheViewportBehindIt() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 820);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    auto& state = window.State();
    viewport->SetOrientation(0.f, 90.f);

    // A line across the whole viewport at the pixel row of the widget's bottom-right corner, which
    // is inside the widget but is no control.
    const viewcube::Scene scene = ViewCubeSceneOf(viewport, state);
    const int cornerX = qRound(scene.originX + scene.sizePx - 3), cornerY = qRound(scene.originY + scene.sizePx - 3);
    QVERIFY(viewcube::Contains(scene, cornerX, cornerY));
    QCOMPARE(viewcube::HitTest(scene, cornerX, cornerY).part, viewcube::Part::None);
    const auto ray = CadViewCamera(state).ScreenRay(static_cast<float>(cornerX), static_cast<float>(cornerY),
        static_cast<float>(viewport->width()), static_cast<float>(viewport->height()));
    ray3d::Vec3 world;
    QVERIFY(ray3d::RayPlaneIntersect(ray, CadActiveWorkPlane(state), &world));
    state.userLinesFlat = {world.x - 100000.0, world.y, 0.0, world.x + 100000.0, world.y, 0.0};
    state.userLineAttrs.push_back(EntityAttributes{});
    window.Refresh();

    // Outside the widget the line picks as normal ...
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(viewport->width() / 2, cornerY));
    QCOMPARE(state.selection.size(), std::size_t{1});
    state.selection.clear();
    // ... and a click on the widget, on a control or off one, never starts a selection or a pick.
    for (const QPoint& at : {QPoint(cornerX, cornerY), Px(scene.home), Px(scene.center)}) {
      QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);
      QVERIFY(!state.selBoxWaitingSecond);
      QVERIFY(state.selection.empty());
    }
    QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier, QPoint(cornerX, cornerY));
    QVERIFY(state.selection.empty());
  }

  void issue1034ManualOrbitCancelsAnimationAndTheCubeTracksTheCamera() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 820);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    auto& state = window.State();
    viewport->SetOrientation(0.f, 90.f);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, Px(ViewCubeSceneOf(viewport, state).home));
    if (!state.viewAnimActive) QSKIP("the click took longer than the animation on this machine");

    // Shift + middle drag is the hand orbit: it takes over at once.
    const QPoint start(viewport->width() / 3, viewport->height() / 2), finish = start + QPoint(30, 20);
    QTest::mousePress(viewport, Qt::MiddleButton, Qt::ShiftModifier, start);
    QMouseEvent move(QEvent::MouseMove, finish, viewport->mapToGlobal(finish), Qt::NoButton, Qt::MiddleButton,
                     Qt::ShiftModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::ShiftModifier, finish);
    QVERIFY(!state.viewAnimActive);
    const float azimuth = state.viewportAzimuthDeg, elevation = state.viewportElevationDeg;
    QTest::qWait(450);  // longer than the animation: nothing may keep pulling the view toward home
    QCOMPARE(state.viewportAzimuthDeg, azimuth);
    QCOMPARE(state.viewportElevationDeg, elevation);

    // After the orbit the cube is laid out for the camera as it now is.
    const viewcube::Scene scene = ViewCubeSceneOf(viewport, state);
    QCOMPARE(scene.cameraAzimuthDeg, azimuth);
    QCOMPARE(scene.cameraElevationDeg, elevation);
  }

  void issue1034ViewCubeScreenshots() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 820);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    viewport->grabFramebuffer();
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    auto& state = window.State();
    state.userLinesFlat = {-30, -20, 0, 30, -20, 0, 30, -20, 0, 30, 20, 0, 30, 20, 0, -30, 20, 0};
    for (int i = 0; i < 3; ++i) state.userLineAttrs.push_back(EntityAttributes{});
    window.Refresh();
    const QString artifacts = QtTestArtifactDirectory() + "/";
    struct Shot { const char* name; float azimuth, elevation; };
    for (const Shot& shot : {Shot{"plan", 0.f, 90.f}, Shot{"sw-isometric", 45.f, 35.26439f},
                             Shot{"front", 0.f, 0.f}, Shot{"back", 180.f, 0.f}, Shot{"bottom", 0.f, -90.f}}) {
      viewport->SetOrientation(shot.azimuth, shot.elevation);
      QCoreApplication::processEvents();
      QVERIFY(viewport->grabFramebuffer().save(artifacts + QString("qt-1034-viewcube-%1.png").arg(shot.name)));
    }
    // Hovering a control highlights it.
    viewport->SetOrientation(45.f, 35.26439f);
    const viewcube::Scene scene = ViewCubeSceneOf(viewport, state);
    QTest::mouseMove(viewport, Px(scene.home));
    QCoreApplication::processEvents();
    QVERIFY(viewport->grabFramebuffer().save(artifacts + "qt-1034-viewcube-home-hover.png"));
    // Under a rotated UCS the compass follows the UCS.
    state.activeUcs = ucs::RotatedAboutZ(ucs::Ucs{}, 30.0);
    viewport->SetOrientation(CadUcsViewAzimuthOffsetDeg(state), 90.f);
    QCoreApplication::processEvents();
    QVERIFY(viewport->grabFramebuffer().save(artifacts + "qt-1034-viewcube-rotated-ucs.png"));
  }

  // GitHub #1034 follow-ups: the painted crosshair replaces the native cursor, and a viewport click must not
  // rebuild the Toolspace tree (that rebuild cost ~100 ms per click).
  void issue1034ViewportHidesNativeCursorAndClicksDoNotRebuildTheToolspace() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 820);
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
    viewport->grabFramebuffer();
    const auto moveTo = [viewport](const QPoint& at) {  // delivered directly: a synthetic cursor move may not arrive on the hidden desktop
      QMouseEvent move(QEvent::MouseMove, at, viewport->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(viewport, &move);
    };
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    moveTo(QPoint(200, 300));
    QCOMPARE(viewport->cursor().shape(), Qt::BlankCursor);
    const viewcube::Scene scene = ViewCubeSceneOf(viewport, window.State());
    moveTo(Px(scene.center));
    QCOMPARE(viewport->cursor().shape(), Qt::ArrowCursor);  // the ViewCube keeps a normal pointer
    moveTo(QPoint(200, 300));
    QCOMPARE(viewport->cursor().shape(), Qt::BlankCursor);

    auto* tree = window.findChild<QTreeWidget*>();
    QVERIFY(tree && tree->topLevelItemCount() > 0);
    const QTreeWidgetItem* first = tree->topLevelItem(0);
    window.Refresh();
    QCOMPARE(tree->topLevelItem(0), first);  // unchanged contents: same items
    window.State().blockDefs.push_back({});
    window.State().blockDefs.back().name = "BLK1034";
    window.Refresh();
    QVERIFY(!tree->findItems("BLK1034", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());  // a change still shows
  }

  // ---- Issue #1028 / REQ-093: the Qt splash progress bar and phase label advance ----
  void issue1028SplashProgressBarAndPhaseLabelAdvanceDuringTheDisplayTime() {
    using namespace qtsplash;
    QCOMPARE(kMinimumDisplayMs, 8000);
    QCOMPARE(ProgressAt(0), 0.0);
    QCOMPARE(ProgressAt(kMinimumDisplayMs / 2), 0.5);
    QCOMPARE(ProgressAt(kMinimumDisplayMs), 1.0);
    QCOMPARE(ProgressAt(qint64{kMinimumDisplayMs} * 4), 1.0);
    QCOMPARE(ProgressAt(-250), 0.0);
    // Six neutral phases share the display time evenly, in order, and the last one holds.
    const QStringList phases{"Starting GoSurvey…", "Gathering resources…", "Preparing workspace…",
                             "Setting up drawing tools…", "Loading your settings…", "Almost ready…"};
    for (int phase = 0; phase < phases.size(); ++phase)
      QCOMPARE(PhaseLabelAt(qint64{kMinimumDisplayMs} * phase / phases.size() + 10), phases[phase]);
    QCOMPARE(PhaseLabelAt(kMinimumDisplayMs), phases.last());
    QCOMPARE(PhaseLabelAt(qint64{kMinimumDisplayMs} * 4), phases.last());
    QCOMPARE(PhaseLabelAt(-250), phases.first());

    const QPixmap source(QtAsset("qt-splash-bg.png"));
    QVERIFY(!source.isNull());
    const QPixmap backdrop = MakeBackdrop(source);
    QCOMPARE(backdrop.size(), QSize(800, 450));
    const auto frameAt = [&](qint64 elapsedMs) {
      return MakeFrame(backdrop, ProgressAt(elapsedMs), PhaseLabelAt(elapsedMs)).toImage();
    };
    // The bar is 280 px wide starting at x=48; sample its vertical middle.
    const QColor fill("#148cff");
    const auto filledPixels = [&](const QImage& image) {
      int count = 0;
      for (int x = 48; x < 48 + 280; ++x)
        if (image.pixelColor(x, 374) == fill) ++count;
      return count;
    };
    const QImage start = frameAt(0);
    const QImage middle = frameAt(kMinimumDisplayMs / 2);
    const QImage end = frameAt(kMinimumDisplayMs);
    QCOMPARE(filledPixels(start), 0);
    QVERIFY(filledPixels(middle) > 100 && filledPixels(middle) < 180);
    QVERIFY(filledPixels(end) > 260);
    // Every phase draws a different label row, and the wordmark above never changes.
    const QRect labelRow(46, 345, 260, 20);
    const QRect wordmarkRow(46, 190, 400, 80);
    QList<QImage> frames;
    for (int phase = 0; phase < phases.size(); ++phase)
      frames << frameAt(qint64{kMinimumDisplayMs} * phase / phases.size() + 10);
    for (int phase = 1; phase < frames.size(); ++phase) {
      QVERIFY(frames[phase - 1].copy(labelRow) != frames[phase].copy(labelRow));
      QVERIFY(filledPixels(frames[phase - 1]) <= filledPixels(frames[phase]));
      QCOMPARE(frames[phase - 1].copy(wordmarkRow), frames[phase].copy(wordmarkRow));
    }
#ifdef GOSURVEY_TEST_ARTIFACT_DIR
    const QString dir = QtTestArtifactDirectory() + "/qt-1028";
    QDir().mkpath(dir);
    for (int phase = 0; phase < frames.size(); ++phase)
      QVERIFY(frames[phase].save(dir + QString("/qt-1028-splash-phase%1.png").arg(phase + 1)));
    QVERIFY(end.save(dir + "/qt-1028-splash-end.png"));
#endif
  }

  // ---- Issue #1037 / REQ-455: PDF command tabs and continuous zoom/pan navigation ----
  void issue1037RibbonSidePanelAndStatusBarNavigation() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "layout.pdf", pdfview::MakeSyntheticPdf(6, 3, 0));
    PdfWindow window(nullptr);
    window.resize(1200, 800);
    window.show();
    auto* tabs = window.findChild<QTabWidget*>("pdfRibbon");
    QVERIFY(tabs);
    QStringList titles;
    for (int i = 0; i < tabs->count(); ++i) titles << tabs->tabText(i);
    QCOMPARE(titles, (QStringList{"Open/View", "Annotate", "Measure", "Review"}));
    // Every command of a tab: the ribbon buttons' actions plus whatever sits in a collapsed panel's menu.
    const auto actionsIn = [&](int tab) {
      QStringList names;
      for (auto* button : tabs->widget(tab)->findChildren<QToolButton*>()) {
        if (button->defaultAction()) names << button->defaultAction()->text();
        if (button->menu()) for (auto* action : button->menu()->actions()) names << action->text();
      }
      return names;
    };
    const QStringList expected[] = {
      {"Open...", "Save As...", "Extract pages...", "Find...", "Side panel"},
      {"Select", "Browse", "Text", "Line", "Rectangle", "Ellipse", "Color...", "Apply style to selected", "Undo", "Redo"},
      {"Length", "Polyline length", "Area", "Angle", "Check", "Undo", "Redo", "Set scale...", "Snap: off"},
      {"Extract coordinates", "Audit scale", QStringLiteral("Compare revisions\u2026")}};
    for (int tab = 0; tab < 4; ++tab) {
      const QStringList actual = actionsIn(tab);
      for (const QString& name : expected[tab]) QVERIFY2(actual.contains(name), qPrintable(titles[tab] + " is missing " + name));
    }
    for (auto* bar : window.findChildren<QToolBar*>())
      QVERIFY2(bar->objectName() == "pdfCommandBar" || bar->objectName() == "pdfFindBar", qPrintable(bar->objectName()));
    QVERIFY(tabs->widget(0)->findChild<QComboBox*>("pdfLayoutBox"));
    // Page navigation and zoom are on the bottom bar only, not in the ribbon.
    for (const QString& name : {"Previous", "Next", "Zoom out", "Zoom in", "Fit width", "Fit page"}) QVERIFY2(!actionsIn(0).contains(name), qPrintable(name));
    QVERIFY(!tabs->widget(0)->findChild<QSpinBox*>());
    QVERIFY(tabs->widget(1)->findChild<QDoubleSpinBox*>("pdfAnnotationWidth"));
    QVERIFY(tabs->widget(1)->findChild<QCheckBox*>("pdfAnnotationBold"));
    QVERIFY(tabs->widget(2)->findChild<QPushButton*>("pdfScaleChecksButton"));
    QVERIFY(tabs->widget(2)->findChild<QCheckBox*>("pdfShowChecks"));
    QVERIFY(tabs->widget(3)->findChild<QToolButton*>("tool_pdfAuditScale"));
    for (int tab = 0; tab < 4; ++tab) QVERIFY(!tabs->widget(tab)->findChildren<QLabel*>("ribbonPanelLabel").isEmpty());
    const char* shots[] = {"open-view", "annotate", "measure", "review"};
    for (int tab = 0; tab < 4; ++tab) {
      tabs->setCurrentIndex(tab);
      QTest::qWait(50);
      QVERIFY(window.grab().save(Issue905Shot((std::string("qt-1037-tab-") + shots[tab] + ".png").c_str())));
    }
    tabs->setCurrentIndex(0);
    // Side panel: three buttons that say what they are (text, not just an icon) choose the page beside them.
    auto* strip = window.findChild<QWidget*>("pdfSideStrip");
    auto* pages = window.findChild<QStackedWidget*>("pdfSidePages");
    QVERIFY(strip && pages);
    const QStringList labels{"Thumbnails", "Tool Chest", "Properties"};
    for (int i = 0; i < 3; ++i) {
      auto* button = strip->findChild<QToolButton*>(QString("pdfSideButton_") + QString(labels[i]).remove(' '));
      QVERIFY2(button, qPrintable(labels[i]));
      QCOMPARE(button->text(), labels[i]);
      QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
      QVERIFY(!button->icon().isNull());
    }
    QCOMPARE(pages->currentIndex(), 0);
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-side-thumbnails.png")));
    strip->findChild<QToolButton*>("pdfSideButton_ToolChest")->click();
    QCOMPARE(pages->currentIndex(), 1);
    auto* chestRectangle = pages->findChild<QToolButton*>("pdfChest_Rectangle");
    QVERIFY(chestRectangle);
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-side-tool-chest.png")));
    chestRectangle->click();
    QCOMPARE(window.m_Tool, 2); // the chest button is the very same tool as the ribbon's
    QVERIFY(Issue906Action(window, "Rectangle")->isChecked());
    strip->findChild<QToolButton*>("pdfSideButton_Properties")->click();
    QCOMPARE(pages->currentIndex(), 2);
    QCOMPARE(window.findChild<QLabel*>("pdfPropFile")->text(), QString("-")); // nothing open yet
    // Open a six-page file: the properties follow it and the page.
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    QCOMPARE(window.findChild<QLabel*>("pdfPropFile")->text(), QString("layout.pdf"));
    QCOMPARE(window.findChild<QLabel*>("pdfPropPages")->text(), QString("6"));
    QCOMPARE(window.findChild<QLabel*>("pdfPropNumber")->text(), QString("1 of 6"));
    QVERIFY(window.findChild<QLabel*>("pdfPropSize")->text().contains(" in"));
    QCOMPARE(window.findChild<QLabel*>("pdfPropScale")->text(), QString("Unscaled"));
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-side-properties.png")));
    // Navigation on the bottom bar: first / previous / page box / next / last and zoom, in step with the ribbon.
    auto* statusPage = window.statusBar()->findChild<QSpinBox*>("pdfPageNumber");
    auto* statusZoom = window.statusBar()->findChild<QSpinBox*>("pdfZoomPercent");
    QVERIFY(statusPage && statusZoom);
    QCOMPARE(window.statusBar()->findChild<QLabel*>("pdfStatusPageCount")->text(), QString("of 6"));
    QCOMPARE(statusPage->maximum(), 6);
    for (const char* name : {"pdfFirstPage", "pdfPreviousPage", "pdfNextPage", "pdfLastPage", "pdfZoomOut", "pdfZoomIn", "pdfFitWidth", "pdfFitPage"}) {
      auto* button = window.statusBar()->findChild<QToolButton*>(QString("pdfStatus_") + name);
      QVERIFY2(button && !button->icon().isNull() && button->height() >= 30, name);
    }
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfNextPage")->click();
    QCOMPARE(window.CurrentPage(), 1);
    QCOMPARE(statusPage->value(), 2);
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfLastPage")->click();
    QCOMPARE(window.CurrentPage(), 5);
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfFirstPage")->click();
    QCOMPARE(window.CurrentPage(), 0);
    statusPage->setValue(4);
    QCOMPARE(window.CurrentPage(), 3);
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfPreviousPage")->click();
    QCOMPARE(window.CurrentPage(), 2);
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfFirstPage")->click();
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    statusZoom->setValue(50);
    QVERIFY(std::abs(window.ZoomPercent() - 50.0) < 1.0);
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfZoomIn")->click();
    QVERIFY(window.ZoomPercent() > 55.0);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.statusBar()->findChild<QToolButton*>("pdfStatus_pdfFitWidth")->click();
    QCOMPARE(window.m_Page->width(), qRound(window.WidthAtZoomOne(0)));
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-status-navigation.png")));
    // The side panel hides and shows from the ribbon.
    window.findChild<QAction*>("pdfThumbnailsAction")->trigger();
    QVERIFY(!window.findChild<QDockWidget*>("pdfThumbnails")->isVisible());
    // Narrow window: ribbon panels collapse into menus holding the same actions; nothing is lost.
    window.resize(560, 800);
    tabs->setCurrentIndex(1);
    QTest::qWait(50);
    auto* statusMore = window.findChild<QToolButton*>("pdfStatusMore");
    QVERIFY(statusMore && statusMore->isVisible());
    for (const char* hiddenName : {"pdfStatus_pdfFirstPage", "pdfStatus_pdfLastPage", "pdfStatus_pdfFitPage"})
      QVERIFY(!window.findChild<QToolButton*>(hiddenName)->isVisible());
    QVERIFY(window.statusBar()->grab().save(Issue905Shot("qt-1037-status-narrow.png")));
    QList<QWidget*> statusControls;
    for (const char* name : {"pdfActiveTool", "pdfStatusPageLabel", "pdfPageNumber", "pdfStatusPageCount",
                             "pdfStatus_pdfPreviousPage", "pdfStatus_pdfNextPage", "pdfStatus_pdfZoomOut",
                             "pdfZoomPercent", "pdfStatus_pdfZoomIn", "pdfStatus_pdfFitWidth", "pdfStatusMore"}) {
      auto* control = window.statusBar()->findChild<QWidget*>(name);
      QVERIFY(control);
      if (control->isVisible()) statusControls << control;
    }
    std::sort(statusControls.begin(), statusControls.end(), [](QWidget* a, QWidget* b) { return a->x() < b->x(); });
    for (int i = 1; i < statusControls.size(); ++i)
      QVERIFY2(statusControls[i - 1]->geometry().right() < statusControls[i]->geometry().left(),
               qPrintable(statusControls[i - 1]->objectName() + " overlaps " + statusControls[i]->objectName()));
    auto* annotatePage = dynamic_cast<pdfribbon::Page*>(tabs->widget(1));
    QVERIFY(annotatePage);
    annotatePage->setFixedWidth(90); // force every collapsed panel into the More menu
    annotatePage->Fit();
    QTest::qWait(50);
    QVERIFY(actionsIn(1).contains("Rectangle") && actionsIn(1).contains("Color..."));
    auto* more = tabs->widget(1)->findChild<QToolButton*>("pdfRibbonMore");
    QVERIFY(more);
    QVERIFY2(more->isVisible(), qPrintable(QString("page width %1, hidden %2, overflow entries %3")
        .arg(annotatePage->width()).arg(more->isHidden()).arg(more->menu()->actions().size())));
    QVERIFY(more->menu());
    const auto overflowPanels = more->menu()->actions();
    QVERIFY(!overflowPanels.isEmpty());
    QStringList overflowActionTexts;
    QMenu* annotationOverflowMenu = nullptr;
    for (auto* panelAction : overflowPanels) if (panelAction->menu()) {
      for (auto* action : panelAction->menu()->actions()) {
        overflowActionTexts << action->text();
        if (action->text() == "Rectangle") annotationOverflowMenu = panelAction->menu();
      }
    }
    QVERIFY2(overflowActionTexts.contains("Rectangle") && overflowActionTexts.contains("Color..."),
             qPrintable(overflowActionTexts.join(", ")));
    auto* overflowPanelMenu = annotationOverflowMenu;
    QVERIFY(overflowPanelMenu);
    overflowPanelMenu->popup(QPoint(120, 120));
    QTRY_VERIFY_WITH_TIMEOUT(overflowPanelMenu->isVisible(), 1000);
    QVERIFY(overflowPanelMenu->grab().save(Issue905Shot("qt-1037-ribbon-overflow-actions.png")));
    overflowPanelMenu->hide();
    QVERIFY(annotatePage->grab().save(Issue905Shot("qt-1037-ribbon-narrow.png")));
    bool mirroredWidth = false;
    for (auto* button : tabs->widget(1)->findChildren<QToolButton*>())
      if (button->menu() && button->objectName().contains("Style"))
        for (auto* action : button->menu()->actions())
          if (auto* widgetAction = qobject_cast<QWidgetAction*>(action))
            if (qobject_cast<QDoubleSpinBox*>(widgetAction->defaultWidget())) mirroredWidth = true;
    QVERIFY(mirroredWidth);
  }

  void issue1037ContinuousLayoutScrollsAndStaysSynchronized() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "continuous.pdf", pdfview::MakeSyntheticPdf(8, 3, 0));
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    QCOMPARE(window.CurrentLayout(), PdfWindow::Layout::PageByPage); // the default keeps the existing behaviour
    auto* spin = window.findChild<QSpinBox*>("pdfPageNumber");
    auto* bar = window.m_Scroll->verticalScrollBar();
    window.SetLayout(PdfWindow::Layout::Continuous);
    QCOMPARE(window.findChild<QComboBox*>("pdfLayoutBox")->currentIndex(), 1);
    QCOMPARE(window.m_PageTop.size(), std::size_t(8));
    int stack = PdfWindow::kPageGap;
    for (int page = 0; page < 8; ++page) stack += window.m_PageHpx[static_cast<std::size_t>(page)] + PdfWindow::kPageGap;
    QCOMPARE(window.m_Page->height(), stack);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(0) == 1, 10000);
    // Scrolling: the page in the middle of the view becomes the current page; the strip follows.
    bar->setValue(window.m_PageTop[4]);
    QCOMPARE(spin->value(), 5);
    QCOMPARE(window.m_ThumbView->currentIndex().row(), 4);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(4) == 1, 10000);
    // Choosing a page scrolls it to the top and the box keeps it.
    window.ScrollToPage(2);
    QCOMPARE(spin->value(), 3);
    QCOMPARE(bar->value(), window.m_PageTop[2] - PdfWindow::kPageGap);
    spin->setValue(7);
    QCOMPARE(window.CurrentPage(), 6);
    QCOMPARE(bar->value(), std::min(bar->maximum(), window.m_PageTop[6] - PdfWindow::kPageGap));
    QTest::keyClick(&window, Qt::Key_Home);
    QCOMPARE(spin->value(), 1);
    QCOMPARE(bar->value(), 0);
    QTest::keyClick(&window, Qt::Key_PageDown);
    QCOMPARE(spin->value(), 2);
    QTest::keyClick(&window, Qt::Key_End);
    QCOMPARE(spin->value(), 8);
    QTest::keyClick(&window, Qt::Key_PageUp);
    QCOMPARE(spin->value(), 7);
    // The plain wheel scrolls the document continuously.
    window.ScrollToPage(0);
    const int before = bar->value();
    Issue1037Wheel(window.m_Page, QPoint(300, 200), -120, Qt::NoModifier);
    QVERIFY(bar->value() > before);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(0) == 1, 10000);
    QVERIFY(window.grab().save(Issue905Shot("qt-1037-continuous.png")));
    // Back to one page: the page is kept and drawn again.
    window.ScrollToPage(3);
    window.SetLayout(PdfWindow::Layout::PageByPage);
    QCOMPARE(spin->value(), 4);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    window.SetLayout(PdfWindow::Layout::Continuous);
    QCOMPARE(spin->value(), 4);
    QCOMPARE(bar->value(), std::min(bar->maximum(), window.m_PageTop[3] - PdfWindow::kPageGap));
  }

  void issue1037CtrlWheelZoomKeepsThePointerAnchored() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "zoom.pdf", pdfview::MakeSyntheticPdf(3, 3, 0));
    for (const auto layout : {PdfWindow::Layout::PageByPage, PdfWindow::Layout::Continuous}) {
      PdfWindow window(nullptr);
      window.resize(1000, 800);
      window.show();
      QString error;
      QVERIFY2(window.LoadFile(path, error), qPrintable(error));
      QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      window.SetLayout(layout);
      if (layout == PdfWindow::Layout::PageByPage) QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      auto* viewport = window.m_Scroll->viewport();
      const QPoint pointer(viewport->width() / 3, viewport->height() / 2);
      for (const int notches : {2, 2, -1}) {
        const double zoomBefore = window.m_Zoom;
        const auto anchor = window.CaptureAnchor(pointer);
        const QPoint onCanvas = window.m_Page->mapFrom(viewport, pointer);
        Issue1037Wheel(window.m_Page, onCanvas, notches * 120, Qt::ControlModifier);
        QVERIFY2((notches > 0) == (window.m_Zoom > zoomBefore), "Ctrl+wheel changes the zoom the right way");
        // The page point that was under the pointer is still under it, within a pixel.
        const QRect rect = window.PageRect(anchor.page);
        const QPointF expected(rect.x() + anchor.fx * rect.width(), rect.y() + anchor.fy * rect.height());
        const QPoint actual = window.m_Page->mapFrom(viewport, pointer);
        const auto stopped = [](const QScrollBar* scrollBar) { return scrollBar->value() == 0 || scrollBar->value() == scrollBar->maximum(); };
        if (!stopped(window.m_Scroll->horizontalScrollBar()))
          QVERIFY2(std::abs(expected.x() - actual.x()) <= 1.0, qPrintable(QString("anchor moved sideways: expected %1 got %2").arg(expected.x()).arg(actual.x())));
        if (!stopped(window.m_Scroll->verticalScrollBar()))
          QVERIFY2(std::abs(expected.y() - actual.y()) <= 1.0, qPrintable(QString("anchor moved vertically: expected %1 got %2").arg(expected.y()).arg(actual.y())));
        QCOMPARE(window.m_ZoomBox->value(), std::clamp(qRound(window.ZoomPercent()), 5, 1000));
      }
      QVERIFY(window.grab().save(Issue905Shot(layout == PdfWindow::Layout::Continuous ? "qt-1037-zoomed-continuous.png" : "qt-1037-zoomed-page.png")));
    }
  }

  void issue1037MiddleDragPansWithoutTouchingTools() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "pan.pdf", pdfview::MakeSyntheticPdf(3, 3, 0));
    for (const auto layout : {PdfWindow::Layout::PageByPage, PdfWindow::Layout::Continuous}) {
      PdfWindow window(nullptr);
      window.resize(1000, 800);
      window.show();
      QString error;
      QVERIFY2(window.LoadFile(path, error), qPrintable(error));
      QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      window.SetLayout(layout);
      if (layout == PdfWindow::Layout::PageByPage) QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      window.SetZoomPercent(200);
      if (layout == PdfWindow::Layout::PageByPage) QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      auto* canvas = window.m_Page;
      auto* h = window.m_Scroll->horizontalScrollBar();
      auto* v = window.m_Scroll->verticalScrollBar();
      QVERIFY(h->maximum() > 100 && v->maximum() > 100);
      h->setValue(150); v->setValue(300);
      for (const char* tool : {"Rectangle", "Length", "Select"}) {
        Issue906Action(window, tool)->trigger();
        // A Length measurement is mid-pick: one point is down.
        if (QString(tool) == "Length") {
          QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->mapFromGlobal(window.m_Scroll->viewport()->mapToGlobal(QPoint(200, 200))));
          QCOMPARE(window.m_Draft.size(), std::size_t(1));
        }
        const auto draft = window.m_Draft;
        const int tool0 = window.m_Tool;
        const int selected = window.m_SelectedAnnotation;
        const QPoint start = window.m_Scroll->viewport()->mapToGlobal(QPoint(400, 300));
        const int hStart = h->value(), vStart = v->value();
        Issue1037MouseGlobal(canvas, QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
        QCOMPARE(canvas->cursor().shape(), Qt::ClosedHandCursor);
        Issue1037MouseGlobal(canvas, QEvent::MouseMove, start + QPoint(-40, -25), Qt::NoButton, Qt::MiddleButton);
        QCOMPARE(h->value(), hStart + 40);
        QCOMPARE(v->value(), vStart + 25);
        if (QString(tool) == "Select") QVERIFY(window.grab().save(Issue905Shot(layout == PdfWindow::Layout::Continuous ? "qt-1037-pan-continuous.png" : "qt-1037-pan-page.png")));
        Issue1037MouseGlobal(canvas, QEvent::MouseButtonRelease, start + QPoint(-40, -25), Qt::MiddleButton, Qt::NoButton);
        QVERIFY(canvas->cursor().shape() != Qt::ClosedHandCursor);
        QVERIFY(window.m_Annotations.Items().empty());
        QCOMPARE(window.m_Draft, draft);
        QCOMPARE(window.m_Tool, tool0);
        QCOMPARE(window.m_SelectedAnnotation, selected);
        QVERIFY(!window.HasUnsavedChanges());
        QVERIFY(!window.m_ExtractDragging);
        window.m_Draft.clear();
        h->setValue(150); v->setValue(300);
      }
      // The margin around the page pans as well (the viewport itself, outside the canvas).
      const int vBefore = v->value();
      const QPoint start = window.m_Scroll->viewport()->mapToGlobal(QPoint(5, 300));
      Issue1037MouseGlobal(window.m_Scroll->viewport(), QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
      Issue1037MouseGlobal(window.m_Scroll->viewport(), QEvent::MouseMove, start + QPoint(0, -30), Qt::NoButton, Qt::MiddleButton);
      Issue1037MouseGlobal(window.m_Scroll->viewport(), QEvent::MouseButtonRelease, start + QPoint(0, -30), Qt::MiddleButton, Qt::NoButton);
      QCOMPARE(v->value(), vBefore + 30);

      Issue906Action(window, "Extract coordinates")->trigger();
      QVERIFY(window.m_ExtractMode);
      const QPoint extractPanStart = window.m_Scroll->viewport()->mapToGlobal(QPoint(400, 300));
      Issue1037MouseGlobal(canvas, QEvent::MouseButtonPress, extractPanStart, Qt::MiddleButton, Qt::MiddleButton);
      QCOMPARE(canvas->cursor().shape(), Qt::ClosedHandCursor);
      Issue1037MouseGlobal(canvas, QEvent::MouseMove, extractPanStart + QPoint(-25, -15), Qt::NoButton, Qt::MiddleButton);
      Issue1037MouseGlobal(canvas, QEvent::MouseButtonRelease, extractPanStart + QPoint(-25, -15), Qt::MiddleButton, Qt::NoButton);
      QVERIFY(window.m_ExtractMode);
      QVERIFY(!window.m_ExtractDragging); // a pan while extraction is armed does not start a selection
      window.BeginExtract(QPointF(200, 200), Qt::LeftButton);
      QVERIFY(window.m_ExtractDragging);
      const QPoint duringExtract = window.m_Scroll->viewport()->mapToGlobal(QPoint(420, 320));
      Issue1037MouseGlobal(canvas, QEvent::MouseButtonPress, duringExtract, Qt::MiddleButton, Qt::MiddleButton);
      Issue1037MouseGlobal(canvas, QEvent::MouseMove, duringExtract + QPoint(-20, -10), Qt::NoButton, Qt::MiddleButton);
      Issue1037MouseGlobal(canvas, QEvent::MouseButtonRelease, duringExtract + QPoint(-20, -10), Qt::MiddleButton, Qt::NoButton);
      QVERIFY(window.m_ExtractMode);
      QVERIFY(window.m_ExtractDragging); // and a pan never ends the active extraction drag
      window.m_ExtractMode = false; window.m_ExtractDragging = false;
    }
  }

  void issue1037FitWidthFitPageTypedPercentAndKeyboard() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "fit.pdf", pdfview::MakeSyntheticPdf(5, 3, 0));
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    auto* viewport = window.m_Scroll->viewport();
    window.FitPage();
    QVERIFY(window.m_Page->height() <= viewport->height());
    QVERIFY(window.m_Page->width() <= viewport->width());
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    window.findChild<QAction*>("pdfFitWidth")->trigger();
    QCOMPARE(window.m_Page->width(), qRound(window.WidthAtZoomOne(0)));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    // A typed percentage is a percentage of the page's actual size at 96 dpi.
    window.m_ZoomBox->setValue(100);
    const double wPt = window.m_Document->Sizes()[0].wPt;
    QVERIFY(std::abs(window.m_Page->width() - wPt * 96.0 / 72.0) <= 1.5);
    QVERIFY(std::abs(window.ZoomPercent() - 100.0) < 0.5);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    window.m_ZoomBox->setValue(50);
    QVERIFY(std::abs(window.m_Page->width() - wPt * 96.0 / 72.0 / 2.0) <= 1.5);
    // Keyboard page navigation in the single-page layout.
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    auto* spin = window.findChild<QSpinBox*>("pdfPageNumber");
    QTest::keyClick(&window, Qt::Key_PageDown);
    QCOMPARE(spin->value(), 2);
    QTest::keyClick(&window, Qt::Key_End);
    QCOMPARE(spin->value(), 5);
    QTest::keyClick(&window, Qt::Key_PageUp);
    QCOMPARE(spin->value(), 4);
    QTest::keyClick(&window, Qt::Key_Home);
    QCOMPARE(spin->value(), 1);
    // Continuous: fit page puts the whole page in view, fit width spans the view.
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.SetLayout(PdfWindow::Layout::Continuous);
    window.FitPage();
    QVERIFY(window.m_PageHpx[0] <= viewport->height());
    window.FitWidth();
    QCOMPARE(window.m_PageWpx[0], qRound(window.WidthAtZoomOne(0)));
  }

  void issue1037ContinuousLayoutAnnotatesTheSecondPage() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "second.pdf", pdfview::MakeSyntheticPdf(3, 3, 0));
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    window.SetLayout(PdfWindow::Layout::Continuous);
    window.ScrollToPage(1);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(1) == 1, 10000);
    Issue906Action(window, "Rectangle")->trigger();
    const QRect second = window.PageRect(1);
    const QPoint first(second.left() + 100, second.top() + 150), last(second.left() + 300, second.top() + 350);
    QTest::mouseClick(window.m_Page, Qt::LeftButton, Qt::NoModifier, first);
    QTest::mouseClick(window.m_Page, Qt::LeftButton, Qt::NoModifier, last);
    QCOMPARE(window.m_Annotations.Items().size(), std::size_t(1));
    const auto& made = window.m_Annotations.Items()[0];
    QCOMPARE(made.page, 1); // the page under the pointer, not the page box
    const auto size = window.m_Document->Sizes()[1];
    QVERIFY(std::abs(made.x0 - 100.0 * size.wPt / second.width()) < 0.5);
    QVERIFY(std::abs(made.y0 - (second.height() - 150.0) * size.hPt / second.height()) < 0.5);
    // Paint at a deterministic 1x scale through the production strip renderer so antialiasing does
    // not depend on the host display's device-pixel ratio.
    QImage shot(window.m_Page->size(), QImage::Format_ARGB32_Premultiplied);
    shot.fill(Qt::transparent);
    {
      QPainter painter(&shot);
      QVERIFY(window.PaintStrip(painter, window.m_Page->rect()));
    }
    QVERIFY(shot.save(Issue905Shot("qt-1037-second-page.png")));
    const auto hasRedInk = [&](const QRect& rect) {
      const QImage page = shot.copy(rect);
      for (int y = 0; y < page.height(); ++y) {
        for (int x = 0; x < page.width(); ++x) {
          const QColor pixel = page.pixelColor(x, y);
          // A one-DIP outline at 1x blends with white; test its red hue instead of requiring a
          // pixel nearly equal to pure red as a higher-DPI capture might produce.
          if (pixel.red() >= 200 && pixel.red() - pixel.green() >= 20 && pixel.red() - pixel.blue() >= 20) return true;
        }
      }
      return false;
    };
    QVERIFY(hasRedInk(second));
    QVERIFY(!hasRedInk(window.PageRect(0)));
    QVERIFY(!hasRedInk(window.PageRect(2)));
    // A pick on one page and a click on another do not mix: the draft restarts on the new page.
    Issue906Action(window, "Line")->trigger();
    QTest::mouseClick(window.m_Page, Qt::LeftButton, Qt::NoModifier, QPoint(second.left() + 50, second.top() + 50));
    QCOMPARE(window.m_Draft.size(), std::size_t(1));
    const QRect third = window.PageRect(2);
    QTest::mouseClick(window.m_Page, Qt::LeftButton, Qt::NoModifier, QPoint(third.left() + 60, third.top() + 60));
    QCOMPARE(window.m_Annotations.Items().size(), std::size_t(1));
    QCOMPARE(window.m_Draft.size(), std::size_t(1));
    QCOMPARE(window.m_DraftPage, 2);
    QVERIFY(window.HasUnsavedChanges());
  }

  void issue1037ZoomReaches1000PercentByDrawingOnlyTheViewedWindow() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "big.pdf", pdfview::MakeSyntheticPdf(3, 3, 0));
    for (const auto layout : {PdfWindow::Layout::PageByPage, PdfWindow::Layout::Continuous}) {
      PdfWindow window(nullptr);
      window.resize(1000, 800);
      window.show();
      QString error;
      QVERIFY2(window.LoadFile(path, error), qPrintable(error));
      QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      window.SetLayout(layout);
      window.SetZoomPercent(1000);
      QVERIFY2(std::abs(window.ZoomPercent() - 1000.0) < 1.0, qPrintable(QString::number(window.ZoomPercent())));
      QVERIFY(window.m_Page->width() > 8000); // far past the old 4096 pixel cap
      // Zooming further is refused at the limit; the box shows it.
      window.ZoomBy(2.0, window.m_Scroll->viewport()->rect().center());
      QVERIFY(window.ZoomPercent() <= 1001.0);
      // Only the window around the view is drawn, and it covers the view.
      if (layout == PdfWindow::Layout::PageByPage) {
        QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull() && window.PageWindowCovers(), 20000);
        QVERIFY(window.m_Page->image.width() < window.m_Page->width() / 2);
        // Scrolling away redraws the window around the new view.
        window.m_Scroll->verticalScrollBar()->setValue(window.m_Scroll->verticalScrollBar()->maximum() / 2);
        QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && window.PageWindowCovers(), 20000);
      } else {
        QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(0) == 1, 20000);
        QVERIFY(window.m_StripCache[0].width() < window.PageRect(0).width() / 2);
        window.m_Scroll->verticalScrollBar()->setValue(window.m_Scroll->verticalScrollBar()->maximum() / 3);
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_StripCache.empty(), 20000);
      }
      QVERIFY(window.grab().save(Issue905Shot(layout == PdfWindow::Layout::Continuous ? "qt-1037-1000-continuous.png" : "qt-1037-1000-page.png")));
      window.SetZoomPercent(5);
      QVERIFY(std::abs(window.ZoomPercent() - 5.0) < 0.6);
    }
  }

  void issue1037RenderCacheStaysBoundedAndCloseWaitsForTheWorker() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "long.pdf", pdfview::MakeSyntheticPdf(40, 3, 0));
    {
      PdfWindow window(nullptr);
      window.resize(1000, 800);
      window.show();
      QString error;
      QVERIFY2(window.LoadFile(path, error), qPrintable(error));
      QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
      window.SetLayout(PdfWindow::Layout::Continuous);
      window.SetZoom(4.0, window.m_Scroll->viewport()->rect().center()); // huge pages: a few of them exceed the cap
      for (int page = 0; page < 10; ++page) {
        window.ScrollToPage(page);
        QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(page) == 1, 20000);
        std::size_t outside = 0;
        const auto [first, last] = window.VisiblePages();
        for (const auto& entry : window.m_StripCache)
          if (entry.first < first || entry.first > last) outside += static_cast<std::size_t>(entry.second.sizeInBytes());
        QVERIFY2(outside <= PdfWindow::kStripCacheBytes, "pages outside the view stay within the cap");
      }
      // The cache is byte-bounded, not entry-bounded; visible pages can keep ten or more images.
      window.ScrollToPage(30); // a render is now in flight; the window is destroyed while it runs
    }
  }

  void issue1037PageByPageWheelScrollsAndAStaleRenderIsCancelled() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "wheel.pdf", pdfview::MakeSyntheticPdf(3, 3, 0));
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    // REQ-455 clause 3: in page-by-page the plain wheel scrolls within the page and never changes the zoom.
    window.SetZoomPercent(300);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    const double zoom = window.ZoomPercent();
    const int before = window.m_Scroll->verticalScrollBar()->value();
    Issue1037Wheel(window.m_Scroll->viewport(), QPoint(300, 200), -120, Qt::NoModifier);
    QVERIFY(window.m_Scroll->verticalScrollBar()->value() > before);
    QCOMPARE(window.ZoomPercent(), zoom);
    // REQ-455 clause 8: a render whose view has changed is cancelled and redone for the new view.
    {
      std::unique_lock<std::recursive_mutex> hold(pdfview::PdfiumMutex()); // the worker cannot finish while this is held
      window.SetZoomPercent(150);
      QTRY_VERIFY_WITH_TIMEOUT(window.IsBusy(), 5000); // the render starts once the zooming settles
      window.SetZoomPercent(200);
      QVERIFY(window.m_Cancel.load());
      QVERIFY(window.m_Pending);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && !window.m_Page->image.isNull(), 10000);
    QCOMPARE(window.m_Page->imageFull, window.RenderPx(window.CurrentPage()));
  }

  // ---- Issue #905 / REQ-429: PDF find bar and thumbnail strip ----
  void pdfFindBarSearchesStepsWrapsAndDoesNotTouchAnnotations() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "find.pdf", pdfview::MakeSyntheticPdf(12, 3, 0));
    PdfWindow window(nullptr);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    auto* bar = window.findChild<QToolBar*>("pdfFindBar");
    auto* edit = window.findChild<QLineEdit*>("pdfFindEdit");
    auto* status = window.findChild<QLabel*>("pdfFindStatus");
    auto* matchCase = window.findChild<QCheckBox*>("pdfFindMatchCase");
    auto* spin = window.findChild<QSpinBox*>("pdfPageNumber");
    QVERIFY(bar && edit && status && matchCase && spin);
    QVERIFY(!bar->isVisible());

    pdfview::Annot rect;
    rect.kind = pdfview::Annot::Kind::Rect;
    rect.x0 = 20; rect.y0 = 30; rect.x1 = 80; rect.y1 = 100;
    QVERIFY2(window.AddAnnotation(rect, error), qPrintable(error));

    window.findChild<QAction*>("pdfFindAction")->trigger(); // Ctrl+F
    QVERIFY(bar->isVisible());
    QTRY_VERIFY(edit->hasFocus());

    QTest::keyClicks(edit, "page 1");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching() && status->text() == "1 of 4", 10000);
    QCOMPARE(spin->value(), 1);
    QTest::keyClick(edit, Qt::Key_Return); // Enter again = next match
    QTRY_COMPARE(status->text(), QString("2 of 4"));
    QCOMPARE(spin->value(), 10);
    QTest::keyClick(edit, Qt::Key_Return);
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(status->text(), QString("4 of 4"));
    QCOMPARE(spin->value(), 12);
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(status->text().startsWith("1 of 4") && status->text().contains("wrapped"));
    QCOMPARE(spin->value(), 1);
    window.findChild<QAction*>("pdfFindPrevious")->trigger();
    QVERIFY(status->text().startsWith("4 of 4") && status->text().contains("wrapped"));
    QCOMPARE(spin->value(), 12);
    window.findChild<QAction*>("pdfFindPrevious")->trigger();
    QCOMPARE(status->text(), QString("3 of 4"));

    // The current match is highlighted on the page (orange over white), the page itself is not edited.
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && Issue905HasHighlight(canvas->grab().toImage()), 10000);
    QVERIFY(canvas->grab().save(Issue905Shot("qt-905-find-highlight.png")));

    // Match case: the text is "Page", so the lower-case query matches nothing.
    matchCase->setChecked(true);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching() && status->text() == "No matches", 10000);
    edit->setText("Page 1");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching() && status->text() == "3 of 4", 10000); // first match on or after page 11

    // An empty text does nothing and clears the results.
    edit->clear();
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(status->text().isEmpty());

    // Searching never changes the annotation session: still one unsaved annotation, undoable once.
    QVERIFY(window.HasUnsavedChanges());
    QTest::keyClick(&window, Qt::Key_Escape); // Esc closes the bar
    QVERIFY(!bar->isVisible());
    QVERIFY(window.HasUnsavedChanges());
  }

  void issue1037ContinuousSearchAndSnapOnLaterPage() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "later-page.pdf", pdfview::MakeSyntheticPdf(12, 3, 0));
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.SetLayout(PdfWindow::Layout::Continuous);
    auto* edit = window.findChild<QLineEdit*>("pdfFindEdit");
    auto* status = window.findChild<QLabel*>("pdfFindStatus");
    auto* spin = window.findChild<QSpinBox*>("pdfPageNumber");
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    QVERIFY(edit && status && spin && canvas);
    edit->setText("Page 10");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching() && status->text() == "1 of 1", 10000);
    QCOMPARE(spin->value(), 10);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_Scroll->verticalScrollBar()->value() ==
        std::min(window.m_Scroll->verticalScrollBar()->maximum(), window.m_PageTop[9] - PdfWindow::kPageGap), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_StripCache.count(9) == 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(Issue905HasHighlight(canvas->grab().toImage()), 10000);

    auto laterPageSnap = std::make_shared<pdfview::SnapIndex>();
    laterPageSnap->Build({{53.f, 103.f, 3.f}});
    window.m_SnapPages[9].started = true;
    window.m_SnapPages[9].index = std::move(laterPageSnap);
    window.m_SnapOn = true;
    window.SetTool(static_cast<int>(pdfview::Annot::Kind::Line), QString());
    {
      PdfWindow::PageScope scope(window, 9);
      const auto size = window.m_Document->Sizes()[9];
      const double pixelsPerPoint = static_cast<double>(window.PageWidthPx()) / size.wPt;
      window.UpdateSnap(QPointF(53.0 * pixelsPerPoint,
                                (size.hPt - 103.0) * pixelsPerPoint));
    }
    QVERIFY(window.m_SnapHit);
    QTRY_VERIFY_WITH_TIMEOUT(Issue906HasSnapMarker(canvas->grab().toImage()), 10000);
  }

  void pdfFindBarReportsNoTextAndCancel() {
    QTemporaryDir directory;
    // A page with line work only: no text layer, like a scan.
    const QString scan = Issue905WritePdf(directory, "scan.pdf", pdfview::MakeLineWorkPdf(612, 792, 50, 1, 0));
    PdfWindow window(nullptr);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(scan, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    auto* edit = window.findChild<QLineEdit*>("pdfFindEdit");
    auto* status = window.findChild<QLabel*>("pdfFindStatus");
    window.findChild<QAction*>("pdfFindAction")->trigger();
    QTest::keyClicks(edit, "anything");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching(), 10000);
    QCOMPARE(status->text(), QString("This PDF has no searchable text"));

    const QString text = Issue905WritePdf(directory, "text.pdf", pdfview::MakeSyntheticPdf(40, 3, 0));
    QVERIFY2(window.LoadFile(text, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    QVERIFY(status->text().isEmpty()); // opening another file clears the search
    edit->setText("nothing like this");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching(), 10000);
    QCOMPARE(status->text(), QString("No matches"));

    // Esc cancels a running search: hold the PDFium lock so the worker cannot finish before the key arrives.
    edit->setText("Page");
    {
      std::unique_lock<std::recursive_mutex> hold(pdfview::PdfiumMutex());
      QTest::keyClick(edit, Qt::Key_Return);
      QVERIFY(window.IsSearching());
      QTest::keyClick(&window, Qt::Key_Escape);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching(), 10000);
    QCOMPARE(status->text(), QString("Search cancelled"));
    QVERIFY(window.findChild<QToolBar*>("pdfFindBar")->isVisible()); // Esc cancelled the search; the bar stays
  }

  void pdfThumbnailStripListsPagesRendersOnlyVisibleOnesAndFollowsThePage() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "plans.pdf", pdfview::MakeSyntheticPdf(500, 10, 50));
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 20000);
    auto* list = window.findChild<QListView*>("pdfThumbnailList");
    auto* dock = window.findChild<QDockWidget*>("pdfThumbnails");
    auto* spin = window.findChild<QSpinBox*>("pdfPageNumber");
    QVERIFY(list && dock && spin);
    QVERIFY(dock->isVisible()); // shown by default
    QCOMPARE(list->model()->rowCount(), 500);

    // Only what is on screen gets an image, and never more than the cache bound.
    QTRY_VERIFY_WITH_TIMEOUT(window.ThumbnailsKept() >= 3, 20000);
    QTest::qWait(600);
    QVERIFY2(window.ThumbnailsKept() < 60, qPrintable(QString::number(window.ThumbnailsKept())));
    QVERIFY(window.grab().save(Issue905Shot("qt-432-graphite-pdf-viewer.png")));
    QVERIFY(list->grab().save(Issue905Shot("qt-905-thumbnails.png")));

    // Clicking a thumbnail opens that page.
    const QModelIndex second = list->model()->index(1, 0);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(second).center());
    QCOMPARE(spin->value(), 2);

    // The strip follows the page chosen elsewhere.
    spin->setValue(300);
    const QModelIndex current = list->model()->index(299, 0);
    QCOMPARE(list->currentIndex(), current);
    QVERIFY(list->viewport()->rect().intersects(list->visualRect(current)));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 20000);

    // Paging through the whole file keeps the strip within its bound.
    for (int page = 1; page <= 500; page += 25) { spin->setValue(page); QTest::qWait(5); }
    QTest::qWait(300);
    QVERIFY(window.ThumbnailsKept() <= 120);

    // Hide and show again from the toolbar.
    auto* toggle = window.findChild<QAction*>("pdfThumbnailsAction");
    QVERIFY(toggle);
    toggle->trigger();
    QVERIFY(!dock->isVisible());
    toggle->trigger();
    QVERIFY(dock->isVisible());
  }

  void pdfThumbnailOfAnUnreadablePageStaysBlankAndTheOthersStillDraw() {
    // Page 3 (object 8) is turned into an array of the same length, so PDFium cannot load it as a page.
    std::string bytes = pdfview::MakeSyntheticPdf(6, 20, 0);
    const std::size_t object = bytes.find("\n8 0 obj\n<<");
    QVERIFY(object != std::string::npos);
    bytes.replace(object + 9, 2, "[ ");
    const std::size_t close = bytes.find(">>\nendobj", object);
    QVERIFY(close != std::string::npos);
    bytes.replace(close, 2, " ]");
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "broken-page.pdf", bytes);
    PdfWindow window(nullptr);
    window.resize(1000, 800);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    auto* list = window.findChild<QListView*>("pdfThumbnailList");
    QVERIFY(list);
    QCOMPARE(list->model()->rowCount(), 6);
    const int lastVisible = list->indexAt({4, list->viewport()->height() - 4}).row();
    QVERIFY(lastVisible >= 2); // the unreadable page 3 (row 2) is on screen
    QTRY_COMPARE_WITH_TIMEOUT(window.ThumbnailsKept(), lastVisible, 20000); // every visible page but page 3 (rows 0..lastVisible)
    QTest::qWait(300);
    QCOMPARE(window.ThumbnailsKept(), lastVisible);
    const QPixmap blank = list->model()->index(2, 0).data(Qt::DecorationRole).value<QPixmap>();
    QVERIFY(!blank.isNull());
    QCOMPARE(blank.toImage().pixelColor(blank.width() / 2, blank.height() / 2), QColor(Qt::white));
  }

  void qtPdfSearchAndThumbnailScreenshotEvidenceForIssue905() {
    QTemporaryDir directory;
    PdfWindow window(nullptr);
    window.resize(1000, 860);
    window.show();
    QString error;
    QVERIFY2(window.LoadFile(Issue905WritePdf(directory, "evidence.pdf", pdfview::MakeSyntheticPdf(24, 60, 6)), error), qPrintable(error));
    auto* list = window.findChild<QListView*>("pdfThumbnailList");
    QVERIFY(list);
    const int lastVisible = list->indexAt({4, list->viewport()->height() - 4}).row();
    QVERIFY(lastVisible >= 0);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy() && window.ThumbnailsKept() >= lastVisible + 1, 20000);
    QVERIFY(window.grab().save(Issue905Shot("qt-905-final-thumbnails.png"))); // find bar closed
    window.findChild<QAction*>("pdfFindAction")->trigger();
    auto* edit = window.findChild<QLineEdit*>("pdfFindEdit");
    auto* status = window.findChild<QLabel*>("pdfFindStatus");
    QTest::keyClicks(edit, "page 2");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsSearching() && status->text().contains(" of "), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    QTest::qWait(400);
    QVERIFY(window.grab().save(Issue905Shot("qt-905-final-match.png")));
    edit->setText("no such words");
    QTest::keyClick(edit, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("No matches"), 10000);
    QVERIFY(window.grab().save(Issue905Shot("qt-905-final-no-matches.png")));
  }

  // ---- Issue #908 / REQ-392/393/395: Qt PDF comparison and automatic scale audit ----
  void pdfScaleAuditShowsMatchesAndAppliesConsensusAsOneUndoableChange() {
    QTemporaryDir directory;
    const QString path = Issue905WritePdf(directory, "audit-sheet.pdf", Issue908AuditPdf());
    PdfWindow window(nullptr);
    window.resize(1100, 850); window.show();
    QString error;
    QVERIFY2(window.LoadFile(path, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    pdfview::PageScale originalScale;
    originalScale.pageValue = 1; originalScale.pageUnit = pdfview::Unit::Inch;
    originalScale.realValue = 20; originalScale.realUnit = pdfview::Unit::Foot;
    QVERIFY(window.m_Annotations.SetScales({{0, originalScale}}));
    window.findChild<QAction*>("pdfAuditScale")->trigger();
    auto* dialog = window.findChild<QDialog*>("pdfScaleAuditDialog");
    auto* table = window.findChild<QTableWidget*>("pdfAuditTable");
    auto* summary = window.findChild<QLabel*>("pdfAuditSummary");
    QVERIFY(dialog && table && summary);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_AuditReady, 10000);
    QVERIFY(window.m_AuditResult.consensusValid);
    QCOMPARE(window.m_AuditMatches.matches.size(), size_t(5));
    QVERIFY(summary->text().contains("Current scale"));
    window.findChild<QCheckBox*>("pdfAuditAllRows")->setChecked(true);
    QCOMPARE(table->rowCount(), 5);
    QVERIFY(window.grab().save(Issue908Shot("qt-908-audit-dialog.png")));

    window.findChild<QPushButton*>("pdfAuditUseConsensus")->click();
    QVERIFY(window.EffectiveScale(0));
    QVERIFY(window.HasUnsavedChanges());
    QVERIFY(std::abs(window.EffectiveScale(0)->realValue - originalScale.realValue) > 0.1);
    QVERIFY(window.m_Annotations.Undo());
    QVERIFY(window.EffectiveScale(0));
    QCOMPARE(window.EffectiveScale(0)->realValue, originalScale.realValue);

    // Cancellation while the worker waits on PDFium must discard its partial result.
    {
      std::unique_lock<std::recursive_mutex> hold(pdfview::PdfiumMutex());
      window.StartScaleAudit();
      QVERIFY(window.m_AuditWork.valid());
      window.findChild<QPushButton*>("pdfAuditCancel")->click();
    }
    QTRY_VERIFY_WITH_TIMEOUT(summary->text().contains("cancelled. No partial result"), 10000);
    QVERIFY(!window.m_AuditReady);
  }

  void pdfCompareRunsDiffAndSupportsManualAlignmentAndFailure() {
    QTemporaryDir directory;
    const QString basePath = Issue905WritePdf(directory, "base.pdf", pdfview::MakeLineWorkPdf(612, 792, 80, 1, 0));
    const QString revisionPath = Issue905WritePdf(directory, "revision.pdf", pdfview::MakeLineWorkPdf(612, 792, 80, 1, 1));
    PdfWindow window(nullptr);
    window.resize(1200, 900); window.show();
    QString error;
    QVERIFY2(window.LoadFile(basePath, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.m_Compare = std::make_unique<PdfWindow::CompareSession>();
    window.m_Compare->source = revisionPath;
    window.ShowComparePanel();
    window.StartCompare(true);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_Compare && !window.m_Compare->busy, 30000);
    QVERIFY(window.m_Compare->revision);
    QVERIFY(!window.m_Compare->overlay.isNull());
    QVERIFY(window.m_Compare->haveChanges);
    QVERIFY(!window.m_Compare->changes.empty());
    QCOMPARE(window.findChild<QTableWidget*>("pdfCompareChanges")->rowCount(), static_cast<int>(window.m_Compare->changes.size()));
    // Zoomed well past the comparison images' own resolution, the Revision view gets a sharp picture of what is on screen.
    window.SetZoomPercent(600);
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    window.m_CompareMode->setCurrentIndex(3);
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_CompareDetailImage.isNull(), 20000);
    QCOMPARE(window.m_CompareDetailPageWidthPx, window.PageWidthPx());
    QVERIFY(!window.m_CompareDetailMarks.isNull()); // the Tint view's red and blue marks are redrawn sharp too
    window.SetZoomPercent(100);
    window.m_CompareMode->setCurrentIndex(0);
    QVERIFY(window.grab().save(Issue908Shot("qt-908-compare-overlay.png")));
    QVERIFY(window.findChild<QWidget*>("pdfComparePanel")->grab().save(Issue908Shot("qt-908-compare-controls.png")));

    // One matching point and two matching points both use Qt's Base / Revision page picking sequence.
    window.findChild<QPushButton*>("pdfComparePickOne")->click();
    window.ComparePick(QPointF(100, 100), Qt::LeftButton);
    window.m_CompareMode->setCurrentIndex(3);
    window.ComparePick(QPointF(100, 100), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_Compare->busy, 30000);
    QVERIFY(window.m_Compare->manualAlignment);
    QVERIFY(std::abs(window.m_Compare->transform.tx - window.m_Compare->automatic.tx) < 0.01);
    window.findChild<QPushButton*>("pdfComparePickTwo")->click();
    window.ComparePick(QPointF(100, 100), Qt::LeftButton);
    window.m_CompareMode->setCurrentIndex(3);
    window.ComparePick(QPointF(100, 100), Qt::LeftButton);
    window.m_CompareMode->setCurrentIndex(2);
    window.ComparePick(QPointF(220, 130), Qt::LeftButton);
    window.m_CompareMode->setCurrentIndex(3);
    window.ComparePick(QPointF(220, 130), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_Compare->busy, 30000);
    QVERIFY(window.m_Compare->manualAlignment);

    window.m_Compare = std::make_unique<PdfWindow::CompareSession>();
    window.m_Compare->source = revisionPath;
    {
      std::unique_lock<std::recursive_mutex> hold(pdfview::PdfiumMutex());
      window.StartCompare(true);
      QVERIFY(window.m_Compare->busy);
      window.findChild<QPushButton*>("pdfCompareCancel")->click();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_Compare->busy, 10000);
    QVERIFY(window.m_CompareStatus->text().contains("cancelled. No partial result"));
    QVERIFY(window.m_Compare->changes.empty());

    window.m_Compare = std::make_unique<PdfWindow::CompareSession>();
    window.m_Compare->source = directory.filePath("missing.pdf");
    window.StartCompare(false);
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_Compare->busy, 10000);
    QVERIFY(window.m_CompareStatus->text().contains("Could not open the revision"));
    QVERIFY(window.m_Compare->overlay.isNull());
  }

  void issue1037CompareDetailKeepsRevisionWhenDetailMarksFail() {
    PdfWindow::CompareDetail detail;
    detail.bitmap.w = 2; detail.bitmap.h = 1; detail.bitmap.bgra = {1, 2, 3, 4, 5, 6, 7, 8};
    detail.marks.w = 2; detail.marks.h = 1; detail.marks.bgra = {9, 10, 11, 12};
    detail.redMarks = detail.marks;
    QVERIFY(PdfWindow::KeepCompareRevisionWhenMarksFail(detail, false));
    QCOMPARE(detail.bitmap.w, 2);
    QCOMPARE(detail.bitmap.bgra, (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
    QVERIFY(detail.marks.bgra.empty() && detail.redMarks.bgra.empty());
    QVERIFY(detail.failed);

    QVERIFY(!PdfWindow::KeepCompareRevisionWhenMarksFail(detail, true));
    QCOMPARE(detail.bitmap.w, 0); // cancellation discards stale revision pixels
    QVERIFY(!detail.failed);
  }

  void qtPdfCompareAndAuditDeepNavyScreenshotsForIssue908() {
    const auto saveComposite = [](PdfWindow& window, QWidget* dialog, const QString& path) {
      QTest::qWait(80);
      const QImage viewer = window.grab().toImage();
      const QImage panel = dialog->grab().toImage();
      const QSize size(std::max(viewer.width(), panel.width()), std::max(viewer.height(), panel.height()));
      QImage image(size, QImage::Format_ARGB32_Premultiplied);
      image.fill(QColor("#10151f"));
      QPainter painter(&image);
      painter.drawImage(0, 0, viewer);
      painter.drawImage((image.width() - panel.width()) / 2, (image.height() - panel.height()) / 2, panel);
      painter.end();
      return image.save(path);
    };

    QTemporaryDir directory;
    const QString auditPath = Issue905WritePdf(directory, "audit-options.pdf", Issue908AuditPdf());
    PdfWindow auditWindow(nullptr);
    auditWindow.resize(1200, 900); auditWindow.show();
    QString error;
    QVERIFY2(auditWindow.LoadFile(auditPath, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!auditWindow.IsBusy(), 10000);
    pdfview::PageScale scale;
    scale.pageValue = 1; scale.pageUnit = pdfview::Unit::Inch;
    scale.realValue = 20; scale.realUnit = pdfview::Unit::Foot;
    QVERIFY(auditWindow.m_Annotations.SetScales({{0, scale}}));
    auditWindow.findChild<QAction*>("pdfAuditScale")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(auditWindow.m_AuditReady, 10000);
    auditWindow.findChild<QCheckBox*>("pdfAuditAllRows")->setChecked(true);
    auto* auditDialog = auditWindow.findChild<QDialog*>("pdfScaleAuditDialog");
    QVERIFY(auditDialog);
    QVERIFY(saveComposite(auditWindow, auditDialog, Issue908Shot("qt-908-audit-deep-navy.png")));

    const QString basePath = Issue905WritePdf(directory, "compare-base.pdf", pdfview::MakeLineWorkPdf(612, 792, 80, 1, 0));
    const QString revisionPath = Issue905WritePdf(directory, "compare-revision.pdf", pdfview::MakeLineWorkPdf(612, 792, 80, 1, 1));
    PdfWindow compareWindow(nullptr);
    compareWindow.resize(1200, 900); compareWindow.show();
    QVERIFY2(compareWindow.LoadFile(basePath, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!compareWindow.IsBusy(), 10000);
    compareWindow.m_Compare = std::make_unique<PdfWindow::CompareSession>();
    compareWindow.m_Compare->source = revisionPath;
    compareWindow.ShowComparePanel();
    compareWindow.StartCompare(true);
    QTRY_VERIFY_WITH_TIMEOUT(compareWindow.m_Compare && !compareWindow.m_Compare->busy && compareWindow.m_Compare->haveChanges, 30000);
    auto* compareDialog = compareWindow.findChild<QWidget*>("pdfComparePanel");
    QVERIFY(compareDialog);
    QVERIFY(saveComposite(compareWindow, compareDialog, Issue908Shot("qt-908-compare-deep-navy.png")));
  }

  // ---- Issue #906 / REQ-430: PDF calibration, scale checks and snapping ----

  void pdfSetScaleAppliesPresetsTypedRatiosAndScopesAndUndoes() {
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, pdfview::MakeSyntheticPdf(5, 3, 0)));
    PdfWindow& window = *holder;
    auto* label = window.findChild<QLabel*>("pdfScaleLabel");
    auto* setScale = window.findChild<QAction*>("pdfSetScaleAction");
    auto* pageBox = window.findChild<QSpinBox*>("pdfPageNumber");
    QVERIFY(label && setScale && pageBox);
    QCOMPARE(label->text(), QString("Unscaled"));
    QVERIFY(!window.HasUnsavedChanges());

    // Cancel changes nothing.
    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) {
      QVERIFY(dialog->findChild<QLabel*>("pdfSetScaleCurrent")->text().contains("unscaled"));
      dialog->findChild<QPushButton*>("pdfScaleCancel")->click();
    });
    setScale->trigger();
    QVERIFY(!window.EffectiveScale(0));
    QVERIFY(!window.HasUnsavedChanges());

    // A preset on this page only.
    Issue906Preset(window, "1\" = 20'");
    QVERIFY(window.EffectiveScale(0));
    QCOMPARE(window.EffectiveScale(0)->realValue, 20.0);
    QVERIFY(!window.EffectiveScale(1));
    QCOMPARE(label->text(), QString("Scale: 1\" = 20'"));
    QVERIFY(window.HasUnsavedChanges());

    // A typed ratio on every page; a ratio that is not above zero is refused first and changes nothing.
    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) {
      dialog->findChild<QRadioButton*>("pdfScaleModeTyped")->setChecked(true);
      dialog->findChild<QDoubleSpinBox*>("pdfScaleTypedPage")->setValue(1);
      dialog->findChild<QDoubleSpinBox*>("pdfScaleTypedReal")->setValue(0);
      dialog->findChild<QRadioButton*>("pdfScopeAll")->setChecked(true);
      dialog->findChild<QPushButton*>("pdfScaleApply")->click();
      auto* error = dialog->findChild<QLabel*>("pdfScaleError");
      QVERIFY(error->isVisible() && error->text().contains("bigger than zero"));
      dialog->findChild<QDoubleSpinBox*>("pdfScaleTypedReal")->setValue(50);
      dialog->findChild<QPushButton*>("pdfScaleApply")->click();
    });
    setScale->trigger();
    for (int page = 0; page < 5; ++page) {
      QVERIFY(window.EffectiveScale(page));
      QCOMPARE(window.EffectiveScale(page)->realValue, 50.0);
    }

    // A page list: a page past the end is refused with the reason, then "2-3" applies to those two only.
    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) {
      dialog->findChild<QRadioButton*>("pdfScaleModePreset")->setChecked(true);
      Issue906Select(dialog->findChild<QComboBox*>("pdfScalePreset"), "1\" = 40'");
      dialog->findChild<QRadioButton*>("pdfScopePages")->setChecked(true);
      auto* text = dialog->findChild<QLineEdit*>("pdfScopeText");
      text->setText("9");
      dialog->findChild<QPushButton*>("pdfScaleApply")->click();
      QVERIFY(dialog->findChild<QLabel*>("pdfScaleError")->isVisible());
      text->setText("2-3");
      dialog->findChild<QPushButton*>("pdfScaleApply")->click();
    });
    setScale->trigger();
    QCOMPARE(window.EffectiveScale(0)->realValue, 50.0);
    QCOMPARE(window.EffectiveScale(1)->realValue, 40.0);
    QCOMPARE(window.EffectiveScale(2)->realValue, 40.0);
    QCOMPARE(window.EffectiveScale(3)->realValue, 50.0);
    pageBox->setValue(2);
    QCOMPARE(label->text(), QString("Scale: 1\" = 40'"));
    pageBox->setValue(1);

    // One undo step per apply.
    Issue906Action(window, "Undo")->trigger();
    QCOMPARE(window.EffectiveScale(1)->realValue, 50.0);
    QCOMPARE(window.EffectiveScale(2)->realValue, 50.0);

    // Clear scale removes it from the chosen page only; undo brings it back.
    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) {
      dialog->findChild<QRadioButton*>("pdfScopeThis")->setChecked(true);
      dialog->findChild<QPushButton*>("pdfScaleClear")->click();
    });
    setScale->trigger();
    QVERIFY(!window.EffectiveScale(0));
    QCOMPARE(label->text(), QString("Unscaled"));
    QVERIFY(window.EffectiveScale(1));
    Issue906Action(window, "Undo")->trigger();
    QVERIFY(window.EffectiveScale(0));
    QCOMPARE(window.EffectiveScale(0)->realValue, 50.0);

    // Calibration points picked on one page are dropped by changing page, and Esc leaves the tool.
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) { dialog->findChild<QPushButton*>("pdfScaleCalibrate")->click(); });
    setScale->trigger();
    QVERIFY(window.statusBar()->currentMessage().contains("known distance"));
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000); // the page was redrawn after the page changes above
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 60, 110, 612, 792));
    QVERIFY(window.statusBar()->currentMessage().contains("other point"));
    pageBox->setValue(2);
    window.statusBar()->clearMessage();
    QTRY_VERIFY_WITH_TIMEOUT(!window.IsBusy(), 10000);
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 60, 110, 612, 792)); // the first point again, not a second
    QVERIFY(window.statusBar()->currentMessage().contains("other point"));
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(Issue906Action(window, "Browse")->isChecked());
  }

  void pdfCalibrationByTwoClicksSnapsOffersStandardScaleAndCountsAsFirstCheck() {
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, Issue906Pdf()));
    PdfWindow& window = *holder;
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    auto* setScale = window.findChild<QAction*>("pdfSetScaleAction");
    auto* checksButton = window.findChild<QPushButton*>("pdfScaleChecksButton");
    QVERIFY(canvas && setScale && checksButton);
    auto startCalibrate = [&] {
      Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) { dialog->findChild<QPushButton*>("pdfScaleCalibrate")->click(); });
      setScale->trigger();
    };

    // Freehand clicks (Snap off): the box says so; a distance of zero is refused; Cancel changes nothing.
    startCalibrate();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 60, 110));
    Issue906OnDialog(window, "pdfKnownDistanceDialog", [](QDialog* dialog) {
      QVERIFY(dialog->findChild<QLabel*>("pdfCalSnapState")->text().contains("freehand"));
      QVERIFY(dialog->findChild<QLabel*>("pdfCalDistance")->text().contains("points"));
      dialog->findChild<QDoubleSpinBox*>("pdfCalReal")->setValue(0);
      dialog->findChild<QPushButton*>("pdfCalSetScale")->click();
      QVERIFY(dialog->findChild<QLabel*>("pdfScaleError")->isVisible());
      dialog->findChild<QPushButton*>("pdfCalCancel")->click();
    });
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 240, 95));
    QVERIFY(!window.EffectiveScale(0));
    QVERIFY(Issue906Action(window, "Browse")->isChecked());

    // Snap on: both points land exactly on the line's ends, the box says so, and 200 pt = 50 ft sets the scale.
    window.findChild<QAction*>("pdfSnapAction")->trigger();
    auto* snapStatus = window.findChild<QLabel*>("pdfSnapStatus");
    QTRY_VERIFY_WITH_TIMEOUT(window.SnapEnabled() && snapStatus->text().isEmpty(), 10000);
    startCalibrate();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 52, 103));
    Issue906OnDialog(window, "pdfKnownDistanceDialog", [](QDialog* dialog) {
      QVERIFY(dialog->findChild<QLabel*>("pdfCalSnapState")->text().contains("Both points snapped"));
      QVERIFY(dialog->findChild<QLabel*>("pdfCalDistance")->text().contains("200.000 points"));
      dialog->findChild<QDoubleSpinBox*>("pdfCalReal")->setValue(50);
      QVERIFY(!dialog->findChild<QLabel*>("pdfCalNearest")->isVisible()); // 18 ft per inch is no standard scale
      dialog->findChild<QPushButton*>("pdfCalSetScale")->click();
    });
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 247, 98));
    QVERIFY(window.EffectiveScale(0));
    QVERIFY(qAbs(window.EffectiveScale(0)->PointsToReal(200.0) - 50.0) < 50.0 * 1e-4); // within 0.01 %
    QCOMPARE(checksButton->text(), QString("Scale checks (1)")); // the calibration is the first check
    QVERIFY(window.HasUnsavedChanges());

    // A calibration within 1 % of a standard scale offers it, and the preset replaces the computed one.
    startCalibrate();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 52, 103));
    Issue906OnDialog(window, "pdfKnownDistanceDialog", [](QDialog* dialog) {
      dialog->findChild<QDoubleSpinBox*>("pdfCalReal")->setValue(55.5556);
      auto* nearest = dialog->findChild<QLabel*>("pdfCalNearest");
      QVERIFY(nearest->isVisible() && nearest->text().contains("1\" = 20'"));
      dialog->findChild<QPushButton*>("pdfCalUseStandard")->click();
    });
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 247, 98));
    QCOMPARE(window.EffectiveScale(0)->realValue, 20.0);
    QCOMPARE(window.EffectiveScale(0)->label, std::string("1\" = 20'"));
    QCOMPARE(checksButton->text(), QString("Scale checks (2)"));

    // "Set scale and check..." sets the scale and goes straight to the Check tool.
    startCalibrate();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 52, 103));
    Issue906OnDialog(window, "pdfKnownDistanceDialog", [](QDialog* dialog) {
      dialog->findChild<QDoubleSpinBox*>("pdfCalReal")->setValue(50);
      dialog->findChild<QPushButton*>("pdfCalSetAndCheck")->click();
    });
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 247, 98));
    QVERIFY(window.findChild<QAction*>("pdfCheckTool")->isChecked());
    QVERIFY(window.statusBar()->currentMessage().contains("check"));
    QCOMPARE(checksButton->text(), QString("Scale checks (3)"));

    // Esc after one point leaves the tool and nothing is asked.
    startCalibrate();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 52, 103));
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(Issue906Action(window, "Browse")->isChecked());
    QCOMPARE(checksButton->text(), QString("Scale checks (3)"));
  }

  void pdfCheckToolGivesVerdictsListsCorrectsAndStaysOutOfTheSavedFile() {
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, Issue906Pdf()));
    PdfWindow& window = *holder;
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    auto* checkTool = window.findChild<QAction*>("pdfCheckTool");
    auto* checksButton = window.findChild<QPushButton*>("pdfScaleChecksButton");
    QVERIFY(canvas && checkTool && checksButton);
    QVERIFY(!checksButton->isEnabled()); // nothing to check on an unscaled page

    // An unscaled page refuses the Check tool.
    checkTool->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 50, 150));
    QVERIFY(window.statusBar()->currentMessage().contains("no scale"));

    Issue906Preset(window, "1\" = 20'");
    QVERIFY(checksButton->isEnabled());
    window.findChild<QAction*>("pdfSnapAction")->trigger();
    auto* snapStatus = window.findChild<QLabel*>("pdfSnapStatus");
    QTRY_VERIFY_WITH_TIMEOUT(window.SnapEnabled() && snapStatus->text().isEmpty(), 10000);

    // A 144 pt span is 40 ft here. An unreadable value is refused; the verdict shows before OK.
    auto pick = [&](double y, const char* tryText, const char* tryVerdict, const char* finalText) {
      Issue906OnDialog(window, "pdfCheckDialog", [=](QDialog* dialog) {
        auto* value = dialog->findChild<QLineEdit*>("pdfCheckValue");
        auto* ok = dialog->findChild<QPushButton*>("pdfCheckOk");
        auto* result = dialog->findChild<QLabel*>("pdfCheckResult");
        value->setText("abc");
        QVERIFY(!ok->isEnabled() && !result->text().isEmpty());
        value->setText(tryText);
        QVERIFY2(result->text().contains(tryVerdict), qPrintable(result->text()));
        value->setText(finalText);
        QVERIFY(ok->isEnabled());
        ok->click();
      });
      checkTool->trigger();
      QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 50, y));
      QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 194, y));
    };
    pick(150, "40 ft", "Good", "40 ft");
    QCOMPARE(checksButton->text(), QString("Scale checks (1)"));
    pick(200, "40.1 ft", "Check", "40 ft"); // 0.25 % out: the Check band
    pick(250, "40.4 ft", "Blunder", "40.4 ft"); // 1 % out
    QCOMPARE(checksButton->text(), QString("Scale checks (3)"));

    // The checks are drawn on the sheet in their verdict colour; "Show on sheet" hides them.
    auto* showOnSheet = window.findChild<QCheckBox*>("pdfShowChecks");
    QVERIFY(showOnSheet && showOnSheet->isChecked());
    QTRY_VERIFY(Issue906HasPixelNear(canvas->grab().toImage(), QColor("#d23a32"), 6)); // the Blunder
    showOnSheet->setChecked(false);
    QTRY_VERIFY(!Issue906HasPixelNear(canvas->grab().toImage(), QColor("#d23a32"), 6));
    showOnSheet->setChecked(true);

    // Checks are working marks: saving writes the scale but no check, and adding one leaves the file saved.
    QString error;
    const QString saved = directory.filePath("saved.pdf");
    QVERIFY2(window.SaveFile(saved, error), qPrintable(error));
    QVERIFY(!window.HasUnsavedChanges());
    pick(150, "40 ft", "Good", "40 ft");
    QVERIFY(!window.HasUnsavedChanges());
    QCOMPARE(checksButton->text(), QString("Scale checks (4)"));
    QVERIFY(pdfview::ReadAnnotations(std::filesystem::path(saved.toStdWString())).empty());

    // Cancel on the popup adds nothing.
    Issue906OnDialog(window, "pdfCheckDialog", [](QDialog* dialog) { dialog->findChild<QPushButton*>("pdfCheckCancel")->click(); });
    checkTool->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 50, 150));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 194, 150));
    QCOMPARE(checksButton->text(), QString("Scale checks (4)"));

    // The Scale checks dialog: the wrong check is the Outlier; corrections preview and apply as one undo step each.
    Issue906OnDialog(window, "pdfChecksDialog", [&](QDialog* dialog) {
      auto* table = dialog->findChild<QTableWidget*>("pdfChecksTable");
      QCOMPARE(table->rowCount(), 4);
      QCOMPARE(table->item(2, 6)->text(), QString("Blunder"));
      QCOMPARE(table->item(2, 7)->text(), QString("Outlier"));
      QCOMPARE(table->item(0, 7)->text(), QString());
      QVERIFY(dialog->findChild<QLabel*>("pdfChecksBestFit")->isVisible());
      QVERIFY(dialog->findChild<QLabel*>("pdfChecksOutlierNote")->isVisible());
      QVERIFY(dialog->findChild<QRadioButton*>("pdfCorrBestFitWithout")->isEnabled());

      // Match the third check (reads 40, drawing says 40.4): every reading grows 1 %, and that check reads Good.
      dialog->findChild<QComboBox*>("pdfCorrMatchIndex")->setCurrentIndex(2);
      QVERIFY(dialog->findChild<QLabel*>("pdfCorrAfter")->text().contains("adjusted"));
      auto* preview = dialog->findChild<QTableWidget*>("pdfCorrTable");
      QCOMPARE(preview->rowCount(), 4);
      QCOMPARE(preview->item(2, 4)->text(), QString("Good"));
      QCOMPARE(preview->item(0, 4)->text(), QString("Blunder")); // the trade-off the user must see
      dialog->findChild<QPushButton*>("pdfCorrApply")->click();
      QTRY_COMPARE(table->item(2, 6)->text(), QString("Good"));

      // A typed +0.20 %.
      dialog->findChild<QDoubleSpinBox*>("pdfCorrPercentValue")->setValue(0.2);
      dialog->findChild<QPushButton*>("pdfCorrApply")->click();

      // The verdict limits are settings: widening them changes the verdicts, and they are put back.
      dialog->findChild<QDoubleSpinBox*>("pdfLimitGood")->setValue(5);
      dialog->findChild<QDoubleSpinBox*>("pdfLimitCheck")->setValue(10);
      QTRY_COMPARE(table->item(0, 6)->text(), QString("Good"));
      dialog->findChild<QDoubleSpinBox*>("pdfLimitGood")->setValue(0.1);
      dialog->findChild<QDoubleSpinBox*>("pdfLimitCheck")->setValue(0.5);
      QTRY_COMPARE(table->item(0, 6)->text(), QString("Blunder"));

      // The report is copied and names the checks.
      const bool clipboardWorks = ClipboardRoundTrips();
      dialog->findChild<QPushButton*>("pdfChecksCopyReport")->click();
      if (clipboardWorks) QVERIFY(QGuiApplication::clipboard()->text().contains("40"));

      // Delete a check.
      dialog->findChild<QPushButton*>("pdfCheckDelete0")->click();
      QTRY_COMPARE(table->rowCount(), 3);
      dialog->findChild<QPushButton*>("pdfChecksClose")->click();
    });
    checksButton->click();
    QCOMPARE(checksButton->text(), QString("Scale checks (3)"));
    QVERIFY(window.EffectiveScale(0)->note.find("adjusted") != std::string::npos);
    QVERIFY(qAbs(window.EffectiveScale(0)->realValue - 20.0 * 1.01 * 1.002) < 1e-4);

    // Each correction was one undo step (deleting a check is one too); undo restores the scale and its text exactly.
    Issue906Action(window, "Undo")->trigger(); // the deleted check comes back
    QCOMPARE(checksButton->text(), QString("Scale checks (4)"));
    Issue906Action(window, "Undo")->trigger(); // the typed percentage
    QVERIFY(qAbs(window.EffectiveScale(0)->realValue - 20.2) < 1e-4);
    Issue906Action(window, "Undo")->trigger(); // matching the third check
    QCOMPARE(window.EffectiveScale(0)->realValue, 20.0);
    QVERIFY(window.EffectiveScale(0)->note.empty());
  }

  void pdfRobustCalibrationNeedsTheMinimumFlagsTheWrongDimensionAndAppliesOnce() {
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, Issue906Pdf()));
    PdfWindow& window = *holder;
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    auto* checksButton = window.findChild<QPushButton*>("pdfScaleChecksButton");
    QVERIFY(canvas && checksButton);
    Issue906Preset(window, "1\" = 20'");
    window.findChild<QAction*>("pdfSnapAction")->trigger();
    auto* snapStatus = window.findChild<QLabel*>("pdfSnapStatus");
    QTRY_VERIFY_WITH_TIMEOUT(window.SnapEnabled() && snapStatus->text().isEmpty(), 10000);

    // Open the robust tab (nothing to apply yet) and ask to add a dimension.
    Issue906OnDialog(window, "pdfChecksDialog", [](QDialog* dialog) {
      dialog->findChild<QTabWidget*>("pdfChecksTabs")->setCurrentIndex(1);
      QVERIFY(!dialog->findChild<QPushButton*>("pdfRobustApply")->isEnabled());
      QVERIFY(!dialog->findChild<QLabel*>("pdfRobustResult")->text().isEmpty());
      dialog->findChild<QPushButton*>("pdfRobustAdd")->click();
    });
    checksButton->click();

    struct Dimension { double x0, x1, y; const char* text; };
    const Dimension dimensions[] = {{50, 194, 150, "40 ft"}, {50, 194, 200, "40 ft"}, {50, 194, 250, "40 ft"},
                                    {50, 250, 100, "55.5556 ft"}, {50, 194, 150, "400 ft"}}; // the last is a mistyped 40
    for (int i = 0; i < 5; ++i) {
      const Dimension d = dimensions[i];
      Issue906OnDialog(window, "pdfCheckDialog", [d](QDialog* dialog) {
        dialog->findChild<QLineEdit*>("pdfCheckValue")->setText(d.text);
        dialog->findChild<QPushButton*>("pdfCheckOk")->click();
      });
      if (i < 4) {
        Issue906OnDialog(window, "pdfChecksDialog", [i](QDialog* dialog) {
          QCOMPARE(dialog->findChild<QTabWidget*>("pdfChecksTabs")->currentIndex(), 1);
          QCOMPARE(dialog->findChild<QTableWidget*>("pdfRobustTable")->rowCount(), i + 1);
          QCOMPARE(dialog->findChild<QPushButton*>("pdfRobustApply")->isEnabled(), i + 1 >= 3); // the minimum is three
          dialog->findChild<QPushButton*>("pdfRobustAdd")->click();
        });
      } else {
        Issue906OnDialog(window, "pdfChecksDialog", [](QDialog* dialog) {
          auto* table = dialog->findChild<QTableWidget*>("pdfRobustTable");
          QCOMPARE(table->rowCount(), 5);
          int suspects = 0, suspectRow = -1;
          for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 6)->text().contains("Suspect")) { ++suspects; suspectRow = row; }
          QCOMPARE(suspects, 1);
          QCOMPARE(suspectRow, 4);
          // Removing it from the fit re-solves; the program never removed it by itself.
          table->item(4, 0)->setCheckState(Qt::Unchecked);
          QTRY_VERIFY(!table->item(4, 6)->text().contains("Suspect") && !table->item(0, 6)->text().contains("Suspect"));
          auto* result = dialog->findChild<QLabel*>("pdfRobustResult");
          QVERIFY2(result->text().contains("Adjusted scale"), qPrintable(result->text()));
          QVERIFY(dialog->findChild<QPushButton*>("pdfRobustApply")->isEnabled());
          dialog->findChild<QPushButton*>("pdfRobustApply")->click();
        });
      }
      for (double x : {d.x0, d.x1}) QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, x, d.y));
    }
    QVERIFY(window.EffectiveScale(0)->note.find("robust") != std::string::npos);
    QVERIFY(qAbs(window.EffectiveScale(0)->realValue / 20.0 - 1.0) < 1e-3);
    QCOMPARE(checksButton->text(), QString("Scale checks (0)")); // robust dimensions are not scale checks

    // Apply was one undo step.
    Issue906Action(window, "Undo")->trigger();
    QCOMPARE(window.EffectiveScale(0)->label, std::string("1\" = 20'"));
    QVERIFY(window.EffectiveScale(0)->note.empty());
  }

  void pdfSnapIsOffByDefaultF3TogglesItAndLandsOnLineEnds() {
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, Issue906Pdf()));
    PdfWindow& window = *holder;
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    auto* snapAction = window.findChild<QAction*>("pdfSnapAction");
    auto* snapStatus = window.findChild<QLabel*>("pdfSnapStatus");
    QVERIFY(canvas && snapAction && snapStatus);
    QVERIFY(!window.SnapEnabled());
    QCOMPARE(snapAction->text(), QString("Snap: off"));
    QTest::keyClick(&window, Qt::Key_F3);
    QVERIFY(window.SnapEnabled());
    QCOMPARE(snapAction->text(), QString("Snap: on"));
    QTRY_VERIFY_WITH_TIMEOUT(snapStatus->text().isEmpty(), 10000);

    // The marker shows on the end the next click will take.
    Issue906Action(window, "Line")->trigger();
    Issue906Move(canvas, Issue906Px(canvas, 53, 103));
    QTRY_VERIFY_WITH_TIMEOUT(Issue906HasSnapMarker(canvas->grab().toImage()), 10000);
    // A snapped line, then (Snap off) a free one at the same distance from the ends.
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 53, 103));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 247, 97));
    QTest::keyClick(&window, Qt::Key_F3);
    QVERIFY(!window.SnapEnabled());
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 53, 53));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 247, 47));
    QString error;
    const QString saved = directory.filePath("lines.pdf");
    QVERIFY2(window.SaveFile(saved, error), qPrintable(error));
    const auto lines = pdfview::ReadAnnotations(std::filesystem::path(saved.toStdWString()));
    QCOMPARE(lines.size(), std::size_t(2));
    QVERIFY(qAbs(lines[0].x0 - 50.f) < 0.01f && qAbs(lines[0].y0 - 100.f) < 0.01f);
    QVERIFY(qAbs(lines[0].x1 - 250.f) < 0.01f && qAbs(lines[0].y1 - 100.f) < 0.01f);
    QVERIFY(qAbs(lines[1].x0 - 53.f) < 1.f && qAbs(lines[1].x0 - 50.f) > 1.f); // placed freehand, within a pixel of where it was clicked

    // A page with no line work says it has nothing to snap to, and a click is not moved.
    std::unique_ptr<PdfWindow> scanned;
    QVERIFY(Issue906Open(scanned, directory, Issue906Pdf("/MediaBox[0 0 400 300]", ""), "scan.pdf"));
    QTest::keyClick(scanned.get(), Qt::Key_F3);
    QTRY_COMPARE_WITH_TIMEOUT(scanned->findChild<QLabel*>("pdfSnapStatus")->text(), QString("this page has nothing to snap to"), 10000);
  }

  void pdfScaleSetInQtIsSavedWithSaveAsAndReadBackAndUnusableScalesAreStated() {
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, pdfview::MakeSyntheticPdf(5, 3, 0)));
    PdfWindow& window = *holder;
    const QString source = directory.filePath("sheet.pdf");
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    original.close();
    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) {
      Issue906Select(dialog->findChild<QComboBox*>("pdfScalePreset"), "1\" = 20'");
      dialog->findChild<QRadioButton*>("pdfScopeAll")->setChecked(true);
      dialog->findChild<QPushButton*>("pdfScaleApply")->click();
    });
    window.findChild<QAction*>("pdfSetScaleAction")->trigger();
    QString error;
    const QString saved = directory.filePath("scaled.pdf");
    QVERIFY2(window.SaveFile(saved, error), qPrintable(error));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes); // the source is never changed
    original.close();
    const auto read = pdfview::ReadPageScales(std::filesystem::path(saved.toStdWString()));
    QCOMPARE(read.scales.size(), std::size_t(5));
    for (const auto& entry : read.scales) {
      QCOMPARE(entry.second.realUnit, pdfview::Unit::Foot);
      QVERIFY(qAbs(entry.second.RealPerPoint() * 72.0 - 20.0) < 1e-6);
    }

    // The saved file shows its scale in a new window.
    PdfWindow reopened(nullptr);
    QVERIFY2(reopened.LoadFile(saved, error), qPrintable(error));
    const QString shown = reopened.findChild<QLabel*>("pdfScaleLabel")->text();
    QVERIFY2(shown.startsWith("Scale:") && shown.contains("20"), qPrintable(shown));

    // Measurement data that cannot be understood shows the page as unscaled with the reason.
    std::unique_ptr<PdfWindow> broken;
    QVERIFY(Issue906Open(broken, directory,
        Issue906Pdf("/MediaBox[0 0 400 300]/VP[<</Type/Viewport/BBox[0 0 400 300]/Measure<</Type/Measure/Subtype/RL/R(broken)>>>>]"),
        "broken.pdf"));
    const QString reason = broken->findChild<QLabel*>("pdfScaleLabel")->text();
    QVERIFY2(reason.startsWith("Unscaled (") && reason.contains("scale"), qPrintable(reason));
  }

  // Issue #906: final screenshots of the scale surfaces in the application graphite palette.
  void qtPdfScaleScreenshotEvidenceForIssue906() {
    const auto shot = [](const char* surface) { return Issue905Shot((QString("qt-906-final-") + surface + ".png").toLatin1().constData()); };
    QTemporaryDir directory;
    std::unique_ptr<PdfWindow> holder;
    QVERIFY(Issue906Open(holder, directory, Issue906Pdf()));
    PdfWindow& window = *holder;
    window.resize(1200, 820);
    auto* canvas = window.findChild<QWidget*>("pdfCanvas");
    auto* checkTool = window.findChild<QAction*>("pdfCheckTool");
    auto* setScale = window.findChild<QAction*>("pdfSetScaleAction");
    auto* checksButton = window.findChild<QPushButton*>("pdfScaleChecksButton");

    Issue906OnDialog(window, "pdfSetScaleDialog", [&](QDialog* dialog) {
      Issue906Select(dialog->findChild<QComboBox*>("pdfScalePreset"), "1\" = 20'");
      QVERIFY(dialog->grab().save(shot("set-scale")));
      dialog->findChild<QPushButton*>("pdfScaleCancel")->click();
    });
    setScale->trigger();
    Issue906Preset(window, "1\" = 20'");
    window.findChild<QAction*>("pdfSnapAction")->trigger();
    auto* snapStatus = window.findChild<QLabel*>("pdfSnapStatus");
    QTRY_VERIFY_WITH_TIMEOUT(window.SnapEnabled() && snapStatus->text().isEmpty(), 10000);

    Issue906OnDialog(window, "pdfSetScaleDialog", [](QDialog* dialog) { dialog->findChild<QPushButton*>("pdfScaleCalibrate")->click(); });
    setScale->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 52, 103));
    Issue906OnDialog(window, "pdfKnownDistanceDialog", [&](QDialog* dialog) {
      dialog->findChild<QDoubleSpinBox*>("pdfCalReal")->setValue(55.5556);
      QVERIFY(dialog->grab().save(shot("known-distance")));
      dialog->findChild<QPushButton*>("pdfCalCancel")->click();
    });
    QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 247, 98));

    auto pick = [&](double y, const char* text) {
      Issue906OnDialog(window, "pdfCheckDialog", [=](QDialog* dialog) {
        dialog->findChild<QLineEdit*>("pdfCheckValue")->setText(text);
        if (std::string(text) == "40.4 ft") QVERIFY(dialog->grab().save(shot("check-popup")));
        dialog->findChild<QPushButton*>("pdfCheckOk")->click();
      });
      checkTool->trigger();
      QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 50, y));
      QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, 194, y));
    };
    pick(150, "40 ft");
    pick(200, "40 ft");
    pick(250, "40.4 ft");
    Issue906Action(window, "Line")->trigger();
    Issue906Move(canvas, Issue906Px(canvas, 253, 97));
    QTest::qWait(150);
    QVERIFY(window.grab().save(shot("window")));

    Issue906OnDialog(window, "pdfChecksDialog", [&](QDialog* dialog) {
      dialog->findChild<QTableWidget*>("pdfChecksTable")->selectRow(2);
      dialog->findChild<QComboBox*>("pdfCorrMatchIndex")->setCurrentIndex(2);
      QTest::qWait(100);
      QVERIFY(dialog->grab().save(shot("checks")));
      dialog->findChild<QPushButton*>("pdfChecksClose")->click();
    });
    checksButton->click();

    Issue906OnDialog(window, "pdfChecksDialog", [](QDialog* dialog) {
      dialog->findChild<QTabWidget*>("pdfChecksTabs")->setCurrentIndex(1);
      dialog->findChild<QPushButton*>("pdfRobustAdd")->click();
    });
    checksButton->click();
    struct Dimension { double x0, x1, y; const char* text; };
    const Dimension dimensions[] = {{50, 194, 150, "40 ft"}, {50, 194, 200, "40 ft"}, {50, 194, 250, "40 ft"},
                                    {50, 250, 100, "55.5556 ft"}, {50, 194, 150, "400 ft"}};
    for (int i = 0; i < 5; ++i) {
      const Dimension d = dimensions[i];
      Issue906OnDialog(window, "pdfCheckDialog", [d](QDialog* dialog) {
        dialog->findChild<QLineEdit*>("pdfCheckValue")->setText(d.text);
        dialog->findChild<QPushButton*>("pdfCheckOk")->click();
      });
      Issue906OnDialog(window, "pdfChecksDialog", [&, i](QDialog* dialog) {
        if (i < 4) { dialog->findChild<QPushButton*>("pdfRobustAdd")->click(); return; }
        QTest::qWait(100);
        QVERIFY(dialog->grab().save(shot("robust")));
        dialog->findChild<QPushButton*>("pdfChecksClose")->click();
      });
      for (double x : {d.x0, d.x1}) QTest::mouseClick(canvas, Qt::LeftButton, {}, Issue906Px(canvas, x, d.y));
    }
  }

  void dimensionStrokesRenderAndHonorLayerVisibility() {
    auto state = StateWithTraverse();
    CadAnnotation dimension;
    dimension.kind = CadAnnotation::Kind::DimLinear;
    dimension.dimExt2X = 10; dimension.dimSignedOffset = 10;
    dimension.text = "10.00";
    state->cadAnnotations.push_back(dimension);
    state->cadAnnotationAttrs.emplace_back();
    auto render = [&] {
      QImage image(400, 400, QImage::Format_ARGB32);
      image.fill(Qt::black);
      QPainter painter(&image);
      DrawQtModelAnnotations(painter, *state, 400, 400);
      painter.end();
      return image;
    };
    const auto visible = render();
    int marked = 0;
    for (int x = 198; x <= 202; ++x) for (int y = 162; y < 195; ++y)
      if (visible.pixelColor(x, y) != QColor(Qt::black)) ++marked;
    QVERIFY(marked > 5);
    state->drawingLayerTable[0].on = false;
    const auto hidden = render();
    for (int x = 0; x < hidden.width(); ++x) for (int y = 0; y < hidden.height(); ++y)
      QCOMPARE(hidden.pixelColor(x, y), QColor(Qt::black));
  }

  void projectAttachQuestionCopiesLinksAndCancelsWithoutTouchingTheOriginal() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    gsproj::Project created; std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Attach Job", &created, &error), error.c_str());
    auto writePdf = [&](const char* name) {
      QDir().mkpath(directory.filePath("outside"));
      QFile file(directory.filePath(QString("outside/") + name));
      if (!file.open(QIODevice::WriteOnly)) return QString();
      file.write("pdf-bytes"); file.close();
      return file.fileName();
    };
    EditorWindow window;
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    auto& state = window.State();
    auto& session = *std::find_if(state.openProjects.begin(), state.openProjects.end(), [uid](const auto& item) { return item.uid == uid; });
    const QString projectFolder = QString::fromStdWString(session.project.Folder().wstring());
    MessageCatcher messages;
    QVERIFY(window.findChild<QAction*>("command_ADDPDFTOPROJECT"));  // File > Add PDF to project...
    auto raise = [&](const QString& path) {
      std::vector<std::string> log;
      return AddPdfToProject(state, path.toUtf8().toStdString(), log);
    };
    const auto none = AppCommandState::ProjectAttachPrompt::Kind::None;

    // The question names the file, the project and both consequences.
    const QString cancelled = writePdf("cancelled.pdf");
    QVERIFY(raise(cancelled));
    QCOMPARE(state.projectAttachPrompt.destRel, std::string("PDFs/cancelled.pdf"));
    bool inspected = false;
    AnswerAttachQuestion(window, "projectAttachCancel", [&] {
      auto* dialog = window.findChild<QDialog*>("projectAttachDialog");
      if (!dialog) return;
      inspected = dialog->findChild<QLabel*>("projectAttachHeadline")->text().contains("cancelled.pdf") &&
                  dialog->findChild<QLabel*>("projectAttachHeadline")->text().contains("Attach Job") &&
                  dialog->findChild<QLabel*>("projectAttachCopyText")->text().contains("PDFs/cancelled.pdf") &&
                  dialog->findChild<QLabel*>("projectAttachLinkText")->text().contains("not") &&
                  !dialog->findChild<QLabel*>("projectAttachLarge") &&
                  dialog->findChild<QPushButton*>("projectAttachCopy")->isDefault();
    });
    QTRY_VERIFY(state.projectAttachPrompt.kind == none);
    QVERIFY(inspected);
    QVERIFY(!QFileInfo::exists(projectFolder + "/PDFs/cancelled.pdf"));
    QVERIFY(QFileInfo::exists(cancelled));
    QVERIFY(session.project.items.empty());

    // Closing the question (Escape) is Cancel too.
    const QString escaped = writePdf("escaped.pdf");
    QVERIFY(raise(escaped));
    AnswerAttachQuestion(window, "");
    QTRY_VERIFY(state.projectAttachPrompt.kind == none);
    QVERIFY(!QFileInfo::exists(projectFolder + "/PDFs/escaped.pdf"));
    QVERIFY(session.project.items.empty());

    // Copy: the file lands in the project and is tracked as part of it; the original stays.
    const QString copied = writePdf("copied.pdf");
    QVERIFY(raise(copied));
    AnswerAttachQuestion(window, "projectAttachCopy");
    QTRY_COMPARE(session.project.items.size(), std::size_t(1));
    QVERIFY(QFileInfo::exists(projectFolder + "/PDFs/copied.pdf"));
    QVERIFY(QFileInfo::exists(copied));
    QCOMPARE(session.project.items[0].kind, std::string(gsproj::kKindInProject));
    gsproj::Project reloaded;
    QVERIFY(gsproj::Load(session.project.file, &reloaded, nullptr));
    QCOMPARE(reloaded.items.size(), std::size_t(1));  // the choice was saved to the .gsproj

    // Link: tracked where it is, flagged as not travelling, and not copied.
    const QString linked = writePdf("linked.pdf");
    QVERIFY(raise(linked));
    AnswerAttachQuestion(window, "projectAttachLink");
    QTRY_COMPARE(session.project.items.size(), std::size_t(2));
    QCOMPARE(session.project.items[1].kind, std::string(gsproj::kKindLocalLink));
    QVERIFY(!QFileInfo::exists(projectFolder + "/PDFs/linked.pdf"));
    QVERIFY(QFileInfo::exists(linked));

    // An identical copy that is already in the project is reused and the question says so.
    QVERIFY(raise(copied));
    bool reuseShown = false;
    AnswerAttachQuestion(window, "projectAttachCancel", [&] {
      auto* dialog = window.findChild<QDialog*>("projectAttachDialog");
      reuseShown = dialog && dialog->findChild<QLabel*>("projectAttachReuse");
    });
    QTRY_VERIFY(state.projectAttachPrompt.kind == none);
    QVERIFY(reuseShown);
    QVERIFY(messages.texts.isEmpty());
  }

  void projectAttachQuestionWarnsOfLargeFilesAndReportsFailures() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    gsproj::Project created; std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Failure Job", &created, &error), error.c_str());
    QDir().mkpath(directory.filePath("outside"));
    auto writePdf = [&](const char* name) {
      QFile file(directory.filePath(QString("outside/") + name));
      if (!file.open(QIODevice::WriteOnly)) return QString();
      file.write("pdf-bytes"); file.close();
      return file.fileName();
    };
    EditorWindow window;
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    auto& state = window.State();
    auto& session = *std::find_if(state.openProjects.begin(), state.openProjects.end(), [uid](const auto& item) { return item.uid == uid; });
    const QString projectFolder = QString::fromStdWString(session.project.Folder().wstring());
    MessageCatcher messages;
    std::vector<std::string> log;
    const auto none = AppCommandState::ProjectAttachPrompt::Kind::None;

    // 100 MB or more gets the size warning (REQ-379 clause 1).
    const QString big = writePdf("big.pdf");
    QVERIFY(AddPdfToProject(state, big.toUtf8().toStdString(), log));
    state.projectAttachPrompt.sizeBytes = 150ull * 1024 * 1024;
    bool warned = false;
    AnswerAttachQuestion(window, "projectAttachCancel", [&] {
      auto* dialog = window.findChild<QDialog*>("projectAttachDialog");
      if (!dialog) return;
      auto* warning = dialog->findChild<QLabel*>("projectAttachLarge");
      warned = warning && warning->text().contains("large file") &&
               dialog->findChild<QLabel*>("projectAttachSize")->text().contains("150.0 MB");
    });
    QTRY_VERIFY(state.projectAttachPrompt.kind == none);
    QVERIFY(warned);

    // A copy that cannot be written says why and attaches nothing.
    QDir(projectFolder + "/PDFs").removeRecursively();
    QFile blocker(projectFolder + "/PDFs");
    QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();  // a file where the folder must go
    const QString blocked = writePdf("blocked.pdf");
    QVERIFY(AddPdfToProject(state, blocked.toUtf8().toStdString(), log));
    AnswerAttachQuestion(window, "projectAttachCopy");
    QTRY_VERIFY(!messages.texts.isEmpty());
    QVERIFY2(messages.texts.first().contains("Nothing was attached"), qPrintable(messages.texts.first()));
    QVERIFY(session.project.items.empty());
    QVERIFY(state.projectAttachPrompt.kind == none);
    QFile::remove(projectFolder + "/PDFs");
    messages.texts.clear();

    // A project that became read-only while the question was open attaches nothing.
    const QString late = writePdf("late.pdf");
    QVERIFY(AddPdfToProject(state, late.toUtf8().toStdString(), log));
    AnswerAttachQuestion(window, "projectAttachCopy", [&] { session.readOnly = true; });
    QTRY_VERIFY(!messages.texts.isEmpty());
    QVERIFY2(messages.texts.first().contains("can no longer be written"), qPrintable(messages.texts.first()));
    QVERIFY(session.project.items.empty());
    QVERIFY(!QFileInfo::exists(projectFolder + "/PDFs/late.pdf"));

    // A read-only project never raises the question in the first place.
    log.clear();
    QVERIFY(!AddPdfToProject(state, late.toUtf8().toStdString(), log));
    QVERIFY(state.projectAttachPrompt.kind == none);
    QVERIFY(QString::fromStdString(log.back()).contains("read-only"));
  }

  void staleProjectLockOffersReadOnlyTakeOverAndCancel() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString thisMachine = qEnvironmentVariable("COMPUTERNAME");
    auto makeProject = [&](const char* name) {
      gsproj::Project created; std::string error;
      const bool made = gsproj::Create(directory.path().toStdWString(), name, &created, &error);
      if (!made) qWarning("project not created: %s", error.c_str());
      return created;
    };
    auto lockHeldBy = [](const gsproj::Project& project, const char* user, const std::string& machine, std::uint32_t pid) {
      gsproj::LockInfo holder;
      holder.user = user; holder.machine = machine; holder.pid = pid; holder.sinceUnix = 1760000000;
      return gsproj::TryAcquire(project.file, holder, nullptr) == gsproj::LockResult::Acquired;
    };
    auto readLock = [](const gsproj::Project& project) {
      gsproj::LockInfo held, none;
      gsproj::TryAcquire(project.file, none, &held);
      return held;
    };
    auto pathOf = [](const gsproj::Project& project) { return QString::fromStdWString(project.file.wstring()); };
    constexpr std::uint32_t kDeadPid = 4294967280u;  // no process has this id
    const auto myPid = static_cast<std::uint32_t>(QApplication::applicationPid());
    MessageCatcher messages;
    bool inspected = false;

    // A lock held by a live editor on another machine: no stale note, a warning on Take over; Cancel
    // and Esc open nothing and leave the other person's lock alone.
    const auto live = makeProject("Live Job");
    QVERIFY(lockHeldBy(live, "alice", "OTHERPC", 4242));
    {
      EditorWindow window;
      AnswerLockQuestion(window, "projectLockCancel", [&](QDialog& dialog) {
        const QString headline = dialog.findChild<QLabel*>("projectLockHeadline")->text();
        inspected = headline.contains("alice") && headline.contains("OTHERPC") && headline.contains("Live Job") &&
                    dialog.findChild<QLabel*>("projectLockSince")->text().contains("2025-") &&
                    dialog.findChild<QLabel*>("projectLockStale")->isHidden() &&
                    dialog.findChild<QLabel*>("projectLockTakeOverText")->text().contains("unsaved work") &&
                    dialog.findChild<QPushButton*>("projectLockTakeOver")->text() == "Take over anyway" &&
                    dialog.findChild<QPushButton*>("projectLockReadOnly")->isDefault();
      });
      QCOMPARE(window.OpenProject(pathOf(live), true), std::uint32_t(0));
      QVERIFY(inspected);
      QVERIFY(window.State().openProjects.empty());
      AnswerLockQuestion(window, "");
      QCOMPARE(window.OpenProject(pathOf(live), true), std::uint32_t(0));
      QVERIFY(window.State().openProjects.empty());
      QCOMPARE(readLock(live).user, std::string("alice"));

      // Read-only opens the project for viewing and never touches the lock.
      AnswerLockQuestion(window, "projectLockReadOnly");
      const auto uid = window.OpenProject(pathOf(live), true);
      QVERIFY(uid != 0);
      QVERIFY(window.State().openProjects.front().readOnly);
      QCOMPARE(readLock(live).user, std::string("alice"));

      // Asking again for the project this window already has shares it: no question.
      QCOMPARE(window.OpenProject(pathOf(live), false), uid);
    }
    QCOMPARE(readLock(live).user, std::string("alice"));  // closing a read-only view leaves it

    // A leftover lock from a program on this machine that no longer runs is called out, and Take over
    // makes this window the editor.
    const auto stale = makeProject("Stale Job");
    QVERIFY(lockHeldBy(stale, "bob", thisMachine.toStdString(), kDeadPid));
    {
      EditorWindow window;
      inspected = false;
      AnswerLockQuestion(window, "projectLockTakeOver", [&](QDialog& dialog) {
        inspected = !dialog.findChild<QLabel*>("projectLockStale")->isHidden() &&
                    dialog.findChild<QLabel*>("projectLockStale")->text().contains("no longer running") &&
                    dialog.findChild<QPushButton*>("projectLockTakeOver")->text() == "Take over (stale lock)" &&
                    !dialog.findChild<QLabel*>("projectLockTakeOverText")->text().contains("unsaved work");
        dialog.grab().save(Issue886Shot("qt-890-stale-lock.png"));
      });
      const auto uid = window.OpenProject(pathOf(stale), true);
      QVERIFY(inspected);
      QVERIFY(uid != 0);
      QVERIFY(!window.State().openProjects.front().readOnly);
      QCOMPARE(readLock(stale).pid, myPid);
    }
    QVERIFY(!QFileInfo::exists(QString::fromStdWString(gsproj::LockPath(stale.file).wstring())));  // released on close

    // A lock file nobody can read is stale too; a live process on this machine is not.
    const auto unreadable = makeProject("Garbled Job");
    {
      QFile lock(QString::fromStdWString(gsproj::LockPath(unreadable.file).wstring()));
      QVERIFY(lock.open(QIODevice::WriteOnly));
      lock.write("not a lock");
      lock.close();
      EditorWindow window;
      inspected = false;
      AnswerLockQuestion(window, "projectLockCancel", [&](QDialog& dialog) {
        inspected = !dialog.findChild<QLabel*>("projectLockStale")->isHidden();
      });
      QCOMPARE(window.OpenProject(pathOf(unreadable), true), std::uint32_t(0));
      QVERIFY(inspected);
    }
    const auto running = makeProject("Running Job");
    QVERIFY(lockHeldBy(running, "carol", thisMachine.toStdString(), myPid));
    {
      EditorWindow window;
      inspected = false;
      AnswerLockQuestion(window, "projectLockCancel", [&](QDialog& dialog) {
        inspected = dialog.findChild<QLabel*>("projectLockStale")->isHidden();
      });
      QCOMPARE(window.OpenProject(pathOf(running), true), std::uint32_t(0));
      QVERIFY(inspected);
    }

    // A takeover that cannot be written reports the failure and opens nothing.
    const auto blocked = makeProject("Blocked Job");
    QVERIFY(lockHeldBy(blocked, "dave", thisMachine.toStdString(), kDeadPid));
    const QString blockedLock = QString::fromStdWString(gsproj::LockPath(blocked.file).wstring());
    QVERIFY(QFile::setPermissions(blockedLock, QFileDevice::ReadOwner));
    {
      EditorWindow window;
      messages.texts.clear();
      AnswerLockQuestion(window, "projectLockTakeOver");
      QCOMPARE(window.OpenProject(pathOf(blocked), true), std::uint32_t(0));
      QVERIFY(window.State().openProjects.empty());
      QTRY_VERIFY(!messages.texts.isEmpty());
      QVERIFY(messages.texts.front().contains("Could not take over"));
      QCOMPARE(readLock(blocked).user, std::string("dave"));
    }
    QFile::setPermissions(blockedLock, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }

  void projectAttachQuestionFollowsThroughToPointCloudImport() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    gsproj::Project created; std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Cloud Job", &created, &error), error.c_str());
    QDir().mkpath(directory.filePath("outside"));
    auto writeScan = [&](const char* name) {
      QFile file(directory.filePath(QString("outside/") + name));
      if (!file.open(QIODevice::WriteOnly)) return QString();
      file.write("3\n0 0 0\n1 1 1\n2 2 2\n"); file.close();
      return file.fileName();
    };
    EditorWindow window;
    const auto uid = window.OpenProject(QString::fromStdWString(created.file.wstring()), true);
    QVERIFY(uid != 0);
    auto& state = window.State();
    auto& session = *std::find_if(state.openProjects.begin(), state.openProjects.end(), [uid](const auto& item) { return item.uid == uid; });
    const QString projectFolder = QString::fromStdWString(session.project.Folder().wstring());
    MessageCatcher messages;
    const auto none = AppCommandState::ProjectAttachPrompt::Kind::None;
    auto attach = [&](const QString& path) { window.Submit("POINTCLOUDATTACH \"" + QDir::toNativeSeparators(path) + "\""); };

    // Cancel starts no import and copies nothing.
    const QString cancelled = writeScan("cancelled.pts");
    bool askedForCloud = false;
    AnswerAttachQuestion(window, "projectAttachCancel", [&] {
      askedForCloud = state.projectAttachPrompt.kind == AppCommandState::ProjectAttachPrompt::Kind::PointCloud;
    });
    attach(cancelled);  // asks the question before Submit returns
    QVERIFY(askedForCloud);
    QTRY_VERIFY(state.projectAttachPrompt.kind == none);
    QVERIFY(!state.pointCloudImportAsync);
    QVERIFY(state.cadPointClouds.empty());
    QVERIFY(!QFileInfo::exists(projectFolder + "/PointClouds/cancelled.pts"));

    // Copy imports the project's copy.
    const QString copied = writeScan("copied.pts");
    AnswerAttachQuestion(window, "projectAttachCopy");
    attach(copied);
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(1), 20000);
    QVERIFY(QFileInfo::exists(projectFolder + "/PointClouds/copied.pts"));
    QCOMPARE(QFileInfo(QString::fromStdString(state.cadPointClouds[0]->sourcePath)).canonicalFilePath(),
             QFileInfo(projectFolder + "/PointClouds/copied.pts").canonicalFilePath());

    // Link imports the file where it is.
    const QString linked = writeScan("linked.pts");
    AnswerAttachQuestion(window, "projectAttachLink");
    attach(linked);
    QTRY_COMPARE_WITH_TIMEOUT(state.cadPointClouds.size(), std::size_t(2), 20000);
    QVERIFY(!QFileInfo::exists(projectFolder + "/PointClouds/linked.pts"));
    QCOMPARE(QFileInfo(QString::fromStdString(state.cadPointClouds[1]->sourcePath)).canonicalFilePath(),
             QFileInfo(linked).canonicalFilePath());
    QVERIFY(messages.texts.isEmpty());
  }

  void richTextColorsAreRenderedByQt() {
    auto state = StateWithTraverse();
    CadAnnotation annotation;
    annotation.kind = CadAnnotation::Kind::Mtext;
    annotation.boxMinX = -20; annotation.boxMaxX = 40;
    annotation.boxMinY = -10; annotation.boxMaxY = 20;
    annotation.text = "[[color:FF0000]]RED[[/color]] [[color:00FF00]]GREEN[[/color]]";
    state->cadAnnotations.push_back(annotation);
    state->cadAnnotationAttrs.emplace_back();
    QImage image(400, 400, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    DrawQtModelAnnotations(painter, *state, 400, 400); painter.end();
    int red = 0, green = 0;
    for (int x = 0; x < image.width(); ++x) for (int y = 0; y < image.height(); ++y) {
      const auto color = image.pixelColor(x, y);
      red += color.red() > 50 && color.red() > 2 * color.green();
      green += color.green() > 50 && color.green() > 2 * color.red();
    }
    QVERIFY(red > 5); QVERIFY(green > 5);
  }

  // ---- Issue #891: core geometry Properties panel and the drawing shortcut menu -------------------
  // The panel and menu are driven the way a user drives them (typing into the panel's own fields,
  // right-clicking the viewport); the rules they enforce are covered by CadPropertyEditTests.
  void Select891Line(EditorWindow& window) {
    auto& st = window.State();
    st.userLinesFlat = {0, 0, 0, 10, 0, 0};
    st.userCirclesCxCyZR = {5, 5, 0, 2};
    EnsureAttrCounts(st);
    st.selection = {{SelectedEntity::Type::LineSeg, 0}};
    window.Refresh();
  }
  static void Commit891(QLineEdit* edit, const QString& text) {
    edit->setText(text);
    QTest::keyClick(edit, Qt::Key_Return);
  }

  void propertiesPanelEditsLineGeometryAsWorldCoordinatesWithUndo() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    st.worldDocumentOriginX = 1000.0;
    Select891Line(window);
    auto* summary = window.findChild<QLabel*>("propertiesSummary");
    QVERIFY(summary); QVERIFY(summary->text().contains("Line"));
    auto* endX = window.findChild<QLineEdit*>("propLineEndX");
    QVERIFY(endX);
    QCOMPARE(endX->text(), QString("1010.0000"));  // shown in world space (REQ-101)
    Commit891(endX, "1025");
    QCOMPARE(st.userLinesFlat[3], 25.0);            // stored local
    QCOMPARE(window.findChild<QLineEdit*>("propLineEndX")->text(), QString("1025.0000"));
    QCOMPARE(window.findChild<QLabel*>("propDerivedLength")->text(), QString("25.0000"));
    Commit891(window.findChild<QLineEdit*>("propLineEndZ"), "4");
    QCOMPARE(st.userLinesFlat[5], 4.0);
    QVERIFY(window.findChild<QLabel*>("propDerivedRise"));  // a sloped line reports its rise
    window.Submit("UNDO");
    QCOMPARE(st.userLinesFlat[5], 0.0);
    QCOMPARE(st.userLinesFlat[3], 25.0);
    window.Submit("UNDO");
    QCOMPARE(st.userLinesFlat[3], 10.0);
  }

  void propertiesPanelRefusesBadInputAndCorrectsRadiusWithAMessage() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    Select891Line(window);
    const auto lineBefore = st.userLinesFlat;
    Commit891(window.findChild<QLineEdit*>("propLineStartX"), "abc");
    QCOMPARE(st.userLinesFlat, lineBefore);
    auto* note = window.findChild<QLabel*>("propertiesNote");
    QVERIFY(note); QVERIFY(note->text().contains("number"));
    st.selection = {{SelectedEntity::Type::Circle, 0}};
    window.Refresh();
    QCOMPARE(window.findChild<QLineEdit*>("propCircleRadius")->text(), QString("2.0000"));
    Commit891(window.findChild<QLineEdit*>("propCircleRadius"), "-3");
    QVERIFY(st.userCirclesCxCyZR[3] > 0.0 && st.userCirclesCxCyZR[3] < 1e-3);
    QVERIFY(window.findChild<QLabel*>("propertiesNote")->text().contains("positive"));
    window.Submit("UNDO");
    QCOMPARE(st.userCirclesCxCyZR[3], 2.0);
  }

  void propertiesPanelEditsTextContentHeightAndRotation() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    CadAnnotation text; text.kind = CadAnnotation::Kind::Text; text.text = "MH-1"; text.insX = 3.f; text.insY = 4.f;
    st.cadAnnotations.push_back(text); EnsureAttrCounts(st);
    st.selection = {{SelectedEntity::Type::Annotation, 0}};
    window.Refresh();
    QVERIFY(window.findChild<QLineEdit*>("propTextRotation"));       // TEXT has a rotation...
    QVERIFY(!window.findChild<QLineEdit*>("propTextBoxMinX"));       // ...and no box
    Commit891(window.findChild<QLineEdit*>("propTextRotation"), "90");
    QVERIFY(std::fabs(BearingCwNorthDegFromMathAngleRad(st.cadAnnotations[0].rotationRad) - 90.f) < 1e-3f);
    Commit891(window.findChild<QLineEdit*>("propTextHeight"), "0.2");
    QVERIFY(std::fabs(st.cadAnnotations[0].plottedHeightInches - 0.2f) < 1e-6f);
    auto* content = window.findChild<QPlainTextEdit*>("propTextContent");
    QVERIFY(content);
    content->setPlainText("MH-2");
    QTest::keyClick(content, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(QString::fromStdString(st.cadAnnotations[0].text), QString("MH-2"));
    window.Submit("UNDO");
    QCOMPARE(QString::fromStdString(st.cadAnnotations[0].text), QString("MH-1"));
  }

  void propertiesPanelGeneralEditsApplyToTheWholeMixedSelectionAsOneUndoStep() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    Select891Line(window);
    st.selection = {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}};
    window.Refresh();
    QVERIFY(window.findChild<QLabel*>("propertiesSummary")->text().contains("Mixed"));
    QVERIFY(!window.findChild<QLineEdit*>("propLineStartX"));  // mixed: geometry is read-only
    QVERIFY(window.findChild<QLabel*>("propertiesMixedNote"));
    auto* layer = window.findChild<QComboBox*>("propLayer");
    QVERIFY(layer && layer->isEnabled());
    layer->lineEdit()->setText("CURB");
    QTest::keyClick(layer->lineEdit(), Qt::Key_Return);
    QCOMPARE(QString::fromStdString(st.userLineAttrs[0].layer), QString("CURB"));
    QCOMPARE(QString::fromStdString(st.userCircleAttrs[0].layer), QString("CURB"));
    auto* transparency = window.findChild<QComboBox*>("propTransparency");
    QMetaObject::invokeMethod(transparency, "activated", Q_ARG(int, 3));  // 50 %
    QCOMPARE(st.userLineAttrs[0].transparency, 0.5f);
    QCOMPARE(st.userCircleAttrs[0].transparency, 0.5f);
    auto* linetype = window.findChild<QComboBox*>("propLinetype");
    QMetaObject::invokeMethod(linetype, "activated", Q_ARG(int, 3));      // Dashed
    QCOMPARE(QString::fromStdString(st.userCircleAttrs[0].linetype), QString("DASHED"));
    window.Submit("UNDO"); window.Submit("UNDO");
    QCOMPARE(st.userCircleAttrs[0].transparency, -1.f);
    QCOMPARE(QString::fromStdString(st.userCircleAttrs[0].layer), QString("CURB"));
    window.Submit("UNDO");
    QCOMPARE(QString::fromStdString(st.userCircleAttrs[0].layer), QString("0"));
    // Selecting objects that differ shows "(varies)" rather than one object's value.
    st.selection = {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}};
    st.userCircleAttrs[0].lineweightMm = 0.5f;
    window.m_PropertiesFingerprint = ~0ull; window.Refresh();
    QCOMPARE(window.findChild<QComboBox*>("propLineweight")->currentText(), QString("(varies)"));
  }

  void propertiesPanelWithNothingSelectedShowsDimmedGeneralAndGeometry() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.State().selection.clear(); window.Refresh();
    QCOMPARE(window.findChild<QLabel*>("propertiesSummary")->text(), QString("No selection"));
    QVERIFY(!window.findChild<QComboBox*>("propLayer")->isEnabled());
    QVERIFY(!window.findChild<QComboBox*>("propTransparency")->isEnabled());
    QVERIFY(!window.findChild<QLineEdit*>("propLineStartX"));
  }

  QMenu* RightClick891(EditorWindow& window) {
    if (auto* old = window.findChild<QMenu*>("drawingContextMenu")) { old->close(); delete old; }
    auto* viewport = window.findChild<QWidget*>("modelViewport");
    QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier, QPoint(40, 40));
    return window.findChild<QMenu*>("drawingContextMenu");
  }
  static QAction* MenuAction891(QMenu* menu, const char* object) {
    for (auto* action : menu->findChildren<QAction*>(object)) return action;
    return nullptr;
  }

  void shortcutMenuWithASelectionOffersTheSharedEditActions() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    Select891Line(window);
    auto* menu = RightClick891(window);
    QVERIFY(menu);                                   // right-click with a selection opens the menu
    QVERIFY(MenuAction891(menu, "ctxSelectSimilar"));
    QVERIFY(MenuAction891(menu, "ctxQuickSelect"));
    QVERIFY(!MenuAction891(menu, "ctxEnter"));
    auto* modify = menu->findChild<QMenu*>("ctxModify");
    QVERIFY(modify && modify->isEnabled());
    QStringList tools; for (auto* action : modify->actions()) tools << action->text();
    for (const char* tool : {"Move", "Copy Selection", "Rotate", "Scale", "Erase", "Offset", "Trim", "Join"}) QVERIFY2(tools.contains(tool), tool);
    // Find... is deliberately absent (REQ-084).
    for (auto* action : menu->actions()) QVERIFY(!action->text().startsWith("Find"));
    MenuAction891(menu, "ctxClearSelection")->trigger();
    QVERIFY(st.selection.empty());
    // Isolation items come from the same commands the ribbon uses.
    Select891Line(window);
    menu = RightClick891(window);
    MenuAction891(menu, "ctxHideObjects")->trigger();
    QVERIFY(!st.hiddenEntityIds.empty());
    menu = RightClick891(window);
    QVERIFY(MenuAction891(menu, "ctxEndIsolation")->isEnabled());
    MenuAction891(menu, "ctxEndIsolation")->trigger();
    QVERIFY(st.hiddenEntityIds.empty());
  }

  void shortcutMenuModifyToolStartsTheCommandOnTheSelection() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    Select891Line(window);
    auto* menu = RightClick891(window);
    auto* modify = menu->findChild<QMenu*>("ctxModify");
    for (auto* action : modify->actions()) if (action->text() == "Move") action->trigger();
    QCOMPARE(window.State().active, AppCommandState::Kind::Move);
    // A running command gets the short Command Mode menu only when its preference says so.
    window.State().rightClickCommandMode = AppCommandState::RightClickCommandMode::ShortcutMenuAlways;
    menu = RightClick891(window);
    QVERIFY(menu);
    QVERIFY(MenuAction891(menu, "ctxEnter")); QVERIFY(MenuAction891(menu, "ctxCancel"));
    QVERIFY(!menu->findChild<QMenu*>("ctxModify"));
    MenuAction891(menu, "ctxCancel")->trigger();
    QCOMPARE(window.State().active, AppCommandState::Kind::None);
  }

  void rightClickModesFollowThePreferencesForEveryContext() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    st.rightClickDefaultMode = AppCommandState::RightClickDefaultMode::RepeatLastCommand;
    window.Submit("LINE"); window.Submit("0,0"); window.Submit("10,0");
    std::vector<std::string> log; CancelActiveCommand(st, log);
    st.selection.clear();
    QVERIFY(!RightClick891(window));                 // no menu: Default Mode repeats LINE
    QCOMPARE(st.active, AppCommandState::Kind::Line);
    CancelActiveCommand(st, log);
    st.rightClickDefaultMode = AppCommandState::RightClickDefaultMode::ShortcutMenu;
    QVERIFY(RightClick891(window));                  // Default Mode = Shortcut Menu
    window.Submit("LINE");
    st.rightClickCommandMode = AppCommandState::RightClickCommandMode::Enter;
    QVERIFY(!RightClick891(window));                 // Command Mode = ENTER: no menu
    st.rightClickCommandMode = AppCommandState::RightClickCommandMode::ShortcutMenuAlways;
    QVERIFY(RightClick891(window));
  }

  void timeSensitiveRightClickIsEnterWhenQuickAndTheMenuWhenHeld() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show();
    auto& st = window.State();
    st.rightClickTimeSensitive = true;
    // Keep the quick-click threshold above a heavily loaded test runner's scheduling jitter;
    // the exact boundary behavior is covered by CadRightClickHoldIsMenu unit tests.
    st.rightClickLongerClickMs = 1000;
    st.rightClickDefaultMode = AppCommandState::RightClickDefaultMode::RepeatLastCommand;
    window.Submit("LINE"); window.Submit("0,0"); window.Submit("10,0");
    std::vector<std::string> log; CancelActiveCommand(st, log);
    st.selection.clear();
    auto* viewport = window.findChild<QWidget*>("modelViewport");
    QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier, QPoint(40, 40));   // quick = ENTER = repeat
    QCOMPARE(st.active, AppCommandState::Kind::Line);
    QVERIFY(!window.findChild<QMenu*>("drawingContextMenu"));
    CancelActiveCommand(st, log);
    QTest::mousePress(viewport, Qt::RightButton, Qt::NoModifier, QPoint(40, 40));   // held = menu
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMenu*>("drawingContextMenu"), 2000);
    QTest::mouseRelease(viewport, Qt::RightButton, Qt::NoModifier, QPoint(40, 40));
    QVERIFY(window.findChild<QMenu*>("drawingContextMenu"));
    QCOMPARE(st.active, AppCommandState::Kind::None);                                // release did not also repeat
  }

  void rightClickCustomizationCancelsApplyPersistsAndGreysTimerGovernedModes() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    window.ShowRightClickCustomization();
    auto* dialog = window.findChild<QDialog*>("rightClickCustomization");
    QVERIFY(dialog);
    auto* timed = dialog->findChild<QCheckBox*>("rightClickTimeSensitive");
    auto* duration = dialog->findChild<QSpinBox*>("rightClickLongerClickMs");
    QVERIFY(!timed->isChecked()); QVERIFY(!duration->isEnabled());
    timed->setChecked(true);
    QVERIFY(duration->isEnabled());
    QVERIFY(!dialog->findChild<QGroupBox*>("rightClickDefaultMode")->isEnabled());   // greyed, not ignored
    QVERIFY(dialog->findChild<QGroupBox*>("rightClickEditMode")->isEnabled());      // Edit Mode stays live
    QVERIFY(!dialog->findChild<QGroupBox*>("rightClickCommandMode")->isEnabled());
    timed->setChecked(false);
    QVERIFY(dialog->findChild<QGroupBox*>("rightClickDefaultMode")->isEnabled());
    timed->setChecked(true); duration->setValue(400);
    dialog->findChild<QRadioButton*>("rightClickEditMode_0")->setChecked(true);
    dialog->findChild<QPushButton*>("rightClickCancel")->click();                   // Cancel leaves everything alone
    QVERIFY(!st.rightClickTimeSensitive);
    QCOMPARE(st.rightClickLongerClickMs, 250);
    QCOMPARE(st.rightClickEditMode, AppCommandState::RightClickEditMode::ShortcutMenu);

    window.ShowRightClickCustomization();
    dialog = window.findChild<QDialog*>("rightClickCustomization");
    dialog->findChild<QCheckBox*>("rightClickTimeSensitive")->setChecked(true);
    dialog->findChild<QSpinBox*>("rightClickLongerClickMs")->setValue(400);
    dialog->findChild<QRadioButton*>("rightClickEditMode_0")->setChecked(true);
    dialog->findChild<QPushButton*>("rightClickApply")->click();
    QVERIFY(st.rightClickTimeSensitive);
    QCOMPARE(st.rightClickLongerClickMs, 400);
    QCOMPARE(st.rightClickEditMode, AppCommandState::RightClickEditMode::RepeatLastCommand);
    AppCommandState reloaded; LoadUserStartupPrefs(reloaded);                        // it survives a restart
    QVERIFY(reloaded.rightClickTimeSensitive);
    QCOMPARE(reloaded.rightClickLongerClickMs, 400);
    QCOMPARE(reloaded.rightClickEditMode, AppCommandState::RightClickEditMode::RepeatLastCommand);
  }

  void rightClickCustomizationSaveFailureChangesNothing() {
    IsolatedUserData userData;
    // A directory where the preferences file belongs makes the write fail.
    QVERIFY(QDir().mkpath(QDir(userData.dir.path()).filePath("GoSurvey/gosurvey-user.json")));
    MessageCatcher messages;
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    window.ShowRightClickCustomization();
    auto* dialog = window.findChild<QDialog*>("rightClickCustomization");
    dialog->findChild<QCheckBox*>("rightClickTimeSensitive")->setChecked(true);
    dialog->findChild<QPushButton*>("rightClickApply")->click();
    QVERIFY(!st.rightClickTimeSensitive);
    QTRY_VERIFY(!messages.texts.isEmpty());
    QVERIFY(messages.texts.first().contains("could not be saved"));
    QVERIFY(window.findChild<QDialog*>("rightClickCustomization"));                 // stays open to retry
  }

  void shortcutMenuOptionsAndSelectionOpenNativeDialogs() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate();
    auto& st = window.State();
    Select891Line(window);
    st.selection = {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}};
    auto* menu = RightClick891(window);
    MenuAction891(menu, "ctxSelectionList")->trigger();
    auto* list = window.findChild<QDialog*>("selectionList");
    QVERIFY(list);
    auto* items = list->findChild<QListWidget*>("selectionListItems");
    QCOMPARE(items->count(), 2);
    items->item(1)->setCheckState(Qt::Unchecked);                                   // un-ticks just the circle
    QCOMPARE(st.selection.size(), std::size_t(1));
    QCOMPARE(st.selection.front().type, SelectedEntity::Type::LineSeg);
    list->findChild<QPushButton*>("selectionListSelectAll")->click();
    QCOMPARE(st.selection.size(), std::size_t(2));
    list->findChild<QPushButton*>("selectionListDeselectAll")->click();
    QVERIFY(st.selection.empty());
    list->close();
    Select891Line(window);
    menu = RightClick891(window);
    MenuAction891(menu, "ctxOptions")->trigger();
    auto* settings = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(settings);
    QVERIFY(settings->findChild<QPushButton*>("settingsRightClickCustomization"));
    settings->findChild<QPushButton*>("settingsRightClickCustomization")->click();
    QVERIFY(window.findChild<QDialog*>("rightClickCustomization"));
  }

  // Screenshot evidence for issue #891 (selected designs), written under build/artifacts.
  static QImage GrabDock891(EditorWindow& window) {
    QCoreApplication::processEvents();
    auto* dock = window.findChild<QDockWidget*>("propertiesDock");
    const QPoint origin = dock->mapTo(&window, QPoint(0, 0));
    auto* body = dock->findChild<QWidget*>("propertiesBody");
    const int height = body ? std::min(dock->height(), body->sizeHint().height() + 40) : dock->height();
    return window.grab(QRect(origin, QSize(dock->width(), height))).toImage();
  }

  void qt891ScreenshotsOfPropertiesMenuAndRightClickDialog() {
    IsolatedUserData userData;
    EditorWindow window; window.CreateDrawingFromTemplate();
    window.resize(1500, 920); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto& st = window.State();
    Select891Line(window);
    st.userLinesFlat[5] = 1.5;
    CadAnnotation text; text.kind = CadAnnotation::Kind::Text; text.text = "MH-1"; text.insX = 3.f; text.insY = 4.f;
    st.cadAnnotations.push_back(text); EnsureAttrCounts(st);
    const QString dir = QtTestArtifactDirectory();
    QDir().mkpath(dir);
    auto save = [&](const QImage& image, const char* name) { QVERIFY(image.save(QDir(dir).filePath(name))); };
    struct Shot { const char* name; std::vector<SelectedEntity> selection; };
    const Shot shots[] = {
        {"qt-891-final-properties-line.png", {{SelectedEntity::Type::LineSeg, 0}}},
        {"qt-891-final-properties-circle.png", {{SelectedEntity::Type::Circle, 0}}},
        {"qt-891-final-properties-text.png", {{SelectedEntity::Type::Annotation, 0}}},
        {"qt-891-final-properties-mixed.png", {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}}},
        {"qt-891-final-properties-empty.png", {}}};
    for (const auto& shot : shots) {
      st.selection = shot.selection;
      window.m_PropertiesFingerprint = ~0ull; window.Refresh();
      save(GrabDock891(window), shot.name);
    }
    // A refused entry stays visible in the panel.
    st.selection = {{SelectedEntity::Type::Circle, 0}};
    window.m_PropertiesFingerprint = ~0ull; window.Refresh();
    Commit891(window.findChild<QLineEdit*>("propCircleRadius"), "-3");
    save(GrabDock891(window), "qt-891-final-properties-corrected.png");
    window.Submit("UNDO");
    // Shortcut menu with a selection, and while a command runs.
    Select891Line(window);
    auto* menu = RightClick891(window);
    QVERIFY(menu);
    QTest::qWait(50);
    save(menu->grab().toImage(), "qt-891-final-menu-selection.png");
    menu->close();
    st.rightClickCommandMode = AppCommandState::RightClickCommandMode::ShortcutMenuAlways;
    window.Submit("LINE");
    menu = RightClick891(window);
    QVERIFY(menu);
    QTest::qWait(50);
    save(menu->grab().toImage(), "qt-891-final-menu-command.png");
    menu->close();
    CancelActiveCommand(st, window.m_Log);
    // Right-click Customization: timer off and timer on.
    window.ShowRightClickCustomization();
    auto* dialog = window.findChild<QDialog*>("rightClickCustomization");
    QVERIFY(dialog);
    QTest::qWait(50);
    save(dialog->grab().toImage(), "qt-891-final-rightclick-default.png");
    dialog->findChild<QCheckBox*>("rightClickTimeSensitive")->setChecked(true);
    QTest::qWait(50);
    save(dialog->grab().toImage(), "qt-891-final-rightclick-timed.png");
    dialog->close(); delete dialog;
    // Selection list.
    st.selection = {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}};
    st.selectionCycleEntities = st.selection;
    window.ShowSelectionList();
    auto* list = window.findChild<QDialog*>("selectionList");
    QVERIFY(list);
    QTest::qWait(50);
    save(list->grab().toImage(), "qt-891-final-selection-list.png");
    list->close();
  }

  void blockAuthoringPaletteUsesSharedCommandsAndDisabledToolsStayUnavailable() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    CadBlockDefinition definition;
    definition.name = "Issue895PaletteBlock";
    window.State().blockDefs.push_back(definition);
    window.Submit("BEDIT Issue895PaletteBlock");
    window.show();
    QTest::qWait(80);

    auto* palette = window.findChild<QDialog*>("blockAuthoringPalette");
    QVERIFY(palette);
    auto* point = palette->findChild<QPushButton*>("blockParameterPoint");
    QVERIFY(point);
    const int blockIndex = CadBlockFindDef(window.State().blockDefs, "Issue895PaletteBlock");
    QVERIFY(blockIndex >= 0);
    const auto initialParameters = window.State().blockDefs[blockIndex].parameters.size();
    const auto initialActions = window.State().blockDefs[blockIndex].actions.size();
    const auto initialUndoDepth = window.State().documents[window.State().activeDrawingIdx].undoStack.size();
    point->click();
    QCOMPARE(window.State().blockDefs[blockIndex].parameters.size(), initialParameters + 1);
    QCOMPARE(window.State().documents[window.State().activeDrawingIdx].undoStack.size(), initialUndoDepth + 1);

    window.Submit("UNDO");
    QCOMPARE(window.State().blockDefs[blockIndex].parameters.size(), initialParameters);
    point->click();
    QCOMPARE(window.State().blockDefs[blockIndex].parameters.size(), initialParameters + 1);
    auto* actions = palette->findChild<QPushButton*>("blockAuthoringTab1");
    QVERIFY(actions);
    actions->click();
    auto* move = palette->findChild<QPushButton*>("blockActionMove");
    QVERIFY(move);
    move->click();
    QCOMPARE(window.State().blockDefs[blockIndex].actions.size(), initialActions + 1);

    auto* sets = palette->findChild<QPushButton*>("blockAuthoringTab2");
    QVERIFY(sets);
    sets->click();
    auto* pointMove = palette->findChild<QPushButton*>("blockSetPointMove");
    QVERIFY(pointMove);
    pointMove->click();
    QCOMPARE(window.State().blockDefs[blockIndex].parameters.size(), initialParameters + 2);
    QCOMPARE(window.State().blockDefs[blockIndex].actions.size(), initialActions + 2);

    auto* constraints = palette->findChild<QPushButton*>("blockAuthoringTab3");
    QVERIFY(constraints);
    constraints->click();
    auto* coincident = palette->findChild<QPushButton*>("blockConstraintCoincident");
    auto* aligned = palette->findChild<QPushButton*>("blockDimensionalAligned");
    QVERIFY(coincident && aligned);
    QVERIFY(!coincident->isEnabled());
    QVERIFY(!aligned->isEnabled());
    auto* actionTab = palette->findChild<QPushButton*>("blockAuthoringTab1");
    actionTab->click();
    auto* table = palette->findChild<QPushButton*>("unavailable_block properties table");
    QVERIFY(table);
    QVERIFY(!table->isEnabled());
  }

  void connectionModesDialogEditsUndoAndDirtyCloseChoicesWork() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    CadBlockDefinition definition;
    definition.name = "Issue895ModesBlock";
    CadBlockConnection connection;
    connection.name = "Inlet";
    connection.engagementLength = 0.25f;
    CadBlockConnectionMode initial;
    initial.name = "Pipe";
    initial.target = CadConnectionModeTarget::PipeEnd;
    initial.isDefault = true;
    connection.modes.push_back(initial);
    definition.connections.push_back(connection);
    window.State().blockDefs.push_back(definition);
    window.Submit("BEDIT Issue895ModesBlock");
    window.show();
    window.State().showConnectionModesWindow = true;
    window.Refresh();
    QTest::qWait(80);

    auto* modes = window.findChild<QDialog*>("connectionModes");
    QVERIFY(modes);
    auto* list = modes->findChild<QListWidget*>("connectionPointList");
    auto* table = modes->findChild<QTableWidget*>("connectionModeTable");
    QVERIFY(list && table);
    const int blockIndex = CadBlockFindDef(window.State().blockDefs, "Issue895ModesBlock");
    QVERIFY(blockIndex >= 0);
    QCOMPARE(table->rowCount(), 1);
    const auto undoBeforeEdit = window.State().documents[window.State().activeDrawingIdx].undoStack.size();
    table->item(0, 0)->setText("Pipe end");
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes[0].name, std::string("Pipe end"));
    QCOMPARE(window.State().documents[window.State().activeDrawingIdx].undoStack.size(), undoBeforeEdit + 1);
    window.Submit("UNDO");
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes[0].name, std::string("Pipe"));

    table = modes->findChild<QTableWidget*>("connectionModeTable");
    QVERIFY(table);
    table->item(0, 0)->setText("");
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes[0].name, std::string("Pipe"));
    QVERIFY(modes->findChild<QLabel*>("connectionModesStatus")->text().contains("needs a name"));
    table = modes->findChild<QTableWidget*>("connectionModeTable");
    auto* addMode = modes->findChild<QPushButton*>("addConnectionMode");
    QVERIFY(table && addMode);
    addMode->click();
    QCoreApplication::processEvents();
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes.size(), std::size_t(2));
    table = modes->findChild<QTableWidget*>("connectionModeTable");
    QVERIFY(table);
    auto* makeDefault = table->findChild<QCheckBox*>("connectionModeDefault1");
    QVERIFY(makeDefault);
    makeDefault->click();
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes[0].isDefault, false);
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes[1].isDefault, true);
    QCoreApplication::processEvents();
    table = modes->findChild<QTableWidget*>("connectionModeTable");
    auto* remove = table->findChild<QPushButton*>("removeConnectionMode0");
    QVERIFY(remove);
    remove->click();
    QCoreApplication::processEvents();
    QCOMPARE(window.State().blockDefs[blockIndex].connections[0].modes.size(), std::size_t(1));
    QVERIFY(window.State().blockDefs[blockIndex].connections[0].modes[0].isDefault);

    // Parameter/action commands must dirty the BEDIT session, and each edit is undoable.
    window.Submit("BPARAM Linear, linear");
    QVERIFY(!window.State().blockDefs[blockIndex].parameters.empty());
    const auto savedParameters = window.State().blockDefs[blockIndex].parameters.size();
    auto answerClose = [&window](const QString& label) {
      QTimer::singleShot(0, &window, [&window, label] {
        auto* prompt = window.findChild<QMessageBox*>("blockEditClosePrompt");
        QVERIFY(prompt);
        for (auto* button : prompt->buttons())
          if (button->text() == label) { button->click(); return; }
        QFAIL("Expected dirty block-close choice was missing");
      });
      window.Submit("BCLOSE");
    };
    answerClose("Cancel");
    QVERIFY(!window.State().blockEditorName.empty());
    QVERIFY(!window.State().blockEditCloseAsked);
    answerClose("Save Changes");
    QVERIFY(window.State().blockEditorName.empty());
    QCOMPARE(window.State().blockDefs[blockIndex].parameters.size(), savedParameters);
    QTRY_VERIFY(!window.findChild<QDialog*>("connectionModes"));

    window.Submit("BEDIT Issue895ModesBlock");
    window.Submit("BPARAM Rotation, rotation");
    answerClose("Don't Save");
    QVERIFY(window.State().blockEditorName.empty());
    QCOMPARE(window.State().blockDefs[blockIndex].parameters.size(), savedParameters);
  }

  void qt895BlockEditorWorkflowScreenshots() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    CadBlockDefinition definition;
    definition.name = "Issue895ScreenshotBlock";
    CadBlockConnection connection;
    connection.name = "Inlet";
    connection.engagementLength = 0.25f;
    CadBlockConnectionMode mode;
    mode.name = "Pipe end";
    mode.target = CadConnectionModeTarget::PipeEnd;
    mode.isDefault = true;
    connection.modes.push_back(mode);
    definition.connections.push_back(connection);
    window.State().blockDefs.push_back(definition);
    window.Submit("BEDIT Issue895ScreenshotBlock");
    window.show();
    QTest::qWait(100);

    auto* palette = window.findChild<QDialog*>("blockAuthoringPalette");
    QVERIFY(palette);
    auto save = [](QWidget* widget, const char* name) {
      const QString directory = QtTestArtifactDirectory();
      QDir().mkpath(directory);
      QVERIFY(widget->grab().save(QDir(directory).filePath(QString::fromLatin1(name))));
    };
    const char* tabNames[] = {"Parameters", "Actions", "Parameter Sets", "Constraints"};
    for (int i = 0; i < 4; ++i) {
      auto* tab = palette->findChild<QPushButton*>(QString("blockAuthoringTab%1").arg(i).toUtf8().constData());
      QVERIFY(tab);
      tab->click();
      QTest::qWait(40);
      save(palette, (QString("qt-895-palette-%1.png").arg(QString::fromLatin1(tabNames[i])).toUtf8().constData()));
    }
    window.State().showConnectionModesWindow = true;
    window.Refresh();
    QTest::qWait(80);
    auto* modes = window.findChild<QDialog*>("connectionModes");
    QVERIFY(modes);
    save(modes, "qt-895-connection-modes.png");
    auto* modeTable = modes->findChild<QTableWidget*>("connectionModeTable");
    QVERIFY(modeTable);
    save(modeTable, "qt-895-connection-mode-table.png");
    modeTable->item(0, 0)->setText("");
    QTest::qWait(40);
    save(modes, "qt-895-connection-mode-invalid.png");
    modes->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const int blockIndex = CadBlockFindDef(window.State().blockDefs, "Issue895ScreenshotBlock");
    QVERIFY(blockIndex >= 0);
    window.State().blockDefs[blockIndex].connections.clear();
    window.State().showConnectionModesWindow = true;
    window.Refresh();
    QTest::qWait(60);
    auto* emptyModes = window.findChild<QDialog*>("connectionModes");
    QVERIFY(emptyModes);
    save(emptyModes, "qt-895-connection-modes-empty.png");
    window.Submit("BPARAM Point, point");
    QTimer::singleShot(0, &window, [&] {
      auto* prompt = window.findChild<QMessageBox*>("blockEditClosePrompt");
      QVERIFY(prompt);
      save(prompt, "qt-895-close-unsaved.png");
      for (auto* button : prompt->buttons())
        if (button->text() == "Cancel") { button->click(); return; }
      QFAIL("Cancel button missing");
    });
    window.Submit("BCLOSE");
  }

  // ---- Issue #893: surface and surface-style dialogs ----------------------------------------------

  void createSurfaceDialogCancelsValidatesAndCreatesOneUndoStep() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    QCOMPARE(st.cadSurfaces.size(), size_t(2));
    // Cancel changes nothing and records nothing.
    const bool couldUndo = CanUndo(st);
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->grab().save(Issue893Shot("qt-893-create-default.png"));
      dialog->findChild<QLineEdit*>("createSurfaceName")->setText("Never created");
      dialog->findChild<QPushButton*>("createSurfaceCancel")->click();
    });
    window.ShowCreateSurface();
    QCOMPARE(st.cadSurfaces.size(), size_t(2));
    QCOMPARE(CanUndo(st), couldUndo);
    // A taken name is refused with the reason and keeps the dialog open; a new one creates the surface.
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      auto* name = dialog->findChild<QLineEdit*>("createSurfaceName");
      auto* ok = dialog->findChild<QPushButton*>("createSurfaceOk");
      QCOMPARE(name->text(), QString("Surface"));  // next free default name
      name->setText("EG");
      ok->click();
      QVERIFY(dialog->isVisible());
      QVERIFY(dialog->findChild<QLabel*>("createSurfaceError")->text().contains("already exists"));
      name->setText("");
      QVERIFY(!ok->isEnabled());  // an empty name cannot be submitted
      name->setText("Pond");
      dialog->findChild<QLineEdit*>("createSurfaceDescription")->setText("Retention pond");
      ok->click();
    });
    window.ShowCreateSurface();
    QCOMPARE(st.cadSurfaces.size(), size_t(3));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[2].name), QString("Pond"));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[2].description), QString("Retention pond"));
    std::vector<std::string> log;
    QVERIFY(DoUndo(st, log));
    QCOMPARE(st.cadSurfaces.size(), size_t(2));
  }

  void createSurfaceDialogGridAndVolumeKindsShowTheirFieldsAndValidate() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      auto* kind = dialog->findChild<QComboBox*>("createSurfaceKind");
      auto* grid = dialog->findChild<QWidget*>("createSurfaceGridGroup");
      auto* volume = dialog->findChild<QWidget*>("createSurfaceVolumeGroup");
      QVERIFY(!grid->isVisible() && !volume->isVisible());
      kind->setCurrentIndex(1);
      QVERIFY(grid->isVisible() && !volume->isVisible());
      dialog->findChild<QDoubleSpinBox*>("createSurfaceSpacingX")->setValue(0.0);
      dialog->findChild<QPushButton*>("createSurfaceOk")->click();
      QCOMPARE(dialog->findChild<QLabel*>("createSurfaceError")->text(), QString("Grid spacing must be positive."));
      dialog->findChild<QDoubleSpinBox*>("createSurfaceSpacingX")->setValue(5.0);
      dialog->grab().save(Issue893Shot("qt-893-create-grid.png"));
      kind->setCurrentIndex(3);
      QVERIFY(!grid->isVisible() && volume->isVisible());
      auto* base = dialog->findChild<QComboBox*>("createSurfaceBase");
      auto* comparison = dialog->findChild<QComboBox*>("createSurfaceComparison");
      QCOMPARE(base->currentText(), QString("EG"));
      QCOMPARE(comparison->currentText(), QString("Proposed"));
      comparison->setCurrentIndex(base->currentIndex());
      dialog->findChild<QLineEdit*>("createSurfaceName")->setText("Volume");
      dialog->findChild<QPushButton*>("createSurfaceOk")->click();
      QCOMPARE(dialog->findChild<QLabel*>("createSurfaceError")->text(), QString("Base and comparison must be different surfaces."));
      dialog->findChild<QPushButton*>("createSurfaceCancel")->click();
    });
    window.ShowCreateSurface();
    QCOMPARE(st.cadSurfaces.size(), size_t(2));
    // A grid surface is created through the same command the command line uses.
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->findChild<QComboBox*>("createSurfaceKind")->setCurrentIndex(1);
      dialog->findChild<QLineEdit*>("createSurfaceName")->setText("Grid");
      dialog->findChild<QSpinBox*>("createSurfaceColumns")->setValue(3);
      dialog->findChild<QSpinBox*>("createSurfaceRows")->setValue(3);
      dialog->findChild<QPushButton*>("createSurfaceOk")->click();
    });
    window.ShowCreateSurface();
    QCOMPARE(st.cadSurfaces.size(), size_t(3));
    QVERIFY(st.cadSurfaces[2].kind == SurfaceKind::Grid);
  }

  void surfaceRequestsFromRibbonCommandsAndToolspaceOpenTheNativeDialogs() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    // Create Surface (ribbon / Toolspace set the flag the command layer sets).
    st.showCreateSurfaceWindow = true;
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->reject();
    });
    window.Refresh();
    QVERIFY(!st.showCreateSurfaceWindow);
    // Surface Properties for the selected surface.
    st.surfacePropertiesIndex = 1;
    st.showSurfacePropertiesWindow = true;
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      QCOMPARE(dialog->windowTitle(), QString("Surface Properties - Proposed"));
      dialog->reject();
    });
    window.Refresh();
    QVERIFY(!st.showSurfacePropertiesWindow);
    // SURFSTYLE opens the editor on the requested style, with the 0.7.4 title rule.
    st.showSurfaceStyleWindow = true;
    st.surfaceStyleUseSurfacesTitle = true;
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      QCOMPARE(dialog->windowTitle(), QString("Surfaces"));
      dialog->reject();
    });
    window.Refresh();
    QVERIFY(!st.showSurfaceStyleWindow);
    st.showSurfaceStyleWindow = true;
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      QCOMPARE(dialog->windowTitle(), QString("Surface Style"));
      dialog->reject();
    });
    window.Refresh();
    // The Surfaces manager is modeless and is raised, not duplicated.
    st.showSurfaceManagerWindow = true;
    window.Refresh();
    auto* manager = window.findChild<QDialog*>("surfaceManager");
    QVERIFY(manager);
    st.showSurfaceManagerWindow = true;
    window.Refresh();
    QCOMPARE(window.findChildren<QDialog*>("surfaceManager").size(), 1);
    manager->close();
    // The Toolspace lists surfaces and surface styles and tags them for its context menu.
    auto* toolspace = window.findChild<QDockWidget*>("toolspaceDock")->findChild<QTreeWidget*>();
    QVERIFY(toolspace);
    QTreeWidgetItem* surfaces = nullptr;
    QTreeWidgetItem* styles = nullptr;
    for (int i = 0; i < toolspace->topLevelItemCount(); ++i) {
      if (toolspace->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == "surfacesFolder") surfaces = toolspace->topLevelItem(i);
      if (toolspace->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == "stylesFolder") styles = toolspace->topLevelItem(i);
    }
    QVERIFY(surfaces && styles);
    QCOMPARE(surfaces->childCount(), 2);
    QCOMPARE(surfaces->child(1)->data(0, Qt::UserRole + 2).toInt(), 1);
    QCOMPARE(styles->child(0)->text(0), QString("Standard"));
  }

  void surfacePropertiesStagesInformationAppliesAsOneStepAndAnalysisIsImmediate() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    std::vector<std::string> log;
    QVERIFY(surfedit::AddStyle(st, 0, "Roads", log));
    st.cadSurfaces[0].sourcePointGroups = {"Ground", "Curb"};
    // Cancel discards the staged information.
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      QCOMPARE(dialog->findChild<QLineEdit*>("surfacePropsName")->text(), QString("EG"));
      dialog->findChild<QLineEdit*>("surfacePropsName")->setText("Changed");
      dialog->findChild<QPushButton*>("surfacePropsCancel")->click();
    });
    window.ShowSurfaceProperties(0);
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].name), QString("EG"));
    // An empty or taken name is refused in the dialog and nothing changes; then OK applies all three fields.
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      auto* name = dialog->findChild<QLineEdit*>("surfacePropsName");
      auto* problem = dialog->findChild<QLabel*>("surfacePropsNameProblem");
      name->setText("");
      dialog->findChild<QPushButton*>("surfacePropsOk")->click();
      QVERIFY(dialog->isVisible());
      QVERIFY(problem->text().contains("cannot be empty"));
      name->setText("Proposed");
      dialog->findChild<QPushButton*>("surfacePropsApply")->click();
      QVERIFY(dialog->isVisible());
      QVERIFY(problem->text().contains("already exists"));
      name->setText("Ground");
      dialog->findChild<QPlainTextEdit*>("surfacePropsDescription")->setPlainText("Existing ground");
      dialog->findChild<QComboBox*>("surfacePropsStyle")->setCurrentText("Roads");
      dialog->findChild<QPushButton*>("surfacePropsOk")->click();
    });
    window.ShowSurfaceProperties(0);
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].name), QString("Ground"));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].description), QString("Existing ground"));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].styleName), QString("Roads"));
    QVERIFY(DoUndo(st, log));  // one step undoes all three
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].name), QString("EG"));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].styleName), QString());
    // Definition reorder, analysis and statistics.
    st.cadSurfaces[0].styleName = "Roads";
    WhenDialogShows(window, [&window, &st] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      auto* tabs = dialog->findChild<QTabWidget*>("surfacePropertiesTabs");
      QCOMPARE(tabs->count(), 4);
      tabs->setCurrentIndex(1);
      auto* operations = dialog->findChild<QTableWidget*>("surfacePropsOperations");
      QCOMPARE(operations->rowCount(), 2);
      QCOMPARE(operations->item(0, 1)->text(), QString("Ground"));
      operations->selectRow(0);
      QVERIFY(!dialog->findChild<QPushButton*>("surfacePropsMoveUp")->isEnabled());
      dialog->findChild<QPushButton*>("surfacePropsMoveDown")->click();
      QCOMPARE(operations->item(0, 1)->text(), QString("Curb"));
      QCOMPARE(QString::fromStdString(st.cadSurfaces[0].sourcePointGroups[0]), QString("Curb"));
      dialog->grab().save(Issue893Shot("qt-893-properties-definition.png"));
      tabs->setCurrentIndex(2);
      auto* type = dialog->findChild<QComboBox*>("surfacePropsAnalysisType");
      type->setCurrentIndex(1);
      Q_EMIT type->activated(1);
      const SurfaceStyle* style = SurfaceStyles::Find(st.surfaceStyles, "Roads");
      QVERIFY(style && style->analysisMode == SurfaceAnalysisMode::Elevation);
      dialog->findChild<QSpinBox*>("surfacePropsRangeCount")->setValue(4);
      dialog->findChild<QPushButton*>("surfacePropsRun")->click();
      style = SurfaceStyles::Find(st.surfaceStyles, "Roads");
      QCOMPARE(style->bands.size(), size_t(4));
      QCOMPARE(dialog->findChild<QTableWidget*>("surfacePropsBands")->rowCount(), 4);
      dialog->grab().save(Issue893Shot("qt-893-properties-analysis.png"));
      tabs->setCurrentIndex(3);
      auto* stats = dialog->findChild<QTableWidget*>("surfacePropsStatistics");
      QVERIFY(stats->rowCount() > 5);  // EG is built from six points
      dialog->grab().save(Issue893Shot("qt-893-properties-statistics.png"));
      tabs->setCurrentIndex(0);
      dialog->grab().save(Issue893Shot("qt-893-properties-information.png"));
      dialog->findChild<QPushButton*>("surfacePropsCancel")->click();
    });
    window.ShowSurfaceProperties(0);
    QVERIFY(DoUndo(st, log));  // the Run step
    QCOMPARE(SurfaceStyles::Find(st.surfaceStyles, "Roads")->bands.size(), size_t(0));
    // A surface that was never built says so on the Statistics tab.
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      QVERIFY(dialog->findChild<QLabel*>("surfacePropsStatsNote")->text().startsWith("Not built"));
      dialog->reject();
    });
    window.ShowSurfaceProperties(1);
  }

  void surfaceStyleEditorEditsLiveCancelRestoresAndOkIsOneUndoStep() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    std::vector<std::string> log;
    QVERIFY(surfedit::AddStyle(st, 0, "Roads", log));
    st.cadSurfaces[0].styleName = "Roads";
    const auto roads = [&st]() -> SurfaceStyle& { return *SurfaceStyles::Find(st.surfaceStyles, "Roads"); };
    const double originalMinor = roads().minorIntervalFt;
    const bool originalTriangles = roads().triangles.visible;
    // Edits show in the drawing state as they are made; Cancel puts everything back.
    WhenDialogShows(window, [&] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      auto* list = dialog->findChild<QListWidget*>("surfaceStyleList");
      QCOMPARE(list->currentItem()->text(), QString("Roads"));
      dialog->findChild<QCheckBox*>("ssc_triangles_visible")->setChecked(!originalTriangles);
      QCOMPARE(roads().triangles.visible, !originalTriangles);
      // An invalid interval pair never reaches the style and disables OK, with REQ-070's message.
      auto* minor = dialog->findChild<QDoubleSpinBox*>("surfaceStyleMinorInterval");
      auto* major = dialog->findChild<QDoubleSpinBox*>("surfaceStyleMajorInterval");
      major->setValue(10.0);
      minor->setValue(3.0);
      QVERIFY(!dialog->findChild<QPushButton*>("surfaceStyleOk")->isEnabled());
      QVERIFY(dialog->findChild<QLabel*>("surfaceStyleIntervalProblem")->text().contains("whole multiple"));
      QCOMPARE(roads().minorIntervalFt, originalMinor);
      minor->setValue(5.0);
      QVERIFY(dialog->findChild<QPushButton*>("surfaceStyleOk")->isEnabled());
      QCOMPARE(roads().minorIntervalFt, 5.0);
      dialog->findChild<QPushButton*>("surfaceStyleCancel")->click();
    });
    window.ShowSurfaceStyles("Roads", false);
    QCOMPARE(roads().triangles.visible, originalTriangles);
    QCOMPARE(roads().minorIntervalFt, originalMinor);
    // OK keeps the whole session as ONE undo step.
    WhenDialogShows(window, [&] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      dialog->findChild<QCheckBox*>("ssc_triangles_visible")->setChecked(!originalTriangles);
      dialog->findChild<QDoubleSpinBox*>("surfaceStyleMinorInterval")->setValue(5.0);
      dialog->findChild<QSpinBox*>("surfaceStyleSmoothPasses")->setValue(2);
      dialog->findChild<QPushButton*>("surfaceStyleOk")->click();
    });
    window.ShowSurfaceStyles("Roads", false);
    QCOMPARE(roads().minorIntervalFt, 5.0);
    QCOMPARE(roads().contourSmoothPasses, 2);
    QVERIFY(DoUndo(st, log));
    QCOMPARE(roads().minorIntervalFt, originalMinor);
    QCOMPARE(roads().contourSmoothPasses, 0);
    QCOMPARE(roads().triangles.visible, originalTriangles);
  }

  void surfaceStyleEditorRenamesAddsDeletesAndProtectsStandard() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    std::vector<std::string> log;
    QVERIFY(surfedit::AddStyle(st, 0, "Roads", log));
    st.cadSurfaces[0].styleName = "Roads";
    WhenDialogShows(window, [&] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      auto* list = dialog->findChild<QListWidget*>("surfaceStyleList");
      auto* name = dialog->findChild<QLineEdit*>("surfaceStyleName");
      auto* del = dialog->findChild<QPushButton*>("surfaceStyleDelete");
      // Standard is selected first: it cannot be renamed or deleted.
      list->setCurrentRow(0);
      QVERIFY(!name->isEnabled());
      QVERIFY(!del->isEnabled());
      QVERIFY(del->toolTip().contains("Standard cannot be deleted"));
      // Rename re-points the surfaces that use the style; a taken name is refused with the reason.
      list->setCurrentRow(1);
      name->setText("Standard");
      Q_EMIT name->editingFinished();
      QVERIFY(dialog->findChild<QLabel*>("surfaceStyleStatus")->text().contains("already exists"));
      QCOMPARE(name->text(), QString("Roads"));
      name->setText("Paving");
      Q_EMIT name->editingFinished();
      QCOMPARE(QString::fromStdString(st.cadSurfaces[0].styleName), QString("Paving"));
      QCOMPARE(list->item(1)->text(), QString("Paving"));
      // New... copies the selected style; a duplicate name disables OK.
      WhenDialogShows(window, [&window] {
        auto* entry = window.findChild<QDialog*>("newSurfaceStyleDialog");
        QVERIFY(entry);
        auto* newName = entry->findChild<QLineEdit*>("newSurfaceStyleName");
        newName->setText("Standard");
        QVERIFY(!entry->findChild<QPushButton*>("newSurfaceStyleOk")->isEnabled());
        QVERIFY(!entry->findChild<QLabel*>("newSurfaceStyleDuplicate")->text().isEmpty());
        newName->setText("Utilities");
        entry->findChild<QPushButton*>("newSurfaceStyleOk")->click();
      });
      dialog->findChild<QPushButton*>("surfaceStyleNew")->click();
      QCOMPARE(list->count(), 3);
      QCOMPARE(list->currentItem()->text(), QString("Utilities"));
      dialog->grab().save(Issue893Shot("qt-893-style-information.png"));
      // Deleting a style that surfaces use keeps their name (REQ-070).
      list->setCurrentRow(1);
      del->click();
      QCOMPARE(list->count(), 2);
      QCOMPARE(QString::fromStdString(st.cadSurfaces[0].styleName), QString("Paving"));
      dialog->findChild<QPushButton*>("surfaceStyleOk")->click();
    });
    window.ShowSurfaceStyles("Roads", false);
    QCOMPARE(st.surfaceStyles.size(), size_t(2));
    QVERIFY(SurfaceStyles::Find(st.surfaceStyles, "Utilities"));
    QVERIFY(DoUndo(st, log));  // the whole session is one step
    QCOMPARE(st.surfaceStyles.size(), size_t(2));
    QVERIFY(SurfaceStyles::Find(st.surfaceStyles, "Roads"));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].styleName), QString("Roads"));
  }

  void surfaceStyleEditorBandsAndComponentPickersEditTheStyle() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    WhenDialogShows(window, [&] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      auto* tabs = dialog->findChild<QTabWidget*>("surfaceStyleTabs");
      QCOMPARE(tabs->count(), 8);
      auto& style = st.surfaceStyles[0];
      // Component pickers write colour, linetype and lineweight.
      auto* color = dialog->findChild<QComboBox*>("ssc_major_color");
      color->setCurrentIndex(color->findData("Red"));
      Q_EMIT color->activated(color->currentIndex());
      QCOMPARE(QString::fromStdString(style.majorContour.color), QString("Red"));
      auto* linetype = dialog->findChild<QComboBox*>("ssc_major_linetype");
      linetype->setCurrentIndex(linetype->findData("DASHED"));
      Q_EMIT linetype->activated(linetype->currentIndex());
      QCOMPARE(QString::fromStdString(style.majorContour.linetype), QString("DASHED"));
      // The Display tab mirrors the same value.
      QCOMPARE(dialog->findChild<QComboBox*>("ssc_display_major_color")->currentData().toString(), QString("Red"));
      // Analysis bands stay ascending after a bound is edited.
      tabs->setCurrentIndex(5);
      auto* type = dialog->findChild<QComboBox*>("surfaceStyleAnalysisType");
      type->setCurrentIndex(1);
      Q_EMIT type->activated(1);
      auto* add = dialog->findChild<QPushButton*>("surfaceStyleAddBand_bands");
      add->click();
      add->click();
      QCOMPARE(style.bands.size(), size_t(2));
      QCOMPARE(style.bands[1].upperBound, 1.0);
      auto* firstBound = dialog->findChild<QDoubleSpinBox*>("surfaceStyleBound_bands_0");
      firstBound->setValue(7.0);
      Q_EMIT firstBound->editingFinished();
      QCOMPARE(style.bands[0].upperBound, 1.0);  // re-sorted: 7 now sits above 1
      QCOMPARE(style.bands[1].upperBound, 7.0);
      dialog->findChild<QPushButton*>("surfaceStyleRemoveBand_bands_0")->click();
      QCOMPARE(style.bands.size(), size_t(1));
      QCOMPARE(style.bands[0].upperBound, 7.0);
      add->click();
      tabs->setCurrentIndex(1);
      dialog->grab().save(Issue893Shot("qt-893-style-contours.png"));
      tabs->setCurrentIndex(5);
      dialog->grab().save(Issue893Shot("qt-893-style-analysis.png"));
      dialog->findChild<QPushButton*>("surfaceStyleCancel")->click();
    });
    window.ShowSurfaceStyles("", false);
    QVERIFY(st.surfaceStyles[0].bands.empty());
    QCOMPARE(QString::fromStdString(st.surfaceStyles[0].majorContour.color), QString("Orange"));
  }

  void surfaceManagerEditsDefinitionRenamesAndCreatesAndDeletesSurfaces() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    window.ShowSurfaceManager();
    auto* manager = window.findChild<QDialog*>("surfaceManager");
    QVERIFY(manager);
    auto* tree = manager->findChild<QTreeWidget*>("surfaceManagerTree");
    QCOMPARE(tree->topLevelItemCount(), 2);
    const auto find = [&](int type, int surface, int index) -> QTreeWidgetItem* {
      for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toInt() == type && (*it)->data(0, Qt::UserRole + 1).toInt() == surface &&
            (*it)->data(0, Qt::UserRole + 2).toInt() == index)
          return *it;
      return nullptr;
    };
    constexpr int kGroupCategory = 2, kBreaklineCategory = 3, kBoundaryCategory = 4, kGroupItem = 6, kBoundaryItem = 8;
    // The seeded surface lists its point group.
    QVERIFY(find(kGroupCategory, 0, 0)->text(0).startsWith("Point Groups (1)"));
    // Add... on a category starts the matching action; adding a point group lists it.
    tree->setCurrentItem(find(kGroupCategory, 1, 0));
    auto* add = manager->findChild<QPushButton*>("surfaceManagerAdd");
    QVERIFY(add->isEnabled());
    WhenDialogShows(window, [&window] {
      auto* entry = window.findChild<QDialog*>("surfaceAddPointGroup");
      QVERIFY(entry);
      auto* groups = entry->findChild<QListWidget*>("surfaceAddPointGroupList");
      QCOMPARE(groups->count(), 2);
      Q_EMIT groups->itemClicked(groups->item(1));  // Curb
    });
    add->click();
    QCOMPARE(st.cadSurfaces[1].sourcePointGroups.size(), size_t(1));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[1].sourcePointGroups[0]), QString("Curb"));
    // Remove, with the node button enabled only on an item.
    auto* remove = manager->findChild<QPushButton*>("surfaceManagerRemove");
    tree->setCurrentItem(find(kGroupCategory, 1, 0));
    QVERIFY(!remove->isEnabled());
    tree->setCurrentItem(find(kGroupItem, 1, 0));
    QVERIFY(remove->isEnabled());
    remove->click();
    QVERIFY(st.cadSurfaces[1].sourcePointGroups.empty());
    // Boundaries keep an order the user can change (REQ-069). Let the rebuild the removal asked for
    // finish first: a rebuild drops boundaries whose entity no longer exists, and these are stand-ins.
    QTRY_VERIFY_WITH_TIMEOUT(st.surfaceRebuildAsync.empty() &&
                                SurfaceRebuildStateOf(st, 0) == SurfaceState::Current &&
                                SurfaceRebuildStateOf(st, 1) == SurfaceState::Current,
                            10000);
    st.cadSurfaces[1].boundaries.push_back({31, CadBoundaryKind::Outer, "first"});
    st.cadSurfaces[1].boundaries.push_back({32, CadBoundaryKind::Hide, "second"});
    // Wait for the refresh timer to publish both rows, regardless of runner load.
    QTRY_VERIFY_WITH_TIMEOUT(find(kBoundaryItem, 1, 0) && find(kBoundaryItem, 1, 1), 10000);
    tree->setCurrentItem(find(kBoundaryItem, 1, 0));
    auto* up = manager->findChild<QPushButton*>("surfaceManagerMoveUp");
    auto* down = manager->findChild<QPushButton*>("surfaceManagerMoveDown");
    QVERIFY(!up->isEnabled());
    QVERIFY(down->isEnabled());
    down->click();
    QCOMPARE(QString::fromStdString(st.cadSurfaces[1].boundaries[0].name), QString("second"));
    std::vector<std::string> log;
    QVERIFY(DoUndo(st, log));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[1].boundaries[0].name), QString("first"));
    // OK on Add Breakline only starts the pick command with the typed description.
    tree->setCurrentItem(find(kBreaklineCategory, 0, 0));
    WhenDialogShows(window, [&window] {
      auto* entry = window.findChild<QDialog*>("surfaceAddBreakline");
      QVERIFY(entry);
      entry->findChild<QLineEdit*>("surfaceAddText")->setText("Top of curb");
      entry->findChild<QPushButton*>("surfaceAddOk")->click();
    });
    add->click();
    QCOMPARE(QString::fromStdString(st.designateBreaklineDescription), QString("Top of curb"));
    QVERIFY(st.active != AppCommandState::Kind::None);
    CancelActiveCommand(st, window.m_Log);
    // The Add Boundaries dialog offers Outer / Hide / Show / Clip / Mask and Cancel starts nothing.
    tree->setCurrentItem(find(kBoundaryCategory, 0, 0));
    WhenDialogShows(window, [&window] {
      auto* entry = window.findChild<QDialog*>("surfaceAddBoundary");
      QVERIFY(entry);
      QCOMPARE(entry->findChild<QComboBox*>("surfaceAddType")->count(), 5);
      entry->findChild<QPushButton*>("surfaceAddCancel")->click();
    });
    add->click();
    QVERIFY(st.active == AppCommandState::Kind::None);
    // Rename: refusals leave the surface and say why; a free name applies.
    auto* name = manager->findChild<QLineEdit*>("surfaceManagerName");
    tree->setCurrentItem(tree->topLevelItem(0));
    name->setText("Proposed");
    Q_EMIT name->editingFinished();
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].name), QString("EG"));
    QVERIFY(manager->findChild<QLabel*>("surfaceManagerMessage")->text().contains("already exists"));
    QCOMPARE(name->text(), QString("EG"));
    name->setText("Existing ground");
    Q_EMIT name->editingFinished();
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].name), QString("Existing ground"));
    // New surface / Delete; a volume surface needs two TINs.
    manager->findChild<QPushButton*>("surfaceManagerNew")->click();
    QCOMPARE(st.cadSurfaces.size(), size_t(3));
    auto* newVolume = manager->findChild<QPushButton*>("surfaceManagerNewVolume");
    QVERIFY(newVolume->isEnabled());
    manager->findChild<QPushButton*>("surfaceManagerDelete")->click();
    QCOMPARE(st.cadSurfaces.size(), size_t(2));
    manager->findChild<QPushButton*>("surfaceManagerDelete")->click();
    manager->findChild<QPushButton*>("surfaceManagerDelete")->click();
    QVERIFY(st.cadSurfaces.empty());
    QVERIFY(!newVolume->isEnabled());
    QVERIFY(manager->findChild<QLabel*>("surfaceManagerEmpty")->isVisible());
    manager->close();
  }

  void surfaceManagerShowsStateStyleAndFileLinks() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    st.cadSurfaces[0].styleName = "Deleted style";  // REQ-070: falls back to Standard, and says so
    st.cadSurfaces[0].sourcePointFiles.push_back({"C:/data/points.csv", 1, true});
    window.ShowSurfaceManager();
    auto* manager = window.findChild<QDialog*>("surfaceManager");
    QVERIFY(manager);
    QTest::qWait(50);
    QVERIFY(manager->findChild<QLabel*>("surfaceManagerStyleNote")->text().contains("no longer exists"));
    QCOMPARE(manager->findChild<QComboBox*>("surfaceManagerStyle")->currentText(), QString("Standard"));
    QVERIFY(manager->findChild<QLabel*>("surfaceManagerBuilt")->text().contains("triangles"));
    auto* tree = manager->findChild<QTreeWidget*>("surfaceManagerTree");
    bool fileRow = false;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toInt() == 9) {  // FileItem
        fileRow = (*it)->text(0) == "C:/data/points.csv  (PENZD, header)";
        tree->setCurrentItem(*it);
      }
    QVERIFY(fileRow);
    QVERIFY(manager->findChild<QPushButton*>("surfaceManagerImport")->isEnabled());
    QCOMPARE(manager->findChild<QPushButton*>("surfaceManagerRemove")->text(), QString("Remove link"));
    manager->findChild<QPushButton*>("surfaceManagerRemove")->click();
    QVERIFY(st.cadSurfaces[0].sourcePointFiles.empty());
    manager->close();
  }

  void homeSurfacesOpensCreateSurfaceAndCancelLeavesDrawingUnchanged() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.show();
    auto* create = window.findChild<QAction*>("ribbon_CgdSurfaces");
    QVERIFY(create);
    QVERIFY(create->isEnabled());
    const auto count = window.State().cadSurfaces.size();
    const bool couldUndo = CanUndo(window.State());
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      QCOMPARE(dialog->windowTitle(), QString("Create Surface"));
      dialog->findChild<QLineEdit*>("createSurfaceName")->setText("Cancelled surface");
      dialog->findChild<QPushButton*>("createSurfaceCancel")->click();
    });
    create->trigger();
    QCOMPARE(window.State().cadSurfaces.size(), count);
    QCOMPARE(CanUndo(window.State()), couldUndo);
  }

  void newlyCreatedSurfaceDisplaysWithoutAnotherCommand() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.resize(1280, 800);
    window.show();
    auto& st = window.State();
    Issue893Fixture(st, false);
    auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport");
    QVERIFY(viewport);
    window.Submit("ZOOMEXTENTS");
    QTest::qWait(100);
    QVERIFY(viewport->grabFramebuffer().save(Issue893Shot("qt-1080-before-create.png")));

    window.Submit("SURFACECREATE First surface, Ground");
    QCOMPARE(st.cadSurfaces.size(), size_t(1));
    QVERIFY(st.cadSurfaces[0].tin);
    // Paint without another command/undo snapshot: that used to leave id zero forever.
    const QImage created = viewport->grabFramebuffer();
    QVERIFY(created.save(Issue893Shot("qt-1080-created.png")));
    QCOMPARE(st.cadSurfaceAttrs.size(), size_t(1));
    QVERIFY(st.cadSurfaceAttrs[0].id != 0);
    QVERIFY(!st.surfaceDisplayGeometry.empty());
    QCOMPARE(st.surfaceDisplayCache.size(), size_t(1));
    const auto id = st.cadSurfaceAttrs[0].id;
    const auto nextId = st.nextEntityId;
    const auto regenerations = st.surfaceDisplayRegenCount;
    for (int i = 0; i < 3; ++i) {
      QVERIFY(!viewport->grabFramebuffer().isNull());
      QCOMPARE(st.cadSurfaceAttrs[0].id, id);
      QCOMPARE(st.nextEntityId, nextId);
      QCOMPARE(st.surfaceDisplayRegenCount, regenerations);
    }
  }

  void homeSurfacesCreatedSurfaceSurvivesSaveAndReopen() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.show();
    auto* create = window.findChild<QAction*>("ribbon_CgdSurfaces");
    QVERIFY(create);
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("createSurfaceName")->setText("Home Pond");
      dialog->findChild<QLineEdit*>("createSurfaceDescription")->setText("From Home ribbon");
      dialog->findChild<QPushButton*>("createSurfaceOk")->click();
    });
    create->trigger();
    auto& st = window.State();
    QCOMPARE(st.cadSurfaces.size(), size_t(1));
    QCOMPARE(QString::fromStdString(st.cadSurfaces[0].name), QString("Home Pond"));
    const QByteArray path = directory.filePath("home-surface.dwg").toUtf8();
    std::vector<std::string> log;
    QVERIFY(SaveDrawingDocument(st, path.constData(), log));
    AppCommandState reopened;
    QVERIFY(OpenDrawingDocument(reopened, path.constData(), log));
    QCOMPARE(reopened.cadSurfaces.size(), size_t(1));
    QCOMPARE(QString::fromStdString(reopened.cadSurfaces[0].name), QString("Home Pond"));
    QCOMPARE(QString::fromStdString(reopened.cadSurfaces[0].description), QString("From Home ribbon"));
  }

  void surfaceRibbonButtonsAreEnabledAndOpenTheNativeDialogs() {
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    window.show();
    auto& st = window.State();
    Issue893Fixture(st);
    // Create Surface is live on both Survey ribbons (it was a disabled placeholder before #893).
    for (const char* id : {"ribbon_SvyCreateSurf", "ribbon_SpCreateSurf"}) {
      auto* create = window.findChild<QAction*>(id);
      QVERIFY2(create, id);
      QVERIFY(create->isEnabled());
      QVERIFY(!create->property("nativeDialogPending").toBool());
    }
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->reject();
    });
    window.findChild<QAction*>("ribbon_SvyCreateSurf")->trigger();
    // A selected surface brings the contextual tab with live Surface Properties.
    st.selection = {{SelectedEntity::Type::Surface, 1}};
    window.Refresh();
    auto* properties = window.findChild<QAction*>("ribbon_TsSurfProps");
    QVERIFY(properties);
    QVERIFY(properties->isEnabled());
    // The Properties dock shows the selected surface read-only, as 0.7.4 did.
    auto* surfaceName = window.findChild<QLabel*>("propSurfaceName");
    QVERIFY(surfaceName);
    QCOMPARE(surfaceName->text(), QString("Proposed"));
    QVERIFY(window.findChild<QLabel*>("propertiesSummary")->text().contains("TIN Surface"));
    QVERIFY(window.findChild<QLabel*>("propSurfaceNote")->text().contains("Surfaces manager"));
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      QVERIFY(dialog->windowTitle().contains("Proposed"));
      dialog->reject();
    });
    properties->trigger();
  }

  void surfaceDialogsRenderFinalScreenshots() {
    EditorWindow window;
    auto& st = window.State();
    Issue893Fixture(st);
    std::vector<std::string> log;
    QVERIFY(surfedit::AddStyle(st, 0, "Roads", log));
    st.cadSurfaces[0].sourcePointGroups = {"Ground", "Curb"};
    st.userLinesFlat = {0, 0, 100, 10, 10, 104};  // a real line, so the breakline survives the rebuild
    EnsureAttrCounts(st);
    EnsureEntityIds(st);
    st.cadSurfaces[0].breaklines.push_back({st.userLineAttrs[0].id, "Top of curb"});
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->findChild<QLineEdit*>("createSurfaceName")->setText("EG");
      dialog->findChild<QPushButton*>("createSurfaceOk")->click();  // refused: the name is taken
      dialog->grab().save(Issue893Shot("qt-893-final-create-error.png"));
      dialog->reject();
    });
    window.ShowCreateSurface();
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("createSurfaceDialog");
      QVERIFY(dialog);
      dialog->grab().save(Issue893Shot("qt-893-final-create.png"));
      dialog->reject();
    });
    window.ShowCreateSurface();
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfacePropertiesDialog");
      QVERIFY(dialog);
      auto* tabs = dialog->findChild<QTabWidget*>("surfacePropertiesTabs");
      for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        dialog->grab().save(Issue893Shot(QString("qt-893-final-properties-%1.png").arg(i).toUtf8().constData()));
      }
      dialog->reject();
    });
    window.ShowSurfaceProperties(0);
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("surfaceStyleDialog");
      QVERIFY(dialog);
      auto* tabs = dialog->findChild<QTabWidget*>("surfaceStyleTabs");
      for (int i : {0, 1, 5}) {
        tabs->setCurrentIndex(i);
        dialog->grab().save(Issue893Shot(QString("qt-893-final-style-%1.png").arg(i).toUtf8().constData()));
      }
      dialog->reject();
    });
    window.ShowSurfaceStyles("Standard", false);
    window.ShowSurfaceManager();
    auto* manager = window.findChild<QDialog*>("surfaceManager");
    QVERIFY(manager);
    manager->resize(960, 620);
    QTest::qWait(900);  // let both surfaces finish their background rebuild
    manager->grab().save(Issue893Shot("qt-893-final-manager.png"));
    manager->close();
  }
  void issue909PdfUnderlayAttachPreviewCancelFailureAndUndo() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString pdf = Issue905WritePdf(directory, "underlay.pdf", Issue906Pdf());
    QVERIFY(!pdf.isEmpty());
    EditorWindow window;
    window.CreateDrawingFromTemplate();
    auto& state = window.State();
    state.pdfAttachSpecifyInsert = false;
    state.pdfAttachSpecifyScale = false;
    state.pdfAttachSpecifyRot = false;
    StartPdfAttachCommand(state, window.m_Log);
    WhenDialogShows(window, [&window, &pdf] {
      auto* dialog = window.findChild<QDialog*>("pdfAttachDialog");
      QVERIFY(dialog);
      auto* path = dialog->findChild<QLineEdit*>("pdfAttachPath");
      path->setText(pdf);
      QVERIFY(QMetaObject::invokeMethod(path, "editingFinished", Qt::DirectConnection));
      QCOMPARE(dialog->findChild<QSpinBox*>("pdfAttachPage")->maximum(), 1);
      QVERIFY(!dialog->findChild<QLabel*>("pdfAttachPreview")->pixmap().isNull());
      dialog->grab().save(Issue907Shot("qt-909-option-a-app-theme.png"));
      dialog->setStyleSheet("QDialog#pdfAttachDialog { background:#203247; color:#f4f7fb; }"
                            "QLabel,QCheckBox { color:#f4f7fb; }"
                            "QLineEdit,QSpinBox,QDoubleSpinBox { background:#2c4058; color:#f4f7fb; border:1px solid #526b86; padding:4px; }"
                            "QPushButton#pdfAttachConfirm { background:#35a9bd; color:#10232d; font-weight:700; padding:7px 16px; border:0; border-radius:4px; }");
      dialog->grab().save(Issue907Shot("qt-909-option-b-deep-navy.png"));
      dialog->setStyleSheet("QDialog#pdfAttachDialog { background:#303940; color:#f4f3ef; }"
                            "QLineEdit,QSpinBox,QDoubleSpinBox { background:#3c464f; color:#f4f3ef; border:1px solid #64717b; padding:4px; }"
                            "QPushButton#pdfAttachConfirm { background:#e6a64b; color:#202b35; font-weight:700; padding:7px 16px; border:0; border-radius:4px; }");
      dialog->grab().save(Issue907Shot("qt-909-option-c-graphite-amber.png"));
      dialog->setStyleSheet({});
      QVERIFY(dialog->grab().save(Issue907Shot("qt-909-final-graphite-attach-dialog.png")));
      dialog->findChild<QPushButton*>("pdfAttachConfirm")->click();
    });
    window.ShowPdfAttachDialog();
    QCOMPARE(state.pdfAttachments.size(), std::size_t(1));
    QCOMPARE(state.pdfAttachments.front().pageIndex, 0);
    QVERIFY(state.pdfAttachments.front().glTexId != 0);
    window.Submit("UNDO");
    QVERIFY(state.pdfAttachments.empty());

    state.pdfAttachSpecifyInsert = true;
    StartPdfAttachCommand(state, window.m_Log);
    WhenDialogShows(window, [&window, &pdf] {
      auto* dialog = window.findChild<QDialog*>("pdfAttachDialog");
      QVERIFY(dialog);
      auto* path = dialog->findChild<QLineEdit*>("pdfAttachPath");
      path->setText(pdf);
      QVERIFY(QMetaObject::invokeMethod(path, "editingFinished", Qt::DirectConnection));
      dialog->findChild<QPushButton*>("pdfAttachConfirm")->click();
    });
    window.ShowPdfAttachDialog();
    QCOMPARE(state.active, AppCommandState::Kind::PdfAttach);
    QCOMPARE(state.pdfAttachPhase, AppCommandState::PdfAttachPhase::WaitInsertPoint);
    QVERIFY(state.pdfAttachPreviewReady);
    SubmitPdfAttachInsertPoint(state, 12.f, 34.f, window.m_Log);
    QCOMPARE(state.pdfAttachments.size(), std::size_t(1));
    QCOMPARE(state.pdfAttachments.front().insertX, 12.f);
    QCOMPARE(state.pdfAttachments.front().insertY, 34.f);
    window.Submit("UNDO");
    QVERIFY(state.pdfAttachments.empty());

    // Cancel leaves the drawing untouched and ends the command cleanly.
    StartPdfAttachCommand(state, window.m_Log);
    WhenDialogShows(window, [&window] {
      auto* dialog = window.findChild<QDialog*>("pdfAttachDialog");
      QVERIFY(dialog);
      dialog->findChild<QPushButton*>("pdfAttachCancel")->click();
    });
    window.ShowPdfAttachDialog();
    QVERIFY(state.pdfAttachments.empty());
    QCOMPARE(state.active, AppCommandState::Kind::None);

    // A corrupt file gives an actionable status and can be cancelled without mutation.
    const QString corrupt = Issue905WritePdf(directory, "broken.pdf", "not a PDF");
    StartPdfAttachCommand(state, window.m_Log);
    WhenDialogShows(window, [&window, &corrupt] {
      auto* dialog = window.findChild<QDialog*>("pdfAttachDialog");
      QVERIFY(dialog);
      auto* path = dialog->findChild<QLineEdit*>("pdfAttachPath");
      path->setText(corrupt);
      QVERIFY(QMetaObject::invokeMethod(path, "editingFinished", Qt::DirectConnection));
      QVERIFY(dialog->findChild<QLabel*>("pdfAttachStatus")->text().contains("Unable to open PDF"));
      dialog->findChild<QPushButton*>("pdfAttachCancel")->click();
    });
    window.ShowPdfAttachDialog();
    QVERIFY(state.pdfAttachments.empty());
    QCOMPARE(state.active, AppCommandState::Kind::None);
  }

  void issue909PdfUnderlayProjectCopyLinkAndCancelContinueCorrectly() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    gsproj::Project created; std::string error;
    QVERIFY2(gsproj::Create(directory.path().toStdWString(), "Underlay Job", &created, &error), error.c_str());
    QDir().mkpath(directory.filePath("outside"));
    const QString source = Issue905WritePdf(directory, "outside/plan.pdf", Issue906Pdf());
    QVERIFY(!source.isEmpty());
    EditorWindow window;
    QVERIFY(window.OpenProject(QString::fromStdWString(created.file.wstring()), true) != 0);
    auto& state = window.State();
    auto& session = *std::find_if(state.openProjects.begin(), state.openProjects.end(),
                                  [&](const auto& item) { return item.uid == state.drawingTabs[state.activeDrawingIdx].projectUid; });
    const QString projectFolder = QString::fromStdWString(session.project.Folder().wstring());
    state.pdfAttachSpecifyInsert = false;
    state.pdfAttachSpecifyScale = false;
    state.pdfAttachSpecifyRot = false;
    const auto noPrompt = AppCommandState::ProjectAttachPrompt::Kind::None;

    auto chooseAndContinue = [&](const char* projectButton, bool attachPdf) {
      AnswerAttachQuestion(window, projectButton);
      auto* poll = new QTimer(&window);
      poll->setInterval(10);
      QObject::connect(poll, &QTimer::timeout, poll, [&] {
        if (auto* dialog = window.findChild<QDialog*>("pdfAttachDialog")) {
          poll->stop(); poll->deleteLater();
          if (attachPdf) dialog->findChild<QPushButton*>("pdfAttachConfirm")->click();
          else dialog->findChild<QPushButton*>("pdfAttachCancel")->click();
          return;
        }
        if (state.active == AppCommandState::Kind::None && !state.pdfAttachDialogOpen) {
          poll->stop(); poll->deleteLater();
        }
      });
      poll->start();
    };
    auto request = [&] {
      StartPdfAttachCommand(state, window.m_Log);
      QVERIFY(RequestProjectAttach(state, AppCommandState::ProjectAttachPrompt::Kind::Pdf,
                                   source.toUtf8().toStdString().c_str(), window.m_Log));
    };

    request();
    chooseAndContinue("projectAttachCopy", true);
    window.ShowProjectAttachPrompt();
    QCOMPARE(state.projectAttachPrompt.kind, noPrompt);
    QCOMPARE(state.pdfAttachments.size(), std::size_t(1));
    QVERIFY(QFileInfo::exists(projectFolder + "/PDFs/plan.pdf"));
    window.Submit("UNDO");
    QVERIFY(state.pdfAttachments.empty());

    request();
    chooseAndContinue("projectAttachLink", false);
    window.ShowProjectAttachPrompt();
    QCOMPARE(state.projectAttachPrompt.kind, noPrompt);
    QVERIFY(state.pdfAttachments.empty());
    QCOMPARE(QString::fromUtf8(state.pdfAttachFilePath), source);
    QVERIFY(QFileInfo::exists(source));

    request();
    chooseAndContinue("projectAttachCancel", false);
    window.ShowProjectAttachPrompt();
    QCOMPARE(state.projectAttachPrompt.kind, noPrompt);
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(state.pdfAttachments.empty());

    session.readOnly = true;
    StartPdfAttachCommand(state, window.m_Log);
    AnswerNextDialog(QMessageBox::Ok);
    window.ShowPdfAttachDialog();
    QCOMPARE(state.active, AppCommandState::Kind::None);
    QVERIFY(state.pdfAttachments.empty());
    session.readOnly = false;
  }

  // REQ-436 / D-2026-10-21-f (issue #1031): the primary accent in Qt chrome is the topo-line amber in both
  // appearances, readable, with a neutral secondary role, and no aqua / Windows-blue accent left behind.
  void issue1031AmberPrimaryAccentInBothAppearances() {
    const auto luminance = [](const QColor& c) {
      const auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
      return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
    };
    const auto contrast = [&](const QColor& a, const QColor& b) {
      const double la = luminance(a), lb = luminance(b);
      return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    const QColor white(Qt::white);
    for (const bool dark : {true, false}) {
      const QtAccent& accent = QtAccentFor(dark);
      const QColor surface = dark ? QColor("#22272b") : QColor("#f0f0f0");
      // Hue is the topo-line amber (RGB 185,100,12 is about 30 degrees) in both appearances.
      for (const char* name : {accent.strong, accent.fill, accent.fillHover, accent.link}) {
        const double hue = QColor(name).hueF() * 360.0;
        QVERIFY2(hue > 24.0 && hue < 38.0, name);
      }
      // Accent indicators and text are readable on the surface; filled controls carry white text.
      QVERIFY2(contrast(QColor(accent.strong), surface) >= 3.0, dark ? "dark strong" : "light strong");
      QVERIFY2(contrast(QColor(accent.link), surface) >= 4.5, dark ? "dark link" : "light link");
      QVERIFY2(contrast(white, QColor(accent.fill)) >= 4.5, dark ? "dark fill" : "light fill");
      QVERIFY2(contrast(white, QColor(accent.fillHover)) >= 4.5, dark ? "dark fillHover" : "light fillHover");
      // Interactive states are distinct from each other.
      QVERIFY(QColor(accent.fill) != QColor(accent.fillHover));
      QVERIFY(QColor(accent.fill) != QColor(accent.tint));
      QVERIFY(QColor(accent.tint) != QColor(accent.hoverTint));
      // Secondary emphasis is a calm neutral: clearly less saturated than, and different from, the primary accent.
      QVERIFY(QColor(accent.quiet).saturationF() < 0.25);
      QVERIFY(QColor(accent.quiet) != QColor(accent.strong));
      QVERIFY(contrast(QColor(accent.quiet), surface) >= 4.5);
      // Warnings stay readable and are not the primary accent.
      QVERIFY(QColor(accent.warn) != QColor(accent.strong));
      QVERIFY(contrast(QColor(accent.warn), surface) >= 4.5);
    }
    QVERIFY(QtApplyAccent("@strong@ @fill@ @fillHover@ @tint@ @hoverTint@ @edge@ @link@ @linkHover@ @quiet@ @warn@", true)
                .count('@') == 0);

    const auto oldPalette = qApp->palette(); const auto oldFont = qApp->font();
    const auto oldSheet = qApp->styleSheet(); const auto oldStyle = qApp->style()->objectName();
    const auto oldDefault = qApp->property("defaultQtStyle"), oldLight = qApp->property("cadLightTheme");
    auto restore = qScopeGuard([&] {
      qApp->setStyle(oldStyle); qApp->setPalette(oldPalette); qApp->setFont(oldFont); qApp->setStyleSheet(oldSheet);
      qApp->setProperty("defaultQtStyle", oldDefault); qApp->setProperty("cadLightTheme", oldLight);
      QSettings().remove("editor/lightTheme");
    });
    IsolatedUserData userData;
    const char* oldAccents[] = {"#55d8d0", "#168e8a", "#1ca6a0", "#244846", "#0078d7", "#0066cc", "#438fda", "#347cc3", "#cee1f4", "#dceafb"};
    for (const bool light : {false, true}) {
      const QString tag = light ? "light" : "dark";
      ApplyQtAppearance(*qApp, light);
      const QtAccent& accent = QtAccentFor(!light);
      QCOMPARE(qApp->palette().color(QPalette::Highlight), QColor(accent.fill));
      QCOMPARE(qApp->palette().color(QPalette::Link), QColor(accent.link));
      const QString sheet = qApp->styleSheet();
      QVERIFY(sheet.contains(accent.strong, Qt::CaseInsensitive));
      QVERIFY(sheet.contains(accent.tint, Qt::CaseInsensitive));
      QVERIFY2(!sheet.contains('@'), "unreplaced accent token");
      for (const char* old : oldAccents) QVERIFY2(!sheet.contains(old, Qt::CaseInsensitive), old);

      EditorWindow window;
      window.CreateDrawingFromTemplate();
      window.show();
      QVERIFY(window.grab().save(Issue886Shot(("qt-1031-" + tag + "-drawing.png").toUtf8().constData())));
      window.ShowSettings();
      auto* dialog = window.findChild<QDialog*>("applicationSettings");
      QVERIFY(dialog);
      dialog->resize(780, 600);
      QVERIFY(dialog->grab().save(Issue886Shot(("qt-1031-" + tag + "-settings.png").toUtf8().constData())));
      for (auto* button : dialog->findChildren<QPushButton*>())
        for (const char* old : oldAccents) QVERIFY2(!button->styleSheet().contains(old, Qt::CaseInsensitive), old);
    }
    {
      EditorWindow startWindow;
      QVERIFY(startWindow.grab().save(Issue886Shot("qt-1031-light-start.png")));
      ApplyQtAppearance(*qApp, false);
      EditorWindow darkStart;
      QVERIFY(darkStart.grab().save(Issue886Shot("qt-1031-dark-start.png")));
    }
  }
};

int main(int argc, char** argv) {
  int minidumpWriterExitCode = 1;
  if (crashReporter::RunMinidumpWriterMode(minidumpWriterExitCode))
    return minidumpWriterExitCode;

  QSurfaceFormat format;
  format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24); format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);
  // GUI tests run on a hidden desktop and inspect dialogs as Qt widgets. Avoid
  // platform-native dialogs, which are not reliably discoverable there.
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  app.setOrganizationName("GoSurveyTests");
  const QString shard = qEnvironmentVariable("GOSURVEY_QT_TEST_SHARD");
  app.setApplicationName(shard.isEmpty() ? "QtEditorTests" : "QtEditorTests-" + shard);
  QtEditorTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "QtEditorTests.moc"
