#include <GoSurvey/Document/PreparedPlotScales.hpp>
#include <GoSurvey/Document/PreparedMtextRichFormat.hpp>

#include <cstdlib>

int main() {
  const auto metric = DocumentPlotScaleChoicesFor(4);
  if (metric.size() != 11 || metric.front().label != "1:1" || metric.front().modelUnitsPerPlottedInch != 25.4f)
    return EXIT_FAILURE;
  if (DocumentPlotScaleLabel(6, 0.0254f) != "1:1") return EXIT_FAILURE;
  if (MtextRichFlattenToPlain("public consumer") != "public consumer") return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
