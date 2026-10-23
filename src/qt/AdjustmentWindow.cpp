#include "AdjustmentWindow.hpp"
#include "AdjustmentGraph.hpp"
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
#include <QDialog>
#include <QDoubleValidator>
#include <QScrollArea>
#include <QSpinBox>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QIntValidator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// Layout comments must not discard importer detail or invalidate an adjustment.
// Compare the existing Commands projection, including notes and source mappings.
bool SameCommandValues(const QString& left, const QString& right) {
  travcmd::CommandDocument a, b;
  std::string error;
  if (!travcmd::Project(left.toUtf8().toStdString(), a, error) ||
      !travcmd::Project(right.toUtf8().toStdString(), b, error)) return false;
  if (a.commands.size() != b.commands.size() || a.sources.size() != b.sources.size() ||
      a.trailingComments != b.trailingComments) return false;
  for (std::size_t i = 0; i < a.commands.size(); ++i)
    if (a.commands[i].text != b.commands[i].text || a.commands[i].comments != b.commands[i].comments) return false;
  for (std::size_t i = 0; i < a.sources.size(); ++i)
    if (a.sources[i].id != b.sources[i].id || a.sources[i].path != b.sources[i].path) return false;
  return true;
}

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

std::size_t ResidualGroupFor(LsaMeasKind kind) {
  switch (kind) {
    case LsaMeasKind::Angle:
    case LsaMeasKind::Direction:
    case LsaMeasKind::Zenith: return 0;
    case LsaMeasKind::Distance: return 1;
    case LsaMeasKind::Vertical:
    case LsaMeasKind::GpsUp:
    case LsaMeasKind::CoordUp: return 2;
    case LsaMeasKind::GpsEast:
    case LsaMeasKind::GpsNorth: return 3;
    case LsaMeasKind::CoordEast:
    case LsaMeasKind::CoordNorth: return 4;
  }
  Q_ASSERT_X(false, "ResidualGroupFor", "Unhandled measurement residual kind");
  return 0;
}

const std::array<const char*, 5> kResidualGroupNames = {"Angle", "Distance", "Vertical", "GPS", "Coordinate"};
const std::array<const char*, 5> kResidualGroupIds = {"Angle", "Distance", "Vertical", "Gps", "Coordinate"};

struct RecordFieldSpec {
  enum class Kind : std::uint8_t { Text, Integer, Number, Choice };
  QString key;
  QString label;
  Kind kind = Kind::Text;
  QStringList choices;
  bool optional = false;
};

std::vector<RecordFieldSpec> RecordFields(const QString& directive) {
  using Kind = RecordFieldSpec::Kind;
  auto text = [](const char* key, const char* label, bool optional = false) {
    return RecordFieldSpec{key, label, Kind::Text, {}, optional};
  };
  auto integer = [](const char* key, const char* label) {
    return RecordFieldSpec{key, label, Kind::Integer, {}, false};
  };
  auto number = [](const char* key, const char* label, bool optional = false) {
    return RecordFieldSpec{key, label, Kind::Number, {}, optional};
  };
  auto choice = [](const char* key, const char* label, QStringList choices, bool optional = false) {
    return RecordFieldSpec{key, label, Kind::Choice, std::move(choices), optional};
  };
  if (directive == "IMPORT") return {text("path", "Imported file path")};
  if (directive == "STATION") return {integer("id", "Station number"), number("e", "Easting"), number("n", "Northing"), number("z", "Elevation")};
  if (directive == "BEARING") return {number("deg", "Bearing (degrees)")};
  if (directive == "ENDPOINT") return {number("e", "Easting"), number("n", "Northing"), number("z", "Elevation")};
  if (directive == "LOOP") return {choice("mode", "Loop", {"ON", "OFF"})};
  if (directive == "SCALE") return {choice("mode", "Scale factor", {"ON", "OFF"}), choice("kind", "Factor type", {"COMBINED", "GRID"})};
  if (directive == "ERRORS") return {number("angle", "Angle error (arc-seconds)"), number("distance", "Distance error"), number("ppm", "Distance error (ppm)")};
  if (directive == "STN") return {integer("at", "Occupied station"), integer("to", "Target station"),
      choice("kind", "Record type", {"Leg", "SIDE", "CHECK"}), number("e", "Known easting", true), number("n", "Known northing", true)};
  if (directive == "HANG") return {text("from", "Backsight station (or REF)"), integer("at", "Occupied station"), integer("to", "Target station"), number("deg", "Horizontal angle (degrees)")};
  if (directive == "HDIST" || directive == "SDIST") return {integer("at", "Occupied station"), integer("to", "Target station"), number("distance", "Distance")};
  if (directive == "VANG") return {integer("at", "Occupied station"), integer("to", "Target station"), number("deg", "Vertical angle (degrees)"), choice("type", "Vertical angle type", {"ZENITH", "ELEV"})};
  if (directive == "DESC") return {integer("at", "Occupied station"), integer("to", "Target station"), text("description", "Description", true)};
  if (directive == "PRISM") return {integer("at", "Occupied station"), integer("to", "Target station"), number("constant", "Prism constant"), number("error", "Prism error")};
  if (directive == "POLE") return {integer("at", "Occupied station"), integer("to", "Target station"), number("error", "Pole centering error")};
  if (directive == "TSERR") return {integer("at", "Occupied station"), integer("to", "Target station"), number("angle", "Angle error (arc-seconds)"), number("distance", "Distance error"), number("ppm", "Distance error (ppm)")};
  if (directive == "CTRL") return {integer("id", "Station number"), number("e", "Approximate easting"), number("n", "Approximate northing"),
      choice("mode", "Control type", {"Fixed 2D", "Component fixes 2D", "3D control"}), number("z", "Approximate elevation", true),
      text("efix", "Easting fix (!, *, or sigma)", true), text("nfix", "Northing fix (!, *, or sigma)", true), text("zfix", "Elevation fix (!, *, or sigma)", true)};
  if (directive == "ANGLE") return {integer("at", "Occupied station"), integer("from", "From station"), integer("to", "To station"), number("deg", "Angle (degrees)"), number("error", "Standard error (arc-seconds)", true)};
  if (directive == "DIST") return {integer("from", "From station"), integer("to", "To station"), number("distance", "Distance"), number("error", "Standard error", true)};
  if (directive == "AZIMUTH" || directive == "ZENITH") return {integer("from", "From station"), integer("to", "To station"), number("deg", directive == "AZIMUTH" ? "Azimuth (degrees)" : "Zenith angle (degrees)"), number("error", "Standard error (arc-seconds)", true)};
  if (directive == "EDIF") return {integer("from", "From station"), integer("to", "To station"), number("elevation", "Elevation difference"), number("error", "Standard error", true)};
  if (directive == "DIR") return {integer("set", "Direction set"), integer("at", "Occupied station"), integer("to", "Target station"), number("deg", "Direction (degrees)"), number("error", "Standard error (arc-seconds)", true)};
  return {};
}

