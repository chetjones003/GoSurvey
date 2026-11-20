param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][int]$ShardIndex,
  [Parameter(Mandatory = $true)][int]$ShardCount,
  [int]$TimeoutSec = 900,
  [Parameter(Mandatory = $true)][string]$WorkingDirectory
)

$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$workDir = (Resolve-Path -LiteralPath $WorkingDirectory).Path
if ($ShardCount -lt 1 -or $ShardIndex -lt 0 -or $ShardIndex -ge $ShardCount) {
  throw "Invalid Qt test shard $ShardIndex/$ShardCount."
}

$functions = @(& $exePath -functions | ForEach-Object { $_.Trim() } |
  Where-Object { $_ -match '^[A-Za-z_][A-Za-z0-9_]*\(\)$' } |
  ForEach-Object { $_ -replace '\(\)$', '' })
if ($LASTEXITCODE -ne 0 -or $functions.Count -eq 0) {
  throw "Could not discover Qt test functions from $exePath."
}

# Shards are balanced by measured duration (tests/qt-test-weights.json). Longest-processing-time-first:
# the heaviest function goes to the currently lightest shard. Every shard computes the same plan
# because the order is fixed (weight, then name), so each function lands in exactly one shard.
$weightsPath = Join-Path $PSScriptRoot '..\tests\qt-test-weights.json'
$manifest = Get-Content -LiteralPath $weightsPath -Raw | ConvertFrom-Json
$defaultWeight = [double]$manifest.defaultSeconds
$weightOf = @{}
foreach ($entry in $manifest.weights.PSObject.Properties) { $weightOf[$entry.Name] = [double]$entry.Value }
$weightFor = { param($name) if ($weightOf.ContainsKey($name)) { $weightOf[$name] } else { $defaultWeight } }
$ranked = @($functions | Sort-Object @{ Expression = { & $weightFor $_ }; Descending = $true }, @{ Expression = { $_ } })
$load = @(0.0) * $ShardCount
$owner = @{}
foreach ($fn in $ranked) {
  $lightest = 0
  for ($s = 1; $s -lt $ShardCount; ++$s) { if ($load[$s] -lt $load[$lightest]) { $lightest = $s } }
  $load[$lightest] += (& $weightFor $fn)
  $owner[$fn] = $lightest
}
$selected = @($functions | Where-Object { $owner[$_] -eq $ShardIndex })
if ($selected.Count -eq 0) { throw "Qt test shard $ShardIndex has no test functions." }

$shardName = "shard-$ShardIndex"
$runId = if ($env:GOSURVEY_TEST_RUN_ID) { $env:GOSURVEY_TEST_RUN_ID } else { 'manual' }
if ($runId -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid test run identifier.' }
$shardRoot = Join-Path (Split-Path -Parent $workDir) "qt-editor/$runId/$shardName"
$tempDir = Join-Path $shardRoot "temp-$PID"
$artifactDir = Join-Path $shardRoot 'artifacts'
$appDataDir = Join-Path $shardRoot "appdata-$PID"
New-Item -ItemType Directory -Force -Path $tempDir, $artifactDir | Out-Null
New-Item -ItemType Directory -Force -Path $appDataDir | Out-Null

$env:TEMP = $tempDir
$env:TMP = $tempDir
$env:APPDATA = $appDataDir
$env:GOSURVEY_QT_TEST_SHARD = "$shardName-$PID"
$env:GOSURVEY_QT_TEST_ARTIFACT_DIR = $artifactDir
@{ count = $ShardCount; index = $ShardIndex; expected = $functions; selected = $selected } |
  ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $shardRoot 'discovery.json') -Encoding UTF8

$log = Join-Path $shardRoot 'qt-tests.log'
$launcher = Join-Path $PSScriptRoot 'run-qt-tests-hidden.ps1'
$launcherArgs = @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcher,
  '-Exe', $exePath, '-TimeoutSec', "$TimeoutSec", '-Log', $log,
  '-WorkingDirectory', $workDir,
  '-JUnitLog', (Join-Path $artifactDir 'qt-tests.xml')
) + $selected

Write-Host "Qt shard ${ShardIndex}/${ShardCount}: $($selected.Count) of $($functions.Count) test functions."
& powershell @launcherArgs
exit $LASTEXITCODE
