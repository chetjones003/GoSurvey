#include "PdfWindow.hpp"
#include "QtAccent.hpp"
#include "PdfCanvas.hpp"
#include "PdfRibbon.hpp"
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
#include <QTabWidget>
#include <QToolButton>
#include <QMenu>
#include <QGridLayout>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
#include <QPainter>
#include <QScreen>
#include <QPainterPath>
#include <QStyle>
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
#include <QKeyEvent>
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

namespace {
// Input tolerances are in the canvas's device-independent pixel coordinates.
constexpr double kGripPickDip = 7.0;
constexpr double kMarkPickDip = 5.0;
constexpr double kMinDragDip = 4.0;
constexpr int kPdfToolCallout = static_cast<int>(pdfview::Annot::Kind::Leader);
constexpr int kPdfToolRevisionCloud = static_cast<int>(pdfview::Annot::Kind::RevCloud);
}

// Navigation icons that say what they do: page arrows (with end bars for first/last), magnifiers, fit marks.
QIcon NavIcon(const QString& kind) {
  QPixmap pixmap(48, 48);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor ink("#e8eef9");
  p.setPen(Qt::NoPen);
  p.setBrush(ink);
  const auto triangle = [&](bool left, double x) {
    QPolygonF shape;
    if (left) shape << QPointF(x + 16, 9) << QPointF(x + 16, 39) << QPointF(x, 24);
    else shape << QPointF(x, 9) << QPointF(x, 39) << QPointF(x + 16, 24);
    p.drawPolygon(shape);
  };
  if (kind == "previous") triangle(true, 14);
  else if (kind == "next") triangle(false, 18);
  else if (kind == "first") { p.drawRect(8, 9, 5, 30); triangle(true, 16); }
  else if (kind == "last") { triangle(false, 16); p.drawRect(35, 9, 5, 30); }
  else if (kind == "zoomin" || kind == "zoomout") {
    p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(20, 20), 13, 13);
    p.drawLine(QPointF(30, 30), QPointF(42, 42));
    p.drawLine(QPointF(14, 20), QPointF(26, 20));
    if (kind == "zoomin") p.drawLine(QPointF(20, 14), QPointF(20, 26));
  } else if (kind == "fitwidth") {
    p.drawRect(5, 12, 4, 24); p.drawRect(39, 12, 4, 24);
    p.drawPolygon(QPolygonF{QPointF(12, 24), QPointF(22, 16), QPointF(22, 32)});
    p.drawPolygon(QPolygonF{QPointF(36, 24), QPointF(26, 16), QPointF(26, 32)});
  } else if (kind == "fitpage") {
    p.setPen(QPen(ink, 4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(12, 5, 24, 38));
    p.setPen(QPen(ink, 3));
    p.drawLine(QPointF(17, 15), QPointF(31, 15)); p.drawLine(QPointF(17, 24), QPointF(31, 24)); p.drawLine(QPointF(17, 33), QPointF(31, 33));
  }
  return QIcon(pixmap);
}

// REQ-387 clause 2: the thumbnail strip's model. Rows are pages; only the rows the view asks to paint are ever
// rendered, one at a time on a worker, nearest the top of the strip first. A draft (time-budgeted) render shows
// at once and a full one replaces it later. A page that cannot be drawn keeps its blank sheet.
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

// PDF viewer chrome follows the app-wide graphite appearance; the current thumbnail keeps an amber frame.
constexpr const char* kPdfWindowStyle = // scoped to the window's own chrome so the dialogs it opens keep the application theme
    "QMainWindow{background:#1c2024;} QToolBar{background:#242a2f;border:0;spacing:6px;padding:3px;}"
    "QToolBar QToolButton{color:#e8eef9;padding:4px 8px;border-radius:4px;} QToolBar QToolButton:hover{background:#343c41;} QToolBar QToolButton:checked{background:@tint@;}"
    "QDockWidget{color:#e8eef9;} QDockWidget::title{background:#292f34;padding:6px;}"
    "#pdfThumbnailList{background:#202529;color:#cfd8da;border:0;selection-background-color:@tint@;selection-color:white;}"
    "#pdfFindEdit{background:#15191c;color:#fff;border:1px solid #424b51;border-radius:4px;padding:3px 6px;}"
    "QToolBar QLabel,QToolBar QCheckBox{color:#e8eef9;} QMainWindow > QScrollArea{background:#171b1e;}"
    "#pdfCommandBar{background:#191d20;border-top:1px solid #30373c;border-bottom:1px solid #333b40;}"
    "#pdfRibbon::pane{border:1px solid #363e43;background:#202529;}"
    "#pdfRibbon QTabBar::tab{background:#1b2023;color:#dce2e5;border:1px solid #353d42;padding:6px 14px;margin-right:2px;}"
    "#pdfRibbon QTabBar::tab:selected{background:#2a3035;color:#f4f7f8;border-bottom:2px solid @strong@;}"
    "#pdfRibbon QTabBar::tab:hover:!selected{background:#30373c;}"
    "#pdfRibbon QToolButton{padding:2px 5px;border-radius:4px;color:#e8eef9;border:1px solid transparent;}"
    "#pdfRibbon QToolButton:hover{background:#343c41;border-color:@edge@;}"
    "#pdfRibbon QToolButton:pressed,#pdfRibbon QToolButton:checked{background:@tint@;border-color:@strong@;}"
    "#pdfRibbon QToolButton:disabled{color:#78858a;}"
    "#pdfRibbon QWidget[ribbonPanel=\"true\"]{border-right:1px solid #3a4247;}"
    "#ribbonPanelLabel{color:#b5c0c5;font-size:10px;border-top:1px solid #3a4247;padding-top:2px;}"
    "#pdfRibbon QLabel,#pdfRibbon QCheckBox{color:#e8eef9;}"
    "#pdfRibbon QSpinBox,#pdfRibbon QDoubleSpinBox,#pdfRibbon QComboBox{background:#15191c;border:1px solid #424b51;border-radius:4px;padding:2px 6px;color:#fff;}"
    "#pdfRibbon QPushButton{background:#2a3035;border:1px solid #41494e;border-radius:4px;padding:3px 10px;color:#e8eef9;}"
    "#pdfRibbon QPushButton:hover{background:#343c41;} #pdfRibbon QPushButton:disabled{color:#89979a;}"
    "#pdfSideStrip{background:#1b2023;border-right:1px solid #30373c;}"
    "#pdfSideStrip QToolButton{color:#cfd8da;border:1px solid transparent;border-radius:4px;padding:3px 2px;}"
    "#pdfSideStrip QToolButton:hover{background:#30373c;border-color:@edge@;}"
    "#pdfSideStrip QToolButton:checked{background:#2a3035;color:#fff;border-left:3px solid @strong@;}"
    "#pdfToolChest,#pdfProperties{background:#202529;}"
    "#pdfToolChest QToolButton{color:#e8eef9;border:1px solid transparent;border-radius:4px;padding:2px 5px;}"
    "#pdfToolChest QToolButton:hover{background:#343c41;border-color:@edge@;} #pdfToolChest QToolButton:checked{background:@tint@;border-color:@strong@;}"
    "#pdfToolChest QLabel,#pdfProperties QLabel{color:#e8eef9;}"
    "#pdfPanelHeading{color:#b5c0c5;font-size:11px;font-weight:600;border-bottom:1px solid #3a4247;padding:4px 0 2px 0;}"
    "QStatusBar QToolButton{color:#e8eef9;padding:2px;border-radius:4px;border:1px solid transparent;} QStatusBar QToolButton:hover{background:#343c41;border-color:@edge@;}"
    "QStatusBar QLabel{color:#e8eef9;font-size:14px;}"
    "QStatusBar QSpinBox{background:#15191c;border:1px solid #424b51;border-radius:4px;padding:2px 6px;color:#fff;font-size:14px;}"
    "#pdfScaleLabel{background:#30373c;border:1px solid #41494e;border-radius:10px;padding:2px 12px;color:#d8e3e4;font-weight:600;}"
    "#pdfSnapStatus{color:#f5b73a;}";

constexpr const char* kPdfExtractReviewStyle =
    "QDialog{background:#22272b;color:#eef2ff;} QLabel{color:#eef2ff;font-size:13px;}"
    "QTableWidget{background:#202529;color:#eef2ff;gridline-color:#41494e;}"
    "QPushButton{background:#2a3035;color:#eef2ff;border:1px solid #41494e;border-radius:4px;padding:6px 12px;}";

// PDF workflow dialogs follow the Qt Dark graphite appearance.
constexpr const char* kPdfWorkflowDialogStyle =
    "QDialog{background:#1c2024;color:#e8eef9;} QLabel{color:#e8eef9;} QLabel:disabled{color:#89979a;}"
    "QCheckBox{color:#e8eef9;} QCheckBox::indicator{width:15px;height:15px;border:2px solid #576469;border-radius:3px;background:#15191c;}"
    "QCheckBox::indicator:checked{background:@fill@;border-color:@fill@;}"
    "QPushButton{background:#2a3035;border:1px solid #41494e;border-radius:5px;padding:6px 12px;color:#e8eef9;}"
    "QPushButton:hover{background:#343c41;} QPushButton:disabled{color:#89979a;background:#242a2f;}"
    "QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#15191c;border:1px solid #424b51;border-radius:4px;padding:3px 6px;color:#fff;}"
    "QComboBox QAbstractItemView{background:#15191c;color:#fff;selection-background-color:@fill@;}"
    "QTableWidget{background:#202529;alternate-background-color:#242a2f;border:1px solid #41494e;color:#cfd8da;selection-background-color:@tint@;selection-color:white;}"
    "QHeaderView::section{background:#292f34;color:#e8eef9;padding:5px 8px;border:0;border-right:1px solid #41494e;font-weight:600;}"
    "QProgressBar{background:#15191c;border:1px solid #424b51;border-radius:4px;color:#e8eef9;text-align:center;}"
    "QProgressBar::chunk{background:@fill@;border-radius:3px;}";

