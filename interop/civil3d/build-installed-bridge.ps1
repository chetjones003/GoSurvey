param(
  [Parameter(Mandatory = $true)][string]$RuntimeDirectory
)

$ErrorActionPreference = 'Stop'
$autocadRoot = Get-ChildItem -LiteralPath 'C:\Program Files\Autodesk' -Directory -ErrorAction SilentlyContinue |
  Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'C3D\AeccDbMgd.dll') } |
  Sort-Object Name -Descending |
  Select-Object -First 1
if ($null -eq $autocadRoot) {
  Write-Host 'Civil 3D bridge skipped: no Civil 3D API installation was found.'
  exit 0
}

$runtimeConfigPath = Join-Path $autocadRoot.FullName 'acdbmgd.runtimeconfig.json'
$runtimeMajor = 8
if (Test-Path -LiteralPath $runtimeConfigPath) {
  $runtimeConfig = Get-Content -LiteralPath $runtimeConfigPath -Raw | ConvertFrom-Json
  $framework = $runtimeConfig.runtimeOptions.frameworks |
    Where-Object { $_.name -eq 'Microsoft.NETCore.App' } |
    Select-Object -First 1
  if ($framework -and $framework.version -match '^(\d+)\.') {
    $runtimeMajor = [int]$Matches[1]
  }
}
$runtime = Get-ChildItem -LiteralPath (Join-Path $env:ProgramFiles 'dotnet\shared\Microsoft.NETCore.App') `
    -Directory -ErrorAction SilentlyContinue |
  Where-Object { try { ([version]$_.Name).Major -eq $runtimeMajor } catch { $false } } |
  Sort-Object { [version]$_.Name } -Descending |
  Select-Object -First 1
$compilerCandidates = @(
  (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'),
  (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'),
  (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\Roslyn\csc.exe'),
  (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\Roslyn\csc.exe'),
  (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\Roslyn\csc.exe')
)
$compiler = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($null -eq $runtime -or $null -eq $compiler) {
  Write-Host "Civil 3D bridge skipped: .NET $runtimeMajor runtime references or Roslyn csc.exe were not found."
  exit 0
}

$runtimeOutput = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
& (Join-Path $PSScriptRoot 'build-bridge.ps1') `
  -AutoCADRoot $autocadRoot.FullName `
  -DotnetRuntimeDirectory $runtime.FullName `
  -CSharpCompiler $compiler `
  -OutputPath (Join-Path $runtimeOutput 'GoSurveyCivilExport.dll') `
  -DefineConstants 'GOSURVEY_EXPORT_ONLY'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot 'build-bridge.ps1') `
  -AutoCADRoot $autocadRoot.FullName `
  -DotnetRuntimeDirectory $runtime.FullName `
  -CSharpCompiler $compiler `
  -OutputPath (Join-Path $runtimeOutput 'GoSurveyCivilImport.dll') `
  -DefineConstants 'GOSURVEY_IMPORT_ONLY'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
