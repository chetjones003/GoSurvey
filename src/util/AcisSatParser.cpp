#include "AcisSatParser.hpp"

#include "nurbs.hpp"
#include "ray3d.hpp"
#include "ucs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

/// See AcisSatParser.hpp for scope. This file also documents, in one place, the exact SAT field
/// layout this parser understands (a fixed subset of the real ACIS standard schema, chosen because
/// this parser has no real-world SAT corpus to test against and every layout below is exercised by a
/// hand-authored fixture in tests/AcisSatParserTests.cpp — ADR-051's explicitly sanctioned approach).
///
///   body            attrib lump wire transform
///   lump            attrib next shell owner
///   shell           attrib next subshell face wire owner
///   face            attrib next loop owner surface sense("forward"/"reversed") sides("single"/"double")
///   loop            attrib next coedge owner
///   coedge          attrib next previous partner edge sense("forward"/"reversed") owner
///   edge            attrib start-vertex end-vertex curve sense("forward"/"reversed")
///   vertex          attrib edge point
///   point           attrib x y z
///   straight-curve  attrib origin.xyz direction.xyz
///   ellipse-curve   attrib centre.xyz normal.xyz major-axis.xyz radius-ratio
///   plane-surface   attrib origin.xyz normal.xyz refdir.xyz
///   cone-surface    attrib origin.xyz axis.xyz refdir.xyz sin-angle cos-angle major-radius radius-ratio
///   sphere-surface  attrib centre.xyz axis.xyz refdir.xyz radius
///   torus-surface   attrib centre.xyz axis.xyz refdir.xyz major-radius minor-radius
///   spline-surface  attrib degU degV nu nv rational knotsU[nu+degU+1] knotsV[nv+degV+1]
///                          ctrlpt[nu*nv](x y z [w if rational])
///   blend-surface   attrib underlying-surface
///   sweep-surface   attrib underlying-surface
///
/// `spline-surface`, `blend-surface` and `sweep-surface` are this project's own invented schema for
/// GitHub issue #300 (ADR-051 fast-follow), not real ACIS record syntax — like every other record
/// above, ADR-051's rationale for choosing a hand-authored layout over the real one applies. A
/// `spline-surface` carries a `nurbs::Patch` directly (degree/knot/control-point/weight data,
/// row-major control points exactly as `nurbs::Patch::ctrl` documents) rather than ACIS's actual
/// fit-point/approximation encoding — this importer maps it straight onto `SurfaceKind::Nurbs`
/// (REQ-315/ADR-048), so it is naturally limited to ADR-048 (b)'s degree-3-or-less, untrimmed
/// rectangular patch. A `blend-surface`/`sweep-surface` record names the one surface it "reduces to"
/// when representable (a `$-1` pointer means it does not reduce to anything this importer can
/// represent, e.g. a genuinely variable-radius fillet) — real ACIS never says this so plainly, but the
/// alternative is re-deriving a blend/sweep surface's true math from its defining curves, which is out
/// of scope (see AcisSatParser.hpp).
///
/// A record's leading "$" pointer fields resolve to another record's 0-based position in the file
/// (the modern, non-indexed ACIS SAT convention); "$-1" is the null pointer.
///
/// No exceptions (this project builds with them disabled): every step below returns `bool` and
/// writes a specific message to `error_` on the first failure, exactly like `brep::Make*`'s
/// `Problem* outWhy` pattern — a refusal the caller can show, not a crash.

namespace acissat {

namespace {

using ray3d::Vec3;

struct SatRecord {
  std::string type;
  std::vector<std::string> fields;
};

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok)
    out.push_back(tok);
  return out;
}

/// Splits the SAT text into records: skips the 3 mandated ACIS header lines, then splits the
/// remainder on '#' (every record, including the last, ends with one).
std::vector<SatRecord> Tokenize(const std::string& sat) {
  std::vector<std::string> lines;
  {
    std::istringstream iss(sat);
    std::string line;
    while (std::getline(iss, line))
      lines.push_back(line);
  }
  size_t headerLinesSeen = 0;
  size_t bodyStart = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (Trim(lines[i]).empty())
      continue;
    ++headerLinesSeen;
    if (headerLinesSeen == 3) {
      bodyStart = i + 1;
      break;
    }
  }
  std::string body;
  for (size_t i = bodyStart; i < lines.size(); ++i) {
    body += lines[i];
    body += ' ';
  }
  std::vector<SatRecord> records;
  size_t pos = 0;
  while (pos < body.size()) {
    size_t hash = body.find('#', pos);
    std::string chunk = Trim(hash == std::string::npos ? body.substr(pos) : body.substr(pos, hash - pos));
    pos = (hash == std::string::npos) ? body.size() : hash + 1;
    if (chunk.empty())
      continue;
    if (chunk.rfind("End-of-ACIS", 0) == 0)
      continue;
    std::vector<std::string> toks = SplitWhitespace(chunk);
    if (toks.empty())
      continue;
    SatRecord rec;
    rec.type = toks.front();
    rec.fields.assign(toks.begin() + 1, toks.end());
    records.push_back(std::move(rec));
  }
  return records;
}

bool IsFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

constexpr double kTol = 1e-6;
constexpr double kPi = 3.14159265358979323846;

struct BuiltEdge {
  int edgeIndex = -1;
};

/// Everything gathered while walking one ACIS face's loop, before it is turned into a brep::Face.
struct LoopWalk {
  std::vector<brep::EdgeUse> uses;
};

