#Requires -Version 7.2
param([Parameter(Mandatory)][ValidateSet('build', 'clang-tidy', 'msvc-analyze')][string]$Role)
$ErrorActionPreference = 'Stop'
if (-not $env:GITHUB_ENV -or $env:RUNNER_ENVIRONMENT -ne 'self-hosted') { throw 'Run this only in a self-hosted Actions job.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
    [Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
  throw 'Use the normal interactive Windows runner account.'
}
. 'C:\GoSurveyCI\Enter-GoSurveyEnvironment.ps1'
foreach ($file in 'bin/Qt6Core.dll', 'bin/moc.exe', 'bin/windeployqt.exe', 'lib/cmake/Qt6/Qt6Config.cmake') {
  if (-not (Test-Path -LiteralPath (Join-Path $env:GOSURVEY_QT_ROOT $file))) { throw "Qt kit is incomplete: $file" }
}
if ($Role -eq 'clang-tidy') {
  $tidy = Join-Path $env:ProgramFiles 'LLVM\bin\clang-tidy.exe'
  & $tidy --version
  if ($LASTEXITCODE -ne 0) { throw 'The installed LLVM clang-tidy could not start.' }
  $env:Path = (Split-Path -Parent $tidy) + ';' + $env:Path
}
$env:CMAKE_BUILD_PARALLEL_LEVEL = if ($Role -eq 'build') { '3' } else { '1' }
$env:CTEST_PARALLEL_LEVEL = '6'
$env:CMAKE_PREFIX_PATH = $env:GOSURVEY_QT_ROOT
$env:SCCACHE_GHA_ENABLED = 'false'
if ($Role -eq 'msvc-analyze') {
  # Analysis flags do not share build entries; isolate daemon/disk ownership.
  $env:SCCACHE_DIR = Join-Path $env:LOCALAPPDATA 'GoSurveyCI\sccache-msvc'
  New-Item -ItemType Directory -Force -Path $env:SCCACHE_DIR | Out-Null
}
foreach ($key in @('GOSURVEY_QT_ROOT', 'QT_ROOT_DIR', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH',
  'CMAKE_BUILD_PARALLEL_LEVEL', 'CTEST_PARALLEL_LEVEL', 'CMAKE_PREFIX_PATH', 'SCCACHE_GHA_ENABLED', 'SCCACHE_DIR',
  'SCCACHE_CACHE_SIZE', 'INCLUDE', 'LIB', 'LIBPATH')) {
  $value = [Environment]::GetEnvironmentVariable($key)
  if ($value -and $value -match '[\r\n]') { throw "Unexpected newline in environment value $key." }
  if ($value) { Add-Content -LiteralPath $env:GITHUB_ENV -Value "$key=$value" }
}
Add-Content -LiteralPath $env:GITHUB_ENV -Value "PATH=$env:Path"
if ($Role -ne 'clang-tidy') {
  $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
  try {
    $listener.Start()
    $env:SCCACHE_SERVER_PORT = [string]$listener.LocalEndpoint.Port
  } finally { $listener.Stop() }
  & (Join-Path $PSScriptRoot 'ci-compiler-cache.ps1') -Action start
  Add-Content -LiteralPath $env:GITHUB_ENV -Value "SCCACHE_SERVER_PORT=$env:SCCACHE_SERVER_PORT"
  Add-Content -LiteralPath $env:GITHUB_ENV -Value 'GOSURVEY_CI_CACHE_STARTED=true'
}
Write-Host "Self-hosted role $Role ready; build cap=$env:CMAKE_BUILD_PARALLEL_LEVEL; CTest cap=$env:CTEST_PARALLEL_LEVEL."
