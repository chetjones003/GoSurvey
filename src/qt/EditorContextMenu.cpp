// The drawing's right-click: shortcut menu, Right-click Customization and the Selection list
// (issue #891; REQ-054, REQ-084).
//
// Which verdict a right-click gets is `CadClassifyRightClick`, shared with the ImGui frontend; this
// file builds the Qt menu and dialogs that verdict opens. Every item runs the same Start*/Apply*
// command the ImGui menu runs.
#include "EditorWindow.hpp"

#include "EditorViewport.hpp"
#include "UserPrefs.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPalette>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

namespace {
QString Text(const std::string& text) { return QString::fromUtf8(text.c_str()); }

QString EntityLabel(const AppCommandState& st, const SelectedEntity& e) {
  using T = SelectedEntity::Type;
  const QString n = QString::number(e.index + 1);
  switch (e.type) {
  case T::LineSeg: return "Line " + n;
  case T::Circle: return "Circle " + n;
  case T::Arc: return "Arc " + n;
  case T::Ellipse: return "Ellipse " + n;
  case T::Polyline: return "Polyline " + n;
  case T::Annotation:
    if (e.index >= 0 && static_cast<size_t>(e.index) < st.cadAnnotations.size()) {
      const auto& a = st.cadAnnotations[static_cast<size_t>(e.index)];
      const QString kind = a.kind == CadAnnotation::Kind::Mtext ? "MText" : a.kind == CadAnnotation::Kind::Text ? "Text" : "Dimension";
      return kind + " " + n + " \"" + Text(a.text).left(24) + "\"";
    }
    return "Annotation " + n;
  case T::Table: return "Table " + n;
  case T::BlockRef: return "Block " + n;
  case T::FilledRegion: return "Hatch " + n;
  case T::Surface: return "Surface " + n;
  default: return "Object " + n;
  }
}
}  // namespace

