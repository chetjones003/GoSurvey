#include "AdjustmentWindow.hpp"
#include "EditorWindow.hpp"
#include "TraverseActions.hpp"
#include "QtText.hpp"
#include "ProjectFiles.hpp"
#include "traverse/TraverseIo.hpp"
#include "traverse/TraverseReport.hpp"
#include "traverse/TraverseCommandLang.hpp"
#include "traverse/FbkImport.hpp"
#include "traverse/StarNetImport.hpp"
#include "traverse/Rw5Import.hpp"
#include "traverse/JobXmlImport.hpp"
#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QTimer>
#include <QCloseEvent>
#include <QMenuBar>
#include <QStatusBar>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QColor>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

AdjustmentWindow::AdjustmentWindow(AppCommandState& state, std::vector<std::string>& log, QWidget* parent)
    : QMainWindow(parent, Qt::Window), m_State(state), m_Log(log),
      m_Data(state.traverseData), m_Weights(state.traverseLsaWeights) {
  m_State.measureAnnotationText = MeasureQtText;
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("GoSurvey | Least-squares adjustment[*]");
  resize(1200, 800);
  auto* toolbar = addToolBar("Adjustment");
  toolbar->setMovable(false);
  toolbar->addAction(QIcon(QtAsset("icons/Open.png")), "Import observations...", this, [this] { Import(); });
  toolbar->addAction(QIcon(QtAsset("icons/Save.png")), "Save", this, [this] { Save(); });
  toolbar->addAction("Settings...", this, [this] { EditWeights(); });
  toolbar->addSeparator();
  m_RunAction = toolbar->addAction(QIcon(QtAsset("icons/Adjust.png")), "Run adjustment", this, [this] { Calculate(); });
  m_RunAction->setShortcut(QKeySequence(Qt::Key_F5));
  m_AcceptAction = toolbar->addAction("Accept result", this, [this] {
    QString error; if (!AcceptResult(error)) QMessageBox::warning(this, "Accept result", error);
  });
  m_CommitAction = toolbar->addAction("Commit to drawing", this, [this] { CommitToDrawing(); });
  auto* file = menuBar()->addMenu("&File");
  file->addAction("Open / Import...", QKeySequence::Open, this, [this] { Import(); });
  file->addAction("Save", QKeySequence::Save, this, [this] { Save(); });
  file->addAction("Save As...", QKeySequence::SaveAs, this, [this] { Save(true); });
  file->addAction("Export report...", this, [this] { ExportReport(); });
  auto* workspace = new QSplitter(Qt::Horizontal, this);
  auto* filesPanel = new QWidget(workspace);
  auto* filesLayout = new QVBoxLayout(filesPanel);
  filesLayout->setContentsMargins(12, 12, 12, 12);
  auto* filesHeading = new QLabel("Files", filesPanel);
  QFont headingFont = filesHeading->font(); headingFont.setBold(true); filesHeading->setFont(headingFont);
  filesLayout->addWidget(filesHeading);
  auto* filesHelp = new QLabel("Imported source files stay listed here. Choose one or more field files to append to this traverse.", filesPanel);
  filesHelp->setWordWrap(true);
  filesLayout->addWidget(filesHelp);
  auto* importFilesButton = new QPushButton("Import raw files…", filesPanel);
  importFilesButton->setObjectName("issue913ImportRawFiles");
  connect(importFilesButton, &QPushButton::clicked, this, [this] {
    if (IsBusy()) return;
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Import raw traverse files", {},
        "Raw traverse files (*.fbk *.dat *.rw5 *.jxl *.xml);;All files (*)");
    if (paths.isEmpty()) return;
    QStringList failures;
    const int imported = ImportFiles(paths, failures);
    if (imported == 0 && !failures.isEmpty())
      QMessageBox::warning(this, "Import raw files", failures.join("\n"));
    else if (!failures.isEmpty())
      statusBar()->showMessage(QString("Imported %1 file(s); %2 failed. See Files for details.").arg(imported).arg(failures.size()));
  });
  filesLayout->addWidget(importFilesButton);
  m_ImportedFiles = new QListWidget(filesPanel);
  m_ImportedFiles->setObjectName("issue913ImportedFiles");
  m_ImportedFiles->setWordWrap(true);
  filesLayout->addWidget(m_ImportedFiles, 1);
  filesPanel->setMinimumWidth(240);
  RefreshImportedFiles();

  auto* splitter = new QSplitter(Qt::Horizontal, workspace);
  m_Editor = new QPlainTextEdit(splitter);
  m_Editor->setObjectName("adjustmentCommands");
  m_Editor->setFont(QFont("Consolas", 11));
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(state.traverseData, state.traverseLsaWeights).c_str()));
  m_SourceText = m_SavedText = m_Editor->toPlainText();
  m_Editor->setLineWrapMode(QPlainTextEdit::NoWrap);
  auto* results = new QTabWidget(splitter);
  m_Output = new QPlainTextEdit(results);
  m_Output->setReadOnly(true);
  m_Output->setFont(QFont("Consolas", 10));
  m_Output->setPlainText("Import observations or enter GoSurvey traverse commands, then run adjustment.");
  m_Coordinates = new QTableWidget(results);
  m_Coordinates->setColumnCount(4);
  m_Coordinates->setHorizontalHeaderLabels({"Station", "Easting", "Northing", "Elevation"});
  m_Residuals = new QTableWidget(results);
  m_Residuals->setColumnCount(6);
  m_Residuals->setHorizontalHeaderLabels({"Measurement", "From", "To", "Residual", "Std. residual", "Flag"});
  for (auto* table : {m_Coordinates, m_Residuals}) {
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  }
  results->addTab(m_Output, "Output");
  results->addTab(m_Coordinates, "Adjusted coordinates");
  results->addTab(m_Residuals, "Residuals");
  splitter->addWidget(m_Editor);
  splitter->addWidget(results);
  splitter->setSizes({550, 650});
  workspace->addWidget(filesPanel);
  workspace->addWidget(splitter);
  workspace->setChildrenCollapsible(false);
  workspace->setSizes({260, 940});
  setCentralWidget(workspace);
  m_Poll = new QTimer(this);
  // Results are derived and tied to a particular drawing/session. Reopening the
  // editor requires a fresh solve before accepting or committing them.
  m_State.traverseLsaComputed = false;
  m_State.traverseLsaAccepted = false;
  m_Poll->setInterval(20);
  connect(m_Poll, &QTimer::timeout, this, [this] { PollCalculation(); });
  connect(m_Editor, &QPlainTextEdit::textChanged, this, [this] {
    m_State.traverseLsaComputed = false;
    m_State.traverseLsaAccepted = false;
    m_AcceptAction->setEnabled(false); m_CommitAction->setEnabled(false);
    setWindowModified(HasUnsavedChanges());
  });
  SetBusy(false);
}

