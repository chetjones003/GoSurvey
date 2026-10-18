#include "AdjustmentWindow.hpp"
#include "EditorWindow.hpp"
#include "io/DwgIo.hpp"
#include "Version.hpp"
#include "EditorViewport.hpp"
#include "EditorChrome.hpp"
#include "QtAppearance.hpp"
#include "RibbonCatalog.hpp"
#include "TraverseActions.hpp"
#include "traverse/TraverseIo.hpp"
#include "PdfAttach.hpp"
#include "PdfWindow.hpp"
#include "QtAnnotations.hpp"
#include "ProjectPoints.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPack.hpp"
#include "ProjectTurnover.hpp"
#include "ProjectSettings.hpp"
#include "SurveyCsv.hpp"
#include "io/ProjectPointDb.hpp"
#include "io/Project.hpp"
#include "ProjectSettings.hpp"
#include "GsMigrate.hpp"
#include "GsIo.hpp"
#include "AppPaths.hpp"
#include "DwgIo.hpp"
#include "ProjectAddFlow.hpp"
#include "ProjectPoints.hpp"
#include "UserPrefs.hpp"
#include "platform/HttpFetch.hpp"
#include "update/UpdateService.hpp"
#include "CrashReporter.hpp"
#include "FailureReport.hpp"
#include "FailureReportUi.hpp"
#include "StartupFailure.hpp"
#include "CadSurfaceEdit.hpp"
#include "SurfaceStyle.hpp"
#include <QPdfWriter>
#include <QPainter>
#include <QtTest/QtTest>
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QPalette>
#include <QMessageBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QLineEdit>
#include <QInputDialog>
#include <QLabel>
#include <QCompleter>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QPushButton>
#include <QListWidget>
#include <QProgressBar>
#include <QTextBrowser>
#include <QToolButton>
#include <QMenu>
#include <QGroupBox>
#include <QRadioButton>
#include <QDockWidget>
#include <QFrame>
#include <QSettings>
#include <QTreeWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QDockWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QClipboard>
#include <QScopeGuard>
#include <QStyle>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>

namespace {
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
QAction* Action(QWidget& widget, const QString& text) {
  for (auto* action : widget.findChildren<QAction*>()) if (action->text() == text) return action;
  return nullptr;
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
QString Issue886Shot(const char* name) {
  const QString directory = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR);
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
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
QString Issue893Shot(const char* name) {
  const QString directory = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR);
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
}
QString Issue897Shot(const char* name) {
  const QString directory = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR);
  QDir().mkpath(directory);
  return QDir(directory).filePath(QString::fromLatin1(name));
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

class QtEditorTests final : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void initTestCase() { QVERIFY(PdfAttach_Init()); }
  void cleanupTestCase() { PdfAttach_Shutdown(); }

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
    if (window.State().paperLayouts.empty()) AddPaperLayout(window.State());
    window.State().activeSpaceIndex = 0;
    const int viewport = AddViewport(window.State(), 0);
    const float initial = window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn;
    window.Submit("VPSCALE 1:25");
    QVERIFY(std::fabs(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn - 25.f) < 1e-4f);
    window.Submit("VPSCALE not-a-scale");
    QVERIFY(std::fabs(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn - 25.f) < 1e-4f);
    QVERIFY(DoUndo(window.State(), window.m_Log));
    QCOMPARE(window.State().paperLayouts[0].viewports[static_cast<size_t>(viewport)].scaleModelPerPaperIn, initial);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("viewportsManager"); QVERIFY(dialog);
      QVERIFY(dialog->grab().save(Issue897Shot("qt-897-viewports-manager.png")));
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
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-884-dwg-loss-r2018.png")));
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
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-884-dwg-overwrite-r2000.png")));
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

