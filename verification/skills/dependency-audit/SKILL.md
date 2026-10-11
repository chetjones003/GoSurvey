# Skill: dependency-audit

**Role:** Supply-chain reviewer. Keeps the dependency graph small, justified,
healthy, and license-clean — the project's defense against bloat and supply-chain
risk.

## Responsibility

For every added or upgraded third-party dependency, confirm it is necessary,
recorded, maintained, affordable, and license-compatible. Owns Domain 3 of
`../review-checklist.md`. Enforces the bias toward a simple in-tree
implementation over a new dependency.

Authority: `spec/project.md` dependency policy + §1 (license),
`spec/requirements.md` REQ-300.

## Inputs

- The diff of dependency manifests/lockfiles (`vcpkg.json`, `Cargo.toml`/`.lock`,
  `build.zig.zon`, `go.mod`/`go.sum`, CMake `find_package`).
- `spec/project.md` dependency policy, license field, and decision log.
- Upstream metadata: maintenance status, license, transitive graph, size.

## Outputs

- A **dependency verdict**: PASS / FAIL / SPEC GAP.
- Findings per offending dependency in `../verification.md` §5 format.
- A required decision-log entry stub for each newly-approved dependency.

## Success criteria

- Each new/upgraded dependency cannot reasonably be implemented in-tree.
- Each has a `project.md` decision-log entry with rationale (REQ-300).
- Each is actively maintained and license-compatible with `project.md` §1.
- No duplicate dependency solving an already-solved problem.
- Versions pinned; lockfiles committed.
- Build-time/binary-size/transitive impact is acceptable.

## Failure criteria

- A dependency added for something implementable simply in-tree.
- No recorded justification, or incompatible/unknown license.
- Abandoned/unmaintained upstream, or an alarming transitive graph.
- Unpinned version, or lockfile not committed.

## Questions this skill asks

- Could this be a small in-tree implementation instead? Why not?
- What does this dependency pull in transitively, and what does it cost at build
  time?
- What is its license, and is it compatible with ours?
- Is it actively maintained (recent releases, responsive issues)?

## Under uncertainty

- If necessity is borderline, default to **reject** in favor of an in-tree
  implementation (bias against new dependencies).
- If license compatibility is unclear, **ask the user** — do not approve an
  unknown license (`../verification.md` §7.4).
- If maintenance status is ambiguous, gather the evidence (last release, open
  CVEs) before verdict; state what you found.

## Operates independently

Triggered by any manifest/lockfile change; needs only the diff and the policy.
Independent of other skills, though `build-project` will surface build-time cost
and `release-review` re-checks shipped licenses.

## Handoff

Rejected dependency → Workshop removes it or implements in-tree. Approved →
record the decision-log entry. Unknown license / borderline call → user question.
