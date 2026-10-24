# A contact sheet of QA frames: rows are steps, columns the host (player 1) and the client (player 2).
# Usage: powershell -File sheet.ps1 -Dir <shots dir> -Prefix <prefix> -Steps a,b,c -Title "text" -Out <png>
# Reads <Dir>\<Prefix>-host-<step>.png and <Prefix>-client-<step>.png, 320 px wide each.
param([string]$Dir, [string]$Prefix, [string[]]$Steps, [string]$Title, [string]$Out)
Add-Type -AssemblyName System.Drawing
$Steps = @($Steps | ForEach-Object { $_ -split ',' })   # -File passes "a,b" as one string
$w = 320; $h = 240; $cap = 22; $head = 30
$rows = $Steps.Count
$bmp = New-Object System.Drawing.Bitmap ($w * 2 + 6), ($head + $rows * ($h + $cap))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 24))
$g.InterpolationMode = 'HighQualityBicubic'
$font = New-Object System.Drawing.Font 'Segoe UI', 10
$white = [System.Drawing.Brushes]::White
$g.DrawString($Title + '   (left: player 1 / host, right: player 2 / client)', $font, $white, 4, 6)
for ($i = 0; $i -lt $rows; $i++) {
	$y = $head + $i * ($h + $cap)
	$g.DrawString($Steps[$i], $font, $white, 4, $y + 2)
	$x = 0
	foreach ($side in 'host', 'client') {
		$f = Join-Path $Dir "$Prefix-$side-$($Steps[$i]).png"
		if (Test-Path $f) {
			$img = [System.Drawing.Image]::FromFile($f)
			$g.DrawImage($img, $x, $y + $cap, $w, $h)
			$img.Dispose()
		}
		$x += $w + 6
	}
}
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$Out
