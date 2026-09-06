#pragma once

#include "util/brep.hpp"

#include <nlohmann/json.hpp>

#include <cmath>

/// `.gs` serialization for the B-rep solid kernel (REQ-313 / REQ-314 / REQ-315).
///
/// Split out of `GsIo.cpp` so the serializer is **linkable without the command layer** — the same
/// ADR-002 split `GsMigrate` already took, and the reason the mesh round-trip test could only test a
/// re-implementation before. A solid's `.gs` fidelity (topology counts identical, volume and area
/// stable across a save/reopen — REQ-315 acceptance) is now checked directly, through exactly the
/// code the saver runs.
///
/// Header-only and `inline`: it is small, it pulls in only `nlohmann/json` and `brep.hpp` (both
/// already in every translation unit that needs it), and keeping it out of a new `.cpp` keeps the
/// build files untouched.
///
/// The encoding is **additive and tolerant** (ADR-020 (d)): a solid with no `Nurbs` face serializes
/// byte-identically to a version-3 build, and a reader fills every absent key with a default. The
/// one key an older reader cannot tolerate is a `Nurbs` surface — that is why `kGsFormatVersion`
/// goes to 4 (ADR-048 (e)); a malformed patch is refused on load by name.
namespace gsio {

using nlohmann::json;

// ---------------------------------------------------------------------------------------------
// Small shared readers/writers. The frame is four unit vectors plus an origin; the reader refuses
// one that is not right-handed orthonormal, so a hand-edited file cannot present a sheared surface.
// ---------------------------------------------------------------------------------------------

[[nodiscard]] inline json BrepVec3ToJson(const ray3d::Vec3& v) {
  return json::array({v.x, v.y, v.z});
}

[[nodiscard]] inline bool BrepReadVec3(const json& j, ray3d::Vec3* out) {
  if (!j.is_array() || j.size() != 3)
    return false;
  for (const auto& c : j)
    if (!c.is_number())
      return false;
  out->x = j[0].get<double>();
  out->y = j[1].get<double>();
  out->z = j[2].get<double>();
  return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
}

[[nodiscard]] inline json BrepFrameToJson(const ucs::Ucs& u) {
  json j;
  j["origin"] = BrepVec3ToJson(u.origin);
  j["xAxis"] = BrepVec3ToJson(u.xAxis);
  j["yAxis"] = BrepVec3ToJson(u.yAxis);
  j["zAxis"] = BrepVec3ToJson(u.zAxis);
  return j;
}

[[nodiscard]] inline bool BrepFrameFromJson(const json& j, ucs::Ucs* out) {
  if (!j.is_object() || !out)
    return false;
  ucs::Ucs u;
  if (!j.contains("origin") || !BrepReadVec3(j["origin"], &u.origin))
    return false;
  if (!j.contains("xAxis") || !BrepReadVec3(j["xAxis"], &u.xAxis))
    return false;
  if (!j.contains("yAxis") || !BrepReadVec3(j["yAxis"], &u.yAxis))
    return false;
  if (!j.contains("zAxis") || !BrepReadVec3(j["zAxis"], &u.zAxis))
    return false;
  if (!ucs::IsRightHandedOrthonormal(u, 1e-6))
    return false;
  *out = u;
  return true;
}

// ---------------------------------------------------------------------------------------------
// The freeform NURBS patch (REQ-315 / ADR-048). Only written when a face actually carries one.
// ---------------------------------------------------------------------------------------------

[[nodiscard]] inline json BrepPatchToJson(const nurbs::Patch& p) {
  json j;
  j["degU"] = p.degU;
  j["degV"] = p.degV;
  j["nu"] = p.nu;
  j["nv"] = p.nv;
  j["knotsU"] = p.knotsU;
  j["knotsV"] = p.knotsV;
  json ctrl = json::array();
  for (const ray3d::Vec3& c : p.ctrl)
    ctrl.push_back(BrepVec3ToJson(c));
  j["ctrl"] = std::move(ctrl);
  j["wts"] = p.wts;
  return j;
}

[[nodiscard]] inline bool BrepPatchFromJson(const json& j, nurbs::Patch* out) {
  if (!j.is_object())
    return false;
  if (!j.contains("degU") || !j.contains("degV") || !j.contains("nu") || !j.contains("nv"))
    return false;
  out->degU = j["degU"].get<int>();
  out->degV = j["degV"].get<int>();
  out->nu = j["nu"].get<int>();
  out->nv = j["nv"].get<int>();
  if (!j.contains("knotsU") || !j["knotsU"].is_array() || !j.contains("knotsV") ||
      !j["knotsV"].is_array())
    return false;
  out->knotsU = j["knotsU"].get<std::vector<double>>();
  out->knotsV = j["knotsV"].get<std::vector<double>>();
  if (!j.contains("ctrl") || !j["ctrl"].is_array() || !j.contains("wts") || !j["wts"].is_array())
    return false;
  out->ctrl.clear();
  for (const auto& jc : j["ctrl"]) {
    ray3d::Vec3 c;
    if (!BrepReadVec3(jc, &c))
      return false;
    out->ctrl.push_back(c);
  }
  out->wts = j["wts"].get<std::vector<double>>();
  // The kernel's own validator is the single source of truth for "is this a usable patch".
  return nurbs::ValidatePatch(*out) == nurbs::PatchProblem::Ok;
}

// ---------------------------------------------------------------------------------------------
// A face's surface. Shared by the face list and by an Intersection edge's carried surfaces.
// ---------------------------------------------------------------------------------------------

[[nodiscard]] inline json BrepSurfaceToJson(const brep::Surface& sf) {
  json j;
  j["kind"] = static_cast<int>(sf.kind);
  j["frame"] = BrepFrameToJson(sf.frame);
  j["r"] = sf.radius;
  j["r2"] = sf.radius2;
  j["h"] = sf.height;
  if (sf.inward)
    j["inward"] = true;
  if (sf.kind == brep::SurfaceKind::Nurbs)  // REQ-315 — the one key that bumps kGsFormatVersion to 4
    j["patch"] = BrepPatchToJson(sf.patch);
  return j;
}

[[nodiscard]] inline bool BrepSurfaceFromJson(const json& j, brep::Surface* out) {
  if (!j.is_object() || !j.contains("kind") || !j.contains("frame"))
    return false;
  const int k = j["kind"].get<int>();
  if (k < static_cast<int>(brep::SurfaceKind::Plane) || k > static_cast<int>(brep::SurfaceKind::Nurbs))
    return false;
  out->kind = static_cast<brep::SurfaceKind>(k);
  if (!BrepFrameFromJson(j["frame"], &out->frame))
    return false;
  out->radius = j.value("r", 0.0);
  out->radius2 = j.value("r2", 0.0);
  out->height = j.value("h", 0.0);
  out->inward = j.value("inward", false);
  if (out->kind == brep::SurfaceKind::Nurbs) {
    if (!j.contains("patch") || !BrepPatchFromJson(j["patch"], &out->patch))
      return false;
  }
  return std::isfinite(out->radius) && std::isfinite(out->radius2) && std::isfinite(out->height);
}

// ---------------------------------------------------------------------------------------------
// The solid. Topology is the stored truth (ADR-045 (c)); the recipe rides along for the Properties
// panel and is never consulted to rebuild geometry.
// ---------------------------------------------------------------------------------------------

[[nodiscard]] inline json SolidToJson(const brep::Solid& s) {
  json o;

  json verts = json::array();
  for (const brep::Vertex& v : s.vertices)
    verts.push_back(BrepVec3ToJson(v.p));
  o["vertices"] = std::move(verts);

  json edges = json::array();
  for (const brep::Edge& e : s.edges) {
    json je;
    je["kind"] = static_cast<int>(e.kind);
    je["v0"] = e.v0;
    je["v1"] = e.v1;
    if (e.kind == brep::CurveKind::Arc || e.kind == brep::CurveKind::Ellipse) {
      je["frame"] = BrepFrameToJson(e.frame);
      je["r"] = e.radius;
      je["sweep"] = e.sweep;
      if (e.kind == brep::CurveKind::Ellipse)
        je["r2"] = e.radius2;
    } else if (e.kind == brep::CurveKind::Intersection) {
      je["witness"] = BrepVec3ToJson(e.frame.origin);
      json surfs = json::array();
      for (const brep::Surface& sf : e.isectSurfaces)
        surfs.push_back(BrepSurfaceToJson(sf));
      je["surfaces"] = std::move(surfs);
    }
    edges.push_back(std::move(je));
  }
  o["edges"] = std::move(edges);

  json faces = json::array();
  for (const brep::Face& f : s.faces) {
    json jf;
    jf["surface"] = BrepSurfaceToJson(f.surface);
    jf["u"] = json::array({f.uStart, f.uEnd});
    jf["v"] = json::array({f.vStart, f.vEnd});
    json loops = json::array();
    for (const brep::Loop& lp : f.loops) {
      json jl = json::array();
      for (const brep::EdgeUse& u : lp.uses)
        jl.push_back(json::array({u.edge, u.reversed ? 1 : 0}));
      loops.push_back(std::move(jl));
    }
    jf["loops"] = std::move(loops);
    // ADR-052 / issue #306: additive, no kGsFormatVersion bump — empty `paramLoops` (the rectangle
    // form, every current builder) writes nothing, so a pre-ADR-052 file round-trips byte-for-byte.
    if (!f.paramLoops.empty()) {
      json paramLoops = json::array();
      for (const auto& poly : f.paramLoops) {
        json jpoly = json::array();
        for (const curveisect::Vec2& pt : poly)
          jpoly.push_back(json::array({pt.x, pt.y}));
        paramLoops.push_back(std::move(jpoly));
      }
      jf["paramLoops"] = std::move(paramLoops);
    }
    faces.push_back(std::move(jf));
  }
  o["faces"] = std::move(faces);

  json shells = json::array();
  for (const brep::Shell& sh : s.shells)
    shells.push_back(sh.faces);
  o["shells"] = std::move(shells);

  json rc;
  rc["kind"] = static_cast<int>(s.recipe.kind);
  rc["frame"] = BrepFrameToJson(s.recipe.frame);
  rc["length"] = s.recipe.length;
  rc["width"] = s.recipe.width;
  rc["height"] = s.recipe.height;
  rc["radius"] = s.recipe.radius;
  rc["radius2"] = s.recipe.radius2;
  rc["sides"] = s.recipe.sides;
  // REQ-317. Written only when there is one, so every pre-REQ-317 solid still serializes exactly as
  // it did - the same additive rule ADR-045 (f) already set for the section as a whole.
  if (!s.recipe.path.segs.empty()) {
    json jp;
    jp["start"] = json::array({s.recipe.path.start.x, s.recipe.path.start.y});
    json segs = json::array();
    for (const brep::PathSeg& ps : s.recipe.path.segs)
      segs.push_back(json::array({ps.end.x, ps.end.y, ps.sweep}));
    jp["segs"] = std::move(segs);
    jp["closed"] = s.recipe.path.closed;
    rc["path"] = std::move(jp);
    rc["justify"] = static_cast<int>(s.recipe.justify);
  }
  o["recipe"] = std::move(rc);

  return o;
}

/// Rebuild one solid. Returns false — writing nothing usable — on any structural problem, so a
/// hand-edited or truncated file is refused rather than partly loaded (REQ-201). The caller
/// validates the result with `brep::Validate` afterwards.
[[nodiscard]] inline bool SolidFromJson(const json& o, brep::Solid* out) {
  if (!o.is_object())
    return false;

  if (o.contains("vertices") && o["vertices"].is_array()) {
    for (const auto& jv : o["vertices"]) {
      brep::Vertex v;
      if (!BrepReadVec3(jv, &v.p))
        return false;
      out->vertices.push_back(v);
    }
  }

  if (o.contains("edges") && o["edges"].is_array()) {
    for (const auto& je : o["edges"]) {
      if (!je.is_object() || !je.contains("kind") || !je.contains("v0") || !je.contains("v1"))
        return false;
      brep::Edge e;
      const int k = je["kind"].get<int>();
      if (k != static_cast<int>(brep::CurveKind::Line) && k != static_cast<int>(brep::CurveKind::Arc) &&
          k != static_cast<int>(brep::CurveKind::Ellipse) &&
          k != static_cast<int>(brep::CurveKind::Intersection))
        return false;
      e.kind = static_cast<brep::CurveKind>(k);
      e.v0 = je["v0"].get<int>();
      e.v1 = je["v1"].get<int>();
      if (e.kind == brep::CurveKind::Arc || e.kind == brep::CurveKind::Ellipse) {
        if (!je.contains("frame") || !BrepFrameFromJson(je["frame"], &e.frame))
          return false;
        if (!je.contains("r") || !je.contains("sweep"))
          return false;
        e.radius = je["r"].get<double>();
        e.sweep = je["sweep"].get<double>();
        if (e.kind == brep::CurveKind::Ellipse) {
          if (!je.contains("r2"))
            return false;
          e.radius2 = je["r2"].get<double>();
        }
      } else if (e.kind == brep::CurveKind::Intersection) {
        if (!je.contains("witness") || !BrepReadVec3(je["witness"], &e.frame.origin))
          return false;
        if (!je.contains("surfaces") || !je["surfaces"].is_array() || je["surfaces"].size() != 2)
          return false;
        for (const auto& js : je["surfaces"]) {
          brep::Surface sf;
          if (!BrepSurfaceFromJson(js, &sf))
            return false;
          e.isectSurfaces.push_back(sf);
        }
      }
      out->edges.push_back(std::move(e));
    }
  }

  if (o.contains("faces") && o["faces"].is_array()) {
    for (const auto& jf : o["faces"]) {
      if (!jf.is_object() || !jf.contains("surface"))
        return false;
      brep::Face f;
      if (!BrepSurfaceFromJson(jf["surface"], &f.surface))
        return false;
      if (jf.contains("u") && jf["u"].is_array() && jf["u"].size() == 2) {
        f.uStart = jf["u"][0].get<double>();
        f.uEnd = jf["u"][1].get<double>();
      }
      if (jf.contains("v") && jf["v"].is_array() && jf["v"].size() == 2) {
        f.vStart = jf["v"][0].get<double>();
        f.vEnd = jf["v"][1].get<double>();
      }
      if (jf.contains("loops") && jf["loops"].is_array()) {
        for (const auto& jl : jf["loops"]) {
          if (!jl.is_array())
            return false;
          brep::Loop lp;
          for (const auto& ju : jl) {
            if (!ju.is_array() || ju.size() != 2)
              return false;
            brep::EdgeUse u;
            u.edge = ju[0].get<int>();
            u.reversed = ju[1].get<int>() != 0;
            lp.uses.push_back(u);
          }
          f.loops.push_back(std::move(lp));
        }
      }
      if (jf.contains("paramLoops") && jf["paramLoops"].is_array()) {
        for (const auto& jpoly : jf["paramLoops"]) {
          if (!jpoly.is_array())
            return false;
          std::vector<curveisect::Vec2> poly;
          for (const auto& jpt : jpoly) {
            if (!jpt.is_array() || jpt.size() != 2)
              return false;
            poly.push_back(curveisect::Vec2{jpt[0].get<double>(), jpt[1].get<double>()});
          }
          f.paramLoops.push_back(std::move(poly));
        }
      }
      out->faces.push_back(std::move(f));
    }
  }

  if (o.contains("shells") && o["shells"].is_array()) {
    for (const auto& js : o["shells"]) {
      if (!js.is_array())
        return false;
      brep::Shell sh;
      for (const auto& fi : js)
        sh.faces.push_back(fi.get<int>());
      out->shells.push_back(std::move(sh));
    }
  }

  if (o.contains("recipe") && o["recipe"].is_object()) {
    const json& rc = o["recipe"];
    const int rk = rc.value("kind", 0);
    if (rk >= static_cast<int>(brep::PrimitiveKind::None) &&
        rk <= static_cast<int>(brep::PrimitiveKind::Polysolid))
      out->recipe.kind = static_cast<brep::PrimitiveKind>(rk);
    if (rc.contains("frame"))
      (void)BrepFrameFromJson(rc["frame"], &out->recipe.frame);  // cosmetic; never rebuilds geometry
    out->recipe.length = rc.value("length", 0.0);
    out->recipe.width = rc.value("width", 0.0);
    out->recipe.height = rc.value("height", 0.0);
    out->recipe.radius = rc.value("radius", 0.0);
    out->recipe.radius2 = rc.value("radius2", 0.0);
    out->recipe.sides = rc.value("sides", 0);
    out->recipe.justify = static_cast<brep::Justify>(rc.value("justify", 0));
    // REQ-317: the one recipe field whose length is not fixed. Read defensively and DISCARDED on a
    // problem rather than refusing the solid - the topology is the stored truth (ADR-045 (c)), so a
    // damaged recipe costs a Properties readout and nothing else.
    if (rc.contains("path") && rc["path"].is_object()) {
      const json& jp = rc["path"];
      brep::Path p;
      bool ok = jp.contains("start") && jp["start"].is_array() && jp["start"].size() == 2;
      if (ok) {
        p.start = ucs::Point2D{jp["start"][0].get<double>(), jp["start"][1].get<double>()};
        p.closed = jp.value("closed", false);
        if (jp.contains("segs") && jp["segs"].is_array()) {
          for (const auto& js : jp["segs"]) {
            if (!js.is_array() || js.size() != 3) {
              ok = false;
              break;
            }
            p.segs.push_back(brep::PathSeg{
                ucs::Point2D{js[0].get<double>(), js[1].get<double>()}, js[2].get<double>()});
          }
        }
      }
      if (ok)
        out->recipe.path = std::move(p);
    }
  }
  return true;
}

}  // namespace gsio
