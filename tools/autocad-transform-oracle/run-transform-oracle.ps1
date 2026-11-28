# Runs transform-oracle.lsp in AutoCAD's accoreconsole and writes the results to
# build/artifacts/autocad-transform-oracle/oracle.txt (ADR-0011 / issue #1577).
#
# Each "### <case>" line is one entity's DXF groups after the command. The entity-kind tests
# (tests/Document/EntityKindOpsAllKindsTests.cpp, tests/EntityKindOpsTests.cpp) quote these values.
param(
  [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\build\artifacts\autocad-transform-oracle'),
  [string]$AccoreConsole = '',
  [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($AccoreConsole)) {
  foreach ($year in 2027, 2026, 2025) {
    $candidate = "C:\Program Files\Autodesk\AutoCAD $year\accoreconsole.exe"
    if (Test-Path -LiteralPath $candidate) { $AccoreConsole = $candidate; break }
  }
}
if (-not (Test-Path -LiteralPath $AccoreConsole)) {
  throw 'accoreconsole.exe not found (pass -AccoreConsole explicitly)'
}

$out = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$lisp = (Join-Path $PSScriptRoot 'transform-oracle.lsp') -replace '\\', '/'
$script = Join-Path $out 'oracle.scr'
# SECURELOAD 0 lets the console load the script from this folder without a trust prompt.
@('SECURELOAD 0', "(load `"$lisp`")", 'TRANSFORMORACLE', 'QUIT', 'Y') |
  Set-Content -LiteralPath $script -Encoding ASCII

$raw = Join-Path $out 'accoreconsole.out.txt'
$proc = Start-Process -FilePath $AccoreConsole -ArgumentList @('/s', "`"$script`"", '/l', 'en-US') `
  -RedirectStandardOutput $raw -PassThru -NoNewWindow
if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
  $proc.Kill()
  throw "accoreconsole did not finish within $TimeoutSeconds s (see $raw)"
}

# accoreconsole writes UTF-16; strip the NULs and keep only the result lines.
$lines = ((Get-Content -LiteralPath $raw -Raw) -replace "`0", '') -split "`n" |
  Where-Object { $_ -match '^### ' } | ForEach-Object { $_.TrimEnd() }
$summary = Join-Path $out 'oracle.txt'
@("accoreconsole: $AccoreConsole", "timestamp: $(Get-Date -Format o)", '') + $lines |
  Set-Content -LiteralPath $summary -Encoding UTF8
if (-not ($lines -match '^### done')) { throw "oracle did not complete (see $raw)" }
Write-Output "Wrote $summary ($($lines.Count) lines)"
