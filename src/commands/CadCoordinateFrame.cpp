#include "CadCoordinateFrame.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CadCoord {

void LocalFromWorld(const AppCommandState& st, double wx, double wy, float* lx, float* ly) {
  if (!lx || !ly)
    return;
  *lx = static_cast<float>(wx - st.worldDocumentOriginX);
  *ly = static_cast<float>(wy - st.worldDocumentOriginY);
}

void WorldFromLocal(const AppCommandState& st, float lx, float ly, double* wx, double* wy) {
  if (!wx || !wy)
    return;
  *wx = static_cast<double>(lx) + st.worldDocumentOriginX;
  *wy = static_cast<double>(ly) + st.worldDocumentOriginY;
}

void ShiftAllStorageBy(AppCommandState& st, double dx, double dy) {
  auto add2 = [&](float* x, float* y) {
    *x = static_cast<float>(static_cast<double>(*x) + dx);
    *y = static_cast<float>(static_cast<double>(*y) + dy);
  };

  for (size_t i = 0; i + 5 < st.userLinesFlat.size(); i += 6) {
    add2(&st.userLinesFlat[i], &st.userLinesFlat[i + 1]);
    add2(&st.userLinesFlat[i + 3], &st.userLinesFlat[i + 4]);
  }
  // Stride 4 (cx,cy,z,r): only the centre's X and Y rebase. Z is absolute (ADR-025 D2) and the
  // radius is a length, not a position — neither may take a document-origin offset.
  for (size_t i = 0; i + 3 < st.userCirclesCxCyZR.size(); i += 4)
    add2(&st.userCirclesCxCyZR[i], &st.userCirclesCxCyZR[i + 1]);
  for (CadArc& a : st.userArcs)
    add2(&a.cx, &a.cy);
  for (CadEllipse& el : st.userEllipses)
    add2(&el.cx, &el.cy);
  for (size_t i = 0; i + 2 < st.userPolylineVerts.size(); i += 3)
    add2(&st.userPolylineVerts[i], &st.userPolylineVerts[i + 1]);
  for (CadAnnotation& an : st.cadAnnotations) {
    add2(&an.insX, &an.insY);
    add2(&an.boxMinX, &an.boxMinY);
    add2(&an.boxMaxX, &an.boxMaxY);
    if (an.kind == CadAnnotation::Kind::DimAligned || an.kind == CadAnnotation::Kind::DimLinear) {
      add2(&an.dimExt1X, &an.dimExt1Y);
      add2(&an.dimExt2X, &an.dimExt2Y);
    }
  }
  // Stride 3, but only X and Y are rebased — Z is absolute and never carries a document origin
  // (ADR-025 D2). The +2 slot is deliberately skipped.
  for (CadFilledRegion& fr : st.cadFilledRegions)
    for (size_t i = 0; i + 2 < fr.vertsXyz.size(); i += 3)
      add2(&fr.vertsXyz[i], &fr.vertsXyz[i + 1]);
  for (SurveyPoint& p : st.surveyPoints)
    add2(&p.easting, &p.northing);
}

void ApplyDocumentOriginRebase(AppCommandState& st, double newOriginX, double newOriginY,
                               std::vector<std::string>* log) {
  const double dx = st.worldDocumentOriginX - newOriginX;
  const double dy = st.worldDocumentOriginY - newOriginY;
  if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9)
    return;
  ShiftAllStorageBy(st, dx, dy);
  st.viewportPanX += dx;
  st.viewportPanY += dy;
  st.worldDocumentOriginX = newOriginX;
  st.worldDocumentOriginY = newOriginY;
  // The origin moved, so {north}/{east} label text is now stale — rebuild it against the new origin.
  RegenerateAllSurveyPointLabels(st);
  if (log) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "Drawing origin shifted for precision (world 0,0 at local offset %.6g, %.6g). Coordinates "
                  "shown in UCS World are unchanged.",
                  newOriginX, newOriginY);
    log->push_back(buf);
  }
  BumpCadGpuCache(st);
}

bool ComputeWorldSpaceExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                              double* outMxY) {
  double smnX = 0.;
  double smxX = 0.;
  double smnY = 0.;
  double smxY = 0.;
  if (!ComputeWorldExtents(st, &smnX, &smxX, &smnY, &smxY))
    return false;
  *outMnX = smnX + st.worldDocumentOriginX;
  *outMxX = smxX + st.worldDocumentOriginX;
  *outMnY = smnY + st.worldDocumentOriginY;
  *outMxY = smxY + st.worldDocumentOriginY;
  return true;
}

bool RebaseDrawingToLocalOrigin(AppCommandState& st, std::vector<std::string>* log) {
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  int skipped = 0;
  if (!ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped))
    return false;
  // Add current world origin to convert local-extent output to world for the rebase pivot.
  const double cx = 0.5 * (mnX + mxX) + st.worldDocumentOriginX;
  const double cy = 0.5 * (mnY + mxY) + st.worldDocumentOriginY;
  ApplyDocumentOriginRebase(st, cx, cy, log);
  return true;
}

bool FitViewportToDrawing(AppCommandState& st, float viewportAspect, int fbW, int fbH) {
  if (fbW <= 0 || fbH <= 0)
    return false;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  int skipped = 0;
  if (!ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped))
    return false;
  // REQ-122: a rect that is not finite frames nothing and leaves the camera alone, so a bad import
  // cannot hand the viewport a NaN centre.
  if (!zoomframing::FrameWorldRect(mnX, mxX, mnY, mxY, viewportAspect, &st.viewportPanX, &st.viewportPanY,
                                   &st.viewportZoom))
    return false;
  BumpCadGpuCache(st);
  return true;
}

bool MaybeRebaseLargeCoordinates(AppCommandState& st, std::vector<std::string>* log) {
  if (st.worldDocumentOriginX != 0.0 || st.worldDocumentOriginY != 0.0)
    return false;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (!ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    return false;
  // `mxY`, not `mnY` twice. The fourth slot read `mnY` again, so the maximum-Y extent was never
  // considered and a drawing whose only large coordinate was a large POSITIVE Y was silently never
  // rebased — the precision repair simply did not fire, with no symptom to notice. Demonstrated
  // rather than deduced: a line from (0,0) to (0,1e+12) was not rebased while the identical case in
  // X was. Found while diagnosing issue #61.
  const double mag = std::max({std::fabs(mnX), std::fabs(mxX), std::fabs(mnY), std::fabs(mxY)});
  if (mag < kLargeCoordinateRebaseThreshold)
    return false;
  return RebaseDrawingToLocalOrigin(st, log);
}

} // namespace CadCoord
