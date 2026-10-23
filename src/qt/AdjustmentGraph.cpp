#include "AdjustmentGraph.hpp"
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsTextItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QCheckBox>
#include <QListWidget>
#include <QSpinBox>
#include <QScrollArea>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTableWidget>
#include <QUuid>
#include <QLabel>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>
#include <algorithm>

namespace {
// The same concrete canvas item handles draggable commands and visual frames.
class GraphRectItem final : public QGraphicsRectItem {
public:
  GraphRectItem(const QRectF& bounds, const QPen& pen, const QBrush& brush)
      : QGraphicsRectItem(bounds) {
    setPen(pen); setBrush(brush);
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
  }
  std::function<void(const QPointF&)> positionChanged;
  std::function<void()> released;
protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
    const QVariant result = QGraphicsRectItem::itemChange(change, value);
    if (change == ItemPositionHasChanged && positionChanged) positionChanged(pos());
    return result;
  }
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
    QGraphicsRectItem::mouseReleaseEvent(event);
    if (released) released();
  }
};

// Labels describe GSTC's existing fields. The complete record remains editable
// for optional fields, field codes, and descriptions containing spaces.
QStringList FieldLabels(const QString& text) {
  QString key = text.simplified().section(' ', 0, 0).toUpper();
  if (key == "#") key = text.simplified().section(' ', 1, 1).toUpper();
  if (key.startsWith('#')) key.remove(0, 1);
  if (key == "STATION") return {"Station", "Easting", "Northing", "Elevation"};
  if (key == "CTRL") return {"Station", "Easting", "Northing", "Elevation / easting fix", "Easting / northing fix", "Northing fix", "Elevation fix"};
  if (key == "BEARING") return {"Bearing (degrees)"};
  if (key == "ENDPOINT") return {"Easting", "Northing", "Elevation"};
  if (key == "LOOP") return {"Loop (ON/OFF)"};
  if (key == "SCALE") return {"Scale (ON/OFF)", "Factor type"};
  if (key == "ERRORS") return {"Angle error (arc-seconds)", "Distance error", "Distance error (ppm)"};
  if (key == "IMPORT") return {"Source path"};
  if (key == "HANG") return {"Backsight station", "Occupied station", "Target station", "Angle (degrees)"};
  if (key == "ANGLE") return {"Occupied station", "From station", "To station", "Angle (degrees)", "Standard error (arc-seconds)"};
  if (key == "DIR") return {"Direction set", "Occupied station", "Target station", "Direction (degrees)", "Standard error (arc-seconds)"};
  QStringList fields{key == "STN" || key == "HDIST" || key == "SDIST" || key == "VANG" || key == "DESC" || key == "PRISM" || key == "POLE" || key == "TSERR" || key == "RAWSET" ? "Occupied station" : "From station", "To station"};
  if (key == "STN") return fields + QStringList{"Leg / SIDE / CHECK", "Known easting", "Known northing"};
  if (key == "DESC") return fields + QStringList{"Description"};
  if (key == "PRISM") return fields + QStringList{"Prism constant", "Prism error"};
  if (key == "POLE") return fields + QStringList{"Pole centering error"};
  if (key == "TSERR") return fields + QStringList{"Angle error (arc-seconds)", "Distance error", "Distance error (ppm)"};
  if (key == "RAWSET") return fields + QStringList{"Set number"};
  if (key == "VANG") return fields + QStringList{"Angle (degrees)", "ZENITH / ELEV"};
  return fields + QStringList{key == "EDIF" ? "Elevation difference" : key == "AZIMUTH" || key == "ZENITH" ? "Angle (degrees)" : "Distance", "Standard error"};
}

QList<QRegularExpressionMatch> ValueSpans(const QString& text) {
  static const QRegularExpression word("\\S+");
  QList<QRegularExpressionMatch> spans;
  auto matches = word.globalMatch(text);
  while (matches.hasNext()) spans.push_back(matches.next());
  if (!spans.empty()) {
    const bool separatePrefix = spans.front().captured() == "#";
    spans.removeFirst();
    if (separatePrefix && !spans.empty()) spans.removeFirst();
  }
  return spans;
}

std::string RawGroupId(const travcmd::CommandNode& command) {
  const auto words = QString::fromStdString(command.text).simplified().split(' ');
  if (words.size() < 4 || words[0].compare("RAWSET", Qt::CaseInsensitive) != 0) return {};
  return "rawsets-" + std::to_string(words[1].toInt()) + "-" + std::to_string(words[2].toInt());
}

QString Provenance(const travcmd::CommandNode& command, const travcmd::CommandDocument& document) {
  if (RawGroupId(command).empty()) return {};
  const auto words = QString::fromStdString(command.text).simplified().split(' ');
  const std::string sourceId = words.back().toStdString();
  const auto source = std::find_if(document.sources.begin(), document.sources.end(),
      [&sourceId](const TraverseRawSource& value) { return value.id == sourceId; });
  return "Set " + words[3] + " · " + (source == document.sources.end() ? words.back() : QString::fromStdString(source->path));
}
}

