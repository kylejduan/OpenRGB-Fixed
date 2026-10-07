$ErrorActionPreference='Stop'
try {
 $svc=Get-Service -Name logi_lamparray_service
 $before=$svc.Status.ToString()
 if($svc.DependentServices.Count -ne 0) {throw 'Lighting service has dependents; stop was not attempted'}
 Stop-Service -Name logi_lamparray_service
 (Get-Service -Name logi_lamparray_service).WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(10))
 [pscustomobject]@{Time=(Get-Date).ToString('o');Service='logi_lamparray_service';Before=$before;After=(Get-Service -Name logi_lamparray_service).Status.ToString();StartModeChanged=$false} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'service-test.json') -Encoding UTF8
 & (Join-Path $PSScriptRoot 'test-mouse-red.ps1')
 Copy-Item (Join-Path $PSScriptRoot 'red-test.json') (Join-Path $PSScriptRoot 'red-without-lamparray.json')
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'service-test-error.txt') -Encoding UTF8;exit 1}
