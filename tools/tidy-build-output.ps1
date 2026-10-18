param([Parameter(Mandatory = $true)][string]$RuntimeDirectory)

$ErrorActionPreference = 'Stop'
$buildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build'))
$runtimeRoot = [IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\', '/')
if ($runtimeRoot -ne $buildRoot -and
    -not $runtimeRoot.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
  throw "Output cleanup must stay inside this checkout's build directory: $runtimeRoot"
}
if (-not (Test-Path -LiteralPath $runtimeRoot)) { return }

# Preserve runtime assets and configurations. Archive old captures, helper programs,
# generator files and experimental folders rather than deleting potentially useful evidence.
$keepDirectories = @('.cmake', 'artifacts', 'resources', 'qt-plugins',
  'debug', 'release', 'relwithdebinfo', 'minsizerel')
if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'qt.conf'))) {
  # An ImGui-only build may coexist with a Qt executable from the old layout.
  $keepDirectories += @('generic', 'iconengines', 'imageformats', 'networkinformation',
    'platforms', 'styles', 'tls')
}
$keepFiles = @('GoSurvey.exe', 'GoSurveyTestImGui.exe', 'GoSurveyQt.exe',
  'GoSurveyQtTests.exe', 'vc_redist.x64.exe', 'qt.conf')
$archiveRoot = Join-Path $buildRoot ('artifacts/legacy-output/' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
foreach ($item in Get-ChildItem -LiteralPath $runtimeRoot -Force) {
  if ($item.PSIsContainer -and $item.Name -in $keepDirectories) { continue }
  if (-not $item.PSIsContainer -and
      ($item.Name -in $keepFiles -or $item.Extension -in @('.dll', '.ini'))) { continue }
  # Both resolved paths are checked before moving a directory tree.
  $source = [IO.Path]::GetFullPath($item.FullName)
  $destination = [IO.Path]::GetFullPath((Join-Path $archiveRoot $item.Name))
  if (-not $source.StartsWith($runtimeRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
      -not $destination.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing output cleanup outside the build directory: $source -> $destination"
  }
  New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
  Move-Item -LiteralPath $source -Destination $destination
  Write-Host "Archived old build output: $($item.Name)"
}
