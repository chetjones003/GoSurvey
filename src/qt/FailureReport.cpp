#include <GL/glew.h>
#include "FailureReport.hpp"
#include "EditorWindow.hpp"  // QtAsset
#include "FailureReportUi.hpp"
#include "StartupFailure.hpp"
#include "ViewportRenderer.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPalette>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

namespace {
QFont ReportFont() {
  for (const char* family : {"Cascadia Mono", "Consolas"})
    if (QFontDatabase::hasFamily(family)) return QFont(family, 10);
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPointSize(10);
  return font;
}

void StylePrimary(QPushButton* button) {
  const bool dark = button->palette().color(QPalette::Window).lightness() < 128;
  button->setStyleSheet(QString(
      "QPushButton { background:%1; color:white; font-weight:600; padding:6px 18px; "
      "border:0; border-radius:4px; } QPushButton:hover { background:%2; }")
                            .arg(dark ? "#168e8a" : "#438fda", dark ? "#1ca6a0" : "#347cc3"));
}

QLabel* Explanation(QWidget* parent) {
  auto* label = new QLabel(
      "The details below say what went wrong. A copy is sent to the GoSurvey developers "
      "automatically when this computer is online. To follow up yourself, choose Send This "
      "Report: it copies the report and opens a new GitHub issue.",
      parent);
  label->setObjectName("failureReportExplanation");
  label->setWordWrap(true);
  return label;
}
}  // namespace

QDialog* CreateFailureReportDialog(const QString& title, const QString& report, QWidget* parent,
                                   OpenUrlFunction openUrl) {
  if (!openUrl) openUrl = [](const QUrl& url) { return QDesktopServices::openUrl(url); };
  auto* dialog = new QDialog(parent);
  dialog->setObjectName("failureReport");
  dialog->setWindowTitle(title);
  dialog->setWindowIcon(QIcon(QtAsset("icons/app.ico")));
  dialog->setMinimumSize(820, 560);

  auto* text = new QPlainTextEdit(dialog);
  text->setObjectName("failureReportText");
  text->setReadOnly(true);
  text->setFont(ReportFont());
  text->setPlainText(report);
  auto* send = new QPushButton("Send This Report", dialog);
  send->setObjectName("failureReportSend");
  send->setMinimumHeight(32);
  send->setAutoDefault(false);
  auto* close = new QPushButton("Close", dialog);
  close->setObjectName("failureReportClose");
  close->setMinimumHeight(32);
  close->setMinimumWidth(100);
  close->setDefault(true);

  // A navy banner carries the title, matching the Software Update dialog.
  auto* outer = new QVBoxLayout(dialog);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto* banner = new QWidget(dialog);
  banner->setFixedHeight(76);
  const bool darkAppearance = qApp->palette().color(QPalette::Window).lightness() < 128;
  banner->setStyleSheet(darkAppearance
      ? "QWidget { background:#30373c; border-bottom:2px solid #55d8d0; }"
      : "QWidget { background:#243a53; }");
  auto* bannerLayout = new QHBoxLayout(banner);
  bannerLayout->setContentsMargins(26, 10, 26, 10);
  auto* logo = new QLabel(banner);
  logo->setPixmap(QIcon(QtAsset("icons/app.ico")).pixmap(48, 48));
  bannerLayout->addWidget(logo);
  auto* bannerText = new QLabel(title, banner);
  bannerText->setStyleSheet("color:#f4f7fb; font-size:20px; font-weight:700;");
  bannerLayout->addWidget(bannerText, 1);
  outer->addWidget(banner);
  auto* content = new QWidget(dialog);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(26, 18, 26, 20);
  layout->setSpacing(10);
  layout->addWidget(Explanation(content));
  layout->addWidget(text, 1);
  auto* row = new QHBoxLayout();
  row->setSpacing(8);
  StylePrimary(send);
  row->addWidget(send);
  row->addWidget(close);
  row->addStretch();
  layout->addLayout(row);
  outer->addWidget(content, 1);

  QObject::connect(send, &QPushButton::clicked, dialog, [dialog, report, openUrl] {
    // Read back rather than trust setText: another program can hold the clipboard open, and
    // then the user must be told to copy the text by hand.
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(report);
    const bool copied = clipboard->text() == report;
    const std::string url = failureReportUi::GitHubIssueUrlForReport(report.toUtf8().toStdString());
    const bool opened = openUrl(QUrl::fromEncoded(QByteArray::fromStdString(url)));
    QMessageBox::information(
        dialog, "Send This Report",
        QString::fromUtf8(failureReportUi::SendOutcomeMessage(copied, opened).c_str()));
  });
  QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
  close->setFocus();
  return dialog;
}

void ExecFailureReportDialog(const std::string& titleUtf8, const std::string& reportUtf8) {
  std::unique_ptr<QDialog> dialog(CreateFailureReportDialog(
      QString::fromUtf8(titleUtf8.c_str()), QString::fromUtf8(reportUtf8.c_str())));
  dialog->exec();
}

bool ProbeStartupGraphics(startupFailure::Stage& stage, std::string& detail) {
  const QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  QOffscreenSurface surface;
  surface.setFormat(format);
  surface.create();
  QOpenGLContext context;
  context.setFormat(format);
  if (!context.create() || !surface.isValid() || !context.makeCurrent(&surface)) {
    stage = startupFailure::Stage::GraphicsContext;
    detail = "Qt could not create an OpenGL 3.3 core context.";
    return false;
  }
  const auto version = context.format().version();
  if (version < qMakePair(3, 3)) {
    context.doneCurrent();
    stage = startupFailure::Stage::GraphicsContext;
    detail = "The graphics driver offers OpenGL " + std::to_string(version.first) + "." +
             std::to_string(version.second) + "; GoSurvey needs 3.3.";
    return false;
  }
  glewExperimental = GL_TRUE;
  ViewportRenderer renderer;
  std::string error;
  const bool ok = renderer.Init(&error);
  if (ok) renderer.Shutdown();
  context.doneCurrent();
  if (!ok) {
    stage = startupFailure::Stage::OpenGlInit;
    detail = error;
  }
  return ok;
}
