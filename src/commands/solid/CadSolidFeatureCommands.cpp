#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

void CadExtrudeSelection(AppCommandState& st, const std::string& rest, std::vector<std::string>& log) {
  std::istringstream iss(rest);
  std::string tok;
  if (!(iss >> tok)) {
    log.push_back("Usage: select a closed polyline or circle, then EXTRUDE <height>.");
    return;
  }
  char* end = nullptr;
  const double height = std::strtod(tok.c_str(), &end);
  if (tok.empty() || !end || *end != '\0' || !std::isfinite(height) || height == 0.0) {
    log.push_back("EXTRUDE — height must be a non-zero number.");
    return;
  }
  std::string extra;
  if (iss >> extra) {
    log.push_back("EXTRUDE — too many values. Usage: EXTRUDE <height>.");
    return;
  }
  if (st.selection.empty()) {
    log.push_back("EXTRUDE — select a closed polyline or circle first, then run EXTRUDE.");
    return;
  }

  std::vector<brep::Solid> built;
  int skipped = 0;
  for (const SelectedEntity& sel : st.selection) {
    brep::Profile profile;
    if (!ExtrudeProfileFromSelection(st, sel, &profile)) {
      ++skipped;
      continue;
    }
    brep::Solid solid;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::Extrude(profile, height, &solid, &why)) {
      log.push_back(std::string("EXTRUDE — ") + brep::ProblemText(why));
      ++skipped;
      continue;
    }
    built.push_back(std::move(solid));
  }

  if (built.empty()) {
    log.push_back("EXTRUDE — nothing in the selection could be extruded (need a closed polyline or a circle).");
    return;
  }

  PushUndoSnapshot(st, "Extrude");
  for (brep::Solid& s : built) {
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
    st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
    log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));  // a feature result may be a Cylinder / Cone (#515)
  }
  BumpCadGpuCache(st);
  st.selection.clear();
  if (skipped > 0)
    log.push_back("EXTRUDE — " + std::to_string(skipped) +
                  " selected object(s) skipped (not a closed polyline or circle).");
}

// -------------------------------------------------------------------------------------------------
// The prompted EXTRUDE command (REQ-314 / ADR-046). A bare EXTRUDE asks for objects (unless some are
// already selected), then a height — typed, or dragged from the cursor with a live ghost. The ghost
// and the commit both go through CadBuildExtrudeSolids, so the ghost cannot show a shape the click
// would not build (the same one-source-of-truth rule the prompted solid primitives follow).
// -------------------------------------------------------------------------------------------------


void CancelExtrudeCommand(AppCommandState& st) {
  if (auto* es = st.sessionAs<stepengine::ExtrudeSession>()) {
    es->phase = stepengine::ExtrudePhase::SelectProfiles;
    es->profiles.clear();
    es->heightPickValid = false;
    es->heightPick = 0.0;
  }
}

std::string CadExtrudePromptText(const AppCommandState& st) {
  const auto* es = st.sessionAs<stepengine::ExtrudeSession>();
  if (!es || es->phase == stepengine::ExtrudePhase::SelectProfiles) {
    return "EXTRUDE — select closed polylines or circles, Enter when done. ESC cancels.";
  }
  std::string s = "EXTRUDE — specify height of extrusion";
  if (es->heightPickValid) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), " <%.4g>", es->heightPick);
    s += buf;
  }
  s += ": type a value or click. ESC cancels.";
  return s;
}

static void ExtrudeEnterHeightPhase(AppCommandState& st, std::vector<std::string>& log) {
  auto* es = st.sessionAs<stepengine::ExtrudeSession>();
  if (!es)
    return;
  int skipped = 0;
  const int n = GatherExtrudeProfiles(st, &es->profiles, &skipped);
  if (n == 0) {
    log.push_back("EXTRUDE — nothing selected can be extruded (need a closed polyline or a circle).");
    es->phase = stepengine::ExtrudePhase::SelectProfiles;
    return;
  }
  if (skipped > 0)
    log.push_back("EXTRUDE — " + std::to_string(skipped) + " selected object(s) are not profiles and were ignored.");
  es->phase = stepengine::ExtrudePhase::WaitHeight;
  es->heightPickValid = false;
  log.push_back(CadExtrudePromptText(st));
}

void StartExtrudeCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  auto sess = std::make_unique<stepengine::ExtrudeSession>();
  auto* es = sess.get();
  stepengine::BeginSession(st, std::move(sess));

  if (!st.selection.empty()) {
    ExtrudeEnterHeightPhase(st, log);
    if (es->phase == stepengine::ExtrudePhase::WaitHeight)
      return;
    st.selection.clear();
  }
  es->phase = stepengine::ExtrudePhase::SelectProfiles;
  log.push_back(CadExtrudePromptText(st));
}

bool CadBuildExtrudeSolids(const AppCommandState& st, double height, std::vector<brep::Solid>* out) {
  out->clear();
  const auto* es = st.sessionAs<stepengine::ExtrudeSession>();
  if (!es || es->profiles.empty() || !std::isfinite(height) || height == 0.0)
    return false;
  for (const brep::Profile& p : es->profiles) {
    brep::Solid s;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::Extrude(p, height, &s, &why)) {
      out->clear();
      return false;
    }
    out->push_back(std::move(s));
  }
  return !out->empty();
}

void CadResolveExtrudePick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray) {
  auto* es = st.sessionAs<stepengine::ExtrudeSession>();
  if (!es)
    return;
  es->heightPickValid = false;
  if (es->phase != stepengine::ExtrudePhase::WaitHeight || es->profiles.empty())
    return;

  (void)cursorOnPlane;
  if (!ray || !ray->valid())
    return;
  const ucs::Ucs& plane = es->profiles.front().plane;
  const ray3d::Vec3 axisDir = plane.zAxis;
  const ray3d::Vec3 w0 = ray3d::Sub(plane.origin, ray->origin);
  const double b = ray3d::Dot(axisDir, ray->dir);
  const double den = 1.0 - b * b;
  if (!(den > 1e-9))
    return;
  const double dv = ray3d::Dot(axisDir, w0);
  const double ev = ray3d::Dot(ray->dir, w0);
  const double h = (b * ev - dv) / den;
  if (!std::isfinite(h) || std::fabs(h) <= 1e-9)
    return;
  es->heightPick = h;
  es->heightPickValid = true;
}

static void CommitExtrude(AppCommandState& st, double height, std::vector<std::string>& log) {
  std::vector<brep::Solid> built;
  if (!CadBuildExtrudeSolids(st, height, &built)) {
    log.push_back("EXTRUDE — that height does not produce a solid; try another value.");
    return;
  }
  PushUndoSnapshot(st, "Extrude");
  for (brep::Solid& s : built) {
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
    st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
    log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));
  }
  BumpCadGpuCache(st);
  st.selection.clear();
  stepengine::EndSession(st);
}

bool HandleExtrudeTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* es = st.sessionAs<stepengine::ExtrudeSession>();
  if (!es)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (es->phase == stepengine::ExtrudePhase::SelectProfiles) {
    if (!line.empty())
      return false;
    if (st.selection.empty()) {
      log.push_back("EXTRUDE — nothing selected. Click a closed polyline or circle, or ESC.");
      return true;
    }
    ExtrudeEnterHeightPhase(st, log);
    return true;
  }

  if (line.empty()) {
    if (es->heightPickValid) {
      CommitExtrude(st, es->heightPick, log);
    } else {
      log.push_back("EXTRUDE — type a height, or move the cursor to a view where one can be read.");
    }
    return true;
  }
  char* end = nullptr;
  const double h = std::strtod(line.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(h) || h == 0.0) {
    log.push_back("EXTRUDE — \"" + line + "\" is not a height. Type a non-zero number, or ESC.");
    return true;
  }
  const double signedH = es->heightPickValid ? std::copysign(h, es->heightPick) : h;
  CommitExtrude(st, signedH, log);
  return true;
}

void SubmitExtrudeViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  (void)wx;
  (void)wy;
  auto* es = st.sessionAs<stepengine::ExtrudeSession>();
  if (!es)
    return;
  if (es->phase == stepengine::ExtrudePhase::SelectProfiles)
    return;
  if (!es->heightPickValid) {
    log.push_back("EXTRUDE — no height under the cursor here; type a value, or orbit the view.");
    return;
  }
  CommitExtrude(st, es->heightPick, log);
}

void CancelLoftCommand(AppCommandState& st) {
  if (auto* ls = st.sessionAs<stepengine::LoftSession>())
    ls->phase = stepengine::LoftPhase::SelectProfiles;
}

