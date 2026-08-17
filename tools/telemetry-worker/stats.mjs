/**
 * The two numbers, and nothing else.
 *
 *   npm run stats          (from tools/telemetry-worker/)
 *
 * Goes through your existing `wrangler login`, so there is no public endpoint to protect and no
 * dashboard to secure — the reason this is a script and not a route on the Worker.
 *
 * For anything beyond these two numbers, see queries.sql or the D1 console in the Cloudflare
 * dashboard.
 */

import { exec } from 'node:child_process';
import { promisify } from 'node:util';

const run = promisify(exec);

const DB = 'gosurvey-telemetry';

// One round trip. Correlated subqueries rather than three separate commands, because three
// commands against a live table can disagree with each other.
//
// Must go through --command, not --file: `wrangler d1 execute --file` uploads the SQL as a batch
// import and returns execution statistics ("Rows read": 3) instead of the rows the SELECT
// produced, which is silently useless for a read.
//
// Single-quoted string literals only — no double quotes. The Windows branch below wraps this
// whole query in double quotes for cmd.exe, and one stray `"` here would split the command.
const SQL = `
SELECT
  (SELECT COUNT(*) FROM pings WHERE event = 'install') AS installs,
  (SELECT COUNT(DISTINCT install_id) FROM pings
     WHERE event = 'active' AND day >= date('now', '-7 days'))  AS active_7d,
  (SELECT COUNT(DISTINCT install_id) FROM pings
     WHERE event = 'active' AND day >= date('now', '-30 days')) AS active_30d
`.replace(/\s+/g, ' ').trim();

// One string, not an args array. npx is a .cmd that Node will not spawn directly on Windows
// (CVE-2024-27980), and passing an args array alongside `shell: true` is both deprecated and
// unescaped. `exec` takes a single pre-quoted command line and hands it to cmd.exe or sh, which
// is the one form that behaves the same on both.
//
// The double quotes around the SQL are load-bearing on Windows: `>=` in the WHERE clause is a
// redirection operator to cmd.exe, and unquoted it would truncate the query into a file.
const COMMAND = `npx wrangler d1 execute ${DB} --remote --json --command "${SQL}"`;

function row(label, value) {
  console.log(`  ${label.padEnd(24)}${String(value).padStart(6)}`);
}

try {
  const { stdout } = await run(COMMAND, { maxBuffer: 1024 * 1024 });

  // Wrangler prints a banner before the JSON, so parse from the first bracket.
  const start = stdout.indexOf('[');
  if (start === -1) throw new Error(`no JSON in wrangler output:\n${stdout}`);
  const [{ results }] = JSON.parse(stdout.slice(start));
  const { installs, active_7d, active_30d } = results[0];

  console.log(`\nGoSurvey telemetry — ${new Date().toISOString().slice(0, 10)}\n`);
  row('Installs (all time)', installs);
  row('Active users (7 days)', active_7d);
  row('Active users (30 days)', active_30d);
  console.log('');
} catch (err) {
  // Almost always one of two things, so say so rather than dumping a stack.
  console.error('\nCould not read the telemetry database.\n');
  console.error('  - Signed in?      npx wrangler whoami');
  console.error('  - Right folder?   run this from tools/telemetry-worker/\n');
  console.error(String(err.stderr || err.message).trim(), '\n');
  process.exit(1);
}
