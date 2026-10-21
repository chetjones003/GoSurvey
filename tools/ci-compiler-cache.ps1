#Requires -Version 7.2
param(
  [Parameter(Mandatory)][ValidateSet('start', 'stop')][string]$Action,
  [string]$OutputDirectory = 'build/artifacts/ci'
)
$ErrorActionPreference = 'Stop'
if (-not $env:SCCACHE_SERVER_PORT) { throw 'An owned compiler-cache endpoint is required.' }
$exe = (Get-Command sccache -CommandType Application | Select-Object -First 1).Source
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$out = Join-Path $OutputDirectory "cache-$Action.stdout.log"
$err = Join-Path $OutputDirectory "cache-$Action.stderr.log"
$process = Start-Process -FilePath $exe -ArgumentList "--$Action-server" -PassThru -WindowStyle Hidden `
  -RedirectStandardOutput $out -RedirectStandardError $err
try {
  if (-not $process.WaitForExit(30000)) {
    $process.Kill()
    throw "Compiler-cache $Action command exceeded 30 seconds."
  }
  Get-Content -LiteralPath $out | Write-Host
  Get-Content -LiteralPath $err | Write-Host
  if ($process.ExitCode -ne 0) { throw "Compiler-cache $Action failed (exit $($process.ExitCode))." }
} finally { $process.Dispose() }
