#include "EditorWindow.hpp"
#include "QtAccent.hpp"
#include "EditorViewport.hpp"
#include "EditorChrome.hpp"
#include "CadSnap.hpp"
#include "UserPrefs.hpp"
#include "WikiHelp.hpp"
#include "engine/CommandRegistry.hpp"
#include <QApplication>
#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QSplitter>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextEdit>
#include <QTextBrowser>
#include <QRegularExpression>
#include <QUrl>
#include <QColor>
#include <QToolButton>
#include <QVBoxLayout>
#include <QCursor>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

namespace {

/// What F1 should document for the widget under the cursor: a ribbon/menu action's command route
/// and tooltip, or a plain widget's tooltip (status-bar toggles).
struct HelpHoverTopic {
  QString command;
  QString tooltip;
};

HelpHoverTopic HelpTopicForAction(const QAction* action) {
  if (!action) return {};
  return {action->property("ribbonCommand").toString(), action->toolTip()};
}

HelpHoverTopic HelpTopicForWidget(QWidget* widget) {
  for (QWidget* w = widget; w; w = w->isWindow() ? nullptr : w->parentWidget()) {
    if (auto* button = qobject_cast<QToolButton*>(w); button && button->defaultAction())
      return HelpTopicForAction(button->defaultAction());
    if (!w->toolTip().isEmpty()) return {{}, w->toolTip()};
  }
  return {};
}

/// Registry primary (lowercase) for the first token of a command line or ribbon route, so aliases
/// such as "L" find the LINE page. Unlisted commands (TOOLSPACE, BPARAM, UNDO...) count too.
/// Empty for "@" UI routes and unknown words.
std::string HelpCommandPrimary(const QString& text) {
  const QString token = text.trimmed().section(QRegularExpression("[\\s,]+"), 0, 0);
  if (token.isEmpty() || token.startsWith('@')) return {};
  const auto* def = stepengine::CommandRegistry::Instance().FindAny(token.toLower().toUtf8().toStdString());
  return def ? QString::fromUtf8(def->primary).toLower().toStdString() : std::string();
}

/// Registry primary for a running command. KindName is tried first; a kind whose name is not a
/// command (SOLID, ZOOM...) resolves only when exactly one command runs as that kind.
std::string HelpCommandPrimaryForKind(AppCommandState::Kind kind) {
  if (kind == AppCommandState::Kind::None) return {};
  if (std::string primary = HelpCommandPrimary(QString::fromUtf8(AppCommandState::KindName(kind))); !primary.empty())
    return primary;
  const stepengine::CommandDef* only = nullptr;
  for (const auto& def : stepengine::CommandRegistry::Instance().All()) {
    if (def.kind != kind) continue;
    if (only) return {};
    only = &def;
  }
  return only ? QString::fromUtf8(only->primary).toLower().toStdString() : std::string();
}

}  // namespace

void EditorWindow::BuildDraftingInput() {
  const auto mode = [this](const char* name, const char* label, int key, bool AppCommandState::*member) {
    auto* button = new QToolButton(this);
    button->setObjectName(name); button->setText(label); button->setCheckable(true);
    button->setProperty("draftingMode", true);
    button->setToolTip(QString("%1 (F%2)").arg(label).arg(key - Qt::Key_F1 + 1));
    button->setProperty("draftingKey", key);
    statusBar()->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, member](bool on) {
      m_State.*member = on;
      if (on && member == &AppCommandState::orthoMode) m_State.polarMode = false;
      if (on && member == &AppCommandState::polarMode) m_State.orthoMode = false;
      Refresh();
    });
    return button;
  };
  mode("draftOrtho", "ORTHO", Qt::Key_F8, &AppCommandState::orthoMode);
  auto* polar = mode("draftPolar", "POLAR", Qt::Key_F10, &AppCommandState::polarMode);
  mode("draftOsnap", "OSNAP", Qt::Key_F3, &AppCommandState::objectSnapEnabled);
  mode("draft3dSnap", "3D OSNAP", Qt::Key_F4, &AppCommandState::objectSnap3dEnabled);
  polar->setContextMenuPolicy(Qt::CustomContextMenu);
  polar->setToolTip("Polar tracking (F10). Right-click for increment and additional angles.");
  connect(polar, &QWidget::customContextMenuRequested, this, [this] { ShowPolarSettings(); });
  m_Viewport->snapMenuRequested = [this](const QPoint& position) { ShowSnapOverride(position); };
  m_Viewport->submitLockedInput = [this] { return SubmitDraftingInput(true); };

  m_DynamicInput = new QFrame(m_Viewport);
  m_DynamicInput->setObjectName("dynamicInput");
  auto* layout = new QVBoxLayout(m_DynamicInput);
  layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
  m_DynamicPrompt = new QLabel(m_DynamicInput);
  m_DynamicPrompt->setWordWrap(true); m_DynamicPrompt->setMaximumWidth(420);
  layout->addWidget(m_DynamicPrompt);
  auto* row = new QHBoxLayout; layout->addLayout(row);
  for (int i = 0; i < 3; ++i) {
    m_DynamicLabels[i] = new QLabel(m_DynamicInput);
    m_DynamicFields[i] = new QLineEdit(m_DynamicInput);
    m_DynamicFields[i]->setObjectName(QString("dynamicField%1").arg(i));
    m_DynamicFields[i]->setMinimumWidth(65); m_DynamicFields[i]->setMaximumWidth(145);
    row->addWidget(m_DynamicLabels[i]); row->addWidget(m_DynamicFields[i]);
    connect(m_DynamicFields[i], &QLineEdit::textEdited, this, [this, i](const QString& text) {
      if (m_DynamicGeometry.pointEntry) {
        dyninput::EditText(m_DynamicGroup, i, text.toUtf8().toStdString());
        RefreshDraftingInput();
        UpdateCommandLineFromDraftingInput();
        if (m_DynamicGroup.requestFocus >= 0) {
          const int slot = m_DynamicGroup.requestFocus;
          m_DynamicGroup.requestFocus = -1;
          m_DynamicFields[slot]->setFocus(); m_DynamicFields[slot]->selectAll();
        }
      } else {
        const QSignalBlocker block(m_Command);
        m_Command->setText(text);
      }
    });
    connect(m_DynamicFields[i], &QLineEdit::returnPressed, this, [this] { SubmitDraftingInput(); });
  }
  // REQ-501.2: the alignment readout is a line of the same dynamic input, not a second cursor UI.
  m_DynamicAlignment = new QLabel(m_DynamicInput);
  m_DynamicAlignment->setObjectName("dynamicAlignmentReadout");
  // A light tooltip in both themes, as Civil 3D's alignment tooltip is, so it stands out from the drawing and its grid.
  m_DynamicAlignment->setStyleSheet(
      "QLabel { color:#1f2428; background:#f3f5f6; border:1px solid #b8c0c6; border-radius:3px; padding:3px 8px; }");
  m_DynamicAlignment->hide();
  layout->addWidget(m_DynamicAlignment);
  m_DynamicInput->hide();
}

