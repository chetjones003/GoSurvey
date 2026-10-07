#include "ConvertDrawing.hpp"

#include "CadCoordinateFrame.hpp"
#include "SurveyPoints.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float kFlatTol = 1e-6f;

/// The plane of an arc / ellipse / bulge segment is "flat" when its normal is +Z, so a turn about the
/// vertical axis turns its angles by exactly the turn.
bool NormalIsPlusZ(float nx, float ny, float nz) { return std::fabs(nx) < kFlatTol && std::fabs(ny) < kFlatTol && nz > 0.f; }

bool NormalIsVertical(float nx, float ny) { return std::fabs(nx) < kFlatTol && std::fabs(ny) < kFlatTol; }

struct Xf {
  double s, c, sn, sz;
  double rot;
  double dx = 0.0, dy = 0.0;  ///< added to every POINT (not to a direction or a length): see ApplyClipboardConversion
  bool   Turns() const { return std::fabs(rot) > 1e-12; }
  void P(double x, double y, double* ox, double* oy) const {
    *ox = s * (c * x - sn * y);
    *oy = s * (sn * x + c * y);
  }
  template <class T> void Pt(T* x, T* y) const {
    double a, b;
    P(static_cast<double>(*x), static_cast<double>(*y), &a, &b);
    *x = static_cast<T>(a + dx);
    *y = static_cast<T>(b + dy);
  }
  template <class T> void Z(T* z) const { *z = static_cast<T>(sz * static_cast<double>(*z)); }
  template <class T> void Dir(T* x, T* y) const {  // a direction: turned, not scaled
    double a = c * static_cast<double>(*x) - sn * static_cast<double>(*y);
    double b = sn * static_cast<double>(*x) + c * static_cast<double>(*y);
    *x = static_cast<T>(a);
    *y = static_cast<T>(b);
  }
};

std::string Plural(size_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// The per-kind moves below serve both a whole drawing (ApplyDrawingConversion) and a clipboard
// (ApplyClipboardConversion): the same objects, the same exact transform.
template <class V> void MoveLines(V& lines, const Xf& f) {
  for (size_t i = 0; i + 5 < lines.size(); i += 6) {
    f.Pt(&lines[i], &lines[i + 1]);
    f.Z(&lines[i + 2]);
    f.Pt(&lines[i + 3], &lines[i + 4]);
    f.Z(&lines[i + 5]);
  }
}

template <class V, class N> void MoveCircles(V& cxCyZR, N& normals, const Xf& f) {
  for (size_t i = 0; i + 3 < cxCyZR.size(); i += 4) {
    f.Pt(&cxCyZR[i], &cxCyZR[i + 1]);
    f.Z(&cxCyZR[i + 2]);
    cxCyZR[i + 3] *= f.s;
  }
  for (size_t i = 0; i + 2 < normals.size(); i += 3)
    f.Dir(&normals[i], &normals[i + 1]);
}

void MoveArcs(std::vector<CadArc>& arcs, const Xf& f, double rotationRad) {
  for (CadArc& a : arcs) {
    f.Pt(&a.cx, &a.cy);
    f.Z(&a.z);
    a.r *= f.s;
    if (f.Turns() && NormalIsVertical(a.nx, a.ny))  // seen from the normal, a turn about +Z is a turn about N·sign(nz)
      a.startRad += static_cast<float>(a.nz >= 0.f ? rotationRad : -rotationRad);
  }
}

void MoveEllipses(std::vector<CadEllipse>& ellipses, const Xf& f) {
  for (CadEllipse& e : ellipses) {
    f.Pt(&e.cx, &e.cy);
    f.Z(&e.z);
    double mx = 0.0, my = 0.0;  // the major axis is a vector: scaled and turned, not shifted
    f.P(e.majVx, e.majVy, &mx, &my);
    e.majVx = static_cast<float>(mx);
    e.majVy = static_cast<float>(my);
  }
}

template <class V> void MovePolylineVerts(V& verts, const Xf& f) {
  for (size_t i = 0; i + 2 < verts.size(); i += 3) {
    f.Pt(&verts[i], &verts[i + 1]);
    f.Z(&verts[i + 2]);
  }
}

void MoveFilledRegions(std::vector<CadFilledRegion>& regions, const Xf& f, double rotationRad) {
  for (CadFilledRegion& r : regions) {
    MovePolylineVerts(r.vertsXyz, f);
    r.patternAngleDeg += static_cast<float>(rotationRad * 180.0 / 3.14159265358979323846);
    r.patternScale *= static_cast<float>(f.s);
  }
}

void MoveAnnotations(std::vector<CadAnnotation>& annotations, const Xf& f, double rotationRad) {
  for (CadAnnotation& a : annotations) {
    if (a.surveyPointLabelForId >= 0)
      continue;  // a point's label is rebuilt from the point
    f.Pt(&a.insX, &a.insY);
    f.Z(&a.insZ);
    a.rotationRad += static_cast<float>(rotationRad);
    float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
    float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
    for (int k = 0; k < 4; ++k)
      f.Pt(&xs[k], &ys[k]);
    a.boxMinX = *std::min_element(xs, xs + 4);
    a.boxMaxX = *std::max_element(xs, xs + 4);
    a.boxMinY = *std::min_element(ys, ys + 4);
    a.boxMaxY = *std::max_element(ys, ys + 4);
    if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      f.Pt(&a.dimExt1X, &a.dimExt1Y);
      f.Pt(&a.dimExt2X, &a.dimExt2Y);
      a.dimSignedOffset *= static_cast<float>(f.s);
    }
    if (a.kind == CadAnnotation::Kind::DimAngular)
      f.Pt(&a.dimAngVertexX, &a.dimAngVertexY);
  }
}