// ---------------------------------------------------------------------------------------------
// The importer. One instance per body; every method returns false and sets `error_` on the first
// unsupported or malformed thing it finds (see file header — no exceptions in this codebase).
// ---------------------------------------------------------------------------------------------

class Importer {
 public:
  Importer(std::vector<SatRecord> recs, std::string label) : recs_(std::move(recs)), label_(std::move(label)) {}

  bool Run(brep::Solid* out) {
    if (!Build(out)) {
      error_ = (label_.empty() ? std::string() : (label_ + ": ")) + error_;
      return false;
    }
    return true;
  }

  const std::string& Error() const { return error_; }

 private:
  const SatRecord* At(int id) const {
    if (id < 0 || static_cast<size_t>(id) >= recs_.size())
      return nullptr;
    return &recs_[static_cast<size_t>(id)];
  }

  int FindFirst(const std::string& type) const {
    for (size_t i = 0; i < recs_.size(); ++i)
      if (recs_[i].type == type)
        return static_cast<int>(i);
    return -1;
  }

  bool Fail(const std::string& msg) {
    error_ = msg;
    return false;
  }

  bool Req(int id, const char* what, const SatRecord** out) {
    const SatRecord* r = At(id);
    if (r == nullptr)
      return Fail(std::string("missing ") + what + " record");
    *out = r;
    return true;
  }

  bool Ptr(const SatRecord& r, size_t field, const char* what, int* out) {
    if (field >= r.fields.size() || r.fields[field].empty() || r.fields[field][0] != '$')
      return Fail(std::string("malformed ") + what + " pointer in a '" + r.type + "' record");
    const char* digits = r.fields[field].c_str() + 1;
    char* end = nullptr;
    const long v = std::strtol(digits, &end, 10);
    if (end == digits || *end != '\0')
      return Fail(std::string("malformed ") + what + " pointer in a '" + r.type + "' record");
    *out = static_cast<int>(v);
    return true;
  }

  bool Num(const SatRecord& r, size_t field, const char* what, double* out) {
    if (field >= r.fields.size())
      return Fail(std::string("missing ") + what + " in a '" + r.type + "' record");
    char* end = nullptr;
    const double v = std::strtod(r.fields[field].c_str(), &end);
    if (end == r.fields[field].c_str())
      return Fail(std::string("malformed ") + what + " in a '" + r.type + "' record");
    *out = v;
    return true;
  }

  bool Vec(const SatRecord& r, size_t field0, const char* what, Vec3* out) {
    return Num(r, field0, what, &out->x) && Num(r, field0 + 1, what, &out->y) &&
           Num(r, field0 + 2, what, &out->z);
  }

  /// A non-negative integer field (a spline-surface's degree/count fields), stored as an ordinary SAT
  /// number token but required to be an exact whole number.
  bool Int(const SatRecord& r, size_t field, const char* what, int* out) {
    double v = 0.0;
    if (!Num(r, field, what, &v))
      return false;
    const double rounded = std::round(v);
    if (std::fabs(v - rounded) > 1e-9 || rounded < 0.0)
      return Fail(std::string("malformed ") + what + " in a '" + r.type + "' record");
    *out = static_cast<int>(rounded);
    return true;
  }

  bool Word(const SatRecord& r, size_t field, const char* what, std::string* out) {
    if (field >= r.fields.size())
      return Fail(std::string("missing ") + what + " in a '" + r.type + "' record");
    *out = r.fields[field];
    return true;
  }

  bool VertexOf(int vertexRecId, brep::Solid* out, int* outIdx) {
    auto it = vertexIndex_.find(vertexRecId);
    if (it != vertexIndex_.end()) {
      *outIdx = it->second;
      return true;
    }
    const SatRecord* v = nullptr;
    if (!Req(vertexRecId, "vertex", &v))
      return false;
    int pointId = 0;
    if (!Ptr(*v, 2, "vertex.point", &pointId))
      return false;
    const SatRecord* p = nullptr;
    if (!Req(pointId, "point", &p))
      return false;
    Vec3 pos{};
    if (!Vec(*p, 1, "point.xyz", &pos))
      return false;
    if (!IsFinite(pos))
      return Fail("vertex has a non-finite coordinate");
    const int idx = static_cast<int>(out->vertices.size());
    out->vertices.push_back(brep::Vertex{pos});
    vertexIndex_[vertexRecId] = idx;
    *outIdx = idx;
    return true;
  }

