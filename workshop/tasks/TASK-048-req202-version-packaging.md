# TASK-048 — One version source, a stable executable name, and a tracked installer script

- Type:    refactor
- Status:  implement
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         M-Distribution step 1 (roadmap) — the packaging refactor the pipeline stands on
- Requirements: **REQ-202** (accepted 2026-08-15) — the packaging half; **REQ-077** (accepted
                2026-08-15) — the single-version-source half; **REQ-078** — the `GoSurvey.exe` rename
                and `[InstallDelete]` sweep it depends on
- Constraints:  **ADR-029 (a)(b)(c)(f)**; REQ-200 (reproducible build, artifacts to `build/` only);
                CON-07; REQ-300 (no new dependency — none is added here)
- Acceptance:   the subset of the three requirements this task can satisfy on its own:
  - from REQ-077: "the version shown in the UI, the version embedded in the executable's Windows
    version resource, the installer's `AppVersion`, and the git tag all derive from the one CMake
    value — changing that value changes all of them and no other edit is required";
  - from REQ-078: "after the installer runs, the previous versioned executables (`GoSurvey-0.*.exe`)
    are gone from the install directory, one `GoSurvey.exe` remains, and desktop/Start-menu shortcuts
    and the `.gs` file association still resolve";
  - from REQ-202: "the installer's `AppVersion`, the release tag, and the manifest's `version` field
    are equal on every published release" — this task supplies the mechanism (ISCC `/D` defines);
    TASK-049 supplies the pipeline that uses it.
- Owning subsystem: Build/Platform. No domain, renderer, command or IO code changes.

## 2. Scope
- In scope:
  - a generated `Version.hpp` (CMake `configure_file`) carrying the version to C++;
  - a generated `VERSIONINFO` resource so the shipped `.exe` reports its version to Windows;
  - `OUTPUT_NAME` changed from `GoSurvey-<version>` to `GoSurvey`;
  - one tracked, parameterized `installer/GoSurvey.iss` with relative paths and `/D` overrides,
    including `[InstallDelete]` for the old versioned executables;
  - `.gitignore` amended so that script is tracked while `installer/Output/` and the superseded
    per-version scripts stay ignored;
  - the `AppMutex` pair (application + script) that lets Inno close a running instance;
  - the version displayed in the UI.
- Out of scope:  the CI workflow (TASK-049); any update checking, downloading or manifest handling
  (TASK-050); code signing; changing the install location away from `{autopf}`.
- Smallest change: CMake already holds the version — nothing new needs to *store* one. The work is
  routing that single value to the three places that currently hard-code it, and removing the
  version from the executable's filename so a path stops changing per release.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed.
    - [ ] Yes → STOP.
- Reasoning: every structural decision here was made in **ADR-029** and accepted in the decision log
  before this task opened — the rename (b), the single version source (a), the parameterized script
  (c), and the `AppMutex` (f). No new dependency: `Version.hpp` is generated, not fetched. The
  executable rename is a **user-visible packaging change**, which is why it is an ADR decision this
  task executes rather than one it makes.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Rename the executable to a stable `GoSurvey.exe`, given it breaks the path of existing 0.4.x installs? | 2026-08-15 | Yes — rename. Recorded as decision-log D1; `[InstallDelete]` handles existing installs |
| Q2 | Keep installing to Program Files (UAC prompt per update) or move to per-user? | 2026-08-15 | Not answered directly; the user's "no silent updates" ruling makes a UAC prompt follow a deliberate click, so `{autopf}` stays. See ASSUMPTION-1 |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: The install location stays {autopf} (Program Files), so applying an update raises UAC.
- Because:       the question was put to the user and answered only indirectly, via the separate
                 ruling that updates are never silent.
- Risk if wrong: every update costs a UAC prompt. Not a correctness risk, and not irreversible —
                 but changing it later relocates existing installs, which is the expensive part.
- Validate by:   confirmed with the user before TASK-050 ships the update flow that triggers it.
```
```
ASSUMPTION-2: Inno Setup's [InstallDelete] runs early enough to remove GoSurvey-0.4.0.exe from an
              existing install before [Files] lays down GoSurvey.exe, leaving exactly one binary.
