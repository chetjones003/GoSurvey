// REQ-391 - how the Qt PDF window paints a dimension: the strokes and label the saved file gets (shared through
// pdfview::DimensionStrokes / DimensionLabel*), so the page on screen and the exported PDF agree.
#include "PdfWindow.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>
#include <QStringList>
#include <algorithm>

void PdfWindow::PaintDimension(QPainter& painter, const pdfview::Annot& a,
                               const std::function<QPointF(float, float)>& toCanvas, double pixelsPerPoint) const {
  painter.save();
  for (const auto& stroke : pdfview::DimensionStrokes(a)) {
    QPolygonF polygon;
    for (const auto& pt : stroke.pts) polygon << toCanvas(pt.first, pt.second);
    if (stroke.filled) { painter.setBrush(painter.pen().color()); painter.drawPolygon(polygon); }
    else { painter.setBrush(Qt::NoBrush); painter.drawPolyline(polygon); }
  }
  const QString label = QString::fromUtf8(pdfview::DimensionLabel(a, CurrentScale()).c_str());
  QFont font(QString::fromStdString(a.font));
  font.setPixelSize(std::max(1, qRound(std::max(4.f, a.fontSize) * pixelsPerPoint)));
  painter.setFont(font);
  const auto anchor = pdfview::DimensionLabelAnchor(a);
  painter.translate(toCanvas(anchor.first, anchor.second));
  painter.rotate(-pdfview::DimensionLabelAngleDeg(a));  // page y is up, the screen's is down
  const QFontMetricsF metrics(font);
  const QStringList lines = label.split(QLatin1Char('\n'));
  double y = -metrics.height() * lines.size() * 0.5;
  for (const QString& line : lines) {
    const double width = metrics.horizontalAdvance(line);
    painter.drawText(QRectF(-width * 0.5 - 2, y, width + 4, metrics.height()), Qt::AlignCenter, line);
    y += metrics.height();
  }
  painter.restore();
}
