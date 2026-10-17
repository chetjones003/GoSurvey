#include "TraverseActions.hpp"
#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace {
bool ValidResult(const LsaResult& result) {
  if (!result.ok || result.stationIds.size() != result.adjEasting.size() ||
      result.stationIds.size() != result.adjNorthing.size() ||
      (TraverseAdjustmentHasElevations(result) && result.stationIds.size() != result.adjElevation.size())) return false;
  for (std::size_t i = 0; i < result.stationIds.size(); ++i)
    if (!std::isfinite(result.adjEasting[i]) || !std::isfinite(result.adjNorthing[i]) ||
        (TraverseAdjustmentHasElevations(result) && !std::isfinite(result.adjElevation[i]))) return false;
  return true;
}
}

bool TraverseAdjustmentHasElevations(const LsaResult& result) {
  return result.usedGpsVectors || std::any_of(result.measResiduals.begin(), result.measResiduals.end(), [](const auto& residual) {
    return residual.kind == LsaMeasKind::Vertical || residual.kind == LsaMeasKind::GpsUp ||
        residual.kind == LsaMeasKind::CoordUp || residual.kind == LsaMeasKind::Zenith;
  });
}

bool AcceptTraverseAdjustment(TraverseData& data, const LsaResult& result, std::string& error) {
  if (!ValidResult(result)) { error = "The adjustment has no valid, complete coordinate solution."; return false; }
  for (std::size_t i = 0; i < result.stationIds.size(); ++i) {
    for (auto& leg : data.legs) {
      if (leg.stationId != result.stationIds[i]) continue;
      leg.computedEasting = result.adjEasting[i];
      leg.computedNorthing = result.adjNorthing[i];
      if (TraverseAdjustmentHasElevations(result)) leg.computedElevation = result.adjElevation[i];
    }
  }
  error.clear();
  return true;
}

int CommitTraverseToDrawing(AppCommandState& state, const TraverseData& data,
                            const LsaResult* accepted, std::vector<std::string>& log) {
  if (accepted && !ValidResult(*accepted)) { log.push_back("TRAVERSE: invalid adjustment; no points committed."); return 0; }
  if (state.activeDrawingIdx < FirstDrawingTabIndex() || state.activeDrawingIdx >= static_cast<int>(state.drawingTabs.size())) {
    log.push_back("TRAVERSE: open a drawing before committing points."); return 0;
  }
  const auto projectUid = state.drawingTabs[state.activeDrawingIdx].projectUid;
  for (const auto& project : state.openProjects)
    if (project.uid == projectUid && project.readOnly) { log.push_back("TRAVERSE: the project is read-only; no points committed."); return 0; }
  struct Point { int id; double e, n, z; };
  std::vector<Point> points;
  std::unordered_set<int> ids;
  auto append = [&](int id, double e, double n, double z) {
    if (!std::isfinite(e) || !std::isfinite(n) || !std::isfinite(z)) return false;
    if (accepted) for (std::size_t i = 0; i < accepted->stationIds.size(); ++i) {
      if (accepted->stationIds[i] != id) continue;
      e = accepted->adjEasting[i]; n = accepted->adjNorthing[i];
      if (TraverseAdjustmentHasElevations(*accepted)) z = accepted->adjElevation[i];
      break;
    }
    if (ids.insert(id).second) points.push_back({id, e, n, z});
    return true;
  };
  bool valid = false;
  for (const auto& leg : data.legs) valid |= leg.computed;
  for (const auto& vector : data.gpsVectors) valid |= vector.computed;
  if (accepted) valid |= !accepted->stationIds.empty();
  if (!valid) { log.push_back("TRAVERSE: compute a valid traverse before committing points."); return 0; }
  if (data.networkStations.empty() || !data.legs.empty())
    valid = append(data.startStationId, data.startEasting, data.startNorthing, data.startElevation);
  for (const auto& station : data.networkStations)
    if (station.easting.present && station.northing.present)
      valid &= append(station.id, station.easting.value, station.northing.value,
          station.elevation.present ? station.elevation.value : 0.0);
  for (const auto& leg : data.legs) if (leg.computed)
    valid &= append(leg.stationId, leg.computedEasting, leg.computedNorthing, leg.computedElevation);
  for (const auto& shot : data.sideShots) if (shot.computed)
    valid &= append(shot.stationId, shot.computedEasting, shot.computedNorthing, shot.computedElevation);
  for (const auto& vector : data.gpsVectors) if (vector.computed)
    valid &= append(vector.toStationId, vector.computedToEasting, vector.computedToNorthing, vector.computedToElevation);
  if (accepted) for (std::size_t i = 0; i < accepted->stationIds.size(); ++i)
    valid &= append(accepted->stationIds[i], accepted->adjEasting[i], accepted->adjNorthing[i],
        TraverseAdjustmentHasElevations(*accepted) ? accepted->adjElevation[i] : data.startElevation);
  if (!valid) { log.push_back("TRAVERSE: nonfinite station coordinate; no points committed."); return 0; }
  const auto options = state.createPointsOpts;
  const int nextPointId = state.createPointsNextId;
  PushUndoSnapshot(state, "Traverse commit");
  state.createPointsOpts.sequentialNumbering = false;
  state.createPointsOpts.duplicatePolicy = SurveyDuplicatePolicy::Renumber;
  const auto before = state.surveyPoints.size();
  for (const auto& point : points) {
    state.createPointsOpts.startNumber = point.id;
    state.createPointsNextId = point.id;
    TryPlaceSurveyPoint(state, point.e - state.worldDocumentOriginX,
        point.n - state.worldDocumentOriginY, point.z, log, false);
  }
  state.createPointsOpts = options;
  state.createPointsNextId = nextPointId;
  const int count = static_cast<int>(state.surveyPoints.size() - before);
  log.push_back("TRAVERSE: committed " + std::to_string(count) + " survey points to the drawing.");
  return count;
}
