# Build Checklist

> The first gate. A change that does not build cleanly is rejected before any
> other review begins — there is nothing to review in code that doesn't compile.
> This checklist is **binary**: every applicable box is checked, or the verdict
> is **FAIL — build gate**.

Authority: `spec/requirements.md` REQ-200 (deterministic, reproducible build),
`spec/project.md` §7 (build constraints), `spec/architecture.md` §10 (layout).

---

## Pass/fail rule

> **PASS** only if the project builds from clean, on the target platform, with no
> new warnings, emitting artifacts to the build directory. Any failure here halts
> the entire review.

---

## 1. Clean build

- [ ] Builds from a **clean** tree (no stale artifacts) on the target platform
      of record (`spec/project.md` §1).
- [ ] Default/CI configuration builds green (`<Debug and Release>` as applicable).
- [ ] No build step depends on machine-local state not captured in the repo.

Per-language build command (fill in the project's actual invocation):

| Language | Clean build |
|----------|-------------|
| C++ (CMake) | `cmake --build build --clean-first` |
| Rust | `cargo build --release` (CI: `--locked`) |
| Zig | `zig build` |
| Go | `go build ./...` |

## 2. Warnings are errors

- [ ] No **new** compiler warnings introduced.
- [ ] Warnings-as-errors policy honored (`-Wall -Wextra -Werror`,
      `cargo clippy -D warnings`, `go vet` clean, `zig` build clean).
- [ ] Static analysis (`clang-tidy`, `clippy`, `staticcheck`) reports no new
      issues.

## 3. Determinism & reproducibility (REQ-200)

- [ ] Two clean builds of the same commit produce matching artifacts (modulo
      timestamps).
- [ ] No build-time network fetch of unpinned content; dependency lockfile
      (`Cargo.lock`, `go.sum`, `vcpkg` baseline, `build.zig.zon` hashes) is
      committed and used.
- [ ] Build is hermetic enough to reproduce on a clean checkout.

## 4. Artifact hygiene (CON-07)

- [ ] All build output lands in the designated build directory, **not** the
      source tree.
- [ ] No generated files, binaries, or local config committed by accident
      (`git status` clean of build noise).
- [ ] `.gitignore` covers any new artifact kinds the change introduces.

## 5. Formatting & tooling gate

- [ ] Formatter reports no diff (`clang-format`, `rustfmt`, `zig fmt`,
      `gofmt`/`goimports`).
- [ ] Tooling config changes (if any) are committed and justified.

## 6. Cross-platform (if multi-target)

- [ ] Builds on every supported platform in `spec/project.md` §1, or the change
      is explicitly platform-scoped with a recorded reason.
- [ ] No platform-specific assumption leaked outside the Platform layer
      (`spec/architecture.md` §3).

---

## Verdict

```
BUILD GATE — <change id> — <date>
- Clean build:     pass / FAIL
- Warnings:        clean / FAIL
- Determinism:     pass / FAIL / n-a
- Artifacts:       clean / FAIL
- Formatting:      clean / FAIL
- Outcome:         PASS → proceed to review  |  FAIL → return to Workshop
```

A FAIL here is returned immediately with the build/warning output attached. No
domain review is performed on a red build.
