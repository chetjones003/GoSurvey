#pragma once

/// Pipe run solid generation (GitHub issue #486 increment B1 / REQ-345), extended with automatic
/// bend filleting (increment B2 follow-up, user-requested 2026-09-17). Header-only so Catch2 can
/// cover the NPS lookup and the swept solids without GL, the same reason cadblock.hpp is.
///
/// A `CadPipeRun` (CadEntities.hpp) stores only its path and a nominal-size LABEL — never a
/// solid. This file is the one place that label becomes a physical radius and the path becomes a
/// swept pipe, so nothing else in the codebase invents a second NPS table, a second fillet-radius
/// formula, or a second sweep.

#include "CadEntities.hpp"
#include "brep.hpp"
#include "cadsolid.hpp"
#include "ray3d.hpp"
#include "ucs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

/// One entry of the standard NPS (nominal pipe size) → outer-diameter table. Outer diameter alone
/// is what a swept pipe solid needs — wall thickness (which pressure class actually governs) does
/// not change the modeled OD, so the table is keyed on size only, not size+class.
struct CadPipeNpsEntry {
  double nps;    ///< nominal size in inches, e.g. 4.0 for "4in"
  double odIn;   ///< outer diameter in inches
};

/// Standard-wall NPS → OD table for the sizes this codebase's fittings library targets (issue
/// #486). Deliberately small and exact-match only: a size the table does not carry is a SPEC GAP
/// for the catalog (increment B4), not something to interpolate or guess.
inline constexpr CadPipeNpsEntry kCadPipeNpsTable[] = {
    {0.5, 0.840},  {0.75, 1.050}, {1.0, 1.315},  {1.25, 1.660}, {1.5, 1.900},
    {2.0, 2.375},  {2.5, 2.875}, {3.0, 3.500},  {4.0, 4.500},  {6.0, 6.625},
    {8.0, 8.625},  {10.0, 10.750}, {12.0, 12.750},
};

/// Parses an NPS label like `"4in"` or `"1.5in"` into inches. Returns false for anything that
/// doesn't parse as `<number>in` (case-insensitive, optional whitespace before "in") — a label
/// like a raw fraction ("1/2in") is out of scope until the catalog work needs it (SPEC GAP, not
/// guessed here).
[[nodiscard]] inline bool CadParsePipeNominalSizeInches(std::string_view label, double* outInches) {
  if (!outInches)
    return false;
  size_t end = label.size();
  while (end > 0 && (label[end - 1] == ' ' || label[end - 1] == '\t'))
    --end;
  if (end < 3)
    return false;
  const char c0 = label[end - 2];
  const char c1 = label[end - 1];
  const bool hasIn = (c0 == 'i' || c0 == 'I') && (c1 == 'n' || c1 == 'N');
  if (!hasIn)
    return false;
  std::string numPart(label.substr(0, end - 2));
  while (!numPart.empty() && (numPart.back() == ' ' || numPart.back() == '\t'))
    numPart.pop_back();
  if (numPart.empty())
    return false;
  char* parseEnd = nullptr;
  const double v = std::strtod(numPart.c_str(), &parseEnd);
  if (parseEnd == numPart.c_str() || parseEnd != numPart.c_str() + numPart.size())
    return false;
  if (!(v > 0.0))
    return false;
  *outInches = v;
  return true;
}

/// Looks up a pipe run's `nominalSize` label in \ref kCadPipeNpsTable and returns its outer
/// diameter in FEET (drawing units, D-2026-09-12 decision 4 — NPS labels display in inches, run
/// geometry is in feet). Returns false (and leaves `*odFeet` untouched) for an unparsable label or
/// a size the table does not carry.
[[nodiscard]] inline bool CadPipeNominalOdFeet(std::string_view nominalSize, double* odFeet) {
  if (!odFeet)
    return false;
  double nps = 0.0;
  if (!CadParsePipeNominalSizeInches(nominalSize, &nps))
    return false;
  for (const CadPipeNpsEntry& e : kCadPipeNpsTable) {
    if (std::fabs(e.nps - nps) < 1e-9) {
      *odFeet = (e.odIn / 12.0);
      return true;
    }
  }
  return false;
}

