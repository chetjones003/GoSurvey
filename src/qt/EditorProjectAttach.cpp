// REQ-379 clause 1 / clause 5 — the Qt side of the Copy / Link / Cancel question.
//
// When a point cloud or PDF from outside the project is attached in a writable project drawing, the
// shared `RequestProjectAttach` queues `projectAttachPrompt`. This file asks the question and hands the
// answer to `CompleteProjectAttach`, which copies or links the file and goes on with the attach that
// asked. The file rules and the follow-up live in commands/ProjectFiles, shared with the ImGui build.
#include "EditorWindow.hpp"
#include "ProjectFiles.hpp"

#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

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
    button->setDefault(true);
    button->setStyleSheet(
        "QPushButton { background:#438fda; color:white; font-weight:600; padding:6px 18px; "
        "border:0; border-radius:4px; } QPushButton:hover { background:#347cc3; }");
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
                             .arg(name, cardFill, recommended ? "2px solid #438fda" : "1px solid " + border));
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(14, 12, 14, 12);
    box->setSpacing(8);
    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet("font-size:14px;font-weight:600;");
    box->addWidget(titleLabel);
    auto* tagLabel = new QLabel(tag, frame);
    tagLabel->setStyleSheet(QString("font-size:11px;font-weight:600;color:%1;").arg(recommended ? "#438fda" : amber));
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
      // The Qt PDF placement dialog is tracked in #909; until it exists there is nothing to place with.
      m_Log.push_back("PDFATTACH - placing a PDF underlay is not available in the Qt build yet; nothing was attached.");
      QMessageBox::information(this, "Attach to project", QString::fromUtf8(m_Log.back().c_str()));
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
