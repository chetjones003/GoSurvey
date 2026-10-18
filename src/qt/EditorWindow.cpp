#include "EditorWindow.hpp"
#include "EditorChrome.hpp"
#include "QtAppearance.hpp"
#include <QActionGroup>
#include <QTreeWidgetItemIterator>
#include <QSet>
#include <QStyle>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QDebug>
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
#include "RecentDrawings.hpp"
#include "ThumbnailCache.hpp"
#include "WhatsNewLogic.hpp"
#include "Project.hpp"
#include "auth/AuthService.hpp"
#include "update/UpdateService.hpp"
#include "telemetry/TelemetryService.hpp"
#include "platform/HttpFetch.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include "MtextRichFormat.hpp"
#include "Version.hpp"
#include <QAction>
#include <QApplication>
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
#include <QUrl>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QPdfWriter>
#include <QPainter>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QClipboard>
#include <QDir>
#include <QFrame>
#include <exception>
#include <cstring>
#include <unordered_map>

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

namespace {
template <typename Action>
void RunUiActionSafely(Action&& action) noexcept {
  try {
    std::forward<Action>(action)();
  } catch (const std::exception& error) {
    qWarning() << "Recent item action failed:" << error.what();
  } catch (...) {
    qWarning() << "Recent item action failed with an unknown exception.";
  }
}

QPixmap ProjectCover(const QString& name) {
  QPixmap cover(360, 150);
  const uint hash = qHash(name);
  const QColor accent = QColor::fromHsv(0, 0, static_cast<int>(112 + hash % 24));
  cover.fill(QColor("#e5e6e7"));
  QPainter painter(&cover);
  painter.setRenderHint(QPainter::Antialiasing);
  QLinearGradient wash(0, 0, 360, 150);
  wash.setColorAt(0, QColor("#d8dadd"));
  wash.setColorAt(1, QColor("#f1f2f3"));
  painter.fillRect(cover.rect(), wash);
  // Project cover thumbnail: a folder with stacked drawing sheets, matching the legacy project
  // cards. A project has no single drawing thumbnail, so keep this image stable and self-contained.
  painter.setPen(Qt::NoPen);
  painter.setBrush(accent.darker(120));
  painter.drawRoundedRect(QRectF(135, 39, 98, 65), 9, 9);
  painter.setBrush(QColor("#ffffff"));
  painter.drawRoundedRect(QRectF(150, 27, 48, 61), 4, 4);
  painter.setPen(QPen(QColor("#b9c8d7"), 2));
  painter.drawLine(159, 44, 189, 44);
  painter.drawLine(159, 53, 186, 53);
  painter.drawLine(159, 62, 182, 62);
  painter.setPen(Qt::NoPen);
  painter.setBrush(accent);
  painter.drawRoundedRect(QRectF(132, 53, 98, 52), 8, 8);
  painter.setPen(QColor("#ffffff"));
  painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
  painter.drawText(QRect(130, 111, 102, 18), Qt::AlignCenter, "PROJECT");
  return cover;
}

QString PathKey(const std::filesystem::path& path) {
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(path, ec).lexically_normal();
  QString key = QString::fromStdWString((ec ? path : absolute).wstring()).toCaseFolded();
  key.replace('\\', '/');
  return key;
}

QPixmap ProjectThumbnail(const QString& name, const QString& projectPath) {
  gsproj::Project project;
  if (gsproj::Load(std::filesystem::path(projectPath.toStdWString()), &project, nullptr)) {
    std::unordered_map<std::string, QString> thumbsByDrawing;
    const auto dataDir = UserDataDirectory();
    const auto recentDrawings = recent::Load((dataDir.empty() ? std::filesystem::path(".") : dataDir) /
                                             "gosurvey-recent.json");
    for (const auto& drawing : recentDrawings)
      if (!drawing.thumb.empty())
        thumbsByDrawing.emplace(PathKey(std::filesystem::u8path(drawing.path)).toUtf8().toStdString(),
                                 QString::fromUtf8(drawing.thumb.c_str()));

    for (const auto& item : project.items) {
      std::filesystem::path drawingPath = std::filesystem::u8path(item.path);
      if (item.kind != gsproj::kKindLocalLink)
        drawingPath = project.Folder() / drawingPath;
      if (QString::fromStdWString(drawingPath.extension().wstring()).compare(".dwg", Qt::CaseInsensitive) != 0)
        continue;
      const auto it = thumbsByDrawing.find(PathKey(drawingPath).toUtf8().toStdString());
      if (it == thumbsByDrawing.end()) continue;
      const auto imagePath = (dataDir.empty() ? std::filesystem::path(".") : dataDir) /
                             "thumbnails" / std::filesystem::path(it->second.toStdWString());
      QPixmap image(QString::fromStdWString(imagePath.wstring()));
      if (image.isNull()) continue;
      const auto scaled = image.scaled(360, 150, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      return scaled.copy((scaled.width() - 360) / 2, (scaled.height() - 150) / 2, 360, 150);
    }
  }
  return ProjectCover(name);
}

QString DrawingProjectTag(const QString& drawingPath) {
  const auto found = gsproj::FindProjectFor(std::filesystem::path(drawingPath.toStdWString()));
  if (found.state == gsproj::FindState::Damaged) return "Project (damaged)";
  if (found.state != gsproj::FindState::Found) return "Standalone";
  gsproj::Project project;
  return gsproj::Load(found.file, &project, nullptr) ? QString::fromUtf8(project.name.c_str()) : "Project";
}

bool HasVisibleDrawingPreview(const QImage& image) {
  if (image.isNull()) return false;
  const int stepX = std::max(1, image.width() / 240);
  const int stepY = std::max(1, image.height() / 160);
  int visible = 0;
  int samples = 0;
  for (int y = 0; y < image.height(); y += stepY) {
    for (int x = 0; x < image.width(); x += stepX) {
      const auto c = image.pixelColor(x, y);
      const int high = std::max({c.red(), c.green(), c.blue()});
      const int low = std::min({c.red(), c.green(), c.blue()});
      if (high > 58 || high - low > 24) ++visible;
      ++samples;
    }
  }
  return samples > 0 && visible > samples / 1000;
}

void QueueRecentDrawing(AppCommandState& state, const std::string& path) {
  if (path.empty()) return;
  const auto dataDir = UserDataDirectory().empty() ? std::filesystem::path(".") : UserDataDirectory();
  recent::Note(dataDir / "gosurvey-recent.json", path, "", QDateTime::currentSecsSinceEpoch());
  state.pendingThumbnailPath = path;
  state.pendingThumbnailTabIdx = state.activeDrawingIdx;
}
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
  InitUpdates();
  setObjectName("GoSurveyQtEditor");
  setWindowTitle("GoSurvey");
  setWindowFlag(Qt::FramelessWindowHint);
  setMinimumSize(820, 540);
  setWindowIcon(QIcon(QtAsset("icons/app.ico")));
  resize(1500, 950);
  setDockNestingEnabled(true);
  // Tab zero is the Start page; a drawing becomes active only after the user opens one.
  m_State.activeDrawingIdx = 0;
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
  startLayout->setContentsMargins(16, 14, 16, 10);
  startLayout->setSpacing(12);
  const bool darkStart = palette().color(QPalette::Window).lightness() < 128;
  const QString panelColor = darkStart ? "#303234" : "#f0f1f2";
  const QString cardColor = darkStart ? "#303234" : "#ffffff";
  const QString edgeColor = darkStart ? "#55585b" : "#c5c8cb";
  const QString textColor = darkStart ? "#f0f1f2" : "#303234";
  auto* banner = new QFrame(start);
  banner->setObjectName("startScreenBanner");
  banner->setMinimumHeight(74);
  banner->setMaximumHeight(82);
  banner->setStyleSheet(QString("QFrame#startScreenBanner { background:%1; border:1px solid %2; border-radius:8px; }")
                            .arg(panelColor, edgeColor));
  auto* bannerLayout = new QHBoxLayout(banner);
  bannerLayout->setContentsMargins(16, 8, 16, 8);
  auto* logo = new QLabel(banner);
  logo->setPixmap(QIcon(QtAsset("icons/app.ico")).pixmap(46, 46));
  bannerLayout->addWidget(logo);
  auto* brand = new QVBoxLayout();
  auto* title = new QLabel("GoSurvey", banner);
  title->setStyleSheet(QString("font-size:26px; font-weight:600; color:%1; border:0;").arg(textColor));
  auto* tagline = new QLabel("Land surveying  ·  civil drafting  ·  CAD", banner);
  tagline->setStyleSheet(QString("color:%1; border:0;").arg(darkStart ? "#c4c6c8" : "#66696c"));
  brand->addWidget(title);
  brand->addWidget(tagline);
  bannerLayout->addLayout(brand);
  bannerLayout->addStretch();
  auto* versionBadge = new QLabel("v" + QString::fromUtf8(GOSURVEY_VERSION_FULL), banner);
  versionBadge->setStyleSheet("background:#3787d6; color:white; border-radius:9px; padding:5px 10px; font-weight:600;");
  bannerLayout->addWidget(versionBadge, 0, Qt::AlignVCenter);
  startLayout->addWidget(banner);

  auto* content = new QHBoxLayout();
  content->setSpacing(18);
  auto* actionsPanel = new QWidget(start);
  actionsPanel->setFixedWidth(184);
  auto* actions = new QVBoxLayout(actionsPanel);
  actions->setContentsMargins(0, 2, 0, 0);
  actions->setSpacing(8);
  auto* getStarted = new QLabel("Get Started", actionsPanel);
  getStarted->setStyleSheet("font-size:17px; font-weight:600;");
  actions->addWidget(getStarted);
  auto* create = new QPushButton(QIcon(QtAsset("icons/New.png")), "New drawing", start);
  auto* open = new QPushButton(QIcon(QtAsset("icons/Open.png")), "Open drawing...", start);
  auto* newProject = new QPushButton("New Project", start);
  newProject->setObjectName("newProjectButton");
  auto* openProject = new QPushButton("Open Project...", start);
  openProject->setObjectName("openProjectButton");
  for (auto* action : {create, open, newProject, openProject}) {
    action->setMinimumHeight(36);
    action->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    actions->addWidget(action);
  }
  auto* news = new QPushButton("What's new", start);
  news->setMinimumHeight(34);
  actions->addWidget(news);
  auto* resourcesHeading = new QLabel("Resources", actionsPanel);
  resourcesHeading->setStyleSheet("font-size:16px; font-weight:600; margin-top:8px;");
  actions->addWidget(resourcesHeading);
  auto* website = new QPushButton("Visit the website", actionsPanel);
  website->setFlat(true);
  website->setCursor(Qt::PointingHandCursor);
  website->setStyleSheet("text-align:left; color:#62a9ed; padding:2px;");
  connect(website, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl("https://chetjones003.github.io/GoSurvey"));
  });
  actions->addWidget(website);
  actions->addStretch();
  content->addWidget(actionsPanel);

  auto* centerPanel = new QWidget(start);
  auto* centerColumn = new QVBoxLayout(centerPanel);
  centerColumn->setContentsMargins(0, 2, 0, 0);
  centerColumn->setSpacing(8);
  auto* projectsHeading = new QLabel("Recent Projects", start);
  projectsHeading->setStyleSheet("font-size:17px; font-weight:600; border-bottom:1px solid #526476; padding-bottom:3px;");
  centerColumn->addWidget(projectsHeading);
  m_ProjectTiles = new QWidget(centerPanel);
  m_ProjectTiles->setObjectName("recentProjectTiles");
  centerColumn->addWidget(m_ProjectTiles);
  auto* recentHeader = new QHBoxLayout();
  auto* drawingsHeading = new QLabel("Recent Drawings", start);
  drawingsHeading->setStyleSheet("font-size:17px; font-weight:600; border-bottom:1px solid #526476; padding-bottom:3px;");
  recentHeader->addWidget(drawingsHeading, 1);
  auto* gridMode = new QToolButton(start);
  gridMode->setObjectName("recentGridMode");
  gridMode->setText("Grid");
  gridMode->setCheckable(true);
  gridMode->setChecked(true);
  auto* listMode = new QToolButton(start);
  listMode->setObjectName("recentListMode");
  listMode->setText("List");
  listMode->setCheckable(true);
  connect(gridMode, &QToolButton::clicked, this, [this, gridMode, listMode] {
    m_RecentListView = false;
    gridMode->setChecked(true);
    listMode->setChecked(false);
    RefreshRecentDrawings();
  });
  connect(listMode, &QToolButton::clicked, this, [this, gridMode, listMode] {
    m_RecentListView = true;
    gridMode->setChecked(false);
    listMode->setChecked(true);
    RefreshRecentDrawings();
  });
  recentHeader->addWidget(gridMode);
  recentHeader->addWidget(listMode);
  auto* sort = new QComboBox(start);
  sort->setObjectName("recentDrawingSort");
  sort->addItems({"Last opened", "Name"});
  sort->setMaximumWidth(120);
  recentHeader->addWidget(sort);
  m_RecentSearch = new QLineEdit(start);
  m_RecentSearch->setPlaceholderText("Search drawings");
  m_RecentSearch->setMaximumWidth(250);
  recentHeader->addWidget(m_RecentSearch);
  centerColumn->addLayout(recentHeader);
  m_RecentTiles = new QWidget(centerPanel);
  m_RecentTiles->setObjectName("recentDrawingTiles");
  centerColumn->addWidget(m_RecentTiles, 1);
  connect(m_RecentSearch, &QLineEdit::textChanged, this, [this] { RefreshRecentDrawings(); });
  connect(sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { RefreshRecentDrawings(); });
  content->addWidget(centerPanel, 1);

  auto* sidePanel = new QWidget(start);
  sidePanel->setObjectName("startConnectPanel");
  sidePanel->setFixedWidth(210);
  auto* sideColumn = new QVBoxLayout(sidePanel);
  sideColumn->setContentsMargins(0, 2, 0, 0);
  sideColumn->setSpacing(8);
  auto* connectHeading = new QLabel("Connect", sidePanel);
  connectHeading->setStyleSheet("font-size:17px; font-weight:600; border-bottom:1px solid #526476; padding-bottom:3px;");
  sideColumn->addWidget(connectHeading);
  auto* account = new QFrame(sidePanel);
  account->setObjectName("startAccountCard");
  account->setStyleSheet(QString("QFrame { background:%1; border:1px solid %2; border-radius:7px; }").arg(cardColor, edgeColor));
  auto* accountLayout = new QVBoxLayout(account);
  auto* signedOut = new QWidget(account);
  signedOut->setObjectName("startSignedOutAccount");
  auto* signedOutLayout = new QVBoxLayout(signedOut);
  signedOutLayout->setContentsMargins(0, 0, 0, 0);
  auto* accountCopy = new QLabel("Sign in to sync projects and access your GoSurvey account.", signedOut);
  accountCopy->setWordWrap(true);
  accountCopy->setObjectName("startAccountMessage");
  accountCopy->setStyleSheet("border:0;");
  signedOutLayout->addWidget(accountCopy);
  auto* signIn = new QPushButton("Sign In", signedOut);
  signIn->setObjectName("startSignInButton");
  signIn->setStyleSheet("QPushButton { background:#3787d6; color:white; border:0; border-radius:4px; padding:7px; }");
  connect(signIn, &QPushButton::clicked, this, [this] { m_State.authSignInRequested = true; });
  signedOutLayout->addWidget(signIn);
  accountLayout->addWidget(signedOut);
  auto* signedIn = new QWidget(account);
  signedIn->setObjectName("startSignedInAccount");
  auto* signedInLayout = new QVBoxLayout(signedIn);
  signedInLayout->setContentsMargins(0, 0, 0, 0);
  auto* welcome = new QLabel(signedIn);
  welcome->setObjectName("startAccountWelcome");
  signedInLayout->addWidget(welcome);
  auto* email = new QLabel(signedIn);
  email->setObjectName("startAccountEmail");
  email->setStyleSheet("border:0;");
  signedInLayout->addWidget(email);
  auto* signOut = new QPushButton("Sign Out", signedIn);
  signOut->setObjectName("startSignOutButton");
  connect(signOut, &QPushButton::clicked, this, [this] {
    m_State.authSignOutRequested = true;
    Refresh();
  });
  signedInLayout->addWidget(signOut);
  accountLayout->addWidget(signedIn);
  sideColumn->addWidget(account);
  auto* feedbackHeading = new QLabel("Help Me Improve This Product", sidePanel);
  feedbackHeading->setWordWrap(true);
  feedbackHeading->setStyleSheet("font-size:15px; font-weight:600; margin-top:8px;");
  sideColumn->addWidget(feedbackHeading);
  auto* feedbackCopy = new QLabel("Found a bug or have an idea? Open an issue on GitHub — every report helps.", sidePanel);
  feedbackCopy->setWordWrap(true);
  sideColumn->addWidget(feedbackCopy);
  auto* feedback = new QPushButton("Send Feedback", sidePanel);
  connect(feedback, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl("https://github.com/chetjones003/GoSurvey/issues/new"));
  });
  sideColumn->addWidget(feedback);
  sideColumn->addStretch();
  content->addWidget(sidePanel);
  startLayout->addLayout(content, 1);
  connect(create, &QPushButton::clicked, this, [this] { NewDrawing(); });
  connect(open, &QPushButton::clicked, this, [this] { Submit("OPEN"); });
  connect(newProject, &QPushButton::clicked, this, [this] { NewProject(); });
  connect(openProject, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(this, "Open project", {}, "GoSurvey project (*.gsproj)");
    if (!path.isEmpty()) OpenProject(path, true);
  });
  connect(news, &QPushButton::clicked, this, [this] { ShowWhatsNew(); });
  RefreshRecentDrawings();
  RefreshRecentProjects();
  start->setObjectName("startScreen");
  m_Pages->addWidget(start);

  auto* modelPage = new QWidget(m_Pages);
  auto* modelLayout = new QVBoxLayout(modelPage);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  m_Viewport = new EditorViewport(m_State, m_Log, modelPage);
  m_Viewport->changed = [this] { Refresh(); };
  m_Viewport->thumbnailReady = [this] {
    QTimer::singleShot(0, this, [this] { RefreshRecentDrawings(); });
  };
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
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] { m_State.showCreatePointsWindow = false; CancelActiveCommand(m_State, m_Log); m_Command->clear(); Refresh(); });
  connect(new QShortcut(QKeySequence(Qt::Key_F3), this), &QShortcut::activated, osnap, &QToolButton::toggle);
  connect(new QShortcut(QKeySequence(Qt::Key_F7), this), &QShortcut::activated, grid, &QToolButton::toggle);
  connect(new QShortcut(QKeySequence(Qt::Key_F8), this), &QShortcut::activated, ortho, &QToolButton::toggle);
  auto* service = new QTimer(this);
  service->setInterval(33);
  connect(service, &QTimer::timeout, this, [this] {
    ServiceAuth();
    ServiceTelemetry();
    ServiceUpdate();
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
  } else if (!UpdateBlocksLaunch() && HasInternetConnectivity()) {
    ShowSignInGate();
  }
  MaybeAutoOpenWhatsNew();
}

