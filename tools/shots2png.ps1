# QA frames of both test copies, as PNG for review: <map>-<start>-<label>.bmp from each copy's
# mw2coop\shots into <out>\<prefix>-host-<label>.png and -client-<label>.png (host = player 1).
# Usage: powershell -File shots2png.ps1 -Root D:\coop-test -Pattern invasion-convoy -Prefix inv -Out <dir>
param([string]$Root = 'D:\coop-test', [string]$Pattern, [string]$Prefix, [string]$Out)
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($side in @(@('mw2-64', 'host'), @('mw2-64-b', 'client'))) {
	$dir = Join-Path $Root ($side[0] + '\mw2coop\shots')
	Get-ChildItem $dir -Filter "$Pattern-*.bmp" -ErrorAction SilentlyContinue | ForEach-Object {
		$label = $_.BaseName.Substring($Pattern.Length + 1)
		$o = Join-Path $Out ("$Prefix-" + $side[1] + "-$label.png")
		$i = [System.Drawing.Image]::FromFile($_.FullName)
		$i.Save($o, [System.Drawing.Imaging.ImageFormat]::Png)
		$i.Dispose()
		$o
	}
}
