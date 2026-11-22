$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($compiler) { $compiler = $compiler.Source }
else { $compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
if (!(Test-Path $compiler)) { throw 'Install Inno Setup 6 to build the Windows installer.' }
& $compiler (Join-Path $root 'pkgbuild/windows.iss')
if ($LASTEXITCODE -ne 0) { throw 'Windows installer build failed' }
