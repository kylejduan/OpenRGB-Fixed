param([Parameter(Mandatory=$true)][ValidateSet('Stop','Start')][string]$Phase)
$ErrorActionPreference='Stop'
$receiptRoot=Join-Path $PSScriptRoot 'resume-profile'
try {
 . (Join-Path $PSScriptRoot 'runtime-helpers.ps1')
 Assert-MainProfile
 $expectedHash='2b6a6d77bb46cdad0372d46446e7a0d8157e309b90d40f6f604ab6f3cd3cb2ea'
 if((Get-FileHash $installedExe).Hash.ToLowerInvariant() -ne $expectedHash) {throw 'Installed build changed'}
 $settingsFile=Join-Path $env:APPDATA 'OpenRGB\OpenRGB.json'
 $task=Get-ScheduledTask OpenRGB
 if($task.Principal.RunLevel -ne 'Highest' -or $task.Actions.Execute -ne $installedExe -or $task.Actions.Arguments -ne '--startminimized --profile Main') {throw 'Unexpected startup task'}
 if($Phase -eq 'Stop') {
  $running=@(Get-Process OpenRGB)
  if($running.Count -ne 1 -or $running[0].Path -ne $installedExe) {throw 'Expected one installed OpenRGB'}
  if(Test-Path $receiptRoot) {throw 'Preserve existing resume-profile receipts'}
  New-Item -ItemType Directory $receiptRoot | Out-Null
  Copy-Item $settingsFile (Join-Path $receiptRoot 'before-stop.json')
  Export-ScheduledTask OpenRGB | Set-Content (Join-Path $receiptRoot 'startup-task-before.xml') -Encoding UTF8
  $exit=Stop-RgbNormally $running[0]
  Copy-Item $settingsFile (Join-Path $receiptRoot 'before-edit.json')
  $exit | ConvertTo-Json | Set-Content (Join-Path $receiptRoot 'clean-exit.json') -Encoding UTF8
 } else {
  # Windows PowerShell rejects valid JSON keys differing only by case in
  # OpenRGB's detector map. Python validates the complete settings document;
  # this launch guard uses its scoped change receipt and full-file checksum.
  $change=Get-Content (Join-Path $receiptRoot 'settings-change.json') -Raw | ConvertFrom-Json
  if((Get-FileHash $settingsFile).Hash.ToLowerInvariant() -ne $change.AfterSHA256) {throw 'Settings changed after validation'}
  $resume=$change.After
  if($resume.enabled -ne $true -or $resume.name -ne 'Main') {throw 'Resume profile not configured'}
  if(@(Get-Process OpenRGB -ErrorAction SilentlyContinue).Count -ne 0) {throw 'An OpenRGB process is already running'}
  if((Get-ScheduledTask OpenRGB).State -eq 'Running') {throw 'Startup task still running'}
  Start-ScheduledTask OpenRGB
  $deadline=(Get-Date).AddSeconds(15)
  do {
   Start-Sleep -Milliseconds 250
   $running=@(Get-Process OpenRGB -ErrorAction SilentlyContinue)
  } while($running.Count -eq 0 -and (Get-Date) -lt $deadline)
  if($running.Count -ne 1 -or $running[0].Path -ne $installedExe) {throw 'Expected one installed app from startup task'}
  Start-Sleep -Seconds 12
  $running[0].Refresh();if($running[0].HasExited) {throw 'OpenRGB exited during startup'}
  Assert-MainProfile
  if(Get-NetTCPConnection -LocalPort 6749 -State Listen -ErrorAction SilentlyContinue) {throw 'Unexpected test listener'}
  [pscustomobject]@{Time=(Get-Date).ToString('o');Pid=$running[0].Id;StartTime=$running[0].StartTime.ToString('o');Executable=$running[0].Path;SHA256=(Get-FileHash $installedExe).Hash;MainSHA256=(Get-FileHash $mainProfile).Hash;ResumeProfile=$resume;TaskState=(Get-ScheduledTask OpenRGB).State.ToString();TemporaryPortListening=$false;ActualSleepTested=$false} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $receiptRoot 'installed-state.json') -Encoding UTF8
 }
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "resume-profile-$Phase-error.txt") -Encoding UTF8;exit 1}
