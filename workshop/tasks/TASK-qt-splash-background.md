# TASK: Replace Qt splash background with approved survey artwork

## Requirement authority
REQ-093 (accepted Qt startup splash presentation). The user selected the poster-derived
background and requested the total station be included.

## Acceptance criteria
- Qt splash uses the new 16:9 poster-derived background, including the total station.
- The existing splash wordmark, subtitle, phase label, and progress bar remain readable.
- Startup behavior and existing splash timing remain unchanged.

## Files / subsystems
- `resources/qt-splash-bg.png` (replace the background asset used by the Qt splash).
- `workshop/tasks/TASK-qt-splash-background.md` (this task plan and completion evidence).

## Approach
Replace only the Qt splash resource with the generated 1672×941 PNG. Keep the legacy Win32
`resources/splash-bg.png` unchanged. Render the actual Qt splash and inspect the overlay at its
800×450 card size before requesting approval.

## Verification approach
- Run the focused Qt editor test target per the repository workflow and capture the splash
  through the existing hidden-desktop Qt test path.
- Present the splash image and capture in one T3 visualizer. Wait for user approval before
  committing or opening the PR.

## Architectural-boundary check
Static resource replacement only. No code, layout, timing, or dependency changes.

## Assumptions / technical debt
- The new artwork targets the active Qt splash; the legacy Win32 splash remains unchanged.

## Verification record
- `build.bat`: PASS (Release build, including staging the Qt splash resource).
- `ctest --preset ninja-release -R "^qt[.]editor"`: PASS (6/6 shards; 326.70 s).
- The suite writes actual startup and completed splash captures under `build/artifacts/qt-editor/`.
- User reviewed and approved the splash visual on 2026-10-23.
