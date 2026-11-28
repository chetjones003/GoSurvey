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
#include <optional>
#include <GoSurvey/Document/EntityRef.hpp>
#include <GoSurvey/Document/EntityKindEdit.hpp>

// --- One transform path (ADR-0011 / issue #1577), defined in CadSelectionTransform.cpp ------------

/// The Document entity kind a selection entry names, or nullopt for kinds with no EntityKind
/// (PDF underlays, alignments, the section plane).
std::optional<EntityKind> EntityKindForSelection(SelectedEntity::Type t);

/// Remove every selected entry that cannot take \p xf under \p op, logging one line per kind
/// ("<CMD> — N <kinds> excluded: <reason>", REQ-201). Returns how many were removed.
size_t RefuseSelectionForTransform(AppCommandState& st, GoSurvey::Document::EntityOp op,
                                   const GoSurvey::Document::SimilarityTransform& xf, const char* commandName,
                                   std::vector<std::string>& log);

/// Transform the whole selection by \p xf: in place, or (\p duplicate) as copies that leave the
/// originals selected and unchanged. Refuses what a kind cannot represent first. Survey points move
/// in place only; COPY-like commands duplicate them through their ID-policy modal.
void TransformSelection(AppCommandState& st, const GoSurvey::Document::SimilarityTransform& xf,
                        GoSurvey::Document::EntityOp op, const char* commandName, bool duplicate,
                        std::vector<std::string>& log);

/// The direction ROTATE turns about: the active UCS Z, or world Z exactly in plan (REQ-329). Shared
/// by the commit and the preview (issue #1700).
ray3d::Vec3 CadRotateAxisUnit(const AppCommandState& st);

/// MIRROR's reflection across the plane through (\p x0..\p z1) perpendicular to the work plane.
/// Invalid when the two points coincide, or (\p outLineAlongUcsZ) when the line runs along the
/// tilted UCS Z axis. Shared by the commit and the preview (issue #1700).
GoSurvey::Document::SimilarityTransform CadMirrorTransform(const AppCommandState& st, float x0, float y0, float z0,
                                                           float x1, float y1, float z1, bool* outLineAlongUcsZ);

// --- STRETCH (REQ-103), defined in CadCurveEditCommands.cpp ---------------------------------------

/// STRETCH's crossing window [\p mnX,\p mxX]x[\p mnY,\p mxY] and displacement. The window is in plan,
/// or (\p rectInUcsPlane) in the active work plane's own 2D frame. Shared by the commit and the
/// preview (issue #1700).
GoSurvey::Document::StretchWindow CadStretchWindow(const AppCommandState& st, float dx, float dy, float dz, float mnX,
                                                   float mxX, float mnY, float mxY, bool rectInUcsPlane);

/// STRETCH feature line \p fi of \p d: each vertex inside the window moves (REQ-087).
bool CadStretchFeatureLine(DrawingContent& d, int fi, const GoSurvey::Document::StretchWindow& w);

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
/// angle' = 2*phi - angle. A reflection reverses handedness (CCW becomes CW), so reflecting a
/// center point alone does not tell you which way an arc now sweeps.


static double NormalizeAngleRadMinusPiToPi(double a) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;
  while (a > kPi)
    a -= kTwoPi;
  while (a < -kPi)
    a += kTwoPi;
  return a;
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

