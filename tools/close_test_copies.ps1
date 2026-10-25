# Closes the game copies left from an earlier test run (a crashed copy keeps its window open, and a running copy
# holds its winmm.dll, its log and the game port). Only processes that live in the test root are touched: the
# owner's own game and any other iw4sp.exe are never closed here (run_test.ps1 watches for those).
# Usage: powershell -File close_test_copies.ps1 -Root D:\coop-test      Prints the ids it closed, one line.
param([string]$Root = 'D:\coop-test')
$closed = @()
Get-Process iw4sp,iw4x-sp -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Root.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase) } | ForEach-Object {
	Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
	$closed += $_.Id
}
if ($closed.Count) {
	Start-Sleep -Seconds 3   # the files and the port are free a moment after the process is gone
	"closed test copies from an earlier run: " + ($closed -join ' ')
}
