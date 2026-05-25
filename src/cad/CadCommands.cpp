#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadLinetype.hpp"
#include "geom2d.hpp"
#include "MtextRichFormat.hpp"
#include "StringUtil.hpp"

#include "CadSnap.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <numeric>

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

namespace {

constexpr float kPiAngF = 3.14159265358979323846f;

static float CadAngNormalizeMinusPiToPi(float a) {
  while (a > kPiAngF)
    a -= 2.f * kPiAngF;
  while (a < -kPiAngF)
    a += 2.f * kPiAngF;
  return a;
}

static bool CadDimAngularComputeFrame(const CadAnnotation& a, float* a1Out, float* a2Out, float* sweepOut, float* bisx,
                                      float* bisy, float* thetaInterior) {
  if (a.kind != CadAnnotation::Kind::DimAngular)
    return false;
  const float vx = a.dimAngVertexX, vy = a.dimAngVertexY;
  const float p1x = a.dimExt1X, p1y = a.dimExt1Y, p2x = a.dimExt2X, p2y = a.dimExt2Y;
  const float u1x = p1x - vx, u1y = p1y - vy;
  const float u2x = p2x - vx, u2y = p2y - vy;
  const float l1 = std::hypot(u1x, u1y);
  const float l2 = std::hypot(u2x, u2y);
  if (l1 < 1.e-8f || l2 < 1.e-8f)
    return false;
  const float n1x = u1x / l1, n1y = u1y / l1;
  const float n2x = u2x / l2, n2y = u2y / l2;
  const float dot = n1x * n2x + n1y * n2y;
  const float a1 = std::atan2(n1y, n1x);
  const float a2 = std::atan2(n2y, n2x);
  const float sweep = CadAngNormalizeMinusPiToPi(a2 - a1);
  const float theta = std::acos(std::clamp(dot, -1.f, 1.f));
  float bx = n1x + n2x;
  float by = n1y + n2y;
  const float bl = std::hypot(bx, by);
  if (bl > 1.e-6f) {
    bx /= bl;
    by /= bl;
  } else {
    bx = -n1y;
    by = n1x;
  }
  const float mid = a1 + 0.5f * sweep;
  const float mdx = std::cos(mid);
  const float mdy = std::sin(mid);
  if (bx * mdx + by * mdy < 0.f) {
    bx = -bx;
    by = -by;
  }
  *a1Out = a1;
  *a2Out = a2;
  *sweepOut = sweep;
  *bisx = bx;
  *bisy = by;
  *thetaInterior = theta;
  return theta > 1.e-7f;
}

static float CadDimAngularPickRadius(float vx, float vy, float bisx, float bisy, float pickx, float picky, float rMin,
                                     float rMax) {
  const float wx = pickx - vx;
  const float wy = picky - vy;
  float t = wx * bisx + wy * bisy;
  if (t <= 1.e-8f) {
    t = wx * -bisx + wy * -bisy;
    if (t <= 1.e-8f)
      t = rMin;
  }
  return std::clamp(t, rMin, rMax);
}

} // namespace

float CadOffsetEntityPickTolWorld(const AppCommandState& st);

float RotateDeltaFromReferenceAndNewSegment(float refX1, float refY1, float refX2, float refY2,
                                             float newX1, float newY1, float newX2, float newY2) {
  const float thetaRef = std::atan2(refY2 - refY1, refX2 - refX1);
  const float thetaNew = std::atan2(newY2 - newY1, newX2 - newX1);
  return thetaNew - thetaRef;
}

bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                         float* r) {
  const double dax = static_cast<double>(ax);
  const double day = static_cast<double>(ay);
  const double dbx = static_cast<double>(bx);
  const double dby = static_cast<double>(by);
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double d = 2.0 * (dax * (dby - dcy) + dbx * (dcy - day) + dcx * (day - dby));
  if (std::fabs(d) < 1e-6)
    return false;
  const double a2 = dax * dax + day * day;
  const double b2 = dbx * dbx + dby * dby;
  const double c2 = dcx * dcx + dcy * dcy;
  const double ux = (a2 * (dby - dcy) + b2 * (dcy - day) + c2 * (day - dby)) / d;
  const double uy = (a2 * (dcx - dbx) + b2 * (dax - dcx) + c2 * (dbx - dax)) / d;
  const double dx = ux - dax;
  const double dy = uy - day;
  *ox = static_cast<float>(ux);
  *oy = static_cast<float>(uy);
  *r = static_cast<float>(std::sqrt(dx * dx + dy * dy));
  return true;
}

bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                           float* ty, float* nx, float* ny, float* measLen) {
  if (a.kind != CadAnnotation::Kind::DimAligned)
    return false;
  const float x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1.e-8f)
    return false;
  vx /= len;
  vy /= len;
  const float n0x = -vy;
  const float n0y = vx;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float dmx = cmx + n0x * a.dimSignedOffset;
  const float dmy = cmy + n0y * a.dimSignedOffset;
  // Feet on the dimension line (parallel to chord through dmx,dmy): perpendicular from each extension point.
  const float t1 = (x1 - dmx) * vx + (y1 - dmy) * vy;
  const float t2 = (x2 - dmx) * vx + (y2 - dmy) * vy;
  *sx1 = dmx + vx * t1;
  *sy1 = dmy + vy * t1;
  *sx2 = dmx + vx * t2;
  *sy2 = dmy + vy * t2;
  *tx = vx;
  *ty = vy;
  *nx = n0x;
  *ny = n0y;
  *measLen = len;
  return true;
}

bool CadDimLinearGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                          float* ty, float* nx, float* ny, float* measLen) {
  if (a.kind != CadAnnotation::Kind::DimLinear)
    return false;
  const float x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  if (!a.dimLinearVertical) {
    const float span = std::fabs(x2 - x1);
    if (span < 1.e-8f)
      return false;
    const float dmy = cmy + a.dimSignedOffset;
    *sx1 = x1;
    *sy1 = dmy;
    *sx2 = x2;
    *sy2 = dmy;
    *tx = (x2 >= x1) ? 1.f : -1.f;
    *ty = 0.f;
    *nx = 0.f;
    *ny = 1.f;
    *measLen = span;
  } else {
    const float span = std::fabs(y2 - y1);
    if (span < 1.e-8f)
      return false;
    const float dmx = cmx + a.dimSignedOffset;
    *sx1 = dmx;
    *sy1 = y1;
    *sx2 = dmx;
    *sy2 = y2;
    *tx = 0.f;
    *ty = (y2 >= y1) ? 1.f : -1.f;
    *nx = 1.f;
    *ny = 0.f;
    *measLen = span;
  }
  return true;
}

bool CadDimAnyGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx, float* ty,
                       float* nx, float* ny, float* measLen) {
  if (a.kind == CadAnnotation::Kind::DimAligned)
    return CadDimAlignedGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  if (a.kind == CadAnnotation::Kind::DimLinear)
    return CadDimLinearGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  return false;
}

/// Place measurement text on the far side of the dimension line from the measured chord (CAD "above" the dim line).
static void CadDimAlignedPlaceTextBeyondDimLine(float chordMidX, float chordMidY, float dimMidX, float dimMidY,
                                                float n0x, float n0y, float hWorld, float* outIx, float* outIy) {
  const float dOff = (dimMidX - chordMidX) * n0x + (dimMidY - chordMidY) * n0y;
  float s = 1.f;
  if (dOff > 1.e-8f)
    s = 1.f;
  else if (dOff < -1.e-8f)
    s = -1.f;
  // Slightly more than half the annotation height so the label clears the dim line but still reads "just above" it.
  const float lift = 1.08f * hWorld;
  *outIx = dimMidX + n0x * (s * lift);
  *outIy = dimMidY + n0y * (s * lift);
}

void CadDimAlignedApplyInsFromLocalOffset(CadAnnotation* ann, float alongN, float alongT) {
  if (!ann)
    return;
  if (ann->kind == CadAnnotation::Kind::DimAngular) {
    const float vx = ann->dimAngVertexX, vy = ann->dimAngVertexY;
    float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
    if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
      return;
    const float R = std::max(ann->dimSignedOffset, 1.e-6f);
    const float mid = a1 + 0.5f * sweep;
    const float mx = vx + std::cos(mid) * R;
    const float my = vy + std::sin(mid) * R;
    const float tx = -std::sin(mid);
    const float ty = std::cos(mid);
    ann->insX = mx + bisx * alongN + tx * alongT;
    ann->insY = my + bisy * alongN + ty * alongT;
    ann->rotationRad = std::atan2(bisy, bisx);
    return;
  }
  if (ann->kind != CadAnnotation::Kind::DimAligned && ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (!CadDimAnyGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    return;
  const float dmx = 0.5f * (sx1 + sx2);
  const float dmy = 0.5f * (sy1 + sy2);
  ann->insX = dmx + nx * alongN + tx * alongT;
  ann->insY = dmy + ny * alongN + ty * alongT;
}

bool CadDimAlignedBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  if (!out || st.active != AppCommandState::Kind::DimAligned ||
      st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return false;
  const float x1 = st.dimE1x, y1 = st.dimE1y, x2 = st.dimE2x, y2 = st.dimE2y;
  const float lx = cursorWx, ly = cursorWy;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1.e-8f)
    return false;
  vx /= len;
  vy /= len;
  const float t1 = (x1 - lx) * vx + (y1 - ly) * vy;
  const float t2 = (x2 - lx) * vx + (y2 - ly) * vy;
  const float sx1 = lx + vx * t1;
  const float sy1 = ly + vy * t1;
  const float sx2 = lx + vx * t2;
  const float sy2 = ly + vy * t2;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float n0x = -vy;
  const float n0y = vx;
  const float dmx = 0.5f * (sx1 + sx2);
  const float dmy = 0.5f * (sy1 + sy2);
  const float dOff = (dmx - cmx) * n0x + (dmy - cmy) * n0y;
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAligned;
  d.dimExt1X = x1;
  d.dimExt1Y = y1;
  d.dimExt2X = x2;
  d.dimExt2Y = y2;
  d.dimSignedOffset = dOff;
  d.plottedHeightInches = std::max(st.defaultPlottedTextHeightInches * 0.85f, 1.e-6f);
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(len));
  d.text = buf;
  d.rotationRad = std::atan2(vy, vx);
  const float hWorld = CadAnnotationHeightWorld(d, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &d.insX, &d.insY);
  *out = std::move(d);
  return true;
}

void CadDimLinearUpdateDraftOrientation(AppCommandState& st, float cursorWx, float cursorWy) {
  if (st.active != AppCommandState::Kind::DimLinear || st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return;
  const float cmx = 0.5f * (st.dimE1x + st.dimE2x);
  const float cmy = 0.5f * (st.dimE1y + st.dimE2y);
  const float chord = std::hypot(st.dimE2x - st.dimE1x, st.dimE2y - st.dimE1y);
  const float unlockTol = 1.e-3f * std::max(1.f, chord);

  const float dxSpan = std::fabs(st.dimE2x - st.dimE1x);
  const float dySpan = std::fabs(st.dimE2y - st.dimE1y);
  const float spanTol =
      std::max(1e-8f, 1e-12f * std::max(std::max(std::fabs(st.dimE1x), std::fabs(st.dimE1y)),
                                        std::max(std::fabs(st.dimE2x), std::fabs(st.dimE2y))));
  const bool mustVertical = dxSpan <= spanTol && dySpan > spanTol;
  const bool mustHorizontal = dySpan <= spanTol && dxSpan > spanTol;
  if (mustVertical || mustHorizontal) {
    st.dimLinearDraftVertical = mustVertical;
    st.dimLinearOrientUserLock = false;
    return;
  }

  if (st.dimLinearOrientUserLock) {
    if (std::hypot(cursorWx - st.dimLinearLockCursorWx, cursorWy - st.dimLinearLockCursorWy) > unlockTol)
      st.dimLinearOrientUserLock = false;
  }
  if (!st.dimLinearOrientUserLock) {
    const float adx = std::fabs(cursorWx - cmx);
    const float ady = std::fabs(cursorWy - cmy);
    st.dimLinearDraftVertical = adx > ady;
  }
}

void CadDimLinearApplyHVHotkey(AppCommandState& st, bool vertical, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::DimLinear || st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return;
  st.dimLinearDraftVertical = vertical;
  st.dimLinearOrientUserLock = true;
  st.dimLinearLockCursorWx = st.uiCursorWorldX;
  st.dimLinearLockCursorWy = st.uiCursorWorldY;
  log.push_back(vertical ? "DIMLINEAR — vertical span (V). Move crosshair to unlock orientation."
                         : "DIMLINEAR — horizontal span (H). Move crosshair to unlock orientation.");
  BumpCadGpuCache(st);
}

bool CadDimLinearBuildDraft(AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  if (!out || st.active != AppCommandState::Kind::DimLinear ||
      st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return false;
  CadDimLinearUpdateDraftOrientation(st, cursorWx, cursorWy);
  const float x1 = st.dimE1x, y1 = st.dimE1y, x2 = st.dimE2x, y2 = st.dimE2y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const bool vert = st.dimLinearDraftVertical;
  const float meas = vert ? std::fabs(y2 - y1) : std::fabs(x2 - x1);
  if (meas < 1.e-8f)
    return false;
  float dmx = cmx;
  float dmy = cmy;
  float n0x = 0.f;
  float n0y = 1.f;
  float dOff = 0.f;
  if (!vert) {
    dmy = cursorWy;
    dOff = dmy - cmy;
  } else {
    dmx = cursorWx;
    dOff = dmx - cmx;
    n0x = 1.f;
    n0y = 0.f;
  }
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimLinear;
  d.dimExt1X = x1;
  d.dimExt1Y = y1;
  d.dimExt2X = x2;
  d.dimExt2Y = y2;
  d.dimSignedOffset = dOff;
  d.dimLinearVertical = vert;
  d.plottedHeightInches = std::max(st.defaultPlottedTextHeightInches * 0.85f, 1.e-6f);
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(meas));
  d.text = buf;
  float tx = 0.f, ty = 0.f;
  if (!vert) {
    tx = (x2 >= x1) ? 1.f : -1.f;
    ty = 0.f;
  } else {
    tx = 0.f;
    ty = (y2 >= y1) ? 1.f : -1.f;
  }
  d.rotationRad = std::atan2(ty, tx);
  const float hWorld = CadAnnotationHeightWorld(d, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &d.insX, &d.insY);
  *out = std::move(d);
  return true;
}

std::string CadFormatAngleDegMinSecFromRad(float angleRad) {
  double deg = static_cast<double>(angleRad) * (180.0 / 3.14159265358979323846);
  if (deg < 0.0)
    deg = -deg;
  if (deg > 180.0)
    deg = 360.0 - deg;
  int id = static_cast<int>(std::floor(deg + 1.e-9));
  double minf = (deg - static_cast<double>(id)) * 60.0;
  if (minf < 0.0)
    minf = 0.0;
  int im = static_cast<int>(std::floor(minf + 1.e-9));
  double sec = (minf - static_cast<double>(im)) * 60.0;
  if (sec < 0.0)
    sec = 0.0;
  if (im >= 60) {
    im = 0;
    id = std::min(id + 1, 359);
  }
  if (sec >= 59.95) {
    sec = 0.0;
    ++im;
    if (im >= 60) {
      im = 0;
      ++id;
    }
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d\xc2\xb0%d'%.1f\"", id, im, static_cast<double>(sec));
  return std::string(buf);
}

std::string CadFormatBearingCwNorthDegMinSec(float bearingDegClockwiseFromNorth) {
  double deg = std::fmod(static_cast<double>(bearingDegClockwiseFromNorth), 360.0);
  if (deg < 0.0)
    deg += 360.0;
  int id = static_cast<int>(std::floor(deg + 1.e-9));
  double minf = (deg - static_cast<double>(id)) * 60.0;
  if (minf < 0.0)
    minf = 0.0;
  int im = static_cast<int>(std::floor(minf + 1.e-9));
  double sec = (minf - static_cast<double>(im)) * 60.0;
  if (sec < 0.0)
    sec = 0.0;
  if (im >= 60) {
    im = 0;
    id = (id + 1) % 360;
  }
  if (sec >= 59.95) {
    sec = 0.0;
    ++im;
    if (im >= 60) {
      im = 0;
      id = (id + 1) % 360;
    }
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d\xc2\xb0%d'%.1f\"", id, im, static_cast<double>(sec));
  return std::string(buf);
}

void CadDimRefreshMeasurementText(CadAnnotation* ann) {
  if (!ann)
    return;
  if (ann->kind == CadAnnotation::Kind::DimAligned || ann->kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (!CadDimAnyGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      return;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(ml));
    ann->text = buf;
  } else if (ann->kind == CadAnnotation::Kind::DimAngular) {
    float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
    if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
      return;
    ann->text = CadFormatAngleDegMinSecFromRad(theta);
  }
}

void CadDimAngularSyncTextPlacement(CadAnnotation* ann, float mupi) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimAngular)
    return;
  const float vx = ann->dimAngVertexX, vy = ann->dimAngVertexY;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
    return;
  const float R = std::max(ann->dimSignedOffset, 1.e-6f);
  const float mid = a1 + 0.5f * sweep;
  const float mx = vx + std::cos(mid) * R;
  const float my = vy + std::sin(mid) * R;
  const float hWorld = CadAnnotationHeightWorld(*ann, mupi);
  const float lift = 1.12f * hWorld;
  ann->insX = mx + bisx * lift;
  ann->insY = my + bisy * lift;
  ann->rotationRad = std::atan2(bisy, bisx);
}

bool CadDimAngularBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  if (!out || st.active != AppCommandState::Kind::DimAngular ||
      st.dimAngularPhase != AppCommandState::DimAngularPhase::WaitArc)
    return false;
  const float vx = st.dimAngVx, vy = st.dimAngVy;
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAngular;
  d.dimAngVertexX = vx;
  d.dimAngVertexY = vy;
  d.dimExt1X = st.dimE1x;
  d.dimExt1Y = st.dimE1y;
  d.dimExt2X = st.dimE2x;
  d.dimExt2Y = st.dimE2y;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(d, &a1, &a2, &sweep, &bisx, &bisy, &theta))
    return false;
  const float leg = std::min(std::hypot(d.dimExt1X - vx, d.dimExt1Y - vy), std::hypot(d.dimExt2X - vx, d.dimExt2Y - vy));
  const float rMax = std::max(1.e-4f, 0.92f * leg);
  const float rMin = std::max(1.e-4f, 0.02f * leg);
  const float R = CadDimAngularPickRadius(vx, vy, bisx, bisy, cursorWx, cursorWy, rMin, rMax);
  d.dimSignedOffset = R;
  d.plottedHeightInches = std::max(st.defaultPlottedTextHeightInches * 0.85f, 1.e-6f);
  d.text = CadFormatAngleDegMinSecFromRad(theta);
  CadDimAngularSyncTextPlacement(&d, st.modelUnitsPerPlottedInch);
  *out = std::move(d);
  return true;
}

void CadAnnotationRoughBounds(const CadAnnotation& a, float modelUnitsPerPlottedInch, float* outMnX, float* outMnY,
                              float* outMxX, float* outMxY) {
  const float h = CadAnnotationHeightWorld(a, modelUnitsPerPlottedInch);
  if (a.kind == CadAnnotation::Kind::Mtext) {
    *outMnX = std::min(a.boxMinX, a.boxMaxX);
    *outMxX = std::max(a.boxMinX, a.boxMaxX);
    *outMnY = std::min(a.boxMinY, a.boxMaxY);
    *outMxY = std::max(a.boxMinY, a.boxMaxY);
    return;
  }
  if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
    if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas)) {
      *outMnX = *outMxX = a.insX;
      *outMnY = *outMxY = a.insY;
      return;
    }
    auto expandSeg = [&](float ax, float ay, float bx, float by) {
      *outMnX = std::min(*outMnX, std::min(ax, bx));
      *outMxX = std::max(*outMxX, std::max(ax, bx));
      *outMnY = std::min(*outMnY, std::min(ay, by));
      *outMxY = std::max(*outMxY, std::max(ay, by));
    };
    *outMnX = *outMxX = sx1;
    *outMnY = *outMxY = sy1;
    expandSeg(sx1, sy1, sx2, sy2);
    const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
    const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
    const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
    const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
    const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
    const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
    const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
    const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
    const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
    const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
    expandSeg(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
    expandSeg(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
    const float charFactor = 0.55f;
    const float tw = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
    const float c = std::cos(a.rotationRad);
    const float s = std::sin(a.rotationRad);
    auto corner = [&](float lx, float ly, float* ox, float* oy) {
      const float rx = lx * c - ly * s;
      const float ry = lx * s + ly * c;
      *ox = a.insX + rx;
      *oy = a.insY + ry;
    };
    float xs[4]{};
    float ys[4]{};
    corner(0.f, 0.f, &xs[0], &ys[0]);
    corner(tw, 0.f, &xs[1], &ys[1]);
    corner(tw, -h, &xs[2], &ys[2]);
    corner(0.f, -h, &xs[3], &ys[3]);
    for (int i = 0; i < 4; ++i)
      expandSeg(xs[i], ys[i], xs[i], ys[i]);
    return;
  }
  const float charFactor = 0.55f;
  const float w = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
  const float c = std::cos(a.rotationRad);
  const float s = std::sin(a.rotationRad);
  auto corner = [&](float lx, float ly, float* ox, float* oy) {
    const float rx = lx * c - ly * s;
    const float ry = lx * s + ly * c;
    *ox = a.insX + rx;
    *oy = a.insY + ry;
  };
  float xs[4]{};
  float ys[4]{};
  corner(0.f, 0.f, &xs[0], &ys[0]);
  corner(w, 0.f, &xs[1], &ys[1]);
  corner(w, -h, &xs[2], &ys[2]);
  corner(0.f, -h, &xs[3], &ys[3]);
  *outMnX = *outMxX = xs[0];
  *outMnY = *outMxY = ys[0];
  for (int i = 1; i < 4; ++i) {
    *outMnX = std::min(*outMnX, xs[i]);
    *outMxX = std::max(*outMxX, xs[i]);
    *outMnY = std::min(*outMnY, ys[i]);
    *outMxY = std::max(*outMxY, ys[i]);
  }
}

int PickCadAnnotationAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                        float viewportHeightPx) {
  const float tol =
      CadSnap::WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, cmd.objectSnapAperturePx);
  const float tol2 = tol * tol;
  auto distSqSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    if (len2 < 1.e-18f) {
      const float dx = px - ax;
      const float dy = py - ay;
      return dx * dx + dy * dy;
    }
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
    const float qx = ax + t * vx;
    const float qy = ay + t * vy;
    const float dx = px - qx;
    const float dy = py - qy;
    return dx * dx + dy * dy;
  };
  for (int i = static_cast<int>(cmd.cadAnnotations.size()) - 1; i >= 0; --i) {
    const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(i)];
    if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      float best = tol2 + 1.f;
      auto upd = [&](float ax, float ay, float bx, float by) {
        best = std::min(best, distSqSeg(wx, wy, ax, ay, bx, by));
      };
      const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
      const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
      const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
      const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
      const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
      const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
      const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
      const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
      const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
      const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
      upd(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
      upd(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
      upd(sx1, sy1, sx2, sy2);
      const float h = CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      const float charFactor = 0.55f;
      const float tw = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
      const float c = std::cos(a.rotationRad);
      const float s = std::sin(a.rotationRad);
      auto corner = [&](float lx, float ly, float* ox, float* oy) {
        const float rx = lx * c - ly * s;
        const float ry = lx * s + ly * c;
        *ox = a.insX + rx;
        *oy = a.insY + ry;
      };
      float xs[4]{};
      float ys[4]{};
      corner(0.f, 0.f, &xs[0], &ys[0]);
      corner(tw, 0.f, &xs[1], &ys[1]);
      corner(tw, -h, &xs[2], &ys[2]);
      corner(0.f, -h, &xs[3], &ys[3]);
      for (int e = 0; e < 4; ++e) {
        const int e2 = (e + 1) % 4;
        upd(xs[e], ys[e], xs[e2], ys[e2]);
      }
      if (best <= tol2)
        return i;
      continue;
    }
    float mnX = 0.f;
    float mnY = 0.f;
    float mxX = 0.f;
    float mxY = 0.f;
    CadAnnotationRoughBounds(a, cmd.modelUnitsPerPlottedInch, &mnX, &mnY, &mxX, &mxY);
    if (wx >= mnX - tol && wx <= mxX + tol && wy >= mnY - tol && wy <= mxY + tol)
      return i;
  }
  return -1;
}

static void ResetModifyRotateDraft(AppCommandState& st) {
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.modifyBaseX = st.modifyBaseY = 0.f;
  st.scaleRefDist = 1.f;
  st.scalePhase = AppCommandState::ScalePhase::FactorPick;
  st.scaleRefP1X = st.scaleRefP1Y = 0.f;
  st.scaleNewLenP1X = st.scaleNewLenP1Y = 0.f;
  st.rotatePhase = AppCommandState::RotatePhase::PickSelection;
  st.rotateBaseX = st.rotateBaseY = 0.f;
  st.rotateRefX1 = st.rotateRefY1 = st.rotateRefX2 = st.rotateRefY2 = 0.f;
  st.rotateAnglePt1X = st.rotateAnglePt1Y = 0.f;
  st.rotateCopyMode = false;
}

static void ClearPendingViewportZoom(AppCommandState& st) {
  st.pendingZoomExtents = false;
  st.pendingZoomWindow = false;
}

namespace CadCmdGeom {

[[nodiscard]] float DistSqPointSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-18f) {
    const float dx = px - ax;
    const float dy = py - ay;
    return dx * dx + dy * dy;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  const float dx = px - qx;
  const float dy = py - qy;
  return dx * dx + dy * dy;
}

/// Minimum squared distance between finite segments AB and CD (dense sampling handles skew / parallel robustly).
[[nodiscard]] float MinDistSqSegSeg(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  float best = DistSqPointSegment(ax, ay, cx, cy, dx, dy);
  best = std::min(best, DistSqPointSegment(bx, by, cx, cy, dx, dy));
  best = std::min(best, DistSqPointSegment(cx, cy, ax, ay, bx, by));
  best = std::min(best, DistSqPointSegment(dx, dy, ax, ay, bx, by));
  constexpr int N = 28;
  for (int i = 1; i < N; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(N);
    const float px = ax + t * (bx - ax);
    const float py = ay + t * (by - ay);
    best = std::min(best, DistSqPointSegment(px, py, cx, cy, dx, dy));
    const float qx = cx + t * (dx - cx);
    const float qy = cy + t * (dy - cy);
    best = std::min(best, DistSqPointSegment(qx, qy, ax, ay, bx, by));
  }
  return best;
}

} // namespace CadCmdGeom

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log);

namespace {

static EntityAttributes MakeNewEntityAttrs(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.currentLayer.empty() ? std::string("0") : st.currentLayer;
  a.color = "ByLayer";
  a.linetype = "ByLayer";
  a.lineweightMm = -1.f;
  a.transparency = -1.f;
  return a;
}

bool ParseTwoFloats(std::string s, float* x, float* y) {
  for (char& c : s) {
    if (c == ',')
      c = ' ';
  }
  std::istringstream iss(s);
  if (!(iss >> *x))
    return false;
  if (!(iss >> *y))
    return false;
  return true;
}

bool ParseOneFloat(const std::string& s, float* v) {
  std::istringstream iss(StringUtil::trimCopy(s));
  return static_cast<bool>(iss >> *v);
}

struct CmdEntry {
  const char* primary;
  const char* aliases;
};

const CmdEntry kRegistry[] = {
    {"line", "l"},
    {"circle", "c"},
    {"polyline", "pl"},
    {"arc", ""},
    {"ellipse", "el"},
    {"text", ""},
    {"mtext", "mt"},
    {"dimaligned", "dal"},
    {"dimlinear", "dli"},
    {"dimangular", "dan"},
    {"id", ""},
    {"inverse", "inv"},
    {"plotscale", "pscale"},
    {"move", "m"},
    {"copy", "cp"},
    {"rotate", "ro"},
    {"scale", "sc"},
    {"delete", "del"},
    {"join", "j"},
    {"trim", "tr"},
    {"offset", "o"},
    {"zoomextents", "ze"},
    {"zoomwindow", "zw"},
    {"createpoints", "crtpts"},
    {"viewpoints", "vwpts"},
    {"importpoints", "imppts"},
    {"exportpoints", "exppts"},
    {"select", ""},
    {"help", ""},
    {"regen", "re"},
    {"layer", "la"},
};

bool DispatchByPrimary(const std::string& primary, AppCommandState& st, std::vector<std::string>& log);

int FuzzySubsequenceScore(std::string_view query, std::string_view cand) {
  if (query.empty())
    return 0;
  size_t qi = 0;
  int score = 0;
  bool prevMatch = false;
  for (size_t i = 0; i < cand.size() && qi < query.size(); ++i) {
    char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(query[qi])));
    char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cand[i])));
    if (qc == cc) {
      score += 10 + (prevMatch ? 5 : 0);
      prevMatch = true;
      ++qi;
    } else
      prevMatch = false;
  }
  if (qi != query.size())
    return -1;
  score += static_cast<int>(50 - cand.size());
  return score;
}

bool TryStrongFuzzyDispatch(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  if (line.empty())
    return false;
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok)
    tokens.push_back(StringUtil::toLowerAsciiCopy(tok));
  if (tokens.empty())
    return false;

  std::unordered_map<std::string, int> bestPerPrimary;
  for (const std::string& t : tokens) {
    for (const CmdEntry& e : kRegistry) {
      const std::string prim = StringUtil::toLowerAsciiCopy(std::string(e.primary));
      auto considerCand = [&](const std::string& candLower) {
        const int sc = FuzzySubsequenceScore(t, candLower);
        if (sc < 0)
          return;
        auto it = bestPerPrimary.find(prim);
        if (it == bestPerPrimary.end() || sc > it->second)
          bestPerPrimary[prim] = sc;
      };
      considerCand(prim);
      if (e.aliases[0] == '\0')
        continue;
      std::istringstream als(std::string(e.aliases));
      std::string a;
      while (std::getline(als, a, ',')) {
        a = StringUtil::trimCopy(a);
        if (a.empty())
          continue;
        considerCand(StringUtil::toLowerAsciiCopy(a));
      }
    }
  }

  std::vector<std::pair<int, std::string>> ranked;
  ranked.reserve(bestPerPrimary.size());
  for (const auto& kv : bestPerPrimary)
    ranked.push_back({kv.second, kv.first});
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first)
      return a.first > b.first;
    return a.second < b.second;
  });
  if (ranked.empty())
    return false;

  const int bestSc = ranked[0].first;
  const int secondSc = ranked.size() > 1 ? ranked[1].first : -1;

  size_t maxTokLen = 0;
  for (const auto& t : tokens)
    maxTokLen = std::max(maxTokLen, t.size());

  const bool shortQuery = (tokens.size() == 1 && maxTokLen <= 2);
  const int minScore = shortQuery ? 72 : 45;
  const int margin = shortQuery ? 48 : 22;
  if (bestSc < minScore)
    return false;
  if (secondSc >= 0 && bestSc - secondSc < margin)
    return false;

  DispatchByPrimary(ranked[0].second, st, log);
  log.push_back("Matched \"" + ranked[0].second + "\" from fuzzy command match.");
  return true;
}

void ResetCircleDraft(AppCommandState& st) {
  st.circleStyle = AppCommandState::CircleStyle::CenterRadius;
  st.circlePhase = AppCommandState::CirclePhase::WaitCenterOrMode;
  st.circleCx = st.circleCy = 0.f;
  st.c3p1x = st.c3p1y = st.c3p2x = st.c3p2y = 0.f;
}

void ResetPolylineDraft(AppCommandState& st) {
  st.polylinePhase = AppCommandState::PolylinePhase::NeedFirstPoint;
  st.polyFirstX = st.polyFirstY = 0.f;
  st.polyDraftSegments = 0;
  st.polylineDraftVerts.clear();
}

void ResetArcDraft(AppCommandState& st) {
  st.arcPhase = AppCommandState::ArcPhase::WaitStart;
  st.arcAx = st.arcAy = st.arcBx = st.arcBy = 0.f;
}

void ResetEllipseDraft(AppCommandState& st) {
  st.ellPhase = AppCommandState::EllipsePhase::WaitCenter;
  st.ellCx = st.ellCy = 0.f;
  st.ellMajEx = st.ellMajEy = 0.f;
}

void ResetTextCmdDraft(AppCommandState& st) {
  st.textPhase = AppCommandState::TextCmdPhase::WaitInsertion;
  st.textInsX = st.textInsY = 0.f;
  st.textHeightDraft = DefaultAnnotationTextHeightWorld(st);
  st.textRotDraft = 0.f;
}

void ResetMtextDraft(AppCommandState& st) {
  st.mtextPhase = AppCommandState::MtextPhase::WaitCorner1;
  st.mtxtX1 = st.mtxtY1 = st.mtxtX2 = st.mtxtY2 = 0.f;
  CloseMtextRichEditorUi(st);
}

void ResetDimDraft(AppCommandState& st) {
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  st.dimE1x = st.dimE1y = st.dimE2x = st.dimE2y = 0.f;
  st.dimLinearDraftVertical = false;
  st.dimLinearOrientUserLock = false;
  st.dimLinearLockCursorWx = st.dimLinearLockCursorWy = 0.f;
}

void ResetDimAngularDraft(AppCommandState& st) {
  st.dimAngularPhase = AppCommandState::DimAngularPhase::WaitVertex;
  st.dimAngVx = st.dimAngVy = 0.f;
}

static void ResetSurveyInverseDraft(AppCommandState& st) {
  st.surveyInversePhase = AppCommandState::SurveyInversePhase::WaitFrom;
  st.surveyInverseFromX = st.surveyInverseFromY = 0.f;
}

static void ResetAllCadDraftTools(AppCommandState& st) {
  ResetCircleDraft(st);
  ResetPolylineDraft(st);
  ResetArcDraft(st);
  ResetEllipseDraft(st);
  ResetTextCmdDraft(st);
  ResetMtextDraft(st);
  ResetDimDraft(st);
  ResetDimAngularDraft(st);
  ResetSurveyInverseDraft(st);
  ClearDimGripInteraction(st);
  AbortMtextGripInteraction(st);
  // Release PDF draft resources if any command was running
  if (st.pdfDraftCache) {
    PdfDraftCache_Free(st.pdfDraftCache);
    st.pdfDraftCache = nullptr;
  }
  if (st.pdfAttachPreviewReady) {
    PdfAttach_ReleaseTexture(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
  }
  st.pdfAttachDialogOpen = false;
  st.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
}

static void CommitDimAngularAt(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAngular;
  d.dimAngVertexX = st.dimAngVx;
  d.dimAngVertexY = st.dimAngVy;
  d.dimExt1X = st.dimE1x;
  d.dimExt1Y = st.dimE1y;
  d.dimExt2X = st.dimE2x;
  d.dimExt2Y = st.dimE2y;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(d, &a1, &a2, &sweep, &bisx, &bisy, &theta)) {
    log.push_back("DIMANGULAR — points are degenerate or collinear.");
    return;
  }
  if (theta < 1e-5f) {
    log.push_back("DIMANGULAR — angle is zero (ray points coincide with vertex direction).");
    return;
  }
  const float vx = st.dimAngVx, vy = st.dimAngVy;
  const float leg = std::min(std::hypot(st.dimE1x - vx, st.dimE1y - vy), std::hypot(st.dimE2x - vx, st.dimE2y - vy));
  const float rMax = std::max(1.e-4f, 0.92f * leg);
  const float rMin = std::max(1.e-4f, 0.02f * leg);
  const float R = CadDimAngularPickRadius(vx, vy, bisx, bisy, wx, wy, rMin, rMax);
  d.dimSignedOffset = R;
  d.plottedHeightInches = st.defaultPlottedTextHeightInches * 0.85f;
  d.text = CadFormatAngleDegMinSecFromRad(theta);
  CadDimAngularSyncTextPlacement(&d, st.modelUnitsPerPlottedInch);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  st.cadAnnotations.push_back(std::move(d));
  st.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetDimAngularDraft(st);
  ResetDimDraft(st);
  log.push_back("DIMANGULAR complete.");
}

