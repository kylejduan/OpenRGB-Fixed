$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 . 'C:\OpenRGB-Diagnostics\2026-09-13-g502-rcca\runtime-helpers.ps1'
 Assert-MainProfile
 $old=@(Get-Process OpenRGB -ErrorAction SilentlyContinue)
 if($old.Count -ne 1 -or $old[0].Path -ne $installedExe) {throw 'Expected exactly one installed OpenRGB process'}
 $exit=Stop-RgbNormally $old[0]
 $logs=Join-Path $env:APPDATA 'OpenRGB\logs'
 $before=@(Get-ChildItem $logs -Filter *.log | Select-Object -ExpandProperty Name)
 $new=Start-Process $installedExe -WorkingDirectory (Split-Path $installedExe) -ArgumentList @('--startminimized','--profile','Main','--loglevel','5') -PassThru
 $deadline=(Get-Date).AddSeconds(150)
 $log=$null
 do {
  Start-Sleep -Seconds 1
  $log=Get-ChildItem $logs -Filter *.log | Where-Object {$before -notcontains $_.Name} | Sort-Object LastWriteTime | Select-Object -Last 1
  $done=$log -and (Select-String -Path $log.FullName -Pattern 'Detection completed' -Quiet)
 } while(-not $done -and (Get-Date) -lt $deadline)
 Start-Sleep -Seconds 5
 if($log) {Copy-Item $log.FullName (Join-Path $here "debug-detection-$stamp.log")}
 [pscustomobject]@{Time=(Get-Date).ToString('o');OldExit=$exit;NewPid=$new.Id;Log=if($log){$log.Name}else{$null};DetectionCompleted=[bool]$done} | ConvertTo-Json | Set-Content (Join-Path $here "restart-debug-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $here "restart-debug-$stamp-error.txt") -Encoding UTF8;exit 1}
