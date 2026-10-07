$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;using System.Collections.Generic;
public static class RgbTray {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr p);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static void Show(uint pid){var found=new List<IntPtr>();EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var s=new StringBuilder(128);if(id==pid){GetWindowText(h,s,s.Capacity);if(s.ToString()=="QTrayIconMessageWindow")found.Add(h);}return true;},IntPtr.Zero);if(found.Count!=1)throw new Exception("Expected one tray window");PostMessage(found[0],0x8065,IntPtr.Zero,new IntPtr(0x0203));}
}
'@
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 if($rgb.MainWindowHandle -eq [IntPtr]::Zero) {[RgbTray]::Show([uint32]$rgb.Id);Start-Sleep -Milliseconds 800;$rgb.Refresh()}
 if($rgb.MainWindowHandle -eq [IntPtr]::Zero) {throw 'No main window'}
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $profile=@($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::ComboBox))) | Where-Object {$_.Current.AutomationId -like '*MainButtonsFrame*' -and $_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -eq 'Main'})
 if($profile.Count -ne 1) {throw 'Expected Main profile selection'}
 $load=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'Load Profile')))
 $t0=Get-Date
 $load.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
 Start-Sleep -Milliseconds 1500
 [pscustomobject]@{Time=$t0.ToString('o');Profile='Main';Action='GUI Load Profile';Pid=$rgb.Id;ProfileSaved=$false;Service=(Get-Service logi_lamparray_service).Status.ToString()} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot "reload-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "reload-$stamp-error.txt") -Encoding UTF8;exit 1}
