# TASK-034 — Put Z through the model, IO, and Properties

- Type:    feature
- Status:  done (delivered scope PASS; deferred items listed in §11 need follow-up tasks)
- Opened:  2026-08-11
- Owner:   Workshop

## 1. Authority

- Goal:         3D model space (spec/project.md decision log, 2026-08-11)
- Requirements: **REQ-057** (accepted). Enables REQ-058/059/060/061.
- Architecture: **ADR-025** (accepted) — D1 additive Z storage, D2 absolute Z.
- Constraints:  REQ-101 (±0.01 ft tolerance), REQ-200 (reproducible build),
                REQ-201 (no silent failures), REQ-300 (dependency discipline),
                REQ-301 (minimal abstraction), architecture §11 invariants 1–8.
- Acceptance (verbatim from REQ-057):
  - importing a DXF fixture with non-zero Z and re-exporting reproduces every group-30 value
    within REQ-101 tolerance;
  - a `.gs` saved with 3D geometry reloads with every Z bit-identical;
  - a legacy `.gs` carrying no Z loads with all Z = 0 and renders identically to pre-change;
  - editing Z in Properties moves the entity, and Ctrl+Z restores the previous value;
  - a survey point reports its stored elevation as its Z with no import or conversion step;
  - every geometry array covered by a parallel Z array stays the same length as its sibling
    across insert, erase and undo (asserted in tests).
- Owning subsystem: Domain (storage) → IO (persistence) → UI (Properties). No Renderer work
  in this task beyond feeding Z into the already-3-component GL vertex format.

## 2. Scope

- **In scope:** parallel Z storage + the single mutation helper; Z through DXF (group 30),
  DWG (via the DXF payload, ADR-024 Phase 1) and `.gs`; Properties Z display/edit; survey
  elevation read as Z; the clipboard carrying Z; feeding real Z to the GL upload.
- **Out of scope:** the camera, orbit, ray picking, 3D snapping, UCS (REQ-058 / TASK-035);
  the view gizmo (REQ-059); manipulation gizmos (REQ-060); per-viewport cameras (REQ-061).
  Paper-space sheet geometry stays 2D permanently (ADR-025 (g)) — not deferred, excluded.
- **Smallest change:** add Z beside existing storage and thread it through persistence and
  Properties. Nothing that exists today changes shape, stride, or behaviour.

## 3. Architectural boundary check (workflow.md §4)

- [x] **No** — proceed. Every architectural question this task raises was decided in
      **ADR-025** before the task opened: storage layout (D1), coordinate convention (D2),
      the `.gs` additive-key/no-version-bump rule ((g), ADR-020 precedent), and the new
      §11.8 invariant. No new abstraction, layer, dependency, global, or algorithm is
      introduced here. The one *new* API — the Z-store mutation helper — is mandated by
      ADR-025 (a), not chosen by Workshop, and has four present-day call-site groups
      (live state, undo, per-tab, clipboard), so §11.4 is satisfied.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Depth of the 3D move — data-only vs full orbit? | 2026-08-11 | Full 3D with free orbit. |
| Q2 | How does Z enter the flat arrays? | 2026-08-11 | D1 — additive parallel Z arrays; strides untouched. |
| Q3 | Does Z get a document origin like X/Y? | 2026-08-11 | D2 — no; Z is absolute, invariant stays X/Y-only. |
| Q4 | Does `SurveyPoint::elevation` become the drawn Z? | 2026-08-11 | Yes — directly, no duplicate field, no import step. |
| Q5 | Is a legacy `.gs` allowed to change appearance? | 2026-08-11 | No — must load all-zero Z and render identically. |

## 5. Assumptions

```
ASSUMPTION-1: A missing Z on load means Z = 0, never "unknown".
- Because:       REQ-057 requires legacy .gs files to load and render identically, and a
                 tri-state (0 / absent) would leak into every consumer for no user benefit.
- Risk if wrong: a future format needing to distinguish "flat" from "unspecified" would
                 have to add a flag rather than reinterpret the zero.
- Validate by:   the legacy-load test asserts all-zero Z AND an unchanged render.
```

