param(
  [Parameter(Mandatory)]
  [ValidateLength(1, 19)]
  [ValidatePattern('^[1-9][0-9]{0,18}$')]
  [string] $ReportId,
  [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:CrashTriageBucket = 'gosurvey-crash-dumps'
$script:CrashTriageConfig = Join-Path $PSScriptRoot 'crash-triage.wrangler.toml'

function Invoke-Wrangler {
  param([Parameter(Mandatory)] [string[]] $Arguments)

  $wrangler = Get-Command wrangler -ErrorAction SilentlyContinue
  if ($wrangler) {
    $command = $wrangler.Source
    $prefix = @()
  } else {
    $npx = Get-Command npx -ErrorAction Stop
    $command = $npx.Source
    $prefix = @('--yes', 'wrangler@4.152.0')
  }

  $output = @(& $command @prefix @Arguments 2>&1)
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    $detail = ($output | ForEach-Object { $_.ToString() } | Select-Object -Last 3) -join ' '
    throw "Wrangler read failed (exit $exitCode): $detail"
  }
  ($output | ForEach-Object { $_.ToString() }) -join "`n"
}

function ConvertFrom-D1Json {
  param([Parameter(Mandatory)] [string] $JsonText)

  $response = ConvertFrom-Json -InputObject $JsonText
  $rows = @($response | ForEach-Object { $_.results })
  if ($rows.Count -gt 1) { throw 'The report-ID query returned more than one row.' }
  if ($rows.Count -eq 0) { return $null }
  $rows[0]
}

function Get-CrashPdbObjectKey {
  param(
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Commit,
    [Parameter(Mandatory)] [string] $Identity
  )

  if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') {
    throw 'The report contains an invalid version for symbol lookup.'
  }
  if ($Commit -notmatch '^[0-9a-fA-F]{12}$') {
    throw 'The report does not contain a 12-character build commit.'
  }
  if ($Identity -notmatch '^[0-9A-Fa-f]{32},\d+$') {
    throw 'The report does not contain a valid PDB GUID/age.'
  }
  "symbols/$Version/$($Commit.ToLowerInvariant())/$($Identity.ToUpperInvariant()).pdb"
}

function Get-CrashDumpObjectKey {
  param([Parameter(Mandatory)] [string] $Key)
  if ($Key -notmatch '^crash-dumps/[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\.dmp$') {
    throw 'The report contains an invalid crash-dump object key.'
  }
  $Key
}

function Get-CrashReportSelectSql {
  param(
    [Parameter(Mandatory)]
    [ValidateLength(1, 19)]
    [ValidatePattern('^[1-9][0-9]{0,18}$')]
    [string] $Id
  )
  "SELECT id, ts, version, channel, os, reason, report, dump_key FROM crash_reports WHERE id = $Id"
}

function Get-CrashReportIdentity {
  param([Parameter(Mandatory)] [string] $Report)

  $values = @{}
  foreach ($field in @('version', 'commit', 'pdb')) {
    $match = [regex]::Match($Report, "(?m)^${field}:\s*(?<value>[^\r\n]+)\s*$")
    if ($match.Success) { $values[$field] = $match.Groups['value'].Value.Trim() }
  }
  $values
}