static void FinalizeCopyTranslation(AppCommandState& st, float dx, float dy, float dz,
                                    std::vector<std::string>& log) {
  st.pendingSurveyDupIsRotate = false;
  TransformSelection(st, GoSurvey::Document::SimilarityTransform::Translation({dx, dy, dz}),
                     GoSurvey::Document::EntityOp::Copy, "COPY", /*duplicate=*/true, log);
  // Stay in COPY — same selection + base, ready for another destination.
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



/// REQ-305 ARRAY: refuse what the per-kind declarations say cannot be arrayed (ADR-0011), then drop
/// survey points, which need an ID-conflict policy for many instances at once (D-2026-08-25-m).
static void RefuseArrayUnsupportedFromSelection(AppCommandState& st, std::vector<std::string>& log) {
  RefuseSelectionForTransform(st, GoSurvey::Document::EntityOp::Array,
                              GoSurvey::Document::SimilarityTransform::Identity(), "ARRAY", log);
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


// REQ-305 ARRAY -------------------------------------------------------------------------------

static void SetArrayAnchorFromSelection(AppCommandState& st) {
  auto* arr = st.sessionAs<stepengine::ArraySession>();
  if (!arr)
    return;
  ComputeSelectionCentroidWorld(st, &arr->anchorX, &arr->anchorY);
  ray3d::Vec3 anchor3{};
  arr->anchorZ = CadGizmoAnchorWorld(st, &anchor3) ? static_cast<float>(anchor3.z)
                                                   : CadCommitElevation(st);
}

static void SetArrayAnchorFromPaperSelection(AppCommandState& st) {
  auto* arr = st.sessionAs<stepengine::ArraySession>();
  if (!arr)
    return;
  PaperLayout* layout = ActivePaperGeometryTarget(st);
  if (!layout || st.selectedPaperEntities.empty()) {
    arr->anchorX = arr->anchorY = 0.f;
    return;
  }
  const PaperEntityRef ref = st.selectedPaperEntities.front();
  switch (ref.type) {
  case PaperEntityRef::Type::Line: {
    const size_t i = static_cast<size_t>(ref.index) * 6;
    if (i + 4 < layout->paperLines.size()) {
      arr->anchorX = (layout->paperLines[i] + layout->paperLines[i + 3]) * .5f;
      arr->anchorY = (layout->paperLines[i + 1] + layout->paperLines[i + 4]) * .5f;
      return;
    }
    break;
  }
  case PaperEntityRef::Type::Circle: {
    const size_t i = static_cast<size_t>(ref.index) * 3;
    if (i + 1 < layout->paperCircles.size()) {
      arr->anchorX = layout->paperCircles[i]; arr->anchorY = layout->paperCircles[i + 1]; return;
    }
    break;
  }
  case PaperEntityRef::Type::Arc:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperArcs.size()) {
      const auto& a = layout->paperArcs[static_cast<size_t>(ref.index)];
      arr->anchorX = a.cx; arr->anchorY = a.cy; return;
    }
    break;
  case PaperEntityRef::Type::Ellipse:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperEllipses.size()) {
      const auto& e = layout->paperEllipses[static_cast<size_t>(ref.index)];
      arr->anchorX = e.cx; arr->anchorY = e.cy; return;
    }
    break;
  case PaperEntityRef::Type::Polyline:
    if (ref.index >= 0 && static_cast<size_t>(ref.index + 1) < layout->paperPolyOffsets.size()) {
      const int first = layout->paperPolyOffsets[static_cast<size_t>(ref.index)];
      const size_t i = static_cast<size_t>(std::max(first, 0)) * 3;
      if (i + 1 < layout->paperPolyVerts.size()) {
        arr->anchorX = layout->paperPolyVerts[i]; arr->anchorY = layout->paperPolyVerts[i + 1]; return;
      }
    }
    break;
  case PaperEntityRef::Type::Text:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperTexts.size()) {
      const auto& a = layout->paperTexts[static_cast<size_t>(ref.index)];
      arr->anchorX = a.insX; arr->anchorY = a.insY; return;
    }
    break;
  case PaperEntityRef::Type::Block:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperBlockRefs.size()) {
      const auto& b = layout->paperBlockRefs[static_cast<size_t>(ref.index)];
      arr->anchorX = static_cast<float>(b.xf.x); arr->anchorY = static_cast<float>(b.xf.y); return;
    }
    break;
  case PaperEntityRef::Type::Leader:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperMultileaders.size()) {
      const auto& l = layout->paperMultileaders[static_cast<size_t>(ref.index)];
      arr->anchorX = l.label.insX; arr->anchorY = l.label.insY; return;
    }
    break;
  case PaperEntityRef::Type::Table:
    if (ref.index >= 0 && static_cast<size_t>(ref.index) < layout->paperTables.size()) {
      const auto& t = layout->paperTables[static_cast<size_t>(ref.index)];
      arr->anchorX = t.insX; arr->anchorY = t.insY; return;
    }
    break;
  case PaperEntityRef::Type::Spline:
  case PaperEntityRef::Type::Point:
    break;
  }
  arr->anchorX = arr->anchorY = 0.f;
}

static void CadResolvePickOnWorkPlaneAnchored(const AppCommandState& st, double wx, double wy,
                                           const ray3d::Ray* pickRay, double ax, double ay, double az,
                                           double* outX, double* outY, double* outZ) {
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
  *outX = hit.x;
  *outY = hit.y;
  if (outZ)
    *outZ = hit.z;
}

