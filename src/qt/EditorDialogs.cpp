#include "EditorWindow.hpp"
#include "QtAccent.hpp"
#include "EditorViewport.hpp"
#include "EditorPointGroups.hpp"
#include "EditorPointTableDialog.hpp"
#include "EditorLayerManager.hpp"
#include "CadCommands.hpp"
#include "CadColor.hpp"
#include "NonDismissableDialog.hpp"
#include "Version.hpp"
#include "platform/AppPaths.hpp"
#include "update/UpdateService.hpp"
#include "UserPrefs.hpp"
#include "geo/CoordinateSystems.hpp"
#include "util/PlotScales.hpp"
#include "StylePreview.hpp"
#include "StyleEditors.hpp"
#include <QDialog>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDebug>
#include <QAbstractItemView>
#include <QGuiApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QColorDialog>
#include <QPixmap>
#include <QSlider>
#include <QPushButton>
#include <QPalette>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QComboBox>
#include <QProxyStyle>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimer>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QPainter>
#include <functional>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QFrame>
#include <QGroupBox>
#include <QRadioButton>
#include <QScreen>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <limits>
#include <climits>
#include <array>
#include <filesystem>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace {
// One manager for every annotation style kind (dimension, multileader, text): the style list on the left, a
// large drawing of the selected style in the middle, its settings in words below, and the actions on the right.
// It follows the application theme; nothing here hard-codes light colours.
struct StyleManagerSpec {
  QString objectName, title, currentCaption, listObjectName, previewObjectName, buttonPrefix;
  std::function<QStringList()> names;
  std::function<QString()> current;
  std::function<QPixmap(const QString&, QSize, qreal)> preview;
  std::function<QString(const QString&)> describe;
  std::function<void(const QString&)> setCurrent;
  std::function<bool()> create;  ///< prompts for and adds a style; true when the list changed
  std::function<bool(const QString&)> modify;
  std::function<bool(const QString&)> overrideStyle, copy, rename, remove;
  std::function<QMap<QString, QString>(const QString&)> properties;
  std::function<void()> afterChange;
};

void RunStyleManager(QWidget* parent, const StyleManagerSpec& spec) {
  QDialog manager(parent);
  manager.setObjectName(spec.objectName);
  manager.setWindowTitle(spec.title);
  manager.resize(780, 540);
  auto* root = new QVBoxLayout(&manager);
  auto* currentLabel = new QLabel(&manager);
  root->addWidget(currentLabel);
  auto* body = new QHBoxLayout;
  auto* list = new QListWidget(&manager);
  list->setObjectName(spec.listObjectName);
  list->setMinimumWidth(220);
  body->addWidget(list, 1);
  auto* details = new QVBoxLayout;
  auto* preview = new QLabel(&manager);
  preview->setObjectName(spec.previewObjectName);
  preview->setAlignment(Qt::AlignCenter);
  preview->setMinimumSize(400, 220);
  preview->setFrameShape(QFrame::StyledPanel);
  preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  details->addWidget(preview, 1);
  auto* descriptionTitle = new QLabel("Description", &manager);
  QFont bold = descriptionTitle->font();
  bold.setBold(true);
  descriptionTitle->setFont(bold);
  details->addWidget(descriptionTitle);
  auto* description = new QLabel(&manager);
  description->setWordWrap(true);
  description->setMinimumHeight(70);
  description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  details->addWidget(description);
  body->addLayout(details, 2);
  auto* actions = new QVBoxLayout;
  const auto addButton = [&](const QString& text, const QString& suffix) {
    auto* button = new QPushButton(text, &manager);
    button->setObjectName(spec.buttonPrefix + suffix);
    actions->addWidget(button);
    return button;
  };
  auto* setCurrentButton = spec.setCurrent ? addButton("Set Current", "SetCurrent") : nullptr;
  auto* newButton = spec.create ? addButton("New…", "New") : nullptr;
  auto* modifyButton = spec.modify ? addButton("Modify…", "Modify") : nullptr;
  auto* overrideButton = spec.overrideStyle ? addButton("Override…", "Override") : nullptr;
  auto* compareButton = spec.properties ? addButton("Compare…", "Compare") : nullptr;
  auto* copyButton = spec.copy ? addButton("Copy…", "Copy") : nullptr;
  auto* renameButton = spec.rename ? addButton("Rename…", "Rename") : nullptr;
  auto* deleteButton = spec.remove ? addButton("Delete", "Delete") : nullptr;
  actions->addStretch();
  body->addLayout(actions);
  root->addLayout(body, 1);
  auto* bottom = new QHBoxLayout;
  bottom->addStretch();
  auto* close = new QPushButton("Close", &manager);
  bottom->addWidget(close);
  auto* help = new QPushButton("Help", &manager);
  bottom->addWidget(help);
  root->addLayout(bottom);

  const auto refill = [&](const QString& keep) {
    const QSignalBlocker block(list);
    list->clear();
    for (const QString& name : spec.names()) {
      auto* item = new QListWidgetItem(name == spec.current() ? name + QStringLiteral("  (current)") : name, list);
      item->setData(Qt::UserRole, name);
    }
    int row = 0;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == keep) row = i;
    list->setCurrentRow(row);
    currentLabel->setText(spec.currentCaption + spec.current());
  };
  // The list shows "(current)" next to the active style, so read the real name back from the item data.
  const auto realName = [list]() { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString(); };
  const auto showSelectedReal = [&]() {
    const QString name = realName();
    if (name.isEmpty()) {
      preview->clear();
      description->clear();
      return;
    }
    const qreal dpr = std::max<qreal>(2.0, manager.devicePixelRatioF());
    preview->setPixmap(spec.preview(name, QSize(420, 228), dpr));
    description->setText(spec.describe(name));
  };
  refill(spec.current());
  showSelectedReal();
  QObject::connect(list, &QListWidget::currentRowChanged, &manager, [&](int) { showSelectedReal(); });
  if (setCurrentButton)
    QObject::connect(setCurrentButton, &QPushButton::clicked, &manager, [&] {
      const QString name = realName();
      if (name.isEmpty()) return;
      spec.setCurrent(name);
      if (spec.afterChange) spec.afterChange();
      refill(name);
      showSelectedReal();
    });
  if (newButton)
    QObject::connect(newButton, &QPushButton::clicked, &manager, [&] {
      if (!spec.create()) return;
      if (spec.afterChange) spec.afterChange();
      const QStringList all = spec.names();
      refill(all.isEmpty() ? QString() : all.last());
      showSelectedReal();
    });
  if (modifyButton)
    QObject::connect(modifyButton, &QPushButton::clicked, &manager, [&] {
      const QString name = realName();
      if (name.isEmpty() || !spec.modify(name)) return;
      if (spec.afterChange) spec.afterChange();
      refill(name);
      showSelectedReal();
    });
  const auto connectAction = [&](QPushButton* button, const std::function<bool(const QString&)>& action) {
    if (!button) return;
    QObject::connect(button, &QPushButton::clicked, &manager, [&, action] {
      const QString name = realName();
      if (name.isEmpty() || !action(name)) return;
      if (spec.afterChange) spec.afterChange();
      refill(spec.current()); showSelectedReal();
    });
  };
  connectAction(overrideButton, spec.overrideStyle);
  connectAction(copyButton, spec.copy);
  connectAction(renameButton, spec.rename);
  connectAction(deleteButton, spec.remove);
  if (compareButton) QObject::connect(compareButton, &QPushButton::clicked, &manager, [&] {
    QDialog compare(&manager); compare.setWindowTitle("Compare Dimension Styles"); compare.resize(740, 500);
    compare.setObjectName("dimensionStyleCompare");
    auto* layout = new QVBoxLayout(&compare);
    auto* selectors = new QHBoxLayout;
    auto* first = new QComboBox(&compare); auto* second = new QComboBox(&compare);
    first->addItems(spec.names()); second->addItems(spec.names());
    first->setCurrentText(realName()); second->setCurrentText(spec.current());
    selectors->addWidget(new QLabel("Compare:")); selectors->addWidget(first, 1);
    selectors->addWidget(new QLabel("With:")); selectors->addWidget(second, 1); layout->addLayout(selectors);
    auto* values = new QTableWidget(&compare); values->setColumnCount(3);
    values->setHorizontalHeaderLabels({"Setting", "Compare", "With"});
    values->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    values->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(values, 1);
    const auto update = [&] {
      const auto left = spec.properties(first->currentText()), right = spec.properties(second->currentText());
      QMap<QString, QString> keys = left; for (auto it = right.begin(); it != right.end(); ++it) keys.insert(it.key(), it.value());
      values->setRowCount(keys.size()); int row = 0;
      for (auto it = keys.begin(); it != keys.end(); ++it, ++row) {
        const QString a = left.value(it.key()), b = right.value(it.key());
        values->setItem(row, 0, new QTableWidgetItem(it.key()));
        values->setItem(row, 1, new QTableWidgetItem(a)); values->setItem(row, 2, new QTableWidgetItem(b));
        if (a != b) for (int col = 0; col < 3; ++col) {
          auto font = values->item(row, col)->font(); font.setBold(true); values->item(row, col)->setFont(font);
        }
      }
    };
    QObject::connect(first, &QComboBox::currentTextChanged, &compare, update);
    QObject::connect(second, &QComboBox::currentTextChanged, &compare, update);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &compare); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &compare, &QDialog::reject);
    update(); compare.exec();
  });
  QObject::connect(help, &QPushButton::clicked, &manager, [&] {
    QMessageBox::information(&manager, spec.title,
        "Select a drawing-owned style to preview or modify it. Set Current chooses the style for new objects. "
        "Modify updates objects that use that style. New creates a copy of the current style. "
        "Changes can be undone after closing this window.");
  });
  QObject::connect(list, &QListWidget::itemDoubleClicked, &manager, [&](QListWidgetItem*) { if (modifyButton) modifyButton->click(); });
  QObject::connect(close, &QPushButton::clicked, &manager, &QDialog::accept);
  manager.exec();
}

class CoordinateSystemPopupStyle final : public QProxyStyle {
public:
  explicit CoordinateSystemPopupStyle(const QString& baseStyle) : QProxyStyle(baseStyle) {}

  int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override {
    // Menu-style combo popups create invisible hover scrollers at their top and bottom.
    // Use the normal list popup so pointer proximity never changes the current scroll position.
    if (hint == QStyle::SH_ComboBox_Popup) return 0;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }
};

class CoordinateSystemComboBox final : public QComboBox {
public:
  explicit CoordinateSystemComboBox(QWidget* parent) : QComboBox(parent) {
    auto* popupStyle = new CoordinateSystemPopupStyle(QApplication::style()->name());
    popupStyle->setParent(this);
    setStyle(popupStyle);
    setMaxVisibleItems(8);
    // SH_ComboBox_Popup can leave this scrollbar hidden from the style used during base
    // construction, so select a normal viewport scrollbar explicitly for long lists.
    view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view()->setAutoScroll(false);
    view()->setAutoScrollMargin(0);
    view()->setMaximumHeight(220);
  }

  void showPopup() override {
    QComboBox::showPopup();
    QWidget* popup = view()->window();
    QTimer::singleShot(0, this, [this, popup] {
      if (!popup || !popup->isVisible()) return;
      const QPoint topLeft = mapToGlobal(QPoint(0, height()));
      QScreen* screen = QGuiApplication::screenAt(topLeft);
      if (!screen) return;
      const int availableBelow = screen->availableGeometry().bottom() + 1 - topLeft.y();
      const int popupHeight = std::min({popup->height(), 220, availableBelow});
      if (popupHeight <= 0) return;
      popup->setGeometry(topLeft.x(), topLeft.y(), width(), popupHeight);
    });
  }
};
}  // namespace