int DirectiveEditDistance(const QString& left, const QString& right) {
  const QString a = left.toUpper();
  const QString b = right.toUpper();
  std::vector<int> previous(b.size() + 1), current(b.size() + 1);
  for (int j = 0; j <= b.size(); ++j) previous[j] = j;
  for (int i = 1; i <= a.size(); ++i) {
    current[0] = i;
    for (int j = 1; j <= b.size(); ++j)
      current[j] = std::min({current[j - 1] + 1, previous[j] + 1,
                             previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    std::swap(previous, current);
  }
  return previous[b.size()];
}

QString AddDirectiveSuggestion(const QString& error) {
  static const QStringList commands = {"IMPORT", "STATION", "BEARING", "ENDPOINT", "LOOP", "SCALE", "ERRORS",
      "STN", "HANG", "HDIST", "SDIST", "VANG", "DESC", "PRISM", "POLE", "TSERR", "CTRL",
      "ANGLE", "DIST", "AZIMUTH", "EDIF", "ZENITH", "DIR"};
  static const QRegularExpression unknown(R"(unknown directive\s+[\"']([^\"']+)[\"'])",
                                           QRegularExpression::CaseInsensitiveOption);
  const auto match = unknown.match(error);
  if (!match.hasMatch()) return error;
  const QString typed = match.captured(1).toUpper();
  QString best;
  int bestDistance = std::numeric_limits<int>::max();
  for (const auto& command : commands) {
    const int distance = DirectiveEditDistance(typed, command);
    if (distance < bestDistance) { bestDistance = distance; best = command; }
  }
  const int limit = typed.size() >= 6 ? 2 : 1;
  return bestDistance <= limit ? error + "\nDid you mean " + best + "?" : error;
}
}

AdjustmentWindow::AdjustmentWindow(AppCommandState& state, std::vector<std::string>& log, QWidget* parent)
    : QMainWindow(parent, Qt::Window), m_State(state), m_Log(log),
      m_Data(state.traverseData), m_Weights(state.traverseLsaWeights) {
  m_State.measureAnnotationText = MeasureQtText;
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("GoSurvey | Least-squares adjustment[*]");
  resize(1200, 800);
  auto* toolbar = addToolBar("Adjustment");
  toolbar->setObjectName("adjustmentToolbar");
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addAction(QIcon(QtAsset("icons/Open.png")), "Import observations...", this, [this] { Import(); });
  toolbar->addAction(QIcon(QtAsset("icons/Save.png")), "Save", this, [this] { Save(); });
  toolbar->addSeparator();
  m_RunAction = toolbar->addAction(QIcon(QtAsset("icons/Adjust.png")), "Run adjustment", this, [this] { Calculate(); });
  m_RunAction->setObjectName("runAdjustmentAction");
  m_RunAction->setShortcut(QKeySequence(Qt::Key_F5));
  m_StopAction = toolbar->addAction("Stop", this, [this] {
    if (m_CancelRequested) m_CancelRequested->store(true, std::memory_order_release);
    m_StopAction->setEnabled(false);
    m_Output->setPlainText("Stopping adjustment...");
    statusBar()->showMessage("Stopping adjustment...");
  });
  m_StopAction->setObjectName("stopAdjustmentAction");
  m_AcceptAction = toolbar->addAction("Accept result", this, [this] {
    QString error; if (!AcceptResult(error)) QMessageBox::warning(this, "Accept result", error);
  });
  m_AcceptAction->setObjectName("acceptAdjustmentAction");
  m_CommitAction = toolbar->addAction("Commit to drawing", this, [this] { CommitToDrawing(); });
  m_CommitAction->setObjectName("commitAdjustmentAction");
  auto* file = menuBar()->addMenu("&File");
  file->setObjectName("adjustmentFileMenu");
  file->addAction("Open / Import...", QKeySequence::Open, this, [this] { Import(); });
  file->addAction("Save", QKeySequence::Save, this, [this] { Save(); });
  file->addAction("Save As...", QKeySequence::SaveAs, this, [this] { Save(true); });
  file->addAction("Export report...", this, [this] { ExportReport(); });
  file->addSeparator();
  auto* settingsAction = file->addAction("Settings", this, [this] { ShowSettings(); });
  settingsAction->setObjectName("adjustmentSettingsAction");
  auto* recordsMenu = menuBar()->addMenu("&Records");
  recordsMenu->setObjectName("adjustmentRecordsMenu");
  auto addRecords = [this, recordsMenu](const QString& groupName, const QStringList& directives) {
    auto* group = recordsMenu->addMenu(groupName);
    QString groupId = groupName;
    groupId.remove(' ');
    group->setObjectName("adjustmentRecordGroup_" + groupId);
    for (const auto& directive : directives) {
      const QString caption = directive == "IMPORT" ? "Import marker (#IMPORT)" :
          (directive == "LOOP" || directive == "SCALE" || directive == "ERRORS" ? "#" + directive : directive);
      auto* action = group->addAction(caption);
      action->setObjectName("adjustmentRecordAction_" + directive);
      action->setData(directive);
      action->setStatusTip("Add a " + caption + " record at the editor cursor");
      connect(action, &QAction::triggered, this, [this, directive] { OpenRecordDialog(directive); });
    }
  };
  addRecords("Import", {"IMPORT"});
  addRecords("Setup", {"STATION", "BEARING", "ENDPOINT", "LOOP", "SCALE", "ERRORS"});
  addRecords("Traverse", {"STN", "HANG", "HDIST", "SDIST", "VANG", "DESC", "PRISM", "POLE", "TSERR"});
  addRecords("Network", {"CTRL", "ANGLE", "DIST", "AZIMUTH", "EDIF", "ZENITH", "DIR"});
  auto* splitter = new QSplitter(this);
  splitter->setObjectName("adjustmentWorkspaceSplitter");
  splitter->setChildrenCollapsible(false);
  // REQ-431: keep Session, Editor, and Results visible together as three resizable panes.
  auto* contextTabs = new QTabWidget(splitter);
  contextTabs->setObjectName("adjustmentContextTabs");
  auto* left = new QWidget(contextTabs);
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
  auto* importFilesButton = new QPushButton("Import raw files…", files);
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
  m_ImportedFiles = new QListWidget(files);
  m_ImportedFiles->setObjectName("issue913ImportedFiles");
  m_ImportedFiles->setWordWrap(true);
  m_ImportedFiles->setMinimumHeight(60);
  m_ImportedFiles->setMaximumHeight(72);
  filesLayout->addWidget(m_ImportedFiles);
  RefreshImportedFiles();
  auto* summary = new QGroupBox("Summary", left);
  auto* summaryLayout = new QVBoxLayout(summary);
  m_Summary = new QLabel(summary);
  m_Summary->setObjectName("adjustmentSummary");
  m_Summary->setWordWrap(true);
  summaryLayout->addWidget(m_Summary);
  auto* preview = new QGroupBox("Preview", left);
  preview->setMaximumHeight(210);
  auto* previewLayout = new QVBoxLayout(preview);
  m_Plan = new AdjustmentPlanView(preview);
  m_Plan->setObjectName("adjustmentPlan");
  m_Plan->SetUnitLabel(QString::fromUtf8(CadDrawingInsUnitsAbbrev(state.drawingInsUnits)));
  previewLayout->addWidget(m_Plan);
  leftLayout->addWidget(files);
  leftLayout->addWidget(summary);
  leftLayout->addWidget(preview, 1);
  auto* documentTabs = new QTabWidget(splitter);
  documentTabs->setObjectName("adjustmentDocumentTabs");
  auto* editorPane = new QWidget(documentTabs);
  documentTabs->addTab(editorPane, "Editor");
  editorPane->setObjectName("adjustmentEditorPane");
  auto* editorLayout = new QVBoxLayout(editorPane);
  editorLayout->setContentsMargins(8, 8, 8, 8);
  editorLayout->setSpacing(6);
  auto* editorHeading = new QLabel("Editor", editorPane);
  editorHeading->setObjectName("adjustmentEditorHeading");
  QFont editorHeadingFont = editorHeading->font();
  editorHeadingFont.setBold(true);
  editorHeading->setFont(editorHeadingFont);
  editorLayout->addWidget(editorHeading);
  m_Editor = new AdjustmentCodeEdit(editorPane);
  m_Editor->setObjectName("adjustmentCommands");
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(state.traverseData, state.traverseLsaWeights).c_str()));
  m_SourceText = m_SavedText = m_Editor->toPlainText();
  editorLayout->addWidget(m_Editor, 1);
  m_Graph = new AdjustmentGraph(documentTabs);
  m_Graph->textChanged = [this](const QString& text) {
    if (m_Editor->toPlainText() == text) return;
    const QScopedValueRollback<bool> layoutEdit(m_GraphLayoutEdit, SameCommandValues(m_Editor->toPlainText(), text));
    m_Editor->setPlainText(text);
    if (m_GraphLayoutEdit) {
      QString error;
      if (!ParseEditor(error)) m_Output->setPlainText(error);
    }
    ValidateNow();
  };
  documentTabs->addTab(m_Graph, "Graph");
  auto* resultsPane = new QWidget(splitter);
  resultsPane->setObjectName("adjustmentResultsPane");
  auto* resultsLayout = new QVBoxLayout(resultsPane);
  resultsLayout->setContentsMargins(8, 8, 8, 8);
  resultsLayout->setSpacing(6);
  auto* resultsHeading = new QLabel("Results", resultsPane);
  resultsHeading->setObjectName("adjustmentResultsHeading");
  QFont resultsHeadingFont = resultsHeading->font();
  resultsHeadingFont.setBold(true);
  resultsHeading->setFont(resultsHeadingFont);
  resultsLayout->addWidget(resultsHeading);
  auto* results = new QTabWidget(resultsPane);
  results->setObjectName("adjustmentResults");
  resultsLayout->addWidget(results, 1);
  auto* reportPage = new QWidget(results);
  auto* reportLayout = new QVBoxLayout(reportPage);
  reportLayout->setContentsMargins(0, 0, 0, 0);
  auto* reportActions = new QHBoxLayout();
  reportActions->addWidget(new QLabel("Adjustment report", reportPage));
  reportActions->addStretch(1);
  auto* exportTxt = new QPushButton("Export TXT…", reportPage);
  exportTxt->setObjectName("issue915ExportTxt");
  auto* exportRtf = new QPushButton("Export RTF…", reportPage);
  exportRtf->setObjectName("issue915ExportRtf");
  auto* exportPdf = new QPushButton("Export PDF…", reportPage);
  exportPdf->setObjectName("issue915ExportPdf");
  reportActions->addWidget(exportTxt);
  reportActions->addWidget(exportRtf);
  reportActions->addWidget(exportPdf);
  reportLayout->addLayout(reportActions);
  m_Output = new QPlainTextEdit(reportPage);
  m_Output->setObjectName("adjustmentOutput");
  m_Output->setReadOnly(true);
  m_Output->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_Output->setFont(QFont("Consolas", 10));
  m_Output->setPlainText("Import observations or enter GoSurvey traverse commands, then run adjustment.");
  reportLayout->addWidget(m_Output, 1);
  results->addTab(reportPage, "Report");
  connect(exportTxt, &QPushButton::clicked, this, [this] { ExportReportAs("txt"); });
  connect(exportRtf, &QPushButton::clicked, this, [this] { ExportReportAs("rtf"); });
  connect(exportPdf, &QPushButton::clicked, this, [this] { ExportReportAs("pdf"); });
  m_Coordinates = new QTableWidget(results);
  m_Coordinates->setObjectName("adjustmentCoordinates");
  m_Coordinates->setColumnCount(4);
  m_Coordinates->setHorizontalHeaderLabels({"Station", "Easting", "Northing", "Elevation"});
  for (auto* table : {m_Coordinates}) {
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  }
  results->addTab(m_Coordinates, "Adjusted coordinates");
  auto* residualsPage = new QWidget(results);
  auto* residualsLayout = new QVBoxLayout(residualsPage);
  residualsLayout->setContentsMargins(0, 0, 0, 0);
  m_ResidualNote = new QLabel(residualsPage);
  m_ResidualNote->setObjectName("adjustmentResidualNote");
  m_ResidualNote->setWordWrap(true);
  residualsLayout->addWidget(m_ResidualNote);
  auto* residualsScroll = new QScrollArea(residualsPage);
  residualsScroll->setObjectName("adjustmentResidualGroupsScroll");
  residualsScroll->setWidgetResizable(true);
  auto* residualGroups = new QWidget(residualsScroll);
  auto* residualGroupsLayout = new QVBoxLayout(residualGroups);
  residualGroupsLayout->setContentsMargins(4, 4, 4, 4);
  residualGroupsLayout->setSpacing(8);
  for (std::size_t i = 0; i < m_ResidualTables.size(); ++i) {
    auto* group = new QGroupBox(kResidualGroupNames[i], residualGroups);
    group->setObjectName(QString("adjustmentResidualGroup%1").arg(kResidualGroupIds[i]));
    auto* groupLayout = new QVBoxLayout(group);
    auto* table = new QTableWidget(group);
    table->setObjectName(QString("adjustmentResiduals%1").arg(kResidualGroupIds[i]));
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({"Measurement", "From", "To", "Residual", "Std. residual", "Flag"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    groupLayout->addWidget(table);
    residualGroupsLayout->addWidget(group);
    m_ResidualTables[i] = table;
    m_ResidualGroups[i] = group;
    group->hide();
  }
  residualGroupsLayout->addStretch(1);
  residualsScroll->setWidget(residualGroups);
  residualsLayout->addWidget(residualsScroll, 1);
  results->addTab(residualsPage, "Residuals");
  auto* sessionScroll = new QScrollArea(contextTabs);
  sessionScroll->setObjectName("adjustmentSessionPane");
  sessionScroll->setWidgetResizable(true);
  sessionScroll->setWidget(left);
  contextTabs->addTab(sessionScroll, "Session");
  auto* settingsDialog = new QDialog(this);
  settingsDialog->setObjectName("adjustmentSettingsWindow");
  settingsDialog->setWindowTitle("Adjustment Settings");
  settingsDialog->resize(720, 820);
  auto* settingsDialogLayout = new QVBoxLayout(settingsDialog);
  auto* settingsScroll = new QScrollArea(settingsDialog);
  settingsScroll->setObjectName("adjustmentSettingsScroll");
  settingsScroll->setWidgetResizable(true);
  auto* settingsGroup = new QGroupBox("Settings");
  settingsGroup->setObjectName("adjustmentSettingsGroup");
  auto* settingsGroupLayout = new QVBoxLayout(settingsGroup);
  auto* settingsPage = new QWidget(settingsGroup);
  settingsPage->setObjectName("adjustmentSettingsPane");
  auto* settingsLayout = new QVBoxLayout(settingsPage);
  settingsLayout->setContentsMargins(8, 8, 8, 8);
  m_SettingsStatus = new QLabel(settingsPage);
  m_SettingsStatus->setObjectName("issue915SettingsStatus");
  m_SettingsStatus->setWordWrap(true);
  m_SettingsStatus->setMinimumHeight(42);
  settingsLayout->addWidget(m_SettingsStatus);
  auto* projectOptions = new QGroupBox("Project options", settingsPage);
  auto* projectForm = new QFormLayout(projectOptions);
  m_SettingsAdjustmentType = new QComboBox(projectOptions);
  m_SettingsAdjustmentType->setObjectName("issue915AdjustmentType");
  m_SettingsAdjustmentType->addItems({"2D", "3D"});
  projectForm->addRow("Adjustment type", m_SettingsAdjustmentType);
  m_SettingsMaxIterations = new QSpinBox(projectOptions);
  m_SettingsMaxIterations->setObjectName("issue915MaxIterations");
  m_SettingsMaxIterations->setRange(1, std::numeric_limits<int>::max());
  projectForm->addRow("Maximum iterations", m_SettingsMaxIterations);
  auto* convergence = new QDoubleSpinBox(projectOptions);
  convergence->setObjectName("issue915ConvergenceLimit");
  convergence->setDecimals(12); convergence->setRange(1.e-12, 1.0);
  projectForm->addRow("Convergence limit", convergence);
  auto* refraction = new QDoubleSpinBox(projectOptions);
  refraction->setObjectName("issue915RefractionCoefficient");
  refraction->setDecimals(6); refraction->setRange(0.0, 1.0);
  projectForm->addRow("Coefficient of refraction", refraction);
  settingsLayout->addWidget(projectOptions);
  auto* instrumentOptions = new QGroupBox("Instrument standard errors", settingsPage);
  auto* instrumentForm = new QFormLayout(instrumentOptions);
  struct SettingsField { const char* name; const char* label; double LsaWeights::* member; double minimum; double maximum; int decimals; bool linear; };
  const SettingsField settingsFields[] = {
      {"issue915DistanceConstant", "Distance constant", &LsaWeights::sigmaDistConstFt, 0.000001, 1000, 6, true},
      {"issue915DistancePpm", "Distance ppm", &LsaWeights::sigmaDistPpm, 0, 100000, 4, false},
      {"issue915AngleSigma", "Angle (arc-seconds)", &LsaWeights::sigmaAngleSec, 0.000001, 3600, 4, false},
      {"issue915AzimuthSigma", "Azimuth / bearing (arc-seconds)", &LsaWeights::sigmaAzimuthSec, 0.000001, 3600, 4, false},
      {"issue915ZenithSigma", "Zenith (arc-seconds)", &LsaWeights::sigmaZenithSec, 0.000001, 3600, 4, false},
      {"issue915ElevationConstant", "Elevation constant", &LsaWeights::sigmaVertConstFt, 0.000001, 1000, 6, true},
      {"issue915ElevationPpm", "Elevation ppm", &LsaWeights::sigmaVertPpm, 0, 100000, 4, false},
      {"issue915LevelSigma", "Differential level run", &LsaWeights::sigmaLevelFt, 0.000001, 1000, 6, true},
      {"issue915InstrumentCentering", "Instrument centering", &LsaWeights::centeringInstrumentFt, 0, 1000, 6, true},
      {"issue915TargetCentering", "Target centering", &LsaWeights::centeringTargetFt, 0, 1000, 6, true},
      {"issue915VerticalCentering", "Vertical centering", &LsaWeights::centeringVerticalFt, 0, 1000, 6, true},
  };
  for (const auto& field : settingsFields) {
    auto* editor = new QDoubleSpinBox(instrumentOptions);
    editor->setObjectName(field.name);
    editor->setDecimals(field.decimals);
    editor->setRange(field.minimum, field.maximum);
    QString label = QString::fromUtf8(field.label);
    if (field.linear) label += " (" + UnitSuffix() + ")";
    instrumentForm->addRow(label, editor);
    m_SettingsWeightEditors.push_back(editor);
  }
  settingsLayout->addWidget(instrumentOptions);
  auto* settingsButtons = new QHBoxLayout();
  settingsButtons->addStretch(1);
  auto* discardSettings = new QPushButton("Discard edits", settingsPage);
  discardSettings->setObjectName("issue915DiscardSettings");
  auto* applySettings = new QPushButton("Apply settings", settingsPage);
  applySettings->setObjectName("issue915ApplySettings");
  settingsButtons->addWidget(discardSettings);
  settingsButtons->addWidget(applySettings);
  settingsLayout->addLayout(settingsButtons);
  settingsLayout->addStretch(1);
  settingsGroupLayout->addWidget(settingsPage);
  settingsScroll->setWidget(settingsGroup);
  settingsDialogLayout->addWidget(settingsScroll);
  connect(discardSettings, &QPushButton::clicked, this, [this] { ResetSettingsDraft(); });
  connect(applySettings, &QPushButton::clicked, this, [this] { ApplySettingsDraft(); });
  ResetSettingsDraft();
  splitter->setSizes({320, 450, 510});
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  splitter->setStretchFactor(2, 1);
  auto* central = new QWidget(this);
  auto* centralLayout = new QVBoxLayout(central);
  centralLayout->setContentsMargins(0, 0, 0, 0);
  centralLayout->setSpacing(0);
  BuildOptionsBar(central);
  centralLayout->addWidget(m_OptionsBar);
  centralLayout->addWidget(splitter, 1);
  setCentralWidget(central);
  m_TextStatus = new QLabel(this);
  m_TextStatus->setObjectName("adjustmentTextStatus");
  statusBar()->addPermanentWidget(m_TextStatus, 1);
  m_ValidateTimer = new QTimer(this);
  m_ValidateTimer->setSingleShot(true);
  m_ValidateTimer->setInterval(300);
  connect(m_ValidateTimer, &QTimer::timeout, this, [this] { ValidateNow(); });
  m_LiveSolveTimer = new QTimer(this);
  m_LiveSolveTimer->setSingleShot(true);
  m_LiveSolveTimer->setInterval(600);
  connect(m_LiveSolveTimer, &QTimer::timeout, this, [this] {
    m_LiveSolvePending = false;
    ValidateNow();
    if (!m_TextStatus->text().startsWith("Command text is valid")) {
      m_Output->setPlainText(m_TextStatus->text());
      m_Coordinates->setRowCount(0);
      ClearResidualTables();
      return;
    }
    if (IsBusy()) {
      m_LiveSolvePending = true;
      if (m_CancelRequested) m_CancelRequested->store(true, std::memory_order_release);
      return;
    }
    Calculate();
  });
  auto palette = AdjustmentPalette(QApplication::palette().color(QPalette::Window).lightness() < 128);
  palette.background = QApplication::palette().color(QPalette::Window);
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
    if (m_GraphLayoutEdit) { setWindowModified(HasUnsavedChanges()); return; }
    m_Graph->SetText({}, "Checking command text…");
    SyncOptionsFromText();
    InvalidateResult();
    if (m_CancelRequested) m_CancelRequested->store(true, std::memory_order_release);
    m_LiveSolvePending = true;
    setWindowModified(HasUnsavedChanges());
    m_ValidateTimer->start();  // REQ-431 clause 3: check once the typing pauses
    m_LiveSolveTimer->start();  // REQ-431: solve current commands after the typing pause
    RefreshPreview();          // any earlier result no longer matches the text
  });
  SyncOptionsFromText();
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
    m_Graph->SetText(m_Editor->toPlainText());
  } else {
    const QString message = AddDirectiveSuggestion(QString::fromUtf8(reason.c_str()));
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
    m_Graph->SetText(m_Editor->toPlainText(), message);
  }
  RefreshPreview();
}