bool EditorWindow::DynamicInputAcceptsInput() const {
  return m_DynamicInput != nullptr && m_DynamicInput->isVisible() && m_DynamicInputForCommand;
}

void EditorWindow::ResetDraftingInput() {
  m_DynamicReset = true;
  for (auto& field : m_DynamicGroup.f) field = {};
}

void EditorWindow::UpdateDraftingInputFromCommandLine(const QString& text) {
  if (!DynamicInputAcceptsInput()) return;
  if (m_DynamicGeometry.pointEntry) {
    // Rebuild from the complete value so cumulative edits do not append duplicate mode transitions.
    dyninput::Reset(m_DynamicGroup, m_DynamicGeometry.initialMode, m_DynamicGeometry.showZ);
    dyninput::EditText(m_DynamicGroup, 0, text.toUtf8().toStdString());
    m_DynamicGroup.requestFocus = -1;
  } else if (m_DynamicFields[0]) {
    const QSignalBlocker block(m_DynamicFields[0]);
    m_DynamicFields[0]->setText(text);
  }
  RefreshDraftingInput();
}

QString EditorWindow::DraftingInputText() const {
  if (!m_DynamicGeometry.pointEntry) return m_DynamicFields[0]->text();
  const auto live = CadDynInputLive(m_State, m_DynamicGeometry, m_DynamicGroup.mode,
      m_DynamicCursor.x, m_DynamicCursor.y, m_DynamicCursor.z);
  QString text = QString::fromStdString(
      dyninput::Compose(m_DynamicGroup, live, m_DynamicGeometry.directDistance));
  const bool locked = std::any_of(m_DynamicGroup.f.begin(), m_DynamicGroup.f.end(),
      [](const dyninput::Field& field) { return field.locked; });
  if (m_DynamicGeometry.ucsPolar && locked) {
    // UCS axis prompts measure from their pending origin, not the command's ordinary @ base.
    bool distanceOk = true, angleOk = true;
    const double distance = m_DynamicGroup.f[0].locked
        ? QString::fromStdString(m_DynamicGroup.f[0].text).toDouble(&distanceOk) : live.v[0];
    const double angle = m_DynamicGroup.f[1].locked
        ? 90.0 - QString::fromStdString(m_DynamicGroup.f[1].text).toDouble(&angleOk) : live.v[1];
    if (distanceOk && angleOk && std::isfinite(distance) && std::isfinite(angle)) {
      const double radians = angle * 3.14159265358979323846 / 180.0;
      const auto world = ray3d::Add(m_DynamicGeometry.baseWorld,
          ucs::UcsVectorToWorld(m_State.activeUcs,
              {distance * std::cos(radians), distance * std::sin(radians), 0}));
      const auto point = ucs::WorldToUcs(m_State.activeUcs, world);
      text = QString("%1,%2,%3").arg(point.x, 0, 'g', 17).arg(point.y, 0, 'g', 17).arg(point.z, 0, 'g', 17);
    }
  }
  return text;
}

QString EditorWindow::DraftingInputDisplayText() const {
  if (!m_DynamicGeometry.pointEntry || !dyninput::IsPolar(m_DynamicGroup.mode))
    return DraftingInputText();

  const auto& distanceField = m_DynamicGroup.f[0];
  const auto& angleField = m_DynamicGroup.f[1];
  if (!distanceField.locked && !angleField.locked) return {};
  if (m_DynamicGeometry.directDistance && distanceField.locked && !angleField.locked)
    return QString::fromStdString(distanceField.text).trimmed();

  const auto live = CadDynInputLive(m_State, m_DynamicGeometry, m_DynamicGroup.mode,
      m_DynamicCursor.x, m_DynamicCursor.y, m_DynamicCursor.z);
  const QString distance = distanceField.locked
      ? QString::fromStdString(distanceField.text).trimmed()
      : QString::number(live.v[0], 'f', m_State.displayLinearPrecision);
  const double liveAngle = std::fmod(450.0 - live.v[1], 360.0);
  const QString angle = angleField.locked
      ? QString::fromStdString(angleField.text).trimmed()
      : QString::number(liveAngle, 'f', m_State.displayLinearPrecision);
  const QString relative = dyninput::IsRelative(m_DynamicGroup.mode) ? "@" : "";
  return relative + distance + "<" + angle;
}

