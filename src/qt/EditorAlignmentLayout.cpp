#include "EditorAlignmentLayout.hpp"

#include "CadCommands.hpp"
#include "EditorWindow.hpp"
#include "alignment/AlignmentDesign.hpp"
#include "AlignmentStyle.hpp"
#include "util/NumFormat.hpp"

#include <QColor>
#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QFont>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {

constexpr double kRadToDeg = 180.0 / alignment::kPi;

enum Column {
  ColNo, ColType, ColStart, ColEnd, ColLength, ColRadius, ColStartRadius, ColDelta, ColA, ColBearing, ColDirection,
  ColTangency, ColParameter, ColLocks, ColStatus, ColCriteria, ColDesign, ColCount
};

constexpr std::array<const char*, 6> kTypeNames = {"", "Centerline", "Offset", "Curb Return", "Miscellaneous", "Rail"};

QString Q(const std::string& s) { return QString::fromUtf8(s.c_str()); }
QString Dash() { return QString::fromUtf8("\xE2\x80\x94"); }

const char* ConstraintName(alignment::Entity::Constraint c) {
  switch (c) {
    case alignment::Entity::Constraint::Fixed: return "Fixed";
    case alignment::Entity::Constraint::FloatOnPrev: return "Float (prev)";
    case alignment::Entity::Constraint::FloatOnNext: return "Float (next)";
    case alignment::Entity::Constraint::Free: return "Free";
  }
  return "?";
}

/// Reads a typed constraint back into the command word, accepting the displayed names.
std::string ConstraintWord(const QString& text) {
  const QString t = text.trimmed().toLower().remove(' ').remove('(').remove(')');
  if (t == "fixed") return "FIXED";
  if (t == "floatprev" || t == "floatonprev") return "FLOATPREV";
  if (t == "floatnext" || t == "floatonnext") return "FLOATNEXT";
  if (t == "free") return "FREE";
  return "";
}

QString LockNames(unsigned char locks) {
  QStringList names;
  if ((locks & alignment::Entity::kLockRadius) != 0) names << "radius";
  if ((locks & alignment::Entity::kLockLength) != 0) names << "length";
  if ((locks & alignment::Entity::kLockBearing) != 0) names << "bearing";
  if ((locks & alignment::Entity::kLockStation) != 0) names << "station";
  return names.isEmpty() ? Dash() : names.join(", ");
}

/// The ALIGNMENTEDIT word for an editable column, or "" for a read-only one.
const char* EditWord(int column) {
  switch (column) {
    case ColLength: return "LENGTH";
    case ColRadius: return "RADIUS";
    case ColStartRadius: return "STARTRADIUS";
    case ColDelta: return "DELTA";
    case ColBearing: return "BEARING";
    case ColDirection: return "DIRECTION";
    case ColTangency: return "CONSTRAINT";
    default: return "";
  }
}

/// The junction after `row`, at the alignment's continuity level (REQ-482).
QString ContinuityText(const alignment::Alignment& a, int row, const std::vector<alignment::ContinuityIssue>& issues, int prec) {
  assert(row >= 0);
  assert(prec >= 0);
  if (static_cast<size_t>(row) >= issues.size() || static_cast<size_t>(row) + 1 >= a.Entities().size()) return Dash();
  const alignment::ContinuityIssue& iss = issues[static_cast<size_t>(row)];
  const auto lin = [&](double v) { return Q(FormatLinear(v, prec)); };
  if (!iss.FailsRequirement(a.continuityRequirement)) return iss.c2 ? "C0 C1 C2" : "C0 C1";
  if (!iss.c0) return QString("gap %1").arg(lin(iss.gap));
  if (!iss.c1) return "kink (C0 only)";
  if (!iss.c2) return QString("curvature %1 (C2)").arg(lin(iss.curvatureGap));
  return Dash();
}

}  // namespace