  bool BuildCircularArc(const SatRecord& c, const Vec3& v0Pos, const Vec3& v1Pos, bool full,
                         brep::Edge* out) {
    Vec3 centre{}, normalRaw{}, majorAxis{};
    double ratio = 0.0;
    if (!Vec(c, 1, "ellipse-curve.centre", &centre) || !Vec(c, 4, "ellipse-curve.normal", &normalRaw) ||
        !Vec(c, 7, "ellipse-curve.major-axis", &majorAxis) ||
        !Num(c, 10, "ellipse-curve.radius-ratio", &ratio))
      return false;
    if (std::fabs(ratio - 1.0) > 1e-4)
      return Fail("edge curve is a true ellipse (radius ratio != 1) — not supported by this importer");
    const Vec3 normal = ray3d::Normalize(normalRaw);
    const double radius = ray3d::Length(majorAxis);
    if (!(radius > kTol))
      return Fail("circular edge has a non-positive radius");
    ucs::Ucs frame;
    if (ray3d::Length(normal) < 0.5 || !ucs::FromNormal(centre, normal, &frame))
      return Fail("circular edge has a degenerate plane normal");
    // Re-align frame X toward v0 so `sweep` measures the actual arc traversed (ucs::FromNormal's X
    // is otherwise arbitrary), matching the convention every other Arc edge in the kernel uses.
    const Vec3 r0 = ray3d::Sub(v0Pos, centre);
    const double a0 = std::atan2(ray3d::Dot(r0, frame.yAxis), ray3d::Dot(r0, frame.xAxis));
    ucs::Ucs f2;
    f2.origin = centre;
    f2.zAxis = frame.zAxis;
    f2.xAxis = ray3d::Normalize(
        ray3d::Add(ray3d::Scale(frame.xAxis, std::cos(a0)), ray3d::Scale(frame.yAxis, std::sin(a0))));
    f2.yAxis = ray3d::Cross(f2.zAxis, f2.xAxis);
    out->kind = brep::CurveKind::Arc;
    out->frame = f2;
    out->radius = radius;
    if (full) {
      out->sweep = 2.0 * kPi;
    } else {
      const Vec3 r1 = ray3d::Sub(v1Pos, centre);
      double sweep = std::atan2(ray3d::Dot(r1, f2.yAxis), ray3d::Dot(r1, f2.xAxis));
      while (sweep <= 0.0)
        sweep += 2.0 * kPi;
      out->sweep = sweep;
    }
    return true;
  }

  bool EdgeOf(int edgeRecId, brep::Solid* out, BuiltEdge* outEdge) {
    auto it = edgeCache_.find(edgeRecId);
    if (it != edgeCache_.end()) {
      *outEdge = it->second;
      return true;
    }
    const SatRecord* e = nullptr;
    if (!Req(edgeRecId, "edge", &e))
      return false;
    int v0Id = 0, v1Id = 0, curveId = 0;
    if (!Ptr(*e, 1, "edge.start-vertex", &v0Id) || !Ptr(*e, 2, "edge.end-vertex", &v1Id) ||
        !Ptr(*e, 3, "edge.curve", &curveId))
      return false;
    const SatRecord* curve = nullptr;
    if (!Req(curveId, "curve", &curve))
      return false;
    int vi0 = 0, vi1 = 0;
    if (!VertexOf(v0Id, out, &vi0) || !VertexOf(v1Id, out, &vi1))
      return false;
    brep::Edge built;
    if (curve->type == "straight-curve") {
      built.kind = brep::CurveKind::Line;
    } else if (curve->type == "ellipse-curve") {
      const bool full = (v0Id == v1Id);
      if (!BuildCircularArc(*curve, out->vertices[static_cast<size_t>(vi0)].p,
                             out->vertices[static_cast<size_t>(vi1)].p, full, &built))
        return false;
    } else {
      return Fail("edge curve kind '" + curve->type + "' is not supported by this importer (see issue #300)");
    }
    built.v0 = vi0;
    built.v1 = vi1;
    const int idx = static_cast<int>(out->edges.size());
    out->edges.push_back(built);
    outEdge->edgeIndex = idx;
    edgeCache_[edgeRecId] = *outEdge;
    return true;
  }

  bool WalkLoop(int loopId, brep::Solid* out, LoopWalk* outLw) {
    const SatRecord* loop = nullptr;
    if (!Req(loopId, "loop", &loop))
      return false;
    int coId = 0;
    if (!Ptr(*loop, 2, "loop.coedge", &coId))
      return false;
    const int firstCoedgeId = coId;
    int guard = 0;
    while (coId >= 0) {
      if (++guard > 4096)
        return Fail("loop's coedge chain never closes (possible ACIS record corruption)");
      const SatRecord* co = nullptr;
      if (!Req(coId, "coedge", &co))
        return false;
      int edgeRecId = 0;
      std::string sense;
      if (!Ptr(*co, 4, "coedge.edge", &edgeRecId) || !Word(*co, 5, "coedge.sense", &sense))
        return false;
      if (sense != "forward" && sense != "reversed")
        return Fail("coedge sense '" + sense + "' is not 'forward'/'reversed'");
      BuiltEdge be;
      if (!EdgeOf(edgeRecId, out, &be))
        return false;
      brep::EdgeUse use;
      use.edge = be.edgeIndex;
      use.reversed = (sense == "reversed");
      outLw->uses.push_back(use);
      int nextCo = 0;
      if (!Ptr(*co, 1, "coedge.next", &nextCo))
        return false;
      if (nextCo == firstCoedgeId)
        break;
      coId = nextCo;
    }
    if (outLw->uses.empty())
      return Fail("loop has no coedges");
    return true;
  }

  /// This SAT schema carries no loop-type field (see the field-layout comment at the top of this
  /// file), so a face's loops arrive in whatever order the ACIS `loop.next` chain happens to list
  /// them — not necessarily outer-boundary-first, which is what `brep::Face::loops` requires
  /// (`loops[0]` is the outer boundary; `Problem::PlaneFaceNotSimple` etc. all trust that order). A
  /// hole is, by construction, smaller than the boundary it is cut from, so the loop with the larger
  /// polygon area IS the outer one — reorder rather than trust ACIS's listing order.
  double PolygonAreaMagnitude(const brep::Solid& out, const ucs::Ucs& planeFrame, const LoopWalk& lw) {
    double acc = 0.0;
    for (const brep::EdgeUse& u : lw.uses) {
      const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
      const int startV = u.reversed ? e.v1 : e.v0;
      const int endV = u.reversed ? e.v0 : e.v1;
      const ucs::Point2D a = ucs::WorldToPlane(planeFrame, out.vertices[static_cast<size_t>(startV)].p);
      const ucs::Point2D b = ucs::WorldToPlane(planeFrame, out.vertices[static_cast<size_t>(endV)].p);
      acc += 0.5 * (a.x * b.y - b.x * a.y);
    }
    return std::fabs(acc);
  }

