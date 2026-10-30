#include "EditorWindow.hpp"
#include "DrawingDocumentTabBar.hpp"
#include "QtAccent.hpp"
#include "EditorChrome.hpp"
#include "QtAppearance.hpp"
#include "TopographicArtwork.hpp"
#include <QActionGroup>
#include <QScrollArea>
#include <QTreeWidgetItemIterator>
#include <QSet>
#include <QStyle>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QAbstractSpinBox>
#include <QDebug>
#include <QElapsedTimer>
#include <QTextEdit>
#include <QKeySequenceEdit>
#include <QShowEvent>
#include <utility>
#include "EditorViewport.hpp"
#include "AdjustmentWindow.hpp"
#include "QtText.hpp"
#include "PdfWindow.hpp"
#include "SurveyPoints.hpp"
#include "CadBlocks.hpp"
#include "DwgIo.hpp"
#include "GsIo.hpp"
#include "AppPaths.hpp"
#include "geo/CoordinateSystems.hpp"
#include "UserPrefs.hpp"
#include "ViewportZoomScale.hpp"
#include <QInputDialog>
#include "RecentDrawings.hpp"
#include "ThumbnailCache.hpp"
#include "WhatsNewLogic.hpp"
#include "Project.hpp"
#include "update/UpdateService.hpp"
#include "telemetry/TelemetryService.hpp"
#include "platform/HttpFetch.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include "util/PlotScales.hpp"
#include "MtextRichFormat.hpp"
#include "Version.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QComboBox>
#include <QAbstractItemView>
#include <QHash>
#include <QListView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QImage>
#include <QKeyEvent>
#include <QShortcut>
#include <QSizeGrip>
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
#include <QUrl>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QPdfWriter>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QClipboard>
#include <QDir>
#include <QFrame>
#include <exception>
#include <cstring>
#include <array>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

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
class VisibleSizeGrip final : public QSizeGrip {
public:
  explicit VisibleSizeGrip(QWidget* parent) : QSizeGrip(parent) {
    setObjectName("editorResizeGrip");
    setFixedSize(24, 24);
    setCursor(Qt::SizeFDiagCursor);
    setToolTip("Drag to resize window");
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#9aabb8"), 1.6, Qt::SolidLine, Qt::RoundCap));
    for (int offset : {0, 5, 10})
      painter.drawLine(width() - 4 - offset, height() - 3,
                       width() - 3, height() - 4 - offset);
  }
};

class TopographicTreeWidget final : public QTreeWidget {
 public:
  explicit TopographicTreeWidget(bool rightCorner, QWidget* parent = nullptr)
      : QTreeWidget(parent), m_RightCorner(rightCorner) {}

 protected:
  void paintEvent(QPaintEvent* event) override {
    QTreeWidget::paintEvent(event);
    if (qApp->property("cadLightTheme").toBool()) return;
    // The tree owns the background. Exclude occupied rows so decoration can never
    // cover a value, selection highlight, or an expanded project entry.
    QRegion openBackground(viewport()->rect());
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
      const QRect row = visualItemRect(*it);
      if (row.isEmpty()) continue;
      QRect content = row;
      if (m_RightCorner && !(*it)->isSelected())
        content.setRight(qMin(row.right(), row.left() + fontMetrics().horizontalAdvance((*it)->text(0)) + 30));
      openBackground -= content.adjusted(-2, -1, 2, 1);
    }
    QPainter painter(viewport());
    painter.setClipRegion(openBackground);
    const qreal artWidth = viewport()->width() * (m_RightCorner ? .82 : 1.0);
    const qreal artHeight = artWidth * (m_RightCorner ? 120.0 / 220.0 : 220.0 / 300.0);
    const QRectF area(m_RightCorner ? viewport()->width() - artWidth : 0,
                      viewport()->height() - artHeight, artWidth, artHeight);
    if (m_RightCorner) qtArtwork::DrawRisingContours(painter, area);
    else qtArtwork::DrawTerrainContours(painter, area);
  }

 private:
  bool m_RightCorner;
};

class TopographicStartBody final : public QWidget {
 public:
  explicit TopographicStartBody(QWidget* parent = nullptr) : QWidget(parent) {}

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    const bool dark = !qApp->property("cadLightTheme").toBool();
    painter.fillRect(rect(), dark ? QColor("#1c2024") : QColor("#f5f6f7"));
    if (!dark) return;
    const qreal cornerWidth = width() * .35;
    const qreal cornerHeight = cornerWidth * 220.0 / 300.0;
    qtArtwork::DrawTerrainContours(painter,
        QRectF(width() - cornerWidth, -cornerHeight * .30, cornerWidth, cornerHeight), true, .70);
    qtArtwork::DrawTerrainContours(painter,
        QRectF(0, height() - cornerHeight, cornerWidth, cornerHeight), false, .75);
  }

};

struct DrawingTemplateChoice {
  QString label;
  QString path;
  bool blank = false;
};

QVector<DrawingTemplateChoice> DiscoverDrawingTemplates() {
  QVector<DrawingTemplateChoice> choices;
  choices.push_back({"Blank Drawing", {}, true});

  const auto bundled = ResolveBundledAssetPath(std::filesystem::path("resources") / "drawing-templates");
  const auto userData = UserDataDirectory();
  const std::array<std::pair<std::filesystem::path, QString>, 2> roots{{
      {bundled, "Bundled"},
      {userData.empty() ? std::filesystem::path{} : userData / "drawing-templates", "Personal"}}};
  for (const auto& [root, origin] : roots) {
    if (root.empty()) continue;
    std::error_code ec;
    std::filesystem::directory_iterator it(root, ec), end;
    if (ec) continue;
    for (; it != end; it.increment(ec)) {
      if (ec) { ec.clear(); continue; }
      std::error_code fileError;
      if (!it->is_regular_file(fileError) || fileError) continue;
      QString extension = QString::fromStdWString(it->path().extension().wstring());
      if (extension.compare(".gst", Qt::CaseInsensitive) != 0) continue;
      const QString stem = QString::fromStdWString(it->path().stem().wstring());
      const QString path = QString::fromStdWString(it->path().wstring());
      choices.push_back({QString("%1 — %2").arg(stem, origin), path, false});
    }
  }
  std::sort(choices.begin() + 1, choices.end(), [](const auto& a, const auto& b) {
    const int byLabel = QString::compare(a.label, b.label, Qt::CaseInsensitive);
    return byLabel == 0 ? a.path.compare(b.path, Qt::CaseInsensitive) < 0 : byLabel < 0;
  });
  return choices;
}

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
  } else {
    // Keep command artwork neutral-light on the graphite workspace while preserving semantic colors
    // such as the amber accents in the supplied icon set.
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
      const auto color = image.pixelColor(x, y);
      const int high = std::max({color.red(), color.green(), color.blue()});
      const int low = std::min({color.red(), color.green(), color.blue()});
      if (color.alpha() && high > 150 && high - low < 32)
        image.setPixelColor(x, y, QColor(226, 230, 234, color.alpha()));
    }
  }
  return QIcon(QPixmap::fromImage(image));
}

QString NormalizedCommandIconName(QString name) {
  name = name.toLower();
  name.remove(QRegularExpression("[^a-z0-9]"));
  return name;
}

QString GeneratedCommandIconFamily(const QString& name, const QString& description) {
  const QString text = (name + " " + description).toLower();
  if (text.contains("surface") || text.contains("terrain") || text.contains("grading") ||
      text.contains("contour") || text.contains("watershed") || text.contains("catchment")) return "terrain";
  if (text.contains("point") || text.contains("survey") || text.contains("traverse") ||
      text.contains("station") || text.contains("bearing") || text.contains("elevation")) return "survey";
  if (text.contains("layer")) return "layers";
  if (text.contains("dimension") || text.contains("measure") || text.contains("distance") ||
      text.contains("volume") || text.contains("massprop") || text.contains("angle")) return "dimension";
  if (text.contains("block") || text.contains("solid") || text.contains("mesh") ||
      text.contains("box") || text.contains("cylinder") || text.contains("sphere")) return "solid";
  if (text.contains("import") || text.contains("export") || text.contains("open") ||
      text.contains("save") || text.contains("file") || text.contains("project")) return "file";
  if (text.contains("view") || text.contains("zoom") || text.contains("orbit") ||
      text.contains("camera") || text.contains("perspective") || text.contains("fov")) return "view";
  if (text.contains("setting") || text.contains("option") || text.contains("unit") ||
      text.contains("style") || text.contains("config")) return "settings";
  if (text.contains("delete") || text.contains("erase") || text.contains("trim") ||
      text.contains("remove") || text.contains("purge")) return "edit";
  if (text.contains("move") || text.contains("copy") || text.contains("rotate") ||
      text.contains("scale") || text.contains("mirror") || text.contains("array") ||
      text.contains("offset")) return "transform";
  if (text.contains("leader")) return "dimension";
  if (text.contains("line") || text.contains("poly") || text.contains("curve") ||
      text.contains("spline") || text.contains("arc")) return "geometry";
  if (text.contains("list") || text.contains("table") || text.contains("report") ||
      text.contains("search") || text.contains("stats") || text.contains("catalog")) return "data";
  if (text.contains("pipe") || text.contains("fitting")) return "pipe";
  return "utility";
}

void EditorWindow::LoadCoordinateSystemDictionaries() {
  if (geo::DictionariesLoaded()) return;
  std::string directory;
  try {
    directory = ResolveBundledAssetPath(std::filesystem::path("resources") / "csmap").string();
  } catch (const std::exception& error) {
    qWarning() << "Unable to locate the bundled coordinate-system dictionary:" << error.what();
  }
  geo::LoadDictionaries(directory);
}

