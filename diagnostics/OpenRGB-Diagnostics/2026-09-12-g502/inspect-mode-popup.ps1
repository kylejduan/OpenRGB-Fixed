$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $all=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition))
 $mode=@($all | Where-Object {$_.Current.AutomationId.EndsWith('.ControlsFrame.ModeBox') -and -not $_.Current.IsOffscreen})[0]
 $before=$mode.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 $mode.SetFocus()
 $mode.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 200
 $desktop=[System.Windows.Automation.AutomationElement]::RootElement
 $items=@($desktop.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty,$rgb.Id))) | Where-Object {-not $_.Current.IsOffscreen} | ForEach-Object {
  [pscustomobject]@{Name=$_.Current.Name;Type=$_.Current.ControlType.ProgrammaticName;Id=$_.Current.AutomationId;Patterns=@($_.GetSupportedPatterns() | ForEach-Object {$_.ProgrammaticName})}
 })
 [pscustomobject]@{Time=(Get-Date).ToString('o');Pid=$rgb.Id;ModeBefore=$before;Items=$items} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'mode-popup.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'mode-popup-error.txt') -Encoding UTF8;exit 1}