AdjustmentGraph::AdjustmentGraph(QWidget* parent) : QWidget(parent) {
  setObjectName("adjustmentGraph");
  auto* layout = new QVBoxLayout(this);
  m_Status = new QLabel(this);
  m_Status->setObjectName("adjustmentGraphStatus");
  m_Status->setWordWrap(true);
  layout->addWidget(m_Status);
  m_OrderingWarnings = new QLabel(this);
  m_OrderingWarnings->setObjectName("graphOrderingWarnings");
  m_OrderingWarnings->setWordWrap(true);
  layout->addWidget(m_OrderingWarnings);
  auto* splitter = new QSplitter(this);
  m_Outline = new QTreeWidget(splitter);
  m_Outline->setObjectName("adjustmentGraphOutline");
  m_Outline->setHeaderLabels({"Survey commands and stations"});
  m_Scene = new QGraphicsScene(this);
  m_Canvas = new QGraphicsView(m_Scene, splitter);
  m_Canvas->setObjectName("adjustmentGraphCanvas");
  m_Canvas->setRenderHint(QPainter::Antialiasing);
  m_Canvas->setDragMode(QGraphicsView::ScrollHandDrag);
  splitter->setStretchFactor(1, 1);
  layout->addWidget(splitter, 1);
  m_Controls = new QWidget(this);
  auto* controls = new QVBoxLayout(m_Controls);
  controls->setContentsMargins(0, 0, 0, 0);
  auto* creation = new QHBoxLayout();
  auto* draft = new QLineEdit(m_Controls);
  draft->setObjectName("graphNewCommand");
  draft->setPlaceholderText("New GSTC command, e.g. DIST 1 2 100");
  auto* add = new QPushButton("Add command", m_Controls);
  add->setObjectName("graphAddCommand");
  creation->addWidget(draft, 1);
  creation->addWidget(add);
  controls->addLayout(creation);
  controls->addWidget(new QLabel("Command properties", m_Controls));
  m_CommandText = new QLineEdit(m_Controls);
  m_CommandText->setObjectName("graphCommandText");
  m_CommandText->setPlaceholderText("Select a command to edit all its values");
  controls->addWidget(m_CommandText);
  m_Provenance = new QLabel(m_Controls);
  m_Provenance->setObjectName("graphProvenance");
  m_Provenance->setWordWrap(true);
  controls->addWidget(m_Provenance);
  m_Values = new QTableWidget(0, 2, m_Controls);
  m_Values->setObjectName("graphPropertyValues");
  m_Values->setHorizontalHeaderLabels({"Property", "Value"});
  m_Values->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_Values->setMaximumHeight(155);
  controls->addWidget(m_Values);
  auto* actions = new QHBoxLayout();
  auto* apply = new QPushButton("Apply properties", m_Controls);
  apply->setObjectName("graphApplyProperties");
  auto* remove = new QPushButton("Delete command", m_Controls);
  remove->setObjectName("graphDeleteCommand");
  actions->addWidget(apply);
  actions->addWidget(remove);
  actions->addStretch();
  controls->addLayout(actions);
  m_EditError = new QLabel(m_Controls);
  m_EditError->setObjectName("graphEditError");
  m_EditError->setWordWrap(true);
  controls->addWidget(m_EditError);
  auto* positions = new QHBoxLayout();
  m_PositionX = new QDoubleSpinBox(m_Controls);
  m_PositionX->setObjectName("graphPositionX");
  m_PositionY = new QDoubleSpinBox(m_Controls);
  m_PositionY->setObjectName("graphPositionY");
  for (auto* spin : {m_PositionX, m_PositionY}) { spin->setRange(-1e9, 1e9); spin->setDecimals(6); }
  auto* move = new QPushButton("Apply layout", m_Controls);
  move->setObjectName("graphMoveSelection");
  positions->addWidget(new QLabel("X", m_Controls)); positions->addWidget(m_PositionX);
  positions->addWidget(new QLabel("Y", m_Controls)); positions->addWidget(m_PositionY);
  positions->addWidget(move);
  controls->addLayout(positions);
  auto* groups = new QHBoxLayout();
  m_VisualGroup = new QComboBox(m_Controls);
  m_VisualGroup->setObjectName("graphVisualGroup");
  groups->addWidget(new QLabel("Visual frame", m_Controls));
  groups->addWidget(m_VisualGroup, 1);
  controls->addLayout(groups);
  auto* frames = new QHBoxLayout();
  m_FrameLabel = new QLineEdit(m_Controls);
  m_FrameLabel->setObjectName("graphFrameLabel");
  m_FrameLabel->setPlaceholderText("Frame label");
  auto* addFrame = new QPushButton("Add frame", m_Controls);
  addFrame->setObjectName("graphAddFrame");
  auto* applyFrame = new QPushButton("Update frame", m_Controls);
  applyFrame->setObjectName("graphApplyFrame");
  frames->addWidget(m_FrameLabel, 1); frames->addWidget(addFrame); frames->addWidget(applyFrame);
  controls->addLayout(frames);
  auto* sizes = new QHBoxLayout();
  m_FrameWidth = new QDoubleSpinBox(m_Controls);
  m_FrameWidth->setObjectName("graphFrameWidth");
  m_FrameHeight = new QDoubleSpinBox(m_Controls);
  m_FrameHeight->setObjectName("graphFrameHeight");
  for (auto* spin : {m_FrameWidth, m_FrameHeight}) { spin->setRange(50, 1e9); spin->setValue(300); }
  sizes->addWidget(new QLabel("Frame width", m_Controls)); sizes->addWidget(m_FrameWidth);
  sizes->addWidget(new QLabel("Height", m_Controls)); sizes->addWidget(m_FrameHeight);
  controls->addLayout(sizes);
  auto* layoutActions = new QHBoxLayout();
  auto* arrange = new QPushButton("Arrange", m_Controls);
  arrange->setObjectName("graphArrange");
  auto* reset = new QPushButton("Reset layout", m_Controls);
  reset->setObjectName("graphResetLayout");
  auto* fit = new QPushButton("Fit", m_Controls);
  auto* zoomIn = new QPushButton("+", m_Controls);
  auto* zoomOut = new QPushButton("−", m_Controls);
  fit->setObjectName("graphFit");
  zoomIn->setAccessibleName("Zoom in"); zoomOut->setAccessibleName("Zoom out");
  layoutActions->addWidget(arrange); layoutActions->addWidget(reset);
  layoutActions->addWidget(fit); layoutActions->addWidget(zoomIn); layoutActions->addWidget(zoomOut);
  controls->addLayout(layoutActions);
  auto* manual = new QHBoxLayout();
  m_ManualOrder = new QCheckBox("Manual order override", m_Controls);
  m_ManualOrder->setObjectName("graphManualOrderEnabled");
  m_ManualOrder->setToolTip("Available for commands with ambiguous or cyclic order. Give each command in the warning a distinct rank.");
  m_ManualRank = new QSpinBox(m_Controls);
  m_ManualRank->setObjectName("graphManualOrderRank");
  m_ManualRank->setRange(-1000000000, 1000000000);
  manual->addWidget(m_ManualOrder); manual->addWidget(m_ManualRank);
  controls->addLayout(manual);
  controls->addWidget(new QLabel("Command sequence (independent of canvas position)", m_Controls));
  m_Sequence = new QListWidget(m_Controls);
  m_Sequence->setObjectName("graphCommandSequence");
  m_Sequence->setMaximumHeight(100);
  controls->addWidget(m_Sequence);
  auto* propertyScroll = new QScrollArea(this);
  propertyScroll->setObjectName("graphPropertiesScroll");
  propertyScroll->setWidgetResizable(true);
  propertyScroll->setWidget(m_Controls);
  propertyScroll->setMaximumHeight(310);
  layout->addWidget(propertyScroll);
  connect(move, &QPushButton::clicked, this, [this] { MoveSelection(); });
  connect(arrange, &QPushButton::clicked, this, [this] { Arrange(false); });
  connect(reset, &QPushButton::clicked, this, [this] { Arrange(true); });
  connect(fit, &QPushButton::clicked, this, [this] { m_Canvas->fitInView(m_Scene->itemsBoundingRect(), Qt::KeepAspectRatio); });
  connect(zoomIn, &QPushButton::clicked, this, [this] { m_Canvas->scale(1.2, 1.2); });
  connect(zoomOut, &QPushButton::clicked, this, [this] { m_Canvas->scale(1.0 / 1.2, 1.0 / 1.2); });
  connect(addFrame, &QPushButton::clicked, this, [this] {
    const QString label = m_FrameLabel->text().trimmed();
    if (label.isEmpty()) { m_EditError->setText("Enter a frame label."); return; }
    auto document = m_Document;
    const std::string id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    document.groups.push_back({id, m_PositionX->value() - 20, m_PositionY->value() - 40,
        m_FrameWidth->value(), m_FrameHeight->value(), false, label.toUtf8().toStdString()});
    for (auto& node : document.commands)
      if (QString::fromStdString(node.id) == m_Selected) node.groupId = id;
    Publish(document);
  });
  connect(applyFrame, &QPushButton::clicked, this, [this] {
    if (m_SelectedFrame.isEmpty()) { m_EditError->setText("Select a frame first."); return; }
    if (m_FrameLabel->text().trimmed().isEmpty()) { m_EditError->setText("Enter a frame label."); return; }
    auto document = m_Document;
    for (auto& group : document.groups) if (QString::fromStdString(group.id) == m_SelectedFrame) {
      group.label = m_FrameLabel->text().trimmed().toUtf8().toStdString();
      group.width = m_FrameWidth->value(); group.height = m_FrameHeight->value();
    }
    Publish(document);
  });
  connect(m_Outline, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
    SelectCommand(item ? item->data(0, Qt::UserRole).toString() : QString{});
    SelectFrame(item ? item->data(0, Qt::UserRole + 1).toString() : QString{});
  });
  connect(m_Outline, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
    SetCollapsed(item->data(0, Qt::UserRole + 1).toString(), true);
  });
  connect(m_Outline, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
    SetCollapsed(item->data(0, Qt::UserRole + 1).toString(), false);
  });
  connect(m_Scene, &QGraphicsScene::selectionChanged, this, [this] {
    const auto selected = m_Scene->selectedItems();
    if (selected.empty()) return;
    const QString id = selected.front()->data(0).toString();
    const bool frame = selected.front()->data(5).toString() == "frame";
    for (QTreeWidgetItemIterator it(m_Outline); *it; ++it)
      if ((*it)->data(0, frame ? Qt::UserRole + 1 : Qt::UserRole).toString() == id) { m_Outline->setCurrentItem(*it); break; }
  });
  connect(m_CommandText, &QLineEdit::textChanged, this, [this] { RefreshProperties(); });
  connect(m_Values, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
    if (item->column() != 1) return;
    QString text = m_CommandText->text();
    const auto spans = ValueSpans(text);
    if (item->row() >= spans.size()) return;
    const auto& span = spans[item->row()];
    text.replace(span.capturedStart(), span.capturedLength(), item->text());
    m_CommandText->setText(text);
  });
  connect(apply, &QPushButton::clicked, this, [this] {
    auto document = m_Document;
    for (auto& command : document.commands) {
      if (QString::fromStdString(command.id) != m_Selected) continue;
      const QString text = m_CommandText->text().trimmed();
      if (text.isEmpty() || text.contains('\n') || text.startsWith("//")) {
        m_EditError->setText("Enter one command."); return;
      }
      command.text = text.toUtf8().toStdString();
      if (m_ManualOrder->isEnabled())
        command.manualOrder = m_ManualOrder->isChecked() ? std::optional<int>(m_ManualRank->value()) : std::nullopt;
      Publish(document);
      return;
    }
    m_EditError->setText("Select a command first.");
  });
  connect(add, &QPushButton::clicked, this, [this, draft] {
    const QString text = draft->text().trimmed();
    if (text.isEmpty() || text.contains('\n') || text.startsWith("//")) {
      m_EditError->setText("Enter one command to add."); return;
    }
    auto document = m_Document;
    travcmd::CommandNode command;
    command.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    command.text = text.toUtf8().toStdString();
    command.sourceOrder = document.commands.size();
    command.x = 340;
    for (const auto& node : document.commands) command.y = std::max(command.y, node.y + 150);
    document.commands.push_back(command);
    const QString previous = m_Selected;
    m_Selected = QString::fromStdString(command.id);
    if (Publish(document)) draft->clear();
    else m_Selected = previous;
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    auto document = m_Document;
    document.commands.erase(std::remove_if(document.commands.begin(), document.commands.end(),
        [this](const travcmd::CommandNode& node) { return QString::fromStdString(node.id) == m_Selected; }), document.commands.end());
    Publish(document);
  });
}

