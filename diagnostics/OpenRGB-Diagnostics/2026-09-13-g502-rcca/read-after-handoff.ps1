$ErrorActionPreference='Stop'
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -eq 0) {throw 'G502 RGB Effects feature absent'}
 $control=$probe.Request(1,$feature[0],0x50,[byte[]]@(0,0,0))
 $power=$probe.Request(1,$feature[0],0x80,[byte[]]@(0,0,0))
 $info=$probe.Request(1,$feature[0],0,[byte[]]@(255,255,0))
 $cluster=$probe.Request(1,$feature[0],0,[byte[]]@(0,255,0))
 $effect=$probe.Request(1,$feature[0],0,[byte[]]@(0,1,0))
 [pscustomobject]@{Time=(Get-Date).ToString('o');Slot=1;FeatureIndex=$feature[0];FeatureVersion=$feature[2];ControlResponse=@($control);PowerResponse=@($power);DeviceInfo=@($info);ClusterInfo=@($cluster);StaticEffectInfo=@($effect);Trace=@($probe.Trace)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'control-after-handoff.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'read-after-handoff-error.txt') -Encoding UTF8; if($probe) {$probe.Trace | Set-Content (Join-Path $PSScriptRoot 'read-after-handoff-error-trace.txt')}; exit 1}
finally {if($probe) {$probe.Dispose()}}
