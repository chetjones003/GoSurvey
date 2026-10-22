#include "AdjustmentEditor.hpp"
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <set>

AdjustmentEditorPalette AdjustmentPalette(bool dark) {
  AdjustmentEditorPalette p;
  if (!dark) {
    p.background = "#f0f0f0"; p.text = "#1e2430"; p.keyword = "#003db3"; p.option = "#7a3e9d";
    p.number = "#0a6b57"; p.comment = "#6b766b"; p.fieldCode = "#8c470d";
    p.gutterBackground = "#eef1f5"; p.gutterText = "#8a94a3"; p.error = "#c9302c";
    p.planLeg = "#3c64b4"; p.planPoint = "#1e1e1e"; p.planEllipse = "#d24628";
    p.planSide = "#8c8c91"; p.planCheck = "#3c965a";
  } else {  // the graphite-gray surface of the Qt Dark appearance (REQ-431)
    p.background = "#1c2024"; p.text = "#d6e2f0"; p.keyword = "#5bc0eb"; p.option = "#e3b04b";
    p.number = "#7fd6a8"; p.comment = "#7088a0"; p.fieldCode = "#e08f62";
    p.gutterBackground = "#0b1524"; p.gutterText = "#6f86a0"; p.error = "#ff6b6b";
    p.planLeg = "#4fa3e0"; p.planPoint = "#e6eef7"; p.planEllipse = "#f28f5c";
    p.planSide = "#8a9bb0"; p.planCheck = "#5dc48a";
  }
  return p;
}

namespace {
bool IsKeyword(const QString& upper) {
  static const char* const keywords[] = {
      "STATION", "BEARING", "ENDPOINT", "LOOP", "SCALE", "ERRORS", "STN", "HANG", "HDIST", "SDIST", "VANG",
      "DESC", "PRISM", "POLE", "TSERR", "CTRL", "ANGLE", "DIST", "AZIMUTH", "EDIF", "DIR", "ON", "OFF",
      "ZENITH", "ELEV", "SIDE", "CHECK", "COMBINED", "GRID", "REF"};
  for (const char* keyword : keywords) if (upper == QLatin1String(keyword)) return true;
  return false;
}

bool IsNumber(const QString& word) {
  int i = 0;
  if (i < word.size() && (word[i] == '-' || word[i] == '+')) ++i;
  bool digit = false, dot = false;
  for (; i < word.size(); ++i) {
    if (word[i].isDigit() && word[i].unicode() < 128) digit = true;
    else if (word[i] == '.' && !dot) dot = true;
    else return false;
  }
  return digit;
}

bool IsBlank(QChar c) { return c == ' ' || c == '\t'; }
}  // namespace

QVector<AdjustmentSpan> ClassifyAdjustmentLine(const QString& line) {
  QVector<AdjustmentSpan> spans;
  int i = 0;
  while (i < line.size() && IsBlank(line[i])) ++i;
  if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
    spans.push_back({i, static_cast<int>(line.size()) - i, AdjustmentToken::Comment});
    return spans;
  }
  while (i < line.size()) {
    if (IsBlank(line[i])) { ++i; continue; }
    int end = i;
    while (end < line.size() && !IsBlank(line[end])) ++end;
    const QString word = line.mid(i, end - i);
    if (word[0] == '\'' || word[0] == '`') {
      spans.push_back({i, static_cast<int>(line.size()) - i, AdjustmentToken::FieldCode});
      return spans;
    }
    if (word[0] == '#') spans.push_back({i, end - i, AdjustmentToken::Option});
    else if (IsKeyword(word.toUpper())) spans.push_back({i, end - i, AdjustmentToken::Keyword});
    else if (IsNumber(word)) spans.push_back({i, end - i, AdjustmentToken::Number});
    i = end;
  }
  return spans;
}

