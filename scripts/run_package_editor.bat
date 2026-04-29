@echo off
where pwsh.exe >nul 2>nul
if %errorlevel%==0 (
    pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_editor.ps1"
) else (
    powershell.exe -ExecutionPolicy Bypass -File "%~dp0package_editor.ps1"
)
pause
