#include "LibreDwgCad.hpp"

#include "AcisSatParser.hpp"
#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "DxfColors.hpp"
#include "DwgIo.hpp"
#include "SurveyPoints.hpp"
#include "TextStyle.hpp"

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
};

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
  st.userLinesFlat.push_back(static_cast<float>(x0 - st.worldDocumentOriginX));
  st.userLinesFlat.push_back(static_cast<float>(y0 - st.worldDocumentOriginY));
  st.userLinesFlat.push_back(static_cast<float>(z0));
  st.userLinesFlat.push_back(static_cast<float>(x1 - st.worldDocumentOriginX));
  st.userLinesFlat.push_back(static_cast<float>(y1 - st.worldDocumentOriginY));
  st.userLinesFlat.push_back(static_cast<float>(z1));
  st.userLineAttrs.push_back(at);
}

void LocalCircle(AppCommandState& st, double cx, double cy, double r, double z, const EntityAttributes& at) {
  st.userCirclesCxCyZR.push_back(static_cast<float>(cx - st.worldDocumentOriginX));
  st.userCirclesCxCyZR.push_back(static_cast<float>(cy - st.worldDocumentOriginY));
  st.userCirclesCxCyZR.push_back(static_cast<float>(z));
  st.userCirclesCxCyZR.push_back(static_cast<float>(r));
  st.userCircleAttrs.push_back(at);
  PushCircleNormal(st.userCircleNormals);   // REQ-312: DWG extrusion not yet read
}

void ArcFromAngles(double a0, double a1, float* startRad, float* sweepRad) {
  double sweep = a1 - a0;
  if (std::fabs(sweep) < 1e-12)
    sweep = 2.0 * kPi;
  while (sweep < 0.0)
    sweep += 2.0 * kPi;
  while (sweep > 2.0 * kPi)
    sweep -= 2.0 * kPi;
  if (sweep < 1e-12)
    sweep = 2.0 * kPi;
  *startRad = static_cast<float>(a0);
  *sweepRad = static_cast<float>(sweep);
}

void LocalArc(AppCommandState& st, double cx, double cy, double r, double a0, double a1, double z,
              const EntityAttributes& at) {
  if (r <= 1e-12)
    return;
  CadArc arc{};
  arc.cx = static_cast<float>(cx - st.worldDocumentOriginX);
  arc.cy = static_cast<float>(cy - st.worldDocumentOriginY);
  arc.r = static_cast<float>(r);
  ArcFromAngles(a0, a1, &arc.startRad, &arc.sweepRad);
  arc.z = static_cast<float>(z);
  st.userArcs.push_back(arc);
  st.userArcAttrs.push_back(at);
}

void LocalPolyline(AppCommandState& st, const std::vector<double>& xyz, bool closed,
                   const EntityAttributes& at) {
  const size_t nv = xyz.size() / 3;
  if (nv < 2)
    return;
  const int base = st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.back();
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(base);
  for (size_t i = 0; i < nv; ++i) {
    st.userPolylineVerts.push_back(static_cast<float>(xyz[i * 3 + 0] - st.worldDocumentOriginX));
    st.userPolylineVerts.push_back(static_cast<float>(xyz[i * 3 + 1] - st.worldDocumentOriginY));
    st.userPolylineVerts.push_back(static_cast<float>(xyz[i * 3 + 2]));
  }
  st.userPolylineOffsets.push_back(base + static_cast<int>(nv));
  st.userPolylineClosed.push_back(closed ? uint8_t{1} : uint8_t{0});
  st.userPolylineAttrs.push_back(at);
}

void LocalText(AppCommandState& st, double x, double y, double z, double height, double rotRad,
               const std::string& text, CadAnnotation::Kind kind, const EntityAttributes& at) {
  CadAnnotation a{};
  a.kind = kind;
  a.insX = static_cast<float>(x - st.worldDocumentOriginX);
  a.insY = static_cast<float>(y - st.worldDocumentOriginY);
  a.insZ = static_cast<float>(z);
  const double mup = std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
  a.plottedHeightInches = static_cast<float>(height / mup);
  a.rotationRad = static_cast<float>(rotRad);
  a.text = text;
  st.cadAnnotations.push_back(std::move(a));
  st.cadAnnotationAttrs.push_back(at);
}

