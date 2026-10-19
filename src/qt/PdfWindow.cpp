#include "PdfWindow.hpp"
#include "EditorWindow.hpp"
#include "PdfSplit.hpp"
#include <QAction>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
#include <QPainter>
#include <QMouseEvent>
#include <QInputDialog>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QActionGroup>
#include <QDebug>
#include <QAbstractListModel>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <map>
#include <set>

class PdfCanvas final : public QWidget {
public:
  explicit PdfCanvas(QWidget* parent) : QWidget(parent) { setObjectName("pdfCanvas"); resize(250, 100); }
  QImage image;
  std::function<void(QPointF, Qt::MouseButton)> clicked;
  std::function<void(QPainter&)> overlay;
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    if (image.isNull()) { painter.drawText(rect(), Qt::AlignCenter, "Open a PDF to begin"); return; }
    painter.drawImage(QPoint(0, 0), image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (overlay) overlay(painter);
  }
  void mousePressEvent(QMouseEvent* event) override { if (clicked) clicked(event->position(), event->button()); }
};

// REQ-387 clause 2: the thumbnail strip's model. Rows are pages; only the rows the view asks to paint are ever
// rendered, one at a time on a worker, nearest the top of the strip first. A draft (time-budgeted) render shows
// at once and a full one replaces it later, as in the ImGui viewer. A page that cannot be drawn keeps its blank sheet.
class PdfThumbnails final : public QAbstractListModel {
public:
  static constexpr int kBox = 150;             ///< the longer side of a thumbnail, in pixels
  static constexpr std::size_t kMaxKept = 120; ///< thumbnails kept at once (the cache bound)
  static constexpr int kDraftBudgetMs = 300;

  explicit PdfThumbnails(QObject* parent) : QAbstractListModel(parent), m_Pump(this) {
    m_Pump.setInterval(30);
    connect(&m_Pump, &QTimer::timeout, this, [this] { Pump(); });
  }
  ~PdfThumbnails() override {
    m_Cancel = true;
    try { if (m_Work.valid()) m_Work.wait(); } catch (...) { qWarning() << "PDF thumbnail wait failed during shutdown"; }
  }

  /// The document must outlive its use here: pass nullptr (which waits for the worker) before destroying it.
  void SetDocument(pdfview::PdfDocument* document) {
    m_Cancel = true;
    if (m_Work.valid()) {
      try { m_Work.wait(); } catch (const std::exception& error) { qWarning() << "PDF thumbnail wait failed:" << error.what(); }
      m_Work = {};
    }
    m_Cancel = false;
    beginResetModel();
    m_Doc = document;
    m_Kept.clear(); m_Failed.clear();
    endResetModel();
    if (m_Doc) m_Pump.start(); else m_Pump.stop();
  }
  std::function<std::pair<int, int>()> visibleRows; ///< first and last row on screen; set by the owner

  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() || !m_Doc ? 0 : m_Doc->PageCount();
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || !m_Doc || index.row() >= m_Doc->PageCount()) return {};
    if (role == Qt::DisplayRole) return QString::number(index.row() + 1);
    if (role == Qt::ToolTipRole) return QString("Page %1").arg(index.row() + 1);
    if (role != Qt::DecorationRole) return {};
    const auto found = m_Kept.find(index.row());
    if (found != m_Kept.end()) return found->second.pixmap;
    if (!m_Pump.isActive()) const_cast<QTimer&>(m_Pump).start();
    return Blank(index.row());
  }
  int Kept() const { return static_cast<int>(m_Kept.size()); }

