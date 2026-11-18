#include "SurveyPoints.hpp"

#include <GoSurvey/Core/NumFormat.hpp>

#include <algorithm>
#include <string>

float SurveyPointCrossHalfWorldFromPaper(float crossSpanPlottedInches, float modelUnitsPerPlottedInch) {
  const float mup = std::max(modelUnitsPerPlottedInch, 1.e-6f);
  const float span = std::max(crossSpanPlottedInches, 1.e-6f);
  return 0.5f * span * mup;
}

void AppendSurveyPointCrossVertices(double easting, double northing, double elevationZ, float halfExtentWorld,
                                    std::vector<float>* outLines, const MarkerBillboardBasis& basis) {
  if (!outLines || halfExtentWorld <= 0.f)
    return;
  const float s = halfExtentWorld;
  // Offsets are in the marker's own plane (u across, v up), mapped into world through the basis.
  // With the default basis this reduces to the previous world-XY arithmetic exactly. The point
  // coordinates stay double through the offset math; only the final GPU-buffer push narrows to
  // float, matching the single narrowing point established in Phase D (ADR-054 (b)).
  auto emit = [&](float u, float v) {
    outLines->push_back(static_cast<float>(easting + static_cast<double>(basis.rightX * u + basis.upX * v)));
    outLines->push_back(static_cast<float>(northing + static_cast<double>(basis.rightY * u + basis.upY * v)));
    outLines->push_back(static_cast<float>(elevationZ + static_cast<double>(basis.rightZ * u + basis.upZ * v)));
  };
  emit(-s, -s);
  emit(s, s);
  emit(-s, s);
  emit(s, -s);
}

void AppendAllSurveyPointMarkers(float crossHalfWorld, const std::vector<SurveyPoint>& pts,
                                 std::vector<float>* outLines, const MarkerBillboardBasis& basis) {
  if (!outLines || pts.empty())
    return;
  // The point's OWN elevation (REQ-057/058). This used to be `0.055f + elevation * 1e-6f` — a
  // draw-ORDER bias from the flat renderer, with a microscopic elevation term to keep coincident
  // markers stably ordered. Depth testing is off, so Z never ordered anything (draw order does);
  // once the view could tilt, that constant simply pinned every marker to the datum while its own
  // snap, hover ring and ID label had moved to the real elevation.
  const float h = std::max(crossHalfWorld, 1.e-8f);
  for (const auto& p : pts)
    AppendSurveyPointCrossVertices(p.easting, p.northing, p.elevation, h, outLines, basis);
}

namespace {

void ReplaceAll(std::string* s, const std::string& from, const std::string& to) {
  if (from.empty())
    return;
  size_t pos = 0;
  while ((pos = s->find(from, pos)) != std::string::npos) {
    s->replace(pos, from.size(), to);
    pos += to.size();
  }
}

} // namespace

std::string FormatSurveyPointLabelPlain(const SurveyPoint& p, SurveyPointLabelStyle style,
                                       const SurveyLabelStyleTemplates& templates, int precision,
                                       double worldOriginX, double worldOriginY) {
  if (style == SurveyPointLabelStyle::None)
    return {};
  const std::string* tpl = &templates.numberDesc;
  switch (style) {
  case SurveyPointLabelStyle::NumberDesc:
    tpl = &templates.numberDesc;
    break;
  case SurveyPointLabelStyle::NumberOnly:
    tpl = &templates.numberOnly;
    break;
  case SurveyPointLabelStyle::DescOnly:
    tpl = &templates.descOnly;
    break;
  case SurveyPointLabelStyle::NumberElev:
    tpl = &templates.numberElev;
    break;
  case SurveyPointLabelStyle::NumberElevDesc:
    tpl = &templates.numberElevDesc;
    break;
  case SurveyPointLabelStyle::NumberNorthEast:
    tpl = &templates.numberNorthEast;
    break;
  case SurveyPointLabelStyle::NorthEast:
    tpl = &templates.northEast;
    break;
  case SurveyPointLabelStyle::NumberNorthEastElev:
    tpl = &templates.numberNorthEastElev;
    break;
  default:
    tpl = &templates.numberDesc;
    break;
  }
  std::string out = *tpl;
  ReplaceAll(&out, "{id}", std::to_string(p.id));
  ReplaceAll(&out, "{desc}", p.description);
  ReplaceAll(&out, "{elev}", FormatLinear(static_cast<double>(p.elevation), precision));
  ReplaceAll(&out, "{north}", FormatLinear(static_cast<double>(p.northing) + worldOriginY, precision));
  ReplaceAll(&out, "{east}", FormatLinear(static_cast<double>(p.easting) + worldOriginX, precision));
  return out;
}
