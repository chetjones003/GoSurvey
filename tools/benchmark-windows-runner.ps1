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
$cacheCommands = [Collections.Generic.List[object]]::new()
$previousCachePort = $env:SCCACHE_SERVER_PORT
$report = [ordered]@{ startedUtc = [DateTime]::UtcNow.ToString('o'); passed = $false; stages = @() }

function Save-BenchmarkReport {
  $report.stages = @($stages.ToArray())
  $report.cacheCommands = @($cacheCommands.ToArray())
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

function Invoke-CompilerCacheCommand {
  param([string[]]$ArgumentList, [string]$LogName,
    [ValidateRange(1, 60)][int]$TimeoutSeconds = 30)
  $exe = (Get-Command sccache -CommandType Application).Source
  $stdout = Join-Path $outputDir "$LogName.stdout.log"
  $stderr = Join-Path $outputDir "$LogName.stderr.log"
  $entry = [ordered]@{ arguments = $ArgumentList; passed = $false; timedOut = $false }
  $cacheCommands.Add($entry)
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $process = $null
  Write-Host "sccache $($ArgumentList -join ' ') (limit: ${TimeoutSeconds}s)"
  try {
    # Files avoid an unbounded wait for EOF on a pipe inherited by another process.
    $process = Start-Process -FilePath $exe -ArgumentList $ArgumentList -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $entry.processId = $process.Id
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
      $entry.timedOut = $true
      # Terminate only this client; the daemon may serve another local checkout.
      $process.Kill()
      throw "sccache $($ArgumentList -join ' ') timed out after ${TimeoutSeconds}s. See $LogName logs."
    }
    $entry.exitCode = $process.ExitCode
    if ($process.ExitCode -ne 0) { throw "sccache failed (exit $($process.ExitCode)). See $LogName logs." }
    $entry.passed = $true
    return [IO.File]::ReadAllText($stdout)
  } catch {
    $entry.error = $_.Exception.Message
    try {
      $port = if ($env:SCCACHE_SERVER_PORT) { [int]$env:SCCACHE_SERVER_PORT } else { 4226 }
      $report.cacheDiagnostics = [ordered]@{
        port = $port
        processes = @(Get-CimInstance Win32_Process -Filter "Name='sccache.exe'" |
          Select-Object ProcessId, ParentProcessId, ExecutablePath)
        listeners = @(Get-NetTCPConnection -State Listen -ErrorAction Stop |
          Where-Object LocalPort -EQ $port | Select-Object LocalAddress, LocalPort, OwningProcess)
      }
    } catch { Write-Warning "Could not collect cache process diagnostics: $($_.Exception.Message)" }
    throw $entry.error
  } finally {
    $clock.Stop()
    $entry.seconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
    if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr | Out-Host }
    Save-BenchmarkReport
    if ($process) { $process.Dispose() }
  }
}

function Save-CompilerCacheStats {
  param([string]$Name)
  $json = Invoke-CompilerCacheCommand @('--show-stats', '--stats-format=json') "cache-$Name-json"
  $null = $json | ConvertFrom-Json
  $json | Set-Content -LiteralPath (Join-Path $outputDir "sccache-$Name.json")
  Invoke-CompilerCacheCommand @('--show-stats') "cache-$Name-text" | Out-Host
}

function Start-BenchmarkCompilerCache {
  # Own a separate endpoint, retaining the persistent disk cache. Do not contact
  # an absent/shared daemon first: a filtered closed-port connect can block.
  $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
  try {
    $listener.Start()
    $env:SCCACHE_SERVER_PORT = [string]$listener.LocalEndpoint.Port
  } finally { $listener.Stop() }
  $report.cacheServer = [ordered]@{ port = [int]$env:SCCACHE_SERVER_PORT; started = $false }
  Invoke-CompilerCacheCommand @('--start-server') cache-start | Out-Host
  $report.cacheServer.started = $true
}

