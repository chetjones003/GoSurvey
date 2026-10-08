# TASK-734 — Import Plant 3D / AcDs ASM `3DSOLID` bodies (REQ-320 increment 2)

- Type:    feature
- Status:  complete (verification PASS, 2026-10-08)
- Opened:  2026-10-07
- Owner:   workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         GOAL-NN (DWG interoperability / 3D reference geometry)
- Requirements: REQ-320 increment 2 (D-2026-10-07-b; amended D-2026-10-08-a torus/sphere scope,
  D-2026-10-08-b Plant 3D placement), REQ-300, REQ-201, REQ-101, ADR-051 (+ 2026-10-08 amendments),
  ADR-026 (narrow placement exception)
- Constraints:  CON-NN as applicable; no commercial ACIS kernel
- Acceptance:
  - Opening `samples/example-piping-system.dwg` imports at least one `brep::Solid`, ZOOM EXTENTS
    frames solid geometry, log not solely `3DSOLID(empty)` × N for that file.
  - Supported ASM/SAB bodies use same analytic import rules as SAT; unsupported bodies refused by name.
  - Existing SAT / `.sat` tests stay green.
  - No third-party ACIS/geometry-kernel dependency.
  - (D-2026-10-08-a) every body imported matches AutoCAD's own volume and extents.
  - (D-2026-10-08-b) Plant pipes, fittings and gaskets are placed where AutoCAD's exploded geometry
    puts them; unplaced Plant parts are named in the log.
- Owning subsystem: IO (`LibreDwgCad.cpp`, vendored LibreDWG), Domain (`AcisSatParser`, `brep`)

## 2. Scope
- In scope:
  - AcDs `_data_` segment decode and handle→blob association (ASM + ACIS binary headers).
  - SAB/ASM → SAT conversion feeding `AcisSatParser`.
  - `ImportAcisSolid` path when `acis_empty` but post-decode `acis_data` is populated.
  - Regression test using `samples/example-piping-system.dwg`.
  - Torus and sphere faces (D-2026-10-08-a).
  - Plant 3D placement fields of `ACPPPIPEINLINEASSET` / `ACPPCONNECTOR` / `ACPPPIPE` (D-2026-10-08-b).