void ImportObject(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj, const Xf2& xf, int depth,
                  std::unordered_map<std::string, int>* skipHist);

/// REQ-320 / ADR-051 (GitHub issue #299): a `3DSOLID` entity's geometry is an ACIS record stream,
/// not lines/circles LibreDWG can hand back directly. `acis_data` is LibreDWG's already-decrypted
/// payload — SAT (v1, text) or SAB (v2+, binary), per `version` (DXF 70). This importer supports SAT
/// only (issue #301 tracks SAB); a SAB stream, or anything AcisSatParser refuses, is reported through
/// the same `NoteSkip` mechanism an unrecognized entity type already uses (REQ-201: never silent).
void ImportAcisSolid(AppCommandState& st, const Dwg_Entity__3DSOLID* sol, const Xf2& xf,
                     const EntityAttributes& at, std::unordered_map<std::string, int>* skipHist) {
  if (sol->acis_empty || sol->acis_data == nullptr) {
    NoteSkip(skipHist, "3DSOLID(empty)");
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
                   std::unordered_map<std::string, int>* skipHist) {
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
    ImportObject(st, dwg, e, child, depth + 1, skipHist);
}

void ImportObject(AppCommandState& st, Dwg_Data* dwg, Dwg_Object* obj, const Xf2& xf, int depth,
                  std::unordered_map<std::string, int>* skipHist) {
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
    double cx = 0, cy = 0;
    xf.apply(e->center.x, e->center.y, &cx, &cy);
    const double sc = std::max(std::fabs(xf.sx), std::fabs(xf.sy));
    LocalCircle(st, cx, cy, e->radius * sc, e->center.z, at);
    return;
  }
  if (ty == DWG_TYPE_ARC && ent->tio.ARC != nullptr) {
    const Dwg_Entity_ARC* e = ent->tio.ARC;
    double cx = 0, cy = 0;
    xf.apply(e->center.x, e->center.y, &cx, &cy);
    const double sc = std::max(std::fabs(xf.sx), std::fabs(xf.sy));
    LocalArc(st, cx, cy, e->radius * sc, e->start_angle + xf.ang, e->end_angle + xf.ang, e->center.z, at);
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
    el.cx = static_cast<float>(cx - st.worldDocumentOriginX);
    el.cy = static_cast<float>(cy - st.worldDocumentOriginY);
    el.majVx = static_cast<float>(e->sm_axis.x * xf.sx);
    el.majVy = static_cast<float>(e->sm_axis.y * xf.sy);
    el.ratio = static_cast<float>(e->axis_ratio);
    el.z = static_cast<float>(e->center.z);
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
    LocalPolyline(st, xyz, closed, at);
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
  if (ty == DWG_TYPE_INSERT) {
    ExplodeInsert(st, dwg, ent, depth, skipHist);
    return;
  }
  if (ty == DWG_TYPE__3DSOLID && ent->tio._3DSOLID != nullptr) {
    ImportAcisSolid(st, ent->tio._3DSOLID, xf, at, skipHist);
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
    row.on = ly->on != 0;
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
  std::unordered_map<std::string, Dwg_Object*> layers;   // lower(name) -> LAYER object
  std::unordered_map<std::string, Dwg_Object*> ltypes;   // lower(name) -> LTYPE object

  static bool IsPlainLinetype(const std::string& n) {
    const std::string l = LowerAscii(n);
    return l.empty() || l == "continuous" || l == "bylayer" || l == "byblock";
  }

  Dwg_Object* EnsureLtype(const std::string& name) {
    if (IsPlainLinetype(name))
      return nullptr;
    const std::string key = LowerAscii(name);
    auto it = ltypes.find(key);
    if (it != ltypes.end())
      return it->second;
    Dwg_Object_LTYPE* lt = dwg_add_LTYPE(dwg, name.c_str());
    Dwg_Object* o = lt != nullptr ? &dwg->object[lt->parent->objid] : nullptr;
    ltypes[key] = o;
    return o;
  }

  BITCODE_H Ref(Dwg_Object* o) {
    return o != nullptr ? dwg_add_handleref(dwg, 5, o->handle.value, o) : nullptr;
  }

  void BuildLayerTable(const AppCommandState& st) {
    for (const CadLayerRow& row : st.drawingLayerTable) {
      if (row.name.empty() || LowerAscii(row.name) == "0")
        continue;
      Dwg_Object_LAYER* ly = dwg_add_LAYER(dwg, row.name.c_str());
      if (ly == nullptr)
        continue;
      uint32_t rgb = 0;
      const int aci = DxfColorStringToRgbPacked(row.color, &rgb) ? DxfNearestAciFromRgbPacked(rgb) : 7;
      ly->color.index = static_cast<BITCODE_BSd>(row.on ? aci : -aci);
      ly->color.method = 0xc2;
      ly->color.rgb = 0;
      ly->on = row.on ? 1 : 0;
      ly->frozen = row.frozen ? 1 : 0;
      ly->locked = row.locked ? 1 : 0;
      ly->flag0 = static_cast<BITCODE_BS>((row.frozen ? 1 : 0) | (row.on ? 2 : 0) |
                                         (row.locked ? 8 : 0) | 16);
      if (Dwg_Object* lt = EnsureLtype(row.linetype))
        ly->ltype = Ref(lt);
      layers[LowerAscii(row.name)] = &dwg->object[ly->parent->objid];
    }
  }

  void Apply(Dwg_Object_Entity* ent, const EntityAttributes& a) {
    if (ent == nullptr)
      return;
    if (!a.layer.empty() && LowerAscii(a.layer) != "0") {
      auto it = layers.find(LowerAscii(a.layer));
      if (it != layers.end() && it->second != nullptr)
        ent->layer = Ref(it->second);
    }
    if (a.color == "ByBlock") {
      ent->color.index = 0;
      ent->color.method = 0xc1;
    } else if (a.color.empty() || a.color == "ByLayer") {
      ent->color.index = 256;
      ent->color.method = 0xc0;
    } else {
      uint32_t rgb = 0;
      if (DxfColorStringToRgbPacked(a.color, &rgb)) {
        ent->color.index = static_cast<BITCODE_BSd>(DxfNearestAciFromRgbPacked(rgb));
        ent->color.method = 0xc2;
      }
    }
    if (Dwg_Object* lt = EnsureLtype(a.linetype)) {
      ent->ltype = Ref(lt);
      ent->ltype_flags = 3;  // has explicit handle
    }
  }
};

const EntityAttributes* AttrAt(const std::vector<EntityAttributes>& v, size_t i) {
  return i < v.size() ? &v[i] : nullptr;
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
  // Register every linetype the entities reference up front, so no LTYPE table object is created
  // after the entity records have started going into the object array.
  for (const std::vector<EntityAttributes>* v :
       {&st.userLineAttrs, &st.userCircleAttrs, &st.userArcAttrs, &st.userPolylineAttrs,
        &st.cadAnnotationAttrs, &st.userEllAttrs}) {
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
    Dwg_Entity_CIRCLE* e = dwg_add_CIRCLE(hdr, &c, static_cast<double>(st.userCirclesCxCyZR[i * 4 + 3]));
    if (e != nullptr)
      apply(e->parent, AttrAt(st.userCircleAttrs, i));
  }
  for (size_t i = 0; i < st.userArcs.size(); ++i) {
    const CadArc& arc = st.userArcs[i];
    dwg_point_3d c{};
    world(arc.cx, arc.cy, arc.z, &c);
    const double a0 = static_cast<double>(arc.startRad);
    const double a1 = a0 + static_cast<double>(arc.sweepRad);
    Dwg_Entity_ARC* e = dwg_add_ARC(hdr, &c, static_cast<double>(arc.r), a0, a1);
    if (e != nullptr)
      apply(e->parent, AttrAt(st.userArcAttrs, i));
  }
  for (size_t i = 0; i + 1 < st.userPolylineOffsets.size(); ++i) {
    const int a = st.userPolylineOffsets[i];
    const int b = st.userPolylineOffsets[i + 1];
    const int nv = b - a;
    if (nv < 2)
      continue;
    std::vector<dwg_point_2d> pts(static_cast<size_t>(nv));
    for (int v = 0; v < nv; ++v) {
      const int k = (a + v) * 3;
      pts[static_cast<size_t>(v)].x = static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k)]) +
                                      st.worldDocumentOriginX;
      pts[static_cast<size_t>(v)].y = static_cast<double>(st.userPolylineVerts[static_cast<size_t>(k + 1)]) +
                                      st.worldDocumentOriginY;
    }
    Dwg_Entity_LWPOLYLINE* lw = dwg_add_LWPOLYLINE(hdr, nv, pts.data());
    if (lw != nullptr && i < st.userPolylineClosed.size() && st.userPolylineClosed[i])
      lw->flag = static_cast<BITCODE_BS>(lw->flag | 512);
    if (lw != nullptr)
      apply(lw->parent, AttrAt(st.userPolylineAttrs, i));
  }
  for (size_t i = 0; i < st.cadAnnotations.size(); ++i) {
    const CadAnnotation& an = st.cadAnnotations[i];
    if (an.surveyPointLabelForId >= 0)
      continue;
    dwg_point_3d p{};
    world(an.insX, an.insY, an.insZ, &p);
    const double h = static_cast<double>(an.plottedHeightInches) *
                     std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
    const EntityAttributes* at = AttrAt(st.cadAnnotationAttrs, i);
    if (an.kind == CadAnnotation::Kind::Mtext) {
      Dwg_Entity_MTEXT* e = dwg_add_MTEXT(hdr, &p, std::max(h * 10.0, 1.0), an.text.c_str());
      if (e != nullptr)
        apply(e->parent, at);
    } else if (an.kind == CadAnnotation::Kind::Text) {
      Dwg_Entity_TEXT* e = dwg_add_TEXT(hdr, an.text.c_str(), &p, h);
      if (e != nullptr)
        apply(e->parent, at);
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
  if (!st.cadFilledRegions.empty())
    log.push_back("CAD export — skipped " + std::to_string(st.cadFilledRegions.size()) +
                  " HATCH region(s).");
  if (!st.cadMeshes.empty())
    log.push_back("CAD export — skipped mesh(es); not written to DXF/DWG.");
  if (!st.cadSurfaces.empty())
    log.push_back("CAD export — skipped TIN surface(s); not written to DXF/DWG.");
  // B-rep solids (ADR-045 (i)). Named and counted, never dropped in silence (REQ-201). A real solid
  // in DXF/DWG is an ACIS 3DSOLID — a proprietary binary B-rep GoSurvey cannot write without a
  // third-party kernel REQ-300 does not permit — and writing a tessellated approximation instead
  // would hand the user a picture of their solid that round-trips back as an uneditable bag of
  // triangles with an approximate volume. The same boundary ADR-026 (c) drew for meshes.
  if (!st.cadSolids.empty())
    log.push_back("CAD export — skipped " + std::to_string(st.cadSolids.size()) +
                  " solid(s); DXF/DWG has no lossless representation for them (ADR-045).");
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
  const std::filesystem::path dst(pathUtf8);
  const std::filesystem::path tmp = dst.string() + ".gosurvey-tmp.dwg";
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
  const int err = dwg_write_file(tmp.string().c_str(), dwg);
  if (err != DWG_NOERR) {
    log.push_back("DWG export — LibreDWG encode failed.");
    std::filesystem::remove(tmp, ec);
    return false;
  }
  std::filesystem::rename(tmp, dst, ec);
  if (ec) {
    std::filesystem::copy_file(tmp, dst, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(tmp, ec);
    if (ec) {
      log.push_back("DWG export failed: could not write " + dst.string() + ".");
      return false;
    }
  }
  return true;
}

}  // namespace

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

  if (dwg.header_vars.INSUNITS == 0 || dwg.header_vars.INSUNITS == 2 || dwg.header_vars.INSUNITS == 6)
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

  std::unordered_map<std::string, int> skipHist;
  const Xf2 id{};
  Dwg_Object* mspace = dwg_model_space_object(&dwg);
  if (mspace != nullptr) {
    for (Dwg_Object* e = get_first_owned_entity(mspace); e != nullptr; e = get_next_owned_entity(mspace, e))
      ImportObject(st, &dwg, e, id, 0, &skipHist);
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
      ImportObject(st, &dwg, o, id, 0, &skipHist);
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
  FillFromState(st, dwg, hdr, log);

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
