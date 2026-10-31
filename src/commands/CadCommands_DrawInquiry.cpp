// CadCommands_DrawInquiry.cpp — LEADER, POINT, PEDIT, MEASURE, DIVIDE, AREA, LIST (REQ-456).

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"

#include "util/NumFormat.hpp"
#include "util/StringUtil.hpp"
#include "util/DataExtract.hpp"
#include "util/cadtablestyle.hpp"
#include "SpreadsheetIo.hpp"
#include "platform/WinFileDialogs.hpp"
#include "util/geom2d.hpp"
#include "util/spline.hpp"

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

[[nodiscard]] bool IsCadDrawInquiryKind(AppCommandState::Kind k) {
  using K = AppCommandState::Kind;
  return k == K::Point || k == K::Leader || k == K::Pedit || k == K::Measure || k == K::Divide ||
         k == K::Area || k == K::List || k == K::Table || k == K::PointTable;
}

void BeginInteractive(AppCommandState& st, AppCommandState::Kind kind) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  // Ribbon commands can replace an in-progress MLEADER without passing through Escape. Clear its
  // cursor/label draft as part of the command transition or stale MLEADER dynamic input remains
  // painted while TABLE (and other draw/inquiry commands) owns the active prompt.
  ResetMleaderDraft(st);
  if (st.mtextRichEditorMultileaderIndex >= 0)
    CancelMtextRichEditor(st, nullptr);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = kind;
  st.lastCommand = kind;
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
  st.cadPoints.push_back(p);
  st.cadPointAttrs.push_back(MakeNewEntityAttrs(st));
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
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return false;
    FillLinePath({st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 2]},
                 {st.userLinesFlat[k + 3], st.userLinesFlat[k + 4], st.userLinesFlat[k + 5]}, pickX, pickY, out);
    return true;
  }
  if (e.type == T::Polyline) {
    if (e.index < 0 || static_cast<size_t>(e.index) + 1 >= st.userPolylineOffsets.size())
      return false;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(e.index)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(e.index) + 1];
    const bool closed = static_cast<size_t>(e.index) < st.userPolylineClosed.size() &&
                        st.userPolylineClosed[static_cast<size_t>(e.index)] != 0;
    FillPolyPath(st.userPolylineVerts, st.userPolylineVertsBulge, v0, v1, closed, out);
    ReverseOpenPathIfPickNearerEnd(out, pickX, pickY);
    return out->pts.size() >= 2;
  }
  if (e.type == T::Circle) {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      return false;
    const double cx = st.userCirclesCxCyZR[k];
    const double cy = st.userCirclesCxCyZR[k + 1];
    const double cz = st.userCirclesCxCyZR[k + 2];
    const double r = st.userCirclesCxCyZR[k + 3];
    FillCirclePath(cx, cy, cz, r, pickX, pickY, out);
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
  const int prec = LinearPrecision(st);
  char buf[192];
  std::snprintf(buf, sizeof(buf), "AREA — area %s  perimeter %s", FormatLinear(area, prec).c_str(),
                FormatLinear(peri, prec).c_str());
  log.push_back(buf);
  if (st.areaAccumulate) {
    std::snprintf(buf, sizeof(buf), "AREA total %s", FormatLinear(st.areaRunning, prec).c_str());
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
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "LINE  layer %s  model  (%s,%s) to (%s,%s)", layer.c_str(),
                  FormatLinear(st.userLinesFlat[k], prec).c_str(),
                  FormatLinear(st.userLinesFlat[k + 1], prec).c_str(),
                  FormatLinear(st.userLinesFlat[k + 3], prec).c_str(),
                  FormatLinear(st.userLinesFlat[k + 4], prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Circle) {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      return;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "CIRCLE  layer %s  model  center (%s,%s)  radius %s", layer.c_str(),
                  FormatLinear(st.userCirclesCxCyZR[k], prec).c_str(),
                  FormatLinear(st.userCirclesCxCyZR[k + 1], prec).c_str(),
                  FormatLinear(st.userCirclesCxCyZR[k + 3], prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Polyline) {
    ListPolylineCsr(layer, "model", st.userPolylineVerts, st.userPolylineOffsets, st.userPolylineClosed, e.index, prec,
                    log);
    return;
  }
  if (e.type == T::Spline) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadSplines.size())
      return;
    ListSplineEntity(layer, "model", st.cadSplines[static_cast<size_t>(e.index)], log);
    return;
  }
  if (e.type == T::Point) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadPoints.size())
      return;
    const CadPoint& p = st.cadPoints[static_cast<size_t>(e.index)];
    char buf[192];
    std::snprintf(buf, sizeof(buf), "POINT  layer %s  model  (%s,%s,%s)", layer.c_str(),
                  FormatLinear(p.x, prec).c_str(), FormatLinear(p.y, prec).c_str(),
                  FormatLinear(p.z, prec).c_str());
    log.push_back(buf);
    return;
  }
  if (e.type == T::Multileader) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadMultileaders.size())
      return;
    ListLeaderEntity(layer, "model", st.cadMultileaders[static_cast<size_t>(e.index)], prec, log);
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
  if (st.leaderDraftPoints.size() < 2) {
    log.push_back("LEADER — need at least two points.");
    return;
  }
  PushUndoSnapshot(st, "LEADER");
  CadMultileader ml;
  ml.annotative = st.activeMultileaderStyle.annotativeDefault;
  ml.splinePath = st.leaderPathStyle == AppCommandState::LeaderPathStyle::Spline;
  ml.showArrow = st.leaderShowArrow;
  std::vector<spline::Vec3> pathPts = st.leaderDraftPoints;
  if (ml.splinePath && st.leaderDraftPoints.size() >= 3) {
    spline::Spline fit;
    std::string why;
    if (spline::MakeFit(st.leaderDraftPoints, false, &fit, &why)) {
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
  const spline::Vec3& land = st.leaderDraftPoints.back();
  if (st.leaderAnnotation == AppCommandState::LeaderAnnotation::Mtext) {
    const float plotH = std::max(st.activeMultileaderStyle.textSizeInches, 0.01f);
    const float unit = ActivePaperGeometryTarget(st) != nullptr
                           ? 1.f
                           : std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
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
    st.cadMultileaders.push_back(std::move(ml));
    st.cadMultileaderAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  st.leaderDraftPoints.clear();
  st.active = AppCommandState::Kind::None;
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
    const size_t k = static_cast<size_t>(hit.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return -1;
    PushUndoSnapshot(st, "PEDIT convert");
    const int idx = AppendOpenPolyline2(st.userPolylineVerts, st.userPolylineOffsets, st.userPolylineClosed,
                                        st.userPolylineAttrs, st.userPolylineVertsBulge, st.userLinesFlat[k],
                                        st.userLinesFlat[k + 1], st.userLinesFlat[k + 2], st.userLinesFlat[k + 3],
                                        st.userLinesFlat[k + 4], st.userLinesFlat[k + 5], 0.f, MakeNewEntityAttrs(st));
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
    st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                           st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k) + 6);
    if (static_cast<size_t>(hit.index) < st.userLineAttrs.size())
      st.userLineAttrs.erase(st.userLineAttrs.begin() + hit.index);
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted line to polyline.");
    return idx;
  }
  if (hit.type == T::Arc) {
    if (static_cast<size_t>(hit.index) >= st.userArcs.size())
      return -1;
    const CadArc a = st.userArcs[static_cast<size_t>(hit.index)];
    PushUndoSnapshot(st, "PEDIT convert");
    const double x0 = a.cx + a.r * std::cos(a.startRad);
    const double y0 = a.cy + a.r * std::sin(a.startRad);
    const double x1 = a.cx + a.r * std::cos(a.startRad + a.sweepRad);
    const double y1 = a.cy + a.r * std::sin(a.startRad + a.sweepRad);
    const float bg = static_cast<float>(std::tan(static_cast<double>(a.sweepRad) / 4.0));
    const int idx = AppendOpenPolyline2(st.userPolylineVerts, st.userPolylineOffsets, st.userPolylineClosed,
                                        st.userPolylineAttrs, st.userPolylineVertsBulge, x0, y0, a.z, x1, y1, a.z, bg,
                                        MakeNewEntityAttrs(st));
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
    st.userArcs.erase(st.userArcs.begin() + hit.index);
    if (static_cast<size_t>(hit.index) < st.userArcAttrs.size())
      st.userArcAttrs.erase(st.userArcAttrs.begin() + hit.index);
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
  if (st.peditPaper) {
    const PaperLayout* L = ActivePaperGeometryTarget(st);
    return L && st.peditIndex >= 0 && static_cast<size_t>(st.peditIndex) + 1 < L->paperPolyOffsets.size();
  }
  return st.peditIndex >= 0 && static_cast<size_t>(st.peditIndex) + 1 < st.userPolylineOffsets.size();
}

void PeditCloseOpen(AppCommandState& st, bool close, std::vector<std::string>& log) {
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, close ? "PEDIT Close" : "PEDIT Open");
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    L->paperPolyClosed[static_cast<size_t>(st.peditIndex)] = close ? 1u : 0u;
  } else {
    st.userPolylineClosed[static_cast<size_t>(st.peditIndex)] = close ? 1u : 0u;
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
  if (!PeditHasPoly(st))
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
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(st.peditIndex)];
    const int v1 = L->paperPolyOffsets[static_cast<size_t>(st.peditIndex) + 1];
    reverseRange(L->paperPolyVerts, v0, v1);
    ReverseBulgeRange(L->paperPolyVertsBulge, v0, v1);
  } else {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1];
    reverseRange(st.userPolylineVerts, v0, v1);
    ReverseBulgeRange(st.userPolylineVertsBulge, v0, v1);
  }
  BumpCadGpuCache(st);
  log.push_back("PEDIT — reversed.");
}