  void OrderPlaneLoopsOuterFirst(const brep::Solid& out, const ucs::Ucs& planeFrame,
                                  std::vector<LoopWalk>* loops) {
    if (loops->size() < 2)
      return;
    if (PolygonAreaMagnitude(out, planeFrame, (*loops)[1]) > PolygonAreaMagnitude(out, planeFrame, (*loops)[0]))
      std::swap((*loops)[0], (*loops)[1]);
  }

  /// A plane face's boundary has no rectangle restriction — the kernel already accepts an arbitrary
  /// simple polygon of Line/Arc edges. `Face::uStart/uEnd/vStart/vEnd` are unused for
  /// `SurfaceKind::Plane` (brep.hpp).
  bool BuildPlaneFace(const SatRecord& surface, const std::string& faceSense, brep::Face* outFace) {
    Vec3 origin{}, normalRaw{};
    if (!Vec(surface, 1, "plane-surface.origin", &origin) || !Vec(surface, 4, "plane-surface.normal", &normalRaw))
      return false;
    const Vec3 normalN = ray3d::Normalize(normalRaw);
    if (!IsFinite(origin) || !IsFinite(normalN) || ray3d::Length(normalN) < 0.5)
      return Fail("plane-surface has a degenerate origin or normal");
    const Vec3 normal = (faceSense == "reversed") ? ray3d::Scale(normalN, -1.0) : normalN;
    ucs::Ucs frame;
    if (!ucs::FromNormal(origin, normal, &frame))
      return Fail("plane-surface has a degenerate normal");
    outFace->surface.kind = brep::SurfaceKind::Plane;
    outFace->surface.frame = frame;
    return true;
  }

  /// Maps a cone/cylinder face's loop into a general parametric trim loop (`Face::paramLoops`,
  /// ADR-052, issue #306) for the non-full-revolve case `BuildConeFace` cannot reduce to its
  /// rectangular `uStart..uEnd` span — issue #310's concrete case for stopping the outright refusal.
  /// \p frame is the face's own base/axis frame, already computed by the caller from the same axial
  /// extent as the rectangle path. `u` is `atan2(y, x)` in that frame, continuously unwrapped as the
  /// loop is walked (so a loop that happens to cross the frame's own angular seam does not read as a
  /// spurious ~2*pi jump); `v` is the frame-local Z, already zero-based at the face's own axial start
  /// — exactly `LocalSurfaceDerivs`' `SurfaceKind::Cylinder`/`Cone` convention (brep.cpp), so no
  /// rescaling is needed for area/volume/tessellation to agree with it. A curved edge is sampled at
  /// `kArcSamples` points along its own span, per \ref brep::Face::paramLoops' "a curved edge
  /// contributes several polyline vertices" contract; a straight edge contributes just its start
  /// point (its end point is the next edge's start).
  ///
  /// This is exact, and the resulting solid passes `Validate`, only when the loop's real 3D shape
  /// reduces to the `uStart..uEnd x [0, height]` rectangle this function's own bounding box reports
  /// (a partial revolve: two constant-`v` rim edges — arcs or straight generatrix chords whose
  /// endpoints share a height — joined by two constant-`u` generatrix edges). `Validate`'s own
  /// closure probe integrates the face by that reported rectangle, not by `paramLoops` (ADR-052 (c):
  /// the closure probe distrusts `paramLoops` as a possible placeholder), so a loop that is
  /// genuinely non-rectangular in (u, v) — e.g. an oblique planar cut through only part of a
  /// revolve, without an `Ellipse` edge `CylinderCutZExtent` (brep.cpp) can recognize — will build
  /// here but then fail `Validate` with `NotClosed`, refused rather than silently misimported.
  bool BuildConeGeneralTrim(const brep::Solid& out, const LoopWalk& loop, const ucs::Ucs& frame,
                             brep::Face* outFace) {
    constexpr int kArcSamples = 8;
    std::vector<curveisect::Vec2> poly;
    bool haveRaw = false;
    double prevRaw = 0.0;
    double contU = 0.0;
    auto addPoint = [&](const Vec3& p3) {
      const Vec3 local = ucs::WorldToUcs(frame, p3);
      const double rawU = std::atan2(local.y, local.x);
      if (!haveRaw) {
        contU = rawU;
        haveRaw = true;
      } else {
        double delta = rawU - prevRaw;
        while (delta > kPi)
          delta -= 2.0 * kPi;
        while (delta <= -kPi)
          delta += 2.0 * kPi;
        contU += delta;
      }
      prevRaw = rawU;
      poly.push_back(curveisect::Vec2{contU, local.z});
    };
    for (const brep::EdgeUse& u : loop.uses) {
      const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
      const int samples = (e.kind == brep::CurveKind::Line) ? 1 : kArcSamples;
      for (int k = 0; k < samples; ++k) {
        const double tTraverse = static_cast<double>(k) / samples;
        const double s = u.reversed ? (1.0 - tTraverse) : tTraverse;
        addPoint(brep::EdgePointAt(out, e, s));
      }
    }
    if (poly.size() < 3)
      return Fail("cylindrical/conical face's general trim loop has too few points to enclose an area");
    double uLo = poly[0].x, uHi = poly[0].x, vLo = poly[0].y, vHi = poly[0].y;
    for (const curveisect::Vec2& p : poly) {
      uLo = std::min(uLo, p.x);
      uHi = std::max(uHi, p.x);
      vLo = std::min(vLo, p.y);
      vHi = std::max(vHi, p.y);
    }
    outFace->uStart = uLo;
    outFace->uEnd = uHi;
    outFace->vStart = vLo;
    outFace->vEnd = vHi;
    outFace->paramLoops.assign(1, std::move(poly));
    return true;
  }

