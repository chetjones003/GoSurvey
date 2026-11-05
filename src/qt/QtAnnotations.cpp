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
#include "alignment/AlignmentTables.hpp"
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
#include <limits>

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
                     const std::string& text, bool baseline, int attachment = 1) {
  if (!cadfont::PreferShxStrokes(annotation.fontFamily, text)) return false;
  auto* font = Shx::Resolve(annotation.fontFamily);
  if (!font || !font->valid()) return false;
  const float scale = pixels / font->capHeight();
  const int attach = std::clamp(attachment, 1, 9) - 1;
  const int column = attach % 3;
  const int row = attach / 3;
  const float width = Shx::MeasureWidthPx(*font, MtextRichFlattenToPlain(text), pixels);
  float x = column == 1 ? -width * 0.5f : column == 2 ? -width : 0.f;
  float y = baseline ? 0.f : row == 0 ? pixels : row == 1 ? pixels * 0.5f : 0.f;
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
    if (!DrawStrokeText(painter, label, pixels, label.text, true, label.mtextAttach)) {
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

namespace {

// REQ-492 / REQ-495.2: the alignment overlay (ticks, markers, arrows, labels, design flags) is placed in screen space.
struct AlignmentView {
  const Camera& camera;
  float width = 0.f;
  float height = 0.f;
  double textPx = 0.0;  ///< Label text height on screen (REQ-492.5: a plotted height, so it follows the zoom).
  [[nodiscard]] QPointF Project(alignment::Point2 p) const {
    float sx = 0.f, sy = 0.f;
    camera.WorldToScreen(p.x, p.y, 0.0, width, height, &sx, &sy);
    return QPointF(sx, sy);
  }
};

// An alignment station on screen: the point, unit screen vectors along travel and to its left, and the screen pixels
// per drawing unit there.
struct AlignmentScreenFrame {
  QPointF point;
  QPointF along{1.0, 0.0};
  QPointF left{0.0, -1.0};
  double pixelsPerUnit = 0.0;
};

AlignmentScreenFrame FrameAt(const AlignmentView& view, const alignment::Alignment& al, double rawStation) {
  const alignment::Point2 p = al.GetPointAtStation(rawStation);
  const double az = al.GetBearingAtStation(rawStation);
  AlignmentScreenFrame f;
  f.point = view.Project(p);
  // Bearings run clockwise from north: travel is (sin, cos) and its left is (-cos, sin), one drawing unit long.
  const QPointF along = view.Project({p.x + std::sin(az), p.y + std::cos(az)}) - f.point;
  const QPointF left = view.Project({p.x - std::cos(az), p.y + std::sin(az)}) - f.point;
  const double alongLength = std::hypot(along.x(), along.y());
  const double leftLength = std::hypot(left.x(), left.y());
  assert(std::isfinite(alongLength));
  assert(std::isfinite(leftLength));
  if (alongLength > 1.e-9) f.along = along / alongLength;
  if (leftLength > 1.e-9) f.left = left / leftLength;
  f.pixelsPerUnit = leftLength;
  return f;
}

QColor ResolvedColor(const std::string& storage, const std::string& layerColor) {
  float rgb[3]{1.f, 1.f, 1.f};
  CadColorResolveRgb(CadColorEffectiveStorage(storage.empty() ? std::string("ByLayer") : storage, layerColor), 1.f, 1.f,
                     1.f, rgb);
  return QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
}

// Splits UTF-8 text into runs of ASCII and of other characters, in order: a stroke font can draw only the first kind.
std::vector<std::pair<bool, std::string>> AsciiRuns(const std::string& text) {
  std::vector<std::pair<bool, std::string>> runs;
  for (const char c : text) {
    const bool ascii = (static_cast<unsigned char>(c) & 0x80u) == 0;
    if (runs.empty() || runs.back().first != ascii) runs.emplace_back(ascii, std::string{});
    runs.back().second.push_back(c);
  }
  return runs;
}

// The face one kind of alignment label is written in (REQ-492.5): its label style's text style, plotted height and
// colour. In an SHX text style the ASCII characters are stroked in that font and any other character (°, Δ, ∞) in a
// TrueType face of the same cap height, because the stroke fonts have no such glyphs (CadFontName.hpp).
struct LabelFace {
  QColor color;
  double px = 0.0;  ///< Cap height on screen.
  Shx::Font* stroke = nullptr;  ///< The SHX font, when the text style names one that loads.
  std::string strokeFamily;
  QFont ttf;
  // REQ-492.5 formatting from the label style: every line is written as prefix + text + suffix; numbers use `linear`
  // decimals and stations `station`; a View-oriented label stays level on the screen.
  std::string prefix, suffix;
  bool viewOriented = false;
  int linear = 2;
  alignment::StationFormat station;

  [[nodiscard]] bool Drawable() const { return std::isfinite(px) && px >= 2.0; }
  [[nodiscard]] double RunWidth(bool ascii, const std::string& run) const {
    if (ascii && stroke != nullptr) return Shx::MeasureWidthPx(*stroke, run, static_cast<float>(px));
    return QFontMetricsF(ttf).horizontalAdvance(QString::fromUtf8(run.c_str()));
  }
  [[nodiscard]] double Width(const std::string& text) const {
    double w = 0.0;
    for (const auto& [ascii, run] : AsciiRuns(prefix + text + suffix)) w += RunWidth(ascii, run);
    return w;
  }
  // Writes `text` with its baseline starting at `start`, in the painter's current transform.
  void Draw(QPainter& painter, QPointF start, const std::string& line) const {
    const std::string text = prefix + line + suffix;
    QPen pen(color, 1.0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setFont(ttf);
    CadAnnotation strokeText;
    strokeText.fontFamily = strokeFamily;
    double x = start.x();
    for (const auto& [ascii, run] : AsciiRuns(text)) {
      bool drawn = false;
      if (ascii && stroke != nullptr) {
        painter.save();
        painter.translate(x, start.y());
        drawn = DrawStrokeText(painter, strokeText, static_cast<float>(px), run, true);
        painter.restore();
      }
      if (!drawn) painter.drawText(QPointF(x, start.y()), QString::fromUtf8(run.c_str()));
      x += RunWidth(ascii, run);
    }
  }
};

LabelFace MakeLabelFace(const AppCommandState& state, const std::string& styleName, double pixelsPerUnit,
                        const std::string& layerColor) {
  assert(std::isfinite(pixelsPerUnit));
  assert(pixelsPerUnit >= 0.0);
  const AlignmentLabelStyle& style = AlignmentLabelStyles::Resolve(state.alignmentLabelStyles, styleName);
  LabelFace face;
  face.prefix = style.prefix;
  face.suffix = style.suffix;
  face.viewOriented = style.viewOriented;
  face.linear = AlignmentLabelStyles::Precision(style, state.displayLinearPrecision);
  face.station = AlignmentLabelStyles::StationFormatFor(style, state.alignmentStationFormat);
  face.color = ResolvedColor(style.color, layerColor);
  face.px = AlignmentLabelStyles::TextHeight(style, state.modelUnitsPerPlottedInch) * pixelsPerUnit;
  const TextStyle* textStyle = TextStyles::Find(state.textStyles, style.textStyle);
  const std::string family = TextStyles::EffectiveFontFamily(textStyle != nullptr ? textStyle->fontFamily : std::string{});
  if (cadfont::IsShxFontName(family)) {
    Shx::Font* font = Shx::Resolve(family);
    if (font != nullptr && font->valid()) {
      face.stroke = font;
      face.strokeFamily = family;
    }
  }
  face.ttf = QFont(QString::fromUtf8(cadfont::IsShxFontName(family) ? "Arial" : family.c_str()));
  // A TrueType pixel size is the em height; a capital is about 0.7 em, and a CAD text height is the capital's height.
  face.ttf.setPixelSize(std::clamp(qRound(face.px / 0.7), 1, 4096));
  return face;
}

// Draws `text` along the screen direction `along`, turned so it never reads upside down. `start` is where the text
// begins, or its middle when `centered`. `lift` moves the baseline off the anchor, in pixels, towards the text's top.
void DrawTextAlong(QPainter& painter, const LabelFace& face, QPointF start, QPointF along, const std::string& text,
                   bool centered, double lift) {
  assert(std::isfinite(start.x()) && std::isfinite(start.y()));
  assert(std::isfinite(along.x()) && std::isfinite(along.y()));
  const double textWidth = face.Width(text);
  double angle = face.viewOriented ? 0.0 : std::atan2(along.y(), along.x()) * 180.0 / alignment::kPi;
  double x = centered ? -0.5 * textWidth : 0.0;
  if (face.viewOriented && !centered && along.x() < 0.0) x = -textWidth;  // still reads away from its anchor
  if (angle > 90.0 || angle < -90.0) {
    angle += 180.0;
    if (!centered) x = -textWidth;
  }
  painter.save();
  painter.translate(start);
  painter.rotate(angle);
  face.Draw(painter, QPointF(x, -lift), text);
  painter.restore();
}

// The screen boxes already claimed by an alignment's labels (D-2026-11-04-e). Labels are placed one after another; each
// takes the first of its candidate positions whose box is free, as Civil 3D's dragged labels would be laid out by hand.
class LabelBoxes {
 public:
  [[nodiscard]] bool Free(const QPolygonF& box) const { return Overlaps(box) == 0; }
  [[nodiscard]] std::size_t Overlaps(const QPolygonF& box) const {
    return static_cast<std::size_t>(
        std::count_if(taken_.begin(), taken_.end(), [&](const QPolygonF& t) { return t.intersects(box); }));
  }
  void Take(QPolygonF box) { taken_.push_back(std::move(box)); }

 private:
  std::vector<QPolygonF> taken_;
};

// The box of a band of text beside the line, parallel to it: `width` long, from `nearPx` to `farPx` off the line.
QPolygonF BesideBox(const AlignmentScreenFrame& f, double side, double nearPx, double farPx, double width) {
  assert(farPx >= nearPx);
  assert(width >= 0.0);
  const QPointF a = f.along * (0.5 * width);
  const QPointF n0 = f.left * side * nearPx;
  const QPointF n1 = f.left * side * farPx;
  return QPolygonF({f.point - a + n0, f.point + a + n0, f.point + a + n1, f.point - a + n1});
}

// Text centred `offset` pixels beside the alignment and parallel to it (stations, segments, equations). `side` is +1
// for the left of travel, -1 for the right.
void DrawTextBeside(QPainter& painter, const LabelFace& face, const AlignmentScreenFrame& f, double offset,
                    const std::string& text, double side = 1.0) {
  assert(offset >= 0.0);
  assert(side == 1.0 || side == -1.0);
  if (text.empty() || !face.Drawable()) return;
  DrawTextAlong(painter, face, f.point + f.left * side * (offset + 0.5 * face.px), f.along, text, true, -0.5 * face.px);
}

// Lines of text stacked beside the line at the first free row, from `firstPx` outward, trying `sides` in order. When no
// row is free, the one overlapping the fewest labels is used. Claims the box it uses.
void PlaceStack(QPainter& painter, const LabelFace& face, const AlignmentScreenFrame& f,
                const std::vector<std::string>& lines, const std::vector<double>& sides, double firstPx, LabelBoxes* boxes) {
  assert(boxes != nullptr);
  assert(!sides.empty());
  if (lines.empty() || !face.Drawable()) return;
  double width = 0.0;
  for (const std::string& line : lines) width = std::max(width, face.Width(line));
  const double pad = 0.3 * face.px, step = 1.4 * face.px;
  const double depth = step * static_cast<double>(lines.size() - 1) + face.px;
  constexpr int kRows = 5;  // rows tried per side; alignment::LabelledBounds leaves room for them
  double side = sides.front(), offset = firstPx;
  std::size_t fewest = std::numeric_limits<std::size_t>::max();
  for (int row = 0; row < kRows && fewest > 0; ++row) {
    for (const double s : sides) {
      const double near = firstPx + step * row;
      const std::size_t overlaps = boxes->Overlaps(BesideBox(f, s, near - pad, near + depth + pad, width + 2.0 * pad));
      if (overlaps >= fewest) continue;
      fewest = overlaps;
      side = s;
      offset = near;
      if (fewest == 0) break;
    }
  }
  boxes->Take(BesideBox(f, side, offset - pad, offset + depth + pad, width + 2.0 * pad));
  // Lines read top to bottom on screen: when the stack grows upward, its first line is the outermost.
  const bool upward = (f.left * side).y() < 0.0;
  const size_t n = lines.size();
  for (size_t k = 0; k < n; ++k)
    DrawTextBeside(painter, face, f, offset + step * static_cast<double>(upward ? n - 1 - k : k), lines[k], side);
}

// A leader across the alignment with the text written along it, as Civil 3D draws geometry-point and design-speed
// labels. `side` is +1 for the left of travel, -1 for the right. Claims the leader's box.
void DrawLeaderLabel(QPainter& painter, const LabelFace& face, const AlignmentScreenFrame& f, double side,
                     const std::string& text, LabelBoxes* boxes) {
  assert(side == 1.0 || side == -1.0);
  assert(boxes != nullptr);
  if (text.empty() || !face.Drawable()) return;
  const QPointF out = f.left * side;
  const QPointF across(-out.y(), out.x());
  const double textStart = alignment::kLeaderTextStartHeights * face.px;
  const QPointF p0 = f.point + out * (alignment::kLeaderGapHeights * face.px);
  const QPointF p1 = f.point + out * (textStart + face.Width(text) + alignment::kLeaderOvershootHeights * face.px);
  const QPointF h = across * (1.4 * face.px);
  boxes->Take(QPolygonF({p0 - h, p1 - h, p1 + h, p0 + h}));
  QPen pen(face.color, 1.0);
  pen.setCosmetic(true);
  painter.setPen(pen);
  painter.drawLine(p0, p1);
  DrawTextAlong(painter, face, f.point + out * textStart, out, text, false, 0.25 * face.px);
}

// REQ-492.1/.2 major station text (minor stations are ticks only, D-2026-11-04-c) and equation labels. Station text
// sits left of the line, as Civil 3D draws it, unless something is already there (D-2026-11-04-e).
void DrawAlignmentStationText(QPainter& painter, const AppCommandState& state, const AlignmentView& view,
                              const alignment::Alignment& al, const std::vector<alignment::StationTickLabel>& ticks,
                              const AlignmentLabelSet& ls, double majorTickLength, const std::string& layerColor,
                              LabelBoxes* boxes) {
  assert(ls.majorInterval > 0.0);
  assert(boxes != nullptr);
  const double ppu = FrameAt(view, al, al.StartStation()).pixelsPerUnit;
  const double rowPx = 0.5 * majorTickLength * ppu;
  const LabelFace major = MakeLabelFace(state, ls.majorStyle, ppu, layerColor);
  for (const alignment::StationTickLabel& tick : ticks) {
    if (!tick.major) continue;
    PlaceStack(painter, major, FrameAt(view, al, tick.rawStation), {alignment::FormatStation(tick.displayedStation, major.station)},
               {1.0, -1.0}, rowPx + 0.3 * major.px, boxes);
  }
  if (!ls.equationsOn) return;
  const LabelFace equation = MakeLabelFace(state, ls.equationStyle, ppu, layerColor);
  for (const alignment::EquationLabel& eq : alignment::CollectEquationLabels(al)) {
    const std::string text = "EQ " + alignment::FormatStation(eq.backDisplayed, equation.station) + " = " +
                             alignment::FormatStation(eq.aheadDisplayed, equation.station);
    PlaceStack(painter, equation, FrameAt(view, al, eq.rawStation), {text}, {1.0, -1.0}, rowPx + 0.3 * equation.px, boxes);
  }
}

// A line's bearing goes above it (left of travel) and its length below, as Civil 3D draws them. When either is in the
// way of another label, both are stacked at the first free row on either side (D-2026-11-04-e).
void DrawLineLabel(QPainter& painter, const LabelFace& face, const AlignmentScreenFrame& f,
                   const std::vector<std::string>& lines, LabelBoxes* boxes) {
  assert(lines.size() == 2);
  assert(boxes != nullptr);
  if (!face.Drawable()) return;
  const double pad = 0.3 * face.px, inner = 0.4 * face.px;
  const QPolygonF above = BesideBox(f, 1.0, inner - pad, inner + face.px + pad, face.Width(lines[0]) + 2.0 * pad);
  const QPolygonF below = BesideBox(f, -1.0, inner - pad, inner + face.px + pad, face.Width(lines[1]) + 2.0 * pad);
  if (boxes->Free(above) && boxes->Free(below)) {
    boxes->Take(above);
    boxes->Take(below);
    DrawTextBeside(painter, face, f, inner, lines[0], 1.0);
    DrawTextBeside(painter, face, f, inner, lines[1], -1.0);
    return;
  }
  PlaceStack(painter, face, f, lines, {-1.0, 1.0}, inner, boxes);
}

// REQ-492.3/.5: a label at the middle of each segment once the alignment's segments are labelled. A curve or spiral
// stacks its values on the inside of the turn, as Civil 3D does, moving out or across when that is taken.
void DrawAlignmentSegmentText(QPainter& painter, const AppCommandState& state, const AlignmentView& view,
                              const alignment::Alignment& al, const LabelFace& face, LabelBoxes* boxes) {
  assert(!al.Empty());
  assert(state.displayLinearPrecision >= 0);
  const AngleDisplaySettings angles = CadAngleDisplaySettings(state);
  const int n = static_cast<int>(al.Entities().size());
  const std::vector<std::string> tags = alignment::ShowsTagLabels(al) ? alignment::SegmentTags(al) : std::vector<std::string>{};
  for (int i = 0; i < n; ++i) {
    if (!alignment::SegmentIsLabelled(al, i)) continue;  // REQ-492.3 single segment labels
    const alignment::Entity& e = al.Entities()[static_cast<size_t>(i)];
    const double mid = 0.5 * (al.EntityStartStation(i) + al.EntityEndStation(i));
    const AlignmentScreenFrame f = FrameAt(view, al, mid);
    if (!tags.empty()) {  // REQ-493.1: a table lists these segments, so each label is its tag
      PlaceStack(painter, face, f, {tags[static_cast<size_t>(i)]}, {1.0, -1.0}, 0.6 * face.px, boxes);
      continue;
    }
    alignment::CurveData cd;
    alignment::SpiralData sd;
    std::vector<std::string> lines;
    if (e.kind == alignment::EntityKind::Line) {
      DrawLineLabel(painter, face, f, alignment::LineLabelLines(e, face.linear, angles), boxes);
      continue;
    }
    if (e.kind == alignment::EntityKind::Arc && al.CurveDataFor(i, &cd))
      lines = alignment::CurveLabelLines(cd, face.linear, angles);
    else if (e.kind == alignment::EntityKind::Spiral && alignment::SpiralDataFor(i, al, &sd))
      lines = alignment::SpiralLabelLines(sd, face.linear);
    const double inside = e.right ? -1.0 : 1.0;
    PlaceStack(painter, face, f, lines, {inside, -inside}, 0.6 * face.px, boxes);
  }
}

// REQ-492: every label an alignment's label set and segment flag ask for, each in its label style (REQ-492.5).
void DrawAlignmentLabels(QPainter& painter, const AppCommandState& state, const AlignmentView& view,
                         const alignment::Alignment& al, const std::vector<alignment::StationTickLabel>& ticks,
                         const AlignmentLabelSet& ls, const AlignmentStyle* sty, const std::string& layerColor,
                         LabelBoxes* claimed) {
  assert(!al.Empty());
  assert(claimed != nullptr);
  const double ppu = FrameAt(view, al, al.StartStation()).pixelsPerUnit;
  // Leaders first (they stay where Civil 3D puts them), then the long segment labels, then the short station text,
  // each stepping around what is already placed (D-2026-11-04-e).
  LabelBoxes& boxes = *claimed;
  if (ls.geometryPointsOn) {  // REQ-492.1: the first Geometry Points item, then any further ones in their own styles
    std::vector<std::pair<const std::string*, const std::vector<std::string>*>> items{{&ls.geometryStyle, &ls.geometryCodes}};
    for (const AlignmentGeometryLabelItem& item : ls.extraGeometryItems) items.emplace_back(&item.style, &item.codes);
    for (const auto& [styleName, codes] : items) {
      const LabelFace face = MakeLabelFace(state, *styleName, ppu, layerColor);
      for (const alignment::GeometryPointLabel& gp : alignment::CollectGeometryPointLabels(al, face.station))
        if (AlignmentLabelStyles::ItemLabelsCode(*codes, alignment::GeometryPointAbbreviation(gp.code)))
          DrawLeaderLabel(painter, face, FrameAt(view, al, gp.internalStation), 1.0, gp.text, &boxes);
    }
  }
  if (ls.designSpeedsOn) {
    const LabelFace face = MakeLabelFace(state, ls.speedStyle, ppu, layerColor);
    const bool metric = state.drawingInsUnits == 4 || state.drawingInsUnits == 6;
    for (const alignment::DesignSpeedLabel& d : alignment::CollectDesignSpeedLabels(al, metric, face.station))
      DrawLeaderLabel(painter, face, FrameAt(view, al, d.internalStation), -1.0, d.text, &boxes);
  }
  if (alignment::HasSegmentLabels(al))
    DrawAlignmentSegmentText(painter, state, view, al, MakeLabelFace(state, ls.segmentStyle, ppu, layerColor), &boxes);
  DrawAlignmentStationText(painter, state, view, al, ticks, ls, sty != nullptr ? sty->majorTickLength : 0.0, layerColor,
                           &boxes);
  // REQ-492.4: station/offset labels, fixed or relative, drawn where they resolve on the current geometry.
  const LabelFace offsetFace = MakeLabelFace(state, ls.offsetStyle, ppu, layerColor);
  for (const alignment::StationOffsetLabel& sl : al.stationLabels) {
    const alignment::StationOffsetLabelPlacement placed =
        alignment::PlaceStationOffsetLabel(al, sl, offsetFace.linear, offsetFace.station);
    if (placed.onAlignment && offsetFace.Drawable()) offsetFace.Draw(painter, view.Project(placed.point), placed.text);
  }
}

// REQ-491 Markers tab: one geometry point marker in its shape, `halfSize` drawing units across each way.
void DrawAlignmentMarker(QPainter& painter, const AlignmentView& view, alignment::Point2 q, const AlignmentMarkerSpec& m) {
  assert(std::isfinite(q.x) && std::isfinite(q.y));
  assert(m.halfSize >= 0.0);
  const double h = m.halfSize;
  switch (m.shape) {
    case AlignmentMarkerShape::Cross:
      painter.drawLine(view.Project({q.x - h, q.y}), view.Project({q.x + h, q.y}));
      painter.drawLine(view.Project({q.x, q.y - h}), view.Project({q.x, q.y + h}));
      return;
    case AlignmentMarkerShape::X:
      painter.drawLine(view.Project({q.x - h, q.y - h}), view.Project({q.x + h, q.y + h}));
      painter.drawLine(view.Project({q.x - h, q.y + h}), view.Project({q.x + h, q.y - h}));
      return;
    case AlignmentMarkerShape::Circle: {
      const QPointF c = view.Project(q);
      const double r = std::hypot(view.Project({q.x + h, q.y}).x() - c.x(), view.Project({q.x + h, q.y}).y() - c.y());
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(c, r, r);
      return;
    }
    case AlignmentMarkerShape::Square:
      painter.setBrush(Qt::NoBrush);
      painter.drawPolygon(QPolygonF({view.Project({q.x - h, q.y - h}), view.Project({q.x + h, q.y - h}),
                                     view.Project({q.x + h, q.y + h}), view.Project({q.x - h, q.y + h})}));
      return;
    case AlignmentMarkerShape::None: return;
  }
}

// REQ-491 station ticks across the alignment at its label-set stations, and geometry point crosses.
void DrawAlignmentTicksAndMarkers(QPainter& painter, const AlignmentView& view, const alignment::Alignment& al,
                                  const std::vector<alignment::StationTickLabel>& ticks, const AlignmentStyle& sty,
                                  const std::string& layerColor) {
  assert(sty.majorTickLength >= 0.0 && sty.minorTickLength >= 0.0);
  assert(sty.markerHalfSize >= 0.0);
  if (sty.ticks.visible) {
    painter.setPen(QPen(ResolvedColor(sty.ticks.color, layerColor), 1));
    double a0 = -0.5, a1 = 0.5;  // side 0: both sides of the line
    if (sty.tickSide == 1) { a0 = 0.0; a1 = -1.0; }  // left
    if (sty.tickSide == 2) { a0 = 0.0; a1 = 1.0; }   // right
    for (const alignment::StationTickLabel& tick : ticks) {
      const alignment::Point2 p = al.GetPointAtStation(tick.rawStation);
      const double az = al.GetBearingAtStation(tick.rawStation);
      const double nx = std::cos(az), ny = -std::sin(az);  // right of travel
      const double len = tick.major ? sty.majorTickLength : sty.minorTickLength;
      painter.drawLine(view.Project({p.x + a0 * len * nx, p.y + a0 * len * ny}),
                       view.Project({p.x + a1 * len * nx, p.y + a1 * len * ny}));
    }
  }
  if (!sty.markers.visible) return;
  const QColor styleColor = ResolvedColor(sty.markers.color, layerColor);
  for (const alignment::GeometryPoint& gp : al.GeometryPoints()) {
    using GC = alignment::GeometryPointCode;
    // Only points on the line are marked; a curve's PI and centre lie off it, as in Civil 3D's default styles.
    if (gp.code == GC::LineMidPt || gp.code == GC::CurveMidPt || gp.code == GC::CurveCenter || gp.code == GC::CurvePI ||
        gp.code == GC::SpiralPI)
      continue;
    const std::string code = alignment::GeometryPointAbbreviation(gp.code);
    const auto spec = std::find_if(sty.codeMarkers.begin(), sty.codeMarkers.end(),
                                   [&](const AlignmentMarkerSpec& m) { return m.code == code; });
    AlignmentMarkerSpec marker{code, AlignmentMarkerShape::Cross, sty.markerHalfSize, "ByLayer"};
    if (spec != sty.codeMarkers.end()) marker = *spec;
    const bool byLayer = marker.color.empty() || marker.color == "ByLayer";
    painter.setPen(QPen(byLayer ? styleColor : ResolvedColor(marker.color, layerColor), 1));
    DrawAlignmentMarker(painter, view, gp.point, marker);
  }
}

// REQ-491 direction arrow at the middle of the alignment.
void DrawAlignmentArrow(QPainter& painter, const AlignmentView& view, const alignment::Alignment& al,
                        const AlignmentStyle& sty, const std::string& layerColor) {
  assert(!al.Empty());
  assert(sty.arrowLength >= 0.0);
  painter.setPen(QPen(ResolvedColor(sty.arrows.color, layerColor), 1));
  const double mid = 0.5 * (al.StartStation() + al.EndStation());
  const alignment::Point2 p = al.GetPointAtStation(mid);
  const double az = al.GetBearingAtStation(mid);
  const double tx = std::sin(az), ty = std::cos(az);
  const double L = sty.arrowLength;
  const alignment::Point2 tip{p.x + 0.5 * L * tx, p.y + 0.5 * L * ty};
  painter.drawLine(view.Project({p.x - 0.5 * L * tx, p.y - 0.5 * L * ty}), view.Project(tip));
  for (const double turn : {0.5236, -0.5236}) {  // the two barbs, 30 degrees either side of the back-pointing direction
    const double c = std::cos(turn), sn = std::sin(turn);
    const double bx = -tx * c + ty * sn;  // (-tx, -ty) rotated by turn
    const double by = -tx * sn - ty * c;
    painter.drawLine(view.Project(tip), view.Project({tip.x + 0.25 * L * bx, tip.y + 0.25 * L * by}));
  }
}

// REQ-502: where the design-check markers go, one per failing entity, at the entity's midpoint on screen.
std::vector<QPointF> DesignWarningPoints(const AppCommandState& state, const AlignmentView& view,
                                         const alignment::Alignment& al) {
  assert(!al.Empty());
  assert(view.width > 0.f && view.height > 0.f);
  std::vector<QPointF> points;
  const alignment::AlignmentCheckSet* checkSet =
      alignment::FindCheckSet(state.alignmentCheckSets, AlignmentStyles::EffectiveCheckSetName(state.alignmentStyles, al));
  if (checkSet == nullptr) return points;
  for (const alignment::DesignFinding& fd : alignment::CheckDesign(al, *checkSet, state.displayLinearPrecision)) {
    const int idx = fd.entity - 1;
    points.push_back(view.Project(al.GetPointAtStation(0.5 * (al.EntityStartStation(idx) + al.EntityEndStation(idx)))));
  }
  return points;
}

// The marker's height in pixels: one label text height on paper, but never under 10 pixels, so a failure stays visible
// when zoomed out (D-2026-11-04-d).
double DesignWarningHeight(const AlignmentView& view) {
  constexpr double kMinPx = 10.0;
  return std::isfinite(view.textPx) ? std::max(view.textPx, kMinPx) : kMinPx;
}

// REQ-502 / D-2026-11-04-d: an amber warning triangle with "!" centred on each entity that fails its design check,
// whatever the style shows, as Civil 3D marks a criteria violation.
void DrawAlignmentDesignWarnings(QPainter& painter, const AlignmentView& view, const std::vector<QPointF>& points) {
  assert(view.width > 0.f && view.height > 0.f);
  assert(std::isfinite(view.textPx));
  const double h = DesignWarningHeight(view);
  const QColor amber("#f5a31a");
  QPen pen(amber, std::max(1.5, 0.1 * h));
  pen.setJoinStyle(Qt::MiterJoin);
  painter.setBrush(Qt::NoBrush);
  for (const QPointF& c : points) {
    const double half = h / std::sqrt(3.0);  // half the side of an equilateral triangle of height h
    painter.setPen(pen);
    painter.drawPolygon(QPolygonF({c + QPointF(0.0, -2.0 * h / 3.0), c + QPointF(half, h / 3.0), c + QPointF(-half, h / 3.0)}));
    painter.drawLine(c + QPointF(0.0, -0.33 * h), c + QPointF(0.0, 0.05 * h));
    painter.setPen(Qt::NoPen);
    painter.setBrush(amber);
    painter.drawEllipse(c + QPointF(0.0, 0.18 * h), 0.06 * h, 0.06 * h);
    painter.setBrush(Qt::NoBrush);
  }
}

void DrawQtAlignmentOverlay(QPainter& painter, const AppCommandState& state, AlignmentView view,
                            const alignment::Alignment& al) {
  assert(view.width > 0.f && view.height > 0.f);
  assert(std::isfinite(view.width) && std::isfinite(view.height));
  if (al.Empty() || CadEntityIdHidden(&state.hiddenEntityIds, al.id)) return;
  view.textPx = AlignmentLabelStyles::TextHeight(AlignmentLabelStyles::Resolve(state.alignmentLabelStyles,
                                                                               AlignmentLabelStyles::kStandardName),
                                                 state.modelUnitsPerPlottedInch) *
                FrameAt(view, al, al.StartStation()).pixelsPerUnit;
  const CadLayerRow* layer = FindDrawingLayerRowCi(state, al.layer.empty() ? std::string("0") : al.layer);
  if (layer != nullptr && (!layer->on || layer->frozen)) return;  // REQ-495.4
  const std::string layerColor = layer != nullptr ? layer->color : "White";
  // The label set decides the station TEXT and where ticks sit; the style decides what is drawn (REQ-491). A missing
  // style or label set (already logged at load) reads as Standard.
  const AlignmentLabelSet* ls = AlignmentLabelSets::Resolve(state.alignmentLabelSets, al.labelSet);
  const AlignmentStyle* sty = AlignmentStyles::Resolve(state.alignmentStyles, al.style);
  const std::vector<alignment::StationTickLabel> ticks =
      ls != nullptr ? alignment::CollectMajorMinorStationTicks(al, ls->majorInterval, ls->minorInterval,
                                                               ls->majorStationsOn, ls->minorStationsOn)
                    : std::vector<alignment::StationTickLabel>{};
  const std::vector<QPointF> warnings = DesignWarningPoints(state, view, al);
  // Text under two pixels cannot be read; each label face skips it, as other drawing text is skipped zoomed far out.
  if (ls != nullptr && (sty == nullptr || sty->labelsVisible)) {
    LabelBoxes boxes;  // labels keep clear of the design-check markers
    const double h = DesignWarningHeight(view);
    for (const QPointF& c : warnings) boxes.Take(QPolygonF(QRectF(c.x() - h, c.y() - h, 2.0 * h, 2.0 * h)));
    DrawAlignmentLabels(painter, state, view, al, ticks, *ls, sty, layerColor, &boxes);
  }
  if (sty != nullptr) DrawAlignmentTicksAndMarkers(painter, view, al, ticks, *sty, layerColor);
  if (sty != nullptr && sty->arrows.visible) DrawAlignmentArrow(painter, view, al, *sty, layerColor);
  DrawAlignmentDesignWarnings(painter, view, warnings);
}

}  // namespace

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
    const float leaderArrow = ml.arrowSizeWorld > 0.f ? ml.arrowSizeWorld :
        std::max(style.arrowSizeInches, .001f) * std::max(state.modelUnitsPerPlottedInch, 1.e-6f);
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
        painter.rotate(-label.rotationRad * 180.0 / 3.14159265358979323846);
        if (!StyleProperty(style.props, "textColor").empty()) {
          float rgbText[3]{1, 1, 1};
          CadColorResolveRgb(CadColorEffectiveStorage(StyleProperty(style.props, "textColor"), layer ? layer->color : "White"), 1, 1, 1, rgbText);
          painter.setPen(QColor::fromRgbF(rgbText[0], rgbText[1], rgbText[2]));
        }
        QFont font(QString::fromUtf8(label.fontFamily.c_str())); font.setPixelSize(std::clamp(qRound(pixels), 1, 4096));
        painter.setFont(font);
        if (!DrawStrokeText(painter, label, pixels, label.text, true, label.mtextAttach)) painter.drawText(QPointF(0, 0), QString::fromUtf8(label.text.c_str()));
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

  for (const alignment::Alignment& al : state.alignments) DrawQtAlignmentOverlay(painter, state, AlignmentView{camera, width, height}, al);
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
