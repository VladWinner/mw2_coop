param([int]$X, [int]$Y, [int]$TargetPid = 0, [string]$Key = '', [string]$Text = '')
# Click the TEST game's menu at pixel (X, Y) of its client area (the same pixels as a
# shot_game.ps1 screenshot), press a key (-Key ESC|ENTER|UP|DOWN|BACK) or type text (-Text). The 'IW4' window is made
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
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit, Size=40)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  public static void TypeChar(char c) {
    var i = new INPUT[2];
    i[0].type = 1; i[0].ki.scan = c; i[0].ki.flags = 4;          // KEYEVENTF_UNICODE
    i[1].type = 1; i[1].ki.scan = c; i[1].ki.flags = 4 | 2;      // + KEYUP
    SendInput(2, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
  }
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
# Windows only lets a process that got the last input bring a window forward: a tap of Alt counts.
for ($try = 0; $try -lt 5 -and [C]::GetForegroundWindow() -ne $h; $try++) {
  [C]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [C]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
  [C]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 300
}
if ([C]::GetForegroundWindow() -ne $h) {
  [C]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, $SWP) | Out-Null
  'the game window could not be brought to the front; nothing clicked'; exit 1
}
Start-Sleep -Milliseconds 400
if ($Text) {
  foreach ($ch in $Text.ToCharArray()) { [C]::TypeChar($ch); Start-Sleep -Milliseconds 40 }
  "typed $($Text.Length) characters"
} elseif ($Key) {
  $vk = @{ ESC = 0x1b; ENTER = 0x0d; UP = 0x26; DOWN = 0x28; BACK = 0x08 }[$Key]
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
