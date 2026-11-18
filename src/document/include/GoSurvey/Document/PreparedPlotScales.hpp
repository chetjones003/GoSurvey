#pragma once

// Preparation facade for the existing drawing plot-scale policy. This deliberately has a
// staging name: the legacy declaration remains in src/util until the pure-move follow-up.
#include <string>
#include <vector>

struct DocumentPlotScaleChoice {
  std::string label;
  float modelUnitsPerPlottedInch = 0.f;
};

[[nodiscard]] bool DocumentPlotScaleUnitsAreMetric(int insUnits);
[[nodiscard]] double DocumentMetricPlotScaleUnitsPerInch(int insUnits);
[[nodiscard]] std::vector<DocumentPlotScaleChoice> DocumentPlotScaleChoicesFor(int insUnits);
[[nodiscard]] int DocumentPlotScaleChoiceIndex(const std::vector<DocumentPlotScaleChoice>& choices, float mup);
[[nodiscard]] std::string DocumentPlotScaleLabel(int insUnits, float mup);
