#include "EditorWindow.hpp"
#include "QtAccent.hpp"
#include "EditorViewport.hpp"
#include "EditorChrome.hpp"
#include "CadSnap.hpp"
#include "UserPrefs.hpp"
#include "WikiHelp.hpp"
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
#include <algorithm>
#include <cmath>

void EditorWindow::BuildDraftingInput() {
  const auto mode = [this](const char* name, const char* label, int key, bool AppCommandState::*member) {
    auto* button = new QToolButton(this);
    button->setObjectName(name); button->setText(label); button->setCheckable(true);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    if (dark) button->setStyleSheet(QtApplyAccent("QToolButton { color:#edf1f3; background:#2a3035; border:1px solid #41494e; "
        "border-radius:4px; padding:2px 8px; } QToolButton:hover { background:#343c41; border-color:@edge@; } "
        "QToolButton:checked { color:#ffffff; background:@tint@; border-color:@strong@; }", true));
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
  if (palette().color(QPalette::Window).lightness() < 128) {
    m_DynamicInput->setStyleSheet(QtApplyAccent("QFrame#dynamicInput { background:transparent; border:none; }"
        "QLabel { color:#dce2e5; background:transparent; }"
        "QLineEdit { color:#f1f4f5; background:#30373c; border:0; border-bottom:1px solid #4c575d; "
        "border-radius:0; min-height:24px; padding:2px 4px; selection-background-color:@fill@; }"
        "QLineEdit:focus { border:0; border-bottom:2px solid @strong@; }", true));
  }
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
  m_DynamicInput->hide();
}

void EditorWindow::ResetDraftingInput() {
  m_DynamicReset = true;
  for (auto& field : m_DynamicGroup.f) field = {};
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
  m_DynamicInput->setVisible(active);
  if (!active) return;
  m_DynamicPrompt->setText(m_DynamicHint);
  const int count = prompt.pointEntry ? dyninput::FieldCount(m_DynamicGroup) : 1;
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
  if (!m_DynamicInput || !m_DynamicInput->isVisible()) return false;
  const bool locked = std::any_of(m_DynamicGroup.f.begin(), m_DynamicGroup.f.end(),
      [](const dyninput::Field& f) { return f.locked; });
  if (click && (!m_DynamicGeometry.pointEntry || !locked)) { ResetDraftingInput(); return false; }
  QString text = m_DynamicFields[0]->text();
  if (m_DynamicGeometry.pointEntry) {
    const auto live = CadDynInputLive(m_State, m_DynamicGeometry, m_DynamicGroup.mode,
        m_DynamicCursor.x, m_DynamicCursor.y, m_DynamicCursor.z);
    text = QString::fromStdString(dyninput::Compose(m_DynamicGroup, live, m_DynamicGeometry.directDistance));
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
            ucs::UcsVectorToWorld(m_State.activeUcs, {distance * std::cos(radians), distance * std::sin(radians), 0}));
        const auto point = ucs::WorldToUcs(m_State.activeUcs, world);
        text = QString("%1,%2,%3").arg(point.x, 0, 'g', 17).arg(point.y, 0, 'g', 17).arg(point.z, 0, 'g', 17);
      }
    }
  }
  ResetDraftingInput(); m_Command->clear(); Submit(text);
  return true;
}

void EditorWindow::CancelDraftingInput() {
  m_State.showCreatePointsWindow = false;
  if (m_Viewport->CancelGripEdit()) {}
  else if (m_State.selBoxWaitingSecond) m_Viewport->CancelSelectionBox();
  else if (m_State.sectionPlaneGripDrag >= 0) AbortSectionPlaneGripDrag(m_State);
  else if (m_State.gizmoDragActive && !IsGizmoCommandKind(m_State.active)) CancelGizmoDrag(m_State);
  else if ((m_State.active == AppCommandState::Kind::Line || m_State.active == AppCommandState::Kind::Polyline) &&
           m_State.segmentAnglePickPhase != AppCommandState::SegmentAnglePickPhase::Idle)
    CancelSegmentAnglePick(m_State, &m_Log);
  else if (m_State.active != AppCommandState::Kind::None || m_State.paperLengthenPhase || m_State.paperExtendPhase ||
           m_State.paperBreakPhase || m_State.paperStretchPhase || m_State.paperFilletPhase || m_State.paperChamferPhase)
    CancelActiveCommand(m_State, m_Log);
  else { ClearSelection(m_State); m_State.pendingZoomExtents = m_State.pendingZoomWindow = false; }
  ClearPendingOneShotObjectSnap(m_State);
  ResetDraftingInput(); m_Command->clear(); m_Viewport->InvalidateGeometry(); Refresh();
}

