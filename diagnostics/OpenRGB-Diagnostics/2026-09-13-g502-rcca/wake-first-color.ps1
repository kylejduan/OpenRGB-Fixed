param([ValidatePattern('^[0-9A-Fa-f]{6}$')][string]$Color='FF0000',[switch]$ReclaimAfterWake,[ValidateRange(1,2)][int]$Frames=1,[ValidateRange(0,1000)][int]$SettleMilliseconds=0)
$ErrorActionPreference='Stop';$probe=$null;$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs');$probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0));if($feature[0] -ne 9) {throw 'Feature map changed'}
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0));$before=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 if($control[1] -ne 3 -or $before[1] -ne 3) {throw 'Expected held software control and RGB power-off'}
 $timer=[Diagnostics.Stopwatch]::StartNew()
 $wake=$probe.Request(1,9,0x80,[byte[]]@(1,1,0));$power=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
 if($power[1] -ne 1) {throw 'Full power did not read back'}
 if($ReclaimAfterWake) {$claim=$probe.Request(1,9,0x50,[byte[]]@(1,3,5))}
 else {$claim=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))}
 $after=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($after[1] -ne 3) {throw 'Control did not read back'}
 [byte[]]$data=New-Object byte[] 16;$data[1]=1;$data[5]=2;$data[12]=1
 for($i=0;$i -lt 3;$i++) {$data[2+$i]=[Convert]::ToByte($Color.Substring($i*2,2),16)}
 if($SettleMilliseconds -gt 0) {Start-Sleep -Milliseconds $SettleMilliseconds}
 $beforePaintMs=$timer.Elapsed.TotalMilliseconds
 $paints=@()
 for($frame=0;$frame -lt $Frames;$frame++) {
  $sentAt=$timer.Elapsed.TotalMilliseconds
  $reply=$probe.Request(1,9,0x10,$data)
  $paints+=[pscustomobject]@{Frame=$frame+1;SentAtMs=$sentAt;ReplyAtMs=$timer.Elapsed.TotalMilliseconds}
 }
 $timer.Stop()
 [pscustomobject]@{Time=(Get-Date).ToString('o');Color=$Color;ReclaimAfterWake=[bool]$ReclaimAfterWake;Frames=$Frames;SettleMilliseconds=$SettleMilliseconds;Paints=$paints;BeforePaintMs=$beforePaintMs;TotalMs=$timer.Elapsed.TotalMilliseconds;BeforeControl=@($control);BeforePower=@($before);Power=@($power);Reply=@($reply);Trace=@($probe.Trace);Note='Controlled wake experiment; exact frame count and any intentional settling delay are recorded'} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "wake-first-color-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "wake-first-color-$stamp-error.txt");exit 1}
finally {if($probe){$probe.Dispose()}}
