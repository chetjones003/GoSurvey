#include "EditorWindow.hpp"

#include "CadBlocks.hpp"
#include "CadCommands.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLayoutItem>
#include <QFont>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>

namespace {

class ConnectionModeViewState final : public QObject {
public:
  using QObject::QObject;
  std::function<void()> renderModes;
  std::function<void()> refresh;
  std::uint32_t lastRevision = 0;
};

int EditingDefinition(const AppCommandState& state) {
  return CadBlockFindDef(state.blockDefs, state.blockEditorName);
}

void RecordConnectionEdit(AppCommandState& state, const char* description) {
  PushUndoSnapshot(state, description);
  state.blockEditorDirty = true;
  BumpCadGpuCache(state);
}

std::string NextModeName(const CadBlockConnection& connection) {
  for (int n = 1; n < 1000; ++n) {
    const std::string candidate = "Mode " + std::to_string(n);
    if (std::none_of(connection.modes.begin(), connection.modes.end(), [&](const auto& mode) {
          return CadBlockEqCi(mode.name, candidate);
        }))
      return candidate;
  }
  return "Mode";
}

void AddUnavailableRow(QVBoxLayout* layout, const QString& title, const QString& detail) {
  auto* card = new QGroupBox(title);
  auto* cardLayout = new QVBoxLayout(card);
  auto* unavailable = new QPushButton(title, card);
  unavailable->setEnabled(false);
  unavailable->setObjectName("unavailable_" + title.toLower());
  cardLayout->addWidget(unavailable);
  auto* note = new QLabel(detail, card);
  note->setWordWrap(true);
  note->setObjectName("unavailableNote_" + title.toLower());
  cardLayout->addWidget(note);
  layout->addWidget(card);
}

} // namespace


void EditorWindow::ShowBlockEditorPicker() {
  std::vector<std::string> names;
  CadBlocksCollectEditPickerNames(m_State, &names);
  if (names.size() <= 1) {
    m_State.blockEditPickerOpen = false;
    QMessageBox::information(this, "Edit Block Definition", "This drawing has no block definitions to edit.");
    return;
  }
  QStringList options;
  for (size_t i = 1; i < names.size(); ++i)
    options.push_back(QString::fromUtf8(names[i].c_str()));
  const QString suggested = QString::fromUtf8(m_State.blockEditPickerName);
  const int selected = static_cast<int>(std::max<qsizetype>(0, options.indexOf(suggested)));
  bool accepted = false;
  const QString name = QInputDialog::getItem(this, "Edit Block Definition",
                                              "Select a block definition:", options, selected, false,
                                              &accepted);
  if (!accepted) {
    m_State.blockEditPickerOpen = false;
    return;
  }
  const QByteArray utf8 = name.toUtf8();
  std::snprintf(m_State.blockEditPickerName, sizeof(m_State.blockEditPickerName), "%s", utf8.constData());
  CadBlocksCommitEditPicker(m_State, m_Log);
}

void EditorWindow::ShowBlockEditorClosePrompt() {
  if (!m_State.blockEditCloseAsked || m_State.blockEditorName.empty()) {
    m_State.blockEditCloseAsked = false;
    return;
  }

  m_ModalOpen = true;
  QMessageBox prompt(QMessageBox::Question, "Unsaved block changes",
                     QString("%1 has edits that are not saved to the block definition.")
                         .arg(QString::fromUtf8(m_State.blockEditorName.c_str())),
                     QMessageBox::NoButton, this);
  prompt.setObjectName("blockEditClosePrompt");
  prompt.setTextFormat(Qt::PlainText);
  auto* cancel = prompt.addButton("Cancel", QMessageBox::RejectRole);
  auto* discard = prompt.addButton("Don't Save", QMessageBox::DestructiveRole);
  auto* save = prompt.addButton("Save Changes", QMessageBox::AcceptRole);
  prompt.setDefaultButton(qobject_cast<QPushButton*>(save));
  prompt.exec();
  QAbstractButton* clicked = prompt.clickedButton();
  m_ModalOpen = false;

  if (clicked == save)
    Submit("BCLOSE SAVE");
  else if (clicked == discard)
    Submit("BCLOSE DISCARD");
  else {
    Q_UNUSED(cancel);
    m_State.blockEditCloseAsked = false;
    Refresh();
  }
}