void AdjustmentWindow::RefreshPreview() {
  const bool current = m_State.traverseLsaComputed && m_State.traverseLsaResult.ok;
  m_Plan->SetTraverse(m_PreviewData, current ? &m_State.traverseLsaResult : nullptr);
  RefreshFilesAndSummary();
}

void AdjustmentWindow::ClearResidualTables() {
  for (std::size_t i = 0; i < m_ResidualTables.size(); ++i) {
    m_ResidualTables[i]->setRowCount(0);
    m_ResidualGroups[i]->hide();
  }
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
  m_ResidualNote->setText("Angle residuals are in arc-seconds; distance, vertical, GPS and coordinate residuals are in " + unit +
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
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    auto palette = AdjustmentPalette(QApplication::palette().color(QPalette::Window).lightness() < 128);
    palette.background = QApplication::palette().color(QPalette::Window);
    m_Editor->SetPalette(palette);
    m_Plan->SetPalette(palette);
  }
  // The drawing's units or coordinate system may have changed while another window was in front.
  if (event->type() == QEvent::ActivationChange && isActiveWindow()) RefreshOptionsBar();
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
  m_SyncingOptions = true;
  m_CurvRefr->setChecked(data.applyCurvRefCorr);
  m_RefractionK->setValue(data.refractionCoeff);
  m_SyncingOptions = false;
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(data, weights).c_str()));
  m_SourceText = m_SavedText = m_Editor->toPlainText();
  m_Path = suffix == "gstrav" || suffix == "gstc" || suffix == "gsdat" ? path : QString{};
  m_Imported = m_Path.isEmpty();
  if (suffix == "gstrav") m_State.traverseFilePath = path.toUtf8().toStdString();
  else if (suffix == "gsdat") m_State.traverseGsdatFilePath = path.toUtf8().toStdString();
  m_Coordinates->setRowCount(0); ClearResidualTables();
  m_Output->setPlainText("Loaded " + path + "\nSave as .gstrav to retain raw measurement sets.");
  RefreshImportedFiles();
  setWindowModified(HasUnsavedChanges());
  ValidateNow();
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
    // Keep raw station identities from the source even when the legacy chained
    // traverse append remaps solver leg numbers. Cross-file sets remain comparable.
    auto projectSets = m_Data.rawSets;
    auto projectSources = m_Data.rawSources;
    const std::string sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    int at = imported.startStationId;
    bool hasSets = false;
    for (const auto& leg : imported.legs) {
      for (const auto& set : leg.rawSets) {
        if (!set.hasF1 && !set.hasF2) continue;
        projectSets.push_back({at, leg.stationId, set, sourceId,
            static_cast<int>(m_Data.legs.size() + projectSets.size() + imported.legs.size() + 1)});
        hasSets = true;
      }
      at = leg.stationId;
    }
    if (hasSets) projectSources.push_back({sourceId, utf8.toStdString()});
    TraverseAppendImported(m_Data, std::move(imported));
    m_Data.rawSets = std::move(projectSets);
    m_Data.rawSources = std::move(projectSources);
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
  ClearResidualTables();
  m_Output->setPlainText(QString("Imported %1 file(s), %2 leg(s).\nSave as .gstrav to retain raw measurement sets.")
      .arg(importedCount).arg(totalLegs));
  setWindowModified(HasUnsavedChanges());
  statusBar()->showMessage(QString("Imported %1 raw file(s)%2.").arg(importedCount)
      .arg(failures.isEmpty() ? QString{} : QString("; %1 failed — see Files").arg(failures.size())));
  ValidateNow();
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
  m_ValidateTimer->stop();
  m_LiveSolveTimer->stop();
  m_LiveSolvePending = false;
  if (IsBusy()) return;
  QString error;
  if (!ParseEditor(error)) { m_Output->setPlainText(error); return; }
  ApplyOptionsToData();
  m_ScaleNote = ApplyResolvedScale();
  RefreshOptionsBar();
  m_ResultDrawingUid = m_State.activeDrawingIdx > 0 ? m_State.drawingTabs[m_State.activeDrawingIdx].uid : 0;
  m_CancelRequested = std::make_shared<std::atomic<bool>>(false);
  SetBusy(true);
  m_State.traverseLsaComputed = false;
  m_State.traverseLsaAccepted = false;
  const auto cancelRequested = m_CancelRequested;
  m_Work = std::async(std::launch::async, [data = m_Data, weights = m_Weights, cancelRequested]() mutable {
    ComputeTraverse(data);
    auto result = ComputeTraverseLeastSquares(data, weights, cancelRequested.get());
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
    const bool cancelled = (m_CancelRequested && m_CancelRequested->load(std::memory_order_acquire)) || completed.second.cancelled;
    m_CancelRequested.reset();
    if (cancelled) {
      m_State.traverseLsaComputed = false;
      m_State.traverseLsaAccepted = false;
      m_Output->setPlainText("Adjustment cancelled. No partial results were kept.");
      m_Log.push_back("TRAVERSE: adjustment cancelled; no partial results were kept.");
      SetBusy(false);
      ValidateNow();
      if (m_LiveSolvePending) {
        m_LiveSolvePending = false;
        m_LiveSolveTimer->start();
      }
      return;
    }
    m_Data = std::move(completed.first);
    m_State.traverseLsaResult = std::move(completed.second);
    m_State.traverseData = m_Data;
    m_State.traverseLsaWeights = m_Weights;
    m_State.traverseLsaComputed = true;
    PresentResult();
  } catch (const std::exception& error) {
    m_CancelRequested.reset();
    m_State.traverseLsaComputed = false;
    m_Output->setPlainText(QString("TRAVERSE: adjustment failed: ") + QString::fromUtf8(error.what()));
    m_Log.push_back("TRAVERSE: adjustment failed: " + std::string(error.what()));
  }
  SetBusy(false);
  ValidateNow();  // the plan and summary take the finished result
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
  ClearResidualTables();
  std::array<int, 5> groupRows{};
  for (const auto& residual : result.measResiduals) ++groupRows[ResidualGroupFor(residual.kind)];
  for (std::size_t group = 0; group < groupRows.size(); ++group) {
    m_ResidualTables[group]->setRowCount(groupRows[group]);
    m_ResidualGroups[group]->setVisible(groupRows[group] > 0);
  }
  std::array<int, 5> nextRows{};
  for (const auto& residual : result.measResiduals) {
    const std::size_t group = ResidualGroupFor(residual.kind);
    const int row = nextRows[group]++;
    auto* table = m_ResidualTables[group];
    const bool angular = residual.kind == LsaMeasKind::Angle || residual.kind == LsaMeasKind::Zenith ||
                         residual.kind == LsaMeasKind::Direction;
    const QStringList cells = {QString::fromUtf8(LsaMeasKindName(residual.kind)),
        QString::number(residual.fromStationId), QString::number(residual.toStationId),
        QString::number(residual.residual, 'f', angular ? 6 : 4), QString::number(residual.stdResidual, 'f', 4),
        residual.flagged ? "> 3 sigma" : ""};
    for (int col = 0; col < cells.size(); ++col) {
      auto* item = new QTableWidgetItem(cells[col]);
      if (residual.flagged) item->setForeground(QColor("#f2ab64"));
      table->setItem(row, col, item);
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
    error = AddDirectiveSuggestion(QString::fromUtf8(reason.c_str())); return false;
  }
  data.importedFiles = importedFiles;
  if (SameCommandValues(m_SourceText, m_Editor->toPlainText())) {
    m_Data.graphNodes = std::move(data.graphNodes);
    m_Data.graphGroups = std::move(data.graphGroups);
  } else {
    m_Data = std::move(data); m_Weights = weights;
  }
  m_SourceText = m_Editor->toPlainText();
  return true;
}

bool AdjustmentWindow::SaveFile(const QString& path, QString& error) {
  if (IsBusy()) { error = "An adjustment is running."; return false; }
  ValidateNow();
  if (!m_Graph->PersistLayout()) { error = m_TextStatus->text(); return false; }
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
  if (IsBusy()) { statusBar()->showMessage("Stop the adjustment and wait for it to stop before closing."); event->ignore(); return; }
  if (!ConfirmDiscard()) { event->ignore(); return; }
  event->accept();
}

void AdjustmentWindow::SetBusy(bool busy) {
  m_Editor->setReadOnly(busy);
  m_Graph->setEnabled(!busy);
  if (auto* apply = findChild<QPushButton*>("issue915ApplySettings")) apply->setEnabled(!busy);
  if (auto* discard = findChild<QPushButton*>("issue915DiscardSettings")) discard->setEnabled(!busy);
  m_RunAction->setEnabled(!busy);
  m_StopAction->setEnabled(busy && m_CancelRequested && !m_CancelRequested->load(std::memory_order_acquire));
  const bool solved = m_State.traverseLsaComputed && m_State.traverseLsaResult.ok;
  m_AcceptAction->setEnabled(!busy && solved && !m_State.traverseLsaAccepted);
  m_CommitAction->setEnabled(!busy && solved);
  for (const char* name : {"issue915ExportTxt", "issue915ExportRtf", "issue915ExportPdf"})
    if (auto* exportButton = findChild<QPushButton*>(name)) exportButton->setEnabled(!busy && solved);
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
  QString error;
  if (!ExportReportFile(path, error)) QMessageBox::warning(this, "Export report", error);
}

bool AdjustmentWindow::ExportReportFile(const QString& path, QString& error) {
  if (IsBusy() || !m_State.traverseLsaComputed || !m_State.traverseLsaResult.ok) {
    error = "Run a successful adjustment before exporting its report.";
    return false;
  }
  const auto report = traverserpt::BuildReportText(m_Data, m_Weights, m_State.traverseLsaResult, "",
      CadDrawingInsUnitsAbbrev(m_State.drawingInsUnits));
  const auto utf8 = path.toUtf8().toStdString();
  const auto suffix = QFileInfo(path).suffix().toLower();
  std::string reason;
  const bool ok = suffix == "pdf" ? traverserpt::ExportReportPdf(utf8, report, reason) :
      suffix == "rtf" ? traverserpt::ExportReportRtf(utf8, report, reason) :
      suffix == "txt" ? traverserpt::ExportReportTxt(utf8, report, reason) : false;
  if (!ok) {
    error = reason.empty() ? "Choose a .txt, .rtf, or .pdf report file." : QString::fromUtf8(reason.c_str());
    return false;
  }
  return true;
}

void AdjustmentWindow::ExportReportAs(const QString& extension) {
  if (IsBusy() || !m_State.traverseLsaComputed || !m_State.traverseLsaResult.ok) {
    QMessageBox::information(this, "Export report", "Run a successful adjustment first.");
    return;
  }
  const QString ext = extension.toLower();
  const QString filter = ext == "pdf" ? "PDF report (*.pdf)" :
      ext == "rtf" ? "Rich text report (*.rtf)" : "Text report (*.txt)";
  QString path = QFileDialog::getSaveFileName(this, "Export " + ext.toUpper() + " report", {}, filter);
  if (path.isEmpty()) return;
  if (QFileInfo(path).suffix().isEmpty()) path += "." + ext;
  QString error;
  if (!ExportReportFile(path, error)) QMessageBox::warning(this, "Export report", error);
}

void AdjustmentWindow::ShowSettings() {
  auto* settingsDialog = findChild<QDialog*>("adjustmentSettingsWindow");
  if (!settingsDialog) return;
  settingsDialog->show();
  settingsDialog->raise();
  settingsDialog->activateWindow();
}

void AdjustmentWindow::OpenRecordDialog(const QString& directive) {
  const auto specs = RecordFields(directive);
  if (specs.empty()) return;
  auto* dialog = new QDialog(this);
  dialog->setObjectName("adjustmentRecordDialog");
  dialog->setWindowTitle("Add " + (directive == "IMPORT" ? QString("import marker") : directive + " record"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  dialog->setMinimumWidth(520);
  auto* layout = new QVBoxLayout(dialog);
  auto* heading = new QLabel("Enter the values for this command record.", dialog);
  heading->setObjectName("recordDialogHeading");
  layout->addWidget(heading);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  QMap<QString, QWidget*> inputs;
  for (const auto& spec : specs) {
    QWidget* input = nullptr;
    if (spec.kind == RecordFieldSpec::Kind::Choice) {
      auto* combo = new QComboBox(dialog);
      combo->addItems(spec.choices);
      input = combo;
    } else {
      auto* edit = new QLineEdit(dialog);
      edit->setClearButtonEnabled(true);
      edit->setPlaceholderText(spec.optional ? "Optional" : "Enter a value");
      if (spec.kind == RecordFieldSpec::Kind::Integer) edit->setValidator(new QIntValidator(edit));
      else if (spec.kind == RecordFieldSpec::Kind::Number) {
        auto* validator = new QDoubleValidator(edit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        edit->setValidator(validator);
      }
      input = edit;
    }
    input->setObjectName("recordField_" + spec.key);
    form->addRow(spec.label, input);
    inputs.insert(spec.key, input);
  }
  layout->addLayout(form);
  auto* error = new QLabel(dialog);
  error->setObjectName("recordValidationError");
  error->setWordWrap(true);
  error->setStyleSheet("color:#c83d35;");
  error->hide();
  layout->addWidget(error);
  auto* buttons = new QDialogButtonBox(dialog);
  auto* submit = buttons->addButton("Submit", QDialogButtonBox::AcceptRole);
  submit->setObjectName("recordSubmitButton");
  auto* cancel = buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
  cancel->setObjectName("recordCancelButton");
  layout->addWidget(buttons);
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(submit, &QPushButton::clicked, this, [this, dialog, directive, specs, inputs, error] {
    auto value = [&inputs](const QString& key) {
      QWidget* field = inputs.value(key);
      if (auto* edit = qobject_cast<QLineEdit*>(field)) return edit->text().trimmed();
      if (auto* combo = qobject_cast<QComboBox*>(field)) return combo->currentText();
      return QString{};
    };
    auto fail = [error](const QString& reason) {
      error->setText(reason);
      error->show();
    };
    for (const auto& spec : specs) {
      const QString fieldValue = value(spec.key);
      if (spec.kind == RecordFieldSpec::Kind::Choice || (spec.optional && fieldValue.isEmpty())) continue;
      if (fieldValue.isEmpty()) { fail(spec.label + " is required."); return; }
      if (spec.kind == RecordFieldSpec::Kind::Integer) {
        bool valid = false;
        fieldValue.toInt(&valid);
        if (!valid) { fail(spec.label + " must be a whole number."); return; }
      } else if (spec.kind == RecordFieldSpec::Kind::Number) {
        bool valid = false;
        const double parsed = fieldValue.toDouble(&valid);
        if (!valid || !std::isfinite(parsed)) { fail(spec.label + " must be a finite number."); return; }
      }
    }
    if (directive == "STN" && value("kind") == "CHECK" &&
        (value("e").isEmpty() != value("n").isEmpty())) {
      fail("Enter both known coordinates, or leave both blank.");
      return;
    }
    if (directive == "CTRL") {
      const QStringList fixes = value("mode") == "3D control"
          ? QStringList{value("efix"), value("nfix"), value("zfix")}
          : value("mode") == "Component fixes 2D" ? QStringList{value("efix"), value("nfix")} : QStringList{};
      if (value("mode") == "3D control" && value("z").isEmpty()) { fail("Approximate elevation is required for a 3D control."); return; }
      for (const auto& fix : fixes) {
        bool validSigma = false;
        const double sigma = fix.toDouble(&validSigma);
        if (fix != "!" && fix != "*" && (!validSigma || !std::isfinite(sigma) || sigma <= 0.0)) {
          fail("Each coordinate fix must be !, *, or a positive standard error.");
          return;
        }
      }
    }
    QString record;
    if (directive == "IMPORT") {
      QString escapedPath = value("path");
      escapedPath.replace('"', "\\\"");
      record = "#IMPORT \"" + escapedPath + "\"";
    } else {
      const bool option = directive == "LOOP" || directive == "SCALE" || directive == "ERRORS";
      record = option ? "#" + directive : directive;
      if (directive == "STN") {
        record += " " + value("at") + " " + value("to");
        if (value("kind") != "Leg") record += " " + value("kind");
        if (value("kind") == "CHECK" && !value("e").isEmpty()) record += " " + value("e") + " " + value("n");
      } else if (directive == "CTRL") {
        record += " " + value("id") + " " + value("e") + " " + value("n");
        if (value("mode") == "Component fixes 2D") record += " " + value("efix") + " " + value("nfix");
        else if (value("mode") == "3D control")
          record += " " + value("z") + " " + value("efix") + " " + value("nfix") + " " + value("zfix");
      } else {
        for (const auto& spec : specs) {
          if ((spec.kind == RecordFieldSpec::Kind::Choice &&
               ((directive == "STN" && spec.key == "kind") || (directive == "CTRL" && spec.key == "mode"))) ||
              value(spec.key).isEmpty()) continue;
          record += " " + value(spec.key);
        }
      }
    }

    QTextCursor cursor = m_Editor->textCursor();
    QTextCursor insertionStart = cursor;
    insertionStart.setPosition(cursor.selectionStart());
    if (insertionStart.positionInBlock() > 0) record.prepend('\n');
    record.append('\n');
    cursor.insertText(record);
    m_Editor->setTextCursor(cursor);
    m_Editor->setFocus();
    dialog->accept();
  });
  dialog->open();
}

void AdjustmentWindow::ResetSettingsDraft() {
  constexpr double LsaWeights::* members[] = {
      &LsaWeights::sigmaDistConstFt, &LsaWeights::sigmaDistPpm, &LsaWeights::sigmaAngleSec,
      &LsaWeights::sigmaAzimuthSec, &LsaWeights::sigmaZenithSec, &LsaWeights::sigmaVertConstFt,
      &LsaWeights::sigmaVertPpm, &LsaWeights::sigmaLevelFt, &LsaWeights::centeringInstrumentFt,
      &LsaWeights::centeringTargetFt, &LsaWeights::centeringVerticalFt};
  for (std::size_t i = 0; i < m_SettingsWeightEditors.size(); ++i)
    m_SettingsWeightEditors[i]->setValue(m_Weights.*members[i]);
  if (m_SettingsAdjustmentType) m_SettingsAdjustmentType->setCurrentIndex(m_Weights.adjustmentIs3D ? 1 : 0);
  if (m_SettingsMaxIterations) m_SettingsMaxIterations->setValue(m_Weights.maxIterations);
  if (auto* editor = findChild<QDoubleSpinBox*>("issue915ConvergenceLimit")) editor->setValue(m_Weights.convergenceLimit);
  if (auto* editor = findChild<QDoubleSpinBox*>("issue915RefractionCoefficient")) editor->setValue(m_Weights.refractionCoefficient);
  if (m_SettingsStatus) m_SettingsStatus->clear();
}

void AdjustmentWindow::ApplySettingsDraft() {
  if (IsBusy()) return;
  QString error;
  if (!ParseEditor(error)) {
    m_SettingsStatus->setText("Settings were not applied: " + error);
    return;
  }
  constexpr double LsaWeights::* members[] = {
      &LsaWeights::sigmaDistConstFt, &LsaWeights::sigmaDistPpm, &LsaWeights::sigmaAngleSec,
      &LsaWeights::sigmaAzimuthSec, &LsaWeights::sigmaZenithSec, &LsaWeights::sigmaVertConstFt,
      &LsaWeights::sigmaVertPpm, &LsaWeights::sigmaLevelFt, &LsaWeights::centeringInstrumentFt,
      &LsaWeights::centeringTargetFt, &LsaWeights::centeringVerticalFt};
  for (std::size_t i = 0; i < m_SettingsWeightEditors.size(); ++i)
    m_Weights.*members[i] = m_SettingsWeightEditors[i]->value();
  m_Weights.adjustmentIs3D = m_SettingsAdjustmentType->currentIndex() == 1;
  m_Weights.maxIterations = m_SettingsMaxIterations->value();
  m_Weights.convergenceLimit = findChild<QDoubleSpinBox*>("issue915ConvergenceLimit")->value();
  m_Weights.refractionCoefficient = findChild<QDoubleSpinBox*>("issue915RefractionCoefficient")->value();
  m_State.traverseData = m_Data;
  m_State.traverseLsaWeights = m_Weights;
  m_Editor->setPlainText(QString::fromUtf8(travcmd::Serialize(m_Data, m_Weights).c_str()));
  m_SourceText = m_Editor->toPlainText();
  m_Imported = true;
  InvalidateResult();
  SetBusy(false);
  setWindowModified(true);
  m_SettingsStatus->setText("Settings applied. Save .gstrav to keep them.");
  m_Output->setPlainText("Settings changed. Run the adjustment to update the report and plot.");
  RefreshPreview();
}

bool AdjustmentWindow::RunSmokeChecks(QString& error) {
  Calculate();
  if (m_Work.valid()) m_Work.wait();
  PollCalculation();
  const auto& result = m_State.traverseLsaResult;
  bool hasResiduals = false;
  for (const auto* table : m_ResidualTables) hasResiduals |= table->rowCount() > 0;
  if (!result.ok || result.adjClosureLinear >= 0.001 || m_Coordinates->rowCount() == 0 || !hasResiduals) {
    error = "Qt adjustment editor failed its closed-loop solve: " + QString::fromUtf8(result.message.c_str());
    return false;
  }
  return true;
}
