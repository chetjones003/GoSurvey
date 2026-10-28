#include "EditorWindow.hpp"
#include "EditorViewport.hpp"
#include "RibbonCatalog.hpp"
#include "ModelingRibbon.hpp"
#include <QAbstractItemView>
#include <QLineEdit>
#include "CadBlocks.hpp"
#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "PdfAttach.hpp"
#include "PdfPlot.hpp"
#include "cadpiperun.hpp"
#include "HatchPat.hpp"
#include <QFile>
#include <QColorDialog>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <numeric>
#include <vector>

namespace {
QString Text(const char* text) { return QString::fromUtf8(text); }
bool NeedsNativeDialog(const QString& command) {
  static const QSet<QString> pending = {"@textStyles", "@dimStyles", "@leaderStyles",
      "@editElevations", "@pointGroups", "@volumes",
      "@connectionModes", "@blockPalettes", "@createBlock", "@batchPlot", "@pageSetup", "BEDIT", "INSERT"};
  return pending.contains(command);
}
int SelectedSurface(const AppCommandState& state) {
  for (const auto& item : state.selection)
    if (item.type == SelectedEntity::Type::Surface && item.index >= 0 && item.index < static_cast<int>(state.cadSurfaces.size())) return item.index;
  return -1;
}

// Expanded panels use three short rows. At smaller widths whole panels become native
// menus containing the very same actions, so resizing never clips or loses a command.
constexpr int kRibbonPanelHeight = 98;
constexpr int kRibbonPageHeight = 102;
constexpr int kRibbonHeight = 130;
constexpr int kRibbonCompactHeaderHeight = 26;
constexpr int kRibbonCaptionBottomSpace = 4;
class RibbonPage final : public QWidget {
  struct Panel { QWidget* expanded; QToolButton* collapsed; QMenu* menu; int priority; };
  std::vector<Panel> m_Panels;
  std::vector<int> m_PriorityOrder;
  std::vector<int> m_OverflowPanelIndices;
  QHBoxLayout* m_Row;
  QToolButton* m_More;
  QMenu* m_Overflow;
public:
  explicit RibbonPage(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(0); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); setFixedHeight(kRibbonPageHeight);
    m_Row = new QHBoxLayout(this); m_Row->setContentsMargins(3, 2, 3, 1); m_Row->setSpacing(2);
    m_More = new QToolButton(this); m_More->setObjectName("ribbonMore"); m_More->setText("More");
    m_More->setIcon(CadCommandIcon("layers.png")); m_More->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_More->setPopupMode(QToolButton::InstantPopup); m_More->setAutoRaise(true); m_More->setFixedSize(62, 90);
    m_Overflow = new QMenu(m_More); m_More->setMenu(m_Overflow); m_More->hide();
    m_Row->addWidget(m_More); m_Row->addStretch();
  }
  void AddPanel(QWidget* panel, const QString& title, const QList<QAction*>& actions, int priority) {
    panel->setParent(this); panel->setProperty("ribbonPanel", true);
    panel->setFixedSize(panel->sizeHint().width(), kRibbonPanelHeight);
    auto* collapsed = new QToolButton(this); collapsed->setObjectName("collapsed_" + panel->objectName());
    collapsed->setText(title); collapsed->setToolTip(title); collapsed->setAutoRaise(true);
    collapsed->setFixedSize(std::clamp(fontMetrics().horizontalAdvance(title) + 22, 66, 155), 92);
    collapsed->setIconSize(QSize(26, 26)); collapsed->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    // Make the full collapsed panel button open its menu; a split button leaves the
    // tiny, style-dependent arrow as the only reliable target at minimum width.
    collapsed->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(title, collapsed); menu->setToolTipsVisible(true); collapsed->setMenu(menu);
    if (!actions.empty()) { collapsed->setIcon(actions.front()->icon()); menu->setIcon(actions.front()->icon()); }
    for (auto* action : actions) menu->addAction(action);
    // Keep live selectors available when their panel collapses, without moving
    // widgets out of the ribbon's layout or duplicating their state handlers.
    for (auto* source : panel->findChildren<QComboBox*>()) {
      auto* widget = new QWidget(menu); auto* row = new QHBoxLayout(widget); row->setContentsMargins(8, 3, 8, 3);
      row->addWidget(new QLabel(source->accessibleName(), widget));
      auto* mirror = new QComboBox(widget); mirror->setMinimumWidth(150); row->addWidget(mirror);
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(widget); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] {
        const QSignalBlocker block(mirror); mirror->clear();
        for (int i = 0; i < source->count(); ++i) mirror->addItem(source->itemIcon(i), source->itemText(i));
        mirror->setCurrentIndex(source->currentIndex());
        mirror->setEnabled(source->isEnabled());
      });
      connect(mirror, &QComboBox::activated, source, [source](int index) {
        source->setCurrentIndex(index);
        QMetaObject::invokeMethod(source, "activated", Qt::DirectConnection, Q_ARG(int, index));
        QMetaObject::invokeMethod(source, "textActivated", Qt::DirectConnection, Q_ARG(QString, source->currentText()));
      });
    }
    for (auto* source : panel->findChildren<QDoubleSpinBox*>()) {
      auto* mirror = new QDoubleSpinBox(menu); mirror->setRange(source->minimum(), source->maximum());
      mirror->setDecimals(source->decimals()); mirror->setSingleStep(source->singleStep()); mirror->setPrefix(source->prefix()); mirror->setSuffix(source->suffix());
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { const QSignalBlocker block(mirror); mirror->setValue(source->value()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &QDoubleSpinBox::valueChanged, source, &QDoubleSpinBox::setValue);
    }
    for (auto* source : panel->findChildren<QSpinBox*>()) {
      auto* mirror = new QSpinBox(menu); mirror->setRange(source->minimum(), source->maximum());
      mirror->setSingleStep(source->singleStep()); mirror->setPrefix(source->prefix()); mirror->setSuffix(source->suffix());
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { const QSignalBlocker block(mirror); mirror->setValue(source->value()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &QSpinBox::valueChanged, source, &QSpinBox::setValue);
    }
    m_Row->insertWidget(m_Row->count() - 2, panel); m_Row->insertWidget(m_Row->count() - 2, collapsed);
    m_Panels.push_back({panel, collapsed, menu, priority});
    m_PriorityOrder.push_back(static_cast<int>(m_Panels.size() - 1));
    std::stable_sort(m_PriorityOrder.begin(), m_PriorityOrder.end(), [this](int a, int b) {
      return m_Panels[a].priority > m_Panels[b].priority;
    });
    Fit();
  }
  void Fit() {
    if (m_Panels.empty()) return;
    int used = 6;
    for (const auto& p : m_Panels) used += p.collapsed->width() + 2;
    std::vector<bool> shown(m_Panels.size(), true);
    if (used > width()) {
      std::fill(shown.begin(), shown.end(), false); used = 6 + m_More->width() + 2;
      for (const int i : m_PriorityOrder) if (used + m_Panels[i].collapsed->width() + 2 <= width()) {
        shown[i] = true; used += m_Panels[i].collapsed->width() + 2;
      }
    }
    std::vector<int> overflowPanels;
    for (int i = 0; i < static_cast<int>(m_Panels.size()); ++i) {
      if (!shown[i]) overflowPanels.push_back(i);
    }
    if (overflowPanels != m_OverflowPanelIndices) {
      m_Overflow->clear();
      for (const int i : overflowPanels) m_Overflow->addMenu(m_Panels[i].menu);
      m_OverflowPanelIndices = std::move(overflowPanels);
    }
    if (m_More->isHidden() == !m_OverflowPanelIndices.empty())
      m_More->setVisible(!m_OverflowPanelIndices.empty());
    std::vector<bool> expanded(m_Panels.size(), false);
    for (const int i : m_PriorityOrder) {
      if (!shown[i]) continue;
      auto& p = m_Panels[i]; const int extra = p.expanded->width() - p.collapsed->width();
      if (used + extra <= width()) { expanded[i] = true; used += extra; }
    }
    for (int i = 0; i < static_cast<int>(m_Panels.size()); ++i) {
      auto& p = m_Panels[i];
      if (p.expanded->isHidden() == expanded[i]) p.expanded->setVisible(expanded[i]);
      const bool collapsedVisible = shown[i] && !expanded[i];
      if (p.collapsed->isHidden() == collapsedVisible) p.collapsed->setVisible(collapsedVisible);
    }
  }
protected:
  void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); Fit(); }
  void showEvent(QShowEvent* event) override { QWidget::showEvent(event); Fit(); }
};

