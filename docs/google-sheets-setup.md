# Google Sheets Telemetry Backend Setup

> **Superseded.** The live telemetry backend is now a Cloudflare Worker + D1 —
> see [`cloudflare-telemetry-setup.md`](cloudflare-telemetry-setup.md), which also explains why
> we moved. This document is kept because the Sheets deployment still exists and still works, so
> it remains a one-line rollback (`TelemetryEndpoint` plus a rebuild) if the Worker ever needs to
> come down. Everything below is correct as of the BUG-020 fixes; do not follow it for new setup.

---

## Step 1: Create a Google Sheet

1. Go to https://sheets.google.com
2. Create a new spreadsheet called "GoSurvey Telemetry"
3. Create a sheet named "pings" (or use the default "Sheet1")
4. Add headers in row 1:
   ```
   A: timestamp
   B: installId
   C: event
   D: version
   E: channel
   F: os
   ```

> There is no IP column. Apps Script has no supported way to read a caller's IP address, so a
> column for one can only ever be blank.

**Save the sheet ID** (from the URL: `https://docs.google.com/spreadsheets/d/{SHEET_ID}/...`)

---

## Step 2: Create Google Apps Script

1. In your Google Sheet, go to **Extensions → Apps Script**
2. Delete the default code
3. Paste this:

```javascript
// Telemetry receiver for GoSurvey
// Receives POST requests with telemetry data and appends to Sheet

const SHEET_ID = 'YOUR_SHEET_ID_HERE';  // Replace with your sheet ID
const SHEET_NAME = 'pings';             // Name of the sheet tab

function jsonReply(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function doPost(e) {
  try {
    // A web app can be reached without a body (a bare browser hit, a health probe). Reading
    // e.postData.contents unguarded throws on those, and the throw looks identical to a real
    // failure in the log.
    if (!e || !e.postData || !e.postData.contents) {
      return jsonReply({ error: 'No POST body' });
    }

    const payload = JSON.parse(e.postData.contents);

    if (!payload.installId || !payload.event) {
      return jsonReply({ error: 'Missing required fields' });
    }

    const sheet = SpreadsheetApp.openById(SHEET_ID).getSheetByName(SHEET_NAME);
    if (!sheet) {
      return jsonReply({ error: 'Sheet not found: ' + SHEET_NAME });
    }

    // Two installs pinging in the same second can otherwise interleave inside appendRow and
    // overwrite one another's row.
    const lock = LockService.getScriptLock();
    lock.waitLock(10000);
    try {
      sheet.appendRow([
        new Date().toISOString(),  // timestamp
        payload.installId,         // installId
        payload.event,             // event (install or active)
        payload.version || '',     // version
        payload.channel || '',     // channel (stable or beta)
        payload.os || ''           // os
      ]);
    } finally {
      lock.releaseLock();
    }

    // GoSurvey checks for this exact acknowledgement. Apps Script cannot set an HTTP status
    // code, so every reply above is also a 200 — the body is the only signal the client has.
    return jsonReply({ ok: true });

  } catch (error) {
    Logger.log('Error: ' + error.toString());
    return jsonReply({ error: error.toString() });
  }
}

// Utility: Get unique install count
function getUniqueInstalls() {
  const sheet = SpreadsheetApp.openById(SHEET_ID).getSheetByName(SHEET_NAME);
  const data = sheet.getRange(2, 2, sheet.getLastRow() - 1, 1).getValues();
  const unique = new Set(data.map(row => row[0]));
  return unique.size;
}

// Utility: Get active users (last 7 days)
function getActiveUsers() {
  const sheet = SpreadsheetApp.openById(SHEET_ID).getSheetByName(SHEET_NAME);
  const data = sheet.getRange(2, 1, sheet.getLastRow() - 1, 2).getValues();
  const sevenDaysAgo = new Date();
  sevenDaysAgo.setDate(sevenDaysAgo.getDate() - 7);
  
  const active = new Set();
  data.forEach(row => {
    const date = new Date(row[0]);
    if (date > sevenDaysAgo && row[1]) {
      active.add(row[1]);
    }
  });
  return active.size;
}
```

