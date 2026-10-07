param([int]$TimeoutSeconds=75)
# Simulates another application taking the mouse back to firmware lighting,
# then measures how long the installed OpenRGB watcher takes to reclaim it.
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbeLocal.cs')
 $probe=New-Object HidppProbeLocal
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 $before=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
 if($before[1] -ne 3) {throw "Expected OpenRGB to hold control before the test, read $($before[1])"}
 $released=$probe.Request(1,9,0x50,[byte[]]@(1,0,6))
 $t0=Get-Date
 $rows=@()
 $reclaimed=$null
 while(((Get-Date)-$t0).TotalSeconds -lt $TimeoutSeconds) {
  Start-Sleep -Seconds 2
  $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
  $rows+=[pscustomobject]@{Seconds=[math]::Round(((Get-Date)-$t0).TotalSeconds,1);Control=$c[1];Events=$c[2]}
  if($c[1] -eq 3) {$reclaimed=[math]::Round(((Get-Date)-$t0).TotalSeconds,1);break}
 }
 [pscustomobject]@{Time=$t0.ToString('o');ControlBefore=@($before[0..2]);Released=@($released[0..2]);ReclaimedAfterSeconds=$reclaimed;Samples=$rows;OpenRGB=@(Get-Process OpenRGB | Select-Object Id,StartTime)} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "takeover-$stamp.json") -Encoding UTF8
 if($null -eq $reclaimed) {exit 2}
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "takeover-$stamp-error.txt") -Encoding UTF8;if($probe){$probe.Trace | Set-Content (Join-Path $PSScriptRoot "takeover-$stamp-error-trace.txt")};exit 1}
finally {if($probe){$probe.Dispose()}}