void PeditSetBulges(AppCommandState& st, float value, const char* undo, const char* done, std::vector<std::string>& log) {
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, undo);
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    SyncPolylineBulge(L->paperPolyVertsBulge, L->paperPolyVerts.size());
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(st.peditIndex)];
    const int v1 = L->paperPolyOffsets[static_cast<size_t>(st.peditIndex) + 1];
    const bool closed = static_cast<size_t>(st.peditIndex) < L->paperPolyClosed.size() &&
                        L->paperPolyClosed[static_cast<size_t>(st.peditIndex)] != 0;
    const int last = closed ? v1 : v1 - 1;
    for (int i = v0; i < last; ++i)
      L->paperPolyVertsBulge[static_cast<size_t>(i)] = value;
  } else {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1];
    SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
    const bool closed = static_cast<size_t>(st.peditIndex) < st.userPolylineClosed.size() &&
                        st.userPolylineClosed[static_cast<size_t>(st.peditIndex)] != 0;
    const int last = closed ? v1 : v1 - 1;
    for (int i = v0; i < last; ++i)
      st.userPolylineVertsBulge[static_cast<size_t>(i)] = value;
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
  if (!PeditHasPoly(st))
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
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    densify(L->paperPolyVerts, L->paperPolyOffsets, L->paperPolyVertsBulge, st.peditIndex);
  } else {
    densify(st.userPolylineVerts, st.userPolylineOffsets, st.userPolylineVertsBulge, st.peditIndex);
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  log.push_back("PEDIT — spline densified.");
}

int PeditVertexCount(const AppCommandState& st) {
  if (st.peditPaper) {
    const PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L || !PeditHasPoly(st))
      return 0;
    return L->paperPolyOffsets[static_cast<size_t>(st.peditIndex) + 1] -
           L->paperPolyOffsets[static_cast<size_t>(st.peditIndex)];
  }
  if (!PeditHasPoly(st))
    return 0;
  return st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1] -
         st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
}

void PeditMoveVertex(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT vertex");
  const int n = PeditVertexCount(st);
  const int vi = std::clamp(st.peditVertex, 0, std::max(0, n - 1));
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(st.peditIndex)];
    const size_t b = static_cast<size_t>(v0 + vi) * 3;
    if (b + 1 < L->paperPolyVerts.size()) {
      L->paperPolyVerts[b] = static_cast<float>(x);
      L->paperPolyVerts[b + 1] = static_cast<float>(y);
    }
  } else {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
    const size_t b = static_cast<size_t>(v0 + vi) * 3;
    if (b + 1 < st.userPolylineVerts.size()) {
      st.userPolylineVerts[b] = x;
      st.userPolylineVerts[b + 1] = y;
    }
  }
  BumpCadGpuCache(st);
  st.peditPhase = AppCommandState::PeditPhase::WaitVertex;
  log.push_back("PEDIT — vertex moved. Next / Previous / Insert / Move / eXit:");
}

