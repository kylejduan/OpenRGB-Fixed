param([Parameter(Mandatory=$true)][string]$LogFile)
$ErrorActionPreference='Stop';$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
function Read-RgbLog {
 $file=[IO.File]::Open($LogFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 $reader=New-Object IO.StreamReader($file)
 try {return $reader.ReadToEnd()} finally {$reader.Dispose()}
}
try {
 $listener=Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 6749 -State Listen
 $server=Get-Process -Id $listener.OwningProcess;$nativeHandle=$server.Handle
 if($server.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected server'}
 $before=Read-RgbLog
 $beforeCount=[regex]::Matches($before,'Detection completed').Count
 & (Join-Path $PSScriptRoot 'sdk-command.ps1') -Action rescan
 if($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {throw 'Rescan request helper failed'}
 $deadline=(Get-Date).AddSeconds(45)
 do {
  Start-Sleep -Milliseconds 300;$server.Refresh()
  if($server.HasExited) {throw "OpenRGB exited during rescan: $($server.ExitCode)"}
  $now=Read-RgbLog
  $afterCount=[regex]::Matches($now,'Detection completed').Count
 } while($afterCount -le $beforeCount -and (Get-Date) -lt $deadline)
 if($afterCount -le $beforeCount) {throw 'No fresh detection completion before deadline'}
 $server.Refresh();if($server.HasExited) {throw 'OpenRGB exited after detection'}
 if((Get-FileHash (Join-Path $env:APPDATA 'OpenRGB\Main.orp')).Hash.ToLowerInvariant() -ne '8a286ce6feb191c7e199944d3509ac3da21573efd95f531ecf4f50f84ba473cd') {throw 'Main profile changed'}
 [pscustomobject]@{Time=(Get-Date).ToString('o');Pid=$server.Id;LogFile=$LogFile;BeforeCompletions=$beforeCount;AfterCompletions=$afterCount;Exited=$server.HasExited;MainRestoredRequested=$false} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot "rescan-check-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "rescan-check-$stamp-error.txt");exit 1}
