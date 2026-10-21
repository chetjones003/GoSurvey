<#
.SYNOPSIS
  Installs GoSurvey's native build tools on a Windows 11 x64 machine.
.DESCRIPTION
  Run from an administrator PowerShell window. Uses WinGet's package verification,
  and installs Qt through an isolated aQt environment. No GitHub credential is
  requested and no runner is registered. Launch the runner as a normal user after
  following docs/windows-self-hosted-runner.md.
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-windows-build-runner.ps1 -PlanOnly
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-windows-build-runner.ps1 -PreventSleep
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
  [string]$InstallRoot = 'C:\GoSurveyCI',
  [string]$QtDirectory = 'C:\Qt',
  [switch]$PreventSleep,
  [switch]$PlanOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$qtVersion = '6.8.3'
$qtRoot = Join-Path $QtDirectory "$qtVersion\msvc2022_64"
$packages = @('Git.Git', 'Kitware.CMake', 'Ninja-build.Ninja', 'Microsoft.PowerShell',
              'Python.Python.3.13', 'LLVM.LLVM', 'Mozilla.sccache', 'GitHub.cli', '7zip.7zip')

if ($PlanOnly) {
  Write-Host 'Windows 11 x64 setup plan (no downloads or changes):'
  Write-Host "  WinGet packages: $($packages -join ', ')"
  Write-Host '  MSVC: use an existing C++ toolchain or install VS 2022 Build Tools + Windows SDK.'
  Write-Host "  Qt: $qtRoot via isolated aqtinstall 3.3.0."
  Write-Host "  Environment and interactive runner launchers: $InstallRoot"
  Write-Host '  Compiler cache: per-user LOCALAPPDATA\GoSurveyCI\sccache, outside checkouts.'
  if ($PreventSleep) { Write-Host '  Disable automatic sleep/hibernate timeout while plugged in.' }
  Write-Host '  Register the runner separately in GitHub; no inbound network port is needed.'
  return
}

if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
  throw 'Use a 64-bit PowerShell on Windows 11 x64.'
}
$os = Get-CimInstance Win32_OperatingSystem
if ($os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 22000) {
  throw 'This installer targets Windows 11. Windows Server needs a separate provisioning path.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Open PowerShell with Run as administrator, then run this setup script again.'
}
$winget = Get-Command winget.exe -ErrorAction SilentlyContinue
if (-not $winget) {
  throw 'Install/update Microsoft App Installer first: https://apps.microsoft.com/detail/9nblggh4nns1'
}
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)
$QtDirectory = [IO.Path]::GetFullPath($QtDirectory)
$qtRoot = Join-Path $QtDirectory "$qtVersion\msvc2022_64"
$drive = Get-PSDrive -Name ([IO.Path]::GetPathRoot($InstallRoot).TrimEnd('\').TrimEnd(':'))
if ($drive.Free -lt 30GB) { throw 'Allow at least 30 GB free space for the tools, Qt, builds, and cache.' }
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null

function Invoke-SetupCommand {
  param([string]$FilePath, [string[]]$ArgumentList)
  & $FilePath @ArgumentList | Out-Host
  $code = $LASTEXITCODE
  if ($code -in @(1641, 3010)) {
    throw 'An installer requested a restart. Restart Windows, then rerun this script.'
  }
  if ($code -ne 0) { throw "$FilePath failed (exit $code). Resolve the error and rerun setup." }
}

function Install-WinGetPackage {
  param([string]$Id)
  & $winget.Source list --id $Id --exact --source winget --disable-interactivity --accept-source-agreements | Out-Host
  if ($LASTEXITCODE -eq 0) { return }
  Write-Host "Installing $Id ..."
  Invoke-SetupCommand $winget.Source @('install', '--id', $Id, '--exact', '--source', 'winget',
    '--silent', '--disable-interactivity', '--accept-source-agreements', '--accept-package-agreements')
}

foreach ($package in $packages) { Install-WinGetPackage $package }
# Refresh this process after installers update Windows' stored PATH.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = ''
if (Test-Path -LiteralPath $vswhere) {
  $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($LASTEXITCODE -ne 0) { throw 'vswhere could not inspect the installed C++ toolchain.' }
}
if (-not $vsPath) {
  Write-Host 'Installing MSVC and the Windows SDK; this can take several minutes ...'
  Invoke-SetupCommand $winget.Source @('install', '--id', 'Microsoft.VisualStudio.2022.BuildTools',
    '--exact', '--source', 'winget', '--force', '--disable-interactivity',
    '--accept-source-agreements', '--accept-package-agreements', '--override',
    '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
  $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($LASTEXITCODE -ne 0 -or -not $vsPath) {
    throw 'The C++ workload is missing. Add Desktop development with C++ in Visual Studio Installer, then rerun.'
  }
}

$qtFiles = @('bin\Qt6Core.dll', 'bin\moc.exe', 'bin\windeployqt.exe', 'lib\cmake\Qt6\Qt6Config.cmake')
$missingQt = @($qtFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $qtRoot $_)) })
if ($missingQt.Count -gt 0) {
  $venv = Join-Path $InstallRoot 'aqt-env'
  $python = Join-Path $venv 'Scripts\python.exe'
  if (-not (Test-Path -LiteralPath $python)) {
    Invoke-SetupCommand 'py.exe' @('-3.13', '-m', 'venv', $venv)
  }
  Invoke-SetupCommand $python @('-m', 'pip', 'install', 'aqtinstall==3.3.0')
  Invoke-SetupCommand $python @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $qtVersion,
    'win64_msvc2022_64', '--outputdir', $QtDirectory)
}
foreach ($required in $qtFiles) {
  if (-not (Test-Path -LiteralPath (Join-Path $qtRoot $required))) {
    throw "Qt installation is incomplete: $required is missing from $qtRoot."
  }
}