  void settingsAccountSupportsLaterSignInAndReadOnlyStatus() {
    EditorWindow window;
    auto *viewMenu = window.findChild<QMenu *>("viewMenu");
    QVERIFY(viewMenu);
    QAction *settings = nullptr;
    for (auto *action : viewMenu->actions())
      if (action->text() == "Settings…")
        settings = action;
    QVERIFY(settings);
    settings->trigger();
    auto *dialog = window.findChild<QDialog *>("applicationSettings");
    QVERIFY(dialog);
    auto *heading = dialog->findChild<QLabel *>("accountSettingsHeading");
    QVERIFY(heading);
    QCOMPARE(heading->text(), QString("Account"));
    QVERIFY(dialog->findChild<QFrame *>("accountIdentityCard"));
    QVERIFY(dialog->findChildren<QLineEdit *>().isEmpty());
    auto *action = dialog->findChild<QPushButton *>("accountSignInAction");
    auto *status = dialog->findChild<QLabel *>("accountSignInStatus");
    QVERIFY(action);
    QVERIFY(status);
    QTRY_COMPARE(action->text(), QString("Sign In"));
    QTest::mouseClick(action, Qt::LeftButton);
    QVERIFY(window.State().authSignInRequested);
    window.State().authSignInRequested = false;
    window.State().authSignedIn = true;
    window.State().authEmail = "surveyor@example.com";
    QTRY_COMPARE(status->text(), QString("Signed in as surveyor@example.com"));
    QTRY_COMPARE(action->text(), QString("Sign Out"));
    const auto accountShot = dialog->grab();
    QVERIFY(accountShot.save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) +
                             "/qt-880-account-final.png"));
    QTest::mouseClick(action, Qt::LeftButton);
    QVERIFY(window.State().authSignOutRequested);
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
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-a-health.png");
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-final-health.png");
      dialog->reject();
    });
    window.ShowProjectHealth(uid);
    QVERIFY(healthShown);

    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectHealthDialog"); if (!dialog) return;
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-b-health.png");
      dialog->reject();
    });
    window.ShowProjectHealth(uid);
    ApplyQtAppearance(*qApp, false);

    bool packShown = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectPackDialog"); if (!dialog) return;
      packShown = dialog->findChild<QLabel*>("projectPackSummary")->text().contains("file");
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-a-pack.png");
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-final-pack.png");
      dialog->reject();
    });
    window.ShowProjectPack(uid);
    QVERIFY(packShown);
    QVERIFY(!std::filesystem::exists(created.Folder() / "Project Workflows.gspack"));

    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectPackDialog"); if (!dialog) return;
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-b-pack.png");
      dialog->reject();
    });
    window.ShowProjectPack(uid);
    ApplyQtAppearance(*qApp, false);

    bool turnoverShown = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectTurnoverDialog"); if (!dialog) return;
      turnoverShown = dialog->findChild<QLineEdit*>("projectTurnoverRecipient") != nullptr;
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-a-turnover.png");
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-final-turnover.png");
      dialog->reject();
    });
    window.ShowProjectTurnover(uid);
    QVERIFY(turnoverShown);
    QVERIFY(projturn::List(created, nullptr).empty());

    ApplyQtAppearance(*qApp, true);
    QTimer::singleShot(0, [&] {
      auto* dialog = window.findChild<QDialog*>("projectTurnoverDialog"); if (!dialog) return;
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-b-turnover.png");
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
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-a-refresh.png");
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-final-refresh.png");
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
      dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-889-option-b-refresh.png");
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

  void startupGateUsesApplicationLogoAndRequiresAChoice() {
    EditorWindow window;
    window.State().authGateResolved = false;
    window.show();
    window.ShowSignInGate();
    auto *gate = window.findChild<QDialog *>("signInGate");
    QVERIFY(gate);
    QTRY_VERIFY(gate->isVisible());
    QCOMPARE(gate->windowModality(), Qt::ApplicationModal);
    QVERIFY(!(gate->windowFlags() & Qt::WindowCloseButtonHint));
    auto *logo = gate->findChild<QLabel *>("signInLogo");
    QVERIFY(logo);
    QVERIFY(!logo->pixmap(Qt::ReturnByValue).isNull());
    const auto startupShot = gate->grab();
    QVERIFY(startupShot.save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) +
                             "/qt-880-startup-final.png"));
    gate->close();
    QVERIFY(gate->isVisible());
    QTest::keyClick(gate, Qt::Key_Escape);
    QVERIFY(gate->isVisible());
    QTest::mouseClick(gate->findChild<QPushButton *>("signInButton"),
                      Qt::LeftButton);
    QVERIFY(window.State().authSignInRequested);
    QTest::mouseClick(gate->findChild<QPushButton *>("declineSignInButton"),
                      Qt::LeftButton);
    QVERIFY(window.State().authDeclineRequested);
  }

  void savedOptOutResolvesQtLaunchWithoutAuthAttempt() {
    EditorWindow window;
    window.State().authOptedOut = true;
    window.StartAuth();
    QVERIFY(window.State().authGateResolved);
    QVERIFY(!window.State().authBusy);
    QCoreApplication::processEvents();
    QVERIFY(!window.findChild<QDialog *>("signInGate"));
  }

  // ---- REQ-080 usage ping / REQ-425 crash and startup-failure reports (issue #883) -----------

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

  /// Clicks OK on the next message box and hands back what it said.
  static void CaptureNextMessage(QString& text) {
    QTimer::singleShot(0, [&text] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = qobject_cast<QMessageBox*>(widget)) {
          text = message->text();
          message->button(QMessageBox::Ok)->click();
        }
    });
  }

  void telemetryPingWaitsForLaunchGateAndFiresOnce() {
    EditorWindow window;
    std::vector<std::string> sent;
    window.m_SendTelemetry = [&sent](const std::string& email) { sent.push_back(email); };
    window.ServiceTelemetry();
    QVERIFY2(sent.empty(), "no ping before the launch sequence starts");
    window.m_AuthStarted = true;
    window.State().authGateResolved = false;
    window.ServiceTelemetry();
    QVERIFY2(sent.empty(), "no ping while the sign-in gate is unresolved");
    window.State().authGateResolved = true;
    window.ServiceTelemetry();
    window.ServiceTelemetry();
    QCOMPARE(sent.size(), std::size_t(1));
    QCOMPARE(sent[0], std::string());  // signed out: no email

    EditorWindow signedIn;
    std::vector<std::string> signedInSent;
    signedIn.m_SendTelemetry = [&signedInSent](const std::string& email) { signedInSent.push_back(email); };
    signedIn.m_AuthStarted = true;
    signedIn.State().authGateResolved = true;
    signedIn.State().authSignedIn = true;
    signedIn.State().authEmail = "surveyor@example.com";
    signedIn.ServiceTelemetry();
    QCOMPARE(signedInSent, std::vector<std::string>{"surveyor@example.com"});
  }

  void telemetryPingFollowsTheRealLaunchPathOnlyWhenEnabled() {
    // The saved opt-out resolves the gate at once; the window's own service loop sends the ping.
    EditorWindow window;
    std::vector<std::string> sent;
    window.m_SendTelemetry = [&sent](const std::string& email) { sent.push_back(email); };
    window.State().authOptedOut = true;
    window.StartAuth();
    QTRY_COMPARE(sent.size(), std::size_t(1));
    QTest::qWait(150);
    QCOMPARE(sent.size(), std::size_t(1));

    // A window the entry point did not enable never sends one (so tests stay off the network).
    EditorWindow quiet;
    quiet.State().authOptedOut = true;
    quiet.StartAuth();
    QTest::qWait(150);
    QVERIFY(!quiet.m_SendTelemetry);
    QVERIFY(!quiet.m_TelemetryTask);
  }

  void settingsUsageDisclosureIsAccurateAndFollowsSignIn() {
    EditorWindow window;
    window.ShowSettings();
    auto* dialog = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(dialog);
    auto* heading = dialog->findChild<QLabel*>("usageDataHeading");
    auto* summary = dialog->findChild<QLabel*>("usageDataSummary");
    auto* email = dialog->findChild<QLabel*>("usageDataEmail");
    auto* crash = dialog->findChild<QLabel*>("usageDataCrashReports");
    QVERIFY(heading && summary && email && crash);
    // REQ-080 as amended: a ping every launch. The 0.7.4 "at most once a day" is no longer true.
    QVERIFY(summary->text().contains("each time you start GoSurvey"));
    QVERIFY(!summary->text().contains("once a day"));
    // A disclosure, not a control: ADR-032 has no opt-out.
    QVERIFY(!dialog->findChild<QWidget*>("usageDataSection")->findChild<QCheckBox*>());
    QVERIFY(email->text().contains("currently signed out"));
    // REQ-425 §6.
    QVERIFY(crash->text().contains("crashes or cannot start"));
    QVERIFY(crash->text().contains("file locations"));
    window.State().authSignedIn = true;
    window.State().authEmail = "surveyor@example.com";
    QTRY_VERIFY(email->text().contains("your email is included too"));
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
    auto* text = dialog->findChild<QPlainTextEdit*>("failureReportText");
    QVERIFY(text);
    QVERIFY(text->isReadOnly());
    QCOMPARE(text->toPlainText(), report);
    auto* send = dialog->findChild<QPushButton*>("failureReportSend");
    auto* close = dialog->findChild<QPushButton*>("failureReportClose");
    QVERIFY(send && close);
    QCOMPARE(send->text(), QString("Send This Report"));
    QVERIFY(close->isDefault());

    QApplication::clipboard()->clear();
    QString message;
    CaptureNextMessage(message);
    QTest::mouseClick(send, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), report);
    QCOMPARE(opened.size(), 1);
    const QUrl url = opened.front();
    QCOMPARE(url.host(), QString("github.com"));
    QCOMPARE(url.path(), QString("/chetjones003/GoSurvey/issues/new"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue("title", QUrl::FullyDecoded),
             QString("GoSurvey report (") + GOSURVEY_VERSION_FULL + ")");
    QVERIFY(query.queryItemValue("body", QUrl::FullyDecoded).contains("Exception code: 0xC0000005"));
    QCOMPARE(message, QString::fromUtf8(failureReportUi::SendOutcomeMessage(true, true).c_str()));
    QVERIFY2(dialog->isVisible(), "Send This Report leaves the window open");

    // A browser that cannot be opened is reported, with the address to use by hand.
    browserWorks = false;
    CaptureNextMessage(message);
    QTest::mouseClick(send, Qt::LeftButton);
    QCOMPARE(message, QString::fromUtf8(failureReportUi::SendOutcomeMessage(true, false).c_str()));
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
    crashReporter::CrashReport rejected;
    QVERIFY(!crashReporter::ReadCrashBundle(malformed.toStdWString(), rejected));
    QVERIFY2(!QFileInfo::exists(malformed), "a malformed bundle is not left in the temp folder");
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
  void failureReportAndUsageNoticeScreenshots() {
    const QString dir = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR);
    std::unique_ptr<QDialog> dialog(CreateFailureReportDialog("GoSurvey encountered a problem",
                                                              SampleCrashReport()));
    dialog->resize(dialog->minimumSize());
    dialog->show();
    QTest::qWait(50);
    QVERIFY(dialog->grab().save(dir + "/qt-883-report-final.png"));
    std::unique_ptr<QDialog> startup(CreateFailureReportDialog(
        "GoSurvey could not start",
        "GoSurvey could not initialize OpenGL 3.3 for drawing.\n\nTechnical detail:\n"
        "OpenGL shader setup failed (driver may not support OpenGL 3.3 core)."));
    startup->resize(startup->minimumSize());
    startup->show();
    QTest::qWait(50);
    QVERIFY(startup->grab().save(dir + "/qt-883-startup-final.png"));
    EditorWindow window;
    window.ShowSettings();
    auto* settings = window.findChild<QDialog*>("applicationSettings");
    QVERIFY(settings);
    // As opened: tall enough for its wrapped text.
    QVERIFY(settings->height() >= settings->heightForWidth(settings->width()));
    QTest::qWait(250);  // let the account card's refresh fill in its status
    QVERIFY(settings->grab().save(dir + "/qt-883-settings-final.png"));
  }

  // ---- REQ-077 / REQ-078 software update (issue #882) ----------------------------------------

  static QDialog* OfferUpdate(EditorWindow& window, const update::Manifest& manifest) {
    window.m_Update->runningVersion = "0.7.4";
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
    QVERIFY(notes->isVisibleTo(dialog));
    QVERIFY(notes->toPlainText().contains("Faster point import"));
    QVERIFY(compatibility->isVisibleTo(dialog));
    QVERIFY(compatibility->mapTo(dialog, QPoint()).y() < notes->mapTo(dialog, QPoint()).y());
    UpdateButton(dialog, "updateRemindButton")->click();
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

  void updateOfferIsShownBeforeTheSignInGate() {
    EditorWindow window;
    window.State().authGateResolved = false;
    window.m_AuthStarted = true;
    window.m_Update->phase = update::Phase::Checking;
    window.ServiceAuth();
    QVERIFY(!window.findChild<QDialog*>("signInGate"));
    window.m_Update->phase = update::Phase::Idle;
    window.ServiceAuth();
    if (HasInternetConnectivity()) QVERIFY(window.findChild<QDialog*>("signInGate"));
  }

  void settingsUpdatesPersistRevertOnFailureAndClearSkip() {
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
    QVERIFY(check && beta && skipped && clear);
    QVERIFY(dialog->findChild<QLabel*>("updateVersionLabel")->text().startsWith("Version "));
    QVERIFY(check->isChecked());
    QVERIFY(!beta->isChecked());
    QCOMPARE(skipped->text(), QString("Skipping 99.0.0"));

    beta->setChecked(true);
    AppCommandState reloaded;
    LoadUserStartupPrefSettings(reloaded);
    QVERIFY(reloaded.updatePrefs.useBetaChannel);
    check->setChecked(false);
    QVERIFY(!window.State().updatePrefs.enabled);
    QVERIFY(!beta->isEnabled());   // the channel is moot while checking is off
    AppCommandState reloadedOff;
    LoadUserStartupPrefSettings(reloadedOff);
    QVERIFY(!reloadedOff.updatePrefs.enabled);
    clear->click();
    QVERIFY(window.State().updatePrefs.skippedVersion.empty());
    QVERIFY(!clear->isVisibleTo(dialog));
    AppCommandState reloadedClear;
    LoadUserStartupPrefSettings(reloadedClear);
    QVERIFY(reloadedClear.updatePrefs.skippedVersion.empty());

    // A preferences file that cannot be written leaves the control showing what is really stored.
    QTemporaryFile blocker;
    QVERIFY(blocker.open());
    IsolatedUserData unwritable(blocker.fileName());   // APPDATA is a file, so the folder cannot exist
    check->setChecked(true);
    QVERIFY(!window.State().updatePrefs.enabled);
    QVERIFY(!check->isChecked());
    QVERIFY(!dialog->findChild<QLabel*>("updateSettingsError")->text().isEmpty());
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
    QVERIFY(window.findChild<QPushButton*>("newProjectButton"));
    QVERIFY(window.findChild<QPushButton*>("openProjectButton"));
    auto* gridMode = window.findChild<QToolButton*>("recentGridMode");
    auto* listMode = window.findChild<QToolButton*>("recentListMode");
    QVERIFY(gridMode);
    QVERIFY(listMode);
    QVERIFY(gridMode->isChecked());
    listMode->click();
    QVERIFY(listMode->isChecked());
    QVERIFY(!gridMode->isChecked());
    gridMode->click();
    QVERIFY(gridMode->isChecked());
    auto* signedIn = window.findChild<QWidget*>("startSignedInAccount");
    QVERIFY(signedIn);
    window.State().authSignedIn = true;
    window.State().authEmail = "survey@example.com";
    window.Refresh();
    QVERIFY(!signedIn->isHidden());
    QCOMPARE(window.findChild<QLabel*>("startAccountWelcome")->text(), QString("Welcome Survey!"));
    QCOMPARE(window.findChild<QLabel*>("startAccountEmail")->text(), QString("survey@example.com"));

    window.State().authGateResolved = true;
    window.State().whatsNewDismissedVersion = GOSURVEY_VERSION_FULL;
    window.InitializeStartup();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));

    EditorWindow firstRun;
    firstRun.State().authGateResolved = true;
    firstRun.State().whatsNewDismissedVersion.clear();
    firstRun.InitializeStartup();
    auto* whatsNew = firstRun.findChild<QDialog*>("whatsNewDialog");
    QVERIFY(whatsNew);
    QVERIFY(whatsNew->findChild<QLabel*>("whatsNewHeading"));
    QVERIFY(whatsNew->findChild<QLabel*>("whatsNewVersion"));
    QVERIFY(whatsNew->findChild<QTextBrowser*>("whatsNewReleaseNotes"));
    QVERIFY(whatsNew->findChild<QLabel*>("whatsNewReleasesLink"));
    QVERIFY(whatsNew->findChild<QCheckBox*>("whatsNewDontShowAgain"));
    auto* about = firstRun.findChild<QAction*>("aboutGoSurveyAction");
    QVERIFY(about);
    about->trigger();
    QCOMPARE(firstRun.findChildren<QDialog*>("whatsNewDialog").size(), 1);
    whatsNew->hide();
  }

  void updateOfferSuppressesWhatsNewForTheLaunch() {
    EditorWindow window;
    window.State().authGateResolved = true;
    window.State().whatsNewDismissedVersion.clear();   // would otherwise auto-open
    auto* dialog = OfferUpdate(window, OfferedManifest());
    QVERIFY(dialog);
    window.InitializeStartup();
    window.ServiceAuth();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));
    UpdateButton(dialog, "updateRemindButton")->click();
    window.ServiceUpdate();
    window.ServiceAuth();
    QVERIFY(!window.findChild<QDialog*>("whatsNewDialog"));   // still not later in the same launch
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
    QTRY_VERIFY(input->completer()->completionCount() > 0);
    input->completer()->popup()->hide();
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
    QSettings settings; settings.remove("editor/commandWidth"); settings.remove("editor/commandAnchor");
    const auto oldPalette = qApp->palette(); const auto oldFont = qApp->font();
    const auto oldSheet = qApp->styleSheet(); const auto oldStyle = qApp->style()->objectName();
    const auto oldDefault = qApp->property("defaultQtStyle"), oldLight = qApp->property("cadLightTheme");
    auto restoreAppearance = qScopeGuard([&] {
      qApp->setStyle(oldStyle); qApp->setPalette(oldPalette); qApp->setFont(oldFont); qApp->setStyleSheet(oldSheet);
      qApp->setProperty("defaultQtStyle", oldDefault); qApp->setProperty("cadLightTheme", oldLight);
    });
    ApplyQtAppearance(*qApp, false);
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(window.windowFlags() & Qt::FramelessWindowHint);
    auto* title = window.findChild<QWidget*>("editorTitleBar"); QVERIFY(title);
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
    auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport"); QVERIFY(viewport);
    auto* panel = dynamic_cast<FloatingCommandBar*>(window.findChild<QFrame*>("floatingCommandBar")); QVERIFY(panel);
    QCOMPARE(panel->parentWidget(), viewport);
    auto* input = panel->Input();
    input->setFocus(); input->setText("LINE"); QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(window.State().active, AppCommandState::Kind::Line);
    auto* hint = panel->findChild<QLabel*>("commandHint"); QVERIFY(hint); QVERIFY(!hint->isHidden());
    input->setText("0,0"); QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(hint->text().contains("href"));
    QVERIFY(QMetaObject::invokeMethod(hint, "linkActivated", Qt::DirectConnection, Q_ARG(QString, "a")));
    QVERIFY(window.State().segmentAngleKeyboardAwaitBearing);
    QTest::keyClick(input, Qt::Key_Escape);
    input->setText("LINE"); QTest::keyClick(input, Qt::Key_Return);
    input->setText("0,0"); QTest::keyClick(input, Qt::Key_Return);
    input->setText("10,0"); QTest::keyClick(input, Qt::Key_Return);
    QTest::keyClick(input, Qt::Key_Escape);
    QCOMPARE(window.State().userLinesFlat.size(), std::size_t(6));
    window.findChild<QToolButton*>("quick_UNDO")->click();
    QVERIFY(window.State().userLinesFlat.empty());
    window.findChild<QToolButton*>("quick_REDO")->click();
    QCOMPARE(window.State().userLinesFlat.size(), std::size_t(6));
    QTest::keyClick(input, Qt::Key_F2);
    QTRY_VERIFY(panel->Transcript()->isVisible());
    QVERIFY(panel->Transcript()->toPlainText().contains("LINE", Qt::CaseInsensitive));
    QTest::keyClick(input, Qt::Key_F2); QTRY_VERIFY(panel->Transcript()->isHidden());
    QTest::keyClick(input, Qt::Key_9, Qt::ControlModifier); QTRY_VERIFY(panel->isHidden());
    QTest::keyClick(viewport, Qt::Key_9, Qt::ControlModifier); QTRY_VERIFY(panel->isVisible());
    auto drag = [&](const char* name, int dx) {
      auto* grip = panel->findChild<QWidget*>(name); QVERIFY(grip);
      const QPoint local = grip->rect().center(); const QPoint global = grip->mapToGlobal(local);
      QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, local);
      QMouseEvent move(QEvent::MouseMove, local + QPoint(dx, 0), global + QPoint(dx, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(grip, &move);
      QTest::mouseRelease(grip, Qt::LeftButton);
    };
    const int oldX = panel->x(); drag("commandDragGrip", 20); QVERIFY(panel->x() > oldX);
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
    input->completer()->popup()->hide(); input->clear();
    tabs->setFocus(); QTRY_VERIFY(tabs->hasFocus());
    QTest::keyClicks(tabs, "c");
    QCOMPARE(input->text(), QString("c")); QVERIFY(input->hasFocus());
    input->completer()->popup()->hide(); input->clear();
    auto* tool = window.findChild<QToolButton*>("quick_NEW"); QVERIFY(tool);
    tool->setFocus(); QTRY_VERIFY(tool->hasFocus());
    QTest::keyClicks(tool, "p");
    QCOMPARE(input->text(), QString("p")); QVERIFY(input->hasFocus());
    input->completer()->popup()->hide(); input->clear();
    // An editable property keeps text input rather than turning it into a CAD command.
    auto* layer = window.findChild<QComboBox*>("selectionLayer"); QVERIFY(layer);
    auto* edit = layer->lineEdit(); QVERIFY(edit); edit->setFocus(); edit->selectAll();
    QTest::keyClicks(edit, "Survey");
    QCOMPARE(edit->text(), QString("Survey")); QVERIFY(edit->hasFocus()); QVERIFY(input->text().isEmpty());
    // Read-only history is not a text editor: typing there returns to command entry.
    auto* panel = dynamic_cast<FloatingCommandBar*>(window.findChild<QFrame*>("floatingCommandBar")); QVERIFY(panel);
    panel->ToggleConsole(); auto* console = panel->Transcript(); console->setFocus();
    QTRY_VERIFY(console->hasFocus()); QTest::keyClicks(console, "u");
    QCOMPARE(input->text(), QString("u")); QVERIFY(input->hasFocus());
    input->completer()->popup()->hide(); input->clear();
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
    QCOMPARE(input->text(), QString("z")); QTRY_VERIFY(input->hasFocus());
    input->completer()->popup()->hide(); input->clear();
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
    for (const auto& theme : {QString("Neutral gray"), QString("Classic light")}) {
      auto* action = Action(window, theme); QVERIFY(action); action->trigger();
      QCOMPARE(window.State().userLinesFlat, geometry);
      QVERIFY(!drawings()->isExpanded()); QVERIFY(blocks()->isExpanded());
      QVERIFY(tree->currentItem()); QCOMPARE(tree->currentItem()->text(0), selected);
      QCOMPARE(qApp->property("cadLightTheme").toBool(), theme == "Classic light");
      if (theme == "Neutral gray") {
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#222426"));
        QCOMPARE(qApp->palette().color(QPalette::Button), QColor("#383a3c"));
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
    const qreal ratio = viewport->devicePixelRatioF();
    const auto face = image.pixelColor(qRound((viewport->width() - 55) * ratio), qRound(91 * ratio));
    QVERIFY2(face.blue() > 60 && face.red() > 40, qPrintable(face.name()));
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

  void pointGroupManagerCreatesAndUndoesFromEmptyState() {
    EditorWindow window; window.CreateDrawingFromTemplate();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("pointGroupManager"); QVERIFY(dialog);
      QTimer::singleShot(20000, dialog, &QDialog::reject);
      auto* matches = dialog->findChild<QLabel*>("pointGroupMatchCount"); QVERIFY(matches);
      QVERIFY(matches->text().contains("No point group selected"));
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-894-point-groups-empty.png")));
      auto* create = dialog->findChild<QPushButton*>("pointGroupNew"); QVERIFY(create); create->click();
      QCOMPARE(window.State().pointGroups.size(), std::size_t(1));
      QCOMPARE(window.State().pointGroups.front().name, std::string("Point Group"));
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-894-point-groups-new.png")));
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
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-894-point-groups-populated.png")));

      auto* name = dialog->findChild<QLineEdit*>("pointGroupName"); QVERIFY(name);
      name->setText("Curb Shots");
      QVERIFY(QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection));
      QCOMPARE(state.pointGroups.front().name, std::string("Existing Ground"));
      auto* validation = dialog->findChild<QLabel*>("pointGroupValidation"); QVERIFY(validation);
      QVERIFY(validation->text().contains("already exists"));
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-894-point-groups-validation.png")));

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
      QVERIFY(dialog->grab().save(QDir(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR)).filePath("qt-894-point-groups-no-match.png")));
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
      auto* dialog = window.findChild<QDialog*>("drawingUnits");
      if (!dialog) return;
      dialog->findChild<QComboBox*>("drawingUnit")->setCurrentIndex(4);
      dialog->findChild<QDoubleSpinBox*>("drawingScale")->setValue(100);
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
    reports->findChild<QPushButton*>("copySurveyReport")->click();
    QVERIFY(QApplication::clipboard()->text().contains("200.25"));
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
    QCOMPARE(buttons, 284);
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

  void releaseRibbonSelectorsAndFittingMenuReachSharedServices() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1500, 950); window.show(); auto& state = window.State();
    if (auto* viewPage = window.findChild<QWidget*>("ribbonPage_View")) {
      if (auto* ribbon = window.findChild<QTabWidget*>("ribbon")) ribbon->setCurrentWidget(viewPage);
      QApplication::processEvents();
      const QString artifact = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/";
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
    const QString artifacts = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/";
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

  void releaseRibbonFeatureLineRunsAndCancelsCompleteWorkflow() {
    EditorWindow window; window.CreateDrawingFromTemplate(); window.resize(1440, 900); window.show();
    QTest::qWait(100);
    auto& state = window.State();
    const QString artifacts = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/";
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
      viewsShot = dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-892-view-manager-views.png");
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
      ucsShot = dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-892-view-manager-ucs.png");
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
      emptyShot = dialog->grab().save(QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR) + "/qt-892-view-manager-empty.png");
      dialog->reject();
    });
    window.Submit("VIEW");
    QVERIFY(sawEmptyLists);
    QVERIFY(emptyShot);
    QVERIFY(state.namedViews.empty());
    QVERIFY(state.ucsNamed.empty());
    QVERIFY(ucs::FramesMatch(state.activeUcs, originalUcs));
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
    st.rightClickLongerClickMs = 150;
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
    const QString dir = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR);
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
      const QString directory = QString::fromUtf8(GOSURVEY_TEST_ARTIFACT_DIR);
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
    QTest::qWait(600);
    st.cadSurfaces[1].boundaries.push_back({31, CadBoundaryKind::Outer, "first"});
    st.cadSurfaces[1].boundaries.push_back({32, CadBoundaryKind::Hide, "second"});
    QTest::qWait(600);  // the manager notices the change on its refresh timer
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
};

int main(int argc, char** argv) {
  QSurfaceFormat format;
  format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24); format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc, argv);
  app.setOrganizationName("GoSurveyTests"); app.setApplicationName("QtEditorTests");
  QtEditorTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "QtEditorTests.moc"
