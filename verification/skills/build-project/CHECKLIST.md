# Checklist: build-project

> Binary gate. Every applicable box checked, or **FAIL — build gate**. Mirrors
> `../../build-checklist.md`.

## Clean build
- [ ] Builds from a clean tree on every in-scope platform (`spec/project.md` §1).
- [ ] Required configs (Debug/Release) build green.
- [ ] No dependence on uncommitted local state.

## Warnings
- [ ] No new compiler warnings.
- [ ] Warnings-as-errors honored (`-Werror`, `clippy -D warnings`, `go vet`).
- [ ] No new static-analysis findings.

## Determinism (REQ-200)
- [ ] Two clean builds of the commit match (modulo timestamps).
- [ ] Lockfiles committed and used; no unpinned network fetch.

## Hygiene (CON-07)
- [ ] Artifacts only in the build directory; source tree clean.
- [ ] `.gitignore` covers new artifact kinds.

## Formatting
- [ ] Formatter reports no diff.

## Verdict
```
BUILD GATE — <change id> — <date>
- Clean build: pass/FAIL   Warnings: clean/FAIL
- Determinism: pass/FAIL/n-a   Artifacts: clean/FAIL   Formatting: clean/FAIL
- Outcome: PASS → proceed | FAIL → return to Workshop
```
