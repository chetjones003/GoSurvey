#Requires -Version 7.2
param(
  [Parameter(Mandatory)][ValidateSet('build', 'clang-tidy', 'msvc-analyze')][string]$Role,
  [Parameter(Mandatory)][string]$RunnerDirectory,
  [string]$EnvironmentFile = 'C:\GoSurveyCI\Enter-GoSurveyEnvironment.ps1',
  [string]$LogDirectory
)
$ErrorActionPreference = 'Stop'
# Open the log inside this detached process. Parent-side Start-Process
# redirection inherits CI pipe handles and triggers the runner's step cleanup.
if ($LogDirectory) {
  $log = Join-Path $LogDirectory "$Role-listener.log"
  & $PSCommandPath -Role $Role -RunnerDirectory $RunnerDirectory -EnvironmentFile $EnvironmentFile *> $log
  return
}
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
}
