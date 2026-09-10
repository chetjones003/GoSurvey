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
- `ImportSatFileToScratch` — reads the file, runs `acissat::ImportSatSolid`, pushes the solid into
  the scratch state and sets `drawingInsUnits` from `mmPerUnit`.
- `ImportCadBlocksFromPathImpl` — `.sat` branch; the shared block-capture path wraps it into a
  definition named after the file, and the `.sat` case *also* copies the solid into `dest.cadSolids`
  so it is visible immediately.
- `BrowseOpenFileBlockUtf8` filter gains `*.sat`; BLOCKIMPORT help text updated.

## 4. Out of scope (tracked in #473)

**Placing / rendering a block-stored solid.** `CadBlockContent::solids` is captured and survives
save / WBLOCK, but no code instances it on INSERT/EXPLODE or draws it from a block reference. This
is a pre-existing REQ-320 (#299) gap affecting every block-stored solid (ACIS-from-DWG too). The
direct placement on `.sat` import is the interim path. Also unchanged: #300 (free-form surfaces),
#301 (SAB binary).

## 5. Tests

- `AcisSatParserTests [issue473]` — the real flange file → `r.ok`, 16 faces (4 plane + 12
  cone/cyl), `Validate == Ok`, positive volume, `mmPerUnit == 25.4`, bounds near (4998, 4998)
  proving the `body` transform was applied.
- `CadBlockImportTests [issue473]` — `.sat` through `ImportCadBlocksFromPath` → a block definition
  named after the file with a 16-face valid solid, `units == "inches"`, no block reference placed,
  and the solid also loose in `st.cadSolids`. A malformed `.sat` → refused, `blockDefs` empty, a
  `BLOCKIMPORT` message logged.
- Full suite green; the 14 existing `[acissat]` hand-authored fixtures unchanged (detected as the
  simplified schema).

## 6. Verification

- `build-project` — full build clean.
- `testing` — `ctest` full pass.
- `architecture-review` — no new dependency; `brep` unchanged; the parser stays graphics-free.
- `code-review` — pending.
