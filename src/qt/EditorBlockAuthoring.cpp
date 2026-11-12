#include "EditorWindow.hpp"

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "util/cadblockparams.hpp"

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
#include <QLineEdit>
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
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <utility>

namespace {

class BlockAuthoringViewState final : public QObject {
public:
  using QObject::QObject;
  std::function<void()> renderModes;
  std::function<void()> refresh;
  std::function<bool()> isEditing;
  std::uint32_t lastRevision = 0;
  int selectedColumn = -1;
};

enum class ParameterColumn : int { Name, Value, Type, Minimum, Maximum, Actions, Count };

constexpr int ColumnIndex(ParameterColumn column) {
  return static_cast<int>(column);
}

bool HasActiveCellEditor(const QTableWidget* table) {
  const auto* editor = table ? table->findChild<QLineEdit*>() : nullptr;
  return editor && editor->isVisible();
}

void TrackTableEditing(BlockAuthoringViewState* view, QTableWidget* table) {
  view->isEditing = [table] { return HasActiveCellEditor(table); };
}

void RestoreTableSelection(QTableWidget* table, int row, int column) {
  if (row < 0 || row >= table->rowCount() || table->columnCount() == 0)
    return;
  const int restoredColumn = std::clamp(column, 0, table->columnCount() - 1);
  table->selectRow(row);
  table->setCurrentCell(row, restoredColumn);
}

bool ParameterFieldFromColumn(ParameterColumn column, CadBlockParamField* field) {
  if (!field)
    return false;
  switch (column) {
    case ParameterColumn::Value:
      *field = CadBlockParamField::Value;
      return true;
    case ParameterColumn::Minimum:
      *field = CadBlockParamField::Min;
      return true;
    case ParameterColumn::Maximum:
      *field = CadBlockParamField::Max;
      return true;
    default:
      return false;
  }
}

void RefreshBlockAuthoringViewIfNeeded(QDialog* dialog, const char* stateObjectName, std::uint32_t revision) {
  auto* stateObject = dialog->findChild<QObject*>(stateObjectName);
  auto* view = static_cast<BlockAuthoringViewState*>(stateObject);
  if (view && view->lastRevision != revision && view->refresh &&
      (!view->isEditing || !view->isEditing()))
    view->refresh();
}

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
    auto* tableButton = new QPushButton("Block Properties Table", unusedPage);
    tableButton->setObjectName("blockPropertiesTableFromPalette");
    tableButton->setMinimumHeight(42);
    layout->addWidget(tableButton);
    connect(tableButton, &QPushButton::clicked, this, [this] { Submit("BTABLE"); });
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

void EditorWindow::RenderBlockParametersManager(QDialog* dialog) {
  auto* table = dialog->findChild<QTableWidget*>("blockParametersTable");
  auto* attrs = dialog->findChild<QLabel*>("blockParametersAttributes");
  auto* status = dialog->findChild<QLabel*>("blockParametersStatus");
  auto* viewState = static_cast<BlockAuthoringViewState*>(dialog->findChild<QObject*>("blockParametersViewState"));
  if (!table || !attrs || !status || !viewState)
    return;

  const QSignalBlocker blocker(table);
  const int definitionIndex = EditingDefinition(m_State);
  const int selectedRow = table->currentRow();
  const int selectedColumn = table->currentColumn();
  table->setRowCount(0);
  viewState->lastRevision = m_State.cadGpuRevision;
  if (definitionIndex < 0) {
    attrs->setText("The block being edited no longer exists.");
    return;
  }

  const CadBlockDefinition& definition = m_State.blockDefs[static_cast<size_t>(definitionIndex)];
  status->setText(definition.parameters.empty()
                      ? "This block has no parameters. Add them from Action Parameters."
                      : QString());
  table->setRowCount(static_cast<int>(definition.parameters.size()));
  for (size_t row = 0; row < definition.parameters.size(); ++row) {
    const CadBlockParameter& parameter = definition.parameters[row];
    const std::string_view kindLabel = CadBlockParamKindLabel(parameter.kind);
    const QStringList values{
        QString::fromUtf8(parameter.name.c_str()),
        QString::fromStdString(CadBlockFormatNumber(parameter.value)),
        QString::fromUtf8(kindLabel.data(), static_cast<int>(kindLabel.size())),
        QString::fromStdString(CadBlockFormatNumber(parameter.minValue)),
        QString::fromStdString(CadBlockFormatNumber(parameter.maxValue)),
        QString::number(CadBlockActionsBoundTo(definition, parameter.name))};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      if (column == ColumnIndex(ParameterColumn::Type) || column == ColumnIndex(ParameterColumn::Actions))
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
      table->setItem(static_cast<int>(row), column, item);
    }
  }

