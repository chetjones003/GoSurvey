$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'prune-release-pdb.ps1')

function Get-TestKey {
  param([string] $Channel, [string] $Version, [string] $Commit = '0123456789ab', [int] $Age = 1)
  "symbols/$Channel/$Version/$Commit/0123456789ABCDEF0123456789ABCDEF,$Age.pdb"
}

function Assert-SameKeys {
  param([string] $Name, [string[]] $Actual, [string[]] $Expected)
  $actualText = (@($Actual | Sort-Object) -join "`n")
  $expectedText = (@($Expected | Sort-Object) -join "`n")
  if ($actualText -cne $expectedText) {
    throw "$Name failed.`nExpected:`n$expectedText`nActual:`n$actualText"
  }
}

function Assert-Throws {
  param([string] $Name, [scriptblock] $Action)
  $threw = $false
  try { & $Action | Out-Null } catch { $threw = $true }
  if (-not $threw) { throw "$Name was accepted but should have been refused." }
}

# Beta keeps only the newest PDB.
$beta079 = Get-TestKey -Channel beta -Version '0.7.9'
$beta0710 = Get-TestKey -Channel beta -Version '0.7.10'
$plan = Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @($beta079, $beta0710)
Assert-SameKeys 'beta: a newer beta deletes the previous beta' $plan.Delete @($beta079)
Assert-SameKeys 'beta: the newest beta is kept' $plan.Keep @($beta0710)

# Two builds of the same newest beta version are both kept.
$beta0710Other = Get-TestKey -Channel beta -Version '0.7.10' -Commit 'fedcba987654' -Age 2
$plan = Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @($beta079, $beta0710, $beta0710Other)
Assert-SameKeys 'beta: same newest version keeps every build' $plan.Delete @($beta079)

# Stable keeps the newest minor series and deletes older series.
$s072 = Get-TestKey -Channel stable -Version '0.7.2'
$s078 = Get-TestKey -Channel stable -Version '0.7.8'
$s080 = Get-TestKey -Channel stable -Version '0.8.0'
$plan = Get-PdbPrunePlan -Channel stable -CurrentKey $s080 -Keys @($s072, $s078, $s080)
Assert-SameKeys 'stable: a newer minor deletes every older minor series' $plan.Delete @($s072, $s078)
Assert-SameKeys 'stable: the newest minor series is kept' $plan.Keep @($s080)

# Patches inside the newest minor series are all kept.
$s079 = Get-TestKey -Channel stable -Version '0.7.9'
$plan = Get-PdbPrunePlan -Channel stable -CurrentKey $s079 -Keys @($s072, $s078, $s079)
Assert-SameKeys 'stable: patches in the newest minor series are kept' $plan.Delete @()

# A hotfix archived on an older series keeps the newest series and the hotfix itself.
$plan = Get-PdbPrunePlan -Channel stable -CurrentKey $s079 -Keys @($s078, $s080, $s079)
Assert-SameKeys 'stable: an older-series hotfix keeps itself and the newest series' $plan.Delete @($s078)
Assert-SameKeys 'stable: an older-series hotfix keeps the newest series' $plan.Keep @($s080, $s079)

# The other channel and the crash-dump prefix are never listed for deletion.
$crashDump = 'crash-dumps/0b1c2d3e-4f50-4a6b-8c7d-9e0f1a2b3c4d.dmp'
$plan = Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @($crashDump, $s078, $beta079, $beta0710)
Assert-SameKeys 'beta run never deletes stable symbols or crash dumps' $plan.Delete @($beta079)

# A malformed key under the channel prefix is reported and left alone.
$junk = 'symbols/beta/not-a-version.pdb'
$plan = Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @($junk, $beta079, $beta0710)
Assert-SameKeys 'malformed symbol key is never deleted' $plan.Delete @($beta079)
Assert-SameKeys 'malformed symbol key is reported' $plan.Unrecognised @($junk)

# A single archived PDB has nothing to delete.
$plan = Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @($beta0710)
Assert-SameKeys 'a lone PDB is kept and nothing is deleted' $plan.Delete @()

# Refusals: the archived key must be in the listing and under the channel prefix.
Assert-Throws 'a listing without the archived key' { Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @($beta079) }
Assert-Throws 'an empty listing' { Get-PdbPrunePlan -Channel beta -CurrentKey $beta0710 -Keys @() }
Assert-Throws 'an archived key from another channel' { Get-PdbPrunePlan -Channel beta -CurrentKey $s080 -Keys @($s080) }

Write-Output 'Prune-release-PDB tests passed.'
