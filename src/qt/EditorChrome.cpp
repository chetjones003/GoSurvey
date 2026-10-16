#include "EditorChrome.hpp"
#include "EditorWindow.hpp"
#include "CommandBar.hpp"
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSettings>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
class TitleStrip final : public QWidget {
public:
  explicit TitleStrip(QMainWindow* window) : QWidget(window), m_Window(window) { setObjectName("editorTitleBar"); window->installEventFilter(this); }
protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == m_Window && event->type() == QEvent::WindowStateChange)
      if (auto* button = findChild<QToolButton*>("windowMaximize")) button->setText(m_Window->isMaximized() ? "❐" : "□");
    return QWidget::eventFilter(watched, event);
  }
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && m_Window->windowHandle()) m_Window->windowHandle()->startSystemMove();
    else QWidget::mousePressEvent(event);
  }
  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) m_Window->isMaximized() ? m_Window->showNormal() : m_Window->showMaximized();
  }
private:
  QMainWindow* m_Window;
};
}

void EditorWindow::BuildTitleBar(QMenu* applicationMenu) {
  auto* strip = new TitleStrip(this);
  strip->setFixedHeight(30);
  strip->setAutoFillBackground(true);
  if (!qApp->property("defaultQtStyle").toBool()) strip->setStyleSheet("#editorTitleBar { background:#242830; border-bottom:1px solid #191d23; } QToolButton { color:#dce2e9; border:0; padding:0; border-radius:0; } QToolButton:hover { background:#465565; } QToolButton#windowClose:hover { background:#c42b1c; } QToolButton#applicationMenu { background:#306c94; } QToolButton#applicationMenu:hover { background:#438ab5; }");
  auto* row = new QHBoxLayout(strip); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(2);
  auto* app = new QToolButton(strip); app->setObjectName("applicationMenu");
  app->setAccessibleName("GoSurvey application menu"); app->setToolTip("GoSurvey application menu (Alt+F)");
  app->setIcon(windowIcon()); app->setIconSize(QSize(22, 22)); app->setFixedSize(40, 30); app->setAutoRaise(true);
  app->setMenu(applicationMenu); app->setPopupMode(QToolButton::InstantPopup);
  row->addWidget(app);
  connect(new QShortcut(QKeySequence("Alt+F"), this), &QShortcut::activated, app, &QToolButton::showMenu);
  for (const auto& command : {"NEW", "OPEN", "SAVE", "UNDO", "REDO"}) {
    const auto actions = findChildren<QAction*>("command_" + QString(command));
    if (actions.empty()) continue;
    auto* tool = new QToolButton(strip); tool->setObjectName("quick_" + QString(command));
    tool->setDefaultAction(actions.first()); tool->setIconSize(QSize(17, 17)); tool->setFixedSize(26, 26); tool->setAutoRaise(true);
    row->addWidget(tool);
  }
  row->addSpacing(16);
  m_WindowTitle = new QLabel(windowTitle(), strip); m_WindowTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
  if (!qApp->property("defaultQtStyle").toBool()) m_WindowTitle->setStyleSheet("color:#c8d0da; font-size:12px; background:transparent;");
  m_WindowTitle->setMinimumWidth(0); row->addWidget(m_WindowTitle, 1);
  auto control = [&](const char* name, const char* label, const char* glyph) {
    auto* button = new QToolButton(strip); button->setObjectName(name); button->setAccessibleName(label); button->setToolTip(label);
    button->setText(QString::fromUtf8(glyph)); button->setFont(QFont("Segoe UI", 11)); button->setFixedSize(40, 30); button->setAutoRaise(true); row->addWidget(button); return button;
  };
  connect(control("windowMinimize", "Minimize", "−"), &QToolButton::clicked, this, &QWidget::showMinimized);
  auto* maximize = control("windowMaximize", "Maximize / Restore", "□");
  connect(maximize, &QToolButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
  connect(control("windowClose", "Close", "×"), &QToolButton::clicked, this, &QWidget::close);
  setMenuWidget(strip);
}

bool EditorWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result) {
#ifdef _WIN32
  auto* msg = static_cast<MSG*>(message);
  if (msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
    RECT rect; GetWindowRect(msg->hwnd, &rect);
    const int border = std::max(4, int(6 * GetDpiForWindow(msg->hwnd) / 96));
    const int x = GET_X_LPARAM(msg->lParam), y = GET_Y_LPARAM(msg->lParam);
    const bool left = x < rect.left + border, right = x >= rect.right - border;
    const bool top = y < rect.top + border, bottom = y >= rect.bottom - border;
    const int hit = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP) : bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM) : left ? HTLEFT : right ? HTRIGHT : HTCLIENT;
    if (hit != HTCLIENT) { *result = hit; return true; }
  }
  if (msg->message == WM_GETMINMAXINFO) {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
      auto* info = reinterpret_cast<MINMAXINFO*>(msg->lParam);
      info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
      info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
      info->ptMinTrackSize = {qRound(minimumWidth() * devicePixelRatioF()), qRound(minimumHeight() * devicePixelRatioF())};
      *result = 0; return true;
    }
  }