- Because:       the ordering is documented behaviour, but the case that matters (upgrading in place
                 over a *differently named* executable) is not one this project has exercised.
- Risk if wrong: an upgraded install keeps a stale GoSurvey-0.4.0.exe next to the new GoSurvey.exe.
                 Harmless at runtime, but it makes the REQ-078 acceptance condition false and would
                 leave a shortcut pointing at a binary that never updates again.
- Validate by:   installing 0.4.0 from the existing installer, then upgrading with the new one and
                 listing the install directory. Recorded as a manual acceptance step.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: route the existing `project(VERSION)` outward. Nothing gains a version field; three
  places lose their hard-coded copy.
- Files/functions to touch:
  - `CMakeLists.txt` — `configure_file` for `Version.hpp` and the version resource; `OUTPUT_NAME`
  - `src/app/Version.hpp.in` — new, generated into the build tree (REQ-200: not the source tree)
  - `resources/icons/app.rc` — unchanged (icon only); a **separate** generated version resource is
    added, because `app.rc` resolves `app.ico` relative to itself and configuring it into `build/`
    would break that path
  - `resources/version.rc.in` — new
  - `src/app/main.cpp` — `AppMutex`; window title carries the version
  - `installer/GoSurvey.iss` — new, replacing `0.3.1.iss` + `0.4.0.iss`
  - `.gitignore` — un-ignore the tracked installer files only
- Test approach: this is build/packaging work, so the checks are observational rather than unit
  tests — there is no pure function here to test, and inventing one would be worse than the
  observation. happy path = bump the CMake version, reconfigure, and confirm the built exe's
  Properties dialog, the `Version.hpp` value and the ISCC output filename all move together;
  failure mode = ASSUMPTION-2's upgrade-over-0.4.0 check, and a second instance refusing to confuse
  the installer's `AppMutex` detection.
- Steps:
  - [ ] `Version.hpp.in` + `configure_file`
  - [ ] version resource so Windows Properties shows it
  - [ ] `OUTPUT_NAME` → `GoSurvey`
  - [ ] `AppMutex` in `main.cpp`; version in the window title
  - [ ] parameterized `installer/GoSurvey.iss` with `[InstallDelete]`
  - [ ] `.gitignore`
  - [ ] build clean; run ISCC locally; verify the three version surfaces agree

## 7. Workflow-specific notes
- Refactor: **"no behavior change" does not hold here and is not claimed.** Three behaviours change
  deliberately and each is authorized: the executable's filename (ADR-029 (b)), the presence of a
  version resource, and single-instance mutex creation. Nothing in the drawing, IO, render or
  command path is touched, which is the boundary that matters — existing tests must pass unchanged,
  and any test that does not is a bug in this task, not an expected update.

## 8. Implementation log  (append as you work)
- 2026-08-15 opened; Authority and Plan complete; boundary check clean (all structure pre-decided in
  ADR-029). Status → implement.
- 2026-08-15 `Version.hpp.in` + `configure_file` → `build/generated/Version.hpp`. Build tree only,
  per REQ-200.
- 2026-08-15 **Finding (self, build):** `#include <windows.h>` for `CreateMutexW` broke four
  `std::max`/`std::min` call sites in `main.cpp`. Cause: several headers already included above pull
  `<windows.h>` themselves, so a `NOMINMAX` placed next to this file's own include arrives after the
  macros are already defined. Fixed by hoisting `NOMINMAX` / `WIN32_LEAN_AND_MEAN` above **every**
  include in the file — the same placement `PdfAttach.cpp` and `WinFrameControls.cpp` use.
- 2026-08-15 **Finding (self, build):** `llvm-rc` rejected `version.rc` — "Non-ASCII 8-bit codepoint
  can't be interpreted in the current codepage" — from an em dash in `FileDescription`. The resource
  compiler reads the file in the current codepage, so the string block is ASCII-only. The UI keeps
  its em dash; only the resource is plain.
- 2026-08-15 Version resource verified on the built binary: `FileVersion` 0.4.0, `ProductVersion`
  0.4.0, `OriginalFilename` GoSurvey.exe, read back through `(Get-Item ...).VersionInfo`.
