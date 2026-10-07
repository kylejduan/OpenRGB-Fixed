$ErrorActionPreference='Stop'
$installedExe='C:\Program Files\OpenRGB\OpenRGB.exe'
$mainProfile=Join-Path $env:APPDATA 'OpenRGB\Main.orp'
$mainHash='8a286ce6feb191c7e199944d3509ac3da21573efd95f531ecf4f50f84ba473cd'
Add-Type @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class RgbWindow {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static void ShowTray(uint pid) {
  var found=new List<IntPtr>();
  EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var s=new StringBuilder(128);GetWindowText(h,s,s.Capacity);if(s.ToString()=="QTrayIconMessageWindow")found.Add(h);}return true;},IntPtr.Zero);
  if(found.Count!=1)throw new Exception("Expected one tray window");
  PostMessage(found[0],0x8065,IntPtr.Zero,new IntPtr(0x0203));
 }
}
'@
function Assert-MainProfile {
 if((Get-FileHash $mainProfile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $mainHash) {throw 'Main profile changed; preserve and inspect before proceeding'}
}
function Stop-RgbNormally($Process) {
 if($Process.Path -ne $installedExe) {throw 'Unexpected executable path'}
 $processId=$Process.Id; $nativeHandle=$Process.Handle
 if($Process.MainWindowHandle -eq [IntPtr]::Zero) {[RgbWindow]::ShowTray($processId);Start-Sleep -Milliseconds 500;$Process.Refresh()}
 $window=$Process.MainWindowHandle
 if($window -eq [IntPtr]::Zero) {throw 'No main window for clean exit'}
 [RgbWindow]::PostMessage($window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
 Start-Sleep -Milliseconds 700
 $Process.Refresh()
 if(-not $Process.HasExited) {
  [uint32]$owner=0;[RgbWindow]::GetWindowThreadProcessId($window,[ref]$owner) | Out-Null
  if($owner -ne $processId) {throw 'Window owner changed'}
  [RgbWindow]::PostMessage($window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
 }
 if(-not $Process.WaitForExit(15000)) {throw 'Clean exit deadline exceeded'}
 if($Process.ExitCode -ne 0) {throw "Unexpected exit code $($Process.ExitCode)"}
 return [pscustomobject]@{Pid=$processId;ExitCode=$Process.ExitCode;Time=(Get-Date).ToString('o')}
}
function Start-RgbTestServer {
 if(@(Get-Process OpenRGB -ErrorAction SilentlyContinue).Count -ne 0) {throw 'OpenRGB is already running'}
 if(Get-NetTCPConnection -LocalPort 6749 -State Listen -ErrorAction SilentlyContinue) {throw 'Test port already occupied'}
 $process=Start-Process $installedExe -WorkingDirectory (Split-Path $installedExe) -ArgumentList @('--gui','--server','--server-host','127.0.0.1','--server-port','6749','--profile','Main','--loglevel','6') -PassThru
 $deadline=(Get-Date).AddSeconds(30)
 do {
  Start-Sleep -Milliseconds 500;$process.Refresh()
  if($process.HasExited) {throw 'New OpenRGB exited during startup'}
  $listener=Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 6749 -State Listen -ErrorAction SilentlyContinue
 } while(-not $listener -and (Get-Date) -lt $deadline)
 if(-not $listener -or $listener.OwningProcess -ne $process.Id) {throw 'Test server listener not established'}
 return $process
}
