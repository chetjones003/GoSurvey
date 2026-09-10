# TASK-248 — SECTIONCLIP: the live section clip, and the frame the plane has to be stated in

- Type:    feat (new requirement + new ADR)
- Status:  review
- Opened:  2026-09-10
- Owner:   Workshop
- GitHub:  #149 acceptance 6 (3D Phase 6 — Analysis), step 6 of 6 — **the last criterion of the phase**.

## 1. Authority

- **REQ-337** (new, accepted 2026-09-10, D-2026-09-10-e) — the requirement this delivers.
- **ADR-057** (new) — the six decisions behind it. A new render capability needs an ADR and an
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
one criterion of one phase, and REQ-337 + ADR-057 (e) now carry the cost of closing it in writing.

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

**`headless.req337-section-clip` — 87 steps.** Every spelling and alias; every refusal with the
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

**The GUI check RAN, and it found a bug.** `--devshell-run req337-section-clip-viewport` builds a
box, orbits, shades it, and captures the viewport across clip off → cut at the UCS plane → two moved
planes → flipped → off again, checking the solid is unchanged throughout. Result: **Success**, six
captures, five of them distinct and `off` byte-identical to `off-again`.

Two things had to be fixed to get there, and both are worth recording.

**(1) The devshell is compiled out of a Release build.** `CMakeLists.txt:148` forces
`GOSURVEY_DEVELOPER_SHELL` OFF for `CMAKE_BUILD_TYPE=Release` (REQ-161), so `--devshell-run` is
parsed by nothing and the app simply launches as normal and waits. It looks exactly like a hang, and
it was misread as one. **The devshell needs `build/debug`.** Written down because the failure gives
no diagnostic at all — no message, no log, no non-zero exit.

**(2) `DevShell_RequestScreenshot` captures pure black here, and it did so silently.** It reads the
window's `GL_FRONT` buffer, which returns black on a window the compositor is not presenting — the
normal case for an automated run. The first six captures came back **byte-identical**, which reads
exactly like "the clip does nothing" and would have been a plausible, wrong conclusion; only
checking the image itself showed it was a black frame. Added
`DevShell_RequestViewportCapture` / `DevShell_ServiceViewportCapture`, which read the RENDERER's own
framebuffer through the existing `CaptureThumbnailBmp` (REQ-308) and so do not depend on the window
being composited. The existing screenshot hook is left in place, with its limitation now documented
at its declaration.

**The bug the GUI caught, which every other test missed: `SECTIONCLIP 0` turned the clip OFF.**
`PERSPECTIVE` and `CROSSHAIR3D` both accept `1`/`0` as ON/OFF and this command was written the same
way — but its main argument is a **distance**, so `0`, the obvious way to ask for a cut exactly at
the UCS plane, was read as "off". The numeric aliases are gone: for a command that takes a number,
digits mean the number.

**It reached the GUI because the transcript had the same blind spot.** The liveness block already
typed `CMD SECTIONCLIP 0` and then asserted only `EXPECT SOLIDTESSGEN 1` — which is trivially true of
a command that did nothing at all. A "nothing was rebuilt" assertion cannot tell a working feature
from an inert one. Four lines now pin `0` and `1` as offsets, and the liveness block keeps its
counter check.

What the six captures show, in order: the whole box; **only the bottom face** surviving a cut at
offset 0 (the plane keeps `z <= c`, so the face lying *on* it remains — the plane is exactly where it
was asked for); a third of the box at offset 4 and two thirds at offset 8, both **open at the top**,
which is ADR-057 (f)'s uncapped cut seen directly; the **complement** slab under FLIP; and the whole
box again. Parked in `Notes for claude/issue149-analysis/req336-clip-evidence/`.

**One thing is still correct-by-construction rather than observed**: that the interface survives with
the clip on — the unconditional `glDisable(GL_CLIP_DISTANCE0)` at `finish_render` in (e). The
captures are of the viewport framebuffer, so they do not contain the UI, and the test engine drives
ImGui through its item registry rather than through pixels, so it would not notice either. The guard
is a one-line read against a clear rule (an unwritten `gl_ClipDistance` under an enabled clip plane
is undefined), but it has not been *seen* working, and it is named here rather than counted as
verified.

COMPLETION REPORT — TASK-248 — 2026-09-10
- Requirements satisfied:  REQ-337 (new, accepted); GitHub #149 acceptance 6
- Summary:                 SECTIONCLIP — a live clip plane as a per-frame shader uniform, rebased
                           onto the view anchor; GL geometry only, uncapped, both stated in the REQ
