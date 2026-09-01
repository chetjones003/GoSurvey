// CadCommands_Align.cpp — ALIGN (2D Helmert / similarity transform), split out
// of CadCommands.cpp (TASK-150 Phase 2, GitHub issue #142).
//
// Same TU conventions as CadCommands.cpp: the ALIGN entry points
// (StartAlignCommand / RecalcAlignResult / ApplyAlignCommand / ExecuteAlignCommand /
// AlignCommandFooterHint) are declared in CadCommands.hpp and dispatched from
// there; the Helmert solver and the geometry-walk helper are file-local statics.

#include "CadCommands.hpp"
#include "CadDimGeom.hpp"
#include "CadDimStroke.hpp"
#include "SurveyPoints.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// ALIGN — 2D Helmert (similarity) transformation
// ---------------------------------------------------------------------------

// Solves the 4×4 normal equations A^T A p = A^T b via Gaussian elimination.
// Returns false if the system is singular (< 2 control pairs or degenerate).
static bool SolveHelmert4x4(
    const std::vector<AppCommandState::AlignControlPt>& pts,
    float* outA, float* outB, float* outTx, float* outTy)
{
  // Build M = A^T A (symmetric 4×4) and rhs = A^T b (4×1)
  // Using analytic structure:
  //   M[0][0] = Σ(xi²+yi²), M[0][1]=0, M[0][2]=Σxi, M[0][3]=Σyi
  //   M[1][1] = M[0][0],    M[1][2]=-Σyi, M[1][3]=Σxi
  //   M[2][2] = n,           M[2][3]=0
  //   M[3][3] = n
  double sr2 = 0, sx = 0, sy = 0, sX = 0, sY = 0, sxX_yY = 0, smyX_xY = 0;
  const int n = static_cast<int>(pts.size());
  for (const auto& p : pts) {
    const double xi = p.srcX, yi = p.srcY, Xi = p.dstX, Yi = p.dstY;
    sr2    += xi * xi + yi * yi;
    sx     += xi;
    sy     += yi;
    sX     += Xi;
    sY     += Yi;
    sxX_yY  += xi * Xi + yi * Yi;
    smyX_xY += -yi * Xi + xi * Yi;
  }

  // Augmented matrix [M | rhs], 4 rows × 5 cols
  double m[4][5] = {
    { sr2,  0.0,   sx,  sy,  sxX_yY  },
    { 0.0,  sr2,  -sy,  sx,  smyX_xY },
    {  sx,  -sy,  static_cast<double>(n), 0.0, sX },
    {  sy,   sx,  0.0,  static_cast<double>(n), sY },
  };

  // Gaussian elimination with partial pivoting
  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    for (int row = col + 1; row < 4; ++row)
      if (std::fabs(m[row][col]) > std::fabs(m[pivot][col]))
        pivot = row;
    if (std::fabs(m[pivot][col]) < 1e-12)
      return false;
    if (pivot != col)
      for (int k = 0; k <= 4; ++k)
        std::swap(m[pivot][k], m[col][k]);
    const double inv = 1.0 / m[col][col];
    for (int row = col + 1; row < 4; ++row) {
      const double f = m[row][col] * inv;
      for (int k = col; k <= 4; ++k)
        m[row][k] -= f * m[col][k];
    }
  }
  // Back-substitution
  double sol[4];
  for (int row = 3; row >= 0; --row) {
    sol[row] = m[row][4];
    for (int k = row + 1; k < 4; ++k)
      sol[row] -= m[row][k] * sol[k];
    sol[row] /= m[row][row];
  }
  *outA  = static_cast<float>(sol[0]);
  *outB  = static_cast<float>(sol[1]);
  *outTx = static_cast<float>(sol[2]);
  *outTy = static_cast<float>(sol[3]);
  return true;
}

// Applies X' = a*x - b*y + tx, Y' = b*x + a*y + ty to a 2D point.
static inline void HelmertPt(float a, float b, float tx, float ty, float* x, float* y) {
  const float ox = *x, oy = *y;
  *x = a * ox - b * oy + tx;
  *y = b * ox + a * oy + ty;
}