void CommitCircle(AppCommandState& st, float cx, float cy, float r, std::vector<std::string>& log) {
  if (r < 1e-5f) {
    log.push_back("Circle radius too small.");
    return;
  }
  st.userCirclesCxCyR.push_back(cx);
  st.userCirclesCxCyR.push_back(cy);
  st.userCirclesCxCyR.push_back(r);
  st.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetCircleDraft(st);
  log.push_back("Circle complete.");
}

bool ParseRadiusOrDiameter(const std::string& raw, float* radiusOut, std::vector<std::string>& log) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  if (!low.empty() && low[0] == 'd') {
    float dia = 0.f;
    if (!ParseOneFloat(low.substr(1), &dia)) {
      log.push_back("Expected diameter after D (e.g. D 40 or D40).");
      return false;
    }
    if (dia <= 0.f) {
      log.push_back("Diameter must be positive.");
      return false;
    }
    *radiusOut = dia * 0.5f;
    return true;
  }
  if (!ParseOneFloat(s, radiusOut)) {
    log.push_back("Expected a radius (number) or D + diameter.");
    return false;
  }
  if (*radiusOut <= 0.f) {
    log.push_back("Radius must be positive.");
    return false;
  }
  return true;
}

bool DispatchByPrimary(const std::string& primary, AppCommandState& st, std::vector<std::string>& log) {
  if (primary == "line") {
    StartLineCommand(st, log);
    return true;
  }
  if (primary == "circle") {
    StartCircleCommand(st, log);
    return true;
  }
  if (primary == "polyline") {
    StartPolylineCommand(st, log);
    return true;
  }
  if (primary == "arc") {
    StartArcCommand(st, log);
    return true;
  }
  if (primary == "ellipse") {
    StartEllipseCommand(st, log);
    return true;
  }
  if (primary == "text") {
    StartTextCommand(st, log);
    return true;
  }
  if (primary == "mtext") {
    StartMtextCommand(st, log);
    return true;
  }
  if (primary == "dimaligned") {
    StartDimAlignedCommand(st, log);
    return true;
  }
  if (primary == "dimlinear") {
    StartDimLinearCommand(st, log);
    return true;
  }
  if (primary == "dimangular") {
    StartDimAngularCommand(st, log);
    return true;
  }
  if (primary == "id") {
    StartIdPointCommand(st, log);
    return true;
  }
  if (primary == "inverse") {
    StartSurveyInverseCommand(st, log);
    return true;
  }
  if (primary == "plotscale") {
    log.push_back(
        "PLOTSCALE sets drawing units per plotted inch (e.g. 50 for civil 1\"=50'). Usage: PLOTSCALE 50");
    return true;
  }
  if (primary == "regen") {
    BumpCadGpuCache(st);
    log.push_back("REGEN — viewport caches refreshed.");
    return true;
  }
  if (primary == "layer") {
    SyncDrawingLayerTableWithGeometry(st);
    st.showLayerManagerWindow = true;
    log.push_back("LAYER — layer manager opened.");
    return true;
  }
  if (primary == "move") {
    StartMoveCommand(st, log);
    return true;
  }
  if (primary == "copy") {
    StartCopyCommand(st, log);
    return true;
  }
  if (primary == "rotate") {
    StartRotateCommand(st, log);
    return true;
  }
  if (primary == "scale") {
    StartScaleCommand(st, log);
    return true;
  }
  if (primary == "delete") {
    StartDeleteCommand(st, log);
    return true;
  }
  if (primary == "join") {
    StartJoinCommand(st, log);
    return true;
  }
  if (primary == "trim") {
    StartTrimCommand(st, log);
    return true;
  }
  if (primary == "offset") {
    StartOffsetCommand(st, log);
    return true;
  }
  if (primary == "zoomextents") {
    StartZoomExtentsCommand(st, log);
    return true;
  }
  if (primary == "zoomwindow") {
    StartZoomWindowCommand(st, log);
    return true;
  }
  if (primary == "createpoints") {
    StartCreatePointsCommand(st, log);
    return true;
  }
  if (primary == "viewpoints") {
    StartViewPointsCommand(st, log);
    return true;
  }
  if (primary == "importpoints") {
    StartImportPointsCommand(st, log);
    return true;
  }
  if (primary == "exportpoints") {
    StartExportPointsCommand(st, log);
    return true;
  }
  if (primary == "pdfattach" || primary == "pdfatt") {
    StartPdfAttachCommand(st, log);
    return true;
  }
  if (primary == "select") {
    ClearPendingViewportZoom(st);
    ClearSelection(st);
    st.selBoxWaitingSecond = false;
    log.push_back("SELECT — click two corners for a window (default when no command is active).");
    return true;
  }
  if (primary == "help") {
    log.push_back(
        "LINE (L), POLYLINE (PL, CLOSE to close), ARC (3-point), ELLIPSE (center, axis end, ratio), TEXT, MTEXT, "
        "DIMALIGNED (DAL), DIMLINEAR (DLI), DIMANGULAR (DAN), ID, INVERSE (INV), CIRCLE (C), MOVE (M), COPY (CP), ROTATE (RO), SCALE (SC), DELETE (DEL), OFFSET (O), ZOOM (ZE/ZW), "
        "PLOTSCALE "
        "(PSCALE), REGEN (RE), LAYER (LA). SURVEY: CRTPTS, VWPTS, IMPPTS, EXPPTS, INVERSE (INV). Idle: two-click box selects. ESC.");
    log.push_back(
        "LINE: @dx,dy from anchor; A or ANGLE alone then bearing on next line (blank Enter cancels); A<bearing> (+ "
        "optional +90) on one line; AP + two picks then Enter (or +90) locks bearing; distance (+/-) along ray; A "
        "clears when lock or AP pick is active. Ortho: distance toward cursor.");
    log.push_back(
        "ROTATE: ° clockwise from north / DMS; R reference; then bearing or P. SCALE: after base, factor or pick from "
        "base; R / REFERENCE then two-point ref length, then new length (type or two picks). INVERSE: two points "
        "(World X=E, Y=N); logs dE, dN, distance, bearing in deg/min/sec and decimal deg CW from north. DELETE / ZW use unsnapped "
        "windows. TRIM "
        "matches Civil 3D: cutting edges, Enter, trim clicks. OFFSET: pick entity, distance + side or through-click "
        "(line/circle/arc). ZE fits geometry.");
    return true;
  }
  return false;
}

bool HandleCircleTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);

  switch (st.circlePhase) {
  case AppCommandState::CirclePhase::WaitCenterOrMode: {
    if (StringUtil::toLowerAsciiCopy(line) == "3p") {
      st.circleStyle = AppCommandState::CircleStyle::ThreePoint;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP1;
      log.push_back("Three-point circle — specify first point.");
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.circleCx = px;
    st.circleCy = py;
    st.circlePhase = AppCommandState::CirclePhase::WaitRadius;
    log.push_back("Center set — radius (click), type value, or D + diameter.");
    return true;
  }
  case AppCommandState::CirclePhase::WaitRadius: {
    float rad = 0.f;
    if (!ParseRadiusOrDiameter(line, &rad, log))
      return false;
    CommitCircle(st, st.circleCx, st.circleCy, rad, log);
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP1: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.c3p1x = px;
    st.c3p1y = py;
    st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP2;
    log.push_back("Second point:");
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.c3p2x = px;
    st.c3p2y = py;
    st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP3;
    log.push_back("Third point:");
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP3: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    float ox = 0.f;
    float oy = 0.f;
    float r = 0.f;
    if (!ComputeCircumcircle(st.c3p1x, st.c3p1y, st.c3p2x, st.c3p2y, px, py, &ox, &oy, &r)) {
      log.push_back("Points are collinear — no circle.");
      return true;
    }
    CommitCircle(st, ox, oy, r, log);
    return true;
  }
  }
  return false;
}

bool SelectedEntityEqual(const SelectedEntity& a, const SelectedEntity& b) {
  return a.type == b.type && a.index == b.index;
}

void ArcRoughBounds(const CadArc& a, float* outMnX, float* outMxX, float* outMnY, float* outMxY, bool* any) {
  const int n = std::max(8, static_cast<int>(std::fabs(static_cast<double>(a.sweepRad)) / (3.14159265 / 16.0)) + 1);
  for (int i = 0; i <= n; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(n);
    const float t = a.startRad + a.sweepRad * u;
    const float x = a.cx + a.r * std::cos(t);
    const float y = a.cy + a.r * std::sin(t);
    if (!*any) {
      *outMnX = *outMxX = x;
      *outMnY = *outMxY = y;
      *any = true;
    } else {
      *outMnX = std::min(*outMnX, x);
      *outMxX = std::max(*outMxX, x);
      *outMnY = std::min(*outMnY, y);
      *outMxY = std::max(*outMxY, y);
    }
  }
}

void EllipseRoughBounds(const CadEllipse& e, float* outMnX, float* outMxX, float* outMnY, float* outMxY,
                        bool* any) {
  const float ma = std::hypot(e.majVx, e.majVy);
  if (ma < 1e-8f)
    return;
  const float ux = e.majVx / ma;
  const float uy = e.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * e.ratio;
  constexpr int n = 48;
  constexpr float twopi = 6.28318530718f;
  for (int i = 0; i <= n; ++i) {
    const float ang = twopi * static_cast<float>(i) / static_cast<float>(n);
    const float c = std::cos(ang);
    const float s = std::sin(ang);
    const float x = e.cx + ux * (ma * c) + px * (mb * s);
    const float y = e.cy + uy * (ma * c) + py * (mb * s);
    if (!*any) {
      *outMnX = *outMxX = x;
      *outMnY = *outMxY = y;
      *any = true;
    } else {
      *outMnX = std::min(*outMnX, x);
      *outMxX = std::max(*outMxX, x);
      *outMnY = std::min(*outMnY, y);
      *outMxY = std::max(*outMxY, y);
    }
  }
}

bool PolylineHitsRect(const AppCommandState& st, int pi, float mnX, float mxX, float mnY, float mxY, bool windowMode) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return false;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  if (v0 >= v1)
    return false;
  const auto& V = st.userPolylineVerts;
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
  const int nVert = v1 - v0;
  if (windowMode) {
    for (int vi = v0; vi < v1; ++vi) {
      const float x = V[static_cast<size_t>(vi * 3 + 0)];
      const float y = V[static_cast<size_t>(vi * 3 + 1)];
      if (!PointInsideClosedRect(x, y, mnX, mxX, mnY, mxY))
        return false;
    }
    return true;
  }
  for (int vi = v0; vi + 1 < v1; ++vi) {
    const float x0 = V[static_cast<size_t>(vi * 3 + 0)];
    const float y0 = V[static_cast<size_t>(vi * 3 + 1)];
    const float x1 = V[static_cast<size_t>((vi + 1) * 3 + 0)];
    const float y1 = V[static_cast<size_t>((vi + 1) * 3 + 1)];
    if (SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY))
      return true;
  }
  if (closed && nVert >= 2) {
    const float x0 = V[static_cast<size_t>((v1 - 1) * 3 + 0)];
    const float y0 = V[static_cast<size_t>((v1 - 1) * 3 + 1)];
    const float x1 = V[static_cast<size_t>(v0 * 3 + 0)];
    const float y1 = V[static_cast<size_t>(v0 * 3 + 1)];
    if (SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY))
      return true;
  }
  return false;
}

void ComputeSelectionFromRect(AppCommandState& st, float xa, float ya, float xb, float yb, bool subtract,
                              bool windowMode, bool includeSurveyPoints) {
  float mnX = std::min(xa, xb);
  float mxX = std::max(xa, xb);
  float mnY = std::min(ya, yb);
  float mxY = std::max(ya, yb);
  const float expand = 1e-4f;
  if (mxX - mnX < expand) {
    mnX -= expand;
    mxX += expand;
  }
  if (mxY - mnY < expand) {
    mnY -= expand;
    mxY += expand;
  }

  std::vector<SelectedEntity> hits;
  std::vector<int> surveyHits;
  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      const float x0 = L[i];
      const float y0 = L[i + 1];
      const float x1 = L[i + 3];
      const float y1 = L[i + 4];
      bool hit = false;
      if (windowMode)
        hit = PointInsideClosedRect(x0, y0, mnX, mxX, mnY, mxY) &&
              PointInsideClosedRect(x1, y1, mnX, mxX, mnY, mxY);
      else
        hit = SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY);
      if (hit) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::LineSeg;
        e.index = static_cast<int>(i / 6);
        hits.push_back(e);
      }
    }
  }
  const auto& C = st.userCirclesCxCyR;
  if (C.size() % 3 == 0) {
    for (size_t ci = 0; ci + 2 < C.size(); ci += 3) {
      const float cx = C[ci];
      const float cy = C[ci + 1];
      const float r = C[ci + 2];
      bool hit = false;
      if (windowMode)
        hit = CircleFullyInsideRect(cx, cy, r, mnX, mxX, mnY, mxY);
      else
        hit = CircleIntersectsAABB(cx, cy, r, mnX, mxX, mnY, mxY);
      if (hit) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Circle;
        e.index = static_cast<int>(ci / 3);
        hits.push_back(e);
      }
    }
  }
  const size_t nAnn = st.cadAnnotations.size();
  for (size_t ai = 0; ai < nAnn; ++ai) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(st.cadAnnotations[ai], st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    bool hit = false;
    if (windowMode)
      hit = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
    else
      hit = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Annotation;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    float amnX = 0.f;
    float amxX = 0.f;
    float amnY = 0.f;
    float amxY = 0.f;
    bool any = false;
    ArcRoughBounds(st.userArcs[ai], &amnX, &amxX, &amnY, &amxY, &any);
    if (!any)
      continue;
    bool hit = false;
    if (windowMode)
      hit = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
    else
      hit = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Arc;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    float emnX = 0.f;
    float emxX = 0.f;
    float emnY = 0.f;
    float emxY = 0.f;
    bool any = false;
    EllipseRoughBounds(st.userEllipses[ei], &emnX, &emxX, &emnY, &emxY, &any);
    if (!any)
      continue;
    bool hit = false;
    if (windowMode)
      hit = emnX >= mnX && emxX <= mxX && emnY >= mnY && emxY <= mxY;
    else
      hit = !(emxX < mnX || emnX > mxX || emxY < mnY || emnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Ellipse;
      e.index = static_cast<int>(ei);
      hits.push_back(e);
    }
  }
  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    if (PolylineHitsRect(st, pi, mnX, mxX, mnY, mxY, windowMode)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Polyline;
      e.index = pi;
      hits.push_back(e);
    }
  }
  if (includeSurveyPoints) {
    for (size_t si = 0; si < st.surveyPoints.size(); ++si) {
      const SurveyPoint& sp = st.surveyPoints[si];
      const bool hitPoint = PointInsideClosedRect(sp.easting, sp.northing, mnX, mxX, mnY, mxY);
      bool hitLabel = false;
      const int lix = sp.labelMtextAnnIndex;
      if (lix >= 0 && static_cast<size_t>(lix) < st.cadAnnotations.size()) {
        const CadAnnotation& lab = st.cadAnnotations[static_cast<size_t>(lix)];
        if (lab.kind == CadAnnotation::Kind::Mtext && lab.surveyPointLabelFor == static_cast<int>(si)) {
          float amnX = 0.f;
          float amnY = 0.f;
          float amxX = 0.f;
          float amxY = 0.f;
          CadAnnotationRoughBounds(lab, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
          if (windowMode)
            hitLabel = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
          else
            hitLabel = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
        }
      }
      if (hitPoint || hitLabel)
        surveyHits.push_back(static_cast<int>(si));
    }
  }

  if (subtract) {
    std::vector<SelectedEntity> kept;
    kept.reserve(st.selection.size());
    for (const auto& e : st.selection) {
      bool remove = false;
      for (const auto& h : hits) {
        if (SelectedEntityEqual(h, e)) {
          remove = true;
          break;
        }
      }
      if (!remove)
        kept.push_back(e);
    }
    st.selection = std::move(kept);
    auto& sv = st.selectedSurveyPointIndices;
    sv.erase(std::remove_if(sv.begin(), sv.end(),
                            [&](int ix) {
                              return std::find(surveyHits.begin(), surveyHits.end(), ix) != surveyHits.end();
                            }),
             sv.end());
  } else {
    for (const auto& h : hits) {
      bool has = false;
      for (const auto& e : st.selection) {
        if (SelectedEntityEqual(h, e)) {
          has = true;
          break;
        }
      }
      if (!has)
        st.selection.push_back(h);
    }
    for (int six : surveyHits) {
      auto& sv = st.selectedSurveyPointIndices;
      if (std::find(sv.begin(), sv.end(), six) == sv.end())
        sv.push_back(six);
    }
  }
}

void RotateAroundBase(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  float dx = *x - bx;
  float dy = *y - by;
  *x = bx + c * dx - s * dy;
  *y = by + s * dx + c * dy;
}

/// Rotates \c DimLinear extension points and offset; keeps \p ann.dimLinearVertical; refreshes \p ann.rotationRad.

static void RotateCadDimLinearAroundBase(float bx, float by, float rad, CadAnnotation* ann) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  const float x1 = ann->dimExt1X, y1 = ann->dimExt1Y, x2 = ann->dimExt2X, y2 = ann->dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  float dmx = cmx;
  float dmy = cmy;
  if (!ann->dimLinearVertical)
    dmy = cmy + ann->dimSignedOffset;
  else
    dmx = cmx + ann->dimSignedOffset;
  RotateAroundBase(bx, by, rad, &ann->dimExt1X, &ann->dimExt1Y);
  RotateAroundBase(bx, by, rad, &ann->dimExt2X, &ann->dimExt2Y);
  RotateAroundBase(bx, by, rad, &dmx, &dmy);
  const float ncmx = 0.5f * (ann->dimExt1X + ann->dimExt2X);
  const float ncmy = 0.5f * (ann->dimExt1Y + ann->dimExt2Y);
  if (!ann->dimLinearVertical)
    ann->dimSignedOffset = dmy - ncmy;
  else
    ann->dimSignedOffset = dmx - ncmx;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (CadDimLinearGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    ann->rotationRad = std::atan2(ty, tx);
}

static float NormalizeAngleRadMinusPiToPi(float a) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = 2.f * kPi;
  while (a > kPi)
    a -= kTwoPi;
  while (a < -kPi)
    a += kTwoPi;
  return a;
}

static void ApplyTranslationToSelectedSurveyPoints(AppCommandState& st, float dx, float dy) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size()) {
      st.surveyPoints[static_cast<size_t>(i)].easting += dx;
      st.surveyPoints[static_cast<size_t>(i)].northing += dy;
    }
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

static void ApplyRotationToSelectedSurveyPoints(AppCommandState& st, float bx, float by, float rad) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.surveyPoints.size())
      continue;
    float x = st.surveyPoints[static_cast<size_t>(i)].easting;
    float y = st.surveyPoints[static_cast<size_t>(i)].northing;
    RotateAroundBase(bx, by, rad, &x, &y);
    st.surveyPoints[static_cast<size_t>(i)].easting = x;
    st.surveyPoints[static_cast<size_t>(i)].northing = y;
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

static void DuplicateCadSelectionTranslated(AppCommandState& st, float dx, float dy) {
  const size_t polyVertsBefore = st.userPolylineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        for (int j = 0; j < 6; ++j)
          newLines.push_back(st.userLinesFlat[k + static_cast<size_t>(j)]);
        newLines[newLines.size() - 6] += dx;
        newLines[newLines.size() - 5] += dy;
        newLines[newLines.size() - 3] += dx;
        newLines[newLines.size() - 2] += dy;
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userLineAttrs.size())
          a = st.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(a);
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 < st.userCirclesCxCyR.size()) {
        newCircles.push_back(st.userCirclesCxCyR[k] + dx);
        newCircles.push_back(st.userCirclesCxCyR[k + 1] + dy);
        newCircles.push_back(st.userCirclesCxCyR[k + 2]);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userCircleAttrs.size())
          a = st.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(a);
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.cadAnnotations.size()) {
        CadAnnotation c = st.cadAnnotations[k];
        c.surveyPointLabelFor = -1;
        c.insX += dx;
        c.insY += dy;
        if (c.kind == CadAnnotation::Kind::Mtext) {
          c.boxMinX += dx;
          c.boxMinY += dy;
          c.boxMaxX += dx;
          c.boxMaxY += dy;
        } else if (c.kind == CadAnnotation::Kind::DimAligned || c.kind == CadAnnotation::Kind::DimLinear) {
          c.dimExt1X += dx;
          c.dimExt1Y += dy;
          c.dimExt2X += dx;
          c.dimExt2Y += dy;
        }
        newAnn.push_back(std::move(c));
        EntityAttributes a{};
        if (k < st.cadAnnotationAttrs.size())
          a = st.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(a);
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        CadArc a = st.userArcs[k];
        a.cx += dx;
        a.cy += dy;
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.userArcAttrs.size())
          at = st.userArcAttrs[k];
        newArcAttrs.push_back(at);
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        CadEllipse el = st.userEllipses[k];
        el.cx += dx;
        el.cy += dy;
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.userEllAttrs.size())
          at = st.userEllAttrs[k];
        newEllAttrs.push_back(at);
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.userPolylineOffsets.empty())
        st.userPolylineOffsets.push_back(0);
      const int baseVert = st.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        st.userPolylineVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)] + dx);
        st.userPolylineVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
        st.userPolylineVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
      }
      st.userPolylineOffsets.push_back(baseVert + nv);
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
        cl = st.userPolylineClosed[static_cast<size_t>(pi)];
      st.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
        at = st.userPolylineAttrs[static_cast<size_t>(pi)];
      st.userPolylineAttrs.push_back(at);
    }
  }
  st.userLinesFlat.insert(st.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.userCirclesCxCyR.insert(st.userCirclesCxCyR.end(), newCircles.begin(), newCircles.end());
  st.userLineAttrs.insert(st.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.userCircleAttrs.insert(st.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.cadAnnotations.insert(st.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.cadAnnotationAttrs.insert(st.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.userArcs.insert(st.userArcs.end(), newArcs.begin(), newArcs.end());
  st.userArcAttrs.insert(st.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.userEllipses.insert(st.userEllipses.end(), newEll.begin(), newEll.end());
  st.userEllAttrs.insert(st.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      st.userPolylineVerts.size() != polyVertsBefore)
    BumpCadGpuCache(st);
}

static void DuplicateCadSelectionRotated(AppCommandState& st, float bx, float by, float rad) {
  const size_t polyVertsBefore = st.userPolylineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        float x0 = st.userLinesFlat[k];
        float y0 = st.userLinesFlat[k + 1];
        float z0 = st.userLinesFlat[k + 2];
        float x1 = st.userLinesFlat[k + 3];
        float y1 = st.userLinesFlat[k + 4];
        float z1 = st.userLinesFlat[k + 5];
        RotateAroundBase(bx, by, rad, &x0, &y0);
        RotateAroundBase(bx, by, rad, &x1, &y1);
        newLines.push_back(x0);
        newLines.push_back(y0);
        newLines.push_back(z0);
        newLines.push_back(x1);
        newLines.push_back(y1);
        newLines.push_back(z1);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userLineAttrs.size())
          a = st.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(a);
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 < st.userCirclesCxCyR.size()) {
        float cx = st.userCirclesCxCyR[k];
        float cy = st.userCirclesCxCyR[k + 1];
        float r = st.userCirclesCxCyR[k + 2];
        RotateAroundBase(bx, by, rad, &cx, &cy);
        newCircles.push_back(cx);
        newCircles.push_back(cy);
        newCircles.push_back(r);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userCircleAttrs.size())
          a = st.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(a);
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.cadAnnotations.size()) {
        CadAnnotation c = st.cadAnnotations[k];
        c.surveyPointLabelFor = -1;
        RotateAroundBase(bx, by, rad, &c.insX, &c.insY);
        if (c.kind == CadAnnotation::Kind::Text) {
          c.rotationRad += rad;
        } else if (c.kind == CadAnnotation::Kind::DimLinear) {
          RotateCadDimLinearAroundBase(bx, by, rad, &c);
        } else if (c.kind == CadAnnotation::Kind::DimAligned) {
          RotateAroundBase(bx, by, rad, &c.dimExt1X, &c.dimExt1Y);
          RotateAroundBase(bx, by, rad, &c.dimExt2X, &c.dimExt2Y);
          float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
          if (CadDimAlignedGeometry(c, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
            c.rotationRad = std::atan2(ty, tx);
        } else {
          float xs[4] = {c.boxMinX, c.boxMaxX, c.boxMaxX, c.boxMinX};
          float ys[4] = {c.boxMinY, c.boxMinY, c.boxMaxY, c.boxMaxY};
          float mnX = xs[0];
          float mxX = xs[0];
          float mnY = ys[0];
          float mxY = ys[0];
          for (int i = 0; i < 4; ++i) {
            RotateAroundBase(bx, by, rad, &xs[i], &ys[i]);
            mnX = std::min(mnX, xs[i]);
            mxX = std::max(mxX, xs[i]);
            mnY = std::min(mnY, ys[i]);
            mxY = std::max(mxY, ys[i]);
          }
          c.boxMinX = mnX;
          c.boxMaxX = mxX;
          c.boxMinY = mnY;
          c.boxMaxY = mxY;
          c.insX = mnX;
          c.insY = mnY;
        }
        newAnn.push_back(std::move(c));
        EntityAttributes a{};
        if (k < st.cadAnnotationAttrs.size())
          a = st.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(a);
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        CadArc a = st.userArcs[k];
        RotateAroundBase(bx, by, rad, &a.cx, &a.cy);
        a.startRad += rad;
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.userArcAttrs.size())
          at = st.userArcAttrs[k];
        newArcAttrs.push_back(at);
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        CadEllipse el = st.userEllipses[k];
        float mx = el.cx + el.majVx;
        float my = el.cy + el.majVy;
        RotateAroundBase(bx, by, rad, &el.cx, &el.cy);
        RotateAroundBase(bx, by, rad, &mx, &my);
        el.majVx = mx - el.cx;
        el.majVy = my - el.cy;
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.userEllAttrs.size())
          at = st.userEllAttrs[k];
        newEllAttrs.push_back(at);
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.userPolylineOffsets.empty())
        st.userPolylineOffsets.push_back(0);
      const int baseVert = st.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        float px = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)];
        float py = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        float pz = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)];
        RotateAroundBase(bx, by, rad, &px, &py);
        st.userPolylineVerts.push_back(px);
        st.userPolylineVerts.push_back(py);
        st.userPolylineVerts.push_back(pz);
      }
      st.userPolylineOffsets.push_back(baseVert + nv);
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
        cl = st.userPolylineClosed[static_cast<size_t>(pi)];
      st.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
        at = st.userPolylineAttrs[static_cast<size_t>(pi)];
      st.userPolylineAttrs.push_back(at);
    }
  }
  st.userLinesFlat.insert(st.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.userCirclesCxCyR.insert(st.userCirclesCxCyR.end(), newCircles.begin(), newCircles.end());
  st.userLineAttrs.insert(st.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.userCircleAttrs.insert(st.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.cadAnnotations.insert(st.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.cadAnnotationAttrs.insert(st.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.userArcs.insert(st.userArcs.end(), newArcs.begin(), newArcs.end());
  st.userArcAttrs.insert(st.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.userEllipses.insert(st.userEllipses.end(), newEll.begin(), newEll.end());
  st.userEllAttrs.insert(st.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      st.userPolylineVerts.size() != polyVertsBefore)
    BumpCadGpuCache(st);
}

static void FinalizeCopyTranslation(AppCommandState& st, float dx, float dy, std::vector<std::string>& log) {
  st.pendingSurveyDupIsRotate = false;
  DuplicateCadSelectionTranslated(st, dx, dy);
  st.active = AppCommandState::Kind::None;
  ResetModifyRotateDraft(st);
  if (!st.selectedSurveyPointIndices.empty()) {
    st.pendingCopyDx = dx;
    st.pendingCopyDy = dy;
    st.copySurveyDupModalOpen = true;
    st.copySurveyDupModalOpenRequested = true;
    log.push_back("COPY — CAD geometry duplicated; choose survey ID policy.");
  } else {
    log.push_back("COPY complete.");
  }
}

void ApplyRotationToSelection(AppCommandState& st, float bx, float by, float rad) {
  std::vector<bool> lineMark(std::max<size_t>(1, st.userLinesFlat.size() / 6), false);
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::LineSeg)
      continue;
    if (e.index >= 0 && static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  if (!lineMark.empty()) {
    for (size_t i = 0; i < lineMark.size(); ++i) {
      if (!lineMark[i])
        continue;
      size_t k = i * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        RotateAroundBase(bx, by, rad, &st.userLinesFlat[k], &st.userLinesFlat[k + 1]);
        RotateAroundBase(bx, by, rad, &st.userLinesFlat[k + 3], &st.userLinesFlat[k + 4]);
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    size_t k = static_cast<size_t>(e.index) * 3;
    if (k + 2 < st.userCirclesCxCyR.size()) {
      RotateAroundBase(bx, by, rad, &st.userCirclesCxCyR[k], &st.userCirclesCxCyR[k + 1]);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userArcs.size())
      continue;
    CadArc& a = st.userArcs[k];
    RotateAroundBase(bx, by, rad, &a.cx, &a.cy);
    a.startRad += rad;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userEllipses.size())
      continue;
    CadEllipse& el = st.userEllipses[k];
    float mx = el.cx + el.majVx;
    float my = el.cy + el.majVy;
    RotateAroundBase(bx, by, rad, &el.cx, &el.cy);
    RotateAroundBase(bx, by, rad, &mx, &my);
    el.majVx = mx - el.cx;
    el.majVy = my - el.cy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      continue;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi < v1; ++vi) {
      RotateAroundBase(bx, by, rad, &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)],
                       &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.cadAnnotations[k];
    RotateAroundBase(bx, by, rad, &a.insX, &a.insY);
    if (a.kind == CadAnnotation::Kind::Text) {
      a.rotationRad += rad;
    } else if (a.kind == CadAnnotation::Kind::DimLinear) {
      RotateCadDimLinearAroundBase(bx, by, rad, &a);
    } else if (a.kind == CadAnnotation::Kind::DimAligned) {
      RotateAroundBase(bx, by, rad, &a.dimExt1X, &a.dimExt1Y);
      RotateAroundBase(bx, by, rad, &a.dimExt2X, &a.dimExt2Y);
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
      if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
        a.rotationRad = std::atan2(ty, tx);
    } else {
      float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
      float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
      float mnX = xs[0];
      float mxX = xs[0];
      float mnY = ys[0];
      float mxY = ys[0];
      for (int i = 0; i < 4; ++i) {
        RotateAroundBase(bx, by, rad, &xs[i], &ys[i]);
        mnX = std::min(mnX, xs[i]);
        mxX = std::max(mxX, xs[i]);
        mnY = std::min(mnY, ys[i]);
        mxY = std::max(mxY, ys[i]);
      }
      a.boxMinX = mnX;
      a.boxMaxX = mxX;
      a.boxMinY = mnY;
      a.boxMaxY = mxY;
      a.insX = mnX;
      a.insY = mnY;
    }
  }
  ApplyRotationToSelectedSurveyPoints(st, bx, by, rad);
  BumpCadGpuCache(st);
}

void ApplyTranslationToSelection(AppCommandState& st, float dx, float dy) {
  std::vector<bool> lineMark(std::max<size_t>(1, st.userLinesFlat.size() / 6), false);
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 &&
        static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  for (size_t i = 0; i < lineMark.size(); ++i) {
    if (!lineMark[i])
      continue;
    size_t k = i * 6;
    if (k + 5 < st.userLinesFlat.size()) {
      st.userLinesFlat[k] += dx;
      st.userLinesFlat[k + 1] += dy;
      st.userLinesFlat[k + 3] += dx;
      st.userLinesFlat[k + 4] += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    size_t k = static_cast<size_t>(e.index) * 3;
    if (k + 2 < st.userCirclesCxCyR.size()) {
      st.userCirclesCxCyR[k] += dx;
      st.userCirclesCxCyR[k + 1] += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userArcs.size())
      continue;
    st.userArcs[k].cx += dx;
    st.userArcs[k].cy += dy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userEllipses.size())
      continue;
    st.userEllipses[k].cx += dx;
    st.userEllipses[k].cy += dy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      continue;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi < v1; ++vi) {
      st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)] += dx;
      st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.cadAnnotations[k];
    a.insX += dx;
    a.insY += dy;
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx;
      a.boxMinY += dy;
      a.boxMaxX += dx;
      a.boxMaxY += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      a.dimExt1X += dx;
      a.dimExt1Y += dy;
      a.dimExt2X += dx;
      a.dimExt2Y += dy;
    }
  }
  ApplyTranslationToSelectedSurveyPoints(st, dx, dy);
  BumpCadGpuCache(st);
}

static void ScalePtAroundBase(float bx, float by, float sc, float* x, float* y) {
  *x = bx + sc * (*x - bx);
  *y = by + sc * (*y - by);
}

static void ScaleCadDimLinearAroundBase(float bx, float by, float sc, CadAnnotation* ann) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  const float x1 = ann->dimExt1X, y1 = ann->dimExt1Y, x2 = ann->dimExt2X, y2 = ann->dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  float dmx = cmx;
  float dmy = cmy;
  if (!ann->dimLinearVertical)
    dmy = cmy + ann->dimSignedOffset;
  else
    dmx = cmx + ann->dimSignedOffset;
  ScalePtAroundBase(bx, by, sc, &ann->dimExt1X, &ann->dimExt1Y);
  ScalePtAroundBase(bx, by, sc, &ann->dimExt2X, &ann->dimExt2Y);
  ScalePtAroundBase(bx, by, sc, &dmx, &dmy);
  const float ncmx = 0.5f * (ann->dimExt1X + ann->dimExt2X);
  const float ncmy = 0.5f * (ann->dimExt1Y + ann->dimExt2Y);
  if (!ann->dimLinearVertical)
    ann->dimSignedOffset = dmy - ncmy;
  else
    ann->dimSignedOffset = dmx - ncmx;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (CadDimLinearGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    ann->rotationRad = std::atan2(ty, tx);
}

static bool ComputeSelectionCentroidWorld(const AppCommandState& st, float* outCx, float* outCy) {
  if (!outCx || !outCy)
    return false;
  double accx = 0.0;
  double accy = 0.0;
  int n = 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        accx += 0.5 * static_cast<double>(st.userLinesFlat[k] + st.userLinesFlat[k + 3]);
        accy += 0.5 * static_cast<double>(st.userLinesFlat[k + 1] + st.userLinesFlat[k + 4]);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 < st.userCirclesCxCyR.size()) {
        accx += static_cast<double>(st.userCirclesCxCyR[k]);
        accy += static_cast<double>(st.userCirclesCxCyR[k + 1]);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        accx += static_cast<double>(st.userArcs[k].cx);
        accy += static_cast<double>(st.userArcs[k].cy);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        accx += static_cast<double>(st.userEllipses[k].cx);
        accy += static_cast<double>(st.userEllipses[k].cy);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      double sx = 0.0, sy = 0.0;
      int nv = 0;
      for (int vi = v0; vi < v1; ++vi) {
        sx += static_cast<double>(st.userPolylineVerts[static_cast<size_t>(vi * 3)]);
        sy += static_cast<double>(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
        ++nv;
      }
      if (nv > 0) {
        accx += sx / static_cast<double>(nv);
        accy += sy / static_cast<double>(nv);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.cadAnnotations.size()) {
        const CadAnnotation& a = st.cadAnnotations[k];
        accx += static_cast<double>(a.insX);
        accy += static_cast<double>(a.insY);
        ++n;
      }
    }
  }
  for (int si : st.selectedSurveyPointIndices) {
    if (si >= 0 && static_cast<size_t>(si) < st.surveyPoints.size()) {
      accx += static_cast<double>(st.surveyPoints[static_cast<size_t>(si)].easting);
      accy += static_cast<double>(st.surveyPoints[static_cast<size_t>(si)].northing);
      ++n;
    }
  }
  if (n <= 0)
    return false;
  *outCx = static_cast<float>(accx / static_cast<double>(n));
  *outCy = static_cast<float>(accy / static_cast<double>(n));
  return true;
}

