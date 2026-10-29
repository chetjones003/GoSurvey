// CadCommands_Issue1019.cpp — LEADER, POINT, PEDIT, MEASURE, DIVIDE, AREA, LIST (REQ-456).

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"

#include "util/NumFormat.hpp"
#include "util/StringUtil.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

[[nodiscard]] std::string UpperTrim(const std::string& raw) {
  std::string s = StringUtil::trimCopy(raw);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return s;
}

[[nodiscard]] bool IsIssue1019Kind(AppCommandState::Kind k) {
  using K = AppCommandState::Kind;
  return k == K::Point || k == K::Leader || k == K::Pedit || k == K::Measure || k == K::Divide ||
         k == K::Area || k == K::List;
}

void BeginInteractive(AppCommandState& st, AppCommandState::Kind kind) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
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

[[nodiscard]] double PathLength(const PathSample& path) {
  assert(!path.pts.empty());
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
    const spline::Vec3 a{st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 2]};
    const spline::Vec3 b{st.userLinesFlat[k + 3], st.userLinesFlat[k + 4], st.userLinesFlat[k + 5]};
    const double da = (a.x - pickX) * (a.x - pickX) + (a.y - pickY) * (a.y - pickY);
    const double db = (b.x - pickX) * (b.x - pickX) + (b.y - pickY) * (b.y - pickY);
    if (da <= db) {
      out->pts.push_back(a);
      out->pts.push_back(b);
    } else {
      out->pts.push_back(b);
      out->pts.push_back(a);
    }
    return true;
  }
  if (e.type == T::Polyline) {
    if (e.index < 0 || static_cast<size_t>(e.index) + 1 >= st.userPolylineOffsets.size())
      return false;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(e.index)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(e.index) + 1];
    out->closed = static_cast<size_t>(e.index) < st.userPolylineClosed.size() &&
                  st.userPolylineClosed[static_cast<size_t>(e.index)] != 0;
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      if (b + 2 >= st.userPolylineVerts.size())
        break;
      out->pts.push_back({st.userPolylineVerts[b], st.userPolylineVerts[b + 1], st.userPolylineVerts[b + 2]});
    }
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
    if (!(r > 1e-12))
      return false;
    const double start = std::atan2(pickY - cy, pickX - cx);
    constexpr int kSeg = 64;
    out->closed = true;
    for (int i = 0; i <= kSeg; ++i) {
      const double a = start + kTwoPi * static_cast<double>(i) / static_cast<double>(kSeg);
      out->pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a), cz});
    }
    return true;
  }
  return false;
}

