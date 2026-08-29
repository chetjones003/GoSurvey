# TASK-140 — UCS and PLAN commands (#126)

- Type:    feature
- Status:  done
- Opened:  2026-08-27
- Owner:   Workshop

Upstream issue: chetjones003/GoSurvey#126.

## 1. Authority
- Goal:         3D model space and coordinate entry (GOAL-03)
- Requirements: **REQ-154** (drafted locally, accepted D-2026-08-28-n, shipped in this PR);
                REQ-058 (accepted) already named "the active work plane (UCS)" and stored a partial
                one; REQ-059 (accepted) already takes a UCS azimuth for the ViewCube compass;
                REQ-047 ORTHO; REQ-024 dynamic input; REQ-101 coordinate tolerance; REQ-201 refusals
- Constraints:  CON-06 smallest change; no new layer; no new dependency
- Acceptance:   REQ-154's list, which is issue #126's minus the four items recorded as not met
- Owning subsystem: Commands (frame + commands + entry), Renderer (grid), UI (icon, readout),
  IO (`.gs`)

**Why a REQ was drafted rather than waited for:** the one-PR-per-issue rule says to build against a
locally drafted requirement and ship both together, so chet evaluates the requirement and the code
that tests it in one merge. REQ-154 and D-2026-08-28-n are part of this PR, not a precondition of it.

## 2. Scope
- In scope: the `ucs::Ucs` value type and its pure transform module; `UCS` with origin / 3-point /
  World / Previous / View / X / Y / Z / ZAxis / Object / Named(Save,Restore,Delete,?); `PLAN`
  Current / World / named; `UCSFOLLOW`; UCS-aware typed entry (including X,Y,Z), click entry, commit
  elevation, ORTHO, grid, UCS icon, `ID` and status readout; `.gs` persistence; per-drawing scoping
- Out of scope, **stated in REQ-154 with reasons**: camera roll (exact PLAN of a tilted UCS);
  polar tracking; per-viewport UCS/UCSFOLLOW isolation; `Object` on mesh/solid faces
- Smallest change: the UCS transform went inside the ONE existing point-parse choke point rather
  than into 38 command call sites, and into the ONE existing commit-elevation helper rather than
  into ~29 geometry-creation sites. No command implementation file was touched for entry.

## 3. Architectural boundary check
- [x] **Yes — escalated and recorded as D-2026-08-28-n before implementing.** A new pure module, new
  persisted `.gs` keys, a new value type on `AppCommandState`, and a new renderer parameter are all
  architectural under CLAUDE.md §Workshop. The four decisions (pure module; world-space storage of
  the frame; the two choke points; WCS branch byte-identical) are in the decision log.
- No new dependency. No new abstraction: `ucs::Ucs` has three present-day consumers (commands,
  renderer grid, UI icon), and it *replaces* the loose origin/normal/azimuth triple rather than
  layering over it.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|------|--------|
