#include "CadCommandsInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Geometry/spline.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

template <class T>
void RotateAroundBase(double bx, double by, double rad, T* x, T* y) {
  const double c = std::cos(rad);
  const double s = std::sin(rad);
  const double dx = static_cast<double>(*x) - bx;
  const double dy = static_cast<double>(*y) - by;
  *x = static_cast<T>(bx + c * dx - s * dy);
  *y = static_cast<T>(by + s * dx + c * dy);
}

/// REQ-103 MIRROR. Reflects (*x,*y) across the line through (x0,y0)-(x1,y1). A degenerate
/// (near-zero-length) mirror line leaves the point unchanged rather than dividing by ~0 — callers
/// require two distinct points before a mirror commits (see \c HandleMirrorText), so this is a
/// safety net, not a user-facing path.
template <class T>
void ReflectPtAcrossLine(double x0, double y0, double x1, double y1, T* x, T* y) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-12)
    return;
  const double t = ((static_cast<double>(*x) - x0) * dx + (static_cast<double>(*y) - y0) * dy) / len2;
  const double projX = x0 + t * dx;
  const double projY = y0 + t * dy;
  *x = static_cast<T>(2.0 * projX - static_cast<double>(*x));
  *y = static_cast<T>(2.0 * projY - static_cast<double>(*y));
}

/// Reflects a direction/angle across the mirror line's own direction angle phi = atan2(dy,dx):
/// angle' = 2*phi - angle. Used to re-derive an arc's swept range after reflection (see
/// \c DuplicateCadSelectionReflected) — reflecting a center point alone does not tell you which
/// way the arc now sweeps, because a reflection reverses handedness (CCW becomes CW).


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
    ann->rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
}

/// REQ-103 MIRROR. Same shape as \c RotateCadDimLinearAroundBase, reflecting instead of rotating.
/// \c ann->rotationRad here is the dimension LINE's geometric angle (text runs parallel to what was
/// measured), not a glyph-flip concern — it is re-derived from the reflected geometry the same way
/// ROTATE already does, independent of the MIRROR text-stays-upright decision for plain Text/Mtext.
static void ReflectCadDimLinearAcrossLine(float x0, float y0, float x1, float y1, CadAnnotation* ann) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  const float ex1 = ann->dimExt1X, ey1 = ann->dimExt1Y, ex2 = ann->dimExt2X, ey2 = ann->dimExt2Y;
  const float cmx = 0.5f * (ex1 + ex2);
  const float cmy = 0.5f * (ey1 + ey2);
  float dmx = cmx;
  float dmy = cmy;
  if (!ann->dimLinearVertical)
    dmy = cmy + ann->dimSignedOffset;
  else
    dmx = cmx + ann->dimSignedOffset;
  ReflectPtAcrossLine(x0, y0, x1, y1, &ann->dimExt1X, &ann->dimExt1Y);
  ReflectPtAcrossLine(x0, y0, x1, y1, &ann->dimExt2X, &ann->dimExt2Y);
  ReflectPtAcrossLine(x0, y0, x1, y1, &dmx, &dmy);
  const float ncmx = 0.5f * (ann->dimExt1X + ann->dimExt2X);
  const float ncmy = 0.5f * (ann->dimExt1Y + ann->dimExt2Y);
  if (!ann->dimLinearVertical)
    ann->dimSignedOffset = dmy - ncmy;
  else
    ann->dimSignedOffset = dmx - ncmx;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (CadDimLinearGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    ann->rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
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
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size()) {
      st.drawing.surveyPoints[static_cast<size_t>(i)].easting += dx;
      st.drawing.surveyPoints[static_cast<size_t>(i)].northing += dy;
    }
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

static void ApplyRotationToSelectedSurveyPoints(AppCommandState& st, float bx, float by, float rad) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.drawing.surveyPoints.size())
      continue;
    double x = st.drawing.surveyPoints[static_cast<size_t>(i)].easting;
    double y = st.drawing.surveyPoints[static_cast<size_t>(i)].northing;
    RotateAroundBase(bx, by, rad, &x, &y);
    st.drawing.surveyPoints[static_cast<size_t>(i)].easting = x;
    st.drawing.surveyPoints[static_cast<size_t>(i)].northing = y;
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

/// REQ-443: every transform command reduces to one point map on a spline, because a B-spline is an
/// affine combination of its control points - moving, turning, scaling or mirroring those points is
/// exactly moving, turning, scaling or mirroring the curve (`spline::MapPoints`). Fit points travel
/// with them, so a transformed fit spline can still be re-derived by a later grip edit.
///
/// In place. The caller owns the undo snapshot and the GPU-cache bump, as every transform does.
template <class F>
static void MapSelectedSplines(AppCommandState& st, const F& f) {
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Spline || e.index < 0)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k < st.drawing.cadSplines.size())
      spline::MapPoints(&st.drawing.cadSplines[k], f);
  }
}

/// REQ-443: remove every selected spline and its attributes, highest index first. Both erase paths
/// (ERASE and MIRROR's erase-source) call it, so a spline cannot be erased by one and left behind by
/// the other.
static void EraseSelectedSplines(AppCommandState& st) {
  std::set<int> ix;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::Spline && e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.cadSplines.size())
      ix.insert(e.index);
  for (auto it = ix.rbegin(); it != ix.rend(); ++it) {
    const size_t k = static_cast<size_t>(*it);
    st.drawing.cadSplines.erase(st.drawing.cadSplines.begin() + static_cast<std::ptrdiff_t>(k));
    if (k < st.drawing.cadSplineAttrs.size())
      st.drawing.cadSplineAttrs.erase(st.drawing.cadSplineAttrs.begin() + static_cast<std::ptrdiff_t>(k));
  }
}

/// REQ-443: append a copy of every selected spline with its points mapped through f, the source
/// untouched - the duplicate path COPY, ROTATE-copy, MIRROR and ARRAY share. The copy takes the
/// source's layer and colour but not its id (see DuplicatedEntityAttrs).
template <class F>
static void DuplicateSelectedSplines(AppCommandState& st, const F& f) {
  const size_t n0 = st.drawing.cadSplines.size();
  std::vector<size_t> sources;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::Spline && e.index >= 0 && static_cast<size_t>(e.index) < n0)
      sources.push_back(static_cast<size_t>(e.index));
  if (sources.empty())
    return;
  while (st.drawing.cadSplineAttrs.size() < n0)
    st.drawing.cadSplineAttrs.push_back(MakeNewEntityAttrs(st));
  for (const size_t k : sources) {
    CadSpline c = st.drawing.cadSplines[k];
    spline::MapPoints(&c, f);
    st.drawing.cadSplines.push_back(std::move(c));
    st.drawing.cadSplineAttrs.push_back(DuplicatedEntityAttrs(st.drawing.cadSplineAttrs[k]));
  }
  BumpCadGpuCache(st);
}

template <class F>
static void MapSelectedCadPoints(AppCommandState& st, const F& f) {
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Point || e.index < 0)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.cadPoints.size())
      continue;
    const spline::Vec3 mapped = f(spline::Vec3{st.drawing.cadPoints[k].x, st.drawing.cadPoints[k].y, st.drawing.cadPoints[k].z});
    st.drawing.cadPoints[k].x = mapped.x;
    st.drawing.cadPoints[k].y = mapped.y;
    st.drawing.cadPoints[k].z = mapped.z;
  }
}

static void EraseSelectedCadPoints(AppCommandState& st) {
  std::set<int> ix;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::Point && e.index >= 0 &&
        static_cast<size_t>(e.index) < st.drawing.cadPoints.size())
      ix.insert(e.index);
  for (auto it = ix.rbegin(); it != ix.rend(); ++it) {
    const size_t k = static_cast<size_t>(*it);
    st.drawing.cadPoints.erase(st.drawing.cadPoints.begin() + static_cast<std::ptrdiff_t>(k));
    if (k < st.drawing.cadPointAttrs.size())
      st.drawing.cadPointAttrs.erase(st.drawing.cadPointAttrs.begin() + static_cast<std::ptrdiff_t>(k));
  }
}

template <class F>
static void DuplicateSelectedCadPoints(AppCommandState& st, const F& f) {
  const size_t n0 = st.drawing.cadPoints.size();
  std::vector<size_t> sources;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::Point && e.index >= 0 && static_cast<size_t>(e.index) < n0)
      sources.push_back(static_cast<size_t>(e.index));
  if (sources.empty())
    return;
  while (st.drawing.cadPointAttrs.size() < n0)
    st.drawing.cadPointAttrs.push_back(MakeNewEntityAttrs(st));
  for (const size_t k : sources) {
    CadPoint c = st.drawing.cadPoints[k];
    const spline::Vec3 mapped = f(spline::Vec3{c.x, c.y, c.z});
    c.x = mapped.x;
    c.y = mapped.y;
    c.z = mapped.z;
    st.drawing.cadPoints.push_back(c);
    st.drawing.cadPointAttrs.push_back(DuplicatedEntityAttrs(st.drawing.cadPointAttrs[k]));
  }
}

/// Visit each selected feature line once as (index, firstVertex, lastVertexExclusive).
///
/// The ranges are COLLECTED FIRST and visited afterwards, which is not incidental: two of the five
/// callers append to `featureLineOffsets` while they work, and walking the offsets table as it grows
/// would read from a reallocated buffer. Selections are small, so the copy costs nothing.
///
/// Five callers — translate, rotate, scale, and the two duplicate paths — plus the centroid and
/// extent helpers. That is the point of it: ADR-035 (g) names a missed case as this entity's whole
/// risk, and seven hand-copied CSR walks would be seven chances to miss one. REQ-087.
template <class Fn>
static void ForEachSelectedFeatureLine(const AppCommandState& st, Fn&& fn) {
  std::vector<std::array<int, 3>> ranges;
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::FeatureLine)
      continue;
    const int fi = e.index;
    if (fi < 0 || (static_cast<size_t>(fi)+ 1) >= st.drawing.featureLineOffsets.size())
      continue;
    const int v0 = st.drawing.featureLineOffsets[static_cast<size_t>(fi)];
    const int v1 = st.drawing.featureLineOffsets[(static_cast<size_t>(fi)+ 1)];
    if (v1 <= v0 || static_cast<size_t>(v1) * 3 > st.drawing.featureLineVerts.size())
      continue;
    ranges.push_back({fi, v0, v1});
  }
  for (const std::array<int, 3>& r : ranges)
    fn(r[0], r[1], r[2]);
}

/// Apply \p xform to the plan position of every vertex of every selected feature line. Elevation is
/// deliberately not offered: MOVE, ROTATE and SCALE are all plan operations here, matching the
/// polyline behaviour they sit beside, and a transform that could silently alter Z would break the
/// surface a feature line feeds (REQ-069) without touching its plan geometry.
template <class Xf>
static void TransformSelectedFeatureLinesInPlace(AppCommandState& st, Xf&& xform) {
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      xform(&st.drawing.featureLineVerts[b], &st.drawing.featureLineVerts[b + 1]);
    }
  });
}

