#Requires -Version 7.2
# Resolve a pinned CI tool on a self-hosted Windows runner, downloading it once into the
# runner user's local tool cache. Prints the executable path. Nothing is stored in GitHub.
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
$installDir = Join-Path $CacheRoot "$Name-$($tool.Version)"
$exe = Join-Path $installDir $tool.Exe
if (Test-Path -LiteralPath $exe) { Write-Output $exe; exit 0 }

New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
$download = Join-Path $CacheRoot ([IO.Path]::GetFileName($tool.Url))
Invoke-WebRequest -Uri $tool.Url -OutFile $download
$hash = (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $tool.Sha256) {
  Remove-Item -LiteralPath $download -Force
  throw "$Name download SHA256 mismatch: $hash"
}
try {
  if ($download.EndsWith('.zip')) {
    Expand-Archive -LiteralPath $download -DestinationPath $installDir -Force
  } else {
    # Inno Setup installs per user without elevation.
    $process = Start-Process -FilePath $download -Wait -PassThru -ArgumentList @(
      '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS', "/DIR=`"$installDir`"")
    if ($process.ExitCode -ne 0) { throw "$Name installer exited with $($process.ExitCode)." }
  }
} finally {
  Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $exe)) { throw "$Name install did not produce $exe." }
Write-Output $exe
