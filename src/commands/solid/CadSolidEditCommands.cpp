#include "CadCommandsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// -------------------------------------------------------------------------------------------------
// UNION / SUBTRACT / INTERSECT (REQ-314 / ADR-046 increment 4, B1). SUBTRACT prompts twice — the
// solids to subtract FROM, then the solids to subtract; UNION and INTERSECT prompt once. A pre-
// selection answers the first prompt. Every involved solid is replaced by the result in one undo
// step; a pair the kernel refuses is reported and the document is untouched (REQ-201).
// -------------------------------------------------------------------------------------------------

namespace {

const char* BooleanVerb(CadBooleanOp op) {
  return op == CadBooleanOp::Union ? "UNION" : op == CadBooleanOp::Subtract ? "SUBTRACT" : "INTERSECT";
}

/// Solid indices in the current selection, de-duplicated, in selection order.
std::vector<int> SelectedSolidIndices(const AppCommandState& st) {
  const int n = static_cast<int>(st.drawing.cadSolids.size());
  std::vector<int> idx;
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Solid && e.index >= 0 && e.index < n &&
        std::find(idx.begin(), idx.end(), e.index) == idx.end())
      idx.push_back(e.index);
  }
  return idx;
}

std::vector<int> SelectedCircleIndices(const AppCommandState& st) {
  const int n = static_cast<int>(st.drawing.circleCount());
  std::vector<int> idx;
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Circle && e.index >= 0 && e.index < n &&
        std::find(idx.begin(), idx.end(), e.index) == idx.end())
      idx.push_back(e.index);
  }
  return idx;
}

// If s is a right circular cylinder (2 half-cylinder faces + 2 caps, as built by MakeCylinder/PRESSPULL),
// extract its axis midpoint, direction and radius. Used to turn a short PRESSPULL cylinder into a
// through-hole cutter when BooleanSubtract would otherwise refuse "only partly enters".
bool TryGetCylinderInfo(const brep::Solid& s, brep::Vec3* outCentre, brep::Vec3* outNormal, double* outRadius) {
  if (!outCentre || !outNormal || !outRadius) return false;
  if (s.faces.size() != 4 || s.vertices.size() != 4 || s.edges.size() != 6) return false;
  int cylIdx = -1;
  int cylCount = 0, planeCount = 0;
  for (int i = 0; i < 4; ++i) {
    auto k = s.faces[static_cast<size_t>(i)].surface.kind;
    if (k == brep::SurfaceKind::Cylinder) { ++cylCount; if (cylIdx < 0) cylIdx = i; }
    else if (k == brep::SurfaceKind::Plane) ++planeCount;
    else return false;
  }
  if (cylCount != 2 || planeCount != 2) return false;
  const auto& sf = s.faces[static_cast<size_t>(cylIdx)].surface;
  if (!(sf.radius > 0) || !(sf.height > 0)) return false;
  // Use the cylinder face's frame: origin is base centre, zAxis is axis direction.
  brep::Vec3 base = sf.frame.origin;
  brep::Vec3 dir = sf.frame.zAxis;
  double len = sf.height;
  double r = sf.radius;
  double dlen = ray3d::Length(dir);
  if (!(dlen > 1e-12)) return false;
  dir = ray3d::Scale(dir, 1.0/dlen);
  *outCentre = ray3d::Add(base, ray3d::Scale(dir, len*0.5));
  *outNormal = dir;
  *outRadius = r;
  return true;
}

/// One physical cylindrical segment of a coaxial stepped stack — \ref base is the cutter's base
/// point (matching \ref brep::SubtractCircle's own "base + depth along +normal" convention), not a
/// midpoint (issue #493 / REQ-337).
struct CylinderStackPiece {
  brep::Vec3 base;
  brep::Vec3 normal;  ///< Shared unit axis direction, same for every piece in a stack.
  double radius = 0.0;
  double axialLen = 0.0;
};

/// Recognise \p s as a stepped/shouldered coaxial cylinder stack — two or more cylinders sharing one
/// axis line, touching end-to-end with no gap or overlap (a shouldered shaft, a from-both-sides
/// counterbored pin) — the composite-operand case \ref TryGetCylinderInfo does not cover because it
/// only matches a single bare-cylinder primitive (issue #493 / REQ-337). On success, \p outPieces is
/// the stack in axis order, each ready for its own \ref brep::SubtractCircle call.
///
/// A physical cylindrical segment is usually TWO faces (MakeCylinder/PRESSPULL split a full turn
/// into two half-turn faces — see the `brep.cpp` comment on `kMinFullCircleSegments`), so cylinder
/// faces are grouped by matching (axial start, axial end, radius) into segments before the tiling
/// check; a segment built some other way, with more or fewer than two faces at the same span, groups
/// the same way. A single physical segment (one group) is a plain cylinder, not a stack — refused
/// here so the existing \ref TryGetCylinderInfo retry (the N==1 case) keeps owning it unchanged.
///
/// This is deliberately narrow: only Plane and Cylinder faces are tolerated (a Cone/Sphere/Torus/
/// Nurbs face anywhere refuses immediately), and only a coaxial run, not an off-axis or angled
/// composite cutter — the scope REQ-337 recorded, not a general classification engine.
bool TryDecomposeCoaxialCylinderStack(const brep::Solid& s, std::vector<CylinderStackPiece>* outPieces) {
  if (!outPieces) return false;
  constexpr double kAxisAngTol = 1e-6;   // 1 - |dot| tolerance for "parallel axis"
  constexpr double kAxisDistTol = 1e-6;  // ft: axis-line coincidence and end-to-end tiling tolerance

  struct RawSpan { double z0, z1, radius; };
  std::vector<RawSpan> raw;
  brep::Vec3 axisDir{}, axisPoint{};
  bool haveAxis = false;
  int planeCount = 0;

  for (const brep::Face& f : s.faces) {
    if (f.surface.kind == brep::SurfaceKind::Plane) {
      ++planeCount;
      continue;
    }
    if (f.surface.kind != brep::SurfaceKind::Cylinder)
      return false;  // a cone/sphere/torus/nurbs face — not this case
    brep::Vec3 dir = f.surface.frame.zAxis;
    const double dlen = ray3d::Length(dir);
    if (!(dlen > 1e-12))
      return false;
    dir = ray3d::Scale(dir, 1.0 / dlen);
    if (!haveAxis) {
      axisDir = dir;
      axisPoint = f.surface.frame.origin;
      haveAxis = true;
    } else {
      double d = ray3d::Dot(dir, axisDir);
      if (std::fabs(d) < 1.0 - kAxisAngTol)
        return false;  // not parallel to the running axis
      if (d < 0.0)
        dir = ray3d::Scale(dir, -1.0);
      const brep::Vec3 toP = ray3d::Sub(f.surface.frame.origin, axisPoint);
      const double along = ray3d::Dot(toP, axisDir);
      const brep::Vec3 perp = ray3d::Sub(toP, ray3d::Scale(axisDir, along));
      if (ray3d::Length(perp) > kAxisDistTol)
        return false;  // parallel but not the same line
    }
    if (!(f.surface.radius > 0.0) || !(f.surface.height > 0.0))
      return false;
    const double z0 = ray3d::Dot(ray3d::Sub(f.surface.frame.origin, axisPoint), axisDir);
    raw.push_back({z0, z0 + f.surface.height, f.surface.radius});
  }
  if (!haveAxis || raw.size() < 2 || planeCount < 2)
    return false;

  std::vector<RawSpan> segs;
  for (const RawSpan& r : raw) {
    bool merged = false;
    for (RawSpan& g : segs) {
      if (std::fabs(g.z0 - r.z0) < kAxisDistTol && std::fabs(g.z1 - r.z1) < kAxisDistTol &&
          std::fabs(g.radius - r.radius) < kAxisDistTol) {
        merged = true;
        break;
      }
    }
    if (!merged)
      segs.push_back(r);
  }
  if (segs.size() < 2)
    return false;  // one physical cylinder — TryGetCylinderInfo's case, not this one

  std::sort(segs.begin(), segs.end(), [](const RawSpan& a, const RawSpan& b) { return a.z0 < b.z0; });
  for (size_t i = 0; i + 1 < segs.size(); ++i) {
    if (std::fabs(segs[i].z1 - segs[i + 1].z0) > kAxisDistTol)
      return false;  // a gap or an overlap — not one contiguous stack
  }

  outPieces->clear();
  outPieces->reserve(segs.size());
  for (const RawSpan& g : segs) {
    CylinderStackPiece p;
    p.base = ray3d::Add(axisPoint, ray3d::Scale(axisDir, g.z0));
    p.normal = axisDir;
    p.radius = g.radius;
    p.axialLen = g.z1 - g.z0;
    outPieces->push_back(p);
  }
  return true;
}

