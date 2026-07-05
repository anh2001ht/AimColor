@echo off
setlocal
cd /d "%~dp0"

start "No-Arduino AutoClicker" "%~dp0start_autoclicker.bat"
timeout /t 1 /nobreak >nul
start "Color Click Tester" "%~dp0start_click_tester.bat"
