#include "EditorWindow.hpp"
#include "QtAccent.hpp"

#include "ProjectSettings.hpp"
#include "geo/CoordinateSystems.hpp"
#include "io/Project.hpp"
#include "platform/AppPaths.hpp"
#include "util/PlotScales.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPalette>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace {
class ProjectScaleSpinBox final : public QDoubleSpinBox {
 public:
  using QDoubleSpinBox::QDoubleSpinBox;
 protected:
  QString textFromValue(double value) const override { return locale().toString(value, 'g', 7); }
};

constexpr std::array<const char*, 5> kUnitNames = {"Unitless", "Inches", "Feet", "Meters", "Millimeters"};
constexpr std::array<int, 5> kUnitCodes = {0, 1, 2, 6, 4};
constexpr std::array<const char*, 3> kAngularNames = {"Degrees", "Radians", "Grads"};
constexpr std::array<const char*, 2> kFootNames = {"US Survey Foot", "International Foot"};
constexpr std::array<const char*, kObjectLayerKindCount> kObjectLayerNames = {
    "Survey point", "Survey point label", "TIN surface", "Feature line",
    "Pipe run", "Pipe fitting", "Solid", "Table"};

QLabel* SectionTitle(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setObjectName("projectSectionTitle");
  label->setStyleSheet("font-size:14px;font-weight:600;");
  return label;
}

void AddLabeledControl(QGridLayout* layout, QWidget* parent, int row, const QString& labelText,
                      QWidget* control, const QString& statusText = {}) {
  auto* label = new QLabel(labelText, parent);
  label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  layout->addWidget(label, row, 0);
  layout->addWidget(control, row, 1, 1, statusText.isEmpty() ? 2 : 1);
  if (!statusText.isEmpty()) {
    auto* status = new QLabel(statusText, parent);
    status->setObjectName("projectSettingStatus");
    const bool dark = parent->palette().color(QPalette::Window).lightness() < 128;
    status->setToolTip("This value is controlled by the project and applies to every drawing in it.");
    status->setAlignment(Qt::AlignCenter);
    status->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    status->setStyleSheet(QString("color:%1;background:%2;font-size:11px;font-weight:600;padding:2px 7px;border:1px solid %3;border-radius:9px;")
        .arg(QtAccentFor(dark).link, dark ? "#40392e" : "#fff5df", dark ? "#66502e" : "#d2b27d"));
    layout->addWidget(status, row, 2);
  }
}

QString Utf8(const std::string& text) { return QString::fromUtf8(text.c_str()); }

std::string Utf8String(const QString& text) { return text.toUtf8().toStdString(); }
}

