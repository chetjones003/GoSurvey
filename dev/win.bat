@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
if "%~1"=="" (
  echo usage: ./dev/win ^<windows command ...^>   ^(or ps ^<powershell ...^>^)
  exit /b 1
)
if /i "%~1"=="--ps" goto :powershell
if /i "%~1"=="/ps" goto :powershell
if /i "%~1"=="ps" goto :powershell
%*
exit /b %ERRORLEVEL%

:powershell
shift
powershell -NoProfile -Command %*
exit /b %ERRORLEVEL%
