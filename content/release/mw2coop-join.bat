@echo off
rem mw2coop: join a friend's two-player campaign (the HOST runs mw2coop-host.bat first).
rem Put this file next to iw4sp.exe (the game folder), with winmm.dll and the mw2coop folder.
setlocal
cd /d "%~dp0"
if not exist "iw4sp.exe" goto nogame
if not exist "mw2coop\scripts" goto nomod
set "HOST=%~1"
if "%HOST%"=="" set /p "HOST=Host IP address: "
if "%HOST%"=="" goto nohost
rem port 28960; the game's connect command expects it byte-swapped (8305)
(
echo # written by mw2coop-join.bat
echo !sleep 25000
echo mw2coop_party_join
echo !sleep 1000
echo connect %HOST% 8305
) > "mw2coop\commands.txt"
echo mw2coop: joining %HOST%. The game starts now and connects after about 25 seconds.
echo Keep this window open for 2 minutes (it then clears the start commands).
rem MW2COOP_NO_LAUNCH=1 (testing): only write the commands, start the game yourself
if defined MW2COOP_NO_LAUNCH exit /b 0
start "" "steam://rungameid/10180"
timeout /t 120 /nobreak >nul
type nul > "mw2coop\commands.txt"
exit /b 0

:nohost
echo No address given.
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
