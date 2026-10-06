/**
 * GoSurvey telemetry receiver — REQ-080, ADR-032.
 *
 * Routes:
 *   POST /v1/ping            — anonymous install/active pings (existing contract)
 *   POST /v1/startup-report  — silent startup failure diagnostics from the desktop client
 *
 * THE RESPONSE BODY IS PART OF THE CONTRACT for /v1/ping. `TelemetryService.cpp` greps for
 * `"ok":true`. Startup reports use the same `{ ok: true }` shape on success but the client
 * does not retry — fire-and-forget.
 */

const PING_PATH = '/v1/ping';
const STARTUP_REPORT_PATH = '/v1/startup-report';

const PING_FIELDS = {
  installId: /^[A-Za-z0-9_-]{1,64}$/,
  event:     /^(install|active)$/,
  version:   /^[A-Za-z0-9.+_-]{1,32}$/,
  channel:   /^(stable|beta)$/,
  os:        /^[a-z0-9_-]{1,16}$/,
};

const STARTUP_FIELDS = {
  installId: /^[A-Za-z0-9_-]{1,64}$/,
  version:   /^[A-Za-z0-9.+_-]{1,32}$/,
  channel:   /^(stable|beta)$/,
  os:        /^[a-z0-9_-]{1,16}$/,
  stage:     /^(glfw_init|glfw_window|opengl_init)$/,
};

const MAX_STARTUP_REPORT_CHARS = 16384;

function json(body, status) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json',
      'cache-control': 'no-store',
    },
  });
}

function validateFields(payload, fields) {
  for (const [name, pattern] of Object.entries(fields)) {
    const value = payload[name];
    if (typeof value !== 'string' || !pattern.test(value)) {
      return name;
    }
  }
  return null;
}

async function readJsonObject(request, maxBytes) {
  const declared = Number(request.headers.get('content-length') || '0');
  if (declared > maxBytes) {
    return { error: json({ error: 'payload too large' }, 413) };
  }

  let payload;
  try {
    payload = await request.json();
  } catch {
    return { error: json({ error: 'body is not valid JSON' }, 400) };
  }
  if (payload === null || typeof payload !== 'object' || Array.isArray(payload)) {
    return { error: json({ error: 'body must be a JSON object' }, 400) };
  }
  return { payload };
}

async function handlePing(request, env) {
  if (request.method !== 'POST') {
    return json({ error: 'method not allowed' }, 405);
  }

  const parsed = await readJsonObject(request, 2048);
  if (parsed.error) return parsed.error;
  const payload = parsed.payload;

  const bad = validateFields(payload, PING_FIELDS);
  if (bad) {
    return json({ error: `invalid or missing field: ${bad}` }, 400);
  }

  const rawEmail = typeof payload.email === 'string' ? payload.email : '';
  const email =
    rawEmail && rawEmail.length <= 320 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(rawEmail)
      ? rawEmail
      : null;

  const now = new Date();
  const ts = now.toISOString();
  const day = ts.slice(0, 10);
  const country = request.cf?.country ?? null;

  try {
    const result = await env.DB.prepare(
      `INSERT OR IGNORE INTO pings (ts, day, install_id, event, version, channel, os, country, email)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
    )
      .bind(ts, day, payload.installId, payload.event, payload.version,
            payload.channel, payload.os, country, email)
      .run();

    return json({ ok: true, stored: result.meta.changes === 1 }, 200);
  } catch (err) {
    console.error('telemetry insert failed:', err);
    return json({ error: 'storage unavailable' }, 503);
  }
}

async function handleStartupReport(request, env) {
  if (request.method !== 'POST') {
    return json({ error: 'method not allowed' }, 405);
  }

  const parsed = await readJsonObject(request, 65536);
  if (parsed.error) return parsed.error;
  const payload = parsed.payload;

  const bad = validateFields(payload, STARTUP_FIELDS);
  if (bad) {
    return json({ error: `invalid or missing field: ${bad}` }, 400);
  }

  const reportRaw = typeof payload.report === 'string' ? payload.report : '';
  if (reportRaw.length < 1 || reportRaw.length > MAX_STARTUP_REPORT_CHARS) {
    return json({ error: 'invalid or missing field: report' }, 400);
  }

  const now = new Date();
  const ts = now.toISOString();
  const country = request.cf?.country ?? null;

  try {
    const result = await env.DB.prepare(
      `INSERT INTO startup_reports (ts, install_id, version, channel, os, stage, country, report)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)`
    )
      .bind(ts, payload.installId, payload.version, payload.channel, payload.os,
            payload.stage, country, reportRaw)
      .run();

    return json({ ok: true, stored: result.meta.changes === 1 }, 200);
  } catch (err) {
    console.error('startup report insert failed:', err);
    return json({ error: 'storage unavailable' }, 503);
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === PING_PATH) {
      return handlePing(request, env);
    }
    if (url.pathname === STARTUP_REPORT_PATH) {
      return handleStartupReport(request, env);
    }
    return json({ error: 'not found' }, 404);
  },
};
