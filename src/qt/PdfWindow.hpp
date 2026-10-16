#pragma once
#include <QMainWindow>
#include "PdfDocument.hpp"
#include <atomic>
#include <future>
class QLabel;
class QSpinBox;
class QScrollArea;
class QTimer;

class PdfWindow final : public QMainWindow {
public:
  explicit PdfWindow(QWidget* parent);
  ~PdfWindow() override;
  void Open(const QString& path);
private:
  void RequestRender();
  void PollRender();
  std::unique_ptr<pdfview::PdfDocument> m_Document;
  std::future<pdfview::Bitmap> m_Work;
  std::atomic<bool> m_Cancel{false};
  QLabel* m_Page;
  QSpinBox* m_PageNumber;
  QScrollArea* m_Scroll;
  QTimer* m_Poll;
  double m_Zoom = 1.0;
  bool m_Pending = false;
};
