$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 $before=(Get-Service logi_lamparray_service).Status.ToString()
 Start-Service logi_lamparray_service
 (Get-Service logi_lamparray_service).WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Running,[TimeSpan]::FromSeconds(10))
 Start-Sleep -Seconds 8
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $tabs=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::TabItem))) | Where-Object {$_.Current.Name -eq ''})
 if($tabs.Count -ne 5) {throw 'Device tab layout changed'}
 $tabs[2].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 function BySuffix($suffix) {return @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | Where-Object {$_.Current.AutomationId.EndsWith($suffix) -and -not $_.Current.IsOffscreen})[0]}
 $mode=BySuffix '.ControlsFrame.ModeBox'
 if($mode.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -ne 'Direct') {throw 'Expected G502 Direct mode'}
 $hex=BySuffix '.ColorEntryFrame.HexLineEdit'
 $hex.SetFocus();$hex.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue('0000FF')
 $select=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'Select All')))
 $select.SetFocus();$select.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 $apply=BySuffix '.ControlsFrame.ApplyColorsButton'
 $apply.SetFocus();$apply.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 $svc=Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'"
 [pscustomobject]@{Time=(Get-Date).ToString('o');Pid=$rgb.Id;DeviceTab=2;Mode='Direct';Hex=$hex.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value;ServiceBefore=$before;ServiceAfter=$svc.State;ServiceStartMode=$svc.StartMode;ServicePid=$svc.ProcessId;ProfileSaved=$false} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'blue-after-service-restart.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'blue-after-service-restart-error.txt') -Encoding UTF8;exit 1}
