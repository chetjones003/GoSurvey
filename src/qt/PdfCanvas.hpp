#pragma once
// The PDF page surface: a bitmap with an overlay painted over it, and the pointer events the window needs.
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <functional>

class PdfCanvas final : public QWidget {
public:
  explicit PdfCanvas(QWidget* parent) : QWidget(parent) { setObjectName("pdfCanvas"); resize(250, 100); setMouseTracking(true); setFocusPolicy(Qt::ClickFocus); }
  QImage image;
  QPoint imageOrigin;  ///< where the picture sits in a page imageFull big
  QSize imageFull;     ///< empty: the picture is the whole page
  QString placeholder = "Open a PDF to begin"; ///< drawn only while there is no picture and no document
  std::function<void(QPointF, Qt::MouseButton)> clicked;
  std::function<void()> doubleClicked;
  std::function<void(QPointF)> moved;   ///< REQ-430: the pointer moved (snap marker)
  std::function<void()> left;           ///< the pointer left the page
  std::function<void(QPointF, Qt::MouseButton)> pressed;  ///< coordinate extraction: drag start
  std::function<void(QPointF, Qt::MouseButton)> released; ///< coordinate extraction: drag end
  std::function<void(QPainter&)> overlay;
  /// Continuous layout: paints every page that meets the exposed rectangle itself and returns true.
  std::function<bool(QPainter&, const QRect&)> paintStrip;
protected:
  void paintEvent(QPaintEvent* event) override {
    QPainter painter(this);
    if (paintStrip && paintStrip(painter, event->rect())) return;
    if (image.isNull()) { if (!placeholder.isEmpty()) painter.drawText(rect(), Qt::AlignCenter, placeholder); return; }
    // A picture of another zoom is stretched to the page's current size until the new render arrives, so zooming
    // never blanks the page.
    // A picture covers the whole page or, when the page is too large to draw whole, only the window of it around the
    // view (imageOrigin, in the pixels of a page imageFull big); it is scaled to the page's current size.
    const QSize full = imageFull.isEmpty() ? image.size() : imageFull;
    const double factor = static_cast<double>(width()) / std::max(1, full.width());
    if (!imageFull.isEmpty()) painter.fillRect(rect(), Qt::white);
    const QRectF target(imageOrigin.x() * factor, imageOrigin.y() * factor, image.width() * factor, image.height() * factor);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, factor != 1.0);
    painter.drawImage(target, image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (overlay) overlay(painter);
  }
  void mousePressEvent(QMouseEvent* event) override {
    if (pressed) pressed(event->position(), event->button());
    if (clicked) clicked(event->position(), event->button());
  }
  void mouseReleaseEvent(QMouseEvent* event) override { if (released) released(event->position(), event->button()); }
  void mouseDoubleClickEvent(QMouseEvent*) override { if (doubleClicked) doubleClicked(); }
  void mouseMoveEvent(QMouseEvent* event) override { if (moved) moved(event->position()); }
  void leaveEvent(QEvent*) override { if (left) left(); }
};