void PeditInsertVertex(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT insert");
  const int n = PeditVertexCount(st);
  const int vi = std::clamp(st.peditVertex, 0, std::max(0, n - 1));
  auto insertAt = [&](auto& verts, auto& offsets, auto& bulge, double z) {
    const int v0 = offsets[static_cast<size_t>(st.peditIndex)];
    const size_t at = static_cast<size_t>(v0 + vi + 1) * 3;
    verts.insert(verts.begin() + static_cast<std::ptrdiff_t>(at),
                 {static_cast<typename std::decay<decltype(verts[0])>::type>(x),
                  static_cast<typename std::decay<decltype(verts[0])>::type>(y),
                  static_cast<typename std::decay<decltype(verts[0])>::type>(z)});
    for (size_t i = static_cast<size_t>(st.peditIndex) + 1; i < offsets.size(); ++i)
      offsets[i] += 1;
    SyncPolylineBulge(bulge, verts.size());
  };
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    insertAt(L->paperPolyVerts, L->paperPolyOffsets, L->paperPolyVertsBulge, 0.0);
  } else {
    insertAt(st.userPolylineVerts, st.userPolylineOffsets, st.userPolylineVertsBulge, CadCommitElevation(st));
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  st.peditVertex = vi + 1;
  st.peditPhase = AppCommandState::PeditPhase::WaitVertex;
  log.push_back("PEDIT — vertex inserted. Next / Previous / Insert / Move / eXit:");
}

void EraseModelPolyline(AppCommandState& st, int pi) {
  if (pi < 0 || static_cast<size_t>(pi) + 1 >= st.userPolylineOffsets.size())
    return;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi) + 1];
  st.userPolylineVerts.erase(st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                             st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  if (st.userPolylineVertsBulge.size() >= static_cast<size_t>(v1))
    st.userPolylineVertsBulge.erase(st.userPolylineVertsBulge.begin() + v0, st.userPolylineVertsBulge.begin() + v1);
  if (st.userPolylineVertsNormal.size() >= static_cast<size_t>(v1) * 3)
    st.userPolylineVertsNormal.erase(st.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                                     st.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  const int nv = v1 - v0;
  st.userPolylineOffsets.erase(st.userPolylineOffsets.begin() + (pi + 1));
  for (size_t k = static_cast<size_t>(pi) + 1; k < st.userPolylineOffsets.size(); ++k)
    st.userPolylineOffsets[k] -= nv;
  if (st.userPolylineOffsets.size() == 1)
    st.userPolylineOffsets.clear();
  if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
    st.userPolylineClosed.erase(st.userPolylineClosed.begin() + pi);
  if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
    st.userPolylineAttrs.erase(st.userPolylineAttrs.begin() + pi);
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
  if (!PeditHasPoly(st) || other == st.peditIndex) {
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
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L || other < 0 || static_cast<size_t>(other) + 1 >= L->paperPolyOffsets.size())
      return;
    if (L->paperPolyClosed[static_cast<size_t>(st.peditIndex)] || L->paperPolyClosed[static_cast<size_t>(other)]) {
      log.push_back("PEDIT — cannot join a closed polyline.");
      return;
    }
    load(L->paperPolyVerts, L->paperPolyVertsBulge, L->paperPolyOffsets[static_cast<size_t>(st.peditIndex)],
         L->paperPolyOffsets[static_cast<size_t>(st.peditIndex) + 1], &keepPts, &keepBg);
    load(L->paperPolyVerts, L->paperPolyVertsBulge, L->paperPolyOffsets[static_cast<size_t>(other)],
         L->paperPolyOffsets[static_cast<size_t>(other) + 1], &otherPts, &otherBg);
    if (!JoinVertexLists(&keepPts, &keepBg, otherPts, otherBg)) {
      log.push_back("PEDIT — objects do not touch.");
      return;
    }
    PushUndoSnapshot(st, "PEDIT Join");
    const int keep = st.peditIndex;
    ReplacePolyRange(L->paperPolyVerts, L->paperPolyVertsBulge, L->paperPolyOffsets, keep, keepPts, keepBg);
    int keepAfter = keep;
    ErasePaperPoly(*L, other);
    if (other < keepAfter)
      --keepAfter;
    st.peditIndex = keepAfter;
  } else {
    if (other < 0 || static_cast<size_t>(other) + 1 >= st.userPolylineOffsets.size())
      return;
    if (st.userPolylineClosed[static_cast<size_t>(st.peditIndex)] || st.userPolylineClosed[static_cast<size_t>(other)]) {
      log.push_back("PEDIT — cannot join a closed polyline.");
      return;
    }
    load(st.userPolylineVerts, st.userPolylineVertsBulge, st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)],
         st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1], &keepPts, &keepBg);
    load(st.userPolylineVerts, st.userPolylineVertsBulge, st.userPolylineOffsets[static_cast<size_t>(other)],
         st.userPolylineOffsets[static_cast<size_t>(other) + 1], &otherPts, &otherBg);
    if (!JoinVertexLists(&keepPts, &keepBg, otherPts, otherBg)) {
      log.push_back("PEDIT — objects do not touch.");
      return;
    }
    PushUndoSnapshot(st, "PEDIT Join");
    const int keep = st.peditIndex;
    ReplacePolyRange(st.userPolylineVerts, st.userPolylineVertsBulge, st.userPolylineOffsets, keep, keepPts, keepBg);
    int keepAfter = keep;
    EraseModelPolyline(st, other);
    if (other < keepAfter)
      --keepAfter;
    st.peditIndex = keepAfter;
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  st.peditPhase = AppCommandState::PeditPhase::WaitOption;
  log.push_back("PEDIT — joined. Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
}

bool HandlePeditOption(AppCommandState& st, const std::string& opt, std::vector<std::string>& log) {
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
    st.peditPhase = AppCommandState::PeditPhase::WaitVertex;
    st.peditVertex = 0;
    log.push_back("PEDIT vertex — Next / Previous / Insert / Move / eXit:");
    return true;
  }
  if (opt == "W" || opt == "WIDTH" || opt == "L" || opt == "LTYPE" || opt == "LTYPEGEN") {
    log.push_back("PEDIT — Width and linetype generation are not supported.");
    return true;
  }
  if (opt == "J" || opt == "JOIN") {
    log.push_back("PEDIT — Join: pick a touching polyline, or Enter to skip.");
    st.peditPhase = AppCommandState::PeditPhase::WaitJoin;
    return true;
  }
  if (opt.empty() || opt == "X" || opt == "EXIT") {
    st.active = AppCommandState::Kind::None;
    log.push_back("PEDIT complete.");
    return true;
  }
  log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
  return true;
}

