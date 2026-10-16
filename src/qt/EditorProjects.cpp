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
#include <algorithm>
#include <chrono>

namespace {
double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

EditorWindow::~EditorWindow() {
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
