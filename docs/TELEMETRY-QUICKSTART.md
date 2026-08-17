# Telemetry Quick Start (15 Minutes)

You have telemetry code built into GoSurvey. Here's how to get it running.

---

## Phase 1: Build (5 min)

**Open Developer Command Prompt for VS 2022** (critical — PowerShell won't work)

```bash
cd C:\Users\chetj\source\repos\GoSurvey

# Configure
cmake --preset ninja-release

# Build
cmake --build build/release --config Release --parallel 8

# Test (optional but recommended)
ctest --test-dir build/release --output-on-failure
```

**Result:** `build/release/GoSurvey.exe` is ready.

---

## Phase 2: Pick a Backend (2 min)

**Quick decision:**
- **Easiest**: Cloudflare Workers (recommended)
- **Best SQL**: Supabase
- **True free forever**: Firebase or PlanetScale
- **Just testing**: Webhook.site

See `docs/FREE-BACKENDS-COMPARISON.md` for full comparison.

---

## Phase 3: Deploy Backend (5 min)

### Option A: Cloudflare Workers (5 min, recommended)

```bash
npm install -g wrangler
npm create cloudflare@latest my-telemetry
cd my-telemetry
```

Create `src/index.ts`:
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
      const payload = await request.json();
      const key = `${payload.event}:${payload.installId}:${Date.now()}`;
      await env.TELEMETRY_KV.put(key, JSON.stringify({
        ...payload,
        timestamp: new Date().toISOString(),
      }), { expirationTtl: 86400 * 90 });
      return new Response(JSON.stringify({ ok: true }), { status: 200 });
    } catch (error) {
      console.error('Error:', error);
      return new Response('Server error', { status: 500 });
    }
  },
};
```

Update `wrangler.toml`:
```toml
name = "telemetry"
main = "src/index.ts"

[[kv_namespaces]]
binding = "TELEMETRY_KV"
id = "your-kv-id"  # Get this from Cloudflare dashboard after first deploy
```

Deploy:
```bash
npx wrangler deploy
# You'll get a URL like: https://my-telemetry.your-account.workers.dev
```

### Option B: Firebase (7 min)

1. Go to https://console.firebase.google.com
2. Create a new project
3. Enable Realtime Database (US region)
4. Copy your database URL

```javascript
// server.js
const admin = require('firebase-admin');
const express = require('express');

admin.initializeApp({
  databaseURL: 'https://your-project.firebaseio.com'
});

const app = express();
app.use(express.json());

app.post('/v1/ping', async (req, res) => {
  try {
    await admin.database().ref(`pings/${req.body.installId}/${Date.now()}`).set(req.body);
    res.json({ ok: true });
  } catch (error) {
    res.status(500).json({ error: 'Failed' });
  }
});

app.listen(3000);
```

Deploy to Firebase Hosting or run locally.

### Option C: Webhook.site (2 min, testing only)

1. Go to https://webhook.site
2. Copy your unique URL
3. That's it — it's your endpoint

---

## Phase 4: Update GoSurvey (1 min)

Edit `src/telemetry/TelemetryPing.hpp`:

```cpp
// Before:
constexpr const char* TelemetryEndpoint = "https://telemetry.example.com/v1/ping";

// After (example Cloudflare):
constexpr const char* TelemetryEndpoint = "https://my-telemetry.your-account.workers.dev/v1/ping";
```

Rebuild:
```bash
cmake --build build/release --config Release --parallel 8
```

---

## Phase 5: Test (2 min)

Run `build/release/GoSurvey.exe`. On startup, you should see pings arriving at your backend:

### Check Cloudflare KV:
```bash
npx wrangler kv:key list --binding=TELEMETRY_KV --limit=10
```

### Check Firebase Console:
- Open https://console.firebase.google.com
- Go to Realtime Database → Data
- Should see `pings/{installId}/{timestamp}` entries

### Check Webhook.site:
- Watch https://webhook.site in real-time
- Should see JSON payload appear when app starts

---

## Phase 6: Query Your Data

### Cloudflare KV:
```bash
# All pings
npx wrangler kv:key list --binding=TELEMETRY_KV --limit=100

# Just installs
npx wrangler kv:key list --binding=TELEMETRY_KV --prefix=install

# Just active pings
npx wrangler kv:key list --binding=TELEMETRY_KV --prefix=active

# Export to JSON
npx wrangler kv:key list --binding=TELEMETRY_KV > pings.json
```

### Firebase Console:
- Realtime Database → Data tab
- Sort by key, drill into timestamps
- Or use Firebase CLI:
```bash
firebase database:get pings --pretty
```

### SQL-based (Supabase, etc):
```sql
-- Unique installs
SELECT COUNT(DISTINCT install_id) FROM pings WHERE event = 'install';

-- Active in last 7 days
SELECT COUNT(DISTINCT install_id) FROM pings 
WHERE event = 'active' AND created_at > now() - interval '7 days';

-- Version distribution
SELECT version, COUNT(*) as count 
FROM pings GROUP BY version ORDER BY count DESC;

-- Channel (stable vs beta)
SELECT channel, COUNT(DISTINCT install_id) as users 
FROM pings GROUP BY channel;
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| **"cl is not found"** | Use Developer Command Prompt (not PowerShell) |
| **No pings arrive** | Check endpoint URL in TelemetryPing.hpp matches deployed URL |
| **Build fails** | Ensure CMake saw TelemetryService.cpp: `cmake --preset ninja-release` (reconfigure) |
| **Endpoint returns 500** | Check backend logs (Cloudflare dashboard, Firebase console) for details |

---

## What's Being Sent?

Each ping is this JSON (anonymized, no PII):
```json
{
  "installId": "abc123def456...",  // Random per-machine, generated once
  "event": "install" or "active",   // install = first run, active = once per 24h
  "version": "0.5.2",               // Your app version
  "channel": "stable" or "beta",    // From settings
  "os": "windows"                   // Always windows currently
}
```

No username, hostname, file paths, email, or hardware fingerprint.

---

## Next Steps

1. ✅ Build GoSurvey
2. ✅ Deploy backend (Cloudflare recommended)
3. ✅ Update endpoint URL
4. ✅ Test with your backend
5. 📊 Monitor adoption over time
6. 📈 Use data to inform pricing/features

For detailed backend comparison, see `FREE-BACKENDS-COMPARISON.md`.
