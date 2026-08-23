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
  install_id TEXT NOT NULL,   -- anonymous 128-bit id from the client; the durable identifier
  event      TEXT NOT NULL CHECK (event IN ('install', 'active')),
  version    TEXT NOT NULL,
  channel    TEXT NOT NULL CHECK (channel IN ('stable', 'beta')),
  os         TEXT NOT NULL,
  country    TEXT,            -- server-derived, country-level; NULL when Cloudflare omits it
  -- REQ-080 amended 2026-08-23 (D-2026-08-23-e): the REQ-091 signed-in email at ping time, or
  -- NULL when signed out. Deliberately NOT a foreign key into gosurvey-accounts.users — the two
  -- tables stay structurally independent (ADR-037 (e)); this is a same-shape optional field in
  -- each, not a join target. If this table already exists (a pre-2026-08-23 deployment), this
  -- column is added by a one-time `ALTER TABLE pings ADD COLUMN email TEXT;` instead — CREATE
  -- TABLE IF NOT EXISTS does not retrofit an existing table.
  email      TEXT
);

-- An install happens once per identity, ever. Without this a client retrying a ping whose reply
-- was lost would book a second install and quietly inflate the headline number.
CREATE UNIQUE INDEX IF NOT EXISTS ux_pings_install_once
  ON pings (install_id) WHERE event = 'install';

-- Amended 2026-08-23 (D-2026-08-23-f): DROPPED by explicit user decision. This used to dedupe
-- "active" pings to one row per identity per day, matching the client's 24h throttle. The client
-- throttle is gone too — a ping now fires every launch — so this table now measures LAUNCHES,
-- not daily-active-identities, and multiple same-day rows for one install_id are expected, not a
-- bug. Every analytics query in queries.sql already uses COUNT(DISTINCT install_id) rather than
-- COUNT(*) for "how many users", so this drop does not silently break "active users" numbers —
-- only a naive COUNT(*) would, and none of the shipped queries do that.
--
-- If this table was created before 2026-08-23, drop the index explicitly once:
--   DROP INDEX IF EXISTS ux_pings_active_daily;

-- Covers the two questions actually asked of this table: "how many active on/since date X" and
-- "how many installs in period X".
CREATE INDEX IF NOT EXISTS ix_pings_event_day ON pings (event, day);

-- Version and channel distribution, for deciding what is safe to stop supporting.
CREATE INDEX IF NOT EXISTS ix_pings_version ON pings (version);