- Tests:                   9 unit cases (96 assertions) + 1 transcript (101 steps) + 1 devshell GUI test; 1451/1451
- Verification verdict:    PASS — devshell GUI run green, six viewport captures as evidence
- Assumptions:             none
- Architectural decisions: ADR-057, D-2026-09-10-e
- Dependencies:            none added
- Technical debt noted:    the uncapped cut and the ImGui-overlay entities are recorded as REQ-337
                           increments, each with the cost of closing it written down
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            REQ-337 + its traceability row, ADR-057, D-2026-09-10-e, this task log

## 8. What this closes

With acceptance 6 met, **all eight of #149's acceptance criteria have been delivered**:

| # | criterion | slice |
|---|---|---|
| 1 | 3D / horizontal distance, vertical difference, grade | PR #464, REQ-105 amended, TASK-235 |
| 2 | face area, surface area, volume for every primitive | PR #465, REQ-313 amended, TASK-236 |
| 3 | analytic volume, not tessellated | the #146 kernel and its `BrepTests [req313]` cases |
| 4 | centroid, primitive and Boolean | PR #466, REQ-334 + ADR-055, TASK-237 |
| 5 | sectioning, non-destructive | PR #468, REQ-335, TASK-238 |
| 6 | **section clipping, live** | **this task, REQ-337 + ADR-057** |
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

---

## 9. Increment: the keywords are CLICKABLE (2026-09-10, user request)

Asked for after the first GUI pass: make `ON`, `OFF` and `FLIP` selectable from the command line
rather than typed. Folded into this task rather than filed as a follow-up, because REQ-337 has not
merged yet — there is no accepted requirement to amend, only a draft to finish.

**The capability already existed and was not written for this.** `cmdbar::ParsePromptSegments` +
`LayoutCommandHint` (REQ-040) already turn a bracketed keyword into a clickable link, which is how
`[A]`, `[2P]` and `[CLOSE]` work in LINE and POLYLINE. Checking for that first is what kept this to
a hint string and a command state instead of a new UI mechanism.

**Why it needed a command STATE and not just brackets.** A link submits its own keyword as the next
line of input. Nothing can consume that unless a command is waiting for it — a one-shot `SECTIONCLIP`
would have rendered three links submitting `on`, `off` and `flip` into the top-level dispatcher,
where they mean nothing. So bare `SECTIONCLIP` now reports and holds `Kind::SectionClip` open, and
the same `ApplySectionClipValue` serves the typed and the clicked path. Five touchpoints, all
following the shape `Kind::TrimState` already sets: the enum value, `StartSectionClipCommand`, the
ESC branch, the input consumer, and the hint in `CommandInputHint`.

**The keywords are written all-caps deliberately.** `VariantShortcut` takes a variant's leading
uppercase run, so `[ON/OFF/FLIP]` submits `on`, `off` and `flip` in full — exactly the tokens the
parser already accepted. The codebase's mixed-case convention (`DElta`, `DYnamic`) would submit `of`
and `fl`, which it does not.