void EditorWindow::EnableTelemetry() {
  // The launch-time channel, as the ImGui build sends: a Settings change applies from next launch.
  const std::string channel = m_State.updatePrefs.useBetaChannel ? "beta" : "stable";
  m_SendTelemetry = [this, channel](const std::string& email) {
    m_TelemetryTask = telemetry::BeginTelemetryPing(GOSURVEY_VERSION_FULL, channel, email);
  };
}

void EditorWindow::ServiceTelemetry() {
  telemetry::PollTelemetryTask(m_TelemetryTask);
  // REQ-080: one ping per launch, fired once the launch gate resolves (signed in, opted out, or
  // the offline exception) so the sign-in state it reports is known. It never waits on sign-in
  // beyond that and never shows anything; failures are dropped inside the worker.
  if (!m_AuthStarted || !m_State.authGateResolved || m_TelemetrySent || !m_SendTelemetry)
    return;
  m_TelemetrySent = true;
  m_SendTelemetry(m_State.authSignedIn ? m_State.authEmail : std::string());
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

void EditorWindow::InitializeStartup() {
  m_StartupInitialized = true;
  // REQ-078 before REQ-336: when the startup check found an update, What's New does not
  // auto-open on this launch. The check has already answered by now (the splash waits for it).
  if (UpdateBlocksLaunch()) m_WhatsNewAutoOpened = true;
  MaybeAutoOpenWhatsNew();
}

void EditorWindow::MaybeAutoOpenWhatsNew() {
  if (!m_StartupInitialized || m_WhatsNewAutoOpened || !m_State.authGateResolved ||
      m_State.activeDrawingIdx != 0 ||
      !WhatsNewShouldAutoOpen(GOSURVEY_VERSION_FULL, m_State.whatsNewDismissedVersion, false))
    return;
  m_WhatsNewAutoOpened = true;
  ShowWhatsNew();
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
  auto* whatsNewAction = help->addAction("What's new", this, [this] { ShowWhatsNew(); });
  whatsNewAction->setObjectName("helpWhatsNewAction");
  auto* aboutAction = help->addAction("About GoSurvey", this, [this] { ShowWhatsNew(); });
  aboutAction->setObjectName("aboutGoSurveyAction");
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
  RefreshRecentProjects();
  RefreshRecentDrawings();
}

void EditorWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (auto* sidePanel = findChild<QWidget*>("startConnectPanel"))
    sidePanel->setVisible(width() >= 1120);
  QTimer::singleShot(0, this, [this] {
    RefreshRecentProjects();
    RefreshRecentDrawings();
  });
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
    std::error_code ec;
    const auto abs = std::filesystem::absolute(std::filesystem::path(path.toStdWString()), ec);
    const auto recentPath = ec ? std::filesystem::path(path.toStdWString()) : abs;
    QueueRecentDrawing(m_State, QString::fromStdWString(recentPath.wstring()).toUtf8().toStdString());
    RefreshRecentDrawings();
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
  std::error_code ec;
  const auto abs = std::filesystem::absolute(std::filesystem::path(path.toStdWString()), ec);
  const auto recentPath = ec ? std::filesystem::path(path.toStdWString()) : abs;
  QueueRecentDrawing(m_State, QString::fromStdWString(recentPath.wstring()).toUtf8().toStdString());
  SaveDocumentToSnapshot(m_State, m_State.activeDrawingIdx);
  RefreshRecentDrawings();
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
  // REQ-078: the installer starts only once every save prompt is settled, as the last step
  // before closing. If it cannot start (the user declined elevation, or the launch failed) the
  // application stays open and the update dialog reports why, rather than quitting without
  // having updated.
  if (m_UpdateExitPending && !m_LaunchInstaller(*m_Update)) {
    m_UpdateExitPending = false;
    // A declined elevation prompt reports "back to the offer", but the installer is already
    // downloaded and verified: offer Install again, not a second download.
    if (m_Update->phase == update::Phase::UpdateReady) m_Update->phase = update::Phase::ReadyToLaunch;
    SwitchDrawing(previous);
    event->ignore();
    return;
  }
  event->accept();
}