void MoveTables(std::vector<CadTable>& tables, const Xf& f, double rotationRad) {
  for (CadTable& tb : tables) {
    f.Pt(&tb.insX, &tb.insY);
    f.Z(&tb.insZ);
    tb.rotationRad += static_cast<float>(rotationRad);
    tb.width *= static_cast<float>(f.s);
    tb.height *= static_cast<float>(f.s);
  }
}

void MoveBlockRefs(std::vector<CadBlockRef>& refs, const Xf& f, double rotationRad) {
  for (CadBlockRef& b : refs) {
    f.Pt(&b.xf.x, &b.xf.y);
    f.Z(&b.xf.z);
    b.xf.sx *= static_cast<float>(f.s);
    b.xf.sy *= static_cast<float>(f.s);
    b.xf.sz *= static_cast<float>(f.sz);
    b.xf.rotZ += static_cast<float>(rotationRad);
  }
}

}  // namespace

std::vector<std::string> UnconvertibleKinds(const AppCommandState& st, bool rotates) {
  std::vector<std::string> k;
  if (!st.cadSurfaces.empty())
    k.push_back(Plural(st.cadSurfaces.size(), "surface", "surfaces"));
  if (!st.cadMeshes.empty())
    k.push_back(Plural(st.cadMeshes.size(), "mesh", "meshes"));
  if (!st.cadPointClouds.empty())
    k.push_back(Plural(st.cadPointClouds.size(), "point cloud", "point clouds"));
  if (!st.cadSolids.empty())
    k.push_back(Plural(st.cadSolids.size(), "3D solid", "3D solids"));
  if (!st.cadPipeRuns.empty())
    k.push_back(Plural(st.cadPipeRuns.size(), "pipe run", "pipe runs"));
  if (!st.cadPositionMarkers.empty())
    k.push_back(Plural(st.cadPositionMarkers.size(), "position marker", "position markers"));
  if (!st.cadMultileaders.empty())
    k.push_back(Plural(st.cadMultileaders.size(), "multileader", "multileaders"));
  if (!st.pdfAttachments.empty())
    k.push_back(Plural(st.pdfAttachments.size(), "PDF underlay", "PDF underlays"));
  size_t viewports = 0;
  for (const PaperLayout& l : st.paperLayouts)
    viewports += l.viewports.size();
  if (viewports > 0)
    k.push_back(Plural(viewports, "paper-space viewport", "paper-space viewports"));

  if (rotates) {  // a turn about the vertical axis cannot be applied exactly to a tilted arc or ellipse
    size_t tiltedArcs = 0;
    for (const CadArc& a : st.userArcs)
      if (!NormalIsVertical(a.nx, a.ny))
        ++tiltedArcs;
    for (const CadEllipse& e : st.userEllipses)
      if (!NormalIsPlusZ(e.nx, e.ny, e.nz))
        ++tiltedArcs;
    for (size_t i = 0; i < st.userPolylineVertsBulge.size(); ++i) {
      if (st.userPolylineVertsBulge[i] == 0.f || i * 3 + 2 >= st.userPolylineVertsNormal.size())
        continue;
      if (!NormalIsPlusZ(st.userPolylineVertsNormal[i * 3], st.userPolylineVertsNormal[i * 3 + 1],
                         st.userPolylineVertsNormal[i * 3 + 2]))
        ++tiltedArcs;
    }
    if (tiltedArcs > 0)
      k.push_back(Plural(tiltedArcs, "arc or ellipse in a tilted plane", "arcs or ellipses in tilted planes"));
  }
  return k;
}

