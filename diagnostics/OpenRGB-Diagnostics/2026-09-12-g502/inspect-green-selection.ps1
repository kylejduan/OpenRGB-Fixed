$ErrorActionPreference='Stop'
try {
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
 Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;using System.Collections.Generic;
public static class RgbUi {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr p);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr d,uint f);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 public struct Rect {public int Left,Top,Right,Bottom;}
 public static void Show(uint pid){var found=new List<IntPtr>();EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var s=new StringBuilder(128);if(id==pid){GetWindowText(h,s,s.Capacity);if(s.ToString()=="QTrayIconMessageWindow")found.Add(h);}return true;},IntPtr.Zero);if(found.Count!=1)throw new Exception("Expected one tray window");PostMessage(found[0],0x8065,IntPtr.Zero,new IntPtr(0x0203));}
}
'@
 [RgbUi]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
 $rgb=Get-Process OpenRGB
 if(@($rgb).Count -ne 1 -or $rgb.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB process'}

 Start-Sleep -Milliseconds 300
 $root=[System.Windows.Automation.AutomationElement]::FromHandle($rgb.MainWindowHandle)
 $rows=@()
 foreach($n in $root.FindAll([System.Windows.Automation.TreeScope]::Subtree,[System.Windows.Automation.Condition]::TrueCondition)) {
  $value=$null;$selection=$null;$pattern=$null
  if($n.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)) {$value=$pattern.Current.Value}
  $pattern=$null
  if($n.TryGetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern,[ref]$pattern)) {$selection=@($pattern.Current.GetSelection() | ForEach-Object {$_.Current.Name})}
  $rows += [pscustomobject]@{Name=$n.Current.Name;Type=$n.Current.ControlType.ProgrammaticName;Class=$n.Current.ClassName;Id=$n.Current.AutomationId;Enabled=$n.Current.IsEnabled;Offscreen=$n.Current.IsOffscreen;Value=$value;Selection=$selection;Rectangle=$n.Current.BoundingRectangle.ToString();Patterns=@($n.GetSupportedPatterns() | ForEach-Object {$_.ProgrammaticName})}
 }
 $rows | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'ui-green-selection.json') -Encoding UTF8
 $r=New-Object RgbUi+Rect;[RgbUi]::GetWindowRect($rgb.MainWindowHandle,[ref]$r) | Out-Null
 $b=New-Object System.Drawing.Bitmap(($r.Right-$r.Left),($r.Bottom-$r.Top));$g=[System.Drawing.Graphics]::FromImage($b);$dc=$g.GetHdc();[RgbUi]::PrintWindow($rgb.MainWindowHandle,$dc,2) | Out-Null;$g.ReleaseHdc($dc);$b.Save((Join-Path $PSScriptRoot 'ui-green-selection.png'),[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$b.Dispose()
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'inspect-green-selection-error.txt') -Encoding UTF8;exit 1}
