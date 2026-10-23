// REQ-172 (issue #916) — the Qt side of point-cloud attach and export.
//
// The shared command layer already reads E57 / LAS / LAZ / PTS / PTX (`StartPointCloudImportAsync`,
// reaped by `TickPointCloudImport`) and writes PTS / PTX / LAS / LAZ (`ExportPointCloudsToFile`). This
// file only asks the user what to import or export, checks the choice before anything starts, shows
// progress with a Cancel button for the background import, and reports failures. E57 is import-only:
// no E57 writer exists in 0.7.4.
#include "EditorWindow.hpp"
#include "NonDismissableDialog.hpp"
#include "ProjectFiles.hpp"
#include "io/CadPointCloudE57.hpp"
#include "io/CadPointCloudLas.hpp"
#include "io/CadPointCloudLaz.hpp"
#include "io/CadPointCloudPts.hpp"
#include "io/CadPointCloudPtx.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace {
using Prompt = AppCommandState::ProjectAttachPrompt;
using SourceFormat = AppCommandState::PointCloudImportAsync::SourceFormat;

struct ScanFileCheck {
  bool ok = false;
  QString format;     // "ASPRS LAS" ...
  QString error;      // empty when ok or when nothing was entered yet
  qint64 bytes = 0;
  qint64 points = 0;  // 0 = unknown
};

QString FormatSize(qint64 bytes) {
  const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
  if (mb >= 1024.0) return QString("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
  if (mb >= 1.0) return QString("%1 MB").arg(mb, 0, 'f', 1);
  return QString("%1 KB").arg(std::max<qint64>(1, bytes / 1024));
}

/// Checks a chosen scan before the import starts. The shared importer treats any unknown extension
/// as E57, so an unsupported file would only fail after a background job; the dialog refuses it here.
ScanFileCheck CheckScanFile(const QString& path) {
  ScanFileCheck check;
  if (path.trimmed().isEmpty()) return check;
  const QFileInfo info(path.trimmed());
  if (!info.exists()) { check.error = "That file does not exist."; return check; }
  if (!info.isFile()) { check.error = "That path is a folder, not a scan file."; return check; }
  const QString ext = info.suffix().toLower();
  const std::string utf8 = info.absoluteFilePath().toUtf8().toStdString();
  if (ext == "e57") { check.format = "ASTM E57"; check.points = pointcloud_e57::QuickPointCountEstimate(utf8); }
  else if (ext == "las") { check.format = "ASPRS LAS"; check.points = pointcloud_las::QuickPointCountEstimate(utf8); }
  else if (ext == "laz") { check.format = "LAS compressed (LAZ)"; check.points = pointcloud_laz::QuickPointCountEstimate(utf8); }
  else if (ext == "pts") { check.format = "PTS text scan"; check.points = pointcloud_pts::QuickPointCountEstimate(utf8); }
  else if (ext == "ptx") { check.format = "PTX text scan"; check.points = pointcloud_ptx::QuickPointCountEstimate(utf8); }
  else {
    check.error = QString("GoSurvey cannot attach .%1 files. Choose an E57, LAS, LAZ, PTS or PTX scan.").arg(ext);
    return check;
  }
  check.bytes = info.size();
  if (check.bytes <= 0) { check.error = "That file is empty."; return check; }
  if (!info.isReadable()) { check.error = "That file cannot be read. Check its permissions."; return check; }
  check.ok = true;
  return check;
}

struct ExportFormatRow {
  PointCloudExportFormat format;
  const char* name;
  const char* extension;
  const char* description;
};
constexpr ExportFormatRow kExportFormats[] = {
    {PointCloudExportFormat::Pts, "PTS", "pts", "Text file of X Y Z, with colour and intensity when the cloud has them."},
    {PointCloudExportFormat::Ptx, "PTX", "ptx", "Text scan file written as one scan block with no setup transform."},
    {PointCloudExportFormat::Las, "LAS", "las", "ASPRS LAS binary point file, uncompressed."},
    {PointCloudExportFormat::Laz, "LAZ", "laz", "Compressed LAS (LASzip). Smaller files, same points."},
};

QString CountText(qint64 count) { return QLocale().toString(count); }

const char* kErrorStyle = "color:#ff8f8f;";
const char* kNoteStyle = "color:#9fb0b8;";
// One stylesheet per dialog (set on the dialog, not its children, so a restyle replaces it whole):
// graphite surface with amber accents (the look chosen for #916), a raised card for the file facts, a visible bar.
const char* kDialogStyle =
    "QDialog { background:#303940; color:#f4f3ef; } QLabel { color:#f4f3ef; }"
    "QLineEdit,QComboBox { background:#3c464f; color:#f4f3ef; border:1px solid #64717b; padding:4px; }"
    "QPushButton { background:#3c464f; color:#f4f3ef; border:1px solid #64717b; padding:6px 14px; border-radius:4px; }"
    "QPushButton:default { background:#e6a64b; color:#202b35; font-weight:700; border:0; }"
    "QPushButton:default:disabled { background:#3c464f; color:#7d878d; border:1px solid #64717b; font-weight:400; }"
    "QFrame#pointCloudCard { background:#3a444c; border:1px solid #64717b; border-left:3px solid #e6a64b; border-radius:6px; }"
    "QProgressBar { min-height:14px; max-height:14px; background:#3c464f; border:1px solid #64717b; border-radius:7px; }"
    "QProgressBar::chunk { background:#e6a64b; border-radius:6px; }";
}  // namespace