  /// Recognizes the two cylinder/cone loop shapes this importer maps straight to `Face`'s
  /// rectangular `uStart..uEnd` span (see AcisSatParser.hpp) — any other single-loop shape falls
  /// through to \ref BuildConeGeneralTrim instead. Derives the rectangular span directly from the
  /// recognized shape rather than from a generic vertex scan — see ADR-051 (b-1) for why.
  bool BuildConeFace(const SatRecord& surface, const std::string& faceSense, LoopWalk* loop,
                      brep::Solid* out, brep::Face* outFace) {
    Vec3 axisOrigin{}, axisRaw{};
    double sinA = 0.0, cosA = 0.0, majorRadius = 0.0;
    if (!Vec(surface, 1, "cone-surface.origin", &axisOrigin) || !Vec(surface, 4, "cone-surface.axis", &axisRaw) ||
        !Num(surface, 10, "cone-surface.sin-angle", &sinA) || !Num(surface, 11, "cone-surface.cos-angle", &cosA) ||
        !Num(surface, 12, "cone-surface.major-radius", &majorRadius))
      return false;
    axisRaw = ray3d::Normalize(axisRaw);
    if (!IsFinite(axisOrigin) || !IsFinite(axisRaw) || ray3d::Length(axisRaw) < 0.5)
      return Fail("cone-surface has a degenerate origin or axis");
    if (!(majorRadius > kTol))
      return Fail("cone-surface has a non-positive major radius");

    const size_t n = loop->uses.size();
    auto edgeKind = [&](size_t i) { return out->edges[static_cast<size_t>(loop->uses[i].edge)].kind; };
    bool full = false;
    if (n == 2 && edgeKind(0) == brep::CurveKind::Arc && edgeKind(1) == brep::CurveKind::Arc) {
      const brep::Edge& e0 = out->edges[static_cast<size_t>(loop->uses[0].edge)];
      const brep::Edge& e1 = out->edges[static_cast<size_t>(loop->uses[1].edge)];
      if (e0.v0 == e0.v1 && e1.v0 == e1.v1)
        full = true;
    }
    // A loop that is not the two-full-circle-rim shape above (most commonly a partial revolve — a
    // seam/arc/seam/arc quadrilateral) used to be refused outright (issue #302's original ADR-051
    // (c) narrowing) because deriving its u-span from the seam edges' angular position, while still
    // reporting the rectangle form, was a materially different and untested derivation. Issue #306's
    // `Face::paramLoops` general trim loop removes the need for that derivation entirely: the loop's
    // own edges are sampled directly into a (u,v) polygon (`BuildConeGeneralTrim` below), so the
    // face's true boundary is what area/volume/tessellation/picking (#307-#309) actually see — this
    // is issue #310's concrete case for this surface kind. See `BuildConeGeneralTrim`'s own comment
    // for why a loop that is genuinely non-rectangular in (u, v) still gets refused, via `Validate`.

    // A full revolve's two rim edges (each a full circle, v0 == v1) share no vertex with each other,
    // so brep::Validate's "consecutive edge uses share a vertex" ring-closure check cannot see them as
    // one closed loop on its own. ACIS's periodic-surface loop has no such connecting edge — the
    // surface's own periodicity closes it — so this importer adds one, exactly like the kernel's own
    // MakeCylinder does with its two seam lines: a synthetic Line edge between the two rims' vertices,
    // traversed once each direction. It has no effect on the analytic area/volume (those integrate
    // the surface in closed form from uStart/uEnd, not from the loop's shape) — it exists only to
    // satisfy the topology check.
    if (full) {
      const brep::Edge& e0 = out->edges[static_cast<size_t>(loop->uses[0].edge)];
      const brep::Edge& e1 = out->edges[static_cast<size_t>(loop->uses[1].edge)];
      brep::Edge seam;
      seam.kind = brep::CurveKind::Line;
      seam.v0 = e0.v0;
      seam.v1 = e1.v0;
      if (ray3d::Length(ray3d::Sub(out->vertices[static_cast<size_t>(seam.v1)].p,
                                    out->vertices[static_cast<size_t>(seam.v0)].p)) <= kTol)
        return Fail("cylindrical/conical face's two rims meet at the same point — degenerate");
      const int seamIdx = static_cast<int>(out->edges.size());
      out->edges.push_back(seam);
      LoopWalk synthesized;
      synthesized.uses = {loop->uses[0], {seamIdx, false}, loop->uses[1], {seamIdx, true}};
      *loop = std::move(synthesized);
    }

    // Axial extent, from the vertices the loop actually uses (works for both shapes).
    double tMin = 0.0, tMax = 0.0;
    bool first = true;
    for (const brep::EdgeUse& u : loop->uses) {
      const brep::Edge& e = out->edges[static_cast<size_t>(u.edge)];
      for (int vi : {e.v0, e.v1}) {
        const double t = ray3d::Dot(ray3d::Sub(out->vertices[static_cast<size_t>(vi)].p, axisOrigin), axisRaw);
        if (first || t < tMin)
          tMin = t;
        if (first || t > tMax)
          tMax = t;
        first = false;
      }
    }
    const double height = tMax - tMin;
    if (!(height > kTol))
      return Fail("cylindrical/conical face has zero axial extent");
    const Vec3 base = ray3d::Add(axisOrigin, ray3d::Scale(axisRaw, tMin));

    ucs::Ucs frame;
    if (!ucs::FromNormal(base, axisRaw, &frame))
      return Fail("cone-surface has a degenerate axis");

    const bool isCylinder = std::fabs(sinA) < 1e-9 && cosA > 0.0;
    outFace->surface.kind = isCylinder ? brep::SurfaceKind::Cylinder : brep::SurfaceKind::Cone;
    outFace->surface.frame = frame;
    outFace->surface.height = height;
    if (isCylinder) {
      outFace->surface.radius = majorRadius;
    } else {
      if (std::fabs(cosA) < 1e-12)
        return Fail("cone-surface has a degenerate half-angle");
      const double tanA = sinA / cosA;
      const auto radiusAt = [&](double t) { return majorRadius + t * tanA; };
      const double rBase = radiusAt(tMin);
      const double rTop = radiusAt(tMax);
      if (!(rBase > kTol) || !(rTop >= 0.0))
        return Fail("cone-surface produces a non-positive radius over this face's extent");
      outFace->surface.radius = rBase;
      outFace->surface.radius2 = rTop;
    }
    outFace->surface.inward = (faceSense == "reversed");
    if (full) {
      outFace->uStart = 0.0;
      outFace->uEnd = 2.0 * kPi;
      return true;
    }
    return BuildConeGeneralTrim(*out, *loop, frame, outFace);
  }

