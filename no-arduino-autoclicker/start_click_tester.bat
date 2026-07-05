@echo off
setlocal
cd /d "%~dp0"

py -3 --version >nul 2>nul
if %errorlevel%==0 (
    py -3 color_click_tester.py
    goto :done
)

python --version >nul 2>nul
if %errorlevel%==0 (
    python color_click_tester.py
    goto :done
)

echo Python was not found in PATH.
echo Falling back to PowerShell tester.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0color_click_tester.ps1"

:done
echo.
echo Closed. Press any key to exit this window.
pause >nul
