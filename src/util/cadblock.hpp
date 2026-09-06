#pragma once

/// Named block definitions and lightweight INSERT references (GitHub issue #124).
/// Header-only so Catch2 can cover transforms, nesting, and cycle detection without GL.

#include "CadEntities.hpp"
#include "cadsolid.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

inline constexpr int kCadBlockMaxNest = 32;
inline constexpr int kCadBlockMaxWalk = 65536;

/// Model-space annotation overlay is gated on having something to paint. Block INSERT labels live
/// in \c cadBlockRefs, not \c cadAnnotations — omitting refs skipped every matchline/attribute draw
/// on a drawing that had no other TEXT/MTEXT/tables.
[[nodiscard]] inline bool CadNeedsAnnotationOverlay(size_t annotationCount, size_t tableCount,
                                                    size_t blockRefCount, bool showMtextDraft,
                                                    bool showDimDraft) {
  return annotationCount != 0 || tableCount != 0 || blockRefCount != 0 || showMtextDraft || showDimDraft;
}

struct CadBlockXform {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float sx = 1.f;
  float sy = 1.f;
  float sz = 1.f;
  float rotX = 0.f;
  float rotY = 0.f;
  float rotZ = 0.f;
};

struct CadBlockAttrDef {
  std::string tag;
  std::string prompt;
  std::string defaultValue;
  float localX = 0.f;
  float localY = 0.f;
  float localZ = 0.f;
  float height = 0.125f;
  float rotationRad = 0.f;
};

struct CadBlockAttrValue {
  std::string tag;
  std::string value;
};

enum class CadBlockParamKind : std::uint8_t {
  Linear = 0,
  Polar,
  Rotation,
  Flip,
  Visibility,
  Move,
  Lookup
};

struct CadBlockParameter {
  std::string name;
  CadBlockParamKind kind = CadBlockParamKind::Linear;
  float value = 0.f;
  float minValue = 0.f;
  float maxValue = 1.e9f;
  std::vector<std::string> lookupKeys;
  std::vector<float> lookupValues;
};

enum class CadBlockActionKind : std::uint8_t {
  Stretch = 0,
  Move,
  Rotate,
  Scale,
  Flip,
  Visibility
};

struct CadBlockAction {
  CadBlockActionKind kind = CadBlockActionKind::Stretch;
  std::string paramName;
  float originX = 0.f;
  float originY = 0.f;
  float dirX = 1.f;
  float dirY = 0.f;
  float threshold = 0.f;
  /// Empty = every primitive. `"geom"` = lines/arcs/circles. `"labels"` = TEXT/MTEXT/ATTDEF.
  std::string applyTo;
  /// Empty = every label. `"SHEET"` / `"NORTH"` select matchline label groups.
  std::string labelGroup;
};

struct CadBlockNested {
  std::string defName;
  CadBlockXform xf;
  std::string visState;
};

struct CadBlockContent {
  std::vector<float> lines;
  std::vector<EntityAttributes> lineAttrs;
  std::vector<std::string> lineVis;
  std::vector<float> circles;
  std::vector<EntityAttributes> circleAttrs;
  std::vector<std::string> circleVis;
  /// Plane normal per circle, 3 floats each (REQ-312) - the block-definition counterpart of
  /// AppCommandState::userCircleNormals, and a third parallel array beside circleAttrs/circleVis
  /// exactly as those two already are. Stored so that BLOCK and BEDIT cannot silently flatten a
  /// tilted circle on the way in or out (REQ-201).
  std::vector<float> circleNormals;
  std::vector<CadArc> arcs;
  std::vector<EntityAttributes> arcAttrs;
  std::vector<CadEllipse> ellipses;
  std::vector<EntityAttributes> ellAttrs;
  std::vector<int> polyOffsets;
  std::vector<float> polyVerts;
  /// REQ-316 / ADR-047: per-vertex bulge, parallel to polyVerts (size()/3). Empty on a legacy
  /// block definition, which reads as an all-straight polyline.
  std::vector<float> polyVertsBulge;
  std::vector<std::uint8_t> polyClosed;
  std::vector<EntityAttributes> polyAttrs;
  std::vector<CadAnnotation> texts;
  std::vector<EntityAttributes> textAttrs;
  std::vector<CadBlockNested> nested;
  std::vector<std::shared_ptr<const CadMesh>> meshes;
  std::vector<EntityAttributes> meshAttrs;
  /// REQ-320 / ADR-051: B-rep solids (native or ACIS-imported), so INSERT/WBLOCK/BLOCKIMPORT round-
  /// trip a 3D-solid block the same way \ref meshes already round-trips a mesh block.
  std::vector<CadSolidPtr> solids;
  std::vector<EntityAttributes> solidAttrs;
};

struct CadBlockDefinition {
  std::uint64_t id = 0;
  std::string name;
  std::string description;
  float baseX = 0.f;
  float baseY = 0.f;
  float baseZ = 0.f;
  std::string units = "unitless";
  CadBlockContent content;
  std::vector<CadBlockAttrDef> attrDefs;
  std::vector<CadBlockParameter> parameters;
  std::vector<CadBlockAction> actions;
  std::vector<std::string> visibilityStates;
  std::string metadata;
};

struct CadBlockRef {
  std::string defName;
  CadBlockXform xf;
  std::vector<CadBlockAttrValue> attributes;
  std::vector<CadBlockParameter> paramState;
  std::string visState;
};

struct CadBlockWorldSeg {
  float x0 = 0.f, y0 = 0.f, z0 = 0.f;
  float x1 = 0.f, y1 = 0.f, z1 = 0.f;
  EntityAttributes attr;
};

struct CadBlockWorldPoint {
  float x = 0.f, y = 0.f, z = 0.f;
};

[[nodiscard]] inline bool CadBlockEqCi(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(b[i]);
    const char la = static_cast<char>((ca >= 'A' && ca <= 'Z') ? (ca - 'A' + 'a') : ca);
    const char lb = static_cast<char>((cb >= 'A' && cb <= 'Z') ? (cb - 'A' + 'a') : cb);
    if (la != lb)
      return false;
  }
  return true;
}

