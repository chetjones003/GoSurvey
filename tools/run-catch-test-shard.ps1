param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][int]$ShardIndex,
  [Parameter(Mandatory = $true)][int]$ShardCount,
  [Parameter(Mandatory = $true)][string]$WorkingDirectory,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
if ($ShardCount -lt 1 -or $ShardIndex -lt 0 -or $ShardIndex -ge $ShardCount) { throw 'Invalid Catch2 shard.' }
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$runId = if ($env:GOSURVEY_TEST_RUN_ID) { $env:GOSURVEY_TEST_RUN_ID } else { 'manual' }
if ($runId -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid test run identifier.' }
$root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
  (Join-Path $OutputDirectory "$runId/shard-$ShardIndex"))
$private = Join-Path $root "private-$PID"
New-Item -ItemType Directory -Force -Path $root, $private | Out-Null
$env:TEMP = $private
$env:TMP = $private
$env:APPDATA = $private
$env:LOCALAPPDATA = $private
$discovery = Join-Path $root 'discovered.xml'
$xml = Join-Path $root 'results.xml'
$log = Join-Path $root 'catch-tests.log'
Push-Location -LiteralPath $WorkingDirectory
try {
  & $exePath --list-tests --order lex --rng-seed 1 --reporter xml --out $discovery
  if ($LASTEXITCODE -ne 0) { throw 'Catch2 discovery failed.' }
  [xml]$tests = Get-Content -LiteralPath $discovery -Raw -Encoding UTF8
  $names = @($tests.MatchingTests.TestCase | ForEach-Object { [string]$_.Name })
  if ($names.Count -eq 0) { throw 'Catch2 discovered no test cases.' }
  @{ count = $ShardCount; index = $ShardIndex; expected = $names } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'discovery.json') -Encoding UTF8
  & $exePath --order lex --rng-seed 1 --shard-count $ShardCount --shard-index $ShardIndex `
    --durations yes --reporter "xml::out=$xml" --reporter "console::out=$log"
  $code = $LASTEXITCODE
  Get-Content -LiteralPath $log -Tail 5 | Out-Host
  Write-Host "Catch2 shard $ShardIndex/$ShardCount finished with exit $code; reports: $root"
  exit $code
} finally { Pop-Location }
