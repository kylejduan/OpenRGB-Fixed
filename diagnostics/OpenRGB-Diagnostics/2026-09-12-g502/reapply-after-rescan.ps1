$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 if((Get-Service logi_lamparray_service).Status -ne 'Stopped') {throw 'Lighting service is not stopped'}
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $profile=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::ComboBox))) | Where-Object {$_.Current.AutomationId -like '*MainButtonsFrame*' -and $_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -eq 'Main'})
 if($profile.Count -ne 1) {throw 'Expected Main profile to be selected'}
 $load=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'Load Profile')))
 $load.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 500
 & (Join-Path $PSScriptRoot 'test-mouse-red.ps1')
 if($LASTEXITCODE -eq 1) {throw 'Red test script failed'}
 Copy-Item (Join-Path $PSScriptRoot 'red-test.json') (Join-Path $PSScriptRoot 'red-after-rescan-without-lamparray.json')
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'reapply-after-rescan-error.txt') -Encoding UTF8;exit 1}