function Find-WorkingClangTidy {
  param([string[]]$Candidates)
  $attempts = [Collections.Generic.List[object]]::new()
  foreach ($candidate in ($Candidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $version = @(& $candidate --version 2>&1)
    $code = $LASTEXITCODE
    $text = $version -join "`n"
    $attempts.Add([ordered]@{ path = $candidate; exitCode = $code; output = $text })
    Write-Host "clang-tidy probe: $candidate (exit $code)"
    Write-Host $text
    if ($code -eq 0 -and $text -match 'LLVM version') {
      return [ordered]@{ path = $candidate; version = $text; attempts = @($attempts.ToArray()) }
    }
  }
  throw "No installed clang-tidy passed its version check. Inspected: $($attempts.path -join ', ')"
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
    $report.tools = @('git', 'cmake', 'ctest', 'ninja', 'cl', 'rc', 'python', 'clang-tidy', 'sccache') |
      ForEach-Object {
        $command = Get-Command $_
        [ordered]@{ name = $_; type = [string]$command.CommandType; path = $command.Source }
      }
    $report.tools | ConvertTo-Json | Write-Host
    # Developer shells can put VS-bundled LLVM ahead of the standalone install.
    # Probe actual executables and fail closed if none can start successfully.
    $tidyCandidates = @((Join-Path $env:ProgramFiles 'LLVM\bin\clang-tidy.exe')) +
      @(Get-Command clang-tidy.exe -All -CommandType Application | ForEach-Object Source)
    $report.clangTidy = Find-WorkingClangTidy $tidyCandidates
    $env:Path = (Split-Path -Parent $report.clangTidy.path) + ';' + $env:Path
    foreach ($tool in 'git', 'cmake', 'ctest', 'ninja', 'python') {
      Invoke-BenchmarkCommand $tool @('--version')
    }
    # This benchmark reuses the local disk cache, with no hosted cache downloads.
    $env:SCCACHE_GHA_ENABLED = 'false'
    Invoke-CompilerCacheCommand @('--version') cache-version | Out-Host
    $previousCacheLog = $env:SCCACHE_LOG
    try {
      # Trace command dispatch/IPC only, not backend configuration or credentials.
      $env:SCCACHE_LOG = 'sccache::commands=trace,sccache::client=trace'
      Start-BenchmarkCompilerCache
      Save-CompilerCacheStats before
    } finally { $env:SCCACHE_LOG = $previousCacheLog }
  }
  Measure-BenchmarkStage configure {
    Invoke-BenchmarkCommand cmake @('--preset', 'ninja-release', '-DGOSURVEY_BUILD_TESTS=ON',
      '-DGOSURVEY_BUILD_HEADLESS=ON', '-DGOSURVEY_USE_PCH=OFF',
      '-DCMAKE_C_COMPILER_LAUNCHER=sccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache',
      "-DCMAKE_PREFIX_PATH=$env:GOSURVEY_QT_ROOT")
  }
  $targets = @('--target', 'GoSurveyQt', 'GoSurveyTests', 'gosurvey_headless', 'GoSurveyQtTests')
  Invoke-CompilerCacheCommand @('--zero-stats') cache-zero-initial | Out-Host
  Measure-BenchmarkStage initial_build {
    Invoke-BenchmarkCommand cmake (@('--build', '--preset', 'ninja-release') + $targets)
  }
  Save-CompilerCacheStats initial
  Copy-Item -LiteralPath build/.cmake/release/.ninja_log -Destination (Join-Path $outputDir 'initial.ninja_log')
  Invoke-CompilerCacheCommand @('--zero-stats') cache-zero-warm | Out-Host
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
  $cleanupError = $null
  if ($report.cacheServer.started) {
    try {
      Invoke-CompilerCacheCommand @('--stop-server') cache-stop | Out-Host
      $report.cacheServer.stopped = $true
    } catch {
      $cleanupError = $_.Exception.Message
      $report.cacheServer.cleanupError = $cleanupError
      $report.passed = $false
      Write-Warning "Could not stop the benchmark cache server: $cleanupError"
    }
  }
  $env:SCCACHE_SERVER_PORT = $previousCachePort
  $report.completedUtc = [DateTime]::UtcNow.ToString('o')
  Save-BenchmarkReport
  $lines = @('## Laptop benchmark', '', "Passed: $($report.passed)", '', '| Stage | Seconds | Passed |', '|---|---:|---|')
  foreach ($stage in $stages) { $lines += "| $($stage.name) | $($stage.seconds) | $($stage.passed) |" }
  $lines += @('', 'The initial build may already have cached objects; inspect sccache-initial.json.',
    'The cached rebuild cleans generated targets first. Neither time represents a no-op build.',
    'Raw Ninja logs may contain multiple invocations; use these per-stage wall-clock measurements.')
  $lines | Set-Content -LiteralPath (Join-Path $outputDir 'benchmark.md')
  if ($env:GITHUB_STEP_SUMMARY) { $lines | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
  if ($cleanupError -and -not $report.error) { throw $cleanupError }
}
