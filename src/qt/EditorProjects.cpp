#include "EditorWindow.hpp"
#include "update/UpdateService.hpp"
#include "telemetry/TelemetryService.hpp"  // ~EditorWindow owns the in-flight ping
#include "auth/AuthService.hpp"
#include "ProjectSettings.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include "ProjectAddFlow.hpp"
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