# These scripts set process-local settings so each runner user owns their cache.
$environmentScript = @'
# Dot-source this file in a normal-user PowerShell window before a manual build.
$ErrorActionPreference = 'Stop'
$env:GOSURVEY_QT_ROOT = '__QT_ROOT__'
$env:QT_ROOT_DIR = $env:GOSURVEY_QT_ROOT
$env:QT_PLUGIN_PATH = Join-Path $env:GOSURVEY_QT_ROOT 'plugins'
$env:QML2_IMPORT_PATH = Join-Path $env:GOSURVEY_QT_ROOT 'qml'
$env:Path = (Join-Path $env:GOSURVEY_QT_ROOT 'bin') + ';' +
  [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
  [Environment]::GetEnvironmentVariable('Path', 'User')
$env:SCCACHE_DIR = Join-Path $env:LOCALAPPDATA 'GoSurveyCI\sccache'
$env:SCCACHE_CACHE_SIZE = '8G'
$env:SCCACHE_GHA_ENABLED = 'false'
New-Item -ItemType Directory -Force -Path $env:SCCACHE_DIR | Out-Null
$memory = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB
$cpuBudget = [Math]::Max(1, [Environment]::ProcessorCount - 1)
$memoryBudget = [Math]::Max(1, [Math]::Floor(($memory - 3) / 2))
$workers = [int][Math]::Min($cpuBudget, $memoryBudget)
$env:CMAKE_BUILD_PARALLEL_LEVEL = [string]$workers
$env:CTEST_PARALLEL_LEVEL = [string]$workers
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsPath) { throw 'MSVC C++ tools were not found.' }
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Write-Host "GoSurvey tools ready; $workers build/test workers; cache at $env:SCCACHE_DIR"
'@
$environmentScript = $environmentScript.Replace('__QT_ROOT__', $qtRoot.Replace("'", "''"))
$environmentPath = Join-Path $InstallRoot 'Enter-GoSurveyEnvironment.ps1'
Set-Content -LiteralPath $environmentPath -Value $environmentScript -Encoding UTF8
$runnerScript = @'
#Requires -Version 7.2
param([string]$RunnerDirectory = (Join-Path $env:USERPROFILE 'actions-runner'))
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Start the runner from a normal PowerShell window, not Run as administrator.'
}
if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
  throw 'Use a logged-in Windows session for the GUI-test runner, not a Windows service.'
}
if (-not (Test-Path -LiteralPath (Join-Path $RunnerDirectory '.runner'))) {
  throw 'Register this runner first using the commands from GitHub Settings > Actions > Runners.'
}
. (Join-Path $PSScriptRoot 'Enter-GoSurveyEnvironment.ps1')
Push-Location -LiteralPath $RunnerDirectory
try {
  & .\run.cmd
  if ($LASTEXITCODE -ne 0) { throw "GitHub runner stopped with exit $LASTEXITCODE." }
} finally { Pop-Location }
'@
Set-Content -LiteralPath (Join-Path $InstallRoot 'Start-GoSurveyRunner.ps1') -Value $runnerScript -Encoding UTF8

if ($PreventSleep) {
  Invoke-SetupCommand 'powercfg.exe' @('/change', 'standby-timeout-ac', '0')
  Invoke-SetupCommand 'powercfg.exe' @('/change', 'hibernate-timeout-ac', '0')
}
foreach ($tool in @('git', 'cmake', 'ninja', 'pwsh', 'clang-tidy', 'sccache', 'gh', 'py')) {
  if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
    throw "$tool is not on PATH. Open a new administrator PowerShell window and rerun setup."
  }
}
Write-Host "Setup finished. MSVC: $vsPath; Qt: $qtRoot"
Write-Host 'Next, use a normal (non-administrator) PowerShell 7 window under the runner account.'
Write-Host 'Register Windows x64 here: https://github.com/chetjones003/GoSurvey/settings/actions/runners/new'
Write-Host 'Keep registration in that account''s own actions-runner folder and decline service mode for GUI testing.'
Write-Host "Then run: pwsh -NoProfile -ExecutionPolicy Bypass -File `"$InstallRoot\Start-GoSurveyRunner.ps1`""
Write-Host 'The workflows still use hosted runners until we configure trusted job routing and validate this machine.'
