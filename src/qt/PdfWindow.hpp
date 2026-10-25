#pragma once
#include <QMainWindow>
#include "PdfDocument.hpp"
#include "PdfAnnotate.hpp"
#include "PdfSnap.hpp"
#include <QPointF>
#include "PdfCoordExtract.hpp"
#include "PdfAlign.hpp"
#include "PdfDiff.hpp"
#include <QString>
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>
#include <QImage>
#include <QPoint>
#include <QRect>
class PdfCanvas;
class PdfScaleDialogs;
class QAction;
class QActionGroup;
class QPushButton;
class PdfThumbnails;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListView;
class QPainter;
class QToolBar;
class QSpinBox;
class QScrollArea;
class QScrollBar;
class QTimer;
class QDialog;
class QSlider;
class QDoubleSpinBox;
class QProgressBar;
class QTableWidget;
class QTabWidget;
class QStackedWidget;
class QEvent;
class QResizeEvent;
class EditorWindow;

class PdfWindow final : public QMainWindow {
public:
  explicit PdfWindow(QWidget* parent);
  ~PdfWindow() override;
  void Open(const QString& path);
  bool LoadFile(const QString& path, QString& error);
  bool SaveFile(const QString& path, QString& error);
  bool IsBusy() const { return m_Work.valid() || m_OpenWork.valid(); }
  bool IsSaving() const { return m_SaveWork.valid(); }
  bool HasUnsavedChanges() const { return m_Annotations.Dirty(); }
  bool AddAnnotation(const pdfview::Annot& annotation, QString& error);
  bool IsSearching() const { return m_Search.valid(); }
  int ThumbnailsKept() const;
  /// REQ-430: the scale in force on a zero-based page (the session's change, else the file's); null = unscaled.
  const pdfview::PageScale* EffectiveScale(int page) const;
  bool SnapEnabled() const { return m_SnapOn; }
  void ShowScaleAudit();
  /// The Set scale dialog's remembered entries.
  struct ScaleForm {
    int mode = 0;       ///< 0 preset, 1 typed ratio
    int preset = 8;     ///< index into PresetScales()
    double page = 1.0, real = 20.0;
    int pageUnit = 0, realUnit = 1;
    int scope = 0;      ///< 0 this page, 1 every page, 2 chosen pages
    QString scopeText;
    double calReal = 10.0; ///< the Known distance box
    int calUnit = 1;
  };
  void SetEditor(EditorWindow* editor) { m_Editor = editor; }
  /// REQ-387 clause 2 / REQ-455: page-by-page (default) or continuous vertical scrolling through every page.
  enum class Layout { PageByPage, Continuous };
  enum class CompareMode { Tint, Opacity, Base, Revision }; ///< the order of the Compare panel's View box
  void SetLayout(Layout layout);
  Layout CurrentLayout() const { return m_Layout; }
  /// The zero-based page the controls and the thumbnail strip are on (the page at the middle of the view when continuous).
  int CurrentPage() const;
  void ScrollToPage(int page);
  /// The displayed width of the current page as a percentage of its actual size (100% = 96 dpi).
  double ZoomPercent() const;
  void SetZoomPercent(double percent);
  void FitWidth();
  void FitPage();
  /// Zoom by a factor keeping the page point under a point of the scroll area's viewport fixed.
  void ZoomBy(double factor, QPoint viewportPoint);
protected:
  void closeEvent(QCloseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
private:
  friend class QtEditorTests;
  struct SaveResult { bool succeeded = false; std::string error; };
  void RequestRender(bool keepPicture = false);
  void PollRender();
  // REQ-387 / REQ-455: layout, zoom and the continuous strip. Page rectangles are in canvas coordinates.
  struct StripResult { int page = -1, w = 0, h = 0, x = 0, y = 0; bool ok = false; pdfview::Bitmap bitmap; };
  static bool Windowed(QSize full);                 ///< too big to render whole: only the window around the view is drawn
  QRect NeededRegion(int page) const;               ///< the part of page \p page (in its pixels) the view needs drawn
  bool PageWindowCovers() const;                    ///< page-by-page: does the picture cover the view?
  QRect m_WorkRegion; QSize m_WorkFull;             ///< what the page-by-page render in flight is drawing
  std::map<int, QSize> m_StripFull;                 ///< continuous: the page size each cached picture was drawn for
  std::map<int, QPoint> m_StripOrigin;              ///< ... and where in that page it sits
  struct ZoomAnchor { int page = 0; double fx = 0, fy = 0; };
  QSize PagePx(int page) const;
  QSize RenderPx(int page) const;
  double WidthAtZoomOne(int page) const;
  QRect PageRect(int page) const;
  int PageAtY(int y) const;
  void RebuildLayout();
  void ApplyScrollRange(QScrollBar* bar, int canvasLength, int viewLength);
  ZoomAnchor CaptureAnchor(QPoint viewportPoint) const;
  void RestoreAnchor(const ZoomAnchor& anchor, QPoint viewportPoint);
  void SetZoom(double zoom, QPoint viewportPoint);
  void UpdateZoomUi();
  void CenterOnPagePoint(int page, double xPt, double yPt);
  bool PaintStrip(QPainter& painter, const QRect& exposed);
  void ScheduleStrip();
  void PollStrip();
  void CancelStrip(bool wait);
  void OnScrolled();
  std::pair<int, int> VisiblePages() const;
  bool HandleViewportEvent(QObject* watched, QEvent* event);
  // The page the input or the overlay being handled belongs to: the current page unless the continuous layout
  // routed the pointer (or the painter) to another one. Everything that used "the" page uses these.
  int ActivePage() const;
  int PageWidthPxOf(int page) const;
  int PageHeightPxOf(int page) const;
  int PageWidthPx() const { return PageWidthPxOf(ActivePage()); }
  int PageHeightPx() const { return PageHeightPxOf(ActivePage()); }
  bool PageReady() const;
  struct PageScope {
    PdfWindow& window; int saved;
    PageScope(PdfWindow& w, int page) : window(w), saved(w.m_CtxPage) { window.m_CtxPage = page; }
    ~PageScope() { window.m_CtxPage = saved; }
  };
  int InputPageFor(QPointF canvasPoint, bool buttonsDown) const;
  void DispatchInput(QPointF canvasPoint, bool buttonsDown, const std::function<void(QPointF)>& handler);
  void RefreshSnap();
  Layout m_Layout = Layout::PageByPage;
  std::vector<int> m_PageTop, m_PageWpx, m_PageHpx; ///< continuous layout, one entry per page
  double m_MaxPageWidthPt = 1.0;
  int m_CtxPage = -1;     ///< see ActivePage()
  int m_InputPage = -1;   ///< the page a pointer press landed on, until the release
  int m_DraftPage = -1;   ///< the page the picks in m_Draft belong to
  int m_PointerPage = -1; ///< the page the pointer (snap marker, rubber band) is over
  int m_ExtractPage = -1;
  static constexpr int kPageGap = 10;
  static constexpr int kReadAhead = 3;                          ///< pages rendered ahead of the scroll direction
  static constexpr std::size_t kStripCacheBytes = 256u << 20;   ///< bounded page-image cache (REQ-387 clause 4)
  std::map<int, QImage> m_StripCache;
  std::future<StripResult> m_StripWork;
  int m_StripWorkPage = -1;
  std::atomic<bool> m_StripCancel{false};
  QTimer* m_StripPoll = nullptr;
  QTimer* m_ResizeTimer = nullptr;
  QTimer* m_ZoomSettle = nullptr; ///< renders at the new zoom once the wheel/buttons stop
  int m_ScrollDirection = 1, m_LastScrollY = 0;
  bool m_ProgrammaticScroll = false;
  bool m_SyncingPage = false;
  bool m_CloudPolygon = false; ///< the revision-cloud button's mode: false = rectangle (two clicks), true = polygon (Enter)
  bool m_Panning = false;
  Qt::MouseButton m_PanButton = Qt::MiddleButton; ///< the button that started the pan: middle, or left under Browse
  QPoint m_PanLast;
  QComboBox* m_LayoutBox = nullptr;
  QSpinBox* m_ZoomBox = nullptr;
  QTabWidget* m_CommandTabs = nullptr;
  QStackedWidget* m_SidePages = nullptr;
  QLabel* m_StatusPageCount = nullptr;
  QLabel* m_ToolStatus = nullptr;  ///< bottom bar: the active tool and how to leave it
  void UpdateToolStatus();
  void SyncPageNav();
  void UpdatePropertiesPanel();
  void PollOpen();
  bool AdoptDocument(std::unique_ptr<pdfview::PdfDocument> document, const QString& path, QString& error);
  std::unique_ptr<pdfview::PdfDocument> m_Document;
  std::future<pdfview::Bitmap> m_Work;
  std::future<pdfview::PdfDocument::OpenResult> m_OpenWork;
  QString m_OpenPath;
  QTimer* m_OpenPoll = nullptr;
  std::future<SaveResult> m_SaveWork;
  QString m_SaveDestination;
  uint64_t m_Revision = 0;
  uint64_t m_SaveRevision = 0;
  QTimer* m_SavePoll = nullptr;
  std::atomic<bool> m_Cancel{false};
  PdfCanvas* m_Page;
  QSpinBox* m_PageNumber;
  QScrollArea* m_Scroll;
  QTimer* m_Poll;
  double m_Zoom = 1.0;
  bool m_Pending = false;
  pdfview::AnnotSession m_Annotations;
  pdfview::ScaleRead m_FileScales; ///< what the opened file already says (REQ-390 clause 3)
  int m_Tool = -2;
  std::vector<std::pair<float, float>> m_Draft;
  int m_SelectedAnnotation = -1;
  std::vector<int> m_SelectedMore; ///< the other marks a box select picked; m_SelectedAnnotation is the last one
  bool m_AnnotationDragging = false;
  int m_AnnotationHandle = -1;
  QPointF m_DragStart;
  pdfview::Annot m_DragOriginal;
  std::optional<pdfview::Annot> m_DragPreview;
  unsigned m_AnnotationColor = 0xFF0000;
  float m_AnnotationThickness = 1.f;
  float m_AnnotationFontSize = 12.f;
  bool m_AnnotationFill = false;
  std::string m_AnnotationFont = "Helvetica";
  bool m_AnnotationBold = false;
  bool m_AnnotationItalic = false;
  EditorWindow* m_Editor = nullptr;
  bool m_ExtractMode = false;
  bool m_ExtractBox = false;  ///< the yellow source box stays up while its candidates are under review
  bool m_ExtractDragging = false;
  QPointF m_ExtractStart;
  QPointF m_ExtractEnd;
  std::optional<pdfview::ExtractResult> m_ExtractResult;
  QDialog* m_ExtractReview = nullptr;
  // REQ-429: the find bar. Results are read-only: nothing here touches the annotation session.
  struct SearchOutcome {
    bool ok = false;
    pdfview::TextSearch result;
  };
  void SyncThumbnail();
  void ShowFindBar();
  void CloseFindBar();
  void StartSearch();
  void PollSearch();
  void CancelSearch(bool wait);
  void StepHit(int direction);
  void ShowHit(int index);
  void ClearSearch();
  void EscapePressed();
  void DrawHighlights(QPainter& painter);
  QToolBar* m_FindBar = nullptr;
  QLineEdit* m_FindEdit = nullptr;
  QCheckBox* m_FindMatchCase = nullptr;
  QLabel* m_FindStatus = nullptr;
  QTimer* m_SearchPoll = nullptr;
  std::future<SearchOutcome> m_Search;
  std::atomic<bool> m_SearchCancel{false};
  bool m_SearchRestart = false;
  std::u16string m_SearchedQuery;
  bool m_SearchedCase = false;
  std::vector<pdfview::TextHit> m_Hits;
  bool m_HitsTruncated = false;
  int m_HitIndex = -1;
  // REQ-387 clause 2 / REQ-429: the thumbnail strip.
  QDockWidget* m_ThumbDock = nullptr;
  QListView* m_ThumbView = nullptr;
  PdfThumbnails* m_Thumbs = nullptr;
  void PageClick(QPointF point, Qt::MouseButton button);
  void EditSelectedAnnotation();
  void MoveAnnotation(QPointF point);
  void EndAnnotation(QPointF point, Qt::MouseButton button);
  QPointF CanvasToPage(QPointF point) const;
  QPointF PageToCanvas(float x, float y) const;
  int HitAnnotation(QPointF pagePoint, float tolerance) const;
  void ApplyAnnotationStyle(pdfview::Annot& annotation) const;
  void SelectInBox(QPointF first, QPointF second);
  bool IsSelected(int index) const;
  static void FitLeaderBox(pdfview::Annot& annotation); ///< a callout's box follows its text
  void DrawAnnotations(QPainter& painter);
  void DrawExtractOverlay(QPainter& painter);
  void BeginExtract(QPointF point, Qt::MouseButton button);
  void MoveExtract(QPointF point);
  void EndExtract(QPointF point, Qt::MouseButton button);
  void ExtractRegion();
  void ShowExtractReview();
  void DecideExtractCandidate(int row, bool accept);
  void SaveAs();
  void StartSaveAs(const QString& path);
  void PollSave();
  void SplitPages();
  bool ConfirmDiscard();
  pdfview::PageScale CurrentScale() const;

  // REQ-430: scale display, calibration, scale checks and snapping (src/qt/PdfScaleUi.cpp).
  friend class PdfScaleDialogs;
  static constexpr int kToolCalibrate = 20; ///< m_Tool values beyond the annotation kinds
  static constexpr int kToolCheck = 21;
  struct SnapPage {
    std::future<std::shared_ptr<pdfview::SnapIndex>> reading;
    std::shared_ptr<pdfview::SnapIndex> index; ///< null until read, or if reading failed
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
    bool started = false;
    bool failed = false;
  };
  void BuildScaleControls();
  void SetTool(int tool, const QString& status);
  void ClearPending();
  void EscapeTool();
  void UpdateScaleDisplay();
  void SceneChanged();                         ///< after anything the scale display or overlay depends on
  void ShowSetScale();
  void BeginCalibrate();
  void FinishCalibrationPicks();
  void FinishCheckPicks();
  void ShowChecks(int tab);
  bool ApplyScale(const pdfview::PageScale& scale, const ScaleForm& form, const pdfview::ScaleCheck* calibration, QString& error);
  bool ScopePages(const ScaleForm& form, std::vector<int>& pages, QString& error) const;
  void UpdateSnap(QPointF point);
  void ToggleSnap();
  void StartSnapRead(int page);
  void DrawScaleOverlay(QPainter& painter);
  std::map<int, pdfview::PageScale> ScalesInForce() const;
  QActionGroup* m_ToolGroup = nullptr;
  QAction* m_SnapAction = nullptr;
  QAction* m_SetScaleAction = nullptr;
  QLabel* m_ScaleLabel = nullptr;
  QLabel* m_SnapStatus = nullptr;
  QPushButton* m_ChecksButton = nullptr;
  QCheckBox* m_ShowChecksBox = nullptr;
  ScaleForm m_ScaleForm;
  pdfview::CheckLimits m_Limits;
  pdfview::RobustParams m_Robust;
  std::vector<std::pair<pdfview::ScaleCheck, bool>> m_RobustPicks; ///< robust calibration: dimension, and whether it is used
  int m_CheckTarget = 0;   ///< 0: a check on the page's scale; 1: a dimension for the robust calibration
  int m_CalPage = 0;
  bool m_CalSnapped[2] = {false, false};
  float m_CalX[2] = {0.f, 0.f}, m_CalY[2] = {0.f, 0.f};
  int m_SelectedCheck = -1;
  bool m_SnapOn = false;
  std::map<int, SnapPage> m_SnapPages;  ///< the last few pages read
  bool m_SnapHit = false;               ///< the marker is on a point of the page being shown
  float m_SnapX = 0.f, m_SnapY = 0.f;
  bool m_SnapPrevValid = false;
  float m_SnapPrevX = 0.f, m_SnapPrevY = 0.f;
  QTimer* m_SnapPoll = nullptr;
  QPointF m_LastPointer;                ///< the pointer on the canvas, for the snap marker and the check rubber band
  bool m_PointerIn = false;

  // REQ-395 Qt workflow. Reading and matching run away from the UI thread; the result is only a suggestion
  // until the user explicitly applies the consensus scale through the undoable annotation session.
  struct AuditOutcome {
    bool ok = false;
    pdfview::DimMatchSet matches;
  };
  std::future<AuditOutcome> m_AuditWork;
  std::atomic<bool> m_AuditCancel{false};
  QTimer* m_AuditPoll = nullptr;
  QDialog* m_AuditDialog = nullptr;
  QTableWidget* m_AuditTable = nullptr;
  QLabel* m_AuditSummary = nullptr;
  QCheckBox* m_AuditAllRows = nullptr;
  QCheckBox* m_AuditShowSheet = nullptr;
  int m_AuditSelected = -1;
  pdfview::DimAudit m_AuditResult;
  pdfview::DimMatchSet m_AuditMatches;
  int m_AuditPage = -1;
  bool m_AuditReady = false;
  std::vector<int> m_AuditRows;
  void StartScaleAudit();
  void PollScaleAudit();
  void RefreshScaleAudit();
  void DrawAuditOverlay(QPainter& painter);

  // REQ-392/393 Qt comparison session. All CPU rendering, alignment, and change detection run on a worker.
  struct CompareOutcome {
    bool ok = false;
    std::string error;
    std::unique_ptr<pdfview::PdfDocument> openedRevision;
    pdfview::Bitmap base, revision, aligned, tint, baseView, revisionView, marks, revMarks;
    pdfalign::AutoResult alignment;
    pdfalign::Transform automaticTransform;
    bool hasAlignment = false;
    pdfdiff::Result changes;
  };
  struct CompareSession {
    std::unique_ptr<pdfview::PdfDocument> revision;
    QString source;
    int basePage = 0, revisionPage = 0;
    pdfalign::Transform transform;
    pdfalign::Transform automatic;
    QImage overlay;
    QImage revMarks; ///< only the marks that are new in the revision, in red
    QImage marksOverlay; ///< only the differing marks, transparent elsewhere: drawn over the crisp page in Tint
    QImage revisionOverlay;
    QImage baseView;
    QImage revisionView;
    std::vector<pdfdiff::Region> changes;
    CompareMode mode = CompareMode::Tint;
    int opacity = 50;
    bool busy = false, aligned = false, haveChanges = false, lowConfidence = false;
    bool manualAlignment = false;
    pdfdiff::Settings settings;
    std::atomic<bool> cancel{false};
  };
  std::unique_ptr<CompareSession> m_Compare;
  std::future<CompareOutcome> m_CompareWork;
  // A sharp picture of the revision for the part of the page on screen (Revision and Opacity views at high zoom): the
  // fixed-resolution comparison images alone go soft once the page is shown much larger than they were made.
  struct CompareDetail { pdfview::Bitmap bitmap, marks, redMarks; QRect rect; int pageWidthPx = 0; double ratio = 1.0; bool failed = false; };
  std::future<CompareDetail> m_CompareDetailWork;
  std::atomic<bool> m_CompareDetailCancel{false};
  bool m_CompareDetailAgain = false;
  QTimer* m_CompareDetailTimer = nullptr;
  QTimer* m_CompareDetailPoll = nullptr;
  QImage m_CompareDetailImage;
  QImage m_CompareDetailMarks;    ///< the differing marks redrawn sharp for the visible part (blue base-only, red revision-only)
  QImage m_CompareDetailRedMarks; ///< only the red ones, for the Revision view
  QRect m_CompareDetailRect;
  int m_CompareDetailPageWidthPx = 0;
  void ClearCompareDetail();
  pdfview::PageSize BasePageSize() const { return m_Document->Sizes()[static_cast<std::size_t>(m_Compare->basePage)]; }
  void RequestCompareDetail();
  void PollCompareDetail();
  void StopCompareDetail();
  QTimer* m_ComparePoll = nullptr;
  QWidget* m_ComparePanel = nullptr; ///< the Compare tab of the side panel
  void ShowComparePanel();
  QComboBox* m_CompareMode = nullptr;
  QSlider* m_CompareOpacity = nullptr;
  QSpinBox* m_CompareBasePage = nullptr;
  QSpinBox* m_CompareRevisionPage = nullptr;
  QDoubleSpinBox* m_CompareTolerance = nullptr;
  QDoubleSpinBox* m_CompareMinRegion = nullptr;
  QCheckBox* m_CompareShowHighlights = nullptr;
  QLabel* m_CompareStatus = nullptr;
  QProgressBar* m_CompareProgress = nullptr;
  QTableWidget* m_CompareTable = nullptr;
  bool m_ComparePicking = false;
  bool m_CompareClosePending = false;
  int m_ComparePickCount = 0;
  int m_ComparePickTarget = 0;
  pdfalign::Pt m_ComparePicks[4];
  void BeginCompare();
  void StartCompare(bool findChanges);
  void PollCompare();
  void UpdateCompareOverlay();
  void DrawCompareOverlay(QPainter& painter);
  void ComparePick(QPointF point, Qt::MouseButton button);
  void SaveCompareChanges();
};