std::string CadLoftPromptText(const AppCommandState& st) {
  std::string s = "LOFT — select closed polylines or circles in lofting order";
  const int have = static_cast<int>(st.selection.size());
  if (have > 0)
    s += " (" + std::to_string(have) + " selected)";
  s += ", Enter when done. ESC cancels.";
  return s;
}

bool CadBuildLoftSolid(const AppCommandState& st, brep::Solid* out) {
  std::vector<brep::Profile> profiles;
  int skipped = 0;
  GatherExtrudeProfiles(st, &profiles, &skipped);
  if (profiles.size() < 2)
    return false;
  brep::Problem why = brep::Problem::Ok;
  return brep::Loft(profiles, out, &why);
}

static void CommitLoft(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<brep::Profile> profiles;
  int skipped = 0;
  GatherExtrudeProfiles(st, &profiles, &skipped);
  if (profiles.size() < 2) {
    log.push_back("LOFT — select at least two closed polylines or circles, in lofting order.");
    return;
  }
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::Loft(profiles, &solid, &why)) {
    log.push_back(std::string("LOFT — ") + brep::ProblemText(why));
    return;
  }
  PushUndoSnapshot(st, "Loft");
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
  log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));
  if (skipped > 0)
    log.push_back("LOFT — " + std::to_string(skipped) +
                  " selected object(s) are not profiles and were ignored.");
  BumpCadGpuCache(st);
  st.selection.clear();
  stepengine::EndSession(st);
}

void StartLoftCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  stepengine::BeginSession(st, std::make_unique<stepengine::LoftSession>());

  if (st.selection.size() >= 2) {
    CommitLoft(st, log);
    if (st.active != AppCommandState::Kind::Loft)
      return;
  }
  log.push_back(CadLoftPromptText(st));
}

bool HandleLoftTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  if (!st.sessionAs<stepengine::LoftSession>())
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  if (!line.empty())
    return false;
  if (st.selection.size() < 2) {
    log.push_back("LOFT — select at least two profiles, in lofting order, then Enter. ESC cancels.");
    return true;
  }
  CommitLoft(st, log);
  return true;
}

// -------------------------------------------------------------------------------------------------
// The SWEEP command (REQ-315 / ADR-048, GitHub issue #241). Select one closed profile and one line
// or arc path; Enter sweeps the profile along the path (brep::Sweep). The closed loop is the
// profile and the open curve is the path — selection order does not matter. The ghost and the
// commit both go through CadBuildSweepSolid.
// -------------------------------------------------------------------------------------------------

