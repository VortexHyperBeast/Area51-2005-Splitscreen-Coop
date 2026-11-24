@echo off
setlocal
cd /d "%~dp0"
title Area 51 co-op - settings

echo.
echo  AREA 51 CO-OP SETTINGS
echo  ======================
echo  These are used the next time you start PLAY-COOP-2-PLAYERS.cmd.
echo.
echo  1. CONTROLLERS
echo     Normally the first controller Windows sees is player 1 (top),
echo     the second one is player 2 (bottom).
choice /C NY /N /M "     Swap them, so the second controller plays player 1? [N/Y] "
set SWAP=0
if errorlevel 2 set SWAP=1

echo.
echo  2. CHARACTER (what the other player sees of you)
echo      0-3   hazmat suit, colours 1 to 4
echo      4-7   special forces, colours 1 to 4
echo      8-11  technician, colours 1 to 4
echo     12-15  grey alien, colours 1 to 4
echo.
set P1=0
set P2=1
set /p P1="     Player 1 character number [0]: "
set /p P2="     Player 2 character number [1]: "

> "%~dp0coop-settings.cmd" echo rem Written by CO-OP-SETTINGS.cmd. Run that file to change these.
>>"%~dp0coop-settings.cmd" echo set A51_COOP_SWAP_PADS=%SWAP%
>>"%~dp0coop-settings.cmd" echo set A51_COOP_SKIN_P1=%P1%
>>"%~dp0coop-settings.cmd" echo set A51_COOP_SKIN_P2=%P2%

echo.
echo  Saved. Swap controllers: %SWAP%   Player 1: %P1%   Player 2: %P2%
echo  (A number outside 0-15 falls back to the default.)
echo  Start PLAY-COOP-2-PLAYERS.cmd to play.
echo.
pause
exit /b 0