/// Append a copy of feature line \p fi, with \p xform applied to each vertex's plan position.
///
/// Everything that makes the line what it is carries across — elevations, the elevation-point flags,
/// the closed flag, the name — and the id does not, because the copy is a new entity (REQ-076).
/// The vertex range is validated BEFORE the first push so this cannot bail out half way and leave
/// `featureLineVerts` longer than `featureLineOffsets` says it is; `EraseFeatureLineByIndex` already
/// showed that the flags and the vertices are cut on different strides and that a length mismatch
/// between them is completely silent.
template <class Xf>
static void AppendFeatureLineCopy(AppCommandState& st, int fi, int v0, int v1, Xf&& xform) {
  const int nv = v1 - v0;
  if (nv < 2 || static_cast<size_t>(v1) * 3 > st.drawing.featureLineVerts.size())
    return;
  if (st.drawing.featureLineOffsets.empty())
    st.drawing.featureLineOffsets.push_back(0);
  const int baseVert = st.drawing.featureLineOffsets.back();
  for (int vi = v0; vi < v1; ++vi) {
    const size_t b = static_cast<size_t>(vi) * 3;
    float x = static_cast<float>(st.drawing.featureLineVerts[b]);
    float y = static_cast<float>(st.drawing.featureLineVerts[b + 1]);
    const float z = static_cast<float>(st.drawing.featureLineVerts[b + 2]);
    xform(&x, &y);
    st.drawing.featureLineVerts.push_back(x);
    st.drawing.featureLineVerts.push_back(y);
    st.drawing.featureLineVerts.push_back(z);
    st.drawing.featureLineElevPt.push_back(static_cast<size_t>(vi) < st.drawing.featureLineElevPt.size()
                                       ? st.drawing.featureLineElevPt[static_cast<size_t>(vi)]
                                       : static_cast<uint8_t>(0));
  }
  st.drawing.featureLineOffsets.push_back(baseVert + nv);
  st.drawing.featureLineClosed.push_back(static_cast<size_t>(fi) < st.drawing.featureLineClosed.size()
                                     ? st.drawing.featureLineClosed[static_cast<size_t>(fi)]
                                     : static_cast<uint8_t>(0));
  st.drawing.featureLineInfo.push_back(static_cast<size_t>(fi) < st.drawing.featureLineInfo.size()
                                   ? st.drawing.featureLineInfo[static_cast<size_t>(fi)]
                                   : CadFeatureLineInfo{});
  st.drawing.featureLineAttrs.push_back(DuplicatedEntityAttrs(
      static_cast<size_t>(fi) < st.drawing.featureLineAttrs.size()
          ? st.drawing.featureLineAttrs[static_cast<size_t>(fi)]
          : MakeNewEntityAttrs(st)));
}

/// Apply \p op to every selected solid, replacing each rather than editing it, and report anything
/// the kernel refuses by name (REQ-201, REQ-332 / TASK-231).
///
/// The de-duplication is `TranslateSelectedSolids`' and is load-bearing for the same reason: a
/// selection should not hold one solid twice, and transforming it twice would turn or scale it twice
/// — a defect that only shows up on the drawing where it happened.
///
/// A refusal leaves that solid EXACTLY as it was and does not abandon the rest of the selection. The
/// kernel computes into a fresh solid and validates before returning (ADR-046 (d)), so there is no
/// half-transformed state to roll back — the old `shared_ptr` is simply not replaced.
///
/// \p duplicate appends the result as a NEW solid carrying the source's attributes (COPY, ROTATE
/// Copy, MIRROR, polar ARRAY — REQ-351) instead of replacing the source; a refusal then adds nothing.
template <typename Op>
static void TransformSelectedSolids(AppCommandState& st, const char* commandName, Op op,
                                    std::vector<std::string>& log, bool duplicate = false) {
  std::set<int> seen;
  size_t refused = 0;
  brep::Problem lastWhy = brep::Problem::Ok;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::Solid || e.index < 0 ||
        static_cast<size_t>(e.index) >= st.drawing.cadSolids.size())
      continue;
    if (!seen.insert(e.index).second)
      continue;
    const size_t ix = static_cast<size_t>(e.index);
    const CadSolidPtr sp = st.drawing.cadSolids[ix];  // a copy: a duplicate's push_back may reallocate
    if (!sp)
      continue;
    brep::Solid out;
    brep::Problem why = brep::Problem::Ok;
    if (!op(*sp, &out, &why)) {
      ++refused;
      lastWhy = why;
      continue;
    }
    if (duplicate) {
      st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(out)));
      st.drawing.cadSolidAttrs.push_back(
          DuplicatedEntityAttrs(ix < st.drawing.cadSolidAttrs.size() ? st.drawing.cadSolidAttrs[ix] : EntityAttributes{}));
    } else {
      st.drawing.cadSolids[ix] = std::make_shared<const brep::Solid>(std::move(out));
    }
  }
  // One line naming the LAST reason, not one line per solid. With several solids refused for
  // different reasons that is lossy, and it is accepted here because the case is close to
  // unreachable: the axis is normalized by both callers and the factor is clamped positive, so a
  // refusal means `RotateResultInvalid` / `ScaleResultInvalid` — an isometry or a positive scale
  // failing to validate, which cannot happen on a solid that was valid going in. This is a safety
  // net that says something true if it ever fires, not a routine path.
  if (refused != 0)
    log.push_back(std::string(commandName) + " — " + std::to_string(refused) +
                  " solid(s) unchanged: " + brep::ProblemText(lastWhy));
}

/// Turn every selected solid about the line through \p axisPoint with unit direction \p axisUnit
/// (REQ-332 / TASK-231). Replaces REQ-322 item 6's blanket refusal for ROTATE. \p duplicate adds the
/// turned solid as a copy instead (ROTATE Copy, polar ARRAY — REQ-351).
static void RotateSelectedSolids(AppCommandState& st, const ray3d::Vec3& axisPoint,
                                 const ray3d::Vec3& axisUnit, float angleRad,
                                 std::vector<std::string>& log, bool duplicate = false,
                                 const char* commandName = "ROTATE") {
  if (angleRad == 0.f && !duplicate)
    return;
  const double rad = static_cast<double>(angleRad);
  TransformSelectedSolids(
      st, commandName,
      [&](const brep::Solid& s, brep::Solid* out, brep::Problem* why) {
        return brep::Rotate(s, axisPoint, axisUnit, rad, out, why);
      },
      log, duplicate);
}

/// Add the mirror image of every selected solid across the plane through \p planePoint with unit
/// normal \p planeUnit (REQ-351). Always a duplicate: MIRROR copies first and erases the source
/// afterwards only if asked (`FinishMirrorCommand`).
static void MirrorSelectedSolids(AppCommandState& st, const ray3d::Vec3& planePoint,
                                 const ray3d::Vec3& planeUnit, std::vector<std::string>& log) {
  TransformSelectedSolids(
      st, "MIRROR",
      [&](const brep::Solid& s, brep::Solid* out, brep::Problem* why) {
        return brep::Mirror(s, planePoint, planeUnit, out, why);
      },
      log, /*duplicate=*/true);
}

/// Add a copy of every selected solid moved by (dx, dy, dz) — COPY and both ARRAY forms (REQ-351,
/// widening GitHub issue #400 increment 3's rectangular-ARRAY-only helper). `brep::Translate` cannot
/// refuse, so there is nothing to report.
static void DuplicateSelectedSolidsTranslated(AppCommandState& st, float dx, float dy, float dz) {
  std::vector<std::string> unused;
  TransformSelectedSolids(
      st, "COPY",
      [&](const brep::Solid& s, brep::Solid* out, brep::Problem*) {
        *out = brep::Translate(s, ray3d::Vec3{static_cast<double>(dx), static_cast<double>(dy),
                                              static_cast<double>(dz)});
        return true;
      },
      unused, /*duplicate=*/true);
}

/// Map every path vertex of every selected pipe run through \p pt, in place or — \p duplicate — as a
/// new run carrying the source's attributes (REQ-351, D-2026-09-28-f).
///
/// A pipe run owns only its path; the swept pipe is re-derived from it whenever the path changes
/// (`RebuildPipeRunWorldSolids` keys on a hash of the vertices), so moving the vertices IS moving
/// the pipe. Its size and wall are untouched by every transform — a scaled run is a longer run of the
/// same pipe, because a 4in pipe is a catalog part (D-2026-09-28-f). A copy joins no piping network:
/// a network is a named set the user built, and the copy is a new run.
template <typename PointFn>
static void TransformSelectedPipeRuns(AppCommandState& st, PointFn pt, bool duplicate) {
  std::set<int> seen;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::PipeRun || e.index < 0 ||
        static_cast<size_t>(e.index) >= st.drawing.cadPipeRuns.size())
      continue;
    if (!seen.insert(e.index).second)
      continue;
    const size_t ix = static_cast<size_t>(e.index);
    CadPipeRun run = st.drawing.cadPipeRuns[ix];
    for (size_t k = 0; k + 2 < run.vertsXyz.size(); k += 3) {
      const ray3d::Vec3 p = pt(ray3d::Vec3{run.vertsXyz[k], run.vertsXyz[k + 1], run.vertsXyz[k + 2]});
      run.vertsXyz[k] = p.x;
      run.vertsXyz[k + 1] = p.y;
      run.vertsXyz[k + 2] = p.z;
    }
    if (duplicate) {
      st.drawing.cadPipeRuns.push_back(std::move(run));
      st.drawing.cadPipeRunAttrs.push_back(DuplicatedEntityAttrs(
          ix < st.drawing.cadPipeRunAttrs.size() ? st.drawing.cadPipeRunAttrs[ix] : EntityAttributes{}));
    } else {
      st.drawing.cadPipeRuns[ix] = std::move(run);
    }
  }
}

/// REQ-487.1: map every selected alignment through \p fn (an Alignment transform that returns false to refuse), in place or,
/// with \p duplicate, as a new INDEPENDENT alignment with a unique name and a fresh id. A linked offset that is moved is
/// rebuilt from its parent afterwards, so it can never be left displaced from the geometry it is defined by.
template <typename AlignFn>
static void TransformSelectedAlignments(AppCommandState& st, AlignFn fn, bool duplicate) {
  std::set<int> seen;
  bool changed = false;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::Alignment || e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.alignments.size())
      continue;
    if (!seen.insert(e.index).second)
      continue;
    const size_t ix = static_cast<size_t>(e.index);
    alignment::Alignment a = st.drawing.alignments[ix];
    if (!fn(a))
      continue;
    if (duplicate) {
      a.id = st.nextEntityId++;
      a.parentId = 0;
      a.name = UniqueAlignmentName(st, a.name);
      a.equations = st.drawing.alignments[ix].equations;
      st.drawing.alignments.push_back(std::move(a));
    } else {
      st.drawing.alignments[ix] = std::move(a);
      changed = true;
    }
  }
  if (changed) {
    std::vector<std::string> quiet;
    RefreshAlignmentOffsets(st, quiet);
  }
}

static void TranslateSelectedAlignments(AppCommandState& st, double dx, double dy, bool duplicate) {
  TransformSelectedAlignments(st, [&](alignment::Alignment& a) { a.Translate(dx, dy); return true; }, duplicate);
}

static void TranslateSelectedPipeRuns(AppCommandState& st, float dx, float dy, float dz, bool duplicate) {
  const ray3d::Vec3 d{static_cast<double>(dx), static_cast<double>(dy), static_cast<double>(dz)};
  TransformSelectedPipeRuns(st, [&](const ray3d::Vec3& p) { return ray3d::Add(p, d); }, duplicate);
  if (dx != 0.f || dy != 0.f)
    TranslateSelectedAlignments(st, static_cast<double>(dx), static_cast<double>(dy), duplicate);  // REQ-487.1
}

