#include "EditorWindow.hpp"
#include "CadColor.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QTimer>
#include <QFileDialog>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>

void EditorWindow::ServiceUiRequests() {
  if (m_State.showCreatePointsWindow) ShowCreatePoints();
  if (m_ModalOpen || m_TextDialogOpen) return;
  if (m_State.pointEditPrompt.active) {
    ShowProjectPointWarning();
  } else if (m_State.pastePrompt.active) {
    ShowPasteWarning();
  } else if (m_State.showLayerManagerWindow) {
    m_State.showLayerManagerWindow = false;
    ShowLayers();
  } else if (m_State.showUnitsWindow) {
    m_State.showUnitsWindow = false;
    ShowUnits();
  } else if (m_State.showQuickSelectWindow) {
    m_State.showQuickSelectWindow = false;
    ShowQuickSelect();
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
  } else if (m_State.surveyReportSelectLatestPending) {
    ShowSurveyReports();
  }
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
  SyncDrawingLayerTableWithGeometry(m_State);
  QDialog dialog(this);
  dialog.setObjectName("layerManager");
  dialog.setWindowTitle("Layer Properties Manager");
  dialog.resize(950, 550);
  auto* layout = new QVBoxLayout(&dialog);
  auto* toolbar = new QHBoxLayout;
  auto* add = new QPushButton(QIcon(QtAsset("icons/New_Layer.png")), "New layer", &dialog);
  auto* erase = new QPushButton("Delete layer", &dialog);
  auto* current = new QPushButton("Set current", &dialog);
  toolbar->addWidget(add); toolbar->addWidget(erase); toolbar->addWidget(current); toolbar->addStretch();
  layout->addLayout(toolbar);
  auto* table = new QTableWidget(&dialog);
  table->setObjectName("layerTable");
  table->setColumnCount(7);
  table->setHorizontalHeaderLabels({"Name", "On", "Frozen", "Locked", "Color", "Linetype", "Lineweight (mm)"});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  layout->addWidget(table);
  auto populate = [&] {
    const QSignalBlocker blocker(table);
    table->setRowCount(static_cast<int>(m_State.drawingLayerTable.size()));
    for (int row = 0; row < table->rowCount(); ++row) {
      const auto& layer = m_State.drawingLayerTable[row];
      auto* name = new QTableWidgetItem(QString::fromUtf8(layer.name.c_str()));
      name->setData(Qt::UserRole, name->text());
      if (layer.name == m_State.currentLayer) { auto font = name->font(); font.setBold(true); name->setFont(font); }
      table->setItem(row, 0, name);
      for (int column = 1; column <= 3; ++column) {
        auto* value = new QTableWidgetItem;
        value->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        value->setCheckState((column == 1 ? layer.on : column == 2 ? layer.frozen : layer.locked) ? Qt::Checked : Qt::Unchecked);
        table->setItem(row, column, value);
      }
      table->setItem(row, 4, new QTableWidgetItem(QString::fromUtf8(layer.color.c_str())));
      table->setItem(row, 5, new QTableWidgetItem(QString::fromUtf8(layer.linetype.c_str())));
      table->setItem(row, 6, new QTableWidgetItem(QString::number(layer.lineweightMm)));
    }
  };
  populate();
  connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
    const int row = item->row(), column = item->column();
    const auto name = table->item(row, 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
    const auto found = std::find_if(m_State.drawingLayerTable.begin(), m_State.drawingLayerTable.end(),
        [&](const auto& layer) { return layer.name == name; });
    if (found == m_State.drawingLayerTable.end()) return;
    std::string error;
    if (column == 0) {
      const auto next = item->text().toUtf8().toStdString();
      if (!CadRenameDrawingLayer(m_State, name, next, &error)) QMessageBox::warning(&dialog, "Rename layer", QString::fromUtf8(error.c_str()));
    } else {
      CadLayerRow next = *found;
      bool valid = true;
      if (column == 1) next.on = item->checkState() == Qt::Checked;
      if (column == 2) next.frozen = item->checkState() == Qt::Checked;
      if (column == 3) next.locked = item->checkState() == Qt::Checked;
      if (column == 4) valid = CadColorStorageFromTyped(item->text().toUtf8().toStdString(), &next.color) && next.color != "ByLayer";
      if (column == 5) { next.linetype = item->text().trimmed().toUtf8().toStdString(); valid = !next.linetype.empty(); }
      if (column == 6) {
        const double value = item->text().toDouble(&valid);
        valid = valid && std::isfinite(value) && value >= -1 && value <= 10;
        next.lineweightMm = static_cast<float>(value);
      }
      if (valid) { PushUndoSnapshot(m_State, "Layer property"); *found = std::move(next); BumpCadGpuCache(m_State); }
      else QMessageBox::warning(&dialog, "Layer property", "Enter a valid color, linetype, or lineweight (-1 for default).");
    }
    populate(); Refresh();
  });
  connect(add, &QPushButton::clicked, &dialog, [&] {
    bool accepted = false;
    const auto name = QInputDialog::getText(&dialog, "New layer", "Name", QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    std::string error;
    if (!CadAddDrawingLayer(m_State, name.toUtf8().toStdString(), &error))
      QMessageBox::warning(&dialog, "New layer", QString::fromUtf8(error.c_str()));
    populate(); Refresh();
  });
  connect(erase, &QPushButton::clicked, &dialog, [&] {
    if (table->currentRow() < 0) return;
    const auto name = table->item(table->currentRow(), 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
    std::string error;
    if (!CadDeleteDrawingLayer(m_State, name, &error)) QMessageBox::warning(&dialog, "Delete layer", QString::fromUtf8(error.c_str()));
    populate(); Refresh();
  });
  connect(current, &QPushButton::clicked, &dialog, [&] {
    if (table->currentRow() < 0) return;
    m_State.currentLayer = table->item(table->currentRow(), 0)->data(Qt::UserRole).toString().toUtf8().toStdString();
    populate(); Refresh();
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowUnits() {
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("drawingUnits");
  dialog.setWindowTitle("Drawing Units and Plot Scale");
  auto* layout = new QFormLayout(&dialog);
  auto* units = new QComboBox(&dialog);
  units->setObjectName("drawingUnit");
  const std::pair<const char*, int> codes[] = {{"Unitless", 0}, {"Inches", 1}, {"Feet", 2}, {"Millimeters", 4}, {"Meters", 6}};
  for (const auto& code : codes) units->addItem(code.first, code.second);
  units->setCurrentIndex(units->findData(m_State.drawingInsUnits));
  auto* scale = new QDoubleSpinBox(&dialog);
  scale->setObjectName("drawingScale");
  scale->setRange(0.000001, 1.e9); scale->setDecimals(6); scale->setValue(m_State.modelUnitsPerPlottedInch);
  layout->addRow("Drawing unit (relabels geometry)", units);
  layout->addRow("Model units per plotted inch", scale);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    if (ApplyDrawingSettings(m_State, units->currentData().toInt(), static_cast<float>(scale->value()), m_State.drawingSettings, m_Log)) dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}