inline void CadBlockXformPoint(const CadBlockXform& xf, float lx, float ly, float lz, float* wx, float* wy,
                               float* wz) {
  assert(wx != nullptr);
  assert(wy != nullptr);
  float x = lx * xf.sx;
  float y = ly * xf.sy;
  float z = lz * xf.sz;
  if (xf.rotZ != 0.f) {
    const float c = std::cos(xf.rotZ);
    const float s = std::sin(xf.rotZ);
    const float nx = x * c - y * s;
    const float ny = x * s + y * c;
    x = nx;
    y = ny;
  }
  if (xf.rotY != 0.f) {
    const float c = std::cos(xf.rotY);
    const float s = std::sin(xf.rotY);
    const float nx = x * c + z * s;
    const float nz = -x * s + z * c;
    x = nx;
    z = nz;
  }
  if (xf.rotX != 0.f) {
    const float c = std::cos(xf.rotX);
    const float s = std::sin(xf.rotX);
    const float ny = y * c - z * s;
    const float nz = y * s + z * c;
    y = ny;
    z = nz;
  }
  *wx = x + xf.x;
  *wy = y + xf.y;
  if (wz)
    *wz = z + xf.z;
}

[[nodiscard]] inline CadBlockXform CadBlockCompose(const CadBlockXform& parent, const CadBlockXform& child) {
  float wx = 0.f, wy = 0.f, wz = 0.f;
  CadBlockXformPoint(parent, child.x, child.y, child.z, &wx, &wy, &wz);
  CadBlockXform out = parent;
  out.x = wx;
  out.y = wy;
  out.z = wz;
  out.sx *= child.sx;
  out.sy *= child.sy;
  out.sz *= child.sz;
  out.rotX += child.rotX;
  out.rotY += child.rotY;
  out.rotZ += child.rotZ;
  return out;
}

inline bool CadBlockNameIsMatchline(std::string_view name) {
  if (name.size() < 11)
    return false;
  return CadBlockEqCi(name.substr(0, 11), "_matchline_");
}

[[nodiscard]] inline int CadBlockFindDef(const std::vector<CadBlockDefinition>& defs, std::string_view name) {
  for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
    if (CadBlockEqCi(defs[static_cast<size_t>(i)].name, name))
      return i;
  }
  return -1;
}

[[nodiscard]] inline float CadBlockParamValue(const CadBlockRef& ref, const CadBlockDefinition& def,
                                              std::string_view name) {
  for (const CadBlockParameter& p : ref.paramState) {
    if (CadBlockEqCi(p.name, name))
      return p.value;
  }
  for (const CadBlockParameter& p : def.parameters) {
    if (CadBlockEqCi(p.name, name))
      return p.value;
  }
  return 0.f;
}

[[nodiscard]] inline std::string CadBlockLabelGroupOf(std::string_view tagOrText) {
  std::string l(tagOrText);
  for (char& c : l) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  if (l.find("sheet") != std::string::npos || l == "n#")
    return "SHEET";
  if (l.find("north") != std::string::npos || l.find("east") != std::string::npos || l == "n:" || l == "e:")
    return "NORTH";
  if (l.find("match") != std::string::npos)
    return "SHEET";
  return {};
}

[[nodiscard]] inline bool CadBlockActionApplies(const CadBlockAction& a, std::string_view prim,
                                                std::string_view group) {
  if (!a.applyTo.empty() && !CadBlockEqCi(a.applyTo, prim))
    return false;
  if (!a.labelGroup.empty() && !CadBlockEqCi(a.labelGroup, group))
    return false;
  return true;
}

inline void CadBlockApplyActionsToPoint(const CadBlockDefinition& def, const CadBlockRef& ref, float* x, float* y,
                                        std::string_view prim = {}, std::string_view group = {}) {
  assert(x != nullptr);
  assert(y != nullptr);
  for (const CadBlockAction& a : def.actions) {
    if (!CadBlockActionApplies(a, prim, group))
      continue;
    // Already-authored matchlines still carry DistNeg/DistPos Stretch on labels; skip those so
    // INSERT endpoint grips match AutoCAD (bar only).
    if (CadBlockNameIsMatchline(def.name) && a.kind == CadBlockActionKind::Stretch &&
        CadBlockEqCi(a.applyTo, "labels") &&
        (CadBlockEqCi(a.paramName, "DistNeg") || CadBlockEqCi(a.paramName, "DistPos")))
      continue;
    const float v = CadBlockParamValue(ref, def, a.paramName);
    if (a.kind == CadBlockActionKind::Move) {
      *x += a.dirX * v;
      *y += a.dirY * v;
    } else if (a.kind == CadBlockActionKind::Stretch) {
      const float px = *x - a.originX;
      const float py = *y - a.originY;
      const float along = px * a.dirX + py * a.dirY;
      if (along >= a.threshold) {
        *x += a.dirX * v;
        *y += a.dirY * v;
      }
    } else if (a.kind == CadBlockActionKind::Rotate) {
      const float c = std::cos(v);
      const float s = std::sin(v);
      const float dx = *x - a.originX;
      const float dy = *y - a.originY;
      *x = a.originX + dx * c - dy * s;
      *y = a.originY + dx * s + dy * c;
    } else if (a.kind == CadBlockActionKind::Scale) {
      const float f = (v == 0.f) ? 1.f : v;
      *x = a.originX + (*x - a.originX) * f;
      *y = a.originY + (*y - a.originY) * f;
    } else if (a.kind == CadBlockActionKind::Flip) {
      if (v >= 0.5f)
        *x = 2.f * a.originX - *x;
    }
  }
}

[[nodiscard]] inline bool CadBlockVisHidden(const std::string& primVis, const std::string& active) {
  if (primVis.empty() || active.empty())
    return false;
  return !CadBlockEqCi(primVis, active);
}

[[nodiscard]] inline EntityAttributes CadBlockResolveAttr(const EntityAttributes& prim, const EntityAttributes& insert) {
  EntityAttributes o = prim;
  if (CadBlockEqCi(prim.color, "ByBlock") || prim.color.empty())
    o.color = insert.color.empty() ? std::string("ByLayer") : insert.color;
  if (CadBlockEqCi(prim.linetype, "ByBlock"))
    o.linetype = insert.linetype.empty() ? std::string("ByLayer") : insert.linetype;
  if (prim.layer.empty())
    o.layer = insert.layer.empty() ? std::string("0") : insert.layer;
  o.id = insert.id;
  return o;
}

