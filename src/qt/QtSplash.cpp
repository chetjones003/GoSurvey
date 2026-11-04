#include "QtSplash.hpp"
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRectF>
#include <algorithm>

namespace qtsplash {
namespace {
constexpr int kCardWidth = 800;
constexpr int kCardHeight = 450;
constexpr int kLeft = 46;
constexpr int kLabelBaseline = 359;
constexpr double kBarTop = 372;
constexpr double kBarWidth = 280;
constexpr double kBarHeight = 5;

QPixmap ScaleBackdropSource(const QPixmap& source, int physW, int physH) {
  if (source.isNull()) {
    QPixmap card(physW, physH);
    card.fill(QColor("#111820"));
    return card;
  }
  QPixmap card =
      source.scaled(QSize(physW, physH), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  if (card.width() > physW || card.height() > physH) {
    const int x = (card.width() - physW) / 2;
    const int y = (card.height() - physH) / 2;
    card = card.copy(x, y, physW, physH);
  } else if (card.width() != physW || card.height() != physH) {
    card = card.scaled(physW, physH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  return card;
}
}  // namespace

double ProgressAt(qint64 elapsedMs) {
  return std::clamp(static_cast<double>(elapsedMs) / kMinimumDisplayMs, 0.0, 1.0);
}

QString PhaseLabelAt(qint64 elapsedMs) {
  static const QString kPhases[] = {
      QStringLiteral("Starting GoSurvey…"),         QStringLiteral("Gathering resources…"),
      QStringLiteral("Preparing workspace…"),       QStringLiteral("Setting up drawing tools…"),
      QStringLiteral("Loading your settings…"),   QStringLiteral("Almost ready…")};
  constexpr int kPhaseCount = static_cast<int>(std::size(kPhases));
  const int phase = std::min(static_cast<int>(ProgressAt(elapsedMs) * kPhaseCount), kPhaseCount - 1);
  return kPhases[phase];
}

QPixmap MakeBackdrop(const QPixmap& source, qreal devicePixelRatio) {
  const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
  const int physW = qMax(1, qRound(static_cast<double>(kCardWidth) * dpr));
  const int physH = qMax(1, qRound(static_cast<double>(kCardHeight) * dpr));
  QPixmap card = ScaleBackdropSource(source, physW, physH);
  card.setDevicePixelRatio(dpr);
  QPainter painter(&card);
  painter.setRenderHint(QPainter::TextAntialiasing);
  const QFont wordmarkFont("Segoe UI", 40, QFont::DemiBold);
  painter.setFont(wordmarkFont);
  const QFontMetrics wordmarkMetrics(wordmarkFont);
  const int wordmarkY = 226;
  painter.setPen(QColor("#1688ff"));
  painter.drawText(kLeft, wordmarkY, "Go");
  painter.setPen(Qt::white);
  painter.drawText(kLeft + wordmarkMetrics.horizontalAdvance("Go"), wordmarkY, "Survey");
  painter.setPen(QColor("#c2d0e2"));
  painter.setFont(QFont("Segoe UI", 16));
  painter.drawText(kLeft + 2, 268, "Tools for the Surveyor");
  return card;
}

QPixmap MakeFrame(const QPixmap& backdrop, double progress, const QString& label, qreal devicePixelRatio) {
  (void)devicePixelRatio;
  QPixmap frame = backdrop;
  QPainter painter(&frame);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QColor("#c2d0e2"));
  painter.setFont(QFont("Segoe UI", 11));
  painter.drawText(kLeft + 2, kLabelBaseline, label);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor("#253c57"));
  painter.drawRoundedRect(QRectF(kLeft + 2, kBarTop, kBarWidth, kBarHeight), 2.5, 2.5);
  const double filled = kBarWidth * std::clamp(progress, 0.0, 1.0);
  if (filled >= 1.0) {
    painter.setBrush(QColor("#148cff"));
    painter.drawRoundedRect(QRectF(kLeft + 2, kBarTop, filled, kBarHeight), 2.5, 2.5);
  }
  return frame;
}

}  // namespace qtsplash