bool PaperPathFromRef(const PaperLayout& L, const PaperEntityRef& r, double pickX, double pickY,
                      PathSample* out) {
  assert(out != nullptr);
  out->pts.clear();
  out->closed = false;
  if (r.type == PaperEntityRef::Type::Line) {
    const size_t k = static_cast<size_t>(r.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return false;
    const spline::Vec3 a{L.paperLines[k], L.paperLines[k + 1], L.paperLines[k + 2]};
    const spline::Vec3 b{L.paperLines[k + 3], L.paperLines[k + 4], L.paperLines[k + 5]};
    const double da = (a.x - pickX) * (a.x - pickX) + (a.y - pickY) * (a.y - pickY);
    const double db = (b.x - pickX) * (b.x - pickX) + (b.y - pickY) * (b.y - pickY);
    if (da <= db) {
      out->pts = {a, b};
    } else {
      out->pts = {b, a};
    }
    return true;
  }
  if (r.type == PaperEntityRef::Type::Polyline) {
    if (r.index < 0 || static_cast<size_t>(r.index) + 1 >= L.paperPolyOffsets.size())
      return false;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(r.index)];
    const int v1 = L.paperPolyOffsets[static_cast<size_t>(r.index) + 1];
    out->closed = static_cast<size_t>(r.index) < L.paperPolyClosed.size() &&
                  L.paperPolyClosed[static_cast<size_t>(r.index)] != 0;
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      if (b + 2 >= L.paperPolyVerts.size())
        break;
      out->pts.push_back({L.paperPolyVerts[b], L.paperPolyVerts[b + 1], L.paperPolyVerts[b + 2]});
    }
    return out->pts.size() >= 2;
  }
  if (r.type == PaperEntityRef::Type::Circle) {
    const size_t k = static_cast<size_t>(r.index) * 3;
    if (k + 2 >= L.paperCircles.size())
      return false;
    const double cx = L.paperCircles[k];
    const double cy = L.paperCircles[k + 1];
    const double rcirc = L.paperCircles[k + 2];
    if (!(rcirc > 1e-12))
      return false;
    const double start = std::atan2(pickY - cy, pickX - cx);
    constexpr int kSeg = 64;
    out->closed = true;
    for (int i = 0; i <= kSeg; ++i) {
      const double a = start + kTwoPi * static_cast<double>(i) / static_cast<double>(kSeg);
      out->pts.push_back({cx + rcirc * std::cos(a), cy + rcirc * std::sin(a), 0.0});
    }
    return true;
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
  for (double d : stations) {
    spline::Vec3 p{};
    if (!PointAtLength(path, d, &p))
      continue;
    CommitCadPointToActiveSpace(st, CadPoint{p.x, p.y, p.z});
  }
  BumpCadGpuCache(st);
  log.push_back(std::string(verb) + " — placed " + std::to_string(stations.size()) + " point(s).");
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
    char buf[160];
    std::snprintf(buf, sizeof(buf), "POLYLINE  layer %s  model  index %d", layer.c_str(), e.index);
    log.push_back(buf);
    return;
  }
  if (e.type == T::Spline) {
    log.push_back("SPLINE  layer " + layer + "  model");
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
    log.push_back("LEADER  layer " + layer + "  model");
    return;
  }
  log.push_back("LIST — " + layer + "  model  type " + std::to_string(static_cast<int>(e.type)));
}