4. Replace `YOUR_SHEET_ID_HERE` with your sheet ID from Step 1

---

## Step 3: Deploy the Script

1. Click **Deploy** (top-right)
2. Select **New deployment**
3. Type: **Web app**
4. Execute as: **Me** (your Google account)
5. Who has access: **Anyone**
6. Click **Deploy**
7. You'll get a deployment URL like:
   ```
   https://script.google.com/macros/s/{DEPLOYMENT_ID}/exec
   ```

**Save this URL** — this is your telemetry endpoint!

> **Two ways to get this wrong, both of which fail silently from the app's side:**
>
> - **"Who has access" must be literally `Anyone`, not `Anyone with Google Account`.** GoSurvey
>   pings anonymously with no credentials. `Anyone with Google Account` makes `/exec` answer a
>   `302` to the Google sign-in page for GET and `401` for POST, and `doPost` never runs.
> - **The URL must end in `/exec`.** Only `https://script.google.com/macros/s/{ID}/exec` is the
>   deployed web app. `/macros/d/{ID}/userweb` is not a real route (404), and
>   `/macros/d/{SCRIPT_ID}/dev` is the author-only test URL that rejects anonymous callers.
>
> Verify before touching the C++ — from any shell, signed out:
> ```bash
> curl -L "https://script.google.com/macros/s/{ID}/exec" \
>   -H "Content-Type: application/json" \
>   -d '{"installId":"curltest","event":"install","version":"0.0.0","channel":"stable","os":"windows"}'
> ```
> A working deployment prints `{"ok":true}` and adds a row. Anything else — HTML, a sign-in
> page, `401` — means the deployment is not open and the app cannot reach it either.
>
> **Do not add `-X POST` here.** `/exec` answers a `302` to `script.googleusercontent.com`, and a
> `302` is meant to be followed with a GET. `-d` alone lets curl do that; `-X POST` pins the
> method so curl re-POSTs the redirect with no body and Google answers **`411 Length Required`** —
> a failure produced entirely by the flag, after `doPost` has already run and written the row.
> WinHTTP converts the method correctly, so GoSurvey never sees this.

**After editing the script**, redeploy or the old code keeps serving: **Deploy → Manage
deployments → ✏️ (edit) → Version: New version → Deploy**. Editing an existing deployment this
way *keeps the same URL*; a brand-new deployment issues a new ID you would have to paste into
the C++ again.

---

## Step 4: Update GoSurvey

Edit `TelemetryEndpoint` in `src/telemetry/TelemetryPing.hpp`:

```cpp
constexpr const char* TelemetryEndpoint =
    "https://script.google.com/macros/s/{YOUR_DEPLOYMENT_ID}/exec";
```

Paste the URL exactly as the Deploy dialog gave it to you — `/macros/s/…/exec`.

---

## Step 5: Rebuild and Test

```bash
cd C:\Users\chetj\source\repos\GoSurvey
cmake --build build --target GoSurvey
```

Run `build/GoSurvey.exe`. On startup it POSTs one ping and you should see a row appear.

**If no row appears, the app already told you why** — it prints the reason to stderr:

```
[telemetry] ping failed: endpoint accepted the POST but did not acknowledge it: {"error":...}
```

That line means the request *reached* the script and the script refused it (read the `error`).
No line at all, or an `HTTP 401`, means it never reached the script — go back to Step 3.

One caveat when retesting: the `install` event fires once per machine and `active` is throttled
to once per 24 h, so a second run in the same day sends nothing at all — which looks exactly like
a broken endpoint. To force a fresh `install` ping, delete the `installId` and
`lastActivePingDate` keys from `gosurvey-user.json` (in your user data directory, beside the
executable as a fallback) before relaunching.

---

