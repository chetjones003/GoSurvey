#include "CadCommandsInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>


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


static void CommitDimAngularAt(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* ds = st.sessionAs<stepengine::DimAngularSession>();
  if (!ds)
    return;
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAngular;
  d.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  d.insZ = CadCommitElevation(st);
  d.dimAngVertexX = ds->vx;
  d.dimAngVertexY = ds->vy;
  d.dimExt1X = ds->e1x;
  d.dimExt1Y = ds->e1y;
  d.dimExt2X = ds->e2x;
  d.dimExt2Y = ds->e2y;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, bisx = 0.f, bisy = 0.f;
  double theta = 0.0;
  if (!CadDimAngularComputeFrame(d, &a1, &a2, &sweep, &bisx, &bisy, &theta)) {
    log.push_back("DIMANGULAR — points are degenerate or collinear.");
    return;
  }
  if (theta < 1e-5) {
    log.push_back("DIMANGULAR — angle is zero (ray points coincide with vertex direction).");
    return;
  }
  const double vx = ds->vx, vy = ds->vy;
  const float leg = std::min(std::hypot(ds->e1x - vx, ds->e1y - vy), std::hypot(ds->e2x - vx, ds->e2y - vy));
  const float rMax = std::max(1.e-4f, 0.92f * leg);
  const float rMin = std::max(1.e-4f, 0.02f * leg);
  const float R = CadDimAngularPickRadius(vx, vy, bisx, bisy, wx, wy, rMin, rMax);
  d.dimSignedOffset = R;
  d.plottedHeightInches = std::max(st.drawing.activeDimensionStyle.textSizeInches, 1.e-6f);
  d.fontFamily = st.drawing.activeDimensionStyle.textFont;
  d.text = FormatSweptAngle(theta * (180.0 / 3.14159265358979323846), CadAngleDisplaySettings(st));
  CadDimAngularSyncTextPlacement(&d, st.drawing.modelUnitsPerPlottedInch);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  PushUndoSnapshot(st, "DIMANGULAR");
  st.drawing.cadAnnotations.push_back(std::move(d));
  st.drawing.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  ds->resetDraft();
  log.push_back("DIMANGULAR complete.");
  log.push_back("DIMANGULAR — vertex, two ray points, then arc position. ESC to exit.");
}


static std::string DimStyleEntityColor(const DimensionStyle& sty, const EntityAttributes& base) {
  if (!sty.dimLineColor.empty() && sty.dimLineColor != "ByLayer" && sty.dimLineColor != "ByBlock")
    return sty.dimLineColor;
  if (!base.color.empty() && base.color != "ByLayer" && base.color != "ByBlock")
    return base.color;
  return std::string("ByLayer");
}

static void CommitDimAlignedAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  auto* ds = st.sessionAs<stepengine::DimSession>();
  if (!ds)
    return;
  const double x1 = ds->e1x, y1 = ds->e1y;
  const double x2 = ds->e2x, y2 = ds->e2y;
  double vx = x2 - x1;
  double vy = y2 - y1;
  const double len = std::hypot(vx, vy);
  if (len < 1e-8) {
    log.push_back("DIMALIGNED — extension points coincide.");
    return;
  }
  vx /= len;
  vy /= len;
  const double t1 = (x1 - lx) * vx + (y1 - ly) * vy;
  const double t2 = (x2 - lx) * vx + (y2 - ly) * vy;
  const double sx1 = lx + vx * t1;
  const double sy1 = ly + vy * t1;
  const double sx2 = lx + vx * t2;
  const double sy2 = ly + vy * t2;
  const double cmx = 0.5 * (x1 + x2);
  const double cmy = 0.5 * (y1 + y2);
  const double n0x = -vy;
  const double n0y = vx;
  const double dmx = 0.5 * (sx1 + sx2);
  const double dmy = 0.5 * (sy1 + sy2);
  const double dOff = (dmx - cmx) * n0x + (dmy - cmy) * n0y;
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::DimAligned;
  ann.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  ann.insZ = CadCommitElevation(st);
  ann.dimExt1X = x1;
  ann.dimExt1Y = y1;
  ann.dimExt2X = x2;
  ann.dimExt2Y = y2;
  ann.dimSignedOffset = dOff;
  ann.rotationRad = CadDimReadableTextRotation(std::atan2(vy, vx));
  DimensionStyles::BakeTextOntoDimension(ann, st.drawing.activeDimensionStyle);
  CadDimRefreshMeasurementText(&ann, CadDimensionStyleForAnnotation(st, ann), CadAngleDisplaySettings(st));
  const float hWorld = CadAnnotationHeightWorld(ann, st.drawing.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &ann.insX, &ann.insY);
  EntityAttributes at = MakeNewEntityAttrs(st);
  const std::string preferredLayer = at.layer;
  at.layer = CadVisibleLayerForNewEntity(st, preferredLayer);
  if (at.layer != preferredLayer)
    log.push_back("DIMALIGNED — current layer is off or frozen; dimension placed on layer \"" + at.layer + "\".");
  at.color = DimStyleEntityColor(st.drawing.activeDimensionStyle, at);
  PushUndoSnapshot(st, "DIMALIGNED");
  st.drawing.cadAnnotations.push_back(std::move(ann));
  st.drawing.cadAnnotationAttrs.push_back(at);
  ClearCadSelection(st);
  st.selection.push_back({SelectedEntity::Type::Annotation, static_cast<int>(st.drawing.cadAnnotations.size()) - 1});
  BumpCadGpuCache(st);
  ds->resetDraft();
  log.push_back("DIMALIGNED complete.");
  log.push_back("DIMALIGNED — extension 1, extension 2, then offset. ESC to exit.");
}

