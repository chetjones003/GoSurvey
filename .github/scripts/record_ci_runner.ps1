$ErrorActionPreference = 'Stop'
$outputDir = 'build/artifacts/ci'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
# Ninja prints help to stderr and exits 1; validate the version command separately.
$parallelism = ((& ninja -h 2>&1) | Select-String 'run N jobs').Line
$version = & ninja --version
if ($LASTEXITCODE -ne 0 -or -not $parallelism) { throw 'Could not inspect Ninja on this runner.' }
$runnerInfo = [ordered]@{
  logicalProcessors = [Environment]::ProcessorCount
  memoryGiB = [Math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
  ninjaVersion = $version
  ninjaDefaultParallelism = $parallelism.Trim()
}
$runnerInfo | ConvertTo-Json | Tee-Object -FilePath "$outputDir/runner.json"
