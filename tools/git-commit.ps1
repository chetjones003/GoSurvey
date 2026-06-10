<#
.SYNOPSIS
  Commit with a multi-line message without the PowerShell here-string pitfall.

.DESCRIPTION
  `git commit -m @'...'@` in PowerShell frequently leaks a stray '@' into the
  subject line, because `@'` after `-m` is not always parsed as a here-string.
  This helper writes the message to a UTF-8 (no BOM) temp file and commits with
  `git commit -F`, appending the Claude co-author trailer when it is missing.

.PARAMETER Message
  The full commit message (subject on the first line, blank line, then body).
  Use `n for newlines in a double-quoted string, or pass a single-quoted
  here-string -- both are safe here because the text never reaches `git -m`.

.PARAMETER File
  Read the message from a file instead of -Message.

.PARAMETER Amend
  Amend the previous commit instead of creating a new one.

.PARAMETER NoCoAuthor
  Do not append the Co-Authored-By trailer.

.EXAMPLE
  ./tools/git-commit.ps1 "fix(io): handle empty file`n`nReturn early when the path is empty."

.EXAMPLE
  ./tools/git-commit.ps1 -File .git/COMMIT_MSG_TMP.txt -Amend
#>
[CmdletBinding(DefaultParameterSetName = 'Message')]
param(
    [Parameter(ParameterSetName = 'Message', Mandatory, Position = 0)]
    [string]$Message,
    [Parameter(ParameterSetName = 'File', Mandatory)]
    [string]$File,
    [switch]$Amend,
    [switch]$NoCoAuthor
)

$ErrorActionPreference = 'Stop'
$trailer = 'Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>'

if ($PSCmdlet.ParameterSetName -eq 'File') {
    $body = Get-Content -Raw -Path $File
} else {
    $body = $Message
}

if (-not $NoCoAuthor -and $body -notmatch 'Co-Authored-By:') {
    $body = $body.TrimEnd() + "`n`n" + $trailer
}

$tmp = New-TemporaryFile
try {
    [System.IO.File]::WriteAllText($tmp.FullName, $body, (New-Object System.Text.UTF8Encoding($false)))
    $gitArgs = @('commit', '-F', $tmp.FullName)
    if ($Amend) { $gitArgs += '--amend' }
    git @gitArgs
} finally {
    Remove-Item $tmp.FullName -ErrorAction SilentlyContinue
}
