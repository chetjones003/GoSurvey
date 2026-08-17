# Cloudflare Telemetry Backend Setup

The REQ-080 telemetry receiver: a Cloudflare Worker that writes each anonymous ping to a D1
(SQLite) database. This replaces the Google Sheets backend — see
[Why we moved](#why-we-moved-off-google-sheets) at the end.

Everything you deploy lives in **`tools/telemetry-worker/`** in this repo, so the endpoint is
version-controlled alongside the client that talks to it.

| | |
|---|---|
| **Your time** | ~10 minutes, once |
| **Cost** | £0 — comfortably inside the free tier at any plausible scale for this product |
| **Prerequisites** | A Cloudflare account (free, no card) and Node.js installed |

---

## Why D1 and not KV

An earlier draft of this setup suggested Workers KV. Don't use it for this.

**KV's free tier allows 1,000 writes per day.** Every telemetry ping is a write, so that ceiling
is hit at roughly 1,000 daily active users — precisely the scale this telemetry exists to detect.
You would lose data at the exact moment the numbers started to matter, and silently.

D1's free tier allows on the order of 100,000 row writes per day and 5 GB of storage, and it
answers "how many unique installs" as a `SELECT COUNT(DISTINCT …)` rather than by listing every
key and counting client-side. For append-and-aggregate workloads, KV is the wrong shape and D1 is
the right one.

> Cloudflare adjusts free-tier limits from time to time. The numbers above were correct when this
> was written; check the current plan page before assuming headroom.

---

## Step 1 — Sign in to Cloudflare from the CLI

```bash
cd tools/telemetry-worker
npx wrangler login
```

This opens a browser to authorise the CLI. If you have no Cloudflare account yet, create one
first at https://dash.cloudflare.com/sign-up — the free plan is all this needs.

Confirm it worked:

```bash
npx wrangler whoami
```

---

## Step 2 — Create the D1 database

```bash
npx wrangler d1 create gosurvey-telemetry
```

It prints a block ending in a `database_id`. **Copy that id into `wrangler.toml`**, replacing
`PASTE_DATABASE_ID_HERE`:

```toml
[[d1_databases]]
binding = "DB"
database_name = "gosurvey-telemetry"
database_id = "the-uuid-it-printed"
```

The id is not a secret — it is useless without your account credentials — so commit it. That
keeps the deploy reproducible for anyone who clones the repo.

---

## Step 3 — Create the table

```bash
npx wrangler d1 execute gosurvey-telemetry --remote --file=./schema.sql
```

**`--remote` matters.** Without it wrangler applies the schema to a *local* simulator database
and the deployed Worker sees nothing — every ping then fails with `storage unavailable`, and the
cause is invisible because the command reported success.

Verify the table is really there, in the real database:

```bash
npx wrangler d1 execute gosurvey-telemetry --remote --command "SELECT name FROM sqlite_master WHERE type='table'"
```

You should see `pings`.

---

## Step 4 — Deploy the Worker

```bash
npx wrangler deploy
```

It prints your endpoint:

```
https://gosurvey-telemetry.<your-subdomain>.workers.dev
```

**Save that URL.** The telemetry endpoint is that plus the path `/v1/ping`:

```
https://gosurvey-telemetry.<your-subdomain>.workers.dev/v1/ping
```

---

## Step 5 — Verify before touching the client

Run this signed out, from any shell, substituting your URL:

```bash
curl -i -X POST "https://gosurvey-telemetry.<your-subdomain>.workers.dev/v1/ping" \
  -H "Content-Type: application/json" \
  -d '{"installId":"curltest","event":"install","version":"0.5.0","channel":"stable","os":"windows"}'
```

Expected — note the status code is real this time:

```
HTTP/2 200
{"ok":true,"stored":true}
```

Then confirm the row actually landed, which is the part a status code alone never proves:

```bash
npx wrangler d1 execute gosurvey-telemetry --remote --command "SELECT * FROM pings"
```

Two more checks worth doing once, because they prove the validation works rather than just the
happy path:

```bash
# Rejected: bad channel. Expect 400 and a message naming the field.
curl -s -X POST "https://…/v1/ping" -H "Content-Type: application/json" \
  -d '{"installId":"x","event":"install","version":"1.0","channel":"nope","os":"windows"}'

# Ignored as a duplicate: same id and event as the first call. Expect {"ok":true,"stored":false}
# and still only one install row in the table.
curl -s -X POST "https://…/v1/ping" -H "Content-Type: application/json" \
  -d '{"installId":"curltest","event":"install","version":"0.5.0","channel":"stable","os":"windows"}'
```

Unlike the Apps Script backend, `-X POST` is safe here — the Worker answers directly with no
redirect, so there is no method-rewriting trap.

Finally, delete the test rows:

```bash
npx wrangler d1 execute gosurvey-telemetry --remote --command "DELETE FROM pings WHERE install_id = 'curltest'"
```

---

## Step 6 — Point the client at it

Send the URL over and it goes into `TelemetryEndpoint` in `src/telemetry/TelemetryPing.hpp`,
followed by a rebuild. It is a compile-time constant by design (ADR-032 (f)) so that a settings
reset cannot disable telemetry and a user preference cannot redirect it — which also means
**changing the endpoint is always a rebuild and a re-release**, never a config change.

That is the whole reason to settle on the final URL now, before shipping: if you ever want
`telemetry.gosurvey.app` instead of `*.workers.dev`, add the custom domain *before* the first
release, not after.

---

## Viewing your data

For the two numbers that matter, from `tools/telemetry-worker/`:

```bash
npm run stats
```

```
GoSurvey telemetry — 2026-08-17

  Installs (all time)          1
  Active users (7 days)        0
  Active users (30 days)       0
```

It goes through your own `wrangler login`. That is deliberate — the alternative, a `/stats` route
on the Worker, would publish your adoption numbers to anyone who guessed the URL unless it were
put behind Cloudflare Access first.

`queries.sql` holds the rest — installs over time, DAU, version distribution, channel split,
30-day retention, and geography. Run one with:

```bash
npx wrangler d1 execute gosurvey-telemetry --remote --command "SELECT COUNT(*) AS installs FROM pings WHERE event = 'install'"
```

For anything returning more than a couple of rows, the D1 console in the Cloudflare dashboard
(**Workers & Pages → D1 → gosurvey-telemetry → Console**) is far easier to read.

To pull everything out — for a spreadsheet, or to leave Cloudflare later:

```bash
npx wrangler d1 export gosurvey-telemetry --remote --output=telemetry-backup.sql
```

---

## What is stored, and what is not

Per ping, one row: timestamp, day, install id, event, version, channel, os, country.

- **The install id is the only identifier**, and it is a random 128-bit value generated on the
  client. It is not derived from hardware, user, or machine name, and it cannot be reversed into
  anything about the person.
- **No IP address is stored.** The Worker reads Cloudflare's country code and nothing finer. A
  client IP is personal data under GDPR; a country code is not. The previous backend tried to log
  IPs and this one deliberately does not.
- **Request logging is off** (`[observability]` is intentionally absent from `wrangler.toml`).
  Cloudflare's Workers Logs would retain request metadata including client IPs, which would
  reintroduce exactly what the line above removes. If you enable it to debug something, turn it
  off again afterwards.

REQ-080 fixes the client payload at exactly five fields and forbids PII. `country` is derived
server-side and is not part of the payload, so it sits on the right side of that line — but if
you would rather not have it at all, drop the column from `schema.sql` and the two references in
`src/index.js`.

---

## Troubleshooting

**`storage unavailable` (503)** — the Worker is running but cannot reach the table. Almost always
Step 3 was run without `--remote`. Re-run it with the flag and check `sqlite_master` as shown.

**`not found` (404)** — the path is wrong. The endpoint is `/v1/ping`; the bare worker root
returns 404 by design, so a browser visit showing "not found" is the Worker working correctly.

**`invalid or missing field: <name>`** (400) — the payload failed validation, and the message
names the field. `channel` must be exactly `stable` or `beta`; `event` exactly `install` or
`active`.

**`{"ok":true,"stored":false}`** — not an error. The row was a duplicate and the unique index
ignored it. The client treats this as success and will not retry, which is intended.

**Nothing at all from the app, but curl works** — the app pings once per install and then at most
once per 24 hours, so a second launch the same day sends nothing. To force a fresh `install`
ping, delete the `installId` and `lastActivePingDate` keys from `gosurvey-user.json` (in
`%APPDATA%\GoSurvey\`) and relaunch. If it still sends nothing, check stderr for
`[telemetry] ping failed:` — the client logs the reason and the response body.

---

## Why we moved off Google Sheets

Not because Sheets stopped working — it was fixed and did work (BUG-020, BUG-021). The reasons
it was still the wrong backend:

1. **Apps Script cannot set an HTTP status code.** Every failure — a thrown handler, a sign-in
   redirect, a missing sheet — answered `200`. The client had to be taught to parse the response
   body to tell success from failure. A Worker returns real status codes.
2. **~1,000 requests/day, per Google account**, shared with anything else that account runs.
3. **Counting required reading the whole sheet.** "How many unique installs" is a `COUNT(DISTINCT)`
   in D1 and a spreadsheet formula over every row in Sheets.
4. **No validation.** A public unauthenticated endpoint writing unbounded strings into a document
   with no schema, no type checks, and no dedupe.

The client-side hardening from that episode is kept, not reverted: `TelemetryService` still
requires the literal `"ok":true` acknowledgement rather than trusting a 2xx. It costs nothing and
it is the check that would have caught the original failure on day one.

The Sheets setup guide has been deleted now that this one supersedes it. The Apps Script
deployment itself still exists, so a rollback is still a one-line change to `TelemetryEndpoint`
plus a rebuild; the guide is recoverable from git history (`docs/google-sheets-setup.md`, removed
2026-08-17) if it is ever actually needed.
