#include "PdfWindow.hpp"
#include "PdfCanvas.hpp"
#include "EditorWindow.hpp"
#include "PdfSplit.hpp"
#include "PdfScaleCheck.hpp"
#include "PdfDimAudit.hpp"
#include "SurveyPoints.hpp"
#include <QAction>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
#include <QPainter>
#include <QMouseEvent>
#include <QLineF>
#include <QInputDialog>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QActionGroup>
#include <QDebug>
#include <tuple>
#include <QAbstractListModel>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QSlider>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QAbstractItemView>
#include <map>
#include <set>
#include <cmath>

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

// PDF viewer chrome follows the app-wide graphite appearance; the current thumbnail keeps a blue frame.
constexpr const char* kPdfWindowStyle = // scoped to the window's own chrome so the dialogs it opens keep the application theme
    "QMainWindow{background:#1c2024;} QToolBar{background:#242a2f;border:0;spacing:6px;padding:3px;}"
    "QToolBar QToolButton{color:#e8eef9;padding:4px 8px;border-radius:4px;} QToolBar QToolButton:hover{background:#343c41;} QToolBar QToolButton:checked{background:#164d4b;}"
    "QDockWidget{color:#e8eef9;} QDockWidget::title{background:#292f34;padding:6px;}"
    "#pdfThumbnailList{background:#202529;color:#cfd8da;border:0;selection-background-color:#244846;selection-color:white;}"
    "#pdfFindEdit{background:#15191c;color:#fff;border:1px solid #424b51;border-radius:4px;padding:3px 6px;}"
    "QToolBar QLabel,QToolBar QCheckBox{color:#e8eef9;} QMainWindow > QScrollArea{background:#171b1e;}";

constexpr const char* kPdfExtractReviewStyle =
    "QDialog{background:#22272b;color:#eef2ff;} QLabel{color:#eef2ff;font-size:13px;}"
    "QTableWidget{background:#202529;color:#eef2ff;gridline-color:#41494e;}"
    "QPushButton{background:#2a3035;color:#eef2ff;border:1px solid #41494e;border-radius:4px;padding:6px 12px;}";

