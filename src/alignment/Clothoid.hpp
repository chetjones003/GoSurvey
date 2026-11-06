#pragma once

#include <cmath>

#include "alignment/Alignment.hpp"

namespace alignment {

/// True when a stored spiral radius means infinite (tangent), i.e. zero curvature.
[[nodiscard]] inline bool IsInfiniteSpiralRadius(double r) { return !(r > 0.0) || !std::isfinite(r); }

/// Signed curvature at the start/end of a spiral entity (1/units; positive = right).
[[nodiscard]] double SpiralKappaStart(const Entity& e);
[[nodiscard]] double SpiralKappaEnd(const Entity& e);

/// Clothoid segment with linear κ(s): κ(s) = κ0 + (κ1-κ0)·s/L. Integrates from entity start along `s`
/// (0..length) in the entity-local frame (heading 0 at start), then rotates into survey (sin/cos azimuth).
[[nodiscard]] Point2 SpiralDisplacementLocal(const Entity& e, double s);

/// Heading change from start to `s` along a spiral (radians, signed: positive = clockwise/right when e.right).
[[nodiscard]] double SpiralHeadingDelta(const Entity& e, double s);

/// Signed curvature at distance `s` from the spiral start.
[[nodiscard]] double SpiralKappaAt(const Entity& e, double s);

/// Signed curvature at entity start/end (any entity kind).
[[nodiscard]] double EntityStartCurvature(const Entity& e);
[[nodiscard]] double EntityEndCurvature(const Entity& e);

}  // namespace alignment
