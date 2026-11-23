#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"
#include "GoSurvey/Document/cadpiperun.hpp"
#include "CadBlocks.hpp"
#include <GoSurvey/Core/StringUtil.hpp>
#include <sstream>


// ---------------------------------------------------------------------------------------------
// PIPERUN (issue #486 increment B2 / REQ-345): prompted nominal size + pressure class, then
// click-to-add straight vertices — a `CadPipeRun`'s path, committed as one entity on Enter/END.
// Shaped after POLYSOLID immediately above: a path-building command with its own Kind, its own
// draft state, and one prompt-text function both the status bar and the typed-line echo share.
// Unlike POLYSOLID's plane-relative path, a pipe run's vertices are stored as absolute
// storage-coordinate xyz (REQ-057) — the same form `CadPipeRun::vertsXyz` itself uses, so commit
// is a plain copy with no second representation to keep in step.
// ---------------------------------------------------------------------------------------------

namespace {
// Defined with the rest of PIPERUN's commit path below; declared here because the cancel path sits
// above it. Inside the anonymous namespace so this is the SAME function, not a second overload.
void RemoveLivePipeRun(AppCommandState& st);
}  // namespace

void CancelPipeRunCommand(AppCommandState& st) {
  RemoveLivePipeRun(st);
  if (auto* ps = st.sessionAs<stepengine::PipeRunSession>()) {
    ps->draftVerts.clear();
    ps->phase = stepengine::PipeRunPhase::WaitNominalSize;
  }
}

std::string CadPipeRunPromptText(const AppCommandState& st) {
  using PRP = stepengine::PipeRunPhase;
  const auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  const PRP phase = ps ? ps->phase : PRP::WaitNominalSize;
  const std::string classSuffix =
      st.pipeRunPressureClassTag.empty() ? std::string() : (" " + st.pipeRunPressureClassTag);
  char buf[256];
  if (phase == PRP::WaitNominalSize) {
    if (st.pipeRunNominalSize.empty())
      return "PIPERUN - nominal size (e.g. 4in), optional pressure class (CS150/CS300):";
    std::snprintf(buf, sizeof(buf), "PIPERUN - nominal size [%s%s], Enter to keep:",
                  st.pipeRunNominalSize.c_str(), classSuffix.c_str());
    return buf;
  }
  if (phase == PRP::WaitWallThickness) {
    double sch40 = 0.0;
    if (CadPipeStandardWallThicknessInches(st.pipeRunNominalSize, &sch40))
      std::snprintf(buf, sizeof(buf),
                    "PIPERUN [%s%s] - wall thickness in inches <%.3f, schedule 40>, Enter to accept:",
                    st.pipeRunNominalSize.c_str(), classSuffix.c_str(), sch40);
    else
      std::snprintf(buf, sizeof(buf), "PIPERUN [%s%s] - wall thickness in inches:",
                    st.pipeRunNominalSize.c_str(), classSuffix.c_str());
    return buf;
  }
  if (phase == PRP::WaitFirstPoint) {
    std::snprintf(buf, sizeof(buf), "PIPERUN [%s%s wall %.3fin] - start point, or ESC to cancel:",
                  st.pipeRunNominalSize.c_str(), classSuffix.c_str(), st.pipeRunWallThicknessIn);
    return buf;
  }
  std::snprintf(buf, sizeof(buf), "PIPERUN [%s%s wall %.3fin] - next point (compass %s), or Undo/End/Compass, Enter to finish:",
                st.pipeRunNominalSize.c_str(), classSuffix.c_str(), st.pipeRunWallThicknessIn,
                st.pipeRunCompassOn ? "on" : "off");
  return buf;
}