void EditorWindow::ShowDrawingContextMenu(const QPoint& globalPosition) {
  using AK = AppCommandState::Kind;
  auto& st = m_State;
  const bool gripActive = st.dimGripMoveActive || st.entityGripMoveActive || st.mtextGripMoveActive ||
                          st.mtextRichEditorOpen || st.tableCellEditorOpen;
  if (gripActive) return;
  if (auto* existing = findChild<QMenu*>("drawingContextMenu")) { existing->close(); existing->deleteLater(); }
  const bool hasSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  auto* menu = new QMenu(this);
  menu->setObjectName("drawingContextMenu");
  menu->setAttribute(Qt::WA_DeleteOnClose);
  // Every item changes the drawing or starts a command, so redraw once it has run.
  auto run = [this](std::function<void()> action) {
    return [this, action] { action(); Refresh(); if (m_Viewport) m_Viewport->update(); };
  };
  auto add = [&](QMenu* into, const QString& label, std::function<void()> action, bool enabled = true,
                 const char* name = nullptr) {
    auto* item = into->addAction(label, this, run(std::move(action)));
    item->setEnabled(enabled);
    if (name) item->setObjectName(name);
    return item;
  };

  if (st.active != AK::None) {
    // Command Mode: only what a half-finished command can use.
    add(menu, "Enter", [this] { char empty[2]{}; ProcessCommandLineSubmit(empty, sizeof(empty), m_State, m_Log); }, true, "ctxEnter");
    add(menu, "Cancel", [this] { CancelActiveCommand(m_State, m_Log); }, true, "ctxCancel");
    menu->popup(globalPosition);
    return;
  }

  // The 0.7.4 order, under small group captions.
  auto caption = [&](const QString& text) {
    menu->addSeparator();
    auto* title = menu->addAction(text.toUpper());
    title->setEnabled(false);
    QFont bold = title->font(); bold.setBold(true); bold.setPointSizeF(bold.pointSizeF() * 0.85); title->setFont(bold);
  };
  struct Tool { const char* label; void (*start)(AppCommandState&, std::vector<std::string>&); };
  const Tool first[] = {{"Move", StartMoveCommand}, {"Copy Selection", StartCopyCommand}, {"Rotate", StartRotateCommand},
                        {"Scale", StartScaleCommand}, {"Mirror", StartMirrorCommand}, {"Lengthen", StartLengthenCommand},
                        {"Erase", StartDeleteCommand}};
  const Tool second[] = {{"Offset", StartOffsetCommand}, {"Trim", StartTrimCommand}, {"Extend", StartExtendCommand},
                         {"Break", StartBreakCommand}, {"Stretch", StartStretchCommand}, {"Fillet", StartFilletCommand},
                         {"Chamfer", StartChamferCommand}, {"Join", StartJoinCommand}};
  const bool hasClip = !st.clipboard.empty();
  caption("Recent");
  if (st.lastCommand != AK::None)
    add(menu, QString("Repeat %1").arg(AppCommandState::KindName(st.lastCommand)), [this] { RepeatLastCommand(m_State, m_Log); }, true, "ctxRepeat");
  auto* recent = menu->addMenu("Recent Input");
  recent->setObjectName("ctxRecentInput");
  recent->setEnabled(!m_CommandHistory.isEmpty());
  for (int i = static_cast<int>(m_CommandHistory.size()); i-- > 0;) {
    const QString entry = m_CommandHistory[i];
    recent->addAction(entry, this, [this, entry] { Submit(entry); Refresh(); });
  }

  caption("Isolation and clipboard");
  auto* isolate = menu->addMenu("Isolate Objects");
  isolate->setObjectName("ctxIsolate");
  add(isolate, "Isolate Objects", [this] { IsolateSelectedObjects(m_State, m_Log); }, hasSel, "ctxIsolateObjects");
  add(isolate, "Hide Objects", [this] { HideSelectedObjects(m_State, m_Log); }, hasSel, "ctxHideObjects");
  add(isolate, "End Object Isolation", [this] { EndObjectIsolation(m_State, m_Log); }, !st.hiddenEntityIds.empty(), "ctxEndIsolation");

  auto* clipboard = menu->addMenu("Clipboard");
  clipboard->setObjectName("ctxClipboard");
  add(clipboard, "Cut\tCtrl+X", [this] { CopySelectionToClipboard(m_State, m_Log); StartDeleteCommand(m_State, m_Log); }, hasSel, "ctxCut");
  add(clipboard, "Copy\tCtrl+C", [this] { CopySelectionToClipboard(m_State, m_Log); }, hasSel, "ctxCopy");
  add(clipboard, "Paste\tCtrl+V", [this] { StartPasteCommand(m_State, m_Log); }, hasClip, "ctxPaste");
  add(clipboard, "Paste at Original Coordinates", [this] { StartPasteOrigCommand(m_State, m_Log); }, hasClip, "ctxPasteOriginal");

  caption("Modify");
  auto* modify = menu->addMenu("Basic Modify Tools");
  modify->setObjectName("ctxModify");
  modify->setEnabled(hasSel);
  for (const auto& tool : first) add(modify, tool.label, [this, start = tool.start] { start(m_State, m_Log); });
  modify->addSeparator();
  for (const auto& tool : second) add(modify, tool.label, [this, start = tool.start] { start(m_State, m_Log); });
  caption("View");

  add(menu, "Pan", [this] { StartPanCommand(m_State, m_Log); }, true, "ctxPan");
  add(menu, "Zoom", [this] { StartZoomWindowCommand(m_State, m_Log); }, true, "ctxZoom");
  add(menu, "Free Orbit", [this] { StartOrbitCommand(m_State, m_Log); }, true, "ctxOrbit");
  caption("Tools");
  // No "Find..." item: GoSurvey has no drawing-wide FIND, and a control that does nothing is what
  // REQ-201 forbids (recorded in REQ-084).
  add(menu, "Quick Select...", [this] { StartQuickSelectCommand(m_State, m_Log); }, true, "ctxQuickSelect");
  add(menu, "Options...", [this] { StartOptionsCommand(m_State, m_Log); }, true, "ctxOptions");
  if (hasSel) {
    caption("Selection");
    add(menu, "Select similar", [this] { SelectSimilarToCurrentSelection(m_State, &m_Log); }, true, "ctxSelectSimilar");
    add(menu, "Selection...", [this] {
      m_State.selectionCycleEntities = m_State.selection;
      m_State.selectionCycleSurveyPoints = m_State.selectedSurveyPointIndices;
      m_State.showSelectionCyclingWindow = true;
    }, true, "ctxSelectionList");
    add(menu, "Clear selection", [this] { ClearCadSelection(m_State); BumpCadGpuCache(m_State); }, true, "ctxClearSelection");
  }
  menu->popup(globalPosition);
}