namespace {
class AdjustmentHighlighter final : public QSyntaxHighlighter {
public:
  explicit AdjustmentHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
  void SetColors(const AdjustmentEditorPalette& palette) {
    m_Palette = palette;
    rehighlight();
  }
protected:
  void highlightBlock(const QString& text) override {
    for (const auto& span : ClassifyAdjustmentLine(text)) {
      QTextCharFormat format;
      switch (span.kind) {
        case AdjustmentToken::Keyword: format.setForeground(m_Palette.keyword); format.setFontWeight(QFont::DemiBold); break;
        case AdjustmentToken::Option: format.setForeground(m_Palette.option); break;
        case AdjustmentToken::Number: format.setForeground(m_Palette.number); break;
        case AdjustmentToken::Comment: format.setForeground(m_Palette.comment); format.setFontItalic(true); break;
        case AdjustmentToken::FieldCode: format.setForeground(m_Palette.fieldCode); break;
      }
      setFormat(span.start, span.length, format);
    }
  }
private:
  AdjustmentEditorPalette m_Palette;
};
}  // namespace

class AdjustmentGutter final : public QWidget {
public:
  explicit AdjustmentGutter(AdjustmentCodeEdit* edit) : QWidget(edit), m_Edit(edit) { setObjectName("adjustmentGutter"); }
  QSize sizeHint() const override { return {m_Edit->GutterWidth(), 0}; }
protected:
  void paintEvent(QPaintEvent* event) override { m_Edit->PaintGutter(event); }
private:
  AdjustmentCodeEdit* m_Edit;
};

AdjustmentCodeEdit::AdjustmentCodeEdit(QWidget* parent) : QPlainTextEdit(parent), m_Gutter(new AdjustmentGutter(this)) {
  setLineWrapMode(QPlainTextEdit::NoWrap);
  setFont(QFont("Consolas", 11));
  m_Highlighter = new AdjustmentHighlighter(document());
  connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { UpdateGutterWidth(); });
  connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
    if (dy) m_Gutter->scroll(0, dy); else m_Gutter->update(0, rect.y(), m_Gutter->width(), rect.height());
    if (rect.contains(viewport()->rect())) UpdateGutterWidth();
  });
  SetPalette(AdjustmentPalette(true));
  UpdateGutterWidth();
}

void AdjustmentCodeEdit::SetPalette(const AdjustmentEditorPalette& palette) {
  m_Palette = palette;
  // The application stylesheet paints text boxes, so the editor's own colours go through one too.
  auto mix = [](const QColor& from, const QColor& to, float amount) {
    auto blend = [amount](qreal a, qreal b) { return static_cast<float>(a + (b - a) * amount); };
    return QColor::fromRgbF(blend(from.redF(), to.redF()), blend(from.greenF(), to.greenF()), blend(from.blueF(), to.blueF()));
  };
  setStyleSheet(QString("QPlainTextEdit { background: %1; color: %2; selection-background-color: %3; selection-color: %2; border: 1px solid %4; }")
                    .arg(palette.background.name(), palette.text.name(), mix(palette.background, palette.keyword, 0.35f).name(),
                         palette.gutterBackground.name()));
  static_cast<AdjustmentHighlighter*>(m_Highlighter)->SetColors(palette);
  m_Gutter->update();
  viewport()->update();
}

void AdjustmentCodeEdit::SetError(int line, const QString& message) {
  if (line == m_ErrorLine && message == m_ErrorMessage) return;
  m_ErrorLine = line;
  m_ErrorMessage = message;
  m_Gutter->update();
  viewport()->update();
}

int AdjustmentCodeEdit::GutterWidth() const {
  const int digits = static_cast<int>(QString::number(std::max(1, blockCount())).size());
  return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 8;
}

QWidget* AdjustmentCodeEdit::Gutter() const { return m_Gutter; }

int AdjustmentCodeEdit::FirstVisibleLine() const { return firstVisibleBlock().blockNumber() + 1; }

void AdjustmentCodeEdit::UpdateGutterWidth() {
  setViewportMargins(GutterWidth(), 0, 0, 0);
  m_Gutter->setGeometry(QRect(contentsRect().left(), contentsRect().top(), GutterWidth(), contentsRect().height()));
}

void AdjustmentCodeEdit::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);
  UpdateGutterWidth();
}

