# Telemetry Backend Setup & Viewing

The GoSurvey binary sends anonymous usage data to a configurable HTTPS endpoint. You own the backend and your data stays in your infrastructure.

## Option 1: Cloudflare Workers + KV (Recommended for simplicity)

**Why this option:**
- Free tier supports your usage (1M requests/day)
- No servers to manage
- KV storage for unique install tracking
- Dashboard built-in

**Setup (5 minutes):**

1. Create a Cloudflare Workers project:
   ```bash
   npm create cloudflare@latest my-telemetry
   cd my-telemetry
   ```

2. Create `src/index.ts`:
   ```typescript
   export interface Env {
     TELEMETRY_KV: KVNamespace;
   }

   export default {
     async fetch(request: Request, env: Env): Promise<Response> {
       if (request.method !== 'POST') {
         return new Response('Method not allowed', { status: 405 });
       }

       try {
         const payload = await request.json<{
           installId: string;
           event: 'install' | 'active';
           version: string;
           channel: 'stable' | 'beta';
           os: string;
         }>();

         const timestamp = new Date().toISOString();
         const key = `${payload.event}:${payload.installId}:${timestamp}`;
         
         await env.TELEMETRY_KV.put(key, JSON.stringify({
           ...payload,
           timestamp,
           ip: request.headers.get('CF-Connecting-IP'),
         }), { expirationTtl: 86400 * 90 }); // 90 day retention

         return new Response(JSON.stringify({ ok: true }), {
           status: 200,
           headers: { 'Content-Type': 'application/json' },
         });
       } catch (error) {
         console.error('Telemetry error:', error);
         return new Response('Internal error', { status: 500 });
       }
     },
   };
   ```

3. Add KV binding in `wrangler.toml`:
   ```toml
   name = "telemetry"
   main = "src/index.ts"
   
   [[kv_namespaces]]
   binding = "TELEMETRY_KV"
   id = "your-kv-namespace-id"
   ```

4. Deploy:
   ```bash
   npx wrangler deploy
   ```

5. Update `TelemetryEndpoint` in `src/telemetry/TelemetryPing.hpp`:
   ```cpp
   constexpr const char* TelemetryEndpoint = "https://your-worker.your-account.workers.dev/v1/ping";
   ```

6. **View the data:**
   ```bash
   npx wrangler kv:key list --binding=TELEMETRY_KV --limit=100
   npx wrangler kv:key list --binding=TELEMETRY_KV --prefix=install --limit=50  # All installs
   npx wrangler kv:key list --binding=TELEMETRY_KV --prefix=active --limit=50   # Active pings
   ```

---

## Option 2: Self-hosted Node.js + SQLite (Full control)

**Setup (15 minutes):**

1. Create Express server:
   ```javascript
   // server.js
   const express = require('express');
   const Database = require('better-sqlite3');
   const app = express();

   const db = new Database('telemetry.db');
   
   // Create tables
   db.exec(`
     CREATE TABLE IF NOT EXISTS pings (
       id INTEGER PRIMARY KEY,
       install_id TEXT NOT NULL,
       event TEXT NOT NULL,
       version TEXT NOT NULL,
       channel TEXT NOT NULL,
       os TEXT NOT NULL,
       ip TEXT,
       timestamp TEXT DEFAULT CURRENT_TIMESTAMP
     );
     CREATE INDEX IF NOT EXISTS idx_install ON pings(install_id);
     CREATE INDEX IF NOT EXISTS idx_event ON pings(event);
     CREATE INDEX IF NOT EXISTS idx_timestamp ON pings(timestamp);
   `);

   app.use(express.json({ limit: '1kb' })); // Telemetry is small

   app.post('/v1/ping', (req, res) => {
     const { installId, event, version, channel, os } = req.body;
     
     if (!installId || !['install', 'active'].includes(event)) {
       return res.status(400).json({ error: 'Invalid payload' });
     }

     try {
       const stmt = db.prepare(`
         INSERT INTO pings (install_id, event, version, channel, os, ip)
         VALUES (?, ?, ?, ?, ?, ?)
       `);
       stmt.run(installId, event, version, channel, os, req.ip);
       res.json({ ok: true });
     } catch (error) {
       console.error('Error recording ping:', error);
       res.status(500).json({ error: 'Server error' });
     }
   });

   app.listen(3000, () => console.log('Telemetry server running on :3000'));
   ```