namespace cadpiperun_detail {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegToRad = kPi / 180.0;

/// Below this, a routed bend is treated as collinear (no fitting needed) rather than filleted to
/// the smallest standard angle — a click a fraction of a degree off straight should not sprout a
/// visible bend.
inline constexpr double kMinFilletTurnRad = 1.0 * kDegToRad;

/// Long-radius elbow takeoff (industry-standard formula, user-specified 2026-09-17): centreline
/// bend radius = 1.5 x nominal pipe size.
inline constexpr double kFilletTakeoffFactor = 1.5;

} // namespace cadpiperun_detail

/// Standard fitting bend angles a routed corner auto-fillets to (product decision, user-specified
/// 2026-09-17): the nearest of these, by absolute degree difference, is used for every bend
/// regardless of the corner's own exact clicked angle — the same "round to the nearest available
/// catalog part" a real pipefitter does.
inline constexpr double kCadPipeFilletStandardAnglesDeg[] = {90.0, 60.0, 45.0, 30.0, 22.5, 11.25};

/// Long-radius elbow takeoff radius in FEET (drawing units) for nominal size \p npsInches inches —
/// 1.5x the nominal size, the standard long-radius fitting formula.
[[nodiscard]] inline double CadPipeFilletRadiusFeet(double npsInches) {
  return (cadpiperun_detail::kFilletTakeoffFactor * npsInches) / 12.0;
}

/// Snaps a raw bend angle (radians) to the nearest entry of \ref kCadPipeFilletStandardAnglesDeg,
/// returned in radians.
[[nodiscard]] inline double CadPipeSnapFilletAngleRad(double rawAngleRad) {
  const double rawDeg = rawAngleRad / cadpiperun_detail::kDegToRad;
  double best = kCadPipeFilletStandardAnglesDeg[0];
  double bestDiff = std::fabs(rawDeg - best);
  for (double cand : kCadPipeFilletStandardAnglesDeg) {
    const double diff = std::fabs(rawDeg - cand);
    if (diff < bestDiff) {
      bestDiff = diff;
      best = cand;
    }
  }
  return best * cadpiperun_detail::kDegToRad;
}

/// A round pipe cross-section profile of \p radiusFt, centred at \p atPoint with its plane
/// perpendicular to \p tangentDir — the same "two opposite vertices, two half-turn arc edges" shape
/// `ExtrudeProfileFromSelection`'s own circle branch builds (CadCommands.cpp), which is what a
/// `brep::Sweep`/`brep::Extrude` profile requires (REQ-314/315). The profile's own world position
/// is a placement convenience only — `brep::Sweep` re-derives every ring from the path, never the
/// profile's original location (see `SweepFrameAt`), so any point on the path would do equally
/// well; the path start is simplest to reason about.
[[nodiscard]] inline bool CadBuildPipeProfile(double radiusFt, const ray3d::Vec3& atPoint,
                                              const ray3d::Vec3& tangentDir, brep::Profile* out) {
  if (!out || !(radiusFt > 0.0))
    return false;
  ucs::Ucs plane;
  if (!ucs::FromNormal(atPoint, tangentDir, &plane))
    return false;
  out->plane = plane;
  out->vertices = {ucs::UcsToWorld(plane, ray3d::Vec3{radiusFt, 0.0, 0.0}),
                   ucs::UcsToWorld(plane, ray3d::Vec3{-radiusFt, 0.0, 0.0})};
  brep::ProfileEdge e;
  e.arc = true;
  e.centre = atPoint;
  e.sweep = cadpiperun_detail::kPi;
  out->edges = {e, e};
  return true;
}

