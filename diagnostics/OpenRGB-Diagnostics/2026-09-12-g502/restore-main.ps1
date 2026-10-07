$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $profile=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::ComboBox))) | Where-Object {$_.Current.AutomationId -like '*MainButtonsFrame*' -and $_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -eq 'Main'})
 if($profile.Count -ne 1) {throw 'Expected Main profile selection'}
 $load=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'Load Profile')))
 $load.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 600
 $service=Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'"
 $task=Get-ScheduledTask -TaskName OpenRGB
 $tabs=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::TabItem))) | Where-Object {$_.Current.Name -eq ''})
 [pscustomobject]@{Time=(Get-Date).ToString('o');Profile='Main';Action='Load Profile';ProfileSaved=$false;Pid=$rgb.Id;Responding=$rgb.Responding;DeviceCount=$tabs.Count;ServiceState=$service.State;ServiceStartMode=$service.StartMode;TaskState=$task.State.ToString();RunLevel=$task.Principal.RunLevel.ToString();ExecutableHash=(Get-FileHash $rgb.Path -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'restored-main.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'restore-main-error.txt') -Encoding UTF8;exit 1}