2. **Query the data:**
   ```javascript
   // queries.js
   const Database = require('better-sqlite3');
   const db = new Database('telemetry.db');

   // Unique installs ever
   const installs = db.prepare(`
     SELECT COUNT(DISTINCT install_id) as total FROM pings WHERE event = 'install'
   `).all()[0];
   console.log(`Total installs: ${installs.total}`);

   // Active in last 7 days
   const active7d = db.prepare(`
     SELECT COUNT(DISTINCT install_id) as total FROM pings
     WHERE event = 'active' AND timestamp > datetime('now', '-7 days')
   `).all()[0];
   console.log(`Active (7d): ${active7d.total}`);

   // Version distribution
   const versions = db.prepare(`
     SELECT version, COUNT(*) as count FROM pings
     GROUP BY version ORDER BY count DESC
   `).all();
   console.log('Versions:', versions);

   // Channel split (stable vs beta)
   const channels = db.prepare(`
     SELECT channel, COUNT(*) as count FROM pings
     GROUP BY channel
   `).all();
   console.log('Channels:', channels);
   ```

---

## Option 3: Vercel Serverless + PostgreSQL

**Setup (20 minutes):**

Use Vercel Functions + Supabase (free tier includes PostgreSQL):

```javascript
// api/telemetry.js
import { createClient } from '@supabase/supabase-js';

const supabase = createClient(
  process.env.SUPABASE_URL,
  process.env.SUPABASE_ANON_KEY
);

export default async (req, res) => {
  if (req.method !== 'POST') return res.status(405).end();

  const { installId, event, version, channel, os } = req.body;

  const { error } = await supabase
    .from('pings')
    .insert({ install_id: installId, event, version, channel, os });

  if (error) {
    console.error(error);
    return res.status(500).json({ error: 'Server error' });
  }
  
  res.json({ ok: true });
};
```

Then query via Supabase dashboard or with simple SQL.

---

## Monitoring Your Telemetry

### Key metrics to track:

```sql
-- How many unique installs?
SELECT COUNT(DISTINCT installId) FROM pings WHERE event = 'install';

-- Active users this month?
SELECT COUNT(DISTINCT installId) FROM pings 
WHERE event = 'active' AND timestamp > now() - interval '30 days';

-- Retention: % of installs that pinged in the last week?
SELECT 
  (SELECT COUNT(DISTINCT installId) FROM pings 
   WHERE event = 'active' AND timestamp > now() - interval '7 days') 
  * 100.0 / 
  (SELECT COUNT(DISTINCT installId) FROM pings WHERE event = 'install')
AS retention_pct;

-- Adoption curve (installs per day):
SELECT DATE(timestamp) as day, COUNT(*) as new_installs 
FROM pings WHERE event = 'install'
GROUP BY DATE(timestamp)
ORDER BY day DESC;

-- Version distribution:
SELECT version, COUNT(DISTINCT installId) as count 
FROM pings GROUP BY version ORDER BY count DESC;

-- Beta vs Stable:
SELECT channel, COUNT(DISTINCT installId) as users 
FROM pings GROUP BY channel;
```

---

## Security Notes

- **No PII is sent** — only anonymous install ID (generated locally), version, channel, OS
- **IP address** — logged server-side to detect abuse, but could be stripped
- **HTTPS only** — the binary refuses non-HTTPS endpoints
- **Rate limiting** — 24h throttle on `active` events prevents spam
- **No auth tokens** — no user accounts, no passwords

---

## For development/testing

Point `TelemetryEndpoint` at a local request inspector to verify the payload before deploying:

```cpp
// TelemetryPing.hpp (dev build only)
constexpr const char* TelemetryEndpoint = "https://webhook.site/your-unique-id";
```

Then check https://webhook.site to see what's being sent.

---

## Next steps

1. Choose backend (Cloudflare Workers recommended for simplicity)
2. Deploy it
3. Update `TelemetryEndpoint` in `src/telemetry/TelemetryPing.hpp` to your URL
4. Rebuild GoSurvey
5. Test with a local request inspector first
6. Monitor adoption metrics over time
