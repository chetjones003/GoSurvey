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

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Clear Windows icon cache to ensure new icon is displayed on reinstall
Filename: "cmd.exe"; Parameters: "/c ""del /f /q %LocalAppData%\IconCache.db"""; Flags: runhidden
; Clear file extension registry cache for .gs files
Filename: "reg.exe"; Parameters: "delete ""HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.gs"" /f"; Flags: runhidden
; Delete user application data (history, layout files, preferences).
; UninstallRun does NOT execute during an upgrade, so an update keeps the user's settings.
Filename: "cmd.exe"; Parameters: "/c ""rmdir /s /q %APPDATA%\GoSurvey"""; Flags: runhidden
