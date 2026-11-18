#pragma once

#include "GoSurvey/Survey/AlignmentModel.hpp"
#include <GoSurvey/Survey/ElevationSampler.hpp>

#include <string>
#include <vector>

/// Ground elevation along an alignment (REQ-497, issue #121 Phase 5). The alignment itself stays horizontal (REQ-480.3):
/// the elevation is read from an existing surface at each station/offset and is session data, never stored on the
/// alignment. A station or offset the surface does not cover is reported as no ground, never as zero.
namespace alignment {

struct GroundPoint {
  double internalStation = 0.0;  ///< Internal station along the geometry (not the displayed, equation-adjusted value).
  double offset = 0.0;           ///< Right positive, as REQ-484.2.
  Point2 point;                  ///< Plan position, drawing-local.
  bool hasGround = false;        ///< False when the station lies off the alignment or the surface has no coverage there.
  double ground = 0.0;           ///< Surface elevation, valid only when hasGround.
};

/// The ground at one station and offset (REQ-497.2). A station outside the alignment is never clamped to its end.
[[nodiscard]] GroundPoint GroundAt(const Alignment& a, const survey::ElevationSampler& sampleElevation,
                                   double internalStation, double offset);
template <typename Surface>
[[nodiscard]] GroundPoint GroundAt(const Alignment& a, const Surface& surface, double internalStation, double offset) {
  const survey::ElevationSampler sample = [&surface](double x, double y, double* z) {
    return surface.elevationAt(x, y, z);
  };
  return GroundAt(a, sample, internalStation, offset);
}

/// Samples the ground every `interval` along the alignment, always including the start, the end and every geometry point
/// that lies on it (REQ-497.3). Returns false with \p err when the alignment is empty, the interval is not a positive
/// number, or the sample count would exceed \p maxSamples. Samples are sorted by station with no duplicates.
[[nodiscard]] bool SampleGround(const Alignment& a, const survey::ElevationSampler& sampleElevation, double interval,
                                int maxSamples,
                                std::vector<GroundPoint>* out, std::string* err);
template <typename Surface>
[[nodiscard]] bool SampleGround(const Alignment& a, const Surface& surface, double interval, int maxSamples,
                                std::vector<GroundPoint>* out, std::string* err) {
  const survey::ElevationSampler sample = [&surface](double x, double y, double* z) {
    return surface.elevationAt(x, y, z);
  };
  return SampleGround(a, sample, interval, maxSamples, out, err);
}

}  // namespace alignment