void AdjustmentCodeEdit::PaintGutter(QPaintEvent* event) {
  QPainter painter(m_Gutter);
  painter.fillRect(event->rect(), m_Palette.gutterBackground);
  QTextBlock block = firstVisibleBlock();
  int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + qRound(blockBoundingRect(block).height());
  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      const bool bad = block.blockNumber() + 1 == m_ErrorLine;
      painter.setPen(bad ? m_Palette.error : m_Palette.gutterText);
      QFont font = painter.font();
      font.setBold(bad);
      painter.setFont(font);
      painter.drawText(0, top, m_Gutter->width() - 8, fontMetrics().height(), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(block.blockNumber() + 1));
      if (bad) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_Palette.error);
        painter.drawEllipse(QPointF(7, top + fontMetrics().height() / 2.0), 3.0, 3.0);
      }
    }
    block = block.next();
    top = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
  }
}

void AdjustmentCodeEdit::paintEvent(QPaintEvent* event) {
  QPlainTextEdit::paintEvent(event);
  if (m_ErrorLine <= 0) return;
  const QTextBlock block = document()->findBlockByNumber(m_ErrorLine - 1);
  if (!block.isValid()) return;
  QPainter painter(viewport());
  const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
  QColor tint = m_Palette.error;
  tint.setAlpha(36);
  painter.fillRect(QRectF(0, rect.top(), viewport()->width(), rect.height()), tint);
  const qreal textEnd = document()->documentMargin() + fontMetrics().horizontalAdvance(block.text());
  const qreal y = rect.top() + fontMetrics().height() - 1;
  painter.setPen(QPen(m_Palette.error, 1.5));
  for (int step = 0; document()->documentMargin() + 4 * step < textEnd; ++step) {  // wavy underline
    const qreal x = document()->documentMargin() + 4 * step;
    painter.drawLine(QPointF(x, y), QPointF(std::min(x + 2, textEnd), y + ((step % 2) ? 1.5 : -0.5)));
  }
  const qreal messageX = textEnd + 20;
  const int room = viewport()->width() - static_cast<int>(messageX) - 6;
  if (room > 60) {
    QFont font = painter.font();
    font.setItalic(true);
    painter.setFont(font);
    painter.drawText(QPointF(messageX, rect.top() + fontMetrics().ascent()),
                     QFontMetrics(font).elidedText(m_ErrorMessage, Qt::ElideRight, room));
  }
}

// ---------------------------------------------------------------- plan preview

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kPad = 22.0;
constexpr double kMinimumEllipsePixels = 14.0;
}

AdjustmentPlanView::AdjustmentPlanView(QWidget* parent) : QWidget(parent) {
  setObjectName("adjustmentPlan");
  setMouseTracking(true);
  setMinimumSize(160, 120);
  m_Palette = AdjustmentPalette(true);
}

void AdjustmentPlanView::SetPalette(const AdjustmentEditorPalette& palette) { m_Palette = palette; update(); }
void AdjustmentPlanView::SetUnitLabel(const QString& unit) { m_Unit = unit; }