static void CommitDimLinearAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  auto* ds = st.sessionAs<stepengine::DimSession>();
  if (!ds)
    return;
  CadDimLinearUpdateDraftOrientation(st, lx, ly);
  const double x1 = ds->e1x, y1 = ds->e1y;
  const double x2 = ds->e2x, y2 = ds->e2y;
  const double cmx = 0.5 * (x1 + x2);
  const double cmy = 0.5 * (y1 + y2);
  const bool vert = ds->linearDraftVertical;
  const double meas = vert ? std::fabs(y2 - y1) : std::fabs(x2 - x1);
  if (meas < 1e-8) {
    log.push_back(vert ? "DIMLINEAR — extension points have the same Y (zero vertical span)."
                       : "DIMLINEAR — extension points have the same X (zero horizontal span).");
    return;
  }
  double dmx = cmx;
  double dmy = cmy;
  float n0x = 0.f;
  float n0y = 1.f;
  double dOff = 0.0;
  if (!vert) {
    dmy = ly;
    dOff = dmy - cmy;
  } else {
    dmx = lx;
    dOff = dmx - cmx;
    n0x = 1.f;
    n0y = 0.f;
  }
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::DimLinear;
  ann.dimensionStyleName = st.drawing.activeDimensionStyle.name;
  ann.insZ = CadCommitElevation(st);
  ann.dimExt1X = x1;
  ann.dimExt1Y = y1;
  ann.dimExt2X = x2;
  ann.dimExt2Y = y2;
  ann.dimSignedOffset = dOff;
  ann.dimLinearVertical = vert;
  float tx = 0.f, ty = 0.f;
  if (!vert) {
    tx = (x2 >= x1) ? 1.f : -1.f;
    ty = 0.f;
  } else {
    tx = 0.f;
    ty = (y2 >= y1) ? 1.f : -1.f;
  }
  ann.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
  DimensionStyles::BakeTextOntoDimension(ann, st.drawing.activeDimensionStyle);
  CadDimRefreshMeasurementText(&ann, CadDimensionStyleForAnnotation(st, ann), CadAngleDisplaySettings(st));
  const float hWorld = CadAnnotationHeightWorld(ann, st.drawing.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &ann.insX, &ann.insY);
  EntityAttributes at = MakeNewEntityAttrs(st);
  const std::string preferredLayer = at.layer;
  at.layer = CadVisibleLayerForNewEntity(st, preferredLayer);
  if (at.layer != preferredLayer)
    log.push_back("DIMLINEAR — current layer is off or frozen; dimension placed on layer \"" + at.layer + "\".");
  at.color = DimStyleEntityColor(st.drawing.activeDimensionStyle, at);
  PushUndoSnapshot(st, "DIMLINEAR");
  st.drawing.cadAnnotations.push_back(std::move(ann));
  st.drawing.cadAnnotationAttrs.push_back(at);
  ClearCadSelection(st);
  st.selection.push_back({SelectedEntity::Type::Annotation, static_cast<int>(st.drawing.cadAnnotations.size()) - 1});
  BumpCadGpuCache(st);
  ds->resetDraft();
  log.push_back("DIMLINEAR complete.");
  log.push_back("DIMLINEAR — extension 1, extension 2, then dimension line. ESC to exit.");
}
