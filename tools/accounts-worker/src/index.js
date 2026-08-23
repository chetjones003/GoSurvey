/**
 * GoSurvey accounts worker — REQ-092, ADR-037.
 *
 * A Cloudflare Worker that verifies an Auth0-issued JWT and returns the caller's license tier,
 * inserting a default-tier row on first contact. Separate from `telemetry-worker` on purpose
 * (ADR-037 (e)): that endpoint is public and unauthenticated by design; this one requires a
 * verified bearer JWT on every request, the opposite trust model, and a defect in one must not
 * be able to read or corrupt the other's data.
 *
 * Plain JavaScript on purpose, same reasoning as telemetry-worker (CLAUDE.md rule 1): Wrangler
 * runs it with no build step and no node_modules. JWT verification is hand-rolled against
 * Auth0's JWKS using the Workers-native Web Crypto API — no JWT library — answered against
 * REQ-300 in ADR-037(a)/TASK-091: RS256 verification against a JWKS is a well-understood, small
 * amount of code, and adds no dependency.
 */

const LICENSE_PATH = '/v1/license';

function json(body, status) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json',
      'cache-control': 'no-store',
    },
  });
}

function base64UrlToBytes(b64url) {
  const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/');
  const padded = b64 + '='.repeat((4 - (b64.length % 4)) % 4);
  const binary = atob(padded);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function base64UrlDecodeJson(b64url) {
  return JSON.parse(new TextDecoder().decode(base64UrlToBytes(b64url)));
}

/**
 * Verifies an RS256 JWT against the tenant's JWKS: signature, expiry, issuer, and audience.
 * Returns `{ ok: true, sub }` or `{ ok: false, reason }` — the reason is for logs/debugging, not
 * returned to the caller (a 401 says only "unauthorized", never which check failed, so a probing
 * client learns nothing about why).
 */
async function verifyJwt(token, { domain, audience }) {
  const parts = token.split('.');
  if (parts.length !== 3) return { ok: false, reason: 'malformed token' };
  const [headerB64, payloadB64, signatureB64] = parts;

  let header, payload;
  try {
    header = base64UrlDecodeJson(headerB64);
    payload = base64UrlDecodeJson(payloadB64);
  } catch {
    return { ok: false, reason: 'malformed token segments' };
  }

  if (header.alg !== 'RS256') {
    // Refusing anything else — including 'none' — is the point: an algorithm-confusion attack
    // is exactly a token whose header claims something other than what the server expects.
    return { ok: false, reason: 'unsupported algorithm' };
  }

  let jwks;
  try {
    const jwksResponse = await fetch(`https://${domain}/.well-known/jwks.json`);
    if (!jwksResponse.ok) return { ok: false, reason: 'could not fetch signing keys' };
    jwks = await jwksResponse.json();
  } catch {
    return { ok: false, reason: 'signing key fetch failed' };
  }

  const jwk = jwks.keys?.find((k) => k.kid === header.kid);
  if (!jwk) return { ok: false, reason: 'unknown signing key' };

  let cryptoKey;
  try {
    cryptoKey = await crypto.subtle.importKey(
      'jwk', jwk, { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['verify']
    );
  } catch {
    return { ok: false, reason: 'could not import signing key' };
  }

  const signedData = new TextEncoder().encode(`${headerB64}.${payloadB64}`);
  const signature   = base64UrlToBytes(signatureB64);

  const valid = await crypto.subtle.verify(
    'RSASSA-PKCS1-v1_5', cryptoKey, signature, signedData
  );
  if (!valid) return { ok: false, reason: 'signature verification failed' };

  const now = Math.floor(Date.now() / 1000);
  if (typeof payload.exp !== 'number' || payload.exp <= now) {
    return { ok: false, reason: 'token expired' };
  }

  const expectedIssuer = `https://${domain}/`;
  if (payload.iss !== expectedIssuer) return { ok: false, reason: 'unexpected issuer' };

  if (audience) {
    const audiences = Array.isArray(payload.aud) ? payload.aud : [payload.aud];
    if (!audiences.includes(audience)) return { ok: false, reason: 'unexpected audience' };
  }

  if (!payload.sub) return { ok: false, reason: 'missing subject' };

  return { ok: true, sub: payload.sub };
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname !== LICENSE_PATH) return json({ error: 'not found' }, 404);
    if (request.method !== 'GET') return json({ error: 'method not allowed' }, 405);

    const authHeader = request.headers.get('authorization') || '';
    const match       = /^Bearer (.+)$/.exec(authHeader);
    if (!match) return json({ error: 'unauthorized' }, 401);

    if (!env.AUTH0_DOMAIN) {
      // A missing binding is an operator misconfiguration, not a client error — 401 here would
      // hide the real problem behind "your token is bad" when the token was never the issue.
      return json({ error: 'server misconfigured' }, 500);
    }

    const verified = await verifyJwt(match[1], {
      domain: env.AUTH0_DOMAIN,
      audience: env.AUTH0_AUDIENCE,
    });
    if (!verified.ok) return json({ error: 'unauthorized' }, 401);

    // The client's own already-authenticated user restating their email for display purposes —
    // not a new trust boundary. auth0_sub (from the verified JWT above) remains the only thing
    // this Worker keys or authorizes anything by; email is stored, never checked. Loosely
    // validated and length-capped so a malformed or hostile value is dropped (stored as NULL)
    // rather than rejecting the whole request over a display field.
    const rawEmail = url.searchParams.get('email');
    const email =
      rawEmail && rawEmail.length <= 320 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(rawEmail)
        ? rawEmail
        : null;

    try {
      // Upsert: a new auth0_sub gets a row with the default tier; an existing one has its email
      // refreshed but keeps its tier untouched (tier is deliberately absent from DO UPDATE SET).
      await env.DB.prepare(
        `INSERT INTO users (auth0_sub, email, tier, created_at)
         VALUES (?, ?, 'free', ?)
         ON CONFLICT(auth0_sub) DO UPDATE SET email = excluded.email`
      )
        .bind(verified.sub, email, new Date().toISOString())
        .run();

      const row = await env.DB.prepare('SELECT tier FROM users WHERE auth0_sub = ?')
        .bind(verified.sub)
        .first();
      return json({ tier: row.tier }, 200);
    } catch (err) {
      console.error('license lookup failed:', err);
      return json({ error: 'storage unavailable' }, 503);
    }
  },
};
