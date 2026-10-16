#include "PdfWindow.hpp"
#include "EditorWindow.hpp"
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

PdfWindow::PdfWindow(QWidget* parent) : QMainWindow(parent, Qt::Window) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("GoSurvey | PDF viewer");
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
  connect(m_PageNumber, &QSpinBox::valueChanged, this, [this] { RequestRender(); });
  tools->addAction("Previous", this, [this] { m_PageNumber->setValue(m_PageNumber->value() - 1); });
  tools->addAction("Next", this, [this] { m_PageNumber->setValue(m_PageNumber->value() + 1); });
  tools->addSeparator();
  tools->addAction("Zoom in", this, [this] { m_Zoom = std::min(4.0, m_Zoom * 1.25); RequestRender(); });
  tools->addAction("Zoom out", this, [this] { m_Zoom = std::max(0.1, m_Zoom / 1.25); RequestRender(); });
  tools->addAction("Fit width", this, [this] { m_Zoom = 1.0; RequestRender(); });
  m_Scroll = new QScrollArea(this);
  m_Scroll->setAlignment(Qt::AlignCenter);
  m_Page = new QLabel("Open a PDF to begin", m_Scroll);
  m_Page->setAlignment(Qt::AlignCenter);
  m_Scroll->setWidget(m_Page);
  setCentralWidget(m_Scroll);
  m_Poll = new QTimer(this);
  m_Poll->setInterval(30);
  connect(m_Poll, &QTimer::timeout, this, [this] { PollRender(); });
}

PdfWindow::~PdfWindow() {
  m_Cancel = true;
  if (m_Work.valid()) m_Work.wait(); // Document outlives every use on the worker.
}

void PdfWindow::Open(const QString& path) {
  if (m_Work.valid()) {
    statusBar()->showMessage("Wait for the current page to finish before opening another PDF.");
    return;
  }
  auto result = pdfview::PdfDocument::Open(std::filesystem::path(path.toStdWString()));
  if (!result.doc) { QMessageBox::warning(this, "Open PDF", QString::fromUtf8(result.error.c_str())); return; }
  m_Document = std::move(result.doc);
  m_PageNumber->setRange(1, m_Document->PageCount());
  m_PageNumber->setValue(1);
  setWindowTitle("GoSurvey | " + path);
  RequestRender();
}

void PdfWindow::RequestRender() {
  if (!m_Document) return;
  if (m_Work.valid()) { m_Pending = true; m_Cancel = true; return; }
  const int page = m_PageNumber->value() - 1;
  const auto& size = m_Document->Sizes()[page];
  const int w = std::clamp(qRound(std::max(200, m_Scroll->viewport()->width() - 30) * m_Zoom), 64, 4096);
  const int h = std::clamp(qRound(w * size.hPt / size.wPt), 64, 8192);
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
      QImage image(bitmap.bgra.data(), bitmap.w, bitmap.h, bitmap.w * 4, QImage::Format_ARGB32);
      m_Page->setPixmap(QPixmap::fromImage(image.copy()));
      m_Page->resize(bitmap.w, bitmap.h);
      statusBar()->showMessage(QString("Page %1 of %2").arg(m_PageNumber->value()).arg(m_Document->PageCount()));
    } else if (!m_Pending) statusBar()->showMessage("The page could not be rendered.");
  } catch (const std::exception& error) {
    statusBar()->showMessage(QString::fromUtf8(error.what()));
  }
  if (m_Pending) { m_Pending = false; RequestRender(); }
}
