[CmdletBinding()]
param([string]$Make = 'nmake')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build/tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
$names = @('QT_QPA_PLATFORM', 'QT_QPA_PLATFORMTHEME', 'QT_QUICK_BACKEND', 'QT_FORCE_STDERR_LOGGING')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
  & qmake (Join-Path $root 'tests/lore_tests.pro')
  if ($LASTEXITCODE -ne 0) { throw 'Test qmake failed' }
  & $Make
  if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
  $env:QT_QPA_PLATFORM = 'offscreen'
  $env:QT_QPA_PLATFORMTHEME = 'generic'
  $env:QT_QUICK_BACKEND = 'software'
  $env:QT_FORCE_STDERR_LOGGING = '1'
  & .\lore_tests.exe
  if ($LASTEXITCODE -ne 0) { throw 'Qt tests failed' }
} finally {
  foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
  Pop-Location
}
