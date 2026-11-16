param([ValidateSet('dev','build','run','test')][string]$Mode='dev')
$ErrorActionPreference='Stop'
$cargoDirectory=Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path (Join-Path $cargoDirectory 'cargo.exe')) { $env:PATH=$cargoDirectory+';'+$env:PATH }
if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) { throw 'Install the stable Rust MSVC toolchain from https://rustup.rs, then rerun this script.' }
Push-Location (Join-Path $PSScriptRoot 'prototypes\tauri-cad')
try {
    if (-not (Test-Path node_modules)) { & npm.cmd ci; if ($LASTEXITCODE) { throw 'npm ci failed.' } }
    switch ($Mode) {
        'dev' { & npm.cmd run desktop }
        'build' { & npm.cmd run desktop:build -- --debug }
        'test' { & npm.cmd run test:rust; if ($LASTEXITCODE) { throw 'Rust tests failed.' }; & npm.cmd run test:camera }
        'run' {
            $application=Join-Path $PSScriptRoot 'build\.cmake\tauri-spike\debug\GoSurveySpike.exe'
            if (-not (Test-Path -LiteralPath $application)) { throw 'Build first: .\run-tauri-spike.ps1 -Mode build' }
            Start-Process -FilePath $application -WorkingDirectory $PSScriptRoot
        }
    }
    if ($Mode -ne 'run' -and $LASTEXITCODE) { throw "Spike command failed with exit code $LASTEXITCODE." }
} finally { Pop-Location }