bool HandlePeditVertex(AppCommandState& st, const std::string& opt, std::vector<std::string>& log) {
  const int n = PeditVertexCount(st);
  if (opt == "N" || opt == "NEXT") {
    if (n > 0)
      st.peditVertex = (st.peditVertex + 1) % n;
    log.push_back("PEDIT — vertex " + std::to_string(st.peditVertex + 1) + " of " + std::to_string(n) + ".");
    return true;
  }
  if (opt == "P" || opt == "PREVIOUS") {
    if (n > 0)
      st.peditVertex = (st.peditVertex + n - 1) % n;
    log.push_back("PEDIT — vertex " + std::to_string(st.peditVertex + 1) + " of " + std::to_string(n) + ".");
    return true;
  }
  if (opt == "I" || opt == "INSERT") {
    log.push_back("PEDIT — specify insert point:");
    st.peditPhase = AppCommandState::PeditPhase::WaitVertexInsert;
    return true;
  }
  if (opt == "M" || opt == "MOVE") {
    log.push_back("PEDIT — specify new vertex location:");
    st.peditPhase = AppCommandState::PeditPhase::WaitVertexMove;
    return true;
  }
  if (opt.empty() || opt == "X" || opt == "EXIT") {
    st.peditPhase = AppCommandState::PeditPhase::WaitOption;
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
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      return false;
    const double r = st.userCirclesCxCyZR[k + 3];
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
  const double signedA = st.areaSubtract ? -area : area;
  st.areaRunning += signedA;
  ReportArea(st, signedA, peri, log);
  st.areaDraftPoints.clear();
  if (st.areaAccumulate) {
    st.areaPhase = AppCommandState::AreaPhase::WaitFirst;
    log.push_back(st.areaSubtract ? "AREA Subtract mode — specify points or Object:"
                                  : "AREA Add mode — specify points or Object:");
    return;
  }
  st.active = AppCommandState::Kind::None;
}

void SampleMeasurePath(const AppCommandState& st, PathSample* path, bool* ok) {
  assert(path != nullptr && ok != nullptr);
  *ok = false;
  if (st.measurePaper) {
    if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
      const PaperEntityRef r = st.measurePaperTarget;
      *ok = PaperPathFromRef(*L, r, st.measurePickX, st.measurePickY, path);
    }
    return;
  }
  *ok = ModelPathFromEntity(st, st.measureTarget, st.measurePickX, st.measurePickY, path);
}

bool HandleLeaderText(const std::string& line, const std::string& opt, AppCommandState& st,
                      std::vector<std::string>& log) {
  if (st.leaderPhase == AppCommandState::LeaderPhase::WaitFormat) {
    if (opt == "S" || opt == "STRAIGHT") {
      st.leaderPathStyle = AppCommandState::LeaderPathStyle::Straight;
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
      log.push_back("LEADER — straight. Specify next point:");
      return true;
    }
    if (opt == "SPLINE") {
      st.leaderPathStyle = AppCommandState::LeaderPathStyle::Spline;
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
      log.push_back("LEADER — spline format. Specify next point:");
      return true;
    }
    if (opt == "A" || opt == "ARROW") {
      st.leaderShowArrow = true;
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
      log.push_back("LEADER — arrow. Specify next point:");
      return true;
    }
    if (opt == "N" || opt == "NONE") {
      st.leaderShowArrow = false;
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
      log.push_back("LEADER — no arrow. Specify next point:");
      return true;
    }
    if (line.empty()) {
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
      log.push_back("LEADER — specify next point (Enter = annotation):");
      return true;
    }
    log.push_back("LEADER format — Straight / Spline / Arrow / None:");
    return true;
  }
  if (st.leaderPhase == AppCommandState::LeaderPhase::WaitAnnotation) {
    if (opt == "N" || opt == "NONE")
      st.leaderAnnotation = AppCommandState::LeaderAnnotation::None;
    else if (opt == "M" || opt == "MTEXT" || line.empty()) {
      if (!line.empty())
        st.leaderAnnotation = AppCommandState::LeaderAnnotation::Mtext;
    } else if (opt == "T" || opt == "TOLERANCE" || opt == "C" || opt == "COPY" || opt == "B" || opt == "BLOCK") {
      log.push_back("LEADER — Tolerance, Copy, and Block annotation are not supported.");
      return true;
    } else {
      log.push_back("LEADER annotation — Mtext / None (Tolerance/Copy/Block are not supported):");
      return true;
    }
    if (st.leaderDraftPoints.size() >= 2 || st.leaderFinishAfterAnnotation) {
      FinishLeader(st, log);
      return true;
    }
    st.leaderPhase = AppCommandState::LeaderPhase::WaitFirst;
    log.push_back("LEADER — specify first leader point (Annotation / Format):");
    return true;
  }
  if (opt == "A" || opt == "ANNOTATION") {
    st.leaderPhase = AppCommandState::LeaderPhase::WaitAnnotation;
    st.leaderFinishAfterAnnotation = st.leaderDraftPoints.size() >= 2;
    log.push_back("LEADER annotation — Mtext / None (Tolerance/Copy/Block are not supported):");
    return true;
  }
  if (opt == "M" || opt == "MTEXT") {
    st.leaderAnnotation = AppCommandState::LeaderAnnotation::Mtext;
    log.push_back("LEADER — Mtext annotation. Specify next point:");
    return true;
  }
  if (opt == "N" || opt == "NONE") {
    st.leaderAnnotation = AppCommandState::LeaderAnnotation::None;
    log.push_back("LEADER — no annotation. Specify next point:");
    return true;
  }
  if (opt == "T" || opt == "TOLERANCE" || opt == "C" || opt == "COPY" || opt == "B" || opt == "BLOCK") {
    log.push_back("LEADER — Tolerance, Copy, and Block annotation are not supported.");
    return true;
  }
  if (opt == "F" || opt == "FORMAT") {
    st.leaderPhase = AppCommandState::LeaderPhase::WaitFormat;
    log.push_back("LEADER format — Straight / Spline / Arrow / None:");
    return true;
  }
  if (opt == "S" || opt == "STRAIGHT") {
    st.leaderPathStyle = AppCommandState::LeaderPathStyle::Straight;
    log.push_back("LEADER — straight. Specify next point:");
    return true;
  }
  if (opt == "SPLINE") {
    st.leaderPathStyle = AppCommandState::LeaderPathStyle::Spline;
    log.push_back("LEADER — spline format. Specify next point:");
    return true;
  }
  if (line.empty()) {
    if (st.leaderDraftPoints.size() >= 2) {
      if (st.leaderAnnotation == AppCommandState::LeaderAnnotation::None) {
        FinishLeader(st, log);
        return true;
      }
      st.leaderPhase = AppCommandState::LeaderPhase::WaitAnnotation;
      st.leaderFinishAfterAnnotation = true;
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
  st.leaderDraftPoints.push_back({x, y, CadCommitElevation(st)});
  st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
  log.push_back("LEADER — specify next point (Enter = annotation):");
  return true;
}

bool HandleAreaText(const std::string& line, const std::string& opt, AppCommandState& st,
                    std::vector<std::string>& log) {
  if (opt == "O" || opt == "OBJECT") {
    st.areaPhase = AppCommandState::AreaPhase::WaitObject;
    log.push_back("AREA — select a closed polyline or circle:");
    return true;
  }
  if (opt == "A" || opt == "ADD") {
    st.areaSubtract = false;
    st.areaAccumulate = true;
    log.push_back("AREA Add mode — specify points or Object:");
    return true;
  }
  if (opt == "S" || opt == "SUBTRACT") {
    st.areaSubtract = true;
    st.areaAccumulate = true;
    log.push_back("AREA Subtract mode — specify points or Object:");
    return true;
  }
  if (line.empty()) {
    if (st.areaDraftPoints.size() < 3) {
      if (st.areaAccumulate) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "AREA total %s", FormatLinear(st.areaRunning, LinearPrecision(st)).c_str());
        log.push_back(buf);
        st.active = AppCommandState::Kind::None;
        return true;
      }
      st.active = AppCommandState::Kind::None;
      log.push_back("AREA — canceled (need at least three points).");
      return true;
    }
    const double area = ShoelaceArea(st.areaDraftPoints);
    const double peri = Perimeter(st.areaDraftPoints, true);
    ApplyAreaValue(st, area, peri, log);
    return true;
  }
  float x = 0.f, y = 0.f;
  if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
    log.push_back("AREA — enter a point, Object, Add, or Subtract.");
    return true;
  }
  st.areaDraftPoints.push_back({x, y, CadCommitElevation(st)});
  st.areaPhase = AppCommandState::AreaPhase::WaitNext;
  log.push_back("AREA — specify next point (Enter to close):");
  return true;
}

