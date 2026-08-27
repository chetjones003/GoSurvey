<#
.SYNOPSIS
  Builds samples/font-demo.gs — the visual fixture for issue #115's font/text audit.

.DESCRIPTION
  Issue #115's Testing section asks for a drawing carrying as many text types as possible, so the same
  font can be compared across object types and between Model Space, Paper Space, and Paper Space
  viewports at different scales.

  The geometry is built by the headless driver from
  tests/headless/transcripts/issue115-font-matrix.txt, so the sample is reproducible rather than
  hand-authored. This script then injects model MTEXT (the on-screen editor is not reachable from a
  transcript), stamps font families, assigns a real entity id, and forces viewport scales to 1:25 and
  1:100 so REQ-050's plotted-height rule is obvious on screen.

  Fonts are assigned round-robin across Arial / Times New Roman / romans.shx so one drawing shows a
  TrueType family, a second TrueType family, and an SHX stroke font side by side.

  Output is UTF-8 without a BOM: LoadGoSurveyFile feeds the bytes to nlohmann JSON, which rejects a BOM.

.EXAMPLE
  pwsh tools/Make-FontDemoGs.ps1
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = '',
  [string]$OutFile  = ''
)

$ErrorActionPreference = 'Stop'

if (-not $RepoRoot) {
  $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
  $RepoRoot = Split-Path -Parent $here
}

$headless = Join-Path $RepoRoot 'build/gosurvey_headless.exe'
if (-not (Test-Path $headless)) { throw "headless driver not built: $headless" }

$transcript = Join-Path $RepoRoot 'tests/headless/transcripts/issue115-font-matrix.txt'
if (-not (Test-Path $transcript)) { throw "transcript missing: $transcript" }

if (-not $OutFile) { $OutFile = Join-Path $RepoRoot 'samples/font-demo.gs' }

function Write-Utf8NoBom([string]$Path, [string]$Content) {
  $enc = New-Object System.Text.UTF8Encoding $false
  [System.IO.File]::WriteAllText($Path, $Content, $enc)
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("fontdemo-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
  & $headless run $transcript --out $work | Write-Verbose
  if ($LASTEXITCODE -ne 0) { throw "transcript failed (exit $LASTEXITCODE)" }

  $built = Join-Path $work 'font-matrix-full.gs'
  if (-not (Test-Path $built)) { throw "transcript produced no font-matrix-full.gs" }

  $root = Get-Content -LiteralPath $built -Raw -Encoding UTF8 | ConvertFrom-Json

  # MTEXT content comes from the on-screen editor, so the transcript cannot place one. Inject a model
  # MTEXT inside the extents so both viewports show it — this is the entity REQ-050 sizes.
  $nextId = [int64]$root.document.nextEntityId
  if ($nextId -lt 1) { $nextId = 1 }

  $mtext = [pscustomobject]@{
    kind                   = 'mtext'
    insX                   = 80.0
    insY                   = 80.0
    plottedHeightInches    = 0.25
    rotationRad            = 0.0
    text                   = 'MODEL MTEXT'
    boxMinX                = 80.0
    boxMinY                = 65.0
    boxMaxX                = 120.0
    boxMaxY                = 80.0
    mtextAttach            = 1
    surveyPointLabelForId  = -1
    fontFamily             = 'Arial'
  }
  $mtextAttr = [pscustomobject]@{
    layer         = '0'
    id            = $nextId
    color         = 'ByLayer'
    linetype      = 'ByLayer'
    lineweightMm  = -1.0
    transparency  = -1.0
  }
  $root.document.nextEntityId = $nextId + 1

  $root.document.annotations = @($mtext) + @($root.document.annotations)
  $root.document.annotationAttrs = @($mtextAttr) + @($root.document.annotationAttrs)

  $fonts = @('Arial', 'Times New Roman', 'romans.shx')
  $i = 0
  foreach ($a in @($root.document.annotations)) {
    $a | Add-Member -NotePropertyName fontFamily -NotePropertyValue $fonts[$i % $fonts.Count] -Force
    $i++
  }
  foreach ($layout in @($root.document.paperLayouts)) {
    foreach ($t in @($layout.paperTexts)) {
      $t | Add-Member -NotePropertyName fontFamily -NotePropertyValue $fonts[$i % $fonts.Count] -Force
      $i++
    }
    if ($layout.viewports -and @($layout.viewports).Count -ge 2) {
      # ZOOMEXTENTS in the transcript frames the model; these explicit scales are the visual REQ-050
      # check (1:25 vs 1:100 → 4× model-height difference, constant plotted height).
      $layout.viewports[0].scaleModelPerPaperIn = 25.0
      $layout.viewports[1].scaleModelPerPaperIn = 100.0
    }
  }

  $json = $root | ConvertTo-Json -Depth 100
  Write-Utf8NoBom $OutFile $json
  Write-Host ("wrote {0} ({1} annotations stamped)" -f $OutFile, $i)
}
finally {
  Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
