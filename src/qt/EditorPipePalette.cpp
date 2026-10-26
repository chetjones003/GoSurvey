// REQ-350 / TASK-275 / GitHub issue #1084 — the Pipe Fittings palette in the Qt editor: the library
// parts that fit the pipe run being routed, grouped by category, each with a shaded picture and its
// name.
//
// Which parts belong on which tab is decided by `CadPipePaletteCollectRows` in the command layer
// (shared, unit-tested without a window). The pictures are rendered by `ViewportRenderer` (ADR-062)
// and handed to the palette as images: this file contains no `gl*` call.

#include "EditorPipePalette.hpp"

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "EditorViewport.hpp"
#include "EditorWindow.hpp"
#include "ViewportRenderer.hpp"
#include "util/brep.hpp"

#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QAbstractItemView>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

/// Thumbnail edge length, in pixels. Big enough to tell a weld-neck flange from a blind one, small
/// enough that a tab full of parts is a short list rather than a slideshow.
constexpr int kPipeFittingThumbPx = 64;

/// How many thumbnails may be rendered in one paint (ADR-062). Each is a small offscreen pass, and
/// they are one-off — but a library of a hundred matching parts must not turn the first paint into a
/// hundred render passes, so the work is spread over consecutive paints. A row waiting for its
/// picture shows a placeholder for a paint or two.
constexpr int kPipeFittingThumbsPerPass = 4;

/// Connection-port role colours, matching the BEDIT authoring gizmo and the INSERT library pane, so a
/// port means the same thing everywhere it is drawn.
void PortRoleColor(CadBlockConnectionRole role, float* rgba) {
  switch (role) {
    case CadBlockConnectionRole::Outlet:
      rgba[0] = 0.35f; rgba[1] = 0.63f; rgba[2] = 0.94f; break;
    case CadBlockConnectionRole::Branch:
      rgba[0] = 0.94f; rgba[1] = 0.63f; rgba[2] = 0.24f; break;
    case CadBlockConnectionRole::Inlet:
    default:
      rgba[0] = 0.35f; rgba[1] = 0.86f; rgba[2] = 0.47f; break;
  }
  rgba[3] = 1.f;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

/// The tab the palette is showing, clamped — `pipeFittingPaletteTab` is plain session state and a
/// stale/garbage value must not index past the category list.
CadPipePaletteCategory ActiveCategory(const AppCommandState& cmd) {
  const int t = std::clamp(cmd.pipeFittingPaletteTab, 0, kCadPipePaletteCategoryCount - 1);
  return static_cast<CadPipePaletteCategory>(t);
}

/// The empty box a row shows while its picture is still being rendered, and for good when the part
/// has no geometry to picture. The row is fully usable either way; the picture is an aid.
QIcon PlaceholderIcon(const QColor& line) {
  QPixmap pixmap(kPipeFittingThumbPx, kPipeFittingThumbPx);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(line);
  painter.drawRect(0, 0, kPipeFittingThumbPx - 1, kPipeFittingThumbPx - 1);
  return QIcon(pixmap);
}

QString RowSubtitle(const CadBlockLibraryEntry& e) {
  std::string sub(CadPipePartTypeTag(e.partType));
  if (!e.nominalSize.empty())
    sub = e.nominalSize + "  " + sub;
  if (e.pressureClass != CadPipePressureClass::None)
    sub += "  " + std::string(CadPipePressureClassTag(e.pressureClass));
  return QString::fromUtf8(sub.c_str());
}

} // namespace

