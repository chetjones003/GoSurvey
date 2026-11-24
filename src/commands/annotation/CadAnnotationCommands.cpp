#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"
#include "annotation/CadAnnotationInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "TextStyle.hpp"
#include "GoSurvey/Document/MtextRichFormat.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
    ann->rotationRad = 0.f;  // angular text is horizontal (REQ-477, issue #804)
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
  const auto* ds = st.sessionAs<stepengine::DimSession>();
  if (!out || !ds || ds->linear || ds->phase != stepengine::DimPhase::WaitDimLinePt)
    return false;
  const float x1 = ds->e1x, y1 = ds->e1y, x2 = ds->e2x, y2 = ds->e2y;
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
  d.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  d.insZ = CadCommitElevation(st);
  d.dimExt1X = x1;
  d.dimExt1Y = y1;
  d.dimExt2X = x2;
  d.dimExt2Y = y2;
  d.dimSignedOffset = dOff;
  d.plottedHeightInches = std::max(st.drawing.activeDimensionStyle.textSizeInches, 1.e-6f);
  d.fontFamily = st.drawing.activeDimensionStyle.textFont;
  d.text = DimensionStyles::FormatLinearDim(static_cast<double>(len), st.drawing.activeDimensionStyle);
  d.rotationRad = CadDimReadableTextRotation(std::atan2(vy, vx));
  const float hWorld = CadAnnotationHeightWorld(d, st.drawing.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &d.insX, &d.insY);
  *out = std::move(d);
  return true;
}

void CadDimLinearUpdateDraftOrientation(AppCommandState& st, float cursorWx, float cursorWy) {
  auto* ds = st.sessionAs<stepengine::DimSession>();
  if (!ds || !ds->linear || ds->phase != stepengine::DimPhase::WaitDimLinePt)
    return;
  const float cmx = 0.5f * (ds->e1x + ds->e2x);
  const float cmy = 0.5f * (ds->e1y + ds->e2y);
  const float chord = std::hypot(ds->e2x - ds->e1x, ds->e2y - ds->e1y);
  const float unlockTol = 1.e-3f * std::max(1.f, chord);

  const float dxSpan = std::fabs(ds->e2x - ds->e1x);
  const float dySpan = std::fabs(ds->e2y - ds->e1y);
  const float spanTol =
      std::max(1e-8f, 1e-12f * std::max(std::max(std::fabs(ds->e1x), std::fabs(ds->e1y)),
                                        std::max(std::fabs(ds->e2x), std::fabs(ds->e2y))));
  const bool mustVertical = dxSpan <= spanTol && dySpan > spanTol;
  const bool mustHorizontal = dySpan <= spanTol && dxSpan > spanTol;
  if (mustVertical || mustHorizontal) {
    ds->linearDraftVertical = mustVertical;
    ds->linearOrientUserLock = false;
    return;
  }

  if (ds->linearOrientUserLock) {
    if (std::hypot(cursorWx - ds->lockCursorWx, cursorWy - ds->lockCursorWy) > unlockTol)
      ds->linearOrientUserLock = false;
  }
  if (!ds->linearOrientUserLock) {
    const float adx = std::fabs(cursorWx - cmx);
    const float ady = std::fabs(cursorWy - cmy);
    ds->linearDraftVertical = adx > ady;
  }
}

void CadDimLinearApplyHVHotkey(AppCommandState& st, bool vertical, std::vector<std::string>& log) {
  auto* ds = st.sessionAs<stepengine::DimSession>();
  if (!ds || !ds->linear || ds->phase != stepengine::DimPhase::WaitDimLinePt)
    return;
  ds->linearDraftVertical = vertical;
  ds->linearOrientUserLock = true;
  ds->lockCursorWx = st.uiCursorWorldX;
  ds->lockCursorWy = st.uiCursorWorldY;
  log.push_back(vertical ? "DIMLINEAR — vertical span (V). Move crosshair to unlock orientation."
                         : "DIMLINEAR — horizontal span (H). Move crosshair to unlock orientation.");
  BumpCadGpuCache(st);
}