AdjustmentGraph::~AdjustmentGraph() {
  // QWidget destroys the outline before the separately owned scene. Disconnect
  // while both are alive so item destruction cannot dispatch into a dead widget.
  QObject::disconnect(m_Scene, nullptr, this, nullptr);
  QObject::disconnect(m_Outline, nullptr, this, nullptr);
}

void AdjustmentGraph::SetText(const QString& text, const QString& error) {
  std::string reason;
  travcmd::CommandDocument document;
  if (!error.isEmpty() || !travcmd::Project(text.toUtf8().toStdString(), document, reason)) {
    m_Status->setText("Graph stale — " + (error.isEmpty() ? QString::fromStdString(reason) : error));
    m_Controls->setEnabled(false);
    m_Canvas->setInteractive(false);
    m_Current = false;
    if (travcmd::Project(text.toUtf8().toStdString(), document, reason)) {
      RefreshOrdering(document, false);
      if (!m_OrderingWarnings->text().isEmpty())
        m_OrderingWarnings->setText("Text ordering warnings (graph stale):\n" + m_OrderingWarnings->text());
    }
    return;
  }
  m_Document = std::move(document);
  std::map<std::string, int> rawCounts;
  for (auto& node : m_Document.commands) {
    const std::string raw = RawGroupId(node);
    if (raw.empty()) continue;
    ++rawCounts[raw];
    if (node.groupId.empty()) node.groupId = raw;
  }
  for (const auto& entry : rawCounts) {
    if (std::any_of(m_Document.groups.begin(), m_Document.groups.end(),
        [&entry](const TraverseGraphGroup& group) { return group.id == entry.first; })) continue;
    const auto command = std::find_if(m_Document.commands.begin(), m_Document.commands.end(),
        [&entry](const travcmd::CommandNode& node) { return RawGroupId(node) == entry.first; });
    const auto words = QString::fromStdString(command->text).simplified().split(' ');
    const QString label = QString::fromUtf8("Raw sets %1 → %2 (%3 sets)").arg(words[1], words[2]).arg(entry.second);
    m_Document.groups.push_back({entry.first, 660.0 + static_cast<double>(m_Document.groups.size()) * 340, 0,
        310, 50.0 + entry.second * 150.0, false, label.toStdString()});
  }
  // Missing metadata receives a deterministic first layout. Persisted zero is
  // a real coordinate, so presence of metadata, rather than value, decides this.
  std::map<std::string, int> rows;
  for (auto& node : m_Document.commands) {
    const QRegularExpression metadata("(?m)^\\s*//\\s*@GSTC-NODE\\s+" + QRegularExpression::escape(QString::fromStdString(node.id)) + "\\s");
    if (text.contains(metadata)) continue;
    const auto frame = std::find_if(m_Document.groups.begin(), m_Document.groups.end(),
        [&node](const TraverseGraphGroup& group) { return group.id == node.groupId; });
    node.x = frame == m_Document.groups.end() ? (node.setup ? 0 : 340) : frame->x + 15;
    const std::string column = frame == m_Document.groups.end() ? (node.setup ? "setup" : "measurements") : node.groupId;
    node.y = (frame == m_Document.groups.end() ? 0 : frame->y + 35) + static_cast<double>(rows[column]++) * 150;
  }
  m_Current = true;
  m_Status->setText("Graph is current");
  m_Controls->setEnabled(true);
  m_Canvas->setInteractive(true);
  RefreshOrdering(m_Document);
  // A live validation tick during a drag must not destroy the grabbed item; the release republishes and rebuilds.
  if (!m_KeepView && !m_Scene->mouseGrabberItem()) Rebuild();
}

