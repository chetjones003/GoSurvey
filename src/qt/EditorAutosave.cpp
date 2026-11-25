// Drawing Recovery modal dialog and formatting helpers for GoSurveyEditor.

#include "EditorAutosave.hpp"

#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <string_view>

namespace {

QString FormatTimestamp(std::int64_t unixSec) {
  if (unixSec <= 0)
    return "Unknown time";
  const QDateTime dt = QDateTime::fromSecsSinceEpoch(unixSec).toLocalTime();
  return dt.toString("yyyy-MM-dd hh:mm:ss");
}

QString FormatBytes(std::uint64_t bytes) {
  if (bytes < 1024)
    return QString("%1 B").arg(bytes);
  if (bytes < 1024 * 1024)
    return QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  return QString("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

}  // namespace

std::string MakeRecoveredTabName(const std::string& rawName) {
  std::string base = rawName.empty() ? std::string("Drawing") : rawName;
  constexpr std::string_view kSuffix = " (Recovered)";
  if (base.size() >= kSuffix.size() &&
      base.compare(base.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
    return base;
  return base + std::string(kSuffix);
}

AutosaveRecoveryChoice RunAutosaveRecoveryDialog(QWidget* parent,
                                                 const std::filesystem::path& autosaveRoot,
                                                 const std::vector<autosave::RecoveryEntry>& entries,
                                                 bool userInitiated) {
  QDialog dialog(parent);
  dialog.setObjectName("autosaveRecoveryDialog");
  dialog.setWindowTitle("Drawing Recovery");
  dialog.setMinimumSize(680, 380);

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(20, 18, 20, 16);
  layout->setSpacing(12);

  auto* heading = new QLabel(
      userInitiated ? "Autosaved drawings available for recovery"
                    : "GoSurvey closed unexpectedly",
      &dialog);
  heading->setObjectName("autosaveRecoveryHeading");
  QFont headingFont = heading->font();
  headingFont.setPointSize(14);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  layout->addWidget(heading);

  auto* intro = new QLabel(
      "The following unsaved drawings were autosaved before the previous session ended. "
      "Select the drawings you want to restore. Recovered drawings open as unsaved copies so "
      "your original .dwg files are never overwritten without confirmation.",
      &dialog);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto* table = new QTableWidget(static_cast<int>(entries.size()), 4, &dialog);
  table->setObjectName("autosaveRecoveryTable");
  table->setHorizontalHeaderLabels({"Drawing", "Original File", "Autosaved", "Notes"});
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setStretchLastSection(true);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

  for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
    const auto& rec = entries[static_cast<std::size_t>(row)];
    auto* nameItem = new QTableWidgetItem(QString::fromUtf8(rec.meta.tabName.c_str()));
    nameItem->setFlags((nameItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    nameItem->setCheckState(Qt::Checked);
    table->setItem(row, 0, nameItem);

    const QString orig = rec.meta.originalFilePath.empty()
                             ? QString("Unsaved drawing")
                             : QString::fromUtf8(rec.meta.originalFilePath.c_str());
    auto* origItem = new QTableWidgetItem(orig);
    origItem->setToolTip(orig);
    table->setItem(row, 1, origItem);

    table->setItem(row, 2, new QTableWidgetItem(FormatTimestamp(rec.meta.savedAtUnix)));

    QString note = FormatBytes(rec.payloadSizeBytes);
    if (rec.originalFileNewer)
      note += " — Original file is newer";
    auto* noteItem = new QTableWidgetItem(note);
    if (rec.originalFileNewer)
      noteItem->setForeground(QColor("#e0a458"));
    table->setItem(row, 3, noteItem);
  }
  layout->addWidget(table, 1);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(8);

  auto* openFolderBtn = new QPushButton("Open Autosave Folder", &dialog);
  openFolderBtn->setObjectName("autosaveOpenFolderButton");
  QObject::connect(openFolderBtn, &QPushButton::clicked, &dialog, [autosaveRoot] {
    std::error_code ec;
    std::filesystem::create_directories(autosaveRoot, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(autosaveRoot.wstring())));
  });
  buttonRow->addWidget(openFolderBtn);

  auto* discardAllBtn = new QPushButton("Discard All", &dialog);
  discardAllBtn->setObjectName("autosaveDiscardAllButton");
  buttonRow->addWidget(discardAllBtn);

  buttonRow->addStretch();

  auto* laterBtn = new QPushButton("Decide Later", &dialog);
  laterBtn->setObjectName("autosaveLaterButton");
  buttonRow->addWidget(laterBtn);

  auto* recoverBtn = new QPushButton("Recover Selected", &dialog);
  recoverBtn->setObjectName("autosaveRecoverButton");
  recoverBtn->setDefault(true);
  recoverBtn->setStyleSheet(
      "QPushButton { background:palette(highlight); color:palette(highlighted-text); font-weight:600; "
      "padding:6px 16px; border:0; border-radius:4px; }"
      "QPushButton:disabled { background:palette(mid); color:palette(window-text); }");
  buttonRow->addWidget(recoverBtn);
  layout->addLayout(buttonRow);

  auto updateRecoverEnabled = [table, recoverBtn] {
    bool anyChecked = false;
    for (int r = 0; r < table->rowCount(); ++r) {
      if (auto* item = table->item(r, 0); item && item->checkState() == Qt::Checked) {
        anyChecked = true;
        break;
      }
    }
    recoverBtn->setEnabled(anyChecked);
  };
  QObject::connect(table, &QTableWidget::itemChanged, &dialog,
                   [updateRecoverEnabled](QTableWidgetItem*) { updateRecoverEnabled(); });
  updateRecoverEnabled();

  AutosaveRecoveryChoice result;

  QObject::connect(recoverBtn, &QPushButton::clicked, &dialog, [&] {
    result.selectedRows.clear();
    for (int r = 0; r < table->rowCount(); ++r) {
      if (auto* item = table->item(r, 0); item && item->checkState() == Qt::Checked)
        result.selectedRows.push_back(r);
    }
    result.action = AutosaveRecoveryChoice::Action::Recover;
    dialog.accept();
  });
  QObject::connect(discardAllBtn, &QPushButton::clicked, &dialog, [&] {
    const auto answer = QMessageBox::warning(
        &dialog, "Discard recovered drawings?",
        "Permanently delete all autosaved recovery files? Unsaved changes in them cannot be restored afterwards.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
      result.action = AutosaveRecoveryChoice::Action::DiscardAll;
      dialog.accept();
    }
  });
  QObject::connect(laterBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

  dialog.exec();
  return result;
}
