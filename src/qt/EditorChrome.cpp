#include "EditorChrome.hpp"
#include <QEnterEvent>
#include <QMouseEvent>
#include "EditorWindow.hpp"
#include "CommandBar.hpp"
#include "TopographicArtwork.hpp"
#include <QApplication>
#include <QMetaObject>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <QEvent>
#include <QSettings>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <array>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
class TitleStrip final : public QWidget {
public:
  explicit TitleStrip(QMainWindow* window) : QWidget(window), m_Window(window) { setObjectName("editorTitleBar"); window->installEventFilter(this); }
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    if (!dark) {
      painter.fillRect(rect(), palette().color(QPalette::Window));
      return;
    }
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0.0, QColor("#171b1e"));
    background.setColorAt(0.65, QColor("#1d2226"));
    background.setColorAt(1.0, QColor("#282e33"));
    painter.fillRect(rect(), background);
    static const QPixmap terrain = [] {
      QPixmap image(500, 366);
      image.fill(Qt::transparent);
      QPainter artwork(&image);
      qtArtwork::DrawTerrainContours(artwork, QRectF(0, 0, 500, 366), true, .80);
      return image;
    }();
    painter.drawPixmap(width() - 600, -170, terrain);
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == m_Window && event->type() == QEvent::WindowStateChange)
      if (auto* button = findChild<QToolButton*>("windowMaximize")) button->setText(m_Window->isMaximized() ? "❐" : "□");
    return QWidget::eventFilter(watched, event);
  }
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && m_Window->windowHandle()) {
#ifdef _WIN32
      const QPoint global = event->globalPosition().toPoint();
      const qreal ratio = m_Window->devicePixelRatioF();
      const LPARAM screenPosition = MAKELPARAM(qRound(global.x() * ratio), qRound(global.y() * ratio));
      const HWND hwnd = reinterpret_cast<HWND>(m_Window->winId());
      ReleaseCapture();
      SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, screenPosition);
      event->accept();
      return;
#else
      m_Window->windowHandle()->startSystemMove();
      event->accept();
      return;