bool CadDimLinearBuildDraft(AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  auto* ds = st.sessionAs<stepengine::DimSession>();
  if (!out || !ds || !ds->linear || ds->phase != stepengine::DimPhase::WaitDimLinePt)
    return false;
  CadDimLinearUpdateDraftOrientation(st, cursorWx, cursorWy);
  const float x1 = ds->e1x, y1 = ds->e1y, x2 = ds->e2x, y2 = ds->e2y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const bool vert = ds->linearDraftVertical;
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
  d.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  d.insZ = CadCommitElevation(st);  // the draft previews at the elevation it will commit to (REQ-058)
  d.dimExt1X = x1;
  d.dimExt1Y = y1;
  d.dimExt2X = x2;
  d.dimExt2Y = y2;
  d.dimSignedOffset = dOff;
  d.dimLinearVertical = vert;
  d.plottedHeightInches = std::max(st.drawing.activeDimensionStyle.textSizeInches, 1.e-6f);
  d.fontFamily = st.drawing.activeDimensionStyle.textFont;
  d.text = DimensionStyles::FormatLinearDim(static_cast<double>(meas), st.drawing.activeDimensionStyle);
  float tx = 0.f, ty = 0.f;
  if (!vert) {
    tx = (x2 >= x1) ? 1.f : -1.f;
    ty = 0.f;
  } else {
    tx = 0.f;
    ty = (y2 >= y1) ? 1.f : -1.f;
  }
  d.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
  const float hWorld = CadAnnotationHeightWorld(d, st.drawing.modelUnitsPerPlottedInch);
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

void CadDimRefreshMeasurementText(CadAnnotation* ann, const DimensionStyle& style, const AngleDisplaySettings& angle) {
  if (!ann || !ann->dimensionTextOverride.empty())
    return;
  if (ann->kind == CadAnnotation::Kind::DimAligned || ann->kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (!CadDimAnyGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      return;
    // REQ-459 (issue #804): the value uses the dimension style's unit scale, format and precision, the same
    // formatting the commit path uses, so moving a dimension never changes its number.
    ann->text = DimensionStyles::FormatLinearDim(static_cast<double>(ml), style);
  } else if (ann->kind == CadAnnotation::Kind::DimAngular) {
    float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
    if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
      return;
    ann->text = FormatSweptAngle(static_cast<double>(theta) * (180.0 / 3.14159265358979323846), angle);
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
  ann->rotationRad = 0.f;  // angular text is horizontal (REQ-477, issue #804)
}

bool CadDimAngularBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  const auto* ds = st.sessionAs<stepengine::DimAngularSession>();
  if (!out || !ds || ds->phase != stepengine::DimAngularPhase::WaitArc)
    return false;
  const float vx = ds->vx, vy = ds->vy;
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAngular;
  d.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  d.insZ = CadCommitElevation(st);
  d.dimAngVertexX = vx;
  d.dimAngVertexY = vy;
  d.dimExt1X = ds->e1x;
  d.dimExt1Y = ds->e1y;
  d.dimExt2X = ds->e2x;
  d.dimExt2Y = ds->e2y;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(d, &a1, &a2, &sweep, &bisx, &bisy, &theta))
    return false;
  const float leg = std::min(std::hypot(d.dimExt1X - vx, d.dimExt1Y - vy), std::hypot(d.dimExt2X - vx, d.dimExt2Y - vy));
  const float rMax = std::max(1.e-4f, 0.92f * leg);
  const float rMin = std::max(1.e-4f, 0.02f * leg);
  const float R = CadDimAngularPickRadius(vx, vy, bisx, bisy, cursorWx, cursorWy, rMin, rMax);
  d.dimSignedOffset = R;
  d.plottedHeightInches = std::max(st.drawing.activeDimensionStyle.textSizeInches, 1.e-6f);
  d.fontFamily = st.drawing.activeDimensionStyle.textFont;
  d.text = FormatSweptAngle(static_cast<double>(theta) * (180.0 / 3.14159265358979323846), CadAngleDisplaySettings(st));
  CadDimAngularSyncTextPlacement(&d, st.drawing.modelUnitsPerPlottedInch);
  *out = std::move(d);
  return true;
}

void CadAnnotationRoughBounds(const CadAnnotation& a, float modelUnitsPerPlottedInch, float* outMnX, float* outMnY,
                              float* outMxX, float* outMxY) {
  const float h = CadAnnotationHeightWorld(a, modelUnitsPerPlottedInch);
  if (CadAnnotationHasTextBox(a.kind)) {
    // Delegated, not duplicated — CadTextAnnotationBounds returns the same stored box. Kept as an
    // explicit early-out only because the dimension branch below must not fall through to it.
    CadTextAnnotationBounds(a, h, outMnX, outMnY, outMxX, outMxY);
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
    const float tw = CadTextEstimatedWidth(a.text, h);  // the dimension LABEL's own quad
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
  // Plain TEXT: the shared rule, one definition with paper space (TASK-198).
  CadTextAnnotationBounds(a, h, outMnX, outMnY, outMxX, outMxY);
}


void CancelTableCellEditor(AppCommandState& st) {
  st.tableCellEditorOpen = false;
  st.tableCellEditorIndex = -1;
  st.tableCellEditorCell = -1;
  st.tableCellEditorBuf.clear();
  st.tableCellEditorFocusRequest = false;
}

void OpenTableCellEditor(AppCommandState& st, int tableIndex, int cellIndex) {
  if (tableIndex < 0 || static_cast<size_t>(tableIndex) >= st.drawing.cadTables.size() || cellIndex < 0)
    return;
  CadTable& t = st.drawing.cadTables[static_cast<size_t>(tableIndex)];
  if (CadTableCellIsSurveyPointData(st.drawing.tableStyles, t, cellIndex))
    return;
  const int n = CadTableRowCount(t) * std::max(t.cols, 1);
  if (cellIndex >= n)
    return;
  while (static_cast<int>(t.cells.size()) <= cellIndex)
    t.cells.emplace_back();
  st.tableCellEditorOpen = true;
  st.tableCellEditorIndex = tableIndex;
  st.tableCellEditorCell = cellIndex;
  st.tableCellEditorBuf = t.cells[static_cast<size_t>(cellIndex)];
  st.tableCellEditorFocusRequest = true;
}

void CommitTableCellEditor(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.tableCellEditorOpen)
    return;
  const int ti = st.tableCellEditorIndex;
  const int ci = st.tableCellEditorCell;
  if (ti >= 0 && static_cast<size_t>(ti) < st.drawing.cadTables.size() && ci >= 0) {
    CadTable& t = st.drawing.cadTables[static_cast<size_t>(ti)];
    if (CadTableCellIsSurveyPointData(st.drawing.tableStyles, t, ci)) {
      CancelTableCellEditor(st);
      return;
    }
    PushUndoSnapshot(st, "Edit table cell");
    while (static_cast<int>(t.cells.size()) <= ci)
      t.cells.emplace_back();
    t.cells[static_cast<size_t>(ci)] = st.tableCellEditorBuf;
    CadTableFitToContent(&t, st.drawing.modelUnitsPerPlottedInch);
    BumpCadGpuCache(st);
    log.push_back("TABLE — cell updated.");
  }
  CancelTableCellEditor(st);
}

void MigrateLegacyAnnotationTables(AppCommandState& st) {
  for (int i = static_cast<int>(st.drawing.cadAnnotations.size()) - 1; i >= 0; --i) {
    const CadAnnotation& a = st.drawing.cadAnnotations[static_cast<size_t>(i)];
    if (a.kind != CadAnnotation::Kind::Table)
      continue;
    CadTable t = CadTableFromAxisAlignedBox(a.boxMinX, a.boxMinY, a.boxMaxX, a.boxMaxY, a.tableCols, a.tableCells,
                                            a.insZ, a.plottedHeightInches, a.fontFamily);
    st.drawing.cadTables.push_back(std::move(t));
    EntityAttributes at{};
    if (static_cast<size_t>(i) < st.drawing.cadAnnotationAttrs.size())
      at = st.drawing.cadAnnotationAttrs[static_cast<size_t>(i)];
    st.drawing.cadTableAttrs.push_back(std::move(at));
    EraseCadAnnotationAtIndex(st, static_cast<size_t>(i));
  }
  EnsureAttrCounts(st);
}


void StartTextCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  st.selectedSurveyPointIndices.clear();
  stepengine::BeginSession(st, std::make_unique<stepengine::TextSession>(DefaultAnnotationTextHeightWorld(st)),
                           /*recordAsLastCommand=*/false);
  log.push_back(
      "TEXT — pick insertion, then height / rotation / string on command line (defaults from plot scale). ESC "
      "cancels.");
}

