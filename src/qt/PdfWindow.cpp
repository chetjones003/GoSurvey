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

PdfWindow::PdfWindow(QWidget* parent) : QMainWindow(parent, Qt::Window) {
  setObjectName("pdfViewerWindow");
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
  connect(m_PageNumber, &QSpinBox::valueChanged, this, [this] { m_Draft.clear(); RequestRender(); });
  tools->addAction("Previous", this, [this] { m_PageNumber->setValue(m_PageNumber->value() - 1); });
  tools->addAction("Next", this, [this] { m_PageNumber->setValue(m_PageNumber->value() + 1); });
  tools->addSeparator();
  tools->addAction("Zoom in", this, [this] { m_Zoom = std::min(4.0, m_Zoom * 1.25); RequestRender(); });
  tools->addAction("Zoom out", this, [this] { m_Zoom = std::max(0.1, m_Zoom / 1.25); RequestRender(); });
  tools->addAction("Fit width", this, [this] { m_Zoom = 1.0; RequestRender(); });
  tools->addAction("Save As...", this, [this] { SaveAs(); });
  tools->addAction("Extract pages...", this, [this] { SplitPages(); });
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
  m_Page->overlay = [this](QPainter& painter) { DrawAnnotations(painter); };
  m_Scroll->setWidget(m_Page);
  setCentralWidget(m_Scroll);
  m_Poll = new QTimer(this);
  m_Poll->setInterval(30);
  connect(m_Poll, &QTimer::timeout, this, [this] { PollRender(); });
}

PdfWindow::~PdfWindow() {
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
  m_Document = std::move(result.doc);
  m_Annotations = {}; m_Draft.clear();
  m_Scales = pdfview::ReadPageScales(m_Document->Path()).scales;
  const QSignalBlocker pageBlock(m_PageNumber);
  m_PageNumber->setRange(1, m_Document->PageCount());
  m_PageNumber->setValue(1);
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
  m_Cancel = true; event->accept();
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
