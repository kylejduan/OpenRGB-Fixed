$ErrorActionPreference='Stop'
$probe=$null
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbe.cs')
 $probe=New-Object HidppProbe
 $features=@{}
 foreach($page in @(0x8071,0x8081,0x8100)) {
  $reply=$probe.Request(1,0,0,[byte[]]@(($page -shr 8),($page -band 255),0))
  $features[$page]=@($reply)
 }
 $f=[byte]$features[0x8071][0]
 $control=$probe.Request(1,$f,0x50,[byte[]]@(0,0,0))
 $power=$probe.Request(1,$f,0x80,[byte[]]@(0,0,0))
 $timeouts=$probe.Request(1,$f,0x70,[byte[]]@(0,0,0))
 $onboard=@()
 if($features[0x8100][0] -gt 0) {$onboard=$probe.Request(1,[byte]$features[0x8100][0],0x20,[byte[]]@(0,0,0))}
 [pscustomobject]@{Time=(Get-Date).ToString('o');Features=@($features.GetEnumerator() | ForEach-Object {[pscustomobject]@{Page=('{0:X4}' -f $_.Key);Reply=$_.Value}});Control=@($control);Power=@($power);Timeouts=@($timeouts);OnboardMode=@($onboard);Trace=@($probe.Trace)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot "lighting-state-$stamp.json") -Encoding UTF8
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot "lighting-state-$stamp-error.txt");if($probe){$probe.Trace | Set-Content (Join-Path $PSScriptRoot "lighting-state-$stamp-error-trace.txt")};exit 1}
finally {if($probe) {$probe.Dispose()}}
