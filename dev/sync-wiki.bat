@echo off
setlocal enableextensions enabledelayedexpansion
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"

set "WIKI_TMP=%TEMP%\gosurvey-wiki-sync-%RANDOM%"
mkdir "%WIKI_TMP%" 2>nul
if errorlevel 1 (
  echo ERROR: could not create temp directory under %TEMP%
  exit /b 1
)

echo [dev] cloning GoSurvey wiki ...
git clone --depth 1 "https://github.com/chetjones003/GoSurvey.wiki.git" "%WIKI_TMP%\wiki"
if errorlevel 1 (
  rmdir /s /q "%WIKI_TMP%" 2>nul
  exit /b 1
)

if exist "resources\wiki\" rmdir /s /q "resources\wiki"
mkdir "resources\wiki"
mkdir "resources\wiki\images"

copy /y "%WIKI_TMP%\wiki\*.md" "resources\wiki\" >nul
if exist "%WIKI_TMP%\wiki\images\" (
  copy /y "%WIKI_TMP%\wiki\images\*" "resources\wiki\images\" >nul 2>&1
)

python "%REPO_ROOT%\dev\generate-wiki-commands.py"
set "RC=!ERRORLEVEL!"
rmdir /s /q "%WIKI_TMP%" 2>nul
if not "!RC!"=="0" exit /b !RC!

echo [dev] synced wiki to resources/wiki/
exit /b 0