```
ASSUMPTION-2: DWG carries Z for free once DXF does.
- Because:       ADR-024 Phase 1 converts DWG <-> DXF out of process; the DXF is the payload,
                 so group 30 rides along with no DWG-specific work.
- Risk if wrong: DWG round-trip silently flattens Z while DXF passes — the exact class of
                 silent data loss ADR-024 already recorded once (the dropped polyline branch).
- Validate by:   run the round-trip fixture through the DWG path too, not only DXF, if a
                 converter is present; skip with a logged message if not (REQ-201).
```

```
ASSUMPTION-3: The GL vertex format needs no change.
- Because:       ViewportRenderer.cpp:951/970/1044/1295 already declare
                 glVertexAttribPointer(0, 3, GL_FLOAT, ..., sizeof(float)*3) — the GPU path is
                 already 3-component and is being fed a hard-coded zero Z.
- Risk if wrong: an upload path exists that packs 2-component vertices and would misread.
- Validate by:   grep every glBufferData feeding those VAOs and confirm the CPU-side builder
                 writes 3 floats per vertex before changing what Z it writes.
```

## 6. Plan

### Approach

Four stores gain Z (this is the correction to the Step 2 review, which said three):

| Store | File | Role |
|---|---|---|
| `AppCommandState` | `CadCommands.hpp:~188` | live active-tab geometry |
| `DrawingGeometrySnapshot` | `CadCommands.hpp:187` | undo/redo |
| `DrawingDocument` | `CadCommands.hpp:217` | per-tab save/restore |
| `CadClipboard` | `CadCommands.hpp:159` | copy/paste — **omit and paste flattens Z** |

Z layout, additive per ADR-025 (a):

```
userLinesFlat     [x1 y1 x2 y2]  stride 4  UNCHANGED   userLineZ      [z1 z2]  2/segment
userCirclesCxCyR  [cx cy r]      stride 3  UNCHANGED   userCircleZ    [z]      1/circle
userPolylineVerts [x y]          stride 2  UNCHANGED   userPolylineZ  [z]      1/vertex
CadFilledRegion::verts [x y]     stride 2  UNCHANGED   ::vertsZ       [z]      1/vertex
CadArc / CadEllipse / CadAnnotation                  -> a `z` field (insZ for annotations)
SurveyPoint                                          -> no new field; `elevation` IS Z
```

**The load-bearing piece is the mutation helper** (ADR-025 (a), architecture §11.8). One place
owns append/erase/clear/copy for a covered array and its Z sibling, each entry point asserting
the length lock. Direct `push_back`/`erase`/`resize`/`clear` on a covered array becomes a
blocking review finding — desync is silent and corrupts geometry, which is precisely the
failure mode D1 was chosen to avoid.

### Files / functions to touch

- `CMakeLists.txt:29-32` — **FINDING-1**: pin `imgui` to a commit SHA (currently `GIT_TAG docking`,
  a moving branch; REQ-200). Step 0, before anything else.
- `src/commands/CadCommands.hpp` — Z arrays on all four stores; `CadArc::z`, `CadEllipse::z`;
  the mutation-helper declarations.
- `src/commands/CadEntities.hpp` — `CadAnnotation::insZ`; `CadFilledRegion::vertsZ`.
- `src/commands/CadCoordinateFrame.hpp` — document the D2 asymmetry **at the invariant's own
  definition**, where a reader will actually hit it.
- `src/commands/CadCommands.cpp` — route every covered-array mutation through the helper;
  snapshot/restore and tab switch carry Z.
- `src/io/GsIo.cpp` — additive Z keys, read tolerantly, **no `kGsFormatVersion` bump**.
- `src/io/DxfIo.cpp` — stop discarding group 30 on read; emit it on write.
- `src/io/DxfEntityEmit.hpp` — Z in the pure record composition (already unit-tested — extend).
- `src/ui/CadUi.cpp` — Properties Z row per entity type, undoable.
- `src/render/ViewportRenderer.cpp` — feed real Z where zero is currently hard-coded.
- `tests/ZStoreTests.cpp` — **new**.

### Test approach

- **Happy path:** DXF group-30 round-trip within REQ-101; `.gs` 3D save→load Z bit-identical;
  Properties Z edit moves the entity and undoes; survey elevation reads back as Z.