static void RotateSelectedAlignments(AppCommandState& st, const ray3d::Vec3& axisPoint, const ray3d::Vec3& axisUnit,
                                     double angleRad, bool duplicate) {
  if (std::fabs(axisUnit.z) < 1.0 - 1e-9)
    return;  // an alignment is horizontal: only a vertical axis keeps it so
  const double cw = -angleRad * (axisUnit.z > 0.0 ? 1.0 : -1.0);  // ray3d rotates counter-clockwise; azimuths run clockwise
  TransformSelectedAlignments(st, [&](alignment::Alignment& a) { a.Rotate(axisPoint.x, axisPoint.y, cw); return true; }, duplicate);
}

static void RotateSelectedPipeRuns(AppCommandState& st, const ray3d::Vec3& axisPoint,
                                   const ray3d::Vec3& axisUnit, float angleRad, bool duplicate) {
  const double rad = static_cast<double>(angleRad);
  TransformSelectedPipeRuns(
      st, [&](const ray3d::Vec3& p) { return ray3d::RotatePointAboutAxis(p, axisPoint, axisUnit, rad); },
      duplicate);
  RotateSelectedAlignments(st, axisPoint, axisUnit, rad, duplicate);  // REQ-487.1
}

/// MIRROR's solids and pipe runs, across the plane through \p planePoint with unit normal
/// \p planeUnit (REQ-351): each gets a mirrored copy. A pipe is symmetric about its own path, so
/// reflecting the path is the whole of mirroring a run.
static void MirrorSelectedSolidsAndPipeRuns(AppCommandState& st, const ray3d::Vec3& planePoint,
                                            const ray3d::Vec3& planeUnit, std::vector<std::string>& log) {
  MirrorSelectedSolids(st, planePoint, planeUnit, log);  // refuses a degenerate plane by name
  if (std::fabs(ray3d::Length(planeUnit) - 1.0) > 1e-9)
    return;
  TransformSelectedPipeRuns(
      st, [&](const ray3d::Vec3& p) { return ray3d::ReflectPointAcrossPlane(p, planePoint, planeUnit); },
      /*duplicate=*/true);
  if (std::fabs(planeUnit.z) < 1e-9) {  // REQ-487.1: a vertical mirror plane keeps an alignment horizontal
    const double px = planePoint.x, py = planePoint.y;
    TransformSelectedAlignments(
        st, [&](alignment::Alignment& a) { return a.Mirror(px, py, px - planeUnit.y, py + planeUnit.x); }, /*duplicate=*/true);
  }
}

