param([ValidateSet('list','color','profile','rescan')][string]$Action='list',[string]$Device='G502 X PLUS',[ValidatePattern('^[0-9A-Fa-f]{6}$')][string]$Color='00FF00',[ValidateSet('','Direct','Static','Off')][string]$Mode='')
$ErrorActionPreference='Stop'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$label="$Action-$stamp"
try {
 $listener=Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 6749 -State Listen -ErrorAction Stop
 $server=Get-Process -Id $listener.OwningProcess
 if($server.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected server process'}
 if($Action -in @('rescan','profile')) {
  $packetId=if($Action -eq 'rescan'){140}else{152}
  [byte[]]$payload=@()
  if($Action -eq 'profile') {$payload=[Text.Encoding]::UTF8.GetBytes('Main')}
  $client=New-Object Net.Sockets.TcpClient
  try {
   $client.Connect('127.0.0.1',6749)
   $stream=$client.GetStream()
   $writer=New-Object IO.BinaryWriter($stream)
   $writer.Write([Text.Encoding]::ASCII.GetBytes('ORGB'))
   $writer.Write([uint32]0);$writer.Write([uint32]$packetId);$writer.Write([uint32]$payload.Length)
   $writer.Write($payload);$writer.Flush()
  } finally {$client.Dispose()}
  [pscustomobject]@{Time=(Get-Date).ToString('o');Action=$Action;ServerPid=$server.Id;PacketId=$packetId;Note='Request sent; verify fresh detection/profile logs separately'} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot "$label.json") -Encoding UTF8
 } else {
  if($Device -notin @('G502 X PLUS','Candy companion chip')) {throw 'Unexpected device selector'}
  $arguments=@('--client','127.0.0.1:6749','--nodetect','--noautoconnect')
  if($Action -eq 'list') {$arguments+='--list-devices'}
  else {
   $arguments+=@('--device',('"'+$Device+'"'),'--color',$Color)
   if($Mode) {$arguments+=@('--mode',$Mode)}
  }
  $process=Start-Process $server.Path -ArgumentList $arguments -WorkingDirectory (Split-Path $server.Path) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $PSScriptRoot "$label-out.txt") -RedirectStandardError (Join-Path $PSScriptRoot "$label-err.txt")
  $nativeHandle=$process.Handle
  if(-not $process.WaitForExit(20000)) {throw "CLI deadline exceeded: owned process $($process.Id)"}
  if($process.ExitCode -ne 0) {throw "CLI exit $($process.ExitCode)"}
  [pscustomobject]@{Time=(Get-Date).ToString('o');Action=$Action;Device=$Device;Color=$Color;Mode=$Mode;CliPid=$process.Id;ExitCode=$process.ExitCode;ServerPid=$server.Id} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot "$label.json") -Encoding UTF8
 }
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "$label-error.txt") -Encoding UTF8;exit 1}
