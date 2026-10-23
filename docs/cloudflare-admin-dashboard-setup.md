# Admin dashboard setup (REQ-402, ADR-068)

The owner-only dashboard over telemetry, accounts and crash/startup reports. Worker source is
`tools/admin-worker/`; this file is the operator-side setup that lives outside the build.

## What's already deployed

```bash
cd tools/admin-worker
npx wrangler deploy
```

This gives you:

- The `admin-api` Worker, bound read-only to both `gosurvey-telemetry` and `gosurveycad-db`.
- Route `admin.gosurveycad.com/*` on the `gosurveycad.com` zone.

At this point `admin.gosurveycad.com` has no DNS record, so nothing resolves yet — a Worker
route alone doesn't create one. Two manual steps remain; neither wrangler's OAuth token nor the
Cloudflare MCP connection used elsewhere in this project has the scope to do them, so they're
done by hand, once.

## One-time: DNS record

1. [dash.cloudflare.com](https://dash.cloudflare.com) → **gosurveycad.com** → **DNS** →
   **Add record**.
2. Type `AAAA`, Name `admin`, Content `100::` (Cloudflare's standard placeholder for a
   Worker-only route — no server actually listens there; the Worker intercepts the request
   before it would ever reach it), **Proxied: ON** (orange cloud).
3. Save.

Once this exists, the dashboard is **live but unprotected** — anyone who learns the subdomain
can load it. The next step gates it.

## One-time: Cloudflare Access policy

Wrangler's own login doesn't carry Zero Trust/Access write scope, and the Cloudflare MCP
integration used elsewhere in this project is read-only — so this step is done by hand in the
dashboard, once:

1. [dash.cloudflare.com](https://dash.cloudflare.com) → **Zero Trust** → **Access** →
   **Applications**.
2. **Add an application** → **Self-hosted**.
3. Application domain: `admin.gosurveycad.com`.
4. Policy: **Allow** → Include → **Emails** → your own email (`chetjones003@gmail.com`).
5. Session duration: your choice (e.g. 24h).
6. Save.

**Test:** visiting `admin.gosurveycad.com` should prompt an Access login (one-time code to your
email, or whatever identity provider you've configured) before the dashboard loads at all. A
rejected Access session never reaches the Worker — REQ-402's access-control requirement is
enforced entirely at Cloudflare's edge, not in `tools/admin-worker/src/index.js`.

## Redeploying after a code change

```bash
cd tools/admin-worker
npx wrangler deploy
```

No schema migration step — this Worker only reads the two existing databases
(`tools/telemetry-worker/schema.sql`, `worker/schema.sql`) and owns no tables of its own.

## Why a separate Worker, and why a subdomain

See ADR-068 in `spec/architecture.md` for the reasoning (read-only separation from the accounts
Worker, Access over an in-code password, subdomain over a path). In short: this Worker cannot
write to either database even if it had a bug, and a rejected Access request never executes a
line of this Worker's code.
