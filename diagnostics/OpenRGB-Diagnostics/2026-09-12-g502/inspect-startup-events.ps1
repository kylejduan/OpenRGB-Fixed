$ErrorActionPreference='Stop'
try {
 $task=Get-ScheduledTask -TaskName OpenRGB
 Export-ScheduledTask -TaskName OpenRGB | Set-Content (Join-Path $PSScriptRoot 'OpenRGB-task-before.xml') -Encoding Unicode
 $since=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime
 $events=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$since;Id=@(42,107,506,507,7036,1)} -ErrorAction SilentlyContinue | Where-Object {($_.ProviderName -eq 'Service Control Manager' -and $_.Message -match '(?i)logitech|lamparray|lghub') -or $_.ProviderName -eq 'Microsoft-Windows-Power-Troubleshooter' -or ($_.ProviderName -eq 'Microsoft-Windows-Kernel-Power' -and $_.Id -in @(42,107,506,507))} | Select-Object TimeCreated,Id,ProviderName,Message)
 $processes=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '(?i)openrgb|lghub|logi_lamp'} | Select-Object ProcessId,Name,CreationDate,ExecutablePath)
 $taskInfo=Get-ScheduledTaskInfo -TaskName OpenRGB
 [pscustomobject]@{Time=(Get-Date).ToString('o');TaskState=$task.State.ToString();TaskLastResult=$taskInfo.LastTaskResult;LastRun=$taskInfo.LastRunTime;TaskRunLevel=$task.Principal.RunLevel.ToString();Processes=$processes;Events=$events;OpenRGBHash=(Get-FileHash 'C:\Program Files\OpenRGB\OpenRGB.exe' -Algorithm SHA256).Hash} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'startup-events.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'startup-events-error.txt') -Encoding UTF8;exit 1}
