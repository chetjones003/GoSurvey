<#
.SYNOPSIS
  Converts an AutoCAD Plant 3D drawing into a .glb that GoSurvey can import (REQ-065).

.DESCRIPTION
  A Plant 3D DWG stores its piping as AcPp* CUSTOM OBJECTS. Those resolve only with Autodesk's
  Plant 3D object enabler, which is not licensable to an independent application — so GoSurvey
  cannot read that geometry from the DWG directly, and no work on its own DWG codec ever will.
  See ADR-026 Context.

  This script uses the AutoCAD you already have as the converter, which is the ADR-024 pattern
  (external converter first, native codec later) applied to 3D content:

    1. accoreconsole opens the drawing headlessly and EXPLODEs the Plant 3D objects, which asks the
       object enabler to emit plain AutoCAD geometry — 3D solids for the pipes and fittings.
    2. STLOUT tessellates those solids into triangles.
    3. The STL is wrapped as a .glb.

  Note what is lost: STL carries no colour and no object identity, so the model arrives as one
  grey part. Geometry, position and scale are exact. If you have Navisworks, exporting to glTF from
  there keeps per-object colour and names and is the better route — this script is the fallback for
  when you do not.

.EXAMPLE
  .\Convert-PlantDwgToGlb.ps1 -Dwg "C:\...\ENTERPRISE PIPING.dwg" -Out "$env:TEMP\piping.glb"
  Then in GoSurvey:  IMPORTMODEL "C:\Users\...\piping.glb" 0.0833333
  (0.0833333 = 1/12, because Plant 3D imperial models are in INCHES and a survey drawing is in feet.)
#>
param(
  [Parameter(Mandatory = $true)][string]$Dwg,
  [Parameter(Mandatory = $true)][string]$Out,
  [int]$FacetRes = 10,          # 1..10; AutoCAD's tessellation fineness. 10 = smoothest.
  [string]$AcadVersion = ""     # e.g. "AutoCAD 2027"; auto-detected when omitted.
)

$ErrorActionPreference = 'Stop'

# --- locate accoreconsole -------------------------------------------------------------------------
$acc = $null
if ($AcadVersion) {
  $acc = "C:\Program Files\Autodesk\$AcadVersion\accoreconsole.exe"
} else {
  $acc = Get-ChildItem "C:\Program Files\Autodesk" -Filter "accoreconsole.exe" -Recurse -Depth 2 -ErrorAction SilentlyContinue |
         Sort-Object FullName -Descending | Select-Object -First 1 -Expand FullName
}
if (-not $acc -or -not (Test-Path $acc)) { throw "accoreconsole.exe not found. Install AutoCAD, or pass -AcadVersion." }
if (-not (Test-Path $Dwg)) { throw "drawing not found: $Dwg" }
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw "node is required to write the .glb." }