/// Builds ONE swept pipe solid for \p run's entire path, with an automatic fillet at every
/// direction change greater than \ref cadpiperun_detail::kMinFilletTurnRad: the corner's bend
/// radius is the long-radius elbow takeoff (\ref CadPipeFilletRadiusFeet, 1.5x nominal size), its
/// SWEEP ANGLE is snapped to the nearest standard fitting (\ref CadPipeSnapFilletAngleRad) — not
/// the corner's own exact clicked angle — because a round-pipe sweep cannot mitre an untreated
/// sharp corner at all (`brep::Sweep`'s mitre path is polygonal-profile only) and because that is
/// literally what was asked for: route to the nearest AVAILABLE catalog fitting, the way a real
/// pipefitter does, rather than to an arbitrary angle no elbow is stocked in.
///
/// **A snapped angle cannot also hit the exact clicked vertex.** Each corner turns from the
/// direction the pipe is ACTUALLY travelling (which already carries any upstream snapping error)
/// toward the ORIGINAL next-vertex direction, by the SNAPPED amount — so a route with several
/// close-together bends can drift visibly from the clicked polyline, and the run's own endpoint may
/// land near, not exactly on, the last clicked point. This is the honest, stated consequence of
/// snapping to a small angle set, not a bug: the alternative (hitting every vertex exactly) needs
/// an off-angle, non-catalog fitting at every bend, which defeats the point of snapping at all.
///
/// A corner whose fillet cannot fit — the tangent setback needed would eat more than roughly half
/// of an adjoining leg — refuses the WHOLE run rather than silently drawing an overlapping or
/// truncated bend (REQ-201): route with more spacing around a tight corner, or accept the nearest
/// smaller standard angle by routing a second, shallower bend instead.
///
/// Split out of \ref CadBuildPipeRunSweptSolid (rather than duplicated) so a connection-port query
/// (\ref CadPipeRunEndPorts) reads EXACTLY the same geometry that gets rendered — no second path
/// computation to ever drift out of sync with the first.
[[nodiscard]] inline bool CadBuildPipeRunSweepPath(const CadPipeRun& run, brep::SweepPath* outPath,
                                                    double* outPipeRadius, ray3d::Vec3* outStartTangent,
                                                    ray3d::Vec3* outEndTangent) {
  if (!outPath || !outPipeRadius || !outStartTangent || !outEndTangent)
    return false;
  double npsIn = 0.0;
  if (!CadParsePipeNominalSizeInches(run.nominalSize, &npsIn))
    return false;
  double odFeet = 0.0;
  if (!CadPipeNominalOdFeet(run.nominalSize, &odFeet))
    return false;
  const double pipeRadius = odFeet * 0.5;
  if (!(pipeRadius > 0.0))
    return false;
  const double bendRadiusNominal = CadPipeFilletRadiusFeet(npsIn);

  const size_t nVerts = run.vertsXyz.size() / 3;
  if (nVerts < 2)
    return false;

  std::vector<ray3d::Vec3> v(nVerts);
  for (size_t i = 0; i < nVerts; ++i)
    v[i] = ray3d::Vec3{run.vertsXyz[i * 3 + 0], run.vertsXyz[i * 3 + 1], run.vertsXyz[i * 3 + 2]};

  std::vector<double> legLen(nVerts - 1);
  std::vector<ray3d::Vec3> legDir(nVerts - 1);
  for (size_t i = 0; i + 1 < nVerts; ++i) {
    const ray3d::Vec3 d = ray3d::Sub(v[i + 1], v[i]);
    legLen[i] = ray3d::Length(d);
    if (!(legLen[i] > 1e-9))
      return false;  // coincident vertices — no segment to sweep
    legDir[i] = ray3d::Scale(d, 1.0 / legLen[i]);
  }

  std::vector<ray3d::Vec3> pathPoints;
  std::vector<brep::SweepSegment> pathSegments;
  ray3d::Vec3 curPoint = v[0];
  ray3d::Vec3 curDir = legDir[0];
  pathPoints.push_back(curPoint);

  for (size_t i = 1; i + 1 < nVerts; ++i) {
    const ray3d::Vec3 dOutTarget = legDir[i];
    const double cosA = std::clamp(ray3d::Dot(curDir, dOutTarget), -1.0, 1.0);
    const double turnRad = std::acos(cosA);
    if (turnRad < cadpiperun_detail::kMinFilletTurnRad)
      continue;  // collinear enough to pass straight through — no fitting needed here

    const ray3d::Vec3 rawAxis = ray3d::Cross(curDir, dOutTarget);
    const double axisLen = ray3d::Length(rawAxis);
    if (axisLen < 1e-9)
      return false;  // a reversal has no well-defined bend plane; refuse rather than guess one
    const ray3d::Vec3 planeNormal = ray3d::Scale(rawAxis, 1.0 / axisLen);

    const double snappedRad = CadPipeSnapFilletAngleRad(turnRad);
    const double tanHalf = std::tan(snappedRad * 0.5);
    if (!(tanHalf > 1e-9))
      return false;
    const double idealTangent = bendRadiusNominal * tanHalf;
    // The available budget on each side, read from the ORIGINAL vertex spacing (an approximation —
    // see the function's own doc comment on drift): half of each adjoining leg, so two fillets
    // sharing one leg can never together claim more than the whole of it.
    const double availIn = ray3d::Length(ray3d::Sub(v[i], curPoint));
    const double availOut = legLen[i];
    const double maxTangent = 0.49 * std::min(availIn, availOut);
    const double tangentLen = std::min(idealTangent, maxTangent);
    if (!(tangentLen > 1e-6))
      return false;  // too tight to fillet at all — refuse the whole run rather than fake a corner
    const double effectiveRadius = tangentLen / tanHalf;

    const double straightLen = availIn - tangentLen;
    if (straightLen > 1e-9) {
      curPoint = ray3d::Add(curPoint, ray3d::Scale(curDir, straightLen));
      pathPoints.push_back(curPoint);
      pathSegments.push_back(brep::SweepSegment{});  // arc=false: straight
    }

    const ray3d::Vec3 inward = ray3d::Normalize(ray3d::Cross(planeNormal, curDir));
    const ray3d::Vec3 centre = ray3d::Add(curPoint, ray3d::Scale(inward, effectiveRadius));
    const ray3d::Vec3 r0 = ray3d::Sub(curPoint, centre);
    const ray3d::Vec3 r1 = ray3d::RotateVectorAboutAxis(r0, planeNormal, snappedRad);
    const ray3d::Vec3 arcEnd = ray3d::Add(centre, r1);
    const ray3d::Vec3 exitDir = ray3d::RotateVectorAboutAxis(curDir, planeNormal, snappedRad);

    brep::SweepSegment arcSeg;
    arcSeg.arc = true;
    arcSeg.centre = centre;
    arcSeg.normal = planeNormal;
    arcSeg.sweep = snappedRad;
    pathPoints.push_back(arcEnd);
    pathSegments.push_back(arcSeg);

    curPoint = arcEnd;
    curDir = exitDir;
  }

  // Final straight run to the last vertex, along the CURRENT (possibly bend-rotated) direction —
  // the endpoint is wherever that direction actually leads, not necessarily bit-identical to
  // v.back(), for the reason the function's own doc comment states.
  const double finalLen = ray3d::Length(ray3d::Sub(v[nVerts - 1], curPoint));
  if (finalLen > 1e-9) {
    curPoint = ray3d::Add(curPoint, ray3d::Scale(curDir, finalLen));
    pathPoints.push_back(curPoint);
    pathSegments.push_back(brep::SweepSegment{});
  }

  if (pathPoints.size() < 2)
    return false;

  *outPipeRadius = pipeRadius;
  *outStartTangent = legDir[0];
  *outEndTangent = curDir;
  outPath->points = std::move(pathPoints);
  outPath->segments = std::move(pathSegments);
  return true;
}

