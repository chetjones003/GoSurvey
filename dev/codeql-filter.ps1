# dev/codeql-filter.ps1 — post-process a raw CodeQL SARIF file the same way
# .github/codeql/codeql-config.yml's paths-ignore does in CI (third_party/, build/,
# samples/), since the CLI's `database analyze` has no equivalent of that Actions-only
# config. Prints a plain-text severity summary either way. See dev/codeql.
param(
  [Parameter(Mandatory = $true)][string]$InputSarif,
  [Parameter(Mandatory = $true)][string]$OutputSarif
)

$ErrorActionPreference = 'Stop'
$ignorePrefixes = @('third_party/', 'build/', 'samples/')

$sarif = Get-Content $InputSarif -Raw | ConvertFrom-Json

foreach ($run in $sarif.runs) {
  $kept = @()
  foreach ($result in $run.results) {
    $uri = $result.locations[0].physicalLocation.artifactLocation.uri
    $ignored = $false
    foreach ($prefix in $ignorePrefixes) {
      if ($uri -and $uri.StartsWith($prefix)) { $ignored = $true; break }
    }
    if (-not $ignored) { $kept += $result }
  }
  $run.results = $kept
}

$sarif | ConvertTo-Json -Depth 100 | Out-File -Encoding utf8 $OutputSarif

Write-Host ""
Write-Host "=== CodeQL findings (first-party only: third_party/build/samples excluded) ==="
$bySeverity = @{}
foreach ($run in $sarif.runs) {
  foreach ($result in $run.results) {
    $level = $result.level
    if (-not $level) {
      $rule = $run.tool.driver.rules | Where-Object { $_.id -eq $result.ruleId } | Select-Object -First 1
      $level = $rule.defaultConfiguration.level
    }
    if (-not $level) { $level = 'warning' }
    if (-not $bySeverity.ContainsKey($level)) { $bySeverity[$level] = 0 }
    $bySeverity[$level]++
  }
}
if ($bySeverity.Count -eq 0) {
  Write-Host "No findings."
} else {
  foreach ($level in $bySeverity.Keys | Sort-Object) {
    Write-Host "  $level : $($bySeverity[$level])"
  }
}
Write-Host ""
Write-Host "Full detail: $OutputSarif (open in VS Code with the SARIF Viewer extension,"
Write-Host "or any SARIF-aware viewer, to jump to each finding's exact file/line)."