void AdjustmentGraph::Rebuild() {
  const QSignalBlocker outlineBlock(m_Outline), sceneBlock(m_Scene);
  const QString selectedFrame = m_SelectedFrame;
  m_Outline->clear();
  m_Scene->clear();
  auto* setup = new QTreeWidgetItem(m_Outline, {"Setup"});
  auto* measurements = new QTreeWidgetItem(m_Outline, {"Measurements"});
  auto* stations = new QTreeWidgetItem(m_Outline, {"Stations"});
  std::map<std::string, QTreeWidgetItem*> rawGroups;
  for (auto& command : m_Document.commands) {
    const std::string groupId = RawGroupId(command);
    if (groupId.empty()) continue;
    if (command.groupId.empty()) command.groupId = groupId;
    if (rawGroups.count(groupId)) continue;
    const auto words = QString::fromStdString(command.text).simplified().split(' ');
    const auto count = std::count_if(m_Document.commands.begin(), m_Document.commands.end(),
        [&groupId](const travcmd::CommandNode& node) { return RawGroupId(node) == groupId; });
    const QString label = QString::fromUtf8("Raw sets %1 → %2 (%3 sets)").arg(words[1], words[2]).arg(count);
    auto* item = new QTreeWidgetItem(m_Outline, {label});
    item->setData(0, Qt::UserRole + 1, QString::fromStdString(groupId));
    rawGroups.emplace(groupId, item);
    const auto frame = std::find_if(m_Document.groups.begin(), m_Document.groups.end(),
        [&groupId](const TraverseGraphGroup& group) { return group.id == groupId; });
    if (frame == m_Document.groups.end())
      m_Document.groups.push_back({groupId, 280.0, static_cast<double>(rawGroups.size() - 1) * 350.0,
          290.0, 50.0 + static_cast<double>(count) * 100.0, false, label.toStdString()});
  }
  for (const auto& group : m_Document.groups) {
    if (!rawGroups.count(group.id)) {
      auto* item = new QTreeWidgetItem(m_Outline, {QString::fromStdString(group.label)});
      item->setData(0, Qt::UserRole + 1, QString::fromStdString(group.id));
      rawGroups.emplace(group.id, item);
    }
    if (QString::fromStdString(group.id) == selectedFrame) m_Outline->setCurrentItem(rawGroups.at(group.id));
    auto* frame = new GraphRectItem(QRectF(0, 0, group.width, group.collapsed ? 45 : group.height),
        QPen(palette().color(QPalette::Highlight)), QBrush(palette().color(QPalette::AlternateBase)));
    m_Scene->addItem(frame);
    frame->setPos(group.x, group.y);
    frame->setData(0, QString::fromStdString(group.id));
    frame->setData(5, "frame");
    const QString id = QString::fromStdString(group.id);
    frame->positionChanged = [this, id](const QPointF& position) { MoveCanvasItem(id, true, position); };
    frame->released = [this] { QTimer::singleShot(0, this, [this] { PersistLayout(); }); };
    frame->setZValue(-2);
    auto* title = new QGraphicsTextItem(QString::fromStdString(group.label) + (group.collapsed ? " (collapsed)" : ""), frame);
    title->setDefaultTextColor(palette().color(QPalette::Text));
    title->setPos(0, -28);
    title->setAcceptedMouseButtons(Qt::NoButton);
  }
  auto* setupTitle = m_Scene->addText("Setup");
  setupTitle->setDefaultTextColor(palette().color(QPalette::Text));
  setupTitle->setPos(0, -30);
  std::map<std::string, QPointF> anchors;
  double anchorX = 680;
  for (const auto& node : m_Document.commands) anchorX = std::max(anchorX, node.x + 400);
  for (const auto& group : m_Document.groups) anchorX = std::max(anchorX, group.x + group.width + 60);
  int stationRow = 0;
  for (const auto& station : m_Document.stations) {
    const QString label = "Station " + QString::fromStdString(station.id) + (station.resolved ? "" : " (unresolved)");
    new QTreeWidgetItem(stations, {label});
    const QPointF position(anchorX, stationRow++ * 65);
    anchors.emplace(station.id, position);
    const QColor color = station.resolved ? palette().color(QPalette::Highlight) : QColor("#c87822");
    m_Scene->addEllipse(QRectF(position, QSizeF(12, 12)), QPen(color), QBrush(color));
    auto* title = m_Scene->addText(label);
    title->setDefaultTextColor(palette().color(QPalette::Text));
    title->setPos(position + QPointF(18, -8));
  }
  for (const auto& command : m_Document.commands) {
    const QString text = QString::fromStdString(command.text);
    const auto rawGroup = RawGroupId(command);
    const auto group = rawGroups.find(rawGroup.empty() ? command.groupId : rawGroup);
    auto* item = new QTreeWidgetItem(group == rawGroups.end() ? (command.setup ? setup : measurements) : group->second, {text});
    item->setData(0, Qt::UserRole, QString::fromStdString(command.id));
    const QString provenance = Provenance(command, m_Document);
    item->setToolTip(0, provenance);
    const QPointF position(command.x, command.y);
    auto* node = new GraphRectItem(QRectF(0, 0, 280, 120), QPen(palette().color(QPalette::Mid)), QBrush(palette().color(QPalette::Base)));
    m_Scene->addItem(node);
    node->setPos(position);
    node->setFlag(QGraphicsItem::ItemIsSelectable);
    node->setData(0, QString::fromStdString(command.id));
    node->setData(1, QString::fromStdString(rawGroup));
    node->setData(2, QString::fromStdString(command.groupId));
    node->setData(5, "command");
    const QString id = QString::fromStdString(command.id);
    node->positionChanged = [this, id](const QPointF& next) { MoveCanvasItem(id, false, next); };
    node->released = [this] { QTimer::singleShot(0, this, [this] { PersistLayout(); }); };
    auto* title = new QGraphicsTextItem(provenance.isEmpty() ? text : text + "\n" + provenance, node);
    title->setTextWidth(250);
    title->setDefaultTextColor(palette().color(QPalette::Text));
    title->setAcceptedMouseButtons(Qt::NoButton);
    if (QString::fromStdString(command.id) == m_Selected) {
      m_Outline->setCurrentItem(item);
      node->setSelected(true);
    }
    for (const auto& id : command.stationIds) {
      auto* wire = m_Scene->addLine(QLineF(position + QPointF(280, 60), anchors.at(id) + QPointF(6, 6)), QPen(palette().color(QPalette::Mid)));
      wire->setZValue(-1);
      wire->setToolTip("Station " + QString::fromStdString(id));
      wire->setData(1, QString::fromStdString(rawGroup));
      wire->setData(2, QString::fromStdString(command.groupId));
      wire->setData(0, QString::fromStdString(command.id));
      wire->setData(3, anchors.at(id).x() + 6);
      wire->setData(4, anchors.at(id).y() + 6);
      wire->setData(5, "wire");
    }
  }
  m_Outline->expandAll();
  for (const auto& group : m_Document.groups) {
    const auto item = rawGroups.find(group.id);
    if (item != rawGroups.end()) item->second->setExpanded(!group.collapsed);
  }
  UpdateGroupVisibility();
  m_Scene->setSceneRect(m_Scene->itemsBoundingRect().adjusted(-30, -30, 30, 30));
  SelectCommand(m_Selected);
  if (!selectedFrame.isEmpty()) SelectFrame(selectedFrame);
}

