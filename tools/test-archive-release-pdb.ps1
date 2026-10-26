$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'archive-release-pdb.ps1')

$codeView = @('  Format: RSDS, {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}, 9, GoSurvey.pdb')
$pdbSummary = @('  Age: 9', '  GUID: {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}')
$exeIdentity = ConvertFrom-CodeViewOutput -Lines $codeView
$pdbIdentity = ConvertFrom-PdbSummaryOutput -Lines $pdbSummary
$identity = Assert-PdbMatchesExecutable -ExecutableIdentity $exeIdentity -PdbIdentity $pdbIdentity
if ($identity -cne 'B9EAF39516DC48E4AE8D7BE49BB99584,9') { throw "Unexpected report PDB identity: $identity" }

$key = Get-PdbObjectKey -Version '0.7.4' -Commit '0123456789ab' -Identity $identity
if ($key -cne 'symbols/0.7.4/0123456789ab/B9EAF39516DC48E4AE8D7BE49BB99584,9.pdb') {
  throw "Unexpected object key: $key"
}

foreach ($badCodeView in @(
  @('no CodeView directory'),
  @('Format: RSDS, {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}, 9, a.pdb', 'Format: RSDS, {B9EAF395-16DC-48E4-AE8D-7BE49BB99585}, 9, b.pdb')
)) {
  $threw = $false
  try { ConvertFrom-CodeViewOutput -Lines $badCodeView | Out-Null } catch { $threw = $true }
  if (-not $threw) { throw 'Malformed or ambiguous executable CodeView metadata was accepted.' }
}

$threw = $false
try { ConvertFrom-PdbSummaryOutput -Lines @('not a PDB summary') | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A PDB summary without GUID/age was accepted.' }

$threw = $false
try { Get-PdbObjectKey -Version '0.7.4' -Commit 'short' -Identity $identity | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A non-build-identified commit was accepted.' }

$missingPath = Join-Path $env:TEMP 'gosurvey-missing-release-pdb-test-input'
$threw = $false
try {
  & (Join-Path $PSScriptRoot 'archive-release-pdb.ps1') `
    -ExecutablePath $missingPath -PdbPath $missingPath -Version '0.7.4' -Commit '0123456789ab' -DryRun
} catch { $threw = $true }
if (-not $threw) { throw 'Missing executable/PDB inputs were accepted.' }

$wrongPdb = ConvertFrom-PdbSummaryOutput -Lines @('Age: 8', 'GUID: {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}')
$threw = $false
try { Assert-PdbMatchesExecutable -ExecutableIdentity $exeIdentity -PdbIdentity $wrongPdb | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A mismatched PDB identity was accepted.' }
Write-Output 'Archive-release-PDB tests passed.'
