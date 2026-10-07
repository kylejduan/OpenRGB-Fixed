$ErrorActionPreference='Stop'
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
 public static void Open(uint pid){var found=new List<IntPtr>();EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var s=new StringBuilder(128);if(id==pid){GetWindowText(h,s,s.Capacity);if(s.ToString()=="QTrayIconMessageWindow")found.Add(h);}return true;},IntPtr.Zero);if(found.Count!=1)throw new Exception("Expected one tray window");PostMessage(found[0],0x8065,IntPtr.Zero,new IntPtr(0x0205));}
}
'@
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 [RgbTray]::Open([uint32]$rgb.Id)
 Start-Sleep -Milliseconds 500
 $roots=[System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty,$rgb.Id)))
 $rows=@()
 foreach($root in $roots) {
  foreach($n in $root.FindAll([System.Windows.Automation.TreeScope]::Subtree,[System.Windows.Automation.Condition]::TrueCondition)) {
   if($n.Current.Name -match '(?i)exit|quit|hide|show|openrgb|main') {
    $rows += [pscustomobject]@{Name=$n.Current.Name;Type=$n.Current.ControlType.ProgrammaticName;Id=$n.Current.AutomationId;Enabled=$n.Current.IsEnabled;Offscreen=$n.Current.IsOffscreen;Patterns=@($n.GetSupportedPatterns() | ForEach-Object {$_.ProgrammaticName})}
   }
  }
 }
 [pscustomobject]@{RootCount=$roots.Count;Elements=$rows} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot 'tray-menu.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'tray-menu-error.txt') -Encoding UTF8;exit 1}
