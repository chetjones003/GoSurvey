// REQ-379 clause 1 / clause 5 — the Qt side of the Copy / Link / Cancel question.
//
// When a point cloud or PDF from outside the project is attached in a writable project drawing, the
// shared `RequestProjectAttach` queues `projectAttachPrompt`. This file asks the question and hands the
// answer to `CompleteProjectAttach`, which copies or links the file and goes on with the attach that
// asked. The file rules and the follow-up live in commands/ProjectFiles, shared with the ImGui build.
#include "EditorWindow.hpp"
#include "ProjectFiles.hpp"
#include "PdfAttachDialog.hpp"
#include "pdf/PdfAttach.hpp"
#include "pdf/PdfDocument.hpp"

#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFormLayout>
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
#include <QVBoxLayout>

#include <utility>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
using Prompt = AppCommandState::ProjectAttachPrompt;

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
                              .arg(dark ? "#168e8a" : "#438fda", dark ? "#1ca6a0" : "#347cc3"));
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
  const QString amber = dark ? "#f0b25a" : "#9a5b00";

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
                             .arg(name, cardFill, recommended ? QString("2px solid %1").arg(dark ? "#55d8d0" : "#438fda") : "1px solid " + border));
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(14, 12, 14, 12);
    box->setSpacing(8);
    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet("font-size:14px;font-weight:600;");
    box->addWidget(titleLabel);
    auto* tagLabel = new QLabel(tag, frame);
    tagLabel->setStyleSheet(QString("font-size:11px;font-weight:600;color:%1;").arg(recommended ? (dark ? "#55d8d0" : "#438fda") : amber));
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
  dialog.setWindowTitle("PDF Attach");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.resize(690, 620);
  auto* root = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout;
  root->addLayout(form);
  auto* path = new QLineEdit(QString::fromUtf8(m_State.pdfAttachFilePath), &dialog);
  path->setObjectName("pdfAttachPath");
  auto* browse = new QPushButton("Browse...", &dialog);
  browse->setObjectName("pdfAttachBrowse");
  auto* pathRow = new QWidget(&dialog);
  auto* pathLayout = new QHBoxLayout(pathRow);
  pathLayout->setContentsMargins(0, 0, 0, 0);
  pathLayout->addWidget(path, 1);
  pathLayout->addWidget(browse);
  form->addRow("PDF file", pathRow);
  auto* page = new QSpinBox(&dialog);
  page->setObjectName("pdfAttachPage");
  page->setRange(1, 1);
  page->setValue(std::max(1, m_State.pdfAttachSelectedPage + 1));
  form->addRow("Page", page);
  auto* preview = new QLabel("Choose a PDF to preview its selected page.", &dialog);
  preview->setObjectName("pdfAttachPreview");
  preview->setAlignment(Qt::AlignCenter);
  preview->setMinimumHeight(220);
  preview->setFrameShape(QFrame::StyledPanel);
  root->addWidget(preview, 1);
  auto number = [&](const char* name, double value, int decimals, double low, double high, double step) {
    auto* field = new QDoubleSpinBox(&dialog);
    field->setObjectName(name);
    field->setDecimals(decimals);
    field->setRange(low, high);
    field->setSingleStep(step);
    field->setValue(value);
    return field;
  };
  auto* x = number("pdfAttachX", m_State.pdfAttachInsertX, 4, -1.e9, 1.e9, 1.0);
  auto* y = number("pdfAttachY", m_State.pdfAttachInsertY, 4, -1.e9, 1.e9, 1.0);
  auto* scale = number("pdfAttachScale", m_State.pdfAttachScale, 6, 1.e-6, 1.e6, .1);
  auto* rotation = number("pdfAttachRotation", m_State.pdfAttachRotDeg, 2, -360000, 360000, 5);
  auto* dpi = number("pdfAttachDpi", m_State.pdfAttachRasterDpi, 0, 72, 300, 10);
  form->addRow("Insertion X", x);
  form->addRow("Insertion Y", y);
  form->addRow("Scale", scale);
  form->addRow("Rotation (degrees)", rotation);
  form->addRow("Raster DPI", dpi);
  auto* specifyInsert = new QCheckBox("Pick insertion point in drawing", &dialog);
  specifyInsert->setObjectName("pdfAttachSpecifyInsert");
  specifyInsert->setChecked(m_State.pdfAttachSpecifyInsert);
  root->addWidget(specifyInsert);
  auto* snapLines = new QCheckBox("Lines", &dialog);
  snapLines->setObjectName("pdfAttachSnapLines");
  snapLines->setChecked(m_State.pdfAttachSnapLines);
  auto* snapCircles = new QCheckBox("Circles", &dialog);
  snapCircles->setObjectName("pdfAttachSnapCircles");
  snapCircles->setChecked(m_State.pdfAttachSnapCircles);
  auto* snapText = new QCheckBox("Text", &dialog);
  snapText->setObjectName("pdfAttachSnapText");
  snapText->setChecked(m_State.pdfAttachSnapText);
  auto* snapRow = new QHBoxLayout;
  snapRow->addWidget(new QLabel("Recognize snaps:", &dialog));
  snapRow->addWidget(snapLines);
  snapRow->addWidget(snapCircles);
  snapRow->addWidget(snapText);
  snapRow->addStretch();
  root->addLayout(snapRow);
  auto* status = new QLabel(&dialog);
  status->setObjectName("pdfAttachStatus");
  status->setWordWrap(true);
  root->addWidget(status);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* attach = buttons->addButton("Attach", QDialogButtonBox::AcceptRole);
  attach->setObjectName("pdfAttachConfirm");
  attach->setDefault(true);
  attach->setStyleSheet("QPushButton { background:#168e8a; color:white; font-weight:600; padding:6px 18px; border:0; border-radius:4px; }"
                        "QPushButton:hover { background:#1ca6a0; }");
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName("pdfAttachCancel");
  root->addWidget(buttons);

  std::unique_ptr<pdfview::PdfDocument> document;
  auto showPage = [&] {
    if (!document) return;
    pdfview::Bitmap bitmap;
    if (!document->RenderPage(page->value() - 1, 340, 280, bitmap, [] { return false; })) {
      preview->setText("The selected page could not be previewed.");
      return;
    }
    QImage image(bitmap.bgra.data(), bitmap.w, bitmap.h, bitmap.w * 4, QImage::Format_ARGB32);
    preview->setPixmap(QPixmap::fromImage(image.copy()).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  };
  auto load = [&] {
    document.reset();
    preview->clear();
    const QString file = path->text().trimmed();
    if (file.isEmpty()) {
      page->setRange(1, 1);
      preview->setText("Choose a PDF to preview its selected page.");
      status->clear();
      return;
    }
    auto opened = pdfview::PdfDocument::Open(std::filesystem::path(file.toStdWString()));
    if (!opened.doc) {
      page->setRange(1, 1);
      status->setText(QString("Unable to open PDF: %1").arg(QString::fromUtf8(opened.error.c_str())));
      status->setStyleSheet("color:#c74747;");
      preview->setText("No page preview is available.");
      return;
    }
    document = std::move(opened.doc);
    page->setRange(1, std::max(1, document->PageCount()));
    page->setValue(std::clamp(m_State.pdfAttachSelectedPage + 1, 1, page->maximum()));
    status->setText(QString("%1 page%2").arg(document->PageCount()).arg(document->PageCount() == 1 ? "" : "s"));
    status->setStyleSheet({});
    showPage();
  };
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const QString selected = QFileDialog::getOpenFileName(&dialog, "Select PDF", path->text(), "PDF files (*.pdf)");
    if (!selected.isEmpty()) { path->setText(selected); load(); }
  });
  connect(path, &QLineEdit::editingFinished, &dialog, load);
  connect(page, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&, showPage] { showPage(); });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(attach, &QPushButton::clicked, &dialog, [&] {
    if (!document) load();
    if (!document) {
      if (path->text().trimmed().isEmpty()) {
        status->setText("Choose a PDF file before attaching.");
        status->setStyleSheet("color:#c74747;");
      }
      return;
    }
    const QByteArray utf8 = path->text().trimmed().toUtf8();
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
  if (!path->text().isEmpty()) load();
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}
