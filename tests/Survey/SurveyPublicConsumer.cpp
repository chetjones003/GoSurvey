#include <GoSurvey/Survey/Alignment.hpp>
#include <GoSurvey/Survey/AlignmentGround.hpp>
#include <GoSurvey/Survey/AlignmentReport.hpp>
#include <GoSurvey/Survey/TraverseReportPublic.hpp>
#include <GoSurvey/Survey/TraverseCalcPublic.hpp>
#include <GoSurvey/Survey/AlignmentLabelSetPublic.hpp>

#include <cmath>
#include <iostream>
#include <vector>

int main() {
  alignment::Alignment route;
  route.startingStation = 100.0;
  if (!route.AppendTangent({0.0, 0.0}, 0.0, 25.0)) return 1;

  const survey::ElevationSampler ground = [](double, double y, double* z) {
    if (y < 0.0 || y > 25.0) return false;
    *z = 40.0 + y;
    return true;
  };
  const auto at = alignment::GroundAt(route, ground, 112.5, 0.0);
  if (!at.hasGround || std::fabs(at.ground - 52.5) > 1e-9) return 2;
  if (alignment::GroundAt(route, ground, 126.0, 0.0).hasGround) return 3;

  std::vector<alignment::GroundPoint> profile;
  std::string error;
  if (!alignment::SampleGround(route, ground, 10.0, 10, &profile, &error)) return 4;
  if (profile.size() != 4 || profile.back().internalStation != 125.0) return 5;

  alignment::AlignmentReportFormat format;
  std::vector<std::string> lines;
  alignment::BuildAlignmentReportLines(route, format, &lines);
  if (lines.empty()) return 6;
  const auto csv = alignment::BuildAlignmentReportCsv(route, format);
  if (csv.empty()) return 7;
  TraverseData traverse;
  LsaWeights weights;
  LsaResult result;
  const auto adjustment = traverserpt::BuildReportText(traverse, weights, result, "Consumer", "ft");
  if (adjustment.find("Summary of Files Used") == std::string::npos) return 8;
  const auto standardStyle = AlignmentStyles::StandardAlignmentStyle();
  if (standardStyle.name != "Standard" || standardStyle.arrows.visible) return 9;
  AlignmentLabelSet labels;
  if (!labels.geometryPointsOn) return 10;
  alignment::Alignment spiral;
  if (!spiral.AppendTangent({0.0, 0.0}, 0.0, 1.0)) return 11;
  if (!spiral.AppendSpiral(10.0, 0.0, 100.0, true)) return 12;
  return 0;
}