void UpdateRibbonTabButton(QWidget* wrapper) {
  auto* button = wrapper->findChild<QToolButton*>("ribbonTabMenu");
  Q_ASSERT(button);
  if (!button) return;
  button->setText(QString::fromUtf8("☰  Tabs"));
}

void UpdateRibbonTabMenu(QWidget* wrapper) {
  UpdateRibbonTabButton(wrapper);
  auto* ribbon = wrapper->findChild<QTabWidget*>("ribbon");
  auto* button = wrapper->findChild<QToolButton*>("ribbonTabMenu");
  if (!ribbon || !button) return;
  const int current = ribbon->currentIndex();
  auto* menu = button->menu();
  if (!menu) return;
  QString signature;
  for (int i = 0; i < ribbon->count(); ++i) {
    if (!ribbon->isTabVisible(i)) continue;
    signature += QString::number(i) + QLatin1Char(':') + ribbon->tabText(i) + QLatin1Char(';');
  }
  if (menu->property("ribbonTabSignature").toString() == signature) return;
  menu->clear();
  for (int i = 0; i < ribbon->count(); ++i) {
    if (!ribbon->isTabVisible(i)) continue;
    auto* action = menu->addAction(ribbon->tabText(i));
    action->setCheckable(true);
    action->setChecked(i == current);
    action->setProperty("ribbonIndex", i);
    action->setObjectName("ribbonTabMenuAction_" + ribbon->widget(i)->property("ribbonTab").toString());
    QObject::connect(action, &QAction::triggered, ribbon, [ribbon, i] { ribbon->setCurrentIndex(i); });
  }
  menu->setProperty("ribbonTabSignature", signature);
}

void FitRibbonChrome(QWidget* wrapper) {
  auto* ribbonColumn = wrapper->findChild<QWidget*>("ribbonColumn");
  auto* ribbon = wrapper->findChild<QTabWidget*>("ribbon");
  auto* shared = wrapper->findChild<QWidget*>("ribbonSharedArea");
  auto* header = wrapper->findChild<QWidget*>("ribbonTabMenuHeader");
  auto* tabMenu = wrapper->findChild<QToolButton*>("ribbonTabMenu");
  auto* compactLayers = wrapper->findChild<QToolButton*>("ribbonCompactLayers");
  Q_ASSERT(ribbonColumn && ribbon && shared && header && tabMenu && compactLayers);
  if (!ribbonColumn || !ribbon || !shared || !header || !tabMenu || !compactLayers) return;

  auto* tabs = ribbon->tabBar();
  int needed = 0;
  int visibleCount = 0;
  for (int i = 0; i < ribbon->count(); ++i) if (tabs->isTabVisible(i)) {
    needed += tabs->tabRect(i).width();
    ++visibleCount;
  }
  if (visibleCount > 1) needed += (visibleCount - 1) * tabs->style()->pixelMetric(QStyle::PM_TabBarTabHSpace);
  if (needed <= 0) needed = tabs->sizeHint().width();

  const int sharedWidth = shared->sizeHint().width();
  const int gap = 2;
  const bool sharedFits = wrapper->width() >= needed + sharedWidth + gap;
  const bool compact = !sharedFits;
  if (shared->isHidden() == sharedFits) shared->setVisible(sharedFits);
  if (header->isHidden() == compact) header->setVisible(compact);
  if (compactLayers->isHidden() == compact) compactLayers->setVisible(compact);
  if (tabs->isHidden() == !compact) tabs->setVisible(!compact);
  if (wrapper->property("ribbonChromeCompact").toBool() != compact) {
    ribbon->setFixedHeight(compact ? kRibbonHeight - kRibbonCompactHeaderHeight : kRibbonHeight);
    wrapper->setProperty("ribbonChromeCompact", compact);
  }
  UpdateRibbonTabMenu(wrapper);
}

class RibbonResizeFilter final : public QObject {
  QWidget* m_Wrapper;
  bool m_Queued = false;
public:
  RibbonResizeFilter(QWidget* wrapper) : QObject(wrapper), m_Wrapper(wrapper) {}
  void Schedule() {
    if (m_Queued) return;
    m_Queued = true;
    QTimer::singleShot(0, this, [this] { m_Queued = false; FitRibbonChrome(m_Wrapper); });
  }
protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == m_Wrapper && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) Schedule();
    return false;
  }
};
}

