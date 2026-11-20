# Runs accoreconsole AUDIT on multileader audit fixtures under build/artifacts/mleader-audit.
# Prerequisite: GOSURVEY_ORACLE_DIR=<that dir> GoSurveyTests.exe "[.][mleader-audit]"
param(
  [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\artifacts\mleader-audit'),
  [string]$AccoreConsole = ''
)

$ErrorActionPreference = 'Stop'
$artifact = [System.IO.Path]::GetFullPath($ArtifactDirectory)
if (-not (Test-Path -LiteralPath $artifact)) {
  throw "Missing artifact directory: $artifact (run GoSurveyTests [.][mleader-audit] first)"
}

if ([string]::IsNullOrWhiteSpace($AccoreConsole)) {
  $candidates = @(
    'C:\Program Files\Autodesk\AutoCAD 2027\accoreconsole.exe',
    'C:\Program Files\Autodesk\AutoCAD 2026\accoreconsole.exe',
    'C:\Program Files\Autodesk\AutoCAD 2025\accoreconsole.exe'
  )
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
      $AccoreConsole = $candidate
      break
    }
  }
}
if (-not (Test-Path -LiteralPath $AccoreConsole)) {
  throw "accoreconsole.exe not found (set -AccoreConsole explicitly)"
}

$scriptPath = Join-Path $artifact 'audit.scr'
@(
  'AUDIT',
  'Y',
  'QUIT',
  'Y'
) | Set-Content -LiteralPath $scriptPath -Encoding ASCII

$summaryPath = Join-Path $artifact 'accore-audit-summary.txt'
$lines = @(
  "accoreconsole: $AccoreConsole",
  "artifacts: $artifact",
  "timestamp: $(Get-Date -Format o)",
  ''
)
$failures = 0
Get-ChildItem -LiteralPath $artifact -Filter '*.dwg' | Sort-Object Name | ForEach-Object {
  $logName = $_.BaseName + '.accore.log'
  $logPath = Join-Path $artifact $logName
  if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force }
  Push-Location -LiteralPath $artifact
  try {
    $proc = Start-Process -FilePath $AccoreConsole -ArgumentList @(
      '/i', $_.Name,
      '/s', (Join-Path $artifact 'audit.scr'),
      '/l', $logName
    ) -Wait -PassThru -RedirectStandardOutput (Join-Path $artifact ($_.BaseName + '.accore.out.txt'))
  }
  finally {
    Pop-Location
  }
  $auditLine = Select-String -LiteralPath $logPath -Pattern 'Total errors found' -SimpleMatch -ErrorAction SilentlyContinue |
    Select-Object -Last 1
  if ($null -eq $auditLine) {
    $auditLine = Select-String -LiteralPath (Join-Path $artifact ($_.BaseName + '.accore.out.txt')) -Pattern 'Total errors found' |
      Select-Object -Last 1
  }
  if ($null -eq $auditLine) {
    $auditText = '(AUDIT summary line not found; exit code ' + $proc.ExitCode + ')'
    $failures++
  }
  else {
    $auditText = $auditLine.Line.Trim()
    if ($auditText -notmatch 'Total errors found 0 fixed 0') { $failures++ }
  }
  $lines += ($_.Name + ': exit=' + $proc.ExitCode + ' ' + $auditText)
}
$lines += ''
$lines += ('result: ' + ($(if ($failures -eq 0) { 'PASS' } else { "FAIL ($failures file(s))" })))
Set-Content -LiteralPath $summaryPath -Value $lines -Encoding UTF8
Get-Content -LiteralPath $summaryPath
if ($failures -ne 0) { exit 1 }