[[nodiscard]] inline bool CadBlockWouldCycle(const std::vector<CadBlockDefinition>& defs, std::string_view host,
                                             std::string_view nested) {
  assert(!host.empty());
  if (CadBlockEqCi(host, nested))
    return true;
  std::array<std::string, kCadBlockMaxNest> stack{};
  int n = 0;
  stack[static_cast<size_t>(n++)] = std::string(nested);
  for (int step = 0; step < kCadBlockMaxWalk && n > 0; ++step) {
    const std::string cur = stack[static_cast<size_t>(--n)];
    if (CadBlockEqCi(cur, host))
      return true;
    const int di = CadBlockFindDef(defs, cur);
    if (di < 0)
      continue;
    const CadBlockDefinition& d = defs[static_cast<size_t>(di)];
    for (const CadBlockNested& ch : d.content.nested) {
      if (n >= kCadBlockMaxNest)
        return true;
      stack[static_cast<size_t>(n++)] = ch.defName;
    }
  }
  return false;
}

inline void CadBlockShiftContent(CadBlockContent* c, float dx, float dy, float dz) {
  assert(c != nullptr);
  for (size_t i = 0; i + 5 < c->lines.size(); i += 6) {
    c->lines[i] += dx;
    c->lines[i + 1] += dy;
    c->lines[i + 2] += dz;
    c->lines[i + 3] += dx;
    c->lines[i + 4] += dy;
    c->lines[i + 5] += dz;
  }
  for (size_t i = 0; i + 3 < c->circles.size(); i += 4) {
    c->circles[i] += dx;
    c->circles[i + 1] += dy;
    c->circles[i + 2] += dz;
  }
  for (CadArc& a : c->arcs) {
    a.cx += dx;
    a.cy += dy;
    a.z += dz;
  }
  for (CadEllipse& e : c->ellipses) {
    e.cx += dx;
    e.cy += dy;
    e.z += dz;
  }
  for (size_t i = 0; i + 2 < c->polyVerts.size(); i += 3) {
    c->polyVerts[i] += dx;
    c->polyVerts[i + 1] += dy;
    c->polyVerts[i + 2] += dz;
  }
  for (CadAnnotation& t : c->texts) {
    t.insX += dx;
    t.insY += dy;
    t.insZ += dz;
    t.boxMinX += dx;
    t.boxMaxX += dx;
    t.boxMinY += dy;
    t.boxMaxY += dy;
  }
  for (CadBlockNested& n : c->nested) {
    n.xf.x += dx;
    n.xf.y += dy;
    n.xf.z += dz;
  }
}

inline void CadBlockBakeBasePoint(CadBlockDefinition* def) {
  assert(def != nullptr);
  CadBlockShiftContent(&def->content, -def->baseX, -def->baseY, -def->baseZ);
  for (CadBlockAttrDef& a : def->attrDefs) {
    a.localX -= def->baseX;
    a.localY -= def->baseY;
    a.localZ -= def->baseZ;
  }
  def->baseX = 0.f;
  def->baseY = 0.f;
  def->baseZ = 0.f;
}

struct CadBlockWalkFrame {
  int defIndex = -1;
  CadBlockXform xf;
  std::string vis;
  const CadBlockRef* ref = nullptr;
};

[[nodiscard]] inline std::string CadBlockAttrGet(const CadBlockRef& r, const CadBlockDefinition& def,
                                                 std::string_view tag);

