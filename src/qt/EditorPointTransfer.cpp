#include "EditorWindow.hpp"
#include "SurveyCsv.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QTabWidget>
#include <QSaveFile>
#include <QApplication>
#include <QClipboard>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdio>

void EditorWindow::ShowSurveyReports() {
  m_State.surveyReportSelectLatestPending = false;
  auto* dock = findChild<QDockWidget*>("surveyReports");
  if (!dock) {
    dock = new QDockWidget("Survey reports", this); dock->setObjectName("surveyReports");
    auto* body = new QWidget(dock); auto* layout = new QVBoxLayout(body);
    auto* tabs = new QTabWidget(body); tabs->setObjectName("surveyReportTabs"); layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(body);
    auto* save = buttons->addButton("Save report...", QDialogButtonBox::ActionRole); save->setObjectName("saveSurveyReport");
    auto* copy = buttons->addButton("Copy report", QDialogButtonBox::ActionRole); copy->setObjectName("copySurveyReport");
    layout->addWidget(buttons); dock->setWidget(body); addDockWidget(Qt::BottomDockWidgetArea, dock);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) { m_State.surveyReportSelectedTab = index; });
    connect(copy, &QPushButton::clicked, this, [tabs] {
      if (auto* page = qobject_cast<QPlainTextEdit*>(tabs->currentWidget())) QApplication::clipboard()->setText(page->toPlainText());
    });
    connect(save, &QPushButton::clicked, this, [this, tabs] {
      auto* page = qobject_cast<QPlainTextEdit*>(tabs->currentWidget()); if (!page) return;
      const auto path = QFileDialog::getSaveFileName(this, "Save survey report", tabs->tabText(tabs->currentIndex()), "Text and CSV (*.txt *.csv);;All files (*)");
      if (path.isEmpty()) return;
      QSaveFile file(path); const auto bytes = page->toPlainText().toUtf8();
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, "Save survey report", file.errorString());
    });
  }
  auto* tabs = dock->findChild<QTabWidget*>("surveyReportTabs");
  const QSignalBlocker block(tabs);
  while (tabs->count()) { auto* page = tabs->widget(0); tabs->removeTab(0); delete page; }
  for (const auto& report : m_State.surveyReportTabs) {
    auto* page = new QPlainTextEdit(tabs); page->setReadOnly(true); page->setLineWrapMode(QPlainTextEdit::NoWrap);
    page->setPlainText(QString::fromUtf8(report.second.c_str())); tabs->addTab(page, QString::fromUtf8(report.first.c_str()));
  }
  tabs->setCurrentIndex(m_State.surveyReportSelectedTab); dock->show(); dock->raise();
}