private:
  struct Entry { QPixmap pixmap; bool partial = false; };
  struct Result { bool ok = false; pdfview::Bitmap bitmap; };

  QSize FitSize(int row) const {
    const auto size = m_Doc->Sizes()[static_cast<std::size_t>(row)];
    const double scale = std::min(kBox / size.wPt, kBox / size.hPt);
    return {std::max(8, qRound(size.wPt * scale)), std::max(8, qRound(size.hPt * scale))};
  }
  QPixmap Blank(int row) const {
    const QSize size = FitSize(row);
    auto found = m_Blank.find({size.width(), size.height()});
    if (found == m_Blank.end()) {
      QPixmap sheet(size);
      sheet.fill(Qt::white);
      found = m_Blank.emplace(std::make_pair(size.width(), size.height()), sheet).first;
    }
    return found->second;
  }
  void Evict(int centre) {
    while (m_Kept.size() > kMaxKept) {
      auto farthest = m_Kept.begin();
      for (auto it = m_Kept.begin(); it != m_Kept.end(); ++it)
        if (std::abs(it->first - centre) > std::abs(farthest->first - centre)) farthest = it;
      const QModelIndex gone = index(farthest->first);
      m_Kept.erase(farthest);
      dataChanged(gone, gone);
    }
  }
  void Pump() {
    if (m_Work.valid()) {
      if (m_Work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
      Result result;
      try { result = m_Work.get(); } catch (const std::exception& error) { qWarning() << "PDF thumbnail failed:" << error.what(); }
      const int row = m_WorkRow;
      if (m_Doc && row < m_Doc->PageCount()) {
        if (result.ok && result.bitmap.w > 0) {
          QImage image(result.bitmap.bgra.data(), result.bitmap.w, result.bitmap.h,
                       static_cast<qsizetype>(result.bitmap.w) * 4, QImage::Format_ARGB32);
          m_Kept[row] = {QPixmap::fromImage(image.copy()), result.bitmap.partial};
          dataChanged(index(row), index(row));
          const auto range = visibleRows ? visibleRows() : std::pair<int, int>{row, row};
          Evict((range.first + range.second) / 2);
        } else {
          m_Failed.insert(row);
        }
      }
    }
    if (!m_Doc) { m_Pump.stop(); return; }
    auto range = visibleRows ? visibleRows() : std::pair<int, int>{0, -1};
    range.first = std::max(0, range.first);
    range.second = std::min(range.second, m_Doc->PageCount() - 1);
    int next = -1;
    bool refine = false;
    for (int row = range.first; row <= range.second && next < 0; ++row)
      if (!m_Kept.count(row) && !m_Failed.count(row)) next = row;
    for (int row = range.first; row <= range.second && next < 0; ++row) {
      const auto kept = m_Kept.find(row);
      if (kept != m_Kept.end() && kept->second.partial) { next = row; refine = true; }
    }
    if (next < 0) { m_Pump.stop(); return; }
    const QSize size = FitSize(next);
    m_WorkRow = next;
    pdfview::PdfDocument* document = m_Doc;
    m_Work = std::async(std::launch::async, [this, document, next, size, refine] {
      Result result;
      try {
        result.ok = document->RenderPage(next, size.width(), size.height(), result.bitmap, [this] { return m_Cancel.load(); },
                                         pdfview::PdfDocument::kDisplayFlags, refine ? 0 : kDraftBudgetMs);
      } catch (...) { result.ok = false; } // the page keeps its blank sheet
      if (refine) result.bitmap.partial = false;
      return result;
    });
  }

  pdfview::PdfDocument* m_Doc = nullptr;
  std::map<int, Entry> m_Kept;
  std::set<int> m_Failed;
  mutable std::map<std::pair<int, int>, QPixmap> m_Blank;
  std::future<Result> m_Work;
  int m_WorkRow = 0;
  std::atomic<bool> m_Cancel{false};
  QTimer m_Pump;
};

