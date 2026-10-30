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
#include <QPolygonF>
#include <QTextDocument>
#include <QFontMetricsF>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
void DrawQtLeaderPath(QPainter& painter, const CadMultileader& ml, const std::vector<float>& path,
                      const Camera& camera, float width, float height, float arrowSize) {
  assert(width > 0.f);
  assert(height > 0.f);
  auto project = [&](double x, double y, double z) {
    float sx = 0.f, sy = 0.f;
    camera.WorldToScreen(x, y, z, width, height, &sx, &sy);
    return QPointF(sx, sy);
  };
  for (size_t i = 0; i + 5 < path.size(); i += 3)
    painter.drawLine(project(path[i], path[i + 1], path[i + 2]),
                     project(path[i + 3], path[i + 4], path[i + 5]));
  const CadLeaderArrowHead arrow = CadMultileaderArrow(ml, path, arrowSize);
  if (!arrow.valid)
    return;
  painter.setBrush(painter.pen().color());
  painter.drawPolygon(QPolygonF{project(arrow.tipX, arrow.tipY, arrow.tipZ),
                                project(arrow.b1x, arrow.b1y, arrow.b1z),
                                project(arrow.b2x, arrow.b2y, arrow.b2z)});
  painter.setBrush(Qt::NoBrush);
}

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

void DrawQtSurveyPoints(QPainter& painter, const AppCommandState& state, float width, float height) {
  const auto camera = CadViewCamera(state);
  const auto right = camera.RightWorld();
  const double arm = SurveyPointCrossHalfWorldFromPaper(state.surveyPointCrossSpanPlottedInches,
                                                       state.modelUnitsPerPlottedInch);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  for (std::size_t i = 0; i < state.surveyPoints.size(); ++i) {
    const auto& point = state.surveyPoints[i];
    const auto layer = std::find_if(state.drawingLayerTable.begin(), state.drawingLayerTable.end(),
        [&point](const auto& row) { return row.name == point.layer; });
    if (layer != state.drawingLayerTable.end() && (!layer->on || layer->frozen)) continue;
    float x = 0, y = 0, ex = 0, ey = 0;
    camera.WorldToScreen(point.easting, point.northing, point.elevation, width, height, &x, &y);
    camera.WorldToScreen(point.easting + right.x * arm, point.northing + right.y * arm,
                        point.elevation + right.z * arm, width, height, &ex, &ey);
    const double half = std::hypot(ex - x, ey - y);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(half)) continue;
    if (x + half < 0 || x - half > width || y + half < 0 || y - half > height) continue;
    const bool selected = std::find(state.selectedSurveyPointIndices.begin(), state.selectedSurveyPointIndices.end(),
        static_cast<int>(i)) != state.selectedSurveyPointIndices.end();
    const bool hovered = state.viewportHoverSurveyPointIndex == static_cast<int>(i);
    painter.setPen(QPen(selected ? QColor(255, 235, 38) : hovered ? QColor(100, 215, 255) : QColor("#f97316"),
                        selected ? 2.8 : hovered ? 2.6 : 1.5));
    painter.drawLine(QPointF(x - half, y - half), QPointF(x + half, y + half));
    painter.drawLine(QPointF(x - half, y + half), QPointF(x + half, y - half));
    if (state.surveyPointShowIdInViewport) {
      const double textPixels = std::clamp(half / arm * state.surveyPointLabelPlottedHeightInches *
          std::max(state.modelUnitsPerPlottedInch, 1.e-6f),
          static_cast<double>(state.viewportTextMinPx), static_cast<double>(state.viewportTextMaxPx));
      QFont font = painter.font(); font.setPixelSize(std::max(1, qRound(textPixels)));
      painter.setFont(font); painter.setPen(QColor(255, 248, 200));
      painter.drawText(QPointF(x + 6, y - textPixels * .35 + QFontMetricsF(font).ascent()), QString::number(point.id));
    }
  }
  painter.restore();
}

