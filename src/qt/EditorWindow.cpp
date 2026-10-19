#include "EditorWindow.hpp"
#include "EditorChrome.hpp"
#include "QtAppearance.hpp"
#include <QActionGroup>
#include <QTreeWidgetItemIterator>
#include <QSet>
#include <QStyle>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QKeySequenceEdit>
#include <QShowEvent>
#include "EditorViewport.hpp"
#include "AdjustmentWindow.hpp"
#include "QtText.hpp"
#include "PdfWindow.hpp"
#include "CadBlocks.hpp"
#include "DwgIo.hpp"
#include "GsIo.hpp"
#include "AppPaths.hpp"
#include "UserPrefs.hpp"
#include "auth/AuthService.hpp"
#include "platform/HttpFetch.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include "MtextRichFormat.hpp"
#include "Version.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QStringListModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QShortcut>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <QPdfWriter>
#include <QPainter>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVBoxLayout>
#include <cstring>

QString QtAsset(const QString& relative) {
  QString resolved = relative;
  if (relative == "icons/New.png") resolved = "icons/New_Drawing.png";
  else if (relative == "icons/circle.png") resolved = "icons/Circle_Center_Radius.png";
  else if (relative == "icons/Zoom_Extents.png") resolved = "icons/Zoom_Extensis.png";
  else if (relative == "icons/Point.png") resolved = "icons/c3d_point.png";
  const auto path = ResolveBundledAssetPath(std::filesystem::path("resources") /
      std::filesystem::path(resolved.toStdWString()));
  return QString::fromStdWString(path.wstring());
}

QIcon CadCommandIcon(const QString& icon) {
  const auto path = QtAsset("icons/" + icon);
  if (path.isEmpty()) return {};
  auto image = QImage(path).convertToFormat(QImage::Format_ARGB32);
  // Keep the supplied icon artwork; give its neutral white strokes contrast on light controls.
  if (qApp->palette().color(QPalette::Window).lightness() > 128) {
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (color.alpha() && color.red() > 210 && color.green() > 210 && color.blue() > 210 &&
          std::max({color.red(), color.green(), color.blue()}) - std::min({color.red(), color.green(), color.blue()}) < 15)
        image.setPixelColor(x, y, QColor(64, 64, 64, color.alpha()));
    }
  }
  return QIcon(QPixmap::fromImage(image));
}

EditorWindow::EditorWindow() {
  m_State.measureAnnotationText = MeasureQtText;
  setObjectName("GoSurveyQtEditor");
  setWindowTitle("GoSurvey");
  setWindowFlag(Qt::FramelessWindowHint);
  setMinimumSize(820, 540);
  setWindowIcon(QIcon(QtAsset("icons/app.ico")));
  resize(1500, 950);
  setDockNestingEnabled(true);
  m_State.activeDrawingIdx = 1;
  m_State.prevDrawingIdx = 1;
  const auto templatePath = ResolveDefaultWorkspaceTemplateGstPath();
  if (!templatePath.empty()) LoadGoSurveyTemplateFile(m_State, templatePath.u8string().c_str(), m_Log);
  LoadBundledBlockLibrary(m_State, m_Log);

  auto* center = new QWidget(this);
  auto* centerLayout = new QVBoxLayout(center);
  centerLayout->setContentsMargins(0, 0, 0, 0);
  centerLayout->setSpacing(0);
  m_Tabs = new QTabBar(center);
  m_Tabs->setObjectName("drawingTabs");
  m_Tabs->setTabsClosable(true);
  m_Tabs->setExpanding(false);
  m_Tabs->setAutoHide(false);
  m_Tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_Tabs->setMinimumHeight(m_Tabs->fontMetrics().height() + 12);
  centerLayout->addWidget(m_Tabs);
  m_Pages = new QStackedWidget(center);
  auto* start = new QWidget(m_Pages);
  auto* startLayout = new QVBoxLayout(start);
  startLayout->setContentsMargins(70, 45, 70, 45);
  auto* title = new QLabel("GoSurvey", start);
  if (!qApp->property("defaultQtStyle").toBool()) title->setStyleSheet("font-size:36px; font-weight:600;");
  startLayout->addWidget(title);
  startLayout->addWidget(new QLabel("Survey. Design. Deliver.", start));
  auto* create = new QPushButton(QIcon(QtAsset("icons/New.png")), "New drawing", start);
  auto* open = new QPushButton(QIcon(QtAsset("icons/Open.png")), "Open drawing...", start);
  startLayout->addWidget(create);
  startLayout->addWidget(open);
  auto* news = new QPushButton("What's new", start);
  startLayout->addWidget(news);
  startLayout->addStretch();
  connect(create, &QPushButton::clicked, this, [this] { NewDrawing(); });
  connect(open, &QPushButton::clicked, this, [this] { Submit("OPEN"); });
  connect(news, &QPushButton::clicked, this, [this] { ShowWhatsNew(); });
  m_Pages->addWidget(start);

  auto* modelPage = new QWidget(m_Pages);
  auto* modelLayout = new QVBoxLayout(modelPage);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  m_Viewport = new EditorViewport(m_State, m_Log, modelPage);
  m_Viewport->changed = [this] { Refresh(); };
  m_Viewport->selectionListRequested = [this] { ShowSelectionList(); };
  m_Viewport->textEntered = [this](const QString& text) { if (m_CommandBar->isHidden()) m_CommandBar->ToggleVisible(); m_Command->setFocus(); m_Command->insert(text); };
  m_Viewport->cursorChanged = [this](double x, double y, double z) {
    m_Coordinates->setText(QString("E %1   N %2   Z %3").arg(x + m_State.worldDocumentOriginX, 0, 'f', 4)
        .arg(y + m_State.worldDocumentOriginY, 0, 'f', 4).arg(z, 0, 'f', 4));
  };
  modelLayout->addWidget(m_Viewport, 1);
  m_Pages->addWidget(modelPage);
  centerLayout->addWidget(m_Pages, 1);
  setCentralWidget(center);
  BuildRibbon();
  BuildDocks();
  UpdateChromeIcons();
  m_Coordinates = new QLabel(this);
  statusBar()->addPermanentWidget(m_Coordinates, 1);
  auto* ortho = new QToolButton(this);
  ortho->setText("ORTHO"); ortho->setCheckable(true);
  statusBar()->addWidget(ortho);
  connect(ortho, &QToolButton::toggled, this, [this](bool on) { m_State.orthoMode = on; });
  auto* grid = new QToolButton(this);
  grid->setText("GRID"); grid->setCheckable(true); grid->setChecked(true);
  statusBar()->addWidget(grid);
  connect(grid, &QToolButton::toggled, this, [this](bool on) { m_Viewport->SetGrid(on); });
  auto* osnap = new QToolButton(this);
  osnap->setText("OSNAP"); osnap->setCheckable(true); osnap->setChecked(m_State.objectSnapEnabled);
  statusBar()->addWidget(osnap);
  connect(osnap, &QToolButton::toggled, this, [this](bool on) { m_State.objectSnapEnabled = on; });
  auto* annotationScale = new QComboBox(this);
  annotationScale->setObjectName("statusAnnotationScale");
  annotationScale->setMinimumWidth(110);
  annotationScale->setAccessibleName("Annotation scale");
  annotationScale->setToolTip("Current annotation scale");
  statusBar()->addWidget(annotationScale);
  connect(annotationScale, &QComboBox::activated, this, [this](int index) {
    if (index < 0 || index >= static_cast<int>(m_State.annotationScales.size()) ||
        index == m_State.currentAnnotationScaleIndex) return;
    SetCurrentAnnotationScaleIndex(m_State, index);
    Refresh();
  });
  auto* multiSelect = new QToolButton(this);
  multiSelect->setText("Multi Selection"); multiSelect->setCheckable(true);
  multiSelect->setChecked(m_State.multiSelectionEnabled);
  multiSelect->setToolTip("Disambiguate overlapping linework picks.");
  statusBar()->addWidget(multiSelect);
  connect(multiSelect, &QToolButton::toggled, this, [this](bool on) {
    m_State.multiSelectionEnabled = on;
    if (!on) { m_State.viewportPickCandidates.clear(); m_State.viewportPickAmbiguous = false; }
    m_Viewport->update();
  });
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] {
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
    m_Viewport->CancelSelectionBox();
    m_State.showCreatePointsWindow = false; CancelActiveCommand(m_State, m_Log); m_Command->clear(); Refresh();
  });
  connect(new QShortcut(QKeySequence(Qt::Key_F3), this), &QShortcut::activated, osnap, &QToolButton::toggle);
  connect(new QShortcut(QKeySequence(Qt::Key_F7), this), &QShortcut::activated, grid, &QToolButton::toggle);
  connect(new QShortcut(QKeySequence(Qt::Key_F8), this), &QShortcut::activated, ortho, &QToolButton::toggle);
  auto* service = new QTimer(this);
  service->setInterval(33);
  connect(service, &QTimer::timeout, this, [this] {
    ServiceAuth();
    const auto before = m_State.cadGpuRevision;
    const auto logSize = m_Log.size();
    TickSurfaceRebuilds(m_State, m_Log); TickPointCloudImport(m_State, m_Log); TickVolumeDashboard(m_State);
    RefreshProjects();
    if (before != m_State.cadGpuRevision || logSize != m_Log.size()) Refresh();
    else ServiceUiRequests();
  });
  service->start();
  connect(m_Tabs, &QTabBar::currentChanged, this, [this](int index) { if (!m_Refreshing) SwitchDrawing(index); });
  connect(m_Tabs, &QTabBar::tabCloseRequested, this, [this](int index) { CloseDrawing(index); });
  QSettings settings;
  restoreGeometry(settings.value("editor/geometry").toByteArray());
  restoreState(settings.value("editor/layout").toByteArray(), 1);
  Refresh();
  qApp->installEventFilter(this);
}