inline void CadBlockCollectWorldLines(const std::vector<CadBlockDefinition>& defs, const CadBlockRef& rootRef,
                                      const EntityAttributes& insertAttr, std::vector<CadBlockWorldSeg>* out) {
  assert(out != nullptr);
  const int root = CadBlockFindDef(defs, rootRef.defName);
  if (root < 0)
    return;
  std::array<CadBlockWalkFrame, kCadBlockMaxNest> stack{};
  int n = 0;
  stack[static_cast<size_t>(n++)] = CadBlockWalkFrame{root, rootRef.xf, rootRef.visState, &rootRef};
  int steps = 0;
  while (n > 0 && steps < kCadBlockMaxWalk) {
    ++steps;
    const CadBlockWalkFrame fr = stack[static_cast<size_t>(--n)];
    if (fr.defIndex < 0)
      continue;
    const CadBlockDefinition& def = defs[static_cast<size_t>(fr.defIndex)];
    const CadBlockRef localRef = fr.ref ? *fr.ref : CadBlockRef{};
    const CadBlockContent& c = def.content;
    auto xformPt = [&](float lx, float ly, float lz, float* wx, float* wy, float* wz) {
      float ax = lx;
      float ay = ly;
      CadBlockApplyActionsToPoint(def, localRef, &ax, &ay, "geom", {});
      CadBlockXformPoint(fr.xf, ax, ay, lz, wx, wy, wz);
    };
    const size_t nSeg = c.lines.size() / 6;
    for (size_t i = 0; i < nSeg; ++i) {
      if (i < c.lineVis.size() && CadBlockVisHidden(c.lineVis[i], fr.vis))
        continue;
      CadBlockWorldSeg s;
      xformPt(c.lines[i * 6 + 0], c.lines[i * 6 + 1], c.lines[i * 6 + 2], &s.x0, &s.y0, &s.z0);
      xformPt(c.lines[i * 6 + 3], c.lines[i * 6 + 4], c.lines[i * 6 + 5], &s.x1, &s.y1, &s.z1);
      EntityAttributes pa{};
      if (i < c.lineAttrs.size())
        pa = c.lineAttrs[i];
      s.attr = CadBlockResolveAttr(pa, insertAttr);
      out->push_back(s);
    }
    const size_t nC = c.circles.size() / 4;
    for (size_t i = 0; i < nC; ++i) {
      if (i < c.circleVis.size() && CadBlockVisHidden(c.circleVis[i], fr.vis))
        continue;
      const float cx = c.circles[i * 4 + 0];
      const float cy = c.circles[i * 4 + 1];
      const float cz = c.circles[i * 4 + 2];
      const float r = c.circles[i * 4 + 3];
      EntityAttributes pa{};
      if (i < c.circleAttrs.size())
        pa = c.circleAttrs[i];
      const EntityAttributes ra = CadBlockResolveAttr(pa, insertAttr);
      constexpr int kSteps = 24;
      // The circle's own plane, walked BEFORE the block transform (REQ-312). Generating the ring
      // flat and then transforming it draws a tilted block circle in the wrong plane entirely --
      // the store has carried the normal since step 2, and until now nothing here read it.
      float cnx = kFlatNormalX, cny = kFlatNormalY, cnz = kFlatNormalZ;
      CircleNormalAt(c.circleNormals, i, &cnx, &cny, &cnz);
      const bool circFlat = IsFlatNormal(cnx, cny, cnz);
      const ucs::Ucs cPlane =
          circFlat ? ucs::Ucs{}
                   : CurvePlane(static_cast<double>(cx), static_cast<double>(cy), static_cast<double>(cz),
                                static_cast<double>(cnx), static_cast<double>(cny), static_cast<double>(cnz));
      // Step \p k in block space. The flat branch is the pre-REQ-312 float arithmetic, unchanged.
      const auto stepPt = [&](int k, float* ox, float* oy, float* oz) {
        if (circFlat) {
          const float ang = (6.28318530718f * static_cast<float>(k)) / static_cast<float>(kSteps);
          *ox = cx + r * std::cos(ang);
          *oy = cy + r * std::sin(ang);
          *oz = cz;
          return;
        }
        constexpr double kTwoPi = 6.283185307179586;
        const ray3d::Vec3 p =
            CurvePointAt(cPlane, static_cast<double>(r), CurveSampleAngle(0.0, kTwoPi, k, kSteps));
        *ox = static_cast<float>(p.x);
        *oy = static_cast<float>(p.y);
        *oz = static_cast<float>(p.z);
      };
      float bx = 0.f, by = 0.f, bz = 0.f;
      float px = 0.f, py = 0.f, pz = 0.f;
      stepPt(0, &bx, &by, &bz);
      xformPt(bx, by, bz, &px, &py, &pz);
      for (int k = 1; k <= kSteps; ++k) {
        stepPt(k, &bx, &by, &bz);
        float qx = 0.f, qy = 0.f, qz = 0.f;
        xformPt(bx, by, bz, &qx, &qy, &qz);
        out->push_back(CadBlockWorldSeg{px, py, pz, qx, qy, qz, ra});
        px = qx;
        py = qy;
        pz = qz;
      }
    }
    if (c.polyOffsets.size() >= 2) {
      const int nPoly = static_cast<int>(c.polyOffsets.size()) - 1;
      for (int p = 0; p < nPoly; ++p) {
        const int a = c.polyOffsets[static_cast<size_t>(p)];
        const int b = c.polyOffsets[static_cast<size_t>(p + 1)];
        EntityAttributes pa{};
        if (static_cast<size_t>(p) < c.polyAttrs.size())
          pa = c.polyAttrs[static_cast<size_t>(p)];
        const EntityAttributes ra = CadBlockResolveAttr(pa, insertAttr);
        for (int v = a; v + 1 < b; ++v) {
          const size_t i0 = static_cast<size_t>(v) * 3;
          const size_t i1 = static_cast<size_t>(v + 1) * 3;
          if (i1 + 2 >= c.polyVerts.size())
            break;
          CadBlockWorldSeg s;
          xformPt(c.polyVerts[i0], c.polyVerts[i0 + 1], c.polyVerts[i0 + 2], &s.x0, &s.y0, &s.z0);
          xformPt(c.polyVerts[i1], c.polyVerts[i1 + 1], c.polyVerts[i1 + 2], &s.x1, &s.y1, &s.z1);
          s.attr = ra;
          out->push_back(s);
        }
        const bool closed = static_cast<size_t>(p) < c.polyClosed.size() && c.polyClosed[static_cast<size_t>(p)] != 0;
        if (closed && b - a >= 2) {
          const size_t i0 = static_cast<size_t>(b - 1) * 3;
          const size_t i1 = static_cast<size_t>(a) * 3;
          CadBlockWorldSeg s;
          xformPt(c.polyVerts[i0], c.polyVerts[i0 + 1], c.polyVerts[i0 + 2], &s.x0, &s.y0, &s.z0);
          xformPt(c.polyVerts[i1], c.polyVerts[i1 + 1], c.polyVerts[i1 + 2], &s.x1, &s.y1, &s.z1);
          s.attr = ra;
          out->push_back(s);
        }
      }
    }
    for (const CadBlockNested& child : c.nested) {
      if (n >= kCadBlockMaxNest)
        break;
      const int ci = CadBlockFindDef(defs, child.defName);
      if (ci < 0)
        continue;
      CadBlockWalkFrame nf;
      nf.defIndex = ci;
      nf.xf = CadBlockCompose(fr.xf, child.xf);
      nf.vis = child.visState.empty() ? fr.vis : child.visState;
      nf.ref = nullptr;
      stack[static_cast<size_t>(n++)] = nf;
    }
  }
}