void AdjustmentWindow::Import() {
  if (IsBusy()) return;
  const QString path = QFileDialog::getOpenFileName(this, "Import observations", {},
      "Survey observations (*.gstrav *.gsdat *.gstc *.fbk *.dat *.rw5 *.jxl *.xml);;All files (*)");
  if (path.isEmpty()) return;
  const QString suffix = QFileInfo(path).suffix().toLower();
  const bool raw = suffix == "fbk" || suffix == "dat" || suffix == "rw5" || suffix == "jxl" || suffix == "xml";
  if (!raw && !ConfirmDiscard()) return;
  QString error;
  if (!LoadFile(path, error)) QMessageBox::warning(this, "Import observations", error);
}

bool AdjustmentWindow::LoadFile(const QString& path, QString& reason) {
  if (IsBusy()) { reason = "An adjustment is running."; return false; }
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == "fbk" || suffix == "dat" || suffix == "rw5" || suffix == "jxl" || suffix == "xml") {
    QStringList failures;
    if (ImportFiles({path}, failures) > 0) return true;
    reason = failures.join("\n");
    return false;
  }
  TraverseData data;
  LsaWeights weights = m_State.traverseLsaWeights;
  std::string error;
  const QByteArray utf8 = path.toUtf8();
  bool ok = false;
  if (suffix == "gstrav") ok = traverseio::Load(utf8.toStdString(), data, weights, error);
  else {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) ok = travcmd::Parse(file.readAll().toStdString(), data, weights, error);
    else error = file.errorString().toUtf8().toStdString();
  }
  if (!ok) { reason = QString::fromUtf8(error.c_str()); return false; }
  m_Data = data; m_Weights = weights;
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(data, weights).c_str()));
  m_SourceText = m_SavedText = m_Editor->toPlainText();
  m_Path = suffix == "gstrav" || suffix == "gstc" || suffix == "gsdat" ? path : QString{};
  m_Imported = m_Path.isEmpty();
  m_Coordinates->setRowCount(0); m_Residuals->setRowCount(0);
  m_Output->setPlainText("Loaded " + path + "\nSave as .gstrav to retain raw measurement sets.");
  RefreshImportedFiles();
  setWindowModified(HasUnsavedChanges());
  return true;
}