AlignmentLayoutPanel::AlignmentLayoutPanel(AppCommandState& state, std::vector<std::string>& log,
                                           std::function<void()> changed, QWidget* parent)
    : QWidget(parent), state_(state), log_(log), changed_(std::move(changed)) {
  setObjectName("alignmentLayoutPanel");
  auto* root = new QVBoxLayout(this);
  auto* top = new QHBoxLayout;
  top->addWidget(new QLabel("Alignment:", this));
  chooser_ = new QComboBox(this);
  chooser_->setObjectName("alignmentLayoutChooser");
  top->addWidget(chooser_);
  summary_ = new QLabel(this);
  summary_->setObjectName("alignmentLayoutSummary");
  top->addWidget(summary_, 1);
  root->addLayout(top);
  table_ = new QTableWidget(this);
  table_->setObjectName("alignmentLayoutTable");
  table_->setColumnCount(ColCount);
  table_->setHorizontalHeaderLabels({"#", "Type", "Start station", "End station", "Length", "Radius", "Start radius",
                                     "Delta (deg)", "A", "Bearing (deg)", "Direction", "Tangency constraint",
                                     "Parameter constraint", "Locked", "Continuity", "Criteria", "Design check"});
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setSectionResizeMode(ColDesign, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setWordWrap(true);
  root->addWidget(table_, 1);
  status_ = new QLabel(this);
  status_->setObjectName("alignmentLayoutStatus");
  status_->setWordWrap(true);
  root->addWidget(status_);
  auto* hint = new QLabel("Only the values an entity was defined by can be edited; greyed values are derived. Locks: "
                          "ALIGNMENTLOCK. A refused edit changes nothing and says why.", this);
  hint->setWordWrap(true);
  root->addWidget(hint);
  QObject::connect(chooser_, &QComboBox::currentIndexChanged, this, [this] {
    status_->clear();
    Populate();
  });
  QObject::connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) { OnItemChanged(item); });
  FillChooser();
  Populate();
}

void AlignmentLayoutPanel::ShowAlignment(const std::string& name) {
  FillChooser();
  if (!name.empty()) {
    const int ix = chooser_->findText(Q(name), Qt::MatchFixedString);
    if (ix >= 0) {
      const QSignalBlocker block(chooser_);
      chooser_->setCurrentIndex(ix);
    }
  }
  status_->clear();
  Populate();
}

void AlignmentLayoutPanel::Sync() {
  if (filledOnce_ && filledRevision_ == state_.cadGpuRevision && filledDrawing_ == state_.activeDrawingIdx) return;
  FillChooser();
  Populate();
}

void AlignmentLayoutPanel::FillChooser() {
  const QString keep = chooser_->currentText();
  const QSignalBlocker block(chooser_);
  chooser_->clear();
  for (const alignment::Alignment& a : state_.alignments) chooser_->addItem(Q(a.name));
  const int ix = chooser_->findText(keep, Qt::MatchFixedString);
  if (ix >= 0) chooser_->setCurrentIndex(ix);
}

const alignment::Alignment* AlignmentLayoutPanel::Current() const {
  const std::string name = chooser_->currentText().toUtf8().toStdString();
  for (const alignment::Alignment& a : state_.alignments)
    if (a.name == name) return &a;
  return nullptr;
}

