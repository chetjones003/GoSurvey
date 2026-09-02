# TASK-176 — Specify curved polyline segments (POLYLINE arc mode + JOIN)

- Type:    docs (specification)
- Status:  done
- Opened:  2026-09-02
- Owner:   chetjones003

## 1. Authority

- Requirements: **REQ-316** (proposed → accepted 2026-09-02)
- ADR:          **ADR-047**
- Decision:     **D-2026-09-02-b**
- Owning subsystem: Specification layer (`spec/`)

## 2. Why this is a Specification task, not a Workshop task

A feature request arrived — "extend POLYLINE to switch between line mode and arc
mode mid-command, and let JOIN weld lines + arcs into one polyline" — with **no
accepted `REQ-NNN`** and **no ADR**. CLAUDE.md §5 / the boundary check stop
implementation here:

- The polyline geometry store (`userPolylineVerts`, stride-3 XYZ) carries only
  corner points; every consumer assumes straight chords. Adding a per-vertex
  bulge is a **data-model + data-format change** — architecture §11.4 and §11.8
  make it a recorded architectural decision, not a Workshop choice.
- `spec/requirements.md` REQ-085 explicitly recorded that curvature would be "a
  storage change" the project was avoiding — so this reverses a stated
  non-goal and must be deliberate.
- The store has ~922 reference sites across 17 files; a stride widening of it is
  the kind of blast radius the decision log exists to authorise.

## 3. SPEC GAP writeup (presented to the user)

**What the spec said:** a polyline is a list of straight-connected points; there
is no field for segment curvature. The DXF importer parses the `LWPOLYLINE`
group-42 bulge and immediately discards it by tessellating each arc into short
straight chords. The exporter writes no bulges. JOIN welds only straight lines
and straight polylines and refuses everything else.

**What was missing:** no requirement for arc segments in a polyline or for JOIN
of arcs; no decision on how curvature is stored; no `.gs`/DXF format rule.

**Why Claude could not decide it:** the cheap implementation ("fake" arcs as many
short straight segments) fails the user's own acceptance criteria (tangent arc,
arc-centre snap, DXF bulge round-trip), and the correct implementation changes
the drawing's core geometry store and two file formats — architecture §11.4/§11.8
reserve that for a recorded decision with the user.

**Choices presented:** (A) true bulge polylines via a new ADR + REQ, phased;
(B) faked arcs, storage unchanged; (C) separate entities, JOIN tessellates;
(D) defer. **Recommendation: A.**

## 4. Resolution

User chose **A** and accepted **ADR-047** as written. Two design points settled
with the user:

1. **Storage is a parallel per-vertex array** — `std::vector<float>
   userPolylineVertsBulge`, one bulge per vertex, across all three copies of the
   store. (Originally accepted as a stride 3→4 rename-widen; corrected 2026-09-02
   before any storage code — the rename's compile-error claim is false for a
   `std::vector<float>`, so 401 sites would be hand-audited unguarded. See the
   ADR-047 (a) correction note.) Chosen over a distinct arc-polyline entity kind
   (ADR-035 (g) ~600-site cost, still would not give lines-and-arcs in one entity).
2. **Delivery is four independently shippable increments** (ADR-047 consequences):
   1. storage + POLYLINE `Arc`/`Line` sub-modes + arc render + DXF/`.gs` round-trip
   2. snap / pick / extents / length / Properties on bulge segments
   3. JOIN of lines + arcs, and arc-segment grips
   4. TRIM / OFFSET / FILLET / CHAMFER of bulge polylines

## 5. Spec changes made

- `spec/architecture.md` — added **ADR-047**; amended invariant **§11.8** to record
  the polyline vertex store as stride-4 `x,y,z,bulge`.
- `spec/project.md` — added decision-log row **D-2026-09-02-b**.
- `spec/requirements.md` — added **REQ-316** (accepted), with full acceptance
  criteria; cross-referenced from REQ-315's neighbourhood.
- `spec/roadmap.md` — added the four-slice item under **Next**.

## 6. Next

Increment 1 is **TASK-177**. No implementation code lands until that task's
Authority + Plan sections pass Verification.

## 11. Outcome

- Requirements specified: REQ-316 (accepted)
- ADR: ADR-047 (accepted)
- Decision: D-2026-09-02-b
- Docs updated: architecture.md, project.md, requirements.md, roadmap.md
- Done: 2026-09-02
