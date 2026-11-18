# mw2coop: let the game receive co-op traffic through Windows Firewall. The mod runs it (src/firewall.c):
# -Check (no administrator rights) only reports; -Apply (with administrator rights, after the in-game
# popup's Yes) fixes.
# The game may receive co-op traffic when enabled inbound rules for its exe allow UDP on every network
# profile (Domain, Private and Public: Radmin VPN, Hamachi and many home networks are Public) and no
# enabled inbound rule for it blocks UDP. Windows adds block rules when its own "Allow access?" prompt was
# cancelled, and allows only the profiles ticked there. Rules are found by their program path, also when
# it is written with environment variables (%ProgramFiles%...).
# -Apply removes the block rules for the exe and adds "Call of Duty MW2 co-op (mw2coop)" for this exe
# (inbound, UDP, any profile), or repairs it; such a rule for another exe (another copy of the game) is
# left alone. Each run writes what it found to firewall-check.txt / firewall-apply.txt next to this script,
# and the mod copies that into mw2coop.log.
# Exit codes: 0 the game may receive co-op traffic, 1 it may not, 2 bad arguments or the firewall could not
# be read.
param([string]$Exe, [switch]$Apply, [switch]$Check)
$name = 'Call of Duty MW2 co-op (mw2coop)'
$report = New-Object System.Collections.Generic.List[string]

function Say([string]$line) {
    $report.Add($line)
    Write-Host $line
}

function Save-Report {
    $file = if ($Apply) { 'firewall-apply.txt' } else { 'firewall-check.txt' }
    try { [IO.File]::WriteAllLines((Join-Path $PSScriptRoot $file), $report) } catch {}
}

function Finish([int]$code) {
    Save-Report
    exit $code
}