// PDF workflow dialogs follow the Qt Dark graphite appearance.
constexpr const char* kPdfWorkflowDialogStyle =
    "QDialog{background:#1c2024;color:#e8eef9;} QLabel{color:#e8eef9;} QLabel:disabled{color:#89979a;}"
    "QCheckBox{color:#e8eef9;} QCheckBox::indicator{width:15px;height:15px;border:2px solid #576469;border-radius:3px;background:#15191c;}"
    "QCheckBox::indicator:checked{background:#168e8a;border-color:#168e8a;}"
    "QPushButton{background:#2a3035;border:1px solid #41494e;border-radius:5px;padding:6px 12px;color:#e8eef9;}"
    "QPushButton:hover{background:#343c41;} QPushButton:disabled{color:#89979a;background:#242a2f;}"
    "QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#15191c;border:1px solid #424b51;border-radius:4px;padding:3px 6px;color:#fff;}"
    "QComboBox QAbstractItemView{background:#15191c;color:#fff;selection-background-color:#168e8a;}"
    "QTableWidget{background:#202529;alternate-background-color:#242a2f;border:1px solid #41494e;color:#cfd8da;selection-background-color:#164d4b;selection-color:white;}"
    "QHeaderView::section{background:#292f34;color:#e8eef9;padding:5px 8px;border:0;border-right:1px solid #41494e;font-weight:600;}"
    "QProgressBar{background:#15191c;border:1px solid #424b51;border-radius:4px;color:#e8eef9;text-align:center;}"
    "QProgressBar::chunk{background:#168e8a;border-radius:3px;}";

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
  connect(m_PageNumber, &QSpinBox::valueChanged, this, [this] {
    m_Draft.clear(); m_SnapHit = false; m_SnapPrevValid = false;
    SyncThumbnail(); UpdateScaleDisplay(); if (m_SnapOn) { StartSnapRead(m_PageNumber->value() - 1); UpdateSnap(m_LastPointer); }
    RequestRender();
    if (m_Compare && m_Compare->basePage != m_PageNumber->value() - 1)
      m_CompareBasePage->setValue(m_PageNumber->value());
  });
  tools->addAction("Previous", this, [this] { m_PageNumber->setValue(m_PageNumber->value() - 1); });
  tools->addAction("Next", this, [this] { m_PageNumber->setValue(m_PageNumber->value() + 1); });
  tools->addSeparator();
  tools->addAction("Zoom in", this, [this] { m_Zoom = std::min(4.0, m_Zoom * 1.25); RequestRender(); });
  tools->addAction("Zoom out", this, [this] { m_Zoom = std::max(0.1, m_Zoom / 1.25); RequestRender(); });
  tools->addAction("Fit width", this, [this] { m_Zoom = 1.0; RequestRender(); });
  tools->addAction("Save As...", this, [this] { SaveAs(); });
  tools->addAction("Extract pages...", this, [this] { SplitPages(); });
  auto* extract = tools->addAction("Extract coordinates", this, [this] {
    if (!m_Document) { statusBar()->showMessage("Open a PDF page before extracting coordinates."); return; }
    SetTool(-1, QString()); // leave any scale pick in progress
    m_ExtractMode = true; m_ExtractDragging = false; m_ExtractResult.reset();
    statusBar()->showMessage("Drag a rectangle around selectable coordinate text. Esc cancels.");
    m_Page->setCursor(Qt::CrossCursor);
  });
  extract->setObjectName("pdfExtractCoordinates");
  auto* audit = tools->addAction("Audit scale", this, [this] { ShowScaleAudit(); });
  audit->setObjectName("pdfAuditScale");
  audit->setToolTip("Compare this page's dimension text with its scale");
  auto* compare = tools->addAction("Compare revisions…", this, [this] { BeginCompare(); });
  compare->setObjectName("pdfCompareRevisions");
  tools->addSeparator();
  auto* findAction = tools->addAction("Find...", this, [this] { ShowFindBar(); });
  findAction->setObjectName("pdfFindAction");
  findAction->setShortcut(QKeySequence::Find);
  findAction->setToolTip("Find text in the document (Ctrl+F)");
  addToolBarBreak(Qt::TopToolBarArea); // the annotation tools get a row of their own (there are enough of them now)
  auto* annotationTools = addToolBar("PDF annotations and measurements");
  annotationTools->setObjectName("pdfAnnotationTools");
  m_ToolGroup = new QActionGroup(this);
  const std::pair<const char*, int> kinds[] = {{"Select", -2}, {"Browse", -1}, {"Text", 0}, {"Line", 1}, {"Rectangle", 2},
      {"Ellipse", 3}, {"Length", 5}, {"Polyline length", 6}, {"Area", 7}, {"Angle", 8}, {"Check", kToolCheck}};
  for (const auto& kind : kinds) {
    auto* action = annotationTools->addAction(kind.first);
    action->setCheckable(true); m_ToolGroup->addAction(action);
    action->setData(kind.second);
    if (kind.second == -2) action->setChecked(true);
    if (kind.second == kToolCheck) {
      action->setObjectName("pdfCheckTool");
      action->setToolTip("Check the page scale against a dimension the drawing states");
    }
    connect(action, &QAction::triggered, this, [this, kind] {
      m_CheckTarget = 0;
      if (kind.second == kToolCheck) SetTool(kToolCheck, "Check: click the two ends of a dimension the drawing states.");
      else SetTool(kind.second, kind.second == -2 ? "Select an annotation; drag it or a grip to edit." :
          kind.second == -1 ? "Browse pages" : "Click points on the page. Right-click finishes a polyline/area or cancels a draft.");
    });
  }
  auto* calibrateTool = new QAction("Calibrate", m_ToolGroup); // started from Set scale...; not a toolbar button
  calibrateTool->setCheckable(true);
  calibrateTool->setData(kToolCalibrate);
  auto* style = addToolBar("Annotation style");
  style->setObjectName("pdfAnnotationStyle");
  auto* color = style->addAction("Color...");
  color->setObjectName("pdfAnnotationColor");
  connect(color, &QAction::triggered, this, [this] {
    const QColor chosen = QColorDialog::getColor(QColor::fromRgb(m_AnnotationColor), this, "Annotation color");
    if (chosen.isValid()) m_AnnotationColor = chosen.rgb() & 0xFFFFFFu;
  });
  style->addWidget(new QLabel("Width", style));
  auto* width = new QDoubleSpinBox(style); width->setObjectName("pdfAnnotationWidth");
  width->setRange(0.1, 20.0); width->setSingleStep(0.25); width->setValue(m_AnnotationThickness); width->setSuffix(" pt");
  style->addWidget(width);
  connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) { m_AnnotationThickness = static_cast<float>(value); });
  style->addWidget(new QLabel("Text size", style));
  auto* fontSize = new QDoubleSpinBox(style); fontSize->setObjectName("pdfAnnotationFontSize");
  fontSize->setRange(4.0, 144.0); fontSize->setValue(m_AnnotationFontSize); fontSize->setSuffix(" pt");
  style->addWidget(fontSize);
  connect(fontSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) { m_AnnotationFontSize = static_cast<float>(value); });
  auto* font = new QComboBox(style); font->setObjectName("pdfAnnotationFont");
  for (const char* family : {"Helvetica", "Times", "Courier"}) font->addItem(family);
  style->addWidget(font);
  connect(font, &QComboBox::currentTextChanged, this, [this](const QString& value) { m_AnnotationFont = value.toStdString(); });
  auto* bold = new QCheckBox("Bold", style); bold->setObjectName("pdfAnnotationBold"); style->addWidget(bold);
  auto* italic = new QCheckBox("Italic", style); italic->setObjectName("pdfAnnotationItalic"); style->addWidget(italic);
  connect(bold, &QCheckBox::toggled, this, [this](bool value) { m_AnnotationBold = value; });
  connect(italic, &QCheckBox::toggled, this, [this](bool value) { m_AnnotationItalic = value; });
  auto* fill = new QCheckBox("Fill closed shapes", style); fill->setObjectName("pdfAnnotationFill"); style->addWidget(fill);
  connect(fill, &QCheckBox::toggled, this, [this](bool value) { m_AnnotationFill = value; });
  auto* applyStyle = style->addAction("Apply style to selected");
  applyStyle->setObjectName("pdfAnnotationApplyStyle");
  connect(applyStyle, &QAction::triggered, this, [this] {
    if (m_SelectedAnnotation < 0 || m_SelectedAnnotation >= static_cast<int>(m_Annotations.Items().size())) return;
    auto annotation = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
    ApplyAnnotationStyle(annotation);
    if (m_Annotations.Replace(m_SelectedAnnotation, annotation)) SceneChanged();
  });
  annotationTools->addAction("Undo", this, [this] { m_Annotations.Undo(); SceneChanged(); });
  annotationTools->addAction("Redo", this, [this] { m_Annotations.Redo(); SceneChanged(); });
  BuildScaleToolbar();
  m_Scroll = new QScrollArea(this);
  m_Scroll->setAlignment(Qt::AlignCenter);
  m_Page = new PdfCanvas(m_Scroll);
  m_Page->setMouseTracking(true);
  m_Page->clicked = [this](QPointF point, Qt::MouseButton button) { PageClick(point, button); };
  m_Page->doubleClicked = [this] { EditSelectedAnnotation(); };
  m_Page->moved = [this](QPointF point) { MoveExtract(point); MoveAnnotation(point); UpdateSnap(point); };
  m_Page->left = [this] { if (m_SnapHit) { m_SnapHit = false; m_SnapPrevValid = false; m_Page->update(); } };
  m_Page->overlay = [this](QPainter& painter) { DrawCompareOverlay(painter); DrawHighlights(painter); DrawAnnotations(painter); DrawScaleOverlay(painter); DrawAuditOverlay(painter); DrawExtractOverlay(painter); };
  m_Page->pressed = [this](QPointF point, Qt::MouseButton button) { BeginExtract(point, button); };
  m_Page->released = [this](QPointF point, Qt::MouseButton button) { EndExtract(point, button); EndAnnotation(point, button); };
  m_Scroll->setWidget(m_Page);
  setCentralWidget(m_Scroll);
  m_Poll = new QTimer(this);
  m_Poll->setInterval(30);
  connect(m_Poll, &QTimer::timeout, this, [this] { PollRender(); });
  m_OpenPoll = new QTimer(this);
  m_OpenPoll->setInterval(30);
  connect(m_OpenPoll, &QTimer::timeout, this, [this] { PollOpen(); });
  m_SavePoll = new QTimer(this);
  m_SavePoll->setInterval(30);
  connect(m_SavePoll, &QTimer::timeout, this, [this] { PollSave(); });

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

  // REQ-429: the find bar, on its own row under the toolbars.
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
  previousHit->setToolTip("Previous match (Shift+Enter)");
  auto* nextHit = new QAction("Next", this);
  nextHit->setObjectName("pdfFindNext");
  nextHit->setToolTip("Next match (Enter)");
  for (auto* action : {previousHit, nextHit}) { addAction(action); m_FindBar->addAction(action); } // F3 belongs to Snap (REQ-430)
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
  auto* removeAnnotation = new QShortcut(QKeySequence::Delete, this);
  connect(removeAnnotation, &QShortcut::activated, this, [this] {
    if (m_SelectedAnnotation >= 0 && m_Annotations.Remove(m_SelectedAnnotation)) {
      m_SelectedAnnotation = -1; SceneChanged();
    }
  });
  auto* undoAnnotation = new QShortcut(QKeySequence::Undo, this);
  connect(undoAnnotation, &QShortcut::activated, this, [this] {
    if (m_Annotations.Undo()) { m_SelectedAnnotation = -1; SceneChanged(); }
  });
  auto* redoAnnotation = new QShortcut(QKeySequence::Redo, this);
  connect(redoAnnotation, &QShortcut::activated, this, [this] {
    if (m_Annotations.Redo()) { m_SelectedAnnotation = -1; SceneChanged(); }
  });
  m_SearchPoll = new QTimer(this);
  m_SearchPoll->setInterval(30);
  connect(m_SearchPoll, &QTimer::timeout, this, [this] { PollSearch(); });

  m_AuditDialog = new QDialog(this);
  m_AuditDialog->setObjectName("pdfScaleAuditDialog");
  m_AuditDialog->setWindowTitle("Audit scale");
  m_AuditDialog->setStyleSheet(QString::fromLatin1(kPdfWorkflowDialogStyle));
  m_AuditDialog->resize(920, 560);
  auto* auditLayout = new QVBoxLayout(m_AuditDialog);
  m_AuditSummary = new QLabel("Audit reads dimension text and line work on one page. It never changes the scale unless you apply the consensus.", m_AuditDialog);
  m_AuditSummary->setWordWrap(true);
  m_AuditSummary->setObjectName("pdfAuditSummary");
  auditLayout->addWidget(m_AuditSummary);
  m_AuditAllRows = new QCheckBox("List every matched dimension", m_AuditDialog);
  m_AuditAllRows->setObjectName("pdfAuditAllRows");
  auditLayout->addWidget(m_AuditAllRows);
  m_AuditShowSheet = new QCheckBox("Show selected dimensions on the sheet", m_AuditDialog);
  m_AuditShowSheet->setObjectName("pdfAuditShowOnSheet"); m_AuditShowSheet->setChecked(true);
  auditLayout->addWidget(m_AuditShowSheet);
  m_AuditTable = new QTableWidget(m_AuditDialog);
  m_AuditTable->setObjectName("pdfAuditTable");
  m_AuditTable->setColumnCount(6);
  m_AuditTable->setHorizontalHeaderLabels({"Drawing says", "Measured", "Difference", "Implied scale", "Verdict", "Page location"});
  m_AuditTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_AuditTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_AuditTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  auditLayout->addWidget(m_AuditTable, 1);
  auto* auditButtons = new QHBoxLayout;
  auto* runAudit = new QPushButton("Run audit", m_AuditDialog);
  runAudit->setObjectName("pdfAuditRun");
  auto* cancelAudit = new QPushButton("Cancel audit", m_AuditDialog);
  cancelAudit->setObjectName("pdfAuditCancel");
  auto* useConsensus = new QPushButton("Use consensus scale", m_AuditDialog);
  useConsensus->setObjectName("pdfAuditUseConsensus");
  auto* closeAudit = new QPushButton("Close", m_AuditDialog);
  closeAudit->setObjectName("pdfAuditClose");
  auditButtons->addWidget(runAudit);
  auditButtons->addWidget(cancelAudit);
  auditButtons->addStretch();
  auditButtons->addWidget(useConsensus);
  auditButtons->addWidget(closeAudit);
  auditLayout->addLayout(auditButtons);
  connect(runAudit, &QPushButton::clicked, this, [this] { StartScaleAudit(); });
  connect(cancelAudit, &QPushButton::clicked, this, [this] { m_AuditCancel = true; m_AuditSummary->setText("Cancelling audit… no partial result will be kept."); });
  connect(closeAudit, &QPushButton::clicked, m_AuditDialog, &QDialog::hide);
  connect(m_AuditAllRows, &QCheckBox::toggled, this, [this] { RefreshScaleAudit(); });
  connect(m_AuditShowSheet, &QCheckBox::toggled, this, [this] { m_Page->update(); });
  connect(m_AuditTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
    if (!m_AuditReady || row < 0 || row >= static_cast<int>(m_AuditRows.size()) || m_AuditPage != m_PageNumber->value() - 1 || m_Page->image.isNull()) return;
    m_AuditSelected = m_AuditRows[static_cast<std::size_t>(row)];
    m_Page->update();
    const auto& match = m_AuditMatches.matches[static_cast<std::size_t>(m_AuditRows[static_cast<std::size_t>(row)])];
    const auto size = m_Document->Sizes()[m_AuditPage];
    const double factor = static_cast<double>(m_Page->image.width()) / std::max(1.0, static_cast<double>(size.wPt));
    const int x = qRound(match.tx * factor), y = qRound((size.hPt - match.ty) * factor);
    m_Scroll->horizontalScrollBar()->setValue(std::max(0, x - m_Scroll->viewport()->width() / 2));
    m_Scroll->verticalScrollBar()->setValue(std::max(0, y - m_Scroll->viewport()->height() / 2));
  });
  connect(useConsensus, &QPushButton::clicked, this, [this] {
    const pdfview::PageScale* current = EffectiveScale(m_AuditPage);
    if (!m_AuditReady || !current || !m_AuditResult.consensusValid) return;
    const auto next = pdfview::ScaleFromConsensus(m_AuditResult.consensusMetresPerPt,
        static_cast<int>(m_AuditMatches.matches.size()), *current);
    if (m_Annotations.SetScales({{m_AuditPage, next}})) {
      SceneChanged();
      m_AuditSummary->setText(QString("Scale updated to %1. This change can be undone.").arg(QString::fromUtf8(next.RatioText().c_str())));
    }
  });
  m_AuditPoll = new QTimer(this);
  m_AuditPoll->setInterval(30);
  connect(m_AuditPoll, &QTimer::timeout, this, [this] { PollScaleAudit(); });

  m_CompareDialog = new QDialog(this);
  m_CompareDialog->setObjectName("pdfCompareDialog");
  m_CompareDialog->setWindowTitle("Compare PDF revisions");
  m_CompareDialog->setStyleSheet(QString::fromLatin1(kPdfWorkflowDialogStyle));
  m_CompareDialog->resize(1100, 680);
  auto* compareLayout = new QVBoxLayout(m_CompareDialog);
  m_CompareStatus = new QLabel("Choose a revision PDF to begin. The open PDF remains the base and is never modified.", m_CompareDialog);
  m_CompareStatus->setObjectName("pdfCompareStatus"); m_CompareStatus->setWordWrap(true);
  compareLayout->addWidget(m_CompareStatus);
  m_CompareProgress = new QProgressBar(m_CompareDialog); m_CompareProgress->setObjectName("pdfCompareProgress");
  m_CompareProgress->setRange(0, 0); m_CompareProgress->hide(); compareLayout->addWidget(m_CompareProgress);
  auto* pageControls = new QHBoxLayout;
  m_CompareBasePage = new QSpinBox(m_CompareDialog); m_CompareBasePage->setObjectName("pdfCompareBasePage");
  m_CompareBasePage->setPrefix("Base page "); m_CompareBasePage->setRange(1, 1);
  m_CompareRevisionPage = new QSpinBox(m_CompareDialog); m_CompareRevisionPage->setObjectName("pdfCompareRevisionPage");
  m_CompareRevisionPage->setPrefix("Revision page "); m_CompareRevisionPage->setRange(1, 1);
  pageControls->addWidget(m_CompareBasePage); pageControls->addWidget(m_CompareRevisionPage); pageControls->addStretch();
  compareLayout->addLayout(pageControls);
  auto* viewControls = new QHBoxLayout;
  m_CompareMode = new QComboBox(m_CompareDialog);
  m_CompareMode->setObjectName("pdfCompareMode");
  m_CompareMode->addItems({"Tint", "Opacity", "Base", "Revision"});
  m_CompareMode->setCurrentIndex(0);
  viewControls->addWidget(new QLabel("View", m_CompareDialog)); viewControls->addWidget(m_CompareMode);
  m_CompareOpacity = new QSlider(Qt::Horizontal, m_CompareDialog);
  m_CompareOpacity->setObjectName("pdfCompareOpacity"); m_CompareOpacity->setRange(0, 100); m_CompareOpacity->setValue(50);
  viewControls->addWidget(new QLabel("Revision opacity", m_CompareDialog)); viewControls->addWidget(m_CompareOpacity, 1);
  compareLayout->addLayout(viewControls);
  auto* diffControls = new QHBoxLayout;
  m_CompareTolerance = new QDoubleSpinBox(m_CompareDialog); m_CompareTolerance->setObjectName("pdfCompareTolerance");
  m_CompareTolerance->setRange(0.0, 25.0); m_CompareTolerance->setSingleStep(0.25); m_CompareTolerance->setSuffix(" mm tolerance");
  m_CompareTolerance->setValue(pdfdiff::Settings{}.toleranceMm);
  m_CompareMinRegion = new QDoubleSpinBox(m_CompareDialog); m_CompareMinRegion->setObjectName("pdfCompareMinRegion");
  m_CompareMinRegion->setRange(0.0, 100.0); m_CompareMinRegion->setSingleStep(0.5); m_CompareMinRegion->setSuffix(" mm minimum region");
  m_CompareMinRegion->setValue(pdfdiff::Settings{}.minSizeMm);
  diffControls->addWidget(m_CompareTolerance); diffControls->addWidget(m_CompareMinRegion);
  m_CompareShowHighlights = new QCheckBox("Show change boxes", m_CompareDialog);
  m_CompareShowHighlights->setObjectName("pdfCompareShowHighlights"); m_CompareShowHighlights->setChecked(true);
  diffControls->addWidget(m_CompareShowHighlights); diffControls->addStretch();
  compareLayout->addLayout(diffControls);
  m_CompareTable = new QTableWidget(m_CompareDialog);
  m_CompareTable->setObjectName("pdfCompareChanges"); m_CompareTable->setColumnCount(4);
  m_CompareTable->setHorizontalHeaderLabels({"Kind", "X", "Y", "Size"});
  m_CompareTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_CompareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  compareLayout->addWidget(m_CompareTable, 1);
  auto* alignButtons = new QHBoxLayout;
  auto* changeButtons = new QHBoxLayout;
  auto* alignAuto = new QPushButton("Align automatically", m_CompareDialog);
  auto* pickOne = new QPushButton("Pick one matching point", m_CompareDialog);
  auto* pickTwo = new QPushButton("Pick two matching points", m_CompareDialog);
  auto* resetAlign = new QPushButton("Reset alignment", m_CompareDialog);
  auto* findChanges = new QPushButton("Find changes", m_CompareDialog);
  auto* previousChange = new QPushButton("Previous change", m_CompareDialog);
  auto* nextChange = new QPushButton("Next change", m_CompareDialog);
  auto* saveChanges = new QPushButton("Save changes as markups…", m_CompareDialog);
  auto* cancelCompare = new QPushButton("Cancel work", m_CompareDialog);
  auto* closeCompare = new QPushButton("Close", m_CompareDialog);
  alignAuto->setObjectName("pdfCompareAlignAuto"); pickOne->setObjectName("pdfComparePickOne");
  pickTwo->setObjectName("pdfComparePickTwo"); findChanges->setObjectName("pdfCompareFindChanges");
  saveChanges->setObjectName("pdfCompareSaveChanges"); cancelCompare->setObjectName("pdfCompareCancel");
  for (auto* b : {alignAuto, pickOne, pickTwo, resetAlign, cancelCompare}) alignButtons->addWidget(b);
  for (auto* b : {findChanges, previousChange, nextChange, saveChanges, closeCompare}) changeButtons->addWidget(b);
  alignButtons->addStretch(); changeButtons->addStretch();
  compareLayout->addLayout(alignButtons);
  compareLayout->addLayout(changeButtons);
  connect(m_CompareMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int mode) {
    if (m_Compare) { m_Compare->mode = mode; UpdateCompareOverlay(); }
  });
  connect(m_CompareOpacity, &QSlider::valueChanged, this, [this](int value) { if (m_Compare) { m_Compare->opacity = value; m_Page->update(); } });
  connect(m_CompareShowHighlights, &QCheckBox::toggled, this, [this] { m_Page->update(); });
  connect(alignAuto, &QPushButton::clicked, this, [this] { if (m_Compare) { m_Compare->aligned = false; m_Compare->manualAlignment = false; StartCompare(false); } });
  connect(pickOne, &QPushButton::clicked, this, [this] { if (m_Compare) { m_ComparePicking = true; m_ComparePickCount = 0; m_ComparePickTarget = 2; m_CompareMode->setCurrentIndex(2); m_CompareStatus->setText("Pick a point on the Base view, switch to Revision, then pick its match. Esc cancels."); } });
  connect(pickTwo, &QPushButton::clicked, this, [this] { if (m_Compare) { m_ComparePicking = true; m_ComparePickCount = 0; m_ComparePickTarget = 4; m_CompareMode->setCurrentIndex(2); m_CompareStatus->setText("Pick base point 1, its revision match, base point 2, then its revision match. Switch views as prompted."); } });
  connect(resetAlign, &QPushButton::clicked, this, [this] { if (m_Compare) { m_Compare->aligned = false; m_Compare->manualAlignment = false; StartCompare(false); } });
  connect(findChanges, &QPushButton::clicked, this, [this] {
    if (m_Compare && m_Compare->lowConfidence && QMessageBox::warning(m_CompareDialog, "Check alignment",
        "The sheets did not align confidently. Change detection may produce misleading results. Find changes anyway?",
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    StartCompare(true);
  });
  const auto stepChange = [this](int direction) {
    const int count = m_CompareTable->rowCount();
    if (count <= 0) return;
    int row = m_CompareTable->currentRow();
    row = row < 0 ? (direction > 0 ? 0 : count - 1) : (row + direction + count) % count;
    m_CompareTable->setCurrentCell(row, 0);
  };
  connect(previousChange, &QPushButton::clicked, this, [stepChange] { stepChange(-1); });
  connect(nextChange, &QPushButton::clicked, this, [stepChange] { stepChange(1); });
  connect(m_CompareTolerance, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
    if (m_Compare) { m_Compare->settings.toleranceMm = value; m_Compare->haveChanges = false; m_CompareTable->setRowCount(0); m_Page->update(); }
  });
  connect(m_CompareMinRegion, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
    if (m_Compare) { m_Compare->settings.minSizeMm = value; m_Compare->haveChanges = false; m_CompareTable->setRowCount(0); m_Page->update(); }
  });
  connect(saveChanges, &QPushButton::clicked, this, [this] { SaveCompareChanges(); });
  connect(cancelCompare, &QPushButton::clicked, this, [this] { if (m_Compare) { m_Compare->cancel = true; m_CompareStatus->setText("Cancelling comparison… no partial result will be kept."); } });
  connect(m_CompareBasePage, qOverload<int>(&QSpinBox::valueChanged), this, [this](int page) { if (m_Compare && page > 0) { m_Compare->basePage = page - 1; m_Compare->aligned = false; m_Compare->manualAlignment = false; if (m_PageNumber->value() != page) m_PageNumber->setValue(page); StartCompare(false); } });
  connect(m_CompareRevisionPage, qOverload<int>(&QSpinBox::valueChanged), this, [this](int page) { if (m_Compare && page > 0) { m_Compare->revisionPage = page - 1; m_Compare->aligned = false; m_Compare->manualAlignment = false; StartCompare(false); } });
  connect(m_CompareTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
    if (!m_Compare || row < 0 || row >= static_cast<int>(m_Compare->changes.size()) || m_Page->image.isNull()) return;
    const auto& region = m_Compare->changes[static_cast<std::size_t>(row)];
    const auto size = m_Document->Sizes()[m_Compare->basePage];
    const double factor = static_cast<double>(m_Page->image.width()) / std::max(1.0, static_cast<double>(size.wPt));
    const int x = qRound((region.x0 + region.x1) * 0.5 * factor);
    const int y = qRound((size.hPt - (region.y0 + region.y1) * 0.5) * factor);
    m_Scroll->horizontalScrollBar()->setValue(std::max(0, x - m_Scroll->viewport()->width() / 2));
    m_Scroll->verticalScrollBar()->setValue(std::max(0, y - m_Scroll->viewport()->height() / 2));
  });
  connect(closeCompare, &QPushButton::clicked, this, [this] {
    m_CompareDialog->hide(); m_ComparePicking = false;
    if (m_Compare && m_Compare->busy) { m_Compare->cancel = true; m_CompareClosePending = true; }
    else { m_Compare.reset(); m_Page->update(); }
  });
  m_ComparePoll = new QTimer(this); m_ComparePoll->setInterval(30);
  connect(m_ComparePoll, &QTimer::timeout, this, [this] { PollCompare(); });
}