namespace {

/// A brep::SweepPath from a selected LINE, ARC, CIRCLE or POLYLINE entity (a polyline's per-vertex
/// bulges become the arc segments — REQ-316). Returns false for anything else. An open polyline is
/// the common path case; a **closed** polyline, or a full-sweep Arc, or a Circle, builds a **closed**
/// path (REQ-315 2026-09-04, issue #259) — reachable for a closed shape only when it is not the
/// first closed shape in the selection, since `GatherSweepInputs` tries the profile role first for
/// every selected entity in order.
[[nodiscard]] bool SweepPathFromSelection(const AppCommandState& st, const SelectedEntity& sel,
                                          brep::SweepPath* out) {
  if (sel.type == SelectedEntity::Type::Polyline) {
    const std::size_t pi = static_cast<std::size_t>(sel.index);
    if (sel.index < 0 || pi + 1 >= st.drawing.userPolylineOffsets.size())
      return false;
    const bool closed = pi < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[pi] != 0;
    const int vB = st.drawing.userPolylineOffsets[pi];
    const int vE = st.drawing.userPolylineOffsets[pi + 1];
    if (vE - vB < 2)
      return false;
    std::vector<ray3d::Vec3> pts;
    for (int v = vB; v < vE; ++v) {
      const std::size_t f = static_cast<std::size_t>(v);
      if (f >= st.drawing.vertexCount())
        return false;
      pts.push_back({static_cast<double>(st.drawing.vertex(f).x),
                     static_cast<double>(st.drawing.vertex(f).y),
                     static_cast<double>(st.drawing.vertex(f).z)});
    }
    if (closed) {
      // A closed polyline may or may not repeat its first vertex at the end
      // (ExtrudeProfileFromSelection makes the same check for the same reason).
      if (pts.size() >= 2 && ray3d::Length(ray3d::Sub(pts.front(), pts.back())) < 1e-7)
        pts.pop_back();
      if (pts.size() < 3)
        return false;
    }
    const std::size_t segCount = closed ? pts.size() : pts.size() - 1;
    std::vector<brep::SweepSegment> segs;
    for (std::size_t k = 0; k < segCount; ++k) {
      const std::size_t k1 = (k + 1) % pts.size();
      const std::size_t vi = static_cast<std::size_t>(vB) + k;
      const float bulge =
          vi < st.drawing.userPolylineVertsBulge.size() ? st.drawing.userPolylineVertsBulge[vi] : 0.f;
      brep::SweepSegment seg;
      if (std::fabs(bulge) > 1e-6f) {
        // REQ-325 / ADR-053: this segment's own plane, when it is not flat +Z — same construction
        // issue #373's 3D FILLET solve and REQ-325 increments 1-3 already use. Before this, ANY
        // bulge segment spanning a change in elevation was refused outright ("not a planar arc"),
        // even a genuinely tilted-but-planar one from JOIN.
        float nx = 0.f, ny = 0.f, nz = 1.f;
        if (vi * 3 + 2 < st.drawing.userPolylineVertsNormal.size()) {
          nx = st.drawing.userPolylineVertsNormal[vi * 3];
          ny = st.drawing.userPolylineVertsNormal[vi * 3 + 1];
          nz = st.drawing.userPolylineVertsNormal[vi * 3 + 2];
        }
        if (IsFlatNormal(nx, ny, nz)) {
          if (std::fabs(pts[k].z - pts[k1].z) > 1e-6)
            return false;  // a flat-normal bulge spanning different Z is inconsistent data
          const BulgeArcSpan span =
              BulgeArc(pts[k].x, pts[k].y, pts[k1].x, pts[k1].y, static_cast<double>(bulge));
          if (!span.valid)
            return false;
          seg.arc = true;
          seg.centre = {span.cx, span.cy, pts[k].z};
          seg.normal = {0.0, 0.0, 1.0};
          seg.sweep = span.sweep;
        } else {
          const ray3d::Vec3 nrm{static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)};
          ucs::Ucs plane{};
          if (!ucs::FromNormal(pts[k], nrm, &plane))
            return false;
          const ucs::Point2D p1Local = ucs::WorldToPlane(plane, pts[k1]);
          const BulgeArcSpan span = BulgeArc(0.0, 0.0, p1Local.x, p1Local.y, static_cast<double>(bulge));
          if (!span.valid)
            return false;
          const ray3d::Vec3 centreWorld = ucs::PlaneToWorld(plane, ucs::Point2D{span.cx, span.cy});
          ucs::Ucs canon{};
          if (!ucs::FromNormal(centreWorld, nrm, &canon))
            return false;
          const ucs::Point2D sLocal = ucs::WorldToPlane(canon, pts[k]);
          const ucs::Point2D eLocal = ucs::WorldToPlane(canon, pts[k1]);
          const double thetaA = std::atan2(sLocal.y, sLocal.x);
          const double thetaB = std::atan2(eLocal.y, eLocal.x);
          constexpr double kTwoPi = 6.28318530717958647692;
          double sweep = thetaB - thetaA;
          if (bulge >= 0.f) {
            while (sweep < 0.0) sweep += kTwoPi;
          } else {
            while (sweep > 0.0) sweep -= kTwoPi;
          }
          seg.arc = true;
          seg.centre = centreWorld;
          seg.normal = nrm;
          seg.sweep = sweep;
        }
      }
      segs.push_back(seg);
    }
    if (closed)
      pts.push_back(pts.front());  // close the path: points[0] == points.back()
    out->points = std::move(pts);
    out->segments = std::move(segs);
    return true;
  }
  if (sel.type == SelectedEntity::Type::LineSeg) {
    const std::size_t b = static_cast<std::size_t>(sel.index);
    if (sel.index < 0 || b >= st.drawing.lineCount())
      return false;
    const ray3d::Vec3 p0{static_cast<double>(st.drawing.line(b).x0),
                         static_cast<double>(st.drawing.line(b).y0),
                         static_cast<double>(st.drawing.line(b).z0)};
    const ray3d::Vec3 p1{static_cast<double>(st.drawing.line(b).x1),
                         static_cast<double>(st.drawing.line(b).y1),
                         static_cast<double>(st.drawing.line(b).z1)};
    out->points = {p0, p1};
    out->segments = {brep::SweepSegment{}};  // one straight segment
    return true;
  }
  if (sel.type == SelectedEntity::Type::Arc) {
    if (sel.index < 0 || static_cast<std::size_t>(sel.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<std::size_t>(sel.index)];
    if (!(a.r > 0.0f) || !(std::fabs(a.sweepRad) > 1e-6f))
      return false;
    ucs::Ucs frame;
    if (!ucs::FromNormal(ray3d::Vec3{a.cx, a.cy, a.z}, ray3d::Vec3{a.nx, a.ny, a.nz}, &frame))
      return false;
    const ray3d::Vec3 start = ucs::PointOnPlaneCircle(frame, a.r, a.startRad);
    const ray3d::Vec3 end = ucs::PointOnPlaneCircle(frame, a.r, a.startRad + a.sweepRad);
    brep::SweepSegment seg;
    seg.arc = true;
    seg.centre = frame.origin;
    seg.normal = frame.zAxis;
    seg.sweep = a.sweepRad;
    out->points = {start, end};
    out->segments = {seg};
    return true;
  }
  if (sel.type == SelectedEntity::Type::Circle) {
    // A full-circle path (REQ-315 2026-09-04, issue #259): the same closed-path shape as a
    // full-sweep Arc above, one segment with a full 2*pi turn. `GatherSweepInputs` tries the
    // profile role first for every selected entity in order, so a Circle only reaches here when it
    // is not the first closed shape selected.
    const std::size_t ci = static_cast<std::size_t>(sel.index);
    if (sel.index < 0 || ci >= st.drawing.circleCount())
      return false;
    const CadCircleRecord cc = st.drawing.circle(ci);
    const double cx = cc.cx;
    const double cy = cc.cy;
    const double cz = cc.cz;
    const double r = cc.r;
    if (!(r > 0.0))
      return false;
    ray3d::Vec3 nrm{0, 0, 1};
    if (ci * 3 + 2 < st.drawing.userCircleNormals.size()) {
      const ray3d::Vec3 cand{static_cast<double>(st.drawing.userCircleNormals[ci * 3 + 0]),
                             static_cast<double>(st.drawing.userCircleNormals[ci * 3 + 1]),
                             static_cast<double>(st.drawing.userCircleNormals[ci * 3 + 2])};
      if (ray3d::Length(cand) > 1e-9)
        nrm = ray3d::Normalize(cand);
    }
    ucs::Ucs frame;
    if (!ucs::FromNormal(ray3d::Vec3{cx, cy, cz}, nrm, &frame))
      return false;
    const ray3d::Vec3 start = ucs::PointOnPlaneCircle(frame, r, 0.0);
    brep::SweepSegment seg;
    seg.arc = true;
    seg.centre = frame.origin;
    seg.normal = frame.zAxis;
    seg.sweep = 2.0 * 3.14159265358979323846;
    out->points = {start, start};
    out->segments = {seg};
    return true;
  }
  return false;
}

/// Scan the selection for the profile (a closed polyline / circle) and the path (a line / arc).
bool GatherSweepInputs(const AppCommandState& st, brep::Profile* profOut, brep::SweepPath* pathOut,
                       bool* haveProf, bool* havePath) {
  *haveProf = false;
  *havePath = false;
  for (const SelectedEntity& sel : st.selection) {
    if (!*haveProf && ExtrudeProfileFromSelection(st, sel, profOut)) {
      *haveProf = true;
      continue;
    }
    if (!*havePath && SweepPathFromSelection(st, sel, pathOut))
      *havePath = true;
  }
  return *haveProf && *havePath;
}

/// The twist / alignment options the SWEEP command has collected (REQ-315 keywords). The kernel
/// already carries both; this only plumbs the two `AppCommandState` fields into a `SweepOptions`.
brep::SweepOptions SweepOptionsFrom(const AppCommandState& st) {
  constexpr double kSweepDegToRad = 3.14159265358979323846 / 180.0;
  const auto* sws = st.sessionAs<stepengine::SweepSession>();
  brep::SweepOptions o;
  o.twistRad = (sws ? sws->twistDeg : 0.0) * kSweepDegToRad;
  o.alignToPath = sws ? sws->alignToPath : true;
  return o;
}

} // namespace