bool HandleMeasureDivideText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.measurePhase != AppCommandState::MeasurePhase::WaitValue) {
    log.push_back(st.active == K::Measure ? "MEASURE — pick an object first." : "DIVIDE — pick an object first.");
    return true;
  }
  PathSample path;
  bool ok = false;
  SampleMeasurePath(st, &path, &ok);
  if (!ok) {
    log.push_back("Cannot sample that object.");
    st.active = K::None;
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
  st.active = K::None;
  return true;
}

bool TryPickPolylineForPedit(AppCommandState& st, double wx, double wy, int* idx, std::vector<std::string>& log) {
  assert(idx != nullptr);
  assert(std::isfinite(wx) && std::isfinite(wy));
  *idx = -1;
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs)) {
      log.push_back("PEDIT — nothing at that point.");
      return false;
    }
    *idx = ConvertPaperPickedToPolyline(st, *L, hit, log);
    if (*idx < 0) {
      log.push_back("PEDIT — select a polyline, line, or arc.");
      return false;
    }
    st.peditPaper = true;
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
  st.peditPaper = false;
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
  int idx = -1;
  if (!TryPickPolylineForPedit(st, wx, wy, &idx, log))
    return true;
  st.peditIndex = idx;
  st.peditPhase = AppCommandState::PeditPhase::WaitOption;
  log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
  return true;
}

bool HandleMeasureObjectPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  st.measurePickX = wx;
  st.measurePickY = wy;
  if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs)) {
      log.push_back(st.active == K::Measure ? "MEASURE — nothing at that point." : "DIVIDE — nothing at that point.");
      return true;
    }
    st.measurePaper = true;
    st.measurePaperTarget = hit;
    st.measurePhase = AppCommandState::MeasurePhase::WaitValue;
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
  st.measureTarget = hit;
  st.measurePaper = false;
  st.measurePhase = AppCommandState::MeasurePhase::WaitValue;
  log.push_back(st.active == K::Measure ? "MEASURE — specify length of segment:"
                                        : "DIVIDE — enter the number of segments:");
  return true;
}

bool HandleListPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
    PaperEntityRef hit;
    if (PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs))
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
    if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs)) {
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

void ResetCadDrawInquiryDrafts(AppCommandState& st) {
  st.leaderDraftPoints.clear();
  st.leaderPhase = AppCommandState::LeaderPhase::WaitFirst;
  st.leaderAnnotation = AppCommandState::LeaderAnnotation::Mtext;
  st.leaderPathStyle = AppCommandState::LeaderPathStyle::Straight;
  st.leaderShowArrow = true;
  st.leaderFinishAfterAnnotation = false;
  st.peditPhase = AppCommandState::PeditPhase::WaitObject;
  st.peditIndex = -1;
  st.peditVertex = 0;
  st.peditPaper = false;
  st.measurePhase = AppCommandState::MeasurePhase::WaitObject;
  st.measureTarget = SelectedEntity{};
  st.measurePaper = false;
  st.measurePaperTarget = PaperEntityRef{PaperEntityRef::Type::Line, -1};
  st.measurePickX = 0.0;
  st.measurePickY = 0.0;
  st.areaDraftPoints.clear();
  st.areaPhase = AppCommandState::AreaPhase::WaitFirst;
  st.areaRunning = 0.0;
  st.areaSubtract = false;
  st.areaAccumulate = false;
}

void CommitCadPointAt(AppCommandState& st, double x, double y, double z, std::vector<std::string>& log) {
  assert(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));
  PushUndoSnapshot(st, "POINT");
  CommitCadPointToActiveSpace(st, CadPoint{x, y, z});
  BumpCadGpuCache(st);
  log.push_back("POINT — specify a point (Enter to end):");
}

void StartPointCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Point);
  log.push_back("POINT — specify a point (Enter to end):");
}

void StartLeaderCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Leader);
  st.leaderPhase = AppCommandState::LeaderPhase::WaitFirst;
  st.leaderDraftPoints.clear();
  log.push_back("LEADER — specify first leader point (Annotation / Format):");
}

void StartPeditCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Pedit);
  st.peditPhase = AppCommandState::PeditPhase::WaitObject;
  log.push_back("PEDIT — select polyline (or line/arc to convert):");
}

void StartMeasureCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Measure);
  st.measurePhase = AppCommandState::MeasurePhase::WaitObject;
  log.push_back("MEASURE — select object to measure:");
}

void StartDivideCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Divide);
  st.measurePhase = AppCommandState::MeasurePhase::WaitObject;
  log.push_back("DIVIDE — select object to divide:");
}

void StartAreaCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Area);
  st.areaPhase = AppCommandState::AreaPhase::WaitFirst;
  st.areaDraftPoints.clear();
  st.areaRunning = 0.0;
  st.areaSubtract = false;
  st.areaAccumulate = false;
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
  BeginInteractive(st, AppCommandState::Kind::List);
  log.push_back("LIST — select objects, then Enter:");
}

void ExecutePdModeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string t = StringUtil::trimCopy(args);
  if (t.empty()) {
    log.push_back("PDMODE = " + std::to_string(st.pdMode) + " (0/1/2/3/4 + 32 circle / 64 square).");
    return;
  }
  int v = 0;
  if (std::sscanf(t.c_str(), "%d", &v) != 1) {
    log.push_back("PDMODE — enter an integer (0/1/2/3/4, plus 32/64/96).");
    return;
  }
  PushUndoSnapshot(st, "PDMODE");
  st.pdMode = v;
  BumpCadGpuCache(st);
  log.push_back("PDMODE = " + std::to_string(st.pdMode) + ".");
}

void ExecutePdSizeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string t = StringUtil::trimCopy(args);
  if (t.empty()) {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "PDSIZE = %.6g (0 = 5%% of view).", st.pdSize);
    log.push_back(buf);
    return;
  }
  double v = 0.0;
  if (std::sscanf(t.c_str(), "%lf", &v) != 1 || !std::isfinite(v)) {
    log.push_back("PDSIZE — enter a number.");
    return;
  }
  PushUndoSnapshot(st, "PDSIZE");
  st.pdSize = v;
  BumpCadGpuCache(st);
  char buf[80];
  std::snprintf(buf, sizeof(buf), "PDSIZE = %.6g.", st.pdSize);
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
  ResetCadDrawInquiryDrafts(st);
  st.active = AppCommandState::Kind::None;
  st.tableStage = 0;
  st.pointTableStage = 0;
}

// Places a sized table with its upper-left corner at (\p wx, \p wy) and logs the result.
}  // namespace

void StartTableCommand(AppCommandState& st, std::vector<std::string>& log) {
  BeginInteractive(st, AppCommandState::Kind::Table);
  st.tableStage = 0;
  st.tablePendingStyle.clear();
  st.tablePendingCols = 3;
  st.tablePendingRows = 2;
  if (const CadTableStyle* standard = CadTableFindStyle(st.tableStyles, "Standard")) {
    st.tablePendingColumnWidthInches = standard->columnWidthInches;
    st.tablePendingRowHeightInches = standard->rowHeightInches;
  }
  if (st.addPointTableDialogAvailable) {
    st.tablePendingStyle = "Standard";
    st.tablePendingRows = 3;  // one title row, one header row and one initial data row
    st.tableStage = 3;
    st.tableInsertDialogRequested = true;
    return;
  }
  log.push_back("TABLE — table style (Standard) or Enter:");
}

bool HandleTableText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  if (st.tableStage == 0) {
    const std::string name = line.empty() ? std::string("Standard") : line;
    const CadTableStyle* s = CadTableFindStyle(st.tableStyles, name);
    if (!s || s->point) {
      log.push_back("TABLE — no data table style named \"" + name + "\". Type Standard or press Enter.");
      return true;
    }
    st.tablePendingStyle = s->name;
    st.tablePendingColumnWidthInches = s->columnWidthInches;
    st.tablePendingRowHeightInches = s->rowHeightInches;
    st.tableStage = 1;
    log.push_back("TABLE — number of columns <3>:");
    return true;
  }
  if (st.tableStage == 1) {
    if (!ParseTableCount(line, 1, 50, 3, &st.tablePendingCols)) {
      log.push_back("TABLE — enter a whole number of columns from 1 to 50.");
      return true;
    }
    st.tableStage = 2;
    log.push_back("TABLE — number of rows <2>:");
    return true;
  }
  if (st.tableStage == 2) {
    if (!ParseTableCount(line, 1, 500, 2, &st.tablePendingRows)) {
      log.push_back("TABLE — enter a whole number of rows from 1 to 500.");
      return true;
    }
    st.tableStage = 3;
    log.push_back("TABLE — specify the insertion point (upper-left corner):");
    return true;
  }
  log.push_back("TABLE — pick the insertion point.");
  return true;
}

