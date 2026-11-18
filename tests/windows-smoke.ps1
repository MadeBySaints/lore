$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $root 'build/windows/lore.exe'
$project = Join-Path ([IO.Path]::GetTempPath()) ("Lore's & mémoire " + [guid]::NewGuid())
$previous = @{}
$names = @('PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
  # The packaged app must find its own Qt DLLs and platform/SQLite plugins.
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  $env:QT_PLUGIN_PATH = $null
  $env:QML_IMPORT_PATH = $null
  $env:QML2_IMPORT_PATH = $null
  New-Item -ItemType Directory -Path $project | Out-Null
  & $binary init --project $project --json
  if ($LASTEXITCODE -ne 0) { throw 'Packaged initialization failed' }
  & $binary connect --project $project --json
  if ($LASTEXITCODE -ne 0) { throw 'Packaged hook installation failed' }
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
    $response = $payload | & $callback.command @hookArgs
    if ($LASTEXITCODE -ne 0) { throw "Packaged $event hook failed" }
    if ($event -in @('SessionStart', 'UserPromptSubmit')) {
      $context = $response | ConvertFrom-Json
      if ($context.hookSpecificOutput.hookEventName -ne $event) { throw "Missing $event context" }
    }
  }
  $status = & $binary status --project $project --json | ConvertFrom-Json
  if ($LASTEXITCODE -ne 0 -or $status.events -ne 1) { throw 'Capture was missing or duplicated' }
} finally {
  foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
  if (Test-Path $project) { Remove-Item $project -Recurse -Force }
}
