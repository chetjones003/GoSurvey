@echo off
REM issue #194 — launch GoSurvey with solid tessellation-cache logging on.
REM Run this, then in the app: BENCH SOLID 400, wait for it to finish, close the app.
REM The log lands at %APPDATA%\GoSurvey\solid-cache-debug.txt
set GOSURVEY_SOLID_CACHE_DEBUG=1
del "%APPDATA%\GoSurvey\solid-cache-debug.txt" 2>nul
"%~dp0..\build\GoSurvey.exe"
