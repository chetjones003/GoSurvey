#pragma once
#include <QFrame>
#include <functional>
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPropertyAnimation;

class FloatingCommandBar final : public QFrame {
public:
  explicit FloatingCommandBar(QWidget* viewport);
  QLineEdit* Input() const { return m_Input; }
  QPlainTextEdit* Transcript() const { return m_Transcript; }
  void Append(const QString& line);
  void ToggleConsole();
  void ToggleVisible();
  void SetHint(const QString& hint);
  std::function<void(const QString&)> optionSelected;
protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
private:
  void Place();
  QLineEdit* m_Input;
  QPlainTextEdit* m_Transcript;
  QLabel* m_Recent;
  QLabel* m_Hint;
  QWidget* m_Grip;
  QWidget* m_SizeGrip;
  QStringList m_Lines;
  int m_PreferredWidth = 680;
  double m_Anchor = 0.5;
  bool m_Dragging = false;
  QPoint m_DragStart;
  int m_StartX = 0;
  int m_StartWidth = 0;
  QPropertyAnimation* m_Fade;
};