void AlignmentLayoutPanel::Populate() {
  const QSignalBlocker block(table_);
  filledOnce_ = true;
  filledRevision_ = state_.cadGpuRevision;
  filledDrawing_ = state_.activeDrawingIdx;
  const int keepRow = table_->currentRow();
  table_->setRowCount(0);
  const alignment::Alignment* a = Current();
  if (a == nullptr) {
    summary_->setText("No alignments in this drawing.");
    return;
  }
  const auto type = static_cast<size_t>(a->type);
  summary_->setText(QString("%1   |   %2 to %3   |   length %4%5")
                        .arg(a->parentId != 0 ? QString("Offset (linked: edit the parent)")
                                              : QString(type < kTypeNames.size() ? kTypeNames[type] : ""))
                        .arg(Q(alignment::FormatStation(a->StartStation())))
                        .arg(Q(alignment::FormatStation(a->EndStationWithEquations())))
                        .arg(Q(FormatLinear(a->Length(), state_.displayLinearPrecision)))
                        .arg(a->equations.empty() ? QString() : QString("   |   %1 station equation(s)").arg(a->equations.size())));
  table_->setRowCount(static_cast<int>(a->Entities().size()));
  RowContext ctx;
  ctx.issues = a->CheckContinuity();
  ctx.checkSet =
      alignment::FindCheckSet(state_.alignmentCheckSets, AlignmentStyles::EffectiveCheckSetName(state_.alignmentStyles, *a));
  if (ctx.checkSet != nullptr) ctx.findings = alignment::CheckDesign(*a, *ctx.checkSet, state_.displayLinearPrecision);
  for (int row = 0; row < table_->rowCount(); ++row) PopulateRow(*a, row, ctx);
  for (int col = 0; col < ColDesign; ++col) table_->resizeColumnToContents(col);
  table_->resizeRowsToContents();  // the Design check text wraps in the last column, which takes the remaining width
  if (keepRow >= 0 && keepRow < table_->rowCount()) table_->setCurrentCell(keepRow, 0);
}

void AlignmentLayoutPanel::PopulateRow(const alignment::Alignment& a, int row, const RowContext& ctx) {
  assert(row >= 0 && static_cast<size_t>(row) < a.Entities().size());
  const alignment::Entity& e = a.Entities()[static_cast<size_t>(row)];
  const bool arc = e.kind == alignment::EntityKind::Arc;
  const bool spiral = e.kind == alignment::EntityKind::Spiral;
  const int prec = state_.displayLinearPrecision;
  const QColor derived = table_->palette().color(QPalette::Disabled, QPalette::Text);
  const auto set = [&](int col, const QString& text, bool editable) {
    auto* item = new QTableWidgetItem(text);
    const bool canEdit = editable && a.parentId == 0;
    item->setFlags(canEdit ? (item->flags() | Qt::ItemIsEditable) : (item->flags() & ~Qt::ItemIsEditable));
    if (!canEdit && col >= ColLength && col <= ColDirection) item->setForeground(derived);
    table_->setItem(row, col, item);
  };
  const auto lin = [&](double v) { return Q(FormatLinear(v, prec)); };
  const auto deg = [](double rad) { return QString::number(rad * kRadToDeg, 'f', 6); };
  const auto editable = [&](alignment::EditParam p) { return alignment::ParameterEditable(e, p); };
  alignment::CurveData cd;
  alignment::SpiralData sd;
  if (arc) (void)a.CurveDataFor(row, &cd);
  if (spiral) (void)alignment::SpiralDataFor(row, a, &sd);
  set(ColNo, QString::number(row + 1), false);
  set(ColType, arc ? "Curve" : spiral ? "Spiral" : "Tangent", false);
  set(ColStart, Q(alignment::FormatStation(a.DisplayedStation(a.EntityStartStation(row), true))), false);
  set(ColEnd, Q(alignment::FormatStation(a.DisplayedStation(a.EntityEndStation(row), false))), false);
  set(ColLength, lin(e.length), editable(arc || spiral ? alignment::EditParam::CurveLength : alignment::EditParam::LineLength));
  set(ColRadius, arc || spiral ? lin(e.radius) : Dash(), editable(alignment::EditParam::CurveRadius));
  set(ColStartRadius, spiral ? lin(e.startRadius) : Dash(), editable(alignment::EditParam::SpiralStartRadius));
  set(ColDelta, arc ? deg(cd.delta) : spiral ? deg(sd.deflection) : Dash(), editable(alignment::EditParam::CurveDelta));
  set(ColA, spiral ? lin(sd.parameterA) : Dash(), false);
  set(ColBearing, deg(e.startAzimuth), editable(alignment::EditParam::LineAzimuth));
  set(ColDirection, arc || spiral ? (e.right ? "RIGHT" : "LEFT") : Dash(), editable(alignment::EditParam::CurveDirection));
  set(ColTangency, ConstraintName(e.constraint), true);
  set(ColParameter, alignment::ParameterConstraintName(e), false);
  set(ColLocks, LockNames(e.locks), false);
  set(ColStatus, ContinuityText(a, row, ctx.issues, prec), false);
  // REQ-486.5 / REQ-502: what the criteria require of this row, and what failed.
  set(ColCriteria, ctx.checkSet != nullptr ? Q(alignment::CriteriaRequirementText(a, *ctx.checkSet, row, prec)) : QString(),
      false);
  QStringList found;
  for (const alignment::DesignFinding& fd : ctx.findings)
    if (fd.entity == row + 1) found << Q(fd.text);
  set(ColDesign, found.join(" "), false);
  if (!found.isEmpty()) MarkFailingRow(row, found);
}