#endif
  return QMainWindow::nativeEvent(type, message, result);
}

FloatingCommandBar::FloatingCommandBar(QWidget* viewport) : QFrame(viewport) {
  setObjectName("floatingCommandBar"); setAttribute(Qt::WA_StyledBackground);
  if (!qApp->property("defaultQtStyle").toBool()) setStyleSheet("#floatingCommandBar { background:transparent; border:0; } #commandRow { background:#343b45; border:1px solid #596572; border-radius:4px; } #commandInput { background:#f4f5f6; color:#20262d; border:0; border-radius:2px; padding:6px; } #commandRecent { color:#cdd7e2; background:#29313b; padding:5px 9px; border-radius:3px; } QToolButton { border:0; padding:3px; }");
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(5);
  m_Transcript = new QPlainTextEdit(this); m_Transcript->setObjectName("commandConsole");
  m_Transcript->setReadOnly(true); m_Transcript->setMaximumBlockCount(5000); m_Transcript->setFixedHeight(200);
  m_Transcript->setFont(QFont("Consolas", 10)); m_Transcript->hide(); layout->addWidget(m_Transcript);
  m_Recent = new QLabel(this); m_Recent->setObjectName("commandRecent"); m_Recent->setTextFormat(Qt::PlainText);
  m_Recent->setAutoFillBackground(true);
  m_Recent->setMaximumHeight(70); m_Recent->hide(); layout->addWidget(m_Recent);
  auto* opacity = new QGraphicsOpacityEffect(m_Recent); opacity->setOpacity(1); m_Recent->setGraphicsEffect(opacity);
  m_Fade = new QPropertyAnimation(opacity, "opacity", this); m_Fade->setDuration(1000); m_Fade->setStartValue(1.0); m_Fade->setEndValue(0.0);
  connect(m_Fade, &QPropertyAnimation::finished, this, [this] { m_Recent->hide(); Place(); });
  m_Hint = new QLabel(this); m_Hint->setObjectName("commandHint");
  m_Hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_Hint->setAutoFillBackground(true);
  if (!qApp->property("defaultQtStyle").toBool()) m_Hint->setStyleSheet("color:#8fd09a; background:#29313b; padding:4px 8px; border-radius:3px;");
  m_Hint->setTextFormat(Qt::RichText); m_Hint->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
  connect(m_Hint, &QLabel::linkActivated, this, [this](const QString& shortcut) { if (optionSelected) optionSelected(shortcut); });
  m_Hint->hide(); layout->addWidget(m_Hint);
  auto* bar = new QFrame(this); bar->setObjectName("commandRow"); bar->setAutoFillBackground(true);
  auto* row = new QHBoxLayout(bar); row->setContentsMargins(3, 3, 3, 3); row->setSpacing(5);
  auto* grip = new QLabel("⠿", bar); grip->setToolTip("Drag horizontally to move the command line"); grip->setCursor(Qt::SizeHorCursor);
  m_Grip = grip; grip->setObjectName("commandDragGrip"); row->addWidget(grip); grip->installEventFilter(this);
  auto* options = new QToolButton(bar); options->setIcon(QIcon(QtAsset("icons/c3d_dwgsettings.png"))); options->setIconSize(QSize(16, 16)); options->setAutoRaise(true); options->setAccessibleName("Command line options");
  auto* menu = new QMenu(options); menu->addAction("Command history (F2)", this, [this] { ToggleConsole(); });
  menu->addAction("Hide command line (Ctrl+9)", this, [this] { ToggleVisible(); });
  options->setMenu(menu); options->setPopupMode(QToolButton::InstantPopup); row->addWidget(options);
  auto* prompt = new QLabel("Command:", bar); if (!qApp->property("defaultQtStyle").toBool()) prompt->setStyleSheet("color:#83cc94; background:transparent; border:0;"); row->addWidget(prompt);
  m_Input = new QLineEdit(bar); m_Input->setObjectName("commandInput"); m_Input->setPlaceholderText("Type a command"); row->addWidget(m_Input, 1);
  auto inputPalette = m_Input->palette(); inputPalette.setColor(QPalette::PlaceholderText, QColor("#737373")); m_Input->setPalette(inputPalette);
  auto* history = new QToolButton(bar); history->setObjectName("commandHistoryToggle"); history->setArrowType(Qt::UpArrow); history->setAutoRaise(true); history->setToolTip("Expand command history (F2)");
  connect(history, &QToolButton::clicked, this, [this] { ToggleConsole(); }); row->addWidget(history);
  auto* size = new QLabel("⋮", bar); size->setToolTip("Drag to resize command line"); size->setCursor(Qt::SizeHorCursor);
  m_SizeGrip = size; size->setObjectName("commandResizeGrip"); row->addWidget(size); size->installEventFilter(this);
  layout->addWidget(bar);
  QSettings settings; m_PreferredWidth = settings.value("editor/commandWidth", 680).toInt(); m_Anchor = settings.value("editor/commandAnchor", 0.5).toDouble();
  viewport->installEventFilter(this);
  auto* fade = new QTimer(this); fade->setSingleShot(true); fade->setInterval(8000); fade->setObjectName("recentCommandTimer");
  connect(fade, &QTimer::timeout, m_Fade, [this] { m_Fade->start(); });
  Place(); show();
}

