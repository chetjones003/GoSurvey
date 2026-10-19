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
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QRegularExpression>
#include <cmath>

namespace {
// Index of the first `#SCALE` / `SCALE` directive line in the command text, or -1.
int FindScaleLine(const QStringList& lines) {
  for (int i = 0; i < lines.size(); ++i) {
    QString text = lines[i].trimmed();
    if (text.startsWith('#')) text = text.mid(1).trimmed();
    if (text.section(QRegularExpression("\\s+"), 0, 0).compare("SCALE", Qt::CaseInsensitive) == 0) return i;
  }
  return -1;
}
// Where a missing `#SCALE` line goes: after the last header directive, else at the top.
int ScaleInsertLine(const QStringList& lines) {
  int after = -1;
  for (int i = 0; i < lines.size(); ++i) {
    QString text = lines[i].trimmed();
    if (text.startsWith('#')) text = text.mid(1).trimmed();
    const QString word = text.section(QRegularExpression("\\s+"), 0, 0).toUpper();
    if (word == "STATION" || word == "BEARING" || word == "ENDPOINT" || word == "LOOP" || word == "ERRORS") after = i;
  }
  return after + 1;
}
constexpr double kTypicalRefractionLimit = 2.0;  // 0.7.4 warns outside -2..+2.
}

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
  auto* residualsPage = new QWidget(results);
  auto* residualsLayout = new QVBoxLayout(residualsPage);
  residualsLayout->setContentsMargins(0, 0, 0, 0);
  m_ResidualNote = new QLabel(residualsPage);
  m_ResidualNote->setObjectName("adjustmentResidualNote");
  m_ResidualNote->setWordWrap(true);
  residualsLayout->addWidget(m_ResidualNote);
  residualsLayout->addWidget(m_Residuals, 1);
  results->addTab(residualsPage, "Residuals");
  splitter->addWidget(m_Editor);
  splitter->addWidget(results);
  splitter->setSizes({550, 650});
  auto* central = new QWidget(this);
  auto* centralLayout = new QVBoxLayout(central);
  centralLayout->setContentsMargins(0, 0, 0, 0);
  centralLayout->setSpacing(0);
  BuildOptionsBar(central);
  centralLayout->addWidget(m_OptionsBar);
  centralLayout->addWidget(splitter, 1);
  setCentralWidget(central);
  m_Poll = new QTimer(this);
  // Results are derived and tied to a particular drawing/session. Reopening the
  // editor requires a fresh solve before accepting or committing them.
  m_State.traverseLsaComputed = false;
  m_State.traverseLsaAccepted = false;
  m_Poll->setInterval(20);
  connect(m_Poll, &QTimer::timeout, this, [this] { PollCalculation(); });
  connect(m_Editor, &QPlainTextEdit::textChanged, this, [this] {
    SyncOptionsFromText();
    InvalidateResult();
    setWindowModified(HasUnsavedChanges());
  });
  SyncOptionsFromText();
  SetBusy(false);
}

void AdjustmentWindow::InvalidateResult() {
  m_State.traverseLsaComputed = false;
  m_State.traverseLsaAccepted = false;
  m_AcceptAction->setEnabled(false); m_CommitAction->setEnabled(false);
}

QString AdjustmentWindow::UnitSuffix() const {
  const QString unit = QString::fromUtf8(CadDrawingInsUnitsAbbrev(m_State.drawingInsUnits));
  return unit.isEmpty() ? QString("drawing units") : unit;
}

