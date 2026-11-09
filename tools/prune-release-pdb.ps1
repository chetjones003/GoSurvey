param(
  [ValidateSet('stable', 'beta', '')] [string] $Channel = '',
  [string] $CurrentKey = '',
  [string] $Bucket = 'gosurvey-crash-dumps',
  [switch] $DryRun
)

# REQ-425 / D-2026-11-09-b: symbol retention follows the versions, not the age of the objects.
#   stable  keeps every PDB in the newest minor series (0.N.x) and deletes older minor series.
#   beta    keeps only the newest beta PDB.
# The archived key is always kept, and nothing outside symbols/<channel>/ is ever listed or deleted.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertFrom-SymbolKey {
  param([Parameter(Mandatory)] [string] $Key)

  $match = [regex]::Match($Key,
    '^symbols/(?<channel>stable|beta)/(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)(?:-[A-Za-z0-9.-]+)?/[0-9a-f]{12}/[0-9A-F]{32},\d+\.pdb$')
  if (-not $match.Success) { return $null }
  [pscustomobject]@{
    Key     = $Key
    Channel = $match.Groups['channel'].Value
    Major   = [int]$match.Groups['major'].Value
    Minor   = [int]$match.Groups['minor'].Value
    Patch   = [int]$match.Groups['patch'].Value
  }
}

function Get-PdbPrunePlan {
  param(
    [Parameter(Mandatory)] [ValidateSet('stable', 'beta')] [string] $Channel,
    [Parameter(Mandatory)] [string] $CurrentKey,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Keys
  )

  $prefix = "symbols/$Channel/"
  if (-not $CurrentKey.StartsWith($prefix, [StringComparison]::Ordinal)) {
    throw "The archived key '$CurrentKey' is not under '$prefix'."
  }

  $ours = New-Object System.Collections.Generic.List[object]
  $unrecognised = New-Object System.Collections.Generic.List[string]
  foreach ($key in $Keys) {
    # Other channels and other prefixes (crash-dumps/) are not this rule's business.
    if (-not $key.StartsWith($prefix, [StringComparison]::Ordinal)) { continue }
    $item = ConvertFrom-SymbolKey -Key $key
    if ($null -eq $item) { $unrecognised.Add($key); continue }
    $ours.Add($item)
  }

  if (-not ($ours | Where-Object { $_.Key -ceq $CurrentKey })) {
    throw "The archived PDB '$CurrentKey' is not in the bucket listing; nothing was deleted."
  }

  if ($Channel -eq 'beta') {
    $newest = $ours | Sort-Object Major, Minor, Patch -Descending | Select-Object -First 1
    $newestSeries = @($ours | Where-Object {
      $_.Major -eq $newest.Major -and $_.Minor -eq $newest.Minor -and $_.Patch -eq $newest.Patch })
  } else {
    $newest = $ours | Sort-Object Major, Minor -Descending | Select-Object -First 1
    $newestSeries = @($ours | Where-Object { $_.Major -eq $newest.Major -and $_.Minor -eq $newest.Minor })
  }

  $keep = @(@($newestSeries | ForEach-Object { $_.Key }) + $CurrentKey | Select-Object -Unique)
  $delete = @($ours | Where-Object { $keep -cnotcontains $_.Key } | ForEach-Object { $_.Key })

  [pscustomobject]@{
    Keep         = $keep
    Delete       = $delete
    Unrecognised = @($unrecognised)
  }
}

if ($MyInvocation.InvocationName -ne '.') {
  foreach ($argumentName in @('Channel', 'CurrentKey')) {
    if ([string]::IsNullOrWhiteSpace((Get-Variable -Name $argumentName -ValueOnly))) {
      throw "Missing required argument: -$argumentName"
    }
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

  $listingJson = & $aws.Source s3api list-objects-v2 --bucket $Bucket --prefix "symbols/$Channel/" `
    --endpoint-url $endpoint --region auto --output json
  if ($LASTEXITCODE -ne 0) { throw "R2 listing failed with exit code $LASTEXITCODE; nothing was deleted." }
  $listing = ($listingJson -join "`n") | ConvertFrom-Json
  $existing = if ($null -eq $listing.Contents) { @() } else { @($listing.Contents | ForEach-Object { $_.Key }) }

  $plan = Get-PdbPrunePlan -Channel $Channel -CurrentKey $CurrentKey -Keys $existing
  foreach ($key in $plan.Unrecognised) {
    Write-Warning "Left in place, unrecognised symbol key: $key"
  }
  Write-Output "Keeping $($plan.Keep.Count) PDB(s) for ${Channel}:"
  $plan.Keep | ForEach-Object { Write-Output "  keep   $_" }
  foreach ($key in $plan.Delete) {
    if ($DryRun) {
      Write-Output "  delete $key (dry run)"
      continue
    }
    & $aws.Source s3 rm "s3://$Bucket/$key" --endpoint-url $endpoint --region auto --only-show-errors
    if ($LASTEXITCODE -ne 0) { throw "R2 delete failed for $key with exit code $LASTEXITCODE." }
    Write-Output "  deleted $key"
  }
}
