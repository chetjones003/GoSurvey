#include "LibreDwgCad.hpp"

#include "AcisSatParser.hpp"
#include "CadCommands.hpp"
#include "CadField.hpp"
#include "LibreDwgField.hpp"
#include "LibreDwgDynamicBlock.hpp"
#include "LibreDwgAnnotContext.hpp"
#include "LibreDwgMaterial.hpp"
#include "LibreDwgVisualStyle.hpp"
#include "util/cadpiperun.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadDimGeom.hpp"
#include "CadDimStroke.hpp"
#include "PolylineTiltedArc.hpp"
#include "DxfColors.hpp"
#include "MtextRichFormat.hpp"
#include "DwgIo.hpp"
#include "LibreDwg.hpp"
#include "SurveyPoints.hpp"
#include "TextStyle.hpp"
#include "util/cadtable.hpp"
#include "util/SaveTrace.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif

extern "C" {
#include <dwg.h>
#include <dwg_api.h>
#include "bits.h"
#include "out_dxf.h"
}

namespace libredwgcad_detail {

std::string DecodeDwgString(const void* raw, bool utf16le) {
  if (raw == nullptr)
    return {};
  if (utf16le) {
    char* u8 = bit_convert_TU(reinterpret_cast<BITCODE_TU>(const_cast<void*>(raw)));
    if (u8 == nullptr)
      return {};
    std::string s(u8);
    std::free(u8);
    return s;
  }
  return std::string(reinterpret_cast<const char*>(raw));
}

// GitHub issue #369 / D-2026-09-10-b. A Civil 3D "parts catalog" component — a pressure pipe,
// a fitting, a structure — carries no portable geometry: its shape is regenerated at open time
// by Civil 3D's proprietary Parts Catalog engine from a parametric catalog reference, the same
// way Plant 3D's AcPp* custom objects are unreachable by any third-party reader (ADR-026). Such
// a file's only 3DSOLID is an empty placeholder, and its class table is full of AECC_* custom
// classes. When that signature is present, a skipped empty 3DSOLID is named for its real cause
// rather than the ambiguous "(empty)". \p dwg may be null.
bool DwgHasCivil3dCatalogClasses(const Dwg_Data* dwg) {
  if (dwg == nullptr || dwg->dwg_class == nullptr)
    return false;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strncmp(name, "AECC_", 5) == 0)
      return true;
  }
  return false;
}

std::string ColorToStorage(int index, unsigned method, unsigned rgb) {
  if (method == 0xc0)
    return "ByLayer";
  if (method == 0xc1)
    return "ByBlock";
  if (method == 0xc3) {
    // 0xc3 is truecolor, except for the documented sentinels (dwg.h): rgb 0 = ByBlock,
    // 0x100 = ByLayer, 0x101 = none. Fall through to the index path for those.
    //
    // AutoCAD 2018 (AC1032) also writes an *indexed* layer/entity colour as a 0xc3 CMC whose
    // rgb payload is just the ACI in the low byte (e.g. 0xc3000007 == ACI 7). LibreDWG does not
    // resolve that to RGB the way it does for 0xc2. A real 24-bit truecolour always has a
    // non-zero red or green byte; when only the low byte is set, treat it as an ACI index so a
    // 0xc3-encoded "layer 0 white" does not import as near-black #000007.
    const unsigned c = rgb & 0xFFFFFFu;
    if (c != 0 && c != 0x100u && c != 0x101u && (c & 0xFFFF00u) != 0) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "#%06X", c);
      return std::string(buf);
    }
    if ((c & 0xFFFF00u) == 0 && c >= 1 && c <= 255) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "#%06X",
                    static_cast<unsigned>(DxfRgbPackedFromAci(static_cast<int>(c)) & 0xFFFFFFu));
      return std::string(buf);
    }
  }
  // A negative ACI encodes "layer turned off" (dwg.h); the on/off state is captured separately,
  // so recover the real ACI here rather than discarding the colour.
  if (index < 0)
    index = -index;
  if (index == 256)
    return "ByLayer";
  if (index == 0)
    return "ByBlock";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned>(DxfRgbPackedFromAci(index) & 0xFFFFFFu));
  return std::string(buf);
}

}  // namespace libredwgcad_detail

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string LowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return s;
}

struct Xf2 {
  double ox = 0, oy = 0;
  double ang = 0;
  double sx = 1, sy = 1;
  void apply(double x, double y, double* oxOut, double* oyOut) const {
    const double c = std::cos(ang);
    const double s = std::sin(ang);
    const double xr = x * sx;
    const double yr = y * sy;
    *oxOut = ox + c * xr - s * yr;
    *oyOut = oy + s * xr + c * yr;
  }
  [[nodiscard]] bool isIdentity() const {
    return std::fabs(ox) < 1e-9 && std::fabs(oy) < 1e-9 && std::fabs(ang) < 1e-9 &&
           std::fabs(sx - 1.0) < 1e-9 && std::fabs(sy - 1.0) < 1e-9;
  }
};

// REQ-312 / GitHub issue #435: DWG equivalent of DxfOcsToWorld / DxfExtrusionIsFlat.
[[nodiscard]] inline bool DwgExtrusionIsFlat(double nx, double ny, double nz) {
  return nx == 0.0 && ny == 0.0 && nz == 1.0;
}

[[nodiscard]] inline bool DwgOcsToWorld(double x, double y, double z, double nx, double ny, double nz,
                                        ray3d::Vec3* out) {
  ucs::Ucs frame;
  if (!ucs::FromNormal({0.0, 0.0, 0.0}, {nx, ny, nz}, &frame))
    return false;
  if (out)
    *out = ucs::UcsToWorld(frame, {x, y, z});
  return true;
}

inline void RotateExtrusionByXf(const Xf2& xf, double* nx, double* ny) {
  if (nx == nullptr || ny == nullptr)
    return;
  if (std::fabs(xf.ang) < 1e-12)
    return;
  const double c = std::cos(xf.ang);
  const double s = std::sin(xf.ang);
  const double ox = *nx;
  const double oy = *ny;
  *nx = c * ox - s * oy;
  *ny = s * ox + c * oy;
}

// LibreDWG keeps table/entity strings in the file's *native* encoding. For R2007+ DWGs that is
// UTF-16LE (BITCODE_TU); casting straight to char* truncates the name at the first NUL byte
// (issue #140). IS_FROM_TU_DWG is LibreDWG's own decode-side gate for this.
std::string FromT(const Dwg_Data* dwg, BITCODE_T t) {
  return libredwgcad_detail::DecodeDwgString(t, dwg != nullptr && IS_FROM_TU_DWG(dwg));
}

std::string LayerName(Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  if (dwg == nullptr || ent == nullptr || ent->layer == nullptr)
    return "0";
  Dwg_Object* o = dwg_resolve_handle_silent(dwg, ent->layer->absolute_ref);
  if (o == nullptr || o->fixedtype != DWG_TYPE_LAYER || o->tio.object == nullptr)
    return "0";
  const Dwg_Object_LAYER* ly = o->tio.object->tio.LAYER;
  if (ly == nullptr)
    return "0";
  const std::string n = FromT(dwg, ly->name);
  return n.empty() ? std::string("0") : n;
}

std::string ColorStorage(const Dwg_Color& c) {
  // R2004+ entity ENC: true RGB in the low 24 bits when flag 0x80 is set (common_entity_data.spec).
  if ((c.flag & 0x80) != 0) {
    const unsigned rgb24 = static_cast<unsigned>(c.rgb) & 0xFFFFFFu;
    if (rgb24 != 0 && rgb24 != 0x100u && rgb24 != 0x101u && (rgb24 & 0xFFFF00u) != 0) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "#%06X", rgb24);
      return std::string(buf);
    }
  }
  return libredwgcad_detail::ColorToStorage(static_cast<int>(c.index), c.method,
                                            static_cast<unsigned>(c.rgb));
}

std::string EntityLinetypeName(Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  if (dwg == nullptr || ent == nullptr)
    return "ByLayer";
  // ltype_flags: 0 ByLayer, 1 ByBlock, 2 Continuous, 3 has explicit handle.
  if (ent->ltype_flags == 1)
    return "ByBlock";
  if (ent->ltype_flags != 3 || ent->ltype == nullptr)
    return "ByLayer";
  Dwg_Object* o = dwg_resolve_handle_silent(dwg, ent->ltype->absolute_ref);
  if (o == nullptr || o->fixedtype != DWG_TYPE_LTYPE || o->tio.object == nullptr ||
      o->tio.object->tio.LTYPE == nullptr)
    return "ByLayer";
  std::string n = FromT(dwg, o->tio.object->tio.LTYPE->name);
  const std::string l = LowerAscii(n);
  if (l.empty() || l == "bylayer")
    return "ByLayer";
  if (l == "continuous")
    return "Continuous";
  return n;
}

// REQ-170, issue #609: DWG stores lineweight as a table index (see dxf_cvt_lweight in dwg.c);
// GoSurvey stores millimetres on paper, or -1 for ByLayer / default.
float LineweightMmFromDwgIndex(BITCODE_RC idx) {
  return CadDxfLineweightMmFromEnum370(dxf_cvt_lweight(static_cast<BITCODE_BSd>(idx)));
}

BITCODE_RC LineweightDwgIndexFromMm(float mm, bool layerRow) {
  if (mm < 0.f)
    return layerRow ? static_cast<BITCODE_RC>(31) : static_cast<BITCODE_RC>(29);  // -3 default / -1 ByLayer
  return static_cast<BITCODE_RC>(dxf_revcvt_lweight(CadDxfLineweightEnum370FromMm(mm)));
}

float Transparency01FromEntityColor(const Dwg_Color& c) {
  if ((c.flag & 0x20) == 0)
    return -1.f;
  const BITCODE_RC alpha = c.alpha_type == 3 ? c.alpha : static_cast<BITCODE_RC>(c.alpha_raw & 0xFFu);
  if (alpha <= 0)
    return -1.f;
  return static_cast<float>(alpha) / 255.f;
}

void ApplyEntityEncTransparency(Dwg_Color* color, float transparency01) {
  if (color == nullptr || transparency01 <= 1.e-5f)
    return;
  const float t = std::clamp(transparency01, 0.f, 1.f);
  const BITCODE_RC alpha = static_cast<BITCODE_RC>(static_cast<int>(t * 255.f + 0.5f));
  if (alpha == 0)
    return;
  color->flag = static_cast<uint16_t>(color->flag | 0x20);
  color->alpha_type = 3;
  color->alpha = alpha;
  color->alpha_raw = (static_cast<BITCODE_BL>(3) << 24) | static_cast<BITCODE_BL>(alpha);
}

EntityAttributes AttrFromEnt(Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  EntityAttributes a{};
  a.layer = LayerName(dwg, ent);
  if (ent != nullptr) {
    a.color = ColorStorage(ent->color);
    a.linetype = EntityLinetypeName(dwg, ent);
    a.lineweightMm = LineweightMmFromDwgIndex(ent->linewt);
    const float tr = Transparency01FromEntityColor(ent->color);
    if (tr >= 0.f)
      a.transparency = tr;
  }
  return a;
}

void NoteSkip(std::unordered_map<std::string, int>* hist, const char* name) {
  if (hist == nullptr || name == nullptr)
    return;
  ++(*hist)[name];
}

static void PushImportedMesh(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* owner,
                             std::shared_ptr<CadMesh> mesh, EntityAttributes at) {
  if (dwg != nullptr && owner != nullptr)
    DwgImportApplyEntityMaterial(dwg, owner, &at);
  st.cadMeshes.push_back(std::move(mesh));
  st.cadMeshAttrs.push_back(at);
}

void LocalLine(AppCommandState& st, double x0, double y0, double z0, double x1, double y1, double z1,
               const EntityAttributes& at) {
  st.userLinesFlat.push_back(x0 - st.worldDocumentOriginX);
  st.userLinesFlat.push_back(y0 - st.worldDocumentOriginY);
  st.userLinesFlat.push_back(z0);
  st.userLinesFlat.push_back(x1 - st.worldDocumentOriginX);
  st.userLinesFlat.push_back(y1 - st.worldDocumentOriginY);
  st.userLinesFlat.push_back(z1);
  st.userLineAttrs.push_back(at);
}

void LocalCircle(AppCommandState& st, double cx, double cy, double r, double z, const EntityAttributes& at,
                 double nx = 0.0, double ny = 0.0, double nz = 1.0) {
  st.userCirclesCxCyZR.push_back(cx - st.worldDocumentOriginX);
  st.userCirclesCxCyZR.push_back(cy - st.worldDocumentOriginY);
  st.userCirclesCxCyZR.push_back(z);
  st.userCirclesCxCyZR.push_back(r);
  st.userCircleAttrs.push_back(at);
  PushCircleNormal(st.userCircleNormals, static_cast<float>(nx), static_cast<float>(ny),
                   static_cast<float>(nz));
}

template <class T>
void ArcFromAngles(double a0, double a1, T* startRad, T* sweepRad) {
  double sweep = a1 - a0;
  if (std::fabs(sweep) < 1e-12)
    sweep = 2.0 * kPi;
  while (sweep < 0.0)
    sweep += 2.0 * kPi;
  while (sweep > 2.0 * kPi)
    sweep -= 2.0 * kPi;
  if (sweep < 1e-12)
    sweep = 2.0 * kPi;
  *startRad = static_cast<T>(a0);
  *sweepRad = static_cast<T>(sweep);
}

void LocalArc(AppCommandState& st, double cx, double cy, double r, double a0, double a1, double z,
              const EntityAttributes& at, double nx = 0.0, double ny = 0.0, double nz = 1.0) {
  if (r <= 1e-12)
    return;
  CadArc arc{};
  arc.cx = cx - st.worldDocumentOriginX;
  arc.cy = cy - st.worldDocumentOriginY;
  arc.r = r;
  ArcFromAngles(a0, a1, &arc.startRad, &arc.sweepRad);
  arc.z = z;
  arc.nx = static_cast<float>(nx);
  arc.ny = static_cast<float>(ny);
  arc.nz = static_cast<float>(nz);
  st.userArcs.push_back(arc);
  st.userArcAttrs.push_back(at);
}

// REQ-316 / ADR-047: `bulge`, when given, is one entry per vertex (DXF group 42's own convention) —
// null or shorter than `xyz`'s vertex count means every segment stays straight, matching every
// caller that predates bulge support (POLYLINE_2D/POLYLINE_3D below have no bulge concept at all).
void LocalPolyline(AppCommandState& st, const std::vector<double>& xyz, bool closed,
                   const EntityAttributes& at, const std::vector<double>* bulge = nullptr) {
  const size_t nv = xyz.size() / 3;
  if (nv < 2)
    return;
  const int base = st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.back();
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(base);
  for (size_t i = 0; i < nv; ++i) {
    st.userPolylineVerts.push_back(xyz[i * 3 + 0] - st.worldDocumentOriginX);
    st.userPolylineVerts.push_back(xyz[i * 3 + 1] - st.worldDocumentOriginY);
    st.userPolylineVerts.push_back(xyz[i * 3 + 2]);
  }
  st.userPolylineOffsets.push_back(base + static_cast<int>(nv));
  st.userPolylineClosed.push_back(closed ? uint8_t{1} : uint8_t{0});
  st.userPolylineAttrs.push_back(at);
  bool anyBulge = bulge != nullptr;
  if (anyBulge) {
    anyBulge = false;
    for (size_t i = 0; i < nv && i < bulge->size(); ++i)
      if ((*bulge)[i] != 0.0) { anyBulge = true; break; }
  }
  if (anyBulge || !st.userPolylineVertsBulge.empty()) {
    SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
    const size_t tail = st.userPolylineVertsBulge.size() >= nv ? st.userPolylineVertsBulge.size() - nv : 0;
    if (bulge)
      for (size_t i = 0; i < nv && i < bulge->size(); ++i)
        st.userPolylineVertsBulge[tail + i] = static_cast<float>((*bulge)[i]);
  }
}

void ImportAnnotativeVisibilityFromEntityEed(const Dwg_Object_Entity* ent, std::vector<std::string>* out);

void LocalText(AppCommandState& st, double x, double y, double z, double height, double rotRad,
               const std::string& text, CadAnnotation::Kind kind, const EntityAttributes& at,
               bool annotative = false, const Dwg_Object_Entity* ownerEnt = nullptr) {
  CadAnnotation a{};
  a.kind = kind;
  a.insX = x - st.worldDocumentOriginX;
  a.insY = y - st.worldDocumentOriginY;
  a.insZ = z;
  const double mup = std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
  a.plottedHeightInches = static_cast<float>(height / mup);
  a.rotationRad = static_cast<float>(rotRad);
  a.text = text;
  a.annotative = annotative;
  if (ownerEnt != nullptr)
    ImportAnnotativeVisibilityFromEntityEed(ownerEnt, &a.annotativeVisibleScaleNames);
  st.cadAnnotations.push_back(std::move(a));
  st.cadAnnotationAttrs.push_back(at);
}

// REQ-170, issue #613: no native spline — tessellate into a polyline (fit points or control points).
static bool ImportSplineAsPolyline(AppCommandState& st, const Dwg_Entity_SPLINE* sp, const Xf2& xf,
                                   const EntityAttributes& at) {
  if (sp == nullptr)
    return false;
  std::vector<double> xyz;
  auto pushWorld = [&](double x, double y, double z) {
    double wx = 0.0, wy = 0.0;
    xf.apply(x, y, &wx, &wy);
    xyz.push_back(wx);
    xyz.push_back(wy);
    xyz.push_back(z);
  };
  if (sp->num_fit_pts >= 2 && sp->fit_pts != nullptr) {
    for (BITCODE_BS i = 0; i < sp->num_fit_pts; ++i)
      pushWorld(sp->fit_pts[i].x, sp->fit_pts[i].y, sp->fit_pts[i].z);
  } else if (sp->num_ctrl_pts >= 2 && sp->ctrl_pts != nullptr) {
    for (BITCODE_BL i = 0; i < sp->num_ctrl_pts; ++i)
      pushWorld(sp->ctrl_pts[i].x, sp->ctrl_pts[i].y, sp->ctrl_pts[i].z);
  } else {
    return false;
  }
  if (xyz.size() < 6)
    return false;
  const bool closed =
      sp->closed_b != 0 || (sp->splineflags & SPLINE_SPLINEFLAGS_CLOSED) != 0 || sp->periodic != 0;
  LocalPolyline(st, xyz, closed, at);
  return true;
}

static void EllipseParamPoint(const Dwg_Entity_ELLIPSE* e, double t, double* outX, double* outY,
                              double* outZ) {
  const double mx = e->sm_axis.x;
  const double my = e->sm_axis.y;
  const double mlen = std::hypot(mx, my);
  if (mlen < 1.e-12) {
    *outX = e->center.x;
    *outY = e->center.y;
    *outZ = e->center.z;
    return;
  }
  const double minX = -my / mlen * mlen * e->axis_ratio;
  const double minY = mx / mlen * mlen * e->axis_ratio;
  *outX = e->center.x + std::cos(t) * mx + std::sin(t) * minX;
  *outY = e->center.y + std::cos(t) * my + std::sin(t) * minY;
  *outZ = e->center.z;
}

// REQ-170, issue #613: partial elliptical arcs become an open polyline approximation.
static bool ImportTrimmedEllipseAsPolyline(AppCommandState& st, const Dwg_Entity_ELLIPSE* e, const Xf2& xf,
                                           const EntityAttributes& at) {
  if (e == nullptr)
    return false;
  double a0 = e->start_angle;
  double a1 = e->end_angle;
  while (a1 < a0)
    a1 += 2.0 * kPi;
  double span = a1 - a0;
  while (span > 2.0 * kPi)
    span -= 2.0 * kPi;
  if (span < 1.e-9)
    return false;
  const int nseg = std::max(8, static_cast<int>(std::ceil(span / (kPi / 12.0))));
  std::vector<double> xyz;
  xyz.reserve(static_cast<size_t>(nseg + 1) * 3);
  for (int i = 0; i <= nseg; ++i) {
    const double t = a0 + span * (static_cast<double>(i) / static_cast<double>(nseg));
    double px = 0.0, py = 0.0, pz = 0.0;
    EllipseParamPoint(e, t, &px, &py, &pz);
    double wx = 0.0, wy = 0.0;
    xf.apply(px, py, &wx, &wy);
    xyz.push_back(wx);
    xyz.push_back(wy);
    xyz.push_back(pz);
  }
  if (xyz.size() < 6)
    return false;
  LocalPolyline(st, xyz, false, at);
  return true;
}

static bool CornersEqual2d(const BITCODE_2RD& a, const BITCODE_2RD& b) {
  return std::fabs(a.x - b.x) < 1.e-9 && std::fabs(a.y - b.y) < 1.e-9;
}

// REQ-170, issue #613: 2D SOLID / TRACE → solid filled region (ADR-011).
static bool Import2DSolidOrTrace(AppCommandState& st, const BITCODE_2RD& c1, const BITCODE_2RD& c2,
                                 const BITCODE_2RD& c3, const BITCODE_2RD& c4, double elev, const Xf2& xf,
                                 const EntityAttributes& at) {
  CadFilledRegion region;
  region.loopStart.push_back(0);
  auto pushCorner = [&](const BITCODE_2RD& c) {
    double wx = 0.0, wy = 0.0;
    xf.apply(c.x, c.y, &wx, &wy);
    region.vertsXyz.push_back(wx - st.worldDocumentOriginX);
    region.vertsXyz.push_back(wy - st.worldDocumentOriginY);
    region.vertsXyz.push_back(elev);
  };
  pushCorner(c1);
  pushCorner(c2);
  pushCorner(c3);
  if (!CornersEqual2d(c3, c4))
    pushCorner(c4);
  if (region.vertsXyz.size() < 9)
    return false;
  region.patternName.clear();
  st.cadFilledRegions.push_back(std::move(region));
  st.cadFilledRegionAttrs.push_back(at);
  return true;
}

static bool Import3DFaceAsMesh(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* owner,
                               const Dwg_Entity__3DFACE* f, const Xf2& xf, const EntityAttributes& at) {
  if (f == nullptr)
    return false;
  auto same3d = [](const BITCODE_3BD& a, const BITCODE_3BD& b) {
    return std::fabs(a.x - b.x) < 1.e-9 && std::fabs(a.y - b.y) < 1.e-9 && std::fabs(a.z - b.z) < 1.e-9;
  };
  const bool triangle = same3d(f->corner3, f->corner4);
  auto mesh = std::make_shared<CadMesh>();
  mesh->sourceName = "DWG 3DFACE";
  auto addVert = [&](const BITCODE_3BD& p) {
    double wx = 0.0, wy = 0.0;
    xf.apply(p.x, p.y, &wx, &wy);
    mesh->vertsXyz.push_back(static_cast<float>(wx - st.worldDocumentOriginX));
    mesh->vertsXyz.push_back(static_cast<float>(wy - st.worldDocumentOriginY));
    mesh->vertsXyz.push_back(static_cast<float>(p.z));
    mesh->normalsXyz.push_back(0.f);
    mesh->normalsXyz.push_back(0.f);
    mesh->normalsXyz.push_back(1.f);
  };
  addVert(f->corner1);
  addVert(f->corner2);
  addVert(f->corner3);
  if (!triangle)
    addVert(f->corner4);
  if (triangle) {
    mesh->indices = {0, 1, 2};
  } else {
    mesh->indices = {0, 1, 2, 0, 2, 3};
  }
  CadMeshPart part;
  part.name = "3DFACE";
  part.indexBegin = 0;
  part.indexCount = static_cast<int>(mesh->indices.size());
  mesh->parts.push_back(part);
  PushImportedMesh(st, dwg, owner, std::move(mesh), at);
  return true;
}

static bool ImportPolylineMesh(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj,
                               const Dwg_Entity_POLYLINE_MESH* pm, const Xf2& xf,
                               const EntityAttributes& at) {
  if (dwg == nullptr || obj == nullptr || pm == nullptr)
    return false;
  const int m = static_cast<int>(pm->num_m_verts);
  const int n = static_cast<int>(pm->num_n_verts);
  if (m < 2 || n < 2)
    return false;
  std::vector<dwg_point_3d> grid;
  grid.reserve(static_cast<size_t>(m * n));
  auto appendVertex = [&](Dwg_Object* v) {
    if (v == nullptr || v->fixedtype != DWG_TYPE_VERTEX_MESH || v->tio.entity == nullptr ||
        v->tio.entity->tio.VERTEX_MESH == nullptr)
      return;
    const Dwg_Entity_VERTEX_3D* p = v->tio.entity->tio.VERTEX_MESH;
    grid.push_back({p->point.x, p->point.y, p->point.z});
  };
  if (pm->num_owned > 0 && pm->vertex != nullptr) {
    for (BITCODE_BL i = 0; i < pm->num_owned; ++i) {
      Dwg_Object* v = pm->vertex[i] != nullptr ? pm->vertex[i]->obj : nullptr;
      if (v == nullptr && pm->vertex[i] != nullptr)
        v = dwg_resolve_handle_silent(dwg, pm->vertex[i]->absolute_ref);
      appendVertex(v);
    }
  } else {
    for (Dwg_Object* v = get_first_owned_entity(obj); v != nullptr; v = get_next_owned_entity(obj, v))
      appendVertex(v);
  }
  if (static_cast<int>(grid.size()) < m * n && pm->first_vertex != nullptr) {
    grid.clear();
    Dwg_Object* v = pm->first_vertex->obj;
    if (v == nullptr)
      v = dwg_resolve_handle_silent(dwg, pm->first_vertex->absolute_ref);
    for (int guard = 0; v != nullptr && guard < m * n + 8; ++guard) {
      if (v->fixedtype == DWG_TYPE_SEQEND)
        break;
      appendVertex(v);
      Dwg_Object_Entity* entVtx = v->tio.entity;
      if (entVtx == nullptr || entVtx->next_entity == nullptr)
        break;
      v = entVtx->next_entity->obj;
      if (v == nullptr)
        v = dwg_resolve_handle_silent(dwg, entVtx->next_entity->absolute_ref);
    }
  }
  if (static_cast<int>(grid.size()) < m * n)
    return false;
  auto mesh = std::make_shared<CadMesh>();
  mesh->sourceName = "DWG POLYLINE_MESH";
  for (const dwg_point_3d& p : grid) {
    double wx = 0.0, wy = 0.0;
    xf.apply(p.x, p.y, &wx, &wy);
    mesh->vertsXyz.push_back(static_cast<float>(wx - st.worldDocumentOriginX));
    mesh->vertsXyz.push_back(static_cast<float>(wy - st.worldDocumentOriginY));
    mesh->vertsXyz.push_back(static_cast<float>(p.z));
    mesh->normalsXyz.push_back(0.f);
    mesh->normalsXyz.push_back(0.f);
    mesh->normalsXyz.push_back(1.f);
  }
  auto idx = [&](int mi, int ni) { return static_cast<std::uint32_t>(mi * n + ni); };
  for (int mi = 0; mi + 1 < m; ++mi) {
    for (int ni = 0; ni + 1 < n; ++ni) {
      const std::uint32_t a = idx(mi, ni);
      const std::uint32_t b = idx(mi + 1, ni);
      const std::uint32_t c = idx(mi, ni + 1);
      const std::uint32_t d = idx(mi + 1, ni + 1);
      mesh->indices.push_back(a);
      mesh->indices.push_back(b);
      mesh->indices.push_back(c);
      mesh->indices.push_back(b);
      mesh->indices.push_back(d);
      mesh->indices.push_back(c);
    }
  }
  CadMeshPart part;
  part.name = "Mesh";
  part.indexBegin = 0;
  part.indexCount = static_cast<int>(mesh->indices.size());
  mesh->parts.push_back(part);
  PushImportedMesh(st, dwg, obj != nullptr ? obj->tio.entity : nullptr, std::move(mesh), at);
  return true;
}

static int PfaceVertexIndex(BITCODE_BSd raw) {
  if (raw == 0)
    return -1;
  const int idx = static_cast<int>(raw);
  if (idx < 0)
    return -idx - 1;
  return idx - 1;
}

static bool ImportPolylinePFace(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj,
                                const Dwg_Entity_POLYLINE_PFACE* pf, const Xf2& xf,
                                const EntityAttributes& at) {
  if (dwg == nullptr || obj == nullptr || pf == nullptr)
    return false;
  int nv = static_cast<int>(pf->numverts);
  int nf = static_cast<int>(pf->numfaces);
  std::vector<dwg_point_3d> positions;
  std::vector<Dwg_Entity_VERTEX_PFACE_FACE*> faces;
  positions.reserve(static_cast<size_t>(nv));
  faces.reserve(static_cast<size_t>(nf));
  auto inspectVertexObject = [&](Dwg_Object* v) {
    if (v == nullptr || v->tio.entity == nullptr)
      return;
    if (v->fixedtype == DWG_TYPE_VERTEX_PFACE && v->tio.entity->tio.VERTEX_PFACE != nullptr) {
      const Dwg_Entity_VERTEX_3D* p = v->tio.entity->tio.VERTEX_PFACE;
      positions.push_back({p->point.x, p->point.y, p->point.z});
    } else if (v->fixedtype == DWG_TYPE_VERTEX_PFACE_FACE &&
               v->tio.entity->tio.VERTEX_PFACE_FACE != nullptr) {
      faces.push_back(v->tio.entity->tio.VERTEX_PFACE_FACE);
    }
  };
  if (pf->num_owned > 0 && pf->vertex != nullptr) {
    for (BITCODE_BL i = 0; i < pf->num_owned; ++i) {
      Dwg_Object* v = pf->vertex[i] != nullptr ? pf->vertex[i]->obj : nullptr;
      if (v == nullptr && pf->vertex[i] != nullptr)
        v = dwg_resolve_handle_silent(dwg, pf->vertex[i]->absolute_ref);
      inspectVertexObject(v);
    }
  } else {
    for (Dwg_Object* v = get_first_owned_entity(obj); v != nullptr; v = get_next_owned_entity(obj, v))
      inspectVertexObject(v);
  }
  if ((static_cast<int>(positions.size()) < nv || static_cast<int>(faces.size()) < nf) &&
      pf->first_vertex != nullptr) {
    positions.clear();
    faces.clear();
    Dwg_Object* v = pf->first_vertex->obj;
    if (v == nullptr)
      v = dwg_resolve_handle_silent(dwg, pf->first_vertex->absolute_ref);
    for (int guard = 0; v != nullptr && guard < nv + nf + 8; ++guard) {
      if (v->fixedtype == DWG_TYPE_SEQEND)
        break;
      inspectVertexObject(v);
      Dwg_Object_Entity* entVtx = v->tio.entity;
      if (entVtx == nullptr || entVtx->next_entity == nullptr)
        break;
      v = entVtx->next_entity->obj;
      if (v == nullptr)
        v = dwg_resolve_handle_silent(dwg, entVtx->next_entity->absolute_ref);
    }
  }
  if (static_cast<int>(positions.size()) < 3 || static_cast<int>(faces.size()) < 1) {
    positions.clear();
    faces.clear();
    const BITCODE_RLL owner = obj->handle.value;
    for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
      Dwg_Object* v = &dwg->object[i];
      if (v->supertype != DWG_SUPERTYPE_ENTITY || v->tio.entity == nullptr ||
          v->tio.entity->ownerhandle == nullptr)
        continue;
      if (v->tio.entity->ownerhandle->absolute_ref != owner)
        continue;
      inspectVertexObject(v);
    }
  }
  if (static_cast<int>(positions.size()) < 3)
    return false;
  if (nv <= 0 || nv > static_cast<int>(positions.size()))
    nv = static_cast<int>(positions.size());
  else
    positions.resize(static_cast<size_t>(nv));
  if (static_cast<int>(faces.size()) < 1)
    return false;
  if (nf <= 0 || nf > static_cast<int>(faces.size()))
    nf = static_cast<int>(faces.size());
  auto mesh = std::make_shared<CadMesh>();
  mesh->sourceName = "DWG POLYLINE_PFACE";
  for (const dwg_point_3d& p : positions) {
    double wx = 0.0, wy = 0.0;
    xf.apply(p.x, p.y, &wx, &wy);
    mesh->vertsXyz.push_back(static_cast<float>(wx - st.worldDocumentOriginX));
    mesh->vertsXyz.push_back(static_cast<float>(wy - st.worldDocumentOriginY));
    mesh->vertsXyz.push_back(static_cast<float>(p.z));
    mesh->normalsXyz.push_back(0.f);
    mesh->normalsXyz.push_back(0.f);
    mesh->normalsXyz.push_back(1.f);
  }
  auto addTri = [&](int i0, int i1, int i2) {
    if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv)
      return;
    mesh->indices.push_back(static_cast<std::uint32_t>(i0));
    mesh->indices.push_back(static_cast<std::uint32_t>(i1));
    mesh->indices.push_back(static_cast<std::uint32_t>(i2));
  };
  for (int fi = 0; fi < nf; ++fi) {
    const Dwg_Entity_VERTEX_PFACE_FACE* f = faces[static_cast<size_t>(fi)];
    if (f == nullptr)
      continue;
    const int i0 = PfaceVertexIndex(f->vertind[0]);
    const int i1 = PfaceVertexIndex(f->vertind[1]);
    const int i2 = PfaceVertexIndex(f->vertind[2]);
    const int i3 = PfaceVertexIndex(f->vertind[3]);
    if (i3 < 0)
      addTri(i0, i1, i2);
    else {
      addTri(i0, i1, i2);
      addTri(i0, i2, i3);
    }
  }
  if (mesh->indices.empty())
    return false;
  CadMeshPart part;
  part.name = "PFace";
  part.indexBegin = 0;
  part.indexCount = static_cast<int>(mesh->indices.size());
  mesh->parts.push_back(part);
  PushImportedMesh(st, dwg, obj != nullptr ? obj->tio.entity : nullptr, std::move(mesh), at);
  return true;
}

