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
rem   build.bat vs                         configure + build Release via VS solution
rem   build.bat vs debug                   configure + build Debug via VS solution
rem   build.bat vs --configure-only        generate GoSurvey.sln only (no compile)
rem
rem The first argument, when it is release / debug / vs (or -r / -d, --release /
rem --debug), selects the configuration. Anything after it is forwarded verbatim
rem to `cmake --build`. With no config argument the build is RELEASE, matching
rem the historical behaviour of this script.
rem
rem   release  ->  preset ninja-release  ->  build\         (CMAKE_BUILD_TYPE=Release)
rem   debug    ->  preset ninja-debug    ->  build\debug\   (CMAKE_BUILD_TYPE=Debug,
rem                                                          Developer Shell on)
rem   vs       ->  preset vs             ->  build\vs\      (GoSurvey.sln, multi-config)
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
set "VSMULTI=0"
set "VSCONFIG=Release"
set "VSCONFIGUREONLY=0"

if /i "%~1"=="release"   ( set "CONSUMED=1" )
if /i "%~1"=="-r"        ( set "CONSUMED=1" )
if /i "%~1"=="--release" ( set "CONSUMED=1" )
if /i "%~1"=="debug"     ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\debug" & set "CFGNAME=debug" )
if /i "%~1"=="-d"        ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\debug" & set "CFGNAME=debug" )
if /i "%~1"=="--debug"   ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\debug" & set "CFGNAME=debug" )
if /i "%~1"=="vs"        ( set "CONSUMED=1" & set "PRESET=vs" & set "BUILDDIR=build\vs" & set "CFGNAME=vs" & set "VSMULTI=1" )
if /i "%~1"=="vs2022"   ( set "CONSUMED=1" & set "PRESET=vs2022" & set "BUILDDIR=build\vs" & set "CFGNAME=vs2022" & set "VSMULTI=1" )

if "%CONSUMED%"=="1" shift

if "%VSMULTI%"=="1" (
  if /i "%~1"=="debug"   ( set "VSCONFIG=Debug" & set "CFGNAME=vs-debug" & shift )
  if /i "%~1"=="release" ( set "VSCONFIG=Release" & set "CFGNAME=vs-release" & shift )
  if /i "%~1"=="--configure-only" ( set "VSCONFIGUREONLY=1" & shift )
)

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

rem --- stale toolchain guard ---------------------------------------------
rem A Visual Studio update can remove an older MSVC folder while build.ninja
rem still points at it, which makes ninja fail with "CreateProcess failed".
if exist "%BUILDDIR%\CMakeCache.txt" (
  set "CACHED_LINKER="
  for /f "usebackq tokens=2 delims==" %%p in (`findstr /B /C:"CMAKE_LINKER:FILEPATH=" "%BUILDDIR%\CMakeCache.txt"`) do set "CACHED_LINKER=%%p"
  if defined CACHED_LINKER if not exist "!CACHED_LINKER!" (
    echo.
    echo Stale MSVC toolchain in %BUILDDIR%:
    echo   !CACHED_LINKER!
    echo Removing %BUILDDIR% and re-configuring...
    rmdir /s /q "%BUILDDIR%"
  )
)

rem Ensure cl.exe is on PATH before configure. Developer shells set DevEnvDir
rem and skip vcvars above, but a fresh terminal or a broken VS update may not.
where cl >nul 2>&1
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "%VSWHERE%" (
    set "VCVARS="
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
      set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
    if defined VCVARS if exist "!VCVARS!" call "!VCVARS!" >nul
  )
)
where cl >nul 2>&1
if errorlevel 1 (
  echo ERROR: cl.exe not found. Open "x64 Native Tools Command Prompt for VS" or install the C++ build tools.
  exit /b 1
)

rem --- configure (only when the build tree is not fully generated) -----
rem Keyed on build.ninja, not CMakeCache.txt: a failed configure leaves a
rem partial CMakeCache.txt behind, but build.ninja appears only after generate
rem succeeds, so this re-runs configure after an earlier failure.
rem
rem `exit /b` is kept OUT of parenthesised blocks on purpose: inside `( )` it
rem does not reliably propagate the failure out of the script.
set "NEEDCONFIG=0"
if "%VSMULTI%"=="1" (
  if not exist "%BUILDDIR%\GoSurvey.sln" set "NEEDCONFIG=1"
) else (
  if not exist "%BUILDDIR%\build.ninja" set "NEEDCONFIG=1"
)
if "%NEEDCONFIG%"=="1" cmake --preset %PRESET%
if "%NEEDCONFIG%"=="1" if errorlevel 1 (
  if /i "%PRESET%"=="vs" (
    echo.
    echo VS 2026 configure failed — retrying with the VS 2022 generator preset...
    set "PRESET=vs2022"
    cmake --preset vs2022
  )
)
if "%NEEDCONFIG%"=="1" if errorlevel 1 exit /b 1

if "%VSMULTI%"=="1" (
  echo.
  echo Visual Studio solution: %CD%\%BUILDDIR%\GoSurvey.sln
)

if "%VSCONFIGUREONLY%"=="1" exit /b 0

rem --- build ----------------------------------------------------------
if "%VSMULTI%"=="1" (
  cmake --build "%BUILDDIR%" --config %VSCONFIG%%FWDARGS%
) else (
  cmake --build "%BUILDDIR%"%FWDARGS%
)
if errorlevel 1 exit /b 1

echo.
if "%VSMULTI%"=="1" (
  echo Built (%CFGNAME%, %VSCONFIG%): %CD%\%BUILDDIR%\%VSCONFIG%\GoSurvey.exe
) else (
  echo Built (%CFGNAME%): %CD%\%BUILDDIR%\GoSurvey.exe
)
exit /b 0

:novcvars
echo ERROR: could not locate vcvars64.bat - install the MSVC C++ build tools.
exit /b 1
