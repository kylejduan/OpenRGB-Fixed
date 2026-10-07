$ErrorActionPreference='Stop'
try {
 $info=New-Object Diagnostics.ProcessStartInfo
 $info.FileName=Join-Path $PSScriptRoot 'package-fixed2\OpenRGB-Fixed\OpenRGB.exe'
 $info.Arguments='--version'
 $info.WorkingDirectory=Split-Path $info.FileName
 $info.UseShellExecute=$false
 $info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true
 $info.RedirectStandardError=$true
 $process=New-Object Diagnostics.Process
 $process.StartInfo=$info
 if(-not $process.Start()) {throw 'Start failed'}
 $nativeHandle=$process.Handle
 if(-not $process.WaitForExit(10000)) {throw 'Version deadline'}
 $code=$process.ExitCode
 [pscustomobject]@{ExitCode=$code;Pid=$process.Id;Output=$process.StandardOutput.ReadToEnd();Error=$process.StandardError.ReadToEnd();SHA256=(Get-FileHash $info.FileName).Hash;Time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'fixed2-version-smoke-native.json') -Encoding UTF8
 $process.Dispose()
 if($code -ne 0) {throw "Version exit $code"}
} catch {$_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'fixed2-version-smoke-native-error.txt');exit 1}
