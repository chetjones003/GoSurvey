#pragma once

#include "CadCommands.hpp"

namespace CadSnap {

enum class Kind { Endpoint, Midpoint, Center, Perpendicular };

struct Hit {
  bool valid = false;
  Kind kind = Kind::Endpoint;
  float x = 0.f;
  float y = 0.f;
};

/// Converts a pixel radius on the viewport image to world-space XY tolerance (vertical basis).
[[nodiscard]] float WorldToleranceFromPixels(float viewportHeightPx, float orthoHalfHeightWorld,
                                             float pixels);

/// Object snaps from committed geometry. Perpendicular snaps use a command-defined reference
/// (LINE previous point, circle center while sizing radius, prior 3P picks, etc.) so the snap
/// lies at the foot from that reference onto each segment—not under the cursor along the line.
/// \p commandActive retained for callers; perpendicular logic ignores it when no reference applies.
[[nodiscard]] Hit FindBest(float wx, float wy, const AppCommandState& cmd, bool commandActive,
                           float tolWorld);

[[nodiscard]] inline int Priority(Kind k) {
  switch (k) {
  case Kind::Endpoint:
    return 3;
  case Kind::Center:
    return 2; ///< Circle centers beat segment midpoint when snap distances tie
  case Kind::Midpoint:
    return 1;
  case Kind::Perpendicular:
    return 0;
  }
  return 0;
}

} // namespace CadSnap