/// Builds \p run's swept pipe solid, auto-filleted at every real bend over
/// \ref cadpiperun_detail::kMinFilletTurnRad — see \ref CadBuildPipeRunSweepPath for the marching
/// algorithm. A run with fewer than 2 vertices, an unresolvable nominal size, or a corner that
/// cannot be filleted contributes nothing (REQ-201: nothing invalid is ever stored).
///
/// **A snapped angle cannot also hit the exact clicked vertex.** Each corner turns from the
/// direction the pipe is ACTUALLY travelling (which already carries any upstream snapping error)
/// toward the ORIGINAL next-vertex direction, by the SNAPPED amount — so a route with several
/// close-together bends can drift visibly from the clicked polyline, and the run's own endpoint may
/// land near, not exactly on, the last clicked point. This is the honest, stated consequence of
/// snapping to a small angle set, not a bug: the alternative (hitting every vertex exactly) needs
/// an off-angle, non-catalog fitting at every bend, which defeats the point of snapping at all.
///
/// A corner whose fillet cannot fit — the tangent setback needed would eat more than roughly half
/// of an adjoining leg — refuses the WHOLE run rather than silently drawing an overlapping or
/// truncated bend (REQ-201): route with more spacing around a tight corner, or accept the nearest
/// smaller standard angle by routing a second, shallower bend instead.
[[nodiscard]] inline bool CadBuildPipeRunSweptSolid(const CadPipeRun& run, brep::Solid* out) {
  if (!out)
    return false;
  brep::SweepPath path;
  double pipeRadius = 0.0;
  ray3d::Vec3 startTangent{};
  ray3d::Vec3 endTangent{};
  if (!CadBuildPipeRunSweepPath(run, &path, &pipeRadius, &startTangent, &endTangent))
    return false;

  brep::Profile profile;
  if (!CadBuildPipeProfile(pipeRadius, path.points[0], startTangent, &profile))
    return false;

  brep::Problem why = brep::Problem::Ok;
  return brep::Sweep(profile, path, brep::SweepOptions{}, out, &why);
}