  QStringList definitions;
  for (const CadBlockAttrDef& attribute : definition.attrDefs) {
    definitions.push_back(
        QString("%1 = %2").arg(QString::fromUtf8(attribute.tag.c_str()), QString::fromUtf8(attribute.defaultValue.c_str())));
  }
  attrs->setText(definitions.isEmpty() ? "No attribute definitions." : definitions.join("\n"));
  RestoreTableSelection(table, selectedRow, selectedColumn);
  if (auto* remove = dialog->findChild<QPushButton*>("blockParameterDelete"))
    remove->setEnabled(table->currentRow() >= 0);
}

void EditorWindow::ConnectBlockParametersManager(QDialog* dialog) {
  auto* table = dialog->findChild<QTableWidget*>("blockParametersTable");
  auto* status = dialog->findChild<QLabel*>("blockParametersStatus");
  auto* buttons = dialog->findChild<QDialogButtonBox*>();
  auto* remove = dialog->findChild<QPushButton*>("blockParameterDelete");
  if (!table || !status || !buttons || !remove)
    return;

  table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  connect(table, &QTableWidget::itemSelectionChanged, dialog,
          [table, remove] { remove->setEnabled(table->currentRow() >= 0); });
  remove->setEnabled(table->currentRow() >= 0);
  connect(table, &QTableWidget::itemChanged, this, [this, dialog, table, status](QTableWidgetItem* item) {
    if (!item)
      return;
    const ParameterColumn column = static_cast<ParameterColumn>(item->column());
    const bool renaming = column == ParameterColumn::Name;
    CadBlockParamField parameterField = CadBlockParamField::Value;
    if (!renaming && !ParameterFieldFromColumn(column, &parameterField))
      return;

    const int row = item->row();
    const std::string value = item->text().toUtf8().toStdString();
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, renaming ? "Rename block parameter" : "Edit block parameter",
        [row, renaming, parameterField, value](CadBlockDefinition& definition) {
          if (renaming)
            return CadBlockParamRename(definition, row, value);
          return CadBlockParamSetNumber(definition, row, parameterField, value);
        });
    Refresh();
    RenderBlockParametersManager(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
  });
  connect(remove, &QPushButton::clicked, this, [this, dialog, table, status] {
    const int row = table->currentRow();
    if (row < 0)
      return;
    const auto answer = QMessageBox::question(
        this, "Delete Block Parameter", "Delete this parameter and its bound actions?");
    if (answer != QMessageBox::Yes)
      return;
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Delete block parameter", [row](CadBlockDefinition& definition) {
          int actionsRemoved = 0;
          return CadBlockParamDelete(definition, row, &actionsRemoved);
        });
    Refresh();
    RenderBlockParametersManager(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
    else
      status->clear();
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  connect(dialog, &QDialog::finished, this, [this] {
    m_State.showBlockParametersWindow = false;
    Refresh();
  });
}

void EditorWindow::ShowBlockParametersManager() {
  if (m_State.blockEditorName.empty()) {
    m_State.showBlockParametersWindow = false;
    if (auto* existing = findChild<QDialog*>("blockParametersManager"))
      existing->close();
    return;
  }
  if (auto* existing = findChild<QDialog*>("blockParametersManager")) {
    RefreshBlockAuthoringViewIfNeeded(existing, "blockParametersViewState", m_State.cadGpuRevision);
    if (!existing->isVisible())
      existing->show();
    return;
  }

  auto* dialog = new QDialog(this);
  dialog->setObjectName("blockParametersManager");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Parameters Manager");
  dialog->resize(860, 460);

  auto* layout = new QVBoxLayout(dialog);
  auto* heading = new QLabel(
      QString("Parameters — %1").arg(QString::fromUtf8(m_State.blockEditorName.c_str())), dialog);
  heading->setObjectName("blockParametersHeading");
  layout->addWidget(heading);

  auto* actionGroup = new QGroupBox("Action Parameters", dialog);
  auto* actionLayout = new QVBoxLayout(actionGroup);
  auto* table = new QTableWidget(actionGroup);
  table->setObjectName("blockParametersTable");
  table->setColumnCount(ColumnIndex(ParameterColumn::Count));
  const std::array<std::pair<ParameterColumn, QString>, ColumnIndex(ParameterColumn::Count)> headers{{
      {ParameterColumn::Name, "Name"}, {ParameterColumn::Value, "Value"}, {ParameterColumn::Type, "Type"},
      {ParameterColumn::Minimum, "Minimum"}, {ParameterColumn::Maximum, "Maximum"},
      {ParameterColumn::Actions, "Actions"}}};
  for (const auto& [column, label] : headers)
    table->setHorizontalHeaderItem(ColumnIndex(column), new QTableWidgetItem(label));
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setStretchLastSection(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  actionLayout->addWidget(table);
  layout->addWidget(actionGroup, 1);

  auto* attributeGroup = new QGroupBox("Attributes", dialog);
  auto* attributeLayout = new QVBoxLayout(attributeGroup);
  auto* attrs = new QLabel(attributeGroup);
  attrs->setObjectName("blockParametersAttributes");
  attrs->setWordWrap(true);
  attributeLayout->addWidget(attrs);
  layout->addWidget(attributeGroup);

  auto* status = new QLabel(dialog);
  status->setObjectName("blockParametersStatus");
  layout->addWidget(status);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  auto* remove = buttons->addButton("Delete Parameter", QDialogButtonBox::ActionRole);
  remove->setObjectName("blockParameterDelete");
  layout->addWidget(buttons);

  auto* viewState = new BlockAuthoringViewState(dialog);
  viewState->setObjectName("blockParametersViewState");
  viewState->lastRevision = m_State.cadGpuRevision;
  TrackTableEditing(viewState, table);
  viewState->refresh = [this, dialog] { RenderBlockParametersManager(dialog); };
  ConnectBlockParametersManager(dialog);
  RenderBlockParametersManager(dialog);
  dialog->show();
}

void EditorWindow::RenderBlockPropertiesTable(QDialog* dialog) {
  auto* table = dialog->findChild<QTableWidget*>("blockPropertiesTableGrid");
  auto* status = dialog->findChild<QLabel*>("blockPropertiesTableStatus");
  auto* viewState = static_cast<BlockAuthoringViewState*>(dialog->findChild<QObject*>("blockPropertiesTableViewState"));
  if (!table || !status || !viewState)
    return;

  const QSignalBlocker blocker(table);
  const int definitionIndex = EditingDefinition(m_State);
  const int selectedRow = table->currentRow();
  const int selectedCellColumn = table->currentColumn();
  if (selectedCellColumn >= 0)
    viewState->selectedColumn = selectedCellColumn;
  table->clear();
  viewState->lastRevision = m_State.cadGpuRevision;
  if (definitionIndex < 0) {
    table->setRowCount(0);
    table->setColumnCount(0);
    return;
  }

  const CadBlockDefinition& definition = m_State.blockDefs[static_cast<size_t>(definitionIndex)];
  const CadBlockPropertyTable& data = definition.propertyTable;
  if (definition.parameters.empty())
    status->setText("This block has no parameters to add.");
  else if (data.columns.empty())
    status->setText("This block has no properties table. Add Properties to start one.");
  else if (data.rows.empty())
    status->setText("The properties table has no rows. Add Row to define a combination.");
  else
    status->clear();

  table->setColumnCount(static_cast<int>(data.columns.size()));
  QStringList headers;
  for (const std::string& name : data.columns)
    headers.push_back(QString::fromUtf8(name.c_str()));
  table->setHorizontalHeaderLabels(headers);
  if (viewState->selectedColumn >= static_cast<int>(data.columns.size()))
    viewState->selectedColumn = static_cast<int>(data.columns.size()) - 1;
  table->setRowCount(static_cast<int>(data.rows.size()));
  for (size_t row = 0; row < data.rows.size(); ++row) {
    for (size_t column = 0; column < data.columns.size() && column < data.rows[row].size(); ++column) {
      table->setItem(static_cast<int>(row), static_cast<int>(column),
                     new QTableWidgetItem(QString::fromUtf8(data.rows[row][column].c_str())));
    }
  }
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  RestoreTableSelection(table, selectedRow, viewState->selectedColumn);
  if (auto* removeColumn = dialog->findChild<QPushButton*>("blockTableRemoveColumn"))
    removeColumn->setEnabled(table->currentColumn() >= 0 || viewState->selectedColumn >= 0);
  if (auto* removeRow = dialog->findChild<QPushButton*>("blockTableRemoveRow"))
    removeRow->setEnabled(table->currentRow() >= 0);
}

void EditorWindow::ConnectBlockPropertiesTable(QDialog* dialog) {
  auto* table = dialog->findChild<QTableWidget*>("blockPropertiesTableGrid");
  auto* status = dialog->findChild<QLabel*>("blockPropertiesTableStatus");
  auto* buttons = dialog->findChild<QDialogButtonBox*>();
  auto* addColumn = dialog->findChild<QPushButton*>("blockTableAddColumn");
  auto* removeColumn = dialog->findChild<QPushButton*>("blockTableRemoveColumn");
  auto* addRow = dialog->findChild<QPushButton*>("blockTableAddRow");
  auto* removeRow = dialog->findChild<QPushButton*>("blockTableRemoveRow");
  auto* viewState = static_cast<BlockAuthoringViewState*>(dialog->findChild<QObject*>("blockPropertiesTableViewState"));
  auto* clear = dialog->findChild<QPushButton*>("blockTableClear");
  if (!table || !status || !buttons || !addColumn || !removeColumn || !addRow || !removeRow || !clear || !viewState)
    return;

  table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  const auto updateSelectionButtons = [table, removeColumn, removeRow, viewState] {
    if (table->currentColumn() >= 0)
      viewState->selectedColumn = table->currentColumn();
    removeColumn->setEnabled(table->currentColumn() >= 0 || viewState->selectedColumn >= 0);
    removeRow->setEnabled(table->currentRow() >= 0);
  };
  connect(table, &QTableWidget::itemSelectionChanged, dialog, updateSelectionButtons);
  connect(table->horizontalHeader(), &QHeaderView::sectionClicked, dialog, [viewState, updateSelectionButtons](int column) {
    viewState->selectedColumn = column;
    updateSelectionButtons();
  });
  updateSelectionButtons();
  connect(table, &QTableWidget::itemChanged, this, [this, dialog, table, status](QTableWidgetItem* item) {
    if (!item)
      return;
    const int definitionIndex = EditingDefinition(m_State);
    if (definitionIndex < 0 || item->row() < 0 || item->column() < 0)
      return;
    const CadBlockPropertyTable& current = m_State.blockDefs[static_cast<size_t>(definitionIndex)].propertyTable;
    if (static_cast<size_t>(item->row()) >= current.rows.size() ||
        static_cast<size_t>(item->column()) >= current.columns.size())
      return;

    const int row = item->row() + 1;
    const int column = item->column();
    const std::string value = item->text().toUtf8().toStdString();
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Block table value", [row, column, value](CadBlockDefinition& definition) {
          if (static_cast<size_t>(column) >= definition.propertyTable.columns.size())
            return std::string("that table column no longer exists.");
          const std::string& name = definition.propertyTable.columns[static_cast<size_t>(column)];
          return CadBlockTableSetCell(definition, row, name, value);
        });
    Refresh();
    RenderBlockPropertiesTable(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
  });

  connect(addColumn, &QPushButton::clicked, this, [this, dialog, status] {
    const int definitionIndex = EditingDefinition(m_State);
    if (definitionIndex < 0)
      return;
    QStringList options;
    std::vector<int> parameterIndices;
    const auto& parameters = m_State.blockDefs[static_cast<size_t>(definitionIndex)].parameters;
    for (size_t index = 0; index < parameters.size(); ++index) {
      if (CadBlockTableColumnIndex(m_State.blockDefs[static_cast<size_t>(definitionIndex)],
                                   parameters[index].name) >= 0)
        continue;
      options.push_back(QString("%1 — %2").arg(index + 1).arg(QString::fromUtf8(parameters[index].name.c_str())));
      parameterIndices.push_back(static_cast<int>(index));
    }
    if (options.isEmpty()) {
      status->setText(parameters.empty() ? "This block has no parameters to add."
                                         : "Every parameter is already in the table.");
      return;
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, "Add Block Table Column", "Parameter:", options, 0, false, &accepted);
    if (!accepted)
      return;
    const int optionIndex = options.indexOf(selected);
    if (optionIndex < 0 || static_cast<size_t>(optionIndex) >= parameterIndices.size())
      return;
    const int parameterIndex = parameterIndices[static_cast<size_t>(optionIndex)];

    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Block table column", [parameterIndex](CadBlockDefinition& definition) {
          return CadBlockTableAddColumn(definition, parameterIndex);
        });
    Refresh();
    RenderBlockPropertiesTable(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
  });

  connect(removeColumn, &QPushButton::clicked, this, [this, dialog, table, status, viewState] {
    const int column = table->currentColumn() >= 0 ? table->currentColumn() : viewState->selectedColumn;
    if (column < 0)
      return;
    const QTableWidgetItem* header = table->horizontalHeaderItem(column);
    if (!header)
      return;
    const std::string name = header->text().toUtf8().toStdString();
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Block table column", [name](CadBlockDefinition& definition) {
          return CadBlockTableRemoveColumn(definition, name);
        });
    Refresh();
    RenderBlockPropertiesTable(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
  });

  connect(addRow, &QPushButton::clicked, this, [this, dialog, status] {
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Block table row", [](CadBlockDefinition& definition) {
          return CadBlockTableAddRow(definition, {});
        });
    Refresh();
    RenderBlockPropertiesTable(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
    else
      status->clear();
  });
  connect(removeRow, &QPushButton::clicked, this, [this, dialog, table, status] {
    const int row = table->currentRow();
    if (row < 0)
      return;
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Block table row", [row](CadBlockDefinition& definition) {
          return CadBlockTableRemoveRow(definition, row + 1);
        });
    Refresh();
    RenderBlockPropertiesTable(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
    else
      status->clear();
  });
  connect(clear, &QPushButton::clicked, this, [this, dialog, status] {
    const auto answer = QMessageBox::question(this, "Clear Block Table", "Remove every column and row?");
    if (answer != QMessageBox::Yes)
      return;
    const std::string reason = CadBlocksApplyDefinitionEdit(
        m_State, "Block table", [](CadBlockDefinition& definition) { return CadBlockTableClear(definition); });
    Refresh();
    RenderBlockPropertiesTable(dialog);
    if (!reason.empty())
      status->setText(QString::fromStdString(reason));
    else
      status->clear();
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  connect(dialog, &QDialog::finished, this, [this] {
    m_State.showBlockTableWindow = false;
    Refresh();
  });
}

void EditorWindow::ShowBlockPropertiesTable() {
  if (m_State.blockEditorName.empty()) {
    m_State.showBlockTableWindow = false;
    if (auto* existing = findChild<QDialog*>("blockPropertiesTable"))
      existing->close();
    return;
  }
  if (auto* existing = findChild<QDialog*>("blockPropertiesTable")) {
    RefreshBlockAuthoringViewIfNeeded(existing, "blockPropertiesTableViewState", m_State.cadGpuRevision);
    if (!existing->isVisible())
      existing->show();
    return;
  }

  auto* dialog = new QDialog(this);
  dialog->setObjectName("blockPropertiesTable");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Block Properties Table");
  dialog->resize(820, 460);

  auto* layout = new QVBoxLayout(dialog);
  auto* heading = new QLabel(
      QString("Block Properties Table — %1").arg(QString::fromUtf8(m_State.blockEditorName.c_str())), dialog);
  heading->setObjectName("blockPropertiesTableHeading");
  layout->addWidget(heading);

  auto* table = new QTableWidget(dialog);
  table->setObjectName("blockPropertiesTableGrid");
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(table, 1);

  auto* status = new QLabel(dialog);
  status->setObjectName("blockPropertiesTableStatus");
  layout->addWidget(status);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  const std::array<std::pair<const char*, const char*>, 5> buttonDefinitions{{
      {"Add Properties", "blockTableAddColumn"}, {"Remove Column", "blockTableRemoveColumn"},
      {"Add Row", "blockTableAddRow"}, {"Remove Row", "blockTableRemoveRow"}, {"Clear Table", "blockTableClear"}}};
  for (const auto& [label, objectName] : buttonDefinitions) {
    auto* button = buttons->addButton(label, QDialogButtonBox::ActionRole);
    button->setObjectName(objectName);
  }
  layout->addWidget(buttons);

  auto* viewState = new BlockAuthoringViewState(dialog);
  viewState->setObjectName("blockPropertiesTableViewState");
  viewState->lastRevision = m_State.cadGpuRevision;
  TrackTableEditing(viewState, table);
  viewState->refresh = [this, dialog] { RenderBlockPropertiesTable(dialog); };
  ConnectBlockPropertiesTable(dialog);
  RenderBlockPropertiesTable(dialog);
  dialog->show();
}

void EditorWindow::ShowConnectionModes() {
  if (m_State.blockEditorName.empty()) {
    m_State.showConnectionModesWindow = false;
    if (auto* existing = findChild<QDialog*>("connectionModes")) existing->close();
    return;
  }
  if (auto* existing = findChild<QDialog*>("connectionModes")) {
    RefreshBlockAuthoringViewIfNeeded(existing, "connectionModesViewState", m_State.cadGpuRevision);
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

  auto* viewState = new BlockAuthoringViewState(dialog);
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