void EditorWindow::ShowBlockAuthoringPalette() {
  if (m_State.blockEditorName.empty()) {
    m_State.blockAuthoringPaletteOpen = false;
    return;
  }
  if (auto* existing = findChild<QDialog*>("blockAuthoringPalette")) {
    if (!existing->isVisible()) existing->show();
    return;
  }

  auto* dialog = new QDialog(this);
  dialog->setObjectName("blockAuthoringPalette");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Block Authoring");
  dialog->resize(680, 570);
  auto* root = new QVBoxLayout(dialog);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(12);
  auto* heading = new QLabel("Block Authoring", dialog);
  heading->setObjectName("blockAuthoringHeading");
  QFont headingFont = heading->font();
  headingFont.setPointSize(17);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  root->addWidget(heading);
  auto* subheading = new QLabel(QString("Editing %1 · commands create parameters and actions in the active block definition.")
                                    .arg(QString::fromUtf8(m_State.blockEditorName.c_str())), dialog);
  subheading->setWordWrap(true);
  subheading->setObjectName("blockAuthoringDescription");
  root->addWidget(subheading);

  auto* tabs = new QWidget(dialog);
  auto* tabLayout = new QGridLayout(tabs);
  tabLayout->setContentsMargins(0, 0, 0, 0);
  tabLayout->setHorizontalSpacing(8);
  auto* pages = new QStackedWidget(dialog);
  pages->setObjectName("blockAuthoringPages");
  auto* tabGroup = new QButtonGroup(dialog);
  tabGroup->setExclusive(true);
  const QStringList names{"Parameters", "Actions", "Parameter Sets", "Constraints"};
  for (int i = 0; i < names.size(); ++i) {
    auto* button = new QPushButton(names[i], tabs);
    button->setCheckable(true);
    button->setObjectName(QString("blockAuthoringTab%1").arg(i));
    tabGroup->addButton(button, i);
    tabLayout->addWidget(button, 0, i);
    connect(button, &QPushButton::clicked, pages, [pages, i] { pages->setCurrentIndex(i); });
  }
  root->addWidget(tabs);
  root->addWidget(pages, 1);

  auto makePage = [pages](const QString& objectName, const QString& intro) {
    auto* page = new QWidget(pages);
    page->setObjectName(objectName);
    auto* layout = new QVBoxLayout(page);
    auto* hint = new QLabel(intro, page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    pages->addWidget(page);
    return std::pair{page, layout};
  };
  auto addCommand = [this](QGridLayout* grid, int& row, int& column, const QString& label,
                            const QString& objectName, const QString& command) {
    auto* button = new QPushButton(label);
    button->setObjectName(objectName);
    button->setMinimumHeight(44);
    grid->addWidget(button, row, column++);
    if (column == 2) { column = 0; ++row; }
    connect(button, &QPushButton::clicked, this, [this, command] { Submit(command); });
  };

  {
    auto [unusedPage, layout] = makePage("blockAuthoringParameters", "Choose a parameter to add it to this block.");
    auto* grid = new QGridLayout;
    int row = 0, column = 0;
    const struct { const char* label; const char* kind; } entries[] = {
        {"Point", "point"}, {"Linear", "linear"}, {"Polar", "polar"}, {"XY", "move"},
        {"Rotation", "rotation"}, {"Alignment", "linear"}, {"Flip", "flip"},
        {"Visibility", "visibility"}, {"Lookup", "lookup"}, {"Basepoint", "point"}};
    for (const auto& entry : entries) {
      const QString label = QString::fromUtf8(entry.label);
      addCommand(grid, row, column, label, QString("blockParameter") + label,
                 QString("BPARAM %1, %2").arg(label, QString::fromUtf8(entry.kind)));
    }
    layout->addLayout(grid);
    layout->addStretch();
  }
  {
    auto [unusedPage, layout] = makePage("blockAuthoringActions", "Actions bind a block parameter to a transform on the block geometry.");
    auto* grid = new QGridLayout;
    int row = 0, column = 0;
    const struct { const char* label; const char* kind; } entries[] = {
        {"Move", "move"}, {"Scale", "scale"}, {"Stretch", "stretch"},
        {"Polar Stretch", "stretch"}, {"Rotate", "rotate"}, {"Flip", "flip"},
        {"Array", "move"}, {"Lookup", "lookup"}};
    for (const auto& entry : entries) {
      const QString label = QString::fromUtf8(entry.label);
      addCommand(grid, row, column, label, QString("blockAction") + label,
                 QString("BACTION %1, DistPos").arg(QString::fromUtf8(entry.kind)));
    }
    layout->addLayout(grid);
    AddUnavailableRow(layout, "Block Properties Table", "This tool was not implemented in 0.7.4.");
    layout->addStretch();
  }
  {
    auto [page, layout] = makePage("blockAuthoringSets", "Parameter sets add a matching parameter and action where the 0.7.4 workflow supports them.");
    auto* grid = new QGridLayout;
    int row = 0, column = 0;
    auto addSet = [this, grid, &row, &column](const QString& label, const QString& objectName,
                                               const QStringList& commands) {
      auto* button = new QPushButton(label);
      button->setObjectName(objectName);
      button->setMinimumHeight(48);
      grid->addWidget(button, row, column++);
      if (column == 2) { column = 0; ++row; }
      connect(button, &QPushButton::clicked, this, [this, commands] {
        for (const auto& command : commands) Submit(command);
      });
    };
    addSet("Point Move", "blockSetPointMove", {"BPARAM Point, point", "BACTION move, Point"});
    addSet("Linear Stretch", "blockSetLinearStretch", {"BPARAM Dist, linear", "BACTION stretch, Dist, 0, 0, 0, 1, 0.05"});
    addSet("Linear Stretch Pair", "blockSetLinearStretchPair",
           {"BPARAM DistNeg, linear", "BPARAM DistPos, linear",
            "BACTION stretch, DistNeg, 0, 0, 0, -1, 0.05", "BACTION stretch, DistPos, 0, 0, 0, 1, 0.05"});
    addSet("Flip Set", "blockSetFlip", {"BPARAM Flip, flip", "BACTION flip, Flip, 0, 0, 1, 0, 0"});
    addSet("Visibility Set", "blockSetVisibility", {"BPARAM Vis, visibility", "BVISIBILITY VisibilityState0"});
    layout->addLayout(grid);
    auto* unavailable = new QLabel("Other parameter sets are not implemented in 0.7.4.", page);
    unavailable->setObjectName("blockOtherSetsUnavailable");
    layout->addWidget(unavailable);
    layout->addStretch();
  }
  {
    auto [page, layout] = makePage("blockAuthoringConstraints", "Constraint authoring is unavailable in 0.7.4.");
    const QStringList geometric{"Coincident", "Perpendicular", "Parallel", "Tangent", "Horizontal", "Vertical",
                                "Collinear", "Concentric", "Smooth", "Symmetric", "Equal", "Fix"};
    auto* geometricBox = new QGroupBox("Geometric constraints", page);
    auto* geometricGrid = new QGridLayout(geometricBox);
    for (int i = 0; i < geometric.size(); ++i) {
      auto* button = new QPushButton(geometric[i], geometricBox);
      button->setEnabled(false);
      button->setObjectName("blockConstraint" + geometric[i]);
      geometricGrid->addWidget(button, i / 3, i % 3);
    }
    layout->addWidget(geometricBox);
    auto* dimensionalBox = new QGroupBox("Dimensional constraints", page);
    auto* dimensionalGrid = new QGridLayout(dimensionalBox);
    const QStringList dimensional{"Aligned", "Horizontal", "Vertical", "Angular", "Radius"};
    for (int i = 0; i < dimensional.size(); ++i) {
      auto* button = new QPushButton(dimensional[i], dimensionalBox);
      button->setEnabled(false);
      button->setObjectName("blockDimensional" + dimensional[i]);
      dimensionalGrid->addWidget(button, i / 3, i % 3);
    }
    layout->addWidget(dimensionalBox);
    layout->addStretch();
  }

  const int selectedTab = std::clamp(m_State.blockAuthoringPaletteTab, 0, pages->count() - 1);
  pages->setCurrentIndex(selectedTab);
  if (auto* selected = tabGroup->button(selectedTab)) selected->setChecked(true);
  connect(tabGroup, &QButtonGroup::idClicked, dialog, [this](int index) { m_State.blockAuthoringPaletteTab = index; });
  connect(dialog, &QDialog::finished, this, [this] {
    m_State.blockAuthoringPaletteOpen = false;
    Refresh();
  });
  dialog->show();
}

void EditorWindow::ShowConnectionModes() {
  if (m_State.blockEditorName.empty()) {
    m_State.showConnectionModesWindow = false;
    if (auto* existing = findChild<QDialog*>("connectionModes")) existing->close();
    return;
  }
  if (auto* existing = findChild<QDialog*>("connectionModes")) {
    auto* stateObject = existing->findChild<QObject*>("connectionModesViewState");
    auto* viewState = static_cast<ConnectionModeViewState*>(stateObject);
    if (viewState && viewState->lastRevision != m_State.cadGpuRevision && viewState->refresh)
      viewState->refresh();
    if (!existing->isVisible()) existing->show();
    return;
  }

  auto* dialog = new QDialog(this);
  dialog->setObjectName("connectionModes");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Connection Modes");
  dialog->resize(1020, 560);
  auto* root = new QVBoxLayout(dialog);
  root->setContentsMargins(20, 18, 20, 18);
  auto* heading = new QLabel("Connection Modes", dialog);
  heading->setObjectName("connectionModesHeading");
  QFont headingFont = heading->font();
  headingFont.setPointSize(17);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  root->addWidget(heading);
  auto* blockLabel = new QLabel(QString("Editing connection behavior for %1.")
                                    .arg(QString::fromUtf8(m_State.blockEditorName.c_str())), dialog);
  blockLabel->setObjectName("connectionModesDescription");
  root->addWidget(blockLabel);

  auto* columns = new QHBoxLayout;
  auto* points = new QListWidget(dialog);
  points->setObjectName("connectionPointList");
  points->setMinimumWidth(190);
  points->setMaximumWidth(220);
  auto* editor = new QWidget(dialog);
  editor->setObjectName("connectionModeEditor");
  auto* editorLayout = new QVBoxLayout(editor);
  editorLayout->setContentsMargins(0, 0, 0, 0);
  auto* status = new QLabel(dialog);
  status->setObjectName("connectionModesStatus");
  status->setWordWrap(true);
  root->addWidget(status);
  columns->addWidget(points);
  columns->addWidget(editor, 1);
  root->addLayout(columns, 1);
  auto* done = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  done->setObjectName("connectionModesButtons");
  root->addWidget(done);
  connect(done, &QDialogButtonBox::rejected, dialog, &QDialog::close);

  auto* viewState = new ConnectionModeViewState(dialog);
  viewState->setObjectName("connectionModesViewState");
  viewState->lastRevision = m_State.cadGpuRevision;
  viewState->renderModes = [this, dialog, points, editorLayout, status, viewState] {
    while (QLayoutItem* item = editorLayout->takeAt(0)) {
      if (QWidget* widget = item->widget()) {
        if (widget->objectName() == "connectionModeTable") widget->setObjectName("connectionModeTable_stale");
        widget->deleteLater();
      }
      delete item;
    }
    viewState->lastRevision = m_State.cadGpuRevision;
    const int definitionIndex = EditingDefinition(m_State);
    if (definitionIndex < 0) {
      status->setText("The block being edited no longer exists.");
      return;
    }
    auto& definition = m_State.blockDefs[static_cast<size_t>(definitionIndex)];
    const int pointIndex = points->currentRow();
    if (pointIndex < 0 || pointIndex >= static_cast<int>(definition.connections.size())) {
      status->setText(definition.connections.empty()
                          ? "No connection points yet. Use Add Connection in the ribbon first."
                          : "Select a connection point to edit its modes.");
      return;
    }
    auto& connection = definition.connections[static_cast<size_t>(pointIndex)];
    status->setText(QString("Connection point: %1 · %2 %3")
                        .arg(QString::fromUtf8(connection.name.c_str()))
                        .arg(connection.modes.size())
                        .arg(connection.modes.size() == 1 ? "mode" : "modes"));
    if (connection.modes.empty()) {
      auto* note = new QLabel("This port uses its base role, engagement, and compatibility tag until a mode is added.", dialog);
      note->setWordWrap(true);
      editorLayout->addWidget(note);
    }
    auto* table = new QTableWidget(static_cast<int>(connection.modes.size()), 7, dialog);
    table->setObjectName("connectionModeTable");
    table->setHorizontalHeaderLabels({"Name", "Target", "Role", "Engagement", "Compat. tag", "Default", ""});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setColumnWidth(0, 120);
    table->setColumnWidth(1, 110);
    table->setColumnWidth(2, 82);
    table->setColumnWidth(3, 100);
    table->setColumnWidth(4, 125);
    table->setColumnWidth(5, 70);
    table->setColumnWidth(6, 82);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setMinimumHeight(150);
    const QSignalBlocker tableBlocker(table);
    for (int row = 0; row < static_cast<int>(connection.modes.size()); ++row) {
      const auto& mode = connection.modes[static_cast<size_t>(row)];
      auto* name = new QTableWidgetItem(QString::fromUtf8(mode.name.c_str()));
      table->setItem(row, 0, name);
      auto* target = new QComboBox(table);
      target->addItems({"Pipe end", "Flange face", "Generic port"});
      target->setCurrentIndex(static_cast<int>(mode.target));
      target->setObjectName(QString("connectionModeTarget%1").arg(row));
      table->setCellWidget(row, 1, target);
      auto* role = new QComboBox(table);
      role->addItems({"Inlet", "Outlet", "Branch"});
      role->setCurrentIndex(static_cast<int>(mode.role));
      role->setObjectName(QString("connectionModeRole%1").arg(row));
      table->setCellWidget(row, 2, role);
      auto* engagement = new QTableWidgetItem(QString::number(mode.engagementLength, 'g', 8));
      table->setItem(row, 3, engagement);
      table->setItem(row, 4, new QTableWidgetItem(QString::fromUtf8(mode.compatibilityTag.c_str())));
      auto* isDefault = new QCheckBox(table);
      isDefault->setChecked(mode.isDefault);
      isDefault->setObjectName(QString("connectionModeDefault%1").arg(row));
      table->setCellWidget(row, 5, isDefault);
      auto* remove = new QPushButton("Remove", table);
      remove->setObjectName(QString("removeConnectionMode%1").arg(row));
      table->setCellWidget(row, 6, remove);
      connect(target, &QComboBox::currentIndexChanged, dialog, [this, pointIndex, row](int index) {
        const int di = EditingDefinition(m_State);
        if (di < 0 || pointIndex >= static_cast<int>(m_State.blockDefs[di].connections.size()) ||
            row >= static_cast<int>(m_State.blockDefs[di].connections[pointIndex].modes.size())) return;
        RecordConnectionEdit(m_State, "Edit connection mode target");
        m_State.blockDefs[di].connections[pointIndex].modes[row].target = CadConnectionModeTarget(index);
        Refresh();
      });
      connect(role, &QComboBox::currentIndexChanged, dialog, [this, pointIndex, row](int index) {
        const int di = EditingDefinition(m_State);
        if (di < 0 || pointIndex >= static_cast<int>(m_State.blockDefs[di].connections.size()) ||
            row >= static_cast<int>(m_State.blockDefs[di].connections[pointIndex].modes.size())) return;
        RecordConnectionEdit(m_State, "Edit connection mode role");
        m_State.blockDefs[di].connections[pointIndex].modes[row].role = CadBlockConnectionRole(index);
        Refresh();
      });
      connect(isDefault, &QCheckBox::clicked, dialog, [this, pointIndex, row, viewState] {
        const int di = EditingDefinition(m_State);
        if (di < 0 || pointIndex >= static_cast<int>(m_State.blockDefs[di].connections.size()) ||
            row >= static_cast<int>(m_State.blockDefs[di].connections[pointIndex].modes.size())) return;
        RecordConnectionEdit(m_State, "Set default connection mode");
        auto& modes = m_State.blockDefs[di].connections[pointIndex].modes;
        for (int i = 0; i < static_cast<int>(modes.size()); ++i) modes[static_cast<size_t>(i)].isDefault = (i == row);
        viewState->renderModes();
        Refresh();
      });
      connect(remove, &QPushButton::clicked, dialog, [this, pointIndex, row, viewState] {
        const int di = EditingDefinition(m_State);
        if (di < 0 || pointIndex >= static_cast<int>(m_State.blockDefs[di].connections.size()) ||
            row >= static_cast<int>(m_State.blockDefs[di].connections[pointIndex].modes.size())) return;
        RecordConnectionEdit(m_State, "Remove connection mode");
        auto& modes = m_State.blockDefs[di].connections[pointIndex].modes;
        modes.erase(modes.begin() + row);
        if (!modes.empty() && std::none_of(modes.begin(), modes.end(), [](const auto& mode) { return mode.isDefault; }))
          modes.front().isDefault = true;
        viewState->renderModes();
        Refresh();
      });
    }
    connect(table, &QTableWidget::cellChanged, dialog, [this, table, status, pointIndex](int row, int column) {
      const int di = EditingDefinition(m_State);
      if (di < 0 || pointIndex < 0 || pointIndex >= static_cast<int>(m_State.blockDefs[di].connections.size()) ||
          row < 0 || row >= static_cast<int>(m_State.blockDefs[di].connections[pointIndex].modes.size())) return;
      auto& mode = m_State.blockDefs[di].connections[pointIndex].modes[static_cast<size_t>(row)];
      const QString text = table->item(row, column)->text();
      if (column == 0) {
        if (text.trimmed().isEmpty()) {
          status->setText("A mode needs a name.");
          const QSignalBlocker blocker(table);
          table->item(row, column)->setText(QString::fromUtf8(mode.name.c_str()));
          return;
        }
        if (mode.name == text.toUtf8().toStdString()) return;
        RecordConnectionEdit(m_State, "Rename connection mode");
        mode.name = text.toUtf8().toStdString();
      } else if (column == 3) {
        bool valid = false;
        const double value = text.toDouble(&valid);
        if (!valid || !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
          status->setText("Engagement must be a finite number.");
          const QSignalBlocker blocker(table);
          table->item(row, column)->setText(QString::number(mode.engagementLength, 'g', 8));
          return;
        }
        if (mode.engagementLength == static_cast<float>(value)) return;
        RecordConnectionEdit(m_State, "Edit connection mode engagement");
        mode.engagementLength = static_cast<float>(value);
      } else if (column == 4) {
        if (mode.compatibilityTag == text.toUtf8().toStdString()) return;
        RecordConnectionEdit(m_State, "Edit connection mode compatibility tag");
        mode.compatibilityTag = text.toUtf8().toStdString();
      } else {
        return;
      }
      status->clear();
      Refresh();
    });
    editorLayout->addWidget(table);
    auto* add = new QPushButton("Add mode", dialog);
    add->setObjectName("addConnectionMode");
    add->setEnabled(pointIndex >= 0 && pointIndex < static_cast<int>(definition.connections.size()));
    editorLayout->addWidget(add, 0, Qt::AlignLeft);
    connect(add, &QPushButton::clicked, dialog, [this, pointIndex, viewState, status] {
      const int di = EditingDefinition(m_State);
      if (di < 0 || pointIndex < 0 || pointIndex >= static_cast<int>(m_State.blockDefs[di].connections.size())) return;
      auto& connection = m_State.blockDefs[di].connections[pointIndex];
      RecordConnectionEdit(m_State, "Add connection mode");
      CadBlockConnectionMode mode;
      mode.name = NextModeName(connection);
      if (connection.modes.empty()) mode.isDefault = true;
      connection.modes.push_back(std::move(mode));
      status->clear();
      viewState->renderModes();
      Refresh();
    });
  };

  auto refreshPointList = [this, points, viewState] {
    const int di = EditingDefinition(m_State);
    const QSignalBlocker blocker(points);
    const int previousRow = points->currentRow();
    points->clear();
    if (di >= 0) {
      const auto& connections = m_State.blockDefs[static_cast<size_t>(di)].connections;
      for (const auto& connection : connections) {
        QString label = QString::fromUtf8(connection.name.c_str());
        label += QString(" · %1 mode(s)").arg(connection.modes.size());
        points->addItem(label);
      }
      if (!connections.empty()) points->setCurrentRow(std::clamp(previousRow, 0, static_cast<int>(connections.size()) - 1));
    }
    viewState->renderModes();
  };
  viewState->refresh = refreshPointList;
  connect(points, &QListWidget::currentRowChanged, dialog, [viewState](int) { viewState->renderModes(); });
  connect(dialog, &QDialog::finished, this, [this] {
    m_State.showConnectionModesWindow = false;
    Refresh();
  });
  refreshPointList();
  dialog->show();
}