void ListPaperEntity(const PaperLayout& L, const PaperEntityRef& r, std::vector<std::string>& log) {
  auto layerOf = [](const std::vector<EntityAttributes>& attrs, int i) -> std::string {
    if (i >= 0 && static_cast<size_t>(i) < attrs.size() && !attrs[static_cast<size_t>(i)].layer.empty())
      return attrs[static_cast<size_t>(i)].layer;
    return "0";
  };
  switch (r.type) {
  case PaperEntityRef::Type::Line:
    log.push_back("LINE  layer " + layerOf(L.paperLineAttrs, r.index) + "  paper");
    break;
  case PaperEntityRef::Type::Circle:
    log.push_back("CIRCLE  layer " + layerOf(L.paperCircleAttrs, r.index) + "  paper");
    break;
  case PaperEntityRef::Type::Polyline:
    log.push_back("POLYLINE  layer " + layerOf(L.paperPolyAttrs, r.index) + "  paper");
    break;
  case PaperEntityRef::Type::Spline:
    log.push_back("SPLINE  layer " + layerOf(L.paperSplineAttrs, r.index) + "  paper");
    break;
  case PaperEntityRef::Type::Point:
    log.push_back("POINT  layer " + layerOf(L.paperPointAttrs, r.index) + "  paper");
    break;
  case PaperEntityRef::Type::Leader:
    log.push_back("LEADER  layer " + layerOf(L.paperMultileaderAttrs, r.index) + "  paper");
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
  for (const spline::Vec3& p : st.leaderDraftPoints) {
    ml.pathXyz.push_back(static_cast<float>(p.x));
    ml.pathXyz.push_back(static_cast<float>(p.y));
    ml.pathXyz.push_back(static_cast<float>(p.z));
  }
  const spline::Vec3& land = st.leaderDraftPoints.back();
  if (st.leaderAnnotation == AppCommandState::LeaderAnnotation::Mtext) {
    const float mup = std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
    const float plotH = std::max(st.activeMultileaderStyle.textSizeInches, 0.01f);
    CadAnnotation& a = ml.label;
    a.kind = CadAnnotation::Kind::Mtext;
    a.plottedHeightInches = plotH;
    a.insX = static_cast<float>(land.x);
    a.insY = static_cast<float>(land.y);
    a.insZ = static_cast<float>(land.z);
    a.boxMinX = a.insX;
    a.boxMinY = a.insY;
    a.boxMaxX = a.insX + 22.f * plotH * mup;
    a.boxMaxY = a.insY + 2.6f * plotH * mup;
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

bool ConvertPickedToPolyline(AppCommandState& st, const SelectedEntity& hit, std::vector<std::string>& log) {
  using T = SelectedEntity::Type;
  if (hit.type == T::Polyline) {
    st.peditIndex = hit.index;
    st.peditPaper = false;
    return true;
  }
  if (hit.type == T::LineSeg) {
    const size_t k = static_cast<size_t>(hit.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return false;
    PushUndoSnapshot(st, "PEDIT convert");
    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(0);
    const int base = st.userPolylineOffsets.back();
    for (int i = 0; i < 2; ++i) {
      st.userPolylineVerts.push_back(st.userLinesFlat[k + static_cast<size_t>(i) * 3]);
      st.userPolylineVerts.push_back(st.userLinesFlat[k + static_cast<size_t>(i) * 3 + 1]);
      st.userPolylineVerts.push_back(st.userLinesFlat[k + static_cast<size_t>(i) * 3 + 2]);
    }
    st.userPolylineOffsets.push_back(base + 2);
    st.userPolylineClosed.push_back(0);
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
    st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                           st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k) + 6);
    if (static_cast<size_t>(hit.index) < st.userLineAttrs.size())
      st.userLineAttrs.erase(st.userLineAttrs.begin() + hit.index);
    st.peditIndex = static_cast<int>(st.userPolylineOffsets.size()) - 2;
    st.peditPaper = false;
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted line to polyline.");
    return true;
  }
  if (hit.type == T::Arc) {
    if (static_cast<size_t>(hit.index) >= st.userArcs.size())
      return false;
    const CadArc a = st.userArcs[static_cast<size_t>(hit.index)];
    PushUndoSnapshot(st, "PEDIT convert");
    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(0);
    const int base = st.userPolylineOffsets.back();
    const double x0 = a.cx + a.r * std::cos(a.startRad);
    const double y0 = a.cy + a.r * std::sin(a.startRad);
    const double x1 = a.cx + a.r * std::cos(a.startRad + a.sweepRad);
    const double y1 = a.cy + a.r * std::sin(a.startRad + a.sweepRad);
    st.userPolylineVerts.push_back(x0);
    st.userPolylineVerts.push_back(y0);
    st.userPolylineVerts.push_back(a.z);
    st.userPolylineVerts.push_back(x1);
    st.userPolylineVerts.push_back(y1);
    st.userPolylineVerts.push_back(a.z);
    st.userPolylineOffsets.push_back(base + 2);
    st.userPolylineClosed.push_back(0);
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
    const size_t bulgeI = static_cast<size_t>(base);
    if (bulgeI < st.userPolylineVertsBulge.size()) {
      const double s = std::tan(static_cast<double>(a.sweepRad) / 4.0);
      st.userPolylineVertsBulge[bulgeI] = static_cast<float>(s);
    }
    st.userArcs.erase(st.userArcs.begin() + hit.index);
    if (static_cast<size_t>(hit.index) < st.userArcAttrs.size())
      st.userArcAttrs.erase(st.userArcAttrs.begin() + hit.index);
    st.peditIndex = static_cast<int>(st.userPolylineOffsets.size()) - 2;
    st.peditPaper = false;
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted arc to polyline.");
    return true;
  }
  return false;
}

bool ConvertPaperPickedToPolyline(AppCommandState& st, PaperLayout& L, const PaperEntityRef& hit,
                                  std::vector<std::string>& log) {
  if (hit.type == PaperEntityRef::Type::Polyline) {
    st.peditIndex = hit.index;
    st.peditPaper = true;
    return true;
  }
  if (hit.type == PaperEntityRef::Type::Line) {
    const size_t k = static_cast<size_t>(hit.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return false;
    PushUndoSnapshot(st, "PEDIT convert");
    if (L.paperPolyOffsets.empty())
      L.paperPolyOffsets.push_back(0);
    const int base = L.paperPolyOffsets.back();
    L.paperPolyVerts.push_back(L.paperLines[k]);
    L.paperPolyVerts.push_back(L.paperLines[k + 1]);
    L.paperPolyVerts.push_back(L.paperLines[k + 2]);
    L.paperPolyVerts.push_back(L.paperLines[k + 3]);
    L.paperPolyVerts.push_back(L.paperLines[k + 4]);
    L.paperPolyVerts.push_back(L.paperLines[k + 5]);
    L.paperPolyOffsets.push_back(base + 2);
    L.paperPolyClosed.push_back(0);
    L.paperPolyAttrs.push_back(MakeNewEntityAttrs(st));
    L.paperLines.erase(L.paperLines.begin() + static_cast<std::ptrdiff_t>(k),
                       L.paperLines.begin() + static_cast<std::ptrdiff_t>(k) + 6);
    if (static_cast<size_t>(hit.index) < L.paperLineAttrs.size())
      L.paperLineAttrs.erase(L.paperLineAttrs.begin() + hit.index);
    st.peditIndex = static_cast<int>(L.paperPolyOffsets.size()) - 2;
    st.peditPaper = true;
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted line to polyline.");
    return true;
  }
  if (hit.type == PaperEntityRef::Type::Arc) {
    if (static_cast<size_t>(hit.index) >= L.paperArcs.size())
      return false;
    const CadArc a = L.paperArcs[static_cast<size_t>(hit.index)];
    PushUndoSnapshot(st, "PEDIT convert");
    if (L.paperPolyOffsets.empty())
      L.paperPolyOffsets.push_back(0);
    const int base = L.paperPolyOffsets.back();
    const float x0 = a.cx + a.r * std::cos(a.startRad);
    const float y0 = a.cy + a.r * std::sin(a.startRad);
    const float x1 = a.cx + a.r * std::cos(a.startRad + a.sweepRad);
    const float y1 = a.cy + a.r * std::sin(a.startRad + a.sweepRad);
    L.paperPolyVerts.push_back(x0);
    L.paperPolyVerts.push_back(y0);
    L.paperPolyVerts.push_back(a.z);
    L.paperPolyVerts.push_back(x1);
    L.paperPolyVerts.push_back(y1);
    L.paperPolyVerts.push_back(a.z);
    L.paperPolyOffsets.push_back(base + 2);
    L.paperPolyClosed.push_back(0);
    L.paperPolyAttrs.push_back(MakeNewEntityAttrs(st));
    L.paperArcs.erase(L.paperArcs.begin() + hit.index);
    if (static_cast<size_t>(hit.index) < L.paperArcAttrs.size())
      L.paperArcAttrs.erase(L.paperArcAttrs.begin() + hit.index);
    st.peditIndex = static_cast<int>(L.paperPolyOffsets.size()) - 2;
    st.peditPaper = true;
    BumpCadGpuCache(st);
    log.push_back("PEDIT — converted arc to polyline.");
    return true;
  }
  return false;
}

bool PeditHasPoly(const AppCommandState& st) {
  if (st.peditPaper) {
    const PaperLayout* L = ActivePaperGeometryTarget(const_cast<AppCommandState&>(st));
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
  } else {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1];
    reverseRange(st.userPolylineVerts, v0, v1);
    if (!st.userPolylineVertsBulge.empty()) {
      std::reverse(st.userPolylineVertsBulge.begin() + v0, st.userPolylineVertsBulge.begin() + v1);
      for (int i = v0; i < v1; ++i)
        st.userPolylineVertsBulge[static_cast<size_t>(i)] = -st.userPolylineVertsBulge[static_cast<size_t>(i)];
    }
  }
  BumpCadGpuCache(st);
  log.push_back("PEDIT — reversed.");
}

void PeditDecurve(AppCommandState& st, std::vector<std::string>& log) {
  if (st.peditPaper) {
    log.push_back("PEDIT — paper polylines have no bulge; nothing to decurve.");
    return;
  }
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT Decurve");
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1];
  SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
  for (int i = v0; i < v1; ++i)
    st.userPolylineVertsBulge[static_cast<size_t>(i)] = 0.f;
  BumpCadGpuCache(st);
  log.push_back("PEDIT — decurved.");
}

void PeditFit(AppCommandState& st, std::vector<std::string>& log) {
  if (st.peditPaper) {
    log.push_back("PEDIT — Fit needs bulge storage; paper polylines keep straight segments.");
    return;
  }
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT Fit");
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(st.peditIndex) + 1];
  SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
  for (int i = v0; i + 1 < v1; ++i)
    st.userPolylineVertsBulge[static_cast<size_t>(i)] = 0.5f;
  BumpCadGpuCache(st);
  log.push_back("PEDIT — fit (bulge arcs).");
}

