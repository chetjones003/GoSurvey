@echo off
setlocal

rem Shows recent data from GoSurvey's two Cloudflare D1 databases. Run from anywhere.
rem
rem Two separate databases by design (ADR-037 (e)): gosurvey-telemetry holds REQ-080's
rem anonymous usage pings; gosurvey-accounts holds REQ-091/092's signed-in users and their
rem license tier. A sign-in never appears in the first, and an anonymous ping never appears
rem in the second - that split is deliberate, not a bug, so this script just gives one place
rem to look at both instead of two.
rem
rem `npx` resolves to npx.cmd on Windows, so it must be invoked with `call` - without it,
rem control jumps into npx.cmd and never returns here, and the second query silently never runs.

cd /d "%~dp0"

echo === gosurvey-telemetry: pings (most recent 20) ===
call npx wrangler d1 execute gosurvey-telemetry --remote --config tools\telemetry-worker\wrangler.toml --command "SELECT * FROM pings ORDER BY id DESC LIMIT 20"
if errorlevel 1 (
  echo ERROR: telemetry query failed - try "npx wrangler login" first.
  exit /b 1
)

echo.
echo === gosurvey-accounts: users ===
call npx wrangler d1 execute gosurvey-accounts --remote --config tools\accounts-worker\wrangler.toml --command "SELECT * FROM users"
if errorlevel 1 (
  echo ERROR: accounts query failed - try "npx wrangler login" first.
  exit /b 1
)

exit /b 0