  /// Reverses a patch's U parametrisation in place (reflects `knotsU`, reverses each row of `ctrl` /
  /// `wts`) — what a 'reversed' face sense means for a NURBS patch: it flips the sign of `du`, and so
  /// of the outward normal `du x dv`, the same job \ref BuildPlaneFace does by negating the normal and
  /// \ref BuildConeFace does by setting `surface.inward`.
  static void ReversePatchU(nurbs::Patch* p) {
    const double lo = p->knotsU.front();
    const double hi = p->knotsU.back();
    std::vector<double> nk(p->knotsU.size());
    for (size_t i = 0; i < nk.size(); ++i)
      nk[i] = lo + hi - p->knotsU[p->knotsU.size() - 1 - i];
    p->knotsU = std::move(nk);
    std::vector<Vec3> nc(p->ctrl.size());
    std::vector<double> nw(p->wts.size());
    for (int j = 0; j < p->nv; ++j)
      for (int i = 0; i < p->nu; ++i) {
        const size_t src = static_cast<size_t>(j) * static_cast<size_t>(p->nu) +
                           static_cast<size_t>(p->nu - 1 - i);
        const size_t dst = static_cast<size_t>(j) * static_cast<size_t>(p->nu) + static_cast<size_t>(i);
        nc[dst] = p->ctrl[src];
        nw[dst] = p->wts[src];
      }
    p->ctrl = std::move(nc);
    p->wts = std::move(nw);
  }

  /// Parses a `spline-surface` record's degree/knot/control-point/weight fields (see the field-layout
  /// comment at the top of this file) straight onto a `nurbs::Patch` and maps it to
  /// `SurfaceKind::Nurbs` (REQ-315/ADR-048). ADR-048 (b)'s patch is always the **whole, untrimmed**
  /// parameter rectangle, so — unlike `BuildConeFace` — the face's `uStart..vEnd` span is simply the
  /// patch's own domain; it is `VerifySplineLoopIsFullBoundary` below, not this function, that confirms
  /// the loop actually bounds that whole rectangle rather than a proper trim this importer cannot
  /// represent.
  bool BuildSplineFace(const SatRecord& surface, const std::string& faceSense, brep::Face* outFace) {
    int degU = 0, degV = 0, nu = 0, nv = 0, rational = 0;
    if (!Int(surface, 1, "spline-surface.degU", &degU) || !Int(surface, 2, "spline-surface.degV", &degV) ||
        !Int(surface, 3, "spline-surface.nu", &nu) || !Int(surface, 4, "spline-surface.nv", &nv) ||
        !Int(surface, 5, "spline-surface.rational", &rational))
      return false;
    if (degU < 1 || degU > nurbs::kMaxDegree || degV < 1 || degV > nurbs::kMaxDegree)
      return Fail("spline-surface has a degree outside the [1, " + std::to_string(nurbs::kMaxDegree) +
                  "] range this importer's NURBS patch representation supports (ADR-048 (b))");

    nurbs::Patch patch;
    patch.degU = degU;
    patch.degV = degV;
    patch.nu = nu;
    patch.nv = nv;
    size_t field = 6;
    const int knotUCount = nu + degU + 1;
    const int knotVCount = nv + degV + 1;
    if (knotUCount < 0 || knotVCount < 0)
      return Fail("spline-surface has a non-positive control-point count");
    patch.knotsU.resize(static_cast<size_t>(knotUCount));
    for (int i = 0; i < knotUCount; ++i, ++field)
      if (!Num(surface, field, "spline-surface.knotsU", &patch.knotsU[static_cast<size_t>(i)]))
        return false;
    patch.knotsV.resize(static_cast<size_t>(knotVCount));
    for (int i = 0; i < knotVCount; ++i, ++field)
      if (!Num(surface, field, "spline-surface.knotsV", &patch.knotsV[static_cast<size_t>(i)]))
        return false;

    const long long ctrlCount = static_cast<long long>(nu) * static_cast<long long>(nv);
    if (ctrlCount <= 0)
      return Fail("spline-surface has no control points");
    patch.ctrl.resize(static_cast<size_t>(ctrlCount));
    patch.wts.assign(static_cast<size_t>(ctrlCount), 1.0);
    for (long long i = 0; i < ctrlCount; ++i) {
      Vec3 p{};
      if (!Vec(surface, field, "spline-surface.ctrlpt", &p))
        return false;
      field += 3;
      if (rational != 0) {
        double w = 1.0;
        if (!Num(surface, field, "spline-surface.ctrlpt-weight", &w))
          return false;
        ++field;
        if (!(w > 0.0))
          return Fail("spline-surface control point has a non-positive weight");
        patch.wts[static_cast<size_t>(i)] = w;
      }
      patch.ctrl[static_cast<size_t>(i)] = p;
    }

    const nurbs::PatchProblem prob = nurbs::ValidatePatch(patch);
    if (prob != nurbs::PatchProblem::Ok)
      return Fail(std::string("spline-surface patch is invalid: ") + nurbs::PatchProblemText(prob));
    if (faceSense == "reversed")
      ReversePatchU(&patch);

    outFace->surface.kind = brep::SurfaceKind::Nurbs;
    outFace->uStart = nurbs::UMin(patch);
    outFace->uEnd = nurbs::UMax(patch);
    outFace->vStart = nurbs::VMin(patch);
    outFace->vEnd = nurbs::VMax(patch);
    outFace->surface.patch = std::move(patch);
    return true;
  }

