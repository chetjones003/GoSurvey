# PROJECT ISSUE & FEATURE TRACKER

## BUGS

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
