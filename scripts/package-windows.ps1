# SPDX-License-Identifier: GPL-2.0-or-later
# Run from a Developer PowerShell with Qt 5.15.2 x64 on PATH.
[CmdletBinding()]
param([int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if($Jobs -lt 1) { throw 'Jobs must be at least one' }
$sourceRoot = Split-Path $PSScriptRoot -Parent
Push-Location $sourceRoot
try {
    foreach($tool in @('cl.exe', 'qmake.exe', 'windeployqt.exe', 'git.exe')) {
        Get-Command $tool -ErrorAction Stop | Out-Null
    }
    $dist = Join-Path $sourceRoot 'build\dist'
    $toolsDir = Join-Path $sourceRoot 'build\tools'
    $package = Join-Path $sourceRoot 'build\package\OpenRGB-Fixed'
    New-Item -ItemType Directory -Force $dist, $toolsDir | Out-Null
    if(Test-Path $package) { throw 'build/package/OpenRGB-Fixed already exists; use a clean packaging directory' }
    $jomZip = Join-Path $toolsDir 'jom_1_1_4.zip'
    Invoke-WebRequest 'https://download.qt.io/official_releases/jom/jom_1_1_4.zip' -OutFile $jomZip
    if((Get-FileHash $jomZip -Algorithm SHA256).Hash -ne 'D533C1EF49214229681E90196ED2094691E8C4A0A0BEF0B2C901DEBCB562682B') {
        throw 'jom download checksum mismatch'
    }
    Expand-Archive $jomZip (Join-Path $toolsDir 'jom') -Force
    & qmake.exe OpenRGB.pro 'CONFIG-=debug_and_release' 'CONFIG+=release'
    if($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & (Join-Path $toolsDir 'jom\jom.exe') -j $Jobs
    if($LASTEXITCODE -ne 0) { throw 'jom build failed' }
    & windeployqt.exe --release --no-patchqt --no-quick-import --no-translations --no-system-d3d-compiler --no-compiler-runtime --no-opengl-sw --no-network .\release\OpenRGB.exe
    if($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
    New-Item -ItemType Directory -Force $package | Out-Null
    Get-ChildItem release -File | Where-Object {$_.Extension -in @('.exe','.dll','.bin')} | Copy-Item -Destination $package
    foreach($subdir in @('platforms','styles','imageformats','iconengines')) {
        if(Test-Path "release\$subdir") { Copy-Item "release\$subdir" $package -Recurse }
    }
    Copy-Item LICENSE, README.md, README.upstream.md, CONTRIBUTING.md, THIRD_PARTY_NOTICES.md $package
    Copy-Item Documentation (Join-Path $package 'Documentation') -Recurse
    if(Test-Path licenses) { Copy-Item licenses (Join-Path $package 'licenses') -Recurse }
    $revision = (& git.exe rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision' }
    $qtVersion = (& qmake.exe -query QT_VERSION).Trim()
    $files = @(Get-ChildItem $package -Recurse -File | ForEach-Object {
        [ordered]@{Path=$_.FullName.Substring($package.Length+1).Replace('\','/');SHA256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    [ordered]@{Project='OpenRGB Fixed';SourceRevision=$revision;SourceUrl="https://github.com/kylejduan/OpenRGB-Fixed/tree/$revision";QtVersion=$qtVersion;Files=$files} |
        ConvertTo-Json -Depth 4 | Set-Content (Join-Path $package 'BUILD-MANIFEST.json') -Encoding UTF8
    $archive = Join-Path $dist 'OpenRGB-Fixed-Windows-x64.zip'
    Compress-Archive -Path $package -DestinationPath $archive -Force
    $hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  OpenRGB-Fixed-Windows-x64.zip" | Set-Content (Join-Path $dist 'OpenRGB-Fixed-Windows-x64.zip.sha256') -Encoding ASCII
    Write-Output "Created $archive"
} finally {
    Pop-Location
}
