@echo off
setlocal enableextensions enabledelayedexpansion
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"
echo repo root      : %REPO_ROOT%
echo shell env      : windows
for /f "delims=" %%B in ('git rev-parse --abbrev-ref HEAD 2^>nul') do echo branch         : %%B
for /f "delims=" %%U in ('git rev-parse --abbrev-ref --symbolic-full-name "@{u}" 2^>nul') do (
  set "UPSTREAM=%%U"
  for /f "tokens=1,2" %%A in ('git rev-list --left-right --count "%%U...HEAD" 2^>nul') do (
    echo upstream       : %%U ^(%%B ahead, %%A behind^)
  )
)
echo last commit    :
git -c color.ui=never log -1 --pretty=format:"%%h %%s (%%cr)"
echo.
echo.
git diff --quiet 2>nul && git diff --cached --quiet 2>nul
if errorlevel 1 (
  echo working tree   : dirty
  echo.
  echo modified / staged:
  git status --short --branch
) else (
  echo working tree   : clean
)
echo.
if exist "build\CMakeCache.txt" (
  echo build tree     : configured ^(%REPO_ROOT%\build^)
) else (
  echo build tree     : not configured — run ./dev/build
)
exit /b 0