void EditorWindow::BuildRibbonPages() {
  auto* toolbar = addToolBar("Ribbon"); toolbar->setObjectName("ribbonBar");
  toolbar->setMovable(false); toolbar->setFloatable(false); toolbar->setContentsMargins(0, 0, 0, 0);
  auto* wrapper = new QWidget(toolbar); wrapper->setObjectName("ribbonWrapper"); wrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* strip = new QHBoxLayout(wrapper); strip->setContentsMargins(0, 0, 0, 0); strip->setSpacing(2);
  auto* ribbonColumn = new QWidget(wrapper); ribbonColumn->setObjectName("ribbonColumn"); ribbonColumn->setMinimumWidth(0);
  auto* ribbonLayout = new QVBoxLayout(ribbonColumn); ribbonLayout->setContentsMargins(0, 0, 0, 0); ribbonLayout->setSpacing(0);
  auto* header = new QWidget(ribbonColumn); header->setObjectName("ribbonTabMenuHeader"); header->setFixedHeight(kRibbonCompactHeaderHeight);
  auto* headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(4, 0, 4, 0); headerLayout->setSpacing(0);
  auto* tabMenu = new QToolButton(header); tabMenu->setObjectName("ribbonTabMenu"); tabMenu->setText(QString::fromUtf8("☰  Tabs"));
  tabMenu->setAccessibleName("Ribbon tabs"); tabMenu->setToolTip("Choose a ribbon tab"); tabMenu->setPopupMode(QToolButton::InstantPopup); tabMenu->setAutoRaise(false); tabMenu->setFixedHeight(24);
  auto* tabMenuItems = new QMenu(tabMenu); tabMenu->setMenu(tabMenuItems);
  headerLayout->addWidget(tabMenu); headerLayout->addStretch(); header->hide(); ribbonLayout->addWidget(header);
  auto* ribbon = new QTabWidget(ribbonColumn); ribbon->setObjectName("ribbon"); ribbonLayout->addWidget(ribbon, 1);
  ribbon->tabBar()->setMouseTracking(true);
  ribbon->tabBar()->setAttribute(Qt::WA_Hover, true);
  strip->addWidget(ribbonColumn, 1);
  auto* shared = new QWidget(wrapper); shared->setObjectName("ribbonSharedArea"); shared->setFixedWidth(228);
  auto* sharedColumn = new QVBoxLayout(shared); sharedColumn->setContentsMargins(0, 26, 0, 0); sharedColumn->setSpacing(0);
  auto* layerPage = new RibbonPage(shared); layerPage->setObjectName("ribbonSharedLayers"); sharedColumn->addWidget(layerPage);
  strip->addWidget(shared);
  ribbon->setUsesScrollButtons(false); ribbon->setElideMode(Qt::ElideNone);
  ribbon->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); ribbon->setMinimumWidth(0);
  ribbon->setFixedHeight(kRibbonHeight);
  const QStringList names = {"Home", "Insert", "Annotate", "View", "Manage", "Output", "Survey", "Modeling",
      "Layout", "SurfaceCtx", "SurveyPointCtx", "PointCloudCtx", "BlockEditor", "GeolocationCtx", "PdfCtx", "HatchCtx"};
  for (const auto& name : names) {
    auto* page = new RibbonPage(ribbon); page->setObjectName("ribbonPage_" + name); page->setProperty("ribbonTab", name);
    QStringList panels;
    for (const auto& item : qtribbon::kButtons) if (Text(item.tab) == name && !panels.contains(Text(item.panel))) panels.append(Text(item.panel));
    if (name == "Manage") {
      auto* layout = qobject_cast<QHBoxLayout*>(page->layout());
      auto* note = new QLabel("Data shortcuts, action recorder, CAD standards and Dynamo are not available in GoSurvey 0.7.2.", page);
      note->setWordWrap(true); note->setMaximumWidth(600); layout->insertWidget(0, note);
    }
    if (name == "PointCloudCtx") panels.prepend("Point Cloud Display");
    if (name == "PointCloudCtx") panels.append("Centerline Fit");
    if (name == "HatchCtx") panels.append("Hatch");
    if (name == "Modeling") panels = {"Primitives", "Create", "Booleans", "Edit", "3D Modify", "Piping"};
    for (const auto& panelName : panels) {
      auto* panel = new QWidget(page); panel->setObjectName("ribbonPanel_" + name + "_" + panelName);
      auto* column = new QVBoxLayout(panel); column->setContentsMargins(5, 0, 5, kRibbonCaptionBottomSpace); column->setSpacing(1);
      auto* grid = new QGridLayout; grid->setContentsMargins(0, 0, 0, 0); grid->setHorizontalSpacing(2); grid->setVerticalSpacing(0);
      column->addLayout(grid, 1); int col = 0, row = 0; QList<QAction*> actions;
      for (const auto& item : qtribbon::kButtons) {
        if (Text(item.tab) != name || Text(item.panel) != panelName) continue;
        auto* action = new QAction(CadCommandIcon(Text(item.icon) + ".png"), Text(item.label), this);
        action->setObjectName("ribbon_" + Text(item.id).mid(2)); action->setProperty("ribbonId", Text(item.id));
        action->setProperty("ribbonCommand", Text(item.command)); action->setProperty("cadIcon", Text(item.icon) + ".png");
        action->setProperty("releasePlaceholder", item.placeholder);
        action->setProperty("nativeDialogPending", NeedsNativeDialog(Text(item.command)));
        const QString help = Text(item.tooltip) + (NeedsNativeDialog(Text(item.command)) ? "\nThis command's dialog is not available in this editor yet." : "");
        action->setToolTip(help); action->setStatusTip(help); action->setEnabled(!item.placeholder && !NeedsNativeDialog(Text(item.command)));
        connect(action, &QAction::triggered, this, [this, id = Text(item.id)] { RunRibbonAction(id); });
        actions.append(action);
        auto* button = new QToolButton(panel); button->setObjectName("tool_" + Text(item.id).mid(2)); button->setDefaultAction(action);
        button->setAutoRaise(true); button->setAttribute(Qt::WA_AlwaysShowToolTips); button->setAccessibleName(Text(item.label).replace('\n', ' '));
        const bool iconOnly = name == "Home" && !item.large && (panelName == "Draw" || panelName == "Modify" || panelName == "Palettes");
        button->setToolButtonStyle(item.large ? Qt::ToolButtonTextUnderIcon : iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        button->setIconSize(item.large ? QSize(30, 30) : QSize(18, 18));
        if (item.large) {
          if (row) { ++col; row = 0; }
          button->setMinimumWidth(58); button->setFixedHeight(73); grid->addWidget(button, 0, col++, 3, 1);
        } else {
          if (iconOnly) button->setFixedWidth(30);
          button->setFixedHeight(24); grid->addWidget(button, row++, col);
          if (row == 3) { row = 0; ++col; }
        }
        // Release flyouts share command actions with the expanded panel and collapsed menu.
        if (Text(item.command).endsWith("Menu") || Text(item.id) == "##ModPipeFit" || Text(item.id) == "##GeoEditLocation" || Text(item.id) == "##GeoMarkPosition") {
          auto* menu = new QMenu(button); menu->setToolTipsVisible(true);
          auto add = [&](const QString& label, const QString& command, const QString& icon = {}) {
            auto* option = menu->addAction(icon.isEmpty() ? action->icon() : CadCommandIcon(icon + ".png"), label);
            option->setProperty("ribbonCommand", command); option->setToolTip(command);
            option->setProperty("cadIcon", icon.isEmpty() ? action->property("cadIcon") : QVariant(icon + ".png"));
            option->setEnabled(!NeedsNativeDialog(command));
            if (NeedsNativeDialog(command)) option->setToolTip("This command's dialog is not available in this editor yet.");
            connect(option, &QAction::triggered, this, [this, command] { RunRibbonAction(command); });
          };
          const auto id = Text(item.id);
          if (id.endsWith("Isolate")) { add("Isolate Objects", "ISOLATEOBJECTS"); add("Hide Objects", "HIDEOBJECTS"); add("End Object Isolation", "UNISOLATEOBJECTS"); }
          else if (id == "##ModPipeFit") for (const auto* part : modelingribbon::kPipeFitPartTypes) add(Text(part), "PIPEFIT " + Text(part));
          else if (id == "##GeoMap") for (const auto& map : kOnlineMaps) add(Text(map.label), "@map:" + QString::number(static_cast<int>(map.map)), Text(map.icon));
          else if (id == "##GeoEditLocation") { add("Edit Location", "@drawingSettings"); add("Edit Geographic Marker", "@geoReorient"); }
          else if (id == "##GeoMarkPosition") { add("Lat-Long", "GEOMARKLATLONG"); add("Point", "GEOMARKPOINT"); }
          else if (id == "##GeoCaptureArea") { add("Capture Area", "@captureViewport"); add("Pick Area", "@captureArea"); menu->addSeparator(); add("Remove Captured Areas", "@removeCaptures"); }
          else if (id == "##TsAddData") {
            add("Breaklines", "@surfaceBreaklines"); add("Contours", "@surfaceContours"); add("Boundary", "@surfaceBoundary"); add("Point Groups", "@surfaceManager");
            auto* missing = menu->addAction("Point Files"); missing->setEnabled(false); missing->setToolTip("Not implemented in GoSurvey 0.7.2.");
          } else if (id == "##TsEditSurf") {
            add("Add Point", "@surfaceAddPoint"); add("Delete Point", "@surfaceDeletePoint"); add("Move Point", "@surfaceMovePoint");
            add("Delete TIN Line", "@surfaceDeleteLine"); add("Swap Edge", "@surfaceSwapEdge"); add("Rebuild", "@surfaceRebuild");
          } else if (id == "##TsExtract") { add("Contours", "@extractContours"); add("Water Drop Path", "WATERDROP EXTRACT"); add("Water Drop Feature Line", "WATERDROP EXTRACT FL"); add("Catchment", "CATCHMENT EXTRACT"); }
          action->setMenu(menu); button->setMenu(menu); button->setPopupMode(id == "##GeoEditLocation" ? QToolButton::MenuButtonPopup : QToolButton::InstantPopup);
        }
      }
      if (row) ++col;
      auto* caption = new QLabel(panelName, panel); caption->setObjectName("ribbonPanelLabel"); caption->setAlignment(Qt::AlignCenter);
      column->addWidget(caption);
      // Live selectors are native Qt controls, kept next to their release commands.
      auto combo = [&](const QString& object, const QStringList& values) {
        auto* result = new QComboBox(panel); result->setObjectName(object); result->addItems(values); result->setMinimumWidth(135); result->setMaximumWidth(170);
        result->setAccessibleName(object.mid(6)); result->setToolTip(object.mid(6));
        grid->addWidget(result, 0, col++, 1, 1); return result;
      };
      if (name == "Home" && panelName == "Layers") {
        auto* layer = combo("ribbonLayer", {}); layer->setEditable(true);
        connect(layer, &QComboBox::textActivated, this, [this](const QString& value) { CadRibbonPickLayer(m_State, value.toUtf8().toStdString(), m_Log); Refresh(); });
        auto* color = combo("ribbonColor", {"ByLayer", "ByBlock", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"});
        color->setEditable(true); color->lineEdit()->setReadOnly(true);
        for (int i = 2; i < color->count(); ++i) { QPixmap swatch(14, 14); swatch.fill(QColor(color->itemText(i))); color->setItemIcon(i, QIcon(swatch)); }
        grid->removeWidget(color); grid->addWidget(color, 1, col - 2); --col;
        connect(color, &QComboBox::textActivated, this, [this](const QString& value) { CadRibbonPickColor(m_State, value.toUtf8().toStdString(), m_Log); Refresh(); });
      }
      if (name == "View" && panelName == "View") {
        auto* style = combo("ribbonVisualStyle", {"2D Wireframe", "Hidden", "Shaded"});
        connect(style, &QComboBox::activated, this, [this](int index) { Submit(QString("VISUALSTYLE %1").arg(QStringList{"2D", "HIDDEN", "SHADED"}[index])); });
        auto* projection = combo("ribbonProjection", {"Orthographic", "Perspective"});
        connect(projection, &QComboBox::activated, this, [this](int index) { Submit(index ? "PERSPECTIVE ON" : "PERSPECTIVE OFF"); });
        auto* fov = new QSpinBox(panel); fov->setObjectName("ribbonFov"); fov->setRange(1, 179); fov->setPrefix("FOV ");
        fov->setSuffix(" deg"); grid->addWidget(fov, 1, col - 1);
        connect(fov, &QSpinBox::valueChanged, this, [this](int value) { Submit(QString("FOV %1").arg(value)); });
      }
      if (name == "View" && panelName == "Named Views") {
        auto* views = combo("ribbonNamedView", {"Top", "Bottom", "Left", "Right", "Front", "Back", "SW Isometric", "SE Isometric", "NE Isometric", "NW Isometric"});
        views->setEditable(true); views->lineEdit()->setReadOnly(true);
        connect(views, &QComboBox::activated, this, [this](int index) {
          static const float az[] = {0, 0, 270, 90, 0, 180, 45, 315, 135, 225};
          static const float el[] = {90, -90, 0, 0, 0, 0, 35.264f, 35.264f, 35.264f, 35.264f};
          if (index < 10) m_Viewport->SetOrientation(az[index], el[index]);
          else if (index - 10 < static_cast<int>(m_State.namedViews.size())) RestoreNamedView(m_State, m_State.namedViews[index - 10], m_Log);
          Refresh();
        });
      }
      if (name == "Annotate" && panelName == "Text") {
        auto* styles = combo("ribbonTextStyle", {});
        connect(styles, &QComboBox::textActivated, this, [this](const QString& value) { SetActiveTextStyle(m_State, value.toUtf8().toStdString()); Refresh(); });
      }
      if (name == "View" && panelName == "Coordinates") {
        QStringList frames{"WCS"}; for (const auto& preset : ucs::OrthographicPresets()) frames.append(Text(preset.name));
        auto* frame = combo("ribbonUcs", frames);
        connect(frame, &QComboBox::activated, this, [this](int index) {
          if (index == 0) SetActiveUcs(m_State, ucs::Ucs{}, m_Log);
          else if (index <= 6) SetActiveUcs(m_State, ucs::OrthographicPresets()[index - 1].frame, m_Log);
          else if (index - 7 < static_cast<int>(m_State.ucsNamed.size())) SetActiveUcs(m_State, m_State.ucsNamed[index - 7].frame, m_Log);
          Refresh();
        });
      }
      if (name == "Modeling" && panelName == "Piping") {
        QStringList sizes; for (const auto& entry : kCadPipeNpsTable) sizes.append(QString::number(entry.nps) + "in");
        auto* size = combo("ribbonPipeSize", sizes);
        connect(size, &QComboBox::textActivated, this, [this](const QString& value) { ChoosePipeRunNominalSize(m_State, value.toUtf8().toStdString()); Refresh(); });
      }
      if (name == "PointCloudCtx" && panelName == "Centerline Fit") {
        auto* radius = new QDoubleSpinBox(panel); radius->setObjectName("ribbonCenterlineRadius"); radius->setRange(0.05, 20); radius->setSingleStep(0.1); radius->setPrefix("Radius "); radius->setSuffix(" ft"); grid->addWidget(radius, 0, col);
        connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_State.extractCenterlineSearchRadiusFt = value; m_Viewport->update(); });
        auto* tolerance = new QSpinBox(panel); tolerance->setObjectName("ribbonCenterlineTolerance"); tolerance->setRange(1, 90); tolerance->setPrefix("Tolerance "); tolerance->setSuffix(" %"); grid->addWidget(tolerance, 1, col++);
        connect(tolerance, &QSpinBox::valueChanged, this, [this](int value) { m_State.extractCenterlineMaxResidualRatio = value / 100.0; m_Viewport->update(); });
      }
      if (name == "HatchCtx") {
        QStringList patterns{"SOLID"}; QFile file(QtAsset("hatches/acadiso.pat"));
        if (file.open(QIODevice::ReadOnly)) {
          std::vector<hatchpat::Def> definitions; hatchpat::Parse(file.readAll().toStdString(), &definitions);
          for (const auto& definition : definitions) if (!patterns.contains(Text(definition.name.c_str()))) patterns.append(Text(definition.name.c_str()));
        }
        auto* pattern = combo("ribbonHatchPattern", patterns); pattern->setAccessibleName("Hatch pattern");
        connect(pattern, &QComboBox::textActivated, this, [this](const QString& value) { EditRibbonHatch("pattern", value); });
        auto* layer = combo("ribbonHatchLayer", {}); layer->setAccessibleName("Hatch layer");
        connect(layer, &QComboBox::textActivated, this, [this](const QString& value) { EditRibbonHatch("layer", value); });
        auto* angle = new QDoubleSpinBox(panel); angle->setObjectName("ribbonHatchAngle"); angle->setRange(-360, 360); angle->setPrefix("Angle "); angle->setSuffix(" deg"); angle->setKeyboardTracking(false); grid->addWidget(angle, 1, col - 2);
        connect(angle, &QDoubleSpinBox::valueChanged, this, [this](double value) { EditRibbonHatch("angle", QString::number(value, 'g', 15)); });
        auto* scale = new QDoubleSpinBox(panel); scale->setObjectName("ribbonHatchScale"); scale->setDecimals(4); scale->setRange(0.0001, 1e9); scale->setPrefix("Scale "); scale->setKeyboardTracking(false); grid->addWidget(scale, 1, col - 1);
        connect(scale, &QDoubleSpinBox::valueChanged, this, [this](double value) { EditRibbonHatch("scale", QString::number(value, 'g', 15)); });
        auto* transparency = new QSpinBox(panel); transparency->setObjectName("ribbonHatchTransparency"); transparency->setRange(0, 100); transparency->setPrefix("Transparency "); transparency->setSuffix(" %"); grid->addWidget(transparency, 2, col - 1);
        connect(transparency, &QSpinBox::valueChanged, this, [this](int value) { EditRibbonHatch("transparency", QString::number(value / 100.0)); });
        auto* color = new QToolButton(panel); color->setText("Color..."); color->setIcon(CadCommandIcon("Color_Faces.png")); color->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); color->setAutoRaise(true); grid->addWidget(color, 2, col - 2);
        connect(color, &QToolButton::clicked, this, [this] {
          const auto selected = QColorDialog::getColor(QColor::fromRgbF(m_State.hatchColorRgb[0], m_State.hatchColorRgb[1], m_State.hatchColorRgb[2]), this, "Hatch color");
          if (selected.isValid()) EditRibbonHatch("color", selected.name());
        });
      }
      if (name == "PointCloudCtx" && panelName == "Point Cloud Display") {
        auto* size = new QDoubleSpinBox(panel); size->setObjectName("ribbonPointSize"); size->setRange(1, 10); size->setSingleStep(0.5); size->setPrefix("Size "); grid->addWidget(size, 0, col);
        connect(size, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_State.pointCloudDisplay.pointSizePx = static_cast<float>(value); m_Viewport->update(); });
        auto* lod = new QSpinBox(panel); lod->setObjectName("ribbonPointLod"); lod->setRange(100000, 3000000); lod->setSingleStep(100000); lod->setPrefix("LOD "); grid->addWidget(lod, 1, col++);
        connect(lod, &QSpinBox::valueChanged, this, [this](int value) { m_State.pointCloudDisplay.lodTargetPoints = value; m_Viewport->update(); });
        auto* scheme = combo("ribbonPointColor", {"RGB", "Solid", "Elevation", "Intensity"});
        connect(scheme, &QComboBox::activated, this, [this](int index) { m_State.pointCloudDisplay.colorScheme = static_cast<PointCloudColorScheme>(index); m_Viewport->update(); });
      }
      const bool important = panelName == "Draw" || panelName == "Modify" || panelName == "Clipboard" || panelName == "Layers";
      const int priority = important ? 10 : std::any_of(actions.begin(), actions.end(), [](auto* a) { return a->isEnabled(); }) ? 5 : 0;
      if (name == "Home" && panelName == "Layers") layerPage->AddPanel(panel, panelName, actions, priority);
      else page->AddPanel(panel, panelName, actions, priority);
    }
    ribbon->addTab(page, name);
  }
  auto* layerButton = layerPage->findChild<QToolButton*>("collapsed_ribbonPanel_Home_Layers");
  if (!layerButton) {
    qFatal("Ribbon Layers panel was not constructed");
    return;
  }
  QMenu* layerMenu = layerButton->menu();
  if (!layerMenu) {
    qFatal("Ribbon Layers menu was not constructed");
    return;
  }
  auto* compactLayers = new QToolButton(header); compactLayers->setObjectName("ribbonCompactLayers");
  compactLayers->setText("Layers"); compactLayers->setAccessibleName("Layer and color controls");
  compactLayers->setPopupMode(QToolButton::InstantPopup); compactLayers->setMenu(layerMenu);
  headerLayout->insertWidget(1, compactLayers);
  QObject::connect(ribbon, &QTabWidget::currentChanged, tabMenu, [wrapper] {
    UpdateRibbonTabButton(wrapper);
    auto* menu = wrapper->findChild<QToolButton*>("ribbonTabMenu")->menu();
    const int current = wrapper->findChild<QTabWidget*>("ribbon")->currentIndex();
    for (auto* action : menu->actions()) action->setChecked(action->property("ribbonIndex").toInt() == current);
  });
  QObject::connect(tabMenuItems, &QMenu::aboutToShow, wrapper, [wrapper] { UpdateRibbonTabMenu(wrapper); });
  auto* resizeFilter = new RibbonResizeFilter(wrapper); wrapper->installEventFilter(resizeFilter); resizeFilter->Schedule();
  toolbar->addWidget(wrapper);
}

