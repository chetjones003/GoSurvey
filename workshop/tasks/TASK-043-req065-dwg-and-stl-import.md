# TASK-043 — REQ-065 amended: DWG and STL as import routes

- Type:    feature (scope extension of REQ-065)
- Status:  done
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-065**, amended 2026-08-12 (decision log) to accept glTF, STL and DWG through
  one command.
- Architecture: **ADR-026 addendum** (2026-08-12) — GoSurvey converts DWG 3D content itself by
  driving an installed AutoCAD; **ADR-024** supplies the converter-route pattern and its discovery.
- Constraints: REQ-101, REQ-201, the local-storage invariant.

## 2. Why this exists — a recommendation that did not survive contact

ADR-026 chose glTF and named **Navisworks** as the exporter. The user could not open their drawing,
and the reason was simple: **Navisworks is not installed**, and neither is 3ds Max. The ADR had
recommended an exporter without checking whether one existed on the machine, which left the hardest
step of the user's actual goal — "open my Plant 3D drawing and see the model" — outside the product
entirely.

What *is* installed is AutoCAD. Its Plant 3D object enabler will **explode** `AcPp*` custom objects
into plain 3D solids — the one useful thing it will do for a third party without a licence — and
`STLOUT` will tessellate those. That is a complete route from the user's DWG to a mesh using only
software they already have, and it is ADR-024's converter pattern rather than a new mechanism: it
reuses that ADR's `FindDwgConverter` discovery and the existing `RunProcessAndWait`.

## 3. What was built

- `util/modelimport.hpp` — format-neutral `Options` / `Part` / `Result`. One importer's result type
  is a detail; two importers sharing one is an interface. The `gltf::` names remain as aliases so
  nothing that already spoke them had to change.
- `util/stlimport` — binary and ASCII STL. Pure, so it is tested without a window.
- `io/DwgMeshConvert` — writes a LISP + script, runs the converter, reads the STL back.
- `IMPORTMODEL` dispatches on the file's extension; the file dialog offers
  `*.glb;*.gltf;*.stl;*.dwg` in one list. From the user's side a DWG is just another model file.

Three details that would otherwise bite:
- the conversion runs on a **copy**, because it explodes the model and must never touch the user's
  file;
- STL's encoding is detected by the length identity `84 + 50·triangles == filesize`, **not** by the
  `"solid"` prefix — a *binary* STL's 80-byte comment header can begin with those five bytes, and
  real exporters write exactly that. Sniffing the prefix misreads real files, and the misread is
  silent: zero triangles reads as an empty model rather than an error;
- an ODA File Converter is **not** sufficient (it translates DWG→DXF but cannot tessellate solids)
  and is refused by name rather than with a generic failure.

## 4. A precision test that was passing for the wrong reason

Writing the STL precision test made an existing glTF test fail its equivalent — and the STL one was
right.

`float32` near a 6,543,210 ft easting has a spacing of **0.5 ft**, which is **50× REQ-101's
0.01 ft**. A sub-foot feature therefore cannot survive being stored at a raw state-plane coordinate,
no matter how carefully the import arithmetic is done in double. GoSurvey's answer is the
local-storage invariant: geometry is stored **local**, and the large number lives once, in double,
in `worldDocumentOrigin`, where it never reaches a vertex.

The glTF test had asserted a 1-unit span at that magnitude and **passed** — but only because 1.0 is
exactly two ULPs there. It was asserting luck. Both tests now assert the real property at
drawing-local coordinates, and a new test in `StlImportTests` pins the **limit** explicitly, so a
future reader who passes a state-plane value as an insertion point finds a test explaining why it
cannot work rather than concluding the importer is lossy.

## 5. Verification

**Tests:** 65,161 → **65,204 assertions**, 236 → **244 cases**, green. `tests/StlImportTests.cpp`
(8 cases): binary and ASCII of the same triangle agree; a binary STL whose header begins `"solid"`
is not misread; facet normals normalised; scale/insertion at local coordinates within REQ-101; the
state-plane limit pinned; truncated binary, empty, and mid-facet ASCII each refused with a reason;
5,000 triangles read exactly, which is where a stride off-by-one would drift visibly.

**In the running app, on the user's real drawing** — `IMPORTMODEL` pointed straight at
`ENTERPRISE PIPING.dwg`, no pre-conversion:

```
Converting the DWG's 3D solids via AutoCAD … — this can take a few minutes on a large model.
Imported ENTERPRISE PIPING.dwg — 5834 triangles, 1 parts, scale 0.0833333.
Not imported (geometry only): per-object colours and names (STL carries neither)
Zoom extents applied — span 38.3 …
```

The model renders shaded. The 38.3 ft span matches the independently measured extent of the
converted geometry.

## 6. Outcome and what is NOT solved

- A DWG can be opened and viewed with no external step. `tools/Convert-PlantDwgToGlb.ps1` remains as
  a standalone equivalent for scripted/batch use.
- **The model is still grey.** STL carries no colour or names, so a DWG import is one part where a
  glTF import keeps its structure. This is stated at import, not discovered. Recovering colour by
  grouping the exploded solids per colour is the obvious next step and was deliberately not
  attempted here.
- **`ZOOM EXTENTS` frames plan extents**, so an orbited view leaves the model small and off-centre.
  Not an import defect, and not covered by any requirement — but it is what makes an imported model
  look lost on arrival, so it is the first thing worth fixing.
- Runtime dependency: importing a DWG now needs an installed AutoCAD **at import time** — software
  we do not ship. Recorded in the ADR addendum's consequences.

---

COMPLETION REPORT — TASK-043 — 2026-08-12
- Requirements satisfied:  REQ-065 as amended (Acceptance met: yes, including the five conditions
                           added for the STL and DWG routes).
- Summary:                 One import command for glTF, STL and DWG, with DWG converted in-process
                           by an installed AutoCAD.
- Tests:                   tests/StlImportTests.cpp (8). 65,204 assertions / 244 cases, green.
- Verification verdict:    PASS
- Assumptions:             none open.
- Architectural decisions: one — DWG conversion as an import route. Recorded as an **ADR-026
                           addendum** and in the decision log. **Recorded late**: the code was
                           written first and the spec caught up before commit, which is the wrong
                           order and is noted here rather than tidied away.
- Dependencies:            no new build dependency; a new RUNTIME dependency on an installed
                           AutoCAD for the DWG route only, reported when absent.
- Technical debt noted:    per-object colour lost on the DWG route (§6); zoom-extents-under-orbit
                           (§6) — neither introduced by this task.
- Build:                   reproducible, clean on target platform
- Docs updated:            spec/requirements.md (REQ-065 statement + 5 acceptance conditions),
                           spec/project.md (decision log), spec/architecture.md (ADR-026 addendum),
                           this task log.
