#Requires -Version 7.2
[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidateSet('store', 'restore', 'prune')][string]$Action,
  [string]$Name,
  [string]$SourcePath,
  [string]$Destination,
  [string]$StorageRoot = 'C:\GoSurveyCI\actions-storage\artifacts',
  [ValidateRange(1, 365)][int]$RetentionDays = 2
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Assert-PlainPath([string]$Path) {
  $full = [IO.Path]::GetFullPath($Path)
  $driveRoot = [IO.Path]::GetPathRoot($full)
  $cursor = $driveRoot
  $remaining = $full.Substring($driveRoot.Length).Split([IO.Path]::DirectorySeparatorChar)
  foreach ($part in $remaining) {
    if ([string]::IsNullOrWhiteSpace($part)) { continue }
    $cursor = Join-Path $cursor $part
    if (Test-Path -LiteralPath $cursor) {
      $item = Get-Item -LiteralPath $cursor -Force
      if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to use a reparse point in local artifact path: $cursor"
      }
    }
  }
}

function Assert-UnderRoot([string]$Path, [string]$Root) {
  $fullPath = Get-FullPath $Path
  $fullRoot = (Get-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
  if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside the local artifact root: $fullPath"
  }
}

function Remove-ExpiredArtifacts([string]$Root, [int]$Days) {
  if (-not (Test-Path -LiteralPath $Root)) { return }
  Assert-PlainPath $Root
  $cutoff = [DateTime]::UtcNow.AddDays(-$Days)
  foreach ($ownerDirectory in Get-ChildItem -LiteralPath $Root -Directory -Force) {
    if (($ownerDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
      Write-Warning "Skipping reparse-point directory under local artifact root: $($ownerDirectory.FullName)"
      continue
    }
    foreach ($runDirectory in Get-ChildItem -LiteralPath $ownerDirectory.FullName -Directory -Force) {
      if (($runDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        Write-Warning "Skipping reparse-point directory under local artifact root: $($runDirectory.FullName)"
        continue
      }
      foreach ($attemptDirectory in Get-ChildItem -LiteralPath $runDirectory.FullName -Directory -Force) {
        if (($attemptDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
          Write-Warning "Skipping reparse-point directory under local artifact root: $($attemptDirectory.FullName)"
          continue
        }
        foreach ($artifactDirectory in Get-ChildItem -LiteralPath $attemptDirectory.FullName -Directory -Force) {
          if (($artifactDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Write-Warning "Skipping reparse-point directory under local artifact root: $($artifactDirectory.FullName)"
            continue
          }
          if ($artifactDirectory.LastWriteTimeUtc -lt $cutoff) {
            Assert-UnderRoot $artifactDirectory.FullName $Root
            Remove-Item -LiteralPath $artifactDirectory.FullName -Recurse -Force
            Write-Host "Removed expired CI artifact: $($artifactDirectory.FullName)"
          }
        }
        foreach ($directory in @($attemptDirectory, $runDirectory, $ownerDirectory)) {
          if ((Test-Path -LiteralPath $directory.FullName) -and
              -not (Get-ChildItem -LiteralPath $directory.FullName -Force | Select-Object -First 1)) {
            Remove-Item -LiteralPath $directory.FullName -Force
          }
        }
        if (-not (Test-Path -LiteralPath $runDirectory.FullName)) { break }
      }
      if (-not (Test-Path -LiteralPath $runDirectory.FullName)) { continue }
    }
  }
}

$fullStorageRoot = Get-FullPath $StorageRoot
if ($Action -eq 'prune') {
  Remove-ExpiredArtifacts $fullStorageRoot $RetentionDays
  exit 0
}

if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$') {
  throw 'Artifact name must contain only letters, digits, dot, underscore, or hyphen.'
}
if ($env:GITHUB_REPOSITORY -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
  throw 'GITHUB_REPOSITORY is missing or invalid.'
}
if ($env:GITHUB_RUN_ID -notmatch '^[0-9]+$' -or $env:GITHUB_RUN_ATTEMPT -notmatch '^[0-9]+$') {
  throw 'GITHUB_RUN_ID and GITHUB_RUN_ATTEMPT must be numeric.'
}

Remove-ExpiredArtifacts $fullStorageRoot $RetentionDays
New-Item -ItemType Directory -Force -Path $fullStorageRoot | Out-Null
Assert-PlainPath $fullStorageRoot

$repositoryKey = $env:GITHUB_REPOSITORY.Replace('/', '_')
$attemptRoot = Join-Path (Join-Path (Join-Path $fullStorageRoot $repositoryKey) $env:GITHUB_RUN_ID) $env:GITHUB_RUN_ATTEMPT
$artifactPath = Join-Path $attemptRoot $Name
New-Item -ItemType Directory -Force -Path $attemptRoot | Out-Null
Assert-PlainPath $attemptRoot

if ($Action -eq 'store') {
  if (-not $SourcePath -or -not (Test-Path -LiteralPath $SourcePath -PathType Container)) {
    throw "Artifact source directory does not exist: $SourcePath"
  }
  $sourceFullPath = Get-FullPath $SourcePath
  if ($sourceFullPath.Equals($fullStorageRoot, [StringComparison]::OrdinalIgnoreCase) -or
      $sourceFullPath.StartsWith(($fullStorageRoot + '\'), [StringComparison]::OrdinalIgnoreCase) -or
      $fullStorageRoot.StartsWith(($sourceFullPath + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Artifact source and storage root must not contain one another.'
  }
  Assert-PlainPath $sourceFullPath
  foreach ($entry in Get-ChildItem -LiteralPath $sourceFullPath -Force -Recurse) {
    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "Refusing to store a reparse point as a CI artifact: $($entry.FullName)"
    }
  }
  if (Test-Path -LiteralPath $artifactPath) {
    Assert-PlainPath $artifactPath
    Remove-Item -LiteralPath $artifactPath -Recurse -Force
  }
  $stagingPath = Join-Path $attemptRoot ($Name + '.tmp-' + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $stagingPath | Out-Null
  try {
    foreach ($entry in Get-ChildItem -LiteralPath $sourceFullPath -Force) {
      Copy-Item -LiteralPath $entry.FullName -Destination $stagingPath -Recurse -Force
    }
    Move-Item -LiteralPath $stagingPath -Destination $artifactPath
  } catch {
    if (Test-Path -LiteralPath $stagingPath) { Remove-Item -LiteralPath $stagingPath -Recurse -Force }
    throw
  }
  Write-Host "Stored local CI artifact: $artifactPath"
  exit 0
}

if (-not $Destination) { throw 'Destination is required for restore.' }
# A re-run of only the failed jobs gets a new attempt number; reuse the newest earlier attempt.
for ($attempt = [int]$env:GITHUB_RUN_ATTEMPT - 1;
     $attempt -ge 1 -and -not (Test-Path -LiteralPath $artifactPath -PathType Container); $attempt--) {
  $artifactPath = Join-Path (Join-Path (Join-Path (Join-Path $fullStorageRoot $repositoryKey) $env:GITHUB_RUN_ID) $attempt) $Name
}
if (-not (Test-Path -LiteralPath $artifactPath -PathType Container)) {
  throw "Local CI artifact is missing: $artifactPath"
}
Assert-PlainPath $artifactPath
$destinationFullPath = Get-FullPath $Destination
$artifactPath = Get-FullPath $artifactPath
if ($destinationFullPath.Equals($artifactPath, [StringComparison]::OrdinalIgnoreCase) -or
    $destinationFullPath.StartsWith(($artifactPath + '\'), [StringComparison]::OrdinalIgnoreCase) -or
    $artifactPath.StartsWith(($destinationFullPath + '\'), [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Restore destination and artifact source must not contain one another.'
}
New-Item -ItemType Directory -Force -Path $destinationFullPath | Out-Null
Assert-PlainPath $destinationFullPath
foreach ($entry in Get-ChildItem -LiteralPath $artifactPath -Force) {
  Copy-Item -LiteralPath $entry.FullName -Destination $destinationFullPath -Recurse -Force
}
Write-Host "Restored local CI artifact '$Name' to $destinationFullPath"
