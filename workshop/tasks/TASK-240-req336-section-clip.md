# TASK-240 — SECTIONCLIP: the live section clip, and the frame the plane has to be stated in

- Type:    feat (new requirement + new ADR)
- Status:  review
- Opened:  2026-09-10
- Owner:   Workshop
- GitHub:  #149 acceptance 6 (3D Phase 6 — Analysis), step 6 of 6 — **the last criterion of the phase**.

## 1. Authority

- **REQ-336** (new, accepted 2026-09-10, D-2026-09-10-a) — the requirement this delivers.
- **ADR-056** (new) — the six decisions behind it. A new render capability needs an ADR and an
  accepted REQ before implementation (CLAUDE.md §4); both were drafted locally and ship here with
  the code, per the one-PR rule.
- **REQ-058 / ADR-025 (c)** — the camera. The whole shape of (a) below exists to leave it alone.
- **REQ-311 / D-2026-08-31-e** — `ucs::Ucs` IS the plane abstraction; there is no second plane type.
- **REQ-335 / D-2026-09-09-i** — `SECTION` chose the active UCS plane; this matches it deliberately.
- **REQ-101** — ±0.002 ft, which is what makes the anchor question a correctness question.
- **REQ-201** — refuse with a stated reason and leave the previous state intact.
- Constraints: CON-06 smallest change.

## 2. Problem

#149 acceptance 6: *"Section clipping updates live as the plane moves."*

Unlike the five criteria before it, this one had **no foundation**. `src/render/` is four files and
contained no clip-plane machinery of any kind; the only occurrences of "clip" in the tree were
unrelated text-layout wording and the frustum's near/far pad.

The phase gameplan flagged this and said, in as many words: *expect it to be the step that needs its
own probe*, and *if it turns out to need a rendering-architecture decision, stop and raise it rather
than shipping a partial phase.* Both came true — but not where the plan expected.

## 3. What the probe settled before any code (P7, 2026-09-10)

`Notes for claude/issue149-analysis/probes/p7_clipplane_route.cpp`, run against a real GL context.
It links no kernel — it links the app's vendored GLFW + GLEW, the app's header-only `Camera`, and
**compiles the four vertex-shader sources byte-for-byte out of `ViewportRenderer.cpp`**, so the
answers are the shipping shaders' own.

| question | measured answer |
|---|---|
| is a clip plane available? | `GL_MAX_CLIP_DISTANCES` = 8; all four vertex shaders compile with the edit, with and without redeclaring the array |
| does it disturb the camera? | no — the plane is a **uniform**, not a matrix |
| does it land where asked? | yes, once rebased: plan, orbited, axis-aligned, oblique and horizontal, at the origin and at E 2,196,010 / N 1,400,000 — worst error **8e-6 ft** |
| what does the obvious version cost? | **exact at the origin**; the plane sits at `anchor + c`, i.e. **2,196,000 ft out** at state-plane, **2,542,755.99 ft** on an oblique plane there, and **moves 1 ft per foot of pan** |
| does it cap the cut? | **no** — centre-pixel depth moved by exactly the box's own depth extent; no `GL_CULL_FACE` anywhere in `src/`, so a clipped solid shows its interior |
| does perspective matter? | no — `RenderScene` never reads `cam.projection` and always builds `Ortho` |

**The one that mattered is the third and fourth together.** `ViewportRenderer` does not upload world
coordinates: *"Vertices arrive with XY relative to the view anchor but Z ABSOLUTE"*
(`ViewportRenderer.cpp:1123`), and `viewAnchorX = panX = cam.targetX` (`:1097`) — the anchor **is
the pan point**. A plane handed to the shader in world coordinates is in the wrong frame, and:

- it is **bit-identical to the correct version at the origin**;
- a **horizontal** cut is exact in *both* versions, because the anchoring covers X and Y only.

So the first plane a person tries (a level cut) and any test written at the origin both pass a badly
wrong implementation. That is the same shape as REQ-334's centroid reference point
(D-2026-09-09-h) — **the second time in this phase** that a state-plane magnitude has been what
separates a correct implementation from a plausible one.

## 4. The scope question, raised before starting

P7 also found the rendering-architecture decision the gameplan predicted, and it is **not** about the
camera. `gl_ClipDistance` clips GL draws. Dimensions (`CadUi.cpp:16620`, `:16718`), annotation text
(`:16556`) and line-pattern hatches (`:16534`) are drawn by the **ImGui overlay** through
`Camera::WorldToScreen`, and **no GPU clip plane can reach them**.

Three options, none small: state the limit in the REQ; CPU-clip at each `WorldToScreen` site; or
move that geometry into GL.

Per [[gosurvey-one-pr-per-issue]] rule 3 this was **put to Nathan before any code was written**
rather than noted in a PR body afterwards. **Decision, 2026-09-10: state the limit.** Acceptance 6
is met as written, the other two options are a rendering-architecture change out of proportion to
one criterion of one phase, and REQ-336 + ADR-056 (e) now carry the cost of closing it in writing.