bool ServicePipeFittingThumbnails(AppCommandState& cmd, ViewportRenderer& renderer, PartThumbnailImages& images) {
  bool changed = false;
  // Parts edited in BEDIT since the last pass: drop their cached pictures so the next request draws
  // the part as it is now (ADR-062 (c)). Cleared even when nothing was cached, since the point is
  // that the name has been dealt with.
  for (const std::string& stale : cmd.pipeFittingThumbStale) {
    renderer.InvalidatePartThumbnail(stale);
    changed |= images.erase(stale) > 0;
    const auto it = std::find(cmd.pipeFittingThumbUnavailable.begin(),
                              cmd.pipeFittingThumbUnavailable.end(), stale);
    if (it != cmd.pipeFittingThumbUnavailable.end())
      cmd.pipeFittingThumbUnavailable.erase(it);  // it may have geometry now
  }
  cmd.pipeFittingThumbStale.clear();

  int done = 0;
  while (!cmd.pipeFittingThumbRequests.empty() && done < kPipeFittingThumbsPerPass) {
    const std::string name = cmd.pipeFittingThumbRequests.front();
    cmd.pipeFittingThumbRequests.erase(cmd.pipeFittingThumbRequests.begin());
    if (images.count(name) != 0)
      continue;  // already pictured; the request raced with an earlier one for the same name
    ++done;

    // The part's geometry, from the drawing when it is already there, and otherwise imported into a
    // SCRATCH state purely to be pictured. Scratch, and not the drawing, because LISTING a part must
    // not add a block definition to the user's drawing — that is what placing one does.
    std::unique_ptr<AppCommandState> scratch;  // owns the preview-only import for this iteration
    const std::vector<CadBlockDefinition>* defs = &cmd.blockDefs;
    int di = CadBlockFindDef(cmd.blockDefs, name);
    if (di < 0) {
      const auto row = std::find_if(cmd.pipeFittingLibraryCache.begin(), cmd.pipeFittingLibraryCache.end(),
                                    [&](const CadBlockLibraryEntry& e) { return e.name == name; });
      if (row != cmd.pipeFittingLibraryCache.end()) {
        scratch = std::make_unique<AppCommandState>();
        std::vector<std::string> quiet;  // an unreadable library file is not the command line's business
        if (CadBlocksImportLibraryEntry(*scratch, *row, quiet)) {
          di = CadBlockFindDef(scratch->blockDefs, name);
          defs = &scratch->blockDefs;
        }
      }
    }
    if (di < 0) {
      // No such part, or it could not be read. Remembered so the row is asked about once rather than
      // re-imported every pass; the row still lists by name (ADR-062's degrade-to-name-only).
      if (!Contains(cmd.pipeFittingThumbUnavailable, name))
        cmd.pipeFittingThumbUnavailable.push_back(name);
      continue;
    }
    const CadBlockDefinition& def = (*defs)[static_cast<size_t>(di)];

    // The part at its own origin, unscaled: a preview is a picture of the DEFINITION, not of a
    // placement, so there is no insertion transform to apply.
    CadBlockRef ref;
    ref.defName = def.name;

    std::vector<float> triVerts;
    std::vector<float> triNormals;
    std::vector<float> edgeVerts;

    std::vector<CadBlockWorldSolid> solids;
    CadBlockCollectWorldSolids(*defs, ref, EntityAttributes{}, &solids);
    for (const CadBlockWorldSolid& ws : solids) {
      if (!ws.solid)
        continue;
      brep::Tessellation tess;
      brep::Problem why = brep::Problem::Ok;
      if (brep::Tessellate(*ws.solid, kSolidChordToleranceFt, &tess, &why)) {
        std::vector<float> v;
        std::vector<float> n;
        std::vector<int> faceIds;  // unused here; the thumbnail has no sub-object picking
        ExpandTessellation(tess, &v, &n, &faceIds);
        triVerts.insert(triVerts.end(), v.begin(), v.end());
        triNormals.insert(triNormals.end(), n.begin(), n.end());
      }
      std::vector<double> edges;
      if (brep::TessellateEdges(*ws.solid, kSolidChordToleranceFt, &edges, &why)) {
        for (double e : edges)
          edgeVerts.push_back(static_cast<float>(e));
      }
    }

    // A part may also carry 2D linework — and a 2D-only block carries nothing else, so this is what
    // gives it a picture instead of an empty box.
    std::vector<CadBlockWorldSeg> segs;
    CadBlockCollectWorldLines(*defs, ref, EntityAttributes{}, &segs);
    for (const CadBlockWorldSeg& s : segs) {
      edgeVerts.push_back(s.x0);
      edgeVerts.push_back(s.y0);
      edgeVerts.push_back(s.z0);
      edgeVerts.push_back(s.x1);
      edgeVerts.push_back(s.y1);
      edgeVerts.push_back(s.z1);
    }

    std::vector<float> ports;
    ports.reserve(def.connections.size() * 7);
    for (const CadBlockConnection& c : def.connections) {
      float rgba[4];
      PortRoleColor(c.role, rgba);
      ports.push_back(c.x);
      ports.push_back(c.y);
      ports.push_back(c.z);
      ports.insert(ports.end(), rgba, rgba + 4);
    }

    ViewportRenderer::PartThumbnailInput in;
    in.triVerts = &triVerts;
    in.triNormals = &triNormals;
    in.edgeVerts = &edgeVerts;
    in.portMarkers = &ports;
    in.rgba[0] = 0.72f;
    in.rgba[1] = 0.76f;
    in.rgba[2] = 0.82f;
    in.rgba[3] = 1.f;

    std::vector<unsigned char> rgba;
    int px = 0;
    if (renderer.EnsurePartThumbnail(name, in, kPipeFittingThumbPx) &&
        renderer.ReadPartThumbnailPixels(name, &rgba, &px) && px > 0) {
      // The renderer's pixels are only valid for this call, so the image owns a copy.
      images[name] = QImage(rgba.data(), px, px, px * 4, QImage::Format_RGBA8888).copy();
      changed = true;
    } else if (!Contains(cmd.pipeFittingThumbUnavailable, name)) {
      // Nothing drawable, or no GL object to draw into. Remembered, so the row is asked for once
      // rather than re-attempted every pass.
      cmd.pipeFittingThumbUnavailable.push_back(name);
      changed = true;
    }
  }
  return changed;
}