void InstallDrawingDocumentTabStripImpl(QMainWindow* window, QTabBar* tabs) {
  if (window == nullptr || tabs == nullptr) return;
  auto* wrapper = window->findChild<QWidget*>("ribbonWrapper");
  if (wrapper == nullptr) return;
  if (wrapper->property("drawingTabStripInstalled").toBool()) return;
  QLayout* strip = wrapper->layout();
  if (strip == nullptr) return;

  auto* outer = new QVBoxLayout;
  outer->setObjectName("ribbonOuterLayout");
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* ribbonRow = new QHBoxLayout;
  ribbonRow->setContentsMargins(0, 0, 0, 0);
  ribbonRow->setSpacing(2);
  while (QLayoutItem* item = strip->takeAt(0)) ribbonRow->addItem(item);
  delete strip;
  outer->addLayout(ribbonRow);

  auto* docStrip = new QWidget(wrapper);
  docStrip->setObjectName("drawingTabRow");
  docStrip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  const int tabHeight = tabs->height();
  docStrip->setFixedHeight(tabHeight);
  auto* docLayout = new QHBoxLayout(docStrip);
  docLayout->setContentsMargins(0, 0, 0, 0);
  docLayout->setSpacing(0);
  tabs->setParent(docStrip);
  tabs->setExpanding(false);
  tabs->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  docLayout->addWidget(tabs, 0);
  auto* newDrawingTab = new QToolButton(docStrip);
  newDrawingTab->setObjectName("drawingTabNew");
  newDrawingTab->setText("+");
  newDrawingTab->setToolTip("New drawing");
  newDrawingTab->setFixedSize(26, 26);
  newDrawingTab->setAutoRaise(true);
  docLayout->addWidget(newDrawingTab, 0, Qt::AlignVCenter);
  docLayout->addStretch(1);
  outer->addWidget(docStrip);
  wrapper->setLayout(outer);
  wrapper->setProperty("drawingTabStripInstalled", true);

  if (auto* ribbon = window->findChild<QTabWidget*>("ribbon")) {
    wrapper->setMinimumHeight(ribbon->height() + tabHeight);
    wrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
}

EditorWindow::EditorWindow(QString developmentBranchLabel)
    : m_DevelopmentBranchLabel(std::move(developmentBranchLabel)) {
  m_State.measureAnnotationText = MeasureQtText;
  // Issue #1083: before any drawing can open, so the zone, grid readouts and the online map work
  // without Drawing Settings having been opened first.
  LoadCoordinateSystemDictionaries();
  InitUpdates();
  setObjectName("GoSurveyQtEditor");
  const QString title = m_DevelopmentBranchLabel.isEmpty()
      ? QString("GoSurvey") : QString("GoSurvey — ") + m_DevelopmentBranchLabel;
  setWindowTitle(title);
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
  centerLayout->setContentsMargins(0, 0, 0, 6);
  centerLayout->setSpacing(0);
  m_Tabs = new DrawingDocumentTabBar(this);
  m_Tabs->setObjectName("drawingTabs");
  m_Tabs->setAutoHide(false);
  m_Tabs->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  m_Pages = new QStackedWidget(center);
  auto* start = new QWidget(m_Pages);
  start->setObjectName("startScreen");
  auto* startLayout = new QVBoxLayout(start);
  startLayout->setContentsMargins(9, 8, 9, 8);
  startLayout->setSpacing(0);
  const bool darkStart = palette().color(QPalette::Window).lightness() < 128;
  const QString panelColor = darkStart ? "#242a2f" : "#f0f1f2";
  const QString cardColor = darkStart ? "#2a3035" : "#ffffff";
  const QString edgeColor = darkStart ? "#3c444a" : "#c5c8cb";
  const QString textColor = darkStart ? "#f0f5fa" : "#303234";
  const QString mutedColor = darkStart ? "#a5bac9" : "#66696c";
  const QString aquaColor = QtAccentFor(darkStart).strong;  // primary accent (REQ-436: topo-line amber)
  const QString amberColor = QtAccentFor(darkStart).quiet;  // secondary emphasis is a calm neutral

  auto* pageScroll = new QScrollArea(start);
  pageScroll->setObjectName("startPageScroll");
  pageScroll->setFrameShape(QFrame::NoFrame);
  pageScroll->setWidgetResizable(true);
  pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  pageScroll->setStyleSheet(QString("QScrollArea#startPageScroll { border:1px solid %1; border-radius:12px; background:%2; }")
                                .arg(darkStart ? "#41494e" : "#c5c8cb", darkStart ? "#1c2024" : "#f5f6f7"));
  auto* body = new TopographicStartBody(pageScroll);
  body->setObjectName("startPageBody");
  auto* bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(24, 7, 24, 6);
  bodyLayout->setSpacing(6);

  auto* welcome = new QWidget(body);
  auto* welcomeLayout = new QVBoxLayout(welcome);
  welcomeLayout->setContentsMargins(0, 0, 0, 0);
  welcomeLayout->setSpacing(1);
  auto* welcomeTitle = new QLabel("Welcome to GoSurvey", welcome);
  welcomeTitle->setObjectName("startWelcomeTitle");
  welcomeTitle->setStyleSheet(QString("font-size:34px; font-weight:700; color:%1; border:0;").arg(textColor));
  welcomeLayout->addWidget(welcomeTitle);
  bodyLayout->addWidget(welcome);

  auto* actionRow = new QHBoxLayout();
  actionRow->setSpacing(14);
  auto addActionCard = [&](const QString& name, const QString& iconName, const QString& titleText,
                           const QString& description, const QString& accent,
                           std::function<void()> action) {
    auto* card = new QPushButton(body);
    card->setObjectName(name);
    card->setAccessibleName(titleText);
    card->setCursor(Qt::PointingHandCursor);
    card->setMinimumHeight(104);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setStyleSheet(QString(
        "QPushButton#%1 { text-align:left; background:%2; border:1px solid %3; border-left:3px solid %4; border-radius:8px; padding:0; }"
        "QPushButton#%1:hover { background:%5; border-color:%6; }"
        "QPushButton#%1:pressed { background:%7; }")
        .arg(name, cardColor, edgeColor, accent,
             darkStart ? "#363d42" : QtAccentFor(false).hoverTint, accent,
             darkStart ? "#22272b" : QtAccentFor(false).tint));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 10, 16, 10);
    cardLayout->setSpacing(3);
    auto* top = new QHBoxLayout();
    auto* icon = new QLabel(card);
    icon->setPixmap(CadCommandIcon(iconName).pixmap(34, 34));
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    top->addWidget(icon);
    top->addStretch();
    auto* arrow = new QLabel("→", card);
    arrow->setStyleSheet(QString("font-size:20px; color:%1; border:0;").arg(mutedColor));
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    top->addWidget(arrow);
    cardLayout->addLayout(top);
    auto* heading = new QLabel(titleText, card);
    heading->setStyleSheet(QString("font-size:18px; font-weight:650; color:%1; border:0;").arg(textColor));
    heading->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(heading);
    auto* copy = new QLabel(description, card);
    copy->setWordWrap(true);
    copy->setStyleSheet(QString("color:%1; border:0;").arg(mutedColor));
    copy->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(copy);
    connect(card, &QPushButton::clicked, this, [action = std::move(action)] { action(); });
    actionRow->addWidget(card, 1);
  };
  addActionCard("startNewDrawing", "New_Drawing.png", "New drawing",
                "Start a fresh survey drawing with a template.", aquaColor,
                [this] { NewDrawing(); });
  addActionCard("startOpenDrawing", "Open.png", "Open drawing",
                "Browse your files and open an existing drawing.", aquaColor,
                [this] { Submit("OPEN"); });
  addActionCard("startNewProject", "New_Drawing.png", "New project",
                "Create a project and its first drawing.", amberColor,
                [this] { NewProject(); });
  addActionCard("startOpenProject", "Open.png", "Open project",
                "Open an existing GoSurvey project.", aquaColor,
                [this] { Submit("OPENPROJECT"); });
  bodyLayout->addLayout(actionRow);

  auto* columns = new QBoxLayout(QBoxLayout::LeftToRight);
  columns->setObjectName("startContentColumns");
  columns->setSpacing(18);
  auto* drawingsPanel = new QWidget(body);
  drawingsPanel->setObjectName("startRecentDrawingsPanel");
  auto* drawingsLayout = new QVBoxLayout(drawingsPanel);
  drawingsLayout->setContentsMargins(0, 0, 0, 0);
  drawingsLayout->setSpacing(6);
  auto* recentHeader = new QHBoxLayout();
  auto* drawingsHeading = new QLabel("Recent drawings", drawingsPanel);
  drawingsHeading->setStyleSheet(QString("font-size:21px; font-weight:650; color:%1;").arg(textColor));
  recentHeader->addWidget(drawingsHeading, 1);
  auto* seeAllRecent = new QToolButton(drawingsPanel);
  seeAllRecent->setObjectName("recentSeeAll");
  seeAllRecent->setText("See all  ›");
  seeAllRecent->setCursor(Qt::PointingHandCursor);
  seeAllRecent->setStyleSheet(QString("QToolButton { color:%1; font-weight:600; border:0; padding:5px; } QToolButton:hover { color:%2; }").arg(aquaColor, QtAccentFor(darkStart).linkHover));
  recentHeader->addWidget(seeAllRecent);
  drawingsLayout->addLayout(recentHeader);

  auto* recentOptions = new QWidget(drawingsPanel);
  recentOptions->setObjectName("recentDrawingOptions");
  auto* recentOptionsLayout = new QHBoxLayout(recentOptions);
  recentOptionsLayout->setContentsMargins(0, 0, 0, 0);
  recentOptionsLayout->setSpacing(7);
  auto* gridMode = new QToolButton(drawingsPanel);
  gridMode->setObjectName("recentGridMode");
  gridMode->setText("Grid");
  gridMode->setCheckable(true);
  auto* listMode = new QToolButton(drawingsPanel);
  listMode->setObjectName("recentListMode");
  listMode->setText("List");
  listMode->setCheckable(true);
  m_RecentListView = true;
  listMode->setChecked(true);
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
  recentOptionsLayout->addWidget(gridMode);
  recentOptionsLayout->addWidget(listMode);
  auto* sort = new QComboBox(drawingsPanel);
  sort->setObjectName("recentDrawingSort");
  sort->addItems({"Last opened", "Name"});
  sort->setMaximumWidth(120);
  recentOptionsLayout->addWidget(sort);
  m_RecentSearch = new QLineEdit(drawingsPanel);
  m_RecentSearch->setObjectName("recentDrawingSearch");
  m_RecentSearch->setPlaceholderText("Search drawings");
  m_RecentSearch->setMaximumWidth(250);
  recentOptionsLayout->addWidget(m_RecentSearch);
  recentOptionsLayout->addStretch();
  recentOptions->hide();
  drawingsLayout->addWidget(recentOptions);
  connect(seeAllRecent, &QToolButton::clicked, this, [this, seeAllRecent, recentOptions] {
    m_RecentShowAll = !m_RecentShowAll;
    seeAllRecent->setText(m_RecentShowAll ? "Show less  ‹" : "See all  ›");
    recentOptions->setVisible(m_RecentShowAll);
    RefreshRecentDrawings();
  });
  auto* recentColumns = new QHBoxLayout();
  recentColumns->setContentsMargins(8, 2, 8, 3);
  auto addRecentColumn = [&](const QString& text, int stretch) {
    auto* label = new QLabel(text, drawingsPanel);
    label->setStyleSheet(QString("font-size:10px; font-weight:650; letter-spacing:0.5px; color:%1; border:0;").arg(mutedColor));
    recentColumns->addWidget(label, stretch);
  };
  addRecentColumn("NAME", 2);
  addRecentColumn("PREVIEW", 1);
  addRecentColumn("MODIFIED", 1);
  addRecentColumn("LOCATION", 2);
  recentColumns->addSpacing(30);
  drawingsLayout->addLayout(recentColumns);
  m_RecentTiles = new QWidget(drawingsPanel);
  m_RecentTiles->setObjectName("recentDrawingTiles");
  auto* recentScroll = new QScrollArea(drawingsPanel);
  recentScroll->setObjectName("recentDrawingsScroll");
  recentScroll->setFrameShape(QFrame::NoFrame);
  recentScroll->setWidgetResizable(true);
  recentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recentScroll->setWidget(m_RecentTiles);
  // QScrollArea makes its content opaque by default; keep the terrain visible
  // through the open space between drawing cards.
  m_RecentTiles->setAutoFillBackground(false);
  recentScroll->viewport()->setAutoFillBackground(false);
  recentScroll->setStyleSheet("QScrollArea#recentDrawingsScroll { background:transparent; border:0; }");
  recentScroll->setMinimumHeight(300);
  recentScroll->setMaximumHeight(QWIDGETSIZE_MAX);
  drawingsLayout->addWidget(recentScroll, 1);
  connect(m_RecentSearch, &QLineEdit::textChanged, this, [this] { RefreshRecentDrawings(); });
  connect(sort, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { RefreshRecentDrawings(); });
  columns->addWidget(drawingsPanel, 2);

  auto* sidePanel = new QWidget(body);
  sidePanel->setObjectName("startRightColumn");
  sidePanel->setMinimumWidth(360);
  sidePanel->setMaximumWidth(560);
  auto* sideColumn = new QVBoxLayout(sidePanel);
  sideColumn->setContentsMargins(0, 0, 0, 0);
  sideColumn->setSpacing(4);

  auto* projectsCard = new QFrame(sidePanel);
  projectsCard->setObjectName("startProjectsCard");
  projectsCard->setStyleSheet(QString("QFrame#startProjectsCard { background:%1; border:1px solid %2; border-radius:8px; }").arg(cardColor, edgeColor));
  auto* projectsLayout = new QVBoxLayout(projectsCard);
  projectsLayout->setContentsMargins(9, 5, 9, 5);
  projectsLayout->setSpacing(3);
  auto* projectsHeader = new QHBoxLayout();
  auto* projectsHeading = new QLabel("Projects", projectsCard);
  projectsHeading->setStyleSheet(QString("font-size:16px; font-weight:650; color:%1; border:0;").arg(textColor));
  projectsHeader->addWidget(projectsHeading, 1);
  auto* projectsSeeAll = new QToolButton(projectsCard);
  projectsSeeAll->setObjectName("projectsSeeAll");
  projectsSeeAll->setText("View all  ›");
  projectsSeeAll->setCursor(Qt::PointingHandCursor);
  projectsSeeAll->setStyleSheet(QString("QToolButton { color:%1; font-weight:600; border:0; padding:4px; } QToolButton:hover { color:%2; }").arg(aquaColor, QtAccentFor(darkStart).linkHover));
  projectsHeader->addWidget(projectsSeeAll);
  projectsLayout->addLayout(projectsHeader);
  auto* projectSearch = new QLineEdit(projectsCard);
  projectSearch->setObjectName("recentProjectSearch");
  projectSearch->setPlaceholderText("⌕  Search projects…");
  projectsLayout->addWidget(projectSearch);
  m_ProjectTiles = new QWidget(projectsCard);
  m_ProjectTiles->setObjectName("recentProjectTiles");
  auto* projectsScroll = new QScrollArea(projectsCard);
  projectsScroll->setObjectName("recentProjectsScroll");
  projectsScroll->setFrameShape(QFrame::NoFrame);
  projectsScroll->setWidgetResizable(true);
  projectsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  projectsScroll->setWidget(m_ProjectTiles);
  projectsScroll->setMinimumHeight(250);
  projectsScroll->setMaximumHeight(258);
  projectsLayout->addWidget(projectsScroll);
  connect(projectSearch, &QLineEdit::textChanged, this, [this] { RefreshRecentProjects(); });
  connect(projectsSeeAll, &QToolButton::clicked, this, [this, projectsSeeAll] {
    m_ShowAllProjects = !m_ShowAllProjects;
    projectsSeeAll->setText(m_ShowAllProjects ? "Show less  ‹" : "View all  ›");
    RefreshRecentProjects();
  });
  sideColumn->addWidget(projectsCard);

  auto* quickCard = new QFrame(sidePanel);
  quickCard->setObjectName("startQuickLinksCard");
  quickCard->setStyleSheet(QString("QFrame#startQuickLinksCard { background:%1; border:1px solid %2; border-radius:8px; }").arg(cardColor, edgeColor));
  auto* quickLayout = new QVBoxLayout(quickCard);
  quickLayout->setContentsMargins(9, 5, 9, 5);
  quickLayout->setSpacing(3);
  auto* quickHeading = new QLabel("Quick links", quickCard);
  quickHeading->setStyleSheet(QString("font-size:16px; font-weight:650; color:%1; border:0;").arg(textColor));
  quickLayout->addWidget(quickHeading);
  auto* quickGrid = new QGridLayout();
  quickGrid->setSpacing(5);
  auto addQuickLink = [&](const QString& label, const QString& detail, const QString& icon, int column,
                          const QString& accent, std::function<void()> action) {
    auto* link = new QToolButton(quickCard);
    link->setObjectName("startQuickLink");
    link->setAccessibleName(label);
    link->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    QString linkText = label;
    linkText.replace("&", "&&");
    QString detailText = detail;
    detailText.replace("&", "&&");
    link->setText(linkText + "\n" + detailText);
    link->setIcon(CadCommandIcon(icon));
    link->setIconSize(QSize(23, 23));
    link->setMinimumHeight(52);
    link->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    link->setStyleSheet(QString("QToolButton { color:%1; background:%2; border:1px solid %3; border-left:3px solid %4; border-radius:6px; padding:4px 2px; font-size:9px; } QToolButton:hover { background:%5; border-color:%4; }")
                            .arg(textColor, cardColor, edgeColor, accent, darkStart ? "#30383d" : QtAccentFor(false).hoverTint));
    connect(link, &QToolButton::clicked, this, [action = std::move(action)] { action(); });
    quickGrid->addWidget(link, 0, column);
  };
  addQuickLink("Templates", "Use a survey template", "New_Drawing.png", 0, amberColor,
               [this] { NewDrawing(); });
  addQuickLink("Survey setup", "Drawing units & plot scale", "settings.png", 1, aquaColor,
               [this] { ShowUnits(); });
  addQuickLink("Import formats", "Survey point files", "Import.png", 2, aquaColor,
               [this] { Submit("IMPORTPOINTS"); });
  addQuickLink("Application settings", "Customize GoSurvey", "settings.png", 3, aquaColor,
               [this] { ShowSettings(); });
  quickLayout->addLayout(quickGrid);
  sideColumn->addWidget(quickCard);

  auto* resourcesCard = new QFrame(sidePanel);
  resourcesCard->setObjectName("startLearningCard");
  resourcesCard->setStyleSheet(QString("QFrame#startLearningCard { background:%1; border:1px solid %2; border-radius:8px; }").arg(cardColor, edgeColor));
  auto* resourcesLayout = new QVBoxLayout(resourcesCard);
  resourcesLayout->setContentsMargins(9, 5, 9, 5);
  resourcesLayout->setSpacing(1);
  auto* resourcesHeader = new QHBoxLayout();
  auto* resourcesHeading = new QLabel("Learning resources", resourcesCard);
  resourcesHeading->setStyleSheet(QString("font-size:16px; font-weight:650; color:%1; border:0;").arg(textColor));
  resourcesHeader->addWidget(resourcesHeading, 1);
  auto* resourcesSeeAll = new QToolButton(resourcesCard);
  resourcesSeeAll->setText("View all  ›");
  resourcesSeeAll->setCursor(Qt::PointingHandCursor);
  resourcesSeeAll->setStyleSheet(QString("QToolButton { color:%1; border:0; padding:3px; } QToolButton:hover { color:%2; }").arg(aquaColor, QtAccentFor(darkStart).linkHover));
  resourcesHeader->addWidget(resourcesSeeAll);
  resourcesLayout->addLayout(resourcesHeader);
  auto addResourceLink = [&](const QString& title, const QString& detail, const QString& icon,
                             const QString& url, const QString& objectName) {
    auto* link = new QToolButton(resourcesCard);
    link->setObjectName(objectName);
    link->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    link->setIcon(CadCommandIcon(icon));
    link->setIconSize(QSize(16, 16));
    QString linkTitle = title;
    linkTitle.replace("&", "&&");
    link->setText(linkTitle + "\n" + detail + "    ›");
    link->setMinimumHeight(27);
    link->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    link->setStyleSheet(QString("QToolButton { text-align:left; color:%1; background:transparent; border:0; border-left:3px solid %3; padding:1px 4px; font-size:10px; } QToolButton:hover { background:%2; }")
                            .arg(textColor, darkStart ? "#30383d" : QtAccentFor(false).hoverTint, QtAccentFor(darkStart).quiet));
    connect(link, &QToolButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
    resourcesLayout->addWidget(link);
  };
  addResourceLink("Getting started", "Install and make your first drawing", "app.png",
                  "https://gosurveycad.com/docs/", "startGettingStartedLink");
  addResourceLink("User guide", "Guides, reference and workflows", "app.png",
                  "https://gosurveycad.com/docs/", "startUserGuideLink");
  addResourceLink("Help & support", "Find answers and contact support", "app.png",
                  "https://github.com/chetjones003/GoSurvey/issues/new", "startFeedbackLink");
  connect(resourcesSeeAll, &QToolButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl("https://gosurveycad.com/docs/"));
  });
  sideColumn->addWidget(resourcesCard);
  sideColumn->addStretch(1);
  columns->addWidget(sidePanel, 1);
  bodyLayout->addLayout(columns, 1);

  pageScroll->setWidget(body);
  startLayout->addWidget(pageScroll);
  RefreshRecentDrawings();
  RefreshRecentProjects();
  m_Pages->addWidget(start);

  auto* modelPage = new QWidget(m_Pages);
  auto* modelLayout = new QVBoxLayout(modelPage);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  m_Viewport = new EditorViewport(m_State, m_Log, modelPage);
  m_Viewport->changed = [this] { ResetDraftingInput(); Refresh(); };
  // Issue #1084: a part picture finished inside the viewport's paint; update the palette afterwards.
  m_Viewport->partThumbnailsChanged = [this] {
    QTimer::singleShot(0, this, [this] {
      if (auto* palette = findChild<QDialog*>("pipeFittingPalette"); palette && palette->isVisible())
        RefreshPipeFittingPalette(palette);
    });
  };
  m_Viewport->contextMenuRequested = [this](const QPoint& position) { ShowDrawingContextMenu(position); };
  m_Viewport->thumbnailReady = [this] {
    QTimer::singleShot(0, this, [this] { RefreshRecentDrawings(); });
  };
  m_Viewport->textEntered = [this](const QString& text) { if (m_CommandBar->isHidden()) m_CommandBar->ToggleVisible(); m_Command->setFocus(); m_Command->insert(text); };
  m_Viewport->cursorChanged = [this](double x, double y, double z) {
    m_DynamicCursor = {x + m_State.worldDocumentOriginX, y + m_State.worldDocumentOriginY, z};
    const auto local = ucs::WorldToUcs(m_State.activeUcs, m_DynamicCursor);
    m_Coordinates->setText(QString("%1  X %2   Y %3   Z %4").arg(CadUcsIsWorld(m_State) ? "WCS" : "UCS")
        .arg(local.x, 0, 'f', 4).arg(local.y, 0, 'f', 4).arg(local.z, 0, 'f', 4));
    RefreshDraftingInput();
  };
  modelLayout->addWidget(m_Viewport, 1);
  m_Pages->addWidget(modelPage);
  centerLayout->addWidget(m_Pages, 1);
  auto* spaceBar = new QWidget(center);
  spaceBar->setObjectName("paperSpaceBar");
  spaceBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  spaceBar->setMinimumHeight(34);
  auto* spaceRow = new QHBoxLayout(spaceBar);
  spaceRow->setContentsMargins(6, 4, 6, 6);
  spaceRow->setSpacing(4);
  m_SpaceTabs = new QTabBar(spaceBar);
  m_SpaceTabs->setObjectName("paperSpaceTabs");
  m_SpaceTabs->setExpanding(true);
  m_SpaceTabs->setMinimumHeight(m_SpaceTabs->fontMetrics().height() + 10);
  m_SpaceTabs->setContextMenuPolicy(Qt::CustomContextMenu);
  spaceRow->addWidget(m_SpaceTabs, 1);
  auto* manageLayouts = new QToolButton(spaceBar);
  manageLayouts->setObjectName("manageLayoutsButton");
  manageLayouts->setText("Manage layouts…");
  spaceRow->addWidget(manageLayouts);
  centerLayout->addWidget(spaceBar);
  setCentralWidget(center);
  BuildRibbon();
  BuildDocks();
  UpdateChromeIcons();
  m_Coordinates = new QLabel(this);
  statusBar()->setMinimumHeight(34);
  statusBar()->setSizeGripEnabled(false);
  statusBar()->addPermanentWidget(new VisibleSizeGrip(this));
  statusBar()->addPermanentWidget(m_Coordinates, 1);
  BuildDraftingInput();
  auto* grid = new QToolButton(this);
  grid->setObjectName("draftGrid"); grid->setText("GRID"); grid->setCheckable(true); grid->setChecked(true);
  grid->setStyleSheet(QtApplyAccent("QToolButton { color:#edf1f3; background:#2a3035; border:1px solid #41494e; "
      "border-radius:4px; padding:2px 8px; } QToolButton:hover { background:#343c41; border-color:@edge@; } "
      "QToolButton:checked { color:#ffffff; background:@tint@; border-color:@strong@; }", true));
  statusBar()->addWidget(grid);
  connect(grid, &QToolButton::toggled, this, [this](bool on) { m_Viewport->SetGrid(on); });
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
  auto* viewportZoom = new QComboBox(this);
  viewportZoom->setObjectName("statusViewportZoom");
  viewportZoom->setMinimumWidth(110);
  viewportZoom->setAccessibleName("Viewport zoom scale");
  viewportZoom->setToolTip("Model viewport zoom ratio (REQ-458)");
  statusBar()->addWidget(viewportZoom);
  connect(viewportZoom, QOverload<int>::of(&QComboBox::activated), this, [this, viewportZoom](int index) {
    if (viewportZoom->itemData(index).toString() == QLatin1String("__add__")) {
      bool ok = false;
      const QString text = QInputDialog::getText(this, "Viewport zoom scale", "Enter ratio (e.g. 1:75):",
          QLineEdit::Normal, QStringLiteral("1:50"), &ok);
      if (!ok) {
        Refresh();
        return;
      }
      float paperUnits = 1.f, drawingUnits = 1.f;
      const QByteArray utf8 = text.toUtf8();
      if (!TryParseViewportZoomRatio(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())), &paperUnits,
                                     &drawingUnits)) {
        QMessageBox::warning(this, "Viewport zoom scale", "Enter a ratio like 1:50.");
        Refresh();
        return;
      }
      AddViewportZoomScale(m_State, std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())), paperUnits,
                           drawingUnits);
      const float dpi = screen() != nullptr ? static_cast<float>(screen()->logicalDotsPerInchY()) : 96.f;
      ApplyViewportZoomScaleIndex(m_State, m_State.currentViewportZoomScaleIndex,
                                  static_cast<float>(m_Viewport->height()), dpi);
      Refresh();
      m_Viewport->update();
      return;
    }
    if (index < 0 || index >= static_cast<int>(m_State.viewportZoomScales.size()) ||
        index == m_State.currentViewportZoomScaleIndex)
      return;
    const float dpi = screen() != nullptr ? static_cast<float>(screen()->logicalDotsPerInchY()) : 96.f;
    ApplyViewportZoomScaleIndex(m_State, index, static_cast<float>(m_Viewport->height()), dpi);
    m_Viewport->update();
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
  connect(new QShortcut(QKeySequence(Qt::Key_F7), this), &QShortcut::activated, grid, &QToolButton::toggle);
  auto* service = new QTimer(this);
  service->setInterval(33);
  connect(service, &QTimer::timeout, this, [this] {
    ServiceTelemetry();
    ServiceUpdate();
    const auto before = m_State.cadGpuRevision;
    const auto logSize = m_Log.size();
    TickSurfaceRebuilds(m_State, m_Log); ServicePointCloudImport(); TickVolumeDashboard(m_State);
    RefreshProjects();
    if (before != m_State.cadGpuRevision || logSize != m_Log.size()) Refresh();
    else ServiceUiRequests();
  });
  service->start();
  connect(m_Tabs, &QTabBar::currentChanged, this, [this](int index) { if (!m_Refreshing) SwitchDrawing(index); });
  connect(m_Tabs, &QTabBar::tabCloseRequested, this, [this](int index) { CloseDrawing(index); });
  connect(m_SpaceTabs, &QTabBar::currentChanged, this, [this](int tab) {
    if (m_Refreshing || tab < 0 || m_State.activeDrawingIdx == 0) return;
    const int space = tab == 0 ? kModelSpaceIndex : tab - 1;
    if (space != m_State.activeSpaceIndex) { SetActiveSpace(m_State, space); Refresh(); }
  });
  connect(m_SpaceTabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
    const int tab = m_SpaceTabs->tabAt(pos);
    if (tab <= 0) return;
    const int layoutIndex = tab - 1;
    QMenu menu(this);
    menu.addAction("Set Current", this, [this, layoutIndex] { SetActiveSpace(m_State, layoutIndex); Refresh(); });
    menu.addAction("Page Setup Manager…", this, [this, layoutIndex] { ShowPageSetupManager(layoutIndex); });
    menu.addAction("Viewports…", this, [this, layoutIndex] { SetActiveSpace(m_State, layoutIndex); m_State.showViewportsWindow = true; Refresh(); });
    menu.addSeparator();
    menu.addAction("Rename…", this, [this, layoutIndex] {
      if (ActiveDrawingReadOnly()) return;
      bool ok = false;
      const QString name = QInputDialog::getText(this, "Rename Layout", "Layout name:", QLineEdit::Normal,
          QString::fromUtf8(m_State.paperLayouts[static_cast<size_t>(layoutIndex)].name.c_str()), &ok).trimmed();
      if (!ok || name.isEmpty()) return;
      const auto before = m_Log.size();
      HandleLayoutCommand("RENAME " + m_State.paperLayouts[static_cast<size_t>(layoutIndex)].name + " " + name.toUtf8().toStdString(), m_State, m_Log);
      if (m_Log.size() > before && m_Log.back().find("renamed") == std::string::npos) QMessageBox::warning(this, "Rename Layout", QString::fromUtf8(m_Log.back().c_str()));
      Refresh();
    });
    menu.addAction("Delete", this, [this, layoutIndex] {
      if (ActiveDrawingReadOnly() || layoutIndex >= static_cast<int>(m_State.paperLayouts.size())) return;
      const QString name = QString::fromUtf8(m_State.paperLayouts[static_cast<size_t>(layoutIndex)].name.c_str());
      if (QMessageBox::question(this, "Delete Layout", "Delete layout “" + name + "”? This can be undone.") != QMessageBox::Yes) return;
      DeletePaperLayout(m_State, layoutIndex); Refresh();
    });
    menu.exec(m_SpaceTabs->mapToGlobal(pos));
  });
  connect(manageLayouts, &QToolButton::clicked, this, &EditorWindow::ShowLayoutManager);
  QSettings settings;
  restoreGeometry(settings.value("editor/geometry").toByteArray());
  restoreState(settings.value("editor/layout").toByteArray(), 2);
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

