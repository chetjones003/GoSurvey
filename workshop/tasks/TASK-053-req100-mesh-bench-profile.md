# TASK-053 — REQ-100 profile (b): a mesh bench case, and a record that names its profile

- Type:    feature (FEAT-011)
- Status:  implemented and verified — profile (b) PASSES at p95 1.97 ms on the reference GPU.
           **The acceptance run exposed FINDING-3: every REQ-100 figure on record was measured on
           the integrated GPU, not the RTX 5060 the spec names.** See §8; awaiting the user's
           decision on BUG-013 before the spec figures are rewritten.
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-100** — "The budget has three cost profiles … the bench carries a case for
  each", and its acceptance: "within budget … **in each of the three profiles above**". Profile (b)
  is the missing one. **REQ-064** — "the REQ-100 frame budget is met in Shaded at the REQ-063 mesh
  density chosen for the bench"; that condition is currently unverifiable for the same reason.
- Constraints:  ADR-026 (c) meshes are immutable and shared; ADR-026 (e) Shaded turns depth testing
  on and 2D Wireframe must stay pixel-identical; architecture §11.5 (one visible owner), §11.8
  (interleaved XYZ); REQ-200 (artifacts to `build/`, deterministic); REQ-300 (no dependency);
  REQ-301 (no speculative abstraction); REQ-201.
- Acceptance (restated from the two requirements, as the conditions this task must meet):
  - a mesh cost profile can be measured on the reference machine and reports a p95 against 16 ms;
  - the scene is committed as a **generator**, deterministic and diffable, like the other two;
  - the profile is measured **in Shaded**, since that is what REQ-100 (b) and REQ-064 both name;
  - the run leaves the drawing, camera and visual style exactly as it found them;
  - profiles (a) and (c) are unchanged — same scenes, same numbers.
- Owning subsystem: `util/benchscene` (scene generation) + Commands (the `BENCH` verb and the run).
  No renderer change: the mesh draw path already exists (TASK-041 §9) and this task measures it, it
  does not touch it.

## 2. The one thing that was NOT the Workshop's to decide

REQ-100 (b) says "the REQ-063 density **chosen for the bench** (ADR-026)". **ADR-026 never chose
one** — it says only "the bench needs a mesh case before REQ-064 can claim the budget." So the
density was a dangling reference to a decision that was never taken, exactly as the reference
machine was in TASK-039 §9 and for the same reason: a performance profile with no stated size is
not reproducible.

Raised to the user before planning rather than picked here:

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | What triangle count defines REQ-100's mesh profile? | 2026-08-15 | **2,000,000 triangles** — the fixture TASK-041 already built and validated end to end. Its behaviour is known (VBO-cached, 10% process CPU under orbit versus 12% for not drawing it at all), so the profile has a predicted result and a regression would show as a departure from it |

This becomes a recorded decision in `spec/project.md` and a concrete number in REQ-100 (b), which
is a **spec edit made by a user decision**, not by the Workshop to make code pass.

## 3. Architectural boundary check  (workflow.md §4)

- New abstraction / layer / dependency / ownership change / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.**
- Reasoning, item by item, because "no" is worth showing here:
  - **No new abstraction.** A third free function in `benchscene`, beside the two that are already
    there. REQ-301's bar is not approached — nothing is parameterised over "kinds of scene"; the
    `BENCH` verb branches on a token exactly as it already does for `SURFACE`.
  - **No new state owner.** `BenchRun` gains a triangle count, a saved mesh store and a saved visual
    style, alongside the saved polylines and saved surfaces it already carries. Same owner, same
    lifetime, same save/restore path.
  - **No data-format change.** `.gs` is untouched — the bench scene is never saved. The one format
    that does change is `bench-req100.txt`, which is an append-only human log with no parser; old
    entries keep their old shape and remain readable.
  - **No renderer change.** If measuring the mesh path required touching the mesh path, that would
    be the tell that this is not a measurement task. It does not.

## 4. Verification review  (Step 2 — before any code)

### 4.1 Architectural impact
Confined to `util/` and `commands/`, both of which already own exactly this. Dependencies flow
downward: `benchscene` stays pure and GL-free (it is linked by the test target without a context —
ADR-026 (d) names this as the standing lesson of TASK-035 §11), and the command layer does the
installing. Nothing in `spec/architecture.md` §11 is crossed.