/// Circle / arc / ellipse centre points of a placed INSERT (and its nested blocks) in world
/// space. Used for the Center object snap on block instances (REQ-107, D-2026-08-29-i).
inline void CadBlockCollectWorldCenters(const std::vector<CadBlockDefinition>& defs, const CadBlockRef& rootRef,
                                        std::vector<CadBlockWorldPoint>* out) {
  assert(out != nullptr);
  const int root = CadBlockFindDef(defs, rootRef.defName);
  if (root < 0)
    return;
  std::array<CadBlockWalkFrame, kCadBlockMaxNest> stack{};
  int n = 0;
  stack[static_cast<size_t>(n++)] = CadBlockWalkFrame{root, rootRef.xf, rootRef.visState, &rootRef};
  int steps = 0;
  while (n > 0 && steps < kCadBlockMaxWalk) {
    ++steps;
    const CadBlockWalkFrame fr = stack[static_cast<size_t>(--n)];
    if (fr.defIndex < 0)
      continue;
    const CadBlockDefinition& def = defs[static_cast<size_t>(fr.defIndex)];
    const CadBlockRef localRef = fr.ref ? *fr.ref : CadBlockRef{};
    const CadBlockContent& c = def.content;
    auto emit = [&](float lx, float ly, float lz) {
      float ax = lx;
      float ay = ly;
      CadBlockApplyActionsToPoint(def, localRef, &ax, &ay, "geom", {});
      CadBlockWorldPoint p;
      CadBlockXformPoint(fr.xf, ax, ay, lz, &p.x, &p.y, &p.z);
      out->push_back(p);
    };
    for (size_t i = 0; i + 3 < c.circles.size(); i += 4)
      emit(c.circles[i], c.circles[i + 1], c.circles[i + 2]);
    for (const CadArc& a : c.arcs)
      emit(a.cx, a.cy, a.z);
    for (const CadEllipse& e : c.ellipses)
      emit(e.cx, e.cy, e.z);
    for (const CadBlockNested& child : c.nested) {
      if (n >= kCadBlockMaxNest)
        break;
      const int ci = CadBlockFindDef(defs, child.defName);
      if (ci < 0)
        continue;
      CadBlockWalkFrame nf;
      nf.defIndex = ci;
      nf.xf = CadBlockCompose(fr.xf, child.xf);
      nf.vis = child.visState.empty() ? fr.vis : child.visState;
      nf.ref = nullptr;
      stack[static_cast<size_t>(n++)] = nf;
    }
  }
}

inline void CadBlockCollectWorldAnnotations(const std::vector<CadBlockDefinition>& defs, const CadBlockRef& rootRef,
                                            std::vector<CadAnnotation>* out) {
  assert(out != nullptr);
  const int root = CadBlockFindDef(defs, rootRef.defName);
  if (root < 0)
    return;
  std::array<CadBlockWalkFrame, kCadBlockMaxNest> stack{};
  int n = 0;
  stack[static_cast<size_t>(n++)] = CadBlockWalkFrame{root, rootRef.xf, rootRef.visState, &rootRef};
  int steps = 0;
  while (n > 0 && steps < kCadBlockMaxWalk) {
    ++steps;
    const CadBlockWalkFrame fr = stack[static_cast<size_t>(--n)];
    if (fr.defIndex < 0)
      continue;
    const CadBlockDefinition& def = defs[static_cast<size_t>(fr.defIndex)];
    const CadBlockRef localRef = fr.ref ? *fr.ref : CadBlockRef{};
    auto xformLabel = [&](float lx, float ly, float lz, std::string_view group, float* wx, float* wy, float* wz) {
      float ax = lx;
      float ay = ly;
      CadBlockApplyActionsToPoint(def, localRef, &ax, &ay, "labels", group);
      CadBlockXformPoint(fr.xf, ax, ay, lz, wx, wy, wz);
    };
    for (const CadAnnotation& t : def.content.texts) {
      CadAnnotation a = t;
      const std::string group = CadBlockLabelGroupOf(t.text);
      xformLabel(t.insX, t.insY, t.insZ, group, &a.insX, &a.insY, &a.insZ);
      a.rotationRad += fr.xf.rotZ;
      const float sc = std::max(std::fabs(fr.xf.sx), std::fabs(fr.xf.sy));
      a.plottedHeightInches *= sc;
      const bool hasBox = t.boxMaxX > t.boxMinX && t.boxMaxY > t.boxMinY;
      if (hasBox) {
        float xs[4] = {}, ys[4] = {}, zz = 0.f;
        xformLabel(t.boxMinX, t.boxMinY, t.insZ, group, &xs[0], &ys[0], &zz);
        xformLabel(t.boxMaxX, t.boxMinY, t.insZ, group, &xs[1], &ys[1], &zz);
        xformLabel(t.boxMaxX, t.boxMaxY, t.insZ, group, &xs[2], &ys[2], &zz);
        xformLabel(t.boxMinX, t.boxMaxY, t.insZ, group, &xs[3], &ys[3], &zz);
        a.boxMinX = a.boxMaxX = xs[0];
        a.boxMinY = a.boxMaxY = ys[0];
        for (int k = 1; k < 4; ++k) {
          a.boxMinX = std::min(a.boxMinX, xs[k]);
          a.boxMaxX = std::max(a.boxMaxX, xs[k]);
          a.boxMinY = std::min(a.boxMinY, ys[k]);
          a.boxMaxY = std::max(a.boxMaxY, ys[k]);
        }
      } else {
        a.boxMinX = a.insX;
        a.boxMinY = a.insY;
        a.boxMaxX = a.insX;
        a.boxMaxY = a.insY;
      }
      out->push_back(std::move(a));
    }
    const CadBlockRef* valRef = fr.ref;
    for (const CadBlockAttrDef& ad : def.attrDefs) {
      CadAnnotation a;
      a.kind = CadAnnotation::Kind::Text;
      xformLabel(ad.localX, ad.localY, ad.localZ, CadBlockLabelGroupOf(ad.tag), &a.insX, &a.insY, &a.insZ);
      a.plottedHeightInches = ad.height * std::max(std::fabs(fr.xf.sx), std::fabs(fr.xf.sy));
      a.rotationRad = ad.rotationRad + fr.xf.rotZ;
      if (valRef)
        a.text = CadBlockAttrGet(*valRef, def, ad.tag);
      if (a.text.empty())
        a.text = ad.defaultValue;
      out->push_back(std::move(a));
    }
    for (const CadBlockNested& child : def.content.nested) {
      if (n >= kCadBlockMaxNest)
        break;
      const int ci = CadBlockFindDef(defs, child.defName);
      if (ci < 0)
        continue;
      CadBlockWalkFrame nf;
      nf.defIndex = ci;
      nf.xf = CadBlockCompose(fr.xf, child.xf);
      nf.vis = child.visState.empty() ? fr.vis : child.visState;
      nf.ref = nullptr;
      stack[static_cast<size_t>(n++)] = nf;
    }
  }
}

