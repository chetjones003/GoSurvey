#pragma once
#include <QCloseEvent>
#include <QDialog>
#include <QKeyEvent>

/// A dialog whose only exits are its own buttons: Escape and the window close
/// request are ignored, so "I closed the window" can never be mistaken for a
/// decision (the REQ-078 update dialog needs this).
class NonDismissableDialog final : public QDialog {
public:
  using QDialog::QDialog;

protected:
  void closeEvent(QCloseEvent* event) override { event->ignore(); }
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape)
      event->ignore();
    else
      QDialog::keyPressEvent(event);
  }
};
