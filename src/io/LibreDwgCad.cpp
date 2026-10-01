#include "LibreDwgCad.hpp"

#include "AcisSatParser.hpp"
#include "CadCommands.hpp"
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
#include "util/SaveTrace.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

EntityAttributes AttrFromEnt(Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  EntityAttributes a{};
  a.layer = LayerName(dwg, ent);
  if (ent != nullptr) {
    a.color = ColorStorage(ent->color);
    a.linetype = EntityLinetypeName(dwg, ent);
  }
  return a;
}

void NoteSkip(std::unordered_map<std::string, int>* hist, const char* name) {
  if (hist == nullptr || name == nullptr)
    return;
  ++(*hist)[name];
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

void LocalText(AppCommandState& st, double x, double y, double z, double height, double rotRad,
               const std::string& text, CadAnnotation::Kind kind, const EntityAttributes& at) {
  CadAnnotation a{};
  a.kind = kind;
  a.insX = x - st.worldDocumentOriginX;
  a.insY = y - st.worldDocumentOriginY;
  a.insZ = z;
  const double mup = std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
  a.plottedHeightInches = static_cast<float>(height / mup);
  a.rotationRad = static_cast<float>(rotRad);
  a.text = text;
  st.cadAnnotations.push_back(std::move(a));
  st.cadAnnotationAttrs.push_back(at);
}

void ImportObject(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj, const Xf2& xf, int depth,
                  std::unordered_map<std::string, int>* skipHist,
                  int* degenerateExtrusions = nullptr);

/// REQ-320 / ADR-051 (GitHub issue #299): a `3DSOLID` entity's geometry is an ACIS record stream,
/// not lines/circles LibreDWG can hand back directly. `acis_data` is LibreDWG's already-decrypted
/// payload — SAT (v1, text) or SAB (v2+, binary), per `version` (DXF 70). This importer supports SAT
/// only (issue #301 tracks SAB); a SAB stream, or anything AcisSatParser refuses, is reported through
/// the same `NoteSkip` mechanism an unrecognized entity type already uses (REQ-201: never silent).
void ImportAcisSolid(AppCommandState& st, const Dwg_Data* dwg, const Dwg_Entity__3DSOLID* sol,
                     const Xf2& xf, const EntityAttributes& at,
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
  st.cadSolidAttrs.push_back(at);
}

void ExplodeInsert(AppCommandState& st, Dwg_Data* dwg, Dwg_Object_Entity* ent, int depth,
                   std::unordered_map<std::string, int>* skipHist,
                   int* degenerateExtrusions) {
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
    ImportObject(st, dwg, e, child, depth + 1, skipHist, degenerateExtrusions);
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
    ImportObject(st, dwg, e, identity, depth + 1, skipHist, degenerateExtrusions);
}

// REQ-366 statement 3: Aligned / Linear (rotated or orthogonal) / 3-point Angular read back into
// the matching CadAnnotation::Kind, points and measured value from the DIMENSION's OWN fields (not
// the *D block). Returns false if the geometry is degenerate (e.g. coincident extension points),
// in which case the caller falls back to importing the block's drawn geometry like any unsupported
// subtype, rather than dropping the dimension silently.
bool ImportSupportedDimension(AppCommandState& st, Dwg_Data* dwg, const Xf2& xf, Dwg_Object_Type ty,
                              const Dwg_DIMENSION_common* common, const BITCODE_3BD* xline1,
                              const BITCODE_3BD* xline2, const BITCODE_3BD* centerOrDefPt,
                              double dimRotation, const EntityAttributes& at) {
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
  st.cadAnnotations.push_back(a);
  st.cadAnnotationAttrs.push_back(at);
  return true;
}

// REQ-170 / issue #608: map a decoded HATCH boundary into CadFilledRegion (solid or pattern).
bool ImportHatchEntity(AppCommandState& st, Dwg_Data* dwg, const Dwg_Entity_HATCH* h, const Xf2& xf,
                       const EntityAttributes& at) {
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

  st.cadFilledRegions.push_back(std::move(region));
  st.cadFilledRegionAttrs.push_back(at);
  return true;
}

void ImportObject(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj, const Xf2& xf, int depth,
                  std::unordered_map<std::string, int>* skipHist,
                  int* degenerateExtrusions) {
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
    LocalText(st, x, y, e->elevation, e->height, e->rotation + xf.ang, FromT(dwg, e->text_value),
              CadAnnotation::Kind::Text, at);
    return;
  }
  if (ty == DWG_TYPE_MTEXT && ent->tio.MTEXT != nullptr) {
    const Dwg_Entity_MTEXT* e = ent->tio.MTEXT;
    double x = 0, y = 0;
    xf.apply(e->ins_pt.x, e->ins_pt.y, &x, &y);
    const double rot = std::atan2(e->x_axis_dir.y, e->x_axis_dir.x);
    LocalText(st, x, y, e->ins_pt.z, e->text_height, rot + xf.ang, FromT(dwg, e->text),
              CadAnnotation::Kind::Mtext, at);
    return;
  }
  if (ty == DWG_TYPE_POINT && ent->tio.POINT != nullptr) {
    const Dwg_Entity_POINT* e = ent->tio.POINT;
    double px = 0, py = 0;
    xf.apply(e->x, e->y, &px, &py);
    const double arm = 0.01;
    LocalLine(st, px - arm, py, e->z, px, py, e->z, at);
    LocalLine(st, px, py, e->z, px + arm, py, e->z, at);
    LocalLine(st, px, py - arm, e->z, px, py, e->z, at);
    LocalLine(st, px, py, e->z, px, py + arm, e->z, at);
    return;
  }
  if (ty == DWG_TYPE_HATCH && ent->tio.HATCH != nullptr) {
    const Dwg_Entity_HATCH* h = ent->tio.HATCH;
    if (h->is_gradient_fill != 0) {
      NoteSkip(skipHist, "HATCH(gradient fill requires R2004+, issue #600)");
      return;
    }
    if (ImportHatchEntity(st, dwg, h, xf, at))
      return;
    NoteSkip(skipHist, "HATCH(degenerate or unsupported boundary)");
    return;
  }
  if (ty == DWG_TYPE_INSERT) {
    ExplodeInsert(st, dwg, ent, depth, skipHist, degenerateExtrusions);
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
    if (!ImportSupportedDimension(st, dwg, xf, ty, common, xline1, xline2, centerOrDef, rot, at))
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
    ImportAcisSolid(st, dwg, ent->tio._3DSOLID, xf, at, skipHist);
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

// Builds the DWG LAYER and LTYPE tables from the GoSurvey layer table and wires each exported
// entity to its layer / colour / linetype (issue #140 / DEBT-151-b — the DWG writer previously
// emitted geometry only, so a saved drawing lost every layer).
struct TableWriter {
  Dwg_Data* dwg = nullptr;
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
      uint32_t rgb = 0;
      const int aci = DxfColorStringToRgbPacked(row.color, &rgb) ? DxfNearestAciFromRgbPacked(rgb) : 7;
      ly->color.index = static_cast<BITCODE_BSd>(row.on ? aci : -aci);
      ly->color.method = DWG_COLOR_METHOD_ACI;
      ly->color.rgb = 0;
      ly->off = row.on ? 0 : 1;
      ly->frozen = row.frozen ? 1 : 0;
      ly->locked = row.locked ? 1 : 0;
      ly->flag0 = static_cast<BITCODE_BS>((row.frozen ? 1 : 0) | (row.on ? 2 : 0) |
                                         (row.locked ? 8 : 0) | 16);
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
    if (a.color == "ByBlock") {
      ent->color.index = 0;
      ent->color.method = DWG_COLOR_METHOD_BYBLOCK;
    } else if (a.color.empty() || a.color == "ByLayer") {
      ent->color.index = 256;
      ent->color.method = DWG_COLOR_METHOD_BYLAYER;
    } else {
      uint32_t rgb = 0;
      if (DxfColorStringToRgbPacked(a.color, &rgb)) {
        ent->color.index = static_cast<BITCODE_BSd>(DxfNearestAciFromRgbPacked(rgb));
        ent->color.method = DWG_COLOR_METHOD_ACI;
      }
    }
    if (Dwg_Object* lt = EnsureLtype(a.linetype)) {
      ent->ltype = Ref(lt);
      ent->ltype_flags = 3;  // has explicit handle
    }
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
// correctly without pulling in every model-space refinement. Nested blocks, meshes and solids
// inside a block are not written (same degradations FillFromState already discloses for model
// space, via the #614 loss summary).
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
        apply(e->parent, at);
      }
    } else if (an.kind == CadAnnotation::Kind::Text) {
      if (Dwg_Entity_TEXT* e = dwg_add_TEXT(blkhdr, SanitizeDwgTextSymbols(an.text).c_str(), &p,
                                            std::max(static_cast<double>(an.plottedHeightInches), 1e-3))) {
        e->rotation = static_cast<double>(an.rotationRad);
        apply(e->parent, at);
      }
    }
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
void DimStyleSetCmc(BITCODE_CMC* out, const std::string& storage) {
  if (out == nullptr)
    return;
  if (storage.empty() || storage == "ByLayer" || storage == "ByBlock") {
    out->index = 256;
    out->method = DWG_COLOR_METHOD_BYLAYER;
    return;
  }
  uint32_t rgb = 0;
  if (DxfColorStringToRgbPacked(storage, &rgb)) {
    out->index = static_cast<BITCODE_BSd>(DxfNearestAciFromRgbPacked(rgb));
    out->method = DWG_COLOR_METHOD_ACI;
  }
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
  add("table(s)", st.cadTables.size());
  add("pipe run(s)", st.cadPipeRuns.size());
  // REQ-366, issue #607: dimensions now write as real DIMSTYLE/DIMENSION_* objects (see
  // FillFromState) — "dimension(s)" removed from this loss list the same way #631 removed "block
  // reference(s)" once blocks got a real writer.

  // Entity classes GoSurvey already refuses on principle (ADR-042/045/026) — counted and logged,
  // now also surfaced in the pre-export warning so the user sees them before committing to it.
  add("mesh(es)", st.cadMeshes.size());
  add("point cloud(s)", st.cadPointClouds.size());
  add("TIN surface(s)", st.cadSurfaces.size());
  add("solid(s)", st.cadSolids.size());

  // Degradations: the entity IS written, but a property on it is not.
  const std::vector<EntityAttributes>* attrSets[] = {&st.userLineAttrs, &st.userCircleAttrs,
                                                      &st.userArcAttrs,  &st.userPolylineAttrs,
                                                      &st.cadAnnotationAttrs, &st.userEllAttrs,
                                                      &st.cadFilledRegionAttrs};
  size_t nColorRounded = 0, nLineweight = 0, nTransparency = 0;
  for (const std::vector<EntityAttributes>* v : attrSets) {
    for (const EntityAttributes& a : *v) {
      uint32_t rgb = 0;
      if (DxfColorStringToRgbPacked(a.color, &rgb)) {
        const int aci = DxfNearestAciFromRgbPacked(rgb);
        if ((DxfRgbPackedFromAci(aci) & 0xFFFFFFu) != (rgb & 0xFFFFFFu))
          ++nColorRounded;
      }
      if (a.lineweightMm >= 0.f)
        ++nLineweight;
      if (a.transparency >= 0.f)
        ++nTransparency;
    }
  }
  add("colour(s) (rounded to the nearest AutoCAD index colour)", nColorRounded);
  add("object(s) with a lineweight (not written; falls back to ByLayer)", nLineweight);
  add("object(s) with transparency (not written)", nTransparency);

  size_t nRotatedText = 0;
  for (const CadAnnotation& a : st.cadAnnotations)
    if ((a.kind == CadAnnotation::Kind::Text || a.kind == CadAnnotation::Kind::Mtext) &&
        std::fabs(a.rotationRad) > 1e-6f)
      ++nRotatedText;
  add("rotated text/mtext label(s) (rotation not written)", nRotatedText);

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

  return out;
}

void FillFromState(const AppCommandState& st, Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* hdr,
                   std::vector<std::string>& log) {
  auto world = [&](float lx, float ly, double z, dwg_point_3d* p) {
    p->x = static_cast<double>(lx) + st.worldDocumentOriginX;
    p->y = static_cast<double>(ly) + st.worldDocumentOriginY;
    p->z = z;
  };

  TableWriter tw;
  tw.dwg = dwg;
  tw.BuildLayerTable(st);
  tw.BuildStyleTable(st, st.modelUnitsPerPlottedInch);
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

  const size_t nSeg = st.userLinesFlat.size() / 6;
  for (size_t i = 0; i < nSeg; ++i) {
    dwg_point_3d a{}, b{};
    world(st.userLinesFlat[i * 6 + 0], st.userLinesFlat[i * 6 + 1], st.userLinesFlat[i * 6 + 2], &a);
    world(st.userLinesFlat[i * 6 + 3], st.userLinesFlat[i * 6 + 4], st.userLinesFlat[i * 6 + 5], &b);
    Dwg_Entity_LINE* e = dwg_add_LINE(hdr, &a, &b);
    if (e != nullptr)
      apply(e->parent, AttrAt(st.userLineAttrs, i));
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
      apply(e->parent, AttrAt(st.userCircleAttrs, i));
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
      apply(e->parent, AttrAt(st.userArcAttrs, i));
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
          DimStyleSetCmc(&dimStyle->DIMCLRD, sty.dimLineColor);
          DimStyleSetCmc(&dimStyle->DIMCLRE, sty.extLineColor);
          DimStyleSetCmc(&dimStyle->DIMCLRT, sty.textColor);
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
      std::string wire;
      for (char ch : MtextRichFlattenToPlain(SanitizeDwgTextSymbols(an.text))) {
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
        if (styleId != static_cast<BITCODE_BL>(-1))
          e->style = tw.RefObjId(styleId);
        apply(e->parent, at);
      }
    } else if (an.kind == CadAnnotation::Kind::Text) {
      dwg_point_3d p{};
      world(an.insX, an.insY, an.insZ, &p);
      Dwg_Entity_TEXT* e = dwg_add_TEXT(hdr, SanitizeDwgTextSymbols(an.text).c_str(), &p, h);
      if (e != nullptr) {
        e->rotation = static_cast<double>(an.rotationRad);
        if (styleId != static_cast<BITCODE_BL>(-1))
          e->style = tw.RefObjId(styleId);
        apply(e->parent, at);
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
  for (const CadBlockDefinition& def : st.blockDefs) {
    if (def.name.empty())
      continue;
    Dwg_Object_BLOCK_HEADER* bh = dwg_add_BLOCK_HEADER(dwg, def.name.c_str());
    if (bh == nullptr)
      continue;
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
    dwg_add_ENDBLK(bh);
    blockDefByName[def.name] = &def;
  }
  for (size_t i = 0; i < st.cadBlockRefs.size(); ++i) {
    const CadBlockRef& ref = st.cadBlockRefs[i];
    auto defIt = blockDefByName.find(ref.defName);
    if (defIt == blockDefByName.end())
      continue;  // definition missing or failed to write; REQ-201 covered by the #614 loss summary
    dwg_point_3d ins{};
    world(ref.xf.x, ref.xf.y, static_cast<double>(ref.xf.z), &ins);
    // GoSurvey's block transform is a single Z rotation (CadBlockXform has no X/Y rotation, nor
    // does dwg_add_INSERT take one); non-uniform XYZ scale carries through directly.
    Dwg_Entity_INSERT* e0 = dwg_add_INSERT(hdr, &ins, ref.defName.c_str(), static_cast<double>(ref.xf.sx),
                                           static_cast<double>(ref.xf.sy), static_cast<double>(ref.xf.sz),
                                           static_cast<double>(ref.xf.rotZ));
    if (e0 == nullptr)
      continue;
    const EntityAttributes* at = AttrAt(st.cadBlockRefAttrs, i);
    if (ref.attributes.empty()) {
      apply(e0->parent, at);
      continue;
    }
    const BITCODE_H defRef = dwg_find_tablehandle(dwg, ref.defName.c_str(), "BLOCK");
    for (const CadBlockAttrValue& av : ref.attributes) {
      if (av.tag.empty())
        continue;
      const CadBlockAttrDef* ad = nullptr;
      for (const CadBlockAttrDef& d : defIt->second->attrDefs)
        if (d.tag == av.tag) { ad = &d; break; }
      dwg_point_3d ap = ins;
      const double h = ad != nullptr ? std::max(static_cast<double>(ad->height), 1e-3) : 0.125;
      dwg_add_ATTRIB(e0, h, 0, &ap, av.tag.c_str(), av.value.c_str());
    }
    // dwg_add_ATTRIB (LibreDWG 0.13.4) overwrites Dwg_Entity_INSERT::block_header — DXF 2, the
    // referenced block — with the INSERT's OWNER block handle (dwg_entity_owner) on every call,
    // instead of leaving it alone (same issue #605/D-2026-09-30-f finding as the survey-point
    // block below). Restored here or an INSERT with attributes silently ends up "referencing" the
    // space it lives in rather than its real definition.
    if (defRef != nullptr)
      e0->block_header = dwg_add_handleref(dwg, 5, defRef->absolute_ref, nullptr);
    apply(e0->parent, at);
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
    apply(hatch->parent, fi < st.cadFilledRegionAttrs.size() ? &st.cadFilledRegionAttrs[fi] : nullptr);
    ++nHatchOut;
  }
  if (nHatchOut > 0)
    log.push_back("CAD export — wrote " + std::to_string(nHatchOut) + " HATCH(es) (REQ-170, issue #608).");

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
  geo->coord_type = 0;
  geo->design_pt = {g.designX, g.designY, 0.0};
  geo->ref_pt = {g.refY, g.refX, 0.0};  // (latitude, longitude)
  geo->obs_pt = {0.0, 0.0, 0.0};
  geo->up_dir = {0.0, 0.0, 1.0};
  geo->north_dir = {g.northX, g.northY};
  geo->north_dir_angle_deg = northRad;  // radians, whatever LibreDWG's field name says
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
      ImportObject(st, &dwg, e, id, 0, &skipHist, &degenerateExtrusions);
  }
  const bool emptyGeom = st.userLinesFlat.empty() && st.userCirclesCxCyZR.empty() && st.userArcs.empty() &&
                         st.userPolylineVerts.empty() && st.cadAnnotations.empty() && st.userEllipses.empty();
  // DXF decode often leaves BLOCK_HEADER.first_entity unset or pointing at BLOCK/ENDBLK only.
  if (emptyGeom) {
    for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
      Dwg_Object* o = &dwg.object[i];
      if (o->supertype != DWG_SUPERTYPE_ENTITY || o->tio.entity == nullptr)
        continue;
      if (o->tio.entity->entmode == 1)
        continue;
      const Dwg_Object_Type ty = o->fixedtype;
      if (ty == DWG_TYPE_VERTEX_2D || ty == DWG_TYPE_VERTEX_3D || ty == DWG_TYPE_SEQEND ||
          ty == DWG_TYPE_ENDBLK || ty == DWG_TYPE_BLOCK)
        continue;
      ImportObject(st, &dwg, o, id, 0, &skipHist, &degenerateExtrusions);
    }
  }

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
  int printed = 0;
  for (const auto& kv : skipHist) {
    if (printed >= 8)
      break;
    log.push_back("  skipped \"" + kv.first + "\" × " + std::to_string(kv.second));
    ++printed;
  }
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

  Dwg_Data* dwg = dwg_new_Document(R_2000, /*imperial=*/0, /*loglevel=*/0);
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
  AppendSaveTrace("export: encode to disk");

  bool ok = false;
  if (asDxf) {
    ok = WriteDxfFile(pathUtf8, dwg, log);
    if (ok)
      log.push_back("DXF export complete (LibreDWG ASCII).");
  } else {
    ok = WriteDwgFile(pathUtf8, dwg, log);
    if (ok)
      log.push_back("DWG export complete: R2000 (AC1015) via LibreDWG.");
  }
  dwg_free(dwg);
  std::free(dwg);
  return ok;
}
