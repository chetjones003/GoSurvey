#pragma once
#include <QMainWindow>
#include "PdfDocument.hpp"
#include "PdfAnnotate.hpp"
#include <QString>
#include <atomic>
#include <future>
class PdfCanvas;
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
  std::map<int, pdfview::PageScale> m_Scales;
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
  void EditScale();
  void SaveAs();
  void SplitPages();
  bool ConfirmDiscard();
  pdfview::PageScale CurrentScale() const;
};
