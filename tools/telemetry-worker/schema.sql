-- GoSurvey telemetry store (REQ-080, ADR-032) — Cloudflare D1 (SQLite).
--
-- Apply with:
--   npx wrangler d1 execute gosurvey-telemetry --remote --file=./schema.sql
--
-- Re-running this file is safe: every statement is IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS pings (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  ts         TEXT NOT NULL,   -- ISO-8601 UTC, full precision, server clock
  day        TEXT NOT NULL,   -- YYYY-MM-DD derived from ts; the dedupe and grouping key
  install_id TEXT NOT NULL,   -- anonymous 128-bit id from the client; the ONLY identifier
  event      TEXT NOT NULL CHECK (event IN ('install', 'active')),
  version    TEXT NOT NULL,
  channel    TEXT NOT NULL CHECK (channel IN ('stable', 'beta')),
  os         TEXT NOT NULL,
  country    TEXT             -- server-derived, country-level; NULL when Cloudflare omits it
);

-- An install happens once per identity, ever. Without this a client retrying a ping whose reply
-- was lost would book a second install and quietly inflate the headline number.
CREATE UNIQUE INDEX IF NOT EXISTS ux_pings_install_once
  ON pings (install_id) WHERE event = 'install';

-- An active ping counts once per identity per day. The client already throttles to a rolling
-- 24h, but the client is the thing whose clock and prefs file can be wrong; the store should not
-- depend on it being right.
CREATE UNIQUE INDEX IF NOT EXISTS ux_pings_active_daily
  ON pings (install_id, day) WHERE event = 'active';

-- Covers the two questions actually asked of this table: "how many active on/since date X" and
-- "how many installs in period X".
CREATE INDEX IF NOT EXISTS ix_pings_event_day ON pings (event, day);

-- Version and channel distribution, for deciding what is safe to stop supporting.
CREATE INDEX IF NOT EXISTS ix_pings_version ON pings (version);