bool DrawingWorldExtents(const AppCommandState& st, double* minX, double* maxX, double* minY, double* maxY) {
  if (CadCoord::ComputeWorldSpaceExtents(st, minX, maxX, minY, maxY))
    return true;
  *minX = *maxX = st.worldDocumentOriginX;
  *minY = *maxY = st.worldDocumentOriginY;
  return false;
}

void ApplyDrawingConversion(AppCommandState& st, const geo::Similarity& t, std::vector<std::string>& log) {
  Xf f{t.scale, std::cos(t.rotationRad), std::sin(t.rotationRad), t.scaleZ, t.rotationRad};

  // The new document origin: world' = A + sR·world, and the stored coordinates are sR·local.
  double ox = 0.0, oy = 0.0;
  t.Apply(st.worldDocumentOriginX, st.worldDocumentOriginY, &ox, &oy);
  st.worldDocumentOriginX = ox;
  st.worldDocumentOriginY = oy;

  MoveLines(st.userLinesFlat, f);
  MoveCircles(st.userCirclesCxCyZR, st.userCircleNormals, f);
  MoveArcs(st.userArcs, f, t.rotationRad);
  MoveEllipses(st.userEllipses, f);
  MovePolylineVerts(st.userPolylineVerts, f);
  for (size_t i = 0; i + 2 < st.userPolylineVertsNormal.size(); i += 3)
    f.Dir(&st.userPolylineVertsNormal[i], &st.userPolylineVertsNormal[i + 1]);
  MovePolylineVerts(st.featureLineVerts, f);
  MoveFilledRegions(st.cadFilledRegions, f, t.rotationRad);
  MoveAnnotations(st.cadAnnotations, f, t.rotationRad);
  MoveTables(st.cadTables, f, t.rotationRad);
  MoveBlockRefs(st.cadBlockRefs, f, t.rotationRad);
  for (SurveyPoint& p : st.surveyPoints) {
    f.Pt(&p.easting, &p.northing);
    f.Z(&p.elevation);
  }

  // Annotation heights are plotted inches times the scale; keeping the scale in step keeps the text
  // the same size on paper when the unit changes (it is an override of the project's scale, REQ-375).
  if (std::fabs(t.scaleZ - 1.0) > 1e-12)
    st.modelUnitsPerPlottedInch = static_cast<float>(st.modelUnitsPerPlottedInch * t.scaleZ);

  size_t views = st.namedViews.size(), ucss = st.ucsNamed.size();
  st.namedViews.clear();
  st.ucsNamed.clear();
  st.ucsPrevious.clear();
  st.activeUcs = ucs::Ucs{};
  st.activeViewName.clear();
  if (views > 0)
    log.push_back("Convert - " + Plural(views, "saved view was", "saved views were") +
                  " removed: they pointed at the old coordinates.");
  if (ucss > 0)
    log.push_back("Convert - " + Plural(ucss, "saved UCS was", "saved UCSs were") +
                  " removed: they pointed at the old coordinates.");
  DrawingSettings& ds = st.drawingSettings;
  const bool hadLocation = ds.markerX != 0.0 || ds.markerY != 0.0 || !ds.capturedAreas.empty() || ds.transform.apply;
  ds.ResetGeographicMarker();
  ds.transform = DrawingSettings::Transform{};
  ds.capturedAreas.clear();
  if (hadLocation)
    log.push_back("Convert - the geographic marker, transformation and captured map areas belonged to the old "
                  "coordinate system and were reset.");

  RegenerateAllSurveyPointLabels(st);
  BumpCadGpuCache(st);
}

// ---- a clipboard (REQ-383 clause 7 / D-2026-10-07-a) -------------------------------------------

