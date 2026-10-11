# TASK-194 — SWEEP exposes its twist and alignment options as command keywords (GitHub issue #241)

## Requirement authority

- **REQ-315** (accepted 2026-09-03, D-2026-09-03-b / ADR-048). Its Statement says SWEEP runs a
  profile along a path *"with an optional constant twist angle and an option to hold the profile
  normal to the path or hold it at a fixed world orientation"*, and that both commands exist *"in the
  typed and the prompted shape the REQ-313 / REQ-314 commands use"*.
- The **kernel already implements both** — `brep::SweepOptions{ twistRad, alignToPath }`, guarded to a
  single straight path segment, `Problem::SweepUnsupportedOption` otherwise (`brep.cpp:2539`). The
  SWEEP *command* (`CadCommands.cpp`) hardcodes `brep::SweepOptions{}` in three places and has no way
  to set them. This task closes that gap; no kernel change.
- Constraints: REQ-201 (no silent failure — a bad keyword is reported, an unsupported combination
  gets the kernel's named reason), REQ-301 (smallest correct change).

## Scope

Command layer only.

- `AppCommandState` gains `double sweepTwistDeg = 0.0` and `bool sweepAlignToPath = true`, reset when
  a SWEEP starts.
- `HandleSweepTextInput` recognises, during `SweepPhase::SelectInputs`:
  - `T <deg>` / `TWIST <deg>` (also `T45`) — set the twist angle in degrees; bare `T` prints usage.
  - `A` / `ALIGN` — toggle path-alignment; `A Y` / `A N` set it explicitly.
  - empty line — build (unchanged).
  - anything else — return false, exactly as now (so a real command name still routes through the
    issue #233 path).
  Each keyword reprompts with the current settings.
- `CadSweepPromptText` names the keywords and shows any non-default setting.
- `CommitSweep` and `CadBuildSweepSolid` (the ghost) pass
  `brep::SweepOptions{ sweepTwistDeg * kDeg2Rad, sweepAlignToPath }` instead of `{}`.

### Deliberately unchanged

- **The bare-`SWEEP`-on-a-ready-selection shortcut still builds immediately with defaults.** Options
  are a prompted-flow feature (matches AutoCAD, where Twist/Alignment appear during the path prompt).
  To set them: run `SWEEP`, select the profile and path, type the keywords, then Enter.
- **No `.gs` change.** A swept solid stores topology only (REQ-315 / ADR-045 (c)); the twist/align
  inputs are not persisted, exactly as the path and profile are not.
- **The kernel's single-straight-segment guard stays.** Setting a twist on a curved or multi-segment
  path fails the commit with `Problem::SweepUnsupportedOption`'s existing message; the ghost simply
  does not draw. Lifting that guard (twist along a curve) is an explicitly deferred REQ-315 item.

## Test approach

`tests/headless/transcripts/req315-sweep.txt` gains:

- **`A N` (fixed orientation) on a path oblique to the profile plane.** A 4×6 rectangle in the XY
  plane swept along a line whose direction has both X and Z components (e.g. `(0,0,0)→(6,0,8)`,
  length 10). With alignment on (the default) the cross-section stands perpendicular to the path and
  V = 24·10 = 240; with `A N` the rectangle is only translated, giving an oblique prism whose
  V = base area · perpendicular rise = 24·8 = 192. The two volumes are exact and differ, so the
  keyword provably reached the kernel. Undo removes it.
- **A twisted straight sweep.** `T 90` on a non-square profile along a straight path; the twist is
  applied as a ruled band to the rotated end profile, so the solid's analytic bounds widen to the
  rotated footprint — `EXPECT SOLIDBOUNDS` on that (exact, integer coords, 0.01 tol). `EXPECT LOG
  "Solid created"`; undo removes it.
- **`T 45` on the curved (arc) path is refused by name** — `EXPECT LOG "single straight path"`, no
  solid added; ESC ends the command cleanly.
- **A bad keyword** (`ZZ`) during selection still falls through to the generic path (no crash, no
  solid).

Exact volume / area / bounds numbers are filled from a first run (the transcript's existing numbers
were obtained the same way). Negative check: revert the `SweepOptions{}` → options change in
`CommitSweep` and the `A N` volume assertion fails (192 → 240).

## Verification

- `./dev/build` clean.
- `./dev/test` — full suite green (baseline 1113/1113).
- `architecture-review`: no new dependency, no layer crossing, no kernel or IO change.

## Status

**Implemented.** Command layer only — no kernel, IO or `.gs` change.

- `AppCommandState::sweepTwistDeg` / `sweepAlignToPath`, reset in `CancelSweepCommand`.
- `HandleSweepTextInput` parses `T <deg>` / `TWIST <deg>` / `T45` and `A` / `ALIGN` (`A Y` / `A N` /
  bare toggle); an unrecognised token returns false, so the prompt just reprints (unchanged).
- `CadSweepPromptText` names the keywords and echoes any non-default setting.
- `SweepOptionsFrom(st)` feeds the ghost (`CadBuildSweepSolid`) and the commit (`CommitSweep`).
- `req315-sweep.txt`: `A N` on a 3D oblique path → oblique prism V=192 (vs the aligned 240),
  `T 90` on a straight path builds (+ a non-keyword at the prompt does not disturb the setting),
  `T 45` on an arc path is refused *"…single straight path segment in this version."*

Verified: `./dev/build` clean, `./dev/test` 1113/1113, negative-tested (revert `SweepOptionsFrom`
in `CommitSweep` → the `A N` volume assertion fails 192→240).