int AdjustmentWindow::ImportFiles(const QStringList& paths, QStringList& failures) {
  failures.clear();
  if (paths.isEmpty()) return 0;
  if (IsBusy()) { failures.push_back("An adjustment is running."); return 0; }
  QString parseError;
  if (!ParseEditor(parseError)) { failures.push_back(parseError); return 0; }

  int importedCount = 0;
  std::size_t totalLegs = 0;
  for (const QString& path : paths) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QByteArray utf8 = path.toUtf8();
    TraverseData imported;
    std::string reason;
    bool ok = false;
    const char* format = "raw traverse";
    if (suffix == "fbk") { ok = FbkImport(utf8.constData(), imported, reason); format = "FBK"; }
    else if (suffix == "dat") { ok = StarNetImport(utf8.constData(), imported, reason); format = "STAR*NET"; }
    else if (suffix == "rw5") { ok = Rw5Import(utf8.constData(), imported, reason); format = "RW5"; }
    else if (suffix == "jxl" || suffix == "xml") { ok = JobXmlImport(utf8.constData(), imported, reason); format = "JobXML"; }
    else reason = "Unsupported raw traverse file type.";

    if (!ok) {
      const QString message = QString("%1 — %2").arg(path, QString::fromUtf8(reason.c_str()));
      failures.push_back(message);
      auto* item = new QListWidgetItem("Failed · " + QFileInfo(path).fileName() + " — " + QString::fromUtf8(reason.c_str()), m_ImportedFiles);
      item->setForeground(QColor(176, 55, 48));
      item->setToolTip(message);
      m_Log.push_back("TRAVERSE — " + std::string(format) + " import failed (" + utf8.toStdString() + "): " + reason);
      continue;
    }

    const std::size_t importedLegs = imported.legs.size();
    TraverseAppendImported(m_Data, std::move(imported));
    m_Data.importedFiles.push_back(utf8.toStdString());
    totalLegs += importedLegs;
    ++importedCount;
    auto* item = new QListWidgetItem(QString("Imported · %1 (%2 legs)").arg(QFileInfo(path).fileName()).arg(importedLegs), m_ImportedFiles);
    item->setForeground(QColor(34, 120, 78));
    item->setToolTip(path);
    m_Log.push_back("TRAVERSE — imported " + std::string(format) + " (" + std::to_string(importedLegs) + " legs): " + utf8.toStdString());
  }

  if (importedCount == 0) return 0;
  m_Path.clear();
  m_Imported = true;
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(m_Data, m_Weights).c_str()));
  m_SourceText = m_Editor->toPlainText();
  m_Coordinates->setRowCount(0);
  m_Residuals->setRowCount(0);
  m_Output->setPlainText(QString("Imported %1 file(s), %2 leg(s).\nSave as .gstrav to retain raw measurement sets.")
      .arg(importedCount).arg(totalLegs));
  setWindowModified(HasUnsavedChanges());
  statusBar()->showMessage(QString("Imported %1 raw file(s)%2.").arg(importedCount)
      .arg(failures.isEmpty() ? QString{} : QString("; %1 failed — see Files").arg(failures.size())));
  return importedCount;
}

void AdjustmentWindow::RefreshImportedFiles() {
  m_ImportedFiles->clear();
  for (const std::string& path : m_Data.importedFiles) {
    auto* item = new QListWidgetItem("Imported · " + QFileInfo(QString::fromUtf8(path.c_str())).fileName(), m_ImportedFiles);
    item->setToolTip(QString::fromUtf8(path.c_str()));
  }
}

void AdjustmentWindow::Calculate() {
  if (IsBusy()) return;
  QString error;
  if (!ParseEditor(error)) { m_Output->setPlainText(error); return; }
  m_ResultDrawingUid = m_State.activeDrawingIdx > 0 ? m_State.drawingTabs[m_State.activeDrawingIdx].uid : 0;
  SetBusy(true);
  m_State.traverseLsaComputed = false;
  m_State.traverseLsaAccepted = false;
  m_Work = std::async(std::launch::async, [data = m_Data, weights = m_Weights]() mutable {
    ComputeTraverse(data);
    auto result = ComputeTraverseLeastSquares(data, weights);
    return std::make_pair(std::move(data), std::move(result));
  });
  m_Output->setPlainText("Computing adjustment...");
  m_Poll->start();
}

