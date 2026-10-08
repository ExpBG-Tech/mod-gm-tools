#requires -Version 7.0
# Portable guard for the Ambient Sounds radios and placed sounds in the Game Master entity browser.
# - Every PlaceableEntities registry of the pack together lists exactly three radios
#   (EAS_RadioModule prefabs): one civilian RadioSmall_01 radio and the AN/GRC-160 and R-123M
#   military radios. No other listed item shows a radio model or a radio preview picture, so
#   the browser has no per-track radios. Which civilian radio is offered is the one Systems.conf line.
# - Radio Red stays loadable for old saves: prefab, GUID and radio persistence rule exist, but no
#   registry lists it.
# - Recording IDs are permanent: 0-12 keep their events, new IDs only append. Every EAS_RadioBank
#   recording resolves in EXPBG_Radio.acp (sound, shader, the radios' fixed 30 m amplitude, mixer,
#   exactly one RadioTransmissions sample of EXPBG Audio Data with the GUID tools/audio-data.json
#   lists, Duration equal to the WAV length recorded there)
#   and is credited. Random groups only name bank recordings.
# - Every radio and TV event keeps priority 80-100 and bypassVolumeTest 1 in Workbench key order
#   (no noInAudible), so busy scenes do not evict quiet chatter first. Crowd events are unchanged.
# - The radios' GM Recording selector offers every radio recording and random group and nothing
#   else (no Nokia, church bell, firefight ...). Rows map to durable IDs; saves store the IDs.
# - The 28 placed sounds stay as they are: listed, invisible EAS_SoundModule prefabs named
#   "Sound: ...", one recording each, and their selector keeps all EAS_SoundBank recordings.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addon = Join-Path $repo 'addon'
$sounds = Join-Path $addon 'ambient-sounds'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Find-Resource([string]$Relative) {
 $found = @(Get-ChildItem -LiteralPath $addon -Directory | ForEach-Object { Join-Path $_.FullName $Relative } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
 Assert ($found.Count -eq 1) "resource must exist in exactly one module: $Relative ($($found.Count) found)"
 $found[0]
}
function Assert-Meta([string]$Guid, [string]$Relative) {
 $path = Find-Resource $Relative
 $meta = [regex]::Match((Read-Text "$path.meta"), 'Name\s+"\{([0-9A-F]{16})\}([^"]+)"')
 Assert ($meta.Success -and $meta.Groups[1].Value -ceq $Guid -and $meta.Groups[2].Value -ceq $Relative) "meta GUID/path differs for {$Guid}$Relative"
 $path
}
# The samples ship in the dependency EXPBG Audio Data; tools/audio-data.json records each one's GUID, path
# and length (frames / rate of its WAV data), checked against the samples by tests/Test-AudioData.ps1.
$audioData = Get-Content -LiteralPath (Join-Path $repo 'tools/audio-data.json') -Raw | ConvertFrom-Json
$inventory = @{}
foreach ($entry in @($audioData.samples)) { $inventory[$entry.path] = $entry }
function Get-SampleSeconds([string]$Guid, [string]$Relative) {
 Assert ($inventory.ContainsKey($Relative)) "sample {$Guid}$Relative is not in tools/audio-data.json (EXPBG Audio Data)"
 $entry = $inventory[$Relative]
 Assert ($entry.guid -ceq $Guid) "tools/audio-data.json names {$($entry.guid)}$Relative; the audio project plays {$Guid}"
 Assert (!@(Get-ChildItem -LiteralPath $addon -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName $Relative) }).Count) "$Relative must not ship in GM Tools (it is EXPBG Audio Data's)"
 [double]$entry.frames / [double]$entry.rate
}
function Read-Prefab([string]$Reference) {
 $match = [regex]::Match($Reference, '^\{([0-9A-F]{16})\}(.+\.et)$')
 Assert $match.Success "registry entry is not a prefab reference: $Reference"
 $path = Assert-Meta $match.Groups[1].Value $match.Groups[2].Value
 $text = Read-Text $path
 [pscustomobject]@{
  Guid = $match.Groups[1].Value; Path = $match.Groups[2].Value; Text = $text
  Class = [regex]::Match($text, '^\s*(\w+)').Groups[1].Value
  Mesh = [regex]::Match($text, '(?s)MeshObject\s+"\{[0-9A-F]{16}\}"\s*\{\s*Object\s+"\{[0-9A-F]{16}\}([^"]+)"').Groups[1].Value
  Image = [regex]::Match($text, '\bm_Image\s+"([^"]*)"').Groups[1].Value
  Name = [regex]::Match($text, '\bName\s+"#([^"]+)"').Groups[1].Value
  Recording = [regex]::Match($text, '(?m)^ Recording\s+(\d+)\s*$')
  Enabled = [regex]::Match($text, '(?m)^ Enabled\s+(\d+)\s*$')
 }
}
function Get-Rows([string]$Edit, [string]$Attribute) {
 @([regex]::Matches((Get-Body $Edit ($Attribute + '\s*\{')), 'm_sEntryName\s+"([^"]+)"\s+m_fEntryFloatValue\s+(\d+)') | ForEach-Object { [pscustomobject]@{ Name = $_.Groups[1].Value; Value = [int]$_.Groups[2].Value } })
}
$radioMeshPattern = '(?i)/(RadioSmall_\d+|RadioStation_[A-Z0-9]+_\d+)\.xob$'
$radioImagePattern = '(?i)/E_Radio(Small|Station)_[^/]*\.edds$'
$nonRadio = '(?i)nokia|ringtone|church|bell|car alarm|police|siren|firefight|shelling|explosion|jet|drone|market|seller|singing|prayer|traffic|barking|dog|sheep|crowd'

