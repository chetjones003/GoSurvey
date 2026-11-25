// CadCommands_DrawInquiry.cpp — LEADER, POINT, PEDIT, MEASURE, DIVIDE, AREA, LIST (REQ-456).

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"

#include <GoSurvey/Core/NumFormat.hpp>
#include <GoSurvey/Core/StringUtil.hpp>
#include "io/src/DataExtract.hpp"
#include "GoSurvey/Document/cadtablestyle.hpp"
#include "SpreadsheetIo.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Geometry/spline.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kJoinTol = 1.0e-3;

[[nodiscard]] std::string UpperTrim(const std::string& raw) {
  std::string s = StringUtil::trimCopy(raw);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return s;
}

template <typename SessionT>
SessionT* BeginDrawInquirySession(AppCommandState& st, std::unique_ptr<SessionT> session,
                                  AppCommandState::Kind /*kind*/) {
  ClearPendingViewportZoom(st);
  // Ribbon commands can replace an in-progress MLEADER without passing through Escape. Clear its
  // cursor/label draft as part of the command transition or stale MLEADER dynamic input remains
  // painted while TABLE (and other draw/inquiry commands) owns the active prompt.
  ResetMleaderDraft(st);
  if (st.mtextRichEditorMultileaderIndex >= 0)
    CancelMtextRichEditor(st, nullptr);
  st.selectedSurveyPointIndices.clear();
  SessionT* raw = session.get();
  stepengine::BeginSession(st, std::move(session));
  return raw;
}

[[nodiscard]] int LinearPrecision(const AppCommandState& st) {
  return DisplayPrecisionClamp(st.displayLinearPrecision);
}

void CommitCadPointToActiveSpace(AppCommandState& st, const CadPoint& p) {
  assert(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    L->paperPoints.push_back(p);
    L->paperPointAttrs.push_back(MakeNewEntityAttrs(st));
    return;
  }
  st.drawing.cadPoints.push_back(p);
  st.drawing.cadPointAttrs.push_back(MakeNewEntityAttrs(st));
}

struct PathSample {
  std::vector<spline::Vec3> pts;
  bool closed = false;
};

void AppendBulgeSegment(PathSample* out, const spline::Vec3& a, const spline::Vec3& b, float bulge) {
  assert(out != nullptr);
  const BulgeArcSpan span = BulgeArc(a.x, a.y, b.x, b.y, bulge);
  if (!span.valid) {
    out->pts.push_back(b);
    return;
  }
  constexpr int kSeg = 16;
  for (int i = 1; i <= kSeg; ++i) {
    const double ang = span.startAngle + span.sweep * static_cast<double>(i) / static_cast<double>(kSeg);
    out->pts.push_back({span.cx + span.radius * std::cos(ang), span.cy + span.radius * std::sin(ang), a.z});
  }
}

[[nodiscard]] double PathLength(const PathSample& path) {
  if (path.pts.size() < 2)
    return 0.0;
  double total = 0.0;
  const size_t n = path.pts.size();
  const size_t segs = path.closed ? n : n - 1;
  for (size_t i = 0; i < segs; ++i) {
    const spline::Vec3& a = path.pts[i];
    const spline::Vec3& b = path.pts[(i + 1) % n];
    total += std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
  }
  return total;
}

bool PointAtLength(const PathSample& path, double dist, spline::Vec3* out) {
  assert(out != nullptr);
  if (path.pts.size() < 2)
    return false;
  const double len = PathLength(path);
  if (!(len > 1e-12) || dist < -1e-12 || dist > len + 1e-9)
    return false;
  double remain = std::max(0.0, dist);
  const size_t n = path.pts.size();
  const size_t segs = path.closed ? n : n - 1;
  for (size_t i = 0; i < segs; ++i) {
    const spline::Vec3& a = path.pts[i];
    const spline::Vec3& b = path.pts[(i + 1) % n];
    const double seg = std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
    if (remain <= seg + 1e-12) {
      const double t = seg > 1e-12 ? remain / seg : 0.0;
      out->x = a.x + t * (b.x - a.x);
      out->y = a.y + t * (b.y - a.y);
      out->z = a.z + t * (b.z - a.z);
      return true;
    }
    remain -= seg;
  }
  *out = path.pts.back();
  return true;
}

void FillPolyPath(const std::vector<double>& verts, const std::vector<float>& bulge, int v0, int v1, bool closed,
                  PathSample* out) {
  assert(out != nullptr);
  out->pts.clear();
  out->closed = closed;
  if (v1 - v0 < 2)
    return;
  const size_t a0 = static_cast<size_t>(v0) * 3;
  out->pts.push_back({verts[a0], verts[a0 + 1], verts[a0 + 2]});
  const int nVert = v1 - v0;
  const int nSeg = closed ? nVert : nVert - 1;
  for (int s = 0; s < nSeg; ++s) {
    const int ia = v0 + s;
    const int ib = v0 + ((s + 1) % nVert);
    const size_t a = static_cast<size_t>(ia) * 3;
    const size_t b = static_cast<size_t>(ib) * 3;
    const spline::Vec3 pa{verts[a], verts[a + 1], verts[a + 2]};
    const spline::Vec3 pb{verts[b], verts[b + 1], verts[b + 2]};
    const float bg = static_cast<size_t>(ia) < bulge.size() ? bulge[static_cast<size_t>(ia)] : 0.f;
    AppendBulgeSegment(out, pa, pb, bg);
  }
}

void FillPolyPathF(const std::vector<float>& verts, const std::vector<float>& bulge, int v0, int v1, bool closed,
                   PathSample* out) {
  std::vector<double> d(verts.begin(), verts.end());
  FillPolyPath(d, bulge, v0, v1, closed, out);
}

void ReverseOpenPathIfPickNearerEnd(PathSample* path, double pickX, double pickY) {
  assert(path != nullptr);
  assert(std::isfinite(pickX) && std::isfinite(pickY));
  if (path->closed || path->pts.size() < 2)
    return;
  const spline::Vec3& a = path->pts.front();
  const spline::Vec3& b = path->pts.back();
  const double da = (a.x - pickX) * (a.x - pickX) + (a.y - pickY) * (a.y - pickY);
  const double db = (b.x - pickX) * (b.x - pickX) + (b.y - pickY) * (b.y - pickY);
  if (db < da)
    std::reverse(path->pts.begin(), path->pts.end());
}

void FillLinePath(const spline::Vec3& a, const spline::Vec3& b, double pickX, double pickY, PathSample* out) {
  assert(out != nullptr);
  assert(std::isfinite(pickX) && std::isfinite(pickY));
  const double da = (a.x - pickX) * (a.x - pickX) + (a.y - pickY) * (a.y - pickY);
  const double db = (b.x - pickX) * (b.x - pickX) + (b.y - pickY) * (b.y - pickY);
  out->closed = false;
  out->pts = (da <= db) ? std::vector<spline::Vec3>{a, b} : std::vector<spline::Vec3>{b, a};
}

void FillCirclePath(double cx, double cy, double cz, double r, double pickX, double pickY, PathSample* out) {
  assert(out != nullptr);
  assert(std::isfinite(cx) && std::isfinite(cy) && std::isfinite(r));
  out->pts.clear();
  out->closed = true;
  if (!(r > 1e-12))
    return;
  const double start = std::atan2(pickY - cy, pickX - cx);
  constexpr int kSeg = 64;
  for (int i = 0; i <= kSeg; ++i) {
    const double a = start + kTwoPi * static_cast<double>(i) / static_cast<double>(kSeg);
    out->pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a), cz});
  }
}

bool ModelPathFromEntity(const AppCommandState& st, const SelectedEntity& e, double pickX, double pickY,
                         PathSample* out) {
  assert(out != nullptr);
  out->pts.clear();
  out->closed = false;
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t li = static_cast<size_t>(e.index);
    if (li >= st.drawing.lineCount())
      return false;
    const CadLineRecord ln = st.drawing.line(li);
    FillLinePath({ln.x0, ln.y0, ln.z0}, {ln.x1, ln.y1, ln.z1}, pickX, pickY, out);
    return true;
  }
  if (e.type == T::Polyline) {
    if (e.index < 0 || static_cast<size_t>(e.index) + 1 >= st.drawing.userPolylineOffsets.size())
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(e.index)];
    const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(e.index) + 1];
    const bool closed = static_cast<size_t>(e.index) < st.drawing.userPolylineClosed.size() &&
                        st.drawing.userPolylineClosed[static_cast<size_t>(e.index)] != 0;
    FillPolyPath(st.drawing.userPolylineVerts, st.drawing.userPolylineVertsBulge, v0, v1, closed, out);
    ReverseOpenPathIfPickNearerEnd(out, pickX, pickY);
    return out->pts.size() >= 2;
  }
  if (e.type == T::Circle) {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= st.drawing.circleCount())
      return false;
    const CadCircleRecord c = st.drawing.circle(ci);
    FillCirclePath(c.cx, c.cy, c.cz, c.r, pickX, pickY, out);
    return !out->pts.empty();
  }
  return false;
}

bool PaperPathFromRef(const PaperLayout& L, const PaperEntityRef& r, double pickX, double pickY, PathSample* out) {
  assert(out != nullptr);
  out->pts.clear();
  out->closed = false;
  if (r.type == PaperEntityRef::Type::Line) {
    const size_t k = static_cast<size_t>(r.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return false;
    FillLinePath({L.paperLines[k], L.paperLines[k + 1], L.paperLines[k + 2]},
                 {L.paperLines[k + 3], L.paperLines[k + 4], L.paperLines[k + 5]}, pickX, pickY, out);
    return true;
  }
  if (r.type == PaperEntityRef::Type::Polyline) {
    if (r.index < 0 || static_cast<size_t>(r.index) + 1 >= L.paperPolyOffsets.size())
      return false;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(r.index)];
    const int v1 = L.paperPolyOffsets[static_cast<size_t>(r.index) + 1];
    const bool closed = static_cast<size_t>(r.index) < L.paperPolyClosed.size() &&
                        L.paperPolyClosed[static_cast<size_t>(r.index)] != 0;
    FillPolyPathF(L.paperPolyVerts, L.paperPolyVertsBulge, v0, v1, closed, out);
    ReverseOpenPathIfPickNearerEnd(out, pickX, pickY);
    return out->pts.size() >= 2;
  }
  if (r.type == PaperEntityRef::Type::Circle) {
    const size_t k = static_cast<size_t>(r.index) * 3;
    if (k + 2 >= L.paperCircles.size())
      return false;
    const double cx = L.paperCircles[k];
    const double cy = L.paperCircles[k + 1];
    const double rcirc = L.paperCircles[k + 2];
    FillCirclePath(cx, cy, 0.0, rcirc, pickX, pickY, out);
    return !out->pts.empty();
  }
  return false;
}

void PlacePointsAlong(AppCommandState& st, const PathSample& path, const std::vector<double>& stations,
                      std::vector<std::string>& log, const char* verb) {
  assert(verb != nullptr);
  if (stations.empty()) {
    log.push_back(std::string(verb) + " — no points to place.");
    return;
  }
  PushUndoSnapshot(st, verb);
  int placed = 0;
  for (double d : stations) {
    spline::Vec3 p{};
    if (!PointAtLength(path, d, &p))
      continue;
    CommitCadPointToActiveSpace(st, CadPoint{p.x, p.y, p.z});
    ++placed;
  }
  BumpCadGpuCache(st);
  log.push_back(std::string(verb) + " — placed " + std::to_string(placed) + " point(s).");
}

[[nodiscard]] double ShoelaceArea(const std::vector<spline::Vec3>& pts) {
  if (pts.size() < 3)
    return 0.0;
  double a = 0.0;
  const size_t n = pts.size();
  for (size_t i = 0; i < n; ++i) {
    const spline::Vec3& p = pts[i];
    const spline::Vec3& q = pts[(i + 1) % n];
    a += p.x * q.y - q.x * p.y;
  }
  return std::fabs(a) * 0.5;
}

