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

$selected = @()
for ($i = 0; $i -lt $functions.Count; ++$i) {
  if (($i % $ShardCount) -eq $ShardIndex) { $selected += $functions[$i] }
}
if ($selected.Count -eq 0) { throw "Qt test shard $ShardIndex has no test functions." }

$shardName = "shard-$ShardIndex"
$shardRoot = Join-Path (Split-Path -Parent $workDir) "qt-editor/$shardName-$PID"
$tempDir = Join-Path $shardRoot 'temp'
$artifactDir = Join-Path $shardRoot 'artifacts'
$appDataDir = Join-Path $shardRoot 'appdata'
New-Item -ItemType Directory -Force -Path $tempDir, $artifactDir | Out-Null
New-Item -ItemType Directory -Force -Path $appDataDir | Out-Null

$env:TEMP = $tempDir
$env:TMP = $tempDir
$env:APPDATA = $appDataDir
$env:GOSURVEY_QT_TEST_SHARD = "$shardName-$PID"
$env:GOSURVEY_QT_TEST_ARTIFACT_DIR = $artifactDir

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
