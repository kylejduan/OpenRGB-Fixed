param([int]$Hours=10,[int]$IntervalSeconds=15)
# Read-only. Logs 0x8071 software control and RGB power for the mouse plus every
# receiver connection notification, so a reported colour drift can be correlated
# with link cycles or power-mode changes. Reads only; never writes lighting.
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$log=Join-Path $PSScriptRoot "drift-monitor-$stamp.jsonl"
function Row($o) { $o | ConvertTo-Json -Compress -Depth 4 | Add-Content $log -Encoding UTF8 }
$probe=$null;$notify=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbeLocal.cs')
 Add-Type -Path (Join-Path $PSScriptRoot 'NotifyProbe.cs')
 $notify=New-Object NotifyProbe
 Row ([pscustomobject]@{Event='start';Time=(Get-Date).ToString('o');IntervalSeconds=$IntervalSeconds})
 $deadline=(Get-Date).AddHours($Hours)
 $last=''
 while((Get-Date) -lt $deadline) {
  # Receiver notifications first: they arrive unsolicited.
  while($true) {
   $r=$notify.Next(0)
   if($r -eq '') {break}
   Row ([pscustomobject]@{Event='report';Time=(Get-Date).ToString('o');Bytes=$r})
  }
  $state='no-reply'
  try {
   $probe=New-Object HidppProbeLocal
   $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
   $p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
   $state="control=$($c[1]) events=$($c[2]) power=$($p[1])"
  } catch { $state='no-reply' }
  finally { if($probe){$probe.Dispose();$probe=$null} }
  if($state -ne $last) {
   Row ([pscustomobject]@{Event='state';Time=(Get-Date).ToString('o');State=$state;OpenRGBPid=@(Get-Process OpenRGB -ErrorAction SilentlyContinue).Id})
   $last=$state
  }
  Start-Sleep -Seconds $IntervalSeconds
 }
 Row ([pscustomobject]@{Event='end';Time=(Get-Date).ToString('o')})
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "drift-monitor-$stamp-error.txt") -Encoding UTF8;exit 1}
finally {if($probe){$probe.Dispose()};if($notify){$notify.Dispose()}}
