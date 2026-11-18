$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('Lore-install-' + [guid]::NewGuid())
$destination = Join-Path $scratch "Lore's app folder"
$project = Join-Path $scratch 'project-memory'
$image = Join-Path $scratch 'installed-ui.png'
$setup = Join-Path $root 'build/installer/Lore-Setup-0.3.2.exe'
$evidence = Join-Path $root 'build/install-check'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$old = @{}
$names = @('LORE_PREFERENCES_FILE', 'LORE_SKIP_TOOL_CHECKS', 'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')
foreach ($name in $names) { $old[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Run-Checked {
  param([string]$Executable, [string[]]$Arguments)
  $start = [Diagnostics.ProcessStartInfo]::new($Executable)
  $start.UseShellExecute = $false
  foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
  $process = [Diagnostics.Process]::Start($start)
  try {
    if (!$process.WaitForExit(60000)) { $process.Kill($true); throw "Timed out: $Executable" }
    if ($process.ExitCode -ne 0) { throw "Exit $($process.ExitCode): $Executable" }
  } finally { $process.Dispose() }
}
try {
  New-Item -ItemType Directory -Path $scratch | Out-Null
  Run-Checked -Executable $setup -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=$destination", "/LOG=$scratch/setup.log")
  if (!(Test-Path (Join-Path $destination 'lore-desktop.exe'))) { throw 'Desktop app was not installed' }
  if (!(Test-Path (Join-Path $destination 'vcruntime140_1.dll'))) { throw 'App-local Microsoft runtime was not installed' }
  $shortcut = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs/Lore.lnk'
  if (!(Test-Path $shortcut)) { throw 'Start menu shortcut was not created' }
  $target = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut).TargetPath
  if ($target -ne (Join-Path $destination 'lore-desktop.exe')) { throw 'Shortcut does not open the desktop app' }
  $env:LORE_PREFERENCES_FILE = Join-Path $scratch 'preferences.json'
  $env:LORE_SKIP_TOOL_CHECKS = '1'
  # Exercise the installed Windows GUI and system fonts, rather than the
  # offscreen plugin used by headless hooks and unit tests.
  $env:QT_QPA_PLATFORM = 'windows'
  $env:QT_QUICK_BACKEND = 'software'
  # Verify the installed app using only its own DLLs, plugins, and QML modules.
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  $env:QT_PLUGIN_PATH = $null
  $env:QML_IMPORT_PATH = $null
  $env:QML2_IMPORT_PATH = $null
  Run-Checked -Executable (Join-Path $destination 'lore-desktop.exe') -Arguments @('--screenshot', $image)
  for ($retry = 0; $retry -lt 40 -and !(Test-Path $image); $retry++) { Start-Sleep -Milliseconds 250 }
  if (!(Test-Path $image)) { throw 'Installed app did not render its setup screen' }
  $png = [IO.File]::ReadAllBytes($image)
  if ($png.Length -lt 1000 -or [BitConverter]::ToString($png, 0, 8) -ne '89-50-4E-47-0D-0A-1A-0A') { throw 'Installed app did not save a valid screenshot' }
  Copy-Item $image (Join-Path $evidence 'installed-ui.png') -Force
  New-Item -ItemType Directory -Path $project | Out-Null
  Run-Checked -Executable (Join-Path $destination 'lore.exe') -Arguments @('init', '--project', $project, '--json')
  Run-Checked -Executable (Join-Path $destination 'unins000.exe') -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
  for ($retry = 0; $retry -lt 40 -and (Test-Path (Join-Path $destination 'lore.exe')); $retry++) { Start-Sleep -Milliseconds 250 }
  if (Test-Path (Join-Path $destination 'lore.exe')) { throw 'Uninstall did not remove the app' }
  if (Test-Path $shortcut) { throw 'Uninstall did not remove the Start menu shortcut' }
  if (!(Test-Path (Join-Path $project '.lore/brain.sqlite'))) { throw 'Uninstall removed project memory' }
} finally {
  $startup = Join-Path $env:LOCALAPPDATA 'Lore/startup.log'
  if (Test-Path $startup) {
    Copy-Item $startup (Join-Path $evidence 'startup.log') -Force
    Get-Content $startup
  }
  if (Test-Path (Join-Path $scratch 'setup.log')) { Copy-Item (Join-Path $scratch 'setup.log') $evidence -Force }
  if (Test-Path $image) { Copy-Item $image (Join-Path $evidence 'installed-ui.png') -Force }
  foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $old[$name], 'Process') }
  if (Test-Path $scratch) { Remove-Item $scratch -Recurse -Force }
}