void AdjustmentWindow::PollCalculation() {
  if (!m_Work.valid() || m_Work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_Poll->stop();
  try {
    auto completed = m_Work.get();
    m_Data = std::move(completed.first);
    m_State.traverseLsaResult = std::move(completed.second);
    m_State.traverseData = m_Data;
    m_State.traverseLsaWeights = m_Weights;
    m_State.traverseLsaComputed = true;
    PresentResult();
  } catch (const std::exception& error) {
    m_State.traverseLsaComputed = false;
    m_Output->setPlainText(QString::fromUtf8(error.what()));
  }
  SetBusy(false);
}

void AdjustmentWindow::PresentResult() {
  const auto& result = m_State.traverseLsaResult;
  m_State.traverseLsaAccepted = false;
  m_Log.push_back(result.message);
  m_Output->setPlainText(QString::fromUtf8(traverserpt::BuildReportText(m_Data, m_Weights, result, "",
      CadDrawingInsUnitsAbbrev(m_State.drawingInsUnits)).c_str()));
  m_Coordinates->setRowCount(static_cast<int>(result.stationIds.size()));
  for (int row = 0; row < static_cast<int>(result.stationIds.size()); ++row) {
    const QStringList cells = {QString::number(result.stationIds[row]),
        QString::number(result.adjEasting[row], 'f', 6), QString::number(result.adjNorthing[row], 'f', 6),
        TraverseAdjustmentHasElevations(result) && row < static_cast<int>(result.adjElevation.size()) ?
          QString::number(result.adjElevation[row], 'f', 6) : "--"};
    for (int col = 0; col < cells.size(); ++col) m_Coordinates->setItem(row, col, new QTableWidgetItem(cells[col]));
  }
  m_Residuals->setRowCount(static_cast<int>(result.measResiduals.size()));
  for (int row = 0; row < static_cast<int>(result.measResiduals.size()); ++row) {
    const auto& residual = result.measResiduals[row];
    const QStringList cells = {QString::fromUtf8(LsaMeasKindName(residual.kind)),
        QString::number(residual.fromStationId), QString::number(residual.toStationId),
        QString::number(residual.residual, 'g', 10), QString::number(residual.stdResidual, 'f', 4),
        residual.flagged ? "> 3 sigma" : ""};
    for (int col = 0; col < cells.size(); ++col) {
      auto* item = new QTableWidgetItem(cells[col]);
      if (residual.flagged) item->setForeground(QColor("#f2ab64"));
      m_Residuals->setItem(row, col, item);
    }
  }
}

void AdjustmentWindow::SaveText() {
  Save(true);
}

AdjustmentWindow::~AdjustmentWindow() {
  if (!m_Work.valid()) return;
  try { m_Work.wait(); }
  catch (const std::exception& error) { qWarning() << "Adjustment worker wait failed during shutdown:" << error.what(); }
}

bool AdjustmentWindow::HasUnsavedChanges() const {
  return m_Imported || m_Editor->toPlainText() != m_SavedText;
}

bool AdjustmentWindow::ParseEditor(QString& error) {
  if (m_Editor->toPlainText() == m_SourceText) return true;
  const auto importedFiles = m_Data.importedFiles;
  TraverseData data;
  LsaWeights weights = m_Weights;
  std::string reason;
  if (!travcmd::Parse(m_Editor->toPlainText().toUtf8().toStdString(), data, weights, reason)) {
    error = QString::fromUtf8(reason.c_str()); return false;
  }
  data.importedFiles = importedFiles;
  m_Data = std::move(data); m_Weights = weights;
  m_SourceText = m_Editor->toPlainText();
  return true;
}

bool AdjustmentWindow::SaveFile(const QString& path, QString& error) {
  if (IsBusy()) { error = "An adjustment is running."; return false; }
  if (!ParseEditor(error)) return false;
  if (QFileInfo(path).suffix().compare("gstrav", Qt::CaseInsensitive) == 0) {
    std::string reason;
    if (!traverseio::Save(path.toUtf8().toStdString(), m_Data, m_Weights, reason)) {
      error = QString::fromUtf8(reason.c_str()); return false;
    }
  } else {
    QSaveFile file(path);
    const QByteArray text = m_Editor->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) {
      error = file.errorString(); return false;
    }
  }
  m_Path = path; m_Imported = false;
  m_State.traverseData = m_Data; m_State.traverseLsaWeights = m_Weights;
  if (QFileInfo(path).suffix().compare("gstrav", Qt::CaseInsensitive) == 0)
    m_State.traverseFilePath = path.toUtf8().toStdString();
  else m_State.traverseGsdatFilePath = path.toUtf8().toStdString();
  m_SavedText = m_Editor->toPlainText(); setWindowModified(false);
  const int tab = m_State.activeDrawingIdx;
  if (tab > 0) {
    const auto uid = m_State.drawingTabs[tab].projectUid;
    for (const auto& project : m_State.openProjects)
      if (project.uid == uid && !project.readOnly && projfiles::IsInsideProject(project.project, std::filesystem::path(path.toStdWString())))
        TrackProjectFile(m_State, uid, path.toUtf8().toStdString(), m_Log);
  }
  statusBar()->showMessage("Saved " + path);
  return true;
}

