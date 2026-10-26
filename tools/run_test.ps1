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
# The iw4x-sp client (MW2COOP_IW4X=1 in qa_pair.sh) starts iw4x-sp.exe, which maps the 1.0.159 iw4sp.exe inside itself.
$ExeName = $env:MW2COOP_EXE # empty: iw4x-sp.exe in a folder that has it, else iw4sp.exe
$Dirs = @($Dirs | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$env:SteamAppId = '10180'
$env:SteamGameId = '10180'
# The game's old steam_api64.dll finds Steam only through HKCU\Software\Valve\Steam\ActiveProcess
# (pid of a running steam.exe). The Steam client of 2026-10-21 leaves pid and ActiveUser at 0 there,
# so a copy started outside Steam quits at SteamAPI_Init (no window, exit 0). Put back what Steam
# itself used to write: its pid and the signed-in account (volatile key, gone after a reboot).
$ap = 'HKCU:\Software\Valve\Steam\ActiveProcess'
$steam = Get-Process steam -ErrorAction SilentlyContinue | Select-Object -First 1
$cur = (Get-ItemProperty $ap -ErrorAction SilentlyContinue).pid
if ($steam -and $cur -ne $steam.Id) {
    Set-ItemProperty $ap -Name pid -Value $steam.Id -Type DWord
    "[$Tag] Steam ActiveProcess pid $cur -> $($steam.Id)"
}
if (-not (Get-ItemProperty $ap -ErrorAction SilentlyContinue).ActiveUser) {
    $vdf = Join-Path (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath 'config\loginusers.vdf'
    # the account marked MostRecent: "7656...." { ... "MostRecent" "1" ... }
    $m = [regex]::Matches((Get-Content -Raw $vdf), '"(\d{17})"\s*\{([^}]*)\}') | Where-Object { $_.Groups[2].Value -match '"MostRecent"\s*"1"' } | Select-Object -First 1
    if ($m) {
        $acc = [uint64]$m.Groups[1].Value - 76561197960265728
        Set-ItemProperty $ap -Name ActiveUser -Value ([int]$acc) -Type DWord
        "[$Tag] Steam ActiveProcess ActiveUser set"
    }
}
# The test copies run on the discrete GPU (Windows "Graphics settings" preference "High performance" for each test exe; a per-user value that only names these files,
# HKCU\Software\Microsoft\DirectX\UserGpuPreferences). MW2COOP_TEST_GPU=default leaves the choice to Windows.
if ($env:MW2COOP_TEST_GPU -ne 'default') {
    $gk = 'HKCU:\Software\Microsoft\DirectX\UserGpuPreferences'
    if (-not (Test-Path $gk)) { New-Item -Path $gk -Force | Out-Null }
    foreach ($Dir in $Dirs) {
        $full = (Resolve-Path $Dir).Path.TrimEnd('\') + '\'
        foreach ($n in @('iw4sp.exe', 'iw4x-sp.exe')) {
            $f = $full + $n
            if ((Test-Path $f) -and ((Get-ItemProperty $gk -ErrorAction SilentlyContinue).$f -ne 'GpuPreference=2;')) {
                Set-ItemProperty -Path $gk -Name $f -Value 'GpuPreference=2;'
                "[$Tag] discrete GPU preference set for $f"
            }
        }
    }
}
$root = $TestRoot.TrimEnd('\') + '\'
$start = Get-Date
$launched = @()
foreach ($Dir in $Dirs) {
    # The game writes its PID to <dir>\__iw4sp and deletes it on a clean quit; we may kill it.
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $Dir '__iw4sp')
    $exe = if ($ExeName) { Join-Path $Dir $ExeName } elseif (Test-Path (Join-Path $Dir 'iw4x-sp.exe')) { Join-Path $Dir 'iw4x-sp.exe' } else { Join-Path $Dir 'iw4sp.exe' }
    if ($GameArgs) { $p = Start-Process -FilePath $exe -ArgumentList $GameArgs -WorkingDirectory $Dir -PassThru }
    else { $p = Start-Process -FilePath $exe -WorkingDirectory $Dir -PassThru }
    $launched += $p
    "[$Tag] started pid $($p.Id) in $Dir at $((Get-Date).ToString('HH:mm:ss.fff'))"
    if ($Dir -ne $Dirs[-1]) {
        $until = (Get-Date).AddSeconds($Stagger)
        while ((Get-Date) -lt $until) {
            Start-Sleep -Milliseconds 200
            Get-CimInstance Win32_Process -Filter "name='iw4sp.exe' or name='iw4x-sp.exe'" | Where-Object { $_.ExecutablePath -and $_.ExecutablePath -notlike "$root*" } |
                ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "[$Tag] WRONG INSTALL RUNNING: $($_.ExecutablePath) -> killed" }
        }
    }
}
# The test windows go to the secondary display (when there is one), so that they do not get in the way on the main one.
# MW2COOP_TEST_DISPLAY=primary keeps them where the game puts them.
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Collections.Generic;
public static class GameWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  // moves every 'IW4' window of the given process ids that is not on the screen [sx, sx+sw) x [sy, sy+sh) to its top left corner + 40
  public static int Move(uint[] pids, int sx, int sy, int sw, int sh) {
    int moved = 0;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (Array.IndexOf(pids, p) < 0) return true;
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() != "IW4") return true;
      if (IsIconic(h)) ShowWindow(h, 4); // SW_SHOWNOACTIVATE: restore a minimised game window
      RECT r; GetWindowRect(h, out r);
      if (r.L >= sx && r.L < sx + sw && r.T >= sy && r.T < sy + sh) return true;
      SetWindowPos(h, IntPtr.Zero, sx, sy, 0, 0, 0x0001 | 0x0004 | 0x0010); // NOSIZE | NOZORDER | NOACTIVATE
      moved++;
      return true;
    }, IntPtr.Zero);
    return moved;
  }
}
"@
# Which display: MW2COOP_TEST_DISPLAY, else the first line of local	est-display.txt (next to the test copies' parent: <workspace>\local), else the first non-primary one.
# Values: 'primary' (do not move), 'secondary', or a device name such as \.\DISPLAY7.
$choice = $env:MW2COOP_TEST_DISPLAY
if (-not $choice) {
    $cfg = Join-Path (Split-Path -Parent $PSScriptRoot) '..\test-display.txt'
    if (Test-Path $cfg) { $choice = (Get-Content $cfg -TotalCount 1).Trim() }
}
$secondary = $null
if ($choice -ne 'primary') {
    $screens = [System.Windows.Forms.Screen]::AllScreens
    if ($choice -and $choice -ne 'secondary') { $secondary = $screens | Where-Object { $_.DeviceName -eq $choice } | Select-Object -First 1 }
    if (-not $secondary) { $secondary = $screens | Where-Object { -not $_.Primary } | Select-Object -First 1 }
}
$lastMove = Get-Date
$deadline = $start.AddSeconds($Timeout)
$seen = @{}
$idleSince = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 200
    $procs = @(Get-CimInstance Win32_Process -Filter "name='iw4sp.exe' or name='iw4x-sp.exe'" -ErrorAction SilentlyContinue)
    if ($secondary -and ((Get-Date) - $lastMove).TotalSeconds -ge 1) {
        $lastMove = Get-Date
        $b = $secondary.Bounds
        $null = [GameWin]::Move([uint32[]]@($procs | ForEach-Object { $_.ProcessId }), $b.X, $b.Y, $b.Width, $b.Height)
    }
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
$left = Get-Process iw4sp,iw4x-sp -ErrorAction SilentlyContinue
if ($left) { "[$Tag] timeout, killing $($left.Id -join ',')"; $left | Stop-Process -Force }
else { "[$Tag] all exited after $([int]((Get-Date) - $start).TotalSeconds) s; codes: $(($launched | ForEach-Object { $_.ExitCode }) -join ',')" }
if ($RealInstall) {
    $touched = Get-ChildItem -Force -Recurse -File $RealInstall | Where-Object { $_.LastWriteTime -ge $start.AddSeconds(-2) }
    if ($touched) { "[$Tag] !!! REAL INSTALL TOUCHED:"; $touched | ForEach-Object { "  $($_.FullName) $($_.Length)" } }
    else { "[$Tag] real install untouched" }
}