# Entity browser: every placeable registry of the pack.
$registered = [ordered]@{}
foreach ($config in Get-ChildItem -LiteralPath $addon -Directory | ForEach-Object { Join-Path $_.FullName 'Configs/Editor/PlaceableEntities' } | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File -Filter '*.conf' }) {
 foreach ($m in [regex]::Matches((Read-Text $config.FullName), '"(\{[0-9A-F]{16}\}PrefabsEditable/[^"]+\.et)"')) { $registered[$m.Groups[1].Value] = $true }
}
# Registries may also list base-game prefabs; only prefabs shipped by the pack are inspected.
$listed = @($registered.Keys | Where-Object { $path = [regex]::Match($_, '^\{[0-9A-F]{16}\}(.+)$').Groups[1].Value; @(Get-ChildItem -LiteralPath $addon -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName $path) -PathType Leaf }).Count -gt 0 } | ForEach-Object { Read-Prefab $_ })
$radios = @($listed | Where-Object Class -ceq 'EAS_RadioModule')
Assert ($radios.Count -eq 3) "the entity browser must list exactly three radios, found $($radios.Count): $(@($radios.Path) -join ', ')"
$civilian = @($radios | Where-Object { $_.Mesh -cmatch '^Assets/Props/Civilian/RadioSmall_01/RadioSmall_01\.xob$' })
$angrc = @($radios | Where-Object { $_.Mesh -cmatch '/RadioStation_ANGRC160_01\.xob$' })
$r123 = @($radios | Where-Object { $_.Mesh -cmatch '/RadioStation_R123M_01\.xob$' })
Assert ($civilian.Count -eq 1 -and $angrc.Count -eq 1 -and $r123.Count -eq 1) 'the three radios must be one civilian RadioSmall_01 radio, the AN/GRC-160 and the R-123M'
foreach ($item in $listed) {
 if ($item.Class -ceq 'EAS_RadioModule') { continue }
 Assert ($item.Mesh -notmatch $radioMeshPattern) "$($item.Path) is not a radio but shows a radio model"
 Assert ($item.Image -notmatch $radioImagePattern) "$($item.Path) is not a radio but shows a radio preview picture"
}