void AdjustmentGraph::SelectCommand(const QString& id) {
  m_Selected = id;
  const auto found = std::find_if(m_Document.commands.begin(), m_Document.commands.end(),
      [&id](const travcmd::CommandNode& command) { return QString::fromStdString(command.id) == id; });
  m_CommandText->setText(found == m_Document.commands.end() ? QString{} : QString::fromStdString(found->text));
  m_Provenance->setText(found == m_Document.commands.end() ? QString{} : Provenance(*found, m_Document));
  findChild<QPushButton*>("graphApplyProperties")->setEnabled(found != m_Document.commands.end());
  findChild<QPushButton*>("graphDeleteCommand")->setEnabled(found != m_Document.commands.end());
  if (found != m_Document.commands.end()) {
    m_SelectedFrame.clear();
    m_PositionX->setValue(found->x); m_PositionY->setValue(found->y);
  }
  const auto order = travcmd::Order(m_Document);
  const bool canOverride = std::any_of(order.diagnostics.begin(), order.diagnostics.end(),
      [&id](const travcmd::OrderDiagnostic& diagnostic) {
        return std::find(diagnostic.commandIds.begin(), diagnostic.commandIds.end(), id.toStdString()) != diagnostic.commandIds.end();
      });
  m_ManualOrder->setEnabled(canOverride);
  m_ManualRank->setEnabled(canOverride);
  m_ManualOrder->setChecked(found != m_Document.commands.end() && found->manualOrder.has_value());
  m_ManualRank->setValue(found != m_Document.commands.end() ? found->manualOrder.value_or(0) : 0);
  const QSignalBlocker groupBlock(m_VisualGroup);
  m_VisualGroup->clear();
  m_VisualGroup->addItem("Ungrouped", QString{});
  for (const auto& group : m_Document.groups)
    m_VisualGroup->addItem(QString::fromStdString(group.label), QString::fromStdString(group.id));
  if (found != m_Document.commands.end())
    m_VisualGroup->setCurrentIndex(std::max(0, m_VisualGroup->findData(QString::fromStdString(found->groupId))));
  const QSignalBlocker block(m_Scene);
  for (auto* item : m_Scene->items()) {
    if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
      item->setSelected(item->data(0).toString() == id);
  }
}