// dz defaults to 0 so every existing 2D caller (COPY, ARRAY rectangular under the World UCS) is
// byte-identical to before this parameter existed. GitHub issue #400 increment 1 is ARRAY's own
// UCS-plane rectangular case, the only caller that ever passes a non-zero dz.
static void DuplicateCadSelectionTranslated(AppCommandState& st, float dx, float dy, float dz = 0.f) {
  // Solids and pipe runs are copied too (REQ-351): COPY, both ARRAY forms and every other caller
  // used to leave them behind without a word.
  DuplicateSelectedSolidsTranslated(st, dx, dy, dz);
  TranslateSelectedPipeRuns(st, dx, dy, dz, /*duplicate=*/true);
  const size_t polyVertsBefore = st.drawing.userPolylineVerts.size();
  const size_t featureVertsBefore = st.drawing.featureLineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<float> newCircleNormals;   // REQ-312, one 3-float normal per new circle
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadTable> newTables;
  std::vector<EntityAttributes> newTableAttrs;
  std::vector<CadPositionMarker> newMarkers;  // REQ-359
  std::vector<EntityAttributes> newMarkerAttrs;
  std::vector<CadMultileader> newMultileaders;  // REQ-367
  std::vector<EntityAttributes> newMultileaderAttrs;
  std::vector<CadBlockRef> newBlockRefs;
  std::vector<EntityAttributes> newBlockRefAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;
  std::vector<CadFilledRegion> newFills;
  std::vector<EntityAttributes> newFillAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::FilledRegion) {
      const size_t fk = static_cast<size_t>(e.index);
      if (fk < st.drawing.cadFilledRegions.size()) {
        CadFilledRegion fr = st.drawing.cadFilledRegions[fk];
        hatchgeom::Translate(fr, dx, dy, dz);
        newFills.push_back(std::move(fr));
        newFillAttrs.push_back(DuplicatedEntityAttrs(
            fk < st.drawing.cadFilledRegionAttrs.size() ? st.drawing.cadFilledRegionAttrs[fk] : EntityAttributes{}));
      }
    } else if (e.type == SelectedEntity::Type::LineSeg) {
      size_t li = static_cast<size_t>(e.index);
      if (li < st.drawing.lineCount()) {
        const CadLineRecord ln = st.drawing.line(li);
        for (const double v : {ln.x0, ln.y0, ln.z0, ln.x1, ln.y1, ln.z1})
          newLines.push_back(static_cast<float>(v));
        newLines[newLines.size() - 6] += dx;
        newLines[newLines.size() - 5] += dy;
        newLines[newLines.size() - 4] += dz;
        newLines[newLines.size() - 3] += dx;
        newLines[newLines.size() - 2] += dy;
        newLines[newLines.size() - 1] += dz;
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size())
          a = st.drawing.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount()) {
        const CadCircleRecord c = st.drawing.circle(ci);
        newCircles.push_back(static_cast<float>(c.cx + dx));
        newCircles.push_back(static_cast<float>(c.cy + dy));
        newCircles.push_back(static_cast<float>(c.cz + dz));  // z (issue #400: UCS-plane arrays)
        newCircles.push_back(static_cast<float>(c.r));  // r
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size())
          a = st.drawing.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
        float nnx = 0.f, nny = 0.f, nnz = 1.f;
        CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &nnx, &nny, &nnz);
        PushCircleNormal(newCircleNormals, nnx, nny, nnz);
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadAnnotations.size()) {
        CadAnnotation c = st.drawing.cadAnnotations[k];
        c.surveyPointLabelForId = -1;
        c.insX += dx;
        c.insY += dy;
        c.insZ += dz;  // issue #400: UCS-plane arrays, matching REQ-322 MOVE's own insZ handling
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
        if (k < st.drawing.cadAnnotationAttrs.size())
          a = st.drawing.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Table) {
      const size_t tk = static_cast<size_t>(e.index);
      if (tk < st.drawing.cadTables.size()) {
        CadTable c = st.drawing.cadTables[tk];
        CadTableTranslate(&c, dx, dy);
        newTables.push_back(std::move(c));
        EntityAttributes a{};
        if (tk < st.drawing.cadTableAttrs.size())
          a = st.drawing.cadTableAttrs[tk];
        newTableAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::PositionMarker) {  // REQ-359: marker + label
      const size_t mk = static_cast<size_t>(e.index);
      if (mk < st.drawing.cadPositionMarkers.size()) {
        CadPositionMarker c = st.drawing.cadPositionMarkers[mk];
        CadPositionMarkerTranslate(&c, dx, dy, dz);
        newMarkers.push_back(std::move(c));
        EntityAttributes a{};
        if (mk < st.drawing.cadPositionMarkerAttrs.size())
          a = st.drawing.cadPositionMarkerAttrs[mk];
        newMarkerAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Multileader) {  // REQ-367: path + label
      const size_t lk = static_cast<size_t>(e.index);
      if (lk < st.drawing.cadMultileaders.size()) {
        CadMultileader c = st.drawing.cadMultileaders[lk];
        CadMultileaderTranslate(&c, dx, dy, dz);
        newMultileaders.push_back(std::move(c));
        EntityAttributes a{};
        if (lk < st.drawing.cadMultileaderAttrs.size())
          a = st.drawing.cadMultileaderAttrs[lk];
        newMultileaderAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      const size_t bk = static_cast<size_t>(e.index);
      if (bk < st.drawing.cadBlockRefs.size()) {
        CadBlockRef c = st.drawing.cadBlockRefs[bk];
        CadBlockTranslate(&c, dx, dy, dz);  // issue #400: UCS-plane arrays
        newBlockRefs.push_back(std::move(c));
        EntityAttributes a{};
        if (bk < st.drawing.cadBlockRefAttrs.size())
          a = st.drawing.cadBlockRefAttrs[bk];
        newBlockRefAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size()) {
        CadArc a = st.drawing.userArcs[k];
        a.cx += dx;
        a.cy += dy;
        a.z += dz;  // issue #400: UCS-plane arrays
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.drawing.userArcAttrs.size())
          at = st.drawing.userArcAttrs[k];
        newArcAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size()) {
        CadEllipse el = st.drawing.userEllipses[k];
        el.cx += dx;
        el.cy += dy;
        el.z += dz;  // issue #400: UCS-plane arrays
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.drawing.userEllAttrs.size())
          at = st.drawing.userEllAttrs[k];
        newEllAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.drawing.userPolylineOffsets.empty())
        st.drawing.userPolylineOffsets.push_back(0);
      const int baseVert = st.drawing.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        st.drawing.pushVertex(CadPolyVertex{st.drawing.vertex(vi).x + dx, st.drawing.vertex(vi).y + dy, st.drawing.vertex(vi).z + dz});  // issue #400
      }
      st.drawing.userPolylineOffsets.push_back(baseVert + nv);
      // REQ-316 / ADR-047: Inc 1 flattens a copied arc polyline to straight (bulges default 0);
      // arc-aware MOVE/COPY/ROTATE/MIRROR is Inc 3.
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
        cl = st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      st.drawing.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
        at = st.drawing.userPolylineAttrs[static_cast<size_t>(pi)];
      st.drawing.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
    }
  }
  st.drawing.userLinesFlat.insert(st.drawing.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.drawing.userCirclesCxCyZR.insert(st.drawing.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.drawing.userLineAttrs.insert(st.drawing.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.drawing.userCircleAttrs.insert(st.drawing.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.drawing.userCircleNormals.insert(st.drawing.userCircleNormals.end(), newCircleNormals.begin(),
                              newCircleNormals.end());
  st.drawing.cadAnnotations.insert(st.drawing.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.drawing.cadAnnotationAttrs.insert(st.drawing.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.drawing.cadTables.insert(st.drawing.cadTables.end(), newTables.begin(), newTables.end());
  st.drawing.cadTableAttrs.insert(st.drawing.cadTableAttrs.end(), newTableAttrs.begin(), newTableAttrs.end());
  st.drawing.cadPositionMarkers.insert(st.drawing.cadPositionMarkers.end(), newMarkers.begin(), newMarkers.end());
  st.drawing.cadPositionMarkerAttrs.insert(st.drawing.cadPositionMarkerAttrs.end(), newMarkerAttrs.begin(), newMarkerAttrs.end());
  st.drawing.cadMultileaders.insert(st.drawing.cadMultileaders.end(), newMultileaders.begin(), newMultileaders.end());
  st.drawing.cadMultileaderAttrs.insert(st.drawing.cadMultileaderAttrs.end(), newMultileaderAttrs.begin(),
                                newMultileaderAttrs.end());
  st.drawing.cadBlockRefs.insert(st.drawing.cadBlockRefs.end(), newBlockRefs.begin(), newBlockRefs.end());
  st.drawing.cadBlockRefAttrs.insert(st.drawing.cadBlockRefAttrs.end(), newBlockRefAttrs.begin(), newBlockRefAttrs.end());
  st.drawing.userArcs.insert(st.drawing.userArcs.end(), newArcs.begin(), newArcs.end());
  st.drawing.userArcAttrs.insert(st.drawing.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.drawing.userEllipses.insert(st.drawing.userEllipses.end(), newEll.begin(), newEll.end());
  st.drawing.userEllAttrs.insert(st.drawing.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());
  st.drawing.cadFilledRegions.insert(st.drawing.cadFilledRegions.end(), newFills.begin(), newFills.end());
  st.drawing.cadFilledRegionAttrs.insert(st.drawing.cadFilledRegionAttrs.end(), newFillAttrs.begin(), newFillAttrs.end());

  DuplicateSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    return spline::Vec3{v.x + dx, v.y + dy, v.z + dz};
  });
  DuplicateSelectedCadPoints(st, [&](const spline::Vec3& v) {
    return spline::Vec3{v.x + dx, v.y + dy, v.z + dz};
  });

  // Feature lines (REQ-087). Appended after the loop above rather than inside it only for
  // readability — ForEachSelectedFeatureLine snapshots its ranges, so growing the store mid-walk is
  // safe either way.
  ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
    AppendFeatureLineCopy(st, fi, v0, v1, [&](float* x, float* y) {
      *x += dx;
      *y += dy;
    });
  });

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newTables.empty() || !newBlockRefs.empty() || !newArcs.empty() || !newEll.empty() ||
      !newFills.empty() || !newMarkers.empty() || !newMultileaders.empty() ||
      st.drawing.userPolylineVerts.size() != polyVertsBefore ||
      st.drawing.featureLineVerts.size() != featureVertsBefore)
    BumpCadGpuCache(st);
}

// Paste clipboard geometry into MODEL space with (dx, dy) applied. Builds st.selection from the pasted

static void DuplicateCadSelectionRotated(AppCommandState& st, float bx, float by, float rad,
                                         std::vector<std::string>& log) {
  // Solids and pipe runs are copied too (REQ-351), turned about the same vertical axis.
  const ray3d::Vec3 axisPoint{static_cast<double>(bx), static_cast<double>(by), 0.0};
  RotateSelectedSolids(st, axisPoint, {0.0, 0.0, 1.0}, rad, log, /*duplicate=*/true);
  RotateSelectedPipeRuns(st, axisPoint, {0.0, 0.0, 1.0}, rad, /*duplicate=*/true);
  const size_t polyVertsBefore = st.drawing.userPolylineVerts.size();
  const size_t featureVertsBefore = st.drawing.featureLineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<float> newCircleNormals;   // REQ-312, one 3-float normal per new circle
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadTable> newTables;
  std::vector<EntityAttributes> newTableAttrs;
  std::vector<CadBlockRef> newBlockRefs;
  std::vector<EntityAttributes> newBlockRefAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t li = static_cast<size_t>(e.index);
      if (li < st.drawing.lineCount()) {
        const CadLineRecord ln = st.drawing.line(li);
        float x0 = static_cast<float>(ln.x0);
        float y0 = static_cast<float>(ln.y0);
        float z0 = static_cast<float>(ln.z0);
        float x1 = static_cast<float>(ln.x1);
        float y1 = static_cast<float>(ln.y1);
        float z1 = static_cast<float>(ln.z1);
        RotateAroundBase(bx, by, rad, &x0, &y0);
        RotateAroundBase(bx, by, rad, &x1, &y1);
        newLines.push_back(x0);
        newLines.push_back(y0);
        newLines.push_back(z0);
        newLines.push_back(x1);
        newLines.push_back(y1);
        newLines.push_back(z1);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size())
          a = st.drawing.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount()) {
        const CadCircleRecord c = st.drawing.circle(ci);
        float cx = static_cast<float>(c.cx);
        float cy = static_cast<float>(c.cy);
        float r = static_cast<float>(c.r);
        RotateAroundBase(bx, by, rad, &cx, &cy);
        newCircles.push_back(cx);
        newCircles.push_back(cy);
        newCircles.push_back(static_cast<float>(c.cz));  // z — rotation is about the Z axis
        newCircles.push_back(r);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size())
          a = st.drawing.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
        float nnx = 0.f, nny = 0.f, nnz = 1.f;
        CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &nnx, &nny, &nnz);
        RotateNormalAboutZ(rad, &nnx, &nny);   // REQ-312: the plane turns with the circle
        PushCircleNormal(newCircleNormals, nnx, nny, nnz);
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadAnnotations.size()) {
        CadAnnotation c = st.drawing.cadAnnotations[k];
        c.surveyPointLabelForId = -1;
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
            c.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
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
        if (k < st.drawing.cadAnnotationAttrs.size())
          a = st.drawing.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Table) {
      const size_t tk = static_cast<size_t>(e.index);
      if (tk < st.drawing.cadTables.size()) {
        CadTable c = st.drawing.cadTables[tk];
        CadTableRotateAround(&c, bx, by, rad);
        newTables.push_back(std::move(c));
        EntityAttributes a{};
        if (tk < st.drawing.cadTableAttrs.size())
          a = st.drawing.cadTableAttrs[tk];
        newTableAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      const size_t bk = static_cast<size_t>(e.index);
      if (bk < st.drawing.cadBlockRefs.size()) {
        CadBlockRef c = st.drawing.cadBlockRefs[bk];
        CadBlockRotateZ(&c, bx, by, rad);
        newBlockRefs.push_back(std::move(c));
        EntityAttributes a{};
        if (bk < st.drawing.cadBlockRefAttrs.size())
          a = st.drawing.cadBlockRefAttrs[bk];
        newBlockRefAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size()) {
        CadArc a = st.drawing.userArcs[k];
        // The start point, through the arc's own plane, before the arc moves (REQ-312).
        ray3d::Vec3 startPt = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
        float spx = static_cast<float>(startPt.x);
        float spy = static_cast<float>(startPt.y);
        RotateAroundBase(bx, by, rad, &spx, &spy);  // a rotation about Z leaves the point's Z alone
        startPt.x = static_cast<double>(spx);
        startPt.y = static_cast<double>(spy);
        RotateAroundBase(bx, by, rad, &a.cx, &a.cy);
        a.startRad += rad;
        RotateNormalAboutZ(rad, &a.nx, &a.ny);   // REQ-312: the arc plane turns with the arc
        CadReanchorArcStart(&a, startPt);  // REQ-312: a no-op on a flat arc, where += rad is exact
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.drawing.userArcAttrs.size())
          at = st.drawing.userArcAttrs[k];
        newArcAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size()) {
        CadEllipse el = st.drawing.userEllipses[k];
        float mx = static_cast<float>(el.cx + el.majVx);
        float my = static_cast<float>(el.cy + el.majVy);
        RotateAroundBase(bx, by, rad, &el.cx, &el.cy);
        RotateAroundBase(bx, by, rad, &mx, &my);
        el.majVx = static_cast<float>(mx - el.cx);
        el.majVy = static_cast<float>(my - el.cy);
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.drawing.userEllAttrs.size())
          at = st.drawing.userEllAttrs[k];
        newEllAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.drawing.userPolylineOffsets.empty())
        st.drawing.userPolylineOffsets.push_back(0);
      const int baseVert = st.drawing.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        float px = static_cast<float>(st.drawing.vertex(vi).x);
        float py = static_cast<float>(st.drawing.vertex(vi).y);
        float pz = static_cast<float>(st.drawing.vertex(vi).z);
        RotateAroundBase(bx, by, rad, &px, &py);
        st.drawing.pushVertex(CadPolyVertex{px, py, pz});
      }
      st.drawing.userPolylineOffsets.push_back(baseVert + nv);
      // REQ-316 / ADR-047: Inc 1 flattens a copied arc polyline to straight (bulges default 0);
      // arc-aware MOVE/COPY/ROTATE/MIRROR is Inc 3.
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
        cl = st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      st.drawing.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
        at = st.drawing.userPolylineAttrs[static_cast<size_t>(pi)];
      st.drawing.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
    }
  }
  st.drawing.userLinesFlat.insert(st.drawing.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.drawing.userCirclesCxCyZR.insert(st.drawing.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.drawing.userLineAttrs.insert(st.drawing.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.drawing.userCircleAttrs.insert(st.drawing.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.drawing.userCircleNormals.insert(st.drawing.userCircleNormals.end(), newCircleNormals.begin(),
                              newCircleNormals.end());
  st.drawing.cadAnnotations.insert(st.drawing.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.drawing.cadAnnotationAttrs.insert(st.drawing.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.drawing.cadTables.insert(st.drawing.cadTables.end(), newTables.begin(), newTables.end());
  st.drawing.cadTableAttrs.insert(st.drawing.cadTableAttrs.end(), newTableAttrs.begin(), newTableAttrs.end());
  st.drawing.cadBlockRefs.insert(st.drawing.cadBlockRefs.end(), newBlockRefs.begin(), newBlockRefs.end());
  st.drawing.cadBlockRefAttrs.insert(st.drawing.cadBlockRefAttrs.end(), newBlockRefAttrs.begin(), newBlockRefAttrs.end());
  st.drawing.userArcs.insert(st.drawing.userArcs.end(), newArcs.begin(), newArcs.end());
  st.drawing.userArcAttrs.insert(st.drawing.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.drawing.userEllipses.insert(st.drawing.userEllipses.end(), newEll.begin(), newEll.end());
  st.drawing.userEllAttrs.insert(st.drawing.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());

  DuplicateSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    double x = v.x, y = v.y;
    RotateAroundBase(bx, by, rad, &x, &y);
    return spline::Vec3{x, y, v.z};
  });
  DuplicateSelectedCadPoints(st, [&](const spline::Vec3& v) {
    double x = v.x, y = v.y;
    RotateAroundBase(bx, by, rad, &x, &y);
    return spline::Vec3{x, y, v.z};
  });

  // Feature lines (REQ-087) — the same append as the translated path, differing only in the point
  // function, which is exactly why both go through AppendFeatureLineCopy.
  ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
    AppendFeatureLineCopy(st, fi, v0, v1,
                          [&](auto* x, auto* y) { RotateAroundBase(bx, by, rad, x, y); });
  });

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      !newBlockRefs.empty() ||
      st.drawing.userPolylineVerts.size() != polyVertsBefore ||
      st.drawing.featureLineVerts.size() != featureVertsBefore)
    BumpCadGpuCache(st);
}

/// REQ-328: rotate every selected entity about the LINE through \p axisPoint with unit direction
/// \p axisUnit by \p angleRad, appending the rotated duplicates the same way
/// \c DuplicateCadSelectionRotated does (never mutating the source). This is the general primitive
/// that function's world-Z-only math is a special case of — proven by \c Ray3dTests.cpp's own
/// regression tests against `RotateAroundBase`.
///
/// Line/Circle/Arc/Ellipse/Polyline/FilledRegion/FeatureLine rotate fully in 3D: a bare point
/// (Line/Polyline/FilledRegion/FeatureLine vertex) has no orientation to preserve, so rotating it is
/// already completely well-defined; Circle/Arc/Ellipse additionally rotate their stored plane NORMAL
/// (a direction, via \c RotateVectorAboutAxis) so the plane itself tips with the rotation, and an
/// Arc re-anchors its start point exactly as the Z-only path already does.
///
/// Annotation/Table/BlockRef are refused when \p axisUnit is not parallel to world Z: none of the
/// three stores a plane normal, so tipping one out of world/UCS XY has no representable result with
/// today's data model (REQ-328 item 2) — the same class of stated boundary REQ-312 drew for why
/// arcs/circles needed a normal and annotations did not get one. When the axis IS Z-parallel they
/// rotate with the exact `RotateAroundBase`-equivalent math the 2D path always used.
static void RotateSelectionAboutAxis(AppCommandState& st, const ray3d::Vec3& axisPoint,
                                     const ray3d::Vec3& axisUnit, float angleRad,
                                     std::vector<std::string>& log,
                                     const char* commandLabel = "ARRAY Polar") {
  const bool axisIsWorldZParallel = std::fabs(axisUnit.x) < 1e-9 && std::fabs(axisUnit.y) < 1e-9 &&
                                    std::fabs(axisUnit.z) > 1e-9;
  const double rad = static_cast<double>(angleRad);
  const auto rotPt = [&](float x, float y, float z) -> ray3d::Vec3 {
    return ray3d::RotatePointAboutAxis({x, y, z}, axisPoint, axisUnit, rad);
  };
  const auto rotDir = [&](float x, float y, float z) -> ray3d::Vec3 {
    return ray3d::RotateVectorAboutAxis({x, y, z}, axisUnit, rad);
  };
  DuplicateSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    return ray3d::RotatePointAboutAxis(v, axisPoint, axisUnit, rad);
  });
  DuplicateSelectedCadPoints(st, [&](const spline::Vec3& v) {
    return ray3d::RotatePointAboutAxis(v, axisPoint, axisUnit, rad);
  });
  // Solids and pipe runs are copied too (REQ-351) — about any axis, since `brep::Rotate` turns
  // every frame a solid stores and a pipe run is only its path.
  RotateSelectedSolids(st, axisPoint, axisUnit, angleRad, log, /*duplicate=*/true, commandLabel);
  RotateSelectedPipeRuns(st, axisPoint, axisUnit, angleRad, /*duplicate=*/true);

  const size_t polyVertsBefore = st.drawing.userPolylineVerts.size();
  const size_t featureVertsBefore = st.drawing.featureLineVerts.size();
  std::vector<float> newLines, newCircles, newCircleNormals;
  std::vector<EntityAttributes> newLineAttrs, newCircleAttrs;
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadTable> newTables;
  std::vector<EntityAttributes> newTableAttrs;
  std::vector<CadBlockRef> newBlockRefs;
  std::vector<EntityAttributes> newBlockRefAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;
  std::vector<CadFilledRegion> newFills;
  std::vector<EntityAttributes> newFillAttrs;
  size_t excludedFlatOnly = 0;  // Annotation/Table/BlockRef refused under a tilted axis

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t li = static_cast<size_t>(e.index);
      if (li < st.drawing.lineCount()) {
        const CadLineRecord ln = st.drawing.line(li);
        const ray3d::Vec3 p0 = rotPt(static_cast<float>(ln.x0), static_cast<float>(ln.y0), static_cast<float>(ln.z0));
        const ray3d::Vec3 p1 = rotPt(static_cast<float>(ln.x1), static_cast<float>(ln.y1), static_cast<float>(ln.z1));
        newLines.push_back(static_cast<float>(p0.x));
        newLines.push_back(static_cast<float>(p0.y));
        newLines.push_back(static_cast<float>(p0.z));
        newLines.push_back(static_cast<float>(p1.x));
        newLines.push_back(static_cast<float>(p1.y));
        newLines.push_back(static_cast<float>(p1.z));
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size())
          a = st.drawing.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount()) {
        const CadCircleRecord cc = st.drawing.circle(ci);
        const ray3d::Vec3 c = rotPt(static_cast<float>(cc.cx), static_cast<float>(cc.cy), static_cast<float>(cc.cz));
        newCircles.push_back(static_cast<float>(c.x));
        newCircles.push_back(static_cast<float>(c.y));
        newCircles.push_back(static_cast<float>(c.z));
        newCircles.push_back(static_cast<float>(cc.r));  // r
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size())
          a = st.drawing.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
        float nnx = 0.f, nny = 0.f, nnz = 1.f;
        CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &nnx, &nny, &nnz);
        const ray3d::Vec3 n = rotDir(nnx, nny, nnz);
        PushCircleNormal(newCircleNormals, static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size()) {
        CadArc a = st.drawing.userArcs[k];
        const ray3d::Vec3 startWorld = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
        const ray3d::Vec3 startRot = rotPt(static_cast<float>(startWorld.x), static_cast<float>(startWorld.y),
                                           static_cast<float>(startWorld.z));
        const ray3d::Vec3 c = rotPt(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.z));
        const ray3d::Vec3 n = rotDir(a.nx, a.ny, a.nz);
        a.cx = static_cast<float>(c.x);
        a.cy = static_cast<float>(c.y);
        a.z = static_cast<float>(c.z);
        a.nx = static_cast<float>(n.x);
        a.ny = static_cast<float>(n.y);
        a.nz = static_cast<float>(n.z);
        CadReanchorArcStart(&a, startRot);
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.drawing.userArcAttrs.size())
          at = st.drawing.userArcAttrs[k];
        newArcAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      // Unlike Circle/Arc (REQ-312), CadEllipse has NO stored plane normal — it is "parallel to XY,
      // absolute" by construction (CadEntities.hpp). Tipping one out of that plane has no
      // representable result, the same reason Annotation/Table/BlockRef refuse a tilted axis.
      if (!axisIsWorldZParallel) { ++excludedFlatOnly; continue; }
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size()) {
        CadEllipse el = st.drawing.userEllipses[k];
        const ray3d::Vec3 c = rotPt(static_cast<float>(el.cx), static_cast<float>(el.cy), static_cast<float>(el.z));
        const ray3d::Vec3 maj = rotDir(el.majVx, el.majVy, 0.f);
        el.cx = static_cast<float>(c.x);
        el.cy = static_cast<float>(c.y);
        el.z = static_cast<float>(c.z);
        el.majVx = static_cast<float>(maj.x);
        el.majVy = static_cast<float>(maj.y);
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.drawing.userEllAttrs.size())
          at = st.drawing.userEllAttrs[k];
        newEllAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.drawing.userPolylineOffsets.empty())
        st.drawing.userPolylineOffsets.push_back(0);
      const int baseVert = st.drawing.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        const ray3d::Vec3 p = rotPt(static_cast<float>(st.drawing.vertex(vi).x),
                                    static_cast<float>(st.drawing.vertex(vi).y),
                                    static_cast<float>(st.drawing.vertex(vi).z));
        st.drawing.pushVertex(CadPolyVertex{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
      }
      st.drawing.userPolylineOffsets.push_back(baseVert + nv);
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
        cl = st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      st.drawing.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
        at = st.drawing.userPolylineAttrs[static_cast<size_t>(pi)];
      st.drawing.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
    } else if (e.type == SelectedEntity::Type::FilledRegion) {
      const size_t fk = static_cast<size_t>(e.index);
      if (fk < st.drawing.cadFilledRegions.size()) {
        CadFilledRegion fr = st.drawing.cadFilledRegions[fk];
        for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3) {
          const ray3d::Vec3 p = rotPt(static_cast<float>(fr.vertsXyz[v]), static_cast<float>(fr.vertsXyz[v + 1]), static_cast<float>(fr.vertsXyz[v + 2]));
          fr.vertsXyz[v] = static_cast<float>(p.x);
          fr.vertsXyz[v + 1] = static_cast<float>(p.y);
          fr.vertsXyz[v + 2] = static_cast<float>(p.z);
        }
        newFills.push_back(std::move(fr));
        newFillAttrs.push_back(DuplicatedEntityAttrs(
            fk < st.drawing.cadFilledRegionAttrs.size() ? st.drawing.cadFilledRegionAttrs[fk] : EntityAttributes{}));
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (!axisIsWorldZParallel) { ++excludedFlatOnly; continue; }
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadAnnotations.size()) {
        CadAnnotation c = st.drawing.cadAnnotations[k];
        c.surveyPointLabelForId = -1;
        const ray3d::Vec3 ins = rotPt(c.insX, c.insY, c.insZ);
        c.insX = static_cast<float>(ins.x);
        c.insY = static_cast<float>(ins.y);
        c.insZ = static_cast<float>(ins.z);
        if (c.kind == CadAnnotation::Kind::Text) {
          c.rotationRad += angleRad;
        } else if (c.kind == CadAnnotation::Kind::DimLinear) {
          RotateCadDimLinearAroundBase(static_cast<float>(axisPoint.x), static_cast<float>(axisPoint.y), angleRad, &c);
        } else if (c.kind == CadAnnotation::Kind::DimAligned) {
          RotateAroundBase(static_cast<float>(axisPoint.x), static_cast<float>(axisPoint.y), angleRad, &c.dimExt1X, &c.dimExt1Y);
          RotateAroundBase(static_cast<float>(axisPoint.x), static_cast<float>(axisPoint.y), angleRad, &c.dimExt2X, &c.dimExt2Y);
          float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
          if (CadDimAlignedGeometry(c, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
            c.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
        } else {
          float xs[4] = {c.boxMinX, c.boxMaxX, c.boxMaxX, c.boxMinX};
          float ys[4] = {c.boxMinY, c.boxMinY, c.boxMaxY, c.boxMaxY};
          float mnX = xs[0], mxX = xs[0], mnY = ys[0], mxY = ys[0];
          for (int i = 0; i < 4; ++i) {
            RotateAroundBase(static_cast<float>(axisPoint.x), static_cast<float>(axisPoint.y), angleRad, &xs[i], &ys[i]);
            mnX = std::min(mnX, xs[i]); mxX = std::max(mxX, xs[i]);
            mnY = std::min(mnY, ys[i]); mxY = std::max(mxY, ys[i]);
          }
          c.boxMinX = mnX; c.boxMaxX = mxX; c.boxMinY = mnY; c.boxMaxY = mxY;
          c.insX = mnX; c.insY = mnY;
        }
        newAnn.push_back(std::move(c));
        EntityAttributes a{};
        if (k < st.drawing.cadAnnotationAttrs.size())
          a = st.drawing.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Table) {
      if (!axisIsWorldZParallel) { ++excludedFlatOnly; continue; }
      const size_t tk = static_cast<size_t>(e.index);
      if (tk < st.drawing.cadTables.size()) {
        CadTable c = st.drawing.cadTables[tk];
        CadTableRotateAround(&c, static_cast<float>(axisPoint.x), static_cast<float>(axisPoint.y), angleRad);
        newTables.push_back(std::move(c));
        EntityAttributes a{};
        if (tk < st.drawing.cadTableAttrs.size())
          a = st.drawing.cadTableAttrs[tk];
        newTableAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      if (!axisIsWorldZParallel) { ++excludedFlatOnly; continue; }
      const size_t bk = static_cast<size_t>(e.index);
      if (bk < st.drawing.cadBlockRefs.size()) {
        CadBlockRef c = st.drawing.cadBlockRefs[bk];
        CadBlockRotateZ(&c, static_cast<float>(axisPoint.x), static_cast<float>(axisPoint.y), angleRad);
        newBlockRefs.push_back(std::move(c));
        EntityAttributes a{};
        if (bk < st.drawing.cadBlockRefAttrs.size())
          a = st.drawing.cadBlockRefAttrs[bk];
        newBlockRefAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    }
  }
  st.drawing.userLinesFlat.insert(st.drawing.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.drawing.userCirclesCxCyZR.insert(st.drawing.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.drawing.userLineAttrs.insert(st.drawing.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.drawing.userCircleAttrs.insert(st.drawing.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.drawing.userCircleNormals.insert(st.drawing.userCircleNormals.end(), newCircleNormals.begin(), newCircleNormals.end());
  st.drawing.cadAnnotations.insert(st.drawing.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.drawing.cadAnnotationAttrs.insert(st.drawing.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.drawing.cadTables.insert(st.drawing.cadTables.end(), newTables.begin(), newTables.end());
  st.drawing.cadTableAttrs.insert(st.drawing.cadTableAttrs.end(), newTableAttrs.begin(), newTableAttrs.end());
  st.drawing.cadBlockRefs.insert(st.drawing.cadBlockRefs.end(), newBlockRefs.begin(), newBlockRefs.end());
  st.drawing.cadBlockRefAttrs.insert(st.drawing.cadBlockRefAttrs.end(), newBlockRefAttrs.begin(), newBlockRefAttrs.end());
  st.drawing.userArcs.insert(st.drawing.userArcs.end(), newArcs.begin(), newArcs.end());
  st.drawing.userArcAttrs.insert(st.drawing.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.drawing.userEllipses.insert(st.drawing.userEllipses.end(), newEll.begin(), newEll.end());
  st.drawing.userEllAttrs.insert(st.drawing.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());
  st.drawing.cadFilledRegions.insert(st.drawing.cadFilledRegions.end(), newFills.begin(), newFills.end());
  st.drawing.cadFilledRegionAttrs.insert(st.drawing.cadFilledRegionAttrs.end(), newFillAttrs.begin(), newFillAttrs.end());

  // AppendFeatureLineCopy's xform only sees (x, y) — its own Z rides through untouched (see its
  // declaration) — which is exactly right for a Z-parallel axis (the general formula's x'/y' never
  // depend on z there, proven algebraically the same way Ray3dTests' off-origin-Z-axis case is) but
  // WRONG for a tilted one, where x'/y' genuinely depend on the point's z. Rather than widen that
  // shared helper's signature for one caller, a tilted axis refuses feature lines too.
  size_t featureLinesExcluded = 0;
  if (axisIsWorldZParallel) {
    ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
      AppendFeatureLineCopy(st, fi, v0, v1, [&](float* x, float* y) {
        const ray3d::Vec3 p = rotPt(*x, *y, 0.f);
        *x = static_cast<float>(p.x);
        *y = static_cast<float>(p.y);
      });
    });
  } else {
    ForEachSelectedFeatureLine(st, [&](int /*fi*/, int /*v0*/, int /*v1*/) { ++featureLinesExcluded; });
  }
  excludedFlatOnly += featureLinesExcluded;

  if (excludedFlatOnly > 0)
    log.push_back(std::string(commandLabel) + " — " + std::to_string(excludedFlatOnly) +
                  " entity(ies) excluded: text/table/block/feature-line/ellipse rotation about a"
                  " tilted axis is not supported yet (REQ-328) — none of these store a plane normal"
                  " to tip. Rotate about a UCS with an upright Z axis.");

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      !newBlockRefs.empty() || !newFills.empty() ||
      st.drawing.userPolylineVerts.size() != polyVertsBefore ||
      st.drawing.featureLineVerts.size() != featureVertsBefore)
    BumpCadGpuCache(st);
}

static void FinalizeCopyTranslation(AppCommandState& st, float dx, float dy, float dz,
                                    std::vector<std::string>& log) {
  st.pendingSurveyDupIsRotate = false;
  DuplicateCadSelectionTranslated(st, dx, dy, dz);
  // Stay in COPY — same selection + base, ready for another destination.
  st.modifyPhase = AppCommandState::ModifyPhase::NeedDestination;
  if (!st.selectedSurveyPointIndices.empty()) {
    st.pendingCopyDx = dx;
    st.pendingCopyDy = dy;
    st.pendingCopyDz = dz;
    st.copySurveyDupModalOpen = true;
    st.copySurveyDupModalOpenRequested = true;
    log.push_back("COPY — CAD geometry duplicated; choose survey ID policy.");
  } else {
    log.push_back("COPY — next destination (ESC to exit):");
  }
}



/// REQ-305 ARRAY. Unlike MIRROR, ARRAY's duplication path (\c DuplicateCadSelectionTranslated /
/// \c DuplicateCadSelectionRotated) already handles \c FilledRegion, so that type is NOT dropped
/// here. Drops \c Mesh / \c PdfUnderlay for the same reasons \c DropMirrorUnsupportedFromSelection
/// does (reference-only / no representable duplicate), calls the existing
/// \c DropSurfacesFromSelectionForTransform for \c Surface, and — the D-2026-08-25-m decision —
/// drops survey points: duplicating them needs an ID-conflict policy (the existing COPY/ROTATE-copy
/// modal), and that modal resolves a single offset/rotation, not N array instances at once, so
/// survey points are excluded rather than silently mis-duplicated or given a policy they were never
/// built for.
///
/// \c Solid and \c PipeRun are NOT dropped: both ARRAY forms copy them (D-2026-09-07-c for
/// Rectangular, REQ-351 for Polar).
static void DropArrayUnsupportedFromSelection(AppCommandState& st, std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "ARRAY", log);
  DropPositionMarkersFromSelection(st, "ARRAY", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "ARRAY", log);     // REQ-367
  size_t mesh = 0, pdf = 0;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      if (e.type == SelectedEntity::Type::Mesh) { ++mesh; return true; }
                                      if (e.type == SelectedEntity::Type::PdfUnderlay) { ++pdf; return true; }
                                      return false;
                                    }),
                     st.selection.end());
  if (mesh)
    log.push_back("ARRAY — " + std::to_string(mesh) + " imported model(s) excluded: imported meshes" +
                  " are reference-only geometry and are never edited.");
  if (pdf)
    log.push_back("ARRAY — " + std::to_string(pdf) + " PDF underlay(s) excluded: an underlay cannot" +
                  " be duplicated yet.");
  if (!st.selectedSurveyPointIndices.empty()) {
    log.push_back("ARRAY — " + std::to_string(st.selectedSurveyPointIndices.size()) +
                  " survey point(s) excluded: arraying survey points needs an ID-conflict policy" +
                  " for many instances at once, which is not supported yet. Use COPY for survey points.");
    st.selectedSurveyPointIndices.clear();
  }
}



static bool ComputeSelectionCentroidWorld(const AppCommandState& st, float* outCx, float* outCy) {
  if (!outCx || !outCy)
    return false;
  double accx = 0.0;
  double accy = 0.0;
  int n = 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.lineCount()) {
        accx += 0.5 * static_cast<double>(st.drawing.line(k).x0 + st.drawing.line(k).x1);
        accy += 0.5 * static_cast<double>(st.drawing.line(k).y0 + st.drawing.line(k).y1);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount()) {
        accx += st.drawing.circle(ci).cx;
        accy += st.drawing.circle(ci).cy;
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size()) {
        accx += static_cast<double>(st.drawing.userArcs[k].cx);
        accy += static_cast<double>(st.drawing.userArcs[k].cy);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size()) {
        accx += static_cast<double>(st.drawing.userEllipses[k].cx);
        accy += static_cast<double>(st.drawing.userEllipses[k].cy);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      double sx = 0.0, sy = 0.0;
      int nv = 0;
      for (int vi = v0; vi < v1; ++vi) {
        sx += static_cast<double>(st.drawing.vertex(vi).x);
        sy += static_cast<double>(st.drawing.vertex(vi).y);
        ++nv;
      }
      if (nv > 0) {
        accx += sx / static_cast<double>(nv);
        accy += sy / static_cast<double>(nv);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadAnnotations.size()) {
        const CadAnnotation& a = st.drawing.cadAnnotations[k];
        accx += static_cast<double>(a.insX);
        accy += static_cast<double>(a.insY);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Table) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadTables.size()) {
        const CadTable& t = st.drawing.cadTables[k];
        accx += static_cast<double>(t.insX);
        accy += static_cast<double>(t.insY);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::PdfUnderlay) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.pdfAttachments.size()) {
        const PdfAttachment& patt = st.drawing.pdfAttachments[k];
        constexpr float kPi = 3.14159265f;
        const float cosR = std::cos(patt.rotationDeg * kPi / 180.f);
        const float sinR = std::sin(patt.rotationDeg * kPi / 180.f);
        const float hw   = patt.pageWidthPts  * patt.scale * 0.5f;
        const float hh   = patt.pageHeightPts * patt.scale * 0.5f;
        accx += static_cast<double>(patt.insertX + cosR * hw - sinR * hh);
        accy += static_cast<double>(patt.insertY + sinR * hw + cosR * hh);
        ++n;
      }
    }
  }
  // Feature lines (REQ-087): one contribution per line, at its own vertex centroid — the same
  // weighting the polyline branch above uses, so a selection of both is not skewed toward whichever
  // happens to have more vertices.
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    double sx = 0.0, sy = 0.0;
    for (int vi = v0; vi < v1; ++vi) {
      sx += static_cast<double>(st.drawing.featureLineVerts[static_cast<size_t>(vi) * 3]);
      sy += static_cast<double>(st.drawing.featureLineVerts[static_cast<size_t>(vi) * 3 + 1]);
    }
    const double nv = static_cast<double>(v1 - v0);
    accx += sx / nv;
    accy += sy / nv;
    ++n;
  });
  for (int si : st.selectedSurveyPointIndices) {
    if (si >= 0 && static_cast<size_t>(si) < st.drawing.surveyPoints.size()) {
      accx += static_cast<double>(st.drawing.surveyPoints[static_cast<size_t>(si)].easting);
      accy += static_cast<double>(st.drawing.surveyPoints[static_cast<size_t>(si)].northing);
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
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.lineCount()) {
        const CadLineRecord ln = st.drawing.line(k);
        for (const auto [x, y] : {std::pair<double, double>{ln.x0, ln.y0}, std::pair<double, double>{ln.x1, ln.y1}}) {
          m = static_cast<float>(std::max<double>(m, std::hypot(static_cast<float>(x) - bx, static_cast<float>(y) - by)));
        }
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount()) {
        const CadCircleRecord c = st.drawing.circle(ci);
        const float cx = static_cast<float>(c.cx);
        const float cy = static_cast<float>(c.cy);
        const float r = static_cast<float>(c.r);
        m = static_cast<float>(std::max<double>(m, std::hypot(cx - bx, cy - by) + r));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size()) {
        const CadArc& a = st.drawing.userArcs[k];
        m = static_cast<float>(std::max<double>(m, std::hypot(a.cx - bx, a.cy - by) + a.r));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size()) {
        const CadEllipse& el = st.drawing.userEllipses[k];
        const float ma = std::hypot(el.majVx, el.majVy);
        const float mb = ma * el.ratio;
        m = static_cast<float>(std::max<double>(m, std::hypot(el.cx - bx, el.cy - by) + std::max(ma, mb)));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      for (int vi = v0; vi < v1; ++vi) {
        const float x = static_cast<float>(st.drawing.vertex(vi).x);
        const float y = static_cast<float>(st.drawing.vertex(vi).y);
        m = static_cast<float>(std::max<double>(m, std::hypot(x - bx, y - by)));
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadAnnotations.size())
        continue;
      const CadAnnotation& a = st.drawing.cadAnnotations[k];
      if (a.kind == CadAnnotation::Kind::Mtext) {
        float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
        float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
        for (int i = 0; i < 4; ++i)
          m = static_cast<float>(std::max<double>(m, std::hypot(xs[i] - bx, ys[i] - by)));
      } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
        m = static_cast<float>(std::max<double>(m, std::hypot(a.dimExt1X - bx, a.dimExt1Y - by)));
        m = static_cast<float>(std::max<double>(m, std::hypot(a.dimExt2X - bx, a.dimExt2Y - by)));
        m = static_cast<float>(std::max<double>(m, std::hypot(a.insX - bx, a.insY - by)));
      } else if (a.kind == CadAnnotation::Kind::DimAngular) {
        m = static_cast<float>(std::max<double>(m, std::hypot(a.dimAngVertexX - bx, a.dimAngVertexY - by)));
        m = static_cast<float>(std::max<double>(m, std::hypot(a.dimExt1X - bx, a.dimExt1Y - by)));
        m = static_cast<float>(std::max<double>(m, std::hypot(a.dimExt2X - bx, a.dimExt2Y - by)));
        m = static_cast<float>(std::max<double>(m, std::hypot(a.insX - bx, a.insY - by)));
      }       else
        m = static_cast<float>(std::max<double>(m, std::hypot(a.insX - bx, a.insY - by)));
    } else if (e.type == SelectedEntity::Type::Table) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadTables.size())
        continue;
      const CadTable& t = st.drawing.cadTables[k];
      for (int i = 0; i < 4; ++i) {
        float cx = 0.f, cy = 0.f;
        CadTableWorldCorner(t, i, &cx, &cy);
        m = static_cast<float>(std::max<double>(m, std::hypot(cx - bx, cy - by)));
      }
    } else if (e.type == SelectedEntity::Type::PdfUnderlay) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.pdfAttachments.size()) {
        const PdfAttachment& patt = st.drawing.pdfAttachments[k];
        if (patt.pageWidthPts > 0.f && patt.pageHeightPts > 0.f) {
          constexpr float kPi = 3.14159265f;
          const float cosR = std::cos(patt.rotationDeg * kPi / 180.f);
          const float sinR = std::sin(patt.rotationDeg * kPi / 180.f);
          const float W    = patt.pageWidthPts  * patt.scale;
          const float H    = patt.pageHeightPts * patt.scale;
          const float lcx[4] = {0.f, W, W, 0.f};
          const float lcy[4] = {0.f, 0.f, H, H};
          for (int ci = 0; ci < 4; ++ci) {
            const float wx = patt.insertX + cosR * lcx[ci] - sinR * lcy[ci];
            const float wy = patt.insertY + sinR * lcx[ci] + cosR * lcy[ci];
            m = static_cast<float>(std::max<double>(m, std::hypot(wx - bx, wy - by)));
          }
        }
      }
    }
  }
  // Feature lines (REQ-087) — farthest vertex, as for a polyline.
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    for (int vi = v0; vi < v1; ++vi) {
      const float x = static_cast<float>(st.drawing.featureLineVerts[static_cast<size_t>(vi) * 3]);
      const float y = static_cast<float>(st.drawing.featureLineVerts[static_cast<size_t>(vi) * 3 + 1]);
      m = static_cast<float>(std::max<double>(m, std::hypot(x - bx, y - by)));
    }
  });
  for (int si : st.selectedSurveyPointIndices) {
    if (si >= 0 && static_cast<size_t>(si) < st.drawing.surveyPoints.size()) {
      const SurveyPoint& sp = st.drawing.surveyPoints[static_cast<size_t>(si)];
      m = static_cast<float>(std::max<double>(m, std::hypot(sp.easting - bx, sp.northing - by)));
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


/// REQ-103 STRETCH typed-entry path — mirrors \c HandleModifyText's NeedBase/NeedDestination shape,
/// but calls \c ApplyStretchToSelection with the box captured by this invocation's own box-select
/// instead of \c ApplyTranslationToSelection.
static bool HandleStretchText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using MP = AppCommandState::ModifyPhase;
  if (st.modifyPhase == MP::NeedBase) {
    // REQ-329 increment 4: base/second points carry an elevation and are read in the active UCS axes,
    // the same shared helper HandleModifyText (MOVE/COPY) uses.
    float bx = 0.f, by = 0.f, bwz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, line, false, 0.f, 0.f, 0.f, "STRETCH", &bx, &by, &bwz, &consumed, log))
      return consumed;
    st.modifyBaseX = bx;
    st.modifyBaseY = by;
    st.modifyBaseZ = bwz;
    st.modifyPhase = MP::NeedDestination;
    log.push_back("STRETCH — specify second point (destination).");
    return true;
  }
  if (st.modifyPhase == MP::NeedDestination) {
    float px = 0.f, py = 0.f, dwz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, line, true, st.modifyBaseX, st.modifyBaseY, st.modifyBaseZ, "STRETCH",
                                 &px, &py, &dwz, &consumed, log))
      return consumed;
    const float dx = px - st.modifyBaseX;
    const float dy = py - st.modifyBaseY;
    const float dz = dwz - st.modifyBaseZ;
    PushUndoSnapshot(st, "Stretch");
    ApplyStretchToSelection(st, dx, dy, dz, st.stretchRectMnX, st.stretchRectMxX, st.stretchRectMnY,
                            st.stretchRectMxY, st.stretchRectInUcsPlane, log);
    st.modifyPhase = MP::NeedBase;
    log.push_back("STRETCH complete — base point (ESC to exit):");
    return true;
  }
  return false;
}

