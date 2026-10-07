$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$out=Join-Path $PSScriptRoot "lamparray-disable-$stamp.json"
try {
 $before=Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'" | Select-Object Name,State,StartMode,ProcessId,PathName
 Set-Service -Name logi_lamparray_service -StartupType Manual
 Stop-Service -Name logi_lamparray_service
 (Get-Service logi_lamparray_service).WaitForStatus('Stopped',[TimeSpan]::FromSeconds(15))
 $after=Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'" | Select-Object Name,State,StartMode,ProcessId
 [pscustomobject]@{Time=(Get-Date).ToString('o');Before=$before;After=$after;Note='Restore with: Set-Service logi_lamparray_service -StartupType Automatic; Start-Service logi_lamparray_service'} | ConvertTo-Json -Depth 4 | Set-Content $out -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "lamparray-disable-$stamp-error.txt") -Encoding UTF8;exit 1}