#endif
    }
    QWidget::mousePressEvent(event);
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
  strip->setFixedHeight(42);
  auto* row = new QHBoxLayout(strip); row->setContentsMargins(12, 0, 8, 0); row->setSpacing(8);
  auto* app = new QToolButton(strip); app->setObjectName("applicationMenu");
  app->setAccessibleName("GoSurvey application menu"); app->setToolTip("GoSurvey application menu (Alt+F)");
  app->setIcon(windowIcon()); app->setIconSize(QSize(28, 28)); app->setFixedSize(40, 34); app->setAutoRaise(true);
  app->setMenu(applicationMenu); app->setPopupMode(QToolButton::InstantPopup);
  row->addWidget(app);
  connect(new QShortcut(QKeySequence("Alt+F"), this), &QShortcut::activated, app, &QToolButton::showMenu);
  for (const auto& command : {"NEW", "OPEN", "SAVE", "UNDO", "REDO"}) {
    const auto actions = findChildren<QAction*>("command_" + QString(command));
    if (actions.empty()) continue;
    auto* tool = new QToolButton(strip); tool->setObjectName("quick_" + QString(command));
    tool->setDefaultAction(actions.first()); tool->setIconSize(QSize(19, 19)); tool->setFixedSize(30, 30); tool->setAutoRaise(true);
    row->addWidget(tool);
  }
  row->addSpacing(16);
  m_WindowTitle = new QLabel(windowTitle(), strip); m_WindowTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_WindowTitle->setObjectName("windowBrandTitle");
  m_WindowTitle->setMinimumWidth(0); row->addWidget(m_WindowTitle, 1);
  auto control = [&](const char* name, const char* label, const char* glyph) {
    auto* button = new QToolButton(strip); button->setObjectName(name); button->setAccessibleName(label); button->setToolTip(label);
    button->setText(QString::fromUtf8(glyph)); button->setFont(QFont("Segoe UI", 13)); button->setFixedSize(46, 42); button->setAutoRaise(true); row->addWidget(button); return button;
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
  if (msg->message == WM_COPYDATA) {
    constexpr ULONG_PTR kOpenDrawingCopyData = 0x47534457; // "GSDW"
    const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(msg->lParam);
    if (data != nullptr && data->dwData == kOpenDrawingCopyData && data->lpData != nullptr &&
        data->cbData >= sizeof(wchar_t) && data->cbData % sizeof(wchar_t) == 0) {
      const auto* widePath = static_cast<const wchar_t*>(data->lpData);
      int charCount = static_cast<int>(data->cbData / sizeof(wchar_t));
      while (charCount > 0 && widePath[charCount - 1] == L'\0')
        --charCount;
      const QString path = QString::fromWCharArray(widePath, charCount);
      if (!path.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, path] {
          showNormal();
          raise();
          activateWindow();
          OpenDrawing(path);
        }, Qt::QueuedConnection);
        *result = TRUE;
        return true;
      }
    }
    *result = FALSE;
    return true;
  }
  if (msg->message == WM_NCCALCSIZE) {
    // Keep the custom Qt title bar inside the full client area while retaining the
    // native sizing/caption styles that Windows uses for resize and Snap Assist.
    // A maximized frame window overhangs the monitor by its sizing border on every side; pull the
    // client area back in so the edges of the app are not cut off by the screen.
    if (msg->wParam && IsZoomed(msg->hwnd)) {
      auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam); // NOLINT(performance-no-int-to-ptr): Win32 encodes NCCALCSIZE_PARAMS* in LPARAM.
      const UINT dpi = GetDpiForWindow(msg->hwnd);
      const int borderX = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
      const int borderY = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
      params->rgrc[0].left += borderX;
      params->rgrc[0].right -= borderX;
      params->rgrc[0].top += borderY;
      params->rgrc[0].bottom -= borderY;
    }
    *result = 0;
    return true;
  }
  if (msg->message == WM_NCHITTEST) {
    POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
    ScreenToClient(msg->hwnd, &point);
    const qreal ratio = devicePixelRatioF();
    const int edge = qMax(1, qRound(8 * ratio));
    const int logicalX = qRound(point.x / ratio), logicalY = qRound(point.y / ratio);
    if (!isMaximized() && !isFullScreen()) {
      const bool left = point.x < edge, right = point.x >= qRound(width() * ratio) - edge;
      const bool top = point.y < edge, bottom = point.y >= qRound(height() * ratio) - edge;
      if (top && left) *result = HTTOPLEFT;
      else if (top && right) *result = HTTOPRIGHT;
      else if (bottom && left) *result = HTBOTTOMLEFT;
      else if (bottom && right) *result = HTBOTTOMRIGHT;
      else if (left) *result = HTLEFT;
      else if (right) *result = HTRIGHT;
      else if (top) *result = HTTOP;
      else if (bottom) *result = HTBOTTOM;
      else goto titleHitTest;
      return true;
    }
titleHitTest:
    {
      auto* title = findChild<QWidget*>("editorTitleBar");
      const QPoint local(logicalX, logicalY);
      if (title && title->geometry().contains(local)) {
        QWidget* child = childAt(local);
        while (child && child != title) {
          if (qobject_cast<QToolButton*>(child)) break;
          child = child->parentWidget();
        }
        if (child == title) { *result = HTCAPTION; return true; }
      }
      *result = HTCLIENT;
    }
    return true;
  }
  if (msg->message == WM_ENTERSIZEMOVE) {
    BeginInteractiveResize();
  } else if (msg->message == WM_EXITSIZEMOVE) {
    EndInteractiveResize();
  }
  if (msg->message == WM_GETMINMAXINFO) {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
      auto* info = reinterpret_cast<MINMAXINFO*>(msg->lParam); // NOLINT(performance-no-int-to-ptr): Win32 encodes MINMAXINFO* in LPARAM.
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
  setMouseTracking(true);
  setCursor(Qt::ArrowCursor);
  if (!qApp->property("defaultQtStyle").toBool()) setStyleSheet("#floatingCommandBar { background:transparent; border:0; } #commandRow { background:#343b45; border:1px solid #596572; border-radius:4px; } #commandInput { background:#f4f5f6; color:#20262d; border:0; border-radius:2px; padding:6px; } #commandRecent { color:#cdd7e2; background:#29313b; padding:5px 9px; border-radius:3px; } QToolButton { border:0; padding:3px; }");
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(8);
  m_Transcript = new QPlainTextEdit(this); m_Transcript->setObjectName("commandConsole");
  m_Transcript->setReadOnly(true); m_Transcript->setMaximumBlockCount(5000); m_Transcript->setLineWrapMode(QPlainTextEdit::WidgetWidth); m_Transcript->setFixedHeight(200);
  m_Transcript->setCursor(Qt::IBeamCursor);
  m_Transcript->setFont(QFont("Consolas", 10)); m_Transcript->hide(); layout->addWidget(m_Transcript);
  m_Recent = new QLabel(this); m_Recent->setObjectName("commandRecent"); m_Recent->setTextFormat(Qt::PlainText);
  m_Recent->setWordWrap(true);
  m_Recent->setToolTip("Recent command message. Expand command history or press F2 to view the full transcript.");
  m_Recent->setCursor(Qt::ArrowCursor);
  m_Recent->setAutoFillBackground(true);
  m_Recent->setMaximumHeight(70); m_Recent->hide(); layout->addWidget(m_Recent);
  auto* opacity = new QGraphicsOpacityEffect(m_Recent); opacity->setOpacity(1); m_Recent->setGraphicsEffect(opacity);
  m_Fade = new QPropertyAnimation(opacity, "opacity", this); m_Fade->setDuration(1000); m_Fade->setStartValue(1.0); m_Fade->setEndValue(0.0);
  connect(m_Fade, &QPropertyAnimation::finished, this, [this] { m_Recent->hide(); Place(); });
  m_Hint = new QLabel(this); m_Hint->setObjectName("commandHint");
  m_Hint->setCursor(Qt::ArrowCursor);
  m_Hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_Hint->setAutoFillBackground(true);
  if (!qApp->property("defaultQtStyle").toBool()) m_Hint->setStyleSheet("color:#8fd09a; background:#29313b; padding:4px 8px; border-radius:3px;");
  m_Hint->setTextFormat(Qt::RichText); m_Hint->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
  // The hint is a link-bearing overlay above the viewport. Keep the complete press/release sequence
  // with the label, including clicks on link glyphs, so the viewport cannot also treat it as a pick.
  m_Hint->setAttribute(Qt::WA_NoMousePropagation);
  connect(m_Hint, &QLabel::linkActivated, this, [this](const QString& shortcut) { if (optionSelected) optionSelected(shortcut); });
  m_Hint->hide(); layout->addWidget(m_Hint);
  auto* bar = new QFrame(this); bar->setObjectName("commandRow"); bar->setAutoFillBackground(true);
  bar->setCursor(Qt::ArrowCursor);
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
  const int availableWidth = std::max(1, parentWidget()->width() - 24);
  const bool compactHistory = m_AdaptiveFullWidth && m_Transcript->isVisible();
  const int width = compactHistory ? std::min(380, availableWidth) : m_AdaptiveFullWidth ? availableWidth :
      std::clamp(m_PreferredWidth, std::min(320, availableWidth), availableWidth);
  setFixedWidth(width); layout()->activate();
  const int height = sizeHint().height(); resize(width, height);
  if (compactHistory) {
    // Keep expanded history in the upper-left so drawing geometry and the ViewCube stay visible.
    move(12, 12);
  } else {
    move(12 + qRound(std::clamp(m_Anchor, 0.0, 1.0) * std::max(0, parentWidget()->width() - width - 24)),
         std::max(0, parentWidget()->height() - height - 12));
  }
  raise();
}
void FloatingCommandBar::SetAdaptiveFullWidth(bool enabled) {
  if (m_AdaptiveFullWidth == enabled) return;
  m_AdaptiveFullWidth = enabled;
  m_Transcript->setFixedHeight(enabled ? 96 : 200);
  m_Grip->setVisible(!enabled);
  m_SizeGrip->setVisible(!enabled);
  if (!m_Recent->isHidden() && !m_Lines.isEmpty())
    m_Recent->setText(enabled ? m_Lines.constLast() : m_Lines.join('\n'));
  Place();
}
void FloatingCommandBar::Append(const QString& line) {
  if (line.startsWith("Block library — loaded ") && m_Transcript->toPlainText().contains(line))
    return;
  const bool backgroundInfo = line.startsWith("Block library — loaded ");
  m_Fade->stop(); m_Fade->targetObject()->setProperty("opacity", 1.0);
  m_Transcript->appendPlainText(line);
  if (backgroundInfo) {
    m_Recent->hide();
    Place();
    return;
  }
  m_Lines.append(line); while (m_Lines.size() > 3) m_Lines.removeFirst();
  m_Recent->setText(m_AdaptiveFullWidth ? m_Lines.constLast() : m_Lines.join('\n'));
  m_Recent->setVisible(!m_Transcript->isVisible());
  findChild<QTimer*>("recentCommandTimer")->start(); Place();
}
void FloatingCommandBar::ToggleConsole() {
  if (isHidden()) show();
  m_Transcript->setVisible(m_Transcript->isHidden()); m_Recent->hide(); Place();
  findChild<QToolButton*>("commandHistoryToggle")->setArrowType(m_Transcript->isHidden() ? Qt::UpArrow : Qt::DownArrow);
  if (m_Transcript->isVisible()) {
    m_Transcript->ensureCursorVisible();
    QTimer::singleShot(0, m_Transcript, [this] {
      m_Transcript->verticalScrollBar()->setValue(m_Transcript->verticalScrollBar()->maximum());
    });
  }
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

void FloatingCommandBar::enterEvent(QEnterEvent* event) {
  if (refreshViewportCursor) refreshViewportCursor();
  QFrame::enterEvent(event);
}

void FloatingCommandBar::leaveEvent(QEvent* event) {
  if (refreshViewportCursor) refreshViewportCursor();
  QFrame::leaveEvent(event);
}

void FloatingCommandBar::mouseMoveEvent(QMouseEvent* event) {
  if (refreshViewportCursor) refreshViewportCursor();
  QFrame::mouseMoveEvent(event);
}

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
