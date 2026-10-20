// Qt Surfaces manager (issue #893, REQ-068 / REQ-069 / REQ-075 / REQ-086 / REQ-136).
//
// An explorer tree over each surface's definition -- Surfaces > <surface> > Definition > Point Groups /
// Breaklines / Boundaries / Point Files -- with Add / Remove / Move on the nodes and a panel for the
// selected surface. Every edit is a `surfedit::` service call, and
// "Add Breakline / Boundary" starts the same DESIGNATE commands the command line does.
//
// Unlike the modal Create / Properties / Style dialogs this window stays open while a breakline or
// boundary is picked in the drawing, so it is modeless and refreshes itself on a timer (a rebuild lands
// asynchronously; an entity can be erased from the drawing underneath it).
//
// Breakline and boundary types the engine does not have are NOT listed at all (REQ-084: no control that
// cannot act), so only Standard breaklines are offered.
#include "EditorWindow.hpp"
#include "CadSurfaceEdit.hpp"
#include "EditorSurfaceUi.hpp"
#include "EditorViewport.hpp"
#include "SurfaceStyle.hpp"
#include "SurveyPoints.hpp"


#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <memory>

using surfaceui::Q;
using surfaceui::S;

namespace {

enum NodeType : std::uint8_t {
  SurfaceNode = 0, DefinitionNode, GroupCategory, BreaklineCategory, BoundaryCategory, FileCategory,
  GroupItem, BreaklineItem, BoundaryItem, FileItem,
};
constexpr int kRoleType = Qt::UserRole;
constexpr int kRoleSurface = Qt::UserRole + 1;
constexpr int kRoleIndex = Qt::UserRole + 2;

const char* LayoutName(int layoutIndex) {
  switch (layoutIndex) {
  case 1: return "PENZD";
  case 2: return "NEZ";
  case 3: return "ENZ";
  default: return "PNEZD";
  }
}

const char* BoundaryKindName(CadBoundaryKind k) {
  return k == CadBoundaryKind::Outer ? "Outer" : k == CadBoundaryKind::Hide ? "Hide"
         : k == CadBoundaryKind::Show ? "Show" : k == CadBoundaryKind::Mask ? "Mask" : "Clip";
}

QString StateChip(SurfaceState state) {
  return state == SurfaceState::Rebuilding ? "rebuilding" : state == SurfaceState::Stale ? "out of date" : "current";
}

/// What a definition item points at, for the tree row. An id that no longer resolves is SHOWN rather
/// than hidden: it is about to be pruned by the next rebuild, and a row that silently vanished would
/// leave nothing to explain the triangle count.
QString EntityText(const AppCommandState& st, std::uint64_t id) {
  const EntityRef ref = FindEntityById(st, id);
  if (!ref.valid()) return QString("(missing entity #%1)").arg(id);
  const char* kind = ref.kind == EntityKind::Line ? "Line" : ref.kind == EntityKind::Polyline ? "Polyline" : "Object";
  return QString("%1 #%2").arg(kind).arg(id);
}

struct ManagerState {
  int selected = 0;
  QString lastFingerprint;
  QSet<QString> seenSurfaces;
};

}  // namespace