namespace {

// --- Auto-fitting elbow insertion at bends (issue #486 increment B5 / REQ-345) -------------------

/// Only 90/45-degree elbows exist in `CadPipePartType`'s own vocabulary — `kCadPipeFilletStandardAnglesDeg`
/// also has 60/30/22.5/11.25, which have no elbow PART TYPE to search the catalog for at all. Those
/// angles keep today's smooth fillet not because no catalog match was found for them, but because
/// nothing is looked up in the first place.
CadPipePartType ElbowPartTypeForSnappedAngleDeg(double snappedDeg) {
  if (std::fabs(snappedDeg - 90.0) < 1e-6)
    return CadPipePartType::Elbow90;
  if (std::fabs(snappedDeg - 45.0) < 1e-6)
    return CadPipePartType::Elbow45;
  return CadPipePartType::None;
}

/// Which of \p def's two connections mates with the PIPE ("near") vs the far side ("far"): the
/// port explicitly configured for a pipe end wins; failing that a role-tagged Inlet/Outlet pair is
/// used when present (the natural authoring convention for a through-run fitting), and failing that
/// definition order. Both left null unless \p def has EXACTLY two connections — a part tagged
/// elbow-90/-45 with any other port count cannot be auto-oriented.
///
/// The pipe-end rule is what stops a flange going in backwards (user report 2026-09-24, TASK-276). A
/// weld-neck flange carries two ports that are NOT interchangeable — a weld neck that is welded to
/// the pipe, tagged `CadConnectionModeTarget::PipeEnd`, and a gasket face that mates with another
/// flange, tagged `FlangeFace` — and the bundled 2in part gives BOTH the `Inlet` role, so both
/// tests above fell through to definition order and picked the gasket face. The caller then welds
/// whichever port this returns as `near` onto the pipe, so the fitting came out end-for-end.
///
/// `CadBlockConnectionHasExactMode` (never the `isDefault` fallback) is the same discrimination
/// `SubmitInsertBlockConnectorPick` already makes for INSERT's connector snap, for the same reason
/// and after the same report (TASK-269): a port whose only mode is flagged default looks compatible
/// with every target, so only an EXACT tag may outrank role and definition order. The rule fires
/// only when exactly one of the two ports carries it — an elbow or a tee has a pipe end on both
/// sides, and those keep the Inlet/Outlet resolution they have always had.
void PickElbowPorts(const CadBlockDefinition& def, const CadBlockConnection** near,
                    const CadBlockConnection** far) {
  *near = nullptr;
  *far = nullptr;
  if (def.connections.size() != 2)
    return;
  const CadBlockConnection& c0 = def.connections[0];
  const CadBlockConnection& c1 = def.connections[1];
  const bool pipeEnd0 = CadBlockConnectionHasExactMode(c0, CadConnectionModeTarget::PipeEnd);
  const bool pipeEnd1 = CadBlockConnectionHasExactMode(c1, CadConnectionModeTarget::PipeEnd);
  if (pipeEnd0 != pipeEnd1) {
    *near = pipeEnd0 ? &c0 : &c1;
    *far = pipeEnd0 ? &c1 : &c0;
    return;
  }
  const CadBlockConnection* inlet = nullptr;
  const CadBlockConnection* outlet = nullptr;
  for (const CadBlockConnection& c : def.connections) {
    if (c.role == CadBlockConnectionRole::Inlet && !inlet)
      inlet = &c;
    else if (c.role == CadBlockConnectionRole::Outlet && !outlet)
      outlet = &c;
  }
  if (inlet && outlet) {
    *near = inlet;
    *far = outlet;
  } else {
    *near = &c0;
    *far = &c1;
  }
}

/// One planned elbow insertion, computed during a PLANNING pass over a PIPERUN draft before
/// anything is committed — so a validation failure anywhere in the run can still refuse the whole
/// thing without having partially inserted fittings.
struct PlannedElbow {
  std::string blockName;
  CadBlockXform xf;
  ray3d::Vec3 nearPoint;  ///< where the incoming pipe segment now ends (cut back from the corner)
  ray3d::Vec3 farPoint;   ///< where the outgoing pipe segment now starts — read from the PLANNED
                          ///< fitting's own far-port position, not independently re-derived, so the
                          ///< elbow's real geometry (not an idealized guess) decides the far-side gap
};

/// Attempts to plan ONE bend's auto-fit: catalog lookup (\ref CadPipeCatalogFind may import a
/// not-yet-imported library entry into \p st.drawing.blockDefs as a side effect — harmless and idempotent
/// even if the overall run is later refused), port/engagement resolution, a cutback-fits-the-leg
/// budget check, and two-port orientation. Returns false — falling back to a smooth bend, with the
/// reason logged — for any reason; never creates a `CadBlockRef`, only plans one.
bool TryPlanAutoFitBend(AppCommandState& st, CadPipePartType elbowType, const std::string& nominalSize,
                        CadPipePressureClass pressureClass, const ray3d::Vec3& corner,
                        const ray3d::Vec3& incomingDir, const ray3d::Vec3& outgoingDir,
                        double availableIn, PlannedElbow* out, std::vector<std::string>& log) {
  std::string blockName;
  if (!CadPipeCatalogFind(st, elbowType, nominalSize, pressureClass, &blockName, log))
    return false;  // CadPipeCatalogFind already logged why
  const int di = CadBlockFindDef(st.drawing.blockDefs, blockName);
  if (di < 0)
    return false;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  const CadBlockConnection* nearC = nullptr;
  const CadBlockConnection* farC = nullptr;
  PickElbowPorts(def, &nearC, &farC);
  if (!nearC || !farC) {
    log.push_back("PIPERUN - \"" + blockName +
                  "\" needs exactly two connection ports to auto-insert; keeping a smooth bend here.");
    return false;
  }
  const CadBlockConnectionMode* nearMode = CadBlockResolveMode(*nearC, CadConnectionModeTarget::PipeEnd);
  const double cutA = nearMode ? nearMode->engagementLength : nearC->engagementLength;
  if (cutA < 0.0 || cutA > 0.9 * availableIn) {
    log.push_back("PIPERUN - \"" + blockName +
                  "\"'s engagement length does not fit this leg; keeping a smooth bend here.");
    return false;
  }
  const ray3d::Vec3 nearPoint = ray3d::Sub(corner, ray3d::Scale(incomingDir, cutA));

  const ray3d::Vec3 aNear{nearC->nx, nearC->ny, nearC->nz};
  const ray3d::Vec3 aFar{farC->nx, farC->ny, farC->nz};
  CadBlockXform xf;
  xf.sx = xf.sy = xf.sz = CadBlockInsertUnitsScale(st, def);
  if (!CadBlockOrientTwoPortFitting(aNear, aFar, incomingDir, outgoingDir, &xf)) {
    log.push_back("PIPERUN - \"" + blockName +
                  "\"'s two connection ports are collinear; keeping a smooth bend here.");
    return false;
  }
  float wx = 0.f, wy = 0.f, wz = 0.f;
  CadBlockXformPoint(xf, nearC->x, nearC->y, nearC->z, &wx, &wy, &wz);
  xf.x = static_cast<float>(nearPoint.x) - wx;
  xf.y = static_cast<float>(nearPoint.y) - wy;
  xf.z = static_cast<float>(nearPoint.z) - wz;

  float fx = 0.f, fy = 0.f, fz = 0.f;
  CadBlockXformPoint(xf, farC->x, farC->y, farC->z, &fx, &fy, &fz);

  out->blockName = blockName;
  out->xf = xf;
  out->nearPoint = nearPoint;
  out->farPoint = ray3d::Vec3{static_cast<double>(fx), static_cast<double>(fy), static_cast<double>(fz)};
  return true;
}

/// Splits \p draftVerts into straight/smooth-filleted pieces plus planned elbow insertions at every
/// qualifying bend. A bend with no catalog match, or that fails any check in `TryPlanAutoFitBend`,
/// simply stays part of its piece's own path — `CadBuildPipeRunSweepPath` fillets it smoothly
/// exactly as it always has. If nothing qualifies, `*pieces` ends up holding the ENTIRE original
/// path as its one and only piece with `*elbows` empty — bit-identical to the pre-B5 single-run
/// commit, so a drawing with no matching library parts is completely unaffected by this function
/// existing.
void PlanPipeRunAutoFittings(AppCommandState& st, const std::vector<double>& draftVerts,
                             const std::string& nominalSize, CadPipePressureClass pressureClass,
                             std::vector<std::vector<ray3d::Vec3>>* pieces,
                             std::vector<PlannedElbow>* elbows, std::vector<std::string>& log) {
  const size_t nVerts = draftVerts.size() / 3;
  std::vector<ray3d::Vec3> v(nVerts);
  for (size_t i = 0; i < nVerts; ++i)
    v[i] = ray3d::Vec3{draftVerts[i * 3 + 0], draftVerts[i * 3 + 1], draftVerts[i * 3 + 2]};

  pieces->clear();
  elbows->clear();
  pieces->push_back({v[0]});

  for (size_t i = 1; i < nVerts; ++i) {
    bool handled = false;
    if (i + 1 < nVerts) {
      std::vector<ray3d::Vec3>& cur = pieces->back();
      const ray3d::Vec3 prevPt = cur.back();
      const ray3d::Vec3 legIn = ray3d::Sub(v[i], prevPt);
      const double availableIn = ray3d::Length(legIn);
      const ray3d::Vec3 legOutRaw = ray3d::Sub(v[i + 1], v[i]);
      if (availableIn > 1e-9 && ray3d::Length(legOutRaw) > 1e-9) {
        const ray3d::Vec3 incomingDir = ray3d::Scale(legIn, 1.0 / availableIn);
        const ray3d::Vec3 outgoingDir = ray3d::Normalize(legOutRaw);
        const double cosA = std::clamp(ray3d::Dot(incomingDir, outgoingDir), -1.0, 1.0);
        const double turnRad = std::acos(cosA);
        if (turnRad >= cadpiperun_detail::kMinFilletTurnRad) {
          const double snappedDeg = CadPipeSnapFilletAngleRad(turnRad) / cadpiperun_detail::kDegToRad;
          const CadPipePartType elbowType = ElbowPartTypeForSnappedAngleDeg(snappedDeg);
          if (elbowType != CadPipePartType::None) {
            PlannedElbow planned;
            if (TryPlanAutoFitBend(st, elbowType, nominalSize, pressureClass, v[i], incomingDir,
                                   outgoingDir, availableIn, &planned, log)) {
              cur.push_back(planned.nearPoint);
              elbows->push_back(planned);
              pieces->push_back({planned.farPoint});
              handled = true;
            }
          }
        }
      }
    }
    if (!handled)
      pieces->back().push_back(v[i]);
  }
}

// --- Auto-fitting tee insertion at branch nodes (issue #486 increment B6 / REQ-345) -------------

/// One leg meeting at a shared point: either the NEW draft's own endpoint (`runIndex < 0`) or an
/// EXISTING `CadPipeRun`'s endpoint (`runIndex` into `st.drawing.cadPipeRuns`, `atStart` which end).
/// `dir` points AWAY from the shared point, into that run's own body — the same "away from the
/// joint" convention on both sides so the through-pair test below needs no special-casing.
struct BranchLeg {
  int runIndex = -1;
  bool atStart = false;
  ray3d::Vec3 dir;
  double availableLen = 0.0;
};

/// Every EXISTING run (not the draft being committed) whose start or end vertex sits exactly at
/// \p pt — the only case this increment recognizes as a branch node. A mid-span tie-in (splicing
/// into the SIDE of an existing straight run) would need to split that run's own path first and is
/// deliberately deferred; see `CommitPipeRunDraft`'s own doc comment for the recorded scope note.
void FindExistingRunLegsAt(const AppCommandState& st, const ray3d::Vec3& pt,
                           std::vector<BranchLeg>* legs) {
  constexpr double kCoincidentEps = 1e-6;
  for (size_t ri = 0; ri < st.drawing.cadPipeRuns.size(); ++ri) {
    const CadPipeRun& run = st.drawing.cadPipeRuns[ri];
    const size_t n = run.vertsXyz.size() / 3;
    if (n < 2)
      continue;
    const ray3d::Vec3 first{run.vertsXyz[0], run.vertsXyz[1], run.vertsXyz[2]};
    const ray3d::Vec3 second{run.vertsXyz[3], run.vertsXyz[4], run.vertsXyz[5]};
    if (ray3d::Length(ray3d::Sub(first, pt)) < kCoincidentEps) {
      const ray3d::Vec3 d = ray3d::Sub(second, first);
      const double len = ray3d::Length(d);
      if (len > 1e-9)
        legs->push_back(BranchLeg{static_cast<int>(ri), true, ray3d::Scale(d, 1.0 / len), len});
    }
    const ray3d::Vec3 last{run.vertsXyz[(n - 1) * 3 + 0], run.vertsXyz[(n - 1) * 3 + 1],
                           run.vertsXyz[(n - 1) * 3 + 2]};
    const ray3d::Vec3 secondLast{run.vertsXyz[(n - 2) * 3 + 0], run.vertsXyz[(n - 2) * 3 + 1],
                                 run.vertsXyz[(n - 2) * 3 + 2]};
    if (ray3d::Length(ray3d::Sub(last, pt)) < kCoincidentEps) {
      const ray3d::Vec3 d = ray3d::Sub(secondLast, last);
      const double len = ray3d::Length(d);
      if (len > 1e-9)
        legs->push_back(BranchLeg{static_cast<int>(ri), false, ray3d::Scale(d, 1.0 / len), len});
    }
  }
}

/// Which of \p def's three connections plays which role in a tee: EXACTLY one each of Inlet,
/// Outlet, Branch (`CadBlockConnectionRole`) — a part tagged `tee` with any other port makeup or
/// count cannot be auto-oriented. All three left null otherwise.
void PickTeePorts(const CadBlockDefinition& def, const CadBlockConnection** inlet,
                  const CadBlockConnection** outlet, const CadBlockConnection** branch) {
  *inlet = *outlet = *branch = nullptr;
  if (def.connections.size() != 3)
    return;
  const CadBlockConnection* in = nullptr;
  const CadBlockConnection* out = nullptr;
  const CadBlockConnection* br = nullptr;
  for (const CadBlockConnection& c : def.connections) {
    if (c.role == CadBlockConnectionRole::Inlet && !in) in = &c;
    else if (c.role == CadBlockConnectionRole::Outlet && !out) out = &c;
    else if (c.role == CadBlockConnectionRole::Branch && !br) br = &c;
  }
  if (in && out && br) {
    *inlet = in;
    *outlet = out;
    *branch = br;
  }
}

/// One planned tee insertion at a node where exactly three legs meet: two roughly opposite
/// (the through run) plus one branch. `legs`/`cutPoint` share an index — `legs[k]` is the leg
/// whose pipe now ends at `cutPoint[k]`.
struct PlannedTee {
  std::string blockName;
  CadBlockXform xf;
  BranchLeg legs[3];
  ray3d::Vec3 cutPoint[3];
};

/// Attempts to plan a tee for exactly three legs meeting at \p node: catalog lookup, port/role
/// resolution, an engagement-length budget check per leg, and orientation via the SAME two-port
/// rigid-alignment primitive B5's elbow fit uses (`CadBlockOrientTwoPortFitting`) — but unlike an
/// elbow, a tee's inlet/outlet pair normally points in ANTI-parallel directions (a straight run
/// through the body): the cross product `RotationAligningTwoDirections` uses to build its local
/// "up" axis is then zero, an unsolvable degenerate case (no unique roll for two collinear
/// references).
/// The inlet/BRANCH pair is used to solve the rotation instead — a real tee's branch sits off-axis
/// from the through run, so that pair is never collinear — and the outlet's resulting world
/// position is then READ from the fitting's own solved geometry (not independently pinned), the
/// same "trust the model after pinning one side" reasoning `PlannedElbow::farPoint` uses (extended
/// here to a second unpinned port).
/// Returns false for any reason, with the reason logged; never mutates `st`.
bool TryPlanBranchTee(AppCommandState& st, const std::vector<BranchLeg>& legs,
                      const ray3d::Vec3& node, const std::string& nominalSize,
                      CadPipePressureClass pressureClass, PlannedTee* out,
                      std::vector<std::string>& log) {
  if (legs.size() != 3)
    return false;

  int bestI = -1, bestJ = -1;
  double bestDot = 1.0;
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      const double d = ray3d::Dot(legs[static_cast<size_t>(i)].dir, legs[static_cast<size_t>(j)].dir);
      if (d < bestDot) {
        bestDot = d;
        bestI = i;
        bestJ = j;
      }
    }
  }
  if (bestDot > -0.85) {
    log.push_back("PIPERUN - the three runs meeting here aren't roughly straight-through plus a "
                  "branch; no tee inserted here.");
    return false;
  }
  const int branchIx = 3 - bestI - bestJ;
  const BranchLeg& legA = legs[static_cast<size_t>(bestI)];
  const BranchLeg& legB = legs[static_cast<size_t>(bestJ)];
  const BranchLeg& legC = legs[static_cast<size_t>(branchIx)];

  std::string blockName;
  if (!CadPipeCatalogFind(st, CadPipePartType::Tee, nominalSize, pressureClass, &blockName, log))
    return false;
  const int di = CadBlockFindDef(st.drawing.blockDefs, blockName);
  if (di < 0)
    return false;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  const CadBlockConnection* inletC = nullptr;
  const CadBlockConnection* outletC = nullptr;
  const CadBlockConnection* branchC = nullptr;
  PickTeePorts(def, &inletC, &outletC, &branchC);
  if (!inletC || !outletC || !branchC) {
    log.push_back("PIPERUN - \"" + blockName + "\" needs inlet+outlet+branch ports to auto-insert "
                  "a tee; no fitting inserted here.");
    return false;
  }

  const ray3d::Vec3 incomingDir = ray3d::Scale(legA.dir, -1.0);
  const ray3d::Vec3 aIn{inletC->nx, inletC->ny, inletC->nz};
  const ray3d::Vec3 aBranch{branchC->nx, branchC->ny, branchC->nz};
  CadBlockXform xf;
  xf.sx = xf.sy = xf.sz = CadBlockInsertUnitsScale(st, def);
  if (!CadBlockOrientTwoPortFitting(aIn, aBranch, incomingDir, legC.dir, &xf)) {
    log.push_back("PIPERUN - \"" + blockName + "\"'s inlet/branch ports are collinear; no tee "
                  "inserted here.");
    return false;
  }

  const auto engagementOf = [](const CadBlockConnection& c) {
    const CadBlockConnectionMode* m = CadBlockResolveMode(c, CadConnectionModeTarget::PipeEnd);
    return m ? m->engagementLength : c.engagementLength;
  };
  const double cutA = engagementOf(*inletC);
  const double cutB = engagementOf(*outletC);
  const double cutC = engagementOf(*branchC);
  if (cutA < 0.0 || cutA > 0.9 * legA.availableLen || cutB < 0.0 || cutB > 0.9 * legB.availableLen ||
      cutC < 0.0 || cutC > 0.9 * legC.availableLen) {
    log.push_back("PIPERUN - \"" + blockName + "\"'s engagement length does not fit one of the "
                  "three legs; no tee inserted here.");
    return false;
  }

  const ray3d::Vec3 nearPointA = ray3d::Add(node, ray3d::Scale(legA.dir, cutA));
  float wx = 0.f, wy = 0.f, wz = 0.f;
  CadBlockXformPoint(xf, inletC->x, inletC->y, inletC->z, &wx, &wy, &wz);
  xf.x = static_cast<float>(nearPointA.x) - wx;
  xf.y = static_cast<float>(nearPointA.y) - wy;
  xf.z = static_cast<float>(nearPointA.z) - wz;

  float ox = 0.f, oy = 0.f, oz = 0.f;
  CadBlockXformPoint(xf, outletC->x, outletC->y, outletC->z, &ox, &oy, &oz);
  float bx = 0.f, by = 0.f, bz = 0.f;
  CadBlockXformPoint(xf, branchC->x, branchC->y, branchC->z, &bx, &by, &bz);
  // farPointB/farPointC are trusted straight from the fitting's own solved geometry — the same
  // "the model decides, not an idealized guess" reasoning `PlannedElbow::farPoint` already uses for
  // an elbow's single unpinned port, just applied to TWO unpinned ports here. Whether the result is
  // actually usable is left to the piece/scratch-run solid validation the caller already performs
  // (`TryApplyBranchAtEndpoint`, `CommitPipeRunDraft`) — the same thing that would catch it for an
  // elbow's far side too.
  const ray3d::Vec3 farPointB{ox, oy, oz};
  const ray3d::Vec3 farPointC{bx, by, bz};

  out->blockName = blockName;
  out->xf = xf;
  out->legs[0] = legA;
  out->legs[1] = legB;
  out->legs[2] = legC;
  out->cutPoint[0] = nearPointA;
  out->cutPoint[1] = farPointB;
  out->cutPoint[2] = farPointC;
  return true;
}

