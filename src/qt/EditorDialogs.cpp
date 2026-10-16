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
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

void EditorWindow::ServiceUiRequests() {
  if (m_ModalOpen || m_TextDialogOpen) return;
  if (m_State.showLayerManagerWindow) {
    m_State.showLayerManagerWindow = false;
    ShowLayers();
  } else if (m_State.showUnitsWindow) {
    m_State.showUnitsWindow = false;
    ShowUnits();
  } else if (m_State.showTraverseEditorWindow || m_State.showTraverseClosureWindow) {
    m_State.showTraverseEditorWindow = m_State.showTraverseClosureWindow = false;
    ShowAdjustment();
  }
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
