#Requires -Version 7.2
param(
  [string]$BuildRunnerDirectory = 'C:\actions-runner',
  [string]$WorkersRoot = (Join-Path $env:USERPROFILE 'GoSurveyCI\pr-workers'),
  [string]$EnvironmentFile = 'C:\GoSurveyCI\Enter-GoSurveyEnvironment.ps1'
)
$ErrorActionPreference = 'Stop'
$pwsh = (Get-Command pwsh.exe -CommandType Application | Select-Object -First 1).Source
$launcher = Join-Path $PSScriptRoot 'start-windows-pr-worker.ps1'
$workers = @(
  @{ role = 'build'; directory = $BuildRunnerDirectory },
  @{ role = 'clang-tidy'; directory = (Join-Path $WorkersRoot 'clang-tidy') },
  @{ role = 'msvc-analyze'; directory = (Join-Path $WorkersRoot 'msvc-analyze') }
)
foreach ($worker in $workers) {
  $directory = (Resolve-Path -LiteralPath $worker.directory).Path
  if (-not (Test-Path -LiteralPath (Join-Path $directory '.runner'))) { throw "Unregistered worker: $directory" }
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $probe = [Threading.Mutex]::new($false, "Local\GoSurveyPrWorker-$($identity.User.Value)-$($worker.role)")
  $acquired = $false
  try {
    try { $acquired = $probe.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired = $true }
  } finally {
    if ($acquired) { $probe.ReleaseMutex() }
    $probe.Dispose()
  }
  if (-not $acquired) { Write-Host "$($worker.role): role launcher already running."; continue }
  $listenerPath = Join-Path $directory 'bin\Runner.Listener.exe'
  $running = @(Get-CimInstance Win32_Process -Filter "Name='Runner.Listener.exe'" |
    Where-Object { $_.ExecutablePath -eq $listenerPath })
  if ($running.Count) { Write-Host "$($worker.role): listener already running."; continue }
  # Persistent workers must not inherit a provisioning job's token, runtime URLs,
  # workspace or process-tracking ID. Restore the parent environment afterwards.
  $removed = @{}
  foreach ($entry in @(Get-ChildItem Env: | Where-Object {
    $_.Name -match '^(GITHUB_|ACTIONS_|RUNNER_)' -or
    $_.Name -in @('GH_TOKEN', 'GOSURVEY_REGISTRATION_TOKEN', 'CI')
  })) {
    $removed[$entry.Name] = $entry.Value
    [Environment]::SetEnvironmentVariable($entry.Name, $null, 'Process')
  }
  try {
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
      '-File', "`"$launcher`"", '-Role', $worker.role, '-RunnerDirectory', "`"$directory`"",
      '-EnvironmentFile', "`"$EnvironmentFile`"")
    $process = Start-Process -FilePath $pwsh -ArgumentList $arguments -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput (Join-Path $WorkersRoot "$($worker.role)-listener.log") `
      -RedirectStandardError (Join-Path $WorkersRoot "$($worker.role)-listener-error.log")
    Write-Host "$($worker.role): started launcher PID $($process.Id)."
    # Observe the launched process while this provisioning shell is still alive.
    Start-Sleep -Seconds 8
    $process.Refresh()
    if ($process.HasExited) {
      Write-Warning "$($worker.role): launch process exited with code $($process.ExitCode)."
    } else {
      Write-Host "$($worker.role): launch process still alive; session=$($process.SessionId)."
    }
  } finally {
    foreach ($entry in $removed.GetEnumerator()) {
      [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
  }
}
