# TASK-228 — REQ-101 ±0.002 ft: widen coordinate storage `float` → `double`

- Type:    refactor (spec-authorized architecture migration)
- Status:  in progress — PR 1 (spec + ADR + plan) done; phases 2–N not started
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #394

## 1. Authority

- REQ-101 — tightened to ±0.002 ft, D-2026-09-08-i (revision note carries the per-subsystem phasing).
- ADR-054 — coordinate storage widens to `double`; the `float` narrowing moves to the GPU-upload
  boundary. This is the recorded authority for the whole migration.
- ADR-025 (a)/(b) — the flat interleaved-XYZ store layout being widened (amended by ADR-054).
- Architecture §11.8 invariant #8 — unchanged (governs layout, not scalar width).
- REQ-200 — deterministic build; each phase must keep it.
- REQ-201 — over-large-magnitude refusal and finiteness guards unchanged.

## 2. Problem

REQ-101 now promises ±0.002 ft stored accuracy. Geometry is stored in flat `std::vector<float>`
arrays (`userLinesFlat` s6, `userPolylineVerts` s3, `userCirclesCxCyZR` s4,
`CadFilledRegion::vertsXyz` s3) plus scalar fields (`CadArc::cx/cy/z`, `CadEllipse`,
`CadAnnotation::insX/insY/insZ`, block inserts, dimension points, feature-line / surface vertices,
paper geometry). A `float` resolves ~0.008 ft at the 100,000 ft rebase ceiling, so ±0.002 ft is not
representable in these stores. The stores must hold `double`; the GPU vertex format stays `float`
(narrowed once at upload, after the document origin is subtracted).

## 3. Scope — phased, one PR per phase, each gated on a full `./dev/build` + `./dev/test`

Until a phase lands, its subsystem keeps `float` and its existing ±0.01 ft assertions.

- **Phase A — core entity stores + the three copies.** Widen the four flat stores to
  `std::vector<double>` and every entity scalar coordinate field to `double`, across the live
  `AppCommandState`, the undo `DrawingGeometrySnapshot`, and the per-tab struct. Fix every
  compile break at the boundaries (command math, `docinvariants`). GPU upload path narrows
  `double`→`float` at buffer assembly (ADR-054 (b)). `CadCoordinateFrame` / rebase logic stays;
  drop REQ-101's "one-time establishment" enforcement per ADR-054 (c).
- **Phase B — serialization.** DWG-trailer coordinate records → 8-byte `double` + trailer
  format-version bump + legacy-load path (old `float` trailers load as-is). DXF importer/exporter
  stop narrowing through `float` (format already ASCII decimal — no on-disk change). Confirm
  LibreDWG path is already `double`.
- **Phase C — snap / preview / pick read-back.** Every site that reads a coordinate back out of a
  store for snapping, rubber-band preview, or pick resolution takes the `double` value. Confirm
  REQ-101's bit-identical-snap property now delivers the full-precision value.
- **Phase D — the GPU-upload narrowing point.** Audit that `float` appears on the geometry path in
  exactly one place (buffer assembly) and nowhere upstream; add a `docinvariants` / review check.
- **Phase E — test-assertion sweep.** Every `0.01` literal and named constant that represents the
  REQ-101 guarantee → `0.002`, audited one at a time:
  - `kReq101` (`src/viewport/CadSnap.cpp`) — is the guarantee, change.
  - `kTinPlanEpsilon` (`src/util/tinbuild.hpp`) — "two shots are the same site" de-dup threshold;
    confirm whether it should track REQ-101 or is an independent domain choice **before** changing.
  - `kSolidChordToleranceFt` (`src/util/cadsolid.hpp`) — tessellation chord tolerance; reconcile
    with #384's isoline work (issue #394 AC item 5) before changing.
  - `kTol = 0.01f` (`src/commands/CadCommands.cpp` ~15751, endpoint-coincidence) — is the guarantee.
  - per-assertion `0.01` literals across the Catch2 suite (arc/curve intersections, DXF/DWG
    round-trips, survey-point import, grading, snapping) — each checked for "is this the REQ-101
    tolerance or a coincidental use" before edit. Assertions using `0.01` for unrelated reasons are
    left alone (issue #394 AC item 3).

OUT:
- Widening any render / tessellation / mesh buffer or the GL vertex format (ADR-054 (b) — they stay
  `float`).
- A user-facing units/precision mode (REQ-101 is a fixed internal guarantee).
- Changing `kLargeCoordinateRebaseThreshold` or `kMaxEstablishableOriginMagnitude` (unchanged).

## 4. Test approach

- Per phase: full `ctest` green before the PR merges; the phase's own subsystem gets at least one
  assertion re-pinned to ±0.002 ft and proven red-before / green-after where practical.
- Phase A: extend `headless.regression-req101-origin-at-entry` — a coordinate typed at 2e6 with the
  origin **not** pre-established is now stored within ±0.002 ft (the `double` store carries it).
- Phase B: DWG trailer round-trip of a coordinate at state-plane magnitude within ±0.002 ft; a
  legacy `float` trailer still loads (within the old ±0.01 ft, asserted as such).
- Phase E: the sweep's own diff is the test — reviewer confirms each changed site is REQ-101 and
  each untouched `0.01` is not.

## 5. Verification

- `./dev/build` clean and `./dev/test` 100% at the end of **every** phase.
- `architecture-review` against ADR-054 each phase (esp. Phase D — the one-narrowing-point rule).
- `dependency-audit` — no new dependency (the migration is in-tree type widening).
- Acceptance criteria (issue #394): tracked across phases; fully met when this task closes.

## 6. Notes / tech debt

- PR 1 changes no code — it is `spec/requirements.md` (REQ-101 + traceability row),
  `spec/architecture.md` (ADR-054 + five cross-reference amendments), `spec/project.md` (decision
  log D-2026-09-08-i), `spec/roadmap.md` (Next entry), and this task file.
- Memory note "flat arrays were ALREADY XYZ (lines stride 6, polylines 3); verify strides at an
  append site, never by grep; rename when widening" — the strides do **not** change here (only the
  scalar type), so no rename is needed; the hazard that guidance addresses (a silent stride misread)
  does not apply to a `float`→`double` widening, which surfaces as a narrowing-conversion at each
  unconverted boundary.
- ADR-025's rejected `std::vector<Vec3>` alternative stays rejected for the same reason (largest
  diff); flat-`double` keeps strides and layout identical.