PdfWindow::PdfWindow(QWidget* parent) : QMainWindow(parent, Qt::Window) {
  setObjectName("pdfViewerWindow");
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("GoSurvey | PDF viewer");
  setStyleSheet(QtApplyAccent(QString::fromLatin1(kPdfWindowStyle), true));
  { // the revision-cloud split button: a thin divider and a drawn chevron instead of the platform arrow
    const QString chevron = QtAsset("icons/chevron_down.png").replace(QChar(92), QChar('/'));
    setStyleSheet(styleSheet() + QString(
        "#pdfCloudButton{padding-right:0;}"
        "#pdfCloudButton::menu-button{border:none;border-left:1px solid #41494e;width:18px;border-top-right-radius:4px;border-bottom-right-radius:4px;}"
        "#pdfCloudButton::menu-button:hover{background:#343c41;}"
        "#pdfCloudButton::menu-arrow{image:url(%1);width:14px;height:14px;}").arg(chevron));
  }
  resize(1000, 800);
  // REQ-455: the commands live in a menu bar, one row of icon tools and a side panel (Bluebeam-style). First every
  // control is made here; they are placed once the side panel exists (it contributes a toggle action).
  const auto makeAction = [this](const char* name, const QString& text, const char* icon, const QString& tip, const auto& run) {
    auto* made = new QAction(CadCommandIcon(icon), text, this);
    if (name && *name) made->setObjectName(name);
    made->setToolTip(tip.isEmpty() ? text : tip);
    connect(made, &QAction::triggered, this, run);
    return made;
  };
  auto* openAction = makeAction("pdfOpenAction", "Open...", "Open.png", "Open a PDF", [this] {
    const auto path = QFileDialog::getOpenFileName(this, "Open PDF", {}, "PDF (*.pdf)");
    if (!path.isEmpty()) Open(path);
  });
  auto* saveAsAction = makeAction("pdfSaveAsAction", "Save As...", "Save_as.png", "Save a copy with the annotations", [this] { SaveAs(); });
  auto* extractPagesAction = makeAction("pdfExtractPagesAction", "Extract pages...", "Export.png", "Save chosen pages as a new PDF", [this] { SplitPages(); });
  auto* findAction = makeAction("pdfFindAction", "Find...", "Find.png", "Find text in the document (Ctrl+F)", [this] { ShowFindBar(); });
  findAction->setShortcut(QKeySequence::Find);
  addAction(findAction); // a shortcut only fires while its action sits on a visible widget: keep Find live from every tab
  m_PageNumber = new QSpinBox(this);
  m_PageNumber->setRange(1, 1);
  m_PageNumber->setAccessibleName("Page");
  connect(m_PageNumber, &QSpinBox::valueChanged, this, [this] {
    if (!m_SyncingPage) m_Draft.clear(); // scrolling past a page keeps the picks in progress on theirs
    m_SnapHit = false; m_SnapPrevValid = false;
    SyncThumbnail(); UpdateScaleDisplay(); if (m_SnapOn) { StartSnapRead(m_PageNumber->value() - 1); RefreshSnap(); }
    if (m_Layout == Layout::Continuous) { if (!m_SyncingPage) ScrollToPage(m_PageNumber->value() - 1); }
    else RequestRender();
    UpdateZoomUi(); SyncPageNav(); UpdatePropertiesPanel();
    if (m_Compare && m_Compare->basePage != m_PageNumber->value() - 1)
      m_CompareBasePage->setValue(m_PageNumber->value());
  });
  auto* previousAction = makeAction("pdfPreviousPage", "Previous", "Back.png", "Previous page (Page Up)", [this] { m_PageNumber->setValue(m_PageNumber->value() - 1); });
  auto* nextAction = makeAction("pdfNextPage", "Next", "Front.png", "Next page (Page Down)", [this] { m_PageNumber->setValue(m_PageNumber->value() + 1); });
  previousAction->setIcon(NavIcon("previous"));
  nextAction->setIcon(NavIcon("next"));
  m_LayoutBox = new QComboBox(this);
  m_LayoutBox->setObjectName("pdfLayoutBox");
  m_LayoutBox->setAccessibleName("Layout");
  m_LayoutBox->addItem("Page by page", static_cast<int>(Layout::PageByPage));
  m_LayoutBox->addItem("Continuous", static_cast<int>(Layout::Continuous));
  m_LayoutBox->setToolTip("Show one page at a time, or scroll through every page");
  connect(m_LayoutBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
    SetLayout(static_cast<Layout>(m_LayoutBox->itemData(index).toInt()));
  });
  auto* zoomOutAction = makeAction("pdfZoomOut", "Zoom out", "Zoom_Window.png", "Zoom out", [this] { ZoomBy(1 / 1.25, m_Scroll->viewport()->rect().center()); });
  m_ZoomBox = new QSpinBox(this);
  m_ZoomBox->setObjectName("pdfZoomPercent");
  m_ZoomBox->setAccessibleName("Zoom");
  m_ZoomBox->setRange(5, 1000);
  m_ZoomBox->setValue(100);
  m_ZoomBox->setSuffix("%");
  m_ZoomBox->setKeyboardTracking(false);
  m_ZoomBox->setToolTip("Zoom: type a percentage of the page's actual size");
  connect(m_ZoomBox, &QSpinBox::valueChanged, this, [this](int percent) {
    if (std::abs(percent - ZoomPercent()) >= 1.0) SetZoomPercent(percent);
  });
  auto* zoomInAction = makeAction("pdfZoomIn", "Zoom in", "Zoom_Window.png", "Zoom in (Ctrl+wheel zooms around the pointer)", [this] { ZoomBy(1.25, m_Scroll->viewport()->rect().center()); });
  auto* fitWidth = makeAction("pdfFitWidth", "Fit width", "Zoom_Extensis.png", "Make the page as wide as the window", [this] { FitWidth(); });
  auto* fitPage = makeAction("pdfFitPage", "Fit page", "Zoom_Extensis.png", "Show the whole page", [this] { FitPage(); });
  zoomOutAction->setIcon(NavIcon("zoomout"));
  zoomInAction->setIcon(NavIcon("zoomin"));
  fitWidth->setIcon(NavIcon("fitwidth"));
  fitPage->setIcon(NavIcon("fitpage"));
  m_ToolGroup = new QActionGroup(this);
  struct ToolKind { const char* label; int id; const char* icon; const char* key; };
  const ToolKind kinds[] = {{"Select", kToolSelect, "Select.png", "S"}, {"Browse", kToolBrowse, "Pan.png", "B"},
      {"Text", static_cast<int>(pdfview::Annot::Kind::Text), "text.png", "T"},
      {"Line", static_cast<int>(pdfview::Annot::Kind::Line), "line.png", "L"},
      {"Rectangle", static_cast<int>(pdfview::Annot::Kind::Rect), "rect.png", "R"},
      {"Ellipse", static_cast<int>(pdfview::Annot::Kind::Ellipse), "ellipse.png", "E"},
      {"Callout", kPdfToolCallout, "Multileader.png", "C"},
      {"Revision cloud", kPdfToolRevisionCloud, "c3d_revcloud.png", "K"},
      {"Length", static_cast<int>(pdfview::Annot::Kind::Length), "Measure_Distance.png", "D"},
      {"Polyline length", static_cast<int>(pdfview::Annot::Kind::PolyLength), "polyline.png", "P"},
      {"Area", static_cast<int>(pdfview::Annot::Kind::Area), "Measure_Area.png", "A"},
      {"Angle", static_cast<int>(pdfview::Annot::Kind::Angle), "Measure_Angle.png", "G"},
      {"Check", kToolCheck, "Check.png", "H"}};
  std::map<int, QAction*> toolActions;
  for (const auto& kind : kinds) {
    auto* tool = new QAction(CadCommandIcon(kind.icon), kind.label, this);
    tool->setCheckable(true); m_ToolGroup->addAction(tool);
    tool->setData(kind.id);
    tool->setShortcut(QKeySequence(QString::fromLatin1(kind.key))); // single-letter tool hotkeys; Esc returns to Browse
    addAction(tool); // the shortcut works from any ribbon tab, not only while the button is on screen
    tool->setToolTip((kind.id == kToolSelect ? QString("Select an annotation; drag it or a grip to edit") : kind.id == kToolBrowse ? QString("Browse pages: drag to pan") :
        kind.id == kPdfToolCallout ? QString("Callout: click what to point at, click where the note goes, then type the note") :
        kind.id == kPdfToolRevisionCloud ? QString("Revision cloud: draw a rectangle with two clicks") : QString(kind.label)) +
        QString(" (%1)").arg(kind.key));
    if (kind.id == kToolSelect) tool->setChecked(true);
    if (kind.id == kToolCheck) {
      tool->setObjectName("pdfCheckTool");
      tool->setToolTip("Check the page scale against a dimension the drawing states (H)");
    }
    toolActions[kind.id] = tool;
    connect(tool, &QAction::triggered, this, [this, kind] {
      m_CheckTarget = 0;
      if (kind.id == kToolCheck) SetTool(kToolCheck, "Check: click the two ends of a dimension the drawing states.");
      else SetTool(kind.id, kind.id == kToolSelect ? "Select an annotation; drag it or a grip to edit." :
          kind.id == kToolBrowse ? "Browse pages" : kind.id == kPdfToolCallout ? "Click the thing to point at, then click where the note goes." :
          kind.id == kPdfToolRevisionCloud ? (m_CloudPolygon ? "Click the corners of the cloud. Enter finishes; Esc cancels." : "Click two opposite corners of the cloud.") :
          "Click points on the page. Right-click finishes a polyline/area/cloud or cancels a draft.");
    });
  }
  // The revision-cloud button has two commands: a rectangle (two clicks) and a polygon (Enter finishes). The button
  // shows, and runs, whichever was used last.
  QAction* cloudTool = toolActions[kPdfToolRevisionCloud];
  auto* cloudMenu = new QMenu(this);
  const auto setCloudMode = [this, cloudTool](bool polygon) {
    m_CloudPolygon = polygon;
    cloudTool->setText(polygon ? "Polygon cloud" : "Rect cloud");
    cloudTool->setToolTip(polygon ? "Polygon revision cloud: click three or more corners, then press Enter (K)" : "Rectangle revision cloud: click two opposite corners (K)");
  };
  setCloudMode(false);
  const QIcon cloudIcon = cloudTool->icon();
  connect(cloudMenu->addAction(cloudIcon, "Rectangle cloud  (two clicks)"), &QAction::triggered, this, [setCloudMode, cloudTool] { setCloudMode(false); cloudTool->trigger(); });
  connect(cloudMenu->addAction(cloudIcon, "Polygon cloud  (Enter finishes)"), &QAction::triggered, this, [setCloudMode, cloudTool] { setCloudMode(true); cloudTool->trigger(); });
  auto* calibrateTool = new QAction("Calibrate", m_ToolGroup); // started from Set scale...; not a ribbon button
  calibrateTool->setCheckable(true);
  calibrateTool->setData(kToolCalibrate);
  auto* color = makeAction("pdfAnnotationColor", "Color...", "Color_Faces.png", "Annotation colour", [this] {
    const QColor chosen = QColorDialog::getColor(QColor::fromRgb(m_AnnotationColor), this, "Annotation color");
    if (chosen.isValid()) m_AnnotationColor = chosen.rgb() & 0xFFFFFFu;
  });
  auto* width = new QDoubleSpinBox(this); width->setObjectName("pdfAnnotationWidth"); width->setAccessibleName("Width");
  width->setRange(0.1, 20.0); width->setSingleStep(0.25); width->setValue(m_AnnotationThickness); width->setSuffix(" pt"); width->setPrefix("Width ");
  connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) { m_AnnotationThickness = static_cast<float>(value); });
  auto* fontSize = new QDoubleSpinBox(this); fontSize->setObjectName("pdfAnnotationFontSize"); fontSize->setAccessibleName("Text size");
  fontSize->setRange(4.0, 144.0); fontSize->setValue(m_AnnotationFontSize); fontSize->setSuffix(" pt"); fontSize->setPrefix("Size ");
  connect(fontSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) { m_AnnotationFontSize = static_cast<float>(value); });
  auto* font = new QComboBox(this); font->setObjectName("pdfAnnotationFont"); font->setAccessibleName("Font");
  for (const char* family : {"Helvetica", "Times", "Courier"}) font->addItem(family);
  connect(font, &QComboBox::currentTextChanged, this, [this](const QString& value) { m_AnnotationFont = value.toStdString(); });
  auto* bold = new QCheckBox("Bold", this); bold->setObjectName("pdfAnnotationBold");
  auto* italic = new QCheckBox("Italic", this); italic->setObjectName("pdfAnnotationItalic");
  connect(bold, &QCheckBox::toggled, this, [this](bool value) { m_AnnotationBold = value; });
  connect(italic, &QCheckBox::toggled, this, [this](bool value) { m_AnnotationItalic = value; });
  auto* fill = new QCheckBox("Fill closed shapes", this); fill->setObjectName("pdfAnnotationFill");
  connect(fill, &QCheckBox::toggled, this, [this](bool value) { m_AnnotationFill = value; });
  auto* applyStyle = makeAction("pdfAnnotationApplyStyle", "Apply style to selected", "Color_Edges.png", "Give the selected annotation the style above", [this] {
    if (m_SelectedAnnotation < 0 || m_SelectedAnnotation >= static_cast<int>(m_Annotations.Items().size())) return;
    std::vector<int> chosen = m_SelectedMore; chosen.push_back(m_SelectedAnnotation);
    bool changed = false;
    for (const int index : chosen) {
      auto annotation = m_Annotations.Items()[static_cast<std::size_t>(index)];
      ApplyAnnotationStyle(annotation);
      changed = m_Annotations.Replace(index, annotation) || changed;
    }
    if (changed) { const int keep = m_SelectedAnnotation; const auto more = m_SelectedMore; SceneChanged(); m_SelectedAnnotation = keep; m_SelectedMore = more; }
  });
  auto* undoAction = makeAction("pdfUndo", "Undo", "undo.png", "Undo the last annotation edit (Ctrl+Z)", [this] { m_Annotations.Undo(); ClearAnnotationSelection(); SceneChanged(); });
  auto* redoAction = makeAction("pdfRedo", "Redo", "redo.png", "Redo (Ctrl+Y)", [this] { m_Annotations.Redo(); ClearAnnotationSelection(); SceneChanged(); });
  BuildScaleControls();
  auto* extract = makeAction("pdfExtractCoordinates", "Extract coordinates", "Extract_Data.png", "Drag a rectangle around coordinate text", [this] {
    if (!m_Document) { statusBar()->showMessage("Open a PDF page before extracting coordinates."); return; }
    SetTool(kToolBrowse, QString()); // leave any scale pick in progress
    m_ExtractMode = true; m_ExtractDragging = false; m_ExtractBox = false; m_ExtractResult.reset(); UpdateToolStatus(); m_Page->update();
    statusBar()->showMessage("Drag a rectangle around selectable coordinate text. Esc cancels.");
    m_Page->setCursor(Qt::CrossCursor);
  });
  auto* audit = makeAction("pdfAuditScale", "Audit scale", "Audit.png", "Compare this page's dimension text with its scale", [this] { ShowScaleAudit(); });
  auto* compare = makeAction("pdfCompareRevisions", "Compare revisions…", "DWG_Compare.png", "Compare this sheet with a revision", [this] { BeginCompare(); });
  addAction(m_SnapAction); // F3 stays live from every tab
  m_Scroll = new QScrollArea(this);
  m_Scroll->setAlignment(Qt::AlignCenter);
  m_Page = new PdfCanvas(m_Scroll);
  m_Page->setMouseTracking(true);
  // The page-local point (and the page it lies on) comes from DispatchInput: with one page shown it is the canvas point.
  m_Page->clicked = [this](QPointF point, Qt::MouseButton button) {
    DispatchInput(point, true, [this, button](QPointF local) { PageClick(local, button); });
  };
  m_Page->doubleClicked = [this] { EditSelectedAnnotation(); };
  m_Page->moved = [this](QPointF point) {
    DispatchInput(point, QApplication::mouseButtons().testFlag(Qt::LeftButton), [this](QPointF local) { MoveExtract(local); MoveAnnotation(local); UpdateSnap(local); });
  };
  m_Page->left = [this] { if (m_SnapHit) { m_SnapHit = false; m_SnapPrevValid = false; m_Page->update(); } };
  m_Page->overlay = [this](QPainter& painter) { DrawCompareOverlay(painter); DrawHighlights(painter); DrawAnnotations(painter); DrawScaleOverlay(painter); DrawAuditOverlay(painter); DrawExtractOverlay(painter); };
  m_Page->paintStrip = [this](QPainter& painter, const QRect& exposed) { return PaintStrip(painter, exposed); };
  m_Page->pressed = [this](QPointF point, Qt::MouseButton button) {
    m_InputPage = m_Layout == Layout::Continuous && m_Document ? PageAtY(qRound(point.y())) : -1;
    DispatchInput(point, true, [this, button](QPointF local) { BeginExtract(local, button); });
  };
  m_Page->released = [this](QPointF point, Qt::MouseButton button) {
    DispatchInput(point, true, [this, button](QPointF local) { EndExtract(local, button); EndAnnotation(local, button); });
    m_InputPage = -1;
  };
  m_Scroll->setWidget(m_Page);
  setCentralWidget(m_Scroll);
  // Middle-button pan and Ctrl+wheel zoom work over the page and over the margin around it.
  m_Page->installEventFilter(this);
  m_Scroll->viewport()->installEventFilter(this);
  m_StripPoll = new QTimer(this);
  m_StripPoll->setInterval(20);
  connect(m_StripPoll, &QTimer::timeout, this, [this] { PollStrip(); });
  m_ZoomSettle = new QTimer(this);
  m_ZoomSettle->setSingleShot(true);
  m_ZoomSettle->setInterval(120);
  connect(m_ZoomSettle, &QTimer::timeout, this, [this] { RequestRender(true); });
  const auto scrolledPage = [this] { // a page too big to draw whole: redraw when the view leaves the drawn window
    if (m_Layout == Layout::PageByPage && m_Document && !m_Page->imageFull.isEmpty() && !PageWindowCovers()) m_ZoomSettle->start();
  };
  connect(m_Scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, scrolledPage);
  connect(m_Scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, scrolledPage);
  m_ResizeTimer = new QTimer(this);
  m_ResizeTimer->setSingleShot(true);
  m_ResizeTimer->setInterval(150);
  connect(m_ResizeTimer, &QTimer::timeout, this, [this] {
    if (m_Layout != Layout::Continuous || !m_Document) return;
    const QPoint top(m_Scroll->viewport()->width() / 2, 0);
    const ZoomAnchor anchor = CaptureAnchor(top);
    RebuildLayout();
    RestoreAnchor(anchor, top);
    ScheduleStrip();
  });
  connect(m_Scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { OnScrolled(); });
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
  // REQ-455: the side panel. A column of labelled buttons (Thumbnails, Tool Chest, Properties) chooses what the
  // panel beside it shows; the buttons say what they are, not just an icon.
  const auto heading = [](const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent); label->setObjectName("pdfPanelHeading"); return label;
  };
  const auto chestButton = [](QAction* tool, QWidget* parent) {
    auto* button = new QToolButton(parent); button->setObjectName("pdfChest_" + tool->text().remove(' ').remove('.'));
    button->setDefaultAction(tool); button->setAutoRaise(true); button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(24, 24)); button->setFixedSize(70, 58); return button;
  };
  auto* chestPage = new QWidget(this);
  chestPage->setObjectName("pdfToolChest");
  auto* chest = new QVBoxLayout(chestPage); chest->setContentsMargins(8, 8, 8, 8); chest->setSpacing(4);
  const auto chestSection = [&](const QString& title, const QList<QAction*>& tools) {
    chest->addWidget(heading(title, chestPage));
    auto* grid = new QGridLayout; grid->setSpacing(2); int index = 0;
    for (auto* tool : tools) { grid->addWidget(chestButton(tool, chestPage), index / 3, index % 3); ++index; }
    grid->setColumnStretch(3, 1); chest->addLayout(grid);
  };
  chestSection("Markup", {toolActions[kToolSelect], toolActions[kToolBrowse],
      toolActions[static_cast<int>(pdfview::Annot::Kind::Text)], toolActions[static_cast<int>(pdfview::Annot::Kind::Line)],
      toolActions[static_cast<int>(pdfview::Annot::Kind::Rect)], toolActions[static_cast<int>(pdfview::Annot::Kind::Ellipse)],
      toolActions[kPdfToolCallout], toolActions[kPdfToolRevisionCloud]});
  chestSection("Measurement", {toolActions[5], toolActions[6], toolActions[7], toolActions[8], toolActions[kToolCheck]});
  chestSection("Review", {extract, audit, compare});
  chest->addStretch(1);
  // Properties: what the open document and the current page are (read-only; UpdatePropertiesPanel keeps it current).
  auto* propertiesPage = new QWidget(this);
  propertiesPage->setObjectName("pdfProperties");
  auto* properties = new QVBoxLayout(propertiesPage); properties->setContentsMargins(8, 8, 8, 8); properties->setSpacing(4);
  properties->addWidget(heading("Document", propertiesPage));
  auto* documentForm = new QFormLayout; documentForm->setLabelAlignment(Qt::AlignLeft);
  for (const char* row : {"File", "Pages"}) {
    auto* value = new QLabel("-", propertiesPage); value->setObjectName(QString("pdfProp") + row); value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse); documentForm->addRow(QString(row) + ":", value);
  }
  properties->addLayout(documentForm);
  properties->addWidget(heading("Current page", propertiesPage));
  auto* pageForm = new QFormLayout; pageForm->setLabelAlignment(Qt::AlignLeft);
  for (const char* row : {"Number", "Size", "Scale"}) {
    auto* value = new QLabel("-", propertiesPage); value->setObjectName(QString("pdfProp") + row); value->setWordWrap(true);
    pageForm->addRow(QString(row) + ":", value);
  }
  properties->addLayout(pageForm);
  properties->addStretch(1);
  m_SidePages = new QStackedWidget(this);
  m_SidePages->setObjectName("pdfSidePages");
  auto* sideStrip = new QWidget(this);
  sideStrip->setObjectName("pdfSideStrip");
  auto* stripLayout = new QVBoxLayout(sideStrip); stripLayout->setContentsMargins(2, 4, 2, 4); stripLayout->setSpacing(2);
  auto* sideGroup = new QButtonGroup(this);
  sideGroup->setObjectName("pdfSideGroup");
  sideGroup->setExclusive(true);
  const std::tuple<QWidget*, const char*, const char*> sideTabs[] = {{m_ThumbView, "Sheet.png", "Thumbnails"},
      {chestPage, "Blocks_Palette.png", "Tool Chest"}, {propertiesPage, "Layer_Properties.png", "Properties"}};
  for (const auto& [page, icon, name] : sideTabs) {
    const int index = m_SidePages->addWidget(page);
    auto* button = new QToolButton(sideStrip);
    button->setObjectName(QString("pdfSideButton_") + QString(name).remove(' '));
    button->setText(name); button->setToolTip(QString("Show the ") + QString(name).toLower());
    button->setIcon(CadCommandIcon(icon)); button->setIconSize(QSize(24, 24));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon); button->setCheckable(true); button->setAutoRaise(true);
    button->setFixedSize(76, 62); button->setChecked(index == 0);
    sideGroup->addButton(button, index); stripLayout->addWidget(button);
  }
  stripLayout->addStretch(1);
  connect(sideGroup, &QButtonGroup::idClicked, m_SidePages, &QStackedWidget::setCurrentIndex);
  auto* sidePanel = new QWidget(this);
  sidePanel->setObjectName("pdfSidePanel");
  auto* sideLayout = new QHBoxLayout(sidePanel); sideLayout->setContentsMargins(0, 0, 0, 0); sideLayout->setSpacing(0);
  sideLayout->addWidget(sideStrip); sideLayout->addWidget(m_SidePages, 1);
  m_ThumbDock = new QDockWidget("Panels", this);
  m_ThumbDock->setObjectName("pdfThumbnails");
  m_ThumbDock->setWidget(sidePanel);
  addDockWidget(Qt::LeftDockWidgetArea, m_ThumbDock);
  resizeDocks({m_ThumbDock}, {PdfThumbnails::kBox + 100}, Qt::Horizontal);
  auto* thumbnailsAction = m_ThumbDock->toggleViewAction();
  thumbnailsAction->setObjectName("pdfThumbnailsAction");
  thumbnailsAction->setIcon(CadCommandIcon("Sheet.png"));
  thumbnailsAction->setText("Side panel");
  thumbnailsAction->setToolTip("Show or hide the side panel (thumbnails, tool chest, properties)");

  // REQ-455: the ribbon. Tabs of panels, as in the main window; a panel that does not fit collapses into a menu.
  m_CommandTabs = new QTabWidget(this);
  m_CommandTabs->setObjectName("pdfRibbon");
  m_CommandTabs->setUsesScrollButtons(false);
  m_CommandTabs->setElideMode(Qt::ElideNone);
  m_CommandTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_CommandTabs->setMinimumWidth(0);
  m_CommandTabs->setFixedHeight(pdfribbon::kHeight);
  const auto addPage = [this](const QString& name, const std::function<void(pdfribbon::Page*)>& fillPage) {
    auto* page = new pdfribbon::Page(m_CommandTabs);
    page->setObjectName("pdfRibbonPage_" + QString(name).remove('/'));
    page->setProperty("ribbonTab", name);
    fillPage(page);
    m_CommandTabs->addTab(page, name);
  };
  const auto tabKey = [](const QString& name) { return QString(name).remove('/'); };
  addPage("Open/View", [&](pdfribbon::Page* page) {
    const QString tab = tabKey("Open/View");
    pdfribbon::PanelBuilder file(page, tab, "File");
    file.Large(openAction); file.Small(saveAsAction); file.Small(extractPagesAction); file.Finish(page, 10);
    pdfribbon::PanelBuilder view(page, tab, "View"); // page navigation and zoom live on the bottom bar
    view.Widget(m_LayoutBox); view.Finish(page, 10);
    pdfribbon::PanelBuilder search(page, tab, "Search");
    search.Large(findAction); search.Finish(page, 5);
    pdfribbon::PanelBuilder palettes(page, tab, "Palettes");
    palettes.Large(thumbnailsAction); palettes.Finish(page, 5);
  });
  addPage("Annotate", [&](pdfribbon::Page* page) {
    const QString tab = tabKey("Annotate");
    pdfribbon::PanelBuilder tools(page, tab, "Annotations");
    for (const int id : {kToolSelect, kToolBrowse, static_cast<int>(pdfview::Annot::Kind::Text),
                         static_cast<int>(pdfview::Annot::Kind::Line), static_cast<int>(pdfview::Annot::Kind::Rect),
                         static_cast<int>(pdfview::Annot::Kind::Ellipse), kPdfToolCallout, kPdfToolRevisionCloud}) {
      auto* button = tools.Small(toolActions[id]);
      if (id == kPdfToolRevisionCloud) {
        button->setObjectName("pdfCloudButton");
        button->setMenu(cloudMenu); button->setPopupMode(QToolButton::MenuButtonPopup);
        // Wide enough for the longer label, so neither mode is elided.
        button->setFixedWidth(button->fontMetrics().horizontalAdvance("Polygon cloud") + 18 + 34);
      }
    }
    tools.Finish(page, 10);
    pdfribbon::PanelBuilder look(page, tab, "Style");
    look.Small(color); look.Widget(width); look.Widget(fontSize);
    look.Widget(font); look.Widget(bold); look.Widget(italic);
    look.Widget(fill); look.Small(applyStyle); look.Finish(page, 8);
    pdfribbon::PanelBuilder edit(page, tab, "Edit");
    edit.Small(undoAction); edit.Small(redoAction); edit.Finish(page, 5);
  });
  addPage("Measure", [&](pdfribbon::Page* page) {
    const QString tab = tabKey("Measure");
    pdfribbon::PanelBuilder measure(page, tab, "Measure");
    for (const int id : {5, 6, 7, 8, int(kToolCheck)}) measure.Small(toolActions[id]);
    measure.Finish(page, 10);
    pdfribbon::PanelBuilder scale(page, tab, "Scale");
    scale.Small(m_SetScaleAction); scale.Widget(m_ScaleLabel); scale.Widget(m_ChecksButton); scale.Widget(m_ShowChecksBox); scale.Finish(page, 9);
    pdfribbon::PanelBuilder snap(page, tab, "Snap");
    snap.Small(m_SnapAction); snap.Widget(m_SnapStatus); snap.Finish(page, 6);
    pdfribbon::PanelBuilder edit(page, tab, "Edit");
    edit.Small(undoAction); edit.Small(redoAction); edit.Finish(page, 5);
  });
  addPage("Review", [&](pdfribbon::Page* page) {
    pdfribbon::PanelBuilder review(page, tabKey("Review"), "Review");
    review.Large(extract); review.Large(audit); review.Large(compare); review.Finish(page, 10);
  });
  auto* ribbonBar = addToolBar("PDF ribbon");
  ribbonBar->setObjectName("pdfCommandBar");
  ribbonBar->setMovable(false); ribbonBar->setFloatable(false); ribbonBar->setContentsMargins(0, 0, 0, 0);
  auto* ribbonWrapper = new QWidget(ribbonBar);
  ribbonWrapper->setObjectName("pdfRibbonWrapper");
  ribbonWrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* ribbonLayout = new QVBoxLayout(ribbonWrapper);
  ribbonLayout->setContentsMargins(0, 0, 0, 0);
  ribbonLayout->addWidget(m_CommandTabs);
  ribbonBar->addWidget(ribbonWrapper);

  // REQ-455: page navigation and zoom live on the bottom bar only.
  const auto statusButton = [this](QAction* action) {
    auto* button = new QToolButton(this); button->setObjectName("pdfStatus_" + action->objectName());
    button->setDefaultAction(action); button->setAutoRaise(true); button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(22, 22)); button->setFixedSize(34, 32); return button;
  };
  auto* firstAction = makeAction("pdfFirstPage", "First page", "Back.png", "First page (Home)", [this] { m_PageNumber->setValue(1); });
  auto* lastAction = makeAction("pdfLastPage", "Last page", "Front.png", "Last page (End)", [this] { m_PageNumber->setValue(m_PageNumber->maximum()); });
  firstAction->setIcon(NavIcon("first"));
  lastAction->setIcon(NavIcon("last"));
  m_PageNumber->setObjectName("pdfPageNumber");
  m_PageNumber->setKeyboardTracking(false);
  m_PageNumber->setToolTip("Go to a page");
  m_PageNumber->setMinimumSize(70, 30);
  m_StatusPageCount = new QLabel("of 1", this);
  m_StatusPageCount->setObjectName("pdfStatusPageCount");
  m_StatusPageCount->setMinimumWidth(48);
  m_ZoomBox->setMinimumSize(96, 30);
  for (QAction* action : {firstAction, previousAction}) statusBar()->addPermanentWidget(statusButton(action));
  m_ToolStatus = new QLabel(this);
  m_ToolStatus->setObjectName("pdfActiveTool");
  m_ToolStatus->setMinimumWidth(0);
  m_ToolStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  statusBar()->addPermanentWidget(m_ToolStatus, 1);
  UpdateToolStatus();
  auto* pageLabel = new QLabel("Page", this);
  pageLabel->setObjectName("pdfStatusPageLabel");
  statusBar()->addPermanentWidget(pageLabel);
  statusBar()->addPermanentWidget(m_PageNumber);
  statusBar()->addPermanentWidget(m_StatusPageCount);
  for (QAction* action : {nextAction, lastAction, zoomOutAction}) statusBar()->addPermanentWidget(statusButton(action));
  statusBar()->addPermanentWidget(m_ZoomBox);
  for (QAction* action : {zoomInAction, fitWidth, fitPage}) statusBar()->addPermanentWidget(statusButton(action));
  m_StatusMore = new QToolButton(this);
  m_StatusMore->setObjectName("pdfStatusMore");
  m_StatusMore->setToolTip("More page and zoom commands");
  m_StatusMore->setIcon(CadCommandIcon("layers.png"));
  m_StatusMore->setIconSize(QSize(22, 22));
  m_StatusMore->setFixedSize(34, 32);
  m_StatusMore->setAutoRaise(true);
  m_StatusMore->setPopupMode(QToolButton::InstantPopup);
  auto* statusMoreMenu = new QMenu(m_StatusMore);
  for (QAction* action : {firstAction, lastAction, fitPage}) statusMoreMenu->addAction(action);
  m_StatusMore->setMenu(statusMoreMenu);
  statusBar()->addPermanentWidget(m_StatusMore);
  m_StatusMore->hide();
  UpdateStatusBarLayout();
  UpdateToolStatus();
  for (const auto key : {Qt::Key_PageUp, Qt::Key_PageDown, Qt::Key_Home, Qt::Key_End}) { // REQ-387 clause 2: keyboard page navigation
    auto* navigate = new QShortcut(QKeySequence(key), this);
    connect(navigate, &QShortcut::activated, this, [this, key] {
      if (!m_Document) return;
      const int last = m_PageNumber->maximum();
      m_PageNumber->setValue(key == Qt::Key_PageUp ? m_PageNumber->value() - 1 : key == Qt::Key_PageDown ? m_PageNumber->value() + 1 :
                             key == Qt::Key_Home ? 1 : last);
    });
  }

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
    if (m_SelectedAnnotation < 0) return;
    std::vector<int> doomed = m_SelectedMore; doomed.push_back(m_SelectedAnnotation);
    std::sort(doomed.begin(), doomed.end(), std::greater<int>()); // highest first, so the indexes stay true
    bool removed = false;
    for (const int index : doomed) removed = m_Annotations.Remove(index) || removed;
    if (removed) { ClearAnnotationSelection(); SceneChanged(); }
  });
  auto* undoAnnotation = new QShortcut(QKeySequence::Undo, this);
  connect(undoAnnotation, &QShortcut::activated, this, [this] {
    if (m_Annotations.Undo()) { ClearAnnotationSelection(); SceneChanged(); }
  });
  auto* redoAnnotation = new QShortcut(QKeySequence::Redo, this);
  connect(redoAnnotation, &QShortcut::activated, this, [this] {
    if (m_Annotations.Redo()) { ClearAnnotationSelection(); SceneChanged(); }
  });
  m_SearchPoll = new QTimer(this);
  m_SearchPoll->setInterval(30);
  connect(m_SearchPoll, &QTimer::timeout, this, [this] { PollSearch(); });

  m_AuditDialog = new QDialog(this);
  m_AuditDialog->setObjectName("pdfScaleAuditDialog");
  m_AuditDialog->setWindowTitle("Audit scale");
  m_AuditDialog->setStyleSheet(QtApplyAccent(QString::fromLatin1(kPdfWorkflowDialogStyle), true));
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
    if (!m_AuditReady || row < 0 || row >= static_cast<int>(m_AuditRows.size()) || m_AuditPage != CurrentPage() || !PageReady()) return;
    m_AuditSelected = m_AuditRows[static_cast<std::size_t>(row)];
    m_Page->update();
    const auto& match = m_AuditMatches.matches[static_cast<std::size_t>(m_AuditRows[static_cast<std::size_t>(row)])];
    CenterOnPagePoint(m_AuditPage, match.tx, match.ty);
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

  // Compare revisions is a page of the side panel (beside Thumbnails, Tool Chest and Properties), not a window.
  m_ComparePanel = new QWidget(this);
  m_ComparePanel->setObjectName("pdfComparePanel");
  m_ComparePanel->setStyleSheet(QtApplyAccent(QString::fromLatin1(kPdfWorkflowDialogStyle).replace("QDialog{", "#pdfComparePanel{"), true));
  auto* compareLayout = new QVBoxLayout(m_ComparePanel);
  compareLayout->setContentsMargins(8, 8, 8, 8);
  compareLayout->addWidget(new QLabel("Compare revisions", m_ComparePanel));
  compareLayout->itemAt(0)->widget()->setObjectName("pdfPanelHeading");
  m_CompareStatus = new QLabel("Choose a revision PDF to begin. The open PDF remains the base and is never modified.", m_ComparePanel);
  m_CompareStatus->setObjectName("pdfCompareStatus"); m_CompareStatus->setWordWrap(true);
  compareLayout->addWidget(m_CompareStatus);
  m_CompareProgress = new QProgressBar(m_ComparePanel); m_CompareProgress->setObjectName("pdfCompareProgress");
  m_CompareProgress->setRange(0, 0); m_CompareProgress->hide(); compareLayout->addWidget(m_CompareProgress);
  auto* pageControls = new QHBoxLayout;
  m_CompareBasePage = new QSpinBox(m_ComparePanel); m_CompareBasePage->setObjectName("pdfCompareBasePage");
  m_CompareBasePage->setPrefix("Base page "); m_CompareBasePage->setRange(1, 1);
  m_CompareRevisionPage = new QSpinBox(m_ComparePanel); m_CompareRevisionPage->setObjectName("pdfCompareRevisionPage");
  m_CompareRevisionPage->setPrefix("Revision page "); m_CompareRevisionPage->setRange(1, 1);
  pageControls->addWidget(m_CompareBasePage); pageControls->addWidget(m_CompareRevisionPage); pageControls->addStretch();
  compareLayout->addLayout(pageControls);
  auto* viewControls = new QHBoxLayout;
  m_CompareMode = new QComboBox(m_ComparePanel);
  m_CompareMode->setObjectName("pdfCompareMode");
  m_CompareMode->addItem("Tint", static_cast<int>(CompareMode::Tint));
  m_CompareMode->addItem("Opacity", static_cast<int>(CompareMode::Opacity));
  m_CompareMode->addItem("Base", static_cast<int>(CompareMode::Base));
  m_CompareMode->addItem("Revision", static_cast<int>(CompareMode::Revision));
  m_CompareMode->setCurrentIndex(0);
  viewControls->addWidget(new QLabel("View", m_ComparePanel)); viewControls->addWidget(m_CompareMode);
  m_CompareOpacity = new QSlider(Qt::Horizontal, m_ComparePanel);
  m_CompareOpacity->setObjectName("pdfCompareOpacity"); m_CompareOpacity->setRange(0, 100); m_CompareOpacity->setValue(50);
  viewControls->addWidget(new QLabel("Revision opacity", m_ComparePanel)); viewControls->addWidget(m_CompareOpacity, 1);
  compareLayout->addLayout(viewControls);
  auto* diffControls = new QHBoxLayout;
  m_CompareTolerance = new QDoubleSpinBox(m_ComparePanel); m_CompareTolerance->setObjectName("pdfCompareTolerance");
  m_CompareTolerance->setRange(0.0, 25.0); m_CompareTolerance->setSingleStep(0.25); m_CompareTolerance->setSuffix(" mm tolerance");
  m_CompareTolerance->setValue(pdfdiff::Settings{}.toleranceMm);
  m_CompareMinRegion = new QDoubleSpinBox(m_ComparePanel); m_CompareMinRegion->setObjectName("pdfCompareMinRegion");
  m_CompareMinRegion->setRange(0.0, 100.0); m_CompareMinRegion->setSingleStep(0.5); m_CompareMinRegion->setSuffix(" mm minimum region");
  m_CompareMinRegion->setValue(pdfdiff::Settings{}.minSizeMm);
  diffControls->addWidget(m_CompareTolerance); diffControls->addWidget(m_CompareMinRegion);
  compareLayout->addLayout(diffControls);
  m_CompareShowHighlights = new QCheckBox("Show change boxes", m_ComparePanel);
  m_CompareShowHighlights->setObjectName("pdfCompareShowHighlights"); m_CompareShowHighlights->setChecked(true);
  compareLayout->addWidget(m_CompareShowHighlights);
  m_CompareTable = new QTableWidget(m_ComparePanel);
  m_CompareTable->setObjectName("pdfCompareChanges"); m_CompareTable->setColumnCount(4);
  m_CompareTable->setHorizontalHeaderLabels({"Kind", "X", "Y", "Size"});
  m_CompareTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_CompareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  compareLayout->addWidget(m_CompareTable, 1);
  auto* compareButtons = new QGridLayout; // two columns: the panel is narrow
  auto* chooseRevision = new QPushButton("Choose revision PDF…", m_ComparePanel);
  chooseRevision->setObjectName("pdfCompareChoose");
  auto* alignAuto = new QPushButton("Align automatically", m_ComparePanel);
  auto* pickOne = new QPushButton("Pick one matching point", m_ComparePanel);
  auto* pickTwo = new QPushButton("Pick two matching points", m_ComparePanel);
  auto* resetAlign = new QPushButton("Reset alignment", m_ComparePanel);
  auto* findChanges = new QPushButton("Find changes", m_ComparePanel);
  auto* previousChange = new QPushButton("Previous change", m_ComparePanel);
  auto* nextChange = new QPushButton("Next change", m_ComparePanel);
  auto* saveChanges = new QPushButton("Save changes as markups…", m_ComparePanel);
  auto* cancelCompare = new QPushButton("Cancel work", m_ComparePanel);
  auto* closeCompare = new QPushButton("Close compare", m_ComparePanel);
  closeCompare->setObjectName("pdfCompareClose");
  alignAuto->setObjectName("pdfCompareAlignAuto"); pickOne->setObjectName("pdfComparePickOne");
  pickTwo->setObjectName("pdfComparePickTwo"); findChanges->setObjectName("pdfCompareFindChanges");
  saveChanges->setObjectName("pdfCompareSaveChanges"); cancelCompare->setObjectName("pdfCompareCancel");
  int buttonIndex = 0;
  for (auto* b : {chooseRevision, findChanges, previousChange, nextChange, alignAuto, resetAlign, pickOne, pickTwo, saveChanges, cancelCompare, closeCompare}) {
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); // never widens the panel; the text may shorten to fit
    compareButtons->addWidget(b, buttonIndex / 2, buttonIndex % 2); ++buttonIndex;
  }
  compareLayout->addLayout(compareButtons);
  connect(chooseRevision, &QPushButton::clicked, this, [this] { BeginCompare(); });
  connect(m_CompareMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (m_Compare) { m_Compare->mode = static_cast<CompareMode>(m_CompareMode->currentData().toInt()); UpdateCompareOverlay(); }
  });
  connect(m_CompareOpacity, &QSlider::valueChanged, this, [this](int value) { if (m_Compare) { m_Compare->opacity = value; m_Page->update(); } });
  connect(m_CompareShowHighlights, &QCheckBox::toggled, this, [this] { m_Page->update(); });
  connect(alignAuto, &QPushButton::clicked, this, [this] { if (m_Compare) { m_Compare->aligned = false; m_Compare->manualAlignment = false; StartCompare(false); } });
  connect(pickOne, &QPushButton::clicked, this, [this] { if (m_Compare) { m_ComparePicking = true; m_ComparePickCount = 0; m_ComparePickTarget = ComparePickCount::OnePair; m_CompareMode->setCurrentIndex(m_CompareMode->findData(static_cast<int>(CompareMode::Base))); m_CompareStatus->setText("Pick a point on the Base view, switch to Revision, then pick its match. Esc cancels."); } });
  connect(pickTwo, &QPushButton::clicked, this, [this] { if (m_Compare) { m_ComparePicking = true; m_ComparePickCount = 0; m_ComparePickTarget = ComparePickCount::TwoPairs; m_CompareMode->setCurrentIndex(m_CompareMode->findData(static_cast<int>(CompareMode::Base))); m_CompareStatus->setText("Pick base point 1, its revision match, base point 2, then its revision match. Switch views as prompted."); } });
  connect(resetAlign, &QPushButton::clicked, this, [this] { if (m_Compare) { m_Compare->aligned = false; m_Compare->manualAlignment = false; StartCompare(false); } });
  connect(findChanges, &QPushButton::clicked, this, [this] {
    if (m_Compare && m_Compare->lowConfidence && QMessageBox::warning(m_ComparePanel, "Check alignment",
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
    if (!m_Compare || row < 0 || row >= static_cast<int>(m_Compare->changes.size()) || !PageReady()) return;
    const auto& region = m_Compare->changes[static_cast<std::size_t>(row)];
    CenterOnPagePoint(m_Compare->basePage, (region.x0 + region.x1) * 0.5, (region.y0 + region.y1) * 0.5);
  });
  connect(closeCompare, &QPushButton::clicked, this, [this] {
    m_ComparePicking = false;
    if (m_Compare && m_Compare->busy) { m_Compare->cancel = true; m_CompareClosePending = true; }
    else { StopCompareDetail(); m_Compare.reset(); m_Page->update(); }
    m_SidePages->setCurrentIndex(0); // back to the thumbnails
    if (auto* group = findChild<QButtonGroup*>("pdfSideGroup")) if (auto* thumbs = group->button(0)) thumbs->setChecked(true);
    resizeDocks({m_ThumbDock}, {PdfThumbnails::kBox + 100}, Qt::Horizontal);
  });
  m_CompareDetailTimer = new QTimer(this); m_CompareDetailTimer->setSingleShot(true); m_CompareDetailTimer->setInterval(150);
  connect(m_CompareDetailTimer, &QTimer::timeout, this, [this] { RequestCompareDetail(); });
  m_CompareDetailPoll = new QTimer(this); m_CompareDetailPoll->setInterval(30);
  connect(m_CompareDetailPoll, &QTimer::timeout, this, [this] { PollCompareDetail(); });
  connect(m_Scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (m_Compare) m_CompareDetailTimer->start(); });
  connect(m_Scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (m_Compare) m_CompareDetailTimer->start(); });
  m_ComparePoll = new QTimer(this); m_ComparePoll->setInterval(30);
  connect(m_ComparePoll, &QTimer::timeout, this, [this] { PollCompare(); });
  { // the side panel's fourth tab
    const int index = m_SidePages->addWidget(m_ComparePanel);
    auto* group = findChild<QButtonGroup*>("pdfSideGroup");
    auto* strip = findChild<QWidget*>("pdfSideStrip");
    auto* button = new QToolButton(strip);
    button->setObjectName("pdfSideButton_Compare");
    button->setText("Compare"); button->setToolTip("Show the revision comparison");
    button->setIcon(CadCommandIcon("DWG_Compare.png"));
    button->setIconSize(QSize(24, 24)); button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setCheckable(true); button->setAutoRaise(true); button->setFixedSize(76, 62);
    auto* layout = static_cast<QVBoxLayout*>(strip->layout());
    layout->insertWidget(layout->count() - 1, button); // before the stretch
    group->addButton(button, index);
  }
}