  /// A `spline-surface` face's loop must bound the **entire** patch rectangle, corner to corner — this
  /// importer has no way to represent a proper trim (ADR-048 (b) patches are always the full untrimmed
  /// rectangle). Requires exactly 4 edges whose 4 vertices are, as an unordered set within tolerance,
  /// the patch's 4 corner control points (`ctrl[0]`, `ctrl[nu-1]`, `ctrl[(nv-1)*nu]`,
  /// `ctrl[nu*nv-1]`) — the same corners `nurbs::RuledLinear`/`ArcRibbon` place at a Loft/Sweep side
  /// face's own 4-edge loop, so a genuinely untrimmed patch built by this importer's own kernel would
  /// pass this same check.
  bool VerifySplineLoopIsFullBoundary(const brep::Solid& out, const nurbs::Patch& patch,
                                       const LoopWalk& lw) {
    if (lw.uses.size() != 4)
      return false;
    const Vec3 corners[4] = {patch.ctrl[0], patch.ctrl[static_cast<size_t>(patch.nu - 1)],
                              patch.ctrl[static_cast<size_t>((patch.nv - 1) * patch.nu)],
                              patch.ctrl[static_cast<size_t>(patch.nu * patch.nv - 1)]};
    bool used[4] = {false, false, false, false};
    for (const brep::EdgeUse& u : lw.uses) {
      const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
      const Vec3& p = out.vertices[static_cast<size_t>(u.reversed ? e.v1 : e.v0)].p;
      bool matched = false;
      for (int c = 0; c < 4; ++c) {
        if (used[c])
          continue;
        if (ray3d::Length(ray3d::Sub(p, corners[c])) <= kTol) {
          used[c] = true;
          matched = true;
          break;
        }
      }
      if (!matched)
        return false;
    }
    return used[0] && used[1] && used[2] && used[3];
  }

  /// Builds one `brep::Face` for the surface at \p surfaceId, dispatching on its record type.
  /// `blend-surface` and `sweep-surface` records recurse through the surface they name as their
  /// representable reduction (a `$-1` "underlying-surface" pointer means they have none) — see the
  /// field-layout comment at the top of this file.
  bool BuildFaceForSurface(int surfaceId, const std::string& faceSense, std::vector<LoopWalk>* loops,
                            brep::Solid* out, brep::Face* outFace, int depth) {
    if (depth > 8)
      return Fail("surface reduction chain is implausibly deep (possible ACIS record corruption)");
    const SatRecord* surface = nullptr;
    if (!Req(surfaceId, "surface", &surface))
      return false;
    if (surface->type == "plane-surface") {
      if (loops->size() > 2)
        return Fail("planar face has more than one hole loop — not supported by this importer");
      if (!BuildPlaneFace(*surface, faceSense, outFace))
        return false;
      OrderPlaneLoopsOuterFirst(*out, outFace->surface.frame, loops);
      return true;
    }
    if (surface->type == "cone-surface") {
      if (loops->size() != 1)
        return Fail("cylindrical/conical face has a hole loop — not a supported loop shape (issue #302)");
      return BuildConeFace(*surface, faceSense, &loops->front(), out, outFace);
    }
    if (surface->type == "spline-surface") {
      if (loops->size() != 1)
        return Fail("spline-surface face has more than one loop — a trimmed NURBS boundary with holes "
                    "is not supported by this importer (issue #300)");
      if (!BuildSplineFace(*surface, faceSense, outFace))
        return false;
      if (!VerifySplineLoopIsFullBoundary(*out, outFace->surface.patch, loops->front()))
        return Fail(
            "spline-surface face's loop does not bound the whole parametric patch — a proper trim is "
            "not representable by this importer's untrimmed NURBS patch (ADR-048 (b), issue #300)");
      return true;
    }
    if (surface->type == "blend-surface" || surface->type == "sweep-surface") {
      int underlyingId = 0;
      const std::string what = surface->type + ".underlying-surface";
      if (!Ptr(*surface, 1, what.c_str(), &underlyingId))
        return false;
      if (underlyingId < 0)
        return Fail("'" + surface->type +
                    "' does not reduce to a surface this importer can represent — not supported "
                    "(issue #300)");
      return BuildFaceForSurface(underlyingId, faceSense, loops, out, outFace, depth + 1);
    }
    if (surface->type == "sphere-surface" || surface->type == "torus-surface")
      return Fail("surface kind '" + surface->type +
                  "' is recognized but not yet mapped by this importer (fast-follow of issue #299)");
    return Fail("surface kind '" + surface->type + "' is not supported by this importer (see issue #300)");
  }

