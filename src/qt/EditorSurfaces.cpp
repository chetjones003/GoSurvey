// Qt Create Surface and Surface Properties dialogs, and the Toolspace surface menus (issue #893).
//
// The edit rules (what is refused, the exact messages, what one undo step covers) are the shared
// `surfedit::` services in commands/CadSurfaceEdit — so
// this file only supplies widgets. Create Surface and Surface Properties are modal; the Surfaces
// manager (EditorSurfaceManager.cpp) and the Surface Style editor (EditorSurfaceStyles.cpp) follow.
#include "EditorWindow.hpp"
#include "CadSurfaceEdit.hpp"
#include "EditorSurfaceUi.hpp"
#include "SurfaceStyle.hpp"
#include "util/surfacestats.hpp"
#include "EditorViewport.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

using surfaceui::Q;
using surfaceui::S;

namespace {

QPushButton* PrimaryButton(const QString& text, QWidget* parent, const char* name) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(name);
  button->setProperty("primary", true);
  button->setDefault(true);
  return button;
}

QTableWidgetItem* ReadOnlyItem(const QString& text) {
  auto* item = new QTableWidgetItem(text);
  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
  return item;
}

QTableWidget* MakeTable(QWidget* parent, const char* name, const QStringList& headers) {
  auto* table = new QTableWidget(parent);
  table->setObjectName(name);
  table->setColumnCount(static_cast<int>(headers.size()));
  table->setHorizontalHeaderLabels(headers);
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  return table;
}

}  // namespace

// --- Create Surface ---------------------------------------------------------------------------------

