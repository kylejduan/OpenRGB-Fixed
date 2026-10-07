param([ValidateSet('Stop','Restore')][string]$Action='Stop')
$ErrorActionPreference='Stop';$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 $baseline=Join-Path $PSScriptRoot 'lamp-baseline-before-power-isolation.json'
 $service=Get-Service -Name logi_lamparray_service
 if($Action -eq 'Stop') {
  if(Test-Path $baseline) {throw 'Isolation baseline already exists'}
  if($service.Status -ne 'Running') {throw 'Expected running Logitech lighting service before isolation'}
  Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'" | Select-Object Name,State,StartMode,ProcessId | ConvertTo-Json | Set-Content $baseline -Encoding UTF8
  Stop-Service -Name logi_lamparray_service
  $service.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(10))
 } else {
  $saved=Get-Content $baseline -Raw | ConvertFrom-Json
  if($saved.State -ne 'Running' -or $saved.StartMode -ne 'Auto') {throw 'Unexpected baseline; inspect before restore'}
  Start-Service -Name logi_lamparray_service
  $service.WaitForStatus('Running',[TimeSpan]::FromSeconds(10))
 }
 [pscustomobject]@{Time=(Get-Date).ToString('o');Action=$Action;Service=(Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'" | Select-Object Name,State,StartMode,ProcessId);OpenRGB=@(Get-Process OpenRGB | Select-Object Id,Path,StartTime)} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "lamp-isolation-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "lamp-isolation-$stamp-error.txt");exit 1}
