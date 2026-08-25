/**
 * Offline checks for the accounts worker's request handling.
 *
 *   node test.mjs
 *
 * No Cloudflare account, no network, no wrangler, and no real Auth0 tenant. A real RSA keypair is
 * generated in-process (Node's Web Crypto), used to sign test JWTs, and `fetch` is stubbed for
 * the one call the Worker makes — the JWKS lookup — to return that keypair's public half. D1 is
 * replaced by a stub, same pattern as telemetry-worker/test.mjs.
 *
 * This is not a substitute for a curl check against a real deployment with a real Auth0-issued
 * token, which nothing local can prove.
 */

import { webcrypto } from 'node:crypto';
import worker from './src/index.js';

const subtle = webcrypto.subtle;

let failures = 0;

function check(name, condition, detail) {
  if (condition) {
    console.log(`  ok    ${name}`);
  } else {
    failures++;
    console.log(`  FAIL  ${name}${detail ? ` — ${detail}` : ''}`);
  }
}

const DOMAIN   = 'test-tenant.auth0.local';
const AUDIENCE = 'https://gosurvey-accounts/';
const ISSUER   = `https://${DOMAIN}/`;
const KID      = 'test-key-1';

function base64url(bufOrArray) {
  return Buffer.from(bufOrArray).toString('base64url');
}

async function generateKeyPair() {
  return subtle.generateKey(
    { name: 'RSASSA-PKCS1-v1_5', modulusLength: 2048,
      publicExponent: new Uint8Array([1, 0, 1]), hash: 'SHA-256' },
    true,
    ['sign', 'verify']
  );
}

async function publicJwk(publicKey) {
  const jwk = await subtle.exportKey('jwk', publicKey);
  jwk.kid = KID;
  jwk.alg = 'RS256';
  jwk.use = 'sig';
  return jwk;
}

async function signJwt(privateKey, { header = {}, payload, corruptSignature = false }) {
  const fullHeader = { alg: 'RS256', typ: 'JWT', kid: KID, ...header };
  const headerB64  = base64url(JSON.stringify(fullHeader));
  const payloadB64 = base64url(JSON.stringify(payload));
  const signingInput = `${headerB64}.${payloadB64}`;

  let signatureBytes;
  if (corruptSignature) {
    signatureBytes = new Uint8Array(256); // well-formed length, wrong content — must fail verify
  } else {
    const sig = await subtle.sign(
      'RSASSA-PKCS1-v1_5', privateKey, new TextEncoder().encode(signingInput)
    );
    signatureBytes = new Uint8Array(sig);
  }
  return `${signingInput}.${base64url(signatureBytes)}`;
}

function stubFetchServingJwks(jwk) {
  const original = globalThis.fetch;
  globalThis.fetch = async (input) => {
    const url = typeof input === 'string' ? input : input.url;
    if (url === `https://${DOMAIN}/.well-known/jwks.json`) {
      return new Response(JSON.stringify({ keys: [jwk] }), {
        status: 200,
        headers: { 'content-type': 'application/json' },
      });
    }
    throw new Error(`unexpected fetch in test: ${url}`);
  };
  return () => { globalThis.fetch = original; };
}

/**
 * Stands in for the D1 binding, with enough real upsert semantics to test the tier-preserved /
 * email-refreshed behavior: `seed` pre-populates rows (auth0_sub -> {tier, email}); `.rows` is
 * exposed afterward for assertions the return value alone can't make (e.g. "was email stored").
 */
function stubDb({ seed = {}, throws = false } = {}) {
  const calls = [];
  const rows  = new Map(Object.entries(seed));
  return {
    calls,
    rows,
    prepare(sql) {
      return {
        bind(...params) {
          return {
            async first() {
              calls.push({ sql, params, op: 'first' });
              if (throws) throw new Error('simulated D1 outage');
              const [sub] = params;
              const row = rows.get(sub);
              return row ? { tier: row.tier } : null;
            },
            async run() {
              calls.push({ sql, params, op: 'run' });
              if (throws) throw new Error('simulated D1 outage');
              const [sub, email] = params;
              const existing = rows.get(sub);
              // Mirrors the real query's ON CONFLICT clause: tier is only ever set on first
              // insert ('free' default); an existing row keeps its tier and only email updates.
              rows.set(sub, existing ? { ...existing, email } : { tier: 'free', email });
              return { meta: { changes: 1 } };
            },
          };
        },
      };
    },
  };
}

function request({ path = '/v1/license', method = 'GET', token } = {}) {
  const headers = {};
  if (token !== undefined) headers['authorization'] = `Bearer ${token}`;
  return new Request(`https://example.workers.dev${path}`, { method, headers });
}

async function call(req, { db = stubDb(), domain = DOMAIN, audience = AUDIENCE } = {}) {
  const response = await worker.fetch(req, { DB: db, AUTH0_DOMAIN: domain, AUTH0_AUDIENCE: audience });
  const text = await response.text();
  let body = null;
  try { body = JSON.parse(text); } catch { /* left null */ }
  return { status: response.status, text, body, db };
}

const { publicKey, privateKey } = await generateKeyPair();
const jwk = await publicJwk(publicKey);
const unstubFetch = stubFetchServingJwks(jwk);

const now = Math.floor(Date.now() / 1000);
const validPayload = { sub: 'auth0|abc123', iss: ISSUER, aud: AUDIENCE, exp: now + 3600, iat: now };

