#include "EditorSheetsDialog.hpp"

#include "CadCommands.hpp"
#include "alignment/AlignmentViewFrames.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

QString RunCreateSheetsDialog(QWidget* parent, const AppCommandState& state) {
  // REQ-518.4: only alignments with a view frame group can make sheets, and only one group per alignment is usable.
  std::vector<QString> alignments;
  for (const alignment::Alignment& a : state.alignments) {
    const auto groups = std::count_if(state.viewFrameGroups.begin(), state.viewFrameGroups.end(),
                                      [&a](const alignment::ViewFrameGroup& g) { return g.alignmentId == a.id; });
    if (groups == 1) alignments.push_back(QString::fromStdString(a.name));
  }
  if (alignments.empty()) {  // no alignment has exactly one group, which is what the sheets need
    QMessageBox::information(parent, "Create Sheets",
                             "Create one view frame group for an alignment first. Sheets need exactly one per alignment.");
    return {};
  }
  QDialog dialog(parent);
  dialog.setWindowTitle("Create Sheets");
  auto* form = new QFormLayout;

  auto* alignmentBox = new QComboBox(&dialog);
  for (const QString& name : alignments) alignmentBox->addItem(name);
  form->addRow("Alignment:", alignmentBox);

  // ALL, or frame names separated by spaces (for example VF-1 VF-3). The command reports any name it cannot find.
  auto* framesEdit = new QLineEdit(QStringLiteral("ALL"), &dialog);
  framesEdit->setToolTip("ALL for every frame, or frame names separated by spaces, such as VF-1 VF-3.");
  form->addRow("Frames:", framesEdit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return {};
  if (alignmentBox->currentIndex() < 0) return {};

  // Commas separate the command's fields, so a list typed with commas is read as frame names separated by spaces.
  const QString frames = framesEdit->text().replace(',', ' ').simplified();
  return "CREATESHEETS " + alignmentBox->currentText() + (frames.isEmpty() ? QString() : ", " + frames);
}