bool ApplyOnePair(CadBooleanOp op, const brep::Solid& x, const brep::Solid& y,
                  std::vector<brep::Solid>* r, brep::Problem* w) {
  switch (op) {
  case CadBooleanOp::Union:
    return brep::BooleanUnion(x, y, r, w);
  case CadBooleanOp::Subtract:
    return brep::BooleanSubtract(x, y, r, w);
  case CadBooleanOp::Intersect:
    return brep::BooleanIntersect(x, y, r, w);
  }
  return false;
}

/// Fold \p solids left with \p op. UNION keeps disjoint pieces aside; the others must stay one solid.
/// Returns false (message pushed) on a kernel refusal.
bool FoldBoolean(CadBooleanOp op, std::vector<brep::Solid> solids, std::vector<brep::Solid>* out,
                 std::vector<std::string>& log) {
  const std::string verb = BooleanVerb(op);
  std::vector<brep::Solid> acc{std::move(solids[0])};
  brep::Problem why = brep::Problem::Ok;
  for (std::size_t k = 1; k < solids.size(); ++k) {
    if (op == CadBooleanOp::Union) {
      std::vector<brep::Solid> nextAcc;
      brep::Solid pending = std::move(solids[k]);
      bool merged = false;
      for (brep::Solid& piece : acc) {
        std::vector<brep::Solid> r;
        if (!ApplyOnePair(op, piece, pending, &r, &why)) {
          log.push_back(verb + " — " + brep::ProblemText(why) + " Nothing changed.");
          return false;
        }
        if (!merged && r.size() == 1) {
          pending = std::move(r[0]);
          merged = true;
        } else {
          nextAcc.push_back(std::move(piece));
        }
      }
      nextAcc.push_back(std::move(pending));
      acc = std::move(nextAcc);
    } else {
      std::vector<brep::Solid> r;
      if (!ApplyOnePair(op, acc[0], solids[k], &r, &why) || r.size() != 1) {
        log.push_back(verb + " — " + brep::ProblemText(why) + " Nothing changed.");
        return false;
      }
      acc = std::move(r);
    }
  }
  *out = std::move(acc);
  return true;
}

/// Combine the solids at \p minuend (UNION/INTERSECT: all operands; SUBTRACT: the "subtract from"
/// set) and \p subtrahend (SUBTRACT only) and replace every one of them with the result.
void CommitBoolean(AppCommandState& st, CadBooleanOp op, const std::vector<int>& minuend,
                   const std::vector<int>& subtrahend, std::vector<std::string>& log) {
  const std::string verb = BooleanVerb(op);
  std::vector<int> all = minuend;
  all.insert(all.end(), subtrahend.begin(), subtrahend.end());
  for (int i : all) {
    if (i < 0 || static_cast<size_t>(i) >= st.drawing.cadSolids.size() || !st.drawing.cadSolids[static_cast<size_t>(i)]) {
      log.push_back(verb + " — a selected solid is missing. Nothing changed.");
      return;
    }
  }

  auto grab = [&](const std::vector<int>& v) {
    std::vector<brep::Solid> s;
    s.reserve(v.size());
    for (int i : v)
      s.push_back(*st.drawing.cadSolids[static_cast<size_t>(i)]);
    return s;
  };

  std::vector<brep::Solid> result;
  if (op != CadBooleanOp::Subtract) {
    if (!FoldBoolean(op, grab(minuend), &result, log))
      return;
  } else {
    // Fold-union the minuend set, then subtract each subtrahend solid and each
    // selected circle (as a cylindrical through-hole, issue #486 follow-up) from every piece.
    if (!FoldBoolean(CadBooleanOp::Union, grab(minuend), &result, log))
      return;
    for (const brep::Solid& sub : grab(subtrahend)) {
      std::vector<brep::Solid> next;
      for (const brep::Solid& piece : result) {
        std::vector<brep::Solid> r;
        brep::Problem why = brep::Problem::Ok;
        if (!brep::BooleanSubtract(piece, sub, &r, &why)) {
          // If a short PRESSPULL cylinder only partly enters, retry as a through-hole
          // using its axis/radius with auto-depth (common UX: user pulls a small nub to
          // indicate a hole location, not the exact depth).
          brep::Vec3 cc, nn; double rr;
          if (why == brep::Problem::BooleanCurvedFace && TryGetCylinderInfo(sub, &cc, &nn, &rr)) {
            brep::Solid cut;
            brep::Problem why2 = brep::Problem::Ok;
            if (brep::SubtractCircleThrough(piece, cc, nn, rr, &cut, &why2)) {
              next.push_back(std::move(cut));
              continue;
            }
            why = why2;
          }
          // A stepped/shouldered coaxial cylinder stack (issue #493 / REQ-337) — built directly as
          // one shouldered bore, base to tip, rather than cut piece-by-piece: a sequential retry
          // (subtract each segment in turn into the solid the previous cut left behind) was tried
          // first and found unreliable — the general classifier every ordinary SUBTRACT retry above
          // goes through refuses once the target already carries an inward cylindrical face from a
          // prior cut nearby, which is exactly every internal step boundary. TrySteppedBoreThroughDirect
          // builds the whole tunnel's topology in one pass instead (only for a non-increasing radius
          // sequence — entry to exit no wider than the step before it, a real counterbore shape; a
          // widening stack still refuses here, by name, same as before this increment).
          std::vector<CylinderStackPiece> stack;
          if (why == brep::Problem::BooleanCurvedFace && TryDecomposeCoaxialCylinderStack(sub, &stack)) {
            std::vector<double> radii;
            std::vector<brep::Vec3> internalShoulders;
            radii.reserve(stack.size());
            internalShoulders.reserve(stack.size() > 0 ? stack.size() - 1 : 0);
            for (size_t i = 0; i < stack.size(); ++i) {
              radii.push_back(stack[i].radius);
              if (i > 0)
                internalShoulders.push_back(stack[i].base);
            }
            brep::Solid cut;
            brep::Problem why3 = brep::Problem::Ok;
            if (brep::TrySteppedBoreThroughDirect(piece, stack.front().base, stack.front().normal, radii,
                                                  internalShoulders, &cut, &why3)) {
              next.push_back(std::move(cut));
              continue;
            }
            why = why3;
          }
          log.push_back(verb + " — " + brep::ProblemText(why) + " Nothing changed.");
          return;
        }
        for (brep::Solid& x : r)
          next.push_back(std::move(x));
      }
      result = std::move(next);
    }
    // Circles as cylindrical cutters (centre+normal+radius, auto-depth through hole)
    const std::vector<int> circleCutters = SelectedCircleIndices(st);
    for (int ci : circleCutters) {
      if (ci < 0 || static_cast<size_t>(ci) >= st.drawing.circleCount()) continue;
      const CadCircleRecord cr = st.drawing.circle(static_cast<size_t>(ci));
      const double cx = cr.cx;
      const double cy = cr.cy;
      const double cz = cr.cz;
      const double r = cr.r;
      double nx = 0, ny = 0, nz = 1;
      const size_t ni = static_cast<size_t>(ci) * 3;
      if (ni + 2 < st.drawing.userCircleNormals.size()) {
        nx = st.drawing.userCircleNormals[ni + 0];
        ny = st.drawing.userCircleNormals[ni + 1];
        nz = st.drawing.userCircleNormals[ni + 2];
      }
      if (!(r > 0.0) || !std::isfinite(r)) {
        log.push_back(verb + " — circle " + std::to_string(ci) + " has invalid radius. Nothing changed.");
        return;
      }
      std::vector<brep::Solid> next;
      for (const brep::Solid& piece : result) {
        brep::Solid cut;
        brep::Problem why = brep::Problem::Ok;
        if (!brep::SubtractCircleThrough(piece, brep::Vec3{cx, cy, cz}, brep::Vec3{nx, ny, nz}, r, &cut, &why)) {
          log.push_back(verb + " — " + brep::ProblemText(why) + " Nothing changed.");
          return;
        }
        next.push_back(std::move(cut));
      }
      result = std::move(next);
    }
  }
  if (result.empty()) {
    log.push_back(verb + " — the result is empty. Nothing changed.");
    return;
  }

  const EntityAttributes attrs = static_cast<size_t>(minuend[0]) < st.drawing.cadSolidAttrs.size()
                                     ? st.drawing.cadSolidAttrs[static_cast<size_t>(minuend[0])]
                                     : MakeNewEntityAttrs(st);
  PushUndoSnapshot(st, verb);
  std::vector<int> sorted = all;
  std::sort(sorted.begin(), sorted.end(), std::greater<int>());
  for (int i : sorted) {
    st.drawing.cadSolids.erase(st.drawing.cadSolids.begin() + i);
    if (static_cast<size_t>(i) < st.drawing.cadSolidAttrs.size())
      st.drawing.cadSolidAttrs.erase(st.drawing.cadSolidAttrs.begin() + i);
  }
  for (brep::Solid& s : result) {
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
    st.drawing.cadSolidAttrs.push_back(attrs);
    log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));  // a feature result may be a Cylinder / Cone (#515)
  }
  BumpCadGpuCache(st);
  st.selection.clear();
  log.push_back(verb + " — " + std::to_string(all.size()) + " solid(s) combined into " +
                std::to_string(result.size()) + (result.size() == 1 ? " solid." : " solids."));
}

}  // namespace