void EditorWindow::EnableTelemetry() {
  if (m_TelemetryTask)
    return;
  const char* channel = m_State.updatePrefs.useBetaChannel ? "beta" : "stable";
  try {
    m_TelemetryTask = telemetry::BeginTelemetryPing(GOSURVEY_VERSION_FULL, channel);
  } catch (...) {
    // Telemetry is best effort; even worker creation failure is invisible to the user.
  }
}

void EditorWindow::ServiceTelemetry() {
  telemetry::PollTelemetryTask(m_TelemetryTask);
}
void EditorWindow::InitializeStartup() {
  m_StartupInitialized = true;
  // The entry point exits its splash wait when the worker marks the task done, before the UI
  // thread has consumed that result. Resolve it here so only a confirmed offer takes priority;
  // a completed no-update check must return to Idle before REQ-336 evaluates startup eligibility.
  ServiceUpdate();
  // REQ-078 before REQ-336: when the startup check found an update, What's New does not
  // auto-open on this launch. The check has already answered by now (the splash waits for it).
  if (UpdateBlocksLaunch()) m_WhatsNewAutoOpened = true;
  MaybeAutoOpenWhatsNew();
}

void EditorWindow::MaybeAutoOpenWhatsNew() {
  if (!m_StartupInitialized || m_WhatsNewAutoOpened || m_State.activeDrawingIdx != 0 ||
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
  file->setObjectName("fileMenu");
  file->addAction(CommandAction("&New", "New.png", "NEW", QKeySequence::New));
  file->addAction(CommandAction("&Open...", "Open.png", "OPEN", QKeySequence::Open));
  file->addAction(CommandAction("&Save", "Save.png", "SAVE", QKeySequence::Save));
  file->addAction(CommandAction("Save &as...", "Save_As.png", "SAVEAS", QKeySequence::SaveAs));
  file->addSeparator();
  file->addAction(CommandAction("Drawing Settings...", "settings.png", "DRAWINGSETTINGS"));
  file->addSeparator();
  file->addAction(CommandAction("New project...", "New_Drawing.png", "NEWPROJECT"));
  file->addAction(CommandAction("Open project...", "Open.png", "OPENPROJECT"));
  file->addAction(CommandAction("Project Settings...", "settings.png", "PROJECTSETTINGS"));
  file->addAction(CommandAction("Project Health...", "settings.png", "PROJECTHEALTH"));
  file->addAction(CommandAction("Refresh Project Files...", "Refresh.png", "REFRESH"));
  file->addAction(CommandAction("Pack Project...", "Save.png", "PACKPROJECT"));
  file->addAction(CommandAction("Open Packed Project...", "Open.png", "OPENPACK"));
  file->addAction(CommandAction("Create Turnover...", "Active_File.png", "TURNOVER"));
  file->addAction(CommandAction("Add PDF to project...", "Pdf.png", "ADDPDFTOPROJECT"));
  file->addAction(CommandAction("Add Drawing to Project...", "Active_File.png", "ADDDRAWING"));
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
    auto* action = appearance->addAction(light ? "Classic light" : "Graphite");
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
  InstallDrawingDocumentTabStripImpl(this, m_Tabs);
  if (auto* newTab = findChild<QToolButton*>("drawingTabNew"))
    connect(newTab, &QToolButton::clicked, this, [this] { NewDrawing(); });
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
  m_Toolspace = new TopographicTreeWidget(true, this);
  m_Toolspace->setHeaderLabel("Prospector");
  m_Toolspace->setUniformRowHeights(true);
  m_Toolspace->setIndentation(14);
  m_Toolspace->setMinimumWidth(0);
  m_Toolspace->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_Toolspace, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) { ShowToolspaceContextMenu(position); });
  auto* toolspaceTabs = new QTabWidget(this);
  toolspaceTabs->setObjectName("toolspaceTabs");
  toolspaceTabs->addTab(m_Toolspace, "Prospector");
  auto* toolSettings = new QWidget(toolspaceTabs);
  auto* toolSettingsLayout = new QVBoxLayout(toolSettings);
  auto* settingsNote = new QLabel("Workspace and drawing preferences", toolSettings);
  settingsNote->setWordWrap(true);
  toolSettingsLayout->addWidget(settingsNote);
  auto* settingsButton = new QPushButton("Open application settings…", toolSettings);
  toolSettingsLayout->addWidget(settingsButton);
  toolSettingsLayout->addStretch();
  connect(settingsButton, &QPushButton::clicked, this, [this] { ShowSettings(); });
  toolspaceTabs->addTab(toolSettings, "Settings");
  makeDock("Toolspace", "toolspaceDock", Qt::RightDockWidgetArea, toolspaceTabs);
  BuildPropertiesPanel();
  auto* propertiesDock = makeDock("Properties", "propertiesDock", Qt::LeftDockWidgetArea, m_PropertiesScroll);
  propertiesDock->raise();
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
  m_CommandSuggestions = new QListView(this);
  m_CommandSuggestions->setObjectName("commandSuggestionPopup");
  // A Qt::Popup grabs subsequent key presses after the first character opened suggestions,
  // preventing the focused command line from receiving the rest of a typed query.
  m_CommandSuggestions->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
  m_CommandSuggestions->setAttribute(Qt::WA_ShowWithoutActivating);
  m_CommandSuggestions->setFocusPolicy(Qt::NoFocus);
  m_CommandSuggestions->setFrameShape(QFrame::NoFrame);
  m_CommandSuggestions->setSelectionMode(QAbstractItemView::SingleSelection);
  m_CommandSuggestions->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_CommandSuggestions->setIconSize(QSize(18, 18));
  m_CommandSuggestions->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_CommandSuggestions->setStyleSheet(
      "QListView#commandSuggestionPopup { background: palette(base); color: palette(text); "
      "border: 1px solid palette(mid); border-radius: 8px; padding: 4px; outline: none; }"
      "QListView#commandSuggestionPopup::item { padding: 6px 9px; min-height: 22px; border-radius: 4px; }"
      "QListView#commandSuggestionPopup::item:selected { background: palette(highlight); "
      "color: palette(highlighted-text); }");
  m_CommandSuggestionModel = new QStandardItemModel(m_CommandSuggestions);
  m_CommandSuggestions->setModel(m_CommandSuggestionModel);

  QHash<QString, QIcon> commandIcons;
  for (const auto* action : findChildren<QAction*>()) {
    QString name = action->property("ribbonCommand").toString();
    if (name.isEmpty() && action->objectName().startsWith("command_"))
      name = action->objectName().mid(QString("command_").size());
    if (name.isEmpty()) continue;
    const QString iconName = action->property("cadIcon").toString();
    const QIcon icon = iconName.isEmpty() ? action->icon() : CadCommandIcon(iconName);
    if (!icon.isNull()) commandIcons.insert(name.toUpper(), icon);
  }
  static const QHash<QString, QString> resourceIcons = [] {
    QHash<QString, QString> icons;
    const QDir commandIconDirectory(QtAsset("icons"));
    for (const auto& entry : commandIconDirectory.entryInfoList({"*.png", "*.svg"}, QDir::Files)) {
      const QString key = NormalizedCommandIconName(entry.completeBaseName());
      if (!key.isEmpty() && !icons.contains(key)) icons.insert(key, entry.fileName());
    }
    return icons;
  }();
  const QHash<QString, QString> semanticIconNames{
      {"CIRCLE", "Circle_Center_Radius.png"}, {"3DPOLY", "3D_Polyline.png"},
      {"FEATURELINE", "c3d_featureline.png"}, {"GRADING", "c3d_grading.png"},
      {"TRIMSTATE", "Auto_Trimming.png"}, {"EXPORTPTS", "c3d_exportpoints.png"},
      {"EXPORTPOINTS", "c3d_exportpoints.png"}, {"QUICKPROFILE", "c3d_quickprofile.png"},
      {"SURFACECREATE", "c3d_surfaces.png"}, {"ZOOMEXTENTS", "Zoom_Extensis.png"},
      {"MOVESEL", "3D_Move.png"}, {"ROTATESEL", "3D_Rotate.png"},
      {"SCALESEL", "Scale.png"}, {"MIRRORSEL", "3D_Mirror.png"},
      {"DELETE", "erase.png"}, {"MATCHPROP", "Match_Properties.png"},
      {"DRAWINGSETTINGS", "c3d_dwgsettings.png"}, {"PROJECTSETTINGS", "settings.png"},
      {"PROJECTHEALTH", "settings.png"}, {"PACKPROJECT", "Save.png"},
      {"TURNOVER", "Active_File.png"}, {"OPENPACK", "Open.png"},
      {"BLOCKLIST", "Blocks_Palette.png"}, {"BLOCKLIB", "Blocks_Palette.png"},
      {"BLOCKFAV", "Blocks_Palette.png"}, {"BLOCKRECENT", "Blocks_Palette.png"},
      {"COPYCLIP", "Copy_Clip.png"}, {"PASTEBLOCK", "Paste.png"},
      {"PDFATTACH", "Pdf.png"}, {"PDFVIEW", "Pdf.png"}, {"PDFSPLIT", "Pdf.png"},
      {"IMPORTPOINTS", "c3d_transferpoints.png"}, {"EXPORTLANDXML", "c3d_exportto.png"},
      {"VOLDASH", "c3d_surfaces.png"}, {"SURFACEIMPORTFILE", "c3d_surfaces.png"},
      {"PIPERUN", "c3d_pipenet.png"}, {"PIPESYS", "c3d_pipenet.png"},
      {"PIPECATALOG", "c3d_pipenet.png"}, {"BEDIT", "Block_Editor.png"},
      {"BSAVE", "Save.png"}, {"BSAVEAS", "Save_As.png"}};
  connect(m_Command, &QLineEdit::textEdited, this,
          [this, commandIcons, semanticIconNames](const QString& text) mutable {
    if (m_DynamicInput && m_DynamicInput->isVisible()) {
      m_CommandSuggestions->hide();
      UpdateDraftingInputFromCommandLine(text);
      return;
    }
    m_CommandSuggestionModel->clear();
    const QString query = text.trimmed();
    const auto suggestions = query.isEmpty()
        ? std::vector<CommandSuggestion>{}
        : FuzzyCommandSuggestions(query.toUtf8().toStdString(), 30);
    for (const auto& suggestion : suggestions) {
      const QString name = QString::fromUtf8(suggestion.name.c_str());
      QIcon icon = commandIcons.value(name);
      if (icon.isNull()) {
        const QString semanticFile = semanticIconNames.value(name);
        if (!semanticFile.isEmpty()) icon = CadCommandIcon(semanticFile);
      }
      if (icon.isNull()) {
        const QString resourceFile = resourceIcons.value(NormalizedCommandIconName(name));
        if (!resourceFile.isEmpty()) icon = CadCommandIcon(resourceFile);
      }
      if (icon.isNull()) {
        const QString generated = GeneratedCommandIconFamily(name, QString::fromUtf8(suggestion.description.c_str()));
        icon = CadCommandIcon("commands/" + generated + ".svg");
      }
      commandIcons.insert(name, icon);
      auto* item = new QStandardItem(icon, name);
      item->setEditable(false);
      item->setToolTip(QString::fromUtf8(suggestion.description.c_str()));
      m_CommandSuggestionModel->appendRow(item);
    }
    const int rows = m_CommandSuggestionModel->rowCount();
    if (rows == 0) { m_CommandSuggestions->hide(); return; }
    const int visibleRows = std::min(rows, 8);
    const int popupHeight = visibleRows * 32 + 10;
    const QPoint inputTop = m_Command->mapToGlobal(QPoint(0, 0));
    m_CommandSuggestions->setGeometry(inputTop.x(), inputTop.y() - popupHeight,
                                      m_Command->width(), popupHeight);
    const QModelIndex first = m_CommandSuggestionModel->index(0, 0);
    m_CommandSuggestions->setCurrentIndex(first);
    m_CommandSuggestions->scrollToTop();
    m_CommandSuggestions->show();
    m_CommandSuggestions->raise();
  });
  connect(m_CommandSuggestions, &QListView::clicked, this, [this](const QModelIndex& index) {
    if (!index.isValid() || (m_DynamicInput && m_DynamicInput->isVisible())) return;
    const QString command = index.data(Qt::DisplayRole).toString();
    m_CommandSuggestions->hide();
    m_Command->clear();
    Submit(command);
    m_Command->setFocus(Qt::OtherFocusReason);
  });
  connect(m_Toolspace, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    const auto index = item->data(0, Qt::UserRole);
    if (index.isValid()) SwitchDrawing(index.toInt());
  });
}