void EditorWindow::ShowSettings() {
  if (findChild<QDialog *>("applicationSettings")) {
    auto *existing = findChild<QDialog *>("applicationSettings");
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setObjectName("applicationSettings");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Settings");
  dialog->resize(760, 560);
  // REQ-449: a category sidebar over a stack of pages. Controls stage their edit as a "commit"
  // closure; Apply/OK run them all, Cancel runs none.
  auto *outer = new QVBoxLayout(dialog);
  auto *body = new QHBoxLayout();
  outer->addLayout(body, 1);
  auto *sidebar = new QListWidget(dialog);
  sidebar->setObjectName("settingsCategories");
  sidebar->setFixedWidth(170);
  sidebar->setFrameShape(QFrame::NoFrame);
  sidebar->setFocusPolicy(Qt::NoFocus);
  sidebar->setSpacing(2);
  sidebar->setStyleSheet(
      "QListWidget { background:palette(button); border:1px solid palette(mid); border-radius:10px; padding:8px; outline:0; }"
      "QListWidget::item { padding:9px 12px; border-radius:7px; }"
      "QListWidget::item:hover:!selected { background:palette(midlight); }"
      "QListWidget::item:selected { background:palette(highlight); color:palette(highlighted-text); }");
  body->addWidget(sidebar);
  auto *pages = new QStackedWidget(dialog);
  pages->setObjectName("settingsPages");
  body->addWidget(pages, 1);
  auto commits = std::make_shared<std::vector<std::function<void()>>>();
  auto newPage = [&](const char *title, const char *name) {
    auto *page = new QWidget(pages);
    page->setObjectName(name);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(20, 18, 20, 16);
    pageLayout->setSpacing(12);
    pages->addWidget(page);
    sidebar->addItem(title);
    return pageLayout;
  };
  // REQ-077: the update check is switchable, and switching it off means no network request is
  // ever made — not a suppressed prompt. The version is shown here too, so a user reporting a
  // problem has somewhere to read it off.
  QVBoxLayout *layout = newPage("Updates", "settingsPageUpdates");
  auto *updatesHeading = new QLabel("Updates", dialog);
  updatesHeading->setObjectName("updatesSettingsHeading");
  QFont headingFont = updatesHeading->font();
  headingFont.setPointSize(16);
  headingFont.setBold(true);
  updatesHeading->setFont(headingFont);
  layout->addWidget(updatesHeading);
  auto *versionLabel = new QLabel(QString("Version ") + GOSURVEY_VERSION_FULL, dialog);
  versionLabel->setObjectName("updateVersionLabel");
  versionLabel->setStyleSheet("color:#777f89;");
  layout->addWidget(versionLabel);
  auto *checkEnabled = new QCheckBox("Check for updates on startup", dialog);
  checkEnabled->setObjectName("updateCheckEnabled");
  layout->addWidget(checkEnabled);
  auto *useBeta = new QCheckBox("Include beta releases", dialog);
  useBeta->setObjectName("updateUseBeta");
  layout->addWidget(useBeta);
  auto *skippedRow = new QHBoxLayout();
  auto *skippedLabel = new QLabel(dialog);
  skippedLabel->setObjectName("updateSkippedLabel");
  skippedRow->addWidget(skippedLabel);
  // Without this the only way out of a skip is editing the prefs file by hand.
  auto *clearSkip = new QPushButton("Clear", dialog);
  clearSkip->setObjectName("updateClearSkip");
  skippedRow->addWidget(clearSkip);
  skippedRow->addStretch();
  layout->addLayout(skippedRow);
  // REQ-449: staged like every other control. Only Apply/OK write the preferences.
  auto stagedSkip = std::make_shared<std::string>(m_State.updatePrefs.skippedVersion);
  checkEnabled->setChecked(m_State.updatePrefs.enabled);
  useBeta->setChecked(m_State.updatePrefs.useBetaChannel);
  auto syncUpdates = [checkEnabled, useBeta, skippedLabel, clearSkip, stagedSkip] {
    useBeta->setEnabled(checkEnabled->isChecked());
    const bool skipping = !stagedSkip->empty();
    skippedLabel->setText("Skipping " + QString::fromUtf8(stagedSkip->c_str()));
    skippedLabel->setVisible(skipping);
    clearSkip->setVisible(skipping);
  };
  connect(checkEnabled, &QCheckBox::toggled, dialog, [syncUpdates](bool) { syncUpdates(); });
  connect(clearSkip, &QPushButton::clicked, dialog, [stagedSkip, syncUpdates] { stagedSkip->clear(); syncUpdates(); });
  syncUpdates();
  commits->push_back([this, checkEnabled, useBeta, stagedSkip] {
    m_State.updatePrefs.enabled = checkEnabled->isChecked();
    m_State.updatePrefs.useBetaChannel = useBeta->isChecked();
    m_State.updatePrefs.skippedVersion = *stagedSkip;
    m_Update->prefs.skippedVersion = *stagedSkip;
  });
  // A skip chosen in the update dialog while Settings is open must show up here too, unless the
  // user is mid-edit on it here.
  auto *updatesSync = new QTimer(dialog);
  updatesSync->setInterval(250);
  auto lastSeenSkip = std::make_shared<std::string>(m_State.updatePrefs.skippedVersion);
  connect(updatesSync, &QTimer::timeout, dialog, [this, stagedSkip, lastSeenSkip, syncUpdates] {
    if (m_State.updatePrefs.skippedVersion != *lastSeenSkip) {
      *lastSeenSkip = m_State.updatePrefs.skippedVersion;
      *stagedSkip = *lastSeenSkip;
      syncUpdates();
    }
  });
  updatesSync->start();

  layout->addStretch();

  // REQ-424: standalone new drawings use this path; an empty path means the bundled default.
  layout = newPage("Files", "settingsPageFiles");
  auto *newDrawingHeading = new QLabel("New drawings", dialog);
  newDrawingHeading->setObjectName("newDrawingSettingsHeading");
  newDrawingHeading->setStyleSheet("font-size:16px; font-weight:600;");
  layout->addWidget(newDrawingHeading);
  auto *templateForm = new QFormLayout();
  layout->addLayout(templateForm);
  auto *templatePath = new QLineEdit(QString::fromUtf8(m_State.defaultDrawingTemplatePathUtf8), dialog);
  templatePath->setObjectName("defaultDrawingTemplatePath");
  templatePath->setReadOnly(true);
  auto *templatePathRow = new QWidget(dialog);
  auto *templatePathLayout = new QHBoxLayout(templatePathRow);
  templatePathLayout->setContentsMargins(0, 0, 0, 0);
  auto *browseTemplate = new QPushButton("Browse…", templatePathRow);
  browseTemplate->setObjectName("browseDefaultDrawingTemplate");
  auto *useBundledTemplate = new QPushButton("Use GoSurvey default", templatePathRow);
  useBundledTemplate->setObjectName("useBundledDrawingTemplate");
  templatePathLayout->addWidget(templatePath, 1);
  templatePathLayout->addWidget(browseTemplate);
  templatePathLayout->addWidget(useBundledTemplate);
  templateForm->addRow("Default drawing template", templatePathRow);
  auto *templateNote = new QLabel("New standalone drawings use this template. Clear the selection to use GoSurvey’s bundled default.", dialog);
  templateNote->setWordWrap(true);
  layout->addWidget(templateNote);
  connect(browseTemplate, &QPushButton::clicked, dialog, [dialog, templatePath] {
    const QString path = QFileDialog::getOpenFileName(dialog, "Choose default drawing template",
        templatePath->text(), "GoSurvey templates (*.gst)");
    if (!path.isEmpty()) templatePath->setText(path);
  });
  connect(useBundledTemplate, &QPushButton::clicked, dialog, [templatePath] { templatePath->clear(); });
  commits->push_back([this, templatePath] {
    const QByteArray path = templatePath->text().toUtf8();
    CopyUtf8PathCapped(m_State.defaultDrawingTemplatePathUtf8,
                       sizeof(m_State.defaultDrawingTemplatePathUtf8), path.constData());
  });
  layout->addStretch();

  // ---- Display page (REQ-449) ------------------------------------------------------------
  layout = newPage("Display", "settingsPageDisplay");
  auto addHeading = [&](QVBoxLayout *into, const QString &text, const char *name) {
    auto *h = new QLabel(text, dialog);
    h->setObjectName(name);
    h->setFont(headingFont);
    into->addWidget(h);
  };
  addHeading(layout, "Display", "displaySettingsHeading");
  auto *displayForm = new QFormLayout();
  layout->addLayout(displayForm);
  // Each helper builds a control from the current preference and registers the closure that writes
  // it back on Apply/OK. Values are range-checked by the control, mirroring the load-time clamps.
  auto intField = [&](QFormLayout *form, const char *name, const QString &title, int *value, int lo, int hi, const QString &suffix = {}) {
    auto *field = new QSpinBox(dialog);
    field->setObjectName(name); field->setRange(lo, hi); field->setValue(*value); field->setSuffix(suffix);
    field->setKeyboardTracking(false);
    form->addRow(title, field);
    commits->push_back([field, value] { *value = field->value(); });
    return field;
  };
  auto floatField = [&](QFormLayout *form, const char *name, const QString &title, float *value, double lo, double hi, double step, int decimals, const QString &suffix = {}) {
    auto *field = new QDoubleSpinBox(dialog);
    field->setObjectName(name); field->setRange(lo, hi); field->setDecimals(decimals);
    field->setSingleStep(step); field->setSuffix(suffix); field->setValue(static_cast<double>(*value));
    field->setKeyboardTracking(false);
    form->addRow(title, field);
    commits->push_back([field, value] { *value = static_cast<float>(field->value()); });
    return field;
  };
  intField(displayForm, "settingsArcSmoothness", "Arc and circle smoothness", &m_State.displayArcCircleSmoothness, 8, 20000, " segments");
  floatField(displayForm, "settingsWheelZoomFactor", "Mouse wheel zoom step", &m_State.displayWheelZoomFactor, 1.01, 3.0, 0.01, 2, " x");
  intField(displayForm, "settingsLinearPrecision", "Linear precision", &m_State.displayLinearPrecision, 0, 12, " decimals");
  auto *angleType = new QComboBox(dialog);
  angleType->setObjectName("settingsAngleType");
  angleType->addItems({"Decimal degrees", "Degrees / minutes / seconds", "Surveyor units"});
  angleType->setCurrentIndex(m_State.angleDisplayType);
  displayForm->addRow("Angle format", angleType);
  commits->push_back([this, angleType] { m_State.angleDisplayType = angleType->currentIndex(); });
  intField(displayForm, "settingsAnglePrecision", "Angle precision", &m_State.angleDisplayPrecision, 0, 6, " decimals");
  auto *clockwise = new QCheckBox("Angles run clockwise", dialog);
  clockwise->setObjectName("settingsAngleClockwise");
  clockwise->setChecked(m_State.angleDisplayClockwise);
  displayForm->addRow(clockwise);
  commits->push_back([this, clockwise] { m_State.angleDisplayClockwise = clockwise->isChecked(); });
  auto *angleBase = new QDoubleSpinBox(dialog);
  angleBase->setObjectName("settingsAngleBase");
  angleBase->setRange(0.0, 360.0); angleBase->setDecimals(4); angleBase->setSuffix(" deg");
  angleBase->setValue(m_State.angleDisplayBaseDeg);
  displayForm->addRow("Zero direction (clockwise from north)", angleBase);
  commits->push_back([this, angleBase] { m_State.angleDisplayBaseDeg = angleBase->value(); });
  // REQ-450: viewport cursor. Colour, arm length and pickbox size preview live in the viewport and
  // are put back by Cancel; the snap aperture is staged like every other control here.
  addHeading(layout, "Cursor", "displayCursorHeading");
  auto *cursorForm = new QFormLayout();
  layout->addLayout(cursorForm);
  struct CursorLook { float r, g, b, pickHalfPx; int sizePct; };
  const auto readCursorLook = [this] {
    return CursorLook{m_State.viewportCrosshairR, m_State.viewportCrosshairG, m_State.viewportCrosshairB,
                      m_State.viewportCrosshairPickHalfPxX, m_State.displayCrosshairSizePct};
  };
  auto savedLook = std::make_shared<CursorLook>(readCursorLook());
  auto *colorButton = new QPushButton(dialog);
  colorButton->setObjectName("settingsCrosshairColor");
  auto paintSwatch = [this, colorButton] {
    const QColor color = QColor::fromRgbF(m_State.viewportCrosshairR, m_State.viewportCrosshairG, m_State.viewportCrosshairB);
    QPixmap swatch(28, 16);
    swatch.fill(color);
    colorButton->setIcon(QIcon(swatch));
    colorButton->setIconSize(swatch.size());
    colorButton->setText(color.name().toUpper());
  };
  paintSwatch();
  cursorForm->addRow("Crosshair color", colorButton);
  connect(colorButton, &QPushButton::clicked, dialog, [this, dialog, paintSwatch] {
    const QColor start = QColor::fromRgbF(m_State.viewportCrosshairR, m_State.viewportCrosshairG, m_State.viewportCrosshairB);
    const QColor picked = QColorDialog::getColor(start, dialog, "Crosshair color");
    if (!picked.isValid()) return;
    m_State.viewportCrosshairR = static_cast<float>(picked.redF());
    m_State.viewportCrosshairG = static_cast<float>(picked.greenF());
    m_State.viewportCrosshairB = static_cast<float>(picked.blueF());
    paintSwatch();
    m_Viewport->update();
  });
  auto *armLength = new QSlider(Qt::Horizontal, dialog);
  armLength->setObjectName("settingsCrosshairSize");
  armLength->setRange(1, 100); armLength->setValue(m_State.displayCrosshairSizePct);
  auto *armText = new QLabel(QString("%1%").arg(armLength->value()), dialog);
  armText->setObjectName("settingsCrosshairSizeText");
  armText->setMinimumWidth(40);
  auto *armRow = new QHBoxLayout();
  armRow->addWidget(armLength, 1);
  armRow->addWidget(armText);
  cursorForm->addRow("Crosshair arm length (% of viewport)", armRow);
  connect(armLength, &QSlider::valueChanged, dialog, [this, armText](int value) {
    armText->setText(QString("%1%").arg(value));
    m_State.displayCrosshairSizePct = value;
    m_Viewport->update();
  });
  auto *pickbox = new QSpinBox(dialog);
  pickbox->setObjectName("settingsPickboxSize");
  pickbox->setRange(2, 20); pickbox->setValue(static_cast<int>(std::lround(m_State.viewportCrosshairPickHalfPxX)));
  pickbox->setSuffix(" px"); pickbox->setKeyboardTracking(false);
  cursorForm->addRow("Pickbox size (half-width)", pickbox);
  connect(pickbox, &QSpinBox::valueChanged, dialog, [this](int value) {
    m_State.viewportCrosshairPickHalfPxX = m_State.viewportCrosshairPickHalfPxY = static_cast<float>(value);
    m_Viewport->update();
  });
  floatField(cursorForm, "settingsSnapAperture", "Snap aperture", &m_State.objectSnapAperturePx, 4, 64, 1, 0, " px");
  // Apply/OK keep what is on screen; Cancel (or closing the window) restores the last saved look.
  commits->push_back([savedLook, readCursorLook] { *savedLook = readCursorLook(); });
  connect(dialog, &QDialog::rejected, this, [this, savedLook] {
    m_State.viewportCrosshairR = savedLook->r; m_State.viewportCrosshairG = savedLook->g;
    m_State.viewportCrosshairB = savedLook->b;
    m_State.viewportCrosshairPickHalfPxX = m_State.viewportCrosshairPickHalfPxY = savedLook->pickHalfPx;
    m_State.displayCrosshairSizePct = savedLook->sizePct;
    m_Viewport->update();
  });
  addHeading(layout, "Text and dimension sizes on screen", "displaySizesHeading");
  auto *sizeForm = new QFormLayout();
  layout->addLayout(sizeForm);
  floatField(sizeForm, "settingsTextMinPx", "Text, smallest", &m_State.viewportTextMinPx, 4, 48, 1, 0, " px");
  floatField(sizeForm, "settingsTextMaxPx", "Text, largest", &m_State.viewportTextMaxPx, 24, 320, 1, 0, " px");
  floatField(sizeForm, "settingsMtextMinPx", "MTEXT, smallest", &m_State.viewportMtextMinPx, 4, 48, 1, 0, " px");
  floatField(sizeForm, "settingsMtextMaxPx", "MTEXT, largest", &m_State.viewportMtextMaxPx, 24, 320, 1, 0, " px");
  floatField(sizeForm, "settingsDimArrowScale", "Dimension arrow size", &m_State.viewportDimArrowScale, 0.2, 4.0, 0.1, 2, " x");
  layout->addStretch();

  // ---- Drafting page (REQ-449) ---------------------------------------------------------
  layout = newPage("Drafting", "settingsPageDrafting");
  addHeading(layout, "Drafting", "draftingSettingsHeading");
  auto *draftForm = new QFormLayout();
  layout->addLayout(draftForm);
  intField(draftForm, "settingsIsolines", "Solid ISOLINES", &m_State.viewportSolidIsolines, 0, kSolidMaxIsolines);
  intField(draftForm, "settingsUndoHistory", "Undo steps kept per drawing", &m_State.undoHistoryMaxSize, 1, 200);
  auto *trimState = new QComboBox(dialog);
  trimState->setObjectName("settingsTrimState");
  trimState->addItems({"Draw a line to trim", "Pick cutting edges first"});
  trimState->setCurrentIndex(m_State.trimState);
  draftForm->addRow("TRIM behaviour", trimState);
  commits->push_back([this, trimState] { m_State.trimState = trimState->currentIndex(); });
  floatField(draftForm, "settingsFilletRadius", "FILLET radius", &m_State.filletRadius, 0, 1.0e9, 0.1, 4);
  auto *cornerTrim = new QComboBox(dialog);
  cornerTrim->setObjectName("settingsCornerTrimMode");
  cornerTrim->addItems({"Do not trim corners", "Trim corners"});
  cornerTrim->setCurrentIndex(m_State.cornerTrimMode);
  draftForm->addRow("FILLET / CHAMFER trimming", cornerTrim);
  commits->push_back([this, cornerTrim] { m_State.cornerTrimMode = cornerTrim->currentIndex(); });
  floatField(draftForm, "settingsChamferDist1", "CHAMFER first distance", &m_State.chamferDist1, 0, 1.0e9, 0.1, 4);
  floatField(draftForm, "settingsChamferDist2", "CHAMFER second distance", &m_State.chamferDist2, 0, 1.0e9, 0.1, 4);
  floatField(draftForm, "settingsChamferAngle", "CHAMFER angle", &m_State.chamferAngle, 0.01, 179.99, 1, 2, " deg");
  auto *chamferMode = new QComboBox(dialog);
  chamferMode->setObjectName("settingsChamferMode");
  chamferMode->addItems({"Distance and distance", "Distance and angle"});
  chamferMode->setCurrentIndex(m_State.chamferMode);
  draftForm->addRow("CHAMFER method", chamferMode);
  commits->push_back([this, chamferMode] { m_State.chamferMode = chamferMode->currentIndex(); });
  // Focused pages that already own their controls; reached from here so nothing is duplicated.
  auto *polarButton = new QPushButton("Polar tracking…", dialog);
  polarButton->setObjectName("settingsPolarTracking");
  connect(polarButton, &QPushButton::clicked, this, [this] { ShowPolarSettings(); });
  layout->addWidget(polarButton, 0, Qt::AlignLeft);
  // REQ-084: the right-click preferences live in their own dialog, reached from here as in 0.7.4.
  auto *rightClickButton = new QPushButton("Right-click Customization...", dialog);
  rightClickButton->setObjectName("settingsRightClickCustomization");
  connect(rightClickButton, &QPushButton::clicked, this, [this] { ShowRightClickCustomization(); });
  layout->addWidget(rightClickButton, 0, Qt::AlignLeft);
  layout->addStretch();

  // ---- Survey Points page (REQ-449) ----------------------------------------------------
  layout = newPage("Survey Points", "settingsPageSurveyPoints");
  addHeading(layout, "Survey Points", "surveyPointsSettingsHeading");
  auto *surveyNote = new QLabel("How survey points and their labels look in the drawing.", dialog);
  surveyNote->setWordWrap(true);
  layout->addWidget(surveyNote);
  auto *surveyDisplay = new QPushButton("Survey point display…", dialog);
  surveyDisplay->setObjectName("surveyPointDisplaySettings");
  layout->addWidget(surveyDisplay, 0, Qt::AlignLeft);
  connect(surveyDisplay, &QPushButton::clicked, this, &EditorWindow::ShowSurveyPointDisplay);
  layout->addStretch();

  auto saveStatus = new QLabel(dialog);
  saveStatus->setObjectName("settingsSaveStatus");
  saveStatus->setWordWrap(true);
  saveStatus->setStyleSheet("color:#c54646;");
  outer->addWidget(saveStatus);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, dialog);
  buttons->button(QDialogButtonBox::Ok)->setObjectName("settingsOk");
  buttons->button(QDialogButtonBox::Apply)->setObjectName("settingsApply");
  buttons->button(QDialogButtonBox::Cancel)->setObjectName("settingsCancel");
  outer->addWidget(buttons);
  // REQ-201: a failed write is shown and keeps the window open, never a silent "saved".
  auto applyAll = [this, commits, saveStatus, templatePath] {
    const auto path = templatePath->text().toUtf8();
    if (path.size() > static_cast<int>(sizeof(m_State.defaultDrawingTemplatePathUtf8) - 1)) {
      saveStatus->setText("The default drawing-template path is too long. Choose a shorter path; "
                          "the current setting was kept.");
      return false;
    }
    for (const auto &commit : *commits) commit();
    BumpCadGpuCache(m_State);
    Refresh();
    if (!SaveUserStartupPrefs(m_State)) {
      saveStatus->setText("Your settings could not be saved. Check folder permissions and try again. They stay active until GoSurvey closes.");
      return false;
    }
    saveStatus->clear();
    return true;
  };
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, dialog, [applyAll] { applyAll(); });
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, dialog, [applyAll, dialog] { if (applyAll()) dialog->accept(); });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(sidebar, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
  sidebar->setCurrentRow(0);
  dialog->show();
}