void EditorWindow::ShowCreateSurface() {
  if (findChild<QDialog*>("createSurfaceDialog")) return;
  SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
  const bool outerModal = m_ModalOpen;
  m_ModalOpen = true;

  QDialog dialog(this);
  dialog.setObjectName("createSurfaceDialog");
  dialog.setWindowTitle("Create Surface");
  dialog.setMinimumWidth(560);
  auto* root = new QVBoxLayout(&dialog);
  surfaceui::AddBanner(root, &dialog, "Create Surface", "Add a TIN, grid, corridor or volume surface to this drawing.");

  auto* typeBox = new QGroupBox("Surface type", &dialog);
  auto* typeForm = new QFormLayout(typeBox);
  auto* kind = new QComboBox(typeBox);
  kind->setObjectName("createSurfaceKind");
  kind->addItems({"TIN surface", "Grid surface", "Corridor surface", "TIN volume surface", "Grid volume surface"});
  typeForm->addRow("Type", kind);

  auto* gridBox = new QGroupBox("Grid", typeBox);
  gridBox->setObjectName("createSurfaceGridGroup");
  auto* gridForm = new QFormLayout(gridBox);
  const auto makeSpin = [&](const char* name, double value, double lo, double hi) {
    auto* spin = new QDoubleSpinBox(gridBox);
    spin->setObjectName(name);
    spin->setDecimals(4);
    spin->setRange(lo, hi);
    spin->setValue(value);
    return spin;
  };
  auto* originX = makeSpin("createSurfaceOriginX", 0.0, -1.0e12, 1.0e12);
  auto* originY = makeSpin("createSurfaceOriginY", 0.0, -1.0e12, 1.0e12);
  auto* spacingX = makeSpin("createSurfaceSpacingX", 10.0, -1.0e9, 1.0e9);
  auto* spacingY = makeSpin("createSurfaceSpacingY", 10.0, -1.0e9, 1.0e9);
  auto* cols = new QSpinBox(gridBox);
  cols->setObjectName("createSurfaceColumns");
  cols->setRange(2, 100000);
  cols->setValue(2);
  auto* rows = new QSpinBox(gridBox);
  rows->setObjectName("createSurfaceRows");
  rows->setRange(2, 100000);
  rows->setValue(2);
  gridForm->addRow("Grid origin X", originX);
  gridForm->addRow("Grid origin Y", originY);
  gridForm->addRow("Spacing X", spacingX);
  gridForm->addRow("Spacing Y", spacingY);
  gridForm->addRow("Columns", cols);
  gridForm->addRow("Rows", rows);
  typeForm->addRow(gridBox);

  auto* volumeBox = new QGroupBox("Volume surfaces", typeBox);
  volumeBox->setObjectName("createSurfaceVolumeGroup");
  auto* volumeForm = new QFormLayout(volumeBox);
  auto* base = new QComboBox(volumeBox);
  base->setObjectName("createSurfaceBase");
  auto* comparison = new QComboBox(volumeBox);
  comparison->setObjectName("createSurfaceComparison");
  volumeForm->addRow("Base surface", base);
  volumeForm->addRow("Comparison", comparison);
  typeForm->addRow(volumeBox);
  root->addWidget(typeBox);

  auto* infoBox = new QGroupBox("Properties", &dialog);
  auto* infoForm = new QFormLayout(infoBox);
  auto* name = new QLineEdit(surfaceui::Q(surfedit::NextSurfaceName(m_State)), infoBox);
  name->setObjectName("createSurfaceName");
  auto* description = new QLineEdit(infoBox);
  description->setObjectName("createSurfaceDescription");
  auto* style = new QComboBox(infoBox);
  style->setObjectName("createSurfaceStyle");
  for (const SurfaceStyle& entry : m_State.surfaceStyles) style->addItem(Q(entry.name));
  style->setCurrentText(SurfaceStyles::kStandardName);
  auto* layer = new QComboBox(infoBox);
  layer->setObjectName("createSurfaceLayer");
  auto* layerButton = new QPushButton("...", infoBox);
  layerButton->setObjectName("createSurfaceLayerManager");
  layerButton->setToolTip("Layer manager");
  layerButton->setFixedWidth(32);
  auto* layerRow = new QHBoxLayout;
  layerRow->addWidget(layer, 1);
  layerRow->addWidget(layerButton);
  infoForm->addRow("Name", name);
  infoForm->addRow("Description", description);
  infoForm->addRow("Style", style);
  infoForm->addRow("Surface layer", layerRow);
  root->addWidget(infoBox);

  auto* hint = new QLabel("Selecting OK will create a new surface which will appear in the list of surfaces in the Toolspace.", &dialog);
  hint->setWordWrap(true);
  root->addWidget(hint);
  auto* error = new QLabel(&dialog);
  error->setObjectName("createSurfaceError");
  error->setWordWrap(true);
  error->setStyleSheet("color:#c54646;");
  root->addWidget(error);

  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* ok = PrimaryButton("OK", &dialog, "ok");
  ok->setObjectName("createSurfaceOk");
  auto* cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("createSurfaceCancel");
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  root->addLayout(buttons);
  surfaceui::StyleDialog(&dialog);

  bool layerChosen = false;  // REQ-361: follows the name's Object Layers layer until a layer is chosen
  const auto syncLayer = [&] {
    const auto& row = m_State.drawingSettings.ObjectLayer(ObjectLayerKind::Surface);
    const QSignalBlocker blocker(layer);
    QString shown = layer->currentText();
    if (!layerChosen || row.locked) shown = Q(ResolveObjectLayer(m_State.drawingSettings, ObjectLayerKind::Surface, S(name->text())));
    layer->clear();
    for (const CadLayerRow& entry : m_State.drawingLayerTable) layer->addItem(Q(entry.name));
    if (layer->findText(shown) < 0) layer->insertItem(0, shown);
    layer->setCurrentText(shown);
    layer->setEnabled(!row.locked);
    layer->setToolTip(row.locked ? "Locked in Drawing Settings > Object Layers." : QString());
  };
  const auto fillVolumeCombo = [&](QComboBox* combo, bool gridsOnly, const QString& keep) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem("(select)", QString());
    for (const CadSurface& surface : m_State.cadSurfaces) {
      if (gridsOnly && surface.kind != SurfaceKind::Grid) continue;
      combo->addItem(Q(surface.name), Q(surface.name));
    }
    const int at = combo->findData(keep);
    combo->setCurrentIndex(at >= 0 ? at : 0);
  };
  QString baseName, comparisonName;
  if (m_State.cadSurfaces.size() >= 2) {
    baseName = Q(m_State.cadSurfaces[0].name);
    comparisonName = Q(m_State.cadSurfaces[1].name);
  }
  const auto syncKind = [&] {
    const int index = kind->currentIndex();
    gridBox->setVisible(index == static_cast<int>(surfedit::CreateKind::Grid));
    const bool volume = index == static_cast<int>(surfedit::CreateKind::TinVolume) ||
                        index == static_cast<int>(surfedit::CreateKind::GridVolume);
    volumeBox->setVisible(volume);
    if (volume) {
      const bool gridsOnly = index == static_cast<int>(surfedit::CreateKind::GridVolume);
      fillVolumeCombo(base, gridsOnly, baseName);
      fillVolumeCombo(comparison, gridsOnly, comparisonName);
    }
    error->clear();
    dialog.adjustSize();
  };
  syncLayer();
  syncKind();

  QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, [&](int) { syncKind(); });
  QObject::connect(base, &QComboBox::currentIndexChanged, &dialog, [&](int) { baseName = base->currentData().toString(); });
  QObject::connect(comparison, &QComboBox::currentIndexChanged, &dialog, [&](int) { comparisonName = comparison->currentData().toString(); });
  QObject::connect(name, &QLineEdit::textChanged, &dialog, [&](const QString& text) {
    ok->setEnabled(!text.isEmpty());
    syncLayer();
  });
  QObject::connect(layer, &QComboBox::activated, &dialog, [&](int) { layerChosen = true; });
  QObject::connect(layerButton, &QPushButton::clicked, &dialog, [&] {
    const bool wasModal = m_ModalOpen;
    ShowLayers();
    m_ModalOpen = wasModal;
    syncLayer();
  });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
    surfedit::CreateRequest request;
    request.kind = static_cast<surfedit::CreateKind>(kind->currentIndex());
    request.name = S(name->text());
    request.description = S(description->text());
    request.styleName = S(style->currentText());
    request.layer = S(layer->currentText());
    request.gridOriginX = originX->value();
    request.gridOriginY = originY->value();
    request.gridSpacingX = spacingX->value();
    request.gridSpacingY = spacingY->value();
    request.gridCols = cols->value();
    request.gridRows = rows->value();
    request.volumeBase = S(base->currentData().toString());
    request.volumeComparison = S(comparison->currentData().toString());
    const std::string why = surfedit::CreateSurface(m_State, request, m_Log);
    if (why.empty()) {
      dialog.accept();
    } else {
      error->setText(Q(why));
    }
  });
  dialog.exec();
  m_ModalOpen = outerModal;
  Refresh();
}

