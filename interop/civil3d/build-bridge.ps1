param(
  [Parameter(Mandatory = $true)][string]$AutoCADRoot,
  [Parameter(Mandatory = $true)][string]$DotnetRuntimeDirectory,
  [Parameter(Mandatory = $true)][string]$CSharpCompiler,
  [Parameter(Mandatory = $true)][string]$OutputPath,
  [string]$DefineConstants
)

$ErrorActionPreference = 'Stop'
$civilRoot = Join-Path $AutoCADRoot 'C3D'
$references = @(
  (Join-Path $AutoCADRoot 'acmgd.dll'),
  (Join-Path $AutoCADRoot 'acdbmgd.dll'),
  (Join-Path $AutoCADRoot 'accoremgd.dll'),
  (Join-Path $AutoCADRoot 'ACA\AecBaseMgd.dll'),
  (Join-Path $civilRoot 'AeccDbMgd.dll'),
  (Join-Path $civilRoot 'AeccCogoMgd.dll')
)

foreach ($reference in $references) {
  if (!(Test-Path -LiteralPath $reference)) {
    throw "Required Civil 3D API reference not found: $reference"
  }
}

$runtimeReferences = Get-ChildItem -LiteralPath $DotnetRuntimeDirectory -Filter '*.dll' |
  Where-Object { $_.Name -notmatch '^(System\.IO\.Compression\.Native|msquic|mscorrc|mscordbi|mscordaccore.*|Microsoft\.DiaSymReader\.Native|hostpolicy|coreclr|clrjit|clrgc|clretwrc)' } |
  ForEach-Object { $_.FullName }
$arguments = @('/noconfig', '/nostdlib', '/target:library', "/out:$OutputPath")
if ($DefineConstants) { $arguments += "/define:$DefineConstants" }
$arguments += $references | ForEach-Object { "/reference:$_" }
$arguments += $runtimeReferences | ForEach-Object { "/reference:$_" }
$arguments += (Join-Path $PSScriptRoot 'GoSurveyCivilBridge.cs')

& $CSharpCompiler @arguments
if ($LASTEXITCODE -ne 0) {
  throw "Civil 3D bridge compile failed with exit code $LASTEXITCODE."
}