void EditorWindow::Submit(const QString& command) {
  ResetDraftingInput();
  if (!command.trimmed().isEmpty()) {
    if (m_CommandHistory.isEmpty() || m_CommandHistory.last() != command) m_CommandHistory.push_back(command);
    if (m_CommandHistory.size() > 500) m_CommandHistory.removeFirst();
    m_HistoryIndex = static_cast<int>(m_CommandHistory.size());
  }
  const QString verb = command.trimmed().toUpper();
  if (verb == "LAYOUT") { ShowLayoutManager(); return; }
  if (verb.startsWith("LAYOUT ")) {
    if (m_State.activeDrawingIdx == 0) { m_Log.push_back("LAYOUT - open a drawing first."); Refresh(); return; }
    if (ActiveDrawingReadOnly()) { QMessageBox::information(this, "Read-only drawing", "Layouts cannot be changed in a read-only drawing."); return; }
    HandleLayoutCommand(command.mid(7).toUtf8().toStdString(), m_State, m_Log); Refresh(); return;
  }
  if (verb == "PAGESETUP") { StartPageSetupCommand(m_State, m_Log); if (m_State.showPageSetupManager) ShowPageSetupManager(); else Refresh(); return; }
  if (verb == "VPSCALE" || verb.startsWith("VPSCALE ")) {
    const QString value = command.trimmed().mid(7).trimmed();
    if (value.isEmpty()) ShowViewportScale();
    else if (ActiveDrawingReadOnly()) QMessageBox::information(this, "Read-only drawing", "Viewport scale cannot be changed in a read-only drawing.");
    else { HandleVpscaleCommand(value.toUtf8().toStdString(), m_State, m_Log); Refresh(); }
    return;
  }
  if (verb == "NEW") NewDrawing();
  else if (verb == "COPYCLIP") CopySelectionToClipboard(m_State, m_Log);
  else if (verb == "PASTECLIP") StartPasteCommand(m_State, m_Log);
  else if (verb == "NEWPROJECT") NewProject();
  else if (verb == "OPENPROJECT") {
    const auto path = QFileDialog::getOpenFileName(this, "Open project", {}, "GoSurvey project (*.gsproj)");
    if (!path.isEmpty()) OpenProject(path, true);
  }
  else if (verb == "PROJECTSETTINGS") {
    if (m_State.activeDrawingIdx > 0 &&
        m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size()) &&
        m_State.drawingTabs[static_cast<size_t>(m_State.activeDrawingIdx)].projectUid != 0) {
      ShowProjectSettings(m_State.drawingTabs[static_cast<size_t>(m_State.activeDrawingIdx)].projectUid);
    } else {
      QMessageBox::information(this, "Project Settings", "Open a drawing that belongs to a project first.");
    }
  }
  else if (verb == "ADDPDFTOPROJECT") AddPdfToProjectFromDisk();
  else if (verb == "POINTCLOUDATTACH" || verb == "IMPORTPOINTCLOUD") ShowPointCloudAttach();
  else if ((verb.startsWith("POINTCLOUDATTACH ") || verb.startsWith("IMPORTPOINTCLOUD ")) && ActiveDrawingReadOnly())
    QMessageBox::information(this, "Attach Point Cloud", "This drawing is read-only. No point cloud was attached.");
  else if (verb == "EXPORTPTS") ShowPointCloudExport(0);
  else if (verb == "EXPORTPTX") ShowPointCloudExport(1);
  else if (verb == "EXPORTLAS") ShowPointCloudExport(2);
  else if (verb == "EXPORTLAZ") ShowPointCloudExport(3);
  else if (verb == "EXPORTLANDXML") {
    if (m_State.activeDrawingIdx == 0) QMessageBox::information(this, "Export to LandXML", "Open a drawing to export first.");
    else {
      QString path = QFileDialog::getSaveFileName(this, "Export to LandXML", {}, "LandXML (*.xml)");
      if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().isEmpty()) path += ".xml";
        Submit("EXPORTLANDXML \"" + QDir::toNativeSeparators(path) + "\"");
      }
    }
  }
  else if (verb == "PROJECTHEALTH" || verb == "PACKPROJECT" || verb == "TURNOVER" || verb == "REFRESH") {
    const auto idx = m_State.activeDrawingIdx;
    const std::uint32_t uid = idx > 0 && idx < static_cast<int>(m_State.drawingTabs.size())
        ? m_State.drawingTabs[static_cast<size_t>(idx)].projectUid : 0;
    if (!uid) QMessageBox::information(this, "Project", "Open a drawing that belongs to a project first.");
    else if (verb == "PROJECTHEALTH") ShowProjectHealth(uid);
    else if (verb == "PACKPROJECT") ShowProjectPack(uid);
    else if (verb == "TURNOVER") ShowProjectTurnover(uid);
    else RefreshProjectFiles();
  }
  else if (verb == "OPENPACK") OpenPackedProject();
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
    if (m_State.blockEditPickerOpen) ShowBlockEditorPicker();
  }
  Refresh();
}

bool EditorWindow::ActiveDrawingReadOnly() const {
  if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) return false;
  const auto uid = m_State.drawingTabs[static_cast<size_t>(m_State.activeDrawingIdx)].projectUid;
  for (const auto& session : m_State.openProjects) if (session.uid == uid) return session.readOnly;
  return false;
}

