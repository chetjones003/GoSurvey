#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// `SECTION` — the cross-section of every selected solid by the **active UCS plane**, drawn as a
/// closed polyline, leaving the solids alone (REQ-335, GitHub #149 acceptance 5).
///
/// **The plane is the active UCS, not three picked points, and that is a decision rather than a
/// shortcut** (D-2026-09-09-i). `ucs::Ucs` IS this project's plane abstraction (REQ-311,
/// D-2026-08-31-e), and since REQ-329 every modify command already resolves its work into the
/// active UCS plane — so "set the work plane, then ask what the section looks like" is the gesture
/// this codebase already teaches. SLICE's three-point form is the natural second increment for the
/// case where a user wants a one-off plane without moving the UCS; REQ-335 records it as such.
///
/// One closed polyline per solid, on the current layer, in one undo step. Arcs ride as **bulges**
/// (REQ-316 / ADR-047), so a cylinder's circular section is a circle and not a polygon — the
/// kernel hands back a `brep::Path` of lines and arcs precisely so nothing has to be flattened
/// here.
/// Section \p solids by the plane through \p planePoint with normal \p planeNormal.
///
/// Split out from `CadSectionSelection` so the plane can come from anywhere: the active UCS (the
/// original form, REQ-335 increment 1) or three picked points (increment 2). Everything below this
/// line was already here and is unchanged — only where the plane comes from moved out.
static void CadSectionSolidsByPlane(AppCommandState& st, const std::vector<int>& solids,
                                    const std::vector<std::weak_ptr<const brep::Solid>>& owners,
                                    const ray3d::Vec3& planePoint, const ray3d::Vec3& planeNormal,
                                    std::vector<std::string>& log) {
  // The indices were resolved at the SELECTION step, up to three picks ago, and an index alone is
  // not a durable reference: anything that replaces or reorders `cadSolids` in between — an UNDO,
  // an erase, a tab switch that swaps the whole store — leaves it naming a DIFFERENT solid, and a
  // bounds check cannot see that. So each index is held to the solid it named when it was picked,
  // the way the sub-object selection is (REQ-318), and a changed selection refuses rather than
  // cutting whatever now sits at that slot (code review on #478, finding 9).
  for (std::size_t k = 0; k < solids.size(); ++k) {
    const int idx = solids[k];
    const bool same = k < owners.size() && idx >= 0 && static_cast<std::size_t>(idx) < st.drawing.cadSolids.size() &&
                      !owners[k].expired() && owners[k].lock() == st.drawing.cadSolids[static_cast<std::size_t>(idx)];
    if (!same) {
      log.push_back("SECTION — the selected solids changed before the plane was picked. Nothing was "
                    "sectioned; run SECTION again.");
      return;
    }
  }

  // Section everything BEFORE touching the document, so a failure part-way leaves nothing behind
  // (REQ-201) — the same all-or-nothing shape SLICE already uses.
  // One cut can be more than one outline: a ring (a torus cut square to its axis, a drilled box) is
  // an outer outline plus its hole, and each becomes its own closed polyline (REQ-335 increment 2,
  // D-2026-09-18-a, GitHub #520). They are appended together, under the one undo step below.
  struct Cut {
    ucs::Ucs plane;
    std::vector<brep::Path> loops;
    /// A tilted cut of a cylinder or cone is one closed ELLIPSE, which no polyline can hold: it
    /// becomes an `ELLIPSE` entity standing in the cut plane instead (GitHub #531, D-2026-09-23-a).
    brep::SectionEllipse ellipse;
    /// And when that cut runs off the end, it is an elliptical ARC plus the chord across the cap —
    /// two objects for one cut, in one undo step (D-2026-09-23-b).
    brep::SectionEllipseArc ellipseArc;
  };
  std::vector<Cut> cuts;
  cuts.reserve(solids.size());
  for (const int idx : solids) {
    // Already held to its owner above; the range test stays so this loop is safe on its own.
    if (idx < 0 || static_cast<std::size_t>(idx) >= st.drawing.cadSolids.size())
      continue;
    const CadSolidPtr& sp = st.drawing.cadSolids[static_cast<std::size_t>(idx)];
    if (!sp)
      continue;
    Cut c;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::SectionOutlines(*sp, planePoint, planeNormal, &c.plane, &c.loops, &why)) {
      // One shape a section can be that an outline cannot hold: a closed ellipse, from a tilted cut
      // of a cylinder or cone (GitHub #531). Asked for by name rather than guessed at, so every
      // other refusal still arrives with the kernel's own reason, verbatim, and draws nothing.
      brep::Problem ellipseWhy = brep::Problem::Ok;
      const bool wholeEllipse =
          why == brep::Problem::SectionEllipse &&
          brep::SectionEllipseOutline(*sp, planePoint, planeNormal, &c.plane, &c.ellipse, &ellipseWhy);
      // And the cut that runs off the end: an elliptical arc plus the chord across the cap.
      const bool arcAndChord =
          !wholeEllipse && why == brep::Problem::SliceCutCrossesCurvedEnd &&
          brep::SectionEllipseArcOutline(*sp, planePoint, planeNormal, &c.plane, &c.ellipseArc, &ellipseWhy);
      if (!wholeEllipse && !arcAndChord) {
        log.push_back(std::string("SECTION — ") + brep::ProblemText(why));
        return;
      }
    }
    cuts.push_back(std::move(c));
  }
  if (cuts.empty()) {
    log.push_back("SECTION — nothing to section.");
    return;
  }

  const std::size_t undoDepthBefore = CadActiveUndoStackSize(st);
  PushUndoSnapshot(st, "Section");

  int made = 0;
  int failed = 0;
  for (const Cut& c : cuts) {
    if (c.ellipseArc.valid) {
      // Two objects for one cut: the elliptical arc, and the chord that closes it across the cap.
      // Both land under the one undo snapshot taken above (D-2026-09-23-b).
      const ucs::Ucs plane = c.plane;
      const ucs::Point2D centre2d = ucs::WorldToPlane(plane, c.ellipseArc.centre);
      const ucs::Point2D major2d = ucs::WorldToPlane(
          plane, ray3d::Add(c.ellipseArc.centre, ray3d::Scale(c.ellipseArc.majorDir, c.ellipseArc.majorSemi)));
      CadEllipse el{};
      float lx = 0.f, ly = 0.f;
      CadCoord::LocalFromWorld(st, c.ellipseArc.centre.x, c.ellipseArc.centre.y, &lx, &ly);
      el.cx = static_cast<double>(lx);
      el.cy = static_cast<double>(ly);
      el.z = c.ellipseArc.centre.z;
      el.majVx = static_cast<float>(major2d.x - centre2d.x);
      el.majVy = static_cast<float>(major2d.y - centre2d.y);
      el.ratio = static_cast<float>(c.ellipseArc.minorSemi / std::max(c.ellipseArc.majorSemi, 1e-12));
      el.nx = static_cast<float>(c.ellipseArc.normal.x);
      el.ny = static_cast<float>(c.ellipseArc.normal.y);
      el.nz = static_cast<float>(c.ellipseArc.normal.z);
      el.startRad = static_cast<float>(c.ellipseArc.startParam);
      el.sweepRad = static_cast<float>(c.ellipseArc.sweep);
      st.drawing.userEllipses.push_back(el);
      st.drawing.userEllAttrs.push_back(MakeNewEntityAttrs(st));
      float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
      CadCoord::LocalFromWorld(st, c.ellipseArc.chordA.x, c.ellipseArc.chordA.y, &ax, &ay);
      CadCoord::LocalFromWorld(st, c.ellipseArc.chordB.x, c.ellipseArc.chordB.y, &bx, &by);
      st.drawing.pushLine(CadLineRecord{ax, ay, static_cast<float>(c.ellipseArc.chordA.z), bx, by, static_cast<float>(c.ellipseArc.chordB.z)});
      st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
      made += 2;
      continue;
    }
    if (c.ellipse.valid) {
      // The ellipse stands in the cut plane: its centre is a world point, and its major axis is
      // stated in that plane's own axes, which is how `CadEllipse` carries a tilted ellipse
      // (GitHub #531). A flat cut still produces a flat ellipse, byte-identical to before.
      CadEllipse el{};
      const ucs::Ucs plane = c.plane;
      const ucs::Point2D centre2d = ucs::WorldToPlane(plane, c.ellipse.centre);
      const ucs::Point2D major2d = ucs::WorldToPlane(
          plane, ray3d::Add(c.ellipse.centre, ray3d::Scale(c.ellipse.majorDir, c.ellipse.majorSemi)));
      float lx = 0.f, ly = 0.f;
      CadCoord::LocalFromWorld(st, c.ellipse.centre.x, c.ellipse.centre.y, &lx, &ly);
      el.cx = static_cast<double>(lx);
      el.cy = static_cast<double>(ly);
      el.z = c.ellipse.centre.z;
      el.majVx = static_cast<float>(major2d.x - centre2d.x);
      el.majVy = static_cast<float>(major2d.y - centre2d.y);
      el.ratio = static_cast<float>(c.ellipse.minorSemi / std::max(c.ellipse.majorSemi, 1e-12));
      el.nx = static_cast<float>(c.ellipse.normal.x);
      el.ny = static_cast<float>(c.ellipse.normal.y);
      el.nz = static_cast<float>(c.ellipse.normal.z);
      st.drawing.userEllipses.push_back(el);
      st.drawing.userEllAttrs.push_back(MakeNewEntityAttrs(st));
      ++made;
      continue;
    }
    for (const brep::Path& loop : c.loops) {
      // The Path is 2D in its own plane; the polyline store is world XYZ.
      std::vector<float> xyz;
      std::vector<float> bulges;
      const ray3d::Vec3 p0 = ucs::PlaneToWorld(c.plane, loop.start);
      xyz.push_back(static_cast<float>(p0.x));
      xyz.push_back(static_cast<float>(p0.y));
      xyz.push_back(static_cast<float>(p0.z));
      // A closed path's last segment returns to `start`, so its END is not a new vertex — but its
      // BULGE belongs to the closing span and has to be carried, or a circular section would come
      // back as a half-circle and a straight chord.
      for (std::size_t i = 0; i + 1 < loop.segs.size(); ++i) {
        const ray3d::Vec3 w = ucs::PlaneToWorld(c.plane, loop.segs[i].end);
        xyz.push_back(static_cast<float>(w.x));
        xyz.push_back(static_cast<float>(w.y));
        xyz.push_back(static_cast<float>(w.z));
      }
      bulges.reserve(loop.segs.size());
      for (const brep::PathSeg& sg : loop.segs)
        bulges.push_back(static_cast<float>(std::tan(sg.sweep * 0.25)));

      const int before = static_cast<int>(st.drawing.userPolylineOffsets.empty() ? 0 : st.drawing.userPolylineOffsets.back());
      if (AppendXyzPathAsPolyline(st, xyz, /*closed=*/true) != 1) {
        // REQ-201: every case is explicitly reported, not silently dropped.
        log.push_back("SECTION — a solid's section outline failed to append; skipped.");
        ++failed;
        continue;
      }
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
      for (std::size_t i = 0; i < bulges.size() && before + static_cast<int>(i) <
                                                       static_cast<int>(st.drawing.userPolylineVertsBulge.size());
           ++i)
        st.drawing.userPolylineVertsBulge[static_cast<std::size_t>(before) + i] = bulges[i];
      // REQ-325 / ADR-053: each vertex carries the plane its curved segment turns in. A section on a
      // vertical or tilted plane has curved segments in THAT plane — a sphere's vertical section is a
      // circle standing on edge — and the default +Z would draw them lying flat, and export them
      // flat (GitHub #521).
      for (std::size_t i = 0; i < bulges.size(); ++i) {
        const std::size_t k = (static_cast<std::size_t>(before) + i) * 3;
        if (k + 2 >= st.drawing.userPolylineVertsNormal.size())
          break;
        st.drawing.userPolylineVertsNormal[k] = static_cast<float>(c.plane.zAxis.x);
        st.drawing.userPolylineVertsNormal[k + 1] = static_cast<float>(c.plane.zAxis.y);
        st.drawing.userPolylineVertsNormal[k + 2] = static_cast<float>(c.plane.zAxis.z);
      }
      ++made;
    }
  }

  if (made == 0) {
    // Nothing was actually appended — the undo snapshot taken above would otherwise leave a
    // stray no-op step, breaking "a refusal leaves the document unchanged".
    CadTruncateActiveUndoStack(st, undoDepthBefore);
    log.push_back("SECTION — no section outlines could be created; the solids are unchanged.");
    return;
  }

  BumpCadGpuCache(st);
  char msg[192];
  if (failed == 0) {
    std::snprintf(msg, sizeof(msg), "SECTION — %d section outline%s created. The solid%s unchanged.",
                  made, made == 1 ? "" : "s", solids.size() == 1 ? " is" : "s are");
  } else {
    const int attempted = made + failed;
    std::snprintf(msg, sizeof(msg),
                  "SECTION — %d of %d section outline%s created. The solid%s unchanged.", made,
                  attempted, attempted == 1 ? "" : "s", solids.size() == 1 ? " is" : "s are");
  }
  log.push_back(msg);
}

