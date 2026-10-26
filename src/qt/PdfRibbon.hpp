#pragma once
// The PDF window's ribbon (REQ-455): the same look and behaviour as the main window's ribbon (EditorRibbon.cpp):
// tabs of panels with captions, large and small buttons, and whole panels that collapse into menus holding the very
// same actions (live selectors included) and then into a "More" menu, so narrowing the window never loses a command.
// This is a deliberate copy kept next to the PDF window (decision of the user, issue #1037): the main ribbon is
// untouched, and the two should be kept in step by hand.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>
#include <type_traits>
#include <algorithm>
#include <vector>

QIcon CadCommandIcon(const QString& icon); // EditorWindow.cpp: the application's command artwork

namespace pdfribbon {
constexpr int kPanelHeight = 98;
constexpr int kPageHeight = 102;
constexpr int kHeight = 130;
constexpr int kCaptionBottomSpace = 4;

class Page final : public QWidget {
  struct Panel { QWidget* expanded; QToolButton* collapsed; QMenu* menu; int priority; };
  std::vector<Panel> m_Panels;
  std::vector<int> m_PriorityOrder;
  std::vector<int> m_OverflowPanelIndices;
  QHBoxLayout* m_Row;
  QToolButton* m_More;
  QMenu* m_Overflow;