// A thumbnail cell: the page picture centred above its number, the current page framed.
class PdfThumbDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
    return {PdfThumbnails::kBox + 20, PdfThumbnails::kBox + 38};
  }
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool selected = option.state & QStyle::State_Selected;
    const QRectF cell = QRectF(option.rect).adjusted(3, 3, -3, -3);
    if (selected) {
      painter->setPen(QPen(option.palette.highlight().color(), 2));
      painter->setBrush(Qt::NoBrush);
      painter->drawRoundedRect(cell, 4, 4);
    }
    const QPixmap picture = index.data(Qt::DecorationRole).value<QPixmap>();
    const QRectF art(cell.left() + (cell.width() - picture.width()) / 2,
                     cell.top() + 6 + (PdfThumbnails::kBox - picture.height()) / 2.0, picture.width(), picture.height());
    painter->drawPixmap(art.topLeft(), picture);
    painter->setPen(QPen(QColor(120, 120, 120), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(art);
    painter->setPen(option.palette.text().color());
    painter->drawText(QRectF(cell.left(), cell.bottom() - 22, cell.width(), 20), Qt::AlignCenter,
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
  }
};

// The selected look for the PDF window (issue #905, option B): deep navy chrome, a bright blue frame on the current thumbnail.
constexpr const char* kPdfWindowStyle = // scoped to the window's own chrome so the dialogs it opens keep the application theme
    "QMainWindow{background:#10151f;} QToolBar{background:#1b2433;border:0;spacing:6px;padding:3px;}"
    "QToolBar QToolButton{color:#e8eef9;padding:4px 8px;border-radius:4px;} QToolBar QToolButton:hover{background:#2c3b57;}"
    "QDockWidget{color:#e8eef9;} QDockWidget::title{background:#1b2433;padding:6px;}"
    "#pdfThumbnailList{background:#141b28;color:#cfd8ea;border:0;selection-background-color:#3b82f6;selection-color:white;}"
    "#pdfFindEdit{background:#0e131c;color:#fff;border:1px solid #3b4a68;border-radius:4px;padding:3px 6px;}"
    "QToolBar QLabel,QToolBar QCheckBox{color:#e8eef9;} QMainWindow > QScrollArea{background:#0e131c;}";

PdfWindow::PdfWindow(QWidget* parent) : QMainWindow(parent, Qt::Window) {
  setObjectName("pdfViewerWindow");
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("GoSurvey | PDF viewer");
  setStyleSheet(QString::fromLatin1(kPdfWindowStyle));
  resize(1000, 800);
  auto* tools = addToolBar("PDF");
  tools->setMovable(false);
  tools->addAction(QIcon(QtAsset("icons/Open.png")), "Open...", this, [this] {
    const auto path = QFileDialog::getOpenFileName(this, "Open PDF", {}, "PDF (*.pdf)");
    if (!path.isEmpty()) Open(path);
  });
  tools->addSeparator();
  m_PageNumber = new QSpinBox(tools);
  m_PageNumber->setRange(1, 1);
  m_PageNumber->setPrefix("Page ");
  tools->addWidget(m_PageNumber);
  connect(m_PageNumber, &QSpinBox::valueChanged, this, [this] { m_Draft.clear(); SyncThumbnail(); RequestRender(); });
  tools->addAction("Previous", this, [this] { m_PageNumber->setValue(m_PageNumber->value() - 1); });
  tools->addAction("Next", this, [this] { m_PageNumber->setValue(m_PageNumber->value() + 1); });
  tools->addSeparator();
  tools->addAction("Zoom in", this, [this] { m_Zoom = std::min(4.0, m_Zoom * 1.25); RequestRender(); });
  tools->addAction("Zoom out", this, [this] { m_Zoom = std::max(0.1, m_Zoom / 1.25); RequestRender(); });
  tools->addAction("Fit width", this, [this] { m_Zoom = 1.0; RequestRender(); });
  tools->addAction("Save As...", this, [this] { SaveAs(); });
  tools->addAction("Extract pages...", this, [this] { SplitPages(); });
  tools->addSeparator();
  auto* findAction = tools->addAction("Find...", this, [this] { ShowFindBar(); });
  findAction->setObjectName("pdfFindAction");
  findAction->setShortcut(QKeySequence::Find);
  findAction->setToolTip("Find text in the document (Ctrl+F)");
  auto* annotationTools = addToolBar("PDF annotations and measurements");
  annotationTools->setObjectName("pdfAnnotationTools");
  auto* group = new QActionGroup(this);
  const std::pair<const char*, int> kinds[] = {{"Browse", -1}, {"Text", 0}, {"Line", 1}, {"Rectangle", 2},
      {"Ellipse", 3}, {"Length", 5}, {"Polyline length", 6}, {"Area", 7}, {"Angle", 8}};
  for (const auto& kind : kinds) {
    auto* action = annotationTools->addAction(kind.first);
    action->setCheckable(true); group->addAction(action);
    if (kind.second == -1) action->setChecked(true);
    connect(action, &QAction::triggered, this, [this, kind] {
      m_Tool = kind.second; m_Draft.clear();
      statusBar()->showMessage(m_Tool < 0 ? "Browse pages" : "Click points on the page. Right-click finishes a polyline/area or cancels a draft.");
    });
  }
  annotationTools->addAction("Scale...", this, [this] { EditScale(); });
  annotationTools->addAction("Undo", this, [this] { m_Annotations.Undo(); m_Page->update(); setWindowModified(HasUnsavedChanges()); });
  annotationTools->addAction("Redo", this, [this] { m_Annotations.Redo(); m_Page->update(); setWindowModified(HasUnsavedChanges()); });
  m_Scroll = new QScrollArea(this);
  m_Scroll->setAlignment(Qt::AlignCenter);
  m_Page = new PdfCanvas(m_Scroll);
  m_Page->clicked = [this](QPointF point, Qt::MouseButton button) { PageClick(point, button); };
  m_Page->overlay = [this](QPainter& painter) { DrawHighlights(painter); DrawAnnotations(painter); };
  m_Scroll->setWidget(m_Page);
  setCentralWidget(m_Scroll);
  m_Poll = new QTimer(this);
  m_Poll->setInterval(30);
  connect(m_Poll, &QTimer::timeout, this, [this] { PollRender(); });

  // REQ-387 clause 2: the thumbnail strip, a dock shown by default and toggled from the toolbar.
  m_Thumbs = new PdfThumbnails(this);
  m_ThumbView = new QListView(this);
  m_ThumbView->setObjectName("pdfThumbnailList");
  m_ThumbView->setModel(m_Thumbs);
  m_ThumbView->setItemDelegate(new PdfThumbDelegate(m_ThumbView));
  m_ThumbView->setUniformItemSizes(true);
  m_ThumbView->setSelectionMode(QAbstractItemView::SingleSelection);
  m_ThumbView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_ThumbView->setMinimumWidth(PdfThumbnails::kBox + 40);
  m_Thumbs->visibleRows = [this] {
    if (!m_ThumbView->isVisible()) return std::make_pair(0, -1); // a hidden strip renders nothing
    const QModelIndex top = m_ThumbView->indexAt({4, 4});
    const QModelIndex bottom = m_ThumbView->indexAt({4, m_ThumbView->viewport()->height() - 4});
    return std::make_pair(top.isValid() ? top.row() : 0, bottom.isValid() ? bottom.row() : m_Thumbs->rowCount() - 1);
  };
  connect(m_ThumbView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
    if (current.isValid() && current.row() + 1 != m_PageNumber->value()) m_PageNumber->setValue(current.row() + 1);
  });
  m_ThumbDock = new QDockWidget("Thumbnails", this);
  m_ThumbDock->setObjectName("pdfThumbnails");
  m_ThumbDock->setWidget(m_ThumbView);
  addDockWidget(Qt::LeftDockWidgetArea, m_ThumbDock);
  resizeDocks({m_ThumbDock}, {PdfThumbnails::kBox + 60}, Qt::Horizontal);
  auto* thumbnailsAction = m_ThumbDock->toggleViewAction();
  thumbnailsAction->setObjectName("pdfThumbnailsAction");
  tools->addAction(thumbnailsAction);

  // REQ-428: the find bar, on its own row under the toolbars.
  addToolBarBreak(Qt::TopToolBarArea);
  m_FindBar = addToolBar("Find in PDF");
  m_FindBar->setObjectName("pdfFindBar");
  m_FindBar->setMovable(false);
  m_FindEdit = new QLineEdit(m_FindBar);
  m_FindEdit->setObjectName("pdfFindEdit");
  m_FindEdit->setPlaceholderText("Find in document");
  m_FindEdit->setClearButtonEnabled(true);
  m_FindEdit->setMinimumWidth(260);
  m_FindBar->addWidget(m_FindEdit);
  auto* previousHit = new QAction("Previous", this);
  previousHit->setObjectName("pdfFindPrevious");
  previousHit->setShortcut(QKeySequence::FindPrevious);
  previousHit->setToolTip("Previous match (Shift+F3, Shift+Enter)");
  auto* nextHit = new QAction("Next", this);
  nextHit->setObjectName("pdfFindNext");
  nextHit->setShortcut(QKeySequence::FindNext);
  nextHit->setToolTip("Next match (F3, Enter)");
  for (auto* action : {previousHit, nextHit}) { addAction(action); m_FindBar->addAction(action); } // addAction on the window keeps F3 live while the bar is hidden
  connect(previousHit, &QAction::triggered, this, [this] { StepHit(-1); });
  connect(nextHit, &QAction::triggered, this, [this] { StepHit(1); });
  m_FindMatchCase = new QCheckBox("Match case", m_FindBar);
  m_FindMatchCase->setObjectName("pdfFindMatchCase");
  m_FindBar->addWidget(m_FindMatchCase);
  m_FindStatus = new QLabel(m_FindBar);
  m_FindStatus->setObjectName("pdfFindStatus");
  m_FindStatus->setMinimumWidth(220);
  m_FindBar->addWidget(m_FindStatus);
  auto* closeFind = m_FindBar->addAction("Close", this, [this] { CloseFindBar(); });
  closeFind->setObjectName("pdfFindClose");
  m_FindBar->hide();
  connect(m_FindEdit, &QLineEdit::returnPressed, this, [this] {
    StepHit(QApplication::keyboardModifiers() & Qt::ShiftModifier ? -1 : 1);
  });
  connect(m_FindMatchCase, &QCheckBox::toggled, this, [this] { if (!m_FindEdit->text().isEmpty()) StartSearch(); });
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] { EscapePressed(); });
  m_SearchPoll = new QTimer(this);
  m_SearchPoll->setInterval(30);
  connect(m_SearchPoll, &QTimer::timeout, this, [this] { PollSearch(); });
}

