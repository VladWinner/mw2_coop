param(
    [string[]]$Dirs = @('D:\coop-test\mw2-64'),
    [int]$Timeout = 90,
    [string]$Tag = 'run',
    [int]$Stagger = 8,
    [string]$GameArgs = '',
    # Folder holding the test copies; any iw4sp.exe outside it is killed.
    [string]$TestRoot = $(if ($env:MW2COOP_TEST_ROOT) { $env:MW2COOP_TEST_ROOT } else { 'D:\coop-test' }),
    # Your real game install: reported if any file in it changes during a run.
    [string]$RealInstall = $env:MW2COOP_REAL_INSTALL,
    # Stop early (close the test copies) once every one of these logs has a line matching $DonePattern.
    [string]$DoneLogs = '',
    [string]$DonePattern = 'qa: [0-9]+: done'
)
# Launch the TEST copies of iw4sp.exe (one process per folder in -Dirs, $Stagger s apart).
# Guard: any iw4sp.exe whose path is not under $TestRoot (e.g. Steam relaunching your real
# install) is killed within ~200 ms.
# Without SteamAppId/SteamGameId, SteamStub exits with code 53 and Steam relaunches the REAL
# install with the same arguments (seen 2026-10-19: it rewrote players\config.cfg and left a
# __iw4sp lock file there).
# powershell -File passes "a,b" as one string
$Dirs = @($Dirs | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$env:SteamAppId = '10180'
$env:SteamGameId = '10180'
$root = $TestRoot.TrimEnd('\') + '\'
$start = Get-Date
$launched = @()
foreach ($Dir in $Dirs) {
    # The game writes its PID to <dir>\__iw4sp and deletes it on a clean quit; we may kill it.
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $Dir '__iw4sp')
    $exe = Join-Path $Dir 'iw4sp.exe'
    if ($GameArgs) { $p = Start-Process -FilePath $exe -ArgumentList $GameArgs -WorkingDirectory $Dir -PassThru }
    else { $p = Start-Process -FilePath $exe -WorkingDirectory $Dir -PassThru }
    $launched += $p
    "[$Tag] started pid $($p.Id) in $Dir at $((Get-Date).ToString('HH:mm:ss.fff'))"
    if ($Dir -ne $Dirs[-1]) {
        $until = (Get-Date).AddSeconds($Stagger)
        while ((Get-Date) -lt $until) {
            Start-Sleep -Milliseconds 200
            Get-CimInstance Win32_Process -Filter "name='iw4sp.exe'" | Where-Object { $_.ExecutablePath -and $_.ExecutablePath -notlike "$root*" } |
                ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "[$Tag] WRONG INSTALL RUNNING: $($_.ExecutablePath) -> killed" }
        }
    }
}
$deadline = $start.AddSeconds($Timeout)
$seen = @{}
$idleSince = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 200
    $procs = @(Get-CimInstance Win32_Process -Filter "name='iw4sp.exe'" -ErrorAction SilentlyContinue)
    foreach ($q in $procs) {
        if ($q.ExecutablePath -and ($q.ExecutablePath -notlike "$root*")) {
            Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue
            "[$Tag] WRONG INSTALL RUNNING (parent $($q.ParentProcessId)): $($q.ExecutablePath) -> killed"
        } elseif (-not $seen.ContainsKey($q.ProcessId)) {
            $seen[$q.ProcessId] = $true
            "[$Tag] pid $($q.ProcessId) running from $($q.ExecutablePath)"
        }
    }
    if ($DoneLogs) {
        $all = $true
        foreach ($log in ($DoneLogs -split ',')) {
            if (-not (Test-Path $log) -or -not (Select-String -Path $log -Pattern $DonePattern -Quiet)) { $all = $false; break }
        }
        if ($all) { Start-Sleep -Seconds 2; "[$Tag] scenario done on all copies, closing them"; break }
    }
    if ($procs.Count -eq 0) {
        if (-not $idleSince) { $idleSince = Get-Date }
        elseif (((Get-Date) - $idleSince).TotalSeconds -gt 6) { break }
    } else { $idleSince = $null }
}
$left = Get-Process iw4sp -ErrorAction SilentlyContinue
if ($left) { "[$Tag] timeout, killing $($left.Id -join ',')"; $left | Stop-Process -Force }
else { "[$Tag] all exited after $([int]((Get-Date) - $start).TotalSeconds) s; codes: $(($launched | ForEach-Object { $_.ExitCode }) -join ',')" }
if ($RealInstall) {
    $touched = Get-ChildItem -Force -Recurse -File $RealInstall | Where-Object { $_.LastWriteTime -ge $start.AddSeconds(-2) }
    if ($touched) { "[$Tag] !!! REAL INSTALL TOUCHED:"; $touched | ForEach-Object { "  $($_.FullName) $($_.Length)" } }
    else { "[$Tag] real install untouched" }
}
