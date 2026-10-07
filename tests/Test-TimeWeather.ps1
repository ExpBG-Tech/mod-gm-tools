#requires -Version 7.0
# Portable guard for the Time and Weather module (addon/time-weather, Unreleased): two GM
# Systems entities, "Weather Transition" (blends clouds through the engine's smooth weather
# state transition, steps rain/fog/wind overrides; optional smoothing of Scenario Properties
# weather) and "Time Skip" (one broadcast fade RPC, clock changed at full black with date
# rollover, overlapping skips refused). Checks registration, browser names, defaults,
# clamps, attribute wiring, hooks, layout, persistence configs, Enforce gotchas and the
# native fixture wiring. No engine is launched.
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
$listed = 'ETW_WTargetAttribute','ETW_WMinutesAttribute','ETW_WRainAttribute','ETW_WFogAttribute','ETW_WWindSpeedAttribute','ETW_WWindDirectionAttribute','ETW_WAfterAttribute','ETW_WSmoothAttribute','ETW_WActionAttribute','ETW_WStatusAttribute','ETW_THoursAttribute','ETW_TMinutesAttribute','ETW_TFadeOutAttribute','ETW_THoldAttribute','ETW_TFadeInAttribute','ETW_TTextAttribute','ETW_TShowTimeAttribute','ETW_TIncludeGmAttribute','ETW_TActionAttribute','ETW_TStatusAttribute'
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