std::string CadSectionPromptText(const AppCommandState& st) {
  switch (st.sectionPhase) {
  case AppCommandState::SectionPhase::SelectSolids:
    return "SECTION — select solids, Enter when done. ESC cancels.";
  case AppCommandState::SectionPhase::WaitP1:
    // `[UCS]` is bracketed so it draws as a clickable option (REQ-040), and it is what keeps
    // increment 1's behaviour reachable: the active work plane, without picking three points.
    return "SECTION — first point on the section plane, or [UCS] for the work plane. ESC cancels.";
  case AppCommandState::SectionPhase::WaitP2:
    return "SECTION — second point on the plane. ESC cancels.";
  case AppCommandState::SectionPhase::WaitP3:
    return "SECTION — third point on the plane. ESC cancels.";
  }
  return "SECTION";
}

/// Resolve the selection into solid indices and move to the plane phase, or say why not.
static void SectionEnterPlanePhase(AppCommandState& st, std::vector<std::string>& log) {
  st.sectionSolidIndices.clear();
  const int nSolid = static_cast<int>(st.drawing.cadSolids.size());
  for (const SelectedEntity& e : st.selection)
    if (e.type == SelectedEntity::Type::Solid && e.index >= 0 && e.index < nSolid)
      st.sectionSolidIndices.push_back(e.index);
  // Deduplicated, as SLICE does with the same data. A solid can reach `selection` twice — a
  // rectangle merged into an existing selection, or a pick-first set carried in from another
  // command — and without this SECTION would cut the same solid twice and lay two identical closed
  // polylines on top of each other, then report "2 section outlines created" for one solid.
  std::sort(st.sectionSolidIndices.begin(), st.sectionSolidIndices.end());
  st.sectionSolidIndices.erase(
      std::unique(st.sectionSolidIndices.begin(), st.sectionSolidIndices.end()),
      st.sectionSolidIndices.end());
  st.sectionSolidOwners.clear();
  for (const int idx : st.sectionSolidIndices)
    st.sectionSolidOwners.push_back(st.drawing.cadSolids[static_cast<std::size_t>(idx)]);
  if (st.sectionSolidIndices.empty()) {
    // Stays in the selection phase rather than ending: the user picked something, it just was not
    // a solid, and throwing them out of the command for that is the behaviour this increment exists
    // to remove.
    log.push_back("SECTION — that is not a solid. Select one or more solids, Enter when done.");
    st.sectionPhase = AppCommandState::SectionPhase::SelectSolids;
    return;
  }
  st.sectionPhase = AppCommandState::SectionPhase::WaitP1;
  log.push_back(CadSectionPromptText(st));
}

void CancelSectionCommand(AppCommandState& st) {
  st.sectionPhase = AppCommandState::SectionPhase::SelectSolids;
  st.sectionSolidIndices.clear();
  st.sectionSolidOwners.clear();
}

/// `SECTION` — choose solids, then define the section plane by three points (REQ-335 increment 2).
///
/// Pick-first is honoured: with solids already selected it goes straight to the plane, which is the
/// convention every modify command in this application already follows.
void StartSectionCommand(AppCommandState& st, std::vector<std::string>& log) {
  CancelSectionCommand(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Section;
  st.lastCommand = AppCommandState::Kind::Section;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty()) {
    SectionEnterPlanePhase(st, log);
    if (st.sectionPhase != AppCommandState::SectionPhase::SelectSolids)
      return;
    st.selection.clear();
  }
  st.sectionPhase = AppCommandState::SectionPhase::SelectSolids;
  log.push_back(CadSectionPromptText(st));
}

/// Build the plane from the three picked points and section with it.
static void CommitSectionFromPoints(AppCommandState& st, std::vector<std::string>& log) {
  const ray3d::Vec3 n =
      ray3d::Cross(ray3d::Sub(st.sectionP2, st.sectionP1), ray3d::Sub(st.sectionP3, st.sectionP1));
  if (!(ray3d::Length(n) > 1e-9)) {
    // Collinear points are a plane with no orientation. Said, not guessed at — and the command
    // stays open at the third point so the pick can simply be repeated.
    log.push_back("SECTION — the three points are in a line; they do not define a plane.");
    st.sectionPhase = AppCommandState::SectionPhase::WaitP3;
    log.push_back(CadSectionPromptText(st));
    return;
  }
  CadSectionSolidsByPlane(st, st.sectionSolidIndices, st.sectionSolidOwners, st.sectionP1, ray3d::Normalize(n),
                          log);
  st.active = AppCommandState::Kind::None;
  CancelSectionCommand(st);
}

/// Section by the ACTIVE UCS plane — increment 1's behaviour, reached now by the `[UCS]` option.
static void CommitSectionByUcs(AppCommandState& st, std::vector<std::string>& log) {
  const ucs::Ucs frame = CadActiveUcsStorage(st);
  CadSectionSolidsByPlane(st, st.sectionSolidIndices, st.sectionSolidOwners, frame.origin, frame.zAxis, log);
  st.active = AppCommandState::Kind::None;
  CancelSectionCommand(st);
}

bool HandleSectionTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Section)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  using SP = AppCommandState::SectionPhase;

  if (st.sectionPhase == SP::SelectSolids) {
    if (!line.empty())
      return false;  // a typed line during selection is not ours; let the dispatcher have it
    if (st.selection.empty()) {
      log.push_back("SECTION — nothing selected. Click a solid, or ESC.");
      return true;
    }
    SectionEnterPlanePhase(st, log);
    return true;
  }

  if (line.empty())
    return true;  // Enter at a point prompt: nothing to accept, keep waiting

  if (st.sectionPhase == SP::WaitP1) {
    const std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "ucs" || low == "u") {
      CommitSectionByUcs(st, log);
      return true;
    }
  }

  ray3d::Vec3 p{};
  if (!ParseSolidBasePoint(st, line, &p, log, "SECTION"))
    return true;  // it has already said why, once (code review on #478, finding 11)
  if (st.sectionPhase == SP::WaitP1) {
    st.sectionP1 = p;
    st.sectionPhase = SP::WaitP2;
  } else if (st.sectionPhase == SP::WaitP2) {
    st.sectionP2 = p;
    st.sectionPhase = SP::WaitP3;
  } else {
    st.sectionP3 = p;
    CommitSectionFromPoints(st, log);
    return true;
  }
  log.push_back(CadSectionPromptText(st));
  return true;
}

void SubmitSectionViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Section)
    return;
  using SP = AppCommandState::SectionPhase;
  const ray3d::Vec3 p{static_cast<double>(wx), static_cast<double>(wy),
                      static_cast<double>(CadCommitElevation(st))};
  switch (st.sectionPhase) {
  case SP::WaitP1:
    st.sectionP1 = p;
    st.sectionPhase = SP::WaitP2;
    break;
  case SP::WaitP2:
    st.sectionP2 = p;
    st.sectionPhase = SP::WaitP3;
    break;
  case SP::WaitP3:
    st.sectionP3 = p;
    CommitSectionFromPoints(st, log);
    return;
  case SP::SelectSolids:
    return;
  }
  log.push_back(CadSectionPromptText(st));
}

