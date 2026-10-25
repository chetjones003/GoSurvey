$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'triage-crash-report.ps1') -ReportId '1'

$identity = 'B9EAF39516DC48E4AE8D7BE49BB99584,9'
$key = Get-CrashPdbObjectKey -Version '0.7.4' -Commit '0123456789ab' -Identity $identity
if ($key -cne "symbols/0.7.4/0123456789ab/$identity.pdb") { throw "Unexpected PDB key: $key" }

$dumpKey = Get-CrashDumpObjectKey -Key 'crash-dumps/b9eaf395-16dc-48e4-ae8d-7be49bb99584.dmp'
if ($dumpKey -notmatch '^crash-dumps/') { throw 'A valid dump key was not preserved.' }

$summaryIdentity = ConvertFrom-PdbSummaryOutput -Lines @(
  '  Age: 9',
  '  GUID: {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}'
)
if ($summaryIdentity -cne $identity) { throw "Unexpected parsed PDB identity: $summaryIdentity" }

$reportInfo = Get-CrashReportIdentity -Report "version: 0.7.4`ncommit: 0123456789ab`npdb: $identity`n"
if ($reportInfo.version -cne '0.7.4' -or $reportInfo.commit -cne '0123456789ab' -or $reportInfo.pdb -cne $identity) {
  throw 'Crash report build identity did not parse as expected.'
}

$json = '[{"results":[{"id":42,"report":"diagnostic text","dump_key":null}]}]'
$row = ConvertFrom-D1Json -JsonText $json
if ($row.id -ne 42 -or $row.report -cne 'diagnostic text') { throw 'D1 JSON row did not parse.' }
if ($null -ne (ConvertFrom-D1Json -JsonText '[{"results":[]}]')) { throw 'An empty D1 result must be treated as missing.' }

$sql = Get-CrashReportSelectSql -Id '42'
if ($sql -notmatch '^SELECT ' -or $sql -notmatch 'WHERE id = 42$' -or $sql -match 'install_id|INSERT|UPDATE|DELETE') {
  throw "The report query is not a narrowly scoped read: $sql"
}

foreach ($invalid in @('0', '../42', '42; DELETE FROM crash_reports')) {
  $rejected = $false
  try { Get-CrashReportSelectSql -Id $invalid | Out-Null } catch { $rejected = $true }
  if (-not $rejected) { throw "Invalid report ID was accepted: $invalid" }
}

foreach ($invalid in @('crash-dumps/../../private.dmp', 'symbols/0.7.4/file.pdb')) {
  $rejected = $false
  try { Get-CrashDumpObjectKey -Key $invalid | Out-Null } catch { $rejected = $true }
  if (-not $rejected) { throw "Invalid dump object key was accepted: $invalid" }
}

$rejected = $false
try { Get-CrashPdbObjectKey -Version '0.7.4' -Commit 'short' -Identity $identity | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'A malformed commit was accepted for symbol lookup.' }

$rejected = $false
try { Get-CrashPdbObjectKey -Version '0.7.4' -Commit '0123456789ab' -Identity 'unavailable' | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'A malformed PDB identity was accepted for symbol lookup.' }

$rejected = $false
try { ConvertFrom-PdbSummaryOutput -Lines @('GUID: {00000000-0000-0000-0000-000000000000}') | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'A PDB summary without an age was accepted.' }

$script:WranglerCalls = [System.Collections.ArrayList]::new()
$script:FixtureId = 42
$script:FixtureReport = "GoSurvey stopped unexpectedly.`nversion: 0.7.4`nchannel: beta`ncommit: 0123456789ab`npdb: $identity`n"
$script:FixtureDumpKey = 'crash-dumps/b9eaf395-16dc-48e4-ae8d-7be49bb99584.dmp'
$script:MockPdbIdentity = $identity
function Invoke-Wrangler {
  param([string[]] $Arguments)
  [void]$script:WranglerCalls.Add(@{ Arguments = @($Arguments) })
  if ($Arguments[0] -eq 'd1') {
    $fixtureRow = @{
      id = $script:FixtureId; ts = '2026-10-25T00:00:00Z'; version = '0.7.4'; channel = 'beta'
      os = 'windows'; reason = 'test'; report = $script:FixtureReport; dump_key = $script:FixtureDumpKey
    }
    return ConvertTo-Json -InputObject @(@{ results = @($fixtureRow) }) -Depth 6 -Compress
  }
  $fileIndex = [Array]::IndexOf($Arguments, '--file')
  [System.IO.File]::WriteAllBytes($Arguments[$fileIndex + 1], [byte[]](0x4d, 0x44, 0x4d, 0x50))
  'mock download'
}
function Get-PdbIdentityFromFile { param([string] $PdbPath) $script:MockPdbIdentity }