// Units label, curvature + refraction, and the ground-to-grid scale factor (REQ-401). The scale
// option IS the command text's `#SCALE` line, so the bar and the text are kept in step both ways;
// curvature/refraction is a bar setting that survives re-reading the text.
void AdjustmentWindow::BuildOptionsBar(QWidget* parent) {
  m_OptionsBar = new QWidget(parent);
  m_OptionsBar->setObjectName("adjustmentOptions");
  auto* row = new QHBoxLayout(m_OptionsBar);
  row->setContentsMargins(8, 4, 8, 4);
  m_UnitsLabel = new QLabel(m_OptionsBar);
  m_UnitsLabel->setObjectName("adjustmentUnits");
  m_UnitsLabel->setToolTip("Distances entered and shown in this editor use the active drawing's linear unit,\n"
                           "set in Drawing Settings or the UNITS command.");
  row->addWidget(m_UnitsLabel);
  row->addSpacing(16);
  m_CurvRefr = new QCheckBox("Curvature + refraction", m_OptionsBar);
  m_CurvRefr->setObjectName("adjustmentCurvRefr");
  m_CurvRefr->setChecked(m_Data.applyCurvRefCorr);
  m_CurvRefr->setToolTip("Apply the combined earth curvature + atmospheric refraction correction\n"
                         "C = (1 - k) * D^2 / (2R) to each leg's elevation difference.\n"
                         "Off = plain trigonometry.");
  row->addWidget(m_CurvRefr);
  row->addWidget(new QLabel("k", m_OptionsBar));
  m_RefractionK = new QDoubleSpinBox(m_OptionsBar);
  m_RefractionK->setObjectName("adjustmentRefractionK");
  m_RefractionK->setDecimals(3);
  m_RefractionK->setRange(-1000.0, 1000.0);
  m_RefractionK->setSingleStep(0.01);
  m_RefractionK->setValue(m_Data.refractionCoeff);
  m_RefractionK->setToolTip("Refraction coefficient k (typical range -2 to +2).\n0.14 = average atmospheric conditions (default).");
  row->addWidget(m_RefractionK);
  m_RefractionWarning = new QLabel("k outside the typical -2..+2 range", m_OptionsBar);
  m_RefractionWarning->setObjectName("adjustmentRefractionWarning");
  m_RefractionWarning->setStyleSheet("color: #d98200;");
  row->addWidget(m_RefractionWarning);
  row->addSpacing(16);
  m_ScaleCheck = new QCheckBox("Scale factor", m_OptionsBar);
  m_ScaleCheck->setObjectName("adjustmentScaleFactor");
  m_ScaleCheck->setToolTip("Reduce each leg's horizontal distance from ground to grid using the active\n"
                           "drawing's coordinate system (Drawing Settings > Transformation tab).\n"
                           "Off by default; existing traverses are unaffected unless turned on.");
  row->addWidget(m_ScaleCheck);
  m_ScaleKind = new QComboBox(m_OptionsBar);
  m_ScaleKind->setObjectName("adjustmentScaleKind");
  m_ScaleKind->addItems({"Combined", "Grid only"});
  m_ScaleKind->setToolTip("Combined: grid scale factor x elevation/sea-level factor, as resolved by the\n"
                          "Transformation tab's reference point.\n"
                          "Grid only: the map-projection grid scale factor alone, ignoring elevation/sea-level\n"
                          "reduction even if the Transformation tab has it enabled.");
  row->addWidget(m_ScaleKind);
  m_ScaleReadout = new QLabel(m_OptionsBar);
  m_ScaleReadout->setObjectName("adjustmentScaleReadout");
  row->addWidget(m_ScaleReadout);
  row->addStretch(1);
  auto curvChanged = [this] {
    if (m_SyncingOptions) return;
    ApplyOptionsToData();
    InvalidateResult();
    RefreshOptionsBar();
  };
  connect(m_CurvRefr, &QCheckBox::toggled, this, curvChanged);
  connect(m_RefractionK, &QDoubleSpinBox::valueChanged, this, curvChanged);
  connect(m_ScaleCheck, &QCheckBox::toggled, this, [this] { if (!m_SyncingOptions) WriteScaleDirective(); });
  connect(m_ScaleKind, &QComboBox::currentIndexChanged, this, [this] { if (!m_SyncingOptions) WriteScaleDirective(); });
}

// The `#SCALE` line is the source of truth for the scale option; mirror it into the bar.
void AdjustmentWindow::SyncOptionsFromText() {
  const QStringList lines = m_Editor->toPlainText().split('\n');
  const int at = FindScaleLine(lines);
  bool on = false, grid = false;
  if (at >= 0) {
    QString text = lines[at].trimmed();
    if (text.startsWith('#')) text = text.mid(1).trimmed();
    const QStringList tokens = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    on = tokens.size() > 1 && tokens[1].compare("ON", Qt::CaseInsensitive) == 0;
    grid = tokens.size() > 2 && tokens[2].compare("GRID", Qt::CaseInsensitive) == 0;
  }
  m_SyncingOptions = true;
  m_ScaleCheck->setChecked(on);
  m_ScaleKind->setCurrentIndex(grid ? 1 : 0);
  m_SyncingOptions = false;
  RefreshOptionsBar();
}

// Bar -> text: rewrite (or add) the `#SCALE` line in place so the rest of the text is untouched.
void AdjustmentWindow::WriteScaleDirective() {
  const QString directive = QString("#SCALE %1 %2").arg(m_ScaleCheck->isChecked() ? "ON" : "OFF",
                                                       m_ScaleKind->currentIndex() == 1 ? "GRID" : "COMBINED");
  const QStringList lines = m_Editor->toPlainText().split('\n');
  const int at = FindScaleLine(lines);
  QTextDocument* document = m_Editor->document();
  QTextCursor cursor(document);
  cursor.beginEditBlock();
  if (at >= 0) {
    cursor.setPosition(document->findBlockByNumber(at).position());
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(directive);
  } else {
    const int insert = ScaleInsertLine(lines);
    if (insert < document->blockCount()) {
      cursor.setPosition(document->findBlockByNumber(insert).position());
      cursor.insertText(directive + "\n");
    } else {
      cursor.movePosition(QTextCursor::End);
      cursor.insertText((document->isEmpty() ? QString() : QString("\n")) + directive);
    }
  }
  cursor.endEditBlock();
}