PdfWindow::~PdfWindow() {
  StopCompareDetail();
  if (m_Compare) m_Compare->cancel = true;
  if (m_CompareWork.valid()) m_CompareWork.wait();
  m_AuditCancel = true;
  if (m_AuditWork.valid()) m_AuditWork.wait();
  for (auto& page : m_SnapPages) *page.second.cancel = true;
  m_SnapPages.clear(); // each reader holds the document: the futures wait here
  CancelSearch(true);
  CancelStrip(true);
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
  m_ComparePoll->stop(); StopCompareDetail(); m_Compare.reset(); m_ComparePicking = false; m_CompareClosePending = false;
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
  CancelStrip(true);
  m_StripCache.clear(); m_StripFull.clear(); m_StripOrigin.clear();
  for (auto& page : m_SnapPages) *page.second.cancel = true;
  m_SnapPages.clear();
  m_Thumbs->SetDocument(nullptr);
  m_Document = std::move(document);
  m_Thumbs->SetDocument(m_Document.get());
  m_Page->placeholder.clear();
  m_MaxPageWidthPt = 1.0;
  for (const auto& size : m_Document->Sizes()) m_MaxPageWidthPt = std::max(m_MaxPageWidthPt, static_cast<double>(size.wPt));
  m_CtxPage = m_InputPage = m_DraftPage = m_PointerPage = m_ExtractPage = -1;
  ClearSearch();
  m_Annotations = {}; m_Draft.clear();
  ClearAnnotationSelection();
  ++m_Revision;
  m_ExtractMode = false; m_ExtractDragging = false; m_ExtractBox = false; m_ExtractResult.reset(); UpdateToolStatus();
  if (m_ExtractReview) m_ExtractReview->hide();
  m_RobustPicks.clear(); m_SelectedCheck = -1; m_SnapHit = false; m_SnapPrevValid = false;
  m_FileScales = pdfview::ReadPageScales(m_Document->Path());
  if (!m_FileScales.error.empty()) qWarning() << "PDF scale: could not read the scales in" << path << "-" << m_FileScales.error.c_str();
  for (const auto& page : m_FileScales.unusable)
    qWarning() << "PDF scale: page" << page.first + 1 << "of" << path << "has a scale that cannot be used -" << page.second.c_str();
  const QSignalBlocker pageBlock(m_PageNumber);
  m_PageNumber->setRange(1, m_Document->PageCount());
  m_PageNumber->setValue(1);
  m_Scroll->verticalScrollBar()->setValue(0); m_Scroll->horizontalScrollBar()->setValue(0);
  SyncPageNav();
  SyncThumbnail();
  setWindowTitle("GoSurvey | " + path + "[*]"); setWindowModified(false);
  UpdateScaleDisplay();
  if (m_SnapOn) StartSnapRead(0);
  RequestRender();
  return true;
}