# Weather runner: smooth engine transition, never ForceWeatherTo; foreign changes stop it.
$runner = Read-Text (Join-Path $scripts 'ETW_WeatherRunner.c')
Assert ($runner -match 'transitions\.RequestStateTransition\(\)' -and $runner -match 'CreateStateTransition\(s_sTarget, hours, s_fStateHours\)' -and $runner -match 'float hours = s_fDuration \* 24 / DayLength\(manager\);') 'clouds blend through RequestStateTransition with real minutes converted at the day length'
Assert (!($runner -match 'ForceWeatherTo\(')) 'the runner must never call ForceWeatherTo (every call is treated as foreign)'
Assert ($runner -match 'static const int TICK_MS = 500;' -and $runner -match 'float eased = t \* t \* \(3 - 2 \* t\);') 'overrides step every 0.5 s along an ease-in-out curve'
$tick = Get-Body $runner 'protected\s+static\s+void\s+Tick\s*\('
Assert ($tick -match 'float written = WrittenValue\(c, value\);\s*if \(written != s_aLast\[c\]\)\s*\{\s*WriteChannel\(s_Manager, c, value\);\s*s_aLast\[c\] = written;' -and (Get-Body $runner 'protected\s+static\s+void\s+WriteChannel\s*\(') -match 'float written = WrittenValue\(channel, value\);' -and (Get-Body $runner 'static\s+bool\s+Start\s*\(') -match 's_aLast\[c\] = UNWRITTEN;') 'a tick rewrites an override only when the value it would write changed (every start writes afresh)'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+Complete\s*\(') -match 'if \(s_aMode\[c\] == MODE_SET\)\s*WriteChannel\(manager, c, s_aTo\[c\]\);') 'the end writes the exact targets unconditionally'
Assert ((Get-Body $runner 'static\s+bool\s+Start\s*\(') -match 'float live = ReadChannel\(manager, c\);' ) 'a new transition starts from the live values'
Assert ((Get-Body $runner 'protected\s+static\s+void\s+Complete\s*\(') -match 'Snap\(transitions, s_sTarget, s_bHold, s_fStateHours\);') 'clouds that did not finish are set at the end'
Assert ((Get-Body $runner 'static\s+void\s+OnForeignWeather\s*\(') -match 'ReleaseChannel\(s_Manager, c\);') 'a foreign weather change releases the overrides the transition set'
$hooks = Read-Text (Join-Path $scripts 'ETW_Hooks.c')
$force = Get-Body $hooks 'override\s+void\s+ForceWeatherTo\s*\('
Assert ($force.IndexOf('ETW_WeatherRunner.OnForeignWeather();') -ge 0 -and $force.IndexOf('ETW_WeatherRunner.OnForeignWeather();') -lt $force.IndexOf('super.ForceWeatherTo(')) 'ForceWeatherTo stops the transition before the vanilla change'
foreach ($wind in 'DelayedSetWindOverride', 'DelayedOverrideWindSpeed', 'DelayedOverrideWindDirection') { Assert ((Get-Body $hooks "override\s+void\s+$wind\s*\(") -match 'ETW_WeatherRunner\.OnForeignWind\(\);\s*super\.') "$wind must hand wind back before the vanilla call" }
Assert ((Get-Body $hooks 'modded\s+class\s+SCR_WeatherInstantEditorAttribute') -match 'if \(item && var && manager && playerID > 0 && ETW_WeatherRunner\.SmoothVanillaRequest\(var\.GetInt\(\), playerID\)\)\s*return;\s*super\.WriteVariable') 'Scenario Properties smoothing skips previews and restores and otherwise keeps vanilla'
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

# Enforce gotchas in every module source and the fixture; new files are ASCII with LF.
$fixturePath = Join-Path $repo 'tests/ETW_TimeWeatherGameplay.c'
$sources = @(Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File | ForEach-Object FullName) + $fixturePath
foreach ($path in $sources) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
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
foreach ($path in $fixturePath, $PSCommandPath) {
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0 -and !($bytes -contains 13)) "new test file must be ASCII with LF: $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'fixture must keep the runner driver class names'
$regex = '\[ETW RESULT\] checks=[1-9]\d* failures=0 rollover=1 skip=1 broadcasts=3 gradual=1 finished=1 interrupt=1 skipFinish=1 foreign=1 smooth=[01] reason=complete'
Assert ($fixture.Contains("-FixturePath tests/ETW_TimeWeatherGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'fixture header must carry its runner command'
$head = [regex]::Match($fixture, 'string head = string\.Format\("(\[ETW RESULT\][^"]*)"').Groups[1].Value
$tail = [regex]::Match($fixture, 'string tail = string\.Format\("( foreign=[^"]*)"').Groups[1].Value
Assert ($head -eq '[ETW RESULT] checks=%1 failures=%2 rollover=%3 skip=%4 broadcasts=%5 gradual=%6 finished=%7 interrupt=%8 skipFinish=%9' -and $tail -eq ' foreign=%1 smooth=%2 reason=%3') "fixture RESULT format changed: $head$tail"
function Format-Result([string[]]$Head, [string[]]$Tail) {
 $line = $script:head; for ($i = 9; $i -ge 1; $i--) { $line = $line.Replace("%$i", $Head[$i - 1]) }
 $end = $script:tail; for ($i = 3; $i -ge 1; $i--) { $end = $end.Replace("%$i", $Tail[$i - 1]) }
 $line + $end
}
Assert ((Format-Result @('24', '0', '1', '1', '3', '1', '1', '1', '1') @('1', '0', 'complete')) -match $regex) 'a passing RESULT line must match the regex'
Assert (!((Format-Result @('24', '1', '1', '1', '3', '0', '1', '1', '1') @('1', '1', 'complete')) -match $regex)) 'a failing RESULT line must not match the regex'
Assert ($fixture -match 'Resource weatherPrefab = Resource\.Load\(WEATHER_MODULE\);' -and $fixture -match 'Resource skipPrefab = Resource\.Load\(SKIP_MODULE\);') 'fixture must keep Resource.Load results in locals'
foreach ($prefab in 'ETW_WeatherTransition.et', 'ETW_TimeSkip.et') {
 $meta = Get-MetaGuid (Join-Path $module "PrefabsEditable/EXPTW/$prefab.meta")
 Assert ($fixture.Contains('"{' + $meta.Groups[1].Value + '}' + $meta.Groups[2].Value + '"')) "fixture must spawn $prefab by its GUID"
}
Assert ((Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')).Contains("-FixturePath tests/ETW_TimeWeatherGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'tests/GAMEPLAY.md must carry the fixture command'
'PASS: Time and Weather registered (Weather Transition, Time Skip; names without EXPBG), defaults 10 min and 6 h with fades 2/3/2 s, smooth engine cloud transition without ForceWeatherTo, foreign-change hooks, broadcast fade with rollover at full black, local read-only statuses, layout and persistence wiring; fixture wired.'
