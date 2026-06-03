#pragma once

#include "CadCommands.hpp"

#include <vector>
#include <string>

/// Internal storage is local (near origin). World = local + worldDocumentOrigin.
namespace CadCoord {

constexpr double kLargeCoordinateRebaseThreshold = 100000.0;

inline double WorldOriginX(const AppCommandState& st) { return st.worldDocumentOriginX; }
inline double WorldOriginY(const AppCommandState& st) { return st.worldDocumentOriginY; }

void LocalFromWorld(const AppCommandState& st, double wx, double wy, float* lx, float* ly);
void WorldFromLocal(const AppCommandState& st, float lx, float ly, double* wx, double* wy);

inline float WorldXFromLocal(const AppCommandState& st, float lx) {
  return static_cast<float>(static_cast<double>(lx) + st.worldDocumentOriginX);
}

inline float WorldYFromLocal(const AppCommandState& st, float ly) {
  return static_cast<float>(static_cast<double>(ly) + st.worldDocumentOriginY);
}

void ShiftAllStorageBy(AppCommandState& st, double dx, double dy);

/// Set worldDocumentOrigin to \p newOriginX/Y and adjust stored geometry so world positions are unchanged.
void ApplyDocumentOriginRebase(AppCommandState& st, double newOriginX, double newOriginY, std::vector<std::string>* log);

/// Shift drawing centroid to local space (after DXF import or when coordinates are large).
bool RebaseDrawingToLocalOrigin(AppCommandState& st, std::vector<std::string>* log);

/// Frame all committed geometry in the viewport (local pan/zoom on \p st).
bool FitViewportToDrawing(AppCommandState& st, float viewportAspect, int fbW, int fbH);

/// On load: rebase files that still store large world coordinates with origin 0.
bool MaybeRebaseLargeCoordinates(AppCommandState& st, std::vector<std::string>* log);

bool ComputeWorldSpaceExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                              double* outMxY);

} // namespace CadCoord
