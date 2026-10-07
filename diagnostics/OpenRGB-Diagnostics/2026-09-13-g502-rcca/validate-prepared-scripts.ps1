$ErrorActionPreference='Stop';$results=@()
foreach($name in @('install-wake-fixed2.ps1','runtime-helpers.ps1','sdk-color-only.ps1','sdk-inventory.ps1','sdk-command.ps1','rescan-once.ps1','finish-on-startup-task.ps1')) {
 $path=Join-Path $PSScriptRoot $name;$tokens=$null;$errors=$null
 [Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors) | Out-Null
 if($errors.Count -gt 0) {throw ($name+': '+($errors | Out-String))}
 $results+=[pscustomobject]@{Script=$name;SHA256=(Get-FileHash $path).Hash;Syntax='pass'}
}
$results | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'prepared-scripts-syntax.json') -Encoding UTF8
