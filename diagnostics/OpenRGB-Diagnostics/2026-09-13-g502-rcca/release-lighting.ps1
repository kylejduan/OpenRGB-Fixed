param([switch]$PowerOff)
$ErrorActionPreference='Stop'
$probe=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 if($PowerOff) {$claim=$probe.Request(1,9,0x50,[byte[]]@(1,3,5));$off=$probe.Request(1,9,0x80,[byte[]]@(1,3,0))}
 if(-not $PowerOff) {$release=$probe.Request(1,9,0x50,[byte[]]@(1,0,0))}
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 $power=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 if($PowerOff) {if($control[1] -ne 3 -or $power[1] -ne 3) {throw 'RGB power-off state did not read back'}}
 else {if($control[1] -ne 0) {throw 'Control release did not read back'}}
 [pscustomobject]@{Time=(Get-Date).ToString('o');PowerOffRequested=[bool]$PowerOff;Control=@($control);Power=@($power);Trace=@($probe.Trace)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot "release-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "release-$stamp-error.txt");exit 1}
finally {if($probe) {$probe.Dispose()}}