void EditorWindow::UpdateCommandLineFromDraftingInput() {
  if (!DynamicInputAcceptsInput()) return;
  const QString text = DraftingInputDisplayText();
  if (m_Command->text() == text) return;
  m_Command->setText(text);
}

void EditorWindow::RefreshDraftingInput(const QString& hint) {
  if (!m_DynamicInput) return;
  for (const auto& entry : {std::pair{"draftOrtho", m_State.orthoMode}, {"draftPolar", m_State.polarMode},
                           {"draftOsnap", m_State.objectSnapEnabled}, {"draft3dSnap", m_State.objectSnap3dEnabled}}) {
    auto* button = findChild<QToolButton*>(entry.first);
    const QSignalBlocker block(button); button->setChecked(entry.second);
  }
  auto prompt = CadDynInputPromptFor(m_State);
  if (prompt.ucsPolar) {
    prompt.haveBase = true; prompt.baseWorld = prompt.ucsPolarBase;
    prompt.initialMode = dyninput::Mode::RelativePolar;
  }
  if (m_DynamicReset || (!hint.isEmpty() && hint != m_DynamicHint) ||
      prompt.initialMode != m_DynamicGroup.initial) {
    dyninput::Reset(m_DynamicGroup, prompt.initialMode, prompt.showZ);
    m_DynamicReset = false;
  }
  if (!hint.isEmpty()) m_DynamicHint = hint;
  m_DynamicGeometry = prompt;
  m_DynamicGroup.showZ = prompt.showZ;
  const bool active = m_State.activeDrawingIdx > 0 && m_State.active != AppCommandState::Kind::None &&
      m_State.active != AppCommandState::Kind::Pan && m_State.active != AppCommandState::Kind::Orbit;
  const auto* angSt = stepengine::ActiveSegmentAngleState(m_State);
  const bool bearingPickAwaitingCommand =
      angSt && angSt->pickPhase == stepengine::SegmentAnglePickPhase::WaitAdjustOrCommit;
  if (bearingPickAwaitingCommand && m_DynamicInput->isVisible()) m_Command->clear();
  // REQ-501.2: near an alignment, a point being entered or a grip being dragged reads its station, offset and bearing.
  const QString readout = QString::fromUtf8(m_State.alignmentCursorReadout.c_str());
  const bool gripDrag = m_State.activeDrawingIdx > 0 && m_State.entityGripMoveActive;
  const bool showReadout = !readout.isEmpty() && ((active && !bearingPickAwaitingCommand && prompt.pointEntry) || gripDrag);
  const bool showCommandInput = active && !bearingPickAwaitingCommand;
  m_DynamicInputForCommand = showCommandInput;
  m_DynamicInput->setVisible(showCommandInput || showReadout);
  m_DynamicAlignment->setVisible(showReadout);
  if (showReadout && m_DynamicAlignment->text() != readout) m_DynamicAlignment->setText(readout);
  if (!showCommandInput && !showReadout) return;
  m_DynamicPrompt->setVisible(showCommandInput);
  if (!showCommandInput) {  // a grip drag with no command: the readout alone, beside the cursor
    for (int i = 0; i < 3; ++i) {
      m_DynamicFields[i]->hide();
      m_DynamicLabels[i]->hide();
    }
    m_DynamicInput->adjustSize();
    const QPoint at = m_Viewport->CursorPosition();
    m_DynamicInput->move(std::clamp(at.x() + 20, 0, std::max(0, m_Viewport->width() - m_DynamicInput->width())),
        std::clamp(at.y() + 24, 0, std::max(0, m_Viewport->height() - m_DynamicInput->height() - 85)));
    m_DynamicInput->raise();
    return;
  }
  m_DynamicPrompt->setText(m_DynamicHint);
  const bool hintOnly = !prompt.pointEntry;
  const int count = hintOnly ? 0 : dyninput::FieldCount(m_DynamicGroup);
  const QFontMetrics promptMetrics(m_DynamicPrompt->font());
  const int promptMaxWidth = std::max(200, m_Viewport->width() - 48);
  const int hintWidth = promptMetrics.horizontalAdvance(m_DynamicHint) + 8;
  if (hintOnly && hintWidth <= promptMaxWidth) {
    m_DynamicPrompt->setWordWrap(false);
    m_DynamicPrompt->setMaximumWidth(hintWidth);
  } else {
    m_DynamicPrompt->setWordWrap(true);
    m_DynamicPrompt->setMaximumWidth(hintOnly ? promptMaxWidth : 420);
  }
  const auto live = CadDynInputLive(m_State, prompt, m_DynamicGroup.mode,
      m_DynamicCursor.x, m_DynamicCursor.y, m_DynamicCursor.z);
  for (int i = 0; i < 3; ++i) {
    auto* edit = m_DynamicFields[i];
    edit->setVisible(i < count); m_DynamicLabels[i]->setVisible(i < count && prompt.pointEntry);
    if (i >= count) continue;
    QString text = m_Command->text();
    if (prompt.pointEntry) {
      m_DynamicLabels[i]->setText(QString::fromUtf8(dyninput::Label(m_DynamicGroup, i).c_str()));
      const auto& field = m_DynamicGroup.f[i];
      double value = live.v[i];
      if (dyninput::IsPolar(m_DynamicGroup.mode) && i == 1) value = std::fmod(450.0 - value, 360.0);
      text = field.locked ? QString::fromUtf8(field.text.c_str())
                          : QString::number(value, 'f', m_State.displayLinearPrecision);
    }
    if (edit->text() != text) {
      const QSignalBlocker block(edit); edit->setText(text);
      if (edit->hasFocus() && prompt.pointEntry && !m_DynamicGroup.f[i].locked) edit->selectAll();
    }
    edit->setFixedWidth(std::clamp(edit->fontMetrics().horizontalAdvance(text) + 22, 65, 145));
  }
  m_DynamicInput->adjustSize();
  // Stay inside the drawing even when the pointer is near its right/bottom edge.
  const QPoint cursor = m_Viewport->CursorPosition();
  m_DynamicInput->move(std::clamp(cursor.x() + 20, 0, std::max(0, m_Viewport->width() - m_DynamicInput->width())),
      std::clamp(cursor.y() + 24, 0, std::max(0, m_Viewport->height() - m_DynamicInput->height() - 85)));
  m_DynamicInput->raise();
}

