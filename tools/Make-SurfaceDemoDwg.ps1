<#
.SYNOPSIS
  Regenerates samples/surface-demo.dwg — the TIN surface test fixture (REQ-066/067/068).

.DESCRIPTION
  The fixture is committed, but it is GENERATED, and this script is why. A committed .dwg is an
  opaque blob: nobody can tell from a diff whether a point moved, a group rule changed, or the
  terrain was reshaped. A deterministic generator beside it makes the fixture reviewable — the
  terrain is six lines of arithmetic you can read, and re-running always produces the same file.
  Same reasoning as util/benchscene.cpp, which generates the REQ-100 scene rather than committing
  a 100 MB artifact.

  The document is authored as JSON (the same shape a .gst template holds, REQ-175), then wrapped
  into a .dwg via GsJsonDwgFixture (issue #264 — .gs is no longer an openable document format;
  its JSON schema lives on only inside the .dwg trailer).

  Determinism matters and is deliberate: the jitter uses a fixed-seed integer LCG, never
  Get-Random and never the clock, so two runs of the same commit produce byte-identical output.

  The scene is built to exercise the things that are easy to get wrong:
    * raw description vs description  — points 1-5 have EDITED descriptions but keep raw code EG,
      so a group keyed on description finds 495 and one keyed on raw finds 500 (REQ-066).
    * union rule                      — "Ground + Curb" combines a raw-desc match with an id range;
      under intersection it would resolve to zero (REQ-067, decision log 2026-08-15).
    * real relief                     — a diagonal drainage swale and a low ridge, so the surface is
      worth contouring later rather than a tilted plane.

  Paper-space layouts are deliberately dropped from the template: they are ~527 KB of title-block
  geometry irrelevant to a surface fixture, and the loader explicitly tolerates their absence
  (model space, no layouts, no crash).

.EXAMPLE
  pwsh -File tools/Make-SurfaceDemoDwg.ps1
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
  [string]$OutFile
)

$ErrorActionPreference = 'Stop'
if (-not $OutFile) { $OutFile = Join-Path $RepoRoot 'samples\surface-demo.dwg' }

$converter = Join-Path $RepoRoot 'build\GsJsonDwgFixture.exe'
if (-not (Test-Path $converter)) { throw "GsJsonDwgFixture not built: $converter (build target GsJsonDwgFixture first)" }

$template = Join-Path $RepoRoot 'resources\default-template.gst'
if (-not (Test-Path $template)) { throw "Template not found: $template" }

$d = Get-Content $template -Raw | ConvertFrom-Json
$doc = $d.document

# --- Terrain -------------------------------------------------------------------------------------
# Falls to the south-west, with a diagonal swale (drainage) and a low ridge. Smooth on purpose:
# contours over it are long and readable rather than confetti.
function Get-Z([double]$x, [double]$y) {
  $base  = 118.0 + 0.020 * $x - 0.030 * $y
  $roll  = 6.0 * [math]::Sin($x / 90.0) + 4.0 * [math]::Cos($y / 70.0)
  $s     = $x - 0.8 * $y - 60.0                        # swale axis, running NE-SW
  $swale = -9.0 * [math]::Exp(-($s * $s) / (2.0 * 55.0 * 55.0))
  $r     = $x + 0.6 * $y - 520.0                       # ridge axis
  $ridge = 5.0 * [math]::Exp(-($r * $r) / (2.0 * 45.0 * 45.0))
  return [math]::Round($base + $roll + $swale + $ridge, 2)
}

# Fixed-seed LCG — reproducible jitter. Values stay inside their grid cell, so no two shots can
# land on the same plan position and the fixture never exercises de-duplication by accident.
$script:seed = 20260815
function Next01 {
  $script:seed = ($script:seed * 1664525 + 1013904223) -band 0x7FFFFFFF
  return (($script:seed -shr 8) -band 0xFFFF) / 65535.0
}

$pts = New-Object System.Collections.ArrayList