void PdfWindow::RequestRender(bool keepPicture) {
  if (!m_Document) return;
  if (m_Compare && m_CompareDetailTimer) m_CompareDetailTimer->start(); // the comparison's sharp picture follows the zoom
  RebuildLayout();
  UpdateZoomUi();
  if (m_Layout == Layout::Continuous) { ScheduleStrip(); m_Page->update(); return; }
  if (!keepPicture) { m_Page->image = {}; m_Page->imageFull = {}; m_Page->imageOrigin = {}; }
  m_Page->update();
  if (m_Work.valid()) { m_Pending = true; m_Cancel = true; return; }
  const int page = m_PageNumber->value() - 1;
  const QSize target = RenderPx(page);
  const int w = target.width(), h = target.height();
  const QRect region = NeededRegion(page);
  m_WorkRegion = region; m_WorkFull = target;
  m_Cancel = false;
  m_Work = std::async(std::launch::async, [this, page, w, h, region] {
    pdfview::Bitmap bitmap;
    const bool ok = Windowed(QSize(w, h))
        ? m_Document->RenderRegion(page, {w, h}, {region.x(), region.y(), region.width(), region.height()}, bitmap,
                                   [this] { return m_Cancel.load(); })
        : m_Document->RenderPage(page, w, h, bitmap, [this] { return m_Cancel.load(); });
    return ok ? bitmap : pdfview::Bitmap{};
  });
  statusBar()->showMessage("Rendering page...");
  m_Poll->start();
}

