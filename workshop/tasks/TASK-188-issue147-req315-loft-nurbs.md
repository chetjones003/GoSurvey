# TASK-188 — REQ-315 loft: the NURBS surface and the LOFT command (GitHub issue #147)

## Requirement authority

- **REQ-315** — Sweep and loft on the solid kernel (accepted 2026-09-03, D-2026-09-03-b).
- **ADR-048** — the freeform surface is a hand-rolled minimal NURBS patch; **loft is delivered
  before sweep**.
- Builds on: REQ-314 / ADR-046 (feature-operation layer), REQ-313 / ADR-045 (kernel + invariants).
- Constraints: REQ-101 (±0.01 ft), REQ-201 (no silent failure), REQ-300 (in-tree, no geometry
  library), REQ-301 (minimal abstraction), REQ-100 profile (d).
- Spec task: TASK-187. GitHub issue #147 / #241, Phase 4 of #120.

## Delivery slices (each its own PR, mirroring the B2b-2 A1/A2/B/C split)

### Slice A1 — the NURBS patch math *(this PR)*

New pure module `src/util/nurbs.{hpp,cpp}` — no GL, no `brep` coupling:

- `nurbs::Patch` — a rational tensor-product B-spline patch: degrees ≤ 3, clamped knot vectors, a
  row-major weighted control net.
- `ValidatePatch` / `PatchProblem` — degrees, control counts, knot-vector length, monotonic and
  clamped knots, finite control points, positive weights. Every fault named (REQ-201).
- `Evaluate`, `EvaluateWithDerivs` — Cox–de Boor basis (`BasisFuns` A2.2) and first derivatives
  (`BasisDers`, A2.3 restricted to order 1), then the rational quotient rule for the point, `dS/du`,
  `dS/dv` and the unit normal.
- `Translate` — every control point by an offset (the REQ-101 document-origin rebase hook).
- Builders loft needs: `RuledLinear` (a straight-span patch between two matched polylines),
  `RationalArc` (exact degree-2 rational control points for a circular arc of any sweep),
  `ArcRibbon` (a straight-span patch between two matched arcs).

Tests — `tests/NurbsTests.cpp`, 6 cases: `ValidatePatch` accept + every rejection; a ruled patch is
exact bilinear interpolation; a 3-point ruled patch passes through every point at its chord
parameter; `RationalArc` control points lie exactly on the circle for quarter / half / 3-quarter /
full / negative / sub-quarter sweeps; an `ArcRibbon` between coaxial arcs is a partial cylinder
(radial distance exact to 1e-7) and a full-turn ribbon closes; analytic derivatives agree with a
central finite difference of `Evaluate` to 1e-4; `Translate` at state-plane magnitude moves every
surface point by exactly the offset.

**No `SurfaceKind::Nurbs`, no `LOFT` command, no `.gs` change in this slice** — the module has no
`brep` caller yet, exactly as B2b-2 A1 added `CurveKind::Intersection` and its evaluator before any
Boolean used it.

### Slice A2 — the freeform face in the kernel *(next PR)*

- `SurfaceKind::Nurbs` + the `Patch` payload on `Surface` (additive, defaulted).
- `brep::Loft(const std::vector<Profile>&, Solid* out, Problem* outWhy)` — matched-edge-count planar
  profiles → NURBS side faces (one patch per corresponding profile-edge pair) + two planar caps.
- `ComputeMassProperties` routes a `Nurbs` face through **adaptive Gauss–Legendre quadrature**
  (ADR-045 (b) as widened by D-2026-09-03-b); volume checked against extrude / frustum / barrel
  hand-values within REQ-101.
- `Validate` support (the two-reference-point closure check already covers a NURBS shell),
  `Tessellate` + `TessellateIsolines` + `ClosestPointOnSurface` (Newton) for a `Nurbs` face.
- `.gs` gains the `Nurbs` surface encoding; `kGsFormatVersion` **3 → 4**; `io/GsMigrate` + CI check.

### Slice B — the command and the viewport *(implemented, branch `feat/req315-loft-command`)*

