$ErrorActionPreference='Stop'
$closed=$false;$copied=$false;$new=$null
try {
 . (Join-Path $PSScriptRoot 'runtime-helpers.ps1')
 Assert-MainProfile
 $installRoot=Split-Path $installedExe
 $package=Join-Path $PSScriptRoot 'package-fixed2\OpenRGB-Fixed'
 $manifest=Get-Content (Join-Path $package 'BUILD-MANIFEST.json') -Raw | ConvertFrom-Json
 if($manifest.SourceRevision -ne 'efe23dcc310e326c35900d79957aa3f153c18708') {throw 'Wrong source revision'}
 foreach($file in $manifest.Files) {
  if((Get-FileHash (Join-Path $package $file.Path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.SHA256) {throw "Package checksum mismatch: $($file.Path)"}
 }
 $old=@(Get-Process OpenRGB)
 if($old.Count -ne 1 -or $old[0].Path -ne $installedExe) {throw 'Expected installed OpenRGB only'}
 $oldHash=(Get-FileHash $installedExe -Algorithm SHA256).Hash.ToLowerInvariant()
 if($oldHash -ne '04a7eb2b1fab4cef296cb60c99348a1ac7df4e4a5803e73cf1a76418457d6845') {throw 'Installed executable changed since baseline'}
 $backup=Join-Path $PSScriptRoot 'rollback-before-fixed2'
 if(Test-Path $backup) {throw 'Rollback directory already exists'}
 Copy-Item $installRoot $backup -Recurse
 $inventory=@(Get-ChildItem $installRoot -Recurse -File | ForEach-Object {
  $relative=$_.FullName.Substring($installRoot.Length+1)
  $hash=(Get-FileHash $_.FullName -Algorithm SHA256).Hash
  if((Get-FileHash (Join-Path $backup $relative) -Algorithm SHA256).Hash -ne $hash) {throw "Backup mismatch: $relative"}
  [pscustomobject]@{Path=$relative;SHA256=$hash}
 })
 $inventory | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'rollback-before-fixed2-manifest.json') -Encoding UTF8
 Export-ScheduledTask OpenRGB | Set-Content (Join-Path $PSScriptRoot 'task-before-fixed2.xml') -Encoding UTF8
 $exit=Stop-RgbNormally $old[0];$closed=$true
 Copy-Item (Join-Path $package '*') $installRoot -Recurse -Force;$copied=$true
 foreach($file in $manifest.Files) {
  if((Get-FileHash (Join-Path $installRoot $file.Path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.SHA256) {throw "Installed checksum mismatch: $($file.Path)"}
 }
 Assert-MainProfile
 $new=Start-RgbTestServer
 [pscustomobject]@{Time=(Get-Date).ToString('o');SourceRevision=$manifest.SourceRevision;OldSHA256=$oldHash;NewSHA256=(Get-FileHash $installedExe -Algorithm SHA256).Hash;CleanExit=$exit;NewPid=$new.Id;Executable=$new.Path;MainSHA256=(Get-FileHash $mainProfile).Hash;PackageFiles=$manifest.Files.Count;Rollback=$backup;TestServer='127.0.0.1:6749';Services=@(Get-Service logi_lamparray_service,LGHUBUpdaterService | Select-Object Name,Status)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $PSScriptRoot 'installed-fixed2.json') -Encoding UTF8
} catch {
 $_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'install-fixed2-error.txt') -Encoding UTF8
 # Preserve all files and report the exact phase; no force-kill or blind rollback.
 [pscustomobject]@{Closed=$closed;Copied=$copied;NewPid=if($new){$new.Id}else{$null};Time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'install-fixed2-error-state.json') -Encoding UTF8
 exit 1
}