bool AdjustmentWindow::Save(bool saveAs) {
  if (IsBusy()) return false;
  QString path = m_Path;
  if (saveAs || path.isEmpty()) {
    QString filter = "Traverse session (*.gstrav)";
    path = QFileDialog::getSaveFileName(this, "Save traverse", path,
        "Traverse session (*.gstrav);;Command text (*.gsdat)", &filter);
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += filter.contains("gstrav") ? ".gstrav" : ".gsdat";
  }
  QString error;
  if (!SaveFile(path, error)) { QMessageBox::warning(this, "Save traverse", error); return false; }
  return true;
}

bool AdjustmentWindow::ConfirmDiscard() {
  if (!HasUnsavedChanges()) return true;
  const auto answer = QMessageBox::question(this, "Unsaved traverse", "Save the changes to this traverse?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  return answer == QMessageBox::Discard || (answer == QMessageBox::Save && Save());
}

void AdjustmentWindow::closeEvent(QCloseEvent* event) {
  if (IsBusy()) { statusBar()->showMessage("Wait for the adjustment to finish before closing."); event->ignore(); return; }
  if (!ConfirmDiscard()) { event->ignore(); return; }
  event->accept();
}

void AdjustmentWindow::SetBusy(bool busy) {
  m_Editor->setReadOnly(busy);
  m_RunAction->setEnabled(!busy);
  const bool solved = m_State.traverseLsaComputed && m_State.traverseLsaResult.ok;
  m_AcceptAction->setEnabled(!busy && solved && !m_State.traverseLsaAccepted);
  m_CommitAction->setEnabled(!busy && solved);
}

bool AdjustmentWindow::AcceptResult(QString& error) {
  if (IsBusy() || !m_State.traverseLsaComputed || m_Editor->toPlainText() != m_SourceText) {
    error = "Run an adjustment for the current observations first."; return false;
  }
  std::string reason;
  if (!AcceptTraverseAdjustment(m_Data, m_State.traverseLsaResult, reason)) {
    error = QString::fromUtf8(reason.c_str()); return false;
  }
  m_State.traverseData = m_Data; m_State.traverseLsaAccepted = true;
  m_Log.push_back("TRAVERSE: accepted least-squares adjustment.");
  SetBusy(false);
  statusBar()->showMessage("Adjustment accepted. Commit to drawing to place adjusted stations.");
  return true;
}

int AdjustmentWindow::CommitToDrawing() {
  if (IsBusy() || !m_State.traverseLsaComputed || !m_State.traverseLsaResult.ok || m_Editor->toPlainText() != m_SourceText) return 0;
  const auto activeUid = m_State.activeDrawingIdx > 0 ? m_State.drawingTabs[m_State.activeDrawingIdx].uid : 0;
  if (activeUid != m_ResultDrawingUid) {
    QMessageBox::warning(this, "Commit traverse", "The active drawing changed. Select the original drawing before committing this adjustment.");
    return 0;
  }
  const int count = CommitTraverseToDrawing(m_State, m_Data,
      m_State.traverseLsaAccepted ? &m_State.traverseLsaResult : nullptr, m_Log);
  if (drawingChanged) drawingChanged();
  statusBar()->showMessage(QString("Committed %1 points to the drawing.").arg(count));
  return count;
}

void AdjustmentWindow::ExportReport() {
  if (IsBusy() || !m_State.traverseLsaComputed || !m_State.traverseLsaResult.ok) {
    QMessageBox::information(this, "Export report", "Run a successful adjustment first."); return;
  }
  QString filter = "Text report (*.txt)";
  QString path = QFileDialog::getSaveFileName(this, "Export report", {},
      "Text report (*.txt);;Rich text (*.rtf);;PDF (*.pdf)", &filter);
  if (path.isEmpty()) return;
  if (QFileInfo(path).suffix().isEmpty()) path += filter.contains("pdf") ? ".pdf" : filter.contains("rtf") ? ".rtf" : ".txt";
  const auto report = traverserpt::BuildReportText(m_Data, m_Weights, m_State.traverseLsaResult, "",
      CadDrawingInsUnitsAbbrev(m_State.drawingInsUnits));
  std::string error;
  const auto utf8 = path.toUtf8().toStdString();
  const auto suffix = QFileInfo(path).suffix().toLower();
  const bool ok = suffix == "pdf" ? traverserpt::ExportReportPdf(utf8, report, error) :
      suffix == "rtf" ? traverserpt::ExportReportRtf(utf8, report, error) : traverserpt::ExportReportTxt(utf8, report, error);
  if (!ok) QMessageBox::warning(this, "Export report", QString::fromUtf8(error.c_str()));
}

void AdjustmentWindow::EditWeights() {
  if (IsBusy()) return;
  QString error;
  if (!ParseEditor(error)) { QMessageBox::warning(this, "Settings", error); return; }
  QDialog dialog(this);
  dialog.setWindowTitle("Instrument / Adjustment Settings");
  auto* layout = new QFormLayout(&dialog);
  struct Field { const char* label; double LsaWeights::* member; double minimum; double maximum; };
  const Field fields[] = {{"Angle sigma (arcsec)", &LsaWeights::sigmaAngleSec, 0.000001, 3600},
      {"Distance sigma (drawing units)", &LsaWeights::sigmaDistConstFt, 0.000001, 1000},
      {"Distance sigma (ppm)", &LsaWeights::sigmaDistPpm, 0, 100000},
      {"Vertical sigma (drawing units)", &LsaWeights::sigmaVertConstFt, 0.000001, 1000},
      {"Vertical sigma (ppm)", &LsaWeights::sigmaVertPpm, 0, 100000},
      {"Azimuth sigma (arcsec)", &LsaWeights::sigmaAzimuthSec, 0.000001, 3600},
      {"Zenith sigma (arcsec)", &LsaWeights::sigmaZenithSec, 0.000001, 3600},
      {"Level sigma (drawing units)", &LsaWeights::sigmaLevelFt, 0.000001, 1000},
      {"Instrument centering (drawing units)", &LsaWeights::centeringInstrumentFt, 0, 1000},
      {"Target centering (drawing units)", &LsaWeights::centeringTargetFt, 0, 1000},
      {"Vertical centering (drawing units)", &LsaWeights::centeringVerticalFt, 0, 1000},
      {"Refraction coefficient", &LsaWeights::refractionCoefficient, 0, 1},
      {"Convergence limit", &LsaWeights::convergenceLimit, 1.e-12, 1}};
  std::vector<QDoubleSpinBox*> editors;
  for (const auto& field : fields) {
    auto* editor = new QDoubleSpinBox(&dialog);
    editor->setDecimals(12); editor->setRange(field.minimum, field.maximum); editor->setValue(m_Weights.*field.member);
    layout->addRow(field.label, editor); editors.push_back(editor);
  }
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  for (std::size_t i = 0; i < editors.size(); ++i) m_Weights.*fields[i].member = editors[i]->value();
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(m_Data, m_Weights).c_str()));
  m_SourceText = m_Editor->toPlainText(); m_Imported = true;
  m_State.traverseLsaComputed = false; SetBusy(false); setWindowModified(true);
}

bool AdjustmentWindow::RunSmokeChecks(QString& error) {
  Calculate();
  if (m_Work.valid()) m_Work.wait();
  PollCalculation();
  const auto& result = m_State.traverseLsaResult;
  if (!result.ok || result.adjClosureLinear >= 0.001 || m_Coordinates->rowCount() == 0 || m_Residuals->rowCount() == 0) {
    error = "Qt adjustment editor failed its closed-loop solve: " + QString::fromUtf8(result.message.c_str());
    return false;
  }
  return true;
}