void CancelSweepCommand(AppCommandState& st) {
  if (auto* sws = st.sessionAs<stepengine::SweepSession>()) {
    sws->phase = stepengine::SweepPhase::SelectInputs;
    sws->twistDeg = 0.0;
    sws->alignToPath = true;
  }
}

std::string CadSweepPromptText(const AppCommandState& st) {
  const auto* sws = st.sessionAs<stepengine::SweepSession>();
  const double twistDeg = sws ? sws->twistDeg : 0.0;
  const bool alignToPath = sws ? sws->alignToPath : true;
  brep::Profile prof;
  brep::SweepPath path;
  bool hp = false;
  bool ha = false;
  (void)GatherSweepInputs(st, &prof, &path, &hp, &ha);
  std::string s = "SWEEP — select a closed profile and a line, arc or open polyline path";
  if (hp || ha) {
    s += " (";
    s += hp ? "profile ok" : "no profile yet";
    s += ha ? ", path ok" : ", no path yet";
    s += ")";
  }
  s += ". Enter builds; T <deg> twist, A toggles path-alignment. ESC cancels.";
  if (twistDeg != 0.0 || !alignToPath) {
    char b[80];
    std::snprintf(b, sizeof(b), " [twist %g°, align %s]", twistDeg,
                  alignToPath ? "on" : "off");
    s += b;
  }
  return s;
}

