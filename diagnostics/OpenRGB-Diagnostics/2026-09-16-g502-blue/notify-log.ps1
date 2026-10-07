param([int]$Hours=6)
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$log=Join-Path $PSScriptRoot "notify-$stamp.log"
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'NotifyProbe.cs')
 $probe=New-Object NotifyProbe
 "$(Get-Date -Format o) start" | Add-Content $log -Encoding UTF8
 $deadline=(Get-Date).AddHours($Hours)
 while((Get-Date) -lt $deadline) {
  $r=$probe.Next(1000)
  if($r -ne '') { "$(Get-Date -Format o) $r" | Add-Content $log -Encoding UTF8 }
 }
 "$(Get-Date -Format o) end" | Add-Content $log -Encoding UTF8
} catch {"$(Get-Date -Format o) error $($_.Exception.Message)" | Add-Content $log -Encoding UTF8; exit 1}
finally {if($probe){$probe.Dispose()}}