bool EditorWindow::SubmitDraftingInput(bool click) {
  if (!DynamicInputAcceptsInput()) return false;
  const bool locked = std::any_of(m_DynamicGroup.f.begin(), m_DynamicGroup.f.end(),
      [](const dyninput::Field& f) { return f.locked; });
  if (click && (!m_DynamicGeometry.pointEntry || !locked)) { ResetDraftingInput(); return false; }
  const QString text = DraftingInputText();
  ResetDraftingInput(); m_Command->clear(); Submit(text);
  return true;
}

void EditorWindow::CancelDraftingInput() {
  m_State.showCreatePointsWindow = false;
  if (m_Viewport->CancelGripEdit()) {}
  else if (m_State.selBoxWaitingSecond) m_Viewport->CancelSelectionBox();
  else if (m_State.sectionPlaneGripDrag >= 0) AbortSectionPlaneGripDrag(m_State);
  else if (m_State.gizmoDragActive && !IsGizmoCommandKind(m_State.active)) CancelGizmoDrag(m_State);
  else if (const auto* angSt = stepengine::ActiveSegmentAngleState(m_State);
           angSt && angSt->pickPhase != stepengine::SegmentAnglePickPhase::Idle)
    CancelSegmentAnglePick(m_State, &m_Log);
  else if (m_State.active != AppCommandState::Kind::None || m_State.paperLengthenPhase || m_State.paperExtendPhase ||
           m_State.paperBreakPhase || m_State.paperStretchPhase || m_State.paperFilletPhase || m_State.paperChamferPhase)
    CancelActiveCommand(m_State, m_Log);
  else { ClearSelection(m_State); ClearPendingViewportZoom(m_State); }
  ClearPendingOneShotObjectSnap(m_State);
  ResetDraftingInput(); m_Command->clear(); m_Viewport->InvalidateGeometry(); Refresh();
}

