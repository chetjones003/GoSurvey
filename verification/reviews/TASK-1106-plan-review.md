# Verification plan review — TASK-1106 — 2026-10-25

## Architectural impact

- **Ownership:** `UpdateService` already detects Program Files installs to choose the migration
  bootstrap. Reusing that result in the existing `UpdateState` keeps path detection in Platform and
  lets the Qt updater present it.
- **Layering:** the change adds no new dependency or subsystem. UI consumes one boolean on existing
  updater state; it does not inspect Windows paths itself.
- **Data/public API:** no persisted format, network contract, or public API changes.
- **Verdict:** within REQ-078 / REQ-454 and existing architecture.

## Risks

1. A legacy install could start the one-time UAC migration without understanding why; the notice must
   precede both the notes and the update choice.
2. Showing the message to per-user installs would be confusing; the state must reuse the same path
   check that selects the bootstrap.
3. The release note must be in the plain `0.7.5` section because the release workflow copies that
   section into the update manifest.
4. The update's three outcomes and explicit user choice must remain unchanged.

## Required tests

- Path detection recognizes Program Files case-insensitively and does not mistake a similarly named
  sibling directory for Program Files.
- The update offer shows the migration message for a legacy Program Files install and hides it for a
  per-user install.
- The message appears above the scrolling release notes, and the existing Update Now, Remind Me
  Later, and Skip This Version actions remain.
- The 0.7.5 release-note section contains the user-facing migration explanation used by the release
  manifest.

## Implementation plan

- Amend the existing updater state with the legacy-install predicate calculated by the existing
  Program Files detector.
- Add a distinct, word-wrapped migration notice in the update dialog before the release notes.
- Add the 0.7.5 entry to `RELEASE_NOTES.md`.
- Add focused unit/UI coverage; build and capture the legacy and per-user update-offer states.

## Verdict

**PASS** — D-2026-10-25-f and REQ-078 / REQ-454 provide complete observable conditions. The change
reuses the existing migration decision and UI state flow, adds no architectural dependency, and
preserves the established update choice. No blocker prevents Workshop implementation.
