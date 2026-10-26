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

$machineInstallFunction = $installerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-MachineInstall'
}, $true)
if (-not $machineInstallFunction) {
    throw 'scripts/install.ps1 is missing Get-MachineInstall.'
}
. ([scriptblock]::Create($machineInstallFunction.Extent.Text))
$perUserVersionFunction = $installerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-PerUserInstallVersion'
}, $true)
$versionComparisonFunction = $installerAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Test-InstalledVersionAtLeast'
}, $true)
if (-not $perUserVersionFunction -or -not $versionComparisonFunction) {
    throw 'scripts/install.ps1 is missing the installed-version check.'
}
. ([scriptblock]::Create($perUserVersionFunction.Extent.Text))
. ([scriptblock]::Create($versionComparisonFunction.Extent.Text))

$savedProgramFiles = $env:ProgramFiles
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('GoSurvey-migration-test-' + [Guid]::NewGuid().ToString('N'))
$legacyInstall = Join-Path $testRoot 'GoSurvey'
New-Item -ItemType Directory -Path $legacyInstall -Force | Out-Null
Set-Content -LiteralPath (Join-Path $legacyInstall 'unins000.exe') -Value 'test'
$env:ProgramFiles = $testRoot
$script:LegacyInstallFixture = [pscustomobject]@{
    DisplayName = 'GoSurvey version 0.7.4'
    DisplayVersion = '0.7.4'
    InstallLocation = $legacyInstall
}
function Get-ItemProperty {
    param([string]$LiteralPath, [string]$ErrorAction)
    return $script:LegacyInstallFixture
}
try {
    $detectedLegacyInstall = Get-MachineInstall
    if ($null -eq $detectedLegacyInstall -or $detectedLegacyInstall.InstallLocation -ne $legacyInstall) {
        throw 'The migration failed to detect the versioned GoSurvey 0.7.4 uninstall entry.'
    }
    if ((Get-PerUserInstallVersion) -ne '0.7.4') {
        throw 'The install script failed to read the installed per-user version.'
    }
    if (-not (Test-InstalledVersionAtLeast -InstalledVersion '0.7.7' -AvailableVersion '0.7.7') -or
        -not (Test-InstalledVersionAtLeast -InstalledVersion '0.7.8' -AvailableVersion '0.7.7') -or
        (Test-InstalledVersionAtLeast -InstalledVersion '0.7.6' -AvailableVersion '0.7.7')) {
        throw 'Installed-version comparison did not handle equal, newer, and older versions correctly.'
    }
} finally {
    Remove-Item Function:\Get-ItemProperty -ErrorAction SilentlyContinue
    $env:ProgramFiles = $savedProgramFiles
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$source = Get-Content -LiteralPath $installerPath -Raw
if ($source -notmatch '(?s)\$machineInstall = Get-MachineInstall.*?\$perUserVersion = Get-PerUserInstallVersion.*?Test-InstalledVersionAtLeast.*?Save-InstallerDownload') {
    throw 'The install script must check the installed version before downloading the installer.'
}
if ($source -notmatch '(?s)if \(\$uninstall\.ExitCode -ne 0\).*?if \(Get-MachineInstall\)') {
    throw 'The migration must verify that the old machine-wide GoSurvey registration is gone before installing the per-user copy.'
}
if ($source -notmatch "Join-Path \`$env:ProgramData 'Microsoft\\Windows\\Start Menu\\Programs\\GoSurvey\.lnk'") {
    throw 'The migration must remove the stale shared Start Menu shortcut before creating the per-user shortcut.'
}
if ($source -notmatch 'Remove-Item -LiteralPath \$machineStartMenuShortcut -Force -ErrorAction Stop') {
    throw 'Failure to remove the stale shared Start Menu shortcut must stop migration.'
}

$innoPath = Join-Path $repoRoot 'installer\GoSurvey.iss'
$innoSource = Get-Content -LiteralPath $innoPath -Raw
if ($innoSource -notmatch '(?s)function IsVersionAlreadyInstalled.*?IsVersionInstalledAtLeast\(HKCU\).*?IsVersionInstalledAtLeast\(HKLM\).*?function InitializeSetup') {
    throw 'The Inno installer must check per-user and machine-wide registrations before replacing files.'
}

Write-Output 'Installer migration postcondition tests passed (legacy registration and shared shortcut).'
