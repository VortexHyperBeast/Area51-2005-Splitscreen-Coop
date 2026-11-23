@echo off
setlocal
cd /d "%~dp0"
title Area 51 - normal one player game

rem Starts the same program with co-op switched off, to compare with the normal game.
if exist "%~dp0.coop-assets-ok" goto launch
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-Assets.ps1"
if errorlevel 1 goto failed

:launch
echo.
echo Starting Area 51 as a normal one player game.
set A51_LOCAL_COOP=
start "" "%~dp0A51.exe"
exit /b 0

:failed
echo.
echo The setup did not finish. Read the message above, then start this file again.
pause
exit /b 1
