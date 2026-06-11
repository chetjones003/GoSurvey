#pragma once

#include <algorithm>
#include <cstdio>
#include <string>

/// Centralized display formatting for user-facing coordinate/length readouts.
/// Pure and UI/GL-free (ADR-002) so it is unit-testable in the Domain test
/// target. These functions control only how many decimals the *user sees* —
/// stored values keep full precision. Non-survey readouts use
/// AppCommandState::displayLinearPrecision; survey readouts use
/// AppCommandState::surveyPointDisplayPrecision.

/// Clamp a requested decimal count to a sane printable range.
[[nodiscard]] inline int DisplayPrecisionClamp(int precision) {
  return std::clamp(precision, 0, 12);
}

/// Build a printf float token (e.g. "%.4f") for the given decimal precision.
[[nodiscard]] inline std::string DisplayFloatFmt(int precision) {
  char fmt[8];
  std::snprintf(fmt, sizeof(fmt), "%%.%df", DisplayPrecisionClamp(precision));
  return fmt;
}

/// Format one linear/coordinate value at the given display precision.
[[nodiscard]] inline std::string FormatLinear(double value, int precision) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), DisplayFloatFmt(precision).c_str(), value);
  return buf;
}
