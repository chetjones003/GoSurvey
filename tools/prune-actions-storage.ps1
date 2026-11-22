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

function Remove-Stale([string]$Kind, [string]$Path, $Items) {
  $stale = @()
  $Items | Group-Object { Get-Family $_.Name } | ForEach-Object {
    $newest = $_.Group | Sort-Object CreatedAt -Descending | Select-Object -First 1
    $stale += $_.Group | Where-Object { $_.Id -ne $newest.Id }
  }
  Write-Output "$Kind`: $($Items.Count) total, $($stale.Count) stale."
  foreach ($item in $stale) {
    if ($DryRun) {
      Write-Output "  would delete $($item.Name) ($($item.CreatedAt))"
      continue
    }
    $response = gh api -X DELETE "repos/$Repo/actions/$Path/$($item.Id)" 2>&1
    if ($LASTEXITCODE -eq 0) {
      Write-Output "  deleted $($item.Name) ($($item.CreatedAt))"
    } elseif ("$response" -match 'Not Found') {
      # Already gone (expired, or removed by an earlier pass).
      Write-Output "  already gone $($item.Name) ($($item.CreatedAt))"
    } else {
      throw "Deleting $Kind $($item.Id) failed: $response"
    }
  }
}

Remove-Stale 'caches' 'caches' (Get-Items 'caches' 'actions_caches' 'key')
Remove-Stale 'artifacts' 'artifacts' (Get-Items 'artifacts' 'artifacts' 'name')
