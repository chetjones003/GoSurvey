# Install GoSurvey on Windows

Open PowerShell and paste this one line, then press Enter:

```powershell
irm https://raw.githubusercontent.com/chetjones003/GoSurvey/master/scripts/install.ps1 | iex
```

This downloads the latest **stable** GoSurvey release, checks its installer against the SHA-256 in
the release manifest, and installs silently for the current Windows user at
`%LOCALAPPDATA%\Programs\GoSurvey`. A fresh install does not show GoSurvey setup windows or ask
for administrator approval. The script does not ask you to choose setup options.

If GoSurvey is currently installed under Program Files, Windows asks once for approval to remove
that protected copy. The migration moves the installation to your user profile and preserves your
settings and data. Choosing Update from an older Program Files install uses the same migration
bootstrap. Future in-app updates still wait for your choice and update that per-user copy without
elevation.

For these per-user paths, the installer carries Microsoft's licensed app-local Visual C++ runtime
DLLs beside GoSurvey and refreshes them with each release. It skips the shared runtime installer,
which can otherwise request UAC when the machine needs the runtime. The Microsoft Store installer
continues to use the shared redistributable.

SHA-256 detects a damaged or incomplete download; it does not prove who published the file. The
installer is currently unsigned, so Windows may show a SmartScreen or Defender warning. The install
script cannot suppress or guarantee away warnings controlled by Windows.

## Console output preview

In Windows Terminal and other PowerShell consoles that support true color, the bootstrap opens with
a GoSurvey splash card: the GS logo and wordmark over a colored ASCII rendering of the survey-map
splash image. Each step then updates in place, with a spinner while it runs and a progress bar
during the download. The script respects `NO_COLOR`. When color or in-place updates are
unavailable (for example, redirected output or a console narrower than 102 columns), it prints the
same steps as plain text:

```text
  GoSurvey  /  CAD for land surveyors
  INSTALLER  /  Latest stable . SHA-256 verified . Per-user

  +  Release   GoSurvey 1.2.3  .  stable channel
  +  Download  86.0 MB  .  3.9 s
  +  Verify    SHA-256 matches the release manifest  .  c47b24dc0541
  +  Install   Installed for this Windows user  .  no administrator needed

  ----------------------------------------------------------------------------------------------------

  +  GoSurvey 1.2.3 is ready.

     Location   C:\Users\Surveyor\AppData\Local\Programs\GoSurvey
     Launch     Start menu  >  GoSurvey
```

When a Program Files copy is migrated, an `Approval` note and a `Migrate` step appear before
`Install`. The version, size, time and hash prefix vary with the release and machine.

If the manifest, download, or SHA-256 check fails, the failing step is marked with the reason, the
installer is not launched, and the script points to the releases page:

```text
  x  Verify    The downloaded installer SHA-256 does not match the stable release manifest.

     Installation stopped. Run the command again, or download the installer from
     https://github.com/chetjones003/GoSurvey/releases/latest
```

The script sets `$LASTEXITCODE` to 1 when installation stops.