void EditorWindow::ShowProjectSettings(std::uint32_t projectUid) {
  auto findSession = [this, projectUid]() -> AppCommandState::ProjectSession* {
    const auto found = std::find_if(m_State.openProjects.begin(), m_State.openProjects.end(),
        [projectUid](const auto& session) { return session.uid == projectUid; });
    return found == m_State.openProjects.end() ? nullptr : &*found;
  };
  auto* session = findSession();
  if (!session || !session->settings) {
    QMessageBox::warning(this, "Project Settings", "This project is no longer open.");
    return;
  }

  LoadCoordinateSystemDictionaries();

  ProjectSettings staged = *session->settings;
  staged.defaults.overridden = 0;
  const std::string originalZone = staged.zoneCode;
  const bool darkTheme = palette().color(QPalette::Window).lightness() < 128;
  const QString secondaryText = darkTheme ? "#c3c7ca" : "#536273";
  const QString accentText = darkTheme ? "#d0d3d5" : QString(QtAccentFor(false).link);
  QDialog dialog(this);
  dialog.setObjectName("projectSettingsDialog");
  dialog.setWindowTitle("Project Settings — " + Utf8(session->project.name));
  dialog.setWindowModality(Qt::WindowModal);
  dialog.resize(1080, 720);
  dialog.setMinimumSize(960, 650);
  QString projectStyle = darkTheme ? QString::fromLatin1(R"(
    QDialog#projectSettingsDialog { background:#1c2024; color:#edf2f7; }
    QListWidget#projectSettingsNavigation { background:#202529; border:1px solid #3a4247; border-radius:6px; padding:5px; }
    QListWidget#projectSettingsNavigation::item { min-height:30px; padding:3px 7px; border-radius:4px; }
    QListWidget#projectSettingsNavigation::item:selected { background:#a95a08; color:#ffffff; }
    QListWidget#projectSettingsNavigation::item:hover:!selected { background:#30373c; }
    QFrame#projectSettingsImpact, QFrame#projectSettingsDefaults { background:#242a2f; border:1px solid #3a4247; border-radius:6px; }
    QComboBox, QLineEdit, QAbstractSpinBox { background:#15191c; color:#edf2f7; border:1px solid #424b51; border-radius:4px; padding:4px 7px; selection-background-color:#343b40; selection-color:white; }
    QComboBox:hover, QLineEdit:hover, QAbstractSpinBox:hover { border-color:#7c8a92; }
    QComboBox:focus, QLineEdit:focus, QAbstractSpinBox:focus { border:1px solid #f29b44; }
    QComboBox:disabled, QLineEdit:disabled, QAbstractSpinBox:disabled { background:#202529; color:#a5afb6; border-color:#353d42; }
    QTableWidget#projectObjectLayers { background:#15191c; alternate-background-color:#202529; color:#edf2f7; gridline-color:#3a4247; border:1px solid #3a4247; }
    QHeaderView::section { background:#292f34; color:#f2f6fa; border:0; border-right:1px solid #3a4247; border-bottom:1px solid #41494e; padding:6px; }
    QCheckBox { spacing:7px; }
    QPushButton { padding:5px 12px; }
    QWidget:disabled { color:#a9afb3; }
  )") : QtApplyAccent(R"(
    QDialog#projectSettingsDialog { background:#f0f3f6; color:#202b36; }
    QListWidget#projectSettingsNavigation { background:#e4eaf0; border:1px solid #bdc8d3; border-radius:6px; padding:5px; }
    QListWidget#projectSettingsNavigation::item { min-height:30px; padding:3px 7px; border-radius:4px; }
    QListWidget#projectSettingsNavigation::item:selected { background:@fill@; color:#ffffff; }
    QListWidget#projectSettingsNavigation::item:hover:!selected { background:#d3dde7; }
    QFrame#projectSettingsImpact, QFrame#projectSettingsDefaults { background:#ffffff; border:1px solid #c5d0da; border-radius:6px; }
    QComboBox, QLineEdit, QAbstractSpinBox { background:#ffffff; color:#202b36; border:1px solid #aebbc8; border-radius:4px; padding:4px 7px; selection-background-color:@fill@; selection-color:white; }
    QComboBox:hover, QLineEdit:hover, QAbstractSpinBox:hover { border-color:@edge@; }
    QComboBox:focus, QLineEdit:focus, QAbstractSpinBox:focus { border:1px solid @strong@; }
    QComboBox:disabled, QLineEdit:disabled, QAbstractSpinBox:disabled { background:#e7ebef; color:#697786; border-color:#c5cdd5; }
    QTableWidget#projectObjectLayers { background:#ffffff; alternate-background-color:#f0f4f7; color:#202b36; gridline-color:#c5d0da; border:1px solid #bdc8d3; }
    QHeaderView::section { background:#e1e8ee; color:#202b36; border:0; border-right:1px solid #c5d0da; border-bottom:1px solid #bdc8d3; padding:6px; }
    QCheckBox { spacing:7px; }
    QPushButton { padding:5px 12px; }
  )", false);
  projectStyle += QtInputSubcontrolStyle(darkTheme) + QtCheckBoxIndicatorStyle(darkTheme);
  projectStyle += darkTheme
      ? "QPushButton[projectPrimaryAction=\"true\"]{background:#a95a08;color:white;border:1px solid #f29b44;font-weight:600;padding:6px 14px;}"
        "QPushButton[projectPrimaryAction=\"true\"]:hover{background:#b5600a;}"
      : "QPushButton[projectPrimaryAction=\"true\"]{background:#a95a08;color:white;border:1px solid #9d5407;font-weight:600;padding:6px 14px;}"
        "QPushButton[projectPrimaryAction=\"true\"]:hover{background:#8f4c06;}";
  dialog.setStyleSheet(projectStyle);

  auto* outer = new QVBoxLayout(&dialog);
  outer->setContentsMargins(22, 18, 22, 16);
  outer->setSpacing(12);
  auto* heading = SectionTitle("Project Settings", &dialog);
  heading->setObjectName("projectSettingsHeading");
  outer->addWidget(heading);

  auto* body = new QHBoxLayout();
  body->setSpacing(14);
  outer->addLayout(body, 1);

  auto* navigation = new QListWidget(&dialog);
  navigation->setObjectName("projectSettingsNavigation");
  navigation->setFixedWidth(170);
  navigation->addItem("Units and Zone");
  navigation->addItem("Object Layers");
  navigation->setCurrentRow(0);
  body->addWidget(navigation);

  auto* pages = new QStackedWidget(&dialog);
  pages->setObjectName("projectSettingsPages");
  body->addWidget(pages, 1);

  auto* impact = new QFrame(&dialog);
  impact->setObjectName("projectSettingsImpact");
  impact->setFrameShape(QFrame::StyledPanel);
  impact->setFixedWidth(205);
  auto* impactLayout = new QVBoxLayout(impact);
  impactLayout->setContentsMargins(14, 14, 14, 14);
  auto* impactTitle = SectionTitle("Project-wide effect", impact);
  impactLayout->addWidget(impactTitle);
  auto* enforcedNote = new QLabel("Linear units and the coordinate system are enforced in every project drawing.", impact);
  enforcedNote->setWordWrap(true);
  impactLayout->addWidget(enforcedNote);
  auto* defaultsNote = new QLabel("Project defaults flow to drawings that have not overridden them.", impact);
  defaultsNote->setWordWrap(true);
  impactLayout->addWidget(defaultsNote);
  auto* saveNote = new QLabel(session->readOnly
      ? "This project is read-only. Changes cannot be saved."
      : "Changes stay staged until you choose Apply or OK.", impact);
  saveNote->setObjectName("projectSettingsSaveNote");
  saveNote->setWordWrap(true);
  saveNote->setStyleSheet(session->readOnly ? "color:#d98b82;" : QString("color:%1;").arg(secondaryText));
  impactLayout->addSpacing(8);
  impactLayout->addWidget(saveNote);
  impactLayout->addStretch();
  body->addWidget(impact);

  // Units and Zone page: values are staged together so Cancel cannot partially update the project.
  auto* unitsPage = new QWidget(pages);
  auto* unitsLayout = new QVBoxLayout(unitsPage);
  unitsLayout->setContentsMargins(12, 10, 12, 10);
  unitsLayout->setSpacing(9);
  unitsLayout->addWidget(SectionTitle("Coordinate system", unitsPage));
  auto* enforcedText = new QLabel("These two values are enforced by the project.", unitsPage);
  enforcedText->setStyleSheet(QString("color:%1;").arg(secondaryText));
  unitsLayout->addWidget(enforcedText);

  auto* coordinateGrid = new QGridLayout();
  coordinateGrid->setContentsMargins(12, 0, 12, 0);
  coordinateGrid->setColumnStretch(1, 1);
  coordinateGrid->setColumnMinimumWidth(0, 170);
  coordinateGrid->setColumnMinimumWidth(2, 90);
  auto* units = new QComboBox(unitsPage);
  units->setObjectName("projectLinearUnits");
  for (size_t i = 0; i < kUnitNames.size(); ++i)
    units->addItem(kUnitNames[i], kUnitCodes[i]);
  const auto unitAt = std::find(kUnitCodes.begin(), kUnitCodes.end(), staged.insUnits);
  units->setCurrentIndex(unitAt == kUnitCodes.end() ? 2 : static_cast<int>(unitAt - kUnitCodes.begin()));
  AddLabeledControl(coordinateGrid, unitsPage, 0, "Linear units", units, "Enforced");
  auto* category = new QComboBox(unitsPage);
  category->setObjectName("projectZoneCategory");
  category->addItem(geo::kNoZoneCategory, QString());
  for (const auto& value : geo::Categories())
    category->addItem(Utf8(value), Utf8(value));
  auto* system = new QComboBox(unitsPage);
  system->setObjectName("projectCoordinateSystem");
  auto* zoneCode = new QLineEdit(Utf8(staged.zoneCode), unitsPage);
  zoneCode->setObjectName("projectZoneCode");
  zoneCode->setPlaceholderText("Enter a coordinate-system code");
  zoneCode->setToolTip("Select a coordinate system above or enter its code here.");
  auto* zoneDescription = new QLabel(unitsPage);
  zoneDescription->setObjectName("projectZoneDescription");
  auto* zoneProjection = new QLabel(unitsPage);
  zoneProjection->setObjectName("projectZoneProjection");
  auto* zoneDatum = new QLabel(unitsPage);
  zoneDatum->setObjectName("projectZoneDatum");
  auto* zoneError = new QLabel(unitsPage);
  zoneError->setObjectName("projectZoneError");
  zoneError->setStyleSheet("color:#b23b35;");
  zoneError->setWordWrap(true);
  AddLabeledControl(coordinateGrid, unitsPage, 1, "Category", category);
  AddLabeledControl(coordinateGrid, unitsPage, 2, "Available coordinate systems", system);
  AddLabeledControl(coordinateGrid, unitsPage, 3, "Selected system code", zoneCode, "Enforced");
  AddLabeledControl(coordinateGrid, unitsPage, 4, "Description", zoneDescription);
  AddLabeledControl(coordinateGrid, unitsPage, 5, "Projection", zoneProjection);
  AddLabeledControl(coordinateGrid, unitsPage, 6, "Datum", zoneDatum);
  coordinateGrid->addWidget(zoneError, 7, 1, 1, 2);
  unitsLayout->addLayout(coordinateGrid);

  auto* defaultsFrame = new QFrame(unitsPage);
  defaultsFrame->setObjectName("projectSettingsDefaults");
  defaultsFrame->setFrameShape(QFrame::StyledPanel);
  auto* defaultsLayout = new QVBoxLayout(defaultsFrame);
  defaultsLayout->setContentsMargins(12, 10, 12, 10);
  defaultsLayout->addWidget(SectionTitle("Defaults for project drawings", defaultsFrame));
  auto* defaultsGrid = new QGridLayout();
  defaultsGrid->setHorizontalSpacing(8);
  defaultsGrid->setVerticalSpacing(8);
  defaultsGrid->setColumnMinimumWidth(0, 170);
  defaultsGrid->setColumnStretch(1, 1);
  defaultsGrid->setColumnMinimumWidth(2, 90);
  defaultsGrid->setColumnStretch(3, 1);
  auto* angular = new QComboBox(defaultsFrame);
  angular->setObjectName("projectAngularUnits");
  for (size_t i = 0; i < kAngularNames.size(); ++i) angular->addItem(kAngularNames[i], static_cast<int>(i));
  angular->setCurrentIndex(static_cast<int>(staged.defaults.angularUnits));
  auto* foot = new QComboBox(defaultsFrame);
  foot->setObjectName("projectFootDefinition");
  for (size_t i = 0; i < kFootNames.size(); ++i) foot->addItem(kFootNames[i], static_cast<int>(i));
  foot->setCurrentIndex(static_cast<int>(staged.defaults.footDefinition));
  auto* plotScale = new QComboBox(defaultsFrame);
  plotScale->setObjectName("projectPlotScale");
  auto* customScale = new ProjectScaleSpinBox(defaultsFrame);
  customScale->setObjectName("projectCustomScale");
  customScale->setRange(0.000001, 1.e9);
  customScale->setDecimals(6);
  customScale->setValue(staged.plotScale);
  const int scaleControlHeight = std::max(plotScale->sizeHint().height(), customScale->sizeHint().height());
  plotScale->setFixedHeight(scaleControlHeight);
  customScale->setFixedHeight(scaleControlHeight);
  auto* scaleInserted = new QCheckBox("Scale objects inserted from other drawings", defaultsFrame);
  scaleInserted->setObjectName("projectScaleInsertedObjects");
  scaleInserted->setChecked(staged.defaults.scaleInsertedObjects);
  auto* setVariables = new QCheckBox("Set drawing variables to match", defaultsFrame);
  setVariables->setObjectName("projectSetDrawingVariables");
  setVariables->setChecked(staged.defaults.setDrawingVariables);
  defaultsGrid->addWidget(new QLabel("Angular units", defaultsFrame), 0, 0);
  defaultsGrid->addWidget(angular, 0, 1, 1, 3);
  defaultsGrid->addWidget(new QLabel("Foot definition", defaultsFrame), 1, 0);
  defaultsGrid->addWidget(foot, 1, 1, 1, 3);
  defaultsGrid->addWidget(new QLabel("Plot scale", defaultsFrame), 2, 0);
  defaultsGrid->addWidget(plotScale, 2, 1);
  defaultsGrid->addWidget(new QLabel("Custom units / in", defaultsFrame), 2, 2);
  defaultsGrid->addWidget(customScale, 2, 3);
  defaultsGrid->addWidget(scaleInserted, 3, 0, 1, 2);
  defaultsGrid->addWidget(setVariables, 3, 2, 1, 2);
  defaultsLayout->addLayout(defaultsGrid);
  auto* inheritanceNote = new QLabel("Drawings inherit these defaults unless they have an override.", defaultsFrame);
  inheritanceNote->setStyleSheet(QString("color:%1;font-size:11px;").arg(secondaryText));
  defaultsLayout->addWidget(inheritanceNote);
  unitsLayout->addWidget(defaultsFrame);
  unitsLayout->addStretch();
  pages->addWidget(unitsPage);

  // Object Layers page: this edits the same eight project defaults as the 0.7.4 settings window.
  auto* layersPage = new QWidget(pages);
  auto* layersLayout = new QVBoxLayout(layersPage);
  layersLayout->setContentsMargins(12, 10, 12, 10);
  layersLayout->addWidget(SectionTitle("Object Layers", layersPage));
  auto* layersHelp = new QLabel("Choose the layer and optional name prefix or suffix used when each object is created.", layersPage);
  layersHelp->setWordWrap(true);
  layersLayout->addWidget(layersHelp);
  auto* layers = new QTableWidget(kObjectLayerKindCount, 5, layersPage);
  layers->setObjectName("projectObjectLayers");
  layers->setHorizontalHeaderLabels({"Object", "Layer", "Name modifier", "Value"});
  layers->verticalHeader()->hide();
  layers->setAlternatingRowColors(true);
  layers->setSelectionBehavior(QAbstractItemView::SelectRows);
  layers->setMinimumHeight(370);
  layers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  layers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  layers->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  layers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  std::array<QComboBox*, kObjectLayerKindCount> layerNames{};
  std::array<QComboBox*, kObjectLayerKindCount> modifiers{};
  std::array<QLineEdit*, kObjectLayerKindCount> modifierValues{};
  std::array<QCheckBox*, kObjectLayerKindCount> locked{};
  for (int i = 0; i < kObjectLayerKindCount; ++i) {
    const auto row = static_cast<size_t>(i);
    auto* object = new QTableWidgetItem(kObjectLayerNames[row]);
    object->setFlags(object->flags() & ~Qt::ItemIsEditable);
    layers->setItem(i, 0, object);
    layerNames[row] = new QComboBox(layers);
    layerNames[row]->setObjectName(QString("projectLayerName%1").arg(i));
    layerNames[row]->setEditable(true);
    for (const auto& existing : m_State.drawingLayerTable)
      layerNames[row]->addItem(Utf8(existing.name));
    layerNames[row]->setCurrentText(Utf8(staged.defaults.objectLayers[row].layer));
    layers->setCellWidget(i, 1, layerNames[row]);
    modifiers[row] = new QComboBox(layers);
    modifiers[row]->setObjectName(QString("projectLayerModifier%1").arg(i));
    modifiers[row]->addItems({"None", "Prefix", "Suffix"});
    modifiers[row]->setCurrentIndex(static_cast<int>(staged.defaults.objectLayers[row].modifier));
    layers->setCellWidget(i, 2, modifiers[row]);
    modifierValues[row] = new QLineEdit(Utf8(staged.defaults.objectLayers[row].value), layers);
    modifierValues[row]->setObjectName(QString("projectLayerValue%1").arg(i));
    layers->setCellWidget(i, 3, modifierValues[row]);
    locked[row] = new QCheckBox(layers);
    locked[row]->setObjectName(QString("projectLayerLocked%1").arg(i));
    locked[row]->setChecked(staged.defaults.objectLayers[row].locked);
    layers->setCellWidget(i, 4, locked[row]);
  }
  layers->setColumnCount(5);
  layers->setHorizontalHeaderLabels({"Object", "Layer", "Name modifier", "Value", "Locked"});
  layers->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  layersLayout->addWidget(layers, 1);
  auto* layerInfo = new QLabel("Use one * in the value to include the new object's name.", layersPage);
  layerInfo->setStyleSheet(QString("color:%1;font-size:11px;").arg(secondaryText));
  layersLayout->addWidget(layerInfo);
  pages->addWidget(layersPage);

  // NOLINTBEGIN(bugprone-exception-escape): Qt callbacks catch exceptions; clang-tidy still flags possible STL allocation failures.
  QObject::connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
  auto setZoneDetails = [zoneDescription, zoneProjection, zoneDatum, zoneError, zoneCode,
                         originalZone](const QString& code) {
    try {
    const std::string value = Utf8String(code.trimmed());
    const auto details = value.empty() ? std::optional<geo::CoordinateSystemInfo>{}
                                       : geo::FindCoordinateSystem(value);
    zoneCode->setText(code);
    zoneDescription->setText(details ? Utf8(details->description) : QString("Select a coordinate system to see its description."));
    zoneProjection->setText(details ? Utf8(details->projection) : QString::fromUtf8("\xE2\x80\x94"));
    zoneDatum->setText(details ? Utf8(details->datum) : QString::fromUtf8("\xE2\x80\x94"));
    if (!value.empty() && !details && value != originalZone)
      zoneError->setText(geo::DictionariesLoaded() ? "Enter a coordinate-system code from the loaded dictionary."
                                                   : Utf8(geo::DictionaryError()));
    else
      zoneError->clear();
    } catch (const std::exception& error) {
      qWarning() << "Unable to show coordinate-system details:" << error.what();
      zoneError->setText("Unable to load coordinate-system details.");
    } catch (...) {
      qWarning() << "Unable to show coordinate-system details due to an unknown error.";
      zoneError->setText("Unable to load coordinate-system details.");
    }
  };
  auto populateSystems = [category, system, zoneError, setZoneDetails](const QString& requestedCode) {
    try {
    const QString categoryName = category->currentData().toString();
    const QSignalBlocker blocker(system);
    system->clear();
    if (categoryName.isEmpty()) {
      if (!requestedCode.isEmpty() && !geo::FindCoordinateSystem(Utf8String(requestedCode)))
        system->addItem("Unknown coordinate system — " + requestedCode, requestedCode);
      else
        system->addItem("No coordinate system", QString());
      system->setCurrentIndex(0);
      setZoneDetails(system->currentData().toString());
      return;
    }
    for (const auto& code : geo::CoordinateSystemsIn(Utf8String(categoryName))) {
      const auto info = geo::FindCoordinateSystem(code);
      const QString label = info && !info->description.empty() ? Utf8(info->description) : Utf8(code);
      system->addItem(label, Utf8(code));
    }
    int index = system->findData(requestedCode);
    if (index < 0 && system->count() > 0) index = 0;
    system->setCurrentIndex(index);
    setZoneDetails(index >= 0 ? system->currentData().toString() : requestedCode);
    } catch (const std::exception& error) {
      qWarning() << "Unable to populate coordinate-system choices:" << error.what();
      zoneError->setText("Unable to load coordinate-system choices.");
    } catch (...) {
      qWarning() << "Unable to populate coordinate-system choices due to an unknown error.";
      zoneError->setText("Unable to load coordinate-system choices.");
    }
  };
  const QString initialCategory = staged.zoneCode.empty() ? QString() : Utf8(geo::CategoryOf(staged.zoneCode));
  int initialCategoryIndex = category->findData(initialCategory);
  if (initialCategoryIndex < 0) initialCategoryIndex = 0;
  category->setCurrentIndex(initialCategoryIndex);
  populateSystems(Utf8(staged.zoneCode));
  QObject::connect(category, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
      [category, zoneError, populateSystems](int) {
        try {
        const QString categoryName = category->currentData().toString();
        const auto codes = categoryName.isEmpty() ? std::vector<std::string>{}
                                                   : geo::CoordinateSystemsIn(Utf8String(categoryName));
        populateSystems(codes.empty() ? QString() : Utf8(codes.front()));
        } catch (const std::exception& error) {
          qWarning() << "Unable to change coordinate-system category:" << error.what();
          zoneError->setText("Unable to load coordinate-system choices.");
        } catch (...) {
          qWarning() << "Unable to change coordinate-system category due to an unknown error.";
          zoneError->setText("Unable to load coordinate-system choices.");
        }
      });
  QObject::connect(system, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
      [system, setZoneDetails](int index) {
        try {
          setZoneDetails(index >= 0 ? system->currentData().toString() : QString());
        } catch (...) {
          qWarning() << "Unable to select coordinate-system code.";
        }
      });
  QObject::connect(zoneCode, &QLineEdit::editingFinished, &dialog,
      [zoneCode, category, system, zoneError, setZoneDetails, populateSystems] {
        try {
        const QString code = zoneCode->text().trimmed();
        if (code.isEmpty()) {
          category->setCurrentIndex(category->findData(QString()));
          populateSystems({});
          return;
        }
        const auto details = geo::FindCoordinateSystem(Utf8String(code));
        if (details) {
          const QString codeCategory = Utf8(geo::CategoryOf(Utf8String(code)));
          const int categoryIndex = category->findData(codeCategory);
          if (categoryIndex >= 0) category->setCurrentIndex(categoryIndex);
          populateSystems(code);
        } else {
          setZoneDetails(code);
        }
        } catch (const std::exception& error) {
          qWarning() << "Unable to apply coordinate-system code:" << error.what();
          zoneError->setText("Unable to apply the coordinate-system code.");
        } catch (...) {
          qWarning() << "Unable to apply coordinate-system code due to an unknown error.";
          zoneError->setText("Unable to apply the coordinate-system code.");
        }
      });
  // NOLINTEND(bugprone-exception-escape)
  const bool dictionaryLoaded = geo::DictionariesLoaded();
  category->setEnabled(dictionaryLoaded);
  system->setEnabled(dictionaryLoaded);
  if (!dictionaryLoaded && !geo::DictionaryError().empty()) zoneError->setText(Utf8(geo::DictionaryError()));

  auto repopulateScaleChoices = [plotScale, customScale, units](float value) {
    const QSignalBlocker blocker(plotScale);
    const auto choices = PlotScaleChoicesFor(units->currentData().toInt());
    plotScale->clear();
    int valueIndex = -1;
    for (size_t i = 0; i < choices.size(); ++i) {
      plotScale->addItem(Utf8(choices[i].label), choices[i].modelUnitsPerPlottedInch);
      if (PlotScaleChoiceIndex({choices[i]}, value) == 0) valueIndex = static_cast<int>(i);
    }
    plotScale->addItem("Custom", -1.0);
    if (valueIndex >= 0) {
      plotScale->setCurrentIndex(valueIndex);
      customScale->setEnabled(false);
    } else {
      plotScale->setCurrentIndex(plotScale->count() - 1);
      customScale->setEnabled(true);
    }
  };
  repopulateScaleChoices(staged.plotScale);
  QObject::connect(units, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
      [repopulateScaleChoices, customScale] { repopulateScaleChoices(static_cast<float>(customScale->value())); });
  QObject::connect(plotScale, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
      [plotScale, customScale](int index) {
        const double value = plotScale->currentData().toDouble();
        const bool isCustom = index < 0 || value < 0.0;
        customScale->setEnabled(isCustom);
        if (!isCustom) customScale->setValue(value);
      });

  auto* validation = new QLabel(&dialog);
  validation->setObjectName("projectSettingsValidation");
  validation->setStyleSheet("color:#ad3e36;");
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto* applyButton = buttons->addButton("Apply", QDialogButtonBox::ApplyRole);
  applyButton->setObjectName("projectSettingsApply");
  auto* okButton = buttons->button(QDialogButtonBox::Ok);
  okButton->setObjectName("projectSettingsOk");
  okButton->setProperty("projectPrimaryAction", true);
  okButton->style()->unpolish(okButton); okButton->style()->polish(okButton);
  buttons->button(QDialogButtonBox::Cancel)->setObjectName("projectSettingsCancel");
  auto* footer = new QHBoxLayout();
  footer->addWidget(validation, 1);
  footer->addWidget(buttons);
  outer->addLayout(footer);

  auto readStaged = [&]() -> ProjectSettings {
    ProjectSettings value = staged;
    value.insUnits = units->currentData().toInt();
    value.zoneCode = Utf8String(zoneCode->text().trimmed());
    value.hasDefaults = true;
    value.defaults.overridden = 0;
    value.defaults.angularUnits = static_cast<DrawingSettings::AngularUnits>(angular->currentData().toInt());
    value.defaults.footDefinition = static_cast<DrawingSettings::FootDefinition>(foot->currentData().toInt());
    value.defaults.scaleInsertedObjects = scaleInserted->isChecked();
    value.defaults.setDrawingVariables = setVariables->isChecked();
    const double selectedScale = plotScale->currentData().toDouble();
    value.plotScale = static_cast<float>(selectedScale > 0.0 ? selectedScale : customScale->value());
    for (int i = 0; i < kObjectLayerKindCount; ++i) {
      auto& row = value.defaults.objectLayers[static_cast<size_t>(i)];
      row.layer = Utf8String(layerNames[static_cast<size_t>(i)]->currentText().trimmed());
      row.modifier = static_cast<ObjectLayerRow::Modifier>(modifiers[static_cast<size_t>(i)]->currentIndex());
      row.value = Utf8String(modifierValues[static_cast<size_t>(i)]->text());
      row.locked = locked[static_cast<size_t>(i)]->isChecked();
    }
    return value;
  };
  auto validate = [&]() {
    ProjectSettings value = readStaged();
    QString error;
    if (!std::isfinite(value.plotScale) || value.plotScale <= 0.f)
      error = "The custom plot scale must be a positive number.";
    else if (!value.zoneCode.empty() && value.zoneCode != originalZone && !geo::FindCoordinateSystem(value.zoneCode))
      error = "Select a coordinate system from the loaded dictionary, or clear the code.";
    else if (const std::string layerError = ValidateObjectLayers(value.defaults); !layerError.empty())
      error = Utf8(layerError);
    validation->setText(error);
    const bool valid = error.isEmpty();
    okButton->setEnabled(valid);
    applyButton->setEnabled(valid);
    return valid;
  };
  for (auto* widget : unitsPage->findChildren<QWidget*>()) {
    if (auto* edit = qobject_cast<QLineEdit*>(widget))
      QObject::connect(edit, &QLineEdit::textChanged, &dialog, [validate] { validate(); });
    else if (auto* combo = qobject_cast<QComboBox*>(widget))
      QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [validate] { validate(); });
  }
  for (auto* widget : layersPage->findChildren<QWidget*>()) {
    if (auto* edit = qobject_cast<QLineEdit*>(widget))
      QObject::connect(edit, &QLineEdit::textChanged, &dialog, [validate] { validate(); });
    else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
      QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [validate] { validate(); });
      if (auto* editor = combo->lineEdit())
        QObject::connect(editor, &QLineEdit::textChanged, &dialog, [validate] { validate(); });
    }
  }
  for (auto* checkbox : layersPage->findChildren<QCheckBox*>())
    QObject::connect(checkbox, &QCheckBox::toggled, &dialog, [validate] { validate(); });
  QObject::connect(customScale, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
      [validate] { validate(); });

  auto save = [&, findSession](bool closeAfterSave) {
    if (!validate()) return;
    auto* current = findSession();
    if (!current || !current->settings) {
      QMessageBox::warning(&dialog, "Project Settings", "This project is no longer open.");
      return;
    }
    if (current->readOnly) {
      QMessageBox::warning(&dialog, "Project Settings",
          QString("%1 is open read-only; nothing was changed.").arg(Utf8(current->project.name)));
      return;
    }
    ProjectSettings nextSettings = readStaged();
    gsproj::Project nextProject = current->project;
    nextProject.settingsJson = WriteProjectSettings(nextProject.settingsJson, nextSettings);
    std::string error;
    if (!gsproj::Save(nextProject, &error)) {
      QMessageBox::warning(&dialog, "Project Settings",
          QString("Project settings could not be saved (%1); nothing was changed.").arg(Utf8(error)));
      return;
    }
    current->project = std::move(nextProject);
    *current->settings = nextSettings;
    staged = nextSettings;
    m_Log.push_back("Project settings saved: " + current->project.name);
    RefreshProjects();
    Refresh();
    if (closeAfterSave) dialog.accept();
  };
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { save(true); });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(applyButton, &QPushButton::clicked, &dialog, [&] { save(false); });
  validate();
  m_ModalOpen = true;
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}
