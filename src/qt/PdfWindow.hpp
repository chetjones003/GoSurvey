#pragma once
#include <QMainWindow>
#include "PdfDocument.hpp"
#include "PdfAnnotate.hpp"
#include "PdfSnap.hpp"
#include <QPointF>
#include <QString>
#include <atomic>
#include <future>
#include <memory>
class PdfCanvas;
class PdfScaleDialogs;
class QAction;
class QActionGroup;
class QPushButton;
class PdfThumbnails;
class QCheckBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListView;
class QPainter;
class QToolBar;
class QSpinBox;
class QScrollArea;
class QTimer;

class PdfWindow final : public QMainWindow {
public:
  explicit PdfWindow(QWidget* parent);
  ~PdfWindow() override;
  void Open(const QString& path);
  bool LoadFile(const QString& path, QString& error);
  bool SaveFile(const QString& path, QString& error);
  bool IsBusy() const { return m_Work.valid(); }
  bool HasUnsavedChanges() const { return m_Annotations.Dirty(); }
  bool AddAnnotation(const pdfview::Annot& annotation, QString& error);
  bool IsSearching() const { return m_Search.valid(); }
  int ThumbnailsKept() const;
  /// REQ-430: the scale in force on a zero-based page (the session's change, else the file's); null = unscaled.
  const pdfview::PageScale* EffectiveScale(int page) const;
  bool SnapEnabled() const { return m_SnapOn; }
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
protected:
  void closeEvent(QCloseEvent* event) override;
private:
  void RequestRender();
  void PollRender();
  std::unique_ptr<pdfview::PdfDocument> m_Document;
  std::future<pdfview::Bitmap> m_Work;
  std::atomic<bool> m_Cancel{false};
  PdfCanvas* m_Page;
  QSpinBox* m_PageNumber;
  QScrollArea* m_Scroll;
  QTimer* m_Poll;
  double m_Zoom = 1.0;
  bool m_Pending = false;
  pdfview::AnnotSession m_Annotations;
  pdfview::ScaleRead m_FileScales; ///< what the opened file already says (REQ-390 clause 3)
  int m_Tool = -1;
  std::vector<std::pair<float, float>> m_Draft;
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
  void DrawAnnotations(QPainter& painter);
  void SaveAs();
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
  void BuildScaleToolbar();
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
  QPointF PageToCanvas(float x, float y) const;
  std::map<int, pdfview::PageScale> ScalesInForce() const;
  QActionGroup* m_ToolGroup = nullptr;
  QAction* m_SnapAction = nullptr;
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
};