# Existing ground, ids 1-500, on a jittered 25 x 20 grid over 600 x 400 ft.
$id = 1
for ($i = 0; $i -lt 25; $i++) {
  for ($j = 0; $j -lt 20; $j++) {
    if ($id -gt 500) { break }
    $x = [math]::Round(($i + 0.15 + 0.7 * (Next01)) * 24.0, 3)
    $y = [math]::Round(($j + 0.15 + 0.7 * (Next01)) * 20.0, 3)
    # Points 1-5: the office edited the description; the field code stands (REQ-066).
    $desc = if ($id -le 5) { "Existing ground - checked $id" } else { 'EG' }
    [void]$pts.Add([pscustomobject]@{
      id = $id; easting = $x; northing = $y; elevation = (Get-Z $x $y)
      description = $desc; rawDescription = 'EG'; layer = '0'
      labelStyle = 0        # None: 500 labels would bury the surface
      labelMtextAnnId = 0
    })
    $id++
  }
}

# Top of curb, ids 501-540, along the east edge and half a foot proud of grade.
for ($k = 0; $k -lt 40; $k++) {
  $x = 560.0
  $y = [math]::Round($k * 10.0, 3)
  [void]$pts.Add([pscustomobject]@{
    id = $id; easting = $x; northing = $y; elevation = ((Get-Z $x $y) + 0.5)
    description = 'TC'; rawDescription = 'TC'; layer = '0'; labelStyle = 0; labelMtextAnnId = 0
  })
  $id++
}

# Control points, ids 901-906. The only labelled points, so a few numbers are visible on screen.
$id = 901
foreach ($cp in @(@(30, 30), @(570, 30), @(570, 380), @(30, 380), @(300, 200), @(150, 300))) {
  [void]$pts.Add([pscustomobject]@{
    id = $id; easting = [double]$cp[0]; northing = [double]$cp[1]
    elevation = (Get-Z $cp[0] $cp[1])
    description = 'CP'; rawDescription = 'CP'; layer = '0'
    labelStyle = 1        # NumberDesc
    labelMtextAnnId = 0
  })
  $id++
}

# --- Document ------------------------------------------------------------------------------------
$doc | Add-Member -NotePropertyName nextEntityId -NotePropertyValue 1 -Force
$doc.surveyPoints    = $pts.ToArray()
$doc.annotations     = @()   # labels are created on load from labelStyle
$doc.annotationAttrs = @()
# Title-block geometry from the template: ~527 KB, irrelevant here, and the loader is fine without it.
$doc.PSObject.Properties.Remove('paperLayouts')

$doc | Add-Member -NotePropertyName pointGroups -NotePropertyValue @(
  [pscustomobject]@{ name = 'Existing Ground'; idRanges = ''; descriptionMatch = ''; rawDescriptionMatch = 'EG'; explicitIds = @() }
  [pscustomobject]@{ name = 'Curb';            idRanges = ''; descriptionMatch = ''; rawDescriptionMatch = 'TC'; explicitIds = @() }
  [pscustomobject]@{ name = 'Control';         idRanges = ''; descriptionMatch = ''; rawDescriptionMatch = 'CP'; explicitIds = @() }
  # Two criteria: raw desc OR id range. Under intersection this resolves to ZERO, which is what
  # makes it a useful check of the union rule rather than a decoration.
  [pscustomobject]@{ name = 'Ground + Curb';   idRanges = '501-540'; descriptionMatch = ''; rawDescriptionMatch = 'EG'; explicitIds = @() }
  # Matches on description, so the five edited points drop out: 495, not 500.
  [pscustomobject]@{ name = 'Desc says EG (only 495)'; idRanges = ''; descriptionMatch = 'EG'; rawDescriptionMatch = ''; explicitIds = @() }
) -Force

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("surfacedemo-" + [guid]::NewGuid().ToString('N') + '.json')
try {
  $d | ConvertTo-Json -Depth 40 | Set-Content $work -Encoding UTF8
  & $converter to-dwg $work $OutFile
  if ($LASTEXITCODE -ne 0) { throw "GsJsonDwgFixture to-dwg failed (exit $LASTEXITCODE)" }
} finally {
  Remove-Item -Force $work -ErrorAction SilentlyContinue
}

$zs = $pts | ForEach-Object { $_.elevation }
Write-Host ("Wrote {0} ({1:N0} bytes)" -f $OutFile, (Get-Item $OutFile).Length)
Write-Host ("  {0} points, {1} groups, elevation {2:N2} to {3:N2}" -f `
  $pts.Count, $doc.pointGroups.Count,
  ($zs | Measure-Object -Minimum).Minimum, ($zs | Measure-Object -Maximum).Maximum)