`AppCommandState::Kind::Loft` + a one-value `LoftPhase::SelectProfiles`. `LOFT` (typed alias `LFT`)
opens the prompted form — select two or more closed polylines / circles **in pick order**, Enter to
build; a bare `LOFT` on a ready selection of ≥2 profiles builds immediately (the EXTRUDE-on-selection
shortcut). `CadBuildLoftSolid` is the single source of truth for the live wireframe ghost and the
commit (reuses `GatherExtrudeProfiles` + `brep::Loft`). Wired through the six places
([[project_3d_entity_checklist]]): `ViewportClickRouteFor` (`R::SelectionAccumulate`), the viewport
submit dispatch (fence-merge only), the command-line Enter branch (`HandleLoftTextInput`), the
`CommandInputHint` / prompt (`CadLoftPromptText`), the `CadRubberPreview` ghost, and
`CancelActiveCommand`; plus the `kRegistry` entry. Rendering / selection / erase / one-undo-step and
the DXF/DWG skip message are the generic `cadSolids` paths, unchanged. Headless transcript
`tests/headless/transcripts/req315-loft.txt` (cylinder loft == the cylinder primitive's props;
square→square == the pyramid frustum; prompted CLICK-select flow; undo/redo; single-profile refusal;
`.gs` save/reopen). ctest 1050/1050. **Known limitation**: the ghost rebuilds `brep::Loft` (with its
numerical `Validate`) each frame while ≥2 profiles are selected — same shape as the EXTRUDE/REVOLVE
ghosts but heavier per rebuild; fine for the 2–3 simple profiles a loft normally has, revisit if a
many-edge loft preview ever stutters.

## Sweep (REQ-315, the second increment) — kernel slice *(branch `feat/req315-sweep-kernel`)*

`brep::Sweep(const Profile&, const SweepPath&, const SweepOptions&, ...)` — one closed planar
profile along **one path segment** (a straight line or a single circular arc; a bulge-polyline path
is the next slice). `SweepPath` = `{arc, start, end, centre, normal, sweep}`; `SweepOptions` =
`{twistRad, alignToPath}`.

- **Straight path** → ruled `SurfaceKind::Nurbs` side faces (`nurbs::RuledCurveToCurve` per profile
  edge) + planar caps; **reproduces `Extrude`** (asserted). `alignToPath` rotates the profile onto
  the plane perpendicular to the path; a constant `twistRad` rotates the end frame about the path
  tangent (realized as one ruled band from untwisted to fully twisted — a many-section subdivision is
  a follow-up for large twists).
- **Arc path** → each profile edge is **revolved** about the arc's axis (`nurbs::RevolveCurve`, exact
  degree-2 rational V); **reproduces the solid of revolution** (Pappus volume asserted for 90°, 180°,
  306°). Rails follow the path as `CurveKind::Arc` edges. Twist / fixed-orientation on a curved path,
  a full-turn arc, and a profile that reaches the axis are each refused by name
  (`SweepUnsupportedOption` / `SweepPathDegenerate` / `SweepProfileTouchesAxis`).
- New `nurbs` builders: `Curve` (a rational B-spline curve), `LineCurve`, `ArcCurve`,
  `RuledCurveToCurve`, `RevolveCurve` (handles an on-axis control point as a collapsed pole column).
- **`IntegrateNurbsFaceNumeric` now integrates per knot cell** (a graded Gauss rule inside each,
  boundaries at the distinct interior knots) — a multi-span rational patch (a >90° revolve, a
  multi-quarter arc ribbon) is only C^(p-1) at its knots and a panel straddling one lost accuracy;
  this also tightens loft's `ArcRibbon` faces. `Tessellate` / `TessellateIsolines` gained a
  control-net planarity test so a **twisted** bilinear patch is subdivided, not drawn as one quad.
- `.gs`: no format change — a swept solid is `SurfaceKind::Nurbs` faces, already v4.

Tests: 7 in `BrepTests` (extrude-equivalence, oblique straight path, arc-path Pappus volume ×3
angles, tilted survey-magnitude arc path, twisted straight sweep with a tessellation cross-check,
`Translate` at state-plane magnitude, every refusal by name) + a swept-solid `.gs` round-trip in
`BrepJsonTests`. Build green, ctest **1057/1057**.

## Sweep — command slice *(branch `feat/req315-sweep-command`)*

`AppCommandState::Kind::Sweep` + a one-value `SweepPhase::SelectInputs`. `SWEEP` (alias `SWP`) opens
the prompted form — select **one** closed polyline / circle (the profile) and **one** line or arc
(the path), Enter to build; a bare `SWEEP` on a ready selection of both builds immediately. Selection
order does not matter: `GatherSweepInputs` takes the closed loop as the profile
(`ExtrudeProfileFromSelection`) and the open curve as the path (`SweepPathFromSelection` — a LINE
gives a straight `brep::SweepPath`, a `CadArc` gives an arc one via `ucs::FromNormal` +
`PointOnPlaneCircle`). `CadBuildSweepSolid` is the one source of truth for the ghost and the commit
(`brep::Sweep`, default options). Wired through the six places
([[project_3d_entity_checklist]]) exactly like LOFT + the `kRegistry` entry. Twist / alignment
keyword options are a follow-up (kernel already takes them). Headless transcript
`tests/headless/transcripts/req315-sweep.txt`: a rectangle swept along a line == a 240-volume prism
(topology + area exact); a circle swept along a 90° arc == a quarter torus (`30·π²` volume and
`π²·30 + 8π` area exact); undo/redo; a profile with no path refused; `.gs` save/reopen. ctest
**1059/1059**.