void EditorWindow::ShowPointTransfer(bool importing) {
  m_ModalOpen = true;
  QDialog dialog(this); dialog.setObjectName(importing ? "importSurveyPoints" : "exportSurveyPoints");
  dialog.setWindowTitle(importing ? "Import Survey Points" : "Export Survey Points"); dialog.resize(850, 650);
  auto* layout = new QVBoxLayout(&dialog);
  auto* fields = new QFormLayout;
  auto* fileRow = new QHBoxLayout;
  auto* path = new QLineEdit(QString::fromUtf8(importing ? m_State.surveyImportCsvPath : m_State.surveyExportCsvPath), &dialog);
  path->setObjectName("pointFilePath"); fileRow->addWidget(path);
  auto* browse = new QPushButton("Browse…", &dialog); fileRow->addWidget(browse); fields->addRow("CSV / TXT file", fileRow);
  auto* order = new QComboBox(&dialog); order->setObjectName("pointColumnOrder");
  order->addItems({"P, N, E, Z, D", "P, E, N, Z, D", "N, E, Z (assign point numbers)", "E, N, Z (assign point numbers)"});
  order->setCurrentIndex(importing ? m_State.surveyImportCsvLayoutIdx : m_State.surveyExportCsvLayoutIdx);
  auto* header = new QCheckBox(importing ? "Skip first row (header)" : "Write header row", &dialog); header->setObjectName("pointFileHeader");
  header->setChecked(importing ? m_State.surveyImportCsvSkipFirstRow : m_State.surveyExportCsvWriteHeader);
  fields->addRow("Column order", order); fields->addRow(header); layout->addLayout(fields);
  auto* summary = new QPlainTextEdit(&dialog); summary->setObjectName("pointFileValidation"); summary->setReadOnly(true); summary->setMaximumHeight(135);
  layout->addWidget(summary);
  auto* preview = new QPlainTextEdit(&dialog); preview->setObjectName("pointFilePreview"); preview->setReadOnly(true); preview->setLineWrapMode(QPlainTextEdit::NoWrap);
  layout->addWidget(preview, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto* apply = buttons->addButton(importing ? "Import" : "Export", QDialogButtonBox::ActionRole); apply->setObjectName("applyPointFile");
  auto* reload = buttons->addButton("Refresh preview", QDialogButtonBox::ActionRole); reload->setVisible(importing);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto readOnly = [&] {
    if (m_State.activeDrawingIdx <= 0 || m_State.activeDrawingIdx >= static_cast<int>(m_State.drawingTabs.size())) return true;
    const auto uid = m_State.drawingTabs[m_State.activeDrawingIdx].projectUid;
    return std::any_of(m_State.openProjects.begin(), m_State.openProjects.end(), [uid](const auto& p) { return p.uid == uid && p.readOnly; });
  };
  auto sync = [&] {
    const auto bytes = path->text().toUtf8();
    const auto capacity = importing ? sizeof(m_State.surveyImportCsvPath) : sizeof(m_State.surveyExportCsvPath);
    if (bytes.size() >= static_cast<qsizetype>(capacity)) {
      summary->setPlainText("The file path is too long for the point-file service."); apply->setEnabled(false); return false;
    }
    std::snprintf(importing ? m_State.surveyImportCsvPath : m_State.surveyExportCsvPath, capacity, "%s", bytes.constData());
    if (importing) { m_State.surveyImportCsvLayoutIdx = order->currentIndex(); m_State.surveyImportCsvSkipFirstRow = header->isChecked(); }
    else { m_State.surveyExportCsvLayoutIdx = order->currentIndex(); m_State.surveyExportCsvWriteHeader = header->isChecked(); }
    return true;
  };
  auto refresh = [&] {
    if (!sync()) return;
    if (importing) {
      SurveyCsvRefreshImportPreview(m_State);
      summary->setPlainText(QString::fromUtf8(m_State.surveyImportPreviewValidation.c_str()));
      preview->setPlainText(QString::fromUtf8(m_State.surveyImportPreviewText.c_str()));
      apply->setEnabled(!readOnly() && !m_State.blockEditActive && !m_State.surveyImportFileBlocked);
      if (readOnly()) summary->appendPlainText("This project is read-only. Import is disabled.");
    } else {
      summary->setPlainText(QString("Export %1 drawing point(s) in world coordinates.").arg(m_State.surveyPoints.size()));
      apply->setEnabled(!path->text().trimmed().isEmpty());
    }
  };
  auto* debounce = new QTimer(&dialog); debounce->setSingleShot(true); debounce->setInterval(200);
  connect(debounce, &QTimer::timeout, &dialog, refresh);
  connect(path, &QLineEdit::textChanged, &dialog, [&] { apply->setEnabled(false); debounce->start(); });
  connect(order, &QComboBox::currentIndexChanged, &dialog, refresh);
  connect(header, &QCheckBox::toggled, &dialog, refresh);
  connect(reload, &QPushButton::clicked, &dialog, refresh);
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto file = importing ? QFileDialog::getOpenFileName(&dialog, "Import survey points", path->text(), "Point files (*.csv *.txt);;All files (*)") :
        QFileDialog::getSaveFileName(&dialog, "Export survey points", path->text(), "Point files (*.csv *.txt);;All files (*)");
    if (!file.isEmpty()) { path->setText(file); debounce->stop(); refresh(); }
  });
  connect(apply, &QPushButton::clicked, &dialog, [&] {
    debounce->stop();
    if (importing) {
      refresh();
      if (!apply->isEnabled()) return;
      if (m_State.surveyImportBadRowCount && QMessageBox::question(&dialog, "Import valid rows", QString("Import %1 valid row(s) and skip %2 invalid or duplicate row(s)?")
          .arg(m_State.surveyImportValidRowCount).arg(m_State.surveyImportBadRowCount), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
      const bool imported = SurveyCsvImportFile(m_State, m_Log);
      summary->setPlainText(QString::fromUtf8(m_State.surveyImportPreviewValidation.c_str())); apply->setEnabled(false);
      if (!imported) summary->appendPlainText("Import did not complete. Check the command history for details.");
    } else {
      if (!sync()) return;
      if (QFileInfo::exists(path->text()) && QMessageBox::question(&dialog, "Replace point file", "Replace the existing point file?", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
      if (SurveyCsvExportFile(m_State, m_Log)) {
        summary->setPlainText("Export completed.");
        preview->setPlainText(QString::fromUtf8(m_State.surveyReportTabs.back().second.c_str()));
      } else QMessageBox::warning(&dialog, "Export survey points", "The export failed. Check the command history for details.");
    }
    Refresh();
  });
  refresh(); dialog.exec(); m_ModalOpen = false; Refresh();
}