void CadBooleanSelection(AppCommandState& st, CadBooleanOp op, std::vector<std::string>& log) {
  const std::vector<int> idx = SelectedSolidIndices(st);
  if (idx.size() < 2) {
    log.push_back(std::string(BooleanVerb(op)) + " — select at least two solids first.");
    return;
  }
  CommitBoolean(st, op, idx, {}, log);
}

void CancelBooleanCommand(AppCommandState& st) {
  if (auto* bs = st.sessionAs<stepengine::BooleanSession>()) {
    bs->phase = stepengine::BooleanPhase::SelectOperands;
    bs->minuend.clear();
  }
}

std::string CadBooleanPromptText(const AppCommandState& st) {
  const auto* bs = st.sessionAs<stepengine::BooleanSession>();
  if (!bs)
    return "BOOLEAN";
  switch (bs->phase) {
  case stepengine::BooleanPhase::SelectOperands:
    return std::string(BooleanVerb(static_cast<CadBooleanOp>(bs->op))) +
           " — select solids to combine, Enter when done. ESC cancels.";
  case stepengine::BooleanPhase::SelectMinuend:
    return "SUBTRACT — select solids to subtract FROM, Enter when done. ESC cancels.";
  case stepengine::BooleanPhase::SelectSubtrahend:
    return "SUBTRACT — select solids/circles to subtract, Enter when done. ESC cancels.";
  }
  return "BOOLEAN";
}

void StartBooleanCommand(AppCommandState& st, CadBooleanOp op, std::vector<std::string>& log) {
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;

  const std::vector<int> pre = SelectedSolidIndices(st);
  if (op != CadBooleanOp::Subtract && pre.size() >= 2) {
    CommitBoolean(st, op, pre, {}, log);
    return;
  }
  auto sess = std::make_unique<stepengine::BooleanSession>();
  sess->op = static_cast<int>(op);
  if (op == CadBooleanOp::Subtract) {
    if (!pre.empty()) {
      sess->minuend = pre;
      st.selection.clear();
      sess->phase = stepengine::BooleanPhase::SelectSubtrahend;
    } else {
      sess->phase = stepengine::BooleanPhase::SelectMinuend;
    }
  } else {
    sess->phase = stepengine::BooleanPhase::SelectOperands;
  }
  stepengine::BeginSession(st, std::move(sess));
  log.push_back(CadBooleanPromptText(st));
}

bool HandleBooleanTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* bs = st.sessionAs<stepengine::BooleanSession>();
  if (!bs)
    return false;
  if (!StringUtil::trimCopy(lineIn).empty())
    return false;
  const CadBooleanOp op = static_cast<CadBooleanOp>(bs->op);
  const std::vector<int> sel = SelectedSolidIndices(st);

  using BP = stepengine::BooleanPhase;
  if (bs->phase == BP::SelectMinuend) {
    if (sel.empty()) {
      log.push_back("SUBTRACT — nothing selected. Click a solid, or ESC.");
      return true;
    }
    bs->minuend = sel;
    st.selection.clear();
    bs->phase = BP::SelectSubtrahend;
    log.push_back(CadBooleanPromptText(st));
    return true;
  }
  if (bs->phase == BP::SelectSubtrahend) {
    const std::vector<int> selCircles = SelectedCircleIndices(st);
    if (sel.empty() && selCircles.empty()) {
      log.push_back("SUBTRACT — select the solids/circles to subtract, or ESC.");
      return true;
    }
    const std::vector<int> minuend = bs->minuend;
    CommitBoolean(st, CadBooleanOp::Subtract, minuend, sel, log);
    stepengine::EndSession(st);
    return true;
  }
  if (sel.size() < 2) {
    log.push_back(std::string(BooleanVerb(op)) + " — select at least two solids, or ESC.");
    return true;
  }
  CommitBoolean(st, op, sel, {}, log);
  stepengine::EndSession(st);
  return true;
}

void CancelSliceCommand(AppCommandState& st) {
  if (auto* ss = st.sessionAs<stepengine::SliceSession>()) {
    ss->phase = stepengine::SlicePhase::SelectSolids;
    ss->solidIndices.clear();
  }
}

std::string CadSlicePromptText(const AppCommandState& st) {
  const auto* ss = st.sessionAs<stepengine::SliceSession>();
  if (!ss)
    return "SLICE";
  switch (ss->phase) {
  case stepengine::SlicePhase::SelectSolids:
    return "SLICE — select solids, Enter when done. ESC cancels.";
  case stepengine::SlicePhase::WaitP1:
    return "SLICE — first point on the cutting plane. ESC cancels.";
  case stepengine::SlicePhase::WaitP2:
    return "SLICE — second point on the cutting plane. ESC cancels.";
  case stepengine::SlicePhase::WaitP3:
    return "SLICE — third point on the cutting plane. ESC cancels.";
  case stepengine::SlicePhase::WaitKeepSide:
    return "SLICE — pick a point on the side to keep, or type B for both. ESC cancels.";
  }
  return "SLICE";
}

static void SliceEnterPlanePhase(AppCommandState& st, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SliceSession>();
  if (!ss)
    return;
  ss->solidIndices.clear();
  const int nSolid = static_cast<int>(st.drawing.cadSolids.size());
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Solid && e.index >= 0 && e.index < nSolid)
      ss->solidIndices.push_back(e.index);
  }
  std::sort(ss->solidIndices.begin(), ss->solidIndices.end());
  ss->solidIndices.erase(std::unique(ss->solidIndices.begin(), ss->solidIndices.end()),
                         ss->solidIndices.end());
  if (ss->solidIndices.empty()) {
    log.push_back("SLICE — no solids selected. Click a solid, or ESC.");
    ss->phase = stepengine::SlicePhase::SelectSolids;
    return;
  }
  ss->phase = stepengine::SlicePhase::WaitP1;
  log.push_back(CadSlicePromptText(st));
}

void StartSliceCommand(AppCommandState& st, std::vector<std::string>& log) {
  auto sess = std::make_unique<stepengine::SliceSession>();
  auto* ss = sess.get();
  stepengine::BeginSession(st, std::move(sess));
  if (!st.selection.empty()) {
    SliceEnterPlanePhase(st, log);
    if (ss->phase != stepengine::SlicePhase::SelectSolids)
      return;
    st.selection.clear();
  }
  ss->phase = stepengine::SlicePhase::SelectSolids;
  log.push_back(CadSlicePromptText(st));
}

