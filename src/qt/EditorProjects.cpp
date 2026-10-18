#include "EditorWindow.hpp"
#include "update/UpdateService.hpp"
#include "telemetry/TelemetryService.hpp"  // ~EditorWindow owns the in-flight ping
#include "auth/AuthService.hpp"
#include "ProjectSettings.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include "ProjectAddFlow.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPack.hpp"
#include "ProjectTurnover.hpp"
#include "ProjectWarnings.hpp"
#include "io/ProjectFiles.hpp"
#include "io/ProjectPack.hpp"
#include "io/ProjectTurnover.hpp"
#include "RecentDrawings.hpp"
#include "AppPaths.hpp"
#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFile>
#include <QFileInfo>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>

namespace {
double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::filesystem::path RecentProjectStorePath() {
  const auto dir = UserDataDirectory();
  return (dir.empty() ? std::filesystem::path(".") : dir) / "gosurvey-recent-projects.json";
}
void NoteRecentProject(const std::filesystem::path& path) {
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(path, ec);
  const auto& stored = ec ? path : absolute;
  const auto utf8 = QString::fromStdWString(stored.wstring()).toUtf8().toStdString();
  recent::Note(RecentProjectStorePath(), utf8, "", QDateTime::currentSecsSinceEpoch());
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
    NoteRecentProject(session.project.file);
    RefreshRecentProjects();
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
  NoteRecentProject(session.project.file);
  m_State.openProjects.push_back(std::move(session));
  if (createDrawing) {
    NewDrawing(uid);
  }
  RefreshProjects();
  RefreshRecentProjects();
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

void EditorWindow::ShowAddDrawing(std::uint32_t projectUid, const QString& selectedPath) {
  const auto session = std::find_if(m_State.openProjects.begin(), m_State.openProjects.end(),
      [projectUid](const auto& value) { return value.uid == projectUid; });
  if (session == m_State.openProjects.end()) {
    QMessageBox::warning(this, "Add Drawing to Project", "The project is no longer open.");
    return;
  }
  if (session->readOnly) {
    QMessageBox::warning(this, "Add Drawing to Project", "This project is read-only. No drawing can be added.");
    return;
  }
  QString sourcePath = selectedPath;
  if (sourcePath.isEmpty())
    sourcePath = QFileDialog::getOpenFileName(this, "Choose drawing to add", {}, "Drawings (*.dwg)");
  if (sourcePath.isEmpty()) return;

  AddDrawingPlan plan;
  PrepareAddDrawing(m_State, projectUid, sourcePath.toUtf8().toStdString(), &plan, m_Log);
  if (!plan.error.empty()) {
    QMessageBox::warning(this, "Add Drawing to Project", QString::fromUtf8(plan.error.c_str()));
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName("addDrawingDialog");
  dialog.setWindowTitle("Add Drawing to Project");
  dialog.setMinimumWidth(780);
  auto* root = new QVBoxLayout(&dialog);
  auto* steps = new QLabel("1  Drawing     ›     2  Align     ›     3  Points     ›     4  Confirm", &dialog);
  steps->setObjectName("addDrawingSteps");
  steps->setTextFormat(Qt::RichText);
  steps->setStyleSheet("font-size: 13px; padding: 8px;");
  root->addWidget(steps);
  auto* stack = new QStackedWidget(&dialog);
  stack->setObjectName("addDrawingPages");
  root->addWidget(stack, 1);

  auto* alignPage = new QWidget(stack);
  auto* align = new QVBoxLayout(alignPage);
  auto* source = new QLabel(QString("<b>%1</b><br>%2").arg(QFileInfo(sourcePath).fileName(), sourcePath.toHtmlEscaped()), alignPage);
  source->setObjectName("addDrawingSource"); source->setTextFormat(Qt::RichText); source->setWordWrap(true);
  align->addWidget(source);
  auto* alignTitle = new QLabel("Coordinate alignment", alignPage); alignTitle->setStyleSheet("font-size: 16px; font-weight: 600;");
  align->addWidget(alignTitle);
  auto* alignment = new QLabel(alignPage); alignment->setObjectName("addDrawingAlignment"); alignment->setWordWrap(true);
  auto refreshAlignment = [&] {
    if (!plan.conversion.Needed()) {
      alignment->setText("This drawing already matches the project's coordinate system and units.");
      alignment->setStyleSheet("color:#67bd86;");
    } else if (plan.converted) {
      alignment->setText(QString("Converted copy to the project's coordinate system and units. Estimated leftover error at the drawing extents: %1 m.")
          .arg(plan.conversion.residualMeters, 0, 'f', 4));
      alignment->setStyleSheet("color:#67bd86;");
    } else {
      QString text = "The drawing differs from the project:";
      if (plan.conversion.unitsDiffer) text += "\n• Linear units differ";
      if (plan.conversion.zoneDiffers) text += "\n• Coordinate systems differ";
      if (!plan.blockers.empty()) {
        text += "\n\nThis drawing cannot be converted because it contains:";
        for (const auto& blocker : plan.blockers) text += "\n• " + QString::fromUtf8(blocker.c_str());
      } else if (!plan.conversion.ok) {
        text += "\n\nConversion is unavailable: " + QString::fromUtf8(plan.conversion.error.c_str());
      } else if (plan.conversion.zoneDiffers) {
        text += QString("\n\nEstimated leftover error at the drawing extents: %1 m (maximum allowed 0.02 m).")
            .arg(plan.conversion.residualMeters, 0, 'f', 4);
      }
      alignment->setText(text); alignment->setStyleSheet("color:#e9bd66;");
    }
  };
  align->addWidget(alignment);
  auto* convert = new QPushButton("Convert project copy", alignPage); convert->setObjectName("addDrawingConvert");
  auto* convertRow = new QHBoxLayout(); convertRow->addWidget(convert); convertRow->addStretch(1);
  align->addLayout(convertRow);
  align->addStretch(1); stack->addWidget(alignPage);

  auto* pointsPage = new QWidget(stack);
  auto* pointLayout = new QVBoxLayout(pointsPage);
  auto* summary = new QLabel(pointsPage); summary->setObjectName("addDrawingSummary"); summary->setWordWrap(true);
  pointLayout->addWidget(summary);
  auto* table = new QTableWidget(pointsPage); table->setObjectName("addDrawingConflicts");
  table->setColumnCount(4); table->setHorizontalHeaderLabels({"Point", "Project value", "Drawing value", "Action"});
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  table->verticalHeader()->hide();
  table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setAlternatingRowColors(true); pointLayout->addWidget(table, 1);
  auto fillPoints = [&] {
    const auto& sm = plan.summary;
    summary->setText(QString("%1 point(s) found: %2 new, %3 identical and shared, %4 with different values. Duplicate numbers in the drawing: %5.")
        .arg(sm.total).arg(sm.fresh).arg(sm.identical).arg(sm.differing).arg(sm.duplicates));
    table->setRowCount(static_cast<int>(sm.conflicts.size()));
    for (int row = 0; row < static_cast<int>(sm.conflicts.size()); ++row) {
      const auto& conflict = sm.conflicts[static_cast<size_t>(row)];
      table->setItem(row, 0, new QTableWidgetItem(QString::number(conflict.id)));
      table->setItem(row, 1, new QTableWidgetItem(QString("%1, %2, %3 · %4").arg(conflict.existing.easting,0,'f',3).arg(conflict.existing.northing,0,'f',3).arg(conflict.existing.elevation,0,'f',3).arg(QString::fromUtf8(conflict.existing.description.c_str()))));
      table->setItem(row, 2, new QTableWidgetItem(QString("%1, %2, %3 · %4").arg(conflict.incoming.easting,0,'f',3).arg(conflict.incoming.northing,0,'f',3).arg(conflict.incoming.elevation,0,'f',3).arg(QString::fromUtf8(conflict.incoming.description.c_str()))));
      auto* choice = new QComboBox(table); choice->setObjectName(QString("addDrawingChoice_%1").arg(conflict.id));
      choice->addItems({"Skip", "Overwrite", "Renumber"});
      connect(choice, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&, id = conflict.id](int index) {
        plan.choices[id] = static_cast<projadd::Choice>(index);
      });
      table->setCellWidget(row, 3, choice);
    }
    table->setVisible(!sm.conflicts.empty());
    if (sm.conflicts.empty()) table->setMaximumHeight(0); else table->setMaximumHeight(360);
  };
  stack->addWidget(pointsPage);

  auto* confirmPage = new QWidget(stack);
  auto* confirm = new QVBoxLayout(confirmPage);
  auto* impact = new QLabel(confirmPage); impact->setObjectName("addDrawingImpact"); impact->setWordWrap(true);
  confirm->addWidget(impact); confirm->addStretch(1); stack->addWidget(confirmPage);
  auto updateConfirm = [&] {
    size_t skipped = 0, overwritten = 0, renumbered = 0;
    for (const auto& conflict : plan.summary.conflicts) {
      const auto it = plan.choices.find(conflict.id);
      const auto choice = it == plan.choices.end() ? projadd::Choice::Skip : it->second;
      if (choice == projadd::Choice::Skip) ++skipped;
      else if (choice == projadd::Choice::Overwrite) ++overwritten;
      else ++renumbered;
    }
    QString text = QString("<b>Project:</b> %1<br><b>Copy:</b> Drawings/%2<br><br>%3 new point(s), %4 identical point(s) shared, %5 conflict(s) resolved.")
      .arg(QString::fromUtf8(plan.projectName.c_str()), QFileInfo(sourcePath).fileName())
      .arg(plan.summary.fresh).arg(plan.summary.identical).arg(plan.summary.differing);
    text += QString("<br><br><b>Conflict choices:</b> %1 skipped, %2 overwritten, %3 renumbered.")
        .arg(skipped).arg(overwritten).arg(renumbered);
    if (!plan.overrides.empty()) {
      text += "<br><br><b>Settings saved as drawing overrides:</b> ";
      for (size_t i=0;i<plan.overrides.size();++i) text += (i ? ", " : "") + QString::fromUtf8(plan.overrides[i].c_str());
    }
    impact->setText(text);
  };

  auto* validation = new QLabel(&dialog); validation->setObjectName("addDrawingValidation");
  validation->setStyleSheet("color:#ed9a8f;"); validation->setWordWrap(true); root->addWidget(validation);
  auto* buttons = new QDialogButtonBox(&dialog); buttons->setObjectName("addDrawingButtons");
  auto* back = buttons->addButton("Back", QDialogButtonBox::ButtonRole::ResetRole);
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  auto* next = buttons->addButton("Continue", QDialogButtonBox::AcceptRole);
  back->setObjectName("addDrawingBack"); cancel->setObjectName("addDrawingCancel"); next->setObjectName("addDrawingNext");
  root->addWidget(buttons);
  auto updateStep = [&] {
    const int page = stack->currentIndex();
    steps->setText(page == 0 ? "1  Drawing     ›     <b><span style='color:#62b4e4'>2  Align</span></b>     ›     3  Points     ›     4  Confirm" :
                   page == 1 ? "✓  Drawing     ›     ✓  Align     ›     <b><span style='color:#62b4e4'>3  Points</span></b>     ›     4  Confirm" :
                               "✓  Drawing     ›     ✓  Align     ›     ✓  Points     ›     <b><span style='color:#62b4e4'>4  Confirm</span></b>");
    back->setVisible(page > 0); next->setText(page == 2 ? "Add Drawing" : "Continue");
    next->setEnabled(page != 0 || !plan.Blocked()); validation->clear();
    if (page == 2) updateConfirm();
  };
  connect(convert, &QPushButton::clicked, &dialog, [&] {
    ChooseAddConvert(m_State, &plan, m_Log); refreshAlignment();
    convert->setEnabled(plan.CanConvert());
    updateStep();
  });
  connect(back, &QPushButton::clicked, &dialog, [&] { stack->setCurrentIndex(stack->currentIndex()-1); updateStep(); });
  connect(next, &QPushButton::clicked, &dialog, [&] {
    const int page = stack->currentIndex();
    if (page == 0) {
      if (plan.Blocked()) { validation->setText("Convert the copy before continuing, or cancel the workflow."); return; }
      fillPoints(); stack->setCurrentIndex(1);
    } else if (page == 1) stack->setCurrentIndex(2);
    else {
      auto committedProject = std::find_if(m_State.openProjects.begin(), m_State.openProjects.end(),
          [projectUid](const auto& value) { return value.uid == projectUid; });
      std::optional<projpts::Db> pointsBefore;
      if (committedProject != m_State.openProjects.end() && committedProject->points)
        pointsBefore = *committedProject->points;
      AddDrawingResult result;
      if (!CommitAddDrawing(m_State, plan, Now(), &result, m_Log)) {
        validation->setText(plan.error.empty() ? "The drawing could not be added. The project was left unchanged." : QString::fromUtf8(plan.error.c_str()));
        if (!plan.error.empty()) next->setEnabled(false);
        return;
      }
      if (committedProject != m_State.openProjects.end() && !FlushProjectPointDb(*committedProject, m_Log)) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(result.destPath), ec);
        if (committedProject->points && pointsBefore) {
          // CommitAddDrawing only changed the in-memory database. Its atomic save failed, so restore
          // the previous contents and leave the project copy out as well.
          *committedProject->points = std::move(*pointsBefore);
        }
        validation->setText("The project point database could not be saved. No drawing was added; see the command log.");
        next->setEnabled(false);
        return;
      }
      dialog.accept();
      OpenDrawing(QString::fromUtf8(result.destPath.c_str()));
      return;
    }
    updateStep();
  });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  refreshAlignment();
  convert->setVisible(plan.conversion.Needed() && !plan.converted);
  convert->setEnabled(plan.CanConvert());
  if (!plan.conversion.Needed()) stack->setCurrentIndex(1);
  updateStep();
  m_ModalOpen = true; dialog.exec(); m_ModalOpen = false;
  Refresh();
}

