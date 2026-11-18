$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $root 'build/windows/lore.exe'
$project = Join-Path ([IO.Path]::GetTempPath()) ("Lore's & mémoire " + [guid]::NewGuid())
$previous = @{}
$names = @('PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_FORCE_STDERR_LOGGING')
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Invoke-Packaged {
  param([string]$Executable = $binary, [string[]]$Arguments, [string]$InputJson = '')
  Write-Host "Checking packaged Lore: $($Arguments[0])"
  $start = [Diagnostics.ProcessStartInfo]::new($Executable)
  $start.UseShellExecute = $false
  $start.RedirectStandardInput = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
  $process = [Diagnostics.Process]::Start($start)
  try {
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if ($InputJson) { $process.StandardInput.WriteLine($InputJson) }
    $process.StandardInput.Close()
    if (!$process.WaitForExit(15000)) {
      $process.Kill($true)
      $process.WaitForExit()
      throw "Packaged Lore timed out: $($stderr.Result)"
    }
    if ($process.ExitCode -ne 0) { throw "Packaged Lore exited $($process.ExitCode): $($stderr.Result)" }
    return $stdout.Result
  } finally {
    $process.Dispose()
  }
}
try {
  # The packaged app must find its own Qt DLLs and platform/SQLite plugins.
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  $env:QT_PLUGIN_PATH = $null
  $env:QML_IMPORT_PATH = $null
  $env:QML2_IMPORT_PATH = $null
  $env:QT_FORCE_STDERR_LOGGING = '1'
  New-Item -ItemType Directory -Path $project | Out-Null
  Invoke-Packaged -Arguments @('init', '--project', $project, '--json') | Out-Null
  Invoke-Packaged -Arguments @('connect', '--project', $project, '--json') | Out-Null
  $settings = Get-Content (Join-Path $project '.claude/settings.local.json') -Raw | ConvertFrom-Json
  $transcript = Join-Path $project 'fixture.jsonl'
  @{ type = 'user'; cwd = $project; timestamp = '2026-11-18T10:00:00Z';
     message = @{ role = 'user'; content = 'Windows hook capture works.' } } |
    ConvertTo-Json -Depth 5 -Compress | Set-Content -Encoding utf8 $transcript
  foreach ($event in @('SessionStart', 'UserPromptSubmit', 'Stop', 'PreCompact', 'SessionEnd')) {
    $callback = $settings.hooks.$event[0].hooks[0]
    $hookArgs = @($callback.args)
    $payload = @{ hook_event_name = $event; transcript_path = $transcript; prompt = 'Windows capture' } |
      ConvertTo-Json -Compress
    $response = Invoke-Packaged -Executable $callback.command -Arguments $hookArgs -InputJson $payload
    if ($event -in @('SessionStart', 'UserPromptSubmit')) {
      $context = $response | ConvertFrom-Json
      if ($context.hookSpecificOutput.hookEventName -ne $event) { throw "Missing $event context" }
    }
  }
  $status = Invoke-Packaged -Arguments @('status', '--project', $project, '--json') | ConvertFrom-Json
  if ($status.events -ne 1) { throw 'Capture was missing or duplicated' }
} finally {
  foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
  if (Test-Path $project) { Remove-Item $project -Recurse -Force }
}
