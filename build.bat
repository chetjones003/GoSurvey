@echo off
setlocal

rem Build GoSurvey. Run from anywhere; builds into <repo>\build.
rem
rem A bare `cmake --build build` fails with a bogus "cannot open source file
rem <vector>" unless the MSVC environment is loaded first, so this script sources
rem vcvars64.bat before doing anything else.

cd /d "%~dp0"

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

rem Configure when the cache is missing, or when Ninja's rules file is gone.
rem A failed cmake run (for example without vcvars) can leave CMakeCache.txt
rem while wiping CMakeFiles\rules.ninja; ninja then errors on include.
if exist "build\CMakeCache.txt" if not exist "build\CMakeFiles\rules.ninja" (
  echo WARNING: incomplete Ninja build tree; reconfiguring.
  del /q "build\CMakeCache.txt" >nul 2>&1
)
if not exist "build\CMakeCache.txt" (
  cmake --preset ninja-release
  if errorlevel 1 exit /b 1
)

cmake --build build %*
if errorlevel 1 exit /b 1

echo.
echo Built: %CD%\build\GoSurvey.exe
exit /b 0

:novcvars
echo ERROR: could not locate vcvars64.bat - install the MSVC C++ build tools.
exit /b 1