inline void CadBlockCollectSnapPoints(const std::vector<CadBlockDefinition>& defs, const CadBlockRef& rootRef,
                                      std::vector<CadBlockWorldPoint>* out) {
  assert(out != nullptr);
  std::vector<CadBlockWorldSeg> segs;
  EntityAttributes dummy;
  CadBlockCollectWorldLines(defs, rootRef, dummy, &segs);
  out->push_back(CadBlockWorldPoint{rootRef.xf.x, rootRef.xf.y, rootRef.xf.z});
  for (const CadBlockWorldSeg& s : segs) {
    out->push_back(CadBlockWorldPoint{s.x0, s.y0, s.z0});
    out->push_back(CadBlockWorldPoint{s.x1, s.y1, s.z1});
    out->push_back(CadBlockWorldPoint{0.5f * (s.x0 + s.x1), 0.5f * (s.y0 + s.y1), 0.5f * (s.z0 + s.z1)});
  }
}

inline void CadBlockWorldAabb(const std::vector<CadBlockDefinition>& defs, const CadBlockRef& ref, float* mnX,
                              float* mnY, float* mxX, float* mxY) {
  assert(mnX && mnY && mxX && mxY);
  std::vector<CadBlockWorldSeg> segs;
  EntityAttributes dummy;
  CadBlockCollectWorldLines(defs, ref, dummy, &segs);
  *mnX = *mxX = ref.xf.x;
  *mnY = *mxY = ref.xf.y;
  for (const CadBlockWorldSeg& s : segs) {
    *mnX = std::min(*mnX, std::min(s.x0, s.x1));
    *mxX = std::max(*mxX, std::max(s.x0, s.x1));
    *mnY = std::min(*mnY, std::min(s.y0, s.y1));
    *mxY = std::max(*mxY, std::max(s.y0, s.y1));
  }
  std::vector<CadAnnotation> anns;
  CadBlockCollectWorldAnnotations(defs, ref, &anns);
  for (const CadAnnotation& a : anns) {
    *mnX = std::min(*mnX, a.insX);
    *mxX = std::max(*mxX, a.insX);
    *mnY = std::min(*mnY, a.insY);
    *mxY = std::max(*mxY, a.insY);
    if (a.boxMaxX > a.boxMinX) {
      *mnX = std::min(*mnX, a.boxMinX);
      *mxX = std::max(*mxX, a.boxMaxX);
    }
    if (a.boxMaxY > a.boxMinY) {
      *mnY = std::min(*mnY, a.boxMinY);
      *mxY = std::max(*mxY, a.boxMaxY);
    }
  }
}

[[nodiscard]] inline bool CadBlockHitWorld(const std::vector<CadBlockDefinition>& defs, const CadBlockRef& ref,
                                           float wx, float wy, float tol) {
  std::vector<CadBlockWorldSeg> segs;
  EntityAttributes dummy;
  CadBlockCollectWorldLines(defs, ref, dummy, &segs);
  const float t2 = tol * tol;
  const float dx0 = wx - ref.xf.x;
  const float dy0 = wy - ref.xf.y;
  if (dx0 * dx0 + dy0 * dy0 <= t2)
    return true;
  for (const CadBlockWorldSeg& s : segs) {
    const float vx = s.x1 - s.x0;
    const float vy = s.y1 - s.y0;
    const float len2 = vx * vx + vy * vy;
    float t = 0.f;
    if (len2 > 1.e-20f)
      t = std::clamp(((wx - s.x0) * vx + (wy - s.y0) * vy) / len2, 0.f, 1.f);
    const float px = s.x0 + t * vx - wx;
    const float py = s.y0 + t * vy - wy;
    if (px * px + py * py <= t2)
      return true;
  }
  return false;
}

inline void CadBlockTranslate(CadBlockRef* r, float dx, float dy, float dz) {
  assert(r != nullptr);
  r->xf.x += dx;
  r->xf.y += dy;
  r->xf.z += dz;
}

inline void CadBlockRotateZ(CadBlockRef* r, float baseX, float baseY, float rad) {
  assert(r != nullptr);
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float dx = r->xf.x - baseX;
  const float dy = r->xf.y - baseY;
  r->xf.x = baseX + dx * c - dy * s;
  r->xf.y = baseY + dx * s + dy * c;
  r->xf.rotZ += rad;
}

inline void CadBlockScaleAbout(CadBlockRef* r, float baseX, float baseY, float factor) {
  assert(r != nullptr);
  r->xf.x = baseX + (r->xf.x - baseX) * factor;
  r->xf.y = baseY + (r->xf.y - baseY) * factor;
  r->xf.sx *= factor;
  r->xf.sy *= factor;
  r->xf.sz *= factor;
}

inline void CadBlockMirror(CadBlockRef* r, float ax, float ay, float bx, float by) {
  assert(r != nullptr);
  float dx = bx - ax;
  float dy = by - ay;
  const float len2 = dx * dx + dy * dy;
  if (len2 < 1.e-20f)
    return;
  dx /= std::sqrt(len2);
  dy /= std::sqrt(len2);
  const float px = r->xf.x - ax;
  const float py = r->xf.y - ay;
  const float along = px * dx + py * dy;
  const float qx = ax + along * dx;
  const float qy = ay + along * dy;
  r->xf.x = 2.f * qx - r->xf.x;
  r->xf.y = 2.f * qy - r->xf.y;
  r->xf.sx = -r->xf.sx;
}

[[nodiscard]] inline std::string CadBlockAttrGet(const CadBlockRef& r, const CadBlockDefinition& def,
                                                 std::string_view tag) {
  for (const CadBlockAttrValue& v : r.attributes) {
    if (CadBlockEqCi(v.tag, tag))
      return v.value;
  }
  for (const CadBlockAttrDef& d : def.attrDefs) {
    if (CadBlockEqCi(d.tag, tag))
      return d.defaultValue;
  }
  return {};
}

inline void CadBlockAttrSet(CadBlockRef* r, std::string tag, std::string value) {
  assert(r != nullptr);
  for (CadBlockAttrValue& v : r->attributes) {
    if (CadBlockEqCi(v.tag, tag)) {
      v.value = std::move(value);
      return;
    }
  }
  r->attributes.push_back(CadBlockAttrValue{std::move(tag), std::move(value)});
}

