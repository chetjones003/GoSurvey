# Install Git hooks for this clone (REQ-336 authoring lock and future hooks).
# Safe to re-run. Sets core.hooksPath to tools/git-hooks for this repository only.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
git config core.hooksPath tools/git-hooks
Write-Host "core.hooksPath = tools/git-hooks"
Write-Host "REQ-336: pushes of resources/whats-new.md require tools/approve-whats-new.ps1 first."
