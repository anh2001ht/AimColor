@echo off
setlocal
cd /d "%~dp0"

python --version >nul 2>nul
if %errorlevel%==0 (
    python autoclicker.py %*
    goto :done
)

py -3 --version >nul 2>nul
if %errorlevel%==0 (
    py -3 autoclicker.py %*
    goto :done
)

echo Python was not found in PATH for this launcher.
echo Falling back to PowerShell version.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0autoclicker.ps1" %*

:done
echo.
echo Closed. Press any key to exit this window.
pause >nul