/// A pipe run's own "pipe end" connection point (user-specified 2026-09-17): the centre of the
/// pipe's actual end FACE — not necessarily the raw clicked vertex, since fillet-angle snapping can
/// move it slightly (see \ref CadBuildPipeRunSweepPath) — with an outward unit normal pointing AWAY
/// from the pipe, matching the sense `CadBlockConnection::nx/ny/nz` and `FindNearestPipeEndpoint`'s
/// own bare-line endpoints already use (CadBlocks.cpp), so a fitting orienting against one behaves
/// identically to orienting against the other.
struct CadPipeRunEndPort {
  ray3d::Vec3 point;
  ray3d::Vec3 outwardNormal;
};

/// Computes \p run's two end ports. Returns false for the same reasons the swept solid itself would
/// refuse to build — reading the SAME path computation (\ref CadBuildPipeRunSweepPath), so a port
/// this reports always matches where the rendered pipe actually ends, never a second, independently
/// (and potentially inconsistently) derived answer.
[[nodiscard]] inline bool CadPipeRunEndPorts(const CadPipeRun& run, CadPipeRunEndPort* start,
                                             CadPipeRunEndPort* end) {
  if (!start || !end)
    return false;
  brep::SweepPath path;
  double pipeRadius = 0.0;
  ray3d::Vec3 startTangent{};
  ray3d::Vec3 endTangent{};
  if (!CadBuildPipeRunSweepPath(run, &path, &pipeRadius, &startTangent, &endTangent))
    return false;
  if (path.points.size() < 2)
    return false;
  start->point = path.points.front();
  start->outwardNormal = ray3d::Normalize(ray3d::Scale(startTangent, -1.0));
  end->point = path.points.back();
  end->outwardNormal = ray3d::Normalize(endTangent);
  return true;
}

/// Builds \p run's swept pipe solid (auto-filleted at every real bend, \ref
/// CadBuildPipeRunSweptSolid) and appends it to \p out (does not clear it first; always at most one
/// element on success). A run with fewer than 2 vertices, an unresolvable nominal size, or a corner
/// that cannot be filleted (see \ref CadBuildPipeRunSweptSolid) contributes nothing — the caller
/// sees an empty append rather than a crash or an invalid/self-intersecting solid, consistent with
/// REQ-201 (nothing invalid is ever stored). Returns true iff a solid was appended. Kept as a
/// vector-appending function (rather than returning one `CadSolidPtr` directly) because every
/// caller — `RebuildPipeRunWorldSolids`, the rubber preview, the PIPERUN command — already expects
/// this shape from increment B1, and a run building to more than one solid remains a live
/// possibility for a future increment (e.g. an unfillable corner that still builds the two straight
/// legs either side of it, kept open for now rather than pre-committed to).
[[nodiscard]] inline bool CadBuildPipeRunSolids(const CadPipeRun& run, std::vector<CadSolidPtr>* out) {
  if (!out)
    return false;
  brep::Solid solid;
  if (!CadBuildPipeRunSweptSolid(run, &solid))
    return false;
  out->push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  return true;
}
