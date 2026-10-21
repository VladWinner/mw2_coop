@echo off
rem mw2coop: let the game receive co-op traffic through Windows Firewall (needed by the host,
rem and by anyone joined through the network list or an address). Asks for administrator
rem rights only if a rule is missing or a block rule is in the way. Run it once.
setlocal
cd /d "%~dp0"
if not exist "iw4sp.exe" (
	echo iw4sp.exe was not found next to this file. Put the mw2coop files into the game folder.
	pause
	exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mw2coop\mw2coop-firewall.ps1" -Exe "%~dp0iw4sp.exe"
pause