static void ComputeMaxSelectionDistanceFromPoint(const AppCommandState& st, float bx, float by, float* outMax) {
  if (!outMax)
    return;
  float m = 0.f;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        for (int i = 0; i < 2; ++i) {
          const float x = st.userLinesFlat[k + i * 3];
          const float y = st.userLinesFlat[k + i * 3 + 1];
          m = std::max(m, std::hypot(x - bx, y - by));
        }
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 < st.userCirclesCxCyR.size()) {
        const float cx = st.userCirclesCxCyR[k];
        const float cy = st.userCirclesCxCyR[k + 1];
        const float r = st.userCirclesCxCyR[k + 2];
        m = std::max(m, std::hypot(cx - bx, cy - by) + r);
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        const CadArc& a = st.userArcs[k];
        m = std::max(m, std::hypot(a.cx - bx, a.cy - by) + a.r);
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        const CadEllipse& el = st.userEllipses[k];
        const float ma = std::hypot(el.majVx, el.majVy);
        const float mb = ma * el.ratio;
        m = std::max(m, std::hypot(el.cx - bx, el.cy - by) + std::max(ma, mb));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      for (int vi = v0; vi < v1; ++vi) {
        const float x = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
        const float y = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        m = std::max(m, std::hypot(x - bx, y - by));
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.cadAnnotations.size())
        continue;
      const CadAnnotation& a = st.cadAnnotations[k];
      if (a.kind == CadAnnotation::Kind::Mtext) {
        float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
        float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
        for (int i = 0; i < 4; ++i)
          m = std::max(m, std::hypot(xs[i] - bx, ys[i] - by));
      } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
        m = std::max(m, std::hypot(a.dimExt1X - bx, a.dimExt1Y - by));
        m = std::max(m, std::hypot(a.dimExt2X - bx, a.dimExt2Y - by));
        m = std::max(m, std::hypot(a.insX - bx, a.insY - by));
      } else if (a.kind == CadAnnotation::Kind::DimAngular) {
        m = std::max(m, std::hypot(a.dimAngVertexX - bx, a.dimAngVertexY - by));
        m = std::max(m, std::hypot(a.dimExt1X - bx, a.dimExt1Y - by));
        m = std::max(m, std::hypot(a.dimExt2X - bx, a.dimExt2Y - by));
        m = std::max(m, std::hypot(a.insX - bx, a.insY - by));
      } else
        m = std::max(m, std::hypot(a.insX - bx, a.insY - by));
    }
  }
  for (int si : st.selectedSurveyPointIndices) {
    if (si >= 0 && static_cast<size_t>(si) < st.surveyPoints.size()) {
      const SurveyPoint& sp = st.surveyPoints[static_cast<size_t>(si)];
      m = std::max(m, std::hypot(sp.easting - bx, sp.northing - by));
    }
  }
  *outMax = m;
}

static float ComputeScaleReferenceDistance(const AppCommandState& st, float bx, float by) {
  float cx = 0.f, cy = 0.f;
  const bool haveC = ComputeSelectionCentroidWorld(st, &cx, &cy);
  const float dCent = haveC ? std::hypot(cx - bx, cy - by) : 0.f;
  float dMax = 0.f;
  ComputeMaxSelectionDistanceFromPoint(st, bx, by, &dMax);
  const float ref = std::max(dCent, 0.25f * std::max(dMax, 1e-6f));
  return std::max(ref, 1e-6f);
}

static void ApplyScaleToSelectedSurveyPoints(AppCommandState& st, float bx, float by, float sc) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.surveyPoints.size())
      continue;
    float x = st.surveyPoints[static_cast<size_t>(i)].easting;
    float y = st.surveyPoints[static_cast<size_t>(i)].northing;
    ScalePtAroundBase(bx, by, sc, &x, &y);
    st.surveyPoints[static_cast<size_t>(i)].easting = x;
    st.surveyPoints[static_cast<size_t>(i)].northing = y;
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

void ApplyScaleToSelection(AppCommandState& st, float bx, float by, float sc) {
  if (!(sc > 0.f) || !std::isfinite(sc))
    return;
  std::vector<bool> lineMark(std::max<size_t>(1, st.userLinesFlat.size() / 6), false);
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::LineSeg)
      continue;
    if (e.index >= 0 && static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  if (!lineMark.empty()) {
    for (size_t i = 0; i < lineMark.size(); ++i) {
      if (!lineMark[i])
        continue;
      size_t k = i * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        ScalePtAroundBase(bx, by, sc, &st.userLinesFlat[k], &st.userLinesFlat[k + 1]);
        ScalePtAroundBase(bx, by, sc, &st.userLinesFlat[k + 3], &st.userLinesFlat[k + 4]);
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    size_t k = static_cast<size_t>(e.index) * 3;
    if (k + 2 < st.userCirclesCxCyR.size()) {
      ScalePtAroundBase(bx, by, sc, &st.userCirclesCxCyR[k], &st.userCirclesCxCyR[k + 1]);
      st.userCirclesCxCyR[k + 2] *= sc;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userArcs.size())
      continue;
    CadArc& a = st.userArcs[k];
    ScalePtAroundBase(bx, by, sc, &a.cx, &a.cy);
    a.r *= sc;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userEllipses.size())
      continue;
    CadEllipse& el = st.userEllipses[k];
    float mx = el.cx + el.majVx;
    float my = el.cy + el.majVy;
    ScalePtAroundBase(bx, by, sc, &el.cx, &el.cy);
    ScalePtAroundBase(bx, by, sc, &mx, &my);
    el.majVx = mx - el.cx;
    el.majVy = my - el.cy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      continue;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi < v1; ++vi)
      ScalePtAroundBase(bx, by, sc, &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)],
                        &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.cadAnnotations[k];
    if (a.kind == CadAnnotation::Kind::Text) {
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    } else if (a.kind == CadAnnotation::Kind::Mtext) {
      ScalePtAroundBase(bx, by, sc, &a.boxMinX, &a.boxMinY);
      ScalePtAroundBase(bx, by, sc, &a.boxMaxX, &a.boxMaxY);
      if (a.boxMinX > a.boxMaxX)
        std::swap(a.boxMinX, a.boxMaxX);
      if (a.boxMinY > a.boxMaxY)
        std::swap(a.boxMinY, a.boxMaxY);
      a.insX = a.boxMinX;
      a.insY = a.boxMinY;
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    } else if (a.kind == CadAnnotation::Kind::DimLinear) {
      ScaleCadDimLinearAroundBase(bx, by, sc, &a);
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
      CadDimRefreshMeasurementText(&a);
    } else if (a.kind == CadAnnotation::Kind::DimAligned) {
      ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
      ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
      a.dimSignedOffset *= sc;
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
      if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
        a.rotationRad = std::atan2(ty, tx);
      CadDimRefreshMeasurementText(&a);
    } else if (a.kind == CadAnnotation::Kind::DimAngular) {
      ScalePtAroundBase(bx, by, sc, &a.dimAngVertexX, &a.dimAngVertexY);
      ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
      ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
      a.dimSignedOffset *= sc;
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
      CadDimAngularSyncTextPlacement(&a, st.modelUnitsPerPlottedInch);
      CadDimRefreshMeasurementText(&a);
    }
  }
  ApplyScaleToSelectedSurveyPoints(st, bx, by, sc);
  BumpCadGpuCache(st);
}

bool ParseAngleDegreesInternal(const std::string& raw, float* degreesOut) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  bool neg = false;
  if (!low.empty() && low[0] == '-') {
    neg = true;
    low = StringUtil::trimCopy(low.substr(1));
  }
  if (!low.empty() && low[0] == '+')
    low = StringUtil::trimCopy(low.substr(1));
  float deg = 0.f;
  float min = 0.f;
  float sec = 0.f;
  size_t pd = low.find('d');
  if (pd != std::string::npos) {
    if (!(std::istringstream(low.substr(0, pd)) >> deg))
      return false;
    std::string rest = low.substr(pd + 1);
    size_t pm = rest.find('m');
    if (pm != std::string::npos) {
      if (!(std::istringstream(rest.substr(0, pm)) >> min))
        return false;
      std::string rest2 = rest.substr(pm + 1);
      size_t ps = rest2.find('s');
      if (ps != std::string::npos) {
        if (!(std::istringstream(rest2.substr(0, ps)) >> sec))
          return false;
      } else {
        if (!(std::istringstream(rest2) >> sec))
          sec = 0.f;
      }
    } else {
      if (!(std::istringstream(rest) >> min))
        min = 0.f;
    }
    *degreesOut = (neg ? -1.f : 1.f) * (deg + min / 60.f + sec / 3600.f);
    return true;
  }
  if (!(std::istringstream(low) >> deg))
    return false;
  *degreesOut = neg ? -deg : deg;
  return true;
}

bool HandleModifyText(AppCommandState& st, bool isCopy, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using MP = AppCommandState::ModifyPhase;
  if (st.modifyPhase == MP::NeedBase) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.modifyBaseX = px;
    st.modifyBaseY = py;
    st.modifyPhase = MP::NeedDestination;
    log.push_back(isCopy ? "COPY — specify second point (destination)." : "MOVE — specify second point (destination).");
    return true;
  }
  if (st.modifyPhase == MP::NeedDestination) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, true, st.modifyBaseX, st.modifyBaseY))
      return false;
    float dx = px - st.modifyBaseX;
    float dy = py - st.modifyBaseY;
    if (isCopy)
      FinalizeCopyTranslation(st, dx, dy, log);
    else {
      ApplyTranslationToSelection(st, dx, dy);
      st.active = AppCommandState::Kind::None;
      ResetModifyRotateDraft(st);
      log.push_back("MOVE complete.");
    }
    return true;
  }
  (void)log;
  return false;
}

static void FinishScaleCommand(AppCommandState& st, float scaleFactor, std::vector<std::string>& log) {
  const float s = std::max(scaleFactor, 1e-6f);
  ApplyScaleToSelection(st, st.modifyBaseX, st.modifyBaseY, s);
  st.active = AppCommandState::Kind::None;
  ResetModifyRotateDraft(st);
  log.push_back("SCALE complete.");
}

static bool HandleScaleText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (st.modifyPhase == MP::NeedBase) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.modifyBaseX = px;
    st.modifyBaseY = py;
    st.scaleRefDist = ComputeScaleReferenceDistance(st, px, py);
    st.scalePhase = SP::FactorPick;
    st.modifyPhase = MP::NeedDestination;
    log.push_back(
        "SCALE — pick second point or type factor (>0), or R / REFERENCE for two-point reference length then new "
        "length.");
    return true;
  }
  if (st.modifyPhase != MP::NeedDestination)
    return false;

  switch (st.scalePhase) {
  case SP::FactorPick: {
    const std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "r" || low == "ref" || low == "reference") {
      st.scalePhase = SP::Ref_WaitP1;
      log.push_back("SCALE ref — first point of reference length:");
      return true;
    }
    float sf = 0.f;
    if (ParseOneFloat(line, &sf)) {
      if (!(sf > 0.f) || !std::isfinite(sf)) {
        log.push_back("SCALE — scale factor must be a positive finite number.");
        return false;
      }
      FinishScaleCommand(st, sf, log);
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, true, st.modifyBaseX, st.modifyBaseY))
      return false;
    const float d = std::hypot(px - st.modifyBaseX, py - st.modifyBaseY);
    FinishScaleCommand(st, d / std::max(st.scaleRefDist, 1e-20f), log);
    return true;
  }
  case SP::Ref_WaitP1: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.scaleRefP1X = px;
    st.scaleRefP1Y = py;
    st.scalePhase = SP::Ref_WaitP2;
    log.push_back("SCALE ref — second point of reference length:");
    return true;
  }
  case SP::Ref_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    const float refLen = std::hypot(px - st.scaleRefP1X, py - st.scaleRefP1Y);
    if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
      log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
      return false;
    }
    st.scaleRefDist = refLen;
    st.scalePhase = SP::NewLength_WaitTypedOrP1;
    log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
    return true;
  }
  case SP::NewLength_WaitTypedOrP1: {
    float L = 0.f;
    if (ParseOneFloat(line, &L)) {
      if (!(L > 0.f) || !std::isfinite(L)) {
        log.push_back("SCALE ref — new length must be a positive finite number.");
        return false;
      }
      FinishScaleCommand(st, L / std::max(st.scaleRefDist, 1e-20f), log);
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.scaleNewLenP1X = px;
    st.scaleNewLenP1Y = py;
    st.scalePhase = SP::NewLength_WaitP2;
    log.push_back("SCALE ref — second point of new length segment:");
    return true;
  }
  case SP::NewLength_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    const float newLen = std::hypot(px - st.scaleNewLenP1X, py - st.scaleNewLenP1Y);
    if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
      log.push_back("SCALE ref — new length is too small; pick two distinct points.");
      return false;
    }
    FinishScaleCommand(st, newLen / std::max(st.scaleRefDist, 1e-20f), log);
    return true;
  }
  default:
    return false;
  }
}

static bool TryRotateCopyToggle(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(lineIn));
  if (low != "c" && low != "copy")
    return false;
  st.rotateCopyMode = !st.rotateCopyMode;
  log.push_back(st.rotateCopyMode ? "ROTATE — copy mode on (original kept)." : "ROTATE — copy mode off.");
  return true;
}

static void FinishRotateCommand(AppCommandState& st, float bx, float by, float rad, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.rotateCopyMode) {
    DuplicateCadSelectionRotated(st, bx, by, rad);
    st.rotateCopyMode = false;
    st.active = K::None;
    ResetModifyRotateDraft(st);
    if (!st.selectedSurveyPointIndices.empty()) {
      st.pendingSurveyDupIsRotate = true;
      st.pendingRotateCopyBx = bx;
      st.pendingRotateCopyBy = by;
      st.pendingRotateCopyRad = rad;
      st.copySurveyDupModalOpen = true;
      st.copySurveyDupModalOpenRequested = true;
      log.push_back("ROTATE COPY — CAD duplicated; choose survey ID policy.");
    } else {
      log.push_back("ROTATE COPY complete.");
    }
  } else {
    ApplyRotationToSelection(st, bx, by, rad);
    st.active = K::None;
    ResetModifyRotateDraft(st);
    log.push_back("ROTATE complete.");
  }
}

bool HandleRotateText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using RP = AppCommandState::RotatePhase;
  constexpr float kDegToRad = 0.01745329251994329577f;

  if (st.rotatePhase == RP::NeedBase) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateBaseX = px;
    st.rotateBaseY = py;
    st.rotatePhase = RP::NeedAngleOrReference;
    log.push_back("ROTATE — ° clockwise from north (decimal/DMS), R reference, C copy — click-drag preview.");
    return true;
  }

  if (st.rotatePhase == RP::NeedAngleOrReference) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "r" || low == "ref" || low == "reference") {
      st.rotatePhase = RP::Ref_WaitP1;
      log.push_back("Reference — first point:");
      return true;
    }
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(line, &deg))
      return false;
    // Clockwise-from-north degrees → internal CCW-positive rotation used by RotateAroundBase.
    FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, -deg * kDegToRad, log);
    return true;
  }

  if (st.rotatePhase == RP::Ref_WaitP1) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateRefX1 = px;
    st.rotateRefY1 = py;
    st.rotatePhase = RP::Ref_WaitP2;
    log.push_back("Reference — second point:");
    return true;
  }

  if (st.rotatePhase == RP::Ref_WaitP2) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateRefX2 = px;
    st.rotateRefY2 = py;
    st.rotatePhase = RP::AfterReference_WaitAngleOrP;
    log.push_back(
        "Enter new bearing from north ° (decimal/DMS — matches properties), or P for two-point line (C toggles copy).");
    return true;
  }

  if (st.rotatePhase == RP::AfterReference_WaitAngleOrP) {
    std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "p") {
      st.rotatePhase = RP::AnglePoints_WaitP1;
      log.push_back("Angle — first point:");
      return true;
    }
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(line, &deg))
      return false;
    const float thetaRef =
        std::atan2(st.rotateRefY2 - st.rotateRefY1, st.rotateRefX2 - st.rotateRefX1);
    // Degrees are clockwise-from-north bearing (same as properties), matching atan2(dx,dy) convention.
    const float targetMath = MathAngleRadFromBearingCwNorthDeg(deg);
    const float delta = NormalizeAngleRadMinusPiToPi(targetMath - thetaRef);
    FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    return true;
  }

  if (st.rotatePhase == RP::AnglePoints_WaitP1) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateAnglePt1X = px;
    st.rotateAnglePt1Y = py;
    st.rotatePhase = RP::AnglePoints_WaitP2;
    log.push_back("Angle — second point:");
    return true;
  }

  if (st.rotatePhase == RP::AnglePoints_WaitP2) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    const float delta = RotateDeltaFromReferenceAndNewSegment(st.rotateRefX1, st.rotateRefY1, st.rotateRefX2,
                                                               st.rotateRefY2, st.rotateAnglePt1X,
                                                               st.rotateAnglePt1Y, px, py);
    FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    return true;
  }

  return false;
}

static void ComputeArcSweepRad(double ox, double oy, double ax, double ay, double bx, double by, double cx,
                               double cy, double* startRad, double* sweepRad) {
  constexpr double twopi = 6.28318530717958647692;
  auto normPos = [](double x) {
    double r = std::fmod(x, twopi);
    if (r < 0)
      r += twopi;
    return r;
  };
  const double ta = std::atan2(ay - oy, ax - ox);
  const double tb = std::atan2(by - oy, bx - ox);
  const double tc = std::atan2(cy - oy, cx - ox);
  const double arc_ab = normPos(tb - ta);
  const double arc_ac = normPos(tc - ta);
  const bool useCcw = arc_ab <= arc_ac + 1e-10;
  double sweep = useCcw ? arc_ac : arc_ac - twopi;
  if (std::fabs(sweep) < 1e-12)
    sweep = twopi;
  *startRad = ta;
  *sweepRad = sweep;
}

static void CommitArcThreePoints(AppCommandState& st, float ax, float ay, float bx, float by, float cx, float cy,
                                 std::vector<std::string>& log) {
  float ox = 0.f, oy = 0.f, r = 0.f;
  if (!ComputeCircumcircle(ax, ay, bx, by, cx, cy, &ox, &oy, &r) || r < 1e-8f) {
    log.push_back("ARC — points are collinear.");
    st.active = AppCommandState::Kind::None;
    ResetArcDraft(st);
    return;
  }
  double sr = 0.;
  double sw = 0.;
  ComputeArcSweepRad(ox, oy, ax, ay, bx, by, cx, cy, &sr, &sw);
  CadArc arc{};
  arc.cx = ox;
  arc.cy = oy;
  arc.r = r;
  arc.startRad = static_cast<float>(sr);
  arc.sweepRad = static_cast<float>(sw);
  st.userArcs.push_back(arc);
  st.userArcAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetArcDraft(st);
  log.push_back("ARC complete.");
}

static void CommitDimAlignedAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  const float x1 = st.dimE1x, y1 = st.dimE1y;
  const float x2 = st.dimE2x, y2 = st.dimE2y;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1e-8f) {
    log.push_back("DIMALIGNED — extension points coincide.");
    return;
  }
  vx /= len;
  vy /= len;
  const float t1 = (x1 - lx) * vx + (y1 - ly) * vy;
  const float t2 = (x2 - lx) * vx + (y2 - ly) * vy;
  const float sx1 = lx + vx * t1;
  const float sy1 = ly + vy * t1;
  const float sx2 = lx + vx * t2;
  const float sy2 = ly + vy * t2;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float n0x = -vy;
  const float n0y = vx;
  const float dmx = 0.5f * (sx1 + sx2);
  const float dmy = 0.5f * (sy1 + sy2);
  const float dOff = (dmx - cmx) * n0x + (dmy - cmy) * n0y;
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(len));
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::DimAligned;
  ann.dimExt1X = x1;
  ann.dimExt1Y = y1;
  ann.dimExt2X = x2;
  ann.dimExt2Y = y2;
  ann.dimSignedOffset = dOff;
  ann.plottedHeightInches = st.defaultPlottedTextHeightInches * 0.85f;
  ann.rotationRad = std::atan2(vy, vx);
  ann.text = buf;
  const float hWorld = CadAnnotationHeightWorld(ann, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &ann.insX, &ann.insY);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  st.cadAnnotations.push_back(std::move(ann));
  st.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetDimDraft(st);
  log.push_back("DIMALIGNED complete.");
}

static void CommitDimLinearAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  CadDimLinearUpdateDraftOrientation(st, lx, ly);
  const float x1 = st.dimE1x, y1 = st.dimE1y;
  const float x2 = st.dimE2x, y2 = st.dimE2y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const bool vert = st.dimLinearDraftVertical;
  const float meas = vert ? std::fabs(y2 - y1) : std::fabs(x2 - x1);
  if (meas < 1e-8f) {
    log.push_back(vert ? "DIMLINEAR — extension points have the same Y (zero vertical span)."
                       : "DIMLINEAR — extension points have the same X (zero horizontal span).");
    return;
  }
  float dmx = cmx;
  float dmy = cmy;
  float n0x = 0.f;
  float n0y = 1.f;
  float dOff = 0.f;
  if (!vert) {
    dmy = ly;
    dOff = dmy - cmy;
  } else {
    dmx = lx;
    dOff = dmx - cmx;
    n0x = 1.f;
    n0y = 0.f;
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(meas));
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::DimLinear;
  ann.dimExt1X = x1;
  ann.dimExt1Y = y1;
  ann.dimExt2X = x2;
  ann.dimExt2Y = y2;
  ann.dimSignedOffset = dOff;
  ann.dimLinearVertical = vert;
  ann.plottedHeightInches = st.defaultPlottedTextHeightInches * 0.85f;
  float tx = 0.f, ty = 0.f;
  if (!vert) {
    tx = (x2 >= x1) ? 1.f : -1.f;
    ty = 0.f;
  } else {
    tx = 0.f;
    ty = (y2 >= y1) ? 1.f : -1.f;
  }
  ann.rotationRad = std::atan2(ty, tx);
  ann.text = buf;
  const float hWorld = CadAnnotationHeightWorld(ann, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &ann.insX, &ann.insY);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  st.cadAnnotations.push_back(std::move(ann));
  st.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetDimDraft(st);
  log.push_back("DIMLINEAR complete.");
}

static void CommitIdPointAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  double wx = 0.;
  double wy = 0.;
  CadCoord::WorldFromLocal(st, lx, ly, &wx, &wy);
  char buf[192];
  std::snprintf(buf, sizeof(buf), "ID — UCS (World)  X = %.6f  Y = %.6f  Z = 0.000000", wx, wy);
  log.push_back(buf);
  st.active = AppCommandState::Kind::None;
}

static void CommitSurveyInverseSecondPoint(AppCommandState& st, float x2, float y2, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SIP = AppCommandState::SurveyInversePhase;
  const float de = x2 - st.surveyInverseFromX;
  const float dn = y2 - st.surveyInverseFromY;
  const float horiz = std::hypot(de, dn);
  if (horiz < 1e-10f) {
    log.push_back("INVERSE — horizontal distance is zero; pick a different second point.");
    return;
  }
  const float theta = std::atan2(dn, de);
  const float brg = BearingCwNorthDegFromMathAngleRad(theta);
  const std::string brgDms = CadFormatBearingCwNorthDegMinSec(brg);
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "INVERSE — ΔE = %.6f  ΔN = %.6f  horiz dist = %.6f  bearing = %s clockwise from north (%.4f° "
                "decimal).",
                static_cast<double>(de), static_cast<double>(dn), static_cast<double>(horiz), brgDms.c_str(),
                static_cast<double>(brg));
  log.push_back(buf);
  st.active = K::None;
  st.surveyInversePhase = SIP::WaitFrom;
}

namespace OffsetCmd {

static void ResetOffsetDraft(AppCommandState& st) {
  st.offsetEntityValid = false;
  st.offsetEntity = {};
  st.offsetTypedDistance = 0.f;
  st.offsetPhase = AppCommandState::OffsetPhase::WaitSelectEntity;
  st.offsetHoverHighlightValid = false;
  st.offsetHoverEntity = {};
}

static void ClosestPointOnSegment(float ax, float ay, float bx, float by, float px, float py, float* qx,
                                  float* qy) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *qx = ax;
    *qy = ay;
    return;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  *qx = ax + t * vx;
  *qy = ay + t * vy;
}

static bool LineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                 float* ox, float* oy) {
  const float rx = bx - ax, ry = by - ay;
  const float sx = dx - cx, sy = dy - cy;
  const float det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-12f * std::max(1.f, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const float t = ((cx - ax) * sy - (cy - ay) * sx) / det;
  *ox = ax + t * rx;
  *oy = ay + t * ry;
  return true;
}

static void UnitLeftNormal(float ax, float ay, float bx, float by, float* nx, float* ny) {
  float vx = bx - ax;
  float vy = by - ay;
  const float len = std::hypot(vx, vy);
  if (len < 1e-12f) {
    *nx = 0.f;
    *ny = 1.f;
    return;
  }
  vx /= len;
  vy /= len;
  *nx = -vy;
  *ny = vx;
}

static float SignedSideLine(float ax, float ay, float bx, float by, float px, float py) {
  float qx = 0.f, qy = 0.f;
  ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
  float nx = 0.f, ny = 0.f;
  UnitLeftNormal(ax, ay, bx, by, &nx, &ny);
  return (px - qx) * nx + (py - qy) * ny;
}

static float SignedSideCircle(float cx, float cy, float r, float px, float py) {
  const float d = std::hypot(px - cx, py - cy);
  return d - r;
}

static bool CommitOffsetLine(AppCommandState& st, int lineIx, float signedD, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(lineIx) * 6;
  if (k + 5 >= st.userLinesFlat.size())
    return false;
  const float x0 = st.userLinesFlat[k];
  const float y0 = st.userLinesFlat[k + 1];
  const float x1 = st.userLinesFlat[k + 3];
  const float y1 = st.userLinesFlat[k + 4];
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  if (std::hypot(dx, dy) < 1e-8f) {
    log.push_back("OFFSET — zero-length line.");
    return false;
  }
  float nx = 0.f, ny = 0.f;
  UnitLeftNormal(x0, y0, x1, y1, &nx, &ny);
  const float ox0 = x0 + nx * signedD;
  const float oy0 = y0 + ny * signedD;
  const float ox1 = x1 + nx * signedD;
  const float oy1 = y1 + ny * signedD;
  st.userLinesFlat.push_back(ox0);
  st.userLinesFlat.push_back(oy0);
  st.userLinesFlat.push_back(0.f);
  st.userLinesFlat.push_back(ox1);
  st.userLinesFlat.push_back(oy1);
  st.userLinesFlat.push_back(0.f);
  if (static_cast<size_t>(lineIx) < st.userLineAttrs.size())
    st.userLineAttrs.push_back(st.userLineAttrs[static_cast<size_t>(lineIx)]);
  else
    st.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetCircle(AppCommandState& st, int ci, float signedD, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(ci) * 3;
  if (k + 2 >= st.userCirclesCxCyR.size())
    return false;
  const float cx = st.userCirclesCxCyR[k];
  const float cy = st.userCirclesCxCyR[k + 1];
  const float r = st.userCirclesCxCyR[k + 2];
  const float nr = r + signedD;
  if (nr <= 1e-6f) {
    log.push_back("OFFSET — resulting circle radius too small.");
    return false;
  }
  st.userCirclesCxCyR.push_back(cx);
  st.userCirclesCxCyR.push_back(cy);
  st.userCirclesCxCyR.push_back(nr);
  if (static_cast<size_t>(ci) < st.userCircleAttrs.size())
    st.userCircleAttrs.push_back(st.userCircleAttrs[static_cast<size_t>(ci)]);
  else
    st.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetArc(AppCommandState& st, int ai, float signedD, std::vector<std::string>& log) {
  if (ai < 0 || static_cast<size_t>(ai) >= st.userArcs.size())
    return false;
  const CadArc& a = st.userArcs[static_cast<size_t>(ai)];
  const float nr = a.r + signedD;
  if (nr <= 1e-6f) {
    log.push_back("OFFSET — resulting arc radius too small.");
    return false;
  }
  CadArc o = a;
  o.r = nr;
  st.userArcs.push_back(o);
  if (static_cast<size_t>(ai) < st.userArcAttrs.size())
    st.userArcAttrs.push_back(st.userArcAttrs[static_cast<size_t>(ai)]);
  else
    st.userArcAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetEllipse(AppCommandState& st, int ei, float signedD, std::vector<std::string>& log) {
  if (ei < 0 || static_cast<size_t>(ei) >= st.userEllipses.size())
    return false;
  const CadEllipse& e = st.userEllipses[static_cast<size_t>(ei)];
  const float ma = std::hypot(e.majVx, e.majVy);
  if (ma < 1e-8f) {
    log.push_back("OFFSET — degenerate ellipse.");
    return false;
  }
  const float f = (ma + signedD) / ma;
  if (ma + signedD <= 1e-6f) {
    log.push_back("OFFSET — resulting ellipse too small.");
    return false;
  }
  CadEllipse o = e;
  o.majVx *= f;
  o.majVy *= f;
  st.userEllipses.push_back(o);
  if (static_cast<size_t>(ei) < st.userEllAttrs.size())
    st.userEllAttrs.push_back(st.userEllAttrs[static_cast<size_t>(ei)]);
  else
    st.userEllAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetPolyline(AppCommandState& st, int pi, float signedD, std::vector<std::string>& log) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return false;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const int nv = v1 - v0;
  if (nv < 2) {
    log.push_back("OFFSET — polyline needs at least two vertices.");
    return false;
  }
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];

  std::vector<std::pair<float, float>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i) {
    v.push_back({st.userPolylineVerts[static_cast<size_t>(i * 3)], st.userPolylineVerts[static_cast<size_t>(i * 3 + 1)]});
  }

  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  if (nEdges < 1) {
    log.push_back("OFFSET — not enough edges.");
    return false;
  }

  std::vector<std::pair<float, float>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    const float ax = v[static_cast<size_t>(ia)].first;
    const float ay = v[static_cast<size_t>(ia)].second;
    const float bx = v[static_cast<size_t>(ib)].first;
    const float by = v[static_cast<size_t>(ib)].second;
    float nx = 0.f, ny = 0.f;
    UnitLeftNormal(ax, ay, bx, by, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {ax + nx * signedD, ay + ny * signedD};
    pb[static_cast<size_t>(ei)] = {bx + nx * signedD, by + ny * signedD};
  }

  std::vector<std::pair<float, float>> out;
  if (!closed) {
    if (nEdges == 1) {
      out.push_back(pa[0]);
      out.push_back(pb[0]);
    } else {
      out.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        const auto& a0 = pa[static_cast<size_t>(ei)];
        const auto& b0 = pb[static_cast<size_t>(ei)];
        const auto& a1 = pa[static_cast<size_t>(ei + 1)];
        const auto& b1 = pb[static_cast<size_t>(ei + 1)];
        float ix = 0.f, iy = 0.f;
        if (LineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                  &ix, &iy))
          out.push_back({ix, iy});
        else {
          const float mx = 0.5f * (b0.first + a1.first);
          const float my = 0.5f * (b0.second + a1.second);
          out.push_back({mx, my});
        }
      }
      out.push_back(pb[static_cast<size_t>(nEdges - 1)]);
    }
  } else {
    out.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      const auto& a0 = pa[static_cast<size_t>(ei)];
      const auto& b0 = pb[static_cast<size_t>(ei)];
      const auto& a1 = pa[static_cast<size_t>(en)];
      const auto& b1 = pb[static_cast<size_t>(en)];
      float ix = 0.f, iy = 0.f;
      if (LineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second, &ix,
                               &iy))
        out[static_cast<size_t>(ei)] = {ix, iy};
      else
        out[static_cast<size_t>(ei)] = {0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)};
    }
  }

  if (out.size() < 2) {
    log.push_back("OFFSET — could not build offset polyline.");
    return false;
  }

  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  const int baseVert = st.userPolylineOffsets.back();
  for (const auto& p : out) {
    st.userPolylineVerts.push_back(p.first);
    st.userPolylineVerts.push_back(p.second);
    st.userPolylineVerts.push_back(0.f);
  }
  st.userPolylineOffsets.push_back(baseVert + static_cast<int>(out.size()));
  st.userPolylineClosed.push_back(closed ? 1u : 0u);
  if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
    st.userPolylineAttrs.push_back(st.userPolylineAttrs[static_cast<size_t>(pi)]);
  else
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetSigned(AppCommandState& st, float signedD, std::vector<std::string>& log) {
  if (!st.offsetEntityValid)
    return false;
  const SelectedEntity& e = st.offsetEntity;
  bool ok = false;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg:
    ok = CommitOffsetLine(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Circle:
    ok = CommitOffsetCircle(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Arc:
    ok = CommitOffsetArc(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Ellipse:
    ok = CommitOffsetEllipse(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Polyline:
    ok = CommitOffsetPolyline(st, e.index, signedD, log);
    break;
  default:
    log.push_back("OFFSET — unsupported entity type.");
    return false;
  }
  if (ok)
    log.push_back("OFFSET — created parallel / concentric geometry.");
  return ok;
}

static void FinishOffsetAndIdle(AppCommandState& st, std::vector<std::string>& log) {
  (void)log;
  ResetOffsetDraft(st);
  st.active = AppCommandState::Kind::None;
}

static void HandleOffsetThroughPick(AppCommandState& st, float px, float py, std::vector<std::string>& log) {
  if (!st.offsetEntityValid)
    return;
  const SelectedEntity& e = st.offsetEntity;
  float signedD = 0.f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return;
    const float x0 = st.userLinesFlat[k];
    const float y0 = st.userLinesFlat[k + 1];
    const float x1 = st.userLinesFlat[k + 3];
    const float y1 = st.userLinesFlat[k + 4];
    signedD = SignedSideLine(x0, y0, x1, y1, px, py);
    break;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 3;
    if (k + 2 >= st.userCirclesCxCyR.size())
      return;
    const float cx = st.userCirclesCxCyR[k];
    const float cy = st.userCirclesCxCyR[k + 1];
    const float r = st.userCirclesCxCyR[k + 2];
    signedD = SignedSideCircle(cx, cy, r, px, py);
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
      return;
    const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
    signedD = SignedSideCircle(a.cx, a.cy, a.r, px, py);
    break;
  }
  case SelectedEntity::Type::Polyline:
    log.push_back("OFFSET — polyline: type a distance, then pick a side (through-click not supported).");
    return;
  case SelectedEntity::Type::Ellipse:
    log.push_back("OFFSET — ellipse: type a distance, then pick a side (through-click not supported).");
    return;
  default:
    return;
  }
  if (std::fabs(signedD) < 1e-8f) {
    log.push_back("OFFSET — through point on original; pick farther away.");
    return;
  }
  if (CommitOffsetSigned(st, signedD, log))
    FinishOffsetAndIdle(st, log);
}

static void HandleOffsetSidePick(AppCommandState& st, float px, float py, std::vector<std::string>& log) {
  if (!st.offsetEntityValid || st.offsetTypedDistance <= 0.f)
    return;
  const float d = st.offsetTypedDistance;
  const SelectedEntity& e = st.offsetEntity;
  float sgn = 1.f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return;
    const float sd = SignedSideLine(st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                                    st.userLinesFlat[k + 4], px, py);
    sgn = sd >= 0.f ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 3;
    if (k + 2 >= st.userCirclesCxCyR.size())
      return;
    const float cx = st.userCirclesCxCyR[k];
    const float cy = st.userCirclesCxCyR[k + 1];
    const float r = st.userCirclesCxCyR[k + 2];
    const float side = SignedSideCircle(cx, cy, r, px, py);
    sgn = side >= 0.f ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
      return;
    const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
    const float side = SignedSideCircle(a.cx, a.cy, a.r, px, py);
    sgn = side >= 0.f ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Ellipse:
  case SelectedEntity::Type::Polyline: {
    if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi >= 0 && static_cast<size_t>(pi + 1) < st.userPolylineOffsets.size()) {
        const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        float best = 1e30f;
        float bestS = 1.f;
        for (int vi = v0; vi + 1 < v1; ++vi) {
          const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
          const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
          const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
          const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
          float qx = 0.f, qy = 0.f;
          ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
          const float sd = SignedSideLine(ax, ay, bx, by, px, py);
          const float dx = px - qx;
          const float dy = py - qy;
          const float dist2 = dx * dx + dy * dy;
          if (dist2 < best) {
            best = dist2;
            bestS = sd >= 0.f ? 1.f : -1.f;
          }
        }
        sgn = bestS;
      }
    } else if (e.index >= 0 && static_cast<size_t>(e.index) < st.userEllipses.size()) {
      const CadEllipse& el = st.userEllipses[static_cast<size_t>(e.index)];
      const float ma = std::hypot(el.majVx, el.majVy);
      if (ma >= 1e-8f) {
        const float ux = el.majVx / ma;
        const float uy = el.majVy / ma;
        const float pxn = -uy;
        const float pyn = ux;
        const float mb = ma * el.ratio;
        constexpr float twopi = 6.28318530718f;
        float best = 1e30f;
        float bx = el.cx, by = el.cy;
        for (int i = 0; i <= 48; ++i) {
          const float ang = twopi * static_cast<float>(i) / 48.f;
          const float c0 = std::cos(ang);
          const float s0 = std::sin(ang);
          const float ex = el.cx + ux * (ma * c0) + pxn * (mb * s0);
          const float ey = el.cy + uy * (ma * c0) + pyn * (mb * s0);
          const float dx = px - ex;
          const float dy = py - ey;
          const float dist2 = dx * dx + dy * dy;
          if (dist2 < best) {
            best = dist2;
            bx = ex;
            by = ey;
          }
        }
        const float ox = bx - el.cx;
        const float oy = by - el.cy;
        const float inX = px - el.cx;
        const float inY = py - el.cy;
        sgn = (inX * ox + inY * oy) >= 0.f ? 1.f : -1.f;
      }
    }
    break;
  }
  default:
    return;
  }
  const float signedD = d * sgn;
  if (CommitOffsetSigned(st, signedD, log))
    FinishOffsetAndIdle(st, log);
}