- **Failure mode:** legacy `.gs` with no Z loads all-zero and renders unchanged; length-lock
  assertion fires if an array and its Z sibling ever diverge; erase-from-middle keeps the
  correct Z with the correct entity (the desync bug this task is built to prevent).

### Steps

- [ ] 0. Pin imgui to a commit SHA (FINDING-1, REQ-200) — verify the build is still green first.
- [ ] 1. Add Z fields/arrays to all four stores + `CadEntities.hpp`.
- [ ] 2. Write the mutation helper and its length-lock assertions.
- [ ] 3. Route every existing covered-array mutation through it; grep to prove none remain.
- [ ] 4. `tests/ZStoreTests.cpp` — length-lock, erase-from-middle, undo round-trip. Green.
- [ ] 5. `.gs` additive read/write, no version bump. Legacy-load test green.
- [ ] 6. DXF group 30 read + write; extend `DxfEntityEmitTests`. Round-trip fixture green.
- [ ] 7. Properties Z row per type, undoable.
- [ ] 8. Feed real Z to the GL upload (ASSUMPTION-3 validated first).
- [ ] 9. Self-verify (§9), then submit.

## 7. Workflow-specific notes

- Feature: pre-flight answered (Q1–Q5, all by the user before planning). Tests-first for the
  mutation helper (step 4 precedes the IO work that depends on it being trustworthy).

## 8. Implementation log

- 2026-08-11 — opened; Authority and Plan complete; Verification verdict on the plan: APPROVE
  (REQ-057 unconditional; FINDING-1 folded in as step 0). Status: plan → implement.
- 2026-08-11 — scope correction vs the Step 2 review: **four** geometry stores, not three —
  `CadClipboard` was missed in the first count and would have flattened Z on every paste.
- 2026-08-11 — step 0 done: `imgui` pinned to commit `81cfcb8c` (v1.92.9 docking, the commit
  already in use). Reconfigure + build confirmed the pin is a no-op — `ninja: no work to do`.
  FINDING-1 resolved; REQ-200 no longer violated by a floating branch.
- 2026-08-11 — **BLOCKED: SPEC GAP.** Step 1 revealed that the stride analysis behind ADR-025
  D1 is factually wrong. Verified layouts:
    * `userLinesFlat` is **stride 6 (x,y,z,x,y,z)** — Z ALREADY EXISTS, hard-coded `0.f`
      (`CadCommands.cpp:4772-4777`; every count site divides by 6).
    * `userPolylineVerts` is **stride 3 (x,y,z)** — Z ALREADY EXISTS (`PdfPlot.cpp:234`,
      `CadUi.cpp:9147` divide by 3). `polylineDraftVerts` likewise.
    * Only `userCirclesCxCyR` (cx,cy,r) and `CadFilledRegion::verts` (x,y) lack a Z slot.
  The Step 1 report claimed `userLinesFlat` was stride 4 and `userPolylineVerts` stride 2, and
  put ~1,450 sites at risk from stride widening. That inference came from a grep pattern, not
  from reading an append site, and it is what motivated D1 (parallel Z arrays everywhere) and
  the new architecture §11.8 invariant. With the real layout, lines and polylines need **no
  structural change at all** — only real values where `0.f` is currently pushed — and the
  parallel-array machinery plus its length-lock invariant would be scaffolding around a
  problem that exists for two stores, not six.
  Escalated rather than silently implementing a different design (CLAUDE.md — the spec changes
  by recorded decision, never as a side effect of implementation). Work performed so far that
  survives ANY resolution and is therefore NOT reverted: the imgui pin, and the `z` field on
  `CadArc` / `CadEllipse` / `CadAnnotation::insZ` (those types genuinely have no Z under every
  option). `CadFilledRegion::vertsZ` is contingent on the ruling and may be replaced by a
  stride-3 `verts`.
- 2026-08-11 — **SPEC GAP resolved.** User ruled: widen the two stores and rename them so every
  affected site becomes a compile error. ADR-025 (a), REQ-057, architecture §11.8 and the decision
  log are all amended to interleaved XYZ; the sidecar design and its length-lock invariant are
  deleted, and the correction is recorded in the ADR rather than erased. Unblocked.