static void CommitSlice(AppCommandState& st, brep::SliceKeep keep, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SliceSession>();
  if (!ss)
    return;
  const ray3d::Vec3 n = ray3d::Cross(ray3d::Sub(ss->p2, ss->p1), ray3d::Sub(ss->p3, ss->p1));
  if (!(ray3d::Length(n) > 1e-9)) {
    log.push_back("SLICE — the three points are in a line; they do not define a plane.");
    return;
  }
  struct Result {
    int index = 0;
    std::vector<brep::Solid> pieces;
  };
  std::vector<Result> results;
  for (int idx : ss->solidIndices) {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.cadSolids.size() || !st.drawing.cadSolids[static_cast<size_t>(idx)])
      continue;
    brep::Solid above;
    brep::Solid below;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::Slice(*st.drawing.cadSolids[static_cast<size_t>(idx)], ss->p1, n, keep, &above, &below, &why)) {
      log.push_back(std::string("SLICE — ") + brep::ProblemText(why) + " Nothing changed.");
      return;
    }
    Result r;
    r.index = idx;
    if ((keep == brep::SliceKeep::Above || keep == brep::SliceKeep::Both) && !above.faces.empty())
      r.pieces.push_back(std::move(above));
    if ((keep == brep::SliceKeep::Below || keep == brep::SliceKeep::Both) && !below.faces.empty())
      r.pieces.push_back(std::move(below));
    results.push_back(std::move(r));
  }
  if (results.empty()) {
    log.push_back("SLICE — nothing to slice.");
    return;
  }

  PushUndoSnapshot(st, "Slice");
  std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) { return a.index > b.index; });
  int made = 0;
  for (Result& r : results) {
    EntityAttributes attrs = static_cast<size_t>(r.index) < st.drawing.cadSolidAttrs.size()
                                 ? st.drawing.cadSolidAttrs[static_cast<size_t>(r.index)]
                                 : MakeNewEntityAttrs(st);
    st.drawing.cadSolids.erase(st.drawing.cadSolids.begin() + r.index);
    if (static_cast<size_t>(r.index) < st.drawing.cadSolidAttrs.size())
      st.drawing.cadSolidAttrs.erase(st.drawing.cadSolidAttrs.begin() + r.index);
    for (brep::Solid& piece : r.pieces) {
      st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(piece)));
      st.drawing.cadSolidAttrs.push_back(attrs);
      ++made;
    }
  }
  BumpCadGpuCache(st);
  st.selection.clear();
  log.push_back("SLICE — " + std::to_string(results.size()) + " solid(s) cut into " +
                std::to_string(made) + " piece(s).");
  stepengine::EndSession(st);
}

// -------------------------------------------------------------------------------------------
// PRESSPULL (REQ-319 / D-2026-09-04-c, widened by GitHub issue #396) — move the selected FACE
// along its own normal, or turn a selected closed polyline/circle into (or out of) a solid the
// same way EXTRUDE does. Brought in line with EXTRUDE's own select-target / wait-distance shape:
// a bare PRESSPULL asks for a target (unless one is already named), then a distance — typed, or
// dragged from the cursor with a live ghost.
// -------------------------------------------------------------------------------------------

namespace {

enum class PressPullTargetKind : std::uint8_t {
  None,     ///< Nothing eligible is selected.
  TooMany,  ///< More than one eligible face or shape is selected.
  Face,     ///< Exactly one solid FACE, from the REQ-318 sub-object selection.
  Profile,  ///< Exactly one closed polyline/circle, from the ordinary entity selection.
};

struct PressPullTarget {
  PressPullTargetKind kind = PressPullTargetKind::None;
  SelectedSubObject face;
  brep::Profile profile;
};

/// What PRESSPULL would act on right now. The two selections are mutually exclusive
/// (D-2026-09-04-a), so a face selection is checked first and, when it names anything at all
/// (even an ineligible edge/vertex), the entity selection is not consulted — the same priority
/// the original face-only command gave its two refusal messages.
PressPullTarget GatherPressPullTarget(AppCommandState& st) {
  // Expiry is swept once a frame in the main loop, but this can run from the command line and
  // must not assume that has happened since the last edit — a PRESSPULL immediately after an
  // undo would otherwise read a reference to a solid that is gone.
  ExpireSubObjectSelection(st);

  if (!st.subObjectSelection.empty()) {
    std::vector<const SelectedSubObject*> faces;
    for (const SelectedSubObject& s : st.subObjectSelection)
      if (s.kind == solidpick::Kind::Face)
        faces.push_back(&s);
    if (faces.size() > 1)
      return {PressPullTargetKind::TooMany, {}, {}};
    if (faces.size() == 1)
      return {PressPullTargetKind::Face, *faces.front(), {}};
    return {PressPullTargetKind::None, {}, {}};  // an edge/vertex selection: no face in it
  }

  std::vector<brep::Profile> profiles;
  int skipped = 0;
  GatherExtrudeProfiles(st, &profiles, &skipped);
  if (profiles.size() > 1)
    return {PressPullTargetKind::TooMany, {}, {}};
  if (profiles.size() == 1)
    return {PressPullTargetKind::Profile, {}, profiles.front()};
  return {PressPullTargetKind::None, {}, {}};
}

/// Build (face mode) or create (profile mode) the solid PRESSPULL describes at \p distance, and
/// apply it: one undo step, GPU cache bump, and the kernel's own sentence on a refusal with the
/// document left untouched. Shared by the one-line shortcut and the prompted command's commit, so
/// the two cannot diverge (the same rule \ref CadApplyPushPull states for the grip drag).
bool CadCommitPressPullTarget(AppCommandState& st, const PressPullTarget& t, double distance,
                              std::vector<std::string>& log) {
  if (t.kind == PressPullTargetKind::Face)
    return CadApplyPushPull(st, t.face, distance, log);

  brep::Solid built;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::Extrude(t.profile, distance, &built, &why)) {
    log.push_back(std::string("PRESSPULL - ") + brep::ProblemText(why));
    return false;
  }
  PushUndoSnapshot(st, "PressPull");
  const brep::MassProperties mp = brep::ComputeMassProperties(built);
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(built)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
  log.push_back(SolidCreatedMessage(st.drawing.cadSolids.back()->recipe.kind, mp));  // a feature result may be a Cylinder / Cone (#515)
  BumpCadGpuCache(st);
  return true;
}

}  // namespace

void CadPressPull(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const PressPullTarget t = GatherPressPullTarget(st);
  if (t.kind == PressPullTargetKind::TooMany) {
    // Refused rather than applied to all of them. Two candidates moved at once means the second
    // is computed against the first's result while the user was picturing the original - a
    // compound edit nobody asked for. One target, one move (REQ-201).
    log.push_back("PRESSPULL - more than one candidate is selected; this moves one at a time.");
    return;
  }
  if (t.kind == PressPullTargetKind::None) {
    log.push_back(st.subObjectSelection.empty() && st.selection.empty()
                      ? "PRESSPULL - select a solid FACE first: hold Ctrl and click one, or "
                        "select a closed polyline or circle."
                      : "PRESSPULL - the selection has no face in it. Ctrl+click a face, not an "
                        "edge or a vertex, or select a closed polyline/circle instead.");
    return;
  }

  const std::string text = StringUtil::trimCopy(args);
  if (text.empty()) {
    log.push_back("Usage: PRESSPULL <distance> - positive moves outward, negative inward.");
    return;
  }
  char* end = nullptr;
  const double distance = std::strtod(text.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(distance)) {
    log.push_back("PRESSPULL - \"" + text + "\" is not a number.");
    return;
  }

  if (CadCommitPressPullTarget(st, t, distance, log) && t.kind == PressPullTargetKind::Profile)
    st.selection.clear();
}

// -------------------------------------------------------------------------------------------------
// The prompted PRESSPULL command (GitHub issue #396). A bare PRESSPULL asks for a target (unless one
// is already named by a Ctrl+clicked face or a selected closed shape), then a distance — typed, or
// dragged from the cursor with a live ghost. The ghost and the commit both go through
// CadBuildPressPullSolid, so the ghost cannot show a shape the click would not build — the same
// one-source-of-truth rule EXTRUDE follows.
// -------------------------------------------------------------------------------------------------

void CancelPressPullCommand(AppCommandState& st) {
  if (auto* pp = st.sessionAs<stepengine::PressPullSession>()) {
    pp->phase = stepengine::PressPullPhase::SelectTarget;
    pp->onFace = false;
    pp->face = SelectedSubObject{};
    pp->profile = brep::Profile{};
    pp->distPickValid = false;
    pp->distPick = 0.0;
  }
}

std::string CadPressPullPromptText(const AppCommandState& st) {
  const auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp || pp->phase == stepengine::PressPullPhase::SelectTarget) {
    return "PRESSPULL — Ctrl+click a solid face, or select a closed polyline or circle, Enter when "
           "done. ESC cancels.";
  }
  std::string s = "PRESSPULL — specify distance";
  if (pp->distPickValid) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), " <%.4g>", pp->distPick);
    s += buf;
  }
  s += ": type a value or click. ESC cancels.";
  return s;
}

static void PressPullEnterDistancePhase(AppCommandState& st, std::vector<std::string>& log) {
  auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp)
    return;
  const PressPullTarget t = GatherPressPullTarget(st);
  if (t.kind == PressPullTargetKind::TooMany) {
    log.push_back("PRESSPULL - more than one candidate is selected; this moves one at a time.");
    pp->phase = stepengine::PressPullPhase::SelectTarget;
    return;
  }
  if (t.kind == PressPullTargetKind::None) {
    log.push_back("PRESSPULL - nothing selected can be pushed or pulled (need a solid face, a "
                  "closed polyline, or a circle).");
    pp->phase = stepengine::PressPullPhase::SelectTarget;
    return;
  }
  pp->onFace = (t.kind == PressPullTargetKind::Face);
  if (pp->onFace)
    pp->face = t.face;
  else
    pp->profile = t.profile;
  pp->phase = stepengine::PressPullPhase::WaitDistance;
  pp->distPickValid = false;
  log.push_back(CadPressPullPromptText(st));
}