QAction* EditorWindow::CommandAction(const QString& label, const QString& icon,
                                    const QString& command, const QKeySequence& shortcut) {
  auto* action = new QAction(CadCommandIcon(icon), label, this);
  action->setProperty("cadIcon", icon);
  if (action->icon().isNull()) action->setIcon(QIcon(QtAsset("icons/app.png")));
  action->setObjectName("command_" + command);
  action->setToolTip(label + " (" + command + ")");
  action->setShortcut(shortcut);
  addAction(action);
  connect(action, &QAction::triggered, this, [this, command] { Submit(command); });
  return action;
}

void EditorWindow::ServiceAuth() {
  if (!m_AuthStarted)
    return;
  if (m_State.authSignOutRequested) {
    m_State.authSignOutRequested = false;
    auth::SignOut();
    m_State.authSignedIn = false;
    m_State.authEmail.clear();
    m_State.authError.clear();
    Refresh();
  }
  if (m_State.authSignInRequested) {
    m_State.authSignInRequested = false;
    if (!m_AuthTask) {
      m_AuthTask = auth::BeginInteractiveSignIn();
      m_AuthLastAttemptInteractive = true;
      m_State.authBusy = true;
      m_State.authInteractiveBusy = true;
      m_State.authError.clear();
    }
  }
  if (m_State.authDeclineRequested) {
    m_State.authDeclineRequested = false;
    m_State.authOptedOut = true;
    if (SaveUserStartupPrefs(m_State)) {
      m_State.authGateResolved = true;
    } else {
      m_State.authOptedOut = false;
      m_State.authError = "Your sign-in choice could not be saved. It may "
                          "appear again next time.";
    }
  }
  if (m_AuthTask && m_AuthTask->done.load(std::memory_order_acquire)) {
    m_State.authBusy = false;
    m_State.authInteractiveBusy = false;
    m_State.authSignedIn = m_AuthTask->ok;
    m_State.authEmail = m_AuthTask->email;
    m_State.authError = (!m_AuthTask->ok && m_AuthLastAttemptInteractive)
                            ? m_AuthTask->error
                            : "";
    if (m_AuthTask->ok || m_AuthTask->skippedNoNetwork)
      m_State.authGateResolved = true;
    if (m_AuthTask->ok && m_State.authOptedOut) {
      m_State.authOptedOut = false;
      if (!SaveUserStartupPrefs(m_State))
        m_State.authError =
            "Signed in, but the saved sign-in preference could not be updated.";
    }
    m_AuthTask.reset();
    Refresh();
  }
  if (m_State.authGateResolved) {
    if (auto *gate = findChild<QDialog *>("signInGate"))
      gate->accept();
  } else if (HasInternetConnectivity()) {
    ShowSignInGate();
  }
}

void EditorWindow::StartAuth(bool previewSignIn) {
  if (m_AuthStarted)
    return;
  m_AuthStarted = true;
  if (previewSignIn) {
    m_State.authGateResolved = false;
    m_State.authSignedIn = false;
    m_State.authBusy = false;
    m_State.authOptedOut = false;
    ShowSignInGate();
    return;
  }
  if (!m_State.authOptedOut) {
    m_AuthTask = auth::BeginSilentRefresh();
    m_State.authBusy = true;
  } else {
    m_State.authGateResolved = true;
  }
}

void EditorWindow::UpdateChromeIcons() {
  for (auto* command : findChildren<QAction*>())
    if (command->property("cadIcon").isValid()) command->setIcon(CadCommandIcon(command->property("cadIcon").toString()));
  for (auto* dock : findChildren<QDockWidget*>()) for (auto* button : dock->findChildren<QAbstractButton*>()) {
    if (button->objectName() != "qt_dockwidget_closebutton" && button->objectName() != "qt_dockwidget_floatbutton") continue;
    auto glyph = button->icon().pixmap(32, 32);
    if (glyph.isNull()) continue;
    QPainter painter(&glyph); painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(glyph.rect(), palette().color(QPalette::WindowText)); painter.end();
    button->setIcon(QIcon(glyph));
  }
}

