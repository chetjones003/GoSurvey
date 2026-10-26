#include <GL/glew.h>
#include "FailureReport.hpp"
#include "QtAccent.hpp"
#include "EditorWindow.hpp"  // QtAsset
#include "FailureReportUi.hpp"
#include "StartupFailure.hpp"
#include "ViewportRenderer.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFrame>
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
  QPalette palette = button->palette();
  palette.setColor(QPalette::Button, QColor(QtAccentFor(dark).fill));
  palette.setColor(QPalette::ButtonText, Qt::white);
  button->setPalette(palette);
  button->setStyleSheet(QString(
      "QPushButton#failureReportSend { background-color:%1; font-weight:600; "
      "padding:6px 18px; border:0; border-radius:4px; } "
      "QPushButton#failureReportSend:hover { background-color:%2; }")
                            .arg(QtAccentFor(dark).fill, QtAccentFor(dark).fillHover));
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

  const bool darkAppearance = qApp->palette().color(QPalette::Window).lightness() < 128;
  const QString window = qApp->palette().color(QPalette::Window).name();
  const QString base = qApp->palette().color(QPalette::Base).name();
  const QString textColor = qApp->palette().color(QPalette::WindowText).name();
  const QString muted = darkAppearance ? "#b5c0c5" : "#5f6770";
  const QString edge = darkAppearance ? "#48535a" : "#c8cdd3";
  const QString buttonSurface = qApp->palette().color(QPalette::Button).name();
  dialog->setStyleSheet(QString("QDialog#failureReport { background:%1; color:%2; }").arg(window, textColor));

  auto* outer = new QVBoxLayout(dialog);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* banner = new QFrame(dialog);
  banner->setObjectName("failureReportHeader");
  banner->setStyleSheet(QtApplyAccent(QString(
      "QFrame#failureReportHeader { background:%1; border-bottom:1px solid %2; "
      "border-left:4px solid @strong@; }").arg(darkAppearance ? "#242c31" : "#f3f6f8",
                                                    darkAppearance ? "#3d474e" : "#dce3e8"),
      darkAppearance));
  auto* bannerLayout = new QHBoxLayout(banner);
  bannerLayout->setContentsMargins(22, 16, 26, 16);
  bannerLayout->setSpacing(16);
  auto* logo = new QLabel(banner);
  logo->setPixmap(QIcon(QtAsset("icons/app.ico")).pixmap(42, 42));
  bannerLayout->addWidget(logo, 0, Qt::AlignVCenter);
  auto* headingBlock = new QWidget(banner);
  auto* headingLayout = new QVBoxLayout(headingBlock);
  headingLayout->setContentsMargins(0, 0, 0, 0);
  headingLayout->setSpacing(3);
  auto* eyebrow = new QLabel(title.contains("could not start", Qt::CaseInsensitive)
                                 ? "STARTUP FAILURE" : "CRASH REPORT", headingBlock);
  eyebrow->setObjectName("failureReportKind");
  eyebrow->setStyleSheet(QString("color:%1; font-size:10px; font-weight:700; letter-spacing:1px;")
                             .arg(QtAccentFor(darkAppearance).strong));
  auto* heading = new QLabel(title, headingBlock);
  heading->setObjectName("failureReportHeading");
  heading->setStyleSheet(QString("color:%1; font-size:21px; font-weight:700;").arg(textColor));
  headingLayout->addWidget(eyebrow);
  headingLayout->addWidget(heading);
  bannerLayout->addWidget(headingBlock, 1, Qt::AlignVCenter);
  outer->addWidget(banner);

  auto* text = new QPlainTextEdit(dialog);
  text->setObjectName("failureReportText");
  text->setReadOnly(true);
  text->setFont(ReportFont());
  text->setPlainText(report);
  text->setStyleSheet(QString("QPlainTextEdit { background:%1; color:%2; border:1px solid %3; "
                              "border-radius:6px; padding:10px; selection-background-color:%4; }")
                          .arg(base, textColor, edge, QtAccentFor(darkAppearance).tint));
  auto* send = new QPushButton("Send This Report", dialog);
  send->setObjectName("failureReportSend");
  send->setMinimumHeight(32);
  send->setAutoDefault(false);
  auto* close = new QPushButton("Close", dialog);
  close->setObjectName("failureReportClose");
  close->setMinimumHeight(32);
  close->setMinimumWidth(100);
  close->setDefault(true);

  auto* content = new QWidget(dialog);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(24, 18, 24, 20);
  layout->setSpacing(12);
  auto* explanation = new QFrame(content);
  explanation->setObjectName("failureReportNextSteps");
  explanation->setStyleSheet(QString("QFrame#failureReportNextSteps { background:%1; border:1px solid %2; "
                                     "border-radius:6px; }")
                                 .arg(darkAppearance ? "#252e33" : "#f7f8fa", edge));
  auto* explanationLayout = new QVBoxLayout(explanation);
  explanationLayout->setContentsMargins(14, 10, 14, 10);
  explanationLayout->setSpacing(4);
  auto* nextSteps = new QLabel("What happens next", explanation);
  nextSteps->setObjectName("failureReportNextStepsHeading");
  nextSteps->setStyleSheet(QString("font-weight:700; color:%1;").arg(textColor));
  auto* explanationText = new QLabel(
      "The details below say what went wrong. A copy is sent to the GoSurvey developers "
      "automatically when this computer is online. To follow up yourself, choose Send This "
      "Report: it copies the report and opens a new GitHub issue.", explanation);
  explanationText->setObjectName("failureReportExplanation");
  explanationText->setWordWrap(true);
  explanationText->setStyleSheet(QString("color:%1;").arg(muted));
  explanationLayout->addWidget(nextSteps);
  explanationLayout->addWidget(explanationText);
  layout->addWidget(explanation);
  layout->addWidget(text, 1);
  auto* row = new QHBoxLayout();
  row->setSpacing(8);
  row->addStretch();
  close->setStyleSheet(QString("QPushButton { background:%1; color:%2; padding:6px 18px; "
                                "border:1px solid %3; border-radius:4px; }")
                           .arg(buttonSurface, textColor, edge));
  row->addWidget(close);
  StylePrimary(send);
  row->addWidget(send);
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
