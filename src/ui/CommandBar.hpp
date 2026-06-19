#pragma once

// Pure helpers for the floating command bar (REQ-040). Kept free of ImGui so the
// fade/tail logic is unit-testable (CommandLineTests) without a UI harness.

#include <cstddef>

namespace cmdbar {

/// Opacity of the floating recent-history overlay (REQ-040). Fully opaque while the
/// log has been idle for less than \p fadeDelaySec, then ramps linearly to 0 over
/// \p fadeDurationSec, and stays 0 after. \p elapsedSec is the time since the last
/// log change. Result is clamped to [0, 1].
inline float HistoryAlpha(double elapsedSec, double fadeDelaySec, double fadeDurationSec) {
  if (elapsedSec <= fadeDelaySec)
    return 1.0f;
  if (fadeDurationSec <= 0.0)
    return 0.0f;
  const double t = (elapsedSec - fadeDelaySec) / fadeDurationSec;
  if (t >= 1.0)
    return 0.0f;
  return static_cast<float>(1.0 - t);
}

/// First line index to show when displaying the last \p maxLines of \p totalLines
/// (REQ-040: recent-history tail and F2 console). A non-positive \p maxLines shows
/// nothing (returns \p totalLines).
inline std::size_t LogTailStart(std::size_t totalLines, int maxLines) {
  if (maxLines <= 0)
    return totalLines;
  const std::size_t m = static_cast<std::size_t>(maxLines);
  return totalLines > m ? totalLines - m : 0;
}

} // namespace cmdbar
