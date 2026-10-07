param([int]$Slot=7)
$ErrorActionPreference='Stop'
$probe=$null
try {
 Add-Type -Path 'C:\OpenRGB-Diagnostics\2026-09-13-g502-rcca\HidppProbe.cs'
 $probe=New-Object HidppProbe
 $reply=$probe.Request([byte]$Slot,0,0,[byte[]]@(0x00,0x03,0))
 [pscustomobject]@{Time=(Get-Date).ToString('o');Slot=$Slot;DeviceNameFeatureIndex=$reply[0];Trace=@($probe.Trace)} | ConvertTo-Json -Depth 4
} catch {$_ | Out-String; if($probe){$probe.Trace}; exit 1}
finally {if($probe) {$probe.Dispose()}}
