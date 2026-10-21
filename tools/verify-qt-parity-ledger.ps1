<#
.SYNOPSIS
  Checks that the Qt parity ledger and sign-off packet still match the tests and tracker.

.DESCRIPTION
  Issue #919. Fails if a release feature is marked Missing, a Verified feature has no
  existing focused Qt test, a backticked test reference is stale, or the packet omits a
  tracker work item from #879 through #919.
#>
param(
  [string]$Ledger = "",
  [string]$Packet = "",
  [string]$TestsDirectory = ""
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Ledger) { $Ledger = Join-Path $repoRoot 'docs/qt-074-parity-ledger.md' }
if (-not $Packet) { $Packet = Join-Path $repoRoot 'docs/qt-074-signoff-packet.md' }
if (-not $TestsDirectory) { $TestsDirectory = Join-Path $repoRoot 'tests' }

$failures = New-Object System.Collections.Generic.List[string]
function Fail([string]$message) { $failures.Add($message) }

$ledgerText = [IO.File]::ReadAllText($Ledger)
$packetText = [IO.File]::ReadAllText($Packet)
$testFiles = @(Get-ChildItem -LiteralPath $TestsDirectory -Filter '*.cpp' -Recurse)
$testText = ($testFiles | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
$cmakeText = [IO.File]::ReadAllText((Join-Path $repoRoot 'CMakeLists.txt'))
$cmakeText += "`n" + [IO.File]::ReadAllText((Join-Path $repoRoot 'cmake/GoSurveyQtTests.cmake'))

$testNamePattern = '`([a-z][A-Za-z0-9]*[A-Z][A-Za-z0-9]*)`'
function CitedTestNames([string]$text) {
  [regex]::Matches($text, $testNamePattern) | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
}
function TestSlotExists([string]$name) { $testText -match ('void\s+' + [regex]::Escape($name) + '\s*\(') }
function AnyTestExists([string]$name) {
  if (TestSlotExists $name) { return $true }
  return $cmakeText -match ('add_test\(NAME\s+' + [regex]::Escape($name) + '\b')
}
function CitedCtestExists([string]$text) {
  foreach ($match in [regex]::Matches($text, '`([a-z][a-z0-9_-]*\.[a-z][a-z0-9_-]*)`')) {
    if ($cmakeText -match ('add_test\(NAME\s+' + [regex]::Escape($match.Groups[1].Value) + '\b')) { return $true }
  }
  return $false
}

# The feature table may contain Implemented (known coverage gap), but never a Missing workflow.
$inTable = $false
foreach ($line in ($ledgerText -split "`r?`n")) {
  if ($line -match '^## Release feature workflows') { $inTable = $true; continue }
  if ($inTable -and $line -match '^## ') { break }
  if (-not $inTable -or $line -notmatch '^\|' -or $line -match '^\|\s*-' -or $line -match '^\|\s*0\.7\.4 feature') { continue }
  $cells = $line.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim() }
  if ($cells.Count -lt 4) { Fail "Ledger row has fewer than four columns: $($line.Substring(0, [Math]::Min(80, $line.Length)))"; continue }
  $label = $cells[0].Substring(0, [Math]::Min(70, $cells[0].Length))
  $status = $cells[2]
  if ($status -notmatch '^\*\*(Verified|Implemented)') { Fail "Ledger row '$label' has status '$status' (must start with Verified or Implemented)." }
  if ($status -match '^\*\*Verified') {
    $names = @(CitedTestNames $cells[3] | Where-Object { AnyTestExists $_ })
    $hasCatchTag = $cells[3] -match '`\[[A-Za-z0-9_-]+\]`' -and $cells[3] -match '\[issue[0-9]+\]'
    if ($names.Count -eq 0 -and -not $hasCatchTag -and -not (CitedCtestExists $cells[3])) { Fail "Ledger row '$label' is Verified but names no existing focused test or CTest check." }
  }
}

# Any backticked lower-camel identifier presented as a test must remain present in the tree.
foreach ($source in @(@{ Name = 'ledger'; Text = $ledgerText }, @{ Name = 'packet'; Text = $packetText })) {
  foreach ($name in (CitedTestNames $source.Text)) {
    if (TestSlotExists $name) { continue }
    $inSource = Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -Recurse -Include '*.cpp', '*.hpp' |
      Select-String -Pattern ('\b' + [regex]::Escape($name) + '\b') -Quiet
    if (-not $inSource -and ($testText -notmatch ('\b' + [regex]::Escape($name) + '\b'))) {
      Fail "The $($source.Name) cites '$name', which is not a test or source identifier."
    }
  }
}

# Every tracker item is represented by its own packet table row, even when its issue is still open.
foreach ($issue in 879..919) {
  if ($packetText -notmatch ("(?m)^\|\s*#?$issue\b")) { Fail "The packet's work-item table has no row for #$issue." }
}

if ($failures.Count -gt 0) {
  $failures | ForEach-Object { Write-Host "FAIL: $_" }
  Write-Host "$($failures.Count) problem(s) in the parity ledger / sign-off packet."
  exit 1
}
Write-Host "Parity ledger and sign-off packet are consistent with the tests and tracker."
exit 0
