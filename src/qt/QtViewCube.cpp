#include "QtViewCube.hpp"

#include "QtAccent.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace {

QPointF ToQt(const viewcube::Point& p) { return {p.x, p.y}; }

// Label centred on `at`, shrunk to fit `maxWidth` so BOTTOM / FRONT / RIGHT stay on their face.
void CenteredLabel(QPainter& painter, const QPointF& at, const char* text, const QColor& color, double maxWidth) {
  QFont font = painter.font();
  const QFontMetricsF natural(font);
  const double width = natural.horizontalAdvance(QString::fromLatin1(text));
  if (width > maxWidth && width > 1.0) font.setPointSizeF(std::max(5.0, font.pointSizeF() * maxWidth / width));
  painter.save();
  painter.setFont(font);
  painter.setPen(color);
  painter.drawText(QRectF(at.x() - 60, at.y() - 12, 120, 24), Qt::AlignCenter, QString::fromLatin1(text));
  painter.restore();
}

// One rotation arrow: a short arc concentric with the ring plus a solid head at its leading end.
// ccw sweeps toward increasing (math-oriented) angle; screen y is flipped when plotting.
void RotationArrow(QPainter& painter, const viewcube::Scene& s, double a0Deg, bool ccw, const QColor& color) {
  const double kDeg = 3.14159265358979323846 / 180.0;
  const double r = s.arrowRadius, dir = ccw ? 1.0 : -1.0, headBack = 7.5;
  const double a0 = a0Deg * kDeg, aTip = (a0Deg + dir * viewcube::kArrowSpanDeg) * kDeg;
  const double aNeck = (a0Deg + dir * (viewcube::kArrowSpanDeg - headBack)) * kDeg;
  auto at = [&](double a, double radius) {
    return QPointF(s.center.x + std::cos(a) * radius, s.center.y - std::sin(a) * radius);
  };
  QPainterPath arc(at(a0, r));
  for (int i = 1; i <= 14; ++i) arc.lineTo(at(a0 + (aNeck - a0) * i / 14.0, r));
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(color, 2.8, Qt::SolidLine, Qt::FlatCap));
  painter.drawPath(arc);
  const double wid = r * 0.10;
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawPolygon(QPolygonF({at(aTip, r), at(aNeck, r - wid), at(aNeck, r + wid)}));
}

void HomeGlyph(QPainter& painter, const viewcube::Scene& s, const QColor& color) {
  const double r = s.homeRadius, w = r * 0.95, cx = s.home.x, cy = s.home.y;
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawPolygon(QPolygonF({QPointF(cx, cy - r), QPointF(cx - w, cy), QPointF(cx + w, cy)}));  // roof
  painter.drawRect(QRectF(cx - w * 0.62, cy, w * 1.24, r * 0.85));                                  // body
}

}  // namespace

void PaintViewCube(QPainter& painter, const viewcube::Scene& scene, const viewcube::Hit& hover) {
  using viewcube::Part;
  const QtAccent& accent = QtAccentFor(true);  // the viewport is graphite in both appearances
  const QColor hot(accent.strong);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);

  // Compass ring.
  QPolygonF ring;
  for (const auto& p : scene.ring) ring << ToQt(p);
  painter.setPen(QPen(QColor(96, 102, 112), 1.5));
  painter.setBrush(QColor(48, 52, 60));
  painter.drawPolygon(ring);

  painter.setPen(Qt::NoPen);
  RotationArrow(painter, scene, viewcube::kArrowMidDeg + viewcube::kArrowGapDeg, true,
                hover.part == Part::TurnCcw ? hot : QColor(206, 210, 218));
  RotationArrow(painter, scene, viewcube::kArrowMidDeg - viewcube::kArrowGapDeg, false,
                hover.part == Part::TurnCw ? hot : QColor(206, 210, 218));
  HomeGlyph(painter, scene, hover.part == Part::Home ? hot : QColor(196, 200, 208));

  // Letters on the far side of the ring go behind the cube, so it hides them rather than showing through.
  for (const auto& label : scene.compass)
    if (label.shown && label.behind) CenteredLabel(painter, ToQt(label.at), label.text, QColor(222, 226, 234), 20.0);

  // Cube faces, far to near.
  for (const auto& quad : scene.quads) {
    if (!quad.visible) continue;
    QPolygonF poly;
    for (const auto& p : quad.pts) poly << ToQt(p);
    const bool isHot = hover.part == Part::Face && hover.face == quad.face;
    painter.setPen(QPen(QColor(70, 74, 82), 1.4));
    painter.setBrush(isHot ? QColor(accent.strong).darker(135) : QColor(150, 154, 162));
    painter.drawPolygon(poly);
    if (quad.labelled)
      CenteredLabel(painter, ToQt(quad.labelAt), viewcube::kFaces[quad.face].label, QColor(28, 30, 36),
                    quad.labelMaxWidth);
  }

  // Letters on the near side of the ring sit in front of the cube.
  for (const auto& label : scene.compass)
    if (label.shown && !label.behind) CenteredLabel(painter, ToQt(label.at), label.text, QColor(222, 226, 234), 20.0);
  painter.restore();
}