void EditorWindow::ShowLayoutManager() {
  if (m_State.activeDrawingIdx == 0) { QMessageBox::information(this, "Layout Manager", "Open a drawing to manage its layouts."); return; }
  QDialog dialog(this); dialog.setObjectName("layoutManager"); dialog.setWindowTitle("Layout Manager"); dialog.resize(520, 380);
  auto* outer = new QVBoxLayout(&dialog);
  auto* description = new QLabel("Switch between model space and paper layouts, or manage the drawing’s sheets.", &dialog);
  description->setWordWrap(true); outer->addWidget(description);
  auto* list = new QListWidget(&dialog); list->setObjectName("layoutManagerList"); outer->addWidget(list, 1);
  auto* row = new QHBoxLayout(); outer->addLayout(row);
  auto* setCurrent = new QPushButton("Set Current", &dialog); setCurrent->setObjectName("layoutSetCurrent");
  auto* add = new QPushButton("New", &dialog); add->setObjectName("layoutNew");
  auto* rename = new QPushButton("Rename…", &dialog); rename->setObjectName("layoutRename");
  auto* remove = new QPushButton("Delete", &dialog); remove->setObjectName("layoutDelete");
  auto* setup = new QPushButton("Page Setup…", &dialog); setup->setObjectName("layoutPageSetup");
  row->addWidget(setCurrent); row->addWidget(add); row->addWidget(rename); row->addWidget(remove); row->addWidget(setup); row->addStretch();
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); outer->addWidget(buttons);
  const bool writable = !ActiveDrawingReadOnly();
  for (auto* b : {add, rename, remove}) b->setEnabled(writable);
  auto refill = [&] {
    list->clear();
    list->addItem("Model");
    for (const auto& layout : m_State.paperLayouts) list->addItem(QString::fromUtf8(layout.name.c_str()));
    list->setCurrentRow(m_State.activeSpaceIndex < 0 ? 0 : m_State.activeSpaceIndex + 1);
    setup->setEnabled(list->currentRow() > 0);
  };
  refill();
  connect(setCurrent, &QPushButton::clicked, &dialog, [&] {
    const int rowIndex = list->currentRow();
    if (rowIndex >= 0) { SetActiveSpace(m_State, rowIndex == 0 ? kModelSpaceIndex : rowIndex - 1); Refresh(); refill(); }
  });
  connect(list, &QListWidget::currentRowChanged, &dialog, [&, list, setup](int rowIndex) { setup->setEnabled(rowIndex > 0); });
  connect(list, &QListWidget::itemDoubleClicked, &dialog, [&, list](QListWidgetItem*) {
    const int rowIndex = list->currentRow();
    SetActiveSpace(m_State, rowIndex == 0 ? kModelSpaceIndex : rowIndex - 1); Refresh();
  });
  connect(add, &QPushButton::clicked, &dialog, [&] {
    PushUndoSnapshot(m_State, "Create layout");
    const int index = AddPaperLayout(m_State);
    SetActiveSpace(m_State, index); Refresh(); refill(); list->setCurrentRow(index + 1);
  });
  connect(rename, &QPushButton::clicked, &dialog, [&] {
    const int rowIndex = list->currentRow(); if (rowIndex <= 0) return;
    const int index = rowIndex - 1;
    bool ok = false;
    const QString name = QInputDialog::getText(&dialog, "Rename Layout", "Layout name:", QLineEdit::Normal,
        QString::fromUtf8(m_State.paperLayouts[static_cast<size_t>(index)].name.c_str()), &ok).trimmed();
    if (!ok) return;
    const auto previous = m_Log.size();
    HandleLayoutCommand("RENAME " + m_State.paperLayouts[static_cast<size_t>(index)].name + " " + name.toUtf8().toStdString(), m_State, m_Log);
    if (m_Log.size() > previous && m_Log.back().find("renamed") == std::string::npos)
      QMessageBox::warning(&dialog, "Rename Layout", QString::fromUtf8(m_Log.back().c_str()));
    Refresh(); refill(); list->setCurrentRow(index + 1);
  });
  connect(remove, &QPushButton::clicked, &dialog, [&] {
    const int rowIndex = list->currentRow(); if (rowIndex <= 0) return;
    const int index = rowIndex - 1;
    const QString name = list->currentItem()->text();
    if (QMessageBox::question(&dialog, "Delete Layout", "Delete layout “" + name + "”? This can be undone.") != QMessageBox::Yes) return;
    DeletePaperLayout(m_State, index); Refresh(); refill();
  });
  connect(setup, &QPushButton::clicked, &dialog, [&] { const int rowIndex = list->currentRow(); if (rowIndex > 0) ShowPageSetupManager(rowIndex - 1); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
  dialog.exec();
}

void EditorWindow::ShowPageSetupManager(int layoutIndex) {
  m_State.showPageSetupManager = false;
  if (layoutIndex < 0) layoutIndex = m_State.activeSpaceIndex;
  if (layoutIndex < 0 || layoutIndex >= static_cast<int>(m_State.paperLayouts.size())) {
    QMessageBox::information(this, "Page Setup Manager", "Switch to a paper layout first."); return;
  }
  m_State.pageSetupLayoutIdx = layoutIndex;
  const bool writable = !ActiveDrawingReadOnly();
  if (writable) EnsureStandardPageSetup(m_State);
  QDialog dialog(this); dialog.setObjectName("pageSetupManager"); dialog.setWindowTitle("Page Setup Manager"); dialog.resize(650, 430);
  auto* outer = new QVBoxLayout(&dialog);
  auto* current = new QLabel("Current layout: “" + QString::fromUtf8(m_State.paperLayouts[static_cast<size_t>(layoutIndex)].name.c_str()) + "”", &dialog);
  outer->addWidget(current);
  auto* row = new QHBoxLayout(); outer->addLayout(row);
  auto* list = new QListWidget(&dialog); list->setObjectName("pageSetupList"); list->setMinimumWidth(250); row->addWidget(list, 1);
  auto* details = new QLabel(&dialog); details->setObjectName("pageSetupDetails"); details->setWordWrap(true); row->addWidget(details, 1);
  auto* actions = new QVBoxLayout(); row->addLayout(actions);
  auto* apply = new QPushButton("Set Current", &dialog); apply->setObjectName("pageSetupApply");
  auto* create = new QPushButton("New…", &dialog); create->setObjectName("pageSetupNew");
  auto* modify = new QPushButton("Modify…", &dialog); modify->setObjectName("pageSetupModify");
  auto* close = new QPushButton("Close", &dialog); close->setObjectName("pageSetupClose");
  actions->addWidget(apply); actions->addWidget(create); actions->addWidget(modify); actions->addStretch(); actions->addWidget(close);
  auto* onNew = new QCheckBox("Display when creating a new layout", &dialog); onNew->setChecked(m_State.pageSetupDisplayOnNew); onNew->setEnabled(writable); outer->addWidget(onNew);
  const auto& layout = m_State.paperLayouts[static_cast<size_t>(layoutIndex)];
  const QString layoutRow = "*" + QString::fromUtf8(layout.name.c_str()) + "*";
  auto refill = [&] {
    list->clear(); list->addItem(layoutRow);
    for (const auto& setup : m_State.savedPageSetups) list->addItem(QString::fromUtf8(setup.name.c_str()));
    int selected = m_State.pageSetupManagerSel < 0 ? 0 : m_State.pageSetupManagerSel + 1;
    list->setCurrentRow(std::clamp(selected, 0, list->count() - 1));
  };
  auto updateDetails = [&] {
    const int selected = list->currentRow();
    const PageSetup value = selected > 0 && selected - 1 < static_cast<int>(m_State.savedPageSetups.size())
        ? m_State.savedPageSetups[static_cast<size_t>(selected - 1)] : PageSetupFromLayout(layout, "");
    const QString paper = value.presetIdx >= 0 && value.presetIdx < kPaperSizePresetCount
        ? QString::fromUtf8(kPaperSizePresets[value.presetIdx].name) : "Custom";
    const float width = value.landscape ? std::max(value.portraitWidthIn, value.portraitHeightIn) : std::min(value.portraitWidthIn, value.portraitHeightIn);
    const float height = value.landscape ? std::min(value.portraitWidthIn, value.portraitHeightIn) : std::max(value.portraitWidthIn, value.portraitHeightIn);
    details->setText(QString("Device\nGoSurvey PDF (vector)\n\nPaper\n%1\n%2 × %3 in · %4\n\nPlot area\nLayout\n\nScale\n%5")
        .arg(paper).arg(width, 0, 'f', 2).arg(height, 0, 'f', 2).arg(value.landscape ? "Landscape" : "Portrait")
        .arg(value.fitToPaper ? "Fit to paper" : QString::number(value.scaleModelPerPaperIn, 'g', 5) + " model units / inch"));
    const bool saved = selected > 0 && selected - 1 < static_cast<int>(m_State.savedPageSetups.size());
    apply->setEnabled(writable && saved); modify->setEnabled(writable); create->setEnabled(writable);
    m_State.pageSetupManagerSel = selected - 1;
  };
  refill(); updateDetails();
  connect(list, &QListWidget::currentRowChanged, &dialog, [&] { updateDetails(); });
  connect(apply, &QPushButton::clicked, &dialog, [&] {
    const int selected = list->currentRow() - 1;
    if (selected < 0 || selected >= static_cast<int>(m_State.savedPageSetups.size())) return;
    PushUndoSnapshot(m_State, "Apply page setup");
    ApplyPageSetupToLayout(m_State.paperLayouts[static_cast<size_t>(layoutIndex)], m_State.savedPageSetups[static_cast<size_t>(selected)]);
    BumpCadGpuCache(m_State); Refresh(); updateDetails();
  });
  connect(create, &QPushButton::clicked, &dialog, [&] {
    bool ok = false; const QString name = QInputDialog::getText(&dialog, "New Page Setup", "Name:", QLineEdit::Normal,
        QString("Setup%1").arg(m_State.savedPageSetups.size() + 1), &ok).trimmed();
    if (!ok) return;
    if (name.isEmpty()) { QMessageBox::warning(&dialog, "New Page Setup", "Enter a page setup name."); return; }
    for (const auto& item : m_State.savedPageSetups) if (QString::fromUtf8(item.name.c_str()).compare(name, Qt::CaseInsensitive) == 0) {
      QMessageBox::warning(&dialog, "New Page Setup", "A page setup with that name already exists."); return;
    }
    PageSetup value = PageSetupFromLayout(m_State.paperLayouts[static_cast<size_t>(layoutIndex)], name.toUtf8().toStdString());
    m_State.savedPageSetups.push_back(value); m_State.pageSetupManagerSel = static_cast<int>(m_State.savedPageSetups.size()) - 1;
    refill(); updateDetails();
  });
  connect(modify, &QPushButton::clicked, &dialog, [&] { const int target = list->currentRow() - 1; dialog.accept(); ShowPageSetupEditor(target); });
  connect(onNew, &QCheckBox::toggled, &dialog, [&](bool value) { m_State.pageSetupDisplayOnNew = value; });
  connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  dialog.exec();
  Refresh();
}

void EditorWindow::ShowPageSetupEditor(int targetIndex) {
  const int layoutIndex = m_State.pageSetupLayoutIdx;
  const bool saved = targetIndex >= 0 && targetIndex < static_cast<int>(m_State.savedPageSetups.size());
  if (!saved && (layoutIndex < 0 || layoutIndex >= static_cast<int>(m_State.paperLayouts.size()))) return;
  PageSetup draft = saved ? m_State.savedPageSetups[static_cast<size_t>(targetIndex)]
      : PageSetupFromLayout(m_State.paperLayouts[static_cast<size_t>(layoutIndex)], "*" + m_State.paperLayouts[static_cast<size_t>(layoutIndex)].name + "*");
  QDialog dialog(this); dialog.setObjectName("pageSetupEditor"); dialog.setWindowTitle("Page Setup — " + QString::fromUtf8(draft.name.c_str()));
  auto* form = new QFormLayout(&dialog);
  auto* paper = new QComboBox(&dialog); paper->setObjectName("pageSetupPaper");
  for (int i = 0; i < kPaperSizePresetCount; ++i) paper->addItem(QString::fromUtf8(kPaperSizePresets[i].name), i);
  paper->addItem("Custom", -1); paper->setCurrentIndex(std::max(0, draft.presetIdx < 0 ? paper->count() - 1 : draft.presetIdx)); form->addRow("Paper size", paper);
  auto* orientation = new QComboBox(&dialog); orientation->addItems({"Portrait", "Landscape"}); orientation->setCurrentIndex(draft.landscape ? 1 : 0); form->addRow("Orientation", orientation);
  auto* customWidth = new QDoubleSpinBox(&dialog); customWidth->setRange(0.1, 1e5); customWidth->setDecimals(3); customWidth->setSuffix(" in"); customWidth->setValue(draft.portraitWidthIn); form->addRow("Custom width", customWidth);
  auto* customHeight = new QDoubleSpinBox(&dialog); customHeight->setRange(0.1, 1e5); customHeight->setDecimals(3); customHeight->setSuffix(" in"); customHeight->setValue(draft.portraitHeightIn); form->addRow("Custom height", customHeight);
  auto* fit = new QCheckBox("Fit to paper", &dialog); fit->setChecked(draft.fitToPaper); form->addRow("Plot scale", fit);
  auto* scale = new QDoubleSpinBox(&dialog); scale->setRange(0.0001, 1e9); scale->setDecimals(4); scale->setValue(std::max(0.0001f, draft.scaleModelPerPaperIn)); form->addRow("Model units / inch", scale);
  auto* offsetX = new QDoubleSpinBox(&dialog); offsetX->setRange(-1e6, 1e6); offsetX->setDecimals(3); offsetX->setValue(draft.offsetXIn); form->addRow("Offset X (in)", offsetX);
  auto* offsetY = new QDoubleSpinBox(&dialog); offsetY->setRange(-1e6, 1e6); offsetY->setDecimals(3); offsetY->setValue(draft.offsetYIn); form->addRow("Offset Y (in)", offsetY);
  auto* center = new QCheckBox("Center the plot", &dialog); center->setChecked(draft.centerPlot); form->addRow("Placement", center);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
  const bool writable = !ActiveDrawingReadOnly();
  for (QWidget* widget : {static_cast<QWidget*>(paper), static_cast<QWidget*>(orientation), static_cast<QWidget*>(customWidth), static_cast<QWidget*>(customHeight), static_cast<QWidget*>(fit), static_cast<QWidget*>(scale), static_cast<QWidget*>(offsetX), static_cast<QWidget*>(offsetY), static_cast<QWidget*>(center)}) widget->setEnabled(writable);
  auto updateCustom = [paper, customWidth, customHeight, writable] {
    const bool custom = paper->currentData().toInt() < 0;
    customWidth->setEnabled(custom && writable);
    customHeight->setEnabled(custom && writable);
  };
  connect(paper, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [updateCustom](int) { updateCustom(); });
  updateCustom();
  connect(fit, &QCheckBox::toggled, scale, &QWidget::setDisabled);
  scale->setDisabled(!writable || draft.fitToPaper);
  connect(paper, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
    const int preset = paper->itemData(index).toInt(); if (preset >= 0 && preset < kPaperSizePresetCount) {
      draft.presetIdx = preset; draft.portraitWidthIn = kPaperSizePresets[preset].widthIn; draft.portraitHeightIn = kPaperSizePresets[preset].heightIn;
    } else draft.presetIdx = -1;
  });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted || !writable) { ShowPageSetupManager(layoutIndex); return; }
  const int preset = paper->currentData().toInt(); draft.presetIdx = preset;
  if (preset >= 0 && preset < kPaperSizePresetCount) { draft.portraitWidthIn = kPaperSizePresets[preset].widthIn; draft.portraitHeightIn = kPaperSizePresets[preset].heightIn; }
  else { draft.portraitWidthIn = static_cast<float>(customWidth->value()); draft.portraitHeightIn = static_cast<float>(customHeight->value()); }
  draft.landscape = orientation->currentIndex() == 1; draft.fitToPaper = fit->isChecked(); draft.scaleModelPerPaperIn = static_cast<float>(scale->value());
  draft.offsetXIn = static_cast<float>(offsetX->value()); draft.offsetYIn = static_cast<float>(offsetY->value()); draft.centerPlot = center->isChecked();
  if (saved) m_State.savedPageSetups[static_cast<size_t>(targetIndex)] = draft;
  else {
    PushUndoSnapshot(m_State, "Modify page setup");
    ApplyPageSetupToLayout(m_State.paperLayouts[static_cast<size_t>(layoutIndex)], draft);
  }
  BumpCadGpuCache(m_State); Refresh(); ShowPageSetupManager(layoutIndex);
}