PdfWindow::~PdfWindow() {
  CancelSearch(true);
  m_Thumbs->SetDocument(nullptr); // the document is destroyed before the model; the worker must be done with it
  m_Cancel = true;
  if (m_Work.valid()) {
    try { m_Work.wait(); } // Document outlives every use on the worker.
    catch (const std::exception& error) { qWarning() << "PDF worker wait failed during shutdown:" << error.what(); }
  }
}

void PdfWindow::Open(const QString& path) {
  if (!ConfirmDiscard()) return;
  QString error;
  if (!LoadFile(path, error)) QMessageBox::warning(this, "Open PDF", error);
}

bool PdfWindow::LoadFile(const QString& path, QString& error) {
  if (m_Work.valid()) {
    error = "Wait for the current page to finish before opening another PDF.";
    return false;
  }
  auto result = pdfview::PdfDocument::Open(std::filesystem::path(path.toStdWString()));
  if (!result.doc) { error = QString::fromUtf8(result.error.c_str()); return false; }
  CancelSearch(true); // the search and the thumbnail worker both hold the old document
  m_Thumbs->SetDocument(nullptr);
  m_Document = std::move(result.doc);
  m_Thumbs->SetDocument(m_Document.get());
  ClearSearch();
  m_Annotations = {}; m_Draft.clear();
  m_Scales = pdfview::ReadPageScales(m_Document->Path()).scales;
  const QSignalBlocker pageBlock(m_PageNumber);
  m_PageNumber->setRange(1, m_Document->PageCount());
  m_PageNumber->setValue(1);
  SyncThumbnail();
  setWindowTitle("GoSurvey | " + path + "[*]"); setWindowModified(false);
  RequestRender();
  return true;
}