void DrawQtModelAnnotations(QPainter& painter, const AppCommandState& state, float width, float height) {
  const auto camera = CadViewCamera(state);
  const auto activeScaleName = CadActiveAnnotationScaleName(&state.annotationScales,
      state.currentAnnotationScaleIndex, nullptr, state.modelUnitsPerPlottedInch);
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
    if (attrs && CadEntityIdHidden(&state.hiddenEntityIds, attrs->id)) continue;
    if (annotation.annotative && !annotation.annotativeVisibleScaleNames.empty() && !activeScaleName.empty() &&
        std::find(annotation.annotativeVisibleScaleNames.begin(), annotation.annotativeVisibleScaleNames.end(), activeScaleName) ==
            annotation.annotativeVisibleScaleNames.end()) continue;
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
    const float annotationMup = AnnotativeModelUnitsPerPlottedInch(annotation, nullptr,
        state.modelUnitsPerPlottedInch, &state.annotationScales, state.currentAnnotationScaleIndex);
    const double textWorld = CadAnnotationHeightWorld(annotation, annotationMup);
    if (!(textWorld > 0) || !std::isfinite(textWorld)) continue;
    std::string text = CadAnnotationResolvedText(state, annotation);
    if (annotation.bold) { text.insert(0, "[[b]]"); text.append("[[/b]]"); }
    if (annotation.italic) { text.insert(0, "[[i]]"); text.append("[[/i]]"); }
    if (annotation.underline) { text.insert(0, "[[u]]"); text.append("[[/u]]"); }
    constexpr float fontPixels = 100;
    const double worldPerPixel = textWorld / fontPixels;
    if (annotation.kind == CadAnnotation::Kind::Mtext) { x = annotation.boxMinX; y = annotation.boxMaxY; }
    if (annotation.surveyLabelHasUserOffset) {
      const int pointIndex = SurveyPointIndexForId(state, annotation.surveyPointLabelForId);
      if (pointIndex >= 0) {
        const auto& point = state.surveyPoints[static_cast<std::size_t>(pointIndex)];
        const double cx = .5 * (annotation.boxMinX + annotation.boxMaxX);
        const double cy = .5 * (annotation.boxMinY + annotation.boxMaxY);
        const double halfDiagonal = .5 * std::hypot(annotation.boxMaxX - annotation.boxMinX,
                                                   annotation.boxMaxY - annotation.boxMinY);
        if (std::hypot(point.easting - cx, point.northing - cy) > halfDiagonal * 1.1) {
          const QRectF box = QRectF(project(annotation.boxMinX, annotation.boxMinY, annotation.insZ),
                                   project(annotation.boxMaxX, annotation.boxMaxY, annotation.insZ)).normalized();
          const QPointF tip = project(point.easting, point.northing, point.elevation);
          const QPointF delta = tip - box.center();
          const double distance = std::hypot(delta.x(), delta.y());
          if (std::isfinite(distance) && distance > 1) {
            const QPointF unit = delta / distance;
            const double edgeX = std::abs(unit.x()) > 1.e-5 ? box.width() * .5 / std::abs(unit.x()) : distance;
            const double edgeY = std::abs(unit.y()) > 1.e-5 ? box.height() * .5 / std::abs(unit.y()) : distance;
            const QPointF start = box.center() + unit * std::min(edgeX, edgeY);
            const double halfWidth = state.surveyLabelLeaderArrowPx;
            const QPointF base = tip - unit * (halfWidth * 2.36);
            const QPointF side(-unit.y() * halfWidth, unit.x() * halfWidth);
            painter.save();
            painter.setPen(QPen(QColor(0, 0, 0, 120), 2));
            painter.drawLine(start + QPointF(1, 1), tip + QPointF(1, 1));
            painter.setPen(QPen(QColor(249, 115, 22, 220), 1.5));
            painter.drawLine(start, tip);
            painter.setBrush(QColor(249, 115, 22, 220));
            painter.drawPolygon(QPolygonF{tip, base + side, base - side});
            painter.restore();
          }
        }
      }
    }
    if (annotation.surveyPointLabelForId >= 0) {
      // Survey labels face the screen and retain the legacy readability floor.
      const auto right = camera.RightWorld();
      const QPointF anchor = project(x, y, annotation.insZ);
      const QPointF edge = project(x + right.x * textWorld, y + right.y * textWorld,
                                   annotation.insZ + right.z * textWorld);
      const double pixels = std::clamp(std::hypot(edge.x() - anchor.x(), edge.y() - anchor.y()),
          static_cast<double>(state.viewportMtextMinPx), static_cast<double>(state.viewportMtextMaxPx));
      if (!std::isfinite(pixels)) continue;
      QTextDocument document;
      PopulateQtText(document, static_cast<float>(pixels), text, annotation.fontFamily, color);
      document.adjustSize();
      const QPointF center = project(.5 * (annotation.boxMinX + annotation.boxMaxX),
                                    .5 * (annotation.boxMinY + annotation.boxMaxY), annotation.insZ);
      painter.save();
      painter.translate(center - QPointF(document.idealWidth() * .5, document.size().height() * .5));
      if (!DrawStrokeText(painter, annotation, static_cast<float>(pixels), text, false)) document.drawContents(&painter);
      painter.restore();
      continue;
    }
    const QPointF origin = project(x, y, annotation.insZ);
    const double c = std::cos(rotation), s = std::sin(rotation);
    const QPointF right = project(x + c * worldPerPixel, y + s * worldPerPixel, annotation.insZ) - origin;
    const QPointF down = project(x + s * worldPerPixel, y - c * worldPerPixel, annotation.insZ) - origin;
    if (!std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
        !std::isfinite(right.x()) || !std::isfinite(down.y())) continue;
    if (std::hypot(right.x(), right.y()) * fontPixels < 1) continue;
    painter.save();
    painter.setWorldTransform(QTransform(right.x(), right.y(), down.x(), down.y(), origin.x(), origin.y()));
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
  const float leaderArrow = std::max(state.activeMultileaderStyle.arrowSizeInches, 0.01f) *
                            std::max(state.modelUnitsPerPlottedInch, 1.e-6f);
  for (std::size_t i = 0; i < state.cadMultileaders.size(); ++i) {
    const CadMultileader& ml = state.cadMultileaders[i];
    const EntityAttributes* attrs = i < state.cadMultileaderAttrs.size() ? &state.cadMultileaderAttrs[i] : nullptr;
    if (attrs && CadEntityIdHidden(&state.hiddenEntityIds, attrs->id)) continue;
    const CadLayerRow* layer = nullptr;
    if (attrs) for (const auto& row : state.drawingLayerTable) if (row.name == attrs->layer) { layer = &row; break; }
    if (layer && (!layer->on || layer->frozen)) continue;
    float rgb[3]{1.f, 1.f, 1.f};
    if (attrs) CadColorResolveRgb(CadColorEffectiveStorage(attrs->color, layer ? layer->color : "White"), 1, 1, 1, rgb);
    QColor color = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    for (const auto& selected : state.selection)
      if (selected.type == SelectedEntity::Type::Multileader && selected.index == static_cast<int>(i))
        color = QColor("#62aaff");
    QPen pen(color, 1.2); pen.setCosmetic(true);
    painter.setPen(pen); painter.setBrush(Qt::NoBrush);
    DrawQtLeaderPath(painter, ml, ml.pathXyz, camera, width, height, leaderArrow);
    for (const auto& branch : ml.extraLeaderPaths)
      DrawQtLeaderPath(painter, ml, branch, camera, width, height, leaderArrow);
    if (!ml.label.text.empty())
      painter.drawText(project(ml.label.insX, ml.label.insY, ml.label.insZ),
                       QString::fromUtf8(ml.label.text.c_str()));
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

void DrawQtTransformAnnotationPreviews(QPainter& painter, const AppCommandState& state, float cursorLocalX,
                                       float cursorLocalY, float width, float height) {
  std::vector<CadAnnotation> previews;
  CadAnnotationCollectTransformPreviews(state, cursorLocalX, cursorLocalY, &previews);
  if (previews.empty())
    return;
  const auto camera = CadViewCamera(state);
  auto project = [&](double x, double y, double z) {
    float sx = 0, sy = 0;
    camera.WorldToScreen(x, y, z, width, height, &sx, &sy);
    return QPointF(sx, sy);
  };
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setOpacity(0.55);
  for (const CadAnnotation& annotation : previews) {
    if (annotation.kind != CadAnnotation::Kind::Text && annotation.kind != CadAnnotation::Kind::Mtext)
      continue;
    float rgb[3]{0.38f, 0.67f, 1.f};
    QColor color = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    QPen pen(color, 1.2);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    double x = annotation.insX, y = annotation.insY, rotation = annotation.rotationRad;
    const float annotationMup = AnnotativeModelUnitsPerPlottedInch(annotation, nullptr,
        state.modelUnitsPerPlottedInch, &state.annotationScales, state.currentAnnotationScaleIndex);
    const double textWorld = CadAnnotationHeightWorld(annotation, annotationMup);
    if (!(textWorld > 0) || !std::isfinite(textWorld))
      continue;
    std::string text = CadAnnotationResolvedText(state, annotation);
    constexpr float fontPixels = 100;
    const double worldPerPixel = textWorld / fontPixels;
    if (annotation.kind == CadAnnotation::Kind::Mtext) {
      x = annotation.boxMinX;
      y = annotation.boxMaxY;
    }
    const QPointF origin = project(x, y, annotation.insZ);
    const double c = std::cos(rotation), s = std::sin(rotation);
    const QPointF right = project(x + c * worldPerPixel, y + s * worldPerPixel, annotation.insZ) - origin;
    const QPointF down = project(x + s * worldPerPixel, y - c * worldPerPixel, annotation.insZ) - origin;
    if (!std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
        !std::isfinite(right.x()) || !std::isfinite(down.y()))
      continue;
    painter.save();
    painter.setWorldTransform(QTransform(right.x(), right.y(), down.x(), down.y(), origin.x(), origin.y()));
    if (DrawStrokeText(painter, annotation, fontPixels, text, annotation.kind == CadAnnotation::Kind::Text)) {
      painter.restore();
      continue;
    }
    QTextDocument document;
    PopulateQtText(document, fontPixels, text, annotation.fontFamily, color);
    if (annotation.kind == CadAnnotation::Kind::Mtext) {
      const double boxWidth = std::max(1.0, (annotation.boxMaxX - annotation.boxMinX) / worldPerPixel);
      document.setTextWidth(boxWidth);
    } else {
      document.adjustSize();
      painter.translate(0, -QFontMetricsF(document.defaultFont()).ascent());
    }
    document.drawContents(&painter);
    painter.restore();
  }
  painter.restore();
}
