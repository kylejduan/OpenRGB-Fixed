$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 if((Get-Service logi_lamparray_service).Status -ne 'Stopped') {throw 'Lighting service is not stopped'}
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $rescan=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'Rescan Devices')))
 if($null -eq $rescan -or -not $rescan.Current.IsEnabled) {throw 'Rescan button unavailable'}
 $started=Get-Date
 $rescan.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Seconds 3
 $rows=@()
 foreach($n in $root.FindAll([System.Windows.Automation.TreeScope]::Subtree,[System.Windows.Automation.Condition]::TrueCondition)) {
  $value=$null;$pattern=$null
  if($n.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)) {$value=$pattern.Current.Value}
  $rows += [pscustomobject]@{Name=$n.Current.Name;Type=$n.Current.ControlType.ProgrammaticName;Id=$n.Current.AutomationId;Enabled=$n.Current.IsEnabled;Offscreen=$n.Current.IsOffscreen;Value=$value}
 }
 [pscustomobject]@{Started=$started.ToString('o');Time=(Get-Date).ToString('o');Pid=$rgb.Id;Service=(Get-Service logi_lamparray_service).Status.ToString();Elements=$rows} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'rescan-without-lamparray.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'rescan-without-lamparray-error.txt') -Encoding UTF8;exit 1}
