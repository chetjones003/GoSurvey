# TASK-246 — Real ACIS SAT schema + standalone `.sat` import

- Type:    feature (amends REQ-320 / ADR-051)
- Status:  review
- Opened:  2026-09-10
- Owner:   Workshop
- GitHub:  #473

## 1. Authority

- **REQ-320 / ADR-051** — ACIS `3DSOLID` import. ADR-051 (a) built `AcisSatParser` against a
  simplified hand-authored schema because no real corpus existed. **Amended here**
  (D-2026-09-10-c, ADR-051 addendum): the parser reads the real ACIS/ASM schema and `.sat`
  becomes an importable file type.
- **ADR-052** — `Face::paramLoops` general trim loops. Reused for multi-hole planar faces.
- **REQ-332 / ADR-046 (m)** — `brep::Rotate` / `Scale`. Used to apply the `body` transform.
- **REQ-201** — refuse with a stated reason (reflection/shear transform, unsupported surface).
- **REQ-300** — no vendored ACIS/geometry kernel (unchanged; nothing new vendored).

## 2. Trigger

The user ran `ACISOUT` in Civil 3D on a 4" weld-neck flange and provided the `.sat`
(`samples/CJ_4in_WELD_NECK_FLANGE.sat`) — the first real ACIS corpus the project has. It did not
parse: real ACIS/ASM records differ from the simplified schema in framing (a `$attrib -1 $pattern`
prefix, edge parameter ranges, `I` markers, `@n` strings, `color-adesk-attrib` records, a `body`
`transform`, cone/cylinder walls as two rim loops). The user asked for `.sat` in the Import Block
window and chose "block definition" as the import target.

## 3. Implementation

### Parser (`src/util/AcisSatParser.{cpp,hpp}`)
- `NormalizeRealAcisSchema(std::vector<SatRecord>&)` — detects a real-format stream (`body` field 1
  is the bare id `-1`, not `$lump`) and rewrites every record's fields onto the simplified layout
  the rest of the file already indexes. Unknown records (`color-adesk-attrib`, `transform`) left
  intact so `$n` numbering holds.
- `HeaderMmPerUnit` + `ImportResult::mmPerUnit` — the ACIS header's third line, the only unit hint a
  standalone `.sat` carries.
- `RotationMatrixToAxisAngle` + `Importer::ApplyBodyTransform` — decompose the `transform`'s 3×3
  (transposed: ACIS is row-vector) into axis/angle, apply `Scale` → `Rotate` → `Translate` in ACIS
  order. Reflection / shear refused.
- `BuildPlaneGeneralTrim` — a planar face with ≥2 loops projects every loop into the plane frame,
  takes the largest-area loop as the outer boundary, winds it CCW / holes CW, and stores
  `Face::paramLoops` (ADR-052). The plane counterpart of `BuildConeGeneralTrim`, which also gained
  an outer-loop winding normalisation.
- Cone branch: two single-edge full-circle rim loops are merged into one two-edge loop for
  `BuildConeFace`'s full-revolve path.

### `.sat` file type (`src/commands/CadBlocks.cpp`, `src/platform/WinFileDialogs.cpp`, `CadCommands.cpp`)
- `ImportSatFileToScratch` — reads the file, runs `acissat::ImportSatSolid`, **re-bases the solid
  onto the origin** (`ComputeBounds` → `Translate` by `{-Xcentre, -Ycentre, -Zmin}`; a `.sat`
  carries its absolute position from the source drawing, ~4999 units for the fixture), pushes it
  into the scratch state, leaves units `unitless`.
- `ImportCadBlocksFromPathImpl` — `.sat` branch: the shared block-capture path keeps a definition
  named after the file, *and* the re-based solid is copied straight into `dest.cadSolids` so it is
  a visible, MOVE-able drawing solid.
- `PlaceInsertImpl` — a block whose `content.solids` is non-empty logs that INSERT cannot place its
  solid and places only the block's 2D content.
- `BrowseOpenFileBlockUtf8` filter gains `*.sat`; BLOCKIMPORT help text updated.

## 4. What was tried and abandoned

The first cut re-centred the solid on its bbox centre and left it in a block definition only —
INSERT was expected to place it. The user's first test put the flange ~2500 ft away, because
INSERT never draws a block-stored solid and the flange still had Civil 3D's world position.

The second cut added `InstantiateBlockSolids` — INSERT materialising the block's solids
transformed by the insert frame. The user's second test: the flange came in **12× oversized**
(block units defaulted to feet against an inch drawing → `CadBlockUnitsScale` = 12) and **still
mislocated**, because `SubmitInsertBlockPick` takes only `(wx, wy)` — INSERT has no Z pick, so a
3D snap to a pipe-end face collapsed to `(x, y, 0)`.

**Conclusion: INSERT is a 2D command and a 3D solid needs a 3D placement.** So the solid is dropped
straight into the drawing on the origin and positioned with `MOVE` (3D- and osnap-aware). A
block-reference path for a solid — instancing on INSERT, or a real 3D INSERT — is future work in
#473. Also unchanged: #300 (free-form surfaces), #301 (SAB binary).

## 5. Tests

- `AcisSatParserTests [issue473]` — the real flange file → `r.ok`, 16 faces (4 plane + 12
  cone/cyl), `Validate == Ok`, positive volume, `mmPerUnit == 25.4`, bounds near (4998, 4998)
  proving the `body` transform was applied.
- `CadBlockImportTests [issue473]` — `.sat` through `ImportCadBlocksFromPath` → one drawing solid
  (16 faces, valid) re-based onto the origin (`X/Y` centred, `Zmin == 0`), a block definition kept,
  no `CadBlockRef`. A malformed `.sat` → refused, `blockDefs` empty, a `BLOCKIMPORT` message.
- `headless.issue473-sat-blockimport` — BLOCKIMPORT → SOLIDS 1 on the origin; box-select + `MOVE`
  0,0 → 10,20 relocates it; UNDO restores it.
- Full suite green; the 14 existing `[acissat]` hand-authored fixtures unchanged (detected as the
  simplified schema).

## 6. Verification

- `build-project` — full build clean.
- `testing` — `ctest` full pass.
- `architecture-review` — no new dependency; `brep` unchanged; the parser stays graphics-free.
- `code-review` — pending.
