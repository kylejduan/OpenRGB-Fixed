param([int]$TimeoutSeconds=25)
# Forces firmware lighting control, then makes the receiver re-announce its paired
# devices (the request OpenRGB sends at startup). A watcher that hears the link
# announcement reclaims within seconds instead of waiting for its 30 s poll.
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$probe=$null;$notify=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbeLocal.cs')
 Add-Type -Path (Join-Path $PSScriptRoot 'NotifyProbe.cs')
 $probe=New-Object HidppProbeLocal
 $notify=New-Object NotifyProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 $before=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($before[1] -ne 3) {throw "Expected OpenRGB to hold control, read $($before[1])"}
 $released=$probe.Request(1,9,0x50,[byte[]]@(1,0,6))
 $t0=Get-Date
 $notify.Write([byte[]]@(0x10,0xFF,0x80,0x02,0x02,0x00,0x00))
 $reports=@()
 $rows=@();$reclaimed=$null
 while(((Get-Date)-$t0).TotalSeconds -lt $TimeoutSeconds) {
  $r=$notify.Next(250); if($r -ne '') {$reports+=("{0:N2} {1}" -f ((Get-Date)-$t0).TotalSeconds,$r)}
  $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
  $s=[math]::Round(((Get-Date)-$t0).TotalSeconds,2)
  $rows+=[pscustomobject]@{Seconds=$s;Control=$c[1];Events=$c[2]}
  if($c[1] -eq 3) {$reclaimed=$s;break}
 }
 [pscustomobject]@{Time=$t0.ToString('o');ControlBefore=@($before[0..2]);Released=@($released[0..2]);ReclaimedAfterSeconds=$reclaimed;Reports=$reports;Samples=$rows} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "link-takeover-$stamp.json") -Encoding UTF8
 if($null -eq $reclaimed) {exit 2}
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "link-takeover-$stamp-error.txt") -Encoding UTF8;exit 1}
finally {if($probe){$probe.Dispose()};if($notify){$notify.Dispose()}}