## Viewing Your Data

### In Google Sheets:
- **Unique installs**: Count distinct values in column B (installId)
- **Active users this week**: Filter column A to last 7 days, count distinct column B
- **Version distribution**: Pivot table on column D (version)
- **Channel split**: Pivot table on column E (channel)

### Quick formulas:
```
=COUNTA(B:B) - 1           // Total pings
=SUMPRODUCT(1/COUNTIF(B2:B,B2:B)) // Unique installs
```

### Advanced: Create a Pivot Table
1. Select all data (Ctrl+A)
2. **Insert → Pivot table**
3. Rows: installId or version
4. Values: COUNT of event
5. Filters: event, channel

---

## Troubleshooting

**No pings appearing in the sheet?** Work down this list — the curl in Step 3 separates the
first three causes from the rest in one command.

1. **URL ends in `/exec`**, on `/macros/s/`, in `TelemetryPing.hpp`. A `404` means the path form
   is wrong, not that the deployment is missing.
2. **Access is `Anyone`**, not `Anyone with Google Account`. A `401` on POST, or a sign-in page
   on GET, is this and only this.
3. **The script was redeployed after its last edit.** Apps Script keeps serving the deployed
   version, so an edit sitting unsaved in the editor changes nothing the app can see.
4. **Sheet tab is named exactly `pings`** and `SHEET_ID` matches the sheet URL.
5. **Apps Script → Executions** — a `doPost` that threw is listed there with the stack trace. An
   execution that never appears means the request never reached the script (causes 1–2).

**Sheet stays empty but the endpoint answers `200`?** Read the response *body*. Apps Script
cannot set an HTTP status code, so a thrown handler still answers `200` — with `{"error":…}`
instead of `{"ok":true}`. GoSurvey checks for `"ok":true` for exactly this reason and logs
`[telemetry] ping failed:` with the body when it is absent.

**"Sheet not found" error?**
- Make sure the sheet tab is named exactly "pings"
- Check the SHEET_ID variable is correct

**Deployment URL changed?**
- To keep the URL: **Deploy → Manage deployments → ✏️ edit → Version: New version → Deploy**
- **Create new deployment** issues a *new* ID — you must then update `TelemetryPing.hpp`

---

## Rate Limits

Google Apps Script has limits:
- ~1000 requests/day (per user, not per app)
- Your expected usage (100-1000 installs/month) is well within limits
- If you hit limits, data will be dropped (not fail) — consider exporting to a database

---

## Security Notes

- **No IP logging**: Apps Script exposes no client IP to `doPost`. The event object carries only
  `postData`, `parameter`/`parameters`, `queryString`, `pathInfo`, `contextPath` and
  `contentLength` — there is no `e.source`, and reaching for one throws. This is a privacy win
  for anonymous telemetry, not a limitation to work around.
- **Sheet visibility**: Make sure your sheet is **private** (not shared with "Anyone"). Setting
  the *deployment* to "Anyone" does not expose the sheet — the script runs as you and is the
  only thing that touches it.
- **No authentication**: Apps Script accepts requests from anyone, so anyone who learns the URL
  can write rows. That is acceptable for anonymous telemetry; treat the numbers as indicative,
  not audited.

---

## Exporting Data

To export your telemetry data:
1. Google Sheets → **File → Download → As CSV**
2. Import into Excel, Python (pandas), or a BI tool
3. Or keep the raw Google Sheet and create a dashboard inside Sheets

---

## Next: Add a Dashboard (Optional)

Once you have data, create a simple dashboard sheet:
1. New sheet called "Dashboard"
2. Add formulas:
   ```
   Total Installs: =SUMPRODUCT(1/COUNTIF(pings!B2:B,pings!B2:B))
   Active (7d):    =COUNTIFS(pings!A:A,">="&TODAY()-7,pings!C:C,"active")
   ```
3. Use charts (Insert → Chart) to visualize trends

This is zero-code analytics in Sheets.