void StartPressPullCommand(AppCommandState& st, std::vector<std::string>& log) {
  auto sess = std::make_unique<stepengine::PressPullSession>();
  auto* pp = sess.get();
  stepengine::BeginSession(st, std::move(sess));

  if (!st.subObjectSelection.empty() || !st.selection.empty()) {
    PressPullEnterDistancePhase(st, log);
    if (pp->phase == stepengine::PressPullPhase::WaitDistance)
      return;
    st.selection.clear();
  }
  pp->phase = stepengine::PressPullPhase::SelectTarget;
  log.push_back(CadPressPullPromptText(st));
}

bool CadBuildPressPullSolid(const AppCommandState& st, double distance, brep::Solid* out) {
  const auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp || !out || !std::isfinite(distance) || distance == 0.0)
    return false;
  if (pp->onFace) {
    const CadSolidPtr sp = pp->face.owner.lock();
    if (!sp || pp->face.solidIndex < 0 ||
        static_cast<size_t>(pp->face.solidIndex) >= st.drawing.cadSolids.size() ||
        st.drawing.cadSolids[static_cast<size_t>(pp->face.solidIndex)] != sp)
      return false;
    brep::Problem why = brep::Problem::Ok;
    return brep::PushPullFace(*sp, pp->face.index, distance, out, &why);
  }
  brep::Problem why = brep::Problem::Ok;
  return brep::Extrude(pp->profile, distance, out, &why);
}

static void CommitPressPull(AppCommandState& st, double distance, std::vector<std::string>& log) {
  auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp)
    return;
  PressPullTarget t;
  t.kind = pp->onFace ? PressPullTargetKind::Face : PressPullTargetKind::Profile;
  t.face = pp->face;
  t.profile = pp->profile;
  if (!CadCommitPressPullTarget(st, t, distance, log))
    return;
  if (!pp->onFace)
    st.selection.clear();
  stepengine::EndSession(st);
}

bool HandlePressPullTextInput(const std::string& lineIn, AppCommandState& st,
                              std::vector<std::string>& log) {
  auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (pp->phase == stepengine::PressPullPhase::SelectTarget) {
    if (!line.empty())
      return false;
    if (st.subObjectSelection.empty() && st.selection.empty()) {
      log.push_back("PRESSPULL — nothing selected. Ctrl+click a face, or select a closed "
                    "polyline/circle, or ESC.");
      return true;
    }
    PressPullEnterDistancePhase(st, log);
    return true;
  }

  if (line.empty()) {
    if (pp->distPickValid) {
      CommitPressPull(st, pp->distPick, log);
    } else {
      log.push_back("PRESSPULL — type a distance, or move the cursor to a view where one can be read.");
    }
    return true;
  }
  char* end = nullptr;
  const double d = std::strtod(line.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(d) || d == 0.0) {
    log.push_back("PRESSPULL — \"" + line + "\" is not a distance. Type a non-zero number, or ESC.");
    return true;
  }
  const double signedD = pp->distPickValid ? std::copysign(d, pp->distPick) : d;
  CommitPressPull(st, signedD, log);
  return true;
}

void SubmitPressPullViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  (void)wx;
  (void)wy;
  auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp)
    return;
  if (pp->phase == stepengine::PressPullPhase::SelectTarget)
    return;
  if (!pp->distPickValid) {
    log.push_back("PRESSPULL — no distance under the cursor here; type a value, or orbit the view.");
    return;
  }
  CommitPressPull(st, pp->distPick, log);
}

void CadResolvePressPullPick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray) {
  auto* pp = st.sessionAs<stepengine::PressPullSession>();
  if (!pp)
    return;
  pp->distPickValid = false;
  if (pp->phase != stepengine::PressPullPhase::WaitDistance)
    return;

  (void)cursorOnPlane;
  if (!ray || !ray->valid())
    return;

  ray3d::Vec3 anchor{};
  ray3d::Vec3 axis{};
  if (pp->onFace) {
    if (!CadSubObjectFaceGrip(st, pp->face, &anchor, &axis))
      return;
  } else {
    anchor = pp->profile.plane.origin;
    axis = pp->profile.plane.zAxis;
  }
  double d = 0.0;
  if (!CadAxisDragParam(anchor, axis, *ray, &d))
    return;
  if (!std::isfinite(d) || std::fabs(d) <= 1e-9)
    return;
  pp->distPick = d;
  pp->distPickValid = true;
}

bool CadApplyPushPull(AppCommandState& st, const SelectedSubObject& ref, double distance,
                      std::vector<std::string>& log) {
  const CadSolidPtr sp = ref.owner.lock();
  if (ref.kind != solidpick::Kind::Face || !sp || ref.solidIndex < 0 ||
      static_cast<size_t>(ref.solidIndex) >= st.drawing.cadSolids.size() ||
      st.drawing.cadSolids[static_cast<size_t>(ref.solidIndex)] != sp) {
    log.push_back("PRESSPULL - that face is no longer there.");
    return false;
  }

  brep::Solid moved;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::PushPullFace(*sp, ref.index, distance, &moved, &why)) {
    // ADR-046 (d) and REQ-201: the kernel's own sentence, and the document is untouched. Nothing
    // above this line has modified anything, which is what makes that true rather than restored.
    log.push_back(std::string("PRESSPULL - ") + brep::ProblemText(why));
    return false;
  }

  // One undo step for the whole edit (REQ-319 item 10) - the geometry, the dropped recipe and the
  // re-tessellation that follows from BumpCadGpuCache.
  PushUndoSnapshot(st, "PressPull");
  const auto replaced = std::make_shared<const brep::Solid>(std::move(moved));
  st.drawing.cadSolids[static_cast<size_t>(ref.solidIndex)] = replaced;

  // The selection FOLLOWS the edit. Push/pull preserves topology, so the face index still names the
  // same face - but the reference is keyed on the solid's IDENTITY (ADR-049), and the solid has
  // just been replaced by a different object. Left alone it would expire on the next sweep and the
  // user would lose the selection after every push, making a second push impossible without
  // re-picking. Re-pointing it at the new solid is the whole reason REQ-319 item 8 states that the
  // topology is preserved.
  for (SelectedSubObject& s : st.subObjectSelection)
    if (s.solidIndex == ref.solidIndex)
      s.owner = replaced;

  BumpCadGpuCache(st);
  const brep::MassProperties mp = brep::ComputeMassProperties(*replaced);
  char msg[240];
  if (mp.valid)
    std::snprintf(msg, sizeof(msg), "PRESSPULL - face %d of solid %d moved %.4f; volume now %.4f.",
                  ref.index, ref.solidIndex + 1, distance, mp.volume);
  else
    std::snprintf(msg, sizeof(msg), "PRESSPULL - face %d of solid %d moved %.4f.", ref.index,
                  ref.solidIndex + 1, distance);
  log.push_back(msg);
  return true;
}

// --- Moving a VERTEX or an EDGE (REQ-333 increment 2, TASK-234; issue #148 acceptance 3) ---------
//
// Both are `CadApplyPushPull` with a different kernel call in the middle, deliberately: the solid is
// replaced, every sub-object reference that named it is re-pointed, the kernel's own sentence is
// logged on a refusal, and the whole thing is one undo step. Those four are properties of editing a
// solid rather than of pushing a face, and stating them three times in three shapes is how they
// start to differ.