- Out of scope:
  - `ACPP*` parametric engine data (ADR-026 still holds beyond placement); structural members.
  - Mesh fallback for unparseable solids.
  - True-ellipse edges, spline/blend surfaces (#300).
- Smallest change: fix LibreDWG attach for `ASM BinaryFile`, convert to SAT, reuse parser.

## 3. Architectural boundary check
- [x] Yes → recorded as D-2026-10-07-b + ADR-051 addendum (2026-10-07) before implementation.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | V1 outcome: B-rep vs mesh? | 2026-10-07 | B-rep where parseable; named refuse otherwise |
| Q2 | Fixture name / location? | 2026-10-07 | `samples/example-piping-system.dwg` |
| Q3 | SPEC GAP: spec deferred torus/sphere but 18 elbows depend on torus — scope? | 2026-10-08 | Accept torus + sphere (D-2026-10-08-a) |
| Q4 | Parts sit at block-local coords; placement lives in Plant objects (ADR-026) — interim? | 2026-10-08 | "build placement now" (D-2026-10-08-b) |

## 5. Assumptions
```
ASSUMPTION-1: Most visible geometry in the fixture is reachable via AcDs ASM on 3DSOLID, not only ACPP proxies.
- Because: 89 has_ds_data solids; 37 ASM BinaryFile markers in file; 0 model-space ACPP entities decoded.
- Risk if wrong: import still empty until ACPP reverse-engineering track opens.
- Validate by: Phase A spike attaches ≥1 blob and produces ≥1 solid on fixture import.
```

## 6. Plan
- Approach:
  - **Phase A (LibreDWG):** implement `_data_` in `acds.spec`; extend DECODER attach for `ASM BinaryFile`;
    map blobs via datidx/search to `acis_sab_hdl` targets; update `VENDORED.md`.
  - **Phase B (convert):** extend `dwg_convert_SAB_to_SAT1` ASM header path; unit-test one extracted blob.
  - **Phase C (GoSurvey):** wire `ImportAcisSolid`; add `LibreDwgCadTests` case for fixture; verify extents.
- Files/functions to touch:
  - `third_party/libredwg/src/acds.spec`, possibly `out_dxf.c` (`dwg_convert_SAB_to_SAT1`)
  - `src/io/LibreDwgCad.cpp` — `ImportAcisSolid`
  - `tests/LibreDwgCadTests.cpp`
  - `samples/example-piping-system.dwg` (fixture, committed)
- Test approach:
  - Happy: fixture import → `st.cadSolids.size() >= 1`, extents non-degenerate.
  - Failure: hand-authored corrupt SAB still refused by name, no crash.
- Steps:
  - [x] Verification APPROVE on this plan (post-SPEC merge)
  - [x] Phase A LibreDWG vendor patch + VENDORED.md entry
  - [x] Phase B ASM→SAT conversion
  - [x] Phase C Import wiring + fixture test
  - [x] Full ctest `[dwg][libredwg]` + `[acis]`
  - [x] Phase D (D-2026-10-08-b): Plant 3D placement + AutoCAD answer keys

## 7. Workflow-specific notes
- Feature: SPEC decision landed 2026-10-07; implementation waits Verification APPROVE on branch PR.

## 8. Implementation log
- 2026-10-07 — Task opened; fixture added; D-2026-10-07-b recorded; user confirmed feature request.
- 2026-10-07 — GitHub issue #786 opened.
- 2026-10-07 — Post-#787 implementation on `feat/plant-acds-asm-import`: LibreDWG AcDs ASM attach,
  `dwg_convert_SAB_to_SAT1` (`num_blocks`, `CAN_ACIS_IN_DS_DATA`), orphan `3DSOLID` import pass,
  `ImportAcisSolid` SAB path, `AcisSatParser` body synthesis + ASM face/coedge sense aliases.
  Decode attaches 89/89 ASM bodies. LibreDWG ASM→SAT: **15-byte** header skip (was 14 — desynced tag
  stream), RecordTable index remap on tag 12, forced `body` record split; parser vertex field [5] for
  point pointer. Post-pass `$` RecordTable remap in convert; **torus-surface** → `SurfaceKind::Torus`
  with general (u,v) trim + body-transform paramLoop rebuild. Plant ASM coedge **I/F** sense mapped
  inverted vs Civil 3D text `forward`/`reversed` (fixes torus loop vertex closure). `[issue786]`:
  `ImportSatSolid` ok + `Validate` ok; E2E orphan pass imports ≥1 solid.
- 2026-10-07 — Bulk survey (`tools/issue786-survey.ps1`, per-body fresh decode): **33/89**
  `Validate` ok; **11** crash in `ImportSatSolid`; refusals dominated by **sphere-surface (15)**,
  **NotClosed/volume after body transform (15)**, **missing curve record (9)**, **intcurve-curve (4)**,
  **true ellipse (2)**. Repeated `dwg_convert_SAB_to_SAT1` on one `Dwg_Data` still corrupts heap;
  orphan pass capped at **1** attempt until convert isolation is fixed.
- 2026-10-07 — **False “crashes” (11/89)** were `dwg_free` heap corruption: `free_3dsolid` used
  `i <= num_blocks` and freed past the last `encr_sat_data` slot after multi-chunk ASM→SAT convert.
  Fixed in `dwg_spec_shared.h` / `dwg.spec` (`i < num_blocks`). Re-survey: **0 crashes**, **35/89**
  Validate ok; remaining failures are named parser/convert refusals only.
- 2026-10-07 — Orphan pass imports **all** AcDs 3DSOLID orphans (no 1-solid cap). E2E
  `[issue786]`: **35** `cadSolids`, non-degenerate `ComputeWorldExtents`.
- 2026-10-07 — **`sphere-surface` → `SurfaceKind::Sphere`**: real ASM field order (centre, radius,
  axis, refdir), general (u,v) trim + multi-loop holes (ADR-052). Plant bodies that only blocked on
  sphere now fail later (`intcurve`, etc.); E2E count still **35** until those parse.
- 2026-10-07 — **`intcurve-curve` → `CurveKind::Intersection`**: embedded torus+plane, torus+cone,
  or cone+cone; witness from NURBS sample triplets.
- 2026-10-07 — **Null edge curve (`$-1`)**: Plant ASM seam edges import as zero-length `Line`
  (clears **11/11** former `missing curve record` refusals; those bodies now stop on #302 hole loops
  or other face-trim limits).
- 2026-10-07 — **Cone/cylinder/torus hole loops (#302)**: multi-loop faces use `paramLoops` (same
  pattern as plane/sphere); degenerate apex seam loops skipped; cone extent clipped at apex when the
  face crosses it. Null-curve seams omit topology (`edgeIndex -1`). Several reducer bodies still
  refuse on zero axial extent or `Validate` after transform — follow-up.
- 2026-10-07 — **Single-rim cone frusta (Plant reducers, e.g. OI 679)**: constant-height rim loops no
  longer sample as self-crossing `(u,v)` polylines; use analytic rectangle trim. Post-pass
  `FinalizeSingleRimConeFaces` stretches wall extent to the solid's axial span after all vertices
  exist.   OI **679** imports + validates; E2E count unchanged at **35/89** until ellipse / torus /
  other bodies.
- 2026-10-07 — **NotClosed/volume on multi-patch reducers (675, 760, 816)**: investigated; failure
  persists **without** body transform (`Validate` on built topology). These solids split the frustum
  into several cone/cylinder **patches** (multi-loop walls, π-span seam arcs, shared plane rims).
  `FinalizeSingleRimConeFaces` is limited to ≤4-face reducers (679-class) so we do not inflate patch
  area; complex bodies still need per-patch axial/`u` span from topology, not full `[0,2π]×height`
  rectangles.   Regression: `[acissat]` guards 675 against trim/parse regressions only.
- 2026-10-07 — **Volume closure alignment:** `ShrinkRevolutionFaceToParamHeight` after
  `RepackConeParamLoops` so `ConicalFaceIntegrals` uses the trim's axial span when it is smaller than
  the vertex-derived height. Complex reducers: single-rim finalize uses radius-band vertex filter
  (not whole-solid extent). **675/760/816** still NotClosed; **679** + Civil 3D flange stay green.
- 2026-10-07 — **`AlignMultiPatchRevolutionFaceSpans`:** when `paramLoops` exist, use
  `ReframeRevolutionFaceToParamBbox` for combined `u`/`v` fixes (avoid desyncing `uStart/uEnd` from
  polys). **`brep` closure probe:** `IntegrateRevolutionTrimFaceNumeric` walks 3D edges into `(u,v)`
  and quadratures when partial circumferential bands, axial subset, or non–`MakeConeCutStrip` ellipse
  trims would make `ConicalFaceIntegrals` / empty `IntegrateConeCutFaceNumeric` lie. Confirmed OI **675**
  fails `Validate` even with identity body transform (8-face reducer with ellipse rims + π-span arcs);
  E2E still **35/89**.
- 2026-10-07 — **Kernel:** two-ellipse cone cuts (`MakeConeCutStrip`), revolution boundary integrals
  (edge-walk + hole winding), `ImportSatSolidBuildOnly` / `VolumeClosureGap` test hooks. Fixed
  regression that sent full-turn primitive cones through the boundary path. OI **675** closure gap
  ~**0.03** (χ=6, 6v/8e/8f) — parser/topology follow-up, not transform-only.
- 2026-10-07 — **Edge normalization regression:** real ASM edges put **curve at field [8]** (field [7] is
  coedge); partial param ranges append `t0`/`t1` as simplified [4]/[5]. Picking [7] as curve broke
  **679** + Civil 3D flange (`coedge` curve kind); restored `{0,3,5,8}` + param append. **`[acissat]`**
  21/22 green; **675** still ~0.03 closure gap / `NotClosed`.
- 2026-10-07 — **Multi-patch cone height:** `vNeedsFix` treats placeholder height (`<0.05`) as well as
  oversize; `FinalizeSingleRimConeFaces` stretches when height is a placeholder; boundary integral
  skips `vSpan≈0` single-rim walls. Bulk survey **30/89** validate; `[issue786]` scans for any
  validating body (first OI is 675).
- 2026-10-07 — **Plane Validate (attempted/reverted):** multi-loop edge-walk area + general-loop volume
  for `Validate` did not beat `-qLocal.z * PlaneFaceArea` on **675** (~0.03 gap) and regressed
  flange / mass-property tests; reverted. Added `[flange-vol]` diagnostic (gap ~7.6e-5 with current
  path — still passes `ImportSatSolid` when planes use analytic area).
- 2026-10-07 — **675 cone radii (kept):** multi-patch single-rim cones with **inner rim at the high-
  axial end** (`rim.radius + 0.02 < rhoAtZ` at rim slice) set `rTop = rim.radius`, `rBase =
  rhoMaxAtZ(zLo)`. **675** `r1≈0.886/0.890`. Bulk **30/89**, `[acissat]` 22/22; closure ~**0.06**
  (honest frustum vs ~0.045 with mean `r1`). Annulus param-loop Validate path tried/reverted (flange).
  Next: cap/cone paired probe residual (faces 0/6, 3/7) or ellipse-trim cylinder cuts.
- 2026-10-07 — **Cone finalize radii (attempted):** neighbor-cap / bimodal-slice / SAT-hint clustering
  overrides in `FinalizeSingleRimConeFaces` either regressed bulk (**30→27**) or worsened **675** gap
  (~0.30); reverted to mean `rhoAtZ`. **675** remains ~**0.045** `NotClosed` with cones analytically
  integrated.
- 2026-10-07 — **Cone `IntegrateFace`:** full-turn single-rim walls skip
  `RevolutionFaceNeedsBoundaryIntegral` (numeric `(u,v)` area was zero → cone vol terms 0). **675**
  cone faces now match `ConeFaceAnalyticVolTermForTest`; closure gap ~**0.045** remains (probe
  residual mostly cones + annulus caps — likely `FinalizeSingleRimConeFaces` `rhoAtZ` blending inner/
  outer cap radii vs walked annulus 0.886/0.890). **`AlignMultiPatchRevolutionFaceSpans`** skips
  single full-turn rims so finalize `u∈[0,2π]` is not reframed. Neighbor-cap radius pick (attempted)
  regressed bulk **30→27**; reverted. Bulk **30/89**; `[acissat]` 22/22.
- 2026-10-07 — **675 root cause (confirmed):** imported cone patches keep **one full-turn arc loop** only;
  SAT carries **π-span seams + ellipse/intcurve cuts** on adjacent records that never become cone
  `Face::loops`, so `ConicalFaceIntegrals` over a full `[0,2π]×height` frustum disagrees with annulus
  caps + ellipse-trimmed cylinder bands (~**0.06** closure gap with correct `r1≈0.886`; ~**0.045**
  with mean `r1≈0.958` — geometry wrong). Cone 6 shares **no edge** with cylinder 1 (only annulus
  vertex 1); `MakeConeCutStrip` never sees the oblique cut. Skipping `FinalizeSingleRimConeFaces`
  for 8-face solids drops gap to ~**0.036** but leaves placeholder `h≈0.02`. **Next:** route 675-class
  cones through `BuildConeGeneralTrimMulti` / `IntegrateConeCutFaceNumeric` by preserving multi-loop
  SAT topology (or edge-adjacent ellipse planes into `MakeConeCutStrip`).
- 2026-10-07 — **Option 1 (multi-loop cone import):** Plant stream gated by
  `persubent-acadSolidHistory-attrib` + face layout (Civil ACISOUT flange stays on
  `NormalizeRealAcisSchema`). Plant adapters for vertex/point/geometry, edge curve at [8],
  loop-by-owner collection, vertex/arc cone end radii, Plant `RepackConeParamLoops` fallback to
  `BuildConeGeneralTrim`, `FinalizeSingleRimConeFaces` skips when `uSpan` or rim `sweep` &lt; 2π.
  **675** import OK; closure ~**2.40** (not ~0.06). Face **$68** (rec 58) has SAT loops **69+76** but
  loop **76** is null-curve-only → dropped → one walked loop → still full-turn `u`. Next: synthesize
  seam generatrix into `LoopWalk` (partner coedges / adjacent intcurve) or `MakeConeCutStrip` from
  ellipse plane caps; avoid blind π/2 `uEnd` hacks (regressed height).
- 2026-10-07 — **π-seam after align:** wired `plantFaceRecIdPerFace_` +
  `ApplyPlantPiSeamAfterFinalize` **after** `AlignMultiPatchRevolutionFaceSpans` (running π halving
  before align collapsed cone/cylinder height to ~0.02). **675** closure ~**0.52**, cone **h≈0.585**,
  **uSpan≈π**. Civil flange: gate `AdaptPlantAsmBodyRecord` + skip `NormalizeRealAcisSchema` only when
  `DetectPlantAsmSatStream` (persubent + face layout). `[acissat]` **21/22** (oi679 volume still fails).
  Plane caps **1/4** still zero area (duplicate π-seam loops); `paramLoops` vs `loops` mismatch when
  trimming duplicates — follow-up.
- 2026-10-08 — **Plant π-seam plane caps:** `BuildPlaneGeneralTrim` keeps all topology loops in
  `Face::loops` but drops coincident full-circle duplicates from `paramLoops`; `Validate` allows
  `paramLoops.size() < loops.size()` on planes; `PlaneTrimArea` + zero volume area on that split so
  closure stays ~**0.52** while caps pass `DegenerateFace`. **675** `Validate` → `NotClosed` (27),
  not zero-area planes.   Cone finalize + π `u` after align unchanged.
- 2026-10-08 — **Bulk validate baseline:** `[issue786-bulk]` on current branch **0/89**
  `ImportSatSolid` (`validateOk=0`, `convertOk=89`). Histogram: **46** `NotClosed`, **15**
  `DegenerateFace`, **16** true-ellipse refuse, **7** sphere refuse, **3** `DegenerateEdge`, **2**
  wrong trim winding. Civil flange `.sat` still **ok=1**. Plane π-seam caps: use `PlaneTrimArea` for
  volume when `paramLoops.size() < loops.size()` (oi679 closure **7.6→3.0**); skip π-seam finalize on
  ≤4-face solids. `[issue786]` DWG test counts any watertight import (≥1) until bulk target met.
- 2026-10-08 — **Audit against AutoCAD; earlier counts superseded.** AutoCAD 2027 `accoreconsole`
  became the answer key (`tools/acad/*.lsp` → `samples/example-piping-system.acad-*.csv`). It showed
  `Validate` passing was not evidence of shape: of the "30 validating" bodies of the 35/30-of-89
  entries above, 15 were wrong (a valve body stretched into two overlapping cylinders by
  `FinalizeSingleRimConeFaces`, wrong bodies on 27 handles, and so on). Root causes fixed:
  1. Parser: a Plant face's surface was guessed as "the next surface record" — use the explicit
     pointer `[7]` (all 520 faces in the fixture verified).
  2. LibreDWG `SAT_boolean`: the face-logical counter carried between faces (`out forward`), flipping
     ~75 faces — reset per record (VENDORED #10).
  3. LibreDWG AcDs attach was FIFO (the ADR said not to be) — bind through `datidx` / slot handles
     (VENDORED #9; 27 of 89 solids had a neighbour's body).
  4. LibreDWG SAB→SAT printed `%g` (6 digits) — `%.17g` (VENDORED #11); 18 torus elbows then close.
  5. Cone faces closed at their apex (valve bodies) — `BuildPointedConeFace`, analytic extent.
  Then removed, with no loss (count went **up**): `FinalizeSingleRimConeFaces`,
  `AlignMultiPatchRevolutionFaceSpans`, π-seam `ApplyPlantPiSeam*`, zero-height sliver padding,
  vertex-derived cone radii, the plane duplicate-loop trim, `PlaneTrimArea` and the `Validate`
  relaxation it needed (Validate is back to beta's strict form), six `brep` `*ForTest` hooks,
  `gPlantAsmSatStream` (now an `Importer` member). **Result: 60/89 bodies match AutoCAD exactly**;
  29 refused by name (true ellipse 16, sphere-only 7, other 6).
- 2026-10-08 — **SPEC GAP Q3** (torus/sphere deferred by spec, 18 elbows need torus) → user chose
  D-2026-10-08-a. **Q4** (parts at block-local coordinates, placement in Plant objects) → user:
  "build placement now", D-2026-10-08-b.
- 2026-10-08 — **Plant 3D placement** (`src/io/LibreDwgPlant.*`). LibreDWG read R2004+ class
  `dwg_version`/`maint_version` as `BS`; a value ≥ 256 mis-framed every later class so Plant classes
  decoded as objects — read `BL` (VENDORED #12). Plant fields follow their DXF order as DWG bit types
  (strings in the string stream, handles in the handle stream); every decode must end at the data
  boundary or the part is refused. LibreDWG's `unknown_bits` packs its last partial byte LSB-first
  (reader honours it — it cost the low 3 bits of block handles). Pipes: solid cylinder less the
  `[46]`/`[47]` cut-backs. Orphan pass that dropped all 89 catalog solids at block-local coordinates
  removed. **Result: 118 placed solids (59 pipes, 52 of 61 fittings, 7 gaskets), every one matching
  AutoCAD's exploded geometry**; refused by name: 117 structural members (no stored solid), 9
  fittings whose catalog solid the parser refuses.

## 9. Self-verification
- [x] build-project — `./dev/build` (release) green; no new warnings in changed files (`dwg.h`
  C4200 and `ViewportRenderer.cpp` C4189 are pre-existing). Incremental, not a from-scratch clean build.
- [x] architecture-review — new IO module `src/io/LibreDwgPlant.*` owns Plant decode; no upward
  dependency; global `gPlantAsmSatStream` removed; shared helper `ray3d::RotationMatrixToAxisAngle`
  has two uses (ACIS body transform, Plant placement); `tests/SolidMeasure.hpp` has two.
- [x] code-review — every Plant decode fails closed (data-boundary check, pre-R2007 refused);
  SAB re-conversion avoided (`_dxf_sab_converted`); LibreDWG attach bounds-checked and signature-checked.
- [x] dependency-audit — no new dependency. AutoCAD is used only to generate committed test data.
- [x] performance-review — fixture import + both `[issue786]` checks run in ~1.3 s; no hot path touched.
- [x] testing — `[issue786]` (block-local and placed answer keys), `[acissat]` 22/22, `[dwg]` 134/134.
  Full `./dev/test`: 2435/2446; the 11 failures (req377 ×6, req383 ×4,
  `headless.regression-surfacedelete-selection`) fail identically on a clean `beta` checkout.

## 10. Verification result
- **PASS** (2026-10-08). Technical debt: (a) `ComputeMassProperties` integrates general-trim planar
  faces over their sampled `paramLoops` (~0.2% low on holed gaskets; pre-existing ADR-052 behaviour);
  (b) LibreDWG's encoder still writes class version fields as `BS` (identical below 256);
  (c) 29 of 89 bodies still refused — true-ellipse edges (#300), sphere-only bodies, a degenerate
  edge; of the 68 catalog placements in the drawing only 9 are missing (5 `CPB_OFOF` elbows and 1
  `CPNS` with a zero-length edge, 3 valves with true-ellipse edges), each named in the log.