$work = Join-Path $env:TEMP ("gs_plant_" + [guid]::NewGuid().ToString("N").Substring(0,8))
New-Item -ItemType Directory -Force $work | Out-Null
try {
  # AutoCAD keeps the file locked while it is open; work on a copy so this runs either way.
  $copy = Join-Path $work "model.dwg"
  $fs = New-Object System.IO.FileStream($Dwg, 'Open', 'Read', 'ReadWrite')
  $o = [System.IO.File]::Create($copy); $fs.CopyTo($o); $o.Close(); $fs.Close()

  $stl = Join-Path $work "model.stl"
  $lsp = Join-Path $work "conv.lsp"
  $scr = Join-Path $work "conv.scr"

  # EXPLODE is repeated: a Plant 3D object can yield a block reference whose contents are the solids,
  # so one pass does not reach them all.
  @"
(defun say (s) (princ (strcat "\n@@" s)) (princ))
(defun c:GSCONV ( / ss n)
  (setvar "QAFLAGS" 1)
  (setvar "FACETRES" $FacetRes)
  (setq n 0)
  (while (< n 4)
    (setq ss (ssget "_X" '((0 . "ACPP*,INSERT"))))
    (if ss (command "_.EXPLODE" ss ""))
    (setq n (1+ n)))
  (setq ss (ssget "_X" '((0 . "3DSOLID"))))
  (if ss
    (progn
      (say (strcat "SOLIDS " (itoa (sslength ss))))
      (command "_.STLOUT" ss "" "_Y" "$($stl -replace '\\','/')"))
    (say "SOLIDS 0"))
  (say "END")
  (princ))
"@ | Set-Content $lsp -Encoding ASCII

  @"
FILEDIA
0
SECURELOAD
0
(load "$($lsp -replace '\\','/')")
GSCONV
QUIT
Y
"@ | Set-Content $scr -Encoding ASCII

  Write-Host "Converting with $acc ..."
  $raw = & $acc /i $copy /s $scr 2>&1 | Out-String
  $clean = ($raw -replace "`0","") -replace '(?<=\S) (?=\S)',''
  ($clean -split "`r?`n" | Where-Object { $_ -match '@@' } | ForEach-Object { $_ -replace '.*@@','' }) |
    ForEach-Object { Write-Host "  $_" }

  if (-not (Test-Path $stl)) { throw "AutoCAD produced no STL — the drawing may contain no solid geometry." }

  # --- STL -> GLB ---------------------------------------------------------------------------------
  $js = Join-Path $work "stl2glb.js"
  @'
const fs=require("fs");
const [,,i,o]=process.argv;
const b=fs.readFileSync(i), tri=b.readUInt32LE(80);
if (84 + tri*50 > b.length) { console.error("STL is truncated"); process.exit(2); }
const P=new Float32Array(tri*9), N=new Float32Array(tri*9);
for(let t=0;t<tri;t++){
  const q=84+t*50;
  const nx=b.readFloatLE(q), ny=b.readFloatLE(q+4), nz=b.readFloatLE(q+8);
  for(let v=0;v<3;v++){
    const p=q+12+v*12;
    // AutoCAD is Z-up, glTF is Y-up. GoSurvey's importer converts glTF Y-up -> CAD Z-up, so the
    // inverse is applied here and the round trip lands upright.
    P[t*9+v*3+0]=b.readFloatLE(p);
    P[t*9+v*3+1]=b.readFloatLE(p+8);
    P[t*9+v*3+2]=-b.readFloatLE(p+4);
    N[t*9+v*3+0]=nx; N[t*9+v*3+1]=nz; N[t*9+v*3+2]=-ny;
  }
}
const pB=Buffer.from(P.buffer), nB=Buffer.from(N.buffer);
let bin=Buffer.concat([pB,nB]); while(bin.length%4) bin=Buffer.concat([bin,Buffer.from([0])]);
const doc={asset:{version:"2.0",generator:"GoSurvey Convert-PlantDwgToGlb"},
 buffers:[{byteLength:bin.length}],
 bufferViews:[{buffer:0,byteOffset:0,byteLength:pB.length},{buffer:0,byteOffset:pB.length,byteLength:nB.length}],
 accessors:[{bufferView:0,componentType:5126,count:tri*3,type:"VEC3"},{bufferView:1,componentType:5126,count:tri*3,type:"VEC3"}],
 materials:[{name:"model",pbrMetallicRoughness:{baseColorFactor:[0.62,0.66,0.70,1]}}],
 meshes:[{primitives:[{attributes:{POSITION:0,NORMAL:1},material:0,mode:4}]}],
 nodes:[{name:"model",mesh:0}],scenes:[{nodes:[0]}],scene:0};
let j=Buffer.from(JSON.stringify(doc),"utf8"); while(j.length%4) j=Buffer.concat([j,Buffer.from(" ")]);
const total=12+8+j.length+8+bin.length, out=Buffer.alloc(total); let k=0;
out.writeUInt32LE(0x46546C67,k);k+=4;out.writeUInt32LE(2,k);k+=4;out.writeUInt32LE(total,k);k+=4;
out.writeUInt32LE(j.length,k);k+=4;out.writeUInt32LE(0x4E4F534A,k);k+=4;j.copy(out,k);k+=j.length;
out.writeUInt32LE(bin.length,k);k+=4;out.writeUInt32LE(0x004E4942,k);k+=4;bin.copy(out,k);
fs.writeFileSync(o,out);
console.log("  triangles " + tri);
'@ | Set-Content $js -Encoding ASCII

  node $js $stl $Out
  Write-Host ""
  Write-Host "Wrote $Out"
  Write-Host "In GoSurvey:  IMPORTMODEL `"$Out`" 0.0833333    (1/12: inches -> feet)"
  Write-Host "Then:         VS SHADED"
} finally {
  Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
