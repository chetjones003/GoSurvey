# Auth0 + accounts Worker setup (REQ-091/092, ADR-037)

The accounts Worker's source and full setup guide live in the private
**chetjones003/gosurveycad-site** repo, alongside the site and the other backend Workers:

- Source: `workers/accounts/` in `gosurveycad-site` (production, license-tier lookup)
- Source: `workers/accounts-profile/` in `gosurveycad-site` (newer, in-progress profile sync)
- Setup guide: `docs/auth0-accounts-setup.md` in `gosurveycad-site`

The Auth0 tenant itself is configured in Auth0's own dashboard, not in any repo. The desktop
client's two constants that must match it live in `src/auth/AuthConfig.hpp`.
