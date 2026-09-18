@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
echo [dev] launching GoSurvey ...
call "%REPO_ROOT%\run.bat" %*
exit /b %ERRORLEVEL%