# Every radio prefab ever shipped stays loadable from old saves (same GUID and class); an
# unlisted one (Radio Red) is only hidden from the browser.
foreach ($reference in @('{A52B74971E17499E}PrefabsEditable/EXPBG/E_EXPBG_Radio_Black.et', '{1EB295BBD7B5489A}PrefabsEditable/EXPBG/E_EXPBG_Radio_Red.et', '{5D3FFD5826E8A488}PrefabsEditable/EXPBG/E_EXPBG_Radio_ANGRC160.et', '{C6F32100C9F59391}PrefabsEditable/EXPBG/E_EXPBG_Radio_R123M.et')) {
 Assert ((Read-Prefab $reference).Class -ceq 'EAS_RadioModule') "$reference must remain an EAS_RadioModule prefab for old saves"
}
$red = '{1EB295BBD7B5489A}PrefabsEditable/EXPBG/E_EXPBG_Radio_Red.et'
Assert (!$registered.Contains($red) -or $civilian[0].Guid -ceq '1EB295BBD7B5489A') 'Radio Red is listed in addition to the civilian radio'
$persistence = Read-Text (Join-Path $sounds 'Configs/Systems/Persistence/Configuration/EXPAS/RadioModule.conf')
Assert ($persistence -cmatch 'EntityClass\s+"EAS_RadioModule"' -and $persistence -cmatch 'EAS_RadioModuleSerializer') 'radio persistence rule must keep restoring every EAS_RadioModule prefab'

# Radio bank: permanent IDs.
$bankText = Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_RadioBank.c')
$project = [regex]::Match($bankText, 'PROJECT\s*=\s*"\{([0-9A-F]{16})\}([^"]+)"')
Assert $project.Success 'EAS_RadioBank.PROJECT not found'
$recordings = @{}
foreach ($m in [regex]::Matches((Get-Body $bankText 'static\s+string\s+Event\s*\('), 'case\s+(\d+)\s*:\s*return\s+"([A-Za-z0-9_]+)"\s*;')) {
 Assert (!$recordings.ContainsKey([int]$m.Groups[1].Value)) "radio recording $($m.Groups[1].Value) defined twice"
 $recordings[[int]$m.Groups[1].Value] = $m.Groups[2].Value
}
$permanent = @('EAS_Radio_Static', 'EAS_Radio_Apache1', 'EAS_Radio_Apache2', 'EAS_Radio_Russian1', 'EAS_Radio_Russian2', 'EAS_Radio_Russian3', 'EAS_Radio_Russian4', 'EAS_Radio_Chinese1', 'EAS_Radio_ArabChatter', 'EAS_Radio_Arab1', 'SOUND_EAS_RADIO_BATTLEFIELD3', 'SOUND_EAS_RADIO_MYSTERYRUSSIAN', 'SOUND_EAS_RADIO_RUSSIANCHATTER', 'SOUND_EAS_RADIO_HANOIHANNAH')
for ($id = 0; $id -lt $permanent.Count; $id++) { Assert ($recordings[$id] -ceq $permanent[$id]) "radio recording $id must stay $($permanent[$id]) (saved radios store this ID)" }
Assert (@($recordings.Keys | Where-Object { $_ -ge 100 }).Count -eq 0) 'radio recordings 100+ are reserved for random groups'
$durations = @{}
foreach ($m in [regex]::Matches((Get-Body $bankText 'static\s+float\s+Duration\s*\('), 'case\s+(\d+)\s*:\s*return\s+([0-9.]+)\s*;')) { $durations[[int]$m.Groups[1].Value] = [double]::Parse($m.Groups[2].Value, $culture) }
$groups = @{}
foreach ($m in [regex]::Matches((Get-Body $bankText 'static\s+int\s+Resolve\s*\('), 'case\s+(\d+)\s*:\s*return\s+EAS_RadioBank\.Pick\(\s*\{([\d,\s]*)\}')) { $groups[[int]$m.Groups[1].Value] = @($m.Groups[2].Value.Split(',') | ForEach-Object { [int]$_.Trim() }) }
$validGroups = @([regex]::Matches((Get-Body $bankText 'static\s+bool\s+ValidSelection\s*\('), 'selection\s*==\s*(\d+)') | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object)
Assert ($groups.Count -ge 1 -and ($validGroups -join ',') -eq (@($groups.Keys | Sort-Object) -join ',')) 'ValidSelection groups differ from Resolve groups'
foreach ($group in $groups.Keys) {
 Assert ($group -ge 100) "random group $group overlaps the recording IDs"
 foreach ($member in $groups[$group]) { Assert $recordings.ContainsKey($member) "random group $group names unknown radio recording $member" }
}

