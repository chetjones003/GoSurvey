# GoSurvey layer architecture

Rust and Tauri replacement for GoSurvey, tracked by issue #1403. This branch holds the Rust workspace only; the original Qt/C++ app stays in git history.

## Layers

Dependencies point strictly downward. Each layer crate depends only on the layer directly below it.

| Crate | Rank | Layer |
| --- | --- | --- |
| `gs-foundation` | 0 | Core services, math, events, UI interfaces, debugging, crash reporting |
| `gs-kernel` | 1 | Geometry: 2D geometry, curves, NURBS, B-rep, meshes |
| `gs-domain` | 2 | Drawing, Project, Survey, Terrain, PointCloud, PDF |
| `gs-services` | 3 | Renderer, IO, Network |
| `gs-application` | 4 | Command registry and interaction |
| `gs-editor` | 5 | Tauri shell and ui-sdk front end |

`layer-check` is the gate. It is a tool, not a layer, and it is not checked for direction.

Limits of the gate, for now:

- It runs as a test (`cargo test --workspace`), not as a plain `cargo build` step. Nothing in CI runs it yet.
- It counts every declared dependency, including dev and build dependencies.
- The layer crates declare the layer below before they use it. `Cargo.toml` allows `unused_dependencies` for this reason. Remove the allowance once each layer uses its dependency.
- Every workspace crate must be listed in `LAYERS` (or be `gs-layer-check`). A new crate that is not listed fails the real-workspace test.

## Commands

Run from the repository root:

```
cargo test --workspace      # includes the layer gate
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all -- --check
```

The gate test (`layer-check/tests/gate.rs`) fails if any layer crate depends on a crate at the same or a higher rank, and it checks the real workspace graph through `cargo metadata`.

Build output goes to `target/`, which is git-ignored.
