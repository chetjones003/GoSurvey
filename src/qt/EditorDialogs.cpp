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
  auto applyAll = [this, commits, saveStatus] {
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
  if (m_State.showDimStyleDialog) {
    m_State.showDimStyleDialog = false;
    m_ModalOpen = true;
    auto editStyle = [this](DimensionStyle* style, bool create) {
      if (!style) return false;
      QDialog edit(this);
      edit.setObjectName("dimensionStyleEditor");
      edit.setWindowTitle((create ? "New Dimension Style" : "Modify Dimension Style: ") +
                          QString::fromUtf8(style->name.c_str()));
      edit.resize(820, 560);
      edit.setStyleSheet("QDialog{background:#f1f1f1;color:#202020;} QTabWidget::pane{border:1px solid #b8b8b8;background:#fafafa;} QTabBar::tab{padding:6px 9px;color:#202020;} QGroupBox{border:1px solid #c8c8c8;margin-top:8px;padding:8px;} QLabel{color:#202020;}");
      auto* root = new QVBoxLayout(&edit);
      auto* name = new QLineEdit(QString::fromUtf8(style->name.c_str()), &edit);
      name->setObjectName("dimensionStyleName");
      if (!create) name->setReadOnly(true);
      root->addWidget(name);
      auto* tabs = new QTabWidget(&edit);
      const auto page = [&](const char* title) {
        auto* w = new QWidget(tabs);
        auto* layout = new QFormLayout(w);
        tabs->addTab(w, title);
        return layout;
      };
      auto* lines = page("Lines");
      auto* dimColor = new QLineEdit(QString::fromUtf8(style->dimLineColor.c_str()), &edit);
      dimColor->setObjectName("dimensionLineColor"); lines->addRow("Dimension line color:", dimColor);
      auto* dimType = new QLineEdit(QString::fromUtf8(style->dimLineType.c_str()), &edit);
      lines->addRow("Dimension line linetype:", dimType);
      auto* extColor = new QLineEdit(QString::fromUtf8(style->extLineColor.c_str()), &edit);
      lines->addRow("Extension line color:", extColor);
      auto* extType = new QLineEdit(QString::fromUtf8(style->extLineType.c_str()), &edit);
      lines->addRow("Extension line linetype:", extType);
      auto* symbols = page("Symbols and Arrows");
      auto* arrow = new QComboBox(&edit);
      for (int i = 0; i <= static_cast<int>(DimArrowType::None); ++i)
        arrow->addItem(DimArrowTypeName(static_cast<DimArrowType>(i)), i);
      arrow->setCurrentIndex(static_cast<int>(style->arrowType));
      symbols->addRow("Arrowhead:", arrow);
      auto* arrowSize = new QDoubleSpinBox(&edit); arrowSize->setRange(0.001, 100000); arrowSize->setDecimals(4); arrowSize->setValue(style->arrowSizeInches);
      symbols->addRow("Arrow size (in):", arrowSize);
      auto* text = page("Text");
      auto* font = new QComboBox(&edit);
      for (const TextStyle& ts : m_State.textStyles) font->addItem(QString::fromUtf8(ts.name.c_str()));
      if (font->findText(QString::fromUtf8(style->textFont.c_str())) < 0 && !style->textFont.empty()) font->addItem(QString::fromUtf8(style->textFont.c_str()));
      font->setCurrentText(style->textFont.empty() ? QStringLiteral("Standard") : QString::fromUtf8(style->textFont.c_str()));
      text->addRow("Text style:", font);
      auto* textHeight = new QDoubleSpinBox(&edit); textHeight->setRange(0.001, 100000); textHeight->setDecimals(4); textHeight->setValue(style->textSizeInches);
      text->addRow("Text height (in):", textHeight);
      auto* textColor = new QLineEdit(QString::fromUtf8(style->textColor.c_str()), &edit); text->addRow("Text color:", textColor);
      auto* alignment = new QComboBox(&edit); alignment->addItems({"Centered", "Above", "Beside"}); alignment->setCurrentIndex(static_cast<int>(style->textAlign)); text->addRow("Placement:", alignment);
      auto* fit = page("Fit"); auto* fitHint = new QLabel("Dimensions use the drawing scale and keep text and arrows inside when space allows.", &edit); fit->addRow(fitHint);
      auto* units = page("Primary Units"); auto* unitFormat = new QComboBox(&edit); for (int i=0;i<=static_cast<int>(DimUnitFormat::Fractional);++i) unitFormat->addItem(DimUnitFormatName(static_cast<DimUnitFormat>(i)), i); unitFormat->setCurrentIndex(static_cast<int>(style->unitFormat)); units->addRow("Unit format:", unitFormat);
      auto* precision = new QSpinBox(&edit); precision->setRange(0, 8); precision->setValue(style->unitPrecision); units->addRow("Precision:", precision);
      auto* scale = new QDoubleSpinBox(&edit); scale->setRange(0.000001, 100000); scale->setDecimals(6); scale->setValue(style->unitScale); units->addRow("Scale factor:", scale);
      auto* alternate = page("Alternate Units"); alternate->addRow(new QLabel("Alternate unit fields are retained from DWG and will be shown when this drawing supports them.", &edit));
      auto* tolerances = page("Tolerances"); tolerances->addRow(new QLabel("Tolerance fields are retained from DWG and will be shown when this drawing supports them.", &edit));
      root->addWidget(tabs, 1);
      auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, &edit);
      root->addWidget(buttons);
      QObject::connect(buttons, &QDialogButtonBox::accepted, &edit, &QDialog::accept);
      QObject::connect(buttons, &QDialogButtonBox::rejected, &edit, &QDialog::reject);
      if (edit.exec() != QDialog::Accepted) return false;
      const QString enteredName = name->text().trimmed();
      if (enteredName.isEmpty()) return false;
      style->name = enteredName.toUtf8().toStdString();
      style->dimLineColor = dimColor->text().toUtf8().toStdString(); style->dimLineType = dimType->text().toUtf8().toStdString();
      style->extLineColor = extColor->text().toUtf8().toStdString(); style->extLineType = extType->text().toUtf8().toStdString();
      style->arrowType = static_cast<DimArrowType>(arrow->currentData().toInt()); style->arrowSizeInches = static_cast<float>(arrowSize->value());
      style->textFont = font->currentText() == "Standard" ? std::string() : font->currentText().toUtf8().toStdString();
      style->textSizeInches = static_cast<float>(textHeight->value()); style->textColor = textColor->text().toUtf8().toStdString();
      style->textAlign = static_cast<DimTextAlign>(alignment->currentIndex());
      style->unitFormat = static_cast<DimUnitFormat>(unitFormat->currentData().toInt()); style->unitPrecision = precision->value(); style->unitScale = scale->value();
      return true;
    };
    if (m_State.dimensionStyles.empty()) m_State.dimensionStyles = DimensionStyles::DefaultStyles();
    QDialog manager(this);
    manager.setObjectName("dimensionStyleManager"); manager.setWindowTitle("Dimension Style Manager"); manager.resize(760, 530);
    manager.setStyleSheet("QDialog{background:#f1f1f1;color:#202020;} QLabel{color:#202020;} QListWidget{background:white;color:#202020;border:1px solid #aab1b5;} QPushButton{min-height:27px;color:#202020;background:#f8f8f8;border:1px solid #9ca3a8;padding:3px 8px;} QPushButton:hover{background:#e7edf2;}");
    auto* root = new QVBoxLayout(&manager);
    root->addWidget(new QLabel("Current dimension style: " + QString::fromUtf8(m_State.activeDimensionStyle.name.c_str()), &manager));
    auto* body = new QHBoxLayout;
    auto* list = new QListWidget(&manager); list->setObjectName("dimensionStyleList"); list->setMinimumWidth(240);
    for (const DimensionStyle& style : m_State.dimensionStyles) list->addItem(QString::fromUtf8(style.name.c_str()));
    list->setCurrentRow(std::max(0, list->row(list->findItems(QString::fromUtf8(m_State.activeDimensionStyle.name.c_str()), Qt::MatchExactly).value(0))));
    body->addWidget(list, 1);
    auto* details = new QVBoxLayout;
    auto* preview = new QLabel(QString("Preview of: %1\n\n←──── 25.40 ────→\n       ╲ 45°\n      ◯───────╱").arg(QString::fromUtf8(m_State.activeDimensionStyle.name.c_str())), &manager);
    preview->setObjectName("dimensionStylePreview"); preview->setAlignment(Qt::AlignCenter); preview->setMinimumSize(290, 220); preview->setStyleSheet("background:#202830;color:#e8a61a;font:16px monospace;border:1px solid #8d9295;");
    details->addWidget(preview, 1); details->addWidget(new QLabel("Description", &manager)); details->addWidget(new QLabel("Dimension style settings for this drawing.", &manager));
    body->addLayout(details, 2);
    auto* actions = new QVBoxLayout;
    auto* setCurrent = new QPushButton("Set Current", &manager); setCurrent->setObjectName("dimensionStyleSetCurrent"); actions->addWidget(setCurrent);
    auto* create = new QPushButton("New…", &manager); create->setObjectName("dimensionStyleNew"); actions->addWidget(create);
    auto* modify = new QPushButton("Modify…", &manager); modify->setObjectName("dimensionStyleModify"); actions->addWidget(modify);
    auto* override = new QPushButton("Override…", &manager); override->setObjectName("dimensionStyleOverride"); actions->addWidget(override);
    auto* compare = new QPushButton("Compare…", &manager); compare->setObjectName("dimensionStyleCompare"); actions->addWidget(compare); actions->addStretch();
    body->addLayout(actions); root->addLayout(body, 1);
    auto* bottom = new QHBoxLayout; bottom->addStretch(); auto* close = new QPushButton("Close", &manager); auto* help = new QPushButton("Help", &manager); bottom->addWidget(close); bottom->addWidget(help); root->addLayout(bottom);
    auto selectedName = [&]() { return list->currentItem() ? list->currentItem()->text().toUtf8().toStdString() : std::string(); };
    QObject::connect(list, &QListWidget::currentRowChanged, &manager, [&, preview](int row) { if (row >= 0 && row < static_cast<int>(m_State.dimensionStyles.size())) preview->setText(QString("Preview of: %1\n\n←──── 25.40 ────→\n       ╲ 45°\n      ◯───────╱").arg(QString::fromUtf8(m_State.dimensionStyles[static_cast<std::size_t>(row)].name.c_str()))); });
    QObject::connect(setCurrent, &QPushButton::clicked, &manager, [&] { const auto n = selectedName(); if (const auto* style = DimensionStyles::Find(m_State.dimensionStyles, n)) { PushUndoSnapshot(m_State, "DIMSTY current"); m_State.activeDimensionStyle = *style; manager.accept(); } });
    QObject::connect(modify, &QPushButton::clicked, &manager, [&] { const auto n = selectedName(); auto it = std::find_if(m_State.dimensionStyles.begin(), m_State.dimensionStyles.end(), [&](const auto& s) { return s.name == n; }); if (it != m_State.dimensionStyles.end()) { DimensionStyle edited = *it; if (editStyle(&edited, false)) { PushUndoSnapshot(m_State, "DIMSTY modify"); *it = std::move(edited); if (m_State.activeDimensionStyle.name == n) m_State.activeDimensionStyle = *it; if (list->currentItem()) list->currentItem()->setText(QString::fromUtf8(it->name.c_str())); } } });
    QObject::connect(create, &QPushButton::clicked, &manager, [&] { bool ok = false; const QString n = QInputDialog::getText(&manager, "New Dimension Style", "New style name:", QLineEdit::Normal, "", &ok).trimmed(); if (!ok || n.isEmpty()) return; if (DimensionStyles::Find(m_State.dimensionStyles, n.toUtf8().toStdString())) { QMessageBox::warning(&manager, "Dimension Style", "That style name already exists."); return; } DimensionStyle copy = m_State.activeDimensionStyle; copy.name = n.toUtf8().toStdString(); if (editStyle(&copy, true)) { PushUndoSnapshot(m_State, "DIMSTY new"); m_State.dimensionStyles.push_back(copy); list->addItem(n); list->setCurrentRow(list->count() - 1); } });
    QObject::connect(override, &QPushButton::clicked, &manager, [&] { QMessageBox::information(&manager, "Dimension Style", "Overrides are not available for this style model."); });
    QObject::connect(compare, &QPushButton::clicked, &manager, [&] { QMessageBox::information(&manager, "Dimension Style", "Select Modify to inspect and edit supported style values."); });
    QObject::connect(close, &QPushButton::clicked, &manager, &QDialog::accept);
    manager.exec();
    m_State.showDimStyleDialog = false; m_ModalOpen = false; Refresh(); return;
  }
  if (m_State.showMleaderStyleDialog) {
    m_State.showMleaderStyleDialog = false;
    m_ModalOpen = true;
    QDialog dialog(this);
    dialog.setObjectName("mleaderStyleDialog");
    dialog.setWindowTitle("Multileader Style: " + QString::fromUtf8(m_State.activeMultileaderStyle.name.c_str()));
    auto* form = new QFormLayout(&dialog);
    auto* textHeight = new QDoubleSpinBox(&dialog);
    textHeight->setObjectName("mleaderTextHeight");
    textHeight->setRange(0.001, 100000.0);
    textHeight->setDecimals(4);
    textHeight->setValue(m_State.activeMultileaderStyle.textSizeInches);
    form->addRow("Text height (in):", textHeight);
    auto* arrowSize = new QDoubleSpinBox(&dialog);
    arrowSize->setObjectName("mleaderArrowSize");
    arrowSize->setRange(0.001, 100000.0);
    arrowSize->setDecimals(4);
    arrowSize->setValue(m_State.activeMultileaderStyle.arrowSizeInches);
    form->addRow("Arrow size (in):", arrowSize);
    auto* landingGap = new QDoubleSpinBox(&dialog);
    landingGap->setObjectName("mleaderLandingGap");
    landingGap->setRange(0.0, 100000.0);
    landingGap->setDecimals(4);
    landingGap->setValue(m_State.activeMultileaderStyle.landingGapInches);
    form->addRow("Landing gap (in):", landingGap);
    auto* annotative = new QCheckBox("Annotative by default", &dialog);
    annotative->setObjectName("mleaderAnnotativeDefault");
    annotative->setChecked(m_State.activeMultileaderStyle.annotativeDefault);
    form->addRow(annotative);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
      PushUndoSnapshot(m_State, "MSTY");
      m_State.activeMultileaderStyle.textSizeInches = static_cast<float>(textHeight->value());
      m_State.activeMultileaderStyle.arrowSizeInches = static_cast<float>(arrowSize->value());
      m_State.activeMultileaderStyle.landingGapInches = static_cast<float>(landingGap->value());
      m_State.activeMultileaderStyle.annotativeDefault = annotative->isChecked();
      auto it = std::find_if(m_State.multileaderStyles.begin(), m_State.multileaderStyles.end(), [&](const MultileaderStyle& style) {
        return style.name == m_State.activeMultileaderStyle.name;
      });
      if (it == m_State.multileaderStyles.end())
        m_State.multileaderStyles.push_back(m_State.activeMultileaderStyle);
      else
        *it = m_State.activeMultileaderStyle;
    }
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
