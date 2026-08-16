# PROJECT ISSUE & FEATURE TRACKER

> **BUG-014 onward are also GitHub issues.** The REQ-204 fuzz harness files findings to
> github.com/chetjones003/GoSurvey/issues so they can be triaged and deduplicated by signature
> (`docs/fuzz-harness.md` §6). They are recorded here too, because this file — not the issue
> tracker — is what a future reader of the repo actually finds.

## BUGS

### [BUG-017] CIRCLE stores an infinite radius — OPEN ([#59](https://github.com/chetjones003/GoSurvey/issues/59))
    - Found 2026-08-16 by the REQ-204 fuzzer (seed 4737), minimized automatically 169 → 4 lines.
    - A centre far from the picked point makes the derived radius overflow `float`, and the circle
      is committed with `r = inf`. It is then saved, exported and fed to every extents/snap/render
      calculation. Neither magnitude is exotic: `1e12` is an ordinary state-plane easting.
    - Violates REQ-201 (the refusal is not reported — there is no refusal) and REQ-101.
    - Repro: `NEW` / `CMD CIRCLE` / `CMD -1e+12,1e+38` / `PICK 50 15`.
    - Fix shape: reject non-finite derived geometry at the commit site. Worth doing generally rather
      than per-command — LINE, ARC, ELLIPSE and OFFSET all derive lengths from two user points.

### [BUG-016] OFFSET duplicates the source entity's id — FIXED 2026-08-16 ([#58](https://github.com/chetjones003/GoSurvey/issues/58))
    - Found 2026-08-16 by the REQ-204 fuzzer (seeds 2004 and 3555), minimized automatically
      155 → 9 lines.
    - Root cause: all five `CommitOffset*` functions copied the source's `EntityAttributes`
      wholesale so the copy would inherit layer/colour/linetype/lineweight/transparency — and `id`
      came with them. `EnsureEntityIds` only fills ids that are 0, so a copied non-zero id was never
      repaired and was written to `.gs` permanently.
    - Violates REQ-076 (ids unique, never reused) and through it architecture §11.9: a cross-object
      reference *is* an id, so an id naming two entities makes every such reference ambiguous.
    - The codebase already had the answer — `CopySelectionToClipboard` clears ids on copy via
      `ClearEntityIdsFrom` (CadCommands.cpp:3698). OFFSET never got the same treatment.
    - **Fixed (TASK-057)** with a `PushOffsetCopyAttrs` helper that does the copy and clears the id,
      called from all five sites. One named helper rather than the same three lines in five places,
      because the defect *was* five copies of a pattern all missing the same step.
    - Regression test: `tests/headless/transcripts/regression-58-offset-entity-id.txt` — the
      fuzzer's own minimized reproducer, kept verbatim. It also asserts `EXPECT LINES 2`, since an
      id-collision check cannot fire if OFFSET silently stops producing anything.

### [BUG-015] An empty drawing fails `.gs` resave idempotence — OPEN ([#57](https://github.com/chetjones003/GoSurvey/issues/57))
    - Found 2026-08-16 by the REQ-204 `gs-roundtrip` oracle (seed 2), minimized 116 → 5 lines.
    - Load materializes a default layer `"0"` and text style `"Standard"` that a newly created
      drawing does not carry, so save → load → save is not byte-identical.
    - Violates REQ-079's first acceptance condition. The deeper issue is that a new drawing is
      briefly in a state the rest of the code is entitled to assume cannot happen — both are
      documented as always existing.
    - Masked in normal use because the startup template is loaded on launch and already has both.
    - Fix shape: populate them at drawing creation, not in the loader.

### [BUG-014] TEXT is saved to `.gs` with id 0 — OPEN ([#56](https://github.com/chetjones003/GoSurvey/issues/56))
    - Found 2026-08-16 by the REQ-204 `gs-roundtrip` oracle on its first run.
    - The TEXT commit path (`CadCommands.cpp`, both the model and paper branches) never calls
      `BumpCadGpuCache`, so the `EnsureEntityIds` early-out — "geometry has not changed since the
      last sweep, so nothing can be missing an id" — returns on a false premise. The annotation
      keeps `id = 0` and is written out that way. The DIM* sibling paths do bump.
    - Violates REQ-076; also breaks REQ-079 resave idempotence, since load then assigns the id.
    - Rarely seen in the GUI because almost any later interaction bumps the revision and the sweep
      catches up before a save. It needs a save with no intervening geometry change.
    - `headless.gs-roundtrip.compare` is registered in CMakeLists but DISABLED against this and
      BUG-015. Re-enabling it is the regression test for both.

