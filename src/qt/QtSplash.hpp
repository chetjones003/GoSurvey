#pragma once
#include <QPixmap>
#include <QString>

// REQ-093 / issue #1028: the Qt startup splash. The progress bar and phase label are cosmetic
// (they report elapsed time, never real startup work) and run for a fixed minimum display time,
// counted from the moment the main window has been built.
namespace qtsplash {

constexpr int kMinimumDisplayMs = 8000;

// Fraction 0..1 of the minimum display time that has passed.
double ProgressAt(qint64 elapsedMs);

// Neutral phase label for the elapsed time; the display time is split evenly across the phases.
QString PhaseLabelAt(qint64 elapsedMs);

// Splash card with background and wordmark, without the progress bar or phase label.
QPixmap MakeBackdrop(const QPixmap& source);

// The backdrop plus the phase label and a progress bar filled to `progress` (clamped to 0..1).
QPixmap MakeFrame(const QPixmap& backdrop, double progress, const QString& label);

}  // namespace qtsplash
