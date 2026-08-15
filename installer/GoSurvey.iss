; GoSurvey installer (Inno Setup 6) — ADR-029 (c).
;
; This ONE script replaces the per-version copies (0.3.1.iss, 0.4.0.iss) that were
; hand-edited per release and carried absolute C:\Users\chetj\... paths, which meant
; the shipped artifact could only be produced on one machine.
;
; Version resolution, in priority order:
;   1. ISCC /DAppVersion=... /DAppVersionQuad=...   (what CI passes)
;   2. build\generated\version.iss                  (written by CMake from project(VERSION))
;   3. the 0.0.0-local fallback below               (an un-configured tree)
; The fallback is deliberately not a plausible release number: a build that reaches it
; has bypassed the single version source and should be obvious at a glance.
;
; Build it locally with:   iscc installer\GoSurvey.iss
; Or with an explicit version: iscc /DAppVersion=0.5.0-beta.7 /DAppVersionQuad=0.5.0.7 installer\GoSurvey.iss

#ifndef AppVersion
  #ifexist "..\build\generated\version.iss"
    #include "..\build\generated\version.iss"
  #endif
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0-local"
#endif
#ifndef AppVersionQuad
  #define AppVersionQuad "0.0.0.0"
#endif

; Where the built binaries live, relative to this script. CI overrides for an out-of-tree build.
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#ifndef OutputDir
  #define OutputDir "Output"
#endif

#define MyAppName "GoSurvey"
#define MyAppPublisher "Chet Jones"
#define MyAppURL "https://github.com/chetjones003/GoSurvey"
; Stable, version-free executable name (ADR-029 (b)). Do not reintroduce the version here:
; the installed path must not change per release or shortcuts, the .gs association and the
; REQ-078 update flow all break.
#define MyAppExeName "GoSurvey.exe"
#define MyAppAssocName MyAppName + " File"
#define MyAppAssocExt ".gs"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt

[Setup]
; AppId identifies the application across versions — it is what makes an install an UPGRADE
; rather than a second copy. It is unchanged from 0.3.1/0.4.0 on purpose.
AppId={{02774040-27C9-4A9A-A999-86E641C42F78}
AppName={#MyAppName}
AppVersion={#AppVersion}
; AppVersion may carry a prerelease suffix ("0.5.0-beta.7"), which is not a valid version
; resource, so the numeric quad is stated separately rather than inferred.
VersionInfoVersion={#AppVersionQuad}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
DisableProgramGroupPage=yes
LicenseFile=License.txt
InfoAfterFile=InfoAfter.txt
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename=GoSurvey-{#AppVersion}-Installer
SetupIconFile=..\resources\icons\app.ico
SolidCompression=yes
WizardStyle=modern dynamic

; REQ-078 / ADR-029 (f): the running application publishes these mutexes (see
; PublishInstallerDetectionMutex in src/app/main.cpp). Without them a silent update
; cannot tell that GoSurvey is running, and fails trying to replace a locked .exe.
; Both namespaces are listed because the installer runs elevated and may not share the
; local one with the user's session.
AppMutex=GoSurveyAppMutex,Global\GoSurveyAppMutex
CloseApplications=yes
RestartApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Sweep the version-stamped executables shipped up to 0.4.0 (ADR-029 (b)). Without this an
; upgraded install keeps a stale GoSurvey-0.4.0.exe beside the new GoSurvey.exe, and any
; shortcut still pointing at it would launch a version that can never update itself again.
Type: files; Name: "{app}\GoSurvey-*.exe"

[Files]
Source: "{#BuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Code]
// True when Setup was started by the in-app updater with /RELAUNCH=1 (REQ-078).
//
// Needed because /RESTARTAPPLICATIONS cannot do this job: it only restarts applications that
// Restart Manager itself shut down, and the updater's own process has already exited by the time
// Setup performs its Restart Manager scan. Nothing is registered, so nothing is restarted — which
// is exactly what happened in the first live update: the app closed and never came back.
//
// Gated on the parameter rather than on "was this a silent install" so that an ordinary scripted
// deployment (/SILENT from an IT script) still does NOT pop a GUI on someone's screen.
function WantsRelaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
; The updater's relaunch, routed through Explorer.
;
; Launching non-elevated is the requirement, not a nicety: Setup runs elevated, so a direct exec
; would bring GoSurvey back as administrator and it would then write prefs and drawings with admin
; ownership that the user's own session cannot modify.
;
; `runasoriginaluser` is the flag meant for this and it was tried first. Setup's log showed it
; accepted the entry ("Run as: Original user", "Type: Exec", correct Filename) and then silently
; produced no process — it depends on obtaining the original user's shell token, which is not
; reliably available when Setup was started from a context that was already elevated. Handing the
; path to Explorer instead makes the shell perform the launch, so the new process inherits
; Explorer's non-elevated token. Verified directly: launched, and measured as non-elevated via
; the process token.
;
; Cost of this route, accepted: Explorer returns immediately and reports nothing, so Setup cannot
; tell whether the app actually started, and on a machine with no running shell the relaunch is a
; silent no-op. Both are acceptable for a best-effort restart -- the update itself has already
; succeeded by this point.
Filename: "{win}\explorer.exe"; Parameters: """{app}\{#MyAppExeName}"""; Flags: nowait; Check: WantsRelaunch

[UninstallRun]
; Clear Windows icon cache to ensure new icon is displayed on reinstall
Filename: "cmd.exe"; Parameters: "/c ""del /f /q %LocalAppData%\IconCache.db"""; Flags: runhidden
; Clear file extension registry cache for .gs files
Filename: "reg.exe"; Parameters: "delete ""HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.gs"" /f"; Flags: runhidden
; Delete user application data (history, layout files, preferences).
; UninstallRun does NOT execute during an upgrade, so an update keeps the user's settings.
Filename: "cmd.exe"; Parameters: "/c ""rmdir /s /q %APPDATA%\GoSurvey"""; Flags: runhidden
