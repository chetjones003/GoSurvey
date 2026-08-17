/**
 * Offline checks for the telemetry Worker's request handling.
 *
 *   node test.mjs
 *
 * No Cloudflare account, no network, no wrangler. D1 is replaced by a stub that records the
 * bound parameters, which is enough to pin the two things worth pinning: that a bad request is
 * refused with a useful message, and that a good one produces exactly the row the schema
 * expects, in the right column order.
 *
 * This is not a substitute for the curl checks in docs/cloudflare-telemetry-setup.md — those
 * prove the deployment is reachable and the table exists, which nothing local can.
 */

import worker from './src/index.js';

let failures = 0;

function check(name, condition, detail) {
  if (condition) {
    console.log(`  ok    ${name}`);
  } else {
    failures++;
    console.log(`  FAIL  ${name}${detail ? ` — ${detail}` : ''}`);
  }
}

/** Stands in for the D1 binding. `changes` drives the duplicate-ignored path. */
function stubDb({ changes = 1, throws = false } = {}) {
  const calls = [];
  return {
    calls,
    prepare(sql) {
      return {
        bind(...params) {
          return {
            async run() {
              if (throws) throw new Error('simulated D1 outage');
              calls.push({ sql, params });
              return { meta: { changes } };
            },
          };
        },
      };
    },
  };
}

const VALID = {
  installId: 'a32726f38ebab41d419916381cb4f5b4',
  event: 'install',
  version: '0.5.0',
  channel: 'stable',
  os: 'windows',
};

function post(body, { path = '/v1/ping', method = 'POST' } = {}) {
  const payload = typeof body === 'string' ? body : JSON.stringify(body);
  return new Request(`https://example.workers.dev${path}`, {
    method,
    headers: { 'content-type': 'application/json' },
    body: method === 'GET' ? undefined : payload,
  });
}

async function call(request, db = stubDb()) {
  const response = await worker.fetch(request, { DB: db });
  const text = await response.text();
  let parsed = null;
  try {
    parsed = JSON.parse(text);
  } catch { /* left null; a non-JSON body is itself a failure the assertions will catch */ }
  return { status: response.status, text, body: parsed, db };
}

console.log('\nrouting');
{
  const r = await call(post(VALID, { path: '/' }));
  check('bare root is 404', r.status === 404, `got ${r.status}`);
}
{
  const r = await call(post(VALID, { path: '/v1/pings' }));
  check('near-miss path is 404', r.status === 404, `got ${r.status}`);
}
{
  const r = await call(post(VALID, { method: 'GET' }));
  check('GET is 405', r.status === 405, `got ${r.status}`);
}

console.log('\nrejects bad input');
{
  const r = await call(post('not json at all'));
  check('non-JSON body is 400', r.status === 400, `got ${r.status}`);
}
{
  const r = await call(post('[1,2,3]'));
  check('JSON array is 400', r.status === 400, `got ${r.status}`);
}
for (const [field, bad] of [
  ['installId', ''],
  ['installId', 'has spaces'],
  ['event', 'uninstall'],
  ['channel', 'nightly'],
  ['version', 'x'.repeat(33)],
  ['os', 'WINDOWS'],
]) {
  const r = await call(post({ ...VALID, [field]: bad }));
  const named = r.body?.error?.includes(field);
  check(`${field}=${JSON.stringify(bad).slice(0, 20)} is 400 naming the field`,
        r.status === 400 && named, `got ${r.status} ${r.text}`);
}
{
  const r = await call(post({ installId: 'abc', event: 'install' }));
  check('missing fields is 400', r.status === 400, `got ${r.status}`);
}

console.log('\naccepts a valid ping');
{
  const r = await call(post(VALID));
  check('status 200', r.status === 200, `got ${r.status}`);
  check('body carries the ok:true the C++ client greps for',
        r.text.includes('"ok":true'), r.text);
  check('reports the row as stored', r.body?.stored === true, r.text);
  check('one insert issued', r.db.calls.length === 1, `${r.db.calls.length}`);

  const { sql, params } = r.db.calls[0];
  check('insert is OR IGNORE', /INSERT OR IGNORE/.test(sql));
  check('binds 8 columns', params.length === 8, `${params.length}`);

  const [ts, day, installId, event, version, channel, os, country] = params;
  check('ts is ISO-8601 Z', /^\d{4}-\d{2}-\d{2}T[\d:.]+Z$/.test(ts), ts);
  check('day is the date part of ts', day === ts.slice(0, 10), `${day} vs ${ts}`);
  check('day is YYYY-MM-DD', /^\d{4}-\d{2}-\d{2}$/.test(day), day);
  check('installId passed through', installId === VALID.installId);
  check('event passed through', event === VALID.event);
  check('version passed through', version === VALID.version);
  check('channel passed through', channel === VALID.channel);
  check('os passed through', os === VALID.os);
  check('country is null without a cf object', country === null, String(country));
}

console.log('\nprivacy');
{
  const r = await call(post(VALID));
  const bound = JSON.stringify(r.db.calls[0].params);
  check('no ip address is bound', !/\d+\.\d+\.\d+\.\d+/.test(bound), bound);
  check('nothing bound beyond the 8 schema columns', r.db.calls[0].params.length === 8);
}

console.log('\nduplicates and outages');
{
  const r = await call(post(VALID), stubDb({ changes: 0 }));
  check('ignored duplicate is still 200', r.status === 200, `got ${r.status}`);
  check('ignored duplicate still says ok:true (client must not retry)',
        r.text.includes('"ok":true'), r.text);
  check('ignored duplicate reports stored:false', r.body?.stored === false, r.text);
}
{
  const r = await call(post(VALID), stubDb({ throws: true }));
  check('D1 failure is 503', r.status === 503, `got ${r.status}`);
  check('D1 failure does NOT say ok:true', !r.text.includes('"ok":true'), r.text);
}

console.log(failures === 0
  ? '\nall checks passed\n'
  : `\n${failures} check(s) FAILED\n`);
process.exit(failures === 0 ? 0 : 1);