static void CadResolvePickOnWorkPlaneAnchored(const AppCommandState& st, float wx, float wy,
                                           const ray3d::Ray* pickRay, float ax, float ay, float az,
                                           float* outX, float* outY, float* outZ) {
  double ox = 0.0, oy = 0.0, oz = 0.0;
  CadResolvePickOnWorkPlaneAnchored(st, static_cast<double>(wx), static_cast<double>(wy), pickRay,
                                    static_cast<double>(ax), static_cast<double>(ay), static_cast<double>(az),
                                    &ox, &oy, &oz);
  *outX = static_cast<float>(ox);
  *outY = static_cast<float>(oy);
  if (outZ)
    *outZ = static_cast<float>(oz);
}

static void FinishArrayCommand(AppCommandState& st, std::vector<std::string>& log, int instanceCount,
                               const char* shapeDesc) {
  stepengine::EndSession(st);
  log.push_back("ARRAY — " + std::to_string(instanceCount) + " total instance(s)" +
                (shapeDesc[0] ? std::string(" (") + shapeDesc + ")." : std::string(".")));
}

static void ArrayCellWorldDelta(const AppCommandState& st, const stepengine::ArraySession& arr,
                                float colOffset, float rowOffset,
                                float* dx, float* dy, float* dz, float levelOffset = 0.f) {
  const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, arr.anchorX, arr.anchorY, arr.anchorZ);
  const ray3d::Vec3 local{static_cast<double>(colOffset), static_cast<double>(rowOffset),
                          static_cast<double>(levelOffset)};
  const ray3d::Vec3 world = ucs::UcsVectorToWorld(frame, local);
  *dx = static_cast<float>(world.x);
  *dy = static_cast<float>(world.y);
  *dz = static_cast<float>(world.z);
}

static void CommitArrayRectangular(AppCommandState& st, std::vector<std::string>& log) {
  const auto* arr = st.sessionAs<stepengine::ArraySession>();
  if (!arr)
    return;
  const int cols = std::max(arr->cols, 1);
  const int rows = std::max(arr->rows, 1);
  const int levels = std::max(arr->levels, 1);
  const float colSpacing = arr->colSpacing;
  const float rowSpacing = arr->rowSpacing;
  const float levelSpacing = arr->levelSpacing;
  const bool onPaper = arr->onPaper;
  PushUndoSnapshot(st, "Array-Rectangular");
  if (onPaper) {
    const std::vector<PaperEntityRef> source = st.selectedPaperEntities;
    std::vector<std::string> transformLog;
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        if (r == 0 && c == 0)
          continue;
        st.selectedPaperEntities = source;
        TranslateSelectedPaperEntities(st, static_cast<float>(c) * colSpacing,
                                       static_cast<float>(r) * rowSpacing, true, transformLog, false);
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
          continue;
        float dx = 0.f, dy = 0.f, dz = 0.f;
        ArrayCellWorldDelta(st, *arr, static_cast<float>(c) * colSpacing,
                            static_cast<float>(r) * rowSpacing, &dx, &dy, &dz,
                            static_cast<float>(lv) * levelSpacing);
        TransformSelection(st, GoSurvey::Document::SimilarityTransform::Translation({dx, dy, dz}),
                           GoSurvey::Document::EntityOp::Array, "ARRAY", /*duplicate=*/true, log);
      }
    }
  }
  std::string shape = std::to_string(cols) + " x " + std::to_string(rows);
  if (levels > 1)
    shape += " x " + std::to_string(levels);
  FinishArrayCommand(st, log, cols * rows * levels, shape.c_str());
}

/// Polar commit: \p arrayRotateItems == true rotates every instance about the active UCS Z axis
/// through the picked centre via \c TransformSelection (REQ-328, issue #1577) — a genuine 3D rotation, not
/// just the world-Z-only case, so a tilted UCS (previously refused outright) now works for every

/// Erase polyline \p pi with its side-car data (Document's \ref GoSurvey::Document::ErasePolyline).
static void ErasePolylineByIndex(AppCommandState& st, int pi) {
  if (pi >= 0)
    GoSurvey::Document::ErasePolyline(st.drawing, static_cast<size_t>(pi));
}

/// REQ-103 step 8 (EXPLODE) / GitHub issue #390. Replace every Polyline in the current selection with
