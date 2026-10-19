#include "AdjustmentWindow.hpp"
#include "EditorWindow.hpp"
#include "io/DwgIo.hpp"
#include "Version.hpp"
#include "EditorViewport.hpp"
#include "EditorChrome.hpp"
#include "QtAppearance.hpp"
#include "TopographicArtwork.hpp"
#include "RibbonCatalog.hpp"
#include "TraverseActions.hpp"
#include "traverse/TraverseIo.hpp"
#include "PdfAttach.hpp"
#include "PdfWindow.hpp"
#include "QtAnnotations.hpp"
#include "ProjectPoints.hpp"
#include "ProjectSettings.hpp"
#include "SurveyCsv.hpp"
#include "io/ProjectPointDb.hpp"
#include "io/Project.hpp"
#include "ProjectSettings.hpp"
#include "GsMigrate.hpp"
#include "GsIo.hpp"
#include "AppPaths.hpp"
#include "DwgIo.hpp"
#include "UserPrefs.hpp"
#include "platform/HttpFetch.hpp"
#include "update/UpdateService.hpp"
#include "CrashReporter.hpp"
#include "FailureReport.hpp"
#include "FailureReportUi.hpp"
#include "StartupFailure.hpp"
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
#include <QDockWidget>
#include <QFrame>
#include <QSettings>
#include <QTreeWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QStatusBar>
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

class QtEditorTests final : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void initTestCase() { QVERIFY(PdfAttach_Init()); }
  void cleanupTestCase() { PdfAttach_Shutdown(); }

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
    auto* tree = properties->findChild<QTreeWidget*>(); QVERIFY(tree);
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
    const int terrainHeight = qMin(tree->viewport()->height(), tree->viewport()->width() * 220 / 300);
    QVERIFY(tree->viewport()->grab(QRect(0, tree->viewport()->height() - terrainHeight,
                                        tree->viewport()->width(), terrainHeight))
                .save(Issue886Shot("qt-topo-properties-detail.png")));
    QVERIFY(toolspace->grab().save(Issue886Shot("qt-topo-toolspace.png")));
    // Art belongs inside the tree's existing background, without consuming row space.
    const int gap = properties->widget()->height() - tree->geometry().bottom() - 1;
    QVERIFY2(gap <= 8, qPrintable(QString("Contour decoration reserves %1 px below Properties").arg(gap)));
    const auto image = tree->viewport()->grab().toImage();
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
    QVERIFY(captureDrawing("qt-topo-drawing-compact.png"));
    ApplyQtAppearance(*qApp, true); QTest::qWait(100);
    QVERIFY(captureDrawing("qt-topo-light.png"));
    const auto light = tree->viewport()->grab().toImage();
    amberPixels = 0;
    for (int y = light.height() * 2 / 3; y < light.height(); ++y)
      for (int x = 0; x < light.width(); ++x) {
        const auto c = light.pixelColor(x, y);
        amberPixels += c.red() > 30 && c.red() > c.green() * 1.25 && c.green() > c.blue();
      }
    QCOMPARE(amberPixels, 0);
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
    QCOMPARE(buttons, 280);
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
    EditorWindow window; window.CreateDrawingFromTemplate(); window.show(); auto& state = window.State();
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