static void HandleOffsetViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  using OP = AppCommandState::OffsetPhase;
  switch (st.offsetPhase) {
  case OP::WaitSelectEntity: {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back("OFFSET — nothing under cursor; try again.");
      return;
    }
    st.offsetEntity = hit;
    st.offsetEntityValid = true;
    st.offsetPhase = OP::WaitDistanceOrThrough;
    st.offsetTypedDistance = 0.f;
    log.push_back("OFFSET — distance (number) + pick side, or click through point for line / circle / arc.");
    return;
  }
  case OP::WaitDistanceOrThrough:
    HandleOffsetThroughPick(st, wx, wy, log);
    return;
  case OP::WaitSidePick:
    HandleOffsetSidePick(st, wx, wy, log);
    return;
  }
}

} // namespace OffsetCmd

void SubmitViewportPickImpl(AppCommandState& st, float wx, float wy, std::vector<std::string>& log,
                             bool windowSelectionSubtract, bool fenceLeftToRightWindowMode) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using RP = AppCommandState::RotatePhase;
  using SP = AppCommandState::ScalePhase;

  auto finishBox = [&]() {
    const bool inclSurvey = (st.active == AppCommandState::Kind::None || st.active == K::Move ||
                             st.active == K::Copy || st.active == K::Rotate || st.active == K::Scale);
    ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, wx, wy, windowSelectionSubtract,
                             fenceLeftToRightWindowMode, inclSurvey);
    st.selBoxWaitingSecond = false;
    log.push_back("Fence — CAD " + std::to_string(st.selection.size()) + ", survey " +
                  std::to_string(st.selectedSurveyPointIndices.size()) +
                  (fenceLeftToRightWindowMode ? " (window)." : " (crossing)."));
  };

  if (st.active == K::Line) {
    using LP = AppCommandState::LinePhase;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (st.linePhase == LP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing) {
      log.push_back("Finish bearing entry on the command line (blank Enter cancels) before viewport picks.");
      return;
    }
    if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1) {
      st.segmentPickRefX1 = wx;
      st.segmentPickRefY1 = wy;
      st.segmentAnglePickPhase = SAP::WaitP2;
      log.push_back("Bearing pick — second reference point:");
      return;
    }
    if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2) {
      const float dx = wx - st.segmentPickRefX1;
      const float dy = wy - st.segmentPickRefY1;
      if (std::hypot(dx, dy) < 1e-8f)
        log.push_back("Bearing pick — points coincide; pick again.");
      else {
        const float theta = std::atan2(dy, dx);
        st.segmentPickDraftBearingDeg = BearingCwNorthDegFromMathAngleRad(theta);
        st.segmentAnglePickPhase = SAP::WaitAdjustOrCommit;
        log.push_back(
            "Bearing from picks — Enter locks as-is; type +90 / -45 (° CW from N) to adjust and lock (one line).");
      }
      return;
    }
    if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      log.push_back("Bearing pick — press Enter to lock (or type +90 / -45); viewport click ignored in this step.");
      return;
    }
    if (st.linePhase == LP::NeedNextPoint && st.segmentAngleLockActive)
      ApplySegmentAngleLockToWorldPick(st.anchorX, st.anchorY, st.segmentLockUx, st.segmentLockUy, &wx, &wy, false);
    else if (st.linePhase == LP::NeedNextPoint && st.orthoMode) {
      const float dx = wx - st.anchorX;
      const float dy = wy - st.anchorY;
      if (std::fabs(dx) >= std::fabs(dy))
        wy = st.anchorY;
      else
        wx = st.anchorX;
    }
    SubmitLineVertex(st, wx, wy, log);
    return;
  }

  if (st.active == K::Polyline) {
    using PP = AppCommandState::PolylinePhase;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing) {
      log.push_back("Finish bearing entry on the command line (blank Enter cancels) before viewport picks.");
      return;
    }
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1) {
      st.segmentPickRefX1 = wx;
      st.segmentPickRefY1 = wy;
      st.segmentAnglePickPhase = SAP::WaitP2;
      log.push_back("Bearing pick — second reference point:");
      return;
    }
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2) {
      const float dx = wx - st.segmentPickRefX1;
      const float dy = wy - st.segmentPickRefY1;
      if (std::hypot(dx, dy) < 1e-8f)
        log.push_back("Bearing pick — points coincide; pick again.");
      else {
        const float theta = std::atan2(dy, dx);
        st.segmentPickDraftBearingDeg = BearingCwNorthDegFromMathAngleRad(theta);
        st.segmentAnglePickPhase = SAP::WaitAdjustOrCommit;
        log.push_back(
            "Bearing from picks — Enter locks as-is; type +90 / -45 (° CW from N) to adjust and lock (one line).");
      }
      return;
    }
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      log.push_back("Bearing pick — press Enter to lock (or type +90 / -45); viewport click ignored in this step.");
      return;
    }
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleLockActive)
      ApplySegmentAngleLockToWorldPick(st.anchorX, st.anchorY, st.segmentLockUx, st.segmentLockUy, &wx, &wy, false);
    else if (st.polylinePhase == PP::NeedNextPoint && st.orthoMode) {
      const float dx = wx - st.anchorX;
      const float dy = wy - st.anchorY;
      if (std::fabs(dx) >= std::fabs(dy))
        wy = st.anchorY;
      else
        wx = st.anchorX;
    }
    SubmitPolylineVertex(st, wx, wy, log);
    return;
  }

  if (st.active == K::Arc) {
    using AP = AppCommandState::ArcPhase;
    switch (st.arcPhase) {
    case AP::WaitStart:
      st.arcAx = wx;
      st.arcAy = wy;
      st.arcPhase = AP::WaitMid;
      log.push_back("ARC — pick middle point on arc:");
      break;
    case AP::WaitMid:
      st.arcBx = wx;
      st.arcBy = wy;
      st.arcPhase = AP::WaitEnd;
      log.push_back("ARC — pick end point:");
      break;
    case AP::WaitEnd:
      CommitArcThreePoints(st, st.arcAx, st.arcAy, st.arcBx, st.arcBy, wx, wy, log);
      break;
    }
    return;
  }

  if (st.active == K::Ellipse) {
    using EP = AppCommandState::EllipsePhase;
    switch (st.ellPhase) {
    case EP::WaitCenter:
      st.ellCx = wx;
      st.ellCy = wy;
      st.ellPhase = EP::WaitMajorEnd;
      log.push_back("ELLIPSE — major axis endpoint:");
      break;
    case EP::WaitMajorEnd:
      st.ellMajEx = wx;
      st.ellMajEy = wy;
      st.ellPhase = EP::WaitRatio;
      log.push_back("ELLIPSE — type minor/major ratio (0-1], or Enter for 0.5:");
      break;
    case EP::WaitRatio:
      log.push_back("ELLIPSE — type ratio on command line (middle mouse pick ignored here).");
      break;
    }
    return;
  }

  if (st.active == K::Text) {
    using TP = AppCommandState::TextCmdPhase;
    if (st.textPhase == TP::WaitInsertion) {
      st.textInsX = wx;
      st.textInsY = wy;
      st.textPhase = TP::WaitHeight;
      log.push_back("TEXT — height (Enter = plot-scale default):");
    } else
      log.push_back("TEXT — continue on command line (height / rotation / text).");
    return;
  }

  if (st.active == K::Mtext) {
    using MPt = AppCommandState::MtextPhase;
    switch (st.mtextPhase) {
    case MPt::WaitCorner1:
      st.mtxtX1 = wx;
      st.mtxtY1 = wy;
      st.mtextPhase = MPt::WaitCorner2;
      log.push_back("MTEXT — opposite corner:");
      break;
    case MPt::WaitCorner2:
      st.mtxtX2 = wx;
      st.mtxtY2 = wy;
      st.mtextPhase = MPt::WaitString;
      OpenMtextRichEditorForPlacement(st, &log);
      break;
    case MPt::WaitString:
      break;
    }
    return;
  }

  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    using DP = AppCommandState::DimPhase;
    const bool linear = st.active == K::DimLinear;
    switch (st.dimPhase) {
    case DP::WaitExt1:
      st.dimE1x = wx;
      st.dimE1y = wy;
      st.dimPhase = DP::WaitExt2;
      log.push_back(std::string(linear ? "DIMLINEAR" : "DIMALIGNED") + " — second extension point:");
      break;
    case DP::WaitExt2:
      st.dimE2x = wx;
      st.dimE2y = wy;
      st.dimPhase = DP::WaitDimLinePt;
      if (linear) {
        st.dimLinearOrientUserLock = false;
        CadDimLinearUpdateDraftOrientation(st, wx, wy);
        log.push_back(
            "DIMLINEAR — pick dimension line position (horizontal vs vertical follows cursor; H / V to lock); type X,Y or @dx,dy from chord mid.");
      } else
        log.push_back("DIMALIGNED — pick dimension line position (offset from measured segment).");
      break;
    case DP::WaitDimLinePt:
      if (linear)
        CommitDimLinearAt(st, wx, wy, log);
      else
        CommitDimAlignedAt(st, wx, wy, log);
      break;
    }
    return;
  }

  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    switch (st.dimAngularPhase) {
    case DAP::WaitVertex:
      st.dimAngVx = wx;
      st.dimAngVy = wy;
      st.dimAngularPhase = DAP::WaitRay1;
      log.push_back("DIMANGULAR — first ray point (on first leg):");
      break;
    case DAP::WaitRay1:
      st.dimE1x = wx;
      st.dimE1y = wy;
      st.dimAngularPhase = DAP::WaitRay2;
      log.push_back("DIMANGULAR — second ray point (on second leg):");
      break;
    case DAP::WaitRay2:
      st.dimE2x = wx;
      st.dimE2y = wy;
      st.dimAngularPhase = DAP::WaitArc;
      log.push_back("DIMANGULAR — pick arc / label side (radius along angle bisector); type X,Y or @dx,dy from vertex.");
      break;
    case DAP::WaitArc:
      CommitDimAngularAt(st, wx, wy, log);
      break;
    }
    return;
  }

  if (st.active == K::IdPoint) {
    CommitIdPointAt(st, wx, wy, log);
    return;
  }

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (st.surveyInversePhase == SIP::WaitFrom) {
      st.surveyInverseFromX = wx;
      st.surveyInverseFromY = wy;
      st.surveyInversePhase = SIP::WaitTo;
      log.push_back("INVERSE — second point (pick or type X,Y; @dx,dy from first):");
      return;
    }
    CommitSurveyInverseSecondPoint(st, wx, wy, log);
    return;
  }

  if (st.active == K::Offset) {
    OffsetCmd::HandleOffsetViewportPick(st, wx, wy, log);
    return;
  }

  if (st.active == K::Circle) {
    switch (st.circlePhase) {
    case AppCommandState::CirclePhase::WaitCenterOrMode:
      st.circleCx = wx;
      st.circleCy = wy;
      st.circlePhase = AppCommandState::CirclePhase::WaitRadius;
      log.push_back("Center set — specify radius (click near edge), type radius, or D + diameter.");
      break;
    case AppCommandState::CirclePhase::WaitRadius: {
      const float dx = wx - st.circleCx;
      const float dy = wy - st.circleCy;
      const float r = std::sqrt(dx * dx + dy * dy);
      CommitCircle(st, st.circleCx, st.circleCy, r, log);
      break;
    }
    case AppCommandState::CirclePhase::ThreeP_WaitP1:
      st.c3p1x = wx;
      st.c3p1y = wy;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP2;
      log.push_back("Second point of circle:");
      break;
    case AppCommandState::CirclePhase::ThreeP_WaitP2:
      st.c3p2x = wx;
      st.c3p2y = wy;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP3;
      log.push_back("Third point of circle:");
      break;
    case AppCommandState::CirclePhase::ThreeP_WaitP3: {
      float ox = 0.f;
      float oy = 0.f;
      float r = 0.f;
      if (!ComputeCircumcircle(st.c3p1x, st.c3p1y, st.c3p2x, st.c3p2y, wx, wy, &ox, &oy, &r))
        log.push_back("Points are collinear — pick a non-collinear third point.");
      else
        CommitCircle(st, ox, oy, r, log);
      break;
    }
    }
    return;
  }

  if (st.active == K::Zoom) {
    if (st.selBoxWaitingSecond) {
      st.pendingZoomMnX = std::min(st.selBoxAnchorX, wx);
      st.pendingZoomMxX = std::max(st.selBoxAnchorX, wx);
      st.pendingZoomMnY = std::min(st.selBoxAnchorY, wy);
      st.pendingZoomMxY = std::max(st.selBoxAnchorY, wy);
      st.selBoxWaitingSecond = false;
      st.pendingZoomWindow = true;
      st.active = K::None;
    }
    return;
  }

  if (st.active == K::Join) {
    if (st.selBoxWaitingSecond) {
      ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, wx, wy, windowSelectionSubtract,
                               fenceLeftToRightWindowMode, false);
      st.selBoxWaitingSecond = false;
      if (st.selection.empty())
        log.push_back("Nothing selected — pick two corners again.");
      else {
        ExecuteJoinSelection(st, log);
        st.active = K::None;
        ResetModifyRotateDraft(st);
      }
    }
    return;
  }

  if (st.active == K::Delete) {
    if (st.selBoxWaitingSecond) {
      ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, wx, wy, windowSelectionSubtract,
                               fenceLeftToRightWindowMode, false);
      st.selBoxWaitingSecond = false;
      if (st.selection.empty())
        log.push_back("Nothing selected — pick two corners again.");
      else {
        ExecuteDeleteSelection(st, log);
        st.active = K::None;
        ResetModifyRotateDraft(st);
      }
    }
    return;
  }

  if (st.active == K::Move || st.active == K::Copy) {
    if (st.modifyPhase == MP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back(st.active == K::Copy ? "COPY — base point:" : "MOVE — base point:");
        }
      }
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = wx;
      st.modifyBaseY = wy;
      st.modifyPhase = MP::NeedDestination;
      log.push_back(st.active == K::Copy ? "COPY — destination:" : "MOVE — destination:");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      const bool wasCopy = (st.active == K::Copy);
      const float dx = wx - st.modifyBaseX;
      const float dy = wy - st.modifyBaseY;
      if (wasCopy)
        FinalizeCopyTranslation(st, dx, dy, log);
      else {
        ApplyTranslationToSelection(st, dx, dy);
        st.active = K::None;
        ResetModifyRotateDraft(st);
        log.push_back("MOVE complete.");
      }
    }
    return;
  }

  if (st.active == K::Scale) {
    if (st.modifyPhase == MP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back("SCALE — base point:");
        }
      }
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = wx;
      st.modifyBaseY = wy;
      st.scaleRefDist = ComputeScaleReferenceDistance(st, wx, wy);
      st.scalePhase = SP::FactorPick;
      st.modifyPhase = MP::NeedDestination;
      log.push_back(
          "SCALE — pick second point or type factor (>0), or R / REFERENCE on command line for two-point reference "
          "length.");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      switch (st.scalePhase) {
      case SP::FactorPick: {
        const float d = std::hypot(wx - st.modifyBaseX, wy - st.modifyBaseY);
        const float s = std::max(d / std::max(st.scaleRefDist, 1e-20f), 1e-6f);
        FinishScaleCommand(st, s, log);
        return;
      }
      case SP::Ref_WaitP1:
        st.scaleRefP1X = wx;
        st.scaleRefP1Y = wy;
        st.scalePhase = SP::Ref_WaitP2;
        log.push_back("SCALE ref — second point of reference length:");
        return;
      case SP::Ref_WaitP2: {
        const float refLen = std::hypot(wx - st.scaleRefP1X, wy - st.scaleRefP1Y);
        if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
          log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
          return;
        }
        st.scaleRefDist = refLen;
        st.scalePhase = SP::NewLength_WaitTypedOrP1;
        log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
        return;
      }
      case SP::NewLength_WaitTypedOrP1:
        st.scaleNewLenP1X = wx;
        st.scaleNewLenP1Y = wy;
        st.scalePhase = SP::NewLength_WaitP2;
        log.push_back("SCALE ref — second point of new length segment:");
        return;
      case SP::NewLength_WaitP2: {
        const float newLen = std::hypot(wx - st.scaleNewLenP1X, wy - st.scaleNewLenP1Y);
        if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
          log.push_back("SCALE ref — new length is too small; pick two distinct points.");
          return;
        }
        FinishScaleCommand(st, newLen / std::max(st.scaleRefDist, 1e-20f), log);
        return;
      }
      }
    }
    return;
  }

  if (st.active == K::Rotate) {
    if (st.rotatePhase == RP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.rotatePhase = RP::NeedBase;
          log.push_back("ROTATE — base point:");
        }
      }
      return;
    }
    if (st.rotatePhase == RP::NeedBase) {
      st.rotateBaseX = wx;
      st.rotateBaseY = wy;
      st.rotatePhase = RP::NeedAngleOrReference;
      log.push_back(
          "ROTATE — ° clockwise from north or R reference or C copy; decimal/DMS or click-drag preview.");
      return;
    }
    if (st.rotatePhase == RP::Ref_WaitP1) {
      st.rotateRefX1 = wx;
      st.rotateRefY1 = wy;
      st.rotatePhase = RP::Ref_WaitP2;
      log.push_back("Reference — second point:");
      return;
    }
    if (st.rotatePhase == RP::Ref_WaitP2) {
      st.rotateRefX2 = wx;
      st.rotateRefY2 = wy;
      st.rotatePhase = RP::AfterReference_WaitAngleOrP;
      log.push_back("Enter new bearing from north ° (matches properties), or P for two-point line.");
      return;
    }
    if (st.rotatePhase == RP::AnglePoints_WaitP1) {
      st.rotateAnglePt1X = wx;
      st.rotateAnglePt1Y = wy;
      st.rotatePhase = RP::AnglePoints_WaitP2;
      log.push_back("Angle — second point:");
      return;
    }
    if (st.rotatePhase == RP::AnglePoints_WaitP2) {
      const float delta =
          RotateDeltaFromReferenceAndNewSegment(st.rotateRefX1, st.rotateRefY1, st.rotateRefX2, st.rotateRefY2,
                                                  st.rotateAnglePt1X, st.rotateAnglePt1Y, wx, wy);
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    }
    return;
  }

  if (st.active == K::None && st.selBoxWaitingSecond)
    finishBox();
}

} // namespace

void StartOffsetCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("OFFSET — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  OffsetCmd::ResetOffsetDraft(st);
  st.active = K::Offset;
  st.selBoxWaitingSecond = false;
  log.push_back("OFFSET — select line, circle, arc, ellipse, or polyline. ESC cancels.");
}

float MathAngleRadFromBearingCwNorthDeg(float bearingDegClockwiseFromNorth) {
  constexpr float kDegToRad = 0.01745329251994329577f;
  const float br = bearingDegClockwiseFromNorth * kDegToRad;
  return std::atan2(std::cos(br), std::sin(br));
}

float BearingCwNorthDegFromMathAngleRad(float mathAngleRadFromEastCcw) {
  const double deg =
      std::atan2(static_cast<double>(std::cos(mathAngleRadFromEastCcw)),
                 static_cast<double>(std::sin(mathAngleRadFromEastCcw))) *
      (180.0 / 3.14159265358979323846);
  double out = deg;
  if (out < 0.0)
    out += 360.0;
  return static_cast<float>(out);
}

static void RotatePtForAnnotationPreview(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  float dx = *x - bx;
  float dy = *y - by;
  *x = bx + c * dx - s * dy;
  *y = by + s * dx + c * dy;
}

bool CadRotatePreviewTheta(const AppCommandState& cmd, float curX, float curY, float* outThetaRad) {
  using K = AppCommandState::Kind;
  using RP = AppCommandState::RotatePhase;
  if (cmd.active != K::Rotate || !outThetaRad)
    return false;
  if (cmd.rotatePhase == RP::NeedAngleOrReference) {
    const float dx = curX - cmd.rotateBaseX;
    const float dy = curY - cmd.rotateBaseY;
    // Match typed convention: bearing CW from north (−atan2(dx,dy) equals −bearing_rad for RotateAroundBase).
    *outThetaRad = -std::atan2(dx, dy);
  }
  else if (cmd.rotatePhase == RP::AfterReference_WaitAngleOrP) {
    const float thetaRef =
        std::atan2(cmd.rotateRefY2 - cmd.rotateRefY1, cmd.rotateRefX2 - cmd.rotateRefX1);
    *outThetaRad = std::atan2(curY - cmd.rotateBaseY, curX - cmd.rotateBaseX) - thetaRef;
  } else if (cmd.rotatePhase == RP::AnglePoints_WaitP2)
    *outThetaRad = RotateDeltaFromReferenceAndNewSegment(cmd.rotateRefX1, cmd.rotateRefY1, cmd.rotateRefX2,
                                                         cmd.rotateRefY2, cmd.rotateAnglePt1X,
                                                         cmd.rotateAnglePt1Y, curX, curY);
  else
    return false;
  return true;
}

bool CadScalePreviewFactor(const AppCommandState& cmd, float curX, float curY, float* outScale) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (cmd.active != K::Scale || !outScale)
    return false;
  if (cmd.modifyPhase != MP::NeedDestination)
    return false;
  if (cmd.scalePhase == SP::FactorPick) {
    const float d = std::hypot(curX - cmd.modifyBaseX, curY - cmd.modifyBaseY);
    float s = d / std::max(cmd.scaleRefDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  if (cmd.scalePhase == SP::NewLength_WaitP2) {
    const float d = std::hypot(curX - cmd.scaleNewLenP1X, curY - cmd.scaleNewLenP1Y);
    float s = d / std::max(cmd.scaleRefDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  return false;
}

static void CadAnnotationPreviewTranslated(const CadAnnotation& src, float dx, float dy, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  out->insX += dx;
  out->insY += dy;
  if (out->kind == CadAnnotation::Kind::Mtext) {
    out->boxMinX += dx;
    out->boxMinY += dy;
    out->boxMaxX += dx;
    out->boxMaxY += dy;
  } else if (out->kind == CadAnnotation::Kind::DimAligned || out->kind == CadAnnotation::Kind::DimLinear) {
    out->dimExt1X += dx;
    out->dimExt1Y += dy;
    out->dimExt2X += dx;
    out->dimExt2Y += dy;
  }
}

static void CadAnnotationPreviewRotated(const CadAnnotation& src, float bx, float by, float rad, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  RotatePtForAnnotationPreview(bx, by, rad, &a.insX, &a.insY);
  if (a.kind == CadAnnotation::Kind::Text) {
    a.rotationRad += rad;
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    RotateCadDimLinearAroundBase(bx, by, rad, &a);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    RotatePtForAnnotationPreview(bx, by, rad, &a.dimExt1X, &a.dimExt1Y);
    RotatePtForAnnotationPreview(bx, by, rad, &a.dimExt2X, &a.dimExt2Y);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = std::atan2(ty, tx);
  } else {
    float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
    float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
    float mnX = xs[0];
    float mxX = xs[0];
    float mnY = ys[0];
    float mxY = ys[0];
    for (int i = 0; i < 4; ++i) {
      RotatePtForAnnotationPreview(bx, by, rad, &xs[i], &ys[i]);
      mnX = std::min(mnX, xs[i]);
      mxX = std::max(mxX, xs[i]);
      mnY = std::min(mnY, ys[i]);
      mxY = std::max(mxY, ys[i]);
    }
    a.boxMinX = mnX;
    a.boxMaxX = mxX;
    a.boxMinY = mnY;
    a.boxMaxY = mxY;
    a.insX = mnX;
    a.insY = mnY;
  }
}

static void CadAnnotationPreviewScaled(const CadAnnotation& src, float bx, float by, float sc, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
  if (a.kind == CadAnnotation::Kind::Text) {
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    ScaleCadDimLinearAroundBase(bx, by, sc, &a);
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
    ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
    a.dimSignedOffset *= sc;
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = std::atan2(ty, tx);
  } else if (a.kind == CadAnnotation::Kind::DimAngular) {
    ScalePtAroundBase(bx, by, sc, &a.dimAngVertexX, &a.dimAngVertexY);
    ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
    ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
    a.dimSignedOffset *= sc;
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else {
    ScalePtAroundBase(bx, by, sc, &a.boxMinX, &a.boxMinY);
    ScalePtAroundBase(bx, by, sc, &a.boxMaxX, &a.boxMaxY);
    if (a.boxMinX > a.boxMaxX)
      std::swap(a.boxMinX, a.boxMaxX);
    if (a.boxMinY > a.boxMaxY)
      std::swap(a.boxMinY, a.boxMaxY);
    a.insX = a.boxMinX;
    a.insY = a.boxMinY;
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  }
}

void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                           std::vector<CadAnnotation>* out) {
  if (!out)
    return;
  out->clear();
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  if ((cmd.active == K::Move || cmd.active == K::Copy) && cmd.modifyPhase == MP::NeedDestination) {
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewTranslated(cmd.cadAnnotations[k], dx, dy, &p);
      out->push_back(p);
    }
    return;
  }
  float sc = 1.f;
  if (cmd.active == K::Scale && cmd.modifyPhase == MP::NeedDestination) {
    if (!CadScalePreviewFactor(cmd, curX, curY, &sc))
      return;
    const float bx = cmd.modifyBaseX;
    const float by = cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewScaled(cmd.cadAnnotations[k], bx, by, sc, &p);
      out->push_back(p);
    }
    return;
  }
  float theta = 0.f;
  if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
    return;
  const float bx = cmd.rotateBaseX;
  const float by = cmd.rotateBaseY;
  for (const auto& e : cmd.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.cadAnnotations.size())
      continue;
    CadAnnotation p{};
    CadAnnotationPreviewRotated(cmd.cadAnnotations[k], bx, by, theta, &p);
    out->push_back(p);
  }
}

bool ComputeWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY, double* outMxY) {
  bool any = false;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  auto consider = [&](double x, double y) { ExpandExtents(x, y, &mnX, &mxX, &mnY, &mxY, &any); };

  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      consider(static_cast<double>(L[i]), static_cast<double>(L[i + 1]));
      consider(static_cast<double>(L[i + 3]), static_cast<double>(L[i + 4]));
    }
  }
  const auto& C = st.userCirclesCxCyR;
  if (C.size() % 3 == 0) {
    for (size_t ci = 0; ci + 2 < C.size(); ci += 3) {
      const double cx = static_cast<double>(C[ci]);
      const double cy = static_cast<double>(C[ci + 1]);
      const double r = std::fabs(static_cast<double>(C[ci + 2]));
      if (r <= 1e-12)
        continue;
      consider(cx - r, cy - r);
      consider(cx + r, cy - r);
      consider(cx - r, cy + r);
      consider(cx + r, cy + r);
    }
  }
  for (const SurveyPoint& p : st.surveyPoints)
    consider(static_cast<double>(p.easting), static_cast<double>(p.northing));

  for (const CadAnnotation& a : st.cadAnnotations) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(a, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    consider(static_cast<double>(amnX), static_cast<double>(amnY));
    consider(static_cast<double>(amxX), static_cast<double>(amxY));
  }

  for (const CadArc& a : st.userArcs) {
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    const int n = std::max(8, static_cast<int>(std::fabs(static_cast<double>(a.sweepRad)) / (3.14159265 / 16.0)) + 1);
    for (int i = 0; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double t = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u;
      double wx = 0.;
      double wy = 0.;
      CirclePointWorld(dcx, dcy, dr, t, &wx, &wy);
      consider(wx, wy);
    }
  }

  for (const CadEllipse& el : st.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      continue;
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ux = static_cast<double>(el.majVx) / ma;
    const double uy = static_cast<double>(el.majVy) / ma;
    const double px = -uy;
    const double py = ux;
    const double mb = ma * static_cast<double>(el.ratio);
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    for (int i = 0; i < n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      consider(ecx + ux * (ma * c) + px * (mb * s), ecy + uy * (ma * c) + py * (mb * s));
    }
  }

  const auto& PV = st.userPolylineVerts;
  const auto& PO = st.userPolylineOffsets;
  if (PO.size() >= 2) {
    for (size_t pi = 0; pi + 1 < PO.size(); ++pi) {
      const int v0 = PO[pi];
      const int v1 = PO[pi + 1];
      for (int vi = v0; vi < v1; ++vi) {
        consider(static_cast<double>(PV[static_cast<size_t>(vi * 3 + 0)]),
                 static_cast<double>(PV[static_cast<size_t>(vi * 3 + 1)]));
      }
    }
  }

  if (!any)
    return false;
  *outMnX = mnX;
  *outMxX = mxX;
  *outMnY = mnY;
  *outMxY = mxY;
  return true;
}

