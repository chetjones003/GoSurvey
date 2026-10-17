#include "EditorWindow.hpp"
#include "ProjectPoints.hpp"
#include "CadCoordinateFrame.hpp"
#include "io/ProjectPointDb.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace {
double PointGridNow() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool PointsReadOnly(const AppCommandState& state) {
  const int index = state.activeDrawingIdx;
  if (index <= 0 || index >= static_cast<int>(state.drawingTabs.size())) return true;
  const auto uid = state.drawingTabs[index].projectUid;
  return std::any_of(state.openProjects.begin(), state.openProjects.end(),
      [uid](const auto& session) { return session.uid == uid && session.readOnly; });
}
// Use numerical ordering without reordering the domain's parallel point/label arrays.
class PointCell final : public QTableWidgetItem {
public:
  explicit PointCell(const QString& value, bool numeric = false) : QTableWidgetItem(value), m_Numeric(numeric) {}
  bool operator<(const QTableWidgetItem& other) const override {
    return m_Numeric ? text().toDouble() < other.text().toDouble() : QTableWidgetItem::operator<(other);
  }
private:
  bool m_Numeric;
};
}

void EditorWindow::ShowPoints(bool database) {
  // Shared points are edited at their database authority, including hidden entries.
  database = database || ActiveProjectDb(m_State) != nullptr;
  if (database && !ActiveProjectDb(m_State)) {
    m_Log.push_back("Survey Point Database: open a drawing with shared project points first.");
    Refresh(); return;
  }
  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("surveyPointGrid");
  dialog.setWindowTitle(database ? "Survey Point Database" : "Drawing Survey Points");
  dialog.resize(1100, 600);
  auto* layout = new QVBoxLayout(&dialog);
  if (PointsReadOnly(m_State)) layout->addWidget(new QLabel("This project is open read-only. Point values cannot be changed.", &dialog));
  auto* filter = new QLineEdit(&dialog); filter->setPlaceholderText("Filter points…");
  filter->setObjectName("pointFilter"); layout->addWidget(filter);
  auto* table = new QTableWidget(&dialog); table->setObjectName("pointGrid");
  table->setColumnCount(9);
  table->setHorizontalHeaderLabels({"Number", "Northing", "Easting", "Elevation", "Description", "Layer", "Visible here", "Source drawing", "Point group"});
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->horizontalHeader()->setSectionsMovable(true);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  table->setAlternatingRowColors(true); layout->addWidget(table);
  auto* tools = new QHBoxLayout;
  auto* select = new QPushButton("Select in drawing", &dialog); select->setObjectName("selectGridPoints");
  auto* hide = new QPushButton("Hide here", &dialog); hide->setVisible(database);
  auto* remove = new QPushButton("Delete selected", &dialog); remove->setVisible(!database);
  auto* reload = new QPushButton("Refresh", &dialog);
  tools->addWidget(select); tools->addWidget(hide); tools->addWidget(remove); tools->addWidget(reload); tools->addStretch();
  layout->addLayout(tools);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto selectedNumbers = [&] {
    std::vector<int> ids;
    for (const auto& index : table->selectionModel()->selectedRows())
      if (!table->isRowHidden(index.row())) ids.push_back(table->item(index.row(), 0)->data(Qt::UserRole).toInt());
    return ids;
  };
  auto applyFilter = [&] {
    for (int row = 0; row < table->rowCount(); ++row) {
      bool match = filter->text().isEmpty();
      for (int column = 0; column < table->columnCount(); ++column)
        match |= table->item(row, column)->text().contains(filter->text(), Qt::CaseInsensitive);
      table->setRowHidden(row, !match);
    }
  };
  std::function<void()> populate = [&] {
    const QSignalBlocker blocker(table);
    const auto selected = selectedNumbers();
    const int sortColumn = table->horizontalHeader()->sortIndicatorSection();
    const auto sortOrder = table->horizontalHeader()->sortIndicatorOrder();
    table->setSortingEnabled(false); table->setRowCount(0);
    const bool readOnly = PointsReadOnly(m_State);
    remove->setEnabled(!readOnly); hide->setEnabled(!readOnly);
    auto append = [&](const SurveyPoint& point, const std::string& source, bool visible) {
      const int row = table->rowCount(); table->insertRow(row);
      QString group;
      for (const auto& candidate : m_State.pointGroups) {
        if (!candidate.rule.empty() && PointMatchesRule(candidate.rule, ParseIdRanges(candidate.rule.idRangesText), point.id, point.description, point.rawDescription)) {
          group = QString::fromUtf8(candidate.name.c_str()); break;
        }
      }
      const QStringList values{QString::number(point.id), QString::number(point.northing, 'g', 17),
          QString::number(point.easting, 'g', 17), QString::number(point.elevation, 'g', 17),
          QString::fromUtf8(point.description.c_str()), QString::fromUtf8(point.layer.c_str()),
          visible ? "Yes" : "No", QString::fromUtf8(source.c_str()), group};
      for (int column = 0; column < values.size(); ++column) {
        auto* cell = new PointCell(values[column], column <= 3);
        cell->setData(Qt::UserRole, point.id);
        if (readOnly || column >= 6 || (database && column == 5)) cell->setFlags(cell->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, cell);
      }
    };
    if (database) {
      if (auto* db = ActiveProjectDb(m_State)) for (const auto& entry : db->points) {
        const bool visible = std::any_of(m_State.surveyPoints.begin(), m_State.surveyPoints.end(),
            [&](const auto& point) { return point.id == entry.point.id; });
        append(entry.point, entry.sourceDrawing, visible);
      }
    } else for (auto point : m_State.surveyPoints) {
      point.easting = CadCoord::WorldXFromLocal(m_State, point.easting);
      point.northing = CadCoord::WorldYFromLocal(m_State, point.northing);
      append(point, {}, true);
    }
    table->setSortingEnabled(true); table->sortItems(sortColumn, sortOrder);
    table->clearSelection();
    for (int row = 0; row < table->rowCount(); ++row) if (std::find(selected.begin(), selected.end(), table->item(row, 0)->data(Qt::UserRole).toInt()) != selected.end())
      for (int column = 0; column < table->columnCount(); ++column) table->item(row, column)->setSelected(true);
    applyFilter();
  };
  connect(filter, &QLineEdit::textChanged, &dialog, applyFilter);
  connect(reload, &QPushButton::clicked, &dialog, [&] { RefreshProjects(); populate(); });
  connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* cell) {
    const int id = cell->data(Qt::UserRole).toInt();
    SurveyPoint point;
    int index = -1;
    bool found = false;
    if (database) {
      if (auto* db = ActiveProjectDb(m_State)) for (const auto& entry : db->points)
        if (entry.point.id == id) { point = entry.point; found = true; break; }
    } else {
      index = SurveyPointIndexForId(m_State, id);
      if (index >= 0) { point = m_State.surveyPoints[index]; found = true; }
    }
    bool valid = found && !PointsReadOnly(m_State);
    const auto text = cell->text();
    if (cell->column() == 0) { bool parsed = false; point.id = text.toInt(&parsed); valid &= parsed && point.id > 0; }
    else if (cell->column() <= 3) {
      bool parsed = false; const double number = text.toDouble(&parsed);
      valid &= parsed && std::isfinite(number);
      if (cell->column() == 1) point.northing = database ? number : number - m_State.worldDocumentOriginY;
      if (cell->column() == 2) point.easting = database ? number : number - m_State.worldDocumentOriginX;
      if (cell->column() == 3) point.elevation = number;
    } else if (cell->column() == 4) point.description = text.toUtf8().toStdString();
    else if (cell->column() == 5 && !database) { point.layer = text.trimmed().toUtf8().toStdString(); valid &= !point.layer.empty(); }
    else valid = false;
    if (valid && database) {
      const auto result = EditDatabasePoint(m_State, id, point, PointGridNow(), m_Log);
      if (result.status == EditDatabasePointStatus::Collision) {
        auto& conflict = m_State.surveyPointGridConflict;
        conflict.active = true; conflict.oldNumber = id; conflict.newNumber = point.id; conflict.pendingValues = point;
        QMessageBox question(QMessageBox::Question, "Point number exists", "Choose how to resolve the duplicate point number.", QMessageBox::NoButton, &dialog);
        auto* overwrite = question.addButton("Overwrite", QMessageBox::DestructiveRole);
        auto* renumber = question.addButton("Next free number", QMessageBox::AcceptRole);
        auto* cancel = question.addButton(QMessageBox::Cancel); question.setDefaultButton(cancel);
        question.exec();
        ResolveGridNumberConflict(m_State, question.clickedButton() == overwrite ? GridConflictAnswer::Overwrite : question.clickedButton() == renumber ? GridConflictAnswer::Renumber : GridConflictAnswer::Cancel, PointGridNow(), m_Log);
      } else valid = result.status == EditDatabasePointStatus::Applied;
    } else if (valid) {
      valid = std::none_of(m_State.surveyPoints.begin(), m_State.surveyPoints.end(), [&](const auto& existing) { return existing.id == point.id && existing.id != id; });
      if (valid) {
        PushUndoSnapshot(m_State, "Edit survey point");
        const int label = FindSurveyLabelAnnIndex(m_State, m_State.surveyPoints[index]);
        m_State.surveyPoints[index] = point;
        if (label >= 0) m_State.cadAnnotations[label].surveyPointLabelForId = point.id;
        if (static_cast<std::size_t>(index) < m_State.surveyPointIdBuffers.size()) m_State.surveyPointIdBuffers[index] = std::to_string(point.id);
        EnsureSurveyPointLabelMtext(m_State, index, &m_Log); BumpCadGpuCache(m_State);
      }
    }
    if (!valid) QMessageBox::warning(&dialog, "Survey point edit", "The edit could not be applied. Enter a finite coordinate, a unique positive point number, or a valid layer in a writable project.");
    RefreshProjects(); populate(); Refresh();
  });
  auto selectPoints = [&] {
    const auto ids = selectedNumbers(); ClearSelection(m_State);
    int missing = 0;
    for (int id : ids) { const int index = SurveyPointIndexForId(m_State, id); if (index >= 0) {
      m_State.selectedSurveyPointIndices.push_back(index); SyncSurveyPointLinkedMtextSelection(m_State, index);
    } else ++missing; }
    if (missing) m_Log.push_back("Survey points: " + std::to_string(missing) + " selected database point(s) are hidden in this drawing.");
  };
  connect(select, &QPushButton::clicked, &dialog, [&] { selectPoints(); dialog.accept(); });
  connect(hide, &QPushButton::clicked, &dialog, [&] {
    if (PointsReadOnly(m_State)) return;
    PushUndoSnapshot(m_State, "Hide survey points here"); selectPoints(); HideSelectedPointsHere(m_State);
    RefreshProjects(); populate(); Refresh();
  });
  connect(remove, &QPushButton::clicked, &dialog, [&] {
    if (PointsReadOnly(m_State)) return;
    const auto ids = selectedNumbers(); if (ids.empty()) return;
    if (QMessageBox::question(&dialog, "Delete survey points", QString("Delete %1 selected point(s)?").arg(ids.size()), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    PushUndoSnapshot(m_State, "Delete survey points");
    for (int id : ids) { const int index = SurveyPointIndexForId(m_State, id); if (index >= 0) RemoveSurveyPointAt(m_State, index); }
    BumpCadGpuCache(m_State); RefreshProjects(); populate(); Refresh();
  });
  populate(); table->resizeColumnsToContents();
  dialog.exec(); m_ModalOpen = false; Refresh();
}
