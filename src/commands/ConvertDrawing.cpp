#include "ConvertDrawing.hpp"

#include "CadCoordinateFrame.hpp"
#include "SurveyPoints.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kFlatTol = 1e-6f;

/// The plane of an arc / ellipse / bulge segment is "flat" when its normal is +Z, so a turn about the
/// vertical axis turns its angles by exactly the turn.
bool NormalIsPlusZ(float nx, float ny, float nz) { return std::fabs(nx) < kFlatTol && std::fabs(ny) < kFlatTol && nz > 0.f; }

bool NormalIsVertical(float nx, float ny) { return std::fabs(nx) < kFlatTol && std::fabs(ny) < kFlatTol; }

struct Xf {
  double s, c, sn, sz;
  double rot;
  bool   Turns() const { return std::fabs(rot) > 1e-12; }
  void P(double x, double y, double* ox, double* oy) const {
    *ox = s * (c * x - sn * y);
    *oy = s * (sn * x + c * y);
  }
  template <class T> void Pt(T* x, T* y) const {
    double a, b;
    P(static_cast<double>(*x), static_cast<double>(*y), &a, &b);
    *x = static_cast<T>(a);
    *y = static_cast<T>(b);
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

  for (size_t i = 0; i + 5 < st.userLinesFlat.size(); i += 6) {
    f.Pt(&st.userLinesFlat[i], &st.userLinesFlat[i + 1]);
    f.Z(&st.userLinesFlat[i + 2]);
    f.Pt(&st.userLinesFlat[i + 3], &st.userLinesFlat[i + 4]);
    f.Z(&st.userLinesFlat[i + 5]);
  }
  for (size_t i = 0; i + 3 < st.userCirclesCxCyZR.size(); i += 4) {
    f.Pt(&st.userCirclesCxCyZR[i], &st.userCirclesCxCyZR[i + 1]);
    f.Z(&st.userCirclesCxCyZR[i + 2]);
    st.userCirclesCxCyZR[i + 3] *= f.s;
  }
  for (size_t i = 0; i + 2 < st.userCircleNormals.size(); i += 3)
    f.Dir(&st.userCircleNormals[i], &st.userCircleNormals[i + 1]);
  for (CadArc& a : st.userArcs) {
    f.Pt(&a.cx, &a.cy);
    f.Z(&a.z);
    a.r *= f.s;
    if (f.Turns() && NormalIsVertical(a.nx, a.ny))  // seen from the normal, a turn about +Z is a turn about N·sign(nz)
      a.startRad += static_cast<float>(a.nz >= 0.f ? t.rotationRad : -t.rotationRad);
  }
  for (CadEllipse& e : st.userEllipses) {
    f.Pt(&e.cx, &e.cy);
    f.Z(&e.z);
    double mx = 0.0, my = 0.0;  // the major axis is a vector: scaled and turned, not shifted
    f.P(e.majVx, e.majVy, &mx, &my);
    e.majVx = static_cast<float>(mx);
    e.majVy = static_cast<float>(my);
  }
  for (size_t i = 0; i + 2 < st.userPolylineVerts.size(); i += 3) {
    f.Pt(&st.userPolylineVerts[i], &st.userPolylineVerts[i + 1]);
    f.Z(&st.userPolylineVerts[i + 2]);
  }
  for (size_t i = 0; i + 2 < st.userPolylineVertsNormal.size(); i += 3)
    f.Dir(&st.userPolylineVertsNormal[i], &st.userPolylineVertsNormal[i + 1]);
  for (size_t i = 0; i + 2 < st.featureLineVerts.size(); i += 3) {
    f.Pt(&st.featureLineVerts[i], &st.featureLineVerts[i + 1]);
    f.Z(&st.featureLineVerts[i + 2]);
  }
  for (CadFilledRegion& r : st.cadFilledRegions) {
    for (size_t i = 0; i + 2 < r.vertsXyz.size(); i += 3) {
      f.Pt(&r.vertsXyz[i], &r.vertsXyz[i + 1]);
      f.Z(&r.vertsXyz[i + 2]);
    }
    r.patternAngleDeg += static_cast<float>(t.rotationRad * 180.0 / 3.14159265358979323846);
    r.patternScale *= static_cast<float>(f.s);
  }
  for (CadAnnotation& a : st.cadAnnotations) {
    if (a.surveyPointLabelForId >= 0)
      continue;  // a point's label is rebuilt from the point below
    f.Pt(&a.insX, &a.insY);
    f.Z(&a.insZ);
    a.rotationRad += static_cast<float>(t.rotationRad);
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
  for (CadTable& tb : st.cadTables) {
    f.Pt(&tb.insX, &tb.insY);
    f.Z(&tb.insZ);
    tb.rotationRad += static_cast<float>(t.rotationRad);
    tb.width *= static_cast<float>(f.s);
    tb.height *= static_cast<float>(f.s);
  }
  for (CadBlockRef& b : st.cadBlockRefs) {
    f.Pt(&b.xf.x, &b.xf.y);
    f.Z(&b.xf.z);
    b.xf.sx *= static_cast<float>(f.s);
    b.xf.sy *= static_cast<float>(f.s);
    b.xf.sz *= static_cast<float>(f.sz);
    b.xf.rotZ += static_cast<float>(t.rotationRad);
  }
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
