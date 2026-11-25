$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'archive-release-pdb.ps1')

$codeView = @('  Format: RSDS, {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}, 9, GoSurvey.pdb')
$pdbSummary = @('  Age: 9', '  GUID: {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}')
$exeIdentity = ConvertFrom-CodeViewOutput -Lines $codeView
$pdbIdentity = ConvertFrom-PdbSummaryOutput -Lines $pdbSummary
$identity = Assert-PdbMatchesExecutable -ExecutableIdentity $exeIdentity -PdbIdentity $pdbIdentity
if ($identity -cne 'B9EAF39516DC48E4AE8D7BE49BB99584,9') { throw "Unexpected report PDB identity: $identity" }

$key = Get-PdbObjectKey -Channel 'beta' -Version '0.7.4' -Commit '0123456789ab' -Identity $identity
if ($key -cne 'symbols/beta/0.7.4/0123456789ab/B9EAF39516DC48E4AE8D7BE49BB99584,9.pdb') {
  throw "Unexpected object key: $key"
}
$stableKey = Get-PdbObjectKey -Channel 'stable' -Version '0.7.4' -Commit '0123456789ab' -Identity $identity
if ($stableKey -cne 'symbols/stable/0.7.4/0123456789ab/B9EAF39516DC48E4AE8D7BE49BB99584,9.pdb') {
  throw "Unexpected stable object key: $stableKey"
}
# D-2026-11-09-d: a developer build is never archived, so it has no symbol channel of its own.
$threw = $false
try { Get-PdbObjectKey -Channel 'dev' -Version '0.7.4' -Commit '0123456789ab' -Identity $identity | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A developer channel was accepted as a symbol channel.' }

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
try { Get-PdbObjectKey -Channel 'beta' -Version '0.7.4' -Commit 'short' -Identity $identity | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A non-build-identified commit was accepted.' }

$missingPath = Join-Path $env:TEMP 'gosurvey-missing-release-pdb-test-input'
$threw = $false
try {
  & (Join-Path $PSScriptRoot 'archive-release-pdb.ps1') `
    -ExecutablePath $missingPath -PdbPath $missingPath -Version '0.7.4' -Commit '0123456789ab' -Channel 'beta' -DryRun
} catch { $threw = $true }
if (-not $threw) { throw 'Missing executable/PDB inputs were accepted.' }

$wrongPdb = ConvertFrom-PdbSummaryOutput -Lines @('Age: 8', 'GUID: {B9EAF395-16DC-48E4-AE8D-7BE49BB99584}')
$threw = $false
try { Assert-PdbMatchesExecutable -ExecutableIdentity $exeIdentity -PdbIdentity $wrongPdb | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A mismatched PDB identity was accepted.' }

# Same GUID bytes rendered in raw byte order (as some llvm-pdbutil builds print it).
$rawOrderPdb = ConvertFrom-PdbSummaryOutput -Lines @('Age: 9', 'GUID: {95F3EAB9-DC16-E448-AE8D-7BE49BB99584}')
$rawOrderIdentity = Assert-PdbMatchesExecutable -ExecutableIdentity $exeIdentity -PdbIdentity $rawOrderPdb 3>$null
if ($rawOrderIdentity -cne 'B9EAF39516DC48E4AE8D7BE49BB99584,9') { throw "Raw-order PDB GUID gave identity $rawOrderIdentity" }
$threw = $false
$rawOrderWrongAge = ConvertFrom-PdbSummaryOutput -Lines @('Age: 8', 'GUID: {95F3EAB9-DC16-E448-AE8D-7BE49BB99584}')
try { Assert-PdbMatchesExecutable -ExecutableIdentity $exeIdentity -PdbIdentity $rawOrderWrongAge 3>$null | Out-Null } catch { $threw = $true }
if (-not $threw) { throw 'A raw-order PDB GUID with the wrong age was accepted.' }

function Invoke-TestPdbUpload {
  $exitCode = $script:uploadExitCodes.Dequeue()
  if ($exitCode -ne 0) {
    [Console]::Error.WriteLine('Connection was closed before we received a valid response from endpoint URL.')
  }
  & cmd.exe /d /c "exit $exitCode"
}

$uploadArguments = @{
  AwsPath = 'Invoke-TestPdbUpload'
  PdbPath = 'GoSurvey.pdb'
  Destination = "s3://test-bucket/$key"
  Endpoint = 'https://test.invalid'
}

function Start-Sleep {
  param([int] $Seconds)
  $script:retryDelays.Add($Seconds)
}

$uploadExitCodes = [Collections.Generic.Queue[int]]::new([int[]] @(1, 0))
$retryDelays = [Collections.Generic.List[int]]::new()
Send-ReleasePdb @uploadArguments
if ($uploadExitCodes.Count -ne 0 -or ($retryDelays -join ',') -ne '5') {
  throw 'A disconnected upload must be retried after a short delay and stop after success.'
}

$uploadExitCodes = [Collections.Generic.Queue[int]]::new([int[]] @(1, 1, 1))
$retryDelays.Clear()
$uploadError = $null
try { Send-ReleasePdb @uploadArguments } catch { $uploadError = $_.Exception.Message }
if ($uploadError -cne 'R2 PDB upload failed with exit code 1.' -or
    $uploadExitCodes.Count -ne 0 -or ($retryDelays -join ',') -ne '5,10') {
  throw 'A persistently failing upload must stop after three attempts and fail the archive.'
}

$uploadExitCodes = [Collections.Generic.Queue[int]]::new([int[]] @(0))
$retryDelays.Clear()
Send-ReleasePdb @uploadArguments
if ($uploadExitCodes.Count -ne 0 -or $retryDelays.Count -ne 0) {
  throw 'A successful first upload must not be retried.'
}

Write-Output 'Archive-release-PDB tests passed.'
