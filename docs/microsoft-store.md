# Microsoft Store submission (MSI/EXE)

GoSurvey uses the Microsoft Store's MSI/EXE submission path with the existing Inno Setup
installer. This does not create an MSIX package. The Store requires installation to start without
installer UI; Windows may still show its UAC consent prompt.

## Partner Center install parameters

For the submitted versioned stable-release installer, set the install command to:

```text
/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
```

`/VERYSILENT` hides both the setup wizard and progress window. `/SILENT` alone is insufficient
because it leaves the progress window visible. Suppressing message boxes makes unattended error
handling choose Inno Setup's documented default response (Abort for an Abort/Retry error). The
command does not force-close GoSurvey or restart Windows. The installer does not launch GoSurvey
after a silent install.

For Partner Center's required **Installer handling** documentation URL, use the direct raw file URL
after the exit-code documentation has merged to `beta`:

```text
https://raw.githubusercontent.com/chetjones003/GoSurvey/beta/docs/installer-exit-codes.md
```

The page documents Inno Setup's actual process exit codes and explains which Partner Center
scenarios do not have a distinct GoSurvey installer code.

The bundled VC++ runtime is run with `/install /quiet /norestart`. The packaging script fails if
that redistributable is missing, so it cannot accidentally create a package that expects to fetch
it later.

The installer contains the application, its Qt DLLs and plugins, `qt.conf`, resource tree, PDFium,
and the VC++ runtime. Once the installer file has been obtained, installation works with network
access disabled. Downloading the installer from the Store still requires connectivity, as Store
acquisition does.

## Artifact URL

Use the versioned installer asset from a stable GitHub release, for example:

```text
https://github.com/chetjones003/GoSurvey/releases/download/v<version>/GoSurvey-<version>-Installer.exe
```

Do not submit the `channel-beta` asset: the beta workflow replaces that binary. A Store submission
must keep the installer at its submitted URL unchanged. Submit a newly versioned stable-release URL
for each new Store version.

## Before submission

1. Dispatch the release workflow against `master` after the release version is ready.
2. Confirm the versioned installer and `store-submission.txt` assets exist on the stable release.
   The note carries that installer's URL, SHA-256, and required install parameters.
3. Enter the versioned URL and the install parameters from the note in Partner Center.
4. Run Microsoft's MSI/EXE package validation and verify a clean install and an upgrade on a clean
   Windows test machine. Confirm no installer window appears, the app is installed, and the app
   does not launch automatically. A UAC prompt is expected when Windows requires elevation.

Store certification and Partner Center submission remain external release steps; the project CI
builds and publishes the same interactive installer used for direct downloads, plus the
version-specific Store submission note on stable releases.
