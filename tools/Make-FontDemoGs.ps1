<#
.SYNOPSIS
  Builds samples/font-demo.gs — the visual fixture for issue #115's font/text audit.

.DESCRIPTION
  Issue #115's Testing section asks for a drawing carrying as many text types as possible, so the same
  font can be compared across object types and between Model Space, Paper Space, and Paper Space
  viewports at different scales.

  The geometry is built by the headless driver from
  tests/headless/transcripts/issue115-font-matrix.txt, so the sample is reproducible rather than
  hand-authored. This script then stamps a font family onto each annotation, because the font of a
  dimension comes from DimensionStyle::textFont (set in the DIMSTY dialog) and the font of TEXT/MTEXT
  from the active text style (set in the STYLE manager) — both UI-only, so a transcript cannot choose
  them. Same pattern as tools/Make-SurfaceDemoGs.ps1.

  Fonts are assigned round-robin across Arial / Times New Roman / romans.shx so one drawing shows a
  TrueType family, a second TrueType family, and an SHX stroke font side by side on every object type.

.EXAMPLE
  pwsh tools/Make-FontDemoGs.ps1
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = '',
  [string]$OutFile  = ''
)

$ErrorActionPreference = 'Stop'

# Resolved in the body rather than as a parameter default: $PSScriptRoot is not reliably bound at
# parameter-binding time across PowerShell hosts, and an empty default there fails inside Split-Path
# with an error that says nothing about which script it came from.
if (-not $RepoRoot) {
  $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
  $RepoRoot = Split-Path -Parent $here
}

$headless = Join-Path $RepoRoot 'build/gosurvey_headless.exe'
if (-not (Test-Path $headless)) { throw "headless driver not built: $headless" }

$transcript = Join-Path $RepoRoot 'tests/headless/transcripts/issue115-font-matrix.txt'
if (-not (Test-Path $transcript)) { throw "transcript missing: $transcript" }

if (-not $OutFile) { $OutFile = Join-Path $RepoRoot 'samples/font-demo.gs' }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("fontdemo-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
  & $headless run $transcript --out $work | Write-Verbose
  if ($LASTEXITCODE -ne 0) { throw "transcript failed (exit $LASTEXITCODE)" }

  $built = Join-Path $work 'font-matrix-full.gs'
  if (-not (Test-Path $built)) { throw "transcript produced no font-matrix-full.gs" }

  # An MTEXT has to be injected rather than drawn: MTEXT's content comes from the on-screen editor
  # ("type in the on-screen editor ... Save to place"), which a transcript cannot reach. It matters
  # enough to inject, because plain MTEXT is the one entity REQ-050's viewport sizing governs — the
  # sample would not exercise that rule without one. It sits inside the model extents so both
  # viewports show it. annotationAttrs must stay the same length as annotations or the loader
  # refuses the file (".gs: annotationAttrs count must match annotations.").
  $mtext = @(
    '      {',
    '        "kind": "mtext",',
    '        "insX": 80.0,',
    '        "insY": 80.0,',
    '        "plottedHeightInches": 0.25,',
    '        "rotationRad": 0.0,',
    '        "text": "MODEL MTEXT",',
    '        "boxMinX": 80.0,',
    '        "boxMinY": 65.0,',
    '        "boxMaxX": 120.0,',
    '        "boxMaxY": 80.0,',
    '        "mtextAttach": 1,',
    '        "surveyPointLabelForId": -1',
    '      },'
  )

  # Round-robin the three families over every annotation, model and paper alike, so each object type
  # appears in each font somewhere in the drawing.
  $fonts = @('Arial', 'Times New Roman', 'romans.shx')
  $i = 0
  $out = New-Object System.Collections.Generic.List[string]
  foreach ($line in Get-Content -LiteralPath $built) {
    if ($line -match '^\s*"annotations":\s*\[\s*$') {
      $out.Add($line)
      foreach ($m in $mtext) { $out.Add($m) }
      continue
    }
    if ($line -match '^\s*"annotationAttrs":\s*\[\s*$') {
      $out.Add($line)
      $out.Add('      { "layer": "0" },')
      continue
    }
    # Every annotation record carries a "kind" key; insert the family just before it, and let the
    # loader's tolerant-key read pick it up (an absent fontFamily means "app default").
    if ($line -match '^\s*"kind":\s*"(dimlinear|dim|dimangular|text|mtext)"') {
      $indent = ($line -replace '^(\s*).*$', '$1')
      $out.Add(('{0}"fontFamily": "{1}",' -f $indent, $fonts[$i % $fonts.Count]))
      $i++
    }
    $out.Add($line)
  }
  Set-Content -LiteralPath $OutFile -Value $out -Encoding utf8
  Write-Host ("wrote {0} ({1} annotations stamped)" -f $OutFile, $i)
}
finally {
  Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
