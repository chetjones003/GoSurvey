#include "QtAnnotations.hpp"
#include "QtText.hpp"
#include "CadCommands.hpp"
#include "CadColor.hpp"
#include "CadField.hpp"
#include "CadDimStroke.hpp"
#include "CadCoordinateFrame.hpp"
#include "TextStyle.hpp"
#include "AlignmentLabelSet.hpp"
#include "AlignmentStyle.hpp"
#include "alignment/AlignmentLabels.hpp"
#include "alignment/AlignmentDesign.hpp"
#include "font/CadFontName.hpp"
#include "font/ShxFont.hpp"
#include "MtextRichFormat.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegion>
#include <QFontMetricsF>
#include <QTextDocument>
#include <QFontMetricsF>
#include <QTransform>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
QPen AnnotationStylePen(const std::string& color, const std::string& linetype, const std::string& weight,
                         const CadLayerRow* layer, QColor inherited, bool selected) {
  float rgb[3] = {inherited.redF(), inherited.greenF(), inherited.blueF()};
  if (!color.empty() && color != "ByBlock")
    CadColorResolveRgb(CadColorEffectiveStorage(color, layer ? layer->color : "White"), rgb[0], rgb[1], rgb[2], rgb);
  QPen pen(selected ? QColor("#62aaff") : QColor::fromRgbF(rgb[0], rgb[1], rgb[2]), 1.2);
  pen.setCosmetic(true);
  const double mm = weight == "ByLayer" ? (layer ? layer->lineweightMm : -1) :
      weight.empty() || weight == "ByBlock" || weight == "Default" ? -1 : std::strtod(weight.c_str(), nullptr);
  if (mm >= 0) pen.setWidthF(std::max(.7, mm * 96 / 25.4));
  const auto type = linetype == "ByLayer" && layer ? layer->linetype : linetype;
  if (type == "Dashed" || type == "Hidden" || type == "DASHED" || type == "HIDDEN") pen.setStyle(Qt::DashLine);
  else if (type == "Center" || type == "DashDot" || type == "CENTER") pen.setStyle(Qt::DashDotLine);
  else if (type == "Phantom" || type == "PHANTOM") pen.setStyle(Qt::DashDotDotLine);
  return pen;
}

void DrawQtLeaderPath(QPainter& painter, const CadMultileader& ml, const std::vector<float>& path,
                      const Camera& camera, float width, float height, float arrowSize) {
  assert(width > 0.f);
  assert(height > 0.f);
  auto project = [&](double x, double y, double z) {
    float sx = 0.f, sy = 0.f;
    camera.WorldToScreen(x, y, z, width, height, &sx, &sy);
    return QPointF(sx, sy);
  };
  if (ml.splinePath && path.size() >= 9) {
    QPainterPath curve(project(path[0], path[1], path[2]));
    for (std::size_t i = 3; i + 5 < path.size(); i += 3) {
      const auto control = project(path[i], path[i + 1], path[i + 2]);
      const auto next = project(path[i + 3], path[i + 4], path[i + 5]);
      curve.quadTo(control, i + 6 == path.size() ? next : (control + next) * .5);
    }
    painter.drawPath(curve);
  } else for (size_t i = 0; i + 5 < path.size(); i += 3)
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

void DrawPointMarkerBlock(QPainter& painter, const AppCommandState& state, const SurveyPoint& point,
                          const SurveyPointStyleDefinition& style, const SurveyPointStyleComponent& component,
                          const Camera& camera, float width, float height, double span, QColor highlight) {
  if (style.markerBlockName.empty()) return;
  CadBlockRef reference;
  reference.defName = style.markerBlockName;
  float minX = 0, minY = 0, maxX = 0, maxY = 0;
  CadBlockWorldAabb(state.blockDefs, reference, &minX, &minY, &maxX, &maxY);
  const double extent = std::max(maxX - minX, maxY - minY);
  const double scale = style.markerSizeType == 3 ? 1.0 : extent > 1.e-12 ? span / extent : 1.0;
  reference.xf.x = static_cast<float>(point.easting);
  reference.xf.y = static_cast<float>(point.northing);
  reference.xf.z = static_cast<float>(style.markerDisplay3dType == 1 ? style.markerElevation : point.elevation);
  reference.xf.rotZ = static_cast<float>(style.markerRotationRad);
  reference.xf.sx = static_cast<float>(scale * (style.markerSizeType == 3 ? style.markerFixedScale[0] : 1.0));
  reference.xf.sy = static_cast<float>(scale * (style.markerSizeType == 3 ? style.markerFixedScale[1] : 1.0));
  reference.xf.sz = static_cast<float>(scale * (style.markerSizeType == 3 ? style.markerFixedScale[2] : 1.0));
  EntityAttributes insert;
  insert.layer = component.layer.empty() || component.layer == "0" ? point.layer : component.layer;
  insert.color = component.color;
  auto color = [&](const EntityAttributes& attributes) {
    if (highlight.isValid()) return highlight;
    const auto layer = std::find_if(state.drawingLayerTable.begin(), state.drawingLayerTable.end(),
        [&](const CadLayerRow& value) { return value.name == attributes.layer; });
    float rgb[3] = {1, 1, 1};
    CadColorResolveRgb(CadColorEffectiveStorage(attributes.color,
        layer == state.drawingLayerTable.end() ? "White" : layer->color), 1, 1, 1, rgb);
    return QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
  };
  std::vector<CadBlockWorldSeg> segments;
  CadBlockCollectWorldLines(state.blockDefs, reference, insert, &segments);
  for (const auto& segment : segments) {
    const auto layer = std::find_if(state.drawingLayerTable.begin(), state.drawingLayerTable.end(),
        [&](const CadLayerRow& value) { return value.name == segment.attr.layer; });
    if (layer != state.drawingLayerTable.end() && (!layer->on || layer->frozen)) continue;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    camera.WorldToScreen(segment.x0, segment.y0, segment.z0, width, height, &x0, &y0);
    camera.WorldToScreen(segment.x1, segment.y1, segment.z1, width, height, &x1, &y1);
    painter.setPen(QPen(color(segment.attr), highlight.isValid() ? 2.6 : 1.5));
    painter.drawLine(QPointF(x0, y0), QPointF(x1, y1));
  }
  std::vector<CadAnnotation> labels;
  CadBlockCollectWorldAnnotations(state.blockDefs, reference, &labels);
  for (const auto& label : labels) {
    float x = 0, y = 0, ex = 0, ey = 0;
    const double size = label.plottedHeightInches * state.modelUnitsPerPlottedInch;
    camera.WorldToScreen(label.insX, label.insY, label.insZ, width, height, &x, &y);
    camera.WorldToScreen(label.insX + camera.RightWorld().x * size,
                        label.insY + camera.RightWorld().y * size,
                        label.insZ + camera.RightWorld().z * size, width, height, &ex, &ey);
    const float pixels = static_cast<float>(std::hypot(ex - x, ey - y));
    if (!std::isfinite(pixels) || pixels <= 0.f) continue;
    painter.save();
    painter.translate(x, y);
    painter.rotate(-label.rotationRad * 180.0 / 3.14159265358979323846);
    painter.setPen(color(insert));
    if (!DrawStrokeText(painter, label, pixels, label.text, true)) {
      QFont font(QString::fromUtf8(label.fontFamily.c_str()));
      font.setPixelSize(std::max(1, qRound(pixels)));
      painter.setFont(font);
      painter.drawText(QPointF(0, 0), QString::fromUtf8(MtextRichFlattenToPlain(label.text).c_str()));
    }
    painter.restore();
  }
}
}

