#Requires -Version 7.2
<#
.SYNOPSIS
Registers PR listeners and reconciles the routing labels on the Victus.
.DESCRIPTION
Run from the normal Windows runner account with gh authenticated as a repository
administrator. A runner registration token is needed only for new listeners.
RegistrationTokenOnly is for bootstrap jobs that can register new listeners but
cannot change the labels on existing GitHub runner registrations.
#>
param(
  [string]$BuildRunnerDirectory = 'C:\actions-runner',
  [string]$WorkersRoot = (Join-Path $env:USERPROFILE 'GoSurveyCI\pr-workers'),
  [string]$ThirdPrRunnerDirectory = 'C:\actions-pr-3',
  [switch]$Start,
  [switch]$RegistrationTokenOnly
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
  @{ role = 'clang-tidy'; name = 'gosurvey-victus-clang-tidy'; directory = (Join-Path $WorkersRoot 'clang-tidy'); labels = 'gosurvey-pr-build,gosurvey-pr-clang-tidy' },
  @{ role = 'msvc-analyze'; name = 'gosurvey-victus-msvc-analyze'; directory = (Join-Path $WorkersRoot 'msvc-analyze'); labels = 'gosurvey-pr-build,gosurvey-pr-msvc-analyze' },
  @{ role = 'pr-3'; name = 'gosurvey-victus-pr-3'; directory = $ThirdPrRunnerDirectory; labels = 'gosurvey-pr-build' }
)
New-Item -ItemType Directory -Force -Path $WorkersRoot | Out-Null
$version = '2.338.2'
$package = Join-Path $WorkersRoot "actions-runner-win-x64-$version.zip"
$hash = 'a1f17aa987fc8158bc7521d284912267a0d01b7c2c4684b33c86783b5e8221cc'
foreach ($worker in $workers) {
  $directory = $worker.directory
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
    Invoke-WebRequest -Uri "https://github.com/actions/runner/releases/download/v$version/actions-runner-win-x64-$version.zip" -OutFile $package
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
      --name $worker.name --labels $worker.labels --work _work
    if ($LASTEXITCODE -ne 0) { throw "Registration failed for $($worker.name)." }
  } finally { Pop-Location }
}

$release = @{ role = 'build'; directory = $BuildRunnerDirectory; name = $build.agentName; labels = 'gosurvey-victus-ci' }
$report = @($release) + $workers
if (-not $RegistrationTokenOnly) {
  # Add the PR slots before removing the legacy PR label from the release runner.
  foreach ($runner in ($workers + @($release))) {
    $settings = Get-Content -LiteralPath (Join-Path $runner.directory '.runner') -Raw | ConvertFrom-Json
    if ($settings.agentId -notmatch '^[1-9][0-9]*$') { throw "Missing runner ID for $($runner.name)." }
    $endpoint = "repos/chetjones003/GoSurvey/actions/runners/$($settings.agentId)"
    $response = gh api $endpoint
    if ($LASTEXITCODE -ne 0) { throw "GitHub runner lookup failed for $($runner.name). Authenticate gh as a repository administrator." }
    $remote = $response | ConvertFrom-Json
    if ($remote.name -ne $runner.name) { throw "GitHub runner ID does not match $($runner.name)." }

    $currentLabels = $remote.labels.name
    $addLabels = @($runner.labels.Split(',') | Where-Object { $_ -notin $currentLabels })
    if ($addLabels.Count) {
      $fields = foreach ($label in $addLabels) { '-f'; "labels[]=$label" }
      gh api --method POST "$endpoint/labels" @fields | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "Could not add GitHub labels for $($runner.name)." }
      Write-Host "$($runner.name): added $($addLabels -join ', ')."
    }
    $removeLabel = if ($runner.role -eq 'build') { 'gosurvey-pr-build' } else { 'gosurvey-victus-ci' }
    if ($removeLabel -in $currentLabels) {
      gh api --method DELETE "$endpoint/labels/$removeLabel" | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "Could not remove $removeLabel from $($runner.name)." }
      Write-Host "$($runner.name): removed $removeLabel."
    }
  }
}

foreach ($file in 'start-windows-pr-worker.ps1', 'start-windows-pr-workers.ps1') {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $WorkersRoot $file) -Force
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $WorkersRoot 'workers.json')
$report | ConvertTo-Json | Write-Host
if ($Start) {
  & (Join-Path $WorkersRoot 'start-windows-pr-workers.ps1') -WorkersRoot $WorkersRoot -BuildRunnerDirectory $BuildRunnerDirectory -ThirdPrRunnerDirectory $ThirdPrRunnerDirectory
}
