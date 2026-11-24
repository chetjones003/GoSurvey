# Keeps only the newest Actions cache and artifact in each family, and deletes the rest.
#
# A family is the name with its unique parts masked out: pure-digit tokens (run ids, build
# numbers) and hex tokens of 8+ characters (commit shas). `sccache-msvc-release-Windows-44652391922-1`
# and `sccache-msvc-release-Windows-44593447355-1` are one family, so only the newer survives.
#
# Use -DryRun to list what would be deleted without deleting anything.
param(
  [string]$Repo = 'chetjones003/GoSurvey',
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Get-Family([string]$Name) {
  $tokens = $Name -split '-' | Where-Object {
    -not ($_ -match '^\d+$') -and -not ($_ -match '^[0-9a-fA-F]{8,}$')
  }
  return ($tokens -join '-')
}

function Get-Items([string]$Path, [string]$ListField, [string]$NameField) {
  # One row per item: id, name, created_at. gh --paginate emits one line set per page.
  $rows = gh api --paginate "repos/$Repo/actions/$Path" `
    --jq ".$ListField[] | [.id, .$NameField, .created_at] | @tsv"
  if ($LASTEXITCODE -ne 0) { throw "Listing $Path failed." }
  return $rows | Where-Object { $_ } | ForEach-Object {
    $parts = $_ -split "`t"
    [pscustomobject]@{ Id = $parts[0]; Name = $parts[1]; CreatedAt = $parts[2] }
  }
}

function Remove-ApiItem([string]$Kind, [string]$Name, [string]$Path) {
  if ($DryRun) {
    Write-Output "  would delete $Kind $Name"
    return
  }
  $response = gh api -X DELETE $Path 2>&1
  if ($LASTEXITCODE -eq 0) {
    Write-Output "  deleted $Kind $Name"
  } elseif ("$response" -match 'Not Found') {
    Write-Output "  already gone $Kind $Name"
  } else {
    throw "Deleting $Kind $Name failed: $response"
  }
}

function Remove-Stale([string]$Kind, [string]$Path, $Items) {
  $stale = @()
  $Items | Group-Object { Get-Family $_.Name } | ForEach-Object {
    $newest = $_.Group | Sort-Object CreatedAt -Descending | Select-Object -First 1
    $stale += $_.Group | Where-Object { $_.Id -ne $newest.Id }
  }
  Write-Output "$Kind`: $($Items.Count) total, $($stale.Count) stale."
  foreach ($item in $stale) {
    Remove-ApiItem $Kind "$($item.Name) ($($item.CreatedAt))" "repos/$Repo/actions/$Path/$($item.Id)"
  }
}

function Remove-OldWorkflowRuns([int]$RetentionDays = 7) {
  $cutoff = [DateTimeOffset]::UtcNow.AddDays(-$RetentionDays)
  $rows = gh api --paginate "repos/$Repo/actions/runs?per_page=100" `
    --jq '.workflow_runs[] | [.id, .created_at] | @tsv'
  if ($LASTEXITCODE -ne 0) { throw 'Listing workflow runs failed.' }
  $runs = @($rows | Where-Object { $_ } | ForEach-Object {
    $parts = $_ -split "`t"
    [pscustomobject]@{ Id = $parts[0]; CreatedAt = [DateTimeOffset]::Parse($parts[1]) }
  })
  $stale = @($runs | Where-Object { $_.CreatedAt -lt $cutoff })
  Write-Output "workflow runs: $($runs.Count) total, $($stale.Count) older than $RetentionDays days."
  foreach ($run in $stale) {
    Remove-ApiItem 'run' "$($run.Id) ($($run.CreatedAt.ToString('u')))" `
      "repos/$Repo/actions/runs/$($run.Id)"
  }
}

function Remove-SupersededReleases {
  $rows = gh api --paginate "repos/$Repo/releases?per_page=100" `
    --jq '.[] | [.id, .tag_name, .draft, .prerelease] | @tsv'
  if ($LASTEXITCODE -ne 0) { throw 'Listing releases failed.' }
  $releases = @($rows | Where-Object { $_ } | ForEach-Object {
    $parts = $_ -split "`t"
    [pscustomobject]@{
      Id = $parts[0]
      Tag = $parts[1]
      Draft = [bool]::Parse($parts[2])
      Prerelease = [bool]::Parse($parts[3])
    }
  })

  # Use GitHub's latest-release endpoint instead of assuming publication order identifies it.
  $stableReleases = @($releases | Where-Object { -not $_.Draft -and -not $_.Prerelease })
  $latestStableId = $null
  $latestStableResponse = gh api "repos/$Repo/releases/latest" --jq '.id' 2>&1
  $latestStableExitCode = $LASTEXITCODE
  if ($latestStableExitCode -eq 0) {
    $latestStableId = "$latestStableResponse".Trim()
    if (-not $latestStableId -or @($stableReleases | Where-Object { $_.Id -eq $latestStableId }).Count -ne 1) {
      throw 'GitHub latest-release endpoint returned an unexpected release.'
    }
  } elseif ($stableReleases.Count -ne 0 -or "$latestStableResponse" -notmatch 'Not Found') {
    throw "Resolving GitHub's latest stable release failed: $latestStableResponse"
  }

  $keepIds = @($latestStableId) + @($releases | Where-Object { $_.Tag -eq 'channel-beta' } | ForEach-Object Id)
  $stale = @($releases | Where-Object { -not $_.Draft -and $_.Id -notin $keepIds })
  Write-Output "releases: $($releases.Count) total, $($stale.Count) superseded."
  foreach ($release in $stale) {
    Remove-ApiItem 'release' "$($release.Tag) ($($release.Id)); its Git tag remains" `
      "repos/$Repo/releases/$($release.Id)"
  }
}

Remove-OldWorkflowRuns
Remove-Stale 'caches' 'caches' (Get-Items 'caches' 'actions_caches' 'key')
Remove-Stale 'artifacts' 'artifacts' (Get-Items 'artifacts' 'artifacts' 'name')
Remove-SupersededReleases
