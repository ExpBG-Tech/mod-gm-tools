#requires -Version 7.0
param([Parameter(Mandatory)][string]$Destination, [string]$InstalledAddonsRoot, [switch]$Companion)
$ErrorActionPreference = 'Stop'
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1"
$entry = $settings.addon
$sources = @{}
if ($Companion) {
 $entry = $settings.companion
 if (!$entry) { throw 'No companion is configured.' }
 $built = Get-Content -LiteralPath (Join-Path $settings.repo '.local/last-build.json') -Raw | ConvertFrom-Json
 if (!$built.compiled -or !(Test-Path -LiteralPath (Join-Path $built.installedAddon 'resourceDatabase.rdb'))) { throw 'Build the core addon before the companion.' }
 $sources[$settings.addon.id] = $built.installedAddon
}
foreach ($id in $entry.installedDependencies.Keys) { $sources[$id] = Join-Path $InstalledAddonsRoot $entry.installedDependencies[$id] }
if (Test-Path -LiteralPath $Destination) { throw 'Dependency snapshot already exists; use a new run name.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach ($id in $sources.Keys) {
 $source = $sources[$id]
 $projects = @(Get-ChildItem -LiteralPath $source -Filter '*.gproj' -File)
 if ($projects.Count -ne 1 -or (Get-Content -LiteralPath $projects[0].FullName -Raw) -cnotmatch ('\bGUID\s+"?' + $id + '\b')) { throw "Install the configured dependency with GUID $id before building." }
 # GUID-named snapshots avoid collisions between unrelated installed directory names.
 Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $id) -Recurse
}
