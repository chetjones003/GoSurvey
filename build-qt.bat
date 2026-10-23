@echo off
rem Compatibility entry point; all configuration is owned by build.bat.
call "%~dp0build.bat" %*
exit /b %errorlevel%
