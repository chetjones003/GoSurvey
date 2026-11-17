#pragma once

// Public Survey bridge for the sample-line model. The canonical declarations remain at their
// legacy path until the history-preserving move; this namespace gives standalone consumers a
// stable Survey-facing API without declaring the same functions twice.
#include "../../../../alignment/AlignmentSampleLines.hpp"

namespace survey::sample_lines {

using Point = alignment::SampleLine;
using Settings = alignment::SampleLineSettings;
using SectionPoint = alignment::SectionPoint;
using Scale = alignment::SectionScale;

[[nodiscard]] inline std::string SettingsRefusal(const alignment::Alignment& alignment, const Settings& settings) {
  return alignment::SampleLineSettingsRefusal(alignment, settings);
}

[[nodiscard]] inline bool Compute(const alignment::Alignment& alignment, const Settings& settings,
                                  const alignment::StationFormat& format, std::vector<Point>* out, std::string* why) {
  return alignment::ComputeSampleLines(alignment, settings, format, out, why);
}

inline void SampleSection(const std::vector<double>& vertices, const std::vector<std::uint32_t>& indices,
                          const survey::ElevationSampler& sampleElevation, Point& line) {
  alignment::SampleSection(vertices, indices, sampleElevation, line);
}

inline void SampleSection(const std::vector<double>& vertices, const std::vector<std::uint32_t>& indices,
                          const geometry::TriangleGridIndex& index,
                          const survey::ElevationSampler& sampleElevation, Point& line) {
  alignment::SampleSection(vertices, indices, index, sampleElevation, line);
}

}  // namespace survey::sample_lines
