$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$installerPath = Join-Path $repoRoot 'scripts\install.ps1'
$tokens = $null
$parseErrors = $null
$installerAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $installerPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) {
    throw "Could not parse scripts/install.ps1: $($parseErrors[0].Message)"
}

$parserFunction = $installerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'ConvertFrom-ReleaseManifestContent'
}, $true)
if (-not $parserFunction) {
    throw 'scripts/install.ps1 is missing ConvertFrom-ReleaseManifestContent.'
}
. ([scriptblock]::Create($parserFunction.Extent.Text))

$json = '{"version":"0.7.5","channel":"stable"}'
$bytes = [Text.Encoding]::UTF8.GetBytes($json)
$fromBytes = ConvertFrom-ReleaseManifestContent -Content $bytes
if ($fromBytes.version -ne '0.7.5' -or $fromBytes.channel -ne 'stable') {
    throw "Byte-backed manifest parsed incorrectly: version='$($fromBytes.version)' channel='$($fromBytes.channel)'"
}

$fromString = ConvertFrom-ReleaseManifestContent -Content $json
if ($fromString.version -ne '0.7.5' -or $fromString.channel -ne 'stable') {
    throw "String-backed manifest parsed incorrectly: version='$($fromString.version)' channel='$($fromString.channel)'"
}

$malformedRejected = $false
try {
    ConvertFrom-ReleaseManifestContent -Content ([Text.Encoding]::UTF8.GetBytes('{')) | Out-Null
} catch {
    $malformedRejected = $true
}
if (-not $malformedRejected) {
    throw 'Malformed manifest JSON was accepted.'
}

Write-Output 'Installer manifest parser tests passed (byte content, string content, malformed JSON).'