/// A resolved tee insertion, ready to apply: the new run's own cutback endpoint plus the two
/// EXISTING runs it ties into and their own new cutback endpoints.
struct BranchFitPlan {
  std::string blockName;
  CadBlockXform xf;
  int existingRunIdx[2] = {-1, -1};
  bool existingAtStart[2] = {false, false};
  ray3d::Vec3 existingCut[2];
  bool newRunAtStart = false;
  ray3d::Vec3 newRunCut;
};

/// Only a plain 3-way joint — this draft's endpoint plus EXACTLY two other existing runs' own
/// endpoints, already coincident at \p node — is in scope; a node with any other leg count (a
/// simple 2-run abutment, or 4+ legs which would want a cross) is left as-is and logged, deferred
/// to a later increment. Never mutates `st` or its entities.
bool TryPlanBranchAtNode(AppCommandState& st, const ray3d::Vec3& node, bool newRunAtStart,
                         const ray3d::Vec3& newRunDir, double newRunAvailable,
                         const std::string& nominalSize, CadPipePressureClass pressureClass,
                         BranchFitPlan* out, std::vector<std::string>& log) {
  std::vector<BranchLeg> existing;
  FindExistingRunLegsAt(st, node, &existing);
  if (existing.size() != 2)
    return false;
  std::vector<BranchLeg> legs = existing;
  legs.push_back(BranchLeg{-1, newRunAtStart, newRunDir, newRunAvailable});

  PlannedTee planned;
  if (!TryPlanBranchTee(st, legs, node, nominalSize, pressureClass, &planned, log))
    return false;

  out->blockName = planned.blockName;
  out->xf = planned.xf;
  int nExisting = 0;
  for (int k = 0; k < 3; ++k) {
    const BranchLeg& lg = planned.legs[static_cast<size_t>(k)];
    if (lg.runIndex < 0) {
      out->newRunAtStart = lg.atStart;
      out->newRunCut = planned.cutPoint[static_cast<size_t>(k)];
    } else {
      if (nExisting >= 2)
        return false;  // planned.legs didn't carry exactly one new (runIndex < 0) leg; refuse rather than overrun.
      out->existingRunIdx[nExisting] = lg.runIndex;
      out->existingAtStart[nExisting] = lg.atStart;
      out->existingCut[nExisting] = planned.cutPoint[static_cast<size_t>(k)];
      ++nExisting;
    }
  }
  return true;
}

/// Attempts a branch tee at one endpoint of the draft (\p piece's front if \p atStart, else its
/// back). Validates the TWO EXISTING runs a match would cut back — using scratch copies, so a
/// refusal here leaves both `st` and \p piece untouched — before applying anything, the same
/// all-or-nothing reasoning `CommitPipeRunDraft`'s own piece-solid validation already uses. On
/// success, mutates \p piece's endpoint in place and appends the resolved plan to \p branchFits.
void TryApplyBranchAtEndpoint(AppCommandState& st, std::vector<ray3d::Vec3>& piece, bool atStart,
                              const std::string& nominalSize, CadPipePressureClass pressureClass,
                              std::vector<BranchFitPlan>& branchFits, std::vector<std::string>& log) {
  if (piece.size() < 2)
    return;
  const ray3d::Vec3 node = atStart ? piece.front() : piece.back();
  const ray3d::Vec3 neighbor = atStart ? piece[1] : piece[piece.size() - 2];
  const ray3d::Vec3 legVec = ray3d::Sub(neighbor, node);
  const double avail = ray3d::Length(legVec);
  if (avail < 1e-9)
    return;
  const ray3d::Vec3 dir = ray3d::Scale(legVec, 1.0 / avail);

  BranchFitPlan plan;
  if (!TryPlanBranchAtNode(st, node, atStart, dir, avail, nominalSize, pressureClass, &plan, log))
    return;

  for (int k = 0; k < 2; ++k) {
    CadPipeRun scratch = st.drawing.cadPipeRuns[static_cast<size_t>(plan.existingRunIdx[k])];
    const size_t n = scratch.vertsXyz.size() / 3;
    const size_t vi = plan.existingAtStart[k] ? 0 : (n - 1);
    scratch.vertsXyz[vi * 3 + 0] = plan.existingCut[k].x;
    scratch.vertsXyz[vi * 3 + 1] = plan.existingCut[k].y;
    scratch.vertsXyz[vi * 3 + 2] = plan.existingCut[k].z;
    std::vector<CadSolidPtr> preview;
    if (!CadBuildPipeRunSolids(scratch, &preview)) {
      log.push_back("PIPERUN - inserting \"" + plan.blockName +
                    "\" here would leave an existing run without a valid pipe solid; no tee "
                    "inserted here.");
      return;
    }
  }

  if (atStart)
    piece.front() = plan.newRunCut;
  else
    piece.back() = plan.newRunCut;
  branchFits.push_back(std::move(plan));
}

/// A new pipe run's attributes: the ordinary new-entity ones (current layer), coloured by its size
/// from the size palette (REQ-353, `kCadPipeNpsTable`). A size the table does not carry keeps
/// `ByLayer` — unreachable through PIPERUN's size prompt, which refuses such a size.
EntityAttributes MakeNewPipeRunAttrs(AppCommandState& st, std::string_view nominalSize) {
  EntityAttributes a = MakeNewObjectAttrs(st, ObjectLayerKind::PipeRun, {});  // REQ-361
  std::string hex;
  if (CadPipeNominalSizeColor(nominalSize, &hex))
    a.color = hex;
  return a;
}

/// Store the run built from the path so far and end the command. Auto-inserts an elbow fitting
/// (issue #486 increment B5) at every bend where a catalog match exists — see
/// `PlanPipeRunAutoFittings` — splitting the single path into multiple straight/smooth-filleted
/// `CadPipeRun` pieces around each inserted elbow; a run with no qualifying bend collapses back to
/// exactly the pre-B5 single-run behavior. Also auto-inserts a tee (issue #486 increment B6) at
/// either endpoint of the draft when it lands exactly on two OTHER existing runs' own endpoints —
/// see `TryApplyBranchAtEndpoint`. Deliberately out of scope, by name: a mid-span tie-in (splicing
/// into the SIDE of an existing run, which would need to split that run's path first), a cross
/// (4+ legs at one node), and vertical-riser/offset-transition fittings — those are already
/// ordinary 90/45 BENDS on one run's own path and were delivered by B5, since its bend detection
/// is fully 3D and not limited to a horizontal plane.
/// Drop the provisional in-progress pipe run, if one is standing (see \ref SyncLivePipeRun).
///
/// Only ever the LAST element, which is what it always is: it is appended when the second point
/// lands and nothing else appends a run while PIPERUN owns the command. Erasing from the middle
/// would renumber every later run, and architecture invariant §11.9 is explicit that an index is not
/// a name — so this refuses rather than renumbering, leaving a stray provisional run the user can
/// delete instead of silently corrupting references to other runs.
void RemoveLivePipeRun(AppCommandState& st) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps)
    return;
  const int idx = ps->liveIndex;
  ps->liveIndex = -1;
  if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.cadPipeRuns.size())
    return;
  if (static_cast<size_t>(idx) + 1 != st.drawing.cadPipeRuns.size())
    return;
  st.drawing.cadPipeRuns.pop_back();
  if (!st.drawing.cadPipeRunAttrs.empty())
    st.drawing.cadPipeRunAttrs.pop_back();
  BumpCadGpuCache(st);
}

void SyncLivePipeRun(AppCommandState& st) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps)
    return;
  const bool haveSegment = ps->draftVerts.size() >= 6;
  if (!haveSegment) {
    RemoveLivePipeRun(st);
    return;
  }
  CadPipeRun run;
  run.vertsXyz = ps->draftVerts;
  run.nominalSize = st.pipeRunNominalSize;
  run.wallThicknessIn = st.pipeRunWallThicknessIn;
  run.pressureClassTag = st.pipeRunPressureClassTag;

  if (ps->liveIndex >= 0 && static_cast<size_t>(ps->liveIndex) < st.drawing.cadPipeRuns.size()) {
    st.drawing.cadPipeRuns[static_cast<size_t>(ps->liveIndex)] = std::move(run);
  } else {
    st.drawing.cadPipeRuns.push_back(std::move(run));
    st.drawing.cadPipeRunAttrs.push_back(MakeNewEntityAttrs(st));
    ps->liveIndex = static_cast<int>(st.drawing.cadPipeRuns.size()) - 1;
  }
  BumpCadGpuCache(st);
}