bool CadBuildSweepSolid(const AppCommandState& st, brep::Solid* out) {
  brep::Profile prof;
  brep::SweepPath path;
  bool hp = false;
  bool ha = false;
  if (!GatherSweepInputs(st, &prof, &path, &hp, &ha))
    return false;
  brep::Problem why = brep::Problem::Ok;
  return brep::Sweep(prof, path, SweepOptionsFrom(st), out, &why);
}

static void CommitSweep(AppCommandState& st, std::vector<std::string>& log) {
  brep::Profile prof;
  brep::SweepPath path;
  bool hp = false;
  bool ha = false;
  GatherSweepInputs(st, &prof, &path, &hp, &ha);
  if (!hp || !ha) {
    log.push_back("SWEEP — select one closed polyline or circle (the profile) and one line, arc or open polyline "
                  "(the path).");
    return;
  }
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::Sweep(prof, path, SweepOptionsFrom(st), &solid, &why)) {
    log.push_back(std::string("SWEEP — ") + brep::ProblemText(why));
    return;
  }
  PushUndoSnapshot(st, "Sweep");
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
  log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));
  BumpCadGpuCache(st);
  st.selection.clear();
  stepengine::EndSession(st);
}

void StartSweepCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  stepengine::BeginSession(st, std::make_unique<stepengine::SweepSession>());

  brep::Profile prof;
  brep::SweepPath path;
  bool hp = false;
  bool ha = false;
  if (GatherSweepInputs(st, &prof, &path, &hp, &ha)) {
    CommitSweep(st, log);
    if (st.active != AppCommandState::Kind::Sweep)
      return;
  }
  log.push_back(CadSweepPromptText(st));
}

bool HandleSweepTextInput(const std::string& lineIn, AppCommandState& st,
                          std::vector<std::string>& log) {
  auto* sws = st.sessionAs<stepengine::SweepSession>();
  if (!sws)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  if (line.empty()) {
    CommitSweep(st, log);
    return true;
  }

  std::string tok = line;
  std::string rest;
  const std::size_t sp = line.find_first_of(" \t");
  if (sp != std::string::npos) {
    tok = line.substr(0, sp);
    rest = StringUtil::trimCopy(line.substr(sp + 1));
  }
  std::string low = StringUtil::toLowerAsciiCopy(tok);
  if (rest.empty() && low.size() > 1 && low[0] == 't' &&
      (std::isdigit(static_cast<unsigned char>(low[1])) || low[1] == '-' || low[1] == '+' ||
       low[1] == '.')) {
    rest = tok.substr(1);
    low = "t";
  }

  if (low == "t" || low == "twist") {
    if (rest.empty()) {
      log.push_back("SWEEP — twist needs an angle, e.g. \"T 45\" (degrees over the whole path).");
      return true;
    }
    char* end = nullptr;
    const double deg = std::strtod(rest.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(deg)) {
      log.push_back("SWEEP — \"" + rest + "\" is not an angle. Type degrees, e.g. \"T 45\".");
      return true;
    }
    sws->twistDeg = deg;
    log.push_back(CadSweepPromptText(st));
    return true;
  }

  if (low == "a" || low == "align" || low == "alignment") {
    if (rest.empty()) {
      sws->alignToPath = !sws->alignToPath;
    } else {
      const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(rest[0])));
      if (c == 'y')
        sws->alignToPath = true;
      else if (c == 'n')
        sws->alignToPath = false;
      else {
        log.push_back("SWEEP — alignment is \"A Y\" or \"A N\" (bare \"A\" toggles).");
        return true;
      }
    }
    log.push_back(CadSweepPromptText(st));
    return true;
  }

  return false;
}

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

} // namespace

