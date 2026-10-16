#pragma once
#include <QMainWindow>
#include "PdfDocument.hpp"
#include "PdfAnnotate.hpp"
#include <atomic>
#include <future>
class PdfCanvas;
class QPainter;
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
  void PageClick(QPointF point, Qt::MouseButton button);
  void DrawAnnotations(QPainter& painter);
  void EditScale();
  void SaveAs();
  void SplitPages();
  bool ConfirmDiscard();
  pdfview::PageScale CurrentScale() const;
};
