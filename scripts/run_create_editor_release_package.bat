@echo off
setlocal

where pwsh.exe >nul 2>nul
if %errorlevel%==0 (
    pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0create_editor_release_package.ps1" %*
) else (
    powershell.exe -ExecutionPolicy Bypass -File "%~dp0create_editor_release_package.ps1" %*
)

pause
