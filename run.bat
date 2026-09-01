@echo off
setlocal enabledelayedexpansion

rem Run GoSurvey. Run from anywhere; launches the executable built by build.bat.
rem
rem   run.bat                 run whichever build is present (newer one if both)
rem   run.bat release         force the release build  (build\GoSurvey.exe)
rem   run.bat debug           force the debug build     (build\debug\GoSurvey.exe)
rem   run.bat [target] ARGS   forward ARGS to GoSurvey.exe
rem
rem This does not build. Use build.bat first (build.bat configures/builds the
rem ninja-release preset; the debug tree comes from `cmake --preset ninja-debug`).

cd /d "%~dp0"

set "REL=build\GoSurvey.exe"
set "DBG=build\debug\GoSurvey.exe"

set "TARGET="
if /i "%~1"=="release" set "TARGET=rel"
if /i "%~1"=="-r"      set "TARGET=rel"
if /i "%~1"=="--release" set "TARGET=rel"
if /i "%~1"=="debug"  set "TARGET=dbg"
if /i "%~1"=="-d"     set "TARGET=dbg"
if /i "%~1"=="--debug" set "TARGET=dbg"
if defined TARGET shift

rem Collect any remaining arguments to forward to the executable.
set "APPARGS="
:collect
if "%~1"=="" goto :resolved
set "APPARGS=!APPARGS! %1"
shift
goto :collect

:resolved

if "%TARGET%"=="rel" (
  if not exist "%REL%" goto :norel
  set "EXE=%REL%"
  goto :launch
)
if "%TARGET%"=="dbg" (
  if not exist "%DBG%" goto :nodbg
  set "EXE=%DBG%"
  goto :launch
)

rem Auto-detect: whichever exists; if both, the more recently built one.
if exist "%REL%" if exist "%DBG%" (
  for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "if ((Get-Item '%DBG%').LastWriteTime -gt (Get-Item '%REL%').LastWriteTime) { 'dbg' } else { 'rel' }"`) do set "NEWER=%%i"
  if "!NEWER!"=="dbg" ( set "EXE=%DBG%" ) else ( set "EXE=%REL%" )
  goto :launch
)
if exist "%REL%" ( set "EXE=%REL%" & goto :launch )
if exist "%DBG%" ( set "EXE=%DBG%" & goto :launch )

echo ERROR: no GoSurvey.exe found.
echo   Expected "%CD%\%REL%" or "%CD%\%DBG%".
echo   Build it first:  build.bat
exit /b 1

:launch
echo Running: %CD%\%EXE%
"%EXE%"%APPARGS%
exit /b %errorlevel%

:norel
echo ERROR: release build not found at "%CD%\%REL%".
echo   Build it first:  build.bat
exit /b 1

:nodbg
echo ERROR: debug build not found at "%CD%\%DBG%".
echo   Configure and build it first:  cmake --preset ninja-debug ^&^& cmake --build build\debug
exit /b 1
