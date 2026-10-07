$ErrorActionPreference='Stop'
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 $before=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($before[1] -ne 0) {throw 'Expected firmware ownership before correction'}
 $claim=$probe.Request(1,9,0x50,[byte[]]@(1,3,5))
 $after=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($after[1] -ne 3) {throw 'Software ownership did not read back'}
 [pscustomobject]@{Time=(Get-Date).ToString('o');Before=@($before);After=@($after);Trace=@($probe.Trace);Service=(Get-Service logi_lamparray_service).Status.ToString()} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'reclaim-control.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'reclaim-control-error.txt') -Encoding UTF8;exit 1}
finally {if($probe) {$probe.Dispose()}}
