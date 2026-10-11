# TASK-042 — REQ-065: glTF / GLB model import

- Type:    feature
- Status:  done
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-065** (accepted 2026-08-12). Step 3 — the last — of the M-Models milestone.
- Architecture: **ADR-026 (b)** glTF is the interchange format; **(d)** the parser is written in-tree
  as a pure module rather than vendored.
- Constraints: REQ-101 (±0.01 ft), REQ-201 (no silent failures), the local-storage invariant.
- Owning subsystem: IO/util (parser), Commands (the import), Platform (file dialog).

## 2. Why in-tree, and when to reconsider

ADR-026 (d) chose an in-tree parser because the subset REQ-065 needs — POSITION, NORMAL, indices,
node transforms, names, `baseColorFactor` — is small against a published spec, while a library
carries texture, animation, skin, sparse-accessor and extension handling the requirement excludes.
The ADR recorded the trigger to revisit: **over ~600 lines, or a real file needing sparse accessors
or Draco → vendor `cgltf`.** The parser came in at ~470 lines including comments, so the trigger has
not fired. It is restated at the top of `gltfimport.hpp` so the next person meets it before adding to
the file rather than after.

## 3. The two things most likely to be wrong, and what was done about them

```
COORDINATE SYSTEM. glTF is Y-up; a survey drawing is Z-up. The mapping is
(x, y, z)_glTF → (x, −z, y)_CAD, and it is the single most consequential line in the importer:
get it wrong and every model arrives lying on its side, which reads as a modelling error rather
than an importer bug. It is a named exported function, `GltfYUpToCadZUp`, so it is testable on its
own rather than buried inside the vertex loop — and there is a test that a model 10 units "tall"
in glTF is 10 units tall in ELEVATION.
```

```
PRECISION. Unit scale and insertion point are applied in DOUBLE, before narrowing to float. At a
6-million-foot easting, float resolves ~0.5 ft, so `float(v) + float(offset)` loses REQ-101
entirely while still looking plausible. The test places a 1-unit triangle at a state-plane
insertion point and asserts the SPAN between two vertices is still 1.0 within REQ-101.
```

## 4. Robustness stance

A malformed file is refused with a **specific** reason and leaves the drawing untouched. That is two
separate mechanisms, both deliberate:
- the parser builds everything into a local `ImportResult` and sets `ok` only at the very end, so
  there is no half-built state to commit;
- the command pushes its undo snapshot **after** the parse succeeds, so a failed import is not even
  an undo step.

Every bound is checked before it is read — GLB chunk lengths, buffer lengths against declared
`byteLength`, accessor extents against their bufferView, and indices against the primitive's vertex
count. A truncated GLB is the most common broken file, and reading past the end of one is a crash
rather than a diagnosis. The node walk is **iterative with a visit set**: a cyclic hierarchy is a
stack overflow under recursion, and a crash is not an error message. Imported geometry is finally
run through the same `meshgeom::ValidateMesh` the `.gs` loader uses, so an importer bug cannot put
geometry on the GPU that a loaded file would have been refused for.

## 5. Verification

**Tests:** 65,078 → **65,161 assertions**, 217 → **236 cases**, green. `tests/GltfImportTests.cpp`
(19 cases) builds every fixture **byte-by-byte in memory** — a checked-in `.glb` would test the same
code and explain none of it. Covered:

- Y-up → Z-up in isolation, and end-to-end (height arrives in elevation);
- triangle count and bounding box after a 1/12 (inch → foot) unit scale;
- **state-plane precision** — span preserved within REQ-101 at a 6.5M-foot easting;
- a **doubly-nested** node chain against hand-computed coordinates (REQ-065's named condition);
- `matrix` read as **column-major** — reading it row-major transposes the rotation and misplaces the
  translation, which looks like "the model is scattered";
- names and `baseColorFactor` survive; separate nodes become separate parts;
- textures/animations/cameras imported-around and **reported**;
- rejection with a reason for: truncated GLB, non-glTF bytes, out-of-range index, **cyclic
  hierarchy**, no triangle geometry, wrong asset version — each asserting nothing partial survives;
- supplied normals rotated into CAD space; missing normals computed rather than left black;
- `uint32` indices as well as `uint16` — real exports of large models use `uint32`, and a reader that
  assumed 16-bit would produce garbage triangles rather than fail;
- point/line primitives skipped and reported, and a file of *only* lines rejected rather than
  imported empty.

**In the running app**, with a real GLB authored for the purpose — three named nodes (BASE, COLUMN,
BEAM) with distinct `baseColorFactor`s, non-identity scale/translation/rotation, plus an
`animations` and a `textures` array to exercise the skip report:

- the model draws shaded, teal and magenta per their material factors — **per-part colour survives**;
- the COLUMN rises in **+Z** from the BASE — Y-up → Z-up confirmed visually as well as by test;
- `ZE` reported **span 12 × 8**, which is exactly what BASE's scale `[6, 0.6, 4]` on a unit box
  predicts — the node transform and the axis mapping verified by a number, not an impression;
- the log states **"Not imported (geometry only): animations …"**.

## 6. Outcome

- REQ-065 delivered; **the M-Models milestone is complete** (REQ-063, REQ-064, REQ-065).
- `IMPORTMODEL` (aliases `GLTF`, `IMPORT3D`): bare form opens a file browser and states the scale
  and insertion it used; `IMPORTMODEL "<path>" <scale> <x> <y> <z>` places it directly, which is
  also what makes the import scriptable for verification.
- **Not done, and not required by any acceptance condition:** a modal import dialog with unit
  presets (the command states its defaults instead); `.gltf` with external `.bin` is implemented but
  was exercised only by data-URI and GLB fixtures; and the non-uniform-scale normal caveat is
  recorded at `TransformDirection` — the 3×3 is applied and renormalised rather than the inverse
  transpose, which is exact for rotation and uniform scale.

---

COMPLETION REPORT — TASK-042 — 2026-08-12
- Requirements satisfied:  REQ-065 (Acceptance met: yes — all six conditions, see §5).
- Summary:                 An in-tree glTF 2.0 / GLB reader producing REQ-063 meshes, with unit
                           scale and insertion applied in double, Y-up → Z-up conversion, per-node
                           parts and base colours, and specific refusal of malformed files.
- Tests:                   tests/GltfImportTests.cpp, 19 cases. 65,161 assertions / 236 cases, green.
- Verification verdict:    PASS
- Assumptions:             none open. The non-uniform-scale normal approximation is documented at
                           the site rather than assumed away.
- Architectural decisions: none made by Workshop — all taken in ADR-026. The vendor-a-library
                           trigger it recorded was checked and has not fired (~470 lines).
- Dependencies:            none added (nlohmann/json was already in the build).
- Technical debt noted:    none new.
- Build:                   reproducible, clean on target platform
- Docs updated:            this task log.