void PdfWindow::PollRender() {
  if (!m_Work.valid() || m_Work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_Poll->stop();
  try {
    auto bitmap = m_Work.get();
    if (m_Layout != Layout::PageByPage) {
      // switched to the continuous layout while this page was rendering: its bitmap is not wanted
    } else if (!m_Pending && bitmap.w > 0 && bitmap.h > 0) {
      QImage image(bitmap.bgra.data(), bitmap.w, bitmap.h, static_cast<qsizetype>(bitmap.w) * 4, QImage::Format_ARGB32);
      m_Page->image = image.copy();
      // The picture is drawn at the display's pixel density, so imageFull is in device pixels.
      m_Page->imageOrigin = m_WorkRegion.topLeft();
      m_Page->imageFull = m_WorkFull;
      if (RenderPx(CurrentPage()) != m_WorkFull) m_Pending = true; // zoomed again while it drew
      m_Page->update();
      statusBar()->showMessage(QString("Page %1 of %2").arg(m_PageNumber->value()).arg(m_Document->PageCount()));
    } else if (!m_Pending) statusBar()->showMessage("The page could not be rendered.");
  } catch (const std::exception& error) {
    statusBar()->showMessage(QString::fromUtf8(error.what()));
  }
  if (m_Layout != Layout::PageByPage) m_Pending = false;
  if (m_Pending) { m_Pending = false; RequestRender(true); } // the picture was already dropped if the page changed
}

pdfview::PageScale PdfWindow::CurrentScale() const {
  const auto* scale = EffectiveScale(ActivePage());
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
  if (!m_Document || PageWidthPx() <= 0 || PageHeightPx() <= 0) return {};
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(ActivePage())];
  return {point.x() * size.wPt / PageWidthPx(), (PageHeightPx() - point.y()) * size.hPt / PageHeightPx()};
}

int PdfWindow::HitAnnotation(QPointF p, float tolerance) const {
  const int page = ActivePage();
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
    else if (a.kind == pdfview::Annot::Kind::Leader && !a.pts.empty()) {
      const auto g = pdfview::LeaderLine(a);
      const QRectF box(QPointF(a.x0,a.y0), QPointF(a.x1,a.y1));
      if (box.normalized().adjusted(-tolerance,-tolerance,tolerance,tolerance).contains(p) ||
          segmentDistance(p, {g.sx,g.sy}, {g.tx,g.ty}) <= tolerance) return i;
    } else if (a.UsesPoints()) {
      if (a.kind == pdfview::Annot::Kind::RevCloud && a.pts.size() > 2 &&
          segmentDistance(p, {a.pts.back().first,a.pts.back().second}, {a.pts.front().first,a.pts.front().second}) <= tolerance) return i;
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

namespace {
// Where the editing handles of a mark sit, in page points: a callout's target and box corner, the picks of a measurement or cloud
// (plus a Length's dimension-line midpoint), a line's two ends, or the four corners of a box.
std::vector<QPointF> GripPoints(const pdfview::Annot& a) {
  std::vector<QPointF> grips;
  if (a.kind == pdfview::Annot::Kind::Leader && !a.pts.empty()) {
    grips = {{a.pts[0].first, a.pts[0].second}, {std::min(a.x0, a.x1), std::max(a.y0, a.y1)}};
  } else if (a.UsesPoints()) {
    for (const auto& p : a.pts) grips.push_back({p.first, p.second});
    if (a.kind == pdfview::Annot::Kind::Length && a.pts.size() == 2) {
      const auto line = pdfview::LengthDimLine(a);
      grips.push_back({(line.x0 + line.x1) * .5, (line.y0 + line.y1) * .5});
    }
  } else if (a.kind == pdfview::Annot::Kind::Line) grips = {{a.x0, a.y0}, {a.x1, a.y1}};
  else grips = {{a.x0, a.y0}, {a.x1, a.y1}, {a.x0, a.y1}, {a.x1, a.y0}};
  return grips;
}
} // namespace

// A callout's box follows its text: top-left corner kept, room (kLeaderPad) round the text.
void PdfWindow::FitLeaderBox(pdfview::Annot& a) {
  float w = 0.f, h = 0.f;
  pdfview::MeasureText(a.font, a.text, a.fontSize, w, h);
  const float left = std::min(a.x0, a.x1), top = std::max(a.y0, a.y1);
  a.x0 = left; a.x1 = left + w + 2.f * pdfview::kLeaderPad;
  a.y1 = top; a.y0 = top - h - 2.f * pdfview::kLeaderPad;
}

void PdfWindow::ClearAnnotationSelection() {
  m_SelectedAnnotation = -1;
  m_SelectedMore.clear();
  m_AnnotationDragging = false;
  m_AnnotationHandle = -1;
  m_DragPreview.reset();
}

// Two-corner box select, as in the drawing: left to right selects what the box fully encloses (window), right to
// left selects what it touches (crossing).
void PdfWindow::SelectInBox(QPointF first, QPointF second) {
  const bool window = second.x() >= first.x();
  const QRectF box = QRectF(first, second).normalized();
  std::vector<int> hits;
  const auto& items = m_Annotations.Items();
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const auto& a = items[static_cast<std::size_t>(i)];
    if (a.page != ActivePage()) continue;
    float left = std::min(a.x0, a.x1), right = std::max(a.x0, a.x1), bottom = std::min(a.y0, a.y1), top = std::max(a.y0, a.y1);
    const auto grow = [&](float x, float y) { left = std::min(left, x); right = std::max(right, x); bottom = std::min(bottom, y); top = std::max(top, y); };
    if (a.UsesPoints() || a.kind == pdfview::Annot::Kind::Leader) {
      if (!a.pts.empty()) { left = right = a.pts.front().first; bottom = top = a.pts.front().second; }
      for (const auto& p : a.pts) grow(p.first, p.second);
      if (a.kind == pdfview::Annot::Kind::Leader) { grow(a.x0, a.y0); grow(a.x1, a.y1); }
    }
    const QRectF bounds(QPointF(left, bottom), QPointF(right, top));
    const bool hit = window ? box.contains(bounds) : (box.intersects(bounds) || bounds.contains(box.center()) || box.contains(bounds));
    if (hit) hits.push_back(i);
  }
  m_SelectedMore.clear();
  m_SelectedAnnotation = hits.empty() ? -1 : hits.back();
  if (!hits.empty()) { hits.pop_back(); m_SelectedMore = hits; }
  m_Page->update();
  statusBar()->showMessage(hits.empty() && m_SelectedAnnotation < 0 ? QString("Nothing in the box.") :
                           QString("%1 selected.").arg(static_cast<int>(m_SelectedMore.size()) + 1));
}

bool PdfWindow::IsSelected(int index) const {
  return index == m_SelectedAnnotation || (m_SelectedAnnotation >= 0 && std::find(m_SelectedMore.begin(), m_SelectedMore.end(), index) != m_SelectedMore.end());
}

void PdfWindow::ApplyAnnotationStyle(pdfview::Annot& a) const {
  a.color = m_AnnotationColor;
  a.thickness = m_AnnotationThickness;
  a.fontSize = m_AnnotationFontSize;
  a.fill = m_AnnotationFill && (a.kind == pdfview::Annot::Kind::Rect || a.kind == pdfview::Annot::Kind::Ellipse);
  if (a.kind == pdfview::Annot::Kind::Text || a.kind == pdfview::Annot::Kind::Leader) {
    a.font = m_AnnotationFont; a.bold = m_AnnotationBold; a.italic = m_AnnotationItalic;
  }
}

void PdfWindow::EditSelectedAnnotation() {
  if (m_SelectedAnnotation < 0 || m_SelectedAnnotation >= static_cast<int>(m_Annotations.Items().size())) return;
  const auto& current = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
  if (current.kind != pdfview::Annot::Kind::Text && current.kind != pdfview::Annot::Kind::Leader) return;
  bool accepted = false;
  const QString value = QInputDialog::getMultiLineText(this, "Edit PDF text", "Text", QString::fromUtf8(current.text.c_str()), &accepted);
  if (!accepted) return;
  if (value.isEmpty()) { statusBar()->showMessage("Text annotation cannot be empty."); return; }
  auto edited = current; edited.text = value.toUtf8().toStdString();
  if (edited.kind == pdfview::Annot::Kind::Leader) FitLeaderBox(edited);
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
  } else if (edited.kind == pdfview::Annot::Kind::Leader && !edited.pts.empty()) { // handle 0 is the arrow tip, 1 the note
    if (m_AnnotationHandle == 0) { edited.pts[0].first += dx; edited.pts[0].second += dy; }
    else { edited.x0 += dx; edited.x1 += dx; edited.y0 += dy; edited.y1 += dy; }
  } else if (edited.UsesPoints() && m_AnnotationHandle < static_cast<int>(edited.pts.size())) {
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
  if (!m_Document || (m_Tool < 0 && m_Tool != kToolSelect) || IsBusy() || !PageReady()) return;
  UpdateSnap(point); // the click's own position, so a click without a prior move still snaps
  const auto size = m_Document->Sizes()[ActivePage()];
  if (m_Tool == kToolSelect) {
    if (button != Qt::LeftButton) return;
    const QPointF pagePoint = CanvasToPage(point);
    if (!m_Draft.empty() && m_DraftPage == ActivePage()) { // the second corner of a box select
      const QPointF first(m_Draft.front().first, m_Draft.front().second);
      m_Draft.clear();
      SelectInBox(first, pagePoint);
      return;
    }
    if (m_SelectedAnnotation >= 0 && m_SelectedAnnotation < static_cast<int>(m_Annotations.Items().size()) && m_SelectedMore.empty()) {
      const auto& selected = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
      const std::vector<QPointF> grips = GripPoints(selected);
      const float tol = static_cast<float>(kGripPickDip * size.wPt / std::max(1, PageWidthPx()));
      for (int i = 0; i < static_cast<int>(grips.size()); ++i) {
        if (QLineF(pagePoint, grips[static_cast<std::size_t>(i)]).length() <= tol) {
          m_AnnotationHandle = i; m_AnnotationDragging = true; m_DragStart = point; m_DragOriginal = selected; return;
        }
      }
    }
    m_SelectedMore.clear();
    m_SelectedAnnotation = HitAnnotation(pagePoint, static_cast<float>(kMarkPickDip * size.wPt / std::max(1, PageWidthPx())));
    if (m_SelectedAnnotation >= 0) {
      m_AnnotationDragging = true; m_AnnotationHandle = -1; m_DragStart = point;
      m_DragOriginal = m_Annotations.Items()[static_cast<std::size_t>(m_SelectedAnnotation)];
    } else { // empty space: this is the first corner of a box select
      m_Draft = {{static_cast<float>(pagePoint.x()), static_cast<float>(pagePoint.y())}};
      m_DraftPage = ActivePage();
    }
    m_Page->update(); return;
  }
  if (!m_Draft.empty() && m_DraftPage >= 0 && m_DraftPage != ActivePage()) m_Draft.clear(); // picks stay on one page
  if (m_Draft.empty()) m_DraftPage = ActivePage();
  if (m_Tool == kToolCalibrate || m_Tool == kToolCheck) {
    if (button != Qt::LeftButton) { m_Draft.clear(); m_Page->update(); return; }
    if (m_Tool == kToolCheck && m_Draft.empty() && m_CheckTarget == 0 && !EffectiveScale(ActivePage())) {
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
  const bool polygonCloud = kind == pdfview::Annot::Kind::RevCloud && m_CloudPolygon;
  const bool multi = kind == pdfview::Annot::Kind::PolyLength || kind == pdfview::Annot::Kind::Area || polygonCloud;
  const std::size_t required = kind == pdfview::Annot::Kind::Angle || kind == pdfview::Annot::Kind::Area || polygonCloud ? 3 : 2;
  if (button == Qt::RightButton && (!multi || m_Draft.size() < required)) { m_Draft.clear(); m_Page->update(); return; }
  if (m_Draft.size() < required || (multi && button != Qt::RightButton)) { m_Page->update(); return; }
  pdfview::Annot annotation;
  annotation.kind = kind; annotation.page = ActivePage();
  ApplyAnnotationStyle(annotation);
  annotation.x0 = m_Draft.front().first; annotation.y0 = m_Draft.front().second;
  annotation.x1 = m_Draft.back().first; annotation.y1 = m_Draft.back().second;
  if (annotation.UsesPoints()) annotation.pts = m_Draft;
  if (kind == pdfview::Annot::Kind::RevCloud && !m_CloudPolygon) { // two opposite corners make a rectangle
    const auto a = m_Draft.front(), b = m_Draft.back();
    if (std::abs(a.first - b.first) * PageWidthPx() / std::max(1.f, size.wPt) < kMinDragDip || std::abs(a.second - b.second) * PageWidthPx() / std::max(1.f, size.wPt) < kMinDragDip) { m_Draft.clear(); m_Page->update(); return; }
    annotation.pts = {a, {b.first, a.second}, b, {a.first, b.second}};
  }
  annotation.offset = dimensionOffset;
  if (kind == pdfview::Annot::Kind::Leader) { // click 1 is the arrow tip, click 2 the note's top-left corner
    annotation.pts = {m_Draft.front()};
    annotation.x0 = annotation.x1 = m_Draft.back().first;
    annotation.y0 = annotation.y1 = m_Draft.back().second;
    const double reach = std::hypot(m_Draft.back().first - m_Draft.front().first, m_Draft.back().second - m_Draft.front().second);
    if (reach * PageWidthPx() / std::max(1.f, size.wPt) < kMinDragDip) { m_Draft.clear(); m_Page->update(); return; } // both clicks on one spot
    bool accepted = false;
    const auto text = QInputDialog::getMultiLineText(this, "PDF callout", "Text", {}, &accepted);
    if (!accepted || text.isEmpty()) { m_Draft.clear(); m_Page->update(); return; }
    annotation.text = text.toUtf8().toStdString();
    FitLeaderBox(annotation);
  }
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
  if (!m_ExtractDragging && !m_ExtractBox) return;
  if (m_Layout == Layout::Continuous && m_ExtractPage != ActivePage()) return;
  painter.setPen(QPen(QColor("#e7a800"), 2, Qt::DashLine));
  painter.setBrush(QColor(255, 208, 64, 55));
  painter.drawRect(QRectF(m_ExtractStart, m_ExtractEnd).normalized());
}

void PdfWindow::BeginExtract(QPointF point, Qt::MouseButton button) {
  if (!m_ExtractMode) return;
  if (button != Qt::LeftButton) {
    m_ExtractMode = false; UpdateToolStatus(); m_Page->unsetCursor(); statusBar()->showMessage("Coordinate extraction cancelled."); return;
  }
  m_ExtractDragging = true; m_ExtractPage = ActivePage(); m_ExtractStart = m_ExtractEnd = point; m_Page->update();
}

void PdfWindow::MoveExtract(QPointF point) {
  if (!m_ExtractDragging) return;
  m_ExtractEnd = point; m_Page->update();
}

void PdfWindow::EndExtract(QPointF point, Qt::MouseButton button) {
  if (!m_ExtractDragging || button != Qt::LeftButton) return;
  m_ExtractEnd = point; m_ExtractDragging = false; m_ExtractMode = false; UpdateToolStatus();
  m_Page->unsetCursor(); m_Page->update();
  if (QRectF(m_ExtractStart, m_ExtractEnd).normalized().width() < 5 ||
      QRectF(m_ExtractStart, m_ExtractEnd).normalized().height() < 5) {
    statusBar()->showMessage("Selection was too small; coordinate extraction cancelled."); return;
  }
  ExtractRegion();
}

void PdfWindow::ExtractRegion() {
  if (!m_Document || !PageReady()) return;
  std::vector<pdfview::DimText> texts;
  std::vector<pdfview::DimSeg> segments;
  if (!m_Document->AuditPageData(ActivePage(), texts, segments, [] { return false; })) {
    QMessageBox::warning(this, "Extract Coordinates", "Could not read selectable text from this PDF page."); return;
  }
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(ActivePage())];
  const QRectF box = QRectF(m_ExtractStart, m_ExtractEnd).normalized();
  const float x0 = static_cast<float>(box.left() * size.wPt / PageWidthPx());
  const float x1 = static_cast<float>(box.right() * size.wPt / PageWidthPx());
  const float y0 = static_cast<float>((PageHeightPx() - box.bottom()) * size.hPt / PageHeightPx());
  const float y1 = static_cast<float>((PageHeightPx() - box.top()) * size.hPt / PageHeightPx());
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
    table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    table->setColumnWidth(7, 220); // room for the Accept and Deny buttons whole
    table->setWordWrap(true); table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table);
    auto* close = new QPushButton("Close", m_ExtractReview); close->setObjectName("pdfCoordinateReviewClose");
    connect(close, &QPushButton::clicked, m_ExtractReview, &QDialog::reject);
    connect(m_ExtractReview, &QDialog::rejected, this, [this] { m_ExtractBox = false; m_Page->update(); }); // review closed: the box goes
    auto* acceptAll = new QPushButton("Accept all", m_ExtractReview); acceptAll->setObjectName("pdfCoordinateAcceptAll");
    auto* denyAll = new QPushButton("Deny all", m_ExtractReview); denyAll->setObjectName("pdfCoordinateDenyAll");
    // Accept all walks the list top to bottom through the same per-candidate path (so an id conflict still asks);
    // it stops at the first candidate that could not be created, leaving that one and the rest for review.
    connect(acceptAll, &QPushButton::clicked, this, [this] {
      if (!m_ExtractResult || m_ExtractResult->candidates.empty()) return;
      if (!m_Editor) { QMessageBox::information(this, "Extract Coordinates", "Open the PDF from an active drawing to accept coordinates."); return; }
      auto& list = m_ExtractResult->candidates;
      const std::size_t created = m_Editor->CommitPdfCoordinates(list);
      list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(created));
      ShowExtractReview();
      statusBar()->showMessage(list.empty() ? "Extracted coordinates accepted." : QString("%1 accepted; the rest need review.").arg(created));
    });
    connect(denyAll, &QPushButton::clicked, this, [this] {
      if (!m_ExtractResult) return;
      m_ExtractResult->candidates.clear();
      ShowExtractReview();
      statusBar()->showMessage("Candidates denied; drawing unchanged.");
    });
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(acceptAll); buttons->addWidget(denyAll); buttons->addStretch(1); buttons->addWidget(close);
    layout->addLayout(buttons);
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
    accept->setMinimumWidth(90); deny->setMinimumWidth(90);
    accept->setObjectName(QString("pdfCoordinateAccept_%1").arg(row)); deny->setObjectName(QString("pdfCoordinateDeny_%1").arg(row));
    buttons->addWidget(accept); buttons->addWidget(deny); table->setCellWidget(row, 7, decisions);
    connect(accept, &QPushButton::clicked, this, [this, row] { DecideExtractCandidate(row, true); });
    connect(deny, &QPushButton::clicked, this, [this, row] { DecideExtractCandidate(row, false); });
  }
  table->resizeRowsToContents();
  m_ExtractBox = !m_ExtractResult->candidates.empty(); m_Page->update();
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
    m_ExtractBox = false; m_Page->update();
    statusBar()->showMessage(accept ? "Extracted coordinates accepted." : "Candidate denied; drawing unchanged.");
  } else ShowExtractReview();
}