### [BUG-013] On a hybrid laptop GoSurvey renders on the integrated GPU — FIXED 2026-08-15
    - Found 2026-08-15 by TASK-053's acceptance run, not by a report: the same scene measured
      9.27 ms at 21:21 and 13.13 ms at 22:39 on an unchanged binary. `nvidia-smi` showed the
      RTX 5060 at 0% utilisation and 12 W idle *while the benchmark ran*; the process's 3D load was
      on the AMD 610M.
    - Root cause: GoSurvey exports neither `NvOptimusEnablement` (NVIDIA) nor
      `AmdPowerXpressRequestHighPerformance` (AMD). Those two exported symbols are how a hybrid
      laptop's driver is told an application wants the discrete GPU. Without them the choice falls
      to Windows' heuristics, which are free to answer differently between launches — which is
      exactly what happened here.
    - Impact on users: most field laptops are hybrid. Those users are silently getting the weak
      GPU for a 3D CAD application. Forced onto the discrete GPU the same scenes run **6-9x
      faster** (250k segments 9.27 -> 1.38 ms; a 2M-triangle shaded mesh 21.40 -> 1.97 ms).
    - Impact on the spec: every REQ-100 figure ever recorded — 8.93 ms (clang), 9.27 / 9.32 ms
      (MSVC, TASK-052) and the whole headroom sweep — is an integrated-GPU number, while
      `project.md` §7 names an RTX 5060. See TASK-053 FINDING-3.
    - Fix: export both symbols from a translation unit that is definitely linked (they must survive
      the linker — `main.cpp`, `extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;` and
      the AMD equivalent). ~6 lines. Verify with `nvidia-smi` showing real utilisation during BENCH,
      not by trusting the numbers to look better.
    - Watch out for: this changes which GPU every user's session runs on, and on some machines the
      discrete GPU costs battery life. It is a shipped-behaviour change and wants a recorded
      decision, which is why TASK-053 escalated it rather than fixing it in passing.
    - **Fixed (TASK-054)** with both halves the user asked for: the exported symbols make the
      discrete GPU the default even on a machine with no registry state, and a Settings → System
      checkbox ("Prefer the integrated GPU") records Windows' own per-application preference to hand
      it back, effective next launch. Measured on the reference machine with nvidia-smi as the
      instrument: exports alone -> discrete, 1.46 ms; setting checked -> integrated, 12.42 ms;
      unchecked -> discrete, 1.38 ms. The override was proven, not assumed — had the preference not
      beaten the exports, the checkbox would have been decorative.
    - Verified through the real UI, not only the API: clicking the checkbox wrote `GpuPreference=1;`
      and unchecking wrote `=2;`. 3 regression tests (registry round-trip, clear-removes-the-value,
      idempotence) touch only the test executable's own key and restore it; suite 332/332.

### [BUG-012] Double-clicking a .gs file opens GoSurvey empty — FIXED 2026-08-15
    - `int main()` in src/app/main.cpp took no argv, so the application could not receive a file
      path from the command line at all.
    - The installer registers `.gs` with `shell\open\command = "...\GoSurvey.exe" "%1"`, so the
      path WAS passed — and silently dropped. The drawing never opened.
    - Pre-existing; found while verifying REQ-079's reader change (TASK-051), not caused by it.
    - Fix: read the WIDE command line (CommandLineToArgvW) rather than adding argc/argv. argv is
      encoded in the process ANSI codepage, so a drawing under a path containing characters
      outside it would have arrived mangled and failed to open on a machine where the file
      plainly exists. A command-line file wins over the startup template; one that fails to load
      falls back to the template AND says so (REQ-201), because silence there looks identical to
      the original bug.
    - Verified: samples\surface-demo.gs opens and renders via the exact command line Explorer
      uses; a malformed .gs falls back with the app alive and responsive; a directory argument is
      ignored.
    - Follow-up, not done: DXF/DWG paths are not handled (only .gs is associated today), and the
      drawing tab still reads "Drawing 1" rather than the opened file's name.