static void ApplyHelmertToAllGeometry(AppCommandState& st, float a, float b, float tx, float ty,
                                       const std::vector<SelectedEntity>* selEnts = nullptr,
                                       const std::vector<int>* selSurvey = nullptr) {
  const float sc  = std::sqrt(a * a + b * b);
  const float rad = std::atan2(b, a);
  const bool selective = selEnts != nullptr;
  std::unordered_set<int> sLines, sCircles, sArcs, sEllipses, sPolylines, sAnns, sTables;
  if (selective) {
    for (const auto& se : *selEnts) {
      switch (se.type) {
      case SelectedEntity::Type::LineSeg:    sLines.insert(se.index);    break;
      case SelectedEntity::Type::Circle:     sCircles.insert(se.index);  break;
      case SelectedEntity::Type::Arc:        sArcs.insert(se.index);     break;
      case SelectedEntity::Type::Ellipse:    sEllipses.insert(se.index); break;
      case SelectedEntity::Type::Polyline:   sPolylines.insert(se.index);break;
      case SelectedEntity::Type::Annotation: sAnns.insert(se.index);     break;
      case SelectedEntity::Type::Table:      sTables.insert(se.index);   break;
      default: break;
      }
    }
  }

  // Lines
  for (size_t i = 0; i + 5 < st.userLinesFlat.size(); i += 6) {
    if (selective && !sLines.count(static_cast<int>(i / 6))) continue;
    HelmertPt(a, b, tx, ty, &st.userLinesFlat[i],     &st.userLinesFlat[i + 1]);
    HelmertPt(a, b, tx, ty, &st.userLinesFlat[i + 3], &st.userLinesFlat[i + 4]);
  }

  // Circles
  for (size_t i = 0; i + 3 < st.userCirclesCxCyZR.size(); i += 4) {
    if (selective && !sCircles.count(static_cast<int>(i / 4))) continue;
    HelmertPt(a, b, tx, ty, &st.userCirclesCxCyZR[i], &st.userCirclesCxCyZR[i + 1]);
    st.userCirclesCxCyZR[i + 3] *= sc;
    float anx = 0.f, any = 0.f, anz = 1.f;   // REQ-312: the plane turns with the circle
    CircleNormalAt(st.userCircleNormals, i / 4, &anx, &any, &anz);
    RotateNormalAboutZ(rad, &anx, &any);
    if (i / 4 * 3 + 2 < st.userCircleNormals.size()) {
      st.userCircleNormals[i / 4 * 3 + 0] = anx;
      st.userCircleNormals[i / 4 * 3 + 1] = any;
      st.userCircleNormals[i / 4 * 3 + 2] = anz;
    }
  }

  // Arcs
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    if (selective && !sArcs.count(static_cast<int>(ai))) continue;
    auto& arc = st.userArcs[ai];
    // The start point, taken through the arc's OWN plane before anything moves (REQ-312).
    // `HelmertPt` applies the same rotation + uniform scale + translation the centre gets, so a
    // tilted arc is re-anchored to where its start actually lands rather than trusting `+= rad`,
    // which is only exact while the arc is flat (see the ROTATE / MIRROR paths).
    ray3d::Vec3 sp = CurveWorldPointOnArc(arc, static_cast<double>(arc.startRad));
    float spx = static_cast<float>(sp.x), spy = static_cast<float>(sp.y);
    HelmertPt(a, b, tx, ty, &spx, &spy);   // Helmert is planar; the start point's Z is unchanged
    sp.x = static_cast<double>(spx);
    sp.y = static_cast<double>(spy);
    HelmertPt(a, b, tx, ty, &arc.cx, &arc.cy);
    arc.r        *= sc;
    arc.startRad += rad;
    RotateNormalAboutZ(rad, &arc.nx, &arc.ny);   // REQ-312: so does the arc plane
    CadReanchorArcStart(&arc, sp);   // REQ-312: a no-op on a flat arc, where += rad is exact
  }

  // Ellipses
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    if (selective && !sEllipses.count(static_cast<int>(ei))) continue;
    auto& el = st.userEllipses[ei];
    float mx = el.cx + el.majVx, my = el.cy + el.majVy;
    HelmertPt(a, b, tx, ty, &el.cx, &el.cy);
    HelmertPt(a, b, tx, ty, &mx, &my);
    el.majVx = mx - el.cx;
    el.majVy = my - el.cy;
  }

  // Polylines
  if (!selective) {
    for (size_t i = 0; i + 1 < st.userPolylineVerts.size(); i += 3)
      HelmertPt(a, b, tx, ty, &st.userPolylineVerts[i], &st.userPolylineVerts[i + 1]);
  } else {
    for (size_t pi = 0; pi + 1 < st.userPolylineOffsets.size(); ++pi) {
      if (!sPolylines.count(static_cast<int>(pi))) continue;
      const int vStart = st.userPolylineOffsets[pi];
      const int vEnd   = st.userPolylineOffsets[pi + 1];
      for (int vi = vStart; vi < vEnd; ++vi) {
        size_t base = static_cast<size_t>(vi) * 3;
        if (base + 1 < st.userPolylineVerts.size())
          HelmertPt(a, b, tx, ty, &st.userPolylineVerts[base], &st.userPolylineVerts[base + 1]);
      }
    }
  }

  // Annotations — skip survey-linked MTEXT; repositioned after survey pts below.
  for (size_t ani = 0; ani < st.cadAnnotations.size(); ++ani) {
    auto& ann = st.cadAnnotations[ani];
    if (ann.surveyPointLabelForId >= 0)
      continue;
    if (selective && !sAnns.count(static_cast<int>(ani))) continue;
    HelmertPt(a, b, tx, ty, &ann.insX, &ann.insY);
    switch (ann.kind) {
    case CadAnnotation::Kind::Text:
      ann.rotationRad += rad;
      break;
    case CadAnnotation::Kind::Mtext:
      HelmertPt(a, b, tx, ty, &ann.boxMinX, &ann.boxMinY);
      HelmertPt(a, b, tx, ty, &ann.boxMaxX, &ann.boxMaxY);
      if (ann.boxMinX > ann.boxMaxX) std::swap(ann.boxMinX, ann.boxMaxX);
      if (ann.boxMinY > ann.boxMaxY) std::swap(ann.boxMinY, ann.boxMaxY);
      ann.insX = ann.boxMinX;
      ann.insY = ann.boxMinY;
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      break;
    case CadAnnotation::Kind::Table:
      HelmertPt(a, b, tx, ty, &ann.boxMinX, &ann.boxMinY);
      HelmertPt(a, b, tx, ty, &ann.boxMaxX, &ann.boxMaxY);
      if (ann.boxMinX > ann.boxMaxX) std::swap(ann.boxMinX, ann.boxMaxX);
      if (ann.boxMinY > ann.boxMaxY) std::swap(ann.boxMinY, ann.boxMaxY);
      ann.insX = ann.boxMinX;
      ann.insY = ann.boxMaxY;
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      break;
    case CadAnnotation::Kind::DimAligned:
    case CadAnnotation::Kind::DimLinear: {
      HelmertPt(a, b, tx, ty, &ann.dimExt1X, &ann.dimExt1Y);
      HelmertPt(a, b, tx, ty, &ann.dimExt2X, &ann.dimExt2Y);
      ann.dimSignedOffset *= sc;
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, ttx = 0.f, tty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
      if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &ttx, &tty, &nx, &ny, &ml))
        ann.rotationRad = std::atan2(tty, ttx);
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      CadDimRefreshMeasurementText(&ann, st.displayLinearPrecision, CadAngleDisplaySettings(st));
      break;
    }
    case CadAnnotation::Kind::DimAngular:
      HelmertPt(a, b, tx, ty, &ann.dimAngVertexX, &ann.dimAngVertexY);
      HelmertPt(a, b, tx, ty, &ann.dimExt1X, &ann.dimExt1Y);
      HelmertPt(a, b, tx, ty, &ann.dimExt2X, &ann.dimExt2Y);
      ann.dimSignedOffset *= sc;
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      CadDimAngularSyncTextPlacement(&ann, st.modelUnitsPerPlottedInch);
      CadDimRefreshMeasurementText(&ann, st.displayLinearPrecision, CadAngleDisplaySettings(st));
      break;
    }
  }

  for (size_t ti = 0; ti < st.cadTables.size(); ++ti) {
    if (selective && !sTables.count(static_cast<int>(ti)))
      continue;
    CadTable& t = st.cadTables[ti];
    HelmertPt(a, b, tx, ty, &t.insX, &t.insY);
    t.rotationRad += rad;
    t.width = std::max(t.width * sc, 1.e-3f);
    t.height = std::max(t.height * sc, 1.e-3f);
    t.plottedHeightInches = std::max(t.plottedHeightInches * sc, 1e-6f);
  }

  // PDF underlays
  constexpr float kR2D = 180.f / 3.14159265f;
  for (auto& att : st.pdfAttachments) {
    HelmertPt(a, b, tx, ty, &att.insertX, &att.insertY);
    att.scale      = std::max(att.scale * sc, 1e-9f);
    att.rotationDeg += rad * kR2D;
  }

  // Survey points
  const bool hasSelSurvey = selSurvey != nullptr;
  std::unordered_set<int> surveySet;
  if (hasSelSurvey)
    for (int idx : *selSurvey) surveySet.insert(idx);
  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    if (hasSelSurvey && !surveySet.count(static_cast<int>(i))) continue;
    HelmertPt(a, b, tx, ty, &st.surveyPoints[i].easting, &st.surveyPoints[i].northing);
  }
  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    if (hasSelSurvey && !surveySet.count(static_cast<int>(i))) continue;
    RepositionSurveyLabelMtextForPoint(st, i);
  }

  BumpCadGpuCache(st);
}