void PeditSplineDensify(AppCommandState& st, std::vector<std::string>& log) {
  if (!PeditHasPoly(st))
    return;
  PushUndoSnapshot(st, "PEDIT Spline");
  auto densify = [](auto& verts, auto& offsets, int pi) {
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
  };
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    densify(L->paperPolyVerts, L->paperPolyOffsets, st.peditIndex);
  } else {
    densify(st.userPolylineVerts, st.userPolylineOffsets, st.peditIndex);
    SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  log.push_back("PEDIT — spline densified.");
}

int PeditVertexCount(const AppCommandState& st) {
  if (st.peditPaper) {
    const PaperLayout* L = ActivePaperGeometryTarget(const_cast<AppCommandState&>(st));
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
  auto insertAt = [&](auto& verts, auto& offsets, double z) {
    const int v0 = offsets[static_cast<size_t>(st.peditIndex)];
    const size_t at = static_cast<size_t>(v0 + vi + 1) * 3;
    verts.insert(verts.begin() + static_cast<std::ptrdiff_t>(at), {static_cast<typename std::decay<decltype(verts[0])>::type>(x),
                                                                  static_cast<typename std::decay<decltype(verts[0])>::type>(y),
                                                                  static_cast<typename std::decay<decltype(verts[0])>::type>(z)});
    for (size_t i = static_cast<size_t>(st.peditIndex) + 1; i < offsets.size(); ++i)
      offsets[i] += 1;
  };
  if (st.peditPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L)
      return;
    insertAt(L->paperPolyVerts, L->paperPolyOffsets, 0.0);
  } else {
    insertAt(st.userPolylineVerts, st.userPolylineOffsets, CadCommitElevation(st));
    SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());
    SyncPolylineNormal(st.userPolylineVertsNormal, st.userPolylineVerts.size());
  }
  BumpCadGpuCache(st);
  st.peditVertex = vi + 1;
  st.peditPhase = AppCommandState::PeditPhase::WaitVertex;
  log.push_back("PEDIT — vertex inserted. Next / Previous / Insert / Move / eXit:");
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
    st.peditPhase = AppCommandState::PeditPhase::WaitVertexMove;
    st.peditVertex = st.peditVertex;  // insert after current
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
    if (!ModelPathFromEntity(st, e, 0.0, 0.0, &path) || path.pts.size() < 3)
      return false;
    *area = ShoelaceArea(path.pts);
    *peri = Perimeter(path.pts, true);
    return true;
  }
  return false;
}

}  // namespace