void EditorWindow::Refresh() {
  if (m_Refreshing) return;
  m_Refreshing = true;
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
  const bool drawingActive = m_State.activeDrawingIdx > 0;
  if (auto* toolspaceDock = findChild<QDockWidget*>("toolspaceDock"))
    toolspaceDock->setVisible(drawingActive);
  if (auto* propertiesDock = findChild<QDockWidget*>("propertiesDock"))
    propertiesDock->setVisible(drawingActive);
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
  if (auto* signedIn = findChild<QWidget*>("startSignedInAccount")) {
    signedIn->setVisible(m_State.authSignedIn);
    if (auto* signedOut = findChild<QWidget*>("startSignedOutAccount"))
      signedOut->setVisible(!m_State.authSignedIn);
    if (m_State.authSignedIn) {
      QString accountName = QString::fromUtf8(m_State.authEmail.c_str()).section('@', 0, 0);
      if (!accountName.isEmpty()) accountName[0] = accountName[0].toUpper();
      if (auto* welcome = findChild<QLabel*>("startAccountWelcome"))
        welcome->setText("Welcome " + (accountName.isEmpty() ? QString("GoSurvey user") : accountName) + "!");
      if (auto* email = findChild<QLabel*>("startAccountEmail"))
        email->setText(QString::fromUtf8(m_State.authEmail.c_str()));
    }
  }
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

void EditorWindow::ShowWhatsNew() {
  if (auto* existing = findChild<QDialog*>("whatsNewDialog")) {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new QDialog(this);
  dialog->setObjectName("whatsNewDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("What's New in GoSurvey");
  dialog->resize(820, 610);
  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(24, 20, 24, 16);
  layout->setSpacing(12);
  auto* heading = new QLabel("What's New", dialog);
  heading->setObjectName("whatsNewHeading");
  QFont headingFont = heading->font();
  headingFont.setPointSize(20);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  layout->addWidget(heading);
  auto* version = new QLabel("GoSurvey " + QString::fromUtf8(GOSURVEY_VERSION_FULL), dialog);
  version->setObjectName("whatsNewVersion");
  version->setStyleSheet("color:#62758a;");
  layout->addWidget(version);
  auto* browser = new QTextBrowser(dialog);
  browser->setObjectName("whatsNewReleaseNotes");
  browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  QFile markdown(QtAsset("whats-new.md"));
  QString notes;
  if (markdown.open(QIODevice::ReadOnly)) {
    notes = QString::fromUtf8(markdown.readAll());
    // Register bundled images under the markdown's `whats-new:` URLs. QTextBrowser has no
    // handler for that scheme by default, and scaling wide screenshots keeps them inside the pane.
    const QRegularExpression imageUrl(R"(whats-new:([A-Za-z0-9_.-]+))");
    auto match = imageUrl.globalMatch(notes);
    QStringList imageNames;
    while (match.hasNext()) {
      const auto found = match.next();
      const QString name = found.captured(1);
      if (!imageNames.contains(name)) imageNames.push_back(name);
    }
    for (const auto& name : imageNames) {
      QImage image(QtAsset("whats-new/" + name));
      if (image.isNull()) continue;
      if (image.width() > 460) image = image.scaledToWidth(460, Qt::SmoothTransformation);
      browser->document()->addResource(QTextDocument::ImageResource, QUrl("whats-new:" + name), image);
    }
  }
  if (notes.trimmed().isEmpty())
    notes = "What's New content could not be loaded from resources/whats-new.md for GoSurvey " +
            QString::fromUtf8(GOSURVEY_VERSION_FULL) + ". You can view the release notes online below.";
  browser->setMarkdown(notes);
  browser->setOpenExternalLinks(true);
  layout->addWidget(browser, 1);
  auto* releases = new QLabel(
      "<a href=\"https://github.com/chetjones003/GoSurvey/releases\">View all GoSurvey releases</a>", dialog);
  releases->setObjectName("whatsNewReleasesLink");
  releases->setOpenExternalLinks(true);
  layout->addWidget(releases);
  auto* dontShow = new QCheckBox("Don't show this again for GoSurvey " + QString::fromUtf8(GOSURVEY_VERSION_FULL), dialog);
  dontShow->setObjectName("whatsNewDontShowAgain");
  dontShow->setChecked(WhatsNewDismissCheckboxChecked(GOSURVEY_VERSION_FULL, m_State.whatsNewDismissedVersion));
  layout->addWidget(dontShow);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  connect(dialog, &QDialog::finished, this, [this, dontShow] {
    m_State.whatsNewDismissedVersion = dontShow->isChecked() ? GOSURVEY_VERSION_FULL : "";
    (void)SaveUserStartupPrefs(m_State);
  });
  layout->addWidget(buttons);
  dialog->show();
}

void EditorWindow::RefreshRecentDrawings() {
  if (!m_RecentTiles) return;
  if (auto* old = m_RecentTiles->layout()) {
    while (auto* item = old->takeAt(0)) {
      if (item->widget()) item->widget()->deleteLater();
      delete item;
    }
    delete old;
  }
  auto* grid = new QGridLayout(m_RecentTiles);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(8);
  grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  const auto dir = UserDataDirectory().empty() ? std::filesystem::path(".") : UserDataDirectory();
  auto entries = recent::Load(dir / "gosurvey-recent.json");
  const QString query = m_RecentSearch ? m_RecentSearch->text().trimmed() : QString();
  if (!query.isEmpty())
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
      return !QString::fromUtf8(entry.name.c_str()).contains(query, Qt::CaseInsensitive) &&
             !QString::fromUtf8(entry.path.c_str()).contains(query, Qt::CaseInsensitive);
    }), entries.end());
  if (auto* sort = findChild<QComboBox*>("recentDrawingSort"); sort && sort->currentIndex() == 1)
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  if (auto* gridMode = findChild<QToolButton*>("recentGridMode")) gridMode->setChecked(!m_RecentListView);
  if (auto* listMode = findChild<QToolButton*>("recentListMode")) listMode->setChecked(m_RecentListView);
  const int columns = m_RecentListView ? 1 : std::max(1, m_RecentTiles->width() / 168);
  const int rowHeight = m_RecentListView ? 54 : 128;
  const int rowsThatFit = std::max(1, m_RecentTiles->height() / rowHeight);
  const int maxVisible = std::min(12, columns * rowsThatFit);
  const int visibleEntries = static_cast<int>(std::min(entries.size(), static_cast<size_t>(maxVisible)));
  const auto recentDrawingStore = std::make_shared<const std::filesystem::path>(dir / "gosurvey-recent.json");
  int i = 0;
  for (const auto& entry : entries) {
    if (i >= visibleEntries) break;
    const QString name = QString::fromUtf8(entry.name.c_str());
    const QString path = QString::fromUtf8(entry.path.c_str());
    const QString lastOpened = QDateTime::fromSecsSinceEpoch(entry.lastOpenedUnix).toString("ddd, MMM dd yyyy");
    const QString projectTag = DrawingProjectTag(path);
    auto* tile = new QToolButton(m_RecentTiles);
    tile->setObjectName("recentDrawingTile");
    tile->setToolButtonStyle(m_RecentListView ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextUnderIcon);
    tile->setText(m_RecentListView ? name + "\n" + lastOpened + " · " + projectTag
                                   : name + "\n" + lastOpened + " · " + projectTag);
    tile->setToolTip(name + "\n" + path + "\n" + lastOpened + " · " + projectTag);
    tile->setIconSize(m_RecentListView ? QSize(72, 40) : QSize(144, 60));
    if (m_RecentListView) {
      tile->setMinimumHeight(50);
      tile->setMaximumHeight(54);
      tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
      tile->setFixedSize(158, 128);
    }
    const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
    const QString cardBg = dark ? "#303234" : "#ffffff";
    const QString cardEdge = dark ? "#55585b" : "#c5c8cb";
    const QString cardHover = dark ? "#3b3d3f" : "#f0f1f2";
    tile->setStyleSheet(QString("QToolButton#recentDrawingTile { text-align:left; padding:5px; border:1px solid %1; border-radius:6px; background:%2; } QToolButton#recentDrawingTile:hover { border-color:#65a9ec; background:%3; }")
                            .arg(cardEdge, cardBg, cardHover));
    const auto thumbPath = dir / "thumbnails" / std::filesystem::path(QString::fromUtf8(entry.thumb.c_str()).toStdWString());
    const QImage savedThumbnail(QString::fromStdWString(thumbPath.wstring()));
    QPixmap thumbnail;
    if (HasVisibleDrawingPreview(savedThumbnail)) {
      thumbnail = QPixmap::fromImage(savedThumbnail);
    } else {
      thumbnail = QPixmap(320, 144);
      thumbnail.fill(QColor("#303234"));
      QPainter painter(&thumbnail);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(QColor("#c8cbce"));
      painter.setFont(QFont("Segoe UI", 26, QFont::DemiBold));
      painter.drawText(thumbnail.rect(), Qt::AlignCenter, "DWG");
    }
    tile->setIcon(QIcon(thumbnail));
    const auto openRecentDrawing = [this, path, recentDrawingStore]() noexcept {
      RunUiActionSafely([&] {
        const auto before = m_State.drawingTabs.size();
        OpenDrawing(path);
        if (m_State.drawingTabs.size() == before &&
            QMessageBox::question(this, "Remove Recent Drawing?",
                                  "This drawing could not be opened. Remove it from Recent Drawings?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
          recent::Remove(*recentDrawingStore, path.toUtf8().toStdString());
          RefreshRecentDrawings();
        }
      });
    };
    tile->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tile, &QWidget::customContextMenuRequested, this,
            [this, tile, path, recentDrawingStore, openRecentDrawing](const QPoint& position) noexcept {
      RunUiActionSafely([&] {
      const bool fileExists = QFileInfo::exists(path);
      QMenu menu(tile);
      auto* openAction = menu.addAction("Open");
      openAction->setEnabled(fileExists);
      auto* folderAction = menu.addAction("Open Containing Folder", this, [path, fileExists]() noexcept {
        RunUiActionSafely([&] {
          if (fileExists) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
      });
      folderAction->setEnabled(fileExists);
      menu.addAction("Copy Full Path", this, [path]() noexcept {
        RunUiActionSafely([&] { QApplication::clipboard()->setText(path); });
      });
      menu.addSeparator();
      menu.addAction("Remove From List", this, [this, path, recentDrawingStore]() noexcept {
        RunUiActionSafely([&] {
          recent::Remove(*recentDrawingStore, path.toUtf8().toStdString());
          RefreshRecentDrawings();
        });
      });
      menu.addAction("Clear All Recent…", this, [this, recentDrawingStore]() noexcept {
        RunUiActionSafely([&] {
          if (QMessageBox::question(this, "Clear Recent Drawings?",
                                    "Remove every drawing from the Recent list?\nThis cannot be undone.",
                                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
            recent::Clear(*recentDrawingStore);
            RefreshRecentDrawings();
          }
        });
      });
      connect(openAction, &QAction::triggered, this, openRecentDrawing);
      menu.exec(tile->mapToGlobal(position));
      });
    });
    connect(tile, &QToolButton::clicked, this, openRecentDrawing);
    grid->addWidget(tile, i / columns, i % columns);
    ++i;
  }
  if (!i) grid->addWidget(new QLabel("Your recent drawings will appear here.", m_RecentTiles), 0, 0);
}

void EditorWindow::RefreshRecentProjects() {
  if (!m_ProjectTiles) return;
  if (auto* old = m_ProjectTiles->layout()) {
    while (auto* item = old->takeAt(0)) {
      if (item->widget()) item->widget()->deleteLater();
      delete item;
    }
    delete old;
  }
  auto* grid = new QGridLayout(m_ProjectTiles);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(9);
  grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  const auto dataDir = UserDataDirectory();
  const auto store = (dataDir.empty() ? std::filesystem::path(".") : dataDir) /
                     "gosurvey-recent-projects.json";
  const auto recentProjectStore = std::make_shared<const std::filesystem::path>(store);
  auto entries = recent::Load(store);
  if (entries.size() > 6) entries.resize(6);
  const int columns = std::max(1, std::min(3, m_ProjectTiles->width() / 174));
  int index = 0;
  for (const auto& entry : entries) {
    const QString name = QString::fromUtf8(entry.name.c_str());
    const QString path = QString::fromUtf8(entry.path.c_str());
    const QString lastOpened = QDateTime::fromSecsSinceEpoch(entry.lastOpenedUnix).toString("ddd, MMM dd yyyy");
    const QString projectFolder = QFileInfo(path).absolutePath();
    auto* tile = new QToolButton(m_ProjectTiles);
    tile->setObjectName("recentProjectTile");
    tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const QString folderLabel = tile->fontMetrics().elidedText(QDir::toNativeSeparators(projectFolder),
                                                               Qt::ElideMiddle, 150);
    tile->setText(name + "\n" + lastOpened + "\n" + folderLabel);
    tile->setIcon(QIcon(ProjectThumbnail(name, path)));
    tile->setIconSize(QSize(150, 54));
    tile->setFixedSize(166, 132);
    const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
    tile->setStyleSheet(dark
        ? "QToolButton { text-align:left; padding:5px; border:1px solid #55585b; border-radius:6px; background:#303234; } QToolButton:hover { border-color:#85898d; background:#3b3d3f; }"
        : "QToolButton { text-align:left; padding:5px; border:1px solid #c5c8cb; border-radius:6px; background:#ffffff; } QToolButton:hover { border-color:#92979c; background:#f0f1f2; }");
    tile->setToolTip(name + "\n" + projectFolder + "\n" + lastOpened);
    const auto openRecentProject = [this, path, recentProjectStore]() noexcept {
      RunUiActionSafely([&] {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::path(path.toStdWString()), ec)) {
          if (QMessageBox::question(this, "Remove Recent Project?",
                                    "This project file could not be found. Remove it from Recent Projects?",
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            recent::Remove(*recentProjectStore, path.toUtf8().toStdString());
            RefreshRecentProjects();
          }
          return;
        }
        OpenProject(path, true);
      });
    };
    tile->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tile, &QWidget::customContextMenuRequested, this,
            [this, tile, path, recentProjectStore, openRecentProject](const QPoint& position) noexcept {
      RunUiActionSafely([&] {
      QMenu menu(tile);
      menu.addAction("Open", this, openRecentProject);
      menu.addAction("Remove From List", this, [this, path, recentProjectStore]() noexcept {
        RunUiActionSafely([&] {
          recent::Remove(*recentProjectStore, path.toUtf8().toStdString());
          RefreshRecentProjects();
        });
      });
      menu.exec(tile->mapToGlobal(position));
      });
    });
    connect(tile, &QToolButton::clicked, this, openRecentProject);
    grid->addWidget(tile, index / columns, index % columns);
    ++index;
  }
  if (!index)
    grid->addWidget(new QLabel("No recent projects. Use New Project or Open Project.", m_ProjectTiles), 0, 0);
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