void StartAlignCommand(AppCommandState& st, std::vector<std::string>& log) {
  CancelActiveCommand(st, log);
  st.active = AppCommandState::Kind::Align;
  st.alignControlPts.clear();
  st.alignSelectionSnapshot.clear();
  st.alignSurveySnapshot.clear();

  const bool hasExisting = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  if (hasExisting) {
    st.alignSelectionSnapshot = st.selection;
    st.alignSurveySnapshot    = st.selectedSurveyPointIndices;
    st.alignHasSelection      = true;
    st.alignPhase             = AppCommandState::AlignPhase::PickSrc;
    log.push_back("ALIGN — " + std::to_string(st.alignSelectionSnapshot.size()) + " CAD, " +
                  std::to_string(st.alignSurveySnapshot.size()) +
                  " survey point(s) selected. Pick SOURCE survey point 1 in drawing, then its real-world destination. "
                  "≥ 2 pairs → Enter to solve. ESC cancels.");
  } else {
    st.alignHasSelection = false;
    st.alignPhase        = AppCommandState::AlignPhase::PickSelection;
    st.selBoxWaitingSecond = false;
    log.push_back("ALIGN — click objects or drag a selection window to transform, then press Enter. ESC cancels.");
  }
}

void RecalcAlignResult(AppCommandState& st) {
  auto& res = st.alignLastResult;
  res = AppCommandState::HelmertResult{};
  const size_t n = st.alignControlPts.size();
  if (n == 0)
    return;
  res.nPairs = static_cast<int>(n);
  if (n == 1) {
    res.a = 1.f; res.b = 0.f;
    res.tx = st.alignControlPts[0].dstX - st.alignControlPts[0].srcX;
    res.ty = st.alignControlPts[0].dstY - st.alignControlPts[0].srcY;
    res.valid = true;
    res.scale = 1.f;
    res.rotationCwNorthDeg = 0.f;
  } else {
    if (!SolveHelmert4x4(st.alignControlPts, &res.a, &res.b, &res.tx, &res.ty))
      return;
    res.valid = true;
    res.scale = std::sqrt(res.a * res.a + res.b * res.b);
    res.rotationCwNorthDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(res.b, res.a));
  }
  float sumSqRes = 0.f;
  for (const auto& cp : st.alignControlPts) {
    const float predX = res.a * cp.srcX - res.b * cp.srcY + res.tx;
    const float predY = res.b * cp.srcX + res.a * cp.srcY + res.ty;
    const float rx = predX - cp.dstX;
    const float ry = predY - cp.dstY;
    const float r  = std::sqrt(rx * rx + ry * ry);
    res.pairResiduals.push_back(r);
    sumSqRes += r * r;
  }
  res.rms = std::sqrt(sumSqRes / static_cast<float>(n));
}