void EditorWindow::ShowSelectionList() {
  if (auto* existing = findChild<QDialog*>("selectionList")) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new QDialog(this);
  dialog->setObjectName("selectionList");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Selection");
  dialog->resize(320, 380);
  auto* layout = new QVBoxLayout(dialog);
  auto* count = new QLabel(dialog);
  count->setObjectName("selectionListCount");
  layout->addWidget(count);
  auto* list = new QListWidget(dialog);
  list->setObjectName("selectionListItems");
  layout->addWidget(list, 1);
  auto isSelected = [this](const SelectedEntity& e) {
    return std::any_of(m_State.selection.begin(), m_State.selection.end(),
                       [&](const SelectedEntity& s) { return s.type == e.type && s.index == e.index; });
  };
  auto fill = [this, count, list, isSelected] {
    const QSignalBlocker block(list);
    list->clear();
    for (const auto& e : m_State.selectionCycleEntities) {
      auto* item = new QListWidgetItem(EntityLabel(m_State, e), list);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(isSelected(e) ? Qt::Checked : Qt::Unchecked);
    }
    for (int index : m_State.selectionCycleSurveyPoints) {
      const bool known = static_cast<size_t>(index) < m_State.surveyPoints.size();
      auto* item = new QListWidgetItem(known ? QString("Survey Pt #%1").arg(m_State.surveyPoints[static_cast<size_t>(index)].id)
                                             : QString("Survey Pt %1").arg(index + 1), list);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(std::find(m_State.selectedSurveyPointIndices.begin(), m_State.selectedSurveyPointIndices.end(), index) !=
                          m_State.selectedSurveyPointIndices.end() ? Qt::Checked : Qt::Unchecked);
    }
    const int total = list->count();
    count->setText(QString("%1 item%2 in list").arg(total).arg(total == 1 ? "" : "s"));
  };
  fill();
  // Ticking an item adds or removes exactly that object from the live selection.
  connect(list, &QListWidget::itemChanged, dialog, [this, list](QListWidgetItem* item) {
    const int row = list->row(item);
    const int cad = static_cast<int>(m_State.selectionCycleEntities.size());
    const bool on = item->checkState() == Qt::Checked;
    if (row < cad) {
      const SelectedEntity e = m_State.selectionCycleEntities[static_cast<size_t>(row)];
      auto same = [&](const SelectedEntity& s) { return s.type == e.type && s.index == e.index; };
      auto& sel = m_State.selection;
      if (on && std::none_of(sel.begin(), sel.end(), same)) sel.push_back(e);
      if (!on) sel.erase(std::remove_if(sel.begin(), sel.end(), same), sel.end());
      EnsureAttrCounts(m_State);
    } else {
      const int index = m_State.selectionCycleSurveyPoints[static_cast<size_t>(row - cad)];
      auto& sel = m_State.selectedSurveyPointIndices;
      if (on && std::find(sel.begin(), sel.end(), index) == sel.end()) sel.push_back(index);
      if (!on) sel.erase(std::remove(sel.begin(), sel.end(), index), sel.end());
    }
    BumpCadGpuCache(m_State);
    Refresh();
  });
  auto* buttons = new QHBoxLayout();
  auto* refresh = new QPushButton("Refresh", dialog);
  auto* none = new QPushButton("Deselect All", dialog);
  auto* all = new QPushButton("Select All", dialog);
  refresh->setObjectName("selectionListRefresh"); none->setObjectName("selectionListDeselectAll"); all->setObjectName("selectionListSelectAll");
  for (auto* b : {refresh, none, all}) buttons->addWidget(b);
  layout->addLayout(buttons);
  connect(refresh, &QPushButton::clicked, dialog, [this, fill] {
    m_State.selectionCycleEntities = m_State.selection;
    m_State.selectionCycleSurveyPoints = m_State.selectedSurveyPointIndices;
    fill();
  });
  connect(none, &QPushButton::clicked, dialog, [this, fill] { ClearCadSelection(m_State); BumpCadGpuCache(m_State); Refresh(); fill(); });
  connect(all, &QPushButton::clicked, dialog, [this, fill, isSelected] {
    for (const auto& e : m_State.selectionCycleEntities) if (!isSelected(e)) m_State.selection.push_back(e);
    for (int index : m_State.selectionCycleSurveyPoints)
      if (std::find(m_State.selectedSurveyPointIndices.begin(), m_State.selectedSurveyPointIndices.end(), index) == m_State.selectedSurveyPointIndices.end())
        m_State.selectedSurveyPointIndices.push_back(index);
    EnsureAttrCounts(m_State); BumpCadGpuCache(m_State); Refresh(); fill();
  });
  dialog->show();
}