void CancelRevolveCommand(AppCommandState& st) {
  if (auto* rvs = st.sessionAs<stepengine::RevolveSession>()) {
    rvs->phase = stepengine::RevolvePhase::SelectProfiles;
    rvs->profiles.clear();
    rvs->axisStartSet = false;
    rvs->angleDeg = 360.0;
  }
}

std::string CadRevolvePromptText(const AppCommandState& st) {
  const auto* rvs = st.sessionAs<stepengine::RevolveSession>();
  if (!rvs)
    return "REVOLVE";
  switch (rvs->phase) {
  case stepengine::RevolvePhase::SelectProfiles:
    return "REVOLVE — select closed polylines or circles, Enter when done. ESC cancels.";
  case stepengine::RevolvePhase::WaitAxisStart:
    return "REVOLVE — specify axis start point (click or type X,Y). ESC cancels.";
  case stepengine::RevolvePhase::WaitAxisEnd:
    return "REVOLVE — specify axis end point (click or type X,Y). ESC cancels.";
  case stepengine::RevolvePhase::WaitAngle: {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "REVOLVE — angle of revolution in degrees <%g>: ",
                  rvs->angleDeg);
    return std::string(buf) + "Enter for the default. ESC cancels.";
  }
  }
  return "REVOLVE";
}

static void RevolveEnterAxisPhase(AppCommandState& st, std::vector<std::string>& log) {
  auto* rvs = st.sessionAs<stepengine::RevolveSession>();
  if (!rvs)
    return;
  int skipped = 0;
  const int n = GatherExtrudeProfiles(st, &rvs->profiles, &skipped);
  if (n == 0) {
    log.push_back("REVOLVE — nothing selected can be revolved (need a closed polyline or a circle).");
    rvs->phase = stepengine::RevolvePhase::SelectProfiles;
    return;
  }
  if (skipped > 0)
    log.push_back("REVOLVE — " + std::to_string(skipped) + " selected object(s) are not profiles and were ignored.");
  rvs->phase = stepengine::RevolvePhase::WaitAxisStart;
  rvs->axisStartSet = false;
  log.push_back(CadRevolvePromptText(st));
}

void StartRevolveCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  auto sess = std::make_unique<stepengine::RevolveSession>();
  auto* rvs = sess.get();
  stepengine::BeginSession(st, std::move(sess));

  if (!st.selection.empty()) {
    RevolveEnterAxisPhase(st, log);
    if (rvs->phase != stepengine::RevolvePhase::SelectProfiles)
      return;
    st.selection.clear();
  }
  rvs->phase = stepengine::RevolvePhase::SelectProfiles;
  log.push_back(CadRevolvePromptText(st));
}

bool CadBuildRevolveSolids(const AppCommandState& st, double angleDeg, std::vector<brep::Solid>* out) {
  out->clear();
  const auto* rvs = st.sessionAs<stepengine::RevolveSession>();
  if (!rvs || rvs->profiles.empty() || !rvs->axisStartSet)
    return false;
  const ray3d::Vec3 dir = ray3d::Sub(rvs->axisEnd, rvs->axisStart);
  if (!(ray3d::Length(dir) > 1e-9))
    return false;
  double ang = angleDeg * kDegToRad;
  if (!std::isfinite(ang) || std::fabs(ang) <= 1e-9)
    return false;
  if (std::fabs(ang) > 2.0 * 3.14159265358979323846)
    ang = (ang > 0.0 ? 1.0 : -1.0) * 2.0 * 3.14159265358979323846;
  for (const brep::Profile& p : rvs->profiles) {
    brep::Solid s;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::Revolve(p, rvs->axisStart, dir, ang, &s, &why)) {
      out->clear();
      return false;
    }
    out->push_back(std::move(s));
  }
  return !out->empty();
}

