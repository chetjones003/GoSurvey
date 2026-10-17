param(
  [string]$Reference = 'origin/master',
  [string]$OutputPath = 'build/qt-parity.json'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $repoRoot
try {
  $revision = & git rev-parse --verify $Reference
  if ($LASTEXITCODE -ne 0) { throw "Cannot resolve $Reference" }
  $header = (& git show "${Reference}:src/commands/CadCommands.hpp") -join "`n"
  if ($LASTEXITCODE -ne 0) { throw 'Cannot read upstream command state' }
  $source = (& git show "${Reference}:src/commands/CadCommands.cpp") -join "`n"
  if ($LASTEXITCODE -ne 0) { throw 'Cannot read upstream command registry' }
  $registry = [regex]::Match($source, '(?s)const CmdEntry kRegistry\[\]\s*=\s*\{(.*?)\n\};').Groups[1].Value
  if (-not $registry) { throw 'Upstream command registry format changed; update the inventory extractor' }
  $commands = @([regex]::Matches($registry, '\{"([^"]+)",\s*"([^"]*)",\s*"([^"]*)"\}') | ForEach-Object {
    [ordered]@{ name = $_.Groups[1].Value; aliases = $_.Groups[2].Value; description = $_.Groups[3].Value; status = 'requires-parity-test' }
  })
  $flags = @([regex]::Matches($header, '\bbool\s+(show\w+)\s*=') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
  $nativeSource = (Get-ChildItem -LiteralPath 'src/qt' -Filter '*.cpp' | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
  $dialogs = @($flags | ForEach-Object {
    [ordered]@{ stateFlag = $_; nativeReference = $nativeSource.Contains($_); status = 'requires-parity-test' }
  })
  $report = [ordered]@{
    reference = $Reference
    revision = $revision.Trim()
    commandCount = $commands.Count
    windowFlagCount = $dialogs.Count
    commands = $commands
    windows = $dialogs
    note = 'A native source reference or shared command handler is not evidence of functional parity. Each row needs workflow tests before release.'
  }
  $target = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
  if (-not $target.StartsWith($repoRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Inventory output must stay inside the repository'
  }
  [IO.File]::WriteAllText($target, ($report | ConvertTo-Json -Depth 8))
  Write-Output "Inventoried $($commands.Count) commands and $($dialogs.Count) window flags at $($revision.Trim())."
  Write-Output $target
} finally {
  Pop-Location
}