[[nodiscard]] double Perimeter(const std::vector<spline::Vec3>& pts, bool closed) {
  if (pts.size() < 2)
    return 0.0;
  double t = 0.0;
  for (size_t i = 1; i < pts.size(); ++i)
    t += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
  if (closed && pts.size() >= 3)
    t += std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y);
  return t;
}

void ReportArea(AppCommandState& st, double area, double peri, std::vector<std::string>& log) {
  const auto* as = st.sessionAs<stepengine::AreaSession>();
  const int prec = LinearPrecision(st);
  char buf[192];
  std::snprintf(buf, sizeof(buf), "AREA — area %s  perimeter %s", FormatLinear(area, prec).c_str(),
                FormatLinear(peri, prec).c_str());
  log.push_back(buf);
  if (as && as->accumulate) {
    std::snprintf(buf, sizeof(buf), "AREA total %s", FormatLinear(as->running, prec).c_str());
    log.push_back(buf);
  }
}

template <typename VT>
void ListPolylineCsr(const std::string& layer, const char* space, const std::vector<VT>& verts,
                     const std::vector<int>& offsets, const std::vector<uint8_t>& closedFlags, int index, int prec,
                     std::vector<std::string>& log) {
  assert(space != nullptr);
  if (index < 0 || static_cast<size_t>(index) + 1 >= offsets.size())
    return;
  const int v0 = offsets[static_cast<size_t>(index)];
  const int v1 = offsets[static_cast<size_t>(index) + 1];
  const int nVert = v1 - v0;
  if (nVert < 1 || static_cast<size_t>(v1) * 3 > verts.size())
    return;
  const bool closed =
      static_cast<size_t>(index) < closedFlags.size() && closedFlags[static_cast<size_t>(index)] != 0;
  const size_t a = static_cast<size_t>(v0) * 3;
  const size_t b = static_cast<size_t>(v1 - 1) * 3;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "POLYLINE  layer %s  %s  %d vertices  %s  (%s,%s) to (%s,%s)", layer.c_str(), space,
                nVert, closed ? "closed" : "open", FormatLinear(static_cast<double>(verts[a]), prec).c_str(),
                FormatLinear(static_cast<double>(verts[a + 1]), prec).c_str(),
                FormatLinear(static_cast<double>(verts[b]), prec).c_str(),
                FormatLinear(static_cast<double>(verts[b + 1]), prec).c_str());
  log.push_back(buf);
}

void ListSplineEntity(const std::string& layer, const char* space, const CadSpline& s, std::vector<std::string>& log) {
  assert(space != nullptr);
  assert(space[0] != '\0');
  char buf[192];
  std::snprintf(buf, sizeof(buf), "SPLINE  layer %s  %s  fit %zu  ctrl %zu", layer.c_str(), space, s.fit.size(),
                s.ctrl.size());
  log.push_back(buf);
}

void ListLeaderEntity(const std::string& layer, const char* space, const CadMultileader& ml, int prec,
                      std::vector<std::string>& log) {
  assert(space != nullptr);
  float lx = 0.f, ly = 0.f, lz = 0.f;
  if (ml.pathXyz.size() >= 3)
    CadMultileaderLandingLocal(ml, &lx, &ly, &lz);
  char buf[256];
  std::snprintf(buf, sizeof(buf), "LEADER  layer %s  %s  %zu path points  landing (%s,%s)", layer.c_str(), space,
                ml.pathXyz.size() / 3, FormatLinear(static_cast<double>(lx), prec).c_str(),
                FormatLinear(static_cast<double>(ly), prec).c_str());
  log.push_back(buf);
}

void ListModelEntity(const AppCommandState& st, const SelectedEntity& e, std::vector<std::string>& log) {
  const int prec = LinearPrecision(st);
  const EntityAttributes* at = CadEntityAttrsForSelected(st, e);
  const std::string layer = (at && !at->layer.empty()) ? at->layer : "0";
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t li = static_cast<size_t>(e.index);
    if (li >= st.drawing.lineCount())
      return;
    const CadLineRecord ln = st.drawing.line(li);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "LINE  layer %s  model  (%s,%s) to (%s,%s)", layer.c_str(),
                  FormatLinear(ln.x0, prec).c_str(),
                  FormatLinear(ln.y0, prec).c_str(),
                  FormatLinear(ln.x1, prec).c_str(),
                  FormatLinear(ln.y1, prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Circle) {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= st.drawing.circleCount())
      return;
    const CadCircleRecord c = st.drawing.circle(ci);
    char buf[192];
    std::snprintf(buf, sizeof(buf), "CIRCLE  layer %s  model  center (%s,%s)  radius %s", layer.c_str(),
                  FormatLinear(c.cx, prec).c_str(),
                  FormatLinear(c.cy, prec).c_str(),
                  FormatLinear(c.r, prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Polyline) {
    ListPolylineCsr(layer, "model", st.drawing.userPolylineVerts, st.drawing.userPolylineOffsets, st.drawing.userPolylineClosed, e.index, prec,
                    log);
    return;
  }
  if (e.type == T::Alignment) {  // REQ-480
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.alignments.size())
      return;
    const alignment::Alignment& al = st.drawing.alignments[static_cast<size_t>(e.index)];
    char buf[256];
    std::snprintf(buf, sizeof(buf), "ALIGNMENT  Name: %s  Layer: %s  Entities: %zu  Length: %s", al.name.c_str(),
                  layer.c_str(), al.Entities().size(), FormatLinear(al.Length(), prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Spline) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadSplines.size())
      return;
    ListSplineEntity(layer, "model", st.drawing.cadSplines[static_cast<size_t>(e.index)], log);
    return;
  }
  if (e.type == T::Point) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadPoints.size())
      return;
    const CadPoint& p = st.drawing.cadPoints[static_cast<size_t>(e.index)];
    char buf[192];
    std::snprintf(buf, sizeof(buf), "POINT  layer %s  model  (%s,%s,%s)", layer.c_str(),
                  FormatLinear(p.x, prec).c_str(), FormatLinear(p.y, prec).c_str(),
                  FormatLinear(p.z, prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Multileader) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadMultileaders.size())
      return;
    ListLeaderEntity(layer, "model", st.drawing.cadMultileaders[static_cast<size_t>(e.index)], prec, log);
    return;
  }
  log.push_back("LIST — " + layer + "  model  type " + std::to_string(static_cast<int>(e.type)));
}

void ListPaperEntity(const AppCommandState& st, const PaperLayout& L, const PaperEntityRef& r,
                     std::vector<std::string>& log) {
  const int prec = LinearPrecision(st);
  auto layerOf = [](const std::vector<EntityAttributes>& attrs, int i) -> std::string {
    if (i >= 0 && static_cast<size_t>(i) < attrs.size() && !attrs[static_cast<size_t>(i)].layer.empty())
      return attrs[static_cast<size_t>(i)].layer;
    return "0";
  };
  char buf[256];
  switch (r.type) {
  case PaperEntityRef::Type::Line: {
    const size_t k = static_cast<size_t>(r.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return;
    std::snprintf(buf, sizeof(buf), "LINE  layer %s  paper  (%s,%s) to (%s,%s)",
                  layerOf(L.paperLineAttrs, r.index).c_str(), FormatLinear(L.paperLines[k], prec).c_str(),
                  FormatLinear(L.paperLines[k + 1], prec).c_str(), FormatLinear(L.paperLines[k + 3], prec).c_str(),
                  FormatLinear(L.paperLines[k + 4], prec).c_str());
    log.push_back(buf);
    break;
  }
  case PaperEntityRef::Type::Circle: {
    const size_t k = static_cast<size_t>(r.index) * 3;
    if (k + 2 >= L.paperCircles.size())
      return;
    std::snprintf(buf, sizeof(buf), "CIRCLE  layer %s  paper  center (%s,%s)  radius %s",
                  layerOf(L.paperCircleAttrs, r.index).c_str(), FormatLinear(L.paperCircles[k], prec).c_str(),
                  FormatLinear(L.paperCircles[k + 1], prec).c_str(), FormatLinear(L.paperCircles[k + 2], prec).c_str());
    log.push_back(buf);
    break;
  }
  case PaperEntityRef::Type::Polyline:
    ListPolylineCsr(layerOf(L.paperPolyAttrs, r.index), "paper", L.paperPolyVerts, L.paperPolyOffsets, L.paperPolyClosed,
                    r.index, prec, log);
    break;
  case PaperEntityRef::Type::Spline:
    if (r.index >= 0 && static_cast<size_t>(r.index) < L.paperSplines.size())
      ListSplineEntity(layerOf(L.paperSplineAttrs, r.index), "paper", L.paperSplines[static_cast<size_t>(r.index)], log);
    break;
  case PaperEntityRef::Type::Point: {
    if (r.index < 0 || static_cast<size_t>(r.index) >= L.paperPoints.size())
      return;
    const CadPoint& p = L.paperPoints[static_cast<size_t>(r.index)];
    std::snprintf(buf, sizeof(buf), "POINT  layer %s  paper  (%s,%s,%s)", layerOf(L.paperPointAttrs, r.index).c_str(),
                  FormatLinear(p.x, prec).c_str(), FormatLinear(p.y, prec).c_str(), FormatLinear(p.z, prec).c_str());
    log.push_back(buf);
    break;
  }
  case PaperEntityRef::Type::Leader:
    if (r.index >= 0 && static_cast<size_t>(r.index) < L.paperMultileaders.size())
      ListLeaderEntity(layerOf(L.paperMultileaderAttrs, r.index), "paper",
                       L.paperMultileaders[static_cast<size_t>(r.index)], prec, log);
    break;
  default:
    log.push_back("LIST — paper object");
    break;
  }
}

void FinishLeader(AppCommandState& st, std::vector<std::string>& log) {
  auto* ls = st.sessionAs<stepengine::LeaderSession>();
  if (!ls || ls->draftPoints.size() < 2) {
    log.push_back("LEADER — need at least two points.");
    return;
  }
  PushUndoSnapshot(st, "LEADER");
  CadMultileader ml;
  ml.annotative = st.drawing.activeMultileaderStyle.annotativeDefault;
  ml.splinePath = ls->pathStyle == stepengine::LeaderPathStyle::Spline;
  ml.showArrow = ls->showArrow;
  std::vector<spline::Vec3> pathPts = ls->draftPoints;
  if (ml.splinePath && ls->draftPoints.size() >= 3) {
    spline::Spline fit;
    std::string why;
    if (spline::MakeFit(ls->draftPoints, false, &fit, &why)) {
      std::vector<spline::Vec3> tess;
      spline::Tessellate(fit, kSplineSampleTolerance, &tess);
      if (tess.size() >= 2)
        pathPts = std::move(tess);
    }
  }
  for (const spline::Vec3& p : pathPts) {
    ml.pathXyz.push_back(static_cast<float>(p.x));
    ml.pathXyz.push_back(static_cast<float>(p.y));
    ml.pathXyz.push_back(static_cast<float>(p.z));
  }
  const spline::Vec3& land = ls->draftPoints.back();
  if (ls->annotation == stepengine::LeaderAnnotation::Mtext) {
    const float plotH = std::max(st.drawing.activeMultileaderStyle.textSizeInches, 0.01f);
    const float unit = ActivePaperGeometryTarget(st) != nullptr
                           ? 1.f
                           : std::max(st.drawing.modelUnitsPerPlottedInch, 1.e-6f);
    CadAnnotation& a = ml.label;
    a.kind = CadAnnotation::Kind::Mtext;
    a.plottedHeightInches = plotH;
    a.insX = static_cast<float>(land.x);
    a.insY = static_cast<float>(land.y);
    a.insZ = static_cast<float>(land.z);
    a.boxMinX = a.insX;
    a.boxMinY = a.insY;
    a.boxMaxX = a.insX + 22.f * plotH * unit;
    a.boxMaxY = a.insY + 2.6f * plotH * unit;
    a.text = "Leader";
    StampActiveTextStyleOnNewText(st, a);
  }
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    L->paperMultileaders.push_back(std::move(ml));
    L->paperMultileaderAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    st.drawing.cadMultileaders.push_back(std::move(ml));
    st.drawing.cadMultileaderAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  stepengine::EndSession(st);
  log.push_back("LEADER complete.");
}

template <typename VT>
int AppendOpenPolyline2(std::vector<VT>& verts, std::vector<int>& offsets, std::vector<uint8_t>& closedFlags,
                        std::vector<EntityAttributes>& attrs, std::vector<float>& bulge, VT x0, VT y0, VT z0, VT x1,
                        VT y1, VT z1, float bulge0, const EntityAttributes& at) {
  if (offsets.empty())
    offsets.push_back(0);
  const int base = offsets.back();
  verts.push_back(x0);
  verts.push_back(y0);
  verts.push_back(z0);
  verts.push_back(x1);
  verts.push_back(y1);
  verts.push_back(z1);
  offsets.push_back(base + 2);
  closedFlags.push_back(0);
  attrs.push_back(at);
  SyncPolylineBulge(bulge, verts.size());
  if (static_cast<size_t>(base) < bulge.size())
    bulge[static_cast<size_t>(base)] = bulge0;
  return static_cast<int>(offsets.size()) - 2;
}

int ConvertPickedToPolyline(AppCommandState& st, const SelectedEntity& hit, std::vector<std::string>& log) {
  using T = SelectedEntity::Type;
  if (hit.type == T::Polyline)
    return hit.index;
  if (hit.type == T::LineSeg) {
    const size_t li = static_cast<size_t>(hit.index);
    if (li >= st.drawing.lineCount())
      return -1;
    PushUndoSnapshot(st, "PEDIT convert");
    const CadLineRecord ln = st.drawing.line(li);
    const int idx = AppendOpenPolyline2(st.drawing.userPolylineVerts, st.drawing.userPolylineOffsets, st.drawing.userPolylineClosed,
                                        st.drawing.userPolylineAttrs, st.drawing.userPolylineVertsBulge, ln.x0,
                                        ln.y0, ln.z0, ln.x1,
                                        ln.y1, ln.z1, 0.f, MakeNewEntityAttrs(st));
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
    st.drawing.eraseLine(li);
    if (static_cast<size_t>(hit.index) < st.drawing.userLineAttrs.size())
      st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + hit.index);
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted line to polyline.");
    return idx;
  }
  if (hit.type == T::Arc) {
    if (static_cast<size_t>(hit.index) >= st.drawing.userArcs.size())
      return -1;
    const CadArc a = st.drawing.userArcs[static_cast<size_t>(hit.index)];
    PushUndoSnapshot(st, "PEDIT convert");
    const double x0 = a.cx + a.r * std::cos(a.startRad);
    const double y0 = a.cy + a.r * std::sin(a.startRad);
    const double x1 = a.cx + a.r * std::cos(a.startRad + a.sweepRad);
    const double y1 = a.cy + a.r * std::sin(a.startRad + a.sweepRad);
    const float bg = static_cast<float>(std::tan(static_cast<double>(a.sweepRad) / 4.0));
    const int idx = AppendOpenPolyline2(st.drawing.userPolylineVerts, st.drawing.userPolylineOffsets, st.drawing.userPolylineClosed,
                                        st.drawing.userPolylineAttrs, st.drawing.userPolylineVertsBulge, x0, y0, a.z, x1, y1, a.z, bg,
                                        MakeNewEntityAttrs(st));
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
    st.drawing.userArcs.erase(st.drawing.userArcs.begin() + hit.index);
    if (static_cast<size_t>(hit.index) < st.drawing.userArcAttrs.size())
      st.drawing.userArcAttrs.erase(st.drawing.userArcAttrs.begin() + hit.index);
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted arc to polyline.");
    return idx;
  }
  return -1;
}