void EditorWindow::ShowPipeFittingPalette() {
  auto* dialog = findChild<QDialog*>("pipeFittingPalette");
  if (dialog == nullptr) {
    dialog = new QDialog(this, Qt::Tool);
    dialog->setObjectName("pipeFittingPalette");
    dialog->setWindowTitle("Pipe Fittings");
    dialog->resize(340, 540);
    auto* root = new QVBoxLayout(dialog);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* header = new QHBoxLayout;
    auto* runLabel = new QLabel(dialog);
    runLabel->setObjectName("pipeFittingRun");
    header->addWidget(runLabel, 1);
    auto* refresh = new QPushButton("Refresh", dialog);
    refresh->setObjectName("pipeFittingRefresh");
    refresh->setToolTip("Re-read the fittings library folders — for a part copied in from outside the application.");
    header->addWidget(refresh);
    root->addLayout(header);

    // Right-hand vertical tabs (REQ-350 (b)): Qt turns the tab text on its side for an East bar.
    auto* tabs = new QTabWidget(dialog);
    tabs->setObjectName("pipeFittingTabs");
    tabs->setTabPosition(QTabWidget::East);
    tabs->setDocumentMode(true);
    for (int i = 0; i < kCadPipePaletteCategoryCount; ++i) {
      auto* page = new QWidget(tabs);
      page->setObjectName(QString("pipeFittingPage%1").arg(i));
      auto* layout = new QVBoxLayout(page);
      layout->setContentsMargins(0, 4, 4, 0);
      auto* list = new QListWidget(page);
      list->setObjectName(QString("pipeFittingList%1").arg(i));
      list->setIconSize(QSize(kPipeFittingThumbPx, kPipeFittingThumbPx));
      list->setUniformItemSizes(true);
      list->setSelectionMode(QAbstractItemView::NoSelection);
      list->setSpacing(2);
      list->setWordWrap(true);
      layout->addWidget(list, 1);
      auto* empty = new QLabel(page);
      empty->setObjectName(QString("pipeFittingEmpty%1").arg(i));
      empty->setWordWrap(true);
      empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
      layout->addWidget(empty, 1);
      tabs->addTab(page, QString::fromUtf8(std::string(CadPipePaletteCategoryLabel(
                             static_cast<CadPipePaletteCategory>(i))).c_str()));
      // Picking a row arms the part; the next viewport click places it (REQ-350 (f)).
      connect(list, &QListWidget::itemClicked, dialog, [this](QListWidgetItem* item) {
        const std::string name = item->data(Qt::UserRole).toString().toUtf8().toStdString();
        const auto row = std::find_if(m_State.pipeFittingLibraryCache.begin(), m_State.pipeFittingLibraryCache.end(),
                                      [&](const CadBlockLibraryEntry& e) { return e.name == name; });
        if (row == m_State.pipeFittingLibraryCache.end()) return;
        const CadBlockLibraryEntry entry = *row;  // arming can invalidate the cache the row lives in
        (void)CadPipePaletteArmPart(m_State, entry, m_Log);  // refusals are logged there (REQ-201)
        // Arming imports the part when it was not in the drawing yet, which changes what the library
        // reading says about it.
        m_State.pipeFittingLibraryCacheValid = false;
        Refresh();
        // The palette must not keep the keyboard: the next thing the user does is click the drawing.
        if (m_Viewport) m_Viewport->setFocus();
      });
    }
    root->addWidget(tabs, 1);
    connect(tabs, &QTabWidget::currentChanged, dialog, [this](int index) {
      m_State.pipeFittingPaletteTab = index;
      if (auto* palette = findChild<QDialog*>("pipeFittingPalette")) RefreshPipeFittingPalette(palette);
    });
    connect(refresh, &QPushButton::clicked, dialog, [this] {
      // Re-read the library folders: the one thing the palette cannot notice on its own is a file
      // copied in from outside the application.
      m_State.pipeFittingLibraryCacheValid = false;
      m_State.pipeFittingThumbUnavailable.clear();
      if (auto* palette = findChild<QDialog*>("pipeFittingPalette")) RefreshPipeFittingPalette(palette);
    });
    // The close box closes it (REQ-350 (a)); the reopen command shows it again. Position and size are
    // kept for the next session like the main window's own geometry.
    connect(dialog, &QDialog::finished, this, [this, dialog] {
      m_State.pipeFittingPaletteOpen = false;
      QSettings().setValue("editor/pipeFittingPaletteGeometry", dialog->saveGeometry());
    });

    const QByteArray saved = QSettings().value("editor/pipeFittingPaletteGeometry").toByteArray();
    if (saved.isEmpty() || !dialog->restoreGeometry(saved)) {
      // First use: beside the drawing's right edge, clear of the ribbon.
      const QPoint topRight = mapToGlobal(QPoint(width(), 0));
      dialog->move(std::max(0, topRight.x() - dialog->width() - 24), topRight.y() + 170);
    }
  }
  if (!dialog->isVisible()) {
    m_State.pipeFittingLibraryCacheValid = false;  // re-read the library on every open
    dialog->setProperty("fingerprint", QString());
    dialog->show();
  }
  RefreshPipeFittingPalette(dialog);
}

