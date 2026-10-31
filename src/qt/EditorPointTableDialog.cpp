#include "EditorPointTableDialog.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include "util/cadtablestyle.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QRadioButton>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace {

QString FromUtf8(const std::string& value) { return QString::fromUtf8(value.c_str()); }

}  // namespace

void ShowAddPointTableDialog(QWidget* parent, AppCommandState& state, std::vector<std::string>& log) {
  std::vector<const CadTableStyle*> pointStyles;
  for (const CadTableStyle& s : state.tableStyles) {
    if (s.point)
      pointStyles.push_back(&s);
  }
  if (pointStyles.empty()) {
    log.push_back("ADDPOINTTABLE — this drawing has no point table style.");
    return;
  }
  if (state.surveyPoints.empty()) {
    log.push_back("ADDPOINTTABLE — this drawing has no survey points.");
    return;
  }
  std::vector<SurveyPoint> points = state.surveyPoints;
  std::sort(points.begin(), points.end(), [](const SurveyPoint& a, const SurveyPoint& b) { return a.id < b.id; });

  QDialog dialog(parent);
  dialog.setObjectName("addPointTableDialog");
  dialog.setWindowTitle("Add Point Table");
  dialog.resize(940, 760);
  dialog.setStyleSheet("QDialog { background: #f0f0f0; color: #202020; }"
                       "QLabel, QCheckBox, QRadioButton, QGroupBox { color: #202020; }"
                       "QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { background: #ffffff; color: #202020;"
                       " border: 1px solid #aab1b5; padding: 3px; }"
                       "QPushButton { background: #f8f8f8; color: #202020; border: 1px solid #aab1b5; padding: 5px 10px; }"
                       "QGroupBox { border: 1px solid #b8b8b8; margin-top: 8px; padding-top: 8px; }"
                       "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
                       "QTableWidget { background: #ffffff; alternate-background-color: #f5f5f5;"
                       " gridline-color: #c8c8c8; color: #202020; }"
                       "QHeaderView::section { background: #e7e7e7; color: #202020; padding: 4px; }");

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(18, 16, 18, 14);
  root->setSpacing(10);

  auto* controls = new QHBoxLayout;
  controls->addWidget(new QLabel("Table style:", &dialog));
  auto* styleCombo = new QComboBox(&dialog);
  styleCombo->setObjectName("pointTableStyle");
  for (const CadTableStyle* s : pointStyles)
    styleCombo->addItem(FromUtf8(s->name));
  controls->addWidget(styleCombo, 1);
  auto* styleEdit = new QPushButton("Edit style…", &dialog);
  styleEdit->setObjectName("pointTableEditStyle");
  controls->addWidget(styleEdit);
  controls->addWidget(new QLabel("Point group:", &dialog));
  auto* groupCombo = new QComboBox(&dialog);
  groupCombo->setObjectName("pointTableGroup");
  groupCombo->addItem("(none)");
  for (const PointGroup& g : state.pointGroups)
    groupCombo->addItem(FromUtf8(g.name));
  controls->addWidget(groupCombo, 1);
  root->addLayout(controls);

  auto* layerRow = new QHBoxLayout;
  layerRow->addWidget(new QLabel("Table layer:", &dialog));
  auto* tableLayer = new QLineEdit(QString::fromUtf8(state.currentLayer.c_str()), &dialog);
  tableLayer->setObjectName("pointTableLayer");
  layerRow->addWidget(tableLayer, 1);
  root->addLayout(layerRow);

  auto* selection = new QGroupBox("Selection", &dialog);
  auto* selectionLayout = new QVBoxLayout(selection);
  auto* selectHint = new QLabel("Choose point groups or check individual points. Selected points are listed once.", selection);
  selectionLayout->addWidget(selectHint);
  root->addWidget(selection);

  auto* list = new QTableWidget(static_cast<int>(points.size()), 6, &dialog);
  list->setObjectName("pointTableList");
  list->setAlternatingRowColors(true);
  list->setHorizontalHeaderLabels({"Use", "Point", "Northing", "Easting", "Elevation", "Description"});
  list->verticalHeader()->setVisible(false);
  list->setSelectionMode(QAbstractItemView::NoSelection);
  list->setEditTriggers(QAbstractItemView::NoEditTriggers);
  list->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  list->horizontalHeader()->setStretchLastSection(true);
  for (int r = 0; r < static_cast<int>(points.size()); ++r) {
    const SurveyPoint& p = points[static_cast<size_t>(r)];
    auto* use = new QTableWidgetItem();
    use->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    use->setCheckState(Qt::Unchecked);
    use->setData(Qt::UserRole, p.id);
    list->setItem(r, 0, use);
    list->setItem(r, 1, new QTableWidgetItem(QString::number(p.id)));
    list->setItem(r, 2, new QTableWidgetItem(QString::number(p.northing, 'f', state.displayLinearPrecision)));
    list->setItem(r, 3, new QTableWidgetItem(QString::number(p.easting, 'f', state.displayLinearPrecision)));
    list->setItem(r, 4, new QTableWidgetItem(QString::number(p.elevation, 'f', state.displayLinearPrecision)));
    list->setItem(r, 5, new QTableWidgetItem(FromUtf8(p.description)));
  }
  selectionLayout->addWidget(list, 1);

  auto* split = new QGroupBox("Split table", &dialog);
  auto* splitLayout = new QFormLayout(split);
  auto* splitEnabled = new QCheckBox("Split table", split);
  splitEnabled->setObjectName("pointTableSplit");
  splitEnabled->setChecked(true);
  splitLayout->addRow(splitEnabled);
  auto* maxRows = new QSpinBox(split);
  maxRows->setObjectName("pointTableMaxRows");
  maxRows->setRange(1, 10000);
  maxRows->setValue(20);
  splitLayout->addRow("Maximum rows per table:", maxRows);
  auto* maxTables = new QSpinBox(split);
  maxTables->setObjectName("pointTableMaxStack");
  maxTables->setRange(1, 1000);
  maxTables->setValue(3);
  splitLayout->addRow("Maximum tables per stack:", maxTables);
  auto* offset = new QDoubleSpinBox(split);
  offset->setObjectName("pointTableOffset");
  offset->setRange(0.0, 100000.0);
  offset->setDecimals(4);
  offset->setValue(0.3);
  splitLayout->addRow("Offset:", offset);
  auto* tileRow = new QHBoxLayout;
  tileRow->addWidget(new QLabel("Tile tables:", split));
  auto* across = new QRadioButton("Across", split);
  across->setObjectName("pointTableAcross");
  across->setChecked(true);
  auto* down = new QRadioButton("Down", split);
  down->setObjectName("pointTableDown");
  tileRow->addWidget(across);
  tileRow->addWidget(down);
  tileRow->addStretch(1);
  splitLayout->addRow(tileRow);
  root->addWidget(split);

  auto* behavior = new QGroupBox("Behavior", &dialog);
  auto* behaviorLayout = new QHBoxLayout(behavior);
  behaviorLayout->addWidget(new QLabel("Reactivity mode:", behavior));
  auto* staticMode = new QRadioButton("Static", behavior);
  staticMode->setObjectName("pointTableStatic");
  auto* dynamicMode = new QRadioButton("Dynamic", behavior);
  dynamicMode->setObjectName("pointTableDynamic");
  dynamicMode->setChecked(true);
  behaviorLayout->addWidget(staticMode);
  behaviorLayout->addWidget(dynamicMode);
  behaviorLayout->addStretch(1);
  root->addWidget(behavior);

  QObject::connect(styleEdit, &QPushButton::clicked, &dialog, [&state, styleCombo, &dialog] {
    auto it = std::find_if(state.tableStyles.begin(), state.tableStyles.end(), [&](const CadTableStyle& style) {
      return style.name == styleCombo->currentText().toUtf8().toStdString();
    });
    if (it == state.tableStyles.end()) return;
    QDialog editor(&dialog);
    editor.setWindowTitle("Modify Table Style: " + QString::fromUtf8(it->name.c_str()));
    editor.setStyleSheet("QDialog { background: #f0f0f0; color: #202020; } QLabel { color: #202020; }");
    auto* form = new QFormLayout(&editor);
    auto* textHeight = new QDoubleSpinBox(&editor);
    textHeight->setRange(0.001, 100000.0); textHeight->setDecimals(4); textHeight->setValue(it->textHeightInches);
    auto* columnWidth = new QDoubleSpinBox(&editor);
    columnWidth->setRange(0.001, 100000.0); columnWidth->setDecimals(4); columnWidth->setValue(it->columnWidthInches);
    auto* rowHeight = new QDoubleSpinBox(&editor);
    rowHeight->setRange(0.001, 100000.0); rowHeight->setDecimals(4); rowHeight->setValue(it->rowHeightInches);
    form->addRow("Text height:", textHeight);
    form->addRow("Column width:", columnWidth);
    form->addRow("Row height:", rowHeight);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    if (editor.exec() == QDialog::Accepted) {
      PushUndoSnapshot(state, "ADDPOINTTABLE table style");
      it->textHeightInches = static_cast<float>(textHeight->value());
      it->columnWidthInches = static_cast<float>(columnWidth->value());
      it->rowHeightInches = static_cast<float>(rowHeight->value());
    }
  });

  // A point group checks exactly its members (REQ-474 clause 2). "(none)" leaves the check boxes as they are.
  QObject::connect(groupCombo, &QComboBox::currentIndexChanged, &dialog, [&state, list](int index) {
    if (index <= 0 || index - 1 >= static_cast<int>(state.pointGroups.size()))
      return;
    const std::vector<int> members = ResolvePointGroup(state, state.pointGroups[static_cast<size_t>(index - 1)]);
    const std::set<int> wanted(members.begin(), members.end());
    for (int r = 0; r < list->rowCount(); ++r) {
      QTableWidgetItem* use = list->item(r, 0);
      use->setCheckState(wanted.count(use->data(Qt::UserRole).toInt()) ? Qt::Checked : Qt::Unchecked);
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName("addPointTableButtons");
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  root->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return;

  std::vector<int> ids;
  for (int r = 0; r < list->rowCount(); ++r) {
    QTableWidgetItem* use = list->item(r, 0);
    if (use->checkState() == Qt::Checked)
      ids.push_back(use->data(Qt::UserRole).toInt());
  }
  if (ids.empty()) {
    log.push_back("ADDPOINTTABLE — no points were checked.");
    return;
  }
  const std::string style = styleCombo->currentText().toUtf8().toStdString();
  state.pointTableLayer = tableLayer->text().trimmed().toUtf8().toStdString();
  state.pointTableSplitEnabled = splitEnabled->isChecked();
  state.pointTableMaxRows = maxRows->value();
  state.pointTableMaxStack = maxTables->value();
  state.pointTableOffset = offset->value();
  state.pointTableTileAcross = across->isChecked();
  state.pointTableDynamic = dynamicMode->isChecked();
  BeginAddPointTableWithPoints(state, style, std::move(ids), log);
}