void AdjustmentPlanView::SetTraverse(const TraverseData& data, const LsaResult* adjustment) {
  m_Stations.clear();
  m_Segments.clear();
  std::set<int> seen;
  auto add = [&](int id, double e, double n) -> Station* {
    if (!seen.insert(id).second) return nullptr;
    Station station;
    station.id = id;
    station.position = {e, n};
    if (adjustment) for (std::size_t i = 0; i < adjustment->stationIds.size(); ++i) {
      if (adjustment->stationIds[i] != id) continue;
      station.position = {adjustment->adjEasting[i], adjustment->adjNorthing[i]};
      if (i < adjustment->stationSemiMajorFt.size() && i < adjustment->stationSemiMinorFt.size() &&
          i < adjustment->stationEllipseAzimuthDeg.size()) {
        station.hasEllipse = true;
        station.semiMajor = adjustment->stationSemiMajorFt[i];
        station.semiMinor = adjustment->stationSemiMinorFt[i];
        station.azimuthDeg = adjustment->stationEllipseAzimuthDeg[i];
      }
      break;
    }
    m_Stations.push_back(station);
    return &m_Stations.back();
  };
  auto position = [&](int id) -> const Station* {
    for (const auto& station : m_Stations) if (station.id == id) return &station;
    return nullptr;
  };
  if (!data.legs.empty() || data.networkStations.empty())
    add(data.startStationId, data.startEasting, data.startNorthing);
  auto adjusted = [&](int id) {
    return adjustment && std::find(adjustment->stationIds.begin(), adjustment->stationIds.end(), id) != adjustment->stationIds.end();
  };
  for (const auto& leg : data.legs) {
    if (!leg.computed && !adjusted(leg.stationId)) continue;  // unresolved leg: nothing to plot yet
    add(leg.stationId, leg.computedEasting, leg.computedNorthing);
  }
  for (const auto& station : data.networkStations)
    if (station.easting.present && station.northing.present)
      add(station.id, station.easting.value, station.northing.value);
  if (adjustment) for (std::size_t i = 0; i < adjustment->stationIds.size(); ++i)
    add(adjustment->stationIds[i], adjustment->adjEasting[i], adjustment->adjNorthing[i]);

  // The backbone runs through the start station and every plotted leg, in order.
  const Station* previous = data.legs.empty() && !data.networkStations.empty() ? nullptr : position(data.startStationId);
  for (const auto& leg : data.legs) {
    const Station* station = position(leg.stationId);
    if (station && previous && station != previous) m_Segments.push_back({previous->position, station->position, Segment::Leg, {}});
    if (station) previous = station;
  }
  if (data.isClosedLoop && m_Stations.size() > 2 && !data.legs.empty() && previous && previous->id != data.startStationId)
    m_Segments.push_back({previous->position, position(data.startStationId)->position, Segment::Closing, {}});
  for (const auto& dist : data.networkDists) {
    const Station* from = position(dist.fromId);
    const Station* to = position(dist.toId);
    if (from && to) m_Segments.push_back({from->position, to->position, Segment::Leg, {}});
  }
  for (const auto& shot : data.sideShots) {
    if (!shot.computed) continue;
    const Station* at = position(shot.atStationId);
    const QPointF point(shot.computedEasting, shot.computedNorthing);
    const QString label = shot.description.empty() ? QString::number(shot.stationId) : QString::fromUtf8(shot.description.c_str());
    if (at) m_Segments.push_back({at->position, point, shot.isCheckShot ? Segment::Check : Segment::Side, label});
    else m_Segments.push_back({point, point, shot.isCheckShot ? Segment::Check : Segment::Side, label});
  }

  QRectF bounds;
  bool first = true;
  auto grow = [&](const QPointF& p) {
    if (first) { bounds = QRectF(p, QSizeF(0, 0)); first = false; return; }
    bounds.setLeft(std::min(bounds.left(), p.x())); bounds.setRight(std::max(bounds.right(), p.x()));
    bounds.setTop(std::min(bounds.top(), p.y())); bounds.setBottom(std::max(bounds.bottom(), p.y()));
  };
  for (const auto& station : m_Stations) grow(station.position);
  for (const auto& segment : m_Segments) if (segment.kind == Segment::Side || segment.kind == Segment::Check) grow(segment.to);
  if (!first) {
    const bool disjoint = m_HavePrevious && (bounds.right() < m_Previous.left() || bounds.left() > m_Previous.right() ||
                                              bounds.bottom() < m_Previous.top() || bounds.top() > m_Previous.bottom());
    m_Previous = bounds;
    m_HavePrevious = true;
    if (!m_Initialised || disjoint) { ZoomExtents(); m_Initialised = true; }
  }
  update();
}

int AdjustmentPlanView::LegCount() const {
  return static_cast<int>(std::count_if(m_Segments.begin(), m_Segments.end(), [](const Segment& s) { return s.kind == Segment::Leg; }));
}
int AdjustmentPlanView::SideShotCount() const {
  return static_cast<int>(std::count_if(m_Segments.begin(), m_Segments.end(),
      [](const Segment& s) { return s.kind == Segment::Side || s.kind == Segment::Check; }));
}
bool AdjustmentPlanView::HasClosingLeg() const {
  return std::any_of(m_Segments.begin(), m_Segments.end(), [](const Segment& s) { return s.kind == Segment::Closing; });
}
int AdjustmentPlanView::EllipseCount() const {
  return static_cast<int>(std::count_if(m_Stations.begin(), m_Stations.end(), [](const Station& s) { return s.hasEllipse; }));
}