/// `SOLIDCHECK` — say whether each solid is sound, and if not, why (REQ-313 as amended,
/// D-2026-09-09-j, GitHub #149 acceptance 7).
///
/// Checks the current selection, or the whole drawing when nothing is selected. Read-only: no undo
/// entry, nothing repaired. REQ-201's position is that a fault is reported rather than silently
/// corrected, and this is the command that reports it.
///
/// **Two questions, not one, and that is the whole point of the command.** `brep::Validate` asks
/// whether the TOPOLOGY holds up — closed, manifold, orientable, non-degenerate — and it already
/// names each of those four faults apart. `brep::SelfIntersects` asks the separate geometric
/// question of whether the surface passes through itself, which `Validate` deliberately calls Ok:
/// a torus whose tube is wider than its ring is legitimate topology and draws correctly, but its
/// surface encloses part of space twice, so its volume and centroid mean nothing and
/// `ComputeMassProperties` declines them.
///
/// A command that answered only the first would call that torus sound and then leave the user to
/// discover, from a Properties panel that has gone blank, that nothing can be measured about it —
/// with no statement anywhere of why. So both are asked and both are reported.
void CadCheckSolids(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<int> targets;
  bool fromSelection = false;
  for (const SelectedEntity& e : st.selection)
    if (e.type == SelectedEntity::Type::Solid && e.index >= 0 &&
        static_cast<std::size_t>(e.index) < st.drawing.cadSolids.size()) {
      targets.push_back(e.index);
      fromSelection = true;
    }
  if (targets.empty()) {
    // A selection that holds only non-solid entities is NOT the same as no selection, and widening
    // silently to the whole drawing would answer a question the user did not ask. Say so instead,
    // the way EXTRUDE ("nothing in the selection could be extruded") and SLICE already do.
    if (!st.selection.empty()) {
      log.push_back("SOLIDCHECK — nothing in the selection is a solid. Select solids, or clear the "
                    "selection to check the whole drawing.");
      return;
    }
    for (int i = 0; i < static_cast<int>(st.drawing.cadSolids.size()); ++i)
      targets.push_back(i);
  }

  if (targets.empty()) {
    log.push_back("SOLIDCHECK — no solids in this drawing.");
    return;
  }

  int sound = 0, faulty = 0, unmeasurable = 0;
  log.push_back(std::string("SOLIDCHECK — checking ") + std::to_string(targets.size()) + " solid" +
                (targets.size() == 1 ? "" : "s") + (fromSelection ? " (selected)." : " (whole drawing)."));

  for (const int idx : targets) {
    const CadSolidPtr& sp = st.drawing.cadSolids[static_cast<std::size_t>(idx)];
    char buf[420];
    if (!sp) {
      std::snprintf(buf, sizeof(buf), "  [%d] missing — the drawing holds no solid at this index.", idx);
      log.push_back(buf);
      ++faulty;
      continue;
    }
    const char* kind = brep::PrimitiveKindName(sp->recipe.kind);
    const brep::Problem why = brep::Validate(*sp);
    if (why != brep::Problem::Ok) {
      // The kernel's own reason, verbatim — the four faults #149 acceptance 7 names are already
      // distinct values with distinct messages, so there is nothing to translate or collapse here.
      std::snprintf(buf, sizeof(buf), "  [%d] %s — NOT VALID: %s", idx, kind, brep::ProblemText(why));
      log.push_back(buf);
      ++faulty;
      continue;
    }
    if (brep::SelfIntersects(*sp)) {
      std::snprintf(buf, sizeof(buf),
                    "  [%d] %s — topology is sound, but the surface passes through itself, so its "
                    "volume, area and centroid are not reported.",
                    idx, kind);
      log.push_back(buf);
      ++unmeasurable;
      continue;
    }
    std::snprintf(buf, sizeof(buf), "  [%d] %s — valid: closed, manifold, consistently oriented.", idx,
                  kind);
    log.push_back(buf);
    ++sound;
  }

  char tail[300];
  std::snprintf(tail, sizeof(tail), "SOLIDCHECK — %d valid, %d not valid, %d self-intersecting.", sound,
                faulty, unmeasurable);
  log.push_back(tail);
}

void CadReportSolids(const AppCommandState& st, std::vector<std::string>& log) {
  if (st.drawing.cadSolids.empty()) {
    log.push_back("No solids in this drawing.");
    return;
  }
  log.push_back("Solids: " + std::to_string(st.drawing.cadSolids.size()));
  for (size_t i = 0; i < st.drawing.cadSolids.size(); ++i) {
    const CadSolidPtr& sp = st.drawing.cadSolids[i];
    if (!sp)
      continue;
    const brep::MassProperties mp = brep::ComputeMassProperties(*sp);
    const char* layer = (i < st.drawing.cadSolidAttrs.size() && !st.drawing.cadSolidAttrs[i].layer.empty())
                            ? st.drawing.cadSolidAttrs[i].layer.c_str()
                            : "0";
    char buf[420];
    // Volume and area come from the topology, never from the recipe (ADR-045 (c)) — so a solid whose
    // recipe somehow disagreed with its geometry reports what it actually IS.
    std::snprintf(buf, sizeof(buf),
                  "  [%d] %s on layer %s — volume %.4f, area %.4f, %d vertices, %d edges, %d faces%s",
                  static_cast<int>(i), brep::PrimitiveKindName(sp->recipe.kind), layer, mp.volume,
                  mp.surfaceArea, static_cast<int>(sp->vertices.size()),
                  static_cast<int>(sp->edges.size()), static_cast<int>(sp->faces.size()),
                  mp.valid ? "." : " — INVALID.");
    log.push_back(buf);
    if (mp.valid && mp.centroidValid) {
      char cbuf[220];
      std::snprintf(cbuf, sizeof(cbuf), "       centroid (%.4f, %.4f, %.4f)", mp.centroid.x,
                    mp.centroid.y, mp.centroid.z);
      log.push_back(cbuf);
    }
    if (mp.valid && mp.inertiaValid) {
      char ibuf[420];
      std::snprintf(ibuf, sizeof(ibuf),
                    "       inertia about centroid (unit density): Ixx %.4f Iyy %.4f Izz %.4f Ixy %.4f Ixz %.4f Iyz %.4f",
                    mp.Ixx, mp.Iyy, mp.Izz, mp.Ixy, mp.Ixz, mp.Iyz);
      log.push_back(ibuf);
      char pbuf[420];
      std::snprintf(pbuf, sizeof(pbuf),
                    "       principal moments %.4f %.4f %.4f", mp.principalI1, mp.principalI2,
                    mp.principalI3);
      log.push_back(pbuf);
      char abuf[420];
      std::snprintf(abuf, sizeof(abuf),
                    "       principal axes: (%.4f,%.4f,%.4f) (%.4f,%.4f,%.4f) (%.4f,%.4f,%.4f)",
                    mp.principalAxis1.x, mp.principalAxis1.y, mp.principalAxis1.z,
                    mp.principalAxis2.x, mp.principalAxis2.y, mp.principalAxis2.z,
                    mp.principalAxis3.x, mp.principalAxis3.y, mp.principalAxis3.z);
      log.push_back(abuf);
    } else if (mp.valid && !mp.inertiaValid && mp.centroidValid) {
      log.push_back("       inertia: unavailable for this solid (face shape not covered).");
    } else if (mp.valid && !mp.inertiaValid) {
      log.push_back("       inertia: unavailable (centroid unavailable).");
    }
    if (!mp.valid) {
      // For an invalid or self-intersecting solid, explain why no mass properties are reported.
      if (brep::SelfIntersects(*sp)) {
        log.push_back("       mass properties unavailable — self-intersecting.");
      } else if (brep::Validate(*sp) != brep::Problem::Ok) {
        log.push_back(std::string("       mass properties unavailable — ") +
                      brep::ProblemText(brep::Validate(*sp)) + ".");
      }
    } else if (brep::SelfIntersects(*sp)) {
      log.push_back("       mass properties unavailable — self-intersecting (inertia and centroid withheld).");
    }
  }
}

