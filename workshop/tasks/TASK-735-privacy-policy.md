# TASK-735 — Publish GoSurvey privacy policy

- Type: docs
- Status: done
- Opened: 2026-10-22
- Owner: Codex

## 1. Authority
- Goal: Documentation / Website (REQ-452)
- Requirements: REQ-452 — accepted
- Constraints: Document current behavior only; do not invent retention periods or a contact address. Use the privacy contact address supplied by the user.
- Acceptance:
  - `PRIVACY.md` exists at the repository root and covers the listed data flows in plain language.
  - The website has an About page with a visible Privacy section that presents the policy and links to the repository copy.
  - The main website links to that About page.
  - Statements are checked against current implementation/configuration; unknown contact or retention details are marked rather than guessed.
- Owning subsystem: Documentation / Website

## 2. Scope
- In scope: Root policy, a site About page containing the Privacy section, a link from the main site, and the project decision record.
- Out of scope: Changing telemetry, diagnostic collection, authentication, hosting, retention configuration, or the product's privacy behavior.
- Smallest change: Write an accurate plain-language policy in Markdown and a matching website presentation using existing site styles.

## 3. Architectural boundary check
- No new application layer, dependency, public API, data format, or runtime behavior. Static documentation only.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Approve adding a requirement for the repository policy and website Privacy section, including accurate disclosure of existing data flows? | 2026-10-22 | Approved by user; assigned REQ-452 after finding REQ-400 already used on `beta`. |

## 5. Assumptions
- The current single-page `site/index.html` is the main website; because it has no About page, add `site/about.html` and link to it from the footer.
- No project privacy contact address or retention schedule is documented; mark those gaps in the policy.

## 6. Plan
- Approach: Verify the policy claims against implementation and provider configuration, then author matching repository and website policy text.
- Files/functions to touch: `PRIVACY.md`, `site/about.html`, `site/index.html`, `site/sitemap.xml`, `spec/requirements.md`, `spec/project.md`, this task log.
- Test approach: Manually inspect policy claims against source/configuration; validate HTML structure and local links. No application tests apply.
- Steps:
  - [x] Add accepted REQ-452 and decision log entry.
  - [x] Complete Verification review before policy/site implementation.
  - [x] Add the policy and About page and link.
  - [x] Check consistency, links, and disclosure against code/configuration.

## 7. Workflow-specific notes
- Docs: Every statement must be cross-checked against current code/spec; unknown retention/contact facts remain explicitly identified.

## 8. Implementation log
- 2026-10-22: User approved scope; REQ-452 and D-2026-10-22-c recorded to avoid identifiers already used on `beta`.
- 2026-10-22: Added `PRIVACY.md`, `site/about.html`, the main-site footer link, and the sitemap entry. Static required-content and link checks passed; `git diff --check` passed.
- 2026-10-22: Added the user-supplied `chet@gosurveycad.com` contact to both policy copies; placeholder check and `git diff --check` passed.
- 2026-10-22: Corrected the website hosting disclosure to Cloudflare in both policy copies and clarified the separate GitHub API and Google Fonts requests.

## 9. Self-verification
- [ ] build-project — n/a
- [x] architecture-review — PASS; static documentation/site only, no runtime boundary changes.
- [x] code-review — PASS; privacy statements checked against telemetry, diagnostic, auth, update, map, and website sources.
- [ ] dependency-audit — n/a
- [ ] performance-review — n/a
- [x] testing — PASS; XML sitemap parsed, About/Privacy/repository links and required policy disclosures checked; `git diff --check` passed.

## 10. Verification result
- Submitted: 2026-10-22
- Verdict: APPROVE
- Findings: none. Static docs/site work respects the documentation boundary, adds no dependency or runtime behavior, and has observable link/content acceptance checks. Policy must disclose that network providers may process request metadata and that diagnostic reports can contain local paths; a fixed retention promise is not supported by configuration.

## 11. Outcome
- Requirements satisfied: REQ-452 (Acceptance met: yes)
- Tests added: none
- Refactors: none
- Docs updated: `PRIVACY.md`, `site/about.html`, `site/index.html`, `site/sitemap.xml`, `spec/requirements.md`, `spec/project.md`.
- Done: 2026-10-22
