@echo off
setlocal enableextensions
call "%~dp0_common.bat"
cd /d "%REPO_ROOT%"

set "CODEQL_EXE=%LOCALAPPDATA%\codeql-cli\codeql\codeql.exe"
set "CODEQL_DB=.codeql-db"
set "SARIF_RAW=codeql-results.sarif"
set "SARIF_FILTERED=codeql-results-filtered.sarif"
set "SUITE=codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls"

if not exist "%CODEQL_EXE%" (
  echo ERROR: CodeQL CLI not found at %CODEQL_EXE%
  echo Download codeql-bundle-win64.tar.gz from https://github.com/github/codeql-action/releases
  echo and extract it to %%LOCALAPPDATA%%\codeql-cli
  exit /b 1
)

echo [dev/codeql] full clean rebuild + CodeQL database create + analyze -- this takes 1-3 hours.
if exist "%CODEQL_DB%" rmdir /s /q "%CODEQL_DB%"
if exist build rmdir /s /q build

echo [dev/codeql] creating database (full build under the CodeQL tracer) ...
"%CODEQL_EXE%" database create "%CODEQL_DB%" --language=cpp --source-root=. --command="call build.bat" --overwrite
if errorlevel 1 exit /b %ERRORLEVEL%

echo [dev/codeql] running cpp-security-and-quality ...
"%CODEQL_EXE%" database analyze "%CODEQL_DB%" --format=sarifv2.1.0 --output="%SARIF_RAW%" --threads=0 %SUITE%
if errorlevel 1 exit /b %ERRORLEVEL%

echo [dev/codeql] filtering results (excludes third_party/build/samples) ...
powershell -NoProfile -ExecutionPolicy Bypass -File dev\codeql-filter.ps1 -InputSarif "%SARIF_RAW%" -OutputSarif "%SARIF_FILTERED%"

echo [dev/codeql] done. Open %SARIF_FILTERED% in VS Code (SARIF Viewer extension).
exit /b 0
