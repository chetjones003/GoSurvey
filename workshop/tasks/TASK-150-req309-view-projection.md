# TASK-150 — REQ-309 selectable view projection (orthographic / perspective)

GitHub issue: #144 (Phase 1 of #120)

## Authority

- **REQ-309** — Selectable view projection: orthographic and perspective. Status: accepted.
- **D-2026-08-31-a** — projection as a per-drawing view property; perspective made reachable.
- Related, not reopened: REQ-058 (the camera and its maths), REQ-064 (`VS`, the command shape
  copied here), REQ-106 (named views), REQ-101 (tolerance), REQ-201 (safe refusal).

### Acceptance conditions restated

1. `PERSPECTIVE` reports with no argument, sets with one, refuses an unrecognised value unchanged.
2. `FOV` reports and sets; a non-finite or out-of-range value is refused unchanged.
3. Ortho → perspective → ortho leaves stored coordinates untouched and restores the appearance.
4. `WorldToScreen` → `ScreenRay` round-trips within REQ-101 under perspective.
5. Object snapping resolves correctly under perspective within REQ-101.
6. Projection and FOV survive `.gs` save/reopen for the model viewport.
7. A named view saved in perspective restores in perspective, with its FOV.
8. A legacy `.gs` with neither field loads orthographic at FOV 45.
9. The REQ-100 frame budget is met while orbiting in perspective.

## Architectural-boundary check

| Question | Answer |
|---|---|
| New abstraction? | **No.** `Camera::Projection` already exists; this selects between its two values. |
| New dependency? | **No.** |
| Ownership change? | **No.** Projection joins the per-drawing view state that already owns azimuth/elevation. |
| Global state? | **No.** Per-drawing, like every other view field. |
| Public API / data format change? | **Yes** — two additive `.gs` fields, model viewport and named views. Escalated and settled by D-2026-08-31-a before implementation. |
| Algorithm swap? | **No.** The perspective branches of `WorldToScreen`/`ScreenRay` are used as written. |
| Crosses an architecture §11 invariant? | **No.** `Camera` stays GL-free (ADR-002); no `gl*` call moves. |

## Plan

### Files to touch

| File | Change |
|---|---|
| `src/commands/CadCommands.hpp` | `viewportProjection` + `viewportFovDeg` on `AppCommandState` and `DrawingDocument`; same two on `NamedView`; `CadViewCamera` threads both into the `Camera`. |
| `src/commands/CadCommands.cpp` | Doc save/restore sync (the two existing blocks that copy the azimuth/elevation pair); `PERSPECTIVE` + `FOV` dispatch beside `VS`; `ApplyProjectionValue` / `ApplyFovValue` shared by prompt and inline forms; named-view capture/compare/restore. |
| `src/io/GsIo.cpp` | Write/read both fields for the model viewport and for each named view; absent ⇒ orthographic / 45. |
| `src/ui/CadUi.cpp` | View-ribbon control beside the visual-style combo. |
| `tests/CameraTests.cpp` | `[req309]` cases — perspective projection round-trip, ortho untouched. |
| `tests/headless/transcripts/req309-perspective-projection.txt` | Command behaviour, refusals, `.gs` round-trip. |

### Order of work

1. State fields + `CadViewCamera` threading (nothing observable yet).
2. Doc save/restore sync — a field added without this silently resets on tab switch.
3. Commands + refusal paths.
4. `.gs` persistence, model viewport then named views.
5. UI control.
6. Tests: `CameraTests` first (pure, fast), then the transcript.
7. `BENCH` under perspective for acceptance 9.

### Test approach

- **Happy path:** set perspective, confirm the camera reports it; project a point and cast a ray back to it.
- **Failure mode:** `PERSPECTIVE NONSENSE` and `FOV 0` / `FOV nan` — each refused, value unchanged (REQ-201).
- **Regression:** orthographic behaviour bit-identical; a legacy `.gs` loads orthographic.
- Per the transcript traps note: `ESC` after any persistent command, and negative-test each new assertion before trusting it.

## Assumptions

- **ASSUMPTION-1** — FOV is clamped to a sane open interval rather than accepting any finite value.
  *Because:* a FOV at or beyond 180° produces a degenerate or inverted projection, and `ScreenRay`
  divides by `tan(fov/2)`. *Risk if wrong:* a legal-but-useless view. *Validate by:* the refusal
  test; revisit if a user asks for a wider angle.