static void AppendLocalPathPoint(CadMultileader& m, const AppCommandState& st, double wx, double wy,
                                 double z) {
  m.pathXyz.push_back(static_cast<float>(wx - st.worldDocumentOriginX));
  m.pathXyz.push_back(static_cast<float>(wy - st.worldDocumentOriginY));
  m.pathXyz.push_back(static_cast<float>(z));
}

static bool ImportLeaderEntity(AppCommandState& st, Dwg_Data* dwg, const Dwg_Entity_LEADER* ld, const Xf2& xf,
                               const EntityAttributes& at) {
  if (ld == nullptr || ld->num_points < 2 || ld->points == nullptr)
    return false;
  Dwg_Object* ann = nullptr;
  if (ld->associated_annotation != nullptr) {
    ann = ld->associated_annotation->obj;
    if (ann == nullptr)
      ann = dwg_resolve_handle_silent(dwg, ld->associated_annotation->absolute_ref);
  }
  const Dwg_Entity_MTEXT* mt =
      (ann != nullptr && ann->fixedtype == DWG_TYPE_MTEXT && ann->tio.entity != nullptr)
          ? ann->tio.entity->tio.MTEXT
          : nullptr;
  if (mt != nullptr) {
    CadMultileader m{};
    for (BITCODE_BL i = 0; i < ld->num_points; ++i) {
      double wx = 0.0, wy = 0.0;
      xf.apply(ld->points[i].x, ld->points[i].y, &wx, &wy);
      AppendLocalPathPoint(m, st, wx, wy, ld->points[i].z);
    }
    double x = 0.0, y = 0.0;
    xf.apply(mt->ins_pt.x, mt->ins_pt.y, &x, &y);
    const double rot = std::atan2(mt->x_axis_dir.y, mt->x_axis_dir.x);
    CadAnnotation& an = m.label;
    an.kind = CadAnnotation::Kind::Mtext;
    an.insX = static_cast<float>(x - st.worldDocumentOriginX);
    an.insY = static_cast<float>(y - st.worldDocumentOriginY);
    an.insZ = static_cast<float>(mt->ins_pt.z);
    an.plottedHeightInches =
        static_cast<float>(static_cast<double>(mt->text_height) /
                           std::max(1e-9, static_cast<double>(st.modelUnitsPerPlottedInch)));
    an.rotationRad = static_cast<float>(rot + xf.ang);
    an.text = FromT(dwg, mt->text);
    an.boxMinX = an.insX;
    an.boxMinY = an.insY - static_cast<float>(mt->text_height);
    an.boxMaxX = an.insX + static_cast<float>(
        std::max(static_cast<double>(mt->rect_width), static_cast<double>(mt->text_height) * 4.0));
    an.boxMaxY = an.insY;
    st.cadMultileaders.push_back(std::move(m));
    st.cadMultileaderAttrs.push_back(at);
    return true;
  }
  std::vector<double> xyz;
  xyz.reserve(static_cast<size_t>(ld->num_points) * 3);
  for (BITCODE_BL i = 0; i < ld->num_points; ++i) {
    double wx = 0.0, wy = 0.0;
    xf.apply(ld->points[i].x, ld->points[i].y, &wx, &wy);
    xyz.push_back(wx);
    xyz.push_back(wy);
    xyz.push_back(ld->points[i].z);
  }
  LocalPolyline(st, xyz, false, at);
  return true;
}

[[nodiscard]] bool ImportEedMarksAnnotative(const Dwg_Data* dwg, const Dwg_Object_Entity* ent);

static bool ImportMultileaderEntity(AppCommandState& st, Dwg_Data* dwg, const Dwg_Entity_MULTILEADER* ml,
                                    const Xf2& xf, const EntityAttributes& at,
                                    const Dwg_Object_Entity* ownerEnt) {
  if (ml == nullptr || dwg == nullptr)
    return false;
  if (ml->ctx.has_content_blk && !ml->ctx.has_content_txt)
    return false;
  if (!ml->ctx.has_content_txt)
    return false;
  if (ml->ctx.num_leaders < 1 || ml->ctx.leaders == nullptr)
    return false;
  const Dwg_LEADER_Node& node = ml->ctx.leaders[0];
  if (node.num_lines < 1 || node.lines == nullptr || node.lines[0].num_points < 2 ||
      node.lines[0].points == nullptr)
    return false;

  auto importLeaderNodePath = [&](const Dwg_LEADER_Node& lnode, std::vector<float>* out) {
    if (lnode.num_lines < 1 || lnode.lines == nullptr || lnode.lines[0].num_points < 2 ||
        lnode.lines[0].points == nullptr)
      return;
    const Dwg_LEADER_Line& seg = lnode.lines[0];
    for (BITCODE_BL pi = 0; pi < seg.num_points; ++pi) {
      double wx = 0.0, wy = 0.0;
      xf.apply(seg.points[pi].x, seg.points[pi].y, &wx, &wy);
      out->push_back(static_cast<float>(wx - st.worldDocumentOriginX));
      out->push_back(static_cast<float>(wy - st.worldDocumentOriginY));
      out->push_back(static_cast<float>(seg.points[pi].z));
    }
    if (lnode.has_lastleaderlinepoint != 0) {
      double wx = 0.0, wy = 0.0;
      xf.apply(lnode.lastleaderlinepoint.x, lnode.lastleaderlinepoint.y, &wx, &wy);
      out->push_back(static_cast<float>(wx - st.worldDocumentOriginX));
      out->push_back(static_cast<float>(wy - st.worldDocumentOriginY));
      out->push_back(static_cast<float>(lnode.lastleaderlinepoint.z));
    }
  };

  CadMultileader m{};
  importLeaderNodePath(node, &m.pathXyz);
  if (m.pathXyz.size() < 6)
    return false;
  for (BITCODE_BL li = 1; li < ml->ctx.num_leaders; ++li) {
    std::vector<float> branch;
    importLeaderNodePath(ml->ctx.leaders[li], &branch);
    if (branch.size() >= 6)
      m.extraLeaderPaths.push_back(std::move(branch));
  }

  const Dwg_MLEADER_Content_MText& txt = ml->ctx.content.txt;
  double lx = 0.0, ly = 0.0;
  xf.apply(txt.location.x, txt.location.y, &lx, &ly);
  CadAnnotation& an = m.label;
  an.kind = CadAnnotation::Kind::Mtext;
  an.insX = static_cast<float>(lx - st.worldDocumentOriginX);
  an.insY = static_cast<float>(ly - st.worldDocumentOriginY);
  an.insZ = static_cast<float>(txt.location.z);
  const double th = ml->ctx.text_height > 1e-9 ? ml->ctx.text_height : 0.18;
  an.plottedHeightInches =
      static_cast<float>(th / std::max(1e-9, static_cast<double>(st.modelUnitsPerPlottedInch)));
  an.text = FromT(dwg, txt.default_text);
  if (an.text.empty())
    an.text = " ";
  const double rot = std::atan2(txt.direction.y, txt.direction.x);
  an.rotationRad = static_cast<float>(rot + xf.ang);
  an.boxMinX = an.insX;
  an.boxMinY = an.insY - static_cast<float>(th);
  an.boxMaxX = an.insX + static_cast<float>(
      std::max(static_cast<double>(txt.width), th * 4.0));
  an.boxMaxY = an.insY;

  m.annotative = ml->is_annotative != 0;
  if (!m.annotative && ownerEnt != nullptr)
    m.annotative = ImportEedMarksAnnotative(dwg, ownerEnt);
  if (ownerEnt != nullptr)
    ImportAnnotativeVisibilityFromEntityEed(ownerEnt, &m.annotativeVisibleScaleNames);
  st.cadMultileaders.push_back(std::move(m));
  st.cadMultileaderAttrs.push_back(at);
  return true;
}

static void ImportPointAsPositionMarker(AppCommandState& st, double wx, double wy, double z,
                                        const EntityAttributes& at) {
  CadPositionMarker m;
  m.x = wx - st.worldDocumentOriginX;
  m.y = wy - st.worldDocumentOriginY;
  m.z = static_cast<float>(z);
  m.label.kind = CadAnnotation::Kind::Mtext;
  m.label.insX = static_cast<float>(m.x);
  m.label.insY = static_cast<float>(m.y);
  m.label.insZ = m.z;
  m.label.text.clear();
  st.cadPositionMarkers.push_back(std::move(m));
  st.cadPositionMarkerAttrs.push_back(at);
}

void ImportObject(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj, const Xf2& xf, int depth,
                  std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions,
                  AppCommandState* blockDefCatalog);

/// REQ-320 / ADR-051 (GitHub issue #299): a `3DSOLID` entity's geometry is an ACIS record stream,
/// not lines/circles LibreDWG can hand back directly. `acis_data` is LibreDWG's already-decrypted
/// payload — SAT (v1, text) or SAB (v2+, binary), per `version` (DXF 70). This importer supports SAT
/// only (issue #301 tracks SAB); a SAB stream, or anything AcisSatParser refuses, is reported through
/// the same `NoteSkip` mechanism an unrecognized entity type already uses (REQ-201: never silent).
void ImportAcisSolid(AppCommandState& st, Dwg_Data* dwg, const Dwg_Object_Entity* ownerEnt,
                     const Dwg_Entity__3DSOLID* sol, const Xf2& xf, const EntityAttributes& at,
                     std::unordered_map<std::string, int>* skipHist) {
  if (sol->acis_empty || sol->acis_data == nullptr) {
    // GitHub issue #369 / D-2026-09-10-b: name a Civil 3D parts-catalog placeholder for what it
    // is, rather than the ambiguous "(empty)" that reads like a decode failure. The block's other
    // 2D/annotation content still imports (BLOCKIMPORT keeps it).
    NoteSkip(skipHist,
             libredwgcad_detail::DwgHasCivil3dCatalogClasses(dwg)
                 ? "3DSOLID(Civil3D parts-catalog part, no portable geometry)"
                 : "3DSOLID(empty)");
    return;
  }
  // A rotated or non-uniformly-scaled placement (a 3DSOLID reached through a rotated/scaled nested
  // INSERT) would need every surface/edge frame in the imported solid transformed consistently, not
  // just its vertices — out of scope this increment. The primary case (a block DEFINITION's own
  // 3DSOLID, imported directly by BLOCKIMPORT) always reaches here with an identity transform.
  const bool identityXf = std::fabs(xf.ox) < 1e-9 && std::fabs(xf.oy) < 1e-9 &&
                           std::fabs(xf.ang) < 1e-9 && std::fabs(xf.sx - 1.0) < 1e-9 &&
                           std::fabs(xf.sy - 1.0) < 1e-9;
  if (!identityXf) {
    NoteSkip(skipHist, "3DSOLID(rotated/scaled placement not supported)");
    return;
  }
  if (sol->version >= 2) {
    NoteSkip(skipHist, "3DSOLID(SAB binary ACIS not supported, issue #301)");
    return;
  }
  // `acis_data` is LibreDWG's decrypted buffer; the SAT-decryption cipher preserves length, so the
  // encrypted blocks' summed size is the decrypted length — read exactly that many bytes rather than
  // trusting a NUL terminator, which a corrupted or unusually-encoded file need not have.
  std::string sat;
  if (sol->num_blocks > 0 && sol->block_size != nullptr) {
    std::size_t total = 0;
    for (BITCODE_BL i = 0; i < sol->num_blocks; ++i)
      total += sol->block_size[i];
    sat.assign(reinterpret_cast<const char*>(sol->acis_data), total);
  } else {
    sat.assign(reinterpret_cast<const char*>(sol->acis_data));
  }
  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "3DSOLID");
  if (!r.ok) {
    NoteSkip(skipHist, ("3DSOLID(" + r.error + ")").c_str());
    return;
  }
  // Every other imported entity localizes against the document origin (LocalLine/LocalCircle/etc.,
  // above) — a solid's vertices and surface/edge frames need the identical shift, or it renders and
  // exports offset from every other entity in a state-plane drawing (REQ-101's Local storage
  // invariant).
  const brep::Solid localized =
      brep::Translate(r.solid, ray3d::Vec3{-st.worldDocumentOriginX, -st.worldDocumentOriginY, 0.0});
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(localized));
  EntityAttributes solidAt = at;
  if (ownerEnt != nullptr && dwg != nullptr)
    DwgImportApplyEntityMaterial(dwg, ownerEnt, &solidAt);
  st.cadSolidAttrs.push_back(solidAt);
}

[[nodiscard]] bool DwgBlockDefNameIsImportable(std::string_view name) {
  if (name.empty())
    return false;
  if (CadBlockNameIsDynamicAnonymous(name))
    return true;
  if (name[0] == '*')
    return false;
  return name != "GOSURVEY_POINT";
}

[[nodiscard]] std::string BlockHeaderDwgName(const Dwg_Data* dwg, const Dwg_Object* blkHeaderObj) {
  if (blkHeaderObj == nullptr || blkHeaderObj->fixedtype != DWG_TYPE_BLOCK_HEADER ||
      blkHeaderObj->tio.object == nullptr || blkHeaderObj->tio.object->tio.BLOCK_HEADER == nullptr)
    return {};
  return FromT(dwg, blkHeaderObj->tio.object->tio.BLOCK_HEADER->name);
}

static bool ScratchHasBlockGeometry(const AppCommandState& s) {
  return !s.userLinesFlat.empty() || !s.userCirclesCxCyZR.empty() || !s.userArcs.empty() ||
         !s.userEllipses.empty() || s.userPolylineOffsets.size() >= 2 || !s.cadAnnotations.empty() ||
         !s.cadMeshes.empty() || !s.cadSolids.empty() || !s.cadBlockRefs.empty();
}

static void CaptureScratchIntoBlockContent(const AppCommandState& st, CadBlockContent* c) {
  assert(c != nullptr);
  c->lines = st.userLinesFlat;
  c->lineAttrs = st.userLineAttrs;
  c->lineVis.assign(st.userLineAttrs.size(), "");
  c->circles = st.userCirclesCxCyZR;
  c->circleAttrs = st.userCircleAttrs;
  c->circleVis.assign(st.userCircleAttrs.size(), "");
  c->circleNormals = st.userCircleNormals;
  EnsureCircleNormals(c->circleNormals, st.userCirclesCxCyZR.size() / 4);
  c->arcs = st.userArcs;
  c->arcAttrs = st.userArcAttrs;
  c->ellipses = st.userEllipses;
  c->ellAttrs = st.userEllAttrs;
  c->polyOffsets = st.userPolylineOffsets;
  c->polyVerts = st.userPolylineVerts;
  c->polyVertsBulge = st.userPolylineVertsBulge;
  c->polyClosed = st.userPolylineClosed;
  c->polyAttrs = st.userPolylineAttrs;
  c->texts = st.cadAnnotations;
  c->textAttrs = st.cadAnnotationAttrs;
  c->meshes = st.cadMeshes;
  c->meshAttrs = st.cadMeshAttrs;
  c->solids = st.cadSolids;
  c->solidAttrs = st.cadSolidAttrs;
  for (const CadBlockRef& r : st.cadBlockRefs) {
    CadBlockNested n;
    n.defName = r.defName;
    n.xf = r.xf;
    c->nested.push_back(std::move(n));
  }
}

void ImportAttdefIntoDefinition(const Dwg_Data* dwg, const Dwg_Entity_ATTDEF* ad, CadBlockDefinition& def) {
  if (ad == nullptr)
    return;
  CadBlockAttrDef d;
  d.tag = FromT(dwg, ad->tag);
  if (d.tag.empty())
    return;
  d.prompt = FromT(dwg, ad->prompt);
  d.defaultValue = FromT(dwg, ad->default_value);
  d.localX = static_cast<float>(ad->ins_pt.x - def.baseX);
  d.localY = static_cast<float>(ad->ins_pt.y - def.baseY);
  d.localZ = static_cast<float>(ad->elevation);
  d.height = static_cast<float>(ad->height > 0.0 ? ad->height : 0.125);
  d.rotationRad = static_cast<float>(ad->rotation);
  def.attrDefs.push_back(std::move(d));
}

bool EnsureDwgBlockDefinitionImported(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* blkHeaderObj,
                                      std::unordered_map<std::string, int>* skipHist,
                                      int* degenerateExtrusions) {
  const std::string name = BlockHeaderDwgName(dwg, blkHeaderObj);
  if (!DwgBlockDefNameIsImportable(name))
    return false;
  if (CadBlockFindDef(st.blockDefs, name) >= 0)
    return true;
  if (blkHeaderObj->tio.object == nullptr || blkHeaderObj->tio.object->tio.BLOCK_HEADER == nullptr)
    return false;
  const Dwg_Object_BLOCK_HEADER* hdr = blkHeaderObj->tio.object->tio.BLOCK_HEADER;

  AppCommandState scratch;
  scratch.worldDocumentOriginX = st.worldDocumentOriginX;
  scratch.worldDocumentOriginY = st.worldDocumentOriginY;
  scratch.modelUnitsPerPlottedInch = st.modelUnitsPerPlottedInch;
  const Xf2 id{};
  for (Dwg_Object* e = get_first_owned_entity(blkHeaderObj); e != nullptr;
       e = get_next_owned_entity(blkHeaderObj, e)) {
    if (e->fixedtype == DWG_TYPE_ATTDEF && e->tio.entity != nullptr && e->tio.entity->tio.ATTDEF != nullptr)
      continue;  // collected below
    ImportObject(scratch, dwg, e, id, 1, skipHist, degenerateExtrusions, &st);
  }
  if (!ScratchHasBlockGeometry(scratch))
    return false;

  CadBlockDefinition def;
  def.name = name;
  def.dynamicAnonymous = CadBlockNameIsDynamicAnonymous(name);
  def.baseX = static_cast<float>(hdr->base_pt.x - st.worldDocumentOriginX);
  def.baseY = static_cast<float>(hdr->base_pt.y - st.worldDocumentOriginY);
  def.baseZ = static_cast<float>(hdr->base_pt.z);
  for (Dwg_Object* e = get_first_owned_entity(blkHeaderObj); e != nullptr;
       e = get_next_owned_entity(blkHeaderObj, e)) {
    if (e->fixedtype == DWG_TYPE_ATTDEF && e->tio.entity != nullptr && e->tio.entity->tio.ATTDEF != nullptr)
      ImportAttdefIntoDefinition(dwg, e->tio.entity->tio.ATTDEF, def);
  }
  CaptureScratchIntoBlockContent(scratch, &def.content);
  CadBlockBakeBasePoint(&def);
  ImportDynamicBlockDefinitionFromDwg(dwg, blkHeaderObj, def);
  st.blockDefs.push_back(std::move(def));
  return true;
}

inline constexpr const char* kGosurveyAnnotativeBlockEed = "annotative";
inline constexpr const char* kGosurveyAnnoVisScalesEed = "annoVisScales";
inline constexpr const char* kAcadAnnotativeDataEed = "AnnotativeData";
inline constexpr const char* kGosurveyCannoscaleEedTag = "CANNOSCALE";

[[nodiscard]] std::string JoinAnnotativeVisibleScaleNames(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& n : names) {
    if (n.empty())
      continue;
    if (!out.empty())
      out += ',';
    out += n;
  }
  return out;
}

void SplitAnnotativeVisibleScaleNames(const std::string& csv, std::vector<std::string>* out) {
  if (out == nullptr)
    return;
  out->clear();
  size_t i = 0;
  while (i < csv.size()) {
    const size_t j = csv.find(',', i);
    const size_t end = j == std::string::npos ? csv.size() : j;
    std::string tok = csv.substr(i, end - i);
    while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t'))
      tok.erase(tok.begin());
    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t'))
      tok.pop_back();
    if (!tok.empty())
      out->push_back(std::move(tok));
    if (j == std::string::npos)
      break;
    i = j + 1;
  }
}

[[nodiscard]] bool GosurveyEedCode0String(const Dwg_Eed_Data* data, std::string* out) {
  if (out == nullptr || data == nullptr || data->code != 0)
    return false;
  if (data->u.eed_0.is_tu != 0) {
    *out = libredwgcad_detail::DecodeDwgString(data->u.eed_0.string, true);
    return !out->empty();
  }
  const unsigned short len = data->u.eed_0.length;
  if (len == 0)
    return false;
  out->assign(reinterpret_cast<const char*>(data->u.eed_0.string), len);
  return true;
}

void ImportAnnotativeVisibilityFromEntityEed(const Dwg_Object_Entity* ent, std::vector<std::string>* out) {
  if (out == nullptr || ent == nullptr || ent->eed == nullptr || ent->num_eed == 0)
    return;
  std::vector<std::string> strings;
  strings.reserve(static_cast<size_t>(ent->num_eed));
  for (BITCODE_BL i = 0; i < ent->num_eed; ++i) {
    std::string s;
    if (GosurveyEedCode0String(ent->eed[i].data, &s))
      strings.push_back(std::move(s));
  }
  for (size_t i = 0; i + 1 < strings.size(); ++i) {
    if (strings[i] != kGosurveyAnnoVisScalesEed)
      continue;
    SplitAnnotativeVisibleScaleNames(strings[i + 1], out);
    return;
  }
}

[[nodiscard]] bool GosurveyEedMarksAnnotative(const Dwg_Object_Entity* ent) {
  if (ent == nullptr || ent->eed == nullptr || ent->num_eed == 0)
    return false;
  for (BITCODE_BL i = 0; i < ent->num_eed; ++i) {
    std::string s;
    if (!GosurveyEedCode0String(ent->eed[i].data, &s))
      continue;
    if (s == kGosurveyAnnotativeBlockEed)
      return true;
  }
  return false;
}

[[nodiscard]] bool EntityEedHasAppidHandle(const Dwg_Object_Entity* ent, BITCODE_RLL appidAbsRef) {
  if (ent == nullptr || ent->eed == nullptr || ent->num_eed == 0 || appidAbsRef == 0)
    return false;
  for (BITCODE_BL i = 0; i < ent->num_eed; ++i) {
    if (ent->eed[i].handle.value == appidAbsRef)
      return true;
  }
  return false;
}

[[nodiscard]] bool AcadAnnotativeEedMarksAnnotative(const Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  if (ent == nullptr || ent->eed == nullptr || ent->num_eed == 0)
    return false;
  BITCODE_RLL acadAppRef = 0;
  if (dwg != nullptr) {
    const BITCODE_H appid = dwg_find_tablehandle(const_cast<Dwg_Data*>(dwg), "AcadAnnotative", "APPID");
    if (appid != nullptr)
      acadAppRef = appid->absolute_ref;
  }
  for (BITCODE_BL i = 0; i < ent->num_eed; ++i) {
    std::string s;
    if (!GosurveyEedCode0String(ent->eed[i].data, &s))
      continue;
    if (s != kAcadAnnotativeDataEed)
      continue;
    if (acadAppRef == 0 || ent->eed[i].handle.value == acadAppRef)
      return true;
  }
  return false;
}

[[nodiscard]] bool ImportEedMarksAnnotative(const Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  return GosurveyEedMarksAnnotative(ent) || AcadAnnotativeEedMarksAnnotative(dwg, ent);
}

[[nodiscard]] bool GosurveyEedMarksAnnotativeBlockInsert(const Dwg_Object_Entity* ent) {
  return GosurveyEedMarksAnnotative(ent);
}

[[nodiscard]] static std::uint64_t DwgObjectHandleValue(const Dwg_Object* o) {
  return o != nullptr ? o->handle.value : 0u;
}

[[nodiscard]] static bool DwgBlockHeaderHasDynamicPurgePreventer(const Dwg_Data* dwg,
                                                                 const Dwg_Object* blkHeaderObj) {
  if (dwg == nullptr || blkHeaderObj == nullptr)
    return false;
  const std::uint64_t target = DwgObjectHandleValue(blkHeaderObj);
  if (target == 0u)
    return false;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_DYNAMICBLOCKPURGEPREVENTER || o->tio.object == nullptr ||
        o->tio.object->tio.DYNAMICBLOCKPURGEPREVENTER == nullptr)
      continue;
    const Dwg_Object_DYNAMICBLOCKPURGEPREVENTER* pp = o->tio.object->tio.DYNAMICBLOCKPURGEPREVENTER;
    if (pp->block == nullptr)
      continue;
    Dwg_Object* linked = dwg_resolve_handle_silent(const_cast<Dwg_Data*>(dwg), pp->block->absolute_ref);
    if (linked != nullptr && DwgObjectHandleValue(linked) == target)
      return true;
  }
  return false;
}

[[nodiscard]] static bool DwgInsertReferencesForeignDynamicDefinition(const Dwg_Data* dwg, const Dwg_Object* blkHeaderObj) {
  if (blkHeaderObj == nullptr)
    return false;
  const std::string name = BlockHeaderDwgName(dwg, blkHeaderObj);
  if (name.empty() || CadBlockNameIsDynamicAnonymous(name))
    return false;
  return DwgBlockHeaderHasDynamicPurgePreventer(dwg, blkHeaderObj);
}

[[nodiscard]] static std::string DwgUniqueDynamicCanonicalBlockName(const Dwg_Data* dwg) {
  if (dwg == nullptr)
    return {};
  std::vector<std::string> names;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_DYNAMICBLOCKPURGEPREVENTER || o->tio.object == nullptr ||
        o->tio.object->tio.DYNAMICBLOCKPURGEPREVENTER == nullptr)
      continue;
    const Dwg_Object_DYNAMICBLOCKPURGEPREVENTER* pp = o->tio.object->tio.DYNAMICBLOCKPURGEPREVENTER;
    if (pp->block == nullptr)
      continue;
    Dwg_Object* linked = dwg_resolve_handle_silent(const_cast<Dwg_Data*>(dwg), pp->block->absolute_ref);
    if (linked == nullptr)
      continue;
    const std::string name = BlockHeaderDwgName(dwg, linked);
    if (name.empty() || CadBlockNameIsDynamicAnonymous(name))
      continue;
    bool dup = false;
    for (const std::string& have : names) {
      if (CadBlockEqCi(have, name)) {
        dup = true;
        break;
      }
    }
    if (!dup)
      names.push_back(name);
  }
  if (names.size() == 1)
    return names[0];
  return {};
}

void CollectInsertAttributes(const Dwg_Data* dwg, const Dwg_Entity_INSERT* ins,
                             std::vector<CadBlockAttrValue>& out) {
  if (ins == nullptr || ins->attribs == nullptr || ins->num_owned == 0)
    return;
  for (BITCODE_BL i = 0; i < ins->num_owned; ++i) {
    if (ins->attribs[i] == nullptr)
      continue;
    Dwg_Object* ao = dwg_resolve_handle_silent(const_cast<Dwg_Data*>(dwg), ins->attribs[i]->absolute_ref);
    if (ao == nullptr || ao->fixedtype != DWG_TYPE_ATTRIB || ao->tio.entity == nullptr ||
        ao->tio.entity->tio.ATTRIB == nullptr)
      continue;
    const Dwg_Entity_ATTRIB* at = ao->tio.entity->tio.ATTRIB;
    const std::string tag = FromT(dwg, at->tag);
    if (tag.empty())
      continue;
    out.push_back(CadBlockAttrValue{tag, FromT(dwg, at->text_value)});
  }
}

static bool ImportNamedInsertAsBlockRef(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* ent,
                                        const EntityAttributes& at,
                                        std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions,
                                        double originSubtractX, double originSubtractY,
                                        std::vector<CadBlockRef>& outRefs, std::vector<EntityAttributes>& outAttrs,
                                        AppCommandState* blockDefCatalog) {
  if (ent == nullptr || ent->tio.INSERT == nullptr)
    return false;
  const Dwg_Entity_INSERT* ins = ent->tio.INSERT;
  if (ins->block_header == nullptr)
    return false;
  Dwg_Object* blk = dwg_resolve_handle_silent(dwg, ins->block_header->absolute_ref);
  if (blk == nullptr)
    return false;
  const std::string name = BlockHeaderDwgName(dwg, blk);
  if (!DwgBlockDefNameIsImportable(name))
    return false;
  AppCommandState& catalog = blockDefCatalog != nullptr ? *blockDefCatalog : st;
  if (!EnsureDwgBlockDefinitionImported(catalog, dwg, blk, skipHist, degenerateExtrusions))
    return false;
  if (CadBlockFindDef(catalog.blockDefs, name) < 0)
    return false;

  CadBlockRef ref;
  ref.defName = name;
  if (CadBlockNameIsDynamicAnonymous(name))
    ref.dynamicCanonicalName = DwgUniqueDynamicCanonicalBlockName(dwg);
  ref.xf.x = static_cast<float>(ins->ins_pt.x - originSubtractX);
  ref.xf.y = static_cast<float>(ins->ins_pt.y - originSubtractY);
  ref.xf.z = static_cast<float>(ins->ins_pt.z);
  ref.xf.sx = ins->scale.x != 0.0 ? static_cast<float>(ins->scale.x) : 1.f;
  ref.xf.sy = ins->scale.y != 0.0 ? static_cast<float>(ins->scale.y) : 1.f;
  ref.xf.sz = ins->scale.z != 0.0 ? static_cast<float>(ins->scale.z) : 1.f;
  ref.xf.rotZ = static_cast<float>(ins->rotation);
  ref.annotative = ImportEedMarksAnnotative(dwg, ent);
  ImportAnnotativeVisibilityFromEntityEed(ent, &ref.annotativeVisibleScaleNames);
  CollectInsertAttributes(dwg, ins, ref.attributes);
  if (!CadBlockNameIsDynamicAnonymous(name)) {
    const int di = CadBlockFindDef(catalog.blockDefs, name);
    if (di >= 0) {
      const CadBlockDefinition& bdef = catalog.blockDefs[static_cast<size_t>(di)];
      if (!bdef.parameters.empty() && ref.paramState.empty())
        ref.paramState = bdef.parameters;
    }
  }
  outRefs.push_back(std::move(ref));
  outAttrs.push_back(at);
  return true;
}

static bool ImportNestedInsertAsBlockRef(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* ent, const Xf2& xf,
                                         std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions,
                                         AppCommandState* blockDefCatalog) {
  if (ent == nullptr || ent->tio.INSERT == nullptr)
    return false;
  const Dwg_Entity_INSERT* ins = ent->tio.INSERT;
  if (ins->block_header == nullptr)
    return false;
  Dwg_Object* blk = dwg_resolve_handle_silent(dwg, ins->block_header->absolute_ref);
  if (blk == nullptr)
    return false;
  const std::string name = BlockHeaderDwgName(dwg, blk);
  if (!DwgBlockDefNameIsImportable(name))
    return false;
  AppCommandState& catalog = blockDefCatalog != nullptr ? *blockDefCatalog : st;
  if (!EnsureDwgBlockDefinitionImported(catalog, dwg, blk, skipHist, degenerateExtrusions))
    return false;
  if (CadBlockFindDef(catalog.blockDefs, name) < 0)
    return false;

  double wx = 0.0;
  double wy = 0.0;
  xf.apply(ins->ins_pt.x, ins->ins_pt.y, &wx, &wy);
  const double insSx = ins->scale.x != 0.0 ? ins->scale.x : 1.0;
  const double insSy = ins->scale.y != 0.0 ? ins->scale.y : 1.0;
  const double insSz = ins->scale.z != 0.0 ? ins->scale.z : 1.0;
  CadBlockRef ref;
  ref.defName = name;
  ref.xf.x = static_cast<float>(wx - st.worldDocumentOriginX);
  ref.xf.y = static_cast<float>(wy - st.worldDocumentOriginY);
  ref.xf.z = static_cast<float>(ins->ins_pt.z);
  ref.xf.sx = static_cast<float>(insSx * xf.sx);
  ref.xf.sy = static_cast<float>(insSy * xf.sy);
  ref.xf.sz = static_cast<float>(insSz);
  ref.xf.rotZ = static_cast<float>(ins->rotation + xf.ang);
  ref.annotative = ImportEedMarksAnnotative(dwg, ent);
  ImportAnnotativeVisibilityFromEntityEed(ent, &ref.annotativeVisibleScaleNames);
  CollectInsertAttributes(dwg, ins, ref.attributes);
  if (!CadBlockNameIsDynamicAnonymous(name)) {
    const int di = CadBlockFindDef(catalog.blockDefs, name);
    if (di >= 0) {
      const CadBlockDefinition& bdef = catalog.blockDefs[static_cast<size_t>(di)];
      if (!bdef.parameters.empty() && ref.paramState.empty())
        ref.paramState = bdef.parameters;
    }
  }
  st.cadBlockRefs.push_back(std::move(ref));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  return true;
}

bool TryImportInsertAsBlockRef(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* ent, int depth,
                               const EntityAttributes& at, std::unordered_map<std::string, int>* skipHist,
                               int* degenerateExtrusions) {
  if (depth != 0)
    return false;
  return ImportNamedInsertAsBlockRef(st, dwg, ent, at, skipHist, degenerateExtrusions, st.worldDocumentOriginX,
                                     st.worldDocumentOriginY, st.cadBlockRefs, st.cadBlockRefAttrs, nullptr);
}

void ExplodeInsert(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* ent, int depth,
                   std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions,
                   AppCommandState* blockDefCatalog) {
  if (depth > 8 || ent == nullptr || ent->tio.INSERT == nullptr)
    return;
  const Dwg_Entity_INSERT* ins = ent->tio.INSERT;
  if (ins->block_header == nullptr)
    return;
  Dwg_Object* blk = dwg_resolve_handle_silent(dwg, ins->block_header->absolute_ref);
  if (blk == nullptr)
    return;
  Xf2 child = {};
  child.ox = ins->ins_pt.x;
  child.oy = ins->ins_pt.y;
  child.ang = ins->rotation;
  child.sx = ins->scale.x != 0.0 ? ins->scale.x : 1.0;
  child.sy = ins->scale.y != 0.0 ? ins->scale.y : 1.0;
  for (Dwg_Object* e = get_first_owned_entity(blk); e != nullptr; e = get_next_owned_entity(blk, e))
    ImportObject(st, dwg, e, child, depth + 1, skipHist, degenerateExtrusions, blockDefCatalog);
}

