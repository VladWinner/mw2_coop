@echo off
rem mw2coop: carry on where you stopped, as the HOST (mission and checkpoint from
rem mw2coop\progress.txt). Your partner runs mw2coop-join.bat as usual.
rem GOD: 1 turns on the test god mode (nobody takes damage), 0 plays normally.
setlocal
set "GOD=0"
cd /d "%~dp0"
if not exist "iw4sp.exe" goto nogame
if not exist "mw2coop\scripts" goto nomod
if not exist "mw2coop\progress.txt" goto noprogress
(
echo # written by mw2coop-continue.bat
echo !sleep 20000
echo set mw2coop_god %GOD%
echo mw2coop_continue
echo listen
echo !sleep 2000
echo coopLaunch
) > "mw2coop\commands.txt"
echo mw2coop: continuing from mw2coop\progress.txt. The game starts now; give it about 20 seconds.
echo Your partner can run mw2coop-join.bat once your game shows the main menu.
echo Keep this window open for 2 minutes (it then clears the start commands).
rem MW2COOP_NO_LAUNCH=1 (testing): only write the commands, start the game yourself
if defined MW2COOP_NO_LAUNCH exit /b 0
start "" "steam://rungameid/10180"
timeout /t 120 /nobreak >nul
type nul > "mw2coop\commands.txt"
exit /b 0

:noprogress
echo No saved progress yet (mw2coop\progress.txt). Start with mw2coop-host.bat.
pause
exit /b 1

:nogame
echo iw4sp.exe was not found next to this file. Put the mw2coop files into the game folder.
pause
exit /b 1

:nomod
echo The mw2coop folder is missing next to this file.
pause
exit /b 1