void EditorWindow::BuildRibbon() {
  auto* applicationMenu = new QMenu(this);
  applicationMenu->setObjectName("goSurveyApplicationMenu");
  auto* file = applicationMenu->addMenu("&File");
  file->addAction(CommandAction("&New", "New.png", "NEW", QKeySequence::New));
  file->addAction(CommandAction("&Open...", "Open.png", "OPEN", QKeySequence::Open));
  file->addAction(CommandAction("&Save", "Save.png", "SAVE", QKeySequence::Save));
  file->addAction(CommandAction("Save &as...", "Save_As.png", "SAVEAS", QKeySequence::SaveAs));
  file->addSeparator();
  file->addAction(CommandAction("New project...", "New_Drawing.png", "NEWPROJECT"));
  file->addAction(CommandAction("Open project...", "Open.png", "OPENPROJECT"));
  file->addSeparator();
  file->addAction(CommandAction("PDF viewer...", "Pdf.png", "PDFVIEW"));
  file->addSeparator();
  file->addAction("Exit", this, &QWidget::close);
  auto* edit = applicationMenu->addMenu("&Edit");
  edit->addAction(CommandAction("Undo", "Undo.png", "UNDO", QKeySequence::Undo));
  edit->addAction(CommandAction("Redo", "Redo.png", "REDO", QKeySequence::Redo));
  edit->addAction(CommandAction("Copy", "Copy.png", "COPYCLIP", QKeySequence::Copy));
  edit->addAction(CommandAction("Paste", "Paste.png", "PASTE", QKeySequence::Paste));
  auto* viewMenu = applicationMenu->addMenu("&View"); viewMenu->setObjectName("viewMenu");
  connect(viewMenu->addAction("Survey reports"), &QAction::triggered, this, [this] { ShowSurveyReports(); });
  auto* orientation = viewMenu->addMenu("Orientation");
  struct View { const char* name; float azimuth; float elevation; };
  for (const auto& view : {View{"Top", 0, 90}, View{"Front", 0, 0}, View{"Right", 90, 0},
      View{"Back", 180, 0}, View{"Left", -90, 0}, View{"Bottom", 0, -90}, View{"Home", 45, 35.264f}})
    connect(orientation->addAction(view.name), &QAction::triggered, this, [this, view] { m_Viewport->SetOrientation(view.azimuth, view.elevation); });
  viewMenu->addAction(CommandAction("Zoom extents", "Zoom_Extents.png", "ZOOMEXTENTS"));
  auto* appearance = viewMenu->addMenu("Appearance");
  auto* themes = new QActionGroup(this); themes->setExclusive(true);
  for (bool light : {false, true}) {
    auto* action = appearance->addAction(light ? "Classic light" : "Neutral gray");
    action->setCheckable(true); action->setChecked(qApp->property("cadLightTheme").toBool() == light); themes->addAction(action);
    connect(action, &QAction::triggered, this, [this, light] {
      ApplyQtAppearance(*qApp, light);
      QSettings settings; settings.setValue("editor/lightTheme", light);
      UpdateChromeIcons();
      Refresh();
    });
  }
  viewMenu->addSeparator();
  connect(viewMenu->addAction("Settings…"), &QAction::triggered, this, [this] { ShowSettings(); });
  viewMenu->addSeparator();
  applicationMenu->addMenu("&Survey")->addAction(CommandAction("Least-squares adjustment...", "Adjust.png", "LSA"));
  auto* help = applicationMenu->addMenu("&Help");
  help->addAction("What's new", this, [this] { ShowWhatsNew(); });
  help->addAction("About GoSurvey", this, [this] {
    QMessageBox::about(this, "GoSurvey", "GoSurvey Qt / Win32 editor\nQt " QT_VERSION_STR "\nMigration branch: test/win32UI");
  });
  BuildTitleBar(applicationMenu);
  BuildRibbonPages();
}

void EditorWindow::BuildDocks() {
  auto makeDock = [&](const char* title, const char* name, Qt::DockWidgetArea area, QWidget* widget) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(name);
    dock->setWidget(widget);
    addDockWidget(area, dock);
    for (auto* menu : findChildren<QMenu*>())
      if (menu->objectName() == "viewMenu") menu->addAction(dock->toggleViewAction());
    return dock;
  };
  m_Toolspace = new QTreeWidget(this);
  m_Toolspace->setHeaderLabel("Prospector");
  m_Toolspace->setUniformRowHeights(true);
  m_Toolspace->setIndentation(14);
  m_Toolspace->setMinimumWidth(215);
  makeDock("Toolspace", "toolspaceDock", Qt::LeftDockWidgetArea, m_Toolspace);
  auto* propertyWidget = new QWidget(this);
  auto* propertyLayout = new QVBoxLayout(propertyWidget);
  propertyLayout->setContentsMargins(4, 4, 4, 4);
  propertyLayout->setSpacing(3);
  m_Layer = new QComboBox(propertyWidget);
  m_Layer->setObjectName("selectionLayer");
  m_Layer->setEditable(true);
  propertyLayout->addWidget(new QLabel("Layer", propertyWidget));
  propertyLayout->addWidget(m_Layer);
  m_Properties = new QTreeWidget(propertyWidget);
  m_Properties->setHeaderLabels({"Property", "Value"});
  m_Properties->setRootIsDecorated(true);
  m_Properties->setIndentation(12);
  m_Properties->setUniformRowHeights(true);
  m_Properties->setAlternatingRowColors(true);
  m_Properties->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_Properties->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_Properties->setColumnWidth(0, 118);
  propertyLayout->addWidget(m_Properties);
  makeDock("Properties", "propertiesDock", Qt::RightDockWidgetArea, propertyWidget);
  connect(m_Layer, &QComboBox::textActivated, this, [this](const QString& layer) {
    CadRibbonPickLayer(m_State, layer.toUtf8().toStdString(), m_Log);
    Refresh();
  });
  m_CommandBar = new FloatingCommandBar(m_Viewport);
  m_CommandBar->optionSelected = [this](const QString& shortcut) { Submit(shortcut); m_Command->setFocus(); };
  m_Transcript = m_CommandBar->Transcript();
  m_Command = m_CommandBar->Input();
  connect(new QShortcut(QKeySequence(Qt::Key_F2), this), &QShortcut::activated, this, [this] { m_CommandBar->ToggleConsole(); });
  auto* toggleCommand = new QAction("Command line", this);
  toggleCommand->setShortcut(QKeySequence("Ctrl+9")); addAction(toggleCommand);
  connect(toggleCommand, &QAction::triggered, this, [this] { m_CommandBar->ToggleVisible(); });
  if (auto* view = findChild<QMenu*>("viewMenu")) view->addAction(toggleCommand);
  m_Command->setPlaceholderText("Type a command or coordinates");
  QStringList names;
  for (const auto& suggestion : FuzzyCommandSuggestions("", 10000))
    names.push_back(QString::fromUtf8(suggestion.name.c_str()));
  auto* completer = new QCompleter(names, m_Command);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  m_Command->setCompleter(completer);
  connect(m_Command, &QLineEdit::textEdited, this, [completer](const QString& text) {
    QStringList matches;
    for (const auto& suggestion : FuzzyCommandSuggestions(text.toUtf8().toStdString(), 30))
      matches.push_back(QString::fromUtf8(suggestion.name.c_str()));
    static_cast<QStringListModel*>(completer->model())->setStringList(matches);
    completer->setCompletionPrefix(text);
    if (!matches.isEmpty()) completer->complete();
  });
  connect(m_Command, &QLineEdit::returnPressed, this, [this] {
    const QString line = m_Command->text();
    m_Command->clear();
    Submit(line);
  });
  connect(m_Toolspace, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    const auto index = item->data(0, Qt::UserRole);
    if (index.isValid()) SwitchDrawing(index.toInt());
  });
  connect(m_Properties, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
    if (m_Refreshing || column != 1) return;
    if (item->text(0) == "Color") CadRibbonPickColor(m_State, item->text(1).toUtf8().toStdString(), m_Log);
    Refresh();
  });
}