void EditorWindow::RefreshRibbon() {
  auto* ribbon = findChild<QTabWidget*>("ribbon"); if (!ribbon) return;
  const bool drawing = m_State.activeDrawingIdx > 0;
  if (auto* layers = findChild<QWidget*>("ribbonSharedLayers")) layers->setEnabled(drawing);
  const bool paper = m_State.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(m_State);
  const int surface = paper ? -1 : SelectedSurface(m_State);
  int point = -1, cloud = -1, pdf = -1;
  int hatch = -1;
  if (!paper) for (const int i : m_State.selectedSurveyPointIndices)
    if (i >= 0 && i < static_cast<int>(m_State.surveyPoints.size())) { point = i; break; }
  for (const auto& item : m_State.selection) {
    if (!paper && item.type == SelectedEntity::Type::PointCloud && item.index >= 0 && item.index < static_cast<int>(m_State.cadPointClouds.size())) cloud = item.index;
    if (item.type == SelectedEntity::Type::PdfUnderlay && item.index >= 0 && item.index < static_cast<int>(m_State.pdfAttachments.size())) pdf = item.index;
    if (item.type == SelectedEntity::Type::FilledRegion && item.index >= 0 && item.index < static_cast<int>(m_State.cadFilledRegions.size())) hatch = item.index;
  }
  const bool hatchActive = hatch >= 0 || m_State.active == AppCommandState::Kind::Hatch;
  const QString desired = !m_State.blockEditorName.empty() ? "BlockEditor" : surface >= 0 ? "SurfaceCtx" : point >= 0 ? "SurveyPointCtx" : cloud >= 0 ? "PointCloudCtx" : hatchActive ? "HatchCtx" : QString{};
  const QString previous = ribbon->property("selectedContext").toString();
  const QString active = ribbon->currentWidget() ? ribbon->currentWidget()->property("ribbonTab").toString() : "Home";
  if (desired != previous && !desired.isEmpty() && previous.isEmpty()) ribbon->setProperty("beforeContext", active);
  int desiredIndex = -1;
  for (int i = 0; i < ribbon->count(); ++i) {
    auto* page = ribbon->widget(i); const QString name = page->property("ribbonTab").toString();
    const bool visible = name == "Layout" ? paper : name == "SurfaceCtx" ? surface >= 0 : name == "SurveyPointCtx" ? point >= 0 :
        name == "PointCloudCtx" ? cloud >= 0 : name == "BlockEditor" ? !m_State.blockEditorName.empty() :
        name == "GeolocationCtx" ? GeolocationRibbonTabVisible(m_State) : name == "PdfCtx" ? pdf >= 0 : name == "HatchCtx" ? hatchActive : true;
    ribbon->setTabVisible(i, visible); page->setEnabled(drawing);
    if (name == desired) desiredIndex = i;
    QString label = name;
    if (name == "SurfaceCtx") label = surface >= 0 ? "TIN Surface: " + Text(m_State.cadSurfaces[surface].name.c_str()) : "TIN Surface";
    else if (name == "SurveyPointCtx") label = point >= 0 ? QString("Survey Point: %1").arg(m_State.surveyPoints[point].id) : "Survey Point";
    else if (name == "PointCloudCtx") label = "Point Cloud";
    else if (name == "BlockEditor") label = "Block Editor";
    else if (name == "GeolocationCtx") label = "Geolocation";
    else if (name == "PdfCtx") label = "PDF Underlay";
    else if (name == "HatchCtx") label = "Hatch";
    ribbon->setTabText(i, label);
  }
  if (desired != previous) {
    if (desiredIndex >= 0) ribbon->setCurrentIndex(desiredIndex);
    else if (!previous.isEmpty()) {
      const QString restore = ribbon->property("beforeContext").toString();
      for (int i = 0; i < ribbon->count(); ++i) if (ribbon->widget(i)->property("ribbonTab").toString() == restore && ribbon->isTabVisible(i)) { ribbon->setCurrentIndex(i); break; }
    }
    ribbon->setProperty("selectedContext", desired);
  }
  for (auto* action : findChildren<QAction*>()) {
    const auto route = action->property("ribbonCommand").toString();
    if (route == "@captureArea" || route == "@captureViewport") action->setEnabled(m_State.drawingSettings.onlineMap != DrawingSettings::OnlineMap::Off);
    if (route == "@removeCaptures") action->setEnabled(!m_State.drawingSettings.capturedAreas.empty());
    if (route == "UNISOLATEOBJECTS") action->setEnabled(!m_State.hiddenEntityIds.empty());
    const auto id = action->property("ribbonId").toString(); if (id.isEmpty()) continue;
    bool enabled = drawing && !action->property("releasePlaceholder").toBool() && !action->property("nativeDialogPending").toBool();
    if (id == "##RibbonPasteHome") enabled = enabled && !m_State.clipboard.empty();
    if (id == "##RibbonCopyClipHome") enabled = enabled && (!m_State.selection.empty() || !m_State.selectedPaperEntities.empty());
    action->setEnabled(enabled);
    if (id == "##GeoMap") {
      const auto& map = OnlineMapInfoOf(m_State.drawingSettings.onlineMap);
      action->setText(Text(map.label)); action->setProperty("cadIcon", Text(map.icon) + ".png"); action->setIcon(CadCommandIcon(Text(map.icon) + ".png"));
    }
  }
  if (auto* wrapper = findChild<QWidget*>("ribbonWrapper")) FitRibbonChrome(wrapper);
  auto syncCombo = [&](const char* name, const QStringList& values, const QString& selected) {
    if (auto* combo = findChild<QComboBox*>(name)) {
      if (combo->hasFocus() || combo->view()->isVisible()) return;
      const QSignalBlocker block(combo); combo->clear(); combo->addItems(values); combo->setCurrentText(selected);
    }
  };
  QStringList layers; for (const auto& layer : m_State.drawingLayerTable) layers.append(Text(layer.name.c_str()));
  const auto selectedLayer = CadSelectionLayer(m_State);
  syncCombo("ribbonLayer", layers, Text((selectedLayer.empty() ? m_State.currentLayer : selectedLayer).c_str()));
  const auto* region = hatch >= 0 ? &m_State.cadFilledRegions[hatch] : nullptr;
  const auto* attrs = hatch >= 0 && hatch < static_cast<int>(m_State.cadFilledRegionAttrs.size()) ? &m_State.cadFilledRegionAttrs[hatch] : nullptr;
  syncCombo("ribbonHatchLayer", layers, Text((attrs ? attrs->layer : m_State.hatchLayer.empty() ? m_State.currentLayer : m_State.hatchLayer).c_str()));
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonHatchPattern")) { const QSignalBlocker block(combo); const auto& pattern = region ? region->patternName : m_State.hatchPatternName; combo->setCurrentText(pattern.empty() ? "SOLID" : Text(pattern.c_str())); }
  if (auto* angle = ribbon->findChild<QDoubleSpinBox*>("ribbonHatchAngle")) { const QSignalBlocker block(angle); angle->setValue(region ? region->patternAngleDeg : m_State.hatchAngleDeg); }
  if (auto* scale = ribbon->findChild<QDoubleSpinBox*>("ribbonHatchScale")) { const QSignalBlocker block(scale); scale->setValue(region ? region->patternScale : m_State.hatchScale); }
  if (auto* transparency = ribbon->findChild<QSpinBox*>("ribbonHatchTransparency")) { const QSignalBlocker block(transparency); transparency->setValue(qRound((attrs ? std::max(0.f, attrs->transparency) : m_State.hatchTransparency01) * 100)); }
  if (auto* combo = findChild<QComboBox*>("ribbonColor")) { const QSignalBlocker block(combo); const auto selected = CadSelectionColor(m_State); combo->setCurrentText(Text((selected.empty() ? m_State.currentColor : selected).c_str())); }
  QStringList styles; for (const auto& style : m_State.textStyles) styles.append(Text(style.name.c_str()));
  syncCombo("ribbonTextStyle", styles, Text(m_State.activeTextStyleName.c_str()));
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonVisualStyle")) { const QSignalBlocker block(combo); auto* vp = CurrentViewport(m_State); combo->setCurrentIndex(static_cast<int>(vp ? vp->visualStyle : m_State.viewportVisualStyle)); }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonProjection")) { const QSignalBlocker block(combo); combo->setCurrentIndex(m_State.viewportProjection == Camera::Projection::Perspective ? 1 : 0); }
  if (auto* fov = ribbon->findChild<QSpinBox*>("ribbonFov")) { const QSignalBlocker block(fov); fov->setValue(qRound(m_State.viewportFovDeg)); fov->setEnabled(m_State.viewportProjection == Camera::Projection::Perspective); }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonPipeSize")) {
    const QSignalBlocker block(combo); combo->setCurrentText(Text(m_State.pipeRunNominalSize.c_str()));
    combo->setEnabled(m_State.active != AppCommandState::Kind::PipeRun || m_State.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonUcs")) {
    const QSignalBlocker block(combo); while (combo->count() > 7) combo->removeItem(7);
    for (const auto& frame : m_State.ucsNamed) combo->addItem(Text(frame.name.c_str()));
    int selected = ucs::FramesMatch(m_State.activeUcs, ucs::Ucs{}) ? 0 : -1;
    if (selected < 0) for (int i = 0; i < 6; ++i) if (ucs::FramesMatch(m_State.activeUcs, ucs::OrthographicPresets()[i].frame)) { selected = i + 1; break; }
    if (selected < 0) for (int i = 0; i < static_cast<int>(m_State.ucsNamed.size()); ++i) if (ucs::FramesMatch(m_State.activeUcs, m_State.ucsNamed[i].frame)) { selected = i + 7; break; }
    if (selected < 0) { combo->addItem("Unnamed"); selected = combo->count() - 1; }
    combo->setCurrentIndex(selected);
  }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonNamedView")) {
    const QSignalBlocker block(combo);
    while (combo->count() > 10) combo->removeItem(10);
    for (const auto& view : m_State.namedViews) combo->addItem(Text(view.name.c_str()));
    const auto* current = CurrentNamedView(m_State); combo->setCurrentText(current ? Text(current->name.c_str()) : "Unsaved View");
  }
  if (auto* size = ribbon->findChild<QDoubleSpinBox*>("ribbonPointSize")) { const QSignalBlocker block(size); size->setValue(m_State.pointCloudDisplay.pointSizePx); }
  if (auto* lod = ribbon->findChild<QSpinBox*>("ribbonPointLod")) { const QSignalBlocker block(lod); lod->setValue(m_State.pointCloudDisplay.lodTargetPoints); }
  if (auto* combo = ribbon->findChild<QComboBox*>("ribbonPointColor")) { const QSignalBlocker block(combo); combo->setCurrentIndex(static_cast<int>(m_State.pointCloudDisplay.colorScheme)); }
  if (auto* radius = ribbon->findChild<QDoubleSpinBox*>("ribbonCenterlineRadius")) { const QSignalBlocker block(radius); radius->setValue(m_State.extractCenterlineSearchRadiusFt); }
  if (auto* tolerance = ribbon->findChild<QSpinBox*>("ribbonCenterlineTolerance")) { const QSignalBlocker block(tolerance); tolerance->setValue(qRound(m_State.extractCenterlineMaxResidualRatio * 100)); }
  for (auto* button : findChildren<QToolButton*>()) if (button->objectName().startsWith("collapsed_") && button->menu() && !button->menu()->actions().empty()) {
    button->setIcon(button->menu()->actions().front()->icon()); button->menu()->setIcon(button->icon());
  }
}

