param([int]$Samples=48,[int]$IntervalSeconds=5,[string]$Label='monitor')
$ErrorActionPreference='Stop'
$probe=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$out=Join-Path $PSScriptRoot "$Label-$stamp.jsonl"
try {
 Add-Type -Path 'C:\OpenRGB-Diagnostics\2026-09-13-g502-rcca\HidppProbe.cs'
 $probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0))
 if($feature[0] -ne 9) {throw 'Feature map changed'}
 for($i=0;$i -lt $Samples;$i++) {
  $c=$probe.Request(1,9,0x50,[byte[]]@(0,0,0))
  $p=$probe.Request(1,9,0x80,[byte[]]@(0,0,0))
  $row=[pscustomobject]@{Time=(Get-Date).ToString('o');Control=$c[1];Events=$c[2];Power=$p[1];OpenRGBPid=@(Get-Process OpenRGB -ErrorAction SilentlyContinue).Id;LampArray=(Get-Service logi_lamparray_service).Status.ToString()}
  $row | ConvertTo-Json -Compress | Add-Content $out -Encoding UTF8
  if($i -lt ($Samples-1)) {Start-Sleep -Seconds $IntervalSeconds}
 }
 $probe.Trace | Set-Content (Join-Path $PSScriptRoot "$Label-$stamp-trace.txt") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "$Label-$stamp-error.txt");if($probe){$probe.Trace | Set-Content (Join-Path $PSScriptRoot "$Label-$stamp-error-trace.txt")};exit 1}
finally {if($probe){$probe.Dispose()}}