## 5. Approach

**(a) One GL-free header, `src/render/SectionClip.hpp`.** All the arithmetic — the plane type,
`SectionClipFromUcs`, `SectionClipToShaderVec4`, `SectionClipDisabledVec4` — lives there for the
reason `Camera.hpp` is header-only (ADR-002): it must be unit-testable without a window. The feature's
entire risk is one function, and that function is reachable from a test.

**(b) `RenderTuning` carries the plane**, rather than becoming `RenderScene` parameter 31 — which is
the reason `RenderTuning` exists. Default-inactive, so every existing call site renders exactly what
it rendered before.

**(c) The plane is stated in WORLD coordinates by the command layer and rebased by the renderer**,
where the anchor is known. The anchor is a renderer-private float-precision device; no caller should
have to know it exists, and a caller that did would have to recompute on every pan.

**(d) The clip is set once per frame on four programs and switched with `GL_CLIP_DISTANCE0`.**
Uniforms are per-program state that survives until that program is next used, so one set per frame is
enough and the passes only flip the enable. `clipForGeometry()` / `clipForOverlay()` sit beside the
existing `depthForGeometry()` / `depthForOverlay()` and follow the same line: model geometry clips,
UI does not. The **grid** is excluded too — it is drawn *on* the UCS plane, so at offset 0 it is
coincident with the clip plane.

**(e) `GL_CLIP_DISTANCE0` is disabled unconditionally at `finish_render`.** ImGui draws the entire
interface right after with shaders that never write `gl_ClipDistance`, and an unwritten clip distance
while the state is enabled is **undefined** — a missing disable can delete arbitrary parts of the UI.
For the same reason the geometry shaders write it unconditionally and the plane is *neutralised* to
`(0,0,0,1)` when off, rather than branching or permuting shaders.

**(f) The command is a view toggle, shaped like `PERSPECTIVE` and `CROSSHAIR3D`** (REQ-309, REQ-310):
report-or-set, `ON`/`OFF`/`FLIP`/`<offset>`. No undo entry, no geometry — the whole distinction from
`SECTION`, which asks the same plane the same question and answers with a polyline.

## 6. Tests

**`SectionClipTests` — 9 cases, 96 assertions.** Deliberately at state-plane coordinates, on a tilted
frame, and across a pan, because those are the three things a plausible wrong version passes without:

| case | what it pins |
|---|---|
| inactive keeps everything | the neutral vec4, which the unconditional shader write depends on |
| the UCS plane and its offset | the plane IS the UCS plane; a negative offset is a plane, not a refusal |
| FLIP | exactly one of the two halves keeps any point off the plane |
| a moved-and-turned frame | the normal is the frame's Z and the offset runs along it, under rotation |
| CPU/shader parity at the origin | `KeepsWorldPoint` and the packed vec4 are two statements of one rule |
| **survey magnitudes** | an oblique plane at E 2.196e6 / N 1.4e6, six probes either side |
| **the origin bit-identity** | the correct and naive packings are *identical* at (0,0) — the trap, asserted as such |
| **an anchor sweep** | the naive plane sits at `anchor + c`, **measured by bisection**; the correct one does not move |
| **REQ-101 resolution** | 0.002 ft steps resolved on a 2.2e6 constant — a float-computed constant quantizes to ~0.25 ft and cannot |

**Measured, not compared.** The two naive-version cases recover *where the plane actually lands* by
bisection — the instrument P7 used — rather than asserting that two answers differ. That choice came
straight from D-2026-09-09-k's lesson: **"these differ" is satisfiable by accident**, and the first
version of these two cases predicted the wrong failure direction (it expected the naive plane to clip
everything; it actually clips nothing) and would have been "fixed" by flipping an assertion. Working
out *why* it passed produced a better test than the one intended.

**Proven to bite:** replacing the rebased constant with the world one fails **4 of 9 cases and 13
assertions**.

**`headless.req336-section-clip` — 87 steps.** Every spelling and alias; every refusal with the
previous state intact; `UNDO` reaching **past** the clip to the previous edit (which only works
because the clip makes no undo entry); the solid byte-identical after clipping; the clip not
surviving a new drawing; and the **no-rebuild sweep** — `SOLIDTESSGEN`, the display-regeneration
counter, unchanged across five plane moves, a flip and an off/on. That last block is how a transcript
can assert "live" at all.

Three new driver verbs: `EXPECT SECTIONCLIP`, `SECTIONCLIPOFFSET`, `SECTIONCLIPFLIP` — needed for the
reason `EXPECT PROJECTION` exists, that `EXPECT LOG` matches the whole accumulated log and so cannot
assert a toggle's *current* value once it has reported any value.