void CommitTableAt(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const CadTableStyle* s = CadTableFindStyle(st.tableStyles, st.tablePendingStyle);
  if (!s) {
    EndTableCommand(st);
    return;
  }
  CadTable t;
  CadTableStyle sizing = *s;
  sizing.columnWidthInches = st.tablePendingColumnWidthInches;
  sizing.rowHeightInches = st.tablePendingRowHeightInches;
  CadTableApplyStyleSize(&t, sizing, st.tablePendingRows, st.tablePendingCols, st.modelUnitsPerPlottedInch);
  t.insX = static_cast<float>(wx);
  t.insY = static_cast<float>(wy);
  t.insZ = static_cast<float>(CadCommitElevation(st));
  if (const TextStyle* ts = ActiveTextStyle(st))
    t.fontFamily = ts->fontFamily;
  PushUndoSnapshot(st, "TABLE");
  st.cadTables.push_back(std::move(t));
  AppendNewObjectAttrs(st, st.cadTableAttrs, st.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
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
  BeginInteractive(st, AppCommandState::Kind::PointTable);
  st.pointTableStage = 0;
  st.pointTableStyle.clear();
  st.pointTableIds.clear();
  log.push_back("ADDPOINTTABLE — point table style (NameNEZD) or Enter:");
}

bool BeginAddPointTableWithPoints(AppCommandState& st, const std::string& styleName, std::vector<int> ids,
                                  std::vector<std::string>& log) {
  const CadTableStyle* s = CadTableFindStyle(st.tableStyles, styleName);
  if (!s || !s->point) {
    log.push_back("ADDPOINTTABLE — no point table style named \"" + styleName + "\".");
    return false;
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  std::vector<int> present;
  for (int id : ids) {
    for (const SurveyPoint& p : st.surveyPoints) {
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
  BeginInteractive(st, AppCommandState::Kind::PointTable);
  st.pointTableStage = 2;
  st.pointTableStyle = s->name;
  st.pointTableIds = std::move(present);
  log.push_back("ADDPOINTTABLE — " + std::to_string(st.pointTableIds.size()) +
                " point(s). Specify the insertion point (upper-left corner):");
  return true;
}

bool HandlePointTableText(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  if (st.pointTableStage == 0) {
    const std::string name = line.empty() ? std::string("NameNEZD") : line;
    const CadTableStyle* s = CadTableFindStyle(st.tableStyles, name);
    if (!s || !s->point) {
      log.push_back("ADDPOINTTABLE — no point table style named \"" + name + "\". Type NameNEZD or press Enter.");
      return true;
    }
    st.pointTableStyle = s->name;
    st.pointTableStage = 1;
    log.push_back("ADDPOINTTABLE — points: numbers such as 1-5,8, a point group name, or Enter for all survey points:");
    return true;
  }
  if (st.pointTableStage == 1) {
    std::vector<int> ids;
    if (line.empty()) {
      for (const SurveyPoint& p : st.surveyPoints)
        ids.push_back(p.id);
    } else if (const int g = FindPointGroupIndex(st, line); g >= 0) {
      ids = ResolvePointGroup(st, st.pointGroups[static_cast<size_t>(g)], &log);
    } else {
      std::vector<std::string> bad;
      const std::vector<PointIdRange> ranges = ParseIdRanges(line, &bad);
      if (!bad.empty()) {
        log.push_back("ADDPOINTTABLE — cannot read \"" + bad.front() + "\". Type numbers such as 1-5,8.");
        return true;
      }
      for (const SurveyPoint& p : st.surveyPoints) {
        if (IdInRanges(ranges, p.id))
          ids.push_back(p.id);
      }
    }
    BeginAddPointTableWithPoints(st, st.pointTableStyle, std::move(ids), log);
    return true;
  }
  log.push_back("ADDPOINTTABLE — pick the insertion point.");
  return true;
}

void CommitPointTableAt(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  const CadTableStyle* s = CadTableFindStyle(st.tableStyles, st.pointTableStyle);
  if (!s) {
    EndTableCommand(st);
    return;
  }
  std::vector<CadTablePointRow> rows;
  for (int id : st.pointTableIds) {
    for (const SurveyPoint& p : st.surveyPoints) {
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
  std::vector<CadTable> tables;
  tables.reserve(chunkCount);
  float firstWidth = 0.f, firstHeight = 0.f;
  for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
    const std::size_t first = chunk * maxRows;
    const std::size_t count = std::min(maxRows, rows.size() - first);
    std::vector<CadTablePointRow> subset(rows.begin() + static_cast<std::ptrdiff_t>(first),
                                         rows.begin() + static_cast<std::ptrdiff_t>(first + count));
    CadTable table;
    CadTableApplyStyleSize(&table, *s, static_cast<int>(count) + 1, cols, st.modelUnitsPerPlottedInch);
    table.cells = CadPointTableCells(*s, subset, st.displayLinearPrecision);
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
    st.cadTables.push_back(std::move(table));
    AppendNewObjectAttrs(st, st.cadTableAttrs, st.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
    if (!st.pointTableLayer.empty()) st.cadTableAttrs.back().layer = st.pointTableLayer;
  }
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
  log.push_back("ADDPOINTTABLE inserted: " + std::to_string(rows.size()) + " point(s) in style " + s->name +
                ". " + std::to_string(chunkCount) + " table(s) (" +
                (st.pointTableDynamic ? "dynamic" : "static") + " setting, " +
                std::to_string(firstWidth) + " x " + std::to_string(firstHeight) + " first table).");
  EndTableCommand(st);
}

bool SubmitCadDrawInquiryViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (!IsCadDrawInquiryKind(st.active))
    return false;
  const double z = CadCommitElevation(st);
  if (st.active == K::Table) {
    if (st.tableStage != 3) {
      log.push_back("TABLE — answer the prompt first.");
      return true;
    }
    CommitTableAt(st, wx, wy, log);
    return true;
  }
  if (st.active == K::PointTable) {
    if (st.pointTableStage != 2) {
      log.push_back("ADDPOINTTABLE — answer the prompt first.");
      return true;
    }
    CommitPointTableAt(st, wx, wy, log);
    return true;
  }
  if (st.active == K::Point) {
    CommitCadPointAt(st, wx, wy, z, log);
    return true;
  }
  if (st.active == K::Leader && st.leaderPhase != AppCommandState::LeaderPhase::WaitAnnotation &&
      st.leaderPhase != AppCommandState::LeaderPhase::WaitFormat) {
    st.leaderDraftPoints.push_back({wx, wy, z});
    if (st.leaderPhase == AppCommandState::LeaderPhase::WaitFirst)
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
    log.push_back("LEADER — specify next point (Enter = annotation):");
    return true;
  }
  if (st.active == K::Area && st.areaPhase != AppCommandState::AreaPhase::WaitObject) {
    st.areaDraftPoints.push_back({wx, wy, z});
    st.areaPhase = AppCommandState::AreaPhase::WaitNext;
    log.push_back("AREA — specify next point (Enter to close):");
    return true;
  }
  if (st.active == K::Pedit && st.peditPhase == AppCommandState::PeditPhase::WaitVertexMove) {
    PeditMoveVertex(st, wx, wy, log);
    return true;
  }
  if (st.active == K::Pedit && st.peditPhase == AppCommandState::PeditPhase::WaitVertexInsert) {
    PeditInsertVertex(st, wx, wy, log);
    return true;
  }
  if (st.active == K::Pedit && st.peditPhase == AppCommandState::PeditPhase::WaitJoin)
    return HandlePeditJoinPick(st, wx, wy, log);
  if (st.active == K::Pedit && st.peditPhase == AppCommandState::PeditPhase::WaitObject)
    return HandlePeditObjectPick(st, wx, wy, log);
  if ((st.active == K::Measure || st.active == K::Divide) &&
      st.measurePhase == AppCommandState::MeasurePhase::WaitObject)
    return HandleMeasureObjectPick(st, wx, wy, log);
  if (st.active == K::List)
    return HandleListPick(st, wx, wy, log);
  if (st.active == K::Area && st.areaPhase == AppCommandState::AreaPhase::WaitObject)
    return HandleAreaObjectPick(st, wx, wy, log);
  return true;
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
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadAnnotations.size())
        return "ANNOTATION";
      switch (st.cadAnnotations[static_cast<size_t>(e.index)].kind) {
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
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 2 < st.userLinesFlat.size()) {
      r.hasPoint = true;
      r.x = st.userLinesFlat[k];
      r.y = st.userLinesFlat[k + 1];
      r.z = st.userLinesFlat[k + 2];
    }
  } else if (e.type == T::Circle) {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 2 < st.userCirclesCxCyZR.size()) {
      r.hasPoint = true;
      r.x = st.userCirclesCxCyZR[k];
      r.y = st.userCirclesCxCyZR[k + 1];
      r.z = st.userCirclesCxCyZR[k + 2];
    }
  } else if (e.type == T::Polyline) {
    if (e.index >= 0 && static_cast<size_t>(e.index) + 1 < st.userPolylineOffsets.size()) {
      const size_t v = static_cast<size_t>(st.userPolylineOffsets[static_cast<size_t>(e.index)]) * 3;
      if (v + 2 < st.userPolylineVerts.size()) {
        r.hasPoint = true;
        r.x = st.userPolylineVerts[v];
        r.y = st.userPolylineVerts[v + 1];
        r.z = st.userPolylineVerts[v + 2];
      }
    }
  } else if (e.type == T::Point) {
    if (e.index >= 0 && static_cast<size_t>(e.index) < st.cadPoints.size()) {
      const CadPoint& p = st.cadPoints[static_cast<size_t>(e.index)];
      r.hasPoint = true;
      r.x = p.x;
      r.y = p.y;
      r.z = p.z;
    }
  } else if (e.type == T::Annotation) {
    if (e.index >= 0 && static_cast<size_t>(e.index) < st.cadAnnotations.size()) {
      const CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(e.index)];
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
  BeginInteractive(st, AppCommandState::Kind::DataExtract);
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
    ResetCadDrawInquiryDrafts(st);
    st.active = AppCommandState::Kind::None;
    log.push_back("DATAEXTRACTION canceled — no file chosen.");
    return true;
  }
  if (!DataExtractWrite(path, grid, &err)) {
    log.push_back("DATAEXTRACTION — " + err + ".");
    return true;
  }
  ResetCadDrawInquiryDrafts(st);
  st.active = AppCommandState::Kind::None;
  log.push_back("DATAEXTRACTION wrote " + std::to_string(records.size()) + " row(s) to " + path + ".");
  return true;
}

bool HandleCadDrawInquiryTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (!IsCadDrawInquiryKind(st.active) && st.active != K::DataExtract)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string opt = UpperTrim(line);

  if (st.active == K::Point) {
    if (line.empty()) {
      st.active = K::None;
      log.push_back("POINT ended.");
      return true;
    }
    float x = 0.f, y = 0.f;
    if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
      log.push_back("POINT — enter a point as X,Y, or Enter to end.");
      return true;
    }
    CommitCadPointAt(st, x, y, CadCommitElevation(st), log);
    return true;
  }

  if (st.active == K::Leader)
    return HandleLeaderText(line, opt, st, log);

  if (st.active == K::Pedit) {
    if (st.peditPhase == AppCommandState::PeditPhase::WaitOption)
      return HandlePeditOption(st, opt, log);
    if (st.peditPhase == AppCommandState::PeditPhase::WaitVertex)
      return HandlePeditVertex(st, opt, log);
    if (st.peditPhase == AppCommandState::PeditPhase::WaitJoin) {
      if (line.empty()) {
        st.peditPhase = AppCommandState::PeditPhase::WaitOption;
        log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
        return true;
      }
      log.push_back("PEDIT — pick a touching polyline, or Enter to skip.");
      return true;
    }
    if (st.peditPhase == AppCommandState::PeditPhase::WaitVertexMove ||
        st.peditPhase == AppCommandState::PeditPhase::WaitVertexInsert) {
      float x = 0.f, y = 0.f;
      if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
        log.push_back("PEDIT — enter a point.");
        return true;
      }
      if (st.peditPhase == AppCommandState::PeditPhase::WaitVertexInsert)
        PeditInsertVertex(st, x, y, log);
      else
        PeditMoveVertex(st, x, y, log);
      return true;
    }
    if (line.empty()) {
      log.push_back("PEDIT — select a polyline.");
      return true;
    }
    log.push_back("PEDIT — pick a polyline, line, or arc.");
    return true;
  }

  if (st.active == K::Measure || st.active == K::Divide)
    return HandleMeasureDivideText(line, st, log);

  if (st.active == K::Table)
    return HandleTableText(line, st, log);
  if (st.active == K::PointTable)
    return HandlePointTableText(line, st, log);
  if (st.active == K::DataExtract)
    return HandleDataExtractText(line, st, log);
  if (st.active == K::Area)
    return HandleAreaText(line, opt, st, log);

  if (st.active == K::List) {
    if (line.empty()) {
      st.active = K::None;
      log.push_back("LIST complete.");
      return true;
    }
    log.push_back("LIST — pick objects, then Enter.");
    return true;
  }
  return true;
}

const char* CadDrawInquiryCommandInputHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  if (st.active == K::Point)
    return "POINT: click or X,Y | Enter ends | ESC cancel";
  if (st.active == K::Leader)
    return "LEADER: next point | Annotation / Format | Enter finishes | ESC cancel";
  if (st.active == K::Pedit)
    return "PEDIT: pick polyline, or Close/Open/Edit/Fit/Spline/Decurve/Reverse | ESC cancel";
  if (st.active == K::Measure)
    return "MEASURE: pick object, then length | ESC cancel";
  if (st.active == K::Divide)
    return "DIVIDE: pick object, then number of segments | ESC cancel";
  if (st.active == K::Area)
    return "AREA: points | Object | Add | Subtract | Enter reports | ESC cancel";
  if (st.active == K::List)
    return "LIST: pick objects | Enter ends | ESC cancel";
  return nullptr;
}