PdfWindow::~PdfWindow() {
  if (m_Compare) m_Compare->cancel = true;
  if (m_CompareWork.valid()) m_CompareWork.wait();
  m_AuditCancel = true;
  if (m_AuditWork.valid()) m_AuditWork.wait();
  for (auto& page : m_SnapPages) *page.second.cancel = true;
  m_SnapPages.clear(); // each reader holds the document: the futures wait here
  CancelSearch(true);
  m_Thumbs->SetDocument(nullptr); // the document is destroyed before the model; the worker must be done with it
  m_Cancel = true;
  if (m_Work.valid()) {
    try { m_Work.wait(); } // Document outlives every use on the worker.
    catch (const std::exception& error) { qWarning() << "PDF worker wait failed during shutdown:" << error.what(); }
  }
  if (m_OpenWork.valid()) {
    try { m_OpenWork.wait(); } catch (const std::exception& error) { qWarning() << "PDF open wait failed during shutdown:" << error.what(); }
  }
  if (m_SaveWork.valid()) {
    try { m_SaveWork.wait(); } catch (const std::exception& error) { qWarning() << "PDF save wait failed during shutdown:" << error.what(); }
  }
}

void PdfWindow::Open(const QString& path) {
  if (!ConfirmDiscard()) return;
  if (m_OpenWork.valid() || m_Work.valid()) { statusBar()->showMessage("Wait for the current PDF operation before opening another file."); return; }
  m_OpenPath = path;
  const auto nativePath = std::make_shared<std::filesystem::path>(path.toStdWString());
  m_OpenWork = std::async(std::launch::async, [nativePath]() noexcept {
    try { return pdfview::PdfDocument::Open(*nativePath); }
    catch (...) { return pdfview::PdfDocument::OpenResult{}; }
  });
  statusBar()->showMessage("Opening PDF...");
  m_OpenPoll->start();
}

bool PdfWindow::LoadFile(const QString& path, QString& error) {
  if (m_Work.valid()) {
    error = "Wait for the current page to finish before opening another PDF.";
    return false;
  }
  m_AuditCancel = true;
  if (m_AuditWork.valid()) { m_AuditWork.wait(); m_AuditWork = {}; }
  m_AuditPoll->stop(); m_AuditReady = false; m_AuditPage = -1;
  if (m_Compare) m_Compare->cancel = true;
  if (m_CompareWork.valid()) { m_CompareWork.wait(); m_CompareWork = {}; }
  m_ComparePoll->stop(); m_Compare.reset(); m_ComparePicking = false; m_CompareClosePending = false;
  auto result = pdfview::PdfDocument::Open(std::filesystem::path(path.toStdWString()));
  if (!result.doc) { error = QString::fromUtf8(result.error.c_str()); return false; }
  return AdoptDocument(std::move(result.doc), path, error);
}