namespace {

struct EntityBox {
  double cx;
  double cy;
  double mnX;
  double mxX;
  double mnY;
  double mxY;
};

[[nodiscard]] double NthPercentile(std::vector<double>& v, double p) {
  if (v.empty())
    return 0.;
  const size_t n = v.size();
  const double idxF = p * static_cast<double>(n - 1);
  const size_t k = std::clamp(static_cast<size_t>(idxF), size_t{0}, n - 1);
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}

void CollectEntityBoxes(const AppCommandState& st, std::vector<EntityBox>& out) {
  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      EntityBox b{};
      b.mnX = std::min(static_cast<double>(L[i]), static_cast<double>(L[i + 3]));
      b.mxX = std::max(static_cast<double>(L[i]), static_cast<double>(L[i + 3]));
      b.mnY = std::min(static_cast<double>(L[i + 1]), static_cast<double>(L[i + 4]));
      b.mxY = std::max(static_cast<double>(L[i + 1]), static_cast<double>(L[i + 4]));
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }
  const auto& C = st.userCirclesCxCyR;
  if (C.size() % 3 == 0) {
    for (size_t ci = 0; ci + 2 < C.size(); ci += 3) {
      const double cx = static_cast<double>(C[ci]);
      const double cy = static_cast<double>(C[ci + 1]);
      const double r = std::fabs(static_cast<double>(C[ci + 2]));
      if (r <= 1e-12)
        continue;
      EntityBox b{};
      b.mnX = cx - r;
      b.mxX = cx + r;
      b.mnY = cy - r;
      b.mxY = cy + r;
      b.cx = cx;
      b.cy = cy;
      out.push_back(b);
    }
  }
  for (const SurveyPoint& p : st.surveyPoints) {
    EntityBox b{};
    b.mnX = b.mxX = b.cx = static_cast<double>(p.easting);
    b.mnY = b.mxY = b.cy = static_cast<double>(p.northing);
    out.push_back(b);
  }
  for (const CadAnnotation& a : st.cadAnnotations) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(a, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    EntityBox b{};
    b.mnX = static_cast<double>(amnX);
    b.mxX = static_cast<double>(amxX);
    b.mnY = static_cast<double>(amnY);
    b.mxY = static_cast<double>(amxY);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (const CadArc& a : st.userArcs) {
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    EntityBox b{};
    b.mnX = static_cast<double>(a.cx) - dr;
    b.mxX = static_cast<double>(a.cx) + dr;
    b.mnY = static_cast<double>(a.cy) - dr;
    b.mxY = static_cast<double>(a.cy) + dr;
    b.cx = static_cast<double>(a.cx);
    b.cy = static_cast<double>(a.cy);
    out.push_back(b);
  }
  for (const CadEllipse& el : st.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      continue;
    const double mb = ma * static_cast<double>(el.ratio);
    const double rrx = std::hypot(ma, mb);
    EntityBox b{};
    b.mnX = static_cast<double>(el.cx) - rrx;
    b.mxX = static_cast<double>(el.cx) + rrx;
    b.mnY = static_cast<double>(el.cy) - rrx;
    b.mxY = static_cast<double>(el.cy) + rrx;
    b.cx = static_cast<double>(el.cx);
    b.cy = static_cast<double>(el.cy);
    out.push_back(b);
  }
  const auto& PV = st.userPolylineVerts;
  const auto& PO = st.userPolylineOffsets;
  if (PO.size() >= 2) {
    for (size_t pi = 0; pi + 1 < PO.size(); ++pi) {
      const int v0 = PO[pi];
      const int v1 = PO[pi + 1];
      if (v1 <= v0)
        continue;
      EntityBox b{};
      bool any = false;
      for (int vi = v0; vi < v1; ++vi) {
        const double vx = static_cast<double>(PV[static_cast<size_t>(vi * 3 + 0)]);
        const double vy = static_cast<double>(PV[static_cast<size_t>(vi * 3 + 1)]);
        if (!any) {
          b.mnX = b.mxX = vx;
          b.mnY = b.mxY = vy;
          any = true;
        } else {
          b.mnX = std::min(b.mnX, vx);
          b.mxX = std::max(b.mxX, vx);
          b.mnY = std::min(b.mnY, vy);
          b.mxY = std::max(b.mxY, vy);
        }
      }
      if (!any)
        continue;
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }
}

} // namespace

bool ComputeRobustWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                               double* outMxY, int* outSkipped) {
  if (outSkipped)
    *outSkipped = 0;
  std::vector<EntityBox> ents;
  ents.reserve(st.userLinesFlat.size() / 6 + st.userCirclesCxCyR.size() / 3 + st.userArcs.size() +
               st.userEllipses.size() + st.cadAnnotations.size() + st.surveyPoints.size() +
               (st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1));
  CollectEntityBoxes(st, ents);

  if (ents.size() < 16)
    return ComputeWorldExtents(st, outMnX, outMxX, outMnY, outMxY);

  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve(ents.size());
  ys.reserve(ents.size());
  for (const EntityBox& b : ents) {
    xs.push_back(b.cx);
    ys.push_back(b.cy);
  }

  std::vector<double> xsCopy = xs;
  std::vector<double> ysCopy = ys;
  const double xP05 = NthPercentile(xsCopy, 0.05);
  xsCopy = xs;
  const double xP95 = NthPercentile(xsCopy, 0.95);
  const double yP05 = NthPercentile(ysCopy, 0.05);
  ysCopy = ys;
  const double yP95 = NthPercentile(ysCopy, 0.95);

  const double bulkSpanX = std::max(xP95 - xP05, 0.);
  const double bulkSpanY = std::max(yP95 - yP05, 0.);
  const double midX = 0.5 * (xP05 + xP95);
  const double midY = 0.5 * (yP05 + yP95);

  // Outlier window: entities whose center is within ±5× the bulk span from the bulk midpoint are kept.
  // A 5× pad is generous enough to retain legitimate sparse content while still rejecting (0,0)-anchored
  // strays in large DXFs.
  const double radX = std::max(bulkSpanX * 5.0, 1.0);
  const double radY = std::max(bulkSpanY * 5.0, 1.0);

  bool any = false;
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  int skipped = 0;
  for (const EntityBox& b : ents) {
    if (std::fabs(b.cx - midX) > radX || std::fabs(b.cy - midY) > radY) {
      ++skipped;
      continue;
    }
    if (!any) {
      mnX = b.mnX;
      mxX = b.mxX;
      mnY = b.mnY;
      mxY = b.mxY;
      any = true;
    } else {
      mnX = std::min(mnX, b.mnX);
      mxX = std::max(mxX, b.mxX);
      mnY = std::min(mnY, b.mnY);
      mxY = std::max(mxY, b.mxY);
    }
  }

  if (!any)
    return ComputeWorldExtents(st, outMnX, outMxX, outMnY, outMxY);

  *outMnX = mnX;
  *outMxX = mxX;
  *outMnY = mnY;
  *outMxY = mxY;
  if (outSkipped)
    *outSkipped = skipped;
  return true;
}

void ApplyViewportZoomToWorldRect(double mnX, double mxX, double mnY, double mxY, double* panX, double* panY,
                                  float* zoom, int fbW, int fbH, float viewportAspect) {
  (void)fbW;
  (void)fbH;
  const float aspect = std::max(viewportAspect, 1e-6f);
  constexpr float kMargin = 0.08f;
  constexpr double kMinSpan = 1e-5;
  double dmnX = mnX;
  double dmxX = mxX;
  double dmnY = mnY;
  double dmxY = mxY;
  double rw = dmxX - dmnX;
  double rh = dmxY - dmnY;
  if (rw < kMinSpan) {
    dmnX -= kMinSpan;
    dmxX += kMinSpan;
    rw = dmxX - dmnX;
  }
  if (rh < kMinSpan) {
    dmnY -= kMinSpan;
    dmxY += kMinSpan;
    rh = dmxY - dmnY;
  }
  const double cx = 0.5 * (dmnX + dmxX);
  const double cy = 0.5 * (dmnY + dmxY);
  const double denom = 2.0 * (1.0 - static_cast<double>(kMargin));
  const double needHalfH = std::max(rh / denom, rw / (static_cast<double>(aspect) * denom));
  constexpr float kOrthoHalfHRef = 50.f;
  *panX = cx;
  *panY = cy;
  *zoom = std::clamp(kOrthoHalfHRef / static_cast<float>(std::max(needHalfH, 1e-8)), 1.e-9f, 1.e9f);
}

bool ParseAngleDegrees(const std::string& raw, float* degreesOut) {
  return ParseAngleDegreesInternal(raw, degreesOut);
}

bool ParseWorldPoint(const std::string& raw, float* ox, float* oy, bool allowRelative, float baseX, float baseY) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  if (!s.empty() && s[0] == '@') {
    if (!allowRelative)
      return false;
    s = StringUtil::trimCopy(s.substr(1));
    float dx = 0.f;
    float dy = 0.f;
    if (!ParseTwoFloats(s, &dx, &dy))
      return false;
    *ox = baseX + dx;
    *oy = baseY + dy;
    return true;
  }
  return ParseTwoFloats(s, ox, oy);
}

bool ParseStoragePoint(const AppCommandState& st, const std::string& raw, float* lx, float* ly, bool allowRelative,
                       float baseLocalX, float baseLocalY) {
  if (!lx || !ly)
    return false;
  double baseWx = 0.;
  double baseWy = 0.;
  CadCoord::WorldFromLocal(st, baseLocalX, baseLocalY, &baseWx, &baseWy);
  float wx = 0.f;
  float wy = 0.f;
  if (!ParseWorldPoint(raw, &wx, &wy, allowRelative, static_cast<float>(baseWx), static_cast<float>(baseWy)))
    return false;
  CadCoord::LocalFromWorld(st, static_cast<double>(wx), static_cast<double>(wy), lx, ly);
  return true;
}

void ApplyOrthoConstrainFromAnchor(float anchorX, float anchorY, float* wx, float* wy, bool ortho) {
  if (!ortho || !wx || !wy)
    return;
  const float dx = *wx - anchorX;
  const float dy = *wy - anchorY;
  if (std::fabs(dx) >= std::fabs(dy))
    *wy = anchorY;
  else
    *wx = anchorX;
}

void ApplySegmentAngleLockToWorldPick(float anchorX, float anchorY, float lockUx, float lockUy, float* wx, float* wy,
                                      bool forwardOnly) {
  if (!wx || !wy)
    return;
  const float dx = *wx - anchorX;
  const float dy = *wy - anchorY;
  float t = dx * lockUx + dy * lockUy;
  if (forwardOnly && t < 0.f)
    t = 0.f;
  *wx = anchorX + t * lockUx;
  *wy = anchorY + t * lockUy;
}

static float NormalizeBearingDegreesCwNorth(float deg) {
  deg = std::fmod(deg, 360.f);
  if (deg < 0.f)
    deg += 360.f;
  return deg;
}

static bool ParseBearingCwNorthStringWithOptionalDelta(const std::string& raw, float* bearingCombinedDegOut,
                                                       std::vector<std::string>& log) {
  std::string work = StringUtil::trimCopy(raw);
  if (work.empty())
    return false;
  float bear = 0.f;
  float delta = 0.f;
  bool hasDelta = false;
  std::string bearOnly = work;

  const size_t sp = work.rfind(' ');
  if (sp != std::string::npos && sp + 1 < work.size()) {
    std::string tail = StringUtil::trimCopy(work.substr(sp + 1));
    std::string head = StringUtil::trimCopy(work.substr(0, sp));
    if (!tail.empty() && (tail[0] == '+' || tail[0] == '-')) {
      if (!ParseAngleDegreesInternal(tail, &delta)) {
        log.push_back("Invalid adjustment — use +90 / -45 (decimal or DMS).");
        return false;
      }
      hasDelta = true;
      bearOnly = head;
    }
  }

  if (!hasDelta) {
    for (size_t k = 1; k < work.size(); ++k) {
      if (work[k] != '+' && work[k] != '-')
        continue;
      std::string head = StringUtil::trimCopy(work.substr(0, k));
      std::string tail = StringUtil::trimCopy(work.substr(k));
      if (head.empty() || tail.empty())
        continue;
      if (!ParseAngleDegreesInternal(head, &bear))
        continue;
      if (!ParseAngleDegreesInternal(tail, &delta))
        continue;
      *bearingCombinedDegOut = NormalizeBearingDegreesCwNorth(bear + delta);
      return true;
    }
  }

  if (!ParseAngleDegreesInternal(bearOnly, &bear)) {
    log.push_back("Could not parse bearing — decimal degrees or DMS (° clockwise from north).");
    return false;
  }
  *bearingCombinedDegOut = NormalizeBearingDegreesCwNorth(bear + (hasDelta ? delta : 0.f));
  return true;
}

static void CommitSegmentAnglePickLock(AppCommandState& st, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const float br = NormalizeBearingDegreesCwNorth(st.segmentPickDraftBearingDeg);
  const float theta = MathAngleRadFromBearingCwNorthDeg(br);
  st.segmentLockUx = std::cos(theta);
  st.segmentLockUy = std::sin(theta);
  st.segmentAngleLockActive = true;
  st.segmentAnglePickPhase = SAP::Idle;
  char buf[144];
  std::snprintf(buf, sizeof(buf),
                "Bearing locked %.6g° clockwise from north — distance (+/-) or click on ray (A clears).",
                static_cast<double>(br));
  log.push_back(buf);
}

void CancelSegmentAnglePick(AppCommandState& st, std::vector<std::string>* log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const bool hadPick = st.segmentAnglePickPhase != SAP::Idle;
  st.segmentAnglePickPhase = SAP::Idle;
  st.segmentAngleKeyboardAwaitBearing = false;
  if (hadPick && log)
    log->push_back("Bearing pick — canceled.");
}

bool TryParseSegmentAngleLockCommand(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const std::string s = StringUtil::trimCopy(lineIn);
  if (s.empty())
    return false;
  const std::string low = StringUtil::toLowerAsciiCopy(s);
  if (low == "ap" || low == "anglepick" || low == "a p") {
    ResetSegmentAngleLock(st);
    st.segmentAngleKeyboardAwaitBearing = false;
    st.segmentAnglePickPhase = SAP::WaitP1;
    log.push_back("Bearing pick — first reference point (viewport click). ESC cancels pick.");
    return true;
  }
  if (low == "a" || low == "angle") {
    if (st.segmentAngleLockActive || st.segmentAnglePickPhase != SAP::Idle) {
      ResetSegmentAngleLock(st);
      log.push_back("Segment bearing lock — off.");
    } else {
      st.segmentAngleKeyboardAwaitBearing = true;
      log.push_back("Bearing ° clockwise from north (decimal/DMS); blank Enter cancels.");
    }
    return true;
  }
  std::string rest;
  if (low.rfind("angle ", 0) == 0)
    rest = StringUtil::trimCopy(s.substr(6));
  else if (low.rfind("a ", 0) == 0)
    rest = StringUtil::trimCopy(s.substr(2));
  else if (low.rfind("angle", 0) == 0 && low.size() > 5)
    rest = StringUtil::trimCopy(s.substr(5));
  else if (low.rfind("a", 0) == 0 && low.size() > 1)
    rest = StringUtil::trimCopy(s.substr(1));
  else
    return false;

  if (rest.empty()) {
    if (st.segmentAngleLockActive || st.segmentAnglePickPhase != SAP::Idle) {
      ResetSegmentAngleLock(st);
      log.push_back("Segment bearing lock — off.");
    } else {
      st.segmentAngleKeyboardAwaitBearing = true;
      log.push_back("Bearing ° clockwise from north (decimal/DMS); blank Enter cancels.");
    }
    return true;
  }
  float combined = 0.f;
  if (!ParseBearingCwNorthStringWithOptionalDelta(rest, &combined, log))
    return true;
  const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
  st.segmentLockUx = std::cos(theta);
  st.segmentLockUy = std::sin(theta);
  st.segmentAngleLockActive = true;
  st.segmentAnglePickPhase = SAP::Idle;
  char buf[144];
  std::snprintf(buf, sizeof(buf),
                "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                static_cast<double>(combined));
  log.push_back(buf);
  return true;
}

bool OrthoUnitTowardPoint(float anchorX, float anchorY, float targetX, float targetY, float* ux, float* uy) {
  const float dx = targetX - anchorX;
  const float dy = targetY - anchorY;
  if (std::fabs(dx) < 1.e-12f && std::fabs(dy) < 1.e-12f)
    return false;
  if (std::fabs(dx) >= std::fabs(dy)) {
    *ux = dx >= 0.f ? 1.f : -1.f;
    *uy = 0.f;
  } else {
    *ux = 0.f;
    *uy = dy >= 0.f ? 1.f : -1.f;
  }
  return true;
}

bool ParseSingleFloatToken(const std::string& raw, float* out) {
  std::istringstream iss(raw);
  iss >> std::ws;
  if (!(iss >> *out))
    return false;
  iss >> std::ws;
  return iss.eof();
}

void StartLineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Line;
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  log.push_back("LINE — specify first point (click or type X,Y / X Y). ESC to cancel.");
}

void StartCircleCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Circle;
  log.push_back(
      "CIRCLE — center + radius: click/type center, then radius (click edge), type radius, or D + diameter.");
  log.push_back("Or type 3P first for a three-point circle. ESC to cancel.");
}

void StartPolylineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Polyline;
  st.polylinePhase = AppCommandState::PolylinePhase::NeedFirstPoint;
  log.push_back("POLYLINE — like LINE (A / AP bearing lock); CLOSE/CL; ortho; ESC cancels.");
}

void StartArcCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Arc;
  st.arcPhase = AppCommandState::ArcPhase::WaitStart;
  log.push_back("ARC — three picks: start, point on arc, end. ESC cancels.");
}

void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Ellipse;
  st.ellPhase = AppCommandState::EllipsePhase::WaitCenter;
  log.push_back("ELLIPSE — center, major axis endpoint, then minor/major ratio (0-1] on command line (Enter=0.5).");
}

void StartTextCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Text;
  st.textPhase = AppCommandState::TextCmdPhase::WaitInsertion;
  log.push_back(
      "TEXT — pick insertion, then height / rotation / string on command line (defaults from plot scale). ESC "
      "cancels.");
}

void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Mtext;
  st.mtextPhase = AppCommandState::MtextPhase::WaitCorner1;
  log.push_back("MTEXT — two corners for box, then type in the on-screen editor (Ctrl+Enter reformats; Save to place). ESC cancels.");
}

void OpenMtextRichEditorForPlacement(AppCommandState& st, std::vector<std::string>* log) {
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = true;
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorBuf.clear();
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back("MTEXT — type in the box; Ctrl+Enter reformats; Save to place; Esc to cancel.");
}

void OpenMtextRichEditorForAnnotation(AppCommandState& st, int annIndex, std::vector<std::string>* log) {
  if (annIndex < 0 || static_cast<size_t>(annIndex) >= st.cadAnnotations.size())
    return;
  CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(annIndex)];
  if (a.kind != CadAnnotation::Kind::Mtext)
    return;
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorAnnIndex = annIndex;
  st.mtextRichEditorBuf = a.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back("MTEXT — edit in the box; Ctrl+Enter reformats; Save to update; Esc to cancel.");
}

void CommitMtextRichEditor(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.mtextRichEditorOpen)
    return;
  using K = AppCommandState::Kind;
  if (st.mtextRichEditorPlacement) {
    if (st.active != K::Mtext || st.mtextPhase != AppCommandState::MtextPhase::WaitString) {
      CloseMtextRichEditorUi(st);
      return;
    }
    const std::string normalized = MtextRichNormalize(st.mtextRichEditorBuf);
    if (!StringUtil::trimCopy(MtextRichFlattenToPlain(normalized)).empty()) {
      CadAnnotation ann;
      ann.kind = CadAnnotation::Kind::Mtext;
      ann.boxMinX = std::min(st.mtxtX1, st.mtxtX2);
      ann.boxMinY = std::min(st.mtxtY1, st.mtxtY2);
      ann.boxMaxX = std::max(st.mtxtX1, st.mtxtX2);
      ann.boxMaxY = std::max(st.mtxtY1, st.mtxtY2);
      ann.insX = ann.boxMinX;
      ann.insY = ann.boxMinY;
      ann.plottedHeightInches = st.defaultPlottedTextHeightInches;
      ann.text = normalized;
      st.cadAnnotations.push_back(std::move(ann));
      st.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
      BumpCadGpuCache(st);
      log.push_back("MTEXT placed.");
    } else
      log.push_back("MTEXT — empty; canceled.");
    st.active = K::None;
    ResetMtextDraft(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  const int ix = st.mtextRichEditorAnnIndex;
  if (ix >= 0 && static_cast<size_t>(ix) < st.cadAnnotations.size() &&
      st.cadAnnotations[static_cast<size_t>(ix)].kind == CadAnnotation::Kind::Mtext) {
    CadAnnotation& ann = st.cadAnnotations[static_cast<size_t>(ix)];
    ann.text = MtextRichNormalize(st.mtextRichEditorBuf);
    if (ann.surveyPointLabelFor >= 0 &&
        static_cast<size_t>(ann.surveyPointLabelFor) < st.surveyPoints.size()) {
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(ann.surveyPointLabelFor));
    }
    BumpCadGpuCache(st);
    log.push_back("MTEXT updated.");
  }
  CloseMtextRichEditorUi(st);
}

void CancelMtextRichEditor(AppCommandState& st, std::vector<std::string>* log) {
  if (!st.mtextRichEditorOpen)
    return;
  if (st.mtextRichEditorPlacement) {
    if (log)
      log->push_back("MTEXT — canceled.");
    st.active = AppCommandState::Kind::None;
    ResetMtextDraft(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  CloseMtextRichEditorUi(st);
}

void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::DimAligned;
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  log.push_back("DIMALIGNED — extension 1, extension 2, then offset (point away from measured line). ESC cancels.");
}

void StartDimLinearCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::DimLinear;
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  log.push_back("DIMLINEAR — ortho distance in X or Y between extension points; third pick sets dimension line; "
                "cursor or H/V chooses horizontal vs vertical. ESC cancels.");
}

void StartDimAngularCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::DimAngular;
  st.dimAngularPhase = AppCommandState::DimAngularPhase::WaitVertex;
  log.push_back("DIMANGULAR — vertex, two ray points, then arc position (radius). Text is degrees/minutes/seconds. ESC "
                "cancels.");
}

void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("ID — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::IdPoint;
  log.push_back("ID — specify point (click in drawing or type X,Y). UCS = World. ESC cancels.");
}

void StartSurveyInverseCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SIP = AppCommandState::SurveyInversePhase;
  if (st.active != K::None) {
    log.push_back("INVERSE — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::SurveyInverse;
  st.surveyInversePhase = SIP::WaitFrom;
  log.push_back(
      "INVERSE — first point (World X=Easting, Y=Northing); then second. "
      "Result: ΔE, ΔN, horizontal distance, bearing D°M'S\" and decimal ° clockwise from north. ESC cancels.");
}

void ClearCadSelection(AppCommandState& st) {
  st.selection.clear();
  st.selBoxWaitingSecond = false;
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
}

void EnsureAttrCounts(AppCommandState& st) {
  bool grew = false;
  const size_t nl = st.userLinesFlat.size() / 6;
  while (st.userLineAttrs.size() < nl) {
    st.userLineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t nc = st.userCirclesCxCyR.size() / 3;
  while (st.userCircleAttrs.size() < nc) {
    st.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.userArcAttrs.size() < st.userArcs.size()) {
    st.userArcAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.userEllAttrs.size() < st.userEllipses.size()) {
    st.userEllAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t np = st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0;
  while (st.userPolylineAttrs.size() < np) {
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t na = st.cadAnnotations.size();
  while (st.cadAnnotationAttrs.size() < na) {
    st.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  if (grew)
    BumpCadGpuCache(st);
}

static void CollectLayersUsedInDrawing(const AppCommandState& st, std::set<std::string>* out) {
  out->insert("0");
  auto add = [&](const std::string& s) {
    if (!s.empty())
      out->insert(s);
  };
  for (const auto& a : st.userLineAttrs)
    add(a.layer);
  for (const auto& a : st.userCircleAttrs)
    add(a.layer);
  for (const auto& a : st.userArcAttrs)
    add(a.layer);
  for (const auto& a : st.userEllAttrs)
    add(a.layer);
  for (const auto& a : st.userPolylineAttrs)
    add(a.layer);
  for (const auto& a : st.cadAnnotationAttrs)
    add(a.layer);
  for (const auto& p : st.surveyPoints)
    add(p.layer);
  add(st.currentLayer);
}

void SyncDrawingLayerTableWithGeometry(AppCommandState& st) {
  if (st.drawingLayerTable.empty()) {
    CadLayerRow z;
    z.name = "0";
    st.drawingLayerTable.push_back(z);
  }
  std::set<std::string> have;
  for (const auto& r : st.drawingLayerTable)
    have.insert(r.name);
  std::set<std::string> used;
  CollectLayersUsedInDrawing(st, &used);
  for (const std::string& name : used) {
    if (!have.count(name)) {
      CadLayerRow row;
      row.name = name;
      st.drawingLayerTable.push_back(row);
      have.insert(name);
    }
  }
  if (st.currentLayer.empty())
    st.currentLayer = "0";
  if (!have.count(st.currentLayer))
    st.currentLayer = "0";
}

static bool ValidNewLayerNameChars(const std::string& n) {
  if (n.empty() || n.size() > 255)
    return false;
  for (unsigned char c : n) {
    if (c < 32 || c == 127)
      return false;
    if (c == '<' || c == '>' || c == '/' || c == '\\' || c == '"' || c == ':' || c == ';' || c == '?' ||
        c == '*' || c == '|' || c == ',' || c == '`')
      return false;
  }
  return true;
}

static bool LayerNameExistsCi(const AppCommandState& st, const std::string& name) {
  const std::string nl = StringUtil::toLowerAsciiCopy(name);
  for (const auto& r : st.drawingLayerTable) {
    if (StringUtil::toLowerAsciiCopy(r.name) == nl)
      return true;
  }
  return false;
}

bool CadAddDrawingLayer(AppCommandState& st, const std::string& raw, std::string* err) {
  const std::string name = StringUtil::trimCopy(raw);
  if (!ValidNewLayerNameChars(name)) {
    if (err)
      *err = "Invalid layer name (empty, too long, or reserved characters).";
    return false;
  }
  SyncDrawingLayerTableWithGeometry(st);
  if (LayerNameExistsCi(st, name)) {
    if (err)
      *err = "Layer already exists.";
    return false;
  }
  CadLayerRow row;
  row.name = name;
  st.drawingLayerTable.push_back(row);
  return true;
}

bool CadRenameDrawingLayer(AppCommandState& st, const std::string& oldNameRaw, const std::string& newNameRaw,
                           std::string* err) {
  const std::string oldN = StringUtil::trimCopy(oldNameRaw);
  const std::string newN = StringUtil::trimCopy(newNameRaw);
  if (oldN == "0") {
    if (err)
      *err = "Layer 0 cannot be renamed.";
    return false;
  }
  if (!ValidNewLayerNameChars(newN)) {
    if (err)
      *err = "Invalid new layer name.";
    return false;
  }
  if (newN == oldN)
    return true;
  if (LayerNameExistsCi(st, newN) && StringUtil::toLowerAsciiCopy(newN) != StringUtil::toLowerAsciiCopy(oldN)) {
    if (err)
      *err = "A layer with that name already exists.";
    return false;
  }
  auto it = std::find_if(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                         [&](const CadLayerRow& r) { return r.name == oldN; });
  if (it == st.drawingLayerTable.end()) {
    if (err)
      *err = "Layer not found in table.";
    return false;
  }
  auto reassign = [&](std::string& L) {
    if (L == oldN)
      L = newN;
  };
  for (auto& a : st.userLineAttrs)
    reassign(a.layer);
  for (auto& a : st.userCircleAttrs)
    reassign(a.layer);
  for (auto& a : st.userArcAttrs)
    reassign(a.layer);
  for (auto& a : st.userEllAttrs)
    reassign(a.layer);
  for (auto& a : st.userPolylineAttrs)
    reassign(a.layer);
  for (auto& a : st.cadAnnotationAttrs)
    reassign(a.layer);
  for (auto& p : st.surveyPoints)
    reassign(p.layer);
  if (st.currentLayer == oldN)
    st.currentLayer = newN;
  it->name = newN;
  BumpCadGpuCache(st);
  return true;
}

bool CadDeleteDrawingLayer(AppCommandState& st, const std::string& nameRaw, std::string* err) {
  const std::string name = StringUtil::trimCopy(nameRaw);
  if (name == "0") {
    if (err)
      *err = "Layer 0 cannot be deleted.";
    return false;
  }
  const auto itRow = std::find_if(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                                  [&](const CadLayerRow& r) { return r.name == name; });
  if (itRow == st.drawingLayerTable.end()) {
    if (err)
      *err = "Layer not found.";
    return false;
  }
  auto reassign = [&](std::string& L) {
    if (L == name)
      L = "0";
  };
  for (auto& a : st.userLineAttrs)
    reassign(a.layer);
  for (auto& a : st.userCircleAttrs)
    reassign(a.layer);
  for (auto& a : st.userArcAttrs)
    reassign(a.layer);
  for (auto& a : st.userEllAttrs)
    reassign(a.layer);
  for (auto& a : st.userPolylineAttrs)
    reassign(a.layer);
  for (auto& a : st.cadAnnotationAttrs)
    reassign(a.layer);
  for (auto& p : st.surveyPoints)
    reassign(p.layer);
  if (st.currentLayer == name)
    st.currentLayer = "0";
  st.drawingLayerTable.erase(itRow);
  BumpCadGpuCache(st);
  return true;
}

void SelectSimilarToCurrentSelection(AppCommandState& st, std::vector<std::string>* log) {
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  ClearEntityGripInteraction(st);

  if (!st.selection.empty()) {
    st.selectedSurveyPointIndices.clear();
    const SelectedEntity& lead = st.selection.front();
    std::vector<SelectedEntity> next;
    switch (lead.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t n = st.userLinesFlat.size() / 6;
      for (size_t i = 0; i < n; ++i) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::LineSeg;
        e.index = static_cast<int>(i);
        next.push_back(e);
      }
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t n = st.userCirclesCxCyR.size() / 3;
      for (size_t i = 0; i < n; ++i) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Circle;
        e.index = static_cast<int>(i);
        next.push_back(e);
      }
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int np =
          st.userPolylineOffsets.size() > 1 ? static_cast<int>(st.userPolylineOffsets.size()) - 1 : 0;
      for (int i = 0; i < np; ++i) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Polyline;
        e.index = i;
        next.push_back(e);
      }
      break;
    }
    case SelectedEntity::Type::Arc: {
      for (size_t i = 0; i < st.userArcs.size(); ++i) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Arc;
        e.index = static_cast<int>(i);
        next.push_back(e);
      }
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      for (size_t i = 0; i < st.userEllipses.size(); ++i) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Ellipse;
        e.index = static_cast<int>(i);
        next.push_back(e);
      }
      break;
    }
    case SelectedEntity::Type::Annotation: {
      const int lix = lead.index;
      if (lix < 0 || static_cast<size_t>(lix) >= st.cadAnnotations.size())
        break;
      const CadAnnotation::Kind want = st.cadAnnotations[static_cast<size_t>(lix)].kind;
      for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
        if (st.cadAnnotations[ai].kind == want) {
          SelectedEntity e{};
          e.type = SelectedEntity::Type::Annotation;
          e.index = static_cast<int>(ai);
          next.push_back(e);
        }
      }
      break;
    }
    default:
      break;
    }
    st.selection = std::move(next);
    EnsureAttrCounts(st);
    BumpCadGpuCache(st);
    if (log)
      log->push_back("Select similar — " + std::to_string(st.selection.size()) + " object(s).");
    return;
  }

  if (!st.selectedSurveyPointIndices.empty()) {
    ClearCadSelection(st);
    st.selectedSurveyPointIndices.clear();
    for (size_t i = 0; i < st.surveyPoints.size(); ++i)
      st.selectedSurveyPointIndices.push_back(static_cast<int>(i));
    for (int svi : st.selectedSurveyPointIndices) {
      if (svi >= 0 && static_cast<size_t>(svi) < st.surveyPoints.size())
        SyncSurveyPointLinkedMtextSelection(st, svi);
    }
    BumpCadGpuCache(st);
    if (log)
      log->push_back("Select similar — all " + std::to_string(st.surveyPoints.size()) + " survey point(s).");
  } else if (log)
    log->push_back("Select similar — nothing selected.");
}

void ClearCadGeometry(AppCommandState& st) {
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  st.userLinesFlat.clear();
  st.userLineAttrs.clear();
  st.userCirclesCxCyR.clear();
  st.userCircleAttrs.clear();
  st.userArcs.clear();
  st.userArcAttrs.clear();
  st.userEllipses.clear();
  st.userEllAttrs.clear();
  st.userPolylineVerts.clear();
  st.userPolylineOffsets.clear();
  st.userPolylineClosed.clear();
  st.userPolylineAttrs.clear();
  st.cadAnnotations.clear();
  st.cadAnnotationAttrs.clear();
  ClearPendingOneShotObjectSnap(st);
  ClearCadSelection(st);
  BumpCadGpuCache(st);
}

void ClearPendingOneShotObjectSnap(AppCommandState& st) {
  st.pendingOneShotSnapValid = false;
}

void ResetCadToolStateToIdle(AppCommandState& st) {
  ClearPendingOneShotObjectSnap(st);
  st.active = AppCommandState::Kind::None;
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  ResetSegmentAngleLock(st);
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  AbortMtextGripInteraction(st);
}

void ClearSelection(AppCommandState& st) {
  ClearCadSelection(st);
  st.selectedSurveyPointIndices.clear();
}

void ApplySurveyPointClickSelection(AppCommandState& st, int surveyPointIndex, bool shiftModifier,
                                    std::vector<std::string>* log) {
  if (surveyPointIndex < 0 || static_cast<size_t>(surveyPointIndex) >= st.surveyPoints.size())
    return;
  auto& v = st.selectedSurveyPointIndices;
  const auto it = std::find(v.begin(), v.end(), surveyPointIndex);
  if (shiftModifier) {
    if (it != v.end()) {
      v.erase(it);
      if (log)
        log->push_back("Survey point removed from selection.");
    } else {
      v.push_back(surveyPointIndex);
      if (log)
        log->push_back("Survey point added to selection.");
    }
  } else if (it == v.end()) {
    v.push_back(surveyPointIndex);
  } else {
    v.clear();
    v.push_back(surveyPointIndex);
  }
}

static void ErasePolylineByIndex(AppCommandState& st, int pi) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return;
  const int np = static_cast<int>(st.userPolylineOffsets.size()) - 1;
  std::vector<int> nvPer(static_cast<size_t>(np));
  for (int i = 0; i < np; ++i)
    nvPer[static_cast<size_t>(i)] = st.userPolylineOffsets[static_cast<size_t>(i + 1)] - st.userPolylineOffsets[static_cast<size_t>(i)];
  const int a = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int b = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  st.userPolylineVerts.erase(st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(3 * a),
                             st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(3 * b));
  std::vector<int> newOff;
  newOff.reserve(static_cast<size_t>(std::max(0, np - 1) + 1));
  newOff.push_back(0);
  int run = 0;
  for (int i = 0; i < np; ++i) {
    if (i == pi)
      continue;
    run += nvPer[static_cast<size_t>(i)];
    newOff.push_back(run);
  }
  st.userPolylineOffsets = std::move(newOff);
  if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
    st.userPolylineClosed.erase(st.userPolylineClosed.begin() + static_cast<std::ptrdiff_t>(pi));
  if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
    st.userPolylineAttrs.erase(st.userPolylineAttrs.begin() + static_cast<std::ptrdiff_t>(pi));
}

void EraseCadAnnotationAtIndex(AppCommandState& st, size_t annIndex) {
  if (annIndex >= st.cadAnnotations.size())
    return;
  const CadAnnotation& doomed = st.cadAnnotations[annIndex];
  const int ownerPi = doomed.surveyPointLabelFor;
  if (ownerPi >= 0 && static_cast<size_t>(ownerPi) < st.surveyPoints.size()) {
    SurveyPoint& sp = st.surveyPoints[static_cast<size_t>(ownerPi)];
    if (sp.labelMtextAnnIndex == static_cast<int>(annIndex))
      sp.labelMtextAnnIndex = -1;
  }
  for (SurveyPoint& q : st.surveyPoints) {
    if (q.labelMtextAnnIndex == static_cast<int>(annIndex))
      q.labelMtextAnnIndex = -1;
    else if (q.labelMtextAnnIndex > static_cast<int>(annIndex))
      --q.labelMtextAnnIndex;
  }
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Annotation &&
                                             e.index == static_cast<int>(annIndex);
                                    }),
                     st.selection.end());
  for (SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Annotation && e.index > static_cast<int>(annIndex))
      --e.index;
  }
  st.cadAnnotations.erase(st.cadAnnotations.begin() + static_cast<std::ptrdiff_t>(annIndex));
  if (annIndex < st.cadAnnotationAttrs.size())
    st.cadAnnotationAttrs.erase(st.cadAnnotationAttrs.begin() + static_cast<std::ptrdiff_t>(annIndex));
}

void DeleteSelectedSurveyPoints(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end(), std::greater<int>());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  size_t n = 0;
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size()) {
      RemoveSurveyPointAt(st, static_cast<size_t>(i));
      ++n;
    }
  }
  st.selectedSurveyPointIndices.clear();
  if (n > 0) {
    BumpCadGpuCache(st);
    log.push_back("Deleted " + std::to_string(n) + " survey point(s).");
  }
}