void CadReportMassProperties(const AppCommandState& st, std::vector<std::string>& log) {
  if (st.drawing.cadSolids.empty()) {
    log.push_back("No solids in this drawing.");
    return;
  }
  log.push_back("Mass properties (unit density, so mass = volume):");
  for (size_t i = 0; i < st.drawing.cadSolids.size(); ++i) {
    const CadSolidPtr& sp = st.drawing.cadSolids[i];
    if (!sp)
      continue;
    const brep::MassProperties mp = brep::ComputeMassProperties(*sp);
    const char* layer = (i < st.drawing.cadSolidAttrs.size() && !st.drawing.cadSolidAttrs[i].layer.empty())
                            ? st.drawing.cadSolidAttrs[i].layer.c_str()
                            : "0";
    char hdr[220];
    std::snprintf(hdr, sizeof(hdr), "  [%d] %s on layer %s", static_cast<int>(i),
                  brep::PrimitiveKindName(sp->recipe.kind), layer);
    log.push_back(hdr);
    if (!mp.valid) {
      if (brep::SelfIntersects(*sp))
        log.push_back("       self-intersecting — volume, area, centroid and inertia are not reported.");
      else
        log.push_back(std::string("       invalid — ") + brep::ProblemText(brep::Validate(*sp)) + ".");
      continue;
    }
    if (brep::SelfIntersects(*sp)) {
      log.push_back("       self-intersecting — volume, area, centroid and inertia are not reported.");
      continue;
    }
    char vbuf[320];
    std::snprintf(vbuf, sizeof(vbuf), "       volume %.6f  area %.6f  centroid (%.6f, %.6f, %.6f)%s",
                  mp.volume, mp.surfaceArea, mp.centroid.x, mp.centroid.y, mp.centroid.z,
                  mp.centroidValid ? "" : " — centroid unavailable");
    log.push_back(vbuf);
    if (!mp.inertiaValid) {
      log.push_back("       inertia about centroid: unavailable (face shape not covered in this increment).");
      continue;
    }
    char ibuf[420];
    std::snprintf(ibuf, sizeof(ibuf),
                  "       inertia about centroid: Ixx %.6f Iyy %.6f Izz %.6f Ixy %.6f Ixz %.6f Iyz %.6f",
                  mp.Ixx, mp.Iyy, mp.Izz, mp.Ixy, mp.Ixz, mp.Iyz);
    log.push_back(ibuf);
    // Also show tensor about world origin as a demonstration of parallel-axis.
    const brep::InertiaTensor Io = brep::InertiaAboutPoint(mp, ray3d::Vec3{0, 0, 0});
    char obuf[420];
    std::snprintf(obuf, sizeof(obuf),
                  "       inertia about world origin: Ixx %.6f Iyy %.6f Izz %.6f Ixy %.6f Ixz %.6f Iyz %.6f",
                  Io.xx, Io.yy, Io.zz, Io.xy, Io.xz, Io.yz);
    log.push_back(obuf);
    char pbuf[420];
    std::snprintf(pbuf, sizeof(pbuf), "       principal moments %.6f %.6f %.6f", mp.principalI1,
                  mp.principalI2, mp.principalI3);
    log.push_back(pbuf);
    char abuf[420];
    std::snprintf(abuf, sizeof(abuf),
                  "       principal axes (world, orthonormal, right-handed):\n"
                  "         X1 (%.6f, %.6f, %.6f)  X2 (%.6f, %.6f, %.6f)  X3 (%.6f, %.6f, %.6f)",
                  mp.principalAxis1.x, mp.principalAxis1.y, mp.principalAxis1.z,
                  mp.principalAxis2.x, mp.principalAxis2.y, mp.principalAxis2.z,
                  mp.principalAxis3.x, mp.principalAxis3.y, mp.principalAxis3.z);
    log.push_back(abuf);
  }
}

bool ComputeSectionClipIndicatorBounds(const AppCommandState& st, ray3d::Vec3* outMin, ray3d::Vec3* outMax) {
  if (!outMin || !outMax)
    return false;
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0;
  if (!ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    return false;
  // Z from the solids' analytic bounds — the only kind here with a real vertical extent worth
  // covering. Everything else is drawn near an elevation, and a flat box still gets a drawable
  // rectangle (`SectionClipIndicatorQuad` pads a zero span).
  bool haveZ = false;
  double mnZ = 0.0, mxZ = 0.0;
  for (const CadSolidPtr& sp : st.drawing.cadSolids) {
    if (!sp)
      continue;
    const brep::Bounds b = brep::ComputeBounds(*sp);
    if (!b.valid)
      continue;
    mnZ = haveZ ? std::min(mnZ, b.mn.z) : b.mn.z;
    mxZ = haveZ ? std::max(mxZ, b.mx.z) : b.mx.z;
    haveZ = true;
  }
  if (!haveZ)
    mnZ = mxZ = CadActiveUcsStorage(st).origin.z;
  *outMin = ray3d::Vec3{mnX, mnY, mnZ};
  *outMax = ray3d::Vec3{mxX, mxY, mxZ};
  return true;
}

/// One line describing the clip's current state, used by the bare command and after every change so
/// a user never has to guess where the plane is (REQ-341).
std::string SectionClipReport(const AppCommandState& st) {
  if (!st.viewportSectionClip)
    return "Section clip = OFF.";
  // The drawing's own linear precision, through the shared formatter — NOT `%.4g`, which was four
  // significant figures and therefore threw away exactly the precision the parser is careful to
  // keep. `SECTIONCLIP 200000.01` reported "offset 2e+05": an offset stated to 0.01 ft read back
  // with about 10 ft of error, on the one line that tells a user where the plane is. Any offset
  // above 9999.5 was reported wrong. The parse comment a few functions down argues for `double`
  // because REQ-101 is +/-0.002 ft; printing it at four figures contradicted that in the same
  // breath.
  return std::string("Section clip = ON at offset ") +
         FormatLinear(st.viewportSectionClipOffset, st.displayLinearPrecision) +
         " along the UCS Z" + (st.viewportSectionClipFlip ? ", flipped" : "") + ".";
}

/// `SECTIONCLIP` — hide everything in front of a plane so the inside of a model can be looked at
/// (REQ-341 / ADR-058, GitHub issue #149 acceptance 6).
///
/// `ON` / `OFF` toggle it, `FLIP` swaps which half survives, and a bare number sets the offset of
/// the plane along the active UCS Z. A bare `SECTIONCLIP` reports — the same report-or-set shape as
/// `PERSPECTIVE` and `CROSSHAIR3D` (REQ-309, REQ-310), so a third view toggle behaves like the two
/// that came before it.
///
/// **This is a view state, not an edit.** No undo entry, no geometry touched, nothing appended: the
/// clip is a uniform the renderer re-reads every frame. That is also what makes it live — setting
/// the offset changes the next frame and invalidates no cached geometry, so there is no rebuild
/// step between moving the plane and seeing the result. Compare `SECTION` (REQ-335), which is the
/// same plane asked a different question and DOES create geometry.
///
/// A number is accepted with or without `OFFSET` in front of it, because "SECTIONCLIP 12" is what a
/// user types once they know the command; the keyword exists so the help line can name the unit.
bool ApplySectionClipValue(AppCommandState& st, const std::string& raw, std::vector<std::string>& log) {
  // The shared helper, not a hand-rolled loop — `HandleSectionTextInput`, added in this same
  // change, already uses it for the identical job a few hundred lines up.
  const std::string v = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(raw));

  // **No numeric aliases for ON and OFF here**, unlike `PERSPECTIVE` and `CROSSHAIR3D` which accept
  // `1` and `0`. Those two have nothing but on and off to say, so a digit is unambiguous. This
  // command's main argument is a DISTANCE, and `SECTIONCLIP 0` — the most natural way to ask for a
  // cut exactly at the UCS plane — would otherwise be read as "off" and switch the feature off
  // instead. For a command that takes a number, digits mean the number. Caught in the real GUI
  // (TASK-249); a transcript had typed `SECTIONCLIP 0` and only checked that nothing was rebuilt.
  if (v == "on" || v == "yes") {
    st.viewportSectionClip = true;
    log.push_back(SectionClipReport(st));
    return true;
  }
  if (v == "off" || v == "no") {
    st.viewportSectionClip = false;
    log.push_back(SectionClipReport(st));
    return true;
  }
  if (v == "flip" || v == "reverse" || v == "invert") {
    // Flipping while the clip is off would silently change what ON later means, so it turns the
    // clip on as well: the user asked to see the other half, and the other half is a visible thing.
    //
    // Shared with the flip HANDLE (REQ-343) rather than written twice, so the typed command and the
    // click cannot drift into meaning different things.
    ToggleSectionClipFlip(st, log);
    return true;
  }

  // `OFFSET <n>` and a bare `<n>` are the same request.
  std::string numTok = v;
  if (v.rfind("offset", 0) == 0)
    numTok = StringUtil::trimCopy(v.substr(6));

  // Parsed as a DOUBLE, not through `ParseOneFloat`: this offset is a coordinate the user typed,
  // the field it lands in is a double, and REQ-101 is +/-0.002 ft. A float round trip resolves that
  // only up to about 16,000 ft — narrowing it here and widening it again on the next line is
  // precisely the one-narrowing-point defect ADR-054 Phase D audited out.
  double parsed = 0.0;
  {
    // `strtod` with an end pointer, not `istringstream >> double`: the stream stops at the first
    // character it cannot use and reports success for whatever it managed to read, so `12abc` set
    // the offset to 12, `4 8` set it to 4, and `12,5` — an ordinary decimal comma outside the US —
    // silently set 12 instead of 12.5. Each reported "Section clip = ON at offset ..." as though it
    // had understood. Every other refusal in this function leaves the previous state alone and says
    // why; this one committed a wrong number and claimed success.
    //
    // The whole token must be consumed, which is the pattern `ParseSolidBasePoint` already uses.
    const char* first = numTok.c_str();
    char* end = nullptr;
    parsed = std::strtod(first, &end);
    const bool consumedAll = (end != first) && (*end == '\0');
    if (numTok.empty() || !consumedAll || !std::isfinite(parsed)) {
      log.push_back("SECTIONCLIP - enter ON, OFF, FLIP, or an offset distance along the UCS Z.");
      return false;
    }
  }
  // A non-finite offset would produce a plane constant that clips everything or nothing with no
  // way back, so it is refused above rather than stored (REQ-201).
  st.viewportSectionClipOffset = parsed;
  st.viewportSectionClip = true;  // typing an offset means "show me that cut"
  log.push_back(SectionClipReport(st));
  return true;
}

/// Bare `SECTIONCLIP` — report the current state, then WAIT, so the keywords can be clicked.
///
/// The waiting state is the whole point. A bracketed option in the command hint
/// (`CommandInputHint`) draws as a link, and clicking it submits that option's shortcut as the next
/// line of input — which only means anything if a command is waiting to consume it. A one-shot
/// command that printed `ON | OFF | FLIP` and returned to idle would render three links that submit
/// `on`, `off` and `flip` into nothing.
///
/// The inline forms (`SECTIONCLIP ON`, `SECTIONCLIP 12`) are untouched and never enter this state:
/// a user who already knows what they want should not be made to answer a prompt.
void StartSectionClipCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::SectionClip;
  log.push_back(SectionClipReport(st));
}

// ---------------------------------------------------------------------------------------------
// SECTIONPLANE (REQ-342 / ADR-059, GitHub issue #479 acceptance 1-3)
// ---------------------------------------------------------------------------------------------

/// The frame the clip plane is currently built from — the face `SECTIONPLANE` was given, or the
/// active UCS when it was never given one (REQ-342 / D-2026-09-11-b).
///
/// One function so the renderer, the report line and the tests cannot disagree about which plane is
/// in force. See \ref AppCommandState::viewportSectionClipFrameValid for why there is one plane
/// rather than two.
ucs::Ucs CadEffectiveSectionClipFrame(const AppCommandState& st) {
  if (st.viewportSectionClipFrameValid)
    return st.viewportSectionClipFrame;
  return CadActiveUcsStorage(st);
}

