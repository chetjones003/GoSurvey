#pragma once

#include <QTabBar>

// Qt owns layout, scrolling and accessible controls; only tab shapes and labels are painted here.

class DrawingDocumentTabBar final : public QTabBar {
 public:
  /// Document: the drawing tabs above the viewport (closable, tabs hang up from the bar's bottom edge).
  /// Space: the Model/Layout tabs below the viewport (no close buttons, mirrored so they hang from the top).
  enum class Kind { Document, Space };
  explicit DrawingDocumentTabBar(QWidget* parent = nullptr, Kind kind = Kind::Document);

 protected:
  void paintEvent(QPaintEvent* event) override;
  QSize tabSizeHint(int index) const override;
  void mousePressEvent(QMouseEvent* event) override;
  void tabInserted(int index) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  Kind m_Kind;
  int m_HoverTab = -1;
};
