param(
  [string] $ExecutablePath,
  [string] $PdbPath,
  [string] $Version,
  [string] $Commit,
  [string] $Bucket = 'gosurvey-crash-dumps',
  [switch] $DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertFrom-CodeViewOutput {
  param([Parameter(Mandatory)] [string[]] $Lines)

  $codeViewMatches = @($Lines | ForEach-Object {
    [regex]::Match($_, 'Format:\s*RSDS,\s*\{(?<guid>[0-9A-Fa-f-]{36})\},\s*(?<age>\d+)')
  } | Where-Object Success)
  if ($codeViewMatches.Count -ne 1) {
    throw "Expected exactly one RSDS CodeView identity in executable output; found $($codeViewMatches.Count)."
  }

  $guid = [Guid]::Empty
  if (-not [Guid]::TryParse($codeViewMatches[0].Groups['guid'].Value, [ref]$guid)) {
    throw 'The executable contains an invalid RSDS GUID.'
  }
  [pscustomobject]@{
    Guid = $guid.ToString('N').ToUpperInvariant()
    Age  = [int]::Parse($codeViewMatches[0].Groups['age'].Value, [Globalization.CultureInfo]::InvariantCulture)
  }
}

function ConvertFrom-PdbSummaryOutput {
  param([Parameter(Mandatory)] [string[]] $Lines)

  $guidLines = @($Lines | Where-Object { $_ -match '^\s*GUID:\s*\{(?<guid>[0-9A-Fa-f-]{36})\}\s*$' })
  $ageLines = @($Lines | Where-Object { $_ -match '^\s*Age:\s*(?<age>\d+)\s*$' })
  if ($guidLines.Count -ne 1 -or $ageLines.Count -ne 1) {
    throw 'Could not read exactly one GUID and age from the PDB summary.'
  }

  $guidMatch = [regex]::Match($guidLines[0], '^\s*GUID:\s*\{(?<guid>[0-9A-Fa-f-]{36})\}\s*$')
  $ageMatch = [regex]::Match($ageLines[0], '^\s*Age:\s*(?<age>\d+)\s*$')
  $guid = [Guid]::Empty
  if (-not [Guid]::TryParse($guidMatch.Groups['guid'].Value, [ref]$guid)) {
    throw 'The PDB contains an invalid GUID.'
  }
  [pscustomobject]@{
    Guid = $guid.ToString('N').ToUpperInvariant()
    Age  = [int]::Parse($ageMatch.Groups['age'].Value, [Globalization.CultureInfo]::InvariantCulture)
  }
}

function Get-PdbObjectKey {
  param(
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Commit,
    [Parameter(Mandatory)] [string] $Identity
  )

  if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') {
    throw "Invalid release version '$Version'."
  }
  if ($Commit -notmatch '^[0-9a-fA-F]{12}$') {
    throw "Expected the 12-character build commit, got '$Commit'."
  }
  if ($Identity -notmatch '^[0-9A-F]{32},\d+$') {
    throw "Invalid report PDB identity '$Identity'."
  }
  "symbols/$Version/$($Commit.ToLowerInvariant())/$Identity.pdb"
}

function Get-ReportPdbIdentity {
  param([Parameter(Mandatory)] $Identity)
  "$($Identity.Guid),$($Identity.Age)"
}

function Assert-PdbMatchesExecutable {
  param(
    [Parameter(Mandatory)] $ExecutableIdentity,
    [Parameter(Mandatory)] $PdbIdentity
  )
  $executableReportIdentity = Get-ReportPdbIdentity -Identity $ExecutableIdentity
  $pdbReportIdentity = Get-ReportPdbIdentity -Identity $PdbIdentity
  if ($pdbReportIdentity -cne $executableReportIdentity) {
    throw "The PDB identity does not match the executable (exe '$executableReportIdentity', PDB '$pdbReportIdentity')."
  }
  $executableReportIdentity
}

if ($MyInvocation.InvocationName -ne '.') {
  foreach ($argumentName in @('ExecutablePath', 'PdbPath', 'Version', 'Commit')) {
    if ([string]::IsNullOrWhiteSpace((Get-Variable -Name $argumentName -ValueOnly))) {
      throw "Missing required argument: -$argumentName"
    }
  }
  foreach ($path in @($ExecutablePath, $PdbPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Required release symbol input is missing: $path"
    }
  }
  if ((Get-Item -LiteralPath $PdbPath).Length -le 0) {
    throw "The release PDB is empty: $PdbPath"
  }

  $dumpbin = Get-Command dumpbin.exe -ErrorAction Stop
  $llvmPdbUtil = Get-Command llvm-pdbutil.exe -ErrorAction Stop
  $executableOutput = @(& $dumpbin.Source /headers $ExecutablePath 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "dumpbin failed with exit code $LASTEXITCODE." }
  $pdbOutput = @(& $llvmPdbUtil.Source dump -summary $PdbPath 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "llvm-pdbutil failed with exit code $LASTEXITCODE." }
  $executableLines = @($executableOutput | Where-Object { -not [string]::IsNullOrWhiteSpace("$_") } | ForEach-Object { "$_" })
  $pdbLines = @($pdbOutput | Where-Object { -not [string]::IsNullOrWhiteSpace("$_") } | ForEach-Object { "$_" })

  $executableIdentity = ConvertFrom-CodeViewOutput -Lines $executableLines
  $pdbIdentity = ConvertFrom-PdbSummaryOutput -Lines $pdbLines
  $reportIdentity = Assert-PdbMatchesExecutable -ExecutableIdentity $executableIdentity -PdbIdentity $pdbIdentity

  $key = Get-PdbObjectKey -Version $Version -Commit $Commit -Identity $reportIdentity
  $destination = "s3://$Bucket/$key"
  if ($DryRun) {
    Write-Output $destination
    return
  }

  foreach ($secretName in @('R2_ACCESS_KEY_ID', 'R2_SECRET_ACCESS_KEY', 'CLOUDFLARE_ACCOUNT_ID')) {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($secretName))) {
      throw "Required GitHub Actions secret is not configured: $secretName"
    }
  }
  $env:AWS_ACCESS_KEY_ID = $env:R2_ACCESS_KEY_ID
  $env:AWS_SECRET_ACCESS_KEY = $env:R2_SECRET_ACCESS_KEY
  $env:AWS_DEFAULT_REGION = 'auto'
  $endpoint = "https://$($env:CLOUDFLARE_ACCOUNT_ID).r2.cloudflarestorage.com"
  $aws = Get-Command aws.exe -ErrorAction Stop
  & $aws.Source s3 cp $PdbPath $destination --endpoint-url $endpoint --region auto --only-show-errors
  if ($LASTEXITCODE -ne 0) { throw "R2 PDB upload failed with exit code $LASTEXITCODE." }
  Write-Output "Archived PDB as $destination"
}