void SyncSurveyPointLinkedMtextSelection(AppCommandState& st, int surveyPointIndex) {
  if (surveyPointIndex < 0 || static_cast<size_t>(surveyPointIndex) >= st.surveyPoints.size())
    return;
  const bool selected =
      std::find(st.selectedSurveyPointIndices.begin(), st.selectedSurveyPointIndices.end(), surveyPointIndex) !=
      st.selectedSurveyPointIndices.end();
  const int annIx = st.surveyPoints[static_cast<size_t>(surveyPointIndex)].labelMtextAnnIndex;
  if (annIx < 0 || static_cast<size_t>(annIx) >= st.cadAnnotations.size())
    return;
  const auto hasAnnSel = [&]() {
    return std::find_if(st.selection.begin(), st.selection.end(), [&](const SelectedEntity& e) {
             return e.type == SelectedEntity::Type::Annotation && e.index == annIx;
           }) != st.selection.end();
  };
  if (selected) {
    if (!hasAnnSel()) {
      SelectedEntity se{};
      se.type = SelectedEntity::Type::Annotation;
      se.index = annIx;
      st.selection.push_back(se);
    }
  } else {
    st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                      [&](const SelectedEntity& e) {
                                        return e.type == SelectedEntity::Type::Annotation && e.index == annIx;
                                      }),
                       st.selection.end());
  }
}

void ApplyLinkedSurveyForAnnotationPick(AppCommandState& st, int annIndex, bool keyShift) {
  if (annIndex < 0 || static_cast<size_t>(annIndex) >= st.cadAnnotations.size())
    return;
  const CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(annIndex)];
  if (a.kind != CadAnnotation::Kind::Mtext || a.surveyPointLabelFor < 0)
    return;
  const int spi = a.surveyPointLabelFor;
  if (static_cast<size_t>(spi) >= st.surveyPoints.size())
    return;
  auto& sv = st.selectedSurveyPointIndices;
  const auto sit = std::find(sv.begin(), sv.end(), spi);
  if (keyShift) {
    if (sit != sv.end())
      sv.erase(sit);
    else
      sv.push_back(spi);
  } else {
    sv.clear();
    sv.push_back(spi);
  }
}

void ExecuteDeleteSelection(AppCommandState& st, std::vector<std::string>& log) {
  if (st.selection.empty())
    return;
  std::set<int> lineIx;
  std::set<int> circIx;
  std::set<int> annIx;
  std::set<int> arcIx;
  std::set<int> ellIx;
  std::set<int> polyIx;
  const size_t nLines = st.userLinesFlat.size() / 6;
  const size_t nCirc = st.userCirclesCxCyR.size() / 3;
  const size_t nAnn = st.cadAnnotations.size();
  const size_t nArc = st.userArcs.size();
  const size_t nEll = st.userEllipses.size();
  const size_t nPoly = st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 && static_cast<size_t>(e.index) < nLines)
      lineIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Circle && e.index >= 0 && static_cast<size_t>(e.index) < nCirc)
      circIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Annotation && e.index >= 0 && static_cast<size_t>(e.index) < nAnn)
      annIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Arc && e.index >= 0 && static_cast<size_t>(e.index) < nArc)
      arcIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Ellipse && e.index >= 0 && static_cast<size_t>(e.index) < nEll)
      ellIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Polyline && e.index >= 0 && static_cast<size_t>(e.index) < nPoly)
      polyIx.insert(e.index);
  }

  std::vector<int> pv(polyIx.begin(), polyIx.end());
  std::sort(pv.begin(), pv.end(), std::greater<int>());
  for (int idx : pv)
    ErasePolylineByIndex(st, idx);

  std::vector<int> lv(lineIx.begin(), lineIx.end());
  std::sort(lv.begin(), lv.end(), std::greater<int>());
  for (int idx : lv) {
    const size_t k = static_cast<size_t>(idx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      continue;
    st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                           st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k + 6));
    if (static_cast<size_t>(idx) < st.userLineAttrs.size())
      st.userLineAttrs.erase(st.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> cv(circIx.begin(), circIx.end());
  std::sort(cv.begin(), cv.end(), std::greater<int>());
  for (int idx : cv) {
    const size_t k = static_cast<size_t>(idx) * 3;
    if (k + 2 >= st.userCirclesCxCyR.size())
      continue;
    st.userCirclesCxCyR.erase(st.userCirclesCxCyR.begin() + static_cast<std::ptrdiff_t>(k),
                              st.userCirclesCxCyR.begin() + static_cast<std::ptrdiff_t>(k + 3));
    if (static_cast<size_t>(idx) < st.userCircleAttrs.size())
      st.userCircleAttrs.erase(st.userCircleAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> av(annIx.begin(), annIx.end());
  std::sort(av.begin(), av.end(), std::greater<int>());
  for (int idx : av)
    EraseCadAnnotationAtIndex(st, static_cast<size_t>(idx));

  std::vector<int> arv(arcIx.begin(), arcIx.end());
  std::sort(arv.begin(), arv.end(), std::greater<int>());
  for (int idx : arv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.userArcs.size())
      continue;
    st.userArcs.erase(st.userArcs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.userArcAttrs.size())
      st.userArcAttrs.erase(st.userArcAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> ev(ellIx.begin(), ellIx.end());
  std::sort(ev.begin(), ev.end(), std::greater<int>());
  for (int idx : ev) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.userEllipses.size())
      continue;
    st.userEllipses.erase(st.userEllipses.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.userEllAttrs.size())
      st.userEllAttrs.erase(st.userEllAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  const size_t nDel = lineIx.size() + circIx.size() + annIx.size() + arcIx.size() + ellIx.size() + polyIx.size();
  st.selection.clear();
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  if (nDel > 0) {
    BumpCadGpuCache(st);
    log.push_back("Deleted " + std::to_string(nDel) + " object(s).");
  }
}

static bool SegSegIntersectParam(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                 float* tAB) {
  const float rx = bx - ax;
  const float ry = by - ay;
  const float sx = dx - cx;
  const float sy = dy - cy;
  const float denom = rx * sy - ry * sx;
  if (std::fabs(denom) < 1e-12f)
    return false;
  /// Solve A + t r = C + u s  =>  t r - u s = C - A  (Cramer's rule on t, u).
  const float wx = cx - ax;
  const float wy = cy - ay;
  const float t = (wx * sy - wy * sx) / denom;
  const float u = (wx * ry - wy * rx) / denom;
  if (t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f) {
    *tAB = t;
    return true;
  }
  return false;
}

struct TrimTargetEdge {
  enum Kind : uint8_t { Line = 0, Poly = 1 } kind = Line;
  int lineIx = -1;
  int polyIx = -1;
  int vLo = -1;
};

static bool SelectedEntityMatches(const SelectedEntity& a, const SelectedEntity& b) {
  return a.type == b.type && a.index == b.index;
}

static void CollectCutSegments(const AppCommandState& st, const SelectedEntity& cut,
                               std::vector<std::array<float, 4>>* out) {
  using ST = SelectedEntity::Type;
  if (cut.type == ST::LineSeg) {
    const size_t k = static_cast<size_t>(cut.index) * 6;
    if (k + 5 < st.userLinesFlat.size())
      out->push_back({st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                      st.userLinesFlat[k + 4]});
    return;
  }
  if (cut.type == ST::Circle) {
    const size_t k = static_cast<size_t>(cut.index) * 3;
    if (k + 2 >= st.userCirclesCxCyR.size())
      return;
    const float cx = st.userCirclesCxCyR[k];
    const float cy = st.userCirclesCxCyR[k + 1];
    const float r = st.userCirclesCxCyR[k + 2];
    constexpr int n = 48;
    const double dcx = static_cast<double>(cx);
    const double dcy = static_cast<double>(cy);
    const double dr = static_cast<double>(r);
    double px = 0.;
    double py = 0.;
    CirclePointWorld(dcx, dcy, dr, 0.0, &px, &py);
    for (int i = 1; i <= n; ++i) {
      constexpr double kTwoPi = 6.283185307179586;
      const double t = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      double x = 0.;
      double y = 0.;
      CirclePointWorld(dcx, dcy, dr, t, &x, &y);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Arc) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k >= st.userArcs.size())
      return;
    const CadArc& a = st.userArcs[k];
    constexpr int n = 40;
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = static_cast<double>(a.r);
    double px = 0.;
    double py = 0.;
    CirclePointWorld(dcx, dcy, dr, static_cast<double>(a.startRad), &px, &py);
    for (int i = 1; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double ang = static_cast<double>(a.startRad + a.sweepRad * u);
      double x = 0.;
      double y = 0.;
      CirclePointWorld(dcx, dcy, dr, ang, &x, &y);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Ellipse) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k >= st.userEllipses.size())
      return;
    const CadEllipse& el = st.userEllipses[k];
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      return;
    const double ux = static_cast<double>(el.majVx) / ma;
    const double uy = static_cast<double>(el.majVy) / ma;
    const double pxv = -uy;
    const double pyv = ux;
    const double mb = ma * static_cast<double>(el.ratio);
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    double px = ecx + ux * ma;
    double py = ecy + uy * ma;
    for (int i = 1; i <= n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      const double x = ecx + ux * (ma * c) + pxv * (mb * s);
      const double y = ecy + uy * (ma * c) + pyv * (mb * s);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Polyline) {
    const int pi = cut.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      return;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      out->push_back({ax, ay, bx, by});
    }
    if (closed && v1 - v0 >= 2) {
      const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      out->push_back({ax, ay, bx, by});
    }
  }
}

static void AppendPolylineCutEdgesExcept(const AppCommandState& st, int pi, int skipEdgeVi,
                                         std::vector<std::array<float, 4>>* out) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
  auto pushEdge = [&](int vi) {
    if (vi == skipEdgeVi)
      return;
    const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
    const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
    const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
    const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
    out->push_back({ax, ay, bx, by});
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    pushEdge(vi);
  if (closed && v1 - v0 >= 2) {
    const int closingVi = v1 - 1;
    if (closingVi != skipEdgeVi) {
      const float ax = st.userPolylineVerts[static_cast<size_t>(closingVi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(closingVi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      out->push_back({ax, ay, bx, by});
    }
  }
}

static void CollectAllDrawingCutSegmentsExceptTarget(const AppCommandState& st, const TrimTargetEdge* excludeEdge,
                                                     std::vector<std::array<float, 4>>* out) {
  out->clear();
  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      const int idx = static_cast<int>(li / 6);
      if (excludeEdge && excludeEdge->kind == TrimTargetEdge::Line && excludeEdge->lineIx == idx)
        continue;
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::LineSeg;
      cut.index = idx;
      CollectCutSegments(st, cut, out);
    }
  }
  const auto& C = st.userCirclesCxCyR;
  if (C.size() % 3 == 0) {
    for (size_t ci = 0; ci + 2 < C.size(); ci += 3) {
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::Circle;
      cut.index = static_cast<int>(ci / 3);
      CollectCutSegments(st, cut, out);
    }
  }
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    SelectedEntity cut{};
    cut.type = SelectedEntity::Type::Arc;
    cut.index = static_cast<int>(ai);
    CollectCutSegments(st, cut, out);
  }
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    SelectedEntity cut{};
    cut.type = SelectedEntity::Type::Ellipse;
    cut.index = static_cast<int>(ei);
    CollectCutSegments(st, cut, out);
  }
  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    if (excludeEdge && excludeEdge->kind == TrimTargetEdge::Poly && excludeEdge->polyIx == pi)
      AppendPolylineCutEdgesExcept(st, pi, excludeEdge->vLo, out);
    else {
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::Polyline;
      cut.index = pi;
      CollectCutSegments(st, cut, out);
    }
  }
}

static void BuildTrimCutSegments(const AppCommandState& st, const std::vector<SelectedEntity>& cutters,
                                 const TrimTargetEdge* excludeEdge, std::vector<std::array<float, 4>>* out) {
  out->clear();
  for (const SelectedEntity& cut : cutters) {
    if (excludeEdge && cut.type == SelectedEntity::Type::LineSeg && excludeEdge->kind == TrimTargetEdge::Line &&
        cut.index == excludeEdge->lineIx)
      continue;
    if (excludeEdge && cut.type == SelectedEntity::Type::Polyline && excludeEdge->kind == TrimTargetEdge::Poly &&
        cut.index == excludeEdge->polyIx) {
      AppendPolylineCutEdgesExcept(st, cut.index, excludeEdge->vLo, out);
      continue;
    }
    CollectCutSegments(st, cut, out);
  }
}

static bool PickClosestTrimTarget(const AppCommandState& st, float wx, float wy, float tolWorld,
                                  TrimTargetEdge* outRef, float* ax, float* ay, float* bx, float* by,
                                  float* outDistSq) {
  if (!outRef || !ax || !ay || !bx || !by || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  TrimTargetEdge bestR{};
  float bax = 0.f, bay = 0.f, bbx = 0.f, bby = 0.f;

  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      const float x0 = Lf[li];
      const float y0 = Lf[li + 1];
      const float x1 = Lf[li + 3];
      const float y1 = Lf[li + 4];
      const float d2 = CadCmdGeom::DistSqPointSegment(wx, wy, x0, y0, x1, y1);
      if (d2 > tol2)
        continue;
      if (!any || d2 < best - 1e-12f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Line;
        bestR.lineIx = static_cast<int>(li / 6);
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    }
  }

  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const float x0 = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float y0 = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float x1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float y1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      const float d2 = CadCmdGeom::DistSqPointSegment(wx, wy, x0, y0, x1, y1);
      if (d2 > tol2)
        return;
      if (!any || d2 < best - 1e-12f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Poly;
        bestR.polyIx = pi;
        bestR.vLo = vi;
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }

  if (!any)
    return false;
  *outRef = bestR;
  *ax = bax;
  *ay = bay;
  *bx = bbx;
  *by = bby;
  *outDistSq = best;
  return true;
}

/// Drawing edge (line or poly segment) whose geometry passes closest to the user-drawn segment \p u1–\p u2.
static bool PickTrimTargetClosestToDrawnSegment(const AppCommandState& st, float u1x, float u1y, float u2x, float u2y,
                                                 float tolWorld, TrimTargetEdge* outRef, float* ax, float* ay,
                                                 float* bx, float* by, float* outDistSq) {
  if (!outRef || !ax || !ay || !bx || !by || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  TrimTargetEdge bestR{};
  float bax = 0.f, bay = 0.f, bbx = 0.f, bby = 0.f;

  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      const float x0 = Lf[li];
      const float y0 = Lf[li + 1];
      const float x1 = Lf[li + 3];
      const float y1 = Lf[li + 4];
      const float d2 = CadCmdGeom::MinDistSqSegSeg(u1x, u1y, u2x, u2y, x0, y0, x1, y1);
      if (d2 > tol2)
        continue;
      if (!any || d2 < best - 1e-18f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Line;
        bestR.lineIx = static_cast<int>(li / 6);
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    }
  }

  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const float x0 = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float y0 = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float x1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float y1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      const float d2 = CadCmdGeom::MinDistSqSegSeg(u1x, u1y, u2x, u2y, x0, y0, x1, y1);
      if (d2 > tol2)
        return;
      if (!any || d2 < best - 1e-18f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Poly;
        bestR.polyIx = pi;
        bestR.vLo = vi;
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }

  if (!any)
    return false;
  *outRef = bestR;
  *ax = bax;
  *ay = bay;
  *bx = bbx;
  *by = bby;
  *outDistSq = best;
  return true;
}

static bool TrimSegmentIntersectPickSide(float ax, float ay, float bx, float by, float pickX, float pickY,
                                         const std::vector<std::array<float, 4>>& cuts, const AppCommandState& st,
                                         float fenceFx, float fenceFy, float fenceGx, float fenceGy,
                                         bool useFenceToPickIntersection,
                                         float* outIx, float* outIy, bool* trimFromA, std::vector<std::string>* log) {
  float epsGeom = 1e-5f;
  double dmnX = 0.;
  double dmxX = 0.;
  double dmnY = 0.;
  double dmxY = 0.;
  if (ComputeWorldExtents(st, &dmnX, &dmxX, &dmnY, &dmxY))
    epsGeom = std::max(1e-8f, static_cast<float>(1e-6 * std::max(dmxX - dmnX, dmxY - dmnY)));

  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    if (log)
      log->push_back("TRIM — degenerate segment.");
    return false;
  }
  const float segLen = std::sqrt(len2);
  const float epsT =
      std::clamp(segLen > 1e-12f ? epsGeom / segLen : 1e-7f, 1e-9f, 0.05f);

  std::vector<float> ts;
  for (const auto& seg : cuts) {
    float t = 0.f;
    if (SegSegIntersectParam(ax, ay, bx, by, seg[0], seg[1], seg[2], seg[3], &t)) {
      if (t > epsT && t < 1.f - epsT)
        ts.push_back(t);
    }
  }
  if (ts.empty()) {
    if (log)
      log->push_back("TRIM — segment does not cross a cutting edge.");
    return false;
  }

  std::sort(ts.begin(), ts.end());
  ts.erase(std::unique(ts.begin(), ts.end(),
                       [&](float a, float b) { return std::fabs(a - b) < std::max(1e-7f, epsGeom / segLen); }),
           ts.end());

  const float u =
      std::clamp(((pickX - ax) * vx + (pickY - ay) * vy) / len2, 0.f, 1.f);

  const float fdx = fenceGx - fenceFx;
  const float fdy = fenceGy - fenceFy;
  const float fenceLen2 = fdx * fdx + fdy * fdy;
  const bool fenceOk = useFenceToPickIntersection && fenceLen2 >= 1e-24f;

  float tNear = ts.front();
  if (fenceOk) {
    float bestFenceD2 = 1e30f;
    float bestPickAbs = 1e30f;
    for (float t : ts) {
      const float ix = ax + t * vx;
      const float iy = ay + t * vy;
      const float df2 = CadCmdGeom::DistSqPointSegment(ix, iy, fenceFx, fenceFy, fenceGx, fenceGy);
      const float dp = std::fabs(t - u);
      if (df2 < bestFenceD2 - 1e-18f ||
          (std::fabs(df2 - bestFenceD2) <= 1e-18f && dp < bestPickAbs - 1e-12f)) {
        bestFenceD2 = df2;
        bestPickAbs = dp;
        tNear = t;
      }
    }
  } else {
    float bestAbs = std::fabs(ts.front() - u);
    for (float t : ts) {
      const float d = std::fabs(t - u);
      if (d < bestAbs - 1e-12f) {
        bestAbs = d;
        tNear = t;
      }
    }
  }

  const float ix = ax + tNear * vx;
  const float iy = ay + tNear * vy;
  const float epsParam = std::max(1e-7f, epsGeom / segLen);

  bool trimA = false;
  if (u < tNear - epsParam)
    trimA = true;
  else if (u > tNear + epsParam)
    trimA = false;
  else {
    const float dA = (pickX - ax) * (pickX - ax) + (pickY - ay) * (pickY - ay);
    const float dB = (pickX - bx) * (pickX - bx) + (pickY - by) * (pickY - by);
    trimA = dA <= dB;
  }

  *outIx = ix;
  *outIy = iy;
  *trimFromA = trimA;
  return true;
}

/// AutoCAD-style: shorten segment toward the nearest cutting intersection from the pick (portion containing pick is
/// removed).
static bool TrimSegmentToCuttingEdges(AppCommandState& st, const TrimTargetEdge& tgt, float ax, float ay, float bx,
                                      float by, float pickX, float pickY,
                                      const std::vector<std::array<float, 4>>& cuts, bool useFence,
                                      float fenceFx, float fenceFy, float fenceGx, float fenceGy,
                                      std::vector<std::string>& log) {
  float ix = 0.f, iy = 0.f;
  bool trimA = false;
  if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickX, pickY, cuts, st, fenceFx, fenceFy, fenceGx, fenceGy,
                                    useFence, &ix, &iy, &trimA, &log))
    return false;

  const float rx = ix;
  const float ry = iy;

  if (tgt.kind == TrimTargetEdge::Line) {
    const size_t k = static_cast<size_t>(tgt.lineIx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return false;
    if (trimA) {
      st.userLinesFlat[k] = rx;
      st.userLinesFlat[k + 1] = ry;
    } else {
      st.userLinesFlat[k + 3] = rx;
      st.userLinesFlat[k + 4] = ry;
    }
    const float exx = st.userLinesFlat[k + 3] - st.userLinesFlat[k];
    const float eyy = st.userLinesFlat[k + 4] - st.userLinesFlat[k + 1];
    if (exx * exx + eyy * eyy < 1e-12f) {
      st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                             st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k + 6));
      if (static_cast<size_t>(tgt.lineIx) < st.userLineAttrs.size())
        st.userLineAttrs.erase(st.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(tgt.lineIx));
    }
  } else {
    const int vi = trimA ? tgt.vLo : tgt.vLo + 1;
    const size_t vk = static_cast<size_t>(vi * 3);
    if (vk + 1 >= st.userPolylineVerts.size())
      return false;
    st.userPolylineVerts[vk] = rx;
    st.userPolylineVerts[vk + 1] = ry;
  }

  log.push_back("TRIM — segment shortened.");
  return true;
}

bool PickClosestCadEntity(const AppCommandState& st, float wx, float wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq) {
  if (!out || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  SelectedEntity bestE{};
  auto consider = [&](const SelectedEntity& e, float d2) {
    if (d2 > tol2)
      return;
    if (!any || d2 < best - 1e-12f) {
      any = true;
      best = d2;
      bestE = e;
    }
  };

  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::LineSeg;
      e.index = static_cast<int>(i / 6);
      const float d2 =
          CadCmdGeom::DistSqPointSegment(wx, wy, L[i], L[i + 1], L[i + 3], L[i + 4]);
      consider(e, d2);
    }
  }
  const auto& C = st.userCirclesCxCyR;
  if (C.size() % 3 == 0) {
    for (size_t ci = 0; ci + 2 < C.size(); ci += 3) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Circle;
      e.index = static_cast<int>(ci / 3);
      const float cx = C[ci];
      const float cy = C[ci + 1];
      const float r = C[ci + 2];
      const float d = std::hypot(wx - cx, wy - cy);
      const float dr = d - r;
      consider(e, dr * dr);
    }
  }
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    const CadArc& a = st.userArcs[ai];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Arc;
    e.index = static_cast<int>(ai);
    float bestD2 = 1e30f;
    constexpr int n = 36;
    for (int i = 0; i <= n; ++i) {
      const float u = static_cast<float>(i) / static_cast<float>(n);
      const float ang = a.startRad + a.sweepRad * u;
      const float x = a.cx + a.r * std::cos(ang);
      const float y = a.cy + a.r * std::sin(ang);
      const float dx = wx - x;
      const float dy = wy - y;
      bestD2 = std::min(bestD2, dx * dx + dy * dy);
    }
    consider(e, bestD2);
  }
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    const CadEllipse& el = st.userEllipses[ei];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Ellipse;
    e.index = static_cast<int>(ei);
    const float ma = std::hypot(el.majVx, el.majVy);
    float bestD2 = 1e30f;
    if (ma >= 1e-8f) {
      const float ux = el.majVx / ma;
      const float uy = el.majVy / ma;
      const float px = -uy;
      const float py = ux;
      const float mb = ma * el.ratio;
      constexpr int n = 36;
      constexpr float twopi = 6.28318530718f;
      for (int i = 0; i <= n; ++i) {
        const float ang = twopi * static_cast<float>(i) / static_cast<float>(n);
        const float c = std::cos(ang);
        const float s = std::sin(ang);
        const float x = el.cx + ux * (ma * c) + px * (mb * s);
        const float y = el.cy + uy * (ma * c) + py * (mb * s);
        const float dx = wx - x;
        const float dy = wy - y;
        bestD2 = std::min(bestD2, dx * dx + dy * dy);
      }
    }
    consider(e, bestD2);
  }
  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Polyline;
    e.index = pi;
    float bestD2 = 1e30f;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      bestD2 = std::min(bestD2, CadCmdGeom::DistSqPointSegment(wx, wy, ax, ay, bx, by));
    }
    if (closed && v1 - v0 >= 2) {
      const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      bestD2 = std::min(bestD2, CadCmdGeom::DistSqPointSegment(wx, wy, ax, ay, bx, by));
    }
    consider(e, bestD2);
  }

  if (!any)
    return false;
  *out = bestE;
  *outDistSq = best;
  return true;
}

namespace {

static void OfsClosestPtSeg(float ax, float ay, float bx, float by, float px, float py, float* qx, float* qy) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *qx = ax;
    *qy = ay;
    return;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  *qx = ax + t * vx;
  *qy = ay + t * vy;
}

static void OfsUnitLeftNormal(float ax, float ay, float bx, float by, float* nx, float* ny) {
  float vx = bx - ax;
  float vy = by - ay;
  const float len = std::hypot(vx, vy);
  if (len < 1e-12f) {
    *nx = 0.f;
    *ny = 1.f;
    return;
  }
  vx /= len;
  vy /= len;
  *nx = -vy;
  *ny = vx;
}

static float OfsSignedSideLine(float ax, float ay, float bx, float by, float px, float py) {
  float qx = 0.f, qy = 0.f;
  OfsClosestPtSeg(ax, ay, bx, by, px, py, &qx, &qy);
  float nx = 0.f, ny = 0.f;
  OfsUnitLeftNormal(ax, ay, bx, by, &nx, &ny);
  return (px - qx) * nx + (py - qy) * ny;
}

static float OfsSignedSideCircle(float cx, float cy, float r, float px, float py) {
  const float d = std::hypot(px - cx, py - cy);
  return d - r;
}

static bool OfsLineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                    float* ox, float* oy) {
  const float rx = bx - ax, ry = by - ay;
  const float sx = dx - cx, sy = dy - cy;
  const float det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-12f * std::max(1.f, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const float t = ((cx - ax) * sy - (cy - ay) * sx) / det;
  *ox = ax + t * rx;
  *oy = ay + t * ry;
  return true;
}

static bool TryOffsetSignedDFromCursor(const AppCommandState& st, float px, float py, float* signedDOut) {
  using OP = AppCommandState::OffsetPhase;
  using T = SelectedEntity::Type;
  if (!st.offsetEntityValid)
    return false;
  const SelectedEntity& e = st.offsetEntity;
  if (st.offsetPhase == OP::WaitSidePick) {
    if (st.offsetTypedDistance <= 0.f)
      return false;
    const float d = st.offsetTypedDistance;
    float sgn = 1.f;
    switch (e.type) {
    case T::LineSeg: {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= st.userLinesFlat.size())
        return false;
      const float sd = OfsSignedSideLine(st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                                         st.userLinesFlat[k + 4], px, py);
      sgn = sd >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Circle: {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= st.userCirclesCxCyR.size())
        return false;
      const float cx = st.userCirclesCxCyR[k];
      const float cy = st.userCirclesCxCyR[k + 1];
      const float r = st.userCirclesCxCyR[k + 2];
      const float side = OfsSignedSideCircle(cx, cy, r, px, py);
      sgn = side >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Arc: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
        return false;
      const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
      const float side = OfsSignedSideCircle(a.cx, a.cy, a.r, px, py);
      sgn = side >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Ellipse:
    case T::Polyline: {
      if (e.type == T::Polyline) {
        const int pi = e.index;
        if (pi >= 0 && static_cast<size_t>(pi + 1) < st.userPolylineOffsets.size()) {
          const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
          const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
          float best = 1e30f;
          float bestS = 1.f;
          for (int vi = v0; vi + 1 < v1; ++vi) {
            const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
            const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
            const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
            const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
            float qx = 0.f, qy = 0.f;
            OfsClosestPtSeg(ax, ay, bx, by, px, py, &qx, &qy);
            const float sd = OfsSignedSideLine(ax, ay, bx, by, px, py);
            const float dx = px - qx;
            const float dy = py - qy;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < best) {
              best = dist2;
              bestS = sd >= 0.f ? 1.f : -1.f;
            }
          }
          sgn = bestS;
        }
      } else if (e.index >= 0 && static_cast<size_t>(e.index) < st.userEllipses.size()) {
        const CadEllipse& el = st.userEllipses[static_cast<size_t>(e.index)];
        const float ma = std::hypot(el.majVx, el.majVy);
        if (ma >= 1e-8f) {
          const float ux = el.majVx / ma;
          const float uy = el.majVy / ma;
          const float pxn = -uy;
          const float pyn = ux;
          const float mb = ma * el.ratio;
          constexpr float twopi = 6.28318530718f;
          float best = 1e30f;
          float bx = el.cx, by = el.cy;
          for (int i = 0; i <= 48; ++i) {
            const float ang = twopi * static_cast<float>(i) / 48.f;
            const float c0 = std::cos(ang);
            const float s0 = std::sin(ang);
            const float ex = el.cx + ux * (ma * c0) + pxn * (mb * s0);
            const float ey = el.cy + uy * (ma * c0) + pyn * (mb * s0);
            const float dx = px - ex;
            const float dy = py - ey;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < best) {
              best = dist2;
              bx = ex;
              by = ey;
            }
          }
          const float ox = bx - el.cx;
          const float oy = by - el.cy;
          const float inX = px - el.cx;
          const float inY = py - el.cy;
          sgn = (inX * ox + inY * oy) >= 0.f ? 1.f : -1.f;
        }
      }
      break;
    }
    default:
      return false;
    }
    *signedDOut = d * sgn;
    return true;
  }
  if (st.offsetPhase == OP::WaitDistanceOrThrough) {
    float signedD = 0.f;
    switch (e.type) {
    case T::LineSeg: {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= st.userLinesFlat.size())
        return false;
      signedD = OfsSignedSideLine(st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                                  st.userLinesFlat[k + 4], px, py);
      break;
    }
    case T::Circle: {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= st.userCirclesCxCyR.size())
        return false;
      signedD = OfsSignedSideCircle(st.userCirclesCxCyR[k], st.userCirclesCxCyR[k + 1], st.userCirclesCxCyR[k + 2], px,
                                  py);
      break;
    }
    case T::Arc: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
        return false;
      const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
      signedD = OfsSignedSideCircle(a.cx, a.cy, a.r, px, py);
      break;
    }
    default:
      return false;
    }
    if (std::fabs(signedD) < 1e-7f)
      return false;
    *signedDOut = signedD;
    return true;
  }
  return false;
}

static void AppendArcPreviewStrip(float z, const CadArc& a, std::vector<float>* lines) {
  AppendArcLineSegments(*lines, static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.r),
                        static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), 56, z);
}

static void AppendEllipsePreviewStrip(float z, const CadEllipse& el, std::vector<float>* lines) {
  AppendEllipseLineSegments(*lines, static_cast<double>(el.cx), static_cast<double>(el.cy),
                            static_cast<double>(el.majVx), static_cast<double>(el.majVy),
                            static_cast<double>(el.ratio), 56, z);
}

static void AppendPolylineOffsetPreview(const AppCommandState& st, int pi, float signedD, float z,
                                        std::vector<float>* lines) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const int nv = v1 - v0;
  if (nv < 2)
    return;
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];

  std::vector<std::pair<float, float>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i)
    v.push_back({st.userPolylineVerts[static_cast<size_t>(i * 3)], st.userPolylineVerts[static_cast<size_t>(i * 3 + 1)]});

  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  if (nEdges < 1)
    return;

  std::vector<std::pair<float, float>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    const float ax = v[static_cast<size_t>(ia)].first;
    const float ay = v[static_cast<size_t>(ia)].second;
    const float bx = v[static_cast<size_t>(ib)].first;
    const float by = v[static_cast<size_t>(ib)].second;
    float nx = 0.f, ny = 0.f;
    OfsUnitLeftNormal(ax, ay, bx, by, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {ax + nx * signedD, ay + ny * signedD};
    pb[static_cast<size_t>(ei)] = {bx + nx * signedD, by + ny * signedD};
  }

  std::vector<std::pair<float, float>> out;
  if (!closed) {
    if (nEdges == 1) {
      out.push_back(pa[0]);
      out.push_back(pb[0]);
    } else {
      out.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        const auto& a0 = pa[static_cast<size_t>(ei)];
        const auto& b0 = pb[static_cast<size_t>(ei)];
        const auto& a1 = pa[static_cast<size_t>(ei + 1)];
        const auto& b1 = pb[static_cast<size_t>(ei + 1)];
        float ix = 0.f, iy = 0.f;
        if (OfsLineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                    &ix, &iy))
          out.push_back({ix, iy});
        else {
          out.push_back({0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)});
        }
      }
      out.push_back(pb[static_cast<size_t>(nEdges - 1)]);
    }
  } else {
    out.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      const auto& a0 = pa[static_cast<size_t>(ei)];
      const auto& b0 = pb[static_cast<size_t>(ei)];
      const auto& a1 = pa[static_cast<size_t>(en)];
      const auto& b1 = pb[static_cast<size_t>(en)];
      float ix = 0.f, iy = 0.f;
      if (OfsLineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                  &ix, &iy))
        out[static_cast<size_t>(ei)] = {ix, iy};
      else
        out[static_cast<size_t>(ei)] = {0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)};
    }
  }
  if (out.size() < 2)
    return;
  for (size_t i = 0; i + 1 < out.size(); ++i) {
    lines->push_back(out[i].first);
    lines->push_back(out[i].second);
    lines->push_back(z);
    lines->push_back(out[i + 1].first);
    lines->push_back(out[i + 1].second);
    lines->push_back(z);
  }
  if (closed && out.size() >= 2) {
    lines->push_back(out.back().first);
    lines->push_back(out.back().second);
    lines->push_back(z);
    lines->push_back(out[0].first);
    lines->push_back(out[0].second);
    lines->push_back(z);
  }
}

} // namespace

float CadOffsetEntityPickTolWorld(const AppCommandState& st) {
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  float geom = 1e-3f;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    geom = std::max(1e-5f, static_cast<float>(2.5e-5 * std::max(mxX - mnX, mxY - mnY)));
  const float px = CadSnap::WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                     st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx);
  return std::max(geom, px * 1.5f);
}

void CadOffsetAppendLivePreview(const AppCommandState& cmd, float cursorWx, float cursorWy,
                                std::vector<float>* previewLines, std::vector<float>* previewCircles) {
  if (!previewLines || !previewCircles)
    return;
  previewLines->clear();
  previewCircles->clear();
  if (!cmd.offsetEntityValid)
    return;
  float signedD = 0.f;
  if (!TryOffsetSignedDFromCursor(cmd, cursorWx, cursorWy, &signedD))
    return;

  using T = SelectedEntity::Type;
  const SelectedEntity& e = cmd.offsetEntity;
  constexpr float zl = 0.022f;

  switch (e.type) {
  case T::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= cmd.userLinesFlat.size())
      return;
    const float x0 = cmd.userLinesFlat[k];
    const float y0 = cmd.userLinesFlat[k + 1];
    const float x1 = cmd.userLinesFlat[k + 3];
    const float y1 = cmd.userLinesFlat[k + 4];
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    if (std::hypot(dx, dy) < 1e-8f)
      return;
    float nx = 0.f, ny = 0.f;
    OfsUnitLeftNormal(x0, y0, x1, y1, &nx, &ny);
    previewLines->push_back(x0 + nx * signedD);
    previewLines->push_back(y0 + ny * signedD);
    previewLines->push_back(zl);
    previewLines->push_back(x1 + nx * signedD);
    previewLines->push_back(y1 + ny * signedD);
    previewLines->push_back(zl);
    break;
  }
  case T::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 3;
    if (k + 2 >= cmd.userCirclesCxCyR.size())
      return;
    const float cx = cmd.userCirclesCxCyR[k];
    const float cy = cmd.userCirclesCxCyR[k + 1];
    const float r = cmd.userCirclesCxCyR[k + 2];
    const float nr = r + signedD;
    if (nr <= 1e-6f)
      return;
    previewCircles->push_back(cx);
    previewCircles->push_back(cy);
    previewCircles->push_back(nr);
    break;
  }
  case T::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size())
      return;
    CadArc o = cmd.userArcs[static_cast<size_t>(e.index)];
    o.r += signedD;
    if (o.r <= 1e-6f)
      return;
    AppendArcPreviewStrip(zl, o, previewLines);
    break;
  }
  case T::Ellipse: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size())
      return;
    const CadEllipse& el0 = cmd.userEllipses[static_cast<size_t>(e.index)];
    const float ma = std::hypot(el0.majVx, el0.majVy);
    if (ma < 1e-8f || ma + signedD <= 1e-6f)
      return;
    CadEllipse el = el0;
    const float f = (ma + signedD) / ma;
    el.majVx *= f;
    el.majVy *= f;
    AppendEllipsePreviewStrip(zl, el, previewLines);
    break;
  }
  case T::Polyline:
    AppendPolylineOffsetPreview(cmd, e.index, signedD, zl, previewLines);
    break;
  default:
    break;
  }
}

