#pragma once

#include "CadCommands.hpp"

namespace CadSnap {

enum class Kind {
  Endpoint,
  Midpoint,
  Center,
  Perpendicular,
  SurveyCenter,
  GeometricCenter,
  /// Objects that genuinely meet in 3D: their XY paths cross AND their elevations agree there
  /// within REQ-101 (REQ-062).
  Intersection,
  /// Objects that cross **as projected into the current view** but need not meet in space — the
  /// case a plan view cannot distinguish and an orbited one makes obvious. Where the two candidate
  /// points differ, the one nearer the camera is returned (REQ-062).
  ApparentIntersection,
  Grip
};

struct Hit {
  bool valid = false;
  Kind kind = Kind::Endpoint;
  float x = 0.f;
  float y = 0.f;
  /// Elevation of the snapped point (REQ-057/058). Without it the snap glyph is drawn on the
  /// datum while the point it marks sits at its own elevation, so the marker floats away from the
  /// geometry as soon as the view is orbited. Zero for flat drawings, which is every pre-3D one.
  float z = 0.f;
};

struct SnapCandidateEntry {
  Hit hit;
  float distSq = 0.f;
};

/// Converts a pixel radius on the viewport image to world-space XY tolerance (vertical basis).
[[nodiscard]] float WorldToleranceFromPixels(float viewportHeightPx, float orthoHalfHeightWorld,
                                             float pixels);

/// Object snaps from committed geometry — plus, while a POLYLINE draft is open, its own starting
/// vertex as an Endpoint candidate (REQ-118), which is the one candidate not yet in any store.
/// Perpendicular snaps use a command-defined reference
/// (LINE previous point, circle center while sizing radius, prior 3P picks, etc.) so the snap
/// lies at the foot from that reference onto each segment—not under the cursor along the line.
/// \p commandActive retained for callers; perpendicular logic ignores it when no reference applies.
/// Identifies a single entity whose snap candidates should be suppressed in FindBest.
struct SnapExclude {
  bool valid = false;
  SelectedEntity::Type type = SelectedEntity::Type::LineSeg;
  int index = -1;
};

/// \p onlyKind, when non-null (issue #103's Shift+Right-Click "Snap once" override), restricts
/// every candidate to that one kind and ignores the persistent per-type OSNAP toggles entirely —
/// the override's whole purpose is to reach a kind the user does not keep enabled generally.
[[nodiscard]] Hit FindBest(double wx, double wy, const AppCommandState& cmd, bool commandActive,
                           float tolWorld, SnapExclude exclude = {}, const ray3d::Ray* pickRay = nullptr,
                           const Kind* onlyKind = nullptr);

/// Grip-only snap: checks grip points of all selected entities (CAD, MTEXT, survey points).
/// Returns Kind::Grip. No glyph is drawn for this kind. Works regardless of OSNAP toggle.
[[nodiscard]] Hit FindGripSnap(double wx, double wy, const AppCommandState& cmd, float tolWorld);

/// All snap targets of a single \p kind in the drawing (no aperture). Sorted by distance to (\p sortWorldX,\p sortWorldY).
void GatherAllSnapsOfKind(Kind kind, float sortWorldX, float sortWorldY, const AppCommandState& cmd,
                          bool commandActive, std::vector<SnapCandidateEntry>& out);

/// True when perpendicular snap has a command reference (same rules as object snap).
/// \p ignoreToggle skips the persistent objectSnapPerpendicular preference check — used by the
/// Shift+right-click one-shot override menu (issue #103), whose whole purpose is to reach a snap
/// type independent of what is enabled as a running OSNAP.
[[nodiscard]] bool CommandHasPerpendicularSnapReference(const AppCommandState& cmd, bool commandActive,
                                                        bool ignoreToggle = false);

[[nodiscard]] inline int Priority(Kind k) {
  switch (k) {
  case Kind::Endpoint:
    return 3;
  case Kind::SurveyCenter:
    return 2; ///< Same tier as circle center; distance breaks ties
  case Kind::Center:
    return 2; ///< Circle centers beat segment midpoint when snap distances tie
  case Kind::Intersection:
    return 3; ///< As precise a feature as an endpoint — a real crossing of two objects (REQ-062)
  case Kind::GeometricCenter:
    return 1; ///< Closed-shape centroid; same tier as midpoint (distance breaks ties)
  case Kind::Midpoint:
    return 1;
  case Kind::ApparentIntersection:
    return 1; ///< A weaker claim than Intersection: the objects may not touch at all (REQ-062)
  case Kind::Perpendicular:
    return 0;
  case Kind::Grip:
    return 4; ///< Beats all geometry snaps; no glyph is drawn for this kind.
  }
  return 0;
}

} // namespace CadSnap
