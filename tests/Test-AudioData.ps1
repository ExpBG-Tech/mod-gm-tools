#requires -Version 7.0
# Portable guard for the audio layout since 0.1.16: GM Tools ships no audio. Its samples ship in the
# dependency EXPBG Audio Data (198987BE7BAC4C84), a pure data mod in its own folder outside Git
# (mod-audio-data beside this repository; built with the tooling of mod-ambient-radio):
# - no audio file (.wav, .ogg, .opus, .snd, ...) or audio .meta in addon/ or anywhere in Git;
# - the dependency is declared in the project file and tools/project.json (dependencies, and
#   installedDependencies for native builds and fixtures: the Workbench project EXPBG_Ambient_Radio_Audio,
#   then the Workshop download folders) and named in the Workshop description and the README;
# - tools/audio-data.json lists every sample once (GUID, path, size, sha256, WAV frames and rate); every
#   {GUID}path sample reference of an EXPBG folder in the GM Tools audio projects (.acp) is a listed
#   sample with that GUID, and every listed sample is referenced;
# - when the mod-audio-data folder sits beside this repository, every listed sample exists in it with
#   the listed size, sha256, frames and rate and a .meta naming the same GUID and path, its
#   gm-samples.json lists exactly the same samples, and its Licenses copies of the GM Tools
#   sound notices equal AUDIO_CREDITS.txt and the Ambient Civilians license.txt. Without the folder (CI)
#   that part is skipped with a message.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addon = Join-Path $repo 'addon'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
$audioTypes = '(?i)\.(wav|ogg|opus|snd|mp3|flac|m4a|aac|wma|aif|aiff)(\.meta)?$'
$baseGame = '58D0FB3206B6F859'
$audioDataId = '198987BE7BAC4C84'

# ---- No audio in GM Tools ----
$files = @(& git -C $repo ls-files --cached --others --exclude-standard)
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect the Git inventory.' }
$tracked = @($files | Where-Object { $_ -match $audioTypes })
Assert ($tracked.Count -eq 0) "GM Tools ships no audio (its samples are in EXPBG Audio Data); found $($tracked -join ', ')"
$onDisk = @(Get-ChildItem -LiteralPath $addon -Recurse -File | Where-Object { $_.Name -match $audioTypes } | ForEach-Object { $_.FullName.Substring($repo.Length + 1) })
Assert ($onDisk.Count -eq 0) "no audio file belongs in addon/; found $($onDisk -join ', ')"

# ---- The dependency ----
$project = & "$repo/tools/Get-ProjectConfig.ps1"
$pack = Get-Content -LiteralPath (Join-Path $repo 'tools/pack.json') -Raw | ConvertFrom-Json
Assert ((@($project.addon.dependencies) -join ',') -ceq "$baseGame,$audioDataId") 'tools/project.json: GM Tools depends on exactly the base game and EXPBG Audio Data'
$gproj = Get-Content -LiteralPath (Join-Path $addon $pack.project) -Raw
$gprojDependencies = @([regex]::Matches([regex]::Match($gproj, '(?s)Dependencies\s*\{([^}]*)\}').Groups[1].Value, '[A-Fa-f0-9]{16}') | ForEach-Object Value)
Assert (($gprojDependencies -join ',') -ceq "$baseGame,$audioDataId") "$($pack.project) must list the base game and EXPBG Audio Data"
Assert ((@($project.addon.installedDependencies.Keys) -join ',') -ceq $audioDataId -and (@($project.addon.installedDependencies[$audioDataId]) -join ',') -ceq "EXPBG_Ambient_Radio_Audio,EXPBGAudioData_$audioDataId,EXPBGAmbientRadioAudio_$audioDataId") 'installedDependencies must resolve EXPBG Audio Data: the Workbench project EXPBG_Ambient_Radio_Audio, then the Workshop download folders EXPBGAudioData_<GUID> and EXPBGAmbientRadioAudio_<GUID>'
Assert ($project.asset.description.Contains("EXPBG Audio Data ($audioDataId)")) 'the Workshop description must name the dependency EXPBG Audio Data with its ID'
$readme = Get-Content -LiteralPath (Join-Path $repo 'README.md') -Raw
Assert ($readme.Contains('EXPBG Audio Data') -and $readme.Contains($audioDataId)) 'README must name the dependency EXPBG Audio Data with its ID'