void EditorWindow::ShowViewportScale() {
  Viewport* viewport = CurrentViewport(m_State);
  if (!viewport) { QMessageBox::information(this, "Viewport Scale", "Select one viewport in a paper layout or enter floating model space first."); return; }
  bool ok = false;
  const QString current = QString::fromUtf8(PlotScaleLabel(m_State.drawingInsUnits, viewport->safeScale()).c_str());
  const QString scale = QInputDialog::getText(this, "Viewport Scale", "Enter a scale such as 1:50 or model units per inch:", QLineEdit::Normal, current, &ok);
  if (!ok) return;
  if (ActiveDrawingReadOnly()) { QMessageBox::information(this, "Read-only drawing", "Viewport scale cannot be changed in a read-only drawing."); return; }
  const auto previous = m_Log.size(); HandleVpscaleCommand(scale.toUtf8().toStdString(), m_State, m_Log);
  if (m_Log.size() > previous && m_Log.back().find("could not read") != std::string::npos)
    QMessageBox::warning(this, "Viewport Scale", QString::fromUtf8(m_Log.back().c_str()));
  Refresh();
}

void EditorWindow::ShowViewportsManager() {
  const int layoutIndex = m_State.activeSpaceIndex;
  if (layoutIndex < 0 || layoutIndex >= static_cast<int>(m_State.paperLayouts.size())) {
    QMessageBox::information(this, "Viewports", "Switch to a paper layout first."); return;
  }
  QDialog dialog(this); dialog.setObjectName("viewportsManager"); dialog.setWindowTitle("Viewports — " + QString::fromUtf8(m_State.paperLayouts[static_cast<size_t>(layoutIndex)].name.c_str())); dialog.resize(450, 350);
  auto* outer = new QVBoxLayout(&dialog);
  auto* list = new QListWidget(&dialog); list->setObjectName("viewportsList"); outer->addWidget(list, 1);
  auto* annotationScaleRow = new QHBoxLayout(); outer->addLayout(annotationScaleRow);
  annotationScaleRow->addWidget(new QLabel("Annotative scale:", &dialog));
  auto* annotationScale = new QComboBox(&dialog); annotationScale->setObjectName("viewportAnnotationScale");
  for (const CadAnnotationScale& scale : m_State.annotationScales)
    annotationScale->addItem(QString::fromUtf8(scale.name.c_str()), QString::fromUtf8(scale.name.c_str()));
  if (annotationScale->count() == 0) {
    annotationScale->addItem("No annotation scales");
    annotationScale->setEnabled(false);
  }
  annotationScaleRow->addWidget(annotationScale, 1);
  auto* row = new QHBoxLayout(); outer->addLayout(row);
  auto* add = new QPushButton("Add viewport", &dialog); add->setObjectName("viewportAdd");
  auto* scale = new QPushButton("Set scale…", &dialog); scale->setObjectName("viewportScale");
  auto* remove = new QPushButton("Delete viewport", &dialog); remove->setObjectName("viewportDelete");
  auto* close = new QPushButton("Close", &dialog); close->setObjectName("viewportClose");
  row->addWidget(add); row->addWidget(scale); row->addWidget(remove); row->addStretch(); row->addWidget(close);
  const bool writable = !ActiveDrawingReadOnly(); add->setEnabled(writable); scale->setEnabled(writable); remove->setEnabled(writable);
  auto refill = [&] {
    list->clear(); const auto& viewports = m_State.paperLayouts[static_cast<size_t>(layoutIndex)].viewports;
    for (size_t i = 0; i < viewports.size(); ++i) {
      const auto& vp = viewports[i];
      list->addItem(QString("Viewport %1 — %2 · %3 × %4 in")
          .arg(i + 1).arg(QString::fromUtf8(PlotScaleLabel(m_State.drawingInsUnits, vp.safeScale()).c_str()))
          .arg(vp.paperWIn, 0, 'f', 2).arg(vp.paperHIn, 0, 'f', 2));
    }
    if (list->count() > 0) list->setCurrentRow(std::clamp(m_State.selectedViewportIndex, 0, list->count() - 1));
    const int selected = list->currentRow();
    const bool hasViewport = selected >= 0 && static_cast<size_t>(selected) < viewports.size();
    const std::string effectiveScale = hasViewport
        ? CadActiveAnnotationScaleName(&m_State.annotationScales, m_State.currentAnnotationScaleIndex,
                                       &viewports[static_cast<size_t>(selected)], m_State.modelUnitsPerPlottedInch)
        : std::string{};
    const QSignalBlocker block(annotationScale);
    const int scaleIndex = annotationScale->findData(QString::fromUtf8(effectiveScale.c_str()));
    annotationScale->setCurrentIndex(scaleIndex);
    annotationScale->setEnabled(writable && hasViewport && scaleIndex >= 0);
    scale->setEnabled(writable && list->currentRow() >= 0); remove->setEnabled(writable && list->currentRow() >= 0);
  };
  refill();
  connect(list, &QListWidget::currentRowChanged, &dialog, [&, writable](int rowIndex) {
    if (rowIndex >= 0) SelectViewport(m_State, rowIndex, false);
    scale->setEnabled(writable && rowIndex >= 0); remove->setEnabled(writable && rowIndex >= 0);
    const auto& viewports = m_State.paperLayouts[static_cast<size_t>(layoutIndex)].viewports;
    const bool hasViewport = rowIndex >= 0 && static_cast<size_t>(rowIndex) < viewports.size();
    const std::string effectiveScale = hasViewport
        ? CadActiveAnnotationScaleName(&m_State.annotationScales, m_State.currentAnnotationScaleIndex,
                                       &viewports[static_cast<size_t>(rowIndex)], m_State.modelUnitsPerPlottedInch)
        : std::string{};
    const QSignalBlocker block(annotationScale);
    annotationScale->setCurrentIndex(annotationScale->findData(QString::fromUtf8(effectiveScale.c_str())));
    annotationScale->setEnabled(writable && hasViewport && annotationScale->currentIndex() >= 0);
  });
  connect(annotationScale, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&, writable](int index) {
    const int viewportIndex = list->currentRow();
    if (!writable || index < 0 || viewportIndex < 0)
      return;
    auto& viewports = m_State.paperLayouts[static_cast<size_t>(layoutIndex)].viewports;
    if (static_cast<size_t>(viewportIndex) >= viewports.size())
      return;
    const std::string name = annotationScale->itemData(index).toString().toUtf8().toStdString();
    Viewport& viewport = viewports[static_cast<size_t>(viewportIndex)];
    if (viewport.annotationScaleName == name)
      return;
    PushUndoSnapshot(m_State, "Viewport annotative scale");
    viewport.annotationScaleName = name;
    BumpCadGpuCache(m_State);
    Refresh();
  });
  connect(add, &QPushButton::clicked, &dialog, [&] {
    PushUndoSnapshot(m_State, "Create viewport");
    const int index = AddViewport(m_State, layoutIndex); Refresh(); refill(); list->setCurrentRow(index);
  });
  connect(scale, &QPushButton::clicked, &dialog, [&] {
    const int index = list->currentRow(); if (index < 0) return;
    SelectViewport(m_State, index, false);
    bool ok = false;
    const auto& viewport = m_State.paperLayouts[static_cast<size_t>(layoutIndex)].viewports[static_cast<size_t>(index)];
    const QString value = QInputDialog::getText(&dialog, "Viewport Scale", "Scale (for example 1:50 or 50 model units/in):", QLineEdit::Normal,
        QString::fromUtf8(PlotScaleLabel(m_State.drawingInsUnits, viewport.safeScale()).c_str()), &ok);
    if (!ok) return;
    const auto previous = m_Log.size(); HandleVpscaleCommand(value.toUtf8().toStdString(), m_State, m_Log);
    if (m_Log.size() > previous && m_Log.back().find("could not read") != std::string::npos)
      QMessageBox::warning(&dialog, "Viewport Scale", QString::fromUtf8(m_Log.back().c_str()));
    Refresh(); refill(); list->setCurrentRow(index);
  });
  connect(remove, &QPushButton::clicked, &dialog, [&] {
    const int index = list->currentRow(); if (index < 0) return;
    if (QMessageBox::question(&dialog, "Delete Viewport", "Delete the selected viewport? This can be undone.") != QMessageBox::Yes) return;
    PushUndoSnapshot(m_State, "Delete viewport"); DeleteViewport(m_State, layoutIndex, index); Refresh(); refill();
  });
  connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  dialog.exec(); Refresh();
}

bool EditorWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress && m_CommandSuggestions && m_CommandSuggestions->isVisible()) {
    auto* clicked = qobject_cast<QWidget*>(watched);
    const bool inSuggestions = clicked &&
        (clicked == m_CommandSuggestions || m_CommandSuggestions->isAncestorOf(clicked));
    const bool inCommand = clicked && (clicked == m_Command || m_Command->isAncestorOf(clicked));
    if (!inSuggestions && !inCommand) m_CommandSuggestions->hide();
  }
  if (DraftingInputEvent(watched, event)) return true;
  auto* keyWidget = qobject_cast<QWidget*>(watched);
  const bool suggestionPopupWidget = keyWidget && m_CommandSuggestions &&
      (keyWidget == m_CommandSuggestions || m_CommandSuggestions->isAncestorOf(keyWidget));
  if ((watched == m_Command || suggestionPopupWidget) && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      if (m_CommandSuggestions && m_CommandSuggestions->isVisible()) {
        m_CommandSuggestions->hide();
        m_Command->clear();
        return true;
      }
      m_State.showCreatePointsWindow = false;
      CancelActiveCommand(m_State, m_Log);
      m_Command->clear(); m_Viewport->InvalidateGeometry(); Refresh();
      return true;
    }
    if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
      if (m_CommandSuggestions && m_CommandSuggestions->isVisible() && m_CommandSuggestionModel &&
          m_CommandSuggestionModel->rowCount() > 0) {
        int row = m_CommandSuggestions->currentIndex().row();
        if (row < 0) row = 0;
        else row = std::clamp(row + (key->key() == Qt::Key_Up ? -1 : 1), 0,
                              m_CommandSuggestionModel->rowCount() - 1);
        const QModelIndex next = m_CommandSuggestionModel->index(row, 0);
        m_CommandSuggestions->setCurrentIndex(next);
        m_CommandSuggestions->scrollTo(next);
        return true;
      }
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
        QTimer::singleShot(0, this, [this] {
          if (!QApplication::activeModalWidget() && !QApplication::activePopupWidget())
            m_Command->setFocus(Qt::OtherFocusReason);
        });
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
#ifdef _WIN32
  // Qt reapplies the frameless flag while first showing the native window, so
  // restore the shell-facing styles after that transition.
  const HWND hwnd = reinterpret_cast<HWND>(winId());
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
      WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
  if (auto* grip = findChild<QSizeGrip*>("editorResizeGrip"))
    grip->setVisible(!isMaximized() && !isFullScreen());
  if (statusBar() != nullptr && statusBar()->layout() != nullptr)
    statusBar()->layout()->setContentsMargins(4, 3, 6, 8);
  m_Tabs->updateGeometry();
  if (centralWidget()->layout()) centralWidget()->layout()->activate();
  m_Tabs->update();
  RefreshRecentProjects();
  RefreshRecentDrawings();
}

void EditorWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange) {
    if (auto* grip = findChild<QSizeGrip*>("editorResizeGrip"))
      grip->setVisible(!isMaximized() && !isFullScreen());
  }
}

void EditorWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (m_Viewport) m_Viewport->UpdateWindowResizePreviewGeometry();
  const bool compact = width() < 1120;
  if (auto* columns = findChild<QBoxLayout*>("startContentColumns")) {
    const auto direction = compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (columns->direction() != direction) columns->setDirection(direction);
  }
  if (auto* sidePanel = findChild<QWidget*>("startRightColumn")) {
    const int maximum = compact ? QWIDGETSIZE_MAX : 560;
    const int minimum = compact ? 0 : 430;
    if (sidePanel->maximumWidth() != maximum) sidePanel->setMaximumWidth(maximum);
    if (sidePanel->minimumWidth() != minimum) sidePanel->setMinimumWidth(minimum);
  }
}

void EditorWindow::BeginInteractiveResize() {
  if (m_Viewport) m_Viewport->BeginWindowResizePreview();
}

void EditorWindow::EndInteractiveResize() {
  if (m_Viewport) m_Viewport->EndWindowResizePreview();
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
  if (projectUid == 0 && m_State.activeDrawingIdx > 0 &&
      m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size()))
    projectUid = m_State.drawingTabs[static_cast<size_t>(m_State.activeDrawingIdx)].projectUid;
  if (projectUid == 0) {
    ShowDrawingTemplateChooser();
    return;
  }
  CreateDrawingFromTemplate(projectUid);
}