bool EditorWindow::DraftingInputEvent(QObject* watched, QEvent* event) {
  auto* widget = qobject_cast<QWidget*>(watched);
  if (!widget || !(widget == this || isAncestorOf(widget)) || QApplication::activeModalWidget() ||
      QApplication::activePopupWidget()) return false;
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
  const bool editShortcut = ctrl && (key->key() == Qt::Key_Z || key->key() == Qt::Key_Y ||
      key->key() == Qt::Key_C || key->key() == Qt::Key_V);
  if (editShortcut && event->type() == QEvent::ShortcutOverride) {
    key->accept(); return false; // Block QAction activation, then route the following KeyPress once.
  }
  if (!editing && event->type() == QEvent::KeyPress && editShortcut) {
    if (key->key() == Qt::Key_Z && !key->modifiers().testFlag(Qt::ShiftModifier)) DoUndo(m_State, m_Log);
    else if (key->key() == Qt::Key_Z || key->key() == Qt::Key_Y) DoRedo(m_State, m_Log);
    else if (key->key() == Qt::Key_C) CopySelectionToClipboard(m_State, m_Log);
    else if (key->key() == Qt::Key_V) StartPasteCommand(m_State, m_Log);
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
    if (event->type() == QEvent::ShortcutOverride) { key->accept(); return false; }
    CancelDraftingInput(); key->accept(); return true;
  }
  if (event->type() != QEvent::KeyPress) return false;
  if (key->key() == Qt::Key_F1) { ShowContextualHelp(); return true; }
  if (!editing && key->key() == Qt::Key_Delete) {
    StartDeleteCommand(m_State, m_Log); Refresh(); return true;
  }
  if (!editing && m_State.active == AppCommandState::Kind::DimLinear &&
      m_State.dimPhase == AppCommandState::DimPhase::WaitDimLinePt &&
      (key->key() == Qt::Key_H || key->key() == Qt::Key_V) && !ctrl) {
    CadDimLinearApplyHVHotkey(m_State, key->key() == Qt::Key_V, m_Log); Refresh(); return true;
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
  if (widget == m_Viewport && m_DynamicInput->isVisible() &&
      !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) return SubmitDraftingInput();
    if (key->key() == Qt::Key_Tab || (!key->text().isEmpty() && key->text()[0].isPrint())) {
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

void EditorWindow::ShowContextualHelp() {
  if (!m_WikiLoaded) {
    m_WikiBundle = LoadWikiBundle();
    WikiHelpBuildIndex(m_WikiBundle);
    m_WikiLoaded = true;
  }
  QDialog dialog(this); dialog.setObjectName("contextualHelpDialog");
  dialog.setWindowTitle("GoSurvey User Manual"); dialog.resize(1080, 720);
  auto* layout = new QVBoxLayout(&dialog);
  auto* title = new QLabel("User Manual", &dialog); title->setObjectName("manualTitle");
  const bool darkHelp = palette().color(QPalette::Window).lightness() < 128;
  title->setStyleSheet(QString("font-size:20px; font-weight:600; padding:10px; background:%1; color:#f1f6fa; border-radius:4px;")
                           .arg(darkHelp ? "#30373c" : "#263e53"));
  layout->addWidget(title);
  auto* splitter = new QSplitter(&dialog); splitter->setObjectName("manualSplitter"); layout->addWidget(splitter, 1);
  auto* pages = new QListWidget(splitter); pages->setObjectName("manualPages");
  auto* browser = new QTextBrowser(splitter); browser->setObjectName("manualContent");
  browser->setOpenLinks(false); browser->setOpenExternalLinks(false);
  splitter->addWidget(pages); splitter->addWidget(browser); splitter->setStretchFactor(1, 1);
  auto showPage = [this, browser, title](const QString& slug) {
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
  };
  for (const auto& entry : m_WikiBundle.nav) {
    if (entry.isSection) {
      auto* item = new QListWidgetItem(QString::fromUtf8(entry.label.c_str()), pages);
      item->setFlags(Qt::NoItemFlags); item->setForeground(QColor(QtAccentFor(true).quiet));
    } else {
      auto* item = new QListWidgetItem(QString::fromUtf8(entry.label.c_str()), pages);
      item->setData(Qt::UserRole, QString::fromUtf8(entry.slug.c_str()));
    }
  }
  connect(pages, &QListWidget::currentItemChanged, &dialog,
      [showPage](QListWidgetItem* current) { if (current) showPage(current->data(Qt::UserRole).toString()); });
  connect(browser, &QTextBrowser::anchorClicked, &dialog, [this, pages, showPage](const QUrl& url) {
    if (url.scheme() != "wiki") { QDesktopServices::openUrl(url); return; }
    const QString slug = url.path().isEmpty() ? url.host() : url.path();
    for (int i = 0; i < pages->count(); ++i) if (pages->item(i)->data(Qt::UserRole).toString() == slug) {
      pages->setCurrentRow(i); showPage(slug); return;
    }
    showPage(slug);
  });
  QString command;
  if (m_State.active != AppCommandState::Kind::None) command = QString::fromUtf8(AppCommandState::KindName(m_State.active));
  else if (m_State.lastCommand != AppCommandState::Kind::None) command = QString::fromUtf8(AppCommandState::KindName(m_State.lastCommand));
  const auto target = WikiHelpLookupCommand(command.toLower().toUtf8().toStdString());
  const QString initial = target.pageSlug.empty() ? QStringLiteral("Home") : QString::fromUtf8(target.pageSlug.c_str());
  bool selected = false;
  for (int i = 0; i < pages->count(); ++i) if (pages->item(i)->data(Qt::UserRole).toString() == initial) {
    pages->setCurrentRow(i); selected = true; break;
  }
  if (!selected) showPage(initial);
  if (!m_WikiBundle.ok) browser->setPlainText(QString::fromUtf8(m_WikiBundle.fallbackMessage.c_str()));
  dialog.exec();
}
