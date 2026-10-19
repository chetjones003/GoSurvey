#pragma once

#include <QPainter>
#include <QPainterPath>
#include <array>
#include <initializer_list>

namespace qtArtwork {
// Coordinates traced from the approved 300 x 220 Properties-panel artwork.
// Each contour follows its own landform; translating a single wave loses the saddle
// above the amber contour and the nested spur in the bottom-left corner.
struct TerrainContour {
  QPainterPath path;
  qreal strength;
  bool amber;
};

inline TerrainContour TraceContour(std::initializer_list<QPointF> points,
                                    qreal strength, bool amber = false) {
  auto it = points.begin();
  QPainterPath path(*it++);
  while (it != points.end()) {
    const QPointF a = *it++, b = *it++, end = *it++;
    path.cubicTo(a, b, end);
  }
  return {path, strength, amber};
}

inline const std::array<TerrainContour, 18>& TerrainContours() {
  static const auto contours = [] {
    auto curve = [](std::initializer_list<QPointF> points, qreal strength, bool amber = false) {
      return TraceContour(points, strength, amber);
    };
    return std::array<TerrainContour, 18>{{
      curve({{-9,12},{24,12},{34,-5},{62,4},{100,15},{113,53},{157,55},
             {190,58},{211,45},{231,58},{250,70},{264,31},{281,9},{292,-5},{304,-16},{312,-25}}, .25),
      curve({{-9,25},{25,25},{34,8},{62,18},{100,34},{110,66},{156,68},
             {190,72},{212,57},{230,70},{250,85},{260,46},{277,23},{288,8},{299,-2},{310,-12}}, .33),
      curve({{-9,39},{23,38},{32,21},{56,29},{90,37},{105,83},{143,90},
             {175,99},{192,73},{214,91},{238,114},{256,82},{272,54},{286,32},{298,23},{311,17}}, .46),
      curve({{-9,53},{22,51},{33,36},{57,44},{88,53},{103,104},{145,104},
             {176,107},{190,89},{214,107},{238,132},{253,95},{273,83},{289,74},{305,62},{314,55}}, .65),
      curve({{-9,67},{24,67},{33,51},{57,57},{88,64},{102,118},{142,118},
             {175,117},{191,103},{213,126},{235,153},{265,167},{312,178}}, .45),
      curve({{-9,77},{25,77},{35,61},{59,67},{89,75},{99,132},{142,132},
             {170,135},{191,137},{210,162},{235,190},{265,192},{312,207}}, .72),
      curve({{-9,85},{26,85},{36,70},{59,76},{90,85},{96,141},{142,145},
             {169,151},{187,160},{204,190},{216,211},{230,222},{256,233}}, .50),
      curve({{-9,93},{26,90},{36,78},{59,84},{89,95},{94,152},{135,156},
             {158,163},{183,174},{195,209},{201,219},{205,226},{211,234}}, .53),
      curve({{-9,103},{20,102},{35,87},{57,93},{83,103},{93,154},{123,164},
             {151,174},{173,187},{185,220},{190,229},{199,234},{212,239}}, 1.0, true),
      curve({{-9,115},{22,111},{35,99},{57,105},{80,118},{83,160},{110,171},
             {139,186},{158,184},{173,220},{178,229},{182,235},{192,240}}, .55),
      curve({{-9,129},{22,122},{37,111},{57,117},{77,129},{80,170},{104,180},
             {126,190},{147,188},{158,220},{163,229},{169,237},{179,240}}, .68),
      curve({{-9,140},{20,130},{34,123},{53,129},{72,140},{72,177},{95,187},
             {117,198},{138,187},{145,220},{148,230},{156,236},{165,240}}, .85),
      curve({{-9,153},{18,144},{33,135},{51,141},{68,153},{65,186},{85,194},
             {106,203},{126,195},{135,220},{139,231},{146,238},{153,244}}, .65),
      curve({{-9,165},{18,156},{32,145},{47,152},{63,162},{59,193},{77,201},
             {97,208},{116,204},{121,220},{125,230},{132,237},{140,241}}, .78),
      curve({{-9,179},{17,168},{29,155},{43,164},{56,176},{52,199},{69,208},
             {83,218},{103,211},{108,230}}, .61),
      curve({{-9,192},{16,179},{28,167},{39,176},{49,189},{46,208},{59,219},
             {72,231},{91,222},{96,238}}, .72),
      curve({{-9,205},{13,192},{26,179},{34,190},{42,200},{37,215},{48,225}}, .60),
      curve({{-9,217},{11,206},{20,192},{29,202},{33,209},{28,216},{35,228}}, .65)
    }};
  }();
  return contours;
}

inline void DrawTerrainContours(QPainter& painter, const QRectF& bounds,
                                bool turn = false, qreal opacity = 1.0) {
  if (bounds.isEmpty()) return;
  painter.save();
  painter.setClipRect(bounds, Qt::IntersectClip);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(bounds.topLeft());
  painter.scale(bounds.width() / 300.0, bounds.height() / 220.0);
  if (turn) { painter.translate(300, 220); painter.rotate(180); }
  painter.setBrush(Qt::NoBrush);
  for (const auto& contour : TerrainContours()) {
    const QColor color = contour.amber ? QColor(185, 100, 12, 108) : QColor(18, 70, 96, 150);
    painter.setOpacity(opacity * contour.strength);
    QPen pen(color, contour.amber ? 1.05 : .8);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawPath(contour.path);
  }
  painter.restore();
}

inline void DrawRisingContours(QPainter& painter, const QRectF& bounds, qreal opacity = 1.0) {
  if (bounds.isEmpty()) return;
  // The reference's right dock has a rising saddle, not a rotated copy of the
  // nested left-hand landform. Endpoints leave through the panel's outer edges.
  static const std::array<TerrainContour, 10> contours{{
    TraceContour({{-15,125},{40,122},{54,113},{72,95},{88,78},{101,75},{119,84},
                  {135,93},{145,77},{155,55},{173,23},{191,11},{230,-5}}, .44),
    TraceContour({{10,132},{50,128},{62,118},{82,102},{97,86},{111,83},{126,92},
                  {143,102},{153,82},{163,62},{181,30},{201,18},{230,6}}, .55),
    TraceContour({{35,136},{65,129},{77,119},{90,108},{105,96},{116,91},{131,101},
                  {149,112},{161,89},{172,70},{188,44},{209,31},{232,20}}, .65),
    TraceContour({{57,137},{82,130},{90,118},{102,111},{116,102},{124,105},{137,109},
                  {153,115},{169,97},{180,79},{195,56},{213,45},{234,36}}, .55),
    TraceContour({{78,139},{98,130},{108,119},{119,116},{135,110},{143,123},{155,117},
                  {172,111},{179,96},{190,85},{204,70},{222,63},{235,58}}, .76),
    TraceContour({{98,141},{114,133},{119,122},{132,124},{146,125},{152,135},{164,125},
                  {177,115},{185,104},{196,99},{213,90},{226,88},{240,86}}, .55),
    TraceContour({{120,140},{132,128},{140,113},{153,116},{167,120},{171,118},{182,107},
                  {194,93},{207,89},{232,86}}, .85, true),
    TraceContour({{140,144},{154,131},{160,125},{174,128},{189,130},{196,110},{208,107},
                  {220,105},{234,104},{245,107}}, .64),
    TraceContour({{165,145},{180,131},{187,134},{199,129},{212,121},{225,118},{244,122}}, .65),
    TraceContour({{190,149},{203,135},{216,131},{229,133},{238,136},{248,133},{256,129}}, .48)
  }};
  painter.save();
  painter.setClipRect(bounds, Qt::IntersectClip);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(bounds.topLeft());
  painter.scale(bounds.width() / 220.0, bounds.height() / 120.0);
  painter.setBrush(Qt::NoBrush);
  for (const auto& contour : contours) {
    painter.setOpacity(opacity * contour.strength);
    QPen pen(contour.amber ? QColor(185, 100, 12, 108) : QColor(18, 70, 96, 150),
             contour.amber ? 1.05 : .8);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawPath(contour.path);
  }
  painter.restore();
}
}  // namespace qtArtwork