std::vector<std::string> UnconvertibleKinds(const CadClipboard& cb, bool rotates) {
  std::vector<std::string> k;
  if (rotates) {  // the same rule as for a drawing: a turn about the vertical axis cannot tilt an arc exactly
    size_t tilted = 0;
    for (const CadArc& a : cb.arcs)
      if (!NormalIsVertical(a.nx, a.ny))
        ++tilted;
    for (const CadEllipse& e : cb.ellipses)
      if (!NormalIsPlusZ(e.nx, e.ny, e.nz))
        ++tilted;
    if (tilted > 0)
      k.push_back(Plural(tilted, "arc or ellipse in a tilted plane", "arcs or ellipses in tilted planes"));
  }
  return k;
}

bool ClipboardWorldExtents(const CadClipboard& cb, double* minX, double* maxX, double* minY, double* maxY) {
  double x0 = std::numeric_limits<double>::max(), x1 = std::numeric_limits<double>::lowest();
  double y0 = x0, y1 = x1;
  auto add = [&](double x, double y, double r = 0.0) {
    x0 = std::min(x0, x - r);
    x1 = std::max(x1, x + r);
    y0 = std::min(y0, y - r);
    y1 = std::max(y1, y + r);
  };
  for (size_t i = 0; i + 5 < cb.lines.size(); i += 6) {
    add(cb.lines[i], cb.lines[i + 1]);
    add(cb.lines[i + 3], cb.lines[i + 4]);
  }
  for (size_t i = 0; i + 3 < cb.circlesCxCyZR.size(); i += 4)
    add(cb.circlesCxCyZR[i], cb.circlesCxCyZR[i + 1], cb.circlesCxCyZR[i + 3]);
  for (const CadArc& a : cb.arcs)
    add(a.cx, a.cy, a.r);
  for (const CadEllipse& e : cb.ellipses)
    add(e.cx, e.cy, std::hypot(static_cast<double>(e.majVx), static_cast<double>(e.majVy)));
  for (size_t i = 0; i + 2 < cb.polyVerts.size(); i += 3)
    add(cb.polyVerts[i], cb.polyVerts[i + 1]);
  for (const CadAnnotation& a : cb.annotations) {
    add(a.insX, a.insY);
    add(a.boxMinX, a.boxMinY);
    add(a.boxMaxX, a.boxMaxY);
  }
  for (const CadTable& tb : cb.tables)
    add(tb.insX, tb.insY, std::max(tb.width, tb.height));
  for (const CadBlockRef& b : cb.blockRefs)
    add(b.xf.x, b.xf.y);
  for (const CadFilledRegion& r : cb.filledRegions)
    for (size_t i = 0; i + 2 < r.vertsXyz.size(); i += 3)
      add(r.vertsXyz[i], r.vertsXyz[i + 1]);
  if (x0 > x1) {  // nothing to measure
    *minX = *maxX = cb.srcOriginX;
    *minY = *maxY = cb.srcOriginY;
    return false;
  }
  *minX = x0 + cb.srcOriginX;
  *maxX = x1 + cb.srcOriginX;
  *minY = y0 + cb.srcOriginY;
  *maxY = y1 + cb.srcOriginY;
  return true;
}

void ApplyClipboardConversion(CadClipboard& cb, const geo::Similarity& t, double destOriginX, double destOriginY) {
  // world' = T(world) with world = srcOrigin + local, and the destination stores local = world' - destOrigin,
  // so local' = sR·local + (T(srcOrigin) - destOrigin): one shift on every point, none on a direction or length.
  double nx = 0.0, ny = 0.0;
  t.Apply(cb.srcOriginX, cb.srcOriginY, &nx, &ny);
  Xf f{t.scale, std::cos(t.rotationRad), std::sin(t.rotationRad), t.scaleZ, t.rotationRad, nx - destOriginX, ny - destOriginY};

  MoveLines(cb.lines, f);
  MoveCircles(cb.circlesCxCyZR, cb.circleNormals, f);
  MoveArcs(cb.arcs, f, t.rotationRad);
  MoveEllipses(cb.ellipses, f);
  MovePolylineVerts(cb.polyVerts, f);
  MoveFilledRegions(cb.filledRegions, f, t.rotationRad);
  MoveAnnotations(cb.annotations, f, t.rotationRad);
  MoveTables(cb.tables, f, t.rotationRad);
  MoveBlockRefs(cb.blockRefs, f, t.rotationRad);
  f.Pt(&cb.basePtX, &cb.basePtY);
  cb.srcOriginX = destOriginX;
  cb.srcOriginY = destOriginY;
}