void ApplyAlignCommand(AppCommandState& st, std::vector<std::string>& log, bool applyScale) {
  const auto& res = st.alignLastResult;
  if (!res.valid) {
    log.push_back("ALIGN — no valid solution to apply.");
    return;
  }
  PushUndoSnapshot(st, "Align");

  // Compute actual transform parameters — optionally strip scale.
  float a = res.a, b = res.b, tx = res.tx, ty = res.ty;
  if (!applyScale) {
    const float theta = std::atan2(res.b, res.a);
    a = std::cos(theta);
    b = std::sin(theta);
    // Re-derive translation so centroid of sources maps to centroid of destinations.
    const int n = static_cast<int>(st.alignControlPts.size());
    float csx = 0.f, csy = 0.f, cdx = 0.f, cdy = 0.f;
    for (const auto& cp : st.alignControlPts) { csx += cp.srcX; csy += cp.srcY; cdx += cp.dstX; cdy += cp.dstY; }
    const float inv = 1.f / static_cast<float>(n);
    csx *= inv; csy *= inv; cdx *= inv; cdy *= inv;
    tx = cdx - (a * csx - b * csy);
    ty = cdy - (b * csx + a * csy);
  }
  const float appliedRotDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(b, a));

  // Identify source and destination survey points BEFORE the transform.
  constexpr float kMatchTol = 0.01f;
  const int nPairs = static_cast<int>(st.alignControlPts.size());
  std::vector<int> srcIdx(static_cast<size_t>(nPairs), -1);
  std::vector<int> dstIdx(static_cast<size_t>(nPairs), -1);
  for (int i = 0; i < nPairs; ++i) {
    const auto& cp = st.alignControlPts[static_cast<size_t>(i)];
    float bestSrc = kMatchTol * kMatchTol, bestDst = kMatchTol * kMatchTol;
    for (int j = 0; j < static_cast<int>(st.surveyPoints.size()); ++j) {
      const float ex = st.surveyPoints[static_cast<size_t>(j)].easting;
      const float ny = st.surveyPoints[static_cast<size_t>(j)].northing;
      const float dSrc = (ex - cp.srcX) * (ex - cp.srcX) + (ny - cp.srcY) * (ny - cp.srcY);
      const float dDst = (ex - cp.dstX) * (ex - cp.dstX) + (ny - cp.dstY) * (ny - cp.dstY);
      if (dSrc < bestSrc) { bestSrc = dSrc; srcIdx[static_cast<size_t>(i)] = j; }
      if (dDst < bestDst) { bestDst = dDst; dstIdx[static_cast<size_t>(i)] = j; }
    }
  }

  // Apply Helmert — only to selected entities when a selection snapshot exists.
  if (st.alignHasSelection)
    ApplyHelmertToAllGeometry(st, a, b, tx, ty, &st.alignSelectionSnapshot, &st.alignSurveySnapshot);
  else
    ApplyHelmertToAllGeometry(st, a, b, tx, ty);

  // Post-transform: restore destination survey points and tag ADJ/CON.
  // Restrict scan to the survey snapshot (only those actually moved).
  std::set<int> taggedCon, taggedAdj;
  for (int i = 0; i < nPairs; ++i) {
    const int idx = dstIdx[static_cast<size_t>(i)];
    if (idx < 0 || taggedCon.count(idx)) continue;
    taggedCon.insert(idx);
    auto& p = st.surveyPoints[static_cast<size_t>(idx)];
    p.easting  = st.alignControlPts[static_cast<size_t>(i)].dstX;
    p.northing = st.alignControlPts[static_cast<size_t>(i)].dstY;
    if (p.description.find("CON") == std::string::npos)
      p.description += (p.description.empty() ? "CON" : " CON");
    EnsureSurveyPointLabelMtext(st, static_cast<size_t>(idx), nullptr);
  }
  // Source (ADJ): already at correct transformed position, just append ADJ.
  for (int i = 0; i < nPairs; ++i) {
    const int idx = srcIdx[static_cast<size_t>(i)];
    if (idx < 0 || taggedCon.count(idx) || taggedAdj.count(idx)) continue;
    taggedAdj.insert(idx);
    auto& p = st.surveyPoints[static_cast<size_t>(idx)];
    if (p.description.find("ADJ") == std::string::npos)
      p.description += (p.description.empty() ? "ADJ" : " ADJ");
    EnsureSurveyPointLabelMtext(st, static_cast<size_t>(idx), nullptr);
  }
  BumpCadGpuCache(st);

  // Build report.
  const int n = res.nPairs;
  char buf[256];
  std::string report;
  report += "HELMERT TRANSFORMATION REPORT\n";
  report += "==============================\n";
  std::snprintf(buf, sizeof(buf), "Control pairs:  %d\n", n);                                                        report += buf;
  std::snprintf(buf, sizeof(buf), "Scale (solved): %.8f%s\n", static_cast<double>(res.scale),
                applyScale ? "" : "  [not applied — rotation+translation only]");                                    report += buf;
  report += "Rotation:       " + CadFormatBearingCwNorthDegMinSec(appliedRotDeg) + "\n";
  std::snprintf(buf, sizeof(buf), "Translation X:  %.6f\n", static_cast<double>(tx));                               report += buf;
  std::snprintf(buf, sizeof(buf), "Translation Y:  %.6f\n", static_cast<double>(ty));                               report += buf;
  std::snprintf(buf, sizeof(buf), "Point error:    %.6f  (avg. distance each src maps from its dst)\n\n",
                static_cast<double>(res.rms));                                                                       report += buf;
  report += "  Pair   Src Easting      Src Northing     Dst Easting      Dst Northing     Point Error\n";
  report += "  ----   -----------      ------------     -----------      ------------     -----------\n";
  for (int i = 0; i < n && i < static_cast<int>(st.alignControlPts.size()); ++i) {
    const auto& cp    = st.alignControlPts[static_cast<size_t>(i)];
    const float resid = i < static_cast<int>(res.pairResiduals.size()) ? res.pairResiduals[static_cast<size_t>(i)] : 0.f;
    std::snprintf(buf, sizeof(buf), "  %4d   %14.4f   %14.4f   %14.4f   %14.4f   %.6f\n",
                  i + 1, static_cast<double>(cp.srcX), static_cast<double>(cp.srcY),
                         static_cast<double>(cp.dstX), static_cast<double>(cp.dstY),
                         static_cast<double>(resid));
    report += buf;
  }

  char tabName[64];
  std::snprintf(tabName, sizeof(tabName), "ALIGN (%d pair%s, %s)", n, n == 1 ? "" : "s",
                applyScale ? "full" : "rot+trans");
  st.surveyReportTabs.emplace_back(std::string(tabName), std::move(report));
  st.surveyReportSelectLatestPending = true;

  st.alignControlPts.clear();
  st.alignPhase             = AppCommandState::AlignPhase::PickSrc;
  st.showAlignResultsWindow = false;
  log.push_back(std::string("ALIGN applied (") + (applyScale ? "scale+rot+trans" : "rot+trans only") +
                ") — report in the Reports tab.");
}

void ExecuteAlignCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.alignControlPts.empty()) {
    log.push_back("ALIGN — need at least 1 control point pair.");
    return;
  }
  RecalcAlignResult(st);
  if (!st.alignLastResult.valid) {
    log.push_back("ALIGN — control points are degenerate (coincident or collinear); cannot solve.");
    return;
  }
  st.showAlignResultsWindow = true;
  st.active                 = AppCommandState::Kind::None;
  log.push_back("ALIGN — solution ready. Review pairs in the results window, then click Apply.");
}

const char* AlignCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Align)
    return "";
  using AP = AppCommandState::AlignPhase;
  if (st.alignPhase == AP::PickSelection)
    return kSelectObjectsPrompt;  // REQ-121
  if (st.alignPhase == AP::PickSrc)
    return "ALIGN: pick SOURCE survey point in drawing (snap to it) — Enter to solve";
  return "ALIGN: pick/type DESTINATION real-world X,Y for this source point";
}
