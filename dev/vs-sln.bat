@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
echo [dev] generating Visual Studio solution (build/vs/GoSurvey.sln) ...
call "%REPO_ROOT%\build.bat" vs %*
exit /b %ERRORLEVEL%