### 4.2 Risks
```
RISK-1 (correctness, HIGH) — the run must restore the visual style, not just the geometry.
  The bench forces Shaded. If it exits without restoring, the user's drawing comes back in a style
  they did not choose, and every subsequent 2D Wireframe parity claim (ADR-026 (e)) is silently
  measured in the wrong mode. Mitigation: the style is saved and restored in the same block as the
  camera, and the restore is in FinishFrameBudgetBench, which is the single exit point.
```
```
RISK-2 (measurement validity, HIGH) — a flat scene would flatter the profile.
  A grid of coplanar triangles has one normal, no depth complexity, and shades in a single gradient
  band. It would produce a number that says nothing about a real shaded model. Mitigation: the mesh
  is the SAME undulating two-sinusoid terrain the surface profile uses, so it is genuinely curved,
  self-occluding under orbit, and directly comparable to profile (c) at a matched size.
```
```
RISK-3 (measurement validity, MEDIUM) — framing. TASK-039 §3 records that the first contour scene
  benchmarked 250k FASTER than 20k because most of it was off-screen. The framing code sizes to the
  scene's bounding sphere but reads whichever store the profile filled; a mesh profile that forgets
  to extend that selection frames an EMPTY viewport and reports a spectacular, meaningless p95.
  Mitigation: extend the existing `frameVerts` selection to the mesh store; the failure is loud
  (a near-zero p95) and the acceptance run will show it.
```
```
RISK-4 (regression, MEDIUM) — profiles (a) and (c) must not move. Their numbers were recorded
  hours ago (TASK-052) and are now quoted in the spec. Mitigation: the mesh branch is additive; the
  existing branches are not edited except where the report and record now name the profile. Verify
  by re-running BENCH 250000 after the change and comparing against 9.27 / 9.11 / 9.08 ms.
```
```
RISK-5 (resource, LOW) — 2M triangles is 1,002,001 vertices: 12 MB positions + 12 MB normals +
  24 MB indices = ~48 MB, uploaded once by the VBO cache (TASK-041 §9). Well within budget for the
  reference machine and no larger than the fixture that path was designed against.
```

### 4.3 Required tests
Unit tests only — the frame measurement itself is not testable without a GL context, which is why
the generator's properties are tested instead (the same argument `BenchSceneTests` already makes):

| # | Test | Proves |
|---|------|--------|
| T1 | `BuildMeshScene` returns **exactly** the requested triangle count, across sizes | the profile measures the density the decision names, not a rounded grid |
| T2 | byte-identical across two runs | "committed bench scene" means something |
| T3 | extent is fixed as the triangle count changes | density changes, not area — the TASK-039 §3 trap, pinned for this generator too |
| T4 | every normal is unit length, and every index is in range | a bad normal shades wrong; an out-of-range index is an out-of-bounds GPU read |
| T5 | the surface is genuinely curved — normals are not all equal | RISK-2, asserted rather than assumed |

### 4.4 Implementation plan (smallest sufficient change)

1. `src/util/benchscene.{hpp,cpp}` — `BuildMeshScene(targetTriangles, verts, normals, indices)`.
   A quad grid over the existing terrain function; normals computed **analytically** from the
   terrain's partial derivatives rather than averaged from faces, which makes them exact and
   order-independent. Emits triangles until the target is hit exactly.
2. `src/commands/CadCommands.hpp` — `BenchRun`: `meshTriangleCount`, `savedMeshes`,
   `savedMeshAttrs`, `savedVisualStyle`.
3. `src/commands/CadCommands.cpp`
   - `StartFrameBudgetBench`: a mesh branch beside the surface one (clear the line stores, install
     one `shared_ptr<const CadMesh>`, force `VisualStyle::Shaded`);
   - the `frameVerts` selection gains the mesh case (RISK-3);
   - `FinishFrameBudgetBench`: restore meshes and style with everything else;
   - the report line and the file record name the profile (FINDING-2);
   - `BENCH MESH [tris] [frames]` parsed beside `SURFACE`, and the help string updated.
4. `tests/BenchSceneTests.cpp` — T1–T5.
5. Spec, on the Q1 decision: REQ-100 (b) gains the number; `project.md` gains the decision-log row.

