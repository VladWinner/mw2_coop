param(
    [string]$DirA = 'D:\coop-test\mw2-iw4x',
    [string]$DirB = 'D:\coop-test\mw2-iw4x-b',
    [string]$Wait = 'facing each other',   # a line of the QA log: both copies must have it before the first frame
    [string]$OutDir = 'D:\coop-test\shots-iw4x',
    [string]$Prefix = 'shot',
    [double[]]$After = @(1.2, 4.5),        # seconds after that line, one frame of each copy per entry
    [int]$Timeout = 240
)
# Saves frames of the two game windows of a QA pair while a scenario runs (tools/shot_game.ps1 does the capture of one window).
# The copies are found by the folder of their exe (iw4x-sp.exe or iw4sp.exe). Start qa_pair.sh first, in another shell.
New-Item -ItemType Directory -Force $OutDir | Out-Null
$logs = @("$DirA\mw2coop\mw2coop.log", "$DirB\mw2coop\mw2coop.log")
$deadline = (Get-Date).AddSeconds($Timeout)
$begin = Get-Date
function Has($log) { (Test-Path $log) -and ((Get-Item $log).LastWriteTime -gt $begin) -and (Select-String -Path $log -Pattern $Wait -SimpleMatch -Quiet) }
while ((Get-Date) -lt $deadline -and -not ((Has $logs[0]) -and (Has $logs[1]))) { Start-Sleep -Milliseconds 300 }
$t0 = Get-Date
$shot = Join-Path $PSScriptRoot 'shot_game.ps1'
foreach ($a in $After) {
    while (((Get-Date) - $t0).TotalSeconds -lt $a) { Start-Sleep -Milliseconds 50 }
    $i = 0
    foreach ($dir in @($DirA, $DirB)) {
        $i++
        $name = if ($i -eq 1) { 'host' } else { 'client' }
        $p = Get-Process iw4x-sp, iw4sp -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($dir + '\', [System.StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
        if ($p) { & $shot -Out (Join-Path $OutDir ("$Prefix-$name-$a.png")) -TargetPid $p.Id } else { "no process in $dir" }
    }
}
