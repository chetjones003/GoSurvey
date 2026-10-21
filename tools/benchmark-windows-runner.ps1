<#
.SYNOPSIS
  Checks the installed laptop environment and measures complete native builds/tests.
.DESCRIPTION
  Run from a normal interactive PowerShell 7 window in a GoSurvey checkout.
  The second build uses --clean-first, so it measures compiler-cache reuse rather
  than a no-op Ninja invocation. Existing source and external cache are retained.
#>
#Requires -Version 7.2
[CmdletBinding()]
param(
  [string]$EnvironmentFile = 'C:\GoSurveyCI\Enter-GoSurveyEnvironment.ps1',
  [ValidateRange(0, 64)][int]$BuildWorkers = 0,
  [ValidateRange(0, 64)][int]$TestWorkers = 0
)

$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run the benchmark in a normal, non-elevated PowerShell window.'
}
if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
  throw 'Run this GUI benchmark in a logged-in Windows desktop session.'
}
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
$outputDir = Join-Path (Get-Location) 'build\artifacts\runner-benchmark'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$stages = [Collections.Generic.List[object]]::new()
$report = [ordered]@{ startedUtc = [DateTime]::UtcNow.ToString('o'); passed = $false; stages = @() }

function Save-BenchmarkReport {
  $report.stages = @($stages.ToArray())
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir 'benchmark.json')
}

function Invoke-BenchmarkCommand {
  param([string]$FilePath, [string[]]$ArgumentList)
  & $FilePath @ArgumentList | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "$FilePath failed (exit $LASTEXITCODE)." }
}

function Measure-BenchmarkStage {
  param([string]$Name, [scriptblock]$Action)
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $passed = $false
  try {
    & $Action
    $passed = $true
  } finally {
    $clock.Stop()
    $stages.Add([ordered]@{ name = $Name; seconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3); passed = $passed })
    Save-BenchmarkReport
    Write-Host "${Name}: $([Math]::Round($clock.Elapsed.TotalSeconds, 2)) seconds; passed=$passed"
  }
}

function Save-CompilerCacheStats {
  param([string]$Name)
  $json = & sccache --show-stats --stats-format=json
  if ($LASTEXITCODE -ne 0) { throw 'Could not collect compiler cache statistics.' }
  $null = ($json -join "`n") | ConvertFrom-Json
  $json | Set-Content -LiteralPath (Join-Path $outputDir "sccache-$Name.json")
  Invoke-BenchmarkCommand sccache @('--show-stats')
}

try {
  Measure-BenchmarkStage preflight {
    if (-not (Test-Path -LiteralPath $EnvironmentFile)) {
      throw "Environment helper missing: $EnvironmentFile. Run tools/setup-windows-build-runner.ps1 first."
    }
    . $EnvironmentFile
    foreach ($tool in 'git', 'cmake', 'ctest', 'ninja', 'cl', 'rc', 'python', 'clang-tidy', 'sccache') {
      if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool is missing from PATH: $tool" }
    }
    foreach ($tool in 'git', 'cmake', 'ctest', 'ninja', 'python', 'clang-tidy', 'sccache') {
      Invoke-BenchmarkCommand $tool @('--version')
    }
    foreach ($required in 'bin\Qt6Core.dll', 'bin\moc.exe', 'bin\windeployqt.exe', 'lib\cmake\Qt6\Qt6Config.cmake') {
      if (-not (Test-Path -LiteralPath (Join-Path $env:GOSURVEY_QT_ROOT $required))) {
        throw "Qt kit is incomplete: $required"
      }
    }
    $computer = Get-CimInstance Win32_ComputerSystem
    if ($BuildWorkers -gt 0) { $env:CMAKE_BUILD_PARALLEL_LEVEL = [string]$BuildWorkers }
    $env:CTEST_PARALLEL_LEVEL = if ($TestWorkers -gt 0) { [string]$TestWorkers } else {
      [string][Math]::Min(8, [int]$env:CMAKE_BUILD_PARALLEL_LEVEL)
    }
    $report.host = [ordered]@{
      processors = @((Get-CimInstance Win32_Processor).Name)
      logicalProcessors = [Environment]::ProcessorCount
      memoryGiB = [Math]::Round($computer.TotalPhysicalMemory / 1GB, 1)
      graphics = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion)
      msvcCompiler = (Get-Command cl).Source
      qtRoot = $env:GOSURVEY_QT_ROOT
      cacheDirectory = $env:SCCACHE_DIR
      buildWorkers = [int]$env:CMAKE_BUILD_PARALLEL_LEVEL
      testWorkers = [int]$env:CTEST_PARALLEL_LEVEL
      sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    }
    $report.host | ConvertTo-Json -Depth 5 | Write-Host
    # This benchmark reuses the local disk cache, with no hosted cache downloads.
    $env:SCCACHE_GHA_ENABLED = 'false'
    Save-CompilerCacheStats before
  }
  Measure-BenchmarkStage configure {
    Invoke-BenchmarkCommand cmake @('--preset', 'ninja-release', '-DGOSURVEY_BUILD_TESTS=ON',
      '-DGOSURVEY_BUILD_HEADLESS=ON', '-DGOSURVEY_USE_PCH=OFF',
      '-DCMAKE_C_COMPILER_LAUNCHER=sccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache',
      "-DCMAKE_PREFIX_PATH=$env:GOSURVEY_QT_ROOT")
  }
  $targets = @('--target', 'GoSurveyQt', 'GoSurveyTests', 'gosurvey_headless', 'GoSurveyQtTests')
  Invoke-BenchmarkCommand sccache @('--zero-stats')
  Measure-BenchmarkStage initial_build {
    Invoke-BenchmarkCommand cmake (@('--build', '--preset', 'ninja-release') + $targets)
  }
  Save-CompilerCacheStats initial
  Copy-Item -LiteralPath build/.cmake/release/.ninja_log -Destination (Join-Path $outputDir 'initial.ninja_log')
  Invoke-BenchmarkCommand sccache @('--zero-stats')
  Measure-BenchmarkStage cached_clean_rebuild {
    Invoke-BenchmarkCommand cmake (@('--build', '--preset', 'ninja-release', '--clean-first') + $targets)
  }
  Save-CompilerCacheStats warm
  Copy-Item -LiteralPath build/.cmake/release/.ninja_log -Destination (Join-Path $outputDir 'warm.ninja_log')
  Measure-BenchmarkStage full_ctest {
    Invoke-BenchmarkCommand ctest @('--preset', 'ninja-release', '--output-junit', (Join-Path $outputDir 'ctest.xml'))
  }
  $report.passed = $true
} catch {
  $report.error = $_.Exception.Message
  throw
} finally {
  $report.completedUtc = [DateTime]::UtcNow.ToString('o')
  Save-BenchmarkReport
  $lines = @('## Laptop benchmark', '', "Passed: $($report.passed)", '', '| Stage | Seconds | Passed |', '|---|---:|---|')
  foreach ($stage in $stages) { $lines += "| $($stage.name) | $($stage.seconds) | $($stage.passed) |" }
  $lines += @('', 'The initial build may already have cached objects; inspect sccache-initial.json.',
    'The cached rebuild cleans generated targets first. Neither time represents a no-op build.',
    'Raw Ninja logs may contain multiple invocations; use these per-stage wall-clock measurements.')
  $lines | Set-Content -LiteralPath (Join-Path $outputDir 'benchmark.md')
  if ($env:GITHUB_STEP_SUMMARY) { $lines | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
}
