# Plan Review — GitHub Issue #807

- Date: 2026-11-06
- Scope: workshop plan and specification for the Surface contextual ribbon controls
- Review basis: `CLAUDE.md` §§2–5, `spec/requirements.md`, `spec/architecture.md`,
  `workshop/implementation-rules.md`, Autodesk source note in `docs/research/`

## Authority and acceptance

- All eleven Issue #807 controls are mapped to accepted requirements REQ-086 and REQ-508…516.
- REQ-143 defines the surface contextual tab, identifies the wired controls and preserves the
  disabled/unavailable behavior for every other unsupported control.
- REQ-145 remains the session-only `QUICKPROFILE`; the user explicitly approved the separate saved
  surface-profile workflow on 2026-11-06. REQ-513 and ADR-080 record that decision. The full design
  profile with vertical tangents/curves remains the distinct issue #1163 scope.
- Autodesk docs support the listed behaviors; the source note states where GoSurvey intentionally
  limits them, including non-corridor visibility and surface-local breakline corrections.

## Architecture and constraints

- ADR-080 gives ownership and persistence decisions for all new drawing data and project references.
- Cross-object links use stable IDs; surface query geometry remains authoritative; display
  simplification is render-only; shared source geometry is not mutated by a surface-specific fix.
- Project references reuse completed REQ-373…383 project behavior and preserve project lock/read-only
  rules. Point Files reuse REQ-086.
- Rendering work must preserve REQ-100 performance limits. Numeric surface results follow REQ-101.
  Recoverable failures follow REQ-201; unsupported ribbon controls continue to follow REQ-084/143.
- The beta baseline contains alignment support (REQ-480…502), project support (REQ-373…383), block
  references and tables. The research and plan are recorded on a feature branch based on current
  `origin/beta`.

## Risks and verification

- The change spans several drawing entities, rendering, project references and Qt UI. The ordered
  task phases keep each module inside its existing owner and require each control to stay disabled
  until its backing behavior is complete.
- Image handling now includes an explicit `IMAGEATTACH` path, so Drape Image is reachable without an
  unrelated feature dependency.
- Each accepted requirement has measurable success and failure criteria, persistence coverage where
  applicable, and an identified owner layer. The planned tests and benchmark cover those criteria.
- Product-documentation review and Qt screenshot presentation remain explicit pre-PR completion
  requirements.

## Verdict

```
PLAN VERDICT — ISSUE-807 — 2026-11-06
- Authority/acceptance: PASS
- Architecture/ownership: PASS
- Scope/sequencing: PASS
- Blocking findings: none
- Outcome: PASS — implementation may begin
```