### [BUG-011] ViewportRenderer did not compile with MSVC — FIXED 2026-08-15
    - Symptom: ~50 x C2362 in ViewportRenderer.cpp, "initialization of 'x' is skipped by
      'goto finish_render'". Never seen locally because CMakePresets' ninja-release pins no
      compiler, so CMake picks clang off PATH; surfaced on the first CI build, where
      msvc-dev-cmd puts cl.exe first.
    - Root cause: `goto finish_render` (paper-space early-exit) jumped over ~50 initialized
      declarations INTO their scope, which is ill-formed C++ ([stmt.dcl]/3). MSVC was correct
      to reject it; clang was the lenient one.
    - Fix: give the skipped model-space region its own block scope, so the label sits outside
      the scope of everything jumped over. 11 added lines, 0 modified — the goto and all 900
      lines of render code are untouched, so no behaviour could change.
    - Verified: MSVC 309/309 (separate cl.exe build tree) and clang 309/309.
    - Residual: CI builds only with clang, so this can regress unnoticed. A second matrix leg
      would catch it.

### [BUG-010] Four tests reported ***Failed while their bodies never ran — FIXED 2026-08-15
    - Symptom: `ctest` reported 4 failures (paper-circle stride, mesh state-plane origin, id sweep
      idempotence, erased-id resolution) that PASSED when run individually by name.
    - Root cause: each of those four `TEST_CASE` names contained an em dash. `catch_discover_tests`
      round-trips the name into a CTest filter through a codepage that mangles it, so Catch2
      received a filter matching nothing, printed "No test cases matched", and exited non-zero.
      The tested logic was never at fault and the test bodies never executed.
    - Fix: ASCII-only `TEST_CASE` names. Suite went 305/309 -> 309/309.
    - Prevention: rule recorded in `spec/coding-standards.md` §12 — ASCII in anything a toolchain
      re-parses (test names, `.rc` scripts), em dashes anywhere a human reads.
    - Why it mattered: REQ-202's release pipeline gates publication on a green `ctest`, so this
      would have blocked every automated release while looking like a real product defect.

## FEATURES

### [FEAT-011] BENCH has no mesh case, and its file record does not name the profile — DONE 2026-08-15
    - Delivered by TASK-053. `BENCH MESH [triangles] [frames]` measures a 2,000,000-triangle shaded
      terrain (the density decided 2026-08-15), forcing Shaded for the run and restoring the user's
      style afterwards; `bench-req100.txt` now records `profile` and `scene` lines for all three
      cases. Six new generator tests; suite 329/329.
    - Result: **p95 1.97 ms against 16 ms** on the RTX 5060, so REQ-100 profile (b) and REQ-064's
      "budget met in Shaded" both close. On the integrated GPU the same scene fails at 21.40 ms,
      which is how BUG-013 was found.
    - Known limit, stated up front: the scene is one mesh of one part, so per-part draw-call cost
      (a real import has hundreds) is not in this profile.
    - Original entry follows.