inline void CadBlockParamSet(CadBlockRef* r, std::string name, float value) {
  assert(r != nullptr);
  for (CadBlockParameter& p : r->paramState) {
    if (CadBlockEqCi(p.name, name)) {
      p.value = value;
      return;
    }
  }
  CadBlockParameter p;
  p.name = std::move(name);
  p.value = value;
  r->paramState.push_back(std::move(p));
}

[[nodiscard]] inline int CadBlockCountRefs(const std::vector<CadBlockRef>& refs, std::string_view name) {
  int n = 0;
  for (const CadBlockRef& r : refs) {
    if (CadBlockEqCi(r.defName, name))
      ++n;
  }
  return n;
}

[[nodiscard]] inline bool CadBlockDefUsedBy(const std::vector<CadBlockDefinition>& defs, std::string_view name) {
  for (const CadBlockDefinition& d : defs) {
    if (CadBlockEqCi(d.name, name))
      continue;
    for (const CadBlockNested& n : d.content.nested) {
      if (CadBlockEqCi(n.defName, name))
        return true;
    }
  }
  return false;
}

[[nodiscard]] inline float CadBlockUnitsScale(std::string_view fromUnits, std::string_view toUnits) {
  auto u = [](std::string_view s) {
    if (CadBlockEqCi(s, "inches") || CadBlockEqCi(s, "in") || CadBlockEqCi(s, "inch"))
      return 1.f;
    if (CadBlockEqCi(s, "feet") || CadBlockEqCi(s, "ft") || CadBlockEqCi(s, "foot"))
      return 12.f;
    if (CadBlockEqCi(s, "meters") || CadBlockEqCi(s, "m") || CadBlockEqCi(s, "metre"))
      return 39.3700787f;
    if (CadBlockEqCi(s, "millimeters") || CadBlockEqCi(s, "mm"))
      return 0.0393700787f;
    return 1.f;
  };
  const float a = u(fromUnits);
  const float b = u(toUnits);
  if (b == 0.f)
    return 1.f;
  return a / b;
}

[[nodiscard]] inline std::string CadDrawingInsUnitsName(int code) {
  if (code == 1)
    return "inches";
  if (code == 2)
    return "feet";
  if (code == 6)
    return "meters";
  return "unitless";
}

[[nodiscard]] inline int CadDrawingInsUnitsCode(std::string_view s) {
  if (CadBlockEqCi(s, "inches") || CadBlockEqCi(s, "in") || CadBlockEqCi(s, "inch"))
    return 1;
  if (CadBlockEqCi(s, "feet") || CadBlockEqCi(s, "ft") || CadBlockEqCi(s, "foot"))
    return 2;
  if (CadBlockEqCi(s, "meters") || CadBlockEqCi(s, "m") || CadBlockEqCi(s, "metre"))
    return 6;
  return 0;
}

[[nodiscard]] inline bool CadBlockHasMatchlineDyn(const CadBlockDefinition& def) {
  for (const CadBlockParameter& p : def.parameters) {
    if (CadBlockEqCi(p.name, "DistNeg"))
      return true;
  }
  return false;
}

inline void CadBlockAuthorMatchlineDynamics(CadBlockDefinition* def) {
  assert(def != nullptr);
  if (!CadBlockNameIsMatchline(def->name) || CadBlockHasMatchlineDyn(*def))
    return;
  auto addP = [&](const char* n, CadBlockParamKind k, float v) {
    CadBlockParameter p;
    p.name = n;
    p.kind = k;
    p.value = v;
    def->parameters.push_back(std::move(p));
  };
  addP("DistNeg", CadBlockParamKind::Linear, 0.f);
  addP("DistPos", CadBlockParamKind::Linear, 0.f);
  addP("Flip", CadBlockParamKind::Flip, 0.f);
  addP("SheetOff", CadBlockParamKind::Linear, 0.f);
  addP("NorthOff", CadBlockParamKind::Linear, 0.f);
  auto addA = [&](CadBlockActionKind k, const char* param, float ox, float oy, float dx, float dy, float th,
                  const char* applyTo, const char* group) {
    CadBlockAction a;
    a.kind = k;
    a.paramName = param;
    a.originX = ox;
    a.originY = oy;
    a.dirX = dx;
    a.dirY = dy;
    a.threshold = th;
    a.applyTo = applyTo;
    a.labelGroup = group;
    def->actions.push_back(std::move(a));
  };
  // Civil 3D matchline: endpoint stretch lengthens the dashed bar only. Labels stay put unless
  // the dedicated SheetOff / NorthOff grips move them.
  addA(CadBlockActionKind::Stretch, "DistNeg", 0.f, 0.f, 0.f, -1.f, 0.05f, "geom", "");
  addA(CadBlockActionKind::Stretch, "DistPos", 0.f, 0.f, 0.f, 1.f, 0.05f, "geom", "");
  addA(CadBlockActionKind::Flip, "Flip", 0.f, 0.f, 1.f, 0.f, 0.f, "labels", "");
  addA(CadBlockActionKind::Move, "SheetOff", 0.f, 0.f, 0.f, -1.f, 0.f, "labels", "SHEET");
  addA(CadBlockActionKind::Move, "NorthOff", 0.f, 0.f, 0.f, 1.f, 0.f, "labels", "NORTH");
}

inline constexpr int kCadBlockDynGripCount = 6;

inline void CadBlockWorldToLocal(const CadBlockXform& xf, float wx, float wy, float* lx, float* ly) {
  assert(lx != nullptr && ly != nullptr);
  float x = wx - xf.x;
  float y = wy - xf.y;
  if (xf.rotZ != 0.f) {
    const float c = std::cos(xf.rotZ);
    const float s = std::sin(xf.rotZ);
    const float nx = x * c + y * s;
    const float ny = -x * s + y * c;
    x = nx;
    y = ny;
  }
  if (std::fabs(xf.sx) > 1.e-12f)
    x /= xf.sx;
  if (std::fabs(xf.sy) > 1.e-12f)
    y /= xf.sy;
  *lx = x;
  *ly = y;
}

