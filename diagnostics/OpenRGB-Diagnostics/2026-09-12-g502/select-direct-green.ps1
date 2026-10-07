$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Id -ne 47300) {throw 'Unexpected OpenRGB instance'}
 $desktop=[System.Windows.Automation.AutomationElement]::RootElement
 $items=@($desktop.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty,$rgb.Id))) | Where-Object {$_.Current.Name -eq 'Direct' -and $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and -not $_.Current.IsOffscreen})
 if($items.Count -eq 0) {throw 'Direct choice is not visible'}
 $items[0].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 200
 & (Join-Path $PSScriptRoot 'test-green-after-restart.ps1')
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'select-direct-green-error.txt') -Encoding UTF8;exit 1}