void EditorWindow::EditRibbonHatch(const QString& field, const QString& value) {
  if (m_State.activeDrawingIdx > 0 && m_State.activeDrawingIdx < static_cast<int>(m_State.drawingTabs.size())) {
    const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
    for (const auto& project : m_State.openProjects) if (project.uid == uid && project.readOnly) { Refresh(); return; }
  }
  std::vector<int> selected;
  for (const auto& item : m_State.selection) if (item.type == SelectedEntity::Type::FilledRegion && item.index >= 0 && item.index < static_cast<int>(m_State.cadFilledRegions.size())) selected.push_back(item.index);
  if (!selected.empty()) {
    PushUndoSnapshot(m_State, "Edit hatch");
    m_State.cadFilledRegionAttrs.resize(m_State.cadFilledRegions.size());
    for (int index : selected) {
      auto& region = m_State.cadFilledRegions[index]; auto& attrs = m_State.cadFilledRegionAttrs[index];
      if (field == "pattern") region.patternName = value.toUtf8().toStdString();
      else if (field == "layer") attrs.layer = value.toUtf8().toStdString();
      else if (field == "color") attrs.color = value.toStdString();
      else if (field == "angle") region.patternAngleDeg = value.toFloat();
      else if (field == "scale") region.patternScale = value.toFloat();
      else if (field == "transparency") attrs.transparency = value.toFloat();
    }
    BumpCadGpuCache(m_State);
  } else {
    if (field == "pattern") m_State.hatchPatternName = value.toUtf8().toStdString();
    else if (field == "layer") m_State.hatchLayer = value.toUtf8().toStdString();
    else if (field == "angle") m_State.hatchAngleDeg = value.toFloat();
    else if (field == "scale") m_State.hatchScale = value.toFloat();
    else if (field == "transparency") m_State.hatchTransparency01 = value.toFloat();
    else if (field == "color") { const QColor color(value); m_State.hatchColorRgb[0] = color.redF(); m_State.hatchColorRgb[1] = color.greenF(); m_State.hatchColorRgb[2] = color.blueF(); }
  }
  m_Viewport->InvalidateGeometry(); Refresh();
}

