$ErrorActionPreference='Stop'
$client=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
function Send-Packet([uint32]$Id,[uint32]$Index,[byte[]]$Payload) {
 $writer.Write([Text.Encoding]::ASCII.GetBytes('ORGB'));$writer.Write($Index);$writer.Write($Id);$writer.Write([uint32]$Payload.Length)
 if($Payload.Length -gt 0) {$writer.Write($Payload)}
 $writer.Flush()
}
function Read-Reply([uint32]$Id,[uint32]$Index) {
 for($attempt=0;$attempt -lt 16;$attempt++) {
  $header=$reader.ReadBytes(16)
  if($header.Length -ne 16 -or [Text.Encoding]::ASCII.GetString($header,0,4) -ne 'ORGB') {throw 'Invalid SDK reply header'}
  $size=[BitConverter]::ToUInt32($header,12)
  if($size -gt 1048576) {throw 'Unexpected SDK reply size'}
  $payload=$reader.ReadBytes($size)
  if($payload.Length -ne $size) {throw 'Truncated SDK payload'}
  if([BitConverter]::ToUInt32($header,8) -eq $Id -and [BitConverter]::ToUInt32($header,4) -eq $Index) {return ,$payload}
 }
 throw 'No matching SDK response'
}
try {
 $listener=Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 6749 -State Listen -ErrorAction Stop
 $server=Get-Process -Id $listener.OwningProcess
 if($server.Path -ne 'C:\Program Files\OpenRGB\OpenRGB.exe') {throw 'Unexpected server executable'}
 $client=New-Object Net.Sockets.TcpClient;$client.Connect('127.0.0.1',6749)
 $stream=$client.GetStream();$stream.ReadTimeout=3000;$stream.WriteTimeout=3000
 $writer=New-Object IO.BinaryWriter($stream);$reader=New-Object IO.BinaryReader($stream)
 Send-Packet 0 0 ([byte[]]@())
 $countReply=Read-Reply 0 0
 if($countReply.Length -ne 4) {throw 'Invalid count reply'}
 $count=[BitConverter]::ToUInt32($countReply,0)
 if($count -lt 1 -or $count -gt 64) {throw 'Unexpected controller count'}
 $descriptions=@()
 for([uint32]$i=0;$i -lt $count;$i++) {
  Send-Packet 1 $i ([BitConverter]::GetBytes([uint32]0))
  $description=Read-Reply 1 $i
  if($description.Length -lt 10) {throw 'Invalid device description'}
  $descriptions+=[pscustomobject]@{Index=$i;Data=[Convert]::ToBase64String($description)}
 }
 [pscustomobject]@{Time=(Get-Date).ToString('o');ServerPid=$server.Id;Protocol=0;Controllers=$descriptions} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "sdk-inventory-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "sdk-inventory-$stamp-error.txt") -Encoding UTF8;exit 1}
finally {if($client){$client.Dispose()}}
