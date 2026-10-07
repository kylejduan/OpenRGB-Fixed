$ErrorActionPreference='Stop'
$probe=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path 'C:\OpenRGB-Diagnostics\2026-09-13-g502-rcca\HidppProbe.cs'
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 $before=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 $powerBefore=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 $claim=$probe.Request(1,9,0x50,[byte[]]@(1,3,5))
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($control[1] -ne 3) {throw 'Software ownership did not read back'}
 $waited=$false
 if($powerBefore[1] -ne 1) {
  $setPower=$probe.Request(1,9,0x80,[byte[]]@(1,1,0))
  Start-Sleep -Milliseconds 1200
  $waited=$true
 }
 $power=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 if($power[1] -ne 1) {throw 'Full RGB power did not read back'}
 [byte[]]$data=New-Object byte[] 16
 $data[0]=0;$data[1]=1;$data[2]=255;$data[3]=0;$data[4]=255;$data[5]=2;$data[12]=1
 $reply=$probe.Request(1,9,0x10,$data)
 $after=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 $powerAfter=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 [pscustomobject]@{Time=(Get-Date).ToString('o');Color='FF00FF';Marker=2;ControlBefore=@($before);PowerBefore=@($powerBefore);WaitedForPower=$waited;ControlAfter=@($after);PowerAfter=@($powerAfter);Reply=@($reply);Trace=@($probe.Trace)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot "paint-magenta-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "paint-magenta-$stamp-error.txt") -Encoding UTF8; if($probe) {$probe.Trace | Set-Content (Join-Path $PSScriptRoot "paint-magenta-$stamp-error-trace.txt")};exit 1}
finally {if($probe) {$probe.Dispose()}}
