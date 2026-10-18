@echo off
setlocal enabledelayedexpansion

rem Unified MSVC build. build.bat [all|imgui|qt] [release|debug|vs] [cmake build arguments]
rem Default: both frontends and tests, Release. Qt kit: GOSURVEY_QT_ROOT.
rem Applications: build\ (Release), build\debug\ (Debug), build\Release\ (VS).
rem Intermediates: build\.cmake\. Screenshots/reports: build\artifacts\.

cd /d "%~dp0"

rem Frontend selector: all (default), imgui, or qt, followed by release/debug/vs.
set "FRONTEND=all"
if /i "%~1"=="imgui" (set "FRONTEND=imgui" & shift)
if /i "%~1"=="qt" (set "FRONTEND=qt" & shift)
if /i "%~1"=="all" shift

rem --- configuration selector ---------------------------------------------
rem Consume the first arg if it names a configuration; forward the rest to
rem `cmake --build` (note: %* ignores `shift`, so the passthrough is rebuilt
rem by hand below).
set "PRESET=ninja-release"
set "BUILDDIR=build\.cmake\release"
set "CFGNAME=release"
set "CONSUMED=0"
set "VSMULTI=0"
set "VSCONFIG=Release"
set "VSCONFIGUREONLY=0"

if /i "%~1"=="release"   ( set "CONSUMED=1" )
if /i "%~1"=="-r"        ( set "CONSUMED=1" )
if /i "%~1"=="--release" ( set "CONSUMED=1" )
if /i "%~1"=="debug"     ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\.cmake\debug" & set "CFGNAME=debug" )
if /i "%~1"=="-d"        ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\.cmake\debug" & set "CFGNAME=debug" )
if /i "%~1"=="--debug"   ( set "CONSUMED=1" & set "PRESET=ninja-debug" & set "BUILDDIR=build\.cmake\debug" & set "CFGNAME=debug" )
if /i "%~1"=="vs"        ( set "CONSUMED=1" & set "PRESET=vs" & set "BUILDDIR=build\.cmake\vs" & set "CFGNAME=vs" & set "VSMULTI=1" )
if /i "%~1"=="vs2022"   ( set "CONSUMED=1" & set "PRESET=vs2022" & set "BUILDDIR=build\.cmake\vs2022" & set "CFGNAME=vs2022" & set "VSMULTI=1" )

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

rem Reconfigure on every invocation so frontend and Qt-kit selection are honored.
set "QTARGS=-DGOSURVEY_BUILD_QT=OFF -DGOSURVEY_BUILD_TESTS=ON"
if /i not "%FRONTEND%"=="imgui" (
  if not defined GOSURVEY_QT_ROOT set "GOSURVEY_QT_ROOT=C:\Qt\6.8.3\msvc2022_64"
  if not exist "!GOSURVEY_QT_ROOT!\lib\cmake\Qt6\Qt6Config.cmake" (
    echo ERROR: Qt kit not found at "!GOSURVEY_QT_ROOT!". Set GOSURVEY_QT_ROOT to an installed MSVC x64 Qt kit.
    exit /b 1
  )
  set "QTARGS=-DGOSURVEY_BUILD_TESTS=ON -DGOSURVEY_BUILD_QT=ON "-DCMAKE_PREFIX_PATH=!GOSURVEY_QT_ROOT!" -UQt6*_DIR -UGOSURVEY_WINDEPLOYQT"
)
cmake --preset %PRESET% %QTARGS%
if errorlevel 1 exit /b 1
if /i "%FRONTEND%"=="imgui" set "FWDARGS= --target GoSurvey GoSurveyTestImGui %FWDARGS%"
if /i "%FRONTEND%"=="qt" set "FWDARGS= --target GoSurveyQt GoSurveyQtTests %FWDARGS%"

rem --- fixup VS CMake path quoting for Ninja POST_BUILD (space in Program Files) ---
if exist "%BUILDDIR%\build.ninja" (
  where python >nul 2>&1 && python "%~dp0cmake\fix-ninja-quoting.py" "%BUILDDIR%\build.ninja" >nul 2>&1
)

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

set "RUNTIMEDIR=%CD%\build"
if /i "%CFGNAME%"=="debug" set "RUNTIMEDIR=%CD%\build\debug"
if "%VSMULTI%"=="1" set "RUNTIMEDIR=%CD%\build\%VSCONFIG%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tidy-build-output.ps1" -RuntimeDirectory "%RUNTIMEDIR%"
if errorlevel 1 exit /b 1

echo.
echo Built ^(%CFGNAME%^). Applications: %RUNTIMEDIR%
exit /b 0

:novcvars
echo ERROR: could not locate vcvars64.bat - install the MSVC C++ build tools.
exit /b 1
