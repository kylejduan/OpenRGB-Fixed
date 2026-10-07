param([int]$Minutes=11)
# Read-only. Windows delivers every HID input report to all open handles, so this
# records the replies to OpenRGB's own lighting transactions on the receiver's
# long-report collection. Used to confirm the periodic colour refresh runs.
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$log=Join-Path $PSScriptRoot "lighting-traffic-$stamp.log"
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'LongNotifyProbe.cs')
 $probe=New-Object LongNotifyProbe
 "$(Get-Date -Format o) start" | Add-Content $log -Encoding UTF8
 $deadline=(Get-Date).AddMinutes($Minutes)
 while((Get-Date) -lt $deadline) {
  $r=$probe.Next(1000)
  if($r -ne '') {"$(Get-Date -Format o) $r" | Add-Content $log -Encoding UTF8}
 }
 "$(Get-Date -Format o) end" | Add-Content $log -Encoding UTF8
} catch {"$(Get-Date -Format o) error $($_.Exception.Message)" | Add-Content $log -Encoding UTF8;exit 1}
finally {if($probe){$probe.Dispose()}}