// REQ-366, issue #607: imports a DIMENSION's anonymous "*D" block content as plain entities —
// same fallback an unsupported DIMENSION subtype (Radius/Diameter/Ordinate/2-line Angular) takes.
// Unlike ExplodeInsert, there is no INSERT transform to apply: AutoCAD bakes an anonymous
// dimension block's content at the dimension's actual WCS location (see WriteDimAnonymousBlock's
// comment on the save side), so the block's own entities are imported with the identity transform.
void ExplodeDimensionBlock(AppCommandState& st, Dwg_Data* dwg, const Dwg_DIMENSION_common* common, int depth,
                           std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions) {
  if (depth > 8 || common == nullptr || common->block == nullptr)
    return;
  Dwg_Object* blk = dwg_resolve_handle_silent(dwg, common->block->absolute_ref);
  if (blk == nullptr)
    return;
  const Xf2 identity{};
  for (Dwg_Object* e = get_first_owned_entity(blk); e != nullptr; e = get_next_owned_entity(blk, e))
    ImportObject(st, dwg, e, identity, depth + 1, skipHist, degenerateExtrusions, nullptr);
}

// REQ-366 statement 3: Aligned / Linear (rotated or orthogonal) / 3-point Angular read back into
// the matching CadAnnotation::Kind, points and measured value from the DIMENSION's OWN fields (not
// the *D block). Returns false if the geometry is degenerate (e.g. coincident extension points),
// in which case the caller falls back to importing the block's drawn geometry like any unsupported
// subtype, rather than dropping the dimension silently.
bool ImportSupportedDimension(AppCommandState& st, Dwg_Data* dwg, const Xf2& xf, Dwg_Object_Type ty,
                              const Dwg_DIMENSION_common* common, const BITCODE_3BD* xline1,
                              const BITCODE_3BD* xline2, const BITCODE_3BD* centerOrDefPt,
                              double dimRotation, const EntityAttributes& at,
                              const Dwg_Object_Entity* ownerEnt) {
  if (common == nullptr)
    return false;
  CadAnnotation a{};
  a.insZ = common->elevation;
  a.rotationRad = static_cast<float>(common->text_rotation);
  double tmx = 0.0, tmy = 0.0;
  xf.apply(common->text_midpt.x, common->text_midpt.y, &tmx, &tmy);
  a.insX = static_cast<float>(tmx - st.worldDocumentOriginX);
  a.insY = static_cast<float>(tmy - st.worldDocumentOriginY);
  if (ty == DWG_TYPE_DIMENSION_ALIGNED || ty == DWG_TYPE_DIMENSION_LINEAR) {
    if (xline1 == nullptr || xline2 == nullptr)
      return false;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    xf.apply(xline1->x, xline1->y, &x1, &y1);
    xf.apply(xline2->x, xline2->y, &x2, &y2);
    a.dimExt1X = static_cast<float>(x1 - st.worldDocumentOriginX);
    a.dimExt1Y = static_cast<float>(y1 - st.worldDocumentOriginY);
    a.dimExt2X = static_cast<float>(x2 - st.worldDocumentOriginX);
    a.dimExt2Y = static_cast<float>(y2 - st.worldDocumentOriginY);
    if (ty == DWG_TYPE_DIMENSION_ALIGNED) {
      a.kind = CadAnnotation::Kind::DimAligned;
      const double vx = a.dimExt2X - a.dimExt1X, vy = a.dimExt2Y - a.dimExt1Y;
      const double len = std::hypot(vx, vy);
      if (len < 1e-8)
        return false;
      const double n0x = -vy / len, n0y = vx / len;
      double dpx = 0, dpy = 0;
      if (centerOrDefPt != nullptr)
        xf.apply(centerOrDefPt->x, centerOrDefPt->y, &dpx, &dpy);
      dpx -= st.worldDocumentOriginX;
      dpy -= st.worldDocumentOriginY;
      const double cmx = 0.5 * (a.dimExt1X + a.dimExt2X), cmy = 0.5 * (a.dimExt1Y + a.dimExt2Y);
      a.dimSignedOffset = static_cast<float>((dpx - cmx) * n0x + (dpy - cmy) * n0y);
    } else {
      a.kind = CadAnnotation::Kind::DimLinear;
      // AutoCAD rotated-linear convention: ~pi/2 (mod pi) = vertical dim line (matches the save
      // side's `an.dimLinearVertical ? kPi*0.5 : 0.0`).
      const double rotMod = std::fmod(std::fabs(dimRotation), kPi);
      a.dimLinearVertical = rotMod > (kPi * 0.25) && rotMod < (kPi * 0.75);
      double dpx = 0, dpy = 0;
      if (centerOrDefPt != nullptr)
        xf.apply(centerOrDefPt->x, centerOrDefPt->y, &dpx, &dpy);
      dpx -= st.worldDocumentOriginX;
      dpy -= st.worldDocumentOriginY;
      const double cmx = 0.5 * (a.dimExt1X + a.dimExt2X), cmy = 0.5 * (a.dimExt1Y + a.dimExt2Y);
      a.dimSignedOffset = static_cast<float>(a.dimLinearVertical ? (dpx - cmx) : (dpy - cmy));
    }
  } else if (ty == DWG_TYPE_DIMENSION_ANG3PT) {
    if (centerOrDefPt == nullptr || xline1 == nullptr || xline2 == nullptr)
      return false;
    a.kind = CadAnnotation::Kind::DimAngular;
    double vx = 0, vy = 0;
    xf.apply(centerOrDefPt->x, centerOrDefPt->y, &vx, &vy);
    a.dimAngVertexX = static_cast<float>(vx - st.worldDocumentOriginX);
    a.dimAngVertexY = static_cast<float>(vy - st.worldDocumentOriginY);
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    xf.apply(xline1->x, xline1->y, &x1, &y1);
    xf.apply(xline2->x, xline2->y, &x2, &y2);
    a.dimExt1X = static_cast<float>(x1 - st.worldDocumentOriginX);
    a.dimExt1Y = static_cast<float>(y1 - st.worldDocumentOriginY);
    a.dimExt2X = static_cast<float>(x2 - st.worldDocumentOriginX);
    a.dimExt2Y = static_cast<float>(y2 - st.worldDocumentOriginY);
    double dpx = 0, dpy = 0;
    xf.apply(common->def_pt.x, common->def_pt.y, &dpx, &dpy);
    dpx -= st.worldDocumentOriginX;
    dpy -= st.worldDocumentOriginY;
    a.dimSignedOffset = static_cast<float>(std::hypot(dpx - a.dimAngVertexX, dpy - a.dimAngVertexY));
    if (a.dimSignedOffset < 1e-6f)
      return false;
  } else {
    return false;
  }
  const AngleDisplaySettings angle = CadAngleDisplaySettings(st);
  CadDimRefreshMeasurementText(&a, st.activeDimensionStyle.unitPrecision, angle);
  // A user-overridden DIMENSION text (DXF 1) takes priority over the geometry-derived label
  // above, matching what AutoCAD itself shows for the entity.
  if (common->user_text != nullptr) {
    const std::string userText = FromT(dwg, common->user_text);
    if (!userText.empty() && userText != "<>")  // AutoCAD's "use the measured value" placeholder
      a.text = userText;
  }
  if (ownerEnt != nullptr) {
    a.annotative = ImportEedMarksAnnotative(dwg, ownerEnt);
    ImportAnnotativeVisibilityFromEntityEed(ownerEnt, &a.annotativeVisibleScaleNames);
  }
  st.cadAnnotations.push_back(a);
  st.cadAnnotationAttrs.push_back(at);
  return true;
}

// REQ-170 / issue #608: map a decoded HATCH boundary into CadFilledRegion (solid or pattern).
bool ImportHatchEntity(AppCommandState& st, Dwg_Data* dwg, const Dwg_Entity_HATCH* h, const Xf2& xf,
                       const EntityAttributes& at, const Dwg_Object_Entity* ownerEnt) {
  if (h == nullptr || h->num_paths == 0 || h->paths == nullptr)
    return false;
  if (h->is_gradient_fill != 0)
    return false;

  CadFilledRegion region;
  const double elev = h->elevation;
  auto pushWorld = [&](double wx, double wy) {
    double ox = 0, oy = 0;
    xf.apply(wx, wy, &ox, &oy);
    region.vertsXyz.push_back(ox - st.worldDocumentOriginX);
    region.vertsXyz.push_back(oy - st.worldDocumentOriginY);
    region.vertsXyz.push_back(elev);
  };
  auto beginLoop = [&]() { region.loopStart.push_back(static_cast<int>(region.vertsXyz.size() / 3)); };
  auto endLoop = [&]() {
    if (region.loopStart.empty())
      return;
    const int start = region.loopStart.back();
    if (static_cast<int>(region.vertsXyz.size() / 3) - start < 3) {
      region.vertsXyz.resize(static_cast<size_t>(start) * 3);
      region.loopStart.pop_back();
    }
  };

  for (BITCODE_BL pi = 0; pi < h->num_paths; ++pi) {
    const Dwg_HATCH_Path& path = h->paths[pi];
    beginLoop();
    if ((path.flag & 2) != 0) {
      if (path.polyline_paths == nullptr || path.num_segs_or_paths < 3) {
        endLoop();
        continue;
      }
      for (BITCODE_BL vi = 0; vi < path.num_segs_or_paths; ++vi)
        pushWorld(path.polyline_paths[vi].point.x, path.polyline_paths[vi].point.y);
    } else if (path.segs != nullptr) {
      for (BITCODE_BL si = 0; si < path.num_segs_or_paths; ++si) {
        const Dwg_HATCH_PathSeg& seg = path.segs[si];
        if (seg.curve_type == 1) {
          pushWorld(seg.first_endpoint.x, seg.first_endpoint.y);
        } else if (seg.curve_type == 2 && seg.radius > 1e-9) {
          double sweep = seg.end_angle - seg.start_angle;
          if (seg.is_ccw) {
            while (sweep < 0)
              sweep += 2.0 * kPi;
          } else {
            while (sweep > 0)
              sweep -= 2.0 * kPi;
          }
          if (std::fabs(sweep) < 1e-6)
            sweep = seg.is_ccw ? 2.0 * kPi : -2.0 * kPi;
          constexpr int nseg = 24;
          for (int s = 0; s < nseg; ++s) {
            const double u =
                seg.start_angle + sweep * (static_cast<double>(s) / static_cast<double>(nseg));
            pushWorld(seg.center.x + seg.radius * std::cos(u), seg.center.y + seg.radius * std::sin(u));
          }
        }
      }
    }
    endLoop();
  }

  if (region.loopStart.empty() || region.vertsXyz.size() < 9)
    return false;

  if (h->is_solid_fill != 0) {
    region.patternName.clear();
  } else {
    region.patternName = FromT(dwg, h->name);
    if (region.patternName.empty())
      region.patternName = "ANSI31";
    region.patternAngleDeg = static_cast<float>(h->angle * (180.0 / kPi));
    region.patternScale = h->scale_spacing > 0.0 ? static_cast<float>(h->scale_spacing) : 1.f;
  }
  if (dwg->header.version >= R_2018 && h->paths != nullptr) {
    for (BITCODE_BL pi = 0; pi < h->num_paths; ++pi) {
      if ((h->paths[pi].flag & 0x200) != 0) {
        region.annotative = true;
        break;
      }
    }
  }
  if (!region.annotative && ownerEnt != nullptr)
    region.annotative = ImportEedMarksAnnotative(dwg, ownerEnt);
  if (ownerEnt != nullptr)
    ImportAnnotativeVisibilityFromEntityEed(ownerEnt, &region.annotativeVisibleScaleNames);

  st.cadFilledRegions.push_back(std::move(region));
  st.cadFilledRegionAttrs.push_back(at);
  return true;
}

void ImportObject(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj, const Xf2& xf, int depth,
                  std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions,
                  AppCommandState* blockDefCatalog) {
  if (obj == nullptr || obj->supertype != DWG_SUPERTYPE_ENTITY || obj->tio.entity == nullptr)
    return;
  Dwg_Object_Entity* ent = obj->tio.entity;
  if (ent->entmode == 1)
    return;
  const EntityAttributes at = AttrFromEnt(dwg, ent);
  const Dwg_Object_Type ty = obj->fixedtype;

  if (ty == DWG_TYPE_LINE && ent->tio.LINE != nullptr) {
    const Dwg_Entity_LINE* e = ent->tio.LINE;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    xf.apply(e->start.x, e->start.y, &x0, &y0);
    xf.apply(e->end.x, e->end.y, &x1, &y1);
    LocalLine(st, x0, y0, e->start.z, x1, y1, e->end.z, at);
    return;
  }
  if (ty == DWG_TYPE_CIRCLE && ent->tio.CIRCLE != nullptr) {
    const Dwg_Entity_CIRCLE* e = ent->tio.CIRCLE;
    // REQ-312 / GitHub issue #435: read extrusion, convert OCS centre to world.
    const double ox = e->extrusion.x;
    const double oy = e->extrusion.y;
    const double oz = e->extrusion.z;
    double ex = ox, ey = oy, ez = oz;
    RotateExtrusionByXf(xf, &ex, &ey);
    if (DwgExtrusionIsFlat(ex, ey, ez)) {
      double cx = 0, cy = 0;
      xf.apply(e->center.x, e->center.y, &cx, &cy);
      const double sc = std::max(std::fabs(xf.sx), std::fabs(xf.sy));
      LocalCircle(st, cx, cy, e->radius * sc, e->center.z, at, ex, ey, ez);
      return;
    }
    ray3d::Vec3 w{};
    if (!DwgOcsToWorld(e->center.x, e->center.y, e->center.z, ox, oy, oz, &w)) {
      NoteSkip(skipHist, "CIRCLE(degenerate extrusion)");
      if (degenerateExtrusions)
        ++(*degenerateExtrusions);
      return;
    }
    // Validate rotated extrusion is still non-degenerate (rotation preserves length, but guard).
    ucs::Ucs tmp;
    if (!ucs::FromNormal({0, 0, 0}, {ex, ey, ez}, &tmp)) {
      NoteSkip(skipHist, "CIRCLE(degenerate extrusion)");
      if (degenerateExtrusions)
        ++(*degenerateExtrusions);
      return;
    }
    double cx = 0, cy = 0;
    xf.apply(w.x, w.y, &cx, &cy);
    const double sc = std::max(std::fabs(xf.sx), std::fabs(xf.sy));
    LocalCircle(st, cx, cy, e->radius * sc, w.z, at, ex, ey, ez);
    return;
  }
  if (ty == DWG_TYPE_ARC && ent->tio.ARC != nullptr) {
    const Dwg_Entity_ARC* e = ent->tio.ARC;
    const double ox = e->extrusion.x;
    const double oy = e->extrusion.y;
    const double oz = e->extrusion.z;
    double ex = ox, ey = oy, ez = oz;
    RotateExtrusionByXf(xf, &ex, &ey);
    if (DwgExtrusionIsFlat(ex, ey, ez)) {
      double cx = 0, cy = 0;
      xf.apply(e->center.x, e->center.y, &cx, &cy);
      const double sc = std::max(std::fabs(xf.sx), std::fabs(xf.sy));
      LocalArc(st, cx, cy, e->radius * sc, e->start_angle + xf.ang, e->end_angle + xf.ang, e->center.z, at,
               ex, ey, ez);
      return;
    }
    ray3d::Vec3 w{};
    if (!DwgOcsToWorld(e->center.x, e->center.y, e->center.z, ox, oy, oz, &w)) {
      NoteSkip(skipHist, "ARC(degenerate extrusion)");
      if (degenerateExtrusions)
        ++(*degenerateExtrusions);
      return;
    }
    ucs::Ucs tmp;
    if (!ucs::FromNormal({0, 0, 0}, {ex, ey, ez}, &tmp)) {
      NoteSkip(skipHist, "ARC(degenerate extrusion)");
      if (degenerateExtrusions)
        ++(*degenerateExtrusions);
      return;
    }
    double cx = 0, cy = 0;
    xf.apply(w.x, w.y, &cx, &cy);
    const double sc = std::max(std::fabs(xf.sx), std::fabs(xf.sy));
    // Angles need no adjustment — OCS shares frame axes (issue #435 Expected).
    LocalArc(st, cx, cy, e->radius * sc, e->start_angle + xf.ang, e->end_angle + xf.ang, w.z, at, ex, ey, ez);
    return;
  }
  if (ty == DWG_TYPE_ELLIPSE && ent->tio.ELLIPSE != nullptr) {
    const Dwg_Entity_ELLIPSE* e = ent->tio.ELLIPSE;
    double span = e->end_angle - e->start_angle;
    while (span < 0.0)
      span += 2.0 * kPi;
    const bool full = span < 1e-9 || std::fabs(span - 2.0 * kPi) < 1e-6;
    if (!full) {
      if (ImportTrimmedEllipseAsPolyline(st, e, xf, at))
        return;
      NoteSkip(skipHist, "ELLIPSE(trimmed)");
      return;
    }
    double cx = 0, cy = 0;
    xf.apply(e->center.x, e->center.y, &cx, &cy);
    CadEllipse el{};
    el.cx = cx - st.worldDocumentOriginX;
    el.cy = cy - st.worldDocumentOriginY;
    el.majVx = e->sm_axis.x * xf.sx;
    el.majVy = e->sm_axis.y * xf.sy;
    el.ratio = e->axis_ratio;
    el.z = e->center.z;
    st.userEllipses.push_back(el);
    st.userEllAttrs.push_back(at);
    return;
  }
  if (ty == DWG_TYPE_LWPOLYLINE && ent->tio.LWPOLYLINE != nullptr) {
    const Dwg_Entity_LWPOLYLINE* e = ent->tio.LWPOLYLINE;
    std::vector<double> xyz;
    xyz.reserve(static_cast<size_t>(e->num_points) * 3);
    for (BITCODE_BL i = 0; i < e->num_points; ++i) {
      double x = 0, y = 0;
      xf.apply(e->points[i].x, e->points[i].y, &x, &y);
      xyz.push_back(x);
      xyz.push_back(y);
      xyz.push_back(e->elevation);
    }
    const bool closed = (e->flag & 512) != 0 || (e->flag & 1) != 0;
    // REQ-316 / ADR-047 (DWG side, REQ-325 / ADR-053 increment 4): group-42-equivalent bulges, one
    // per vertex, present whenever `bulges` is non-null — LibreDWG's own `num_bulges` flag bit (16).
    std::vector<double> bulge;
    if (e->bulges != nullptr && e->num_bulges > 0) {
      bulge.reserve(e->num_bulges);
      for (BITCODE_BL i = 0; i < e->num_bulges; ++i)
        bulge.push_back(e->bulges[i]);
    }
    LocalPolyline(st, xyz, closed, at, bulge.empty() ? nullptr : &bulge);
    return;
  }
  if ((ty == DWG_TYPE_POLYLINE_2D || ty == DWG_TYPE_POLYLINE_3D)) {
    std::vector<double> xyz;
    bool closed = false;
    for (Dwg_Object* v = get_first_owned_entity(obj); v != nullptr; v = get_next_owned_entity(obj, v)) {
      if (v->fixedtype == DWG_TYPE_VERTEX_2D && v->tio.entity != nullptr && v->tio.entity->tio.VERTEX_2D != nullptr) {
        const Dwg_Entity_VERTEX_2D* p = v->tio.entity->tio.VERTEX_2D;
        if ((p->flag & 16) != 0)
          continue;
        double x = 0, y = 0;
        xf.apply(p->point.x, p->point.y, &x, &y);
        xyz.push_back(x);
        xyz.push_back(y);
        xyz.push_back(p->point.z);
      } else if (v->fixedtype == DWG_TYPE_VERTEX_3D && v->tio.entity != nullptr &&
                 v->tio.entity->tio.VERTEX_3D != nullptr) {
        const Dwg_Entity_VERTEX_3D* p = v->tio.entity->tio.VERTEX_3D;
        double x = 0, y = 0;
        xf.apply(p->point.x, p->point.y, &x, &y);
        xyz.push_back(x);
        xyz.push_back(y);
        xyz.push_back(p->point.z);
      }
    }
    if (ty == DWG_TYPE_POLYLINE_2D && ent->tio.POLYLINE_2D != nullptr)
      closed = (ent->tio.POLYLINE_2D->flag & 1) != 0;
    LocalPolyline(st, xyz, closed, at);
    return;
  }
  if (ty == DWG_TYPE_TEXT && ent->tio.TEXT != nullptr) {
    const Dwg_Entity_TEXT* e = ent->tio.TEXT;
    double x = 0, y = 0;
    xf.apply(e->ins_pt.x, e->ins_pt.y, &x, &y);
    const bool annotative = ImportEedMarksAnnotative(dwg, ent);
    LocalText(st, x, y, e->elevation, e->height, e->rotation + xf.ang, FromT(dwg, e->text_value),
              CadAnnotation::Kind::Text, at, annotative, ent);
    return;
  }
  if (ty == DWG_TYPE_MTEXT && ent->tio.MTEXT != nullptr) {
    const Dwg_Entity_MTEXT* e = ent->tio.MTEXT;
    double x = 0, y = 0;
    xf.apply(e->ins_pt.x, e->ins_pt.y, &x, &y);
    const double rot = std::atan2(e->x_axis_dir.y, e->x_axis_dir.x);
    const bool annotative =
        ImportEedMarksAnnotative(dwg, ent) ||
        (dwg->header.version >= R_2018 && e->is_not_annotative == 0);
    LocalText(st, x, y, e->ins_pt.z, e->text_height, rot + xf.ang, FromT(dwg, e->text),
              CadAnnotation::Kind::Mtext, at, annotative, ent);
    return;
  }
  if (ty == DWG_TYPE_SPLINE && ent->tio.SPLINE != nullptr) {
    if (ImportSplineAsPolyline(st, ent->tio.SPLINE, xf, at))
      return;
    NoteSkip(skipHist, "SPLINE(degenerate or unsupported)");
    return;
  }
  if (ty == DWG_TYPE_LEADER && ent->tio.LEADER != nullptr) {
    if (ImportLeaderEntity(st, dwg, ent->tio.LEADER, xf, at))
      return;
    NoteSkip(skipHist, "LEADER(degenerate or unsupported)");
    return;
  }
  if (ty == DWG_TYPE_MULTILEADER && ent->tio.MULTILEADER != nullptr) {
    const Dwg_Entity_MULTILEADER* ml = ent->tio.MULTILEADER;
    if (ml->ctx.has_content_blk && !ml->ctx.has_content_txt) {
      NoteSkip(skipHist, "MULTILEADER(block content, issue #619)");
      return;
    }
    if (ImportMultileaderEntity(st, dwg, ml, xf, at, ent))
      return;
    NoteSkip(skipHist, "MULTILEADER(unsupported layout, issue #619)");
    return;
  }
  if (ty == DWG_TYPE__3DFACE && ent->tio._3DFACE != nullptr) {
    if (Import3DFaceAsMesh(st, dwg, ent, ent->tio._3DFACE, xf, at))
      return;
    NoteSkip(skipHist, "3DFACE(degenerate)");
    return;
  }
  if (ty == DWG_TYPE_SOLID && ent->tio.SOLID != nullptr) {
    const Dwg_Entity_SOLID* e = ent->tio.SOLID;
    if (Import2DSolidOrTrace(st, e->corner1, e->corner2, e->corner3, e->corner4, e->elevation, xf, at))
      return;
    NoteSkip(skipHist, "SOLID(degenerate)");
    return;
  }
  if (ty == DWG_TYPE_TRACE && ent->tio.TRACE != nullptr) {
    const Dwg_Entity_TRACE* e = ent->tio.TRACE;
    if (Import2DSolidOrTrace(st, e->corner1, e->corner2, e->corner3, e->corner4, e->elevation, xf, at))
      return;
    NoteSkip(skipHist, "TRACE(degenerate)");
    return;
  }
  if (ty == DWG_TYPE_POLYLINE_MESH && ent->tio.POLYLINE_MESH != nullptr) {
    if (ImportPolylineMesh(st, dwg, obj, ent->tio.POLYLINE_MESH, xf, at))
      return;
    NoteSkip(skipHist, "POLYLINE_MESH(degenerate or unsupported)");
    return;
  }
  if (ty == DWG_TYPE_POLYLINE_PFACE && ent->tio.POLYLINE_PFACE != nullptr) {
    if (ImportPolylinePFace(st, dwg, obj, ent->tio.POLYLINE_PFACE, xf, at))
      return;
    NoteSkip(skipHist, "POLYLINE_PFACE(degenerate or unsupported)");
    return;
  }
  if (ty == DWG_TYPE_IMAGE || ty == DWG_TYPE_WIPEOUT || ty == DWG_TYPE_PDFUNDERLAY ||
      ty == DWG_TYPE_DWFUNDERLAY || ty == DWG_TYPE_DGNUNDERLAY) {
    NoteSkip(skipHist, "IMAGE/underlay(reference not imported)");
    return;
  }
  if (ty == DWG_TYPE_POINT && ent->tio.POINT != nullptr) {
    const Dwg_Entity_POINT* e = ent->tio.POINT;
    double px = 0.0, py = 0.0;
    xf.apply(e->x, e->y, &px, &py);
    ImportPointAsPositionMarker(st, px, py, e->z, at);
    return;
  }
  if (ty == DWG_TYPE_HATCH && ent->tio.HATCH != nullptr) {
    const Dwg_Entity_HATCH* h = ent->tio.HATCH;
    if (h->is_gradient_fill != 0) {
      NoteSkip(skipHist, "HATCH(gradient fill not imported yet, issue #608)");
      return;
    }
    if (ImportHatchEntity(st, dwg, h, xf, at, ent))
      return;
    NoteSkip(skipHist, "HATCH(degenerate or unsupported boundary)");
    return;
  }
  if (ty == DWG_TYPE_TABLE && ent->tio.TABLE != nullptr) {
    const Dwg_Entity_TABLE* tab = ent->tio.TABLE;
    if (tab->num_cols >= 1 && tab->num_rows >= 1 && tab->cells != nullptr) {
      CadTable table;
      table.cols = static_cast<int>(tab->num_cols);
      double ix = tab->ins_pt.x, iy = tab->ins_pt.y;
      xf.apply(ix, iy, &ix, &iy);
      table.insX = static_cast<float>(ix - st.worldDocumentOriginX);
      table.insY = static_cast<float>(iy - st.worldDocumentOriginY);
      table.insZ = static_cast<float>(tab->ins_pt.z);
      table.rotationRad = static_cast<float>(tab->rotation);
      if (tab->col_widths != nullptr && tab->num_cols > 0) {
        double sum = 0.0;
        for (BITCODE_BL c = 0; c < tab->num_cols; ++c)
          sum += tab->col_widths[c];
        table.width = static_cast<float>(std::max(sum, 1.0));
      }
      if (tab->row_heights != nullptr && tab->num_rows > 0) {
        double sum = 0.0;
        for (BITCODE_BL r = 0; r < tab->num_rows; ++r)
          sum += tab->row_heights[r];
        table.height = static_cast<float>(std::max(sum, 1.0));
      }
      const unsigned long nCells = tab->num_cells;
      table.cells.reserve(static_cast<size_t>(nCells));
      for (unsigned long ci = 0; ci < nCells; ++ci)
        table.cells.push_back(FromT(dwg, tab->cells[ci].text_value));
      st.cadTables.push_back(std::move(table));
      st.cadTableAttrs.push_back(at);
      return;
    }
    NoteSkip(skipHist, "TABLE(degenerate)");
    return;
  }
  if (ty == DWG_TYPE_INSERT) {
    if (ent->tio.INSERT != nullptr && ent->tio.INSERT->block_header != nullptr) {
      Dwg_Object* insBlk = dwg_resolve_handle_silent(dwg, ent->tio.INSERT->block_header->absolute_ref);
      if (DwgInsertReferencesForeignDynamicDefinition(dwg, insBlk)) {
        AppCommandState& catalog = blockDefCatalog != nullptr ? *blockDefCatalog : st;
        bool canEval = false;
        if (insBlk != nullptr &&
            EnsureDwgBlockDefinitionImported(catalog, dwg, insBlk, skipHist, degenerateExtrusions)) {
          const std::string bname = BlockHeaderDwgName(dwg, insBlk);
          const int di = CadBlockFindDef(catalog.blockDefs, bname);
          canEval = di >= 0 && !catalog.blockDefs[static_cast<size_t>(di)].parameters.empty();
        }
        if (!canEval) {
          NoteSkip(skipHist, "INSERT(dynamic block definition; expected *U instance)");
          return;
        }
      }
    }
    if (depth == 0) {
      if (TryImportInsertAsBlockRef(st, dwg, ent, depth, at, skipHist, degenerateExtrusions))
        return;
    } else if (ImportNestedInsertAsBlockRef(st, dwg, ent, xf, skipHist, degenerateExtrusions, blockDefCatalog)) {
      return;
    }
    ExplodeInsert(st, dwg, ent, depth, skipHist, degenerateExtrusions, blockDefCatalog);
    return;
  }
  // REQ-366, issue #607: Aligned / Linear / 3-point Angular DIMENSION entities read back into
  // GoSurvey's own dimension kind (statement 3). Any other subtype falls through to the branch
  // below, which imports the anonymous *D block's drawn geometry instead.
  if ((ty == DWG_TYPE_DIMENSION_ALIGNED || ty == DWG_TYPE_DIMENSION_LINEAR ||
       ty == DWG_TYPE_DIMENSION_ANG3PT) &&
      ent->tio.DIMENSION_common != nullptr) {
    const Dwg_DIMENSION_common* common = ent->tio.DIMENSION_common;
    const BITCODE_3BD* xline1 = nullptr;
    const BITCODE_3BD* xline2 = nullptr;
    const BITCODE_3BD* centerOrDef = nullptr;
    double rot = 0.0;
    if (ty == DWG_TYPE_DIMENSION_ALIGNED && ent->tio.DIMENSION_ALIGNED != nullptr) {
      xline1 = &ent->tio.DIMENSION_ALIGNED->xline1_pt;
      xline2 = &ent->tio.DIMENSION_ALIGNED->xline2_pt;
      centerOrDef = &common->def_pt;
    } else if (ty == DWG_TYPE_DIMENSION_LINEAR && ent->tio.DIMENSION_LINEAR != nullptr) {
      xline1 = &ent->tio.DIMENSION_LINEAR->xline1_pt;
      xline2 = &ent->tio.DIMENSION_LINEAR->xline2_pt;
      centerOrDef = &common->def_pt;
      rot = ent->tio.DIMENSION_LINEAR->dim_rotation;
    } else if (ty == DWG_TYPE_DIMENSION_ANG3PT && ent->tio.DIMENSION_ANG3PT != nullptr) {
      xline1 = &ent->tio.DIMENSION_ANG3PT->xline1_pt;
      xline2 = &ent->tio.DIMENSION_ANG3PT->xline2_pt;
      centerOrDef = &ent->tio.DIMENSION_ANG3PT->center_pt;
    }
    if (!ImportSupportedDimension(st, dwg, xf, ty, common, xline1, xline2, centerOrDef, rot, at, ent))
      ExplodeDimensionBlock(st, dwg, common, depth, skipHist, degenerateExtrusions);
    return;
  }
  if ((ty == DWG_TYPE_DIMENSION_RADIUS || ty == DWG_TYPE_DIMENSION_DIAMETER ||
       ty == DWG_TYPE_DIMENSION_ORDINATE || ty == DWG_TYPE_DIMENSION_ANG2LN) &&
      ent->tio.DIMENSION_common != nullptr) {
    // REQ-366 statement 3: not mapped to a GoSurvey dimension kind — the *D block's drawn geometry
    // is kept as plain entities, and the subtype is named ONCE (aggregated via NoteSkip/skipHist,
    // same mechanism every other "skipped N of kind X" case in this file uses) rather than once per
    // instance.
    const char* subtype = ty == DWG_TYPE_DIMENSION_RADIUS     ? "DIMENSION(Radius, unsupported subtype)"
                          : ty == DWG_TYPE_DIMENSION_DIAMETER  ? "DIMENSION(Diameter, unsupported subtype)"
                          : ty == DWG_TYPE_DIMENSION_ORDINATE  ? "DIMENSION(Ordinate, unsupported subtype)"
                                                                : "DIMENSION(2-line Angular, unsupported subtype)";
    NoteSkip(skipHist, subtype);
    ExplodeDimensionBlock(st, dwg, ent->tio.DIMENSION_common, depth, skipHist, degenerateExtrusions);
    return;
  }
  if (ty == DWG_TYPE__3DSOLID && ent->tio._3DSOLID != nullptr) {
    ImportAcisSolid(st, dwg, ent, ent->tio._3DSOLID, xf, at, skipHist);
    return;
  }
  if (ty == DWG_TYPE_SEQEND || ty == DWG_TYPE_VERTEX_2D || ty == DWG_TYPE_VERTEX_3D || ty == DWG_TYPE_ENDBLK)
    return;
  NoteSkip(skipHist, obj->dxfname != nullptr ? obj->dxfname : "UNKNOWN");
}

// Resolve a layer's linetype handle (DXF 6) to its LTYPE table name. CONTINUOUS / ByLayer / an
// unresolved handle all collapse to the canonical "Continuous".
std::string LayerLinetypeName(Dwg_Data* dwg, const Dwg_Object_LAYER* ly) {
  if (dwg == nullptr || ly == nullptr || ly->ltype == nullptr)
    return "Continuous";
  Dwg_Object* o = dwg_resolve_handle_silent(dwg, ly->ltype->absolute_ref);
  if (o == nullptr || o->fixedtype != DWG_TYPE_LTYPE || o->tio.object == nullptr ||
      o->tio.object->tio.LTYPE == nullptr)
    return "Continuous";
  std::string n = FromT(dwg, o->tio.object->tio.LTYPE->name);
  std::string lower = n;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (n.empty() || lower == "continuous" || lower == "bylayer")
    return "Continuous";
  return n;
}