void EditorWindow::RunRibbonAction(const QString& id) {
  QString command = id;
  for (const auto& item : qtribbon::kButtons) if (Text(item.id) == id) { if (item.placeholder) return; command = Text(item.command); break; }
  const int surface = SelectedSurface(m_State);
  const std::string surfaceName = surface >= 0 ? m_State.cadSurfaces[surface].name : std::string{};
  if (id == "##ModPipeRun") { CancelActiveCommand(m_State, m_Log); StartPipeRunAtCurrentSize(m_State, m_Log); }
  else if (id == "##RibbonInsEdit" || id == "##BeEdit") { CadBlocksOpenEditPicker(m_State, m_Log); ShowBlockEditorPicker(); }
  else if (command == "TOOLSPACE" || command == "@properties") {
    auto* dock = findChild<QDockWidget*>(command == "TOOLSPACE" ? "toolspaceDock" : "propertiesDock");
    if (dock) { dock->show(); dock->raise(); dock->widget()->setFocus(); }
  } else if (command == "@drawingSettings" || command == "@settings") { ShowUnits(); }
  else if (command == "@newView") {
    bool accepted = false; const auto name = QInputDialog::getText(this, "Save view", "View name", QLineEdit::Normal, {}, &accepted);
    if (accepted && !name.trimmed().isEmpty()) Submit("VIEW S " + name.trimmed());
  } else if (command == "@viewManager") m_State.showViewManagerWindow = true;
  else if (command == "@textStyles") m_State.showTextStyleManagerWindow = true;
  else if (command == "@dimStyles") StartDimStyleCommand(m_State, m_Log);
  else if (command == "@leaderStyles") m_State.showMleaderStyleDialog = true;
  else if (command == "@addLeader") StartMleaderAddLeaderCommand(m_State, m_Log);
  else if (command == "@removeLeader") RemoveLeaderFromSelectedMultileader(m_State, m_Log);
  else if (command == "@editElevations") m_State.showFeatureLineElevWindow = true;
  else if (command == "@createSurface") m_State.showCreateSurfaceWindow = true;
  else if (command == "@pointGroups") m_State.showPointGroupManagerWindow = true;
  else if (command == "@surfaceProperties" && surface >= 0) { m_State.surfacePropertiesIndex = surface; m_State.showSurfacePropertiesWindow = true; }
  else if (command == "@volumes") m_State.volumeDashboard.open = true;
  else if (command == "@connectionModes") m_State.showConnectionModesWindow = true;
  else if (command == "@blockPalettes") m_State.blockAuthoringPaletteOpen = !m_State.blockAuthoringPaletteOpen;
  else if (command == "@createBlock") StartBlockCreateDialog(m_State, m_Log);
  else if (command == "@geoReorient") StartGeoReorientMarkerCommand(m_State, m_Log);
  else if (command == "@geoRemove") {
    if (QMessageBox::question(this, "Remove location", "Remove this drawing's geographic location?", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) RemoveGeoLocation(m_State, m_Log);
  } else if (command.startsWith("@map:")) SetOnlineMap(m_State, static_cast<DrawingSettings::OnlineMap>(command.mid(5).toInt()), m_Log);
  else if (command == "@captureArea") StartCaptureMapArea(m_State, true, m_Log);
  else if (command == "@captureViewport") StartCaptureMapArea(m_State, false, m_Log);
  else if (command == "@removeCaptures") RemoveCapturedMapAreas(m_State, m_Log);
  else if (command == "@surfaceManager") m_State.showSurfaceManagerWindow = true;
  else if (command == "@surfaceBreaklines" && surface >= 0) StartDesignateBreaklineCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceContours" && surface >= 0) StartDesignateContourCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceBoundary" && surface >= 0) StartDesignateBoundaryCommand(m_State, surfaceName, CadBoundaryKind::Outer, m_Log);
  else if (command == "@surfaceAddPoint" && surface >= 0) StartSurfAddPointCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceDeletePoint" && surface >= 0) StartSurfDelPointCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceMovePoint" && surface >= 0) StartSurfMovePointCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceDeleteLine" && surface >= 0) StartSurfDelLineCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceSwapEdge" && surface >= 0) StartSurfSwapEdgeCommand(m_State, surfaceName, m_Log);
  else if (command == "@surfaceRebuild" && surface >= 0) Submit(Text(("SURFACEREBUILD " + surfaceName).c_str()));
  else if (command == "@extractContours" && surface >= 0) Submit(Text(("EXTRACT " + surfaceName).c_str()));
  else if (command == "@pdfBackground" || command == "@pdfVectorize") {
    for (const auto& item : m_State.selection) if (item.type == SelectedEntity::Type::PdfUnderlay && item.index >= 0 && item.index < static_cast<int>(m_State.pdfAttachments.size())) {
      if (command == "@pdfBackground") { PushUndoSnapshot(m_State, "PDF background"); m_State.pdfAttachments[item.index].showBackground = !m_State.pdfAttachments[item.index].showBackground; BumpCadGpuCache(m_State); }
      else VectorizePdfAttachmentLines(m_State, item.index, m_Log);
      break;
    }
  } else if (command == "@importDxf" || command == "@importDwg") {
    const bool dxf = command == "@importDxf";
    const auto path = QFileDialog::getOpenFileName(this, "Import drawing", {}, dxf ? "DXF (*.dxf)" : "DWG (*.dwg)");
    if (!path.isEmpty()) { if (dxf) ImportDxfFile(m_State, path.toUtf8().constData(), m_Log); else ImportDwgFile(m_State, path.toUtf8().constData(), m_Log); }
  } else if (command == "@exportDxf" || command == "@exportDwg") {
    const bool dxf = command == "@exportDxf";
    const auto path = QFileDialog::getSaveFileName(this, "Export drawing", {}, dxf ? "DXF (*.dxf)" : "DWG (*.dwg)");
    if (!path.isEmpty()) {
      const bool proceed = dxf || ConfirmDwgExport(path);
      if (proceed) { if (dxf) ExportDxfFile(m_State, path.toUtf8().constData(), m_Log); else ExportDwgFile(m_State, path.toUtf8().constData(), m_Log); }
    }
  } else if (command == "@batchPlot") {
    m_State.batchPlotSelected.clear(); if (m_State.activeSpaceIndex >= 0) m_State.batchPlotSelected.push_back(m_State.activeSpaceIndex); m_State.showBatchPlotDialog = true;
  } else if (command == "@layoutManager") {
    ShowLayoutManager();
  } else if (command == "@pageSetup") {
    StartPageSetupCommand(m_State, m_Log);
    if (m_State.showPageSetupManager) ShowPageSetupManager(); else QMessageBox::information(this, "Page Setup Manager", QString::fromUtf8(m_Log.back().c_str()));
  } else if (command == "@vpScale") {
    ShowViewportScale();
  } else if (command == "@viewports") {
    if (m_State.activeSpaceIndex < 0) QMessageBox::information(this, "Viewports", "Switch to a paper layout first.");
    else ShowViewportsManager();
  } else if (command == "PLOT") {
    const auto path = QFileDialog::getSaveFileName(this, "Plot layout", {}, "PDF (*.pdf)");
    if (!path.isEmpty()) PlotLayoutsToPdf(m_State, {std::max(0, m_State.activeSpaceIndex)}, path.toUtf8().constData(), m_Log);
  } else if (!command.startsWith('@')) {
    CancelActiveCommand(m_State, m_Log); Submit(command);
  }
  Refresh();
}

