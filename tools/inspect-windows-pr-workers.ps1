#Requires -Version 7.2
param(
  [string]$WorkersRoot = (Join-Path $env:USERPROFILE 'GoSurveyCI\pr-workers'),
  [switch]$RequireWorkers
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
  # The observed CIM snapshot did not show the briefly connected child listeners.
  # The role launcher holds this mutex until run.cmd exits, independent of CIM.
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $probe = [Threading.Mutex]::new($false, "Local\GoSurveyPrWorker-$($identity.User.Value)-$role")
  $acquired = $false
  try {
    try { $acquired = $probe.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired = $true }
  } finally {
    if ($acquired) { $probe.ReleaseMutex() }
    $probe.Dispose()
  }
  Write-Host "$role role launcher active: $(-not $acquired); visible listener processes: $($listening.Count)"
  if ($acquired -and -not $listening.Count) { $missing += $role }
  foreach ($suffix in 'listener.log', 'listener-error.log') {
    $log = Join-Path $WorkersRoot "$role-$suffix"
    Write-Host "Startup log: $log"
    if (Test-Path -LiteralPath $log) {
      try { Get-Content -LiteralPath $log -Tail 30 | Write-Host }
      catch { Write-Warning "Could not read startup log: $($_.Exception.Message)" }
    }
  }
}
if ($RequireWorkers -and $missing.Count) { throw "Analyzer workers did not start: $($missing -join ', ')." }