void ImportLayers(AppCommandState& st, Dwg_Data* dwg, std::vector<std::string>& log) {
  st.drawingLayerTable = DefaultDrawingLayerTable();
  int imported = 0;
  int undecoded = 0;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_LAYER || o->tio.object == nullptr || o->tio.object->tio.LAYER == nullptr)
      continue;
    const Dwg_Object_LAYER* ly = o->tio.object->tio.LAYER;
    const std::string name = FromT(dwg, ly->name);
    if (name == "0")
      continue;
    if (name.empty()) {
      ++undecoded;
      continue;
    }
    CadLayerRow row{};
    row.name = name;
    row.on = ly->off == 0;
    row.frozen = ly->frozen != 0;
    row.locked = ly->locked != 0;
    row.color = ColorStorage(ly->color);
    row.linetype = LayerLinetypeName(dwg, ly);
    row.lineweightMm = LineweightMmFromDwgIndex(ly->linewt);
    const float layerTr = Transparency01FromEntityColor(ly->color);
    if (layerTr >= 0.f)
      row.transparency = layerTr;
    st.drawingLayerTable.push_back(row);
    ++imported;
  }
  log.push_back("DWG import — " + std::to_string(imported) + " layer(s).");
  if (undecoded > 0)
    log.push_back("  skipped " + std::to_string(undecoded) + " layer(s) whose name could not be decoded");
}

void ImportStyles(AppCommandState& st, Dwg_Data* dwg) {
  TextStyles::EnsureStandard(st.textStyles);
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_STYLE || o->tio.object == nullptr || o->tio.object->tio.STYLE == nullptr)
      continue;
    const Dwg_Object_STYLE* s = o->tio.object->tio.STYLE;
    const std::string name = FromT(dwg, s->name);
    if (name.empty())
      continue;
    if (TextStyles::Find(st.textStyles, name) != nullptr)
      continue;
    TextStyle ts;
    ts.name = name;
    ts.fontFamily = FromT(dwg, s->font_file);
    st.textStyles.push_back(ts);
  }
}

bool LoadDwgData(const char* pathUtf8, bool asDxf, Dwg_Data* dwg, std::vector<std::string>& log) {
  std::memset(dwg, 0, sizeof(*dwg));
  const int err = asDxf ? dxf_read_file(pathUtf8, dwg) : dwg_read_file(pathUtf8, dwg);
  if (err >= DWG_ERR_CRITICAL) {
    log.push_back(std::string(asDxf ? "DXF" : "DWG") + " import — LibreDWG could not decode the file (0x" +
                  [&]() {
                    char b[16];
                    std::snprintf(b, sizeof(b), "%x", err);
                    return std::string(b);
                  }() +
                  ").");
    dwg_free(dwg);
    return false;
  }
  return true;
}

Dwg_Object_BLOCK_HEADER* ModelHeader(Dwg_Data* dwg) {
  Dwg_Object* m = dwg_model_space_object(dwg);
  if (m == nullptr || m->tio.object == nullptr)
    return nullptr;
  return m->tio.object->tio.BLOCK_HEADER;
}

Dwg_Version_Type LibreDwgVersionFromExport(DwgSaveVersion version) {
  switch (version) {
  case DwgSaveVersion::R2018:
    return R_2018;
  case DwgSaveVersion::R2013:
    return R_2013;
  case DwgSaveVersion::R2010:
    return R_2010;
  case DwgSaveVersion::R2004:
    return R_2004;
  case DwgSaveVersion::R2000:
  default:
    return R_2000;
  }
}

// REQ-170, issue #615: R2004+ writes 24-bit true colour; R2000 keeps nearest ACI (no rgb field).
void SetCmcFromStorage(BITCODE_CMC* cmc, const std::string& storage, bool useTrueColor) {
  if (cmc == nullptr)
    return;
  if (storage == "ByBlock") {
    cmc->index = 0;
    cmc->method = DWG_COLOR_METHOD_BYBLOCK;
    cmc->rgb = 0;
    return;
  }
  if (storage.empty() || storage == "ByLayer") {
    cmc->index = 256;
    cmc->method = DWG_COLOR_METHOD_BYLAYER;
    cmc->rgb = 0;
    return;
  }
  uint32_t rgb = 0;
  if (!DxfColorStringToRgbPacked(storage, &rgb)) {
    cmc->index = 7;
    cmc->method = DWG_COLOR_METHOD_ACI;
    cmc->rgb = 0;
    return;
  }
  const int aci = DxfNearestAciFromRgbPacked(rgb);
  const bool exactPalette = (DxfRgbPackedFromAci(aci) & 0xFFFFFFu) == (rgb & 0xFFFFFFu);
  if (useTrueColor && !exactPalette) {
    cmc->method = DWG_COLOR_METHOD_TRUECOLOR;
    cmc->rgb = 0xC3000000u | (rgb & 0xFFFFFFu);
    cmc->index = static_cast<BITCODE_BSd>(aci);
  } else {
    cmc->index = static_cast<BITCODE_BSd>(aci);
    cmc->method = DWG_COLOR_METHOD_ACI;
    cmc->rgb = 0;
  }
}

// Entity colour on R2004+ uses ENC (flag 0x80 + 24-bit rgb), not table CMC packing.
void SetEntityColorFromStorage(Dwg_Color* color, const std::string& storage, bool useTrueColor) {
  if (color == nullptr)
    return;
  color->flag = 0;
  if (storage == "ByBlock") {
    color->index = 0;
    color->method = DWG_COLOR_METHOD_BYBLOCK;
    color->rgb = 0;
    return;
  }
  if (storage.empty() || storage == "ByLayer") {
    color->index = 256;
    color->method = DWG_COLOR_METHOD_BYLAYER;
    color->rgb = 0;
    return;
  }
  uint32_t rgb = 0;
  if (!DxfColorStringToRgbPacked(storage, &rgb)) {
    color->index = 7;
    color->method = DWG_COLOR_METHOD_ACI;
    color->rgb = 0;
    return;
  }
  const int aci = DxfNearestAciFromRgbPacked(rgb);
  const bool exactPalette = (DxfRgbPackedFromAci(aci) & 0xFFFFFFu) == (rgb & 0xFFFFFFu);
  if (useTrueColor && !exactPalette) {
    color->method = DWG_COLOR_METHOD_TRUECOLOR;
    color->rgb = static_cast<BITCODE_BL>(rgb & 0xFFFFFFu);
    color->index = static_cast<BITCODE_BSd>(aci);
    color->flag = 0x80;
  } else {
    color->index = static_cast<BITCODE_BSd>(aci);
    color->method = DWG_COLOR_METHOD_ACI;
    color->rgb = 0;
  }
}

void ApplyLayerTableColor(Dwg_Object_LAYER* ly, const std::string& colorStr, bool on, bool useTrueColor,
                          float layerTransparency01) {
  if (ly == nullptr)
    return;
  uint32_t rgb = 0;
  const bool hasRgb = DxfColorStringToRgbPacked(colorStr, &rgb);
  const int aci = hasRgb ? DxfNearestAciFromRgbPacked(rgb) : 7;
  const bool exactPalette = hasRgb && ((DxfRgbPackedFromAci(aci) & 0xFFFFFFu) == (rgb & 0xFFFFFFu));
  const bool needsTrueColor =
      useTrueColor && hasRgb && (!exactPalette || layerTransparency01 > 1.e-5f);
  if (needsTrueColor) {
    ly->color.method = DWG_COLOR_METHOD_TRUECOLOR;
    ly->color.rgb = 0xC3000000u | (rgb & 0xFFFFFFu);
    ly->color.index = static_cast<BITCODE_BSd>(aci);
  } else {
    ly->color.index = static_cast<BITCODE_BSd>(on ? aci : -aci);
    ly->color.method = DWG_COLOR_METHOD_ACI;
    ly->color.rgb = 0;
  }
  if (useTrueColor && layerTransparency01 > 1.e-5f)
    ApplyEntityEncTransparency(&ly->color, layerTransparency01);
}

// Builds the DWG LAYER and LTYPE tables from the GoSurvey layer table and wires each exported
// entity to its layer / colour / linetype (issue #140 / DEBT-151-b — the DWG writer previously
// emitted geometry only, so a saved drawing lost every layer).
struct TableWriter {
  Dwg_Data* dwg = nullptr;
  bool useTrueColor = false;  // R2004+ entity ENC features (#615, #620)
  const AppCommandState* layerState = nullptr;
  // Store LibreDWG object indices, not Dwg_Object* — dwg_add_* can reallocate dwg->object and
  // invalidate raw pointers cached from an earlier BuildLayerTable / EnsureLtype call.
  std::unordered_map<std::string, BITCODE_BL> layers;  // lower(name) -> parent objid
  std::unordered_map<std::string, BITCODE_BL> ltypes;  // lower(name) -> parent objid
  std::unordered_map<std::string, BITCODE_BL> styles;  // lower(name) -> parent objid

  static bool IsPlainLinetype(const std::string& n) {
    const std::string l = LowerAscii(n);
    return l.empty() || l == "continuous" || l == "bylayer" || l == "byblock";
  }

  Dwg_Object* ObjectAt(BITCODE_BL objid) const {
    if (dwg == nullptr || objid < 0 || static_cast<BITCODE_BL>(objid) >= dwg->num_objects)
      return nullptr;
    return &dwg->object[objid];
  }

  Dwg_Object* EnsureLtype(const std::string& name) {
    if (IsPlainLinetype(name))
      return nullptr;
    const std::string key = LowerAscii(name);
    auto it = ltypes.find(key);
    if (it != ltypes.end())
      return ObjectAt(it->second);
    Dwg_Object_LTYPE* lt = dwg_add_LTYPE(dwg, name.c_str());
    if (lt == nullptr || lt->parent == nullptr)
      return nullptr;
    ltypes[key] = lt->parent->objid;
    return ObjectAt(lt->parent->objid);
  }

  BITCODE_H Ref(Dwg_Object* o) {
    return o != nullptr ? dwg_add_handleref(dwg, 5, o->handle.value, o) : nullptr;
  }

  BITCODE_H RefObjId(BITCODE_BL objid) {
    return Ref(ObjectAt(objid));
  }

  void BuildLayerTable(const AppCommandState& st) {
    for (const CadLayerRow& row : st.drawingLayerTable) {
      if (row.name.empty() || LowerAscii(row.name) == "0")
        continue;
      Dwg_Object_LAYER* ly = dwg_add_LAYER(dwg, row.name.c_str());
      if (ly == nullptr || ly->parent == nullptr)
        continue;
      ApplyLayerTableColor(ly, row.color, row.on, useTrueColor, row.transparency);
      ly->off = row.on ? 0 : 1;
      ly->frozen = row.frozen ? 1 : 0;
      ly->locked = row.locked ? 1 : 0;
      ly->linewt = LineweightDwgIndexFromMm(row.lineweightMm, /*layerRow=*/true);
      ly->flag0 = static_cast<BITCODE_BS>((row.frozen ? 1 : 0) | (row.on ? 0 : 2) |
                                         (row.locked ? 8 : 0) | 16 |
                                         static_cast<BITCODE_BS>((ly->linewt & 0x1F) << 5));
      if (Dwg_Object* lt = EnsureLtype(row.linetype))
        ly->ltype = Ref(lt);
      layers[LowerAscii(row.name)] = ly->parent->objid;
    }
  }

  // REQ-044 / REQ-170, issue #604: the STYLE table (TEXT/MTEXT's font, height, oblique angle),
  // same shape as BuildLayerTable above. "Standard" is skipped: dwg_new_Document already creates
  // it (TEXTSTYLE header var points at it, and dwg_add_TEXT/MTEXT/ATTDEF/ATTRIB default `style`
  // to it), so adding it again would be a second, conflicting STYLE named "Standard".
  void BuildStyleTable(const AppCommandState& st, float modelUnitsPerPlottedInch) {
    for (const TextStyle& ts : st.textStyles) {
      if (ts.name.empty() || LowerAscii(ts.name) == "standard")
        continue;
      Dwg_Object_STYLE* sy = dwg_add_STYLE(dwg, ts.name.c_str());
      if (sy == nullptr || sy->parent == nullptr)
        continue;
      sy->text_size = static_cast<double>(ts.heightInches * std::max(modelUnitsPerPlottedInch, 1.e-6f));
      sy->width_factor = 1.0;
      sy->oblique_angle = static_cast<double>(ts.obliqueDeg) * (kPi / 180.0);
      // fontFamily empty = app default (romans.shx), same convention TextStyle.hpp documents.
      sy->font_file = dwg_add_u8_input(dwg, ts.fontFamily.empty() ? "romans.shx" : ts.fontFamily.c_str());
      styles[LowerAscii(ts.name)] = sy->parent->objid;
    }
  }

  // Returns the STYLE table objid for `name`, or -1 (meaning "leave the entity's default alone" —
  // dwg_add_TEXT/MTEXT already points it at Standard) when the name is empty, "Standard", or not
  // in the table (e.g. an annotation referencing a style that was since deleted).
  BITCODE_BL StyleObjId(const std::string& name) const {
    if (name.empty() || LowerAscii(name) == "standard")
      return static_cast<BITCODE_BL>(-1);
    auto it = styles.find(LowerAscii(name));
    return it != styles.end() ? it->second : static_cast<BITCODE_BL>(-1);
  }

  void Apply(Dwg_Object_Entity* ent, const EntityAttributes& a) {
    if (ent == nullptr)
      return;
    if (!a.layer.empty() && LowerAscii(a.layer) != "0") {
      auto it = layers.find(LowerAscii(a.layer));
      if (it != layers.end())
        ent->layer = RefObjId(it->second);
    }
    SetEntityColorFromStorage(&ent->color, a.color, useTrueColor);
    if (useTrueColor && layerState != nullptr) {
      const std::string layerKey = a.layer.empty() ? std::string("0") : a.layer;
      const CadLayerRow* lyr = FindDrawingLayerRowCi(*layerState, layerKey);
      ApplyEntityEncTransparency(&ent->color, EffectiveEntityTransparency01(a, lyr));
    }
    if (Dwg_Object* lt = EnsureLtype(a.linetype)) {
      ent->ltype = Ref(lt);
      ent->ltype_flags = 3;  // has explicit handle
    }
    ent->linewt = LineweightDwgIndexFromMm(a.lineweightMm, /*layerRow=*/false);
  }
};

// REQ-170, issue #604: LibreDWG 0.13.4's pre-R2007 string writer (dwg_add_u8_input) does not
// encode non-ASCII UTF-8 as the `\U+XXXX` escape AutoCAD expects for an ANSI-codepage DWG (R2000
// is pre-R2007) — its own TODO for that is `#if 0`'d out and calls an internal function with no
// other caller anywhere in the library, so wiring it in untested is exactly the kind of risk that
// produced the D-2026-09-30-f crash. Degree and plus/minus, the two symbols survey text actually
// uses (bearings, tolerances), have had dedicated, codepage-independent AutoCAD control codes
// since R12 — `%%d` / `%%p` — that need no Unicode handling at all, so those two are substituted
// directly. Other non-ASCII characters (accents, non-Latin letters) are passed through as raw
// UTF-8 bytes and will still show as garbage in AutoCAD, unchanged from before this fix; full
// Unicode support is the deferred, harder half of issue #604's character fix.
std::string SanitizeDwgTextSymbols(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 == 0xC2 && i + 1 < s.size()) {
      const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
      if (c1 == 0xB0) {  // U+00B0 DEGREE SIGN
        out += "%%d";
        i += 2;
        continue;
      }
      if (c1 == 0xB1) {  // U+00B1 PLUS-MINUS SIGN
        out += "%%p";
        i += 2;
        continue;
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

const EntityAttributes* AttrAt(const std::vector<EntityAttributes>& v, size_t i) {
  return i < v.size() ? &v[i] : nullptr;
}

// REQ-365 / D-2026-09-30-f / issue #605: the block name other programs see for a GoSurvey survey
// point. One shared definition (a marker circle plus NUMBER/DESCRIPTION attribute
// tags, the Civil 3D/Carlson PNEZD convention), one INSERT per point. GoSurvey's own reopen does
// not read this — it prefers the lossless ADR-044 trailer (DwgIo.cpp) — so this exists purely for
// AutoCAD/Civil 3D, which is why there is no XDATA identity to preserve here.
inline constexpr const char* kSurveyPointBlockName = "GOSURVEY_POINT";

// REQ-107, issue #606: a block definition's own geometry, written into its BLOCK_HEADER. Content
// coordinates are block-LOCAL (no worldDocumentOrigin offset — that only applies to model space),
// which is the one difference from FillFromState's top-level entity loops below; the field
// layouts are otherwise identical (CadBlockContent mirrors AppCommandState field-for-field — see
// HarvestDrawingPrimitivesIntoContent/LoadBlockPrimitivesIntoDrawing in CadBlocks.cpp, the same
// mapping run in reverse for BEDIT). Deliberately simpler than the model-space writer: no tilted-
// polyline split (REQ-325), no MTEXT attachment/style resolution (issue #604) — a block
// definition's own content is written flat, matching what AutoCAD needs to show the block
// correctly without pulling in every model-space refinement. Meshes and solids inside a block are
// not written (same degradations FillFromState already discloses for model space, via the #614
// loss summary). Named nested INSERTs in `content.nested` are written as real INSERT records.
bool AppendGosurveyStringEed(Dwg_Data* dwg, Dwg_Object_Entity* ent, const std::vector<std::string>& strs);
void WriteAnnotativeEntityEed(Dwg_Data* dwg, Dwg_Object_Entity* ent,
                              const std::vector<std::string>* visibleScaleNames = nullptr);
void ImportAnnotativeVisibilityFromEntityEed(const Dwg_Object_Entity* ent, std::vector<std::string>* out);
void WriteBlockDefinitionGeometry(Dwg_Object_BLOCK_HEADER* blkhdr,
                                  const CadBlockContent& content, TableWriter& tw) {
  auto apply = [&](Dwg_Object_Entity* ent, const EntityAttributes* a) {
    if (a != nullptr)
      tw.Apply(ent, *a);
  };
  const size_t nSeg = content.lines.size() / 6;
  for (size_t i = 0; i < nSeg; ++i) {
    dwg_point_3d a{content.lines[i * 6 + 0], content.lines[i * 6 + 1], content.lines[i * 6 + 2]};
    dwg_point_3d b{content.lines[i * 6 + 3], content.lines[i * 6 + 4], content.lines[i * 6 + 5]};
    if (Dwg_Entity_LINE* e = dwg_add_LINE(blkhdr, &a, &b))
      apply(e->parent, AttrAt(content.lineAttrs, i));
  }
  const size_t nC = content.circles.size() / 4;
  for (size_t i = 0; i < nC; ++i) {
    dwg_point_3d c{content.circles[i * 4 + 0], content.circles[i * 4 + 1], content.circles[i * 4 + 2]};
    float nx = kFlatNormalX, ny = kFlatNormalY, nz = kFlatNormalZ;
    CircleNormalAt(content.circleNormals, i, &nx, &ny, &nz);
    dwg_point_3d ext{0.0, 0.0, 1.0};
    if (!IsFlatNormal(nx, ny, nz)) {
      ucs::Ucs frame;
      if (ucs::FromNormal({0.0, 0.0, 0.0}, {static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)},
                          &frame)) {
        const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, {c.x, c.y, c.z});
        c = {ocs.x, ocs.y, ocs.z};
        ext = {static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)};
      }
    }
    if (Dwg_Entity_CIRCLE* e = dwg_add_CIRCLE(blkhdr, &c, content.circles[i * 4 + 3])) {
      e->extrusion.x = ext.x;
      e->extrusion.y = ext.y;
      e->extrusion.z = ext.z;
      apply(e->parent, AttrAt(content.circleAttrs, i));
    }
  }
  for (size_t i = 0; i < content.arcs.size(); ++i) {
    const CadArc& arc = content.arcs[i];
    dwg_point_3d c{arc.cx, arc.cy, arc.z};
    const double a0 = static_cast<double>(arc.startRad);
    const double a1 = a0 + static_cast<double>(arc.sweepRad);
    dwg_point_3d ext{0.0, 0.0, 1.0};
    if (!IsFlatNormal(arc.nx, arc.ny, arc.nz)) {
      ucs::Ucs frame;
      if (ucs::FromNormal({0.0, 0.0, 0.0},
                          {static_cast<double>(arc.nx), static_cast<double>(arc.ny), static_cast<double>(arc.nz)},
                          &frame)) {
        const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, {c.x, c.y, c.z});
        c = {ocs.x, ocs.y, ocs.z};
        ext = {static_cast<double>(arc.nx), static_cast<double>(arc.ny), static_cast<double>(arc.nz)};
      }
    }
    if (Dwg_Entity_ARC* e = dwg_add_ARC(blkhdr, &c, static_cast<double>(arc.r), a0, a1)) {
      e->extrusion.x = ext.x;
      e->extrusion.y = ext.y;
      e->extrusion.z = ext.z;
      apply(e->parent, AttrAt(content.arcAttrs, i));
    }
  }
  for (size_t i = 0; i < content.ellipses.size(); ++i) {
    const CadEllipse& el = content.ellipses[i];
    dwg_point_3d c{el.cx, el.cy, el.z};
    const double majLen = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (majLen < 1e-12)
      continue;
    double ratio = static_cast<double>(el.ratio);
    if (ratio <= 0.0 || ratio > 1.0)
      ratio = 1.0;
    if (Dwg_Entity_ELLIPSE* e = dwg_add_ELLIPSE(blkhdr, &c, majLen, ratio)) {
      apply(e->parent, AttrAt(content.ellAttrs, i));
      e->sm_axis.x = static_cast<double>(el.majVx);
      e->sm_axis.y = static_cast<double>(el.majVy);
      e->sm_axis.z = 0.0;
      e->axis_ratio = ratio;
      e->start_angle = 0.0;
      e->end_angle = 2.0 * kPi;
    }
  }
  for (size_t i = 0; i + 1 < content.polyOffsets.size(); ++i) {
    const int a = content.polyOffsets[i];
    const int b = content.polyOffsets[i + 1];
    const int nv = b - a;
    if (nv < 2)
      continue;
    std::vector<dwg_point_2d> pts(static_cast<size_t>(nv));
    for (int v = 0; v < nv; ++v) {
      const size_t k = static_cast<size_t>(a + v) * 3;
      pts[static_cast<size_t>(v)].x = content.polyVerts[k];
      pts[static_cast<size_t>(v)].y = content.polyVerts[k + 1];
    }
    Dwg_Entity_LWPOLYLINE* lw = dwg_add_LWPOLYLINE(blkhdr, nv, pts.data());
    if (lw == nullptr)
      continue;
    const double z0 = content.polyVerts[static_cast<size_t>(a) * 3 + 2];
    if (z0 != 0.0) {
      lw->elevation = z0;
      lw->flag = static_cast<BITCODE_BS>(lw->flag | 8);
    }
    if (i < content.polyClosed.size() && content.polyClosed[i] != 0)
      lw->flag = static_cast<BITCODE_BS>(lw->flag | 512);
    bool anyBulge = false;
    std::vector<double> bulges(static_cast<size_t>(nv), 0.0);
    for (int v = 0; v < nv; ++v) {
      const size_t vi = static_cast<size_t>(a + v);
      const float bg = vi < content.polyVertsBulge.size() ? content.polyVertsBulge[vi] : 0.f;
      if (bg == 0.f)
        continue;
      bulges[static_cast<size_t>(v)] = static_cast<double>(bg);
      anyBulge = true;
    }
    if (anyBulge) {
      lw->num_bulges = static_cast<BITCODE_BL>(nv);
      lw->bulges = static_cast<BITCODE_BD*>(calloc(static_cast<size_t>(nv), sizeof(BITCODE_BD)));
      if (lw->bulges != nullptr) {
        for (int v = 0; v < nv; ++v)
          lw->bulges[v] = bulges[static_cast<size_t>(v)];
        lw->flag = static_cast<BITCODE_BS>(lw->flag | 16);
      } else {
        lw->num_bulges = 0;
      }
    }
    apply(lw->parent, AttrAt(content.polyAttrs, i));
  }
  for (size_t i = 0; i < content.texts.size(); ++i) {
    const CadAnnotation& an = content.texts[i];
    dwg_point_3d p{an.insX, an.insY, an.insZ};
    const EntityAttributes* at = AttrAt(content.textAttrs, i);
    if (an.kind == CadAnnotation::Kind::Mtext) {
      std::string wire;
      for (char ch : MtextRichFlattenToPlain(SanitizeDwgTextSymbols(an.text))) {
        if (ch == '\n')
          wire += "\\P";
        else if (ch != '\r')
          wire += ch;
      }
      const double bw = std::max(1.0, static_cast<double>(std::fabs(an.boxMaxX - an.boxMinX)));
      if (Dwg_Entity_MTEXT* e = dwg_add_MTEXT(blkhdr, &p, bw, wire.c_str())) {
        e->text_height = std::max(static_cast<double>(an.plottedHeightInches), 1e-3);
        if (tw.dwg != nullptr && tw.dwg->header.version >= R_2018) {
          if (an.annotative)
            e->is_not_annotative = 0;
          else
            e->is_not_annotative = 1;
          if (an.annotative && e->parent != nullptr)
            WriteAnnotativeEntityEed(tw.dwg, e->parent, &an.annotativeVisibleScaleNames);
        }
        apply(e->parent, at);
      }
    } else if (an.kind == CadAnnotation::Kind::Text) {
      if (Dwg_Entity_TEXT* e = dwg_add_TEXT(blkhdr, SanitizeDwgTextSymbols(an.text).c_str(), &p,
                                            std::max(static_cast<double>(an.plottedHeightInches), 1e-3))) {
        e->rotation = static_cast<double>(an.rotationRad);
        if (tw.dwg != nullptr && tw.dwg->header.version >= R_2018 && an.annotative && e->parent != nullptr)
          WriteAnnotativeEntityEed(tw.dwg, e->parent, &an.annotativeVisibleScaleNames);
        apply(e->parent, at);
      }
    }
  }
}

static void WriteBlockDefinitionNestedInserts(Dwg_Object_BLOCK_HEADER* blkhdr, const CadBlockContent& content) {
  if (blkhdr == nullptr)
    return;
  for (const CadBlockNested& n : content.nested) {
    if (n.defName.empty())
      continue;
    dwg_point_3d ins{static_cast<double>(n.xf.x), static_cast<double>(n.xf.y), static_cast<double>(n.xf.z)};
    if (Dwg_Entity_INSERT* e0 =
            dwg_add_INSERT(blkhdr, &ins, n.defName.c_str(), static_cast<double>(n.xf.sx),
                           static_cast<double>(n.xf.sy), static_cast<double>(n.xf.sz),
                           static_cast<double>(n.xf.rotZ)))
      (void)e0;
  }
}

// Creates the GOSURVEY_POINT block definition (marker + attribute tags) once per export. Returns
// nullptr if the block header could not be created, in which case the caller skips every point
// rather than writing a broken INSERT.
Dwg_Object_BLOCK_HEADER* EnsureSurveyPointBlockDef(Dwg_Data* dwg, double markerRadius) {
  Dwg_Object_BLOCK_HEADER* blkhdr = dwg_add_BLOCK_HEADER(dwg, kSurveyPointBlockName);
  if (blkhdr == nullptr)
    return nullptr;
  dwg_add_BLOCK(blkhdr, kSurveyPointBlockName);
  dwg_point_3d c{0.0, 0.0, 0.0};
  dwg_add_CIRCLE(blkhdr, &c, markerRadius);
  const double th = markerRadius * 0.8;
  // Two ATTDEFs only, matching the two ATTRIBs each INSERT below actually fills — see the comment
  // there on why a third attribute isn't safe with this LibreDWG version. Elevation is still
  // visible on the point: it's the INSERT's own Z coordinate.
  dwg_point_3d pNum{markerRadius * 1.5, markerRadius * 0.5, 0.0};
  dwg_add_ATTDEF(blkhdr, th, 0, "Point Number", &pNum, "NUMBER", "");
  dwg_point_3d pDesc{markerRadius * 1.5, -markerRadius * 0.5, 0.0};
  dwg_add_ATTDEF(blkhdr, th, 0, "Description", &pDesc, "DESCRIPTION", "");
  dwg_add_ENDBLK(blkhdr);
  return blkhdr;
}

// REQ-366, issue #607: native DWG DIMENSION objects (save side).
//
// Writes a BITCODE_CMC from the same colour storage convention CadColor.hpp uses ("ByLayer" /
// "ACI:N" / "#RRGGBB" / legacy name). Mirrors TableWriter::Apply's entity-colour branch above —
// DIMSTYLE has no ByBlock concept, so that case collapses to ByLayer.
void DimStyleSetCmc(BITCODE_CMC* out, const std::string& storage, bool useTrueColor) {
  if (out == nullptr)
    return;
  std::string s = storage;
  if (s == "ByBlock")
    s = "ByLayer";
  SetCmcFromStorage(out, s, useTrueColor);
}

// REQ-366 DIMSTYLE field mapping table: only the arrow block NAME maps (DimArrowType -> AutoCAD's
// stock arrow block). "" (empty) means AutoCAD's own default closed-filled arrow.
const char* DimArrowBlockName(DimArrowType t) {
  switch (t) {
    case DimArrowType::ClosedFilled: return "";
    case DimArrowType::ClosedBlank:  return "_CLOSEDBLANK";
    case DimArrowType::Tick:         return "_ARCHTICK";
    case DimArrowType::Dot:          return "_DOT";
    case DimArrowType::Open:         return "_OPEN";
    case DimArrowType::None:         return "_NONE";
  }
  return "";
}

// Finds (or creates) the DWG DIMSTYLE table entry named `wantedName`, disambiguating a name
// collision with "_2", "_3", ... (REQ-366 statement 1). GoSurvey models one ACTIVE DimensionStyle
// per drawing today (AppCommandState::activeDimensionStyle; DimensionStyle.hpp), so in practice
// this writes exactly one entry — the disambiguation loop exists for forward-compat with a future
// per-dimension style snapshot, which DimensionStyle.hpp's own header comment names as a known
// follow-up.
Dwg_Object_DIMSTYLE* EnsureDimStyleNamed(Dwg_Data* dwg, const std::string& wantedName) {
  std::string base = wantedName.empty() ? "Standard" : wantedName;
  std::string name = base;
  for (int n = 2; n < 1000; ++n) {
    BITCODE_H existing = dwg_find_tablehandle(dwg, name.c_str(), "DIMSTYLE");
    if (existing == nullptr)
      break;
    Dwg_Object* o = dwg_resolve_handle_silent(dwg, existing->absolute_ref);
    if (o != nullptr && o->fixedtype == DWG_TYPE_DIMSTYLE && o->tio.object != nullptr &&
        o->tio.object->tio.DIMSTYLE != nullptr)
      return o->tio.object->tio.DIMSTYLE;  // reuse (e.g. "Standard" already exists)
    name = base + "_" + std::to_string(n);
  }
  Dwg_Object_DIMSTYLE* ds = dwg_add_DIMSTYLE(dwg, name.c_str());
  if (ds != nullptr && dwg->header_vars.DIMSTYLE == nullptr)
    dwg->header_vars.DIMSTYLE = dwg_add_handleref(dwg, 5, dwg_obj_generic_handlevalue(ds), nullptr);
  return ds;
}

// Writes the anonymous "*D<n>" block AutoCAD expects every DIMENSION to own (REQ-366 statement 1):
// the same lines/arrows/text CadDimBuildWorldStrokes already produces for viewport/PDF/DXF, so a
// reader that shows block content without regenerating dimensions still draws the right picture.
// Points are in WORLD (local-storage) coordinates, same convention CadDimWorldStrokes already
// uses — unlike WriteBlockDefinitionGeometry's named blocks, an AutoCAD anonymous dimension block
// is not re-transformed by an INSERT; its content is baked at the dimension's actual location.
Dwg_Object_BLOCK_HEADER* WriteDimAnonymousBlock(Dwg_Data* dwg, int* anonCounter,
                                                const CadDimWorldStrokes& strokes, double z,
                                                double originX, double originY,
                                                const std::string& labelText, double labelHeight) {
  const std::string name = "*D" + std::to_string(++(*anonCounter));
  Dwg_Object_BLOCK_HEADER* bh = dwg_add_BLOCK_HEADER(dwg, name.c_str());
  if (bh == nullptr)
    return nullptr;
  bh->anonymous = 1;
  dwg_add_BLOCK(bh, name.c_str());
  auto w = [&](float lx, float ly, dwg_point_3d* p) {
    p->x = static_cast<double>(lx) + originX;
    p->y = static_cast<double>(ly) + originY;
    p->z = z;
  };
  for (const CadDimWorldSeg& s : strokes.segs) {
    dwg_point_3d a{}, b{};
    w(s.x0, s.y0, &a);
    w(s.x1, s.y1, &b);
    dwg_add_LINE(bh, &a, &b);
  }
  // Arrow triangles: three LINEs per triangle (same primitive set WriteBlockDefinitionGeometry
  // already uses elsewhere — no filled-triangle entity is needed for a correct picture).
  for (const CadDimWorldTri& t : strokes.arrows) {
    dwg_point_3d p0{}, p1{}, p2{};
    w(t.x0, t.y0, &p0);
    w(t.x1, t.y1, &p1);
    w(t.x2, t.y2, &p2);
    dwg_add_LINE(bh, &p0, &p1);
    dwg_add_LINE(bh, &p1, &p2);
    dwg_add_LINE(bh, &p2, &p0);
  }
  if (strokes.ok && !labelText.empty()) {
    dwg_point_3d lp{};
    w(strokes.labelX, strokes.labelY, &lp);
    if (Dwg_Entity_TEXT* e = dwg_add_TEXT(bh, SanitizeDwgTextSymbols(labelText).c_str(), &lp,
                                          std::max(labelHeight, 1e-3)))
      e->rotation = strokes.labelRotRad;
  }
  dwg_add_ENDBLK(bh);
  return bh;
}

