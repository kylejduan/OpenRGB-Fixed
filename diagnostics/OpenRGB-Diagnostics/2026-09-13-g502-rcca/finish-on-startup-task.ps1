$ErrorActionPreference='Stop'
try {
 . (Join-Path $PSScriptRoot 'runtime-helpers.ps1')
 Assert-MainProfile
 $current=@(Get-Process OpenRGB)
 if($current.Count -ne 1 -or $current[0].Path -ne $installedExe) {throw 'Expected one installed OpenRGB'}
 $beforeHash=(Get-FileHash $installedExe -Algorithm SHA256).Hash
 $task=Get-ScheduledTask OpenRGB
 if($task.Principal.RunLevel -ne 'Highest' -or $task.Actions.Execute -ne $installedExe -or $task.Actions.Arguments -ne '--startminimized --profile Main') {throw 'Startup task changed; inspect before continuing'}
 $exit=Stop-RgbNormally $current[0]
 if(Get-NetTCPConnection -LocalPort 6749 -State Listen -ErrorAction SilentlyContinue) {throw 'Temporary SDK listener still exists after clean exit'}
 $deadline=(Get-Date).AddSeconds(10)
 do {$task=Get-ScheduledTask OpenRGB;if($task.State -ne 'Running'){break};Start-Sleep -Milliseconds 250} while((Get-Date) -lt $deadline)
 if($task.State -eq 'Running') {throw 'Startup task still running after app exit'}
 Start-ScheduledTask OpenRGB
 $deadline=(Get-Date).AddSeconds(30)
 do {
  Start-Sleep -Milliseconds 500
  $running=@(Get-Process OpenRGB -ErrorAction SilentlyContinue)
  if($running.Count -eq 1 -and $running[0].Path -eq $installedExe) {break}
 } while((Get-Date) -lt $deadline)
 if($running.Count -ne 1 -or $running[0].Path -ne $installedExe) {throw 'Expected one installed app from scheduled task'}
 Start-Sleep -Seconds 12
 $running[0].Refresh();if($running[0].HasExited) {throw 'Startup app exited during detection'}
 if(Get-NetTCPConnection -LocalPort 6749 -State Listen -ErrorAction SilentlyContinue) {throw 'Temporary SDK port was unexpectedly reopened'}
 Assert-MainProfile
 if((Get-FileHash $installedExe -Algorithm SHA256).Hash -ne $beforeHash) {throw 'Executable changed during restart'}
 $task=Get-ScheduledTask OpenRGB
 $record=[pscustomobject]@{Time=(Get-Date).ToString('o');CleanExit=$exit;Pid=$running[0].Id;StartTime=$running[0].StartTime.ToString('o');SHA256=$beforeHash;Executable=$running[0].Path;MainSHA256=(Get-FileHash $mainProfile).Hash;TaskState=$task.State.ToString();RunLevel=$task.Principal.RunLevel.ToString();Arguments=$task.Actions.Arguments;WorkingDirectory=$task.Actions.WorkingDirectory;ExecutionTimeLimit=$task.Settings.ExecutionTimeLimit;TemporaryPortListening=$false;Services=@(Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service' OR Name='LGHUBUpdaterService'" | Select-Object Name,State,StartMode)}
 $record | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'final-task-restart.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'final-task-restart-error.txt');exit 1}
