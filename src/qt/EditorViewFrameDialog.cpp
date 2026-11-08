#include "EditorViewFrameDialog.hpp"

#include "CadCommands.hpp"
#include "PaperSpace.hpp"
#include "ViewportZoomScale.hpp"
#include "alignment/Alignment.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace {

// Typed station text for a stored internal station, so the dialog opens on the alignment's own stationing. The text
// keeps six decimals (the drawing's display precision could round an endpoint outside the alignment), and the start
// is nudged up and the end down by a millionth so that rounding at six decimals stays inside the range.
QString StationText(const AppCommandState& state, const alignment::Alignment& a, double internalStation, bool isEnd) {
  alignment::StationFormat fmt = state.alignmentStationFormat;
  fmt.decimals = std::max(fmt.decimals, 6);
  const double displayed = a.DisplayedStation(internalStation, true) + (isEnd ? -1e-6 : 1e-6);
  return QString::fromStdString(alignment::FormatStation(displayed, fmt));
}

}  // namespace

QString RunCreateViewFramesDialog(QWidget* parent, const AppCommandState& state) {
  if (state.alignments.empty()) return {};

  QDialog dialog(parent);
  dialog.setWindowTitle("Create View Frames");
  auto* form = new QFormLayout;

  auto* alignmentBox = new QComboBox(&dialog);
  for (const alignment::Alignment& a : state.alignments)
    alignmentBox->addItem(QString::fromStdString(a.name));
  form->addRow("Alignment:", alignmentBox);

  auto* startEdit = new QLineEdit(&dialog);
  auto* endEdit = new QLineEdit(&dialog);
  auto fillRange = [&](int index) {
    const alignment::Alignment& a = state.alignments[static_cast<size_t>(index)];
    startEdit->setText(StationText(state, a, a.StartStation(), false));
    endEdit->setText(StationText(state, a, a.EndStation(), true));
  };
  fillRange(0);
  QObject::connect(alignmentBox, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
    if (index >= 0 && index < static_cast<int>(state.alignments.size())) fillRange(index);
  });
  form->addRow("Start station:", startEdit);
  form->addRow("End station:", endEdit);

  auto* sheetBox = new QComboBox(&dialog);
  int arch = 0;
  for (int i = 0; i < kPaperSizePresetCount; ++i) {
    sheetBox->addItem(QString::fromUtf8(kPaperSizePresets[i].name));
    if (QString::fromUtf8(kPaperSizePresets[i].name).startsWith("ARCH D")) arch = i;
  }
  sheetBox->setCurrentIndex(arch);
  form->addRow("Sheet size:", sheetBox);

  // The named scales (REQ-517.3): the drawing's own list, or the standard list when it has none yet.
  std::vector<CadAnnotationScale> scales = state.viewportZoomScales;
  if (scales.empty()) EnsureStandardAnnotationScales(scales, state.drawingInsUnits);
  auto* scaleBox = new QComboBox(&dialog);
  int scaleDefault = 0;
  for (size_t i = 0; i < scales.size(); ++i) {
    scaleBox->addItem(QString::fromStdString(scales[i].name));
    if (scales[i].name == "1\" = 50'") scaleDefault = static_cast<int>(i);  // a plan sheet's usual scale, when listed
  }
  scaleBox->setCurrentIndex(scaleDefault);
  form->addRow("Scale:", scaleBox);

  auto* overlapEdit = new QLineEdit(QStringLiteral("0"), &dialog);
  overlapEdit->setValidator(new QDoubleValidator(0.0, 1e12, 4, overlapEdit));
  form->addRow("Overlap:", overlapEdit);

  auto* placementBox = new QComboBox(&dialog);
  placementBox->addItem("Centred", "CENTRED");
  placementBox->addItem("Left", "LEFT");
  placementBox->addItem("Right", "RIGHT");
  form->addRow("Alignment position:", placementBox);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return {};
  if (alignmentBox->currentIndex() < 0 || scaleBox->currentIndex() < 0 || sheetBox->currentIndex() < 0) return {};

  // Every value goes in as typed text, so the command's own checks report any problem in the log.
  const QString name = alignmentBox->currentText();
  // The overlap is read in the user's locale (a decimal comma is fine here) and written back in the C locale, so the
  // command's comma-separated fields never contain a decimal comma.
  bool overlapOk = false;
  const double overlap = QLocale().toDouble(overlapEdit->text().trimmed(), &overlapOk);
  if (!overlapOk) {
    QMessageBox::warning(parent, "Create View Frames", "The overlap must be a number.");
    return {};
  }
  return "CREATEVIEWFRAMES " + name + ", " + startEdit->text().trimmed() + ", " + endEdit->text().trimmed() + ", " +
         sheetBox->currentText() + ", " + scaleBox->currentText() + ", " + QString::number(overlap, 'f', 6) + ", " +
         placementBox->currentData().toString();
}