**One defect found by reviewing the diff rather than by a test.** The offset was first parsed with
`ParseOneFloat` and then widened into the `double` field it lives in — the exact one-narrowing-point
shape ADR-054 Phase D audited out of the codebase, and invisible to every test written so far because
they all used small offsets. Parsed as a double now, and pinned by a transcript line at 200,000.01,
where the float round trip lands **0.005625 ft** away — outside REQ-101's ±0.002. **Proven to bite**:
reinstating the float fails that line and nothing else.

That failure also exposed a second, smaller problem in the verb it had just added: the message
printed both sides with `%.6g`, so a real 0.005625 ft miss read as *"is 200000, expected 200000"* — a
test appearing to fail against itself. Now `%.10g` with the difference stated.

**Full suite 1451/1451**, up from 1441.

## 7. Verification

- **build-project** — clean, Windows/MSVC/Ninja.
- **architecture-review** — one new GL-free header; no new layer, dependency or abstraction; the
  camera, the geometry pipeline, the display caches and every document type are untouched. The
  abstraction rule is satisfied without strain: `SectionClipPlane` has two concrete uses (the command
  layer states one, the renderer consumes one) and replaces no existing type.
- **code-review** — the command sits beside `ApplyProjectionValue` and follows its shape; the render
  changes follow the file's own `depthForGeometry`/`depthForOverlay` idiom.
- **dependency-audit** — none added.
- **performance-review** — four `glUniform4fv` per frame and one `glEnable`/`glDisable` per pass
  group. Nothing is re-tessellated or re-uploaded, which the transcript asserts rather than assumes.
- **testing** — 9 new unit cases (96 assertions) + 1 new transcript (87 steps); **1451/1451**.

**One verification is NOT complete, and it is the visual one.** A devshell test
(`--devshell-run req336-section-clip-viewport`) is written and committed: it builds a box, orbits,
shades it, and captures six screenshots across clip off → cut at the UCS plane → two moved planes →
flipped → off again, checking the solid is unchanged throughout. **It could not be run in this
session** — synthesized input does not reach the window in this environment, and the devshell CLI
mode produced no output or screenshots. Everything it would show about the *arithmetic* is already
measured by P7 in a real GL context using these exact shader sources; what remains unconfirmed is the
**integration** — that the uniform reaches the shader in the running app, and that the interface
survives with the clip on (the `finish_render` disable in (e)). That is a real gap and it is flagged
in the PR rather than papered over.

COMPLETION REPORT — TASK-240 — 2026-09-10
- Requirements satisfied:  REQ-336 (new, accepted); GitHub #149 acceptance 6
- Summary:                 SECTIONCLIP — a live clip plane as a per-frame shader uniform, rebased
                           onto the view anchor; GL geometry only, uncapped, both stated in the REQ
- Tests:                   9 unit cases (96 assertions) + 1 transcript (87 steps); 1451/1451
- Verification verdict:    PASS, with one gap — the GUI/devshell check is committed but unrun
- Assumptions:             none
- Architectural decisions: ADR-056, D-2026-09-10-a
- Dependencies:            none added
- Technical debt noted:    the uncapped cut and the ImGui-overlay entities are recorded as REQ-336
                           increments, each with the cost of closing it written down
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-336 + its traceability row, ADR-056, D-2026-09-10-a, this task log

## 8. What this closes

With acceptance 6 met, **all eight of #149's acceptance criteria have been delivered**:

| # | criterion | slice |
|---|---|---|
| 1 | 3D / horizontal distance, vertical difference, grade | PR #464, REQ-105 amended, TASK-235 |
| 2 | face area, surface area, volume for every primitive | PR #465, REQ-313 amended, TASK-236 |
| 3 | analytic volume, not tessellated | the #146 kernel and its `BrepTests [req313]` cases |
| 4 | centroid, primitive and Boolean | PR #466, REQ-334 + ADR-055, TASK-237 |
| 5 | sectioning, non-destructive | PR #468, REQ-335, TASK-238 |
| 6 | **section clipping, live** | **this task, REQ-336 + ADR-056** |
| 7 | validation names each fault | PR #469, REQ-313 amended, TASK-239 + D-2026-09-09-k |
| 8 | accurate at survey magnitudes | pinned per slice; this one adds the clip plane at E 2.196e6 |

Criterion **3** is met by shipped code but was never claimed by a slice of its own — it is the #146
kernel's closed-form integrals plus `Tessellation quality does not change the solid`. Criterion **8**
is pinned for solids, faces, the centroid, sections and now the clip; **`DistCommandTests` still runs
entirely at the origin**, which is a test gap rather than a defect (`CommitDistSecondPoint` is double
throughout, and its `float` locals are correct under origin-at-entry with the magnitude in the double
origin). Both are noted here so the phase's own record shows where they stand.

Moments of inertia and principal axes are **out of #149 by decision** (2026-09-09) and carried by
issue #460, which depends on this phase.