  bool Build(brep::Solid* out) {
    const int bodyId = FindFirst("body");
    if (bodyId < 0)
      return Fail("no ACIS 'body' record found");
    const SatRecord* body = At(bodyId);
    int wireId = 0, lumpId = 0;
    if (!Ptr(*body, 2, "body.wire", &wireId) || !Ptr(*body, 1, "body.lump", &lumpId))
      return false;
    if (lumpId < 0) {
      if (wireId >= 0)
        return Fail("body is a wire (curves only, no faces) — not a solid; wire import is out of scope (#299)");
      return Fail("body has no lump — nothing to import");
    }
    const SatRecord* lump = nullptr;
    if (!Req(lumpId, "lump", &lump))
      return false;
    int lumpNext = 0;
    if (!Ptr(*lump, 1, "lump.next", &lumpNext))
      return false;
    if (lumpNext >= 0)
      return Fail("body has more than one lump — multi-lump bodies are out of scope for this increment (#299)");
    int shellId = 0;
    if (!Ptr(*lump, 2, "lump.shell", &shellId))
      return false;
    const SatRecord* shell = nullptr;
    if (!Req(shellId, "shell", &shell))
      return false;
    int subshell = 0;
    if (!Ptr(*shell, 2, "shell.subshell", &subshell))
      return false;
    if (subshell >= 0)
      return Fail("shell has a sub-shell — void/nested shells are out of scope for this increment (#299)");

    int faceId = 0;
    if (!Ptr(*shell, 3, "shell.face", &faceId))
      return false;
    int guardFaces = 0;
    while (faceId >= 0) {
      if (++guardFaces > 100000)
        return Fail("face chain never terminates (possible ACIS record corruption)");
      const SatRecord* face = nullptr;
      if (!Req(faceId, "face", &face))
        return false;
      int loopId = 0, surfaceId = 0;
      std::string faceSense;
      if (!Ptr(*face, 2, "face.loop", &loopId) || !Ptr(*face, 4, "face.surface", &surfaceId) ||
          !Word(*face, 5, "face.sense", &faceSense))
        return false;
      if (faceSense != "forward" && faceSense != "reversed")
        return Fail("face sense '" + faceSense + "' is not 'forward'/'reversed'");

      std::vector<LoopWalk> loops;
      {
        int lId = loopId;
        int guardLoops = 0;
        while (lId >= 0) {
          if (++guardLoops > 64)
            return Fail("face has an implausible number of loops (possible ACIS record corruption)");
          LoopWalk lw;
          if (!WalkLoop(lId, out, &lw))
            return false;
          loops.push_back(std::move(lw));
          const SatRecord* lr = nullptr;
          if (!Req(lId, "loop", &lr))
            return false;
          int lNext = 0;
          if (!Ptr(*lr, 1, "loop.next", &lNext))
            return false;
          lId = lNext;
        }
      }

      brep::Face outFace;
      if (!BuildFaceForSurface(surfaceId, faceSense, &loops, out, &outFace, 0))
        return false;
      for (LoopWalk& lw : loops)
        outFace.loops.push_back(brep::Loop{std::move(lw.uses)});
      out->faces.push_back(std::move(outFace));

      int faceNext = 0;
      if (!Ptr(*face, 1, "face.next", &faceNext))
        return false;
      faceId = faceNext;
    }

    if (out->faces.empty())
      return Fail("shell has no faces");
    brep::Shell sh;
    sh.faces.resize(out->faces.size());
    for (size_t i = 0; i < out->faces.size(); ++i)
      sh.faces[i] = static_cast<int>(i);
    out->shells.push_back(std::move(sh));

    const brep::Problem why = brep::Validate(*out);
    if (why != brep::Problem::Ok)
      return Fail(std::string("imported topology failed validation: ") + brep::ProblemText(why));
    return true;
  }

  std::vector<SatRecord> recs_;
  std::string label_;
  std::string error_;
  std::unordered_map<int, int> vertexIndex_;   // ACIS vertex record id -> out.vertices index
  std::unordered_map<int, BuiltEdge> edgeCache_;  // ACIS edge record id -> already-built edge
};

}  // namespace

ImportResult ImportSatSolid(const std::string& sat, const std::string& entityLabel) {
  ImportResult result;
  std::vector<SatRecord> recs = Tokenize(sat);
  if (recs.empty()) {
    result.error = (entityLabel.empty() ? std::string() : entityLabel + ": ") +
                    "ACIS SAT stream is empty or has no records";
    return result;
  }
  Importer importer(std::move(recs), entityLabel);
  brep::Solid solid;
  if (!importer.Run(&solid)) {
    result.ok = false;
    result.error = importer.Error();
    return result;
  }
  result.ok = true;
  result.solid = std::move(solid);
  return result;
}

}  // namespace acissat
