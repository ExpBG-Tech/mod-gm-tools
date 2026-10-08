#requires -Version 7.0
# Freeze every declared external dependency (tools/project.json installedDependencies) into a GUID-named
# snapshot for one native run. Roots are searched in order (-SearchRoots, or -InstalledAddonsRoot alone;
# Get-LocalConfig.ps1 DependencyAddonsRoots: the local build directory, the Workbench addons directory,
# then the Workshop downloads), and within a root the configured directory names in order. The first
# existing candidate wins; a candidate with another project GUID stops the build. The chosen sources are
# recorded in <Destination>.json beside the snapshot. -ResolveOnly returns the chosen sources (GUID ->
# folder) and copies nothing.
# EXPBG Audio Data (tools/audio-data.json) holds the samples of the GM Tools audio projects. An unpacked
# folder (a local build of the mod-audio-data project) must hold every listed sample with its .meta
# naming the listed GUID and path and the listed size; a Workshop download (data.pak) must be at least the
# listed minimumVersion. Otherwise the build stops: the GM Tools sounds would be silent.
# Samples (.wav, .snd; about 1 GB in EXPBG Audio Data) are hard links when source and snapshot share a
# volume, otherwise copies; everything else is copied. Runs delete their snapshot, which removes only the
# links.
param([string]$Destination = '', [string]$InstalledAddonsRoot, [string[]]$SearchRoots = @(), [switch]$ResolveOnly, [switch]$Companion)
$ErrorActionPreference = 'Stop'
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1"
$entry = $settings.addon
$sources = [ordered]@{}
$origins = @{}
if ($Companion) {
 $entry = $settings.companion
 if (!$entry) { throw 'No companion is configured.' }
 $built = Get-Content -LiteralPath (Join-Path $settings.repo '.local/last-build.json') -Raw | ConvertFrom-Json
 if (!$built.compiled -or !(Test-Path -LiteralPath (Join-Path $built.installedAddon 'resourceDatabase.rdb'))) { throw 'Build the core addon before the companion.' }
 $sources[$settings.addon.id] = $built.installedAddon
 $origins[$settings.addon.id] = 'last-build'
}
$roots = @(@($SearchRoots) | Where-Object { $_ } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (!$roots.Count -and $InstalledAddonsRoot) { $roots = @($InstalledAddonsRoot) }
function Test-AddonIdentity([string]$Path, [string]$Id) {
 $projects = @(Get-ChildItem -LiteralPath $Path -Filter '*.gproj' -File)
 return $projects.Count -eq 1 -and (Get-Content -LiteralPath $projects[0].FullName -Raw) -cmatch ('\bGUID\s+"?' + $Id + '\b')
}
$audioDataPath = Join-Path $settings.repo 'tools/audio-data.json'
$audioData = if (Test-Path -LiteralPath $audioDataPath -PathType Leaf) { Get-Content -LiteralPath $audioDataPath -Raw | ConvertFrom-Json } else { $null }
function Assert-AudioData([string]$Path) {
 $hint = 'Build EXPBG Audio Data 0.1.2 or later (its folder mod-audio-data, with ./build.ps1 -NonInteractive -Audio in mod-ambient-radio) or update its Workshop download.'
 if (Test-Path -LiteralPath (Join-Path $Path 'data.pak') -PathType Leaf) {
  # A Workshop download: packed, so only its version can be checked.
  $version = $null
  try { $version = [string](Get-Content -LiteralPath (Join-Path $Path 'ServerData.json') -Raw | ConvertFrom-Json).revision.version } catch { $version = $null }
  if (!$version) { Write-Warning "EXPBG Audio Data at $Path has no readable version; it must be $($audioData.dependency.minimumVersion) or later."; return }
  if ([version]$version -lt [version]$audioData.dependency.minimumVersion) { throw "EXPBG Audio Data at $Path is version $version; GM Tools needs $($audioData.dependency.minimumVersion) or later (its sounds). $hint" }
  return
 }
 foreach ($sample in @($audioData.samples)) {
  $file = Join-Path $Path $sample.path
  if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne $sample.bytes -or !(Test-Path -LiteralPath "$file.meta" -PathType Leaf)) { throw "EXPBG Audio Data at $Path lacks the GM Tools sample $($sample.path) (or it differs from tools/audio-data.json). $hint" }
  $meta = [regex]::Match((Get-Content -LiteralPath "$file.meta" -Raw), 'Name\s+"\{([0-9A-Fa-f]{16})\}([^"\r\n]+)"')
  if (!$meta.Success -or $meta.Groups[1].Value.ToUpperInvariant() -cne $sample.guid -or $meta.Groups[2].Value -cne $sample.path) { throw "EXPBG Audio Data at $Path does not name {$($sample.guid)}$($sample.path) in its .meta. $hint" }
 }
}
if ($entry.installedDependencies) {
 foreach ($id in $entry.installedDependencies.Keys) {
  $found = $null
  foreach ($root in $roots) {
   foreach ($name in @($entry.installedDependencies[$id])) {
    $candidate = Join-Path $root $name
    if (!(Test-Path -LiteralPath $candidate -PathType Container)) { continue }
    if (!(Test-AddonIdentity $candidate $id)) { throw "Install the configured dependency with GUID $id before building: $candidate has a different or incomplete project identity." }
    $found = $candidate
    break
   }
   if ($found) { break }
  }
  if (!$found) { throw "Install the configured dependency with GUID $id before building (looked for $(@($entry.installedDependencies[$id]) -join ', ') in: $($roots -join '; '))." }
  if ($audioData -and $id -ceq $audioData.dependency.id) { Assert-AudioData $found }
  $sources[$id] = $found
  $origins[$id] = 'search-root'
 }
}
if ($ResolveOnly) { return [pscustomobject]$sources }
if (!$Destination) { throw 'Name the dependency snapshot directory (-Destination).' }
if (Test-Path -LiteralPath $Destination) { throw 'Dependency snapshot already exists; use a new run name.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
$record = @(foreach ($id in $sources.Keys) {
 $source = $sources[$id]
 if (!(Test-AddonIdentity $source $id)) { throw "Install the configured dependency with GUID $id before building." }
 # GUID-named snapshots avoid collisions between unrelated installed directory names.
 $sourceRoot = [IO.Path]::GetFullPath($source).TrimEnd('\','/')
 $target = Join-Path $Destination $id
 $linked = 0
 New-Item -ItemType Directory -Path $target | Out-Null
 foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Force) {
  if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked file in dependency: $($file.FullName)" }
  $copy = Join-Path $target $file.FullName.Substring($sourceRoot.Length + 1)
  New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
  $isLinked = $false
  if ($file.Extension -in '.wav', '.snd') {
   try { New-Item -ItemType HardLink -Path $copy -Target $file.FullName -ErrorAction Stop | Out-Null; $isLinked = $true } catch { $isLinked = $false }
  }
  if ($isLinked) { $linked++ } else { Copy-Item -LiteralPath $file.FullName -Destination $copy }
 }
 $version = $null
 $serverData = Join-Path $source 'ServerData.json'
 if (Test-Path -LiteralPath $serverData -PathType Leaf) { try { $version = (Get-Content -LiteralPath $serverData -Raw | ConvertFrom-Json).revision.version } catch { $version = $null } }
 $pak = Join-Path $source 'data.pak'
 [ordered]@{
  id = $id; origin = $origins[$id]; source = $source; workshopVersion = $version
  packed = Test-Path -LiteralPath $pak -PathType Leaf
  dataPakSHA256 = if (Test-Path -LiteralPath $pak -PathType Leaf) { (Get-FileHash -LiteralPath $pak).Hash } else { $null }
  projectSHA256 = (Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $source -Filter '*.gproj' -File)[0].FullName).Hash
  samplesLinked = $linked
 }
})
# Kept beside the snapshot, never inside the scanned addons directory.
ConvertTo-Json -InputObject $record -Depth 4 | Set-Content -LiteralPath ([IO.Path]::GetFullPath($Destination).TrimEnd('\','/') + '.json') -Encoding utf8