void AdjustmentGraph::SelectFrame(const QString& id) {
  m_SelectedFrame = id;
  if (id.isEmpty()) return;
  for (const auto& group : m_Document.groups) if (QString::fromStdString(group.id) == id) {
    m_PositionX->setValue(group.x); m_PositionY->setValue(group.y);
    m_FrameLabel->setText(QString::fromStdString(group.label));
    m_FrameWidth->setValue(group.width); m_FrameHeight->setValue(group.height);
  }
  const QSignalBlocker block(m_Scene);
  for (auto* item : m_Scene->items())
    if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
      item->setSelected(item->data(5).toString() == "frame" && item->data(0).toString() == id);
}

void AdjustmentGraph::RefreshProperties() {
  const QSignalBlocker block(m_Values);
  const QString text = m_CommandText->text();
  const auto spans = ValueSpans(text);
  const auto labels = FieldLabels(text);
  m_Values->setRowCount(static_cast<int>(spans.size()));
  QString face;
  int faceField = 0;
  for (int i = 0; i < spans.size(); ++i) {
    const QString value = spans[i].captured();
    QString label = i < labels.size() ? labels[i] : "Optional value";
    if (text.trimmed().startsWith("RAWSET", Qt::CaseInsensitive) && i >= 3) {
      if (value == "F1" || value == "F2" || value == "SOURCE") { face = value; faceField = 0; label = value == "SOURCE" ? "Source" : "Face"; }
      else if (face == "SOURCE") label = "Source identity";
      else if (faceField < 3) label = face + " " + QStringList{"horizontal (degrees)", "zenith (degrees)", "slope distance"}[faceField++];
    }
    auto* name = new QTableWidgetItem(label);
    name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    m_Values->setItem(i, 0, name);
    m_Values->setItem(i, 1, new QTableWidgetItem(value));
  }
}

