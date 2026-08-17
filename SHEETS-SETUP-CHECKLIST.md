# Google Sheets Telemetry — Setup Checklist

## ✅ Phase 1: Build GoSurvey (You do this now)

```bash
# Open Developer Command Prompt for VS 2022
cd C:\Users\chetj\source\repos\GoSurvey
cmake --preset ninja-release
cmake --build build/release --config Release --parallel 8
```

**Result:** `build/release/GoSurvey.exe` with telemetry code ready

---

## ✅ Phase 2: Set Up Google Sheets & Apps Script (You do this next)

**Follow this guide:** `docs/google-sheets-setup.md`

**Key steps:**
1. Create Google Sheet called "GoSurvey Telemetry"
2. Add headers to "pings" sheet: timestamp | installId | event | version | channel | os
3. Go to **Extensions → Apps Script**
4. Paste the code from the guide (copy the whole doPost function)
5. Replace `YOUR_SHEET_ID_HERE` with your sheet ID
6. Click **Deploy → New deployment → Web app**
7. Set **Who has access: `Anyone`** — *not* `Anyone with Google Account`. This one setting is
   the difference between a working endpoint and a `401`; GoSurvey pings with no credentials.
8. Copy your deployment URL

**You'll need:**
- Google account (Gmail)
- Sheet ID (from the URL)
- Deployment ID (from Apps Script)

---

## ✅ Phase 3: Give Me Your Deployment ID

Once you have the URL from Apps Script deployment, it looks like:
```
https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID_HERE/exec
```

**Tell me the `YOUR_DEPLOYMENT_ID_HERE` part** (the long alphanumeric string between `/s/` and `/exec`)

Better still, confirm it works first — paste the whole URL into this and run it signed out:
```bash
curl -L "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID_HERE/exec" \
  -H "Content-Type: application/json" \
  -d '{"installId":"curltest","event":"install","version":"0.0.0","channel":"stable","os":"windows"}'
```
`{"ok":true}` plus a new row in the sheet means it is live. HTML, a sign-in page, or `401` means
the deployment is not open to `Anyone` yet.

No `-X POST` — it makes curl re-POST the `302` with no body and Google answers `411`, which looks
like a failure but is purely the flag's doing. `-d` already implies POST.

---

## ✅ Phase 4: I'll Update & Rebuild

Once you give me your deployment ID, I'll:
1. Update `src/telemetry/TelemetryPing.hpp` with your URL
2. Rebuild GoSurvey
3. Test that pings work

---

## ✅ Phase 5: You Test It

Run `build/release/GoSurvey.exe` and check your Google Sheet — you should see telemetry rows appearing!

---

## Summary of What's Happening

```
GoSurvey (your app)
    ↓ (POSTs JSON on startup + daily active ping)
Google Apps Script (your deployment)
    ↓ (appends row)
Google Sheet "pings"
    ↓ (you view & analyze)
Your telemetry data
```

**Total setup time:** ~10 minutes (mostly waiting for Google to process)

---

## What Each Column Means

| Column | Example | Purpose |
|--------|---------|---------|
| timestamp | 2025-08-16T10:45:23.456Z | When the ping was received |
| installId | abc123def456... | Anonymous machine ID (same per install) |
| event | install or active | "install" = first run, "active" = once per 24h |
| version | 0.5.2 | Your app version |
| channel | stable or beta | Release channel from user settings |
| os | windows | Operating system |

There is no IP column: Apps Script gives `doPost` no access to the caller's IP address.

---

## Next: Your Turn

1. Go to `docs/google-sheets-setup.md`
2. Follow steps 1-3 (create sheet, create script, deploy)
3. Copy your deployment ID
4. Reply with the ID
5. I'll finish the setup

**Questions while setting up?** Ask and I'll help!
