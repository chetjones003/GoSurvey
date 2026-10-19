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
#include <QLabel>
#include <QApplication>
#include <QVBoxLayout>
#include <QGroupBox>
#include <set>
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
  auto* splitter = new QSplitter(this);
  // REQ-431: left panel (Files, Summary, plan preview) | command editor | results.
  auto* left = new QWidget(splitter);
  left->setObjectName("adjustmentLeftPanel");
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(4, 4, 4, 4);
  auto* files = new QGroupBox("Files", left);
  auto* filesLayout = new QVBoxLayout(files);
  m_Files = new QLabel(files);
  m_Files->setObjectName("adjustmentFiles");
  m_Files->setWordWrap(true);
  m_Files->setTextInteractionFlags(Qt::TextSelectableByMouse);
  filesLayout->addWidget(m_Files);
  auto* summary = new QGroupBox("Summary", left);
  auto* summaryLayout = new QVBoxLayout(summary);
  m_Summary = new QLabel(summary);
  m_Summary->setObjectName("adjustmentSummary");
  m_Summary->setWordWrap(true);
  summaryLayout->addWidget(m_Summary);
  auto* preview = new QGroupBox("Preview", left);
  auto* previewLayout = new QVBoxLayout(preview);
  m_Plan = new AdjustmentPlanView(preview);
  m_Plan->SetUnitLabel(QString::fromUtf8(CadDrawingInsUnitsAbbrev(state.drawingInsUnits)));
  previewLayout->addWidget(m_Plan);
  leftLayout->addWidget(files);
  leftLayout->addWidget(summary);
  leftLayout->addWidget(preview, 1);
  m_Editor = new AdjustmentCodeEdit(splitter);
  m_Editor->setObjectName("adjustmentCommands");
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(state.traverseData, state.traverseLsaWeights).c_str()));
  m_SourceText = m_SavedText = m_Editor->toPlainText();
  auto* results = new QTabWidget(splitter);
  m_Output = new QPlainTextEdit(results);
  m_Output->setObjectName("adjustmentOutput");
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
  splitter->insertWidget(0, left);
  splitter->setSizes({300, 520, 480});
  splitter->setStretchFactor(1, 1);
  setCentralWidget(splitter);
  m_TextStatus = new QLabel(this);
  m_TextStatus->setObjectName("adjustmentTextStatus");
  statusBar()->addPermanentWidget(m_TextStatus, 1);
  m_ValidateTimer = new QTimer(this);
  m_ValidateTimer->setSingleShot(true);
  m_ValidateTimer->setInterval(300);
  connect(m_ValidateTimer, &QTimer::timeout, this, [this] { ValidateNow(); });
  const auto palette = AdjustmentPalette(QApplication::palette().color(QPalette::Window).lightness() < 128);
  m_Editor->SetPalette(palette);
  m_Plan->SetPalette(palette);
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
    m_ValidateTimer->start();  // REQ-431 clause 3: check once the typing pauses
    RefreshPreview();          // any earlier result no longer matches the text
  });
  SetBusy(false);
  ValidateNow();
}

// REQ-431 clause 3: read the text without touching the working traverse, the results or any file.
void AdjustmentWindow::ValidateNow() {
  m_ValidateTimer->stop();
  TraverseData data;
  LsaWeights weights = m_Weights;
  std::string reason;
  if (travcmd::Parse(m_Editor->toPlainText().toUtf8().toStdString(), data, weights, reason)) {
    ComputeTraverse(data);
    m_PreviewData = std::move(data);
    m_Editor->SetError(0, {});
    m_TextStatus->setText("Command text is valid");
    m_TextStatus->setStyleSheet({});
  } else {
    const QString message = QString::fromUtf8(reason.c_str());
    int line = 0;
    QString detail = message;
    if (message.startsWith("line ")) {
      const int colon = static_cast<int>(message.indexOf(':'));
      line = message.mid(5, colon - 5).toInt();
      if (colon > 0) detail = message.mid(colon + 1).trimmed();
    }
    m_Editor->SetError(line, detail);
    m_TextStatus->setText("Parse error \u2014 " + message);
    m_TextStatus->setStyleSheet("color:" + m_Editor->Palette().error.name());
  }
  RefreshPreview();
}

void AdjustmentWindow::RefreshPreview() {
  const bool current = m_State.traverseLsaComputed && m_State.traverseLsaResult.ok;
  m_Plan->SetTraverse(m_PreviewData, current ? &m_State.traverseLsaResult : nullptr);
  RefreshFilesAndSummary();
}