const char* CadSectionPlanePromptText(const AppCommandState& st) {
  if (st.sectionPlanePhase == AppCommandState::SectionPlanePhase::WaitThroughPoint)
    return "SECTIONPLANE — specify the through point of the section line. ESC cancels.";
  return "SECTIONPLANE — select a flat face, or a point to start a section line. ESC cancels.";
}

/// The frame of the plane standing square to the work plane through the line \p a to \p b, or false
/// when those two points name no plane — the same two cases \ref ApplySectionPlaneFromLine refuses,
/// without the message, so the live preview and the placement cannot disagree about either.
static bool SectionPlaneFrameFromLine(const AppCommandState& st, const ray3d::Vec3& a,
                                      const ray3d::Vec3& b, ucs::Ucs* out) {
  const ucs::Ucs work = CadActiveUcsStorage(st);
  const ray3d::Vec3 along = ray3d::Sub(b, a);
  const double len = ray3d::Length(along);
  const double scale = std::max(1.0, ray3d::Length(a));
  if (!(len > 1e-9 * scale))
    return false;
  const ray3d::Vec3 normal = ray3d::Cross(along, work.zAxis);
  if (!(ray3d::Length(normal) > 1e-9 * len))
    return false;
  return ucs::FromNormal(a, ray3d::Normalize(normal), out);
}

/// Place the clip plane square to the work plane, through the section line \p a to \p b (REQ-342,
/// 2026-09-18 revision; AutoCAD's "Select face or any point to locate section line").
///
/// The plane's normal is across the line, level with the work plane, so the plane STANDS on the line
/// the way the one AutoCAD draws does. That is what lets a plane be aimed at a solid with no flat
/// face — a sphere, a torus, a fillet — which the face form cannot do at all.
///
/// Refuses two points at the same place, and a line that runs straight up the work plane's own
/// normal: neither names a plane (REQ-201).
static bool ApplySectionPlaneFromLine(AppCommandState& st, const ray3d::Vec3& a, const ray3d::Vec3& b,
                                      std::vector<std::string>& log) {
  const ucs::Ucs work = CadActiveUcsStorage(st);
  const ray3d::Vec3 along = ray3d::Sub(b, a);
  const double len = ray3d::Length(along);
  const double scale = std::max(1.0, ray3d::Length(a));
  if (!(len > 1e-9 * scale)) {
    log.push_back("SECTIONPLANE — the two points are in the same place; a section line needs two.");
    return false;
  }
  if (!(ray3d::Length(ray3d::Cross(along, work.zAxis)) > 1e-9 * len)) {
    log.push_back("SECTIONPLANE — that line runs square to the work plane, so it names no section "
                  "plane. Pick two points across the work plane instead.");
    return false;
  }
  ucs::Ucs frame{};
  if (!SectionPlaneFrameFromLine(st, a, b, &frame)) {
    log.push_back("SECTIONPLANE — those two points do not name a plane.");
    return false;
  }
  st.viewportSectionClipFrame = frame;
  st.viewportSectionClipFrameValid = true;
  st.viewportSectionClipOffset = 0.0;
  st.viewportSectionClipFlip = false;
  st.viewportSectionClipExtent = SectionPlaneExtent{};
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  st.viewportSectionClip = true;
  st.sectionPlaneSelected = true;
  log.push_back("SECTIONPLANE — plane placed on the section line. Drag the centre handle to slide "
                "it, the arrow to flip it, the end handles to resize it.");
  return true;
}

/// Take \p p as the next point of the section line, and place the plane once there are two.
static void SubmitSectionPlaneLinePoint(AppCommandState& st, const ray3d::Vec3& p,
                                        std::vector<std::string>& log) {
  using PP = AppCommandState::SectionPlanePhase;
  if (st.sectionPlanePhase == PP::PickFaceOrPoint) {
    st.sectionPlaneP1 = p;
    st.sectionPlanePhase = PP::WaitThroughPoint;
    log.push_back(CadSectionPlanePromptText(st));
    return;
  }
  if (!ApplySectionPlaneFromLine(st, st.sectionPlaneP1, p, log)) {
    // Still open, still on the same first point: the through point is what was wrong, and making
    // the user start the line again would throw away a pick that was fine.
    log.push_back(CadSectionPlanePromptText(st));
    return;
  }
  CancelSectionPlaneCommand(st);
}

/// ORTHO applied to a section line: the through point is pulled onto the work plane's X or Y from the
/// first point, whichever the cursor is further along — the rule the LINE rubber band already
/// follows, and the one AutoCAD's own "Ortho: … < 270°" readout in the report's screenshots shows.
///
/// The point's height along the work plane's normal is left alone: the plane stands up that normal
/// either way, so the constraint is about the DIRECTION of the line and nothing else.
static ray3d::Vec3 SectionPlaneOrthoConstrain(const AppCommandState& st, const ray3d::Vec3& a,
                                              const ray3d::Vec3& p) {
  if (!st.orthoMode)
    return p;
  const ucs::Ucs work = CadActiveUcsStorage(st);
  const ray3d::Vec3 d = ray3d::Sub(p, a);
  double du = ray3d::Dot(d, work.xAxis);
  double dv = ray3d::Dot(d, work.yAxis);
  const double dz = ray3d::Dot(d, work.zAxis);
  if (std::fabs(du) >= std::fabs(dv))
    dv = 0.0;
  else
    du = 0.0;
  return ray3d::Add(a, ray3d::Add(ray3d::Scale(work.xAxis, du),
                                  ray3d::Add(ray3d::Scale(work.yAxis, dv),
                                             ray3d::Scale(work.zAxis, dz))));
}

/// Where a cursor ray lands when SECTIONPLANE takes it as a POINT: on the geometry it hit, which is
/// what makes "click the middle of the torus" mean the middle of the torus, and otherwise on the work
/// plane, which is where a click in empty space has always been resolved. Shared by the click and by
/// the live preview, so the preview cannot show a plane through a point the click would not take.
static bool SectionPlanePointFromRay(const AppCommandState& st, const ray3d::Ray& ray,
                                     const solidpick::Pick* hit, ray3d::Vec3* out) {
  if (hit) {
    *out = hit->point;
    return true;
  }
  const ucs::Ucs work = CadActiveUcsStorage(st);
  ray3d::Plane plane{};
  plane.point = work.origin;
  plane.normal = work.zAxis;
  return ray3d::RayPlaneIntersect(ray, plane, out);
}

void UpdateSectionPlanePreview(AppCommandState& st, const ray3d::Ray& ray,
                               const solidpick::Tolerance& tol) {
  if (st.active != AppCommandState::Kind::SectionPlane ||
      st.sectionPlanePhase != AppCommandState::SectionPlanePhase::WaitThroughPoint) {
    st.sectionPlanePreviewValid = false;
    return;
  }
  SelectedSubObject hit{};
  solidpick::Pick pick{};
  const bool hitSomething = PickSubObjectAcrossSolids(st, ray, tol, &hit, &pick);
  ray3d::Vec3 p{};
  st.sectionPlanePreviewValid = SectionPlanePointFromRay(st, ray, hitSomething ? &pick : nullptr, &p);
  if (st.sectionPlanePreviewValid)
    st.sectionPlanePreviewPoint = SectionPlaneOrthoConstrain(st, st.sectionPlaneP1, p);
}

void ClearSectionPlanePreview(AppCommandState& st) { st.sectionPlanePreviewValid = false; }

void SubmitSectionPlanePointPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::SectionPlane)
    return;
  ray3d::Vec3 p{static_cast<double>(wx), static_cast<double>(wy),
                static_cast<double>(CadCommitElevation(st))};
  if (st.sectionPlanePhase == AppCommandState::SectionPlanePhase::WaitThroughPoint)
    p = SectionPlaneOrthoConstrain(st, st.sectionPlaneP1, p);
  SubmitSectionPlaneLinePoint(st, p, log);
}

bool HandleSectionPlaneTextInput(const std::string& lineIn, AppCommandState& st,
                                 std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::SectionPlane)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  if (line.empty())
    return true;  // Enter at a point prompt: nothing to accept, keep waiting
  ray3d::Vec3 p{};
  if (!ParseSolidBasePoint(st, line, &p, log, "SECTIONPLANE"))
    return true;  // it has already said why, once
  SubmitSectionPlaneLinePoint(st, p, log);
  return true;
}

void CancelSectionPlaneCommand(AppCommandState& st) {
  if (st.active == AppCommandState::Kind::SectionPlane)
    st.active = AppCommandState::Kind::None;
}

/// `SECTIONPLANE` — put a section plane on a face of a solid, and show it (REQ-342).
///
/// The command has one step, so being active IS "waiting for a face". Note what it does NOT do:
/// it does not ask which solids to section, the way `SECTION` does. A clip plane is a property of
/// the view and cuts everything in the drawing, so a per-solid selection would be a question whose
/// answer is never used — and #479's own screenshots show AutoCAD asking for the face and nothing
/// else.
void StartSectionPlaneCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::SectionPlane;
  st.lastCommand = AppCommandState::Kind::SectionPlane;
  st.selBoxWaitingSecond = false;
  st.sectionPlanePhase = AppCommandState::SectionPlanePhase::PickFaceOrPoint;
  st.sectionPlaneP1 = ray3d::Vec3{};
  if (st.drawing.cadSolids.empty()) {
    // Said before the user hunts for something to click. The command still opens: a solid can be
    // created and picked without retyping, and refusing outright would be a command that works or
    // not depending on drawing order.
    log.push_back("SECTIONPLANE — there are no solids in the drawing yet.");
  }
  log.push_back(CadSectionPlanePromptText(st));
}

