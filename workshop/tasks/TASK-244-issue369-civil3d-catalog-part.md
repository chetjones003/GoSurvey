# TASK-244 — Civil 3D parts-catalog blocks: name the skip, keep the 2D content

- Type:    fix + spec acknowledgment (amends an accepted requirement)
- Status:  review
- Opened:  2026-09-10
- Owner:   Workshop
- GitHub:  #369

## 1. Authority

- **REQ-320 / ADR-051** — ACIS `3DSOLID` import and its refuse-never-approximate posture.
  **Amended here** (D-2026-09-10-b, ADR-051 addendum).
- **ADR-026** — vendor custom objects (Plant 3D `AcPp*`, and now Civil 3D `AECC_*` parts-catalog
  parts) are unreachable by any third-party reader; interchange only.
- **REQ-201** — refuse with a stated reason; never a silent or ambiguous result.
- Constraint CON-06 — smallest correct change.

## 2. Problem

`BLOCKIMPORT` of `CS150_4in_WELD_NECK_FLANGE.dwg` logged `skipped "3DSOLID(empty)" × 2`. A
LibreDWG diagnostic against the file confirmed:

- the single `3DSOLID` has `acis_empty=1`, `version=0`, `num_blocks=0`, **no ACDS handle** —
  there is no ACIS data anywhere in the file, inline or via the newer ACDS section (#366);
- the class table is full of Civil 3D Pressure Pipes Network classes — `AECC_FITTING_STYLE`,
  `AECC_PRESSURE_PIPE_STYLE`, `AECC_DISP_REP_FITTING`, …

So the flange's shape is **not in the file**. Civil 3D regenerates it at open time from its
proprietary Parts Catalog engine, the same structural dead-end as Plant 3D's `AcPp*` objects
(ADR-026). The `(empty)` message reads like a decode bug when it is an unfixable-by-design limit.

## 3. SPEC GAP and its resolution

The issue raised a genuine SPEC GAP (CLAUDE.md §5): when a block's only 3D geometry is one of
these unreachable catalog parts, does `BLOCKIMPORT` still import the block's 2D / annotation
content, or refuse the whole block? This is a product decision, not an engineering one.

Put to the user, answered: **keep the 2D content, name the missing solid.** Recorded as
**D-2026-09-10-b**. Rationale: matches REQ-320 / ADR-051 (d) (an unsupported ACIS solid is
refused by name while the rest of the file imports) and ADR-026's lower-fidelity-plus-explicit-
refusal posture. Refusing the block outright would discard usable centerlines and labels and be
stricter than the importer is anywhere else.

## 4. Implementation

- `libredwgcad_detail::DwgHasCivil3dCatalogClasses(const _dwg_struct*)` — scans
  `Dwg_Data::dwg_class` for any `dxfname` beginning `AECC_`. Pure, null-safe, unit-testable.
  Declared in `LibreDwgCad.hpp` beside `DecodeDwgString` / `ColorToStorage`.
- `ImportAcisSolid` takes the `Dwg_Data*` (already held by its one caller `ImportObject`) and,
  in the existing `acis_empty` branch, logs
  `3DSOLID(Civil3D parts-catalog part, no portable geometry)` when the class signature is
  present, else the unchanged `3DSOLID(empty)`.
- No change to what imports: the empty `3DSOLID` was already skipped; `BLOCKIMPORT` already
  keeps the block's 2D geometry. Only the message is more accurate.

Detection is conservative — **both** signs required (a payload-less `3DSOLID` *and* an `AECC_*`
class) — so an ordinary drawing with a genuinely empty legacy `3DSOLID` still logs `(empty)`.

## 5. Tests

`LibreDwgCadTests` — `Civil3D parts-catalog class signature is detected from the class table`
`[dwg][libredwg][issue369]`: a stack `Dwg_Data` with `AECC_PRESSURE_PIPE` / `AECC_FITTING_STYLE`
entries is flagged; a plain table (`LWPOLYLINE` etc.) is not; an empty table and a null pointer
are both safe.

An end-to-end transcript against a real catalog `.dwg` is not added — LibreDWG 0.13.3's own
encoder cannot write a `3DSOLID` with a populated class table for a fixture, and no such sample
is in the tree. The signature check is the whole of the new logic and is covered directly.

## 6. Assumptions

- `dxfname` (ASCII/UTF-8, filled from `dxfname_u` for R2007+ by LibreDWG) is the reliable field
  for the `AECC_` prefix; `appname` for these classes is a longer vendor string. Matches the
  class names quoted in the issue's diagnostic.

## 7. Verification result

- **build-project** — clean release build, MSVC/Ninja, no new warnings.
- **architecture-review** — one pure helper + one existing message branch; no new layer,
  dependency, abstraction or global state. Detection lives in IO where the class table is.
- **code-review** — the helper sits beside `DecodeDwgString`; the message selection is one
  ternary in the branch that already handled this case.
- **dependency-audit** — none added.
- **performance-review** — one class-table scan (tens of entries) per empty `3DSOLID`, on an
  import the user invokes by hand.
- **testing** — full suite **1444/1444 green**; 1 new unit case.

COMPLETION REPORT — TASK-244 — 2026-09-10
- Requirements satisfied:  REQ-320 as amended (D-2026-09-10-b); GitHub #369
- Summary:                 a Civil 3D parts-catalog block names its missing solid and keeps its 2D content
- Tests:                   1 new unit case; 1444/1444
- Verification verdict:    PASS
- Assumptions:             `dxfname` carries the `AECC_` prefix (§6)
- Architectural decisions: D-2026-09-10-b, ADR-051 addendum
- Dependencies:            none added
- Technical debt noted:    no end-to-end transcript (no writable fixture; §5)
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-320 statement + revisions, traceability row, ADR-051 addendum,
                           D-2026-09-10-b, this task log
