# Auth0 + Accounts Worker Setup

The REQ-091 / REQ-092 / ADR-037 identity and license-tier system: an Auth0 tenant handles sign-in
(Google, Microsoft, and email/username/password) and a Cloudflare Worker verifies the resulting
token to answer "what tier is this user on."

Everything you deploy on the Cloudflare side lives in **`tools/accounts-worker/`**. The Auth0 side
is configured in Auth0's dashboard — there is nothing to check into this repo for that part except
the two constants in `src/auth/AuthConfig.hpp` this guide ends with.

| | |
|---|---|
| **Your time** | ~20 minutes, once |
| **Cost** | $0 — Auth0's free plan covers 25,000 MAU and unlimited social + database connections; the Worker is the same $0 tier `telemetry-worker` already uses |
| **Prerequisites** | An Auth0 account (free, no card), a Cloudflare account, Node.js installed |

---

## Step 1 — Create the Auth0 tenant and application

1. Sign up at https://auth0.com (or sign in) and create a tenant if you don't have one.
2. **Applications → Create Application → Native.** This is the app type for a desktop app doing
   the system-browser + PKCE flow (ADR-037 (b)) — not "Single Page Application" or "Regular Web
   Application", which assume a browser or a server session this app doesn't have.
3. Under the new application's **Settings** (not Tenant Settings — this field lives on the
   Application itself, under **Application URIs**), set **Allowed Callback URLs** to these three
   exact entries (one per line/tag — press Enter or Space after each):
   ```
   http://127.0.0.1:53682/callback
   http://127.0.0.1:53683/callback
   http://127.0.0.1:53684/callback
   ```
   **Do not use a wildcard port** (`http://127.0.0.1:*/callback`) — Auth0 rejects it outright
   (`"callbacks" must be a valid uri`). Its own placeholder support is documented as "subdomain or
   domain name only," never the port, which is the opposite of what RFC 8252's ephemeral-port
   recommendation needs. `OAuthListener` binds a fixed port instead and tries these three in order
   at runtime (`kOAuthCallbackPorts` in `src/auth/AuthConfig.hpp`) until one is free on the user's
   machine — if you ever change that list, update this list to match, entry for entry.
4. Note the **Domain** and **Client ID** shown on this page. They go into `AuthConfig.hpp` in
   Step 5 — neither is secret (a native app's client id is public by construction; there is no
   client secret for this application type).

---

## Step 2 — Enable the three sign-in connections

**Authentication → Social**, enable:
- **Google** (google-oauth2) — Auth0's built-in dev keys work for testing; for production, follow
  Auth0's prompt to configure your own Google OAuth client.
- **Microsoft Account** (windowslive) — covers personal Outlook/Live accounts, same "use Auth0's
  dev keys to start, add your own for production" path.

**Authentication → Database**, confirm the default **Username-Password-Authentication** connection
is enabled — this is the email + username + password option and requires no external setup.

Under your application's **Connections** tab, confirm all three are toggled on for this specific
application (a tenant-level "enabled" social connection still needs enabling per-application).

---

## Step 3 — Register the accounts API (so tokens are verifiable JWTs)

Without this step, Auth0 issues an **opaque** access token for the default audience, which
`accounts-worker` cannot verify — REQ-092's whole mechanism depends on a real signed JWT.

1. **Applications → APIs → Create API.**
2. **Name:** `GoSurvey Accounts`. **Identifier:** `https://gosurvey-accounts/` (this exact string
   becomes both the client's `audience` request parameter and the Worker's `AUTH0_AUDIENCE` — it
   is an identifier, not a URL that needs to resolve to anything).
3. **Signing Algorithm: RS256.** This must be RS256, not HS256 — the Worker verifies signatures
   against the tenant's public JWKS (`accounts-worker/src/index.js`), which only works for an
   asymmetric algorithm. RS256 is Auth0's default for a new API.

---

## Step 4 — Deploy the accounts Worker

```bash
cd tools/accounts-worker
npx wrangler login          # same account telemetry-worker uses, if you have one already
npx wrangler d1 create gosurvey-accounts
```

Paste the printed `database_id` into `wrangler.toml`, then:

```bash
npm run schema               # creates the `users` table — requires --remote, which the script sets
```

Edit `wrangler.toml`'s `[vars]` block to match Step 1/3 exactly:

```toml
[vars]
AUTH0_DOMAIN = "your-tenant.us.auth0.com"      # from Step 1, no https://, no trailing slash
AUTH0_AUDIENCE = "https://gosurvey-accounts/"  # from Step 3, must match byte-for-byte
```

```bash
npm run deploy
```

