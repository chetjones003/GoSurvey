# Deploying the marketing site (Cloudflare Pages)

The marketing site in `site/` is no longer published via GitHub Pages. It's deployed by hand
with `wrangler`, the same CLI already used for the telemetry and accounts Workers
(see `docs/cloudflare-telemetry-setup.md`), to Cloudflare Pages, and served at
**gosurveycad.com**.

| | |
|---|---|
| **Your time** | ~10 minutes, once, then ~30 seconds per deploy |
| **Cost** | £0 on Cloudflare's free tier |
| **Prerequisites** | A Cloudflare account (the one you already use for Workers) and Node.js |

---

## One-time setup

### 1. Add the domain to Cloudflare

In the Cloudflare dashboard: **Add a domain** → `gosurveycad.com`. Cloudflare scans existing
DNS and gives you two nameservers.

In GoDaddy, under gosurveycad.com → DNS → **Nameservers**, switch from GoDaddy's default
nameservers to the two Cloudflare gave you. (This moves DNS management to Cloudflare — it does
not move the registration, which stays with GoDaddy. It's what lets Pages route the bare apex
domain, which GoDaddy's own DNS can't do for a third-party host.)

Wait for Cloudflare to show the zone as **Active** (usually minutes, rarely up to 24h).

### 2. Create the Pages project

```bash
npx wrangler login
npx wrangler pages project create gosurveycad-site --production-branch=master
```

### 3. Attach the custom domain

In the dashboard: **Workers & Pages → gosurveycad-site → Custom domains → Set up a domain** →
enter `gosurveycad.com` (and `www.gosurveycad.com` if you want both). Cloudflare adds the DNS
records for you automatically since it now manages the zone.

---

## Deploying

From the repo root, whenever `site/` changes:

```bash
# Keep the feature screenshots in sync first (same PNGs as the in-app What's New billboard)
cp resources/whats-new/*.png site/img/features/

npx wrangler pages deploy site --project-name=gosurveycad-site
```

That's it — no GitHub Actions workflow runs this; it was removed along with GitHub Pages
(`.github/workflows/pages.yml`). Deploy by hand after merging site changes to `master`.