**One mesh, one part — deliberately.** A real imported model has hundreds of parts and therefore
hundreds of `glDrawElements` calls, and that per-part cost is NOT in this profile. Single-part is
chosen because it is what TASK-041 measured, so the two numbers are comparable and the decision the
user actually took ("the fixture TASK-041 already validated") is the one implemented. Inventing a
part count nobody chose would make the profile incomparable to the only mesh measurement that
exists. Recorded as a known limit of the profile, not left to be discovered.

### 4.5 Verdict
**APPROVE.** No architectural decision is required of the Workshop; the one decision that existed
(Q1) was escalated and answered before planning. The change is additive, confined to the two
subsystems that own it, and every risk above has a stated mitigation and an observable failure.

## 5. Assumptions

```
ASSUMPTION-1: 2,000,000 triangles will pass the 16 ms budget on the reference machine.
- Because:       TASK-041 §9 measured this density at 10% process CPU under continuous orbit with
                 the VBO cache in place — cheaper than 2D Wireframe's 12%, where the mesh is not
                 drawn at all.
- Risk if wrong: REQ-100 profile (b) opens as FAIL, which is a real result and would be recorded as
                 one. It does not invalidate the task; it would start a renderer task.
- Validate by:   the acceptance run. This is measured, not assumed, before the task closes.
```

## 6. Implementation log

- 2026-08-15 — `BuildMeshScene` written. A quad grid over the same two-sinusoid terrain the surface
  profile uses, with **analytic** normals from the terrain's partial derivatives rather than
  face-averaged ones: exact, order-independent, and it makes "byte-identical across runs" a property
  of the geometry instead of the summation order. Triangles are emitted until the target is hit
  exactly, so the profile measures the density the decision named.
  The terrain formula is written out twice — here and in `BuildSurfacePointScene` — deliberately,
  with the reason in a comment: the surface scene's exact bytes back a recorded measurement, and
  saving four lines is not worth any chance of moving it.
- 2026-08-15 — the run: mesh branch installs one single-part `CadMesh`, clears the line and surface
  stores, and forces `VisualStyle::Shaded`. Style saved and restored alongside the camera (RISK-1).
  Framing extended to read the mesh store (RISK-3).
- 2026-08-15 — FINDING-2 fixed at the same time: `profileName` and `scene` are built once and used
  by **both** the console line and the file record, so the two cannot disagree and the record can no
  longer omit what the console says.
- 2026-08-15 — **acceptance run, and it went sideways in a way worth the whole task.** See §8.

## 7. Self-verification

- [x] build-project        — **PASS.** Clean MSVC build, no new warnings.
- [x] architecture-review  — **PASS.** No new abstraction, layer, dependency, ownership change or
      data-format change; §3's boundary check holds as written. `benchscene` stays pure and
      GL-free — the new function is unit-tested without a GL context, like the other two.
- [x] code-review          — **PASS.** The profile is named once and reused rather than formatted
      twice; the mesh branch mirrors the surface branch it sits beside; every non-obvious choice
      (analytic normals, winding, exact count, single part, duplicated terrain formula) carries its
      reason inline.
- [x] dependency-audit     — n-a. Nothing added.
- [x] performance-review   — **PASS**, and it is the point of the task: profile (b) measures
      **p95 1.97 ms** against the 16 ms budget at 2,000,000 triangles on the reference GPU.
- [x] testing              — **PASS.** 6 new cases (T1–T5 plus the degenerate-input case) in
      `BenchSceneTests.cpp`; the `[bench]` tag runs 14 cases / 192,133 assertions green. Full suite
      **329/329**. Confirmed the new cases actually execute rather than trusting the summary — the
      BUG-010 lesson.

## 8. The acceptance run — and what it exposed

The first `BENCH MESH` **failed**: p95 21.40 ms against 16 ms, reproduced at 21.45 and 21.84 ms.
ASSUMPTION-1 said it should pass, so the result was checked rather than accepted:

1. **Was the mesh actually on screen?** Yes — captured mid-run, filling the viewport, shaded. A
   framing bug (RISK-3) would have made it *fast*, not slow.
2. **Did the visual style restore?** Yes — the ribbon read "2D Wireframe" afterwards, so RISK-1 is
   verified by observation, and the slow line runs were not secretly measured in Shaded.