$artifactRoot = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) "build\artifacts\crash-triage-test-$PID"))
$artifactParent = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'build\artifacts'))
if (-not $artifactRoot.StartsWith($artifactParent + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'The test artifact path escaped build/artifacts.'
}
if (Test-Path -LiteralPath $artifactRoot) { throw 'The unique test artifact path already exists.' }
try {
  $script:WranglerCalls.Clear()
  $triage = Invoke-CrashReportTriage -Id '42' -DestinationRoot $artifactRoot
  if ($script:WranglerCalls.Count -ne 3) { throw "Expected one D1 read and two R2 reads, got $($script:WranglerCalls.Count)." }
  $expectedSql = 'SELECT id, ts, version, channel, os, reason, report, dump_key FROM crash_reports WHERE id = 42'
  if ($script:WranglerCalls[0].Arguments[-1] -cne $expectedSql) { throw 'The D1 request was not a narrowly scoped SELECT for this report.' }
  if ($script:WranglerCalls[1].Arguments[3] -cne "gosurvey-crash-dumps/$script:FixtureDumpKey") { throw 'The minidump request did not use the report row object key.' }
  if ($script:WranglerCalls[2].Arguments[3] -cne "gosurvey-crash-dumps/symbols/0.7.4/0123456789ab/$identity.pdb") { throw 'The PDB request did not use the report build identity.' }
  if (($script:WranglerCalls | ForEach-Object { $_.Arguments -join ' ' }) -match '(?i)\b(put|delete|insert|update)\b') {
    throw 'The triage tool attempted a remote write.'
  }
  if ((Get-Content -LiteralPath (Join-Path $triage.Directory 'report.txt') -Raw) -cne $script:FixtureReport) { throw 'The report text was not saved exactly.' }
  if (-not (Test-Path -LiteralPath (Join-Path $triage.Directory 'crash.dmp'))) { throw 'The minidump was not saved.' }
  if (-not (Test-Path -LiteralPath (Join-Path $triage.Directory 'GoSurvey.pdb'))) { throw 'The matching PDB was not saved.' }
  if ($triage.PdbStatus -notmatch 'verified against report') { throw 'The matching PDB was not reported as verified.' }

  $script:WranglerCalls.Clear()
  $script:FixtureId = 43
  $script:FixtureDumpKey = $null
  $script:FixtureReport = "GoSurvey stopped unexpectedly.`nversion: 0.7.4`nchannel: beta`n"
  $reportOnlyRoot = Join-Path $artifactRoot 'report-only'
  $reportOnly = Invoke-CrashReportTriage -Id '43' -DestinationRoot $reportOnlyRoot
  if ($script:WranglerCalls.Count -ne 1) { throw 'A report without artifacts caused unnecessary R2 reads.' }
  if (-not (Test-Path -LiteralPath $reportOnly.Report)) { throw 'The report-only case lost its text report.' }
  if ($reportOnly.MinidumpStatus -notmatch 'not present') { throw 'A missing dump was not reported.' }
  if (Test-Path -LiteralPath (Join-Path $reportOnly.Directory 'GoSurvey.pdb')) { throw 'A PDB was created without report identity.' }

  $script:WranglerCalls.Clear()
  $script:FixtureId = 44
  $script:FixtureDumpKey = 'crash-dumps/b9eaf395-16dc-48e4-ae8d-7be49bb99584.dmp'
  $script:FixtureReport = "version: 0.7.4`ncommit: 0123456789ab`npdb: $identity`n"
  $script:MockPdbIdentity = '00000000000000000000000000000000,1'
  $mismatchRoot = Join-Path $artifactRoot 'pdb-mismatch'
  $mismatch = Invoke-CrashReportTriage -Id '44' -DestinationRoot $mismatchRoot
  if ($mismatch.PdbStatus -notmatch 'does not match') { throw 'A mismatched PDB identity was not reported.' }
  if (Test-Path -LiteralPath (Join-Path $mismatch.Directory 'GoSurvey.pdb')) { throw 'A mismatched PDB was left available to the agent.' }
} finally {
  $resolvedArtifacts = [System.IO.Path]::GetFullPath($artifactRoot)
  if (-not $resolvedArtifacts.StartsWith($artifactParent + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to remove a test artifact path outside build/artifacts.'
  }
  Remove-Item -LiteralPath $artifactRoot -Recurse -Force
}

Write-Host 'Crash report triage helper tests passed.'