void AdjustmentWindow::ApplyOptionsToData() {
  m_Data.applyCurvRefCorr = m_CurvRefr->isChecked();
  m_Data.refractionCoeff = m_RefractionK->value();
}

// Refreshes everything the bar shows that depends on the drawing or on the other fields. A factor
// that no longer matches the solved one makes that result stale.
void AdjustmentWindow::RefreshOptionsBar() {
  int unitIndex = 2;  // Feet, the fallback default (REQ-357).
  for (int i = 0; i < kDrawingUnitCount; ++i)
    if (kDrawingUnitCodes[i] == m_State.drawingInsUnits) unitIndex = i;
  m_UnitsLabel->setText(QString("Units: ") + kDrawingUnitNames[unitIndex]);
  const QString unit = UnitSuffix();
  m_Coordinates->setHorizontalHeaderLabels({"Station", "Easting (" + unit + ")", "Northing (" + unit + ")", "Elevation (" + unit + ")"});
  m_ResidualNote->setText("Angle residuals are in arc-seconds; distance, vertical and GPS residuals are in " + unit +
                          ". A flag appears when the standardized residual exceeds 3.");
  m_RefractionK->setEnabled(m_CurvRefr->isChecked());
  m_RefractionWarning->setVisible(m_CurvRefr->isChecked() && std::fabs(m_RefractionK->value()) > kTypicalRefractionLimit);
  const bool scaleOn = m_ScaleCheck->isChecked();
  m_ScaleKind->setEnabled(scaleOn);
  m_ScaleReadout->setVisible(scaleOn);
  if (!scaleOn) return;
  double factor = 1.0;
  const std::string reason = ResolveTraverseScaleFactor(m_State,
      m_ScaleKind->currentIndex() == 1 ? TraverseData::ScaleFactorKind::GridOnly : TraverseData::ScaleFactorKind::Combined, &factor);
  if (reason.empty()) {
    m_ScaleReadout->setStyleSheet({});
    m_ScaleReadout->setText(QString("k = %1").arg(factor, 0, 'f', 6));
  } else {
    m_ScaleReadout->setStyleSheet("color: #d98200;");
    m_ScaleReadout->setText("(" + QString::fromUtf8(reason.c_str()) + ")");
    factor = 1.0;
  }
  if (m_State.traverseLsaComputed && factor != m_Data.scaleFactor && !IsBusy()) {
    InvalidateResult();
    SetBusy(false);
  }
}

// Resolves the drawing's ground->grid factor into the traverse (REQ-401 sections 3 and 5). When
// the drawing cannot supply one, the traverse stays unscaled and the reason is returned for display.
QString AdjustmentWindow::ApplyResolvedScale() {
  m_Data.scaleFactor = 1.0;
  if (!m_Data.applyScaleFactor) return {};
  double factor = 1.0;
  const std::string reason = ResolveTraverseScaleFactor(m_State, m_Data.scaleFactorKind, &factor);
  if (!reason.empty()) return QString::fromUtf8(reason.c_str());
  m_Data.scaleFactor = factor;
  return {};
}

void AdjustmentWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  // The drawing's units or coordinate system may have changed while another window was in front.
  if (event->type() == QEvent::ActivationChange && isActiveWindow()) RefreshOptionsBar();
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
  m_SyncingOptions = true;
  m_CurvRefr->setChecked(data.applyCurvRefCorr);
  m_RefractionK->setValue(data.refractionCoeff);
  m_SyncingOptions = false;
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(data, weights).c_str()));
  m_SourceText = m_SavedText = m_Editor->toPlainText();
  m_Path = suffix == "gstrav" || suffix == "gstc" || suffix == "gsdat" ? path : QString{};
  m_Imported = m_Path.isEmpty();
  m_Coordinates->setRowCount(0); m_Residuals->setRowCount(0);
  m_Output->setPlainText("Loaded " + path + "\nSave as .gstrav to retain raw measurement sets.");
  setWindowModified(HasUnsavedChanges());
  return true;
}

void AdjustmentWindow::Calculate() {
  if (IsBusy()) return;
  QString error;
  if (!ParseEditor(error)) { m_Output->setPlainText(error); return; }
  ApplyOptionsToData();
  m_ScaleNote = ApplyResolvedScale();
  RefreshOptionsBar();
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
  QString report = QString::fromUtf8(traverserpt::BuildReportText(m_Data, m_Weights, result, "",
      CadDrawingInsUnitsAbbrev(m_State.drawingInsUnits)).c_str());
  if (!m_ScaleNote.isEmpty()) report = "Scale factor not applied (computed unscaled): " + m_ScaleNote + "\n\n" + report;
  m_Output->setPlainText(report);
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
  ApplyOptionsToData();
  ApplyResolvedScale();  // keep the saved factor current; an unresolvable one is saved as 1.0
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
    layout->addRow(QString(field.label).replace("drawing units", UnitSuffix()), editor); editors.push_back(editor);
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