void EditorWindow::ShowPointCloudAttach() {
  if (m_ModalOpen) return;
  if (m_State.pointCloudImportAsync) {
    QMessageBox::information(this, "Attach Point Cloud", "A point cloud is still being imported. Wait for it to finish, or cancel it, before attaching another.");
    return;
  }
  if (m_State.activeDrawingIdx == 0) NewDrawing();
  if (ActiveDrawingReadOnly()) {
    QMessageBox::information(this, "Attach Point Cloud", "This drawing is read-only. No point cloud was attached.");
    return;
  }

  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("pointCloudAttachDialog");
  dialog.setStyleSheet(kDialogStyle);
  dialog.setWindowTitle("Attach Point Cloud");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.setMinimumWidth(560);
  auto* root = new QVBoxLayout(&dialog);
  root->setSpacing(10);
  auto* intro = new QLabel("Attach a laser scan to this drawing as reference geometry. GoSurvey indexes large scans in the background so the drawing stays responsive.", &dialog);
  intro->setObjectName("pointCloudAttachIntro");
  intro->setWordWrap(true);
  root->addWidget(intro);

  auto* pathRow = new QHBoxLayout;
  auto* path = new QLineEdit(&dialog);
  path->setObjectName("pointCloudAttachPath");
  path->setPlaceholderText("E57, LAS, LAZ, PTS or PTX file");
  auto* browse = new QPushButton("Browse...", &dialog);
  browse->setObjectName("pointCloudAttachBrowse");
  pathRow->addWidget(path, 1);
  pathRow->addWidget(browse);
  root->addLayout(pathRow);

  auto* card = new QFrame(&dialog);
  card->setObjectName("pointCloudCard");
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(12, 8, 12, 8);
  auto* details = new QLabel("Choose a scan to see its format, size and point count.", card);
  details->setObjectName("pointCloudAttachDetails");
  details->setWordWrap(true);
  details->setTextFormat(Qt::RichText);
  cardLayout->addWidget(details);
  root->addWidget(card);
  auto* status = new QLabel(&dialog);
  status->setObjectName("pointCloudAttachStatus");
  status->setWordWrap(true);
  root->addWidget(status);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* attach = buttons->addButton("Attach", QDialogButtonBox::AcceptRole);
  attach->setObjectName("pointCloudAttachConfirm");
  attach->setDefault(true);
  attach->setEnabled(false);
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName("pointCloudAttachCancel");
  root->addWidget(buttons);

  auto validate = [&] {
    const ScanFileCheck check = CheckScanFile(path->text());
    attach->setEnabled(check.ok);
    if (check.ok) {
      const QString points = check.points > 0 ? QString("about %1 points").arg(CountText(check.points)) : QString("point count not known until it is read");
      details->setText(QString("<b>%1</b> &nbsp;·&nbsp; %2 &nbsp;·&nbsp; %3").arg(check.format.toHtmlEscaped(), FormatSize(check.bytes), points));
      status->setText("Ready to attach. You can cancel while it imports.");
      status->setStyleSheet(kNoteStyle);
    } else {
      details->setText("Choose a scan to see its format, size and point count.");
      status->setText(check.error);
      status->setStyleSheet(check.error.isEmpty() ? QString() : QString(kErrorStyle));
    }
  };
  connect(path, &QLineEdit::textChanged, &dialog, validate);
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const QString selected = QFileDialog::getOpenFileName(&dialog, "Select point cloud", path->text(),
        "Point clouds (*.e57 *.las *.laz *.pts *.ptx);;E57 (*.e57);;LAS (*.las);;LAZ (*.laz);;PTS (*.pts);;PTX (*.ptx);;All files (*)");
    if (!selected.isEmpty()) path->setText(QDir::toNativeSeparators(selected));
  });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(attach, &QPushButton::clicked, &dialog, [&] {
    // Re-checked at the moment of attaching: the file may have changed since it was typed.
    const ScanFileCheck check = CheckScanFile(path->text());
    if (!check.ok) { validate(); return; }
    const std::string utf8 = QFileInfo(path->text().trimmed()).absoluteFilePath().toUtf8().toStdString();
    const std::size_t logged = m_Log.size();
    // A project drawing asks Copy / Link / Cancel first for a file outside the project; the answer
    // starts the import (shared `CompleteProjectAttach`).
    if (RequestProjectAttach(m_State, Prompt::Kind::PointCloud, utf8, m_Log)) { dialog.accept(); return; }
    if (m_Log.size() > logged) {
      status->setText(QString::fromUtf8(m_Log.back().c_str()));
      status->setStyleSheet(kErrorStyle);
      return;
    }
    if (!StartPointCloudImportAsync(m_State, utf8, m_Log)) {
      status->setText(QString::fromUtf8(m_Log.back().c_str()));
      status->setStyleSheet(kErrorStyle);
      return;
    }
    dialog.accept();
  });
  dialog.exec();
  m_ModalOpen = false;
  Refresh();
}

