#include "AdjustmentWindow.hpp"
#include "EditorWindow.hpp"
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

AdjustmentWindow::AdjustmentWindow(AppCommandState& state, std::vector<std::string>& log, QWidget* parent)
    : QMainWindow(parent, Qt::Window), m_State(state), m_Log(log) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("GoSurvey | Least-squares adjustment");
  resize(1200, 800);
  auto* toolbar = addToolBar("Adjustment");
  toolbar->setMovable(false);
  toolbar->addAction(QIcon(QtAsset("icons/Open.png")), "Import observations...", this, [this] { Import(); });
  toolbar->addAction(QIcon(QtAsset("icons/Save.png")), "Save commands...", this, [this] { SaveText(); });
  toolbar->addSeparator();
  toolbar->addAction(QIcon(QtAsset("icons/Adjust.png")), "Run adjustment", this, [this] { Calculate(); });
  auto* splitter = new QSplitter(this);
  m_Editor = new QPlainTextEdit(splitter);
  m_Editor->setObjectName("adjustmentCommands");
  m_Editor->setFont(QFont("Consolas", 11));
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(state.traverseData, state.traverseLsaWeights).c_str()));
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
  setCentralWidget(splitter);
}

void AdjustmentWindow::Import() {
  const QString path = QFileDialog::getOpenFileName(this, "Import observations", {},
      "Survey observations (*.gstc *.fbk *.dat *.rw5 *.jxl *.xml);;All files (*)");
  if (path.isEmpty()) return;
  TraverseData data;
  LsaWeights weights = m_State.traverseLsaWeights;
  std::string error;
  const QByteArray utf8 = path.toUtf8();
  const QString suffix = QFileInfo(path).suffix().toLower();
  bool ok = false;
  if (suffix == "fbk") ok = FbkImport(utf8.constData(), data, error);
  else if (suffix == "dat") ok = StarNetImport(utf8.constData(), data, error);
  else if (suffix == "rw5") ok = Rw5Import(utf8.constData(), data, error);
  else if (suffix == "jxl" || suffix == "xml") ok = JobXmlImport(utf8.constData(), data, error);
  else {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) ok = travcmd::Parse(file.readAll().toStdString(), data, weights, error);
    else error = file.errorString().toUtf8().toStdString();
  }
  if (!ok) { QMessageBox::warning(this, "Import observations", QString::fromUtf8(error.c_str())); return; }
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(data, weights).c_str()));
}

void AdjustmentWindow::Calculate() {
  TraverseData data;
  LsaWeights weights;
  std::string error;
  if (!travcmd::Parse(m_Editor->toPlainText().toUtf8().toStdString(), data, weights, error)) {
    m_Output->setPlainText(QString::fromUtf8(error.c_str()));
    return;
  }
  ComputeTraverse(data);
  const auto result = ComputeTraverseLeastSquares(data, weights);
  m_State.traverseData = std::move(data);
  m_State.traverseLsaWeights = weights;
  m_State.traverseLsaResult = result;
  m_State.traverseLsaComputed = true;
  m_State.traverseLsaAccepted = false;
  m_Log.push_back(result.message);
  m_Output->setPlainText(QString::fromUtf8(result.message.c_str()) +
      QString("\n\nObservations: %1\nUnknowns: %2\nRedundancy: %3\nIterations: %4\nReference standard deviation: %5\nAdjusted closure: %6\nChi-square: %7")
      .arg(result.observations).arg(result.unknowns).arg(result.redundancy).arg(result.iterations)
      .arg(result.refStdDev, 0, 'g', 12).arg(result.adjClosureLinear, 0, 'g', 12)
      .arg(!result.chiSquare.evaluated ? "Not evaluated" : result.chiSquare.passed ? "Passed" : "Failed"));
  m_Coordinates->setRowCount(static_cast<int>(result.stationIds.size()));
  for (int row = 0; row < static_cast<int>(result.stationIds.size()); ++row) {
    const QStringList cells = {QString::number(result.stationIds[row]),
        QString::number(result.adjEasting[row], 'f', 6), QString::number(result.adjNorthing[row], 'f', 6),
        result.usedGpsVectors && row < static_cast<int>(result.adjElevation.size()) ?
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
  const QString path = QFileDialog::getSaveFileName(this, "Save traverse commands", {}, "GoSurvey commands (*.gstc)");
  if (path.isEmpty()) return;
  QSaveFile file(path);
  const QByteArray text = m_Editor->toPlainText().toUtf8();
  if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit())
    QMessageBox::warning(this, "Save commands", file.errorString());
}

bool AdjustmentWindow::RunSmokeChecks(QString& error) {
  Calculate();
  const auto& result = m_State.traverseLsaResult;
  if (!result.ok || result.adjClosureLinear >= 0.001 || m_Coordinates->rowCount() == 0 || m_Residuals->rowCount() == 0) {
    error = "Qt adjustment editor failed its closed-loop solve: " + QString::fromUtf8(result.message.c_str());
    return false;
  }
  return true;
}
