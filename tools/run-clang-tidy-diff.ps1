#Requires -Version 7.2
param(
  [Parameter(Mandatory = $true)][string]$BaseSha,
  [string]$BuildDirectory = 'build/.cmake/release',
  [string]$OutputDirectory = 'build/artifacts/ci/clang-tidy',
  [ValidateRange(1, 16)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$buildDir = (Resolve-Path -LiteralPath $BuildDirectory).Path
$outputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$repoRoot = (Get-Location).Path
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$tool = (Get-Command clang-tidy -CommandType Application | Select-Object -First 1).Source
$version = @(& $tool --version 2>&1)
if ($LASTEXITCODE -ne 0) { throw 'clang-tidy could not start.' }
$changed = @(git diff --name-only --diff-filter=ACMR $BaseSha HEAD)
if ($LASTEXITCODE -ne 0) { throw 'Could not read the analyzer diff.' }
$changed = @($changed | Where-Object { $_ -match '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' -and
  $_ -notmatch '^third_party/' -and (Test-Path -LiteralPath $_) })
$filters = @()
$files = @()
foreach ($file in $changed) {
  $diff = @(git diff -U0 $BaseSha HEAD -- $file)
  if ($LASTEXITCODE -ne 0) { throw "Could not read hunks for $file" }
  $ranges = @()
  foreach ($line in $diff) {
    if ($line -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@') {
      $start = [int]$matches[1]
      $count = if ($matches[2]) { [int]$matches[2] } else { 1 }
      if ($count -gt 0) { $ranges += ,@($start, ($start + $count - 1)) }
    }
  }
  if ($ranges.Count -eq 0) { continue }
  $filters += @{ name = $file.Replace('/', '\'); lines = $ranges }
  if ($file.Contains('/')) { $filters += @{ name = $file; lines = $ranges } }
  $files += $file
}
$report = [ordered]@{ baseSha = $BaseSha; headSha = (git rev-parse HEAD); lineFilters = $filters;
  tool = $tool; version = ($version -join "`n"); workers = $Jobs; files = $files; results = @() }
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir 'scope.json')
if ($files.Count -eq 0) { Write-Host 'No added or modified C/C++ lines to analyze.'; return }
if ($files -contains 'tests/QtEditorTests.cpp') {
  & cmake -E cmake_autogen (Join-Path $buildDir 'CMakeFiles/GoSurveyQtTests_autogen.dir/AutogenInfo.json') Release
  if ($LASTEXITCODE -ne 0) { throw 'Qt test MOC generation failed.' }
}
$lineFilter = ConvertTo-Json -InputObject @($filters) -Depth 6 -Compress
$config = (Resolve-Path -LiteralPath .clang-tidy).Path
$results = @($files | ForEach-Object -Parallel {
  $file = $_
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $output = @(& $using:tool --enable-check-profile "--store-check-profile=$using:outputDir" `
    "--config-file=$using:config" --warnings-as-errors=* "-line-filter=$using:lineFilter" `
    -p $using:buildDir (Join-Path $using:repoRoot $file) 2>&1)
  $code = $LASTEXITCODE
  [pscustomobject]@{ file = $file; exitCode = $code; seconds = $clock.Elapsed.TotalSeconds; output = $output }
} -ThrottleLimit $Jobs)
$failed = $false
foreach ($result in $results) {
  $name = $result.file -replace '[^A-Za-z0-9_-]', '_'
  $result.output | Set-Content -LiteralPath (Join-Path $outputDir "$name.log")
  $result.output | Out-Host
  Write-Host "clang-tidy: $($result.file), exit $($result.exitCode), $([Math]::Round($result.seconds, 2))s"
  if ($result.exitCode -ne 0) { $failed = $true }
}
$report.results = @($results | Select-Object file, exitCode, seconds)
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir 'scope.json')
if ($failed) { throw 'clang-tidy failed on one or more diff-scoped translation units.' }
