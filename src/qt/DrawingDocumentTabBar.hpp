#pragma once

#include <QTabBar>

// Qt owns layout, scrolling and accessible controls; only tab shapes and labels are painted here.

class DrawingDocumentTabBar final : public QTabBar {
 public:
  explicit DrawingDocumentTabBar(QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
  QSize tabSizeHint(int index) const override;
  void mousePressEvent(QMouseEvent* event) override;
  void tabInserted(int index) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  int m_HoverTab = -1;
};