# a rule's program as the firewall service reads it; this script may run as 32-bit PowerShell (the 32-bit
# game starts it), where %ProgramFiles% would be the x86 folder
function Get-NormalPath([string]$path) {
    if (-not $path) { return '' }
    $p = $path
    if ($env:ProgramW6432) { $p = $p -ireplace '%ProgramFiles%', $env:ProgramW6432 }
    $p = [Environment]::ExpandEnvironmentVariables($p)
    try { $p = [IO.Path]::GetFullPath($p) } catch {}
    return $p.TrimEnd('\').ToLowerInvariant()
}

# Domain 1, Private 2, Public 4; Any (or no profile) is all three
function Get-ProfileBits($profile) {
    $s = [string]$profile
    if ($s -eq '' -or $s -eq 'Any' -or $s -eq '0') { return 7 }
    $bits = 0
    if ($s -match 'Domain') { $bits = $bits -bor 1 }
    if ($s -match 'Private') { $bits = $bits -bor 2 }
    if ($s -match 'Public') { $bits = $bits -bor 4 }
    return $bits
}

function Get-RuleProtocol($rule) {
    $f = @(Get-NetFirewallPortFilter -AssociatedNetFirewallRule $rule -ErrorAction SilentlyContinue)
    if ($f.Count -eq 0) { return 'Any' }
    return [string]$f[0].Protocol
}

function Test-CarriesUdp([string]$protocol) {
    return $protocol -eq 'Any' -or $protocol -eq 'UDP' -or $protocol -eq '17'
}

# every inbound rule whose program is the exe, enabled or not: objects with Rule, Protocol
function Get-GameRules([string]$path) {
    $want = Get-NormalPath $path
    $filters = @(Get-NetFirewallApplicationFilter -All -ErrorAction Stop | Where-Object { (Get-NormalPath $_.Program) -eq $want })
    foreach ($f in $filters) {
        foreach ($r in @(Get-NetFirewallRule -AssociatedNetFirewallApplicationFilter $f -ErrorAction SilentlyContinue)) {
            if ([string]$r.Direction -ne 'Inbound') { continue }
            [pscustomobject]@{ Rule = $r; Protocol = (Get-RuleProtocol $r) }
        }
    }
}

function Describe($g) {
    $r = $g.Rule
    return "'$($r.DisplayName)' $($r.Action) $($g.Protocol) profile $($r.Profile) enabled $($r.Enabled)"
}

# 0 when the rules let UDP in on every profile and none blocks it, else 1
function Test-Rules($rules) {
    $on = @($rules | Where-Object { [string]$_.Rule.Enabled -eq 'True' -and (Test-CarriesUdp $_.Protocol) })
    $block = @($on | Where-Object { [string]$_.Rule.Action -eq 'Block' })
    $bits = 0
    foreach ($g in @($on | Where-Object { [string]$_.Rule.Action -eq 'Allow' })) { $bits = $bits -bor (Get-ProfileBits $g.Rule.Profile) }
    if ($block.Count -gt 0) {
        Say "Windows Firewall blocks the game ($($block.Count) rule(s))."
        return 1
    }
    if ($bits -eq 0) {
        Say 'Windows Firewall has no rule that lets the game receive UDP.'
        return 1
    }
    if (($bits -band 7) -ne 7) {
        $missing = @()
        if (-not ($bits -band 1)) { $missing += 'Domain' }
        if (-not ($bits -band 2)) { $missing += 'Private' }
        if (-not ($bits -band 4)) { $missing += 'Public' }
        Say "Windows Firewall lets the game receive UDP only on some networks (not on: $($missing -join ', '))."
        return 1
    }
    Say 'Windows Firewall lets the game receive co-op traffic on every network.'
    return 0
}

if (-not $Exe -or -not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    Say "No game exe given or it is not there: '$Exe'. Nothing checked."
    Finish 2
}
Say "exe: $Exe"
$disabled = @(Get-NetFirewallProfile -ErrorAction SilentlyContinue | Where-Object { [string]$_.Enabled -eq 'False' } | ForEach-Object { $_.Name })
if ($disabled.Count -gt 0) { Say "firewall off for: $($disabled -join ', ')" }

try {
    $rules = @(Get-GameRules $Exe)
} catch {
    Say "The firewall rules could not be read: $($_.Exception.Message)"
    Finish 2
}
foreach ($g in $rules) { Say "rule: $(Describe $g)" }
$others = @(Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | ForEach-Object {
        $f = @(Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $_ -ErrorAction SilentlyContinue)
        if ($f.Count -gt 0 -and (Get-NormalPath $f[0].Program) -ne (Get-NormalPath $Exe)) { $f[0].Program }
    })
foreach ($p in $others) {
    if ($p -eq 'Any') { Say "'$name' is also there for every program (Any; left alone, not counted)" }
    else { Say "'$name' is also there for another program: $p (left alone)" }
}

if ($Apply) {
    foreach ($g in @($rules | Where-Object { [string]$_.Rule.Action -eq 'Block' -and [string]$_.Rule.Enabled -eq 'True' })) {
        try {
            Remove-NetFirewallRule -Name $g.Rule.Name -ErrorAction Stop
            Say "removed block rule: $($g.Rule.DisplayName)"
        } catch {
            Say "could not remove block rule $($g.Rule.DisplayName) (a group policy one?): $($_.Exception.Message)"
        }
    }
    $ours = @($rules | Where-Object { $_.Rule.DisplayName -eq $name })
    try {
        if ($ours.Count -eq 0) {
            New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow -Protocol UDP -Program $Exe -Profile Any -ErrorAction Stop | Out-Null
            Say "added: $name"
        } else {
            foreach ($g in $ours) {
                if ([string]$g.Rule.Enabled -ne 'True' -or [string]$g.Rule.Action -ne 'Allow' -or (Get-ProfileBits $g.Rule.Profile) -ne 7 -or -not (Test-CarriesUdp $g.Protocol)) {
                    Set-NetFirewallRule -Name $g.Rule.Name -Enabled True -Action Allow -Profile Any -ErrorAction Stop
                    if (-not (Test-CarriesUdp $g.Protocol)) { Set-NetFirewallRule -Name $g.Rule.Name -Protocol UDP -ErrorAction Stop }
                    Say "repaired: $name"
                }
            }
        }
    } catch {
        Say "The rule could not be made: $($_.Exception.Message)"
    }
    Finish (Test-Rules @(Get-GameRules $Exe))
}

$code = Test-Rules $rules
if ($code -eq 0 -or $Check) { Finish $code }

# run by hand without -Check: ask for administrator rights once and fix
'Windows will now ask for administrator rights once to fix it.'
Save-Report
$argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Exe', "`"$Exe`"", '-Apply')
try {
    Start-Process powershell -Verb RunAs -Wait -ArgumentList $argList | Out-Null
} catch {
    'Administrator rights were not given; nothing changed.'
    exit 1
}
$report.Clear()
exit (Test-Rules @(Get-GameRules $Exe))