console.log('\nrouting');
{
  const r = await call(request({ path: '/' }));
  check('bare root is 404', r.status === 404, `got ${r.status}`);
}
{
  const r = await call(request({ path: '/v1/licenses' }));
  check('near-miss path is 404', r.status === 404, `got ${r.status}`);
}
{
  const r = await call(request({ method: 'POST', token: 'whatever' }));
  check('POST is 405', r.status === 405, `got ${r.status}`);
}

console.log('\nrejects missing/invalid/expired tokens (never reaching D1)');
{
  const r = await call(request({}));
  check('no Authorization header is 401', r.status === 401, `got ${r.status}`);
  check('no D1 query ran', r.db.calls.length === 0, `${r.db.calls.length} calls`);
}
{
  const r = await call(request({ token: 'not-a-jwt-at-all' }));
  check('malformed token is 401', r.status === 401, `got ${r.status}`);
  check('no D1 query ran', r.db.calls.length === 0, `${r.db.calls.length} calls`);
}
{
  const token = await signJwt(privateKey, { payload: validPayload, corruptSignature: true });
  const r = await call(request({ token }));
  check('tampered signature is 401', r.status === 401, `got ${r.status}`);
  check('no D1 query ran', r.db.calls.length === 0, `${r.db.calls.length} calls`);
}
{
  const token = await signJwt(privateKey, { payload: { ...validPayload, exp: now - 10 } });
  const r = await call(request({ token }));
  check('expired token is 401', r.status === 401, `got ${r.status}`);
  check('no D1 query ran', r.db.calls.length === 0, `${r.db.calls.length} calls`);
}
{
  const token = await signJwt(privateKey, { payload: { ...validPayload, iss: 'https://someone-elses-tenant.auth0.com/' } });
  const r = await call(request({ token }));
  check('wrong issuer is 401', r.status === 401, `got ${r.status}`);
}
{
  const token = await signJwt(privateKey, { payload: { ...validPayload, aud: 'https://someone-elses-api/' } });
  const r = await call(request({ token }));
  check('wrong audience is 401', r.status === 401, `got ${r.status}`);
}
{
  const token = await signJwt(privateKey, { header: { alg: 'none' }, payload: validPayload });
  const r = await call(request({ token }));
  check('alg:none is rejected, not trusted', r.status === 401, `got ${r.status}`);
}
{
  const token = await signJwt(privateKey, { payload: { ...validPayload, sub: undefined } });
  const r = await call(request({ token }));
  check('missing subject is 401', r.status === 401, `got ${r.status}`);
}

console.log('\naccepts a valid token');
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const r = await call(request({ token }), { db: stubDb() });
  check('status 200', r.status === 200, `got ${r.status}`);
  check('new user gets the default tier', r.body?.tier === 'free', r.text);

  const upsert = r.db.calls.find((c) => c.op === 'run');
  check('upserted the same auth0_sub', upsert?.params[0] === validPayload.sub);
  check('email is null when none was sent', r.db.rows.get(validPayload.sub)?.email === null);
}
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const r = await call(request({ token }),
                      { db: stubDb({ seed: { [validPayload.sub]: { tier: 'pro', email: null } } }) });
  check('existing user returns their stored tier, not the default', r.body?.tier === 'pro', r.text);
}

console.log('\nemail (display-only; never a trust boundary)');
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const path  = `/v1/license?email=${encodeURIComponent('surveyor@example.com')}`;
  const r = await call(request({ token, path }), { db: stubDb() });
  check('status 200', r.status === 200, `got ${r.status}`);
  check('email is stored for a new user',
        r.db.rows.get(validPayload.sub)?.email === 'surveyor@example.com');
}
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const path  = `/v1/license?email=${encodeURIComponent('new-address@example.com')}`;
  const r = await call(request({ token, path }), {
    db: stubDb({ seed: { [validPayload.sub]: { tier: 'pro', email: 'old-address@example.com' } } }),
  });
  check('tier is preserved across an email change', r.body?.tier === 'pro', r.text);
  check('email is refreshed on repeat sign-in',
        r.db.rows.get(validPayload.sub)?.email === 'new-address@example.com');
}
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const path  = `/v1/license?email=${encodeURIComponent('not-an-email')}`;
  const r = await call(request({ token, path }), { db: stubDb() });
  check('status 200 — a malformed email does not fail the whole request', r.status === 200,
        `got ${r.status}`);
  check('malformed email is dropped (stored as null), not stored as garbage',
        r.db.rows.get(validPayload.sub)?.email === null);
}

console.log('\nisolation from telemetry (separate store, no ip, no extra data)');
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const r = await call(request({ token }));
  const bound = JSON.stringify(r.db.calls.map((c) => c.params));
  check('no ip address is ever bound', !/\d+\.\d+\.\d+\.\d+/.test(bound), bound);
}

console.log('\nstorage outage');
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const r = await call(request({ token }), { db: stubDb({ throws: true }) });
  check('D1 failure is 503', r.status === 503, `got ${r.status}`);
}

console.log('\nmisconfiguration');
{
  const token = await signJwt(privateKey, { payload: validPayload });
  const r = await call(request({ token }), { domain: '' });
  check('missing AUTH0_DOMAIN binding is 500, not misreported as unauthorized', r.status === 500,
        `got ${r.status}`);
}

unstubFetch();

console.log(failures === 0
  ? '\nall checks passed\n'
  : `\n${failures} check(s) FAILED\n`);
process.exit(failures === 0 ? 0 : 1);