- 2026-08-15 Added a generated `build/generated/version.iss` so a hand-run `ISCC` also takes the
  version from `project(VERSION)`. Without it the acceptance condition ("changing that value changes
  all of them and **no other edit is required**") would have held for CI and quietly failed for a
  local build, which is exactly the drift this task exists to remove.
- 2026-08-15 `.gitignore` narrowed from `/installer/` to `/installer/*` plus three un-ignore rules.
  Verified with `git check-ignore -v`: `GoSurvey.iss`, `License.txt`, `InfoAfter.txt` are trackable;
  `0.3.1.iss`, `0.4.0.iss` and `Output/` remain ignored. The superseded scripts were left on disk
  rather than deleted — they are ignored, so they cannot reach the repository.
- 2026-08-15 **Blocked, non-fatal:** `ISCC.exe` is not installed on this machine (searched Program
  Files, user-scope Programs, and the uninstall registry). The last local installer build was
  2026-06-18, so the toolchain has since been removed. **`installer/GoSurvey.iss` is therefore
  written but not compiled**, and the ISPP constructs it relies on (`#ifexist`, the `#include` of the
  generated version file, `/D` precedence) are unproven. Deferred to TASK-049, where the CI runner
  compiles it — which is also the machine whose result actually matters. Recorded so no one reads
  this task as evidence the script builds.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS. Clean configure + build; `build/GoSurvey.exe` produced; two
      build errors found and fixed en route (§8). Pre-existing deprecation warnings unchanged.
- [x] architecture-review  — PASS. No Workshop architectural decision: every structural choice was
      settled in ADR-029 before the task opened. No new layer, dependency, global state or
      data-format change. `Version.hpp` and `version.rc` are generated into `build/` (REQ-200).
- [x] code-review          — PASS. Three small runtime additions, each commented with its reason:
      the mutex (why it exists, why it is *not* single-instance enforcement, why handles leak by
      design), the NOMINMAX placement (why at the top), the ASCII-only resource string.
- [x] dependency-audit     — n-a. No dependency added, removed or moved.
- [x] performance-review   — n-a. Two `CreateMutexW` calls once at startup; no measured path
      touched.
- [x] testing              — `ctest`: **309/309 pass** (as of 2026-08-15). At the time this task
      was written it was 288/292; the 4 failures (#57 paper-circle stride, #241 mesh state-plane
      origin, #246 id sweep idempotence, #248 erased-id resolution) were shown here to be
      **pre-existing and not caused by this task** — by stashing every change, reconfiguring, and
      re-running: `ninja: no work to do` (the test binary links none of the files touched here)
      and the identical 4 failures. They were root-caused and fixed under TASK-049 Q1: em dashes
      in the `TEST_CASE` names, which `catch_discover_tests` mangles into a filter that matches
      nothing. No test was added by this task: it contains no pure
      function to test, and the acceptance conditions are observational (§6). The installer half is
      **unverified** — see the ISCC entry in §8.

## 10. Verification result
- Submitted:  2026-08-15
- Verdict:    <pending — awaiting Verification>
- Findings:   two self-found build defects, both fixed before submission (§8)

## 11. Outcome
- Requirements satisfied: REQ-077 (version-source clause only — the check itself is TASK-050);
                          REQ-078 (rename + `[InstallDelete]` clause only); REQ-202 (the `/D`
                          parameterization mechanism; the pipeline that uses it is TASK-049)
- Tests added:            none — see §9 testing
- Refactors:              executable renamed `GoSurvey-<version>.exe` → `GoSurvey.exe`
- Docs updated:           spec/{requirements,architecture,project,roadmap}.md (spec layer, prior to
                          this task); this log
- Technical debt:         (1) `installer/GoSurvey.iss` is uncompiled — no ISCC on this machine;
                          removal condition = TASK-049's first successful runner build.
                          (2) ~~The 4 pre-existing test failures block a clean `ctest`.~~
                          **CLEARED 2026-08-15** — root-caused (em dashes in `TEST_CASE` names,
                          mangled by `catch_discover_tests`) and fixed; 309/309 pass. See
                          TASK-049 Q1 and `coding-standards.md` §12.
- Done:                   <pending>