void ResetIssue1019Drafts(AppCommandState& st) {
  st.leaderDraftPoints.clear();
  st.leaderPhase = AppCommandState::LeaderPhase::WaitFirst;
  st.leaderAnnotation = AppCommandState::LeaderAnnotation::Mtext;
  st.leaderFormat = AppCommandState::LeaderFormat::Straight;
  st.peditPhase = AppCommandState::PeditPhase::WaitObject;
  st.peditIndex = -1;
  st.peditVertex = 0;
  st.peditPaper = false;
  st.measurePhase = AppCommandState::MeasurePhase::WaitObject;
  st.measureTarget = SelectedEntity{};
  st.measurePaper = false;
  st.areaDraftPoints.clear();
  st.areaPhase = AppCommandState::AreaPhase::WaitFirst;
  st.areaRunning = 0.0;
  st.areaSubtract = false;
  st.listPhase = AppCommandState::ListPhase::WaitObject;
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
        ListPaperEntity(*L, r, log);
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

bool SubmitIssue1019ViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (!IsIssue1019Kind(st.active))
    return false;
  const double z = CadCommitElevation(st);
  if (st.active == K::Point) {
    CommitCadPointAt(st, wx, wy, z, log);
    return true;
  }
  if (st.active == K::Leader) {
    st.leaderDraftPoints.push_back({wx, wy, z});
    if (st.leaderPhase == AppCommandState::LeaderPhase::WaitFirst)
      st.leaderPhase = AppCommandState::LeaderPhase::WaitNext;
    log.push_back("LEADER — specify next point (Enter = annotation):");
    return true;
  }
  if (st.active == K::Area) {
    st.areaDraftPoints.push_back({wx, wy, z});
    st.areaPhase = AppCommandState::AreaPhase::WaitNext;
    log.push_back("AREA — specify next point (Enter to close):");
    return true;
  }
  if (st.active == K::Pedit && st.peditPhase == AppCommandState::PeditPhase::WaitVertexMove) {
    PeditMoveVertex(st, wx, wy, log);
    return true;
  }
  if (st.active == K::Pedit && st.peditPhase == AppCommandState::PeditPhase::WaitObject) {
    if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
      PaperEntityRef hit;
      if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs)) {
        log.push_back("PEDIT — nothing at that point.");
        return true;
      }
      if (!ConvertPaperPickedToPolyline(st, *L, hit, log)) {
        log.push_back("PEDIT — select a polyline, line, or arc.");
        return true;
      }
      st.peditPhase = AppCommandState::PeditPhase::WaitOption;
      log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
      return true;
    }
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back("PEDIT — nothing at that point.");
      return true;
    }
    if (!ConvertPickedToPolyline(st, hit, log) && hit.type != SelectedEntity::Type::Polyline) {
      log.push_back("PEDIT — select a polyline, line, or arc.");
      return true;
    }
    if (hit.type == SelectedEntity::Type::Polyline)
      st.peditIndex = hit.index;
    st.peditPhase = AppCommandState::PeditPhase::WaitOption;
    log.push_back("PEDIT — Close/Open/Join/Edit/Fit/Spline/Decurve/Reverse/eXit:");
    return true;
  }
  if ((st.active == K::Measure || st.active == K::Divide) &&
      st.measurePhase == AppCommandState::MeasurePhase::WaitObject) {
    if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
      PaperEntityRef hit;
      if (!PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs)) {
        log.push_back(st.active == K::Measure ? "MEASURE — nothing at that point."
                                              : "DIVIDE — nothing at that point.");
        return true;
      }
      st.measurePaper = true;
      st.measurePaperType = hit.type;
      st.measurePaperIndex = hit.index;
      st.measurePhase = AppCommandState::MeasurePhase::WaitValue;
      log.push_back(st.active == K::Measure ? "MEASURE — specify length of segment:"
                                            : "DIVIDE — enter the number of segments:");
      return true;
    }
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back(st.active == K::Measure ? "MEASURE — nothing at that point."
                                            : "DIVIDE — nothing at that point.");
      return true;
    }
    st.measureTarget = hit;
    st.measurePaper = false;
    st.measurePhase = AppCommandState::MeasurePhase::WaitValue;
    log.push_back(st.active == K::Measure ? "MEASURE — specify length of segment:"
                                          : "DIVIDE — enter the number of segments:");
    return true;
  }
  if (st.active == K::List) {
    if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
      PaperEntityRef hit;
      if (PickPaperEntityAt(*L, static_cast<float>(wx), static_cast<float>(wy), 0.15f, &hit, &st.blockDefs))
        ListPaperEntity(*L, hit, log);
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
  if (st.active == K::Area && st.areaPhase == AppCommandState::AreaPhase::WaitObject) {
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
    const double signedA = st.areaSubtract ? -area : area;
    st.areaRunning += signedA;
    ReportArea(st, signedA, peri, log);
    if (st.areaRunning != signedA) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "AREA total %s", FormatLinear(st.areaRunning, LinearPrecision(st)).c_str());
      log.push_back(buf);
    }
    st.active = AppCommandState::Kind::None;
    return true;
  }
  return true;
}