// REQ-057 / D-2026-10-01-a, issue #603: attaches GOSURVEY-appid XDATA string entries to an
// entity — one group-0 EED per string, chained under the one APPID handle registered in the
// first entry (mirrors encode.c's own internal `add_DUMMY_eed`, the only precedent in this
// vendored library for hand-building an EED record; see that function for the byte layout this
// follows byte-for-byte, generalized from one string to N). Invisible in AutoCAD/Civil 3D, which
// is the point — it survives for a future GoSurvey-aware reader without cluttering the drawing;
// GoSurvey's own reopen never reads it, since ImportDwgFile prefers the lossless ADR-044 trailer.
// Returns false (leaving the entity's EED untouched) if the APPID could not be registered.
bool AppendGosurveyStringEed(Dwg_Data* dwg, Dwg_Object_Entity* ent, const std::vector<std::string>& strs) {
  if (ent == nullptr || strs.empty())
    return false;
  BITCODE_H appid = dwg_find_tablehandle(dwg, "GOSURVEY", "APPID");
  if (appid == nullptr) {
    if (dwg_add_APPID(dwg, "GOSURVEY") == nullptr)
      return false;
    appid = dwg_find_tablehandle(dwg, "GOSURVEY", "APPID");
    if (appid == nullptr)
      return false;
  }
  const BITCODE_BL n = static_cast<BITCODE_BL>(strs.size());
  Dwg_Eed* eed = static_cast<Dwg_Eed*>(calloc(n, sizeof(Dwg_Eed)));
  if (eed == nullptr)
    return false;
  ent->eed = eed;
  ent->num_eed = n;
  for (BITCODE_BL i = 0; i < n; ++i) {
    size_t len = strs[static_cast<size_t>(i)].size();
    if (len > 255)
      len = 255;  // RC length field (non-TU eed_0)
    const BITCODE_BS size = static_cast<BITCODE_BS>(1 + 3 + (len & 0xFF) + 1);
    Dwg_Eed_Data* data = static_cast<Dwg_Eed_Data*>(calloc(static_cast<size_t>(size) + 3, 1));
    if (data == nullptr)
      return i > 0;  // earlier entries are still valid; this one and later are simply absent
    data->code = 0;
    data->u.eed_0.is_tu = 0;
    data->u.eed_0.length = static_cast<unsigned short>(len & 0xFF);
    data->u.eed_0.codepage = 30;
    std::memcpy(data->u.eed_0.string, strs[static_cast<size_t>(i)].data(), len);
    eed[i].data = data;
    if (i == 0) {
      eed[i].size = size;
      dwg_add_handle(&eed[i].handle, 5, appid->absolute_ref, nullptr);
    } else {
      eed[i].size = 0;
      eed[0].size = static_cast<BITCODE_BS>(eed[0].size + size);
    }
  }
  return true;
}

// Issue #622: AutoCAD-native annotative marker (AcadAnnotative APPID + AnnotativeData XDATA).
// Appends five EED records; mirrors encode.c downconvert_DIMSTYLE AnnotativeData layout.
bool AppendAcadAnnotativeEntityEed(Dwg_Data* dwg, Dwg_Object_Entity* ent) {
  if (dwg == nullptr || ent == nullptr)
    return false;
  BITCODE_H appid = dwg_find_tablehandle(dwg, "AcadAnnotative", "APPID");
  if (appid == nullptr) {
    if (dwg_add_APPID(dwg, "AcadAnnotative") == nullptr)
      return false;
    appid = dwg_find_tablehandle(dwg, "AcadAnnotative", "APPID");
  }
  if (appid == nullptr)
    return false;
  const BITCODE_RLL eedAppRef = appid->absolute_ref;
  if (EntityEedHasAppidHandle(ent, eedAppRef))
    return true;

  const BITCODE_BL idx = ent->num_eed;
  ent->num_eed += 5;
  Dwg_Eed* eed = static_cast<Dwg_Eed*>(realloc(ent->eed, (ent->num_eed + 1) * sizeof(Dwg_Eed)));
  if (eed == nullptr) {
    ent->num_eed = idx;
    return false;
  }
  ent->eed = eed;
  for (BITCODE_BL z = idx; z <= ent->num_eed; ++z) {
    ent->eed[z].size = 0;
    ent->eed[z].raw = nullptr;
    ent->eed[z].data = nullptr;
    ent->eed[z].handle.value = 0;
  }

  BITCODE_BL i = idx;
  const size_t annoLen = std::strlen(kAcadAnnotativeDataEed);
  const BITCODE_BS headSize = static_cast<BITCODE_BS>(1 + 3 + (annoLen & 0xFF) + 1);
  dwg_add_handle(&ent->eed[i].handle, 5, eedAppRef, nullptr);
  ent->eed[i].size = headSize;
  ent->eed[i].data = static_cast<Dwg_Eed_Data*>(calloc(static_cast<size_t>(headSize) + 3, 1));
  if (ent->eed[i].data == nullptr)
    return false;
  ent->eed[i].data->code = 0;
  ent->eed[i].data->u.eed_0.is_tu = 0;
  ent->eed[i].data->u.eed_0.length = static_cast<unsigned short>(annoLen & 0xFF);
  ent->eed[i].data->u.eed_0.codepage = 30;
  std::memcpy(ent->eed[i].data->u.eed_0.string, kAcadAnnotativeDataEed, annoLen);
  ++i;
  ent->eed[i].data = static_cast<Dwg_Eed_Data*>(calloc(8, 1));
  if (ent->eed[i].data == nullptr)
    return false;
  ent->eed[i].data->code = 2;
  ++i;
  ent->eed[i].data = static_cast<Dwg_Eed_Data*>(calloc(8, 1));
  if (ent->eed[i].data == nullptr)
    return false;
  ent->eed[i].data->code = 70;
  ent->eed[i].data->u.eed_70.rs = 1;
  ++i;
  ent->eed[i].data = static_cast<Dwg_Eed_Data*>(calloc(8, 1));
  if (ent->eed[i].data == nullptr)
    return false;
  ent->eed[i].data->code = 70;
  ent->eed[i].data->u.eed_70.rs = 1;
  ++i;
  ent->eed[i].data = static_cast<Dwg_Eed_Data*>(calloc(8, 1));
  if (ent->eed[i].data == nullptr)
    return false;
  ent->eed[i].data->code = 2;
  ent->eed[i].data->u.eed_2.close = 1;
  return true;
}

void WriteAnnotativeEntityEed(Dwg_Data* dwg, Dwg_Object_Entity* ent,
                              const std::vector<std::string>* visibleScaleNames) {
  if (dwg == nullptr || ent == nullptr)
    return;
  std::vector<std::string> gos;
  gos.push_back(kGosurveyAnnotativeBlockEed);
  if (visibleScaleNames != nullptr && !visibleScaleNames->empty()) {
    const std::string csv = JoinAnnotativeVisibleScaleNames(*visibleScaleNames);
    if (!csv.empty()) {
      gos.push_back(kGosurveyAnnoVisScalesEed);
      gos.push_back(csv);
    }
  }
  AppendGosurveyStringEed(dwg, ent, gos);
  AppendAcadAnnotativeEntityEed(dwg, ent);
}

// Issue #622: persist the status-bar CANNOSCALE choice on *Model_Space (object EED shares entity
// layout per LibreDWG encode.c). AutoCAD ignores this; GoSurvey native reopen uses it before plot sync.
[[nodiscard]] bool ApplyGosurveyCannoscaleFromEed(const Dwg_Object_Entity* ent, AppCommandState& st) {
  if (ent == nullptr || ent->eed == nullptr || ent->num_eed == 0 || st.annotationScales.empty())
    return false;
  std::vector<std::string> strings;
  strings.reserve(static_cast<size_t>(ent->num_eed));
  for (BITCODE_BL i = 0; i < ent->num_eed; ++i) {
    std::string s;
    if (GosurveyEedCode0String(ent->eed[i].data, &s))
      strings.push_back(std::move(s));
  }
  for (size_t i = 0; i + 1 < strings.size(); ++i) {
    if (strings[i] != kGosurveyCannoscaleEedTag)
      continue;
    const std::string& scaleName = strings[i + 1];
    for (int j = 0; j < static_cast<int>(st.annotationScales.size()); ++j) {
      if (st.annotationScales[static_cast<size_t>(j)].name == scaleName) {
        st.currentAnnotationScaleIndex = j;
        return true;
      }
    }
    return false;
  }
  return false;
}

static void WriteGosurveyCannoscaleOnModelSpace(const AppCommandState& st, Dwg_Data* dwg) {
  if (dwg == nullptr || st.annotationScales.empty())
    return;
  const int ix = st.currentAnnotationScaleIndex;
  if (ix < 0 || ix >= static_cast<int>(st.annotationScales.size()))
    return;
  const std::string& scaleName = st.annotationScales[static_cast<size_t>(ix)].name;
  if (scaleName.empty())
    return;
  Dwg_Object* ms = dwg_model_space_object(dwg);
  if (ms == nullptr || ms->tio.object == nullptr)
    return;
  auto* eedHost = reinterpret_cast<Dwg_Object_Entity*>(ms->tio.object);
  AppendGosurveyStringEed(dwg, eedHost, {kGosurveyCannoscaleEedTag, scaleName});
}

namespace dwg_solid_export {
void WorldPoint(const AppCommandState& st, const ray3d::Vec3& local, dwg_point_3d* out);
}

// Issue #611 / D-2026-10-01-e — triangle meshes and built TINs as POLYLINE_PFACE (ADR-026 amended).
namespace dwg_mesh_export {

namespace {
constexpr unsigned kMaxPfaceVerts = 5000000u;
constexpr unsigned kMaxPfaceFaces = 5000000u;
}  // namespace

bool WriteIndexedTriangles(int vertexCount, const std::function<void(int, dwg_point_3d*)>& fillVertex,
                           const std::vector<std::uint32_t>& indices, Dwg_Object_BLOCK_HEADER* hdr,
                           TableWriter* tw, const EntityAttributes* attr, Dwg_Data* dwg,
                           DwgExportMaterialContext* matCtx, const AppCommandState* st, float defR,
                           float defG, float defB) {
  if (hdr == nullptr || vertexCount < 3)
    return false;
  const size_t nTri = indices.size() / 3;
  if (nTri < 1 || nTri > kMaxPfaceFaces || static_cast<unsigned>(vertexCount) > kMaxPfaceVerts)
    return false;
  std::vector<dwg_point_3d> verts(static_cast<size_t>(vertexCount));
  for (int vi = 0; vi < vertexCount; ++vi)
    fillVertex(vi, &verts[static_cast<size_t>(vi)]);
  std::vector<dwg_face> faces(nTri);
  for (size_t t = 0; t < nTri; ++t) {
    const std::uint32_t a = indices[t * 3 + 0];
    const std::uint32_t b = indices[t * 3 + 1];
    const std::uint32_t c = indices[t * 3 + 2];
    if (a >= static_cast<std::uint32_t>(vertexCount) || b >= static_cast<std::uint32_t>(vertexCount) ||
        c >= static_cast<std::uint32_t>(vertexCount))
      return false;
    faces[t][0] = static_cast<BITCODE_BSd>(static_cast<int>(a) + 1);
    faces[t][1] = static_cast<BITCODE_BSd>(static_cast<int>(b) + 1);
    faces[t][2] = static_cast<BITCODE_BSd>(static_cast<int>(c) + 1);
    faces[t][3] = 0;
  }
  Dwg_Entity_POLYLINE_PFACE* pf =
      dwg_add_POLYLINE_PFACE(hdr, static_cast<unsigned>(vertexCount), static_cast<unsigned>(nTri),
                             verts.data(), faces.data());
  if (pf == nullptr || pf->parent == nullptr)
    return false;
  if (tw != nullptr && attr != nullptr)
    tw->Apply(pf->parent, *attr);
  if (dwg != nullptr && matCtx != nullptr && st != nullptr && attr != nullptr)
    DwgExportApplyEntityMaterial(dwg, matCtx, *st, pf->parent, attr, defR, defG, defB);
  return true;
}

bool WriteCadMesh(const AppCommandState& st, const CadMesh& mesh, Dwg_Object_BLOCK_HEADER* hdr,
                  TableWriter* tw, const EntityAttributes* attr, Dwg_Data* dwg,
                  DwgExportMaterialContext* matCtx) {
  const int nv = mesh.vertexCount();
  if (nv < 3 || mesh.indices.size() < 3)
    return false;
  float defR = 0.78f;
  float defG = 0.78f;
  float defB = 0.78f;
  if (!mesh.parts.empty()) {
    defR = mesh.parts[0].r;
    defG = mesh.parts[0].g;
    defB = mesh.parts[0].b;
  }
  return WriteIndexedTriangles(
      nv,
      [&](int vi, dwg_point_3d* out) {
        const size_t o = static_cast<size_t>(vi) * 3;
        dwg_solid_export::WorldPoint(st, ray3d::Vec3{mesh.vertsXyz[o], mesh.vertsXyz[o + 1],
                                                     mesh.vertsXyz[o + 2]},
                                     out);
      },
      mesh.indices, hdr, tw, attr, dwg, matCtx, &st, defR, defG, defB);
}

bool WriteCadSurfaceTin(const AppCommandState& st, const CadSurface& surface, Dwg_Object_BLOCK_HEADER* hdr,
                        TableWriter* tw, const EntityAttributes* attr, Dwg_Data* dwg,
                        DwgExportMaterialContext* matCtx) {
  if (surface.tin == nullptr)
    return false;
  const CadTin& tin = *surface.tin;
  const int nv = tin.vertexCount();
  if (nv < 3 || tin.indices.size() < 3)
    return false;
  return WriteIndexedTriangles(
      nv,
      [&](int vi, dwg_point_3d* out) {
        const size_t o = static_cast<size_t>(vi) * 3;
        dwg_solid_export::WorldPoint(st, ray3d::Vec3{tin.vertsXyz[o], tin.vertsXyz[o + 1],
                                                     tin.vertsXyz[o + 2]},
                                     out);
      },
      tin.indices, hdr, tw, attr, dwg, matCtx, &st, 0.42f, 0.62f, 0.78f);
}

size_t CountSkippedMeshes(const AppCommandState& st) {
  size_t n = 0;
  for (const std::shared_ptr<const CadMesh>& mp : st.cadMeshes) {
    if (mp == nullptr || mp->triangleCount() < 1)
      ++n;
  }
  return n;
}

size_t CountSkippedSurfaces(const AppCommandState& st) {
  size_t n = 0;
  for (const CadSurface& s : st.cadSurfaces) {
    if (s.tin == nullptr || s.triangleCount() < 1)
      ++n;
  }
  return n;
}

}  // namespace dwg_mesh_export

// Issue #616: LibreDWG has no dwg_add_TABLE — export grid + cell MTEXT (AutoCAD-visible fallback).
namespace dwg_table_export {

bool WriteCadTable(const AppCommandState& st, const CadTable& table, Dwg_Object_BLOCK_HEADER* hdr,
                   TableWriter& tw, const EntityAttributes* attr, DwgExportFieldContext* fldCtx,
                   std::uint64_t blockOwnerHandle) {
  if (hdr == nullptr || table.cols <= 0)
    return false;
  const int rows = CadTableRowCount(table);
  const float w = std::max(table.width, 1.e-3f);
  const float h = std::max(table.height, 1.e-3f);
  const double z = static_cast<double>(table.insZ);
  auto worldLocal = [&](float lx, float ly, dwg_point_3d* p) {
    float wx = 0.f, wy = 0.f;
    CadTableLocalToWorld(table, lx, ly, &wx, &wy);
    p->x = static_cast<double>(wx) + st.worldDocumentOriginX;
    p->y = static_cast<double>(wy) + st.worldDocumentOriginY;
    p->z = z;
  };
  auto seg = [&](float lx0, float ly0, float lx1, float ly1) {
    dwg_point_3d a{}, b{};
    worldLocal(lx0, ly0, &a);
    worldLocal(lx1, ly1, &b);
    if (Dwg_Entity_LINE* e = dwg_add_LINE(hdr, &a, &b))
      if (attr != nullptr)
        tw.Apply(e->parent, *attr);
  };
  for (int r = 0; r <= rows; ++r) {
    const float ly = h * static_cast<float>(r) / static_cast<float>(rows);
    seg(0.f, ly, w, ly);
  }
  for (int c = 0; c <= table.cols; ++c) {
    const float lx = w * static_cast<float>(c) / static_cast<float>(table.cols);
    seg(lx, 0.f, lx, h);
  }
  std::vector<CadTableCellRect> cells;
  CadTableLayoutWorldCells(table, &cells);
  const double textH =
      std::max(static_cast<double>(CadTableHeightWorld(table, st.modelUnitsPerPlottedInch)), 1e-3);
  const double rotRad = static_cast<double>(table.rotationRad);
  for (size_t ci = 0; ci < cells.size() && ci < table.cells.size(); ++ci) {
    dwg_point_3d p{};
    p.x = static_cast<double>(cells[ci].x0) + st.worldDocumentOriginX;
    p.y = static_cast<double>(cells[ci].y1) - textH + st.worldDocumentOriginY;
    p.z = z;
    const CadFieldContext fctx = CadFieldContextFromState(st);
    std::string wire = SanitizeDwgTextSymbols(table.cells[ci]);
    if (wire.empty())
      continue;
    std::string flat;
    for (char ch : MtextRichFlattenToPlain(wire)) {
      if (ch == '\n')
        flat += "\\P";
      else if (ch != '\r')
        flat += ch;
    }
    if (Dwg_Entity_MTEXT* mt = dwg_add_MTEXT(hdr, &p, std::max(textH * 8.0, 1.0), flat.c_str())) {
      mt->text_height = textH;
      mt->attachment = 1;
      mt->x_axis_dir.x = std::cos(rotRad);
      mt->x_axis_dir.y = std::sin(rotRad);
      mt->x_axis_dir.z = 0.0;
      if (attr != nullptr)
        tw.Apply(mt->parent, *attr);
      if (fldCtx != nullptr && fldCtx->enabled && mt->parent != nullptr &&
          CadTextContainsFieldCodes(table.cells[ci])) {
        const std::string nativeWire = DwgExportPrepareAnnotationFieldText(
            fldCtx, tw.dwg, st, table.cells[ci], fctx, mt->parent, blockOwnerHandle);
        std::string nativeFlat;
        for (char ch : MtextRichFlattenToPlain(nativeWire)) {
          if (ch == '\n')
            nativeFlat += "\\P";
          else if (ch != '\r')
            nativeFlat += ch;
        }
        if (!nativeFlat.empty())
          mt->text = dwg_add_u8_input(tw.dwg, nativeFlat.c_str());
      }
    }
  }
  return true;
}

size_t CountSkippedTables(const AppCommandState& st) {
  size_t n = 0;
  for (const CadTable& t : st.cadTables) {
    if (t.cols <= 0)
      ++n;
  }
  return n;
}

}  // namespace dwg_table_export

// Issue #621: POINTCLOUDEX is unavailable — write a scan-file link in XDATA plus a 3D extent box.
namespace dwg_pointcloud_export {

bool CloudBounds(const CadPointCloud& pc, double* mnX, double* mnY, double* mnZ, double* mxX, double* mxY,
                 double* mxZ) {
  if (pc.pointsXyz.size() < 6)
    return false;
  *mnX = *mxX = pc.pointsXyz[0];
  *mnY = *mxY = pc.pointsXyz[1];
  *mnZ = *mxZ = pc.pointsXyz[2];
  for (size_t i = 3; i + 2 < pc.pointsXyz.size(); i += 3) {
    *mnX = std::min(*mnX, pc.pointsXyz[i]);
    *mxX = std::max(*mxX, pc.pointsXyz[i]);
    *mnY = std::min(*mnY, pc.pointsXyz[i + 1]);
    *mxY = std::max(*mxY, pc.pointsXyz[i + 1]);
    *mnZ = std::min(*mnZ, pc.pointsXyz[i + 2]);
    *mxZ = std::max(*mxZ, pc.pointsXyz[i + 2]);
  }
  return *mxX > *mnX && *mxY > *mnY && *mxZ >= *mnZ;
}

bool WritePointCloudMarker(const AppCommandState& st, const CadPointCloud& pc, Dwg_Object_BLOCK_HEADER* hdr,
                           TableWriter& tw, const EntityAttributes* attr) {
  if (hdr == nullptr || pc.sourcePath.empty())
    return false;
  double mnX = 0, mnY = 0, mnZ = 0, mxX = 0, mxY = 0, mxZ = 0;
  if (!CloudBounds(pc, &mnX, &mnY, &mnZ, &mxX, &mxY, &mxZ))
    return false;
  const double ox = st.worldDocumentOriginX;
  const double oy = st.worldDocumentOriginY;
  const dwg_point_3d corners[8] = {
      {mnX + ox, mnY + oy, mnZ}, {mxX + ox, mnY + oy, mnZ}, {mxX + ox, mxY + oy, mnZ},
      {mnX + ox, mxY + oy, mnZ}, {mnX + ox, mnY + oy, mxZ}, {mxX + ox, mnY + oy, mxZ},
      {mxX + ox, mxY + oy, mxZ}, {mnX + ox, mxY + oy, mxZ},
  };
  static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                      {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  Dwg_Object_Entity* tagEnt = nullptr;
  for (int ei = 0; ei < 12; ++ei) {
    if (Dwg_Entity_LINE* e = dwg_add_LINE(hdr, &corners[kEdges[ei][0]], &corners[kEdges[ei][1]])) {
      if (attr != nullptr)
        tw.Apply(e->parent, *attr);
      if (tagEnt == nullptr)
        tagEnt = e->parent;
    }
  }
  if (tagEnt != nullptr)
    AppendGosurveyStringEed(tw.dwg, tagEnt, {"POINTCLOUD", pc.sourcePath});
  return tagEnt != nullptr;
}

size_t CountSkippedPointClouds(const AppCommandState& st) {
  size_t n = 0;
  for (const std::shared_ptr<const CadPointCloud>& pc : st.cadPointClouds) {
    if (pc == nullptr || pc->sourcePath.empty() || pc->pointsXyz.size() < 6)
      ++n;
  }
  return n;
}

}  // namespace dwg_pointcloud_export

// Issue #612 / D-2026-10-01-d — write B-rep solids and pipe runs as ACIS 3DSOLID (ADR-045 (i) amended).
namespace dwg_solid_export {

void WorldPoint(const AppCommandState& st, const ray3d::Vec3& local, dwg_point_3d* out) {
  out->x = local.x + st.worldDocumentOriginX;
  out->y = local.y + st.worldDocumentOriginY;
  out->z = local.z;
}

dwg_point_3d NormalFromUcs(const ucs::Ucs& fr) {
  return dwg_point_3d{fr.zAxis.x, fr.zAxis.y, fr.zAxis.z};
}

ray3d::Vec3 RecipePrimitiveCenterLocal(const brep::Recipe& rc) {
  if (rc.kind == brep::PrimitiveKind::Sphere || rc.kind == brep::PrimitiveKind::Torus)
    return rc.frame.origin;
  return ray3d::Add(rc.frame.origin, ray3d::Scale(rc.frame.zAxis, rc.height * 0.5));
}

bool ParamsFinitePositive(const brep::Recipe& rc) {
  if (!std::isfinite(rc.length) || !std::isfinite(rc.width) || !std::isfinite(rc.height) ||
      !std::isfinite(rc.radius) || !std::isfinite(rc.radius2))
    return false;
  switch (rc.kind) {
  case brep::PrimitiveKind::Box:
  case brep::PrimitiveKind::Wedge:
    return rc.length > 0.0 && rc.width > 0.0 && rc.height > 0.0;
  case brep::PrimitiveKind::Pyramid:
    return rc.height > 0.0 && rc.radius > 0.0 && rc.sides >= 3;
  case brep::PrimitiveKind::Cylinder:
    return rc.radius > 0.0 && rc.height > 0.0;
  case brep::PrimitiveKind::Cone:
    return rc.radius >= 0.0 && rc.radius2 >= 0.0 && rc.height > 0.0;
  case brep::PrimitiveKind::Sphere:
    return rc.radius > 0.0;
  case brep::PrimitiveKind::Torus:
    return rc.radius > 0.0 && rc.radius2 > 0.0;
  default:
    return false;
  }
}

bool CanWriteSolid(const brep::Solid& solid) {
  const brep::Recipe& rc = solid.recipe;
  if (rc.kind != brep::PrimitiveKind::None && rc.kind != brep::PrimitiveKind::Polysolid) {
    if (ParamsFinitePositive(rc))
      return true;
  }
  const acissat::ExportResult er = acissat::ExportSatSolid(solid, "3DSOLID");
  return er.ok;
}

Dwg_Entity__3DSOLID* WriteRecipeSolid(Dwg_Object_BLOCK_HEADER* hdr, const AppCommandState& st,
                                      const brep::Recipe& rc) {
  if (!ParamsFinitePositive(rc))
    return nullptr;
  dwg_point_3d origin{};
  WorldPoint(st, RecipePrimitiveCenterLocal(rc), &origin);
  dwg_point_3d normal = NormalFromUcs(rc.frame);
  switch (rc.kind) {
  case brep::PrimitiveKind::Box:
    return dwg_add_BOX(hdr, &origin, &normal, rc.length, rc.width, rc.height);
  case brep::PrimitiveKind::Wedge:
    return dwg_add_WEDGE(hdr, &origin, &normal, rc.length, rc.width, rc.height);
  case brep::PrimitiveKind::Pyramid:
    return dwg_add_PYRAMID(hdr, &origin, &normal, rc.height, rc.sides, rc.radius, rc.radius2);
  case brep::PrimitiveKind::Cylinder:
    return dwg_add_CYLINDER(hdr, &origin, &normal, rc.height, rc.radius, rc.radius, rc.radius);
  case brep::PrimitiveKind::Cone:
    return dwg_add_CONE(hdr, &origin, &normal, rc.height, rc.radius, rc.radius2, rc.radius);
  case brep::PrimitiveKind::Sphere:
    return dwg_add_SPHERE(hdr, &origin, &normal, rc.radius);
  case brep::PrimitiveKind::Torus:
    return dwg_add_TORUS(hdr, &origin, &normal, rc.radius, rc.radius2);
  default:
    return nullptr;
  }
}

bool WriteSolidEntity(const AppCommandState& st, const brep::Solid& solid, Dwg_Object_BLOCK_HEADER* hdr,
                      TableWriter* tw, const EntityAttributes* attr, Dwg_Data* dwg,
                      DwgExportMaterialContext* matCtx) {
  if (hdr == nullptr)
    return false;
  Dwg_Entity__3DSOLID* ent = WriteRecipeSolid(hdr, st, solid.recipe);
  if (ent == nullptr) {
    const acissat::ExportResult er = acissat::ExportSatSolid(solid, "3DSOLID");
    if (!er.ok)
      return false;
    ent = dwg_add_3DSOLID(hdr, er.sat.c_str());
  }
  if (ent == nullptr || ent->parent == nullptr)
    return false;
  if (tw != nullptr && attr != nullptr)
    tw->Apply(ent->parent, *attr);
  // REQ-372: entity-level MATERIAL on 3DSOLID currently corrupts LibreDWG R2018 encode; mesh
  // hosts (POLYLINE_PFACE) are wired below. Solid subentity materials (DXF 331) are a follow-up.
  (void)dwg;
  (void)matCtx;
  return true;
}

size_t CountSkippedSolids(const AppCommandState& st) {
  size_t n = 0;
  for (const CadSolidPtr& sp : st.cadSolids) {
    if (sp != nullptr && !CanWriteSolid(*sp))
      ++n;
  }
  return n;
}

bool WriteStraightPipeRunAsCylinder(const AppCommandState& st, const CadPipeRun& run,
                                    Dwg_Object_BLOCK_HEADER* hdr, TableWriter* tw,
                                    const EntityAttributes* attr, Dwg_Data* dwg,
                                    DwgExportMaterialContext* matCtx) {
  if (run.vertsXyz.size() != 6)
    return false;
  double odFeet = 0.0;
  if (!CadPipeNominalOdFeet(run.nominalSize, &odFeet))
    return false;
  const double r = odFeet * 0.5;
  if (!(r > 0.0))
    return false;
  ray3d::Vec3 a{run.vertsXyz[0], run.vertsXyz[1], run.vertsXyz[2]};
  ray3d::Vec3 b{run.vertsXyz[3], run.vertsXyz[4], run.vertsXyz[5]};
  ray3d::Vec3 d = ray3d::Sub(b, a);
  const double h = ray3d::Length(d);
  if (!(h > 1e-9))
    return false;
  const ray3d::Vec3 z = ray3d::Scale(d, 1.0 / h);
  ray3d::Vec3 mid = ray3d::Scale(ray3d::Add(a, b), 0.5);
  dwg_point_3d origin{};
  WorldPoint(st, mid, &origin);
  dwg_point_3d normal{z.x, z.y, z.z};
  Dwg_Entity__3DSOLID* ent = dwg_add_CYLINDER(hdr, &origin, &normal, h, r, r, r);
  if (ent == nullptr || ent->parent == nullptr)
    return false;
  if (tw != nullptr && attr != nullptr)
    tw->Apply(ent->parent, *attr);
  (void)dwg;
  (void)matCtx;
  return true;
}

bool WritePipeRunEntity(const AppCommandState& st, const CadPipeRun& run, Dwg_Object_BLOCK_HEADER* hdr,
                        TableWriter* tw, const EntityAttributes* attr, Dwg_Data* dwg,
                        DwgExportMaterialContext* matCtx) {
  std::vector<CadSolidPtr> built;
  if (CadBuildPipeRunSolids(run, &built)) {
    for (const CadSolidPtr& sp : built) {
      if (sp != nullptr && WriteSolidEntity(st, *sp, hdr, tw, attr, dwg, matCtx))
        return true;
    }
  }
  return WriteStraightPipeRunAsCylinder(st, run, hdr, tw, attr, dwg, matCtx);
}

size_t CountSkippedPipeRuns(const AppCommandState& st) {
  size_t n = 0;
  for (size_t ri = 0; ri < st.cadPipeRuns.size(); ++ri) {
    const CadPipeRun& run = st.cadPipeRuns[ri];
    bool canExport = false;
    std::vector<CadSolidPtr> built;
    if (CadBuildPipeRunSolids(run, &built)) {
      for (const CadSolidPtr& sp : built) {
        if (sp != nullptr && CanWriteSolid(*sp)) {
          canExport = true;
          break;
        }
      }
    }
    if (!canExport && run.vertsXyz.size() == 6) {
      double odFeet = 0.0;
      if (CadPipeNominalOdFeet(run.nominalSize, &odFeet) && odFeet > 0.0)
        canExport = true;
    }
    if (!canExport)
      ++n;
  }
  return n;
}

}  // namespace dwg_solid_export

// REQ-170 / REQ-201, issue #614: what DWG save actually drops or degrades, computed from the
// drawing instead of a fixed list — so the "Export DWG" warning and the save log cannot disagree
// with each other or with what FillFromState above actually writes, and a line disappears here the
// moment the entity class it names gets a real writer.
std::vector<DwgExportLoss> ComputeDwgExportLossesImpl(const AppCommandState& st) {
  std::vector<DwgExportLoss> out;
  auto add = [&](const char* label, size_t n) {
    if (n > 0)
      out.push_back(DwgExportLoss{label, static_cast<int>(n)});
  };

  // Entity classes with no DWG writer at all (silent today — nothing in FillFromState touches
  // these vectors). Survey points (REQ-365, #605), feature lines (REQ-057, #603) and block
  // references (REQ-107, #606) are written, so none of the three is here.
  add("table(s)", dwg_table_export::CountSkippedTables(st));
  add("pipe run(s)", dwg_solid_export::CountSkippedPipeRuns(st));
  // REQ-366, issue #607: dimensions now write as real DIMSTYLE/DIMENSION_* objects (see
  // FillFromState) — "dimension(s)" removed from this loss list the same way #631 removed "block
  // reference(s)" once blocks got a real writer.

  // Entity classes GoSurvey already refuses on principle (ADR-042/045/026) — counted and logged,
  // now also surfaced in the pre-export warning so the user sees them before committing to it.
  add("mesh(es)", dwg_mesh_export::CountSkippedMeshes(st));
  add("point cloud(s)", dwg_pointcloud_export::CountSkippedPointClouds(st));
  add("TIN surface(s)", dwg_mesh_export::CountSkippedSurfaces(st));
  add("solid(s)", dwg_solid_export::CountSkippedSolids(st));

  // Degradations: the entity IS written, but a property on it is not.
  const std::vector<EntityAttributes>* attrSets[] = {&st.userLineAttrs, &st.userCircleAttrs,
                                                      &st.userArcAttrs,  &st.userPolylineAttrs,
                                                      &st.cadAnnotationAttrs, &st.userEllAttrs,
                                                      &st.cadFilledRegionAttrs};
  const bool r2004Write = DwgSaveVersionUsesR2004Features(st.dwgExportVersion);
  size_t nColorRounded = 0, nTransparency = 0;
  auto countRoundedRgb = [&](const std::string& colorStr) {
    if (r2004Write)
      return;
    uint32_t rgb = 0;
    if (!DxfColorStringToRgbPacked(colorStr, &rgb))
      return;
    const int aci = DxfNearestAciFromRgbPacked(rgb);
    if ((DxfRgbPackedFromAci(aci) & 0xFFFFFFu) != (rgb & 0xFFFFFFu))
      ++nColorRounded;
  };
  auto countTransparencyLoss = [&](const EntityAttributes& a, const CadLayerRow* layer) {
    if (r2004Write)
      return;
    if (EffectiveEntityTransparency01(a, layer) > 1.e-5f)
      ++nTransparency;
  };
  for (const std::vector<EntityAttributes>* v : attrSets) {
    for (const EntityAttributes& a : *v) {
      countRoundedRgb(a.color);
      countTransparencyLoss(a, FindDrawingLayerRowCi(st, a.layer.empty() ? std::string("0") : a.layer));
    }
  }
  for (const CadLayerRow& row : st.drawingLayerTable)
    countRoundedRgb(row.color);
  add("colour(s) (rounded to the nearest AutoCAD index colour)", nColorRounded);
  add("object(s) with transparency (not written)", nTransparency);

  size_t nRotatedText = 0;
  for (const CadAnnotation& a : st.cadAnnotations)
    if ((a.kind == CadAnnotation::Kind::Text || a.kind == CadAnnotation::Kind::Mtext) &&
        std::fabs(a.rotationRad) > 1e-6f)
      ++nRotatedText;
  add("rotated text/mtext label(s) (rotation not written)", nRotatedText);

  const bool nativeMl = LibreDwgVersionFromExport(st.dwgExportVersion) >= R_2010;
  if (!nativeMl) {
    size_t nMlExtraBranches = 0;
    for (const CadMultileader& ml : st.cadMultileaders)
      nMlExtraBranches += ml.extraLeaderPaths.size();
    add("multileader extra branch(es) (R2000/R2004 export keeps primary branch only)", nMlExtraBranches);
  }

  if (!r2004Write) {
    size_t nDynBlock = 0;
    for (const CadBlockDefinition& d : st.blockDefs) {
      if (CadBlockDefinitionNeedsDynamicDwgExport(d))
        ++nDynBlock;
    }
    add("block definition(s) with dynamic parameters (ACAD evaluation graph requires R2004+)", nDynBlock);
  } else {
    const CadBlockDynamicExportLossCounts dyn = ComputeCadBlockDynamicExportLossCounts(st);
    add("block definition(s) with visibility dynamic parameters (not written to DWG yet)", dyn.visibilityBlockDefs);
    add("dynamic block parameter(s) with no R2004+ DWG encoder yet", dyn.unsupportedParameters);
    add("extra linear dynamic parameter(s) (only one linear/stretch chain is written per block)", dyn.extraLinearParameters);
    add("block insert(s) with conflicting dynamic parameter values (block definition default written)",
        dyn.insertParamConflicts);
    add("dynamic stretch action(s) without entity associations (AutoCAD may not stretch geometry)",
        dyn.stretchWithoutEntityLinks);
  }

  // REQ-057, issue #603: a varying-Z polyline now writes as POLYLINE_3D (real per-vertex Z), but
  // POLYLINE_3D has no bulge — a run that is BOTH 3D and curved still degrades to straight
  // segments between its vertices.
  size_t nFlattenedCurves = 0;
  for (size_t i = 0; i + 1 < st.userPolylineOffsets.size(); ++i) {
    const int a = st.userPolylineOffsets[i];
    const int b = st.userPolylineOffsets[i + 1];
    if (b - a < 2)
      continue;
    const float z0 = st.userPolylineVerts[static_cast<size_t>(a) * 3 + 2];
    bool is3d = false, anyBulge = false;
    for (int vi = a; vi < b; ++vi) {
      if (vi > a && std::fabs(st.userPolylineVerts[static_cast<size_t>(vi) * 3 + 2] - z0) > 1e-4f)
        is3d = true;
      if (static_cast<size_t>(vi) < st.userPolylineVertsBulge.size() &&
          st.userPolylineVertsBulge[static_cast<size_t>(vi)] != 0.f)
        anyBulge = true;
    }
    if (is3d && anyBulge)
      ++nFlattenedCurves;
  }
  add("3D polyline curve(s) (straightened between vertices; POLYLINE_3D has no bulge)",
      nFlattenedCurves);

  const int nMatAppearance = DwgExportCountMaterialAppearanceLosses(st);
  if (nMatAppearance > 0) {
    const bool r2007MaterialExport =
        LibreDwgVersionFromExport(st.dwgExportVersion) >= R_2007;
    if (r2007MaterialExport) {
      add("solid(s) with MATERIAL appearance (entity material not written on 3DSOLID yet)",
          static_cast<size_t>(nMatAppearance));
    } else {
      add("object(s) with MATERIAL appearance (native MATERIAL requires R2010+ DWG export)",
          static_cast<size_t>(nMatAppearance));
    }
  }

  return out;
}

// REQ-037 / REQ-155 / REQ-170, issue #610: paper-space layouts, sheet geometry, and viewports.
static const char* kDwgDefaultCanonicalMedia = "ANSI_B(11.00 x 17.00 Inches)";

static void SetDwgLayoutTabName(Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* bh, const std::string& tabName) {
  if (dwg == nullptr || bh == nullptr || bh->layout == nullptr || tabName.empty())
    return;
  Dwg_Object* layObj = bh->layout->obj;
  if (layObj == nullptr)
    layObj = dwg_resolve_handle_silent(dwg, bh->layout->absolute_ref);
  if (layObj == nullptr || layObj->tio.object == nullptr || layObj->tio.object->tio.LAYOUT == nullptr)
    return;
  layObj->tio.object->tio.LAYOUT->layout_name = dwg_add_u8_input(dwg, tabName.c_str());
}

static Dwg_Object_BLOCK_HEADER* EnsurePaperSpaceBlockHeader(Dwg_Data* dwg, size_t layoutIndex,
                                                              const char* layoutTabName) {
  if (dwg == nullptr || layoutTabName == nullptr || layoutTabName[0] == '\0')
    return nullptr;
  if (layoutIndex == 0) {
    Dwg_Object* ps = dwg_paper_space_object(dwg);
    if (ps == nullptr || ps->tio.object == nullptr)
      return nullptr;
    return ps->tio.object->tio.BLOCK_HEADER;
  }
  char blockName[32];
  const unsigned suffix = static_cast<unsigned>(layoutIndex - 1);
  std::snprintf(blockName, sizeof(blockName), "*Paper_Space%u", suffix);
  Dwg_Object_BLOCK_HEADER* bh = dwg_add_BLOCK_HEADER(dwg, blockName);
  if (bh == nullptr)
    return nullptr;
  dwg_add_BLOCK(bh, blockName);
  dwg_add_ENDBLK(bh);
  int err = 0;
  Dwg_Object* bobj = dwg_obj_generic_to_object(bh, &err);
  if (bobj == nullptr)
    return bh;
  Dwg_Object_LAYOUT* lay = dwg_add_LAYOUT(bobj, layoutTabName, kDwgDefaultCanonicalMedia);
  Dwg_Object* layObj = dwg_obj_generic_to_object(lay, &err);
  if (layObj != nullptr)
    bh->layout = dwg_add_handleref(dwg, 5, layObj->handle.value, nullptr);
  return bh;
}

static std::uint64_t BlockHeaderObjectHandle(Dwg_Object_BLOCK_HEADER* hdr) {
  if (hdr == nullptr)
    return 0;
  int err = 0;
  const Dwg_Object* o = dwg_obj_generic_to_object(hdr, &err);
  return (o != nullptr && err == 0) ? o->handle.value : 0;
}

static void WriteBlockRefInsertToHeader(Dwg_Object_BLOCK_HEADER* hdr, TableWriter& tw, const AppCommandState& st,
                                        const CadBlockRef& ref, const EntityAttributes* at, double insOriginAddX,
                                        double insOriginAddY, DwgExportFieldContext* fldCtx) {
  if (hdr == nullptr || tw.dwg == nullptr || ref.defName.empty())
    return;
  const std::uint64_t blockOwnerHandle = BlockHeaderObjectHandle(hdr);
  if (dwg_find_tablehandle(tw.dwg, ref.defName.c_str(), "BLOCK") == nullptr)
    return;
  const int defIdx = CadBlockFindDef(st.blockDefs, ref.defName);
  const CadBlockDefinition* def =
      defIdx >= 0 ? &st.blockDefs[static_cast<size_t>(defIdx)] : nullptr;

  dwg_point_3d ins{};
  ins.x = static_cast<double>(ref.xf.x) + insOriginAddX;
  ins.y = static_cast<double>(ref.xf.y) + insOriginAddY;
  ins.z = static_cast<double>(ref.xf.z);
  Dwg_Entity_INSERT* e0 =
      dwg_add_INSERT(hdr, &ins, ref.defName.c_str(), static_cast<double>(ref.xf.sx),
                     static_cast<double>(ref.xf.sy), static_cast<double>(ref.xf.sz),
                     static_cast<double>(ref.xf.rotZ));
  if (e0 == nullptr)
    return;
  auto apply = [&](Dwg_Object_Entity* ent, const EntityAttributes* a) {
    if (a != nullptr)
      tw.Apply(ent, *a);
  };
  if (ref.annotative && e0->parent != nullptr)
    WriteAnnotativeEntityEed(tw.dwg, e0->parent, &ref.annotativeVisibleScaleNames);
  if (ref.attributes.empty()) {
    apply(e0->parent, at);
    return;
  }
  const BITCODE_H defRef = dwg_find_tablehandle(tw.dwg, ref.defName.c_str(), "BLOCK");
  for (const CadBlockAttrValue& av : ref.attributes) {
    if (av.tag.empty())
      continue;
    const CadBlockAttrDef* ad = nullptr;
    if (def != nullptr) {
      for (const CadBlockAttrDef& d : def->attrDefs)
        if (d.tag == av.tag) {
          ad = &d;
          break;
        }
    }
    dwg_point_3d ap = ins;
    const double h = ad != nullptr ? std::max(static_cast<double>(ad->height), 1e-3) : 0.125;
    const CadFieldContext fctx = CadFieldContextFromState(st);
    const std::string attrText =
        fldCtx != nullptr && fldCtx->enabled
            ? CadFieldTextForDwgExport(st, av.value, fctx, true)
            : SanitizeDwgTextSymbols(av.value);
    Dwg_Entity_ATTRIB* atEnt =
        dwg_add_ATTRIB(e0, h, 0, &ap, av.tag.c_str(), attrText.c_str());
    if (atEnt != nullptr && fldCtx != nullptr && fldCtx->enabled && atEnt->parent != nullptr &&
        CadTextContainsFieldCodes(av.value)) {
      const std::string nativeText = DwgExportPrepareAnnotationFieldText(
          fldCtx, tw.dwg, st, av.value, fctx, atEnt->parent, blockOwnerHandle);
      if (!nativeText.empty())
        atEnt->text_value = dwg_add_u8_input(tw.dwg, SanitizeDwgTextSymbols(nativeText).c_str());
    }
  }
  if (defRef != nullptr)
    e0->block_header = dwg_add_handleref(tw.dwg, 5, defRef->absolute_ref, nullptr);
  apply(e0->parent, at);
}

static void WritePaperLayoutContent(const PaperLayout& L, Dwg_Object_BLOCK_HEADER* ps, TableWriter& tw,
                                    const AppCommandState& st, DwgExportFieldContext* fldCtx,
                                    DwgExportVisualStyleContext* vsCtx) {
  if (ps == nullptr)
    return;
  auto apply = [&](Dwg_Object_Entity* ent, const EntityAttributes* a) {
    if (a != nullptr)
      tw.Apply(ent, *a);
  };
  for (size_t i = 0; i + 5 < L.paperLines.size(); i += 6) {
    dwg_point_3d a{};
    dwg_point_3d b{};
    a.x = static_cast<double>(L.paperLines[i]);
    a.y = static_cast<double>(L.paperLines[i + 1]);
    a.z = static_cast<double>(L.paperLines[i + 2]);
    b.x = static_cast<double>(L.paperLines[i + 3]);
    b.y = static_cast<double>(L.paperLines[i + 4]);
    b.z = static_cast<double>(L.paperLines[i + 5]);
    Dwg_Entity_LINE* e = dwg_add_LINE(ps, &a, &b);
    if (e != nullptr) {
      const size_t seg = i / 6;
      apply(e->parent, seg < L.paperLineAttrs.size() ? &L.paperLineAttrs[seg] : nullptr);
    }
  }
  int nextVpId = 2;
  for (const Viewport& gv : L.viewports) {
    Dwg_Entity_VIEWPORT* vp = dwg_add_VIEWPORT(ps, "");
    if (vp == nullptr)
      continue;
    const float cx = gv.paperXIn + gv.paperWIn * 0.5f;
    const float cy = gv.paperYIn + gv.paperHIn * 0.5f;
    vp->center.x = static_cast<double>(cx);
    vp->center.y = static_cast<double>(cy);
    vp->center.z = 0.0;
    vp->width = static_cast<double>(gv.paperWIn);
    vp->height = static_cast<double>(gv.paperHIn);
    vp->on_off = 1;
    vp->id = static_cast<BITCODE_RS>(nextVpId++);
    const double ox = st.worldDocumentOriginX;
    const double oy = st.worldDocumentOriginY;
    vp->view_target.x = gv.modelCenterX + ox;
    vp->view_target.y = gv.modelCenterY + oy;
    vp->view_target.z = 0.0;
    vp->VIEWDIR.x = 0.0;
    vp->VIEWDIR.y = 0.0;
    vp->VIEWDIR.z = 1.0;
    vp->VIEWCTR.x = gv.modelCenterX + ox;
    vp->VIEWCTR.y = gv.modelCenterY + oy;
    const float sc = gv.safeScale();
    vp->VIEWSIZE = static_cast<double>(gv.paperHIn) * static_cast<double>(sc);
    vp->LENSLENGTH = 50.0;
    vp->status_flag = 32800;
    vp->UCSVP = 1;
    vp->ucsxdir.x = 1.0;
    vp->ucsydir.y = 1.0;
    if (!gv.layer.empty() && gv.layer != "0") {
      EntityAttributes layerOnly;
      layerOnly.layer = gv.layer;
      apply(vp->parent, &layerOnly);
    }
    if (vsCtx != nullptr)
      DwgExportSetPaperViewportVisualStyle(tw.dwg, vsCtx, vp, gv.visualStyle);
  }
  for (size_t i = 0; i < L.paperBlockRefs.size(); ++i)
    WriteBlockRefInsertToHeader(ps, tw, st, L.paperBlockRefs[i], AttrAt(L.paperBlockRefAttrs, i), 0.0, 0.0,
                                fldCtx);
}

static void FillPaperLayoutsFromState(const AppCommandState& st, Dwg_Data* dwg, TableWriter& tw,
                                      std::vector<std::string>& log, DwgExportFieldContext* fldCtx,
                                      DwgExportVisualStyleContext* vsCtx) {
  if (st.paperLayouts.empty())
    return;
  for (const PaperLayout& L : st.paperLayouts) {
    for (const EntityAttributes& a : L.paperLineAttrs)
      tw.EnsureLtype(a.linetype);
    for (const EntityAttributes& a : L.paperBlockRefAttrs)
      tw.EnsureLtype(a.linetype);
    (void)L;
  }
  size_t nWritten = 0;
  for (size_t li = 0; li < st.paperLayouts.size(); ++li) {
    const PaperLayout& L = st.paperLayouts[li];
    Dwg_Object_BLOCK_HEADER* ps =
        EnsurePaperSpaceBlockHeader(dwg, li, L.name.empty() ? "Layout" : L.name.c_str());
    if (ps == nullptr)
      continue;
    SetDwgLayoutTabName(dwg, ps, L.name.empty() ? "Layout" : L.name);
    WritePaperLayoutContent(L, ps, tw, st, fldCtx, vsCtx);
    ++nWritten;
  }
  if (nWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(nWritten) +
                  " paper layout(s) with viewports (REQ-170, issue #610).");
  }
}