void FloatingCommandBar::Place() {
  const int width = std::clamp(m_PreferredWidth, std::min(320, std::max(1, parentWidget()->width() - 24)), std::max(1, parentWidget()->width() - 24));
  setFixedWidth(width); layout()->activate();
  const int height = sizeHint().height(); resize(width, height);
  move(12 + qRound(std::clamp(m_Anchor, 0.0, 1.0) * std::max(0, parentWidget()->width() - width - 24)), std::max(0, parentWidget()->height() - height - 12)); raise();
}
void FloatingCommandBar::Append(const QString& line) {
  m_Fade->stop(); m_Fade->targetObject()->setProperty("opacity", 1.0);
  m_Transcript->appendPlainText(line);
  m_Lines.append(line); while (m_Lines.size() > 3) m_Lines.removeFirst();
  m_Recent->setText(m_Lines.join('\n')); m_Recent->setVisible(!m_Transcript->isVisible());
  findChild<QTimer*>("recentCommandTimer")->start(); Place();
}
void FloatingCommandBar::ToggleConsole() {
  if (isHidden()) show();
  m_Transcript->setVisible(m_Transcript->isHidden()); m_Recent->hide(); Place();
  findChild<QToolButton*>("commandHistoryToggle")->setArrowType(m_Transcript->isHidden() ? Qt::UpArrow : Qt::DownArrow);
  if (m_Transcript->isVisible()) m_Transcript->ensureCursorVisible();
}
void FloatingCommandBar::ToggleVisible() { setVisible(isHidden()); if (isVisible()) { Place(); m_Input->setFocus(); } }
void FloatingCommandBar::SetHint(const QString& hint) {
  QString html;
  for (const auto& segment : cmdbar::ParsePromptSegments(hint.toUtf8().constData())) {
    const auto text = QString::fromUtf8(segment.text.c_str()).toHtmlEscaped();
    html += segment.isLink ? "<a href='" + QString::fromUtf8(segment.shortcut.c_str()).toHtmlEscaped() + "'>" + text + "</a>" : text;
  }
  if (m_Hint->text() == html) return;
  m_Hint->setText(html); m_Hint->setVisible(!html.isEmpty()); Place();
}
// An ignored Return from QLineEdit must stop here rather than reaching the viewport's
// repeat/finish-command handler and submitting a second, empty command.
void FloatingCommandBar::keyPressEvent(QKeyEvent* event) { event->accept(); }
bool FloatingCommandBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == parentWidget() && event->type() == QEvent::Resize) Place();
  if (watched == m_Grip || watched == m_SizeGrip) {
    if (event->type() == QEvent::MouseButtonPress) {
      auto* mouse = static_cast<QMouseEvent*>(event); if (mouse->button() != Qt::LeftButton) return false;
      m_Dragging = true; m_DragStart = mouse->globalPosition().toPoint(); m_StartX = x(); m_StartWidth = width(); return true;
    }
    if (event->type() == QEvent::MouseMove && m_Dragging) {
      const int delta = static_cast<QMouseEvent*>(event)->globalPosition().toPoint().x() - m_DragStart.x();
      if (watched == m_SizeGrip) m_PreferredWidth = std::clamp(m_StartWidth + delta, 320, std::max(320, parentWidget()->width() - 24));
      else m_Anchor = std::clamp(double(m_StartX + delta - 12) / std::max(1, parentWidget()->width() - width() - 24), 0.0, 1.0);
      Place(); return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && m_Dragging) {
      m_Dragging = false; QSettings settings; settings.setValue("editor/commandWidth", m_PreferredWidth); settings.setValue("editor/commandAnchor", m_Anchor); return true;
    }
  }
  return QFrame::eventFilter(watched, event);
}