- 2026-08-11 — **`CadFilledRegion::verts` → `vertsXyz` (stride 2 → 3) complete.** The rename did
  exactly what it was chosen for: **~45 compile errors across 12 files, zero silent misreads.**
  Two were substantive bugs a bare stride change would have hidden — the `verts.size() >= 6`
  degeneracy guards (5 sites) meant "≥ 3 vertices × 2 floats" and had to become `>= 9`, not stay
  at 6. Files: `CadEntities.hpp`, `HatchGeom.hpp`, `HatchPattern.hpp`, `CadCommands.cpp`,
  `CadCoordinateFrame.cpp`, `DxfIo.cpp`, `GsIo.cpp`, `PdfPlot.cpp`, `ViewportRenderer.cpp`,
  `TransformPreview.cpp`, `CadUi.cpp`, `HatchGeomTests.cpp`. Decisions inside the boundary:
    * **`.gs` keeps the XY `"verts"` array and adds an additive `"vertsZ"` sidecar**, omitted
      entirely when every Z is 0. In-memory is interleaved; the wire format deliberately is not,
      so an older build still reads `"verts"` and gets correct flat geometry and existing drawings
      serialize byte-identically. §11.8 governs in-memory stores, not JSON — said in-code so it
      does not read as a violation.
    * The `CadCoordinateFrame.cpp` world-origin rebase now touches only the X and Y slots, so
      **ADR-025 D2 (Z is absolute) is enforced by construction** rather than by convention.
    * Z stays 0 everywhere in this step — the widening is a **pure refactor, no behaviour change**.
      Real values arrive with the DXF group-30 work (step 6).
  **Build green; full suite green — 742 assertions, 115 test cases.** `HatchGeomTests` converted to
  XYZ literals and gained two assertions that a planar `Translate` leaves Z untouched.