# ---- Inventory and references ----
$audioData = Get-Content -LiteralPath (Join-Path $repo 'tools/audio-data.json') -Raw | ConvertFrom-Json
$dependency = $audioData.dependency
Assert ($dependency.id -ceq $audioDataId -and $dependency.name -ceq 'EXPBG Audio Data' -and $dependency.project -ceq 'EXPBG_Ambient_Radio_Audio' -and [version]$dependency.minimumVersion -ge [version]'0.1.3') 'tools/audio-data.json must describe EXPBG Audio Data 0.1.3 or later'
$samples = @($audioData.samples)
$modules = @($pack.modules | ForEach-Object name)
Assert ($samples.Count -ge 1 -and !@($samples | Where-Object { $_.path -cnotmatch '^(Audio|Sounds)/[A-Za-z0-9_./-]+\.wav$' -or $_.guid -cnotmatch '^[0-9A-F]{16}$' -or $_.sha256 -cnotmatch '^[0-9A-F]{64}$' -or $_.bytes -le 44 -or $_.frames -le 0 -or $_.rate -lt 8000 -or $_.rate -gt 96000 -or $_.module -cnotin $modules }).Count) 'every inventory entry needs a WAV path, GUID, sha256, size, frames, rate and a pack module'
Assert (@($samples.path | Select-Object -Unique).Count -eq $samples.Count -and @($samples.guid | Select-Object -Unique).Count -eq $samples.Count) 'tools/audio-data.json lists every sample and GUID once'
$listed = @{}
foreach ($sample in $samples) { $listed[$sample.path] = $sample }
$referenced = @{}
$references = 0
$projects = @(Get-ChildItem -LiteralPath $addon -Recurse -File -Filter '*.acp')
foreach ($acp in $projects) {
 $origin = $acp.FullName.Substring($addon.Length + 1).Replace('\', '/')
 foreach ($match in [regex]::Matches([IO.File]::ReadAllText($acp.FullName), '\{([0-9A-Fa-f]{16})\}([A-Za-z0-9_./-]+\.(?:wav|ogg|snd|opus))"')) {
  $path = $match.Groups[2].Value
  # Vanilla samples (no EXPBG folder) come from the base game; every EXPBG sample is EXPBG Audio Data's.
  if ($path -cnotmatch '(^|/)EXP[A-Z]*/') { continue }
  $references++
  Assert ($listed.ContainsKey($path)) "$origin plays {$($match.Groups[1].Value)}$path, which is not in tools/audio-data.json (EXPBG Audio Data)"
  Assert ($listed[$path].guid -ceq $match.Groups[1].Value.ToUpperInvariant()) "$origin plays {$($match.Groups[1].Value)}$path; EXPBG Audio Data has it as {$($listed[$path].guid)}"
  $referenced[$path] = $true
 }
}
$stale = @($samples | Where-Object { !$referenced.ContainsKey($_.path) } | ForEach-Object path)
Assert ($stale.Count -eq 0) "tools/audio-data.json lists samples no GM Tools audio project plays: $($stale -join ', ')"
$megabytes = [math]::Round(([long]($samples | Measure-Object bytes -Sum).Sum) / 1MB)

# ---- Cross-folder: the samples in EXPBG Audio Data (mod-audio-data beside this repository) ----
function Read-WavFormat([string]$Path) {
 $stream = [IO.File]::OpenRead($Path)
 try {
  $reader = [IO.BinaryReader]::new($stream)
  Assert ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -eq 'RIFF') "not RIFF: $Path"
  $null = $reader.ReadUInt32()
  Assert ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -eq 'WAVE') "not WAVE: $Path"
  $blockAlign = 0; $rate = 0
  while ($stream.Position + 8 -le $stream.Length) {
   $id = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)); $size = [long]$reader.ReadUInt32(); $next = $stream.Position + $size + ($size % 2)
   if ($id -eq 'fmt ') {
    Assert ($reader.ReadUInt16() -eq 1) "not PCM: $Path"
    $null = $reader.ReadUInt16(); $rate = $reader.ReadUInt32(); $null = $reader.ReadUInt32(); $blockAlign = $reader.ReadUInt16()
   } elseif ($id -eq 'data') {
    Assert ($rate -gt 0 -and $blockAlign -gt 0) "data before fmt: $Path"
    return [pscustomobject]@{ frames = [long]($size / $blockAlign); rate = [long]$rate }
   }
   $stream.Position = $next
  }
  throw "FAIL: no data chunk: $Path"
 } finally { $stream.Dispose() }
}
$audioPack = Join-Path (Split-Path -Parent $repo) $dependency.folder
$crossCheck = "SKIP (cross-folder part): no EXPBG Audio Data folder at $audioPack; its samples were not compared."
if (Test-Path -LiteralPath (Join-Path $audioPack "$($dependency.project).gproj") -PathType Leaf) {
 Assert ((Get-Content -LiteralPath (Join-Path $audioPack "$($dependency.project).gproj") -Raw) -cmatch ('\bGUID\s+"' + $audioDataId + '"')) "$audioPack is not the EXPBG Audio Data project $audioDataId"
 $packInventory = @((Get-Content -LiteralPath (Join-Path $audioPack $dependency.inventory) -Raw | ConvertFrom-Json).samples)
 $mine = @($samples | ForEach-Object { "$($_.path)|$($_.guid)|$($_.bytes)|$($_.sha256)" } | Sort-Object)
 $theirs = @($packInventory | ForEach-Object { "$($_.path)|$($_.guid)|$($_.bytes)|$($_.sha256)" } | Sort-Object)
 Assert (!(Compare-Object $mine $theirs -CaseSensitive)) "tools/audio-data.json and $($dependency.folder)/$($dependency.inventory) list different samples"
 foreach ($sample in $samples) {
  $file = Join-Path $audioPack $sample.path
  Assert (Test-Path -LiteralPath $file -PathType Leaf) "EXPBG Audio Data lacks $($sample.path) ($audioPack)"
  Assert ((Get-Item -LiteralPath $file).Length -eq $sample.bytes -and (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ceq $sample.sha256) "EXPBG Audio Data's $($sample.path) differs from tools/audio-data.json"
  $meta = [regex]::Match([IO.File]::ReadAllText("$file.meta"), 'Name\s+"\{([0-9A-Fa-f]{16})\}([^"\r\n]+)"')
  Assert ($meta.Success -and $meta.Groups[1].Value.ToUpperInvariant() -ceq $sample.guid -and $meta.Groups[2].Value -ceq $sample.path) "EXPBG Audio Data's $($sample.path).meta must name {$($sample.guid)}$($sample.path)"
  $format = Read-WavFormat $file
  Assert ($format.frames -eq $sample.frames -and $format.rate -eq $sample.rate) "EXPBG Audio Data's $($sample.path) has $($format.frames) frames at $($format.rate) Hz; tools/audio-data.json says $($sample.frames) at $($sample.rate)"
 }
 foreach ($notice in @(@('addon/ambient-sounds/Credits/AUDIO_CREDITS.txt', 'Licenses/EXPBG_Audio_Data_GM_Tools_Sounds_Credits.txt'), @('addon/ambient-civilians/license.txt', 'Licenses/EXPBG_Audio_Data_Civilian_Sounds_license.txt'))) {
  $copy = Join-Path $audioPack $notice[1]
  Assert ((Test-Path -LiteralPath $copy -PathType Leaf) -and (Get-FileHash -LiteralPath $copy).Hash -ceq (Get-FileHash -LiteralPath (Join-Path $repo $notice[0])).Hash) "EXPBG Audio Data's $($notice[1]) must be a copy of $($notice[0]) (the credits travel with the samples)"
 }
 $crossCheck = "$($dependency.folder): $($samples.Count) samples present with the same GUIDs, sizes, sha256, frames and rates, gm-samples.json identical, credit copies in sync."
}
"PASS: no audio in GM Tools; depends on the base game and EXPBG Audio Data $audioDataId (project file, tools/project.json, installedDependencies, listing, README); $references sample references in $($projects.Count) audio projects resolve to the $($samples.Count) samples ($megabytes MB) of tools/audio-data.json, none unreferenced."
$crossCheck
