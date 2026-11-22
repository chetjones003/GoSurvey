#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadCurveEditInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/geom2d.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log);

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log) {
  if (PaperLayout* paper = ActivePaperGeometryTarget(st)) {
    // Keep the established JOIN edge walk as the single source of connectivity and path rules.
    // Its input is a small, isolated command state populated only from this layout's paper stores;
    // no model-space geometry is read or modified by this branch.
    AppCommandState paperJoin;
    paperJoin.activeSpaceIndex = kModelSpaceIndex;
    paperJoin.drawing.currentLayer = st.drawing.currentLayer;
    paperJoin.drawing.currentColor = st.drawing.currentColor;
    paperJoin.drawing.userLinesFlat.assign(paper->paperLines.begin(), paper->paperLines.end());
    paperJoin.drawing.userLineAttrs = paper->paperLineAttrs;
    paperJoin.drawing.userPolylineOffsets = paper->paperPolyOffsets;
    paperJoin.drawing.userPolylineVerts.assign(paper->paperPolyVerts.begin(), paper->paperPolyVerts.end());
    paperJoin.drawing.userPolylineVertsBulge = paper->paperPolyVertsBulge;
    paperJoin.drawing.userPolylineClosed = paper->paperPolyClosed;
    paperJoin.drawing.userPolylineAttrs = paper->paperPolyAttrs;

    int unsupported = 0;
    for (const PaperEntityRef& ref : st.selectedPaperEntities) {
      if (ref.type == PaperEntityRef::Type::Line) {
        if (ref.index < 0 || static_cast<size_t>(ref.index) >= paperJoin.drawing.lineCount()) {
          ++unsupported;
          continue;
        }
        paperJoin.selection.push_back({SelectedEntity::Type::LineSeg, ref.index});
      } else if (ref.type == PaperEntityRef::Type::Polyline) {
        if (ref.index < 0 || static_cast<size_t>(ref.index) + 1 >= paperJoin.drawing.userPolylineOffsets.size()) {
          ++unsupported;
          continue;
        }
        paperJoin.selection.push_back({SelectedEntity::Type::Polyline, ref.index});
      } else {
        ++unsupported;
      }
    }
    if (unsupported > 0)
      log.push_back("JOIN — " + std::to_string(unsupported) + " selected paper object" +
                    (unsupported == 1 ? " ignored" : "s ignored") +
                    ": paper JOIN supports lines and polylines.");

    std::vector<std::string> paperLog;
    ExecuteJoinSelection(paperJoin, paperLog);
    const bool joined = std::any_of(paperLog.begin(), paperLog.end(), [](const std::string& entry) {
      return entry.rfind("JOIN — created ", 0) == 0;
    });
    if (!joined) {
      log.insert(log.end(), paperLog.begin(), paperLog.end());
      st.selectedPaperEntities.clear();
      return;
    }

    PushUndoSnapshot(st, "Join paper geometry");
    paper->paperLines.assign(paperJoin.drawing.userLinesFlat.begin(), paperJoin.drawing.userLinesFlat.end());
    paper->paperLineAttrs = std::move(paperJoin.drawing.userLineAttrs);
    paper->paperPolyOffsets = std::move(paperJoin.drawing.userPolylineOffsets);
    paper->paperPolyVerts.assign(paperJoin.drawing.userPolylineVerts.begin(), paperJoin.drawing.userPolylineVerts.end());
    paper->paperPolyVertsBulge = std::move(paperJoin.drawing.userPolylineVertsBulge);
    paper->paperPolyClosed = std::move(paperJoin.drawing.userPolylineClosed);
    paper->paperPolyAttrs = std::move(paperJoin.drawing.userPolylineAttrs);
    st.selectedPaperEntities.clear();
    BumpCadGpuCache(st);
    log.insert(log.end(), paperLog.begin(), paperLog.end());
    return;
  }

  // Same space guard as OVERKILL (issue 02). JOIN reaches model geometry from a sheet because a
  // MODEL selection survives the switch to paper space, so "JOIN - created 1 polyline(s)" could
  // consume two model lines the user could not see. That the selection persists is arguably right;
  // acting on it from a space that cannot show it is not.
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    log.push_back("JOIN — not available in paper space; switch to model space or a floating viewport.");
    return;
  }
  PushUndoSnapshot(st, "Join");
  using ST = SelectedEntity::Type;

  // REQ-087 / REQ-201. Feature lines in the selection are skipped by the edge walk below, and until
  // now that skip was silent — the user would see "JOIN — 2 edges joined" on a selection of three
  // objects and have no way to learn which one was left out. Joining two feature lines is not
  // merely unimplemented: it is undefined, because the shared endpoint would need one elevation and
  // the two lines each supply their own, and nothing in REQ-087 or REQ-088 says which wins.
  int featureLinesSkipped = 0;
  for (const auto& se : st.selection)
    if (se.type == ST::FeatureLine)
      ++featureLinesSkipped;
  if (featureLinesSkipped > 0)
    log.push_back("JOIN — " + std::to_string(featureLinesSkipped) + " feature line" +
                  (featureLinesSkipped == 1 ? "" : "s") +
                  " ignored: joining feature lines has no defined elevation at the shared end.");

  // The same disclosure for surfaces (issue 08). The edge walk below ignores them, and until now it
  // did so silently: real work would be reported ("JOIN — created 1 polyline(s)") while one selected
  // object was quietly left out. That is the failure mode ADR-036 (c) and req068-surface-selection's
  // own header describe - a skip and a success look identical right up until the user notices their
  // surface did not change and cannot find out why. MOVE already names it; this is the neighbour
  // that was missed.
  int surfacesSkipped = 0;
  for (const auto& se : st.selection)
    if (se.type == ST::Surface)
      ++surfacesSkipped;
  if (surfacesSkipped > 0)
    log.push_back("JOIN — " + std::to_string(surfacesSkipped) + " surface" +
                  (surfacesSkipped == 1 ? "" : "s") +
                  " ignored: a surface is derived from its definition, not linework that can be joined.");
  const int splinesSkipped = static_cast<int>(std::count_if(st.selection.begin(), st.selection.end(),
      [](const SelectedEntity& e) { return e.type == ST::Spline; }));
  if (splinesSkipped > 0)
    log.push_back("JOIN — " + std::to_string(splinesSkipped) + " spline" +
                  (splinesSkipped == 1 ? "" : "s") + " ignored: spline joining is not supported.");

  // z0/z1 carry each edge's endpoint elevations through the join (issue 01). Without them the
  // rebuilt polyline was written flat at datum, so joining two lines surveyed at 286 ft produced a
  // polyline at 0 - a silent loss of the one quantity that was actually measured in the field.
  struct Edge {
    float x0, y0, x1, y1;
    float z0 = 0.f, z1 = 0.f;  // issue #373: JOIN must respect Z, not just plan position
    int lineIx;
    int polyIx;
    float bulge = 0.f;  // REQ-316 / ADR-047: bulge traversing x0,y0 -> x1,y1 (0 = straight)
    // REQ-325 / ADR-053: the plane the bulge is measured in — world +Z for every straight edge and
    // every flat arc, an arc's own nx,ny,nz when it is tilted. Meaningless when bulge == 0.
    float nx = 0.f, ny = 0.f, nz = 1.f;
    int arcIx = -1;
  };
  std::vector<Edge> edges;
  float tol = 1e-3f;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    tol = std::max(1e-5f, static_cast<float>(1e-4 * std::max(mxX - mnX, mxY - mnY)));

  auto readLine = [&](int idx, float* x0, float* y0, float* z0, float* x1, float* y1, float* z1) -> bool {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.lineCount())
      return false;
    *x0 = static_cast<float>(st.drawing.line(k).x0);
    *y0 = static_cast<float>(st.drawing.line(k).y0);
    *z0 = static_cast<float>(st.drawing.line(k).z0);
    *x1 = static_cast<float>(st.drawing.line(k).x1);
    *y1 = static_cast<float>(st.drawing.line(k).y1);
    *z1 = static_cast<float>(st.drawing.line(k).z1);
    return true;
  };

  auto polyBulgeAt = [&](int vi) -> float {
    return static_cast<size_t>(vi) < st.drawing.userPolylineVertsBulge.size()
               ? st.drawing.userPolylineVertsBulge[static_cast<size_t>(vi)]
               : 0.f;
  };
  // REQ-325 / ADR-053: a Polyline's own curved segment already carries a plane (e.g. re-joining a
  // polyline a previous JOIN gave a tilted arc) — read it instead of assuming world +Z.
  auto polyNormalAt = [&](int vi, float* nx, float* ny, float* nz) {
    const size_t k = static_cast<size_t>(vi) * 3;
    if (k + 2 < st.drawing.userPolylineVertsNormal.size()) {
      *nx = st.drawing.userPolylineVertsNormal[k];
      *ny = st.drawing.userPolylineVertsNormal[k + 1];
      *nz = st.drawing.userPolylineVertsNormal[k + 2];
    } else {
      *nx = 0.f; *ny = 0.f; *nz = 1.f;
    }
  };

  for (const auto& se : st.selection) {
    if (se.type == ST::LineSeg && se.index >= 0) {
      float x0 = 0.f, y0 = 0.f, z0 = 0.f, x1 = 0.f, y1 = 0.f, z1 = 0.f;
      if (!readLine(se.index, &x0, &y0, &z0, &x1, &y1, &z1))
        continue;
      Edge e{};
      e.x0 = x0; e.y0 = y0; e.x1 = x1; e.y1 = y1; e.z0 = z0; e.z1 = z1;
      e.lineIx = se.index; e.polyIx = -1;
      edges.push_back(e);
    } else if (se.type == ST::Arc && se.index >= 0) {
      // REQ-316 / ADR-047 + REQ-325 / ADR-053: an ARC contributes one bulge edge, flat or tilted —
      // its own plane rides along as this edge's normal (\ref Edge::nx). A tilted arc is only
      // refused later, per-component, when it turns out NOT coplanar with what it would join to.
      const size_t k = static_cast<size_t>(se.index);
      if (k >= st.drawing.userArcs.size())
        continue;
      const CadArc& a = st.drawing.userArcs[k];
      // REQ-325 / ADR-053: an arc's true 3D endpoints are only ever `a.cx + r*cos/sin(angle)` when
      // it is flat — that formula assumes world X/Y are the arc's own in-plane axes, which is exactly
      // what a tilted arc's Arbitrary-Axis-Algorithm frame (`CurvePlane`) does NOT agree with. Using
      // it for a tilted arc silently computed a point nowhere near the real geometry (the bug this
      // fixed: JOIN's endpoint-coincidence test then finds nothing to connect at all).
      const ucs::Ucs plane = CurvePlane(a);
      const ray3d::Vec3 p0 = CurvePointAt(plane, a.r, a.startRad);
      const ray3d::Vec3 p1 = CurvePointAt(plane, a.r, a.startRad + a.sweepRad);
      const float bulge = std::tan(a.sweepRad * 0.25f);
      Edge e{};
      e.x0 = static_cast<float>(p0.x); e.y0 = static_cast<float>(p0.y); e.z0 = static_cast<float>(p0.z);
      e.x1 = static_cast<float>(p1.x); e.y1 = static_cast<float>(p1.y); e.z1 = static_cast<float>(p1.z);
      e.lineIx = -1; e.polyIx = -1; e.bulge = bulge;
      e.nx = a.nx; e.ny = a.ny; e.nz = a.nz;
      e.arcIx = static_cast<int>(k);
      edges.push_back(e);
    } else if (se.type == ST::Polyline && se.index >= 0) {
      const int pi = se.index;
      if ((static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const bool closed =
          static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      for (int vi = v0; vi + 1 < v1; ++vi) {
        const float ax = static_cast<float>(st.drawing.vertex(vi).x);
        const float ay = static_cast<float>(st.drawing.vertex(vi).y);
        const float az = static_cast<float>(st.drawing.vertex(vi).z);
        const float bx = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).x);
        const float by = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).y);
        const float bz = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).z);
        Edge e{};
        e.x0 = ax; e.y0 = ay; e.x1 = bx; e.y1 = by; e.z0 = az; e.z1 = bz;
        e.lineIx = -1; e.polyIx = pi; e.bulge = polyBulgeAt(vi);
        polyNormalAt(vi, &e.nx, &e.ny, &e.nz);
        edges.push_back(e);
      }
      if (closed && v1 - v0 >= 2) {
        const float ax = static_cast<float>(st.drawing.vertex(static_cast<size_t>(v1 - 1)).x);
        const float ay = static_cast<float>(st.drawing.vertex(static_cast<size_t>(v1 - 1)).y);
        const float az = static_cast<float>(st.drawing.vertex(static_cast<size_t>(v1 - 1)).z);
        const float bx = static_cast<float>(st.drawing.vertex(v0).x);
        const float by = static_cast<float>(st.drawing.vertex(v0).y);
        const float bz = static_cast<float>(st.drawing.vertex(v0).z);
        Edge e{};
        e.x0 = ax; e.y0 = ay; e.x1 = bx; e.y1 = by; e.z0 = az; e.z1 = bz;
        e.lineIx = -1; e.polyIx = pi; e.bulge = polyBulgeAt(v1 - 1);
        polyNormalAt(v1 - 1, &e.nx, &e.ny, &e.nz);
        edges.push_back(e);
      }
    }
  }
  if (edges.size() < 2) {
    log.push_back("JOIN — select at least two connected lines, arcs, or polylines.");
    st.selection.clear();
    return;
  }

  const int n = static_cast<int>(edges.size());
  struct UF {
    std::vector<int> p;
    explicit UF(int nn) : p(static_cast<size_t>(nn)) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) {
      return p[static_cast<size_t>(x)] == x ? x : (p[static_cast<size_t>(x)] = find(p[static_cast<size_t>(x)]));
    }
    void unite(int a, int b) {
      a = find(a);
      b = find(b);
      if (a != b)
        p[static_cast<size_t>(a)] = b;
    }
  };
  UF uf(2 * n);
  // issue #373: two endpoints only coincide when they share the same 3D position, not just the
  // same plan (X/Y) position — otherwise a vertical run sharing a plan location at a different
  // elevation gets fused with the wrong endpoint and its vertices are discarded.
  auto nearPt = [&](float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    return dx * dx + dy * dy + dz * dz <= tol * tol;
  };
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const Edge& A = edges[static_cast<size_t>(i)];
      const Edge& B = edges[static_cast<size_t>(j)];
      if (nearPt(A.x0, A.y0, A.z0, B.x0, B.y0, B.z0))
        uf.unite(2 * i, 2 * j);
      if (nearPt(A.x0, A.y0, A.z0, B.x1, B.y1, B.z1))
        uf.unite(2 * i, 2 * j + 1);
      if (nearPt(A.x1, A.y1, A.z1, B.x0, B.y0, B.z0))
        uf.unite(2 * i + 1, 2 * j);
      if (nearPt(A.x1, A.y1, A.z1, B.x1, B.y1, B.z1))
        uf.unite(2 * i + 1, 2 * j + 1);
    }
  }

  auto clusterOf = [&](int ep) { return uf.find(ep); };
  std::vector<char> edgeUsed(static_cast<size_t>(n), 0);
  std::unordered_set<int> lineDel;
  std::unordered_set<int> polyDel;
  std::unordered_set<int> arcDel;  // REQ-316 / ADR-047
  int polysOut = 0;
  int lonelyEdges = 0;  // REQ-316: selected objects that connect to nothing else

  for (int ei = 0; ei < n; ++ei) {
    if (edgeUsed[static_cast<size_t>(ei)])
      continue;
    std::vector<int> comp;
    std::vector<int> stk = {ei};
    edgeUsed[static_cast<size_t>(ei)] = 1;
    while (!stk.empty()) {
      const int cur = stk.back();
      stk.pop_back();
      comp.push_back(cur);
      const int cua = clusterOf(2 * cur);
      const int cub = clusterOf(2 * cur + 1);
      for (int ej = 0; ej < n; ++ej) {
        if (edgeUsed[static_cast<size_t>(ej)])
          continue;
        const int cva = clusterOf(2 * ej);
        const int cvb = clusterOf(2 * ej + 1);
        if (cua == cva || cua == cvb || cub == cva || cub == cvb) {
          edgeUsed[static_cast<size_t>(ej)] = 1;
          stk.push_back(ej);
        }
      }
    }

    // REQ-316 acceptance: a lone object that connects to nothing else in the selection is NOT
    // silently turned into a polyline — the drawing is left unchanged and the miss is reported.
    // A single multi-segment polyline is still allowed to normalise itself (comp holds >1 edge).
    if (comp.size() < 2) {
      ++lonelyEdges;
      continue;
    }

    // REQ-325 / ADR-053: a tilted-arc edge is only accepted into this component when its own plane
    // (already known — REQ-312 gives every ARC its own centre+normal, nothing to derive) agrees, to
    // JOIN's own tolerance, with the FAR endpoint of every edge directly touching it. The far
    // endpoint of a straight neighbor is enough to prove the whole neighbor lies in that plane (it
    // is a straight line, and its near end already sits on the plane by construction); a curved
    // neighbor is checked at its own endpoint only, an increment-1 approximation (full-curve
    // coplanarity is increment 2's rendering work, not JOIN's).
    {
      bool badTiltedArc = false;
      for (int ej : comp) {
        const Edge& E = edges[static_cast<size_t>(ej)];
        if (E.bulge == 0.f || IsFlatNormal(E.nx, E.ny, E.nz) || E.arcIx < 0)
          continue;
        if (static_cast<size_t>(E.arcIx) >= st.drawing.userArcs.size()) {
          badTiltedArc = true;
          break;
        }
        const CadArc& a = st.drawing.userArcs[static_cast<size_t>(E.arcIx)];
        ucs::Ucs arcPlane{};
        if (!ucs::FromNormal(ray3d::Vec3{a.cx, a.cy, a.z}, ray3d::Vec3{a.nx, a.ny, a.nz}, &arcPlane)) {
          badTiltedArc = true;
          break;
        }
        const int k0 = clusterOf(2 * ej), k1 = clusterOf(2 * ej + 1);
        for (int oj : comp) {
          if (oj == ej)
            continue;
          const Edge& N = edges[static_cast<size_t>(oj)];
          const int u = clusterOf(2 * oj), v = clusterOf(2 * oj + 1);
          const bool uTouches = (u == k0 || u == k1), vTouches = (v == k0 || v == k1);
          if (!uTouches && !vTouches)
            continue;
          if (uTouches && vTouches)
            continue;  // both ends of a tiny loop touch the arc — nothing "far" to check
          const ray3d::Vec3 farPt = uTouches ? ray3d::Vec3{N.x1, N.y1, N.z1} : ray3d::Vec3{N.x0, N.y0, N.z0};
          if (std::fabs(ucs::SignedDistanceToPlane(arcPlane, farPt)) > static_cast<double>(tol)) {
            badTiltedArc = true;
            break;
          }
        }
        if (badTiltedArc)
          break;
      }
      if (badTiltedArc) {
        log.push_back("JOIN — skipped a group: a tilted arc does not share a plane with the edges it "
                      "would be joined to.");
        continue;
      }
    }

    struct Pt3 { float x, y, z; };
    std::unordered_map<int, Pt3> rep;
    for (int ej : comp) {
      const Edge& E = edges[static_cast<size_t>(ej)];
      const int k0 = clusterOf(2 * ej);
      const int k1 = clusterOf(2 * ej + 1);
      if (!rep.count(k0))
        rep[k0] = {E.x0, E.y0, E.z0};
      if (!rep.count(k1))
        rep[k1] = {E.x1, E.y1, E.z1};
    }

    std::unordered_map<int, int> deg;
    for (int ej : comp) {
      const int u = clusterOf(2 * ej);
      const int v = clusterOf(2 * ej + 1);
      deg[u]++;
      deg[v]++;
    }
    int odd = 0;
    for (const auto& kv : deg)
      if (kv.second % 2 == 1)
        odd++;
    if (odd != 0 && odd != 2) {
      log.push_back("JOIN — skipped a group (branching junction).");
      continue;
    }

    std::vector<int> clusters;
    clusters.reserve(rep.size());
    for (const auto& kv : rep)
      clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());
    std::unordered_map<int, int> dense;
    for (size_t i = 0; i < clusters.size(); ++i)
      dense[clusters[static_cast<size_t>(i)]] = static_cast<int>(i);
    const int K = static_cast<int>(clusters.size());
    std::vector<std::vector<std::pair<int, int>>> adj(static_cast<size_t>(K));
    std::vector<char> eu(static_cast<size_t>(n), 0);
    for (int ej : comp) {
      const int u = dense[clusterOf(2 * ej)];
      const int v = dense[clusterOf(2 * ej + 1)];
      adj[static_cast<size_t>(u)].push_back({v, ej});
      adj[static_cast<size_t>(v)].push_back({u, ej});
    }

    int start = 0;
    if (odd == 2) {
      start = -1;
      for (const auto& kv : deg) {
        if (kv.second % 2 == 1) {
          start = dense[kv.first];
          break;
        }
      }
      if (start < 0)
        start = 0;
    } else if (!comp.empty())
      start = dense[clusterOf(2 * comp[0])];

    std::vector<std::vector<std::pair<int, int>>> adjW = adj;
    std::vector<int> stkE = {start};
    std::vector<int> stkVia = {-1};  // edge used to arrive at the matching stkE entry
    std::vector<int> pathVerts;
    std::vector<int> pathEdges;  // REQ-316: parallel to pathVerts; pathEdges[i>=1] joins i-1 and i
    while (!stkE.empty()) {
      const int v = stkE.back();
      while (!adjW[static_cast<size_t>(v)].empty() &&
             eu[static_cast<size_t>(adjW[static_cast<size_t>(v)].back().second)])
        adjW[static_cast<size_t>(v)].pop_back();
      if (adjW[static_cast<size_t>(v)].empty()) {
        pathVerts.push_back(v);
        pathEdges.push_back(stkVia.back());
        stkE.pop_back();
        stkVia.pop_back();
      } else {
        const auto pr = adjW[static_cast<size_t>(v)].back();
        adjW[static_cast<size_t>(v)].pop_back();
        const int to = pr.first;
        const int eix = pr.second;
        if (eu[static_cast<size_t>(eix)])
          continue;
        eu[static_cast<size_t>(eix)] = 1;
        stkE.push_back(to);
        stkVia.push_back(eix);
      }
    }
    std::reverse(pathVerts.begin(), pathVerts.end());
    std::reverse(pathEdges.begin(), pathEdges.end());
    if (pathVerts.size() < 2)
      continue;

    std::vector<float> pv;
    std::vector<float> pvBulge;  // REQ-316 / ADR-047: bulge of the segment leaving each vertex
    std::vector<float> pvNormal;  // REQ-325 / ADR-053: plane of that same leaving segment, 3 per vertex
    for (size_t i = 0; i < pathVerts.size(); ++i) {
      const int cid = clusters[static_cast<size_t>(pathVerts[i])];
      const auto& pt = rep[cid];
      pv.push_back(pt.x);
      pv.push_back(pt.y);
      pv.push_back(pt.z);
      // The bulge for the segment LEAVING vertex i is the edge that joins i to i+1 (pathEdges[i+1]),
      // negated when that edge is traversed against its stored x0->x1 direction. A curve's PLANE is
      // not sign-sensitive the way its bulge is — traversing an arc backwards reverses which way it
      // bows (the bulge sign) but not which plane it lies in — so the normal carries straight across.
      float leave = 0.f;
      float leaveNx = 0.f, leaveNy = 0.f, leaveNz = 1.f;
      if (i + 1 < pathVerts.size()) {
        const int e = pathEdges[i + 1];
        if (e >= 0) {
          const Edge& E = edges[static_cast<size_t>(e)];
          const bool forward = clusterOf(2 * e) == cid;  // stored x0 sits at this vertex
          leave = forward ? E.bulge : -E.bulge;
          leaveNx = E.nx; leaveNy = E.ny; leaveNz = E.nz;
        }
      }
      pvBulge.push_back(leave);
      pvNormal.push_back(leaveNx);
      pvNormal.push_back(leaveNy);
      pvNormal.push_back(leaveNz);
    }

    bool closed = pathVerts.front() == pathVerts.back();
    if (closed && pv.size() >= 9) {
      // Drop the repeated closing vertex; its leaving-bulge (the closing segment) moves onto the
      // last kept vertex, which is where userPolylineClosed expects the closing arc.
      const float closeBulge = pvBulge[pvBulge.size() - 2];  // edge from vert n-2 -> repeated last
      const size_t closeN = pvNormal.size() - 6;  // the same edge's normal, 3 floats back from last
      const float closeNx = pvNormal[closeN], closeNy = pvNormal[closeN + 1], closeNz = pvNormal[closeN + 2];
      pv.resize(pv.size() - 3);
      pvBulge.resize(pvBulge.size() - 1);
      pvNormal.resize(pvNormal.size() - 3);
      if (!pvBulge.empty())
        pvBulge.back() = closeBulge;
      if (pvNormal.size() >= 3) {
        pvNormal[pvNormal.size() - 3] = closeNx;
        pvNormal[pvNormal.size() - 2] = closeNy;
        pvNormal[pvNormal.size() - 1] = closeNz;
      }
    }

    if (pv.size() < 6)
      continue;

    if (st.drawing.userPolylineOffsets.empty())
      st.drawing.userPolylineOffsets.push_back(0);
    const int baseVert = st.drawing.userPolylineOffsets.back();
    const int nv = static_cast<int>(pv.size() / 3);
    st.drawing.userPolylineVerts.insert(st.drawing.userPolylineVerts.end(), pv.begin(), pv.end());
    st.drawing.userPolylineOffsets.push_back(baseVert + nv);
    st.drawing.userPolylineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
    st.drawing.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    // REQ-316 / ADR-047: carry the joined bulges through. Only materialise the array when a
    // segment is actually curved, so a straight-only JOIN keeps its export byte-stable.
    {
      bool anyArc = false;
      for (float b : pvBulge)
        if (b != 0.f) { anyArc = true; break; }
      if (anyArc || !st.drawing.userPolylineVertsBulge.empty()) {
        SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
        const size_t tail = st.drawing.userPolylineVertsBulge.size() >= static_cast<size_t>(nv)
                                ? st.drawing.userPolylineVertsBulge.size() - static_cast<size_t>(nv)
                                : 0;
        for (size_t k = 0; k < static_cast<size_t>(nv) && k < pvBulge.size(); ++k)
          st.drawing.userPolylineVertsBulge[tail + k] = pvBulge[k];
      }
      // REQ-325 / ADR-053: same rule, one level up — materialise the normal array only when a
      // joined segment is actually TILTED, so a flat-only JOIN (the overwhelming common case)
      // leaves it untouched.
      bool anyTilted = false;
      for (size_t k = 0; k + 2 < pvNormal.size(); k += 3)
        if (!IsFlatNormal(pvNormal[k], pvNormal[k + 1], pvNormal[k + 2])) { anyTilted = true; break; }
      if (anyTilted || !st.drawing.userPolylineVertsNormal.empty()) {
        SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
        const size_t tail3 = st.drawing.userPolylineVertsNormal.size() >= static_cast<size_t>(nv) * 3
                                 ? st.drawing.userPolylineVertsNormal.size() - static_cast<size_t>(nv) * 3
                                 : 0;
        for (size_t k = 0; k < static_cast<size_t>(nv) * 3 && k < pvNormal.size(); ++k)
          st.drawing.userPolylineVertsNormal[tail3 + k] = pvNormal[k];
      }
    }
    polysOut++;

    for (int ej : comp) {
      const Edge& E = edges[static_cast<size_t>(ej)];
      if (E.lineIx >= 0)
        lineDel.insert(E.lineIx);
      if (E.arcIx >= 0)
        arcDel.insert(E.arcIx);
      if (E.polyIx >= 0)
        polyDel.insert(E.polyIx);
    }
  }

  std::vector<int> pDel(polyDel.begin(), polyDel.end());
  std::sort(pDel.begin(), pDel.end(), std::greater<int>());
  for (int ix : pDel)
    ErasePolylineByIndex(st, ix);

  std::vector<int> lDel(lineDel.begin(), lineDel.end());
  std::sort(lDel.begin(), lDel.end(), std::greater<int>());
  for (int idx : lDel) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.lineCount())
      continue;
    st.drawing.eraseLine(k);
    if (static_cast<size_t>(idx) < st.drawing.userLineAttrs.size())
      st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // REQ-316 / ADR-047: arcs consumed by the join. Descending index, mirror the attr array — the
  // same shape as the line-delete block above.
  std::vector<int> aDel(arcDel.begin(), arcDel.end());
  std::sort(aDel.begin(), aDel.end(), std::greater<int>());
  for (int idx : aDel) {
    if (static_cast<size_t>(idx) >= st.drawing.userArcs.size())
      continue;
    st.drawing.userArcs.erase(st.drawing.userArcs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.userArcAttrs.size())
      st.drawing.userArcAttrs.erase(st.drawing.userArcAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  st.selection.clear();
  if (polysOut > 0) {
    BumpCadGpuCache(st);
    log.push_back("JOIN — created " + std::to_string(polysOut) + " polyline(s).");
    if (lonelyEdges > 0)
      log.push_back("JOIN — " + std::to_string(lonelyEdges) + " object" + (lonelyEdges == 1 ? "" : "s") +
                    " left unchanged: endpoints do not meet any other selected object.");
  } else if (lonelyEdges > 0) {
    log.push_back("JOIN — nothing merged: the selected objects' endpoints do not connect.");
  } else
    log.push_back("JOIN — nothing merged.");
}

// =============================================================================
// OVERKILL — batch cleanup of model geometry or the active paper layout.
//   • Lines: delete zero-length, exact duplicates, merge collinear overlapping
//            / contiguous segments into the shortest covering segment.
//   • Circles: delete exact duplicates (same center + radius).
//   • Arcs: delete arcs whose circle matches an existing full circle; delete
//           exact duplicate arcs (same center/radius/start/sweep).
//   • Polylines: remove zero-length (coincident) vertex steps.
// Tolerance is auto-derived from drawing extents (1e-4 × span, min 1e-6).
// =============================================================================
void ExecuteOverkill(AppCommandState& st, std::vector<std::string>& log) {
  if (PaperLayout* layout = ActivePaperGeometryTarget(st)) {
    PushUndoSnapshot(st, "Overkill paper layout");
    AppCommandState cleanup;
    cleanup.activeSpaceIndex = kModelSpaceIndex;
    cleanup.drawing.userLinesFlat.reserve(layout->paperLines.size());
    for (float value : layout->paperLines)
      cleanup.drawing.userLinesFlat.push_back(value);
    cleanup.drawing.userLineAttrs = layout->paperLineAttrs;
    cleanup.drawing.userArcs = layout->paperArcs;
    cleanup.drawing.userArcAttrs = layout->paperArcAttrs;
    cleanup.drawing.userPolylineOffsets = layout->paperPolyOffsets;
    cleanup.drawing.userPolylineVerts.reserve(layout->paperPolyVerts.size());
    for (float value : layout->paperPolyVerts)
      cleanup.drawing.userPolylineVerts.push_back(value);
    cleanup.drawing.userPolylineClosed = layout->paperPolyClosed;
    cleanup.drawing.userPolylineVertsBulge = layout->paperPolyVertsBulge;
    cleanup.drawing.userPolylineAttrs = layout->paperPolyAttrs;
    for (size_t i = 0; i + 2 < layout->paperCircles.size(); i += 3) {
      cleanup.drawing.pushCircle(CadCircleRecord{layout->paperCircles[i], layout->paperCircles[i + 1], 0.0,
                                         layout->paperCircles[i + 2]});
      PushCircleNormal(cleanup.drawing.userCircleNormals);
    }
    cleanup.drawing.userCircleAttrs = layout->paperCircleAttrs;
    ExecuteOverkill(cleanup, log);
    layout->paperLines.clear();
    layout->paperLines.reserve(cleanup.drawing.userLinesFlat.size());
    for (double value : cleanup.drawing.userLinesFlat)
      layout->paperLines.push_back(static_cast<float>(value));
    layout->paperLineAttrs = std::move(cleanup.drawing.userLineAttrs);
    layout->paperArcs = std::move(cleanup.drawing.userArcs);
    layout->paperArcAttrs = std::move(cleanup.drawing.userArcAttrs);
    layout->paperPolyOffsets = std::move(cleanup.drawing.userPolylineOffsets);
    layout->paperPolyVerts.clear();
    layout->paperPolyVerts.reserve(cleanup.drawing.userPolylineVerts.size());
    for (double value : cleanup.drawing.userPolylineVerts)
      layout->paperPolyVerts.push_back(static_cast<float>(value));
    layout->paperPolyClosed = std::move(cleanup.drawing.userPolylineClosed);
    layout->paperPolyVertsBulge = std::move(cleanup.drawing.userPolylineVertsBulge);
    layout->paperPolyAttrs = std::move(cleanup.drawing.userPolylineAttrs);
    layout->paperCircles.clear();
    layout->paperCircleAttrs = std::move(cleanup.drawing.userCircleAttrs);
    for (size_t i = 0; i < cleanup.drawing.circleCount(); ++i) {
      const CadCircleRecord c = cleanup.drawing.circle(i);
      layout->paperCircles.insert(layout->paperCircles.end(),
                                  {static_cast<float>(c.cx), static_cast<float>(c.cy), static_cast<float>(c.r)});
    }
    st.selectedPaperEntities.clear();
    BumpCadGpuCache(st);
    return;
  }
  // OVERKILL is a MODEL-space tool and has no paper branch (issue 02). Run from a layout it used to
  // ignore the sheet entirely - the only geometry the user can see, and the reason they ran it -
  // while rewriting every model line, and then report "nothing to clean up", which was accurate
  // about neither. The drawing commands have routed by space since issue #84; the editing commands
  // were never given the same treatment. Refusing is the honest half of that fix: acting on a space
  // the user is not looking at is worse than not acting.
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    log.push_back("OVERKILL — not available in paper space; switch to model space or a floating viewport.");
    return;
  }
  PushUndoSnapshot(st, "Overkill");
  // ── tolerance ────────────────────────────────────────────────────────────
  float tol = 1e-3f;
  {
    double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
    if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
      tol = std::max(1e-6f, static_cast<float>(1e-4 * std::max(mxX - mnX, mxY - mnY)));
  }
  const float tolSq = tol * tol;
  int nRemoved = 0;

  // =========================================================================
  // 1. LINE SEGMENTS
  // =========================================================================
  {
    // z0/z1 are load-bearing (issue 01). This working record used to be XY-only, and because the
    // write-back below CLEARS and rebuilds userLinesFlat, every line was re-emitted at datum whether
    // or not it was ever a removal candidate - which is how "OVERKILL - nothing to clean up" and
    // "every elevation in the drawing destroyed" happened in the same run.
    // The coordinates are `double` per ADR-054 Phase A; Z rides in the same width as X and Y.
    struct LSeg { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; double z0 = 0, z1 = 0; EntityAttributes attr; };

    // Snapshot into a working vector that carries attrs
    const size_t nL = st.drawing.lineCount();
    std::vector<LSeg> segs;
    segs.reserve(nL);
    for (size_t i = 0; i < nL; ++i) {
      const size_t k = i;
      segs.push_back({ st.drawing.line(k).x0,     st.drawing.line(k).y0,
                       st.drawing.line(k).x1, st.drawing.line(k).y1,
                       st.drawing.line(k).z0, st.drawing.line(k).z1,
                       i < st.drawing.userLineAttrs.size() ? st.drawing.userLineAttrs[i] : MakeNewEntityAttrs(st) });
    }

    // ── 1a. Zero-length segments ──────────────────────────────────────────
    {
      const size_t before = segs.size();
      segs.erase(std::remove_if(segs.begin(), segs.end(),
                                [&](const LSeg& s) {
                                  const float dx = static_cast<float>(s.x1 - s.x0), dy = static_cast<float>(s.y1 - s.y0);
                                  return dx * dx + dy * dy < tolSq;
                                }),
                 segs.end());
      nRemoved += static_cast<int>(before - segs.size());
    }

    // Canonicalize direction so that any reversed duplicate looks identical:
    // ensure dx > 0, or (dx ≈ 0 and dy > 0), by swapping P0↔P1 if needed.
    for (auto& s : segs) {
      const float dx = static_cast<float>(s.x1 - s.x0), dy = static_cast<float>(s.y1 - s.y0);
      if (dx < 0.f || (std::fabs(dx) < 1e-12f && dy < 0.f)) {
        std::swap(s.x0, s.x1);
        std::swap(s.y0, s.y1);
        std::swap(s.z0, s.z1);  // the elevations belong to the endpoints, so they swap with them
      }
    }

    // ── 1b. Exact duplicates ─────────────────────────────────────────────
    // Sort by (x0, y0, x1, y1); compare adjacent clusters within tol.
    {
      std::sort(segs.begin(), segs.end(), [](const LSeg& a, const LSeg& b) {
        if (a.x0 != b.x0) return a.x0 < b.x0;
        if (a.y0 != b.y0) return a.y0 < b.y0;
        if (a.x1 != b.x1) return a.x1 < b.x1;
        return a.y1 < b.y1;
      });
      std::vector<bool> dead(segs.size(), false);
      for (size_t i = 0; i + 1 < segs.size(); ++i) {
        if (dead[i]) continue;
        for (size_t j = i + 1; j < segs.size(); ++j) {
          if (segs[j].x0 - segs[i].x0 > tol) break; // no more same-x0 candidates
          if (dead[j]) continue;
          const float dx0 = static_cast<float>(segs[j].x0 - segs[i].x0), dy0 = static_cast<float>(segs[j].y0 - segs[i].y0);
          const float dx1 = static_cast<float>(segs[j].x1 - segs[i].x1), dy1 = static_cast<float>(segs[j].y1 - segs[i].y1);
          // Elevation counts (issue 01). Two segments identical in plan but at different heights are
          // different objects - a fence line and the contour beneath it - and treating them as
          // duplicates silently deleted one of them. AutoCAD exposes this as an explicit "Ignore Z"
          // option; that option is not built here, and this is the safe default of the two.
          const float dz0 = static_cast<float>(segs[j].z0 - segs[i].z0), dz1 = static_cast<float>(segs[j].z1 - segs[i].z1);
          if (dx0 * dx0 + dy0 * dy0 < tolSq && dx1 * dx1 + dy1 * dy1 < tolSq &&
              std::fabs(dz0) <= tol && std::fabs(dz1) <= tol) {
            dead[j] = true;
            ++nRemoved;
          }
        }
      }
      std::vector<LSeg> kept;
      kept.reserve(segs.size());
      for (size_t i = 0; i < segs.size(); ++i)
        if (!dead[i]) kept.push_back(std::move(segs[i]));
      segs = std::move(kept);
    }

    // ── 1c. Collinear overlap / contiguous merge ──────────────────────────
    // Two segments are merged when they are:
    //   (a) parallel (|sin angle| < 0.001, ≈ 0.06°), AND
    //   (b) collinear (perpendicular distance < tol), AND
    //   (c) overlapping or touching (1-D intervals on shared axis overlap or touch within tol).
    // Union-Find groups them; within each group, project onto canonical direction,
    // merge the 1-D interval cover, and emit the minimal set of segments.
    {
      const int n = static_cast<int>(segs.size());
      std::vector<int> par(static_cast<size_t>(n));
      std::iota(par.begin(), par.end(), 0);

      // Iterative union-find with path halving
      auto find = [&](int x) {
        while (par[x] != x) { par[x] = par[par[x]]; x = par[x]; }
        return x;
      };
      auto unite = [&](int a, int b) { par[find(a)] = find(b); };

      for (int i = 0; i < n; ++i) {
        const float dxi = static_cast<float>(segs[i].x1 - segs[i].x0);
        const float dyi = static_cast<float>(segs[i].y1 - segs[i].y0);
        const float li  = std::sqrt(dxi * dxi + dyi * dyi);
        if (li < 1e-12f) continue;
        const float uxi = dxi / li, uyi = dyi / li;

        for (int j = i + 1; j < n; ++j) {
          if (find(i) == find(j)) continue; // already same group

          const float dxj = static_cast<float>(segs[j].x1 - segs[j].x0);
          const float dyj = static_cast<float>(segs[j].y1 - segs[j].y0);
          const float lj  = std::sqrt(dxj * dxj + dyj * dyj);
          if (lj < 1e-12f) continue;
          const float uxj = dxj / lj, uyj = dyj / lj;

          // (a) Parallel?
          if (std::fabs(uxi * uyj - uyi * uxj) > 0.001f) continue;

          // (b) Collinear? — perpendicular distance from j.P0 to line through i
          {
            const float vx = static_cast<float>(segs[j].x0 - segs[i].x0);
            const float vy = static_cast<float>(segs[j].y0 - segs[i].y0);
            if (std::fabs(vx * uyi - vy * uxi) > tol) continue;
          }

          // (d) Same elevation profile? Two collinear segments at different heights are different
          // objects and must not merge into one (issue 01) - the merged output can hold only one
          // elevation per endpoint, so merging across a height difference would invent geometry.
          // Each segment defines a linear z along the shared axis; they may merge only where those
          // agree, which is exact for the level case and correct for two pieces of one slope.
          {
            auto zAlong = [&](int s, float t) {
              const float ta = static_cast<float>((segs[s].x0 - segs[i].x0) * uxi + (segs[s].y0 - segs[i].y0) * uyi);
              const float tb = static_cast<float>((segs[s].x1 - segs[i].x0) * uxi + (segs[s].y1 - segs[i].y0) * uyi);
              const float span = tb - ta;
              const float f = (std::fabs(span) > 1e-9f) ? ((t - ta) / span) : 0.f;
              return segs[s].z0 + (segs[s].z1 - segs[s].z0) * f;
            };
            const float tj0 = static_cast<float>((segs[j].x0 - segs[i].x0) * uxi + (segs[j].y0 - segs[i].y0) * uyi);
            const float tj1 = static_cast<float>((segs[j].x1 - segs[i].x0) * uxi + (segs[j].y1 - segs[i].y0) * uyi);
            if (std::fabs(zAlong(i, tj0) - segs[j].z0) > tol ||
                std::fabs(zAlong(i, tj1) - segs[j].z1) > tol)
              continue;
          }

          // (c) Overlap or touch along uxi,uyi?
          {
            const float tj0 = static_cast<float>((segs[j].x0 - segs[i].x0) * uxi + (segs[j].y0 - segs[i].y0) * uyi);
            const float tj1 = static_cast<float>((segs[j].x1 - segs[i].x0) * uxi + (segs[j].y1 - segs[i].y0) * uyi);
            const float jMin = std::min(tj0, tj1);
            const float jMax = std::max(tj0, tj1);
            if (jMax < -tol || jMin > li + tol) continue;
          }

          unite(i, j);
        }
      }

      // Group segments by their union-find root
      std::unordered_map<int, std::vector<int>> groups;
      groups.reserve(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i)
        groups[find(i)].push_back(i);

      std::vector<LSeg> result;
      result.reserve(static_cast<size_t>(n));

      for (auto& [root, members] : groups) {
        (void)root;
        if (members.size() == 1) {
          result.push_back(std::move(segs[static_cast<size_t>(members[0])]));
          continue;
        }

        // Pick canonical direction from the longest member
        int best = members[0];
        double bestLen = 0.0;
        for (int idx : members) {
          const double dx = segs[idx].x1 - segs[idx].x0, dy = segs[idx].y1 - segs[idx].y0;
          const double l = std::sqrt(dx * dx + dy * dy);
          if (l > bestLen) { bestLen = l; best = idx; }
        }
        const double dxb = segs[best].x1 - segs[best].x0;
        const double dyb = segs[best].y1 - segs[best].y0;
        const double lb  = std::sqrt(dxb * dxb + dyb * dyb);
        if (lb < 1e-12) { nRemoved += static_cast<int>(members.size()); continue; }
        const double ux = dxb / lb, uy = dyb / lb;
        const double ox = segs[best].x0, oy = segs[best].y0;

        // Project each member's endpoints onto the canonical axis
        struct Iv { double t0, t1; int idx; };
        std::vector<Iv> ivs;
        ivs.reserve(members.size());
        for (int idx : members) {
          const double ta = (segs[idx].x0 - ox) * ux + (segs[idx].y0 - oy) * uy;
          const double tb = (segs[idx].x1 - ox) * ux + (segs[idx].y1 - oy) * uy;
          ivs.push_back({ std::min(ta, tb), std::max(ta, tb), idx });
        }
        std::sort(ivs.begin(), ivs.end(), [](const Iv& a, const Iv& b) { return a.t0 < b.t0; });

        // Sweep through intervals and merge overlapping/touching ones
        struct Mv { double t0, t1; int idx; }; // idx carries representative attrs
        std::vector<Mv> merged;
        merged.reserve(ivs.size());
        for (const auto& iv : ivs) {
          if (merged.empty() || iv.t0 > merged.back().t1 + tol)
            merged.push_back({ iv.t0, iv.t1, iv.idx });
          else if (iv.t1 > merged.back().t1)
            merged.back().t1 = iv.t1;
        }

        // members.size() originals → merged.size() outputs; log net removal
        nRemoved += static_cast<int>(members.size()) - static_cast<int>(merged.size());

        for (const auto& m : merged) {
          LSeg nl;
          nl.x0   = ox + ux * m.t0;  nl.y0 = oy + uy * m.t0;
          nl.x1   = ox + ux * m.t1;  nl.y1 = oy + uy * m.t1;
          // Every member of this group shares one elevation profile along the axis - test (d) above
          // is what guarantees it - so the representative's profile describes the merged span too.
          {
            const LSeg& r = segs[static_cast<size_t>(m.idx)];
            const double ra = (r.x0 - ox) * ux + (r.y0 - oy) * uy;
            const double rb = (r.x1 - ox) * ux + (r.y1 - oy) * uy;
            const double span = rb - ra;
            auto zAt = [&](double t) {
              const double f = (std::fabs(span) > 1e-9) ? ((t - ra) / span) : 0.0;
              return r.z0 + (r.z1 - r.z0) * f;
            };
            nl.z0 = zAt(m.t0);
            nl.z1 = zAt(m.t1);
          }
          nl.attr = segs[static_cast<size_t>(m.idx)].attr;
          result.push_back(std::move(nl));
        }
      }

      segs = std::move(result);
    }

    // Write back
    st.drawing.userLinesFlat.clear();
    st.drawing.userLineAttrs.clear();
    st.drawing.userLineAttrs.reserve(segs.size());
    for (const auto& s : segs) {
      st.drawing.pushLine(CadLineRecord{s.x0, s.y0, s.z0, s.x1, s.y1, s.z1});
      st.drawing.userLineAttrs.push_back(s.attr);
    }
  }

  // =========================================================================
  // 2. CIRCLES — remove exact duplicates (same center + radius)
  // =========================================================================
  {
    const size_t nC = st.drawing.circleCount();
    struct Circ { double cx, cy, z, r; float nx, ny, nz; EntityAttributes attr; };
    std::vector<Circ> cs;
    cs.reserve(nC);
    for (size_t i = 0; i < nC; ++i) {
      float cnx = 0.f, cny = 0.f, cnz = 1.f;
      CircleNormalAt(st.drawing.userCircleNormals, i, &cnx, &cny, &cnz);
      const CadCircleRecord c = st.drawing.circle(i);
      cs.push_back({ c.cx, c.cy, c.cz, c.r, cnx, cny, cnz,
                     i < st.drawing.userCircleAttrs.size() ? st.drawing.userCircleAttrs[i] : MakeNewEntityAttrs(st) });
    }
    // Sort by radius then center; allows early break on radius mismatch
    std::sort(cs.begin(), cs.end(), [](const Circ& a, const Circ& b) {
      if (a.r != b.r) return a.r < b.r;
      if (a.cx != b.cx) return a.cx < b.cx;
      return a.cy < b.cy;
    });
    std::vector<bool> dead(cs.size(), false);
    for (size_t i = 0; i + 1 < cs.size(); ++i) {
      if (dead[i]) continue;
      for (size_t j = i + 1; j < cs.size(); ++j) {
        if (cs[j].r - cs[i].r > tol) break; // sorted by r; no more matching radii
        if (dead[j]) continue;
        // Z participates: two circles sharing a centre in plan but sitting at different
        // elevations are distinct objects in 3D, not duplicates (REQ-057).
        const double dx = cs[j].cx - cs[i].cx, dy = cs[j].cy - cs[i].cy, dz = cs[j].z - cs[i].z;
        const double dr = cs[j].r  - cs[i].r;
        // Two circles sharing a centre and a radius but lying in different planes are distinct
        // objects, not duplicates (REQ-312) - the same reasoning Z already gets just above.
        if (cs[j].nx != cs[i].nx || cs[j].ny != cs[i].ny || cs[j].nz != cs[i].nz) continue;
        if (dx * dx + dy * dy + dz * dz < tolSq && dr * dr < tolSq) { dead[j] = true; ++nRemoved; }
      }
    }
    st.drawing.userCirclesCxCyZR.clear();
    st.drawing.userCircleAttrs.clear();
    st.drawing.userCircleNormals.clear();
    for (size_t i = 0; i < cs.size(); ++i) {
      if (!dead[i]) {
        st.drawing.pushCircle(CadCircleRecord{cs[i].cx, cs[i].cy, cs[i].z, cs[i].r});
        st.drawing.userCircleAttrs.push_back(cs[i].attr);
        PushCircleNormal(st.drawing.userCircleNormals, cs[i].nx, cs[i].ny, cs[i].nz);
      }
    }
  }

  // =========================================================================
  // 3. ARCS
  //   3a. Delete arcs whose (center, radius) matches an existing full circle.
  //   3b. Delete exact duplicate arcs (same center/radius/startRad/sweepRad).
  // =========================================================================
  {
    const size_t nA = st.drawing.userArcs.size();
    const size_t nC = st.drawing.circleCount();
    std::vector<bool> dead(nA, false);

    // 3a — arcs over full circles
    for (size_t i = 0; i < nA; ++i) {
      if (dead[i]) continue;
      const CadArc& a = st.drawing.userArcs[i];
      for (size_t c = 0; c < nC; ++c) {
        const CadCircleRecord cc = st.drawing.circle(c);
        const double dx = a.cx - cc.cx;
        const double dy = a.cy - cc.cy;
        const double dr = a.r  - cc.r;
        if (dx * dx + dy * dy < tolSq && dr * dr < tolSq) { dead[i] = true; ++nRemoved; break; }
      }
    }

    // 3b — exact duplicate arcs
    // Normalize startRad to [0, 2π) for comparison
    constexpr float kTwoPi = 2.f * 3.14159265358979f;
    auto normAngle = [&](float a) -> float {
      a = std::fmod(a, kTwoPi);
      if (a < 0.f) a += kTwoPi;
      return a;
    };
    constexpr float kAngTol = 1e-4f; // ≈ 0.006°
    for (size_t i = 0; i < nA; ++i) {
      if (dead[i]) continue;
      const CadArc& a = st.drawing.userArcs[i];
      const float aStart = normAngle(a.startRad);
      for (size_t j = i + 1; j < nA; ++j) {
        if (dead[j]) continue;
        const CadArc& b = st.drawing.userArcs[j];
        const double dxC = a.cx - b.cx, dyC = a.cy - b.cy, drR = a.r - b.r;
        if (dxC * dxC + dyC * dyC >= tolSq || drR * drR >= tolSq) continue;
        const float dStart = std::fabs(aStart - normAngle(b.startRad));
        const float dSweep = std::fabs(a.sweepRad - b.sweepRad);
        // Handle 0 vs 2π wrap for start angle
        const float dStartW = std::min(dStart, std::fabs(dStart - kTwoPi));
        if (dStartW < kAngTol && dSweep < kAngTol) { dead[j] = true; ++nRemoved; }
      }
    }

    std::vector<CadArc>           newArcs;
    std::vector<EntityAttributes> newArcAttrs;
    newArcs.reserve(nA);
    newArcAttrs.reserve(nA);
    for (size_t i = 0; i < nA; ++i) {
      if (!dead[i]) {
        newArcs.push_back(st.drawing.userArcs[i]);
        newArcAttrs.push_back(i < st.drawing.userArcAttrs.size() ? st.drawing.userArcAttrs[i] : MakeNewEntityAttrs(st));
      }
    }
    st.drawing.userArcs     = std::move(newArcs);
    st.drawing.userArcAttrs = std::move(newArcAttrs);
  }

  // =========================================================================
  // 4. POLYLINES — remove zero-length (coincident) vertex steps
  // =========================================================================
  {
    const int nP = static_cast<int>(st.drawing.userPolylineOffsets.size()) > 0
                     ? static_cast<int>(st.drawing.userPolylineOffsets.size()) - 1
                     : 0;
    // Collect polylines to erase (those that become degenerate after cleanup).
    // Iterate high→low so earlier polyline offsets stay valid while we mutate.
    std::vector<int> polyToErase;

    for (int pi = nP - 1; pi >= 0; --pi) {
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi) + 1];
      if (v1 - v0 < 2) { polyToErase.push_back(pi); continue; }

      const bool closed = static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() &&
                          st.drawing.userPolylineClosed[static_cast<size_t>(pi)];

      // Collect this polyline's vertices, elevation included (issue 01) - the rebuild below used to
      // write every Z back as 0, flattening any polyline OVERKILL touched.
      const std::vector<PolyVert> verts = PolylineVertsOf(st, pi);

      // Remove consecutive duplicate vertices (zero-length steps)
      std::vector<PolyVert> clean;
      clean.reserve(verts.size());
      clean.push_back(verts[0]);
      for (size_t k = 1; k < verts.size(); ++k) {
        const float dx = verts[k].x - clean.back().x;
        const float dy = verts[k].y - clean.back().y;
        if (dx * dx + dy * dy >= tolSq)
          clean.push_back(verts[k]);
        else
          ++nRemoved;
      }
      // For closed polylines, also remove zero-length wrap (last vertex ≈ first)
      if (closed) {
        while (clean.size() >= 2) {
          const float dx = clean.back().x - clean.front().x;
          const float dy = clean.back().y - clean.front().y;
          if (dx * dx + dy * dy < tolSq) { clean.pop_back(); ++nRemoved; }
          else break;
        }
      }

      if (clean.size() < 2)   { polyToErase.push_back(pi); continue; }
      if (clean.size() == verts.size()) continue; // unchanged

      // Replace the vertex slice [v0*3, v1*3) with cleaned data
      const int nNew  = static_cast<int>(clean.size());
      const int delta = nNew - (v1 - v0);
      st.drawing.eraseVertexRange(static_cast<size_t>(v0), static_cast<size_t>(v1));

      std::vector<float> newV;
      newV.reserve(static_cast<size_t>(nNew) * 3);
      for (const auto& p : clean) { newV.push_back(p.x); newV.push_back(p.y); newV.push_back(p.z); }
      st.drawing.userPolylineVerts.insert(
          st.drawing.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
          newV.begin(), newV.end());

      // Adjust all offsets after pi by the vertex delta
      for (size_t oi = static_cast<size_t>(pi) + 1; oi < st.drawing.userPolylineOffsets.size(); ++oi)
        st.drawing.userPolylineOffsets[oi] += delta;
    }

    // polyToErase is in descending order (loop ran high→low, push_back is stable)
    for (int pi : polyToErase)
      ErasePolylineByIndex(st, pi);
    SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());  // REQ-316 / ADR-047
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
  }

  // ── report ────────────────────────────────────────────────────────────────
  st.selection.clear();
  if (nRemoved > 0) {
    BumpCadGpuCache(st);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "OVERKILL — cleaned up %d object(s).", nRemoved);
    log.push_back(buf);
  } else {
    log.push_back("OVERKILL — nothing to clean up.");
  }
  // Say what was NOT considered (issue 08, REQ-201). OVERKILL never reported skipping anything of
  // any kind, so on a drawing with surfaces "nothing to clean up" could be read as "your surfaces
  // are clean" when they were never examined. Surfaces are derived from their definitions and are
  // not duplicate linework, so excluding them is correct - being silent about it was not.
  if (!st.drawing.cadSurfaces.empty())
    log.push_back("OVERKILL — " + std::to_string(st.drawing.cadSurfaces.size()) + " surface" +
                  (st.drawing.cadSurfaces.size() == 1 ? " was" : "s were") +
                  " not examined: a surface is derived from its definition, not duplicate linework.");
}

void StartJoinCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  if (ActivePaperGeometryTarget(st)) {
    if (!st.selectedPaperEntities.empty()) {
      ExecuteJoinSelection(st, log);
      return;
    }
    st.active = AppCommandState::Kind::Join;
    st.selBoxWaitingSecond = false;
    log.push_back("JOIN — click paper objects or drag a selection window (Enter when done) to join "
                  "connected lines/polylines. ESC cancels.");
    return;
  }
  if (!st.selection.empty()) {
    ExecuteJoinSelection(st, log);
    return;
  }
  st.active = AppCommandState::Kind::Join;
  st.selBoxWaitingSecond = false;
  log.push_back("JOIN — click objects or drag a selection window (Enter when done) to join lines/"
                "polylines that meet at endpoints. ESC cancels.");
}