## Sweep — bulge-polyline path *(branch `feat/req315-sweep-polyline`)*

`SweepPath` generalised from a single `{arc,start,end,centre,normal,sweep}` to `{points, segments}`
— an open chain of `SweepSegment{arc,centre,normal,sweep}`. `brep::Sweep` now builds **one band per
segment**, Loft-style, with the rings **shared at the joints**. A rotation-minimizing frame is
carried along the path: unchanged across a straight segment (constant tangent), rotated by the arc
about its plane normal across an arc segment (parallel transport — no torsion on a circular arc). A
**tangent discontinuity at a joint (a mitred corner) is refused by name**
(`SweepUnsupportedOption`) — the common bulge-polyline / filleted-path case is tangent-continuous and
is supported. Twist / fixed-orientation still require a single straight segment (guarded). A
single-segment path reduces exactly to the previous code path (all prior sweep tests pass unchanged).

`IntegrateNurbsFaceNumeric`'s knot-cell change (from the previous slice) is what lets a multi-span
patch anywhere in a multi-band sweep integrate accurately.

Tests: 3 new in `BrepTests` (a circle along line→tangent-arc→line is a bent pipe with volume
`18π + 2.5π²`; a 90° corner is refused; two collinear segments == the one-segment sweep) + the
`ArcPath` / `LinePath` / `BrepJsonTests` helpers updated to the new struct. ctest **1062/1062**.

**Deferred**: a mitred corner (needs a miter cross-section); twist / fixed orientation on a curved
or multi-segment path; a full-turn arc segment; the `SWEEP` command reading a **polyline** entity as
the path (bulge extraction + corner-refusal UX); the `SWEEP` twist / alignment keyword options.

### Slice B (original plan) — the command and the viewport

- `LOFT` (typed + prompted), wired through the six places
  ([[project_3d_entity_checklist]]): route, submit-pick, command-line Enter, input hint / resolve,
  rubber preview (shares the builder with the commit), cancel; plus the `kRegistry` entry.
- Rendering in every REQ-064 style; selection / erase / one-undo-step; DXF/DWG skip message.

## Status

**Slice A1 implemented** (PR #244). **Slice A2 implemented** (branch
`feat/req315-loft-nurbs-kernel`): `SurfaceKind::Nurbs` + the `Patch` payload on `Surface`;
`brep::Loft(std::vector<Profile>, ...)` — Extrude's topology generalised to N profiles, one NURBS
side patch per corresponding edge pair (`RuledLinear` / `ArcRibbon`) plus two planar caps;
`IntegrateFace` routes a `Nurbs` face through nested `GradedGaussIntegrate` (area = ∫∫|Su×Sv|,
vol term = ∫∫(S−q)·(Su×Sv)); `Validate` (patch validity + closure residual relaxed to `1e-5·scale³`
like the procedural-edge case), `Tessellate` (uniform param grid, analytic normals),
`TessellateIsolines` (interior param lines), `ClosestPointOnSurface` (grid + Gauss–Newton),
`Translate` (moves the control net), `ComputeBounds` (control-net hull). `.gs`: the encoding moved to
a new pure header `src/io/BrepJson.hpp` (`namespace gsio`, linkable without the command layer); a
`Nurbs` face writes a `patch` key; `kGsFormatVersion` **3 → 4** with a no-op v3→v4 migration step and
the CI check unchanged (it reads the constant). Build green, `ctest` **1049/1049** (+12: 8 loft cases
in `BrepTests`, 4 in the new `BrepJsonTests`, plus a migration-chain test). Slice B (the `LOFT`
command + viewport) to follow as its own PR.

## Verification

- `build-project`: `./dev/build` clean (release).
- `testing`: `./dev/test` — 1037/1037, including 6 new `NurbsTests` cases.
- `architecture-review`: new module is pure `util/` (only `ray3d`), directly unit-tested, no GL —
  matches ADR-048 (c) and the ADR-002 layering. No existing file's behaviour changes.
- `dependency-audit`: no new dependency; the NURBS maths is hand-rolled (REQ-300).
