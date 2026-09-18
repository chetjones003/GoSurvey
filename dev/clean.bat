@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
if /i "%~1"=="--all" (
  for %%D in (build build-debug build-release out) do (
    if exist "%%D\" (
      echo [dev] removing %%D/
      rmdir /s /q "%%D"
    ) else (
      echo [dev] %%D/ already absent
    )
  )
) else (
  if exist "build\" (
    echo [dev] removing build/
    rmdir /s /q build
  ) else (
    echo [dev] build/ already absent
  )
)
echo [dev] clean complete
exit /b 0
