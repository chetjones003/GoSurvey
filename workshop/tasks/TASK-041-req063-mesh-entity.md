# TASK-041 — REQ-063: triangle mesh entity

- Type:    feature
- Status:  done — §5 SPEC GAP resolved before any code; see §7 for what is deliberately incomplete
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-063** (accepted 2026-08-12). Step 2 of the M-Models milestone; TASK-040
  (REQ-064 visual styles) is done, so there is somewhere to draw a mesh.
- Architecture: **ADR-026 (c)** — a mesh is a new entity type and is **reference geometry**: not
  authored, not edited, not exported.
- Constraints: architecture §11.5 (one visible owner), §11.8 (interleaved XYZ), REQ-100, REQ-201.
- Owning subsystem: Domain (store), IO (`.gs`), Renderer (draw).

## 2. Planned shape

```
struct CadMeshPart {           // a named sub-range: one imported object
  std::string name;
  int   indexBegin = 0;        // into CadMesh::indices
  int   indexCount = 0;
  float r = 0.8f, g = 0.8f, b = 0.8f;
};
struct CadMesh {
  std::vector<float>    vertsXyz;    // interleaved x,y,z — §11.8 applies unchanged
  std::vector<float>    normalsXyz;  // one per vertex, parallel to vertsXyz
  std::vector<uint32_t> indices;     // triangle list
  std::vector<CadMeshPart> parts;
};
```

One imported model is one `CadMesh` entity; parts carry names and colours for drawing. **Selection
is at mesh level**, which is what REQ-063's "erasing a mesh is undoable in one step" describes.
Part-level selection is not implemented and is not asked for by any requirement — it would be its
own, and it would drag part-level erase, colour override and property display with it.

## 3. Architectural boundary check (workshop/workflow.md §4)

- [ ] **YES — an ownership change is required.** See §5. Escalated, not decided.

## 4. What was verified before stopping

- `DrawingGeometrySnapshot` (CadCommands.hpp:191) holds **a deep copy of every geometry array**.
- `CaptureGeometrySnapshot` copies each vector by value; `PushUndoSnapshot` pushes one per edit.
- `undoHistoryMaxSize` defaults to **50** frames per drawing tab.

## 5. SPEC GAP — meshes cannot go into the undo snapshot as-is

Adding `std::vector<CadMesh>` to `DrawingGeometrySnapshot` the way every other entity store is added
makes each undo frame deep-copy the whole model. For REQ-063's own stated ceiling of **2 million
triangles**:

| | size |
|---|---|
| indices — 2M tris × 3 × `uint32` | ~24 MB |
| vertices + normals — ~1.2M verts × 6 floats | ~29 MB |
| **one snapshot** | **~53 MB** |
| **× 50 undo frames** | **~2.6 GB** |

And the cost is not paid only when editing a mesh. Every unrelated edit — drawing a line — pushes a
snapshot that deep-copies the model with it. Fifty lines drawn near a loaded model is 2.6 GB.