void DrawQtSurveyPoints(QPainter& painter, const AppCommandState& state, float width, float height) {
  const auto camera = CadViewCamera(state);
  const auto right = camera.RightWorld();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  for (std::size_t i = 0; i < state.surveyPoints.size(); ++i) {
    const auto& point = state.surveyPoints[i];
    const auto layer = std::find_if(state.drawingLayerTable.begin(), state.drawingLayerTable.end(),
        [&point](const auto& row) { return row.name == point.layer; });
    if (layer != state.drawingLayerTable.end() && (!layer->on || layer->frozen)) continue;
    const SurveyPointStyleDefinition* pointStyle = FindSurveyPointStyle(state.surveyPointStyles, point.styleName);
    const bool explicitGeometry = pointStyle != nullptr && pointStyle->hasExplicitMarkerGeometry;
    const std::size_t componentIndex = std::fabs(camera.ForwardWorld().z) > .999 ? 0 : 2;
    const auto* component = explicitGeometry ? &pointStyle->displayComponents[componentIndex] : nullptr;
    const std::string markerLayer = component != nullptr && !component->layer.empty() && component->layer != "0"
        ? component->layer : point.layer;
    const auto markerLayerRow = std::find_if(state.drawingLayerTable.begin(), state.drawingLayerTable.end(),
        [&](const CadLayerRow& value) { return value.name == markerLayer; });
    const bool markerLayerVisible = markerLayerRow == state.drawingLayerTable.end() ||
                                    (markerLayerRow->on && !markerLayerRow->frozen);
    const bool markerVisible = markerLayerVisible && (component != nullptr && componentIndex != 0
        ? component->visible : SurveyPointMarkerVisible(point, state.surveyPointStyles));
    // The viewport ID is independent of the MTEXT label (labelStyle None only means no MTEXT label).
    const bool labelVisible = SurveyPointNamedLabelVisible(point, state.surveyPointLabelStyles) &&
        (explicitGeometry && componentIndex != 0 ? pointStyle->displayComponents[componentIndex + 1].visible :
                                                 SurveyPointLabelVisible(point, state.surveyPointStyles));
    if (!markerVisible && !(labelVisible && state.surveyPointShowIdInViewport)) continue;
    double arm = explicitGeometry && pointStyle->markerSizeType == 2 ? pointStyle->markerSize * .5 :
        SurveyPointCrossHalfWorldFromPaper(explicitGeometry ? pointStyle->crossSpanPlottedInches :
            state.surveyPointCrossSpanPlottedInches, state.modelUnitsPerPlottedInch);
    float x = 0, y = 0, ex = 0, ey = 0;
    camera.WorldToScreen(point.easting, point.northing, point.elevation, width, height, &x, &y);
    camera.WorldToScreen(point.easting + right.x * arm, point.northing + right.y * arm,
                        point.elevation + right.z * arm, width, height, &ex, &ey);
    double half = std::hypot(ex - x, ey - y);
    if (explicitGeometry && pointStyle->markerSizeType == 1 && half > 1.e-9) {
      const double screenHalf = height * pointStyle->markerSize * .005;
      arm *= screenHalf / half;
      half = screenHalf;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(half)) continue;
    if (x + half < 0 || x - half > width || y + half < 0 || y - half > height) continue;
    const bool selected = std::find(state.selectedSurveyPointIndices.begin(), state.selectedSurveyPointIndices.end(),
        static_cast<int>(i)) != state.selectedSurveyPointIndices.end();
    const bool hovered = state.viewportHoverSurveyPointIndex == static_cast<int>(i);
    if (markerVisible) {
      QColor markerColor("#f97316");
      if (pointStyle != nullptr) {
        float rgb[3] = {1.f, 0.45f, 0.08f};
        const std::string effective = CadColorEffectiveStorage(
              component != nullptr && componentIndex != 0 ? component->color : pointStyle->color,
              markerLayerRow == state.drawingLayerTable.end() ? "White" : markerLayerRow->color);
        CadColorResolveRgb(effective, rgb[0], rgb[1], rgb[2], rgb);
        markerColor = QColor::fromRgbF(std::clamp(rgb[0], 0.f, 1.f), std::clamp(rgb[1], 0.f, 1.f),
                                       std::clamp(rgb[2], 0.f, 1.f));
      }
      painter.setPen(QPen(selected ? QColor(255, 235, 38) : hovered ? QColor(100, 215, 255) : markerColor,
                          selected ? 2.8 : hovered ? 2.6 : 1.5));
      if (explicitGeometry && pointStyle->markerType == 2) {
        DrawPointMarkerBlock(painter, state, point, *pointStyle, *component, camera, width, height,
                             arm * 2.0, selected ? QColor(255, 235, 38) : hovered ? QColor(100, 215, 255) : QColor());
      } else {
        painter.save();
        painter.translate(x, y);
        painter.rotate(explicitGeometry ? -pointStyle->markerRotationRad * 180.0 / 3.14159265358979323846 : 0.0);
        const unsigned shape = explicitGeometry ? pointStyle->customMarker : 3;
        if (shape == 0) painter.drawPoint(QPointF(0, 0));
        if (shape == 2) {
          painter.drawLine(QPointF(-half, 0), QPointF(half, 0));
          painter.drawLine(QPointF(0, -half), QPointF(0, half));
        }
        if (shape == 3) {
          painter.drawLine(QPointF(-half, -half), QPointF(half, half));
          painter.drawLine(QPointF(-half, half), QPointF(half, -half));
        }
        if (shape == 4) painter.drawLine(QPointF(0, -half), QPointF(0, half));
        if (explicitGeometry && (pointStyle->markerOverlay & 1u) != 0)
          painter.drawEllipse(QRectF(-half, -half, half * 2, half * 2));
        if (explicitGeometry && (pointStyle->markerOverlay & 2u) != 0)
          painter.drawRect(QRectF(-half, -half, half * 2, half * 2));
        painter.restore();
      }
    }
    if (labelVisible && state.surveyPointShowIdInViewport) {
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
    CadAnnotation annotation = state.cadAnnotations[i];
    if (CadAnnotationIsDimension(annotation)) {
      const auto& style = CadDimensionStyleForAnnotation(state, annotation);
      if (!annotation.ovHeight) annotation.plottedHeightInches = style.textSizeInches;
      if (!StyleFlag(style.props, "annotative"))
        annotation.plottedHeightInches *= static_cast<float>(StyleNumber(style.props, "overallScale", 1));
      if (!annotation.ovFont) {
        const auto* textStyle = TextStyles::Find(state.textStyles, style.textFont);
        annotation.fontFamily = textStyle ? textStyle->fontFamily : style.textFont;
      }
    } else if (const auto* style = TextStyles::Find(state.textStyles, annotation.styleName)) {
      // Native zero-height styles leave each entity's chosen height intact.
      const float height = annotation.plottedHeightInches;
      TextStyles::RebakeAnnotation(annotation, *style);
      if (style->heightInches == 0) annotation.plottedHeightInches = height;
    }
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
    QColor textColor = color;
    if (CadAnnotationIsDimension(annotation)) {
      const DimensionStyle& dimStyle = CadDimensionStyleForAnnotation(state, annotation);
      if (!dimStyle.textColor.empty() && dimStyle.textColor != "ByLayer" && dimStyle.textColor != "ByBlock") {
        float trgb[3]{1, 1, 1};
        CadColorResolveRgb(dimStyle.textColor, 1, 1, 1, trgb);
        textColor = QColor::fromRgbF(trgb[0], trgb[1], trgb[2]);
      }
    }
    const bool selected = std::any_of(state.selection.begin(), state.selection.end(), [&](const auto& value) {
      return value.type == SelectedEntity::Type::Annotation && value.index == static_cast<int>(i);
    });
    if (selected) color = textColor = QColor("#62aaff");
    QPen pen(color, 1.2); pen.setCosmetic(true);
    painter.setPen(pen); painter.setBrush(Qt::NoBrush);
    double x = annotation.insX, y = annotation.insY, rotation = annotation.rotationRad;
    const bool dimension = CadAnnotationIsDimension(annotation);
    if (dimension) {
      const DimensionStyle& dimStyle = CadDimensionStyleForAnnotation(state, annotation);
      CadDimStrokeParams parameters;
      parameters.modelUnitsPerPlottedInch = state.modelUnitsPerPlottedInch;
      parameters.arrowSizeInches = dimStyle.arrowSizeInches;
      parameters.arrowScale = state.viewportDimArrowScale;
      parameters.arrowType = dimStyle.arrowType;
      parameters.style = &dimStyle;
      CadDimWorldStrokes strokes;
      if (!CadDimBuildWorldStrokes(annotation, parameters, &strokes)) continue;
      for (const auto& segment : strokes.segs) {
        const bool extension = segment.kind == CadDimWorldSeg::Kind::Extension;
        painter.setPen(AnnotationStylePen(extension ? dimStyle.extLineColor :
            segment.kind == CadDimWorldSeg::Kind::Arrow ? dimStyle.arrowColor : dimStyle.dimLineColor,
            extension ? dimStyle.extLineType : dimStyle.dimLineType,
            StyleProperty(dimStyle.props, extension ? "extLineWeight" : "dimLineWeight"), layer, color, selected));
        painter.drawLine(project(segment.x0, segment.y0, annotation.insZ), project(segment.x1, segment.y1, annotation.insZ));
      }
      painter.setPen(AnnotationStylePen(dimStyle.arrowColor, "Continuous", "", layer, color, selected));
      painter.setBrush(painter.pen().color());
      for (const auto& arrow : strokes.arrows) painter.drawPolygon(QPolygonF{
          project(arrow.x0, arrow.y0, annotation.insZ), project(arrow.x1, arrow.y1, annotation.insZ),
          project(arrow.x2, arrow.y2, annotation.insZ)});
      painter.setBrush(Qt::NoBrush);
      painter.setPen(textColor);
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
    if (const auto* style = TextStyles::Find(state.textStyles, annotation.styleName))
      painter.scale(style->widthFactor * (StyleFlag(style->props, "backwards") ? -1 : 1),
                    StyleFlag(style->props, "upsideDown") ? -1 : 1);
    const auto drawDimensionTextBox = [&](const QRectF& rect) {
      if (!dimension) return;
      const auto& style = CadDimensionStyleForAnnotation(state, annotation);
      const QRectF frame = rect.adjusted(-fontPixels * .15, -fontPixels * .1, fontPixels * .15, fontPixels * .1);
      const auto fill = StyleProperty(style.props, "textFill", "None");
      if (fill != "None") painter.fillRect(frame, AnnotationStylePen(fill, "", "", layer, color, false).color());
      if (StyleFlag(style.props, "drawFrame") || StyleProperty(style.props, "tolMethod") == "Basic") painter.drawRect(frame);
    };
    if (dimension && cadfont::PreferShxStrokes(annotation.fontFamily, text)) {
      if (auto* font = Shx::Resolve(annotation.fontFamily); font && font->valid()) {
        const float labelWidth = Shx::MeasureWidthPx(*font, MtextRichFlattenToPlain(text), fontPixels);
        const float labelHeight = fontPixels * (1 + 1.4f * static_cast<float>(std::count(text.begin(), text.end(), '\n')));
        drawDimensionTextBox(QRectF(-labelWidth * .5, -labelHeight * .5, labelWidth, labelHeight));
        painter.translate(-labelWidth * .5, -labelHeight * .5);
        DrawStrokeText(painter, annotation, fontPixels, text, false);
        painter.restore(); continue;
      }
    }
    if (DrawStrokeText(painter, annotation, fontPixels, text, annotation.kind == CadAnnotation::Kind::Text)) {
      painter.restore(); continue;
    }
    QTextDocument document;
    PopulateQtText(document, fontPixels, text, annotation.fontFamily, dimension ? textColor : color);
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
      if (dimension) {
        drawDimensionTextBox(QRectF(-document.idealWidth() * .5, -document.size().height() * .5,
                                   document.idealWidth(), document.size().height()));
        painter.translate(-document.idealWidth() * 0.5, -document.size().height() * 0.5);
      }
      else painter.translate(0, -QFontMetricsF(document.defaultFont()).ascent());
    }
    document.drawContents(&painter);
    painter.restore();
  }
  if ((state.active == AppCommandState::Kind::DimAligned &&
       state.dimPhase == AppCommandState::DimPhase::WaitDimLinePt) ||
      (state.active == AppCommandState::Kind::DimLinear &&
       state.dimPhase == AppCommandState::DimPhase::WaitDimLinePt) ||
      (state.active == AppCommandState::Kind::DimAngular &&
       state.dimAngularPhase == AppCommandState::DimAngularPhase::WaitArc)) {
    CadAnnotation draft{};
    bool hasDraft = false;
    float cursorLocalX = 0.f;
    float cursorLocalY = 0.f;
    CadCoord::LocalFromWorld(state, static_cast<double>(state.uiCursorWorldX),
                             static_cast<double>(state.uiCursorWorldY), &cursorLocalX, &cursorLocalY);
    if (state.active == AppCommandState::Kind::DimAligned)
      hasDraft = CadDimAlignedBuildDraft(state, cursorLocalX, cursorLocalY, &draft);
    else if (state.active == AppCommandState::Kind::DimLinear)
      hasDraft = CadDimLinearBuildDraft(const_cast<AppCommandState&>(state), cursorLocalX, cursorLocalY,
                                        &draft);
    else
      hasDraft = CadDimAngularBuildDraft(state, cursorLocalX, cursorLocalY, &draft);
    if (hasDraft) {
      const DimensionStyle& dimStyle = state.activeDimensionStyle;
      std::string strokeColor = dimStyle.dimLineColor;
      if (strokeColor.empty() || strokeColor == "ByLayer" || strokeColor == "ByBlock") {
        if (!dimStyle.textColor.empty() && dimStyle.textColor != "ByLayer" && dimStyle.textColor != "ByBlock")
          strokeColor = dimStyle.textColor;
        else
          strokeColor = "#f0c75e";
      }
      float drgb[3]{1.f, 1.f, 1.f};
      CadColorResolveRgb(strokeColor, 1.f, 1.f, 1.f, drgb);
      QColor color = QColor::fromRgbF(drgb[0], drgb[1], drgb[2]);
      QColor textColor = color;
      if (!dimStyle.textColor.empty() && dimStyle.textColor != "ByLayer" && dimStyle.textColor != "ByBlock") {
        float trgb[3]{1.f, 1.f, 1.f};
        CadColorResolveRgb(dimStyle.textColor, 1.f, 1.f, 1.f, trgb);
        textColor = QColor::fromRgbF(trgb[0], trgb[1], trgb[2]);
      }
      CadDimStrokeParams parameters;
      parameters.modelUnitsPerPlottedInch = state.modelUnitsPerPlottedInch;
      parameters.arrowSizeInches = state.activeDimensionStyle.arrowSizeInches;
      parameters.arrowScale = state.viewportDimArrowScale;
      parameters.arrowType = state.activeDimensionStyle.arrowType;
      parameters.style = &state.activeDimensionStyle;
      CadDimWorldStrokes strokes;
      if (CadDimBuildWorldStrokes(draft, parameters, &strokes)) {
        painter.save();
        painter.setPen(QPen(color, 1.2));
        painter.setBrush(color);
        for (const auto& segment : strokes.segs)
          painter.drawLine(project(segment.x0, segment.y0, draft.insZ),
                           project(segment.x1, segment.y1, draft.insZ));
        for (const auto& arrow : strokes.arrows)
          painter.drawPolygon(QPolygonF{project(arrow.x0, arrow.y0, draft.insZ),
                                        project(arrow.x1, arrow.y1, draft.insZ),
                                        project(arrow.x2, arrow.y2, draft.insZ)});
        const double textWorld = CadAnnotationHeightWorld(draft, state.modelUnitsPerPlottedInch);
        const QPointF base = project(strokes.labelX, strokes.labelY, draft.insZ);
        const QPointF top = project(strokes.labelX, strokes.labelY + textWorld, draft.insZ);
        QFont font = painter.font();
        font.setPixelSize(std::clamp(qRound(std::hypot(top.x() - base.x(), top.y() - base.y())), 7, 80));
        painter.setFont(font);
        painter.setPen(textColor);
        painter.drawText(base, QString::fromUtf8(draft.text.c_str()));
        painter.restore();
      }
    }
  }
  for (std::size_t i = 0; i < state.cadMultileaders.size(); ++i) {
    CadMultileader ml = state.cadMultileaders[i];
    const auto* namedStyle = MultileaderStyles::Find(state.multileaderStyles, ml.styleName);
    const MultileaderStyle& style = namedStyle ? *namedStyle : state.activeMultileaderStyle;
    const float leaderArrow = std::max(style.arrowSizeInches, .001f) * std::max(state.modelUnitsPerPlottedInch, 1.e-6f);
    if (style.props.count("leaderType")) ml.splinePath = StyleProperty(style.props, "leaderType") == "Spline";
    if (StyleProperty(style.props, "arrowSymbol") == "None") ml.showArrow = false;
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
    if (!StyleProperty(style.props, "leaderColor").empty()) {
      float rgbStyle[3] = {color.redF(), color.greenF(), color.blueF()};
      CadColorResolveRgb(CadColorEffectiveStorage(StyleProperty(style.props, "leaderColor"), layer ? layer->color : "White"), rgbStyle[0], rgbStyle[1], rgbStyle[2], rgbStyle);
      color = QColor::fromRgbF(rgbStyle[0], rgbStyle[1], rgbStyle[2]);
    }
    QPen pen(color, 1.2); pen.setCosmetic(true);
    painter.setPen(pen); painter.setBrush(Qt::NoBrush);
    if (StyleProperty(style.props, "leaderType") != "None") DrawQtLeaderPath(painter, ml, ml.pathXyz, camera, width, height, leaderArrow);
    for (const auto& branch : ml.extraLeaderPaths)
      DrawQtLeaderPath(painter, ml, branch, camera, width, height, leaderArrow);
    if (!ml.label.text.empty() && StyleProperty(style.props, "contentType", "Mtext") == "Mtext") {
      CadAnnotation label = ml.label;
      if (!label.ovHeight) label.plottedHeightInches = style.textSizeInches;
      if (!label.ovFont) {
        const auto* textStyle = TextStyles::Find(state.textStyles, style.textFont);
        label.fontFamily = textStyle ? textStyle->fontFamily : style.textFont;
      }
      const double textWorld = std::max(label.plottedHeightInches * state.modelUnitsPerPlottedInch, 1.e-6f);
      const QPointF anchor = project(label.insX, label.insY, label.insZ);
      const QPointF cap = project(label.insX, label.insY + textWorld, label.insZ);
      const float pixels = static_cast<float>(std::hypot(cap.x() - anchor.x(), cap.y() - anchor.y()));
      if (pixels >= .5f) {
        painter.save(); painter.translate(anchor);
        if (!StyleProperty(style.props, "textColor").empty()) {
          float rgbText[3]{1, 1, 1};
          CadColorResolveRgb(CadColorEffectiveStorage(StyleProperty(style.props, "textColor"), layer ? layer->color : "White"), 1, 1, 1, rgbText);
          painter.setPen(QColor::fromRgbF(rgbText[0], rgbText[1], rgbText[2]));
        }
        QFont font(QString::fromUtf8(label.fontFamily.c_str())); font.setPixelSize(std::clamp(qRound(pixels), 1, 4096));
        painter.setFont(font);
        if (!DrawStrokeText(painter, label, pixels, label.text, true)) painter.drawText(QPointF(0, 0), QString::fromUtf8(label.text.c_str()));
        if (StyleFlag(style.props, "textFrame")) {
          const auto bounds = QFontMetricsF(font).boundingRect(QString::fromUtf8(label.text.c_str())).adjusted(-3, -3, 3, 3);
          painter.drawRect(bounds);
        }
        painter.restore();
      }
    }
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
  // TABLE lives in the dedicated cadTables store, so the annotation loop above never paints it.
  // Keep model-space tables in the same overlay pass as selectable annotations; otherwise picking
  // succeeds on geometry that the user cannot see.
  for (std::size_t ti = 0; ti < state.cadTables.size(); ++ti) {
    const CadTable& table = state.cadTables[ti];
    const EntityAttributes* attrs = ti < state.cadTableAttrs.size() ? &state.cadTableAttrs[ti] : nullptr;
    if (attrs && CadEntityIdHidden(&state.hiddenEntityIds, attrs->id))
      continue;
    const CadLayerRow* layer = nullptr;
    if (attrs) for (const auto& row : state.drawingLayerTable) if (row.name == attrs->layer) { layer = &row; break; }
    if (layer && (!layer->on || layer->frozen)) continue;
    const bool selected = std::any_of(state.selection.begin(), state.selection.end(), [ti](const SelectedEntity& e) {
      return e.type == SelectedEntity::Type::Table && e.index == static_cast<int>(ti);
    });
    const CadTableStyle* tableStyle = CadTableFindStyle(state.tableStyles, table.styleName);
    const std::map<std::string, std::string> noProperties;
    const auto& properties = tableStyle ? tableStyle->props : noProperties;
    const auto cellKind = [&](std::size_t ci) -> std::string {
      const auto row = ci / static_cast<std::size_t>(table.cols);
      return row == 0 && table.mergeTitleRow ? "Title" : row == static_cast<std::size_t>(table.mergeTitleRow ? 1 : 0) ? "Header" : "Data";
    };
    const CadLayerRow* layerForColor = layer;
    auto resolveTableColor = [&](const std::string& storage, float fallbackR, float fallbackG, float fallbackB) {
      float rgb[3] = {fallbackR, fallbackG, fallbackB};
      if (!storage.empty())
        CadColorResolveRgb(CadColorEffectiveStorage(storage, layerForColor ? layerForColor->color : "White"),
                           fallbackR, fallbackG, fallbackB, rgb);
      else if (attrs)
        CadColorResolveRgb(CadColorEffectiveStorage(attrs->color, layerForColor ? layerForColor->color : "White"),
                           fallbackR, fallbackG, fallbackB, rgb);
      return selected ? QColor("#62aaff") : QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    };
    const QColor gridColor = resolveTableColor(tableStyle ? tableStyle->gridLineColor : std::string{}, 1.f, 1.f, 1.f);
    const QColor borderColor = resolveTableColor(tableStyle ? tableStyle->borderColor : std::string{}, 1.f, 1.f, 1.f);
    QPen gridPen(gridColor, selected ? 1.8 : 1.2);
    gridPen.setCosmetic(true);
    painter.setPen(gridPen);
    std::vector<CadTableCellRect> cells;
    CadTableLayoutWorldCells(table, &cells);
    const auto cellProject = [&](float x, float y) {
      float wx = 0, wy = 0;
      CadTableLocalToWorld(table, x, y, &wx, &wy);
      return project(wx, wy, table.insZ);
    };
    for (std::size_t ci = 0; ci < cells.size(); ++ci) {
      const auto cell = CadTableLocalCellRect(table, static_cast<int>(ci));
      if (cell.x1 <= cell.x0 || cell.y1 <= cell.y0) continue;
      const auto tl = cellProject(cell.x0, cell.y0), tr = cellProject(cell.x1, cell.y0);
      const std::string kind = cellKind(ci);
      const auto fill = StyleProperty(properties, kind + ".fillColor", "None");
      painter.save();
      if (fill != "None") painter.setBrush(resolveTableColor(fill, 1, 1, 1));
      else painter.setBrush(Qt::NoBrush);
      if (!StyleFlag(properties, kind + ".bordersVisible", true) && !selected) painter.setPen(Qt::NoPen);
      else painter.setPen(QPen(resolveTableColor(StyleProperty(properties, kind + ".borderColor"), 1, 1, 1), gridPen.widthF()));
      painter.drawPolygon(QPolygonF{tl, tr, cellProject(cell.x1, cell.y1), cellProject(cell.x0, cell.y1)});
      painter.restore();
    }
    for (std::size_t ci = 0; ci < cells.size(); ++ci) {
      const auto cell = CadTableLocalCellRect(table, static_cast<int>(ci));
      if (cell.x1 <= cell.x0 || cell.y1 <= cell.y0) continue;
      if (ci >= table.cells.size() || table.cells[ci].empty()) continue;
      const auto tl = cellProject(cell.x0, cell.y0), tr = cellProject(cell.x1, cell.y0);
      const std::size_t row = ci / static_cast<std::size_t>(table.cols);
      const std::string& textColorStorage = tableStyle == nullptr ? std::string{} :
          row == 0 && table.mergeTitleRow ? tableStyle->titleTextColor :
          row == static_cast<std::size_t>(table.mergeTitleRow ? 1 : 0) ? tableStyle->headerTextColor :
                                                                             tableStyle->dataTextColor;
      const QColor textColor = resolveTableColor(textColorStorage, 1.f, 1.f, 1.f);
      const auto kind = cellKind(ci);
      const float priorHeight = row < table.rowTextHeightsInches.size() ? table.rowTextHeightsInches[row] : table.plottedHeightInches;
      const float textHeight = static_cast<float>(StyleNumber(properties, kind + ".textHeight", priorHeight));
      std::string family = table.fontFamily;
      const std::string styleName = StyleProperty(properties, kind + ".textStyle");
      if (const auto* style = TextStyles::Find(state.textStyles, styleName)) family = style->fontFamily;
      const float textWorld = std::max(textHeight * state.modelUnitsPerPlottedInch, 1.e-5f);
      const auto cap = cellProject(cell.x0, cell.y0 + textWorld);
      const float fontPx = static_cast<float>(std::hypot(cap.x() - tl.x(), cap.y() - tl.y()));
      if (fontPx < .5f) continue;
      QFont font(QString::fromUtf8(family.c_str()));
      font.setPixelSize(std::clamp(qRound(fontPx), 1, 4096));
      painter.save();
      painter.setFont(font);
      painter.setPen(textColor);
      const auto alignment = StyleProperty(properties, kind + ".alignment", table.mergeTitleRow && row == 0 ? "Middle Center" : "Top Left");
      const bool centerInCell = alignment.find("Center") != std::string::npos;
      const float boxWidth = static_cast<float>(std::hypot(tr.x() - tl.x(), tr.y() - tl.y()));
      const float pad = std::max(0.f, static_cast<float>(StyleNumber(properties, kind + ".horizontalMargin", 0)) * fontPx / textHeight);
      const float verticalPad = std::max(0.f, static_cast<float>(StyleNumber(properties, kind + ".verticalMargin", 0)) * fontPx / textHeight);
      const auto bottom = cellProject(cell.x0, cell.y1);
      const float boxHeight = static_cast<float>(std::hypot(bottom.x() - tl.x(), bottom.y() - tl.y()));
      const QPolygonF cellPoly{tl, tr, cellProject(cell.x1, cell.y1), cellProject(cell.x0, cell.y1)};
      painter.setClipRegion(QRegion(cellPoly.toPolygon()), Qt::IntersectClip);
      painter.translate(tl);
      painter.rotate(std::atan2(tr.y() - tl.y(), tr.x() - tl.x()) * 180.0 / 3.14159265358979323846);
      const auto plain = MtextRichFlattenToPlain(table.cells[ci]);
      float textWidth = static_cast<float>(QFontMetricsF(font).horizontalAdvance(QString::fromUtf8(plain.c_str())));
      if (auto* shx = Shx::Resolve(family); shx && shx->valid())
        textWidth = Shx::MeasureWidthPx(*shx, plain, fontPx);
      QString drawn = QString::fromUtf8(plain.c_str());
      if (textWidth > boxWidth - pad * 2.f)
        drawn = QFontMetricsF(font).elidedText(drawn, Qt::ElideRight, std::max(boxWidth - pad * 2.f, 1.f));
      const float left = centerInCell ? (boxWidth - textWidth) * .5f : alignment.find("Right") != std::string::npos ? boxWidth - pad - textWidth : pad;
      const float baseline = alignment.find("Middle") != std::string::npos ? (boxHeight + fontPx) * .5f :
          alignment.find("Bottom") != std::string::npos ? boxHeight - verticalPad : verticalPad + fontPx;
      painter.translate(left, baseline);
      painter.rotate(-StyleNumber(properties, kind + ".textAngle", 0));
      CadAnnotation annotation;
      annotation.fontFamily = family;
      if (!DrawStrokeText(painter, annotation, fontPx, drawn.toUtf8().constData(), true))
        painter.drawText(QPointF(0, 0), drawn);
      painter.restore();
    }
    if (!selected && tableStyle != nullptr && !tableStyle->borderColor.empty()) {
      QPen outlinePen(borderColor, 1.8);
      outlinePen.setCosmetic(true);
      painter.setPen(outlinePen);
      painter.setBrush(Qt::NoBrush);
      const auto corner = [&](float lx, float ly) { return cellProject(lx, ly); };
      const auto topLeft = corner(0.f, 0.f);
      const auto topRight = corner(table.width, 0.f);
      const auto bottomRight = corner(table.width, table.height);
      const auto bottomLeft = corner(0.f, table.height);
      painter.drawPolygon(QPolygonF{topLeft, topRight, bottomRight, bottomLeft});
    }
  }
  // TABLE remains in its own command state until insertion. Show the same layout
  // the user is about to place, anchored at the current cursor, so placement is
  // visible before the click instead of leaving a stale prompt as the only cue.
  if (state.active == AppCommandState::Kind::Table && state.tableStage == 3) {
    if (const CadTableStyle* style = CadTableFindStyle(state.tableStyles, state.tablePendingStyle)) {
      CadTable preview;
      CadTableStyle sizing = *style;
      sizing.columnWidthInches = state.tablePendingColumnWidthInches;
      sizing.rowHeightInches = state.tablePendingRowHeightInches;
      CadTableApplyStyleSize(&preview, sizing, state.tablePendingRows, state.tablePendingCols,
                             state.modelUnitsPerPlottedInch);
      preview.insX = state.uiCursorWorldX;
      preview.insY = state.uiCursorWorldY;
      preview.insZ = state.uiCursorWorldZ;
      const QColor color("#f0c75e");
      QPen pen(color, 1.5);
      pen.setCosmetic(true);
      painter.save();
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      std::vector<CadTableCellRect> cells;
      CadTableLayoutWorldCells(preview, &cells);
      const float textWorld = std::max(preview.plottedHeightInches * state.modelUnitsPerPlottedInch, 1.e-5f);
      const QPointF textBase = project(preview.insX, preview.insY, preview.insZ);
      const QPointF textTop = project(preview.insX, preview.insY - textWorld, preview.insZ);
      QFont previewFont(QString::fromUtf8(preview.fontFamily.c_str()));
      previewFont.setPixelSize(std::clamp(qRound(std::hypot(textTop.x() - textBase.x(),
                                                         textTop.y() - textBase.y()) * 0.8), 7, 80));
      painter.setFont(previewFont);
      for (const auto& cell : cells) {
        const QRectF rect(project(cell.x0, cell.y0, preview.insZ), project(cell.x1, cell.y1, preview.insZ));
        painter.drawRect(rect.normalized());
        painter.drawText(rect.normalized().adjusted(3, 1, -2, -1), Qt::AlignLeft | Qt::AlignVCenter, "Data");
      }
      painter.restore();
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

  if (!state.alignments.empty()) {
    painter.setPen(QColor("#20252a"));
    QFont stationFont(QStringLiteral("Segoe UI"), 9);
    painter.setFont(stationFont);
    const double textOffset = 3.0;
    for (const alignment::Alignment& al : state.alignments) {
      if (al.Empty()) continue;
      // The label set decides the station and equation TEXT; the style's ticks, markers and arrows draw without one.
      const AlignmentLabelSet* ls = AlignmentLabelSets::Resolve(state.alignmentLabelSets, al.labelSet);
      const std::vector<alignment::StationTickLabel> ticks =
          ls != nullptr ? alignment::CollectMajorMinorStationTicks(al, ls->majorInterval, ls->minorInterval,
                                                                   ls->majorStationsOn, ls->minorStationsOn)
                        : std::vector<alignment::StationTickLabel>{};
      // REQ-491: the style decides what is drawn for this alignment. A missing style (already logged at load) reads as Standard.
      const AlignmentStyle* sty = AlignmentStyles::Resolve(state.alignmentStyles, al.style);
      const bool showLabels = sty == nullptr || sty->labelsVisible;
      const auto styleColor = [&](const SurfaceComponentStyle* comp, QColor fallback) {
        if (comp != nullptr && comp->color.size() == 7 && comp->color[0] == '#') return QColor(QString::fromStdString(comp->color));
        return fallback;
      };
      if (showLabels && ls != nullptr) for (const alignment::StationTickLabel& tick : ticks) {
        const alignment::Point2 p = al.GetPointAtStation(tick.rawStation);
        const double az = al.GetBearingAtStation(tick.rawStation);
        const double lx = p.x + textOffset * std::cos(az);
        const double ly = p.y - textOffset * std::sin(az);
        const std::string text = alignment::FormatStation(tick.displayedStation);
        painter.drawText(project(lx, ly, 0.0), QString::fromStdString(text));
      }
      // REQ-492.1 / .2: an equation label at each station equation, showing the back and ahead displayed values.
      if (ls != nullptr && ls->equationsOn && showLabels) {
        for (const alignment::EquationLabel& eq : alignment::CollectEquationLabels(al)) {
          const alignment::Point2 p = al.GetPointAtStation(eq.rawStation);
          const double az = al.GetBearingAtStation(eq.rawStation);
          const std::string text = "EQ " + alignment::FormatStation(eq.backDisplayed) + " = " + alignment::FormatStation(eq.aheadDisplayed);
          painter.drawText(project(p.x + textOffset * std::cos(az), p.y - textOffset * std::sin(az), 0.0), QString::fromStdString(text));
        }
      }
      // REQ-492.4: station/offset labels, fixed or relative, drawn where they resolve on the current geometry.
      for (const alignment::StationOffsetLabel& sl : al.stationLabels) {
        if (!showLabels) break;
        const alignment::StationOffsetLabelPlacement placed = alignment::PlaceStationOffsetLabel(al, sl, state.displayLinearPrecision);
        if (!placed.onAlignment) continue;
        painter.drawText(project(placed.point.x, placed.point.y, 0.0), QString::fromStdString(placed.text));
      }
      if (sty != nullptr && sty->ticks.visible) {  // REQ-491 station ticks, across the alignment at its label-set stations
        painter.setPen(QPen(styleColor(&sty->ticks, QColor("#20252a")), 1));
        for (const alignment::StationTickLabel& tick : ticks) {
          const alignment::Point2 p = al.GetPointAtStation(tick.rawStation);
          const double az = al.GetBearingAtStation(tick.rawStation);
          const double nx = std::cos(az), ny = -std::sin(az);  // right of travel
          const double len = tick.major ? sty->majorTickLength : sty->minorTickLength;
          double a0 = -0.5, a1 = 0.5;  // side 0: both sides of the line
          if (sty->tickSide == 1) { a0 = 0.0; a1 = -1.0; }  // left
          if (sty->tickSide == 2) { a0 = 0.0; a1 = 1.0; }   // right
          painter.drawLine(project(p.x + a0 * len * nx, p.y + a0 * len * ny, 0.0),
                           project(p.x + a1 * len * nx, p.y + a1 * len * ny, 0.0));
        }
      }
      if (sty != nullptr && sty->markers.visible) {  // REQ-491 geometry point markers: a cross per point, by code
        painter.setPen(QPen(styleColor(&sty->markers, QColor("#20252a")), 1));
        const double h = sty->markerHalfSize;
        for (const alignment::GeometryPoint& gp : al.GeometryPoints()) {
          using GC = alignment::GeometryPointCode;
          if (gp.code == GC::LineMidPt || gp.code == GC::CurveMidPt || gp.code == GC::CurveCenter) continue;
          const alignment::Point2 q = gp.point;
          painter.drawLine(project(q.x - h, q.y, 0.0), project(q.x + h, q.y, 0.0));
          painter.drawLine(project(q.x, q.y - h, 0.0), project(q.x, q.y + h, 0.0));
        }
      }
      if (sty != nullptr && sty->arrows.visible && !al.Empty()) {  // REQ-491 direction arrow at the middle of the alignment
        painter.setPen(QPen(styleColor(&sty->arrows, QColor("#20252a")), 1));
        const double mid = 0.5 * (al.StartStation() + al.EndStation());
        const alignment::Point2 p = al.GetPointAtStation(mid);
        const double az = al.GetBearingAtStation(mid);
        const double tx = std::sin(az), ty = std::cos(az);
        const double L = sty->arrowLength;
        const alignment::Point2 tail{p.x - 0.5 * L * tx, p.y - 0.5 * L * ty};
        const alignment::Point2 tip{p.x + 0.5 * L * tx, p.y + 0.5 * L * ty};
        painter.drawLine(project(tail.x, tail.y, 0.0), project(tip.x, tip.y, 0.0));
        for (const double turn : {0.5236, -0.5236}) {  // the two barbs, 30 degrees either side of the back-pointing direction
          const double c = std::cos(turn), sn = std::sin(turn);
          const double bx = -tx * c + ty * sn;  // (-tx, -ty) rotated by turn
          const double by = -tx * sn - ty * c;
          painter.drawLine(project(tip.x, tip.y, 0.0), project(tip.x + 0.25 * L * bx, tip.y + 0.25 * L * by, 0.0));
        }
      }
      if (const alignment::AlignmentCheckSet* checkSet =
            alignment::FindCheckSet(state.alignmentCheckSets, AlignmentStyles::EffectiveCheckSetName(state.alignmentStyles, al))) {
        // REQ-502: a red flag stands on each entity that fails its design check, whatever the style shows.
        painter.setPen(QPen(QColor("#c0392b"), 1.5));
        painter.setBrush(QColor("#c0392b"));
        for (const alignment::DesignFinding& fd : alignment::CheckDesign(al, *checkSet, state.displayLinearPrecision)) {
          const int idx = fd.entity - 1;
          const double mid = 0.5 * (al.EntityStartStation(idx) + al.EntityEndStation(idx));
          const alignment::Point2 p = al.GetPointAtStation(mid);
          const double h = 4.0;  // flag height, in drawing units
          painter.drawLine(project(p.x, p.y, 0.0), project(p.x, p.y + h, 0.0));
          painter.drawPolygon(QPolygonF({project(p.x, p.y + h, 0.0), project(p.x + 0.7 * h, p.y + 0.8 * h, 0.0),
                                         project(p.x, p.y + 0.6 * h, 0.0)}));
        }
        painter.setBrush(Qt::NoBrush);
      }
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
