# Shared installation paths only; never place passwords or tokens in this configuration.
param([string]$ConfigPath = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$config = [ordered]@{
 GameRoot = ''
 ServerRoot = ''
 WorkbenchRoot = ''
 InstalledAddonsRoot = ''
 IncidentLog = ''
 BuildAddonsDirectory = ''
}
if ($IsWindows) {
 if (${env:ProgramFiles(x86)}) {
  $steam = Join-Path ${env:ProgramFiles(x86)} 'Steam/steamapps/common'
  $config.GameRoot = Join-Path $steam 'Arma Reforger'
  $config.ServerRoot = Join-Path $steam 'Arma Reforger Server'
  $config.WorkbenchRoot = Join-Path $steam 'Arma Reforger Tools/Workbench'
 }
 $documents = [Environment]::GetFolderPath('MyDocuments')
 if ($documents) { $config.InstalledAddonsRoot = Join-Path $documents 'My Games/ArmaReforger/addons' }
}
$local = $ConfigPath
if (!$local) { $local = Join-Path $repo '.local/config.json' }
if (Test-Path -LiteralPath $local) {
 $overrides = Get-Content -LiteralPath $local -Raw | ConvertFrom-Json
 if ($null -eq $overrides -or $overrides -isnot [pscustomobject]) { throw 'Local configuration must be a JSON object.' }
 foreach ($entry in $overrides.PSObject.Properties) {
  if (!$config.Contains($entry.Name)) { throw "Unknown local configuration key: $($entry.Name)" }
  if ($entry.Value -isnot [string]) { throw "Configuration value must be a path string: $($entry.Name)" }
  $config[$entry.Name] = $entry.Value
 }
}
[pscustomobject]$config
