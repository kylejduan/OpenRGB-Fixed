$ErrorActionPreference='Stop'
$probe=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$rows=@()
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 for($i=0;$i -lt 31;$i++) {
  if(Test-Path (Join-Path $PSScriptRoot 'stop-control-monitor.flag')) {break}
  $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
  $p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
  $row=[pscustomobject]@{Time=(Get-Date).ToString('o');Control=$c[1];Events=$c[2];Power=$p[1];OpenRGBPid=@(Get-Process OpenRGB).Id;LampArray=(Get-Service logi_lamparray_service).Status.ToString()}
  $rows+=$row;$row | ConvertTo-Json -Compress | Add-Content (Join-Path $PSScriptRoot "control-monitor-$stamp.jsonl") -Encoding UTF8
  if($i -lt 30) {Start-Sleep -Seconds 10}
 }
 [pscustomobject]@{Completed=(Get-Date).ToString('o');Pid=$PID;Samples=$rows.Count;Rows=$rows;Trace=@($probe.Trace);Note='Read-only protocol polling, not an idle/sleep acceptance test'} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot "control-monitor-$stamp-complete.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "control-monitor-$stamp-error.txt");exit 1}
finally {if($probe){$probe.Dispose()}}