void PdfWindow::PollOpen() {
  if (!m_OpenWork.valid() || m_OpenWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_OpenPoll->stop();
  pdfview::PdfDocument::OpenResult result;
  try { result = m_OpenWork.get(); }
  catch (const std::exception& error) { result.error = error.what(); }
  QString message;
  if (!result.doc || !AdoptDocument(std::move(result.doc), m_OpenPath, message)) {
    if (message.isEmpty()) message = result.error.empty() ? "The PDF could not be opened." : QString::fromUtf8(result.error.c_str());
    QMessageBox::warning(this, "Open PDF", message);
    statusBar()->showMessage("Could not open PDF.");
  }
}

bool PdfWindow::AdoptDocument(std::unique_ptr<pdfview::PdfDocument> document, const QString& path, QString& error) {
  if (!document) { error = "The PDF could not be opened."; return false; }
  if (m_Work.valid()) { error = "Wait for the current page to finish before opening another PDF."; return false; }
  CancelSearch(true); // the search, the snap readers and the thumbnail worker all hold the old document
  for (auto& page : m_SnapPages) *page.second.cancel = true;
  m_SnapPages.clear();
  m_Thumbs->SetDocument(nullptr);
  m_Document = std::move(document);
  m_Thumbs->SetDocument(m_Document.get());
  ClearSearch();
  m_Annotations = {}; m_Draft.clear();
  m_SelectedAnnotation = -1; m_AnnotationDragging = false; m_DragPreview.reset();
  ++m_Revision;
  m_ExtractMode = false; m_ExtractDragging = false; m_ExtractResult.reset();
  if (m_ExtractReview) m_ExtractReview->hide();
  m_RobustPicks.clear(); m_SelectedCheck = -1; m_SnapHit = false; m_SnapPrevValid = false;
  m_FileScales = pdfview::ReadPageScales(m_Document->Path());
  if (!m_FileScales.error.empty()) qWarning() << "PDF scale: could not read the scales in" << path << "-" << m_FileScales.error.c_str();
  for (const auto& page : m_FileScales.unusable)
    qWarning() << "PDF scale: page" << page.first + 1 << "of" << path << "has a scale that cannot be used -" << page.second.c_str();
  const QSignalBlocker pageBlock(m_PageNumber);
  m_PageNumber->setRange(1, m_Document->PageCount());
  m_PageNumber->setValue(1);
  SyncThumbnail();
  setWindowTitle("GoSurvey | " + path + "[*]"); setWindowModified(false);
  UpdateScaleDisplay();
  if (m_SnapOn) StartSnapRead(0);
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
  const auto* scale = EffectiveScale(m_PageNumber->value() - 1);
  return scale ? *scale : pdfview::PageScale{0, pdfview::Unit::Inch, 0};
}

bool PdfWindow::AddAnnotation(const pdfview::Annot& annotation, QString& error) {
  if (!m_Document || annotation.page < 0 || annotation.page >= m_Document->PageCount()) { error = "Open a PDF page first."; return false; }
  if (!std::isfinite(annotation.x0) || !std::isfinite(annotation.y0) || !std::isfinite(annotation.x1) || !std::isfinite(annotation.y1)) {
    error = "Annotation coordinates must be finite."; return false;
  }
  for (const auto& point : annotation.pts)
    if (!std::isfinite(point.first) || !std::isfinite(point.second)) { error = "Measurement coordinates must be finite."; return false; }
  const auto* inForce = EffectiveScale(annotation.page);
  const auto scale = inForce ? *inForce : pdfview::PageScale{0, pdfview::Unit::Inch, 0};
  if (annotation.IsDimension() && (!pdfview::DimensionComplete(annotation) ||
      (annotation.kind != pdfview::Annot::Kind::Angle && !scale.Valid()))) {
    error = "Complete the measurement and set a page scale first."; return false;
  }
  m_Annotations.Add(annotation);
  SceneChanged();
  return true;
}

QPointF PdfWindow::CanvasToPage(QPointF point) const {
  if (!m_Document || m_Page->width() <= 0 || m_Page->height() <= 0) return {};
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(m_PageNumber->value() - 1)];
  return {point.x() * size.wPt / m_Page->width(), (m_Page->height() - point.y()) * size.hPt / m_Page->height()};
}

int PdfWindow::HitAnnotation(QPointF p, float tolerance) const {
  const int page = m_PageNumber->value() - 1;
  const auto& items = m_Annotations.Items();
  const auto segmentDistance = [](QPointF p, QPointF a, QPointF b) {
    const QPointF d = b - a;
    const double denom = d.x() * d.x() + d.y() * d.y();
    const double t = denom > 1e-12 ? std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / denom, 0.0, 1.0) : 0.0;
    return std::hypot(p.x() - (a.x() + t * d.x()), p.y() - (a.y() + t * d.y()));
  };
  for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i) {
    const auto& a = items[static_cast<std::size_t>(i)];
    if (a.page != page) continue;
    if (a.kind == pdfview::Annot::Kind::Line)
      { if (segmentDistance(p, {a.x0,a.y0}, {a.x1,a.y1}) <= tolerance) return i; }
    else if (a.IsDimension()) {
      for (std::size_t j = 1; j < a.pts.size(); ++j)
        if (segmentDistance(p, {a.pts[j-1].first,a.pts[j-1].second}, {a.pts[j].first,a.pts[j].second}) <= tolerance) return i;
      if (a.kind == pdfview::Annot::Kind::Length && a.pts.size() == 2) {
        const auto d = pdfview::LengthDimLine(a);
        if (segmentDistance(p, {d.x0,d.y0}, {d.x1,d.y1}) <= tolerance) return i;
      }
    } else {
      const QRectF box(QPointF(a.x0,a.y0), QPointF(a.x1,a.y1));
      if (box.normalized().adjusted(-tolerance,-tolerance,tolerance,tolerance).contains(p)) return i;
    }
  }
  return -1;
}

void PdfWindow::ApplyAnnotationStyle(pdfview::Annot& a) const {
  a.color = m_AnnotationColor;
  a.thickness = m_AnnotationThickness;
  a.fontSize = m_AnnotationFontSize;
  a.fill = m_AnnotationFill && (a.kind == pdfview::Annot::Kind::Rect || a.kind == pdfview::Annot::Kind::Ellipse);
  if (a.kind == pdfview::Annot::Kind::Text) {
    a.font = m_AnnotationFont; a.bold = m_AnnotationBold; a.italic = m_AnnotationItalic;
  }
}

void PdfWindow::EditSelectedAnnotation() {
  if (m_SelectedAnnotation < 0 || m_SelectedAnnotation >= static_cast<int>(m_Annotations.Items().size())) return;
  const auto& current = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
  if (current.kind != pdfview::Annot::Kind::Text) return;
  bool accepted = false;
  const QString value = QInputDialog::getMultiLineText(this, "Edit PDF text", "Text", QString::fromUtf8(current.text.c_str()), &accepted);
  if (!accepted) return;
  if (value.isEmpty()) { statusBar()->showMessage("Text annotation cannot be empty."); return; }
  auto edited = current; edited.text = value.toUtf8().toStdString();
  if (m_Annotations.Replace(m_SelectedAnnotation, edited)) SceneChanged();
}

void PdfWindow::MoveAnnotation(QPointF point) {
  if (!m_AnnotationDragging || m_SelectedAnnotation < 0 || m_SelectedAnnotation >= static_cast<int>(m_Annotations.Items().size())) return;
  const QPointF now = CanvasToPage(point), start = CanvasToPage(m_DragStart);
  const float dx = static_cast<float>(now.x() - start.x()), dy = static_cast<float>(now.y() - start.y());
  auto edited = m_DragOriginal;
  if (m_AnnotationHandle < 0) {
    edited.x0 += dx; edited.x1 += dx; edited.y0 += dy; edited.y1 += dy;
    for (auto& p : edited.pts) { p.first += dx; p.second += dy; }
  } else if (edited.IsDimension() && m_AnnotationHandle < static_cast<int>(edited.pts.size())) {
    auto& p = edited.pts[static_cast<std::size_t>(m_AnnotationHandle)]; p.first += dx; p.second += dy;
    if (m_AnnotationHandle == 0) { edited.x0 = p.first; edited.y0 = p.second; }
    if (m_AnnotationHandle == static_cast<int>(edited.pts.size()) - 1) { edited.x1 = p.first; edited.y1 = p.second; }
  } else if (edited.kind == pdfview::Annot::Kind::Length && m_AnnotationHandle == 2 && edited.pts.size() == 2) {
    const auto line = pdfview::LengthDimLine(edited); edited.offset += dx * line.nx + dy * line.ny;
  } else {
    const int corner = m_AnnotationHandle;
    if (corner == 0 || corner == 2) edited.x0 += dx; else edited.x1 += dx;
    if (corner == 0 || corner == 3) edited.y0 += dy; else edited.y1 += dy;
  }
  m_DragPreview = std::move(edited);
  m_Page->update();
}

void PdfWindow::EndAnnotation(QPointF point, Qt::MouseButton button) {
  if (button != Qt::LeftButton || !m_AnnotationDragging) return;
  MoveAnnotation(point);
  if (m_DragPreview) {
    if (m_Annotations.Replace(m_SelectedAnnotation, *m_DragPreview)) SceneChanged();
    m_DragPreview.reset();
  }
  m_AnnotationDragging = false; m_AnnotationHandle = -1;
}