void PdfWindow::RequestRender() {
  if (!m_Document) return;
  m_Page->image = {}; m_Page->update();
  if (m_Work.valid()) { m_Pending = true; m_Cancel = true; return; }
  const int page = m_PageNumber->value() - 1;
  const auto& size = m_Document->Sizes()[page];
  const int w = std::clamp(qRound(std::max(200, m_Scroll->viewport()->width() - 30) * m_Zoom), 64, 4096);
  const int h = std::clamp(qRound(static_cast<double>(w) * size.hPt / size.wPt), 64, 8192);
  m_Cancel = false;
  m_Work = std::async(std::launch::async, [this, page, w, h] {
    pdfview::Bitmap bitmap;
    if (!m_Document->RenderPage(page, w, h, bitmap, [this] { return m_Cancel.load(); })) return pdfview::Bitmap{};
    return bitmap;
  });
  statusBar()->showMessage("Rendering page...");
  m_Poll->start();
}

void PdfWindow::PollRender() {
  if (!m_Work.valid() || m_Work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_Poll->stop();
  try {
    auto bitmap = m_Work.get();
    if (!m_Pending && bitmap.w > 0 && bitmap.h > 0) {
      QImage image(bitmap.bgra.data(), bitmap.w, bitmap.h, static_cast<qsizetype>(bitmap.w) * 4, QImage::Format_ARGB32);
      m_Page->image = image.copy();
      m_Page->resize(bitmap.w, bitmap.h);
      m_Page->update();
      statusBar()->showMessage(QString("Page %1 of %2").arg(m_PageNumber->value()).arg(m_Document->PageCount()));
    } else if (!m_Pending) statusBar()->showMessage("The page could not be rendered.");
  } catch (const std::exception& error) {
    statusBar()->showMessage(QString::fromUtf8(error.what()));
  }
  if (m_Pending) { m_Pending = false; RequestRender(); }
}

pdfview::PageScale PdfWindow::CurrentScale() const {
  const int page = m_PageNumber->value() - 1;
  const auto changed = m_Annotations.Scales().find(page);
  if (changed != m_Annotations.Scales().end()) return changed->second;
  const auto original = m_Scales.find(page);
  return original != m_Scales.end() ? original->second : pdfview::PageScale{0, pdfview::Unit::Inch, 0};
}

bool PdfWindow::AddAnnotation(const pdfview::Annot& annotation, QString& error) {
  if (!m_Document || annotation.page < 0 || annotation.page >= m_Document->PageCount()) { error = "Open a PDF page first."; return false; }
  if (!std::isfinite(annotation.x0) || !std::isfinite(annotation.y0) || !std::isfinite(annotation.x1) || !std::isfinite(annotation.y1)) {
    error = "Annotation coordinates must be finite."; return false;
  }
  for (const auto& point : annotation.pts)
    if (!std::isfinite(point.first) || !std::isfinite(point.second)) { error = "Measurement coordinates must be finite."; return false; }
  auto scale = pdfview::PageScale{0, pdfview::Unit::Inch, 0};
  if (const auto found = m_Scales.find(annotation.page); found != m_Scales.end()) scale = found->second;
  if (const auto found = m_Annotations.Scales().find(annotation.page); found != m_Annotations.Scales().end()) scale = found->second;
  if (annotation.IsDimension() && (!pdfview::DimensionComplete(annotation) ||
      (annotation.kind != pdfview::Annot::Kind::Angle && !scale.Valid()))) {
    error = "Complete the measurement and set a page scale first."; return false;
  }
  m_Annotations.Add(annotation);
  setWindowModified(true); m_Page->update();
  return true;
}

void PdfWindow::PageClick(QPointF point, Qt::MouseButton button) {
  if (!m_Document || m_Tool < 0 || IsBusy() || m_Page->image.isNull()) return;
  const auto size = m_Document->Sizes()[m_PageNumber->value() - 1];
  if (button == Qt::LeftButton) m_Draft.emplace_back(static_cast<float>(point.x() * size.wPt / m_Page->width()),
      static_cast<float>((m_Page->height() - point.y()) * size.hPt / static_cast<double>(m_Page->height())));
  const auto kind = static_cast<pdfview::Annot::Kind>(m_Tool);
  const bool multi = kind == pdfview::Annot::Kind::PolyLength || kind == pdfview::Annot::Kind::Area;
  const std::size_t required = kind == pdfview::Annot::Kind::Angle || kind == pdfview::Annot::Kind::Area ? 3 : 2;
  if (button == Qt::RightButton && (!multi || m_Draft.size() < required)) { m_Draft.clear(); m_Page->update(); return; }
  if (m_Draft.size() < required || (multi && button != Qt::RightButton)) { m_Page->update(); return; }
  pdfview::Annot annotation;
  annotation.kind = kind; annotation.page = m_PageNumber->value() - 1;
  annotation.x0 = m_Draft.front().first; annotation.y0 = m_Draft.front().second;
  annotation.x1 = m_Draft.back().first; annotation.y1 = m_Draft.back().second;
  if (annotation.IsDimension()) annotation.pts = m_Draft;
  if (kind == pdfview::Annot::Kind::Text) {
    bool accepted = false;
    const auto text = QInputDialog::getMultiLineText(this, "PDF text", "Text", {}, &accepted);
    if (!accepted || text.isEmpty()) { m_Draft.clear(); return; }
    annotation.text = text.toUtf8().toStdString();
  }
  QString error;
  if (!AddAnnotation(annotation, error)) QMessageBox::warning(this, "PDF annotation", error);
  m_Draft.clear(); m_Page->update();
}

void PdfWindow::DrawAnnotations(QPainter& painter) {
  if (!m_Document || m_Page->image.isNull()) return;
  const int page = m_PageNumber->value() - 1;
  const auto size = m_Document->Sizes()[page];
  const double factor = static_cast<double>(m_Page->width()) / size.wPt;
  auto map = [&](float x, float y) { return QPointF(x * factor, (size.hPt - y) * factor); };
  for (const auto& annotation : m_Annotations.Items()) {
    if (annotation.page != page) continue;
    painter.setPen(QPen(QColor::fromRgb(annotation.color), std::max(1.0, annotation.thickness * factor)));
    painter.setBrush(annotation.fill ? QBrush(QColor::fromRgb(annotation.color)) : Qt::NoBrush);
    const QPointF first = map(annotation.x0, annotation.y0), last = map(annotation.x1, annotation.y1);
    const QRectF rect = QRectF(first, last).normalized();
    if (annotation.kind == pdfview::Annot::Kind::Rect) painter.drawRect(rect);
    else if (annotation.kind == pdfview::Annot::Kind::Ellipse) painter.drawEllipse(rect);
    else if (annotation.kind == pdfview::Annot::Kind::Line) painter.drawLine(first, last);
    else if (annotation.kind == pdfview::Annot::Kind::Text) {
      QFont font("Helvetica"); font.setPixelSize(std::max(1, qRound(annotation.fontSize * factor)));
      painter.setFont(font); painter.drawText(rect, Qt::TextWordWrap, QString::fromUtf8(annotation.text.c_str()));
    } else if (annotation.IsDimension()) {
      QPolygonF polygon;
      for (const auto& pt : annotation.pts) polygon << map(pt.first, pt.second);
      painter.drawPolyline(polygon);
      if (annotation.kind == pdfview::Annot::Kind::Area) painter.drawLine(polygon.first(), polygon.last());
      const auto anchor = pdfview::DimensionLabelAnchor(annotation);
      painter.drawText(map(anchor.first, anchor.second), QString::fromUtf8(pdfview::DimensionLabel(annotation, CurrentScale()).c_str()));
    }
  }
  painter.setPen(QPen(QColor("#45a8df"), 2));
  for (std::size_t i = 0; i < m_Draft.size(); ++i) {
    const auto pt = map(m_Draft[i].first, m_Draft[i].second);
    painter.drawEllipse(pt, 3, 3);
    if (i) painter.drawLine(map(m_Draft[i - 1].first, m_Draft[i - 1].second), pt);
  }
}

void PdfWindow::EditScale() {
  if (!m_Document) return;
  QDialog dialog(this); dialog.setWindowTitle("PDF Page Scale");
  auto* layout = new QFormLayout(&dialog);
  auto* presets = new QComboBox(&dialog);
  for (const auto& scale : pdfview::PresetScales()) presets->addItem(QString::fromUtf8(scale.RatioText().c_str()));
  layout->addRow("Scale", presets);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    m_Annotations.SetScales({{m_PageNumber->value() - 1, pdfview::PresetScales()[presets->currentIndex()]}});
    setWindowModified(HasUnsavedChanges()); m_Page->update();
  }
}