void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut) {
  if (!previewLinesOut)
    return;

  const auto pushRemoved = [&](const TrimTargetEdge& tgt, float ax, float ay, float bx, float by) {
    std::vector<std::array<float, 4>> cuts;
    CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
    if (cuts.empty())
      return;
    float ix = 0.f, iy = 0.f;
    bool trimA = false;
    if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickPreviewX, pickPreviewY, cuts, st, fenceP1x, fenceP1y,
                                      fenceP2x, fenceP2y, true, &ix, &iy, &trimA, nullptr))
      return;
    if (trimA) {
      previewLinesOut->push_back(ax);
      previewLinesOut->push_back(ay);
      previewLinesOut->push_back(0.f);
      previewLinesOut->push_back(ix);
      previewLinesOut->push_back(iy);
      previewLinesOut->push_back(0.f);
    } else {
      previewLinesOut->push_back(ix);
      previewLinesOut->push_back(iy);
      previewLinesOut->push_back(0.f);
      previewLinesOut->push_back(bx);
      previewLinesOut->push_back(by);
      previewLinesOut->push_back(0.f);
    }
  };

  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      TrimTargetEdge tgt{};
      tgt.kind = TrimTargetEdge::Line;
      tgt.lineIx = static_cast<int>(li / 6);
      pushRemoved(tgt, Lf[li], Lf[li + 1], Lf[li + 3], Lf[li + 4]);
    }
  }

  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      TrimTargetEdge tgt{};
      tgt.kind = TrimTargetEdge::Poly;
      tgt.polyIx = pi;
      tgt.vLo = vi;
      const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      pushRemoved(tgt, ax, ay, bx, by);
    }
    if (closed && v1 - v0 >= 2) {
      TrimTargetEdge tgt{};
      tgt.kind = TrimTargetEdge::Poly;
      tgt.polyIx = pi;
      tgt.vLo = v1 - 1;
      const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      pushRemoved(tgt, ax, ay, bx, by);
    }
  }
}

static void ExecuteDrawnSegmentTrimOnce(AppCommandState& st, float p1x, float p1y, float p2x, float p2y,
                                        float tolWorld, std::vector<std::string>& log) {
  float matchTol = std::max(tolWorld * 4.f, 1e-6f);
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    matchTol = std::max(matchTol, static_cast<float>(2e-5 * std::max(mxX - mnX, mxY - mnY)));
  TrimTargetEdge tgt{};
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f, dEdge = 0.f;
  if (!PickTrimTargetClosestToDrawnSegment(st, p1x, p1y, p2x, p2y, matchTol, &tgt, &ax, &ay, &bx, &by, &dEdge)) {
    log.push_back("TRIM — no segment close enough to your line (draw along the edge to shorten).");
    return;
  }
  std::vector<std::array<float, 4>> cuts;
  CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
  if (cuts.empty()) {
    log.push_back("TRIM — nothing crosses that segment.");
    return;
  }
  const float pmx = (p1x + p2x) * 0.5f;
  const float pmy = (p1y + p2y) * 0.5f;
  if (!TrimSegmentToCuttingEdges(st, tgt, ax, ay, bx, by, pmx, pmy, cuts, true, p1x, p1y, p2x, p2y, log))
    return;
  BumpCadGpuCache(st);
}

bool SubmitTrimViewportPick(AppCommandState& st, float wx, float wy, float tolWorld, std::vector<std::string>& log) {
  ClearPendingOneShotObjectSnap(st);
  using K = AppCommandState::Kind;
  using TP = AppCommandState::TrimPhase;
  if (st.active != K::Trim)
    return false;

  if (st.trimPhase == TP::CuttingLine_WaitP1) {
    st.trimCutInfP1x = wx;
    st.trimCutInfP1y = wy;
    st.trimPhase = TP::CuttingLine_WaitP2;
    log.push_back("TRIM — second point: finishes trim on nearest edge along your line (dashed preview).");
    return true;
  }

  if (st.trimPhase == TP::CuttingLine_WaitP2) {
    float p2x = wx;
    float p2y = wy;
    if (st.orthoMode) {
      const float dx = p2x - st.trimCutInfP1x;
      const float dy = p2y - st.trimCutInfP1y;
      if (std::fabs(dx) >= std::fabs(dy))
        p2y = st.trimCutInfP1y;
      else
        p2x = st.trimCutInfP1x;
    }
    const float ddx = p2x - st.trimCutInfP1x;
    const float ddy = p2y - st.trimCutInfP1y;
    if (ddx * ddx + ddy * ddy < 1e-18f) {
      log.push_back("TRIM — line too short.");
      return false;
    }
    st.trimCutInfP2x = p2x;
    st.trimCutInfP2y = p2y;
    ExecuteDrawnSegmentTrimOnce(st, st.trimCutInfP1x, st.trimCutInfP1y, p2x, p2y, tolWorld, log);
    st.trimCutters.clear();
    st.trimPhase = TP::SelectCuttingEdges;
    st.active = K::None;
    return true;
  }

  if (st.trimPhase == TP::SelectCuttingEdges) {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, tolWorld, &hit, &d2)) {
      log.push_back("TRIM — no object at pick.");
      return false;
    }
    if (hit.type == SelectedEntity::Type::Annotation) {
      log.push_back("TRIM — use a line, circle, arc, ellipse, or polyline as a cutting edge.");
      return false;
    }
    for (const auto& c : st.trimCutters) {
      if (SelectedEntityMatches(c, hit)) {
        log.push_back("TRIM — already a cutting edge.");
        return true;
      }
    }
    st.trimCutters.push_back(hit);
    log.push_back("TRIM — cutting edge added.");
    return true;
  }

  TrimTargetEdge tgt{};
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f, d2 = 0.f;
  if (!PickClosestTrimTarget(st, wx, wy, tolWorld, &tgt, &ax, &ay, &bx, &by, &d2)) {
    log.push_back("TRIM — nothing to trim at pick.");
    return false;
  }

  std::vector<std::array<float, 4>> cuts;
  BuildTrimCutSegments(st, st.trimCutters, &tgt, &cuts);
  if (cuts.empty()) {
    log.push_back("TRIM — no cutting segments (check cutting edges).");
    return false;
  }

  if (!TrimSegmentToCuttingEdges(st, tgt, ax, ay, bx, by, wx, wy, cuts, false, 0.f, 0.f, 0.f, 0.f, log))
    return true;
  BumpCadGpuCache(st);
  return true;
}

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log) {
  using ST = SelectedEntity::Type;
  struct Edge {
    float x0, y0, x1, y1;
    int lineIx;
    int polyIx;
  };
  std::vector<Edge> edges;
  float tol = 1e-3f;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    tol = std::max(1e-5f, static_cast<float>(1e-4 * std::max(mxX - mnX, mxY - mnY)));

  auto readLine = [&](int idx, float* x0, float* y0, float* x1, float* y1) -> bool {
    const size_t k = static_cast<size_t>(idx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return false;
    *x0 = st.userLinesFlat[k];
    *y0 = st.userLinesFlat[k + 1];
    *x1 = st.userLinesFlat[k + 3];
    *y1 = st.userLinesFlat[k + 4];
    return true;
  };

  for (const auto& se : st.selection) {
    if (se.type == ST::LineSeg && se.index >= 0) {
      float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
      if (!readLine(se.index, &x0, &y0, &x1, &y1))
        continue;
      edges.push_back({x0, y0, x1, y1, se.index, -1});
    } else if (se.type == ST::Polyline && se.index >= 0) {
      const int pi = se.index;
      if (static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
      for (int vi = v0; vi + 1 < v1; ++vi) {
        const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
        const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
        const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
        edges.push_back({ax, ay, bx, by, -1, pi});
      }
      if (closed && v1 - v0 >= 2) {
        const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
        const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
        const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
        const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
        edges.push_back({ax, ay, bx, by, -1, pi});
      }
    }
  }

  if (edges.size() < 2) {
    log.push_back("JOIN — select at least two connected lines or polylines.");
    st.selection.clear();
    return;
  }

  const int n = static_cast<int>(edges.size());
  struct UF {
    std::vector<int> p;
    explicit UF(int nn) : p(static_cast<size_t>(nn)) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) {
      return p[static_cast<size_t>(x)] == x ? x : (p[static_cast<size_t>(x)] = find(p[static_cast<size_t>(x)]));
    }
    void unite(int a, int b) {
      a = find(a);
      b = find(b);
      if (a != b)
        p[static_cast<size_t>(a)] = b;
    }
  };
  UF uf(2 * n);
  auto nearPt = [&](float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy <= tol * tol;
  };
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const Edge& A = edges[static_cast<size_t>(i)];
      const Edge& B = edges[static_cast<size_t>(j)];
      if (nearPt(A.x0, A.y0, B.x0, B.y0))
        uf.unite(2 * i, 2 * j);
      if (nearPt(A.x0, A.y0, B.x1, B.y1))
        uf.unite(2 * i, 2 * j + 1);
      if (nearPt(A.x1, A.y1, B.x0, B.y0))
        uf.unite(2 * i + 1, 2 * j);
      if (nearPt(A.x1, A.y1, B.x1, B.y1))
        uf.unite(2 * i + 1, 2 * j + 1);
    }
  }

  auto clusterOf = [&](int ep) { return uf.find(ep); };
  std::vector<char> edgeUsed(static_cast<size_t>(n), 0);
  std::unordered_set<int> lineDel;
  std::unordered_set<int> polyDel;
  int polysOut = 0;

  for (int ei = 0; ei < n; ++ei) {
    if (edgeUsed[static_cast<size_t>(ei)])
      continue;
    std::vector<int> comp;
    std::vector<int> stk = {ei};
    edgeUsed[static_cast<size_t>(ei)] = 1;
    while (!stk.empty()) {
      const int cur = stk.back();
      stk.pop_back();
      comp.push_back(cur);
      const int cua = clusterOf(2 * cur);
      const int cub = clusterOf(2 * cur + 1);
      for (int ej = 0; ej < n; ++ej) {
        if (edgeUsed[static_cast<size_t>(ej)])
          continue;
        const int cva = clusterOf(2 * ej);
        const int cvb = clusterOf(2 * ej + 1);
        if (cua == cva || cua == cvb || cub == cva || cub == cvb) {
          edgeUsed[static_cast<size_t>(ej)] = 1;
          stk.push_back(ej);
        }
      }
    }

    std::unordered_map<int, std::pair<float, float>> rep;
    for (int ej : comp) {
      const Edge& E = edges[static_cast<size_t>(ej)];
      const int k0 = clusterOf(2 * ej);
      const int k1 = clusterOf(2 * ej + 1);
      if (!rep.count(k0))
        rep[k0] = {E.x0, E.y0};
      if (!rep.count(k1))
        rep[k1] = {E.x1, E.y1};
    }

    std::unordered_map<int, int> deg;
    for (int ej : comp) {
      const int u = clusterOf(2 * ej);
      const int v = clusterOf(2 * ej + 1);
      deg[u]++;
      deg[v]++;
    }
    int odd = 0;
    for (const auto& kv : deg)
      if (kv.second % 2 == 1)
        odd++;
    if (odd != 0 && odd != 2) {
      log.push_back("JOIN — skipped a group (branching junction).");
      continue;
    }

    std::vector<int> clusters;
    clusters.reserve(rep.size());
    for (const auto& kv : rep)
      clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());
    std::unordered_map<int, int> dense;
    for (size_t i = 0; i < clusters.size(); ++i)
      dense[clusters[static_cast<size_t>(i)]] = static_cast<int>(i);
    const int K = static_cast<int>(clusters.size());
    std::vector<std::vector<std::pair<int, int>>> adj(static_cast<size_t>(K));
    std::vector<char> eu(static_cast<size_t>(n), 0);
    for (int ej : comp) {
      const int u = dense[clusterOf(2 * ej)];
      const int v = dense[clusterOf(2 * ej + 1)];
      adj[static_cast<size_t>(u)].push_back({v, ej});
      adj[static_cast<size_t>(v)].push_back({u, ej});
    }

    int start = 0;
    if (odd == 2) {
      start = -1;
      for (const auto& kv : deg) {
        if (kv.second % 2 == 1) {
          start = dense[kv.first];
          break;
        }
      }
      if (start < 0)
        start = 0;
    } else if (!comp.empty())
      start = dense[clusterOf(2 * comp[0])];

    std::vector<std::vector<std::pair<int, int>>> adjW = adj;
    std::vector<int> stkE = {start};
    std::vector<int> pathVerts;
    while (!stkE.empty()) {
      const int v = stkE.back();
      while (!adjW[static_cast<size_t>(v)].empty() &&
             eu[static_cast<size_t>(adjW[static_cast<size_t>(v)].back().second)])
        adjW[static_cast<size_t>(v)].pop_back();
      if (adjW[static_cast<size_t>(v)].empty()) {
        pathVerts.push_back(v);
        stkE.pop_back();
      } else {
        const auto pr = adjW[static_cast<size_t>(v)].back();
        adjW[static_cast<size_t>(v)].pop_back();
        const int to = pr.first;
        const int eix = pr.second;
        if (eu[static_cast<size_t>(eix)])
          continue;
        eu[static_cast<size_t>(eix)] = 1;
        stkE.push_back(to);
      }
    }
    std::reverse(pathVerts.begin(), pathVerts.end());
    if (pathVerts.size() < 2)
      continue;

    std::vector<float> pv;
    auto appendCluster = [&](int d) {
      const int cid = clusters[static_cast<size_t>(d)];
      const auto& pt = rep[cid];
      if (!pv.empty()) {
        const size_t z = pv.size();
        if (z >= 3 && pv[z - 3] == pt.first && pv[z - 2] == pt.second)
          return;
      }
      pv.push_back(pt.first);
      pv.push_back(pt.second);
      pv.push_back(0.f);
    };
    for (const int d : pathVerts)
      appendCluster(d);

    bool closed = pathVerts.front() == pathVerts.back();
    if (closed && pv.size() >= 9)
      pv.resize(pv.size() - 3);

    if (pv.size() < 6)
      continue;

    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(0);
    const int baseVert = st.userPolylineOffsets.back();
    const int nv = static_cast<int>(pv.size() / 3);
    st.userPolylineVerts.insert(st.userPolylineVerts.end(), pv.begin(), pv.end());
    st.userPolylineOffsets.push_back(baseVert + nv);
    st.userPolylineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    polysOut++;

    for (int ej : comp) {
      const Edge& E = edges[static_cast<size_t>(ej)];
      if (E.lineIx >= 0)
        lineDel.insert(E.lineIx);
      if (E.polyIx >= 0)
        polyDel.insert(E.polyIx);
    }
  }

  std::vector<int> pDel(polyDel.begin(), polyDel.end());
  std::sort(pDel.begin(), pDel.end(), std::greater<int>());
  for (int ix : pDel)
    ErasePolylineByIndex(st, ix);

  std::vector<int> lDel(lineDel.begin(), lineDel.end());
  std::sort(lDel.begin(), lDel.end(), std::greater<int>());
  for (int idx : lDel) {
    const size_t k = static_cast<size_t>(idx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      continue;
    st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                           st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k + 6));
    if (static_cast<size_t>(idx) < st.userLineAttrs.size())
      st.userLineAttrs.erase(st.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  st.selection.clear();
  if (polysOut > 0) {
    BumpCadGpuCache(st);
    log.push_back("JOIN — created " + std::to_string(polysOut) + " polyline(s).");
  } else
    log.push_back("JOIN — nothing merged.");
}

void StartJoinCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  if (!st.selection.empty()) {
    ExecuteJoinSelection(st, log);
    return;
  }
  st.active = AppCommandState::Kind::Join;
  st.selBoxWaitingSecond = false;
  log.push_back("JOIN — window-select lines/polylines that meet at endpoints. ESC cancels.");
}

void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Trim;
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  st.selBoxWaitingSecond = false;
  log.push_back(
      "TRIM — pick cutting edges, Enter; trim clicks — or type L, draw on the segment to trim (two clicks). ESC "
      "cancels.");
}

void StartDeleteCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  if (!st.selection.empty()) {
    ExecuteDeleteSelection(st, log);
    return;
  }
  if (!st.selectedSurveyPointIndices.empty()) {
    DeleteSelectedSurveyPoints(st, log);
    return;
  }
  st.active = AppCommandState::Kind::Delete;
  st.selBoxWaitingSecond = false;
  log.push_back("DELETE — click two corners to window-select objects to erase. ESC cancels.");
}

void StartZoomExtentsCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("ZOOM EXTENTS — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  st.pendingZoomExtents = true;
}

void StartZoomWindowCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None && st.active != K::Zoom) {
    log.push_back("ZOOM WINDOW — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.active = K::Zoom;
  st.selBoxWaitingSecond = false;
  log.push_back(
      "ZOOM WINDOW — click two corners (crossing window corners use cursor position, not object snap). ESC "
      "cancels.");
}

void ProcessPendingViewportZoom(AppCommandState& st, double* panX, double* panY, float* zoom, int fbW, int fbH,
                                float viewportAspect, std::vector<std::string>& log) {
  if (fbW <= 0 || fbH <= 0)
    return;
  if (st.pendingZoomExtents) {
    double mnX = 0.;
    double mxX = 0.;
    double mnY = 0.;
    double mxY = 0.;
    int skipped = 0;
    if (!ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped)) {
      st.pendingZoomExtents = false;
      log.push_back("ZOOM EXTENTS — nothing to frame.");
      return;
    }
    ApplyViewportZoomToWorldRect(mnX, mxX, mnY, mxY, &st.viewportPanX, &st.viewportPanY, &st.viewportZoom, fbW, fbH,
                                 viewportAspect);
    BumpCadGpuCache(st);
    if (panX)
      *panX = st.viewportPanX;
    if (panY)
      *panY = st.viewportPanY;
    if (zoom)
      *zoom = st.viewportZoom;
    st.pendingZoomExtents = false;
    char buf[256];
    const double spanX = mxX - mnX;
    const double spanY = mxY - mnY;
    std::snprintf(buf, sizeof(buf),
                  "Zoom extents applied — span %.6g x %.6g (local %.6g..%.6g, %.6g..%.6g) zoom=%.6g skipped=%d.",
                  spanX, spanY, mnX, mxX, mnY, mxY, static_cast<double>(st.viewportZoom), skipped);
    log.push_back(buf);
    return;
  }
  if (st.pendingZoomWindow) {
    st.pendingZoomWindow = false;
    ApplyViewportZoomToWorldRect(static_cast<double>(st.pendingZoomMnX), static_cast<double>(st.pendingZoomMxX),
                                 static_cast<double>(st.pendingZoomMnY), static_cast<double>(st.pendingZoomMxY),
                                 &st.viewportPanX, &st.viewportPanY, &st.viewportZoom, fbW, fbH, viewportAspect);
    if (panX)
      *panX = st.viewportPanX;
    if (panY)
      *panY = st.viewportPanY;
    if (zoom)
      *zoom = st.viewportZoom;
    BumpCadGpuCache(st);
    log.push_back("Zoom window applied.");
  }
}

void BeginSelectionBoxCorner(AppCommandState& st, float wx, float wy, float anchorScreenX,
                             float anchorScreenY) {
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  st.selBoxAnchorX = wx;
  st.selBoxAnchorY = wy;
  st.selBoxAnchorScreenX = anchorScreenX;
  st.selBoxAnchorScreenY = anchorScreenY;
  st.selBoxWaitingSecond = true;
}

void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Move;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    log.push_back("MOVE — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "MOVE — click two corners to window-select objects, then base point and destination. ESC cancels.");
}

void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Copy;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    log.push_back("COPY — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "COPY — click two corners to window-select objects, then base point and destination. ESC cancels.");
}

void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Rotate;
  st.rotatePhase = AppCommandState::RotatePhase::PickSelection;
  st.rotateBaseX = st.rotateBaseY = 0.f;
  st.rotateCopyMode = false;
  st.pendingSurveyDupIsRotate = false;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.rotatePhase = AppCommandState::RotatePhase::NeedBase;
    log.push_back("ROTATE — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "ROTATE — window-select (two clicks), base point, then ° clockwise from north / DMS, or R reference. ESC.");
}

void StartScaleCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Scale;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = false;
  st.scaleRefDist = 1.f;
  st.scalePhase = AppCommandState::ScalePhase::FactorPick;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    log.push_back("SCALE — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "SCALE — window-select (two clicks), base point, then scale: second point or factor (>0), or R for "
        "two-point reference length then new length (type or two picks). ESC cancels.");
}

void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active == AppCommandState::Kind::None)
    return;
  ClearPendingOneShotObjectSnap(st);
  const AppCommandState::Kind prev = st.active;
  if (st.active == AppCommandState::Kind::Line)
    log.push_back("LINE canceled.");
  else if (st.active == AppCommandState::Kind::Circle)
    log.push_back("CIRCLE canceled.");
  else if (st.active == AppCommandState::Kind::Polyline)
    log.push_back("POLYLINE canceled.");
  else if (st.active == AppCommandState::Kind::Arc)
    log.push_back("ARC canceled.");
  else if (st.active == AppCommandState::Kind::Ellipse)
    log.push_back("ELLIPSE canceled.");
  else if (st.active == AppCommandState::Kind::Text)
    log.push_back("TEXT canceled.");
  else if (st.active == AppCommandState::Kind::Mtext)
    log.push_back("MTEXT canceled.");
  else if (st.active == AppCommandState::Kind::DimAligned)
    log.push_back("DIMALIGNED canceled.");
  else if (st.active == AppCommandState::Kind::DimLinear)
    log.push_back("DIMLINEAR canceled.");
  else if (st.active == AppCommandState::Kind::DimAngular)
    log.push_back("DIMANGULAR canceled.");
  else if (st.active == AppCommandState::Kind::Move)
    log.push_back("MOVE canceled.");
  else if (st.active == AppCommandState::Kind::Copy)
    log.push_back("COPY canceled.");
  else if (st.active == AppCommandState::Kind::Rotate)
    log.push_back("ROTATE canceled.");
  else if (st.active == AppCommandState::Kind::Scale)
    log.push_back("SCALE canceled.");
  else if (st.active == AppCommandState::Kind::Delete)
    log.push_back("DELETE canceled.");
  else if (st.active == AppCommandState::Kind::Join)
    log.push_back("JOIN canceled.");
  else if (st.active == AppCommandState::Kind::Trim)
    log.push_back("TRIM canceled.");
  else if (st.active == AppCommandState::Kind::Offset)
    log.push_back("OFFSET canceled.");
  else if (st.active == AppCommandState::Kind::IdPoint)
    log.push_back("ID canceled.");
  else if (st.active == AppCommandState::Kind::SurveyInverse)
    log.push_back("INVERSE canceled.");
  else if (st.active == AppCommandState::Kind::Zoom)
    log.push_back("ZOOM WINDOW canceled.");
  else if (st.active == AppCommandState::Kind::PdfAttach)
    log.push_back("PDFATTACH canceled.");
  st.active = AppCommandState::Kind::None;
  if (prev == AppCommandState::Kind::Offset)
    OffsetCmd::ResetOffsetDraft(st);
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  ResetSegmentAngleLock(st);
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  ClearEntityGripInteraction(st);
  ClearDimGripInteraction(st);
  if (prev == AppCommandState::Kind::Zoom)
    ClearPendingViewportZoom(st);
}

void ApplyCopySurveyDuplicateModalResult(AppCommandState& st, bool applySurveyDup, std::vector<std::string>& log) {
  if (!st.copySurveyDupModalOpen)
    return;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  const bool wasRotateDup = st.pendingSurveyDupIsRotate;
  st.pendingSurveyDupIsRotate = false;
  if (applySurveyDup) {
    if (wasRotateDup)
      DuplicateSelectedSurveyPointsRotated(st, st.pendingRotateCopyBx, st.pendingRotateCopyBy, st.pendingRotateCopyRad,
                                           st.copySurveyDuplicatePolicy, log);
    else
      DuplicateSelectedSurveyPointsTranslated(st, st.pendingCopyDx, st.pendingCopyDy, st.copySurveyDuplicatePolicy,
                                              log);
  } else if (wasRotateDup)
    log.push_back("ROTATE COPY survey — skipped (CAD copy kept).");
  else
    log.push_back("COPY survey — skipped (CAD copy kept).");
}

static void FinishEllipseFromRatio(AppCommandState& st, float ratio, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using EP = AppCommandState::EllipsePhase;
  if (st.active != K::Ellipse || st.ellPhase != EP::WaitRatio)
    return;
  if (ratio <= 1e-8f || ratio > 1.f + 1e-4f) {
    log.push_back("ELLIPSE — ratio must be in (0, 1].");
    return;
  }
  const float vx0 = st.ellMajEx - st.ellCx;
  const float vy0 = st.ellMajEy - st.ellCy;
  const float ma = std::hypot(vx0, vy0);
  if (ma < 1e-8f) {
    log.push_back("ELLIPSE — major axis too short.");
    return;
  }
  CadEllipse ell{};
  ell.cx = st.ellCx;
  ell.cy = st.ellCy;
  ell.majVx = vx0;
  ell.majVy = vy0;
  ell.ratio = ratio;
  st.userEllipses.push_back(ell);
  st.userEllAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = K::None;
  ResetEllipseDraft(st);
  log.push_back("ELLIPSE complete.");
}

static void CommitPolylineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  const size_t nvert = st.polylineDraftVerts.size() / 3;
  if (closed) {
    if (nvert < 3) {
      log.push_back("POLYLINE CLOSE — need at least three vertices.");
      return;
    }
  } else if (nvert < 2) {
    log.push_back("POLYLINE — need at least two vertices (use END to finish open).");
    return;
  }
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  const int baseVert = st.userPolylineOffsets.back();
  st.userPolylineVerts.insert(st.userPolylineVerts.end(), st.polylineDraftVerts.begin(), st.polylineDraftVerts.end());
  st.userPolylineOffsets.push_back(baseVert + static_cast<int>(nvert));
  st.userPolylineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
  st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetPolylineDraft(st);
  log.push_back(closed ? "POLYLINE closed." : "POLYLINE complete.");
}

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Line)
    return false;

  if (st.linePhase == AppCommandState::LinePhase::NeedFirstPoint) {
    st.anchorX = x;
    st.anchorY = y;
    st.linePhase = AppCommandState::LinePhase::NeedNextPoint;
    log.push_back(
        "First point set — next: click, X,Y, @dx,dy; A / AP (two picks + Enter / +90); distance along bearing.");
    return true;
  }

  st.userLinesFlat.push_back(st.anchorX);
  st.userLinesFlat.push_back(st.anchorY);
  st.userLinesFlat.push_back(0.f);
  st.userLinesFlat.push_back(x);
  st.userLinesFlat.push_back(y);
  st.userLinesFlat.push_back(0.f);
  st.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);

  st.anchorX = x;
  st.anchorY = y;
  log.push_back("Segment added — next point or ESC to finish.");
  return true;
}

bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Polyline)
    return false;

  using PP = AppCommandState::PolylinePhase;
  if (st.polylinePhase == PP::NeedFirstPoint) {
    st.polylineDraftVerts.clear();
    st.polylineDraftVerts.push_back(x);
    st.polylineDraftVerts.push_back(y);
    st.polylineDraftVerts.push_back(0.f);
    st.anchorX = x;
    st.anchorY = y;
    st.polyFirstX = x;
    st.polyFirstY = y;
    st.polyDraftSegments = 0;
    st.polylinePhase = PP::NeedNextPoint;
    log.push_back("POLYLINE — next vertex (A + bearing then distance like LINE), CLOSE / END, or ESC.");
    return true;
  }

  st.polylineDraftVerts.push_back(x);
  st.polylineDraftVerts.push_back(y);
  st.polylineDraftVerts.push_back(0.f);
  ++st.polyDraftSegments;
  st.anchorX = x;
  st.anchorY = y;
  log.push_back("POLYLINE vertex added.");
  return true;
}

void SubmitViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log,
                         bool windowSelectionSubtract, bool fenceLeftToRightWindowMode) {
  ClearPendingOneShotObjectSnap(st);
  SubmitViewportPickImpl(st, wx, wy, log, windowSelectionSubtract, fenceLeftToRightWindowMode);
}

