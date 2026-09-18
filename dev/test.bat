@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
echo [dev] building test targets + running ctest ...
call "%REPO_ROOT%\build.bat"
if errorlevel 1 exit /b %ERRORLEVEL%
ctest --test-dir build --output-on-failure --no-tests=error %*
exit /b %ERRORLEVEL%
