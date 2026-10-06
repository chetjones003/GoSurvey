# Downloads the Microsoft Visual C++ 2015–2022 x64 redistributable into build/ for Inno Setup.
# CI runs the same download in .github/workflows/release.yml; use this for local installer builds.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root 'build\vc_redist.x64.exe'
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
if (Test-Path $out) {
  Write-Host "Already present: $out"
  exit 0
}
Write-Host "Downloading VC++ redistributable to $out"
Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $out
Write-Host "Done."
