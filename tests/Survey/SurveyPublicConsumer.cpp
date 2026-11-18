#include <GoSurvey/Survey/Alignment.hpp>
#include <GoSurvey/Survey/AlignmentGround.hpp>
#include <GoSurvey/Survey/AlignmentReport.hpp>
#include <GoSurvey/Survey/SurveyPoints.hpp>
#include <GoSurvey/Survey/TraverseReportPublic.hpp>
#include <GoSurvey/Survey/TraverseCalcPublic.hpp>
#include <GoSurvey/Survey/AlignmentLabelSetPublic.hpp>
#include <GoSurvey/Survey/CoordinateSystems.hpp>
#include <GoSurvey/Survey/EcefTransformPublic.hpp>
#include <GoSurvey/Survey/SampleLines.hpp>

#include <cmath>
#include <iostream>
#include <vector>

#if __has_include(<commands/CadCommands.hpp>) || __has_include(<cs_map.h>)
#error Survey consumers must not inherit application or CS-MAP private roots
#endif

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
  if (geo::LoadDictionaries({}) || geo::DictionariesLoaded() || geo::DictionaryError().empty()) return 13;
  double x = 0.0, y = 0.0, z = 0.0;
  geo::GeodeticToEcefWgs84(0.0, 0.0, 0.0, &x, &y, &z);
  if (std::fabs(x - 6378137.0) > 1e-6 || std::fabs(y) > 1e-6 || std::fabs(z) > 1e-6) return 14;
  SurveyPoint point;
  point.id = 42;
  point.labelStyle = SurveyPointLabelStyle::NumberOnly;
  if (FormatSurveyPointLabelPlain(point, point.labelStyle, {}, 2) != "42") return 15;
  std::vector<float> marker;
  AppendAllSurveyPointMarkers(0.1f, {point}, &marker);
  if (marker.size() != 12) return 16;
  alignment::SampleLine section;
  section.origin = {0.0, 0.0};
  section.direction = {1.0, 0.0};
  section.leftWidth = 0.25;
  section.rightWidth = 0.25;
  const survey::ElevationSampler flat = [](double, double, double* out) { *out = 0.0; return true; };
  alignment::SampleSection({-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 0.0, 1.0, 0.0}, {0, 1, 2}, flat, section);
  if (section.section.empty()) return 17;
  return 0;
}