int ConvertPaperPickedToPolyline(AppCommandState& st, PaperLayout& L, const PaperEntityRef& hit,
                                 std::vector<std::string>& log) {
  if (hit.type == PaperEntityRef::Type::Polyline)
    return hit.index;
  if (hit.type == PaperEntityRef::Type::Line) {
    const size_t k = static_cast<size_t>(hit.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return -1;
    PushUndoSnapshot(st, "PEDIT convert");
    const int idx = AppendOpenPolyline2(L.paperPolyVerts, L.paperPolyOffsets, L.paperPolyClosed, L.paperPolyAttrs,
                                        L.paperPolyVertsBulge, L.paperLines[k], L.paperLines[k + 1], L.paperLines[k + 2],
                                        L.paperLines[k + 3], L.paperLines[k + 4], L.paperLines[k + 5], 0.f,
                                        MakeNewEntityAttrs(st));
    L.paperLines.erase(L.paperLines.begin() + static_cast<std::ptrdiff_t>(k),
                       L.paperLines.begin() + static_cast<std::ptrdiff_t>(k) + 6);
    if (static_cast<size_t>(hit.index) < L.paperLineAttrs.size())
      L.paperLineAttrs.erase(L.paperLineAttrs.begin() + hit.index);
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted line to polyline.");
    return idx;
  }
  if (hit.type == PaperEntityRef::Type::Arc) {
    if (static_cast<size_t>(hit.index) >= L.paperArcs.size())
      return -1;
    const CadArc a = L.paperArcs[static_cast<size_t>(hit.index)];
    PushUndoSnapshot(st, "PEDIT convert");
    const float x0 = a.cx + a.r * std::cos(a.startRad);
    const float y0 = a.cy + a.r * std::sin(a.startRad);
    const float x1 = a.cx + a.r * std::cos(a.startRad + a.sweepRad);
    const float y1 = a.cy + a.r * std::sin(a.startRad + a.sweepRad);
    const float bg = static_cast<float>(std::tan(static_cast<double>(a.sweepRad) / 4.0));
    const int idx = AppendOpenPolyline2(L.paperPolyVerts, L.paperPolyOffsets, L.paperPolyClosed, L.paperPolyAttrs,
                                        L.paperPolyVertsBulge, x0, y0, static_cast<float>(a.z), x1, y1,
                                        static_cast<float>(a.z), bg, MakeNewEntityAttrs(st));
    L.paperArcs.erase(L.paperArcs.begin() + hit.index);
    if (static_cast<size_t>(hit.index) < L.paperArcAttrs.size())
      L.paperArcAttrs.erase(L.paperArcAttrs.begin() + hit.index);
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted arc to polyline.");
    return idx;
  }
  return -1;
}

bool PeditHasPoly(const AppCommandState& st) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps)
    return false;
  if (ps->paper) {
    const PaperLayout* L = ActivePaperGeometryTarget(st);
    return L && ps->index >= 0 && static_cast<size_t>(ps->index) + 1 < L->paperPolyOffsets.size();
  }
  return ps->index >= 0 && static_cast<size_t>(ps->index) + 1 < st.drawing.userPolylineOffsets.size();
}

void PeditCloseOpen(AppCommandState& st, bool close, std::vector<std::string>& log) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, close ? "PEDIT Close" : "PEDIT Open");
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    L->paperPolyClosed[static_cast<size_t>(ps->index)] = close ? 1u : 0u;
  } else {
    st.drawing.userPolylineClosed[static_cast<size_t>(ps->index)] = close ? 1u : 0u;
  }
  BumpCadGpuCache(st);
  log.push_back(close ? "PEDIT — closed." : "PEDIT — opened.");
}

void ReverseBulgeRange(std::vector<float>& bulge, int v0, int v1) {
  if (bulge.empty() || v1 <= v0)
    return;
  if (static_cast<size_t>(v1) > bulge.size())
    return;
  std::reverse(bulge.begin() + v0, bulge.begin() + v1);
  for (int i = v0; i < v1; ++i)
    bulge[static_cast<size_t>(i)] = -bulge[static_cast<size_t>(i)];
}

void PeditReverse(AppCommandState& st, std::vector<std::string>& log) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT Reverse");
  auto reverseRange = [](auto& verts, int v0, int v1) {
    const int n = v1 - v0;
    for (int i = 0; i < n / 2; ++i) {
      const size_t a = static_cast<size_t>(v0 + i) * 3;
      const size_t b = static_cast<size_t>(v1 - 1 - i) * 3;
      std::swap(verts[a], verts[b]);
      std::swap(verts[a + 1], verts[b + 1]);
      std::swap(verts[a + 2], verts[b + 2]);
    }
  };
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(ps->index)];
    const int v1 = L->paperPolyOffsets[static_cast<size_t>(ps->index) + 1];
    reverseRange(L->paperPolyVerts, v0, v1);
    ReverseBulgeRange(L->paperPolyVertsBulge, v0, v1);
  } else {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index)];
    const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index) + 1];
    reverseRange(st.drawing.userPolylineVerts, v0, v1);
    ReverseBulgeRange(st.drawing.userPolylineVertsBulge, v0, v1);
  }
  BumpCadGpuCache(st);
  log.push_back("PEDIT — reversed.");
}

void PeditSetBulges(AppCommandState& st, float value, const char* undo, const char* done, std::vector<std::string>& log) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, undo);
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    SyncPolylineBulge(L->paperPolyVertsBulge, L->paperPolyVerts.size());
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(ps->index)];
    const int v1 = L->paperPolyOffsets[static_cast<size_t>(ps->index) + 1];
    const bool closed = static_cast<size_t>(ps->index) < L->paperPolyClosed.size() &&
                        L->paperPolyClosed[static_cast<size_t>(ps->index)] != 0;
    const int last = closed ? v1 : v1 - 1;
    for (int i = v0; i < last; ++i)
      L->paperPolyVertsBulge[static_cast<size_t>(i)] = value;
  } else {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index)];
    const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index) + 1];
    SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
    const bool closed = static_cast<size_t>(ps->index) < st.drawing.userPolylineClosed.size() &&
                        st.drawing.userPolylineClosed[static_cast<size_t>(ps->index)] != 0;
    const int last = closed ? v1 : v1 - 1;
    for (int i = v0; i < last; ++i)
      st.drawing.userPolylineVertsBulge[static_cast<size_t>(i)] = value;
  }
  BumpCadGpuCache(st);
  log.push_back(done);
}

void PeditDecurve(AppCommandState& st, std::vector<std::string>& log) {
  PeditSetBulges(st, 0.f, "PEDIT Decurve", "PEDIT — decurved.", log);
}

void PeditFit(AppCommandState& st, std::vector<std::string>& log) {
  PeditSetBulges(st, 0.5f, "PEDIT Fit", "PEDIT — fit (bulge arcs).", log);
}

void PeditSplineDensify(AppCommandState& st, std::vector<std::string>& log) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT Spline");
  auto densify = [](auto& verts, auto& offsets, auto& bulge, int pi) {
    const int v0 = offsets[static_cast<size_t>(pi)];
    const int v1 = offsets[static_cast<size_t>(pi) + 1];
    std::vector<typename std::decay<decltype(verts[0])>::type> neu;
    for (int vi = v0; vi < v1; ++vi) {
      const size_t a = static_cast<size_t>(vi) * 3;
      neu.push_back(verts[a]);
      neu.push_back(verts[a + 1]);
      neu.push_back(verts[a + 2]);
      if (vi + 1 < v1) {
        const size_t b = static_cast<size_t>(vi + 1) * 3;
        neu.push_back((verts[a] + verts[b]) * 0.5f);
        neu.push_back((verts[a + 1] + verts[b + 1]) * 0.5f);
        neu.push_back((verts[a + 2] + verts[b + 2]) * 0.5f);
      }
    }
    const int oldN = v1 - v0;
    const int newN = static_cast<int>(neu.size() / 3);
    verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                verts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
    verts.insert(verts.begin() + static_cast<std::ptrdiff_t>(v0) * 3, neu.begin(), neu.end());
    const int delta = newN - oldN;
    for (size_t i = static_cast<size_t>(pi) + 1; i < offsets.size(); ++i)
      offsets[i] += delta;
    SyncPolylineBulge(bulge, verts.size());
  };
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    densify(L->paperPolyVerts, L->paperPolyOffsets, L->paperPolyVertsBulge, ps->index);
  } else {
    densify(st.drawing.userPolylineVerts, st.drawing.userPolylineOffsets, st.drawing.userPolylineVertsBulge, ps->index);
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  log.push_back("PEDIT — spline densified.");
}

