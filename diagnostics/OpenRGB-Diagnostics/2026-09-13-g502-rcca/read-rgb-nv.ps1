$ErrorActionPreference='Stop';$probe=$null;$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs');$probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0));if($feature[0] -ne 9) {throw 'Feature map changed'}
 $info=$probe.Request(1,9,0,[byte[]]@(255,255,0));$caps=@()
 foreach($cap in @(0x10,0x20)) {
  try {$r=$probe.Request(1,9,0x30,[byte[]]@(0,0,$cap));$caps+=[pscustomobject]@{Capability=$cap;Reply=@($r)}}
  catch {$caps+=[pscustomobject]@{Capability=$cap;Error=$_.ToString()}}
 }
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0));$power=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 [pscustomobject]@{Time=(Get-Date).ToString('o');DeviceInfo=@($info);Caps=$caps;Control=@($control);Power=@($power);Trace=@($probe.Trace);ReadOnly=$true} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "rgb-nv-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "rgb-nv-$stamp-error.txt");exit 1}
finally {if($probe){$probe.Dispose()}}
