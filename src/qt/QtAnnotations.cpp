#include "QtAnnotations.hpp"
#include "QtText.hpp"
#include "CadCommands.hpp"
#include "CadColor.hpp"
#include "CadField.hpp"
#include "CadDimStroke.hpp"
#include "font/CadFontName.hpp"
#include "font/ShxFont.hpp"
#include "MtextRichFormat.hpp"
#include <QPainter>
#include <QTextDocument>
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

namespace {
bool DrawStrokeText(QPainter& painter, const CadAnnotation& annotation, float pixels,
                     const std::string& text, bool baseline) {
  if (!cadfont::PreferShxStrokes(annotation.fontFamily, text)) return false;
  auto* font = Shx::Resolve(annotation.fontFamily);
  if (!font || !font->valid()) return false;
  const float scale = pixels / font->capHeight();
  float x = 0, y = baseline ? 0 : pixels;
  const float shear = std::tan(annotation.obliqueDeg * 3.14159265358979323846f / 180.f);
  const std::string plain = MtextRichFlattenToPlain(text);
  for (const unsigned char character : plain) {
    if (character == '\n') { x = 0; y += pixels * 1.4f; continue; }
    const auto* glyph = font->glyph(character);
    if (!glyph) continue;
    for (const auto& stroke : glyph->strokes) {
      QPolygonF path;
      for (const auto& point : stroke) path << QPointF(x + (point.x + shear * point.y) * scale, y - point.y * scale);
      if (path.size() > 1) painter.drawPolyline(path);
    }
    x += glyph->advance * scale;
  }
  return true;
}
}

