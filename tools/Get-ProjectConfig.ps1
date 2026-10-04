#requires -Version 7.0
param([string]$Repo = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath($Repo)
$config = Get-Content -LiteralPath (Join-Path $repoPath 'tools/project.json') -Raw | ConvertFrom-Json -AsHashtable
$asset = Get-Content -LiteralPath (Join-Path $repoPath 'tools/workshop-asset.json') -Raw | ConvertFrom-Json -AsHashtable
function Assert-RelativePath([string]$Path) {
 if (!$Path -or $Path -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -or @($Path.Split('/') | Where-Object { $_ -in '.', '..' }).Count) { throw "Expected a repository-relative path: $Path" }
}
if ($config -isnot [System.Collections.IDictionary] -or $config.addon -isnot [System.Collections.IDictionary]) { throw 'Project configuration requires an addon object.' }
if ($asset.id -cnotmatch '^[A-F0-9]{16}$' -or !$asset.name -or $asset.unlisted -isnot [bool] -or $asset.private -isnot [bool]) { throw 'Workshop metadata requires an item GUID, name and explicit visibility.' }
$config.addon.id = $asset.id
$entries = @($config.addon)
if ($config.companion) { $entries += $config.companion }
$ids = @{}
$names = @{}
foreach ($entry in $entries) {
 if ($entry.name -cnotmatch '^[A-Za-z][A-Za-z0-9_-]*$' -or $entry.id -cnotmatch '^[A-F0-9]{16}$') { throw 'Each addon requires a safe name and project GUID.' }
 if ($names.ContainsKey($entry.name) -or $ids.ContainsKey($entry.id)) { throw 'Addon names and project GUIDs must be distinct.' }
 $names[$entry.name] = $true; $ids[$entry.id] = $true
 Assert-RelativePath $entry.source
 $entry.sourcePath = Join-Path $repoPath $entry.source
 $entry.project = $entry.name + '.gproj'
 if ($entry.dependencies -isnot [array] -or !$entry.dependencies.Count -or @($entry.dependencies | Where-Object { $_ -cnotmatch '^[A-F0-9]{16}$' -or $_ -ceq $entry.id }).Count -or @($entry.dependencies | Select-Object -Unique).Count -ne $entry.dependencies.Count) { throw 'Dependencies must be distinct GUIDs and cannot include the addon itself.' }
 if ($entry.runtimePaths -isnot [array] -or !$entry.runtimePaths.Count) { throw 'Each addon requires an explicit runtime path allowlist.' }
 foreach ($path in $entry.runtimePaths) {
  Assert-RelativePath $path
  if ($path -match '(?i)(^|/)(docs?|tests?|tools|logs?|profile|evidence|\.local|build|artifacts)(/|$)') { throw 'Development content cannot be a runtime path.' }
 }
 if ($entry.installedDependencies) {
  if ($entry.installedDependencies -isnot [System.Collections.IDictionary]) { throw 'Installed dependencies must map GUIDs to installed directory names.' }
  foreach ($id in $entry.installedDependencies.Keys) {
   if ($id -cnotin $entry.dependencies -or $entry.installedDependencies[$id] -notmatch '^[A-Za-z0-9_-]+$') { throw 'Installed dependencies require a declared GUID and a directory name.' }
  }
 }
}
Assert-RelativePath $config.addon.preview
if ($config.companion -and ($config.companion.source -eq $config.addon.source -or $config.companion.id -ceq $config.addon.id)) { throw 'The companion must have its own source and identity.' }
if ($config.legacyPublishWorkflows -isnot [array] -or @($config.legacyPublishWorkflows | Where-Object { $_ -notmatch '^[A-Za-z0-9_-]+\.ya?ml$' }).Count) { throw 'Legacy workflows must be an explicit array of workflow filenames (empty for new repositories).' }
$config.asset = $asset
$config.repo = $repoPath
[pscustomobject]$config
