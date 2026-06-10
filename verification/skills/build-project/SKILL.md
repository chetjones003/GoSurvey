# Skill: build-project

**Role:** Build engineer. The first gate — confirms the change compiles cleanly,
deterministically, and without warnings before any other review spends effort.

## Responsibility

Establish that the codebase builds from clean on the target platform(s), emits
artifacts only to the build directory, and introduces no new warnings or
non-determinism. This skill does **not** judge correctness, design, or style — it
judges *buildability*. A red build halts the entire review pipeline.

Authority: `spec/requirements.md` REQ-200, `spec/project.md` §7,
`spec/architecture.md` §10.

## Inputs

- The change under review (diff / branch / PR) and the target commit.
- `spec/project.md` §1 (platforms, toolchain) and §7 (build constraints).
- The project's build invocation (CMake/Cargo/Zig/Go) and lockfiles.
- CI configuration, if present.

## Outputs

- A **BUILD GATE** verdict: PASS or FAIL.
- On FAIL: the failing build/warning output plus findings in the
  `../verification.md` §5 format (location, violated item, observed, required).
- A note of which platforms/configs were built.

## Success criteria

- Clean build is green on every supported platform/config in scope.
- Zero new compiler/analyzer warnings; warnings-as-errors honored.
- Build is reproducible from a fixed commit (REQ-200); lockfiles committed.
- All artifacts land in the build directory; source tree stays clean (CON-07).
- Formatter reports no diff.

## Failure criteria

- Build fails, or introduces any new warning.
- Build depends on uncommitted local state, or fetches unpinned content.
- Artifacts written into the source tree, or build noise left uncommitted-ignored.
- Non-deterministic output across two clean builds of the same commit.

## Questions this skill asks

- Which platforms/configs are in scope for this change? (If unclear → ask.)
- Is a newly-failing warning pre-existing or introduced by this change?
- Is a build-config change intentional and recorded?

## Under uncertainty

- If the canonical build command is unknown, **ask** rather than guess; do not
  invent flags.
- If a failure may be environmental (local toolchain) vs. real, reproduce from
  clean before reporting; state which it is.
- If determinism cannot be verified in the available environment, mark that
  sub-check `n-a` and say so explicitly — never assume.

## Operates independently

Needs only the change, the spec, and a build environment. Requires no other skill
to have run. It is the designated upstream gate: other skills may note "build
gate not yet PASS" but this skill never depends on them.

## Handoff

On PASS → unblocks `architecture-review`, `code-review`, `testing`, etc. On FAIL
→ returns immediately to Workshop; no further review proceeds.