void CommitPipeRunDraft(AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps || ps->draftVerts.size() < 6) {
    log.push_back("PIPERUN - a run needs at least two points; Esc cancels.");
    return;
  }

  RemoveLivePipeRun(st);

  const CadPipePressureClass pressureClass = ParseCadPipePressureClass(st.pipeRunPressureClassTag);
  std::vector<std::vector<ray3d::Vec3>> pieces;
  std::vector<PlannedElbow> elbows;
  PlanPipeRunAutoFittings(st, ps->draftVerts, st.pipeRunNominalSize, pressureClass, &pieces,
                          &elbows, log);

  std::vector<BranchFitPlan> branchFits;
  TryApplyBranchAtEndpoint(st, pieces.front(), /*atStart=*/true, st.pipeRunNominalSize,
                           pressureClass, branchFits, log);
  TryApplyBranchAtEndpoint(st, pieces.back(), /*atStart=*/false, st.pipeRunNominalSize,
                           pressureClass, branchFits, log);

  std::vector<CadPipeRun> pieceRuns(pieces.size());
  for (size_t p = 0; p < pieces.size(); ++p) {
    CadPipeRun& run = pieceRuns[p];
    run.vertsXyz.reserve(pieces[p].size() * 3);
    for (const ray3d::Vec3& pt : pieces[p]) {
      run.vertsXyz.push_back(pt.x);
      run.vertsXyz.push_back(pt.y);
      run.vertsXyz.push_back(pt.z);
    }
    run.nominalSize = st.pipeRunNominalSize;
    run.wallThicknessIn = st.pipeRunWallThicknessIn;
    run.pressureClassTag = st.pipeRunPressureClassTag;
    std::vector<CadSolidPtr> preview;
    if (!CadBuildPipeRunSolids(run, &preview)) {
      log.push_back("PIPERUN - could not build a pipe solid — a corner may be too tight for this "
                    "size's fillet radius. U to remove the last point, or Esc to cancel.");
      return;
    }
  }

  PushUndoSnapshot(st, "Create Pipe Run");
  for (const BranchFitPlan& bf : branchFits) {
    for (int k = 0; k < 2; ++k) {
      CadPipeRun& run = st.drawing.cadPipeRuns[static_cast<size_t>(bf.existingRunIdx[k])];
      const size_t n = run.vertsXyz.size() / 3;
      const size_t vi = bf.existingAtStart[k] ? 0 : (n - 1);
      run.vertsXyz[vi * 3 + 0] = bf.existingCut[k].x;
      run.vertsXyz[vi * 3 + 1] = bf.existingCut[k].y;
      run.vertsXyz[vi * 3 + 2] = bf.existingCut[k].z;
    }
  }
  const EntityAttributes lineAttrs = MakeNewPipeRunAttrs(st, st.pipeRunNominalSize);
  size_t elbowIx = 0;
  size_t totalVerts = 0;
  for (size_t p = 0; p < pieceRuns.size(); ++p) {
    totalVerts += pieceRuns[p].vertsXyz.size() / 3;
    st.drawing.cadPipeRuns.push_back(std::move(pieceRuns[p]));
    st.drawing.cadPipeRunAttrs.push_back(lineAttrs);
    if (elbowIx < elbows.size()) {
      const PlannedElbow& e = elbows[elbowIx++];
      CadBlockPlaceInsertNoUndo(st, e.blockName, e.xf, log, &lineAttrs);
    }
  }
  for (const BranchFitPlan& bf : branchFits)
    CadBlockPlaceInsertNoUndo(st, bf.blockName, bf.xf, log, &lineAttrs);
  BumpCadGpuCache(st);
  if (elbows.empty() && branchFits.empty()) {
    log.push_back("PIPERUN - run created: " + std::to_string(totalVerts) + " point(s).");
  } else {
    std::string msg = "PIPERUN - run created: " + std::to_string(pieceRuns.size()) + " segment(s)";
    if (!elbows.empty())
      msg += ", " + std::to_string(elbows.size()) + " elbow fitting(s)";
    if (!branchFits.empty())
      msg += ", " + std::to_string(branchFits.size()) + " tee fitting(s)";
    log.push_back(msg + " inserted.");
  }
  CancelPipeRunCommand(st);
  stepengine::EndSession(st);
}

void AddPipeRunPoint(AppCommandState& st, const ray3d::Vec3& pt, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps)
    return;
  using PRP = stepengine::PipeRunPhase;
  if (ps->phase == PRP::WaitFirstPoint) {
    ps->draftVerts = {pt.x, pt.y, pt.z};
    ps->phase = PRP::WaitNextPoint;
    log.push_back(CadPipeRunPromptText(st));
    return;
  }
  const size_t n = ps->draftVerts.size();
  if (n >= 3) {
    const double lx = ps->draftVerts[n - 3];
    const double ly = ps->draftVerts[n - 2];
    const double lz = ps->draftVerts[n - 1];
    if (std::fabs(pt.x - lx) < 1e-9 && std::fabs(pt.y - ly) < 1e-9 && std::fabs(pt.z - lz) < 1e-9) {
      log.push_back("PIPERUN - that is the same point as the last one.");
      return;
    }
  }
  ps->draftVerts.push_back(pt.x);
  ps->draftVerts.push_back(pt.y);
  ps->draftVerts.push_back(pt.z);
  ++st.pipeRunPerf.clicks;
  SyncLivePipeRun(st);
  log.push_back(CadPipeRunPromptText(st));
}

} // namespace

bool CadPipeRunFinishForHandoff(AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps)
    return false;
  if (ps->draftVerts.size() >= 6) {
    CommitPipeRunDraft(st, log);
    return true;
  }
  CancelPipeRunCommand(st);
  stepengine::EndSession(st);
  log.push_back("PIPERUN - ended (a run needs at least two points).");
  return true;
}

void StartPipeRunCommand(AppCommandState& st, std::vector<std::string>& log) {
  stepengine::BeginSession(st, std::make_unique<stepengine::PipeRunSession>(), false);
  st.pipeRunWallThicknessIn = 0.0;
  st.pipeRunPerf.Reset();
  CadPipePaletteSetOpen(st, true, log);
  log.push_back(CadPipeRunPromptText(st));
}

void ChoosePipeRunNominalSize(AppCommandState& st, const std::string& size) {
  st.pipeRunNominalSize = size;
  st.pipeRunPressureClassTag.clear();
}

void StartPipeRunAtCurrentSize(AppCommandState& st, std::vector<std::string>& log) {
  auto sess = std::make_unique<stepengine::PipeRunSession>();
  auto* ps = sess.get();
  stepengine::BeginSession(st, std::move(sess), false);
  st.pipeRunWallThicknessIn = 0.0;
  st.pipeRunPerf.Reset();
  CadPipePaletteSetOpen(st, true, log);
  double wallIn = 0.0;
  if (CadPipeStandardWallThicknessInches(st.pipeRunNominalSize, &wallIn)) {
    st.pipeRunWallThicknessIn = wallIn;
    ps->phase = stepengine::PipeRunPhase::WaitFirstPoint;
  }
  log.push_back(CadPipeRunPromptText(st));
}

bool HandlePipeRunTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps)
    return false;
  using PRP = stepengine::PipeRunPhase;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (ps->phase == PRP::WaitNominalSize) {
    if (line.empty()) {
      if (st.pipeRunNominalSize.empty()) {
        log.push_back("PIPERUN - a nominal size is required, e.g. 4in.");
        return true;
      }
      ps->phase = PRP::WaitWallThickness;
      log.push_back(CadPipeRunPromptText(st));
      return true;
    }
    std::istringstream iss(line);
    std::string sizeTok;
    std::string classTok;
    iss >> sizeTok >> classTok;
    double odFeet = 0.0;
    if (!CadPipeNominalOdFeet(sizeTok, &odFeet)) {
      log.push_back("PIPERUN - unknown nominal size \"" + sizeTok +
                    "\". Known NPS sizes: " + CadPipeKnownNominalSizesText() + ".");
      return true;
    }
    std::string classTag;
    if (!classTok.empty()) {
      const CadPipePressureClass pc = ParseCadPipePressureClass(classTok);
      if (pc == CadPipePressureClass::None) {
        log.push_back("PIPERUN - unknown pressure class \"" + classTok + "\". Use CS150 or CS300.");
        return true;
      }
      classTag = std::string(CadPipePressureClassTag(pc));
    }
    st.pipeRunNominalSize = sizeTok;
    st.pipeRunPressureClassTag = classTag;
    ps->phase = PRP::WaitWallThickness;
    log.push_back(CadPipeRunPromptText(st));
    return true;
  }

  if (ps->phase == PRP::WaitWallThickness) {
    double odFeet = 0.0;
    if (!CadPipeNominalOdFeet(st.pipeRunNominalSize, &odFeet)) {
      ps->phase = PRP::WaitNominalSize;
      log.push_back("PIPERUN - nominal size is not set; type one first, e.g. 4in.");
      return true;
    }
    const double odIn = odFeet * 12.0;
    double wallIn = 0.0;
    if (line.empty()) {
      if (!CadPipeStandardWallThicknessInches(st.pipeRunNominalSize, &wallIn)) {
        log.push_back("PIPERUN - no standard wall is known for " + st.pipeRunNominalSize +
                      "; type a wall thickness in inches.");
        return true;
      }
    } else {
      char* end = nullptr;
      wallIn = std::strtod(line.c_str(), &end);
      if (!end || *end != '\0' || !std::isfinite(wallIn) || !(wallIn > 0.0)) {
        log.push_back("PIPERUN - wall thickness must be a positive number of inches, e.g. 0.237.");
        return true;
      }
    }
    if (!(wallIn < odIn * 0.5)) {
      char msg[192];
      std::snprintf(msg, sizeof(msg),
                    "PIPERUN - a %.3fin wall leaves no bore in %s pipe (OD %.3fin); it must be under "
                    "%.3fin.",
                    wallIn, st.pipeRunNominalSize.c_str(), odIn, odIn * 0.5);
      log.push_back(msg);
      return true;
    }
    st.pipeRunWallThicknessIn = wallIn;
    ps->phase = PRP::WaitFirstPoint;
    log.push_back(CadPipeRunPromptText(st));
    return true;
  }

  if (line.empty()) {
    if (ps->phase == PRP::WaitFirstPoint) {
      log.push_back("PIPERUN canceled — no points picked.");
      CancelPipeRunCommand(st);
      stepengine::EndSession(st);
      return true;
    }
    CommitPipeRunDraft(st, log);
    return true;
  }

  const std::string low = StringUtil::toLowerAsciiCopy(line);
  if (low == "end") {
    if (ps->phase != PRP::WaitNextPoint)
      log.push_back("PIPERUN END - need at least one segment after the start point.");
    else
      CommitPipeRunDraft(st, log);
    return true;
  }
  if (low == "u" || low == "undo") {
    if (ps->phase != PRP::WaitNextPoint || ps->draftVerts.size() < 6) {
      log.push_back("PIPERUN - nothing to undo yet.");
      return true;
    }
    ps->draftVerts.resize(ps->draftVerts.size() - 3);
    SyncLivePipeRun(st);
    log.push_back(CadPipeRunPromptText(st));
    return true;
  }
  if (low == "compass") {
    st.pipeRunCompassOn = !st.pipeRunCompassOn;
    log.push_back(st.pipeRunCompassOn ? "PIPERUN - compass ON." : "PIPERUN - compass OFF.");
    log.push_back(CadPipeRunPromptText(st));
    return true;
  }

  if (ps->phase == PRP::WaitNextPoint && st.pipeRunCompassOn && ps->draftVerts.size() >= 3) {
    char* distEnd = nullptr;
    const double dist = std::strtod(line.c_str(), &distEnd);
    if (distEnd && *distEnd == '\0' && !line.empty() && std::isfinite(dist) && dist > 0.0) {
      const size_t n = ps->draftVerts.size();
      const float lastX = static_cast<float>(ps->draftVerts[n - 3]);
      const float lastY = static_cast<float>(ps->draftVerts[n - 2]);
      const float lastZ = static_cast<float>(ps->draftVerts[n - 1]);
      float wx = st.viewportSnapPickValid ? static_cast<float>(st.viewportSnapPickLocalX) : st.uiCursorWorldX;
      float wy = st.viewportSnapPickValid ? static_cast<float>(st.viewportSnapPickLocalY) : st.uiCursorWorldY;
      float wz = lastZ;
      const float targetZ = static_cast<float>(CadCommitElevation(st));
      ApplyPipeRunCompassFromAnchor(st, lastX, lastY, &wx, &wy, /*compass=*/true, lastZ, targetZ, &wz);
      const double dx = static_cast<double>(wx) - lastX;
      const double dy = static_cast<double>(wy) - lastY;
      const double dz = static_cast<double>(wz) - lastZ;
      const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (len > 1e-9) {
        const double ux = dx / len;
        const double uy = dy / len;
        const double uz = dz / len;
        const ray3d::Vec3 pt{lastX + ux * dist, lastY + uy * dist, lastZ + uz * dist};
        AddPipeRunPoint(st, pt, log);
        return true;
      }
    }
  }

  ray3d::Vec3 pt{};
  ray3d::Vec3 lastVertex{};
  const bool haveLast = ps->phase == PRP::WaitNextPoint && ps->draftVerts.size() >= 3;
  if (haveLast) {
    const size_t n = ps->draftVerts.size();
    lastVertex = {ps->draftVerts[n - 3], ps->draftVerts[n - 2], ps->draftVerts[n - 1]};
  }
  if (!ParseSolidBasePoint(st, line, &pt, log, "PIPERUN", haveLast ? &lastVertex : nullptr, /*zInUcs=*/true))
    return true;
  AddPipeRunPoint(st, pt, log);
  return true;
}

void SubmitPipeRunViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeRunSession>();
  if (!ps)
    return;
  using PRP = stepengine::PipeRunPhase;
  if (ps->phase == PRP::WaitNominalSize) {
    log.push_back("PIPERUN - type a nominal size first (e.g. 4in), optionally followed by a "
                  "pressure class.");
    return;
  }
  if (ps->phase == PRP::WaitWallThickness) {
    log.push_back("PIPERUN - answer the wall thickness first (Enter takes the standard schedule-40 "
                  "wall for this size).");
    return;
  }
  const size_t n = ps->draftVerts.size();
  float wz = static_cast<float>(CadCommitElevation(st));
  if (ps->phase == PRP::WaitNextPoint && n >= 3) {
    const float lastX = static_cast<float>(ps->draftVerts[n - 3]);
    const float lastY = static_cast<float>(ps->draftVerts[n - 2]);
    const float lastZ = static_cast<float>(ps->draftVerts[n - 1]);
    const float targetZ = wz;
    ApplyPipeRunCompassFromAnchor(st, lastX, lastY, &wx, &wy, /*compass=*/true, lastZ, targetZ, &wz);
  }
  const ray3d::Vec3 pt{static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz)};
  AddPipeRunPoint(st, pt, log);
}

// ---------------------------------------------------------------------------------------------
// PIPESYS (issue #486 increment B3 / REQ-345) — named piping networks. Purely text-driven, no
// viewport interaction and no `AppCommandState::Kind` of its own: a network is metadata grouping
// existing `cadPipeRuns` (already routed by PIPERUN), not a thing you route, so every operation
// takes exactly the same "bare verb reports, verb + args sets" one-shot shape `SOLIDLIST`/`ISOLINES`
// already use, rather than a multi-turn prompt state machine.
// ---------------------------------------------------------------------------------------------

namespace {

/// Finds a network by name (case-sensitive — names are user-chosen labels, not identifiers with a
/// canonical case). Returns -1 if none matches.
int FindPipingSystemByName(const AppCommandState& st, const std::string& name) {
  for (size_t i = 0; i < st.drawing.cadPipingSystems.size(); ++i) {
    if (st.drawing.cadPipingSystems[i].name == name)
      return static_cast<int>(i);
  }
  return -1;
}

/// Every selected pipe run's index, sorted ascending and de-duplicated — the order `PIPESYS ADD`/
/// `REMOVE` report and store in, matching `CadPipingSystem::pipeRunIndices`'s own stated invariant.
std::vector<int> SelectedPipeRunIndices(const AppCommandState& st) {
  std::set<int> ix;
  const size_t n = st.drawing.cadPipeRuns.size();
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::PipeRun && e.index >= 0 && static_cast<size_t>(e.index) < n)
      ix.insert(e.index);
  }
  return std::vector<int>(ix.begin(), ix.end());
}

} // namespace

void HandlePipingSystemCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log) {
  std::istringstream iss(args);
  std::string verb;
  iss >> verb;
  for (char& c : verb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  const auto listAll = [&]() {
    if (st.drawing.cadPipingSystems.empty()) {
      log.push_back("PIPESYS - no piping networks defined. Usage: PIPESYS NEW <name>.");
      return;
    }
    for (const CadPipingSystem& sys : st.drawing.cadPipingSystems) {
      log.push_back("PIPESYS - \"" + sys.name + "\": " + std::to_string(sys.pipeRunIndices.size()) +
                    " run(s).");
    }
  };

  if (verb.empty() || verb == "list") {
    listAll();
    return;
  }

  if (verb == "new") {
    std::string name;
    std::getline(iss, name);
    name = StringUtil::trimCopy(name);
    if (name.empty()) {
      log.push_back("PIPESYS NEW - a network needs a name.");
      return;
    }
    if (FindPipingSystemByName(st, name) >= 0) {
      log.push_back("PIPESYS NEW - a network named \"" + name + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Create Piping Network");
    CadPipingSystem sys;
    sys.name = name;
    st.drawing.cadPipingSystems.push_back(std::move(sys));
    log.push_back("PIPESYS - network \"" + name + "\" created.");
    return;
  }

  if (verb == "add" || verb == "remove") {
    std::string name;
    std::getline(iss, name);
    name = StringUtil::trimCopy(name);
    if (name.empty()) {
      log.push_back("PIPESYS " + verb + " - name the network to " + verb + " to, e.g. PIPESYS " + verb +
                    " Cooling Loop 1.");
      return;
    }
    const int sysIx = FindPipingSystemByName(st, name);
    if (sysIx < 0) {
      log.push_back("PIPESYS " + verb + " - no network named \"" + name + "\". PIPESYS NEW " + name +
                    " first, or PIPESYS LIST to see what exists.");
      return;
    }
    const std::vector<int> picked = SelectedPipeRunIndices(st);
    if (picked.empty()) {
      log.push_back("PIPESYS " + verb + " - select one or more pipe runs first.");
      return;
    }
    PushUndoSnapshot(st, verb == "add" ? "Add To Piping Network" : "Remove From Piping Network");
    CadPipingSystem& target = st.drawing.cadPipingSystems[static_cast<size_t>(sysIx)];
    if (verb == "add") {
      // A run belongs to at most one network (CadPipingSystem's own doc comment) — drop it from
      // whichever network already holds it before it joins this one, rather than refusing or
      // silently duplicating the reference.
      for (CadPipingSystem& other : st.drawing.cadPipingSystems) {
        for (int idx : picked) {
          auto& runs = other.pipeRunIndices;
          runs.erase(std::remove(runs.begin(), runs.end(), idx), runs.end());
        }
      }
      for (int idx : picked)
        target.pipeRunIndices.push_back(idx);
      std::sort(target.pipeRunIndices.begin(), target.pipeRunIndices.end());
      target.pipeRunIndices.erase(std::unique(target.pipeRunIndices.begin(), target.pipeRunIndices.end()),
                                  target.pipeRunIndices.end());
    } else {
      for (int idx : picked) {
        auto& runs = target.pipeRunIndices;
        runs.erase(std::remove(runs.begin(), runs.end(), idx), runs.end());
      }
    }
    log.push_back("PIPESYS - " + std::to_string(picked.size()) + " run(s) " +
                  (verb == "add" ? "added to " : "removed from ") + "\"" + name + "\".");
    return;
  }

  if (verb == "rename") {
    // Names may contain spaces on both sides of the split, so a single-token old-name read
    // (`iss >> oldName`) would truncate "Loop A" to just "Loop". Instead, match the REST of the
    // line against every existing network's name as a prefix, and take the LONGEST match — the
    // same greedy-longest-prefix trick avoids ambiguity between "Loop A" and "Loop A Extra" both
    // being valid prefixes of the typed line.
    std::string rest;
    std::getline(iss, rest);
    rest = StringUtil::trimCopy(rest);
    int sysIx = -1;
    size_t oldNameLen = 0;
    for (size_t i = 0; i < st.drawing.cadPipingSystems.size(); ++i) {
      const std::string& candidate = st.drawing.cadPipingSystems[i].name;
      if (rest.size() > candidate.size() && rest.compare(0, candidate.size(), candidate) == 0 &&
          rest[candidate.size()] == ' ' && candidate.size() > oldNameLen) {
        sysIx = static_cast<int>(i);
        oldNameLen = candidate.size();
      }
    }
    if (sysIx < 0) {
      log.push_back("PIPESYS RENAME - usage: PIPESYS RENAME <old name> <new name>; "
                    "<old name> must exactly match an existing network.");
      return;
    }
    const std::string oldName = st.drawing.cadPipingSystems[static_cast<size_t>(sysIx)].name;
    std::string newName = StringUtil::trimCopy(rest.substr(oldNameLen));
    if (newName.empty()) {
      log.push_back("PIPESYS RENAME - a new name is required.");
      return;
    }
    if (FindPipingSystemByName(st, newName) >= 0) {
      log.push_back("PIPESYS RENAME - a network named \"" + newName + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Rename Piping Network");
    st.drawing.cadPipingSystems[static_cast<size_t>(sysIx)].name = newName;
    log.push_back("PIPESYS - \"" + oldName + "\" renamed to \"" + newName + "\".");
    return;
  }

  if (verb == "delete") {
    std::string name;
    std::getline(iss, name);
    name = StringUtil::trimCopy(name);
    const int sysIx = FindPipingSystemByName(st, name);
    if (sysIx < 0) {
      log.push_back("PIPESYS DELETE - no network named \"" + name + "\".");
      return;
    }
    PushUndoSnapshot(st, "Delete Piping Network");
    // Deleting a NETWORK never deletes its pipe runs — they simply become unassigned, the same
    // "container owns no geometry" split CadPipingSystem's own doc comment states.
    st.drawing.cadPipingSystems.erase(st.drawing.cadPipingSystems.begin() + sysIx);
    log.push_back("PIPESYS - network \"" + name + "\" deleted (its pipe runs are unaffected).");
    return;
  }

  log.push_back("PIPESYS - unknown option \"" + verb +
                "\". Use NEW, ADD, REMOVE, RENAME, DELETE or LIST.");
}

// ---------------------------------------------------------------------------------------------
// PIPEFIT (issue #486 increment B7 / REQ-345) — manual fitting placement on an already-routed run.
// Unlike B5/B6 (automatic, triggered at PIPERUN commit time), this is a user-directed splice into
// an EXISTING CadPipeRun: pick the run (via ordinary selection, like PIPESYS ADD), name a part
// type, then pick a station point anywhere along the run's own path.
// ---------------------------------------------------------------------------------------------

namespace {

/// Nearest point to \p pick on the polyline \p run, clamped to each segment (never extrapolated
/// past an endpoint). \return the owning segment index (`verts[i]`..`verts[i+1]`) and the point
/// itself; false if \p run has fewer than 2 vertices.
bool NearestPointOnPipeRun(const CadPipeRun& run, const ray3d::Vec3& pick, size_t* segIndex,
                           ray3d::Vec3* out) {
  const size_t n = run.vertsXyz.size() / 3;
  if (n < 2)
    return false;
  double bestDist = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i + 1 < n; ++i) {
    const ray3d::Vec3 a{run.vertsXyz[i * 3 + 0], run.vertsXyz[i * 3 + 1], run.vertsXyz[i * 3 + 2]};
    const ray3d::Vec3 b{run.vertsXyz[(i + 1) * 3 + 0], run.vertsXyz[(i + 1) * 3 + 1],
                        run.vertsXyz[(i + 1) * 3 + 2]};
    const ray3d::Vec3 ab = ray3d::Sub(b, a);
    const double abLen2 = ray3d::Dot(ab, ab);
    double t = abLen2 > 1e-18 ? ray3d::Dot(ray3d::Sub(pick, a), ab) / abLen2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const ray3d::Vec3 cand = ray3d::Add(a, ray3d::Scale(ab, t));
    const double dist = ray3d::Length(ray3d::Sub(pick, cand));
    if (dist < bestDist) {
      bestDist = dist;
      *segIndex = i;
      *out = cand;
    }
  }
  return true;
}

/// Splices \p partType into `st.drawing.cadPipeRuns[runIdx]` at the point of \p run nearest \p pick.
/// Reuses B5's own `PickElbowPorts` (an inline 2-port, Inlet/Outlet-tagged fitting is exactly what
/// a mid-span valve/flange/reducer/coupling needs too) and the single-port `CadBlockSnapInsertTo
/// Connection` primitive — NOT B5/B6's two-port rigid alignment: a round pipe's cross-section is
/// rotationally symmetric, so unlike an elbow or a tee, nothing here needs the roll fixed (REQ-346
/// makes the same observation for why a pipe roll prompt has no fitting to attach to yet). The
/// outlet's resulting world position is trusted from the fitting's own solved geometry, the same
/// "the model decides" reasoning B5/B6 both use for their own unpinned far side(s).
bool TrySplicePipeFitNamed(AppCommandState& st, int runIdx, const std::string& blockName,
                           const ray3d::Vec3& pick, std::vector<std::string>& log) {
  if (runIdx < 0 || static_cast<size_t>(runIdx) >= st.drawing.cadPipeRuns.size())
    return false;
  const CadPipeRun& run = st.drawing.cadPipeRuns[static_cast<size_t>(runIdx)];
  size_t segIx = 0;
  ray3d::Vec3 station;
  if (!NearestPointOnPipeRun(run, pick, &segIx, &station)) {
    log.push_back("PIPEFIT - the selected run no longer has a valid path.");
    return false;
  }
  const ray3d::Vec3 a{run.vertsXyz[segIx * 3 + 0], run.vertsXyz[segIx * 3 + 1], run.vertsXyz[segIx * 3 + 2]};
  const ray3d::Vec3 b{run.vertsXyz[(segIx + 1) * 3 + 0], run.vertsXyz[(segIx + 1) * 3 + 1],
                      run.vertsXyz[(segIx + 1) * 3 + 2]};
  const double availA = ray3d::Length(ray3d::Sub(station, a));
  const double availB = ray3d::Length(ray3d::Sub(b, station));
  const ray3d::Vec3 segDir = ray3d::Normalize(ray3d::Sub(b, a));
  if (ray3d::Length(ray3d::Sub(b, a)) < 1e-9) {
    log.push_back("PIPEFIT - that point falls on a degenerate segment of the run.");
    return false;
  }

  const int di = CadBlockFindDef(st.drawing.blockDefs, blockName);
  if (di < 0)
    return false;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  const CadBlockConnection* inletC = nullptr;
  const CadBlockConnection* outletC = nullptr;
  PickElbowPorts(def, &inletC, &outletC);
  if (!inletC || !outletC) {
    log.push_back("PIPEFIT - \"" + blockName + "\" needs exactly two connection ports to splice "
                  "inline.");
    return false;
  }

  const auto engagementOf = [](const CadBlockConnection& c) {
    const CadBlockConnectionMode* m = CadBlockResolveMode(c, CadConnectionModeTarget::PipeEnd);
    return m ? m->engagementLength : c.engagementLength;
  };
  const double cutA = engagementOf(*inletC);
  const double cutB = engagementOf(*outletC);
  if (cutA < 0.0 || cutA > 0.9 * availA || cutB < 0.0 || cutB > 0.9 * availB) {
    log.push_back("PIPEFIT - \"" + blockName + "\"'s engagement length does not fit here.");
    return false;
  }

  const ray3d::Vec3 nearPoint = ray3d::Sub(station, ray3d::Scale(segDir, cutA));
  CadBlockXform xf;
  xf.sx = xf.sy = xf.sz = CadBlockInsertUnitsScale(st, def);
  CadBlockSnapInsertToConnection(*inletC, static_cast<float>(nearPoint.x), static_cast<float>(nearPoint.y),
                                 static_cast<float>(nearPoint.z), static_cast<float>(segDir.x),
                                 static_cast<float>(segDir.y), static_cast<float>(segDir.z), &xf);
  float ox = 0.f, oy = 0.f, oz = 0.f;
  CadBlockXformPoint(xf, outletC->x, outletC->y, outletC->z, &ox, &oy, &oz);
  const ray3d::Vec3 farPoint{ox, oy, oz};

  // Build both pieces: the FIRST keeps every vertex up to and including `a`, plus the cutback near
  // point; the SECOND starts at the far point and keeps every vertex from `b` onward. Either side
  // can be as short as 2 vertices (segIx at the very start/end of the run).
  CadPipeRun piece1;
  piece1.nominalSize = run.nominalSize;
  piece1.wallThicknessIn = run.wallThicknessIn;  // a piece of a pipe has the pipe's wall
  piece1.pressureClassTag = run.pressureClassTag;
  for (size_t i = 0; i <= segIx; ++i) {
    piece1.vertsXyz.push_back(run.vertsXyz[i * 3 + 0]);
    piece1.vertsXyz.push_back(run.vertsXyz[i * 3 + 1]);
    piece1.vertsXyz.push_back(run.vertsXyz[i * 3 + 2]);
  }
  piece1.vertsXyz.push_back(nearPoint.x);
  piece1.vertsXyz.push_back(nearPoint.y);
  piece1.vertsXyz.push_back(nearPoint.z);

  CadPipeRun piece2;
  piece2.nominalSize = run.nominalSize;
  piece2.wallThicknessIn = run.wallThicknessIn;
  piece2.pressureClassTag = run.pressureClassTag;
  piece2.vertsXyz.push_back(farPoint.x);
  piece2.vertsXyz.push_back(farPoint.y);
  piece2.vertsXyz.push_back(farPoint.z);
  const size_t nVerts = run.vertsXyz.size() / 3;
  for (size_t i = segIx + 1; i < nVerts; ++i) {
    piece2.vertsXyz.push_back(run.vertsXyz[i * 3 + 0]);
    piece2.vertsXyz.push_back(run.vertsXyz[i * 3 + 1]);
    piece2.vertsXyz.push_back(run.vertsXyz[i * 3 + 2]);
  }

  std::vector<CadSolidPtr> preview;
  if (!CadBuildPipeRunSolids(piece1, &preview) || !CadBuildPipeRunSolids(piece2, &preview)) {
    log.push_back("PIPEFIT - could not build a pipe solid for one of the resulting pieces here.");
    return false;
  }

  // The fitting and the far piece are the same line as the run they split: they take its layer and
  // colour — an override included — not the current ones (REQ-353). A reducer therefore reads as
  // the run it was spliced into (issue #564 Q4).
  const EntityAttributes lineAttrs = DuplicatedEntityAttrs(
      static_cast<size_t>(runIdx) < st.drawing.cadPipeRunAttrs.size() ? st.drawing.cadPipeRunAttrs[static_cast<size_t>(runIdx)]
                                                               : MakeNewPipeRunAttrs(st, run.nominalSize));
  PushUndoSnapshot(st, "Insert Pipe Fitting");
  st.drawing.cadPipeRuns[static_cast<size_t>(runIdx)] = std::move(piece1);  // keeps the original's index
  st.drawing.cadPipeRuns.push_back(std::move(piece2));
  st.drawing.cadPipeRunAttrs.push_back(lineAttrs);
  const int piece2Idx = static_cast<int>(st.drawing.cadPipeRuns.size()) - 1;
  for (CadPipingSystem& sys : st.drawing.cadPipingSystems) {
    if (std::find(sys.pipeRunIndices.begin(), sys.pipeRunIndices.end(), runIdx) !=
        sys.pipeRunIndices.end()) {
      sys.pipeRunIndices.push_back(piece2Idx);
      std::sort(sys.pipeRunIndices.begin(), sys.pipeRunIndices.end());
    }
  }
  CadBlockPlaceInsertNoUndo(st, blockName, xf, log, &lineAttrs);
  BumpCadGpuCache(st);
  log.push_back("PIPEFIT - \"" + blockName + "\" inserted, run split into 2 pieces.");
  return true;
}

/// PIPEFIT's original entry point, now a thin wrapper: resolve `(part type, the run's own size and
/// class)` to ONE library part through the catalog, then splice that part by name.
///
/// Split out for REQ-350 (f), which needs the second half on its own: the Pipe Fittings palette has
/// already picked a SPECIFIC part, and `CadPipeCatalogFind` deliberately refuses when more than one
/// part matches — which is exactly the case the bundled library presents (two 2in flanges). Behaviour
/// here is unchanged, including that refusal, so `PIPEFIT <part type>` works precisely as before.
bool TrySplicePipeFit(AppCommandState& st, int runIdx, CadPipePartType partType,
                      const ray3d::Vec3& pick, std::vector<std::string>& log) {
  if (runIdx < 0 || static_cast<size_t>(runIdx) >= st.drawing.cadPipeRuns.size())
    return false;
  const CadPipeRun& run = st.drawing.cadPipeRuns[static_cast<size_t>(runIdx)];
  std::string blockName;
  if (!CadPipeCatalogFind(st, partType, run.nominalSize, ParseCadPipePressureClass(run.pressureClassTag),
                          &blockName, log))
    return false;
  return TrySplicePipeFitNamed(st, runIdx, blockName, pick, log);
}

/// How far off a run's centreline a click still counts as "on the pipe" (REQ-350 (f)), as a multiple
/// of the pipe's own outer radius. Slightly generous, because the alternative failure — a click aimed
/// at the pipe that falls through to a free INSERT beside it — is the more annoying of the two, and
/// both are one undo away.
constexpr double kCadPipeRunPickRadiusSlack = 1.5;

} // namespace

bool CadPipeRunUnderPick(const AppCommandState& st, const ray3d::Vec3& pick, int* outRunIdx) {
  if (outRunIdx == nullptr)
    return false;
  *outRunIdx = -1;
  double best = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < st.drawing.cadPipeRuns.size(); ++i) {
    const CadPipeRun& run = st.drawing.cadPipeRuns[i];
    size_t seg = 0;
    ray3d::Vec3 station{};
    if (!NearestPointOnPipeRun(run, pick, &seg, &station))
      continue;
    double odFeet = 0.0;
    if (!CadPipeNominalOdFeet(run.nominalSize, &odFeet))
      continue;  // a run whose size does not resolve draws no pipe to click on
    const double dist = ray3d::Length(ray3d::Sub(pick, station));
    if (dist > odFeet * 0.5 * kCadPipeRunPickRadiusSlack)
      continue;
    if (dist < best) {
      best = dist;
      *outRunIdx = static_cast<int>(i);
    }
  }
  return *outRunIdx >= 0;
}

bool CadPipeFitNamedAtPick(AppCommandState& st, int runIdx, const std::string& blockName,
                           const ray3d::Vec3& pick, std::vector<std::string>& log) {
  return TrySplicePipeFitNamed(st, runIdx, blockName, pick, log);
}

void StartPipeFitCommand(AppCommandState& st, const std::string& partTypeTok, std::vector<std::string>& log) {
  const std::vector<int> picked = SelectedPipeRunIndices(st);
  if (picked.size() != 1) {
    log.push_back("PIPEFIT - select exactly one pipe run first.");
    return;
  }
  if (partTypeTok.empty()) {
    log.push_back("PIPEFIT - usage: PIPEFIT <part type>. Part types: elbow-90, elbow-45, tee, "
                  "cross, reducer, flange, valve, coupling, cap, nozzle, other.");
    return;
  }
  const CadPipePartType partType = ParseCadPipePartType(StringUtil::toLowerAsciiCopy(partTypeTok));
  if (partType == CadPipePartType::None) {
    log.push_back("PIPEFIT - unknown part type \"" + partTypeTok + "\".");
    return;
  }
  auto sess = std::make_unique<stepengine::PipeFitSession>();
  sess->runIndex = picked[0];
  sess->partType = partType;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("PIPEFIT - pick a point on the selected run, or ESC to cancel.");
}

void SubmitPipeFitViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* pf = st.sessionAs<stepengine::PipeFitSession>();
  if (!pf)
    return;
  const int runIdx = pf->runIndex;
  const CadPipePartType partType = pf->partType;
  const ray3d::Vec3 pt{static_cast<double>(wx), static_cast<double>(wy), CadCommitElevation(st)};
  TrySplicePipeFit(st, runIdx, partType, pt, log);
  stepengine::EndSession(st);
}

bool HandlePipeFitTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  auto* pf = st.sessionAs<stepengine::PipeFitSession>();
  if (!pf)
    return false;
  ray3d::Vec3 pt{};
  if (!ParseSolidBasePoint(st, line, &pt, log, "PIPEFIT"))
    return true;
  const int runIdx = pf->runIndex;
  const CadPipePartType partType = pf->partType;
  TrySplicePipeFit(st, runIdx, partType, pt, log);
  stepengine::EndSession(st);
  return true;
}

// ---------------------------------------------------------------------------------------------
// PIPESPLIT / PIPEJOIN / PIPEPROP (issue #486 increment B8 / REQ-345) — the rest of Track B's
// named edit operations: a plain split (no fitting — PIPEFIT's own splice covers the WITH-a-part
// case), merging two runs back into one, and changing size/class on already-routed runs.
//
// **Deliberately out of scope, by name, recorded here rather than re-litigated at each site**:
// grip-editing a single vertex in the viewport (the existing "CAD ENTITY GRIPS" system —
// `AppCommandState::entityGripType` and friends — is 2D-only, local-X/Y storage with no Z field
// anywhere in its drag state; giving a `CadPipeRun` vertex a live 3D drag, including what happens
// to any fitting already spliced onto that vertex's segment, is its own substantial increment, not
// a corner of this one) and "replace fitting" (which existing `CadBlockRef` counts as "the"
// fitting on a run is not well-defined without picking it explicitly, and DELETE + PIPEFIT +
// PIPEJOIN already cover the same end result: remove the old block, PIPEJOIN the two pipe pieces
// it used to sit between back into one run, PIPEFIT the replacement part back in).
// ---------------------------------------------------------------------------------------------

void StartPipeSplitCommand(AppCommandState& st, std::vector<std::string>& log) {
  const std::vector<int> picked = SelectedPipeRunIndices(st);
  if (picked.size() != 1) {
    log.push_back("PIPESPLIT - select exactly one pipe run first.");
    return;
  }
  auto sess = std::make_unique<stepengine::PipeSplitSession>();
  sess->runIndex = picked[0];
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("PIPESPLIT - pick a point on the selected run, or ESC to cancel.");
}

namespace {

/// Splits `st.drawing.cadPipeRuns[runIdx]` at the point of the run nearest \p pick — no fitting, no
/// engagement cutback, the pieces meet exactly at the projected station. Shares `NearestPointOn
/// PipeRun` with `TrySplicePipeFit`; unlike that function there is no catalog part, so orientation
/// and port resolution simply do not apply here.
bool TrySplitPipeRun(AppCommandState& st, int runIdx, const ray3d::Vec3& pick, std::vector<std::string>& log) {
  if (runIdx < 0 || static_cast<size_t>(runIdx) >= st.drawing.cadPipeRuns.size())
    return false;
  const CadPipeRun& run = st.drawing.cadPipeRuns[static_cast<size_t>(runIdx)];
  size_t segIx = 0;
  ray3d::Vec3 station;
  if (!NearestPointOnPipeRun(run, pick, &segIx, &station)) {
    log.push_back("PIPESPLIT - the selected run no longer has a valid path.");
    return false;
  }

  CadPipeRun piece1;
  piece1.nominalSize = run.nominalSize;
  piece1.wallThicknessIn = run.wallThicknessIn;  // a piece of a pipe has the pipe's wall
  piece1.pressureClassTag = run.pressureClassTag;
  for (size_t i = 0; i <= segIx; ++i) {
    piece1.vertsXyz.push_back(run.vertsXyz[i * 3 + 0]);
    piece1.vertsXyz.push_back(run.vertsXyz[i * 3 + 1]);
    piece1.vertsXyz.push_back(run.vertsXyz[i * 3 + 2]);
  }
  piece1.vertsXyz.push_back(station.x);
  piece1.vertsXyz.push_back(station.y);
  piece1.vertsXyz.push_back(station.z);

  CadPipeRun piece2;
  piece2.nominalSize = run.nominalSize;
  piece2.wallThicknessIn = run.wallThicknessIn;
  piece2.pressureClassTag = run.pressureClassTag;
  piece2.vertsXyz.push_back(station.x);
  piece2.vertsXyz.push_back(station.y);
  piece2.vertsXyz.push_back(station.z);
  const size_t nVerts = run.vertsXyz.size() / 3;
  for (size_t i = segIx + 1; i < nVerts; ++i) {
    piece2.vertsXyz.push_back(run.vertsXyz[i * 3 + 0]);
    piece2.vertsXyz.push_back(run.vertsXyz[i * 3 + 1]);
    piece2.vertsXyz.push_back(run.vertsXyz[i * 3 + 2]);
  }

  std::vector<CadSolidPtr> preview;
  if (!CadBuildPipeRunSolids(piece1, &preview) || !CadBuildPipeRunSolids(piece2, &preview)) {
    log.push_back("PIPESPLIT - could not build a pipe solid for one of the resulting pieces here.");
    return false;
  }

  // The far piece is the same line: it keeps the run's layer and colour, an override included (REQ-353).
  const EntityAttributes lineAttrs = DuplicatedEntityAttrs(
      static_cast<size_t>(runIdx) < st.drawing.cadPipeRunAttrs.size() ? st.drawing.cadPipeRunAttrs[static_cast<size_t>(runIdx)]
                                                               : MakeNewPipeRunAttrs(st, run.nominalSize));
  PushUndoSnapshot(st, "Split Pipe Run");
  st.drawing.cadPipeRuns[static_cast<size_t>(runIdx)] = std::move(piece1);
  st.drawing.cadPipeRuns.push_back(std::move(piece2));
  st.drawing.cadPipeRunAttrs.push_back(lineAttrs);
  const int piece2Idx = static_cast<int>(st.drawing.cadPipeRuns.size()) - 1;
  for (CadPipingSystem& sys : st.drawing.cadPipingSystems) {
    if (std::find(sys.pipeRunIndices.begin(), sys.pipeRunIndices.end(), runIdx) !=
        sys.pipeRunIndices.end()) {
      sys.pipeRunIndices.push_back(piece2Idx);
      std::sort(sys.pipeRunIndices.begin(), sys.pipeRunIndices.end());
    }
  }
  BumpCadGpuCache(st);
  log.push_back("PIPESPLIT - run split into 2 pieces.");
  return true;
}

} // namespace

void SubmitPipeSplitViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeSplitSession>();
  if (!ps)
    return;
  const int runIdx = ps->runIndex;
  const ray3d::Vec3 pt{static_cast<double>(wx), static_cast<double>(wy), CadCommitElevation(st)};
  TrySplitPipeRun(st, runIdx, pt, log);
  stepengine::EndSession(st);
}

