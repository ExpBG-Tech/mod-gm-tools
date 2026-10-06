#requires -Version 7.0
# Portable guard for the Ambient Sounds crowd bank and the Civil Protest Zone crowd sound.
# Every EAS_CrowdBank recording must exist in EXPBG_Radio.acp at every range of the bank's
# range guard (sound, shader, matching amplitude range, mixer output, sample bank), its
# sample .wav must carry a matching .meta GUID/path, and Duration() must equal the WAV's
# data length. Random groups, the GM recording selector and the protest zone's Crowd sound
# choices may only name what the bank defines; every crowd sample is credited, and
# EAS_DistributionNotices.c mirrors AUDIO_CREDITS.txt. No engine is launched.
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
function Read-WavSeconds([string]$Path) {
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
    return ($size / $blockAlign) / $rate
   }
   $stream.Position = $next
  }
  throw "FAIL: no data chunk: $Path"
 } finally { $stream.Dispose() }
}

# Bank source.
$bankText = Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_CrowdBank.c')
$project = [regex]::Match($bankText, 'PROJECT\s*=\s*"\{([0-9A-F]{16})\}([^"]+)"')
Assert $project.Success 'EAS_CrowdBank.PROJECT not found'
$eventBody = Get-Body $bankText 'static\s+string\s+Event\s*\('
$guard = [regex]::Match($eventBody, 'range\s*<\s*(\d+)\s*\|\|\s*range\s*>\s*(\d+)\s*\|\|\s*range\s*%\s*(\d+)\s*!=\s*0')
Assert $guard.Success 'EAS_CrowdBank.Event range guard has an unrecognised form'
$ranges = @(for ($r = [int]$guard.Groups[1].Value; $r -le [int]$guard.Groups[2].Value; $r += [int]$guard.Groups[3].Value) { $r })
$recordings = @{}
foreach ($m in [regex]::Matches($eventBody, 'case\s+(\d+)\s*:\s*return\s+"([A-Z0-9_]+)"\s*\+\s*"_R"\s*\+\s*range\.ToString\(\)\s*;')) { $recordings[[int]$m.Groups[1].Value] = $m.Groups[2].Value }
Assert ($recordings.Count -ge 5 -and $recordings.ContainsKey(4) -and $recordings[4] -ceq 'SOUND_EAS_CROWD_RIOTING') 'crowd recording 4 must be SOUND_EAS_CROWD_RIOTING'
$durations = @{}
foreach ($m in [regex]::Matches((Get-Body $bankText 'static\s+float\s+Duration\s*\('), 'case\s+(\d+)\s*:\s*return\s+([0-9.]+)\s*;')) { $durations[[int]$m.Groups[1].Value] = [double]::Parse($m.Groups[2].Value, $culture) }
$groups = @{}
foreach ($m in [regex]::Matches((Get-Body $bankText 'static\s+int\s+Resolve\s*\('), 'case\s+(\d+)\s*:\s*return\s+EAS_RadioBank\.Pick\(\s*\{([\d,\s]*)\}')) { $groups[[int]$m.Groups[1].Value] = @($m.Groups[2].Value.Split(',') | ForEach-Object { [int]$_.Trim() }) }
$validGroups = @([regex]::Matches((Get-Body $bankText 'static\s+bool\s+ValidSelection\s*\('), 'selection\s*==\s*(\d+)') | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object)
Assert (($validGroups -join ',') -eq (@($groups.Keys | Sort-Object) -join ',')) 'ValidSelection groups differ from Resolve groups'
foreach ($group in $groups.Keys) { foreach ($member in $groups[$group]) { Assert $recordings.ContainsKey($member) "group $group names unknown recording $member" } }