namespace {

/// The half of `CadApplyPushPull` that is not about pushing: resolve the reference against the live
/// document, replace the solid, re-point the selection, and report.
bool CadCommitSolidEdit(AppCommandState& st, const SelectedSubObject& ref, brep::Solid&& edited,
                        const char* verb, const char* undoLabel, const char* whatMoved,
                        std::vector<std::string>& log) {
  PushUndoSnapshot(st, undoLabel);
  const auto replaced = std::make_shared<const brep::Solid>(std::move(edited));
  st.drawing.cadSolids[static_cast<size_t>(ref.solidIndex)] = replaced;
  // The selection FOLLOWS the edit, exactly as it does after a push. These operations preserve the
  // topology counts, so the index still names the same sub-object — but the reference is keyed on
  // the solid's IDENTITY (ADR-049) and the solid has just been replaced, so left alone it would
  // expire on the next sweep and a second drag would need a re-pick.
  for (SelectedSubObject& s : st.subObjectSelection)
    if (s.solidIndex == ref.solidIndex)
      s.owner = replaced;
  BumpCadGpuCache(st);
  const brep::MassProperties mp = brep::ComputeMassProperties(*replaced);
  char msg[240];
  if (mp.valid)
    std::snprintf(msg, sizeof(msg), "%s - %s %d of solid %d moved; volume now %.4f.", verb,
                  whatMoved, ref.index, ref.solidIndex + 1, mp.volume);
  else
    std::snprintf(msg, sizeof(msg), "%s - %s %d of solid %d moved.", verb, whatMoved, ref.index,
                  ref.solidIndex + 1);
  log.push_back(msg);
  return true;
}

/// The live solid behind \p ref, or null when the reference no longer names one.
CadSolidPtr CadLiveSolidFor(const AppCommandState& st, const SelectedSubObject& ref,
                            solidpick::Kind want) {
  CadSolidPtr sp = ref.owner.lock();
  if (ref.kind != want || !sp || ref.index < 0 || ref.solidIndex < 0 ||
      static_cast<size_t>(ref.solidIndex) >= st.drawing.cadSolids.size() ||
      st.drawing.cadSolids[static_cast<size_t>(ref.solidIndex)] != sp)
    return nullptr;
  return sp;
}

}  // namespace

bool CadApplyMoveVertex(AppCommandState& st, const SelectedSubObject& ref, const ray3d::Vec3& delta,
                        std::vector<std::string>& log) {
  const CadSolidPtr sp = CadLiveSolidFor(st, ref, solidpick::Kind::Vertex);
  if (!sp) {
    log.push_back("MOVE - that vertex is no longer there.");
    return false;
  }
  brep::Solid edited;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::MoveVertex(*sp, ref.index, delta, &edited, &why)) {
    log.push_back(std::string("MOVE - ") + brep::ProblemText(why));
    return false;
  }
  return CadCommitSolidEdit(st, ref, std::move(edited), "MOVE", "Move vertex", "vertex", log);
}

bool CadApplyMoveEdge(AppCommandState& st, const SelectedSubObject& ref, const ray3d::Vec3& delta,
                      std::vector<std::string>& log) {
  const CadSolidPtr sp = CadLiveSolidFor(st, ref, solidpick::Kind::Edge);
  if (!sp) {
    log.push_back("MOVE - that edge is no longer there.");
    return false;
  }
  brep::Solid edited;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::MoveEdge(*sp, ref.index, delta, &edited, &why)) {
    log.push_back(std::string("MOVE - ") + brep::ProblemText(why));
    return false;
  }
  return CadCommitSolidEdit(st, ref, std::move(edited), "MOVE", "Move edge", "edge", log);
}

// --- FILLET on a solid EDGE (REQ-323 increment 1, command half; issue #148 acceptance 5) ---------
//
// Paired with the REQ-318 sub-object selection exactly as PRESSPULL is: Ctrl+click names the edge,
// FILLET rounds it. There is no "select edges" step of its own, for the reason D-2026-09-04-a gave
// for the pick — a persistent mode is one a user can be left in without noticing.

bool CadSubObjectSelectionIsAllEdges(const AppCommandState& st) {
  if (st.subObjectSelection.empty())
    return false;
  for (const SelectedSubObject& s : st.subObjectSelection)
    if (s.kind != solidpick::Kind::Edge)
      return false;
  return true;
}

bool CadApplyFilletToSelectedEdges(AppCommandState& st, double radius,
                                   std::vector<std::string>& log) {
  if (!CadSubObjectSelectionIsAllEdges(st)) {
    log.push_back("FILLET - select solid EDGES first: hold Ctrl and click one.");
    return false;
  }
  // One solid at a time. Two solids filleted from one line would be two edits under one undo step,
  // and the second's radius would be checked against the first's result - a compound edit nobody
  // asked for, which is the same reason PRESSPULL refuses more than one target (REQ-201).
  const int solidIndex = st.subObjectSelection.front().solidIndex;
  for (const SelectedSubObject& s : st.subObjectSelection)
    if (s.solidIndex != solidIndex) {
      log.push_back("FILLET - the selected edges are on different solids; this rounds one solid at "
                    "a time.");
      return false;
    }
  const CadSolidPtr sp = st.subObjectSelection.front().owner.lock();
  if (!sp || solidIndex < 0 || static_cast<size_t>(solidIndex) >= st.drawing.cadSolids.size() ||
      st.drawing.cadSolids[static_cast<size_t>(solidIndex)] != sp) {
    log.push_back("FILLET - that edge is no longer there.");
    return false;
  }

  std::vector<int> edges;
  edges.reserve(st.subObjectSelection.size());
  for (const SelectedSubObject& s : st.subObjectSelection)
    edges.push_back(s.index);

  brep::Solid rounded;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::FilletEdges(*sp, edges, radius, &rounded, &why)) {
    // The kernel's own sentence, and the document untouched - ADR-046 (d) and REQ-201. Every fillet
    // refusal is a PRE-check, so "untouched" is true because nothing was built, not because
    // something was rolled back.
    log.push_back(std::string("FILLET - ") + brep::ProblemText(why));
    return false;
  }

  // One undo step for the whole edit, however many edges were named (REQ-323 item 8).
  PushUndoSnapshot(st, "Fillet");
  const auto replaced = std::make_shared<const brep::Solid>(std::move(rounded));
  st.drawing.cadSolids[static_cast<size_t>(solidIndex)] = replaced;

  // **The sub-object selection is CLEARED, and this is where fillet differs from push/pull.**
  // `CadApplyPushPull` re-points its references at the replaced solid because a push preserves the
  // topology, so the face index still names the same face. A fillet does not: the edge that was
  // selected no longer exists, and every index after it has shifted (the kernel compacts). Keeping
  // the reference would leave it pointing at whatever edge inherited the number - a selection that
  // looks live and names something the user never picked. ADR-049's expiring reference is the
  // precedent.
  st.subObjectSelection.clear();

  BumpCadGpuCache(st);
  const brep::MassProperties mp = brep::ComputeMassProperties(*replaced);
  char msg[256];
  if (mp.valid)
    std::snprintf(msg, sizeof(msg),
                  "FILLET - %d edge(s) of solid %d rounded at radius %.4f; volume now %.4f.",
                  static_cast<int>(edges.size()), solidIndex + 1, radius, mp.volume);
  else
    std::snprintf(msg, sizeof(msg), "FILLET - %d edge(s) of solid %d rounded at radius %.4f.",
                  static_cast<int>(edges.size()), solidIndex + 1, radius);
  log.push_back(msg);
  return true;
}


void CadFilletReportEdgeSelection(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Fillet)
    return;
  if (!CadSubObjectSelectionIsAllEdges(st)) {
    // Ctrl+clicking a FACE or a VERTEX mid-FILLET is not an error, but it is not something this
    // command can round either - say so rather than leaving the prompt looking ready.
    if (!st.subObjectSelection.empty())
      log.push_back("FILLET - that is not an edge. Ctrl+click a solid EDGE, or ESC to cancel.");
    return;
  }
  char buf[192];
  std::snprintf(buf, sizeof(buf),
                "FILLET - %d solid edge(s) selected. Ctrl+click more, or type a radius <%.4f> and "
                "Enter to round them.",
                static_cast<int>(st.subObjectSelection.size()),
                static_cast<double>(st.filletRadius));
  log.push_back(buf);
}
void CadFilletSolidEdges(AppCommandState& st, const std::string& args,
                         std::vector<std::string>& log) {
  if (!CadSubObjectSelectionIsAllEdges(st)) {
    log.push_back("FILLET - select solid EDGES first: hold Ctrl and click one.");
    return;
  }
  const std::string text = StringUtil::trimCopy(args);

  // A BARE `FILLET` asks for the radius rather than printing usage and stopping.
  //
  // Printing usage was a dead end, and a user found it: the command entered no state, so the next
  // keystroke went to the IDLE command line - where `R`, the 2D fillet's Radius option and the
  // natural thing to press, matched the `RECT` command and started drawing a rectangle. The
  // prompted form is what PRESSPULL already grew for the same reason (issue #396).
  if (text.empty()) {
    stepengine::BeginSession(st, std::make_unique<stepengine::FilletSession>(true), true);
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "FILLET - %d edge(s) selected. Specify fillet radius <%.4f>, Enter to accept, "
                  "ESC to cancel:",
                  static_cast<int>(st.subObjectSelection.size()),
                  static_cast<double>(st.filletRadius));
    log.push_back(buf);
    return;
  }

  char* end = nullptr;
  const double radius = std::strtod(text.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(radius)) {
    log.push_back("FILLET - \"" + text + "\" is not a number.");
    return;
  }
  if (CadApplyFilletToSelectedEdges(st, radius, log))
    st.filletRadius = static_cast<float>(radius);  // remembered, as the 2D fillet's radius is
}

