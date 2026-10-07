$ErrorActionPreference='Stop'
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe7.cs')
 $probe=New-Object HidppProbe7
 $feature=$probe.Request(7,0,0,[byte[]]@(0x80,0x70,0))
 if($feature[0] -eq 0) {throw 'Expected Powerplay 8070 feature'}
 $control=$probe.Request(7,$feature[0],0x70,[byte[]]@(0,0,0))
 $info=$probe.Request(7,$feature[0],0,[byte[]]@(0,0,0))
 [pscustomobject]@{Time=(Get-Date).ToString('o');SoftwareId=7;Feature=@($feature);Control=@($control);Info=@($info);Trace=@($probe.Trace)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'powerplay-identity7.json') -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'powerplay-identity7-error.txt');if($probe){$probe.Trace | Set-Content (Join-Path $PSScriptRoot 'powerplay-identity7-error-trace.txt')};exit 1}
finally {if($probe){$probe.Dispose()}}