void AdjustmentWindow::RefreshFilesAndSummary() {
  auto path = [](const std::string& value) { return value.empty() ? QString("(unsaved)") : QString::fromUtf8(value.c_str()); };
  m_Files->setText(".gstrav: " + path(m_State.traverseFilePath) + "\n.gsdat: " + path(m_State.traverseGsdatFilePath));
  const auto& data = m_PreviewData;
  std::set<int> ids;  // a loop's last foresight is the start monument, so it counts once
  if (!data.legs.empty() || data.networkStations.empty()) ids.insert(data.startStationId);
  for (const auto& leg : data.legs) ids.insert(leg.stationId);
  for (const auto& station : data.networkStations) ids.insert(station.id);
  const int stations = static_cast<int>(ids.size());
  QStringList lines{QString("Stations: %1").arg(stations)};
  if (data.closureValid) {
    lines << QString("Misclosure: %1").arg(data.closureLinear, 0, 'f', 4);
    lines << (data.closureLinear > 1e-6 ? QString("Precision: 1:%1").arg(data.closurePrecision, 0, 'f', 0)
                                        : QString("Precision: perfect"));
  } else {
    lines << "No closure (open traverse or not computed).";
  }
  const auto& result = m_State.traverseLsaResult;
  if (m_State.traverseLsaComputed && result.ok) {
    lines << QString("Std dev of unit weight: %1").arg(result.refStdDev, 0, 'f', 4);
    lines << QString("Redundancy: %1").arg(result.redundancy);
  } else if (m_State.traverseLsaComputed) {
    lines << "LSA: " + QString::fromUtf8(result.message.c_str());
  } else {
    lines << "Run adjustment for the standard deviation and redundancy.";
  }
  m_Summary->setText(lines.join('\n'));
}

void AdjustmentWindow::Import() {
  if (IsBusy() || !ConfirmDiscard()) return;
  const QString path = QFileDialog::getOpenFileName(this, "Import observations", {},
      "Survey observations (*.gstrav *.gsdat *.gstc *.fbk *.dat *.rw5 *.jxl *.xml);;All files (*)");
  if (path.isEmpty()) return;
  QString error;
  if (!LoadFile(path, error)) QMessageBox::warning(this, "Import observations", error);
}

bool AdjustmentWindow::LoadFile(const QString& path, QString& reason) {
  if (IsBusy()) { reason = "An adjustment is running."; return false; }
  TraverseData data;
  LsaWeights weights = m_State.traverseLsaWeights;
  std::string error;
  const QByteArray utf8 = path.toUtf8();
  const QString suffix = QFileInfo(path).suffix().toLower();
  bool ok = false;
  if (suffix == "gstrav") ok = traverseio::Load(utf8.toStdString(), data, weights, error);
  else if (suffix == "fbk") ok = FbkImport(utf8.constData(), data, error);
  else if (suffix == "dat") ok = StarNetImport(utf8.constData(), data, error);
  else if (suffix == "rw5") ok = Rw5Import(utf8.constData(), data, error);
  else if (suffix == "jxl" || suffix == "xml") ok = JobXmlImport(utf8.constData(), data, error);
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
  if (suffix == "gstrav") m_State.traverseFilePath = path.toUtf8().toStdString();
  else if (suffix == "gsdat") m_State.traverseGsdatFilePath = path.toUtf8().toStdString();
  m_Coordinates->setRowCount(0); m_Residuals->setRowCount(0);
  m_Output->setPlainText("Loaded " + path + "\nSave as .gstrav to retain raw measurement sets.");
  setWindowModified(HasUnsavedChanges());
  ValidateNow();
  return true;
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
  ValidateNow();  // the plan and summary take the finished result
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
  TraverseData data;
  LsaWeights weights = m_Weights;
  std::string reason;
  if (!travcmd::Parse(m_Editor->toPlainText().toUtf8().toStdString(), data, weights, reason)) {
    error = QString::fromUtf8(reason.c_str()); return false;
  }
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
  RefreshFilesAndSummary();
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
  RefreshPreview();
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
  if (count == 0) {  // REQ-431 clause 7: say why nothing was placed
    const QString why = m_Log.empty() ? QString("No points were placed.") : QString::fromUtf8(m_Log.back().c_str());
    QMessageBox::warning(this, "Commit traverse", why);
    return 0;
  }
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