bool EditorWindow::DraftingInputEvent(QObject* watched, QEvent* event) {
  auto* widget = qobject_cast<QWidget*>(watched);
  const bool suggestionPopupWidget = widget && m_CommandSuggestions &&
      (widget == m_CommandSuggestions || m_CommandSuggestions->isAncestorOf(widget));
  const bool ownSuggestionsPopup = QApplication::activePopupWidget() == m_CommandSuggestions;
  if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_F1 &&
      !QApplication::activeModalWidget()) {
    // F1 over a ribbon flyout entry documents that entry; the menus must close before the modal opens.
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    const QAction* action = menu ? menu->activeAction() : nullptr;
    // Only menus opened from this window: walk submenus up to the widget that owns the menu.
    QWidget* owner = menu ? menu->parentWidget() : nullptr;
    while (qobject_cast<QMenu*>(owner)) owner = owner->parentWidget();
    const bool ownMenu = owner && (owner == this || isAncestorOf(owner));
    if (ownMenu && action && action->property("ribbonCommand").isValid()) {
      const HelpHoverTopic topic = HelpTopicForAction(action);
      while (auto* open = qobject_cast<QMenu*>(QApplication::activePopupWidget())) open->close();
      ShowContextualHelp(topic.command, topic.tooltip);
      return true;
    }
  }
  if (!widget || !(widget == this || isAncestorOf(widget) || suggestionPopupWidget) ||
      QApplication::activeModalWidget() ||
      (QApplication::activePopupWidget() && !ownSuggestionsPopup)) return false;
  if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
  auto* key = static_cast<QKeyEvent*>(event);
  bool editing = false;
  for (auto* focus = widget; focus; focus = focus->parentWidget()) {
    const auto* line = qobject_cast<QLineEdit*>(focus);
    const auto* text = qobject_cast<QTextEdit*>(focus);
    const auto* plain = qobject_cast<QPlainTextEdit*>(focus);
    if ((line && !line->isReadOnly()) || (text && !text->isReadOnly()) ||
        (plain && !plain->isReadOnly()) || qobject_cast<QAbstractSpinBox*>(focus) ||
        qobject_cast<QComboBox*>(focus)) { editing = true; break; }
    if (focus->isWindow()) break;
  }
  const bool ctrl = key->modifiers().testFlag(Qt::ControlModifier);
  if (event->type() == QEvent::KeyPress && ctrl && key->key() == Qt::Key_Z &&
      widget == m_Command && m_Command->text().isEmpty()) {
    DoUndo(m_State, m_Log);
    ResetDraftingInput();
    Refresh();
    return true;
  }
  if ((widget == m_Command || suggestionPopupWidget) &&
      (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
      event->type() == QEvent::ShortcutOverride) {
    key->accept();
    return false;
  }
  if ((widget == m_Command || suggestionPopupWidget) &&
      (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
    if (m_State.activeDrawingIdx == 0) {
      if (widget == m_Command) m_Command->clear();
      if (m_CommandSuggestions) m_CommandSuggestions->hide();
      if (m_CommandBar) m_CommandBar->Append("Open or create a drawing before entering commands.");
      return true;
    }
    if (widget == m_Command && DynamicInputAcceptsInput()) return SubmitDraftingInput();
    if (m_CommandSuggestions && m_CommandSuggestions->isVisible() &&
        m_CommandSuggestionModel && m_CommandSuggestionModel->rowCount() > 0) {
      QModelIndex selected = m_CommandSuggestions->currentIndex();
      if (!selected.isValid()) selected = m_CommandSuggestionModel->index(0, 0);
      const QString typedLine = m_Command->text();
      const QString typedCommand = typedLine.trimmed();
      const auto exactSuggestions = FuzzyCommandSuggestions(typedCommand.toStdString(), 1);
      const bool exactRegisteredCommand = !exactSuggestions.empty() && exactSuggestions.front().exactMatch;
      // OPEN and CUT are routed by EditorWindow::Submit rather than the command registry, so they need the
      // same exact-name precedence when a fuzzy suggestion such as OPENPACK or CREATEDATASHORTCUT is highlighted.
      const bool exactRoutedVerb = typedCommand.compare(QStringLiteral("OPEN"), Qt::CaseInsensitive) == 0 ||
                                   typedCommand.compare(QStringLiteral("CUT"), Qt::CaseInsensitive) == 0;
      const bool hasArguments = typedCommand.contains(' ') || typedCommand.contains('\t');
      QString command;
      if (exactRoutedVerb)
        command = typedCommand.toUpper();
      else if (exactRegisteredCommand)
        command = QString::fromStdString(exactSuggestions.front().name);
      else if (hasArguments)
        command = typedLine;
      else
        command = selected.data(Qt::DisplayRole).toString();
      m_CommandSuggestions->hide();
      m_Command->clear();
      Submit(command);
      m_Command->setFocus(Qt::OtherFocusReason);
      return true;
    }
    if (widget != m_Command) return false;
    const QString line = m_Command->text();
    m_Command->clear();
    Submit(line);
    return true;  // Keep this Enter from bubbling to the window's blank-Enter handler.
  }
  const bool editShortcut = ctrl && (key->key() == Qt::Key_Z || key->key() == Qt::Key_Y ||
      key->key() == Qt::Key_C || key->key() == Qt::Key_V || key->key() == Qt::Key_X);
  if (editShortcut && event->type() == QEvent::ShortcutOverride) {
    key->accept(); return false; // Block QAction activation, then route the following KeyPress once.
  }
  if (!editing && event->type() == QEvent::KeyPress && editShortcut) {
    if (key->key() == Qt::Key_Z && !key->modifiers().testFlag(Qt::ShiftModifier)) DoUndo(m_State, m_Log);
    else if (key->key() == Qt::Key_Z || key->key() == Qt::Key_Y) DoRedo(m_State, m_Log);
    else if (key->key() == Qt::Key_C) CopySelectionToClipboard(m_State, m_Log);
    else if (key->key() == Qt::Key_V) StartPasteCommand(m_State, m_Log);
    else if (key->key() == Qt::Key_X) CutSelection();
    ResetDraftingInput(); Refresh(); return true;
  }
  bool modeKey = false;
  for (auto* button : findChildren<QToolButton*>()) {
    if (button->property("draftingKey").toInt() != key->key()) continue;
    modeKey = true;
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) button->click();
    break;
  }
  if (modeKey) {
    key->accept();
    return event->type() == QEvent::ShortcutOverride ? false : true;
  }
  if (key->key() == Qt::Key_Escape) {
    if (event->type() == QEvent::KeyPress && (widget == m_Command || suggestionPopupWidget) && m_CommandSuggestions &&
        m_CommandSuggestions->isVisible()) {
      m_CommandSuggestions->hide();
      m_Command->clear();
      key->accept();
      return true;
    }
    if (event->type() == QEvent::ShortcutOverride) { key->accept(); return false; }
    CancelDraftingInput(); key->accept(); return true;
  }
  if (event->type() != QEvent::KeyPress) return false;
  if (key->key() == Qt::Key_F1) {
    // The highlighted (or hovered) fuzzy match wins while the command line is offering suggestions;
    // otherwise document whatever is under the cursor, then the running or last command.
    if (m_CommandSuggestions && m_CommandSuggestions->isVisible() && m_CommandSuggestionModel &&
        m_CommandSuggestionModel->rowCount() > 0) {
      QModelIndex row = m_CommandSuggestions->indexAt(m_CommandSuggestions->viewport()->mapFromGlobal(QCursor::pos()));
      if (!row.isValid()) row = m_CommandSuggestions->currentIndex();
      if (!row.isValid()) row = m_CommandSuggestionModel->index(0, 0);
      ShowContextualHelp(row.data(Qt::DisplayRole).toString());
      return true;
    }
    const HelpHoverTopic topic = HelpTopicForWidget(QApplication::widgetAt(QCursor::pos()));
    ShowContextualHelp(topic.command, topic.tooltip);
    return true;
  }
  if (!editing && key->key() == Qt::Key_Delete) {
    StartDeleteCommand(m_State, m_Log); Refresh(); return true;
  }
  if (!editing) {
    if (const auto* ds = m_State.sessionAs<stepengine::DimSession>();
        ds && ds->linear && ds->phase == stepengine::DimPhase::WaitDimLinePt &&
        (key->key() == Qt::Key_H || key->key() == Qt::Key_V) && !ctrl) {
      CadDimLinearApplyHVHotkey(m_State, key->key() == Qt::Key_V, m_Log); Refresh(); return true;
    }
  }
  for (int i = 0; i < 3; ++i) if (widget == m_DynamicFields[i]) {
    // Consume Return here: propagation after returnPressed would submit a second blank line.
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) return SubmitDraftingInput();
    if (!m_DynamicGeometry.pointEntry) break;
    if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
      const int count = dyninput::FieldCount(m_DynamicGroup);
      const int slot = (i + (key->key() == Qt::Key_Backtab ? count - 1 : 1)) % count;
      m_DynamicGroup.focus = slot; m_DynamicFields[slot]->setFocus(); m_DynamicFields[slot]->selectAll();
      return true;
    }
    if (key->key() == Qt::Key_Backspace && !m_DynamicGroup.f[i].locked &&
        dyninput::BackspaceAtEmpty(m_DynamicGroup, i)) { RefreshDraftingInput(); return true; }
  }
  // While a point is being entered the dynamic fields own the keyboard, as AutoCAD's do: not only with the
  // drawing focused, but also from an empty command line (where focus stays after typing the command) and
  // from any other panel that is not itself being typed into. Without this, coordinates and Tab went to
  // the command line and the fields beside the cursor were display-only.
  const bool fieldsOwnKeys = widget == m_Viewport ||
      (m_DynamicGeometry.pointEntry && widget == m_Command && m_Command->text().isEmpty()) ||
      (m_DynamicGeometry.pointEntry && !editing);
  if (fieldsOwnKeys && DynamicInputAcceptsInput() &&
      !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    if (widget == m_Viewport && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
      return SubmitDraftingInput();
    const bool spaceAsEnter = widget != m_Viewport && key->key() == Qt::Key_Space;
    if (key->key() == Qt::Key_Tab || (!spaceAsEnter && !key->text().isEmpty() && key->text()[0].isPrint())) {
      auto* edit = m_DynamicFields[0]; edit->setFocus(); edit->selectAll();
      if (key->key() != Qt::Key_Tab) QApplication::sendEvent(edit, key);
      return true;
    }
  }
  if (!editing && !ctrl && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
                           key->key() == Qt::Key_Space)) {
    Submit(""); return true;
  }
  return false;
}