  // The integer and decimal spin boxes mirror the same way; only the decimals differ.
  template <class Spin>
  void MirrorSpinBoxes(QWidget* panel, QMenu* menu) {
    for (auto* source : panel->findChildren<Spin*>()) {
      auto* mirror = new Spin(menu); mirror->setRange(source->minimum(), source->maximum());
      if constexpr (std::is_same_v<Spin, QDoubleSpinBox>) mirror->setDecimals(source->decimals());
      mirror->setSingleStep(source->singleStep()); mirror->setPrefix(source->prefix()); mirror->setSuffix(source->suffix());
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { const QSignalBlocker block(mirror); mirror->setValue(source->value()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &Spin::valueChanged, source, &Spin::setValue);
    }
  }
  // A collapsed panel keeps its live controls in its menu: each gets a mirror that reads the real control when the
  // menu opens and writes back to it, so no state is duplicated and no widget leaves the ribbon's layout.
  void MirrorControls(QWidget* panel, QMenu* menu) {
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
      connect(mirror, &QComboBox::activated, source, [source](int index) { source->setCurrentIndex(index); });
    }
    MirrorSpinBoxes<QDoubleSpinBox>(panel, menu);
    MirrorSpinBoxes<QSpinBox>(panel, menu);
    for (auto* source : panel->findChildren<QCheckBox*>()) {
      auto* mirror = new QCheckBox(source->text(), menu);
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { const QSignalBlocker block(mirror); mirror->setChecked(source->isChecked()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &QCheckBox::toggled, source, &QCheckBox::setChecked);
    }
    for (auto* source : panel->findChildren<QPushButton*>()) {
      auto* mirror = new QPushButton(source->text(), menu); mirror->setFlat(true);
      auto* entry = new QWidgetAction(menu); entry->setDefaultWidget(mirror); menu->addAction(entry);
      connect(menu, &QMenu::aboutToShow, mirror, [source, mirror] { mirror->setText(source->text()); mirror->setEnabled(source->isEnabled()); });
      connect(mirror, &QPushButton::clicked, source, [source, menu] { menu->hide(); source->click(); });
    }
  }

public:
  explicit Page(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(0); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); setFixedHeight(kPageHeight);
    m_Row = new QHBoxLayout(this); m_Row->setContentsMargins(3, 2, 3, 1); m_Row->setSpacing(2);
    m_More = new QToolButton(this); m_More->setObjectName("pdfRibbonMore"); m_More->setText("More");
    m_More->setIcon(CadCommandIcon("layers.png")); m_More->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_More->setPopupMode(QToolButton::InstantPopup); m_More->setAutoRaise(true); m_More->setFixedSize(62, 90);
    m_Overflow = new QMenu(m_More); m_More->setMenu(m_Overflow); m_More->hide();
    m_Row->addWidget(m_More); m_Row->addStretch();
  }
  void AddPanel(QWidget* panel, const QString& title, const QList<QAction*>& actions, int priority) {
    panel->setParent(this); panel->setProperty("ribbonPanel", true);
    panel->setFixedSize(panel->sizeHint().width(), kPanelHeight);
    auto* collapsed = new QToolButton(this); collapsed->setObjectName("collapsed_" + panel->objectName());
    collapsed->setText(title); collapsed->setToolTip(title); collapsed->setAutoRaise(true);
    collapsed->setFixedSize(std::clamp(fontMetrics().horizontalAdvance(title) + 22, 66, 155), 92);
    collapsed->setIconSize(QSize(26, 26)); collapsed->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    collapsed->setPopupMode(QToolButton::InstantPopup); // the whole button opens the menu, as in the main ribbon
    auto* menu = new QMenu(title, collapsed); menu->setToolTipsVisible(true); collapsed->setMenu(menu);
    if (!actions.empty()) { collapsed->setIcon(actions.front()->icon()); menu->setIcon(actions.front()->icon()); }
    for (auto* action : actions) menu->addAction(action);
    MirrorControls(panel, menu);
    m_Row->insertWidget(m_Row->count() - 2, panel); m_Row->insertWidget(m_Row->count() - 2, collapsed);
    m_Panels.push_back({panel, collapsed, menu, priority});
    m_PriorityOrder.push_back(static_cast<int>(m_Panels.size() - 1));
    std::stable_sort(m_PriorityOrder.begin(), m_PriorityOrder.end(), [this](int a, int b) { return m_Panels[a].priority > m_Panels[b].priority; });
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
    for (int i = 0; i < static_cast<int>(m_Panels.size()); ++i) if (!shown[i]) overflowPanels.push_back(i);
    if (overflowPanels != m_OverflowPanelIndices) {
      m_Overflow->clear();
      for (const int i : overflowPanels) m_Overflow->addMenu(m_Panels[i].menu);
      m_OverflowPanelIndices = std::move(overflowPanels);
    }
    if (m_More->isHidden() == !m_OverflowPanelIndices.empty()) m_More->setVisible(!m_OverflowPanelIndices.empty());
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

// One panel being filled: large buttons (icon over text, three rows tall) and small ones (three to a column), the
// same grid the main ribbon uses. Finish() hands the panel, with its caption, to its page.
struct PanelBuilder {
  QWidget* panel = nullptr;
  QGridLayout* grid = nullptr;
  QList<QAction*> actions;
  QString title;
  int col = 0, row = 0;

  PanelBuilder(QWidget* parent, const QString& tab, const QString& name) : title(name) {
    panel = new QWidget(parent); panel->setObjectName("pdfRibbonPanel_" + tab + "_" + name);
    auto* column = new QVBoxLayout(panel); column->setContentsMargins(5, 0, 5, kCaptionBottomSpace); column->setSpacing(1);
    grid = new QGridLayout; grid->setContentsMargins(0, 0, 0, 0); grid->setHorizontalSpacing(2); grid->setVerticalSpacing(0);
    column->addLayout(grid, 1);
    auto* caption = new QLabel(name, panel); caption->setObjectName("ribbonPanelLabel"); caption->setAlignment(Qt::AlignCenter);
    column->addWidget(caption);
  }
  QToolButton* Large(QAction* action) {
    auto* button = MakeButton(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon); button->setIconSize(QSize(30, 30));
    if (row) { ++col; row = 0; }
    button->setMinimumWidth(58); button->setFixedHeight(73); grid->addWidget(button, 0, col++, 3, 1);
    return button;
  }
  QToolButton* Small(QAction* action, bool iconOnly = false) {
    auto* button = MakeButton(action);
    button->setToolButtonStyle(iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon); button->setIconSize(QSize(18, 18));
    if (iconOnly) button->setFixedWidth(30);
    button->setFixedHeight(24); Next(button);
    return button;
  }
  // A native control (spin box, combo, check box, label) occupies the next small cell.
  void Widget(QWidget* widget) { widget->setParent(panel); Next(widget); }
  void Finish(Page* page, int priority) { page->AddPanel(panel, title, actions, priority); }
private:
  QToolButton* MakeButton(QAction* action) {
    actions.append(action);
    auto* button = new QToolButton(panel); button->setObjectName("tool_" + action->objectName());
    button->setDefaultAction(action); button->setAutoRaise(true); button->setAttribute(Qt::WA_AlwaysShowToolTips);
    button->setAccessibleName(action->text()); return button;
  }
  void Next(QWidget* widget) {
    grid->addWidget(widget, row++, col);
    if (row == 3) { row = 0; ++col; }
  }
};
}  // namespace pdfribbon