int PeditVertexCount(const AppCommandState& st) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return 0;
  if (ps->paper) {
    const PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return 0;
    return L->paperPolyOffsets[static_cast<size_t>(ps->index) + 1] -
           L->paperPolyOffsets[static_cast<size_t>(ps->index)];
  }
  return st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index) + 1] -
         st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index)];
}

void PeditMoveVertex(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT vertex");
  const int n = PeditVertexCount(st);
  const int vi = std::clamp(ps->vertex, 0, std::max(0, n - 1));
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(ps->index)];
    const size_t b = static_cast<size_t>(v0 + vi) * 3;
    if (b + 1 < L->paperPolyVerts.size()) {
      L->paperPolyVerts[b] = static_cast<float>(x);
      L->paperPolyVerts[b + 1] = static_cast<float>(y);
    }
  } else {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index)];
    const size_t b = static_cast<size_t>(v0 + vi);
    if (b < st.drawing.vertexCount()) {
      st.drawing.vertexComponent(b, CadVertexComponent::X) = x;
      st.drawing.vertexComponent(b, CadVertexComponent::Y) = y;
    }
  }
  BumpCadGpuCache(st);
  ps->phase = stepengine::PeditPhase::WaitVertex;
  log.push_back("PEDIT — vertex moved. Next / Previous / Insert / Move / eXit:");
}

void PeditInsertVertex(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT insert");
  const int n = PeditVertexCount(st);
  const int vi = std::clamp(ps->vertex, 0, std::max(0, n - 1));
  auto insertAt = [&](auto& verts, auto& offsets, auto& bulge, double z) {
    const int v0 = offsets[static_cast<size_t>(ps->index)];
    const size_t at = static_cast<size_t>(v0 + vi + 1) * 3;
    verts.insert(verts.begin() + static_cast<std::ptrdiff_t>(at),
                 {static_cast<typename std::decay<decltype(verts[0])>::type>(x),
                  static_cast<typename std::decay<decltype(verts[0])>::type>(y),
                  static_cast<typename std::decay<decltype(verts[0])>::type>(z)});
    for (size_t i = static_cast<size_t>(ps->index) + 1; i < offsets.size(); ++i)
      offsets[i] += 1;
    SyncPolylineBulge(bulge, verts.size());
  };
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    insertAt(L->paperPolyVerts, L->paperPolyOffsets, L->paperPolyVertsBulge, 0.0);
  } else {
    insertAt(st.drawing.userPolylineVerts, st.drawing.userPolylineOffsets, st.drawing.userPolylineVertsBulge, CadCommitElevation(st));
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  ps->vertex = vi + 1;
  ps->phase = stepengine::PeditPhase::WaitVertex;
  log.push_back("PEDIT — vertex inserted. Next / Previous / Insert / Move / eXit:");
}

void EraseModelPolyline(AppCommandState& st, int pi) {
  if (pi < 0 || static_cast<size_t>(pi) + 1 >= st.drawing.userPolylineOffsets.size())
    return;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi) + 1];
  st.drawing.userPolylineVerts.erase(st.drawing.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                             st.drawing.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  if (st.drawing.userPolylineVertsBulge.size() >= static_cast<size_t>(v1))
    st.drawing.userPolylineVertsBulge.erase(st.drawing.userPolylineVertsBulge.begin() + v0, st.drawing.userPolylineVertsBulge.begin() + v1);
  if (st.drawing.userPolylineVertsNormal.size() >= static_cast<size_t>(v1) * 3)
    st.drawing.userPolylineVertsNormal.erase(st.drawing.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                                     st.drawing.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  const int nv = v1 - v0;
  st.drawing.userPolylineOffsets.erase(st.drawing.userPolylineOffsets.begin() + (pi + 1));
  for (size_t k = static_cast<size_t>(pi) + 1; k < st.drawing.userPolylineOffsets.size(); ++k)
    st.drawing.userPolylineOffsets[k] -= nv;
  if (st.drawing.userPolylineOffsets.size() == 1)
    st.drawing.userPolylineOffsets.clear();
  if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
    st.drawing.userPolylineClosed.erase(st.drawing.userPolylineClosed.begin() + pi);
  if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
    st.drawing.userPolylineAttrs.erase(st.drawing.userPolylineAttrs.begin() + pi);
}

void ErasePaperPoly(PaperLayout& L, int pi) {
  if (pi < 0 || static_cast<size_t>(pi) + 1 >= L.paperPolyOffsets.size())
    return;
  const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi) + 1];
  const int nv = v1 - v0;
  L.paperPolyVerts.erase(L.paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                         L.paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  if (L.paperPolyVertsBulge.size() >= static_cast<size_t>(v1))
    L.paperPolyVertsBulge.erase(L.paperPolyVertsBulge.begin() + v0, L.paperPolyVertsBulge.begin() + v1);
  L.paperPolyOffsets.erase(L.paperPolyOffsets.begin() + (pi + 1));
  for (size_t k = static_cast<size_t>(pi) + 1; k < L.paperPolyOffsets.size(); ++k)
    L.paperPolyOffsets[k] -= nv;
  if (L.paperPolyOffsets.size() == 1)
    L.paperPolyOffsets.clear();
  if (static_cast<size_t>(pi) < L.paperPolyClosed.size())
    L.paperPolyClosed.erase(L.paperPolyClosed.begin() + pi);
  if (static_cast<size_t>(pi) < L.paperPolyAttrs.size())
    L.paperPolyAttrs.erase(L.paperPolyAttrs.begin() + pi);
}

bool JoinVertexLists(std::vector<spline::Vec3>* keep, std::vector<float>* keepBulge, const std::vector<spline::Vec3>& other,
                     const std::vector<float>& otherBulge) {
  assert(keep != nullptr && keepBulge != nullptr);
  if (keep->size() < 2 || other.size() < 2)
    return false;
  const auto d2 = [](const spline::Vec3& a, const spline::Vec3& b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
  };
  const double t2 = kJoinTol * kJoinTol;
  const double dLfOf = d2(keep->back(), other.front());
  const double dLfOl = d2(keep->back(), other.back());
  const double dFfOl = d2(keep->front(), other.back());
  const double dFfOf = d2(keep->front(), other.front());
  double best = dLfOf;
  int mode = 0;
  if (dLfOl < best) {
    best = dLfOl;
    mode = 1;
  }
  if (dFfOl < best) {
    best = dFfOl;
    mode = 2;
  }
  if (dFfOf < best) {
    best = dFfOf;
    mode = 3;
  }
  if (best > t2)
    return false;
  std::vector<spline::Vec3> o = other;
  std::vector<float> ob = otherBulge;
  ob.resize(o.size(), 0.f);
  if (mode == 1 || mode == 2) {
    std::reverse(o.begin(), o.end());
    std::reverse(ob.begin(), ob.end());
    for (float& b : ob)
      b = -b;
  }
  if (mode == 2 || mode == 3) {
    std::reverse(keep->begin(), keep->end());
    std::reverse(keepBulge->begin(), keepBulge->end());
    for (float& b : *keepBulge)
      b = -b;
  }
  keepBulge->resize(keep->size(), 0.f);
  for (size_t i = 1; i < o.size(); ++i) {
    keep->push_back(o[i]);
    keepBulge->push_back(i < ob.size() ? ob[i] : 0.f);
  }
  return true;
}

template <typename VT>
void ReplacePolyRange(std::vector<VT>& verts, std::vector<float>& bulge, std::vector<int>& offsets, int keep,
                      const std::vector<spline::Vec3>& keepPts, const std::vector<float>& keepBg) {
  assert(keep >= 0 && static_cast<size_t>(keep) + 1 < offsets.size());
  const int v0 = offsets[static_cast<size_t>(keep)];
  const int v1 = offsets[static_cast<size_t>(keep) + 1];
  verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
              verts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  if (bulge.size() >= static_cast<size_t>(v1))
    bulge.erase(bulge.begin() + v0, bulge.begin() + v1);
  std::vector<VT> flat;
  for (const spline::Vec3& p : keepPts) {
    flat.push_back(static_cast<VT>(p.x));
    flat.push_back(static_cast<VT>(p.y));
    flat.push_back(static_cast<VT>(p.z));
  }
  verts.insert(verts.begin() + static_cast<std::ptrdiff_t>(v0) * 3, flat.begin(), flat.end());
  bulge.insert(bulge.begin() + v0, keepBg.begin(), keepBg.end());
  const int delta = static_cast<int>(keepPts.size()) - (v1 - v0);
  for (size_t i = static_cast<size_t>(keep) + 1; i < offsets.size(); ++i)
    offsets[i] += delta;
}

void PeditJoinIndex(AppCommandState& st, int other, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps || !PeditHasPoly(st) || other == ps->index) {
    log.push_back("PEDIT — pick a different touching polyline.");
    return;
  }
  std::vector<spline::Vec3> keepPts;
  std::vector<float> keepBg;
  std::vector<spline::Vec3> otherPts;
  std::vector<float> otherBg;
  auto load = [](const auto& verts, const auto& bulge, int v0, int v1, std::vector<spline::Vec3>* pts,
                 std::vector<float>* bg) {
    pts->clear();
    bg->clear();
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      pts->push_back({static_cast<double>(verts[b]), static_cast<double>(verts[b + 1]), static_cast<double>(verts[b + 2])});
      bg->push_back(static_cast<size_t>(vi) < bulge.size() ? static_cast<float>(bulge[static_cast<size_t>(vi)]) : 0.f);
    }
  };
  if (ps->paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L || other < 0 || static_cast<size_t>(other) + 1 >= L->paperPolyOffsets.size())
      return;
    if (L->paperPolyClosed[static_cast<size_t>(ps->index)] || L->paperPolyClosed[static_cast<size_t>(other)]) {
      log.push_back("PEDIT — cannot join a closed polyline.");
      return;
    }
    load(L->paperPolyVerts, L->paperPolyVertsBulge, L->paperPolyOffsets[static_cast<size_t>(ps->index)],
         L->paperPolyOffsets[static_cast<size_t>(ps->index) + 1], &keepPts, &keepBg);
    load(L->paperPolyVerts, L->paperPolyVertsBulge, L->paperPolyOffsets[static_cast<size_t>(other)],
         L->paperPolyOffsets[static_cast<size_t>(other) + 1], &otherPts, &otherBg);
    if (!JoinVertexLists(&keepPts, &keepBg, otherPts, otherBg)) {
      log.push_back("PEDIT — objects do not touch.");
      return;
    }
    PushUndoSnapshot(st, "PEDIT Join");
    const int keep = ps->index;
    ReplacePolyRange(L->paperPolyVerts, L->paperPolyVertsBulge, L->paperPolyOffsets, keep, keepPts, keepBg);
    int keepAfter = keep;
    ErasePaperPoly(*L, other);
    if (other < keepAfter)
      --keepAfter;
    ps->index = keepAfter;
  } else {
    if (other < 0 || static_cast<size_t>(other) + 1 >= st.drawing.userPolylineOffsets.size())
      return;
    if (st.drawing.userPolylineClosed[static_cast<size_t>(ps->index)] || st.drawing.userPolylineClosed[static_cast<size_t>(other)]) {
      log.push_back("PEDIT — cannot join a closed polyline.");
      return;
    }
    load(st.drawing.userPolylineVerts, st.drawing.userPolylineVertsBulge, st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index)],
         st.drawing.userPolylineOffsets[static_cast<size_t>(ps->index) + 1], &keepPts, &keepBg);
    load(st.drawing.userPolylineVerts, st.drawing.userPolylineVertsBulge, st.drawing.userPolylineOffsets[static_cast<size_t>(other)],
         st.drawing.userPolylineOffsets[static_cast<size_t>(other) + 1], &otherPts, &otherBg);
    if (!JoinVertexLists(&keepPts, &keepBg, otherPts, otherBg)) {
      log.push_back("PEDIT — objects do not touch.");
      return;
    }
    PushUndoSnapshot(st, "PEDIT Join");
    const int keep = ps->index;
    ReplacePolyRange(st.drawing.userPolylineVerts, st.drawing.userPolylineVertsBulge, st.drawing.userPolylineOffsets, keep, keepPts, keepBg);
    int keepAfter = keep;
    EraseModelPolyline(st, other);
    if (other < keepAfter)
      --keepAfter;
    ps->index = keepAfter;
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  ps->phase = stepengine::PeditPhase::WaitOption;
  log.push_back("PEDIT — joined. Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
}

