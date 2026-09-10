# REQ-336 — one-time unlock so resources/whats-new.md may be pushed after Chet's review.
# Creates .whats-new-push-unlock (gitignored). The pre-push hook consumes it on the next push
# that includes the notes file.
$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root ".whats-new-push-unlock"
"approved $(Get-Date -Format o)" | Set-Content -Path $path -Encoding utf8
Write-Host "Created $path"
Write-Host "You may now push a commit that includes resources/whats-new.md."
Write-Host "The unlock is removed automatically after that push."
