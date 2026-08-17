# Free Telemetry Backends — Complete Comparison

Your telemetry pings are simple JSON over HTTPS. Any backend that accepts POST requests works. Here are **10 battle-tested free options** ranked by ease + features:

---

## 🏆 Tier 1: Easiest (No Server Management)

### 1. **Cloudflare Workers + KV** ⭐ RECOMMENDED
**Best for:** Set-it-and-forget-it, instant deployment, beautiful analytics

| Metric | Value |
|--------|-------|
| Free tier | 1M requests/day |
| Storage | 1 GB KV |
| Setup time | 5 min |
| Cost (if you hit limits) | ~$0.50/M requests |
| Query data | CLI or via dashboard |

**Pros:**
- Deploys in seconds
- Global CDN (fast responses from anywhere)
- KV storage is perfect for time-series data
- Built-in analytics dashboard
- No cold starts

**Cons:**
- KV is eventually consistent (not real-time)
- TypeScript/JavaScript only

**Setup:**
```bash
npm create cloudflare@latest my-telemetry
# Follow the guide in telemetry-backend-guide.md (Option 1)
npx wrangler deploy
```

**Query data:**
```bash
npx wrangler kv:key list --binding=TELEMETRY_KV --limit=100
npx wrangler kv:key list --binding=TELEMETRY_KV --prefix=install
```

---

### 2. **Supabase (PostgreSQL)** ⭐ HIGHLY RECOMMENDED
**Best for:** SQL queries, dashboards, real analytics

| Metric | Value |
|--------|-------|
| Free tier | 500MB DB, 2GB bandwidth |
| Database | Full PostgreSQL |
| Setup time | 10 min |
| Cost (if you hit limits) | Pay-as-you-go (~$0.03/GB) |
| Query data | SQL via dashboard, API |

**Pros:**
- Full relational database (SQL)
- Real-time dashboards
- Built-in auth (if you want it later)
- Can export data as CSV
- Visual query builder

**Cons:**
- Slower to query than KV (but still fast)
- Requires SQL knowledge for advanced queries

**Setup:**
```javascript
// Simple Node.js + Supabase
import { createClient } from '@supabase/supabase-js';

const supabase = createClient(
  process.env.SUPABASE_URL,
  process.env.SUPABASE_ANON_KEY
);

app.post('/v1/ping', async (req, res) => {
  const { error } = await supabase.from('pings').insert(req.body);
  res.json({ ok: !error });
});
```

**Query data (via dashboard):**
```sql
SELECT COUNT(DISTINCT install_id) FROM pings WHERE event = 'install';
```

---

### 3. **Firebase Realtime Database** ✅ WORKS GREAT
**Best for:** Google ecosystem, instant sync

| Metric | Value |
|--------|-------|
| Free tier | 100 concurrent, 10 GB storage |
| Setup time | 7 min |
| Cost | Free for years with your usage levels |
| Query data | Console + API |

**Pros:**
- Google's infrastructure (reliable)
- Realtime updates
- Mobile-friendly (if you expand later)
- Generous free tier

**Cons:**
- No SQL (document-based)
- Harder to do aggregate queries

**Setup:**
```javascript
const admin = require('firebase-admin');
const db = admin.database();

app.post('/v1/ping', async (req, res) => {
  const { installId, event } = req.body;
  await db.ref(`pings/${event}/${installId}/${Date.now()}`).set(req.body);
  res.json({ ok: true });
});
```

---

## 🥈 Tier 2: Easy + Powerful

### 4. **Railway + PostgreSQL**
**Best for:** Simple deployment, Docker-friendly

| Metric | Value |
|--------|-------|
| Free tier | $5 credit/month (usually enough) |
| Setup time | 10 min |
| Cost | ~$0.0007/hour (basically free for telemetry) |
| Query data | Direct DB access |

**Pros:**
- One-click GitHub deployment
- Full PostgreSQL included
- Inspect logs easily
- Can add web dashboard later

**Cons:**
- Credits expire monthly (no true free tier)
- Requires GitHub account

**How:** Connect GitHub repo → Railway auto-deploys your `server.js`

---

### 5. **PlanetScale (MySQL as a Service)** ✅ TRUE FREE TIER
**Best for:** MySQL users, zero commitment

| Metric | Value |
|--------|-------|
| Free tier | 5 GB, 100M rows, no auto-scaling |
| Setup time | 8 min |
| Cost | Free forever if you stay in limits |
| Query data | Web console, MySQL CLI |

**Pros:**
- True free tier (no expiring credits)
- MySQL is standard
- Good for small datasets
- Can upgrade if needed

**Cons:**
- Web console is basic
- No auto-scaling on free tier

**Setup:**
```javascript
const mysql = require('mysql2/promise');
const pool = mysql.createPool({
  host: process.env.DB_HOST,
  user: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
  database: 'telemetry'
});

app.post('/v1/ping', async (req, res) => {
  const conn = await pool.getConnection();
  await conn.query(
    'INSERT INTO pings (install_id, event, version, channel) VALUES (?, ?, ?, ?)',
    [req.body.installId, req.body.event, req.body.version, req.body.channel]
  );
  res.json({ ok: true });
});
```

---

## 🥉 Tier 3: Specialized but Free

### 6. **Elasticsearch Cloud (Kibana Dashboards)**
**Best for:** If you want professional analytics

| Metric | Value |
|--------|-------|
| Free tier | 14 days full access, then read-only |
| Setup time | 10 min |
| Cost | Free tier is limited, then paid |
| Query data | Kibana (beautiful dashboards) |

**Pros:**
- Incredible visualization tools
- Can build complex queries
- Time-series optimized