# Radio module: radios only take radio bank selections at the fixed radio range.
$module = Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_RadioModule.c')
Assert ($module -cmatch 'string\s+AudioEvent\(int\s+recording\)\s*\{\s*return\s+EAS_RadioBank\.Event\(recording\);' -and $module -cmatch 'ResourceName\s+AudioProject\(\)\s*\{\s*return\s+EAS_RadioBank\.PROJECT;') 'radios must play EAS_RadioBank events from its project'
Assert ($module -cmatch 'if\s*\(key\s*==\s*0\)\s*return\s+EAS_RadioBank\.ValidSelection\(value\);') 'radio Recording must be validated by EAS_RadioBank.ValidSelection'
$fixed = [regex]::Match($module, 'if\s*\(key\s*==\s*5\)\s*return\s+value\s*==\s*(\d+);')
Assert $fixed.Success 'radio audible distance must stay fixed'
$radioRange = [int]$fixed.Groups[1].Value

# Audio project graph: one class per line.
$acpPath = Assert-Meta $project.Groups[1].Value $project.Groups[2].Value
$acp = Read-Text $acpPath
$soundIds = @{}; $soundLine = @{}; $soundShader = @{}; $shaderAmp = @{}; $shaderBank = @{}; $ampRange = @{}; $bankSamples = @{}; $nodeIds = @{}
foreach ($line in $acp -split "`n") {
 if ($line -match '^\s*\w+Class \{ id (\d+) ') { Assert (!$nodeIds.ContainsKey($Matches[1])) "duplicate node id $($Matches[1]) in $($project.Groups[2].Value)"; $nodeIds[$Matches[1]] = $true }
 if ($line -match '^\s*SoundClass \{ id (\d+) name "([^"]+)"') {
  Assert (!$soundIds.ContainsKey($Matches[2])) "duplicate sound $($Matches[2])"
  $soundIds[$Matches[2]] = [int]$Matches[1]; $soundLine[$Matches[2]] = $line.TrimEnd()
  $soundShader[[int]$Matches[1]] = [int][regex]::Match($line, 'connections \{ id 64 links \{ ConnectionClass connection \{ id (\d+) port 65').Groups[1].Value
 } elseif ($line -match '^\s*ShaderClass \{ id (\d+) ') {
  $shaderAmp[[int]$Matches[1]] = [int][regex]::Match($line, 'connections \{ id 1 links \{ ConnectionClass connection \{ id (\d+) port 65').Groups[1].Value
  $shaderBank[[int]$Matches[1]] = [int][regex]::Match($line, 'connections \{ id 64 links \{ ConnectionClass connection \{ id (\d+) port 65').Groups[1].Value
 } elseif ($line -match '^\s*AmplitudeClass \{ id (\d+) .*\bouterRange (\d+)') {
  $ampRange[[int]$Matches[1]] = [int]$Matches[2]
 } elseif ($line -match '^\s*BankLocalClass \{ id (\d+) ') {
  $bankSamples[[int]$Matches[1]] = @([regex]::Matches($line, 'Filename "\{([0-9A-F]{16})\}([^"]+)"') | ForEach-Object { [pscustomobject]@{ Guid = $_.Groups[1].Value; Path = $_.Groups[2].Value } })
 }
}
$mixer = @([regex]::Matches(([regex]::Match($acp, '(?m)^\s*MixerClass \{.*$').Value), 'connection \{ id (\d+) port 65 \}') | ForEach-Object { [int]$_.Groups[1].Value })
$credits = Read-Text (Join-Path $sounds 'Credits/AUDIO_CREDITS.txt')
# Workbench writes SoundClass keys in this order: pi, priority, outState, outStatePort, bypassVolumeTest,
# speedOfSoundSimulation (vanilla Weapons_UnderbarrelGrenadeHits.acp). The engine keeps higher-priority
# voices when the playing-source limit is exceeded; bypassVolumeTest stops it choosing by loudness.
function Assert-Kept([string]$Name) {
 Assert $soundLine.ContainsKey($Name) "missing event $Name in $($project.Groups[2].Value)"
 $kept = [regex]::Match($soundLine[$Name], ' pi \{ [^}]*\} priority (\d+) outState \d+ outStatePort \d+ bypassVolumeTest 1 speedOfSoundSimulation 0 \}$')
 Assert ($kept.Success -and [int]$kept.Groups[1].Value -ge 80 -and [int]$kept.Groups[1].Value -le 100) "$Name must keep priority 80-100 and bypassVolumeTest 1 in Workbench key order"
 Assert ($soundLine[$Name] -notmatch '\bnoInAudible\b') "$Name must keep the engine's inaudible-start refusal (no noInAudible key)"
}
$tvBank = Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_TVBank.c')
$tvEvents = @([regex]::Matches((Get-Body $tvBank 'static\s+string\s+Event\s*\('), 'case\s+\d+\s*:\s*return\s+"([A-Za-z0-9_]+)"\s*;') | ForEach-Object { $_.Groups[1].Value })
Assert ($tvEvents.Count -ge 1 -and $tvBank.Contains('"{' + $project.Groups[1].Value + '}' + $project.Groups[2].Value + '"')) 'EAS_TVBank must play its events from the radio project'
foreach ($name in $tvEvents) { Assert-Kept $name }
foreach ($id in @($recordings.Keys | Sort-Object)) {
 $name = $recordings[$id]
 Assert $soundIds.ContainsKey($name) "missing radio event $name in $($project.Groups[2].Value)"
 Assert-Kept $name
 $sound = $soundIds[$name]; $shader = $soundShader[$sound]
 Assert ($mixer -contains $sound) "$name is not routed to the mixer"
 Assert ($shaderAmp.ContainsKey($shader) -and $ampRange[$shaderAmp[$shader]] -eq $radioRange) "$name does not use the radios' $radioRange m amplitude"
 $bank = $shaderBank[$shader]
 Assert ($bankSamples.ContainsKey($bank) -and $bankSamples[$bank].Count -eq 1) "$name must play exactly one sample"
 $sample = $bankSamples[$bank][0]
 Assert ($sample.Path -cmatch '^Audio/EXPBG/AmbientSounds/Samples/RadioTransmissions/[^/]+\.wav$') "radio recording $id ($name) plays $($sample.Path), which is not a radio transmission"
 $seconds = Get-SampleSeconds $sample.Guid $sample.Path
 Assert ($durations.ContainsKey($id) -and [math]::Abs($durations[$id] - $seconds) -lt 0.001) ([string]::Format($culture, "radio Duration({0}) {1} differs from the WAV length {2:F7} s", $id, $durations[$id], $seconds))
 $stem = [IO.Path]::GetFileNameWithoutExtension($sample.Path)
 Assert ($credits -cmatch [regex]::Escape($stem)) "$stem is not credited in AUDIO_CREDITS.txt"
}

