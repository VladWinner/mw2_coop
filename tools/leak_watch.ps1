# QA tool: samples the memory of the running game (iw4sp.exe) every few seconds into a CSV: private bytes, working set, handles, threads, GDI and USER objects.
# Usage: powershell -File tools\leak_watch.ps1 -Out <csv> [-Seconds 300] [-Every 5]
# A leak shows as a staircase that does not come back down after a level restart (run the restarts with MW2COOP_MID in tools\qa_native.sh, see the header there).
param([string]$Out = "leak.csv", [int]$Seconds = 300, [int]$Every = 5)
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public class G{[DllImport("user32.dll")]public static extern uint GetGuiResources(IntPtr h,uint f);}'
"t,pid,private_mb,ws_mb,handles,threads,gdi,user" | Out-File -Encoding ascii $Out
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
	$p = Get-Process iw4sp -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($p) {
		$line = [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0:F0},{1},{2:F1},{3:F1},{4},{5},{6},{7}", ((Get-Date) - $t0).TotalSeconds, $p.Id, ($p.PrivateMemorySize64 / 1MB), ($p.WorkingSet64 / 1MB), $p.HandleCount, $p.Threads.Count, [G]::GetGuiResources($p.Handle, 0), [G]::GetGuiResources($p.Handle, 1))
		$line | Out-File -Encoding ascii -Append $Out
	}
	Start-Sleep -Seconds $Every
}