// --- CHAMFER on a solid EDGE (REQ-331, command half; issue #148 acceptance 5) ---------------------
//
// Deliberately the fillet's shape, function for function, because they are the same command with a
// different surface behind them: Ctrl+click names the edges, the verb bevels them, one undo step,
// the selection cleared afterwards because the topology changed. Every comment the fillet's half
// earned applies here unchanged, so this half states only what DIFFERS - which is nothing about the
// interaction and everything about the kernel it calls.

bool CadApplyChamferToSelectedEdges(AppCommandState& st, double distance,
                                    std::vector<std::string>& log) {
  if (!CadSubObjectSelectionIsAllEdges(st)) {
    log.push_back("CHAMFER - select solid EDGES first: hold Ctrl and click one.");
    return false;
  }
  // One solid at a time, for the reason `CadApplyFilletToSelectedEdges` records: two solids from one
  // line would be two edits under one undo step.
  const int solidIndex = st.subObjectSelection.front().solidIndex;
  for (const SelectedSubObject& s : st.subObjectSelection)
    if (s.solidIndex != solidIndex) {
      log.push_back("CHAMFER - the selected edges are on different solids; this bevels one solid at "
                    "a time.");
      return false;
    }
  const CadSolidPtr sp = st.subObjectSelection.front().owner.lock();
  if (!sp || solidIndex < 0 || static_cast<size_t>(solidIndex) >= st.drawing.cadSolids.size() ||
      st.drawing.cadSolids[static_cast<size_t>(solidIndex)] != sp) {
    log.push_back("CHAMFER - that edge is no longer there.");
    return false;
  }

  std::vector<int> edges;
  edges.reserve(st.subObjectSelection.size());
  for (const SelectedSubObject& s : st.subObjectSelection)
    edges.push_back(s.index);

  brep::Solid bevelled;
  brep::Problem why = brep::Problem::Ok;
  if (!brep::ChamferEdges(*sp, edges, distance, &bevelled, &why)) {
    log.push_back(std::string("CHAMFER - ") + brep::ProblemText(why));
    return false;
  }

  // One undo step for the whole edit, however many edges were named (REQ-331 item 11).
  PushUndoSnapshot(st, "Chamfer");
  const auto replaced = std::make_shared<const brep::Solid>(std::move(bevelled));
  st.drawing.cadSolids[static_cast<size_t>(solidIndex)] = replaced;
  // Cleared for the fillet's reason: a chamfer changes the TOPOLOGY, so the selected edge is gone
  // and every index after it has shifted (the kernel compacts). ADR-049's expiring reference.
  st.subObjectSelection.clear();

  BumpCadGpuCache(st);
  const brep::MassProperties mp = brep::ComputeMassProperties(*replaced);
  char msg[256];
  if (mp.valid)
    std::snprintf(msg, sizeof(msg),
                  "CHAMFER - %d edge(s) of solid %d bevelled at distance %.4f; volume now %.4f.",
                  static_cast<int>(edges.size()), solidIndex + 1, distance, mp.volume);
  else
    std::snprintf(msg, sizeof(msg), "CHAMFER - %d edge(s) of solid %d bevelled at distance %.4f.",
                  static_cast<int>(edges.size()), solidIndex + 1, distance);
  log.push_back(msg);
  return true;
}

void CadChamferReportEdgeSelection(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Chamfer)
    return;
  if (!CadSubObjectSelectionIsAllEdges(st)) {
    if (!st.subObjectSelection.empty())
      log.push_back("CHAMFER - that is not an edge. Ctrl+click a solid EDGE, or ESC to cancel.");
    return;
  }
  char buf[200];
  std::snprintf(buf, sizeof(buf),
                "CHAMFER - %d solid edge(s) selected. Ctrl+click more, or type a distance <%.4f> "
                "and Enter to bevel them.",
                static_cast<int>(st.subObjectSelection.size()),
                static_cast<double>(st.chamferDist1));
  log.push_back(buf);
}

void CadChamferSolidEdges(AppCommandState& st, const std::string& args,
                          std::vector<std::string>& log) {
  if (!CadSubObjectSelectionIsAllEdges(st)) {
    log.push_back("CHAMFER - select solid EDGES first: hold Ctrl and click one.");
    return;
  }
  const std::string text = StringUtil::trimCopy(args);

  // A bare `CHAMFER` prompts rather than printing usage — the trap TASK-219 found for FILLET, where
  // entering no command state sent the next keystroke to the idle command line.
  if (text.empty()) {
    stepengine::BeginSession(st, std::make_unique<stepengine::ChamferSession>(true), true);
    char buf[176];
    std::snprintf(buf, sizeof(buf),
                  "CHAMFER - %d edge(s) selected. Specify chamfer distance <%.4f>, Enter to accept, "
                  "ESC to cancel:",
                  static_cast<int>(st.subObjectSelection.size()),
                  static_cast<double>(st.chamferDist1));
    log.push_back(buf);
    return;
  }

  char* end = nullptr;
  const double distance = std::strtod(text.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(distance)) {
    log.push_back("CHAMFER - \"" + text + "\" is not a number.");
    return;
  }
  if (CadApplyChamferToSelectedEdges(st, distance, log))
    st.chamferDist1 = static_cast<float>(distance);  // remembered, as the 2D chamfer's distance is
}

bool CadSubObjectFaceGrip(const AppCommandState& st, const SelectedSubObject& ref,
                          ray3d::Vec3* outAnchor, ray3d::Vec3* outAxis) {
  if (!outAnchor || !outAxis || ref.kind != solidpick::Kind::Face || ref.index < 0)
    return false;
  const CadSolidPtr sp = ref.owner.lock();
  if (!sp || ref.solidIndex < 0 || static_cast<size_t>(ref.solidIndex) >= st.drawing.cadSolids.size() ||
      st.drawing.cadSolids[static_cast<size_t>(ref.solidIndex)] != sp)
    return false;
  if (static_cast<size_t>(ref.index) >= sp->faces.size())
    return false;
  const brep::Face& f = sp->faces[static_cast<size_t>(ref.index)];

  // A CYLINDER WALL gets a handle too, and it slides RADIALLY (REQ-319 increment 4). The handle sits
  // on the surface at the middle of the face's own angular span and half way up, and its axis is the
  // outward normal AT THAT POINT — which is what the drag distance means, since the push moves every
  // point of the wall along its own normal by the same amount.
  //
  // Mid-span rather than anywhere on the face because a wall is curved: a handle at the edge of the
  // span sits on the seam, where it reads as belonging to the neighbouring half.
  if (f.surface.kind == brep::SurfaceKind::Cylinder) {
    const ray3d::Vec3 axis = ray3d::Normalize(f.surface.frame.zAxis);
    const double u = 0.5 * (f.uStart + f.uEnd);
    const ray3d::Vec3 x = ray3d::Normalize(f.surface.frame.xAxis);
    const ray3d::Vec3 y = ray3d::Cross(axis, x);
    ray3d::Vec3 radial =
        ray3d::Add(ray3d::Scale(x, std::cos(u)), ray3d::Scale(y, std::sin(u)));
    const double rl = ray3d::Length(radial);
    if (!(rl > 1e-12))
      return false;
    radial = ray3d::Scale(radial, 1.0 / rl);
    *outAnchor = ray3d::Add(ray3d::Add(f.surface.frame.origin,
                                       ray3d::Scale(axis, f.surface.height * 0.5)),
                            ray3d::Scale(radial, f.surface.radius));
    // `inward` flips which way is out of the material, exactly as the kernel's own sign does — the
    // handle has to drag the way the commit will move, or the preview and the result disagree.
    *outAxis = f.surface.inward ? ray3d::Scale(radial, -1.0) : radial;
    return true;
  }

  if (f.surface.kind != brep::SurfaceKind::Plane)
    return false;  // a cone, sphere or torus wall cannot be pushed, so it gets no handle

  // The centroid of the face's boundary vertices. Averaged over DISTINCT vertices, not over edge
  // uses: a loop uses each vertex twice, so summing uses would weight a shared corner double and
  // pull the handle off centre on any face whose loop is not uniform.
  ray3d::Vec3 sum{0.0, 0.0, 0.0};
  int n = 0;
  std::vector<int> seen;
  for (const brep::Loop& loop : f.loops) {
    for (const brep::EdgeUse& use : loop.uses) {
      if (use.edge < 0 || static_cast<size_t>(use.edge) >= sp->edges.size())
        continue;
      const brep::Edge& e = sp->edges[static_cast<size_t>(use.edge)];
      for (int v : {e.v0, e.v1}) {
        if (v < 0 || static_cast<size_t>(v) >= sp->vertices.size())
          continue;
        if (std::find(seen.begin(), seen.end(), v) != seen.end())
          continue;
        seen.push_back(v);
        sum = ray3d::Add(sum, sp->vertices[static_cast<size_t>(v)].p);
        ++n;
      }
    }
  }
  if (n == 0)
    return false;
  *outAnchor = ray3d::Scale(sum, 1.0 / static_cast<double>(n));

  // The same direction `brep::PushPullFace` will move it, `Surface::inward` included — or a positive
  // drag would push the face one way and the commit would move it the other.
  ray3d::Vec3 dir = f.surface.frame.zAxis;
  if (f.surface.inward)
    dir = ray3d::Scale(dir, -1.0);
  const double len = ray3d::Length(dir);
  if (!(len > 1e-12))
    return false;
  *outAxis = ray3d::Scale(dir, 1.0 / len);
  return true;
}

