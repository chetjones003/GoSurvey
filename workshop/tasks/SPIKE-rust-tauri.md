# Rust/Tauri drafting spike

Authority: accepted experiment REQ-9901, spec/spikes/rust-tauri.md, user request 2026-11-15.
References: REQ-040 command line, REQ-045 PAN, REQ-047 ORTHO, REQ-101 precision,
REQ-122 extents; only their relevant 2D subset is explored, not full production compliance.

Plan: isolated prototypes/tauri-cad frontend and src-tauri Rust engine; no production
source edits. Build React workspace with Tailwind/daisyUI, SVG world-to-screen display,
typed/pointer command events into a native engine, selection, line transforms, camera,
sample survey linework, command history, undo/redo and useful keyboard shortcuts.

Preimplementation Verification: APPROVE for REQ-9901 only.
- Architecture: Rust entity/command engine has no frontend dependency; Tauri adapter
  owns engine; React renders native snapshots and owns camera, no duplicate edit logic.
- Risks: stale HMR mirrors, asynchronous event ordering, malformed/overflow coordinates,
  degenerate lines, selection references, cursor zoom, native toolchain availability.
- Validation: Rust tests for happy/failure commands and f64 state-plane values; TypeScript
  check and Vite build; native desktop build and actual interaction smoke test/captures.
- Dependencies: requested stack plus serde serialization, Tauri build tooling, icon components;
  lock dependencies; no production build integration or implied release readiness.
- Scope decisions recorded in SPEC before coding. User's concrete request supplies title,
  problem (evaluate port and UI iteration), goal, stack, scope and observable acceptance;
  no additional confirmation interview is needed.

Status: implemented, verified and UI approved. User requested branch publication only; no PR.

## Outcome / verification

- Added isolated React 19 / Vite 6 / Tailwind 4 / daisyUI 5 workspace with
  ribbon, drawing toolspace, layer visibility/current layer, object list,
  properties inspector, command history, help and viewport navigation.
- Rust f64 engine ports the bounded 2D command phases: LINE, MOVE, COPY,
  DELETE/ERASE, selection, ORTHO and undo/redo. Pan/zoom remain presentation-owned.
- Existing C++ references: CadCommands.cpp, OrthoConstrain.hpp, ZoomFraming.hpp.
  SVG viewport is explicit spike-only scope, no claim of production CAD/GPU parity.
- `cargo test --no-default-features`: 3 tests passed, including state-plane values,
  transforms, undo/redo, invalid/degenerate input, ORTHO and cancellation.
- `cargo clippy --no-default-features -- -D warnings`: PASS.
- Camera tests: 3 passed, covering coordinate roundtrip, cursor-anchored zoom and extents.
- TypeScript check (also noUnusedLocals), Vite production build, Windows native
  Tauri debug build: PASS. Initial missing Windows icon was fixed with a generated
  code-native icon; final native build succeeded.
- Actual hidden native WebView2 smoke: 12 workflow checks passed, no page errors.
  It covers command input, pointer LINE, selection/properties, MOVE/COPY/DELETE,
  UNDO/REDO, invalid input, PAN 1:1, wheel zoom, extents and help.
- Live Vite CSS HMR: observed 176 ms including polling; no native recompile;
  geometry survived HMR and a full frontend reload. One local sample only.
- `npm audit --omit=dev`: zero vulnerabilities reported. Cargo dependencies
  locked; no claim of a full security audit or production dependency approval.
- No production C++/Qt build/test run: production source and build scripts untouched.
- Captures and evidence under build/artifacts/tauri-spike, never committed.
  Normal workspace, selection/properties, invalid input, line preview, help and
  final edited state captured, plus readable close-ups.
- Documented run/build/test commands and exact limitations in prototypes/tauri-cad/README.md.
- Build output stays in build/.cmake/tauri-spike, separate from production apps.
- README/PRIVACY/bundled/public manual pre-PR review is not triggered: no PR or
  production behavior change is proposed. A later PR must complete that review.

Verification verdict: PASS for REQ-9901 spike scope. User approved the neutral UI
on 2026-11-16; no production architecture transition or broader parity is approved.
Technical debt is intentional: in-memory only, SVG 2D renderer, snapshot transport,
no interchange/projects/COGO/3D/PDF, no large-scene benchmarks or native rebuild timing.

2026-11-16: User requested a neutral palette. Replaced green/teal UI accents and tinted surfaces with charcoal/grays; boundary linework and previews use neutral silver. Kept distinct drawing-layer colors and semantic errors. This updates the ongoing prototype, with no command/engine changes.

2026-11-16: User approved the appearance and requested publishing this branch without
a pull request. Preserve this spike for later; further migration work is deferred
while the user addresses other GoSurvey issues. Generated schemas and build evidence
are excluded from the published sources.
