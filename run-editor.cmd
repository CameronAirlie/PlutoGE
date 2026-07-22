@echo off
setlocal

where pwsh.exe >nul 2>nul
if %errorlevel% equ 0 (
    pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Run-Editor.ps1" %*
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Run-Editor.ps1" %*
)

set "PLUTO_EXIT_CODE=%errorlevel%"
if not "%PLUTO_EXIT_CODE%"=="0" (
    echo.
    echo PlutoGE build or launch failed with exit code %PLUTO_EXIT_CODE%.
)
exit /b %PLUTO_EXIT_CODE%