/// The surface kind, for the refusal message. Local rather than added to `brep` because this is
/// the only caller: a name for the user, not a kernel facility.
static const char* SectionPlaneSurfaceWord(brep::SurfaceKind k) {
  switch (k) {
  case brep::SurfaceKind::Plane:    return "flat";
  case brep::SurfaceKind::Cylinder: return "cylindrical";
  case brep::SurfaceKind::Cone:     return "conical";
  case brep::SurfaceKind::Sphere:   return "spherical";
  case brep::SurfaceKind::Torus:    return "toroidal";
  case brep::SurfaceKind::Nurbs:    return "freeform";
  }
  return "curved";
}

/// Put the clip plane on \p face of \p solid, or say why that face cannot carry one.
///
/// **Only a planar face.** Every other surface kind carries a frame too, and its Z is the surface's
/// AXIS rather than a normal — a cylinder's frame Z runs up the middle of it. Accepting one would
/// silently produce a plane through the centre of the solid at right angles to what was clicked:
/// plausible, wrong, and invisible in a screenshot. Refused by name instead (REQ-201).
static bool ApplySectionPlaneFromFace(AppCommandState& st, const brep::Solid& solid, int faceIndex,
                                      std::vector<std::string>& log) {
  if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= solid.faces.size()) {
    log.push_back("SECTIONPLANE — that face is no longer part of the solid.");
    return false;
  }
  const brep::Face& f = solid.faces[static_cast<size_t>(faceIndex)];
  if (f.surface.kind != brep::SurfaceKind::Plane) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "SECTIONPLANE — that is a %s face; a section plane needs a flat one.",
                  SectionPlaneSurfaceWord(f.surface.kind));
    log.push_back(buf);
    return false;
  }

  // REQ-343 amended (issue #479 acceptance 8): creating/re-aiming the plane is undoable, pushed
  // before the write below — the same "Create ..." moment every other creating command pushes at.
  PushUndoSnapshot(st, st.viewportSectionClip ? "Re-aim section plane" : "Create section plane");

  // The face's frame IS the plane. Its origin lies on the face's plane and its Z is the OUTWARD
  // normal — measured across every primitive, both B1 Booleans and an oblique SLICE in probe P1/P2
  // (2026-09-11), with no counterexample and no difference at survey magnitudes.
  st.viewportSectionClipFrame = f.surface.frame;
  st.viewportSectionClipFrameValid = true;
  // Offset 0, so at the moment of creation the plane sits ON the face and the whole solid is on the
  // kept side: nothing disappears. That is deliberate and it is what AutoCAD does — creating a
  // section plane should show you a plane, not make half your model vanish. Sliding it in is the
  // next gesture, and the manipulation slice is what makes that direct.
  st.viewportSectionClipOffset = 0.0;
  st.viewportSectionClipFlip = false;
  // The stretched size goes too (REQ-343). It was stated in the OLD plane's basis, and that basis
  // is derived from the normal — so keeping it would apply a length measured across one face to a
  // completely different direction on another.
  st.viewportSectionClipExtent = SectionPlaneExtent{};
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  st.viewportSectionClip = true;
  // Selected on creation, so the handles are there to be used immediately — the user placed it in
  // order to move it, and making them click it again first is a step with no purpose.
  st.sectionPlaneSelected = true;
  log.push_back("SECTIONPLANE — plane placed on the face. Drag the centre handle to slide it, the "
                "arrow to flip it, the end handles to resize it.");
  return true;
}

/// The viewport click that answers "select a flat face" (REQ-342 acceptance 1).
///
/// Takes the cursor RAY and a world tolerance rather than a plan-space point, because a face is a
/// 3D thing and plan view is the default view: `PickSubObjectAcrossSolids` is the same entry point
/// `Ctrl`+click has used since REQ-318, so the face this takes is the face that highlighted.
bool SubmitSectionPlaneFacePick(AppCommandState& st, const ray3d::Ray& ray,
                                const solidpick::Tolerance& tol, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::SectionPlane)
    return false;
  using PP = AppCommandState::SectionPlanePhase;

  SelectedSubObject hit{};
  solidpick::Pick pick{};
  const bool hitSomething = PickSubObjectAcrossSolids(st, ray, tol, &hit, &pick);

  // Where this click lands when it is taken as a POINT rather than a face: on the geometry it hit,
  // which is what makes "click the middle of the torus" mean the middle of the torus, and otherwise
  // on the work plane, which is where a click in empty space has always been resolved.
  const auto pointOfClick = [&](ray3d::Vec3* out) {
    return SectionPlanePointFromRay(st, ray, hitSomething ? &pick : nullptr, out);
  };

  if (st.sectionPlanePhase == PP::WaitThroughPoint) {
    ray3d::Vec3 p{};
    if (!pointOfClick(&p)) {
      log.push_back("SECTIONPLANE — that click is not on the work plane; pick a point on it.");
      log.push_back(CadSectionPlanePromptText(st));
      return false;
    }
    // ORTHO applies to the picked through point, exactly as it did to the preview — so the plane
    // that lands is the plane that was drawn.
    SubmitSectionPlaneLinePoint(st, SectionPlaneOrthoConstrain(st, st.sectionPlaneP1, p), log);
    return st.active != AppCommandState::Kind::SectionPlane;
  }

  // A FLAT face answers the whole command in one click, and is still what this offers first.
  if (hitSomething && hit.kind == solidpick::Kind::Face) {
    const std::shared_ptr<const brep::Solid> owner = hit.owner.lock();
    if (!owner) {
      log.push_back("SECTIONPLANE — that solid is no longer in the drawing.");
      log.push_back(CadSectionPlanePromptText(st));
      return false;
    }
    if (owner->faces.size() > static_cast<size_t>(std::max(hit.index, 0)) && hit.index >= 0 &&
        owner->faces[static_cast<size_t>(hit.index)].surface.kind == brep::SurfaceKind::Plane) {
      if (!ApplySectionPlaneFromFace(st, *owner, hit.index, log)) {
        log.push_back(CadSectionPlanePromptText(st));
        return false;  // still open, so the next click can pick a different face
      }
      CancelSectionPlaneCommand(st);
      return true;
    }
  }

  // Anything else — a curved face, an edge, a vertex, empty space — is a POINT, and starts a section
  // line (REQ-342, 2026-09-18 revision). A sphere and a torus have no flat face at all, so before
  // this they could not be given a section plane by any click.
  ray3d::Vec3 p{};
  if (!pointOfClick(&p)) {
    // The command STAYS OPEN. A missed click is a missed click, not a reason to throw the user out
    // of a command they are halfway through — the rule REQ-335's selection step had to learn after
    // it ended itself and left the next click landing on nothing.
    log.push_back("SECTIONPLANE — that click is not on the work plane; pick a point on it.");
    log.push_back(CadSectionPlanePromptText(st));
    return false;
  }
  SubmitSectionPlaneLinePoint(st, p, log);
  return false;  // the command is still running: it has one more point to take
}

// ---------------------------------------------------------------------------------------------
// The section plane as a manipulable object (REQ-343, GitHub issue #479 acceptance 4-7)
// ---------------------------------------------------------------------------------------------

SectionClipPlane CadSectionClipPlane(const AppCommandState& st) {
  if (!st.viewportSectionClip)
    return SectionClipPlane{};
  return SectionClipFromUcs(CadEffectiveSectionClipFrame(st), st.viewportSectionClipOffset,
                            st.viewportSectionClipFlip);
}

/// The indicator rectangle for \p p, sized to the drawing — the body of \ref CadSectionClipIndicator,
/// shared with the SECTIONPLANE preview (REQ-342, 2026-09-18) so the plane the preview draws is the
/// same rectangle, in the same place, as the plane the click then places.
static SectionClipIndicator SectionClipIndicatorSizedToDrawing(const AppCommandState& st,
                                                               const SectionClipPlane& p,
                                                               const SectionPlaneExtent& extent) {
  if (!p.active)
    return SectionClipIndicator{};

  // Sized as #478 sizes it (D-2026-09-16-b): the drawing extents, else centred on the view.
  //
  // Cached against `cadGpuRevision` and the active tab (code review on #478, findings 8/13): the
  // extents walk and `ComputeBounds` (64-point marches along Intersection edges) are not free, and
  // this function is now called several times a frame (indicator, grips, graphics) against
  // REQ-100's 16 ms budget.
  static struct {
    bool cached = false;
    uint32_t revision = 0;
    uint32_t tabUid = 0;
    bool valid = false;
    ray3d::Vec3 mn, mx;
  } s_clipBounds;
  const uint32_t tabUid =
      (st.activeDrawingIdx >= 0 && static_cast<size_t>(st.activeDrawingIdx) < st.drawingTabs.size())
          ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].uid
          : 0u;
  if (!s_clipBounds.cached || s_clipBounds.revision != st.cadGpuRevision || s_clipBounds.tabUid != tabUid) {
    s_clipBounds.valid = ComputeSectionClipIndicatorBounds(st, &s_clipBounds.mn, &s_clipBounds.mx);
    s_clipBounds.revision = st.cadGpuRevision;
    s_clipBounds.tabUid = tabUid;
    s_clipBounds.cached = true;
  }
  ray3d::Vec3 bbMin = s_clipBounds.mn;
  ray3d::Vec3 bbMax = s_clipBounds.mx;
  if (!s_clipBounds.valid) {
    const Camera viewCam = CadViewCamera(st);
    const double r = std::max(10.0, static_cast<double>(viewCam.orthoHalfH));
    bbMin = ray3d::Vec3{viewCam.targetX - r, viewCam.targetY - r, viewCam.targetZ};
    bbMax = ray3d::Vec3{viewCam.targetX + r, viewCam.targetY + r, viewCam.targetZ};
  }
  return SectionClipIndicatorQuad(p, bbMin, bbMax, 0.15, extent);
}

SectionClipIndicator CadSectionClipIndicator(const AppCommandState& st) {
  return SectionClipIndicatorSizedToDrawing(st, CadSectionClipPlane(st), st.viewportSectionClipExtent);
}