void EditorWindow::Submit(const QString& command) {
  if (!command.trimmed().isEmpty()) {
    if (m_CommandHistory.isEmpty() || m_CommandHistory.last() != command) m_CommandHistory.push_back(command);
    if (m_CommandHistory.size() > 500) m_CommandHistory.removeFirst();
    m_HistoryIndex = static_cast<int>(m_CommandHistory.size());
  }
  const QString verb = command.trimmed().toUpper();
  if (verb == "NEW") NewDrawing();
  else if (verb == "COPYCLIP") CopySelectionToClipboard(m_State, m_Log);
  else if (verb == "PASTECLIP") StartPasteCommand(m_State, m_Log);
  else if (verb == "NEWPROJECT") NewProject();
  else if (verb == "OPENPROJECT") {
    const auto path = QFileDialog::getOpenFileName(this, "Open project", {}, "GoSurvey project (*.gsproj)");
    if (!path.isEmpty()) OpenProject(path, true);
  }
  else if (verb == "OPEN") {
    const QString path = QFileDialog::getOpenFileName(this, "Open drawing", {}, "Drawing (*.dwg)");
    if (!path.isEmpty()) OpenDrawing(path);
  } else if (verb == "SAVE" || verb == "QSAVE") SaveDrawing();
  else if (verb == "SAVEAS") SaveDrawing(true);
  else if (verb == "PDFVIEW") ShowPdf();
  else if (verb == "LSA" || verb == "TRAVERSE") ShowAdjustment();
  else if (verb == "POINTDATABASE") ShowPoints(true);
  else {
    if (m_State.activeDrawingIdx == 0) NewDrawing();
    const QByteArray input = command.toUtf8();
    std::vector<char> buffer(input.begin(), input.end());
    buffer.push_back('\0');
    ProcessCommandLineSubmit(buffer.data(), static_cast<int>(buffer.size()), m_State, m_Log);
  }
  Refresh();
}

