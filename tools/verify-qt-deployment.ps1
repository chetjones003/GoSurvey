<#
.SYNOPSIS
  Verifies that GoSurvey.exe deploys and launches without ImGui, GLFW, or any file from the rest of the build.

.DESCRIPTION
  Issue #918. Four checks, each a hard failure (exit code 1):
    1. The Qt runtime set is all there: GoSurvey.exe, the Qt DLLs, PDFium, qt.conf, the Qt platform
       plugin and the resources directory.
    2. No ImGui or GLFW runtime file sits in the deployed set.
    3. GoSurvey.exe contains no ImGui or GLFW library code (a byte scan for strings only those
       libraries carry; the "glfw_init" style stage names of the startup-failure report are wire
       names from REQ-425 and are not a library).
    4. A copy of ONLY the deployed set - no GoSurvey.exe, no test executables, no ImGui files - and a PATH
       reduced to the Windows directories still starts: `GoSurvey.exe --smoke-test` exits 0 on a
       private invisible desktop. A DLL the copy lacks makes the process fail to load, so this proves
       the deployed set is complete.

  The smoke screenshot is kept under -WorkDirectory (build/artifacts by default).

.EXAMPLE
  powershell -File tools/verify-qt-deployment.ps1 -RuntimeDirectory build
#>
param(
  [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
  [string]$WorkDirectory = "",
  [int]$TimeoutSec = 120
)

$ErrorActionPreference = 'Stop'
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
if (-not $WorkDirectory) { $WorkDirectory = Join-Path $runtime 'artifacts' }
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null
$work = (Resolve-Path -LiteralPath $WorkDirectory).Path

function Fail([string]$message) {
  Write-Host "FAIL: $message"
  exit 1
}

# 1. The Qt runtime set.
$required = @(
  'GoSurvey.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll',
  'pdfium.dll', 'qt.conf', 'qt-plugins\platforms\qwindows.dll', 'resources\default-template.gst'
)
foreach ($name in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $runtime $name))) { Fail "missing from the deployed set: $name" }
}

# The deployed set: the Qt application and what it loads. Other applications and test-only files are not
# part of it, so the copy in step 4 cannot borrow anything from them.
$deployedFiles = @(Get-ChildItem -LiteralPath $runtime -File | Where-Object {
    $_.Extension -eq '.dll' -and $_.Name -ne 'Qt6Test.dll' -or $_.Name -in @('GoSurvey.exe', 'qt.conf')
  })

# 2. No ImGui or GLFW runtime file.
$forbidden = @($deployedFiles | Where-Object { $_.Name -match '(?i)imgui|glfw' })
if ($forbidden.Count -gt 0) { Fail "ImGui/GLFW runtime file in the deployed set: $($forbidden.Name -join ', ')" }

# 3. No ImGui or GLFW library code in the executable. Bytes are read as Latin-1 so each byte is one
# character and the search is a plain substring test.
$bytes = [IO.File]::ReadAllBytes((Join-Path $runtime 'GoSurvey.exe'))
$text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
$markers = @('Dear ImGui', 'imgui.ini', 'ImGui::', 'The GLFW library is not initialized', 'GLFW_PLATFORM_ERROR')
foreach ($marker in $markers) {
  if ($text.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
    Fail "GoSurvey.exe contains ImGui/GLFW library text: '$marker'"
  }
}

# 4. Launch a copy of only the deployed set with a minimal PATH.
$stage = Join-Path $work "qt-deployment-$PID"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
  foreach ($file in $deployedFiles) { Copy-Item -LiteralPath $file.FullName -Destination $stage }
  foreach ($directory in 'qt-plugins', 'resources') {
    Copy-Item -LiteralPath (Join-Path $runtime $directory) -Destination $stage -Recurse
  }

  $savedPath = $env:PATH
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  try {
    & (Join-Path $PSScriptRoot 'run-qt-tests-hidden.ps1') -Exe (Join-Path $stage 'GoSurvey.exe') `
      -TimeoutSec $TimeoutSec -WorkingDirectory $stage -PlainArguments -TestArgs '--smoke-test'
    $code = $LASTEXITCODE
  } finally {
    $env:PATH = $savedPath
  }
  if ($code -ne 0) { Fail "GoSurvey.exe --smoke-test from the clean copy exited with code $code" }
  $shot = Join-Path $stage 'qt-editor-smoke.png'
  if (-not (Test-Path -LiteralPath $shot) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
    Fail 'the smoke run produced no screenshot'
  }
  Copy-Item -LiteralPath $shot -Destination (Join-Path $work 'qt-deployment-smoke.png') -Force
} finally {
  if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}

Write-Host "PASS: GoSurvey.exe deploys without ImGui/GLFW and launches from a clean copy ($($deployedFiles.Count) files + qt-plugins + resources)."
exit 0