bool HandlePeditOption(AppCommandState& st, const std::string& opt, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps)
    return false;
  if (opt == "C" || opt == "CLOSE") {
    PeditCloseOpen(st, true, log);
    return true;
  }
  if (opt == "O" || opt == "OPEN") {
    PeditCloseOpen(st, false, log);
    return true;
  }
  if (opt == "R" || opt == "REVERSE") {
    PeditReverse(st, log);
    return true;
  }
  if (opt == "D" || opt == "DECURVE") {
    PeditDecurve(st, log);
    return true;
  }
  if (opt == "F" || opt == "FIT") {
    PeditFit(st, log);
    return true;
  }
  if (opt == "S" || opt == "SPLINE") {
    PeditSplineDensify(st, log);
    return true;
  }
  if (opt == "E" || opt == "EDIT") {
    ps->phase = stepengine::PeditPhase::WaitVertex;
    ps->vertex = 0;
    log.push_back("PEDIT vertex — Next / Previous / Insert / Move / eXit:");
    return true;
  }
  if (opt == "W" || opt == "WIDTH" || opt == "L" || opt == "LTYPE" || opt == "LTYPEGEN") {
    log.push_back("PEDIT — Width and linetype generation are not supported.");
    return true;
  }
  if (opt == "J" || opt == "JOIN") {
    log.push_back("PEDIT — Join: pick a touching polyline, or Enter to skip.");
    ps->phase = stepengine::PeditPhase::WaitJoin;
    return true;
  }
  if (opt.empty() || opt == "X" || opt == "EXIT") {
    stepengine::EndSession(st);
    log.push_back("PEDIT complete.");
    return true;
  }
  log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
  return true;
}

bool HandlePeditVertex(AppCommandState& st, const std::string& opt, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps)
    return false;
  const int n = PeditVertexCount(st);
  if (opt == "N" || opt == "NEXT") {
    if (n > 0)
      ps->vertex = (ps->vertex + 1) % n;
    log.push_back("PEDIT — vertex " + std::to_string(ps->vertex + 1) + " of " + std::to_string(n) + ".");
    return true;
  }
  if (opt == "P" || opt == "PREVIOUS") {
    if (n > 0)
      ps->vertex = (ps->vertex + n - 1) % n;
    log.push_back("PEDIT — vertex " + std::to_string(ps->vertex + 1) + " of " + std::to_string(n) + ".");
    return true;
  }
  if (opt == "I" || opt == "INSERT") {
    log.push_back("PEDIT — specify insert point:");
    ps->phase = stepengine::PeditPhase::WaitVertexInsert;
    return true;
  }
  if (opt == "M" || opt == "MOVE") {
    log.push_back("PEDIT — specify new vertex location:");
    ps->phase = stepengine::PeditPhase::WaitVertexMove;
    return true;
  }
  if (opt.empty() || opt == "X" || opt == "EXIT") {
    ps->phase = stepengine::PeditPhase::WaitOption;
    log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
    return true;
  }
  log.push_back("PEDIT vertex — Next / Previous / Insert / Move / eXit:");
  return true;
}

bool AreaFromPicked(const AppCommandState& st, const SelectedEntity& e, double* area, double* peri) {
  assert(area != nullptr && peri != nullptr);
  using T = SelectedEntity::Type;
  if (e.type == T::Circle) {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= st.drawing.circleCount())
      return false;
    const double r = st.drawing.circle(ci).r;
    if (!(r > 0.0))
      return false;
    *area = kPi * r * r;
    *peri = kTwoPi * r;
    return true;
  }
  if (e.type == T::Polyline) {
    PathSample path;
    if (!ModelPathFromEntity(st, e, 0.0, 0.0, &path) || !path.closed || path.pts.size() < 3)
      return false;
    *area = ShoelaceArea(path.pts);
    *peri = Perimeter(path.pts, true);
    return true;
  }
  return false;
}

bool AreaFromPaper(const PaperLayout& L, const PaperEntityRef& r, double* area, double* peri) {
  assert(area != nullptr && peri != nullptr);
  if (r.type == PaperEntityRef::Type::Circle) {
    const size_t k = static_cast<size_t>(r.index) * 3;
    if (k + 2 >= L.paperCircles.size())
      return false;
    const double rad = L.paperCircles[k + 2];
    if (!(rad > 0.0))
      return false;
    *area = kPi * rad * rad;
    *peri = kTwoPi * rad;
    return true;
  }
  if (r.type == PaperEntityRef::Type::Polyline) {
    PathSample path;
    if (!PaperPathFromRef(L, r, 0.0, 0.0, &path) || !path.closed || path.pts.size() < 3)
      return false;
    *area = ShoelaceArea(path.pts);
    *peri = Perimeter(path.pts, true);
    return true;
  }
  return false;
}

void ApplyAreaValue(AppCommandState& st, double area, double peri, std::vector<std::string>& log) {
  auto* as = st.sessionAs<stepengine::AreaSession>();
  if (!as)
    return;
  const double signedA = as->subtract ? -area : area;
  as->running += signedA;
  ReportArea(st, signedA, peri, log);
  as->draftPoints.clear();
  if (as->accumulate) {
    as->phase = stepengine::AreaPhase::WaitFirst;
    log.push_back(as->subtract ? "AREA Subtract mode — specify points or Object:"
                               : "AREA Add mode — specify points or Object:");
    return;
  }
  stepengine::EndSession(st);
}

void SampleMeasurePath(const AppCommandState& st, PathSample* path, bool* ok) {
  assert(path != nullptr && ok != nullptr);
  *ok = false;
  const auto* ms = st.sessionAs<stepengine::MeasureDivideSession>();
  if (!ms)
    return;
  if (ms->paper) {
    if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
      const PaperEntityRef r = ms->paperTarget;
      *ok = PaperPathFromRef(*L, r, ms->pickX, ms->pickY, path);
    }
    return;
  }
  *ok = ModelPathFromEntity(st, ms->target, ms->pickX, ms->pickY, path);
}

bool HandleLeaderText(const std::string& line, const std::string& opt, AppCommandState& st,
                      std::vector<std::string>& log) {
  auto* ls = st.sessionAs<stepengine::LeaderSession>();
  if (!ls)
    return false;
  if (ls->phase == stepengine::LeaderPhase::WaitFormat) {
    if (opt == "S" || opt == "STRAIGHT") {
      ls->pathStyle = stepengine::LeaderPathStyle::Straight;
      ls->phase = stepengine::LeaderPhase::WaitNext;
      log.push_back("LEADER — straight. Specify next point:");
      return true;
    }
    if (opt == "SPLINE") {
      ls->pathStyle = stepengine::LeaderPathStyle::Spline;
      ls->phase = stepengine::LeaderPhase::WaitNext;
      log.push_back("LEADER — spline format. Specify next point:");
      return true;
    }
    if (opt == "A" || opt == "ARROW") {
      ls->showArrow = true;
      ls->phase = stepengine::LeaderPhase::WaitNext;
      log.push_back("LEADER — arrow. Specify next point:");
      return true;
    }
    if (opt == "N" || opt == "NONE") {
      ls->showArrow = false;
      ls->phase = stepengine::LeaderPhase::WaitNext;
      log.push_back("LEADER — no arrow. Specify next point:");
      return true;
    }
    if (line.empty()) {
      ls->phase = stepengine::LeaderPhase::WaitNext;
      log.push_back("LEADER — specify next point (Enter = annotation):");
      return true;
    }
    log.push_back("LEADER format — Straight / Spline / Arrow / None:");
    return true;
  }
  if (ls->phase == stepengine::LeaderPhase::WaitAnnotation) {
    if (opt == "N" || opt == "NONE")
      ls->annotation = stepengine::LeaderAnnotation::None;
    else if (opt == "M" || opt == "MTEXT" || line.empty()) {
      if (!line.empty())
        ls->annotation = stepengine::LeaderAnnotation::Mtext;
    } else if (opt == "T" || opt == "TOLERANCE" || opt == "C" || opt == "COPY" || opt == "B" || opt == "BLOCK") {
      log.push_back("LEADER — Tolerance, Copy, and Block annotation are not supported.");
      return true;
    } else {
      log.push_back("LEADER annotation — Mtext / None (Tolerance/Copy/Block are not supported):");
      return true;
    }
    if (ls->draftPoints.size() >= 2 || ls->finishAfterAnnotation) {
      FinishLeader(st, log);
      return true;
    }
    ls->phase = stepengine::LeaderPhase::WaitFirst;
    log.push_back("LEADER — specify first leader point (Annotation / Format):");
    return true;
  }
  if (opt == "A" || opt == "ANNOTATION") {
    ls->phase = stepengine::LeaderPhase::WaitAnnotation;
    ls->finishAfterAnnotation = ls->draftPoints.size() >= 2;
    log.push_back("LEADER annotation — Mtext / None (Tolerance/Copy/Block are not supported):");
    return true;
  }
  if (opt == "M" || opt == "MTEXT") {
    ls->annotation = stepengine::LeaderAnnotation::Mtext;
    log.push_back("LEADER — Mtext annotation. Specify next point:");
    return true;
  }
  if (opt == "N" || opt == "NONE") {
    ls->annotation = stepengine::LeaderAnnotation::None;
    log.push_back("LEADER — no annotation. Specify next point:");
    return true;
  }
  if (opt == "T" || opt == "TOLERANCE" || opt == "C" || opt == "COPY" || opt == "B" || opt == "BLOCK") {
    log.push_back("LEADER — Tolerance, Copy, and Block annotation are not supported.");
    return true;
  }
  if (opt == "F" || opt == "FORMAT") {
    ls->phase = stepengine::LeaderPhase::WaitFormat;
    log.push_back("LEADER format — Straight / Spline / Arrow / None:");
    return true;
  }
  if (opt == "S" || opt == "STRAIGHT") {
    ls->pathStyle = stepengine::LeaderPathStyle::Straight;
    log.push_back("LEADER — straight. Specify next point:");
    return true;
  }
  if (opt == "SPLINE") {
    ls->pathStyle = stepengine::LeaderPathStyle::Spline;
    log.push_back("LEADER — spline format. Specify next point:");
    return true;
  }
  if (line.empty()) {
    if (ls->draftPoints.size() >= 2) {
      if (ls->annotation == stepengine::LeaderAnnotation::None) {
        FinishLeader(st, log);
        return true;
      }
      ls->phase = stepengine::LeaderPhase::WaitAnnotation;
      ls->finishAfterAnnotation = true;
      log.push_back("LEADER annotation — Mtext / None (Enter = Mtext):");
      return true;
    }
    FinishLeader(st, log);
    return true;
  }
  float x = 0.f, y = 0.f;
  if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
    log.push_back("LEADER — enter a point, Annotation, Format, or Enter to finish.");
    return true;
  }
  ls->draftPoints.push_back({x, y, CadCommitElevation(st)});
  ls->phase = stepengine::LeaderPhase::WaitNext;
  log.push_back("LEADER — specify next point (Enter = annotation):");
  return true;
}

