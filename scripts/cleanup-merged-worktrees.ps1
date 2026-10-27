# Removes worktrees and local branches whose pull request has been merged.
# GitHub deletes the remote branch on merge (repository setting); this clears the local side.
# Usage: pwsh scripts/cleanup-merged-worktrees.ps1 [-WhatIf]
[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
$mainWorktree = (git rev-parse --path-format=absolute --git-common-dir | Split-Path -Parent)
Set-Location -LiteralPath $mainWorktree
git fetch --prune origin | Out-Null

$protected = @('master', 'beta')
$entries = @()
$current = @{}
foreach ($line in (git worktree list --porcelain) + '') {
    if ($line -match '^worktree (.+)$') { $current = @{ Path = $Matches[1] } }
    elseif ($line -match '^branch refs/heads/(.+)$') { $current.Branch = $Matches[1] }
    elseif ($line -eq '' -and $current.Path) { $entries += [pscustomobject]$current; $current = @{} }
}

foreach ($entry in $entries) {
    if (-not $entry.Branch -or $entry.Branch -in $protected) { continue }
    if ((Resolve-Path -LiteralPath $entry.Path).Path -eq (Resolve-Path -LiteralPath $mainWorktree).Path) { continue }

    $merged = gh pr list --head $entry.Branch --state merged --json number --jq 'length'
    if ([int]$merged -eq 0) { continue }
    if (git -C $entry.Path status --porcelain) {
        Write-Warning "Skipping $($entry.Path): merged branch '$($entry.Branch)' has uncommitted changes."
        continue
    }
    if ($PSCmdlet.ShouldProcess($entry.Path, "Remove worktree and delete branch $($entry.Branch)")) {
        git worktree remove --force $entry.Path
        if (Test-Path -LiteralPath $entry.Path) {
            Remove-Item -LiteralPath $entry.Path -Recurse -Force -ErrorAction SilentlyContinue
            git worktree prune
        }
        git branch -D $entry.Branch | Out-Null
        Write-Output "Removed $($entry.Path) and branch $($entry.Branch)."
    }
}