void EditorWindow::CreateDrawingFromTemplate(std::uint32_t projectUid, const QString& templatePath) {
  RefreshProjects();
  const int previous = m_State.activeDrawingIdx;
  SaveDocumentToSnapshot(m_State, m_State.activeDrawingIdx);
  const int index = static_cast<int>(m_State.drawingTabs.size());
  m_State.drawingTabs.push_back({"Drawing " + std::to_string(m_State.nextDrawingNumber++), m_State.nextTabUid++});
  m_State.drawingTabs.back().projectUid = projectUid;
  m_State.documents.emplace_back();
  RestoreDocumentFromSnapshot(m_State, index);
  m_State.activeDrawingIdx = index;
  m_State.prevDrawingIdx = index;
  LoadBundledBlockLibrary(m_State, m_Log);
  if (!templatePath.isEmpty()) {
    const QByteArray pathUtf8 = templatePath.toUtf8();
    // REQ-450: the cursor and snap aperture are app-wide preferences; the template's copy must not win.
    const float crosshairR = m_State.viewportCrosshairR, crosshairG = m_State.viewportCrosshairG,
                crosshairB = m_State.viewportCrosshairB;
    const float pickHalfX = m_State.viewportCrosshairPickHalfPxX, pickHalfY = m_State.viewportCrosshairPickHalfPxY;
    const float snapAperture = m_State.objectSnapAperturePx;
    const bool loaded = LoadGoSurveyTemplateFile(m_State, pathUtf8.constData(), m_Log);
    m_State.objectSnapAperturePx = snapAperture;
    m_State.viewportCrosshairR = crosshairR; m_State.viewportCrosshairG = crosshairG;
    m_State.viewportCrosshairB = crosshairB;
    m_State.viewportCrosshairPickHalfPxX = pickHalfX; m_State.viewportCrosshairPickHalfPxY = pickHalfY;
    if (!loaded) {
      m_State.drawingTabs.pop_back();
      m_State.documents.pop_back();
      RestoreDocumentFromSnapshot(m_State, previous);
      m_State.activeDrawingIdx = previous;
      m_State.prevDrawingIdx = previous;
      m_Viewport->InvalidateGeometry();
      Refresh();
      QMessageBox::warning(this, "New drawing", "The selected template could not be loaded or validated. See the command log.");
      return;
    }
    m_State.activeDocFilePath.clear();
    m_State.activeDocSavedRevision = m_State.cadGpuRevision;
  }
  m_Viewport->InvalidateGeometry();
  Refresh();
}

void EditorWindow::ShowDrawingTemplateChooser() {
  QDialog dialog(this);
  dialog.setObjectName("drawingTemplateDialog");
  dialog.setWindowTitle("New Drawing");
  dialog.setMinimumSize(720, 430);
  auto* layout = new QVBoxLayout(&dialog);
  auto* heading = new QLabel("New Drawing", &dialog);
  heading->setStyleSheet("font-size:20px; font-weight:600;");
  layout->addWidget(heading);
  auto* intro = new QLabel("Choose a blank drawing or start from a template.", &dialog);
  layout->addWidget(intro);
  auto* list = new QListWidget(&dialog);
  list->setObjectName("drawingTemplateList");
  const auto choices = DiscoverDrawingTemplates();
  for (const auto& choice : choices) {
    auto* item = new QListWidgetItem(choice.label, list);
    item->setData(Qt::UserRole, choice.path);
    item->setData(Qt::UserRole + 1, choice.blank);
  }
  if (list->count()) list->setCurrentRow(0);
  list->hide();
  auto* tiles = new QWidget(&dialog);
  auto* grid = new QGridLayout(tiles);
  grid->setSpacing(12);
  grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  auto* group = new QButtonGroup(tiles);
  group->setExclusive(true);
  for (int row = 0; row < choices.size(); ++row) {
    const auto& choice = choices[row];
    auto* tile = new QPushButton(choice.label + "\n\n" + (choice.blank ?
        "Start with an empty drawing" : "Use this drawing template"), tiles);
    tile->setObjectName(QString("drawingTemplateTile%1").arg(row));
    tile->setCheckable(true);
    tile->setFixedSize(228, 124);
    tile->setIcon(style()->standardIcon(choice.blank ? QStyle::SP_FileIcon : QStyle::SP_DirIcon));
    tile->setIconSize(QSize(34, 34));
    group->addButton(tile, row);
    grid->addWidget(tile, row / 3, row % 3);
    connect(tile, &QPushButton::clicked, &dialog, [list, row] { list->setCurrentRow(row); });
    if (row == 0) tile->setChecked(true);
  }
  connect(list, &QListWidget::currentRowChanged, &dialog, [group](int row) {
    if (auto* button = group->button(row)) button->setChecked(true);
  });
  layout->addWidget(tiles, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Open, &dialog);
  buttons->setObjectName("drawingTemplateButtons");
  buttons->button(QDialogButtonBox::Open)->setText("Create Drawing");
  buttons->button(QDialogButtonBox::Open)->setEnabled(list->currentItem() != nullptr);
  connect(list, &QListWidget::currentRowChanged, &dialog, [buttons](int row) {
    buttons->button(QDialogButtonBox::Open)->setEnabled(row >= 0);
  });
  connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem*) { dialog.accept(); });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  m_ModalOpen = true;
  if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
    const bool blank = list->currentItem()->data(Qt::UserRole + 1).toBool();
    CreateDrawingFromTemplate(0, blank ? QString{} : list->currentItem()->data(Qt::UserRole).toString());
  }
  m_ModalOpen = false;
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
  CreateDrawingFromTemplate(uid);
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