bool EditorWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_Command && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      m_State.showCreatePointsWindow = false;
      CancelActiveCommand(m_State, m_Log);
      m_Command->clear(); m_Viewport->InvalidateGeometry(); Refresh();
      return true;
    }
    if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
      if (m_Command->completer() && m_Command->completer()->popup()->isVisible()) return QMainWindow::eventFilter(watched, event);
      m_HistoryIndex = std::clamp(m_HistoryIndex + (key->key() == Qt::Key_Up ? -1 : 1), 0, static_cast<int>(m_CommandHistory.size()));
      m_Command->setText(m_HistoryIndex < m_CommandHistory.size() ? m_CommandHistory[m_HistoryIndex] : QString{});
      return true;
    }
  }
  if (event->type() == QEvent::KeyPress && m_Command && watched != m_Command &&
      !QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
    auto* widget = qobject_cast<QWidget*>(watched);
    auto* key = static_cast<QKeyEvent*>(event);
    auto* floatingDock = widget ? qobject_cast<QDockWidget*>(widget->window()) : nullptr;
    const bool editorWindow = widget && (widget->window() == this ||
        (floatingDock && findChildren<QDockWidget*>().contains(floatingDock)));
    bool editing = false;
    for (auto* focus = QApplication::focusWidget(); focus; focus = focus->parentWidget()) {
      const auto* line = qobject_cast<QLineEdit*>(focus);
      const auto* text = qobject_cast<QTextEdit*>(focus);
      const auto* plain = qobject_cast<QPlainTextEdit*>(focus);
      if ((line && !line->isReadOnly()) || (text && !text->isReadOnly()) ||
          (plain && !plain->isReadOnly()) || qobject_cast<QAbstractSpinBox*>(focus) ||
          qobject_cast<QComboBox*>(focus) || qobject_cast<QKeySequenceEdit*>(focus)) { editing = true; break; }
      if (focus->isWindow()) break;
    }
    if (editorWindow && !editing && !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        !key->text().isEmpty() && key->text().front().isPrint() && key->key() != Qt::Key_Space) {
      if (m_State.activeDrawingIdx == 0) NewDrawing();
      if (m_CommandBar->isHidden()) m_CommandBar->ToggleVisible();
      if (widget->window() != this) {
        raise(); activateWindow();
      }
      m_Command->setFocus(Qt::OtherFocusReason);
      QKeyEvent input(key->type(), key->key(), key->modifiers(), key->text(), key->isAutoRepeat(), key->count());
      QApplication::sendEvent(m_Command, &input);
      key->accept();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void EditorWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  // Reserve the tab strip before the first OpenGL composition, rather than waiting
  // for a viewport click to trigger a layout/refresh.
  m_Tabs->show(); m_Tabs->updateGeometry();
  if (centralWidget()->layout()) centralWidget()->layout()->activate();
  m_Tabs->update();
}

void EditorWindow::SwitchDrawing(int index) {
  if (index < 0 || index >= static_cast<int>(m_State.drawingTabs.size()) || index == m_State.activeDrawingIdx) return;
  CancelActiveCommand(m_State, m_Log);
  RefreshProjects();
  SaveDocumentToSnapshot(m_State, m_State.activeDrawingIdx);
  RestoreDocumentFromSnapshot(m_State, index);
  m_State.activeDrawingIdx = index;
  m_State.prevDrawingIdx = index;
  RefreshProjects();
  m_Viewport->InvalidateGeometry();
  Refresh();
}

void EditorWindow::NewDrawing(std::uint32_t projectUid) {
  RefreshProjects();
  SaveDocumentToSnapshot(m_State, m_State.activeDrawingIdx);
  const int index = static_cast<int>(m_State.drawingTabs.size());
  m_State.drawingTabs.push_back({"Drawing " + std::to_string(m_State.nextDrawingNumber++), m_State.nextTabUid++});
  m_State.drawingTabs.back().projectUid = projectUid;
  m_State.documents.emplace_back();
  RestoreDocumentFromSnapshot(m_State, index);
  m_State.activeDrawingIdx = index;
  m_State.prevDrawingIdx = index;
  LoadBundledBlockLibrary(m_State, m_Log);
  m_Viewport->InvalidateGeometry();
  Refresh();
}

void EditorWindow::OpenDrawing(const QString& path) {
  const auto found = gsproj::FindProjectFor(std::filesystem::path(path.toStdWString()));
  std::uint32_t uid = 0;
  if (found.state == gsproj::FindState::Damaged) {
    QMessageBox::warning(this, "Open drawing", QString::fromUtf8(found.message.c_str()));
    return;
  }
  if (found.state == gsproj::FindState::Found) {
    uid = OpenProject(QString::fromStdWString(found.file.wstring()), false);
    if (!uid) return;
  }
  const int previous = m_State.activeDrawingIdx;
  NewDrawing(uid);
  // Decide shared versus detached points from the loaded DWG, never from the
  // empty tab's initial database view. Otherwise opening could look like deletions.
  auto& openingTab = m_State.drawingTabs[m_State.activeDrawingIdx];
  openingTab.pointsMode = AppCommandState::DrawingTab::PointsMode::Unattached;
  openingTab.pointsBaseWorld.clear();
  openingTab.pointsRevision = 0;
  if (!OpenDrawingDocument(m_State, path.toUtf8().constData(), m_Log)) {
    m_State.drawingTabs.pop_back();
    m_State.documents.pop_back();
    RestoreDocumentFromSnapshot(m_State, previous);
    m_State.activeDrawingIdx = previous;
    m_State.prevDrawingIdx = previous;
    QMessageBox::warning(this, "Open drawing", "The drawing could not be opened. See the command log.");
  } else {
    m_State.activeDocFilePath = path.toUtf8().toStdString();
    if (uid) ApplyProjectFilesOnOpen(m_State, uid, m_State.activeDocFilePath, m_Log);
    m_State.activeDocSavedRevision = m_State.cadGpuRevision;
    m_State.drawingTabs[m_State.activeDrawingIdx].name = QFileInfo(path).completeBaseName().toUtf8().toStdString();
  }
  m_Viewport->InvalidateGeometry();
  Refresh();
}

bool EditorWindow::SaveDrawing(bool saveAs) {
  if (m_State.activeDrawingIdx == 0) return true;
  RefreshProjects();
  const auto projectUid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
  for (const auto& session : m_State.openProjects) {
    if (session.uid == projectUid && session.readOnly) {
      QMessageBox::warning(this, "Read-only project", "This project was opened read-only. The drawing cannot be saved into it.");
      return false;
    }
  }
  QString path = QString::fromUtf8(m_State.activeDocFilePath.c_str());
  if (saveAs || path.isEmpty()) {
    path = QFileDialog::getSaveFileName(this, "Save drawing", path, "Drawing (*.dwg)");
    if (path.isEmpty()) return false;
    if (!path.endsWith(".dwg", Qt::CaseInsensitive)) path += ".dwg";
  }
  if (!SaveDrawingDocument(m_State, path.toUtf8().constData(), m_Log)) {
    QMessageBox::warning(this, "Save drawing", "The drawing could not be saved. See the command log.");
    Refresh();
    return false;
  }
  m_State.activeDocFilePath = path.toUtf8().toStdString();
  m_State.activeDocSavedRevision = m_State.cadGpuRevision;
  SyncProjectFilesOnSave(m_State, m_State.activeDrawingIdx, path.toUtf8().toStdString(), m_Log);
  m_State.drawingTabs[m_State.activeDrawingIdx].name = QFileInfo(path).completeBaseName().toUtf8().toStdString();
  SaveDocumentToSnapshot(m_State, m_State.activeDrawingIdx);
  Refresh();
  return true;
}

bool EditorWindow::ConfirmSave() {
  if (m_State.activeDrawingIdx == 0 || m_State.cadGpuRevision == m_State.activeDocSavedRevision) return true;
  const auto answer = QMessageBox::question(this, "Unsaved drawing",
      "Save changes to " + QString::fromUtf8(m_State.drawingTabs[m_State.activeDrawingIdx].name.c_str()) + "?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Cancel) return false;
  return answer != QMessageBox::Save || SaveDrawing();
}

void EditorWindow::CloseDrawing(int index) {
  if (index <= 0) return;
  SwitchDrawing(index);
  if (!ConfirmSave()) return;
  const auto uid = m_State.drawingTabs[index].projectUid;
  if (uid && !ProjectsWithUnsavedPoints(m_State, uid, m_Log).empty()) {
    QMessageBox::warning(this, "Unsaved project points", "Project points could not be saved. Resolve the write error before closing this drawing.");
    return;
  }
  CancelActiveCommand(m_State, m_Log);
  m_State.drawingTabs.erase(m_State.drawingTabs.begin() + index);
  m_State.documents.erase(m_State.documents.begin() + index);
  const int next = std::min(index, static_cast<int>(m_State.drawingTabs.size()) - 1);
  RestoreDocumentFromSnapshot(m_State, next);
  m_State.activeDrawingIdx = next;
  m_State.prevDrawingIdx = next;
  for (auto it = m_State.openProjects.begin(); it != m_State.openProjects.end();) {
    const bool used = std::any_of(m_State.drawingTabs.begin(), m_State.drawingTabs.end(),
        [&](const auto& tab) { return tab.projectUid == it->uid; });
    if (used) { ++it; continue; }
    FlushProjectPointDb(*it, m_Log);
    if (!it->readOnly) gsproj::Release(it->project.file, it->me);
    it = m_State.openProjects.erase(it);
  }
  m_Viewport->InvalidateGeometry();
  Refresh();
}

void EditorWindow::closeEvent(QCloseEvent* event) {
  for (auto* child : findChildren<QMainWindow*>())
    if (child->parentWidget() == this && !child->close()) { event->ignore(); return; }
  const int previous = m_State.activeDrawingIdx;
  for (int i = 1; i < static_cast<int>(m_State.drawingTabs.size()); ++i) {
    SwitchDrawing(i);
    if (!ConfirmSave()) { SwitchDrawing(previous); event->ignore(); return; }
  }
  if (!ProjectsWithUnsavedPoints(m_State, 0, m_Log).empty()) {
    QMessageBox::warning(this, "Unsaved project points", "Project points could not be saved. Resolve the write error before exiting.");
    SwitchDrawing(previous);
    event->ignore();
    return;
  }
  QSettings settings;
  settings.setValue("editor/geometry", saveGeometry());
  settings.setValue("editor/layout", saveState(1));
  event->accept();
}

void EditorWindow::Refresh() {
  if (m_Refreshing) return;
  m_Refreshing = true;
  if (auto* annotationScale = findChild<QComboBox*>("statusAnnotationScale")) {
    const QSignalBlocker block(annotationScale);
    annotationScale->clear();
    for (const auto& scale : m_State.annotationScales)
      annotationScale->addItem(QString::fromUtf8(CadAnnotationScaleStatusLabel(scale).c_str()));
    annotationScale->setCurrentIndex(m_State.currentAnnotationScaleIndex);
    annotationScale->setEnabled(!m_State.annotationScales.empty());
  }
  RefreshProjects();
  const QSignalBlocker tabBlock(m_Tabs);
  while (m_Tabs->count() > static_cast<int>(m_State.drawingTabs.size())) m_Tabs->removeTab(m_Tabs->count() - 1);
  for (int i = 0; i < static_cast<int>(m_State.drawingTabs.size()); ++i) {
    QString title = QString::fromUtf8(m_State.drawingTabs[i].name.c_str());
    const bool dirty = i == m_State.activeDrawingIdx ? m_State.cadGpuRevision != m_State.activeDocSavedRevision :
        m_State.documents[i].cadGpuRevision != m_State.documents[i].savedRevision;
    if (i > 0 && dirty) title += " *";
    if (i == m_Tabs->count()) m_Tabs->addTab(title); else m_Tabs->setTabText(i, title);
  }
  m_Tabs->setTabButton(0, QTabBar::RightSide, nullptr);
  m_Tabs->setCurrentIndex(m_State.activeDrawingIdx);
  m_Tabs->setMinimumHeight(std::max(m_Tabs->sizeHint().height(), m_Tabs->fontMetrics().height() + 12));
  m_Pages->setCurrentIndex(m_State.activeDrawingIdx == 0 ? 0 : 1);
  while (m_LogShown < m_Log.size()) m_CommandBar->Append(QString::fromUtf8(m_Log[m_LogShown++].c_str()));
  QString commandHint;
  for (const auto* hint : {LineCommandFooterHint(m_State), CircleCommandFooterHint(m_State), ModifyCommandFooterHint(m_State),
      ScaleCommandFooterHint(m_State), RotateCommandFooterHint(m_State), DeleteCommandFooterHint(m_State),
      JoinCommandFooterHint(m_State), TrimCommandFooterHint(m_State), OffsetCommandFooterHint(m_State),
      AlignCommandFooterHint(m_State), ZoomCommandFooterHint(m_State), DrawingExtrasFooterHint(m_State)})
    if (hint && *hint) { commandHint = QString::fromUtf8(hint); break; }
  m_CommandBar->SetHint(commandHint);
  if (m_WindowTitle) {
    const auto title = m_Tabs->tabText(m_State.activeDrawingIdx) + QString::fromUtf8("  —  GoSurvey");
    m_WindowTitle->setText(title); setWindowTitle(title);
  }
  for (const auto& name : {"propertyAnnotative", "propertyAnnotationScales"})
    if (auto* widget = m_Properties->findChild<QWidget*>(name)) delete widget;
  m_Properties->clear();
  auto* general = new QTreeWidgetItem(m_Properties, {"General"});
  general->setFirstColumnSpanned(true);
  general->setBackground(0, palette().color(QPalette::Button)); general->setForeground(0, palette().color(QPalette::ButtonText));
  auto sectionFont = general->font(0); sectionFont.setBold(true); general->setFont(0, sectionFont);
  new QTreeWidgetItem(general, {"Selected", QString::number(m_State.selection.size())});
  new QTreeWidgetItem(general, {"Lines", QString::number(m_State.userLinesFlat.size() / 6)});
  new QTreeWidgetItem(general, {"Circles", QString::number(m_State.userCirclesCxCyZR.size() / 4)});
  new QTreeWidgetItem(general, {"File", QString::fromUtf8(m_State.activeDocFilePath.c_str())});
  general->setExpanded(true);
  if (m_State.selection.size() == 1) {
    const auto selected = m_State.selection.front();
    const int index = selected.index;
    bool* annotativeFlag = nullptr;
    std::vector<std::string>* visibleScaleNames = nullptr;
    if (selected.type == SelectedEntity::Type::Annotation && index >= 0 && index < static_cast<int>(m_State.cadAnnotations.size())) {
      annotativeFlag = &m_State.cadAnnotations[static_cast<size_t>(index)].annotative;
      visibleScaleNames = &m_State.cadAnnotations[static_cast<size_t>(index)].annotativeVisibleScaleNames;
    } else if (selected.type == SelectedEntity::Type::FilledRegion && index >= 0 && index < static_cast<int>(m_State.cadFilledRegions.size())) {
      annotativeFlag = &m_State.cadFilledRegions[static_cast<size_t>(index)].annotative;
      visibleScaleNames = &m_State.cadFilledRegions[static_cast<size_t>(index)].annotativeVisibleScaleNames;
    } else if (selected.type == SelectedEntity::Type::BlockRef && index >= 0 && index < static_cast<int>(m_State.cadBlockRefs.size())) {
      annotativeFlag = &m_State.cadBlockRefs[static_cast<size_t>(index)].annotative;
      visibleScaleNames = &m_State.cadBlockRefs[static_cast<size_t>(index)].annotativeVisibleScaleNames;
    } else if (selected.type == SelectedEntity::Type::Multileader && index >= 0 && index < static_cast<int>(m_State.cadMultileaders.size())) {
      annotativeFlag = &m_State.cadMultileaders[static_cast<size_t>(index)].annotative;
      visibleScaleNames = &m_State.cadMultileaders[static_cast<size_t>(index)].annotativeVisibleScaleNames;
    }
    if (annotativeFlag && visibleScaleNames) {
      auto* section = new QTreeWidgetItem(m_Properties, {"Annotation scale"});
      section->setFirstColumnSpanned(true);
      section->setBackground(0, palette().color(QPalette::Button));
      section->setForeground(0, palette().color(QPalette::ButtonText));
      section->setFont(0, sectionFont);
      auto* annotative = new QCheckBox("Annotative", m_Properties);
      annotative->setObjectName("propertyAnnotative"); annotative->setChecked(*annotativeFlag);
      m_Properties->setItemWidget(new QTreeWidgetItem(section, {"Annotative"}), 1, annotative);
      connect(annotative, &QCheckBox::toggled, this, [this, annotativeFlag](bool enabled) {
        if (*annotativeFlag == enabled) return;
        PushUndoSnapshot(m_State, "Annotative property");
        *annotativeFlag = enabled;
        BumpCadGpuCache(m_State); Refresh();
      });
      auto* scaleRow = new QTreeWidgetItem(section, {"Visible scales"});
      auto* scales = new QComboBox(m_Properties); scales->setObjectName("propertyAnnotationScales");
      auto* scaleModel = new QStandardItemModel(scales);
      for (const auto& scale : m_State.annotationScales) {
        auto* item = new QStandardItem(QString::fromUtf8(CadAnnotationScaleStatusLabel(scale).c_str()));
        item->setData(QString::fromUtf8(scale.name.c_str()), Qt::UserRole);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const bool visible = visibleScaleNames->empty() ||
            std::find(visibleScaleNames->begin(), visibleScaleNames->end(), scale.name) != visibleScaleNames->end();
        item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
        scaleModel->appendRow(item);
      }
      scales->setModel(scaleModel);
      scales->setEnabled(*annotativeFlag && !m_State.annotationScales.empty());
      m_Properties->setItemWidget(scaleRow, 1, scales);
      connect(scaleModel, &QStandardItemModel::itemChanged, this, [this, visibleScaleNames, scaleModel](QStandardItem* changed) {
        std::vector<std::string> visibleNames;
        int visibleCount = 0;
        for (int row = 0; row < scaleModel->rowCount(); ++row) {
          const auto* item = scaleModel->item(row);
          if (item->checkState() == Qt::Checked) {
            visibleNames.push_back(item->data(Qt::UserRole).toString().toUtf8().toStdString());
            ++visibleCount;
          }
        }
        if (!scaleModel->rowCount()) return;
        if (!visibleCount) {
          const QSignalBlocker block(scaleModel);
          changed->setCheckState(Qt::Checked);
          return;
        }
        if (visibleCount == scaleModel->rowCount()) visibleNames.clear();
        if (*visibleScaleNames == visibleNames) return;
        PushUndoSnapshot(m_State, "Annotative scale visibility");
        *visibleScaleNames = std::move(visibleNames);
        BumpCadGpuCache(m_State); Refresh();
      });
      section->setExpanded(true);
    }
  }
  const QSignalBlocker layerBlock(m_Layer);
  m_Layer->clear();
  for (const auto& layer : m_State.drawingLayerTable) m_Layer->addItem(QString::fromUtf8(layer.name.c_str()));
  const std::string selectedLayer = CadSelectionLayer(m_State);
  m_Layer->setCurrentText(QString::fromUtf8((selectedLayer.empty() ? m_State.currentLayer : selectedLayer).c_str()));
  auto* color = new QTreeWidgetItem(general, {"Color", QString::fromUtf8(
      (m_State.selection.empty() ? m_State.currentColor : CadSelectionColor(m_State)).c_str())});
  color->setFlags(color->flags() | Qt::ItemIsEditable);
  auto itemPath = [](QTreeWidgetItem* item) {
    QStringList parts; for (; item; item = item->parent()) parts.prepend(item->text(0));
    return parts.join(QChar(0x1f));
  };
  const bool initialTree = m_Toolspace->topLevelItemCount() == 0;
  QSet<QString> expanded;
  QString currentItem;
  for (QTreeWidgetItemIterator it(m_Toolspace); *it; ++it) {
    if ((*it)->isExpanded()) expanded.insert(itemPath(*it));
    if ((*it) == m_Toolspace->currentItem()) currentItem = itemPath(*it);
  }
  m_Toolspace->clear();
  auto* projects = new QTreeWidgetItem(m_Toolspace, {"Projects"});
  projects->setIcon(0, style()->standardIcon(QStyle::SP_DirHomeIcon));
  for (const auto& session : m_State.openProjects) {
    auto* project = new QTreeWidgetItem(projects, {QString::fromUtf8(session.project.name.c_str()) +
        (session.readOnly ? " (read-only)" : "")});
    project->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    for (const auto& item : session.project.items)
      new QTreeWidgetItem(project, {QString::fromUtf8(item.path.c_str())});
  }
  projects->setExpanded(true);
  auto* drawings = new QTreeWidgetItem(m_Toolspace, {"Drawings"});
  drawings->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
  for (int i = 1; i < static_cast<int>(m_State.drawingTabs.size()); ++i) {
    auto* drawing = new QTreeWidgetItem(drawings, {QString::fromUtf8(m_State.drawingTabs[i].name.c_str())});
    drawing->setData(0, Qt::UserRole, i);
    drawing->setIcon(0, CadCommandIcon("Active_File.png"));
  }
  drawings->setExpanded(true);
  auto* points = new QTreeWidgetItem(m_Toolspace, {"Survey points (" + QString::number(m_State.surveyPoints.size()) + ")"});
  points->setIcon(0, CadCommandIcon("Point.png"));
  auto* surfaces = new QTreeWidgetItem(m_Toolspace, {"Surfaces"});
  surfaces->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
  for (const auto& surface : m_State.cadSurfaces) new QTreeWidgetItem(surfaces, {QString::fromUtf8(surface.name.c_str())});
  auto* blocks = new QTreeWidgetItem(m_Toolspace, {"Blocks"});
  blocks->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
  for (const auto& block : m_State.blockDefs) new QTreeWidgetItem(blocks, {QString::fromUtf8(block.name.c_str())});
  if (!initialTree) for (QTreeWidgetItemIterator it(m_Toolspace); *it; ++it) {
    (*it)->setExpanded(expanded.contains(itemPath(*it)));
    if (itemPath(*it) == currentItem) m_Toolspace->setCurrentItem(*it);
  }
  RefreshRibbon();
  m_Viewport->update();
  m_Refreshing = false;
  ServiceUiRequests();
  if (m_State.mtextRichEditorOpen && !m_TextDialogOpen) {
    m_TextDialogOpen = true;
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, "Edit text", "Text contents", QString::fromUtf8(
        m_State.mtextRichEditorBuf.c_str()), &accepted);
    if (accepted) {
      m_State.mtextRichEditorBuf = text.toUtf8().toStdString();
      CommitMtextRichEditor(m_State, m_Log);
    } else CancelMtextRichEditor(m_State, &m_Log);
    m_TextDialogOpen = false;
    Refresh();
  }
}

void EditorWindow::ShowSelectionList() {
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName("selectionList");
  dialog->setWindowTitle("Selection");
  dialog->resize(360, 420);
  auto* layout = new QVBoxLayout(dialog);
  auto* list = new QListWidget(dialog);
  list->setObjectName("selectionSnapshotList");
  layout->addWidget(list, 1);
  auto* actions = new QHBoxLayout();
  auto* refresh = new QPushButton("Refresh", dialog);
  refresh->setObjectName("selectionRefresh");
  auto* selectAll = new QPushButton("Select All", dialog);
  selectAll->setObjectName("selectionSelectAll");
  auto* deselectAll = new QPushButton("Deselect All", dialog);
  deselectAll->setObjectName("selectionDeselectAll");
  actions->addWidget(refresh); actions->addWidget(selectAll); actions->addWidget(deselectAll);
  layout->addLayout(actions);
  auto* close = new QPushButton("Close", dialog);
  layout->addWidget(close);
  auto populate = [this, list] {
    QSignalBlocker blocker(list);
    list->clear();
    for (const SelectedEntity& entity : m_State.selection) {
      const char* kind = "Object";
      switch (entity.type) {
        case SelectedEntity::Type::LineSeg: kind = "Line"; break;
        case SelectedEntity::Type::Circle: kind = "Circle"; break;
        case SelectedEntity::Type::Polyline: kind = "Polyline"; break;
        case SelectedEntity::Type::Arc: kind = "Arc"; break;
        case SelectedEntity::Type::Ellipse: kind = "Ellipse"; break;
        case SelectedEntity::Type::Annotation: kind = "Annotation"; break;
        case SelectedEntity::Type::Table: kind = "Table"; break;
        case SelectedEntity::Type::BlockRef: kind = "Block"; break;
        case SelectedEntity::Type::Solid: kind = "3D Solid"; break;
        case SelectedEntity::Type::PipeRun: kind = "Pipe Run"; break;
        default: break;
      }
      auto* item = new QListWidgetItem(QString("%1 %2").arg(kind).arg(entity.index + 1), list);
      item->setData(Qt::UserRole, 0); item->setData(Qt::UserRole + 1, entity.index);
      item->setData(Qt::UserRole + 2, static_cast<int>(entity.type));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Checked);
    }
    for (int index : m_State.selectedSurveyPointIndices) {
      auto* item = new QListWidgetItem(QString("Survey point %1").arg(index + 1), list);
      item->setData(Qt::UserRole, 1); item->setData(Qt::UserRole + 1, index);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Checked);
    }
  };
  auto setChecked = [this, list](Qt::CheckState state) {
    for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(state);
    Refresh();
  };
  connect(refresh, &QPushButton::clicked, dialog, populate);
  connect(selectAll, &QPushButton::clicked, dialog, [setChecked] { setChecked(Qt::Checked); });
  connect(deselectAll, &QPushButton::clicked, dialog, [setChecked] { setChecked(Qt::Unchecked); });
  connect(close, &QPushButton::clicked, dialog, &QDialog::close);
  connect(list, &QListWidget::itemChanged, dialog, [this, list](QListWidgetItem* item) {
    const int index = item->data(Qt::UserRole + 1).toInt();
    const bool survey = item->data(Qt::UserRole).toInt() == 1;
    if (survey) {
      auto it = std::find(m_State.selectedSurveyPointIndices.begin(), m_State.selectedSurveyPointIndices.end(), index);
      if (item->checkState() == Qt::Checked && it == m_State.selectedSurveyPointIndices.end()) m_State.selectedSurveyPointIndices.push_back(index);
      else if (item->checkState() != Qt::Checked && it != m_State.selectedSurveyPointIndices.end()) m_State.selectedSurveyPointIndices.erase(it);
      SyncSurveyPointLinkedMtextSelection(m_State, index);
    } else {
      const auto type = static_cast<SelectedEntity::Type>(item->data(Qt::UserRole + 2).toInt());
      auto it = std::find_if(m_State.selection.begin(), m_State.selection.end(), [index, type](const SelectedEntity& e) { return e.index == index && e.type == type; });
      if (item->checkState() == Qt::Checked && it == m_State.selection.end()) {
        m_State.selection.push_back({static_cast<SelectedEntity::Type>(type), index});
      } else if (item->checkState() != Qt::Checked && it != m_State.selection.end()) m_State.selection.erase(it);
      if (type == SelectedEntity::Type::Annotation && index >= 0 && static_cast<std::size_t>(index) < m_State.cadAnnotations.size()) {
        const CadAnnotation& annotation = m_State.cadAnnotations[static_cast<std::size_t>(index)];
        if (annotation.kind == CadAnnotation::Kind::Mtext && annotation.surveyPointLabelForId >= 0) {
          const int pointIndex = SurveyPointIndexForId(m_State, annotation.surveyPointLabelForId);
          auto point = std::find(m_State.selectedSurveyPointIndices.begin(), m_State.selectedSurveyPointIndices.end(), pointIndex);
          if (pointIndex >= 0 && item->checkState() == Qt::Checked && point == m_State.selectedSurveyPointIndices.end())
            m_State.selectedSurveyPointIndices.push_back(pointIndex);
          else if (pointIndex >= 0 && item->checkState() != Qt::Checked && point != m_State.selectedSurveyPointIndices.end())
            m_State.selectedSurveyPointIndices.erase(point);
          if (pointIndex >= 0) SyncSurveyPointLinkedMtextSelection(m_State, pointIndex);
        }
      }
    }
    if (m_Viewport) m_Viewport->update();
    Refresh();
  });
  populate();
  dialog->show();
}

