$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('Lore-install-' + [guid]::NewGuid())
$destination = Join-Path $scratch "Lore's app folder"
$project = Join-Path $scratch 'project-memory'
$image = Join-Path $scratch 'installed-ui.png'
$setup = Join-Path $root 'build/installer/Lore-Setup-0.2.0.exe'
$old = @{}
$names = @('LORE_PREFERENCES_FILE', 'LORE_SKIP_TOOL_CHECKS', 'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND')
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
  $env:QT_QPA_PLATFORM = 'offscreen'
  $env:QT_QUICK_BACKEND = 'software'
  Run-Checked -Executable (Join-Path $destination 'lore-desktop.exe') -Arguments @('--screenshot', $image)
  for ($retry = 0; $retry -lt 40 -and !(Test-Path $image); $retry++) { Start-Sleep -Milliseconds 250 }
  if (!(Test-Path $image) -or (Get-Item $image).Length -lt 10000) { throw 'Installed app did not render its setup screen' }
  New-Item -ItemType Directory -Path $project | Out-Null
  Run-Checked -Executable (Join-Path $destination 'lore.exe') -Arguments @('init', '--project', $project, '--json')
  Run-Checked -Executable (Join-Path $destination 'unins000.exe') -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
  for ($retry = 0; $retry -lt 40 -and (Test-Path (Join-Path $destination 'lore.exe')); $retry++) { Start-Sleep -Milliseconds 250 }
  if (!(Test-Path (Join-Path $project '.lore/brain.sqlite'))) { throw 'Uninstall removed project memory' }
} finally {
  foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $old[$name], 'Process') }
  if (Test-Path $scratch) { Remove-Item $scratch -Recurse -Force }
}
