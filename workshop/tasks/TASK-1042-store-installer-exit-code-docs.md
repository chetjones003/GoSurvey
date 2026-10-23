# TASK-1042 — Document Microsoft Store installer exit codes

## Authority

- Accepted requirement: REQ-453 (Microsoft Store installs run without installer UI).
- User request: document the installer's exit codes so Partner Center can link to the required
  miscellaneous return-code documentation.

## Scope

- Add `docs/installer-exit-codes.md` with Inno Setup's documented exit codes and a truthful mapping
  to Partner Center's standard scenarios.
- Do not change installer behavior or invent scenario-specific exit codes that the EXE does not
  return.

## Approach

- Record the standard Inno Setup process exit codes from the Inno Setup documentation.
- Explain which Partner Center scenarios have no distinct GoSurvey installer code.
- Provide the stable raw GitHub URL after this documentation merges to `beta`.

## Verification

- Check every listed code and meaning against Inno Setup's official Setup Exit Codes page.
- Confirm the scenario mapping does not assign duplicate or fabricated codes.
- Documentation-only change; no build or runtime tests apply.

## Plan review

REQ-453 establishes the Store installer invocation and payload, while this addition documents the
existing installer contract without changing it. Some Partner Center categories cannot be mapped
one-to-one because Inno Setup returns general process outcomes. Stating that limitation avoids
misleading Store diagnostics.