// The surrounding anonymous namespace is closed for these TWO functions and reopened after them.
//
// Both are the COMPLETE typed transform — the dispatch included, not just its inner half — and both
// are public because REQ-060 requires a gizmo drag and the equivalent typed command to produce the
// same coordinates. The only way to make that a property rather than a hope is for one function to
// be what both call, which means the gizmo's translation unit and the test that asserts the
// agreement each have to be able to name it. This is the same move `ApplyTranslationToSelection`
// made for the translate handle in slice 4b.


static void FinishScaleCommand(AppCommandState& st, float scaleFactor, std::vector<std::string>& log) {
  PushUndoSnapshot(st, "Scale");
  const float s = std::max(scaleFactor, 1e-6f);
  ApplyUniformScaleAboutBase(st, st.modifyBaseX, st.modifyBaseY, st.modifyBaseZ, s, log);
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
    st.modifyBaseZ = CadCommitElevation(st);  // REQ-329 (SCALE): the scale is about this elevation too
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



static void FinishRotateCommand(AppCommandState& st, float bx, float by, float rad, std::vector<std::string>& log) {
  PushUndoSnapshot(st, st.rotateCopyMode ? "Rotate-copy" : "Rotate");
  using K = AppCommandState::Kind;

  // REQ-329 increment 2 (GitHub issue #402): ROTATE turns the selection about an axis through the
  // picked base point PARALLEL TO THE ACTIVE UCS Z. When that axis is world-Z-parallel — plan view,
  // or any UCS merely rotated/translated in plan — this is identical to the old world-Z rotation
  // through (bx, by), so the pre-REQ-329 path runs unchanged and the result is byte-identical. Only
  // a genuinely tilted UCS takes the axis-aware branch (REQ-328's `RotateSelectionAboutAxis` for a
  // copy, its new in-place sibling otherwise).
  const bool tilted = !CadWorkPlaneIsWorldXy(st);

  if (st.rotateCopyMode) {
    if (tilted) {
      DropSurfacesFromSelectionForTransform(st, "ROTATE", log);
      DropPositionMarkersFromSelection(st, "ROTATE", log);  // REQ-359: move/copy/erase only
      DropMultileadersFromSelection(st, "ROTATE", log);     // REQ-367
      const ucs::Ucs u = CadActiveUcsStorage(st);
      const ray3d::Vec3 axisUnit =
          ray3d::Normalize(ray3d::Vec3{u.zAxis.x, u.zAxis.y, u.zAxis.z});
      RotateSelectionAboutAxis(st, {static_cast<double>(bx), static_cast<double>(by),
                                    static_cast<double>(st.rotateBaseZ)},
                               axisUnit, rad, log, "ROTATE");
      if (!st.selectedSurveyPointIndices.empty())
        log.push_back("ROTATE COPY — " + std::to_string(st.selectedSurveyPointIndices.size()) +
                      " survey point(s) not duplicated: rotation about a tilted axis is not"
                      " supported yet (REQ-328).");
    } else {
      DuplicateCadSelectionRotated(st, bx, by, rad, log);
    }
    st.rotateCopyMode = false;
    st.active = K::None;
    ResetModifyRotateDraft(st);
    if (!tilted && !st.selectedSurveyPointIndices.empty()) {
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
    ApplyRotationAboutUcsZ(st, bx, by, st.rotateBaseZ, rad, log);
    st.active = K::None;
    ResetModifyRotateDraft(st);
    log.push_back("ROTATE complete.");
  }
}


// REQ-305 ARRAY -------------------------------------------------------------------------------

static void ResetArrayDraft(AppCommandState& st) {
  using AP = AppCommandState::ArrayPhase;
  st.arrayPhase = AP::PickSelection;
  st.arrayType = AppCommandState::ArrayType::Rectangular;
  st.arrayCols = st.arrayRows = 0;
  st.arrayColSpacing = st.arrayRowSpacing = 0.f;
  st.arrayLevels = 1;
  st.arrayLevelSpacing = 0.f;
  st.arrayAnchorX = st.arrayAnchorY = st.arrayAnchorZ = 0.f;
  st.arrayCenterX = st.arrayCenterY = st.arrayCenterZ = 0.f;
  st.arrayItemCount = 0;
  st.arrayFillAngleDeg = 360.f;
  st.arrayRotateItems = true;
  st.arrayOnPaper = false;
}

/// The rigid reference point ARRAY hangs the pattern on: the selection centroid in X/Y, and the
/// selection's own 3D bounds centre in Z (\c CadGizmoAnchorWorld, the same anchor the MOVE gizmo
/// uses). Z must be the geometry's real elevation, NOT the work-plane elevation
/// (\c CadCommitElevation): the polar no-rotate path rotates THIS point about the array axis and
/// translates the selection by the result, so an anchor sitting at Z 0 while the geometry is 20 ft
/// up rotates at a 20-ft-inflated radius and the copies fly out (GitHub issue #400 increment 4).
/// Falls back to the work-plane elevation only when the selection bounds nothing measurable.
static void SetArrayAnchorFromSelection(AppCommandState& st) {
  ComputeSelectionCentroidWorld(st, &st.arrayAnchorX, &st.arrayAnchorY);
  ray3d::Vec3 anchor3{};
  st.arrayAnchorZ = CadGizmoAnchorWorld(st, &anchor3) ? static_cast<float>(anchor3.z)
                                                      : CadCommitElevation(st);
}

static void SetArrayAnchorFromPaperSelection(AppCommandState& st) {
  PaperLayout* layout = ActivePaperGeometryTarget(st);
  if (!layout || st.selectedPaperEntities.empty()) {
    st.arrayAnchorX = st.arrayAnchorY = 0.f;
    return;
  }
  const PaperEntityRef ref = st.selectedPaperEntities.front();
  switch (ref.type) {
  case PaperEntityRef::Type::Line: {
    const size_t i = static_cast<size_t>(ref.index) * 6;
    if (i + 4 < layout->paperLines.size()) {
      st.arrayAnchorX = (layout->paperLines[i] + layout->paperLines[i + 3]) * .5f;
      st.arrayAnchorY = (layout->paperLines[i + 1] + layout->paperLines[i + 4]) * .5f;
      return;
    }
    break;
  }
  case PaperEntityRef::Type::Circle: {
    const size_t i = static_cast<size_t>(ref.index) * 3;
    if (i + 1 < layout->paperCircles.size()) {
      st.arrayAnchorX = layout->paperCircles[i]; st.arrayAnchorY = layout->paperCircles[i + 1]; return;
    }
    break;
  }
  case PaperEntityRef::Type::Arc:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperArcs.size()) {
      const auto& a = layout->paperArcs[static_cast<size_t>(ref.index)];
      st.arrayAnchorX = a.cx; st.arrayAnchorY = a.cy; return;
    }
    break;
  case PaperEntityRef::Type::Ellipse:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperEllipses.size()) {
      const auto& e = layout->paperEllipses[static_cast<size_t>(ref.index)];
      st.arrayAnchorX = e.cx; st.arrayAnchorY = e.cy; return;
    }
    break;
  case PaperEntityRef::Type::Polyline:
    if (ref.index >= 0 && static_cast<size_t>(ref.index + 1) < layout->paperPolyOffsets.size()) {
      const int first = layout->paperPolyOffsets[static_cast<size_t>(ref.index)];
      const size_t i = static_cast<size_t>(std::max(first, 0)) * 3;
      if (i + 1 < layout->paperPolyVerts.size()) {
        st.arrayAnchorX = layout->paperPolyVerts[i]; st.arrayAnchorY = layout->paperPolyVerts[i + 1]; return;
      }
    }
    break;
  case PaperEntityRef::Type::Text:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperTexts.size()) {
      const auto& a = layout->paperTexts[static_cast<size_t>(ref.index)];
      st.arrayAnchorX = a.insX; st.arrayAnchorY = a.insY; return;
    }
    break;
  case PaperEntityRef::Type::Block:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperBlockRefs.size()) {
      const auto& b = layout->paperBlockRefs[static_cast<size_t>(ref.index)];
      st.arrayAnchorX = static_cast<float>(b.xf.x); st.arrayAnchorY = static_cast<float>(b.xf.y); return;
    }
    break;
  case PaperEntityRef::Type::Leader:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperMultileaders.size()) {
      const auto& l = layout->paperMultileaders[static_cast<size_t>(ref.index)];
      st.arrayAnchorX = l.label.insX; st.arrayAnchorY = l.label.insY; return;
    }
    break;
  case PaperEntityRef::Type::Table:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperTables.size()) {
      const auto& t = layout->paperTables[static_cast<size_t>(ref.index)];
      st.arrayAnchorX = t.insX; st.arrayAnchorY = t.insY; return;
    }
    break;
  case PaperEntityRef::Type::Spline:
  case PaperEntityRef::Type::Point:
    break;
  }
  st.arrayAnchorX = st.arrayAnchorY = 0.f;
}

