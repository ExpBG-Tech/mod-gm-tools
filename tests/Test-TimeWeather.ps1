#requires -Version 7.0
# Portable guard for the Time and Weather module (addon/time-weather): two GM Systems
# entities, "Weather Transition" (blends clouds through the engine's weather state queue -
# direct start behind the node in place once its hold is over, else a pin node set at once
# with ours behind it -
# and eases rain/fog/wind overrides with the clouds so everything arrives together; optional
# smoothing of Scenario Properties weather, no instant previews) and "Time Skip" (one
# broadcast fade RPC, clock changed at full black with date rollover, overlapping skips
# refused). Checks registration, browser names, defaults, clamps, attribute wiring, hooks,
# layout, persistence configs, Enforce gotchas and the native fixture wiring (time and
# weather, cloud probe, cloud blend gate). No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Join-Path $repo 'addon/time-weather'
$scripts = Join-Path $module 'Scripts/Game/EXPTW'
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
function Get-MetaGuid([string]$Path) {
 $match = [regex]::Match((Read-Text $Path), 'Name\s+"\{([0-9A-F]{16})\}([^"]+)"')
 Assert $match.Success "metadata without a GUID: $Path"
 $match
}

# Registration: the folder and its pack.json entry land together (Assemble-Pack refuses others).
$pack = Get-Content -LiteralPath (Join-Path $repo 'tools/pack.json') -Raw | ConvertFrom-Json
$entry = @($pack.modules | Where-Object name -ceq 'time-weather')
Assert ($entry.Count -eq 1 -and $entry[0].title -ceq 'Time and Weather') 'tools/pack.json must register time-weather as "Time and Weather" once'
foreach ($shared in 'Configs/Editor/AttributeLists/Edit.conf', 'Configs/Editor/PlaceableEntities/Systems/Systems.conf', 'Configs/Systems/Persistence/GameMode/GameMaster.conf') {
 Assert ($pack.merge -contains $shared) "shared config $shared must be merged"
 Assert (Test-Path -LiteralPath (Join-Path $module $shared) -PathType Leaf) "module must bring its own $shared"
}
$expected = @(
 'Configs/Editor/AttributeCategories/ETW_Weather.conf', 'Configs/Editor/AttributeCategories/ETW_TimeSkip.conf',
 'Configs/Systems/Persistence/Configuration/EXPTW/WeatherTransition.conf', 'Configs/Systems/Persistence/Configuration/EXPTW/TimeSkip.conf',
 'PrefabsEditable/EXPTW/ETW_WeatherTransition.et', 'PrefabsEditable/EXPTW/ETW_TimeSkip.et', 'UI/layouts/EXPTW/ETW_Fade.layout',
 'Scripts/Game/EXPTW/ETW_Settings.c', 'Scripts/Game/EXPTW/ETW_TimeMath.c', 'Scripts/Game/EXPTW/ETW_WeatherModule.c', 'Scripts/Game/EXPTW/ETW_WeatherRunner.c',
 'Scripts/Game/EXPTW/ETW_TimeSkipModule.c', 'Scripts/Game/EXPTW/ETW_FadeOverlay.c', 'Scripts/Game/EXPTW/ETW_Attributes.c', 'Scripts/Game/EXPTW/ETW_Hooks.c')
foreach ($relative in $expected) { Assert (Test-Path -LiteralPath (Join-Path $module $relative) -PathType Leaf) "missing $relative" }

