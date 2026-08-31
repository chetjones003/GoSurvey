#pragma once

#include "CadCommands.hpp"

#include <vector>
#include <string>

/// Internal storage is local (near origin). World = local + worldDocumentOrigin.
namespace CadCoord {

constexpr double kLargeCoordinateRebaseThreshold = 100000.0;

/// Upper bound on a coordinate the ENTRY-TIME origin establishment will accept as a document frame
/// (REQ-101, decision D-2026-08-17-b). Above this a value is not a coordinate, it is garbage, and
/// accommodating it would be worse than refusing it: establishing an origin at 1e38 makes an absurd
/// value *representable*, so a typo silently produces a drawing in a nonsense frame instead of the
/// refusal REQ-201 wants. It also disarmed the non-finite guards that issue #59 added — the two
/// regression transcripts for #59 caught exactly that and are why this bound exists.
///
/// 1e9 is roughly 100x beyond any real projected system (state plane tops out near 1e7 ft, UTM near
/// 1e7 m), so it cannot reject legitimate survey data. Values between this and the float limit are
/// still handled — by the *load-time* normalization (REQ-079), which is bounded by extents rather
/// than by one typed token, and by the commit-site finiteness guards when they are not.
constexpr double kMaxEstablishableOriginMagnitude = 1.0e9;

/// Upper bound on the magnitude of a coordinate a command's commit path will write into the
/// geometry stores. This is NOT `kMaxEstablishableOriginMagnitude`: a coordinate between that and
/// here (e.g. an easting of 1e12) is a legitimate value that the load-time normalization (REQ-079)
/// still rebases into a precise local frame. What is refused here is a magnitude past which
/// `float` squared-distance math itself overflows — `x*x` exceeds `FLT_MAX` (~3.4e38) once `|x|`
/// exceeds ~1.8e19, so OFFSET's signed-side projection produced inf and wrote `-nan(ind)` into
/// `userLinesFlat` (issue #122, REQ-204 `finite-coords`). 1e18 sits an order of magnitude below
/// that overflow point and ~1e6× above any real or stress-test coordinate, so nothing legitimate
/// is refused; what is caught is reported (REQ-201) rather than propagating as inf/NaN.
constexpr double kMaxStorableCoordinateMagnitude = 1.0e18;

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
