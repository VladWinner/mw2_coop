param([string]$Out, [int]$TargetPid = 0, [switch]$Keep)
# Capture the iw4sp.exe 'IW4' window CLIENT area. PrintWindow gives black frames for D3D9, so the
# window is made TOPMOST + foreground for a moment and only its client rectangle is copied from the
# screen; nothing else can lie on top of it while it is topmost. (A plain CopyFromScreen without
# topmost grabs whatever window lies on top of the game: never do that.)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class P {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
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
[P]::SetProcessDPIAware() | Out-Null
$h = [P]::Find([uint32]$TargetPid)
if ($h -eq [IntPtr]::Zero) { 'no IW4 window'; exit 1 }
if ([P]::IsIconic($h)) { [P]::ShowWindow($h, 9) | Out-Null }
$SWP = 0x0001 -bor 0x0002 -bor 0x0040   # NOSIZE | NOMOVE | SHOWWINDOW
[P]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, $SWP) | Out-Null   # HWND_TOPMOST
[P]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600
$r = New-Object P+RECT; [P]::GetClientRect($h, [ref]$r) | Out-Null
$pt = New-Object P+POINT; [P]::ClientToScreen($h, [ref]$pt) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
if ($Out -and $w -gt 0 -and $hh -gt 0) {
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, $bmp.Size)
  $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "saved $Out (${w}x$hh at $($pt.X),$($pt.Y))"
}
if (-not $Keep) { [P]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, $SWP) | Out-Null }  # HWND_NOTOPMOST