void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  st.selectedSurveyPointIndices.clear();
  stepengine::BeginSession(st, std::make_unique<stepengine::MtextSession>(), /*recordAsLastCommand=*/false);
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
  if (annIndex < 0 || static_cast<size_t>(annIndex) >= st.drawing.cadAnnotations.size())
    return;
  CadAnnotation& a = st.drawing.cadAnnotations[static_cast<size_t>(annIndex)];
  // REQ-039 phase 2: double-click edits MTEXT (rich) or single-line TEXT (plain). Other kinds are not text.
  if (a.kind != CadAnnotation::Kind::Mtext && a.kind != CadAnnotation::Kind::Text)
    return;
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = false;
  st.mtextRichEditorPaperLayout = -1;
  st.mtextRichEditorPlain = (a.kind == CadAnnotation::Kind::Text);
  st.mtextRichEditorAnnIndex = annIndex;
  st.mtextRichEditorBuf = a.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back(st.mtextRichEditorPlain
                       ? "TEXT — edit the contents; Enter or Save to update; Esc to cancel."
                       : "MTEXT — edit in the box; Ctrl+Enter reformats; Save to update; Esc to cancel.");
}

// In-place editor for a native paper-space text (REQ-039 phase 2): the SAME editor as model text, retargeted
// to the active layout's paperTexts store. Plain box for Kind::Text, rich box for Kind::Mtext.
void OpenPaperTextEditor(AppCommandState& st, int layoutIndex, int textIndex, std::vector<std::string>* log) {
  if (layoutIndex < 0 || static_cast<size_t>(layoutIndex) >= st.drawing.paperLayouts.size())
    return;
  PaperLayout& L = st.drawing.paperLayouts[static_cast<size_t>(layoutIndex)];
  if (textIndex < 0 || static_cast<size_t>(textIndex) >= L.paperTexts.size())
    return;
  CadAnnotation& a = L.paperTexts[static_cast<size_t>(textIndex)];
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = true;
  st.mtextRichEditorPaperLayout = layoutIndex;
  st.mtextRichEditorPlain = (a.kind == CadAnnotation::Kind::Text);
  st.mtextRichEditorAnnIndex = textIndex;
  st.mtextRichEditorBuf = a.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back(st.mtextRichEditorPlain
                       ? "Paper text — edit the contents; Enter or Save to update; Esc to cancel."
                       : "Paper MTEXT — edit in the box; Ctrl+Enter reformats; Save to update; Esc to cancel.");
}

