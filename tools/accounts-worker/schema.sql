-- REQ-092 / ADR-037 (e): the accounts D1 database, separate from gosurvey-telemetry.
--
-- Deliberately minimal: this table exists to answer "what tier is this signed-in user on",
-- nothing more. Billing, plan history, and admin-grant tooling are explicitly future work
-- (REQ-092) and get their own schema addition when that work is scoped.

CREATE TABLE IF NOT EXISTS users (
  auth0_sub  TEXT PRIMARY KEY,   -- Auth0's `sub` claim; stable per identity, unique per user
  email      TEXT,               -- populated only if a future claim/Action supplies it; nullable
  tier       TEXT NOT NULL DEFAULT 'free',
  created_at TEXT NOT NULL       -- ISO-8601 UTC, set once at first sign-in
);