bool HandleAreaText(const std::string& line, const std::string& opt, AppCommandState& st,
                    std::vector<std::string>& log) {
  auto* as = st.sessionAs<stepengine::AreaSession>();
  if (!as)
    return false;
  if (opt == "O" || opt == "OBJECT") {
    as->phase = stepengine::AreaPhase::WaitObject;
    log.push_back("AREA — select a closed polyline or circle:");
    return true;
  }
  if (opt == "A" || opt == "ADD") {
    as->subtract = false;
    as->accumulate = true;
    log.push_back("AREA Add mode — specify points or Object:");
    return true;
  }
  if (opt == "S" || opt == "SUBTRACT") {
    as->subtract = true;
    as->accumulate = true;
    log.push_back("AREA Subtract mode — specify points or Object:");
    return true;
  }
  if (line.empty()) {
    if (as->draftPoints.size() < 3) {
      if (as->accumulate) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "AREA total %s", FormatLinear(as->running, LinearPrecision(st)).c_str());
        log.push_back(buf);
        stepengine::EndSession(st);
        return true;
      }
      stepengine::EndSession(st);
      log.push_back("AREA — canceled (need at least three points).");
      return true;
    }
    const double area = ShoelaceArea(as->draftPoints);
    const double peri = Perimeter(as->draftPoints, true);
    ApplyAreaValue(st, area, peri, log);
    return true;
  }
  float x = 0.f, y = 0.f;
  if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
    log.push_back("AREA — enter a point, Object, Add, or Subtract.");
    return true;
  }
  as->draftPoints.push_back({x, y, CadCommitElevation(st)});
  as->phase = stepengine::AreaPhase::WaitNext;
  log.push_back("AREA — specify next point (Enter to close):");
  return true;
}

bool HandleMeasureDivideText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  auto* ms = st.sessionAs<stepengine::MeasureDivideSession>();
  if (!ms)
    return false;
  if (ms->phase != stepengine::MeasurePhase::WaitValue) {
    log.push_back(st.active == K::Measure ? "MEASURE — pick an object first." : "DIVIDE — pick an object first.");
    return true;
  }
  PathSample path;
  bool ok = false;
  SampleMeasurePath(st, &path, &ok);
  if (!ok) {
    log.push_back("Cannot sample that object.");
    stepengine::EndSession(st);
    return true;
  }
  const double len = PathLength(path);
  if (st.active == K::Measure) {
    double interval = 0.0;
    if (std::sscanf(line.c_str(), "%lf", &interval) != 1 || !(interval > 1e-12)) {
      log.push_back("MEASURE — enter a positive length.");
      return true;
    }
    std::vector<double> stations;
    for (int i = 1; i < 100000; ++i) {
      const double d = interval * static_cast<double>(i);
      if (d >= len - 1e-9)
        break;
      stations.push_back(d);
    }
    PlacePointsAlong(st, path, stations, log, "MEASURE");
  } else {
    int n = 0;
    if (std::sscanf(line.c_str(), "%d", &n) != 1 || n < 2) {
      log.push_back("DIVIDE — enter an integer of 2 or more.");
      return true;
    }
    std::vector<double> stations;
    if (path.closed) {
      for (int i = 0; i < n; ++i)
        stations.push_back(len * static_cast<double>(i) / static_cast<double>(n));
    } else {
      for (int i = 1; i < n; ++i)
        stations.push_back(len * static_cast<double>(i) / static_cast<double>(n));
    }
    PlacePointsAlong(st, path, stations, log, "DIVIDE");
  }
  stepengine::EndSession(st);
  return true;
}

bool TryPickPolylineForPedit(AppCommandState& st, double wx, double wy, int* idx, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  assert(idx != nullptr);
  assert(std::isfinite(wx) && std::isfinite(wy));
  *idx = -1;
  if (!ps)
    return false;
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.drawing.blockDefs)) {
      log.push_back("PEDIT — nothing at that point.");
      return false;
    }
    *idx = ConvertPaperPickedToPolyline(st, *L, hit, log);
    if (*idx < 0) {
      log.push_back("PEDIT — select a polyline, line, or arc.");
      return false;
    }
    ps->paper = true;
    return true;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("PEDIT — nothing at that point.");
    return false;
  }
  *idx = ConvertPickedToPolyline(st, hit, log);
  if (*idx < 0) {
    log.push_back("PEDIT — select a polyline, line, or arc.");
    return false;
  }
  ps->paper = false;
  return true;
}

bool HandlePeditJoinPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  int idx = -1;
  if (!TryPickPolylineForPedit(st, wx, wy, &idx, log))
    return true;
  PeditJoinIndex(st, idx, log);
  return true;
}

bool HandlePeditObjectPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps)
    return false;
  int idx = -1;
  if (!TryPickPolylineForPedit(st, wx, wy, &idx, log))
    return true;
  ps->index = idx;
  ps->phase = stepengine::PeditPhase::WaitOption;
  log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
  return true;
}

bool HandleMeasureObjectPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  auto* ms = st.sessionAs<stepengine::MeasureDivideSession>();
  if (!ms)
    return false;
  ms->pickX = wx;
  ms->pickY = wy;
  if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.drawing.blockDefs)) {
      log.push_back(st.active == K::Measure ? "MEASURE — nothing at that point." : "DIVIDE — nothing at that point.");
      return true;
    }
    ms->paper = true;
    ms->paperTarget = hit;
    ms->phase = stepengine::MeasurePhase::WaitValue;
    log.push_back(st.active == K::Measure ? "MEASURE — specify length of segment:"
                                          : "DIVIDE — enter the number of segments:");
    return true;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back(st.active == K::Measure ? "MEASURE — nothing at that point." : "DIVIDE — nothing at that point.");
    return true;
  }
  ms->target = hit;
  ms->paper = false;
  ms->phase = stepengine::MeasurePhase::WaitValue;
  log.push_back(st.active == K::Measure ? "MEASURE — specify length of segment:"
                                        : "DIVIDE — enter the number of segments:");
  return true;
}

bool HandleListPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.drawing.blockDefs))
      ListPaperEntity(st, *L, hit, log);
    else
      log.push_back("LIST — nothing at that point.");
    return true;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2))
    ListModelEntity(st, hit, log);
  else
    log.push_back("LIST — nothing at that point.");
  return true;
}

bool HandleAreaObjectPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.drawing.blockDefs)) {
      log.push_back("AREA — nothing at that point.");
      return true;
    }
    double area = 0.0, peri = 0.0;
    if (!AreaFromPaper(*L, hit, &area, &peri)) {
      log.push_back("AREA — object must be a closed polyline or circle.");
      return true;
    }
    ApplyAreaValue(st, area, peri, log);
    return true;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("AREA — nothing at that point.");
    return true;
  }
  double area = 0.0, peri = 0.0;
  if (!AreaFromPicked(st, hit, &area, &peri)) {
    log.push_back("AREA — object must be a closed polyline or circle.");
    return true;
  }
  ApplyAreaValue(st, area, peri, log);
  return true;
}

}  // namespace

void CommitCadPointAt(AppCommandState& st, double x, double y, double z, std::vector<std::string>& log) {
  assert(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));
  PushUndoSnapshot(st, "POINT");
  CommitCadPointToActiveSpace(st, CadPoint{x, y, z});
  BumpCadGpuCache(st);
  log.push_back("POINT — specify a point (Enter to end):");
}

void StartPointCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginDrawInquirySession(st, std::make_unique<stepengine::PointSession>(), AppCommandState::Kind::Point);
  log.push_back("POINT — specify a point (Enter to end):");
}

void StartLeaderCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginDrawInquirySession(st, std::make_unique<stepengine::LeaderSession>(), AppCommandState::Kind::Leader);
  log.push_back("LEADER — specify first leader point (Annotation / Format):");
}

void StartPeditCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginDrawInquirySession(st, std::make_unique<stepengine::PeditSession>(), AppCommandState::Kind::Pedit);
  log.push_back("PEDIT — select polyline (or line/arc to convert):");
}

void StartMeasureCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginDrawInquirySession(st, std::make_unique<stepengine::MeasureDivideSession>(false),
                          AppCommandState::Kind::Measure);
  log.push_back("MEASURE — select object to measure:");
}

void StartDivideCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginDrawInquirySession(st, std::make_unique<stepengine::MeasureDivideSession>(true),
                          AppCommandState::Kind::Divide);
  log.push_back("DIVIDE — select object to divide:");
}

void StartAreaCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginDrawInquirySession(st, std::make_unique<stepengine::AreaSession>(), AppCommandState::Kind::Area);
  log.push_back("AREA — specify first corner point (Object / Add / Subtract):");
}

void StartListCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.selection.empty() && st.activeSpaceIndex == kModelSpaceIndex) {
    for (const SelectedEntity& e : st.selection)
      ListModelEntity(st, e, log);
    return;
  }
  if (!st.selectedPaperEntities.empty()) {
    if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
      for (const PaperEntityRef& r : st.selectedPaperEntities)
        ListPaperEntity(st, *L, r, log);
      return;
    }
  }
  BeginDrawInquirySession(st, std::make_unique<stepengine::ListSession>(), AppCommandState::Kind::List);
  log.push_back("LIST — select objects, then Enter:");
}

void ExecutePdModeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string t = StringUtil::trimCopy(args);
  if (t.empty()) {
    log.push_back("PDMODE = " + std::to_string(st.drawing.pdMode) + " (0/1/2/3/4 + 32 circle / 64 square).");
    return;
  }
  int v = 0;
  if (std::sscanf(t.c_str(), "%d", &v) != 1) {
    log.push_back("PDMODE — enter an integer (0/1/2/3/4, plus 32/64/96).");
    return;
  }
  PushUndoSnapshot(st, "PDMODE");
  st.drawing.pdMode = v;
  BumpCadGpuCache(st);
  log.push_back("PDMODE = " + std::to_string(st.drawing.pdMode) + ".");
}

void ExecutePdSizeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string t = StringUtil::trimCopy(args);
  if (t.empty()) {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "PDSIZE = %.6g (0 = 5%% of view).", st.drawing.pdSize);
    log.push_back(buf);
    return;
  }
  double v = 0.0;
  if (std::sscanf(t.c_str(), "%lf", &v) != 1 || !std::isfinite(v)) {
    log.push_back("PDSIZE — enter a number.");
    return;
  }
  PushUndoSnapshot(st, "PDSIZE");
  st.drawing.pdSize = v;
  BumpCadGpuCache(st);
  char buf[80];
  std::snprintf(buf, sizeof(buf), "PDSIZE = %.6g.", st.drawing.pdSize);
  log.push_back(buf);
}

// TABLE and ADDPOINTTABLE (REQ-466, REQ-474; D-2026-10-30-b). Typed prompts drive both commands. The Qt editor
// opens the point table dialog for ADDPOINTTABLE and then calls BeginAddPointTableWithPoints.
namespace {

bool ParseTableCount(const std::string& line, int lo, int hi, int dflt, int* out) {
  if (line.empty()) {
    *out = dflt;
    return true;
  }
  char* end = nullptr;
  const long v = std::strtol(line.c_str(), &end, 10);
  if (end == line.c_str() || *end != '\0' || v < lo || v > hi)
    return false;
  *out = static_cast<int>(v);
  return true;
}

void EndTableCommand(AppCommandState& st) {
  stepengine::EndSession(st);
}

// Places a sized table with its upper-left corner at (\p wx, \p wy) and logs the result.
}  // namespace