inline void CadBlockLineYExtent(const CadBlockDefinition& def, float* yMin, float* yMax) {
  assert(yMin != nullptr && yMax != nullptr);
  *yMin = 0.f;
  *yMax = 0.f;
  bool any = false;
  const std::vector<float>& L = def.content.lines;
  for (size_t i = 0; i + 4 < L.size(); i += 6) {
    const float y0 = L[i + 1];
    const float y1 = L[i + 4];
    if (!any) {
      *yMin = std::min(y0, y1);
      *yMax = std::max(y0, y1);
      any = true;
    } else {
      *yMin = std::min(*yMin, std::min(y0, y1));
      *yMax = std::max(*yMax, std::max(y0, y1));
    }
  }
  if (!any) {
    *yMin = -2.f;
    *yMax = 2.f;
  }
}

inline float CadBlockLabelRestY(const CadBlockDefinition& def, std::string_view group) {
  for (const CadBlockAttrDef& ad : def.attrDefs) {
    if (CadBlockEqCi(CadBlockLabelGroupOf(ad.tag), group))
      return ad.localY;
  }
  for (const CadAnnotation& t : def.content.texts) {
    if (CadBlockEqCi(CadBlockLabelGroupOf(t.text), group))
      return t.insY;
  }
  return CadBlockEqCi(group, "SHEET") ? -1.6f : 0.22f;
}

inline bool CadBlockDynGripWorld(const CadBlockDefinition& def, const CadBlockRef& ref, int which, float* wx,
                                 float* wy, float* wz) {
  assert(wx && wy);
  if (which == 0) {
    *wx = ref.xf.x;
    *wy = ref.xf.y;
    if (wz)
      *wz = ref.xf.z;
    return true;
  }
  if (!CadBlockHasMatchlineDyn(def))
    return false;
  float lx = 0.f, ly = 0.f, lz = 0.f;
  float yMin = 0.f, yMax = 0.f;
  CadBlockLineYExtent(def, &yMin, &yMax);
  const float dNeg = CadBlockParamValue(ref, def, "DistNeg");
  const float dPos = CadBlockParamValue(ref, def, "DistPos");
  if (which == 1) {
    lx = 0.f;
    ly = yMin - dNeg;
  } else if (which == 2) {
    lx = 0.f;
    ly = yMax + dPos;
  } else if (which == 3) {
    lx = 0.18f;
    ly = 0.f;
    CadBlockApplyActionsToPoint(def, ref, &lx, &ly, "labels", {});
  } else if (which == 4) {
    lx = 0.12f;
    ly = CadBlockLabelRestY(def, "SHEET");
    CadBlockApplyActionsToPoint(def, ref, &lx, &ly, "labels", "SHEET");
  } else if (which == 5) {
    lx = 0.12f;
    ly = CadBlockLabelRestY(def, "NORTH");
    CadBlockApplyActionsToPoint(def, ref, &lx, &ly, "labels", "NORTH");
  } else
    return false;
  CadBlockXformPoint(ref.xf, lx, ly, lz, wx, wy, wz);
  return true;
}

inline int CadBlockDynGripCount(const CadBlockDefinition& def) {
  return CadBlockHasMatchlineDyn(def) ? kCadBlockDynGripCount : 1;
}

enum class CadBlockDynGripShape : std::uint8_t { Square = 0, StretchArrow, OffsetTriangle, FlipArrow };

/// Every matchline dynamic grip is shown on a selected INSERT, including Flip (click-toggle).
[[nodiscard]] inline bool CadBlockDynGripShownOnInsert(int which) {
  (void)which;
  return true;
}

[[nodiscard]] inline CadBlockDynGripShape CadBlockDynGripShapeOf(int which) {
  if (which == 1 || which == 2)
    return CadBlockDynGripShape::StretchArrow;
  if (which == 3)
    return CadBlockDynGripShape::FlipArrow;
  if (which == 4 || which == 5)
    return CadBlockDynGripShape::OffsetTriangle;
  return CadBlockDynGripShape::Square;
}

inline void CadBlockDynGripLocalAxis(int which, float* dx, float* dy) {
  assert(dx != nullptr && dy != nullptr);
  *dx = 0.f;
  *dy = 0.f;
  if (which == 3)
    *dx = 1.f;
  else if (which == 1 || which == 4)
    *dy = -1.f;
  else if (which == 2 || which == 5)
    *dy = 1.f;
}

inline void CadBlockXformDelta(const CadBlockXform& xf, float lx, float ly, float* wx, float* wy) {
  assert(wx != nullptr && wy != nullptr);
  float x0 = 0.f, y0 = 0.f, z0 = 0.f, x1 = 0.f, y1 = 0.f, z1 = 0.f;
  CadBlockXformPoint(xf, 0.f, 0.f, 0.f, &x0, &y0, &z0);
  CadBlockXformPoint(xf, lx, ly, 0.f, &x1, &y1, &z1);
  *wx = x1 - x0;
  *wy = y1 - y0;
}

inline void CadBlockApplyDynGripDrag(CadBlockRef* r, const CadBlockDefinition& def, int which, float wx, float wy) {
  assert(r != nullptr);
  if (which == 0) {
    r->xf.x = wx;
    r->xf.y = wy;
    return;
  }
  if (!CadBlockHasMatchlineDyn(def))
    return;
  float lx = 0.f, ly = 0.f;
  CadBlockWorldToLocal(r->xf, wx, wy, &lx, &ly);
  float yMin = 0.f, yMax = 0.f;
  CadBlockLineYExtent(def, &yMin, &yMax);
  if (which == 1)
    CadBlockParamSet(r, "DistNeg", std::max(0.f, yMin - ly));
  else if (which == 2)
    CadBlockParamSet(r, "DistPos", std::max(0.f, ly - yMax));
  else if (which == 4)
    CadBlockParamSet(r, "SheetOff", CadBlockLabelRestY(def, "SHEET") - ly);
  else if (which == 5)
    CadBlockParamSet(r, "NorthOff", ly - CadBlockLabelRestY(def, "NORTH"));
}

inline void CadBlockToggleMatchlineFlip(CadBlockRef* r, const CadBlockDefinition& def) {
  assert(r != nullptr);
  const float v = CadBlockParamValue(*r, def, "Flip");
  CadBlockParamSet(r, "Flip", v >= 0.5f ? 0.f : 1.f);
}