void EditorWindow::ShowSnapOverride(const QPoint& position) {
  QMenu menu(this); menu.setObjectName("snapOverrideMenu");
  if (palette().color(QPalette::Window).lightness() < 128)
    menu.setStyleSheet(QtApplyAccent("QMenu { background:#252b2f; color:#edf1f3; border:1px solid #454e53; padding:2px; }"
        "QMenu::item { padding:4px 24px; background:transparent; } QMenu::item:selected { background:@tint@; color:#ffffff; }"
        "QMenu::separator { height:1px; background:#454e53; margin:4px 8px; }", true));
  menu.addSection("Snap once — next pick");
  const std::pair<const char*, CadSnap::Kind> kinds[] = {
      {"Endpoint", CadSnap::Kind::Endpoint}, {"Midpoint", CadSnap::Kind::Midpoint},
      {"Center", CadSnap::Kind::Center}, {"Quadrant", CadSnap::Kind::Quadrant},
      {"Station", CadSnap::Kind::Station}, {"Node", CadSnap::Kind::Node},
      {"Perpendicular", CadSnap::Kind::Perpendicular}, {"Survey", CadSnap::Kind::SurveyCenter},
      {"Geometric center", CadSnap::Kind::GeometricCenter}, {"Intersection", CadSnap::Kind::Intersection},
      {"Apparent intersection", CadSnap::Kind::ApparentIntersection}, {"Surface", CadSnap::Kind::Surface},
      {"Solid edge", CadSnap::Kind::Edge}, {"Solid face", CadSnap::Kind::Face}, {"Point cloud", CadSnap::Kind::PointCloud},
      {"Nearest", CadSnap::Kind::Nearest}};
  for (const auto& [label, kind] : kinds) {
    if (kind == CadSnap::Kind::Perpendicular && !CadSnap::CommandHasPerpendicularSnapReference(m_State, true, true)) continue;
    connect(menu.addAction(label), &QAction::triggered, this, [this, kind, label] {
      m_State.objectSnapKindOverrideValid = true;
      m_State.objectSnapKindOverrideKind = static_cast<int>(kind);
      m_Log.push_back(std::string(label) + " snap locked for the next pick.");
    });
  }
  menu.exec(position); Refresh();
}