bool AdjustmentGraph::Publish(const travcmd::CommandDocument& document) {
  if (!m_Current) return false;
  const auto text = travcmd::Serialize(document);
  TraverseData data;
  LsaWeights weights;
  std::string reason;
  if (!travcmd::Parse(text, data, weights, reason)) {
    m_EditError->setText(QString::fromStdString(reason));
    return false;
  }
  m_EditError->clear();
  if (textChanged) textChanged(QString::fromStdString(text));
  return true;
}

void AdjustmentGraph::SetCollapsed(const QString& id, bool collapsed) {
  if (id.isEmpty() || !m_Current) return;
  for (auto& group : m_Document.groups) {
    if (QString::fromStdString(group.id) != id) continue;
    group.collapsed = collapsed;
    UpdateGroupVisibility();
    // Keep the live tree item alive while QTreeWidget dispatches its signal.
    const QScopedValueRollback<bool> keepView(m_KeepView, true);
    Publish(m_Document);
    return;
  }
}

void AdjustmentGraph::UpdateGroupVisibility() {
  for (auto* item : m_Scene->items()) {
    if (item->data(5).toString() == "frame") {
      const auto group = std::find_if(m_Document.groups.begin(), m_Document.groups.end(),
          [item](const TraverseGraphGroup& value) { return QString::fromStdString(value.id) == item->data(0).toString(); });
      if (group != m_Document.groups.end()) {
        static_cast<GraphRectItem*>(item)->setRect(0, 0, group->width, group->collapsed ? 45 : group->height);
        for (auto* child : item->childItems())
          if (auto* label = dynamic_cast<QGraphicsTextItem*>(child))
            label->setPlainText(QString::fromStdString(group->label) + (group->collapsed ? " (collapsed)" : ""));
      }
    }
    const QString raw = item->data(1).toString();
    const QString visual = item->data(2).toString();
    if (raw.isEmpty() && visual.isEmpty()) continue;
    const bool collapsed = std::any_of(m_Document.groups.begin(), m_Document.groups.end(),
        [&raw, &visual](const TraverseGraphGroup& value) {
          return value.collapsed && (QString::fromStdString(value.id) == raw || QString::fromStdString(value.id) == visual);
        });
    item->setVisible(!collapsed);
  }
}

bool AdjustmentGraph::PersistLayout() {
  if (!m_Current) return false;
  const QScopedValueRollback<bool> keepView(m_KeepView, true);
  return Publish(m_Document);
}

