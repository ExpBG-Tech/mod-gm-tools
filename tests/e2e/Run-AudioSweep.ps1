#requires -Version 7.0
<#
.SYNOPSIS
 Verifies that every audio recording shipped by addon/ambient-sounds resolves and starts playing in the
 native engine, and writes a pass/fail list per event and per recording.

.DESCRIPTION
 Native entry point (Run-*; never discovered by tests/Test-Tools.ps1). Two layers, one command:

 1. Static resolution (portable; always runs first; alone with -StaticOnly).
    Parses the pack's bank sources (EAS_Bank, EAS_RadioBank, EAS_CrowdBank, EAS_TVBank, EAS_SoundBank,
    EAS_ContentSettings, EAS_Runtime) into every event name the runtimes can request, then checks each
    one in the three .acp audio projects: SoundClass present exactly once, shader, amplitude branch whose
    outerRange equals the _R<range> suffix, mixer output, EAS_Gain signal (.sig resolves, GUID matches,
    exposes the EAS_Gain input the runtimes pass), sample bank, and every sample .wav present with a
    matching .meta GUID/path and a valid RIFF/WAVE header. Since 0.1.16 the samples ship in the dependency
    EXPBG Audio Data: they are read from -SampleDir (default: the mod-audio-data folder beside this
    repository, tools/audio-data.json). Bank durations are compared with the WAV data
    length (warning). Random groups (radio 100-103, crowd 100) may only name existing recordings.

 2. Native playback (needs -OrchestratorSlotGranted and a free native slot).
    Launches ArmaReforgerSteamDiag.exe, the diagnostic CLIENT, directly into an offline world (-world: no
    RplSession, no port, no backend join) with EXPBG GM Tools and the test-only harness addon
    tests/e2e/AudioSweep (staged into the run directory, never into addon/**). For every case the harness
    calls AudioSystem.PlayEvent with the same project, event name and EAS_Gain parameter the runtimes use,
    at the listener, and logs:
      [EAS DIAG] action=play ... runtime=e2e-sweep seq=K ... event=NAME handle=N ... duration=S
      [EXPE2E AUDIO] bank=B id=I event=NAME range=R result=PASS|FAIL reason=WHY ... handle=N ...
    The [EAS DIAG] line goes through the pack's own EAS_Diagnostics (-easDiagnostics 1).
    An event FAILS when: the handle is missing, -1, 0 or AudioHandle.Invalid; the play line is missing or
    disagrees; the voice ended before its recorded duration allows; an AUDIO/SOUND/RESOURCES error naming
    the pack or the event appears between its BEGIN and result lines; the event is not in the source
    graph; or it failed static resolution. The run also fails when the harness skipped or invented a case
    (exact inventory comparison with the bank sources), a random group drew an invalid recording, any
    sample file / graph node / recording of the three projects was not played by a passing event (all
    banks selected), the result line is missing or inconsistent, scripts failed to compile, the engine
    crashed or a VM exception mentions EXPE2E/EAS.

 Why the client and not ArmaReforgerServerDiag (the Run-Gameplay.ps1 binary): a dedicated server has no
 audio device or listener, and the pack's runtimes return before PlayEvent when System.IsConsoleApp()
 (EAS_Runtime.Play, EAS_RadioRuntime.Tick). A server-side sweep would only prove that nothing plays. The
 harness refuses to start in a console app for the same reason. The machine needs an audio output
 endpoint; never pass -noSound.

 Modes: quick (every recording at its authored default range + every range of one recording per range
 bank = every sample, every graph node), standard (default: quick for war; every recording x every range
 for crowd and Vinny sounds = every event of EXPBG_Radio.acp and EXPBG_Sounds.acp), full (all 2062 events).
 Per event the sweep takes about hold + 0.4 s; standard is ~500 events (~16 min plus world load).

 Evidence: build/audio-sweep-<utc>/ holds static-summary.json, static-events.csv, plan.json, inputs.json,
 run.json, result.json, logs/, summary.json, events.csv (native, one row per played event) and
 recordings.csv (one row per recording / random group: static and native verdict).
 Exit code 0 only when every gate passed. PASS proves resolution and native start/playback only, not
 audible loudness, mixing, replication or GM module behaviour.

.PARAMETER Mode
 quick | standard | full. Default standard.
.PARAMETER Banks
 Optional subset, comma separated: war,radio,crowd,tv,sound. Coverage gates need all banks.
.PARAMETER LaunchMode
 world (default): -world <World>, offline. server: -server <World>, a listen-server host session (fallback
 if a world does not start its game mode offline).
.PARAMETER SampleDir
 EXPBG Audio Data folder holding the samples (its project folder or a local build). Default: the
 mod-audio-data folder beside this repository (tools/audio-data.json).
.PARAMETER GmToolsDir
 Use this EXPBG GM Tools addon directory (e.g. a local build) instead of the installed Workshop copy.
.PARAMETER ModsetPath
 Also load every addon of this modset JSON (e.g. .local/e2e/modset.json). Default: GM Tools only.
.PARAMETER OrchestratorSlotGranted
 Required to launch the client. Without it only -StaticOnly, -SelfTest, -ParseLog and -DryRun work.
.PARAMETER SkipStaticGate
 Launch even when static resolution failed (the failures still fail the run).
.PARAMETER StaticOnly
 Run only the portable static resolution checks.
.PARAMETER SelfTest
 Check the log parser against synthetic logs (positive and negative cases). No engine, no files.
.PARAMETER ParseLog
 Re-parse an existing console.log; writes summary/events/recordings beside its run directory.
.PARAMETER DryRun
 Resolve inputs, write plan.json and print the client command line; nothing is launched.

.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -StaticOnly
 Portable: every event name, signal, sample file and WAV header resolves. No engine.
.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -SelfTest
 Portable: the evidence parser accepts a complete synthetic log and rejects broken ones.
.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -DryRun
 Prints the exact client command line; launches nothing.
.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -OrchestratorSlotGranted
 The real sweep (standard). Requires no running Arma Reforger / Workbench / server process.
.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -OrchestratorSlotGranted -Mode full -TimeoutSeconds 10800
 Every event of every bank natively (about 70 minutes plus world load).
.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -OrchestratorSlotGranted -GmToolsDir "$HOME/Documents/My Games/ArmaReforgerWorkbench/addons/EXPBG_GM_Tools"
 Sweep a local build instead of the installed Workshop copy.
.EXAMPLE
 pwsh -NoProfile -File tests/e2e/Run-AudioSweep.ps1 -ParseLog build/audio-sweep-20261006-010203-004/logs/console.log
 Re-evaluate an existing run without launching anything.
#>
[CmdletBinding()]
param(
 [ValidateSet('quick', 'standard', 'full')][string]$Mode = 'standard',
 [ValidatePattern('^((war|radio|crowd|tv|sound)(,(war|radio|crowd|tv|sound))*)?$')][string]$Banks = '',
 [ValidateSet('world', 'server')][string]$LaunchMode = 'world',
 [string]$World = 'worlds/GameMaster/GM_Eden.ent',
 [AllowEmptyString()][string]$WorldSystemsConfig = '{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf',
 [ValidateRange(0, 600)][int]$SettleSeconds = 20,
 [ValidateRange(500, 10000)][int]$HoldMilliseconds = 1500,
 [int]$TimeoutSeconds = 0,
 [string]$GameDir = '',
 [string]$AddonsDir = '',
 [string]$GmToolsDir = '',
 [string]$ModsetPath = '',
 [switch]$StrictVersions,
 [switch]$UseDefaultProfile,
 [switch]$OrchestratorSlotGranted,
 [switch]$SkipStaticGate,
 [string]$SourceDir = '',
 [string]$SampleDir = '',
 [switch]$StaticOnly,
 [switch]$SelfTest,
 [string]$ParseLog = '',
 [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$harnessSource = Join-Path $PSScriptRoot 'AudioSweep'
$harnessName = 'EXPE2E_AudioSweep'
$harnessScript = Join-Path $harnessSource 'Scripts/Game/EXPE2E_AudioSweep.c'
$gmToolsGuid = 'FC1402F65B2F4A45'
$builtinGuids = @('58D0FB3206B6F859', '5614BBCCBB55ED1C') # Arma Reforger data, core
$diagExeName = 'ArmaReforgerSteamDiag.exe'
if (!$SourceDir) { $SourceDir = Join-Path $repo 'addon/ambient-sounds' }
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
# The samples ship in EXPBG Audio Data since 0.1.16 (tools/audio-data.json); the .acp and .sig stay in SourceDir.
if (!$SampleDir) {
 $audioData = Get-Content -LiteralPath (Join-Path $repo 'tools/audio-data.json') -Raw | ConvertFrom-Json
 $SampleDir = Join-Path (Split-Path -Parent $repo) $audioData.dependency.folder
}
$SampleDir = [IO.Path]::GetFullPath($SampleDir)
if (!(Test-Path -LiteralPath $SampleDir -PathType Container)) { Write-Warning "Sample folder $SampleDir not found: every sample fails static resolution. Put mod-audio-data beside this repository or pass -SampleDir." }

#region Common helpers ------------------------------------------------------------------------------
function Write-Utf8File([string]$Path, [string]$Text) {
 [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Write-JsonFile([string]$Path, $Value, [int]$Depth = 8) {
 Write-Utf8File $Path ($Value | ConvertTo-Json -Depth $Depth)
}

function Write-CsvFile([string]$Path, $Rows) {
 $items = @($Rows)
 if (!$items.Count) { Write-Utf8File $Path ''; return }
 Write-Utf8File $Path ((@($items | ConvertTo-Csv -NoTypeInformation) -join "`n") + "`n")
}

function Read-SharedText([string]$Path) {
 $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
 $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
 try {
  $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
  try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
 } finally { $stream.Dispose() }
}

function ConvertTo-IntOrDefault($Value, [int]$Default = -2147483648) {
 $parsed = 0
 if ([int]::TryParse([string]$Value, [Globalization.NumberStyles]::Integer, [Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)) { return $parsed }
 return $Default
}

function Get-BankList([string]$Value) {
 if (!$Value -or $Value -eq 'all') { return @() }
 return @($Value.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

function Read-Gproj([string]$Path) {
 $text = Get-Content -LiteralPath $Path -Raw
 $guid = [regex]::Match($text, '\bGUID\s+"?([0-9A-Fa-f]{16})\b')
 if (!$guid.Success) { return $null }
 $id = [regex]::Match($text, '\bID\s+"([^"]+)"')
 $block = [regex]::Match($text, '(?s)Dependencies\s*\{([^}]*)\}')
 $dependencies = @()
 if ($block.Success) { $dependencies = @([regex]::Matches($block.Groups[1].Value, '[0-9A-Fa-f]{16}') | ForEach-Object { $_.Value.ToUpperInvariant() }) }
 [pscustomobject]@{ Guid = $guid.Groups[1].Value.ToUpperInvariant(); Id = $(if ($id.Success) { $id.Groups[1].Value } else { '' }); Dependencies = $dependencies }
}

function Get-AddonInfo([string]$Dir) {
 $gproj = Get-ChildItem -LiteralPath $Dir -File | Where-Object Extension -EQ '.gproj' | Select-Object -First 1
 if (!$gproj) { return $null }
 $project = Read-Gproj $gproj.FullName
 if (!$project) { return $null }
 $version = $null
 $corrupted = $null
 $serverData = Join-Path $Dir 'ServerData.json'
 if (Test-Path -LiteralPath $serverData -PathType Leaf) {
  try {
   $data = Get-Content -LiteralPath $serverData -Raw | ConvertFrom-Json
   $version = [string]$data.revision.version
   $corrupted = [bool]$data.revision.corrupted
  } catch { $version = $null }
 }
 if (!$version) {
  $manifest = Get-ChildItem -LiteralPath $Dir -File -Filter '*_manifest.json' | Select-Object -First 1
  if ($manifest -and $manifest.Name -match '_(\d+(?:\.\d+)+)_manifest\.json$') { $version = $Matches[1] }
 }
 [pscustomobject]@{
  Guid = $project.Guid; Id = $project.Id; Dir = (Get-Item -LiteralPath $Dir).FullName; Gproj = $gproj.FullName; Dependencies = $project.Dependencies
  Version = $version; Corrupted = $corrupted
  Packed = (Test-Path -LiteralPath (Join-Path $Dir 'data.pak') -PathType Leaf)
  Indexed = (Test-Path -LiteralPath (Join-Path $Dir 'resourceDatabase.rdb') -PathType Leaf)
  Duplicates = [Collections.Generic.List[string]]::new()
 }
}

function Get-AddonIndex([string[]]$Roots) {
 $index = @{}
 foreach ($root in $Roots) {
  foreach ($dir in Get-ChildItem -LiteralPath $root -Directory) {
   $info = Get-AddonInfo $dir.FullName
   if (!$info) { continue }
   if ($index.ContainsKey($info.Guid)) { if ($index[$info.Guid].Dir -ne $info.Dir) { $index[$info.Guid].Duplicates.Add($info.Dir) }; continue }
   $index[$info.Guid] = $info
  }
 }
 $index
}

function Get-MetaInfo([string]$MetaPath) {
 if (!(Test-Path -LiteralPath $MetaPath -PathType Leaf)) { return $null }
 $match = [regex]::Match((Get-Content -LiteralPath $MetaPath -Raw), 'Name\s+"\{([0-9A-Fa-f]{16})\}([^"\r\n]+)"')
 if (!$match.Success) { return $null }
 [pscustomobject]@{ Guid = $match.Groups[1].Value.ToUpperInvariant(); Name = $match.Groups[2].Value }
}

function Format-CommandLine([string]$Executable, [string[]]$Arguments) {
 $quoted = foreach ($argument in @($Executable) + $Arguments) { if ($argument -match '[\s,"]') { '"' + $argument.Replace('"', '\"') + '"' } else { $argument } }
 $quoted -join ' '
}

function Get-NativeProcesses {
 # Any Arma or Workbench process means another owner may hold the native slot.
 @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '(?i)^arma|workbench' })
}

function Assert-NativeSlot {
 $busy = Get-NativeProcesses
 if (@($busy).Count) {
  $names = ($busy | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ', '
  throw "Native slot occupied ($names). No process is stopped by this script."
 }
}

function Stop-OwnedProcess($Process, [datetime]$StartedUtc, [string]$Executable) {
 # Only the process launched by this run may be stopped; fail closed on identity drift.
 $current = Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
 if (!$current) { return $false }
 if ($current.StartTime.ToUniversalTime().Ticks -ne $StartedUtc.ToUniversalTime().Ticks -or [IO.Path]::GetFullPath($current.Path) -ine [IO.Path]::GetFullPath($Executable)) {
  throw 'Process ownership could not be verified; no process was stopped.'
 }
 $Process.Kill($true)
 if (!$Process.WaitForExit(20000)) { throw "Owned process $($Process.Id) did not exit after being stopped." }
 return $true
}
#endregion

#region Static model: bank sources, harness constants, audio graph ---------------------------------
function Get-FunctionBody([string]$Text, [string]$Signature) {
 # Body (without the outer braces) of the first function whose header matches $Signature.
 $match = [regex]::Match($Text, $Signature)
 if (!$match.Success) { return $null }
 $open = $Text.IndexOf('{', $match.Index)
 if ($open -lt 0) { return $null }
 $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ }
  elseif ($Text[$i] -eq '}') {
   $depth--
   if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) }
  }
 }
 return $null
}

function Get-WavInfo([string]$Path) {
 $stream = [IO.File]::OpenRead($Path)
 try {
  $reader = [IO.BinaryReader]::new($stream)
  if ($stream.Length -lt 12) { return [pscustomobject]@{ Ok = $false; Error = 'too-short'; Seconds = 0.0 } }
  $riff = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
  $null = $reader.ReadUInt32()
  $wave = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
  if ($riff -ne 'RIFF' -or $wave -ne 'WAVE') { return [pscustomobject]@{ Ok = $false; Error = 'not-riff-wave'; Seconds = 0.0 } }
  $format = 0; $channels = 0; $rate = 0; $byteRate = 0; $bits = 0; $data = -1L
  while ($stream.Position + 8 -le $stream.Length) {
   $id = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
   $size = [long]$reader.ReadUInt32()
   $next = $stream.Position + $size + ($size % 2)
   if ($id -eq 'fmt ' -and $size -ge 16) {
    $format = $reader.ReadUInt16(); $channels = $reader.ReadUInt16(); $rate = $reader.ReadUInt32(); $byteRate = $reader.ReadUInt32()
    $null = $reader.ReadUInt16(); $bits = $reader.ReadUInt16()
   } elseif ($id -eq 'data') {
    $data = $size
    if ($stream.Position + $size -gt $stream.Length) { return [pscustomobject]@{ Ok = $false; Error = 'data-chunk-truncated'; Seconds = 0.0 } }
   }
   if ($next -gt $stream.Length) { break }
   $stream.Position = $next
  }
  if (!$byteRate -or !$channels -or !$rate) { return [pscustomobject]@{ Ok = $false; Error = 'fmt-chunk-missing'; Seconds = 0.0 } }
  if ($data -le 0) { return [pscustomobject]@{ Ok = $false; Error = 'data-chunk-missing'; Seconds = 0.0 } }
  [pscustomobject]@{ Ok = $true; Error = ''; Seconds = [double]$data / $byteRate; Format = $format; Channels = $channels; Rate = $rate; Bits = $bits }
 } finally { $stream.Dispose() }
}

function Get-AudioGraph([string]$AudioRoot) {
 # The .acp files store one class per line. Event (SoundClass) -> shader -> spatiality, amplitude
 # (range branch), optional frequency filter and bank (sample files); SoundClass -> signal, mixer.
 $graph = @{}
 foreach ($acp in @(Get-ChildItem -LiteralPath $AudioRoot -File -Filter '*.acp' | Where-Object Extension -EQ '.acp' | Sort-Object Name)) {
  $meta = Get-MetaInfo ($acp.FullName + '.meta')
  if (!$meta) { throw "Audio project metadata without GUID: $($acp.Name)" }
  $sounds = @{}
  $shaderTargets = @{}
  $nodes = @{}
  $duplicates = [Collections.Generic.List[string]]::new()
  foreach ($line in [IO.File]::ReadLines($acp.FullName)) {
   $class = [regex]::Match($line, '^\s*(?<class>[A-Za-z]+Class) \{ id (?<id>\d+) name "(?<name>[^"]*)"')
   if (!$class.Success) { continue }
   $kind = $class.Groups['class'].Value
   $id = $class.Groups['id'].Value
   $name = $class.Groups['name'].Value
   if ($kind -eq 'SoundClass') {
    if ($sounds.ContainsKey($name)) { $duplicates.Add($name); continue }
    $shader = [regex]::Match($line, 'connections \{ id 64 links \{ ConnectionClass connection \{ id (\d+)')
    $signal = [regex]::Match($line, 'connections \{ id 1 links \{ ConnectionClass connection \{ id (\d+)')
    $out = [regex]::Match($line, '\boutState (\d+)')
    $sounds[$name] = [pscustomobject]@{
     Shader = $(if ($shader.Success) { $shader.Groups[1].Value } else { '' })
     Signal = $(if ($signal.Success) { $signal.Groups[1].Value } else { '' })
     OutState = $(if ($out.Success) { $out.Groups[1].Value } else { '' })
    }
   } elseif ($kind -eq 'ShaderClass') {
    $shaderTargets[$id] = @([regex]::Matches($line, 'ConnectionClass connection \{ id (\d+)') | ForEach-Object { $_.Groups[1].Value })
   } else {
    $refs = @([regex]::Matches($line, 'Filename "(?:\{(?<guid>[0-9A-Fa-f]{16})\})?(?<path>[^"]+)"') | ForEach-Object { [pscustomobject]@{ Guid = $_.Groups['guid'].Value.ToUpperInvariant(); Path = $_.Groups['path'].Value } })
    $res = [regex]::Match($line, '\bres "(?:\{(?<guid>[0-9A-Fa-f]{16})\})?(?<path>[^"]+)"')
    $outer = [regex]::Match($line, '\bouterRange (\d+(?:\.\d+)?)')
    $nodes[$id] = [pscustomobject]@{
     Id = $id; Class = $kind; Name = $name; Samples = @($refs | ForEach-Object { $_.Path }); SampleRefs = $refs
     Res = $(if ($res.Success) { [pscustomobject]@{ Guid = $res.Groups['guid'].Value.ToUpperInvariant(); Path = $res.Groups['path'].Value } } else { $null })
     OuterRange = $(if ($outer.Success) { [double]::Parse($outer.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) } else { $null })
    }
   }
  }
  $events = @{}
  foreach ($eventName in $sounds.Keys) {
   $sound = $sounds[$eventName]
   $targets = @()
   $shaderFound = $false
   if ($sound.Shader -and $shaderTargets.ContainsKey($sound.Shader)) {
    $shaderFound = $true
    $targets = @($shaderTargets[$sound.Shader] | Where-Object { $nodes.ContainsKey($_) })
   }
   $samples = @($targets | ForEach-Object { $nodes[$_].Samples } | Where-Object { $_ })
   $events[$eventName] = [pscustomobject]@{ Name = $eventName; Nodes = $targets; Samples = $samples; ShaderFound = $shaderFound; Signal = $sound.Signal; OutState = $sound.OutState }
  }
  $graph[$meta.Guid] = [pscustomobject]@{ Guid = $meta.Guid; Path = $meta.Name; File = $acp.Name; Events = $events; Nodes = $nodes; Duplicates = @($duplicates) }
 }
 if (!$graph.Count) { throw "No audio projects found under $AudioRoot" }
 $graph
}

function Get-BankModel([string]$ModuleDir) {
 # Independent re-derivation of every event name the pack can request, from its own sources.
 $scripts = Join-Path $ModuleDir 'Scripts/Game/EXPAS'
 $problems = [Collections.Generic.List[string]]::new()
 $numberPattern = '-?\d+(?:\.\d+)?'
 $culture = [Globalization.CultureInfo]::InvariantCulture
 $banks = [ordered]@{}
 $definitions = @(
  @{ Name = 'war'; Class = 'EAS_Bank'; File = 'EAS_Bank.c' }
  @{ Name = 'radio'; Class = 'EAS_RadioBank'; File = 'EAS_RadioBank.c' }
  @{ Name = 'crowd'; Class = 'EAS_CrowdBank'; File = 'EAS_CrowdBank.c' }
  @{ Name = 'tv'; Class = 'EAS_TVBank'; File = 'EAS_TVBank.c' }
  @{ Name = 'sound'; Class = 'EAS_SoundBank'; File = 'EAS_SoundBank.c' }
 )
 foreach ($definition in $definitions) {
  $path = Join-Path $scripts $definition.File
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Bank source missing: $path" }
  $text = Get-Content -LiteralPath $path -Raw
  $project = [regex]::Match($text, 'static\s+const\s+ResourceName\s+PROJECT\s*=\s*"\{([0-9A-Fa-f]{16})\}([^"]+)"')
  if (!$project.Success) { throw "$($definition.File): PROJECT constant not found" }
  $recordings = [Collections.Generic.List[object]]::new()
  $ranged = $false
  $ranges = @()
  $groups = @{}
  if ($definition.Name -eq 'war') {
   $body = Get-FunctionBody $text 'static\s+array\s*<\s*ref\s+EAS_Clip\s*>\s*Create\s*\('
   if ($null -eq $body) { throw 'EAS_Bank.c: Create() not found' }
   $index = 0
   foreach ($clip in [regex]::Matches($body, 'bank\.Insert\(\s*new\s+EAS_Clip\(\s*"([^"]+)"\s*,\s*(' + $numberPattern + ')')) {
    $recordings.Add([pscustomobject]@{ Id = $index; Base = $clip.Groups[1].Value; Duration = [double]::Parse($clip.Groups[2].Value, $culture) })
    $index++
   }
   $runtime = Get-Content -LiteralPath (Join-Path $scripts 'EAS_Runtime.c') -Raw
   if ($runtime -notmatch 'PlayEvent\(\s*EAS_Bank\.PROJECT\s*,\s*clip\.EventName\s*\+\s*"_R"\s*\+\s*range\.ToString\(\)') { $problems.Add('EAS_Runtime.c no longer plays war events as clip.EventName + "_R" + range; update the harness and this runner') }
   $settings = Get-Content -LiteralPath (Join-Path $scripts 'EAS_ContentSettings.c') -Raw
   $valid = Get-FunctionBody $settings 'static\s+bool\s+ValidValue\s*\('
   $cap = [regex]::Match([string]$valid, 'value\s*>\s*(\d+)')
   $rule = [regex]::Match([string]$valid, 'case\s+3\s*:\s*return\s+number\s*>=\s*(\d+)\s*&&\s*number\s*%\s*(\d+)\s*==\s*0\s*;')
   if (!$cap.Success -or !$rule.Success) { $problems.Add('EAS_ContentSettings.ValidValue key 3 (war audible distance) has an unrecognised form') }
   else {
    $ranged = $true
    $ranges = @(for ($range = [int]$rule.Groups[1].Value; $range -le [int]$cap.Groups[1].Value; $range += [int]$rule.Groups[2].Value) { $range })
   }
  } else {
   $eventBody = Get-FunctionBody $text 'static\s+string\s+Event\s*\('
   $durationBody = Get-FunctionBody $text 'static\s+float\s+Duration\s*\('
   if ($null -eq $eventBody -or $null -eq $durationBody) { throw "$($definition.File): Event() or Duration() not found" }
   $durations = @{}
   foreach ($entry in [regex]::Matches($durationBody, 'case\s+(\d+)\s*:\s*return\s+(' + $numberPattern + ')\s*;')) { $durations[[int]$entry.Groups[1].Value] = [double]::Parse($entry.Groups[2].Value, $culture) }
   $suffixes = @()
   foreach ($entry in [regex]::Matches($eventBody, 'case\s+(\d+)\s*:\s*return\s+"([^"]+)"(\s*\+\s*"_R"\s*\+\s*range\.ToString\(\))?\s*;')) {
    $id = [int]$entry.Groups[1].Value
    $suffixes += [bool]$entry.Groups[3].Success
    $duration = 0.0
    if ($durations.ContainsKey($id)) { $duration = $durations[$id] } else { $problems.Add("$($definition.Class).Duration has no entry for recording $id") }
    $recordings.Add([pscustomobject]@{ Id = $id; Base = $entry.Groups[2].Value; Duration = $duration })
   }
   if (@($suffixes | Sort-Object -Unique).Count -gt 1) { $problems.Add("$($definition.Class).Event mixes ranged and unranged event names") }
   $ranged = @($suffixes | Where-Object { $_ }).Count -gt 0
   if ($definition.Name -eq 'crowd') {
    $window = [regex]::Match($eventBody, 'range\s*<\s*(\d+)\s*\|\|\s*range\s*>\s*(\d+)\s*\|\|\s*range\s*%\s*(\d+)\s*!=\s*0')
    if (!$window.Success) { $problems.Add('EAS_CrowdBank.Event range guard has an unrecognised form') }
    else { $ranges = @(for ($range = [int]$window.Groups[1].Value; $range -le [int]$window.Groups[2].Value; $range += [int]$window.Groups[3].Value) { if ($range % [int]$window.Groups[3].Value -eq 0) { $range } }) }
   } elseif ($definition.Name -eq 'sound') {
    $validBody = Get-FunctionBody $text 'static\s+bool\s+ValidRange\s*\('
    $ranges = @([regex]::Matches([string]$validBody, 'case\s+(\d+)\s*:') | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object -Unique)
    if (!$ranges.Count) { $problems.Add('EAS_SoundBank.ValidRange lists no ranges') }
   }
   if ($ranged -and !$ranges.Count) { $problems.Add("$($definition.Class) builds ranged events but no range rule was recognised") }
   $resolveBody = Get-FunctionBody $text 'static\s+int\s+Resolve\s*\('
   foreach ($entry in [regex]::Matches([string]$resolveBody, 'case\s+(\d+)\s*:\s*return\s+EAS_RadioBank\.Pick\(\s*\{([\d,\s]*)\}')) {
    $groups[[int]$entry.Groups[1].Value] = @($entry.Groups[2].Value.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ } | ForEach-Object { [int]$_ })
   }
  }
  if (!$recordings.Count) { $problems.Add("$($definition.Class) defines no recordings") }
  $byId = @{}
  foreach ($recording in $recordings) {
   if ($byId.ContainsKey($recording.Id)) { $problems.Add("$($definition.Class) defines recording $($recording.Id) twice") }
   $byId[$recording.Id] = $recording
  }
  $banks[$definition.Name] = [pscustomobject]@{
   Name = $definition.Name; Class = $definition.Class; File = $definition.File
   Project = '{' + $project.Groups[1].Value.ToUpperInvariant() + '}' + $project.Groups[2].Value
   ProjectGuid = $project.Groups[1].Value.ToUpperInvariant(); ProjectPath = $project.Groups[2].Value
   Ranged = $ranged; Ranges = @($ranges)
   Recordings = @($recordings | Sort-Object Id); ById = $byId
   Groups = $groups; GroupIds = @($groups.Keys | Sort-Object)
  }
 }
 # Authored defaults (prefabs) for drift warnings against the harness tables.
 $prefabs = Join-Path $ModuleDir 'PrefabsEditable/EXPBG'
 $authored = [ordered]@{ war = $null; crowd = $null; sound = @{} }
 $warPrefab = Join-Path $prefabs 'E_EXPBG_AmbientSounds.et'
 if (Test-Path -LiteralPath $warPrefab) { $value = [regex]::Match((Get-Content -LiteralPath $warPrefab -Raw), '(?m)^\s*Range\s+(\d+)'); if ($value.Success) { $authored.war = [int]$value.Groups[1].Value } }
 $crowdPrefab = Join-Path $prefabs 'Crowd/E_EXPBG_Crowd.et'
 if (Test-Path -LiteralPath $crowdPrefab) { $value = [regex]::Match((Get-Content -LiteralPath $crowdPrefab -Raw), '(?m)^\s*Range\s+(\d+)'); if ($value.Success) { $authored.crowd = [int]$value.Groups[1].Value } }
 $soundPrefabs = Join-Path $prefabs 'Sounds'
 if (Test-Path -LiteralPath $soundPrefabs) {
  foreach ($prefab in Get-ChildItem -LiteralPath $soundPrefabs -File -Filter '*.et') {
   $text = Get-Content -LiteralPath $prefab.FullName -Raw
   $recording = [regex]::Match($text, '(?m)^\s*Recording\s+(\d+)'); $range = [regex]::Match($text, '(?m)^\s*Range\s+(\d+)')
   if ($recording.Success -and $range.Success) { $authored.sound[[int]$recording.Groups[1].Value] = [int]$range.Groups[1].Value }
  }
 }
 [pscustomobject]@{ Banks = $banks; Authored = $authored; Problems = @($problems) }
}

function Get-HarnessSettings([string]$Path) {
 $text = Get-Content -LiteralPath $Path -Raw
 $constants = @{}
 foreach ($entry in [regex]::Matches($text, 'static\s+const\s+int\s+(\w+)\s*=\s*(-?\d+)\s*;')) { $constants[$entry.Groups[1].Value] = [int]$entry.Groups[2].Value }
 foreach ($name in 'WAR_DEFAULT_RANGE', 'CROWD_DEFAULT_RANGE', 'WAR_SWEEP_CLIP', 'CROWD_SWEEP_RECORDING', 'SOUND_SWEEP_RECORDING', 'MAX_RECORDING', 'GROUP_FIRST', 'GROUP_LAST') {
  if (!$constants.ContainsKey($name)) { throw "Harness constant $name not found in $Path" }
 }
 $body = Get-FunctionBody $text 'static\s+int\s+SoundDefaultRange\s*\('
 if ($null -eq $body) { throw "SoundDefaultRange not found in $Path" }
 $defaults = @{}
 foreach ($entry in [regex]::Matches($body, 'case\s+(\d+)\s*:\s*return\s+(\d+)\s*;')) { $defaults[[int]$entry.Groups[1].Value] = [int]$entry.Groups[2].Value }
 $tableFallback = [regex]::Match($body, 'return\s+(\d+)\s*;\s*$')
 $invalidFallback = [regex]::Match($text, 'if\s*\(\s*!EAS_SoundBank\.ValidRange\(defaultRange\)\s*\)\s*defaultRange\s*=\s*(\d+)\s*;')
 [pscustomobject]@{
  WarDefault = $constants['WAR_DEFAULT_RANGE']; CrowdDefault = $constants['CROWD_DEFAULT_RANGE']
  WarSweep = $constants['WAR_SWEEP_CLIP']; CrowdSweep = $constants['CROWD_SWEEP_RECORDING']; SoundSweep = $constants['SOUND_SWEEP_RECORDING']
  MaxRecording = $constants['MAX_RECORDING']; GroupFirst = $constants['GROUP_FIRST']; GroupLast = $constants['GROUP_LAST']
  SoundDefaults = $defaults
  SoundTableFallback = $(if ($tableFallback.Success) { [int]$tableFallback.Groups[1].Value } else { 50 })
  SoundInvalidFallback = $(if ($invalidFallback.Success) { [int]$invalidFallback.Groups[1].Value } else { 50 })
 }
}

function Get-EventName($Bank, [int]$Id, [int]$Range) {
 if (!$Bank.ById.ContainsKey($Id)) { return '' }
 if (!$Bank.Ranged) { return $Bank.ById[$Id].Base }
 if ($Range -lt 0) { return '' }
 return $Bank.ById[$Id].Base + '_R' + $Range
}

function Get-DefaultRange([string]$Name, $Bank, [int]$Id, $Harness) {
 # Mirrors the harness: authored default, else the first valid range (war, crowd) or 50 (sound).
 if ($Name -eq 'war' -or $Name -eq 'crowd') {
  $default = $(if ($Name -eq 'war') { $Harness.WarDefault } else { $Harness.CrowdDefault })
  if ($Bank.Ranges -notcontains $default -and $Bank.Ranges.Count) { $default = $Bank.Ranges[0] }
  return $default
 }
 if ($Name -eq 'sound') {
  $default = $Harness.SoundTableFallback
  if ($Harness.SoundDefaults.ContainsKey($Id)) { $default = $Harness.SoundDefaults[$Id] }
  if ($Bank.Ranges -notcontains $default) { $default = $Harness.SoundInvalidFallback }
  return $default
 }
 return -1
}

function New-Case([string]$Name, $Bank, [int]$Id, [int]$Range, [bool]$IsDefault) {
 [pscustomobject]@{ Key = "$Name|$Id|$Range"; Bank = $Name; Id = $Id; Range = $Range; Event = (Get-EventName $Bank $Id $Range); Project = $Bank.Project; Default = $IsDefault; Group = $false; Candidates = @(); CandidateEvents = @() }
}

function Get-ExpectedCases($Model, $Harness, [string]$Mode, [string[]]$BankList) {
 # The exact case list the harness must report for this mode and bank selection.
 $cases = [Collections.Generic.List[object]]::new()
 foreach ($name in $Model.Banks.Keys) {
  if ($BankList.Count -and $BankList -notcontains $name) { continue }
  $bank = $Model.Banks[$name]
  $allRanges = $Mode -eq 'full' -or ($Mode -eq 'standard' -and ($name -eq 'crowd' -or $name -eq 'sound'))
  $sweep = -1
  if ($name -eq 'war') { $sweep = $Harness.WarSweep } elseif ($name -eq 'crowd') { $sweep = $Harness.CrowdSweep } elseif ($name -eq 'sound') { $sweep = $Harness.SoundSweep }
  foreach ($recording in $bank.Recordings) {
   if ($recording.Id -ge $Harness.MaxRecording) { continue }
   if (!$bank.Ranged) { $cases.Add((New-Case $name $bank $recording.Id -1 $true)); continue }
   $default = Get-DefaultRange $name $bank $recording.Id $Harness
   foreach ($range in $bank.Ranges) {
    $isDefault = $range -eq $default
    if ($allRanges -or $isDefault -or $recording.Id -eq $sweep) { $cases.Add((New-Case $name $bank $recording.Id $range $isDefault)) }
   }
  }
  foreach ($groupId in $bank.GroupIds) {
   if ($groupId -lt $Harness.GroupFirst -or $groupId -gt $Harness.GroupLast) { continue }
   $range = -1
   if ($bank.Ranged) { $range = Get-DefaultRange $name $bank -1 $Harness }
   $candidates = @($bank.Groups[$groupId])
   $candidateEvents = @($candidates | ForEach-Object { Get-EventName $bank $_ $range })
   $cases.Add([pscustomobject]@{ Key = "$name|$groupId|$range"; Bank = $name; Id = $groupId; Range = $range; Event = ''; Project = $bank.Project; Default = $true; Group = $true; Candidates = $candidates; CandidateEvents = $candidateEvents })
  }
 }
 $cases
}

function Resolve-SampleRef($Ref, [string]$ModuleDir, $Cache) {
 $key = "$($Ref.Guid)|$($Ref.Path)"
 if ($Cache.ContainsKey($key)) { return $Cache[$key] }
 $file = Join-Path $ModuleDir $Ref.Path
 $result = [pscustomobject]@{ Ok = $false; Reason = ''; Seconds = 0.0 }
 if (!(Test-Path -LiteralPath $file -PathType Leaf)) { $result.Reason = "sample-missing:$($Ref.Path)" }
 else {
  $meta = Get-MetaInfo ($file + '.meta')
  if (!$meta) { $result.Reason = "sample-meta-missing:$($Ref.Path)" }
  elseif ($Ref.Guid -and $meta.Guid -ne $Ref.Guid) { $result.Reason = "sample-guid-mismatch:$($Ref.Path)" }
  elseif ($meta.Name -cne $Ref.Path) { $result.Reason = "sample-path-case-mismatch:$($Ref.Path)" }
  else {
   $wav = Get-WavInfo $file
   if (!$wav.Ok) { $result.Reason = "wav-invalid-$($wav.Error):$($Ref.Path)" }
   else { $result.Ok = $true; $result.Seconds = $wav.Seconds }
  }
 }
 $Cache[$key] = $result
 $result
}

function Resolve-SignalRef($Res, [string]$ModuleDir, $Cache) {
 if (!$Res) { return [pscustomobject]@{ Ok = $false; Reason = 'gain-signal-resource-missing' } }
 $key = "$($Res.Guid)|$($Res.Path)"
 if ($Cache.ContainsKey($key)) { return $Cache[$key] }
 $file = Join-Path $ModuleDir $Res.Path
 $result = [pscustomobject]@{ Ok = $false; Reason = '' }
 if (!(Test-Path -LiteralPath $file -PathType Leaf)) { $result.Reason = "signal-missing:$($Res.Path)" }
 else {
  $meta = Get-MetaInfo ($file + '.meta')
  if (!$meta -or ($Res.Guid -and $meta.Guid -ne $Res.Guid) -or $meta.Name -cne $Res.Path) { $result.Reason = "signal-meta-mismatch:$($Res.Path)" }
  elseif ((Get-Content -LiteralPath $file -Raw) -notmatch '(?s)Inputs\s*\{.*?name\s+"EAS_Gain"') { $result.Reason = "signal-input-EAS_Gain-missing:$($Res.Path)" }
  else { $result.Ok = $true }
 }
 $Cache[$key] = $result
 $result
}

function Test-StaticAudio($Model, $Graph, $Harness, [string]$ModuleDir, [string]$SampleDir) {
 $failures = [Collections.Generic.List[string]]::new()
 $warnings = [Collections.Generic.List[string]]::new()
 $rows = [Collections.Generic.List[object]]::new()
 $groupRows = [Collections.Generic.List[object]]::new()
 $wavCache = @{}; $signalCache = @{}; $expectedNames = @{}
 foreach ($problem in $Model.Problems) { $failures.Add("bank source: $problem") }
 $counts = [ordered]@{}
 foreach ($name in $Model.Banks.Keys) {
  $bank = $Model.Banks[$name]
  $project = $null
  if ($Graph.ContainsKey($bank.ProjectGuid)) {
   $project = $Graph[$bank.ProjectGuid]
   if ($project.Path -cne $bank.ProjectPath) { $failures.Add("$($bank.Class).PROJECT path $($bank.ProjectPath) differs from the .acp metadata $($project.Path)") }
  } else { $failures.Add("$($bank.Class).PROJECT $($bank.Project) is not one of the shipped .acp files") }
  $bankEvents = 0
  foreach ($recording in $bank.Recordings) {
   if ($recording.Id -ge $Harness.MaxRecording) { $failures.Add("$($bank.Class) recording $($recording.Id) is beyond the harness scan (MAX_RECORDING $($Harness.MaxRecording))") }
   $ranges = @(-1)
   if ($bank.Ranged) { $ranges = $bank.Ranges }
   foreach ($range in $ranges) {
    $bankEvents++
    $eventName = Get-EventName $bank $recording.Id $range
    $expectedNames["$($bank.ProjectGuid)|$eventName"] = $true
    $reasons = [Collections.Generic.List[string]]::new()
    $notes = [Collections.Generic.List[string]]::new()
    $wavSeconds = 0.0
    $samplesFound = 0
    if (!$project) { $reasons.Add('project-missing') }
    elseif (!$project.Events.ContainsKey($eventName)) { $reasons.Add('event-not-in-acp') }
    else {
     $graphEvent = $project.Events[$eventName]
     if ($project.Duplicates -contains $eventName) { $reasons.Add('duplicate-event-name') }
     if (!$graphEvent.ShaderFound) { $reasons.Add('shader-missing') }
     $nodeObjects = @($graphEvent.Nodes | ForEach-Object { $project.Nodes[$_] })
     $sampleBanks = @($nodeObjects | Where-Object Class -EQ 'BankLocalClass')
     if (!$sampleBanks.Count) { $reasons.Add('sample-bank-missing') }
     $refs = @($sampleBanks | ForEach-Object { $_.SampleRefs })
     if ($sampleBanks.Count -and !$refs.Count) { $reasons.Add('sample-list-empty') }
     foreach ($ref in $refs) {
      $sample = Resolve-SampleRef $ref $SampleDir $wavCache
      if (!$sample.Ok) { $reasons.Add($sample.Reason) } else { $samplesFound++; $wavSeconds = [Math]::Max($wavSeconds, $sample.Seconds) }
     }
     $amplitude = @($nodeObjects | Where-Object Class -EQ 'AmplitudeClass')
     if (!$amplitude.Count) { $notes.Add('no-amplitude-node') }
     elseif ($range -ge 0 -and !@($amplitude | Where-Object { $_.OuterRange -eq $range }).Count) { $reasons.Add('amplitude-range-mismatch:' + (@($amplitude | ForEach-Object { $_.OuterRange }) -join '+')) }
     if (!@($nodeObjects | Where-Object Class -EQ 'SpatialityClass').Count) { $notes.Add('no-spatiality-node') }
     if (!$graphEvent.Signal -or !$project.Nodes.ContainsKey($graphEvent.Signal) -or $project.Nodes[$graphEvent.Signal].Class -ne 'SignalClass') { $reasons.Add('gain-signal-missing') }
     else {
      $signal = Resolve-SignalRef $project.Nodes[$graphEvent.Signal].Res $ModuleDir $signalCache
      if (!$signal.Ok) { $reasons.Add($signal.Reason) }
     }
     if (!$graphEvent.OutState -or !$project.Nodes.ContainsKey($graphEvent.OutState) -or $project.Nodes[$graphEvent.OutState].Class -ne 'MixerClass') { $reasons.Add('mixer-output-missing') }
    }
    # Finite runtimes refuse durations <= 0 or > 3600 s (EAS_RadioRuntime finite-event-invalid).
    if ($recording.Duration -le 0 -or ($name -ne 'war' -and $recording.Duration -gt 3600)) { $reasons.Add("bank-duration-invalid:$($recording.Duration)") }
    $delta = ''
    if ($samplesFound -and $recording.Duration -gt 0) {
     $deltaValue = [Math]::Round($wavSeconds - $recording.Duration, 3)
     $delta = $deltaValue.ToString([Globalization.CultureInfo]::InvariantCulture)
     if ([Math]::Abs($deltaValue) -gt 0.05) { $notes.Add("duration-delta:$delta") }
    }
    $rows.Add([pscustomobject][ordered]@{
     bank = $name; id = $recording.Id; range = $(if ($range -lt 0) { 'none' } else { [string]$range }); event = $eventName
     static = $(if ($reasons.Count) { 'FAIL' } else { 'PASS' }); reasons = ($reasons -join ';'); notes = ($notes -join ';')
     bankSeconds = $recording.Duration; wavSeconds = [Math]::Round($wavSeconds, 3); durationDelta = $delta; samples = $samplesFound
     project = $bank.Project; projectGuid = $bank.ProjectGuid
    })
   }
  }
  foreach ($groupId in $bank.GroupIds) {
   $candidates = @($bank.Groups[$groupId])
   $invalid = @($candidates | Where-Object { !$bank.ById.ContainsKey($_) })
   $ok = $candidates.Count -gt 0 -and !$invalid.Count
   if (!$candidates.Count) { $failures.Add("$($bank.Class) random selection $groupId has no candidates") }
   if ($invalid.Count) { $failures.Add("$($bank.Class) random selection $groupId names unknown recording(s): $($invalid -join ', ')") }
   if ($groupId -lt $Harness.GroupFirst -or $groupId -gt $Harness.GroupLast) { $failures.Add("$($bank.Class) random selection $groupId is outside the harness group scan ($($Harness.GroupFirst)..$($Harness.GroupLast))") }
   $groupRows.Add([pscustomobject][ordered]@{ bank = $name; selection = $groupId; candidates = ($candidates -join '+'); static = $(if ($ok) { 'PASS' } else { 'FAIL' }) })
  }
  $counts[$name] = [ordered]@{ recordings = $bank.Recordings.Count; ranges = $(if ($bank.Ranged) { $bank.Ranges.Count } else { 0 }); events = $bankEvents; randomGroups = $bank.GroupIds.Count; project = $bank.Project }
 }
 $failedRows = @($rows | Where-Object static -EQ 'FAIL')
 if ($failedRows.Count) { $failures.Add("$($failedRows.Count) event(s) failed static resolution") }
 $durationNotes = @($rows | Where-Object { $_.notes -match 'duration-delta' })
 if ($durationNotes.Count) { $warnings.Add("$($durationNotes.Count) event(s) have a bank duration more than 0.05 s from the WAV data length (see static-events.csv durationDelta)") }
 $orphans = [Collections.Generic.List[string]]::new()
 foreach ($project in $Graph.Values) {
  foreach ($eventName in $project.Events.Keys) { if (!$expectedNames.ContainsKey("$($project.Guid)|$eventName")) { $orphans.Add("$($project.File):$eventName") } }
  foreach ($duplicate in $project.Duplicates) { $failures.Add("$($project.File) defines event $duplicate more than once") }
 }
 if ($orphans.Count) { $warnings.Add("$($orphans.Count) graph event(s) cannot be requested by any bank: $(@($orphans | Sort-Object | Select-Object -First 10) -join ', ')") }
 $referenced = @{}
 foreach ($project in $Graph.Values) { foreach ($node in $project.Nodes.Values) { foreach ($ref in $node.SampleRefs) { $referenced[$ref.Path.ToLowerInvariant()] = $true } } }
 $unreferenced = @()
 $samplesRoot = Join-Path $SampleDir 'Audio/EXPBG/AmbientSounds/Samples'
 if (Test-Path -LiteralPath $samplesRoot) {
  $unreferenced = @(Get-ChildItem -LiteralPath $samplesRoot -Recurse -File -Filter '*.wav' | ForEach-Object {
   $relative = $_.FullName.Substring($SampleDir.Length).TrimStart('\', '/').Replace('\', '/')
   if (!$referenced.ContainsKey($relative.ToLowerInvariant())) { $relative }
  })
  if ($unreferenced.Count) { $warnings.Add("$($unreferenced.Count) sample file(s) are not referenced by any audio project and cannot be played by the pack: $($unreferenced -join ', ')") }
 }
 if ($null -ne $Model.Authored.war -and $Model.Authored.war -ne $Harness.WarDefault) { $warnings.Add("harness WAR_DEFAULT_RANGE $($Harness.WarDefault) differs from E_EXPBG_AmbientSounds.et ($($Model.Authored.war))") }
 if ($null -ne $Model.Authored.crowd -and $Model.Authored.crowd -ne $Harness.CrowdDefault) { $warnings.Add("harness CROWD_DEFAULT_RANGE $($Harness.CrowdDefault) differs from E_EXPBG_Crowd.et ($($Model.Authored.crowd))") }
 foreach ($id in $Model.Authored.sound.Keys) {
  $harnessDefault = $Harness.SoundTableFallback
  if ($Harness.SoundDefaults.ContainsKey($id)) { $harnessDefault = $Harness.SoundDefaults[$id] }
  if ($harnessDefault -ne $Model.Authored.sound[$id]) { $warnings.Add("harness SoundDefaultRange($id) = $harnessDefault differs from its prefab ($($Model.Authored.sound[$id]))") }
 }
 [pscustomobject][ordered]@{
  passed = ($failures.Count -eq 0); failures = @($failures); warnings = @($warnings)
  counts = $counts; totalEvents = $rows.Count; failedEvents = $failedRows.Count
  groups = @($groupRows); orphanEvents = @($orphans); unreferencedSamples = $unreferenced
  events = @($rows)
 }
}
#endregion

#region Native log evidence ------------------------------------------------------------------------
function Get-KeyValues([string]$Body) {
 $values = [ordered]@{}
 foreach ($pair in [regex]::Matches($Body, '(?<k>[A-Za-z]+)=(?<v>\S*)')) { $values[$pair.Groups['k'].Value] = $pair.Groups['v'].Value }
 $values
}

function ConvertFrom-AudioLog([string[]]$Lines, [string]$LogPath, $Context, [string]$RequestedMode = '', [string]$RequestedBanks = '', [switch]$CompareRequest) {
 $Model = $Context.Model; $Graph = $Context.Graph
 $harnessPattern = [regex]'\[EXPE2E AUDIO(?: (?<kind>[A-Z]+))?\] (?<body>.*)$'
 $diagPattern = [regex]'\[EAS DIAG\] action=play (?<body>.*)$'
 $enginePattern = [regex]'^(?<time>\d\d:\d\d:\d\d\.\d{3})\s+(?<cat>[A-Z_]+)\s*\((?<lvl>[EWF])\)\s*:\s?(?<msg>.*)$'
 $packPattern = '(?i)Audio/EXPBG/AmbientSounds|EXPBG_AmbientSounds|EXPBG_Radio\.acp|EXPBG_Sounds\.acp|EAS_Gain\.sig|Samples/[A-Za-z/]+/EAS_'
 $audioPathPattern = '(?i)AmbientSounds|EXPBG|\.wav|\.acp|\.sig'
 $inventory = $null; $final = $null; $abort = $null; $ready = $null; $config = $null
 $begins = @{}; $plays = @{}
 $results = [Collections.Generic.List[object]]::new()
 $groupLines = [Collections.Generic.List[object]]::new()
 $projectLines = [Collections.Generic.List[object]]::new()
 $warnings = [Collections.Generic.List[string]]::new()
 $failures = [Collections.Generic.List[string]]::new()
 $packAudioErrors = [Collections.Generic.List[string]]::new()
 $harnessScriptErrors = [Collections.Generic.List[string]]::new()
 $harnessVme = [Collections.Generic.List[string]]::new()
 $easErrors = [Collections.Generic.List[string]]::new()
 $loadedAddons = [Collections.Generic.List[object]]::new()
 $foreignVme = 0; $foreignPlays = 0; $crash = $false; $compileFailed = $false; $inLoaded = $false; $sawLoaded = $false; $gameDestroyed = $false; $audioEndpoint = $false
 for ($i = 0; $i -lt $Lines.Count; $i++) {
  $line = $Lines[$i]
  if ($line -match 'Loaded addons:') { $inLoaded = $true; $sawLoaded = $true; continue }
  if ($inLoaded) {
   $addon = [regex]::Match($line, "gproj: '(?<path>[^']+)' guid: '(?<guid>[0-9A-Fa-f]{16})'")
   if ($addon.Success) { $loadedAddons.Add([pscustomobject]@{ Guid = $addon.Groups['guid'].Value.ToUpperInvariant(); Path = $addon.Groups['path'].Value }); continue }
   $inLoaded = $false
  }
  if ($line -match 'Game destroyed') { $gameDestroyed = $true }
  if ($line -match '\bAUDIO\s+:\s*Endpoint:') { $audioEndpoint = $true }
  if ($line -match "Can't compile|Script compilation failed|Compilation failed") { $compileFailed = $true }
  if ($line -match 'ENGINE\s+\(F\)|\bCrashed\b|Application crashed') { $crash = $true }
  if ($line -match 'Virtual Machine Exception') {
   $vmeContext = ($Lines[$i..([Math]::Min($i + 12, $Lines.Count - 1))]) -join "`n"
   if ($vmeContext -match 'EXPE2E|EAS_') { $harnessVme.Add($vmeContext) } else { $foreignVme++ }
  }
  if ($line -match '\[EAS ERROR\]|\[EAS\] Radio start failed') { $easErrors.Add($line.Trim()) }
  $engineLine = $enginePattern.Match($line)
  if ($engineLine.Success) {
   if ($engineLine.Groups['lvl'].Value -ne 'W' -and $line -match $packPattern) { $packAudioErrors.Add($line.Trim()) }
   if ($engineLine.Groups['cat'].Value -eq 'SCRIPT' -and $engineLine.Groups['lvl'].Value -ne 'W' -and $line -match 'EXPE2E') { $harnessScriptErrors.Add($line.Trim()) }
  }
  $diag = $diagPattern.Match($line)
  if ($diag.Success) {
   $values = Get-KeyValues $diag.Groups['body'].Value
   $seq = ConvertTo-IntOrDefault $values['seq']
   if ([string]$values['runtime'] -eq 'e2e-sweep' -and $seq -gt 0) {
    if ($plays.ContainsKey($seq)) { $failures.Add("duplicate play evidence for seq=$seq") }
    $plays[$seq] = $values
   } else { $foreignPlays++ }
   continue
  }
  $match = $harnessPattern.Match($line)
  if (!$match.Success) { continue }
  $kind = $match.Groups['kind'].Value
  $values = Get-KeyValues $match.Groups['body'].Value
  switch ($kind) {
   'INVENTORY' { $inventory = $values }
   'CONFIG' { $config = $values }
   'READY' { $ready = $values }
   'RESULT' { $final = $values }
   'ABORT' { $abort = $values }
   'GROUP' { $groupLines.Add($values) }
   'PROJECT' { $projectLines.Add($values) }
   'BEGIN' { $seq = ConvertTo-IntOrDefault $values['seq']; if ($seq -gt 0) { $begins[$seq] = $i } }
   '' { $values['line'] = $i; $results.Add($values) }
  }
 }

 $logMode = $RequestedMode
 if ($inventory -and $inventory.Contains('mode')) { $logMode = [string]$inventory['mode'] }
 if (!$logMode) { $logMode = 'standard' }
 $bankText = $RequestedBanks
 if ($inventory -and $inventory.Contains('banks')) { $bankText = [string]$inventory['banks'] }
 $bankList = @(Get-BankList $bankText)
 if ($CompareRequest) {
  if ($RequestedMode -and $logMode -ne $RequestedMode) { $failures.Add("harness ran mode=$logMode but $RequestedMode was requested") }
  if ((@(Get-BankList $RequestedBanks) -join ',') -ne ($bankList -join ',')) { $failures.Add("harness ran banks=$bankText but '$RequestedBanks' was requested") }
 }
 $easDiag = $null -ne $config -and [string]$config['easDiag'] -eq '1'

 # Per-event verdicts. Engine AUDIO/SOUND/RESOURCES lines between BEGIN and result belong to the event.
 $rows = [Collections.Generic.List[object]]::new()
 $seen = @{}
 foreach ($result in $results) {
  $seq = ConvertTo-IntOrDefault $result['seq'] 0
  if ($seen.ContainsKey($seq)) { $failures.Add("duplicate result for seq=$seq") }
  $seen[$seq] = $true
  $eventName = [string]$result['event']
  $verdict = [string]$result['result']
  $reason = [string]$result['reason']
  $messages = [Collections.Generic.List[string]]::new()
  $foreignMessages = [Collections.Generic.List[string]]::new()
  $errorHit = $false
  $start = -1
  if ($begins.ContainsKey($seq)) { $start = $begins[$seq] } else { $failures.Add("result without BEGIN line for seq=$seq") }
  if ($start -ge 0) {
   for ($j = $start + 1; $j -lt $result['line']; $j++) {
    $engineLine = $enginePattern.Match($Lines[$j])
    if (!$engineLine.Success) { continue }
    $category = $engineLine.Groups['cat'].Value
    if (!($category -eq 'AUDIO' -or $category -eq 'SOUND' -or ($category -eq 'RESOURCES' -and $Lines[$j] -match $audioPathPattern))) { continue }
    $text = $Lines[$j].Trim()
    $ours = $Lines[$j] -match $packPattern -or ($eventName -and $Lines[$j].Contains($eventName))
    if ($engineLine.Groups['lvl'].Value -eq 'W') { $messages.Add($text) }
    elseif ($ours) { $messages.Add($text); $errorHit = $true }
    else { $foreignMessages.Add($text) }
   }
  }
  $handleText = [string]$result['handle']
  $invalidText = [string]$result['invalid']
  $handleValue = 0L
  $handleNumeric = [long]::TryParse($handleText, [ref]$handleValue)
  $play = $null
  if ($plays.ContainsKey($seq)) { $play = $plays[$seq] }
  if ($verdict -eq 'PASS') {
   if (!$handleNumeric) { $verdict = 'FAIL'; $reason = 'handle-missing' }
   elseif ($handleValue -eq -1 -or $handleValue -eq 0 -or ($invalidText -and $handleText -eq $invalidText)) { $verdict = 'FAIL'; $reason = 'invalid-handle' }
   elseif ($easDiag -and !$play) { $verdict = 'FAIL'; $reason = 'missing-play-evidence' }
   elseif ($easDiag -and ([string]$play['event'] -cne $eventName -or [string]$play['handle'] -ne $handleText)) { $verdict = 'FAIL'; $reason = 'play-evidence-mismatch' }
   elseif ($errorHit) { $verdict = 'FAIL'; $reason = 'engine-audio-error' }
  }
  $projectGuid = ''
  $projectMatch = [regex]::Match([string]$result['project'], '^\{([0-9A-Fa-f]{16})\}')
  if ($projectMatch.Success) { $projectGuid = $projectMatch.Groups[1].Value.ToUpperInvariant() }
  $inGraph = $false
  if ($projectGuid -and $Graph.ContainsKey($projectGuid)) { $inGraph = $Graph[$projectGuid].Events.ContainsKey($eventName) }
  if (!$inGraph -and $eventName) {
   $failures.Add("event not found in the source audio graph: project=$($result['project']) event=$eventName")
   if ($verdict -eq 'PASS') { $verdict = 'FAIL'; $reason = 'event-not-in-source-graph' }
  }
  $staticRow = $null
  if ($Context.StaticByEvent.ContainsKey("$projectGuid|$eventName")) { $staticRow = $Context.StaticByEvent["$projectGuid|$eventName"] }
  if ($staticRow -and $staticRow.static -ne 'PASS' -and $verdict -eq 'PASS') { $verdict = 'FAIL'; $reason = 'static-resolution-failed' }
  $rangeText = [string]$result['range']
  $rangeValue = -1
  if ($rangeText -ne 'none') { $rangeValue = ConvertTo-IntOrDefault $rangeText -999 }
  $rows.Add([pscustomobject][ordered]@{
   seq = $seq; bank = [string]$result['bank']; id = (ConvertTo-IntOrDefault $result['id'] -1); range = $rangeText; rangeValue = $rangeValue; event = $eventName
   result = $verdict; reason = $reason; harnessResult = [string]$result['result']; harnessReason = [string]$result['reason']
   handle = $handleText; invalidHandle = $invalidText; playEvidence = [bool]$play; resolved = [string]$result['resolved']; group = [string]$result['group']
   default = [string]$result['default']; durationSeconds = [string]$result['duration']; early = [string]$result['early']; late = [string]$result['late']
   audible = [string]$result['audible']; listenerDistance = [string]$result['distance']; project = [string]$result['project']; projectGuid = $projectGuid
   static = $(if ($staticRow) { $staticRow.static } else { '' })
   engineMessages = @($messages); foreignAudioMessages = @($foreignMessages)
  })
 }
 foreach ($row in $rows) { foreach ($message in $row.foreignAudioMessages) { $warnings.Add("seq=$($row.seq) $($row.event): foreign audio error during its window: $message") } }

 # Global gates.
 $total = 0
 if ($inventory -and $inventory.Contains('total')) { $total = ConvertTo-IntOrDefault $inventory['total'] 0 } else { $failures.Add('missing [EXPE2E AUDIO INVENTORY] line (harness did not start)') }
 if (!$final) { $failures.Add('missing [EXPE2E AUDIO RESULT] line') }
 if ($abort) { $failures.Add("harness aborted: reason=$($abort['reason'])") }
 if ($final -and (ConvertTo-IntOrDefault $final['total']) -ne $total) { $failures.Add("result total $($final['total']) differs from inventory total $total") }
 if ($rows.Count -ne $total) { $failures.Add("parsed $($rows.Count) event results; inventory announced $total") }
 if ($total -eq 0 -and $inventory) { $failures.Add('inventory is empty') }
 $failed = @($rows | Where-Object result -NE 'PASS')
 if ($final) {
  $harnessFails = @($rows | Where-Object harnessResult -NE 'PASS').Count
  if ((ConvertTo-IntOrDefault $final['fail']) -ne $harnessFails -or (ConvertTo-IntOrDefault $final['pass']) -ne ($rows.Count - $harnessFails)) { $failures.Add("result line pass/fail ($($final['pass'])/$($final['fail'])) differs from the event lines") }
 }
 if ($failed.Count) { $failures.Add("$($failed.Count) event(s) failed") }
 if ($packAudioErrors.Count) { $failures.Add("$($packAudioErrors.Count) EXPBG audio error line(s) in the engine log") }
 if ($easErrors.Count) { $failures.Add("$($easErrors.Count) [EAS ERROR]/radio start failure line(s) in the log") }
 if ($harnessScriptErrors.Count) { $failures.Add("$($harnessScriptErrors.Count) harness script error line(s)") }
 if ($harnessVme.Count) { $failures.Add("$($harnessVme.Count) VM exception(s) mentioning EXPE2E/EAS") }
 if ($compileFailed) { $failures.Add('script compilation failed') }
 if ($crash) { $failures.Add('engine crash marker in the log') }
 $harnessLoaded = @($loadedAddons | Where-Object Guid -EQ $Context.HarnessGuid).Count -gt 0
 $gmToolsLoaded = @($loadedAddons | Where-Object Guid -EQ $Context.GmToolsGuid).Count -gt 0
 if ($sawLoaded -and !$harnessLoaded) { $failures.Add("harness $($Context.HarnessGuid) is not in the engine's Loaded addons list") }
 if ($sawLoaded -and !$gmToolsLoaded) { $failures.Add("EXPBG GM Tools $($Context.GmToolsGuid) is not in the engine's Loaded addons list") }
 if (!$sawLoaded) { $warnings.Add('no "Loaded addons:" section found in the log; addon load evidence unavailable') }
 if (!$audioEndpoint) { $warnings.Add('no "AUDIO : Endpoint:" line in the log; the client may have had no audio output device') }
 if (!$easDiag) { $warnings.Add('play evidence disabled (CONFIG easDiag=0); [EAS DIAG] action=play lines were not required') }
 if ($foreignPlays) { $warnings.Add("$foreignPlays [EAS DIAG] action=play line(s) from the pack's own runtimes (modules in the world)") }
 if ($foreignVme) { $warnings.Add("$foreignVme VM exception(s) not mentioning EXPE2E/EAS (other mods)") }
 if (!$gameDestroyed) { $warnings.Add('no "Game destroyed" line; the client did not shut down cleanly before the log ended') }
 foreach ($row in $rows) { foreach ($message in $row.engineMessages) { if ($message -match '\(W\)') { $warnings.Add("seq=$($row.seq) $($row.event): $message") } } }
 $far = @($rows | Where-Object {
   $distance = 0.0
   [double]::TryParse($_.listenerDistance, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$distance) -and ($distance -lt 0 -or $distance -gt 5)
  })
 if ($far.Count) { $warnings.Add("$($far.Count) event(s) started more than 5 m from the native listener or without one (see listenerDistance)") }
 if ($ready -and $ready.Contains('listener') -and $ready['listener'] -eq '0') { $warnings.Add('the native listener was not at the camera when the sweep started (READY listener=0; ready timeout reached)') }
 if ($inventory -and !@($projectLines).Count -and $rows.Count) { $warnings.Add('no [EXPE2E AUDIO PROJECT] preload lines') }
 foreach ($project in $projectLines) { if ([string]$project['preload'] -ne '1') { $warnings.Add("AudioSystem.PlayEventInitialize returned false for bank=$($project['bank']) project=$($project['project'])") } }

 # Inventory gate: exactly the cases the bank sources define for this mode and bank selection.
 $expected = @(Get-ExpectedCases $Model $Context.Harness $logMode $bankList)
 $expectedByKey = @{}
 foreach ($case in $expected) { $expectedByKey[$case.Key] = $case }
 $reportedKeys = @{}
 $inventoryProblems = [Collections.Generic.List[string]]::new()
 foreach ($row in $rows) {
  $key = "$($row.bank)|$($row.id)|$($row.rangeValue)"
  if ($reportedKeys.ContainsKey($key)) { $inventoryProblems.Add("duplicate case $key") }
  $reportedKeys[$key] = $true
  if (!$expectedByKey.ContainsKey($key)) {
   $inventoryProblems.Add("unexpected case $key ($($row.event))")
   if ($row.result -eq 'PASS') { $row.result = 'FAIL'; $row.reason = 'unexpected-case' }
   continue
  }
  $case = $expectedByKey[$key]
  $nameOk = $(if ($case.Group) { $case.CandidateEvents -ccontains $row.event } else { $row.event -ceq $case.Event })
  if (!$nameOk) {
   $inventoryProblems.Add("case $key played $($row.event); the bank source gives $(if ($case.Group) { $case.CandidateEvents -join '|' } else { $case.Event })")
   if ($row.result -eq 'PASS') { $row.result = 'FAIL'; $row.reason = 'event-differs-from-bank-source' }
  }
 }
 $missingCases = @($expected | Where-Object { !$reportedKeys.ContainsKey($_.Key) })
 if ($missingCases.Count) { $inventoryProblems.Add("$($missingCases.Count) expected case(s) not reported: $(@($missingCases | Select-Object -First 10 | ForEach-Object { if ($_.Event) { $_.Event } else { $_.Key } }) -join ', ')") }
 if ($inventory) {
  foreach ($name in $Model.Banks.Keys) {
   $announced = ConvertTo-IntOrDefault $inventory[$name] 0
   $wanted = @($expected | Where-Object Bank -EQ $name).Count
   if ($announced -ne $wanted) { $inventoryProblems.Add("inventory $name=$announced; the bank sources give $wanted for mode=$logMode") }
  }
 }
 foreach ($problem in $inventoryProblems) { $failures.Add("inventory: $problem") }
 $failed = @($rows | Where-Object result -NE 'PASS')

 # Random selections: every draw resolved to one of the source candidates.
 $groupSeen = @{}
 foreach ($group in $groupLines) {
  $bankName = [string]$group['bank']
  $selection = ConvertTo-IntOrDefault $group['selection']
  $groupSeen["$bankName|$selection"] = $true
  if (!$Model.Banks.Contains($bankName) -or !$Model.Banks[$bankName].Groups.ContainsKey($selection)) { $failures.Add("group draw for unknown selection bank=$bankName selection=$selection"); continue }
  $candidates = @($Model.Banks[$bankName].Groups[$selection])
  if ((ConvertTo-IntOrDefault $group['invalid'] 1) -ne 0) { $failures.Add("group bank=$bankName selection=${selection}: $($group['invalid']) of $($group['draws']) draws did not resolve to a playable recording") }
  $drawn = @(([string]$group['resolved']).Split('+') | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
  $outside = @($drawn | Where-Object { $candidates -notcontains $_ })
  if ($outside.Count) { $failures.Add("group bank=$bankName selection=$selection drew recording(s) outside its source candidates: $($outside -join ', ')") }
  $never = @($candidates | Where-Object { $drawn -notcontains $_ })
  if ($never.Count) { $warnings.Add("group bank=$bankName selection=$selection never drew candidate(s) $($never -join ', ') in $($group['draws']) draws") }
 }
 if ($inventory) {
  foreach ($case in @($expected | Where-Object Group)) { if (!$groupSeen.ContainsKey("$($case.Bank)|$($case.Id)")) { $failures.Add("no [EXPE2E AUDIO GROUP] draw evidence for bank=$($case.Bank) selection=$($case.Id)") } }
 }

 # Coverage: every sample file, graph node (range branch, filter, bank) and recording, per project.
 $exhaustive = @()
 if ($logMode -eq 'full') { $exhaustive = @($Model.Banks.Values | ForEach-Object { $_.ProjectGuid } | Sort-Object -Unique) }
 elseif ($logMode -eq 'standard') { $exhaustive = @($Model.Banks['crowd'].ProjectGuid, $Model.Banks['sound'].ProjectGuid) }
 $coverage = [ordered]@{}
 $passing = @($rows | Where-Object result -EQ 'PASS')
 foreach ($project in $Graph.Values) {
  $projectPassing = @($passing | Where-Object projectGuid -EQ $project.Guid)
  $coveredNodes = @{}; $coveredSamples = @{}; $coveredBases = @{}; $testedEvents = @{}
  foreach ($row in @($rows | Where-Object projectGuid -EQ $project.Guid)) { $testedEvents[$row.event] = $true }
  foreach ($row in $projectPassing) {
   if (!$project.Events.ContainsKey($row.event)) { continue }
   $graphEvent = $project.Events[$row.event]
   foreach ($node in $graphEvent.Nodes) { $coveredNodes[$node] = $true }
   foreach ($sample in $graphEvent.Samples) { $coveredSamples[$sample] = $true }
   $coveredBases[($row.event -replace '_R\d+$', '')] = $true
  }
  $allNodes = @{}; $allSamples = @{}; $allBases = @{}
  foreach ($graphEvent in $project.Events.Values) {
   foreach ($node in $graphEvent.Nodes) { $allNodes[$node] = $true }
   foreach ($sample in $graphEvent.Samples) { $allSamples[$sample] = $true }
   $allBases[($graphEvent.Name -replace '_R\d+$', '')] = $true
  }
  $missingNodes = @($allNodes.Keys | Where-Object { !$coveredNodes.ContainsKey($_) } | ForEach-Object { "$($project.Nodes[$_].Class):$($project.Nodes[$_].Name)" } | Sort-Object)
  $missingSamples = @($allSamples.Keys | Where-Object { !$coveredSamples.ContainsKey($_) } | Sort-Object)
  $missingBases = @($allBases.Keys | Where-Object { !$coveredBases.ContainsKey($_) } | Sort-Object)
  $untestedEvents = @($project.Events.Keys | Where-Object { !$testedEvents.ContainsKey($_) }).Count
  $coverage[$project.File] = [ordered]@{
   guid = $project.Guid; graphEvents = $project.Events.Count; testedEvents = $testedEvents.Count; passedEvents = $projectPassing.Count; untestedEvents = $untestedEvents
   samples = $allSamples.Count; samplesPlayed = $coveredSamples.Count; samplesMissing = $missingSamples
   graphNodes = $allNodes.Count; graphNodesPlayed = $coveredNodes.Count; graphNodesMissing = $missingNodes
   recordings = $allBases.Count; recordingsPlayed = $coveredBases.Count; recordingsMissing = $missingBases
  }
  if (!$bankList.Count -and $inventory) {
   if ($missingSamples.Count) { $failures.Add("$($project.File): $($missingSamples.Count) sample file(s) not played by a passing event") }
   if ($missingNodes.Count) { $failures.Add("$($project.File): $($missingNodes.Count) graph node(s) not played by a passing event") }
   if ($missingBases.Count) { $failures.Add("$($project.File): $($missingBases.Count) recording(s) not played by a passing event") }
   if ($exhaustive -contains $project.Guid -and $untestedEvents) { $failures.Add("$($project.File): mode $logMode left $untestedEvents graph event(s) untested") }
  }
 }

 $byBank = [ordered]@{}
 foreach ($group in $rows | Group-Object bank) {
  $byBank[$group.Name] = [ordered]@{ total = $group.Count; pass = @($group.Group | Where-Object result -EQ 'PASS').Count; fail = @($group.Group | Where-Object result -NE 'PASS').Count }
 }
 [pscustomobject][ordered]@{
  passed = ($failures.Count -eq 0)
  failures = @($failures)
  warnings = @($warnings)
  log = $LogPath
  mode = $logMode; banks = $(if ($bankList.Count) { $bankList -join ',' } else { 'all' })
  inventory = $inventory; config = $config; ready = $ready; resultLine = $final; abort = $abort
  totals = [ordered]@{ expected = $expected.Count; events = $rows.Count; pass = $passing.Count; fail = $failed.Count }
  byBank = $byBank
  failedEvents = @($failed | Select-Object seq, bank, id, range, event, result, reason, handle, harnessReason)
  groups = @($groupLines); projects = @($projectLines)
  coverage = $coverage
  packAudioErrors = @($packAudioErrors); easErrors = @($easErrors)
  harnessScriptErrors = @($harnessScriptErrors); harnessVmExceptions = @($harnessVme)
  loadedAddons = @($loadedAddons | Where-Object { $_.Guid -eq $Context.HarnessGuid -or $_.Guid -eq $Context.GmToolsGuid })
  loadedAddonCount = $loadedAddons.Count; harnessLoaded = $harnessLoaded; gmToolsLoaded = $gmToolsLoaded
  audioEndpoint = $audioEndpoint; gameDestroyed = $gameDestroyed
  events = @($rows)
 }
}

function Get-RecordingRollup($Static, $Rows, $Model, [bool]$NativeRan) {
 # One row per recording and per random selection: static events and native plays.
 $map = [ordered]@{}
 foreach ($staticEvent in $Static.events) {
  $key = "$($staticEvent.bank)|$($staticEvent.id)"
  if (!$map.Contains($key)) {
   $map[$key] = [pscustomobject][ordered]@{ bank = $staticEvent.bank; id = $staticEvent.id; recording = $Model.Banks[$staticEvent.bank].ById[$staticEvent.id].Base; staticEvents = 0; staticFailed = 0; played = 0; passed = 0; failed = 0; result = ''; reasons = '' }
  }
  $map[$key].staticEvents++
  if ($staticEvent.static -ne 'PASS') { $map[$key].staticFailed++; $map[$key].reasons = (@($map[$key].reasons, "$($staticEvent.event):$($staticEvent.reasons)") | Where-Object { $_ }) -join ' ' }
 }
 foreach ($group in $Static.groups) {
  $map["$($group.bank)|$($group.selection)"] = [pscustomobject][ordered]@{ bank = $group.bank; id = $group.selection; recording = "random:$($group.candidates)"; staticEvents = 0; staticFailed = $(if ($group.static -eq 'PASS') { 0 } else { 1 }); played = 0; passed = 0; failed = 0; result = ''; reasons = '' }
 }
 foreach ($row in @($Rows)) {
  $key = "$($row.bank)|$($row.id)"
  if (!$map.Contains($key)) { $map[$key] = [pscustomobject][ordered]@{ bank = $row.bank; id = $row.id; recording = '?'; staticEvents = 0; staticFailed = 0; played = 0; passed = 0; failed = 0; result = ''; reasons = '' } }
  $entry = $map[$key]
  $entry.played++
  if ($row.result -eq 'PASS') { $entry.passed++ } else { $entry.failed++; $entry.reasons = (@($entry.reasons, "$($row.event):$($row.reason)") | Where-Object { $_ }) -join ' ' }
 }
 foreach ($entry in $map.Values) {
  if ($entry.staticFailed -or $entry.failed) { $entry.result = 'FAIL' }
  elseif ($entry.played) { $entry.result = 'PASS' }
  elseif ($NativeRan) { $entry.result = 'NOT-PLAYED' }
  else { $entry.result = 'STATIC-PASS' }
 }
 @($map.Values)
}

function Write-RecordingTable($Recordings) {
 foreach ($entry in $Recordings) {
  Write-Host ('  {0,-11} {1,-5} {2,3}  {3,-36} static {4,3}/{5,-3} native {6,3}/{7,-3}{8}' -f $entry.result, $entry.bank, $entry.id, $entry.recording, ($entry.staticEvents - $entry.staticFailed), $entry.staticEvents, $entry.passed, $entry.played, $(if ($entry.reasons) { '  ' + $entry.reasons } else { '' }))
 }
}

function Write-StaticReport($Static) {
 Write-Host ''
 Write-Host ('Static resolution: {0} events, {1} failed' -f $Static.totalEvents, $Static.failedEvents)
 foreach ($name in $Static.counts.Keys) {
  $count = $Static.counts[$name]
  Write-Host ('  {0,-6} recordings={1,-3} ranges={2,-3} events={3,-5} randomGroups={4} project={5}' -f $name, $count.recordings, $count.ranges, $count.events, $count.randomGroups, $count.project)
 }
 foreach ($staticEvent in @($Static.events | Where-Object static -NE 'PASS' | Select-Object -First 40)) { Write-Host ('  STATIC FAIL {0} {1} {2}: {3}' -f $staticEvent.bank, $staticEvent.id, $staticEvent.event, $staticEvent.reasons) }
 foreach ($failure in $Static.failures) { Write-Host "  static gate: $failure" }
 foreach ($warning in $Static.warnings) { Write-Host "  static warning: $warning" }
}

function Write-SummaryReport($Summary) {
 Write-Host ''
 Write-Host ('Native audio sweep (mode {0}, banks {1}): {2} events, {3} PASS, {4} FAIL; {5} expected' -f $Summary.mode, $Summary.banks, $Summary.totals.events, $Summary.totals.pass, $Summary.totals.fail, $Summary.totals.expected)
 foreach ($bank in $Summary.byBank.Keys) { Write-Host ('  {0,-6} total={1} pass={2} fail={3}' -f $bank, $Summary.byBank[$bank].total, $Summary.byBank[$bank].pass, $Summary.byBank[$bank].fail) }
 foreach ($project in $Summary.coverage.Keys) {
  $c = $Summary.coverage[$project]
  Write-Host ('  {0}: samples {1}/{2}, graph nodes {3}/{4}, recordings {5}/{6}, events tested {7}/{8}' -f $project, $c.samplesPlayed, $c.samples, $c.graphNodesPlayed, $c.graphNodes, $c.recordingsPlayed, $c.recordings, $c.testedEvents, $c.graphEvents)
 }
 foreach ($row in @($Summary.failedEvents | Select-Object -First 40)) { Write-Host ('  FAIL seq={0} bank={1} id={2} event={3} handle={4} reason={5}' -f $row.seq, $row.bank, $row.id, $row.event, $row.handle, $row.reason) }
 foreach ($failure in $Summary.failures) { Write-Host "  gate: $failure" }
 foreach ($warning in @($Summary.warnings | Select-Object -First 25)) { Write-Host "  warning: $warning" }
}

function Get-EventCsvRows($Rows) {
 @($Rows | Select-Object seq, bank, id, range, event, result, reason, handle, playEvidence, static, harnessResult, harnessReason, durationSeconds, early, late, audible, listenerDistance, resolved, project)
}

function Write-NativeOutputs([string]$Directory, [string]$Suffix, $Summary, $Context) {
 Write-JsonFile (Join-Path $Directory "summary$Suffix.json") $Summary
 Write-CsvFile (Join-Path $Directory "events$Suffix.csv") (Get-EventCsvRows $Summary.events)
 $recordings = Get-RecordingRollup $Context.Static $Summary.events $Context.Model $true
 Write-CsvFile (Join-Path $Directory "recordings$Suffix.csv") $recordings
 $recordings
}

function Write-StaticOutputs([string]$Directory, $Context) {
 $static = $Context.Static
 Write-JsonFile (Join-Path $Directory 'static-summary.json') ([ordered]@{ passed = $static.passed; failures = $static.failures; warnings = $static.warnings; counts = $static.counts; totalEvents = $static.totalEvents; failedEvents = $static.failedEvents; groups = $static.groups; orphanEvents = $static.orphanEvents; unreferencedSamples = $static.unreferencedSamples }) 6
 Write-CsvFile (Join-Path $Directory 'static-events.csv') $static.events
}
#endregion

#region Self-test ----------------------------------------------------------------------------------
function New-SyntheticLog($Context, [string]$Mode, [bool]$EasDiag = $true) {
 # A complete, passing console.log as the harness would write it.
 $cases = @(Get-ExpectedCases $Context.Model $Context.Harness $Mode @())
 $lines = [Collections.Generic.List[string]]::new()
 $prefix = '00:00:01.000 SCRIPT       : '
 $lines.Add('00:00:00.100  ENGINE       : Loaded addons:')
 $lines.Add("00:00:00.100   ENGINE       : gproj: './addons/data/ArmaReforger.gproj' guid: '58D0FB3206B6F859'")
 $lines.Add("00:00:00.100   ENGINE       : gproj: 'X:/addons/EXPBG_GM_Tools/EXPBG_GM_Tools.gproj' guid: '$($Context.GmToolsGuid)'")
 $lines.Add("00:00:00.100   ENGINE       : gproj: 'X:/run/addons/EXPE2E_AudioSweep/EXPE2E_AudioSweep.gproj' guid: '$($Context.HarnessGuid)'")
 $lines.Add('00:00:00.200 AUDIO        : Endpoint: Synthetic speakers')
 $perBank = @{}
 foreach ($name in $Context.Model.Banks.Keys) { $perBank[$name] = @($cases | Where-Object Bank -EQ $name).Count }
 $lines.Add("$prefix[EXPE2E AUDIO INVENTORY] mode=$Mode total=$($cases.Count) war=$($perBank['war']) radio=$($perBank['radio']) crowd=$($perBank['crowd']) tv=$($perBank['tv']) sound=$($perBank['sound']) banks=all")
 $lines.Add("$prefix[EXPE2E AUDIO CONFIG] mode=$Mode delayMs=20000 holdMs=1500 probeMs=250 gapMs=300 gain=0.5 keepOpen=0 easDiag=$([int]$EasDiag)")
 foreach ($case in @($cases | Where-Object Group)) { $lines.Add("$prefix[EXPE2E AUDIO GROUP] bank=$($case.Bank) selection=$($case.Id) draws=64 invalid=0 resolved=$($case.Candidates -join '+')") }
 $lines.Add("$prefix[EXPE2E AUDIO READY] camera=1 listener=1 waitedMs=1200 position=<0,0,0> listenerDistance=0 settleMs=20000")
 foreach ($name in $Context.Model.Banks.Keys) { $lines.Add("$prefix[EXPE2E AUDIO PROJECT] bank=$name project=$($Context.Model.Banks[$name].Project) preload=1") }
 $lines.Add("$prefix[EXPE2E AUDIO START] total=$($cases.Count) mode=$Mode")
 $seq = 0
 foreach ($case in $cases) {
  $seq++
  $eventName = $case.Event
  $resolved = $case.Id
  $duration = 0.0
  if ($case.Group) { $resolved = $case.Candidates[0]; $eventName = $case.CandidateEvents[0] }
  $duration = $Context.Model.Banks[$case.Bank].ById[$resolved].Duration
  $range = $(if ($case.Range -lt 0) { 'none' } else { [string]$case.Range })
  $handle = 1000 + $seq
  $lines.Add("$prefix[EXPE2E AUDIO BEGIN] seq=$seq bank=$($case.Bank) id=$($case.Id) event=$eventName range=$range resolved=$resolved")
  if ($EasDiag) { $lines.Add("$prefix[EAS DIAG] action=play owner=world console=0 authority=1 runtime=e2e-sweep seq=$seq bank=$($case.Bank) selection=$($case.Id) resolved=$resolved event=$eventName handle=$handle range=$range duration=$duration gain=0.5") }
  $lines.Add("$prefix[EXPE2E AUDIO] bank=$($case.Bank) id=$($case.Id) event=$eventName range=$range result=PASS reason=playing seq=$seq default=$([int]$case.Default) duration=$duration early=playing late=playing audible=0.000000 distance=0.000000 holdMs=1500 handle=$handle invalid=-1 resolved=$resolved group=$([int]$case.Group) project=$($case.Project)")
 }
 $lines.Add("$prefix[EXPE2E AUDIO RESULT] total=$($cases.Count) pass=$($cases.Count) fail=0 mode=$Mode")
 $lines.Add("$prefix[EXPE2E AUDIO CLOSE] requested=1")
 $lines.Add('00:00:09.000 ENGINE       : Game destroyed.')
 , $lines.ToArray()
}

function Edit-SyntheticLine([string[]]$Lines, [string]$Pattern, [scriptblock]$Change) {
 # Applies $Change to the first line matching $Pattern; $Change returns the replacement or $null (delete).
 $done = $false
 $output = [Collections.Generic.List[string]]::new()
 foreach ($line in $Lines) {
  if (!$done -and $line -match $Pattern) {
   $done = $true
   $replacement = & $Change $line
   if ($null -ne $replacement) { foreach ($item in @($replacement)) { $output.Add($item) } }
   continue
  }
  $output.Add($line)
 }
 if (!$done) { throw "Self-test pattern not found: $Pattern" }
 , $output.ToArray()
}

function Invoke-SelfTest($Context) {
 # Parser logic only: the static verdicts are replaced by all-PASS so a repo problem cannot mask it.
 $clean = [pscustomobject]@{ Model = $Context.Model; Graph = $Context.Graph; Harness = $Context.Harness; HarnessGuid = $Context.HarnessGuid; GmToolsGuid = $Context.GmToolsGuid; StaticByEvent = @{}; Static = $null }
 $quick = New-SyntheticLog $clean 'quick'
 $war = @(Get-ExpectedCases $Context.Model $Context.Harness 'quick' @() | Where-Object { $_.Bank -eq 'war' -and $_.Default })[0].Event
 $staticFail = [pscustomobject]@{ Model = $clean.Model; Graph = $clean.Graph; Harness = $clean.Harness; HarnessGuid = $clean.HarnessGuid; GmToolsGuid = $clean.GmToolsGuid; Static = $null; StaticByEvent = @{ "$($Context.Model.Banks['war'].ProjectGuid)|$war" = [pscustomobject]@{ static = 'FAIL' } } }
 $tests = @(
  @{ Name = 'complete quick log passes'; Expect = $true; Lines = $quick }
  @{ Name = 'complete standard log passes'; Expect = $true; Lines = (New-SyntheticLog $clean 'standard') }
  @{ Name = 'complete full log passes'; Expect = $true; Lines = (New-SyntheticLog $clean 'full') }
  @{ Name = 'play evidence disabled still passes (warning only)'; Expect = $true; Lines = (New-SyntheticLog $clean 'quick' $false) }
  @{ Name = 'foreign base-game audio error inside a window is tolerated'; Expect = $true; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO BEGIN\] seq=9 ' { param($l) @($l, '00:00:02.000      AUDIO     (E): Sounds/Structures/Infrastructure/Power/X.acp does not have a trigger') }) }
  @{ Name = 'handle -1 (invalid) is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO\] .* seq=3 ' { param($l) ($l -replace 'result=PASS reason=playing', 'result=FAIL reason=invalid-handle') -replace 'handle=\d+', 'handle=-1' }) }
  @{ Name = 'handle 0 reported as PASS is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO\] .* seq=4 ' { param($l) $l -replace 'handle=\d+', 'handle=0' }) }
  @{ Name = 'missing handle is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO\] .* seq=5 ' { param($l) $l -replace ' handle=\d+', '' }) }
  @{ Name = 'missing result line is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO\] .* seq=6 ' { param($l) $null }) }
  @{ Name = 'missing [EAS DIAG] play line is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EAS DIAG\] action=play .* seq=7 ' { param($l) $null }) }
  @{ Name = 'play line with another handle is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EAS DIAG\] action=play .* seq=8 ' { param($l) $l -replace 'handle=\d+', 'handle=77' }) }
  @{ Name = 'pack audio error inside a window is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO BEGIN\] seq=10 ' { param($l) @($l, '00:00:02.000      AUDIO     (E): Cannot load Audio/EXPBG/AmbientSounds/Samples/Crowd/EAS_Crowd_Angry.wav') }) }
  @{ Name = 'ended-early harness failure is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO\] .* seq=11 ' { param($l) $l -replace 'result=PASS reason=playing', 'result=FAIL reason=ended-early' }) }
  @{ Name = 'event missing from the audio graph is rejected'; Expect = $false; Lines = (Edit-SyntheticLine (Edit-SyntheticLine $quick '\[EXPE2E AUDIO\] .* seq=12 ' { param($l) $l -replace 'event=\S+', 'event=SOUND_EAS_DOES_NOT_EXIST' }) '\[EAS DIAG\] action=play .* seq=12 ' { param($l) $l -replace 'event=\S+', 'event=SOUND_EAS_DOES_NOT_EXIST' }) }
  @{ Name = 'event name not matching the bank source for its range is rejected'; Expect = $false; Lines = (Edit-SyntheticLine (Edit-SyntheticLine $quick ('\[EXPE2E AUDIO\] .*event=' + [regex]::Escape($war) + ' ') { param($l) $l -replace '_R(\d+) ', '_R100 ' }) ('\[EAS DIAG\] action=play .*event=' + [regex]::Escape($war) + ' ') { param($l) $l -replace '_R(\d+) ', '_R100 ' }) }
  @{ Name = 'missing RESULT line is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO RESULT\]' { param($l) $null }) }
  @{ Name = 'invalid random-group draw is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO GROUP\]' { param($l) $l -replace 'invalid=0', 'invalid=2' }) }
  @{ Name = 'random group drawing a foreign recording is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO GROUP\]' { param($l) $l -replace 'resolved=\S+', 'resolved=0+99' }) }
  @{ Name = 'harness VM exception is rejected'; Expect = $false; Lines = (Edit-SyntheticLine $quick '\[EXPE2E AUDIO START\]' { param($l) @($l, '00:00:02.000 SCRIPT    (E): Virtual Machine Exception', 'Function: EXPE2E_AudioSweep.TickNext') }) }
  @{ Name = 'mode mismatch with the request is rejected'; Expect = $false; Lines = $quick; Mode = 'full' }
  @{ Name = 'statically broken event is rejected even if it played'; Expect = $false; Lines = $quick; Context = $staticFail }
 )
 $failedTests = 0
 foreach ($test in $tests) {
  $testContext = $clean
  if ($test.ContainsKey('Context')) { $testContext = $test.Context }
  $testMode = 'quick'
  if ($test.ContainsKey('Mode')) { $testMode = $test.Mode }
  elseif ($test.Name -match 'standard') { $testMode = 'standard' }
  elseif ($test.Name -match 'full log') { $testMode = 'full' }
  $summary = ConvertFrom-AudioLog $test.Lines 'synthetic' $testContext $testMode '' -CompareRequest
  $ok = $summary.passed -eq $test.Expect
  if (!$ok) { $failedTests++ }
  $detail = $(if ($summary.failures.Count) { $summary.failures[0] } else { 'no gate failures' })
  Write-Host ('  {0}  {1} (parser passed={2}; {3})' -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $test.Name, $summary.passed, $detail)
 }
 Write-Host ("Self-test: {0}/{1} cases behaved as expected." -f ($tests.Count - $failedTests), $tests.Count)
 return ($failedTests -eq 0)
}
#endregion

#region Main ---------------------------------------------------------------------------------------
$harnessProject = Read-Gproj (Join-Path $harnessSource "$harnessName.gproj")
if (!$harnessProject) { throw 'Harness project GUID not found.' }
$audioRoot = Join-Path $SourceDir 'Audio/EXPBG/AmbientSounds'
$graph = Get-AudioGraph $audioRoot
$model = Get-BankModel $SourceDir
$harness = Get-HarnessSettings $harnessScript
$static = Test-StaticAudio $model $graph $harness $SourceDir $SampleDir
$staticByEvent = @{}
foreach ($staticEvent in $static.events) { $staticByEvent["$($staticEvent.projectGuid)|$($staticEvent.event)"] = $staticEvent }
$context = [pscustomobject]@{ Model = $model; Graph = $graph; Harness = $harness; Static = $static; StaticByEvent = $staticByEvent; HarnessGuid = $harnessProject.Guid; GmToolsGuid = $gmToolsGuid }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')

if ($SelfTest) {
 Write-Host 'Audio sweep parser self-test (synthetic logs; no engine, no files):'
 if (Invoke-SelfTest $context) { Write-Host 'PASS: self-test.'; exit 0 }
 Write-Host 'FAIL: self-test.'
 exit 1
}

if ($StaticOnly) {
 $run = Join-Path $repo "build/audio-sweep-static-$stamp"
 New-Item -ItemType Directory -Path $run | Out-Null
 Write-StaticOutputs $run $context
 $recordings = Get-RecordingRollup $static @() $model $false
 Write-CsvFile (Join-Path $run 'recordings.csv') $recordings
 Write-StaticReport $static
 Write-Host ''
 Write-Host 'Per recording (static only):'
 Write-RecordingTable $recordings
 Write-Host "Evidence: $run"
 if ($static.passed) { Write-Host "PASS: all $($static.totalEvents) event names, signals, sample files and WAV headers resolve. No engine was launched; native playback is unproven."; exit 0 }
 Write-Host 'FAIL: static audio resolution gates not met (see static-summary.json / static-events.csv).'
 exit 1
}

if ($ParseLog) {
 $logFile = (Resolve-Path -LiteralPath $ParseLog).Path
 $outDir = Split-Path -Parent $logFile
 if ((Split-Path -Leaf $outDir) -eq 'logs') { $outDir = Split-Path -Parent $outDir }
 $summary = ConvertFrom-AudioLog ((Read-SharedText $logFile) -split "\r?\n") $logFile $context
 $suffix = "-reparsed-$stamp"
 $recordings = Write-NativeOutputs $outDir $suffix $summary $context
 Write-StaticReport $static
 Write-SummaryReport $summary
 Write-Host ''
 Write-Host 'Per recording:'
 Write-RecordingTable $recordings
 Write-Host "Summary: $(Join-Path $outDir "summary$suffix.json")"
 if ($summary.passed -and $static.passed) { Write-Host 'PASS (re-parsed log)'; exit 0 }
 Write-Host 'FAIL (re-parsed log)'
 exit 1
}

# ---- Preflight ------------------------------------------------------------------------------------
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'The diagnostic game client runs on Windows only.' }
if (!$DryRun -and !$OrchestratorSlotGranted) { throw 'Explicit orchestrator native-slot handoff required (-OrchestratorSlotGranted). This launches the diagnostic game client. Use -StaticOnly, -SelfTest or -DryRun for portable checks.' }
if (!$TimeoutSeconds) { $TimeoutSeconds = $(switch ($Mode) { 'full' { 9000 } 'standard' { 3600 } default { 2400 } }) }
if ($TimeoutSeconds -lt 300 -or $TimeoutSeconds -gt 14400) { throw 'TimeoutSeconds must be 300..14400.' }
$config = & (Join-Path $repo 'tools/Get-LocalConfig.ps1')
if (!$GameDir) { $GameDir = $config.GameRoot }
if (!$GameDir) { throw 'Game directory unknown; pass -GameDir or set GameRoot in .local/config.json.' }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$engine = Join-Path $GameDir $diagExeName
if (!(Test-Path -LiteralPath $engine -PathType Leaf)) { throw "Diagnostic client not found: $engine" }
$busy = Get-NativeProcesses
if (!$DryRun) { Assert-NativeSlot }

$run = Join-Path $repo "build/audio-sweep-$stamp"
$logs = Join-Path $run 'logs'
$profileDir = Join-Path $run 'profile'
$temp = Join-Path $run 'temp'
$stageParent = Join-Path $run 'addons'
New-Item -ItemType Directory -Path $logs, $profileDir, $temp, $stageParent | Out-Null
Write-StaticOutputs $run $context
Write-StaticReport $static
if (!$static.passed -and !$SkipStaticGate) { throw "Static audio resolution failed; no native process was started. Fix the pack or pass -SkipStaticGate. Evidence: $run" }

$warnings = [Collections.Generic.List[string]]::new()
$roots = [Collections.Generic.List[string]]::new()
$mods = [Collections.Generic.List[object]]::new()
if ($GmToolsDir) {
 $gmTools = Get-AddonInfo ((Resolve-Path -LiteralPath $GmToolsDir).Path)
 if (!$gmTools -or $gmTools.Guid -ne $gmToolsGuid) { throw "GmToolsDir must contain the EXPBG GM Tools project ($gmToolsGuid)." }
 $roots.Add((Split-Path -Parent $gmTools.Dir))
} else {
 if (!$AddonsDir) { $AddonsDir = $config.InstalledAddonsRoot }
 if (!$AddonsDir) { throw 'Installed addons directory unknown; pass -AddonsDir or -GmToolsDir.' }
 $AddonsDir = (Resolve-Path -LiteralPath $AddonsDir).Path
 $installed = Get-AddonIndex @($AddonsDir)
 if (!$installed.ContainsKey($gmToolsGuid)) { throw "EXPBG GM Tools ($gmToolsGuid) is not installed in $AddonsDir; subscribe to it or pass -GmToolsDir." }
 $gmTools = $installed[$gmToolsGuid]
 $roots.Add($AddonsDir)
}
$mods.Add([pscustomobject]@{ Id = $gmToolsGuid; Name = 'EXPBG GM Tools'; Version = '' })
if ($ModsetPath) {
 $ModsetPath = (Resolve-Path -LiteralPath $ModsetPath).Path
 if (!$AddonsDir) { $AddonsDir = $config.InstalledAddonsRoot }
 $AddonsDir = (Resolve-Path -LiteralPath $AddonsDir).Path
 if ($roots -notcontains $AddonsDir) { $roots.Add($AddonsDir) }
 $seenMods = @{ $gmToolsGuid = $true }
 foreach ($entry in @(Get-Content -LiteralPath $ModsetPath -Raw | ConvertFrom-Json)) {
  if (!$entry.PSObject.Properties['modId']) { throw 'Every modset entry needs a modId.' }
  $id = ([string]$entry.modId).ToUpperInvariant()
  if ($id -notmatch '^[0-9A-F]{16}$') { throw "Invalid modId in modset: '$($entry.modId)'" }
  if ($seenMods.ContainsKey($id)) { continue }
  $seenMods[$id] = $true
  $mods.Add([pscustomobject]@{ Id = $id; Name = $(if ($entry.PSObject.Properties['name']) { [string]$entry.name } else { '' }); Version = $(if ($entry.PSObject.Properties['version']) { [string]$entry.version } else { '' }) })
 }
}
$roots.Add($stageParent)

# Every addon GUID must resolve to exactly one directory across the -addonsDir roots.
$index = Get-AddonIndex @($roots | Where-Object { $_ -ne $stageParent })
if (!$index.ContainsKey($gmToolsGuid) -or $index[$gmToolsGuid].Dir -ne $gmTools.Dir -or $index[$gmToolsGuid].Duplicates.Count) {
 throw "EXPBG GM Tools is ambiguous across the addon roots ($($roots -join '; ')): $($gmTools.Dir) $(if ($index.ContainsKey($gmToolsGuid)) { $index[$gmToolsGuid].Duplicates -join ' ' })"
}
if ($index.ContainsKey($harnessProject.Guid)) { throw "The harness GUID $($harnessProject.Guid) already exists in $($index[$harnessProject.Guid].Dir); remove that copy first." }
$missing = @(); $versionMismatches = @()
foreach ($mod in $mods) {
 if (!$index.ContainsKey($mod.Id)) { $missing += "$($mod.Id) $($mod.Name)"; continue }
 $found = $index[$mod.Id]
 if ($found.Duplicates.Count) { $warnings.Add("$($mod.Id) $($mod.Name) is present more than once: $($found.Dir); $($found.Duplicates -join '; ')") }
 if ($found.Corrupted) { $warnings.Add("$($mod.Id) $($mod.Name) is marked corrupted in ServerData.json") }
 if ($mod.Version -and $found.Version -and $mod.Version -ne $found.Version) { $versionMismatches += "$($mod.Id) $($mod.Name): modset $($mod.Version), installed $($found.Version)" }
}
if ($missing.Count) { throw "Addons missing from $($roots -join '; '): $($missing -join '; ')" }
$closure = @{}
$queue = [Collections.Generic.Queue[string]]::new()
foreach ($mod in $mods) { $queue.Enqueue($mod.Id) }
$missingDependencies = @()
while ($queue.Count) {
 $guid = $queue.Dequeue()
 if ($closure.ContainsKey($guid) -or $builtinGuids -contains $guid) { continue }
 $closure[$guid] = $true
 if (!$index.ContainsKey($guid)) { $missingDependencies += $guid; continue }
 foreach ($dependency in $index[$guid].Dependencies) { $queue.Enqueue($dependency) }
}
foreach ($dependency in $harnessProject.Dependencies) { if ($builtinGuids -notcontains $dependency -and !$index.ContainsKey($dependency)) { $missingDependencies += $dependency } }
if ($missingDependencies.Count) { throw "Dependencies missing from the addon roots: $(@($missingDependencies | Sort-Object -Unique) -join ', ')" }
if ($versionMismatches.Count) {
 if ($StrictVersions) { throw "Installed versions differ from the modset: $($versionMismatches -join '; ')" }
 foreach ($mismatch in $versionMismatches) { $warnings.Add("version pin differs: $mismatch") }
}
$repoVersion = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if ($gmTools.Version -and $gmTools.Version -ne $repoVersion) { $warnings.Add("EXPBG GM Tools under test is $($gmTools.Version) but this checkout is $repoVersion; event names are checked against this checkout") }
$audioParity = 'packed (not comparable offline)'
if (!$gmTools.Packed) {
 if (!$gmTools.Indexed) { $warnings.Add("$($gmTools.Dir) is a loose addon without resourceDatabase.rdb; GUID resource lookups may fail (build it with ./build.ps1 first)") }
 $different = @(foreach ($file in @(Get-ChildItem -LiteralPath $audioRoot -File | Where-Object { $_.Extension -in '.acp', '.sig' })) {
  $other = Join-Path $gmTools.Dir ('Audio/EXPBG/AmbientSounds/' + $file.Name)
  if (!(Test-Path -LiteralPath $other -PathType Leaf) -or (Get-FileHash -LiteralPath $other).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { $file.Name }
 })
 $audioParity = $(if ($different.Count) { 'differs: ' + ($different -join ', ') } else { 'identical' })
 if ($different.Count) { $warnings.Add("audio project data under test differs from this checkout: $($different -join ', ')") }
}

# ---- Stage the harness (test-only addon, private to this run) ----------------------------------
Copy-Item -LiteralPath $harnessSource -Destination (Join-Path $stageParent $harnessName) -Recurse

# ---- Command line ---------------------------------------------------------------------------------
$addonIds = @($mods | ForEach-Object Id) + @($harnessProject.Guid)
$arguments = @(
 '-disableCrashReporter', '-noThrow', '-noSplash', '-window', '-screenWidth', '1280', '-screenHeight', '720', '-forceUpdate', '-noFocus', '-maxFPS', '60',
 '-backendLocalStorage', '-logsDir', $logs, '-addonTempDir', $temp,
 '-addonsDir', ($roots -join ','), '-addons', ($addonIds -join ',')
)
if (!$UseDefaultProfile) { $arguments += @('-profile', $profileDir) }
if ($LaunchMode -eq 'world') { $arguments += @('-world', $World) } else { $arguments += @('-server', $World) }
if ($WorldSystemsConfig) { $arguments += @('-worldSystemsConfig', $WorldSystemsConfig) }
$arguments += @('-easDiagnostics', '1', '-expe2eAudioSweep', '1', '-expe2eAudioMode', $Mode, '-expe2eAudioDelay', [string]$SettleSeconds, '-expe2eAudioHold', [string]$HoldMilliseconds)
if ($Banks) { $arguments += @('-expe2eAudioBanks', $Banks) }
$expectedCases = @(Get-ExpectedCases $model $harness $Mode @(Get-BankList $Banks))

$commit = $null
try { $commit = (& git -C $repo rev-parse HEAD 2>$null) } catch { $commit = $null }
$inputs = [ordered]@{
 repoCommit = $commit; repoVersion = $repoVersion; mode = $Mode; banks = $(if ($Banks) { $Banks } else { 'all' }); expectedCases = $expectedCases.Count
 gmTools = [ordered]@{ dir = $gmTools.Dir; version = $gmTools.Version; packed = $gmTools.Packed; indexed = $gmTools.Indexed; audioParity = $audioParity }
 modset = $(if ($ModsetPath) { [ordered]@{ path = $ModsetPath; sha256 = (Get-FileHash -LiteralPath $ModsetPath -Algorithm SHA256).Hash; mods = $mods.Count; dependencyClosure = $closure.Count } } else { $null })
 harness = [ordered]@{ guid = $harnessProject.Guid; stage = (Join-Path $stageParent $harnessName)
  files = @(Get-ChildItem -LiteralPath $harnessSource -Recurse -File | ForEach-Object { [ordered]@{ path = $_.FullName.Substring($harnessSource.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } }) }
 audioSource = $audioRoot
 sampleSource = $SampleDir
 audioProjects = @($graph.Values | ForEach-Object { [ordered]@{ file = $_.File; guid = $_.Guid; events = $_.Events.Count } })
 staticPassed = $static.passed; staticGateSkipped = [bool]($SkipStaticGate -and !$static.passed)
 warnings = @($warnings)
}
Write-JsonFile (Join-Path $run 'inputs.json') $inputs 6
$display = Format-CommandLine $engine $arguments
$plan = [ordered]@{ run = $run; executable = $engine; workingDirectory = $GameDir; arguments = $arguments; commandLine = $display; timeoutSeconds = $TimeoutSeconds; expectedCases = $expectedCases.Count
 nativeProcessesAtPlanTime = @($busy | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) }
Write-JsonFile (Join-Path $run 'plan.json') $plan 5
foreach ($warning in $warnings) { Write-Host "warning: $warning" }
Write-Host ''
Write-Host "Run directory: $run"
Write-Host ("EXPBG GM Tools {0} from {1}; {2} addon(s) requested; {3} case(s) expected in mode {4}" -f $gmTools.Version, $gmTools.Dir, $addonIds.Count, $expectedCases.Count, $Mode)
Write-Host "Client: $display"
if ($DryRun) {
 if (@($busy).Count) { Write-Host "Note: native processes currently running: $($plan.nativeProcessesAtPlanTime -join ', ')" }
 Write-Host 'Dry run: nothing was launched.'
 exit 0
}

# ---- Launch the diagnostic client -----------------------------------------------------------------
$start = [Diagnostics.ProcessStartInfo]::new($engine)
$start.UseShellExecute = $false
$start.WorkingDirectory = $GameDir
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
Assert-NativeSlot
$process = [Diagnostics.Process]::Start($start)
if (!$process) { throw 'Diagnostic client failed to start.' }
$startedUtc = $process.StartTime.ToUniversalTime()
$receipt = [ordered]@{
 run = $run; purpose = 'EXPBG Ambient Sounds client audio sweep'; pid = $process.Id; startedUtc = $startedUtc.ToString('o'); executable = $engine
 profile = $(if ($UseDefaultProfile) { 'default' } else { $profileDir }); arguments = $arguments; mode = $Mode; banks = $Banks; launchMode = $LaunchMode; timeoutSeconds = $TimeoutSeconds
 timedOut = $false; compileFailure = $false; closedByRunner = $false; ownedProcessStopped = $false; nativeExitCode = $null; staticPassed = $static.passed; passed = $false
}
Write-JsonFile (Join-Path $run 'run.json') $receipt 4
Write-Host "Started $diagExeName pid=$($process.Id); timeout $TimeoutSeconds s."

$consoleLog = $null; $offset = 0; $carry = ''; $progress = 0; $announced = 0
$resultSeenAt = $null; $compileSeenAt = $null; $lastReport = [DateTime]::MinValue
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
try {
 while (!$process.WaitForExit(5000)) {
  $now = [DateTime]::UtcNow
  if (!$consoleLog) {
   $candidate = Get-ChildItem -LiteralPath $logs -Recurse -File -Filter 'console.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
   if ($candidate) { $consoleLog = $candidate.FullName }
  }
  if ($consoleLog) {
   $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
   $stream = [IO.File]::Open($consoleLog, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
   try {
    if ($stream.Length -lt $offset) { $offset = 0 }
    $null = $stream.Seek($offset, [IO.SeekOrigin]::Begin)
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $false, 65536, $true)
    $chunk = $reader.ReadToEnd()
    $reader.Dispose()
    $offset = $stream.Position
   } finally { $stream.Dispose() }
   $text = $carry + $chunk
   $cut = $text.LastIndexOf("`n")
   if ($cut -ge 0) {
    $complete = $text.Substring(0, $cut + 1)
    $carry = $text.Substring($cut + 1)
    $progress += ([regex]::Matches($complete, '\[EXPE2E AUDIO\] bank=')).Count
    $inventoryMatch = [regex]::Match($complete, '\[EXPE2E AUDIO INVENTORY\][^\r\n]* total=(\d+)')
    if ($inventoryMatch.Success) { $announced = [int]$inventoryMatch.Groups[1].Value }
    if (!$resultSeenAt -and $complete -match '\[EXPE2E AUDIO (RESULT|ABORT)\]') { $resultSeenAt = $now; Write-Host 'Harness finished; waiting for the client to close.' }
    if (!$compileSeenAt -and $complete -match "Can't compile|Script compilation failed") { $compileSeenAt = $now; Write-Host 'Script compilation failure detected in console.log.' }
   } else { $carry = $text }
  }
  if (($now - $lastReport).TotalSeconds -ge 30) {
   $lastReport = $now
   Write-Host ('  {0:HH:mm:ss} progress {1}/{2} events' -f [DateTime]::Now, $progress, $(if ($announced) { $announced } else { '?' }))
  }
  if ($resultSeenAt -and ($now - $resultSeenAt).TotalSeconds -gt 120) { $receipt.closedByRunner = $true; $receipt.ownedProcessStopped = Stop-OwnedProcess $process $startedUtc $engine; break }
  if ($compileSeenAt -and ($now - $compileSeenAt).TotalSeconds -gt 30) { $receipt.compileFailure = $true; $receipt.ownedProcessStopped = Stop-OwnedProcess $process $startedUtc $engine; break }
  if ($now -gt $deadline) { $receipt.timedOut = $true; $receipt.ownedProcessStopped = Stop-OwnedProcess $process $startedUtc $engine; break }
 }
 $process.WaitForExit()
 $receipt.nativeExitCode = $process.ExitCode
 if (!$consoleLog) {
  $candidate = Get-ChildItem -LiteralPath $logs -Recurse -File -Filter 'console.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
  if ($candidate) { $consoleLog = $candidate.FullName }
 }
 if (!$consoleLog) { throw "No console.log was written under $logs" }
 $summary = ConvertFrom-AudioLog ((Read-SharedText $consoleLog) -split "\r?\n") $consoleLog $context $Mode $Banks -CompareRequest
 if ($receipt.timedOut) { $summary.failures += "client did not finish within $TimeoutSeconds s (stopped by the runner)"; $summary.passed = $false }
 if ($receipt.compileFailure) { $summary.failures += 'script compilation failure (client stopped by the runner)'; $summary.passed = $false }
 if (!$static.passed) { $summary.failures += 'static audio resolution failed (see static-summary.json)'; $summary.passed = $false }
 if ($receipt.closedByRunner) { $summary.warnings += 'client did not close within 120 s after the result line; the runner stopped its own process' }
 if ($receipt.nativeExitCode -ne 0 -and !$receipt.ownedProcessStopped) { $summary.warnings += "client exit code $($receipt.nativeExitCode)" }
 $summary | Add-Member -NotePropertyName run -NotePropertyValue $receipt
 $summary | Add-Member -NotePropertyName inputWarnings -NotePropertyValue @($warnings)
 $receipt.passed = $summary.passed
 $recordings = Write-NativeOutputs $run '' $summary $context
 Write-SummaryReport $summary
 Write-Host ''
 Write-Host 'Per recording:'
 Write-RecordingTable $recordings
} finally {
 Write-JsonFile (Join-Path $run 'result.json') $receipt 4
}
Write-Host "Evidence: $run"
if (!$receipt.passed) { Write-Host 'FAIL: audio sweep gates not met (see summary.json, events.csv, recordings.csv).'; exit 1 }
Write-Host 'PASS: every swept EXPBG Ambient Sounds event resolved, started with a valid handle and played in the diagnostic client. This proves loading and native playback only, not audible loudness, mixing, replication or GM module behaviour.'
exit 0
#endregion