// --- Surface Properties -----------------------------------------------------------------------------

void EditorWindow::ShowSurfaceProperties(int surfaceIndex) {
  if (findChild<QDialog*>("surfacePropertiesDialog")) return;
  if (surfaceIndex < 0 || surfaceIndex >= static_cast<int>(m_State.cadSurfaces.size())) return;
  SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
  const bool outerModal = m_ModalOpen;
  m_ModalOpen = true;
  const int si = surfaceIndex;

  QDialog dialog(this);
  dialog.setObjectName("surfacePropertiesDialog");
  dialog.resize(680, 600);
  auto* root = new QVBoxLayout(&dialog);
  auto* banner = surfaceui::AddBanner(root, &dialog, "Surface Properties", "Name, definition, analysis and statistics.");
  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("surfacePropertiesTabs");
  root->addWidget(tabs, 1);

  // Information -- staged until OK / Apply, like the 0.7.4 window.
  auto* info = new QWidget(tabs);
  auto* infoLayout = new QVBoxLayout(info);
  auto* nameBox = new QGroupBox("Surface", info);
  auto* nameForm = new QFormLayout(nameBox);
  auto* nameEdit = new QLineEdit(nameBox);
  nameEdit->setObjectName("surfacePropsName");
  auto* descriptionEdit = new QPlainTextEdit(nameBox);
  descriptionEdit->setObjectName("surfacePropsDescription");
  descriptionEdit->setFixedHeight(88);
  nameForm->addRow("Name", nameEdit);
  nameForm->addRow("Description", descriptionEdit);
  auto* nameProblem = new QLabel(nameBox);
  nameProblem->setObjectName("surfacePropsNameProblem");
  nameProblem->setWordWrap(true);
  nameProblem->setStyleSheet("color:#c54646;");
  nameForm->addRow(nameProblem);
  infoLayout->addWidget(nameBox);
  auto* styleBox = new QGroupBox("Default styles", info);
  auto* styleRow = new QHBoxLayout(styleBox);
  auto* styleCombo = new QComboBox(styleBox);
  styleCombo->setObjectName("surfacePropsStyle");
  auto* editStyle = new QPushButton("Edit...", styleBox);
  editStyle->setObjectName("surfacePropsEditStyle");
  styleRow->addWidget(new QLabel("Surface style:", styleBox));
  styleRow->addWidget(styleCombo, 1);
  styleRow->addWidget(editStyle);
  infoLayout->addWidget(styleBox);
  infoLayout->addStretch();
  tabs->addTab(info, "Information");

  // Definition
  auto* definition = new QWidget(tabs);
  auto* definitionLayout = new QVBoxLayout(definition);
  auto* summary = MakeTable(definition, "surfacePropsDefinitionOptions", {"Definition Options", "Value"});
  summary->setRowCount(3);
  summary->setFixedHeight(132);
  summary->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  definitionLayout->addWidget(summary);
  definitionLayout->addWidget(new QLabel("Operations", definition));
  auto* operationsRow = new QHBoxLayout;
  auto* operations = MakeTable(definition, "surfacePropsOperations", {"Operation Type", "Parameters"});
  auto* moveColumn = new QVBoxLayout;
  auto* moveUp = new QPushButton("Move up", definition);
  moveUp->setObjectName("surfacePropsMoveUp");
  auto* moveDown = new QPushButton("Move down", definition);
  moveDown->setObjectName("surfacePropsMoveDown");
  moveColumn->addWidget(moveUp);
  moveColumn->addWidget(moveDown);
  moveColumn->addStretch();
  operationsRow->addLayout(moveColumn);
  operationsRow->addWidget(operations, 1);
  definitionLayout->addLayout(operationsRow, 1);
  auto* noItems = new QLabel("(no definition items \xe2\x80\x94 add them from the Surfaces manager)", definition);
  noItems->setObjectName("surfacePropsNoItems");
  definitionLayout->addWidget(noItems);
  tabs->addTab(definition, "Definition");

  // Analysis
  auto* analysis = new QWidget(tabs);
  auto* analysisLayout = new QVBoxLayout(analysis);
  auto* analysisBox = new QGroupBox("Ranges", analysis);
  auto* analysisForm = new QFormLayout(analysisBox);
  auto* analysisType = new QComboBox(analysisBox);
  analysisType->setObjectName("surfacePropsAnalysisType");
  analysisType->addItems({"None", "Elevations", "Slope", "Direction", "Slope angle"});
  auto* rangeCount = new QSpinBox(analysisBox);
  rangeCount->setObjectName("surfacePropsRangeCount");
  rangeCount->setRange(1, 64);
  rangeCount->setValue(8);
  auto* run = new QPushButton("Run", analysisBox);
  run->setObjectName("surfacePropsRun");
  analysisForm->addRow("Analysis type", analysisType);
  analysisForm->addRow("Range count", rangeCount);
  analysisForm->addRow(run);
  analysisLayout->addWidget(analysisBox);
  analysisLayout->addWidget(new QLabel("Range Details", analysis));
  auto* bands = MakeTable(analysis, "surfacePropsBands", {"ID", "Upper bound", "Color"});
  analysisLayout->addWidget(bands, 1);
  tabs->addTab(analysis, "Analysis");

  // Statistics
  auto* statistics = new QWidget(tabs);
  auto* statisticsLayout = new QVBoxLayout(statistics);
  auto* statisticsNote = new QLabel(statistics);
  statisticsNote->setObjectName("surfacePropsStatsNote");
  statisticsNote->setWordWrap(true);
  auto* statisticsTable = MakeTable(statistics, "surfacePropsStatistics", {"Property", "Value"});
  statisticsLayout->addWidget(statisticsNote);
  statisticsLayout->addWidget(statisticsTable, 1);
  tabs->addTab(statistics, "Statistics");

  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* ok = PrimaryButton("OK", &dialog, "ok");
  ok->setObjectName("surfacePropsOk");
  auto* cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("surfacePropsCancel");
  auto* apply = new QPushButton("Apply", &dialog);
  apply->setObjectName("surfacePropsApply");
  buttons->addWidget(ok);
  buttons->addWidget(cancel);
  buttons->addWidget(apply);
  root->addLayout(buttons);
  surfaceui::StyleDialog(&dialog);

  const auto surface = [&]() -> CadSurface& { return m_State.cadSurfaces[static_cast<size_t>(si)]; };
  std::string committedStyle = surface().styleName.empty() ? std::string(SurfaceStyles::kStandardName) : surface().styleName;
  const auto fillStyles = [&](const std::string& select) {
    const QSignalBlocker blocker(styleCombo);
    styleCombo->clear();
    for (const SurfaceStyle& entry : m_State.surfaceStyles) styleCombo->addItem(Q(entry.name));
    if (styleCombo->findText(Q(select)) < 0) styleCombo->addItem(Q(select));
    styleCombo->setCurrentText(Q(select));
  };
  const auto retitle = [&] {
    dialog.setWindowTitle(Q("Surface Properties - " + surface().name));
    banner->setText(QString("<div style='font-size:15pt;font-weight:600'>Surface Properties</div><div>%1</div>")
                        .arg(Q(surface().name).toHtmlEscaped()));
  };
  nameEdit->setText(Q(surface().name));
  descriptionEdit->setPlainText(Q(surface().description));
  fillStyles(committedStyle);
  retitle();

  std::vector<surfedit::DefinitionRow> definitionRows;
  const auto loadDefinition = [&](int select) {
    const CadSurface& s = surface();
    summary->setItem(0, 0, ReadOnlyItem("Build"));
    summary->setItem(0, 1, ReadOnlyItem("automatic"));
    summary->setItem(1, 0, ReadOnlyItem("Data operations"));
    summary->setItem(1, 1, ReadOnlyItem(QString("%1 group(s), %2 file(s), %3 breakline(s)")
                                             .arg(s.sourcePointGroups.size()).arg(s.sourcePointFiles.size()).arg(s.breaklines.size())));
    summary->setItem(2, 0, ReadOnlyItem("Edit operations"));
    summary->setItem(2, 1, ReadOnlyItem(QString("%1 edge swap(s), %2 added point(s), %3 deleted point(s)")
                                             .arg(s.swappedEdgePicks.size()).arg(s.addedPointXyz.size() / 3).arg(s.deletedPointPicks.size())));
    definitionRows = surfedit::DefinitionRows(s);
    operations->setRowCount(static_cast<int>(definitionRows.size()));
    for (int row = 0; row < static_cast<int>(definitionRows.size()); ++row) {
      operations->setItem(row, 0, ReadOnlyItem(definitionRows[static_cast<size_t>(row)].type));
      operations->setItem(row, 1, ReadOnlyItem(Q(definitionRows[static_cast<size_t>(row)].param)));
    }
    noItems->setVisible(definitionRows.empty());
    if (!definitionRows.empty()) operations->selectRow(std::clamp(select, 0, static_cast<int>(definitionRows.size()) - 1));
    const int current = operations->currentRow();
    moveUp->setEnabled(surfedit::CanMoveDefinitionRow(definitionRows, current, -1));
    moveDown->setEnabled(surfedit::CanMoveDefinitionRow(definitionRows, current, 1));
  };
  const auto currentStyle = [&]() -> SurfaceStyle* {
    SurfaceStyle* found = SurfaceStyles::Find(m_State.surfaceStyles, surface().styleName);
    return found ? found : &SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
  };
  const auto loadAnalysis = [&] {
    const SurfaceStyle* style = currentStyle();
    {
      const QSignalBlocker blocker(analysisType);
      analysisType->setCurrentIndex(static_cast<int>(style->analysisMode));
    }
    bands->setRowCount(static_cast<int>(style->bands.size()));
    for (int row = 0; row < static_cast<int>(style->bands.size()); ++row) {
      const SurfaceBand& band = style->bands[static_cast<size_t>(row)];
      bands->setItem(row, 0, ReadOnlyItem(QString::number(row + 1)));
      bands->setItem(row, 1, ReadOnlyItem(QString::number(band.upperBound, 'g', 4)));
      auto* color = ReadOnlyItem(Q(band.color));
      color->setIcon(surfaceui::Swatch(band.color));
      bands->setItem(row, 2, color);
    }
  };
  const auto loadStatistics = [&] {
    const CadSurface& s = surface();
    const SurfaceStats stats = s.tin ? ComputeSurfaceStats(s.tin->vertsXyz, s.tin->indices, static_cast<int>(s.breaklines.size()),
                                                           s.isVolumeSurface())
                                     : SurfaceStats{};
    statisticsTable->setRowCount(0);
    if (!stats.built) {
      statisticsNote->setText(QString("Not built. Add definition items from the Surfaces manager, then Rebuild.%1")
                                  .arg(s.lastBuildMessage.empty() ? QString() : "\n" + Q(s.lastBuildMessage)));
      statisticsTable->hide();
      return;
    }
    statisticsNote->clear();
    statisticsTable->show();
    const auto add = [&](const QString& key, const QString& value) {
      const int row = statisticsTable->rowCount();
      statisticsTable->insertRow(row);
      statisticsTable->setItem(row, 0, ReadOnlyItem(key));
      statisticsTable->setItem(row, 1, ReadOnlyItem(value));
    };
    add("Points", QString::number(stats.points));
    add("Triangles", QString::number(stats.triangles));
    add("Edges", QString::number(stats.uniqueEdges));
    add("Breakline edges", QString::number(stats.breaklineEdges));
    add("X range", QString("%1 to %2").arg(stats.minX, 0, 'f', 3).arg(stats.maxX, 0, 'f', 3));
    add("Y range", QString("%1 to %2").arg(stats.minY, 0, 'f', 3).arg(stats.maxY, 0, 'f', 3));
    add("Elevation", QString("%1 to %2").arg(stats.minZ, 0, 'f', 3).arg(stats.maxZ, 0, 'f', 3));
    add("2D area", QString::number(stats.area2d, 'f', 3));
    add("3D area", QString::number(stats.area3d, 'f', 3));
    add("Slope", QString("%1 %% to %2 %% (mean %3 %%)").arg(stats.minSlopePct, 0, 'f', 2).arg(stats.maxSlopePct, 0, 'f', 2).arg(stats.meanSlopePct, 0, 'f', 2));
    add("Slope (deg)", QString("%1 to %2 (mean %3)").arg(stats.minSlopeDeg, 0, 'f', 2).arg(stats.maxSlopeDeg, 0, 'f', 2).arg(stats.meanSlopeDeg, 0, 'f', 2));
    if (s.isVolumeSurface()) add("Volume (ft3)", QString("cut %1  fill %2").arg(stats.volumeCutFt3, 0, 'f', 3).arg(stats.volumeFillFt3, 0, 'f', 3));
  };
  // Analysis edits apply immediately and undoably (as in 0.7.4); they may copy a shared style, which
  // moves this surface to the copy, so the staged style follows when the user had not changed it.
  const auto afterStyleChange = [&] {
    const std::string now = surface().styleName.empty() ? std::string(SurfaceStyles::kStandardName) : surface().styleName;
    if (S(styleCombo->currentText()) == committedStyle) fillStyles(now);
    committedStyle = now;
    loadAnalysis();
    m_Viewport->update();
  };
  loadDefinition(0);
  loadAnalysis();
  loadStatistics();

  QObject::connect(operations, &QTableWidget::currentCellChanged, &dialog, [&](int row, int, int, int) {
    moveUp->setEnabled(surfedit::CanMoveDefinitionRow(definitionRows, row, -1));
    moveDown->setEnabled(surfedit::CanMoveDefinitionRow(definitionRows, row, 1));
  });
  const auto moveSelected = [&](int delta) {
    const int row = operations->currentRow();
    if (surfedit::MoveDefinitionRow(m_State, si, definitionRows, row, delta)) {
      loadDefinition(row + delta);
      m_Viewport->update();
    }
  };
  QObject::connect(moveUp, &QPushButton::clicked, &dialog, [&] { moveSelected(-1); });
  QObject::connect(moveDown, &QPushButton::clicked, &dialog, [&] { moveSelected(1); });
  QObject::connect(analysisType, &QComboBox::activated, &dialog, [&](int index) {
    surfedit::SetAnalysisMode(m_State, si, static_cast<SurfaceAnalysisMode>(index), m_Log);
    afterStyleChange();
  });
  QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
    surfedit::RunElevationBands(m_State, si, rangeCount->value(), m_Log);
    afterStyleChange();
  });
  QObject::connect(editStyle, &QPushButton::clicked, &dialog, [&] {
    const bool wasModal = m_ModalOpen;
    ShowSurfaceStyles(styleCombo->currentText(), false);
    m_ModalOpen = wasModal;
    std::string keep = S(styleCombo->currentText());
    if (!SurfaceStyles::Find(m_State.surfaceStyles, keep)) keep = surface().styleName.empty() ? std::string(SurfaceStyles::kStandardName) : surface().styleName;
    committedStyle = surface().styleName.empty() ? std::string(SurfaceStyles::kStandardName) : surface().styleName;
    fillStyles(keep);
    loadAnalysis();
  });
  const auto applyInfo = [&]() -> bool {
    if (!surfedit::ApplySurfaceInfo(m_State, si, S(nameEdit->text()), S(descriptionEdit->toPlainText()), S(styleCombo->currentText()), m_Log))
      return false;
    committedStyle = S(styleCombo->currentText());
    retitle();
    loadAnalysis();
    loadStatistics();
    return true;
  };
  // A refused name keeps the dialog open and says why (the reason is also in the command history).
  const auto commit = [&]() -> bool {
    const bool applied = applyInfo();
    nameProblem->setText(applied || m_Log.empty() ? QString() : Q(m_Log.back()));
    return applied;
  };
  QObject::connect(apply, &QPushButton::clicked, &dialog, [&] { commit(); });
  QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
    if (commit()) dialog.accept();
  });
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.exec();
  m_ModalOpen = outerModal;
  Refresh();
}