void AdjustmentPlanView::ZoomExtents() {
  if (!m_HavePrevious) return;
  m_Center = m_Previous.center();
  const double spanE = std::max(1e-6, m_Previous.width()), spanN = std::max(1e-6, m_Previous.height());
  m_Scale = std::min(std::max(10.0, width() - 2 * kPad) / spanE, std::max(10.0, height() - 2 * kPad) / spanN);
  update();
}

QPointF AdjustmentPlanView::ToScreen(double easting, double northing) const {
  return {width() / 2.0 + (easting - m_Center.x()) * m_Scale, height() / 2.0 - (northing - m_Center.y()) * m_Scale};
}

void AdjustmentPlanView::ZoomAt(const QPointF& at, double steps) {
  const double e = m_Center.x() + (at.x() - width() / 2.0) / m_Scale;
  const double n = m_Center.y() - (at.y() - height() / 2.0) / m_Scale;
  m_Scale = std::clamp(m_Scale * std::pow(1.1, steps), 1e-6, 1e9);
  m_Center = {e - (at.x() - width() / 2.0) / m_Scale, n + (at.y() - height() / 2.0) / m_Scale};
  update();
}

void AdjustmentPlanView::PanBy(const QPointF& pixels) {
  m_Center -= QPointF(pixels.x() / m_Scale, -pixels.y() / m_Scale);
  update();
}

double AdjustmentPlanView::EllipseFactor() const {
  double largest = 0;
  for (const auto& station : m_Stations) if (station.hasEllipse) largest = std::max(largest, station.semiMajor);
  const double pixels = largest * m_Scale;
  if (pixels <= 0 || pixels >= kMinimumEllipsePixels) return 1.0;
  const double wanted = kMinimumEllipsePixels / pixels;
  const double decade = std::pow(10.0, std::floor(std::log10(wanted)));
  for (double step : {1.0, 2.0, 5.0, 10.0}) if (step * decade >= wanted) return step * decade;
  return 10.0 * decade;
}

QPolygonF AdjustmentPlanView::EllipseOutline(const Station& station, double factor) const {
  const QPointF c = ToScreen(station.position.x(), station.position.y());
  const double major = station.semiMajor * m_Scale * factor, minor = station.semiMinor * m_Scale * factor;
  const double rotation = station.azimuthDeg * kPi / 180.0;
  QPolygonF polygon;
  for (int i = 0; i <= 48; ++i) {
    const double t = 2 * kPi * i / 48;
    const double lx = major * std::cos(t), ly = minor * std::sin(t);
    polygon << QPointF(c.x() + lx * std::sin(rotation) + ly * std::cos(rotation),
                       c.y() - (lx * std::cos(rotation) - ly * std::sin(rotation)));
  }
  return polygon;
}

const AdjustmentPlanView::Station* AdjustmentPlanView::StationAt(const QPoint& position) const {
  const double factor = EllipseFactor();
  const Station* best = nullptr;
  double bestDistance = 1e30;
  for (const auto& station : m_Stations) {
    const QPointF c = ToScreen(station.position.x(), station.position.y());
    const double dx = position.x() - c.x(), dy = position.y() - c.y();
    const double distance = dx * dx + dy * dy;
    bool inside = false;
    if (station.hasEllipse && (station.semiMajor > 1e-9 || station.semiMinor > 1e-9)) {
      const double major = station.semiMajor * m_Scale * factor, minor = station.semiMinor * m_Scale * factor;
      const double rotation = station.azimuthDeg * kPi / 180.0;
      const double u = dx * std::sin(rotation) + dy * std::cos(rotation);
      const double v = dx * std::cos(rotation) - dy * std::sin(rotation);
      if (major > 1e-6 && minor > 1e-6) inside = u * u / (major * major) + v * v / (minor * minor) <= 1.0;
    }
    if ((distance <= 64.0 || inside) && distance < bestDistance) { bestDistance = distance; best = &station; }
  }
  return best;
}

