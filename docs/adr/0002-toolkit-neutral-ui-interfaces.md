# ADR-0002: Keep engine UI interfaces toolkit-neutral

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

The engine needs to request windows, dialogs, progress, clipboard, and timers without depending on Qt. Qt-specific UI currently makes shared behavior harder to test without a GUI and would make changing toolkits require changes throughout the engine.

## Decision

Define toolkit-neutral UI interfaces in the engine. Provide thin platform adapters: Qt implements the interfaces and translates Qt input into engine events; Headless supplies test implementations such as scripted dialogs. Keep windows, dialogs, ribbon, panels, hotkeys, and toolkit selection in `GoSurveyEditor`. Adapters contain translation and presentation glue, not business rules.

## Alternatives rejected

- **Use Qt types in engine interfaces:** this couples engine code and tests to Qt.
- **Build a project-owned widget toolkit:** this expands the work beyond the need for an abstraction seam.
- **Put business behavior in each toolkit's dialogs:** this duplicates behavior and makes toolkit replacement expensive.

## Consequences

- Engine behavior can be tested without a GUI toolkit.
- Qt remains the current UI implementation, behind a narrow adapter.
- A future toolkit change affects the application and its adapter rather than engine logic.
- Interfaces and adapters require explicit contracts for ownership, input translation, and outcomes.