// --- Toolspace menus -------------------------------------------------------------------------------

void EditorWindow::ShowToolspaceContextMenu(const QPoint& position) {
  QTreeWidgetItem* item = m_Toolspace->itemAt(position);
  if (!item) return;
  const QString kind = item->data(0, Qt::UserRole + 1).toString();
  if (kind.isEmpty()) return;
  const int surface = item->data(0, Qt::UserRole + 2).isValid() ? item->data(0, Qt::UserRole + 2).toInt() : -1;
  const QString styleName = item->data(0, Qt::UserRole + 3).toString();
  QMenu menu(this);
  menu.setObjectName("toolspaceSurfaceMenu");
  const auto add = [&](const QString& text, const char* name, bool enabled, std::function<void()> action) {
    QAction* entry = menu.addAction(text);
    entry->setObjectName(name);
    entry->setEnabled(enabled);
    connect(entry, &QAction::triggered, this, [action = std::move(action)] { action(); });  // NOLINT(bugprone-exception-escape): a slot, like every other lambda in this file
  };
  const bool valid = surface >= 0 && surface < static_cast<int>(m_State.cadSurfaces.size());
  if (kind == "surfacesFolder") {
    add("Create Surface...", "toolspaceCreateSurface", true, [this] { m_State.showCreateSurfaceWindow = true; Refresh(); });
    add("Surfaces Manager...", "toolspaceSurfaceManager", true, [this] { m_State.showSurfaceManagerWindow = true; Refresh(); });
    menu.addSeparator();
    add("Rebuild Out of Date Items", "toolspaceRebuildAll", !m_State.cadSurfaces.empty(), [this] { Submit("SURFACEREBUILD"); });
  } else if (kind == "surface" && valid) {
    const std::string name = m_State.cadSurfaces[static_cast<size_t>(surface)].name;
    add("Surface Properties...", "toolspaceSurfaceProperties", true, [this, surface] {
      m_State.surfacePropertiesIndex = surface;
      m_State.showSurfacePropertiesWindow = true;
      Refresh();
    });
    add("Edit Surface Style...", "toolspaceEditSurfaceStyle", true, [this, surface] {
      const CadSurface& s = m_State.cadSurfaces[static_cast<size_t>(surface)];
      m_State.surfaceStyleEditorFocusName = s.styleName.empty() ? std::string(SurfaceStyles::kStandardName) : s.styleName;
      m_State.surfaceStyleUseSurfacesTitle = false;
      m_State.showSurfaceStyleWindow = true;
      Refresh();
    });
    menu.addSeparator();
    add("Rebuild", "toolspaceRebuildSurface", true, [this, name] { Submit(Q("SURFACEREBUILD " + name)); }); // NOLINT(bugprone-exception-escape): a slot; allocation failure propagates like any other in this file
    add("Delete", "toolspaceDeleteSurface", true, [this, surface] {
      surfedit::DeleteSurface(m_State, surface, m_Log);
      Refresh();
    });
    menu.addSeparator();
    add("Select", "toolspaceSelectSurface", true, [this, surface] {
      m_State.selection.clear();
      m_State.selectedSurveyPointIndices.clear();
      m_State.selection.push_back({SelectedEntity::Type::Surface, surface});
      Refresh();
    });
  } else if (kind == "stylesFolder") {
    add("Properties", "toolspaceSurfaceStyles", true, [this] { m_State.showSurfaceStyleWindow = true; Refresh(); });
  } else if (kind == "style") {
    add("Properties", "toolspaceSurfaceStyle", true, [this, styleName] {
      m_State.surfaceStyleEditorFocusName = S(styleName);
      m_State.surfaceStyleUseSurfacesTitle = false;
      m_State.showSurfaceStyleWindow = true;
      Refresh();
    });
  } else {
    return;
  }
  menu.exec(m_Toolspace->viewport()->mapToGlobal(position));
}