void PdfWindow::PageClick(QPointF point, Qt::MouseButton button) {
  if (m_ComparePicking) { ComparePick(point, button); return; }
  if (m_ExtractMode) return;
  if (!m_Document || (m_Tool < 0 && m_Tool != -2) || IsBusy() || m_Page->image.isNull()) return;
  UpdateSnap(point); // the click's own position, so a click without a prior move still snaps
  const auto size = m_Document->Sizes()[m_PageNumber->value() - 1];
  if (m_Tool == -2) {
    if (button != Qt::LeftButton) return;
    const QPointF pagePoint = CanvasToPage(point);
    if (m_SelectedAnnotation >= 0 && m_SelectedAnnotation < static_cast<int>(m_Annotations.Items().size())) {
      const auto& selected = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
      std::vector<QPointF> grips;
      if (selected.IsDimension()) {
        for (const auto& p : selected.pts) grips.push_back({p.first,p.second});
        if (selected.kind == pdfview::Annot::Kind::Length && selected.pts.size() == 2) {
          const auto line = pdfview::LengthDimLine(selected); grips.push_back({(line.x0+line.x1)*.5,(line.y0+line.y1)*.5});
        }
      } else if (selected.kind == pdfview::Annot::Kind::Line) grips = {{selected.x0,selected.y0},{selected.x1,selected.y1}};
      else grips = {{selected.x0,selected.y0},{selected.x1,selected.y1},{selected.x0,selected.y1},{selected.x1,selected.y0}};
      const float tol = static_cast<float>(7.0 * size.wPt / std::max(1, m_Page->width()));
      for (int i = 0; i < static_cast<int>(grips.size()); ++i) {
        if (QLineF(pagePoint, grips[static_cast<std::size_t>(i)]).length() <= tol) {
          m_AnnotationHandle = i; m_AnnotationDragging = true; m_DragStart = point; m_DragOriginal = selected; return;
        }
      }
    }
    m_SelectedAnnotation = HitAnnotation(pagePoint, static_cast<float>(5.0 * size.wPt / std::max(1, m_Page->width())));
    if (m_SelectedAnnotation >= 0) {
      m_AnnotationDragging = true; m_AnnotationHandle = -1; m_DragStart = point;
      m_DragOriginal = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
    }
    m_Page->update(); return;
  }
  if (m_Tool == kToolCalibrate || m_Tool == kToolCheck) {
    if (button != Qt::LeftButton) { m_Draft.clear(); m_Page->update(); return; }
    if (m_Tool == kToolCheck && m_Draft.empty() && m_CheckTarget == 0 && !EffectiveScale(m_PageNumber->value() - 1)) {
      statusBar()->showMessage("This page has no scale. Use Set scale... first.");
      return;
    }
    const std::size_t index = m_Draft.size();
    const auto pagePoint = CanvasToPage(point);
    m_Draft.emplace_back(m_SnapHit ? m_SnapX : static_cast<float>(pagePoint.x()), m_SnapHit ? m_SnapY : static_cast<float>(pagePoint.y()));
    if (m_Tool == kToolCalibrate && index < 2) m_CalSnapped[index] = m_SnapHit;
    if (m_Draft.size() < 2) { statusBar()->showMessage(m_Tool == kToolCalibrate ? "Click the other point of the known distance." : "Click the other end of the dimension."); m_Page->update(); return; }
    m_Page->update();
    if (m_Tool == kToolCalibrate) FinishCalibrationPicks(); else FinishCheckPicks();
    return;
  }
  float dimensionOffset = 0.f;
  if (button == Qt::LeftButton) { const auto pagePoint = CanvasToPage(point);
    const bool lengthOffsetPick = m_Tool == static_cast<int>(pdfview::Annot::Kind::Length) && m_Draft.size() == 2;
    m_Draft.emplace_back(!lengthOffsetPick && m_SnapHit ? m_SnapX : static_cast<float>(pagePoint.x()),
                         !lengthOffsetPick && m_SnapHit ? m_SnapY : static_cast<float>(pagePoint.y()));
    if (m_Tool == static_cast<int>(pdfview::Annot::Kind::Length) && m_Draft.size() == 2) {
      statusBar()->showMessage("Click where the dimension line should sit."); m_Page->update(); return;
    }
    if (m_Tool == static_cast<int>(pdfview::Annot::Kind::Length) && m_Draft.size() == 3) {
      pdfview::Annot baseline; baseline.kind = pdfview::Annot::Kind::Length; baseline.pts = {m_Draft[0], m_Draft[1]};
      const auto line = pdfview::LengthDimLine(baseline);
      dimensionOffset = (m_Draft[2].first - m_Draft[0].first) * line.nx + (m_Draft[2].second - m_Draft[0].second) * line.ny;
      m_Draft.resize(2);
    }
  }
  const auto kind = static_cast<pdfview::Annot::Kind>(m_Tool);
  const bool multi = kind == pdfview::Annot::Kind::PolyLength || kind == pdfview::Annot::Kind::Area;
  const std::size_t required = kind == pdfview::Annot::Kind::Angle || kind == pdfview::Annot::Kind::Area ? 3 : 2;
  if (button == Qt::RightButton && (!multi || m_Draft.size() < required)) { m_Draft.clear(); m_Page->update(); return; }
  if (m_Draft.size() < required || (multi && button != Qt::RightButton)) { m_Page->update(); return; }
  pdfview::Annot annotation;
  annotation.kind = kind; annotation.page = m_PageNumber->value() - 1;
  ApplyAnnotationStyle(annotation);
  annotation.x0 = m_Draft.front().first; annotation.y0 = m_Draft.front().second;
  annotation.x1 = m_Draft.back().first; annotation.y1 = m_Draft.back().second;
  if (annotation.IsDimension()) annotation.pts = m_Draft;
  annotation.offset = dimensionOffset;
  if (kind == pdfview::Annot::Kind::Text) {
    bool accepted = false;
    const auto text = QInputDialog::getMultiLineText(this, "PDF text", "Text", {}, &accepted);
    if (!accepted || text.isEmpty()) { m_Draft.clear(); return; }
    annotation.text = text.toUtf8().toStdString();
    annotation.font = m_AnnotationFont; annotation.bold = m_AnnotationBold; annotation.italic = m_AnnotationItalic;
  }
  QString error;
  if (!AddAnnotation(annotation, error)) QMessageBox::warning(this, "PDF annotation", error);
  m_Draft.clear(); m_Page->update();
}

void PdfWindow::DrawExtractOverlay(QPainter& painter) {
  if (!m_ExtractDragging && !m_ExtractResult) return;
  painter.setPen(QPen(QColor("#e7a800"), 2, Qt::DashLine));
  painter.setBrush(QColor(255, 208, 64, 55));
  painter.drawRect(QRectF(m_ExtractStart, m_ExtractEnd).normalized());
}

void PdfWindow::BeginExtract(QPointF point, Qt::MouseButton button) {
  if (!m_ExtractMode) return;
  if (button != Qt::LeftButton) {
    m_ExtractMode = false; m_Page->unsetCursor(); statusBar()->showMessage("Coordinate extraction cancelled."); return;
  }
  m_ExtractDragging = true; m_ExtractStart = m_ExtractEnd = point; m_Page->update();
}

void PdfWindow::MoveExtract(QPointF point) {
  if (!m_ExtractDragging) return;
  m_ExtractEnd = point; m_Page->update();
}

void PdfWindow::EndExtract(QPointF point, Qt::MouseButton button) {
  if (!m_ExtractDragging || button != Qt::LeftButton) return;
  m_ExtractEnd = point; m_ExtractDragging = false; m_ExtractMode = false;
  m_Page->unsetCursor(); m_Page->update();
  if (QRectF(m_ExtractStart, m_ExtractEnd).normalized().width() < 5 ||
      QRectF(m_ExtractStart, m_ExtractEnd).normalized().height() < 5) {
    statusBar()->showMessage("Selection was too small; coordinate extraction cancelled."); return;
  }
  ExtractRegion();
}

void PdfWindow::ExtractRegion() {
  if (!m_Document || m_Page->image.isNull()) return;
  std::vector<pdfview::DimText> texts;
  std::vector<pdfview::DimSeg> segments;
  if (!m_Document->AuditPageData(m_PageNumber->value() - 1, texts, segments, [] { return false; })) {
    QMessageBox::warning(this, "Extract Coordinates", "Could not read selectable text from this PDF page."); return;
  }
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(m_PageNumber->value() - 1)];
  const QRectF box = QRectF(m_ExtractStart, m_ExtractEnd).normalized();
  const float x0 = static_cast<float>(box.left() * size.wPt / m_Page->width());
  const float x1 = static_cast<float>(box.right() * size.wPt / m_Page->width());
  const float y0 = static_cast<float>((m_Page->height() - box.bottom()) * size.hPt / m_Page->height());
  const float y1 = static_cast<float>((m_Page->height() - box.top()) * size.hPt / m_Page->height());
  m_ExtractResult = pdfview::ExtractCoordinates(texts, x0, y0, x1, y1);
  if (m_ExtractResult->status != pdfview::ExtractStatus::Ok) {
    QMessageBox::information(this, "Extract Coordinates", QString::fromUtf8(m_ExtractResult->message.c_str()));
    m_ExtractResult.reset(); return;
  }
  ShowExtractReview();
}

