#Requires -Version 7.2
# Resolve a pinned CI tool on a self-hosted Windows runner. The verified download is cached in
# the runner user's local tool cache. Prints the executable path. Nothing is stored in GitHub.
[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidateSet('gitleaks', 'trivy', 'innosetup')][string]$Name,
  [string]$CacheRoot = (Join-Path $env:LOCALAPPDATA 'GoSurveyCI\tools')
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$tools = @{
  gitleaks  = @{ Version = '8.30.1'; Exe = 'gitleaks.exe'
    Url = 'https://github.com/gitleaks/gitleaks/releases/download/v8.30.1/gitleaks_8.30.1_windows_x64.zip'
    Sha256 = 'd29144deff3a68aa93ced33dddf84b7fdc26070add4aa0f4513094c8332afc4e' }
  trivy     = @{ Version = '0.75.0'; Exe = 'trivy.exe'
    Url = 'https://github.com/aquasecurity/trivy/releases/download/v0.75.0/trivy_0.75.0_windows-64bit.zip'
    Sha256 = '4e43bd71a30f51aee39525f60f2b47043af77eb8df8fe082aae4372b69c6660f' }
  innosetup = @{ Version = '6.7.4'; Exe = 'ISCC.exe'
    Url = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe'
    Sha256 = 'a549929319f9120716677c685535ca4671e4b937b5fdc87e2585f32d8eb30e8c' }
}
$tool = $tools[$Name]

# The verified download is cached once per user; every use re-checks its SHA256 and unpacks
# into this job's own temp directory, so a stale, partial or tampered cache entry is never run
# and concurrent jobs never share an install directory.
New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
$archive = Join-Path $CacheRoot ([IO.Path]::GetFileName($tool.Url))
function Test-Archive { (Test-Path -LiteralPath $archive) -and
  (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -eq $tool.Sha256 }
if (-not (Test-Archive)) {
  $partial = "$archive.$([Guid]::NewGuid().ToString('N')).partial"
  try {
    Invoke-WebRequest -Uri $tool.Url -OutFile $partial
    $hash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $tool.Sha256) { throw "$Name download SHA256 mismatch: $hash" }
    # A concurrent job may have published the same verified archive first; that is fine.
    try { Move-Item -LiteralPath $partial -Destination $archive -Force } catch { if (-not (Test-Archive)) { throw } }
  } finally {
    Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
  }
  if (-not (Test-Archive)) { throw "$Name cached archive failed verification." }
}

$jobRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$installDir = Join-Path $jobRoot "$Name-$($tool.Version)-$([Guid]::NewGuid().ToString('N'))"
if ($archive.EndsWith('.zip')) {
  Expand-Archive -LiteralPath $archive -DestinationPath $installDir
} else {
  # Inno Setup installs per user without elevation.
  $process = Start-Process -FilePath $archive -Wait -PassThru -ArgumentList @(
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS', "/DIR=`"$installDir`"")
  if ($process.ExitCode -ne 0) { throw "$Name installer exited with $($process.ExitCode)." }
}
$exe = Join-Path $installDir $tool.Exe
if (-not (Test-Path -LiteralPath $exe)) { throw "$Name install did not produce $exe." }
Write-Output $exe