static Dwg_Object* LayoutPaperBlockObject(Dwg_Data* dwg, const Dwg_Object_LAYOUT* lo) {
  if (dwg == nullptr || lo == nullptr || lo->block_header == nullptr)
    return nullptr;
  Dwg_Object* o = lo->block_header->obj;
  if (o == nullptr)
    o = dwg_resolve_handle_silent(dwg, lo->block_header->absolute_ref);
  return o;
}

static void ImportPaperEntity(PaperLayout& L, AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj,
                              std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions) {
  if (obj == nullptr || obj->supertype != DWG_SUPERTYPE_ENTITY || obj->tio.entity == nullptr)
    return;
  Dwg_Object_Entity* ent = obj->tio.entity;
  const EntityAttributes at = AttrFromEnt(dwg, ent);
  const Dwg_Object_Type ty = obj->fixedtype;
  if (ty == DWG_TYPE_VIEWPORT && ent->tio.VIEWPORT != nullptr) {
    const Dwg_Entity_VIEWPORT* e = ent->tio.VIEWPORT;
    // AutoCAD's layout chrome viewport spans the sheet; user viewports are smaller rectangles.
    if (e->width > 50.0 || e->height > 50.0)
      return;
    Viewport vp;
    vp.paperWIn = static_cast<float>(e->width);
    vp.paperHIn = static_cast<float>(e->height);
    vp.paperXIn = static_cast<float>(e->center.x - e->width * 0.5);
    vp.paperYIn = static_cast<float>(e->center.y - e->height * 0.5);
    if (e->height > 1.e-6)
      vp.scaleModelPerPaperIn = static_cast<float>(e->VIEWSIZE / e->height);
    vp.modelCenterX = e->view_target.x - st.worldDocumentOriginX;
    vp.modelCenterY = e->view_target.y - st.worldDocumentOriginY;
    if (!at.layer.empty() && at.layer != "0")
      vp.layer = at.layer;
    vp.visualStyle = DwgImportVisualStyleFromViewport(dwg, e);
    L.viewports.push_back(vp);
    return;
  }
  if (ty == DWG_TYPE_LINE && ent->tio.LINE != nullptr) {
    const Dwg_Entity_LINE* e = ent->tio.LINE;
    L.paperLines.push_back(static_cast<float>(e->start.x));
    L.paperLines.push_back(static_cast<float>(e->start.y));
    L.paperLines.push_back(static_cast<float>(e->start.z));
    L.paperLines.push_back(static_cast<float>(e->end.x));
    L.paperLines.push_back(static_cast<float>(e->end.y));
    L.paperLines.push_back(static_cast<float>(e->end.z));
    L.paperLineAttrs.push_back(at);
    return;
  }
  if (ty == DWG_TYPE_INSERT && ent->tio.INSERT != nullptr) {
    if (ImportNamedInsertAsBlockRef(st, dwg, ent, at, skipHist, degenerateExtrusions, 0.0, 0.0, L.paperBlockRefs,
                                    L.paperBlockRefAttrs, nullptr))
      return;
  }
  if (ty == DWG_TYPE_BLOCK || ty == DWG_TYPE_ENDBLK)
    return;
  NoteSkip(skipHist, "paper-space entity (unsupported type)");
}

static void ImportPaperLayoutsFromDwg(AppCommandState& st, Dwg_Data* dwg,
                                      std::unordered_map<std::string, int>* skipHist, int* degenerateExtrusions) {
  st.paperLayouts.clear();
  struct LayoutRow {
    int tab = 0;
    Dwg_Object* layObj = nullptr;
    std::string name;
  };
  std::vector<LayoutRow> rows;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_LAYOUT || o->tio.object == nullptr ||
        o->tio.object->tio.LAYOUT == nullptr)
      continue;
    Dwg_Object_LAYOUT* lo = o->tio.object->tio.LAYOUT;
    const std::string name = FromT(dwg, lo->layout_name);
    if (name == "Model")
      continue;
    rows.push_back({static_cast<int>(lo->tab_order), o, name});
  }
  std::sort(rows.begin(), rows.end(), [](const LayoutRow& a, const LayoutRow& b) {
    if (a.tab != b.tab)
      return a.tab < b.tab;
    return a.name < b.name;
  });
  for (const LayoutRow& row : rows) {
    Dwg_Object_LAYOUT* lo = row.layObj->tio.object->tio.LAYOUT;
    PaperLayout pl;
    pl.name = row.name.empty() ? "Layout" : row.name;
    Dwg_Object* blk = LayoutPaperBlockObject(dwg, lo);
    if (blk != nullptr) {
      for (Dwg_Object* e = get_first_owned_entity(blk); e != nullptr;
           e = get_next_owned_entity(blk, e))
        ImportPaperEntity(pl, st, dwg, e, skipHist, degenerateExtrusions);
    }
    st.paperLayouts.push_back(std::move(pl));
  }
}

extern "C" void dwg_set_next_objhandle(Dwg_Object* obj);
extern "C" void dwg_resolve_objectrefs_silent(Dwg_Data* dwg);

// LibreDWG has no dwg_add_SCALE (HAVE_NO_DWG_ADD_SCALE). Hand-build AcDbScale objects the same way
// WriteDwgGeoData hand-builds GEODATA — enough for ImportAnnotationScales' object scan on reopen.
static int EnsureDwgScaleClassNumber(Dwg_Data* dwg) {
  if (dwg == nullptr || dwg->dwg_class == nullptr)
    return -1;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname != nullptr && std::strcmp(dxfname, "SCALE") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "SCALE", "AcDbScale", "ObjectDBX Classes", false);
}

static bool AppendDwgAnnotationScaleObject(Dwg_Data* dwg, const CadAnnotationScale& entry) {
  if (dwg == nullptr || entry.name.empty() || entry.paperUnits <= 0.f || entry.drawingUnits <= 0.f)
    return false;
  const int classNumber = EnsureDwgScaleClassNumber(dwg);
  if (classNumber < 0)
    return false;
  const BITCODE_BL idx = dwg->num_objects;
  const int added = dwg_add_object(dwg);
  if (added > 0)
    return false;
  if (added < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = DWG_TYPE_SCALE;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("SCALE") : const_cast<char*>("SCALE");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("SCALE") : const_cast<char*>("SCALE");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return false;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* sc = static_cast<Dwg_Object_SCALE*>(std::calloc(1, sizeof(Dwg_Object_SCALE)));
  if (sc == nullptr)
    return false;
  obj->tio.object->tio.SCALE = sc;
  sc->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  sc->flag = 0;
  sc->name = dwg_add_u8_input(dwg, entry.name.c_str());
  sc->paper_units = static_cast<double>(entry.paperUnits);
  sc->drawing_units = static_cast<double>(entry.drawingUnits);
  sc->is_unit_scale = 0;
  return sc->name != nullptr;
}

static void WriteAnnotationScalesFromState(const AppCommandState& st, Dwg_Data* dwg,
                                           std::vector<std::string>& log) {
  if (dwg == nullptr || st.annotationScales.empty())
    return;
  if (LibreDwgVersionFromExport(st.dwgExportVersion) < R_2007)
    return;
  size_t nWritten = 0;
  for (const CadAnnotationScale& s : st.annotationScales) {
    if (AppendDwgAnnotationScaleObject(dwg, s))
      ++nWritten;
  }
  if (nWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(nWritten) +
                  " annotation SCALE object(s) (issue #622).");
  }
}