3. **Was the machine hot?** No. Four minutes idle changed nothing, and the GPU sat at 44 °C.
4. **Was it my change?** No — a *fresh process* running the untouched line profile was equally slow.

That last step is what broke it open. The line profile had measured **9.27 ms** at 21:21 and
**13.13 ms** at 22:39, on the same binary and the same scene. Something outside the code had moved.

**The application was rendering on the integrated Radeon 610M, not the RTX 5060.** `nvidia-smi`
reported the discrete GPU at 0% utilisation and 12 W idle *while the benchmark was running*, and the
process's 3D engine load sat on the other adapter. The cause is in the source, or rather absent from
it: GoSurvey exports neither `NvOptimusEnablement` nor `AmdPowerXpressRequestHighPerformance`, the
two symbols a hybrid-laptop driver looks for, so which GPU it gets is left to Windows' heuristics —
and those are free to answer differently between launches.

Forcing the discrete GPU (`GpuPreference=2` for the exe) and re-running everything:

| profile | scene | iGPU (as measured all day) | **RTX 5060** | verdict |
|---|---|---|---|---|
| (a) line segments | 250,000 | 9.27 ms | **1.38 ms** | PASS |
| (b) shaded meshes | 2,000,000 triangles | 21.40 ms — FAIL | **1.97 ms** | **PASS** |
| (c) surface | 100k pts / 199,966 tris | 9.32 ms | **10.28 ms** | PASS |
| (a) headroom | 1,000,000 segments | 21.94 ms — FAIL | **2.30 ms** | PASS |

Three consequences, in order of how much they matter:

```
FINDING-3 — every REQ-100 figure this project has ever recorded was measured on the wrong GPU.
  8.93 ms (clang, TASK-039), 9.27 / 9.32 ms (MSVC, TASK-052 — recorded hours ago), and the entire
  headroom sweep are all iGPU numbers, while `project.md` §7 names an RTX 5060 as the reference
  machine. The budget was never measured on the hardware the spec names. This is the same class of
  error the reference machine and the toolchain pin were introduced to prevent, one level further
  down: the machine was named, the compiler was named, and the *device inside the machine* was not.
```

```
FINDING-4 — this is a product defect, not only a measurement one. Every user on a hybrid laptop —
  which is most surveyors' field machines — is silently getting the weak GPU. The fix is the two
  exported symbols, ~6 lines. It changes the shipped binary's runtime behaviour, so it is escalated
  rather than slipped into this task. Filed as BUG-013.
```

```
FINDING-5 — the surface profile is CPU-bound, and it is now the only profile near the budget.
  It barely moved between the two GPUs (9.32 -> 10.28 ms) while a mesh with TEN TIMES the triangles
  runs at 1.97 ms. Its cost is the per-frame regeneration of triangle edges on the CPU, not
  rasterisation. It went slightly *up* on the faster GPU, which is what a CPU-bound workload does
  when the GPU stops being the thing you wait for. Worth knowing before REQ-069/070/071 add contour
  regeneration on top of it.
```

## 9. Verification result

- Submitted:  2026-08-15
- Verdict:    **PASS** for the delivered scope — profile (b) exists, is measurable, and passes at
              1.97 ms on the reference GPU; profiles (a) and (c) are unchanged in behaviour.
- Findings:   FINDING-3/4/5 above are **not defects in this task** — they are what the task's own
              acceptance run uncovered, and they are escalated rather than absorbed. REQ-100's
              recorded figures and BUG-013 are the user's decisions to take, not the Workshop's.

## 10. Outcome

- Requirements satisfied: **REQ-100 profile (b)** — the missing bench case now exists and passes.
  REQ-064's "budget met in Shaded" condition can be closed on this measurement. REQ-100 as a whole
  is claimable **only once the device question in FINDING-3 is settled**.
- Tests added:            6 cases in `tests/BenchSceneTests.cpp`; suite 329/329.
- Technical debt:         the profile is one mesh of one part, so per-part draw-call cost is not
                          measured (§4.4, stated up front rather than discovered later).
- Docs updated:           this log; TRACKER BUG-013; and the spec corrections FINDING-3 forces.
- Done:                   code complete and verified. The spec record depends on a user decision.