bool HandleIssue1019TextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (!IsIssue1019Kind(st.active))
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

  if (st.active == K::Leader) {
    if (opt == "A" || opt == "ANNOTATION") {
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
      log.push_back("LEADER format — Straight / Spline / Arrow / None:");
      return true;
    }
    if (opt == "S" || opt == "STRAIGHT") {
      st.leaderFormat = AppCommandState::LeaderFormat::Straight;
      log.push_back("LEADER — straight. Specify next point:");
      return true;
    }
    if (opt == "SPLINE") {
      st.leaderFormat = AppCommandState::LeaderFormat::Spline;
      log.push_back("LEADER — spline format. Specify next point:");
      return true;
    }
    if (line.empty()) {
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

  if (st.active == K::Pedit) {
    if (st.peditPhase == AppCommandState::PeditPhase::WaitOption)
      return HandlePeditOption(st, opt, log);
    if (st.peditPhase == AppCommandState::PeditPhase::WaitVertex)
      return HandlePeditVertex(st, opt, log);
    if (st.peditPhase == AppCommandState::PeditPhase::WaitVertexMove) {
      float x = 0.f, y = 0.f;
      if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
        log.push_back("PEDIT — enter a point.");
        return true;
      }
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

  if (st.active == K::Measure || st.active == K::Divide) {
    if (st.measurePhase != AppCommandState::MeasurePhase::WaitValue) {
      log.push_back(st.active == K::Measure ? "MEASURE — pick an object first."
                                            : "DIVIDE — pick an object first.");
      return true;
    }
    PathSample path;
    bool ok = false;
    if (st.measurePaper) {
      if (const PaperLayout* L = ActivePaperGeometryTarget(st)) {
        PaperEntityRef r{st.measurePaperType, st.measurePaperIndex};
        ok = PaperPathFromRef(*L, r, 0.0, 0.0, &path);
      }
    } else {
      ok = ModelPathFromEntity(st, st.measureTarget, 0.0, 0.0, &path);
    }
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
      const int count = path.closed ? n : n - 1;
      for (int i = 1; i <= count; ++i) {
        if (!path.closed && i == n)
          break;
        stations.push_back(len * static_cast<double>(i) / static_cast<double>(n));
      }
      if (path.closed && !stations.empty() && std::fabs(stations.back() - len) < 1e-9)
        stations.pop_back();
      PlacePointsAlong(st, path, stations, log, "DIVIDE");
    }
    st.active = K::None;
    return true;
  }

  if (st.active == K::Area) {
    if (opt == "O" || opt == "OBJECT") {
      st.areaPhase = AppCommandState::AreaPhase::WaitObject;
      log.push_back("AREA — select a closed polyline or circle:");
      return true;
    }
    if (opt == "A" || opt == "ADD") {
      st.areaSubtract = false;
      log.push_back("AREA Add mode — specify points or Object:");
      return true;
    }
    if (opt == "S" || opt == "SUBTRACT") {
      st.areaSubtract = true;
      log.push_back("AREA Subtract mode — specify points or Object:");
      return true;
    }
    if (line.empty()) {
      if (st.areaDraftPoints.size() < 3) {
        st.active = K::None;
        log.push_back("AREA — canceled (need at least three points).");
        return true;
      }
      const double area = ShoelaceArea(st.areaDraftPoints);
      const double peri = Perimeter(st.areaDraftPoints, true);
      const double signedA = st.areaSubtract ? -area : area;
      st.areaRunning += signedA;
      ReportArea(st, signedA, peri, log);
      st.active = K::None;
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

const char* Issue1019CommandInputHint(const AppCommandState& st) {
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