It prints your endpoint:
```
https://gosurvey-accounts.<your-subdomain>.workers.dev
```
The license lookup path is `/v1/license` on that host.

---

## Step 5 — Point the client at it

Edit `src/auth/AuthConfig.hpp`:

```cpp
constexpr const char* kAuth0Domain   = "your-tenant.us.auth0.com";  // Step 1, no https://
constexpr const char* kAuth0ClientId = "the-client-id-from-step-1";
constexpr const char* kAuth0Audience = "https://gosurvey-accounts/"; // Step 3, byte-for-byte
```

Compile-time constants by design, same reasoning as `TelemetryEndpoint` (ADR-032 (f) / ADR-037):
a settings reset cannot disable or redirect sign-in. Changing any of them is a rebuild, never a
runtime config change — settle these before the first release that ships sign-in.

The `accounts-worker`'s base URL also needs to match in `kAccountsApiBaseUrl` (same file):

```cpp
constexpr const char* kAccountsApiBaseUrl = "https://gosurvey-accounts.<your-subdomain>.workers.dev";
```

The client calls `GET /v1/license` with the just-obtained access token after every successful
sign-in (interactive or silent refresh) — best-effort: a failed lookup (network hiccup, Worker
briefly down) is logged and does not undo the sign-in itself, since identity (REQ-091) and tier
(REQ-092) are separate concerns. Nothing in the app reads the returned tier for anything yet — the
call exists so a `users` row appears in `gosurvey-accounts` on sign-in, which is what makes the
Worker side of this observable without waiting for a feature that gates on it.

---

## Step 6 — Verify before relying on it

**The Auth0 side**, using the actual app: run GoSurvey, open Settings → System → Account, click
Sign In. The system browser should open Auth0's Universal Login showing Google, Microsoft, and a
username/password form. Complete any one of them; the browser tab should show "Sign-in complete,
you can close this tab," and the Settings panel should show "Signed in as `<email>`" within a
couple of seconds.

**The Worker side**, once you have a real access token (open the browser's dev tools during a
sign-in, or use Auth0's Dashboard → your API → Test tab to mint one against the accounts audience):

```bash
curl -i "https://gosurvey-accounts.<your-subdomain>.workers.dev/v1/license" \
  -H "Authorization: Bearer <token>"
```

Expected:
```
HTTP/2 200
{"tier":"free"}
```

And confirm rejection without a token:
```bash
curl -i "https://gosurvey-accounts.<your-subdomain>.workers.dev/v1/license"
# HTTP/2 401
```

---

## What is stored, and what is not

Per signed-in user, one row: `auth0_sub` (Auth0's stable identity claim), `email` (not currently
populated — see `schema.sql`'s comment), `tier` (defaults to `'free'`), `created_at`.

- **The refresh token never reaches this Worker or its database.** It lives only in the client's
  Windows Credential Manager (ADR-037 (c)); the Worker only ever sees a short-lived access token
  on each request.
- **No IP address is stored** — same policy as `telemetry-worker`, and `[observability]` is
  intentionally absent from `wrangler.toml` for the same reason.
- **Nothing is gated on `tier` yet.** REQ-091/092 ship identity and the lookup mechanism; which
  features check it, and how a user's tier ever becomes anything but `'free'`, are explicitly
  future work.

---

## Troubleshooting

**Sign-in browser opens but the app never shows "Signed in"** — check `AuthConfig.hpp`'s
`kAuth0Domain`/`kAuth0ClientId` are the exact values from Step 1, and that all three exact URLs
from Step 1.3 are present in Allowed Callback URLs, matching `kOAuthCallbackPorts` port-for-port.
A redirect_uri mismatch fails silently in the browser (Auth0 shows its own error page there, not
in the app) — "unauthorized" / "callback URL is not on the allowed list" there means a port drifted
out of sync between the two lists.

**All three candidate ports fail to bind** — something else on the machine holds all three
(unlikely, but possible). The interactive sign-in reports "could not start the local sign-in
listener on any candidate port"; add another candidate to `kOAuthCallbackPorts` **and** register
the matching URL in Auth0, then rebuild.

**`/v1/license` returns 401 even with what looks like a valid token** — the most common cause is
the token being the **ID token** instead of the **access token**, or an access token minted
without the `audience` parameter (an opaque token, not a JWT at all — REQ-092's Step 3 exists to
prevent this). Decode the token at https://jwt.io: its `aud` claim must equal `AUTH0_AUDIENCE`
exactly, and its header `alg` must be `RS256`.

**500 "server misconfigured"** — `AUTH0_DOMAIN` is missing from the deployed Worker's `[vars]`.
Re-check Step 4's `wrangler.toml` edit and redeploy.