void PdfWindow::DrawAnnotations(QPainter& painter) {
  if (!m_Document || !PageReady()) return;
  const int page = ActivePage();
  const auto size = m_Document->Sizes()[page];
  const double scaleX = static_cast<double>(PageWidthPx()) / size.wPt;
  const double scaleY = static_cast<double>(PageHeightPx()) / size.hPt;
  const double factor = std::sqrt(scaleX * scaleY);
  auto map = [&](float x, float y) { return QPointF(x * scaleX, (size.hPt - y) * scaleY); };
  // A callout: its box and text, then the line from the box to the arrow tip and the arrowhead.
  const auto drawLeader = [&](const pdfview::Annot& a, bool preview) {
    const auto g = pdfview::LeaderLine(a);
    const QRectF box = QRectF(map(a.x0, a.y0), map(a.x1, a.y1)).normalized();
    painter.drawRect(box);
    painter.drawLine(map(g.sx, g.sy), map(g.tx, g.ty));
    if (!preview) {
      painter.save();
      painter.setBrush(painter.pen().color());
      painter.drawPolygon(QPolygonF({map(g.tx, g.ty), map(g.w1x, g.w1y), map(g.w2x, g.w2y)}));
      painter.restore();
      QFont font(QString::fromStdString(a.font)); font.setBold(a.bold); font.setItalic(a.italic);
      font.setPixelSize(std::max(1, qRound(a.fontSize * factor)));
      painter.setFont(font);
      const double pad = pdfview::kLeaderPad * factor;
      painter.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignLeft | Qt::AlignTop, QString::fromUtf8(a.text.c_str()));
    }
  };
  // A revision cloud: the scalloped outline of the polygon of corners.
  const auto drawCloud = [&](const pdfview::Annot& a) {
    QPainterPath path;
    bool first = true;
    for (const auto& arc : pdfview::RevCloudArcs(a)) {
      if (first) { path.moveTo(map(arc.x0, arc.y0)); first = false; }
      path.cubicTo(map(arc.c1x, arc.c1y), map(arc.c2x, arc.c2y), map(arc.x1, arc.y1));
    }
    painter.drawPath(path);
  };
  for (std::size_t itemIndex = 0; itemIndex < m_Annotations.Items().size(); ++itemIndex) {
    const auto& annotation = m_DragPreview && static_cast<int>(itemIndex) == m_SelectedAnnotation ? *m_DragPreview : m_Annotations.Items()[itemIndex];
    if (annotation.page != page) continue;
    const bool selected = IsSelected(static_cast<int>(itemIndex));
    const bool showGrips = static_cast<int>(itemIndex) == m_SelectedAnnotation && m_SelectedMore.empty();
    painter.setPen(QPen(selected ? QColor(QtAccentFor(false).strong) : QColor::fromRgb(annotation.color), std::max(1.0, annotation.thickness * factor)));
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
    } else if (annotation.kind == pdfview::Annot::Kind::Leader && !annotation.pts.empty()) {
      drawLeader(annotation, false);
    } else if (annotation.kind == pdfview::Annot::Kind::RevCloud) {
      drawCloud(annotation);
    } else if (annotation.IsDimension()) {
      QPolygonF polygon;
      for (const auto& pt : annotation.pts) polygon << map(pt.first, pt.second);
      painter.drawPolyline(polygon);
      if (annotation.kind == pdfview::Annot::Kind::Area) painter.drawLine(polygon.first(), polygon.last());
      const auto anchor = pdfview::DimensionLabelAnchor(annotation);
      painter.drawText(map(anchor.first, anchor.second), QString::fromUtf8(pdfview::DimensionLabel(annotation, CurrentScale()).c_str()));
    }
    if (showGrips) {
      std::vector<QPointF> grips = GripPoints(annotation);
      for (auto& grip : grips) grip = map(static_cast<float>(grip.x()), static_cast<float>(grip.y()));
      painter.setPen(QPen(QColor("#0d1521"), 1)); painter.setBrush(QColor(QtAccentFor(true).strong));
      for (const auto& grip : grips) painter.drawRect(QRectF(grip.x()-4,grip.y()-4,8,8));
    }
  }
  painter.setPen(QPen(QColor(QtAccentFor(false).strong), 2));
  for (std::size_t i = 0; (m_DraftPage < 0 || m_DraftPage == page) && m_Tool != kToolCalibrate && m_Tool != kToolCheck && i < m_Draft.size(); ++i) {
    const auto pt = map(m_Draft[i].first, m_Draft[i].second);
    painter.drawEllipse(pt, 3, 3);
    const auto draftKind = static_cast<pdfview::Annot::Kind>(m_Tool);
    const bool connected = draftKind == pdfview::Annot::Kind::Length || draftKind == pdfview::Annot::Kind::PolyLength ||
                           draftKind == pdfview::Annot::Kind::Area || draftKind == pdfview::Annot::Kind::Angle ||
                           draftKind == pdfview::Annot::Kind::RevCloud;
    if (i && connected) painter.drawLine(map(m_Draft[i - 1].first, m_Draft[i - 1].second), pt);
  }
  // Box select under Select: blue and solid for a window (left to right), green and dashed for a crossing, as in the drawing.
  if (m_Tool == kToolSelect && !m_Draft.empty() && m_PointerIn && (m_DraftPage < 0 || m_DraftPage == page) && (m_PointerPage < 0 || m_PointerPage == page)) {
    const QPointF anchor = map(m_Draft.front().first, m_Draft.front().second);
    const QPointF end = m_LastPointer;
    const bool window = end.x() >= anchor.x();
    const QColor colour = window ? QColor(70, 140, 220) : QColor(65, 190, 125);
    painter.setPen(QPen(colour, 1, window ? Qt::SolidLine : Qt::DashLine));
    painter.setBrush(QColor(colour.red(), colour.green(), colour.blue(), 35));
    painter.drawRect(QRectF(anchor, end).normalized());
  }
  // Rubber band from the picks so far to the pointer, so the second click has a preview.
  if (m_Tool >= 0 && m_Tool != kToolCalibrate && m_Tool != kToolCheck && !m_Draft.empty() && m_PointerIn &&
      (m_DraftPage < 0 || m_DraftPage == page) && (m_PointerPage < 0 || m_PointerPage == page)) {
    const QPointF start = map(m_Draft.front().first, m_Draft.front().second);
    const QPointF end = m_SnapHit ? map(m_SnapX, m_SnapY) : m_LastPointer;
    const auto kind = static_cast<pdfview::Annot::Kind>(m_Tool);
    painter.setPen(QPen(QColor(QtAccentFor(false).strong), 1.5, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    if (kind == pdfview::Annot::Kind::Ellipse) painter.drawEllipse(QRectF(start, end).normalized());
    else if (kind == pdfview::Annot::Kind::Rect || kind == pdfview::Annot::Kind::Text) painter.drawRect(QRectF(start, end).normalized());
    else if (kind == pdfview::Annot::Kind::Leader) { // the arrow and an empty box that follow the pointer
      pdfview::Annot ghost; ghost.kind = kind; ghost.font = m_AnnotationFont; ghost.fontSize = m_AnnotationFontSize; ghost.thickness = m_AnnotationThickness;
      ghost.pts = {m_Draft.front()}; ghost.text = "Note";
      const QPointF at = CanvasToPage(end);
      ghost.x0 = ghost.x1 = static_cast<float>(at.x()); ghost.y0 = ghost.y1 = static_cast<float>(at.y());
      FitLeaderBox(ghost);
      drawLeader(ghost, true);
    } else if (kind == pdfview::Annot::Kind::RevCloud) {
      pdfview::Annot ghost; ghost.kind = kind; ghost.thickness = m_AnnotationThickness; ghost.pts = m_Draft;
      const QPointF at = CanvasToPage(end);
      const std::pair<float, float> corner(static_cast<float>(at.x()), static_cast<float>(at.y()));
      if (m_CloudPolygon) ghost.pts.push_back(corner);
      else ghost.pts = {m_Draft.front(), {corner.first, m_Draft.front().second}, corner, {m_Draft.front().first, corner.second}};
      if (ghost.pts.size() >= 3) drawCloud(ghost); else painter.drawLine(start, end);
    }
    else painter.drawLine(map(m_Draft.back().first, m_Draft.back().second), end);
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

// Enter finishes a polygon revision cloud that has its three corners.
void PdfWindow::keyPressEvent(QKeyEvent* event) {
  const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
  if (enter && m_Tool == static_cast<int>(pdfview::Annot::Kind::RevCloud) && m_CloudPolygon && m_Draft.size() >= 3) {
    PageScope scope(*this, m_Layout == Layout::Continuous ? m_DraftPage : -1);
    PageClick(m_LastPointer, Qt::RightButton);
    event->accept();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void PdfWindow::closeEvent(QCloseEvent* event) {
  if (m_OpenWork.valid()) { statusBar()->showMessage("Wait for the PDF to finish opening before closing."); event->ignore(); return; }
  if (m_SaveWork.valid()) { statusBar()->showMessage("Wait for the PDF save to finish before closing."); event->ignore(); return; }
  if (!ConfirmDiscard()) { event->ignore(); return; }
  m_Cancel = true; m_StripCancel = true; m_SearchCancel = true; m_AuditCancel = true;
  if (m_Compare) m_Compare->cancel = true;
  event->accept();
}

// ---------------------------------------------------------------------------------------------------
// REQ-387 clause 2 / REQ-455: layout, zoom, pan and the continuous strip
// ---------------------------------------------------------------------------------------------------

int PdfWindow::CurrentPage() const { return m_PageNumber->value() - 1; }
int PdfWindow::ActivePage() const { return m_CtxPage >= 0 ? m_CtxPage : CurrentPage(); }

QSize PdfWindow::PagePx(int page) const {
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(page)];
  const double wide = WidthAtZoomOne(page) * m_Zoom;
  const int w = std::clamp(qRound(wide), 64, 200000);
  const int h = std::clamp(qRound(static_cast<double>(w) * size.hPt / size.wPt), 64, 400000);
  return {w, h};
}

// A page bigger than this is never drawn whole (a bitmap that size would be gigabytes): only the window around
// the view is, and it is redrawn as the view moves.
bool PdfWindow::Windowed(QSize full) {
  return static_cast<long long>(full.width()) * full.height() > 24'000'000LL;
}

// The page's size in device pixels: pages are drawn at the display's density so they stay crisp on a scaled screen.
QSize PdfWindow::RenderPx(int page) const {
  const QSize logical = PagePx(page);
  const double ratio = devicePixelRatioF();
  return {std::max(1, qRound(logical.width() * ratio)), std::max(1, qRound(logical.height() * ratio))};
}

QRect PdfWindow::NeededRegion(int page) const {
  const QSize full = RenderPx(page);
  if (!Windowed(full)) return QRect(QPoint(0, 0), full);
  const QRect pageRect = PageRect(page);
  const QSize view = m_Scroll->viewport()->size();
  QRect seen(m_Scroll->horizontalScrollBar()->value(), m_Scroll->verticalScrollBar()->value(), view.width(), view.height());
  seen.translate(-pageRect.topLeft());
  seen.adjust(-view.width() / 2, -view.height() / 2, view.width() / 2, view.height() / 2);
  const double ratio = devicePixelRatioF();
  const QRect device(QPoint(qFloor(seen.x() * ratio), qFloor(seen.y() * ratio)), QPoint(qCeil(seen.right() * ratio), qCeil(seen.bottom() * ratio)));
  return device.intersected(QRect(QPoint(0, 0), full));
}

// Page-by-page: true when the current picture covers everything the view shows.
bool PdfWindow::PageWindowCovers() const {
  if (m_Page->image.isNull()) return false;
  if (m_Page->imageFull.isEmpty()) return true;
  const double factor = static_cast<double>(m_Page->width()) / std::max(1, m_Page->imageFull.width());
  const QRectF have(m_Page->imageOrigin.x() * factor, m_Page->imageOrigin.y() * factor, m_Page->image.width() * factor, m_Page->image.height() * factor);
  const QRectF view(m_Scroll->horizontalScrollBar()->value(), m_Scroll->verticalScrollBar()->value(), m_Scroll->viewport()->width(), m_Scroll->viewport()->height());
  return have.adjusted(-1, -1, 1, 1).contains(view.intersected(QRectF(m_Page->rect())));
}

// The page's width at zoom 1: the viewport width in page-by-page mode ("fit width"); in the continuous layout the
// widest page is that wide and narrower pages keep their proportion, so the pages stay in true relative size.
double PdfWindow::WidthAtZoomOne(int page) const {
  // Measured with the vertical scroll bar always counted: using the viewport's own width made the fit flip every time the
  // bar appeared or went away, and the page re-rendered endlessly between the two sizes.
  const double fit = std::max(200, m_Scroll->width() - 2 * m_Scroll->frameWidth() - m_Scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_Scroll) - 12);
  if (m_Layout != Layout::Continuous) return fit;
  return fit * static_cast<double>(m_Document->Sizes()[static_cast<std::size_t>(page)].wPt) / m_MaxPageWidthPt;
}

double PdfWindow::ZoomPercent() const {
  if (!m_Document) return 100.0;
  const int page = CurrentPage();
  const double actualPx = static_cast<double>(m_Document->Sizes()[static_cast<std::size_t>(page)].wPt) * 96.0 / 72.0;
  return WidthAtZoomOne(page) * m_Zoom / actualPx * 100.0;
}

void PdfWindow::SetZoomPercent(double percent) {
  if (!m_Document || percent <= 0) return;
  const int page = CurrentPage();
  const double actualPx = static_cast<double>(m_Document->Sizes()[static_cast<std::size_t>(page)].wPt) * 96.0 / 72.0;
  SetZoom(percent / 100.0 * actualPx / WidthAtZoomOne(page), m_Scroll->viewport()->rect().center());
}

void PdfWindow::UpdateZoomUi() {
  if (!m_ZoomBox) return;
  const int percent = std::clamp(qRound(ZoomPercent()), m_ZoomBox->minimum(), m_ZoomBox->maximum());
  const QSignalBlocker block(m_ZoomBox);
  m_ZoomBox->setValue(percent);
}

// The bottom bar's "of N" count beside the page box.
void PdfWindow::SyncPageNav() {
  if (!m_StatusPageCount) return;
  m_StatusPageCount->setText(QString("of %1").arg(m_PageNumber->maximum()));
}

// Properties tab of the side panel: what the open document and the page being read are.
void PdfWindow::UpdatePropertiesPanel() {
  const auto show = [this](const char* name, const QString& text) { if (auto* label = findChild<QLabel*>(name)) label->setText(text); };
  if (!m_Document) {
    for (const char* name : {"pdfPropFile", "pdfPropPages", "pdfPropNumber", "pdfPropSize", "pdfPropScale"}) show(name, "-");
    return;
  }
  const int page = CurrentPage();
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(page)];
  show("pdfPropFile", QFileInfo(QString::fromStdWString(m_Document->Path().wstring())).fileName());
  show("pdfPropPages", QString::number(m_Document->PageCount()));
  show("pdfPropNumber", QString("%1 of %2").arg(page + 1).arg(m_Document->PageCount()));
  show("pdfPropSize", QString("%1 x %2 in").arg(size.wPt / 72.0, 0, 'f', 2).arg(size.hPt / 72.0, 0, 'f', 2));
  const auto* scale = EffectiveScale(page);
  show("pdfPropScale", scale ? QString::fromUtf8(scale->RatioText().c_str()) : QString("Unscaled"));
}

void PdfWindow::ZoomBy(double factor, QPoint viewportPoint) { SetZoom(m_Zoom * factor, viewportPoint); }

void PdfWindow::FitWidth() {
  if (!m_Document) { m_Zoom = 1.0; return; }
  const double wide = m_Layout == Layout::Continuous
      ? m_MaxPageWidthPt / static_cast<double>(m_Document->Sizes()[static_cast<std::size_t>(CurrentPage())].wPt) : 1.0;
  SetZoom(wide, m_Scroll->viewport()->rect().center());
  m_Scroll->horizontalScrollBar()->setValue(0);
}

void PdfWindow::FitPage() {
  if (!m_Document) return;
  const int page = CurrentPage();
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(page)];
  const double widthZoom = m_Layout == Layout::Continuous ? m_MaxPageWidthPt / static_cast<double>(size.wPt) : 1.0;
  const double heightAtOne = WidthAtZoomOne(page) * size.hPt / size.wPt;
  const double heightZoom = std::max(100, m_Scroll->viewport()->height() - 2 * kPageGap) / heightAtOne;
  SetZoom(std::min(widthZoom, heightZoom), m_Scroll->viewport()->rect().center());
  m_Scroll->horizontalScrollBar()->setValue(0);
  if (m_Layout == Layout::Continuous) ScrollToPage(page);
}

PdfWindow::ZoomAnchor PdfWindow::CaptureAnchor(QPoint viewportPoint) const {
  ZoomAnchor anchor;
  if (!m_Document) return anchor;
  const QPoint canvasPoint = m_Page->mapFrom(m_Scroll->viewport(), viewportPoint);
  anchor.page = m_Layout == Layout::Continuous ? PageAtY(canvasPoint.y()) : CurrentPage();
  const QRect rect = PageRect(anchor.page);
  anchor.fx = (canvasPoint.x() - rect.x()) / std::max(1.0, static_cast<double>(rect.width()));
  anchor.fy = (canvasPoint.y() - rect.y()) / std::max(1.0, static_cast<double>(rect.height()));
  return anchor;
}

void PdfWindow::ApplyScrollRange(QScrollBar* bar, int canvasLength, int viewLength) {
  bar->setRange(0, std::max(0, canvasLength - viewLength)); // what QScrollArea settles on once it has laid the page out
  bar->setPageStep(viewLength);
}

void PdfWindow::RestoreAnchor(const ZoomAnchor& anchor, QPoint viewportPoint) {
  if (!m_Document) return;
  const QRect rect = PageRect(anchor.page);
  const QPointF target(rect.x() + anchor.fx * rect.width(), rect.y() + anchor.fy * rect.height());
  const QSize view = m_Scroll->viewport()->size();
  ApplyScrollRange(m_Scroll->horizontalScrollBar(), m_Page->width(), view.width());
  ApplyScrollRange(m_Scroll->verticalScrollBar(), m_Page->height(), view.height());
  m_Scroll->horizontalScrollBar()->setValue(m_Page->width() > view.width() ? qRound(target.x() - viewportPoint.x()) : 0);
  m_Scroll->verticalScrollBar()->setValue(m_Page->height() > view.height() ? qRound(target.y() - viewportPoint.y()) : 0);
}

// Zooms so the page point under viewportPoint stays under it; the page is re-rendered at the new size.
void PdfWindow::SetZoom(double zoom, QPoint viewportPoint) {
  if (m_Document) { // 5% to 1000% of the page's actual size
    const int page = CurrentPage();
    const double actualPx = static_cast<double>(m_Document->Sizes()[static_cast<std::size_t>(page)].wPt) * 96.0 / 72.0;
    zoom = std::clamp(zoom, 0.05 * actualPx / WidthAtZoomOne(page), 10.0 * actualPx / WidthAtZoomOne(page));
  } else zoom = std::clamp(zoom, 0.1, 4.0);
  if (!m_Document) { m_Zoom = zoom; return; }
  if (std::abs(zoom - m_Zoom) < 1e-9) { UpdateZoomUi(); return; }
  const ZoomAnchor anchor = CaptureAnchor(viewportPoint);
  m_Zoom = zoom;
  // Resize at once (the old picture is stretched to the new size, so the page follows the wheel without a blank
  // frame); the sharp render waits until the zooming pauses, so a run of wheel notches costs one render.
  RebuildLayout();
  UpdateZoomUi();
  if (m_Layout == Layout::PageByPage && m_Work.valid()) { m_Pending = true; m_Cancel = true; }
  m_Page->update();
  RestoreAnchor(anchor, viewportPoint);
  m_ZoomSettle->start();
}

void PdfWindow::CenterOnPagePoint(int page, double xPt, double yPt) {
  if (!m_Document || !PageReady() || page < 0 || page >= m_Document->PageCount()) return;
  if (m_Layout == Layout::PageByPage && page != CurrentPage()) return;
  const QRect rect = PageRect(page);
  const auto size = m_Document->Sizes()[static_cast<std::size_t>(page)];
  const int x = rect.x() + qRound(xPt * rect.width() / size.wPt);
  const int y = rect.y() + qRound((size.hPt - yPt) * rect.height() / size.hPt);
  m_Scroll->horizontalScrollBar()->setValue(std::max(0, x - m_Scroll->viewport()->width() / 2));
  m_Scroll->verticalScrollBar()->setValue(std::max(0, y - m_Scroll->viewport()->height() / 2));
}

// Page-by-page sizes the canvas to the current page; continuous stacks every page (top to bottom, centred).
void PdfWindow::RebuildLayout() {
  if (!m_Document) return;
  if (m_Layout == Layout::PageByPage) {
    const QSize px = PagePx(CurrentPage());
    if (m_Page->size() != px) m_Page->resize(px);
    return;
  }
  const int count = m_Document->PageCount();
  m_PageTop.assign(static_cast<std::size_t>(count), 0);
  m_PageWpx.assign(static_cast<std::size_t>(count), 0);
  m_PageHpx.assign(static_cast<std::size_t>(count), 0);
  int y = kPageGap, widest = 0;
  for (int page = 0; page < count; ++page) {
    const QSize px = PagePx(page);
    const auto index = static_cast<std::size_t>(page);
    m_PageTop[index] = y; m_PageWpx[index] = px.width(); m_PageHpx[index] = px.height();
    y += px.height() + kPageGap;
    widest = std::max(widest, px.width());
  }
  const QSize canvas(widest + 2 * kPageGap, y);
  if (m_Page->size() != canvas) m_Page->resize(canvas);
}

QRect PdfWindow::PageRect(int page) const {
  if (m_Layout != Layout::Continuous) return QRect(0, 0, m_Page->width(), m_Page->height());
  if (page < 0 || page >= static_cast<int>(m_PageTop.size())) return {};
  const auto index = static_cast<std::size_t>(page);
  return QRect((m_Page->width() - m_PageWpx[index]) / 2, m_PageTop[index], m_PageWpx[index], m_PageHpx[index]);
}

int PdfWindow::PageAtY(int y) const {
  if (m_PageTop.empty()) return 0;
  const auto after = std::upper_bound(m_PageTop.begin(), m_PageTop.end(), y);
  int page = std::max(0, static_cast<int>(after - m_PageTop.begin()) - 1);
  const int bottom = m_PageTop[static_cast<std::size_t>(page)] + m_PageHpx[static_cast<std::size_t>(page)];
  // In the gap below a page: the nearer of the two neighbours.
  if (y > bottom && page + 1 < static_cast<int>(m_PageTop.size()) && y - bottom > m_PageTop[static_cast<std::size_t>(page) + 1] - y) ++page;
  return page;
}

int PdfWindow::PageWidthPxOf(int page) const {
  if (m_Layout == Layout::Continuous && page >= 0 && page < static_cast<int>(m_PageWpx.size())) return m_PageWpx[static_cast<std::size_t>(page)];
  return m_Page->width();
}

int PdfWindow::PageHeightPxOf(int page) const {
  if (m_Layout == Layout::Continuous && page >= 0 && page < static_cast<int>(m_PageHpx.size())) return m_PageHpx[static_cast<std::size_t>(page)];
  return m_Page->height();
}

bool PdfWindow::PageReady() const {
  if (!m_Document) return false;
  return m_Layout == Layout::Continuous ? !m_PageTop.empty() : !m_Page->image.isNull();
}

void PdfWindow::SetLayout(Layout layout) {
  {
    const QSignalBlocker block(m_LayoutBox);
    m_LayoutBox->setCurrentIndex(layout == Layout::Continuous ? 1 : 0);
  }
  if (auto* menuAction = findChild<QAction*>(layout == Layout::Continuous ? "pdfLayoutContinuous" : "pdfLayoutPageByPage")) menuAction->setChecked(true);
  if (layout == m_Layout) return;
  const int page = CurrentPage();
  CancelStrip(true);
  m_Layout = layout;
  m_Draft.clear(); m_SnapHit = false; m_SnapPrevValid = false;
  if (layout == Layout::Continuous) {
    m_Cancel = true; m_Pending = false; // a page-by-page render still running is dropped when it ends
    m_Page->image = {}; m_Page->imageFull = {}; m_Page->imageOrigin = {};
    UpdateZoomUi();
    if (m_Document) {
      RebuildLayout();
      m_Scroll->horizontalScrollBar()->setValue(0);
      ScrollToPage(page);
      ScheduleStrip();
    }
  } else {
    m_StripCache.clear(); m_StripFull.clear(); m_StripOrigin.clear(); m_PageTop.clear(); m_PageWpx.clear(); m_PageHpx.clear();
    m_Scroll->verticalScrollBar()->setValue(0); m_Scroll->horizontalScrollBar()->setValue(0);
    RequestRender();
  }
  m_Page->update();
}

void PdfWindow::ScrollToPage(int page) {
  if (!m_Document) return;
  page = std::clamp(page, 0, m_Document->PageCount() - 1);
  if (m_Layout != Layout::Continuous) { m_PageNumber->setValue(page + 1); return; }
  if (m_PageTop.empty()) RebuildLayout();
  auto* bar = m_Scroll->verticalScrollBar();
  m_ProgrammaticScroll = true; // the page the user asked for stays current even when it is too short to reach the middle
  ApplyScrollRange(bar, m_Page->height(), m_Scroll->viewport()->height());
  bar->setValue(m_PageTop[static_cast<std::size_t>(page)] - kPageGap);
  m_ProgrammaticScroll = false;
  if (CurrentPage() != page) {
    m_SyncingPage = true;
    m_PageNumber->setValue(page + 1);
    m_SyncingPage = false;
  }
  ScheduleStrip();
}

// The page boxes meeting the viewport, first and last.
std::pair<int, int> PdfWindow::VisiblePages() const {
  if (m_PageTop.empty()) return {0, -1};
  const int top = m_Scroll->verticalScrollBar()->value();
  const int bottom = top + m_Scroll->viewport()->height();
  const int count = static_cast<int>(m_PageTop.size());
  int first = std::max(0, static_cast<int>(std::upper_bound(m_PageTop.begin(), m_PageTop.end(), top) - m_PageTop.begin()) - 1);
  if (m_PageTop[static_cast<std::size_t>(first)] + m_PageHpx[static_cast<std::size_t>(first)] < top && first + 1 < count) ++first;
  const int last = std::max(first, static_cast<int>(std::upper_bound(m_PageTop.begin(), m_PageTop.end(), bottom) - m_PageTop.begin()) - 1);
  return {first, std::min(last, count - 1)};
}

// The viewport moved: follow the page in the middle of it and render what came into view.
void PdfWindow::OnScrolled() {
  if (m_Layout != Layout::Continuous || !m_Document || m_PageTop.empty()) return;
  const int y = m_Scroll->verticalScrollBar()->value();
  if (y != m_LastScrollY) { m_ScrollDirection = y > m_LastScrollY ? 1 : -1; m_LastScrollY = y; }
  if (!m_ProgrammaticScroll) {
    const int page = PageAtY(y + m_Scroll->viewport()->height() / 2);
    if (page != CurrentPage()) {
      m_SyncingPage = true; // the thumbnail strip, page box and scale readout follow; the picks in progress stay
      m_PageNumber->setValue(page + 1);
      m_SyncingPage = false;
      statusBar()->showMessage(QString("Page %1 of %2").arg(page + 1).arg(m_Document->PageCount()));
    }
  }
  ScheduleStrip();
}

// Paints each page meeting the exposed rectangle: its cached picture (a blank sheet until it arrives; a picture of
// another zoom is scaled as a stand-in), then every overlay in the page's own coordinates.
bool PdfWindow::PaintStrip(QPainter& painter, const QRect& exposed) {
  if (m_Layout != Layout::Continuous || !m_Document || m_PageTop.empty()) return false;
  const int count = static_cast<int>(m_PageTop.size());
  int first = std::max(0, static_cast<int>(std::upper_bound(m_PageTop.begin(), m_PageTop.end(), exposed.top()) - m_PageTop.begin()) - 1);
  for (int page = first; page < count && m_PageTop[static_cast<std::size_t>(page)] <= exposed.bottom(); ++page) {
    const QRect rect = PageRect(page);
    if (!rect.intersects(exposed)) continue;
    painter.fillRect(rect, Qt::white);
    const auto cached = m_StripCache.find(page);
    if (cached != m_StripCache.end()) {
      const QSize full = m_StripFull.count(page) ? m_StripFull[page] : cached->second.size();
      const QPoint origin = m_StripOrigin.count(page) ? m_StripOrigin[page] : QPoint();
      const double factor = static_cast<double>(rect.width()) / std::max(1, full.width());
      const QRectF target(rect.x() + origin.x() * factor, rect.y() + origin.y() * factor, cached->second.width() * factor, cached->second.height() * factor);
      painter.setRenderHint(QPainter::SmoothPixmapTransform, factor != 1.0);
      painter.setClipRect(rect);
      painter.drawImage(target, cached->second);
      painter.setClipping(false);
    }
    painter.setPen(QPen(QColor(96, 104, 110), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect.adjusted(-1, -1, 0, 0));
    painter.save();
    painter.translate(rect.topLeft());
    painter.setClipRect(QRect(0, 0, rect.width(), rect.height()));
    painter.setRenderHint(QPainter::Antialiasing);
    {
      PageScope scope(*this, page);
      if (m_Page->overlay) m_Page->overlay(painter);
    }
    painter.restore();
  }
  return true;
}

void PdfWindow::CancelStrip(bool wait) {
  m_StripCancel = true;
  if (!wait) return;
  if (m_StripWork.valid()) {
    try { m_StripWork.wait(); } catch (const std::exception& error) { qWarning() << "PDF page worker wait failed:" << error.what(); }
    m_StripWork = {};
  }
  m_StripWorkPage = -1;
  if (m_StripPoll) m_StripPoll->stop();
}

// One worker at a time (REQ-387 clause 4: one-shot workers, not a pool): the visible pages first, then the pages
// ahead of the scroll direction, then one behind. A page that scrolled away is cancelled.
void PdfWindow::ScheduleStrip() {
  if (m_Layout != Layout::Continuous || !m_Document || m_PageTop.empty()) return;
  const auto [first, last] = VisiblePages();
  const int count = static_cast<int>(m_PageTop.size());
  std::vector<int> wanted;
  for (int page = first; page <= last; ++page) wanted.push_back(page);
  for (int step = 1; step <= kReadAhead; ++step) {
    const int ahead = m_ScrollDirection > 0 ? last + step : first - step;
    if (ahead >= 0 && ahead < count) wanted.push_back(ahead);
  }
  const int behind = m_ScrollDirection > 0 ? first - 1 : last + 1;
  if (behind >= 0 && behind < count) wanted.push_back(behind);
  const auto stale = [this](int page) {
    const auto cached = m_StripCache.find(page);
    if (cached == m_StripCache.end()) return true;
    const QSize px = RenderPx(page);
    const QSize full = m_StripFull.count(page) ? m_StripFull[page] : cached->second.size();
    if (full != px) return true;
    if (!Windowed(px)) return false;
    return !QRect(m_StripOrigin[page], cached->second.size()).contains(NeededRegion(page));
  };
  if (m_StripWork.valid()) {
    const bool stillWanted = std::find(wanted.begin(), wanted.end(), m_StripWorkPage) != wanted.end() && stale(m_StripWorkPage);
    if (!stillWanted) m_StripCancel = true; // its result is dropped; the poll starts the next page
    m_StripPoll->start();
    return;
  }
  for (const int page : wanted) {
    if (!stale(page)) continue;
    const QSize px = RenderPx(page);
    const QRect region = NeededRegion(page);
    m_StripCancel = false;
    m_StripWorkPage = page;
    pdfview::PdfDocument* document = m_Document.get();
    m_StripWork = std::async(std::launch::async, [this, document, page, px, region] {
      StripResult result;
      result.page = page; result.w = px.width(); result.h = px.height(); result.x = region.x(); result.y = region.y();
      try {
        result.ok = Windowed(px)
            ? document->RenderRegion(page, {px.width(), px.height()}, {region.x(), region.y(), region.width(), region.height()},
                                     result.bitmap, [this] { return m_StripCancel.load(); })
            : document->RenderPage(page, px.width(), px.height(), result.bitmap, [this] { return m_StripCancel.load(); });
      }
      catch (...) { result.ok = false; } // the page keeps its blank sheet
      return result;
    });
    m_StripPoll->start();
    return;
  }
}

void PdfWindow::PollStrip() {
  if (m_StripWork.valid()) {
    if (m_StripWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    StripResult result;
    try { result = m_StripWork.get(); }
    catch (const std::exception& error) { qWarning() << "PDF page render failed:" << error.what(); }
    m_StripWork = {};
    m_StripWorkPage = -1;
    if (m_Layout == Layout::Continuous && m_Document && result.ok && result.bitmap.w > 0 && result.page >= 0 &&
        result.page < static_cast<int>(m_PageTop.size()) && RenderPx(result.page) == QSize(result.w, result.h)) {
      QImage image(result.bitmap.bgra.data(), result.bitmap.w, result.bitmap.h, static_cast<qsizetype>(result.bitmap.w) * 4, QImage::Format_ARGB32);
      m_StripCache[result.page] = image.copy();
      m_StripFull[result.page] = QSize(result.w, result.h);
      m_StripOrigin[result.page] = Windowed(QSize(result.w, result.h)) ? QPoint(result.x, result.y) : QPoint();
      m_Page->update(PageRect(result.page));
      // Bounded cache: drop the page farthest from the view until it fits; pages in view are never dropped.
      const auto [first, last] = VisiblePages();
      std::size_t total = 0;
      for (const auto& entry : m_StripCache) total += static_cast<std::size_t>(entry.second.sizeInBytes());
      while (total > kStripCacheBytes) {
        int farthest = -1, farthestDistance = -1;
        for (const auto& entry : m_StripCache) {
          if (entry.first >= first && entry.first <= last) continue;
          const int distance = entry.first < first ? first - entry.first : entry.first - last;
          if (distance > farthestDistance) { farthest = entry.first; farthestDistance = distance; }
        }
        if (farthest < 0) break;
        total -= static_cast<std::size_t>(m_StripCache[farthest].sizeInBytes());
        m_StripCache.erase(farthest); m_StripFull.erase(farthest); m_StripOrigin.erase(farthest);
      }
    }
  }
  if (!m_StripWork.valid()) {
    ScheduleStrip();
    if (!m_StripWork.valid()) m_StripPoll->stop();
  }
}

void PdfWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  UpdateStatusBarLayout();
  QTimer::singleShot(0, this, [this] { UpdateToolStatus(); });
  if (m_Layout == Layout::Continuous && m_Document && m_ResizeTimer) m_ResizeTimer->start(); // re-fit once the resize settles
}

// Pointer routing for the continuous layout: the page under the pointer (or the one a press/pick started on) is the
// active page, and the point handed on is in that page's own pixels. With one page shown nothing changes.
int PdfWindow::InputPageFor(QPointF canvasPoint, bool buttonsDown) const {
  if (m_Layout != Layout::Continuous || !m_Document || m_PageTop.empty()) return -1;
  if (buttonsDown && m_InputPage >= 0) return m_InputPage;
  if (!m_Draft.empty() && m_DraftPage >= 0) return m_DraftPage;
  return PageAtY(qRound(canvasPoint.y()));
}

void PdfWindow::DispatchInput(QPointF canvasPoint, bool buttonsDown, const std::function<void(QPointF)>& handler) {
  const int page = InputPageFor(canvasPoint, buttonsDown);
  const QPointF local = page >= 0 ? canvasPoint - PageRect(page).topLeft() : canvasPoint;
  PageScope scope(*this, page);
  handler(local);
}

void PdfWindow::RefreshSnap() {
  PageScope scope(*this, m_Layout == Layout::Continuous && m_PointerPage >= 0 ? m_PointerPage : -1);
  UpdateSnap(m_LastPointer);
}

bool PdfWindow::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == m_Page || (m_Scroll && watched == m_Scroll->viewport())) && HandleViewportEvent(watched, event)) return true;
  return QMainWindow::eventFilter(watched, event);
}

