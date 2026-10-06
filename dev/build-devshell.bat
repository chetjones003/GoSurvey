@echo off
rem TASK-272 scratch: a RelWithDebInfo tree with the Developer Shell (REQ-161) ON.
rem
rem The ninja-debug preset cannot link here: third_party/xerces-c ships a RELEASE-only
rem prebuilt (.lib built /MD), so a /MDd Debug link fails with ~500 LNK2038 runtime-library
rem mismatches. RelWithDebInfo uses the release runtime, so it links, and CMakeLists only
rem forces the Developer Shell off for CMAKE_BUILD_TYPE=Release exactly.
setlocal
cd /d "%~dp0.."
if not defined VSINSTALLDIR (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)
if not exist "build\devshell\build.ninja" (
  cmake -S . -B build\devshell -G Ninja -D CMAKE_BUILD_TYPE=RelWithDebInfo ^
        -D CMAKE_C_COMPILER=cl -D CMAKE_CXX_COMPILER=cl -D GOSURVEY_DEVELOPER_SHELL=ON
  if errorlevel 1 exit /b 1
)
cmake --build build\devshell %*
exit /b %errorlevel%
