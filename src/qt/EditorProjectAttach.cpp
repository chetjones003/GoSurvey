// REQ-379 clause 1 / clause 5 — the Qt side of the Copy / Link / Cancel question.
//
// When a point cloud or PDF from outside the project is attached in a writable project drawing, the
// shared `RequestProjectAttach` queues `projectAttachPrompt`. This file asks the question and hands the
// answer to `CompleteProjectAttach`, which copies or links the file and goes on with the attach that
// asked. The file rules and the follow-up live in commands/ProjectFiles, shared with the Qt application.
#include "EditorWindow.hpp"
#include "QtAccent.hpp"
#include "ProjectFiles.hpp"
#include "pdf/PdfAttach.hpp"
#include "pdf/PdfDocument.hpp"

#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QLineEdit>
#include <QSpinBox>
#include <QTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QApplication>

#include <utility>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
using Prompt = AppCommandState::ProjectAttachPrompt;

/// Page spin box: select all on focus so typing replaces the page number.
class SelectAllSpinBox final : public QSpinBox {
 public:
  using QSpinBox::QSpinBox;
  void focusInEvent(QFocusEvent* event) override {
    QSpinBox::focusInEvent(event);
    if (lineEdit() != nullptr) lineEdit()->selectAll();
  }
};

constexpr int kPdfAttachThumbGridColumns = 5;

static QString PdfAttachDialogStyleSheet(bool dark) {
  return QtApplyAccent(
      "QDialog#pdfAttachDialog QGroupBox {"
      "  font-weight:600; margin-top:8px; padding:8px 8px 6px 8px;"
      "  border:1px solid palette(mid); border-radius:3px; }"
      "QDialog#pdfAttachDialog QGroupBox::title {"
      "  subcontrol-origin:margin; left:8px; padding:0 4px; color:@strong@; }"
      "QDialog#pdfAttachDialog QComboBox,"
      "QDialog#pdfAttachDialog QDoubleSpinBox,"
      "QDialog#pdfAttachDialog QSpinBox {"
      "  min-height:22px; padding:2px 6px; border:1px solid palette(mid); border-radius:2px; }"
      "QDialog#pdfAttachDialog QScrollArea#pdfAttachPageStripScroll {"
      "  background:palette(base); border:1px solid palette(mid); }"
      "QDialog#pdfAttachDialog QLabel#pdfAttachPreview {"
      "  color:@quiet@; padding:24px; }"
      "QDialog#pdfAttachDialog QLabel#pdfAttachSavedPath { color:@quiet@; font-size:11px; }"
      "QDialog#pdfAttachDialog QLabel#pdfAttachStatus { color:palette(windowText); }",
      dark);
}

QString SizeText(std::uint64_t bytes) {
  const double value = static_cast<double>(bytes);
  if (bytes >= 1024ull * 1024 * 1024) return QString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
  if (bytes >= 1024ull * 1024) return QString::number(value / (1024.0 * 1024.0), 'f', 1) + " MB";
  return QString::number(value / 1024.0, 'f', 0) + " KB";
}

QLabel* Text(const QString& text, QWidget* parent, const char* name = nullptr) {
  auto* label = new QLabel(text, parent);
  if (name) label->setObjectName(name);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}

QPushButton* Button(const char* name, const QString& text, QWidget* parent, bool primary) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(name);
  button->setMinimumHeight(34);
  if (primary) {
    const bool dark = parent->palette().color(QPalette::Window).lightness() < 128;
    button->setDefault(true);
    button->setStyleSheet(QString(
        "QPushButton { background:%1; color:white; font-weight:600; padding:6px 18px; "
        "border:0; border-radius:4px; } QPushButton:hover { background:%2; }")
                              .arg(QtAccentFor(dark).fill, QtAccentFor(dark).fillHover));
  }
  return button;
}
}  // namespace

