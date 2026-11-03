#include "DrawingDocumentTabBar.hpp"
#include "QtAccent.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QToolButton>

#include <algorithm>

namespace {
constexpr double kTabRadius = 7.0;
constexpr int kTabPadLeft = 14;
constexpr int kTabPadRight = 10;
constexpr int kCloseSize = 18;

// Flat-edged tab with two rounded corners; the active tab stands taller than the others. Document tabs
// round the top corners, Space tabs (below the viewport) round the bottom ones.
QPainterPath TabShape(const QRect& tab, bool selected, bool below) {
  const double inset = selected ? 3.5 : 6.5;
  QPainterPath shape;
  if (!below) {
    const QRectF rect = QRectF(tab).adjusted(1.5, inset, -1.5, -0.5);
    shape.moveTo(rect.left(), rect.bottom());
    shape.lineTo(rect.left(), rect.top() + kTabRadius);
    shape.quadTo(rect.left(), rect.top(), rect.left() + kTabRadius, rect.top());
    shape.lineTo(rect.right() - kTabRadius, rect.top());
    shape.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + kTabRadius);
    shape.lineTo(rect.right(), rect.bottom());
  } else {
    const QRectF rect = QRectF(tab).adjusted(1.5, 0.5, -1.5, -inset);
    shape.moveTo(rect.left(), rect.top());
    shape.lineTo(rect.left(), rect.bottom() - kTabRadius);
    shape.quadTo(rect.left(), rect.bottom(), rect.left() + kTabRadius, rect.bottom());
    shape.lineTo(rect.right() - kTabRadius, rect.bottom());
    shape.quadTo(rect.right(), rect.bottom(), rect.right(), rect.bottom() - kTabRadius);
    shape.lineTo(rect.right(), rect.top());
  }
  shape.closeSubpath();
  return shape;
}

// Vertical band of the tab that holds the label and the close button.
QRect TabContent(const QRect& tab, bool selected, bool below) {
  const int margin = selected ? 4 : 7;
  const int top = below ? tab.top() + 1 : tab.top() + margin;
  const int bottom = below ? tab.bottom() - margin : tab.bottom() - 1;
  return QRect(tab.left() + kTabPadLeft, top, tab.width() - kTabPadLeft - kTabPadRight, bottom - top);
}

// Close glyph drawn as two strokes so it is the same weight and size on every tab and theme.
class TabCloseButton final : public QToolButton {
 public:
  explicit TabCloseButton(QWidget* parent) : QToolButton(parent) { setMouseTracking(true); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = !qApp->property("cadLightTheme").toBool();
    const bool hot = underMouse() || hasFocus();
    if (hot) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(dark ? "#6f808c" : "#b5bec7"));
      painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    painter.setPen(QPen(QColor(dark ? (hot ? "#ffffff" : "#c3ced6") : (hot ? "#20262d" : "#4b5a66")), 1.4,
                        Qt::SolidLine, Qt::RoundCap));
    const QPointF c = QRectF(rect()).center();
    constexpr double r = 3.6;
    painter.drawLine(QPointF(c.x() - r, c.y() - r), QPointF(c.x() + r, c.y() + r));
    painter.drawLine(QPointF(c.x() - r, c.y() + r), QPointF(c.x() + r, c.y() - r));
  }
};
}  // namespace

DrawingDocumentTabBar::DrawingDocumentTabBar(QWidget* parent, Kind kind) : QTabBar(parent), m_Kind(kind) {
  setMouseTracking(true);
  setDocumentMode(true);
  setDrawBase(false);
  setExpanding(false);
  setTabsClosable(kind == Kind::Document);
  setElideMode(Qt::ElideRight);
  setUsesScrollButtons(true);
  setFixedHeight(kind == Kind::Document ? 32 : 30);
  if (kind == Kind::Space) setShape(QTabBar::RoundedSouth);
}

QSize DrawingDocumentTabBar::tabSizeHint(int index) const {
  QFont labelFont = font();
  labelFont.setWeight(QFont::DemiBold);
  const int iconWidth = tabIcon(index).isNull() ? 0 : 22;
  const int closeWidth = m_Kind == Kind::Space || index == 0 ? 0 : 24;
  const int width = QFontMetrics(labelFont).horizontalAdvance(tabText(index)) + iconWidth + closeWidth + 40;
  return {std::clamp(width, m_Kind == Kind::Space ? 84 : index == 0 ? 90 : 130, 260), height()};
}

