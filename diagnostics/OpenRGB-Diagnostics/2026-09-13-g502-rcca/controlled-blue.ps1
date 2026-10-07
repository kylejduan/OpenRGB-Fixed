param([ValidateSet(0,2)][int]$Marker=0)
$ErrorActionPreference='Stop'
$probe=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 $claim=$probe.Request(1,9,0x50,[byte[]]@(1,3,5))
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 $power=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 if($control[1] -ne 3 -or $power[1] -ne 1) {throw 'Expected software control and full RGB power'}
 [byte[]]$data=New-Object byte[] 16
 $data[0]=0;$data[1]=1;$data[4]=255;$data[5]=[byte]$Marker;$data[12]=1
 $reply=$probe.Request(1,9,0x10,$data)
 [pscustomobject]@{Time=(Get-Date).ToString('o');Marker=$Marker;Color='0000FF';Reply=@($reply);Trace=@($probe.Trace)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot "controlled-blue-$stamp-marker-$Marker.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "controlled-blue-$stamp-marker-$Marker-error.txt") -Encoding UTF8; if($probe) {$probe.Trace | Set-Content (Join-Path $PSScriptRoot "controlled-blue-$stamp-marker-$Marker-error-trace.txt")};exit 1}
finally {if($probe) {$probe.Dispose()}}