QString AdjustmentPlanView::TooltipAt(const QPoint& position) const {
  const Station* station = StationAt(position);
  if (!station) return {};
  if (!station->hasEllipse)
    return QString("Station %1 — no error ellipse (run the least-squares adjustment).").arg(station->id);
  return QString("Station %1 — 1-sigma error ellipse\nSemi-major: %2 %5\nSemi-minor: %3 %5\nAzimuth: %4°")
      .arg(station->id).arg(station->semiMajor, 0, 'f', 4).arg(station->semiMinor, 0, 'f', 4)
      .arg(station->azimuthDeg, 0, 'f', 1).arg(m_Unit);
}

void AdjustmentPlanView::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QColor background = m_Palette.background;
  painter.fillRect(rect(), background);
  if (!HasEnoughStations()) {
    painter.setPen(m_Palette.gutterText);
    painter.drawText(rect(), Qt::AlignCenter, "Not enough computed stations yet.");
    return;
  }
  for (const auto& segment : m_Segments) {
    if (segment.kind == Segment::Leg) painter.setPen(QPen(m_Palette.planLeg, 1.5));
    else if (segment.kind == Segment::Closing) { QColor c = m_Palette.planLeg; c.setAlpha(140); painter.setPen(QPen(c, 1.0, Qt::DashLine)); }
    else painter.setPen(QPen(segment.kind == Segment::Check ? m_Palette.planCheck : m_Palette.planSide, 1.0));
    painter.drawLine(ToScreen(segment.from.x(), segment.from.y()), ToScreen(segment.to.x(), segment.to.y()));
    if (segment.kind == Segment::Side || segment.kind == Segment::Check) {
      const QPointF p = ToScreen(segment.to.x(), segment.to.y());
      painter.setBrush(painter.pen().color());
      painter.drawEllipse(p, 2.5, 2.5);
      painter.drawText(p + QPointF(5, 4), segment.label);
    }
  }
  const double factor = EllipseFactor();
  const Station* hover = underMouse() ? StationAt(mapFromGlobal(QCursor::pos())) : nullptr;
  painter.setBrush(Qt::NoBrush);
  for (const auto& station : m_Stations) {
    const bool hot = &station == hover;
    if (station.hasEllipse && (station.semiMajor > 1e-9 || station.semiMinor > 1e-9)) {
      QColor c = m_Palette.planEllipse;
      c.setAlpha(210);
      painter.setPen(QPen(c, hot ? 2.5 : 1.25));
      painter.drawPolyline(EllipseOutline(station, factor));
    }
  }
  for (const auto& station : m_Stations) {
    const QPointF c = ToScreen(station.position.x(), station.position.y());
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_Palette.planPoint);
    painter.drawEllipse(c, &station == hover ? 4.0 : 3.0, &station == hover ? 4.0 : 3.0);
    painter.setPen(m_Palette.planPoint);
    painter.drawText(c + QPointF(6, 4), QString::number(station.id));
  }
  if (factor > 1.0) {
    painter.setPen(m_Palette.gutterText);
    painter.drawText(QPointF(8, height() - 8), QString("Ellipses drawn ×%1 (hover for true size)").arg(factor, 0, 'g', 6));
  }
}

void AdjustmentPlanView::wheelEvent(QWheelEvent* event) {
  if (event->angleDelta().y() == 0) return;
  ZoomAt(event->position(), event->angleDelta().y() / 120.0);
  event->accept();
}

void AdjustmentPlanView::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::MiddleButton) return;
  m_Panning = true;
  m_LastMouse = event->position().toPoint();
  setCursor(Qt::SizeAllCursor);
}

void AdjustmentPlanView::mouseMoveEvent(QMouseEvent* event) {
  if (m_Panning) {
    const QPoint now = event->position().toPoint();
    PanBy(now - m_LastMouse);
    m_LastMouse = now;
  } else update();  // hover highlight
}

void AdjustmentPlanView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::MiddleButton) return;
  m_Panning = false;
  unsetCursor();
}

void AdjustmentPlanView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) ZoomExtents();
}

bool AdjustmentPlanView::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* help = static_cast<QHelpEvent*>(e);
    const QString text = TooltipAt(help->pos());
    if (text.isEmpty()) QToolTip::hideText(); else QToolTip::showText(help->globalPos(), text, this);
    return true;
  }
  return QWidget::event(e);
}
