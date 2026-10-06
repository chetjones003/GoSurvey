#include "AcisSatParser.hpp"

#include "brep.hpp"
#include "nurbs.hpp"
#include "ray3d.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace acissat {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

struct SatRecord {
  std::string type;
  std::vector<std::string> fields;
};

std::string Fmt(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return buf;
}

std::string Ref(int id) { return id >= 0 ? ("$" + std::to_string(id)) : "$-1"; }

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<SatRecord> TokenizeBody(const std::string& sat) {
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
    const size_t hash = body.find('#', pos);
    std::string chunk = Trim(hash == std::string::npos ? body.substr(pos) : body.substr(pos, hash - pos));
    pos = (hash == std::string::npos) ? body.size() : hash + 1;
    if (chunk.empty() || chunk.rfind("End-of-ACIS", 0) == 0)
      continue;
    std::istringstream iss(chunk);
    SatRecord rec;
    if (!(iss >> rec.type))
      continue;
    std::string tok;
    while (iss >> tok)
      rec.fields.push_back(tok);
    records.push_back(std::move(rec));
  }
  return records;
}

std::string SerializeSat(const std::string& headerLine1, const std::string& headerLine2,
                         const std::string& headerLine3, const std::vector<SatRecord>& recs) {
  std::ostringstream os;
  os << headerLine1 << '\n';
  os << headerLine2 << '\n';
  os << headerLine3 << '\n';
  for (const SatRecord& r : recs)
    os << r.type << ' ' << [&] {
         std::ostringstream f;
         for (size_t i = 0; i < r.fields.size(); ++i) {
           if (i > 0)
             f << ' ';
           f << r.fields[i];
         }
         return f.str();
       }() << " #\n";
  os << "End-of-ACIS-data\n";
  return os.str();
}

/// Inverse of `NormalizeRealAcisSchema` in AcisSatParser.cpp (GitHub #473 / issue #612).
void ExpandToRealAcisSchema(std::vector<SatRecord>* recs) {
  if (recs == nullptr || recs->empty())
    return;
  const SatRecord* body = nullptr;
  for (const SatRecord& r : *recs) {
    if (r.type == "body") {
      body = &r;
      break;
    }
  }
  if (body == nullptr || body->fields.size() < 2 || body->fields[1][0] == '$')
    return;  // already real or unusable

  int transformId = -1;
  for (size_t i = 0; i < recs->size(); ++i) {
    if ((*recs)[i].type == "transform") {
      transformId = static_cast<int>(i);
      break;
    }
  }
  if (transformId < 0) {
    SatRecord tr;
    tr.type = "transform";
    tr.fields = {"$-1", "-1", "1",  "0", "0", "0",  "0", "1", "0",  "0", "0", "0", "1",
                 "0",   "0",  "0",  "no_rotate", "no_reflect", "no_shear"};
    transformId = static_cast<int>(recs->size());
    recs->push_back(std::move(tr));
  }

  for (SatRecord& r : *recs) {
    const std::vector<std::string>& s = r.fields;
    if (r.type == "body") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], Ref(transformId)};
    } else if (r.type == "lump") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2]};
    } else if (r.type == "shell") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3]};
    } else if (r.type == "face") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3], s[4], s[5], s[6]};
    } else if (r.type == "loop") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2]};
    } else if (r.type == "coedge") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3], s[4], s[5], s[6]};
    } else if (r.type == "edge") {
      r.fields = {"$-1", "-1", "$-1", s[1], "$-1", s[2], "$-1", "$-1", s[3], "I", "I"};
    } else if (r.type == "vertex") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2]};
    } else if (r.type == "point") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3]};
    } else if (r.type == "straight-curve") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3], s[4], s[5], s[6], "I", "I"};
    } else if (r.type == "ellipse-curve") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], "I", "I"};
    } else if (r.type == "plane-surface") {
      r.fields = {"$-1", "-1", "$-1", s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], "I", "I", "I", "I"};
    } else if (r.type == "cone-surface") {
      r.fields = {"$-1", "-1", "$-1", s[1],  s[2],  s[3],  s[4],  s[5],  s[6],
                  s[7],  s[8],  s[9],  "I",   "I",   "I",   s[10], s[11], s[12], s[13], "I", "I", "I", "I"};
    }
  }
}

