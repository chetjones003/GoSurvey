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
