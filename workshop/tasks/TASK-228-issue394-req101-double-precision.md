# TASK-228 — REQ-101 ±0.002 ft: widen coordinate storage `float` → `double`

- Type:    refactor (spec-authorized architecture migration)
- Status:  in progress — PR 1 done; Phase A (#440), B (#441), C (#442), D (#443), E (#444), F (#447) done, closes #394; Phase G open
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #394 (sub-issues #440 A, #441 B, #442 C, #443 D, #444 E, #447 F — SurveyPoint, #453 G — TIN/surface mesh)

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

- **Phase A — core entity stores + the three copies. DONE (#440, commit `3fbd82f`).** Widened the
  four flat stores to `std::vector<double>` and every entity scalar coordinate field to `double`,
  across the live `AppCommandState`, the undo `DrawingGeometrySnapshot`, and the per-tab struct.
  GPU upload path narrows `double`→`float` at buffer assembly — `RenderScene` / `AppendChainEdgesVc`
  take `const std::vector<double>&` (ADR-054 (b)). Shared coordinate helpers templated on scalar type
  so no call site changed. `.gs`/DWG-trailer JSON reader reads coordinate arrays as `double` (the
  writer already emitted full precision — narrowing on read broke round-trip idempotence, caught by
  `regression-61` + `fuzz-smoke`). `CadCoordinateFrame` / rebase logic unchanged and still runs; the
  entry-time and load-time establishment both still fire (dropping the "one-time" *spec* acceptance
  bullet per ADR-054 (c) does not require removing the guard — it still returns early when an origin
  is already set). Build clean; `ctest` 1359/1359. **Deferred to Phase B:** DXF/DWG-native import
  `static_cast<float>` narrowing sites, and paper-space stores (paper inches — small magnitudes).
- **Phase B — import narrowing. DONE (#441).** `LibreDwgCad.cpp` (`LocalLine`/`LocalCircle`/`LocalArc`
  /`LocalPolyline`/`LocalText`, ellipse import) and `DxfIo.cpp` (line/circle/arc/ellipse/polyline/
  filled-region import) stop narrowing coordinates through `static_cast<float>`. **`CadArc::startRad`
  /`sweepRad` and `CadEllipse::majVx/majVy/ratio` reverted to `float`** — they are angles / a
  direction / a ratio, not coordinates (endpoint error is r·Δθ, well inside ±0.002 ft), and keeping
  them `float` preserves the DXF writer's byte-stable angle round-trip. DXF writer: `worldX`/`worldY`
  take `double`; the `$EXTMIN/$EXTMAX` extent sweep snaps every coordinate to the written six-decimal
  grid (`q6`/`q6lx`/`q6ly`) so it sees what a reader reconstructs — `float` storage made that free
  (float ≈ 6-7 sig digits at model magnitude), `double` does not, and a shallow huge-radius arc's
  near-cancelling centre+radius otherwise lands ~1e-6 off on re-export (`regression-111`/`-113`).
  **ADR-054 (a) amended:** paper-space stores stay `float` (sheet inches — `float` resolves ~1e-6 in,
  orders of magnitude inside ±0.002 ft; widening is pure churn). `SurveyPoint` split to Phase F (#447).
  Build clean; `ctest` 1359/1359. **Follow-up (closes #441):** the DWG-trailer document is the same
  `double` GsIo JSON tree as `.gst` (Phase A already widened the reader/writer), so no further
  production code was needed — only the two acceptance-criteria tests were missing. Added
  `tests/LibreDwgCadTests.cpp` "DWG trailer round-trips a state-plane coordinate within REQ-101
  tolerance" (compares world coordinates — `local + worldDocumentOrigin`, since a state-plane
  magnitude rebases on import) and "A legacy float-precision DWG trailer still loads within the old
  REQ-101 tolerance" (hand-builds a trailer whose JSON already carries only `float` resolution,
  mirroring DwgIo.cpp's private magic/length trailer layout). No `kGsFormatVersion` bump: the trailer
  JSON shape is unchanged, so there is nothing for a legacy reader to fail open on. Build clean;
  `ctest` 1361/1361.
- **Phase C — snap / pick read-back. DONE (#442).** `CadSnap::Hit::x/y/z` → `double`;
  `AppCommandState::viewportSnapPickLocalX/Y/Z` → `double`; `SubmitViewportPick` /
  `SubmitViewportPickImpl` / `UiSubmitViewportPick` entry coordinates → `double`;
  `ApplySegmentAnglePickToViewportPick` → `double&`; `CadUi` `commitX/commitY` and the snapped
  `curMX/curMY` submit calls stay `double` (dropped their `static_cast<float>`). The
  snap→commit chain is now `double` end to end, so REQ-101's *bit-identical-snap* property holds at
  any drawing magnitude (a `float` copy broke it above ~10,000 ft local). The ORTHO / polar /
  angle-lock constraint helpers (`ApplyOrthoConstrainFromAnchor`, `ApplySegmentAngleLockToWorldPick`)
  keep working in `float` — narrowed across the call and back — because an axis/ray-locked pick is a
  computed, pixel-bounded point that REQ-101 scopes out, and it is mutually exclusive with an active
  object snap. Rubber-band **preview** buffers stay `float` (render-only, GPU-bound). Build clean;
  `ctest` 1359/1359. Only 12 boundary sites needed edits — the `float wx/wy` pick handlers compile
  unchanged (double→float at their internal comparisons, warning-suppressed).
- **Phase D — the GPU-upload narrowing point. DONE (#443).** Audited `commands/`, `viewport/`,
  `io/`, `util/` for a `float` coordinate carrying stored/authoritative geometry. Confirmed the four
  flat stores and `CadFilledRegion::vertsXyz` are `double` on all three copies (live state, undo
  snapshot, per-tab document) per Phases A-C, and the single narrowing point is
  `WorldToViewRelativeFloat` (`util/geom2d.cpp`) — it takes `double` world coordinates and the view
  anchor, subtracts in `double`, and narrows only the already view-local result; every GPU-buffer
  builder in `ViewportRenderer.cpp` (`AppendChainEdgesVc` and the other `RenderScene` helpers) calls
  through it or narrows an already-render-local (preview/hover/highlight/gizmo) buffer. Documented
  both sites with a comment citing ADR-054 (b). No upstream narrowing bug found — every other
  `float` coordinate site on the geometry path is a legitimate, already-decided exception (angles/
  ratios — Phase B; paper-space sheet inches — ADR-054 (a) amendment; rubber-band preview and other
  render-only buffers; ORTHO/polar/angle-lock constraint internals — Phase C). **New finding,
  deliberately deferred, not fixed here:** TIN/surface mesh vertex storage (`util/tinbuild.cpp`
  `TinBuildResult::vertsXyz` and the `TinTriangleElevationAt`/`TinElevationAt`/`TinCullByBoundaries`/
  `TinBorderEdges`/`FindNearestInteriorEdge`/`TinSwapInteriorEdgeNear`/`TinDeleteInteriorEdgeNear`
  family that reads it) is still `std::vector<float>`. ADR-054 (a) names "surface vertices" as an
  authoritative store that should widen, but it was never one of the four named flat stores Phases
  A-C covered, and widening it touches surface build/render/snap/volume/contour code well beyond a
  one-PR audit. Same pattern as `SurveyPoint` (deferred to Phase F, #447): recommend a Phase G
  sub-issue rather than silently expanding this PR. Added a compile-time guard (`static_assert` in
  `CadCommands.hpp`/`CadEntities.hpp`) on the three copies of the four named stores so a reintroduced
  `float` there is a build error, not a silent regression — proven red (reverted one store to `float`,
  confirmed two `static_assert` failures) before green. Build clean; `ctest` 1359/1359.
- **Phase E — test-assertion sweep. DONE (#444).** Every `0.01` literal and named constant that
  represents the REQ-101 guarantee audited one at a time and, where it did, changed to `0.002`:
  - `kReq101` (`src/viewport/CadSnap.cpp`) — is the guarantee → `0.002`.
  - Model-space `kTol` in `src/commands/CadCommands.cpp` — `ApplyBreakToLine` (~15757),
    `ApplyBreakToArc` (~15840), `ApplyBreakToOpenPolyline` (~15939): these read the four core `double`
    flat stores (Phases A-D) but the local endpoint-coincidence arithmetic (`x0/y0/z0/x1/y1/z1`,
    `totalLen`, `ux/uy`, `nearP/farP`, `kTol` itself) was still narrowing through `float`, which only
    resolves ~0.008 ft at large coordinates — so a bare constant change would have been cosmetic.
    Widened those locals to `double` and set `kTol = 0.002`, logic otherwise unchanged. (Residual note:
    `ApplyBreakToOpenPolyline`'s `totalLen` still ultimately derives from `PolylineOpenLengthOf`, which
    itself returns `float` — a pre-existing narrowing one level up that this phase's named-locals scope
    did not reach; left for a future audit, does not fail any test at ±0.002 ft today.)
  - Paper-space `kTol` (`ApplyBreakToPaperLine` ~16235, paper-arc break ~16316,
    `ApplyBreakToPaperPolyline` ~16393) — deliberately left at `0.01f`: paper-space stores stay `float`
    per ADR-054 (a)'s amendment (sheet inches, resolves ~1e-6 in), and REQ-101 is a world/model-space
    guarantee.
  - `kTinPlanEpsilon` (`src/util/tinbuild.hpp`) — independent domain "same field shot" de-dup
    threshold; TIN vertex storage is still `float` (Phase G, #453). Left at `0.01`, comment updated to
    say so explicitly.
  - `kSolidChordToleranceFt` (`src/util/cadsolid.hpp`) — tessellation chord tolerance, a
    rendering/pick density knob independent of coordinate storage (issue #394 AC item 5, covered by
    #384's isoline work). Left at `0.01`, comment updated.
  - Per-assertion `0.01` literals across the Catch2 suite: `tests/CurveIntersectTests.cpp` `kReq101`
    (curve/curve intersection accuracy, `double` math) and `tests/CadSnapTests.cpp` (hand-computed
    endpoint-snap and perspective/orthographic snap-agreement assertions reading the core `double`
    stores) → `0.002`. Everything else across `tests/*.cpp` — TIN/surface-query assertions
    (`TinQueryTests`, `Issue119SurfaceTests`, `SurfaceProfileTests`, `TinVolumeTests`,
    `ContourGenTests`, `TinBuildTests`, `SurfaceAnalysisTests`, `SolidPickTests`, `SurfaceVolumeTests`,
    `WatershedTests`, `PushPullTests`), still backed by `float` TIN storage or tessellation-chord/
    volume tolerances; `GltfImportTests`/`StlImportTests` (`modelimport::Result::vertsXyz`, still
    `float`, not one of the four core stores); `BrepTests`/`AcisSatParserTests`/`FilletGeomTests`
    (chord-tessellation params/volume epsilons); `CadBlockImportTests` (an angle, not a coordinate);
    `HoverDwellTests` (a wall-clock seconds argument); `Trim3DDrawnLineTests`/`Trim3DLineLineTests`/
    `UcsTests` (comments only, no live assertion) — left at `0.01` with the reason recorded per site.
  - Build clean; `ctest` 1359/1359.
- **Phase F — SurveyPoint. DONE (#447).** Widened `SurveyPoint::easting/northing/elevation`
  (`src/survey/SurveyPoints.hpp`) to `double`, plus every helper signature that carries a
  survey-point coordinate: `AppendSurveyPointCrossVertices` (easting/northing/elevationZ; `outLines`
  stays `std::vector<float>*`, narrowed once at the GPU-buffer `push_back` per ADR-054 (b), same
  pattern as `WorldToViewRelativeFloat`), `TryPlaceSurveyPoint`, `DuplicateSelectedSurveyPointsTranslated`
  /`Rotated`/`Reflected` (dx/dy/dz, bx/by, x0/y0/x1/y1 — `rad` stays `float`, an angle, Phase B
  precedent), and the file-local `RotateSurveyCoords`/`ReflectSurveyCoords` helpers. Added `double`
  overloads of `CadCoord::WorldXFromLocal`/`WorldYFromLocal`/`LocalFromWorld` (`CadCoordinateFrame.hpp`)
  rather than narrowing every survey-point caller through the existing `float` overloads — those
  stay for the viewport-cursor caller that is still `float`; the two are disambiguated by ordinary
  overload resolution, so no call site needed a cast. `CadCoordinateFrame.cpp`'s `ShiftAllStorageBy`
  needed no change at all: `add2` is already a generic lambda, so it started carrying survey points
  through the document-origin rebase at full `double` precision for free.
  **Real narrowing bugs fixed** (not just type-widening churn): `DxfIo.cpp`'s POINT/XDATA reader
  (`sp.easting = static_cast<float>(wx - st.worldDocumentOriginX)`, the embedded-points-conflict
  merge) and `SurveyCsv.cpp`'s CSV importer (`pr.pt.easting = static_cast<float>(pr.worldE -
  st.worldDocumentOriginX)`) both narrowed to `float` at exactly the width the DXF-extent sweep and
  entry-time-establishment precedents (Phase A/B) warn about — the CSV path in particular computes
  the local coordinate at ORIGIN-ZERO magnitude (full state-plane value) before
  `MaybeRebaseLargeCoordinates` ever runs, so the old `float` field quantized the point before the
  rebase had a chance to help, exactly the "narrow-before-origin" hazard
  `regression-req101-origin-at-entry` pins for typed LINE points. `GsIo.cpp`'s survey-point JSON
  reader (`o.value("easting", 0.f)` → `0.0`) had the same Phase-A-pattern bug the `.gs`/DWG-trailer
  coordinate arrays already had fixed. The internal VIEWPOINTS Save/Load JSON writer
  (`SaveSurveyPointsToJsonFile`) also needed `std::setprecision(17)` added (it had none — `<<`'s
  default 6-significant-digit precision cannot round-trip a `double` state-plane value; the reader's
  `parseFloatField`/`strtof` became `parseDoubleField`/`strtod`). The DXF `$EXTMIN/$EXTMAX` extent
  sweep for survey points (`DxfIo.cpp`) was not applying the `q6lx`/`q6ly` reader-agreement
  quantization every other entity kind there uses — added, matching Phase B.
  **Narrowing-boundary decisions** (left `float`, each at an established boundary): the ImGui
  survey-point Properties/VIEWPOINTS-table editors and the multi-select `applyCoord` helper (widened
  its `float SurveyPoint::* memb` pointer-to-member to `double SurveyPoint::*`, since it now
  compares/writes against a double field — but the ImGui widgets themselves already used
  `InputDouble`, so no precision was actually lost there before this phase either); `CadCommands.cpp`'s
  viewport-pick/box-select screen-projection lambdas (`SP`, `worldToScreen`, `wts`) and
  `CadSnap.cpp`'s whole snap-candidate pipeline (`ConsiderSnap`, `MinDistSqToSurveyMarker`,
  `PushSnapPickerEntry`, the grip-candidate lambda) — render/pick boundary, Phase C/D precedent,
  every other entity kind narrows there too; `CadUi.cpp`'s QuickSelect numeric-match lambda
  (`matchNum`) and the survey-label annotation-box math in `SurveyPoints.cpp`
  (`RepositionSurveyLabelMtextForPoint` — `CadAnnotation::boxMinX` etc. are a `float` store, not one
  of the four core stores, ADR-054 scope); `CadUi_Toolspace.cpp`'s `ZoomToSurveyPoints` (widened the
  accumulation locals to `double`, narrows only at the `float` `pendingZoomMnX` etc. viewport-state
  assignment). `CadCommands_Align.cpp` (ALIGN) was intentionally left untouched — the 2D survey
  Helmert-fit module the D-2026-09-08-c decision already closed no-change for REQ-329; its
  `HelmertPt` is templated so it compiled unchanged against the now-`double` fields, and its own
  `AlignControlPt`/`HelmertResult` stay `float` by that same decision. `CadUi_TraverseEditor.cpp`'s
  "Commit to Drawing" button was widened (`startE/startN`, `locE/locN` locals `float`→`double`) since
  its source fields (`TraverseData::startEasting` etc.) were already `double` — a real, if minor,
  precision fix, not churn.
  **Test:** `tests/LibreDwgCadTests.cpp` gained "DXF survey point XDATA round-trips a state-plane
  coordinate within REQ-101 tolerance" and "CSV import stores a state-plane survey point within
  REQ-101 tolerance" (the latter proven red against the pre-fix `SurveyCsv.cpp` narrowing cast, using
  the same 2000000.10/500000.03 values `regression-req101-origin-at-entry` documents quantizing to
  2000000.125 — both tests needed `Catch::Approx(...).margin(0.002).epsilon(0.0)`: Catch2's default
  *relative* epsilon at a ~2e6 magnitude is worth ~2000+ ft on its own and silently swallows a 0.002
  ft margin, which is a trap for every REQ-101 assertion at state-plane magnitude, not just this
  one). Survey-point label creation reaches `ImGui::GetFont()`
  (`EnsureSurveyPointLabelMtext`/`MtextRichNaturalContentPx`), so the CSV test needed the same
  `HeadlessImGuiScope` fixture `GsMigrateLegacyBreaklineTests.cpp` established. Build clean; `ctest`
  1363/1363 (1361 + 2 new). Traverse/adjustment math (`tests/TraverseTests.cpp`) does not read or
  write `SurveyPoint` at all — it operates on its own `double` types already — so no residual
  tolerance changed.
  **Deferred, not fixed here** (recommend a follow-up issue, not created): `SurveyFilePoint::elevation`
  (`src/io/SurveyCsv.hpp`, REQ-086 linked-surface point files) stays `float` — it feeds the TIN
  builder, which is Phase G's (#453) scope, not Phase F's; `ApplyRotationToSelection`/
  `ApplyScaleToSelection`'s own `bx`/`by`/`sc` parameters (the general MOVE/ROTATE/SCALE modify-command
  entry points, REQ-329) are still `float` at that outer layer — untouched, pre-existing, and outside
  this phase's named scope (only the survey-point-specific inner functions were named).
- **Phase G (#453) — TIN/surface mesh vertex storage.** Surfaced by Phase D's audit: `util/tinbuild.cpp`
  still stores TIN/surface mesh vertices as `float`. Out of scope for Phases A-C (the four core flat
  stores); split off as its own phase for the same reason `SurveyPoint` was split into Phase F —
  larger, separate subsystem. Widen the TIN/surface vertex store(s) to `double`, keep the GPU-upload
  narrowing at the single point established in Phase D, and reconcile `kTinPlanEpsilon` (Phase E)
  against the widened store.

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
