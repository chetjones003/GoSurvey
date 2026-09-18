@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
where gh >nul 2>&1
if errorlevel 1 (
  echo ERROR: gh ^(GitHub CLI^) was not found.
  echo Install it:  winget install --id GitHub.cli
  exit /b 1
)
if "%~1"=="" (
  gh issue list
  exit /b %ERRORLEVEL%
)
gh issue %*
exit /b %ERRORLEVEL%