/// Resolve a viewport click onto the active UCS work plane, anchored at (\p ax,\p ay,\p az) — the
/// same camera-ray-onto-plane pattern \c CadSolveCircleThreePoints uses. Used by ARRAY's spatial
/// phases (GitHub issue #400 increment 1) and by ROTATE's base/angle picks (REQ-329 increment 2).
/// In plan view / under the World UCS this is exactly the flat \p wx,\p wy pick (regression guard,
/// REQ-305 acceptance 10 / REQ-329): the anchored plane's axes ARE world X/Y there, so the
/// ray/plane intersection reproduces the same point the old flat code read directly off the cursor.
static void CadResolvePickOnWorkPlaneAnchored(const AppCommandState& st, float wx, float wy,
                                           const ray3d::Ray* pickRay, float ax, float ay, float az,
                                           float* outX, float* outY, float* outZ) {
  if (!pickRay || !pickRay->valid() || CadWorkPlaneIsWorldXy(st)) {
    *outX = wx;
    *outY = wy;
    if (outZ)
      *outZ = CadCommitElevation(st);
    return;
  }
  const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, ax, ay, az);
  ray3d::Vec3 hit;
  if (!ray3d::RayPlaneIntersect(*pickRay, ucs::WorkPlane(frame), &hit)) {
    *outX = wx;
    *outY = wy;
    if (outZ)
      *outZ = CadCommitElevation(st);
    return;
  }
  *outX = static_cast<float>(hit.x);
  *outY = static_cast<float>(hit.y);
  if (outZ)
    *outZ = static_cast<float>(hit.z);
}

