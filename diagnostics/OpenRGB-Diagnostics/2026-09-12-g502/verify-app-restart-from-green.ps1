$ErrorActionPreference='Stop'
try {
 Add-Type @'
using System; using System.Runtime.InteropServices;
public static class RgbClose {
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
}
'@
 $old=Get-Process OpenRGB
 if(@($old).Count -ne 1 -or $old.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instance'}
 $oldId=$old.Id;$oldHandle=$old.Handle;$window=$old.MainWindowHandle
 if($window -eq [IntPtr]::Zero) {throw 'Show the OpenRGB window before testing close'}
 # Its closeEvent first hides a visible window when minimize-on-close is set.
 # Closing that same hidden window then follows OpenRGB's normal exit path.
 [RgbClose]::PostMessage($window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
 Start-Sleep -Milliseconds 700
 $old.Refresh()
 if(-not $old.HasExited) {
  [uint32]$owner=0;[RgbClose]::GetWindowThreadProcessId($window,[ref]$owner) | Out-Null
  if($owner -ne $oldId) {throw 'Window owner changed; second close not attempted'}
  [RgbClose]::PostMessage($window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
 }
 if(-not $old.WaitForExit(12000)) {throw 'OpenRGB did not exit normally within the deadline'}
 $exitCode=$old.ExitCode
 if($exitCode -ne 0) {throw "OpenRGB exited with code $exitCode"}
 $deadline=(Get-Date).AddSeconds(10)
 while((Get-ScheduledTask OpenRGB).State -eq 'Running' -and (Get-Date) -lt $deadline) {Start-Sleep -Milliseconds 250}
 if((Get-ScheduledTask OpenRGB).State -eq 'Running') {throw 'Task has not observed the clean exit'}
 Start-ScheduledTask -TaskName OpenRGB
 $deadline=(Get-Date).AddSeconds(20)
 do {Start-Sleep -Milliseconds 500;$new=@(Get-Process OpenRGB -ErrorAction SilentlyContinue)} while($new.Count -eq 0 -and (Get-Date) -lt $deadline)
 if($new.Count -ne 1) {throw 'Expected one new OpenRGB process'}
 Start-Sleep -Seconds 3
 $task=Get-ScheduledTask OpenRGB
 $svc=Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'"
 [pscustomobject]@{Time=(Get-Date).ToString('o');OldPid=$oldId;CleanExitCode=$exitCode;NewPid=$new[0].Id;Responding=$new[0].Responding;TaskState=$task.State.ToString();RunLevel=$task.Principal.RunLevel.ToString();Arguments=$task.Actions.Arguments;ServiceState=$svc.State;ServiceStartMode=$svc.StartMode;OSRestarted=$false} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'app-restart-from-green.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'app-restart-from-green-error.txt') -Encoding UTF8;exit 1}