void EditorWindow::ShowPolarSettings() {
  QDialog dialog(this); dialog.setObjectName("polarSettingsDialog"); dialog.setWindowTitle("Polar tracking angles");
  if (palette().color(QPalette::Window).lightness() < 128)
    dialog.setStyleSheet(QtApplyAccent("QDialog#polarSettingsDialog { background:#22272b; color:#edf1f3; }"
        "QLabel { color:#dce2e5; background:transparent; }"
        "QComboBox, QLineEdit { background:#171b1e; color:#edf1f3; border:1px solid #424b51; border-radius:5px; "
        "padding:5px 8px; selection-background-color:@fill@; }"
        "QComboBox:focus, QLineEdit:focus { border-color:@strong@; }"
        "QPushButton { background:#2a3035; color:#eaf0f2; border:1px solid #41494e; border-radius:5px; padding:6px 10px; }"
        "QPushButton:hover { background:#343c41; border-color:@edge@; }"
        "QPushButton:pressed { background:@tint@; border-color:@strong@; }", true));
  auto* layout = new QVBoxLayout(&dialog);
  auto* summary = new QLabel("Angles follow the active UCS.\nThe pipe compass uses these same presets.", &dialog);
  summary->setObjectName("polarSettingsSummary"); layout->addWidget(summary);
  auto* form = new QFormLayout; layout->addLayout(form);
  auto* increment = new QComboBox(&dialog); increment->setObjectName("polarIncrement");
  for (double value : {90., 45., 30., 22.5, 18., 15., 10., 5.}) increment->addItem(QString::number(value), value);
  int index = increment->findData(m_State.polarIncrementDeg);
  if (index < 0) { increment->addItem(QString::number(m_State.polarIncrementDeg), m_State.polarIncrementDeg); index = increment->count() - 1; }
  increment->setCurrentIndex(index); form->addRow("Increment (degrees)", increment);
  auto* extras = new QLineEdit(&dialog); extras->setObjectName("polarExtraAngles");
  QStringList angles; for (double value : m_State.polarExtraAnglesDeg) angles << QString::number(value, 'g', 17);
  extras->setText(angles.join(", ")); extras->setPlaceholderText("e.g. 12.5, 67.5"); form->addRow("Additional angles", extras);
  auto* error = new QLabel(&dialog); error->setObjectName("polarSettingsError"); error->setWordWrap(true);
  error->setStyleSheet("color:#e67e70"); layout->addWidget(error);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    std::vector<double> values;
    if (!extras->text().trimmed().isEmpty()) for (const auto& token : extras->text().split(',')) {
      bool ok = false; const double value = token.trimmed().toDouble(&ok);
      if (!ok || !std::isfinite(value)) {
        error->setText("Enter finite angles separated by commas."); dialog.adjustSize(); return;
      }
      values.push_back(value);
    }
    const double oldIncrement = m_State.polarIncrementDeg; const auto oldExtras = m_State.polarExtraAnglesDeg;
    m_State.polarIncrementDeg = increment->currentData().toDouble(); m_State.polarExtraAnglesDeg = values;
    if (!SaveUserStartupPrefs(m_State)) {
      m_State.polarIncrementDeg = oldIncrement; m_State.polarExtraAnglesDeg = oldExtras;
      error->setText("Could not save angle preferences. Previous settings are unchanged.");
      dialog.adjustSize(); return;
    }
    dialog.accept();
  });
  dialog.exec(); Refresh();
}

