#pragma once

#include "CadCommands.hpp"

namespace CadSnap {

enum class Kind { Endpoint, Midpoint, Center, Perpendicular, SurveyCenter, GeometricCenter, Grip };

struct Hit {
  bool valid = false;
  Kind kind = Kind::Endpoint;
  float x = 0.f;
  float y = 0.f;
};

struct SnapCandidateEntry {
  Hit hit;
  float distSq = 0.f;
};

/// Converts a pixel radius on the viewport image to world-space XY tolerance (vertical basis).
[[nodiscard]] float WorldToleranceFromPixels(float viewportHeightPx, float orthoHalfHeightWorld,
                                             float pixels);

/// Object snaps from committed geometry. Perpendicular snaps use a command-defined reference
/// (LINE previous point, circle center while sizing radius, prior 3P picks, etc.) so the snap
/// lies at the foot from that reference onto each segment—not under the cursor along the line.
/// \p commandActive retained for callers; perpendicular logic ignores it when no reference applies.
/// Identifies a single entity whose snap candidates should be suppressed in FindBest.
struct SnapExclude {
  bool valid = false;
  SelectedEntity::Type type = SelectedEntity::Type::LineSeg;
  int index = -1;
};

[[nodiscard]] Hit FindBest(double wx, double wy, const AppCommandState& cmd, bool commandActive,
                           float tolWorld, SnapExclude exclude = {});

/// Grip-only snap: checks grip points of all selected entities (CAD, MTEXT, survey points).
/// Returns Kind::Grip. No glyph is drawn for this kind. Works regardless of OSNAP toggle.
[[nodiscard]] Hit FindGripSnap(double wx, double wy, const AppCommandState& cmd, float tolWorld);

/// All snap targets of a single \p kind in the drawing (no aperture). Sorted by distance to (\p sortWorldX,\p sortWorldY).
void GatherAllSnapsOfKind(Kind kind, float sortWorldX, float sortWorldY, const AppCommandState& cmd,
                          bool commandActive, std::vector<SnapCandidateEntry>& out);

/// True when perpendicular snap has a command reference (same rules as object snap).
[[nodiscard]] bool CommandHasPerpendicularSnapReference(const AppCommandState& cmd, bool commandActive);

[[nodiscard]] inline int Priority(Kind k) {
  switch (k) {
  case Kind::Endpoint:
    return 3;
  case Kind::SurveyCenter:
    return 2; ///< Same tier as circle center; distance breaks ties
  case Kind::Center:
    return 2; ///< Circle centers beat segment midpoint when snap distances tie
  case Kind::GeometricCenter:
    return 1; ///< Closed-shape centroid; same tier as midpoint (distance breaks ties)
  case Kind::Midpoint:
    return 1;
  case Kind::Perpendicular:
    return 0;
  case Kind::Grip:
    return 4; ///< Beats all geometry snaps; no glyph is drawn for this kind.
  }
  return 0;
}

} // namespace CadSnap
