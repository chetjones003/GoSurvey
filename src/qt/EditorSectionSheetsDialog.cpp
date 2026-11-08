#include "EditorSectionSheetsDialog.hpp"

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentSampleLines.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

// The sample lines of one alignment, found by its name.
std::vector<const alignment::SampleLine*> LinesOf(const AppCommandState& state, const QString& alignmentName) {
  std::vector<const alignment::SampleLine*> lines;
  for (const alignment::Alignment& a : state.alignments) {
    if (QString::fromStdString(a.name) != alignmentName) continue;
    for (const alignment::SampleLine& line : state.sampleLines)
      if (line.alignmentId == a.id) lines.push_back(&line);
  }
  return lines;
}

// REQ-519.8: the axis elevation a user starts from. The lowest ground in the chosen lines, rounded down to 10 ft, or 0
// when no section has ground.
QString DefaultAxisElevation(const std::vector<const alignment::SampleLine*>& lines) {
  double lowest = std::numeric_limits<double>::infinity();
  for (const alignment::SampleLine* line : lines)
    for (const alignment::SectionPoint& p : line->section) lowest = std::min(lowest, p.ground);
  if (!std::isfinite(lowest)) return QStringLiteral("0");
  return QString::number(std::floor(lowest / 10.0) * 10.0, 'f', 0);
}

}  // namespace

QString RunCreateSectionSheetsDialog(QWidget* parent, const AppCommandState& state) {
  // REQ-519.4: sheets are made from sample lines, so only alignments with at least one can be chosen.
  std::vector<QString> alignments;
  for (const alignment::Alignment& a : state.alignments) {
    const QString name = QString::fromStdString(a.name);
    if (!LinesOf(state, name).empty()) alignments.push_back(name);
  }
  if (alignments.empty()) {
    QMessageBox::information(parent, "Create Section Sheets",
                             "Make sample lines for an alignment first (Sample Lines). Sheets are drawn from them.");
    return {};
  }
  QDialog dialog(parent);
  dialog.setWindowTitle("Create Section Sheets");
  auto* form = new QFormLayout;

  auto* alignmentBox = new QComboBox(&dialog);
  for (const QString& name : alignments) alignmentBox->addItem(name);
  form->addRow("Alignment:", alignmentBox);

  // ALL, or sample line names separated by spaces (for example 0+00 0+50). The command reports any name it cannot find.
  auto* linesEdit = new QLineEdit(QStringLiteral("ALL"), &dialog);
  linesEdit->setToolTip("ALL for every sample line on the alignment, or sample line names separated by spaces.");
  form->addRow("Sample lines:", linesEdit);

  // REQ-519.8: four sections per sheet, a default the user can change (1 to 10).
  auto* perSheet = new QSpinBox(&dialog);
  perSheet->setRange(1, 10);
  perSheet->setValue(4);
  perSheet->setToolTip("How many sections go on each sheet, left to right in station order.");
  form->addRow("Sections per sheet:", perSheet);

  // REQ-519.3: the axis every section is drawn about. It starts from the lowest ground in the chosen lines.
  auto* axisEdit = new QLineEdit(DefaultAxisElevation(LinesOf(state, alignments.front())), &dialog);
  axisEdit->setToolTip("The elevation of each section's horizontal axis, in drawing units.");
  form->addRow("Axis elevation:", axisEdit);
  QObject::connect(alignmentBox, &QComboBox::currentTextChanged, &dialog, [&](const QString& name) {
    axisEdit->setText(DefaultAxisElevation(LinesOf(state, name)));
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return {};
  if (alignmentBox->currentIndex() < 0) return {};

  // Commas separate the command's fields, so a list typed with commas is read as names separated by spaces.
  const QString lines = linesEdit->text().replace(',', ' ').simplified();
  return "CREATESECTIONSHEETS " + alignmentBox->currentText() + ", " + lines +
         ", " + QString::number(perSheet->value()) + ", " + axisEdit->text().trimmed();
}
