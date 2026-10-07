$ErrorActionPreference='Stop'
try {
 $rgb=Get-Process OpenRGB
 $services=@(Get-CimInstance Win32_Service | Where-Object {$_.Name -match '(?i)logi|lghub|lighting|lamp'} | Select-Object Name,DisplayName,State,StartMode,ProcessId,PathName)
 $lighting=@()
 foreach($path in @('HKCU:\Software\Microsoft\Lighting','HKLM:\SOFTWARE\Microsoft\Lighting','HKCU:\Software\Microsoft\Windows\CurrentVersion\Lighting')) {
  if(Test-Path $path) {
   $lighting += [pscustomobject]@{Path=$path;Properties=(Get-ItemProperty $path | Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider);Children=@(Get-ChildItem $path -Recurse | ForEach-Object {[pscustomobject]@{Path=$_.Name;Properties=(Get-ItemProperty $_.PSPath | Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider)}})}
  }
 }
 $devices=@(Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -match 'VID_046D' -or $_.FriendlyName -match '(?i)G502|Powerplay|LampArray'} | Select-Object Class,FriendlyName,Status,InstanceId)
 $processes=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '(?i)openrgb|lghub|logi_lamp'} | Select-Object ProcessId,Name,ExecutablePath,CommandLine)
 $versions=@($processes | ForEach-Object {if($_.ExecutablePath -and (Test-Path $_.ExecutablePath)) {[pscustomobject]@{Name=$_.Name;Path=$_.ExecutablePath;Version=(Get-Item $_.ExecutablePath).VersionInfo.FileVersion;LastWriteTime=(Get-Item $_.ExecutablePath).LastWriteTime}}})
 $os=Get-CimInstance Win32_OperatingSystem
 [pscustomobject]@{Time=(Get-Date).ToString('o');LastBoot=$os.LastBootUpTime.ToString('o');OpenRGB=[pscustomobject]@{Pid=$rgb.Id;Path=$rgb.Path;Responding=$rgb.Responding;StartTime=$rgb.StartTime.ToString('o');MainWindow=$rgb.MainWindowHandle.ToInt64()};Services=$services;Lighting=$lighting;Devices=$devices;Processes=$processes;Versions=$versions} | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $PSScriptRoot 'host-during-marker-test.json') -Encoding UTF8
} catch { $_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'inspect-host-marker-error.txt') -Encoding UTF8; exit 1 }
