#pragma once
// The drawing's plot-scale list (REQ-357, D-2026-09-29-c): one list, shared by the status-bar
// plot-scale dropdown and the Drawing Settings window, chosen by the drawing unit. A scale is stored
// as model units per plotted inch (`AppCommandState::modelUnitsPerPlottedInch`).

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

struct PlotScaleChoice {
  std::string label;
  float modelUnitsPerPlottedInch = 0.f;
};

/// True for the metric drawing units (INSUNITS 4 = millimeters, 6 = meters).
[[nodiscard]] inline bool PlotScaleUnitsAreMetric(int insUnits) { return insUnits == 4 || insUnits == 6; }

/// Model units in one plotted inch for a metric `1:N` scale: `0.0254·N` m or `25.4·N` mm.
[[nodiscard]] inline double MetricPlotScaleUnitsPerInch(int insUnits) { return insUnits == 4 ? 25.4 : 0.0254; }

/// The scales offered for a drawing in \p insUnits. Imperial `1" = N'` (N feet per plotted inch, or
/// 12·N in an Inches drawing) for Feet, Inches and Unitless; metric `1:N` for Meters and Millimeters.
[[nodiscard]] inline std::vector<PlotScaleChoice> PlotScaleChoicesFor(int insUnits) {
  std::vector<PlotScaleChoice> out;
  char buf[32];
  if (PlotScaleUnitsAreMetric(insUnits)) {
    const double perInch = MetricPlotScaleUnitsPerInch(insUnits);
    for (int n : {1, 10, 20, 50, 100, 200, 250, 500, 1000, 2000, 5000}) {
      std::snprintf(buf, sizeof(buf), "1:%d", n);
      out.push_back({buf, static_cast<float>(perInch * n)});
    }
    return out;
  }
  const double perFoot = insUnits == 1 ? 12.0 : 1.0;
  for (int n : {1, 2, 5, 10, 20, 30, 40, 50, 60, 80, 100, 120, 200, 300, 400, 500}) {
    std::snprintf(buf, sizeof(buf), "1\" = %d'", n);
    out.push_back({buf, static_cast<float>(perFoot * n)});
  }
  return out;
}

/// Index of \p mup in \p choices (relative tolerance, so `1:1` in meters still matches), or -1.
[[nodiscard]] inline int PlotScaleChoiceIndex(const std::vector<PlotScaleChoice>& choices, float mup) {
  for (size_t i = 0; i < choices.size(); ++i) {
    const double v = choices[i].modelUnitsPerPlottedInch;
    if (std::fabs(static_cast<double>(mup) - v) <= 1e-4 * v)
      return static_cast<int>(i);
  }
  return -1;
}

/// The label for \p mup in a drawing in \p insUnits: the list's own label, or a "(custom)" one.
[[nodiscard]] inline std::string PlotScaleLabel(int insUnits, float mup) {
  const std::vector<PlotScaleChoice> choices = PlotScaleChoicesFor(insUnits);
  const int ix = PlotScaleChoiceIndex(choices, mup);
  if (ix >= 0)
    return choices[static_cast<size_t>(ix)].label;
  char buf[48];
  if (PlotScaleUnitsAreMetric(insUnits))
    std::snprintf(buf, sizeof(buf), "1:%.4g (custom)", mup / MetricPlotScaleUnitsPerInch(insUnits));
  else
    std::snprintf(buf, sizeof(buf), "1\" = %.4g' (custom)", mup / (insUnits == 1 ? 12.0 : 1.0));
  return buf;
}
