// REQ-077 / REQ-078 — the Qt side of the software-update workflow.
//
// The decisions (is this newer? is it skipped? is the download intact?) live in
// `update::` and are shared with the ImGui build. This file only presents the
// phases that service reports, and carries the user's choices back to it. As in
// the ImGui build, nothing downloads or installs without a click: the app holds
// unsaved drawings, and an unannounced restart destroys work.
#include "EditorWindow.hpp"
#include "GsMigrate.hpp"  // kGsFormatVersion — what the offered build's format is compared against
#include "NonDismissableDialog.hpp"
#include "UserPrefs.hpp"
#include "Version.hpp"
#include "update/UpdateService.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
/// "12.4 MB of 31.0 MB" — or just the received figure when the server sent no Content-Length.
QString FormatProgress(long long received, long long total) {
  auto megabytes = [](long long bytes) {
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + " MB";
  };
  return total > 0 ? megabytes(received) + " of " + megabytes(total) : megabytes(received);
}

QPushButton* AddButton(QHBoxLayout* row, QWidget* parent, const char* name, const QString& text,
                       bool primary) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(name);
  button->setMinimumHeight(34);
  if (primary) {
    button->setDefault(true);
    button->setStyleSheet(
        "QPushButton { background:#438fda; color:white; font-weight:600; padding:6px 18px; "
        "border:0; border-radius:4px; } QPushButton:hover { background:#347cc3; }"
        "QPushButton:disabled { background:#8fb4dc; }");
  }
  row->addWidget(button);
  return button;
}
}  // namespace

void EditorWindow::InitUpdates() {
  m_Update = std::make_unique<update::UpdateState>();
  m_Update->runningVersion = GOSURVEY_VERSION_FULL;
  m_LaunchInstaller = [](update::UpdateState& state) { return update::LaunchInstallerAndExit(state); };
}

void EditorWindow::StartUpdateCheck() {
  m_Update->prefs = m_State.updatePrefs;
  update::BeginStartupCheck(*m_Update, "chetjones003/GoSurvey");
}

bool EditorWindow::UpdateCheckRunning() const {
  return m_Update && m_Update->phase == update::Phase::Checking && m_Update->task &&
         !m_Update->task->done.load(std::memory_order_acquire);
}

bool EditorWindow::UpdateBlocksLaunch() const {
  return m_Update && m_Update->phase != update::Phase::Idle;
}

void EditorWindow::ServiceUpdate() {
  using update::Phase;
  update::PollUpdateTask(*m_Update);

  // "Install and Restart" was clicked. The unsaved-work guard is the window's own close path
  // (it prompts for every dirty drawing), so the update reuses it instead of repeating the
  // dirty check. closeEvent starts the installer once nothing is left to save.
  if (m_Update->awaitingUnsavedCheck) {
    m_Update->awaitingUnsavedCheck = false;
    m_UpdateExitPending = true;
    // A refusal means the user cancelled a save prompt, which cancels the update too; the
    // verified download stays on disk and the dialog offers it again.
    if (!close()) m_UpdateExitPending = false;
    return;
  }

  const Phase phase = m_Update->phase;
  const bool dialogPhase = phase == Phase::UpdateReady || phase == Phase::Downloading ||
                           phase == Phase::ReadyToLaunch || phase == Phase::Failed;
  auto* dialog = findChild<QDialog*>("softwareUpdate");
  if (dialogPhase && !dialog) ShowUpdateDialog();
  else if (!dialogPhase && dialog) dialog->accept();
}

