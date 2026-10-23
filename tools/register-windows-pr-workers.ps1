#Requires -Version 7.2
param(
  [string]$BuildRunnerDirectory = 'C:\actions-runner',
  [string]$WorkersRoot = (Join-Path $env:USERPROFILE 'GoSurveyCI\pr-workers'),
  [switch]$Start
)
$ErrorActionPreference = 'Stop'
$repoUrl = 'https://github.com/chetjones003/GoSurvey'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
    [Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
  throw 'Register workers from the normal logged-in runner account.'
}
$build = Get-Content -LiteralPath (Join-Path $BuildRunnerDirectory '.runner') -Raw | ConvertFrom-Json
if ($build.gitHubUrl.TrimEnd('/') -ne $repoUrl) { throw 'Build worker belongs to a different repository.' }
$workers = @(
  @{ role = 'clang-tidy'; name = 'gosurvey-victus-clang-tidy'; label = 'gosurvey-pr-clang-tidy' },
  @{ role = 'msvc-analyze'; name = 'gosurvey-victus-msvc-analyze'; label = 'gosurvey-pr-msvc-analyze' }
)
New-Item -ItemType Directory -Force -Path $WorkersRoot | Out-Null
$package = Join-Path $WorkersRoot 'actions-runner-win-x64-2.337.0.zip'
$hash = '1150692afa94e71f872017e254ea55b6eece1eece3fe7e3a6d4c93d0a1b85cfc'
foreach ($worker in $workers) {
  $directory = Join-Path $WorkersRoot $worker.role
  $settings = Join-Path $directory '.runner'
  if (Test-Path -LiteralPath $settings) {
    $registered = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
    if ($registered.gitHubUrl.TrimEnd('/') -ne $repoUrl -or $registered.agentName -ne $worker.name) {
      throw "Existing worker settings do not match $($worker.name)."
    }
    Write-Host "$($worker.name): already registered."
    continue
  }
  if (-not $env:GOSURVEY_REGISTRATION_TOKEN) { throw 'Supply a fresh registration token in GOSURVEY_REGISTRATION_TOKEN.' }
  if (-not (Test-Path -LiteralPath $package)) {
    Invoke-WebRequest -Uri 'https://github.com/actions/runner/releases/download/v2.337.0/actions-runner-win-x64-2.337.0.zip' -OutFile $package
  }
  if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
    throw 'Official GitHub runner archive checksum mismatch.'
  }
  New-Item -ItemType Directory -Force -Path $directory | Out-Null
  Expand-Archive -LiteralPath $package -DestinationPath $directory -Force
  Push-Location -LiteralPath $directory
  try {
    # Never copy .runner/.credentials from another instance or replace a live registration.
    & .\config.cmd --unattended --url $repoUrl --token $env:GOSURVEY_REGISTRATION_TOKEN `
      --name $worker.name --labels $worker.label --work _work
    if ($LASTEXITCODE -ne 0) { throw "Registration failed for $($worker.name)." }
  } finally { Pop-Location }
}
foreach ($file in 'start-windows-pr-worker.ps1', 'start-windows-pr-workers.ps1') {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $WorkersRoot $file) -Force
}
$report = @(@{ role = 'build'; directory = $BuildRunnerDirectory; name = $build.agentName; label = 'gosurvey-pr-build' }) +
  @($workers | ForEach-Object { @{ role = $_.role; name = $_.name; label = $_.label; directory = (Join-Path $WorkersRoot $_.role) } })
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $WorkersRoot 'workers.json')
$report | ConvertTo-Json | Write-Host
if ($Start) {
  & (Join-Path $WorkersRoot 'start-windows-pr-workers.ps1') -WorkersRoot $WorkersRoot -BuildRunnerDirectory $BuildRunnerDirectory
}