### [FEAT-011 detail] the gap as originally filed
    - REQ-100 defines three cost profiles. `BENCH` implements two: `BENCH [segments]` and
      `BENCH SURFACE`. There is no mesh scene and no `BENCH MESH`, so profile (b) — shaded meshes at
      the REQ-063 density — has never been measured and cannot be. TASK-041 §7 called this out on
      2026-08-12 and nothing has closed it since; TASK-052 hit it again on 2026-08-15.
    - Two requirements are held open by it: REQ-100 itself (its acceptance says "in each of the
      three profiles") and REQ-064, whose "budget met in Shaded" condition has no measurement behind
      it. Both are marked accordingly in `spec/requirements.md` rather than reading as met.
    - Fix (a): a mesh scene in `src/util/benchscene.*` at a stated triangle count, a `BENCH MESH
      [tris] [frames]` branch beside the SURFACE one in `CadCommands.cpp`, and a generator test in
      the shape of the existing ones (deterministic, byte-identical across runs, extent fixed so
      density changes rather than area — the trap TASK-039 §3 documents).
    - Fix (b), same writer, ~5 lines: `bench-req100.txt` records only `segments <n>`, so the surface
      run appears as `segments 599898` and is indistinguishable from a 600k-segment line scene. The
      console message names the profile and a comment right above the file writer explains exactly
      why that matters — the permanent record is the half that missed it. Write the profile name,
      and the point/triangle counts when it is a surface.
    - Watch out for: the mesh profile needs the depth buffer and Shaded style active to mean
      anything (TASK-040 measured depth testing at ~2 ms on its own, clang, with no meshes present),
      and the bench must restore the visual style along with everything else it already restores.

### [FEAT-010] Clean up downloaded update installers — OPEN
    - `%LOCALAPPDATA%\GoSurvey\updates` keeps every installer the updater has ever downloaded.
      Nothing deletes them after a successful install, so it grows ~6 MB per update forever.
      Observed 2026-08-16 holding both `0.5.0-beta.8` and `0.5.0` (~12 MB).
    - Not a correctness problem — the hash check and install both work — purely disk hygiene.
    - Fix: after `LaunchInstallerAndExit` succeeds, or on the next startup check, delete
      `GoSurvey-*-Installer.exe` in that folder other than the one just applied. Next-startup is
      safer: the app is exiting at launch time and the file is in use by the running installer.
    - Watch out for: a partially downloaded file from a killed run (REQ-078 already deletes those
      on failure, so anything left is from a hard kill), and not deleting an installer the user
      may still be mid-install with.

### [FEAT-002] Traverse Editor
    - [ ]   Survey Traverse editor window that allows user
            to create legs of a traverse in different ways
                - such as with a backsight point, backsight reference angle,
                face 1/face2 measurments, forsight point/points,
                horizontal distance, horizontal angle (DMS or decimal), vertical angle (DMS or decimal,
                zenith or from backsight), slope distance. All of this should optional input data based\
                on what the user wants to provide and it will be up to the traverse engine to solve the
                traverse and let the user know what data to provide if data is insufficient.
    - [~] Survey Least Squares Adjustment window that lets the user review traverse leg residuals to detect
            blunders and edit accordingly.
            - Done (REQ-014..017): "Calculate Closure" opens a window showing the unadjusted closure
              beside a weighted least-squares adjustment (closed-loop), with a per-observation
              residuals tab and configurable a-priori standard errors. Raw F1/F2 measurements and
              per-leg statistics now display in the editor (REQ-010..012). See spec ADR-001/002.
            - Done (REQ-018): each leg expands inline to an editable observation-set editor —
              add/remove sets and edit the literal F1/F2 circle readings, slope distances, and
              zenith angles; the leg re-reduces from its sets via ReduceLegFromSets (ADR-003,
              backsight reading stored on the leg). "+ Add Leg" moved into the table as its last row.
            - Remaining: connecting (point-to-point) traverses.
    - [ ] Ability to import raw data formats such as Autodesk .fbk, Bently RWD,
            Carlson RW5, Microsurvey RW5, TDS RAW,
            TDS RW5 and traverse editor gets filled in automatically

### [FEAT-003] Level Loop Editor
    - [ ] same concept as traverse editor, allowing the user to process level loop data, whether single wire
            or three wire with a least sqaures adjustment editor

# COMPLETED
~~### [BUG-002] Fuzzy find menu has some functionality problems
    - using the up arrow closes the menu
    - using the down arrow and selecting the highlighted command does not run that command
    - Fixed: command input claims Up/Down via SetItemKeyOwner so keyboard-nav no longer steals
      focus while the list is open; highlighted command is persisted across the Enter frame
      (single-line InputText self-deactivates on Enter) so submit runs the highlighted entry.
~~

~~### [BUG-003] Focusing different panels shows a ugly dark blue
    - I don't want to visually see different panels gaining focus. no color change
    - Fixed: light theme TitleBgActive set equal to TitleBg, so a focused docked node's tab-bar
      strip no longer flips to the dark caption blue (ImGui fills it with TitleBgActive on focus).
~~

~~### [BUG-001] Object hovering not working properly when in state plane coordinate system.
    - Object hovering triggers on objects even if my cursor is not "visually" touching the object
    - Fixed: pick distance math now runs in double precision (float cancellation at state-plane
      magnitudes was quantizing distances to ~1 ft); hover/click tolerance uses the robust
      outlier-trimmed extent instead of the raw bbox; idle hover highlight uses a tight fixed
      3px aperture so the cursor must visually touch the stroke at any zoom.
~~

~~### [FEAT-001] Undo Redo System
    - [ ] Full undo redo with configureable history size settings window
    - [ ] UI Buttons at the top ribbon
    - [ ] CRTL+Z and CRTL+SHIFT+Z for undo redo
    - [ ] history log should be in %APPDATA%\GoSurvey\
    - [ ] update INNO script if necessary
~~