void DrawQtModelAnnotations(QPainter& painter, const AppCommandState& state, float width, float height) {
  const auto camera = CadViewCamera(state);
  auto project = [&](double x, double y, double z) {
    float sx = 0, sy = 0;
    camera.WorldToScreen(x, y, z, width, height, &sx, &sy);
    return QPointF(sx, sy);
  };
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  for (std::size_t i = 0; i < state.cadAnnotations.size(); ++i) {
    const auto& annotation = state.cadAnnotations[i];
    const auto* attrs = i < state.cadAnnotationAttrs.size() ? &state.cadAnnotationAttrs[i] : nullptr;
    const CadLayerRow* layer = nullptr;
    if (attrs) {
      for (const auto& row : state.drawingLayerTable) if (row.name == attrs->layer) { layer = &row; break; }
      if (layer && (!layer->on || layer->frozen)) continue;
    }
    float rgb[3]{1, 1, 1};
    if (attrs) CadColorResolveRgb(CadColorEffectiveStorage(attrs->color, layer ? layer->color : "White"), 1, 1, 1, rgb);
    QColor color = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    for (const auto& selected : state.selection)
      if (selected.type == SelectedEntity::Type::Annotation && selected.index == static_cast<int>(i)) color = QColor("#62aaff");
    QPen pen(color, 1.2); pen.setCosmetic(true);
    painter.setPen(pen); painter.setBrush(Qt::NoBrush);
    double x = annotation.insX, y = annotation.insY, rotation = annotation.rotationRad;
    const bool dimension = CadAnnotationIsDimension(annotation);
    if (dimension) {
      CadDimStrokeParams parameters;
      parameters.modelUnitsPerPlottedInch = state.modelUnitsPerPlottedInch;
      parameters.arrowSizeInches = state.activeDimensionStyle.arrowSizeInches;
      parameters.arrowScale = state.viewportDimArrowScale;
      parameters.arrowType = state.activeDimensionStyle.arrowType;
      CadDimWorldStrokes strokes;
      if (!CadDimBuildWorldStrokes(annotation, parameters, &strokes)) continue;
      for (const auto& segment : strokes.segs)
        painter.drawLine(project(segment.x0, segment.y0, annotation.insZ), project(segment.x1, segment.y1, annotation.insZ));
      painter.setBrush(color);
      for (const auto& arrow : strokes.arrows) painter.drawPolygon(QPolygonF{
          project(arrow.x0, arrow.y0, annotation.insZ), project(arrow.x1, arrow.y1, annotation.insZ),
          project(arrow.x2, arrow.y2, annotation.insZ)});
      painter.setBrush(Qt::NoBrush);
      x = strokes.labelX; y = strokes.labelY; rotation = strokes.labelRotRad;
    } else if (annotation.kind != CadAnnotation::Kind::Text && annotation.kind != CadAnnotation::Kind::Mtext) continue;
    const double textWorld = CadAnnotationHeightWorld(annotation, state.modelUnitsPerPlottedInch);
    if (!(textWorld > 0) || !std::isfinite(textWorld)) continue;
    constexpr float fontPixels = 100;
    const double worldPerPixel = textWorld / fontPixels;
    if (annotation.kind == CadAnnotation::Kind::Mtext) { x = annotation.boxMinX; y = annotation.boxMaxY; }
    const QPointF origin = project(x, y, annotation.insZ);
    const double c = std::cos(rotation), s = std::sin(rotation);
    const QPointF right = project(x + c * worldPerPixel, y + s * worldPerPixel, annotation.insZ) - origin;
    const QPointF down = project(x + s * worldPerPixel, y - c * worldPerPixel, annotation.insZ) - origin;
    if (!std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
        !std::isfinite(right.x()) || !std::isfinite(down.y())) continue;
    if (std::hypot(right.x(), right.y()) * fontPixels < 1) continue;
    painter.save();
    painter.setWorldTransform(QTransform(right.x(), right.y(), down.x(), down.y(), origin.x(), origin.y()));
    std::string text = CadAnnotationResolvedText(state, annotation);
    if (annotation.bold) { text.insert(0, "[[b]]"); text.append("[[/b]]"); }
    if (annotation.italic) { text.insert(0, "[[i]]"); text.append("[[/i]]"); }
    if (annotation.underline) { text.insert(0, "[[u]]"); text.append("[[/u]]"); }
    if (DrawStrokeText(painter, annotation, fontPixels, text, annotation.kind == CadAnnotation::Kind::Text)) {
      painter.restore(); continue;
    }
    QTextDocument document;
    PopulateQtText(document, fontPixels, text, annotation.fontFamily, color);
    if (annotation.kind == CadAnnotation::Kind::Mtext) {
      const double boxWidth = std::max(1.0, (annotation.boxMaxX - annotation.boxMinX) / worldPerPixel);
      const double boxHeight = std::max(1.0, (annotation.boxMaxY - annotation.boxMinY) / worldPerPixel);
      document.setTextWidth(boxWidth);
      const int attachment = std::clamp(annotation.mtextAttach, 1, 9) - 1;
      auto option = document.defaultTextOption();
      option.setAlignment(attachment % 3 == 1 ? Qt::AlignHCenter : attachment % 3 == 2 ? Qt::AlignRight : Qt::AlignLeft);
      document.setDefaultTextOption(option);
      const double offset = attachment / 3 == 1 ? (boxHeight - document.size().height()) * 0.5 :
          attachment / 3 == 2 ? boxHeight - document.size().height() : 0;
      painter.translate(0, offset);
    } else {
      document.adjustSize();
      if (dimension) painter.translate(-document.idealWidth() * 0.5, -document.size().height() * 0.5);
      else painter.translate(0, -QFontMetricsF(document.defaultFont()).ascent());
    }
    document.drawContents(&painter);
    painter.restore();
  }
  const float markerRadius = PositionMarkerRadiusWorld(state);
  for (std::size_t i = 0; i < state.cadPositionMarkers.size(); ++i) {
    const auto& marker = state.cadPositionMarkers[i];
    const EntityAttributes* attrs = i < state.cadPositionMarkerAttrs.size() ? &state.cadPositionMarkerAttrs[i] : nullptr;
    if (attrs && CadEntityIdHidden(&state.hiddenEntityIds, attrs->id)) continue;
    const CadLayerRow* layer = nullptr;
    if (attrs) for (const auto& row : state.drawingLayerTable) if (row.name == attrs->layer) { layer = &row; break; }
    if (layer && (!layer->on || layer->frozen)) continue;
    float rgb[3]{0.9f, 0.9f, 0.9f};
    if (attrs) CadColorResolveRgb(CadColorEffectiveStorage(attrs->color, layer ? layer->color : "White"),
                                  0.9f, 0.9f, 0.9f, rgb);
    QColor color = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    bool selected = false;
    for (const auto& entity : state.selection)
      selected |= entity.type == SelectedEntity::Type::PositionMarker && entity.index == static_cast<int>(i);
    const QPointF center = project(marker.x, marker.y, marker.z);
    const QPointF edge = project(marker.x + markerRadius, marker.y, marker.z);
    const double radius = std::max(4.0, std::hypot(edge.x() - center.x(), edge.y() - center.y()));
    if (selected) color = QColor("#62aaff");
    QPen markerPen(color, selected ? 2.0 : 1.5); markerPen.setCosmetic(true);
    painter.setPen(markerPen); painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);
    painter.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));
    if (!(state.mtextRichEditorOpen && state.mtextRichEditorMarkerIndex == static_cast<int>(i)) && !marker.label.text.empty()) {
      painter.setPen(color);
      painter.drawText(QPointF(center.x() + radius + 4, center.y() - 4),
                       QString::fromUtf8(marker.label.text.c_str()));
    }
  }
  if (state.drawingSettings.Geolocated()) {
    const double x = state.drawingSettings.markerX - state.worldDocumentOriginX;
    const double y = state.drawingSettings.markerY - state.worldDocumentOriginY;
    const double angle = state.drawingSettings.markerNorthDeg * 3.14159265358979323846 / 180.0;
    const QPointF center = project(x, y, 0.0);
    const QPointF north = project(x + std::cos(angle), y + std::sin(angle), 0.0);
    QPointF direction = north - center;
    const double length = std::hypot(direction.x(), direction.y());
    if (length > 1.e-6) {
      direction /= length;
      const QPointF tip = center + direction * 25.0;
      const QColor geoColor("#f47721");
      QPen geoPen(geoColor, 2.0); geoPen.setCosmetic(true);
      painter.setPen(geoPen); painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(center, 8.0, 8.0);
      painter.setBrush(geoColor);
      painter.drawEllipse(center, 2.5, 2.5);
      painter.drawLine(center + direction * 8.0, tip);
      const QPointF perpendicular(-direction.y(), direction.x());
      painter.drawPolygon(QPolygonF{tip, tip - direction * 8.0 + perpendicular * 4.0,
                                    tip - direction * 8.0 - perpendicular * 4.0});
      painter.drawText(tip + direction * 8.0 + QPointF(-4, 4), "N");
    }
  }
  painter.restore();
}
