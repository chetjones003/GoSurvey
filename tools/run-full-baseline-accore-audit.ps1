# Runs accoreconsole AUDIT on the full Rapides mapped R2018 export.
# Prerequisite:
#   $env:GOSURVEY_ORACLE_DIR = '<worktree>\build\artifacts\full-baseline-audit'
#   GoSurveyTests.exe '[req479][full-baseline-audit]'
param(
  [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\artifacts\full-baseline-audit'),
  [string]$AccoreConsole = ''
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'run-mleader-accore-audit.ps1') -ArtifactDirectory $ArtifactDirectory -AccoreConsole $AccoreConsole
