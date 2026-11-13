# System map

GoSurvey's C++ source lives under `src/`, organized by domain and subsystem (for example,
`src/commands`, `src/io`, `src/qt`, and `src/render`). `src/commands/CadCommands.hpp` is a
widely included command/model header. Qt UI code is grouped under `src/qt`; OpenGL rendering
code is grouped under `src/render`.

## Architecture inventory

Run `python tools/architecture_inventory.py` from the repository root to write a readable
report and JSON metrics to `build/artifacts/architecture-inventory/`. The scanner does not
compile the project. It reports the largest source files, per-folder line counts, include
fan-in for `CadCommands.hpp`, source include cycles, Qt includes outside `src/qt`, and
OpenGL usage outside `src/render`. Use `--root` to scan another checkout and `--output-dir`
to choose a different report directory. Fixture coverage is in
`python -m unittest tools.test_architecture_inventory`.
