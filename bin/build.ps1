[CmdletBinding()]
param([string]$Make = 'nmake')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
  & qmake (Join-Path $root 'lore.pro')
  if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
  & $Make
  if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
} finally {
  Pop-Location
}