# Radio GM Recording selector: exactly the radio recordings and groups, stored as durable IDs.
$edit = Read-Text (Join-Path $sounds 'Configs/Editor/AttributeLists/Edit.conf')
$radioRows = Get-Rows $edit 'EAS_RadioRecordingAttribute'
$radioValues = @($radioRows.Value)
Assert (@($radioValues | Sort-Object -Unique).Count -eq $radioValues.Count) 'radio selector repeats a value'
$expected = @(@($recordings.Keys) + @($groups.Keys) | Sort-Object)
Assert ((@($radioValues | Sort-Object) -join ',') -eq ($expected -join ',')) "radio selector must offer exactly the radio recordings and random groups ($($expected -join ',')), offers $(@($radioValues | Sort-Object) -join ',')"
foreach ($row in $radioRows) { Assert ($row.Name -notmatch $nonRadio) "radio selector offers a non-radio entry: $($row.Name)" }
$attribute = Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_RadioAttribute.c')
Assert ($attribute -cmatch 'if\s*\(!manager\)\s*return\s+SCR_BaseEditorAttributeVar\.CreateInt\(durable\);' -and $attribute -cmatch 'value\s*=\s*m_aValues\[value\]\.GetFloatValue\(\);') 'radio Recording rows must map to durable recording IDs; session/CDF saves store the IDs, never row positions'