bool EditorWindow::ConfirmDwgExport(const QString& path) {
  const auto losses = ComputeDwgExportLosses(m_State);
  const bool overwriting = QFileInfo::exists(path);
  QDialog dialog(this);
  dialog.setObjectName("dwgExportWarning");
  dialog.setWindowTitle("Export DWG");
  dialog.setMinimumWidth(440);
  auto* layout = new QVBoxLayout(&dialog);
  auto* formatLabel = new QLabel("DWG format:", &dialog);
  layout->addWidget(formatLabel);
  auto* format = new QComboBox(&dialog);
  format->setObjectName("dwgExportVersion");
  size_t formatCount = 0;
  const auto* formats = DwgExportFormatRows(&formatCount);
  int selectedIndex = -1;
  for (size_t i = 0; i < formatCount; ++i) {
    if (!formats[i].selectable) continue;
    format->addItem(QString("%1 (%2)").arg(Text(formats[i].displayName), Text(formats[i].acTag)),
                    static_cast<int>(formats[i].version));
    if (formats[i].version == m_State.dwgExportVersion) selectedIndex = format->count() - 1;
  }
  if (selectedIndex >= 0) format->setCurrentIndex(selectedIndex);
  layout->addWidget(format);

  auto* details = new QLabel(&dialog);
  details->setObjectName("dwgExportLossDetails");
  details->setWordWrap(true);
  auto updateDetails = [&, details](int index) {
    const auto version = static_cast<DwgSaveVersion>(format->itemData(index).toInt());
    QStringList lines;
    if (!losses.empty()) {
      lines.append(QString("This export drops or changes (as %1):").arg(Text(DwgSaveVersionAcTag(version))));
      for (const auto& loss : losses)
        lines.append(QString("• %1 %2").arg(loss.count).arg(Text(loss.label.c_str())));
    } else {
      lines.append("This drawing has no known DWG export losses.");
    }
    if (overwriting)
      lines.append(QString("\n%1 already exists. Exporting will replace its current contents.").arg(QFileInfo(path).fileName()));
    details->setText(lines.join('\n'));
  };
  updateDetails(format->currentIndex());
  connect(format, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updateDetails);
  layout->addWidget(details);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* proceed = buttons->addButton(overwriting ? "Overwrite" : "Export", QDialogButtonBox::AcceptRole);
  proceed->setObjectName("confirmDwgExport");
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName("cancelDwgExport");
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return false;
  m_State.dwgExportVersion = static_cast<DwgSaveVersion>(format->currentData().toInt());
  return true;
}
