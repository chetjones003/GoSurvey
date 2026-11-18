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

## How the rule is enforced

Each layer crate depends on the crate directly below it, and that chain reaches every lower layer. Cargo rejects dependency cycles at load time. So any dependency on a higher layer closes a cycle and `cargo build` fails with `cyclic package dependency`. This is the standard cargo check, with no custom tooling.

Verified by hand: adding `gs-domain` to `foundation/Cargo.toml` makes `cargo build --workspace` fail with that error. Removing the line restores the build.

## Intentional allowances

These are allowed on purpose. Cargo does not check them, so review is the check.

- **Independent crates.** A new crate may sit outside the chain. Cargo does not check it. Its header comment must say which layer it belongs to, and it must not depend on a higher layer.
- **Dev-dependencies that point upward.** Cargo permits dev-dependency cycles, so a test may use a higher layer. Such a dev-dependency must be intentional and noted in the crate's manifest.
- **Skip-level dependencies.** For example, `gs-editor` may depend directly on `gs-foundation`. This matches the spec's "downward only".
- **`unused_dependencies = "allow"`** in `Cargo.toml`. It stays until each layer uses the crate below it.

## Commands

From the repository root:

```
cargo build --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all -- --check
```

Build output goes to `target/`, which is git-ignored.