static void FinishArrayCommand(AppCommandState& st, std::vector<std::string>& log, int instanceCount,
                               const char* shapeDesc) {
  using K = AppCommandState::Kind;
  st.active = K::None;
  ResetArrayDraft(st);
  log.push_back("ARRAY — " + std::to_string(instanceCount) + " total instance(s)" +
                (shapeDesc[0] ? std::string(" (") + shapeDesc + ")." : std::string(".")));
}

/// GitHub issue #400 increment 1: a grid cell's offset is measured along the active UCS X/Y axes
/// (colSpacing/rowSpacing are UCS-local distances — see \c CadResolvePickOnWorkPlaneAnchored), then
/// converted to a world (dx,dy,dz) via the same anchored frame \c CadSolveCircleThreePoints uses
/// for its own plane-vs-flat split. Under the World UCS the frame's axes ARE world X/Y/Z, so this
/// reduces to \p colOffset,\p rowOffset,0 exactly — the REQ-305 acceptance 10 regression guard.
// levelOffset defaults to 0 (GitHub issue #400 increment 2's "levels" is the UCS-Z local
// component of the same vector this always computed) so increment 1's two-argument call sites are
// untouched.
static void ArrayCellWorldDelta(const AppCommandState& st, float colOffset, float rowOffset,
                                float* dx, float* dy, float* dz, float levelOffset = 0.f) {
  const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, st.arrayAnchorX, st.arrayAnchorY, st.arrayAnchorZ);
  const ray3d::Vec3 local{static_cast<double>(colOffset), static_cast<double>(rowOffset),
                          static_cast<double>(levelOffset)};
  const ray3d::Vec3 world = ucs::UcsVectorToWorld(frame, local);
  *dx = static_cast<float>(world.x);
  *dy = static_cast<float>(world.y);
  *dz = static_cast<float>(world.z);
}

