-- Analytics for the GoSurvey telemetry store.
--
-- Run one at a time:
--   npx wrangler d1 execute gosurvey-telemetry --remote --command "<paste a query>"
--
-- Or open the D1 console in the Cloudflare dashboard and paste them there, which is easier to
-- read for anything returning more than one row.


-- Total installs, ever. The headline number.
SELECT COUNT(*) AS installs FROM pings WHERE event = 'install';


-- Installs per day for the last 30 days — the adoption curve.
SELECT day, COUNT(*) AS installs
FROM pings
WHERE event = 'install' AND day >= date('now', '-30 days')
GROUP BY day
ORDER BY day;


-- Active users in the last 7 and 30 days. DISTINCT is belt-and-braces: the unique index already
-- guarantees one active row per identity per day.
SELECT
  COUNT(DISTINCT CASE WHEN day >= date('now', '-7 days')  THEN install_id END) AS active_7d,
  COUNT(DISTINCT CASE WHEN day >= date('now', '-30 days') THEN install_id END) AS active_30d
FROM pings
WHERE event = 'active';


-- Daily active users over the last 30 days.
SELECT day, COUNT(DISTINCT install_id) AS dau
FROM pings
WHERE event = 'active' AND day >= date('now', '-30 days')
GROUP BY day
ORDER BY day;


-- Version distribution among users seen in the last 30 days. This is the query that says whether
-- an old version is safe to stop supporting.
SELECT version, COUNT(DISTINCT install_id) AS users
FROM pings
WHERE day >= date('now', '-30 days')
GROUP BY version
ORDER BY users DESC;


-- Stable vs beta split.
SELECT channel, COUNT(DISTINCT install_id) AS users
FROM pings
WHERE day >= date('now', '-30 days')
GROUP BY channel;


-- Retention: of the identities that installed 30+ days ago, how many are still active?
SELECT
  COUNT(DISTINCT i.install_id) AS cohort,
  COUNT(DISTINCT a.install_id) AS still_active
FROM pings i
LEFT JOIN pings a
  ON a.install_id = i.install_id
 AND a.event = 'active'
 AND a.day >= date('now', '-30 days')
WHERE i.event = 'install' AND i.day <= date('now', '-30 days');


-- Geography, country level.
SELECT COALESCE(country, 'unknown') AS country, COUNT(DISTINCT install_id) AS users
FROM pings
GROUP BY country
ORDER BY users DESC;


-- Installs that never came back: booked an install but no active ping in 30 days. A high number
-- here means people run it once and drop it, which is a product signal, not a telemetry bug.
SELECT COUNT(*) AS installed_never_returned
FROM pings i
WHERE i.event = 'install'
  AND i.day <= date('now', '-30 days')
  AND NOT EXISTS (
    SELECT 1 FROM pings a
    WHERE a.install_id = i.install_id
      AND a.event = 'active'
      AND a.day >= date('now', '-30 days')
  );
