/**
 * GoSurvey telemetry receiver — REQ-080, ADR-032.
 *
 * A Cloudflare Worker that accepts the client's ping and appends one row to D1.
 * ADR-032 names this shape ("a Cloudflare Worker + KV/D1") and delegates the backend to the
 * operator; the client side is fixed by the ADR and this file must match it, not the reverse.
 *
 * Amended 2026-08-23 (D-2026-08-23-e): the ping is no longer unconditionally anonymous. When the
 * client is signed in (REQ-091) at ping time it includes `email`; when signed out, `email` is an
 * empty string and the row is exactly as anonymous as before. This was an explicit, user-directed
 * reversal of REQ-080's original "no PII" promise — see spec/requirements.md and the decision log.
 *
 * Plain JavaScript on purpose. Wrangler runs it with no build step and no node_modules, so
 * nobody needs a TypeScript toolchain to deploy a 120-line endpoint (CLAUDE.md rule 1).
 *
 * THE RESPONSE BODY IS PART OF THE CONTRACT. `TelemetryService.cpp` treats a 2xx alone as
 * inconclusive and requires the literal substring `"ok":true` in the body before it records the
 * ping as delivered. That check exists because the previous backend (Google Apps Script) could
 * not set a status code and answered 200 to failures for a day before anyone noticed. Keep
 * `{ ok: true }` first in the success object and do not pretty-print the JSON.
 */

const PING_PATH = '/v1/ping';

// Shapes, not just presence. The endpoint is public and unauthenticated (the same exposure the
// Sheets backend had), so anything that reaches D1 must be bounded first — an unvalidated string
// column is how a public writer turns into a storage bill.
const FIELDS = {
  installId: /^[A-Za-z0-9_-]{1,64}$/,
  event:     /^(install|active)$/,
  version:   /^[A-Za-z0-9.+_-]{1,32}$/,
  channel:   /^(stable|beta)$/,
  os:        /^[a-z0-9_-]{1,16}$/,
};

function json(body, status) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json',
      'cache-control': 'no-store',
    },
  });
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);

    if (url.pathname !== PING_PATH) {
      return json({ error: 'not found' }, 404);
    }
    if (request.method !== 'POST') {
      return json({ error: 'method not allowed' }, 405);
    }

    // Cap the read before parsing. Without this a single large body is an unbounded allocation.
    const declared = Number(request.headers.get('content-length') || '0');
    if (declared > 2048) {
      return json({ error: 'payload too large' }, 413);
    }

    let payload;
    try {
      payload = await request.json();
    } catch {
      return json({ error: 'body is not valid JSON' }, 400);
    }
    if (payload === null || typeof payload !== 'object' || Array.isArray(payload)) {
      return json({ error: 'body must be a JSON object' }, 400);
    }

    for (const [name, pattern] of Object.entries(FIELDS)) {
      const value = payload[name];
      if (typeof value !== 'string' || !pattern.test(value)) {
        // Name the offending field. A rejection the operator cannot diagnose from the response
        // is the failure mode this whole migration was fixing.
        return json({ error: `invalid or missing field: ${name}` }, 400);
      }
    }

    // REQ-080 amended 2026-08-23 (D-2026-08-23-e): the one deliberate, user-directed exception to
    // "no PII" — the REQ-091 signed-in email, sent only when the client actually has one. Not in
    // the strict FIELDS whitelist above because it's optional (an empty string is the ordinary
    // signed-out case, not a rejected request); a present-but-malformed value is dropped to NULL
    // rather than failing the whole ping, same tolerance gosurvey-accounts' email column uses.
    const rawEmail = typeof payload.email === 'string' ? payload.email : '';
    const email =
      rawEmail && rawEmail.length <= 320 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(rawEmail)
        ? rawEmail
        : null;

    const now = new Date();
    const ts = now.toISOString();
    const day = ts.slice(0, 10); // YYYY-MM-DD, the dedupe key

    // Server-derived, country-level only, and never sent by the client.
    // Deliberately NOT the IP address: coarse geography is useful, a client IP is personal data.
    const country = request.cf?.country ?? null;

    try {
      // INSERT OR IGNORE against the unique indexes in schema.sql makes a retry harmless: a
      // client that resends the same event for the same id (a lost reply, a double launch)
      // cannot inflate the counts. It does NOT dedupe a client that lost its stored id and
      // minted a new one — nothing server-side can, since that is by design a new identity.
      const result = await env.DB.prepare(
        `INSERT OR IGNORE INTO pings (ts, day, install_id, event, version, channel, os, country, email)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
      )
        .bind(ts, day, payload.installId, payload.event, payload.version,
              payload.channel, payload.os, country, email)
        .run();

      // `stored: false` means the row was a duplicate and was ignored. Still a success from the
      // client's point of view — the event is on record — so `ok` stays true and the client
      // will not retry.
      return json({ ok: true, stored: result.meta.changes === 1 }, 200);
    } catch (err) {
      // A 5xx here is honest: the client should not mark the ping delivered, and it will retry
      // on the next launch because it only persists its id on an acknowledged success.
      console.error('telemetry insert failed:', err);
      return json({ error: 'storage unavailable' }, 503);
    }
  },
};