// The Toolspace tree is rebuilt only when what it lists changes. Refresh() runs on every viewport click,
// and rebuilding the tree (icons and all) took ~100 ms of each one.
void EditorWindow::RefreshToolspace() {
  QStringList key;
  for (const auto& session : m_State.openProjects) {
    key << QString::fromUtf8(session.project.name.c_str()) + (session.readOnly ? "|ro" : "|rw");
    for (const auto& item : session.project.items) key << QString::fromUtf8(item.path.c_str());
  }
  key << "#drawings";
  for (std::size_t i = 1; i < m_State.drawingTabs.size(); ++i) key << QString::fromUtf8(m_State.drawingTabs[i].name.c_str());
  key << "#points" + QString::number(m_State.surveyPoints.size()) << "#surfaces";
  for (const auto& surface : m_State.cadSurfaces) key << QString::fromUtf8(surface.name.c_str());
  key << "#styles";
  for (const auto& surfaceStyle : m_State.surfaceStyles) key << QString::fromUtf8(surfaceStyle.name.c_str());
  key << "#blocks";
  for (const auto& block : m_State.blockDefs) key << QString::fromUtf8(block.name.c_str());
  if (m_ToolspaceKey == key && m_Toolspace->topLevelItemCount() > 0) return;
  m_ToolspaceKey = key;
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
  surfaces->setData(0, Qt::UserRole + 1, "surfacesFolder");
  for (int i = 0; i < static_cast<int>(m_State.cadSurfaces.size()); ++i) {
    auto* surface = new QTreeWidgetItem(surfaces, {QString::fromUtf8(m_State.cadSurfaces[static_cast<size_t>(i)].name.c_str())});
    surface->setData(0, Qt::UserRole + 1, "surface");  // Qt::UserRole itself is the drawing-tab index
    surface->setData(0, Qt::UserRole + 2, i);
  }
  auto* surfaceStyles = new QTreeWidgetItem(m_Toolspace, {"Surface Styles"});
  surfaceStyles->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
  surfaceStyles->setData(0, Qt::UserRole + 1, "stylesFolder");
  for (const auto& surfaceStyle : m_State.surfaceStyles) {
    auto* entry = new QTreeWidgetItem(surfaceStyles, {QString::fromUtf8(surfaceStyle.name.c_str())});
    entry->setData(0, Qt::UserRole + 1, "style");
    entry->setData(0, Qt::UserRole + 3, QString::fromUtf8(surfaceStyle.name.c_str()));
  }
  auto* blocks = new QTreeWidgetItem(m_Toolspace, {"Blocks"});
  blocks->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
  for (const auto& block : m_State.blockDefs) new QTreeWidgetItem(blocks, {QString::fromUtf8(block.name.c_str())});
  if (!initialTree) for (QTreeWidgetItemIterator it(m_Toolspace); *it; ++it) {
    (*it)->setExpanded(expanded.contains(itemPath(*it)));
    if (itemPath(*it) == currentItem) m_Toolspace->setCurrentItem(*it);
  }
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
  if (auto* viewportZoom = findChild<QComboBox*>("statusViewportZoom")) {
    EnsureDefaultViewportZoomScales(m_State.viewportZoomScales);
    const float dpi = screen() != nullptr ? static_cast<float>(screen()->logicalDotsPerInchY()) : 96.f;
    const float heightPx = m_Viewport != nullptr ? static_cast<float>(m_Viewport->height()) : 800.f;
    SyncCurrentViewportZoomScaleIndex(m_State, heightPx, dpi);
    const QSignalBlocker block(viewportZoom);
    viewportZoom->clear();
    for (const CadAnnotationScale& scale : m_State.viewportZoomScales)
      viewportZoom->addItem(QString::fromUtf8(CadViewportZoomScaleStatusLabel(scale).c_str()));
    viewportZoom->addItem(tr("Add custom scale…"), QStringLiteral("__add__"));
    if (m_State.currentViewportZoomScaleIndex >= 0)
      viewportZoom->setCurrentIndex(m_State.currentViewportZoomScaleIndex);
    else
      viewportZoom->setCurrentIndex(-1);
    const bool modelSpace = m_State.activeSpaceIndex == kModelSpaceIndex && !InFloatingModelSpace(m_State);
    viewportZoom->setEnabled(modelSpace && !m_State.viewportZoomScales.empty());
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
  if (m_Tabs->count() > 0)
    m_Tabs->setTabIcon(0, windowIcon().pixmap(16, 16));
  m_Tabs->setCurrentIndex(m_State.activeDrawingIdx);
  {
    const QSignalBlocker spaceBlock(m_SpaceTabs);
    while (m_SpaceTabs->count() > static_cast<int>(m_State.paperLayouts.size()) + 1)
      m_SpaceTabs->removeTab(m_SpaceTabs->count() - 1);
    const QString modelLabel = m_State.floatingViewportIndex >= 0 ? "Model (Floating)" : "Model";
    if (m_SpaceTabs->count() == 0) m_SpaceTabs->addTab(modelLabel);
    else m_SpaceTabs->setTabText(0, modelLabel);
    for (int i = 0; i < static_cast<int>(m_State.paperLayouts.size()); ++i) {
      const QString name = QString::fromUtf8(m_State.paperLayouts[static_cast<size_t>(i)].name.c_str());
      if (i + 1 == m_SpaceTabs->count()) m_SpaceTabs->addTab(name);
      else m_SpaceTabs->setTabText(i + 1, name);
    }
    m_SpaceTabs->setCurrentIndex(m_State.activeSpaceIndex < 0 ? 0 : m_State.activeSpaceIndex + 1);
  }
  if (auto* spaceBar = findChild<QWidget*>("paperSpaceBar"))
    spaceBar->setVisible(m_State.activeDrawingIdx > 0);
  if (auto* drawingTabRow = findChild<QWidget*>("drawingTabRow"))
    drawingTabRow->setVisible(true);
  m_Tabs->update();
  m_Pages->setCurrentIndex(m_State.activeDrawingIdx == 0 ? 0 : 1);
  const bool drawingActive = m_State.activeDrawingIdx > 0;
  if (auto* drawingSettings = findChild<QAction*>("command_DRAWINGSETTINGS"))
    drawingSettings->setEnabled(drawingActive);
  if (auto* ribbon = findChild<QToolBar*>("ribbonBar")) ribbon->setVisible(true);
  m_Tabs->setVisible(true);
  statusBar()->setVisible(true);
  for (auto* button : findChildren<QToolButton*>())
    if (button->objectName().startsWith("quick_")) button->setVisible(drawingActive);
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
  RefreshDraftingInput(commandHint);
  if (m_WindowTitle) {
    const auto title = m_DevelopmentBranchLabel.isEmpty()
        ? QString("GoSurvey") : QString("GoSurvey — ") + m_DevelopmentBranchLabel;
    m_WindowTitle->setText(title); setWindowTitle(title);
  }
  RefreshProperties();
  RefreshToolspace();
  RefreshRibbon();
  m_Viewport->RefreshCommandCursor();
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
  QStringList imageNames;
  if (markdown.open(QIODevice::ReadOnly)) {
    notes = QString::fromUtf8(markdown.readAll());
    // QTextBrowser has no handler for the Markdown's custom `whats-new:` image URLs.
    const QRegularExpression imageUrl(R"(whats-new:([A-Za-z0-9_.-]+))");
    auto match = imageUrl.globalMatch(notes);
    while (match.hasNext()) {
      const auto found = match.next();
      const QString name = found.captured(1);
      if (!imageNames.contains(name)) imageNames.push_back(name);
    }
  }
  if (notes.trimmed().isEmpty())
    notes = "What's New content could not be loaded from resources/whats-new.md for GoSurvey " +
            QString::fromUtf8(GOSURVEY_VERSION_FULL) + ". You can view the release notes online below.";
  browser->setMarkdown(notes);
  // setMarkdown clears document resources, so install bundled images afterward. Scaling wide
  // screenshots keeps them inside the release-notes pane.
  for (const auto& name : imageNames) {
    QImage image(QtAsset("whats-new/" + name));
    if (image.isNull()) continue;
    if (image.width() > 460) image = image.scaledToWidth(460, Qt::SmoothTransformation);
    browser->document()->addResource(QTextDocument::ImageResource, QUrl("whats-new:" + name), image);
  }
  browser->document()->markContentsDirty(0, browser->document()->characterCount());
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
  auto* rows = new QVBoxLayout(m_RecentTiles);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(3);
  const auto dir = UserDataDirectory().empty() ? std::filesystem::path(".") : UserDataDirectory();
  auto entries = recent::Load(dir / "gosurvey-recent.json");
  const QString query = m_RecentShowAll && m_RecentSearch ? m_RecentSearch->text().trimmed() : QString();
  if (!query.isEmpty())
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
      return !QString::fromUtf8(entry.name.c_str()).contains(query, Qt::CaseInsensitive) &&
             !QString::fromUtf8(entry.path.c_str()).contains(query, Qt::CaseInsensitive);
    }), entries.end());
  if (auto* sort = findChild<QComboBox*>("recentDrawingSort"); sort && sort->currentIndex() == 1)
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  if (auto* gridMode = findChild<QToolButton*>("recentGridMode")) gridMode->setChecked(!m_RecentListView);
  if (auto* listMode = findChild<QToolButton*>("recentListMode")) listMode->setChecked(m_RecentListView);
  const size_t visibleEntries = m_RecentShowAll ? entries.size() : std::min<size_t>(5, entries.size());
  const auto recentDrawingStore = std::make_shared<const std::filesystem::path>(dir / "gosurvey-recent.json");
  const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
  const QString cardBg = dark ? "#242a2f" : "#ffffff";
  const QString cardEdge = dark ? "#3c444a" : "#c5c8cb";
  const QString cardHover = dark ? "#30373c" : "#f0f1f2";
  size_t index = 0;
  for (const auto& entry : entries) {
    if (index >= visibleEntries) break;
    const QString name = QString::fromUtf8(entry.name.c_str());
    const QString path = QString::fromUtf8(entry.path.c_str());
    const auto opened = QDateTime::fromSecsSinceEpoch(entry.lastOpenedUnix);
    const QString lastOpened = opened.toString("d MMM yyyy\nHH:mm");
    const QString projectTag = DrawingProjectTag(path);
    const QString folder = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
    const auto thumbPath = dir / "thumbnails" / std::filesystem::path(QString::fromUtf8(entry.thumb.c_str()).toStdWString());
    const QImage savedThumbnail(QString::fromStdWString(thumbPath.wstring()));
    QPixmap thumbnail;
    if (HasVisibleDrawingPreview(savedThumbnail)) {
      thumbnail = QPixmap::fromImage(savedThumbnail);
    } else {
      thumbnail = QPixmap(320, 144);
      thumbnail.fill(QColor(dark ? "#171b1e" : "#303234"));
      QPainter painter(&thumbnail);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(QColor(dark ? QtAccentFor(true).strong : "#c8cbce"));
      painter.setFont(QFont("Segoe UI", 24, QFont::DemiBold));
      painter.drawText(thumbnail.rect(), Qt::AlignCenter, "DWG");
    }
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
    auto showDrawingMenu = [this, path, recentDrawingStore, openRecentDrawing](QWidget* owner, const QPoint& globalPosition) noexcept {
      RunUiActionSafely([&] {
      const bool fileExists = QFileInfo::exists(path);
      QMenu menu(owner);
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
      menu.exec(globalPosition);
      });
    };
    if (!m_RecentListView) {
      auto* tile = new QToolButton(m_RecentTiles);
      tile->setObjectName("recentDrawingTile");
      tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      tile->setText(name + "\n" + opened.toString("d MMM yyyy"));
      tile->setIcon(QIcon(thumbnail));
      tile->setIconSize(QSize(144, 58));
      tile->setFixedSize(170, 130);
      tile->setStyleSheet(QString("QToolButton#recentDrawingTile { border:1px solid %1; border-radius:7px; background:%2; padding:7px; } QToolButton#recentDrawingTile:hover { border-color:%4; background:%3; }")
                              .arg(cardEdge, cardBg, cardHover, QtAccentFor(dark).strong));
      tile->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(tile, &QWidget::customContextMenuRequested, this, [tile, showDrawingMenu](const QPoint& pos) {
        showDrawingMenu(tile, tile->mapToGlobal(pos));
      });
      connect(tile, &QToolButton::clicked, this, openRecentDrawing);
      rows->addWidget(tile);
      ++index;
      continue;
    }
    auto* tile = new QToolButton(m_RecentTiles);
    tile->setObjectName("recentDrawingTile");
    tile->setCursor(Qt::PointingHandCursor);
    tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tile->setMinimumHeight(64);
    tile->setMaximumHeight(68);
    tile->setStyleSheet(QString("QToolButton#recentDrawingTile { text-align:left; border:1px solid %1; border-radius:7px; background:%2; padding:0; } QToolButton#recentDrawingTile:hover { border-color:%4; background:%3; }")
                            .arg(cardEdge, cardBg, cardHover, QtAccentFor(dark).strong));
    auto* row = new QHBoxLayout(tile);
    row->setContentsMargins(9, 4, 7, 4);
    row->setSpacing(9);
    auto* fileIcon = new QLabel(tile);
    fileIcon->setPixmap(CadCommandIcon("Active_File.png").pixmap(24, 24));
    fileIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(fileIcon);
    auto* identity = new QWidget(tile);
    auto* identityLayout = new QVBoxLayout(identity);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(1);
    auto* title = new QLabel(name, identity);
    title->setStyleSheet(QString("font-size:13px; font-weight:650; color:%1; border:0;").arg(dark ? "#f1f8fc" : "#202b36"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* fileName = new QLabel(QFileInfo(path).fileName(), identity);
    fileName->setStyleSheet(QString("font-size:11px; color:%1; border:0;").arg(dark ? "#9db5c5" : "#6b7884"));
    fileName->setAttribute(Qt::WA_TransparentForMouseEvents);
    identityLayout->addWidget(title);
    identityLayout->addWidget(fileName);
    row->addWidget(identity, 2);
    auto* preview = new QLabel(tile);
    preview->setFixedSize(120, 46);
    preview->setPixmap(thumbnail.scaled(preview->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QString("background:%1; border:1px solid %2; border-radius:5px;").arg(dark ? "#171b1e" : "#edf1f4", cardEdge));
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(preview, 1);
    auto* modified = new QLabel(lastOpened, tile);
    modified->setStyleSheet(QString("font-size:11px; line-height:1.3; color:%1; border:0;").arg(dark ? "#d1e0e9" : "#45525e"));
    modified->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(modified, 1);
    auto* location = new QWidget(tile);
    auto* locationLayout = new QHBoxLayout(location);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    locationLayout->setSpacing(7);
    auto* folderIcon = new QLabel(location);
    folderIcon->setPixmap(style()->standardIcon(QStyle::SP_DirIcon).pixmap(20, 20));
    folderIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* folderText = new QLabel(tile->fontMetrics().elidedText(folder, Qt::ElideMiddle, 250), location);
    folderText->setStyleSheet(QString("font-size:11px; color:%1; border:0;").arg(dark ? "#d1e0e9" : "#45525e"));
    folderText->setAttribute(Qt::WA_TransparentForMouseEvents);
    locationLayout->addWidget(folderIcon);
    locationLayout->addWidget(folderText, 1);
    location->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(location, 2);
    auto* more = new QToolButton(tile);
    more->setText("⋮");
    more->setAccessibleName("Recent drawing actions");
    more->setToolTip(name + " actions");
    more->setAutoRaise(true);
    connect(more, &QToolButton::clicked, this, [more, showDrawingMenu] {
      showDrawingMenu(more, more->mapToGlobal(QPoint(0, more->height())));
    });
    row->addWidget(more);
    tile->setToolTip(name + "\n" + path + "\n" + lastOpened + " · " + projectTag);
    tile->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tile, &QWidget::customContextMenuRequested, this, [tile, showDrawingMenu](const QPoint& pos) {
      showDrawingMenu(tile, tile->mapToGlobal(pos));
    });
    connect(tile, &QToolButton::clicked, this, openRecentDrawing);
    rows->addWidget(tile);
    ++index;
  }
  if (!index) {
    auto* empty = new QLabel("Your recent drawings will appear here.", m_RecentTiles);
    empty->setStyleSheet(QString("color:%1; padding:16px;").arg(dark ? "#9db5c5" : "#6b7884"));
    rows->addWidget(empty);
  }
  rows->addStretch(1);
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
  grid->setSpacing(3);
  grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  const auto dataDir = UserDataDirectory();
  const auto store = (dataDir.empty() ? std::filesystem::path(".") : dataDir) /
                     "gosurvey-recent-projects.json";
  const auto recentProjectStore = std::make_shared<const std::filesystem::path>(store);
  auto entries = recent::Load(store);
  if (auto* search = findChild<QLineEdit*>("recentProjectSearch")) {
    const auto query = search->text().trimmed();
    if (!query.isEmpty())
      entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return !QString::fromUtf8(entry.name.c_str()).contains(query, Qt::CaseInsensitive) &&
               !QString::fromUtf8(entry.path.c_str()).contains(query, Qt::CaseInsensitive);
      }), entries.end());
  }
  const size_t projectLimit = m_ShowAllProjects ? entries.size() : std::min<size_t>(5, entries.size());
  const int columns = 1;
  int index = 0;
  for (const auto& entry : entries) {
    if (static_cast<size_t>(index) >= projectLimit) break;
    const QString name = QString::fromUtf8(entry.name.c_str());
    const QString path = QString::fromUtf8(entry.path.c_str());
    const QString lastOpened = QDateTime::fromSecsSinceEpoch(entry.lastOpenedUnix).toString("d MMM yyyy");
    const QString projectFolder = QFileInfo(path).absolutePath();
    auto* tile = new QToolButton(m_ProjectTiles);
    tile->setObjectName("recentProjectTile");
    tile->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const QString folderLabel = tile->fontMetrics().elidedText(QDir::toNativeSeparators(projectFolder),
                                                               Qt::ElideMiddle, 270);
    tile->setText(name + "\n" + folderLabel + " · " + lastOpened);
    tile->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    tile->setIconSize(QSize(26, 26));
    tile->setMinimumHeight(46);
    tile->setMaximumHeight(48);
    tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
    tile->setStyleSheet(dark
        ? QtApplyAccent("QToolButton { text-align:left; padding:6px 8px; border:0; border-left:3px solid @quiet@; border-bottom:1px solid #3c444a; border-radius:0; background:transparent; } QToolButton:hover { border-left-color:@strong@; background:#30373c; }", true)
        : "QToolButton { text-align:left; padding:6px 8px; border:1px solid #c5c8cb; border-radius:6px; background:#ffffff; } QToolButton:hover { border-color:#92979c; background:#f0f1f2; }");
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

// The viewer is its own top-level window, not an owned child: an owned window is kept above its owner, which stopped
// the main window being focused (or alt-tabbed to) while a PDF was open. It closes with the main window; if the user
// declines to discard unsaved marks the close is refused and the viewer stays open on its own, which keeps their work.
void EditorWindow::ShowPdf() {
  auto* window = new PdfWindow(nullptr);
  window->SetEditor(this);
  connect(this, &QObject::destroyed, window, [window] { window->SetEditor(nullptr); window->close(); });
  window->show();
}

bool EditorWindow::CommitPdfCoordinate(const pdfview::CoordCandidate& c) {
  if (!CreatePdfCoordinate(c)) return false;
  FinishPdfCoordinates();
  return true;
}

// Accept all: every candidate is created first and the view is framed and redrawn once at the end, so a long list
// does not flash the drawing once per point. Stops at the first candidate that could not be created.
std::size_t EditorWindow::CommitPdfCoordinates(const std::vector<pdfview::CoordCandidate>& candidates) {
  std::size_t created = 0;
  for (const auto& candidate : candidates) {
    if (!CreatePdfCoordinate(candidate)) break;
    ++created;
  }
  if (created) FinishPdfCoordinates();
  return created;
}

void EditorWindow::FinishPdfCoordinates() {
  StartZoomExtentsCommand(m_State, m_Log);
  m_State.requestMainWindowFocus = true;
  Refresh();
  raise(); activateWindow();
}

bool EditorWindow::CreatePdfCoordinate(const pdfview::CoordCandidate& c) {
  if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) {
    QMessageBox::information(this, "Extract Coordinates", "Open a drawing before accepting extracted coordinates.");
    return false;
  }
  if (ActiveDrawingReadOnly()) {
    QMessageBox::information(this, "Read-only drawing", "Extracted coordinates cannot change a read-only drawing.");
    return false;
  }
  if (c.IsSurveyPoint()) {
    int id = c.pointNumber.value_or(1);
    if (!c.pointNumber.has_value()) while (std::any_of(m_State.surveyPoints.begin(), m_State.surveyPoints.end(),
        [id](const SurveyPoint& p) { return p.id == id; })) ++id;
    SurveyPoint point{};
    point.id = id; point.easting = c.easting; point.northing = c.northing;
    point.elevation = c.elevation.value_or(0.0); point.description = c.description; point.rawDescription = c.description;
    if (std::any_of(m_State.surveyPoints.begin(), m_State.surveyPoints.end(), [id](const SurveyPoint& p) { return p.id == id; })) {
      QMessageBox box(QMessageBox::Warning, "Point number already exists",
                      QString("Point %1 already exists. Choose how to resolve the conflict.").arg(id), QMessageBox::NoButton, this);
      auto* overwrite = box.addButton("Overwrite", QMessageBox::AcceptRole);
      auto* offset = box.addButton("Offset", QMessageBox::ActionRole);
      box.addButton(QMessageBox::Cancel);
      box.exec();
      if (box.clickedButton() != overwrite && box.clickedButton() != offset) return false;
      int offsetBy = 0;
      if (box.clickedButton() == offset) {
        int maxId = 0; for (const auto& p : m_State.surveyPoints) maxId = std::max(maxId, p.id);
        bool ok = false;
        offsetBy = QInputDialog::getInt(this, "Offset point number", "ID offset", std::max(maxId, 1), 0, 1000000, 1, &ok);
        if (!ok) return false;
      }
      PushUndoSnapshot(m_State, "Extract coordinates");
      ResolveConflictingWorldSurveyPoints(m_State, {point}, box.clickedButton() == overwrite, offsetBy, &m_Log);
    } else {
      PushUndoSnapshot(m_State, "Extract coordinates");
      point.easting -= m_State.worldDocumentOriginX; point.northing -= m_State.worldDocumentOriginY;
      point.layer = CreatePointsLayer(m_State);
      m_State.surveyPoints.push_back(point);
      EnsureSurveyPointLabelMtext(m_State, m_State.surveyPoints.size() - 1, &m_Log);
    }
    m_Log.push_back("Extract Coordinates — survey point " + std::to_string(id) + " created.");
  } else {
    const float radius = SurveyPointCrossHalfWorldFromPaper(m_State.surveyPointCrossSpanPlottedInches,
                                                              m_State.modelUnitsPerPlottedInch);
    CommitCircleExternal(m_State, static_cast<float>(c.easting - m_State.worldDocumentOriginX),
                         static_cast<float>(c.northing - m_State.worldDocumentOriginY),
                         static_cast<float>(c.elevation.value_or(0.0)), radius, 0.f, 0.f, 1.f, m_Log);
  }
  return true;
}
void EditorWindow::ShowAdjustment() {
  if (auto* existing = findChild<QMainWindow*>("adjustmentWindow")) { existing->show(); existing->raise(); return; }
  auto* window = new AdjustmentWindow(m_State, m_Log, this);
  window->drawingChanged = [this] { Refresh(); };
  window->setObjectName("adjustmentWindow");
  window->show();
}

bool EditorWindow::RunSmokeChecks(QString& error) {
  // The window opens on the Start tab, whose viewport is never shown, so its renderer cannot start.
  // Work in the first drawing, and give the renderer the event loop it needs to initialize.
  SwitchDrawing(1);
  QElapsedTimer renderWait; renderWait.start();
  while (!m_Viewport->IsRendererReady() && renderWait.elapsed() < 5000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (!m_Viewport->IsRendererReady()) { error = "OpenGL renderer failed to initialize"; return false; }
  Submit("LINE"); Submit("0,0"); Submit("10,0");
  CancelActiveCommand(m_State, m_Log);
  if (m_State.userLinesFlat.size() != 6) { error = "LINE did not create one segment"; return false; }
  Submit("UNDO");
  if (!m_State.userLinesFlat.empty()) { error = "UNDO did not remove segment"; return false; }
  Submit("REDO");
  if (m_State.userLinesFlat.size() != 6) { error = "REDO did not restore segment"; return false; }
  CreateDrawingFromTemplate();  // NewDrawing() opens the template chooser, a modal dialog
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
