@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
echo [dev] building GoSurvey (Windows MSVC/Ninja) ...
call "%REPO_ROOT%\build.bat" %*
exit /b %ERRORLEVEL%