// Ctrl+wheel zooms around the pointer; the middle button drags the document. Both work over the page and over the
// margin around it, and the middle button never reaches the annotation or measurement tools.
bool PdfWindow::HandleViewportEvent(QObject* watched, QEvent* event) {
  const auto endPan = [this] {
    m_Panning = false;
    if (m_ExtractMode) m_Page->setCursor(Qt::CrossCursor); else m_Page->unsetCursor();
    m_Scroll->viewport()->unsetCursor();
  };
  switch (event->type()) {
  case QEvent::Wheel: {
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (!m_Document) return false;
    if (!(wheel->modifiers() & Qt::ControlModifier)) {
      if (m_Layout != Layout::Continuous) return false; // one page: the scroll area's own wheel handling
      // Continuous layout: the wheel scrolls through the document (three lines a notch, or the touchpad's pixels).
      auto* bar = m_Scroll->verticalScrollBar();
      const int step = !wheel->pixelDelta().isNull() ? -wheel->pixelDelta().y()
                                                     : qRound(-wheel->angleDelta().y() / 120.0 * 3 * std::max(bar->singleStep(), 13));
      bar->setValue(bar->value() + step);
      wheel->accept();
      return true;
    }
    const int notches = wheel->angleDelta().y();
    if (notches == 0) return true;
    const QPoint at = wheel->position().toPoint();
    const QPoint viewportPoint = watched == m_Page ? m_Page->mapTo(m_Scroll->viewport(), at) : at;
    ZoomBy(std::pow(1.2, notches / 120.0), viewportPoint);
    wheel->accept();
    return true;
  }
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonDblClick: {
    auto* mouse = static_cast<QMouseEvent*>(event);
    // The middle button always pans; the left button pans too while the Browse tool is active.
    const bool browseDrag = mouse->button() == Qt::LeftButton && m_Tool == -1 && m_Document && !m_ExtractMode && !m_ExtractDragging && !m_ComparePicking;
    if (mouse->button() != Qt::MiddleButton && !browseDrag) return false;
    if (event->type() == QEvent::MouseButtonPress && m_Document) {
      m_Panning = true;
      m_PanButton = mouse->button();
      m_PanLast = mouse->globalPosition().toPoint();
      m_Page->setCursor(Qt::ClosedHandCursor);
      m_Scroll->viewport()->setCursor(Qt::ClosedHandCursor);
    }
    return true;
  }
  case QEvent::MouseMove: {
    if (!m_Panning) return false;
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (!(mouse->buttons() & m_PanButton)) { endPan(); return false; }
    const QPoint now = mouse->globalPosition().toPoint();
    const QPoint delta = now - m_PanLast;
    m_PanLast = now;
    m_Scroll->horizontalScrollBar()->setValue(m_Scroll->horizontalScrollBar()->value() - delta.x());
    m_Scroll->verticalScrollBar()->setValue(m_Scroll->verticalScrollBar()->value() - delta.y());
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != m_PanButton || !m_Panning) return mouse->button() == Qt::MiddleButton;
    endPan();
    return true;
  }
  default: return false;
  }
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
    m_ExtractMode = false; m_ExtractDragging = false; m_ExtractBox = false; UpdateToolStatus(); m_Page->unsetCursor(); m_Page->update();
    statusBar()->showMessage("Coordinate extraction cancelled.");
  }
  else if (m_ExtractBox) { m_ExtractBox = false; m_Page->update(); } // Esc clears the source box left up for review
  else if (IsSearching()) CancelSearch(false); // the poll reports "Search cancelled"
  else if (m_Tool >= 0 || !m_Draft.empty()) EscapeTool(); // REQ-430: a pick in progress, then the tool
  else if (m_SelectedAnnotation >= 0) { ClearAnnotationSelection(); m_Page->update(); } // Select: Esc lets go of the selection
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
  if (!m_Document || !PageReady() || m_Hits.empty()) return;
  const int page = ActivePage();
  const auto size = m_Document->Sizes()[page];
  const double scaleX = static_cast<double>(PageWidthPx()) / size.wPt;
  const double scaleY = static_cast<double>(PageHeightPx()) / size.hPt;
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
      m_AuditPage != ActivePage() || m_AuditSelected < 0 ||
      m_AuditSelected >= static_cast<int>(m_AuditMatches.matches.size()) || !PageReady()) return;
  const auto& match = m_AuditMatches.matches[static_cast<std::size_t>(m_AuditSelected)];
  const auto size = m_Document->Sizes()[m_AuditPage];
  const double factor = static_cast<double>(PageWidthPx()) / std::max(1.0, static_cast<double>(size.wPt));
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