/// Rectangular commit: the original selection occupies cell (0,0); every other cell is produced by
/// looping the EXISTING \c DuplicateCadSelectionTranslated (already used by COPY) — no new
/// per-type duplication code. One \c PushUndoSnapshot for the whole grid (REQ-305 acceptance 8).
static void CommitArrayRectangular(AppCommandState& st, std::vector<std::string>& log) {
  const int cols = std::max(st.arrayCols, 1);
  const int rows = std::max(st.arrayRows, 1);
  const int levels = std::max(st.arrayLevels, 1);  // GitHub issue #400 increment 2
  PushUndoSnapshot(st, "Array-Rectangular");
  if (st.arrayOnPaper) {
    const std::vector<PaperEntityRef> source = st.selectedPaperEntities;
    std::vector<std::string> transformLog;
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        if (r == 0 && c == 0)
          continue;
        st.selectedPaperEntities = source;
        TranslateSelectedPaperEntities(st, static_cast<float>(c) * st.arrayColSpacing,
                                       static_cast<float>(r) * st.arrayRowSpacing, true, transformLog, false);
      }
    }
    st.selectedPaperEntities = source;
    FinishArrayCommand(st, log, cols * rows,
                       (std::to_string(cols) + " x " + std::to_string(rows)).c_str());
    return;
  }
  for (int lv = 0; lv < levels; ++lv) {
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        if (lv == 0 && r == 0 && c == 0)
          continue;  // the original selection IS cell (0,0,0) — REQ-305 acceptance 3
        float dx = 0.f, dy = 0.f, dz = 0.f;
        ArrayCellWorldDelta(st, static_cast<float>(c) * st.arrayColSpacing,
                           static_cast<float>(r) * st.arrayRowSpacing, &dx, &dy, &dz,
                           static_cast<float>(lv) * st.arrayLevelSpacing);
        DuplicateCadSelectionTranslated(st, dx, dy, dz);  // solids and pipe runs too (REQ-351)
      }
    }
  }
  std::string shape = std::to_string(cols) + " x " + std::to_string(rows);
  if (levels > 1)
    shape += " x " + std::to_string(levels);
  FinishArrayCommand(st, log, cols * rows * levels, shape.c_str());
}

/// Polar commit: \p arrayRotateItems == true rotates every instance about the active UCS Z axis
/// through the picked centre via \c RotateSelectionAboutAxis (REQ-328) — a genuine 3D rotation, not
/// just the world-Z-only case, so a tilted UCS (previously refused outright) now works for every

static void ErasePolylineByIndex(AppCommandState& st, int pi) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return;
  const int np = static_cast<int>(st.drawing.userPolylineOffsets.size()) - 1;
  std::vector<int> nvPer(static_cast<size_t>(np));
  for (int i = 0; i < np; ++i)
    nvPer[static_cast<size_t>(i)] = st.drawing.userPolylineOffsets[(static_cast<size_t>(i)+ 1)] - st.drawing.userPolylineOffsets[static_cast<size_t>(i)];
  const int a = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int b = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  st.drawing.eraseVertexRange(static_cast<size_t>(a), static_cast<size_t>(b));
  // REQ-316 / ADR-047: the parallel bulge array is per-VERTEX, so its cut span is [a, b) — not the
  // triplet range the vertex array uses. Same reasoning as EraseFeatureLineByIndex's flag array.
  if (!st.drawing.userPolylineVertsBulge.empty() && static_cast<size_t>(b) <= st.drawing.userPolylineVertsBulge.size())
    st.drawing.userPolylineVertsBulge.erase(st.drawing.userPolylineVertsBulge.begin() + static_cast<std::ptrdiff_t>(a),
                                    st.drawing.userPolylineVertsBulge.begin() + static_cast<std::ptrdiff_t>(b));
  // REQ-325 / ADR-053: same per-vertex cut span, 3 floats wide instead of 1.
  if (!st.drawing.userPolylineVertsNormal.empty() && static_cast<size_t>(b) * 3 <= st.drawing.userPolylineVertsNormal.size())
    st.drawing.userPolylineVertsNormal.erase(st.drawing.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(a) * 3,
                                     st.drawing.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(b) * 3);
  std::vector<int> newOff;
  newOff.reserve((static_cast<size_t>(std::max(0, np - 1))+ 1));
  newOff.push_back(0);
  int run = 0;
  for (int i = 0; i < np; ++i) {
    if (i == pi)
      continue;
    run += nvPer[static_cast<size_t>(i)];
    newOff.push_back(run);
  }
  // Erasing the ONLY polyline leaves the seeded 0 with nothing appended after it, and `{0}` is not
  // how this store spells "no polylines" — an EMPTY table is (CommitPolylineDraft seeds the 0 only
  // when it has a polyline to describe, and GsIo's reader rejects a single entry outright). Writing
  // `{0}` produced a internal document payload that saved successfully and could never be reopened: issue #60, REQ-079.
  // The paper-space sibling ErasePaperPolyline has always done this; this store was the outlier.
  if (newOff.size() == 1)
    newOff.clear();
  st.drawing.userPolylineOffsets = std::move(newOff);
  if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
    st.drawing.userPolylineClosed.erase(st.drawing.userPolylineClosed.begin() + static_cast<std::ptrdiff_t>(pi));
  if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
    st.drawing.userPolylineAttrs.erase(st.drawing.userPolylineAttrs.begin() + static_cast<std::ptrdiff_t>(pi));
}

/// REQ-103 step 8 (EXPLODE) / GitHub issue #390. Replace every Polyline in the current selection with
