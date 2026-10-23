#pragma once
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include <string>
#include <vector>

struct AppCommandState;
bool TraverseAdjustmentHasElevations(const LsaResult& result);

// Shared by native frontends. Never accepts a failed, incomplete or nonfinite result.
bool AcceptTraverseAdjustment(TraverseData& data, const LsaResult& result, std::string& error);

// One undo step, one point per station ID, WORLD -> drawing-local before placement.
// The optional accepted result also supplies GPS-only/network station coordinates.
int CommitTraverseToDrawing(AppCommandState& state, const TraverseData& data,
                            const LsaResult* accepted, std::vector<std::string>& log);
