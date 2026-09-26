@echo off
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0config.ps1" %*
exit /b %ERRORLEVEL%
