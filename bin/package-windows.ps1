[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $root 'build/lore.exe'
if (!(Test-Path $binary)) { throw 'Run bin/build.ps1 first' }
$package = Join-Path $root 'build/windows'
New-Item -ItemType Directory -Force -Path $package | Out-Null
Copy-Item $binary (Join-Path $package 'lore.exe') -Force
Copy-Item (Join-Path $root 'LICENSE') $package -Force
& windeployqt --release --qmldir (Join-Path $root 'src') (Join-Path $package 'lore.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
Write-Output "Windows app: $package"