void CommitMtextRichEditor(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.mtextRichEditorOpen)
    return;
  using K = AppCommandState::Kind;
  if (st.mtextRichEditorPlacement) {
    auto* ms = st.sessionAs<stepengine::MtextSession>();
    if (!ms || ms->phase != stepengine::MtextPhase::WaitString) {
      CloseMtextRichEditorUi(st);
      return;
    }
    const std::string normalized = MtextRichNormalize(st.mtextRichEditorBuf);
    if (!StringUtil::trimCopy(MtextRichFlattenToPlain(normalized)).empty()) {
      PushUndoSnapshot(st, "MTEXT");
      CadAnnotation ann;
      ann.kind = CadAnnotation::Kind::Mtext;
      ann.boxMinX = std::min(ms->x1, ms->x2);
      ann.boxMinY = std::min(ms->y1, ms->y2);
      ann.boxMaxX = std::max(ms->x1, ms->x2);
      ann.boxMaxY = std::max(ms->y1, ms->y2);
      ann.insX = ann.boxMinX;
      ann.insY = ann.boxMinY;
      ann.plottedHeightInches = st.drawing.defaultPlottedTextHeightInches;
      ann.text = normalized;
      ann.insZ = CadCommitElevation(st);
      StampActiveTextStyleOnNewText(st, ann);
      st.drawing.cadAnnotations.push_back(std::move(ann));
      st.drawing.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
      BumpCadGpuCache(st);
      log.push_back("MTEXT placed.");
    } else
      log.push_back("MTEXT — empty; canceled.");
    stepengine::EndSession(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  // Edit an existing text object (model cadAnnotations or paper paperTexts; MTEXT rich or TEXT plain).
  const bool paper = st.mtextRichEditorPaper;
  const bool plain = st.mtextRichEditorPlain;
  if (MtextRichEditorTargetAnnotation(st)) {
    const bool marker = st.mtextRichEditorMarkerIndex >= 0;  // a Position Marker's label (REQ-359)
    const bool multileader = st.mtextRichEditorMultileaderIndex >= 0;  // REQ-367
    // The label typed right after placing belongs to the placement's undo step.
    if (!(marker && st.mtextRichEditorMarkerJustPlaced) &&
        !(multileader && st.mtextRichEditorMultileaderJustPlaced))
      PushUndoSnapshot(st, marker ? "Position Marker label"
                                  : multileader ? "Multileader label"
                                                : paper ? "Paper text edit"
                                                        : (plain ? "TEXT edit" : "MTEXT edit"));
    // Re-resolve after the snapshot (it does not mutate the live stores, but keep the access pattern safe).
    CadAnnotation* ann = MtextRichEditorTargetAnnotation(st);
    if (ann) {
      if (plain) {
        // Single-line TEXT: store contents verbatim, collapsing any stray newlines (the box is single-line,
        // but paste can introduce them). No MTEXT normalization — that would inject [[…]] tags into TEXT.
        std::string t = st.mtextRichEditorBuf;
        for (char& c : t)
          if (c == '\n' || c == '\r')
            c = ' ';
        ann->text = std::move(t);
      } else {
        ann->text = MtextRichNormalize(st.mtextRichEditorBuf);
        const int linkedPi = paper ? -1 : SurveyPointIndexForId(st, ann->surveyPointLabelForId);
        if (linkedPi >= 0)
          RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(linkedPi));
      }
      BumpCadGpuCache(st);
      if (multileader && st.mtextRichEditorMultileaderJustPlaced && st.active == K::Mleader) {
        FinishMleaderCommand(st, log);
        return;
      }
      log.push_back(marker ? "Position Marker label updated."
                           : multileader ? "Multileader label updated."
                                         : paper ? "Paper text updated."
                                                 : (plain ? "TEXT updated." : "MTEXT updated."));
    }
  }
  CloseMtextRichEditorUi(st);
}