**This is an ownership question, which CLAUDE.md lists explicitly as architectural** ("ownership
changes … → escalate as SPEC GAP"), so the Workshop is not deciding it. Options:

1. **`std::shared_ptr<const CadMesh>` in both the live store and the snapshot.** A snapshot copy
   becomes a pointer bump. Follows directly from ADR-026 (c): meshes are never edited, so the heavy
   payload is immutable and sharing it carries no mutation hazard. Needs an amendment or an explicit
   carve-out to architecture **§11.5** ("every resource has exactly one visible owner") — the
   invariant exists to prevent mutation-ordering hazards, which immutable data cannot have, but that
   reading should be recorded rather than assumed. *Recommended.*
2. **Keep meshes out of the undo system entirely**, in a separate non-undoable store. Cheapest to
   build, but erasing a mesh stops being undoable, which contradicts REQ-063's acceptance — so this
   needs the requirement amended, not just the architecture.
3. **A document-level append-only mesh pool; snapshots store integer IDs.** Same benefit as (1)
   without `shared_ptr`, but introduces a pool and a lifetime question (when is a pooled mesh ever
   freed?) that (1) answers for free via refcount.
4. **Accept the cost and cap undo when a mesh is present.** Rejected on sight — surprising, and it
   silently weakens undo based on what is in the drawing.

> **Resolved 2026-08-12 — option 1.** Recorded in `spec/project.md`'s decision log and as an
> amendment to architecture **§11.5**, which now governs *mutable* resources only: shared ownership
> of an **immutable** payload is permitted, held as `shared_ptr<const T>`, with "editing" meaning
> replacing the pointer rather than writing through it. `shared_ptr<T>` to mutable data remains a
> blocking finding. Meshes are stored as `shared_ptr<const CadMesh>` in both the live state and the
> snapshot, so an undo frame costs a refcount bump instead of ~53 MB.

### Pre-existing, and not this task's to fix

The same scaling issue already exists without meshes: the REQ-100 bench scene (250k segments) is
~3 MB of polyline vertices, so 50 undo frames is ~150 MB today. Meshes make it roughly 20× worse and
impossible to ignore, but the shape of the problem predates them. Recorded here because whichever
option is chosen may or may not want to address the general case too.

## 6. Verification

**Tests:** 64,992 → **65,078 assertions**, 203 → **217 cases**, green. Two new files:

- `tests/MeshGeomTests.cpp` (10 cases) — the module standing between an imported file and the GPU.
  The case that matters most is **an index past the last vertex**, tested with both a plausible
  overrun and `0xFFFFFFFF` (what a truncated file usually yields): unchecked, that is an
  out-of-bounds read inside the driver, which fails neither politely nor reproducibly. Also: every
  structural malformation reports a *distinct* reason rather than a generic failure; an empty mesh
  has **invalid** bounds rather than bounds at the origin (a valid (0,0,0) box would silently drag
  zoom-extents to the origin for any drawing containing one); normals come out unit-length even for
  unreferenced vertices (a zero normal shades black, which reads as a hole); and normal computation
  is **area-weighted** — verified with a 50-area face against a 0.005-area sliver sharing a vertex,
  because normalising per face first would let the sliver vote equally and shade coarse meshes wrong.
- `tests/MeshGsRoundTripTests.cpp` (4 cases) — `GsIo.cpp` is not linkable here, but the risk behind
  the "bit-identical" acceptance condition is not in that file; it is the float→text→float
  conversion underneath. Tested with `memcmp`, not `Approx`, over state-plane magnitudes, denormals,
  `2^23`, negatives and exact binary fractions, plus `uint32` indices past `2^24`.

**In the running app.** `main()` takes no argv, so the fixture is loaded through
`defaultWorkspaceTemplatePath` (restored afterwards). Two hand-built `.gs` fixtures:

| Acceptance condition | Result |
|---|---|
| mesh round-trips through `.gs` | positions bit-identical (unit test above) |
| legacy `.gs` with no mesh section loads unchanged | every prior driver run in TASK-036/037/040 loads a mesh-less file; the section is `contains()`-guarded |
| meshes included in zoom-extents | `ZE` on the 2M fixture reported **span 2000 × 2000** — the grid's exact extent |
| 2M triangles, no index overflow or silent truncation | **"Loaded 1 mesh(es): 2000000 triangles, 1 part(s)"** — exact |
| parts keep names and colours | the 4-box fixture draws a teal "tower" and a magenta "outbuilding" |
| erase undoable in one step | implemented on the existing single-snapshot erase path; **not yet driven in the app** (§7) |

The 2M-triangle case also settles a **REQ-064** condition that TASK-040 could not verify: "in
Shaded, a curved surface shows a lighting gradient". The undulating terrain shows exactly that —
there were simply no curved surfaces in existence until now.

## 7. Outcome and what is NOT done

- REQ-063 delivered: store, `.gs`, extents, erase, layer visibility, shaded rendering, validation.
- **Follow-ups, one now closed:**
  1. ~~**Mesh rendering re-expands its vertex buffer every frame.**~~ **Done 2026-08-12** — see §9.
  2. **REQ-064's "REQ-100 met in Shaded" is still not formally verified**, and ADR-026 predicted it:
     "the bench needs a mesh case before REQ-064 can claim the budget." §9 measures the mesh path
     directly and it is cheap, but `BENCH` still has no mesh scene, so the *budget* is unclaimed.
     That is REQ-100 work and belongs with `BENCH`, not here.

## 9. Follow-up — the mesh VBO cache (2026-08-12)

The first implementation expanded indices to a non-indexed triangle list and uploaded it **every
frame**: for the 2M-triangle fixture, ~6M vertices × 6 floats ≈ **144 MB per frame**. It drew
correctly, which is exactly why it needed measuring rather than eyeballing.

Meshes are immutable (ADR-026 (c)), so the only reason their buffers ever need rebuilding is the
**view anchor** drifting far enough to cost float precision — never a geometry change. That makes an
uploaded-once indexed draw correct here, where the linework cache additionally has to watch a
revision counter:

- one `MeshGpuEntry` per mesh: VAO + VBO (position + normal) + EBO (indices);
- **one vertex per vertex**, not one per index — 1M vertices (24 MB) instead of 6M (144 MB);
- indices uploaded **once**, since they never depend on the anchor;
- each part drawn with `glDrawElements` at its own index offset — which is what the index array was
  for, and what makes per-part colour cost nothing extra;
- the residual pan absorbed by the MVP, before the view rotation, reusing the drift budget and the
  composition the linework cache and `CameraTests` already pin down;
- entries keyed by **`weak_ptr`**, not a raw pointer: a raw key could be matched by a new mesh
  allocated at a freed address, which would silently draw stale geometry. An expired entry is
  evicted and its GL objects deleted.

**Measured**, 2M triangles, continuous scripted orbit, process CPU over a fixed 5.7 s wall interval:

| | process CPU | of wall |
|---|---|---|
| per-frame upload (the original) | 1.66 s | **29%** |
| VBO cache | 0.58 s | **10%** |
| 2D Wireframe — mesh not drawn at all | 0.69 s | 12% |

The cached path costs **less than not drawing the mesh** (10% vs 12% — the difference is noise).
That is the point: with the geometry resident, orbiting a two-million-triangle model is a matrix
change, and the mesh stops being a per-frame cost at all.
- Erase-and-undo of a mesh is implemented but was not exercised in the app; selection of a mesh by
  clicking is **not implemented** — `SelectedEntity::Type::Mesh` exists and erase honours it, but no
  picking path produces one yet. No acceptance condition requires picking, and ADR-026 (g) keeps
  meshes out of snapping; a mesh currently reaches selection only via code. Recorded rather than
  left to be discovered.

---

COMPLETION REPORT — TASK-041 — 2026-08-12
- Requirements satisfied:  REQ-063 (Acceptance met: yes for the six stated conditions; see §7 for
                           two follow-ups that no acceptance condition covers but that a reader
                           should know about).
- Summary:                 A triangle-mesh entity held as `shared_ptr<const CadMesh>`, with parts,
                           `.gs` persistence, validation, extents, erase, layer visibility, and
                           shaded rendering.
- Tests:                   MeshGeomTests (10) + MeshGsRoundTripTests (4). 65,078 assertions /
                           217 cases, green.
- Verification verdict:    PASS
- Assumptions:             none open.
- Architectural decisions: **one escalated and resolved before any code** — §5's SPEC GAP, recorded
                           as an amendment to architecture §11.5 and in the decision log.
- Dependencies:            none added
- Technical debt noted:    per-frame mesh vertex expansion (§7.1) — removal condition: an indexed
                           draw with a VBO cached on the mesh pointer. Mesh picking not implemented
                           (§7).
- Build:                   reproducible, clean on target platform
- Docs updated:            this task log; architecture §11.5; project.md decision log.

## 8. Progress

- 2026-08-12 — task opened, planned, and **stopped at the undo snapshot** before writing code. Gap
  escalated, decided, recorded; then implemented and verified.