void EditorWindow::RefreshProjects() {
  EnforceProjectSettings(m_State, m_Log);
  SyncProjectPoints(m_State, m_Log, Now());
}

namespace {
AppCommandState::ProjectSession* ProjectSession(AppCommandState& state, std::uint32_t uid) {
  const auto found = std::find_if(state.openProjects.begin(), state.openProjects.end(),
      [uid](const auto& session) { return session.uid == uid; });
  return found == state.openProjects.end() ? nullptr : &*found;
}

QString HealthText(const projfiles::Health& health) {
  QStringList lines;
  const auto append = [&lines](const QString& heading, const std::vector<std::string>& entries) {
    if (entries.empty()) return;
    lines << heading;
    for (const auto& entry : entries) lines << "  • " + QString::fromUtf8(entry.c_str());
  };
  append("Linked files (will not travel with the project):", health.linked);
  append("Missing files:", health.missing);
  append("Files unavailable to this version:", health.unavailable);
  append("Drawings with unsaved changes:", health.unsaved);
  append("Point clouds omitted from a pack:", health.omitted);
  return lines.join('\n');
}

void ApplyProjectWorkflowDarkStyle(QDialog& dialog) {
  dialog.setStyleSheet(R"(
    QDialog { background: #222426; color: #edf2f7; }
    QLabel, QCheckBox { color: #edf2f7; }
    QPlainTextEdit, QListWidget, QLineEdit {
      background: #252729; color: #edf2f7; border: 1px solid #4b4e50;
      selection-background-color: #356f96; selection-color: #ffffff;
    }
    QPushButton {
      background: #383a3c; color: #edf2f7; border: 1px solid #4b4e50;
      border-radius: 4px; padding: 6px 10px;
    }
    QPushButton:hover { background: #45484a; }
    QPushButton:pressed { background: #303234; }
    QPushButton:disabled { background: #303234; color: #8d9296; }
    QScrollBar:vertical { background: #252729; width: 12px; }
    QScrollBar::handle:vertical { background: #505457; min-height: 24px; }
    QScrollBar:horizontal { background: #252729; height: 12px; }
    QScrollBar::handle:horizontal { background: #505457; min-width: 24px; }
  )");
}
}

void EditorWindow::ShowProjectHealth(std::uint32_t uid) {
  auto* session = ProjectSession(m_State, uid);
  if (!session) return;
  QDialog dialog(this);
  dialog.setObjectName("projectHealthDialog");
  dialog.setWindowTitle("Project Health — " + QString::fromUtf8(session->project.name.c_str()));
  ApplyProjectWorkflowDarkStyle(dialog);
  dialog.setMinimumWidth(560);
  auto* layout = new QVBoxLayout(&dialog);
  auto* heading = new QLabel("Project Health", &dialog); heading->setObjectName("projectHealthHeading");
  QFont font = heading->font(); font.setPointSize(font.pointSize() + 3); font.setBold(true); heading->setFont(font);
  layout->addWidget(heading);
  auto* status = new QLabel(&dialog); status->setObjectName("projectHealthStatus"); status->setWordWrap(true);
  auto* details = new QPlainTextEdit(&dialog); details->setObjectName("projectHealthDetails"); details->setReadOnly(true); details->setMinimumHeight(230);
  details->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  details->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  details->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  auto refresh = [&] {
    const auto health = ProjectHealthFor(m_State, uid);
    const int problems = static_cast<int>(health.linked.size() + health.missing.size() + health.unavailable.size() + health.unsaved.size());
    status->setText(health.Clean() ? "No problems found. Every tracked file is in the project and every drawing is saved."
                                   : QString("%1 project health issue(s) need attention or acknowledgment.").arg(problems));
    details->setPlainText(health.Clean() && health.omitted.empty() ? "No file or drawing issues found."
                                                                  : HealthText(health));
  };
  refresh(); layout->addWidget(status); layout->addWidget(details, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* copy = buttons->addButton("Copy links into the project", QDialogButtonBox::ActionRole);
  copy->setObjectName("projectHealthCopyLinks");
  copy->setEnabled(!session->readOnly && !ProjectHealthFor(m_State, uid).linked.empty());
  connect(copy, &QPushButton::clicked, &dialog, [&] {
    size_t converted = 0; CopyProjectLinksIn(m_State, uid, m_Log, &converted); refresh();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::clicked, &dialog, [&dialog](QAbstractButton* button) {
    if (button->text() == "Close") dialog.accept();
  });
  layout->addWidget(buttons); m_ModalOpen = true; dialog.exec(); m_ModalOpen = false;
}

void EditorWindow::ShowProjectPack(std::uint32_t uid) {
  auto* session = ProjectSession(m_State, uid);
  if (!session) return;
  QDialog dialog(this); dialog.setObjectName("projectPackDialog");
  dialog.setWindowTitle("Pack Project — " + QString::fromUtf8(session->project.name.c_str())); dialog.setMinimumWidth(590);
  ApplyProjectWorkflowDarkStyle(dialog);
  auto* layout = new QVBoxLayout(&dialog);
  auto* healthLabel = new QLabel(&dialog); healthLabel->setObjectName("projectPackHealth"); healthLabel->setWordWrap(true); layout->addWidget(healthLabel);
  auto* acceptIssues = new QCheckBox("Pack anyway, with the problems above", &dialog); acceptIssues->setObjectName("projectPackAnyway"); layout->addWidget(acceptIssues);
  auto* summary = new QLabel(&dialog); summary->setObjectName("projectPackSummary"); summary->setWordWrap(true); layout->addWidget(summary);
  auto* omitClouds = new QCheckBox("Leave point clouds out of the pack", &dialog); omitClouds->setObjectName("projectPackExcludeClouds"); layout->addWidget(omitClouds);
  auto* error = new QLabel(&dialog); error->setObjectName("projectPackError"); error->setWordWrap(true); error->setStyleSheet("color:#c5544f"); layout->addWidget(error);
  gspack::PackPlan plan; std::string planError;
  if (!FlushProjectPointDb(*session, m_Log)) {
    QMessageBox::warning(this, "Pack Project", "The project's shared point database could not be saved. No pack was created.");
    return;
  }
  const bool planned = gspack::PlanPack(session->project, {}, &plan, &planError);
  if (!planned) error->setText(QString::fromUtf8(planError.c_str()));
  auto refresh = [&] {
    const auto h = ProjectHealthFor(m_State, uid); const QString health = HealthText(h);
    healthLabel->setText(h.Clean() ? "Project Health: no problems found." : "Project Health\n" + health);
    acceptIssues->setVisible(!h.Clean());
    const auto bytes = plan.totalBytes - (omitClouds->isChecked() ? plan.pointCloudBytes : 0);
    summary->setText(planned ? QString("%1 file%2 · %3 before compression.%4")
      .arg(plan.files.size()).arg(plan.files.size() == 1 ? "" : "s")
      .arg(QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + " MB")
      .arg(plan.pointCloudBytes ? QString("\nPoint clouds account for %1 MB.").arg(static_cast<double>(plan.pointCloudBytes) / (1024.0 * 1024.0), 0, 'f', 1) : QString())
      : QString("Project files could not be listed: %1").arg(QString::fromUtf8(planError.c_str())));
    if (bytes >= gspack::kEmailWarnBytes) summary->setText(summary->text() + "\nThis is large for an email attachment (25 MB or more).");
    if (plan.pointCloudBytes) omitClouds->setVisible(true); else omitClouds->setVisible(false);
  };
  refresh(); connect(omitClouds, &QCheckBox::toggled, &dialog, refresh);
  auto* buttons = new QDialogButtonBox(&dialog); auto* pack = buttons->addButton("Pack…", QDialogButtonBox::AcceptRole);
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel); pack->setObjectName("projectPackStart");
  auto updatePackEnabled = [&] {
    const auto h = ProjectHealthFor(m_State, uid);
    pack->setEnabled(planned && (h.Clean() || acceptIssues->isChecked()));
  };
  connect(acceptIssues, &QCheckBox::toggled, &dialog, updatePackEnabled);
  updatePackEnabled();
  connect(pack, &QPushButton::clicked, &dialog, [&] {
    const auto h = ProjectHealthFor(m_State, uid); if (!planned || (!h.Clean() && !acceptIssues->isChecked())) return;
    const QString suggested = QString::fromUtf8(session->project.name.c_str()) + ".gspack";
    const QString out = QFileDialog::getSaveFileName(&dialog, "Pack Project", suggested, "GoSurvey project pack (*.gspack)");
    if (out.isEmpty()) return;
    gspack::PackPlan current; std::string why;
    gspack::PackOptions options; options.excludePointClouds = omitClouds->isChecked(); options.nowUnix = QDateTime::currentSecsSinceEpoch();
    if (!gspack::PlanPack(session->project, std::filesystem::path(out.toStdWString()), &current, &why) ||
        !gspack::WritePack(session->project, current, options, std::filesystem::path(out.toStdWString()), &why)) {
      error->setText(QString::fromUtf8(why.c_str())); return;
    }
    dialog.accept();
  });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject); layout->addWidget(buttons);
  m_ModalOpen = true; dialog.exec(); m_ModalOpen = false;
}

void EditorWindow::OpenPackedProject() {
  const QString pack = QFileDialog::getOpenFileName(this, "Open Packed Project", {}, "GoSurvey project pack (*.gspack)");
  if (pack.isEmpty()) return;
  const QString folder = QFileDialog::getExistingDirectory(this, "Choose an empty folder for the project");
  if (folder.isEmpty()) return;
  std::filesystem::path marker; gspack::Manifest manifest; std::string error;
  if (!gspack::ExtractPack(std::filesystem::path(pack.toStdWString()), std::filesystem::path(folder.toStdWString()), &marker, &manifest, &error)) {
    QMessageBox::warning(this, "Open Packed Project", QString::fromUtf8(error.c_str())); return;
  }
  OpenProject(QString::fromStdWString(marker.wstring()), true);
}

void EditorWindow::ShowProjectTurnover(std::uint32_t uid) {
  auto* session = ProjectSession(m_State, uid); if (!session) return;
  if (session->readOnly) { QMessageBox::warning(this, "Create Turnover", "This project is read-only. Nothing was written."); return; }
  QDialog dialog(this); dialog.setObjectName("projectTurnoverDialog");
  dialog.setWindowTitle("Create Turnover — " + QString::fromUtf8(session->project.name.c_str())); dialog.setMinimumSize(560, 520);
  ApplyProjectWorkflowDarkStyle(dialog);
  auto* layout = new QVBoxLayout(&dialog);
  auto* healthLabel = new QLabel(&dialog); healthLabel->setObjectName("projectTurnoverHealth"); healthLabel->setWordWrap(true); layout->addWidget(healthLabel);
  auto* acknowledge = new QCheckBox("Create the turnover anyway, with the problems above", &dialog); acknowledge->setObjectName("projectTurnoverAcknowledge"); layout->addWidget(acknowledge);
  auto* copyLinks = new QPushButton("Copy links into the project", &dialog); copyLinks->setObjectName("projectTurnoverCopyLinks"); layout->addWidget(copyLinks);
  auto* recipient = new QLineEdit(&dialog); recipient->setObjectName("projectTurnoverRecipient"); recipient->setPlaceholderText("Who receives this turnover");
  auto* form = new QFormLayout(); form->addRow("Recipient", recipient); layout->addLayout(form);
  auto* files = new QListWidget(&dialog); files->setObjectName("projectTurnoverFiles"); layout->addWidget(files, 1);
  const auto candidates = projturn::Candidates(session->project);
  for (const auto& path : candidates) { auto* item = new QListWidgetItem(QString::fromUtf8(path.c_str()), files); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Checked); item->setData(Qt::UserRole, QString::fromUtf8(path.c_str())); }
  auto refresh = [&] {
    const auto h = ProjectHealthFor(m_State, uid);
    healthLabel->setText(h.Clean() ? "Project Health: no problems found." : "Project Health\n" + HealthText(h));
    acknowledge->setVisible(!h.Clean());
    copyLinks->setEnabled(!session->readOnly && !h.linked.empty());
  };
  refresh(); connect(copyLinks, &QPushButton::clicked, &dialog, [&] { size_t n=0; CopyProjectLinksIn(m_State, uid, m_Log, &n); refresh(); });
  auto* buttons = new QDialogButtonBox(&dialog); auto* create = buttons->addButton("Create turnover", QDialogButtonBox::AcceptRole); auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  create->setObjectName("projectTurnoverCreate");
  connect(create, &QPushButton::clicked, &dialog, [&] {
    std::vector<std::string> chosen;
    for (int i=0; i<files->count(); ++i) if(files->item(i)->checkState()==Qt::Checked) chosen.push_back(files->item(i)->data(Qt::UserRole).toString().toUtf8().toStdString());
    if (!CreateProjectTurnover(m_State, uid, recipient->text().toUtf8().toStdString(), chosen, acknowledge->isChecked(), m_Log)) {
      QMessageBox::warning(&dialog, "Create Turnover", m_Log.empty() ? "The turnover could not be created." : QString::fromUtf8(m_Log.back().c_str())); return;
    }
    dialog.accept();
  });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject); layout->addWidget(buttons);
  m_ModalOpen = true; dialog.exec(); m_ModalOpen = false;
}

void EditorWindow::RefreshProjectFiles() {
  if (!::RefreshProjectFiles(m_State, m_Log)) {
    if (!m_Log.empty()) QMessageBox::information(this, "Refresh Project Files", QString::fromUtf8(m_Log.back().c_str()));
    return;
  }
  auto& prompt = m_State.projectRefreshPrompt;
  QDialog dialog(this); dialog.setObjectName("projectRefreshDialog"); dialog.setWindowTitle("New files found in project");
  ApplyProjectWorkflowDarkStyle(dialog);
  auto* layout = new QVBoxLayout(&dialog); layout->addWidget(new QLabel("Select the files to add to the project file list.", &dialog));
  auto* list = new QListWidget(&dialog); list->setObjectName("projectRefreshFiles"); layout->addWidget(list);
  for (const auto& path : prompt.files) { auto* item = new QListWidgetItem(QString::fromUtf8(path.c_str()), list); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Checked); }
  auto* buttons = new QDialogButtonBox(&dialog); auto* add = buttons->addButton("Add selected", QDialogButtonBox::AcceptRole); auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  add->setObjectName("projectRefreshAddSelected");
  connect(add, &QPushButton::clicked, &dialog, [&] { std::vector<std::string> selected; for(int i=0;i<list->count();++i) if(list->item(i)->checkState()==Qt::Checked) selected.push_back(prompt.files[static_cast<size_t>(i)]); TrackProjectFiles(m_State, prompt.projectUid, selected, m_Log); prompt = {}; dialog.accept(); });
  connect(cancel, &QPushButton::clicked, &dialog, [&] { prompt = {}; dialog.reject(); });
  layout->addWidget(buttons); m_ModalOpen = true; dialog.exec(); m_ModalOpen = false;
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
