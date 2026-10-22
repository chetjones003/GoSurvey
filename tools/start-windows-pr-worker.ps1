#Requires -Version 7.2
param(
  [Parameter(Mandatory)][ValidateSet('build', 'clang-tidy', 'msvc-analyze')][string]$Role,
  [Parameter(Mandatory)][string]$RunnerDirectory,
  [string]$EnvironmentFile = 'C:\GoSurveyCI\Enter-GoSurveyEnvironment.ps1'
)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
    [Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
  throw 'PR workers require a normal user in a logged-in Windows session.'
}
$runnerDir = (Resolve-Path -LiteralPath $RunnerDirectory).Path
if (-not (Test-Path -LiteralPath (Join-Path $runnerDir '.runner'))) { throw 'Register this worker first.' }
$mutex = [Threading.Mutex]::new($false, "Local\GoSurveyPrWorker-$($identity.User.Value)-$Role")
$owned = $false
Write-Host "Worker launcher: role=$Role; PID=$PID; session=$([Diagnostics.Process]::GetCurrentProcess().SessionId); user=$($identity.User.Value); executable=$([Diagnostics.Process]::GetCurrentProcess().MainModule.FileName); trackingPresent=$([bool]$env:RUNNER_TRACKING_ID)"
try {
  try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
  if (-not $owned) { Write-Host "$Role worker launcher is already running."; return }
  . $EnvironmentFile
  Push-Location -LiteralPath $runnerDir
  try {
    & .\run.cmd
    if ($LASTEXITCODE -ne 0) { throw "$Role worker stopped with exit $LASTEXITCODE." }
  } finally { Pop-Location }
} finally {
  if ($owned) { $mutex.ReleaseMutex() }
  $mutex.Dispose()
  Write-Host "Worker launcher ended: role=$Role; PID=$PID; UTC=$([DateTime]::UtcNow.ToString('o'))"
}