static void CommitRevolve(AppCommandState& st, double angleDeg, std::vector<std::string>& log) {
  auto* rvs = st.sessionAs<stepengine::RevolveSession>();
  if (!rvs)
    return;
  const ray3d::Vec3 dir = ray3d::Sub(rvs->axisEnd, rvs->axisStart);
  double ang = angleDeg * kDegToRad;
  if (std::fabs(ang) > 2.0 * 3.14159265358979323846)
    ang = (ang > 0.0 ? 1.0 : -1.0) * 2.0 * 3.14159265358979323846;
  std::vector<brep::Solid> built;
  bool ok = !rvs->profiles.empty() && ray3d::Length(dir) > 1e-9;
  brep::Problem lastWhy = brep::Problem::Ok;
  if (ok) {
    for (const brep::Profile& p : rvs->profiles) {
      brep::Solid s;
      brep::Problem why = brep::Problem::Ok;
      if (brep::Revolve(p, rvs->axisStart, dir, ang, &s, &why))
        built.push_back(std::move(s));
      else {
        lastWhy = why;
        ok = false;
      }
    }
  }
  if (!ok || built.empty()) {
    log.push_back(std::string("REVOLVE — ") +
                  (lastWhy != brep::Problem::Ok ? brep::ProblemText(lastWhy)
                                                : "could not build a solid from that axis and angle."));
    return;
  }
  PushUndoSnapshot(st, "Revolve");
  for (brep::Solid& s : built) {
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
    st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
    log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));
  }
  BumpCadGpuCache(st);
  st.selection.clear();
  stepengine::EndSession(st);
}

bool HandleRevolveTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* rvs = st.sessionAs<stepengine::RevolveSession>();
  if (!rvs)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  using RP = stepengine::RevolvePhase;

  if (rvs->phase == RP::SelectProfiles) {
    if (!line.empty())
      return false;
    if (st.selection.empty()) {
      log.push_back("REVOLVE — nothing selected. Click a closed polyline or circle, or ESC.");
      return true;
    }
    RevolveEnterAxisPhase(st, log);
    return true;
  }

  if (rvs->phase == RP::WaitAxisStart || rvs->phase == RP::WaitAxisEnd) {
    if (line.empty())
      return true;
    float lx = 0.f, ly = 0.f;
    double wz = 0.0;
    if (!ParseStoragePointZ(st, line, &lx, &ly, &wz, /*allowRelative=*/false, 0.f, 0.f)) {
      log.push_back("REVOLVE — could not read the point. Use X,Y or X,Y,Z.");
      return true;
    }
    const ray3d::Vec3 p{static_cast<double>(lx), static_cast<double>(ly), wz};
    if (rvs->phase == RP::WaitAxisStart) {
      rvs->axisStart = p;
      rvs->axisStartSet = true;
      rvs->phase = RP::WaitAxisEnd;
    } else {
      rvs->axisEnd = p;
      rvs->phase = RP::WaitAngle;
    }
    log.push_back(CadRevolvePromptText(st));
    return true;
  }

  if (line.empty()) {
    CommitRevolve(st, rvs->angleDeg, log);
    return true;
  }
  char* end = nullptr;
  const double deg = std::strtod(line.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(deg) || std::fabs(deg) <= 1e-9) {
    log.push_back("REVOLVE — \"" + line + "\" is not an angle. Type degrees, or ESC.");
    return true;
  }
  rvs->angleDeg = deg;
  CommitRevolve(st, deg, log);
  return true;
}

void SubmitRevolveViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* rvs = st.sessionAs<stepengine::RevolveSession>();
  if (!rvs)
    return;
  using RP = stepengine::RevolvePhase;
  const ray3d::Vec3 p{static_cast<double>(wx), static_cast<double>(wy),
                      static_cast<double>(CadCommitElevation(st))};
  if (rvs->phase == RP::WaitAxisStart) {
    rvs->axisStart = p;
    rvs->axisStartSet = true;
    rvs->phase = RP::WaitAxisEnd;
    log.push_back(CadRevolvePromptText(st));
    return;
  }
  if (rvs->phase == RP::WaitAxisEnd) {
    rvs->axisEnd = p;
    rvs->phase = RP::WaitAngle;
    log.push_back(CadRevolvePromptText(st));
    return;
  }
}