void DrawingDocumentTabBar::tabInserted(int index) {
  QTabBar::tabInserted(index);
  QWidget* previous = tabButton(index, QTabBar::RightSide);
  if (m_Kind == Kind::Space || index == 0) {
    setTabButton(index, QTabBar::RightSide, nullptr);
  } else {
    auto* close = new TabCloseButton(this);
    close->setObjectName("drawingTabClose");
    close->setToolTip("Close drawing");
    close->setAccessibleName("Close drawing");
    close->setAutoRaise(true);
    close->setFixedSize(kCloseSize, kCloseSize);
    connect(close, &QToolButton::clicked, this, [this, close] {
      for (int i = 1; i < count(); ++i) {
        if (tabButton(i, QTabBar::RightSide) == close) {
          Q_EMIT tabCloseRequested(i);
          return;
        }
      }
    });
    setTabButton(index, QTabBar::RightSide, close);
  }
  if (previous != nullptr) previous->deleteLater();
}

void DrawingDocumentTabBar::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  // A transparent tab stylesheet can resolve the widget's Window palette role to black in Light.
  const bool dark = !qApp->property("cadLightTheme").toBool();
  QStyleOption focus;
  focus.initFrom(this);
  // Qt parks the close button on the raw tab edge; seat it in the tab's content band instead.
  for (int i = 1; i < count(); ++i) {
    if (auto* close = tabButton(i, QTabBar::RightSide)) {
      const QRect band = TabContent(tabRect(i), i == currentIndex(), m_Kind == Kind::Space);
      close->move(band.right() - close->width() + 1, band.center().y() - close->height() / 2 + 1);
    }
  }
  const auto drawTab = [&](int index) {
    const bool selected = index == currentIndex();
    const QRect tab = tabRect(index);
    const QPainterPath shape = TabShape(tab, selected, m_Kind == Kind::Space);
    const QColor fill = selected ? QColor(dark ? "#52616d" : "#ffffff")
        : QColor(dark ? (m_HoverTab == index ? "#3b454d" : "#303940")
                      : (m_HoverTab == index ? "#e4e8ed" : "#ccd3da"));
    painter.setBrush(fill);
    painter.setPen(QColor(dark ? (selected ? "#8999a5" : "#65737d") : "#8b98a4"));
    painter.drawPath(shape);
    if (selected) {
      const QRectF bounds = shape.boundingRect();
      painter.setPen(QPen(QColor(QtAccentFor(dark).strong), 2));
      const double y = m_Kind == Kind::Space ? bounds.top() + 0.5 : bounds.bottom() - 0.5;
      painter.drawLine(QPointF(bounds.left() + 2, y), QPointF(bounds.right() - 2, y));
    }

    QRect content = TabContent(tab, selected, m_Kind == Kind::Space);
    if (!tabIcon(index).isNull()) {
      tabIcon(index).paint(&painter, QRect(content.left(), content.center().y() - 8, 16, 16));
      content.setLeft(content.left() + 22);
    }
    if (auto* close = tabButton(index, QTabBar::RightSide)) content.setRight(close->geometry().left() - 6);
    QFont labelFont = font();
    labelFont.setWeight(selected ? QFont::DemiBold : QFont::Normal);
    painter.setFont(labelFont);
    painter.setPen(QColor(dark ? (selected ? "#ffffff" : "#d3dce2") : "#26333e"));
    const QString label = painter.fontMetrics().elidedText(tabText(index), Qt::ElideRight, content.width());
    painter.drawText(content, Qt::AlignLeft | Qt::AlignVCenter, label);
    if (selected && hasFocus() && focus.state.testFlag(QStyle::State_KeyboardFocusChange)) {
      painter.setPen(QPen(QColor(QtAccentFor(dark).strong), 1, Qt::DotLine));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(content.adjusted(-2, 2, 2, -2));
    }
  };
  for (int i = 0; i < count(); ++i) if (i != currentIndex()) drawTab(i);
  if (currentIndex() >= 0) drawTab(currentIndex());
}

void DrawingDocumentTabBar::mousePressEvent(QMouseEvent* event) {
  const int index = tabAt(event->position().toPoint());
  if (index >= 0 && !TabShape(tabRect(index), index == currentIndex(), m_Kind == Kind::Space).contains(event->position())) {
    event->ignore();
    return;
  }
  QTabBar::mousePressEvent(event);
}

void DrawingDocumentTabBar::mouseMoveEvent(QMouseEvent* event) {
  const int index = tabAt(event->position().toPoint());
  const int hovered = index >= 0 && TabShape(tabRect(index), index == currentIndex(), m_Kind == Kind::Space).contains(event->position())
      ? index : -1;
  if (m_HoverTab != hovered) {
    m_HoverTab = hovered;
    update();
  }
  QTabBar::mouseMoveEvent(event);
}

void DrawingDocumentTabBar::leaveEvent(QEvent* event) {
  m_HoverTab = -1;
  update();
  QTabBar::leaveEvent(event);
}
