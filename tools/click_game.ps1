param([int]$X, [int]$Y, [int]$TargetPid = 0, [string]$Key = '')
# Click the TEST game's menu at pixel (X, Y) of its client area (the same pixels as a
# shot_game.ps1 screenshot), or press a key (-Key ESC|ENTER|UP|DOWN). The 'IW4' window is made
# TOPMOST + foreground first so the click cannot land in another window; it is released after.
# Use only on test copies started by run_test.ps1.
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class C {
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public static IntPtr Find(uint want) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      var c = new System.Text.StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() == "IW4" && (want == 0 || p == want)) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
}
"@
[C]::SetProcessDPIAware() | Out-Null
$h = [C]::Find([uint32]$TargetPid)
if ($h -eq [IntPtr]::Zero) { 'no IW4 window'; exit 1 }
$SWP = 0x0001 -bor 0x0002 -bor 0x0040
[C]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, $SWP) | Out-Null
[C]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
if ($Key) {
  $vk = @{ ESC = 0x1b; ENTER = 0x0d; UP = 0x26; DOWN = 0x28 }[$Key]
  [C]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [C]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero)
  "key $Key"
} else {
  $pt = New-Object C+POINT; $pt.X = $X; $pt.Y = $Y
  [C]::ClientToScreen($h, [ref]$pt) | Out-Null
  [C]::SetCursorPos($pt.X, $pt.Y) | Out-Null; Start-Sleep -Milliseconds 300
  [C]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
  [C]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  "clicked $X,$Y"
}
Start-Sleep -Milliseconds 300
[C]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, $SWP) | Out-Null