bool CadSectionPlanePreviewIndicator(const AppCommandState& st, SectionClipIndicator* out) {
  if (!out || !st.sectionPlanePreviewValid ||
      st.active != AppCommandState::Kind::SectionPlane ||
      st.sectionPlanePhase != AppCommandState::SectionPlanePhase::WaitThroughPoint)
    return false;
  const ray3d::Vec3 a = st.sectionPlaneP1;
  const ray3d::Vec3 b = st.sectionPlanePreviewPoint;
  ucs::Ucs frame{};
  if (!SectionPlaneFrameFromLine(st, a, b, &frame))
    return false;  // the cursor is on the first point, or straight above it: no plane to show yet

  // The preview RECTANGLE IS THE SECTION LINE, stood up: it runs from the first point to the
  // cursor and grows with the drag, which is what AutoCAD draws and what was asked for — the
  // drawing-sized rectangle the placed plane uses looked fixed under the cursor, however far the
  // cursor went (user report 2026-09-22, with screenshots).
  //
  // Upright means along the WORK PLANE's normal, the same direction the plane itself stands in, so
  // the preview is square to the plane on a tilted UCS too.
  const ucs::Ucs work = CadActiveUcsStorage(st);
  const ray3d::Vec3 along = ray3d::Sub(b, a);
  const double lineLen = ray3d::Length(along);
  if (!(lineLen > 0.0))
    return false;
  const ray3d::Vec3 up = ray3d::Normalize(work.zAxis);

  // How far up and down to draw. The drawing's own span in that direction, so the preview covers the
  // model it is about to cut, with a floor tied to the line's length so a flat drawing — or a drag
  // far outside the model — still shows a plane with some height to it.
  double vLo = 0.0;
  double vHi = 0.0;
  ray3d::Vec3 bbMin{}, bbMax{};
  if (ComputeSectionClipIndicatorBounds(st, &bbMin, &bbMax)) {
    double lo = 1e300;
    double hi = -1e300;
    for (int i = 0; i < 8; ++i) {
      const ray3d::Vec3 c{(i & 1) ? bbMax.x : bbMin.x, (i & 2) ? bbMax.y : bbMin.y,
                          (i & 4) ? bbMax.z : bbMin.z};
      const double d = ray3d::Dot(ray3d::Sub(c, a), up);
      lo = std::fmin(lo, d);
      hi = std::fmax(hi, d);
    }
    if (hi >= lo) {
      const double pad = std::fmax((hi - lo) * 0.15, lineLen * 0.05);
      vLo = lo - pad;
      vHi = hi + pad;
    }
  }
  const double minHalf = std::fmax(lineLen * 0.35, 1e-6);
  if (!(vHi - vLo > 2.0 * minHalf)) {
    const double mid = 0.5 * (vLo + vHi);
    vLo = mid - minHalf;
    vHi = mid + minHalf;
  }

  SectionClipIndicator ind;
  const auto at = [&](const ray3d::Vec3& base, double v) {
    return ray3d::Add(base, ray3d::Scale(up, v));
  };
  ind.corner[0] = at(a, vLo);
  ind.corner[1] = at(b, vLo);
  ind.corner[2] = at(b, vHi);
  ind.corner[3] = at(a, vHi);
  ind.valid = true;
  *out = ind;
  return true;
}

SectionPlaneGrips CadSectionPlaneGrips(const AppCommandState& st) {
  if (!st.viewportSectionClip || !st.sectionPlaneSelected)
    return SectionPlaneGrips{};
  return SectionPlaneGripsFor(CadSectionClipIndicator(st), CadSectionClipPlane(st));
}

/// A ray with a unit direction, whatever the caller handed in.
///
/// Normalized ON ENTRY to every section-plane pick, the rule `PickSubObjectAcrossSolids` already
/// follows: a camera builds unit rays, but a caller that aims one at a point by subtracting two
/// positions does not, and the projection arithmetic below is silently scaled by the length if it
/// is not one. Wrong by a factor of the distance to the target, which looks like the handles simply
/// not being where they are drawn.
static ray3d::Ray SectionPlaneUnitRay(const ray3d::Ray& in) {
  ray3d::Ray r = in;
  r.dir = ray3d::Normalize(in.dir);
  return r;
}

SectionPlaneGrip PickSectionPlaneGrip(const AppCommandState& st, const ray3d::Ray& rayIn,
                                      double tolWorld) {
  const ray3d::Ray ray = SectionPlaneUnitRay(rayIn);
  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  if (!g.valid || !ray.valid())
    return SectionPlaneGrip::None;
  // Nearest handle wins, measured as the true 3D distance from the ray to the handle POINT. The
  // gizmo measures to a segment because its handles are arrows; these are points, and a point is
  // what the user aims at.
  SectionPlaneGrip best = SectionPlaneGrip::None;
  double bestD = tolWorld;
  for (int i = 0; i < kSectionPlaneGripCount; ++i) {
    const ray3d::Vec3 w = ray3d::Sub(g.at[i], ray.origin);
    const double t = ray3d::Dot(w, ray.dir);
    if (t < 0.0)
      continue;  // behind the camera
    const ray3d::Vec3 onRay{ray.origin.x + ray.dir.x * t, ray.origin.y + ray.dir.y * t,
                            ray.origin.z + ray.dir.z * t};
    // The flip symbol is drawn larger (kSectionPlaneFlipScale), so its grab zone is too; the
    // distance is shrunk by the same factor so it still competes fairly on "nearest".
    const double scale = static_cast<SectionPlaneGrip>(i) == SectionPlaneGrip::Flip ? kSectionPlaneFlipScale : 1.0;
    const double d = ray3d::Length(ray3d::Sub(g.at[i], onRay)) / scale;
    if (d <= bestD) {
      bestD = d;
      best = static_cast<SectionPlaneGrip>(i);
    }
  }
  return best;
}


void ToggleSectionClipFlip(AppCommandState& st, std::vector<std::string>& log) {
  // REQ-343 amended (issue #479 acceptance 8): flip is a single click with no drag to arm an undo
  // entry at, so it is pushed here, before the flip, exactly like every other one-shot command.
  PushUndoSnapshot(st, "Section plane flip");
  st.viewportSectionClipFlip = !st.viewportSectionClipFlip;
  // The stored extent has to follow the basis, and this is not a detail.
  //
  // `SectionClipPlaneBasis` derives u from the normal — `u = normalize(cross(helper, n))` — and
  // flipping negates n, so **u negates and v does not**. The extent's `cu` is an ABSOLUTE
  // `dot(centre, u)`, so leaving it alone mirrors the rectangle about the storage origin: a plane
  // stretched over a model at y = 400 jumps to y = -400 on flip, 800 ft away, taking every handle
  // with it and leaving a cut that is still correct with no visible plane to grab.
  //
  // Negating `cu` keeps the same world centre through the sign change: `u_new * (-cu) == u_old * cu`
  // because `u_new == -u_old`. `cv` is untouched because `v` is.
  if (st.viewportSectionClipExtent.valid)
    st.viewportSectionClipExtent.cu = -st.viewportSectionClipExtent.cu;
  st.viewportSectionClip = true;
  log.push_back(SectionClipReport(st));
}

void CancelSectionPlaneGripDrag(AppCommandState& st) {
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
}

void AbortSectionPlaneGripDrag(AppCommandState& st) {
  if (st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None))
    return;
  // A TRUE cancel, not a disarm — the wording the gizmo's own ESC branch uses, and the same
  // reasoning. The drag writes the offset and the extent on every frame, so by the time ESC is
  // pressed the plane is already 40 ft into the model; disarming alone would COMMIT that, and
  // REQ-343 records that a slide makes no undo entry, so there would be no way back at all.
  //
  // `sectionPlaneGripStartOffset` and `sectionPlaneGripStartExtent` were recorded at the grab
  // precisely so this can put them back. Until now nothing read them.
  st.viewportSectionClipOffset = st.sectionPlaneGripStartOffset;
  // The extent goes back to what it was BEFORE the grab, which is not always what was recorded:
  // the stretch arithmetic needs a valid extent to work from, so a first stretch seeds one from the
  // drawn rectangle. Restoring that would leave an aborted first stretch with the plane pinned at
  // its then-current size and no longer following the model — invisible, and never asked for.
  if (st.sectionPlaneGripStartExtentWasValid)
    st.viewportSectionClipExtent = st.sectionPlaneGripStartExtent;
  else
    st.viewportSectionClipExtent = SectionPlaneExtent{};
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
}

/// The axis a handle drags along, and the point it starts from. False for handles that are clicks.
static bool SectionPlaneGripAxis(const AppCommandState& st, SectionPlaneGrip grip,
                                 ray3d::Vec3* outAnchor, ray3d::Vec3* outDir) {
  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  if (!g.valid || grip == SectionPlaneGrip::None || grip == SectionPlaneGrip::Flip)
    return false;
  const int i = static_cast<int>(grip);
  if (i < 0 || i >= kSectionPlaneGripCount)
    return false;
  if (ray3d::Length(g.dir[i]) < 0.5)
    return false;
  *outAnchor = g.at[i];
  *outDir = g.dir[i];
  return true;
}