void StartTableCommand(AppCommandState& st, std::vector<std::string>& log) {
  auto* ts = BeginDrawInquirySession(st, std::make_unique<stepengine::TableSession>(), AppCommandState::Kind::Table);
  st.tablePendingStyle.clear();
  st.tablePendingCols = 3;
  st.tablePendingRows = 2;
  if (const CadTableStyle* standard = CadTableFindStyle(st.drawing.tableStyles, "Standard")) {
    st.tablePendingColumnWidthInches = standard->columnWidthInches;
    st.tablePendingRowHeightInches = standard->rowHeightInches;
  }
  if (st.addPointTableDialogAvailable) {
    st.tablePendingStyle = "Standard";
    st.tablePendingRows = 3;  // one title row, one header row and one initial data row
    ts->stage = 3;
    st.tableInsertDialogRequested = true;
    return;
  }
  log.push_back("TABLE — table style (Standard) or Enter:");
}

bool HandleTableText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  auto* ts = st.sessionAs<stepengine::TableSession>();
  if (!ts)
    return false;
  if (ts->stage == 0) {
    const std::string name = line.empty() ? std::string("Standard") : line;
    const CadTableStyle* s = CadTableFindStyle(st.drawing.tableStyles, name);
    if (!s || s->point) {
      log.push_back("TABLE — no data table style named \"" + name + "\". Type Standard or press Enter.");
      return true;
    }
    st.tablePendingStyle = s->name;
    st.tablePendingColumnWidthInches = s->columnWidthInches;
    st.tablePendingRowHeightInches = s->rowHeightInches;
    ts->stage = 1;
    log.push_back("TABLE — number of columns <3>:");
    return true;
  }
  if (ts->stage == 1) {
    if (!ParseTableCount(line, 1, 50, 3, &st.tablePendingCols)) {
      log.push_back("TABLE — enter a whole number of columns from 1 to 50.");
      return true;
    }
    ts->stage = 2;
    log.push_back("TABLE — number of rows <2>:");
    return true;
  }
  if (ts->stage == 2) {
    if (!ParseTableCount(line, 1, 500, 2, &st.tablePendingRows)) {
      log.push_back("TABLE — enter a whole number of rows from 1 to 500.");
      return true;
    }
    ts->stage = 3;
    log.push_back("TABLE — specify the insertion point (upper-left corner):");
    return true;
  }
  log.push_back("TABLE — pick the insertion point.");
  return true;
}

void CommitTableAt(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const CadTableStyle* s = CadTableFindStyle(st.drawing.tableStyles, st.tablePendingStyle);
  if (!s) {
    EndTableCommand(st);
    return;
  }
  CadTable t;
  CadTableStyle sizing = *s;
  sizing.columnWidthInches = st.tablePendingColumnWidthInches;
  sizing.rowHeightInches = st.tablePendingRowHeightInches;
  CadTableApplyStyleSize(&t, sizing, st.tablePendingRows, st.tablePendingCols, st.drawing.modelUnitsPerPlottedInch);
  t.insX = static_cast<float>(wx);
  t.insY = static_cast<float>(wy);
  t.insZ = static_cast<float>(CadCommitElevation(st));
  if (const TextStyle* ts = ActiveTextStyle(st))
    t.fontFamily = ts->fontFamily;
  PushUndoSnapshot(st, "TABLE");
  st.drawing.cadTables.push_back(std::move(t));
  AppendNewObjectAttrs(st, st.drawing.cadTableAttrs, st.drawing.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
  log.push_back("TABLE inserted: " + std::to_string(st.tablePendingCols) + " x " +
                std::to_string(st.tablePendingRows) + " in style " + s->name + ".");
  EndTableCommand(st);
}

void StartAddPointTableCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.addPointTableDialogAvailable) {
    st.addPointTableDialogRequested = true;  // the Qt editor opens the dialog and calls BeginAddPointTableWithPoints
    return;
  }
  BeginDrawInquirySession(st, std::make_unique<stepengine::PointTableSession>(), AppCommandState::Kind::PointTable);
  log.push_back("ADDPOINTTABLE — point table style (NameNEZD) or Enter:");
}

bool BeginAddPointTableWithPoints(AppCommandState& st, const std::string& styleName, std::vector<int> ids,
                                  std::vector<std::string>& log) {
  const CadTableStyle* s = CadTableFindStyle(st.drawing.tableStyles, styleName);
  if (!s || !s->point) {
    log.push_back("ADDPOINTTABLE — no point table style named \"" + styleName + "\".");
    return false;
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  std::vector<int> present;
  for (int id : ids) {
    for (const SurveyPoint& p : st.drawing.surveyPoints) {
      if (p.id == id) {
        present.push_back(id);
        break;
      }
    }
  }
  if (present.empty()) {
    log.push_back("ADDPOINTTABLE — no survey points to list.");
    return false;
  }
  auto* pt = BeginDrawInquirySession(st, std::make_unique<stepengine::PointTableSession>(),
                                     AppCommandState::Kind::PointTable);
  pt->stage = 2;
  pt->style = s->name;
  pt->ids = std::move(present);
  log.push_back("ADDPOINTTABLE — " + std::to_string(pt->ids.size()) +
                " point(s). Specify the insertion point (upper-left corner):");
  return true;
}

bool HandlePointTableText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  auto* pt = st.sessionAs<stepengine::PointTableSession>();
  if (!pt)
    return false;
  if (pt->stage == 0) {
    const std::string name = line.empty() ? std::string("NameNEZD") : line;
    const CadTableStyle* s = CadTableFindStyle(st.drawing.tableStyles, name);
    if (!s || !s->point) {
      log.push_back("ADDPOINTTABLE — no point table style named \"" + name + "\". Type NameNEZD or press Enter.");
      return true;
    }
    pt->style = s->name;
    pt->stage = 1;
    log.push_back("ADDPOINTTABLE — points: numbers such as 1-5,8, a point group name, or Enter for all survey points:");
    return true;
  }
  if (pt->stage == 1) {
    std::vector<int> ids;
    if (line.empty()) {
      for (const SurveyPoint& p : st.drawing.surveyPoints)
        ids.push_back(p.id);
    } else if (const int g = FindPointGroupIndex(st, line); g >= 0) {
      ids = ResolvePointGroup(st, st.drawing.pointGroups[static_cast<size_t>(g)], &log);
    } else {
      std::vector<std::string> bad;
      const std::vector<PointIdRange> ranges = ParseIdRanges(line, &bad);
      if (!bad.empty()) {
        log.push_back("ADDPOINTTABLE — cannot read \"" + bad.front() + "\". Type numbers such as 1-5,8.");
        return true;
      }
      for (const SurveyPoint& p : st.drawing.surveyPoints) {
        if (IdInRanges(ranges, p.id))
          ids.push_back(p.id);
      }
    }
    const std::string style = pt->style;
    BeginAddPointTableWithPoints(st, style, std::move(ids), log);
    return true;
  }
  log.push_back("ADDPOINTTABLE — pick the insertion point.");
  return true;
}

void CommitPointTableAt(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const auto* pt = st.sessionAs<stepengine::PointTableSession>();
  const CadTableStyle* s = pt ? CadTableFindStyle(st.drawing.tableStyles, pt->style) : nullptr;
  if (!s) {
    EndTableCommand(st);
    return;
  }
  std::vector<CadTablePointRow> rows;
  for (int id : pt->ids) {
    for (const SurveyPoint& p : st.drawing.surveyPoints) {
      if (p.id != id)
        continue;
      CadTablePointRow r;
      r.id = p.id;
      r.northing = p.northing;
      r.easting = p.easting;
      r.elevation = p.elevation;
      r.description = p.description;
      rows.push_back(std::move(r));
      break;
    }
  }
  if (rows.empty()) {
    log.push_back("ADDPOINTTABLE — no survey points to list.");
    EndTableCommand(st);
    return;
  }
  const std::size_t maxRows = st.pointTableSplitEnabled
      ? static_cast<std::size_t>(std::max(1, st.pointTableMaxRows)) : rows.size();
  const std::size_t chunkCount = (rows.size() + maxRows - 1) / maxRows;
  const int cols = static_cast<int>(s->pointColumns.size());
  const bool mergedTitle = !s->title.empty();
  std::vector<CadTable> tables;
  tables.reserve(chunkCount);
  float firstWidth = 0.f, firstHeight = 0.f;
  for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
    const std::size_t first = chunk * maxRows;
    const std::size_t count = std::min(maxRows, rows.size() - first);
    std::vector<CadTablePointRow> subset(rows.begin() + static_cast<std::ptrdiff_t>(first),
                                         rows.begin() + static_cast<std::ptrdiff_t>(first + count));
    CadTable table;
    const int tableRows = static_cast<int>(count) + 1 + (mergedTitle ? 1 : 0);
    CadTableApplyStyleSize(&table, *s, tableRows, cols, st.drawing.modelUnitsPerPlottedInch);
    table.cells = CadPointTableCells(*s, subset, st.displayLinearPrecision);
    table.surveyPointDataRows = true;
    if (mergedTitle) {
      table.mergeTitleRow = true;
      table.surveyPointDataRows = true;
      // A merged title row still occupies one cell per column, so the header and data cells keep their row-major slots.
      std::vector<std::string> titleRow(static_cast<std::size_t>(std::max(cols, 1)));
      titleRow[0] = s->title;
      table.cells.insert(table.cells.begin(), titleRow.begin(), titleRow.end());
      table.rowTextHeightsInches = {s->titleTextHeightInches, s->headerTextHeightInches};
      table.rowWeights = {s->titleTextHeightInches * 3.f, s->headerTextHeightInches * 3.f};
      for (std::size_t row = 0; row < count; ++row) {
        table.rowTextHeightsInches.push_back(s->textHeightInches);
        table.rowWeights.push_back(s->textHeightInches * 3.f);
      }
      if (!s->columnWidths.empty()) {
        table.columnWeights.clear();
        for (double width : s->columnWidths) table.columnWeights.push_back(static_cast<float>(width));
      } else {
        CadTableFitColumnWeightsFromContent(&table, st.drawing.modelUnitsPerPlottedInch);
      }
    } else {
      CadTableFitColumnWeightsFromContent(&table, st.drawing.modelUnitsPerPlottedInch);
    }
    const std::size_t stackLimit = static_cast<std::size_t>(std::max(1, st.pointTableMaxStack));
    const std::size_t stack = chunk / stackLimit;
    const std::size_t tile = chunk % stackLimit;
    if (st.pointTableTileAcross) {
      table.insX = static_cast<float>(wx + stack * (table.width + st.pointTableOffset));
      table.insY = static_cast<float>(wy + tile * (table.height + st.pointTableOffset));
    } else {
      table.insX = static_cast<float>(wx + tile * (table.width + st.pointTableOffset));
      table.insY = static_cast<float>(wy - stack * (table.height + st.pointTableOffset));
    }
    table.insZ = static_cast<float>(CadCommitElevation(st));
    if (const TextStyle* ts = ActiveTextStyle(st)) table.fontFamily = ts->fontFamily;
    if (chunk == 0) { firstWidth = table.width; firstHeight = table.height; }
    tables.push_back(std::move(table));
  }
  PushUndoSnapshot(st, "ADDPOINTTABLE");
  for (CadTable& table : tables) {
    st.drawing.cadTables.push_back(std::move(table));
    AppendNewObjectAttrs(st, st.drawing.cadTableAttrs, st.drawing.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
    if (!st.pointTableLayer.empty()) st.drawing.cadTableAttrs.back().layer = st.pointTableLayer;
  }
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
  log.push_back("ADDPOINTTABLE inserted: " + std::to_string(rows.size()) + " point(s) in style " + s->name +
                ". " + std::to_string(chunkCount) + " table(s) (" +
                (st.pointTableDynamic ? "dynamic" : "static") + " setting, " +
                std::to_string(firstWidth) + " x " + std::to_string(firstHeight) + " first table).");
  EndTableCommand(st);
}

void SubmitTableViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const auto* ts = st.sessionAs<stepengine::TableSession>();
  if (!ts || ts->stage != 3) {
    log.push_back("TABLE — answer the prompt first.");
    return;
  }
  CommitTableAt(st, wx, wy, log);
}

void SubmitPointTableViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const auto* pt = st.sessionAs<stepengine::PointTableSession>();
  if (!pt || pt->stage != 2) {
    log.push_back("ADDPOINTTABLE — answer the prompt first.");
    return;
  }
  CommitPointTableAt(st, wx, wy, log);
}

void SubmitLeaderViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  auto* ls = st.sessionAs<stepengine::LeaderSession>();
  if (!ls || ls->phase == stepengine::LeaderPhase::WaitAnnotation ||
      ls->phase == stepengine::LeaderPhase::WaitFormat)
    return;
  ls->draftPoints.push_back({wx, wy, CadCommitElevation(st)});
  if (ls->phase == stepengine::LeaderPhase::WaitFirst)
    ls->phase = stepengine::LeaderPhase::WaitNext;
  log.push_back("LEADER — specify next point (Enter = annotation):");
}

void SubmitAreaViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  auto* as = st.sessionAs<stepengine::AreaSession>();
  if (!as)
    return;
  if (as->phase == stepengine::AreaPhase::WaitObject) {
    (void)HandleAreaObjectPick(st, wx, wy, log);
    return;
  }
  as->draftPoints.push_back({wx, wy, CadCommitElevation(st)});
  as->phase = stepengine::AreaPhase::WaitNext;
  log.push_back("AREA — specify next point (Enter to close):");
}

void SubmitPeditViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps)
    return;
  if (ps->phase == stepengine::PeditPhase::WaitVertexMove) {
    PeditMoveVertex(st, wx, wy, log);
    return;
  }
  if (ps->phase == stepengine::PeditPhase::WaitVertexInsert) {
    PeditInsertVertex(st, wx, wy, log);
    return;
  }
  if (ps->phase == stepengine::PeditPhase::WaitJoin) {
    (void)HandlePeditJoinPick(st, wx, wy, log);
    return;
  }
  if (ps->phase == stepengine::PeditPhase::WaitObject)
    (void)HandlePeditObjectPick(st, wx, wy, log);
}

void SubmitMeasureDivideViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const auto* ms = st.sessionAs<stepengine::MeasureDivideSession>();
  if (ms && ms->phase == stepengine::MeasurePhase::WaitObject)
    (void)HandleMeasureObjectPick(st, wx, wy, log);
}

void SubmitListViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  (void)HandleListPick(st, wx, wy, log);
}

// DATAEXTRACTION (REQ-467, issue #804). Selection first, then a typed column list, then a file.
// The drawing-table target waits for REQ-466 and is not offered here.
namespace {

const char* DataExtractTypeName(const SelectedEntity& e, const AppCommandState& st) {
  using T = SelectedEntity::Type;
  switch (e.type) {
    case T::LineSeg:  return "LINE";
    case T::Circle:   return "CIRCLE";
    case T::Arc:      return "ARC";
    case T::Ellipse:  return "ELLIPSE";
    case T::Polyline: return "POLYLINE";
    case T::Point:    return "POINT";
    case T::Spline:   return "SPLINE";
    case T::Multileader: return "MLEADER";
    case T::FilledRegion: return "HATCH";
    case T::Annotation: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadAnnotations.size())
        return "ANNOTATION";
      switch (st.drawing.cadAnnotations[static_cast<size_t>(e.index)].kind) {
        case CadAnnotation::Kind::Text:      return "TEXT";
        case CadAnnotation::Kind::Mtext:     return "MTEXT";
        case CadAnnotation::Kind::DimAligned:
        case CadAnnotation::Kind::DimLinear:
        case CadAnnotation::Kind::DimAngular: return "DIMENSION";
        default:                             return "ANNOTATION";
      }
    }
    default: return "OBJECT";
  }
}

// One row for a selected object. Coordinates are the object's defining point (REQ-467 implementation notes).
ExtractRecord DataExtractRecord(const AppCommandState& st, const SelectedEntity& e) {
  using T = SelectedEntity::Type;
  ExtractRecord r;
  r.type = DataExtractTypeName(e, st);
  const EntityAttributes* at = CadEntityAttrsForSelected(st, e);
  r.layer = (at && !at->layer.empty()) ? at->layer : "0";
  if (e.type == T::LineSeg) {
    const size_t li = static_cast<size_t>(e.index);
    if (li < st.drawing.lineCount()) {
      const CadLineRecord ln = st.drawing.line(li);
      r.hasPoint = true;
      r.x = ln.x0;
      r.y = ln.y0;
      r.z = ln.z0;
    }
  } else if (e.type == T::Circle) {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci < st.drawing.circleCount()) {
      const CadCircleRecord c = st.drawing.circle(ci);
      r.hasPoint = true;
      r.x = c.cx;
      r.y = c.cy;
      r.z = c.cz;
    }
  } else if (e.type == T::Polyline) {
    if (e.index >= 0 && static_cast<size_t>(e.index) + 1 < st.drawing.userPolylineOffsets.size()) {
      const size_t v = static_cast<size_t>(st.drawing.userPolylineOffsets[static_cast<size_t>(e.index)]);
      if (v < st.drawing.vertexCount()) {
        r.hasPoint = true;
        r.x = st.drawing.vertex(v).x;
        r.y = st.drawing.vertex(v).y;
        r.z = st.drawing.vertex(v).z;
      }
    }
  } else if (e.type == T::Point) {
    if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.cadPoints.size()) {
      const CadPoint& p = st.drawing.cadPoints[static_cast<size_t>(e.index)];
      r.hasPoint = true;
      r.x = p.x;
      r.y = p.y;
      r.z = p.z;
    }
  } else if (e.type == T::Annotation) {
    if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.cadAnnotations.size()) {
      const CadAnnotation& a = st.drawing.cadAnnotations[static_cast<size_t>(e.index)];
      r.hasPoint = true;
      r.x = a.insX;
      r.y = a.insY;
      r.z = a.insZ;
      r.hasText = true;
      r.text = a.text;
    }
  }
  return r;
}

bool DataExtractWrite(const std::string& path, const SheetGrid::Grid& grid, std::string* err) {
  const std::string lower = [&] {
    std::string s = path;
    for (char& ch : s)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  }();
  if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".csv") == 0)
    return SpreadsheetIo::WriteCsv(path, grid, err);
  return SpreadsheetIo::WriteXlsx(path, grid, err);
}

}  // namespace

void StartDataExtractCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.selectedPaperEntities.empty()) {
    log.push_back("DATAEXTRACTION — paper-space selections are not supported yet (REQ-201); select model objects.");
    return;
  }
  if (st.selection.empty() || st.activeSpaceIndex != kModelSpaceIndex) {
    log.push_back("DATAEXTRACTION — select the objects to extract first, then run the command.");
    return;
  }
  BeginDrawInquirySession(st, std::make_unique<stepengine::DataExtractSession>(), AppCommandState::Kind::DataExtract);
  log.push_back("DATAEXTRACTION — " + std::to_string(st.selection.size()) +
                " object(s). Columns: TYPE, LAYER, TEXT, X, Y, Z separated by commas, or Enter for all:");
}

bool HandleDataExtractText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  std::vector<ExtractColumn> cols;
  std::string err;
  if (!ParseExtractColumns(line, &cols, &err)) {
    log.push_back("DATAEXTRACTION — " + err + ".");
    return true;
  }
  std::vector<ExtractRecord> records;
  records.reserve(st.selection.size());
  for (const SelectedEntity& e : st.selection)
    records.push_back(DataExtractRecord(st, e));
  const SheetGrid::Grid grid = BuildExtractGrid(cols, records);

  char path[1024]{};
  if (!BrowseSaveFileSpreadsheetUtf8(path, sizeof(path), "extract.xlsx") || path[0] == '\0') {
    stepengine::EndSession(st);
    log.push_back("DATAEXTRACTION canceled — no file chosen.");
    return true;
  }
  if (!DataExtractWrite(path, grid, &err)) {
    log.push_back("DATAEXTRACTION — " + err + ".");
    return true;
  }
  stepengine::EndSession(st);
  log.push_back("DATAEXTRACTION wrote " + std::to_string(records.size()) + " row(s) to " + path + ".");
  return true;
}

void HandlePointCommandText(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  const std::string line = StringUtil::trimCopy(lineIn);
  if (line.empty()) {
    stepengine::EndSession(st);
    log.push_back("POINT ended.");
    return;
  }
  float x = 0.f, y = 0.f;
  if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
    log.push_back("POINT — enter a point as X,Y, or Enter to end.");
    return;
  }
  CommitCadPointAt(st, x, y, CadCommitElevation(st), log);
}

void HandleLeaderCommandText(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  const std::string line = StringUtil::trimCopy(lineIn);
  (void)HandleLeaderText(line, UpperTrim(line), st, log);
}

void HandlePeditCommandText(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PeditSession>();
  if (!ps)
    return;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string opt = UpperTrim(line);
  if (ps->phase == stepengine::PeditPhase::WaitOption) {
    (void)HandlePeditOption(st, opt, log);
    return;
  }
  if (ps->phase == stepengine::PeditPhase::WaitVertex) {
    (void)HandlePeditVertex(st, opt, log);
    return;
  }
  if (ps->phase == stepengine::PeditPhase::WaitJoin) {
    if (line.empty()) {
      ps->phase = stepengine::PeditPhase::WaitOption;
      log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
      return;
    }
    log.push_back("PEDIT — pick a touching polyline, or Enter to skip.");
    return;
  }
  if (ps->phase == stepengine::PeditPhase::WaitVertexMove ||
      ps->phase == stepengine::PeditPhase::WaitVertexInsert) {
    float x = 0.f, y = 0.f;
    if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
      log.push_back("PEDIT — enter a point.");
      return;
    }
    if (ps->phase == stepengine::PeditPhase::WaitVertexInsert)
      PeditInsertVertex(st, x, y, log);
    else
      PeditMoveVertex(st, x, y, log);
    return;
  }
  if (line.empty()) {
    log.push_back("PEDIT — select a polyline.");
    return;
  }
  log.push_back("PEDIT — pick a polyline, line, or arc.");
}

void HandleMeasureDivideCommandText(const std::string& lineIn, AppCommandState& st,
                                    std::vector<std::string>& log) {
  (void)HandleMeasureDivideText(StringUtil::trimCopy(lineIn), st, log);
}

void HandleAreaCommandText(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  const std::string line = StringUtil::trimCopy(lineIn);
  (void)HandleAreaText(line, UpperTrim(line), st, log);
}

void HandleListCommandText(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  const std::string line = StringUtil::trimCopy(lineIn);
  if (line.empty()) {
    stepengine::EndSession(st);
    log.push_back("LIST complete.");
    return;
  }
  log.push_back("LIST — pick objects, then Enter.");
}

void HandleDataExtractCommandText(const std::string& lineIn, AppCommandState& st,
                                  std::vector<std::string>& log) {
  (void)HandleDataExtractText(StringUtil::trimCopy(lineIn), st, log);
}

void HandleTableCommandText(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleTableText(StringUtil::trimCopy(lineIn), st, log);
}

void HandlePointTableCommandText(const std::string& lineIn, AppCommandState& st,
                                 std::vector<std::string>& log) {
  (void)HandlePointTableText(StringUtil::trimCopy(lineIn), st, log);
}
