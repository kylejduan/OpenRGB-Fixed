param([int]$MaxWaitMinutes=240,[int]$MonitorSamples=120,[int]$IntervalSeconds=60)
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$log=Join-Path $PSScriptRoot "wake-watch-$stamp.jsonl"
function Row($obj) { $obj | ConvertTo-Json -Compress -Depth 4 | Add-Content $log -Encoding UTF8 }
$probe=$null
try {
 Add-Type -Path 'C:\OpenRGB-Diagnostics\2026-09-13-g502-rcca\HidppProbe.cs'
 $deadline=(Get-Date).AddMinutes($MaxWaitMinutes)
 $awake=$false
 Row ([pscustomobject]@{Event='start';Time=(Get-Date).ToString('o')})
 while((Get-Date) -lt $deadline) {
  $probe=New-Object HidppProbe
  try {
   $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
   if($feature[0] -eq 9) {$awake=$true}
  } catch {}
  if($awake) {break}
  $probe.Dispose();$probe=$null
  Start-Sleep -Seconds 20
 }
 if(-not $awake) {Row ([pscustomobject]@{Event='timeout';Time=(Get-Date).ToString('o')});exit 2}
 $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0));$p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 Row ([pscustomobject]@{Event='wake-state';Time=(Get-Date).ToString('o');Control=$c[1];Events=$c[2];Power=$p[1];LampArray=(Get-Service logi_lamparray_service).Status.ToString()})
 # Claim software control and paint the saved magenta the way the driver does.
 $claim=$probe.Request(1,9,0x50,[byte[]]@(1,3,5))
 $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($c[1] -ne 3) {Row ([pscustomobject]@{Event='claim-failed';Control=$c[1]});exit 3}
 $p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 if($p[1] -ne 1) {$null=$probe.Request(1,9,0x80,[byte[]]@(1,1,0));Start-Sleep -Milliseconds 1200;$p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))}
 [byte[]]$data=New-Object byte[] 16
 $data[0]=0;$data[1]=1;$data[2]=255;$data[3]=0;$data[4]=255;$data[5]=2;$data[12]=1
 $reply=$probe.Request(1,9,0x10,$data)
 Row ([pscustomobject]@{Event='painted';Time=(Get-Date).ToString('o');Color='FF00FF';PowerBeforePaint=$p[1];Reply=@($reply[0..5])})
 $last=$null
 for($i=0;$i -lt $MonitorSamples;$i++) {
  try {
   $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0));$p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
   $state="$($c[1])/$($c[2])/$($p[1])"
   if($state -ne $last) {Row ([pscustomobject]@{Event='state';Time=(Get-Date).ToString('o');Control=$c[1];Events=$c[2];Power=$p[1];LampArray=(Get-Service logi_lamparray_service).Status.ToString();OpenRGBPid=@(Get-Process OpenRGB -ErrorAction SilentlyContinue).Id});$last=$state}
  } catch {Row ([pscustomobject]@{Event='no-reply';Time=(Get-Date).ToString('o');Message=$_.Exception.Message});$last='no-reply'}
  Start-Sleep -Seconds $IntervalSeconds
 }
 Row ([pscustomobject]@{Event='end';Time=(Get-Date).ToString('o')})
 $probe.Trace | Set-Content (Join-Path $PSScriptRoot "wake-watch-$stamp-trace.txt") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "wake-watch-$stamp-error.txt") -Encoding UTF8;exit 1}
finally {if($probe) {$probe.Dispose()}}