void EditorWindow::ShowWhatsNew() {
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("What's New in GoSurvey");
  dialog->resize(900, 650);
  auto* layout = new QVBoxLayout(dialog);
  auto* banner = new QLabel(dialog);
  banner->setPixmap(QPixmap(QtAsset("whats-new-bg.png")).scaledToWidth(860, Qt::SmoothTransformation));
  banner->setMaximumHeight(200);
  layout->addWidget(banner);
  auto* browser = new QTextBrowser(dialog);
  QFile markdown(QtAsset("whats-new.md"));
  if (markdown.open(QIODevice::ReadOnly)) browser->setMarkdown(QString::fromUtf8(markdown.readAll()));
  browser->setOpenExternalLinks(true);
  layout->addWidget(browser, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  layout->addWidget(buttons);
  dialog->show();
}

void EditorWindow::ShowPdf() { auto* window = new PdfWindow(this); window->show(); }
void EditorWindow::ShowAdjustment() {
  if (auto* existing = findChild<QMainWindow*>("adjustmentWindow")) { existing->show(); existing->raise(); return; }
  auto* window = new AdjustmentWindow(m_State, m_Log, this);
  window->drawingChanged = [this] { Refresh(); };
  window->setObjectName("adjustmentWindow");
  window->show();
}

bool EditorWindow::RunSmokeChecks(QString& error) {
  if (!m_Viewport->IsRendererReady()) { error = "OpenGL renderer failed to initialize"; return false; }
  Submit("LINE"); Submit("0,0"); Submit("10,0");
  CancelActiveCommand(m_State, m_Log);
  if (m_State.userLinesFlat.size() != 6) { error = "LINE did not create one segment"; return false; }
  Submit("UNDO");
  if (!m_State.userLinesFlat.empty()) { error = "UNDO did not remove segment"; return false; }
  Submit("REDO");
  if (m_State.userLinesFlat.size() != 6) { error = "REDO did not restore segment"; return false; }
  NewDrawing();
  if (!m_State.userLinesFlat.empty()) { error = "New document inherited geometry"; return false; }
  SwitchDrawing(1);
  if (m_State.userLinesFlat.size() != 6) { error = "Document switch lost geometry"; return false; }
  QTemporaryDir temporary;
  if (!temporary.isValid()) { error = "Cannot create smoke-test temporary directory"; return false; }
  const QString drawingPath = temporary.filePath("roundtrip.dwg");
  if (!SaveDrawingDocument(m_State, drawingPath.toUtf8().constData(), m_Log)) {
    error = "DWG smoke save failed"; return false;
  }
  auto loaded = std::make_unique<AppCommandState>();
  if (!OpenDrawingDocument(*loaded, drawingPath.toUtf8().constData(), m_Log) || loaded->userLinesFlat.size() != 6) {
    error = "DWG round trip lost line geometry"; return false;
  }
  const QString pdfPath = temporary.filePath("viewer.pdf");
  {
    QPdfWriter writer(pdfPath);
    QPainter painter(&writer);
    painter.drawText(100, 100, "GoSurvey Qt PDF smoke test");
  }
  auto pdf = pdfview::PdfDocument::Open(std::filesystem::path(pdfPath.toStdWString()));
  pdfview::Bitmap bitmap;
  if (!pdf.doc || pdf.doc->PageCount() != 1 || !pdf.doc->RenderPage(0, 256, 256, bitmap, [] { return false; }) || bitmap.bgra.empty()) {
    error = "PDF open/render smoke test failed"; return false;
  }
  TraverseData traverse;
  traverse.hasStartBearing = true;
  traverse.isClosedLoop = true;
  traverse.startStationId = 0;
  for (int station : {1, 2, 3, 0}) {
    TraverseLeg leg;
    leg.stationId = station;
    leg.hasHorizAngle = true; leg.horizAngleDeg = 90;
    leg.hasHorizDist = true; leg.horizDist = station == 1 ? 100.1 : 100;
    traverse.legs.push_back(leg);
  }
  ComputeTraverse(traverse);
  m_State.traverseData = traverse;
  auto* adjustment = new AdjustmentWindow(m_State, m_Log, this);
  if (!adjustment->RunSmokeChecks(error)) { delete adjustment; return false; }
  delete adjustment;
  Refresh();
  return true;
}