**Cons:**
- Free tier expires
- More complex than you need

---

### 7. **MongoDB Atlas + Realm**
**Best for:** If you love NoSQL, JSON-like data

| Metric | Value |
|--------|-------|
| Free tier | 512 MB, forever |
| Setup time | 8 min |
| Cost | Free for your usage |
| Query data | MongoDB Compass, web console |

**Pros:**
- JSON storage (natural fit for your pings)
- Global replication
- Mobile/web sync available later

**Cons:**
- Document DB (not great for aggregates)

**Setup:**
```javascript
const { MongoClient } = require('mongodb');
const client = new MongoClient(process.env.MONGO_URI);
const db = client.db('telemetry');

app.post('/v1/ping', async (req, res) => {
  await db.collection('pings').insertOne({
    ...req.body,
    timestamp: new Date()
  });
  res.json({ ok: true });
});
```

---

### 8. **Google Sheets + Apps Script** 🚀 QUIRKY BUT FREE
**Best for:** If you want to view data in Excel-like format

| Metric | Value |
|--------|-------|
| Free tier | Unlimited (Google Drive limits) |
| Setup time | 15 min |
| Cost | Free forever |
| Query data | Sheets interface |

**Pros:**
- Easy to share/view
- Native spreadsheet formulas
- No infrastructure needed

**Cons:**
- Rate-limited (not scalable past ~1000/day)
- Slower than databases

**Setup:**
```javascript
// Google Apps Script
function doPost(e) {
  const sheet = SpreadsheetApp.getActiveSheet();
  const data = JSON.parse(e.postData.contents);
  sheet.appendRow([
    data.installId,
    data.event,
    data.version,
    data.channel,
    new Date()
  ]);
  return ContentService.createTextOutput(JSON.stringify({ ok: true }));
}
```

---

### 9. **Webhook.site + Serverless Function**
**Best for:** Development/testing, ephemeral data

| Metric | Value |
|--------|-------|
| Free tier | Unlimited (data expires after 7 days) |
| Setup time | 2 min (literally just get a URL) |
| Cost | Free forever |
| Query data | Web UI only |

**Pros:**
- Instant, zero setup
- Great for smoke testing your pings

**Cons:**
- No persistence (data expires)
- Not for production

**How:**
1. Go to https://webhook.site
2. Copy your unique URL
3. Point your telemetry there:
   ```cpp
   constexpr const char* TelemetryEndpoint = "https://webhook.site/your-unique-id";
   ```
4. Rebuild and run
5. Watch pings come in at https://webhook.site

---

### 10. **Self-Hosted + Linux VPS (Hetzner, Linode)**
**Best for:** Full control, future-proofing

| Metric | Value |
|--------|-------|
| Free tier | Varies (many offer $100 credits) |
| Setup time | 30 min |
| Cost | $2.50-5/month (practically free) |
| Query data | Direct SSH access |

**Pros:**
- Complete control
- Can run anything (Node.js, Python, etc.)
- No vendor lock-in

**Cons:**
- You manage security, backups, uptime
- Requires Linux knowledge

**Providers:**
- **Hetzner** — €2.99/month, rock-solid
- **DigitalOcean** — $4/month, great docs
- **Linode** — $2.50/month (if you sign up for year)
- **AWS Lightsail** — $3.50/month, free tier eligible
- **Oracle Cloud** — Always-free tier (1 VM 1GB RAM, forever)

---

## 📊 Quick Decision Matrix

| Need | Best Choice |
|------|-------------|
| **Simplest** (5 min setup) | Cloudflare Workers |
| **Best SQL queries** | Supabase |
| **True free forever** | Firebase Realtime DB or PlanetScale |
| **Beautiful dashboards** | Elasticsearch Cloud (trial) |
| **Full control** | Self-hosted Node.js |
| **Just testing pings** | Webhook.site |
| **No-code** | Google Sheets + Apps Script |

---

## 🎯 Recommended Path

### For production (what most users do):
1. **Development**: Webhook.site to test payload
2. **Staging**: Cloudflare Workers (instant, free, scales infinitely)
3. **Production**: Keep Cloudflare or migrate to Supabase for SQL queries

### Setup order (15 min total):
```bash
# 1. Test with Webhook.site (2 min)
# Update TelemetryPing.hpp with webhook.site URL
# Rebuild and run
# Check https://webhook.site — confirm pings arrive

# 2. Deploy Cloudflare Workers (5 min)
npm create cloudflare@latest my-telemetry
# Copy worker code from telemetry-backend-guide.md
npx wrangler deploy

# 3. Update your app (1 min)
# Edit TelemetryPing.hpp with your Cloudflare URL
# Rebuild

# 4. Query data (2 min)
npx wrangler kv:key list --binding=TELEMETRY_KV
```

---

## Cost Projections (If You Hit Free Tier Limits)

Assuming 100 unique installs/month, 5 active pings per install/month (conservative):

| Backend | 100 installs/mo | 1K installs/mo | 10K installs/mo |
|---------|-----------------|----------------|-----------------|
| **Cloudflare Workers** | Free | Free | ~$2.50 |
| **Supabase** | Free | Free | ~$5 |
| **PlanetScale** | Free | Free | ~$10 |
| **Firebase** | Free | Free | Free |
| **Self-hosted VPS** | $3-5 | $3-5 | $3-5 |

---

## Final Recommendation

🎯 **Use Cloudflare Workers** for the first 3-6 months while you get adoption data. It's so cheap and simple that you won't regret it. If you need SQL queries later, export to Supabase.

The migration between backends is trivial (just change the endpoint URL) because your telemetry payload is standardized.
