# TASK-252 — Issue #475 Increment 3: 3D orientation + align-to-face + rubber previews

**Requirement authority:** GitHub issue #475, REQ-107 (D-2026-08-29-i preview parity), plan D4

**Files/subsystems affected:**
- `src/util/cadblock.hpp` — `CadBlockSetLocalZAxis`, `CadBlockRotDegToRad`
- `src/commands/CadBlocks.{hpp,cpp}` — rotX/rotY dialog, align-face pick, 3D scale, ghost rubber
- `src/commands/CadCommands.hpp` — `WaitAlignFace`, rotX/Y state
- `src/viewport/CadRubberPreview.cpp` — solid wireframe ghost + 3D drag indicator
- `src/viewport/ViewportPickPolicy.hpp` — `InsertBlockAlignFacePick`
- `src/ui/CadUi.cpp`, `CadUi_InsertBlock.cpp` — face pick route, dialog fields
- `tests/CadBlockTests.cpp`, `tests/CadBlockImportTests.cpp`

**Implementation approach:**
1. Wire `rotX`/`rotY` through `InsertDialogXform` / preview / commit.
2. `InsertLiveScaleDist` uses 3D distance from insertion point.
3. `WaitAlignFace` phase: ray pick planar face → `CadBlockSetLocalZAxis`.
4. `AppendInsertBlockGhostRubber` tessellates block solid edges into rubber lines.
5. `CadRubberPreview` calls ghost helper for all INSERT pick phases.

**Test approach:**
- `CadBlockSetLocalZAxis` maps local +Z to target normal.
- Dialog rotX=90° commits on place.
- 3D scale preview distance along Z.
- Solid block ghost rubber includes elevated Z segments.

**Acceptance:**
- INSERT dialog accepts rot X/Y; align-to-face sets orientation from a planar face pick.
- Live rubber-band preview shows 2D linework + B-rep wireframe at the pending transform during insertion, scale, rotation, and align-to-face picks.
- Preview transform matches commit within REQ-101 tolerance.