void EditorWindow::ShowSurfaceManager() {
  if (auto* existing = findChild<QDialog*>("surfaceManager")) {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
  auto* dialog = new QDialog(this);
  dialog->setObjectName("surfaceManager");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("Surfaces");
  dialog->resize(960, 620);
  auto state = std::make_shared<ManagerState>();
  auto* root = new QVBoxLayout(dialog);
  surfaceui::AddBanner(root, dialog, "Surfaces",
                       "The definition of every surface in this drawing: point groups, breaklines, boundaries and point files.");
  auto* body = new QHBoxLayout;
  root->addLayout(body, 1);

  // Left: the definition tree and the actions on its selection.
  auto* left = new QVBoxLayout;
  left->addWidget(new QLabel("Surfaces:", dialog));
  auto* tree = new QTreeWidget(dialog);
  tree->setObjectName("surfaceManagerTree");
  tree->setHeaderHidden(true);
  tree->setMinimumWidth(360);
  tree->setContextMenuPolicy(Qt::CustomContextMenu);
  left->addWidget(tree, 1);
  auto* nodeActions = new QHBoxLayout;
  const auto makeButton = [&](const QString& text, const char* name) {
    auto* button = new QPushButton(text, dialog);
    button->setObjectName(name);
    return button;
  };
  auto* addButton = makeButton("Add...", "surfaceManagerAdd");
  auto* removeButton = makeButton("Remove", "surfaceManagerRemove");
  auto* upButton = makeButton("Move up", "surfaceManagerMoveUp");
  auto* downButton = makeButton("Move down", "surfaceManagerMoveDown");
  auto* importButton = makeButton("Import into drawing", "surfaceManagerImport");
  importButton->setToolTip("Breaks the link: the file's points become drawing survey points.");
  for (auto* button : {addButton, removeButton, upButton, downButton, importButton}) nodeActions->addWidget(button);
  left->addLayout(nodeActions);
  auto* newSurface = makeButton("New surface", "surfaceManagerNew");
  auto* newVolume = makeButton("New volume surface...", "surfaceManagerNewVolume");
  auto* newGroup = makeButton("New from group...", "surfaceManagerNewFromGroup");
  auto* deleteSurface = makeButton("Delete", "surfaceManagerDelete");
  for (auto* button : {newSurface, newVolume, newGroup, deleteSurface}) left->addWidget(button);
  body->addLayout(left);

  // Right: the selected surface.
  auto* pages = new QStackedWidget(dialog);
  pages->setObjectName("surfaceManagerPages");
  body->addWidget(pages, 1);
  auto* empty = new QLabel("No surfaces yet.\n\nA surface is triangulated from the points in one or more point groups. "
                           "Define a group under Survey > Groups, then create a surface from it here. Once it exists, "
                           "use Add... on Breaklines or Boundaries in the tree to add to its definition.", pages);
  empty->setObjectName("surfaceManagerEmpty");
  empty->setWordWrap(true);
  empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  pages->addWidget(empty);
  auto* panel = new QGroupBox("Selected surface", pages);
  auto* panelLayout = new QVBoxLayout(panel);
  auto* nameEdit = new QLineEdit(panel);
  nameEdit->setObjectName("surfaceManagerName");
  auto* nameForm = new QFormLayout;
  nameForm->addRow("Name", nameEdit);
  panelLayout->addLayout(nameForm);
  auto* volumeInfo = new QLabel(panel);
  volumeInfo->setObjectName("surfaceManagerVolume");
  panelLayout->addWidget(volumeInfo);
  auto* styleRow = new QHBoxLayout;
  auto* styleCombo = new QComboBox(panel);
  styleCombo->setObjectName("surfaceManagerStyle");
  auto* editStyle = makeButton("Edit...", "surfaceManagerEditStyle");
  styleRow->addWidget(new QLabel("Style", panel));
  styleRow->addWidget(styleCombo, 1);
  styleRow->addWidget(editStyle);
  panelLayout->addLayout(styleRow);
  const auto makeNote = [&](const char* name, const char* color) {
    auto* label = new QLabel(panel);
    label->setObjectName(name);
    label->setWordWrap(true);
    if (color) label->setStyleSheet(QString("color:%1;").arg(color));
    panelLayout->addWidget(label);
    return label;
  };
  auto* contoursNote = makeNote("surfaceManagerContoursNote", "#c98a1a");
  auto* styleNote = makeNote("surfaceManagerStyleNote", "#c98a1a");
  auto* stateLabel = makeNote("surfaceManagerState", nullptr);
  auto* builtLabel = makeNote("surfaceManagerBuilt", nullptr);
  auto* watershedLabel = makeNote("surfaceManagerWatershed", nullptr);
  auto* buildMessage = makeNote("surfaceManagerBuildMessage", nullptr);
  auto* definitionLabel = makeNote("surfaceManagerDefinition", nullptr);
  auto* rebuildRow = new QHBoxLayout;
  auto* rebuild = makeButton("Rebuild", "surfaceManagerRebuild");
  auto* rebuildHint = new QLabel("Rebuilds from the current definition. Edits rebuild on their own.", panel);
  rebuildHint->setWordWrap(true);
  rebuildRow->addWidget(rebuild);
  rebuildRow->addWidget(rebuildHint, 1);
  panelLayout->addLayout(rebuildRow);
  auto* message = makeNote("surfaceManagerMessage", "#c54646");
  panelLayout->addStretch();
  pages->addWidget(panel);

  auto* footer = new QHBoxLayout;
  footer->addStretch();
  auto* close = makeButton("Close", "surfaceManagerClose");
  close->setProperty("primary", true);
  footer->addWidget(close);
  root->addLayout(footer);
  surfaceui::StyleDialog(dialog);

  // --- helpers ----------------------------------------------------------------------------------
  const auto validSurface = [this, state] { return state->selected >= 0 && state->selected < static_cast<int>(m_State.cadSurfaces.size()); };
  const auto runModal = [this](QDialog& d) {
    const bool was = m_ModalOpen;
    m_ModalOpen = true;
    const int result = d.exec();
    m_ModalOpen = was;
    return result;
  };
  const auto currentNode = [tree]() -> QTreeWidgetItem* { return tree->currentItem(); };
  const auto nodeType = [](const QTreeWidgetItem* item) { return item ? item->data(0, kRoleType).toInt() : -1; };

  // The tree and the panel mirror the document; rebuilt only when what they would show has changed.
  const auto refresh = [this, state, tree, pages, nameEdit, volumeInfo, styleCombo, contoursNote, styleNote,
                        stateLabel, builtLabel, watershedLabel, buildMessage, definitionLabel, newVolume, newGroup,
                        deleteSurface, addButton, removeButton, upButton, downButton, importButton, validSurface, nodeType](bool force) {
    struct Row { QString text; int type; int surface; int index; int depth; };
    std::vector<Row> rows;
    for (size_t si = 0; si < m_State.cadSurfaces.size(); ++si) {
      const CadSurface& s = m_State.cadSurfaces[si];
      const SurfaceState st = SurfaceRebuildStateOf(m_State, si);
      rows.push_back({Q(s.name) + (st == SurfaceState::Current ? QString() : " [" + StateChip(st) + "]"), SurfaceNode, int(si), 0, 0});
      rows.push_back({"Definition", DefinitionNode, int(si), 0, 1});
      rows.push_back({QString("Point Groups (%1)").arg(s.sourcePointGroups.size()), GroupCategory, int(si), 0, 2});
      for (size_t i = 0; i < s.sourcePointGroups.size(); ++i) {
        const bool exists = FindPointGroupIndex(m_State, s.sourcePointGroups[i]) >= 0;
        rows.push_back({Q(s.sourcePointGroups[i]) + (exists ? QString() : "  (missing)"), GroupItem, int(si), int(i), 3});
      }
      rows.push_back({QString("Breaklines (%1)").arg(s.breaklines.size()), BreaklineCategory, int(si), 0, 2});
      for (size_t i = 0; i < s.breaklines.size(); ++i) {
        const QString what = EntityText(m_State, s.breaklines[i].entityId);
        rows.push_back({s.breaklines[i].description.empty() ? what : Q(s.breaklines[i].description) + "  -  " + what, BreaklineItem, int(si), int(i), 3});
      }
      rows.push_back({QString("Boundaries (%1)").arg(s.boundaries.size()), BoundaryCategory, int(si), 0, 2});
      for (size_t i = 0; i < s.boundaries.size(); ++i) {
        const CadSurfaceBoundary& b = s.boundaries[i];
        const QString what = EntityText(m_State, b.entityId);
        rows.push_back({b.name.empty() ? QString("%1  -  %2").arg(BoundaryKindName(b.kind), what)
                                       : QString("%1  (%2)  -  %3").arg(Q(b.name), BoundaryKindName(b.kind), what),
                        BoundaryItem, int(si), int(i), 3});
      }
      rows.push_back({QString("Point Files (%1)").arg(s.sourcePointFiles.size()), FileCategory, int(si), 0, 2});
      for (size_t i = 0; i < s.sourcePointFiles.size(); ++i) {
        const CadSurfacePointFile& f = s.sourcePointFiles[i];
        rows.push_back({QString("%1  (%2%3)").arg(Q(f.path), LayoutName(f.layoutIndex), f.skipFirstRow ? ", header" : ""), FileItem, int(si), int(i), 3});
      }
    }
    QString fingerprint;
    for (const Row& r : rows) fingerprint += QString("%1|%2|%3|%4;").arg(r.depth).arg(r.type).arg(r.index).arg(r.text);
    if (force || fingerprint != state->lastFingerprint) {
      state->lastFingerprint = fingerprint;
      // Keep the user's place: the selected node is found again by (type, surface, index).
      int keepType = nodeType(tree->currentItem()), keepSurface = -1, keepIndex = 0;
      if (tree->currentItem()) {
        keepSurface = tree->currentItem()->data(0, kRoleSurface).toInt();
        keepIndex = tree->currentItem()->data(0, kRoleIndex).toInt();
      }
      QSet<QString> expanded;
      for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->isExpanded()) expanded.insert(QString("%1/%2").arg((*it)->data(0, kRoleSurface).toInt()).arg((*it)->data(0, kRoleType).toInt()));
      const QSignalBlocker blocker(tree);
      tree->clear();
      QTreeWidgetItem* surfaceItem = nullptr;
      QTreeWidgetItem* definitionItem = nullptr;
      QTreeWidgetItem* categoryItem = nullptr;
      QTreeWidgetItem* restore = nullptr;
      bool newSurface = false;
      for (const Row& r : rows) {
        QTreeWidgetItem* parent = r.depth == 0 ? nullptr : r.depth == 1 ? surfaceItem : r.depth == 2 ? definitionItem : categoryItem;
        auto* item = parent ? new QTreeWidgetItem(parent, {r.text}) : new QTreeWidgetItem(tree, {r.text});
        item->setData(0, kRoleType, r.type);
        item->setData(0, kRoleSurface, r.surface);
        item->setData(0, kRoleIndex, r.index);
        if (r.depth == 0) surfaceItem = item;
        if (r.depth == 1) definitionItem = item;
        if (r.depth == 2) categoryItem = item;
        if (r.type == keepType && r.surface == keepSurface && r.index == keepIndex) restore = item;
        const QString key = QString("%1/%2").arg(r.surface).arg(r.type);
        if (r.type == SurfaceNode || r.type == DefinitionNode) {
          const QString surfaceName = Q(m_State.cadSurfaces[static_cast<size_t>(r.surface)].name);
          if (r.type == SurfaceNode) newSurface = !state->seenSurfaces.contains(surfaceName);
          item->setExpanded(newSurface || expanded.contains(key));
          if (r.type == DefinitionNode) state->seenSurfaces.insert(surfaceName);
        } else if (r.depth == 2) {
          item->setExpanded(expanded.contains(key));
        }
      }
      if (restore) tree->setCurrentItem(restore);
      else if (tree->topLevelItemCount() > 0) tree->setCurrentItem(tree->topLevelItem(std::min(state->selected, tree->topLevelItemCount() - 1)));
    }
    if (tree->currentItem()) state->selected = tree->currentItem()->data(0, kRoleSurface).toInt();
    if (state->selected >= static_cast<int>(m_State.cadSurfaces.size())) state->selected = static_cast<int>(m_State.cadSurfaces.size()) - 1;
    if (state->selected < 0) state->selected = 0;

    // Footer buttons.
    newVolume->setEnabled(m_State.cadSurfaces.size() >= 2);
    newVolume->setToolTip(m_State.cadSurfaces.size() < 2 ? "A volume surface needs two existing TIN surfaces." : QString());
    newGroup->setEnabled(!m_State.pointGroups.empty());
    newGroup->setToolTip(m_State.pointGroups.empty() ? "A surface is built from point groups - create one in Survey > Groups first." : QString());
    deleteSurface->setEnabled(!m_State.cadSurfaces.empty());

    // Node buttons follow the selected node.
    const int type = nodeType(tree->currentItem());
    const bool category = type == GroupCategory || type == BreaklineCategory || type == BoundaryCategory || type == FileCategory;
    const bool item = type == GroupItem || type == BreaklineItem || type == BoundaryItem || type == FileItem;
    const int itemIndex = tree->currentItem() ? tree->currentItem()->data(0, kRoleIndex).toInt() : 0;
    int count = 0;
    if (validSurface() && (type == BreaklineItem || type == BoundaryItem)) {
      const CadSurface& s = m_State.cadSurfaces[static_cast<size_t>(state->selected)];
      count = static_cast<int>(type == BreaklineItem ? s.breaklines.size() : s.boundaries.size());
    }
    addButton->setEnabled(category);
    removeButton->setEnabled(item);
    removeButton->setText(type == FileItem ? "Remove link" : "Remove");
    upButton->setEnabled((type == BreaklineItem || type == BoundaryItem) && itemIndex > 0);
    downButton->setEnabled((type == BreaklineItem || type == BoundaryItem) && itemIndex + 1 < count);
    importButton->setEnabled(type == FileItem);

    // Right panel.
    if (m_State.cadSurfaces.empty()) {
      pages->setCurrentIndex(0);
      return;
    }
    pages->setCurrentIndex(1);
    const CadSurface& s = m_State.cadSurfaces[static_cast<size_t>(state->selected)];
    if (!nameEdit->hasFocus() && nameEdit->text() != Q(s.name)) nameEdit->setText(Q(s.name));
    volumeInfo->setVisible(s.isVolumeSurface());
    volumeInfo->setText(QString("TIN volume surface\nBase: %1\nComparison: %2").arg(Q(s.volumeBaseName), Q(s.volumeComparisonName)));
    SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
    const SurfaceStyle* resolved = SurfaceStyles::Resolve(m_State.surfaceStyles, s.styleName);
    {
      const QSignalBlocker blocker(styleCombo);
      styleCombo->clear();
      for (const SurfaceStyle& st : m_State.surfaceStyles) styleCombo->addItem(Q(st.name));
      styleCombo->setCurrentText(resolved ? Q(resolved->name) : QString());
    }
    // REQ-201: contours the display path declined to generate are said out loud.
    int levelsAsked = 0;
    contoursNote->setText(SurfaceContoursSuppressed(m_State, static_cast<size_t>(state->selected), &levelsAsked)
                              ? QString("Contours not drawn: this interval asks for %1 levels over the surface's elevation range. "
                                        "Use a larger contour interval in the style.").arg(levelsAsked)
                              : QString());
    // REQ-070's deleted-style fallback, said out loud rather than silently substituted.
    const bool fellBack = resolved && !s.styleName.empty() && resolved->name != s.styleName;
    styleNote->setText(fellBack ? QString("Style \"%1\" no longer exists \xe2\x80\x94 drawing with \"%2\".").arg(Q(s.styleName), Q(resolved->name)) : QString());
    stateLabel->setText("Status: " + StateChip(SurfaceRebuildStateOf(m_State, static_cast<size_t>(state->selected))));
    if (!s.tin) {
      builtLabel->setText("Not built.");
      watershedLabel->clear();
    } else {
      QString text = QString("%1 points, %2 triangles.").arg(s.vertexCount()).arg(s.triangleCount());
      double lo = 0.0, hi = 0.0;
      if (surfedit::ElevationRange(s, &lo, &hi)) text += QString("\nElevation %1 to %2 (%3 range).").arg(lo, 0, 'f', 2).arg(hi, 0, 'f', 2).arg(hi - lo, 0, 'f', 2);
      builtLabel->setText(text);
      QString watershed;
      const std::uint64_t sid = m_State.cadSurfaceAttrs[static_cast<size_t>(state->selected)].id;
      for (const auto& entry : m_State.surfaceWatershedCache) {
        if (entry.surfaceId != sid || !entry.analysis.ok) continue;
        watershed = QString("Watershed: %1 basin(s).").arg(entry.analysis.basins.size());
        for (const WatershedBasin& b : entry.analysis.basins) {
          const char* kind = b.drain == DrainKind::Depression ? "depression" : b.drain == DrainKind::Flat ? "flat" : "boundary";
          watershed += QString("\n  #%1 %2  area %3").arg(b.id).arg(kind).arg(b.area2d, 0, 'f', 2);
        }
        break;
      }
      watershedLabel->setText(watershed);
    }
    buildMessage->setText(Q(s.lastBuildMessage));
    definitionLabel->setText(QString("Definition: %1 group(s), %2 breakline(s), %3 boundary(ies).")
                                 .arg(s.sourcePointGroups.size()).arg(s.breaklines.size()).arg(s.boundaries.size()));
  };

  // --- actions ----------------------------------------------------------------------------------
  const auto afterEdit = [this, refresh] {
    refresh(false);
    Refresh();
  };
  const auto say = [message](const QString& text) { message->setText(text); };
  const auto selectSurface = [state, tree, refresh](int index) {
    state->selected = index;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->data(0, kRoleSurface).toInt() == index) tree->setCurrentItem(tree->topLevelItem(i));
    refresh(false);
  };

  const auto addGroupDialog = [this, state, dialog, runModal, validSurface] {
    if (!validSurface()) return;
    QDialog entry(dialog);
    entry.setObjectName("surfaceAddPointGroup");
    entry.setWindowTitle("Add Point Group");
    auto* layout = new QVBoxLayout(&entry);
    layout->addWidget(new QLabel("Add a point group to this surface:", &entry));
    auto* list = new QListWidget(&entry);
    list->setObjectName("surfaceAddPointGroupList");
    const CadSurface& s = m_State.cadSurfaces[static_cast<size_t>(state->selected)];
    for (const auto& group : m_State.pointGroups) {
      if (std::find(s.sourcePointGroups.begin(), s.sourcePointGroups.end(), group.name) != s.sourcePointGroups.end()) continue;
      auto* item = new QListWidgetItem(QString("%1  (%2)").arg(Q(group.name)).arg(ResolvePointGroup(m_State, group, nullptr).size()), list);
      item->setData(Qt::UserRole, Q(group.name));
    }
    if (list->count() == 0) layout->addWidget(new QLabel("(every point group is already in this definition)", &entry));
    else layout->addWidget(list);
    auto* cancel = new QPushButton("Cancel", &entry);
    cancel->setObjectName("surfaceAddPointGroupCancel");
    layout->addWidget(cancel, 0, Qt::AlignRight);
    QObject::connect(cancel, &QPushButton::clicked, &entry, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemActivated, &entry, [&](QListWidgetItem* item) {
      surfedit::AddPointGroup(m_State, state->selected, S(item->data(Qt::UserRole).toString()), m_Log);
      entry.accept();
    });
    QObject::connect(list, &QListWidget::itemClicked, &entry, [&](QListWidgetItem* item) {
      surfedit::AddPointGroup(m_State, state->selected, S(item->data(Qt::UserRole).toString()), m_Log);
      entry.accept();
    });
    runModal(entry);
  };
  // The OK on these two only STARTS the DESIGNATE command: the entity is then picked in the drawing.
  const auto addDesignateDialog = [this, state, dialog, runModal, validSurface](bool boundary) {
    if (!validSurface()) return;
    QDialog entry(dialog);
    entry.setObjectName(boundary ? "surfaceAddBoundary" : "surfaceAddBreakline");
    entry.setWindowTitle(boundary ? "Add Boundaries" : "Add Breaklines");
    auto* layout = new QFormLayout(&entry);
    auto* text = new QLineEdit(&entry);
    text->setObjectName("surfaceAddText");
    auto* type = new QComboBox(&entry);
    type->setObjectName("surfaceAddType");
    if (boundary) type->addItems({"Outer", "Hide", "Show", "Clip", "Mask"});
    else type->addItem("Standard");  // the engine has one breakline type; ADR-028 defers the rest
    layout->addRow(boundary ? "Name:" : "Description:", text);
    layout->addRow("Type:", type);
    layout->addRow(new QLabel(boundary ? "OK, then pick a CLOSED polyline in the drawing." : "OK, then pick a line or polyline in the drawing.", &entry));
    auto* ok = new QPushButton("OK", &entry);
    ok->setObjectName("surfaceAddOk");
    ok->setDefault(true);
    auto* cancel = new QPushButton("Cancel", &entry);
    cancel->setObjectName("surfaceAddCancel");
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(ok);
    row->addWidget(cancel);
    layout->addRow(row);
    QObject::connect(ok, &QPushButton::clicked, &entry, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &entry, &QDialog::reject);
    if (runModal(entry) != QDialog::Accepted || !validSurface()) return;
    const std::string surfaceName = m_State.cadSurfaces[static_cast<size_t>(state->selected)].name;
    if (boundary) {
      static const CadBoundaryKind kinds[] = {CadBoundaryKind::Outer, CadBoundaryKind::Hide, CadBoundaryKind::Show, CadBoundaryKind::Clip, CadBoundaryKind::Mask};
      m_State.designateBoundaryName = S(text->text());
      StartDesignateBoundaryCommand(m_State, surfaceName, kinds[std::clamp(type->currentIndex(), 0, 4)], m_Log);
    } else {
      m_State.designateBreaklineDescription = S(text->text());
      StartDesignateBreaklineCommand(m_State, surfaceName, m_Log);
    }
  };
  const auto addFileDialog = [this, state, dialog, runModal, validSurface] {
    if (!validSurface()) return;
    QDialog entry(dialog);
    entry.setObjectName("surfaceAddPointFile");
    entry.setWindowTitle("Add Point File");
    entry.setMinimumWidth(560);
    auto* layout = new QFormLayout(&entry);
    auto* path = new QLineEdit(&entry);
    path->setObjectName("surfaceAddFilePath");
    auto* browse = new QPushButton("Browse...", &entry);
    browse->setObjectName("surfaceAddFileBrowse");
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(path, 1);
    pathRow->addWidget(browse);
    layout->addRow("Point file:", pathRow);
    // A point file does not describe its own column order, so the layout is stated, not guessed --
    // guessing would swap northing for easting on a file that happens to parse either way.
    auto* columns = new QComboBox(&entry);
    columns->setObjectName("surfaceAddFileLayout");
    columns->addItems({"PNEZD", "PENZD", "NEZ", "ENZ"});
    auto* header = new QCheckBox("First row is a header", &entry);
    header->setObjectName("surfaceAddFileHeader");
    layout->addRow("Column layout:", columns);
    layout->addRow(header);
    auto* note = new QLabel("The file stays LINKED: it is re-read whenever the surface rebuilds, and its points do not "
                            "become drawing survey points. Use \"Import into drawing\" on the item to change that.", &entry);
    note->setWordWrap(true);
    layout->addRow(note);
    auto* ok = new QPushButton("OK", &entry);
    ok->setObjectName("surfaceAddOk");
    ok->setDefault(true);
    ok->setEnabled(false);
    auto* cancel = new QPushButton("Cancel", &entry);
    cancel->setObjectName("surfaceAddCancel");
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(ok);
    row->addWidget(cancel);
    layout->addRow(row);
    QObject::connect(path, &QLineEdit::textChanged, &entry, [ok](const QString& text) { ok->setEnabled(!text.isEmpty()); });
    QObject::connect(browse, &QPushButton::clicked, &entry, [&] {
      const QString picked = QFileDialog::getOpenFileName(&entry, "Point file", {}, "Point files (*.csv *.txt *.xyz *.pts);;All files (*)");
      if (!picked.isEmpty()) path->setText(picked);
    });
    QObject::connect(ok, &QPushButton::clicked, &entry, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &entry, &QDialog::reject);
    if (runModal(entry) != QDialog::Accepted) return;
    surfedit::AddPointFile(m_State, state->selected, S(path->text()), S(columns->currentText()), header->isChecked(), m_Log);
  };

  const auto addToNode = [this, currentNode, nodeType, addGroupDialog, addDesignateDialog, addFileDialog, state, afterEdit] {
    QTreeWidgetItem* item = currentNode();
    if (!item) return;
    state->selected = item->data(0, kRoleSurface).toInt();
    switch (nodeType(item)) {
    case GroupCategory: addGroupDialog(); break;
    case BreaklineCategory: addDesignateDialog(false); break;
    case BoundaryCategory: addDesignateDialog(true); break;
    case FileCategory: addFileDialog(); break;
    default: return;
    }
    afterEdit();
  };
  const auto removeNode = [this, currentNode, nodeType, afterEdit] {
    QTreeWidgetItem* item = currentNode();
    if (!item) return;
    const int type = nodeType(item);
    if (type < GroupItem) return;
    surfedit::RemoveDefinitionItem(m_State, item->data(0, kRoleSurface).toInt(), static_cast<surfedit::DefinitionKind>(type - GroupItem),
                                   static_cast<size_t>(item->data(0, kRoleIndex).toInt()), m_Log);
    afterEdit();
  };
  const auto moveNode = [this, currentNode, nodeType, afterEdit, tree](int delta) {
    QTreeWidgetItem* item = currentNode();
    if (!item) return;
    const int type = nodeType(item);
    if (type != BreaklineItem && type != BoundaryItem) return;
    const int surface = item->data(0, kRoleSurface).toInt();
    const int index = item->data(0, kRoleIndex).toInt();
    if (surfedit::MoveDefinitionItem(m_State, surface, static_cast<surfedit::DefinitionKind>(type - GroupItem), static_cast<size_t>(index), delta, m_Log)) {
      afterEdit();
      for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, kRoleType).toInt() == type && (*it)->data(0, kRoleSurface).toInt() == surface && (*it)->data(0, kRoleIndex).toInt() == index + delta)
          tree->setCurrentItem(*it);
      afterEdit();
    }
  };
  const auto importNode = [this, currentNode, nodeType, afterEdit] {
    QTreeWidgetItem* item = currentNode();
    if (!item || nodeType(item) != FileItem) return;
    surfedit::ImportPointFile(m_State, item->data(0, kRoleSurface).toInt(), static_cast<size_t>(item->data(0, kRoleIndex).toInt()), m_Log);
    afterEdit();
  };

  QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog, [refresh, say] { say({}); refresh(false); });
  QObject::connect(addButton, &QPushButton::clicked, dialog, addToNode);
  QObject::connect(removeButton, &QPushButton::clicked, dialog, removeNode);
  QObject::connect(upButton, &QPushButton::clicked, dialog, [moveNode] { moveNode(-1); });
  QObject::connect(downButton, &QPushButton::clicked, dialog, [moveNode] { moveNode(1); });
  QObject::connect(importButton, &QPushButton::clicked, dialog, importNode);
  QObject::connect(tree, &QWidget::customContextMenuRequested, dialog, [=](const QPoint& position) {
    QTreeWidgetItem* item = tree->itemAt(position);
    if (!item) return;
    tree->setCurrentItem(item);
    state->selected = item->data(0, kRoleSurface).toInt();
    const int type = nodeType(item);
    QMenu menu(dialog);
    menu.setObjectName("surfaceManagerMenu");
    const auto add = [&](const QString& text, bool enabled, const std::function<void()>& action) {
      QAction* entry = menu.addAction(text);
      entry->setEnabled(enabled);
      QObject::connect(entry, &QAction::triggered, dialog, action);
    };
    const int count = [&] {
      const CadSurface& s = m_State.cadSurfaces[static_cast<size_t>(state->selected)];
      return static_cast<int>(type == BreaklineItem ? s.breaklines.size() : type == BoundaryItem ? s.boundaries.size() : 0);
    }();
    const int index = item->data(0, kRoleIndex).toInt();
    if (type == SurfaceNode) {
      add("Rebuild now", true, [=] { surfedit::RebuildSurface(m_State, state->selected, m_Log); Refresh(); refresh(false); });
      menu.addSeparator();
      add("Delete surface", true, [=] { surfedit::DeleteSurface(m_State, state->selected, m_Log); Refresh(); refresh(false); });
    } else if (type >= GroupCategory && type <= FileCategory) {
      add("Add...", true, addToNode);
    } else if (type == FileItem) {
      add("Import into drawing (breaks the link)", true, importNode);
      menu.addSeparator();
      add("Remove link", true, removeNode);
    } else if (type >= GroupItem) {
      add("Remove", true, removeNode);
      if (type == BreaklineItem || type == BoundaryItem) {
        menu.addSeparator();
        add("Move up", index > 0, [moveNode] { moveNode(-1); });
        add("Move down", index + 1 < count, [moveNode] { moveNode(1); });
      }
    }
    if (!menu.isEmpty()) menu.exec(tree->viewport()->mapToGlobal(position));
  });

  // Surface panel.
  QObject::connect(nameEdit, &QLineEdit::editingFinished, dialog, [=] {
    if (!validSurface() || S(nameEdit->text()) == m_State.cadSurfaces[static_cast<size_t>(state->selected)].name) return;
    if (!surfedit::RenameSurface(m_State, state->selected, S(nameEdit->text()), m_Log)) {
      say(Q(m_Log.empty() ? std::string() : m_Log.back()));
      nameEdit->setText(Q(m_State.cadSurfaces[static_cast<size_t>(state->selected)].name));
      return;
    }
    say({});
    afterEdit();
  });
  QObject::connect(styleCombo, &QComboBox::activated, dialog, [=](int) {
    if (!validSurface()) return;
    surfedit::AssignSurfaceStyle(m_State, state->selected, S(styleCombo->currentText()), m_Log);
    afterEdit();
  });
  QObject::connect(editStyle, &QPushButton::clicked, dialog, [=] {
    if (!validSurface()) return;
    // Edit... copies the style for THIS surface when others still share it, so toggling triangles here
    // does not rewrite every TIN (REQ-070).
    SurfaceStyles::EnsureStandard(m_State.surfaceStyles);
    const CadSurface& s = m_State.cadSurfaces[static_cast<size_t>(state->selected)];
    const SurfaceStyle* resolved = SurfaceStyles::Resolve(m_State.surfaceStyles, s.styleName);
    const int users = resolved ? SurfaceStyles::CountSurfacesResolvingTo(m_State.cadSurfaces, m_State.surfaceStyles, resolved->name) : 0;
    if (users > 1) {
      PushUndoSnapshot(m_State, "Surface style");
      DetachSurfaceStyleIfShared(m_State, static_cast<size_t>(state->selected), &m_Log);
    }
    const SurfaceStyle* now = SurfaceStyles::Resolve(m_State.surfaceStyles, m_State.cadSurfaces[static_cast<size_t>(state->selected)].styleName);
    const bool was = m_ModalOpen;
    ShowSurfaceStyles(now ? Q(now->name) : QString(), true);
    m_ModalOpen = was;
    afterEdit();
  });
  QObject::connect(rebuild, &QPushButton::clicked, dialog, [=] {
    if (!validSurface()) return;
    surfedit::RebuildSurface(m_State, state->selected, m_Log);
    afterEdit();
  });
  QObject::connect(newSurface, &QPushButton::clicked, dialog, [=] {
    const int created = surfedit::CreateFromPointGroups(m_State, surfedit::NextSurfaceName(m_State), {}, m_Log);
    Refresh();
    if (created >= 0) selectSurface(created);
    refresh(true);
  });
  QObject::connect(deleteSurface, &QPushButton::clicked, dialog, [=] {
    if (!validSurface()) return;
    surfedit::DeleteSurface(m_State, state->selected, m_Log);
    state->selected = std::min(state->selected, static_cast<int>(m_State.cadSurfaces.size()) - 1);
    Refresh();
    refresh(true);
  });
  QObject::connect(newGroup, &QPushButton::clicked, dialog, [=] {
    QDialog entry(dialog);
    entry.setObjectName("surfaceNewFromGroup");
    entry.setWindowTitle("New Surface");
    auto* layout = new QVBoxLayout(&entry);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(Q(surfedit::NextSurfaceName(m_State)), &entry);
    name->setObjectName("surfaceNewName");
    form->addRow("Name", name);
    layout->addLayout(form);
    layout->addWidget(new QLabel("Build from point groups:", &entry));
    auto* groups = new QListWidget(&entry);
    groups->setObjectName("surfaceNewGroups");
    for (const auto& group : m_State.pointGroups) {
      auto* item = new QListWidgetItem(QString("%1  (%2)").arg(Q(group.name)).arg(ResolvePointGroup(m_State, group, nullptr).size()), groups);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Unchecked);
      item->setData(Qt::UserRole, Q(group.name));
      item->setData(Qt::UserRole + 1, static_cast<int>(ResolvePointGroup(m_State, group, nullptr).size()));
    }
    layout->addWidget(groups);
    auto* total = new QLabel(&entry);
    total->setObjectName("surfaceNewTotal");
    layout->addWidget(total);
    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* createEmpty = new QPushButton("Create empty", &entry);
    createEmpty->setObjectName("surfaceNewEmpty");
    auto* create = new QPushButton("Create", &entry);
    create->setObjectName("surfaceNewCreate");
    create->setDefault(true);
    auto* cancel = new QPushButton("Cancel", &entry);
    cancel->setObjectName("surfaceNewCancel");
    row->addWidget(createEmpty);
    row->addWidget(create);
    row->addWidget(cancel);
    layout->addLayout(row);
    const auto picked = [&] {
      std::vector<std::string> names;
      int points = 0;
      for (int i = 0; i < groups->count(); ++i)
        if (groups->item(i)->checkState() == Qt::Checked) {
          names.push_back(S(groups->item(i)->data(Qt::UserRole).toString()));
          points += groups->item(i)->data(Qt::UserRole + 1).toInt();
        }
      return std::make_pair(names, points);
    };
    const auto validate = [&] {
      const auto [names, points] = picked();
      total->setText(points < 3 ? QString("Need at least 3 points; %1 selected.").arg(points) : QString("%1 point(s) selected.").arg(points));
      createEmpty->setEnabled(!name->text().isEmpty());
      create->setEnabled(points >= 3 && !name->text().isEmpty());
    };
    QObject::connect(groups, &QListWidget::itemChanged, &entry, [&](QListWidgetItem*) { validate(); });
    QObject::connect(name, &QLineEdit::textChanged, &entry, [&](const QString&) { validate(); });
    QObject::connect(cancel, &QPushButton::clicked, &entry, &QDialog::reject);
    int created = -1;
    QObject::connect(createEmpty, &QPushButton::clicked, &entry, [&] {
      created = surfedit::CreateFromPointGroups(m_State, S(name->text()), {}, m_Log);
      entry.accept();
    });
    QObject::connect(create, &QPushButton::clicked, &entry, [&] {
      created = surfedit::CreateFromPointGroups(m_State, S(name->text()), picked().first, m_Log);
      entry.accept();
    });
    validate();
    runModal(entry);
    Refresh();
    if (created >= 0) selectSurface(created);
    refresh(true);
  });
  QObject::connect(newVolume, &QPushButton::clicked, dialog, [=] {
    QDialog entry(dialog);
    entry.setObjectName("surfaceNewVolume");
    entry.setWindowTitle("New Volume Surface");
    auto* form = new QFormLayout(&entry);
    auto* name = new QLineEdit(Q(surfedit::NextSurfaceName(m_State)), &entry);
    name->setObjectName("surfaceVolumeName");
    auto* base = new QComboBox(&entry);
    base->setObjectName("surfaceVolumeBase");
    auto* comparison = new QComboBox(&entry);
    comparison->setObjectName("surfaceVolumeComparison");
    for (const CadSurface& s : m_State.cadSurfaces) {
      if (s.isVolumeSurface()) continue;
      base->addItem(Q(s.name));
      comparison->addItem(Q(s.name));
    }
    if (comparison->count() > 1) comparison->setCurrentIndex(1);
    auto* warning = new QLabel(&entry);
    warning->setObjectName("surfaceVolumeWarning");
    warning->setStyleSheet("color:#c98a1a;");
    auto* ok = new QPushButton("Create", &entry);
    ok->setObjectName("surfaceVolumeCreate");
    auto* cancel = new QPushButton("Cancel", &entry);
    cancel->setObjectName("surfaceVolumeCancel");
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(ok);
    row->addWidget(cancel);
    form->addRow("Name", name);
    form->addRow("Base surface", base);
    form->addRow("Comparison surface", comparison);
    form->addRow(warning);
    form->addRow(row);
    const auto validate = [&] {
      const bool same = base->currentIndex() == comparison->currentIndex();
      warning->setText(same ? "Base and comparison must differ." : QString());
      ok->setEnabled(!name->text().isEmpty() && !same);
    };
    QObject::connect(name, &QLineEdit::textChanged, &entry, [&](const QString&) { validate(); });
    QObject::connect(base, &QComboBox::currentIndexChanged, &entry, [&](int) { validate(); });
    QObject::connect(comparison, &QComboBox::currentIndexChanged, &entry, [&](int) { validate(); });
    QObject::connect(cancel, &QPushButton::clicked, &entry, &QDialog::reject);
    int created = -1;
    QObject::connect(ok, &QPushButton::clicked, &entry, [&] {
      created = surfedit::CreateVolumeFromParents(m_State, S(name->text()), S(base->currentText()), S(comparison->currentText()), m_Log);
      entry.accept();
    });
    validate();
    runModal(entry);
    Refresh();
    if (created >= 0) selectSurface(created);
    refresh(true);
  });
  QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);

  auto* timer = new QTimer(dialog);
  timer->setInterval(400);
  QObject::connect(timer, &QTimer::timeout, dialog, [refresh] { refresh(false); });
  timer->start();
  refresh(true);
  dialog->show();
}
