# Beta rolling version plan review — REQ-202 / ADR-074

**Review type:** Verification self-review before Workshop implementation.
**Verdict:** APPROVE.

## Authority and acceptance

REQ-202 owns CI release production. D-2026-11-02-a and ADR-074 amend its prior manual-only
beta policy. The observable outcomes are:

- A push to `beta` builds, tests, and replaces assets on the single `channel-beta` release.
- Other branch pushes do not start this release workflow; stable publishing remains manual and
  version-gated.
- A newly published beta source commit gets a plain patch version one above the higher of current
  stable and previous beta versions. A rerun for the same source commit reuses the version.
- Stable `project(VERSION)`, its `v<version>` tag, installer version and stable manifest stay equal.
- Each beta manifest includes the source commit, and installer, app version, and manifest agree.
- A failing test suite blocks publication.

## Architectural impact

Build/Platform owns the release workflow and version metadata. CMake's stable `project(VERSION)`
remains the stable source of truth; CI supplies a computed beta version at configure time. The beta
manifest adds one optional, beta-only provenance field. The application updater already parses
individual manifest keys and ignores unrelated fields, so this additive field does not change its
interface. No runtime layer, application dependency, or stable-release policy is added.

## Risks and required handling

1. **Version monotonicity:** compare numeric version components, not strings; when stable advances
   above the last beta, start at stable patch + 1.
2. **Idempotency:** read the rolling manifest's source commit and reuse its version for an identical
   commit; reject malformed version/provenance data rather than publishing a guessed version.
3. **Publish safety:** compute version before build, but replace the rolling assets only after build
   and tests succeed. Keep beta publication serialized so an older build cannot overwrite a newer
   one after it completes.
4. **Version parity:** pass the computed beta version to CMake and Inno Setup; assert the generated
   app version, installer `AppVersion`, and manifest version are identical.
5. **Master transition:** resolve the stable version from current `master`, not a possibly stale
   beta checkout; this lets the first new beta commit after stable `0.8.0` receive `0.8.1`.

## Verification plan

- Happy path: beta push with master `0.7.8` and no beta manifest publishes `0.7.9`; a subsequent
  beta source commit publishes `0.7.10`; master `0.8.0` followed by a new beta commit publishes
  `0.8.1`.
- Idempotency: rerun for the same source commit and verify the same version is retained.
- Failure mode: malformed prior manifest or a failing test suite produces no release-asset update.
- Regression: feature-branch pushes remain quiet; manual master dispatch still skips an existing
  stable tag and publishes a newly bumped stable version once.
- Static check: `git diff --check`; workflow syntax and configured version paths are reviewed.

## Plan

1. Update `.github/workflows/release.yml` to trigger on beta pushes while retaining manual dispatch,
   fetch the latest stable and rolling beta state, calculate an idempotent version, and record the
   source commit in the beta manifest.
2. Let CMake accept an optional CI-computed project version, defaulting to the checked-in stable
   version; leave stable configure behavior unchanged.
3. Pass the resolved version through Windows VERSIONINFO, installer, release title, and manifest;
   keep the stable tag gate unchanged.
4. Review the diff and run only checks authorized by the task/runtime instructions.

No new dependency or runtime abstraction is required. **Plan verdict: APPROVE.**
