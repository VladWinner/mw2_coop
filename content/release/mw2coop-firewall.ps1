# mw2coop: let the game receive co-op traffic through Windows Firewall (run by mw2coop-firewall.bat).
# Without administrator rights it only checks. If iw4sp.exe has no inbound allow rule, or has a
# block rule (Windows adds one when its "Allow access?" prompt was cancelled), it asks for
# administrator rights once (UAC) and then: removes the block rules for iw4sp.exe and adds
# "Call of Duty MW2 co-op (mw2coop)", inbound UDP, any network profile. -Check only reports.
param([string]$Exe, [switch]$Apply, [switch]$Check)
$name = 'Call of Duty MW2 co-op (mw2coop)'

function Get-GameRules([string]$path) {
    Get-NetFirewallApplicationFilter -Program $path -ErrorAction SilentlyContinue |
        Get-NetFirewallRule -ErrorAction SilentlyContinue |
        Where-Object { $_.Direction -eq 'Inbound' -and $_.Enabled -eq 'True' }
}

if ($Apply) {
    Get-GameRules $Exe | Where-Object { $_.Action -eq 'Block' } | ForEach-Object {
        Remove-NetFirewallRule -Name $_.Name
        "removed block rule: $($_.DisplayName)"
    }
    if (-not (Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow -Protocol UDP `
            -Program $Exe -Profile Any | Out-Null
        "added: $name"
    }
    exit 0
}

$rules = @(Get-GameRules $Exe)
$block = @($rules | Where-Object { $_.Action -eq 'Block' })
$allow = @($rules | Where-Object { $_.Action -eq 'Allow' })
if ($allow.Count -gt 0 -and $block.Count -eq 0) {
    'Windows Firewall already lets the game receive co-op traffic. Nothing to do.'
    exit 0
}
if ($block.Count -gt 0) { "Windows Firewall blocks the game ($($block.Count) rule(s))." }
else { 'Windows Firewall has no rule for the game yet.' }
if ($Check) { exit 1 }
'Windows will now ask for administrator rights once to fix it.'
$argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Exe', "`"$Exe`"", '-Apply')
try {
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList $argList
} catch {
    'Administrator rights were not given; nothing changed.'
    exit 1
}
$rules = @(Get-GameRules $Exe)
if (@($rules | Where-Object { $_.Action -eq 'Allow' }).Count -gt 0 -and @($rules | Where-Object { $_.Action -eq 'Block' }).Count -eq 0) {
    'Done: the game can receive co-op traffic.'
    exit 0
}
'The rule could not be added.'
exit 1