void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log) {
  (void)cmdBufSize;
  std::string line = StringUtil::trimCopy(std::string(cmdBuf));
  using K = AppCommandState::Kind;
  using LP = AppCommandState::LinePhase;
  using PP = AppCommandState::PolylinePhase;
  using SAP = AppCommandState::SegmentAnglePickPhase;

  const bool segPickNeedAdjust =
      ((st.active == K::Line && st.linePhase == LP::NeedNextPoint) ||
       (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint)) &&
      st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit;

  if (segPickNeedAdjust) {
    if (line.empty()) {
      CommitSegmentAnglePickLock(st, log);
      cmdBuf[0] = '\0';
      return;
    }
    const std::string t = StringUtil::trimCopy(line);
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
      float dlt = 0.f;
      if (!ParseAngleDegreesInternal(t, &dlt)) {
        log.push_back("Bearing pick — invalid adjustment (decimal/DMS). Blank Enter locks; +/- adds turn.");
        cmdBuf[0] = '\0';
        return;
      }
      st.segmentPickDraftBearingDeg = NormalizeBearingDegreesCwNorth(st.segmentPickDraftBearingDeg + dlt);
      CommitSegmentAnglePickLock(st, log);
      cmdBuf[0] = '\0';
      return;
    }
    log.push_back("Bearing pick — blank Enter to lock, or +90 / -45 first (° clockwise from north).");
    cmdBuf[0] = '\0';
    return;
  }

  const bool linePolyNextNeedPoint =
      (st.active == K::Line && st.linePhase == LP::NeedNextPoint) ||
      (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint);

  if (linePolyNextNeedPoint && st.segmentAngleKeyboardAwaitBearing) {
    if (line.empty()) {
      st.segmentAngleKeyboardAwaitBearing = false;
      log.push_back("Bearing entry canceled.");
      cmdBuf[0] = '\0';
      return;
    }
    float combined = 0.f;
    if (!ParseBearingCwNorthStringWithOptionalDelta(line, &combined, log)) {
      cmdBuf[0] = '\0';
      return;
    }
    const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
    st.segmentLockUx = std::cos(theta);
    st.segmentLockUy = std::sin(theta);
    st.segmentAngleLockActive = true;
    st.segmentAnglePickPhase = SAP::Idle;
    st.segmentAngleKeyboardAwaitBearing = false;
    char bufKb[144];
    std::snprintf(bufKb, sizeof(bufKb),
                  "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                  static_cast<double>(combined));
    log.push_back(bufKb);
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::Mtext && st.mtextPhase == AppCommandState::MtextPhase::WaitString && !line.empty()) {
    log.push_back("MTEXT — type in the on-screen editor over the box (Ctrl+Enter reformats; Save to place).");
    cmdBuf[0] = '\0';
    return;
  }

  if (line.empty()) {
    if (st.active == K::Trim) {
      using TP = AppCommandState::TrimPhase;
      if (st.trimPhase == TP::SelectCuttingEdges) {
        if (st.trimCutters.empty())
          log.push_back("TRIM — pick at least one cutting edge before pressing Enter.");
        else {
          st.trimPhase = TP::SelectTrimTargets;
          log.push_back("TRIM — click segments to trim (near the piece to remove). Enter when done.");
        }
      } else if (st.trimPhase == TP::CuttingLine_WaitP1 || st.trimPhase == TP::CuttingLine_WaitP2) {
        log.push_back("TRIM — specify cutting-line points in the viewport.");
      } else {
        st.active = K::None;
        st.trimPhase = TP::SelectCuttingEdges;
        st.trimCutters.clear();
        log.push_back("TRIM — finished.");
      }
    } else if (st.active == K::Offset) {
      using OP = AppCommandState::OffsetPhase;
      if (st.offsetPhase == OP::WaitDistanceOrThrough)
        log.push_back("OFFSET — type a positive distance, or pick a through point (line / circle / arc).");
      else if (st.offsetPhase == OP::WaitSidePick)
        log.push_back("OFFSET — pick which side to offset.");
      else
        log.push_back("OFFSET — select an entity in the viewport.");
    }
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::None) {
    std::istringstream issIdle(StringUtil::trimCopy(std::string(cmdBuf)));
    std::string plotTok;
    issIdle >> plotTok;
    plotTok = StringUtil::toLowerAsciiCopy(plotTok);
    if (plotTok == "plotscale" || plotTok == "pscale") {
      float pv = 0.f;
      if (!(issIdle >> pv) || pv <= 0.f)
        log.push_back("PLOTSCALE — usage: PLOTSCALE <model_units_per_plotted_inch> (example: 50 for 1\"=50').");
      else {
        st.modelUnitsPerPlottedInch = pv;
        RepositionAllSurveyPointLabels(st);
        st.surveyLabelLayoutCacheHalfH = st.viewportLastSurveyLayoutOrthoHalfH;
        st.surveyLabelLayoutCacheVpHeightPx = st.viewportLastSurveyLayoutHeightPx;
        st.surveyLabelLayoutCacheMup = st.modelUnitsPerPlottedInch;
        BumpCadGpuCache(st);
        log.push_back("Plot scale: 1 plotted inch = " + std::to_string(pv) + " model units.");
      }
      cmdBuf[0] = '\0';
      return;
    }
  }

  if (st.active == AppCommandState::Kind::Delete) {
    log.push_back("DELETE — finish window-select in the viewport (two clicks), or ESC to cancel.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Join) {
    log.push_back("JOIN — finish window-select in the viewport (two clicks), or ESC to cancel.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Trim) {
    using TP = AppCommandState::TrimPhase;
    const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));
    if (low == "l" || low == "line") {
      if (st.trimPhase != TP::SelectCuttingEdges)
        log.push_back("TRIM — L only while picking cutting edges (ESC and restart TRIM if stuck).");
      else {
        st.trimCutters.clear();
        st.trimPhase = TP::CuttingLine_WaitP1;
        log.push_back("TRIM — draw along the segment to trim: first point (rubber band shows dashed preview).");
      }
      cmdBuf[0] = '\0';
      return;
    }
    log.push_back("TRIM — viewport picks, or type L to trim by drawing on the segment (two clicks); ESC cancels.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Zoom) {
    log.push_back("ZOOM WINDOW — finish two clicks in the viewport, or ESC to cancel.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::Offset) {
    using OP = AppCommandState::OffsetPhase;
    if (st.offsetPhase != OP::WaitDistanceOrThrough) {
      log.push_back("OFFSET — use viewport picks; type a distance only after selecting the object.");
      cmdBuf[0] = '\0';
      return;
    }
    float d = 0.f;
    if (!ParseOneFloat(StringUtil::trimCopy(line), &d)) {
      log.push_back("OFFSET — type a positive offset distance (model units), then pick a side.");
      cmdBuf[0] = '\0';
      return;
    }
    if (d <= 0.f) {
      log.push_back("OFFSET — distance must be positive.");
      cmdBuf[0] = '\0';
      return;
    }
    st.offsetTypedDistance = d;
    st.offsetPhase = OP::WaitSidePick;
    log.push_back("OFFSET — pick which side of the object to offset.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::IdPoint) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("ID — pick in viewport or type X,Y (model units, UCS World).");
      cmdBuf[0] = '\0';
      return;
    }
    CommitIdPointAt(st, px, py, log);
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    float px = 0.f;
    float py = 0.f;
    if (st.surveyInversePhase == SIP::WaitFrom) {
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("INVERSE — type X,Y (World) or pick first point in Drawing1.");
        cmdBuf[0] = '\0';
        return;
      }
      st.surveyInverseFromX = px;
      st.surveyInverseFromY = py;
      st.surveyInversePhase = SIP::WaitTo;
      log.push_back("INVERSE — second point (X,Y or @dx,dy from first).");
      cmdBuf[0] = '\0';
      return;
    }
    if (!ParseStoragePoint(st, line, &px, &py, true, st.surveyInverseFromX, st.surveyInverseFromY)) {
      log.push_back("INVERSE — type X,Y or @dx,dy from first point.");
      cmdBuf[0] = '\0';
      return;
    }
    CommitSurveyInverseSecondPoint(st, px, py, log);
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Move || st.active == AppCommandState::Kind::Copy) {
    if (HandleModifyText(st, st.active == AppCommandState::Kind::Copy, line, log)) {
      cmdBuf[0] = '\0';
      return;
    }
    log.push_back("Could not parse MOVE/COPY input — use X,Y or @dx,dy from base.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Scale) {
    if (HandleScaleText(st, line, log)) {
      cmdBuf[0] = '\0';
      return;
    }
    log.push_back("Could not parse SCALE input — see command hints (base X,Y; factor; R + reference/new length).");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Rotate) {
    if (HandleRotateText(st, line, log)) {
      cmdBuf[0] = '\0';
      return;
    }
    log.push_back("Could not parse ROTATE input — see command hints.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::Polyline) {
    using PP = AppCommandState::PolylinePhase;
    const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));
    if (low == "close" || low == "cl") {
      CancelSegmentAnglePick(st, nullptr);
      if (st.polylinePhase != PP::NeedNextPoint || st.polyDraftSegments == 0)
        log.push_back("POLYLINE CLOSE — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, true, log);
      cmdBuf[0] = '\0';
      return;
    }
    if (low == "end") {
      CancelSegmentAnglePick(st, nullptr);
      if (st.polylinePhase != PP::NeedNextPoint || st.polyDraftSegments == 0)
        log.push_back("POLYLINE END — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, false, log);
      cmdBuf[0] = '\0';
      return;
    }

    float px = 0.f;
    float py = 0.f;
    const bool allowRel = st.polylinePhase == PP::NeedNextPoint;

    if (allowRel && TryParseSegmentAngleLockCommand(st, line, log)) {
      cmdBuf[0] = '\0';
      return;
    }

    if (ParseStoragePoint(st, line, &px, &py, allowRel, st.anchorX, st.anchorY)) {
      SubmitPolylineVertex(st, px, py, log);
      cmdBuf[0] = '\0';
      return;
    }

    if (allowRel && st.segmentAngleLockActive) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        if (std::fabs(dist) < 1e-20f)
          log.push_back("POLYLINE — distance must be non-zero.");
        else
          SubmitPolylineVertex(st, st.anchorX + st.segmentLockUx * dist, st.anchorY + st.segmentLockUy * dist, log);
        cmdBuf[0] = '\0';
        return;
      }
    }

    if (allowRel && st.orthoMode) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        float ux = 0.f;
        float uy = 0.f;
        if (!OrthoUnitTowardPoint(st.anchorX, st.anchorY, st.uiCursorWorldX, st.uiCursorWorldY, &ux, &uy))
          log.push_back(
              "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else
          SubmitPolylineVertex(st, st.anchorX + ux * dist, st.anchorY + uy * dist, log);
        cmdBuf[0] = '\0';
        return;
      }
    }

    log.push_back("POLYLINE — X,Y / @dx,dy / A or AP bearing / CLOSE / END / ortho distance.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::Ellipse && st.ellPhase == AppCommandState::EllipsePhase::WaitRatio) {
    const std::string tr = StringUtil::trimCopy(line);
    float ratio = 0.5f;
    if (!tr.empty() && !ParseSingleFloatToken(tr, &ratio)) {
      log.push_back("ELLIPSE — enter one number for minor/major ratio (0-1], or blank for 0.5.");
      cmdBuf[0] = '\0';
      return;
    }
    FinishEllipseFromRatio(st, ratio, log);
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::Text) {
    using TP = AppCommandState::TextCmdPhase;
    if (st.textPhase == TP::WaitInsertion) {
      float px = 0.f;
      float py = 0.f;
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("TEXT — type insertion X,Y or click in viewport.");
        cmdBuf[0] = '\0';
        return;
      }
      st.textInsX = px;
      st.textInsY = py;
      st.textPhase = TP::WaitHeight;
      log.push_back("TEXT height — Enter for plot-scale default:");
      cmdBuf[0] = '\0';
      return;
    }
    if (st.textPhase == TP::WaitHeight) {
      const std::string tr = StringUtil::trimCopy(line);
      if (tr.empty())
        st.textHeightDraft = DefaultAnnotationTextHeightWorld(st);
      else if (!ParseSingleFloatToken(tr, &st.textHeightDraft) || st.textHeightDraft <= 0.f) {
        log.push_back("TEXT — invalid height.");
        cmdBuf[0] = '\0';
        return;
      }
      st.textPhase = TP::WaitRotation;
      log.push_back("TEXT rotation ° clockwise from north (decimal/DMS) — Enter for 0:");
      cmdBuf[0] = '\0';
      return;
    }
    if (st.textPhase == TP::WaitRotation) {
      const std::string tr = StringUtil::trimCopy(line);
      if (tr.empty())
        st.textRotDraft = 0.f;
      else {
        float deg = 0.f;
        if (!ParseAngleDegrees(tr, &deg)) {
          log.push_back("TEXT — could not parse angle.");
          cmdBuf[0] = '\0';
          return;
        }
        st.textRotDraft = MathAngleRadFromBearingCwNorthDeg(deg);
      }
      st.textPhase = TP::WaitString;
      log.push_back("TEXT — enter content:");
      cmdBuf[0] = '\0';
      return;
    }
    if (st.textPhase == TP::WaitString) {
      CadAnnotation ann;
      ann.kind = CadAnnotation::Kind::Text;
      ann.insX = st.textInsX;
      ann.insY = st.textInsY;
      ann.plottedHeightInches =
          st.textHeightDraft / std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
      ann.rotationRad = st.textRotDraft;
      ann.text = line;
      if (!ann.text.empty()) {
        st.cadAnnotations.push_back(std::move(ann));
        st.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
        log.push_back("TEXT placed.");
      } else
        log.push_back("TEXT — empty; canceled.");
      st.active = K::None;
      ResetTextCmdDraft(st);
      cmdBuf[0] = '\0';
      return;
    }
  }

  if (st.active == AppCommandState::Kind::Line) {
    float px = 0.f;
    float py = 0.f;
    const bool allowRel = st.linePhase == AppCommandState::LinePhase::NeedNextPoint;

    if (allowRel && TryParseSegmentAngleLockCommand(st, line, log)) {
      cmdBuf[0] = '\0';
      return;
    }

    if (ParseStoragePoint(st, line, &px, &py, allowRel, st.anchorX, st.anchorY)) {
      SubmitLineVertex(st, px, py, log);
      cmdBuf[0] = '\0';
      return;
    }

    if (allowRel && st.segmentAngleLockActive) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        if (std::fabs(dist) < 1e-20f)
          log.push_back("LINE — distance must be non-zero.");
        else {
          px = st.anchorX + st.segmentLockUx * dist;
          py = st.anchorY + st.segmentLockUy * dist;
          SubmitLineVertex(st, px, py, log);
        }
        cmdBuf[0] = '\0';
        return;
      }
    }

    if (allowRel && st.orthoMode) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        float ux = 0.f;
        float uy = 0.f;
        if (!OrthoUnitTowardPoint(st.anchorX, st.anchorY, st.uiCursorWorldX, st.uiCursorWorldY, &ux, &uy))
          log.push_back(
              "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else {
          px = st.anchorX + ux * dist;
          py = st.anchorY + uy * dist;
          SubmitLineVertex(st, px, py, log);
        }
        cmdBuf[0] = '\0';
        return;
      }
    }

    log.push_back(
        std::string("Could not parse point. Use X,Y or X Y") +
        (allowRel ? "; @dx,dy; A / AP (two picks); A 45 +90; ortho distance toward cursor." : "."));
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == AppCommandState::Kind::Circle) {
    if (HandleCircleTextInput(line, st, log)) {
      cmdBuf[0] = '\0';
      return;
    }
    log.push_back("Could not parse input for current CIRCLE step — see hint below.");
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    using DP = AppCommandState::DimPhase;
    const bool linear = st.active == K::DimLinear;
    const std::string dimTrim = StringUtil::trimCopy(line);
    const std::string dimLow = StringUtil::toLowerAsciiCopy(dimTrim);
    if (linear && st.dimPhase == DP::WaitDimLinePt && (dimLow == "h" || dimLow == "v")) {
      CadDimLinearApplyHVHotkey(st, dimLow == "v", log);
      cmdBuf[0] = '\0';
      return;
    }
    float px = 0.f;
    float py = 0.f;
    bool allowRel = false;
    float ax = 0.f;
    float ay = 0.f;
    if (st.dimPhase == DP::WaitExt2) {
      allowRel = true;
      ax = st.dimE1x;
      ay = st.dimE1y;
    } else if (linear && st.dimPhase == DP::WaitDimLinePt) {
      allowRel = true;
      ax = 0.5f * (st.dimE1x + st.dimE2x);
      ay = 0.5f * (st.dimE1y + st.dimE2y);
    }
    if (!ParseStoragePoint(st, dimTrim, &px, &py, allowRel, ax, ay)) {
      log.push_back(linear ? "DIMLINEAR — X,Y or @dx,dy; at line step H / V locks orientation; move cursor to unlock."
                           : "DIMALIGNED — X,Y or @dx,dy from first extension.");
      cmdBuf[0] = '\0';
      return;
    }
    SubmitViewportPick(st, px, py, log, false, false);
    cmdBuf[0] = '\0';
    return;
  }

  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    const std::string dimTrim = StringUtil::trimCopy(line);
    float px = 0.f;
    float py = 0.f;
    bool allowRel = false;
    float ax = 0.f;
    float ay = 0.f;
    if (st.dimAngularPhase != DAP::WaitVertex) {
      allowRel = true;
      ax = st.dimAngVx;
      ay = st.dimAngVy;
    }
    if (!ParseStoragePoint(st, dimTrim, &px, &py, allowRel, ax, ay)) {
      log.push_back("DIMANGULAR — X,Y or @dx,dy from vertex.");
      cmdBuf[0] = '\0';
      return;
    }
    SubmitViewportPick(st, px, py, log, false, false);
    cmdBuf[0] = '\0';
    return;
  }

  std::string low = StringUtil::toLowerAsciiCopy(line);
  for (const CmdEntry& e : kRegistry) {
    if (low == StringUtil::toLowerAsciiCopy(e.primary)) {
      DispatchByPrimary(StringUtil::toLowerAsciiCopy(e.primary), st, log);
      cmdBuf[0] = '\0';
      return;
    }
    if (e.aliases[0] == '\0')
      continue;
    std::istringstream als(std::string(e.aliases));
    std::string a;
    while (std::getline(als, a, ',')) {
      a = StringUtil::trimCopy(a);
      if (a.empty())
        continue;
      if (low == StringUtil::toLowerAsciiCopy(a)) {
        DispatchByPrimary(StringUtil::toLowerAsciiCopy(e.primary), st, log);
        cmdBuf[0] = '\0';
        return;
      }
    }
  }

  if (TryStrongFuzzyDispatch(line, st, log)) {
    cmdBuf[0] = '\0';
    return;
  }

  auto fuzzy = FuzzyCommandMatches(line, 6);
  if (!fuzzy.empty()) {
    std::string hint = "Unknown command. Did you mean:";
    for (const auto& w : fuzzy)
      hint += " " + w + ",";
    if (!hint.empty() && hint.back() == ',')
      hint.pop_back();
    hint += "?";
    log.push_back(hint);
  } else
    log.push_back("Unknown command. Type HELP.");

  cmdBuf[0] = '\0';
}

std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults) {
  std::vector<std::string> names;
  for (const CmdEntry& e : kRegistry)
    names.push_back(e.primary);

  struct Scored {
    std::string name;
    int score;
  };
  std::vector<Scored> ranked;
  std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};

  for (const auto& n : names) {
    int sc = FuzzySubsequenceScore(qlow, n);
    if (sc >= 0)
      ranked.push_back({n, sc});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.name < b.name;
  });

  std::vector<std::string> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i)
    out.push_back(ranked[i].name);
  return out;
}

const char* CircleCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Circle)
    return "";
  switch (st.circlePhase) {
  case AppCommandState::CirclePhase::WaitCenterOrMode:
    return "CIRCLE: Click or type center | Type 3P for three-point circle | ESC cancel";
  case AppCommandState::CirclePhase::WaitRadius:
    return "CIRCLE: Click edge for radius | Type radius | D <value> or D<value> for diameter | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP1:
    return "CIRCLE (3P): Point 1 of 3 — click or X,Y | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP2:
    return "CIRCLE (3P): Point 2 of 3 — click or X,Y | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP3:
    return "CIRCLE (3P): Point 3 of 3 — click or X,Y | ESC cancel";
  }
  return "";
}

const char* ModifyCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  if (st.active != K::Move && st.active != K::Copy)
    return "";
  if (st.modifyPhase == MP::PickSelection)
    return "MOVE/COPY: Click opposite corners of selection window | ESC cancel";
  if (st.modifyPhase == MP::NeedBase)
    return "MOVE/COPY: Base point — click or X,Y | ESC cancel";
  if (st.modifyPhase == MP::NeedDestination)
    return "MOVE/COPY: Second point — click or X,Y or @dx,dy from base | ESC cancel";
  return "";
}

const char* RotateCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using RP = AppCommandState::RotatePhase;
  if (st.active != K::Rotate)
    return "";
  switch (st.rotatePhase) {
  case RP::PickSelection:
    return "ROTATE: Window-select — click two corners | ESC cancel";
  case RP::NeedBase:
    return "ROTATE: Base point — click or X,Y | ESC cancel";
  case RP::NeedAngleOrReference:
    return "ROTATE: ° clockwise / DMS | R ref | C copy | ESC (north=0° CW)";
  case RP::Ref_WaitP1:
    return "ROTATE ref: First point | C toggles copy | ESC cancel";
  case RP::Ref_WaitP2:
    return "ROTATE ref: Second point | C toggles copy | ESC cancel";
  case RP::AfterReference_WaitAngleOrP:
    return "ROTATE ref: New bearing ° from north (like props) | P two pts | C copy | ESC";
  case RP::AnglePoints_WaitP1:
    return "ROTATE angle pts: First point | C copy | ESC cancel";
  case RP::AnglePoints_WaitP2:
    return "ROTATE angle pts: Second point | C copy | ESC cancel";
  }
  return "";
}

const char* ScaleCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (st.active != K::Scale)
    return "";
  if (st.modifyPhase == MP::PickSelection)
    return "SCALE: Window-select — click two corners | ESC cancel";
  if (st.modifyPhase == MP::NeedBase)
    return "SCALE: Base point — click or X,Y | ESC cancel";
  if (st.modifyPhase == MP::NeedDestination) {
    switch (st.scalePhase) {
    case SP::FactorPick:
      return "SCALE: Second point or type factor (>0) — dist/base-ref | R = two-point ref length | ESC";
    case SP::Ref_WaitP1:
      return "SCALE ref: First point of reference length | ESC cancel";
    case SP::Ref_WaitP2:
      return "SCALE ref: Second point (reference length) | ESC cancel";
    case SP::NewLength_WaitTypedOrP1:
      return "SCALE ref: Type new length (model units) or pick first point of new length | ESC";
    case SP::NewLength_WaitP2:
      return "SCALE ref: Second point of new length (preview) | ESC cancel";
    default:
      return "";
    }
  }
  return "";
}

const char* DeleteCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Delete)
    return "";
  return "DELETE: Window-select — click two corners | ESC cancel";
}

const char* JoinCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Join)
    return "";
  return "JOIN: Window-select — click two corners | ESC cancel";
}

const char* TrimCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using TP = AppCommandState::TrimPhase;
  if (st.active != K::Trim)
    return "";
  switch (st.trimPhase) {
  case TP::SelectCuttingEdges:
    return "TRIM: Cutting edges | Enter | type L — draw on segment to trim (2 clicks, done) | ESC cancel";
  case TP::CuttingLine_WaitP1:
    return "TRIM line-trim: First point on/near edge | ESC cancel";
  case TP::CuttingLine_WaitP2:
    return "TRIM line-trim: Second point — dashed = removed part (midpoint picks side) | Ortho | ESC";
  case TP::SelectTrimTargets:
    return "TRIM: Click segment near end to remove | Enter done | ESC cancel";
  }
  return "";
}

const char* OffsetCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using OP = AppCommandState::OffsetPhase;
  if (st.active != K::Offset)
    return "";
  switch (st.offsetPhase) {
  case OP::WaitSelectEntity:
    return "OFFSET: Pick line, circle, arc, ellipse, or polyline | ESC cancel";
  case OP::WaitDistanceOrThrough:
    return "OFFSET: Type distance then pick side — or through-click (line / circle / arc) | ESC cancel";
  case OP::WaitSidePick:
    return "OFFSET: Pick side of object (polyline/ellipse use closest edge) | ESC cancel";
  }
  return "";
}

const char* ZoomCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Zoom)
    return "";
  return "ZOOM WINDOW: Two corners (unsnapped) — rubber previews fit area | ESC cancel";
}

const char* LineCommandFooterHint(const AppCommandState& st) {
  using LP = AppCommandState::LinePhase;
  using SAP = AppCommandState::SegmentAnglePickPhase;
  if (st.active != AppCommandState::Kind::Line)
    return "";
  if (st.linePhase == LP::NeedFirstPoint)
    return "LINE: First point — click or X,Y | ESC ends command";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing)
    return "LINE: Type bearing ° CW from N (decimal/DMS) | blank Enter cancels | ESC ends command";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1)
    return "LINE bearing pick: First direction point — click | AP started | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2)
    return "LINE bearing pick: Second point — direction | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
    return "LINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAngleLockActive)
    return "LINE (bearing lock): distance ± along ray | click on line | X,Y | @dx,dy | A clears";
  if (st.orthoMode)
    return "LINE (Ortho): Next — click locks H/V | X,Y | @dx,dy | distance toward cursor | A/AP bearing";
  return "LINE: Next — click | X,Y | @dx,dy | A / AP / A 45 +90 | Ortho panel";
}

const char* DrawingExtrasFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using PP = AppCommandState::PolylinePhase;
  using AP = AppCommandState::ArcPhase;
  using EP = AppCommandState::EllipsePhase;
  using TP = AppCommandState::TextCmdPhase;
  using MP = AppCommandState::MtextPhase;
  using DP = AppCommandState::DimPhase;

  if (st.active == K::IdPoint)
    return "ID: Pick point (OSNAP when enabled) or type X,Y — logs UCS World | ESC cancel";

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (st.surveyInversePhase == SIP::WaitFrom)
      return "INVERSE: First point — pick or X,Y (Easting, Northing) | ESC cancel";
    return "INVERSE: Second point — pick or X,Y / @ from first | ESC cancel";
  }

  if (st.active == K::Polyline) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (st.polylinePhase == PP::NeedFirstPoint)
      return "POLYLINE: First point — click or X,Y | CLOSE closes | ESC cancel";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing)
      return "POLYLINE: Type bearing ° CW from N | blank Enter cancels | ESC cancel";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1)
      return "POLYLINE bearing pick: First direction click | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2)
      return "POLYLINE bearing pick: Second point | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "POLYLINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleLockActive)
      return "POLYLINE (bearing lock): distance ± | click on ray | X,Y | A clears | CLOSE / END";
    return "POLYLINE: Next — click | X,Y | @dx,dy | A/AP | CLOSE / END | ESC";
  }
  if (st.active == K::Arc) {
    switch (st.arcPhase) {
    case AP::WaitStart:
      return "ARC: Start point | ESC cancel";
    case AP::WaitMid:
      return "ARC: Point on arc | ESC cancel";
    case AP::WaitEnd:
      return "ARC: End point | ESC cancel";
    }
  }
  if (st.active == K::Ellipse) {
    switch (st.ellPhase) {
    case EP::WaitCenter:
      return "ELLIPSE: Center | ESC cancel";
    case EP::WaitMajorEnd:
      return "ELLIPSE: Major axis end | ESC cancel";
    case EP::WaitRatio:
      return "ELLIPSE: Ratio (0-1] on command line | Enter = 0.5 | ESC cancel";
    }
  }
  if (st.active == K::Text) {
    switch (st.textPhase) {
    case TP::WaitInsertion:
      return "TEXT: Insertion — click or X,Y | ESC cancel";
    case TP::WaitHeight:
      return "TEXT: Height — Enter for plot-scale default | ESC cancel";
    case TP::WaitRotation:
      return "TEXT: Rotation ° CW from north — decimal/DMS or Enter=0 | ESC cancel";
    case TP::WaitString:
      return "TEXT: Enter content | ESC cancel";
    }
  }
  if (st.active == K::Mtext) {
    switch (st.mtextPhase) {
    case MP::WaitCorner1:
      return "MTEXT: First corner | ESC cancel";
    case MP::WaitCorner2:
      return "MTEXT: Opposite corner | ESC cancel";
    case MP::WaitString:
      return "MTEXT: Edit in drawing box — Ctrl+Enter reformats | Save to place | Esc cancel";
    }
  }
  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    switch (st.dimPhase) {
    case DP::WaitExt1:
      return st.active == K::DimLinear ? "DIMLINEAR: Extension 1 | ESC cancel" : "DIMALIGNED: Extension 1 | ESC cancel";
    case DP::WaitExt2:
      return st.active == K::DimLinear ? "DIMLINEAR: Extension 2 | ESC cancel" : "DIMALIGNED: Extension 2 | ESC cancel";
    case DP::WaitDimLinePt:
      return st.active == K::DimLinear
                 ? "DIMLINEAR: Line — dominant X vs Y from chord mid; H / V keys; X,Y | @ from chord mid | ESC"
                 : "DIMALIGNED: Offset — click | X,Y | @ from chord mid | ESC";
    }
  }
  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    switch (st.dimAngularPhase) {
    case DAP::WaitVertex:
      return "DIMANGULAR: Vertex | X,Y | ESC cancel";
    case DAP::WaitRay1:
      return "DIMANGULAR: First ray point | X,Y | @ from vertex | ESC";
    case DAP::WaitRay2:
      return "DIMANGULAR: Second ray point | X,Y | @ from vertex | ESC";
    case DAP::WaitArc:
      return "DIMANGULAR: Arc radius (bisector) | click | X,Y | @ from vertex | ESC";
    }
  }
  return "";
}

static bool ParseHexColorForViewport(const std::string& s, float* r, float* g, float* b) {
  if (s.size() < 4 || s[0] != '#')
    return false;
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
      return 10 + (c - 'A');
    return -1;
  };
  if (s.size() == 4) {
    const int rh = hexVal(s[1]);
    const int gh = hexVal(s[2]);
    const int bh = hexVal(s[3]);
    if (rh < 0 || gh < 0 || bh < 0)
      return false;
    *r = static_cast<float>(rh | (rh << 4)) / 255.f;
    *g = static_cast<float>(gh | (gh << 4)) / 255.f;
    *b = static_cast<float>(bh | (bh << 4)) / 255.f;
    return true;
  }
  if (s.size() != 7)
    return false;
  int rv = 0;
  int gv = 0;
  int bv = 0;
  for (int i = 0; i < 2; ++i) {
    const int d = hexVal(s[static_cast<size_t>(1 + i)]);
    if (d < 0)
      return false;
    rv = rv * 16 + d;
  }
  for (int i = 0; i < 2; ++i) {
    const int d = hexVal(s[static_cast<size_t>(3 + i)]);
    if (d < 0)
      return false;
    gv = gv * 16 + d;
  }
  for (int i = 0; i < 2; ++i) {
    const int d = hexVal(s[static_cast<size_t>(5 + i)]);
    if (d < 0)
      return false;
    bv = bv * 16 + d;
  }
  *r = static_cast<float>(rv) / 255.f;
  *g = static_cast<float>(gv) / 255.f;
  *b = static_cast<float>(bv) / 255.f;
  return true;
}

struct NamedRgbPreset {
  const char* storage;
  float r;
  float g;
  float b;
};

// Keep storage strings aligned with Properties combo (except ByLayer handled separately).

static const NamedRgbPreset kViewportColorPresets[] = {
    {"Red", 1.f, 0.f, 0.f},       {"Yellow", 1.f, 1.f, 0.f}, {"Green", 0.f, 1.f, 0.f},
    {"Cyan", 0.f, 1.f, 1.f},      {"Blue", 0.f, 0.f, 1.f}, {"Magenta", 1.f, 0.f, 1.f},
    {"White", 1.f, 1.f, 1.f},     {"Gray", 0.5f, 0.5f, 0.5f}, {"Black", 0.f, 0.f, 0.f},
    {"Orange", 1.f, 0.5f, 0.f},
};

static bool LookupNamedRgbPreset(const std::string& c, float* r, float* g, float* b) {
  for (const auto& p : kViewportColorPresets) {
    if (c == p.storage) {
      *r = p.r;
      *g = p.g;
      *b = p.b;
      return true;
    }
  }
  return false;
}

void ResolveStoredColorForViewport(const std::string& colorStorage, float transparency, float defaultR,
                                  float defaultG, float defaultB, float* outRgba) {
  const float tr = transparency < 0.f ? 0.f : std::clamp(transparency, 0.f, 1.f);
  const float alpha = 1.f - tr;
  const std::string& c = colorStorage;

  if (c.empty() || c == "ByLayer") {
    outRgba[0] = defaultR;
    outRgba[1] = defaultG;
    outRgba[2] = defaultB;
    outRgba[3] = alpha;
    return;
  }
  float r = defaultR;
  float g = defaultG;
  float bl = defaultB;
  if (!c.empty() && c[0] == '#') {
    if (ParseHexColorForViewport(c, &r, &g, &bl)) {
      outRgba[0] = r;
      outRgba[1] = g;
      outRgba[2] = bl;
      outRgba[3] = alpha;
      return;
    }
  }
  if (LookupNamedRgbPreset(c, &r, &g, &bl)) {
    outRgba[0] = r;
    outRgba[1] = g;
    outRgba[2] = bl;
    outRgba[3] = alpha;
    return;
  }
  outRgba[0] = defaultR;
  outRgba[1] = defaultG;
  outRgba[2] = defaultB;
  outRgba[3] = alpha;
}

static bool LayerNamesEqCi(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

const CadLayerRow* FindDrawingLayerRowCi(const AppCommandState& st, const std::string& layerName) {
  for (const auto& r : st.drawingLayerTable) {
    if (LayerNamesEqCi(r.name, layerName))
      return &r;
  }
  return nullptr;
}

float EffectiveEntityTransparency01(const EntityAttributes& e, const CadLayerRow* layer) {
  if (e.transparency >= 0.f)
    return std::clamp(e.transparency, 0.f, 1.f);
  if (layer)
    return std::clamp(layer->transparency, 0.f, 1.f);
  return 0.f;
}

float EffectiveEntityLineweightMm(const EntityAttributes& e, const CadLayerRow* layer) {
  if (e.lineweightMm >= 0.f)
    return e.lineweightMm;
  if (layer && layer->lineweightMm >= 0.f)
    return layer->lineweightMm;
  return 0.18f;
}

std::string EffectiveEntityLinetypeNameForViewport(const EntityAttributes& e, const CadLayerRow* layer) {
  const std::string c = CadCanonicalLinetypeNameForDxf(e.linetype);
  if (c.empty() || c == "ByLayer")
    return layer ? layer->linetype : std::string("Continuous");
  return e.linetype;
}

void ResolveEntityRgbaForViewport(const EntityAttributes& attr, const CadLayerRow* layer, float defaultR,
                                  float defaultG, float defaultB, float* outRgba) {
  const float tr = EffectiveEntityTransparency01(attr, layer);
  std::string col = attr.color;
  if (col.empty() || col == "ByLayer") {
    if (layer && !layer->color.empty() && layer->color != "ByLayer")
      col = layer->color;
  }
  ResolveStoredColorForViewport(col, tr, defaultR, defaultG, defaultB, outRgba);
}

struct DxfLwPair {
  int code;
  float mm;
};

static const DxfLwPair kDxfLwTable[] = {
    {0, 0.f},     {5, 0.05f},   {9, 0.09f},   {13, 0.13f},  {15, 0.15f},  {18, 0.18f},  {20, 0.20f},
    {25, 0.25f},  {30, 0.30f},  {35, 0.35f},  {40, 0.40f},  {50, 0.50f},  {53, 0.53f},  {60, 0.60f},
    {70, 0.70f},  {80, 0.80f},  {90, 0.90f},  {100, 1.00f}, {106, 1.06f}, {120, 1.20f}, {140, 1.40f},
    {158, 1.58f}, {200, 2.00f}, {211, 2.11f},
};

int CadDxfLineweightEnum370FromMm(float mm) {
  if (mm < 0.f)
    return -1;
  int best = 18;
  float bestD = 1e9f;
  for (const auto& e : kDxfLwTable) {
    const float d = std::fabs(e.mm - mm);
    if (d < bestD) {
      bestD = d;
      best = e.code;
    }
  }
  return best;
}

float CadDxfLineweightMmFromEnum370(int code) {
  if (code < 0)
    return -1.f;
  for (const auto& e : kDxfLwTable) {
    if (e.code == code)
      return e.mm;
  }
  return -1.f;
}

// ---------------------------------------------------------------------------
// PDFATTACH
// ---------------------------------------------------------------------------
void StartPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active              = AppCommandState::Kind::PdfAttach;
  st.pdfAttachPhase      = AppCommandState::PdfAttachPhase::WaitDialog;
  st.pdfAttachDialogOpen = true;
  log.push_back("PDFATTACH — select PDF file and options in the dialog, then place the underlay.");
}

void SubmitPdfAttachInsertPoint(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::PdfAttach ||
      st.pdfAttachPhase != AppCommandState::PdfAttachPhase::WaitInsertPoint)
    return;

  // Build the committed attachment at the picked world location.
  PdfAttachment att;
  bool ok = PdfAttach_Build(st.pdfAttachFilePath,
                             st.pdfAttachSelectedPage,
                             st.pdfAttachRasterDpi,
                             st.pdfAttachSnapLines,
                             st.pdfAttachSnapCircles,
                             st.pdfAttachSnapText,
                             att);
  if (ok) {
    att.insertX    = wx;
    att.insertY    = wy;
    att.scale      = st.pdfAttachScale;
    att.rotationDeg = st.pdfAttachRotDeg;
    const int nSnapLines   = static_cast<int>(att.snapLinesFlat.size())    / 4;
    const int nSnapCircles = static_cast<int>(att.snapCirclesCxCyR.size()) / 3;
    const int nSnapText    = static_cast<int>(att.snapTextPos.size())      / 2;
    st.pdfAttachments.push_back(std::move(att));
    char snapMsg[128];
    std::snprintf(snapMsg, sizeof(snapMsg),
                  "PDFATTACH — placed.  Snap: %d lines, %d circles, %d text.",
                  nSnapLines, nSnapCircles, nSnapText);
    log.push_back(snapMsg);
  } else {
    log.push_back("PDFATTACH — failed to rasterize page.");
  }

  // Release preview texture and reset to idle.
  if (st.pdfAttachPreviewReady) {
    PdfAttach_ReleaseTexture(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
  }
  st.active         = AppCommandState::Kind::None;
  st.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
}

void CancelPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("PDFATTACH cancelled.");
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::None;
}

void VectorizePdfAttachmentLines(AppCommandState& st, int pdfIndex, std::vector<std::string>& log) {
  if (pdfIndex < 0 || pdfIndex >= static_cast<int>(st.pdfAttachments.size()))
    return;
  const PdfAttachment& att = st.pdfAttachments[static_cast<size_t>(pdfIndex)];
  const auto& snap = att.snapLinesFlat;
  if (snap.empty()) {
    log.push_back("Vectorize — no line snap geometry in this PDF underlay.");
    return;
  }
  constexpr float kPi = 3.14159265f;
  const float cosR = std::cos(att.rotationDeg * kPi / 180.f);
  const float sinR = std::sin(att.rotationDeg * kPi / 180.f);
  int n = 0;
  for (size_t i = 0; i + 3 < snap.size(); i += 4) {
    const float sx1 = snap[i]     * att.scale, sy1 = snap[i + 1] * att.scale;
    const float sx2 = snap[i + 2] * att.scale, sy2 = snap[i + 3] * att.scale;
    st.userLinesFlat.push_back(att.insertX + sx1 * cosR - sy1 * sinR);
    st.userLinesFlat.push_back(att.insertY + sx1 * sinR + sy1 * cosR);
    st.userLinesFlat.push_back(0.f);
    st.userLinesFlat.push_back(att.insertX + sx2 * cosR - sy2 * sinR);
    st.userLinesFlat.push_back(att.insertY + sx2 * sinR + sy2 * cosR);
    st.userLinesFlat.push_back(0.f);
    ++n;
  }
  EnsureAttrCounts(st);
  // Tag newly-added lines with this underlay's layer.
  if (!att.layer.empty()) {
    const size_t total = st.userLinesFlat.size() / 6;
    const size_t firstNew = total - static_cast<size_t>(n);
    for (size_t k = firstNew; k < total && k < st.userLineAttrs.size(); ++k)
      st.userLineAttrs[k].layer = att.layer;
  }
  BumpCadGpuCache(st);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Vectorize — added %d line segment%s from PDF underlay.",
                n, n == 1 ? "" : "s");
  log.push_back(buf);
}

bool LoadApplicationFont() {
  ImGuiIO& io = ImGui::GetIO();
  const char* candidates[] = {
      "C:/Windows/Fonts/calibri.ttf",
      "C:/Windows/Fonts/Calibri.ttf",
  };
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  for (const char* path : candidates) {
    ImFont* f = io.Fonts->AddFontFromFileTTF(path, 18.5f, &cfg);
    if (f) {
      io.FontDefault = f;
      return true;
    }
  }
  return false;
}