void EditorWindow::ShowPointCloudExport(int formatIndex) {
  if (m_ModalOpen) return;
  if (m_State.activeDrawingIdx == 0 || m_State.cadPointClouds.empty()) {
    QMessageBox::information(this, "Export Point Cloud", "The drawing has no point clouds. Attach one first.");
    return;
  }

  m_ModalOpen = true;
  QDialog dialog(this);
  dialog.setObjectName("pointCloudExportDialog");
  dialog.setStyleSheet(kDialogStyle);
  dialog.setWindowTitle("Export Point Cloud");
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.setMinimumWidth(560);
  auto* root = new QVBoxLayout(&dialog);
  root->setSpacing(10);
  auto* card = new QFrame(&dialog);
  card->setObjectName("pointCloudCard");
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(12, 8, 12, 8);
  auto* summary = new QLabel(card);
  summary->setObjectName("pointCloudExportSummary");
  summary->setWordWrap(true);
  cardLayout->addWidget(summary);
  root->addWidget(card);

  auto* formatRow = new QHBoxLayout;
  formatRow->addWidget(new QLabel("Format", &dialog));
  auto* format = new QComboBox(&dialog);
  format->setObjectName("pointCloudExportFormat");
  for (const auto& row : kExportFormats) format->addItem(QString("%1 (*.%2)").arg(row.name, row.extension));
  format->setCurrentIndex(std::clamp(formatIndex, 0, static_cast<int>(std::size(kExportFormats)) - 1));
  formatRow->addWidget(format, 1);
  root->addLayout(formatRow);
  auto* description = new QLabel(&dialog);
  description->setObjectName("pointCloudExportDescription");
  description->setWordWrap(true);
  description->setStyleSheet(kNoteStyle);
  root->addWidget(description);

  auto* pathRow = new QHBoxLayout;
  auto* path = new QLineEdit(&dialog);
  path->setObjectName("pointCloudExportPath");
  auto* browse = new QPushButton("Browse...", &dialog);
  browse->setObjectName("pointCloudExportBrowse");
  pathRow->addWidget(path, 1);
  pathRow->addWidget(browse);
  root->addLayout(pathRow);
  auto* note = new QLabel("E57 scans can be attached but not exported.", &dialog);
  note->setObjectName("pointCloudExportNote");
  note->setStyleSheet(kNoteStyle);
  root->addWidget(note);
  auto* status = new QLabel(&dialog);
  status->setObjectName("pointCloudExportStatus");
  status->setWordWrap(true);
  root->addWidget(status);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* exportButton = buttons->addButton("Export", QDialogButtonBox::AcceptRole);
  exportButton->setObjectName("pointCloudExportConfirm");
  exportButton->setDefault(true);
  auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setObjectName("pointCloudExportCancel");
  root->addWidget(buttons);

  // The folder of the first cloud that will be exported, else Documents.
  auto defaultFolder = [&] {
    for (const auto& item : m_State.selection)
      if (item.type == SelectedEntity::Type::PointCloud && item.index >= 0 && item.index < static_cast<int>(m_State.cadPointClouds.size())) {
        const QFileInfo source(QString::fromUtf8(m_State.cadPointClouds[static_cast<std::size_t>(item.index)]->sourcePath.c_str()));
        if (source.dir().exists()) return source.absolutePath();
        break;
      }
    const QFileInfo source(QString::fromUtf8(m_State.cadPointClouds.front()->sourcePath.c_str()));
    return source.dir().exists() ? source.absolutePath() : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  };
  const QString folder = defaultFolder();
  auto currentFormat = [&]() -> const ExportFormatRow& { return kExportFormats[static_cast<std::size_t>(format->currentIndex())]; };
  auto refresh = [&] {
    const auto& row = currentFormat();
    const PointCloudExportPlan plan = PlanPointCloudExport(m_State, row.format);
    summary->setText(plan.fromSelection
        ? QString("Exporting the %1 selected point cloud%2 (about %3 points) into one file.").arg(plan.cloudCount).arg(plan.cloudCount == 1 ? "" : "s").arg(CountText(plan.pointCount))
        : QString("No point cloud is selected, so all %1 in this drawing (about %2 points) will be exported into one file.").arg(plan.cloudCount).arg(CountText(plan.pointCount)));
    description->setText(row.description);
    path->setText(QDir::toNativeSeparators(QDir(folder).filePath(QString::fromUtf8(plan.defaultFileName.c_str()))));
    status->clear();
  };
  refresh();
  connect(format, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { refresh(); });
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto& row = currentFormat();
    const QString selected = QFileDialog::getSaveFileName(&dialog, "Export point cloud", path->text(),
        QString("%1 (*.%2)").arg(row.name, row.extension));
    if (!selected.isEmpty()) path->setText(QDir::toNativeSeparators(selected));
  });
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QString result;
  connect(exportButton, &QPushButton::clicked, &dialog, [&] {
    const auto& row = currentFormat();
    QString target = path->text().trimmed();
    if (target.isEmpty()) { status->setText("Choose where to save the file."); status->setStyleSheet(kErrorStyle); return; }
    if (QFileInfo(target).suffix().compare(row.extension, Qt::CaseInsensitive) != 0) target += QString(".") + row.extension;
    const QFileInfo info(target);
    if (!info.absoluteDir().exists()) { status->setText("That folder does not exist."); status->setStyleSheet(kErrorStyle); return; }
    if (info.isDir()) { status->setText("That path is a folder. Enter a file name."); status->setStyleSheet(kErrorStyle); return; }
    if (info.exists() && QMessageBox::question(&dialog, "Export Point Cloud",
            QString("%1 already exists. Replace it?").arg(info.fileName()), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
      return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool wrote = ExportPointCloudsToFile(m_State, row.format, info.absoluteFilePath().toUtf8().toStdString(), m_Log);
    QApplication::restoreOverrideCursor();
    const QString message = QString::fromUtf8(m_Log.back().c_str());
    if (!wrote) { status->setText(message); status->setStyleSheet(kErrorStyle); return; }
    result = message;
    dialog.accept();
  });
  dialog.exec();
  m_ModalOpen = false;
  if (!result.isEmpty()) QMessageBox::information(this, "Export Point Cloud", result);
  Refresh();
}

// Called every service tick. Reaps the background import (shared `TickPointCloudImport`), shows a
// progress window with Cancel while one runs, and reports a failed import. A cancelled one is not a
// failure: the user asked for it, so the log line is enough.
void EditorWindow::ServicePointCloudImport() {
  QString failure;
  if (const auto* job = m_State.pointCloudImportAsync.get();
      job && job->done.load(std::memory_order_acquire) && !job->ok && !job->cancelRequested.load(std::memory_order_acquire))
    failure = QString::fromUtf8(job->errorMessage.c_str());
  TickPointCloudImport(m_State, m_Log);

  auto* progressDialog = findChild<QDialog*>("pointCloudImportProgress");
  auto* job = m_State.pointCloudImportAsync.get();
  if (!job) {
    if (progressDialog) progressDialog->deleteLater();
  } else {
    if (!progressDialog) {
      progressDialog = new NonDismissableDialog(this);
      progressDialog->setObjectName("pointCloudImportProgress");
      progressDialog->setStyleSheet(kDialogStyle);
      progressDialog->setWindowTitle("Importing Point Cloud");
      progressDialog->setWindowModality(Qt::WindowModal);
      progressDialog->setMinimumWidth(460);
      auto* layout = new QVBoxLayout(progressDialog);
      layout->setSpacing(8);
      auto* file = new QLabel(progressDialog);
      file->setObjectName("pointCloudImportFile");
      file->setText(QFileInfo(QString::fromUtf8(job->sourcePath.c_str())).fileName());
      layout->addWidget(file);
      auto* phase = new QLabel(progressDialog);
      phase->setObjectName("pointCloudImportPhase");
      layout->addWidget(phase);
      auto* bar = new QProgressBar(progressDialog);
      bar->setObjectName("pointCloudImportBar");
      bar->setTextVisible(false);
      layout->addWidget(bar);
      auto* eta = new QLabel(progressDialog);
      eta->setObjectName("pointCloudImportEta");
      eta->setStyleSheet(kNoteStyle);
      layout->addWidget(eta);
      auto* cancel = new QPushButton("Cancel", progressDialog);
      cancel->setObjectName("pointCloudImportCancel");
      layout->addWidget(cancel, 0, Qt::AlignRight);
      connect(cancel, &QPushButton::clicked, progressDialog, [this, cancel] {
        if (m_State.pointCloudImportAsync) m_State.pointCloudImportAsync->cancelRequested.store(true, std::memory_order_release);
        cancel->setEnabled(false);
        cancel->setText("Cancelling...");
      });
      progressDialog->show();
    }
    const PointCloudImportProgress progress = UpdatePointCloudImportProgress(*job);
    progressDialog->findChild<QLabel*>("pointCloudImportPhase")->setText(QString::fromUtf8(progress.phase));
    auto* bar = progressDialog->findChild<QProgressBar*>("pointCloudImportBar");
    if (progress.fraction < 0.f) bar->setRange(0, 0);
    else { bar->setRange(0, 1000); bar->setValue(static_cast<int>(progress.fraction * 1000.f)); }
    progressDialog->findChild<QLabel*>("pointCloudImportEta")->setText(QString::fromUtf8(progress.eta.c_str()));
  }
  if (!failure.isEmpty()) {
    QMessageBox::warning(this, "Attach Point Cloud", failure);
  }
}