void EditorWindow::RefreshPipeFittingPalette(QDialog* dialog) {
  if (dialog == nullptr || m_Viewport == nullptr) return;
  const std::string& size = m_State.pipeRunNominalSize;
  const CadPipePressureClass runClass = ParseCadPipePressureClass(m_State.pipeRunPressureClassTag);

  // The size changing is what re-filters the list (REQ-350's live re-filter condition). Thumbnail
  // bookkeeping is reset with it, so a part that could not be pictured at one size is asked for again
  // at another — the library may have been added to in between.
  if (m_State.pipeFittingPaletteShownSize != size) {
    m_State.pipeFittingPaletteShownSize = size;
    m_State.pipeFittingThumbRequests.clear();
    m_State.pipeFittingThumbUnavailable.clear();
    m_State.pipeFittingLibraryCacheValid = false;
  }
  // Reading the library is file I/O, so it happens on the events that can change it — not on every
  // refresh (§11.7). Filtering the cached rows below is pure and costs nothing worth caching.
  if (!m_State.pipeFittingLibraryCacheValid) {
    CadBlocksCollectLibraryEntries(m_State, &m_State.pipeFittingLibraryCache);
    m_State.pipeFittingLibraryCacheValid = true;
  }

  const CadPipePaletteCategory category = ActiveCategory(m_State);
  std::vector<CadBlockLibraryEntry> rows;
  CadPipePaletteCollectRows(m_State.pipeFittingLibraryCache, size, runClass, category, &rows);

  // Rebuild only when something the palette shows has changed — Refresh() runs on every command.
  const PartThumbnailImages& images = m_Viewport->PartThumbnails();
  QString fingerprint = QString("%1|%2|%3").arg(QString::fromUtf8(size.c_str()))
                            .arg(static_cast<int>(runClass)).arg(static_cast<int>(category));
  for (const CadBlockLibraryEntry& e : rows) {
    fingerprint += QString("|%1:%2%3").arg(QString::fromUtf8(e.name.c_str()))
                       .arg(images.count(e.name) != 0 ? 1 : 0)
                       .arg(Contains(m_State.pipeFittingThumbUnavailable, e.name) ? 1 : 0);
  }
  if (dialog->property("fingerprint").toString() == fingerprint) return;
  dialog->setProperty("fingerprint", fingerprint);

  if (auto* label = dialog->findChild<QLabel*>("pipeFittingRun")) {
    if (size.empty())
      label->setText("No pipe size set");
    else if (runClass == CadPipePressureClass::None)
      label->setText(QString("Pipe run: %1").arg(QString::fromUtf8(size.c_str())));
    else
      label->setText(QString("Pipe run: %1  %2").arg(QString::fromUtf8(size.c_str()),
          QString::fromUtf8(std::string(CadPipePressureClassTag(runClass)).c_str())));
  }
  if (auto* tabs = dialog->findChild<QTabWidget*>("pipeFittingTabs")) {
    const QSignalBlocker blocker(tabs);
    tabs->setCurrentIndex(static_cast<int>(category));
  }

  const QIcon placeholder = PlaceholderIcon(dialog->palette().color(QPalette::Mid));
  bool requested = false;
  for (int i = 0; i < kCadPipePaletteCategoryCount; ++i) {
    auto* list = dialog->findChild<QListWidget*>(QString("pipeFittingList%1").arg(i));
    auto* empty = dialog->findChild<QLabel*>(QString("pipeFittingEmpty%1").arg(i));
    if (list == nullptr || empty == nullptr) continue;
    list->clear();
    const bool current = i == static_cast<int>(category);
    // Only the shown tab is filled, so only visible parts are ever pictured (REQ-350's REQ-100 note).
    const bool hasRows = current && !rows.empty();
    list->setVisible(hasRows);
    empty->setVisible(current && rows.empty());
    if (!current) continue;
    if (rows.empty()) {
      empty->setText(QString::fromUtf8(CadPipePaletteEmptyReason(category, size).c_str()));
      continue;
    }
    for (const CadBlockLibraryEntry& e : rows) {
      auto* item = new QListWidgetItem(QString::fromUtf8(e.name.c_str()) + "\n" + RowSubtitle(e), list);
      item->setData(Qt::UserRole, QString::fromUtf8(e.name.c_str()));
      item->setToolTip(QString::fromUtf8(e.name.c_str()));
      item->setSizeHint(QSize(0, kPipeFittingThumbPx + 12));
      const auto picture = images.find(e.name);
      if (picture != images.end()) {
        item->setIcon(QIcon(QPixmap::fromImage(picture->second)));
      } else {
        item->setIcon(placeholder);
        if (!Contains(m_State.pipeFittingThumbUnavailable, e.name) &&
            !Contains(m_State.pipeFittingThumbRequests, e.name)) {
          m_State.pipeFittingThumbRequests.push_back(e.name);
          requested = true;
        }
      }
    }
  }
  // The pictures are made inside the viewport's paint, so ask for one.
  if (requested) m_Viewport->update();
}
