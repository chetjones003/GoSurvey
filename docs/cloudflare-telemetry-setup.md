# Cloudflare telemetry backend setup (REQ-080)

The telemetry receiver's Worker source and full setup guide live in the private
**chetjones003/gosurveycad-site** repo, alongside the site and the other backend Workers:

- Source: `workers/telemetry/` in `gosurveycad-site`
- Setup guide: `docs/telemetry-worker-setup.md` in `gosurveycad-site`

The desktop client only ever talks to it by its deployed URL (`src/telemetry/TelemetryPing.hpp`),
never by file path, so this move needs no client rebuild.