// A failing row reads at a glance, as Civil 3D marks it: a warning icon on the row number, bold text, and the full
// finding as a tooltip on its number and its Design check cell.
void AlignmentLayoutPanel::MarkFailingRow(int row, const QStringList& found) {
  assert(row >= 0 && row < table_->rowCount());
  assert(!found.isEmpty());
  table_->item(row, ColDesign)->setForeground(QColor("#c0392b"));
  table_->item(row, ColNo)->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
  for (int col = 0; col < ColCount; ++col) {
    QTableWidgetItem* cell = table_->item(row, col);
    if (cell == nullptr) continue;
    QFont font = cell->font();
    font.setBold(true);
    cell->setFont(font);
  }
  table_->item(row, ColNo)->setToolTip(found.join("\n"));
  table_->item(row, ColDesign)->setToolTip(found.join("\n"));
}

void AlignmentLayoutPanel::OnItemChanged(QTableWidgetItem* item) {
  const alignment::Alignment* a = Current();
  if (a == nullptr || item == nullptr) return;
  const std::string word = EditWord(item->column());
  if (word.empty()) return;
  std::string value = item->text().trimmed().toUtf8().toStdString();
  if (item->column() == ColTangency) {
    value = ConstraintWord(item->text());
    if (value.empty()) {
      status_->setText("The tangency constraint must be Fixed, Float (prev), Float (next) or Free.");
      QTimer::singleShot(0, this, [this] { Populate(); });
      return;
    }
  }
  const std::string command = "ALIGNMENTEDIT " + a->name + ", " + std::to_string(item->row() + 1) + ", " + word + ", " + value;
  std::vector<std::string> out;
  char buf[512];
  std::snprintf(buf, sizeof buf, "%s", command.c_str());
  ProcessCommandLineSubmit(buf, sizeof buf, state_, out);
  QString message;
  for (const std::string& line : out) {
    log_.push_back(line);
    message += Q(line) + "\n";
  }
  status_->setText(message.trimmed());
  // Show the real values, edited or refused, once this signal has returned; then let the window redraw.
  QTimer::singleShot(0, this, [this] {
    Populate();
    if (changed_) changed_();
  });
}

void EditorWindow::ShowAlignmentLayout(const std::string& name) {
  auto* dock = findChild<QDockWidget*>("alignmentLayoutDock");
  if (dock == nullptr) {
    dock = new QDockWidget("Alignment Layout", this);
    dock->setObjectName("alignmentLayoutDock");
    dock->setWidget(new AlignmentLayoutPanel(m_State, m_Log, [this] { Refresh(); }, dock));
    addDockWidget(Qt::BottomDockWidgetArea, dock);
  }
  dock->show();
  dock->raise();
  if (auto* panel = dynamic_cast<AlignmentLayoutPanel*>(dock->widget())) panel->ShowAlignment(name);
}

void EditorWindow::SyncAlignmentLayout() {
  auto* dock = findChild<QDockWidget*>("alignmentLayoutDock");
  if (dock == nullptr || dock->isHidden()) return;  // closed docks are hidden; an open one may be in an unshown window
  if (auto* panel = dynamic_cast<AlignmentLayoutPanel*>(dock->widget())) panel->Sync();
}
