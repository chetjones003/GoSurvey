#include "EditorPointTableDialog.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include "util/cadtablestyle.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QObject>
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
  dialog.resize(780, 560);

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
  controls->addWidget(new QLabel("Point group:", &dialog));
  auto* groupCombo = new QComboBox(&dialog);
  groupCombo->setObjectName("pointTableGroup");
  groupCombo->addItem("(none)");
  for (const PointGroup& g : state.pointGroups)
    groupCombo->addItem(FromUtf8(g.name));
  controls->addWidget(groupCombo, 1);
  root->addLayout(controls);

  auto* list = new QTableWidget(static_cast<int>(points.size()), 6, &dialog);
  list->setObjectName("pointTableList");
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
  root->addWidget(list, 1);

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
  BeginAddPointTableWithPoints(state, style, std::move(ids), log);
}