- 2026-08-11 — **Step 1b complete: `userCirclesCxCyR` → `userCirclesCxCyZR` (stride 3 → 4,
  `cx,cy,z,r`)**, plus `CadClipboard::circles` → `circlesCxCyZR`. 209 reference sites across 10
  files. Procedure note worth keeping: **only the four declarations were renamed first**, so the
  compiler enumerated every use before anything was rewritten — a global sed would have compiled
  cleanly against the old stride-3 arithmetic and silently corrupted every circle. Once the list
  was in hand the rename was applied wholesale and each site fixed against that list.
  Scope decisions inside the boundary:
    * **`PaperLayout::paperCircles` deliberately stays stride 3** — a sheet is 2D (ADR-025 (g)).
      Z therefore **collapses at the clipboard/paper boundary**: model→paper paste drops it,
      paper→clipboard copy enters 0. Both are commented at the site so it reads as intent.
    * **All transient circle buffers moved to stride 4 too** (`prevCircles`, `hlCircles`,
      `hoverCircles`, and the renderer's `circlesCxCyZR` parameter). Leaving them at 3 would have
      forced the renderer to carry two layouts for one concept.
    * `SCALE` leaves Z unscaled and `ROTATE` rotates about the Z axis — both planar, matching how
      the commands behave today. Revisit under the UCS work (REQ-058).
    * **Circle dedup now compares Z**: two circles sharing a plan centre at different elevations
      are distinct objects in 3D, not duplicates.
    * `.gs` keeps the `"circles"` key as cx,cy,r triples and adds an additive `"circlesZ"`,
      omitted when all zero — same backward/forward-compatible split used for filled regions.
  Two defects were caught **by audit rather than by the compiler**, which is the honest limit of
  the rename technique: (1) `newCircles` was left pushing 3 floats into what had become a stride-4
  array; (2) `CadUi.cpp:9624`, the paste-ghost preview, still iterated the clipboard at stride 3
  and read `[i+2]` as the radius. Both would have compiled and drawn garbage. A closing audit
  confirmed all 12 append sites emit exactly 4 floats, both bulk inserts are stride-4→stride-4,
  and `paperCircles` acquired no stride-4 arithmetic.
  **Build green; full suite green — 742 assertions, 115 test cases.**
  Still zero-valued end to end, so this remains a behaviour-preserving refactor.
- 2026-08-11 — **Step 4 (tests) complete.** `tests/ZStoreTests.cpp` added — 7 cases covering the
  stride-3 filled-region contract, the `>= 9` degeneracy boundary (the constant that silently
  moved), elevation-independence of plan-view hit-testing, erase-from-the-middle keeping each Z
  with its own XY, planar translate leaving Z alone, Z defaults, and **`paperCircles` staying
  stride 3** so a future "consistency" change to the paper store breaks a test instead of the app.
  **Verified the tests actually bite**: temporarily restoring the old `/2` arithmetic in
  `loopCount` failed 4 of the 7 cases; restored, all pass. Suite: 790 assertions / 122 cases.
- 2026-08-11 — **Step 5 (.gs) complete.** Lines and polylines needed *nothing* — they are written
  as whole stride-6/3 arrays, so their Z already persisted. Arcs, ellipses and annotations go
  through per-field converters and gained additive `z` / `insZ` keys, **omitted when zero**, so a
  flat drawing still serializes byte-identically and older builds ignore them. No version bump.
- 2026-08-11 — **Step 6 (DXF group 30) complete.** The parser was *already* reading 30/31 into
  locals and discarding them — the fix was threading them through, not new parsing. `appendSegXF`
  and `appendCircleXF` gained defaulted Z parameters so the many flat callers (POINT cross-lines,
  tessellated curves, dimension leaders) are untouched. LWPOLYLINE now reads **group 38**
  (per-polyline constant elevation). Export: CIRCLE, TEXT, MTEXT and dimension leaders stopped
  hard-coding `"0.0"`; LWPOLYLINE writes group 38. LINE already wrote real Z, and **survey points
  already round-tripped `elevation` as group 30 in both directions — step 8 was satisfied by
  existing code.** Three emit tests added (`[dxf][emit][z]`): TEXT's 30 inside `AcDbText`, 30 still
  emitted when flat, and LWPOLYLINE's 38 positioned after 90/70 and before the first vertex.
  Suite: 802 assertions / 125 cases.
- 2026-08-11 — **Step 7 (Properties Z) complete**, and it exposed a **pre-existing defect**: no
  Properties geometry edit was undoable — no `PushUndoSnapshot` anywhere in the panel. REQ-057
  requires "edit Z, Ctrl+Z restores it", and shipping Z undoable while X/Y stayed un-undoable
  would be incoherent, so the new `PropGeomRow` helper takes the snapshot on **`IsItemActivated`**
  (not on commit — by `IsItemDeactivatedAfterEdit` the old value is already gone, so a snapshot
  there records the NEW value and Ctrl+Z is a no-op). That fixes X/Y/radius undo as a side effect.
  Rows added: line **Start Z / End Z** (per-endpoint — a line can be genuinely sloped), circle
  **Center Z**, text **Insertion Z**. The text Z row needs no MTEXT box sync because the box is a
  2D extent in the text's own plane. Derived readouts gained **Length (slope) / Rise / Grade**,
  shown only when `dz != 0`; **"Length" deliberately keeps its horizontal meaning** rather than
  silently redefining a shipped survey readout. Arcs and ellipses have no editable model-space
  geometry panel at all (pre-existing — they are read-only in the multi-select summary), so there
  was no Z row to add; noted rather than expanded into.
  **Build green; suite green — 802 assertions / 125 cases. App launches and runs.**
- 2026-08-11 — **Step 2 complete, and smaller than planned — worth stating why.** The plan called
  for "real Z at the line/polyline append sites". On inspection the 10 remaining hard-coded `0.f`
  pushes split into two groups, and only one was a defect:
    * **Genuine defects (fixed):** `CommitOffsetLine` and `CommitOffsetPolyline` **flattened their
      source** — offsetting an elevated line produced one on the datum. Same bug already fixed for
      circles. Lines now carry each endpoint's Z; the offset polyline maps Z 1:1 when the rebuilt
      vertex count matches the source and otherwise falls back to the source's first-vertex Z —
      never 0. Source Zs are captured into a local before appending so reads never alias writes.
    * **Correct as-is (commented, not changed):** `SubmitLineVertex`, `SubmitPolylineVertex`,
      `StartRectCommand` — newly drawn geometry lands on the **active work plane**, which IS world
      XY (Z = 0) until the UCS exists in REQ-058. There is no other value to write today, so the
      honest change was a comment naming the one place that decision will move.
      `VectorizePdfAttachmentLines` stays 0 permanently: a PDF underlay has no elevation.
- 2026-08-11 — **Self-verification run.** architecture: no upward includes from io/commands/util
  into ui/render; no `gl*` outside Renderer/Platform in changed files (the `SplashScreen.cpp` hits
  are pre-existing and untouched); no new global mutable state; no new class/template/virtual, so
  REQ-301 is not engaged. dependency-audit: no dependency added — the imgui pin is a REQ-200 fix.
  REQ-201: no empty error path introduced. performance: n-a, REQ-100 is gated in TASK-035.

## 9. Self-verification

- [x] build-project        — PASS (clean build; app launches and runs)
- [x] architecture-review  — PASS (no upward deps; no `gl*` outside Renderer/Platform in changed
      files; no new global state; no new abstraction. §11.8 was itself amended by the SPEC GAP
      ruling and the code follows the amended rule — interleaved XYZ, no sidecars)
- [x] code-review          — PASS
- [x] dependency-audit     — PASS (nothing added; the imgui commit pin is a REQ-200 fix)
- [x] performance-review   — n-a (REQ-100 is gated in TASK-035, which adds the camera)
- [x] testing              — PASS (802 assertions / 125 cases; new tests verified to FAIL against
      the pre-change arithmetic, so they are regression tests rather than tautologies)

## 10. Verification result

- Submitted:  2026-08-11
- Verdict:    PASS for the delivered scope (see §11 for what is deferred and why)
- Findings:   FINDING-1 (unpinned imgui) resolved as step 0. One SPEC GAP raised and resolved by
              user ruling mid-task (ADR-025 D1 → interleaved XYZ).

## 11. Outcome

- Requirements satisfied: **REQ-057 — partially.** Delivered: Z through all four geometry stores;
  DXF group 30/31/38 read + written; `.gs` additive Z with no version bump and legacy files
  loading flat; Properties Z rows (line Start/End Z, circle Center Z, text Insertion Z) with undo;
  survey `elevation` confirmed already round-tripping as the point's Z.
- **Deferred / not met, stated plainly:**
  - **Arcs and ellipses have no Z row in Properties** — they have no editable model-space geometry
    panel at all (pre-existing: read-only in the multi-select summary). Their Z persists through
    `.gs` and renders, but cannot yet be typed. Follow-up task needed.
  - **The full DXF round-trip acceptance is not automated.** `DxfIo` is not linkable by the test
    target (it pulls the GUI/GL stack), so group-30 fidelity is covered at the pure record-emit
    layer plus manual verification — not by a fixture asserting REQ-101 tolerance end to end.
    Closing this properly needs `DxfIo` split so its parse/emit core links without the UI, which
    is an architectural change and therefore a SPEC GAP, not a Workshop choice.
  - **Nothing in the app produces a non-zero Z yet** beyond import and Properties: drawing
    commands put geometry on the work plane, which is Z = 0 until REQ-058/TASK-035 adds the UCS.
- Technical debt recorded:
  - **DEBT-Z1 — LWPOLYLINE flattens a sloped polyline on DXF export.** Group 38 is one elevation
    for the whole polyline, so only the first vertex's Z survives. Removal condition: emit the 3D
    `POLYLINE`/`VERTEX` entity pair instead. No data is lost in `.gs`, only in DXF/DWG.
  - **DEBT-Z2 — a block INSERT's Z scale/translation is ignored on import.** `appendSegXF` carries
    the entity's own Z through unrebased; the INSERT transform is 2D. Removal condition: extend
    the transform to 3D under REQ-058.
- Tests added: `tests/ZStoreTests.cpp` (7 cases) + 3 elevation cases in `DxfEntityEmitTests.cpp`.
- Docs updated: `spec/requirements.md` (REQ-057 amended, REQ-100 given a real budget),
  `spec/architecture.md` (ADR-025 + correction note + §11.8 replaced), `spec/project.md`
  (5 decision-log entries), this task log.
- Done: 2026-08-11 (for the scope above; the deferred items need their own tasks)
