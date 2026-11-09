#include "EditorSampleLinesDialog.hpp"

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentSampleLines.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

#include <cmath>
#include <string>

QString RunCreateSampleLinesDialog(QWidget* parent, const AppCommandState& state) {
  if (state.alignments.empty()) {  // REQ-519.1: sample lines run along an alignment, so one must exist
    QMessageBox::information(parent, "Sample Lines", "Create an alignment first. Sample lines are placed along one.");
    return {};
  }
  QDialog dialog(parent);
  dialog.setWindowTitle("Sample Lines");
  auto* form = new QFormLayout;

  auto* alignmentBox = new QComboBox(&dialog);
  for (const alignment::Alignment& a : state.alignments) alignmentBox->addItem(QString::fromStdString(a.name));
  form->addRow("Alignment:", alignmentBox);

  // The default range is the whole alignment, from its start station to its end station (as the view frames use).
  // A whole station is shown without decimals; a fractional one keeps the drawing's, so the end never rounds past itself.
  // The start rounds up and the end rounds down to the drawing's decimals, so the prefilled range never leaves the alignment.
  auto stationText = [&state](double displayed, bool roundUp) {
    alignment::StationFormat fmt = state.alignmentStationFormat;
    if (std::fabs(displayed - std::round(displayed)) < 1e-6) fmt.decimals = 0;  // a whole station shows no decimals
    const double scale = std::pow(10.0, fmt.decimals);
    const double scaled = displayed * scale;
    displayed = (roundUp ? std::ceil(scaled - 1e-9) : std::floor(scaled + 1e-9)) / scale;
    return QString::fromStdString(alignment::FormatStation(displayed, fmt));
  };
  const alignment::Alignment& first = state.alignments.front();
  auto* startEdit = new QLineEdit(stationText(first.DisplayedStation(first.StartStation(), true), true), &dialog);
  auto* endEdit = new QLineEdit(stationText(first.DisplayedStation(first.EndStation(), true), false), &dialog);
  form->addRow("Start station:", startEdit);
  form->addRow("End station:", endEdit);
  // Choosing another alignment moves the range to that alignment's own start and end.
  QObject::connect(alignmentBox, &QComboBox::currentIndexChanged, &dialog,
                   [&state, stationText, startEdit, endEdit](int index) {
                     if (index < 0 || index >= static_cast<int>(state.alignments.size())) return;
                     const alignment::Alignment& chosen = state.alignments[static_cast<size_t>(index)];
                     startEdit->setText(stationText(chosen.DisplayedStation(chosen.StartStation(), true), true));
                     endEdit->setText(stationText(chosen.DisplayedStation(chosen.EndStation(), true), false));
                   });

  // REQ-519.8: the interval and swaths are in drawing units, as the view frames are.
  auto* intervalEdit = new QLineEdit(QStringLiteral("50"), &dialog);
  auto* leftEdit = new QLineEdit(QStringLiteral("50"), &dialog);
  auto* rightEdit = new QLineEdit(QStringLiteral("50"), &dialog);
  intervalEdit->setToolTip("Distance between sample lines along the alignment, in drawing units.");
  leftEdit->setToolTip("Swath to the left of the alignment (zero or more).");
  rightEdit->setToolTip("Swath to the right of the alignment (zero or more).");
  form->addRow("Interval:", intervalEdit);
  form->addRow("Left swath:", leftEdit);
  form->addRow("Right swath:", rightEdit);

  // REQ-519.9: the surface each line is sampled against. None makes lines with no sections.
  auto* surfaceBox = new QComboBox(&dialog);
  surfaceBox->addItem("None");
  for (const CadSurface& surface : state.cadSurfaces)
    if (surface.kind == SurfaceKind::Tin && surface.tin) surfaceBox->addItem(QString::fromStdString(surface.name));
  surfaceBox->setToolTip("The TIN surface to take each line's section from. None gives lines with no sections.");
  form->addRow("Surface:", surfaceBox);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return {};
  if (alignmentBox->currentIndex() < 0) return {};

  // Commas separate the command's fields, so every value is trimmed and sent as typed.
  QString command = "CREATESAMPLELINES " + alignmentBox->currentText() + ", " + startEdit->text().trimmed() + ", " +
                    endEdit->text().trimmed() + ", " + intervalEdit->text().trimmed() + ", " + leftEdit->text().trimmed() +
                    ", " + rightEdit->text().trimmed();
  if (surfaceBox->currentIndex() > 0) command += ", " + surfaceBox->currentText();
  return command;
}
