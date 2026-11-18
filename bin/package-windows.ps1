[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $root 'build/lore.exe'
if (!(Test-Path $binary)) { throw 'Run bin/build.ps1 first' }
$package = Join-Path $root 'build/windows'
New-Item -ItemType Directory -Force -Path $package | Out-Null
Copy-Item $binary (Join-Path $package 'lore.exe') -Force
Copy-Item (Join-Path $root 'build/launcher/lore-desktop.exe') $package -Force
Copy-Item (Join-Path $root 'LICENSE') $package -Force
& windeployqt --release --qmldir (Join-Path $root 'src') (Join-Path $package 'lore.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# windeployqt includes qwindows, but headless hooks explicitly load qoffscreen.
# It is not inferred from the executable's static library dependencies.
$plugins = & qmake -query QT_INSTALL_PLUGINS
if ($LASTEXITCODE -ne 0) { throw 'Cannot locate Qt plugins' }
$offscreen = Join-Path $plugins.Trim() 'platforms/qoffscreen.dll'
if (!(Test-Path $offscreen)) { throw 'This Qt installation is missing qoffscreen.dll' }
Copy-Item $offscreen (Join-Path $package 'platforms/qoffscreen.dll') -Force
# App-local VC runtime DLLs let the per-user installer work without elevation.
$runtime = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
if (!(Test-Path (Join-Path $runtime 'vcruntime140_1.dll'))) { throw 'The Visual Studio x64 redistributable DLLs were not found.' }
Copy-Item (Join-Path $runtime '*.dll') $package -Force
Remove-Item (Join-Path $package 'vc_redist.x64.exe') -ErrorAction SilentlyContinue
Write-Output "Windows app: $package"