class SatWriter {
 public:
  explicit SatWriter(std::string* err) : err_(err) {}

  [[nodiscard]] bool emit(const brep::Solid& s) {
    assert(err_ != nullptr);
    if (s.shells.size() != 1)
      return fail("solid has other than one shell — not supported for ACIS export (issue #612)");
    for (const brep::Edge& e : s.edges) {
      if (e.kind == brep::CurveKind::Intersection || e.kind == brep::CurveKind::Ellipse)
        return fail("edge uses a curve kind not supported for ACIS export (issue #612)");
    }
    for (const brep::Face& f : s.faces) {
      if (f.surface.kind == brep::SurfaceKind::Sphere || f.surface.kind == brep::SurfaceKind::Torus)
        return fail("face surface kind not supported for ACIS export (issue #612)");
      if (f.surface.kind == brep::SurfaceKind::Nurbs &&
          nurbs::ValidatePatch(f.surface.patch) != nurbs::PatchProblem::Ok)
        return fail("NURBS face patch is invalid — not exported (issue #612)");
    }

    pointId_.assign(s.vertices.size(), -1);
    vertexId_.assign(s.vertices.size(), -1);
    edgeId_.assign(s.edges.size(), -1);
    curveId_.assign(s.edges.size(), -1);

    for (size_t i = 0; i < s.vertices.size(); ++i) {
      const ray3d::Vec3& p = s.vertices[i].p;
      pointId_[i] = add({"point", Ref(-1), Fmt(p.x), Fmt(p.y), Fmt(p.z)});
      vertexId_[i] = add({"vertex", Ref(-1), Ref(-1), Ref(pointId_[i])});
    }

    for (size_t ei = 0; ei < s.edges.size(); ++ei) {
      const brep::Edge& e = s.edges[ei];
      if (e.kind == brep::CurveKind::Line) {
        const ray3d::Vec3& p0 = s.vertices[static_cast<size_t>(e.v0)].p;
        const ray3d::Vec3& p1 = s.vertices[static_cast<size_t>(e.v1)].p;
        ray3d::Vec3 d = ray3d::Sub(p1, p0);
        const double len = ray3d::Length(d);
        if (!(len > 0.0))
          return fail("degenerate line edge in solid (ACIS export)");
        d = ray3d::Scale(d, 1.0 / len);
        curveId_[ei] = add({"straight-curve", Ref(-1), Fmt(p0.x), Fmt(p0.y), Fmt(p0.z), Fmt(d.x), Fmt(d.y),
                            Fmt(d.z)});
      } else if (e.kind == brep::CurveKind::Arc) {
        const ucs::Ucs& fr = e.frame;
        const ray3d::Vec3 major = ray3d::Scale(fr.xAxis, e.radius);
        curveId_[ei] = add({"ellipse-curve", Ref(-1), Fmt(fr.origin.x), Fmt(fr.origin.y), Fmt(fr.origin.z),
                            Fmt(fr.zAxis.x), Fmt(fr.zAxis.y), Fmt(fr.zAxis.z), Fmt(major.x), Fmt(major.y),
                            Fmt(major.z), "1"});
      } else {
        return fail("unsupported edge curve kind for ACIS export");
      }
      const bool closedArc = e.kind == brep::CurveKind::Arc &&
                             (e.v0 == e.v1 || std::fabs(std::fabs(e.sweep) - kTwoPi) < 1e-6);
      const int satV0 = vertexId_[static_cast<size_t>(e.v0)];
      const int satV1 = closedArc ? satV0 : vertexId_[static_cast<size_t>(e.v1)];
      edgeId_[ei] = add({"edge", Ref(-1), Ref(satV0), Ref(satV1), Ref(curveId_[ei]), "forward"});
    }

    std::vector<int> faceIds;
    faceIds.reserve(s.faces.size());
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      const brep::Face& face = s.faces[fi];
      int surfaceId = -1;
      if (!emitSurface(face.surface, &surfaceId))
        return false;

      std::vector<int> loopIds;
      std::vector<int> loopFirstCoedge;
      for (const brep::Loop& lp : face.loops) {
        if (lp.uses.empty())
          return fail("face loop is empty (ACIS export)");
        const size_t nCo = lp.uses.size();
        const int firstCoId = static_cast<int>(recs_.size());
        for (size_t ci = 0; ci < nCo; ++ci) {
          const brep::EdgeUse& u = lp.uses[ci];
          const char* sense = u.reversed ? "reversed" : "forward";
          const int coId = firstCoId + static_cast<int>(ci);
          const int next = nCo == 1 ? coId : firstCoId + static_cast<int>((ci + 1) % nCo);
          const int prev = nCo == 1 ? coId : firstCoId + static_cast<int>((ci + nCo - 1) % nCo);
          add({"coedge", Ref(-1), Ref(next), Ref(prev), Ref(-1),
               Ref(edgeId_[static_cast<size_t>(u.edge)]), sense, Ref(-1)});
        }
        loopFirstCoedge.push_back(firstCoId);
        loopIds.push_back(add({"loop", Ref(-1), Ref(-1), Ref(firstCoId)}));
      }
      for (size_t li = 0; li < loopIds.size(); ++li) {
        const int lNext = li + 1 < loopIds.size() ? loopIds[li + 1] : -1;
        recs_[static_cast<size_t>(loopIds[li])].fields = {Ref(-1), Ref(lNext), Ref(loopFirstCoedge[li])};
      }
      const char* faceSense = face.surface.inward ? "reversed" : "forward";
      faceIds.push_back(
          add({"face", Ref(-1), Ref(-1), Ref(loopIds.front()), Ref(-1), Ref(surfaceId), faceSense, "single"}));
    }

