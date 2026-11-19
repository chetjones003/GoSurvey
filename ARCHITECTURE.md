# GoSurvey layer architecture

Rust and Tauri replacement for GoSurvey, tracked by issue #1403. The Qt/C++ source, build and tests are removed from this tree and stay in git history. The kept root files (README.md, PRIVACY.md, RELEASE_NOTES.md, CLAUDE.md, AGENTS.md) still describe the Qt app until the rewrite updates them.

## Layers

| Crate | Rank | Layer |
| --- | --- | --- |
| `gs-foundation` | 0 | Core services, math, events, toolkit-neutral UI interfaces, debugging, crash reporting |
| `gs-kernel` | 1 | Geometry: 2D geometry, curves, NURBS, B-rep, meshes |
| `gs-domain` | 2 | Drawing, Project, Survey, Terrain, PointCloud, PDF |
| `gs-services` | 3 | Renderer, IO, Network |
| `gs-application` | 4 | Command registry and interaction |
| `gs-editor` | 5 | Tauri shell and ui-sdk front end |

Command handlers and their active sessions live in `gs-application`. The Editor converts native
keyboard and viewport interactions into `CommandInputEvent`s (`TextSubmitted`, `PointPicked`, or
`Cancelled`). The Foundation `EventBus` routes those inputs to the Application command event
handler, which forwards semantic input to the active session. Sessions publish typed `EngineEvent`s
through a second synchronous bus; Application handlers apply drawing changes and collect command
output. The Editor renders the returned prompt, messages, and drawing snapshot.

`gs-application` owns command input parsing and session lifecycle, while `gs-kernel` owns coordinate
and geometry calculations such as translation, bearing projection, and line validation. See
[`docs/architecture/commands.md`](docs/architecture/commands.md) for the command-author workflow.

## How the rule is enforced

Each layer crate depends on the crate directly below it, and that chain reaches every lower layer. Cargo rejects dependency cycles at load time. So any dependency on a higher layer closes a cycle and `cargo build` fails with `cyclic package dependency`. This is the standard cargo check, with no custom tooling.

Verified by hand: adding `gs-domain` to `foundation/Cargo.toml` makes `cargo build --workspace` fail with that error. Removing the line restores the build.

The check runs only when someone builds the workspace. No CI workflow runs it yet, so a violation is caught on a developer's machine, not automatically on push.

## Intentional allowances

These are allowed on purpose. Cargo does not check them, and nothing in this repo checks them either. Only review does.

- **Independent crates.** A new crate may sit outside the chain. Cargo does not check it. Its header comment should say which layer it belongs to, and reviewers should confirm it does not depend on a higher layer.
- **Dev-dependencies that point upward.** Cargo permits dev-dependency cycles, so a test may use a higher layer. Reviewers should confirm each one is intentional.
- **Skip-level dependencies.** For example, `gs-editor` may depend directly on `gs-foundation`. This matches the spec's "downward only".
- **`unused_dependencies = "allow"`** in `Cargo.toml`. It stays until each layer uses the crate below it.

## Commands

From the repository root:

```
cargo build --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all -- --check
```

The Editor front end lives in `editor/ui`. From that folder:

```
npm ci
npm run build
npm test
```

Run the app on Windows from the repository root:

```
npm run dev       # debug: Vite dev server, then the app. Ctrl+C stops both.
npm run release   # release: builds the front end, embeds it, then runs the app
```

`npm run dev` runs the app without `custom-protocol`, so the window loads the Vite server at `http://127.0.0.1:1420`. `npm run release` builds with `tauri/custom-protocol`, which embeds the built front end.

Build output goes to `target/`, which is git-ignored.
