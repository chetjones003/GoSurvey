#Requires -Version 7.2
# Integration regression: multiple installed tools must resolve to one executable;
# safe changed code passes, and a real diagnostic on changed lines fails.
param([string]$ArtifactDirectory = 'build/artifacts/clang-tidy-runner-test')
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Location).Path
$realTool = (Get-Command clang-tidy -CommandType Application | Select-Object -First 1).Source
$fixture = Join-Path $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArtifactDirectory) ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $fixture, "$fixture/src", "$fixture/build" | Out-Null
$source = Join-Path $fixture 'src/changed.cpp'
Set-Content -LiteralPath (Join-Path $fixture '.clang-tidy') -Value "Checks: '-*,clang-analyzer-core.DivideZero'"
Set-Content -LiteralPath $source -Value 'int main() { return 0; }'
function Invoke-FixtureGit {
  param([string[]]$Arguments)
  & git -C $fixture @Arguments
  if ($LASTEXITCODE -ne 0) { throw 'Fixture git command failed.' }
}
Invoke-FixtureGit @('init', '--quiet')
Invoke-FixtureGit @('add', 'src/changed.cpp')
Invoke-FixtureGit @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '--quiet', '-m', 'baseline')
$base = Invoke-FixtureGit @('rev-parse', 'HEAD')
$command = @{ directory = $fixture; file = $source; command = 'clang++ -std=c++17 -c src/changed.cpp' }
ConvertTo-Json -InputObject @($command) | Set-Content -LiteralPath (Join-Path $fixture 'build/compile_commands.json')
# Stub only discovery; every analyzer invocation below executes the real LLVM tool.
function Get-Command {
  param([string]$Name, [string]$CommandType)
  if ($Name -ne 'clang-tidy' -or $CommandType -ne 'Application') { throw 'Unexpected tool lookup.' }
  [pscustomobject]@{ Source = $realTool }
  [pscustomobject]@{ Source = 'C:\second-installation\clang-tidy.exe' }
}
Push-Location -LiteralPath $fixture
try {
  Set-Content -LiteralPath $source -Value 'int main() { return 1; }'
  Invoke-FixtureGit @('add', 'src/changed.cpp')
  Invoke-FixtureGit @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '--quiet', '-m', 'safe')
  & (Join-Path $repoRoot 'tools/run-clang-tidy-diff.ps1') -BaseSha $base -BuildDirectory build -OutputDirectory pass -Jobs 1
  $scope = Get-Content -LiteralPath pass/scope.json -Raw | ConvertFrom-Json
  if ($scope.tool -ne $realTool -or $scope.files.Count -ne 1 -or $scope.results[0].exitCode -ne 0) {
    throw 'Safe changed code did not pass using the first tool.'
  }
  Set-Content -LiteralPath $source -Value 'int main() { int zero = 0; return 10 / zero; }'
  Invoke-FixtureGit @('add', 'src/changed.cpp')
  Invoke-FixtureGit @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '--quiet', '-m', 'diagnostic')
  $failed = $false
  try {
    & (Join-Path $repoRoot 'tools/run-clang-tidy-diff.ps1') -BaseSha $base -BuildDirectory build -OutputDirectory fail -Jobs 1
  } catch { $failed = $_.Exception.Message -match 'clang-tidy failed' }
  if (-not $failed) { throw 'A real changed-line diagnostic did not fail the check.' }
  Write-Host 'PASS: multiple installations select one tool; safe code passes and a real diagnostic fails.'
} finally { Pop-Location }