void PdfWindow::ShowExtractReview() {
  if (!m_ExtractResult) return;
  if (!m_ExtractReview) {
    m_ExtractReview = new QDialog(this, Qt::Window);
    m_ExtractReview->setObjectName("pdfCoordinateReview");
    m_ExtractReview->setStyleSheet(QString::fromLatin1(kPdfExtractReviewStyle));
    m_ExtractReview->setWindowTitle("Review extracted coordinates");
    m_ExtractReview->resize(1040, 380);
    auto* layout = new QVBoxLayout(m_ExtractReview);
    auto* intro = new QLabel("Review each candidate against its source text. Accept creates drawing geometry; Deny discards it.", m_ExtractReview);
    intro->setWordWrap(true); layout->addWidget(intro);
    auto* table = new QTableWidget(0, 8, m_ExtractReview); table->setObjectName("pdfCoordinateCandidates");
    table->setHorizontalHeaderLabels({"Source text", "Northing", "Easting", "Elevation", "Description", "Point #", "Creates", "Decision"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->setWordWrap(true); table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table);
    auto* close = new QPushButton("Close", m_ExtractReview); close->setObjectName("pdfCoordinateReviewClose");
    connect(close, &QPushButton::clicked, m_ExtractReview, &QDialog::hide); layout->addWidget(close, 0, Qt::AlignRight);
  }
  auto* table = m_ExtractReview->findChild<QTableWidget*>("pdfCoordinateCandidates");
  table->setRowCount(static_cast<int>(m_ExtractResult->candidates.size()));
  for (int row = 0; row < table->rowCount(); ++row) {
    const auto& c = m_ExtractResult->candidates[static_cast<std::size_t>(row)];
    const QString cells[] = {QString::fromUtf8(c.sourceText.c_str()), QString::number(c.northing, 'f', 3),
      QString::number(c.easting, 'f', 3), c.elevation ? QString::number(*c.elevation, 'f', 3) : "—",
      c.description.empty() ? "—" : QString::fromUtf8(c.description.c_str()),
      c.pointNumber ? QString::number(*c.pointNumber) : "Auto", c.IsSurveyPoint() ? "Survey point" : "Circle", {}};
    for (int col = 0; col < 7; ++col) table->setItem(row, col, new QTableWidgetItem(cells[col]));
    auto* decisions = new QWidget(table); auto* buttons = new QHBoxLayout(decisions); buttons->setContentsMargins(2, 2, 2, 2);
    auto* accept = new QPushButton("Accept", decisions); auto* deny = new QPushButton("Deny", decisions);
    accept->setObjectName(QString("pdfCoordinateAccept_%1").arg(row)); deny->setObjectName(QString("pdfCoordinateDeny_%1").arg(row));
    buttons->addWidget(accept); buttons->addWidget(deny); table->setCellWidget(row, 7, decisions);
    connect(accept, &QPushButton::clicked, this, [this, row] { DecideExtractCandidate(row, true); });
    connect(deny, &QPushButton::clicked, this, [this, row] { DecideExtractCandidate(row, false); });
  }
  table->resizeRowsToContents();
  m_ExtractReview->show(); m_ExtractReview->raise();
}

void PdfWindow::DecideExtractCandidate(int row, bool accept) {
  if (!m_ExtractResult || row < 0 || row >= static_cast<int>(m_ExtractResult->candidates.size())) return;
  if (accept) {
    if (!m_Editor) { QMessageBox::information(this, "Extract Coordinates", "Open the PDF from an active drawing to accept coordinates."); return; }
    if (!m_Editor->CommitPdfCoordinate(m_ExtractResult->candidates[static_cast<std::size_t>(row)])) return;
  }
  m_ExtractResult->candidates.erase(m_ExtractResult->candidates.begin() + row);
  if (m_ExtractResult->candidates.empty()) {
    if (auto* table = m_ExtractReview->findChild<QTableWidget*>("pdfCoordinateCandidates")) table->setRowCount(0);
    statusBar()->showMessage(accept ? "Extracted coordinates accepted." : "Candidate denied; drawing unchanged.");
  } else ShowExtractReview();
}

void PdfWindow::DrawAnnotations(QPainter& painter) {
  if (!m_Document || m_Page->image.isNull()) return;
  const int page = m_PageNumber->value() - 1;
  const auto size = m_Document->Sizes()[page];
  const double scaleX = static_cast<double>(m_Page->width()) / size.wPt;
  const double scaleY = static_cast<double>(m_Page->height()) / size.hPt;
  const double factor = std::sqrt(scaleX * scaleY);
  auto map = [&](float x, float y) { return QPointF(x * scaleX, (size.hPt - y) * scaleY); };
  for (std::size_t itemIndex = 0; itemIndex < m_Annotations.Items().size(); ++itemIndex) {
    const auto& annotation = m_DragPreview && static_cast<int>(itemIndex) == m_SelectedAnnotation ? *m_DragPreview : m_Annotations.Items()[itemIndex];
    if (annotation.page != page) continue;
    const bool selected = static_cast<int>(itemIndex) == m_SelectedAnnotation;
    painter.setPen(QPen(selected ? QColor("#45a8df") : QColor::fromRgb(annotation.color), std::max(1.0, annotation.thickness * factor)));
    painter.setBrush(annotation.fill ? QBrush(QColor::fromRgb(annotation.color)) : Qt::NoBrush);
    const QPointF first = map(annotation.x0, annotation.y0), last = map(annotation.x1, annotation.y1);
    const QRectF rect = QRectF(first, last).normalized();
    if (annotation.kind == pdfview::Annot::Kind::Rect) painter.drawRect(rect);
    else if (annotation.kind == pdfview::Annot::Kind::Ellipse) painter.drawEllipse(rect);
    else if (annotation.kind == pdfview::Annot::Kind::Line) painter.drawLine(first, last);
    else if (annotation.kind == pdfview::Annot::Kind::Text) {
      QFont font(QString::fromStdString(annotation.font)); font.setBold(annotation.bold); font.setItalic(annotation.italic);
      font.setPixelSize(std::max(1, qRound(annotation.fontSize * factor)));
      painter.setFont(font); painter.drawText(rect, Qt::TextWordWrap, QString::fromUtf8(annotation.text.c_str()));
    } else if (annotation.IsDimension()) {
      QPolygonF polygon;
      for (const auto& pt : annotation.pts) polygon << map(pt.first, pt.second);
      painter.drawPolyline(polygon);
      if (annotation.kind == pdfview::Annot::Kind::Area) painter.drawLine(polygon.first(), polygon.last());
      const auto anchor = pdfview::DimensionLabelAnchor(annotation);
      painter.drawText(map(anchor.first, anchor.second), QString::fromUtf8(pdfview::DimensionLabel(annotation, CurrentScale()).c_str()));
    }
    if (selected) {
      std::vector<QPointF> grips;
      if (annotation.IsDimension()) {
        for (const auto& pt : annotation.pts) grips.push_back(map(pt.first, pt.second));
        if (annotation.kind == pdfview::Annot::Kind::Length && annotation.pts.size() == 2) {
          const auto dim = pdfview::LengthDimLine(annotation); grips.push_back(map((dim.x0+dim.x1)*.5f,(dim.y0+dim.y1)*.5f));
        }
      } else if (annotation.kind == pdfview::Annot::Kind::Line) grips = {map(annotation.x0,annotation.y0),map(annotation.x1,annotation.y1)};
      else grips = {map(annotation.x0,annotation.y0),map(annotation.x1,annotation.y1),map(annotation.x0,annotation.y1),map(annotation.x1,annotation.y0)};
      painter.setPen(QPen(QColor("#0d1521"), 1)); painter.setBrush(QColor("#8dd7ff"));
      for (const auto& grip : grips) painter.drawRect(QRectF(grip.x()-4,grip.y()-4,8,8));
    }
  }
  painter.setPen(QPen(QColor("#45a8df"), 2));
  for (std::size_t i = 0; m_Tool != kToolCalibrate && m_Tool != kToolCheck && i < m_Draft.size(); ++i) {
    const auto pt = map(m_Draft[i].first, m_Draft[i].second);
    painter.drawEllipse(pt, 3, 3);
    if (i) painter.drawLine(map(m_Draft[i - 1].first, m_Draft[i - 1].second), pt);
  }
}

bool PdfWindow::SaveFile(const QString& path, QString& error) {
  if (!m_Document) { error = "Open a PDF first."; return false; }
  const auto reason = pdfview::SaveAnnotated(m_Document->Path(), m_Annotations.Items(),
      std::filesystem::path(path.toStdWString()), m_Annotations.Scales(), ScalesInForce());
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
  StartSaveAs(path);
}

void PdfWindow::StartSaveAs(const QString& path) {
  if (!m_Document || path.isEmpty()) return;
  if (m_SaveWork.valid()) { statusBar()->showMessage("A PDF save is already in progress."); return; }
  auto source = m_Document->Path();
  auto items = m_Annotations.Items();
  auto scales = m_Annotations.Scales();
  auto inForce = ScalesInForce();
  auto destination = std::filesystem::path(path.toStdWString());
  using SaveInput = std::tuple<std::filesystem::path, std::vector<pdfview::Annot>,
      std::map<int, pdfview::PageScale>, std::map<int, pdfview::PageScale>, std::filesystem::path>;
  auto input = std::make_shared<SaveInput>(std::move(source), std::move(items), std::move(scales), std::move(inForce), std::move(destination));
  m_SaveDestination = path;
  m_SaveRevision = m_Revision;
  m_SaveWork = std::async(std::launch::async, [input]() noexcept {
    try {
      auto error = pdfview::SaveAnnotated(std::get<0>(*input), std::get<1>(*input), std::get<4>(*input), std::get<2>(*input), std::get<3>(*input));
      return SaveResult{error.empty(), std::move(error)};
    }
    catch (...) { return SaveResult{}; }
  });
  statusBar()->showMessage("Saving annotated PDF...");
  m_SavePoll->start();
}

void PdfWindow::PollSave() {
  if (!m_SaveWork.valid() || m_SaveWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_SavePoll->stop();
  SaveResult result;
  try { result = m_SaveWork.get(); }
  catch (...) { result = SaveResult{}; }
  if (!result.succeeded) {
    const auto reason = result.error.empty() ? QString("The PDF could not be saved.") : QString::fromUtf8(result.error.c_str());
    QMessageBox::warning(this, "Save PDF", reason);
    statusBar()->showMessage("Save failed; annotations are still available to retry.");
    return;
  }
  if (m_Revision == m_SaveRevision) {
    m_Annotations.MarkSaved(); setWindowModified(false);
  }
  statusBar()->showMessage("Saved " + m_SaveDestination);
}

bool PdfWindow::ConfirmDiscard() {
  if (!HasUnsavedChanges()) return true;
  const auto answer = QMessageBox::question(this, "Unsaved PDF annotations", "Save PDF annotations before closing?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Save) { SaveAs(); return !HasUnsavedChanges(); }
  return answer == QMessageBox::Discard;
}

void PdfWindow::closeEvent(QCloseEvent* event) {
  if (m_OpenWork.valid()) { statusBar()->showMessage("Wait for the PDF to finish opening before closing."); event->ignore(); return; }
  if (m_SaveWork.valid()) { statusBar()->showMessage("Wait for the PDF save to finish before closing."); event->ignore(); return; }
  if (!ConfirmDiscard()) { event->ignore(); return; }
  m_Cancel = true; m_SearchCancel = true; m_AuditCancel = true;
  if (m_Compare) m_Compare->cancel = true;
  event->accept();
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
  if (m_ComparePicking) {
    m_ComparePicking = false;
    m_CompareStatus->setText("Manual alignment cancelled. The current alignment is unchanged.");
  }
  if (m_ExtractMode || m_ExtractDragging) {
    m_ExtractMode = false; m_ExtractDragging = false; m_Page->unsetCursor(); m_Page->update();
    statusBar()->showMessage("Coordinate extraction cancelled.");
  }
  else if (IsSearching()) CancelSearch(false); // the poll reports "Search cancelled"
  else if (m_Tool >= 0 || !m_Draft.empty()) EscapeTool(); // REQ-430: a pick in progress, then the tool
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
  const auto text = std::make_shared<const std::u16string>(query); // shared so the worker's capture copy cannot throw
  m_Search = std::async(std::launch::async, [this, document, text, matchCase] {
    SearchOutcome outcome;
    try { outcome.ok = document->SearchText(*text, matchCase, outcome.result, [this] { return m_SearchCancel.load(); }); }
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
  const double scaleX = static_cast<double>(m_Page->width()) / size.wPt;
  const double scaleY = static_cast<double>(m_Page->height()) / size.hPt;
  painter.setPen(Qt::NoPen);
  for (int i = 0; i < static_cast<int>(m_Hits.size()); ++i) {
    const auto& hit = m_Hits[static_cast<std::size_t>(i)];
    if (hit.page != page) continue;
    painter.setBrush(i == m_HitIndex ? QColor(255, 140, 0, 150) : QColor(255, 230, 0, 110));
    for (const auto& box : hit.boxes)
      painter.drawRect(QRectF(QPointF(box.x0 * scaleX, (size.hPt - box.y1) * scaleY),
                              QPointF(box.x1 * scaleX, (size.hPt - box.y0) * scaleY)));
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

void PdfWindow::ShowScaleAudit() {
  m_AuditDialog->show();
  m_AuditDialog->raise();
  if (m_Document && !m_AuditWork.valid()) StartScaleAudit();
  else if (!m_Document) m_AuditSummary->setText("Open a PDF before auditing a page.");
}

void PdfWindow::StartScaleAudit() {
  if (!m_Document) { m_AuditSummary->setText("Open a PDF before auditing a page."); return; }
  if (m_AuditWork.valid()) { m_AuditSummary->setText("An audit is already running. Cancel it before starting another."); return; }
  const int page = m_PageNumber->value() - 1;
  m_AuditPage = page;
  m_AuditReady = false;
  m_AuditMatches = {};
  m_AuditResult = {};
  m_AuditCancel = false;
  m_AuditTable->setRowCount(0);
  m_AuditSummary->setText(QString("Reading dimension text and line work on page %1…").arg(page + 1));
  auto* document = m_Document.get();
  m_AuditWork = std::async(std::launch::async, [this, document, page] {
    AuditOutcome outcome;
    std::vector<pdfview::DimText> texts;
    std::vector<pdfview::DimSeg> segments;
    const auto cancelled = [this] { return m_AuditCancel.load(); };
    if (!document->AuditPageData(page, texts, segments, cancelled)) return outcome;
    pdfview::DimMatchSet matches;
    if (!pdfview::MatchDimensions(texts, segments, pdfview::DimMatchParams{}, cancelled, matches)) return outcome;
    outcome.ok = true;
    outcome.matches = std::move(matches);
    return outcome;
  });
  m_AuditPoll->start();
}

void PdfWindow::PollScaleAudit() {
  if (!m_AuditWork.valid() || m_AuditWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_AuditPoll->stop();
  AuditOutcome outcome;
  try { outcome = m_AuditWork.get(); }
  catch (const std::exception& error) { qWarning() << "PDF scale audit failed:" << error.what(); }
  if (m_AuditCancel) {
    m_AuditMatches = {};
    m_AuditResult = {};
    m_AuditReady = false;
    m_AuditSummary->setText("Audit cancelled. No partial result was kept.");
    return;
  }
  if (!outcome.ok) {
    m_AuditSummary->setText("This page could not be read. No result was kept.");
    return;
  }
  m_AuditMatches = std::move(outcome.matches);
  const auto* scale = EffectiveScale(m_AuditPage);
  const pdfview::PageScale unknown{};
  m_AuditResult = pdfview::AuditDimensions(m_AuditMatches, scale ? *scale : unknown, m_Limits);
  m_AuditSelected = !m_AuditResult.offenders.empty() ? m_AuditResult.offenders.front() :
      (!m_AuditMatches.matches.empty() ? 0 : -1);
  m_AuditReady = true;
  RefreshScaleAudit();
}

void PdfWindow::RefreshScaleAudit() {
  if (!m_AuditReady || !m_Document || m_AuditPage < 0) return;
  const auto* scale = EffectiveScale(m_AuditPage);
  const auto& matches = m_AuditMatches.matches;
  QString summary = QString("Page %1: %2 dimension(s) matched; %3 text run(s) had no dimension line; %4 dimension line(s) had no text.")
      .arg(m_AuditPage + 1).arg(matches.size()).arg(m_AuditMatches.unmatchedText).arg(m_AuditMatches.unmatchedLines);
  if (scale) summary += " Current scale: " + QString::fromUtf8(scale->RatioText().c_str()) + ".";
  else summary += " This page has no scale to judge; set one to see Good / Check / Blunder verdicts.";
  if (!m_AuditResult.message.empty()) summary += " " + QString::fromUtf8(m_AuditResult.message.c_str());
  if (scale) summary += QString(" Verdicts: %1 Good, %2 Check, %3 Blunder.").arg(m_AuditResult.good).arg(m_AuditResult.check).arg(m_AuditResult.blunder);
  if (m_AuditResult.consensusValid) {
    const auto consensus = pdfview::ScaleFromConsensus(m_AuditResult.consensusMetresPerPt,
        static_cast<int>(matches.size()), scale ? *scale : pdfview::PageScale{});
    summary += " Consensus: " + QString::fromUtf8(consensus.RatioText().c_str()) + ".";
  }
  m_AuditSummary->setText(summary);

  std::vector<int> rows;
  if (m_AuditAllRows->isChecked()) {
    rows.reserve(matches.size());
    for (int i = 0; i < static_cast<int>(matches.size()); ++i) rows.push_back(i);
  } else rows = m_AuditResult.offenders;
  m_AuditRows = rows;
  m_AuditTable->setRowCount(static_cast<int>(rows.size()));
  for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
    const int index = rows[static_cast<std::size_t>(row)];
    const auto& match = matches[static_cast<std::size_t>(index)];
    const auto& result = m_AuditResult.results[static_cast<std::size_t>(index)];
    pdfview::PageScale implied = scale ? *scale : pdfview::PageScale{};
    implied.realValue = match.metresPerPt * 72.0 / pdfview::UnitInMetres(implied.realUnit);
    QStringList cells{QString::fromUtf8(match.text.c_str()),
      QString("%1 %2").arg(result.measured, 0, 'f', 3).arg(pdfview::UnitLabel(match.unit)),
      QString("%1% ").arg(result.pct, 0, 'f', 3), QString::fromUtf8(implied.RatioText().c_str()),
      QString::fromUtf8(pdfview::VerdictName(result.verdict)),
      QString("(%1, %2)").arg(match.tx, 0, 'f', 1).arg(match.ty, 0, 'f', 1)};
    for (int column = 0; column < cells.size(); ++column) {
      auto* item = new QTableWidgetItem(cells[column]);
      if (column == 4) {
        const auto color = result.verdict == pdfview::Verdict::Good ? QColor("#48b878") :
          result.verdict == pdfview::Verdict::Check ? QColor("#e5a83b") : QColor("#e05b5b");
        item->setForeground(color);
      }
      m_AuditTable->setItem(row, column, item);
    }
  }
  if (auto* use = m_AuditDialog->findChild<QPushButton*>("pdfAuditUseConsensus"))
    use->setEnabled(m_AuditResult.consensusValid && scale != nullptr);
}

void PdfWindow::DrawAuditOverlay(QPainter& painter) {
  if (!m_AuditShowSheet || !m_AuditShowSheet->isChecked() || !m_AuditReady || !m_Document ||
      m_AuditPage != m_PageNumber->value() - 1 || m_AuditSelected < 0 ||
      m_AuditSelected >= static_cast<int>(m_AuditMatches.matches.size()) || m_Page->image.isNull()) return;
  const auto& match = m_AuditMatches.matches[static_cast<std::size_t>(m_AuditSelected)];
  const auto size = m_Document->Sizes()[m_AuditPage];
  const double factor = static_cast<double>(m_Page->image.width()) / std::max(1.0, static_cast<double>(size.wPt));
  const QPointF first(match.x0 * factor, (size.hPt - match.y0) * factor);
  const QPointF last(match.x1 * factor, (size.hPt - match.y1) * factor);
  const auto& result = m_AuditResult.results[static_cast<std::size_t>(m_AuditSelected)];
  const QColor color = result.verdict == pdfview::Verdict::Good ? QColor("#37c878") :
      result.verdict == pdfview::Verdict::Check ? QColor("#f2b544") : QColor("#ef5350");
  painter.save();
  painter.setPen(QPen(color, 3)); painter.setBrush(QColor(color.red(), color.green(), color.blue(), 65));
  painter.drawLine(first, last);
  painter.drawEllipse(first, 6, 6); painter.drawEllipse(last, 6, 6);
  painter.drawText(QRectF((first.x() + last.x()) / 2 - 50, (first.y() + last.y()) / 2 - 24, 100, 20),
      Qt::AlignCenter, QString("%1: %2%%").arg(QString::fromUtf8(pdfview::VerdictName(result.verdict))).arg(result.pct, 0, 'f', 2));
  painter.restore();
}

void PdfWindow::BeginCompare() {
  if (!m_Document) { QMessageBox::information(this, "Compare revisions", "Open the base PDF before comparing revisions."); return; }
  const QString path = QFileDialog::getOpenFileName(this, "Choose revision PDF", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  if (QFileInfo(path).absoluteFilePath() == QString::fromStdWString(m_Document->Path().wstring())) {
    QMessageBox::warning(this, "Compare revisions", "Choose a different PDF file for the revision."); return;
  }
  if (m_Compare && m_CompareWork.valid()) { QMessageBox::warning(this, "Compare revisions", "Wait for the current comparison to finish."); return; }
  m_Compare = std::make_unique<CompareSession>();
  m_Compare->source = path;
  m_Compare->basePage = m_PageNumber->value() - 1;
  m_Compare->revisionPage = 0;
  {
    const QSignalBlocker baseBlock(m_CompareBasePage);
    const QSignalBlocker revisionBlock(m_CompareRevisionPage);
    m_CompareBasePage->setRange(1, m_Document->PageCount());
    m_CompareBasePage->setValue(m_Compare->basePage + 1);
    m_CompareRevisionPage->setRange(1, 1); m_CompareRevisionPage->setValue(1);
  }
  m_Compare->mode = m_CompareMode->currentIndex();
  m_CompareDialog->show(); m_CompareDialog->raise();
  StartCompare(false);
}

void PdfWindow::StartCompare(bool findChanges) {
  if (!m_Document || !m_Compare || m_Compare->busy || m_CompareWork.valid()) return;
  m_Compare->cancel = false;
  m_Compare->busy = true;
  m_CompareProgress->show();
  if (!findChanges) m_Compare->haveChanges = false;
  m_CompareBasePage->setEnabled(false); m_CompareRevisionPage->setEnabled(false);
  m_CompareStatus->setText(findChanges ? "Aligning sheets and finding changes…" : "Rendering and aligning the two pages…");
  m_CompareTable->setRowCount(0);
  auto* base = m_Document.get();
  auto* existingRevision = m_Compare->revision.get();
  const auto path = std::filesystem::path(m_Compare->source.toStdWString());
  const auto cancel = &m_Compare->cancel;
  const int bp = m_Compare->basePage, rp = m_Compare->revisionPage;
  const auto requestedTransform = m_Compare->transform;
  const auto diffSettings = m_Compare->settings;
  const bool hadAlignment = m_Compare->aligned;
  m_CompareWork = std::async(std::launch::async, [base, existingRevision, path, cancel, bp, rp, requestedTransform, diffSettings, hadAlignment, findChanges]() noexcept { // NOLINT(bugprone-exception-escape): catches worker failures; clang-tidy attributes potential std::async/STL allocation exceptions to the call operator.
    CompareOutcome out;
    try {
    std::unique_ptr<pdfview::PdfDocument> opened;
    pdfview::PdfDocument* revision = existingRevision;
    if (!revision) {
      auto result = pdfview::PdfDocument::Open(path);
      if (!result.doc) { out.error = result.error; return out; }
      opened = std::move(result.doc); revision = opened.get();
    }
    if (bp < 0 || bp >= base->PageCount() || rp < 0 || rp >= revision->PageCount()) return out;
    const auto bs = base->Sizes()[bp]; const auto rs = revision->Sizes()[rp];
    if (!(bs.wPt > 0 && bs.hPt > 0 && rs.wPt > 0 && rs.hPt > 0) ||
        !std::isfinite(bs.wPt) || !std::isfinite(bs.hPt) || !std::isfinite(rs.wPt) || !std::isfinite(rs.hPt)) {
      out.error = "A selected page has invalid dimensions."; return out;
    }
    constexpr double maxPixels = 4.0 * 1024.0 * 1024.0;
    const double basePixelsPerPt = std::sqrt(maxPixels / (static_cast<double>(bs.wPt) * bs.hPt));
    const double revisionPixelsPerPt = std::sqrt(maxPixels / (static_cast<double>(rs.wPt) * rs.hPt));
    const double pxPerPt = std::min({2.0, 1500.0 / bs.wPt, 8192.0 / bs.hPt, 4096.0 / rs.wPt,
                                     8192.0 / rs.hPt, basePixelsPerPt, revisionPixelsPerPt});
    const int bw = std::max(64, qRound(bs.wPt * pxPerPt));
    const int bh = std::max(64, qRound(bs.hPt * pxPerPt));
    const int rw = std::max(64, qRound(rs.wPt * pxPerPt));
    const int rh = std::max(64, qRound(rs.hPt * pxPerPt));
    if (!base->RenderPage(bp, bw, bh, out.base, [cancel] { return cancel->load(); }) ||
        !revision->RenderPage(rp, rw, rh, out.revision, [cancel] { return cancel->load(); })) return out;
    out.alignment = pdfalign::AutoAlign(out.base, bs.hPt, out.revision, rs.hPt, pxPerPt, [cancel] { return cancel->load(); });
    out.hasAlignment = true;
    out.automaticTransform = out.alignment.xf;
    if (hadAlignment) out.alignment.xf = requestedTransform;
    pdfalign::ResampleAligned(out.revision, rs.hPt, static_cast<float>(pxPerPt), out.alignment.xf,
        bw, bh, static_cast<float>(pxPerPt), bs.hPt, out.aligned);
    std::vector<pdfview::ObjBox> baseBoxes, revisionBoxes;
    const bool hasBaseBoxes = base->TextBoxes(bp, baseBoxes, [cancel] { return cancel->load(); });
    const bool hasRevisionBoxes = revision->TextBoxes(rp, revisionBoxes, [cancel] { return cancel->load(); });
    const auto baseUnits = hasBaseBoxes ? pdfalign::BoxesToPixels(baseBoxes, {}, bs.hPt, pxPerPt, bw, bh) : std::vector<pdfalign::PixRect>{};
    const auto revisionUnits = hasRevisionBoxes ? pdfalign::BoxesToPixels(revisionBoxes, out.alignment.xf, bs.hPt, pxPerPt, bw, bh) : std::vector<pdfalign::PixRect>{};
    const uint8_t blue[3] = {255, 55, 30}, red[3] = {30, 55, 255};
    pdfview::Bitmap baseOnly, revisionOnly;
    pdfalign::MarkOnlyIn(out.base, out.aligned, blue, baseOnly, pxPerPt, baseUnits);
    pdfalign::MarkOnlyIn(out.aligned, out.base, red, revisionOnly, pxPerPt, revisionUnits);
    pdfalign::TintFromMarks(out.base, out.aligned, baseOnly, revisionOnly, out.tint);
    out.baseView = out.base; out.revisionView = out.aligned;
    for (size_t i = 0; i < out.base.bgra.size() / 4; ++i) {
      if (baseOnly.bgra[i * 4 + 3]) std::copy_n(baseOnly.bgra.data() + i * 4, 3, out.baseView.bgra.data() + i * 4);
      if (revisionOnly.bgra[i * 4 + 3]) std::copy_n(revisionOnly.bgra.data() + i * 4, 3, out.revisionView.bgra.data() + i * 4);
    }
    if (findChanges && !cancel->load()) {
      auto found = pdfdiff::FindChanges(out.base, out.aligned, pxPerPt, bs.hPt, diffSettings, {}, {}, [cancel] { return cancel->load(); });
      if (!found.cancelled) out.changes = std::move(found);
      else return out;
    }
    if (cancel->load()) return out;
    out.ok = true;
    out.openedRevision = std::move(opened);
    return out;
    } catch (...) {
      return CompareOutcome{};
    }
  });
  m_ComparePoll->start();
}

void PdfWindow::PollCompare() {
  if (!m_CompareWork.valid() || m_CompareWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_ComparePoll->stop();
  CompareOutcome out;
  try { out = m_CompareWork.get(); }
  catch (const std::exception& error) { qWarning() << "PDF comparison failed:" << error.what(); }
  if (!m_Compare) return;
  m_Compare->busy = false;
  m_CompareProgress->hide();
  m_CompareBasePage->setEnabled(true); m_CompareRevisionPage->setEnabled(true);
  if (m_Compare->cancel) {
    m_CompareStatus->setText("Comparison cancelled. No partial result was kept.");
    if (m_CompareClosePending) { m_Compare.reset(); m_CompareClosePending = false; m_Page->update(); }
    return;
  }
  if (!out.ok) {
    const QString reason = QString::fromUtf8(out.error.c_str());
    m_CompareStatus->setText(reason.isEmpty() ? "The pages could not be rendered or compared. Check that both PDFs are readable." : "Could not open the revision: " + reason);
    return;
  }
  if (out.openedRevision) {
    m_Compare->revision = std::move(out.openedRevision);
    const QSignalBlocker block(m_CompareRevisionPage);
    m_CompareRevisionPage->setRange(1, m_Compare->revision->PageCount());
    m_CompareRevisionPage->setValue(std::clamp(m_Compare->revisionPage + 1, 1, m_Compare->revision->PageCount()));
  }
  m_Compare->automatic = out.automaticTransform;
  if (!m_Compare->aligned) m_Compare->transform = out.alignment.xf;
  m_Compare->aligned = true;
  m_Compare->lowConfidence = !out.alignment.matched && !m_Compare->manualAlignment;
  m_Compare->overlay = QImage(out.tint.bgra.data(), out.tint.w, out.tint.h, static_cast<qsizetype>(out.tint.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->revisionOverlay = QImage(out.aligned.bgra.data(), out.aligned.w, out.aligned.h, static_cast<qsizetype>(out.aligned.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->baseView = QImage(out.baseView.bgra.data(), out.baseView.w, out.baseView.h, static_cast<qsizetype>(out.baseView.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->revisionView = QImage(out.revisionView.bgra.data(), out.revisionView.w, out.revisionView.h, static_cast<qsizetype>(out.revisionView.w) * 4, QImage::Format_ARGB32).copy();
  if (!out.changes.regions.empty() || m_CompareStatus->text().startsWith("Aligning")) {
    m_Compare->changes = std::move(out.changes.regions);
    m_Compare->haveChanges = true;
  }
  m_CompareStatus->setText(QString("Revision: %1, page %2. Alignment confidence %3%4")
      .arg(QFileInfo(m_Compare->source).fileName()).arg(m_Compare->revisionPage + 1)
      .arg(out.alignment.confidence * 100.0, 0, 'f', 1)
      .arg(m_Compare->lowConfidence ? ". Check alignment before trusting any change list." : ""));
  if (m_Compare->haveChanges) {
    m_CompareTable->setRowCount(static_cast<int>(m_Compare->changes.size()));
    for (int row = 0; row < static_cast<int>(m_Compare->changes.size()); ++row) {
      const auto& region = m_Compare->changes[static_cast<std::size_t>(row)];
      const QString values[] = {QString::fromLatin1(pdfdiff::KindName(region.kind)), QString::number(region.x0, 'f', 1),
          QString::number(region.y0, 'f', 1), QString("%1 × %2 pt").arg(region.Width(), 0, 'f', 1).arg(region.Height(), 0, 'f', 1)};
      for (int column = 0; column < 4; ++column) m_CompareTable->setItem(row, column, new QTableWidgetItem(values[column]));
    }
  }
  m_Page->update();
}

void PdfWindow::UpdateCompareOverlay() {
  if (!m_Compare || !m_Compare->revision) return;
  m_Compare->mode = m_CompareMode->currentIndex();
  m_Page->update();
}

void PdfWindow::DrawCompareOverlay(QPainter& painter) {
  if (!m_Compare || m_Compare->overlay.isNull()) return;
  if (m_Compare->mode == 2) return; // Base mode is the original page.
  painter.save();
  if (m_Compare->mode == 1) painter.setOpacity(std::clamp(m_Compare->opacity / 100.0, 0.0, 1.0));
  if (m_Compare->mode == 1) painter.drawImage(QPoint(0, 0), m_Compare->revisionOverlay);
  else if (m_Compare->mode == 2) painter.drawImage(QPoint(0, 0), m_Compare->baseView);
  else if (m_Compare->mode == 3) painter.drawImage(QPoint(0, 0), m_Compare->revisionView);
  else painter.drawImage(QPoint(0, 0), m_Compare->overlay);
  if (m_Compare->haveChanges && m_CompareShowHighlights->isChecked()) {
    const double factor = static_cast<double>(m_Page->image.width()) / std::max(1, m_Compare->overlay.width());
    for (const auto& region : m_Compare->changes) {
      const QColor color = region.kind == pdfdiff::Kind::Added ? QColor(45, 205, 105, 90) :
        region.kind == pdfdiff::Kind::Removed ? QColor(235, 65, 65, 90) : QColor(240, 175, 45, 100);
      painter.setPen(QPen(color.darker(140), 1.5)); painter.setBrush(color);
      const auto size = m_Document->Sizes()[m_Compare->basePage];
      painter.drawRect(QRectF(region.x0 * factor, (size.hPt - region.y1) * factor,
          region.Width() * factor, region.Height() * factor));
    }
  }
  painter.restore();
}

void PdfWindow::ComparePick(QPointF point, Qt::MouseButton button) {
  if (!m_Compare || !m_Compare->revision || button != Qt::LeftButton || m_Page->image.isNull()) return;
  if (m_CompareMode->currentIndex() != 2 && m_CompareMode->currentIndex() != 3) {
    m_CompareStatus->setText("Choose Base or Revision in the view selector before picking matching points."); return;
  }
  const auto size = m_Document->Sizes()[m_Compare->basePage];
  const double factor = static_cast<double>(m_Page->image.width()) / std::max(1.0, static_cast<double>(size.wPt));
  pdfalign::Pt picked{point.x() / factor, size.hPt - point.y() / factor};
  const bool isBase = m_CompareMode->currentIndex() == 2;
  const bool expectedBase = (m_ComparePickCount % 2) == 0;
  if (isBase != expectedBase) {
    m_CompareStatus->setText(expectedBase ? "Switch to Base and pick the next base point." : "Switch to Revision and pick its matching point."); return;
  }
  if (!isBase) picked = m_Compare->transform.Inverse().Apply(picked);
  m_ComparePicks[m_ComparePickCount++] = picked;
  if (m_ComparePickCount < m_ComparePickTarget) {
    m_CompareStatus->setText(isBase ? "Now switch to Revision and pick its matching point." : "Now switch to Base for the second matching pair."); return;
  }
  std::string why;
  pdfalign::Transform transform;
  bool ok = false;
  if (m_ComparePickTarget == 2) transform = pdfalign::FromOnePoint(m_ComparePicks[1], m_ComparePicks[0]), ok = true;
  else ok = pdfalign::FromTwoPoints(m_ComparePicks[1], m_ComparePicks[3], m_ComparePicks[0], m_ComparePicks[2], transform, why);
  m_ComparePicking = false;
  if (!ok) { m_CompareStatus->setText(QString("Could not align the picked points: %1").arg(QString::fromUtf8(why.c_str()))); return; }
  m_Compare->transform = transform; m_Compare->aligned = true; m_Compare->lowConfidence = false;
  m_Compare->manualAlignment = true;
  StartCompare(false);
}

void PdfWindow::SaveCompareChanges() {
  if (!m_Compare || !m_Compare->revision || !m_Compare->haveChanges || m_Compare->changes.empty()) {
    m_CompareStatus->setText("Find changes before saving markups."); return;
  }
  if (m_Compare->lowConfidence) {
    if (QMessageBox::warning(m_CompareDialog, "Low-confidence alignment", "This alignment may be wrong. Save these findings anyway?",
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
  }
  const QString path = QFileDialog::getSaveFileName(m_CompareDialog, "Save findings as PDF", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  const auto dest = path.endsWith(".pdf", Qt::CaseInsensitive) ? path : path + ".pdf";
  const auto marks = pdfdiff::RegionsToMarkups(m_Compare->changes, m_Compare->revisionPage, m_Compare->transform.Inverse());
  const auto error = pdfview::SaveAnnotated(m_Compare->revision->Path(), marks, std::filesystem::path(dest.toStdWString()));
  if (!error.empty()) { QMessageBox::warning(m_CompareDialog, "Save findings", QString::fromUtf8(error.c_str())); return; }
  m_CompareStatus->setText("Change markups saved to a new revision copy. The source PDF was not changed.");
}
