#include <GoSurvey/Document/CadSolidPtr.hpp>
#include <GoSurvey/Document/EntityRef.hpp>
#include <GoSurvey/Document/PlotScalesPublic.hpp>
#include <GoSurvey/Document/MtextRichFormatPublic.hpp>
#include <GoSurvey/Document/MtextRichSpansPublic.hpp>
#include <GoSurvey/Document/MtextTextOpsPublic.hpp>
#include <GoSurvey/Document/TablePublic.hpp>
#include <GoSurvey/Document/TableStylePublic.hpp>
#include <GoSurvey/Document/StylePropertyPublic.hpp>

#include <cstdlib>
#include <memory>
#include <type_traits>

static_assert(std::is_same_v<CadSolidPtr, std::shared_ptr<const brep::Solid>>);

#if __has_include("CadCommands.hpp") || __has_include("CadEntities.hpp")
#error Document consumer must not inherit application-private include roots
#endif

int main() {
  const auto metric = PlotScaleChoicesFor(4);
  if (metric.size() != 11 || metric.front().label != "1:1" || metric.front().modelUnitsPerPlottedInch != 25.4f)
    return EXIT_FAILURE;
  if (PlotScaleLabel(6, 0.0254f) != "1:1") return EXIT_FAILURE;
  if (MtextRichFlattenToPlain("[[b]]public[[/b]] consumer") != "public consumer") return EXIT_FAILURE;
  if (MtextRichColorTag(255, 0, 128) != "[[color:FF0080]]") return EXIT_FAILURE;
  if (CadTableRowCount(2, {"A", "B", "C"}) != 2) return EXIT_FAILURE;
  if (std::string(CadPointTableColumnTitle(CadPointTableColumn::Elevation)) != "Elevation") return EXIT_FAILURE;
  if (StyleNumber({{"width", "bad"}}, "width", 3.5) != 3.5) return EXIT_FAILURE;
  const EntityRef ref{EntityKind::Line, 0};
  if (!ref.valid() || GoSurvey::Document::CheckOpSupport(EntityKind::Surface, GoSurvey::Document::EntityOp::Move).supported)
    return EXIT_FAILURE;
  const auto xf = GoSurvey::Document::SimilarityTransform::Translation({1.0, 2.0, 3.0});
  const ray3d::Vec3 moved = xf.applyPoint({4.0, 5.0, 6.0});
  if (moved.x != 5.0 || moved.y != 7.0 || moved.z != 9.0) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
