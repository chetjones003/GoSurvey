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
QPainterPath TabShape(const QRect& tab, bool selected) {
  const QRectF rect = QRectF(tab).adjusted(2.5, selected ? 3.5 : 6.5, -2.5, -1.5);
  constexpr double slope = 9.0;
  QPainterPath shape;
  shape.moveTo(rect.left(), rect.bottom());
  shape.lineTo(rect.left() + slope, rect.top());
  shape.lineTo(rect.right() - slope, rect.top());
  shape.lineTo(rect.right(), rect.bottom());
  shape.closeSubpath();
  return shape;
}
}  // namespace

DrawingDocumentTabBar::DrawingDocumentTabBar(QWidget* parent) : QTabBar(parent) {
  setMouseTracking(true);
  setDocumentMode(true);
  setDrawBase(false);
  setExpanding(false);
  setTabsClosable(true);
  setElideMode(Qt::ElideRight);
  setUsesScrollButtons(true);
  setFixedHeight(32);
}

QSize DrawingDocumentTabBar::tabSizeHint(int index) const {
  QFont labelFont = font();
  labelFont.setWeight(QFont::DemiBold);
  const int iconWidth = tabIcon(index).isNull() ? 0 : 22;
  const int closeWidth = index == 0 ? 0 : 24;
  const int width = QFontMetrics(labelFont).horizontalAdvance(tabText(index)) + iconWidth + closeWidth + 40;
  return {std::clamp(width, index == 0 ? 90 : 130, 260), 32};
}

void DrawingDocumentTabBar::tabInserted(int index) {
  QTabBar::tabInserted(index);
  QWidget* previous = tabButton(index, QTabBar::RightSide);
  if (index == 0) {
    setTabButton(index, QTabBar::RightSide, nullptr);
  } else {
    auto* close = new QToolButton(this);
    close->setObjectName("drawingTabClose");
    close->setText(QString(QChar(0x00d7)));
    close->setToolTip("Close drawing");
    close->setAccessibleName("Close drawing");
    close->setAutoRaise(true);
    close->setFixedSize(18, 18);
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
  const auto drawTab = [&](int index) {
    const bool selected = index == currentIndex();
    const QRect tab = tabRect(index);
    const QPainterPath shape = TabShape(tab, selected);
    const QColor fill = selected ? QColor(dark ? "#52616d" : "#ffffff")
        : QColor(dark ? (m_HoverTab == index ? "#3b454d" : "#303940")
                      : (m_HoverTab == index ? "#e4e8ed" : "#ccd3da"));
    painter.setBrush(fill);
    painter.setPen(QColor(dark ? (selected ? "#8999a5" : "#65737d") : "#8b98a4"));
    painter.drawPath(shape);
    if (selected) {
      const QRectF bounds = shape.boundingRect();
      painter.setPen(QPen(QColor(QtAccentFor(dark).strong), 2));
      painter.drawLine(QPointF(bounds.left() + 2, bounds.bottom() - 0.5),
                       QPointF(bounds.right() - 2, bounds.bottom() - 0.5));
    }

    QRect content = tab.adjusted(20, selected ? 3 : 6, -20, -2);
    if (!tabIcon(index).isNull()) {
      tabIcon(index).paint(&painter, QRect(content.left(), content.center().y() - 8, 16, 16));
      content.setLeft(content.left() + 22);
    }
    if (auto* close = tabButton(index, QTabBar::RightSide)) content.setRight(close->geometry().left() - 5);
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
  if (index >= 0 && !TabShape(tabRect(index), index == currentIndex()).contains(event->position())) {
    event->ignore();
    return;
  }
  QTabBar::mousePressEvent(event);
}

void DrawingDocumentTabBar::mouseMoveEvent(QMouseEvent* event) {
  const int index = tabAt(event->position().toPoint());
  const int hovered = index >= 0 && TabShape(tabRect(index), index == currentIndex()).contains(event->position())
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
