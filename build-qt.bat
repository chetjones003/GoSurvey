@echo off
setlocal
cd /d "%~dp0"
if defined VSINSTALLDIR goto configured
set "QT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%QT_VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%QT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:configured
rem Override with the root of another installed MSVC x64 Qt kit when needed.
if not defined GOSURVEY_QT_ROOT set "GOSURVEY_QT_ROOT=C:\Qt\6.8.3\msvc2022_64"
cmake --preset ninja-release -DGOSURVEY_BUILD_QT=ON "-DCMAKE_PREFIX_PATH=%GOSURVEY_QT_ROOT%"
if errorlevel 1 exit /b 1
cmake --build --preset ninja-release --target GoSurveyQt %*
exit /b %errorlevel%