void AdjustmentGraph::MoveSelection() {
  if (!m_SelectedFrame.isEmpty()) {
    MoveCanvasItem(m_SelectedFrame, true, QPointF(m_PositionX->value(), m_PositionY->value()));
  } else {
    for (auto& node : m_Document.commands) if (QString::fromStdString(node.id) == m_Selected) {
      node.x = m_PositionX->value(); node.y = m_PositionY->value();
      node.groupId = m_VisualGroup->currentData().toString().toStdString();
    }
  }
  Publish(m_Document);
}

void AdjustmentGraph::MoveCanvasItem(const QString& id, bool frame, const QPointF& position) {
  if (!m_Current) return;
  if (!frame) {
    for (auto& node : m_Document.commands) if (QString::fromStdString(node.id) == id) {
      node.x = position.x(); node.y = position.y();
      if (id == m_Selected) { m_PositionX->setValue(node.x); m_PositionY->setValue(node.y); }
    }
  } else {
    for (auto& group : m_Document.groups) if (QString::fromStdString(group.id) == id) {
      const QPointF delta = position - QPointF(group.x, group.y);
      group.x = position.x(); group.y = position.y();
      if (id == m_SelectedFrame) { m_PositionX->setValue(group.x); m_PositionY->setValue(group.y); }
      for (auto& node : m_Document.commands) if (node.groupId == group.id) {
        node.x += delta.x(); node.y += delta.y();
        for (auto* item : m_Scene->items())
          if (item->data(5).toString() == "command" && item->data(0).toString() == QString::fromStdString(node.id))
            item->setPos(node.x, node.y);
      }
    }
  }
  UpdateWires();
}

void AdjustmentGraph::UpdateWires() {
  for (auto* item : m_Scene->items()) {
    if (item->data(5).toString() != "wire") continue;
    const auto command = std::find_if(m_Document.commands.begin(), m_Document.commands.end(),
        [item](const travcmd::CommandNode& node) { return QString::fromStdString(node.id) == item->data(0).toString(); });
    if (command == m_Document.commands.end()) continue;
    static_cast<QGraphicsLineItem*>(item)->setLine(QLineF(QPointF(command->x + 280, command->y + 60),
        QPointF(item->data(3).toDouble(), item->data(4).toDouble())));
  }
}

void AdjustmentGraph::Arrange(bool reset) {
  if (reset) {
    m_Document.groups.erase(std::remove_if(m_Document.groups.begin(), m_Document.groups.end(),
        [this](const TraverseGraphGroup& group) {
          return std::none_of(m_Document.commands.begin(), m_Document.commands.end(),
              [&group](const travcmd::CommandNode& node) { return RawGroupId(node) == group.id; });
        }), m_Document.groups.end());
    for (auto& node : m_Document.commands) node.groupId = RawGroupId(node);
    m_SelectedFrame.clear();
  }
  std::map<std::string, int> rows;
  int groupColumn = 2;
  for (auto& group : m_Document.groups) {
    group.x = static_cast<double>(groupColumn++) * 340;
    group.y = 0;
    group.width = 310;
    const auto count = std::count_if(m_Document.commands.begin(), m_Document.commands.end(),
        [&group](const travcmd::CommandNode& node) { return node.groupId == group.id; });
    group.height = std::max(150.0, 50 + static_cast<double>(count) * 150);
  }
  for (auto& node : m_Document.commands) {
    const auto group = std::find_if(m_Document.groups.begin(), m_Document.groups.end(),
        [&node](const TraverseGraphGroup& value) { return value.id == node.groupId; });
    const std::string column = group == m_Document.groups.end() ? (node.setup ? "setup" : "measurements") : node.groupId;
    node.x = group == m_Document.groups.end() ? (node.setup ? 0 : 340) : group->x + 15;
    node.y = (group == m_Document.groups.end() ? 0 : group->y + 35) + static_cast<double>(rows[column]++) * 150;
  }
  Publish(m_Document);
}

void AdjustmentGraph::RefreshOrdering(const travcmd::CommandDocument& document, bool updateSequence) {
  const auto order = travcmd::Order(document);
  QStringList warnings;
  for (const auto& diagnostic : order.diagnostics) {
    QStringList commands;
    for (const auto& id : diagnostic.commandIds) {
      const auto node = std::find_if(document.commands.begin(), document.commands.end(),
          [&id](const travcmd::CommandNode& command) { return command.id == id; });
      if (node != document.commands.end()) commands << QString::fromStdString(node->text).simplified();
    }
    warnings << (diagnostic.kind == travcmd::OrderDiagnosticKind::Cyclic ? "Cyclic order: " : "Ambiguous order: ") +
        commands.join("; ") + (diagnostic.overridden ? " — manual override applied" : " — assign distinct manual ranks to these commands to override");
  }
  m_OrderingWarnings->setText(warnings.join('\n'));
  if (!updateSequence) return;
  m_Sequence->clear();
  int sequence = 1;
  for (const auto& id : order.commandIds) {
    const auto node = std::find_if(document.commands.begin(), document.commands.end(),
        [&id](const travcmd::CommandNode& command) { return command.id == id; });
    if (node != document.commands.end()) m_Sequence->addItem(QString::number(sequence++) + ". " + QString::fromStdString(node->text));
  }
}
