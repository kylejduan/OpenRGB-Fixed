param([ValidatePattern('^[0-9A-F]{6}$')][string]$Color='FF0000',[string]$Record='apply-color.json')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms
if(-not ('RgbFocus' -as [type])) {
 Add-Type @'
using System;using System.Runtime.InteropServices;
public static class RgbFocus {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
}
'@
}
$rgb=Get-Process OpenRGB
if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
if($rgb.MainWindowHandle -eq [IntPtr]::Zero) {throw 'OpenRGB window must be shown first'}
$root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
$tabs=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::TabItem))) | Where-Object {$_.Current.Name -eq ''})
if($tabs.Count -ne 5) {throw 'Device tab layout changed'}
$tabs[2].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
function BySuffix($suffix) {return @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | Where-Object {$_.Current.AutomationId.EndsWith($suffix) -and -not $_.Current.IsOffscreen})[0]}
$mode=BySuffix '.ControlsFrame.ModeBox'
$modeBefore=$mode.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
if($modeBefore -ne 'Direct') {
 [RgbFocus]::SetForegroundWindow($rgb.MainWindowHandle) | Out-Null
 $mode.SetFocus()
 [uint32]$owner=0;[RgbFocus]::GetWindowThreadProcessId([RgbFocus]::GetForegroundWindow(),[ref]$owner) | Out-Null
 if($owner -ne $rgb.Id) {throw 'OpenRGB does not own keyboard focus'}
 $mode.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 200
 [System.Windows.Forms.SendKeys]::SendWait('{HOME}{DOWN}{ENTER}')
}
if($mode.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -ne 'Direct') {throw 'Direct mode selection failed'}
$hex=BySuffix '.ColorEntryFrame.HexLineEdit'
$hex.SetFocus();$hex.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($Color)
$select=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'Select All')))
$select.SetFocus();$select.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
$apply=BySuffix '.ControlsFrame.ApplyColorsButton'
$apply.SetFocus();$apply.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
[pscustomobject]@{Time=(Get-Date).ToString('o');Pid=$rgb.Id;DeviceTab=2;ModeBefore=$modeBefore;Mode='Direct';Color=$Color;ProfileSaved=$false} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot $Record) -Encoding UTF8
