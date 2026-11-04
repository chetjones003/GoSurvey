#include "EditorAlignmentLayout.hpp"

#include "CadCommands.hpp"
#include "alignment/AlignmentDesign.hpp"
#include "AlignmentStyle.hpp"
#include "util/NumFormat.hpp"

#include <QColor>
#include <QComboBox>
#include <QStringList>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr double kRadToDeg = 180.0 / alignment::kPi;

enum Column { ColNo, ColType, ColStart, ColEnd, ColLength, ColRadius, ColDelta, ColBearing, ColDirection, ColConstraint, ColStatus, ColDesign, ColCount };

const char* const kTypeNames[] = {"", "Centerline", "Offset", "Curb Return", "Miscellaneous", "Rail"};

QString Q(const std::string& s) { return QString::fromUtf8(s.c_str()); }

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

}  // namespace

void ShowAlignmentLayoutDialog(QWidget* parent, AppCommandState& state, std::vector<std::string>& log,
                               const std::string& initialName) {
  QDialog dialog(parent);
  dialog.setObjectName("alignmentLayoutDialog");
  dialog.setWindowTitle("Alignment Layout");
  dialog.resize(1080, 380);
  auto* root = new QVBoxLayout(&dialog);
  auto* top = new QHBoxLayout;
  top->addWidget(new QLabel("Alignment:", &dialog));
  auto* chooser = new QComboBox(&dialog);
  chooser->setObjectName("alignmentLayoutChooser");
  top->addWidget(chooser);
  auto* summary = new QLabel(&dialog);
  summary->setObjectName("alignmentLayoutSummary");
  top->addWidget(summary, 1);
  root->addLayout(top);

  auto* table = new QTableWidget(&dialog);
  table->setObjectName("alignmentLayoutTable");
  table->setColumnCount(ColCount);
  table->setHorizontalHeaderLabels({"#", "Type", "Start station", "End station", "Length", "Radius", "Delta (deg)", "Bearing (deg)",
                                    "Direction", "Constraint", "Continuity", "Design check"});
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setStretchLastSection(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  root->addWidget(table, 1);

  auto* status = new QLabel(&dialog);
  status->setObjectName("alignmentLayoutStatus");
  status->setWordWrap(true);
  root->addWidget(status);
  auto* hint = new QLabel("Edit Length, Radius, Delta, Bearing, Direction (LEFT/RIGHT) or Constraint. A derived value is read-only; "
                          "a refused edit changes nothing and says why.", &dialog);
  hint->setWordWrap(true);
  root->addWidget(hint);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  auto* close = new QPushButton("Close", &dialog);
  buttons->addWidget(close);
  root->addLayout(buttons);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

  const auto currentName = [&]() { return chooser->currentText().toUtf8().toStdString(); };
  const auto findCurrent = [&]() -> const alignment::Alignment* {
    const std::string name = currentName();
    for (const alignment::Alignment& a : state.alignments) {
      if (a.name == name) return &a;
    }
    return nullptr;
  };

  const auto populate = [&]() {
    QSignalBlocker block(table);
    table->setRowCount(0);
    const alignment::Alignment* a = findCurrent();
    if (a == nullptr) {
      summary->setText("No alignments in this drawing.");
      return;
    }
    const int prec = state.displayLinearPrecision;
    const auto lin = [&](double v) { return Q(FormatLinear(v, prec)); };
    const auto deg = [&](double rad) { return QString::number(rad * kRadToDeg, 'f', 6); };
    summary->setText(QString("%1   |   %2 to %3   |   length %4%5")
                         .arg(a->parentId != 0 ? QString("Offset (linked: edit the parent)")
                                           : QString(kTypeNames[static_cast<int>(a->type)]))
                         .arg(Q(alignment::FormatStation(a->StartStation())))
                         .arg(Q(alignment::FormatStation(a->EndStationWithEquations())))
                         .arg(lin(a->Length()))
                         .arg(a->equations.empty() ? QString() : QString("   |   %1 station equation(s)").arg(a->equations.size())));
    const std::vector<alignment::ContinuityIssue> issues = a->CheckContinuity();
    // REQ-502: the failed checks, computed once for the table
    std::vector<alignment::DesignFinding> designFindings;
    if (const alignment::AlignmentCheckSet* checkSet =
            alignment::FindCheckSet(state.alignmentCheckSets, AlignmentStyles::EffectiveCheckSetName(state.alignmentStyles, *a)))
      designFindings = alignment::CheckDesign(*a, *checkSet, prec);
    table->setRowCount(static_cast<int>(a->Entities().size()));
    for (size_t i = 0; i < a->Entities().size(); ++i) {
      const alignment::Entity& e = a->Entities()[i];
      const int row = static_cast<int>(i);
      const bool arc = e.kind == alignment::EntityKind::Arc;
      const bool spiral = e.kind == alignment::EntityKind::Spiral;
      const auto set = [&](int col, const QString& text, bool editable) {
        auto* item = new QTableWidgetItem(text);
        Qt::ItemFlags flags = item->flags() & ~Qt::ItemIsEditable;
        if (editable && a->parentId == 0) flags |= Qt::ItemIsEditable;
        item->setFlags(flags);
        table->setItem(row, col, item);
      };
      set(ColNo, QString::number(i + 1), false);
      set(ColType, arc ? "Curve" : spiral ? "Spiral" : "Tangent", false);
      set(ColStart, Q(alignment::FormatStation(a->DisplayedStation(a->EntityStartStation(row), true))), false);
      set(ColEnd, Q(alignment::FormatStation(a->DisplayedStation(a->EntityEndStation(row), false))), false);
      set(ColLength, lin(e.length), true);
      alignment::CurveData cd;
      alignment::SpiralData sd;
      if (arc) (void)a->CurveDataFor(row, &cd);
      if (spiral) (void)alignment::SpiralDataFor(row, *a, &sd);
      set(ColRadius, arc ? lin(e.radius) : spiral ? lin(e.radius) : QString::fromUtf8("\xE2\x80\x94"), arc || spiral);
      set(ColDelta, arc ? deg(cd.delta) : spiral ? deg(sd.deflection) : QString::fromUtf8("\xE2\x80\x94"), arc);
      const bool bearingEditable = !arc && !spiral && e.constraint == alignment::Entity::Constraint::Fixed;
      set(ColBearing, deg(e.startAzimuth), bearingEditable);
      set(ColDirection, (arc || spiral) ? (e.right ? "RIGHT" : "LEFT") : QString::fromUtf8("\xE2\x80\x94"), arc);
      set(ColConstraint, ConstraintName(e.constraint), true);
      QString cont = QString::fromUtf8("\xE2\x80\x94");
      if (i + 1 < a->Entities().size() && i < issues.size()) {
        const alignment::ContinuityIssue& iss = issues[static_cast<size_t>(i)];
        if (iss.FailsRequirement(a->continuityRequirement))
          cont = !iss.c0 ? QString("gap %1").arg(lin(iss.gap))
                         : !iss.c1 ? "kink (C0 only)"
                                   : !iss.c2 ? QString("curvature %1 (C2)").arg(lin(iss.curvatureGap)) : cont;
        else
          cont = iss.c2 ? "C0 C1 C2" : "C0 C1";
      }
      set(ColStatus, cont, false);
      QStringList found;
      for (const alignment::DesignFinding& fd : designFindings) {
        if (fd.entity == row + 1) found << Q(fd.text);
      }
      set(ColDesign, found.join(" "), false);
      if (!found.isEmpty()) table->item(row, ColDesign)->setForeground(QColor("#c0392b"));
    }
    table->resizeColumnsToContents();
  };

  for (const alignment::Alignment& a : state.alignments) chooser->addItem(Q(a.name));
  if (!initialName.empty()) {
    const int ix = chooser->findText(Q(initialName), Qt::MatchFixedString);
    if (ix >= 0) chooser->setCurrentIndex(ix);
  }
  QObject::connect(chooser, &QComboBox::currentIndexChanged, &dialog, [&] { status->clear(); populate(); });
  populate();

  QObject::connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
    const alignment::Alignment* a = findCurrent();
    if (a == nullptr || item == nullptr) return;
    const int row = item->row();
    std::string command = "ALIGNMENTEDIT " + a->name + ", " + std::to_string(row + 1) + ", ";
    const std::string text = item->text().trimmed().toUtf8().toStdString();
    switch (item->column()) {
      case ColLength: command += "LENGTH, " + text; break;
      case ColRadius: command += "RADIUS, " + text; break;
      case ColDelta: command += "DELTA, " + text; break;
      case ColBearing: command += "BEARING, " + text; break;
      case ColDirection: command += "DIRECTION, " + text; break;
      case ColConstraint: {
        const std::string word = ConstraintWord(item->text());
        if (word.empty()) {
          status->setText("The constraint must be Fixed, Float (prev), Float (next) or Free.");
          QTimer::singleShot(0, &dialog, populate);
          return;
        }
        command += "CONSTRAINT, " + word;
        break;
      }
      default: return;
    }
    std::vector<std::string> out;
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s", command.c_str());
    ProcessCommandLineSubmit(buf, sizeof buf, state, out);
    for (const std::string& line : out) log.push_back(line);
    QString message;
    for (const std::string& line : out) message += Q(line) + "\n";
    status->setText(message.trimmed());
    QTimer::singleShot(0, &dialog, populate);  // always show the real values, edited or refused (after this signal returns)
  });

  dialog.exec();
}