# Audio project graph: one class per line.
$acpPath = Assert-Meta $project.Groups[1].Value $project.Groups[2].Value
$acp = Read-Text $acpPath
$soundIds = @{}; $soundShader = @{}; $shaderAmp = @{}; $shaderBank = @{}; $ampRange = @{}; $bankSamples = @{}
foreach ($line in $acp -split "`n") {
 if ($line -match '^\s*SoundClass \{ id (\d+) name "([^"]+)"') {
  Assert (!$soundIds.ContainsKey($Matches[2])) "duplicate sound $($Matches[2])"
  $soundIds[$Matches[2]] = [int]$Matches[1]
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
$events = 0
$credits = Read-Text (Join-Path $sounds 'Credits/AUDIO_CREDITS.txt')
foreach ($id in @($recordings.Keys | Sort-Object)) {
 $base = $recordings[$id]; $samples = $null
 foreach ($range in $ranges) {
  $name = "${base}_R$range"
  Assert $soundIds.ContainsKey($name) "missing event $name in $($project.Groups[2].Value)"
  $sound = $soundIds[$name]; $shader = $soundShader[$sound]
  Assert ($mixer -contains $sound) "$name is not routed to the mixer"
  Assert ($shaderAmp.ContainsKey($shader) -and $ampRange[$shaderAmp[$shader]] -eq $range) "$name does not use the ${range} m amplitude"
  $bank = $shaderBank[$shader]
  Assert ($bankSamples.ContainsKey($bank) -and $bankSamples[$bank].Count -ge 1) "$name has no sample bank"
  if ($null -eq $samples) { $samples = $bankSamples[$bank] } else { Assert ((@($samples.Path) -join '|') -ceq (@($bankSamples[$bank].Path) -join '|')) "$name plays other samples than the recording's other ranges" }
  $events++
 }
 Assert ($samples.Count -eq 1) "recording $id must play exactly one sample"
 $wav = Assert-Meta $samples[0].Guid $samples[0].Path
 $seconds = Read-WavSeconds $wav
 Assert ($durations.ContainsKey($id) -and [math]::Abs($durations[$id] - $seconds) -lt 0.001) ([string]::Format($culture, "Duration({0}) {1} differs from the WAV length {2:F7} s", $id, $durations[$id], $seconds))
 $stem = [IO.Path]::GetFileNameWithoutExtension($samples[0].Path)
 Assert ($credits -cmatch [regex]::Escape($stem)) "$stem is not credited in AUDIO_CREDITS.txt"
}

# Credits mirror: EAS_DistributionNotices.c starts with AUDIO_CREDITS.txt as // comments.
$notices = (Read-Text (Join-Path $sounds 'Scripts/Game/EXPAS/EAS_DistributionNotices.c')) -split "`n"
$creditLines = $credits.TrimEnd("`n") -split "`n"
for ($i = 0; $i -lt $creditLines.Count; $i++) { Assert ($notices[$i] -ceq ('// ' + $creditLines[$i]).TrimEnd(' ')) "EAS_DistributionNotices.c differs from AUDIO_CREDITS.txt at line $($i + 1)" }

# GM recording selector of the EXPBG Ambient Crowd Sound module.
$edit = Read-Text (Join-Path $sounds 'Configs/Editor/AttributeLists/Edit.conf')
$selector = @([regex]::Matches((Get-Body $edit 'EAS_CrowdRecordingAttribute\s*\{'), 'm_fEntryFloatValue\s+(\d+)') | ForEach-Object { [int]$_.Groups[1].Value })
Assert (@($selector | Sort-Object -Unique).Count -eq $selector.Count) 'crowd selector repeats a value'
foreach ($value in $selector) { Assert ($recordings.ContainsKey($value) -or $groups.ContainsKey($value)) "crowd selector offers unknown selection $value" }
foreach ($id in @($recordings.Keys | Sort-Object)) { Assert ($selector -contains $id) "crowd selector lacks recording $id" }

# Civil Protest Zone: Crowd sound values, default and their crowd selections.
$zone = Read-Text (Join-Path $addon 'ambient-unrest/Scripts/Game/EXPAU/EAU_ProtestZone.c')
$constants = @{}
foreach ($m in [regex]::Matches($zone, 'static\s+const\s+int\s+(\w+)\s*=\s*(-?\d+)\s*;')) { $constants[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
Assert ($recordings.ContainsKey($constants['RECORDING_ANGRY']) -and $recordings.ContainsKey($constants['RECORDING_RIOTING'])) 'protest zone names unknown crowd recordings'
Assert ($groups.ContainsKey($constants['SELECTION_ALTERNATE']) -and ((@($groups[$constants['SELECTION_ALTERNATE']] | Sort-Object) -join ',') -eq (@($constants['RECORDING_ANGRY'], $constants['RECORDING_RIOTING']) | Sort-Object) -join ',')) 'protest zone Alternate group must hold exactly the angry and rioting recordings'
$choices = @($constants['SOUND_ANGRY'], $constants['SOUND_RIOTING'], $constants['SOUND_ALTERNATE'])
$zoneEdit = Read-Text (Join-Path $addon 'ambient-unrest/Configs/Editor/AttributeLists/Edit.conf')
$soundBlock = Get-Body $zoneEdit 'EAU_CrowdSoundAttribute\s*\{'
Assert ([regex]::Match($soundBlock, '\bm_Key\s+(\d+)').Groups[1].Value -eq [string]$constants['KEY_SOUND']) 'Crowd sound attribute key differs from EAU_ProtestZone.KEY_SOUND'
$rows = @([regex]::Matches($soundBlock, 'm_fEntryFloatValue\s+(\d+)') | ForEach-Object { [int]$_.Groups[1].Value })
Assert (($rows -join ',') -eq ($choices -join ',')) 'Crowd sound rows differ from the zone SOUND_ values'
$default = [regex]::Match($zone, '\[Attribute\("(\d+)"[^\]]*\]\s*int\s+CrowdSound\s*;')
Assert ($default.Success -and [int]$default.Groups[1].Value -eq $constants['SOUND_ALTERNATE']) 'new zones must default to Alternate'

"PASS: $($recordings.Count) crowd recordings x $($ranges.Count) ranges ($events events) resolve in the audio project with matching samples, durations and credits; groups, GM selector and protest Crowd sound agree."
