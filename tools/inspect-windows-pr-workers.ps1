#Requires -Version 7.2
param(
  [string]$WorkersRoot = (Join-Path $env:USERPROFILE 'GoSurveyCI\pr-workers'),
  [switch]$RequireListeners
)
$ErrorActionPreference = 'Stop'
$pwsh = Get-Command pwsh.exe -CommandType Application | Select-Object -First 1
Write-Host "Selected PowerShell executable: $($pwsh.Source)"
$processes = @(Get-CimInstance Win32_Process -Filter "Name='Runner.Listener.exe' OR Name='pwsh.exe'" |
  Select-Object Name, ProcessId, ParentProcessId, SessionId, ExecutablePath)
$processes | Format-Table | Out-String | Write-Host
$missing = @()
foreach ($role in 'clang-tidy', 'msvc-analyze') {
  $exe = Join-Path $WorkersRoot "$role\bin\Runner.Listener.exe"
  $listening = @($processes | Where-Object { $_.ExecutablePath -eq $exe })
  Write-Host "$role listener processes: $($listening.Count)"
  if (-not $listening.Count) { $missing += $role }
  foreach ($suffix in 'listener.log', 'listener-error.log') {
    $log = Join-Path $WorkersRoot "$role-$suffix"
    Write-Host "Startup log: $log"
    if (Test-Path -LiteralPath $log) {
      try { Get-Content -LiteralPath $log -Tail 30 | Write-Host }
      catch { Write-Warning "Could not read startup log: $($_.Exception.Message)" }
    }
  }
}
if ($RequireListeners -and $missing.Count) { throw "Analyzer listeners did not start: $($missing -join ', ')." }
