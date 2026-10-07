param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$Revision,
 [ValidatePattern('^[0-9a-f]{64}$')][string]$ExpectedOldSha256='2b6a6d77bb46cdad0372d46446e7a0d8157e309b90d40f6f604ab6f3cd3cb2ea',
 [ValidatePattern('^[A-Za-z0-9-]+$')][string]$Label='fixed3'
)
$ErrorActionPreference='Stop'
$closed=$false;$copied=$false;$started=$false
$here=$PSScriptRoot
try {
 . 'C:\OpenRGB-Diagnostics\2026-09-13-g502-rcca\runtime-helpers.ps1'
 Assert-MainProfile
 $installRoot=Split-Path $installedExe
 $package=Join-Path $here "package-$Label\OpenRGB-Fixed"
 $manifest=Get-Content (Join-Path $package 'BUILD-MANIFEST.json') -Raw | ConvertFrom-Json
 if($manifest.SourceRevision -ne $Revision) {throw "Wrong source revision $($manifest.SourceRevision)"}
 foreach($file in $manifest.Files) {
  if((Get-FileHash (Join-Path $package $file.Path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.SHA256) {throw "Package checksum mismatch: $($file.Path)"}
 }
 $old=@(Get-Process OpenRGB -ErrorAction SilentlyContinue)
 if($old.Count -ne 1 -or $old[0].Path -ne $installedExe) {throw 'Expected exactly one installed OpenRGB process'}
 $oldHash=(Get-FileHash $installedExe -Algorithm SHA256).Hash.ToLowerInvariant()
 if($oldHash -ne $ExpectedOldSha256) {throw "Installed executable $oldHash is not the expected build"}
 $backup=Join-Path $here "rollback-before-$Label"
 if(Test-Path $backup) {throw 'Rollback directory already exists'}
 Copy-Item $installRoot $backup -Recurse
 $inventory=@(Get-ChildItem $installRoot -Recurse -File | ForEach-Object {
  $relative=$_.FullName.Substring($installRoot.Length+1)
  $hash=(Get-FileHash $_.FullName -Algorithm SHA256).Hash
  if((Get-FileHash (Join-Path $backup $relative) -Algorithm SHA256).Hash -ne $hash) {throw "Backup mismatch: $relative"}
  [pscustomobject]@{Path=$relative;SHA256=$hash}
 })
 $inventory | ConvertTo-Json | Set-Content (Join-Path $here "rollback-before-$Label-manifest.json") -Encoding UTF8
 Export-ScheduledTask OpenRGB | Set-Content (Join-Path $here "task-before-$Label.xml") -Encoding UTF8
 $exit=Stop-RgbNormally $old[0];$closed=$true
 Copy-Item (Join-Path $package '*') $installRoot -Recurse -Force;$copied=$true
 foreach($file in $manifest.Files) {
  if((Get-FileHash (Join-Path $installRoot $file.Path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.SHA256) {throw "Installed checksum mismatch: $($file.Path)"}
 }
 Assert-MainProfile
 Start-ScheduledTask -TaskName OpenRGB;$started=$true
 $deadline=(Get-Date).AddSeconds(30)
 do {Start-Sleep -Milliseconds 500;$new=@(Get-Process OpenRGB -ErrorAction SilentlyContinue)} while($new.Count -eq 0 -and (Get-Date) -lt $deadline)
 if($new.Count -ne 1 -or $new[0].Path -ne $installedExe) {throw 'Startup task did not launch the installed OpenRGB'}
 [pscustomobject]@{Time=(Get-Date).ToString('o');SourceRevision=$manifest.SourceRevision;OldSHA256=$oldHash;NewSHA256=(Get-FileHash $installedExe -Algorithm SHA256).Hash.ToLowerInvariant();CleanExit=$exit;NewPid=$new[0].Id;StartedBy='Scheduled task OpenRGB';MainSHA256=(Get-FileHash $mainProfile -Algorithm SHA256).Hash.ToLowerInvariant();PackageFiles=@($manifest.Files).Count;Rollback=$backup;Services=@(Get-Service logi_lamparray_service,LGHUBUpdaterService | Select-Object Name,Status,StartType)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $here "installed-$Label.json") -Encoding UTF8
} catch {
 $_ | Out-String | Set-Content (Join-Path $here "install-$Label-error.txt") -Encoding UTF8
 # Preserve all files and report the exact phase; no force-kill or blind rollback.
 [pscustomobject]@{Closed=$closed;Copied=$copied;Started=$started;Time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $here "install-$Label-error-state.json") -Encoding UTF8
 exit 1
}
