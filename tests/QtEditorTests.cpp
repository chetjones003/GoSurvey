#include "AdjustmentWindow.hpp"
#include "EditorWindow.hpp"
#include "EditorViewport.hpp"
#include "EditorChrome.hpp"
#include "TraverseActions.hpp"
#include "traverse/TraverseIo.hpp"
#include "PdfAttach.hpp"
#include "PdfWindow.hpp"
#include "QtAnnotations.hpp"
#include "ProjectPoints.hpp"
#include "io/ProjectPointDb.hpp"
#include <QPdfWriter>
#include <QPainter>
#include <QtTest/QtTest>
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QCompleter>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QDockWidget>
#include <QSettings>
#include <QTreeWidget>
#include <QScopeGuard>
#include <QStyle>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <cmath>
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
}

class QtEditorTests final : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void initTestCase() { QVERIFY(PdfAttach_Init()); }
  void cleanupTestCase() { PdfAttach_Shutdown(); }

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
    EditorWindow window;
    window.show();
    auto* viewport = window.findChild<QOpenGLWidget*>("modelViewport");
    auto* input = window.findChild<QLineEdit*>("commandInput");
    QVERIFY(viewport); QVERIFY(input);
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
    EditorWindow window; window.Submit("NEW"); window.show(); window.activateWindow();
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
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/qt-chrome-preview.png"));
    settings.remove("editor/commandWidth"); settings.remove("editor/commandAnchor");
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
    EditorWindow window;
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
    }
    window.Submit("UNDO"); QVERIFY(window.State().userLinesFlat.empty());
    QVERIFY(!drawings()->isExpanded()); QVERIFY(blocks()->isExpanded());
  }

  void frontViewPanMovesInCameraPlane() {
    EditorWindow window;
    window.show();
    auto* viewport = static_cast<EditorViewport*>(window.findChild<QOpenGLWidget*>("modelViewport"));
    QVERIFY(viewport);
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
    QTRY_VERIFY_WITH_TIMEOUT(viewport->IsRendererReady(), 10000);
    const auto image = viewport->grabFramebuffer();
    QVERIFY(!image.isNull());
    const qreal ratio = viewport->devicePixelRatioF();
    const auto face = image.pixelColor(qRound((viewport->width() - 55) * ratio), qRound(91 * ratio));
    QVERIFY2(face.blue() > 60 && face.red() > 40, qPrintable(face.name()));
  }

  void nativeLayerManagerEditsAreUndoable() {
    EditorWindow window;
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
    EditorWindow window;
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
    EditorWindow window;
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

  void nativePointCreationPlacesSelectsAndUndoes() {
    EditorWindow window;
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
    window.Submit("NEW");
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
    EditorWindow source; source.Submit("NEW");
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
    EditorWindow target; target.Submit("NEW");
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
    EditorWindow window; window.Submit("NEW");
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
    EditorWindow window; window.Submit("NEW"); std::vector<std::string> log;
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
    EditorWindow window; window.Submit("NEW");
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
    EditorWindow window; window.Submit("NEW"); auto& state = window.State();
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