void EditorWindow::ShowProjectAttachPrompt() {
  const Prompt pending = m_State.projectAttachPrompt;
  if (pending.kind == Prompt::Kind::None) return;
  m_ModalOpen = true;
  m_State.projectAttachPrompt.openRequested = false;

  QString projectName = "this project";
  for (const auto& session : m_State.openProjects)
    if (session.uid == pending.projectUid) projectName = QString::fromUtf8(session.project.name.c_str());
  const QString fileName = QFileInfo(QString::fromUtf8(pending.sourcePath.c_str())).fileName();
  const QString destination = QString::fromUtf8(pending.destRel.c_str());
  const bool large = pending.sizeBytes >= projfiles::kLargeFileBytes;
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const QString amber = QtAccentFor(dark).warn;

  QDialog dialog(this);
  dialog.setObjectName("projectAttachDialog");
  dialog.setWindowTitle("Attach to project");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.setMinimumWidth(640);
  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(24, 20, 24, 18);
  layout->setSpacing(10);

  auto* headline = Text(QString("Attach “%1” to project %2").arg(fileName, projectName), &dialog, "projectAttachHeadline");
  QFont headlineFont = headline->font();
  headlineFont.setPointSize(14);
  headlineFont.setBold(true);
  headline->setFont(headlineFont);
  auto* sizeLabel = Text(SizeText(pending.sizeBytes), &dialog, "projectAttachSize");
  auto* copyText = Text(QString("<b>Copy</b> puts the file in the project (%1) so it travels with the project "
                                "when it is packed or emailed.").arg(destination.toHtmlEscaped()), &dialog,
                        "projectAttachCopyText");
  copyText->setTextFormat(Qt::RichText);
  QLabel* reuse = pending.reuse ? Text("An identical copy is already there; it will be used.", &dialog, "projectAttachReuse") : nullptr;
  QLabel* warning = nullptr;
  if (large) {
    warning = Text("This is a large file: copying it makes the project folder, and any pack of it, that much bigger.",
                   &dialog, "projectAttachLarge");
    warning->setStyleSheet(QString("color:%1;").arg(amber));
  }
  auto* linkText = Text("<b>Link</b> leaves the file where it is. A linked file will <b>not</b> travel with the project.",
                        &dialog, "projectAttachLinkText");
  linkText->setTextFormat(Qt::RichText);
  auto* cancel = Button("projectAttachCancel", "Cancel", &dialog, false);
  auto* link = Button("projectAttachLink", "Link", &dialog, false);
  auto* copy = Button("projectAttachCopy", "Copy into project", &dialog, true);

  // Two choice cards side by side: Copy (recommended) and Link (flagged as not travelling).
  const QString border = dark ? "#4b4e50" : "#c5d0da";
  const QString cardFill = dark ? "#2d2f31" : "#ffffff";
  auto card = [&](const char* name, const QString& title, const QString& tag, bool recommended) {
    auto* frame = new QFrame(&dialog);
    frame->setObjectName(name);
    frame->setStyleSheet(QString("QFrame#%1 { background:%2; border:%3; border-radius:6px; }")
                             .arg(name, cardFill, recommended ? QString("2px solid %1").arg(QtAccentFor(dark).strong) : "1px solid " + border));
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(14, 12, 14, 12);
    box->setSpacing(8);
    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet("font-size:14px;font-weight:600;");
    box->addWidget(titleLabel);
    auto* tagLabel = new QLabel(tag, frame);
    tagLabel->setStyleSheet(QString("font-size:11px;font-weight:600;color:%1;").arg(recommended ? QString(QtAccentFor(dark).strong) : amber));
    box->addWidget(tagLabel);
    return std::pair{frame, box};
  };
  auto [copyCard, copyBox] = card("projectAttachCopyCard", "Copy into project", "RECOMMENDED — travels with the project", true);
  copyBox->addWidget(copyText);
  if (reuse) copyBox->addWidget(reuse);
  if (warning) copyBox->addWidget(warning);
  copyBox->addStretch();
  copyBox->addWidget(copy);
  auto [linkCard, linkBox] = card("projectAttachLinkCard", "Link", "WILL NOT TRAVEL with the project", false);
  linkBox->addWidget(linkText);
  linkBox->addStretch();
  linkBox->addWidget(link);

  auto* cards = new QHBoxLayout;
  cards->setSpacing(12);
  cards->addWidget(copyCard);
  cards->addWidget(linkCard);
  auto* footer = new QHBoxLayout;
  footer->addStretch();
  footer->addWidget(cancel);
  layout->addWidget(headline);
  layout->addWidget(sizeLabel);
  layout->addLayout(cards);
  layout->addLayout(footer);

  AttachAnswer answer = AttachAnswer::Cancel;  // Esc and the window close button are Cancel
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(link, &QPushButton::clicked, &dialog, [&] { answer = AttachAnswer::Link; dialog.accept(); });
  connect(copy, &QPushButton::clicked, &dialog, [&] { answer = AttachAnswer::Copy; dialog.accept(); });
  dialog.exec();

  // A modal wait can outlive the prompt (the project closed, another attach replaced it): only answer
  // the question that was shown.
  const Prompt& current = m_State.projectAttachPrompt;
  if (current.kind == pending.kind && current.projectUid == pending.projectUid &&
      current.sourcePath == pending.sourcePath) {
    std::string pdfPath;
    const bool wentAhead = CompleteProjectAttach(m_State, answer, m_Log, &pdfPath);
    if (!wentAhead && answer != AttachAnswer::Cancel && !m_Log.empty())
      QMessageBox::warning(this, "Attach to project", QString::fromUtf8(m_Log.back().c_str()));
    if (wentAhead && !pdfPath.empty()) {
      std::snprintf(m_State.pdfAttachFilePath, sizeof(m_State.pdfAttachFilePath), "%s", pdfPath.c_str());
      m_State.pdfAttachSelectedPage = 0;
      m_State.pdfAttachDialogOpen = true;
    } else if (answer == AttachAnswer::Cancel && m_State.active == AppCommandState::Kind::PdfAttach) {
      CancelPdfAttachCommand(m_State, m_Log);
    }
  }
  m_ModalOpen = false;
  RefreshProjects();
  Refresh();
}

