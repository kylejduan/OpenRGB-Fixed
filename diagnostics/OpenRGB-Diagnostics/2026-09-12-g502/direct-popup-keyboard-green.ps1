$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms
 Add-Type @'
using System;using System.Runtime.InteropServices;
public static class RgbKeys {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
}
'@
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Id -ne 47300) {throw 'Unexpected OpenRGB instance'}
 [RgbKeys]::SetForegroundWindow($rgb.MainWindowHandle) | Out-Null
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $mode=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | Where-Object {$_.Current.AutomationId.EndsWith('.ControlsFrame.ModeBox') -and -not $_.Current.IsOffscreen})[0]
 $before=$mode.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 $mode.SetFocus()
 [uint32]$owner=0;[RgbKeys]::GetWindowThreadProcessId([RgbKeys]::GetForegroundWindow(),[ref]$owner) | Out-Null
 if($owner -ne $rgb.Id) {throw 'OpenRGB does not have keyboard focus'}
 $mode.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 200
 [System.Windows.Forms.SendKeys]::SendWait('{HOME}{DOWN}{ENTER}')
 Start-Sleep -Milliseconds 250
 $after=$mode.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 [pscustomobject]@{Time=(Get-Date).ToString('o');Pid=$rgb.Id;Before=$before;After=$after} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'direct-popup-keyboard.json') -Encoding UTF8
 if($after -ne 'Direct') {throw 'Keyboard selection did not establish Direct mode'}
 & (Join-Path $PSScriptRoot 'test-green-after-restart.ps1')
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'direct-popup-keyboard-error.txt') -Encoding UTF8;exit 1}