bool PdfWindow::SaveFile(const QString& path, QString& error) {
  if (!m_Document) { error = "Open a PDF first."; return false; }
  auto scales = m_Scales;
  for (const auto& changed : m_Annotations.Scales()) scales[changed.first] = changed.second;
  const auto reason = pdfview::SaveAnnotated(m_Document->Path(), m_Annotations.Items(),
      std::filesystem::path(path.toStdWString()), m_Annotations.Scales(), scales);
  if (!reason.empty()) { error = QString::fromUtf8(reason.c_str()); return false; }
  m_Annotations.MarkSaved(); setWindowModified(false);
  statusBar()->showMessage("Saved " + path);
  return true;
}

void PdfWindow::SaveAs() {
  if (!m_Document) return;
  QString path = QFileDialog::getSaveFileName(this, "Save annotated PDF as", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";
  QString error;
  if (!SaveFile(path, error)) QMessageBox::warning(this, "Save PDF", error);
}

bool PdfWindow::ConfirmDiscard() {
  if (!HasUnsavedChanges()) return true;
  const auto answer = QMessageBox::question(this, "Unsaved PDF annotations", "Save PDF annotations before closing?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Save) { SaveAs(); return !HasUnsavedChanges(); }
  return answer == QMessageBox::Discard;
}

void PdfWindow::closeEvent(QCloseEvent* event) {
  if (!ConfirmDiscard()) { event->ignore(); return; }
  m_Cancel = true; m_SearchCancel = true; event->accept();
}

int PdfWindow::ThumbnailsKept() const { return m_Thumbs ? m_Thumbs->Kept() : 0; }

void PdfWindow::SyncThumbnail() {
  if (!m_Document) return;
  const QModelIndex row = m_Thumbs->index(m_PageNumber->value() - 1);
  {
    const QSignalBlocker block(m_ThumbView->selectionModel());
    m_ThumbView->selectionModel()->setCurrentIndex(row, QItemSelectionModel::ClearAndSelect);
  }
  m_ThumbView->scrollTo(row);
}

void PdfWindow::ShowFindBar() {
  m_FindBar->show();
  m_FindEdit->setFocus();
  m_FindEdit->selectAll();
}

void PdfWindow::CloseFindBar() {
  CancelSearch(false);
  ClearSearch();
  m_FindBar->hide();
  m_Scroll->setFocus();
}

void PdfWindow::EscapePressed() {
  if (IsSearching()) CancelSearch(false); // the poll reports "Search cancelled"
  else if (m_FindBar->isVisible()) CloseFindBar();
}

void PdfWindow::ClearSearch() {
  m_Hits.clear(); m_HitIndex = -1; m_HitsTruncated = false;
  m_SearchedQuery.clear();
  m_FindStatus->clear();
  m_Page->update();
}

void PdfWindow::CancelSearch(bool wait) {
  m_SearchCancel = true;
  m_SearchRestart = false;
  if (wait && m_Search.valid()) {
    try { m_Search.wait(); } catch (const std::exception& error) { qWarning() << "PDF search wait failed:" << error.what(); }
    m_Search = {};
    m_SearchPoll->stop();
  }
}

void PdfWindow::StartSearch() {
  if (!m_Document) { m_FindStatus->setText("Open a PDF first"); return; }
  if (m_Search.valid()) { m_SearchRestart = true; m_SearchCancel = true; return; } // the poll starts it again
  const std::u16string query = m_FindEdit->text().toStdU16String();
  ClearSearch();
  if (query.empty()) return;
  const bool matchCase = m_FindMatchCase->isChecked();
  m_SearchedQuery = query; m_SearchedCase = matchCase;
  m_SearchCancel = false;
  m_FindStatus->setText("Searching... (Esc cancels)");
  pdfview::PdfDocument* document = m_Document.get();
  m_Search = std::async(std::launch::async, [this, document, query, matchCase] {
    SearchOutcome outcome;
    try { outcome.ok = document->SearchText(query, matchCase, outcome.result, [this] { return m_SearchCancel.load(); }); }
    catch (...) { outcome.ok = false; } // reported as "The search could not be completed."
    return outcome;
  });
  m_SearchPoll->start();
}

void PdfWindow::PollSearch() {
  if (!m_Search.valid() || m_Search.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_SearchPoll->stop();
  SearchOutcome outcome;
  try { outcome = m_Search.get(); }
  catch (const std::exception& error) { qWarning() << "PDF search failed:" << error.what(); }
  if (m_SearchRestart) { m_SearchRestart = false; StartSearch(); return; }
  if (m_SearchCancel) { ClearSearch(); if (m_FindBar->isVisible()) m_FindStatus->setText("Search cancelled"); return; } // a closed bar stays quiet
  if (!outcome.ok) { ClearSearch(); m_FindStatus->setText("The search could not be completed."); return; }
  if (!outcome.result.anyText) { ClearSearch(); m_FindStatus->setText("This PDF has no searchable text"); return; }
  if (outcome.result.hits.empty()) { ClearSearch(); m_FindStatus->setText("No matches"); return; }
  m_Hits = std::move(outcome.result.hits);
  m_HitsTruncated = outcome.result.truncated;
  const int page = m_PageNumber->value() - 1;
  const auto first = std::find_if(m_Hits.begin(), m_Hits.end(), [page](const pdfview::TextHit& hit) { return hit.page >= page; });
  const bool wrapped = first == m_Hits.end();
  ShowHit(wrapped ? 0 : static_cast<int>(first - m_Hits.begin()));
  if (wrapped) m_FindStatus->setText(m_FindStatus->text() + " (wrapped)");
}

void PdfWindow::StepHit(int direction) {
  if (!m_Document || IsSearching()) return;
  const std::u16string query = m_FindEdit->text().toStdU16String();
  if (query != m_SearchedQuery || m_FindMatchCase->isChecked() != m_SearchedCase || m_SearchedQuery.empty()) {
    StartSearch(); // a new text (or case rule): search first, then show the first match
    return;
  }
  if (m_Hits.empty()) return; // the last search found nothing; its message stays
  const int count = static_cast<int>(m_Hits.size());
  const int next = (m_HitIndex + direction + count) % count;
  const bool wrapped = count > 1 && ((direction > 0 && next < m_HitIndex) || (direction < 0 && next > m_HitIndex));
  ShowHit(next);
  if (wrapped) m_FindStatus->setText(m_FindStatus->text() + (direction > 0 ? " (wrapped to the first match)" : " (wrapped to the last match)"));
}

void PdfWindow::ShowHit(int index) {
  if (index < 0 || index >= static_cast<int>(m_Hits.size())) return;
  m_HitIndex = index;
  m_FindStatus->setText(QString("%1 of %2%3").arg(index + 1).arg(m_Hits.size()).arg(m_HitsTruncated ? "+" : ""));
  const int page = m_Hits[static_cast<std::size_t>(index)].page + 1;
  if (page != m_PageNumber->value()) m_PageNumber->setValue(page); // renders the page; the overlay follows
  else m_Page->update();
}

void PdfWindow::DrawHighlights(QPainter& painter) {
  if (!m_Document || m_Page->image.isNull() || m_Hits.empty()) return;
  const int page = m_PageNumber->value() - 1;
  const auto size = m_Document->Sizes()[page];
  const double factor = static_cast<double>(m_Page->width()) / size.wPt;
  painter.setPen(Qt::NoPen);
  for (int i = 0; i < static_cast<int>(m_Hits.size()); ++i) {
    const auto& hit = m_Hits[static_cast<std::size_t>(i)];
    if (hit.page != page) continue;
    painter.setBrush(i == m_HitIndex ? QColor(255, 140, 0, 150) : QColor(255, 230, 0, 110));
    for (const auto& box : hit.boxes)
      painter.drawRect(QRectF(QPointF(box.x0 * factor, (size.hPt - box.y1) * factor),
                              QPointF(box.x1 * factor, (size.hPt - box.y0) * factor)));
  }
}

void PdfWindow::SplitPages() {
  if (!m_Document) return;
  bool accepted = false;
  const auto text = QInputDialog::getText(this, "Extract PDF pages", "Page ranges (for example 1-3, 5)", QLineEdit::Normal, {}, &accepted);
  if (!accepted) return;
  const auto pages = pdfview::ParsePageList(text.toUtf8().toStdString(), m_Document->PageCount());
  if (!pages.error.empty()) { QMessageBox::warning(this, "Extract pages", QString::fromUtf8(pages.error.c_str())); return; }
  const auto path = QFileDialog::getSaveFileName(this, "Save extracted pages", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  const auto error = pdfview::SplitPdf(m_Document->Path(), pages.pages, std::filesystem::path(path.toStdWString()));
  if (!error.empty()) QMessageBox::warning(this, "Extract pages", QString::fromUtf8(error.c_str()));
}
