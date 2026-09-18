@echo off
rem Shared helpers for dev/*.bat (native Windows entry points).
set "DEV_DIR=%~dp0"
for %%I in ("%DEV_DIR%..") do set "REPO_ROOT=%%~fI"
exit /b 0
