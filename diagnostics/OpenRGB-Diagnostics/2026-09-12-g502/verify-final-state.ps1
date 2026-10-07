$ErrorActionPreference='Stop'
try {
 $rgb=@(Get-Process OpenRGB -ErrorAction Stop)
 if($rgb.Count -ne 1 -or $rgb[0].Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected OpenRGB instances'}
 $task=Get-ScheduledTask OpenRGB
 $service=Get-CimInstance Win32_Service -Filter "Name='logi_lamparray_service'"
 $profile=Join-Path $env:APPDATA 'OpenRGB\Main.orp'
 $beforeHash=(Get-FileHash (Join-Path $PSScriptRoot 'before-Main.orp') -Algorithm SHA256).Hash
 $currentHash=(Get-FileHash $profile -Algorithm SHA256).Hash
 $probes=@(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object {$_.ProcessId -ne $PID -and $_.CommandLine -like '*OpenRGB-Diagnostics*2026-09-12-g502*'} | Select-Object ProcessId,Name)
 $events=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000,1001;StartTime=[datetime]'2026-09-12T23:00:00'} -ErrorAction SilentlyContinue | Where-Object {$_.Message -match 'OpenRGB'})
 [pscustomobject]@{
 Time=(Get-Date).ToString('o');Pid=$rgb[0].Id;Responding=$rgb[0].Responding;Path=$rgb[0].Path;
 ExecutableHash=(Get-FileHash $rgb[0].Path -Algorithm SHA256).Hash;
 TaskState=$task.State.ToString();RunLevel=$task.Principal.RunLevel.ToString();Arguments=$task.Actions.Arguments;
 WorkingDirectory=$task.Actions.WorkingDirectory;ServiceState=$service.State;ServiceStartMode=$service.StartMode;
 ProfileHash=$currentHash;ProfileMatchesBefore=($currentHash -eq $beforeHash);OtherProbeProcesses=$probes;
 OpenRGBCrashEventsSinceStart=$events.Count;OSRestarted=$false
 } | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'final-state.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'final-state-error.txt') -Encoding UTF8;exit 1}