void FillFromState(const AppCommandState& st, Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* hdr,
                   std::vector<std::string>& log) {
  auto world = [&](float lx, float ly, double z, dwg_point_3d* p) {
    p->x = static_cast<double>(lx) + st.worldDocumentOriginX;
    p->y = static_cast<double>(ly) + st.worldDocumentOriginY;
    p->z = z;
  };

  const bool r2018Write = LibreDwgVersionFromExport(st.dwgExportVersion) >= R_2018;

  TableWriter tw;
  tw.dwg = dwg;
  tw.useTrueColor = DwgSaveVersionUsesR2004Features(st.dwgExportVersion);
  tw.layerState = &st;
  tw.BuildLayerTable(st);
  tw.BuildStyleTable(st, st.modelUnitsPerPlottedInch);
  WriteAnnotationScalesFromState(st, dwg, log);
  // Register every linetype the entities reference up front, so no LTYPE table object is created
  // after the entity records have started going into the object array.
  for (const std::vector<EntityAttributes>* v :
       {&st.userLineAttrs, &st.userCircleAttrs, &st.userArcAttrs, &st.userPolylineAttrs,
        &st.cadAnnotationAttrs, &st.userEllAttrs, &st.cadFilledRegionAttrs}) {
    for (const EntityAttributes& a : *v)
      tw.EnsureLtype(a.linetype);
  }
  auto apply = [&](Dwg_Object_Entity* ent, const EntityAttributes* a) {
    if (a != nullptr)
      tw.Apply(ent, *a);
  };

  DwgExportFieldContext fldCtx;
  DwgExportFieldContextInit(&fldCtx, DwgSaveVersionUsesR2004Features(st.dwgExportVersion));
  DwgExportVisualStyleContext vsCtx;
  DwgExportVisualStyleContextInit(&vsCtx, LibreDwgVersionFromExport(st.dwgExportVersion) >= R_2007);
  DwgExportMaterialContext matCtx;
  DwgExportMaterialContextInit(&matCtx, LibreDwgVersionFromExport(st.dwgExportVersion) >= R_2007);
  std::uint64_t blockOwnerHandle = 0;
  {
    int hdrErr = 0;
    const Dwg_Object* hdrObj = dwg_obj_generic_to_object(hdr, &hdrErr);
    if (hdrObj != nullptr && hdrErr == 0)
      blockOwnerHandle = hdrObj->handle.value;
  }
  auto regEnt = [&](const void* ent, const EntityAttributes* a) {
    if (a != nullptr)
      DwgExportRegisterEntityHandle(&fldCtx, a->id, ent);
  };

  const size_t nSeg = st.userLinesFlat.size() / 6;
  for (size_t i = 0; i < nSeg; ++i) {
    dwg_point_3d a{}, b{};
    world(st.userLinesFlat[i * 6 + 0], st.userLinesFlat[i * 6 + 1], st.userLinesFlat[i * 6 + 2], &a);
    world(st.userLinesFlat[i * 6 + 3], st.userLinesFlat[i * 6 + 4], st.userLinesFlat[i * 6 + 5], &b);
    Dwg_Entity_LINE* e = dwg_add_LINE(hdr, &a, &b);
    if (e != nullptr) {
      const EntityAttributes* la = AttrAt(st.userLineAttrs, i);
      apply(e->parent, la);
      regEnt(e, la);
    }
  }
  const size_t nC = st.userCirclesCxCyZR.size() / 4;
  for (size_t i = 0; i < nC; ++i) {
    dwg_point_3d c{};
    world(st.userCirclesCxCyZR[i * 4 + 0], st.userCirclesCxCyZR[i * 4 + 1], st.userCirclesCxCyZR[i * 4 + 2], &c);
    float nx = kFlatNormalX, ny = kFlatNormalY, nz = kFlatNormalZ;
    CircleNormalAt(st.userCircleNormals, i, &nx, &ny, &nz);
    const bool circFlat = IsFlatNormal(nx, ny, nz);
    dwg_point_3d ext{0.0, 0.0, 1.0};
    if (!circFlat) {
      ucs::Ucs frame;
      if (ucs::FromNormal({0.0, 0.0, 0.0}, {static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)},
                          &frame)) {
        const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, {c.x, c.y, c.z});
        c.x = ocs.x;
        c.y = ocs.y;
        c.z = ocs.z;
        ext.x = static_cast<double>(nx);
        ext.y = static_cast<double>(ny);
        ext.z = static_cast<double>(nz);
      }
    }
    Dwg_Entity_CIRCLE* e = dwg_add_CIRCLE(hdr, &c, static_cast<double>(st.userCirclesCxCyZR[i * 4 + 3]));
    if (e != nullptr) {
      e->extrusion.x = ext.x;
      e->extrusion.y = ext.y;
      e->extrusion.z = ext.z;
      const EntityAttributes* ca = AttrAt(st.userCircleAttrs, i);
      apply(e->parent, ca);
      regEnt(e, ca);
    }
  }
  for (size_t i = 0; i < st.userArcs.size(); ++i) {
    const CadArc& arc = st.userArcs[i];
    dwg_point_3d c{};
    world(arc.cx, arc.cy, arc.z, &c);
    const double a0 = static_cast<double>(arc.startRad);
    const double a1 = a0 + static_cast<double>(arc.sweepRad);
    // REQ-312 (GitHub issue #391): a tilted arc's normal is group 210's DWG equivalent — the ARC's
    // own `extrusion` field. Without it every arc exported flat, silently. When the normal is not
    // world +Z the centre (group 10) is an OCS coordinate in the Arbitrary Axis frame the normal
    // defines, exactly as `ocsPointOf` writes it in DxfIo.cpp; `ucs::FromNormal` IS that algorithm
    // and returns the world axes unchanged for +Z, so a flat arc's centre and extrusion are
    // byte-identical to before.
    const bool arcFlat = IsFlatNormal(arc.nx, arc.ny, arc.nz);
    dwg_point_3d ext{0.0, 0.0, 1.0};
    if (!arcFlat) {
      ucs::Ucs frame;
      if (ucs::FromNormal({0.0, 0.0, 0.0},
                          {static_cast<double>(arc.nx), static_cast<double>(arc.ny),
                           static_cast<double>(arc.nz)},
                          &frame)) {
        const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, {c.x, c.y, c.z});
        c.x = ocs.x;
        c.y = ocs.y;
        c.z = ocs.z;
        ext.x = static_cast<double>(arc.nx);
        ext.y = static_cast<double>(arc.ny);
        ext.z = static_cast<double>(arc.nz);
      }
    }
    Dwg_Entity_ARC* e = dwg_add_ARC(hdr, &c, static_cast<double>(arc.r), a0, a1);
    if (e != nullptr) {
      e->extrusion.x = ext.x;
      e->extrusion.y = ext.y;
      e->extrusion.z = ext.z;
      const EntityAttributes* aa = AttrAt(st.userArcAttrs, i);
      apply(e->parent, aa);
      regEnt(e, aa);
    }
  }
  // REQ-325 / ADR-053 increment 4 — DWG mirror of DxfIo.cpp's split-on-export.
  // A polyline containing a tilted curved segment is split: flat runs stay
  // LWPOLYLINE, each tilted curved edge becomes its own ARC carrying the
  // segment's own plane (IsFlatNormal guard, BulgeArc + BuildTilted...).
  // Closure is not preserved through a split (ADR-053 (e)), same as DXF.
  for (size_t i = 0; i + 1 < st.userPolylineOffsets.size(); ++i) {
    const int a = st.userPolylineOffsets[i];
    const int b = st.userPolylineOffsets[i + 1];
    const int nv = b - a;
    if (nv < 2)
      continue;
    const EntityAttributes* atPtr = AttrAt(st.userPolylineAttrs, i);
    const bool closed = i < st.userPolylineClosed.size() && st.userPolylineClosed[i] != 0;

    auto emitPolylineRun = [&](int vStart, int vEndIncl, bool runClosed) {
      if (vEndIncl - vStart < 1)
        return;
      const int runNv = vEndIncl - vStart + 1;
      // REQ-057 / REQ-170, issue #603: a vertex whose Z differs from the run's first vertex makes
      // this a genuinely 3D polyline — LWPOLYLINE carries only ONE elevation for the whole run, so
      // writing it there would flatten every other vertex onto the first one's Z (TASK-034 debt).
      // POLYLINE_3D + VERTEX_3D carries a real Z per vertex instead; it does not carry a bulge, so
      // a curved 3D run's curve degrades to straight segments — same precedent as DxfIo's own
      // genuinely-3D-polyline path.
      const double z0 = static_cast<double>(st.userPolylineVerts[static_cast<size_t>(vStart) * 3 + 2]);
      bool is3d = false;
      for (int v = 1; v < runNv; ++v) {
        const int k = (vStart + v) * 3;
        if (std::fabs(static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k) + 2]) - z0) > 1e-4) {
          is3d = true;
          break;
        }
      }
      if (is3d) {
        std::vector<dwg_point_3d> pts3(static_cast<size_t>(runNv));
        for (int v = 0; v < runNv; ++v) {
          const int k = (vStart + v) * 3;
          pts3[static_cast<size_t>(v)].x =
              static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k)]) + st.worldDocumentOriginX;
          pts3[static_cast<size_t>(v)].y =
              static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k + 1)]) + st.worldDocumentOriginY;
          pts3[static_cast<size_t>(v)].z = static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k + 2)]);
        }
        Dwg_Entity_POLYLINE_3D* pl = dwg_add_POLYLINE_3D(hdr, runNv, pts3.data());
        if (pl != nullptr) {
          if (runClosed)
            pl->flag = static_cast<BITCODE_RC>(pl->flag | 1);  // FLAG_POLYLINE_CLOSED
          apply(pl->parent, atPtr);
        }
        return;
      }
      std::vector<dwg_point_2d> pts(static_cast<size_t>(runNv));
      for (int v = 0; v < runNv; ++v) {
        const int k = (vStart + v) * 3;
        pts[static_cast<size_t>(v)].x =
            static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k)]) + st.worldDocumentOriginX;
        pts[static_cast<size_t>(v)].y =
            static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k + 1)]) + st.worldDocumentOriginY;
      }
      Dwg_Entity_LWPOLYLINE* lw = dwg_add_LWPOLYLINE(hdr, runNv, pts.data());
      if (lw == nullptr)
        return;
      if (z0 != 0.0) {
        lw->elevation = z0;
        lw->flag = static_cast<BITCODE_BS>(lw->flag | 8);  // elevation present (dwg.spec)
      }
      if (runClosed)
        lw->flag = static_cast<BITCODE_BS>(lw->flag | 512);
      bool anyBulge = false;
      std::vector<double> bulges(static_cast<size_t>(runNv), 0.0);
      for (int v = 0; v < runNv; ++v) {
        const int vi = vStart + v;
        const float b2 = static_cast<size_t>(vi) < st.userPolylineVertsBulge.size()
                             ? st.userPolylineVertsBulge[static_cast<size_t>(vi)]
                             : 0.f;
        if (b2 == 0.f)
          continue;
        // A run's LAST vertex may be the split point that fed a tilted edge to
        // emitSyntheticArc above (its own stored normal is still tilted) — its bulge describes
        // the segment that got pulled out as that ARC, not a segment left inside this flat run.
        // Writing it would tell a reader this run curves onward to a point that does not exist.
        float nx = 0.f, ny = 0.f, nz = 1.f;
        const size_t nk = static_cast<size_t>(vi) * 3;
        if (nk + 2 < st.userPolylineVertsNormal.size()) {
          nx = st.userPolylineVertsNormal[nk];
          ny = st.userPolylineVertsNormal[nk + 1];
          nz = st.userPolylineVertsNormal[nk + 2];
        }
        if (v == runNv - 1 && !IsFlatNormal(nx, ny, nz))
          continue;
        bulges[static_cast<size_t>(v)] = static_cast<double>(b2);
        anyBulge = true;
      }
      if (anyBulge) {
        lw->num_bulges = static_cast<BITCODE_BL>(runNv);
        lw->bulges = static_cast<BITCODE_BD*>(calloc(static_cast<size_t>(runNv), sizeof(BITCODE_BD)));
        if (lw->bulges != nullptr) {
          for (int v = 0; v < runNv; ++v)
            lw->bulges[v] = bulges[static_cast<size_t>(v)];
          lw->flag = static_cast<BITCODE_BS>(lw->flag | 16);
        } else {
          lw->num_bulges = 0;
        }
      }
      apply(lw->parent, atPtr);
      regEnt(lw, atPtr);
    };

    auto emitSyntheticArc = [&](const CadArc& arc) {
      dwg_point_3d c{};
      world(arc.cx, arc.cy, arc.z, &c);
      const double a0 = static_cast<double>(arc.startRad);
      const double a1 = a0 + static_cast<double>(arc.sweepRad);
      const bool arcFlat = IsFlatNormal(arc.nx, arc.ny, arc.nz);
      dwg_point_3d ext{0.0, 0.0, 1.0};
      if (!arcFlat) {
        ucs::Ucs frame;
        if (ucs::FromNormal({0.0, 0.0, 0.0},
                            {static_cast<double>(arc.nx), static_cast<double>(arc.ny),
                             static_cast<double>(arc.nz)},
                            &frame)) {
          const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, {c.x, c.y, c.z});
          c.x = ocs.x;
          c.y = ocs.y;
          c.z = ocs.z;
          ext.x = static_cast<double>(arc.nx);
          ext.y = static_cast<double>(arc.ny);
          ext.z = static_cast<double>(arc.nz);
        }
      }
      Dwg_Entity_ARC* e = dwg_add_ARC(hdr, &c, static_cast<double>(arc.r), a0, a1);
      if (e != nullptr) {
        e->extrusion.x = ext.x;
        e->extrusion.y = ext.y;
        e->extrusion.z = ext.z;
        apply(e->parent, atPtr);
        regEnt(e, atPtr);
      }
    };

    auto normalAt = [&](int vi, float* nx, float* ny, float* nz) {
      const size_t k = static_cast<size_t>(vi) * 3;
      if (k + 2 < st.userPolylineVertsNormal.size()) {
        *nx = st.userPolylineVertsNormal[k];
        *ny = st.userPolylineVertsNormal[k + 1];
        *nz = st.userPolylineVertsNormal[k + 2];
      } else {
        *nx = 0.f;
        *ny = 0.f;
        *nz = 1.f;
      }
    };
    auto bulgeAt = [&](int vi) -> float {
      return static_cast<size_t>(vi) < st.userPolylineVertsBulge.size()
                 ? st.userPolylineVertsBulge[static_cast<size_t>(vi)]
                 : 0.f;
    };
    auto isTiltedEdge = [&](int vi) {
      float nx = 0.f, ny = 0.f, nz = 1.f;
      normalAt(vi, &nx, &ny, &nz);
      return bulgeAt(vi) != 0.f && !IsFlatNormal(nx, ny, nz);
    };

    bool hasTilted = false;
    for (int vi = a; vi + 1 < b; ++vi)
      if (isTiltedEdge(vi)) {
        hasTilted = true;
        break;
      }
    if (closed && nv >= 2 && isTiltedEdge(b - 1))
      hasTilted = true;

    if (!hasTilted) {
      emitPolylineRun(a, b - 1, closed);
      continue;
    }

    int runStart = a;
    for (int vi = a; vi + 1 < b; ++vi) {
      if (!isTiltedEdge(vi))
        continue;
      emitPolylineRun(runStart, vi, false);
      float nx = 0.f, ny = 0.f, nz = 1.f;
      normalAt(vi, &nx, &ny, &nz);
      const ray3d::Vec3 pA{st.userPolylineVerts[static_cast<size_t>(vi) * 3],
                           st.userPolylineVerts[static_cast<size_t>(vi) * 3 + 1],
                           st.userPolylineVerts[static_cast<size_t>(vi) * 3 + 2]};
      const ray3d::Vec3 pB{st.userPolylineVerts[static_cast<size_t>((vi + 1)) * 3],
                           st.userPolylineVerts[static_cast<size_t>((vi + 1)) * 3 + 1],
                           st.userPolylineVerts[static_cast<size_t>((vi + 1)) * 3 + 2]};
      CadArc arc{};
      if (BuildTiltedPolylineSegmentArc(
              pA, pB, static_cast<double>(bulgeAt(vi)),
              ray3d::Vec3{static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)}, &arc))
        emitSyntheticArc(arc);
      runStart = vi + 1;
    }
    if (closed && nv >= 2 && isTiltedEdge(b - 1)) {
      emitPolylineRun(runStart, b - 1, false);
      float nx = 0.f, ny = 0.f, nz = 1.f;
      normalAt(b - 1, &nx, &ny, &nz);
      const ray3d::Vec3 pA{st.userPolylineVerts[static_cast<size_t>((b - 1)) * 3],
                           st.userPolylineVerts[static_cast<size_t>((b - 1)) * 3 + 1],
                           st.userPolylineVerts[static_cast<size_t>((b - 1)) * 3 + 2]};
      const ray3d::Vec3 pB{st.userPolylineVerts[static_cast<size_t>(a) * 3],
                           st.userPolylineVerts[static_cast<size_t>(a) * 3 + 1],
                           st.userPolylineVerts[static_cast<size_t>(a) * 3 + 2]};
      CadArc arc{};
      if (BuildTiltedPolylineSegmentArc(
              pA, pB, static_cast<double>(bulgeAt(b - 1)),
              ray3d::Vec3{static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)}, &arc))
        emitSyntheticArc(arc);
    } else {
      emitPolylineRun(runStart, b - 1, false);
    }
  }
  // REQ-366, issue #607: DIMSTYLE is created lazily on the first dimension written below, named
  // from the drawing's one active DimensionStyle (DimensionStyle.hpp: a single style today, not a
  // per-dimension table — see EnsureDimStyleNamed's comment).
  Dwg_Object_DIMSTYLE* dimStyle = nullptr;
  int dimAnonCounter = 0;
  int dimsWritten = 0;
  for (size_t i = 0; i < st.cadAnnotations.size(); ++i) {
    const CadAnnotation& an = st.cadAnnotations[i];
    if (an.surveyPointLabelForId >= 0)
      continue;
    const double h = static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch));
    const EntityAttributes* at = AttrAt(st.cadAnnotationAttrs, i);
    const BITCODE_BL styleId = tw.StyleObjId(an.styleName);
    if (CadAnnotationIsDimension(an)) {
      if (dimStyle == nullptr) {
        dimStyle = EnsureDimStyleNamed(dwg, st.activeDimensionStyle.name);
        if (dimStyle != nullptr) {
          const DimensionStyle& sty = st.activeDimensionStyle;
          const double mup = std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
          dimStyle->DIMTXT = static_cast<double>(sty.textSizeInches) * mup;
          dimStyle->DIMASZ = static_cast<double>(sty.arrowSizeInches) * mup;
          dimStyle->DIMDEC = std::clamp(sty.unitPrecision, 0, 8);
          dimStyle->DIMBLK_T = dwg_add_u8_input(dwg, DimArrowBlockName(sty.arrowType));
          DimStyleSetCmc(&dimStyle->DIMCLRD, sty.dimLineColor, tw.useTrueColor);
          DimStyleSetCmc(&dimStyle->DIMCLRE, sty.extLineColor, tw.useTrueColor);
          DimStyleSetCmc(&dimStyle->DIMCLRT, sty.textColor, tw.useTrueColor);
        }
      }
      CadDimStrokeParams sp;
      sp.modelUnitsPerPlottedInch = st.modelUnitsPerPlottedInch;
      sp.arrowSizeInches = st.activeDimensionStyle.arrowSizeInches;
      sp.arrowType = st.activeDimensionStyle.arrowType;
      CadDimWorldStrokes strokes;
      CadDimBuildWorldStrokes(an, sp, &strokes);
      const double labelH = std::max(static_cast<double>(an.plottedHeightInches) *
                                         std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6),
                                     1e-3);
      Dwg_Object_BLOCK_HEADER* anonBlk = WriteDimAnonymousBlock(
          dwg, &dimAnonCounter, strokes, static_cast<double>(an.insZ), st.worldDocumentOriginX,
          st.worldDocumentOriginY, an.text, labelH);
      dwg_point_3d textMid{};
      world(an.insX, an.insY, an.insZ, &textMid);
      Dwg_Entity_DIMENSION_ALIGNED* dimA = nullptr;
      Dwg_Entity_DIMENSION_LINEAR* dimL = nullptr;
      Dwg_Entity_DIMENSION_ANG3PT* dimG = nullptr;
      Dwg_DIMENSION_common* common = nullptr;
      if (an.kind == CadAnnotation::Kind::DimAligned) {
        float sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0, tx = 0, ty = 0, nx = 0, ny = 0, meas = 0;
        if (CadDimAlignedGeometry(an, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas)) {
          dwg_point_3d p1{}, p2{};
          world(an.dimExt1X, an.dimExt1Y, an.insZ, &p1);
          world(an.dimExt2X, an.dimExt2Y, an.insZ, &p2);
          dimA = dwg_add_DIMENSION_ALIGNED(hdr, &p1, &p2, &textMid);
          if (dimA != nullptr) {
            common = reinterpret_cast<Dwg_DIMENSION_common*>(dimA);
            dwg_point_3d dp{};
            world(sx1, sy1, an.insZ, &dp);
            common->def_pt.x = dp.x;  // REQ-366: dim-line point the wrapper omits
            common->def_pt.y = dp.y;
            common->def_pt.z = dp.z;
            common->act_measurement = static_cast<double>(meas);
          }
        }
      } else if (an.kind == CadAnnotation::Kind::DimLinear) {
        float sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0, tx = 0, ty = 0, nx = 0, ny = 0, meas = 0;
        if (CadDimLinearGeometry(an, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas)) {
          dwg_point_3d p1{}, p2{}, pd{};
          world(an.dimExt1X, an.dimExt1Y, an.insZ, &p1);
          world(an.dimExt2X, an.dimExt2Y, an.insZ, &p2);
          world(sx1, sy1, an.insZ, &pd);
          // AutoCAD's rotated-linear convention: 0 = horizontal dim line, pi/2 = vertical — matches
          // CadDimLinearGeometry's own dimLinearVertical branch, so OPEN can recover it losslessly.
          dimL = dwg_add_DIMENSION_LINEAR(hdr, &p1, &p2, &pd, an.dimLinearVertical ? (kPi * 0.5) : 0.0);
          if (dimL != nullptr) {
            common = reinterpret_cast<Dwg_DIMENSION_common*>(dimL);
            common->text_midpt.x = textMid.x;
            common->text_midpt.y = textMid.y;
            common->act_measurement = static_cast<double>(meas);
          }
        }
      } else if (an.kind == CadAnnotation::Kind::DimAngular) {
        float a1 = 0, a2 = 0, sweep = 0, theta = 0, bisx = 0, bisy = 0;
        if (CadDimAngularComputeFrame(an, &a1, &a2, &sweep, &bisx, &bisy, &theta)) {
          const double R = std::max(static_cast<double>(an.dimSignedOffset), 1e-6);
          dwg_point_3d center{}, x1{}, x2{};
          world(an.dimAngVertexX, an.dimAngVertexY, an.insZ, &center);
          world(an.dimExt1X, an.dimExt1Y, an.insZ, &x1);
          world(an.dimExt2X, an.dimExt2Y, an.insZ, &x2);
          dimG = dwg_add_DIMENSION_ANG3PT(hdr, &center, &x1, &x2, &textMid);
          if (dimG != nullptr) {
            common = reinterpret_cast<Dwg_DIMENSION_common*>(dimG);
            // Arc point (DXF 10): bisector direction from the vertex, at radius R — the wrapper
            // leaves def_pt unset (see dwg_add_DIMENSION_ANG3PT in dwg_api.c).
            common->def_pt.x = center.x + static_cast<double>(bisx) * R;
            common->def_pt.y = center.y + static_cast<double>(bisy) * R;
            common->def_pt.z = center.z;
            common->act_measurement = static_cast<double>(theta);
          }
        }
      }
      if (common != nullptr) {
        common->user_text = dwg_add_u8_input(dwg, SanitizeDwgTextSymbols(an.text).c_str());
        common->text_rotation = static_cast<double>(an.rotationRad);
        common->elevation = static_cast<double>(an.insZ);
        if (anonBlk != nullptr) {
          BITCODE_H blkRef = dwg_find_tablehandle(dwg, (anonBlk->name != nullptr ? anonBlk->name : ""),
                                                  "BLOCK");
          if (blkRef != nullptr)
            common->block = dwg_add_handleref(dwg, 5, blkRef->absolute_ref, nullptr);
        }
        apply(common->parent, at);
        if (r2018Write && an.annotative && common->parent != nullptr)
          WriteAnnotativeEntityEed(dwg, common->parent, &an.annotativeVisibleScaleNames);
        ++dimsWritten;
      }
      continue;
    }
    if (an.kind == CadAnnotation::Kind::Mtext) {
      // REQ-044 / REQ-170, issue #604: insertion point is the box corner/edge the attachment
      // selects — same convention DxfIo.cpp's MTEXT writer uses (group 71: col = (attach-1)%3,
      // row = (attach-1)/3), so a GoSurvey DWG and its DXF sibling place the box identically.
      const int attach = std::clamp(an.mtextAttach, 1, 9);
      const int acol = (attach - 1) % 3;
      const int arow = (attach - 1) / 3;
      const float bMnX = std::min(an.boxMinX, an.boxMaxX), bMxX = std::max(an.boxMinX, an.boxMaxX);
      const float bMnY = std::min(an.boxMinY, an.boxMaxY), bMxY = std::max(an.boxMinY, an.boxMaxY);
      const float insXl = acol == 0 ? bMnX : acol == 1 ? 0.5f * (bMnX + bMxX) : bMxX;
      const float insYl = arow == 0 ? bMxY : arow == 1 ? 0.5f * (bMnY + bMxY) : bMnY;
      dwg_point_3d p{};
      world(insXl, insYl, an.insZ, &p);
      const double bw = std::max(1.0, static_cast<double>(std::fabs(an.boxMaxX - an.boxMinX)));
      const CadFieldContext fctx = CadFieldContextFromState(st);
      const std::string fieldText = CadFieldTextForDwgExport(
          st, an.text, fctx, fldCtx.enabled);
      std::string wire;
      for (char ch : MtextRichFlattenToPlain(SanitizeDwgTextSymbols(fieldText))) {
        if (ch == '\n')
          wire += "\\P";
        else if (ch != '\r')
          wire += ch;
      }
      Dwg_Entity_MTEXT* e = dwg_add_MTEXT(hdr, &p, bw, wire.c_str());
      if (e != nullptr) {
        e->text_height = h;
        e->attachment = static_cast<BITCODE_BS>(attach);
        const double rotRad = static_cast<double>(an.rotationRad);
        e->x_axis_dir.x = std::cos(rotRad);
        e->x_axis_dir.y = std::sin(rotRad);
        e->x_axis_dir.z = 0.0;
        if (r2018Write) {
          if (an.annotative)
            e->is_not_annotative = 0;
          else
            e->is_not_annotative = 1;
          if (an.annotative && e->parent != nullptr)
            WriteAnnotativeEntityEed(dwg, e->parent, &an.annotativeVisibleScaleNames);
        }
        if (styleId != static_cast<BITCODE_BL>(-1))
          e->style = tw.RefObjId(styleId);
        apply(e->parent, at);
        if (fldCtx.enabled && e->parent != nullptr && CadTextContainsFieldCodes(an.text)) {
          const std::string nativeWire = DwgExportPrepareAnnotationFieldText(
              &fldCtx, dwg, st, an.text, fctx, e->parent, blockOwnerHandle);
          std::string nativeFlat;
          for (char ch : MtextRichFlattenToPlain(SanitizeDwgTextSymbols(nativeWire))) {
            if (ch == '\n')
              nativeFlat += "\\P";
            else if (ch != '\r')
              nativeFlat += ch;
          }
          if (!nativeFlat.empty())
            e->text = dwg_add_u8_input(dwg, nativeFlat.c_str());
        }
      }
    } else if (an.kind == CadAnnotation::Kind::Text) {
      dwg_point_3d p{};
      world(an.insX, an.insY, an.insZ, &p);
      const CadFieldContext fctxText = CadFieldContextFromState(st);
      const std::string textOut =
          CadFieldTextForDwgExport(st, an.text, fctxText, fldCtx.enabled);
      Dwg_Entity_TEXT* e = dwg_add_TEXT(hdr, SanitizeDwgTextSymbols(textOut).c_str(), &p, h);
      if (e != nullptr) {
        e->rotation = static_cast<double>(an.rotationRad);
        if (r2018Write && an.annotative && e->parent != nullptr)
          WriteAnnotativeEntityEed(dwg, e->parent, &an.annotativeVisibleScaleNames);
        if (styleId != static_cast<BITCODE_BL>(-1))
          e->style = tw.RefObjId(styleId);
        apply(e->parent, at);
        if (fldCtx.enabled && e->parent != nullptr && CadTextContainsFieldCodes(an.text)) {
          const std::string nativeText = DwgExportPrepareAnnotationFieldText(
              &fldCtx, dwg, st, an.text, fctxText, e->parent, blockOwnerHandle);
          if (!nativeText.empty())
            e->text_value = dwg_add_u8_input(dwg, SanitizeDwgTextSymbols(nativeText).c_str());
        }
      }
    }
  }
  if (dimsWritten > 0)
    log.push_back("DWG export — " + std::to_string(dimsWritten) + " dimension(s) (REQ-366).");
  // Position Markers (REQ-359 item 3, D-2026-09-29-e): other programs see a CIRCLE, a cross of two
  // LINEs and the label MTEXT. GoSurvey reopens its own DWG from the trailer, so these never come
  // back as duplicates.
  for (size_t i = 0; i < st.cadPositionMarkers.size(); ++i) {
    const CadPositionMarker& m = st.cadPositionMarkers[i];
    const EntityAttributes* at = AttrAt(st.cadPositionMarkerAttrs, i);
    const double r = static_cast<double>(PositionMarkerRadiusWorld(st));
    dwg_point_3d c{};
    c.x = m.x + st.worldDocumentOriginX;  // double all the way (REQ-101)
    c.y = m.y + st.worldDocumentOriginY;
    c.z = static_cast<double>(m.z);
    if (Dwg_Entity_CIRCLE* e = dwg_add_CIRCLE(hdr, &c, r))
      apply(e->parent, at);
    for (int arm = 0; arm < 2; ++arm) {
      dwg_point_3d a = c, b = c;
      (arm == 0 ? a.x : a.y) -= r;
      (arm == 0 ? b.x : b.y) += r;
      if (Dwg_Entity_LINE* e = dwg_add_LINE(hdr, &a, &b))
        apply(e->parent, at);
    }
    const CadAnnotation& an = m.label;
    dwg_point_3d p{};
    world(std::min(an.boxMinX, an.boxMaxX), std::max(an.boxMinY, an.boxMaxY), an.insZ, &p);  // top-left
    std::string wire;
    for (char ch : MtextRichFlattenToPlain(an.text)) {
      if (ch == '\n')
        wire += "\\P";
      else if (ch != '\r')
        wire += ch;
    }
    const double width = std::max(1.0, static_cast<double>(std::fabs(an.boxMaxX - an.boxMinX)));
    if (Dwg_Entity_MTEXT* e = dwg_add_MTEXT(hdr, &p, width, wire.c_str())) {
      e->text_height = static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch));
      e->attachment = 1;
      apply(e->parent, at);
    }
  }
  // REQ-367 / issue #619: R2010+ native MULTILEADER; older saves use LEADER + MTEXT fallback.
  const bool nativeMultileaderExport = LibreDwgVersionFromExport(st.dwgExportVersion) >= R_2010;
  for (size_t i = 0; i < st.cadMultileaders.size(); ++i) {
    const CadMultileader& ml = st.cadMultileaders[i];
    const EntityAttributes* at = AttrAt(st.cadMultileaderAttrs, i);
    const CadAnnotation& an = ml.label;
    dwg_point_3d tp{};
    world(an.insX, an.insY, an.insZ, &tp);
    std::string wire;
    for (char ch : MtextRichFlattenToPlain(an.text)) {
      if (ch == '\n')
        wire += "\\P";
      else if (ch != '\r')
        wire += ch;
    }
    const double width = std::max(1.0, static_cast<double>(std::fabs(an.boxMaxX - an.boxMinX)));
    const double textH =
        static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch));
    const size_t nPt = ml.pathXyz.size() / 3;
    if (nativeMultileaderExport && nPt >= 2) {
      std::vector<std::vector<dwg_point_3d>> branchPtsStorage;
      std::vector<dwg_mleader_branch> branches;
      auto appendBranch = [&](const std::vector<float>& path) {
        const size_t n = path.size() / 3;
        if (n < 2)
          return;
        branchPtsStorage.emplace_back(n);
        std::vector<dwg_point_3d>& pts = branchPtsStorage.back();
        for (size_t j = 0; j < n; ++j)
          world(path[j * 3], path[j * 3 + 1], path[j * 3 + 2], &pts[j]);
        dwg_mleader_branch b{};
        b.num_points = static_cast<unsigned>(n);
        b.points = pts.data();
        branches.push_back(b);
      };
      appendBranch(ml.pathXyz);
      for (const std::vector<float>& extra : ml.extraLeaderPaths)
        appendBranch(extra);
      if (!branches.empty()) {
        const std::vector<dwg_point_3d>& primary = branchPtsStorage.front();
        dwg_point_3d dir{};
        const size_t last = primary.size() - 1;
        const size_t prev = primary.size() >= 2 ? last - 1 : 0;
        dir.x = primary[last].x - primary[prev].x;
        dir.y = primary[last].y - primary[prev].y;
        dir.z = primary[last].z - primary[prev].z;
        const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len > 1e-9) {
          dir.x /= len;
          dir.y /= len;
          dir.z /= len;
        } else {
          dir.x = 1.0;
          dir.y = dir.z = 0.0;
        }
        if (Dwg_Entity_MULTILEADER* mld = dwg_add_MULTILEADER_branches(
                hdr, static_cast<unsigned>(branches.size()), branches.data(),
                wire.empty() ? " " : wire.c_str(), &tp, &dir, textH, width)) {
          mld->is_annotative = ml.annotative ? 1 : 0;
          if (r2018Write && ml.annotative && mld->parent != nullptr)
            WriteAnnotativeEntityEed(dwg, mld->parent, &ml.annotativeVisibleScaleNames);
          apply(mld->parent, at);
          continue;
        }
      }
    }
    Dwg_Entity_MTEXT* mt =
        dwg_add_MTEXT(hdr, &tp, width, wire.empty() ? " " : wire.c_str());
    if (mt != nullptr) {
      mt->text_height = textH;
      mt->attachment = 1;
      apply(mt->parent, at);
    }
    if (nPt >= 2) {
      std::vector<dwg_point_3d> pts(nPt);
      for (size_t j = 0; j < nPt; ++j) {
        world(ml.pathXyz[j * 3], ml.pathXyz[j * 3 + 1], ml.pathXyz[j * 3 + 2], &pts[j]);
      }
      Dwg_Entity_LEADER* ld =
          dwg_add_LEADER(hdr, static_cast<unsigned>(nPt), pts.data(), mt, 0);
      if (ld != nullptr) {
        apply(ld->parent, at);
      } else {
        for (size_t j = 0; j + 1 < nPt; ++j) {
          if (Dwg_Entity_LINE* e = dwg_add_LINE(hdr, &pts[j], &pts[j + 1]))
            apply(e->parent, at);
        }
      }
    }
  }
  // REQ-365 / issue #605: survey points, written as a GOSURVEY_POINT block INSERT with visible
  // NUMBER/DESCRIPTION attributes so AutoCAD and Civil 3D show them, not just GoSurvey.
  if (!st.surveyPoints.empty()) {
    const double r = static_cast<double>(PositionMarkerRadiusWorld(st));
    Dwg_Object_BLOCK_HEADER* spBlk = EnsureSurveyPointBlockDef(dwg, r);
    if (spBlk != nullptr) {
      // dwg_add_ATTRIB (LibreDWG 0.13.4) overwrites Dwg_Entity_INSERT::block_header — DXF 2, the
      // referenced block — with the INSERT's OWNER block handle (dwg_entity_owner) each time it is
      // called, instead of leaving it alone. Captured here and restored after every ATTRIB, or an
      // INSERT with attributes silently ends up "referencing" the space it lives in.
      const BITCODE_H spBlkRef = dwg_find_tablehandle(dwg, kSurveyPointBlockName, "BLOCK");
      for (const SurveyPoint& sp : st.surveyPoints) {
        dwg_point_3d ins{};
        ins.x = sp.easting + st.worldDocumentOriginX;  // double all the way (REQ-101)
        ins.y = sp.northing + st.worldDocumentOriginY;
        ins.z = sp.elevation;
        Dwg_Entity_INSERT* e0 = dwg_add_INSERT(hdr, &ins, kSurveyPointBlockName, 1.0, 1.0, 1.0, 0.0);
        if (e0 == nullptr)
          continue;
        // Each dwg_add_* call can grow (and so relocate) dwg->object[], which invalidates any
        // Dwg_Object*/entity pointer taken before it — the same reason TableWriter::ObjectAt above
        // resolves by objid rather than caching a pointer. `e0` is only good until the NEXT
        // dwg_add_* call, so every later step re-resolves the INSERT from its stable objid.
        const BITCODE_BL insObjId = e0->parent->objid;
        auto ins2 = [&]() -> Dwg_Entity_INSERT* { return dwg->object[insObjId].tio.entity->tio.INSERT; };
        // Elevation is NOT written as a third attribute here — LibreDWG 0.13.4's dwg_add_ATTRIB /
        // add_attrib_links has a reproducible heap overrun on a THIRD call for the same INSERT
        // (confirmed with a minimal repro independent of GoSurvey's own state: one block, one
        // INSERT, three bare dwg_add_ATTRIB calls, page-heap-verified crash inside
        // add_attrib_links). A patch to the vendored copy (dwg_add_ATTRIB's stale `insobj` after
        // API_ADD_ENTITY can relocate dwg->object[]) is a real bug fix but did not resolve this
        // specific crash, and root-causing further needs a debugger this environment does not
        // have. Two attributes is the safe, verified ceiling; elevation is not lost — it is the
        // INSERT's own Z coordinate (ins.z above), which AutoCAD's Properties palette shows.
        dwg_add_ATTRIB(ins2(), r * 0.8, 0, &ins, "NUMBER", std::to_string(sp.id).c_str());
        dwg_add_ATTRIB(ins2(), r * 0.8, 0, &ins, "DESCRIPTION",
                       sp.description.empty() ? sp.rawDescription.c_str() : sp.description.c_str());
        // dwg_add_ATTRIB (LibreDWG 0.13.4) also overwrites Dwg_Entity_INSERT::block_header — DXF 2,
        // the referenced block — with the INSERT's OWNER block handle (dwg_entity_owner) on every
        // call, instead of leaving it alone. Restored here or an INSERT with attributes silently
        // ends up "referencing" the space it lives in rather than GOSURVEY_POINT.
        if (spBlkRef != nullptr)
          ins2()->block_header = dwg_add_handleref(dwg, 5, spBlkRef->absolute_ref, nullptr);
        EntityAttributes at{};
        at.layer = sp.layer;
        apply(ins2()->parent, &at);
      }
    }
  }
  // REQ-107, issue #606: real BLOCK/INSERT/ATTRIB records, so a GoSurvey DWG shows blocks (incl.
  // pipe-fitting parts) as real, re-insertable blocks in AutoCAD/Civil 3D instead of nothing at
  // all. Definitions are written FIRST and in full before any INSERT, since dwg_add_INSERT looks
  // its target block up by name (must already be in the BLOCK table).
  std::unordered_map<std::string, const CadBlockDefinition*> blockDefByName;
  std::unordered_map<std::string, Dwg_Object_BLOCK_HEADER*> blockHdrByName;
  for (const CadBlockDefinition& def : st.blockDefs) {
    if (def.name.empty())
      continue;
    Dwg_Object_BLOCK_HEADER* bh = dwg_add_BLOCK_HEADER(dwg, def.name.c_str());
    if (bh == nullptr)
      continue;
    if (def.dynamicAnonymous)
      bh->anonymous = 1;
    dwg_add_BLOCK(bh, def.name.c_str());
    WriteBlockDefinitionGeometry(bh, def.content, tw);
    for (const CadBlockAttrDef& ad : def.attrDefs) {
      if (ad.tag.empty())
        continue;
      dwg_point_3d ap{static_cast<double>(ad.localX), static_cast<double>(ad.localY),
                      static_cast<double>(ad.localZ)};
      dwg_add_ATTDEF(bh, std::max(static_cast<double>(ad.height), 1e-3), 0,
                     ad.prompt.empty() ? ad.tag.c_str() : ad.prompt.c_str(), &ap, ad.tag.c_str(),
                     ad.defaultValue.c_str());
    }
    blockDefByName[def.name] = &def;
    blockHdrByName[def.name] = bh;
  }
  // Nested INSERTs need every BLOCK table entry to exist first (LibreDWG dwg_add_INSERT resolves
  // by name via dwg_find_tablehandle), and must be created before ENDBLK so import walks owned
  // entities correctly.
  for (const CadBlockDefinition& def : st.blockDefs) {
    const auto hdrIt = blockHdrByName.find(def.name);
    if (hdrIt == blockHdrByName.end())
      continue;
    WriteBlockDefinitionNestedInserts(hdrIt->second, def.content);
  }
  for (const auto& kv : blockHdrByName)
    dwg_add_ENDBLK(kv.second);
  if (DwgSaveVersionUsesR2004Features(st.dwgExportVersion)) {
    for (const CadBlockDefinition& def : st.blockDefs) {
      const auto hdrIt = blockHdrByName.find(def.name);
      if (hdrIt != blockHdrByName.end())
        WriteGoSurveyDynamicBlockObjects(dwg, hdrIt->second, def, &st.cadBlockRefs, log);
    }
  }
  for (size_t i = 0; i < st.cadBlockRefs.size(); ++i) {
    const CadBlockRef& ref = st.cadBlockRefs[i];
    if (blockDefByName.find(ref.defName) == blockDefByName.end())
      continue;  // definition missing or failed to write; REQ-201 covered by the #614 loss summary
    WriteBlockRefInsertToHeader(hdr, tw, st, ref, AttrAt(st.cadBlockRefAttrs, i), st.worldDocumentOriginX,
                                st.worldDocumentOriginY, &fldCtx);
  }
  // REQ-057 / D-2026-10-01-a, issue #603: survey feature lines, written as POLYLINE_3D (a real Z
  // per vertex — these almost always have varying elevation, which is the whole point of a
  // feature line) with their name/description preserved for a future GoSurvey-aware reader via
  // GOSURVEY-appid XDATA (the user's choice; plain-polyline-only was the alternative). No bulge
  // support — a feature line has none today (st.featureLineVerts carries no bulge array).
  for (size_t i = 0; i + 1 < st.featureLineOffsets.size(); ++i) {
    const int a = st.featureLineOffsets[i];
    const int b = st.featureLineOffsets[i + 1];
    const int nv = b - a;
    if (nv < 2)
      continue;
    std::vector<dwg_point_3d> pts3(static_cast<size_t>(nv));
    for (int v = 0; v < nv; ++v) {
      const size_t k = static_cast<size_t>(a + v) * 3;
      pts3[static_cast<size_t>(v)].x = st.featureLineVerts[k] + st.worldDocumentOriginX;
      pts3[static_cast<size_t>(v)].y = st.featureLineVerts[k + 1] + st.worldDocumentOriginY;
      pts3[static_cast<size_t>(v)].z = st.featureLineVerts[k + 2];
    }
    Dwg_Entity_POLYLINE_3D* pl = dwg_add_POLYLINE_3D(hdr, nv, pts3.data());
    if (pl == nullptr)
      continue;
    if (i < st.featureLineClosed.size() && st.featureLineClosed[i] != 0)
      pl->flag = static_cast<BITCODE_RC>(pl->flag | 1);  // FLAG_POLYLINE_CLOSED
    apply(pl->parent, AttrAt(st.featureLineAttrs, i));
    if (i < st.featureLineInfo.size()) {
      const CadFeatureLineInfo& info = st.featureLineInfo[i];
      if (!info.name.empty() || !info.description.empty())
        AppendGosurveyStringEed(dwg, pl->parent, {info.name, info.description});
    }
  }
  for (size_t i = 0; i < st.userEllipses.size(); ++i) {
    const CadEllipse& el = st.userEllipses[i];
    dwg_point_3d c{};
    world(el.cx, el.cy, el.z, &c);
    const double majLen =
        std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (majLen < 1e-12)
      continue;
    double ratio = static_cast<double>(el.ratio);
    if (ratio <= 0.0 || ratio > 1.0)
      ratio = 1.0;
    Dwg_Entity_ELLIPSE* e = dwg_add_ELLIPSE(hdr, &c, majLen, ratio);
    if (e == nullptr)
      continue;
    apply(e->parent, AttrAt(st.userEllAttrs, i));
    e->sm_axis.x = static_cast<double>(el.majVx);
    e->sm_axis.y = static_cast<double>(el.majVy);
    e->sm_axis.z = 0.0;
    e->axis_ratio = ratio;
    e->start_angle = 0.0;
    e->end_angle = 2.0 * kPi;
  }
  // REQ-170 / issue #608: filled regions → HATCH (solid or predefined pattern). Boundary loops are
  // built from temporary invisible LWPOLYLINE scaffolds that LibreDWG's dwg_add_HATCH copies into
  // the hatch path (non-associative).
  size_t nHatchOut = 0;
  for (size_t fi = 0; fi < st.cadFilledRegions.size(); ++fi) {
    const CadFilledRegion& fr = st.cadFilledRegions[fi];
    if (fr.loopStart.empty() || fr.vertsXyz.size() < 9)
      continue;
    std::vector<Dwg_Object*> pathObjs;
    pathObjs.reserve(fr.loopStart.size());
    bool pathsOk = true;
    for (size_t li = 0; li < fr.loopStart.size(); ++li) {
      const int begin = fr.loopStart[li];
      const int cnt = fr.loopCount(li);
      if (cnt < 3) {
        pathsOk = false;
        break;
      }
      std::vector<dwg_point_2d> pts(static_cast<size_t>(cnt));
      for (int v = 0; v < cnt; ++v) {
        const size_t k = static_cast<size_t>(begin + v) * 3;
        dwg_point_3d wp{};
        world(static_cast<float>(fr.vertsXyz[k]), static_cast<float>(fr.vertsXyz[k + 1]),
              fr.vertsXyz[k + 2], &wp);
        pts[static_cast<size_t>(v)].x = wp.x;
        pts[static_cast<size_t>(v)].y = wp.y;
      }
      Dwg_Entity_LWPOLYLINE* lw = dwg_add_LWPOLYLINE(hdr, cnt, pts.data());
      if (lw == nullptr) {
        pathsOk = false;
        break;
      }
      lw->flag = static_cast<BITCODE_BS>(lw->flag | 512);  // closed
      lw->elevation = fr.vertsXyz[static_cast<size_t>(begin) * 3 + 2];
      lw->parent->invisible = 1;
      pathObjs.push_back(&dwg->object[lw->parent->objid]);
    }
    if (!pathsOk || pathObjs.size() != fr.loopStart.size())
      continue;

    const bool solid = fr.isSolid();
    const char* patName = solid ? "SOLID" : fr.patternName.c_str();
    if (!solid && (patName == nullptr || patName[0] == '\0'))
      patName = "ANSI31";
    Dwg_Entity_HATCH* hatch = dwg_add_HATCH(
        hdr, 1, patName, false, static_cast<unsigned>(pathObjs.size()),
        const_cast<const Dwg_Object**>(pathObjs.data()));
    if (hatch == nullptr)
      continue;
    hatch->elevation = fr.vertsXyz.size() >= 3 ? fr.vertsXyz[2] : 0.0;
    if (!solid) {
      hatch->angle = static_cast<double>(fr.patternAngleDeg) * (kPi / 180.0);
      hatch->scale_spacing = fr.patternScale > 0.f ? static_cast<double>(fr.patternScale) : 1.0;
      hatch->is_solid_fill = 0;
    }
    if (r2018Write && fr.annotative && hatch->paths != nullptr) {
      for (BITCODE_BL pi = 0; pi < hatch->num_paths; ++pi)
        hatch->paths[pi].flag = static_cast<BITCODE_BL>(hatch->paths[pi].flag | 0x200);
      if (hatch->parent != nullptr)
        WriteAnnotativeEntityEed(dwg, hatch->parent, &fr.annotativeVisibleScaleNames);
    }
    apply(hatch->parent, fi < st.cadFilledRegionAttrs.size() ? &st.cadFilledRegionAttrs[fi] : nullptr);
    ++nHatchOut;
  }
  if (nHatchOut > 0)
    log.push_back("CAD export — wrote " + std::to_string(nHatchOut) + " HATCH(es) (REQ-170, issue #608).");

  size_t nMeshOut = 0;
  for (size_t mi = 0; mi < st.cadMeshes.size(); ++mi) {
    const std::shared_ptr<const CadMesh>& mp = st.cadMeshes[mi];
    if (mp == nullptr)
      continue;
    const EntityAttributes* at = mi < st.cadMeshAttrs.size() ? &st.cadMeshAttrs[mi] : nullptr;
    if (dwg_mesh_export::WriteCadMesh(st, *mp, hdr, &tw, at, dwg, &matCtx))
      ++nMeshOut;
  }
  size_t nTinOut = 0;
  for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
    const EntityAttributes* at = si < st.cadSurfaceAttrs.size() ? &st.cadSurfaceAttrs[si] : nullptr;
    if (dwg_mesh_export::WriteCadSurfaceTin(st, st.cadSurfaces[si], hdr, &tw, at, dwg, &matCtx))
      ++nTinOut;
  }
  if (nMeshOut + nTinOut > 0)
    log.push_back("CAD export — wrote " + std::to_string(nMeshOut + nTinOut) +
                  " POLYLINE_PFACE mesh(es) (issue #611).");

  size_t nSolidOut = 0;
  for (size_t si = 0; si < st.cadSolids.size(); ++si) {
    const CadSolidPtr& sp = st.cadSolids[si];
    if (sp == nullptr)
      continue;
    const EntityAttributes* at =
        si < st.cadSolidAttrs.size() ? &st.cadSolidAttrs[si] : nullptr;
    if (dwg_solid_export::WriteSolidEntity(st, *sp, hdr, &tw, at, dwg, &matCtx))
      ++nSolidOut;
  }
  size_t nPipeSolidOut = 0;
  for (size_t ri = 0; ri < st.cadPipeRuns.size(); ++ri) {
    const EntityAttributes* at =
        ri < st.cadPipeRunAttrs.size() ? &st.cadPipeRunAttrs[ri] : nullptr;
    if (dwg_solid_export::WritePipeRunEntity(st, st.cadPipeRuns[ri], hdr, &tw, at, dwg, &matCtx))
      ++nPipeSolidOut;
  }
  if (nSolidOut + nPipeSolidOut > 0)
    log.push_back("CAD export — wrote " + std::to_string(nSolidOut + nPipeSolidOut) +
                  " 3DSOLID(s) (ACIS, issue #612).");

  size_t nTableOut = 0;
  for (size_t ti = 0; ti < st.cadTables.size(); ++ti) {
    const EntityAttributes* at = ti < st.cadTableAttrs.size() ? &st.cadTableAttrs[ti] : nullptr;
    if (dwg_table_export::WriteCadTable(st, st.cadTables[ti], hdr, tw, at, &fldCtx, blockOwnerHandle))
      ++nTableOut;
  }
  if (nTableOut > 0)
    log.push_back("CAD export — wrote " + std::to_string(nTableOut) +
                  " table(s) as grid geometry (issue #616).");

  size_t nPcOut = 0;
  for (size_t pi = 0; pi < st.cadPointClouds.size(); ++pi) {
    const std::shared_ptr<const CadPointCloud>& pc = st.cadPointClouds[pi];
    if (pc == nullptr)
      continue;
    const EntityAttributes* at = pi < st.cadPointCloudAttrs.size() ? &st.cadPointCloudAttrs[pi] : nullptr;
    if (dwg_pointcloud_export::WritePointCloudMarker(st, *pc, hdr, tw, at))
      ++nPcOut;
  }
  if (nPcOut > 0)
    log.push_back("CAD export — wrote " + std::to_string(nPcOut) +
                  " point-cloud extent box(es) with scan path in XDATA (issue #621).");

  FillPaperLayoutsFromState(st, dwg, tw, log, &fldCtx, &vsCtx);
  DwgExportSetModelVisualStyle(dwg, &vsCtx, st.viewportVisualStyle);

  DwgExportFinalizeFieldObjects(&fldCtx, dwg, log);
  DwgExportMaterialAppendLog(matCtx, log);

  // REQ-170 / REQ-201, issue #614: every drop and degradation, named and counted, from the ONE
  // scan the pre-export warning dialog also reads — so the log and the dialog cannot disagree, and
  // nothing is silently dropped.
  for (const DwgExportLoss& loss : ComputeDwgExportLossesImpl(st))
    log.push_back("CAD export — drops " + std::to_string(loss.count) + " " + loss.label + ".");
  (void)dwg;
}

