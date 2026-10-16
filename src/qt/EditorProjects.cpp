#include "EditorWindow.hpp"
#include "ProjectSettings.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <algorithm>
#include <chrono>

namespace {
double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

EditorWindow::~EditorWindow() {
  if (qApp) qApp->removeEventFilter(this);
  for (auto& session : m_State.openProjects) {
    FlushProjectPointDb(session, m_Log);
    if (!session.readOnly) gsproj::Release(session.project.file, session.me);
  }
}

std::uint32_t EditorWindow::OpenProject(const QString& path, bool createDrawing) {
  const std::filesystem::path file(path.toStdWString());
  for (const auto& session : m_State.openProjects) {
    std::error_code ec;
    if (!std::filesystem::equivalent(session.project.file, file, ec)) continue;
    if (createDrawing) {
      for (int i = 1; i < static_cast<int>(m_State.drawingTabs.size()); ++i)
        if (m_State.drawingTabs[i].projectUid == session.uid) { SwitchDrawing(i); break; }
    }
    return session.uid;
  }
  AppCommandState::ProjectSession session;
  std::string error;
  if (!gsproj::Load(file, &session.project, &error)) {
    QMessageBox::warning(this, "Open project", QString::fromUtf8(error.c_str()));
    return 0;
  }
  session.settings = std::make_shared<ProjectSettings>();
  if (!ParseProjectSettings(session.project.settingsJson, session.settings.get(), &error)) {
    QMessageBox::warning(this, "Project settings", QString::fromUtf8(error.c_str()));
    return 0;
  }
  session.me.user = qEnvironmentVariable("USERNAME").toUtf8().toStdString();
  session.me.machine = qEnvironmentVariable("COMPUTERNAME").toUtf8().toStdString();
  session.me.pid = static_cast<std::uint32_t>(QApplication::applicationPid());
  session.me.sinceUnix = QDateTime::currentSecsSinceEpoch();
  gsproj::LockInfo holder;
  if (gsproj::TryAcquire(session.project.file, session.me, &holder) != gsproj::LockResult::Acquired) {
    const auto answer = QMessageBox::question(this, "Project in use",
        QString("This project is locked by %1 on %2. Open it read-only?")
          .arg(QString::fromUtf8(holder.user.c_str()), QString::fromUtf8(holder.machine.c_str())),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return 0;
    session.readOnly = true;
  }
  session.uid = m_State.nextProjectUid++;
  const auto uid = session.uid;
  OpenProjectPointDb(session, m_Log);
  m_State.openProjects.push_back(std::move(session));
  if (createDrawing) {
    NewDrawing(uid);
  }
  RefreshProjects();
  Refresh();
  return uid;
}

void EditorWindow::NewProject() {
  const QString folder = QFileDialog::getExistingDirectory(this, "Choose parent folder for the project");
  if (folder.isEmpty()) return;
  bool accepted = false;
  const QString name = QInputDialog::getText(this, "New project", "Project name", QLineEdit::Normal, {}, &accepted);
  if (!accepted || name.trimmed().isEmpty()) return;
  gsproj::Project project;
  std::string error;
  if (!gsproj::Create(std::filesystem::path(folder.toStdWString()), name.toUtf8().toStdString(), &project, &error)) {
    QMessageBox::warning(this, "New project", QString::fromUtf8(error.c_str()));
    return;
  }
  OpenProject(QString::fromStdWString(project.file.wstring()), true);
}

void EditorWindow::RefreshProjects() {
  EnforceProjectSettings(m_State, m_Log);
  SyncProjectPoints(m_State, m_Log, Now());
}

void EditorWindow::ShowPasteWarning() {
  m_ModalOpen = true;
  const auto pending = m_State.pastePrompt;
  bool completed = false;
  while (!completed) {
    QMessageBox question(QMessageBox::Warning, "Paste into drawing", QString::fromUtf8(pending.text.c_str()), QMessageBox::Cancel, this);
    question.setObjectName("pasteWarning");
    auto* convert = pending.canConvert ? question.addButton("Convert and paste", QMessageBox::ActionRole) : nullptr;
    auto* proceed = !pending.block ? question.addButton("Paste anyway", QMessageBox::ActionRole) : nullptr;
    question.setDefaultButton(QMessageBox::Cancel);
    question.exec();
    const bool converting = convert && question.clickedButton() == convert;
    if (!converting && (!proceed || question.clickedButton() != proceed)) {
      m_State.pastePrompt = {};
      break;
    }
    if (converting && !ConvertClipboardForPaste(m_State, m_Log)) {
      QMessageBox::warning(this, "Cannot convert clipboard", "Conversion failed. See command history for details.");
      continue;
    }
    m_State.pastePrompt = {};
    m_State.pasteWarningAnswered = true;
    if (pending.original) StartPasteOrigCommand(m_State, m_Log);
    else StartPasteCommand(m_State, m_Log);
    completed = true;
  }
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowProjectPointWarning() {
  using Answer = AppCommandState::PointEditPrompt::Answer;
  const auto pending = m_State.pointEditPrompt;
  const bool conflict = !pending.conflicts.empty() && pending.conflictAnswer == Answer::None;
  if (!conflict && (!pending.deletePending || pending.deleteAnswer != Answer::None)) return;
  m_ModalOpen = true;
  QMessageBox question(QMessageBox::Warning, "Shared project points", {}, QMessageBox::NoButton, this);
  question.setObjectName("projectPointWarning");
  QStringList ids;
  const auto& numbers = conflict ? pending.conflicts : pending.removed;
  for (int id : numbers) { if (ids.size() == 8) { ids.append("…"); break; } ids.append(QString::number(id)); }
  const auto project = QString::fromUtf8(pending.projectName.c_str());
  if (conflict) question.setText(QString("Project %1 already contains point number(s) %2, hidden in this drawing. Overwrite replaces them throughout the project. Renumber preserves them and gives the new points free numbers.").arg(project, ids.join(", ")));
  else {
    question.setText(QString("Deleting point(s) %1 removes them from project %2 and its other drawings. Hide here preserves the shared points.").arg(ids.join(", "), project));
    QStringList names;
    for (const auto& name : pending.otherNames) names.append(QString::fromUtf8(name.c_str()));
    question.setInformativeText(QString("Other open drawings affected: %1 (%2).\nClosed project drawings that may use these points: %3.").arg(pending.othersOpenShowing).arg(names.join(", ")).arg(pending.othersClosedMaybe));
  }
  auto* proceed = question.addButton(conflict ? "Overwrite" : "Delete from project", QMessageBox::DestructiveRole);
  auto* alternative = question.addButton(conflict ? "Renumber" : "Hide in this drawing only", QMessageBox::ActionRole);
  auto* cancel = question.addButton(QMessageBox::Cancel);
  question.setDefaultButton(cancel); question.setEscapeButton(cancel);
  question.exec();
  const auto& current = m_State.pointEditPrompt;
  if (current.active && current.projectUid == pending.projectUid && current.tabIdx == pending.tabIdx) {
    AnswerPointEdit(m_State, !conflict, question.clickedButton() == proceed ? Answer::Proceed :
        question.clickedButton() == alternative ? (conflict ? Answer::Renumber : Answer::HideHere) : Answer::Cancel);
    RefreshProjects();
  }
  m_ModalOpen = false;
  Refresh();
}