| 1 | Issue #126's acceptance list has ~42 boxes, 4 blocked on features that do not exist. Build the core and document the gaps, add camera roll too, split the issue, or ship the transform core alone? | 2026-08-27, before any code | Build the core; record the four gaps explicitly |
| 2 | Recent PRs changed base branch (#116 → `master`; `beta` 9 behind with nothing of its own). Which base? | 2026-08-27 | "merge with the master and push this to the beta when you make a pr" — branch from `master`, PR into `beta` |

## 5. Assumptions
```
ASSUMPTION-1: The UCS belongs to the DRAWING, not the session or the viewport.
- Because: there is one model view per tab; REQ-061's per-viewport camera was never implemented and
  multiple model-space viewports are an open scope question (requirements.md, REQ-084 note)
- Risk if wrong: if per-viewport model views are added later, the scoping has to move down a level
- Validate by: DrawingDocument save/restore + ClearCadGeometry reset; a tab switch carries nothing

ASSUMPTION-2: ELEV keeps meaning "move the work plane in Z, leave the orientation alone".
- Because: ELEV is AutoCAD's elevation half of the same idea and predates the UCS here; reverting to
  a world-parallel plane on ELEV would silently discard a rotation the user set
- Risk if wrong: a user expecting ELEV to reset the frame gets a raised rotated plane instead
- Validate by: the ELEV path in the transcript, and ApplyElevValue routing through SetActiveUcs

ASSUMPTION-3: A UCS whose basis fails its own validity check on load is DISCARDED, not repaired.
- Because: a hand-edited or truncated basis would skew every coordinate typed afterwards, invisibly;
  falling back to the WCS is the one outcome that cannot be wrong (REQ-201)
- Risk if wrong: a file with a marginally non-orthonormal frame silently loses its UCS
- Validate by: the 1e-6 tolerance in GsIo's readUcs, which is loose against float noise and tight
  against a genuinely broken basis
```

## 6. Plan
- Approach: one pure frame module; carry it into the app through the two existing choke points
  (point parse, commit elevation); everything else consumes the frame rather than re-deriving it.
- Files: `src/util/ucs.hpp` (new), `tests/UcsTests.cpp` (new), `src/ui/UcsIcon.{hpp,cpp}` (new),
  `CadCommands.{hpp,cpp}`, `CadUi.cpp`, `GsIo.cpp`, `ViewportRenderer.{hpp,cpp}`, `main.cpp`,
  `HeadlessDriver.cpp`, `CMakeLists.txt`, `spec/requirements.md`, `spec/project.md`.
- Tests: 33 Catch2 cases on the pure math (every transform, every constructor, every refusal, the
  PLAN derivation, and "changing the UCS leaves world coordinates untouched"); one headless
  transcript asserting **stored world coordinates** for geometry drawn under translated, rotated and
  tilted frames, plus Previous, Named + `.gs` round trip, PLAN-does-not-change-UCS, and refusals.
- Steps: [x] pure module + tests [x] state [x] commands [x] entry + commit Z [x] ORTHO [x] grid
  [x] icon [x] readouts [x] persistence [x] per-drawing scoping [x] transcript [x] spec + log

## 7. Workflow-specific notes

**Rebased onto `beta` and renumbered, 2026-08-28.** The work was written against `upstream/master`.
By the time it was ready, PR #128 (issue #119 — TIN surfaces, analysis, volumes, watersheds) had
merged, putting `beta` 17 commits ahead of `master` and taking the identifiers this task had claimed:

| was | is | why |
|---|---|---|
| REQ-125 | **REQ-154** | `beta`'s REQ-125 is Surface statistics |
| TASK-125 | **TASK-140** | `beta`'s TASK-125 is Issue #119 Phase 1 TIN gaps |
| D-2026-08-27-b | **D-2026-08-28-n** | `beta`'s D-2026-08-27-b accepts the TIN volume surface |

`req125-ucs-plan.txt` became `req154-ucs-plan.txt` with it. The one rebase conflict was in
`spec/requirements.md`, where both sides had appended requirements to the same region — resolved by
keeping `beta`'s REQ-124…153 intact and appending REQ-154 after them. Every code file merged
cleanly, which is the useful signal: the UCS work touches ten files that #119 also touched
(`CadCommands.cpp/.hpp`, `GsIo.cpp`, `ViewportRenderer.cpp`, `CadUi.cpp`, `main.cpp`,
`HeadlessDriver.cpp`, `CMakeLists.txt`, and both spec files) and collided in none of the code.

Care was needed renumbering `CadCommands.cpp`: `beta` legitimately references REQ-125 there for
`util/surfacestats.hpp`. That one line is untouched; the other seventeen were this task's.

A traceability-table row for the new requirement was missing and has been added — the original
`REQ-125` entry updated the requirement text but never `spec/requirements.md`'s summary table.

Full suite re-run after the rebase: **771/771 green**.

**Measured, not reasoned — the camera convention.** `ucs::PlanViewAngles` has to agree with `Camera`
on a sign, and a second derivation of the same convention is exactly how the two drift apart
(Camera.hpp: "one derivation, one convention"). The relationship was measured with a standalone
probe against the real `Camera` rather than deduced: a **positive** camera azimuth turns screen-up
**clockwise** from north, so a UCS rotated +30° counter-clockwise needs azimuth −30. The first
derivation had it backwards, and three tests caught it. At a pole the eye direction carries no
azimuth at all, so it comes from the UCS's own +Y — measured the same way, `up = (sin az, cos az, 0)`
at elevation +90 and `(−sin az, −cos az, 0)` at −90.

**Two seams that were nearly missed.**
- A blank Enter never reaches the Kind-keyed command block: a global `line.empty()` block consumes
  it first (the same trap TASK-082 hit with FEATURELINE). `UCS`'s bare Enter is meaningful at three
  prompts, so it is handled there.
- Typed UCS prompt points had to accept `X,Y,Z`. With two components every typed point lies in the
  current UCS's XY plane, so the three-point and ZAxis options could only ever produce rotations
  about the current Z — a *tilted* UCS, the whole 3D half of the feature, would have been
  mouse-only.

**`EXPECT LINEXYZ` was added to the transcript driver.** Every existing EXPECT counts entities or
matches log text. Neither can state the property a UCS must be judged on: that geometry drawn in a
rotated or tilted frame lands at the **world** position the frame implies. A count passes just as
happily when the line went somewhere else entirely, and the log echoes what was typed rather than
where it ended up.


**`2P` on the rotation-angle prompt, added 2026-08-29 from hands-on testing.** Nathan drove the
branch and asked for it directly: *"to find an angle for your new ucs you should be able to type it
in like we have now and define a new angle by picking two points."* The case is the everyday one —
a surveyor squaring a frame to a lot line or a building face knows the LINE, not its bearing, and
making them read a bearing off the drawing and type it back is arithmetic the app does exactly and
they can only do approximately.

`2P` was chosen over inventing a keyword because LINE and POLYLINE already use it for "define this
direction by picking" (`A / 2P bearing lock`), so this is one convention rather than a second.

The angle is measured in **the plane that rotation actually spins**, using the same axis pair and the
same order as `detail::SpinPair` — Z measures from +X toward +Y, X from +Y toward +Z, Y from +Z toward
+X. Deriving it from the same pair is what stops the measurement and the rotation disagreeing about
which way is positive; a second derivation is exactly how they would drift, which is the same trap
`PlanViewAngles` was written to avoid.

Both new phases route typed points and viewport clicks through one commit helper
(`CommitUcsRotationFromTwoPoints`), and both the typed and measured angles reach the frame through
one `ApplyUcsRotation`, so the three-way axis branch exists once.

The derived angle is **echoed to the log**. The user picked two points and never saw a number; showing
the one that was used is what lets a mis-pick be noticed, and it is the number they would type to
repeat the frame later.

Two picks that define no angle in the plane — coincident, or perpendicular to it, e.g. straight up
under `UCS Z` — are refused and the prompt **stands at the second point** rather than dropping the
whole command, so the pick can simply be retaken (REQ-201). The out-of-plane test scales with the
picks' own length rather than a fixed distance, so a mile-long pair that is barely off-plane is
refused for the same reason a short one is.

**Live preview, polar cursor input, and the frame selector — added 2026-08-29 from hands-on
testing.** Nathan drove the branch, sent AutoCAD screenshots of the three, and asked for them.

**The one thing that needed a decision, not a guess.** REQ-024 is accepted and says a point prompt
shows a **single** `x,y` field — and its Revisions record that this deliberately *replaced* an
earlier two-box design on 2026-06-19, "so relative/bearing/distance entry works in the same field".
The screenshots show AutoCAD's two-box polar pair. Building it silently would have reversed a
recorded decision, so it was raised: the answer was a **UCS-only exception**, now stated in REQ-024
itself, leaving every other prompt on the single field. Narrow on purpose.

**The polar pair is real syntax, not a widget.** The boxes assemble `@<distance><<angle>` and submit
it through `ProcessCommandLineSubmit`, and `ParseUcsPolarPoint` accepts the same string typed. That
came from the screenshots themselves: AutoCAD's command line echoes `@33.2311<17` after the pick. So
the mouse path and the keyboard path are one path, and a user can read what they just did and repeat
it — rather than a UI-only affordance with no textual equivalent.

**UCS was missing from the SECOND of the two lists a point-picking command must appear in.**
Yesterday's fix added `K::Ucs` to `ViewportClickRouteFor`; it was also absent from
`CommandExpectsPointEntry`, so the cursor prompt never appeared at all. `CommandExpectsPointEntry`
carries a comment about SURFELEV hitting exactly this — "the same pre-existing TASK-055 gap, in the
second of the two lists" — which is now three commands that have fallen into it. Worth a guard that
makes the pair impossible to half-fill.

**A bug I introduced and caught in the GUI pass.** The frame selector first placed its button with
`SetCursorScreenPos`, reaching outside the viewport window's content region. ImGui answered with a
red "code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries" banner painted
across the drawing. Rebuilt as its own overlay window — the shape the cursor's dynamic input beside
it already uses. Its popup is also pinned right-aligned to the button, because the button tracks the
ViewCube against the viewport's right edge and a popup growing rightward ran under the docked panel.

Neither of those two is reachable by any transcript — they are placement, and this repo has no
render-comparison harness (DEBT-3). Both were found by driving the GUI and reading the capture, which
is the second time in two days that pass has caught something the suite could not.

**Named views (REQ-106) added 2026-08-29 — a SECOND requirement in this PR, at the user's explicit
direction.** This is worth recording plainly because it breaks the one-PR-per-issue rule the user set
on 2026-08-26. It was raised before starting: named views are REQ-106, catalogued 2026-08-23, not
issue #126's REQ-154, and the recommendation was a separate branch and PR. The user chose to add it
here anyway ("just add it to 129 dont worry about making it a seperate pr"). So issue #126 closes
carrying work it never asked for, and chetjones003 reviews two features in one thread. Recorded, not
argued.

**What was built, and what deliberately was not.** REQ-106 has four parts: ZOOM PREVIOUS, named
views, a VIEW command/dialog, and isometric presets. Three are built. **ZOOM PREVIOUS is not** — it
needs a view history that nothing in the app keeps, and it was neither asked for nor implied by the
screenshots. REQ-106's Status is therefore **partially delivered**, not accepted: closing it would
claim a view-history feature that does not exist, which is precisely the REQ-064 failure mode this
same PR already had to escalate (§5a of TASK-141).

**A view is the camera's inputs, not a matrix.** `NamedView` stores pan/zoom/azimuth/elevation — the
same four fields `CadViewCamera` builds a `Camera` from — plus the UCS. Storing a derived matrix
would let a saved view mean something the live view with the same numbers would not.

**The UCS travels with the view**, which is REQ-106's own acceptance wording ("camera
position/target/UCS exactly") and the half worth testing hardest: a restore that returns the camera
but leaves the frame behind looks correct and silently changes what the user's next typed coordinate
means. The transcript pins it by drawing in the restored frame and asserting the world position.

**Which view is "current" is DERIVED, never stored.** The first cut kept an `activeViewName` string,
and the GUI pass caught it lying: the fixture saved a view, changed the UCS, and the ribbon still
claimed the old name. A remembered name goes stale the moment the user pans. `CurrentNamedView`
compares the live camera and frame against each saved view instead, so `Unsaved View` appears exactly
when it is true — and that phrase is the whole point, being the warning that what is on screen would
be lost.

**One convention, not two.** `VIEW`'s Save / Restore / Delete / `?` options, its case-insensitive
matching, and its overwrite-on-resave all mirror `UCS Named` deliberately. The preset angles come
from the ViewCube's own face table and `kIsometricElevationDeg` rather than a second copy — the combo
and the cube must agree about where "Front" is, and a duplicated table is how they would stop
agreeing. The View Manager dialog calls `ProcessViewCommandLine` for every action it performs, so the
dialog cannot drift from the command; that is the exact DIMSTY-vs-UNITS failure this session's
probe 36 found in the dimension code.

**A self-inflicted incident worth recording.** A `sed` whose line-address variable came back empty
degraded to "append after EVERY line" and inserted the same registry entry 28,002 times into
`CadCommands.cpp`. `git diff --stat` showed insertions only, which is what made it recoverable: the
junk line was filtered out with `grep -vF` and the 152 lines of real work underneath verified present
by name. **Compute the anchor line, assert it is non-empty, and use `awk` to rewrite rather than
`sed -i` with an interpolated address** — an empty address is silently catastrophic in a way an empty
pattern is not.
## 7b. Second GUI pass, 2026-08-29 — the Coordinates ribbon panel, and a command option that could never succeed

**What was added.** A **Coordinates** panel on the View tab, beside Named Views — AutoCAD's own name
and position for it. Six buttons in three two-button columns (UCS / 3-Point, World / Previous,
Object / Rotate Z) and a frame selector labelled "Coordinate system". Until this existed, every UCS
option was reachable only by typing, which is a poor showing for a command whose whole job is to be
switched in and out of constantly.

Two things keep the panel from drifting from the command it names:

- Every button submits its command **through the command line's own parser**
  (`ProcessCommandLineSubmitStr`, a scratch-buffer wrapper over `ProcessCommandLineSubmit`) rather
  than calling a commit helper directly. A button and its documented `Command bar:` line cannot
  diverge, because they are the same string going through the same code. This is the DIMSTY-vs-UNITS
  failure probe 36 found, designed out rather than watched for.
- The panel's frame label and the ViewCube dropdown's both come from one `CadUcsFrameLabel`. Two
  widgets naming the same frame must not be able to name it differently.

**What the GUI pass then found: `UCS Object` had never worked, on any drawing, at any zoom.**

Clicking a line at the `Select object to align UCS with:` prompt always answered
`UCS Object - no object found at that point.` The pick was verified against a control: ordinary
selection at the *same pixel* selected the line, so the click and its coordinates were fine.

The cause is one argument. `UcsFromObjectPick` called

```cpp
PickClosestCadEntity(st, px, py, tol, &hit, nullptr)   // no out-distance wanted
```

and `PickClosestCadEntity` opens with `if (!out || !outDistSq) return false;` — it rejected the null
**before it looked at a single entity**. Every other caller in the file passes a `float d2` it does
not always use, so this contract was never load-bearing until UCS became the first caller to decline
it. The option was dead on arrival: no tolerance, no zoom level and no drawing could make it succeed.

**Fixed in both directions.** The caller passes a real `float`, and `outDistSq` is now genuinely
optional — null is accepted and written to a local — so the next caller that only wants the entity
cannot fall into the same hole. `CadCommands.hpp` documents it as optional.

**Why nothing caught it.** `req154-ucs-plan.txt` had **no `UCS Object` steps at all**: the option
went out with the command's own transcript never exercising it once. Six steps have been added, and
they assert the resulting **geometry**, not the log — a line drawn 10 units along the new frame's X
must land 10 units along the line the frame was aligned to (`EXPECT LINEXYZ 1 0 0 0 8.9443 4.4721 0`).
A log assertion would only prove the message changed. Also covered: clicking the far end reverses the
frame, and a miss still reports honestly and leaves the command in its pick phase.

**Red-before, measured, not assumed.** With the `nullptr` restored and rebuilt, the transcript fails
at step 292 / line 431 on exactly the new assertion; with the fix, 310 steps pass. The test catches
the defect it was written for.

**The self-inflicted incident, part two.** The `sed` lesson recorded above repeated itself with
`perl`: `s/\Qif (!out)\E$/.../ ` matched **ten** functions, not the one intended, and nine unrelated
guards were rewritten. The compiler caught it immediately (`'outDistSq': undeclared identifier` ×9)
and the repair was mechanical — but the general rule stands and is now proven twice: **for a pattern
that could plausibly appear more than once, locate the line, assert the occurrence count, and rewrite
by index.** The repair script that fixed it does exactly that, and is the shape to reach for first.

## 8. Technical debt
```
DEBT-1: PLAN of a tilted UCS sets the view direction but not the in-plane rotation.
- Constraint: Camera stores azimuth + elevation with no roll (ADR-025 (c), deliberate — a free
  eye/up pair flips at the pole)
- Removal condition: add rollDeg to Camera (view matrix, ScreenRay, WorldToScreen, FromViewRotation,
  ViewCube, PDF plot, per-tab + .gs persistence) — an ADR, not a fix
- Visible: the command says so when it happens, rather than looking correct and being wrong
- Follow-up: drafted as a ready-to-file issue body in the `ready-to-file` notes folder; not yet filed

DEBT-2: `Object` covers lines, arcs, circles, ellipses and text; not mesh or solid faces.
- Constraint: no face-level picking — PickClosestCadEntity resolves a mesh as one object with no
  face identity; solids/surfaces as editable entities are issue #120
- Removal condition: face picking exists
- Visible: refused with a stated reason naming the limitation

DEBT-3: The UCS icon and the UCS-following grid have no automated coverage.
- Constraint: both are rendered output (ImGui draw list / GL), and this repo has no headless
  equivalent — the same reasoning as REQ-121 DEBT-1 and REQ-306
- Removal condition: a render-comparison harness exists
- Mitigated by: the math both consume IS covered (ucs::Ucs transforms, UcsTests), so what is
- GUI pass 2026-08-28 found one real defect this constraint had hidden: in the **World** frame the
  icon's X arm, its label and the "W" marker were drawn underneath the floating command bar (REQ-040),
  which is a separate window painted over the viewport and shares this corner. Only World shows it —
  any rotation lifts both arms clear, which is why every rotated capture looked correct. Fixed by
  recording the bar's laid-out top edge (`cmdBarTopYPx`) and clamping the icon's root above it.
  untested is the drawing call, not the geometry; and by the user's own GUI pass
```

## 10. Verification result

Self-run against `verification/skills/`:

- **build-project** — clean full build, MSVC/Ninja release, no new warnings.
- **architecture-review** — one new pure module in `util/` (the ADR-025 (d) precedent it names);
  dependencies flow downward only (Commands → Renderer, UI → Commands, IO → Commands); no new layer,
  global, dependency or abstraction; the one architectural decision set is recorded as
  D-2026-08-28-n **before** implementation, per the escalation rule.
- **code-review** — the WCS branch is the original code path at every seam, which is what bounds the
  blast radius of a change touching entry, commit, ORTHO, grid and IO.
- **dependency-audit** — none added.
- **performance-review** — the frame is four `Vec3`s passed by value; the transform is three dot
  products; the grid path is unchanged under the WCS and generates the same vertex count under a UCS.
- **testing** — 33 new Catch2 cases (188 assertions) + 1 new transcript (157 steps). Full suite
  **771/771 green** on `beta`, including every pre-existing test, on the first run after the state change (702/702 before the rebase onto `beta`, which brought 69 more).

COMPLETION REPORT — TASK-140 — 2026-08-27
- Requirements satisfied:  REQ-154 (Acceptance met: yes — the four items REQ-154 excludes are named
                           in it, with reasons, and were raised with the user before work started)
- Summary:                 UCS became a real coordinate-system service with one authoritative pure
                           transform module; PLAN orients the view without touching it; entry,
                           ORTHO, grid, readouts, persistence and the viewport icon all follow it.
- Tests:                   UcsTests.cpp (33 cases, happy + every refusal); req154-ucs-plan.txt
                           (157 steps, asserting stored world coordinates); 771/771 ctest green on `beta`
- Verification verdict:    PASS (findings resolved: none outstanding)
- Assumptions:             ASSUMPTION-1..3 documented above, all validated in code
- Architectural decisions: escalated and recorded as D-2026-08-28-n before implementing
- Dependencies:            none added
- Technical debt noted:    DEBT-1..3 above, each with a removal condition
- Build:                   reproducible, clean on Windows/MSVC
- Docs updated:            spec/requirements.md (REQ-154), spec/project.md (D-2026-08-28-n),
                           this task log
