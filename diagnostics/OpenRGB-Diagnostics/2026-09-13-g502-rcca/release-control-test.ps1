$ErrorActionPreference='Stop'
$probe=$null
try {
 & (Join-Path $PSScriptRoot 'inspect-before-release.ps1')
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 $before=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($before[1] -ne 3) {throw 'Expected software control before experiment'}
 $release=$probe.Request(1,9,0x50,[byte[]]@(1,0,0))
 $released=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($released[1] -ne 0) {throw 'Control release was not acknowledged by readback'}
 & (Join-Path $PSScriptRoot 'Apply-MouseColor.ps1') -Color FF0000 -Record 'red-after-release.json'
 $after=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 $power=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 [pscustomobject]@{Time=(Get-Date).ToString('o');Before=@($before);Released=@($released);AfterOpenRGBRed=@($after);Power=@($power);Trace=@($probe.Trace);Service=(Get-Service logi_lamparray_service).Status.ToString()} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'release-control-test.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'release-control-test-error.txt') -Encoding UTF8; if($probe) {$probe.Trace | Set-Content (Join-Path $PSScriptRoot 'release-control-test-error-trace.txt')};exit 1}
finally {if($probe) {$probe.Dispose()}}
