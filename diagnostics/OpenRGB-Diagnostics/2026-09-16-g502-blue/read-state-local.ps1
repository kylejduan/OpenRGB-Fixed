param([int]$Slot=1)
$ErrorActionPreference='Stop'
$probe=$null
try {
 Add-Type -Path (Join-Path $PSScriptRoot 'HidppProbeLocal.cs')
 $probe=New-Object HidppProbeLocal
 $t0=Get-Date
 $name=$probe.Request([byte]$Slot,0,0,[byte[]]@(0x00,0x05,0))
 $feature=$probe.Request([byte]$Slot,0,0,[byte[]]@(0x80,0x71,0))
 $out=[ordered]@{Time=$t0.ToString('o');Slot=$Slot;NameFeature=$name[0];Rgb8071=$feature[0]}
 if($feature[0] -ne 0) {
  $c=$probe.Request([byte]$Slot,$feature[0],0x50,[byte[]]@(0,0,0))
  $p=$probe.Request([byte]$Slot,$feature[0],0x80,[byte[]]@(0,0,0))
  $out.Control=$c[1];$out.Events=$c[2];$out.Power=$p[1]
 }
 $out.ElapsedMs=[int]((Get-Date)-$t0).TotalMilliseconds
 $out.Trace=@($probe.Trace)
 [pscustomobject]$out | ConvertTo-Json -Depth 4
} catch {"ERROR: $($_.Exception.Message)"; if($probe){$probe.Trace}; exit 1}
finally {if($probe){$probe.Dispose()}}
