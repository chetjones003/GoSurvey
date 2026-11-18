#include "../../util/PlotScales.hpp"
#include <GoSurvey/Document/PreparedPlotScales.hpp>

namespace {
std::vector<PlotScaleChoice> ToLegacyChoices(const std::vector<DocumentPlotScaleChoice>& choices) {
  std::vector<PlotScaleChoice> legacy;
  legacy.reserve(choices.size());
  for (const auto& choice : choices) legacy.push_back({choice.label, choice.modelUnitsPerPlottedInch});
  return legacy;
}
} // namespace

bool DocumentPlotScaleUnitsAreMetric(int insUnits) { return PlotScaleUnitsAreMetric(insUnits); }
double DocumentMetricPlotScaleUnitsPerInch(int insUnits) { return MetricPlotScaleUnitsPerInch(insUnits); }
std::vector<DocumentPlotScaleChoice> DocumentPlotScaleChoicesFor(int insUnits) {
  const auto legacy = PlotScaleChoicesFor(insUnits);
  std::vector<DocumentPlotScaleChoice> choices;
  choices.reserve(legacy.size());
  for (const auto& choice : legacy) choices.push_back({choice.label, choice.modelUnitsPerPlottedInch});
  return choices;
}
int DocumentPlotScaleChoiceIndex(const std::vector<DocumentPlotScaleChoice>& choices, float mup) {
  return PlotScaleChoiceIndex(ToLegacyChoices(choices), mup);
}
std::string DocumentPlotScaleLabel(int insUnits, float mup) { return PlotScaleLabel(insUnits, mup); }