**A bare Enter is handled in the global `line.empty()` block, not in the command's own branch.** That
block consumes every blank line before the per-command branches run, and each prompting command that
wants a meaningful Enter handles it there with a comment saying why. The command's own branch
therefore carries no empty check — a dead one that looked live would invite the next reader to
maintain two answers to one question. (`Kind::TrimState`'s branch has exactly that dead check.)

### Verified by clicking, not by screenshot

`--devshell-run req337-section-clip-links` opens the prompt and **clicks each of the three links**,
asserting the state changed and the prompt closed. **Success.** That covers the half a transcript
structurally cannot: typing `on` proves the receiving end works and says nothing about whether a link
exists to click. A screenshot could not have shown it either — the links are drawn in the command
bar, which is ImGui, not in the viewport framebuffer the other GUI test captures.

**Two things had to be found out rather than assumed to get there:**

- The links are **not addressable by path**. `ctx->ItemClick("ON")` fails with *"Unable to locate item
  '##CommandBarFloat/ON'"* — but a `GatherItems` dump of that same window lists `ON`, `OFF` and
  `FLIP` plainly. They are real items sitting inside the bar's own ID scope rather than at the window
  root. `ClickCommandBarLink` gathers and matches the label, then clicks by ID. Worth keeping: the
  path failure looks exactly like "the feature is missing", and it is not.
- Dumping the item list was what settled it. Two rounds of reading the draw code had produced only
  plausible theories (a stale window, a clipped item, the wrong label); one dump answered it.

### Also in this increment

- **`EXPECT ACTIVE <KIND>`**, a new transcript verb reading `AppCommandState::KindName`. General
  rather than specific to this command: any transcript driving a prompting command needs to assert
  the prompt opened and later closed, and neither is visible in the log — a command that never
  prompted and one that prompted and finished look identical from outside. `Kind::None` has no case
  in `KindName` and returns an empty string; that is mapped to `NONE` in the driver rather than in
  production, since the app's own reporting should not change to suit a test verb.
- The transcript gains the prompt cases: each keyword answered as the link submits it, an offset
  answer, bare Enter, ESC, and a bad answer leaving the prompt open. **143 steps.**
- A transcript trap worth recording: the new block left `flip` ON, and every later `EXPECT LOG` in
  the file then failed because the report reads "..., flipped" — a failure with nothing to do with
  what those lines test. The reset between sections now restores **all three** fields, not just the
  toggle.

Full suite **1452/1452**; both devshell GUI tests green.

---

## 10. Increment: the clip plane is VISIBLE (2026-09-10, user request)

Asked for after §8's repro: *"make it so that the plane is visible to the user once it is made."*
This is the first of the two things §8 identified as missing, and the one that turns the feature
from working-but-invisible into usable.

**The problem it fixes, restated as a measurement.** In the default plan view a level cut removes
the top of a solid and leaves its outline in exactly the same screen position — the before and after
captures were **byte-identical**. The clip was correct and had nothing to show for it. That is what
"it doesn't work on a box" was.

**What it draws.** A rectangle lying on the clip plane: a translucent blue fill so it reads as a
surface, and a solid outline so its edges are still legible at a grazing angle where the fill is
nearly invisible. Sized to the solids' combined bounds with a 15% margin, so its edges stand clear
of the geometry rather than coinciding with it.

**Three decisions inside it worth keeping:**

- **Drawn UNCLIPPED, in the overlay pass.** The rectangle lies exactly on the clip plane, so a
  clipped copy of it would be cut by itself — half kept, half dropped, at the driver's discretion —
  and at offset 0 it would z-fight with the geometry it exists to explain.
- **Built in the plane's OWN axes, not world X/Y**, so it stays a rectangle on the plane under any
  orientation. The in-plane basis takes its helper axis from whichever world axis is *least* aligned
  with the normal: a fixed helper collapses exactly when the plane faces along it, which for a level
  cut — normal `+Z`, the most common case there is — is every time.
- **Sized by the caller, drawn by the renderer.** `main.cpp` knows how big the drawing is; the
  renderer is handed four corners. A drawing with no solids still gets a plane sized around the UCS
  origin, because an indicator that appeared only once you owned a solid would be missing exactly
  when someone is working out what the command does.

**Tests: 6 new cases** in `SectionClipTests` (15 total, 163 assertions) — the corners lie ON the
plane at three offsets on a moved-and-turned frame; the quad is a true rectangle (opposite sides
equal, adjacent perpendicular); it covers the model; a **level cut** still produces a usable
rectangle, which is both the case that needed the indicator and the one that breaks a naive basis; a
**flat drawing** (zero Z span) still gets a drawable rectangle rather than a zero-width sliver; and
the corners hold on the plane to REQ-101 at survey magnitudes.

**A test bug worth recording, because it looked like a code bug.** "It covers the model" was first
written as *edge 0 is longer than the model's X span* — and failed at 36.4 > 40. The rectangle was
right: for a level cut the in-plane axes come out as `(-Y, +X)`, so edge 0 runs along **Y** and was
being compared against the model's **X** extent. Rewritten to the claim that is actually meant and is
basis-independent: every one of the model box's eight corners projects **inside** the rectangle's own
two edge directions. Same lesson as the naive-packing cases in §6 — an assertion that assumes a frame
tests the frame, not the thing.

Full suite **1458/1458**. Visual evidence in
`Notes for claude/issue149-analysis/req336-clip-evidence/i-*.png`: the plan view that used to show
nothing, the orbited wireframe that used to look like dangling lines, and the shaded view.

**Still open, and now the only thing on §8's list:** the cut is not capped. The indicator covers the
opening well enough that a clipped solid reads as sectioned rather than broken, but it is the PLANE
being drawn, not the cut face — a solid whose cut is genuinely filled is REQ-337 increment 2, and
`brep::SectionLoop` (REQ-335) is still the geometry that would do it.

---

## 11. REQ-335 increment 2: SECTION prompts instead of refusing (2026-09-10)

Reported from the real app: *"it gets to the select-an-object stage, and then when selecting the
object it takes me out of the section and just selects the object by itself."*

**The report was exact, and the diagnosis is one line.** Increment 1's `SECTION` had no phases. It
read the current selection, and with nothing selected it printed

    SECTION — select one or more solids first.

and **ended**. That text reads as a prompt while the command behaves as a refusal, so the next click
lands with nothing running and does the only thing a click does then: selects the solid.

**Reproduced before touching anything** — `CMD SECTION` followed by `EXPECT ACTIVE NONE` **passed**,
which is the whole bug in two lines.

### The fix, and why it is the shape it is

`SECTION` gains SLICE's phase machine, because it is the same gesture: choose solids, then define a
plane by three points. Not a new pattern — SLICE has asked in that order since REQ-314, and AutoCAD's
own SECTION asks in that order too, which is what the screenshots in the request show. Five entry
points mirroring SLICE's exactly (`StartSectionCommand`, `CancelSectionCommand`,
`CadSectionPromptText`, `HandleSectionTextInput`, `SubmitSectionViewportPick`) plus the click routing,
the Enter hook, the ESC branch and the prompt hint.