function Get-PdbIdentityFromFile {
  param([Parameter(Mandatory)] [string] $PdbPath)

  $pdbutil = Get-Command llvm-pdbutil.exe -ErrorAction Stop
  $summary = @(& $pdbutil.Source dump -summary $PdbPath 2>&1)
  if ($LASTEXITCODE -ne 0) { throw 'llvm-pdbutil could not read the downloaded PDB.' }
  $lines = @($summary | ForEach-Object { $_.ToString() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  ConvertFrom-PdbSummaryOutput -Lines $lines
}

function ConvertFrom-PdbSummaryOutput {
  param([Parameter(Mandatory)] [string[]] $Lines)

  $guids = @($Lines | ForEach-Object { [regex]::Match($_, '^\s*GUID:\s*\{(?<guid>[0-9A-Fa-f-]{36})\}\s*$') } | Where-Object Success)
  $ages = @($Lines | ForEach-Object { [regex]::Match($_, '^\s*Age:\s*(?<age>\d+)\s*$') } | Where-Object Success)
  if ($guids.Count -ne 1 -or $ages.Count -ne 1) { throw 'The downloaded PDB has no unambiguous GUID/age.' }
  $guid = [Guid]::Parse($guids[0].Groups['guid'].Value).ToString('N').ToUpperInvariant()
  "$guid,$($ages[0].Groups['age'].Value)"
}

function Invoke-CrashReportTriage {
  param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9]+$')]
    [string] $Id,
    [string] $DestinationRoot
  )

  if (-not $DestinationRoot) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $DestinationRoot = Join-Path $repoRoot 'build\artifacts\crash-triage'
  }
  $destination = Join-Path $DestinationRoot $Id
  $sql = Get-CrashReportSelectSql -Id $Id
  $rawJson = Invoke-Wrangler -Arguments @(
    'd1', 'execute', 'gosurvey-telemetry', '--remote', '--json', '--config', $script:CrashTriageConfig,
    '--command', $sql
  )
  $row = ConvertFrom-D1Json -JsonText $rawJson
  if (-not $row) { throw "Crash report $Id was not found." }
  if ([string]$row.id -ne $Id -or [string]::IsNullOrWhiteSpace([string]$row.report)) {
    throw 'The report query returned an unexpected row.'
  }

  New-Item -ItemType Directory -Path $destination -Force | Out-Null
  foreach ($staleFile in @('crash.dmp', 'GoSurvey.pdb', 'GoSurvey.pdb.partial')) {
    Remove-Item -LiteralPath (Join-Path $destination $staleFile) -Force -ErrorAction SilentlyContinue
  }
  $reportPath = Join-Path $destination 'report.txt'
  [System.IO.File]::WriteAllText($reportPath, [string]$row.report, [System.Text.UTF8Encoding]::new($false))

  $metadata = [ordered]@{
    reportId = [long]$row.id
    timestamp = [string]$row.ts
    version = [string]$row.version
    channel = [string]$row.channel
    os = [string]$row.os
    reason = [string]$row.reason
    report = 'report.txt'
    minidump = $null
    minidumpStatus = 'not present on this report'
    pdb = $null
    pdbStatus = 'not available: report has no verified build identity'
  }

  if ($row.dump_key) {
    $dumpKey = Get-CrashDumpObjectKey -Key ([string]$row.dump_key)
    $dumpPath = Join-Path $destination 'crash.dmp'
    try {
      Invoke-Wrangler -Arguments @(
        'r2', 'object', 'get', "$script:CrashTriageBucket/$dumpKey", '--remote', '--config', $script:CrashTriageConfig,
        '--file', $dumpPath
      ) | Out-Null
      if (-not (Test-Path -LiteralPath $dumpPath -PathType Leaf)) { throw 'Wrangler did not create the dump file.' }
      $dumpLength = (Get-Item -LiteralPath $dumpPath).Length
      if ($dumpLength -le 0 -or $dumpLength -gt 10MB) { throw 'The downloaded minidump is empty or exceeds the 10 MB upload limit.' }
      $metadata.minidump = 'crash.dmp'
      $metadata.minidumpStatus = 'downloaded'
    } catch {
      Remove-Item -LiteralPath $dumpPath -Force -ErrorAction SilentlyContinue
      $metadata.minidumpStatus = "unavailable: $($_.Exception.Message)"
    }
  }

  $identity = Get-CrashReportIdentity -Report ([string]$row.report)
  if ($identity.ContainsKey('commit')) { $metadata.buildCommit = $identity.commit }
  if ($identity.ContainsKey('pdb')) { $metadata.pdbIdentity = $identity.pdb }
  if ($identity.ContainsKey('version') -and $identity.version -ceq [string]$row.version -and
      $identity.ContainsKey('commit') -and $identity.ContainsKey('pdb') -and $identity.pdb -cne 'unavailable') {
    $pdbKey = Get-CrashPdbObjectKey -Version $identity.version -Commit $identity.commit -Identity $identity.pdb
    $partialPdbPath = Join-Path $destination 'GoSurvey.pdb.partial'
    $pdbPath = Join-Path $destination 'GoSurvey.pdb'
    try {
      Invoke-Wrangler -Arguments @(
        'r2', 'object', 'get', "$script:CrashTriageBucket/$pdbKey", '--remote', '--config', $script:CrashTriageConfig,
        '--file', $partialPdbPath
      ) | Out-Null
      if (-not (Test-Path -LiteralPath $partialPdbPath -PathType Leaf)) { throw 'Wrangler did not create the PDB file.' }
      $downloadedIdentity = Get-PdbIdentityFromFile -PdbPath $partialPdbPath
      if ($downloadedIdentity -cne $identity.pdb.ToUpperInvariant()) { throw 'The downloaded PDB identity does not match the crash report.' }
      Move-Item -LiteralPath $partialPdbPath -Destination $pdbPath -Force | Out-Null
      $metadata.pdb = 'GoSurvey.pdb'
      $metadata.pdbStatus = 'downloaded and verified against report GUID/age'
    } catch {
      Remove-Item -LiteralPath $partialPdbPath -Force -ErrorAction SilentlyContinue
      Remove-Item -LiteralPath $pdbPath -Force -ErrorAction SilentlyContinue
      $metadata.pdbStatus = "unavailable: $($_.Exception.Message)"
    }
  }

  $manifestPath = Join-Path $destination 'manifest.json'
  $manifest = ConvertTo-Json -InputObject $metadata -Depth 5
  [System.IO.File]::WriteAllText($manifestPath, $manifest + "`n", [System.Text.UTF8Encoding]::new($false))

  [pscustomobject]@{
    ReportId = $Id
    Directory = $destination
    Report = $reportPath
    MinidumpStatus = $metadata.minidumpStatus
    PdbStatus = $metadata.pdbStatus
    Manifest = $manifestPath
  }
}

if ($MyInvocation.InvocationName -ne '.') {
  Invoke-CrashReportTriage -Id $ReportId -DestinationRoot $OutputRoot
}