bool HandlePipeSplitTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PipeSplitSession>();
  if (!ps)
    return false;
  ray3d::Vec3 pt{};
  if (!ParseSolidBasePoint(st, line, &pt, log, "PIPESPLIT"))
    return true;
  const int runIdx = ps->runIndex;
  TrySplitPipeRun(st, runIdx, pt, log);
  stepengine::EndSession(st);
  return true;
}

namespace {

/// Erases `cadPipeRuns[idx]` and reindexes every `CadPipingSystem` reference above it — the exact
/// same shape `ExecuteDeleteSelection` already uses for a pipe-run erase, pulled out here so
/// `HandlePipeJoinCommand` does not have to duplicate it for the one run a merge consumes.
void EraseOnePipeRunReindexed(AppCommandState& st, int idx) {
  st.drawing.cadPipeRuns.erase(st.drawing.cadPipeRuns.begin() + static_cast<std::ptrdiff_t>(idx));
  if (static_cast<size_t>(idx) < st.drawing.cadPipeRunAttrs.size())
    st.drawing.cadPipeRunAttrs.erase(st.drawing.cadPipeRunAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  for (CadPipingSystem& sys : st.drawing.cadPipingSystems) {
    auto& runs = sys.pipeRunIndices;
    runs.erase(std::remove(runs.begin(), runs.end(), idx), runs.end());
    for (int& ri : runs) {
      if (ri > idx)
        --ri;
    }
  }
}

} // namespace

void HandlePipeJoinCommand(AppCommandState& st, std::vector<std::string>& log) {
  const std::vector<int> picked = SelectedPipeRunIndices(st);
  if (picked.size() != 2) {
    log.push_back("PIPEJOIN - select exactly two pipe runs first.");
    return;
  }
  const CadPipeRun& ra = st.drawing.cadPipeRuns[static_cast<size_t>(picked[0])];
  const CadPipeRun& rb = st.drawing.cadPipeRuns[static_cast<size_t>(picked[1])];
  if (ra.nominalSize != rb.nominalSize || ra.pressureClassTag != rb.pressureClassTag) {
    log.push_back("PIPEJOIN - the two runs must share the same nominal size and pressure class.");
    return;
  }
  const size_t na = ra.vertsXyz.size() / 3;
  const size_t nb = rb.vertsXyz.size() / 3;
  if (na < 2 || nb < 2) {
    log.push_back("PIPEJOIN - one of the selected runs has no valid path.");
    return;
  }
  const ray3d::Vec3 aStart{ra.vertsXyz[0], ra.vertsXyz[1], ra.vertsXyz[2]};
  const ray3d::Vec3 aEnd{ra.vertsXyz[(na - 1) * 3 + 0], ra.vertsXyz[(na - 1) * 3 + 1],
                         ra.vertsXyz[(na - 1) * 3 + 2]};
  const ray3d::Vec3 bStart{rb.vertsXyz[0], rb.vertsXyz[1], rb.vertsXyz[2]};
  const ray3d::Vec3 bEnd{rb.vertsXyz[(nb - 1) * 3 + 0], rb.vertsXyz[(nb - 1) * 3 + 1],
                         rb.vertsXyz[(nb - 1) * 3 + 2]};
  constexpr double kEps = 1e-6;
  const bool endStart = ray3d::Length(ray3d::Sub(aEnd, bStart)) < kEps;   // A end -> B start
  const bool endEnd = ray3d::Length(ray3d::Sub(aEnd, bEnd)) < kEps;       // A end -> B end
  const bool startStart = ray3d::Length(ray3d::Sub(aStart, bStart)) < kEps;  // A start -> B start
  const bool startEnd = ray3d::Length(ray3d::Sub(aStart, bEnd)) < kEps;   // A start -> B end
  const int matchCount = (endStart ? 1 : 0) + (endEnd ? 1 : 0) + (startStart ? 1 : 0) + (startEnd ? 1 : 0);
  if (matchCount != 1) {
    log.push_back("PIPEJOIN - the two runs must share EXACTLY one coincident endpoint.");
    return;
  }

  std::vector<double> merged;
  merged.reserve(ra.vertsXyz.size() + rb.vertsXyz.size());
  const auto appendVerts = [&merged](const std::vector<double>& v, bool reversed, bool skipFirst) {
    const size_t n = v.size() / 3;
    for (size_t k = 0; k < n; ++k) {
      const size_t i = reversed ? (n - 1 - k) : k;
      if (skipFirst && k == 0)
        continue;
      merged.push_back(v[i * 3 + 0]);
      merged.push_back(v[i * 3 + 1]);
      merged.push_back(v[i * 3 + 2]);
    }
  };
  if (endStart) {
    appendVerts(ra.vertsXyz, /*reversed=*/false, /*skipFirst=*/false);
    appendVerts(rb.vertsXyz, /*reversed=*/false, /*skipFirst=*/true);
  } else if (endEnd) {
    appendVerts(ra.vertsXyz, /*reversed=*/false, /*skipFirst=*/false);
    appendVerts(rb.vertsXyz, /*reversed=*/true, /*skipFirst=*/true);
  } else if (startStart) {
    appendVerts(ra.vertsXyz, /*reversed=*/true, /*skipFirst=*/false);
    appendVerts(rb.vertsXyz, /*reversed=*/false, /*skipFirst=*/true);
  } else {  // startEnd
    appendVerts(rb.vertsXyz, /*reversed=*/false, /*skipFirst=*/false);
    appendVerts(ra.vertsXyz, /*reversed=*/false, /*skipFirst=*/true);
  }

  CadPipeRun mergedRun;
  mergedRun.vertsXyz = merged;
  mergedRun.nominalSize = ra.nominalSize;
  mergedRun.wallThicknessIn = ra.wallThicknessIn;
  mergedRun.pressureClassTag = ra.pressureClassTag;
  std::vector<CadSolidPtr> preview;
  if (!CadBuildPipeRunSolids(mergedRun, &preview)) {
    log.push_back("PIPEJOIN - could not build a pipe solid for the merged run — the new joint may "
                  "be too tight for this size's fillet radius.");
    return;
  }

  // The lower index survives (keeps whatever network membership/selection it already had); the
  // higher index is erased and reindexed — the same "near piece stays put" precedent PIPEFIT and
  // PIPESPLIT both established for their own surviving index.
  const int keepIdx = std::min(picked[0], picked[1]);
  const int dropIdx = std::max(picked[0], picked[1]);
  PushUndoSnapshot(st, "Join Pipe Runs");
  st.drawing.cadPipeRuns[static_cast<size_t>(keepIdx)] = std::move(mergedRun);
  EraseOnePipeRunReindexed(st, dropIdx);
  st.selection.clear();
  BumpCadGpuCache(st);
  log.push_back("PIPEJOIN - runs merged into one.");
}

void HandlePipePropCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log) {
  const std::vector<int> picked = SelectedPipeRunIndices(st);
  if (picked.empty()) {
    log.push_back("PIPEPROP - select one or more pipe runs first.");
    return;
  }
  std::istringstream iss(args);
  std::string sizeTok, classTok;
  iss >> sizeTok;
  // After the size, a bare number is a WALL THICKNESS in inches (D-2026-09-23-a) and anything else
  // is a pressure class — the two cannot be confused, and this keeps `PIPEPROP 4in CS150` working
  // exactly as it did while `PIPEPROP 4in 0.5` and `PIPEPROP 4in CS150 0.5` both reach the wall.
  bool wallGiven = false;
  double wallIn = 0.0;
  for (std::string tok; iss >> tok;) {
    char* end = nullptr;
    const double asNumber = std::strtod(tok.c_str(), &end);
    if (end && *end == '\0' && !tok.empty()) {
      wallGiven = true;
      wallIn = asNumber;
    } else {
      classTok = tok;
    }
  }
  double odFeet = 0.0;
  if (!CadPipeNominalOdFeet(sizeTok, &odFeet)) {
    log.push_back("PIPEPROP - unknown nominal size \"" + sizeTok +
                  "\". Known NPS sizes: 0.5in, 0.75in, 1in, 1.25in, 1.5in, 2in, 2.5in, 3in, 4in, "
                  "6in, 8in, 10in, 12in.");
    return;
  }
  if (wallGiven && (!std::isfinite(wallIn) || !(wallIn > 0.0) || !(wallIn < odFeet * 6.0))) {
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "PIPEPROP - a %.3fin wall leaves no bore in %s pipe (OD %.3fin); it must be over 0 "
                  "and under %.3fin.",
                  wallIn, sizeTok.c_str(), odFeet * 12.0, odFeet * 6.0);
    log.push_back(msg);
    return;
  }
  std::string classTag;
  if (!classTok.empty()) {
    const CadPipePressureClass pc = ParseCadPipePressureClass(classTok);
    if (pc == CadPipePressureClass::None) {
      log.push_back("PIPEPROP - unknown pressure class \"" + classTok + "\". Use CS150 or CS300.");
      return;
    }
    classTag = std::string(CadPipePressureClassTag(pc));
  }

  // Refit-or-refuse (REQ-201) applies PER RUN, not to the whole selection: these are independent
  // existing entities, not pieces of one atomic commit the way a single PIPERUN/PIPEFIT/PIPESPLIT
  // is, so one run whose new size no longer fits an existing tight corner should not block the
  // others in the same selection from resizing.
  int changed = 0, refused = 0, refusedWall = 0;
  PushUndoSnapshot(st, "Change Pipe Properties");
  for (int idx : picked) {
    CadPipeRun candidate = st.drawing.cadPipeRuns[static_cast<size_t>(idx)];
    candidate.nominalSize = sizeTok;
    candidate.pressureClassTag = classTag;
    if (wallGiven)
      candidate.wallThicknessIn = wallIn;
    // A run keeping its OWN wall through a size change can end up with a wall too thick for the new,
    // smaller pipe. That is a different refusal from a corner that no longer fits, and saying so is
    // the difference between an answer the user can act on (state a thinner wall) and a misleading
    // one about fillet radii.
    double wallFeet = 0.0;
    if (!CadPipeRunWallThicknessFeet(candidate, &wallFeet)) {
      ++refusedWall;
      continue;
    }
    std::vector<CadSolidPtr> preview;
    if (!CadBuildPipeRunSolids(candidate, &preview)) {
      ++refused;
      continue;
    }
    st.drawing.cadPipeRuns[static_cast<size_t>(idx)] = std::move(candidate);
    ++changed;
  }
  BumpCadGpuCache(st);
  const std::string wallNote =
      refusedWall > 0 ? (", " + std::to_string(refusedWall) +
                         " refused (their wall leaves no bore at this size — give a thinner wall)")
                      : std::string();
  if (changed == 0 && refused == 0 && refusedWall > 0) {
    log.push_back("PIPEPROP - no run could be resized to " + sizeTok +
                  ": their wall leaves no bore at this size. Give a thinner wall, e.g. PIPEPROP " +
                  sizeTok + " 0.1.");
  } else if (changed == 0) {
    log.push_back("PIPEPROP - no run could be resized to " + sizeTok + (classTag.empty() ? "" : " " + classTag) +
                  " — the new size does not fit an existing corner's fillet radius.");
  } else if (refused == 0 && refusedWall == 0) {
    log.push_back("PIPEPROP - " + std::to_string(changed) + " run(s) updated.");
  } else if (refused == 0) {
    log.push_back("PIPEPROP - " + std::to_string(changed) + " run(s) updated" + wallNote + ".");
  } else {
    log.push_back("PIPEPROP - " + std::to_string(changed) + " run(s) updated, " + std::to_string(refused) +
                  " refused (new size does not fit an existing corner's fillet radius)" + wallNote + ".");
  }
}