void EditorWindow::ShowSurveyPointDisplay() {
  if (auto* existing = findChild<QDialog*>("surveyPointDisplay")) {
    existing->raise(); existing->activateWindow(); return;
  }
  auto* dialog = new QDialog(this);
  dialog->setObjectName("surveyPointDisplay");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Survey point display");
  dialog->resize(580, 520);
  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(20, 18, 20, 16);
  layout->setSpacing(14);
  auto* heading = new QLabel("Survey point display", dialog);
  heading->setStyleSheet(QString("font-size:20px; font-weight:600; color:%1;")
                             .arg(QtAccentFor(palette().color(QPalette::Window).lightness() < 128).link));
  layout->addWidget(heading);
  auto* note = new QLabel("Changes appear immediately. Close keeps your changes.\n"
                         "Save Preferences remembers them for your next session.", dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* groups = new QBoxLayout(QBoxLayout::TopToBottom);
  groups->setObjectName("surveyDisplayGroups");
  groups->setSpacing(14);
  layout->addLayout(groups);
  auto* markers = new QGroupBox("Point markers", dialog);
  auto* labels = new QGroupBox("Linked labels", dialog);
  groups->addWidget(markers); groups->addWidget(labels);
  auto* markerForm = new QFormLayout(markers);
  auto* labelForm = new QFormLayout(labels);
  markerForm->setVerticalSpacing(12); labelForm->setVerticalSpacing(12);
  auto updateLabels = [this](bool reposition) {
    for (std::size_t i = 0; i < m_State.surveyPoints.size(); ++i) {
      if (reposition) RepositionSurveyLabelMtextForPoint(m_State, i);
      else EnsureSurveyPointLabelMtext(m_State, i, nullptr);
    }
    BumpCadGpuCache(m_State);
    Refresh();
  };
  // Store a pointer to the state member, never a point/annotation: drawings can change
  // while this modeless preferences window is open.
  auto number = [this, dialog, updateLabels](QFormLayout* form, const char* name,
      const char* title, float* value, double low, double high, double step,
      const char* suffix, int effect) {
    auto* field = new QDoubleSpinBox(dialog);
    field->setObjectName(name); field->setRange(low, high);
    field->setDecimals(3); field->setSingleStep(step); field->setSuffix(suffix);
    field->setKeyboardTracking(false); field->setValue(*value);
    form->addRow(title, field);
    connect(field, &QDoubleSpinBox::valueChanged, dialog, [this, value, effect, updateLabels](double chosen) {
      *value = static_cast<float>(chosen);
      if (effect) updateLabels(effect == 2);
      else { BumpCadGpuCache(m_State); Refresh(); }
    });
  };
  number(markerForm, "surveyCrossSpan", "Cross span on paper", &m_State.surveyPointCrossSpanPlottedInches,
         .02, 2, .002, " in", 0);
  auto* showId = new QCheckBox("Show point ID in viewport", markers);
  showId->setObjectName("surveyShowId"); showId->setChecked(m_State.surveyPointShowIdInViewport);
  markerForm->addRow(showId);
  connect(showId, &QCheckBox::toggled, dialog, [this](bool checked) {
    m_State.surveyPointShowIdInViewport = checked; Refresh();
  });
  auto* precision = new QSpinBox(labels);
  precision->setObjectName("surveyPrecision"); precision->setRange(0, 12);
  precision->setValue(m_State.surveyPointDisplayPrecision);
  precision->setSuffix(" decimals"); labelForm->addRow("Coordinate precision", precision);
  connect(precision, &QSpinBox::valueChanged, dialog, [this, updateLabels](int chosen) {
    m_State.surveyPointDisplayPrecision = chosen; updateLabels(false);
  });
  number(labelForm, "surveyLabelHeight", "Text height on paper", &m_State.surveyPointLabelPlottedHeightInches,
         .04, .5, .001, " in", 1);
  number(labelForm, "surveyLabelEast", "Left edge east of point", &m_State.surveyLabelOffsetEastPlottedIn,
         -2, 4, .002, " in", 2);
  number(labelForm, "surveyLabelNorth", "Centre north of point", &m_State.surveyLabelOffsetNorthPlottedIn,
         -2, 4, .002, " in", 2);
  number(labelForm, "surveyLeaderWidth", "Leader arrow half-width", &m_State.surveyLabelLeaderArrowPx,
         2, 30, .1, " px", 0);
  auto* hint = new QLabel("Paper sizes follow the drawing scale. Leaders appear when a label is moved away from its point.", dialog);
  hint->setWordWrap(true); layout->addWidget(hint);
  auto* status = new QLabel(dialog);
  status->setObjectName("surveyDisplaySaveStatus"); status->setWordWrap(true);
  layout->addWidget(status);
  layout->addStretch();
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  auto* save = buttons->addButton("Save Preferences", QDialogButtonBox::ActionRole);
  save->setObjectName("surveyDisplaySave");
  connect(save, &QPushButton::clicked, dialog, [this, status, dialog] {
    const bool saved = SaveUserStartupPrefs(m_State);
    status->setText(saved ? "Preferences saved." :
        "Preferences could not be saved. Check folder permissions and try again. Your current display changes are still active.");
    status->setStyleSheet(saved ? "color:#8fca9a;" : "color:#f0ad79;");
    status->setMinimumHeight(status->heightForWidth(status->width()));
    dialog->resize(dialog->width(), std::max(dialog->height(), dialog->sizeHint().height()));
    dialog->layout()->activate();
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog->show();
}

void EditorWindow::ShowFeatureLineElevations() {
  if (auto* prior = findChild<QDialog*>("featureLineElevations")) {
    prior->close();
    delete prior;
  }
  auto* dialog = new QDialog(this);
  dialog->setObjectName("featureLineElevations");
  dialog->setWindowTitle("Feature Line Elevations");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
  auto* layout = new QVBoxLayout(dialog);
  auto* heading = new QLabel("Edit elevations and grades", dialog);
  QFont bold = heading->font(); bold.setBold(true); heading->setFont(bold);
  auto* help = new QLabel("Station and length are derived from plan geometry. Elevation and grade edits change the line; grading outputs update only when explicitly rebuilt.", dialog);
  help->setWordWrap(true);
  layout->addWidget(heading); layout->addWidget(help);
  auto* feature = new QComboBox(dialog); feature->setObjectName("featureLineElevationsFeature");
  EnsureEntityIds(m_State);
  auto refreshFeatureLines = [this, feature](std::uint64_t preferredId) {
    EnsureEntityIds(m_State);
    const QSignalBlocker blocker(feature);
    feature->clear();
    for (size_t i = 0; i + 1 < m_State.featureLineOffsets.size(); ++i) {
      const QString label = i < m_State.featureLineInfo.size() && !m_State.featureLineInfo[i].name.empty()
          ? QString::fromUtf8(m_State.featureLineInfo[i].name.c_str()) : QString("Feature line %1").arg(i + 1);
      feature->addItem(label, QVariant::fromValue<qulonglong>(m_State.featureLineAttrs[i].id));
    }
    feature->setCurrentIndex(feature->findData(QVariant::fromValue<qulonglong>(preferredId)));
  };
  const std::uint64_t preferredFeatureId = m_State.featureLineElevIndex >= 0 &&
      static_cast<size_t>(m_State.featureLineElevIndex) < m_State.featureLineAttrs.size()
      ? m_State.featureLineAttrs[static_cast<size_t>(m_State.featureLineElevIndex)].id : 0;
  refreshFeatureLines(preferredFeatureId);
  layout->addWidget(feature);
  auto* table = new QTableWidget(dialog); table->setObjectName("featureLineElevationsTable");
  table->setColumnCount(5); table->setHorizontalHeaderLabels({"Station", "Elevation", "Length ahead", "Grade back (%)", "Grade ahead (%)"});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  layout->addWidget(table, 1);
  auto* actions = new QHBoxLayout;
  auto* raise = new QPushButton("Raise / lower…", dialog); raise->setObjectName("featureLineElevRaise");
  auto* insert = new QPushButton("Insert elevation point…", dialog); insert->setObjectName("featureLineElevInsert");
  auto* remove = new QPushButton("Delete elevation point", dialog); remove->setObjectName("featureLineElevDelete");
  actions->addWidget(raise); actions->addWidget(insert); actions->addWidget(remove); layout->addLayout(actions);
  auto* status = new QLabel(dialog); status->setObjectName("featureLineElevStatus"); status->setWordWrap(true); layout->addWidget(status);

  auto fill = [this, feature, table, status]() {
    table->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
    table->setProperty("cadGpuRevision", m_State.cadGpuRevision);
    const int fi = FindEntityIndexById(m_State.featureLineAttrs, feature->currentData().toULongLong());
    std::vector<FeatureLineElevRow> rows;
    if (!BuildFeatureLineElevTable(m_State, fi, &rows)) { table->setRowCount(0); status->setText("This feature line needs at least two points."); return; }
    const QSignalBlocker blockTable(table);
    table->setRowCount(static_cast<int>(rows.size()));
    for (size_t r = 0; r < rows.size(); ++r) {
      const auto& row = rows[r];
      const QStringList values{QString::number(row.station, 'f', 3), QString::number(row.elevation, 'f', 3),
          QString::number(row.lengthAhead, 'f', 3), std::isnan(row.gradeBackPct) ? QString("—") : QString::number(row.gradeBackPct, 'f', 3),
          std::isnan(row.gradeAheadPct) ? QString("—") : QString::number(row.gradeAheadPct, 'f', 3)};
      for (int c = 0; c < 5; ++c) {
        auto* item = new QTableWidgetItem(values[c]);
        item->setData(Qt::UserRole, row.vertexIndex + 1);
        if (c == 0 || c == 2 || (c == 3 && std::isnan(row.gradeBackPct)) || (c == 4 && std::isnan(row.gradeAheadPct)))
          item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(static_cast<int>(r), c, item);
      }
    }
    status->setText(QString("Feature line %1: %2 elevation rows.").arg(fi + 1).arg(rows.size()));
  };
  auto ensureCurrent = [this, dialog, feature, table, status, fill, refreshFeatureLines]() {
    const bool sameDocument = dialog->property("documentGeneration").toULongLong() == m_DocumentGeneration;
    if (sameDocument && table->property("cadGpuRevision").toUInt() == m_State.cadGpuRevision) return true;
    const std::uint64_t preferredId = sameDocument ? feature->currentData().toULongLong() : 0;
    refreshFeatureLines(preferredId);
    dialog->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
    fill();
    status->setText("Drawing changed. Elevation rows were refreshed; review the selected line before editing.");
    return false;
  };
  QObject::connect(feature, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [this, feature, fill](int i) {
    if (i >= 0) {
      m_State.featureLineElevIndex = FindEntityIndexById(m_State.featureLineAttrs, feature->currentData().toULongLong());
      fill();
    }
  });
  QObject::connect(table, &QTableWidget::itemChanged, dialog, [this, table, status, fill, ensureCurrent](QTableWidgetItem* item) {
    if (!item || item->column() == 0 || item->column() == 2) return;
    if (!ensureCurrent()) return;
    bool ok = false; const double value = item->text().toDouble(&ok);
    const int featureIndex = FindEntityIndexById(m_State.featureLineAttrs,
        table->parentWidget()->findChild<QComboBox*>("featureLineElevationsFeature")->currentData().toULongLong());
    if (featureIndex < 0) { status->setText("Select a feature line before editing."); return; }
    const int fl = featureIndex + 1;
    const int point = item->data(Qt::UserRole).toInt();
    bool applied = false;
    if (ok && std::isfinite(value) && value >= std::numeric_limits<float>::lowest() &&
        value <= std::numeric_limits<float>::max()) {
      if (item->column() == 1) applied = SetFeatureLinePointElevation(m_State, fl, point, static_cast<float>(value), m_Log);
      else if (item->column() == 3) applied = SetFeatureLineGradeBack(m_State, fl, point, value, m_Log);
      else if (item->column() == 4) applied = SetFeatureLineGradeAhead(m_State, fl, point, value, m_Log);
    }
    const QString message = applied ? "Elevation edit applied." :
        (ok && std::isfinite(value) && (value < std::numeric_limits<float>::lowest() || value > std::numeric_limits<float>::max())
          ? "Elevation is outside the supported range." :
            (m_Log.empty() ? "Invalid elevation or grade." : QString::fromUtf8(m_Log.back().c_str())));
    if (applied) Refresh();
    fill();
    status->setText(message);
  });
  QObject::connect(raise, &QPushButton::clicked, dialog, [this, feature, status, fill, ensureCurrent] {
    if (!ensureCurrent()) return;
    bool ok = false; const double delta = QInputDialog::getDouble(this, "Raise / lower elevations", "Elevation change", 0.0, -1.0e9, 1.0e9, 3, &ok);
    if (!ok || !ensureCurrent()) return;
    const int featureIndex = FindEntityIndexById(m_State.featureLineAttrs, feature->currentData().toULongLong());
    const bool changed = featureIndex >= 0 && RaiseFeatureLineElevations(m_State, featureIndex + 1, static_cast<float>(delta), m_Log);
    if (changed) Refresh();
    fill();
    status->setText(changed ? "Feature line elevations adjusted." : (m_Log.empty() ? "No change." : QString::fromUtf8(m_Log.back().c_str())));
  });
  QObject::connect(insert, &QPushButton::clicked, dialog, [this, feature, status, fill, ensureCurrent] {
    if (!ensureCurrent()) return;
    bool stationOk = false, elevationOk = false;
    const double station = QInputDialog::getDouble(this, "Insert elevation point", "Station", 0.0, 0.0, 1.0e12, 3, &stationOk);
    if (!stationOk) return;
    const double elevation = QInputDialog::getDouble(this, "Insert elevation point", "Elevation", 0.0, -1.0e9, 1.0e9, 3, &elevationOk);
    if (!elevationOk || !ensureCurrent()) return;
    const int featureIndex = FindEntityIndexById(m_State.featureLineAttrs, feature->currentData().toULongLong());
    const bool changed = featureIndex >= 0 && std::isfinite(elevation) &&
        elevation >= std::numeric_limits<float>::lowest() && elevation <= std::numeric_limits<float>::max() &&
        InsertFeatureLineElevationPoint(m_State, featureIndex + 1, station, static_cast<float>(elevation), m_Log);
    if (changed) Refresh();
    fill();
    status->setText(changed ? "Elevation point inserted." :
        (elevation < std::numeric_limits<float>::lowest() || elevation > std::numeric_limits<float>::max()
          ? "Elevation is outside the supported range." : (m_Log.empty() ? "Could not insert elevation point." : QString::fromUtf8(m_Log.back().c_str()))));
  });
  QObject::connect(remove, &QPushButton::clicked, dialog, [this, feature, table, status, fill, ensureCurrent] {
    if (!ensureCurrent()) return;
    const int row = table->currentRow();
    if (row < 0 || !table->item(row, 1)) { status->setText("Select an elevation point to delete."); return; }
    const int point = table->item(row, 1)->data(Qt::UserRole).toInt();
    const int featureIndex = FindEntityIndexById(m_State.featureLineAttrs, feature->currentData().toULongLong());
    const bool changed = featureIndex >= 0 && DeleteFeatureLineElevationPoint(m_State, featureIndex + 1, point, m_Log);
    if (changed) Refresh();
    fill();
    status->setText(changed ? "Elevation point deleted." : (m_Log.empty() ? "Only elevation points can be deleted here." : QString::fromUtf8(m_Log.back().c_str())));
  });
  fill(); dialog->resize(900, 520); dialog->show();
}

void EditorWindow::ShowAlignResultsDialog() {
  if (!m_State.showAlignResultsWindow)
    return;
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("alignResultsDialog");
  dialog.setWindowTitle("ALIGN — Helmert transformation");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.resize(680, 520);
  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(16, 14, 16, 12);
  layout->setSpacing(10);
  auto* summary = new QLabel(&dialog);
  summary->setObjectName("alignResultsSummary");
  summary->setWordWrap(true);
  summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(summary);
  auto* table = new QTableWidget(&dialog);
  table->setObjectName("alignResultsPairs");
  table->setColumnCount(7);
  table->setHorizontalHeaderLabels({"", "Pair", "Src X", "Src Y", "Dst X", "Dst Y", "Resid"});
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  table->setColumnWidth(0, 34);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setStretchLastSection(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table, 1);
  auto* applyScale = new QCheckBox("Apply scale", &dialog);
  applyScale->setObjectName("alignResultsApplyScale");
  applyScale->setChecked(true);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* apply = buttons->addButton("Apply", QDialogButtonBox::AcceptRole);
  apply->setObjectName("alignResultsApply");
  auto* close = buttons->addButton("Close", QDialogButtonBox::RejectRole);
  close->setObjectName("alignResultsClose");
  auto* buttonRow = new QHBoxLayout;
  buttonRow->addWidget(applyScale);
  buttonRow->addStretch();
  buttonRow->addWidget(buttons);
  layout->addLayout(buttonRow);

  const int linearPrecision = m_State.displayLinearPrecision;
  auto formatLinear = [linearPrecision](double value) {
    return QString::number(value, 'f', linearPrecision);
  };
  std::function<void()> refreshUi;
  refreshUi = [&] {
    RecalcAlignResult(m_State);
    const auto& res = m_State.alignLastResult;
    if (res.valid) {
      summary->setText(QString(
          "Pairs: %1<br>Scale: %2<br>Rotation: %3<br>Translation: X = %4 &nbsp;&nbsp; Y = %5<br>Point error (RMS): %6")
                           .arg(res.nPairs)
                           .arg(QString::number(res.scale, 'f', 8))
                           .arg(QString::fromUtf8(CadFormatBearingCwNorthDegMinSec(res.rotationCwNorthDeg).c_str()))
                           .arg(formatLinear(res.tx))
                           .arg(formatLinear(res.ty))
                           .arg(formatLinear(res.rms)));
      apply->setEnabled(true);
    } else if (m_State.alignControlPts.empty()) {
      summary->setText("No pairs — add control pairs and solve again.");
      apply->setEnabled(false);
    } else {
      summary->setText("Degenerate — pairs are coincident or collinear.");
      apply->setEnabled(false);
    }
    table->setRowCount(0);
    table->setRowCount(static_cast<int>(m_State.alignControlPts.size()));
    for (int i = 0; i < static_cast<int>(m_State.alignControlPts.size()); ++i) {
      const auto& cp = m_State.alignControlPts[static_cast<std::size_t>(i)];
      const float resid =
          res.valid && i < static_cast<int>(res.pairResiduals.size()) ? res.pairResiduals[static_cast<std::size_t>(i)] : 0.f;
      auto* remove = new QPushButton("-", table);
      remove->setObjectName(QString("alignResultsRemove_%1").arg(i));
      remove->setFixedWidth(28);
      connect(remove, &QPushButton::clicked, &dialog, [&, i] {
        if (i < 0 || i >= static_cast<int>(m_State.alignControlPts.size()))
          return;
        m_State.alignControlPts.erase(m_State.alignControlPts.begin() + i);
        refreshUi();
      });
      table->setCellWidget(i, 0, remove);
      auto* pairItem = new QTableWidgetItem(QString::number(i + 1));
      pairItem->setTextAlignment(Qt::AlignCenter);
      table->setItem(i, 1, pairItem);
      const auto makeItem = [&](const QString& text) {
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return item;
      };
      table->setItem(i, 2, makeItem(formatLinear(cp.srcX)));
      table->setItem(i, 3, makeItem(formatLinear(cp.srcY)));
      table->setItem(i, 4, makeItem(formatLinear(cp.dstX)));
      table->setItem(i, 5, makeItem(formatLinear(cp.dstY)));
      table->setItem(i, 6, makeItem(res.valid ? formatLinear(resid) : QString("—")));
    }
  };
  refreshUi();

  connect(apply, &QPushButton::clicked, &dialog, [&] {
    if (!m_State.alignLastResult.valid)
      return;
    ApplyAlignCommand(m_State, m_Log, applyScale->isChecked());
    dialog.accept();
  });
  connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(&dialog, &QDialog::rejected, &dialog, [&] {
    m_State.alignControlPts.clear();
    m_State.alignPhase = AppCommandState::AlignPhase::PickSrc;
    m_State.showAlignResultsWindow = false;
  });

  dialog.exec();
  m_State.showAlignResultsWindow = false;
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ServiceUiRequests() {
  if (m_State.showCreatePointsWindow) ShowCreatePoints();
  if (m_State.showFeatureLineElevWindow) {
    m_State.showFeatureLineElevWindow = false;
    ShowFeatureLineElevations();
  }
  if (m_ModalOpen || m_TextDialogOpen) return;
  if (m_State.showAlignResultsWindow) {
    ShowAlignResultsDialog();
    return;
  }
  if (m_State.blockEditCloseAsked) {
    ShowBlockEditorClosePrompt();
    return;
  }
  if (m_State.tableInsertDialogRequested) {
    m_State.tableInsertDialogRequested = false;
    m_ModalOpen = true;
    QDialog dialog(this);
    dialog.setObjectName("insertTableDialog");
    dialog.setWindowTitle("Insert Table");
    dialog.resize(940, 570);
    auto* root = new QVBoxLayout(&dialog);
    auto* body = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    auto* styleForm = new QFormLayout;
    auto* style = new QComboBox(&dialog);
    style->setObjectName("insertTableStyle");
    for (const CadTableStyle& candidate : m_State.tableStyles)
      if (!candidate.point) style->addItem(QString::fromUtf8(candidate.name.c_str()));
    const int selectedStyle = style->findText(QString::fromUtf8(m_State.tablePendingStyle.c_str()));
    if (selectedStyle >= 0) style->setCurrentIndex(selectedStyle);
    styleForm->addRow("Table style:", style);
    left->addLayout(styleForm);
    auto* options = new QGroupBox("Insert options", &dialog);
    auto* optionLayout = new QVBoxLayout(options);
    auto* empty = new QRadioButton("Start from empty table", options);
    empty->setChecked(true);
    optionLayout->addWidget(empty);
    auto* dataLink = new QRadioButton("From a data link", options);
    dataLink->setEnabled(false);
    optionLayout->addWidget(dataLink);
    auto* extraction = new QRadioButton("From object data in the drawing (Data Extraction)", options);
    extraction->setEnabled(false);
    optionLayout->addWidget(extraction);
    left->addWidget(options);
    auto* previewCheck = new QCheckBox("Preview", &dialog);
    previewCheck->setChecked(true);
    left->addWidget(previewCheck);
    auto* preview = new QTableWidget(&dialog);
    preview->setObjectName("insertTablePreview");
    preview->setMinimumSize(400, 260);
    preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview->setSelectionMode(QAbstractItemView::NoSelection);
    preview->horizontalHeader()->hide();
    preview->verticalHeader()->hide();
    preview->setStyleSheet("QTableWidget{background:#202830;color:#e8a61a;border:1px solid #87939b;gridline-color:#e85b24;font:14px monospace;} QTableWidget::item{border:1px solid #e85b24;padding:4px;}");
    left->addWidget(preview, 1);
    QObject::connect(previewCheck, &QCheckBox::toggled, preview, &QWidget::setVisible);
    auto* right = new QVBoxLayout;
    auto* insertion = new QGroupBox("Insertion behavior", &dialog);
    auto* insertionLayout = new QVBoxLayout(insertion);
    auto* insertionPoint = new QRadioButton("Specify insertion point", insertion);
    insertionPoint->setChecked(true);
    insertionLayout->addWidget(insertionPoint);
    auto* windowInsert = new QRadioButton("Specify window", insertion);
    windowInsert->setEnabled(false);
    windowInsert->setToolTip("Window insertion is not supported yet.");
    insertionLayout->addWidget(windowInsert);
    right->addWidget(insertion);
    auto* dimensions = new QGroupBox("Column & row settings", &dialog);
    auto* dimensionsForm = new QFormLayout(dimensions);
    auto* columns = new QSpinBox(&dialog);
    columns->setObjectName("insertTableColumns");
    columns->setRange(1, 50);
    columns->setValue(m_State.tablePendingCols);
    auto* rows = new QSpinBox(&dialog);
    rows->setObjectName("insertTableRows");
    rows->setRange(1, 500);
    rows->setValue(std::max(1, m_State.tablePendingRows - 2));
    auto* columnWidth = new QDoubleSpinBox(&dialog);
    columnWidth->setRange(0.001, 100000.0);
    columnWidth->setDecimals(4);
    auto* rowHeight = new QDoubleSpinBox(&dialog);
    rowHeight->setRange(0.001, 100000.0);
    rowHeight->setDecimals(4);
    auto updateDimensions = [&] {
      if (const CadTableStyle* candidate = CadTableFindStyle(m_State.tableStyles,
                                                             style->currentText().toUtf8().toStdString())) {
        columnWidth->setValue(candidate->columnWidthInches);
        rowHeight->setValue(candidate->rowHeightInches);
        m_State.tablePendingColumnWidthInches = candidate->columnWidthInches;
        m_State.tablePendingRowHeightInches = candidate->rowHeightInches;
      }
    };
    dimensionsForm->addRow("Columns:", columns);
    dimensionsForm->addRow("Column width:", columnWidth);
    dimensionsForm->addRow("Data rows:", rows);
    dimensionsForm->addRow("Row height:", rowHeight);
    right->addWidget(dimensions);
    auto* cellStyles = new QGroupBox("Set cell styles", &dialog);
    auto* cellStyleForm = new QFormLayout(cellStyles);
    auto addCellStyle = [&](const char* label, const char* selectedValue) {
      auto* value = new QComboBox(cellStyles);
      value->addItems({"Title", "Header", "Data"});
      value->setCurrentText(QString::fromUtf8(selectedValue));
      cellStyleForm->addRow(label, value);
      return value;
    };
    auto* titleCellStyle = addCellStyle("First row cell style:", "Title");
    auto* headerCellStyle = addCellStyle("Second row cell style:", "Header");
    auto* dataCellStyle = addCellStyle("All other row cell styles:", "Data");
    right->addWidget(cellStyles);
    right->addStretch(1);
    body->addLayout(left, 3);
    body->addLayout(right, 2);
    root->addLayout(body, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::Help,
                                         &dialog);
    root->addWidget(buttons);
    auto updatePreview = [&] {
      const int dataRows = std::clamp(rows->value(), 1, 8);
      const int rowCount = dataRows + 2;
      const int colCount = std::clamp(columns->value(), 1, 8);
      preview->clear();
      preview->setRowCount(rowCount);
      preview->setColumnCount(colCount);
      preview->setSpan(0, 0, 1, colCount);
      auto setCell = [&](int row, int col, const QString& value, const QComboBox* cellStyle) {
        auto* item = new QTableWidgetItem(value);
        item->setTextAlignment(Qt::AlignCenter);
        const QString cellType = cellStyle->currentText();
        if (cellType == "Title") item->setBackground(QColor("#202830"));
        else if (cellType == "Header") item->setBackground(QColor("#293239"));
        else item->setBackground(QColor("#202830"));
        item->setForeground(QColor("#ef5424"));
        preview->setItem(row, col, item);
      };
      setCell(0, 0, style->currentText(), titleCellStyle);
      for (int col = 0; col < colCount; ++col) setCell(1, col, "Header", headerCellStyle);
      for (int row = 2; row < rowCount; ++row)
        for (int col = 0; col < colCount; ++col) setCell(row, col, "Data", dataCellStyle);
      preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      preview->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    };
    QObject::connect(titleCellStyle, &QComboBox::currentTextChanged, &dialog, updatePreview);
    QObject::connect(headerCellStyle, &QComboBox::currentTextChanged, &dialog, updatePreview);
    QObject::connect(dataCellStyle, &QComboBox::currentTextChanged, &dialog, updatePreview);
    QObject::connect(style, &QComboBox::currentTextChanged, &dialog, [&] { updateDimensions(); updatePreview(); });
    QObject::connect(columns, qOverload<int>(&QSpinBox::valueChanged), &dialog, updatePreview);
    QObject::connect(rows, qOverload<int>(&QSpinBox::valueChanged), &dialog, updatePreview);
    updateDimensions();
    updatePreview();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
      m_State.tablePendingStyle = style->currentText().toUtf8().toStdString();
      m_State.tablePendingCols = columns->value();
      m_State.tablePendingRows = rows->value() + 2;  // title, header, and requested data rows
      m_State.tablePendingColumnWidthInches = static_cast<float>(columnWidth->value());
      m_State.tablePendingRowHeightInches = static_cast<float>(rowHeight->value());
      m_State.tableStage = 3;
      m_Log.push_back("TABLE — specify the insertion point (upper-left corner):");
    } else {
      m_State.active = AppCommandState::Kind::None;
      m_State.tableStage = 0;
    }
    m_ModalOpen = false;
    Refresh();
    return;
  }
  if (m_State.showTableStyleDialog) {
    m_State.showTableStyleDialog = false;
    ShowTableStyles();
    return;
  }
  if (m_State.showDimStyleDialog) {
    m_State.showDimStyleDialog = false;
    m_ModalOpen = true;
    const auto editStyle = [this](DimensionStyle* style, bool create) {
      QStringList names;
      for (const auto& text : m_State.textStyles) names << QString::fromUtf8(text.name.c_str());
      return EditDimensionStyleDialog(this, style, create, names);
    };
    if (m_State.dimensionStyles.empty()) m_State.dimensionStyles = DimensionStyles::DefaultStyles();
    StyleManagerSpec spec;
    spec.objectName = "dimensionStyleManager"; spec.title = "Dimension Style Manager";
    spec.currentCaption = "Current dimension style: ";
    spec.listObjectName = "dimensionStyleList"; spec.previewObjectName = "dimensionStylePreview"; spec.buttonPrefix = "dimensionStyle";
    spec.names = [this] { QStringList out; for (const auto& style : m_State.dimensionStyles) out << QString::fromUtf8(style.name.c_str()); return out; };
    spec.current = [this] { return QString::fromUtf8(m_State.activeDimensionStyle.name.c_str()); };
    spec.preview = [this](const QString& name, QSize size, qreal dpr) {
      const DimensionStyle* style = DimensionStyles::Find(m_State.dimensionStyles, name.toUtf8().toStdString());
      return style ? DimStylePreviewPixmap(*style, size, dpr) : QPixmap();
    };
    spec.describe = [this](const QString& name) {
      const DimensionStyle* style = DimensionStyles::Find(m_State.dimensionStyles, name.toUtf8().toStdString());
      if (!style) return QString();
      return QString("Arrowheads: %1, %2 in\nText: %3, %4 in\nDimension line: %5 · Extension lines: %6")
          .arg(DimArrowTypeName(style->arrowType)).arg(style->arrowSizeInches, 0, 'f', 3)
          .arg(style->textFont.empty() ? QString("Standard") : QString::fromUtf8(style->textFont.c_str()))
          .arg(style->textSizeInches, 0, 'f', 3)
          .arg(QString::fromUtf8(style->dimLineColor.c_str())).arg(QString::fromUtf8(style->extLineColor.c_str()));
    };
    spec.setCurrent = [this](const QString& name) {
      if (const auto* style = DimensionStyles::Find(m_State.dimensionStyles, name.toUtf8().toStdString())) {
        PushUndoSnapshot(m_State, "DIMSTY current"); m_State.activeDimensionStyle = *style;
      }
    };
    spec.create = [this, editStyle]() {
      bool ok = false;
      const QString n = QInputDialog::getText(this, "New Dimension Style", "New style name:", QLineEdit::Normal, "", &ok).trimmed();
      if (!ok || n.isEmpty()) return false;
      if (DimensionStyles::Find(m_State.dimensionStyles, n.toUtf8().toStdString())) { QMessageBox::warning(this, "Dimension Style", "That style name already exists."); return false; }
      DimensionStyle copy = m_State.activeDimensionStyle; copy.name = n.toUtf8().toStdString();
      if (!editStyle(&copy, true)) return false;
      // The editor lets the name be changed: check again so two styles never share a name.
      if (DimensionStyles::Find(m_State.dimensionStyles, copy.name)) { QMessageBox::warning(this, "Dimension Style", "That style name already exists."); return false; }
      PushUndoSnapshot(m_State, "DIMSTY new"); m_State.dimensionStyles.push_back(copy); return true;
    };
    spec.modify = [this, editStyle](const QString& name) {
      const std::string n = name.toUtf8().toStdString();
      auto it = std::find_if(m_State.dimensionStyles.begin(), m_State.dimensionStyles.end(), [&](const auto& s) { return s.name == n; });
      if (it == m_State.dimensionStyles.end()) return false;
      DimensionStyle edited = *it;
      if (!editStyle(&edited, false)) return false;
      PushUndoSnapshot(m_State, "DIMSTY modify");
      it = std::find_if(m_State.dimensionStyles.begin(), m_State.dimensionStyles.end(), [&](const auto& s) { return s.name == n; });
      if (it != m_State.dimensionStyles.end()) *it = edited;
      if (m_State.activeDimensionStyle.name == n) m_State.activeDimensionStyle = edited;
      return true;
    };
    spec.overrideStyle = [this, editStyle](const QString& name) {
      const auto* original = DimensionStyles::Find(m_State.dimensionStyles, name.toUtf8().toStdString());
      if (!original) return false;
      DimensionStyle edited = *original;
      if (!editStyle(&edited, false)) return false;
      // Keep the base definition intact. A drawing-owned override style gives new dimensions a stable
      // native DIMSTYLE reference, including after Autodesk saves the drawing without our payload.
      const std::string base = StyleProperty(original->props, "overrideOf", original->name);
      edited.name = base + " (Overrides)"; edited.props["overrideOf"] = base;
      PushUndoSnapshot(m_State, "DIMSTY override");
      auto found = std::find_if(m_State.dimensionStyles.begin(), m_State.dimensionStyles.end(),
          [&](const auto& item) { return item.name == edited.name; });
      if (found == m_State.dimensionStyles.end()) m_State.dimensionStyles.push_back(edited); else *found = edited;
      m_State.activeDimensionStyle = edited; return true;
    };
    spec.properties = [this](const QString& name) {
      QMap<QString, QString> values;
      const auto* style = DimensionStyles::Find(m_State.dimensionStyles, name.toUtf8().toStdString());
      if (!style) return values;
      for (const auto& [key, value] : style->props) values.insert(QString::fromStdString(key), QString::fromStdString(value));
      values["Text style"] = QString::fromStdString(style->textFont);
      values["Text height"] = QString::number(style->textSizeInches); values["Text color"] = QString::fromStdString(style->textColor);
      values["Text placement"] = DimTextAlignName(style->textAlign);
      values["Dimension color"] = QString::fromStdString(style->dimLineColor);
      values["Dimension linetype"] = QString::fromStdString(style->dimLineType);
      values["Extension color"] = QString::fromStdString(style->extLineColor);
      values["Extension linetype"] = QString::fromStdString(style->extLineType);
      values["Arrow symbol"] = DimArrowTypeName(style->arrowType); values["Arrow size"] = QString::number(style->arrowSizeInches);
      values["Unit format"] = DimUnitFormatName(style->unitFormat); values["Precision"] = QString::number(style->unitPrecision);
      values["Measurement scale"] = QString::number(style->unitScale); values["Suffix"] = QString::fromStdString(style->unitSuffix);
      return values;
    };
    RunStyleManager(this, spec);
    m_State.showDimStyleDialog = false; m_ModalOpen = false; Refresh(); return;
  }
  if (m_State.showMleaderStyleDialog) {
    m_State.showMleaderStyleDialog = false;
    m_ModalOpen = true;
    if (m_State.multileaderStyles.empty()) m_State.multileaderStyles = MultileaderStyles::DefaultStyles();
    // The settings form for one multileader style.
    const auto editStyle = [this](MultileaderStyle* style, bool create) {
      QStringList names;
      for (const auto& text : m_State.textStyles) names << QString::fromUtf8(text.name.c_str());
      return EditMultileaderStyleDialog(this, style, create, names);
    };
    StyleManagerSpec spec;
    spec.objectName = "mleaderStyleDialog"; spec.title = "Multileader Style Manager";
    spec.currentCaption = "Current multileader style: ";
    spec.listObjectName = "mleaderStyleList"; spec.previewObjectName = "mleaderStylePreview"; spec.buttonPrefix = "mleaderStyle";
    spec.names = [this] { QStringList out; for (const auto& style : m_State.multileaderStyles) out << QString::fromUtf8(style.name.c_str()); return out; };
    spec.current = [this] { return QString::fromUtf8(m_State.activeMultileaderStyle.name.c_str()); };
    spec.preview = [this](const QString& name, QSize size, qreal dpr) {
      const MultileaderStyle* style = MultileaderStyles::Find(m_State.multileaderStyles, name.toUtf8().toStdString());
      return style ? MleaderStylePreviewPixmap(*style, name, size, dpr) : QPixmap();
    };
    spec.describe = [this](const QString& name) {
      const MultileaderStyle* style = MultileaderStyles::Find(m_State.multileaderStyles, name.toUtf8().toStdString());
      if (!style) return QString();
      return QString("Text: %1 in%2\nArrowhead: %3 in · Landing gap: %4 in\n%5")
          .arg(style->textSizeInches, 0, 'f', 3)
          .arg(style->textFont.empty() ? QString() : QString(", ") + QString::fromUtf8(style->textFont.c_str()))
          .arg(style->arrowSizeInches, 0, 'f', 3).arg(style->landingGapInches, 0, 'f', 3)
          .arg(style->annotativeDefault ? "Annotative by default" : "Not annotative");
    };
    spec.setCurrent = [this](const QString& name) {
      if (const auto* style = MultileaderStyles::Find(m_State.multileaderStyles, name.toUtf8().toStdString())) {
        PushUndoSnapshot(m_State, "MSTY current"); m_State.activeMultileaderStyle = *style;
      }
    };
    spec.create = [this, editStyle]() {
      bool ok = false;
      const QString n = QInputDialog::getText(this, "New Multileader Style", "New style name:", QLineEdit::Normal, "", &ok).trimmed();
      if (!ok || n.isEmpty()) return false;
      if (MultileaderStyles::Find(m_State.multileaderStyles, n.toUtf8().toStdString())) { QMessageBox::warning(this, "Multileader Style", "That style name already exists."); return false; }
      MultileaderStyle copy = m_State.activeMultileaderStyle; copy.name = n.toUtf8().toStdString();
      if (!editStyle(&copy, true)) return false;
      if (MultileaderStyles::Find(m_State.multileaderStyles, copy.name)) { QMessageBox::warning(this, "Multileader Style", "That style name already exists."); return false; }
      PushUndoSnapshot(m_State, "MSTY new"); m_State.multileaderStyles.push_back(copy); return true;
    };
    spec.modify = [this, editStyle](const QString& name) {
      const std::string n = name.toUtf8().toStdString();
      auto it = std::find_if(m_State.multileaderStyles.begin(), m_State.multileaderStyles.end(), [&](const MultileaderStyle& s) { return s.name == n; });
      if (it == m_State.multileaderStyles.end()) return false;
      MultileaderStyle edited = *it;
      if (!editStyle(&edited, false)) return false;
      PushUndoSnapshot(m_State, "MSTY modify");
      it = std::find_if(m_State.multileaderStyles.begin(), m_State.multileaderStyles.end(), [&](const MultileaderStyle& s) { return s.name == n; });
      if (it != m_State.multileaderStyles.end()) *it = edited;
      if (m_State.activeMultileaderStyle.name == n) m_State.activeMultileaderStyle = edited;
      return true;
    };
    RunStyleManager(this, spec);
    m_ModalOpen = false;
    Refresh();
    return;
  }
  if (m_State.showTextStyleManagerWindow) {
    m_State.showTextStyleManagerWindow = false;
    m_ModalOpen = true;
    TextStyles::EnsureStandard(m_State.textStyles);
    if (m_State.activeTextStyleName.empty()) m_State.activeTextStyleName = TextStyles::kStandardName;
    const auto editStyle = [this](TextStyle* style, bool create) {
      return EditTextStyleDialog(this, style, create);
    };
    StyleManagerSpec spec;
    spec.objectName = "textStyleManager"; spec.title = "Text Style Manager";
    spec.currentCaption = "Current text style: ";
    spec.listObjectName = "textStyleList"; spec.previewObjectName = "textStylePreview"; spec.buttonPrefix = "textStyle";
    spec.names = [this] { QStringList out; for (const auto& style : m_State.textStyles) out << QString::fromUtf8(style.name.c_str()); return out; };
    spec.current = [this] { return QString::fromUtf8(m_State.activeTextStyleName.c_str()); };
    spec.preview = [this](const QString& name, QSize size, qreal dpr) {
      const TextStyle* style = TextStyles::Find(m_State.textStyles, name.toUtf8().toStdString());
      return style ? TextStylePreviewPixmap(*style, size, dpr) : QPixmap();
    };
    spec.describe = [this](const QString& name) {
      const TextStyle* style = TextStyles::Find(m_State.textStyles, name.toUtf8().toStdString());
      if (!style) return QString();
      return QString("Font: %1%2%3\nHeight: %4 in · Width factor: %5 · Oblique: %6°")
          .arg(style->fontFamily.empty() ? QString("Standard") : QString::fromUtf8(style->fontFamily.c_str()))
          .arg(style->bold ? ", bold" : "").arg(style->italic ? ", italic" : "")
          .arg(style->heightInches, 0, 'f', 3).arg(style->widthFactor, 0, 'f', 2).arg(style->obliqueDeg, 0, 'f', 1);
    };
    spec.setCurrent = [this](const QString& name) { PushUndoSnapshot(m_State, "STYLE current"); m_State.activeTextStyleName = name.toUtf8().toStdString(); };
    spec.create = [this, editStyle]() {
      bool ok = false;
      const QString n = QInputDialog::getText(this, "New Text Style", "New style name:", QLineEdit::Normal, "", &ok).trimmed();
      if (!ok || n.isEmpty()) return false;
      if (TextStyles::Find(m_State.textStyles, n.toUtf8().toStdString())) { QMessageBox::warning(this, "Text Style", "That style name already exists."); return false; }
      const TextStyle* base = TextStyles::Find(m_State.textStyles, m_State.activeTextStyleName);
      if (!base) base = TextStyles::Find(m_State.textStyles, TextStyles::kStandardName);
      TextStyle copy = base ? *base : TextStyle{};
      copy.name = n.toUtf8().toStdString();
      if (!editStyle(&copy, true)) return false;
      if (TextStyles::Find(m_State.textStyles, copy.name)) { QMessageBox::warning(this, "Text Style", "That style name already exists."); return false; }
      PushUndoSnapshot(m_State, "STYLE new"); m_State.textStyles.push_back(copy); return true;
    };
    spec.modify = [this, editStyle](const QString& name) {
      TextStyle* style = TextStyles::Find(m_State.textStyles, name.toUtf8().toStdString());
      if (!style) return false;
      TextStyle edited = *style;
      if (!editStyle(&edited, false)) return false;
      PushUndoSnapshot(m_State, "STYLE modify");
      // The dialog ran a nested event loop and the undo snapshot touched the state: look the style up again.
      if (TextStyle* current = TextStyles::Find(m_State.textStyles, name.toUtf8().toStdString())) *current = edited;
      return true;
    };
    RunStyleManager(this, spec);
    m_ModalOpen = false;
    Refresh();
    return;
  }
  if (m_State.blockAuthoringPaletteOpen) ShowBlockAuthoringPalette();
  else if (auto* palette = findChild<QDialog*>("blockAuthoringPalette")) palette->close();
  if (m_State.pipeFittingPaletteOpen) ShowPipeFittingPalette();
  else if (auto* palette = findChild<QDialog*>("pipeFittingPalette"); palette && palette->isVisible()) palette->close();
  if (m_State.showConnectionModesWindow) ShowConnectionModes();
  else if (auto* modes = findChild<QDialog*>("connectionModes")) modes->close();
  if (m_State.addDrawingToProjectUid != 0) {
    const auto uid = m_State.addDrawingToProjectUid;
    m_State.addDrawingToProjectUid = 0;
    ShowAddDrawing(uid);
  } else if (m_State.pointEditPrompt.active) {
    ShowProjectPointWarning();
  } else if (m_State.projectAttachPrompt.kind != AppCommandState::ProjectAttachPrompt::Kind::None) {
    ShowProjectAttachPrompt();
  } else if (m_State.pdfAttachDialogOpen) {
    ShowPdfAttachDialog();
  } else if (m_State.pastePrompt.active) {
    ShowPasteWarning();
  } else if (m_State.showLayerManagerWindow) {
    m_State.showLayerManagerWindow = false;
    ShowLayers();
  } else if (m_State.addPointTableDialogRequested) {
    m_State.addPointTableDialogRequested = false;
    m_ModalOpen = true;
    ShowAddPointTableDialog(this, m_State, m_Log);
    m_ModalOpen = false;
    Refresh();
  } else if (m_State.showPointGroupManagerWindow) {
    m_State.showPointGroupManagerWindow = false;
    m_ModalOpen = true;
    ShowPointGroupManager(this, m_State, m_Log);
    m_ModalOpen = false;
    Refresh();
  } else if (m_State.showUnitsWindow || m_State.showDrawingSettingsWindow) {
    m_State.showUnitsWindow = false;
    m_State.showDrawingSettingsWindow = false;
    m_State.drawingSettingsShowUnitsAndZone = false;
    ShowUnits();
  } else if (m_State.showQuickSelectWindow) {
    m_State.showQuickSelectWindow = false;
    ShowQuickSelect();
  } else if (m_State.showSettingsWindow) {
    m_State.showSettingsWindow = false;  // OPTIONS / the shortcut menu's Options...
    ShowSettings();
  } else if (m_State.showSelectionCyclingWindow) {
    m_State.showSelectionCyclingWindow = false;
    ShowSelectionList();
  } else if (m_State.showViewManagerWindow) {
    m_State.showViewManagerWindow = false;
    ShowViewManager();
  } else if (m_State.showViewPointsWindow || m_State.showSurveyPointGridWindow) {
    const bool database = m_State.showSurveyPointGridWindow;
    m_State.showViewPointsWindow = m_State.showSurveyPointGridWindow = false;
    ShowPoints(database);
  } else if (m_State.showImportPointsWindow || m_State.showExportPointsWindow) {
    const bool importing = m_State.showImportPointsWindow;
    m_State.showImportPointsWindow = m_State.showExportPointsWindow = false;
    ShowPointTransfer(importing);
  } else if (m_State.showTraverseEditorWindow || m_State.showTraverseClosureWindow) {
    m_State.showTraverseEditorWindow = m_State.showTraverseClosureWindow = false;
    ShowAdjustment();
  } else if (m_State.showCreateSurfaceWindow) {
    m_State.showCreateSurfaceWindow = false;
    ShowCreateSurface();
  } else if (m_State.showSurfacePropertiesWindow) {
    m_State.showSurfacePropertiesWindow = false;
    ShowSurfaceProperties(m_State.surfacePropertiesIndex);
  } else if (m_State.showSurfaceManagerWindow) {
    m_State.showSurfaceManagerWindow = false;
    ShowSurfaceManager();
  } else if (m_State.showSurfaceStyleWindow) {
    const QString focus = QString::fromUtf8(m_State.surfaceStyleEditorFocusName.c_str());
    const bool surfacesTitle = m_State.surfaceStyleUseSurfacesTitle;
    m_State.showSurfaceStyleWindow = false;
    m_State.surfaceStyleEditorFocusName.clear();
    m_State.surfaceStyleUseSurfacesTitle = false;
    ShowSurfaceStyles(focus, surfacesTitle);
  } else if (m_State.showPageSetupManager) {
    const int layoutIndex = m_State.pageSetupLayoutIdx;
    m_State.showPageSetupManager = false;
    ShowPageSetupManager(layoutIndex);
  } else if (m_State.surveyReportSelectLatestPending) {
    ShowSurveyReports();
  }
}

void EditorWindow::ShowViewManager() {
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("viewManagerDialog");
  dialog.setWindowTitle("Manage Views & UCS");
  dialog.resize(650, 510);
  auto* layout = new QVBoxLayout(&dialog);
  auto* current = new QLabel(&dialog);
  current->setObjectName("viewManagerCurrentView");
  const bool darkViews = palette().color(QPalette::Window).lightness() < 128;
  current->setStyleSheet(darkViews
      ? QtApplyAccent("background:#30373c;color:#f4f7fb;border-left:3px solid @strong@;padding:10px 12px;font-weight:600;border-radius:5px;", true)
      : "background:#243a53;color:#f4f7fb;padding:10px 12px;font-weight:600;border-radius:5px;");
  layout->addWidget(current);

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("viewManagerTabs");
  auto* views = new QListWidget(tabs);
  views->setObjectName("viewManagerSavedViews");
  views->setSelectionMode(QAbstractItemView::SingleSelection);
  views->setSpacing(6);
  views->setStyleSheet(darkViews
      ? QtApplyAccent("QListWidget{border:0;background:transparent} QListWidget::item{background:#292f34;border:1px solid #3c444a;border-radius:5px;padding:10px;color:#e7ecee} QListWidget::item:selected{background:@tint@;border:1px solid @strong@;color:#ffffff}", true)
      : QtApplyAccent("QListWidget{border:0;background:transparent} QListWidget::item{background:#f3f6f8;border:1px solid #d7dee5;border-radius:5px;padding:10px;color:#202a33} QListWidget::item:selected{background:@tint@;border:1px solid @strong@;color:#202020}", false));
  auto* ucsList = new QListWidget(tabs);
  ucsList->setObjectName("viewManagerSavedUcs");
  ucsList->setSelectionMode(QAbstractItemView::SingleSelection);
  ucsList->setSpacing(6);
  ucsList->setStyleSheet(views->styleSheet());
  tabs->addTab(views, "Views");
  tabs->addTab(ucsList, "Coordinate Systems");
  layout->addWidget(tabs, 1);

  auto* actionRow = new QHBoxLayout();
  auto* restore = new QPushButton("Restore", &dialog);
  restore->setObjectName("viewManagerRestore");
  auto* update = new QPushButton("Update", &dialog);
  update->setObjectName("viewManagerUpdate");
  auto* remove = new QPushButton("Delete", &dialog);
  remove->setObjectName("viewManagerDelete");
  auto* save = new QPushButton("New View…", &dialog);
  save->setObjectName("viewManagerNewView");
  actionRow->addWidget(restore);
  actionRow->addWidget(update);
  actionRow->addWidget(remove);
  actionRow->addStretch();
  actionRow->addWidget(save);
  layout->addLayout(actionRow);
  auto* close = new QPushButton("Close", &dialog);
  close->setObjectName("viewManagerClose");
  close->setDefault(true);
  auto* closeRow = new QHBoxLayout();
  closeRow->addStretch();
  closeRow->addWidget(close);
  layout->addLayout(closeRow);

  auto refresh = [&] {
    const auto* active = CurrentNamedView(m_State);
    current->setText(active ? QString("Current view · %1").arg(QString::fromUtf8(active->name.c_str()))
                            : "Current view · Unsaved View");
    const QString selectedView = views->currentItem() ? views->currentItem()->data(Qt::UserRole).toString() : QString();
    const QSignalBlocker blockViews(views);
    const QSignalBlocker blockUcs(ucsList);
    views->clear();
    for (const auto& view : m_State.namedViews) {
      const QString name = QString::fromUtf8(view.name.c_str());
      const bool isCurrent = active && active->name == view.name;
      const QString projection = view.projection == Camera::Projection::Perspective ? "Perspective" : "Orthographic";
      auto* item = new QListWidgetItem(QString("%1%2\n%3 · %4 · %5")
                                           .arg(name, isCurrent ? "   CURRENT" : "", projection,
                                                QString("FOV %1°").arg(view.fovDeg, 0, 'f', 0),
                                                ucs::IsWorld(view.ucs) ? "WCS" : "UCS"),
                                       views);
      item->setData(Qt::UserRole, name);
      item->setSizeHint(QSize(0, 58));
      item->setToolTip(QString("Azimuth %1° · Elevation %2° · %3")
                           .arg(view.azimuthDeg, 0, 'f', 1).arg(view.elevationDeg, 0, 'f', 1)
                           .arg(DescribeUcs(view.ucs).c_str()));
      if (name == selectedView || (selectedView.isEmpty() && isCurrent)) views->setCurrentItem(item);
    }
    if (!views->currentItem() && views->count()) views->setCurrentRow(0);

    const QString selectedUcs = ucsList->currentItem() ? ucsList->currentItem()->data(Qt::UserRole).toString() : QString();
    ucsList->clear();
    for (const auto& named : m_State.ucsNamed) {
      const QString name = QString::fromUtf8(named.name.c_str());
      const bool isCurrent = ucs::FramesMatch(named.frame, m_State.activeUcs);
      auto* item = new QListWidgetItem(QString("%1%2\n%3")
                                           .arg(name, isCurrent ? "   CURRENT" : "", DescribeUcs(named.frame).c_str()),
                                       ucsList);
      item->setData(Qt::UserRole, name);
      item->setSizeHint(QSize(0, 58));
      if (name == selectedUcs || (selectedUcs.isEmpty() && isCurrent)) ucsList->setCurrentItem(item);
    }
    if (!ucsList->currentItem() && ucsList->count()) ucsList->setCurrentRow(0);
    const bool onViews = tabs->currentIndex() == 0;
    const bool hasSelection = onViews ? views->currentItem() != nullptr : ucsList->currentItem() != nullptr;
    const bool currentUcsSelected = !onViews && ucsList->currentRow() >= 0 &&
        ucs::FramesMatch(m_State.activeUcs, m_State.ucsNamed[static_cast<size_t>(ucsList->currentRow())].frame);
    restore->setEnabled(hasSelection && (onViews || !currentUcsSelected));
    update->setVisible(onViews);
    save->setVisible(onViews);
    update->setEnabled(onViews && hasSelection);
    remove->setEnabled(hasSelection);
    remove->setVisible(!onViews || hasSelection);
    if (views->count() == 0 && onViews) current->setText("No saved views · Choose New View to save this view");
    if (ucsList->count() == 0 && !onViews) current->setText("No saved coordinate systems · Save one with UCS N <name>");
  };

  connect(views, &QListWidget::currentRowChanged, &dialog, [&] { refresh(); });
  connect(ucsList, &QListWidget::currentRowChanged, &dialog, [&] { refresh(); });
  connect(tabs, &QTabWidget::currentChanged, &dialog, [&] { refresh(); });
  connect(restore, &QPushButton::clicked, &dialog, [&] {
    if (tabs->currentIndex() == 0 && views->currentRow() >= 0) {
      const auto selected = m_State.namedViews[static_cast<size_t>(views->currentRow())];
      RestoreNamedView(m_State, selected, m_Log);
    } else if (tabs->currentIndex() == 1 && ucsList->currentRow() >= 0) {
      RestoreNamedUcs(m_State, m_State.ucsNamed[static_cast<size_t>(ucsList->currentRow())].name, m_Log);
    }
    Refresh();
    refresh();
  });
  connect(update, &QPushButton::clicked, &dialog, [&] {
    if (views->currentRow() < 0) return;
    const auto name = m_State.namedViews[static_cast<size_t>(views->currentRow())].name;
    ProcessViewCommandLine(m_State, "S " + name, m_Log);
    Refresh();
    refresh();
  });
  connect(remove, &QPushButton::clicked, &dialog, [&] {
    if (tabs->currentIndex() == 0 && views->currentRow() >= 0) {
      const auto name = m_State.namedViews[static_cast<size_t>(views->currentRow())].name;
      ProcessViewCommandLine(m_State, "D " + name, m_Log);
    } else if (tabs->currentIndex() == 1 && ucsList->currentRow() >= 0) {
      DeleteNamedUcs(m_State, m_State.ucsNamed[static_cast<size_t>(ucsList->currentRow())].name, m_Log);
    }
    Refresh();
    refresh();
  });
  connect(save, &QPushButton::clicked, &dialog, [&] {
    bool accepted = false;
    const QString name = QInputDialog::getText(&dialog, "New View", "Save the current camera and coordinate frame as:",
                                                QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    ProcessViewCommandLine(m_State, "S " + name.toUtf8().toStdString(), m_Log);
    Refresh();
    refresh();
  });
  connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(&dialog, &QDialog::finished, this, [this] { m_State.showViewManagerWindow = false; });
  refresh();
  m_State.showViewManagerWindow = false;
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowCreatePoints() {
  if (findChild<QDialog*>("createPoints")) return;
  auto* dialog = new QDialog(this);
  dialog->setObjectName("createPoints");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Create Survey Points");
  auto* layout = new QFormLayout(dialog);
  auto* hint = new QLabel("Click in the drawing to place points. Click an existing marker to select it.\nEscape ends point creation.", dialog);
  hint->setWordWrap(true); layout->addRow(hint);
  auto* number = new QSpinBox(dialog); number->setObjectName("nextPointNumber");
  number->setRange(1, std::numeric_limits<int>::max() - 1);
  auto* layer = new QLineEdit(dialog); layer->setObjectName("pointLayer");
  auto* description = new QPlainTextEdit(dialog); description->setObjectName("pointDescription");
  description->setMaximumHeight(85);
  auto* elevation = new QDoubleSpinBox(dialog); elevation->setObjectName("pointElevation");
  elevation->setRange(-1.e9, 1.e9); elevation->setDecimals(6);
  auto* duplicates = new QComboBox(dialog); duplicates->setObjectName("pointDuplicatePolicy");
  duplicates->addItems({"Notify (skip)", "Renumber (next free)", "Merge (update coordinates)", "Overwrite"});
  auto* sequential = new QCheckBox("Sequential numbering", dialog);
  auto* step = new QSpinBox(dialog); step->setRange(1, 1000000);
  layout->addRow("Next point number", number); layout->addRow("Layer", layer);
  layout->addRow("Description", description); layout->addRow("Elevation", elevation);
  layout->addRow("If number exists", duplicates); layout->addRow(sequential); layout->addRow("Number increment", step);
  auto* files = new QHBoxLayout;
  auto* save = new QPushButton("Save points JSON…", dialog);
  auto* load = new QPushButton("Load points JSON…", dialog);
  files->addWidget(save); files->addWidget(load); layout->addRow(files);
  connect(number, &QSpinBox::valueChanged, dialog, [this](int n) { m_State.createPointsNextId = n; });
  connect(layer, &QLineEdit::textEdited, dialog, [this](const QString& s) { m_State.createPointsOpts.layer = s.trimmed().toUtf8().toStdString(); });
  connect(description, &QPlainTextEdit::textChanged, dialog, [this, description] { m_State.createPointsOpts.defaultDescription = description->toPlainText().toUtf8().toStdString(); });
  connect(elevation, &QDoubleSpinBox::valueChanged, dialog, [this](double z) { m_State.createPointsOpts.defaultElevation = static_cast<float>(z); });
  connect(duplicates, &QComboBox::currentIndexChanged, dialog, [this](int index) { m_State.createPointsOpts.duplicatePolicy = SurveyDuplicatePolicy(index); });
  connect(sequential, &QCheckBox::toggled, dialog, [this](bool enabled) { m_State.createPointsOpts.sequentialNumbering = enabled; });
  connect(step, &QSpinBox::valueChanged, dialog, [this](int n) { m_State.createPointsOpts.pointNumberOffset = n; });
  connect(save, &QPushButton::clicked, dialog, [this, dialog] {
    const auto path = QFileDialog::getSaveFileName(dialog, "Save survey points", {}, "Survey points (*.json)");
    if (!path.isEmpty()) { SaveSurveyPointsToJsonFile(m_State, path.toUtf8().constData(), m_Log); Refresh(); }
  });
  connect(load, &QPushButton::clicked, dialog, [this, dialog] {
    if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) return;
    const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
    if (std::any_of(m_State.openProjects.begin(), m_State.openProjects.end(), [uid](const auto& p) { return p.uid == uid && p.readOnly; })) {
      QMessageBox::warning(dialog, "Load survey points", "This project is read-only."); return;
    }
    const auto path = QFileDialog::getOpenFileName(dialog, "Load survey points", {}, "Survey points (*.json)");
    if (!path.isEmpty()) { LoadSurveyPointsFromJsonFile(m_State, path.toUtf8().constData(), m_Log); Refresh(); }
  });
  connect(dialog, &QDialog::finished, this, [this] { m_State.showCreatePointsWindow = false; Refresh(); });
  auto* timer = new QTimer(dialog);
  auto sync = [=] {
    if (!m_State.showCreatePointsWindow || m_State.activeDrawingIdx <= 0) { dialog->close(); return; }
    const auto& opts = m_State.createPointsOpts;
    const QSignalBlocker a(number), b(layer), c(description), d(elevation), e(duplicates), f(sequential), g(step);
    if (!number->hasFocus()) number->setValue(m_State.createPointsNextId);
    const auto& objectLayer = m_State.drawingSettings.ObjectLayer(ObjectLayerKind::SurveyPoint);
    layer->setEnabled(!objectLayer.locked);
    if (!layer->hasFocus()) layer->setText(QString::fromUtf8((objectLayer.locked || opts.layer.empty() ? ResolveObjectLayer(m_State.drawingSettings, ObjectLayerKind::SurveyPoint, {}) : opts.layer).c_str()));
    if (!description->hasFocus()) description->setPlainText(QString::fromUtf8(opts.defaultDescription.c_str()));
    if (!elevation->hasFocus()) elevation->setValue(opts.defaultElevation);
    duplicates->setCurrentIndex(int(opts.duplicatePolicy));
    sequential->setChecked(opts.sequentialNumbering);
    if (!step->hasFocus()) step->setValue(opts.pointNumberOffset);
  };
  connect(timer, &QTimer::timeout, dialog, sync);
  sync(); timer->start(100); dialog->show();
}

void EditorWindow::ShowQuickSelect() {
  using S = AppCommandState;
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("quickSelect");
  dialog.setWindowTitle("Quick Select");
  auto* layout = new QFormLayout(&dialog);
  auto combo = [&](const char* name, const QStringList& labels, int index) {
    auto* widget = new QComboBox(&dialog);
    widget->setObjectName(name); widget->addItems(labels); widget->setCurrentIndex(index);
    return widget;
  };
  auto* scope = combo("selectionScope", {"Entire drawing", "Current selection"}, int(m_State.qsApplyTo));
  auto* type = combo("selectionType", {"All", "Line", "Circle", "Arc", "Ellipse", "Polyline", "Text", "MText", "Aligned dimension", "Linear dimension", "Angular dimension", "Survey point"}, int(m_State.qsObjectType));
  auto* property = new QComboBox(&dialog); property->setObjectName("selectionProperty");
  auto* operation = new QComboBox(&dialog); operation->setObjectName("selectionOperator");
  auto* value = new QComboBox(&dialog); value->setObjectName("selectionValue"); value->setEditable(true);
  auto* mode = combo("selectionMode", {"Include matching objects", "Exclude matching objects"}, int(m_State.qsIncludeMode));
  auto* append = new QCheckBox("Append to current selection", &dialog);
  append->setChecked(m_State.qsAppendToExisting);
  layout->addRow("Apply to", scope); layout->addRow("Object type", type);
  layout->addRow("Property", property); layout->addRow("Operator", operation);
  layout->addRow("Value", value); layout->addRow("Selection", mode); layout->addRow(append);
  const QStringList names{"Layer", "Color", "Length", "Radius", "Closed", "Content", "Point number", "Elevation", "Easting", "Northing", "Description"};
  auto numeric = [&] { const int p = property->currentData().toInt(); return p == 2 || p == 3 || (p >= 6 && p <= 9); };
  auto updateOperators = [&] {
    const int previous = operation->currentData().toInt();
    operation->clear();
    operation->addItem("Equals", 0); operation->addItem("Not equal", 1);
    if (numeric()) { operation->addItem("Less than", 2); operation->addItem("Greater than", 3); }
    operation->addItem("Select all", 4);
    operation->setCurrentIndex(std::max(0, operation->findData(previous)));
    const QString previousValue = value->currentText();
    value->clear();
    if (property->currentData().toInt() == 0) {
      SyncDrawingLayerTableWithGeometry(m_State);
      for (const auto& layer : m_State.drawingLayerTable) value->addItem(QString::fromUtf8(layer.name.c_str()));
    } else if (property->currentData().toInt() == 1) value->addItems({"ByLayer", "ByBlock", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"});
    else if (property->currentData().toInt() == 4) value->addItems({"Yes", "No"});
    value->setEditText(previousValue);
  };
  auto updateProperties = [&] {
    const int previous = property->currentData().toInt();
    const QSignalBlocker blocker(property);
    property->clear();
    std::vector<int> allowed{0, 1};
    switch (type->currentIndex()) {
    case 1: allowed.push_back(2); break;
    case 2: case 3: allowed.push_back(3); break;
    case 5: allowed.insert(allowed.end(), {2, 4}); break;
    case 6: case 7: allowed.push_back(5); break;
    case 11: allowed = {0, 6, 7, 8, 9, 10}; break;
    default: break;
    }
    for (int p : allowed) property->addItem(names[p], p);
    property->setCurrentIndex(std::max(0, property->findData(previous)));
    updateOperators();
  };
  connect(type, &QComboBox::currentIndexChanged, &dialog, updateProperties);
  connect(property, &QComboBox::currentIndexChanged, &dialog, updateOperators);
  connect(operation, &QComboBox::currentIndexChanged, &dialog, [&] { value->setEnabled(operation->currentData().toInt() != 4); });
  updateProperties();
  property->setCurrentIndex(std::max(0, property->findData(int(m_State.qsProperty))));
  operation->setCurrentIndex(std::max(0, operation->findData(int(m_State.qsOperator))));
  value->setEditText(QString::fromUtf8(m_State.qsValueBuf));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    std::string text = value->currentText().toUtf8().toStdString();
    if (operation->currentData().toInt() != 4) {
      bool valid = true;
      if (numeric()) { const double number = value->currentText().toDouble(&valid); valid = valid && std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(); }
      if (property->currentData().toInt() == 1) valid = CadColorStorageFromTyped(text, &text);
      if (text.size() >= sizeof(m_State.qsValueBuf)) valid = false;
      if (!valid) { QMessageBox::warning(&dialog, "Quick Select", "Enter a valid value for the selected property."); return; }
    }
    m_State.qsApplyTo = S::QsApplyTo(scope->currentIndex());
    m_State.qsObjectType = S::QsObjectType(type->currentIndex());
    m_State.qsProperty = S::QsProperty(property->currentData().toInt());
    m_State.qsOperator = S::QsOperator(operation->currentData().toInt());
    m_State.qsIncludeMode = S::QsInclude(mode->currentIndex());
    m_State.qsAppendToExisting = append->isChecked();
    std::snprintf(m_State.qsValueBuf, sizeof(m_State.qsValueBuf), "%s", text.c_str());
    ExecuteQuickSelect(m_State, m_Log);
    dialog.accept();
  });
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowLayers() {
  m_ModalOpen = true;
  ShowLayerManager(this, m_State, [this] { Refresh(); });
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowUnits() {
  if (auto* existing = findChild<QDialog*>("drawingSettingsDialog")) {
    existing->show(); existing->raise(); existing->activateWindow(); return;
  }
  if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) {
    m_Log.push_back("DRAWINGSETTINGS — open or create a drawing first."); Refresh(); return;
  }
  LoadCoordinateSystemDictionaries();
  const bool coordinateSystemsAvailable = geo::DictionariesLoaded();
  auto* dialog = new QDialog(this);
  dialog->setObjectName("drawingSettingsDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Drawing Settings - " + QString::fromUtf8(m_State.drawingTabs[m_State.activeDrawingIdx].name.c_str()));
  dialog->setWindowModality(Qt::WindowModal);
  dialog->resize(970, 650);
  DrawingSettings staged = m_State.drawingSettings;
  int stagedUnits = m_State.drawingInsUnits;
  auto* root = new QVBoxLayout(dialog);
  auto* body = new QHBoxLayout;
  auto* navigation = new QListWidget(dialog);
  navigation->setObjectName("drawingSettingsNavigation");
  navigation->setFixedWidth(190);
  navigation->addItems({"Units and Zone", "Transformation", "Object Layers"});
  navigation->setCurrentRow(0);
  auto* pages = new QStackedWidget(dialog);
  pages->setObjectName("drawingSettingsPages");
  body->addWidget(navigation); body->addWidget(pages, 1); root->addLayout(body, 1);

  const std::pair<const char*, int> unitCodes[] = {{"Unitless", 0}, {"Inches", 1}, {"Feet", 2}, {"Millimeters", 4}, {"Meters", 6}};
  auto* unitsPage = new QWidget(dialog);
  auto* unitsForm = new QFormLayout(unitsPage);
  auto* units = new QComboBox(unitsPage); units->setObjectName("drawingUnit");
  for (const auto& code : unitCodes) units->addItem(code.first, code.second);
  units->setCurrentIndex(units->findData(stagedUnits));
  auto* angular = new QComboBox(unitsPage); angular->setObjectName("drawingAngularUnits");
  angular->addItems({"Degrees", "Radians", "Grads"}); angular->setCurrentIndex(static_cast<int>(staged.angularUnits));
  auto* foot = new QComboBox(unitsPage); foot->setObjectName("drawingFootDefinition");
  foot->addItems({"US Survey Foot", "International Foot"}); foot->setCurrentIndex(static_cast<int>(staged.footDefinition));
  auto* scale = new QComboBox(unitsPage); scale->setObjectName("drawingScaleChoice");
  auto* customScale = new QDoubleSpinBox(unitsPage); customScale->setObjectName("drawingCustomScale");
  customScale->setRange(0.000001, 1.e9); customScale->setDecimals(6); customScale->setValue(m_State.modelUnitsPerPlottedInch);
  auto fillScales = [scale, customScale](int insUnits, float current) {
    const QSignalBlocker blocker(scale); scale->clear();
    const auto choices = PlotScaleChoicesFor(insUnits);
    for (const auto& choice : choices) scale->addItem(QString::fromStdString(choice.label), choice.modelUnitsPerPlottedInch);
    const int choice = PlotScaleChoiceIndex(choices, current);
    scale->addItem("Custom", -1);
    scale->setCurrentIndex(choice >= 0 ? choice : scale->count() - 1);
    customScale->setValue(current); customScale->setEnabled(choice < 0);
  };
  fillScales(stagedUnits, m_State.modelUnitsPerPlottedInch);
  connect(units, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [&, fillScales](int) {
    stagedUnits = units->currentData().toInt(); fillScales(stagedUnits, static_cast<float>(customScale->value()));
  });
  connect(scale, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [scale, customScale](int index) {
    const bool custom = index >= 0 && scale->currentData().toInt() == -1;
    customScale->setEnabled(custom);
    if (!custom && index >= 0) customScale->setValue(scale->currentData().toFloat());
  });
  auto* scaleRow = new QWidget(unitsPage); auto* scaleRowLayout = new QHBoxLayout(scaleRow); scaleRowLayout->setContentsMargins(0,0,0,0);
  scaleRowLayout->addWidget(scale, 1); scaleRowLayout->addWidget(customScale);
  auto* scaleInserted = new QCheckBox("Scale objects inserted from other drawings", unitsPage);
  scaleInserted->setChecked(staged.scaleInsertedObjects);
  auto* setVariables = new QCheckBox("Set drawing variables to match", unitsPage);
  setVariables->setChecked(staged.setDrawingVariables);
  unitsForm->addRow("Drawing units (relabels geometry)", units);
  unitsForm->addRow("Angular units", angular);
  unitsForm->addRow("Imperial to Metric conversion", foot);
  unitsForm->addRow("Plot scale", scaleRow);
  unitsForm->addRow(scaleInserted); unitsForm->addRow(setVariables);
  auto* zoneGroup = new QGroupBox("Coordinate system", unitsPage);
  auto* zoneForm = new QFormLayout(zoneGroup);
  auto* category = new CoordinateSystemComboBox(zoneGroup); category->setObjectName("drawingZoneCategory");
  category->addItem(QString::fromUtf8(geo::kNoZoneCategory), QString());
  for (const auto& name : geo::Categories()) category->addItem(QString::fromUtf8(name.c_str()), QString::fromUtf8(name.c_str()));
  auto* systems = new CoordinateSystemComboBox(zoneGroup); systems->setObjectName("drawingZoneSystem");
  auto* zoneCode = new QLineEdit(zoneGroup); zoneCode->setObjectName("drawingZoneCode");
  auto* zoneDetails = new QLabel(zoneGroup); zoneDetails->setObjectName("drawingZoneDetails"); zoneDetails->setWordWrap(true);
  auto* zoneError = new QLabel(unitsPage); zoneError->setObjectName("drawingZoneError");
  zoneError->setWordWrap(true); zoneError->setStyleSheet("color:#b23b35;");
  zoneError->setText(coordinateSystemsAvailable ? QString() : QString::fromUtf8(geo::DictionaryError().c_str()));
  zoneError->setVisible(!coordinateSystemsAvailable);
  zoneGroup->setEnabled(coordinateSystemsAvailable);
  auto updateDetails = [zoneDetails, &staged] {
    if (staged.zoneCode.empty()) { zoneDetails->setText("No Datum, No Projection\nProjection: Unknown projection\nDatum: Unknown Datum"); return; }
    const auto info = geo::FindCoordinateSystem(staged.zoneCode);
    zoneDetails->setText(info ? QString("%1\nProjection: %2\nDatum: %3").arg(QString::fromUtf8(info->description.c_str()), QString::fromUtf8(info->projection.c_str()), QString::fromUtf8(info->datum.c_str()))
                              : QString("%1 (unknown in this dictionary)\nProjection: Unknown projection\nDatum: Unknown Datum").arg(QString::fromUtf8(staged.zoneCode.c_str())));
  };
  auto populateSystems = [systems, &staged, updateDetails](const QString& cat) {
    const QSignalBlocker blocker(systems); systems->clear();
    const auto values = cat.isEmpty() ? std::vector<std::string>{} : geo::CoordinateSystemsIn(cat.toUtf8().toStdString());
    for (const auto& code : values) {
      const auto info = geo::FindCoordinateSystem(code);
      systems->addItem(info ? QString::fromUtf8(info->description.c_str()) : QString::fromUtf8(code.c_str()), QString::fromUtf8(code.c_str()));
    }
    if (!values.empty()) { systems->setCurrentIndex(0); staged.zoneCode = values.front(); }
    else if (cat.isEmpty()) staged.zoneCode.clear();
    updateDetails();
  };
  QString selectedCategory = staged.zoneCode.empty() ? QString() : QString::fromUtf8(geo::CategoryOf(staged.zoneCode).c_str());
  int catIndex = category->findData(selectedCategory);
  category->setCurrentIndex(catIndex >= 0 ? catIndex : 0);
  const std::string initialZoneCode = staged.zoneCode;
  populateSystems(selectedCategory);
  if (!staged.zoneCode.empty()) {
    const auto info = geo::FindCoordinateSystem(initialZoneCode);
    if (info) {
      const int zoneIndex = systems->findData(QString::fromUtf8(initialZoneCode.c_str()));
      if (zoneIndex >= 0) { systems->setCurrentIndex(zoneIndex); staged.zoneCode = initialZoneCode; }
    } else staged.zoneCode = initialZoneCode;
    updateDetails();
  }
  zoneCode->setText(staged.zoneCode.empty() ? "." : QString::fromUtf8(staged.zoneCode.c_str()));
  connect(category, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [&, populateSystems](int) {
    populateSystems(category->currentData().toString());
    zoneCode->setText(staged.zoneCode.empty() ? "." : QString::fromUtf8(staged.zoneCode.c_str()));
  });
  connect(systems, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [&, updateDetails](int) {
    if (systems->currentIndex() >= 0) staged.zoneCode = systems->currentData().toString().toUtf8().toStdString();
    zoneCode->setText(staged.zoneCode.empty() ? "." : QString::fromUtf8(staged.zoneCode.c_str())); updateDetails();
  });
  connect(zoneCode, &QLineEdit::editingFinished, dialog, [&, populateSystems, updateDetails] {
    const auto typed = zoneCode->text().trimmed().toUtf8().toStdString();
    if (typed.empty() || typed == ".") { staged.zoneCode.clear(); category->setCurrentIndex(0); populateSystems(QString()); zoneCode->setText("."); return; }
    const auto info = geo::FindCoordinateSystem(typed);
    if (!info) { QMessageBox::warning(dialog, "Coordinate system", QString("\"%1\" is not a coordinate system in this dictionary.").arg(QString::fromUtf8(typed.c_str()))); zoneCode->setText(QString::fromUtf8(staged.zoneCode.c_str())); return; }
    staged.zoneCode = info->code; const QString cat = QString::fromUtf8(geo::CategoryOf(info->code).c_str());
    category->setCurrentIndex(category->findData(cat)); populateSystems(cat); systems->setCurrentIndex(systems->findData(QString::fromUtf8(info->code.c_str())));
    zoneCode->setText(QString::fromUtf8(info->code.c_str())); updateDetails();
  });
  zoneForm->addRow("Categories", category); zoneForm->addRow("Available coordinate systems", systems);
  zoneForm->addRow("Selected coordinate system code", zoneCode); zoneForm->addRow(zoneDetails);
  unitsForm->addRow(zoneGroup); unitsForm->addRow(zoneError);
  pages->addWidget(unitsPage);

  auto* transformPage = new QWidget(dialog); auto* transformForm = new QFormLayout(transformPage);
  auto* transformOn = new QCheckBox("Apply transform settings", transformPage); transformOn->setChecked(staged.transform.apply);
  auto* seaLevel = new QCheckBox("Apply sea level scale factor", transformPage); seaLevel->setChecked(staged.transform.applySeaLevel);
  auto number = [transformPage](const QString& name, double value) {
    auto* field = new QDoubleSpinBox(transformPage); field->setObjectName(name); field->setRange(-1.e12, 1.e12); field->setDecimals(8); field->setValue(value); return field;
  };
  auto* elevation = number("transformElevation", staged.transform.elevation);
  auto* radius = number("transformRadius", staged.transform.spheroidRadiusM);
  auto* computation = new QComboBox(transformPage); computation->addItems({"Reference Point", "User Defined"}); computation->setCurrentIndex(static_cast<int>(staged.transform.computation));
  auto* userScale = number("transformUserScale", staged.transform.userScaleFactor);
  auto* refLocalX = number("transformRefLocalX", staged.transform.refLocalX);
  auto* refLocalY = number("transformRefLocalY", staged.transform.refLocalY);
  auto* refGridE = number("transformRefGridE", staged.transform.refGridE);
  auto* refGridN = number("transformRefGridN", staged.transform.refGridN);
  auto* refPoint = new QSpinBox(transformPage); refPoint->setRange(0, INT_MAX); refPoint->setValue(staged.transform.refPointNumber);
  auto* rotation = new QComboBox(transformPage); rotation->addItems({"Rotation point", "Specify grid rotation angle"}); rotation->setCurrentIndex(static_cast<int>(staged.transform.rotation));
  auto* rotLocalX = number("transformRotLocalX", staged.transform.rotLocalX);
  auto* rotLocalY = number("transformRotLocalY", staged.transform.rotLocalY);
  auto* rotGridE = number("transformRotGridE", staged.transform.rotGridE);
  auto* rotGridN = number("transformRotGridN", staged.transform.rotGridN);
  auto* rotPoint = new QSpinBox(transformPage); rotPoint->setRange(0, INT_MAX); rotPoint->setValue(staged.transform.rotPointNumber);
  auto* toNorth = number("transformToNorth", staged.transform.toNorthDeg);
  auto* localAzimuth = number("transformLocalAzimuth", staged.transform.localAzimuthDeg);
  auto* gridAzimuth = number("transformGridAzimuth", staged.transform.gridAzimuthDeg);
  auto pickRow = [transformPage](QWidget* first, QWidget* second, QWidget* third, const QString& object) {
    auto* row = new QWidget(transformPage); auto* h = new QHBoxLayout(row); h->setContentsMargins(0,0,0,0);
    h->addWidget(first); if (second) h->addWidget(second); if (third) h->addWidget(third);
    auto* pick = new QPushButton("Pick", row); pick->setObjectName(object); h->addWidget(pick); return row;
  };
  auto* referenceRow = pickRow(refLocalX, refLocalY, refGridE, "pickTransformReference");
  static_cast<QHBoxLayout*>(referenceRow->layout())->addWidget(refGridN);
  static_cast<QHBoxLayout*>(referenceRow->layout())->addWidget(refPoint);
  auto* rotationRow = pickRow(rotLocalX, rotLocalY, rotGridE, "pickTransformRotation");
  static_cast<QHBoxLayout*>(rotationRow->layout())->addWidget(rotGridN);
  static_cast<QHBoxLayout*>(rotationRow->layout())->addWidget(rotPoint);
  auto* northRow = pickRow(toNorth, nullptr, nullptr, "pickTransformNorth");
  auto* localAzRow = pickRow(localAzimuth, gridAzimuth, nullptr, "pickTransformAzimuth");
  transformForm->addRow(transformOn); transformForm->addRow("Zone", new QLabel(staged.zoneCode.empty() ? geo::kNoZoneCategory : staged.zoneCode.c_str(), transformPage));
  transformForm->addRow(seaLevel); transformForm->addRow("Elevation", elevation); transformForm->addRow("Spheroid radius (m)", radius);
  transformForm->addRow("Computation", computation); transformForm->addRow("User grid scale factor", userScale);
  transformForm->addRow("Reference point — local X/Y, grid E/N, point #", referenceRow);
  transformForm->addRow("Rotation", rotation); transformForm->addRow("Rotation point — local X/Y, grid E/N, point #", rotationRow);
  transformForm->addRow("North direction", northRow); transformForm->addRow("Azimuth pair", localAzRow);
  pages->addWidget(transformPage);

  auto* layersPage = new QWidget(dialog); auto* layersLayout = new QVBoxLayout(layersPage);
  auto* layersTable = new QTableWidget(layersPage); layersTable->setObjectName("drawingObjectLayers");
  layersTable->setColumnCount(4); layersTable->setHorizontalHeaderLabels({"Object", "Layer", "Name modifier / value", "Locked"});
  const char* layerNames[] = {"Survey point", "Survey point label", "TIN surface", "Feature line", "Pipe run", "Pipe fitting", "Solid", "Table"};
  layersTable->setRowCount(kObjectLayerKindCount); layersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  layersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); layersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  layersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  std::array<QComboBox*, kObjectLayerKindCount> modifiers{};
  std::array<QComboBox*, kObjectLayerKindCount> layerFields{};
  std::array<QLineEdit*, kObjectLayerKindCount> modifierValues{};
  std::array<QCheckBox*, kObjectLayerKindCount> layerLocks{};
  for (int i = 0; i < kObjectLayerKindCount; ++i) {
    const auto& row = staged.objectLayers[static_cast<size_t>(i)];
    layersTable->setItem(i, 0, new QTableWidgetItem(layerNames[i])); layersTable->item(i,0)->setFlags(Qt::ItemIsEnabled);
    auto* layer = new QComboBox(layersTable); layer->setEditable(true); layer->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& entry : m_State.drawingLayerTable) layer->addItem(QString::fromUtf8(entry.name.c_str()));
    layer->setCurrentText(QString::fromUtf8(row.layer.c_str()));
    layerFields[static_cast<size_t>(i)] = layer; layersTable->setCellWidget(i, 1, layer);
    auto* cell = new QWidget(layersTable); auto* h = new QHBoxLayout(cell); h->setContentsMargins(0,0,0,0);
    auto* modifier = new QComboBox(cell); modifier->addItems({"None", "Prefix", "Suffix"}); modifier->setCurrentIndex(static_cast<int>(row.modifier));
    auto* value = new QLineEdit(QString::fromUtf8(row.value.c_str()), cell); value->setEnabled(row.modifier != ObjectLayerRow::Modifier::None);
    connect(modifier, qOverload<int>(&QComboBox::currentIndexChanged), value, [value](int index) { value->setEnabled(index != 0); });
    h->addWidget(modifier); h->addWidget(value, 1); layersTable->setCellWidget(i, 2, cell);
    modifiers[static_cast<size_t>(i)] = modifier; modifierValues[static_cast<size_t>(i)] = value;
    auto* locked = new QCheckBox(layersTable); locked->setChecked(row.locked);
    auto* lockCell = new QWidget(layersTable); auto* lockLayout = new QHBoxLayout(lockCell);
    lockLayout->setContentsMargins(0, 0, 0, 0); lockLayout->addStretch(); lockLayout->addWidget(locked); lockLayout->addStretch();
    layerLocks[static_cast<size_t>(i)] = locked; layersTable->setCellWidget(i, 3, lockCell);
  }
  layersLayout->addWidget(layersTable, 1);
  auto* layerHelp = new QLabel("Use * in a prefix or suffix value to insert the new object's name. Display component controls are not implemented yet.", layersPage);
  layerHelp->setWordWrap(true); layersLayout->addWidget(layerHelp); pages->addWidget(layersPage);
  connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

  auto* buttons = new QDialogButtonBox(dialog);
  auto* ok = buttons->addButton(QDialogButtonBox::Ok); auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  auto* apply = buttons->addButton(QDialogButtonBox::Apply);
  ok->setObjectName("drawingSettingsOk"); cancel->setObjectName("drawingSettingsCancel"); apply->setObjectName("drawingSettingsApply");
  root->addWidget(buttons);
  auto* pickTimer = new QTimer(dialog); pickTimer->setInterval(50);
  bool waitingForPick = false;
  auto beginPick = [this, dialog, pickTimer, &waitingForPick](AppCommandState::DrawingSettingsPickState::Target target) {
    if (!StartDrawingSettingsPick(m_State, target, m_Log)) { Refresh(); return; }
    waitingForPick = true; dialog->setWindowModality(Qt::NonModal); dialog->hide(); m_Viewport->setFocus(); pickTimer->start();
  };
  auto* pickRef = referenceRow->findChild<QPushButton*>("pickTransformReference");
  auto* pickRot = rotationRow->findChild<QPushButton*>("pickTransformRotation");
  auto* pickNorth = northRow->findChild<QPushButton*>("pickTransformNorth");
  auto* pickAz = localAzRow->findChild<QPushButton*>("pickTransformAzimuth");
  connect(pickRef, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::ReferencePoint); });
  connect(pickRot, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::RotationPoint); });
  connect(pickNorth, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::ToNorth); });
  connect(pickAz, &QPushButton::clicked, dialog, [&, beginPick] { beginPick(AppCommandState::DrawingSettingsPickState::Target::LocalAzimuth); });
  connect(pickTimer, &QTimer::timeout, dialog, [&, dialog, pickTimer] {
    if (m_State.active == AppCommandState::Kind::DrawingSettingsPick) return;
    pickTimer->stop(); if (!waitingForPick) return; waitingForPick = false;
    auto& result = m_State.drawingSettingsPick;
    if (result.done) {
      const auto target = result.target;
      if (target == AppCommandState::DrawingSettingsPickState::Target::ReferencePoint) {
        refLocalX->setValue(result.worldX); refLocalY->setValue(result.worldY); refPoint->setValue(result.pointNumber);
      } else if (target == AppCommandState::DrawingSettingsPickState::Target::RotationPoint) {
        rotLocalX->setValue(result.worldX); rotLocalY->setValue(result.worldY); rotPoint->setValue(result.pointNumber);
      } else if (target == AppCommandState::DrawingSettingsPickState::Target::ToNorth) toNorth->setValue(result.azimuthDeg);
      else if (target == AppCommandState::DrawingSettingsPickState::Target::LocalAzimuth) localAzimuth->setValue(result.azimuthDeg);
      result.done = false;
    }
    dialog->setWindowModality(Qt::WindowModal); dialog->show(); dialog->raise(); dialog->activateWindow(); Refresh();
  });
  auto applySettings = [&, dialog](bool closeAfter) {
    staged.angularUnits = static_cast<DrawingSettings::AngularUnits>(angular->currentIndex());
    staged.footDefinition = static_cast<DrawingSettings::FootDefinition>(foot->currentIndex());
    staged.scaleInsertedObjects = scaleInserted->isChecked(); staged.setDrawingVariables = setVariables->isChecked();
    if (scale->currentData().toInt() == -1 && customScale->value() <= 0) { QMessageBox::warning(dialog, "Drawing Settings", "The custom scale must be a positive number."); return; }
    const float plotScale = scale->currentData().toInt() == -1 ? static_cast<float>(customScale->value()) : scale->currentData().toFloat();
    auto& transform = staged.transform;
    transform.apply = transformOn->isChecked(); transform.applySeaLevel = seaLevel->isChecked(); transform.elevation = elevation->value();
    transform.spheroidRadiusM = radius->value(); transform.computation = static_cast<DrawingSettings::Transform::Computation>(computation->currentIndex());
    transform.userScaleFactor = userScale->value(); transform.refLocalX = refLocalX->value(); transform.refLocalY = refLocalY->value();
    transform.refGridE = refGridE->value(); transform.refGridN = refGridN->value(); transform.refPointNumber = refPoint->value();
    transform.rotation = static_cast<DrawingSettings::Transform::Rotation>(rotation->currentIndex());
    transform.rotLocalX = rotLocalX->value(); transform.rotLocalY = rotLocalY->value(); transform.rotGridE = rotGridE->value();
    transform.rotGridN = rotGridN->value(); transform.rotPointNumber = rotPoint->value(); transform.toNorthDeg = toNorth->value();
    transform.localAzimuthDeg = localAzimuth->value(); transform.gridAzimuthDeg = gridAzimuth->value();
    for (int i = 0; i < kObjectLayerKindCount; ++i) {
      auto& row = staged.objectLayers[static_cast<size_t>(i)];
      row.layer = layerFields[static_cast<size_t>(i)]->currentText().trimmed().toUtf8().toStdString();
      row.modifier = static_cast<ObjectLayerRow::Modifier>(modifiers[static_cast<size_t>(i)]->currentIndex());
      row.value = modifierValues[static_cast<size_t>(i)]->text().toUtf8().toStdString();
      row.locked = layerLocks[static_cast<size_t>(i)]->isChecked();
    }
    if (const auto error = ValidateObjectLayers(staged); !error.empty()) { QMessageBox::warning(dialog, "Object Layers", QString::fromUtf8(error.c_str())); return; }
    if (const auto error = ValidateDrawingTransform(staged); !error.empty()) { QMessageBox::warning(dialog, "Transformation", QString::fromUtf8(error.c_str())); return; }
    if (ApplyDrawingSettings(m_State, stagedUnits, plotScale, staged, m_Log)) { Refresh(); if (closeAfter) dialog->accept(); }
    else Refresh();
  };
  connect(ok, &QPushButton::clicked, dialog, [applySettings] { applySettings(true); });
  connect(apply, &QPushButton::clicked, dialog, [applySettings] { applySettings(false); });
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  m_ModalOpen = true;
  dialog->exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowTableStyles() {
  if (m_ModalOpen) return;
  m_ModalOpen = true;
  StyleManagerSpec spec;
  spec.objectName = "tableStyleManager"; spec.title = "Table Style";
  spec.currentCaption = "Current table style: ";
  spec.listObjectName = "tableStyleList"; spec.previewObjectName = "tableStylePreview"; spec.buttonPrefix = "tableStyle";
  spec.names = [this] { QStringList names; for (const auto& style : m_State.tableStyles) names << QString::fromUtf8(style.name.c_str()); return names; };
  spec.current = [this] { return QString::fromUtf8(m_State.tablePendingStyle.empty() ? "Standard" : m_State.tablePendingStyle.c_str()); };
  spec.preview = [this](const QString& name, QSize size, qreal dpr) {
    QPixmap pixmap(size * dpr); pixmap.setDevicePixelRatio(dpr); pixmap.fill(QColor("#202830"));
    QPainter painter(&pixmap);
    const auto* style = CadTableFindStyle(m_State.tableStyles, name.toUtf8().toStdString());
    if (!style) return pixmap;
    painter.setPen(QColor("#d9e0e5"));
    const QRectF frame(15, 15, size.width() - 30, size.height() - 30);
    painter.drawRect(frame);
    for (int row = 1; row < 4; ++row) painter.drawLine(QPointF(frame.left(), frame.top() + frame.height() * row / 4), QPointF(frame.right(), frame.top() + frame.height() * row / 4));
    for (int col = 1; col < 3; ++col) painter.drawLine(QPointF(frame.left() + frame.width() * col / 3, frame.top() + frame.height() / 4), QPointF(frame.left() + frame.width() * col / 3, frame.bottom()));
    painter.drawText(QRectF(frame.left(), frame.top(), frame.width(), frame.height() / 4), Qt::AlignCenter, name);
    for (int row = 1; row < 4; ++row) for (int col = 0; col < 3; ++col)
      painter.drawText(QRectF(frame.left() + frame.width() * col / 3, frame.top() + frame.height() * row / 4, frame.width() / 3, frame.height() / 4), Qt::AlignCenter, row == 1 ? "Header" : "Data");
    return pixmap;
  };
  spec.describe = [this](const QString& name) {
    const auto* style = CadTableFindStyle(m_State.tableStyles, name.toUtf8().toStdString());
    return style ? QString("%1\nText height: %2 · Column width: %3 · Row height: %4")
        .arg(name).arg(style->textHeightInches).arg(style->columnWidthInches).arg(style->rowHeightInches) : QString();
  };
  const auto edit = [this](CadTableStyle* style, bool create) {
    QStringList names; for (const auto& text : m_State.textStyles) names << QString::fromUtf8(text.name.c_str());
    return EditTableStyleDialog(this, style, create, names);
  };
  spec.setCurrent = [this](const QString& name) { PushUndoSnapshot(m_State, "TABLESTYLE current"); m_State.tablePendingStyle = name.toUtf8().toStdString(); };
  spec.create = [this, edit] {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Table Style", "New style name:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty()) return false;
    if (CadTableFindStyle(m_State.tableStyles, name.toUtf8().toStdString())) {
      QMessageBox::warning(this, "Table Style", "That style name already exists."); return false;
    }
    const auto* current = CadTableFindStyle(m_State.tableStyles, m_State.tablePendingStyle);
    CadTableStyle style = current ? *current : CadTableBuiltinStyles().front();
    style.name = name.toUtf8().toStdString();
    if (!edit(&style, true)) return false;
    if (CadTableFindStyle(m_State.tableStyles, style.name)) { QMessageBox::warning(this, "Table Style", "That style name already exists."); return false; }
    PushUndoSnapshot(m_State, "TABLESTYLE new"); m_State.tableStyles.push_back(std::move(style)); return true;
  };
  spec.modify = [this, edit](const QString& name) {
    const std::string key = name.toUtf8().toStdString();
    const auto* original = CadTableFindStyle(m_State.tableStyles, key);
    if (!original) return false;
    CadTableStyle style = *original;
    if (!edit(&style, false)) return false;
    PushUndoSnapshot(m_State, "TABLESTYLE modify");
    auto found = std::find_if(m_State.tableStyles.begin(), m_State.tableStyles.end(), [&](const auto& item) { return item.name == key; });
    if (found != m_State.tableStyles.end()) *found = std::move(style);
    return true;
  };
  spec.copy = [this](const QString& name) {
    const auto* original = CadTableFindStyle(m_State.tableStyles, name.toUtf8().toStdString());
    if (!original) return false;
    bool ok = false;
    const QString newName = QInputDialog::getText(this, "Copy Table Style", "Name:", QLineEdit::Normal, name + " Copy", &ok).trimmed();
    if (!ok || newName.isEmpty()) return false;
    if (CadTableFindStyle(m_State.tableStyles, newName.toUtf8().toStdString())) {
      QMessageBox::warning(this, "Table Style", "That style name already exists."); return false;
    }
    CadTableStyle copy = *original; copy.name = newName.toUtf8().toStdString();
    PushUndoSnapshot(m_State, "TABLESTYLE copy"); m_State.tableStyles.push_back(std::move(copy)); return true;
  };
  spec.rename = [this](const QString& name) {
    const auto* original = CadTableFindStyle(m_State.tableStyles, name.toUtf8().toStdString());
    if (!original) return false;
    bool ok = false;
    const QString newName = QInputDialog::getText(this, "Rename Table Style", "Name:", QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == name) return false;
    if (const auto* other = CadTableFindStyle(m_State.tableStyles, newName.toUtf8().toStdString()); other && other != original) {
      QMessageBox::warning(this, "Table Style", "That style name already exists."); return false;
    }
    const std::string oldKey = original->name, newKey = newName.toUtf8().toStdString();
    PushUndoSnapshot(m_State, "TABLESTYLE rename");
    for (auto& style : m_State.tableStyles) if (style.name == oldKey) style.name = newKey;
    for (auto& table : m_State.cadTables) if (CadTableNameEquals(table.styleName, oldKey)) table.styleName = newKey;
    if (CadTableNameEquals(m_State.tablePendingStyle, oldKey)) m_State.tablePendingStyle = newKey;
    return true;
  };
  spec.remove = [this](const QString& name) {
    const std::string key = name.toUtf8().toStdString();
    if (CadTableNameEquals(key, "Standard") || CadTableNameEquals(key, m_State.tablePendingStyle) ||
        std::any_of(m_State.cadTables.begin(), m_State.cadTables.end(), [&](const auto& table) { return CadTableNameEquals(table.styleName, key); })) {
      QMessageBox::information(this, "Table Style", "This style is current, Standard, or used by a table. Reassign its tables and choose another current style before deleting it.");
      return false;
    }
    PushUndoSnapshot(m_State, "TABLESTYLE delete");
    m_State.tableStyles.erase(std::remove_if(m_State.tableStyles.begin(), m_State.tableStyles.end(),
        [&](const auto& style) { return CadTableNameEquals(style.name, key); }), m_State.tableStyles.end());
    return true;
  };
  RunStyleManager(this, spec);
  m_ModalOpen = false; Refresh();
}