void EditorWindow::ShowRightClickCustomization() {
  if (auto* existing = findChild<QDialog*>("rightClickCustomization")) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  using DM = AppCommandState::RightClickDefaultMode;
  using EM = AppCommandState::RightClickEditMode;
  using CM = AppCommandState::RightClickCommandMode;
  auto* dialog = new QDialog(this);
  dialog->setObjectName("rightClickCustomization");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Right-Click Customization");
  auto* layout = new QVBoxLayout(dialog);
  {  // graphite banner over bordered cards
    layout->setContentsMargins(0, 0, 0, 12);
    auto* banner = new QLabel("<b style='font-size:15px'>Right-Click Customization</b><br>Choose what right-click does, and whether a quick click means ENTER.", dialog);
    banner->setObjectName("rightClickBanner");
    banner->setStyleSheet(palette().color(QPalette::Window).lightness() < 128
        ? "background:#30373c; color:#ffffff; border-bottom:2px solid #55d8d0; padding:12px 16px; font-size:13px;"
        : "background:#1f3a5f; color:#ffffff; padding:12px 16px; font-size:13px;");
    layout->addWidget(banner);
  }
  // Everything is staged in the widgets; nothing reaches the preferences until Apply & Close.
  auto* timed = new QCheckBox("Turn on time-sensitive right-click:", dialog);
  timed->setObjectName("rightClickTimeSensitive");
  timed->setChecked(m_State.rightClickTimeSensitive);
  layout->addWidget(timed);
  auto* rule = new QLabel("Quick click for ENTER\nLonger click to display Shortcut Menu", dialog);
  rule->setContentsMargins(24, 0, 0, 0);
  layout->addWidget(rule);
  auto* durationRow = new QHBoxLayout();
  durationRow->setContentsMargins(24, 0, 0, 0);
  auto* duration = new QSpinBox(dialog);
  duration->setObjectName("rightClickLongerClickMs");
  duration->setRange(AppCommandState::kRightClickMinMs, AppCommandState::kRightClickMaxMs);
  duration->setValue(m_State.rightClickLongerClickMs);
  durationRow->addWidget(new QLabel("Longer click duration:", dialog));
  durationRow->addWidget(duration);
  durationRow->addWidget(new QLabel("milliseconds", dialog));
  durationRow->addStretch();
  layout->addLayout(durationRow);

  auto* groups = new QVBoxLayout();
  layout->addLayout(groups);
  auto group = [&](const QString& title, const QString& sentence, const char* name, const QStringList& options, int current) {
    auto* box = new QGroupBox(title, dialog);
    box->setObjectName(name);
    auto* inner = new QVBoxLayout(box);
    inner->addWidget(new QLabel(sentence, box));
    auto* buttons = new QButtonGroup(box);
    for (int i = 0; i < options.size(); ++i) {
      auto* radio = new QRadioButton(options[i], box);
      radio->setObjectName(QString("%1_%2").arg(name).arg(i));
      radio->setChecked(i == current);
      buttons->addButton(radio, i);
      inner->addWidget(radio);
    }
    box->setStyleSheet("QGroupBox { border:1px solid palette(mid); border-radius:4px; margin-top:12px; padding:10px 8px 6px 8px; } QGroupBox::title { subcontrol-origin:margin; left:10px; font-weight:bold; }");
    groups->addWidget(box);
    return buttons;
  };
  auto* defaults = group("Default Mode", "If no objects are selected, right-click means", "rightClickDefaultMode",
                         {"Repeat Last Command", "Shortcut Menu"}, static_cast<int>(m_State.rightClickDefaultMode));
  auto* edits = group("Edit Mode", "If one or more objects are selected, right-click means", "rightClickEditMode",
                      {"Repeat Last Command", "Shortcut Menu"}, static_cast<int>(m_State.rightClickEditMode));
  auto* commands = group("Command Mode", "If a command is in progress, right-click means", "rightClickCommandMode",
                         {"ENTER", "Shortcut Menu: always enabled", "Shortcut Menu: enabled when command options are present"},
                         static_cast<int>(m_State.rightClickCommandMode));
  // The timer, not the preference, decides Default and Command Mode while it is on; greying them
  // says so rather than silently ignoring them (REQ-084 (a), REQ-201). Edit Mode stays live.
  auto syncEnabled = [=] {
    duration->setEnabled(timed->isChecked());
    defaults->button(0)->parentWidget()->setEnabled(!timed->isChecked());
    commands->button(0)->parentWidget()->setEnabled(!timed->isChecked());
  };
  connect(timed, &QCheckBox::toggled, dialog, syncEnabled);
  syncEnabled();

  auto* buttons = new QDialogButtonBox(dialog);
  auto* apply = buttons->addButton("Apply && Close", QDialogButtonBox::AcceptRole);
  apply->setObjectName("rightClickApply");
  auto* cancel = buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
  cancel->setObjectName("rightClickCancel");
  auto* help = buttons->addButton("Help", QDialogButtonBox::HelpRole);
  help->setObjectName("rightClickHelp");
  layout->addWidget(buttons);
  connect(help, &QPushButton::clicked, dialog, [dialog] {
    QMessageBox::information(dialog, "Right-Click Customization",
        "Default Mode: what right-click does when nothing is selected.\n"
        "Edit Mode: what it does when objects are selected.\n"
        "Command Mode: what it does while a command is running.\n\n"
        "With time-sensitive right-click on, a quick click is ENTER and a longer click opens the "
        "shortcut menu; Default and Command Mode are then decided by the timer.");
  });
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(apply, &QPushButton::clicked, dialog, [this, dialog, timed, duration, defaults, edits, commands] {
    auto& st = m_State;
    const auto before = std::tuple(st.rightClickTimeSensitive, st.rightClickLongerClickMs, st.rightClickDefaultMode,
                                   st.rightClickEditMode, st.rightClickCommandMode);
    st.rightClickTimeSensitive = timed->isChecked();
    st.rightClickLongerClickMs = duration->value();
    st.rightClickDefaultMode = static_cast<DM>(defaults->checkedId());
    st.rightClickEditMode = static_cast<EM>(edits->checkedId());
    st.rightClickCommandMode = static_cast<CM>(commands->checkedId());
    if (SaveUserStartupPrefs(st)) {
      m_Log.push_back("Right-click customization saved (gosurvey-user.json).");
      dialog->accept();
      Refresh();
      return;
    }
    // A failed write must not leave behaviour that will not survive a restart.
    std::tie(st.rightClickTimeSensitive, st.rightClickLongerClickMs, st.rightClickDefaultMode, st.rightClickEditMode,
             st.rightClickCommandMode) = before;
    m_Log.push_back("Error: failed to write gosurvey-user.json (check directory permissions).");
    QMessageBox::warning(dialog, "Right-Click Customization", "Your right-click settings could not be saved. Nothing was changed.");
  });
  dialog->show();
}