void EditorWindow::ShowContextualHelp(const QString& topicCommand, const QString& uiTooltip) {
  if (!m_WikiLoaded) {
    m_WikiBundle = LoadWikiBundle();
    WikiHelpBuildIndex(m_WikiBundle);
    m_WikiLoaded = true;
  }
  // Topic order: the explicit command, then the hovered control's "Command bar:" name, its ribbon
  // route or UI topic, then (when none of those names a page) the running or last command, then Home.
  WikiHelpTarget uiTopic;
  if (!uiTooltip.isEmpty()) uiTopic = WikiHelpTargetFromUiTooltip(uiTooltip.toUtf8().toStdString());
  std::string primary = HelpCommandPrimary(topicCommand);
  if (primary.empty()) primary = HelpCommandPrimary(QString::fromStdString(uiTopic.commandPrimary));
  const bool uiPage = primary.empty() && !uiTopic.pageSlug.empty() && uiTopic.commandPrimary.empty();
  if (primary.empty() && !uiPage) primary = HelpCommandPrimaryForKind(m_State.active);
  if (primary.empty() && !uiPage) primary = HelpCommandPrimaryForKind(m_State.lastCommand);
  WikiHelpTarget target;
  if (!primary.empty()) target = WikiHelpLookupCommand(primary);
  else if (uiPage) target = uiTopic;
  if (target.pageSlug.empty()) target = WikiHelpTarget{"Home", {}};

  QDialog dialog(this); dialog.setObjectName("contextualHelpDialog");
  dialog.setWindowTitle("GoSurvey User Manual"); dialog.resize(1080, 720);
  auto* layout = new QVBoxLayout(&dialog);
  auto* title = new QLabel("User Manual", &dialog); title->setObjectName("manualTitle");
  const bool darkHelp = palette().color(QPalette::Window).lightness() < 128;
  title->setStyleSheet(QString("font-size:20px; font-weight:600; padding:10px; background:%1; color:#f1f6fa; border-radius:4px;")
                           .arg(darkHelp ? "#30373c" : "#263e53"));
  layout->addWidget(title);
  auto* splitter = new QSplitter(&dialog); splitter->setObjectName("manualSplitter"); layout->addWidget(splitter, 1);
  auto* side = new QWidget(splitter);
  auto* sideLayout = new QVBoxLayout(side); sideLayout->setContentsMargins(0, 0, 0, 0); sideLayout->setSpacing(4);
  auto* search = new QLineEdit(side); search->setObjectName("manualSearch");
  search->setPlaceholderText("Search the manual"); search->setClearButtonEnabled(true);
  auto* pages = new QListWidget(side); pages->setObjectName("manualPages");
  sideLayout->addWidget(search); sideLayout->addWidget(pages, 1);
  auto* browser = new QTextBrowser(splitter); browser->setObjectName("manualContent");
  browser->setOpenLinks(false); browser->setOpenExternalLinks(false);
  splitter->addWidget(side); splitter->addWidget(browser); splitter->setStretchFactor(1, 1);
  splitter->setSizes({260, 820});
  // Shows a page and scrolls to `focus`: for a command, the heading that documents it, else its first
  // whole-word mention in capitals; for a search, the first case-insensitive match.
  auto showPage = [this, browser, title](const QString& slug, const QString& focus = {}, bool command = false) {
    const auto found = m_WikiBundle.pagesBySlug.find(slug.toUtf8().toStdString());
    if (found == m_WikiBundle.pagesBySlug.end()) return;
    title->setText(QString::fromUtf8(found->second.title.c_str()));
    QString markdown = QString::fromUtf8(found->second.markdown.c_str());
    QRegularExpression image("wiki-img:([A-Za-z0-9._-]+)");
    auto match = image.globalMatch(markdown);
    while (match.hasNext()) {
      const auto item = match.next();
      const auto path = std::filesystem::path(m_WikiBundle.rootPath) / "images" / item.captured(1).toStdWString();
      markdown.replace(item.capturedStart(), item.capturedLength(), QUrl::fromLocalFile(QString::fromStdWString(path.wstring())).toString());
    }
    browser->setMarkdown(markdown);
    if (focus.isEmpty()) return;
    QTextDocument* doc = browser->document();
    QTextCursor at;
    if (command) {
      const std::string primaryLower = focus.toLower().toStdString();
      for (QTextBlock block = doc->begin(); block.isValid() && at.isNull(); block = block.next())
        if (block.blockFormat().headingLevel() > 0 && WikiHeadingMatchesCommand(block.text().toStdString(), primaryLower))
          at = QTextCursor(block);
      if (at.isNull()) at = doc->find(focus.toUpper(), 0, QTextDocument::FindCaseSensitively | QTextDocument::FindWholeWords);
    } else {
      at = doc->find(focus);
    }
    if (at.isNull()) return;
    // Jump to the end first so ensureCursorVisible brings the match to the top, not the bottom.
    browser->moveCursor(QTextCursor::End); browser->setTextCursor(at); browser->ensureCursorVisible();
  };
  const auto fillNav = [this, pages] {
    const QSignalBlocker block(pages);
    pages->clear();
    for (const auto& entry : m_WikiBundle.nav) {
      auto* item = new QListWidgetItem(QString::fromUtf8(entry.label.c_str()), pages);
      if (entry.isSection) {
        item->setFlags(Qt::NoItemFlags); item->setForeground(QColor(QtAccentFor(true).quiet));
      } else {
        item->setData(Qt::UserRole, QString::fromUtf8(entry.slug.c_str()));
      }
    }
  };
  // Search covers every page, including the per-command pages the sidebar does not list. Every word
  // must appear somewhere on the page; title hits rank first, then pages that mention the words most.
  const auto fillResults = [this, pages](const QString& query) {
    const QStringList words = query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    struct Hit { long long score; QString title; QString slug; };
    std::vector<Hit> hits;
    for (const auto& [slug, page] : m_WikiBundle.pagesBySlug) {
      const QString pageTitle = QString::fromUtf8(page.title.c_str());
      const QString body = QString::fromUtf8(page.markdown.c_str());
      long long score = 0; bool all = true;
      for (const QString& word : words) {
        const bool inTitle = pageTitle.contains(word, Qt::CaseInsensitive);
        const qsizetype mentions = body.count(word, Qt::CaseInsensitive);
        if (!inTitle && mentions == 0) { all = false; break; }
        score += (inTitle ? 1000 : 0) + std::min<qsizetype>(mentions, 50);
      }
      if (all) hits.push_back({score, pageTitle, QString::fromUtf8(slug.c_str())});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
      return a.score != b.score ? a.score > b.score : a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    const QSignalBlocker block(pages);
    pages->clear();
    if (hits.empty()) {
      auto* none = new QListWidgetItem("No matches", pages); none->setFlags(Qt::NoItemFlags);
      return;
    }
    for (const Hit& hit : hits) {
      auto* item = new QListWidgetItem(hit.title, pages);
      item->setData(Qt::UserRole, hit.slug); item->setToolTip(hit.slug);
    }
  };
  fillNav();
  connect(pages, &QListWidget::currentItemChanged, &dialog, [showPage, search](QListWidgetItem* current) {
    if (!current) return;
    const QString firstWord = search->text().trimmed().section(QRegularExpression("\\s+"), 0, 0);
    showPage(current->data(Qt::UserRole).toString(), firstWord);
  });
  connect(search, &QLineEdit::textChanged, &dialog, [fillNav, fillResults](const QString& text) {
    if (text.trimmed().isEmpty()) fillNav(); else fillResults(text.trimmed());
  });
  connect(search, &QLineEdit::returnPressed, &dialog, [pages] {
    for (int i = 0; i < pages->count(); ++i) if (pages->item(i)->flags() & Qt::ItemIsSelectable) {
      pages->setCurrentRow(i); pages->setFocus(); return;
    }
  });
  connect(browser, &QTextBrowser::anchorClicked, &dialog, [pages, showPage](const QUrl& url) {
    if (url.scheme() != "wiki") { QDesktopServices::openUrl(url); return; }
    const QString slug = url.path().isEmpty() ? url.host() : url.path();
    for (int i = 0; i < pages->count(); ++i) if (pages->item(i)->data(Qt::UserRole).toString() == slug) {
      const QSignalBlocker block(pages); pages->setCurrentRow(i); break;
    }
    showPage(slug);
  });
  const QString initial = QString::fromUtf8(target.pageSlug.c_str());
  for (int i = 0; i < pages->count(); ++i) if (pages->item(i)->data(Qt::UserRole).toString() == initial) {
    const QSignalBlocker block(pages); pages->setCurrentRow(i); break;
  }
  // A command's own page opens at the top; a shared topic page scrolls to the command.
  const QString commandName = QString::fromStdString(target.commandPrimary);
  const bool ownPage = initial.compare("Commands/" + commandName, Qt::CaseInsensitive) == 0;
  showPage(initial, ownPage ? QString() : commandName, true);
  if (!m_WikiBundle.ok) browser->setPlainText(QString::fromUtf8(m_WikiBundle.fallbackMessage.c_str()));
  search->setFocus();
  dialog.exec();
}