void UpdateSectionPlaneGripDrag(AppCommandState& st, const ray3d::Ray& rayIn,
                                const ray3d::Vec3* snapPoint) {
  const ray3d::Ray ray = SectionPlaneUnitRay(rayIn);
  const SectionPlaneGrip grip = static_cast<SectionPlaneGrip>(st.sectionPlaneGripDrag);
  if (grip == SectionPlaneGrip::None)
    return;
  // The axis recorded AT THE GRAB, not the live one — see `sectionPlaneGripAnchor` for why
  // re-deriving it here makes the drag collapse after one frame.
  const ray3d::Vec3 anchor = st.sectionPlaneGripAnchor;
  const ray3d::Vec3 dir = st.sectionPlaneGripAxis;
  if (ray3d::Length(dir) < 0.5)
    return;
  // How far the HANDLE should travel from where it was grabbed. The two ways of asking are
  // different in kind, and conflating them is the bug this shape exists to prevent.
  double delta = 0.0;
  if (snapPoint) {
    // REQ-344 — land on the object snap.
    //
    // **ABSOLUTE, and that is the whole point.** `anchor` IS the handle's position at the grab, so
    // the snapped point's projection onto the axis is already the distance the handle must travel
    // to put the plane through that point. Nothing is subtracted.
    //
    // Subtracting `sectionPlaneGripStartParam` here — as this did until a user reported the plane
    // landing beside the snap rather than on it — mixes the two kinds. That value is where the
    // CURSOR crossed the axis when the handle was grabbed, which is only zero if the click landed
    // exactly on the handle's centre. Anywhere else inside the grab aperture and the plane ends up
    // wrong by precisely that much, in whichever direction the click was off: "it looks like it is
    // going to snap too far and then snaps too close" (2026-09-11).
    //
    // Every test missed it because every fixture aimed its grab ray straight at the handle, which
    // makes that term exactly zero. `[req344]` now grabs off-centre on purpose.
    //
    // The snapped point almost never lies ON the axis — the axis is a line through the handle, a
    // midpoint is out in the model — so the handle goes where it PROJECTS. That is the only reading
    // a one-degree-of-freedom drag allows, and it is the one that puts the plane exactly through
    // the point, because the plane is perpendicular to the axis it slides along.
    //
    // No further guard on WHETHER to honour it. `CadSnap::FindBest` only answers at all when the
    // cursor is inside the snap aperture of a real feature, and that aperture is pixel-derived, so
    // a point reaching here is by definition one the user is pointing at. A second distance test
    // would be this code second-guessing the snap system with a worse rule — and the case it would
    // reject is the useful one, a midpoint out in the model deliberately reached for.
    delta = ray3d::Dot(ray3d::Sub(*snapPoint, anchor), dir);
  } else {
    // RELATIVE: a delta from where the grab happened, so the handle does not leap to the cursor on
    // the first frame. `sectionPlaneGripStartParam` was recorded against the same anchor and axis,
    // and subtracting it is exactly right HERE — both terms are cursor positions.
    double param = 0.0;
    if (!CadAxisDragParam(anchor, dir, ray, &param))
      return;  // sighting straight down the axis: no distance the gesture could mean
    delta = param - st.sectionPlaneGripStartParam;
  }

  switch (grip) {
  case SectionPlaneGrip::Move:
    // The gesture the whole feature exists for: the plane slides along its OWN normal, so it stays
    // parallel to the face it was created from however the view is turned. A flipped plane has its
    // normal negated, so the offset moves the other way — negating here keeps "drag towards the
    // model" meaning "cut deeper" in both states.
    st.viewportSectionClipOffset =
        st.sectionPlaneGripStartOffset + (st.viewportSectionClipFlip ? -delta : delta);
    break;
  case SectionPlaneGrip::LengthNeg:
  case SectionPlaneGrip::LengthPos:
  case SectionPlaneGrip::HeightNeg:
  case SectionPlaneGrip::HeightPos: {
    // Resizing moves ONE edge: the opposite edge stays put, so the rectangle grows out from where
    // it was rather than about its centre. That is what a grip on an edge means everywhere else in
    // this application, and a centre-symmetric stretch would move an edge the user is not touching.
    SectionPlaneExtent e = st.sectionPlaneGripStartExtent;
    if (!e.valid)
      break;
    const bool isU =
        (grip == SectionPlaneGrip::LengthNeg || grip == SectionPlaneGrip::LengthPos);
    double& half = isU ? e.halfU : e.halfV;
    double& centre = isU ? e.cu : e.cv;
    const double half0 = half;
    // `delta` is how far the DRAGGED EDGE moved, measured along that handle's own outward
    // direction — so a positive delta always grows the rectangle, whichever of the pair was
    // grabbed. Moving one edge by `delta` while the opposite one stays put changes the width by
    // `delta` and therefore the HALF-width by half of it, with the centre following by the same
    // amount. Using the full delta for both moves the grabbed edge twice as far as the cursor,
    // which reads as the plane running away from the pointer.
    half = std::max(kSectionPlaneMinHalfExtent, half0 + delta * 0.5);
    const double grew = half - half0;
    const bool positiveSide =
        (grip == SectionPlaneGrip::LengthPos || grip == SectionPlaneGrip::HeightPos);
    centre += positiveSide ? grew : -grew;
    st.viewportSectionClipExtent = e;
    break;
  }
  case SectionPlaneGrip::Flip:
  case SectionPlaneGrip::None:
  case SectionPlaneGrip::Count:
    break;
  }
}

void UpdateSectionPlaneGripHover(AppCommandState& st, const ray3d::Ray& ray, double tolWorld) {
  // PickSectionPlaneGrip normalizes; nothing here touches the direction itself.
  if (st.sectionPlaneGripDrag != static_cast<int>(SectionPlaneGrip::None))
    return;  // the grabbed handle stays lit; pre-highlighting one the click cannot reach is a lie
  st.sectionPlaneGripHover = static_cast<int>(PickSectionPlaneGrip(st, ray, tolWorld));
}

bool SubmitSectionPlaneClick(AppCommandState& st, const ray3d::Ray& rayIn, double tolWorld,
                             std::vector<std::string>& log) {
  const ray3d::Ray ray = SectionPlaneUnitRay(rayIn);
  if (!st.viewportSectionClip)
    return false;

  // An armed drag: this click DROPS it. The offset and extent are already live — the drag has been
  // updating them every frame, which is what makes the cut follow the handle — so committing is
  // just disarming, and there is nothing to apply. That is a property of the plane being a view
  // state: there is no geometry to rebuild and so no moment at which the change becomes real.
  if (st.sectionPlaneGripDrag != static_cast<int>(SectionPlaneGrip::None)) {
    // Disarm, and ONLY disarm. This used to re-run the drag from the drop click's ray — which had
    // no snapped point, because the click path is not given one — so releasing the mouse recomputed
    // the placement from the raw cursor and threw the snap away. The plane visibly jumped off the
    // feature it had just locked onto at the instant the user let go (reported 2026-09-11).
    //
    // There was never anything for it to do: the live drag has been writing the offset and the
    // extent every frame, which is what makes the cut follow the handle, so the state is already
    // exactly what the user is looking at. Re-applying it could only ever differ from what is on
    // screen, and the comment above said as much while the code did the opposite.
    CancelSectionPlaneGripDrag(st);
    log.push_back(SectionClipReport(st));
    return true;
  }

  if (st.sectionPlaneSelected) {
    const SectionPlaneGrip grip = PickSectionPlaneGrip(st, ray, tolWorld);
    if (grip == SectionPlaneGrip::Flip) {
      // A click, not a drag. Flipping is a discrete choice — there is no halfway between looking at
      // one half and the other — so a drag gesture would be pretending it has a magnitude.
      ToggleSectionClipFlip(st, log);
      return true;
    }
    if (grip != SectionPlaneGrip::None) {
      ray3d::Vec3 anchor{}, dir{};
      if (SectionPlaneGripAxis(st, grip, &anchor, &dir)) {
        double param = 0.0;
        if (!CadAxisDragParam(anchor, dir, ray, &param)) {
          log.push_back("Section plane — you are looking straight down that handle; turn the view "
                        "a little first.");
          return true;
        }
        // REQ-343 amended (issue #479 acceptance 8): one undo entry per drag, pushed at the grab —
        // the same moment "Grip edit" pushes for every other grip drag in the file — not per frame,
        // since the live frames only keep writing the SAME entry's already-captured pre-drag state.
        PushUndoSnapshot(st, grip == SectionPlaneGrip::Move ? "Section plane slide" : "Section plane resize");
        st.sectionPlaneGripDrag = static_cast<int>(grip);
        st.sectionPlaneGripAnchor = anchor;
        st.sectionPlaneGripAxis = dir;
        st.sectionPlaneGripStartParam = param;
        st.sectionPlaneGripStartOffset = st.viewportSectionClipOffset;
        // Seed the stored extent from the rectangle as it is RIGHT NOW, so the first stretch keeps
        // the size the user is looking at and only moves the edge they grabbed. Without this the
        // plane would snap to a default size the instant a stretch began.
        st.sectionPlaneGripStartExtentWasValid = st.viewportSectionClipExtent.valid;
        st.sectionPlaneGripStartExtent = st.viewportSectionClipExtent.valid
                                             ? st.viewportSectionClipExtent
                                             : SectionPlaneExtentFromQuad(CadSectionClipIndicator(st),
                                                                          CadSectionClipPlane(st));
        return true;
      }
    }
  }

  // Not a handle: the SECTION LINE selects the plane.
  //
  // **The line, not the rectangle interior.** The rectangle is sized to the model plus a margin, so
  // in plan view its screen projection covers everything in the drawing — and consuming every click
  // that landed inside it made every solid, grip, survey point and selection window unreachable for
  // as long as the clip was on. Depth arbitration does not rescue that: a plane sitting on a box's
  // top face is genuinely NEARER the camera than the box, so it would still win.
  //
  // A thin, deliberate target is also what AutoCAD uses, and it is why the line is drawn heavier
  // and brighter than everything else on the plane. The handles keep their own aperture above.
  const SectionPlaneGraphics gfx = SectionPlaneGraphicsFor(CadSectionClipIndicator(st));
  if (gfx.valid && ray3d::RaySegmentDistance(ray, gfx.lineA, gfx.lineB) <= tolWorld) {
    if (!st.sectionPlaneSelected) {
      st.sectionPlaneSelected = true;
      // Mutually exclusive with the entity selection, the rule REQ-318 item 9 already applies to
      // sub-objects. Without this the two are simultaneously live and DELETE has to guess which the
      // user meant — it took the plane, silently, while a selected solid survived.
      st.selection.clear();
      st.subObjectSelection.clear();
      log.push_back("Section plane selected — drag the centre handle to slide it, the arrow to "
                    "flip it, the end handles to resize it.");
    }
    return true;
  }
  if (st.sectionPlaneSelected) {
    // Deselect, but do NOT consume: the click still means whatever it would have meant. Consuming
    // it cost the user a second click for every "click away from the plane onto something else",
    // and with the rectangle as the target that was most clicks in the drawing.
    st.sectionPlaneSelected = false;
    st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  }
  return false;
}
