@echo off
setlocal enabledelayedexpansion

rem ===========================================================================
rem Build GoSurvey. Run from anywhere; builds into <repo>\build[\debug].
rem
rem Usage:
rem   build.bat                    incremental RELEASE build (build\)
rem   build.bat release            same as above, explicit
rem   build.bat debug              incremental DEBUG build   (build\debug\)
rem   build.bat debug --clean-first          clean debug rebuild
rem   build.bat release --target GoSurveyTests
rem   build.bat debug -- -v                  verbose ninja output
rem
rem The first argument, when it is release / debug (or -r / -d, --release /
rem --debug), selects the configuration. Anything after it is forwarded verbatim
rem to `cmake --build`. With no config argument the build is RELEASE, matching
rem the historical behaviour of this script.
rem
rem   release  ->  preset ninja-release  ->  build\         (CMAKE_BUILD_TYPE=Release)
rem   debug    ->  preset ninja-debug    ->  build\debug\   (CMAKE_BUILD_TYPE=Debug,
rem                                                          Developer Shell on)
rem
rem NOTE: the ninja-debug preset turns on the Developer Shell (REQ-161), which
rem needs third_party/imgui_test_engine/ vendored. If it is absent the first
rem `build.bat debug` fails at configure with a message pointing at that dir's
rem VENDORED.md. To build Debug without the Developer Shell, configure the tree
rem once by hand, then `build.bat debug` reuses it:
rem     cmake --preset ninja-debug -D GOSURVEY_DEVELOPER_SHELL=OFF
rem
rem A bare `cmake --build` fails with a bogus "cannot open source file <vector>"
rem unless the MSVC environment is loaded first, so this script sources
rem vcvars64.bat before doing anything else.
rem ===========================================================================

cd /d "%~dp0"

rem --- configuration selector ---------------------------------------------
rem Consume the first arg if it names a configuration; forward the rest to
rem `cmake --build` (note: %* ignores `shift`, so the passthrough is rebuilt
rem by hand below).
set "PRESET=ninja-release"
set "BUILDDIR=build"
set "CFGNAME=release"
set "CONSUMED=0"

if /i "%~1"=="release"   ( set "CONSUMED=1" )
if /i "%~1"=="-r"        ( set "CONSUMED=1" )
if /i "%~1"=="--release" ( set "CONSUMED=1" )
if /i "%~1"=="debug"     ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\debug" & set "CFGNAME=debug" )
if /i "%~1"=="-d"        ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\debug" & set "CFGNAME=debug" )
if /i "%~1"=="--debug"   ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\debug" & set "CFGNAME=debug" )

if "%CONSUMED%"=="1" shift

set "FWDARGS="
:collect
if "%~1"=="" goto :collected
set "FWDARGS=!FWDARGS! %1"
shift
goto :collect
:collected

rem --- MSVC environment --------------------------------------------------
if defined VSINSTALLDIR goto :configured
if defined DevEnvDir goto :configured

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found - is Visual Studio installed?
  exit /b 1
)

set "VCVARS="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS goto :novcvars
if not exist "%VCVARS%" goto :novcvars

call "%VCVARS%" >nul
if errorlevel 1 (
  echo ERROR: vcvars64.bat failed.
  exit /b 1
)

:configured

rem --- configure (only when the build tree is not fully generated) -----
rem Keyed on build.ninja, not CMakeCache.txt: a failed configure leaves a
rem partial CMakeCache.txt behind, but build.ninja appears only after generate
rem succeeds, so this re-runs configure after an earlier failure.
rem
rem `exit /b` is kept OUT of parenthesised blocks on purpose: inside `( )` it
rem does not reliably propagate the failure out of the script.
set "NEEDCONFIG=0"
if not exist "%BUILDDIR%\build.ninja" set "NEEDCONFIG=1"
if "%NEEDCONFIG%"=="1" cmake --preset %PRESET%
if "%NEEDCONFIG%"=="1" if errorlevel 1 exit /b 1

rem --- build ----------------------------------------------------------
cmake --build "%BUILDDIR%"%FWDARGS%
if errorlevel 1 exit /b 1

echo.
echo Built (%CFGNAME%): %CD%\%BUILDDIR%\GoSurvey.exe
exit /b 0

:novcvars
echo ERROR: could not locate vcvars64.bat - install the MSVC C++ build tools.
exit /b 1
