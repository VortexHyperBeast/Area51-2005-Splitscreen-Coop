@echo off
setlocal
cd /d "%~dp0"
title Area 51 - 2 player co-op (experimental)

rem First start only: puts the game data next to A51.exe (asks before downloading).
if exist "%~dp0.coop-assets-ok" goto launch
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-Assets.ps1"
if errorlevel 1 goto failed

:launch
echo.
echo Starting Area 51 for 2 players on this PC.
echo Connect both controllers before the game window opens.
set A51_LOCAL_COOP=2
start "" "%~dp0A51.exe"
exit /b 0

:failed
echo.
echo The setup did not finish. Read the message above, then start this file again.
pause
exit /b 1