void EditorWindow::AddPdfToProjectFromDisk() {
  const int tab = m_State.activeDrawingIdx;
  const bool inProject = tab > 0 && tab < static_cast<int>(m_State.drawingTabs.size()) &&
                         m_State.drawingTabs[static_cast<size_t>(tab)].projectUid != 0;
  if (!inProject) {
    QMessageBox::information(this, "Add PDF to project", "Open a drawing that belongs to a project first.");
    return;
  }
  const QString path = QFileDialog::getOpenFileName(this, "Add PDF to project", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  const std::string utf8 = path.toUtf8().toStdString();
  const std::size_t logged = m_Log.size();
  // Raises the Copy / Link / Cancel question for a PDF outside the project; the next UI tick asks it.
  const bool accepted = AddPdfToProject(m_State, utf8, m_Log);
  if (!accepted && m_Log.size() > logged)
    QMessageBox::warning(this, "Add PDF to project", QString::fromUtf8(m_Log.back().c_str()));
  else if (accepted && m_State.projectAttachPrompt.kind == Prompt::Kind::None && m_Log.size() > logged)
    QMessageBox::information(this, "Add PDF to project", QString::fromUtf8(m_Log.back().c_str()));
}

void EditorWindow::ShowPdfAttachDialog() {
  if (!m_State.pdfAttachDialogOpen || m_ModalOpen) return;
  m_State.pdfAttachDialogOpen = false;
  if (ActiveDrawingReadOnly()) {
    QMessageBox::information(this, "PDF Attach", "This drawing is read-only. No PDF underlay was attached.");
    CancelPdfAttachCommand(m_State, m_Log);
    Refresh();
    return;
  }

  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("pdfAttachDialog");
  dialog.setWindowTitle("Attach PDF Underlay");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.resize(920, 640);
  const bool darkDialog = dialog.palette().color(QPalette::Window).lightness() < 128;
  dialog.setStyleSheet(PdfAttachDialogStyleSheet(darkDialog));

  auto* pathEdit = new QLineEdit(QString::fromUtf8(m_State.pdfAttachFilePath), &dialog);
  pathEdit->setObjectName("pdfAttachPath");
  pathEdit->hide();

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(12, 12, 12, 10);
  root->setSpacing(8);

  auto* mainRow = new QHBoxLayout;
  mainRow->setSpacing(12);
  root->addLayout(mainRow, 1);

  auto* leftColumn = new QVBoxLayout;
  leftColumn->setSpacing(8);
  mainRow->addLayout(leftColumn, 1);

  auto* browse = new QPushButton("Browse...", &dialog);
  browse->setObjectName("pdfAttachBrowse");
  auto* name = new QComboBox(&dialog);
  name->setObjectName("pdfAttachName");
  name->setEditable(true);
  name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* nameLabel = new QLabel("Name:", &dialog);
  nameLabel->setFixedWidth(44);
  auto* nameRow = new QHBoxLayout;
  nameRow->setSpacing(8);
  nameRow->addWidget(nameLabel);
  nameRow->addWidget(name, 1);
  nameRow->addWidget(browse);
  leftColumn->addLayout(nameRow);

  auto* thumbGridScroll = new QScrollArea(&dialog);
  thumbGridScroll->setObjectName("pdfAttachPageStripScroll");
  thumbGridScroll->setWidgetResizable(true);
  thumbGridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  thumbGridScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  thumbGridScroll->setMinimumHeight(360);
  thumbGridScroll->setFrameShape(QFrame::StyledPanel);
  auto* gridHost = new QWidget(thumbGridScroll);
  gridHost->setObjectName("pdfAttachPageStrip");
  auto* gridLayout = new QGridLayout(gridHost);
  gridLayout->setContentsMargins(8, 8, 8, 8);
  gridLayout->setHorizontalSpacing(8);
  gridLayout->setVerticalSpacing(8);
  auto* gridPlaceholder = new QLabel("Choose a PDF to preview pages.", gridHost);
  gridPlaceholder->setObjectName("pdfAttachPreview");
  gridPlaceholder->setAlignment(Qt::AlignCenter);
  gridLayout->addWidget(gridPlaceholder, 0, 0, 1, kPdfAttachThumbGridColumns);
  thumbGridScroll->setWidget(gridHost);
  leftColumn->addWidget(thumbGridScroll, 1);

  auto* savedPath = new QLabel(&dialog);
  savedPath->setObjectName("pdfAttachSavedPath");
  savedPath->setWordWrap(true);
  savedPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
  leftColumn->addWidget(savedPath);

  auto* settingsHost = new QWidget(&dialog);
  settingsHost->setObjectName("pdfAttachSettings");
  settingsHost->setFixedWidth(268);
  auto* settingsLayout = new QVBoxLayout(settingsHost);
  settingsLayout->setContentsMargins(0, 0, 0, 0);
  settingsLayout->setSpacing(8);
  mainRow->addWidget(settingsHost);

  auto* pageButtons = new QButtonGroup(&dialog);
  pageButtons->setExclusive(true);
  auto number = [&](const char* name, double value, int decimals, double low, double high, double step) {
    auto* field = new QDoubleSpinBox(&dialog);
    field->setObjectName(name);
    field->setDecimals(decimals);
    field->setRange(low, high);
    field->setSingleStep(step);
    field->setValue(value);
    return field;
  };
  auto* page = new SelectAllSpinBox(&dialog);
  page->setObjectName("pdfAttachPage");
  page->setRange(1, 1);
  page->setValue(std::max(1, m_State.pdfAttachSelectedPage + 1));
  page->hide();

  auto* pathType = new QComboBox(&dialog);
  pathType->setObjectName("pdfAttachPathType");
  pathType->addItem(QStringLiteral("Relative path"));
  pathType->addItem(QStringLiteral("Full path"));
  pathType->addItem(QStringLiteral("No path"));
  auto* pathGroup = new QGroupBox("Path type", settingsHost);
  auto* pathForm = new QFormLayout(pathGroup);
  pathForm->addRow(pathType);
  settingsLayout->addWidget(pathGroup);

  auto* specifyInsert = new QCheckBox("Specify on-screen", &dialog);
  specifyInsert->setObjectName("pdfAttachSpecifyInsert");
  specifyInsert->setChecked(m_State.pdfAttachSpecifyInsert);
  auto* x = number("pdfAttachX", m_State.pdfAttachInsertX, 4, -1.e9, 1.e9, 1.0);
  auto* y = number("pdfAttachY", m_State.pdfAttachInsertY, 4, -1.e9, 1.e9, 1.0);
  auto* z = number("pdfAttachZ", 0.0, 4, -1.e9, 1.e9, 1.0);
  z->setEnabled(false);
  auto* insertGroup = new QGroupBox("Insertion point", settingsHost);
  auto* insertForm = new QFormLayout(insertGroup);
  insertForm->addRow(specifyInsert);
  insertForm->addRow("X", x);
  insertForm->addRow("Y", y);
  insertForm->addRow("Z", z);
  settingsLayout->addWidget(insertGroup);

  auto* specifyScale = new QCheckBox("Specify on-screen", &dialog);
  specifyScale->setObjectName("pdfAttachSpecifyScale");
  specifyScale->setChecked(m_State.pdfAttachSpecifyScale);
  auto* scale = number("pdfAttachScale", m_State.pdfAttachScale, 6, 1.e-6, 1.e6, .1);
  auto* scaleGroup = new QGroupBox("Scale", settingsHost);
  auto* scaleForm = new QFormLayout(scaleGroup);
  scaleForm->addRow(specifyScale);
  scaleForm->addRow("Scale factor", scale);
  settingsLayout->addWidget(scaleGroup);

  auto* specifyRot = new QCheckBox("Specify on-screen", &dialog);
  specifyRot->setObjectName("pdfAttachSpecifyRot");
  specifyRot->setChecked(m_State.pdfAttachSpecifyRot);
  auto* rotation = number("pdfAttachRotation", m_State.pdfAttachRotDeg, 2, -360000, 360000, 5);
  auto* rotGroup = new QGroupBox("Rotation", settingsHost);
  auto* rotForm = new QFormLayout(rotGroup);
  rotForm->addRow(specifyRot);
  rotForm->addRow("Angle", rotation);
  settingsLayout->addWidget(rotGroup);
  settingsLayout->addStretch();

  auto* details = new QWidget(&dialog);
  details->setObjectName("pdfAttachDetails");
  details->setVisible(false);
  auto* detailsLayout = new QVBoxLayout(details);
  detailsLayout->setContentsMargins(0, 0, 0, 0);
  auto* dpi = number("pdfAttachDpi", m_State.pdfAttachRasterDpi, 0, 72, 300, 10);
  auto* snapLines = new QCheckBox("Lines", &dialog);
  snapLines->setObjectName("pdfAttachSnapLines");
  snapLines->setChecked(m_State.pdfAttachSnapLines);
  auto* snapCircles = new QCheckBox("Circles", &dialog);
  snapCircles->setObjectName("pdfAttachSnapCircles");
  snapCircles->setChecked(m_State.pdfAttachSnapCircles);
  auto* snapText = new QCheckBox("Text", &dialog);
  snapText->setObjectName("pdfAttachSnapText");
  snapText->setChecked(m_State.pdfAttachSnapText);
  auto* detailsForm = new QFormLayout;
  detailsForm->addRow("Raster DPI", dpi);
  auto* snapRow = new QHBoxLayout;
  snapRow->addWidget(new QLabel("Recognize snaps:", &dialog));
  snapRow->addWidget(snapLines);
  snapRow->addWidget(snapCircles);
  snapRow->addWidget(snapText);
  snapRow->addStretch();
  detailsLayout->addLayout(detailsForm);
  detailsLayout->addLayout(snapRow);
  root->addWidget(details);

  auto syncFieldEnable = [&] {
    x->setEnabled(!specifyInsert->isChecked());
    y->setEnabled(!specifyInsert->isChecked());
    scale->setEnabled(!specifyScale->isChecked());
    rotation->setEnabled(!specifyRot->isChecked());
  };
  connect(specifyInsert, &QCheckBox::toggled, &dialog, syncFieldEnable);
  connect(specifyScale, &QCheckBox::toggled, &dialog, syncFieldEnable);
  connect(specifyRot, &QCheckBox::toggled, &dialog, syncFieldEnable);
  syncFieldEnable();

  auto* status = new QLabel(&dialog);
  status->setObjectName("pdfAttachStatus");
  status->setWordWrap(true);
  auto* pagePrevious = new QToolButton(&dialog);
  pagePrevious->setObjectName("pdfAttachPagePrevious");
  pagePrevious->setText("◀");
  pagePrevious->setToolTip("Previous page");
  pagePrevious->setAutoRaise(true);
  pagePrevious->setFixedSize(34, 28);
  auto* pageNext = new QToolButton(&dialog);
  pageNext->setObjectName("pdfAttachPageNext");
  pageNext->setText("▶");
  pageNext->setToolTip("Next page");
  pageNext->setAutoRaise(true);
  pageNext->setFixedSize(34, 28);
  connect(pagePrevious, &QToolButton::clicked, &dialog, [&] {
    if (page->value() > page->minimum()) page->setValue(page->value() - 1);
  });
  connect(pageNext, &QToolButton::clicked, &dialog, [&] {
    if (page->value() < page->maximum()) page->setValue(page->value() + 1);
  });
  auto* loadProgress = new QProgressBar(&dialog);
  loadProgress->setObjectName("pdfAttachLoadProgress");
  loadProgress->setTextVisible(true);
  loadProgress->setFormat("Loading page previews %p% (%v / %m)");
  loadProgress->hide();
  leftColumn->addWidget(loadProgress);
  auto* showDetails = new QPushButton("Show Details <<", &dialog);
  showDetails->setObjectName("pdfAttachShowDetails");
  showDetails->setFlat(true);
  auto* bottomLeft = new QHBoxLayout;
  bottomLeft->addWidget(showDetails);
  bottomLeft->addWidget(status, 1);
  bottomLeft->addWidget(pagePrevious);
  bottomLeft->addWidget(pageNext);
  leftColumn->addLayout(bottomLeft);

  auto* footer = new QHBoxLayout;
  footer->addStretch();
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* attach = buttons->addButton("OK", QDialogButtonBox::AcceptRole);
  attach->setObjectName("pdfAttachConfirm");
  attach->setDefault(true);
  attach->setStyleSheet(QtApplyAccent("QPushButton { background:@fill@; color:white; font-weight:600; padding:6px 18px; border:0; border-radius:4px; }"
                                      "QPushButton:hover { background:@fillHover@; }",
                                      dialog.palette().color(QPalette::Window).lightness() < 128));
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName("pdfAttachCancel");
  auto* help = buttons->addButton("Help", QDialogButtonBox::HelpRole);
  help->setObjectName("pdfAttachHelp");
  footer->addWidget(buttons);
  root->addLayout(footer);

  connect(showDetails, &QPushButton::clicked, &dialog, [details, showDetails] {
    const bool open = !details->isVisible();
    details->setVisible(open);
    showDetails->setText(open ? "Hide Details >>" : "Show Details <<");
  });
  connect(help, &QPushButton::clicked, &dialog, [] {
    QDesktopServices::openUrl(QUrl("https://github.com/chetjones003/GoSurvey/wiki/PDF-Underlays"));
  });

  std::unique_ptr<pdfview::PdfDocument> document;
  const QString thumbFrame = darkDialog ? QStringLiteral("#e6a64b") : QStringLiteral("#c47a12");
  const QString thumbIdle = darkDialog ? QStringLiteral("#3c464f") : QStringLiteral("#d8dee3");
  auto renderThumb = [&](int pageIndex, int maxW, int maxH) -> QPixmap {
    pdfview::Bitmap bitmap;
    if (!document || pageIndex < 0 || pageIndex >= document->PageCount()) return {};
    if (!document->RenderPage(pageIndex, maxW, maxH, bitmap, [] { return false; })) return {};
    QImage image(bitmap.bgra.data(), bitmap.w, bitmap.h, static_cast<qsizetype>(bitmap.w) * 4,
                 QImage::Format_ARGB32);
    return QPixmap::fromImage(image.copy());
  };
  auto syncSelectedThumb = [&](int pageIndex) {
    for (QAbstractButton* button : pageButtons->buttons()) {
      const bool selected = button->property("pageIndex").toInt() == pageIndex;
      button->setChecked(selected);
      button->setStyleSheet(selected
          ? QString("QToolButton { border:2px solid %1; border-radius:4px; padding:2px; background:%2; }")
                .arg(thumbFrame, thumbIdle)
          : QString("QToolButton { border:1px solid %1; border-radius:4px; padding:3px; background:transparent; }")
                .arg(thumbIdle));
    }
    if (QAbstractButton* selected = pageButtons->checkedButton())
      thumbGridScroll->ensureWidgetVisible(selected);
  };
  auto clearThumbGrid = [&] {
    while (QLayoutItem* item = gridLayout->takeAt(0)) {
      if (QWidget* widget = item->widget()) {
        if (widget != gridPlaceholder)
          widget->deleteLater();
      }
      delete item;
    }
    for (QAbstractButton* old : pageButtons->buttons()) pageButtons->removeButton(old);
    gridPlaceholder->hide();
  };
  auto rebuildStripPlaceholders = [&] {
    clearThumbGrid();
    if (!document || document->PageCount() <= 0) {
      gridPlaceholder->setText("Choose a PDF to preview pages.");
      gridPlaceholder->show();
      gridLayout->addWidget(gridPlaceholder, 0, 0, 1, kPdfAttachThumbGridColumns);
      return;
    }
    gridPlaceholder->hide();
    const int pageCount = document->PageCount();
    for (int i = 0; i < pageCount; ++i) {
      auto* button = new QToolButton(gridHost);
      button->setObjectName(QString("pdfAttachPageThumb%1").arg(i));
      button->setProperty("pageIndex", i);
      button->setCheckable(true);
      button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      button->setIconSize(QSize(92, 108));
      button->setFixedSize(100, 124);
      button->setText(QString::number(i + 1));
      pageButtons->addButton(button);
      const int row = i / kPdfAttachThumbGridColumns;
      const int col = i % kPdfAttachThumbGridColumns;
      gridLayout->addWidget(button, row, col);
      connect(button, &QToolButton::clicked, &dialog, [&, i] {
        page->setValue(i + 1);
        syncSelectedThumb(i);
      });
    }
  };
  QTimer thumbLoadTimer(&dialog);
  thumbLoadTimer.setSingleShot(true);
  int thumbLoadNext = 0;
  auto applyStripThumb = [&](int pageIndex) {
    if (!document || pageIndex < 0 || pageIndex >= document->PageCount()) return;
    auto* button = gridHost->findChild<QToolButton*>(QString("pdfAttachPageThumb%1").arg(pageIndex));
    if (button == nullptr) return;
    const QPixmap thumb = renderThumb(pageIndex, 236, 264);
    if (thumb.isNull()) return;
    button->setIcon(QIcon(thumb.scaled(button->iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  };
  auto pumpStripThumbs = [&] {
    if (!document) {
      loadProgress->hide();
      return;
    }
    const int total = document->PageCount();
    while (thumbLoadNext < total) {
      applyStripThumb(thumbLoadNext);
      thumbLoadNext++;
      loadProgress->setValue(thumbLoadNext);
      if (thumbLoadNext % 2 == 0 || thumbLoadNext >= total) {
        thumbLoadTimer.start(0);
        return;
      }
    }
    loadProgress->hide();
    status->setText(QString("%1 page%2").arg(total).arg(total == 1 ? "" : "s"));
    syncSelectedThumb(page->value() - 1);
  };
  connect(&thumbLoadTimer, &QTimer::timeout, &dialog, pumpStripThumbs);
  auto startStripThumbLoad = [&] {
    thumbLoadTimer.stop();
    thumbLoadNext = 0;
    rebuildStripPlaceholders();
    if (!document || document->PageCount() <= 0) {
      loadProgress->hide();
      return;
    }
    syncSelectedThumb(page->value() - 1);
    const int total = document->PageCount();
    loadProgress->setRange(0, total);
    loadProgress->setValue(0);
    loadProgress->show();
    status->setText(QString("Loading previews for %1 page%2…")
                        .arg(total)
                        .arg(total == 1 ? "" : "s"));
    applyStripThumb(page->value() - 1);
    thumbLoadTimer.start(0);
  };
  auto showPage = [&] {
    if (!document) return;
    syncSelectedThumb(page->value() - 1);
  };
  auto load = [&] {
    thumbLoadTimer.stop();
    loadProgress->hide();
    document.reset();
    clearThumbGrid();
    gridPlaceholder->setText("Choose a PDF to preview pages.");
    gridPlaceholder->show();
    gridLayout->addWidget(gridPlaceholder, 0, 0, 1, kPdfAttachThumbGridColumns);
    const QString file = pathEdit->text().trimmed();
    if (file.isEmpty()) {
      page->setRange(1, 1);
      name->setCurrentText({});
      savedPath->clear();
      status->clear();
      return;
    }
    const QFileInfo info(file);
    name->setCurrentText(info.completeBaseName());
    savedPath->setText(QString("Saved path: %1").arg(info.absoluteFilePath()));
    auto opened = pdfview::PdfDocument::Open(std::filesystem::path(file.toStdWString()));
    if (!opened.doc) {
      page->setRange(1, 1);
      status->setText(QString("Unable to open PDF: %1").arg(QString::fromUtf8(opened.error.c_str())));
      status->setStyleSheet("color:#c74747;");
      gridPlaceholder->setText("No page preview is available.");
      gridPlaceholder->show();
      gridLayout->addWidget(gridPlaceholder, 0, 0, 1, kPdfAttachThumbGridColumns);
      return;
    }
    document = std::move(opened.doc);
    page->setRange(1, std::max(1, document->PageCount()));
    page->setValue(std::clamp(m_State.pdfAttachSelectedPage + 1, 1, page->maximum()));
    status->setText(QString("%1 page%2").arg(document->PageCount()).arg(document->PageCount() == 1 ? "" : "s"));
    status->setStyleSheet({});
    syncSelectedThumb(page->value() - 1);
    startStripThumbLoad();
  };
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const QString selected = QFileDialog::getOpenFileName(&dialog, "Select PDF", pathEdit->text(), "PDF files (*.pdf)");
    if (!selected.isEmpty()) { pathEdit->setText(selected); load(); }
  });
  connect(pathEdit, &QLineEdit::editingFinished, &dialog, load);
  connect(page, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&, showPage] { showPage(); });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(attach, &QPushButton::clicked, &dialog, [&] {
    if (!document) load();
    if (!document) {
      if (pathEdit->text().trimmed().isEmpty()) {
        status->setText("Choose a PDF file before attaching.");
        status->setStyleSheet("color:#c74747;");
      }
      return;
    }
    const QByteArray utf8 = pathEdit->text().trimmed().toUtf8();
    if (page->value() < 1 || page->value() > document->PageCount() || scale->value() <= 0) {
      status->setText("Choose a readable PDF page and a positive scale before attaching.");
      status->setStyleSheet("color:#c74747;");
      return;
    }
    std::snprintf(m_State.pdfAttachFilePath, sizeof(m_State.pdfAttachFilePath), "%s", utf8.constData());
    m_State.pdfAttachSelectedPage = page->value() - 1;
    m_State.pdfAttachInsertX = static_cast<float>(x->value());
    m_State.pdfAttachInsertY = static_cast<float>(y->value());
    m_State.pdfAttachScale = static_cast<float>(scale->value());
    m_State.pdfAttachRotDeg = static_cast<float>(rotation->value());
    m_State.pdfAttachRasterDpi = static_cast<float>(dpi->value());
    m_State.pdfAttachSpecifyInsert = specifyInsert->isChecked();
    m_State.pdfAttachSpecifyScale = specifyScale->isChecked();
    m_State.pdfAttachSpecifyRot = specifyRot->isChecked();
    m_State.pdfAttachSnapLines = snapLines->isChecked();
    m_State.pdfAttachSnapCircles = snapCircles->isChecked();
    m_State.pdfAttachSnapText = snapText->isChecked();
    const std::size_t logSize = m_Log.size();
    if (RequestProjectAttach(m_State, AppCommandState::ProjectAttachPrompt::Kind::Pdf, m_State.pdfAttachFilePath, m_Log)) {
      dialog.accept();
      return;
    }
    if (m_Log.size() > logSize) {
      status->setText(QString::fromUtf8(m_Log.back().c_str()));
      status->setStyleSheet("color:#c74747;");
      return;
    }
    StartPdfAttachBuild(m_State, m_Log);
    status->setText("Rasterizing selected page...");
    attach->setEnabled(false);
    QEventLoop wait;
    QTimer poll;
    poll.setInterval(30);
    connect(&poll, &QTimer::timeout, &wait, [&] {
      if (!m_State.pdfAttachAsync || m_State.pdfAttachAsync->done.load(std::memory_order_acquire)) wait.quit();
    });
    connect(cancel, &QPushButton::clicked, &wait, &QEventLoop::quit);
    poll.start();
    wait.exec();
    poll.stop();
    if (!m_State.pdfAttachAsync) { attach->setEnabled(true); return; }
    if (!m_State.pdfAttachAsync->done.load(std::memory_order_acquire)) {
      CancelPdfAttachCommand(m_State, m_Log);
      dialog.reject();
      return;
    }
    PdfAttachment attachment;
    const bool built = PdfAttach_FinishBuild(m_State.pdfAttachAsync->result, m_State.pdfAttachFilePath,
        m_State.pdfAttachSelectedPage, attachment);
    if (m_State.pdfAttachAsync->thread.joinable()) m_State.pdfAttachAsync->thread.join();
    m_State.pdfAttachAsync.reset();
    if (!built) {
      status->setText("The selected page could not be rasterized. Check the file and try again.");
      status->setStyleSheet("color:#c74747;");
      m_State.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
      attach->setEnabled(true);
      return;
    }
    if (m_State.pdfAttachSpecifyInsert) {
      m_State.pdfAttachPreview = std::move(attachment);
      m_State.pdfAttachPreviewReady = true;
      m_State.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitInsertPoint;
      dialog.accept();
      Refresh();
      return;
    }
    m_State.pdfAttachPreview = std::move(attachment);
    m_State.pdfAttachPreviewReady = true;
    m_State.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitInsertPoint;
    SubmitPdfAttachInsertPoint(m_State, m_State.pdfAttachInsertX, m_State.pdfAttachInsertY, m_Log);
    dialog.accept();
    Refresh();
  });
  connect(&dialog, &QDialog::rejected, this, [this] {
    if (m_State.active == AppCommandState::Kind::PdfAttach && m_State.projectAttachPrompt.kind == Prompt::Kind::None)
      CancelPdfAttachCommand(m_State, m_Log);
  });
  if (!pathEdit->text().isEmpty()) load();
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}