- **ASSUMPTION-2** — Switching projection does not re-fit the view. *Because:* REQ-309 says
  switching and switching back restores the prior appearance, which a re-fit would break.
  *Risk if wrong:* a perspective view may open at an awkward distance. *Validate by:* the GUI pass.

## Open items

- Paper-space projection is out of scope (REQ-061's per-viewport camera does not exist). Recorded
  in REQ-309's revisions as a distinct gap; not filed as an issue by this task.

## Progress

- 2026-08-31 — Authority recorded (REQ-309, D-2026-08-31-a), plan written. Implementation starting.

## Verification evidence

Suite: **851/851 ctest green** (baseline at `ef9cf02` was 844; +7 new).

| Acceptance | Status | Evidence |
|---|---|---|
| 1. `PERSPECTIVE` reports / sets / refuses | **Met** | `headless.req309-perspective-projection` |
| 2. `FOV` reports / sets / refuses | **Met** | same transcript — 400, 0 and `banana` each refused, value stays 60 |
| 3. Switching leaves coordinates untouched | **Met** | same transcript, `EXPECT LINEXYZ` at a non-zero ELEV across two switches |
| 4. `WorldToScreen` → `ScreenRay` round trip | **Met** | `CameraTests [req309]` — closest approach 0 within 1e-4 |
| 5. Snapping correct under perspective | **Met** | `CadSnapTests [req309]` — endpoint resolved from a real `ScreenRay` pixel, hand-computed (140, 90, 25) within REQ-101 |
| 6. Projection + FOV survive `.gs` | **Met** | transcript save/reopen |
| 7. Named view restores its projection | **Met** | transcript, and **negative-tested** — see below |
| 8. Legacy `.gs` loads orthographic | **Met** | absent-key default; every pre-existing transcript still green |
| 9. REQ-100 frame budget under perspective | **PENDING — user** | `BENCH` is driven by the GLFW loop (`main.cpp`), GUI only. Blocked here, see below |

### The negative test that changed the design of this task

The first version of the transcript asserted projection state with `EXPECT LOG`. Suppressing the
named-view projection write in `GsIo.cpp` left it **green**: `EXPECT LOG` substring-matches the
*whole accumulated log*, so once a transcript has switched to perspective even once, every later
`EXPECT LOG "Projection = Perspective"` passes whether or not it is still true — making exactly the
post-save/reopen and post-restore assertions this requirement most needs silently vacuous.

Two new driver verbs, **`EXPECT PROJECTION`** and **`EXPECT FOV`**, assert the *live* state instead.
Re-running the suppression against the rewritten transcript fails at the right line
(`EXPECT PROJECTION: is Orthographic, expected Perspective`), and the write was then restored and
the file diffed against its backup to confirm nothing else moved.

Also corrected during the work: the transcript originally drew `LINE 0,0,0` → `10,20,30` and
asserted z=30. `LINE` commits at the current elevation rather than a typed per-vertex Z (per-vertex
Z is `3DPOLY`, REQ-085), so the assertion was wrong about the app, not about the feature. Replaced
with `ELEV 30` + `LINE`, which gives `LINEXYZ` a real non-zero Z to defend.

## Blocked item

**Acceptance 9 needs a GUI run and could not be completed in this session.** `BENCH` reports only
to the command log and is driven by the render loop, so it cannot run headless. Driving the GUI by
synthesized keystrokes was attempted and **blocked by the machine's endpoint protection** (RAV
flagged `Risk.Command.POWERSHELL.SendKeys` and held a decision prompt). Not worked around — an AV
prompt is the user's to answer.

Handing this one check back rather than claiming it. Everything else is verified.

## Progress

- 2026-08-31 — Authority recorded (REQ-309, D-2026-08-31-a), plan written.
- 2026-08-31 — Implemented: state fields, camera threading, doc sync, `PERSPECTIVE`/`FOV`, named
  views, `.gs` persistence, ribbon control. Tests written, negative-tested, 851/851 green.
  **Paused before PR** pending the user's GUI pass (acceptance 9 + the ribbon control's appearance).