    for (size_t i = 0; i < faceIds.size(); ++i)
      recs_[static_cast<size_t>(faceIds[i])].fields[1] = Ref(i + 1 < faceIds.size() ? faceIds[i + 1] : -1);

    const int shellId = add({"shell", Ref(-1), Ref(-1), Ref(-1), Ref(faceIds.front()), Ref(-1)});
    const int lumpId = add({"lump", Ref(-1), Ref(-1), Ref(shellId)});
    add({"body", Ref(-1), Ref(lumpId), Ref(-1), Ref(-1)});
    return true;
  }

  [[nodiscard]] std::string finishSimplified() const {
    return SerializeSat("700 0 1 0", "17 GoSurvey 7 32.0.2 NT 24 today", "1 9.9999999999999995e-07 1e-10", recs_);
  }

  [[nodiscard]] std::string finishRealAsm() const {
    const std::string simplified = finishSimplified();
    std::vector<SatRecord> recs = TokenizeBody(simplified);
    ExpandToRealAcisSchema(&recs);
    // Model coordinates are in feet; ACIS header stores mm per model unit (304.8 mm = 1 ft).
    return SerializeSat("700 0 1 0", "8 GoSurvey 19 ASM 223.0.1.1930 NT 24 today",
                        "304.79999999999995 9.999999999999999547e-07 1.000000000000000036e-10", recs);
  }

 private:
  std::vector<SatRecord> recs_;
  std::string* err_;
  std::vector<int> pointId_;
  std::vector<int> vertexId_;
  std::vector<int> edgeId_;
  std::vector<int> curveId_;

  [[nodiscard]] bool fail(const char* msg) {
    if (err_ != nullptr)
      *err_ = msg;
    return false;
  }

  int add(std::initializer_list<std::string> fields) {
    SatRecord rec;
    rec.type = *fields.begin();
    rec.fields.assign(fields.begin() + 1, fields.end());
    const int id = static_cast<int>(recs_.size());
    recs_.push_back(std::move(rec));
    return id;
  }

  [[nodiscard]] bool emitSurface(const brep::Surface& sf, int* outId) {
    assert(outId != nullptr);
    const ucs::Ucs& fr = sf.frame;
    const ray3d::Vec3 o = fr.origin;
    const ray3d::Vec3 z = fr.zAxis;
    const ray3d::Vec3 x = fr.xAxis;
    switch (sf.kind) {
    case brep::SurfaceKind::Plane:
      *outId = add({"plane-surface", Ref(-1), Fmt(o.x), Fmt(o.y), Fmt(o.z), Fmt(z.x), Fmt(z.y), Fmt(z.z),
                    Fmt(x.x), Fmt(x.y), Fmt(x.z)});
      return true;
    case brep::SurfaceKind::Cylinder: {
      const double r = sf.radius;
      *outId = add({"cone-surface", Ref(-1), Fmt(o.x), Fmt(o.y), Fmt(o.z), Fmt(z.x), Fmt(z.y), Fmt(z.z),
                    Fmt(x.x), Fmt(x.y), Fmt(x.z), "0", "1", Fmt(r), "1"});
      return true;
    }
    case brep::SurfaceKind::Cone: {
      const double sinA = sf.radius2 >= 0.0 && sf.height > 0.0
                              ? (sf.radius - sf.radius2) / std::hypot(sf.height, sf.radius - sf.radius2)
                              : 0.0;
      const double cosA = sf.height > 0.0 ? sf.radius / std::hypot(sf.height, sf.radius - sf.radius2) : 1.0;
      *outId = add({"cone-surface", Ref(-1), Fmt(o.x), Fmt(o.y), Fmt(o.z), Fmt(z.x), Fmt(z.y), Fmt(z.z),
                    Fmt(x.x), Fmt(x.y), Fmt(x.z), Fmt(sinA), Fmt(cosA), Fmt(sf.radius), "1"});
      return true;
    }
    case brep::SurfaceKind::Nurbs:
      return emitSplineSurface(sf.patch, outId);
    default:
      return fail("surface kind not supported for ACIS export");
    }
  }

  [[nodiscard]] bool emitSplineSurface(const nurbs::Patch& patchIn, int* outId) {
    assert(outId != nullptr);
    nurbs::Patch patch = patchIn;
    const nurbs::PatchProblem prob = nurbs::ValidatePatch(patch);
    if (prob != nurbs::PatchProblem::Ok)
      return fail("NURBS patch failed validation for ACIS export (issue #612)");
    std::vector<std::string> fields;
    fields.push_back(Ref(-1));
    fields.push_back(std::to_string(patch.degU));
    fields.push_back(std::to_string(patch.degV));
    fields.push_back(std::to_string(patch.nu));
    fields.push_back(std::to_string(patch.nv));
    const bool rational =
        std::any_of(patch.wts.begin(), patch.wts.end(), [](double w) { return std::fabs(w - 1.0) > 1e-9; });
    fields.push_back(rational ? "1" : "0");
    for (double k : patch.knotsU)
      fields.push_back(Fmt(k));
    for (double k : patch.knotsV)
      fields.push_back(Fmt(k));
    for (size_t i = 0; i < patch.ctrl.size(); ++i) {
      const ray3d::Vec3& p = patch.ctrl[i];
      fields.push_back(Fmt(p.x));
      fields.push_back(Fmt(p.y));
      fields.push_back(Fmt(p.z));
      if (rational)
        fields.push_back(Fmt(patch.wts[i]));
    }
    SatRecord rec;
    rec.type = "spline-surface";
    rec.fields = std::move(fields);
    const int id = static_cast<int>(recs_.size());
    recs_.push_back(std::move(rec));
    *outId = id;
    return true;
  }
};

}  // namespace

ExportResult ExportSatSolid(const brep::Solid& solid, std::string_view entityLabel) {
  ExportResult r;
  (void)entityLabel;
  if (brep::Validate(solid) != brep::Problem::Ok) {
    r.error = "solid failed validation — not exported to DWG (issue #612)";
    return r;
  }
  SatWriter w(&r.error);
  if (!w.emit(solid))
    return r;
  r.sat = w.finishRealAsm();
  r.ok = true;
  return r;
}

}  // namespace acissat
