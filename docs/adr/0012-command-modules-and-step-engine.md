# ADR-0012: Commands are modules driven by one step engine

- **Status:** Accepted
- **Date:** 2026-11-21

## Context

A command is not a module today. Its behaviour is split across parallel hand-written lists in
`src/commands/CadCommands.cpp`, each keyed on the active command or the typed command name:

| List | Size |
|---|---|
| `ProcessCommandLineSubmit` | 3,911 lines; 125 branches on the active command |
| `SubmitViewportPickImpl` | 1,269 lines; 79 branches |
| `DispatchByPrimary` | 567 lines; 120 checks on the typed command name |
| `kRegistry` | a separate list of names, aliases and descriptions |

Click routing (`ViewportClickRouteFor`), dynamic-input eligibility, the Esc handler and
`ResetAllCadDraftTools` are further lists. Shared step behaviour is written out by hand:
`selBoxWaitingSecond = false` appears 70 times and `"ESC to cancel"` 50 times. `CadCommands.hpp`
declares 62 `*Phase` enums on `AppCommandState`. A command missing from one list is a known bug class.
Two examples: a command absent from click routing swallows every model-space click, and Enter needs
a per-command case.

`CadCommands.cpp` is 52,385 lines and `CadCommands.hpp` is 7,978, against ADR-0005's 3,000-line
limit. `check_move_only.py` proves only whole-file renames, so it cannot prove a PR that moves
functions out of a file.

## Decision

- **A command module is one file per command or small family**, under `src/commands/<family>/`. It
  declares one command definition:
  - its name, aliases and description;
  - its steps: select objects, point, distance, angle, keyword options, text or entity pick;
  - what happens on completion and on cancel;
  - its own state struct.
- **The registry is built from those definitions and is the only list.** The dispatch chains, click
  routing and dynamic-input eligibility shrink to nothing as families migrate.
- **One step engine owns input routing for every command:** clicks, Enter, Esc, dynamic input,
  prompt text and the select-objects box.
  - Each step declares what a blank Enter means: finish, take the default, or repeat the last value.
    Enter behaviour stays per command, and one Enter is one submit.
  - The engine is tested once per step kind with scripted inputs, not once per command.
- **A command's state lives in an active-command session**, not on `AppCommandState`. The session
  holds the command's own state struct, including its phase. It is dropped on finish or Esc. It is
  not undoable and is not drawing content.
- **Migration runs in two tracks.**
  - **Track A** moves whole functions into family files under the size limit, with no behaviour
    change. Dispatch chains that cannot be split stay in one leftover dispatch file. A new
    `check_split_only.py` proves each Track A PR. It checks that:
    - every removed line reappears verbatim in an added file;
    - the only new lines are includes, forward declarations and CMake source entries;
    - the file-size baseline only shrinks.
  - **Track B** moves one family at a time onto the step engine. The pilot is the Modify family
    (MOVE, COPY, ROTATE, SCALE, MIRROR), after ADR-0011's entity-kind operations have rewritten
    their bodies.
- **Family files are the module boundary,** not a size workaround. Files are not split arbitrarily
  just to pass the gate.

## Alternatives rejected

- **Splitting `CadCommands.cpp` into numbered parts:** this meets the line count but changes no
  ownership, and the parallel lists stay.
- **Building the step engine before the split:** the engine's pilot would land in a 52,000-line
  file, and every family PR would conflict there.
- **Keeping per-command state on `AppCommandState`:** a command could keep leaving stale state for
  the next one, so `ResetAllCadDraftTools` would stay necessary.

## Consequences

- A command's behaviour can be read in one file. Adding a command touches one definition.
- Select objects, Enter semantics and Esc cleanup are written once.
- `AppCommandState` loses its per-command phase enums and scratch fields.
- The Commands files leave the ADR-0005 baseline: `CadCommands.cpp`, `CadCommands.hpp`,
  `CadCommands_Alignment.cpp` and `CadBlocks.cpp`.
