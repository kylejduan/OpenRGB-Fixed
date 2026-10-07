$ErrorActionPreference='Stop'
$probe=$null;$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs');$probe=New-Object HidppProbe
 $feature=$probe.Request(1,0,0,[byte[]]@(0x80,0x71,0));if($feature[0] -ne 9) {throw 'Feature map changed'}
 $control=$probe.Request(1,9,0x50,[byte[]]@(0,0,0));if($control[1] -ne 3) {throw 'Expected existing full software control'}
 $off=$probe.Request(1,9,0x80,[byte[]]@(1,3,0))
 $after=$probe.Request(1,9,0x80,[byte[]]@(0,0,0));if($after[1] -ne 3) {throw 'Power-off did not read back'}
 [pscustomobject]@{Time=(Get-Date).ToString('o');Control=@($control);Power=@($after);Trace=@($probe.Trace);Note='Only RGB power changed; existing software claim untouched'} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot "power-off-only-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "power-off-only-$stamp-error.txt");exit 1}
finally {if($probe){$probe.Dispose()}}