std::string TrimAscii(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
    ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
    --b;
  return s.substr(a, b - a);
}

void BumpExportedDxfHandseed(const char* pathUtf8) {
  std::ifstream in(pathUtf8, std::ios::binary);
  if (!in)
    return;
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line))
    lines.push_back(line);
  in.close();
  if (lines.size() < 2)
    return;

  unsigned long long maxH = 0;
  size_t seedValueIdx = static_cast<size_t>(-1);
  std::string lastVar;
  for (size_t i = 0; i + 1 < lines.size(); i += 1) {
    const std::string code = TrimAscii(lines[i]);
    const std::string value = TrimAscii(lines[i + 1]);
    if (code == "9")
      lastVar = value;
    if (code == "40" &&
        (lastVar == "$TDCREATE" || lastVar == "$TDUPDATE" || lastVar == "$TDUCREATE" ||
         lastVar == "$TDUUPDATE" || lastVar == "$TDINDWG" || lastVar == "$TDUSRTIMER")) {
      const bool crlf = !lines[i + 1].empty() && lines[i + 1].back() == '\r';
      lines[i + 1] = std::string("0.000000000");
      if (crlf)
        lines[i + 1] += '\r';
    }
    if (code == "5") {
      if (lastVar == "$HANDSEED" && seedValueIdx == static_cast<size_t>(-1)) {
        seedValueIdx = i + 1;
        lastVar.clear();
      } else {
        const unsigned long long h = std::strtoull(value.c_str(), nullptr, 16);
        if (h > maxH)
          maxH = h;
      }
    }
  }
  if (seedValueIdx >= lines.size() || maxH == 0)
    return;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(maxH + 1));
  std::string& seedLine = lines[seedValueIdx];
  const bool crlf = !seedLine.empty() && seedLine.back() == '\r';
  seedLine = std::string(buf);
  if (crlf)
    seedLine += '\r';

  std::ofstream out(pathUtf8, std::ios::binary | std::ios::trunc);
  if (!out)
    return;
  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    out.put('\n');
  }
}

bool WriteDxfFile(const char* pathUtf8, Dwg_Data* dwg, std::vector<std::string>& log) {
  // dwg_write_dxf walks BLOCK_HEADER.first_entity. A drawing built only with
  // dwg_add_* often encodes to DWG correctly but has an empty DXF ENTITIES
  // section until it is read back.
  const std::filesystem::path tmp = std::string(pathUtf8) + ".gosurvey-tmp.dwg";
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
  if (dwg_write_file(tmp.string().c_str(), dwg) != DWG_NOERR) {
    log.push_back("DXF export — LibreDWG could not encode an intermediate DWG.");
    std::filesystem::remove(tmp, ec);
    return false;
  }
  Dwg_Data loaded;
  std::memset(&loaded, 0, sizeof(loaded));
  const int rd = dwg_read_file(tmp.string().c_str(), &loaded);
  std::filesystem::remove(tmp, ec);
  if (rd >= DWG_ERR_CRITICAL) {
    log.push_back("DXF export — LibreDWG could not re-read the intermediate DWG.");
    dwg_free(&loaded);
    return false;
  }

  Bit_Chain dat;
  std::memset(&dat, 0, sizeof(dat));
  dat.version = loaded.header.version;
  dat.from_version = loaded.header.from_version;
#if defined(_MSC_VER)
  if (fopen_s(&dat.fh, pathUtf8, "wb") != 0 || dat.fh == nullptr) {
#else
  dat.fh = std::fopen(pathUtf8, "wb");
  if (dat.fh == nullptr) {
#endif
    log.push_back("DXF export failed: could not create " + std::string(pathUtf8) + ".");
    dwg_free(&loaded);
    return false;
  }
  const int err = dwg_write_dxf(&dat, &loaded);
  std::fclose(dat.fh);
  if (dat.chain != nullptr)
    std::free(dat.chain);
  dwg_free(&loaded);
  if (err >= DWG_ERR_CRITICAL) {
    log.push_back("DXF export — LibreDWG encode failed.");
    std::filesystem::remove(pathUtf8, ec);
    return false;
  }
  BumpExportedDxfHandseed(pathUtf8);
  return true;
}

bool WriteDwgFile(const char* pathUtf8, Dwg_Data* dwg, std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0')
    return false;
  const std::filesystem::path dst = std::filesystem::u8path(pathUtf8);
  const std::filesystem::path tmp =
      std::filesystem::path(dst.u8string() + u8".gosurvey-tmp.dwg");
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
  const std::string tmpUtf8 = tmp.u8string();
  AppendSaveTrace("export: dwg_write_file");
  const int err = dwg_write_file(tmpUtf8.c_str(), dwg);
  if (err != DWG_NOERR) {
    log.push_back("DWG export — LibreDWG encode failed.");
    std::filesystem::remove(tmp, ec);
    return false;
  }
  AppendSaveTrace("export: dwg rename staged");
  std::filesystem::rename(tmp, dst, ec);
  if (ec) {
    std::filesystem::copy_file(tmp, dst, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(tmp, ec);
    if (ec) {
      log.push_back(std::string("DWG export failed: could not write ") + pathUtf8 + ".");
      return false;
    }
  }
  return true;
}

}  // namespace

/// REQ-362: the drawing's GEODATA (the one hosted by model space, else the first), copied out for
/// ApplyDwgGeoData. False when the file has none.
static bool ReadDwgGeoData(Dwg_Data* dwg, DwgGeoData* out) {
  const Dwg_Object* ms = dwg_model_space_object(dwg);
  const BITCODE_RLL msHandle = ms != nullptr ? ms->handle.value : 0;
  const Dwg_Object_GEODATA* found = nullptr;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_GEODATA || o->tio.object == nullptr || o->tio.object->tio.GEODATA == nullptr)
      continue;
    const Dwg_Object_GEODATA* g = o->tio.object->tio.GEODATA;
    if (found == nullptr)
      found = g;
    if (g->host_block != nullptr && g->host_block->absolute_ref == msHandle) {
      found = g;
      break;
    }
  }
  if (found == nullptr)
    return false;
  const Dwg_Object_GEODATA& g = *found;
  out->designX = g.design_pt.x;
  out->designY = g.design_pt.y;
  out->reference = g.coord_type == 3   ? DwgGeoData::Reference::Geographic
                   : g.coord_type == 2 ? DwgGeoData::Reference::ProjectedGrid
                                       : DwgGeoData::Reference::None;
  out->refX = g.ref_pt.x;
  out->refY = g.ref_pt.y;
  out->northX = g.north_dir.x;
  out->northY = g.north_dir.y;
  if (g.class_version <= 1) {
    // The 2009 layout (R2000-R2007 files; REQ-362 item 3): the reference point is (latitude,
    // longitude) and north is an angle in radians from +Y, with no north vector.
    out->reference = DwgGeoData::Reference::Geographic;
    out->refX = g.ref_pt.y;
    out->refY = g.ref_pt.x;
    out->northX = std::sin(g.north_dir_angle_deg);
    out->northY = std::cos(g.north_dir_angle_deg);
  }
  out->scaleEstimation = static_cast<int>(g.scale_est);
  out->userScaleFactor = g.user_scale_factor;
  out->seaLevelCorrection = g.do_sea_level_corr != 0;
  out->seaLevelElevation = g.sea_level_elev;
  out->projectionRadius = g.coord_proj_radius;
  out->coordinateSystemDefinition = FromT(dwg, g.coord_system_def);
  return true;
}

// LibreDWG has no dwg_add_GEODATA (HAVE_NO_DWG_ADD_GEODATA). Its own NEW_OBJECT / API_ADD_OBJECT
// macros (src/dwg_api.c) do what WriteDwgGeoData does, with these two library functions that dwg.h
// does not declare (src/dwg.c, src/decode.c; both non-static).
extern "C" void dwg_set_next_objhandle(Dwg_Object* obj);
extern "C" void dwg_resolve_objectrefs_silent(Dwg_Data* dwg);

/// REQ-362 item 2: \p g as a GEODATA on model space's extension dictionary (`ACAD_GEOGRAPHICDATA`),
/// laid out as AutoCAD writes it in an R2000 file (REQ-362 item 3): the reference point stored as
/// (latitude, longitude), north as an angle in radians from +Y in all three angle fields, and
/// coordinate type 0. The encoder writes the class-version-1 layout at R2000 by itself.
static bool WriteDwgGeoData(Dwg_Data* dwg, const DwgGeoData& g) {
  // A new document's *Model_Space has no extension dictionary; never replace one that exists.
  const Dwg_Object* msBefore = dwg_model_space_object(dwg);
  if (msBefore == nullptr || (msBefore->tio.object->xdicobjhandle != nullptr &&
                              msBefore->tio.object->xdicobjhandle->absolute_ref != 0))
    return false;
  // Returns the new class's number (500 and up), or -1.
  const int classNumber = dwg_add_class(dwg, "GEODATA", "AcDbGeoData", "ObjectDBX Classes", false);
  if (classNumber < 0)
    return false;
  const BITCODE_BL geoIndex = dwg->num_objects;
  // 0, or -1 when dwg->object[] moved (then re-resolve, as NEW_OBJECT does), or DWG_ERR_OUTOFMEM.
  const int added = dwg_add_object(dwg);
  if (added > 0)
    return false;
  if (added < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[geoIndex];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->fixedtype = DWG_TYPE_GEODATA;
  // A new document has DWG_OPTS_IN set, so dwg_free frees both names (as for dwg_add_* objects).
  obj->name = _strdup("GEODATA");
  obj->dxfname = _strdup("GEODATA");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return false;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* geo = static_cast<Dwg_Object_GEODATA*>(std::calloc(1, sizeof(Dwg_Object_GEODATA)));
  if (geo == nullptr)
    return false;
  obj->tio.object->tio.GEODATA = geo;
  geo->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  const BITCODE_HV geoHandle = obj->handle.value;

  // The extension dictionary holding it: owned by *Model_Space, as AutoCAD does it.
  Dwg_Object_DICTIONARY* dict = dwg_add_DICTIONARY(dwg, nullptr, "ACAD_GEOGRAPHICDATA", geoHandle);
  int err = 0;
  Dwg_Object* dictObj = dict != nullptr ? dwg_obj_generic_to_object(dict, &err) : nullptr;
  Dwg_Object* ms = dwg_model_space_object(dwg);
  if (dictObj == nullptr || err != 0 || ms == nullptr)
    return false;
  const BITCODE_HV dictHandle = dictObj->handle.value;
  const BITCODE_HV msHandle = ms->handle.value;
  // Owner plus one reactor pointing at it — the links AutoCAD writes on both objects.
  auto ownBy = [dwg](Dwg_Object_Object* o, BITCODE_HV owner) {
    o->ownerhandle = dwg_add_handleref(dwg, 4, owner, nullptr);
    o->num_reactors = 1;
    o->reactors = static_cast<BITCODE_H*>(std::calloc(1, sizeof(BITCODE_H)));
    if (o->reactors != nullptr)
      o->reactors[0] = dwg_add_handleref(dwg, 4, owner, nullptr);
    else
      o->num_reactors = 0;
  };
  ownBy(dictObj->tio.object, msHandle);
  ms->tio.object->xdicobjhandle = dwg_add_handleref(dwg, 3, dictHandle, nullptr);

  obj = &dwg->object[geoIndex];  // dwg->object[] may have moved while the dictionary was added
  ownBy(obj->tio.object, dictHandle);
  obj->tio.object->xdicobjhandle = dwg_add_handleref(dwg, 3, 0, nullptr);

  geo = obj->tio.object->tio.GEODATA;
  const double northRad = std::atan2(g.northX, g.northY);  // from +Y towards +X
  geo->host_block = dwg_add_handleref(dwg, 4, msHandle, nullptr);
  const bool geodataV2 = dwg->header.version >= R_2010;
  geo->class_version = geodataV2 ? 2 : 1;
  if (geodataV2) {
    geo->coord_type = g.reference == DwgGeoData::Reference::Geographic
                          ? static_cast<BITCODE_BS>(3)
                          : g.reference == DwgGeoData::Reference::ProjectedGrid
                                ? static_cast<BITCODE_BS>(2)
                                : static_cast<BITCODE_BS>(1);
    geo->ref_pt = {g.refX, g.refY, 0.0};
  } else {
    geo->coord_type = 0;
    geo->ref_pt = {g.refY, g.refX, 0.0};  // (latitude, longitude), 2009 layout
  }
  geo->design_pt = {g.designX, g.designY, 0.0};
  geo->obs_pt = {0.0, 0.0, 0.0};
  geo->up_dir = {0.0, 0.0, 1.0};
  geo->north_dir = {g.northX, g.northY};
  geo->north_dir_angle_deg = northRad;
  geo->north_dir_angle_rad = northRad;
  geo->scale_vec = {1.0, 1.0, 1.0};
  geo->units_value_horiz = static_cast<BITCODE_BL>(g.horizontalUnits);
  geo->unit_scale_horiz = g.horizontalUnitScale;
  geo->units_value_vert = static_cast<BITCODE_BL>(g.horizontalUnits);
  geo->unit_scale_vert = g.horizontalUnitScale;
  geo->scale_est = static_cast<BITCODE_BL>(g.scaleEstimation);
  geo->user_scale_factor = g.userScaleFactor;
  geo->do_sea_level_corr = g.seaLevelCorrection ? 1 : 0;
  geo->sea_level_elev = g.seaLevelElevation;
  geo->coord_proj_radius = g.projectionRadius;
  geo->coord_system_def = dwg_add_u8_input(dwg, g.coordinateSystemDefinition.c_str());
  geo->geo_rss_tag = dwg_add_u8_input(dwg, "");
  geo->coord_system_datum = dwg_add_u8_input(dwg, "");
  geo->coord_system_wkt = dwg_add_u8_input(dwg, "");
  geo->observation_from_tag = dwg_add_u8_input(dwg, "");
  geo->observation_to_tag = dwg_add_u8_input(dwg, "");
  geo->observation_coverage_tag = dwg_add_u8_input(dwg, "");
  geo->has_civil_data = 1;
  return true;
}

void ImportAnnotationScales(AppCommandState& st, Dwg_Data* dwg) {
  st.annotationScales.clear();
  if (dwg == nullptr)
    return;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_SCALE || o->tio.object == nullptr || o->tio.object->tio.SCALE == nullptr)
      continue;
    const Dwg_Object_SCALE* sc = o->tio.object->tio.SCALE;
    if (sc->flag != 0)
      continue;  // skip temporary scales
    CadAnnotationScale entry;
    entry.name = FromT(dwg, sc->name);
    entry.paperUnits = static_cast<float>(sc->paper_units);
    entry.drawingUnits = static_cast<float>(sc->drawing_units);
    if (entry.name.empty() || entry.paperUnits <= 0.f || entry.drawingUnits <= 0.f)
      continue;
    st.annotationScales.push_back(std::move(entry));
  }
}

bool ImportLibreCadFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log, bool asDxf) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back(asDxf ? "DXF import — no path." : "DWG import — no path.");
    return false;
  }
  if (!asDxf) {
    const std::string ver = DwgVersionName(pathUtf8);
    if (ver.empty()) {
      log.push_back("DWG import — file is not a DWG (no AC#### format tag).");
      return false;
    }
    log.push_back("DWG import — LibreDWG (" + ver + ").");
  } else {
    log.push_back("DXF import — LibreDWG.");
  }

  Dwg_Data dwg;
  if (!LoadDwgData(pathUtf8, asDxf, &dwg, log))
    return false;
  DwgMaterialImportBegin();
  DwgAnnotContextImportBegin();

  const double oldOx = st.worldDocumentOriginX;
  const double oldOy = st.worldDocumentOriginY;
  ResetCadToolStateToIdle(st);
  ClearCadGeometry(st);
  st.selectedSurveyPointIndices.clear();
  const bool hadPts = !st.surveyPoints.empty();
  if (hadPts) {
    if (oldOx != 0.0 || oldOy != 0.0)
      CadCoord::ShiftAllStorageBy(st, oldOx, oldOy);
    for (SurveyPoint& p : st.surveyPoints)
      p.labelMtextAnnId = 0;
  }

  if (CadDrawingInsUnitsOffered(static_cast<int>(dwg.header_vars.INSUNITS)))
    st.drawingInsUnits = static_cast<int>(dwg.header_vars.INSUNITS);

  const double minX = dwg.header_vars.EXTMIN.x;
  const double minY = dwg.header_vars.EXTMIN.y;
  const double maxX = dwg.header_vars.EXTMAX.x;
  const double maxY = dwg.header_vars.EXTMAX.y;
  const double span = std::max(std::fabs(maxX - minX), std::fabs(maxY - minY));
  const double mag = std::max({std::fabs(minX), std::fabs(maxX), std::fabs(minY), std::fabs(maxY)});
  if (span < 1e6 && mag >= CadCoord::kLargeCoordinateRebaseThreshold)
    CadCoord::ApplyDocumentOriginRebase(st, 0.5 * (minX + maxX), 0.5 * (minY + maxY), &log);

  ImportLayers(st, &dwg, log);
  ImportStyles(st, &dwg);
  if (!asDxf) {  // REQ-362: an AutoCAD / Civil 3D drawing's geolocation (read only, D-2026-09-29-g)
    DwgGeoData geo;
    if (ReadDwgGeoData(&dwg, &geo))
      ApplyDwgGeoData(st, geo, log);
  }

  std::unordered_map<std::string, int> skipHist;
  int degenerateExtrusions = 0;
  const Xf2 id{};
  Dwg_Object* mspace = dwg_model_space_object(&dwg);
  if (mspace != nullptr) {
    for (Dwg_Object* e = get_first_owned_entity(mspace); e != nullptr; e = get_next_owned_entity(mspace, e))
      ImportObject(st, &dwg, e, id, 0, &skipHist, &degenerateExtrusions, nullptr);
  }
  const bool emptyGeom = st.userLinesFlat.empty() && st.userCirclesCxCyZR.empty() && st.userArcs.empty() &&
                         st.userPolylineVerts.empty() && st.cadAnnotations.empty() && st.userEllipses.empty() &&
                         st.cadMultileaders.empty() && st.cadBlockRefs.empty();
  // DXF decode often leaves BLOCK_HEADER.first_entity unset or pointing at BLOCK/ENDBLK only.
  if (emptyGeom) {
    for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
      Dwg_Object* o = &dwg.object[i];
      if (o->supertype != DWG_SUPERTYPE_ENTITY || o->tio.entity == nullptr)
        continue;
      if (o->tio.entity->entmode == 1)
        continue;
      const Dwg_Object_Type ty = o->fixedtype;
      if (ty == DWG_TYPE_VERTEX_2D || ty == DWG_TYPE_VERTEX_3D || ty == DWG_TYPE_VERTEX_MESH ||
          ty == DWG_TYPE_VERTEX_PFACE || ty == DWG_TYPE_VERTEX_PFACE_FACE || ty == DWG_TYPE_SEQEND ||
          ty == DWG_TYPE_ENDBLK || ty == DWG_TYPE_BLOCK)
        continue;
      if (ty == DWG_TYPE_POLYLINE_PFACE || ty == DWG_TYPE_POLYLINE_MESH || ty == DWG_TYPE__3DFACE)
        continue;
      ImportObject(st, &dwg, o, id, 0, &skipHist, &degenerateExtrusions, nullptr);
    }
  }
  if (st.cadMeshes.empty()) {
    for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
      Dwg_Object* o = &dwg.object[i];
      if (o->supertype != DWG_SUPERTYPE_ENTITY || o->tio.entity == nullptr)
        continue;
      if (o->tio.entity->entmode == 1)
        continue;
      const Dwg_Object_Type ty = o->fixedtype;
      if (ty != DWG_TYPE_POLYLINE_PFACE && ty != DWG_TYPE_POLYLINE_MESH && ty != DWG_TYPE__3DFACE)
        continue;
      ImportObject(st, &dwg, o, id, 0, &skipHist, &degenerateExtrusions, nullptr);
    }
  }

  ImportPaperLayoutsFromDwg(st, &dwg, &skipHist, &degenerateExtrusions);
  if (dwg.header.version >= R_2007)
    st.viewportVisualStyle = DwgImportModelVisualStyle(&dwg);
  ImportAnnotationScales(st, &dwg);
  const Dwg_Object* msForCannoscale = dwg_model_space_object(&dwg);
  if (msForCannoscale == nullptr || msForCannoscale->tio.object == nullptr ||
      !ApplyGosurveyCannoscaleFromEed(reinterpret_cast<Dwg_Object_Entity*>(msForCannoscale->tio.object),
                                    st))
    SyncCurrentAnnotationScaleIndex(st);

  if (!asDxf)
    DwgAnnotContextImportScan(&dwg);

  dwg_free(&dwg);

  CadCoord::MaybeRebaseLargeCoordinates(st, &log);
  const int fbW = std::max(st.viewportLastFbW, 1);
  const int fbH = std::max(st.viewportLastFbH, 1);
  const float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
  if (!CadCoord::FitViewportToDrawing(st, aspect, fbW, fbH))
    st.pendingZoomExtents = true;
  for (size_t i = 0; i < st.surveyPoints.size(); ++i)
    EnsureSurveyPointLabelMtext(st, i, nullptr);

  const size_t nLines = st.userLinesFlat.size() / 6;
  const size_t nCirc = st.userCirclesCxCyZR.size() / 4;
  const size_t nPoly = st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1;
  log.push_back((asDxf ? std::string("DXF") : std::string("DWG")) + " import — " + std::to_string(nLines) +
                " line(s), " + std::to_string(nCirc) + " circle(s), " + std::to_string(nPoly) + " polyline(s), " +
                std::to_string(st.userArcs.size()) + " arc(s).");
  if (degenerateExtrusions > 0) {
    log.push_back((asDxf ? std::string("DXF") : std::string("DWG")) +
                  " import — refused " + std::to_string(degenerateExtrusions) +
                  " ARC/CIRCLE record(s) whose extrusion is a zero-length vector.");
  }
  // REQ-366, issue #607: dimensions are now counted, not silently omitted, on import too.
  size_t nDimsImported = 0;
  for (const CadAnnotation& a : st.cadAnnotations)
    if (CadAnnotationIsDimension(a))
      ++nDimsImported;
  if (nDimsImported > 0)
    log.push_back((asDxf ? std::string("DXF") : std::string("DWG")) + " import — " +
                  std::to_string(nDimsImported) + " dimension(s) (REQ-366).");
  if (!st.cadFilledRegions.empty())
    log.push_back((asDxf ? std::string("DXF") : std::string("DWG")) + " import — " +
                  std::to_string(st.cadFilledRegions.size()) + " HATCH fill(s) (REQ-170, issue #608).");
  if (!st.paperLayouts.empty()) {
    size_t nVp = 0;
    for (const PaperLayout& pl : st.paperLayouts)
      nVp += pl.viewports.size();
    log.push_back((asDxf ? std::string("DXF") : std::string("DWG")) + " import — " +
                  std::to_string(st.paperLayouts.size()) + " paper layout(s), " +
                  std::to_string(nVp) + " viewport(s) (REQ-170, issue #610).");
  }
  int printed = 0;
  for (const auto& kv : skipHist) {
    if (printed >= 8)
      break;
    log.push_back("  skipped \"" + kv.first + "\" × " + std::to_string(kv.second));
    ++printed;
  }
  DwgMaterialImportAppendLog(log);
  DwgAnnotContextImportAppendLog(log);
  BumpCadGpuCache(st);
  return true;
}

std::vector<DwgExportLoss> ComputeDwgExportLosses(const AppCommandState& st) {
  return ComputeDwgExportLossesImpl(st);
}

bool ExportLibreCadFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log,
                        bool asDxf) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back(asDxf ? "DXF export — no path." : "DWG export — no path.");
    return false;
  }

  const Dwg_Version_Type libVer = LibreDwgVersionFromExport(st.dwgExportVersion);
  Dwg_Data* dwg = dwg_new_Document(libVer, /*imperial=*/0, /*loglevel=*/0);
  if (dwg == nullptr) {
    log.push_back("CAD export — LibreDWG could not create a drawing.");
    return false;
  }
  Dwg_Object_BLOCK_HEADER* hdr = ModelHeader(dwg);
  if (hdr == nullptr) {
    dwg_free(dwg);
    std::free(dwg);
    log.push_back("CAD export — missing model space.");
    return false;
  }
  // Drawing unit (REQ-022) and, with "Set drawing variables to match" (REQ-357), LUNITS / AUNITS.
  dwg->header_vars.INSUNITS = static_cast<BITCODE_BS>(st.drawingInsUnits);
  if (st.drawingSettings.setDrawingVariables) {
    dwg->header_vars.LUNITS = 2;
    dwg->header_vars.AUNITS = static_cast<BITCODE_BS>(DrawingAunitsCode(st.drawingSettings.angularUnits));
  }
  AppendSaveTrace("export: fill from state");
  FillFromState(st, dwg, hdr, log);
  if (!asDxf) {  // REQ-362 item 2: the map pin other programs read (DWG only)
    DwgGeoData geo;
    std::string why;
    if (!BuildDwgGeoData(st, &geo, &why))
      log.push_back("DWG export — no GEODATA written: " + why + ".");
    else if (!WriteDwgGeoData(dwg, geo))
      log.push_back("DWG export — LibreDWG could not add the GEODATA; the location is kept in GoSurvey's data only.");
    else {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "DWG export — GEODATA written: zone %s, marker at %.3f, %.3f.",
                    geo.coordinateSystemDefinition.c_str(), geo.designX, geo.designY);
      log.push_back(buf);
    }
  }
  LibreDwgLinkBlockEntities(dwg);  // issue #590: AutoCAD refuses LibreDWG's implicit last link
  WriteGosurveyCannoscaleOnModelSpace(st, dwg);
  AppendSaveTrace("export: encode to disk");

  bool ok = false;
  if (asDxf) {
    ok = WriteDxfFile(pathUtf8, dwg, log);
    if (ok)
      log.push_back("DXF export complete (LibreDWG ASCII).");
  } else {
    ok = WriteDwgFile(pathUtf8, dwg, log);
    if (ok) {
      log.push_back(std::string("DWG export complete: ") + DwgSaveVersionDisplayName(st.dwgExportVersion) +
                    " (" + DwgSaveVersionAcTag(st.dwgExportVersion) + ") via LibreDWG.");
    }
  }
  dwg_free(dwg);
  std::free(dwg);
  return ok;
}

