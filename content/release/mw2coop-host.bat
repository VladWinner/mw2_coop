@echo off
rem mw2coop: start a two-player campaign as the HOST.
rem Put this file next to iw4sp.exe (the game folder), with winmm.dll and the mw2coop folder.
rem Run it, the game starts through Steam; your partner then runs mw2coop-join.bat.
rem MISSION: the mission to start (trainer = S.S.D.D., the first one).
rem GOD: 1 turns on the test god mode (nobody takes damage), 0 plays normally.
setlocal
set "MISSION=trainer"
set "GOD=0"
cd /d "%~dp0"
if not exist "iw4sp.exe" goto nogame
if not exist "mw2coop\scripts" goto nomod
(
echo # written by mw2coop-host.bat
echo !sleep 20000
echo set mw2coop_god %GOD%
echo set coop_mapName %MISSION%
echo listen
echo !sleep 2000
echo coopLaunch
) > "mw2coop\commands.txt"
echo mw2coop: hosting %MISSION%. The game starts now; give it about 20 seconds.
echo Your partner can run mw2coop-join.bat once your game shows the main menu.
echo Keep this window open for 2 minutes (it then clears the start commands).
rem MW2COOP_NO_LAUNCH=1 (testing): only write the commands, start the game yourself
if defined MW2COOP_NO_LAUNCH exit /b 0
start "" "steam://rungameid/10180"
timeout /t 120 /nobreak >nul
type nul > "mw2coop\commands.txt"
exit /b 0

:nogame
echo iw4sp.exe was not found next to this file. Put the mw2coop files into the game folder.
pause
exit /b 1

:nomod
echo The mw2coop folder is missing next to this file.
pause
exit /b 1