# Browser names: string-table keys whose text never starts with EXPBG.
$weatherPrefab = Read-Text (Join-Path $module 'PrefabsEditable/EXPTW/ETW_WeatherTransition.et')
$skipPrefab = Read-Text (Join-Path $module 'PrefabsEditable/EXPTW/ETW_TimeSkip.et')
Assert ($weatherPrefab -match '(?m)^ETW_WeatherModule \{' -and $weatherPrefab -match 'Name "#EXPBG-WeatherTransition_Name"' -and $weatherPrefab -match 'm_EntityType SYSTEM') 'Weather Transition prefab: ETW_WeatherModule, SYSTEM, name key'
Assert ($skipPrefab -match '(?m)^ETW_TimeSkipModule \{' -and $skipPrefab -match 'Name "#EXPBG-TimeSkip_Name"' -and $skipPrefab -match 'm_EntityType SYSTEM') 'Time Skip prefab: ETW_TimeSkipModule, SYSTEM, name key'
foreach ($prefab in $weatherPrefab, $skipPrefab) {
 Assert ($prefab -match 'SpatialRelevancy 0' -and $prefab -match 'Streamable Disabled') 'modules must be always relevant (status RplProps, broadcast fade RPC)'
 Assert ($prefab -match 'ENTITYTYPE_SYSTEM 157026' -and $prefab -match 'EXPBG_Badge_UI\.edds') 'modules carry the EXPBG label and badge'
 Assert ($prefab -match 'm_Image "\{[0-9A-F]{16}\}UI/Textures/EXPTW/ETW_\w+_Card\.edds"') 'modules carry their cooked EXPBG card'
}
$language = Join-Path $repo 'addon/unit-caching/Language'
$source = Read-Text (Join-Path $language 'EXPBG_Localization.st')
$runtime = Read-Text (Join-Path $language 'EXPBG_Localization.en_us.conf')
foreach ($pair in @(@('EXPBG-WeatherTransition_Name', 'Weather Transition'), @('EXPBG-TimeSkip_Name', 'Time Skip'))) {
 Assert ($source -match ('Id "' + $pair[0] + '"\s*\n\s*Target_en_us "' + $pair[1] + '"')) "source table must name $($pair[0]) '$($pair[1])'"
 Assert ($runtime.Contains('"' + $pair[0] + '"') -and $runtime.Contains('"' + $pair[1] + '"')) "runtime table must hold $($pair[0])"
 Assert ($pair[1] -notmatch '^\s*EXPBG') 'entity browser names never start with EXPBG'
}
foreach ($key in 'EXPBG-WeatherTransition_Description', 'EXPBG-TimeSkip_Description') { Assert ($source.Contains("Id `"$key`"") -and $runtime.Contains("`"$key`"")) "missing $key" }

# Systems registry and persistence configs point at the module's own metadata GUIDs.
$systems = Read-Text (Join-Path $module 'Configs/Editor/PlaceableEntities/Systems/Systems.conf')
foreach ($prefab in 'ETW_WeatherTransition.et', 'ETW_TimeSkip.et') {
 $meta = Get-MetaGuid (Join-Path $module "PrefabsEditable/EXPTW/$prefab.meta")
 Assert ($systems.Contains('"{' + $meta.Groups[1].Value + '}' + $meta.Groups[2].Value + '"')) "Systems.conf must list $prefab with its GUID"
}
$gameMaster = Read-Text (Join-Path $module 'Configs/Systems/Persistence/GameMode/GameMaster.conf')
foreach ($pair in @(@('WeatherTransition.conf', 'ETW_WeatherModule', 'ETW_WeatherModuleSerializer'), @('TimeSkip.conf', 'ETW_TimeSkipModule', 'ETW_TimeSkipModuleSerializer'))) {
 $path = Join-Path $module "Configs/Systems/Persistence/Configuration/EXPTW/$($pair[0])"
 $meta = Get-MetaGuid "$path.meta"
 Assert ($gameMaster.Contains('"{' + $meta.Groups[1].Value + '}' + $meta.Groups[2].Value + '"')) "GameMaster.conf must reference $($pair[0])"
 $config = Read-Text $path
 Assert ($config -match ('EntityClass "' + $pair[1] + '"') -and $config -match ('EntitySerializer ' + $pair[2] + ' ') -and $config -match 'Priority 20000') "$($pair[0]) must save $($pair[1]) with $($pair[2])"
}
Assert ($gameMaster -match 'Collection "\{6624C4351F719B74\}"') 'module entities go to the editable-entity collection'

# Settings: keys, defaults and clamps.
$settings = Read-Text (Join-Path $scripts 'ETW_Settings.c')
Assert ($settings -match 'static const int MINUTES_DEFAULT = 10;' -and $settings -match 'static const int MINUTES_MIN = 1;' -and $settings -match 'static const int MINUTES_MAX = 120;') 'transition time 1-120 min, default 10'
Assert ($settings -match 'static const int HOURS_DEFAULT = 6;' -and $settings -match 'static const int HOURS_MAX = 48;') 'skip hours up to 48, default 6'
Assert ($settings -match 'static const float FADE_OUT_DEFAULT = 2;' -and $settings -match 'static const float HOLD_DEFAULT = 3;' -and $settings -match 'static const float FADE_IN_DEFAULT = 2;') 'fades default to 2/3/2 s'
Assert ($settings -match 'static const string TEXT_DEFAULT = "\{hours\} hours later";' -and $settings -match 'static const int TEXT_LIMIT = 96;') 'default text {hours} hours later, 96 characters'
$weatherDefault = Get-Body $settings 'static\s+int\s+WeatherDefault\s*\('
Assert ($weatherDefault -match 'W_MINUTES\)\s*return MINUTES_DEFAULT;' -and $weatherDefault -match 'W_SMOOTH\)\s*return 1;') 'smoothing of Scenario Properties weather defaults to ON'
$clampSkip = Get-Body $settings 'static\s+float\s+ClampSkip\s*\('
Assert ($clampSkip -match 'Math\.Clamp\(Math\.Round\(value\), 0, HOURS_MAX\)' -and $clampSkip -match 'Math\.Clamp\(value, 1, 15\)' -and $clampSkip -match 'Math\.Clamp\(value, 0\.5, 10\)') 'skip clamps: hours 0-48, black screen 1-15 s, fades 0.5-10 s'
Assert ((Get-Body $settings 'static\s+int\s+ClampWeather\s*\(') -match 'Math\.ClampInt\(rounded, MINUTES_MIN, MINUTES_MAX\)') 'transition time clamp'
$weatherModule = Read-Text (Join-Path $scripts 'ETW_WeatherModule.c')
$skipModule = Read-Text (Join-Path $scripts 'ETW_TimeSkipModule.c')
Assert ($weatherModule -match '\[Attribute\("10", UIWidgets\.Slider, "Transition time \(min\)", "1 120 1"' -and $weatherModule -match '\[Attribute\("1", UIWidgets\.CheckBox, "Smooth Scenario Properties weather changes"') 'weather prefab attribute defaults match the settings'
Assert ($skipModule -match '\[Attribute\("6", UIWidgets\.Slider, "Hours to skip", "0 48 1"' -and $skipModule -match '\[Attribute\("2", UIWidgets\.Slider, "Fade to black \(s\)"' -and $skipModule -match '\[Attribute\("3", UIWidgets\.Slider, "Black screen \(s\)"' -and $skipModule -match '\[Attribute\("2", UIWidgets\.Slider, "Fade back in \(s\)"' -and $skipModule -match '\[Attribute\("\{hours\} hours later", UIWidgets\.EditBox') 'time skip prefab attribute defaults match the settings'

# Attributes: actions and statuses are never saved; statuses are local and read-only.
$attributes = Read-Text (Join-Path $scripts 'ETW_Attributes.c')
foreach ($class in 'ETW_WActionAttribute', 'ETW_TActionAttribute', 'ETW_WStatusAttribute', 'ETW_TStatusAttribute') {
 Assert ((Get-Body $attributes "class\s+$class\s*:") -match 'override bool IsSerializable\(\)\s*\{\s*return false;') "$class must not be serializable"
}
foreach ($class in 'ETW_WStatusAttribute', 'ETW_TStatusAttribute') { Assert ((Get-Body $attributes "class\s+$class\s*:") -match 'Enable\(false\);') "$class must be read-only" }
$weatherWrite = Get-Body $attributes 'class\s+ETW_WeatherAttribute\s*:'
Assert ($weatherWrite -match 'IsAllowedWrite\(manager, playerID\)' -and $weatherWrite -match 'if \(manager && m_Key != ETW_Settings\.W_SMOOTH\)\s*module\.QueueApply\(playerID\);') 'a confirmed weather change applies now; restores and the smoothing switch never start a transition'
$allowed = Get-Body $attributes 'static\s+bool\s+IsAllowedWrite\s*\('
Assert ($allowed -match 'return playerID == -1;' -and $allowed -match 'editor\.GetPlayerID\(\) != playerID \|\| editor\.IsLimited\(\)' -and $allowed -match 'EEditorMode\.EDIT') 'writes need this player''s unlimited editor in Edit mode, or a server restore'
Assert ((Get-Body $attributes 'class\s+ETW_SkipAttribute\s*:') -match 'module\.QueueSkip\(playerID\);') 'Skip time now queues the skip'
Assert ($attributes -match 'class ETW_TTextAttribute : EUD_TextAttribute' -and $attributes -match 'return ETW_Settings\.TEXT_LIMIT;') 'skip text uses the Unit Dialog edit box with the 96-character limit'
$list = Read-Text (Join-Path $module 'Configs/Editor/AttributeLists/Edit.conf')
$order = [regex]::Matches($list, '(?m)^  (ETW_\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value }
$listed = 'ETW_WTargetAttribute','ETW_WMinutesAttribute','ETW_WRainAttribute','ETW_WFogAttribute','ETW_WWindSpeedAttribute','ETW_WWindDirectionAttribute','ETW_WAfterAttribute','ETW_WSmoothAttribute','ETW_WActionAttribute','ETW_WStatusAttribute','ETW_THoursAttribute','ETW_TMinutesAttribute','ETW_TFadeOutAttribute','ETW_THoldAttribute','ETW_TFadeInAttribute','ETW_TTextAttribute','ETW_TShowTimeAttribute','ETW_TIncludeGmAttribute','ETW_TActionAttribute','ETW_TStatusAttribute','ETW_IOnAttribute','ETW_ITitleAttribute','ETW_ILocationAttribute','ETW_IHoldAttribute'
Assert (($order -join ',') -eq ($listed -join ',')) "attribute list order changed: $($order -join ',')"
foreach ($class in $listed) { Assert ($attributes -match "class\s+$class\s*:") "attribute class $class missing" }
foreach ($status in 'ETW_WStatusAttribute', 'ETW_TStatusAttribute') {
 $block = [regex]::Match($list, "(?s)$status \{(.*?)\n  \}").Groups[1].Value
 Assert ($block -match 'm_Key 200' -and $block -match 'm_bIsServer 0') "$status must be key 200 and local"
}
Assert ([regex]::Match($list, '(?s)ETW_WMinutesAttribute \{(.*?)\n  \}').Groups[1].Value -match 'm_fMin 1 m_fMax 120 m_fStep 1') 'transition time slider 1-120'
Assert ([regex]::Match($list, '(?s)ETW_THoursAttribute \{(.*?)\n  \}').Groups[1].Value -match 'm_fMin 0 m_fMax 48 m_fStep 1') 'skip hours slider 0-48'
Assert ([regex]::Match($list, '(?s)ETW_WTargetAttribute \{(.*?)\n  \}').Groups[1].Value -match 'AttributePrefab_ButtonBox_Selection\.layout') 'target weather uses the vanilla preset buttons'
foreach ($choice in @(@('ETW_WRainAttribute', 7), @('ETW_WFogAttribute', 7), @('ETW_WWindSpeedAttribute', 9), @('ETW_WWindDirectionAttribute', 9), @('ETW_WAfterAttribute', 2), @('ETW_WActionAttribute', 4), @('ETW_TActionAttribute', 2))) {
 $block = [regex]::Match($list, "(?s)$($choice[0]) \{(.*?)\n  \}").Groups[1].Value
 Assert ([regex]::Matches($block, 'SCR_EditorAttributeFloatStringValueHolder').Count -eq $choice[1]) "$($choice[0]) must offer $($choice[1]) choices"
}

# Weather runner: clouds through the engine queue, never ForceWeatherTo; foreign changes stop it.
$runner = Read-Text (Join-Path $scripts 'ETW_WeatherRunner.c')
Assert (!($runner -match 'ForceWeatherTo\(')) 'the runner must never call ForceWeatherTo (every call is treated as foreign)'
Assert (!($runner -match 'RemoveStateTransition\(')) 'queued weather nodes are never removed by hand (RemoveStateTransition crashed the server natively)'
Assert ((Get-Body $runner 'protected\s+static\s+float\s+RealToHours\s*\(') -match 'return seconds \* 24 / DayLength\(s_Manager\);' -and (Get-Body $runner 'protected\s+static\s+float\s+HoursToReal\s*\(') -match 'return hours \* DayLength\(s_Manager\) / 24;') 'real seconds and in-game hours convert at the current day length'
# Direct start: our node right behind the node in place, which stops looping with the shortest
# hold; the hold left over is read back (bounded) and waited out before pinning.
$direct = Get-Body $runner 'protected\s+static\s+bool\s+Direct\s*\('
Assert ($direct -match 'CreateStateTransition\(s_sTarget, RealToHours\(s_fRequested\), s_fStateHours\)' -and $direct -match 'InsertStateTransition\(1, node, false\)' -and $direct -match 'head\.SetLooping\(false\);\s*head\.SetStateDurationHours\(SHORTEST_HOURS\);\s*holdHours = Math\.Min\(head\.GetStateDurationHours\(\), DIRECT_MAX_HOURS\);' -and $direct -match 's_iCloudStage = CLOUD_DIRECT;' -and $direct -match 'PlanClouds\(now \+ wait, HoursToReal\(node\.GetTransitionDurationHours\(\)\)\);') 'direct start: our node behind the node in place, which stops looping; the hold left over and the blend length are read back'
Assert ($direct -match 'float timeLeft = transitions\.GetTimeLeftUntilNextState\(\);\s*float reading = timeLeft - node\.GetTransitionDurationHours\(\);\s*if \(timeLeft > 0 && reading <= holdHours \+ 0\.001\)' -and $direct -match 'float leftHours = holdHours;' -and $direct -match 's_fDirectUntil = now \+ wait \+ DIRECT_MARGIN_S;') 'the hold left over comes from the engine''s time left, bounded by the hold of the node in place (the latest start without a reading); the direct start waits it out plus a margin'
Assert ($direct -match 'if \(!DIRECT_WAITS_HOLD && wait > DIRECT_MARGIN_S\)\s*\{[^}]*return false;' -and $direct -match 'if \(s_bStartKnown && wait >= DIRECT_MARGIN_S\)\s*SetBlend\(node, s_fRequested - wait\);') 'DIRECT_WAITS_HOLD off pins instead of waiting; a known hold left over counts toward the requested time (through the floored SetBlend)'
# A queued node's blend is only shortened through SetBlend, never below the engine minimum (or
# the requested time when shorter): whether the engine raises a setter's value is unknown, and
# a 1 s blend switched the sky at once (5 min picked 3 min after a change, at the 1x day).
$code = [regex]::Replace($runner, '//[^\n]*', '')
Assert ([regex]::Matches($code, 'SetTransitionDurationHours\(').Count -eq 1 -and (Get-Body $runner 'protected\s+static\s+void\s+SetBlend\s*\(') -match '^\s*node\.SetTransitionDurationHours\(RealToHours\(Math\.Max\(seconds, ShortestBlend\(\)\)\)\);\s*$') 'every blend change of a queued node goes through SetBlend, floored at the shortest blend'
Assert ((Get-Body $runner 'protected\s+static\s+float\s+ShortestBlend\s*\(') -match 'return Math\.Min\(s_fRequested, HoursToReal\(FloorHours\(\)\)\);' -and (Get-Body $runner 'protected\s+static\s+float\s+FloorHours\s*\(') -match 'if \(s_fFloorHours > 0\)\s*return s_fFloorHours;\s*return BLEND_FLOOR_HOURS;' -and $runner -match 'static const float BLEND_FLOOR_HOURS = 0\.167;' -and $runner -match 'protected static float s_fFloorHours;') 'the shortest blend is the engine minimum (10 in-game minutes until the engine shows its own), or the requested time when shorter'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+NoteFloor\s*\(') -match 'if \(keptHours > askedHours \+ 0\.0005 && keptHours < 1\)\s*s_fFloorHours = keptHours;' -and $direct -match 'InsertStateTransition\(1, node, false\)\)\s*return false;\s*NoteFloor\(RealToHours\(s_fRequested\), node\.GetTransitionDurationHours\(\)\);') 'the engine minimum is learned only from a queued node of ours it raised'
Assert ($code -notmatch 'Math\.Max\([^;\n]*, 1\)\)') 'no blend is asked as short as 1 s any more'
$kick = Get-Body $runner 'protected\s+static\s+void\s+Kick\s*\('
Assert ($kick -match 'SetBlend\(s_CloudNode, s_fBegin \+ s_fRequested - now\);\s*WeatherTransitionRequestResponse response = transitions\.RequestStateTransition\(\);') 'the start request asks for the time left, floored, before RequestStateTransition'
$adopt = Get-Body $runner 'protected\s+static\s+void\s+AdoptClouds\s*\('
Assert ($adopt -match 'float start = Math\.Max\(s_fCloudStart, now\);\s*SetBlend\(s_CloudNode, now \+ s_fRequested - start\);\s*PlanClouds\(start, HoursToReal\(s_CloudNode\.GetTransitionDurationHours\(\)\)\);\s*s_fPinBlend = s_fCloudEnd - s_fCloudStart;') 'a node of ours that has not started is resized floored (also when it starts later than the new time), and that plan is what a failed start request returns to'
# Model of SetBlend at the reviewer's cases (real seconds; floor 0.167 in-game hours).
$floorHours = [double]::Parse([regex]::Match($runner, 'static const float BLEND_FLOOR_HOURS = ([0-9.]+);').Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
function Get-Blend([double]$Requested, [double]$Seconds, [double]$Day) { [Math]::Max($Seconds, [Math]::Min($Requested, $floorHours * $Day / 24)) }
Assert ((Get-Blend 300 (300 - 420) 86400) -eq 300 -and [Math]::Round((Get-Blend 1800 (1800 - 420) 86400)) -eq 1380 -and [Math]::Round((Get-Blend 60 (60 - 10) 1440)) -eq 50 -and [Math]::Round((Get-Blend 60 -5 1440), 1) -eq 10.0) 'SetBlend model: 5 min picked with 7 min of hold left blends 5 min (was 1 s); longer times keep the hold left counted; the 1440 s fixture day keeps its 50 s replace blend; a late start never drops below the 10 s floor there'
Assert ($runner -match 'static const float DIRECT_MARGIN_S = 2;' -and $runner -match 'static const float DIRECT_MAX_HOURS = 0\.2;' -and $runner -match 'static const bool DIRECT_WAITS_HOLD = true;' -and $runner -notmatch 'DIRECT_WAIT_S\b') 'the direct start waits out the hold left over (at most 0.2 in-game hours) plus 2 s, not a fixed 1.5 s'
Assert ($direct -notmatch 'RequestStateTransition') 'a direct start asks the engine for nothing (RequestStateTransition restarted the weather in place)'
$directNext = Get-Body $runner 'protected\s+static\s+bool\s+DirectNext\s*\('
Assert ($directNext -match 'if \(index == 0\)\s*return true;' -and $directNext -match 'return index == 1 && transitions\.GetStateTransitionNode\(0\) == transitions\.GetCurrentStateTransitionNode\(\);') 'our direct node is next: first in the queue, or right behind the node in place'
# Pinned start: pin and our node at the back, durations read back, the pin set at once.
$pin = Get-Body $runner 'protected\s+static\s+bool\s+Pin\s*\('
Assert ($pin -match '^\s*//[^\n]*\n\s*if \(from\.IsEmpty\(\)\)\s*\{\s*s_iCloudStage = CLOUD_NONE;[^}]*return false;\s*\}\s*WeatherStateTransitionNode pin = transitions\.CreateStateTransition\(from,') 'a pin never creates a node with an empty weather name (the engine writes through memory it cannot find)'
$order = @('CreateStateTransition(from, SHORTEST_HOURS, SHORTEST_HOURS)', 'pin.SetLooping(false);', 'EnqueueStateTransition(pin, false)', 'NoteFloor(SHORTEST_HOURS, pin.GetTransitionDurationHours());', 'float hold = HoursToReal(pin.GetStateDurationHours());', 'CreateStateTransition(s_sTarget, RealToHours(blend), s_fStateHours)', 'queued = transitions.EnqueueStateTransition(node, false);', 'WatchUntouched(now);', 'WeatherTransitionRequestResponse response = transitions.RequestStateTransitionImmediately(pin);', 'PlanClouds(now + hold, HoursToReal(node.GetTransitionDurationHours()));', 's_fPinBlend = s_fCloudEnd - s_fCloudStart;', 'HoldUntouched();')
$at = -1
foreach ($needle in $order) { $next = $pin.IndexOf($needle, [Math]::Max($at, 0)); Assert ($next -gt $at) "pinned start out of order or missing: $needle"; $at = $next }
Assert ($pin -match 'float blend = Math\.Max\(s_fBegin \+ s_fRequested - \(now \+ hold\), ShortestBlend\(\)\);' -and $pin -match 'pin\.SetLooping\(true\);\s*transitions\.RequestStateTransitionImmediately\(pin\);') 'the pin hold counts toward the requested time (never below the shortest blend); a failed queue keeps the weather in place held'
# One start request at most, only when our node is first in the queue after the pin.
Assert ([regex]::Matches([regex]::Replace($runner, '//[^\n]*', ''), 'RequestStateTransition\(\)').Count -eq 1 -and (Get-Body $runner 'protected\s+static\s+void\s+Kick\s*\(') -match 's_bKickTried = true;[\s\S]*RequestStateTransition\(\)') 'RequestStateTransition() only in Kick, which marks itself tried'
$update = Get-Body $runner 'protected\s+static\s+void\s+UpdateClouds\s*\('
Assert ($update -match 'if \(START_REQUEST && !s_bKickTried && !moving && now - s_fStageAt >= 0\.4 && QueueIndex\(transitions, s_CloudNode\) == 0\)\s*\{\s*Kick\(transitions, now\);' -and $runner -match 'static const bool START_REQUEST = true;') 'the start request is asked once, only with our node first in the queue and nothing moving (START_REQUEST switches it off)'
$directStage = [regex]::Match($update, 'if \(s_iCloudStage == CLOUD_DIRECT\)\s*\{([\s\S]*?)\n  \}').Groups[1].Value
Assert ($directStage -match 'if \(moving\)\s*\{\s*reason = ' -and $directStage -match 'else if \(!DirectNext\(transitions\)\)\s*reason = ' -and $directStage -match 'else if \(now >= s_fDirectUntil\)\s*reason = ' -and $directStage -match 'if \(!reason\.IsEmpty\(\)\)\s*\{\s*LogQueue\(transitions, reason\);\s*Pin\(transitions, from, now\);' -and $directStage -match 'if \(NativeProgress\(transitions\) >= 0\.5 && !NextName\(transitions\)\.IsEmpty\(\)\)\s*from = NextName\(transitions\);') 'a direct start pins only when something else blends (on the nearer weather), our node is no longer next, or the hold left over has run out'
Assert ($update -match 'if \(moving && !s_bRepinned\)\s*\{[\s\S]*?s_bRepinned = true;') 'a start request that moved another node is undone by one more pin, once'
$kicked = [regex]::Match($update, 'if \(s_iCloudStage == CLOUD_KICKED\)\s*\{([\s\S]*?)\n  \}').Groups[1].Value
Assert ($kicked -match 's_iCloudStage = CLOUD_PINNED;\s*SetBlend\(s_CloudNode, s_fPinBlend\);\s*PlanClouds\(s_fCloudStart, HoursToReal\(s_CloudNode\.GetTransitionDurationHours\(\)\)\);') 'a start request that started nothing gives the pin its own blend back (the end does not move a pin hold later)'
$startBody = Get-Body $runner 'static\s+bool\s+Start\s*\('
Assert ($startBody -match 'if \(oursPending \|\| !DIRECT_START \|\| transitions\.GetStateTransitionsCount\(\) < 1 \|\| !Direct\(transitions, now\)\)\s*Pin\(transitions, current, now\);') 'a clean start tries direct only when DIRECT_START is on; a node of ours still queued or an empty queue goes pinned'
Assert ($runner -match 'static const bool DIRECT_START = false;') 'the direct start stays off (the 2026-10-08 native cloud probe reported direct=misdirected): every cloud change pins'
Assert ($startBody -match 'bool adopt = oursPending && runningTarget == target;') 'a replacing transition with the same target keeps our cloud node'
# Keeping the clouds (no target, Return to automatic weather) never jumps: our blend goes on,
# another source's blend is left to run; resolved before the statics are replaced.
Assert ($startBody -match 'bool keepClouds = FindStateIndex\(states, target\) < 0;' -and $startBody -match 'if \(oursBlending\)\s*target = runningTarget;\s*else if \(moving && !oursPending\)\s*\{\s*leaveBlend = true;' -and $startBody -match 'if \(leaveBlend\)\s*Log\([^\n]*\);\s*else if \(moving\)' -and $startBody.IndexOf('bool keepClouds') -lt $startBody.IndexOf('s_sTarget = target;')) 'kept clouds: a blend of ours continues (adopted), a blend of another weather source is left to run, no jump'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+Complete\s*\(') -match 'else if \(!s_bCloudChange && \(CurrentName\(transitions\) != s_sTarget \|\| IsMoving\(transitions\)\)\)\s*done = ') 'the end status says when the clouds are still changing by themselves'
# Lost node: something else rebuilt the queue; the clouds are left to it (no end snap).
$lost = Get-Body $runner 'protected\s+static\s+void\s+CheckLost\s*\('
Assert ($lost -match 'QueueIndex\(transitions, s_CloudNode\) >= 0 \|\| HeadsToTarget\(transitions\) \|\| CurrentName\(transitions\) == s_sTarget' -and $lost -match 'if \(s_iLostTicks < LOST_TICKS\)' -and $lost -match 's_bDeferred = true;\s*s_bCloudChange = false;') 'a lost cloud node (by identity and destination, LOST_TICKS in a row) defers the clouds'
$complete = Get-Body $runner 'protected\s+static\s+void\s+Complete\s*\('
Assert ($complete -match 'else if \(!s_bDeferred\)\s*ApplyLooping\(transitions, s_bHold\);' -and $complete -match 'if \(!s_bDeferred\)\s*manager\.ETW_SetLoopingFlag\(s_bHold\);' -and $complete -match 'if \(s_bDeferred\)\s*done = "Done: rain, fog and wind reached; the clouds were left to another weather source\.";') 'clouds left to another weather source: the end neither loops its queue nor sets the looping flag, and the status says so'
$stop = Get-Body $runner 'static\s+bool\s+Stop\s*\('
Assert ($stop -match 'if \(s_bDeferred\)\s*\{\s*Halt\([^\n]*the clouds were left to another weather source[^\n]*\);[^}]*return true;\s*\}' -and $stop.IndexOf('if (s_bDeferred)') -lt $stop.IndexOf('Snap(')) 'Stop leaves clouds handed to another weather source alone'
Assert ($runner -match 'static const int QUEUE_SCAN = 8;' -and (Get-Body $runner 'protected\s+static\s+int\s+ScanCount\s*\(') -match 'if \(count > QUEUE_SCAN\)') 'queue scans are bounded'
Assert ($runner -match 'static const int QUEUE_LOGS = 3;' -and (Get-Body $runner 'protected\s+static\s+void\s+LogQueue\s*\(') -match 'if \(s_iQueueLogs >= QUEUE_LOGS\)\s*return;\s*s_iQueueLogs\+\+;' -and (Get-Body $runner 'static\s+bool\s+Start\s*\(') -match 's_iQueueLogs = 0;') 'queue dumps are bounded per transition (three) and reset by each start'
# Rain and fog left to the weather across an immediate change: watched (a tick later, logged)
# or, with HOLD_ACROSS_PIN, held and handed back gradually.
Assert ($runner -match 'static const bool HOLD_ACROSS_PIN = false;' -and (Get-Body $runner 'protected\s+static\s+void\s+Tick\s*\(') -match 'if \(s_bWatchRain \|\| s_bWatchFog\)\s*CheckUntouched\(now\);') 'pins are measured for rain and fog left to the weather; holding them is off until a native pin jump is seen'
$watch = Get-Body $runner 'protected\s+static\s+void\s+WatchUntouched\s*\('
Assert ($watch -match 's_bWatchRain = s_aMode\[CH_RAIN\] == MODE_NONE && !s_Manager\.IsRainIntensityOverridden\(\);' -and $watch -match 's_bWatchFog = s_aMode\[CH_FOG\] == MODE_NONE && !s_Manager\.IsFogAmountOverridden\(\);') 'only rain and fog left to the weather are watched'
$holdBody = Get-Body $runner 'protected\s+static\s+bool\s+HoldUntouched\s*\('
Assert ($holdBody -match 'if \(!HOLD_ACROSS_PIN' -and (Get-Body $runner 'protected\s+static\s+void\s+HoldChannel\s*\(') -match 's_aMode\[channel\] = MODE_RELEASE;\s*s_aFrom\[channel\] = value;\s*s_aTo\[channel\] = NaturalValue\(states, s_sTarget, channel, value\);\s*s_aLast\[channel\] = UNWRITTEN;') 'a held value starts at the value read before the change and is handed back like weather default'
$snapStart = Get-Body $runner 'protected\s+static\s+bool\s+SnapAtStart\s*\('
Assert ($snapStart -match 'WatchUntouched\(now\);\s*Snap\(transitions, stateName, looping, s_fStateHours\);\s*return HoldUntouched\(\);' -and ([regex]::Matches((Get-Body $runner 'static\s+bool\s+Start\s*\('), 'SnapAtStart\(').Count -eq 2)) 'an immediate change when a transition starts is watched like a pin'
# Snap: at the back of the queue like vanilla ForceWeatherTo, then set at once.
$snap = Get-Body $runner 'protected\s+static\s+bool\s+Snap\s*\('
Assert ($snap -match 'EnqueueStateTransition\(node, false\)' -and $snap -notmatch 'InsertStateTransition' -and $snap -match 'RequestStateTransitionImmediately\(node\) == WeatherTransitionRequestResponse\.SUCCESS') 'an immediate change goes to the back of the queue and drops everything ahead of it'
# Rain, fog and wind ease with the clouds; the planned end never comes before the requested time.
Assert ($runner -match 'static const int TICK_MS = 500;' -and (Get-Body $runner 'protected\s+static\s+float\s+Ease\s*\(') -match 't = Math\.Clamp\(\(now - s_fEaseFrom\) / \(s_fEnd - s_fEaseFrom\), 0, 1\);\s*float eased = t \* t \* \(3 - 2 \* t\);') 'overrides step every 0.5 s along an ease-in-out curve from the clouds'' start to the planned end'
$retime = Get-Body $runner 'protected\s+static\s+void\s+Retime\s*\('
Assert ($retime -match 'float eased = Ease\(now\);[\s\S]*s_aFrom\[c\] = s_aFrom\[c\] \+ \(s_aTo\[c\] - s_aFrom\[c\]\) \* eased;[\s\S]*s_fEaseFrom = Math\.Max\(easeFrom, now\);\s*s_fEnd = Math\.Max\(end, s_fBegin \+ s_fRequested\);') 'a new timing continues from the values reached and never ends before the requested time'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+PlanClouds\s*\(') -match 'Retime\(start, s_fCloudEnd\);' -and (Get-Body $runner 'protected\s+static\s+void\s+Started\s*\(') -match 'if \(Math\.AbsFloat\(now - s_fEaseFrom\) > 2 \|\| end > s_fEnd \+ 2\)\s*Retime\(now, end\);') 'rain, fog and wind follow the planned and the observed cloud blend'
$cloudText = Get-Body $runner 'protected\s+static\s+string\s+CloudText\s*\('
Assert ($cloudText -match '10 in-game minutes' -and $cloudText -match 'GetIsDayAutoAdvanced\(\)' -and $cloudText -match 'Clouds start changing in about') 'the status says when the clouds start, the 10 in-game minute minimum and a paused clock'
$tickBody = Get-Body $runner 'protected\s+static\s+void\s+Tick\s*\('
Assert ($tickBody -match 'bool finishing = !cloudsThere && s_iCloudStage == CLOUD_BLENDING;' -and $tickBody -match 'if \(s_bCloudChange\)\s*UpdateClouds\(transitions, now\);') 'the end waits for clouds that are blending, and only for them'
$tick = Get-Body $runner 'protected\s+static\s+void\s+Tick\s*\('
Assert ($tick -match 'float written = WrittenValue\(c, value\);\s*if \(written != s_aLast\[c\]\)\s*\{\s*WriteChannel\(s_Manager, c, value\);\s*s_aLast\[c\] = written;' -and (Get-Body $runner 'protected\s+static\s+void\s+WriteChannel\s*\(') -match 'float written = WrittenValue\(channel, value\);' -and (Get-Body $runner 'static\s+bool\s+Start\s*\(') -match 's_aLast\[c\] = UNWRITTEN;') 'a tick rewrites an override only when the value it would write changed (every start writes afresh)'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+Complete\s*\(') -match 'if \(s_aMode\[c\] == MODE_SET\)\s*WriteChannel\(manager, c, s_aTo\[c\]\);') 'the end writes the exact targets unconditionally'
Assert ((Get-Body $runner 'static\s+bool\s+Start\s*\(') -match 'float live = ReadChannel\(manager, c\);' ) 'a new transition starts from the live values'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+Complete\s*\(') -match 'Snap\(transitions, s_sTarget, s_bHold, s_fStateHours\);') 'clouds that did not finish are set at the end'
Assert ((Get-Body $runner 'static\s+void\s+OnForeignWeather\s*\(') -match 'ReleaseChannel\(s_Manager, c\);') 'a foreign weather change releases the overrides the transition set'
$hooks = Read-Text (Join-Path $scripts 'ETW_Hooks.c')
$force = Get-Body $hooks 'override\s+void\s+ForceWeatherTo\s*\('
Assert ($force.IndexOf('ETW_WeatherRunner.OnForeignWeather(playerThatChangedWeather);') -ge 0 -and $force.IndexOf('ETW_WeatherRunner.OnForeignWeather(playerThatChangedWeather);') -lt $force.IndexOf('super.ForceWeatherTo(')) 'ForceWeatherTo stops the transition before the vanilla change'
$foreignBody = Get-Body $runner 'static\s+void\s+OnForeignWeather\s*\(int playerId = 0\)'
Assert ($foreignBody -match 'if \(playerId > 0\)\s*status = "Stopped: a Game Master changed the weather in Scenario Properties;' -and $foreignBody -match 'string status = "Stopped: the mission or another mod set the weather;') 'a foreign weather change says who won: a Game Master in Scenario Properties, else the mission or another mod'
foreach ($wind in 'DelayedSetWindOverride', 'DelayedOverrideWindSpeed', 'DelayedOverrideWindDirection') { Assert ((Get-Body $hooks "override\s+void\s+$wind\s*\(") -match 'ETW_WeatherRunner\.OnForeignWind\(\);\s*super\.') "$wind must hand wind back before the vanilla call" }
Assert ((Get-Body $hooks 'modded\s+class\s+SCR_WeatherInstantEditorAttribute') -match 'if \(item && var && manager && playerID > 0 && ETW_WeatherRunner\.SmoothVanillaRequest\(var\.GetInt\(\), playerID\)\)\s*return;\s*super\.WriteVariable') 'Scenario Properties smoothing skips previews and restores and otherwise keeps vanilla'
Assert ((Get-Body $hooks 'override\s+void\s+PreviewVariable\s*\(') -match 'if \(setPreview && ETW_WeatherModule\.FindSmoothing\(\)\)\s*\{\s*super\.PreviewVariable\(false, manager\);\s*return;\s*\}\s*super\.PreviewVariable\(setPreview, manager\);') 'with smoothing on, Scenario Properties weather shows no instant preview (it jumped and reset on Save)'
$targetPreview = Get-Body $attributes 'override\s+void\s+PreviewVariable\s*\('
Assert ($targetPreview -notmatch 'SetWeatherStatePreview\(true' -and $targetPreview -match 'SetWeatherStatePreview\(false\)') 'the target weather never previews the new sky; it only clears a preview'
Assert ($weatherModule -match '\[Attribute\("1", UIWidgets\.CheckBox, "Smooth Scenario Properties weather changes", category: "EXPBG Weather Transition"\), RplProp\(\)\]' -and (Get-Body $weatherModule 'protected\s+void\s+StoreSmooth\s*\(') -match 'if \(smooth == m_bSmooth\)\s*return;\s*m_bSmooth = smooth;\s*if \(Replication\.IsServer\(\)\)\s*Replication\.BumpMe\(\);') 'the smoothing switch is replicated (bumped only when it changes) so clients skip the preview'
foreach ($pair in @(@($weatherModule, 'ETW_WeatherModule'), @($skipModule, 'ETW_TimeSkipModule'))) {
 Assert ($pair[0].Contains("protected static ref array<$($pair[1])> s_aModules;") -and (Get-Body $pair[0] "protected\s+static\s+void\s+EXPBG_LazyStatics_$($pair[1])\s*\(").Contains("s_aModules = new array<$($pair[1])>();")) "$($pair[1]) creates its module list on first use"
}
Assert ((Get-Body $runner 'static\s+bool\s+SmoothVanillaRequest\s*\(') -match 'CallLater\(ETW_WeatherRunner\.StartVanilla, 0,') 'smoothed Scenario Properties weather starts after the rest of the Save'

# Time skip: one broadcast RPC, local host handled, clock at full black, overlap refused.
Assert ($skipModule -match '\[RplRpc\(RplChannel\.Reliable, RplRcver\.Broadcast\)\]\s*protected void RpcDo_Fade\(') 'the fade is a reliable broadcast RPC'
$broadcast = Get-Body $skipModule 'void\s+BroadcastFade\s*\('
Assert ($broadcast -match 'RpcDo_Fade\(fadeOut' -and $broadcast -match 'Rpc\(RpcDo_Fade,' -and $broadcast.IndexOf('RpcDo_Fade(fadeOut') -lt $broadcast.IndexOf('Rpc(RpcDo_Fade,')) 'the host runs its own fade, then broadcasts'
$start = Get-Body $skipModule 'static\s+bool\s+Start\s*\(\s*notnull\s+ETW_TimeSkipModule'
Assert ($start -match 'if \(IsRunning\(\)\)' -and $start -match 'Refuse\("Refused: a time skip is already running\."' -and $start -match 'totalMinutes <= 0') 'overlapping and empty skips are refused'
Assert ($start -match 'int applyMs = Math\.Round\(\(fadeOut \+ hold \* 0\.5\) \* 1000\);' -and $start -match 'CallLater\(ETW_TimeSkip\.Apply, applyMs, false, s_iSerial\)') 'the clock changes in the middle of the black screen'
$apply = Get-Body $skipModule 'protected\s+static\s+void\s+Apply\s*\('
Assert ($apply -match 'serial != s_iSerial' -and $apply -match 'ETW_WeatherRunner\.FinishNow\("time skip"\);' -and $apply -match 'SetDate\(toYear, toMonth, toDay, true\)' -and $apply -match 'SetTimeOfTheDay\(toHours, true\)') 'Apply finishes a transition, then sets date and time immediately'
Assert ($apply.IndexOf('SetDate(') -lt $apply.IndexOf('SetTimeOfTheDay(')) 'date before time'
$math = Read-Text (Join-Path $scripts 'ETW_TimeMath.c')
Assert ((Get-Body $math 'static\s+bool\s+IsLeapYear\s*\(') -match 'year % 400 == 0' -and (Get-Body $math 'static\s+int\s+DaysInMonth\s*\(') -match 'month == 4 \|\| month == 6 \|\| month == 9 \|\| month == 11') 'Gregorian leap years and month lengths'
# Same rollover as AddHours, in PowerShell, for the fixture's cases.
function Add-Hours([int]$Y, [int]$M, [int]$D, [double]$H, [double]$Add) {
 $total = $H + $Add; $days = [math]::Floor($total / 24); $rest = $total - $days * 24
 for ($i = 0; $i -lt $days; $i++) { $D++; if ($D -le [DateTime]::DaysInMonth($Y, $M)) { continue }; $D = 1; $M++; if ($M -le 12) { continue }; $M = 1; $Y++ }
 [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0:D4}-{1:D2}-{2:D2} {3}', $Y, $M, $D, $rest)
}
Assert ((Add-Hours 2026 12 31 22 26.5) -eq '2027-01-02 0.5') 'year-end rollover model'
Assert ((Add-Hours 2028 2 28 23 2) -eq '2028-02-29 1' -and (Add-Hours 2027 2 28 23 2) -eq '2027-03-01 1') 'leap-day rollover model'
$overlay = Read-Text (Join-Path $scripts 'ETW_FadeOverlay.c')
$show = Get-Body $overlay 'static\s+void\s+Show\s*\('
Assert ($show.IndexOf('s_iReceived++;') -lt $show.IndexOf('System.IsConsoleApp()') -and $show -match 'if \(System\.IsConsoleApp\(\)\)\s*return;') 'a dedicated server counts the fade but draws nothing'
Assert ($show -match 'SetZOrder\(Z_ORDER\)' -and $show -match 'WidgetFlags\.IGNORE_CURSOR \| WidgetFlags\.NOFOCUS' -and $overlay -match 'static const int Z_ORDER = 10000;') 'black screen at the workspace root, above menus, never taking input'
$layoutMeta = Get-MetaGuid (Join-Path $module 'UI/layouts/EXPTW/ETW_Fade.layout.meta')
Assert ($overlay.Contains('"{' + $layoutMeta.Groups[1].Value + '}' + $layoutMeta.Groups[2].Value + '"')) 'overlay must load the layout by its metadata GUID'
$layout = Read-Text (Join-Path $module 'UI/layouts/EXPTW/ETW_Fade.layout')
foreach ($name in 'ETW_FadeRoot', 'ETW_Black', 'ETW_Text', 'ETW_Time') { Assert ($layout.Contains("Name `"$name`"")) "layout widget $name missing" }
Assert ($layout -match '(?s)Name "ETW_Black".*?Anchor 0 0 1 1.*?Opacity 0\s*Color 0 0 0 1') 'black full-screen image starts transparent'

# Enforce gotchas in every module source and the fixtures; new files are ASCII with LF.
$fixturePath = Join-Path $repo 'tests/ETW_TimeWeatherGameplay.c'
$probePath = Join-Path $repo 'tests/ETW_CloudProbeGameplay.c'
$blendPath = Join-Path $repo 'tests/ETW_CloudBlendGameplay.c'
$moduleSources = @(Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File | ForEach-Object FullName)
foreach ($path in $moduleSources) {
 Assert (![regex]::IsMatch((Read-Text $path), '(?m)^\s*(protected\s+|private\s+)?static\s+(?!const\b)[^;(){}]*=')) "static field with an initializer (create it on first use) in $path"
}
$sources = $moduleSources + $fixturePath + $probePath + $blendPath
foreach ($path in $sources) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 # A method named Wait was ignored natively (the next fixture phase ran one frame later).
 Assert (![regex]::IsMatch($text, '\b(void|bool|int|float)\s+Wait\s*\(') -and ![regex]::IsMatch($text, '(?m)^\s*Wait\s*\(')) "a method named Wait is ignored natively (use PauseFor): $path"
 Assert (![regex]::IsMatch($text, '(?m)^\s*(protected\s+|private\s+)?(static\s+)?(ref\s+)?\w+(<[^>]*>)?\s+(WeatherState|WeatherVariant|WeatherTransition|LocalWeatherSituation|TimeAndWeatherManagerEntity|FactionKey|ResourceName|Widget|TextWidget)\s*(=|;)')) "a field or variable is named like a vanilla type in $path"
 foreach ($line in $text -split "`n") {
  Assert (!($line -match '\S.*\breturn\s+[^;\s]' -and $line -notmatch '^\s*return\b' -and $line -notmatch '^\s*//')) "non-void return must be on its own line in ${path}: $($line.Trim())"
 }
}
foreach ($file in Get-ChildItem -LiteralPath $module -Recurse -File | Where-Object { $_.Extension -notin '.png', '.edds' }) {
 $bytes = [IO.File]::ReadAllBytes($file.FullName)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $($file.FullName)"
 Assert (!($bytes -contains 13)) "CR line ending in $($file.FullName)"
}
foreach ($path in $fixturePath, $probePath, $blendPath, $PSCommandPath) {
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0 -and !($bytes -contains 13)) "new test file must be ASCII with LF: $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'fixture must keep the runner driver class names'
# The weather phases run at a 1440 s day (the engine's 10-in-game-minute cloud minimum is then
# 10 s; at the 86400 s day a 1-minute transition takes 10 minutes and gradual/finished fail), so
# the clouds must blend: smooth=1 only.
$regex = '\[ETW RESULT\] checks=[1-9]\d* failures=0 rollover=1 skip=1 broadcasts=3 gradual=1 finished=1 interrupt=1 skipFinish=1 foreign=1 smooth=1 reason=complete'
Assert ($fixture.Contains("-FixturePath tests/ETW_TimeWeatherGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '$regex'") -and !$fixture.Contains('smooth=[01]')) 'fixture header must carry its runner command, with smooth=1 required'
Assert ($fixture -match 'm_fDayLength = m_TimeManager\.GetDayDuration\(\);\s*m_TimeManager\.SetDayDuration\(1440\);' -and (Get-Body $fixture 'void\s+Finish\s*\(') -match 'if \(m_TimeManager && m_fDayLength > 0\)\s*m_TimeManager\.SetDayDuration\(m_fDayLength\);') 'fixture runs its weather phases at a 1440 s day and restores the day length in Finish'
# gradual: rain judged every frame against the highest confirmed reading; the one-frame read
# of the value the runner just wrote (the engine applies it about 0.75 s later) is tolerated
# up to WRITE_FRAME_LIMIT, never confirmed, and counted; any other move back fails.
$observe = Get-Body $fixture 'void\s+ObserveRain\s*\('
$sampleWeather = Get-Body $fixture 'void\s+SampleWeather\s*\('
Assert ($fixture -match 'static const float RAIN_SLACK = 0\.01;' -and [regex]::Match($fixture, 'static const float WRITE_FRAME_LIMIT = (0\.\d+);').Success -and [double][regex]::Match($fixture, 'static const float WRITE_FRAME_LIMIT = (0\.\d+);').Groups[1].Value -le 0.025) 'fixture tolerates a write-frame read of at most 0.025 and a move back of at most 0.01'
Assert ($observe -match 'bool writeFrame = back > RAIN_SLACK && progressed >= m_fRainHigh - RAIN_SLACK && m_fLastProgress - m_fRainHigh <= WRITE_FRAME_LIMIT;' -and $observe -match 'if \(writeFrame\)\s*m_iWriteFrames\+\+;\s*else\s*m_fRainHigh = Math\.Max\(m_fRainHigh, m_fLastProgress\);' -and $observe -match 'if \(progressed < m_fRainHigh - RAIN_SLACK\)\s*m_bMonotonic = false;') 'fixture: a reading below the highest confirmed rain fails; only a one-frame write read is skipped'
Assert ($sampleWeather -match 'ObserveRain\(progressed\);' -and $sampleWeather -match 'if \(running && elapsed < 100\)\s*return;' -and $sampleWeather -notmatch 'PauseFor\(2\)' -and $sampleWeather -match 'write-frame reads %4') 'fixture: rain is judged every frame while the transition runs and the write-frame reads are reported'
$head = [regex]::Match($fixture, 'string head = string\.Format\("(\[ETW RESULT\][^"]*)"').Groups[1].Value
$tail = [regex]::Match($fixture, 'string tail = string\.Format\("( foreign=[^"]*)"').Groups[1].Value
Assert ($head -eq '[ETW RESULT] checks=%1 failures=%2 rollover=%3 skip=%4 broadcasts=%5 gradual=%6 finished=%7 interrupt=%8 skipFinish=%9' -and $tail -eq ' foreign=%1 smooth=%2 reason=%3') "fixture RESULT format changed: $head$tail"
function Format-Result([string[]]$Head, [string[]]$Tail) {
 $line = $script:head; for ($i = 9; $i -ge 1; $i--) { $line = $line.Replace("%$i", $Head[$i - 1]) }
 $end = $script:tail; for ($i = 3; $i -ge 1; $i--) { $end = $end.Replace("%$i", $Tail[$i - 1]) }
 $line + $end
}
Assert ((Format-Result @('24', '0', '1', '1', '3', '1', '1', '1', '1') @('1', '1', 'complete')) -match $regex) 'a passing RESULT line must match the regex'
Assert (!((Format-Result @('24', '1', '1', '1', '3', '0', '1', '1', '1') @('1', '1', 'complete')) -match $regex)) 'a failing RESULT line must not match the regex'
Assert ($fixture -match 'Resource weatherPrefab = Resource\.Load\(WEATHER_MODULE\);' -and $fixture -match 'Resource skipPrefab = Resource\.Load\(SKIP_MODULE\);') 'fixture must keep Resource.Load results in locals'
foreach ($prefab in 'ETW_WeatherTransition.et', 'ETW_TimeSkip.et') {
 $meta = Get-MetaGuid (Join-Path $module "PrefabsEditable/EXPTW/$prefab.meta")
 Assert ($fixture.Contains('"{' + $meta.Groups[1].Value + '}' + $meta.Groups[2].Value + '"')) "fixture must spawn $prefab by its GUID"
}
Assert ((Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')).Contains("-FixturePath tests/ETW_TimeWeatherGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'tests/GAMEPLAY.md must carry the fixture command'

# Cloud probe (evidence) and cloud blend gate fixtures.
$weatherMeta = Get-MetaGuid (Join-Path $module 'PrefabsEditable/EXPTW/ETW_WeatherTransition.et.meta')
$probeRegex = '\[ETW PROBE RESULT\] checks=[1-9]\d* failures=0 direct=\w+ directAuto=\w+ directEarly=\w+ pin=\w+ kick=\w+ paused=\w+ predicted=\S+ pinHold=\S+ blendFloor=\S+ headHold=\S+ setBlend=\S+ setHold=\S+ queueB=-?\d+ reason=complete'
$blendRegex = '\[ETW BLEND RESULT\] checks=[1-9]\d* failures=0 smooth=1 heading=1 together=1 replace=1 pinned=1 stop=1 automatic=1 foreign=1 path=\S+ early=\S+ paused=\w+ pinJump=\S+ reason=complete'
foreach ($case in @(@($probePath, 'ETW_CloudProbeGameplay.c', 420, $probeRegex), @($blendPath, 'ETW_CloudBlendGameplay.c', 600, $blendRegex))) {
 $text = Read-Text $case[0]
 Assert ($text -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $text -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') "$($case[1]) must keep the runner driver class names"
 Assert ($text.Contains("-FixturePath tests/$($case[1]) -TimeoutSeconds $($case[2]) -OrchestratorSlotGranted -ExpectResult '$($case[3])'")) "$($case[1]) header must carry its runner command"
 Assert ($text -match 'static const float DAY_SECONDS = 1440;' -and $text -match 'SetDayDuration\(DAY_SECONDS\)' -and $text -match 'SetDayDuration\(m_fDayLength\)') "$($case[1]) runs at a 1440 s day and restores the day length"
 Assert ($text -match 'GetGame\(\)\.RequestClose\(\);') "$($case[1]) closes the game after its result"
}
$blend = Read-Text $blendPath
Assert ($blend.Contains('"{' + $weatherMeta.Groups[1].Value + '}' + $weatherMeta.Groups[2].Value + '"') -and $blend -match 'Resource weatherPrefab = Resource\.Load\(WEATHER_MODULE\);') 'blend fixture spawns the Weather Transition prefab by its GUID from a local Resource'
$blendHead = [regex]::Match($blend, 'string head = string\.Format\("(\[ETW BLEND RESULT\][^"]*)"').Groups[1].Value
$blendTail = [regex]::Match($blend, 'string tail = string\.Format\("( foreign=[^"]*)"').Groups[1].Value
Assert ($blendHead -eq '[ETW BLEND RESULT] checks=%1 failures=%2 smooth=%3 heading=%4 together=%5 replace=%6 pinned=%7 stop=%8 automatic=%9' -and $blendTail -eq ' foreign=%1 path=%2 early=%3 paused=%4 pinJump=%5 reason=%6') "blend fixture RESULT format changed: $blendHead$blendTail"
$passing = $blendHead
foreach ($pair in @(@(9, '1'), @(8, '1'), @(7, '1'), @(6, '1'), @(5, '1'), @(4, '1'), @(3, '1'), @(2, '0'), @(1, '35'))) { $passing = $passing.Replace("%$($pair[0])", $pair[1]) }
$passing += $blendTail.Replace('%6', 'complete').Replace('%5', '0.00312').Replace('%4', 'wait').Replace('%3', 'direct@9s').Replace('%2', 'direct').Replace('%1', '1')
Assert ($passing -match $blendRegex -and !($passing.Replace('smooth=1', 'smooth=0') -match $blendRegex) -and !($passing.Replace('pinned=1', 'pinned=0') -match $blendRegex) -and !($passing.Replace('failures=0', 'failures=1') -match $blendRegex)) 'a passing blend RESULT line must match its regex; a snap, a failed pinned case or a failed pin-jump check must not'
# The smooth case runs after the hold ForceWeatherTo leaves (10 s here), so it can take the direct path.
$blendSetup = Get-Body $blend 'void\s+Setup\s*\('
Assert ($blend -match 'static const float HOLD_WAIT = 15;' -and $blendSetup -match 'ForceWeatherTo\(true, m_sA\);\s*m_iPhase = 1;\s*PauseFor\(HOLD_WAIT\);') 'blend gate: the smooth case starts 15 s after ForceWeatherTo (longer than its 10 s hold), so the direct start is what it proves'
$pinnedStart = Get-Body $blend 'void\s+StartPinned\s*\('
Assert ($pinnedStart -match 'ConfigureWeather\(m_sA, 0, 0\);\s*bool first = ETW_WeatherRunner\.StartFromModule\(m_WeatherModule, -1\);\s*ConfigureWeather\(m_sPinnedTarget, 0, 0\);\s*bool second = ETW_WeatherRunner\.StartFromModule\(m_WeatherModule, -1\);' -and (Get-Body $blend 'void\s+SamplePinJump\s*\(') -match 'Check\(m_fPinJump < PIN_JUMP_LIMIT \+ m_fPinDrift,' -and $blend -match 'static const float PIN_JUMP_LIMIT = 0\.05;') 'blend gate: a deterministic pin with rain and fog left to the weather, and a check that they do not jump across it'
Assert ($blend -match 'static const float FIXTURE_SECONDS = 520;' -and $blend -match '-TimeoutSeconds 600 ') 'blend gate deadline 520 s: room for fallbacks, and for boot and shutdown inside the largest runner timeout (600 s)'
$runGameplay = Read-Text (Join-Path $repo 'tests/Run-Gameplay.ps1')
$timeoutRange = [regex]::Match($runGameplay, '\[ValidateRange\((\d+),(\d+)\)\]\[int\]\$TimeoutSeconds')
Assert ($timeoutRange.Success -and 600 -le [int]$timeoutRange.Groups[2].Value -and 420 -ge [int]$timeoutRange.Groups[1].Value) 'the fixture timeouts (420, 600) are within Run-Gameplay''s allowed range'
$autoCase = Get-Body $blend 'void\s+AutomaticWhileBlending\s*\('
Assert ($autoCase -match 'bool started = ETW_WeatherRunner\.StartAutomatic\(m_WeatherModule, -1\);' -and $autoCase -match 'bool kept = ETW_WeatherRunner\.GetTarget\(\) == m_sAutoTarget && ETW_WeatherRunner\.GetCloudStage\(\) == ETW_WeatherRunner\.CLOUD_BLENDING;' -and (Get-Body $blend 'void\s+SampleAutomatic\s*\(') -match 'bool reached = m_bAutoWhileBlending && StateName\(\) == m_sAutoTarget;') 'blend gate: Return to automatic weather while the clouds blend lets that blend finish on its target (kept clouds never jump)'
$probe = Read-Text $probePath
$probeHead = [regex]::Match($probe, 'string head = string\.Format\("(\[ETW PROBE RESULT\][^"]*)"').Groups[1].Value
$probeTail = [regex]::Match($probe, 'string tail = string\.Format\("( predicted=[^"]*)"').Groups[1].Value
Assert ($probeHead -eq '[ETW PROBE RESULT] checks=%1 failures=%2 direct=%3 directAuto=%4 directEarly=%5 pin=%6 kick=%7 paused=%8' -and $probeTail -eq ' predicted=%1 pinHold=%2 blendFloor=%3 headHold=%4 setBlend=%5 setHold=%6 queueB=%7 reason=%8') "probe fixture RESULT format changed: $probeHead$probeTail"
$probeLine = $probeHead.Replace('%8', 'wait').Replace('%7', 'nothing').Replace('%6', '10s').Replace('%5', '7s').Replace('%4', '0s').Replace('%3', 'never').Replace('%2', '0').Replace('%1', '7') + $probeTail.Replace('%8', 'complete').Replace('%7', '1').Replace('%6', '0.001').Replace('%5', '0.001').Replace('%4', '0.167').Replace('%3', '0.167').Replace('%2', '0.167').Replace('%1', '0/7')
Assert ($probeLine -match $probeRegex -and $probeLine.Replace('queueB=1', 'queueB=-1') -match $probeRegex -and !($probeLine.Replace('failures=0', 'failures=1') -match $probeRegex)) 'a complete probe RESULT line must match its regex (setter and identity values are evidence)'
# Setter read-back: a node already queued behind the held weather given the shortest blend and
# hold through the setters; the held direct case records the hold setter and B's queue index.
$setters = Get-Body $probe 'void\s+ProbeSetters\s*\('
Assert ($setters -match 'bool queued = m_Transitions\.EnqueueStateTransition\(m_Node, false\);[\s\S]*m_Node\.SetTransitionDurationHours\(SHORTEST_HOURS\);\s*m_Node\.SetStateDurationHours\(SHORTEST_HOURS\);\s*m_fSetBlend = m_Node\.GetTransitionDurationHours\(\);\s*m_fSetHold = m_Node\.GetStateDurationHours\(\);' -and $setters -match 'm_Node\.SetLooping\(true\);') 'probe: the blend and hold setters are read back on a queued node that never plays'
Assert ((Get-Body $probe 'void\s+QueueDirect\s*\(') -match 'if \(m_iQueueB == -2\)\s*\{\s*m_fHeadHold = headHold;\s*m_iQueueB = QueueIndexOf\(m_Node\);' -and (Get-Body $probe 'void\s+SampleSetters\s*\(') -match 'ProbeSetters\(\);[\s\S]*Finish\("complete"\);' -and (Get-Body $probe 'void\s+SamplePaused\s*\(') -match 'm_iPhase = 14;' -and $probe -match 'else if \(m_iPhase == 14\)\s*SampleSetters\(\);') 'probe: the held direct case records headHold and queueB; the setter read-back runs last, after every timed case'
# Direct cases wait out the hold ForceWeatherTo leaves (10 s here) and watch longer than it;
# the start request is judged within less than the pin's hold.
Assert ($probe -match 'static const float HOLD_WAIT = 12;' -and $probe -match 'static const float DIRECT_SECONDS = 15;' -and $probe -match 'static const float EARLY_WAIT = 3;' -and $probe -match 'static const float KICK_SECONDS = 3;') 'probe timing: 12 s past ForceWeatherTo before a direct case, 15 s to watch it, the early case 3 s in, 3 s for the start request'
Assert ((Get-Body $probe 'void\s+Setup\s*\(') -match 'Normalize\(true\);\s*m_iPhase = 1;\s*PauseFor\(HOLD_WAIT\);' -and (Get-Body $probe 'string\s+DirectVerdict\s*\(') -match 'return whole\.ToString\(\) \+ "s";' -and (Get-Body $probe 'string\s+DirectVerdict\s*\(') -match 'if \(elapsed >= DIRECT_SECONDS\)\s*return "never";' -and (Get-Body $probe 'string\s+KickVerdict\s*\(') -match 'Now\(\) - m_fCaseAt >= KICK_SECONDS') 'probe: direct cases report the seconds a start took (or never) after waiting out the hold; the start request has its own short window'
$sampleDirect = Get-Body $probe 'void\s+SampleDirect\s*\('
Assert ($sampleDirect -match 'Normalize\(false\);[\s\S]*?head\.SetStateDurationHours\(1\);[\s\S]*?m_iPhase = 3;\s*PauseFor\(HOLD_WAIT\);' -and $sampleDirect -match 'Normalize\(true\);\s*m_iPhase = 5;\s*PauseFor\(EARLY_WAIT\);') 'probe: the automatic case waits out the hold with a mission-length hold in place; the early case is queued inside the hold'
'PASS: Time and Weather registered (Weather Transition, Time Skip; names without EXPBG), defaults 10 min and 6 h with fades 2/3/2 s, engine cloud blend (direct after the hold left over, else pinned; one start request; queued blends never shortened below the engine minimum or the requested time; a start request that starts nothing keeps the pin''s plan; never ForceWeatherTo or RemoveStateTransition; empty names refused; deferral leaves the other source alone; pins watched for rain and fog jumps; kept clouds never jump) with rain/fog/wind arriving together, no instant weather previews, foreign-change hooks naming who changed the weather, broadcast fade with rollover at full black, local read-only statuses, layout and persistence wiring; time and weather, cloud probe and cloud blend fixtures wired.'