namespace {

/// A face's outward unit normal in the command layer, honouring `Surface::inward` the same way the
/// kernel does. False on a degenerate frame.
bool CadFaceOutwardNormal(const brep::Face& f, ray3d::Vec3* out) {
  ray3d::Vec3 n = f.surface.frame.zAxis;
  if (f.surface.inward)
    n = ray3d::Scale(n, -1.0);
  const double len = ray3d::Length(n);
  if (!(len > 1e-12))
    return false;
  *out = ray3d::Scale(n, 1.0 / len);
  return true;
}

}  // namespace

bool CadSubObjectVertexGrip(const AppCommandState& st, const SelectedSubObject& ref,
                            ray3d::Vec3* outAnchor) {
  if (!outAnchor)
    return false;
  const CadSolidPtr sp = CadLiveSolidFor(st, ref, solidpick::Kind::Vertex);
  if (!sp || static_cast<size_t>(ref.index) >= sp->vertices.size())
    return false;
  // Only where `brep::MoveVertex` can actually work: exactly three faces, every one of them planar.
  // Asked here rather than discovered on release, so a pyramid's apex and a cylinder's rim — both
  // easy to pick, both impossible to move — simply have no handle.
  std::vector<int> faces;
  brep::FacesAtVertex(*sp, ref.index, &faces);
  if (faces.size() != 3)
    return false;
  for (int fi : faces)
    if (sp->faces[static_cast<size_t>(fi)].surface.kind != brep::SurfaceKind::Plane)
      return false;
  *outAnchor = sp->vertices[static_cast<size_t>(ref.index)].p;
  return true;
}

bool CadSubObjectEdgeGrip(const AppCommandState& st, const SelectedSubObject& ref,
                          ray3d::Vec3* outAnchor, ray3d::Vec3* outAxisA, ray3d::Vec3* outAxisB) {
  if (!outAnchor || !outAxisA || !outAxisB)
    return false;
  const CadSolidPtr sp = CadLiveSolidFor(st, ref, solidpick::Kind::Edge);
  if (!sp || static_cast<size_t>(ref.index) >= sp->edges.size())
    return false;
  const brep::Edge& e = sp->edges[static_cast<size_t>(ref.index)];
  if (e.kind != brep::CurveKind::Line)
    return false;  // a curved edge's faces are curved, and those cannot follow a dragged edge
  int fa = -1;
  int fb = -1;
  if (!brep::FacesAlongEdge(*sp, ref.index, &fa, &fb))
    return false;
  const brep::Face& f0 = sp->faces[static_cast<size_t>(fa)];
  const brep::Face& f1 = sp->faces[static_cast<size_t>(fb)];
  if (f0.surface.kind != brep::SurfaceKind::Plane || f1.surface.kind != brep::SurfaceKind::Plane)
    return false;
  ray3d::Vec3 n0{};
  ray3d::Vec3 n1{};
  if (!CadFaceOutwardNormal(f0, &n0) || !CadFaceOutwardNormal(f1, &n1))
    return false;
  // Parallel faces give no line for the edge to lie on, which is `MoveEdge`'s own refusal.
  if (ray3d::Length(ray3d::Cross(n0, n1)) < 1e-9)
    return false;
  // The MIDPOINT, so the handle sits on the edge rather than at one of its ends, where it would read
  // as a vertex grip — the same reason the face grip uses a centroid rather than a corner.
  const ray3d::Vec3 p0 = sp->vertices[static_cast<size_t>(e.v0)].p;
  const ray3d::Vec3 p1 = sp->vertices[static_cast<size_t>(e.v1)].p;
  *outAnchor = ray3d::Scale(ray3d::Add(p0, p1), 0.5);
  *outAxisA = n0;
  *outAxisB = n1;
  return true;
}



bool HandleSliceTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SliceSession>();
  if (!ss)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  using SP = stepengine::SlicePhase;

  if (ss->phase == SP::SelectSolids) {
    if (!line.empty())
      return false;
    if (st.selection.empty()) {
      log.push_back("SLICE — nothing selected. Click a solid, or ESC.");
      return true;
    }
    SliceEnterPlanePhase(st, log);
    return true;
  }

  if (ss->phase == SP::WaitKeepSide) {
    if (line.empty()) {
      log.push_back("SLICE — pick a point on the side to keep, or type B for both.");
      return true;
    }
    if (line == "B" || line == "b" || line == "Both" || line == "both") {
      CommitSlice(st, brep::SliceKeep::Both, log);
      return true;
    }
    ray3d::Vec3 q{};
    if (!ParseSolidBasePoint(st, line, &q, log, "SLICE")) {
      log.push_back("SLICE — type B for both, or X,Y[,Z] for a point on the side to keep.");
      return true;
    }
    const ray3d::Vec3 nrm = ray3d::Cross(ray3d::Sub(ss->p2, ss->p1), ray3d::Sub(ss->p3, ss->p1));
    const brep::SliceKeep keep =
        ray3d::Dot(ray3d::Sub(q, ss->p1), nrm) >= 0.0 ? brep::SliceKeep::Above : brep::SliceKeep::Below;
    CommitSlice(st, keep, log);
    return true;
  }

  if (line.empty())
    return true;
  ray3d::Vec3 p{};
  if (!ParseSolidBasePoint(st, line, &p, log, "SLICE")) {
    log.push_back("SLICE — could not read the point. Use X,Y or X,Y,Z.");
    return true;
  }
  if (ss->phase == SP::WaitP1) {
    ss->p1 = p;
    ss->phase = SP::WaitP2;
  } else if (ss->phase == SP::WaitP2) {
    ss->p2 = p;
    ss->phase = SP::WaitP3;
  } else {
    ss->p3 = p;
    ss->phase = SP::WaitKeepSide;
  }
  log.push_back(CadSlicePromptText(st));
  return true;
}

void SubmitSliceViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SliceSession>();
  if (!ss)
    return;
  using SP = stepengine::SlicePhase;
  const ray3d::Vec3 p{static_cast<double>(wx), static_cast<double>(wy),
                      static_cast<double>(CadCommitElevation(st))};
  switch (ss->phase) {
  case SP::WaitP1:
    ss->p1 = p;
    ss->phase = SP::WaitP2;
    break;
  case SP::WaitP2:
    ss->p2 = p;
    ss->phase = SP::WaitP3;
    break;
  case SP::WaitP3:
    ss->p3 = p;
    ss->phase = SP::WaitKeepSide;
    break;
  case SP::WaitKeepSide: {
    const ray3d::Vec3 nrm = ray3d::Cross(ray3d::Sub(ss->p2, ss->p1), ray3d::Sub(ss->p3, ss->p1));
    const brep::SliceKeep keep =
        ray3d::Dot(ray3d::Sub(p, ss->p1), nrm) >= 0.0 ? brep::SliceKeep::Above : brep::SliceKeep::Below;
    CommitSlice(st, keep, log);
    return;
  }
  case SP::SelectSolids:
    return;
  }
  log.push_back(CadSlicePromptText(st));
}