void EditorWindow::ShowUpdateDialog() {
  using update::Phase;
  auto* dialog = new NonDismissableDialog(this);
  dialog->setObjectName("softwareUpdate");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Software Update");
  dialog->setWindowModality(Qt::ApplicationModal);
  dialog->setWindowFlag(Qt::WindowCloseButtonHint, false);
  auto* content = new QWidget(dialog);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(26, 22, 26, 20);
  layout->setSpacing(10);
  auto* headline = new QLabel(dialog);
  headline->setObjectName("updateHeadline");
  QFont headlineFont = headline->font();
  headlineFont.setPointSize(15);
  headlineFont.setBold(true);
  headline->setFont(headlineFont);
  headline->setWordWrap(true);
  layout->addWidget(headline);
  auto* detail = new QLabel(dialog);
  detail->setObjectName("updateDetail");
  detail->setWordWrap(true);
  layout->addWidget(detail);
  // REQ-078: compatibility comes BEFORE the release notes. A warning placed under a scrolling
  // notes box is a warning nobody reads, and this is the one thing here that can cost the user
  // their work.
  auto* compatibility = new QLabel(dialog);
  compatibility->setObjectName("updateCompatibility");
  compatibility->setWordWrap(true);
  layout->addWidget(compatibility);
  auto* notesCaption = new QLabel("What's new:", dialog);
  notesCaption->setObjectName("updateNotesCaption");
  layout->addWidget(notesCaption);
  auto* notes = new QTextBrowser(dialog);
  notes->setObjectName("updateNotes");
  notes->setOpenExternalLinks(true);
  notes->setMinimumHeight(240);
  layout->addWidget(notes, 1);
  auto* progress = new QProgressBar(dialog);
  progress->setObjectName("updateProgress");
  layout->addWidget(progress);
  auto* error = new QLabel(dialog);
  error->setObjectName("updateError");
  error->setWordWrap(true);
  error->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(error);
  auto* footnote = new QLabel(dialog);
  footnote->setObjectName("updateFootnote");
  footnote->setWordWrap(true);
  footnote->setStyleSheet("color:#777f89;");
  layout->addWidget(footnote);

  auto* row = new QHBoxLayout();
  row->setSpacing(8);
  auto* updateNow = AddButton(row, dialog, "updateNowButton", "Update Now", true);
  auto* remindLater = AddButton(row, dialog, "updateRemindButton", "Remind Me Later", false);
  auto* skipVersion = AddButton(row, dialog, "updateSkipButton", "Skip This Version", false);
  auto* cancelDownload = AddButton(row, dialog, "updateCancelButton", "Cancel", false);
  auto* install = AddButton(row, dialog, "updateInstallButton", "Install and Restart", true);
  auto* notNow = AddButton(row, dialog, "updateNotNowButton", "Not Now", false);
  auto* closeFailure = AddButton(row, dialog, "updateCloseButton", "Close", false);
  row->addStretch();
  layout->addLayout(row);

  connect(updateNow, &QPushButton::clicked, dialog, [this] { update::BeginDownload(*m_Update); });
  connect(remindLater, &QPushButton::clicked, dialog, [this] {
    // No state written: the next launch checks again and offers this same version.
    m_Update->phase = Phase::Idle;
  });
  connect(skipVersion, &QPushButton::clicked, dialog, [this] {
    update::SkipAvailableVersion(*m_Update);
    const std::string previousSkip = m_State.updatePrefs.skippedVersion;
    m_State.updatePrefs.skippedVersion = m_Update->prefs.skippedVersion;
    if (!SaveUserStartupPrefs(m_State)) {
      // Roll back so an unrelated later save cannot quietly persist a choice reported as lost.
      m_State.updatePrefs.skippedVersion = previousSkip;
      QMessageBox::warning(this, "Skip this version",
                           "Your choice to skip this version could not be saved, so it may be "
                           "offered again next time.");
    }
  });
  connect(cancelDownload, &QPushButton::clicked, dialog, [this] {
    if (m_Update->task) m_Update->task->cancel.store(true, std::memory_order_relaxed);
  });
  connect(install, &QPushButton::clicked, dialog,
          [this] { m_Update->awaitingUnsavedCheck = true; });
  connect(notNow, &QPushButton::clicked, dialog, [this] {
    // The verified installer stays on disk; the next launch re-offers and re-uses it.
    m_Update->phase = Phase::Idle;
  });
  connect(closeFailure, &QPushButton::clicked, dialog, [this] {
    m_Update->lastError.clear();
    m_Update->phase = Phase::Idle;
  });

  auto refresh = [this, dialog, headline, detail, compatibility, notesCaption, notes, progress,
                  error, footnote, updateNow, remindLater, skipVersion, cancelDownload, install,
                  notNow, closeFailure] {
    const auto& state = *m_Update;
    const QString version = QString::fromUtf8(state.available.version.c_str());
    const QString running = QString::fromUtf8(state.runningVersion.c_str());
    const Phase phase = state.phase;
    const bool offering = phase == Phase::UpdateReady;
    const bool downloading = phase == Phase::Downloading;
    const bool ready = phase == Phase::ReadyToLaunch;
    const bool failed = phase == Phase::Failed;
    // A button just ended the flow; ServiceUpdate closes the dialog on its next tick. Until then
    // leave the last frame alone rather than redrawing it as the failure state.
    if (!offering && !downloading && !ready && !failed) return;

    QString title;
    QString sub;
    if (offering) {
      title = "GoSurvey " + version + " is available.";
      sub = "You are running " + running + ".";
    } else if (downloading) {
      title = "Downloading GoSurvey " + version + "...";
    } else if (ready) {
      title = "GoSurvey " + version + " is ready to install.";
      sub = "The installer will close GoSurvey, update it, and start it again.";
    } else {
      title = "The update could not be completed.";
    }
    headline->setText(title);
    detail->setText(sub);
    detail->setVisible(!sub.isEmpty());

    QString warning;
    QString warningStyle;
    if (offering) {
      switch (update::ClassifyCompatibility(state.available, kGsFormatVersion)) {
        case update::CompatibilityRisk::Breaking:
          // Deliberately loud, and deliberately rare — only shown when a release author has
          // declared that existing drawings will not open.
          warning = "WARNING: this update cannot open your existing drawings.";
          if (!state.available.compatibilityWarning.empty())
            warning += "\n" + QString::fromUtf8(state.available.compatibilityWarning.c_str());
          warning += "\nBack up your drawings before continuing. If you are unsure, choose "
                     "Remind Me Later and check the release notes first.";
          warningStyle = "color:#c54646; font-weight:600;";
          break;
        case update::CompatibilityRisk::ForwardOnly:
          // Routine and expected, so stated plainly rather than alarmingly.
          warning = "Note: this update changes the drawing file format. Your existing drawings "
                    "will still open, but drawings you save afterwards cannot be opened by "
                    "GoSurvey " + running + ".";
          break;
        case update::CompatibilityRisk::None:
          break;
      }
    }
    compatibility->setStyleSheet(warningStyle);
    compatibility->setText(warning);
    compatibility->setVisible(!warning.isEmpty());

    // Set the notes only when the offer changes: re-setting every tick would reset the scroll
    // position under a user who is reading them.
    const bool showNotes = offering && !state.available.notes.empty();
    notesCaption->setVisible(showNotes);
    notes->setVisible(showNotes);
    if (showNotes && notes->property("shownVersion").toString() != version) {
      notes->setMarkdown(QString::fromUtf8(state.available.notes.c_str()));
      notes->setProperty("shownVersion", version);
    }

    progress->setVisible(downloading);
    if (downloading) {
      const long long received = state.task ? state.task->bytesReceived.load() : 0;
      const long long total = state.task ? state.task->bytesTotal.load() : 0;
      // A zero range is Qt's indeterminate marquee, used when the server sent no length.
      progress->setRange(0, total > 0 ? 1000 : 0);
      if (total > 0) progress->setValue(static_cast<int>(1000.0 * static_cast<double>(received) / static_cast<double>(total)));
      progress->setFormat(FormatProgress(received, total));
      progress->setTextVisible(true);
    }

    // REQ-201: this path was user-initiated, so unlike the silent startup check its failure is
    // stated plainly.
    error->setText(failed ? QString::fromUtf8(state.lastError.c_str()) : QString());
    error->setVisible(failed);
    QString note;
    if (offering)
      note = "GoSurvey will close and reopen automatically. You'll be asked to save any open "
             "work first.";
    else if (failed)
      note = "You can keep working; the update will be offered again next time.";
    footnote->setText(note);
    footnote->setVisible(!note.isEmpty());

    updateNow->setVisible(offering);
    remindLater->setVisible(offering);
    skipVersion->setVisible(offering);
    cancelDownload->setVisible(downloading);
    install->setVisible(ready);
    install->setEnabled(!m_UpdateExitPending);
    notNow->setVisible(ready);
    notNow->setEnabled(!m_UpdateExitPending);
    closeFailure->setVisible(failed);
    dialog->layout()->activate();
    const QSize before = dialog->size();
    dialog->adjustSize();
    // The height changes between phases; re-centre over the window so the buttons cannot grow
    // off the bottom of the screen. Only on a size change, so a dragged dialog stays put.
    if (dialog->size() != before)
      dialog->move(geometry().center() - QPoint(dialog->width() / 2, dialog->height() / 2));
  };
  // A navy banner carries the branding; the content sits beneath it.
  dialog->setFixedWidth(720);
  auto* outer = new QVBoxLayout(dialog);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto* banner = new QWidget(dialog);
  banner->setFixedHeight(76);
  banner->setStyleSheet("QWidget { background:#243a53; }");
  auto* bannerLayout = new QHBoxLayout(banner);
  bannerLayout->setContentsMargins(26, 10, 26, 10);
  auto* logo = new QLabel(banner);
  logo->setPixmap(QIcon(QtAsset("icons/app.ico")).pixmap(48, 48));
  bannerLayout->addWidget(logo);
  auto* bannerText = new QLabel("GoSurvey Software Update", banner);
  bannerText->setStyleSheet("color:#f4f7fb; font-size:20px; font-weight:700;");
  bannerLayout->addWidget(bannerText, 1);
  outer->addWidget(banner);
  outer->addWidget(content, 1);
  auto* timer = new QTimer(dialog);
  timer->setInterval(100);
  connect(timer, &QTimer::timeout, dialog, refresh);
  timer->start();
  refresh();
  dialog->show();
}