void CancelMtextRichEditor(AppCommandState& st, std::vector<std::string>* log) {
  if (!st.mtextRichEditorOpen)
    return;
  if (st.mtextRichEditorMultileaderIndex >= 0 && st.mtextRichEditorMultileaderJustPlaced) {
    AbandonJustPlacedMultileader(st);
    if (log)
      log->push_back("MLEADER — canceled.");
    stepengine::EndSession(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  if (st.mtextRichEditorPlacement) {
    if (log)
      log->push_back("MTEXT — canceled.");
    stepengine::EndSession(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  CloseMtextRichEditorUi(st);
}

void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  st.selectedSurveyPointIndices.clear();
  stepengine::BeginSession(st, std::make_unique<stepengine::DimSession>(false), /*recordAsLastCommand=*/true);
  log.push_back("DIMALIGNED — extension 1, extension 2, then offset (point away from measured line). ESC cancels.");
}

void StartDimLinearCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  st.selectedSurveyPointIndices.clear();
  stepengine::BeginSession(st, std::make_unique<stepengine::DimSession>(true), /*recordAsLastCommand=*/true);
  log.push_back("DIMLINEAR — ortho distance in X or Y between extension points; third pick sets dimension line; "
                "cursor or H/V chooses horizontal vs vertical. ESC cancels.");
}

void StartDimAngularCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  st.selectedSurveyPointIndices.clear();
  stepengine::BeginSession(st, std::make_unique<stepengine::DimAngularSession>(), /*recordAsLastCommand=*/true);
  log.push_back("DIMANGULAR — vertex, two ray points, then arc position (radius). Text is degrees/minutes/seconds. ESC "
                "cancels.");
}

void StartDimStyleCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.dimStyleDraft = st.drawing.activeDimensionStyle;
  st.showDimStyleDialog = true;
  log.push_back("DIMSTY — dimension style editor opened.");
}
