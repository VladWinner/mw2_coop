param([int]$Delay = 105, [int]$Duration = 80, [string]$Pattern = "walkturn")
# QA input for a test copy (the owner is not at the PC): waits, brings the game window to the front and moves the mouse / presses keys with SendInput; sends nothing while another window is in front.
# Only touches the window of an iw4sp.exe under the test root ($env:MW2COOP_TEST_ROOT as a Windows path, default D:\coop-test).
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Inp {
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U u; }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public static uint FgPid() { uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return pid; }
    public static void Move(int dx, int dy) {
        INPUT[] a = new INPUT[1]; a[0].type = 0; a[0].u.mi.dx = dx; a[0].u.mi.dy = dy; a[0].u.mi.dwFlags = 1;
        SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Key(ushort scan, bool down) {
        INPUT[] a = new INPUT[1]; a[0].type = 1; a[0].u.ki.wScan = scan; a[0].u.ki.dwFlags = (uint)(0x8 | (down ? 0 : 2));
        SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Click(bool down) {
        INPUT[] a = new INPUT[1]; a[0].type = 0; a[0].u.mi.dwFlags = (uint)(down ? 2 : 4);
        SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@
$root = 'D:\coop-test'
if ($env:MW2COOP_TEST_ROOT -and $env:MW2COOP_TEST_ROOT -match '^[A-Za-z]:\\') { $root = $env:MW2COOP_TEST_ROOT.TrimEnd('\') }
Start-Sleep -Seconds $Delay
$p = Get-Process iw4sp -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$root\*" } | Select-Object -First 1
if (-not $p -or $p.MainWindowHandle -eq 0) { "drive: no test game window"; exit 1 }
[Inp]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[Inp]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 500
"drive: start $(Get-Date -Format HH:mm:ss.fff)"
$sw = [Diagnostics.Stopwatch]::StartNew()
$w = $false
while ($sw.Elapsed.TotalSeconds -lt $Duration) {
    $t = $sw.Elapsed.TotalSeconds
    if ([Inp]::FgPid() -ne $p.Id) { [Inp]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 100; if ([Inp]::FgPid() -ne $p.Id) { "drive: game not in front, input paused"; Start-Sleep -Milliseconds 500; continue } }
    if ($Pattern -eq "walkturn") {
        $dir = if ([int][math]::Floor($t / 3) % 2 -eq 0) { 1 } else { -1 }
        [Inp]::Move(5 * $dir, 0)
        $want = ([int][math]::Floor($t / 6) % 2 -eq 0)
        if ($want -and -not $w) { [Inp]::Key(0x11, $true); $w = $true }
        if (-not $want -and $w) { [Inp]::Key(0x11, $false); $w = $false }
        if ([int][math]::Floor($t * 2) % 3 -eq 0) { [Inp]::Click($true) } else { [Inp]::Click($false) }
    }
    Start-Sleep -Milliseconds 12
}
if ($w) { [Inp]::Key(0x11, $false) }
[Inp]::Click($false)
"drive: end $(Get-Date -Format HH:mm:ss.fff)"