# Radio defaults: offered, start Off; military radios open on chatter, not static.
foreach ($radio in $radios) {
 Assert ($radio.Recording.Success -and $recordings.ContainsKey([int]$radio.Recording.Groups[1].Value)) "$($radio.Path) default Recording must be a radio recording"
 Assert ($radio.Enabled.Success -and $radio.Enabled.Groups[1].Value -eq '0') "$($radio.Path) must start Off"
}
foreach ($radio in @($angrc + $r123)) { Assert ([int]$radio.Recording.Groups[1].Value -ne 0) "$($radio.Path) should default to military chatter, not static" }

# Placed sounds: unchanged set, invisible, "Sound: ..." names, full recording list.
$soundBank = Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_SoundBank.c')
$soundRecordings = @([regex]::Matches((Get-Body $soundBank 'static\s+string\s+Event\s*\('), 'case\s+(\d+)\s*:') | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object)
Assert ($soundRecordings.Count -eq 28 -and ($soundRecordings -join ',') -eq ((0..27) -join ',')) 'EAS_SoundBank must keep recordings 0-27'
$soundRows = @((Get-Rows $edit 'EAS_SoundRecordingAttribute').Value | Sort-Object)
Assert (($soundRows -join ',') -eq ($soundRecordings -join ',')) 'placed sound selector must keep all 28 recordings'
$strings = @{}
foreach ($item in [regex]::Matches((Read-Text (Join-Path $sounds 'Language/EAS_Localization.st')), '(?s)\bId\s+"([^"]+)"\s+Target_en_us\s+"([^"]*)"')) { $strings[$item.Groups[1].Value] = $item.Groups[2].Value }
$placed = @($listed | Where-Object { $_.Path -cmatch '^PrefabsEditable/EXPBG/Sounds/E_EXPBG_Sound_\w+\.et$' })
$onDisk = @(Get-ChildItem -LiteralPath (Join-Path $sounds 'PrefabsEditable/EXPBG/Sounds') -File -Filter 'E_EXPBG_Sound_*.et')
Assert ($placed.Count -eq 28 -and $onDisk.Count -eq 28) "the entity browser must list all 28 placed sounds ($($placed.Count) listed, $($onDisk.Count) on disk)"
$used = @{}
foreach ($item in $placed) {
 Assert ($item.Class -ceq 'EAS_SoundModule') "$($item.Path) must stay an EAS_SoundModule"
 Assert ($item.Text -notmatch '\bMeshObject\b') "$($item.Path) must stay invisible"
 Assert ($strings.ContainsKey($item.Name) -and $strings[$item.Name] -cmatch '^Sound: \S') "$($item.Path) must be named 'Sound: ...'"
 Assert ($item.Recording.Success) "$($item.Path) has no Recording"
 $value = [int]$item.Recording.Groups[1].Value
 Assert (!$used.ContainsKey($value) -and $soundRecordings -contains $value) "$($item.Path) Recording $value is unknown or repeated"
 $used[$value] = $true
}

"PASS: browser lists 3 radios ($(@($radios | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_.Path) }) -join ', ')) and 28 placed sounds; Radio Red stays loadable and unlisted; $($recordings.Count) radio recordings + $($groups.Count) random groups resolve to radio transmissions, keep priority and bypassVolumeTest (with $($tvEvents.Count) TV event) and match the radio selector."