**Nothing that shipped was taken away.** The sectioning core was split out as
`CadSectionSolidsByPlane`, which takes any plane; `[UCS]` at the first-point prompt feeds it the
active work plane, which is increment 1's exact behaviour one keystroke away. It is bracketed, so it
is also clickable — the mechanism §9 wired up for SECTIONCLIP paying for itself immediately.

**The behaviour change that needed the existing transcript updated:** pick-first used to mean
"section by the UCS plane now"; it now means "go straight to the plane prompt". `req335-section.txt`
gains `CMD UCS` after each `CMD SECTION`, with a note saying why. That is a real change to a shipped
command and it is deliberate: a command that silently chooses a plane cannot be given a different one,
and the request was specifically for the plane to be asked about.

### Tests

`req335-section.txt` grows from 35 to **84 steps**, and the new cases are the report itself:

| case | what it pins |
|---|---|
| `SECTION` then `EXPECT ACTIVE SECTION` | **the bug**: the command is still running after it asks |
| select WHILE running, then `EXPECT ACTIVE SECTION` | the click selects and does **not** end the command |
| Enter → "first point on the section plane" | the selection is confirmed rather than re-asked |
| three points → one polyline, solid untouched | the plane actually gets used |
| Enter with nothing selected | prompt stays open, does not fall through to idle |
| three **collinear** points | refused by name, command open at the third point, and a different third point still commits |
| pick-first | goes straight to the plane |
| ESC at each step | cancels, draws nothing |

Full suite **1458/1458**.

**Still not done, and now explicitly the next thing:** the seven plane-definition keywords AutoCAD
offers (`Object/Zaxis/View/XY/YZ/ZX/3points`). This delivers `3points` — their default and the only
one the screenshots exercise — plus `UCS`, which is ours. The rest are a menu on top of a working
command rather than a change to it.

---

## 12. The selection prompt had nowhere for its clicks to go (2026-09-10)

Reported immediately after §11 shipped: *"on my end there is no object select for the section
command"* — with a screenshot showing the prompt drawn correctly, *"SECTION — select solids, Enter
when done. ESC cancels."*, and clicking the box doing nothing.

**§11 fixed half the problem.** The command's state machine gained a `SelectSolids` phase and its
prompt appeared — but `ViewportPickPolicy.hpp` decides what a click in the model viewport MEANS, and
it had no case for `K::Section`. So the click routed to `Ignore` and was discarded. A prompt that
asks for a selection while every click is thrown away is indistinguishable, from the user's side,
from the refusal it replaced.

**This file's own header describes the bug it just suffered.** `ViewportPickPolicyTests.cpp` opens
with: *"A command missing from that whitelist did not error, did not log, and did not draw — it
silently discarded every click and appeared to hang on its first prompt. It happened to RECT. Then
to FEATURELINE. Then, at once, to all five of REQ-103's MIRROR, LENGTHEN, EXTEND, BREAK and
STRETCH."* SECTION is the sixth. The two guards that file names are an exhaustive `switch` with no
`default:` and a list of every pick-driven command — the switch caught nothing because `K::Section`
compiles fine when the case returns from the enclosing function's fallthrough, and **the list was
never extended**.

Fixed in two places, both of which SLICE already occupied one line above:

- `ViewportClickRouteFor` gains a `K::Section` case: `SelectSolids` → `SelectionAccumulate`, the
  three point phases → `SnappedPointPick`.
- `ViewportIsObjectSelectionStep` gains SECTION's selection phase. Omitting it is the ALIGN accident
  recorded in that function's own comment — a selection step whose fence corners come from SNAPPED
  coordinates while its siblings use unsnapped ones.

**Tests.** `K::Section` added to *"Every pick-driven command is routed by the model-space viewport"*
— the list whose omission caused this — plus a dedicated case asserting the route for all four
phases and the selection-step predicate. **Proven to bite:** routing `SelectSolids` to `Ignore`
fails it.

**The lesson worth keeping, and it is not "add SECTION to the list".** A transcript cannot catch
this class of bug at all: the driver's `PICK` verb calls `SubmitViewportPick` directly and never
touches the routing layer — which is exactly why `req337-section-clip.txt` and the extended
`req335-section.txt` were both green while the command was unusable. **A command with a selection
phase needs a `ViewportPickPolicyTests` entry, not just a transcript.** The transcripts test what
the command does with input; only that file tests whether input reaches it.

Full suite **1470/1470**.
