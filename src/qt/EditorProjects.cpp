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
#include <QFrame>
#include <QPalette>
#include <QComboBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFile>
#include <QFileInfo>
#include <QAbstractItemView>
#include <algorithm>
#include <chrono>
#include <cstdint>
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
enum class LockChoice : std::uint8_t { Cancel, ReadOnly, TakeOver };

QString LockWhen(std::int64_t unix) {
  if (unix <= 0) return "an unknown time";
  return QDateTime::fromSecsSinceEpoch(unix).toString("yyyy-MM-dd HH:mm");
}

/// REQ-382 clauses 2-3: the project is held by another editor. Says who, whether the lock looks left
/// over from a crash, and offers Open read-only, Take over, or Cancel (Esc). Take over is the stale
/// takeover; for a live holder it carries the explicit warning that their unsaved work can be lost.
LockChoice AskLockedProject(QWidget* parent, const QString& projectName, const gsproj::LockInfo& holder, bool stale) {
  const QString who = holder.user.empty() ? QString("someone") : QString::fromUtf8(holder.user.c_str());
  const QString where = holder.machine.empty() ? QString() : QString(" on %1").arg(QString::fromUtf8(holder.machine.c_str()));
  const bool dark = parent->palette().color(QPalette::Window).lightness() < 128;
  const QString amber = dark ? "#f0b25a" : "#9a5b00";
  const QString border = dark ? "#4b4e50" : "#c5d0da";
  const QString cardFill = dark ? "#2d2f31" : "#ffffff";

  QDialog dialog(parent);
  dialog.setObjectName("projectLockDialog");
  dialog.setWindowTitle("Project in use");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.setMinimumWidth(620);
  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(24, 20, 24, 18);
  layout->setSpacing(10);

  auto label = [&](const QString& text, const char* name) {
    auto* value = new QLabel(text, &dialog);
    value->setObjectName(name);
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return value;
  };
  auto button = [&](const char* name, const QString& text, bool primary) {
    auto* value = new QPushButton(text, &dialog);
    value->setObjectName(name);
    value->setMinimumHeight(34);
    if (primary) {
      value->setDefault(true);
      value->setStyleSheet("QPushButton { background:#438fda; color:white; font-weight:600; padding:6px 18px; "
                           "border:0; border-radius:4px; } QPushButton:hover { background:#347cc3; }");
    }
    return value;
  };
  auto* headline = label(QString("“%1” is being edited by %2%3").arg(projectName, who, where), "projectLockHeadline");
  QFont headlineFont = headline->font();
  headlineFont.setPointSize(14);
  headlineFont.setBold(true);
  headline->setFont(headlineFont);
  auto* since = label(QString("Since %1.").arg(LockWhen(holder.sinceUnix)), "projectLockSince");
  auto* staleNote = label("That program is no longer running, so the lock looks left over from a crash.", "projectLockStale");
  staleNote->setStyleSheet(QString("font-weight:600;color:%1;").arg(amber));
  staleNote->setVisible(stale);
  auto* readOnlyText = label("<b>Open read-only</b> lets you view and print but not change the project.", "projectLockReadOnlyText");
  readOnlyText->setTextFormat(Qt::RichText);
  auto* takeOverText = label(
      stale ? "<b>Take over</b> removes the leftover lock and makes you the editor."
            : "<b>Take over anyway</b> makes you the editor. Only do this if you are sure the other person is "
              "not editing: their unsaved work in this project could be overwritten.",
      "projectLockTakeOverText");
  takeOverText->setTextFormat(Qt::RichText);
  auto* readOnly = button("projectLockReadOnly", "Open read-only", true);
  auto* takeOver = button("projectLockTakeOver", stale ? "Take over (stale lock)" : "Take over anyway", false);
  auto* cancel = button("projectLockCancel", "Cancel", false);

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
    return box;
  };
  auto* readOnlyBox = card("projectLockReadOnlyCard", "Open read-only", "SAFE — nothing can be overwritten", true);
  readOnlyBox->addWidget(readOnlyText);
  readOnlyBox->addStretch();
  readOnlyBox->addWidget(readOnly);
  auto* takeOverBox = card("projectLockTakeOverCard", "Take over", stale ? "LEFT OVER FROM A CRASH" : "ONLY IF THEY ARE NOT EDITING", false);
  takeOverBox->addWidget(takeOverText);
  takeOverBox->addStretch();
  takeOverBox->addWidget(takeOver);
  auto* cards = new QHBoxLayout;
  cards->setSpacing(12);
  cards->addWidget(readOnlyBox->parentWidget());
  cards->addWidget(takeOverBox->parentWidget());
  auto* footer = new QHBoxLayout;
  footer->addStretch();
  footer->addWidget(cancel);
  layout->addWidget(headline);
  layout->addWidget(since);
  layout->addWidget(staleNote);
  layout->addLayout(cards);
  layout->addLayout(footer);

  LockChoice choice = LockChoice::Cancel;  // Esc and the window close button are Cancel
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(readOnly, &QPushButton::clicked, &dialog, [&] { choice = LockChoice::ReadOnly; dialog.accept(); });
  QObject::connect(takeOver, &QPushButton::clicked, &dialog, [&] { choice = LockChoice::TakeOver; dialog.accept(); });
  dialog.exec();
  return choice;
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
    const bool stale = gsproj::IsStale(holder, session.me, gsproj::ProcessIsRunning);
    const auto choice = AskLockedProject(this, QString::fromUtf8(session.project.name.c_str()), holder, stale);
    if (choice == LockChoice::Cancel) return 0;
    if (choice == LockChoice::ReadOnly) {
      session.readOnly = true;
    } else if (!gsproj::TakeOver(session.project.file, session.me)) {
      QMessageBox::warning(this, "Project in use",
          QString("Could not take over the project lock for %1.").arg(QString::fromUtf8(session.project.name.c_str())));
      return 0;
    }
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
