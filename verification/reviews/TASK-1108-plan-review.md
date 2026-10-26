# TASK-1108 Plan Review — stable manifest byte parsing

**Review type:** Self-review against the Verification and architecture checklists; delegated review is disabled in this run.

## Evidence and finding

The stable manifest endpoint currently returns a valid v0.7.5 manifest. On PowerShell 7.6.9, however, the exact installer request (`Invoke-WebRequest` with `-UseBasicParsing`) returns `application/octet-stream` and a `System.Byte[]` body. Passing that body directly through `ConvertFrom-Json` produces `System.Object[]` with no `channel`, which triggers the installer's existing stable-channel rejection. Decoding the same bytes as UTF-8 before parsing yields `version=0.7.5`, `channel=stable`.

## Plan assessment

- **Specification:** REQ-454 requires the one-line bootstrap to install the latest stable release and to stop clearly on invalid manifest data. Parsing transport bytes as JSON preserves both requirements and keeps the stable-channel guard strict.
- **Architecture and scope:** The Build/Platform bootstrap already owns manifest consumption. A local conversion helper is within that subsystem. No new layer, public API, manifest format, dependency, or channel-selection policy is introduced.
- **Regression protection:** A deterministic PowerShell test will exercise the helper extracted from the actual installer source with both byte-array and string JSON, and assert malformed JSON remains rejected. Windows full validation will run the test.
- **Risks:** UTF-8 decoding changes only byte-backed response handling; malformed JSON still fails parsing, and beta manifests remain rejected by the unchanged stable-channel check.

**Plan verdict: PASS.** Add the regression test before the fix, then implement only the content normalization and invoke the test in Windows validation.
