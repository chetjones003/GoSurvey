#pragma once
// The PDF page surface: a bitmap with an overlay painted over it, and the pointer events the window needs.
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <functional>

class PdfCanvas final : public QWidget {
public:
  explicit PdfCanvas(QWidget* parent) : QWidget(parent) { setObjectName("pdfCanvas"); resize(250, 100); setMouseTracking(true); }
  QImage image;
  std::function<void(QPointF, Qt::MouseButton)> clicked;
  std::function<void(QPointF)> moved;   ///< REQ-430: the pointer moved (snap marker)
  std::function<void()> left;           ///< the pointer left the page
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
  void mouseMoveEvent(QMouseEvent* event) override { if (moved) moved(event->position()); }
  void leaveEvent(QEvent*) override { if (left) left(); }
};
