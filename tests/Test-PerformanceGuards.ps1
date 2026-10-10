#requires -Version 7.0
# Portable performance guards for every addon script (addon/**/*.c), from the 0.1.15
# performance plan (WP0). They pin today's inventory, so new per-frame or per-instance
# work, world scans, log calls and static initializers cannot appear unnoticed:
#  G1 per-frame hooks           G2 repeating timers (sub-second, unresolved or per-instance)
#  G3 world-scan ratchet        G4 log ratchet, plus gated logs in frame hooks and timer callbacks
#  G5 static initializers       G6 change-gated replication bumps in frame hooks and timer callbacks
# Permitted-set semantics: an allowlisted or pinned entry may shrink or disappear without
# editing this file; anything new, or a count above its pin, fails. Comments and string
# contents are ignored. Keys are 'module/FOLDER/File.c|construct' (Scripts/Game/ left out).
# -Report prints today's inventory in allowlist form for re-pinning. No engine is launched.
param([switch]$Report)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addonRoot = Join-Path $repo 'addon'

# ---------------------------------------------------------------------------------------
# Allowlists: key -> @(permitted sites, reason). A key containing '*' is a wildcard.
# Pins (G3, G4 counts, G5): key -> highest permitted count. Lower them as work lands.
# ---------------------------------------------------------------------------------------
# G1: SetEventMask with a frame/simulate event, frame hook declarations (EOnFrame, EOnPostFrame,
# EOnSimulate, EOnPostSimulate, EOnFixedFrame, EOnPostFixedFrame, OnPrepareControls,
# OnMenuUpdate, UpdateValues), 'void Update(float' and zero-delay repeating CallLater.
$PerFrameAllowed = [ordered]@{
 'ai-surrender/EXPSR/ESR_InterrogationPoint.c|SetEventMask(FRAME)' = @(1, 'interrogation point follows the prisoner head on clients (10 Hz within 20 m, else 1 Hz); a dedicated server has no local entity and follows at 1 Hz, plus the 5 s upkeep')
 'ai-surrender/EXPSR/ESR_InterrogationPoint.c|EOnFrame' = @(1, 'interrogation point follows the prisoner head on clients (10 Hz within 20 m, else 1 Hz); a dedicated server has no local entity and follows at 1 Hz, plus the 5 s upkeep')
 'ai-surrender/EXPSR/ESR_ResultDialog.c|OnMenuUpdate' = @(1, 'runs only while the dialog is open')
 'ambient-civilians/EXPAC/EAC_AmbientModule.c|SetEventMask(FRAME|POSTFRAME)' = @(1, 'server civilian scheduler; EOnInit clears it on clients and outside play mode')
 'ambient-civilians/EXPAC/EAC_AmbientModule.c|EOnPostFrame' = @(1, 'server civilian scheduler, budgeted per frame')
 'ambient-civilians/EXPAC/EAC_AmbientModule.c|EOnFrame' = @(1, 'server civilian scheduler, budgeted per frame')
 'ambient-civilians/EXPAC/EAC_DebugView.c|EOnFrame' = @(1, 'editor manager debug view; draws only with civilian debug on')
 'ambient-sounds/EXPAS/EAS_Activation.c|Update(float)' = @(1, 'not a frame hook: called from the EAS runtime Tick, collects at most every 2 s')
 'garrison/EXPG/EXPG_PatrolControl.c|OnPrepareControls' = @(1, 'required input gate for a possessed or patrolling garrison soldier')
 'intel-items/EXPII/EII_ReadDialog.c|OnMenuUpdate' = @(1, 'runs only while the dialog is open')
 'time-weather/EXPTW/ETW_FadeOverlay.c|CallLater(ETW_FadeOverlay.Update, 0, true)' = @(1, 'runs only during a screen fade')
 'unit-caching/EXPBG/EBG_CacheDebug.c|EOnFrame' = @(1, 'game mode debug panel; draws only with debug on')
 'unit-caching/EXPBG/EBG_CacheVisuals.c|Update(float)' = @(1, 'GM map module, while the map is open')
 'unit-caching/EXPBG/EBG_CacheZone.c|SetEventMask(FRAME)' = @(1, 'per-zone GM visuals; WP3 removes it, then delete this entry')
 'unit-caching/EXPBG/EBG_CacheZone.c|EOnFrame' = @(1, 'per-zone GM visuals; WP3 removes it, then delete this entry')
 'unit-caching/EXPBG/EBG_FullCacheMarkers.c|EOnFrame' = @(1, 'editor manager markers for Game Masters')
 'unit-dialog/EXPUD/EUD_DialogWindow.c|OnMenuUpdate' = @(1, 'runs only while the dialog is open')
 'unit-scripts/EXPUS/EUS_UnitControl.c|OnPrepareControls' = @(2, 'controller override plus its per-unit handler: input gate for scripted units')
}
# G2: repeating CallLater that is sub-second, has an unresolved delay, or arms one timer per
# component/entity instance. Key 'path|callback method'.
$TimerAllowed = [ordered]@{
 'advanced-briefing-map/EXPBM/EBM_BriefingBoardComponent.c|ServerWatchdog' = @(1, '1 s, per board, armed once while a briefing is live')
 'advanced-briefing-map/EXPBM/EBM_BriefingBoardComponent.c|ClientProximityTick' = @(1, '1 s, per board on clients (pending review)')
 'advanced-briefing-map/EXPBM/EBM_BriefingClient.c|Tick' = @(1, '200 ms, briefer only')
 'ai-global-skills/EXPGS/EGS_Manager.c|Tick' = @(1, '100 ms shared manager tick')
 'ambient-civilians/EXPAC/EAC_CivilianVoice.c|UpdateLocalVoices' = @(1, '100 ms, not in the verified set; pending review')
 'ambient-civilians/EXPAC/EAC_PedestrianSpawner.c|DrainActivities' = @(1, '500 ms, gap only')
 'ambient-destruction/EXPAD/EAD_World.c|Tick' = @(1, '100 ms shared world tick')
 'ambient-sounds/EXPAS/EAS_RadioRuntime.c|Tick' = @(1, 'shared tick, 250 ms on clients and 2000 ms on dedicated servers')
 'ambient-sounds/EXPAS/EAS_Runtime.c|Tick' = @(1, 'shared tick, 250 ms')
 'ambient-unrest/EXPAU/EAU_Director.c|Tick' = @(1, '250 ms shared director tick')
 'garrison/EXPG/EXPG_Editor.c|EXPG_WatchWait' = @(1, '1 s, per editor while a Game Master waits for a plan (pending review)')
 'garrison/EXPG/EXPG_GarrisonManager.c|Pump' = @(1, '100 ms shared pump')
 'garrison/EXPG/EXPG_Persistence.c|KickNativeImport' = @(1, '500 ms, load only (pending review)')
 'intel-items/EXPII/EIR_ProgressHud.c|Refresh' = @(1, '100 ms, local HUD during a download (pending review)')
 'intel-items/EXPII/EIR_RackComponent.c|ServerTick' = @(1, '250 ms, per rack during a download (pending review)')
 'random-garrison/EXPGR/EXPG_RandomGarrisonDirector.c|Tick' = @(1, '100 ms shared director tick while busy, re-armed from Wake; idle cadence is 2 s')
 'time-weather/EXPTW/ETW_FadeOverlay.c|Update' = @(1, 'every frame, only during a screen fade (pending review)')
 'time-weather/EXPTW/ETW_WeatherRunner.c|Tick' = @(1, '500 ms shared weather transition tick')
 'unit-caching/EXPBG/EBG_BayonetState.c|EBG_StepBayonetSamplers' = @(1, 'one shared bayonet sampler, 250 ms, ceil(n/4) components per step, each sampled at most once per second')
 'unit-caching/EXPBG/EBG_BayonetState.c|EBG_StepBladeSamplers' = @(1, 'one shared cloth-blade sampler, 250 ms, ceil(n/4) components per step, each sampled at most once per second')
 'unit-caching/EXPBG/EBG_CacheManager.c|Pump' = @(1, '100 ms shared pump')
 'unit-caching/EXPBG/EBG_RHSWeaponRails.c|EBG_StepRailSamplers' = @(1, 'one shared rail sampler, 250 ms, ceil(n/4) components per step, each sampled at most once per second')
 'unit-caching/EXPBG/EBG_RHSWeaponRails.c|EBG_TryRHSRails' = @(1, '50 ms on a client, at most 100 tries per replicated change')
 'unit-scripts/EXPUS/EUS_Manager.c|Pump' = @(1, '250 ms shared pump')
}
# G3: QueryEntitiesBySphere/AABB, GetAIAgents(, GetLoadedAddons(, GetAllHitZones( per file.
# EBG_FullSaveGate reads loaded addons once per world (ReadLoadedAddons). EXPG_GarrisonManager keeps
# 2: CdfLoaded() stays the CDF fallback fixture's override point; CdfWithoutCompanion() caches per mission.
$WorldScanPins = [ordered]@{
 'ai-global-skills/EXPGS/EGS_Manager.c|GetAIAgents' = 1
 'ai-surrender/EXPSR/ESR_AceCaptives.c|GetLoadedAddons' = 1
 'ai-surrender/EXPSR/ESR_SurrenderManager.c|GetAIAgents' = 1
 'ambient-civilians/EXPAC/EAC_AceAnimations.c|GetLoadedAddons' = 1
 'ambient-civilians/EXPAC/EAC_DoorIndex.c|QueryEntitiesBySphere' = 1
 'ambient-civilians/EXPAC/EAC_HomeIndex.c|QueryEntitiesByAABB' = 1
 'ambient-civilians/EXPAC/EAC_SceneIndex.c|QueryEntitiesBySphere' = 1
 'ambient-destruction/EXPAD/EAD_Buildings.c|QueryEntitiesByAABB' = 2
 'ambient-destruction/EXPAD/EAD_BuildingSnapshot.c|QueryEntitiesByAABB' = 2
 'ambient-destruction/EXPAD/EAD_Zone.c|QueryEntitiesBySphere' = 1
 'garrison/EXPG/EXPG_GarrisonManager.c|GetLoadedAddons' = 2
 'garrison/EXPG/EXPG_Snapshot.c|QueryEntitiesByAABB' = 1
 'random-garrison/EXPGR/EXPG_RandomGarrisonCensus.c|QueryEntitiesByAABB' = 1
 'unit-caching/EXPBG/EBG_CacheManager.c|GetAIAgents' = 3
 'unit-caching/EXPBG/EBG_CacheSnapshot.c|GetAIAgents' = 1
 'unit-caching/EXPBG/EBG_FullSaveGate.c|GetLoadedAddons' = 1
 'unit-caching/EXPBG/EBG_SimulationCache.c|GetAllHitZones' = 1
 'unit-caching/EXPBG/EBG_StandaloneConflict.c|GetLoadedAddons' = 1
}
# G4: Print/PrintFormat calls per file.
$PrintPins = [ordered]@{
 'advanced-briefing-map/EXPBM/EBM_BriefingBoardComponent.c' = 8
 'advanced-briefing-map/EXPBM/EBM_Diagnostics.c' = 2
 'ai-global-skills/EXPGS/EGS_AttributeInjection.c' = 2
 'ai-global-skills/EXPGS/EGS_CombatComponent.c' = 2
 'ai-global-skills/EXPGS/EGS_Group.c' = 1
 'ai-global-skills/EXPGS/EGS_Manager.c' = 5
 'ai-global-skills/EXPGS/EGS_OverrideCache.c' = 6
 'ai-global-skills/EXPGS/EGS_Settings.c' = 3
 'ai-global-skills/EXPGS/EGS_SkillOverrides.c' = 5
 'ai-global-skills/EXPGS/EGS_SuppressionGuard.c' = 1
 'ai-surrender/EXPSR/ESR_AceCaptives.c' = 4
 'ai-surrender/EXPSR/ESR_AttributeInjection.c' = 2
 'ai-surrender/EXPSR/ESR_OverrideCache.c' = 3
 'ai-surrender/EXPSR/ESR_Overrides.c' = 3
 'ai-surrender/EXPSR/ESR_SurrenderManager.c' = 7
 'ambient-civilians/EXPAC/EAC_AmbientModule.c' = 17
 'ambient-civilians/EXPAC/EAC_AutoExclusions.c' = 4
 'ambient-civilians/EXPAC/EAC_CivilianActivity.c' = 4
 'ambient-civilians/EXPAC/EAC_CivilianDanger.c' = 2
 'ambient-civilians/EXPAC/EAC_CivilianVoice.c' = 2
 'ambient-civilians/EXPAC/EAC_ExclusionZone.c' = 2
 'ambient-civilians/EXPAC/EAC_ExclusionZoneArea.c' = 1
 'ambient-civilians/EXPAC/EAC_MoveFailureGuard.c' = 1
 'ambient-civilians/EXPAC/EAC_PedestrianCaching.c' = 2
 'ambient-civilians/EXPAC/EAC_PedestrianSpawner.c' = 1
 'ambient-civilians/EXPAC/EAC_RoutineStats.c' = 1
 'ambient-civilians/EXPAC/EAC_SceneIndex.c' = 1
 'ambient-civilians/EXPAC/EAC_SessionLifecycle.c' = 2
 'ambient-civilians/EXPAC/EAC_SessionSettings.c' = 1
 'ambient-civilians/EXPAC/EAC_Themes.c' = 3
 'ambient-civilians/EXPAC/EAC_TrafficDirector.c' = 5
 'ambient-civilians/EXPAC/EAC_TrafficRoute.c' = 1
 'ambient-destruction/EXPAD/EAD_Buildings.c' = 3
 'ambient-destruction/EXPAD/EAD_BuildingSnapshot.c' = 1
 'ambient-destruction/EXPAD/EAD_Zone.c' = 7
 'ambient-sounds/EXPAS/EAS_AmbientModule.c' = 1
 'ambient-sounds/EXPAS/EAS_Diagnostics.c' = 1
 'ambient-sounds/EXPAS/EAS_RadioModule.c' = 2
 'ambient-sounds/EXPAS/EAS_RadioRuntime.c' = 1
 'ambient-unrest/EXPAU/EAU_CrowdCast.c' = 3
 'ambient-unrest/EXPAU/EAU_Director.c' = 4
 'ambient-unrest/EXPAU/EAU_ProtestZone.c' = 5
 'garrison/EXPG/EXPG_Editor.c' = 14
 'garrison/EXPG/EXPG_GarrisonManager.c' = 25
 'garrison/EXPG/EXPG_Persistence.c' = 6
 'garrison/EXPG/EXPG_SaveExclusion.c' = 1
 'garrison/EXPG/EXPG_SaveSafety.c' = 2
 'intel-items/EXPII/EII_EditorAttributes.c' = 2
 'intel-items/EXPII/EII_IntelComponent.c' = 3
 'intel-items/EXPII/EIR_EditorAttributes.c' = 2
 'intel-items/EXPII/EIR_RackComponent.c' = 1
 'no-gm-budget/EXPNB/ENB_GameModeBudgets.c' = 1
 'persistent-battlefield/AI/ScriptedNodes/Soldier/EXPBG_AISelectFireMode.c' = 1
 'persistent-battlefield/GameMode/Components/EXPBG_ReconnectComponent.c' = 5
 'random-garrison/EXPGR/EXPG_RandomGarrisonAttributes.c' = 1
 'random-garrison/EXPGR/EXPG_RandomGarrisonCensus.c' = 1
 'random-garrison/EXPGR/EXPG_RandomGarrisonDirector.c' = 1
 'random-garrison/EXPGR/EXPG_RandomGarrisonModule.c' = 18
 'random-garrison/EXPGR/EXPG_SquadPool.c' = 1
 'time-weather/EXPTW/ETW_Attributes.c' = 2
 'time-weather/EXPTW/ETW_FadeOverlay.c' = 1
 'time-weather/EXPTW/ETW_TimeSkipModule.c' = 4
 'time-weather/EXPTW/ETW_WeatherRunner.c' = 1
 'unit-caching/EXPBG/EBG_BayonetState.c' = 6
 'unit-caching/EXPBG/EBG_CacheCleanup.c' = 72
 'unit-caching/EXPBG/EBG_CacheFullCoordinator.c' = 1
 # Includes the rate-limited soldiers-only summary line (at most once per 300 s per zone).
 'unit-caching/EXPBG/EBG_CacheManager.c' = 19
 'unit-caching/EXPBG/EBG_CacheRegroup.c' = 4
 'unit-caching/EXPBG/EBG_CacheSnapshot.c' = 7
 'unit-caching/EXPBG/EBG_CacheVisuals.c' = 5
 'unit-caching/EXPBG/EBG_CacheZone.c' = 1
 'unit-caching/EXPBG/EBG_DebugChecks.c' = 1
 'unit-caching/EXPBG/EBG_FullCacheGroup.c' = 27
 'unit-caching/EXPBG/EBG_FullSaveGate.c' = 2
 'unit-caching/EXPBG/EBG_MissionGroupCallbacks.c' = 5
 'unit-caching/EXPBG/EBG_MissionPersistence.c' = 9
 'unit-caching/EXPBG/EBG_OptimizerController.c' = 7
 'unit-caching/EXPBG/EBG_PlayerHistory.c' = 1
 'unit-caching/EXPBG/EBG_PrefabFullCache.c' = 2
 'unit-caching/EXPBG/EBG_RHSWeaponRails.c' = 4
 'unit-caching/EXPBG/EBG_SimulationCache.c' = 6
 'unit-caching/EXPBG/EBG_StandaloneConflict.c' = 1
 'unit-caching/EXPBG/EBG_ZoneFeedback.c' = 1
 'unit-caching/EXPBG/EBG_ZonePersistence.c' = 16
 'unit-dialog/EXPUD/EUD_EditableCharacter.c' = 1
 'unit-dialog/EXPUD/EUD_EditorAttributes.c' = 2
 'unit-dialog/EXPUD/EUD_FullCacheCarry.c' = 3
 'unit-scripts/EXPUS/EUS_FullCacheCarry.c' = 3
 'unit-caching/EXPBG/EBG_NeverCache.c' = 1
 'unit-dialog/EXPUD/EUD_Persistence.c' = 3
 'unit-scripts/EXPUS/EUS_Discipline.c' = 2
 'unit-scripts/EXPUS/EUS_Manager.c' = 1
 'unit-scripts/EXPUS/EUS_Replication.c' = 2
 'unit-scripts/EXPUS/EUS_UnitControl.c' = 4
}
# G4: logs inside frame hooks and timer callbacks not under a Debug/Diag/Level/Warned/Logged/Rate
# condition. Key 'path|method|"format prefix"'.
$UngatedPrintAllowed = [ordered]@{
 'ambient-sounds/EXPAS/EAS_RadioRuntime.c|Tick|"[EAS] Radio start failed: event=%1 distance=%2 at' = @(1, 'logs only the first and third failed start of a radio (failures == 1 || failures == 3)')
}
# G5: 'static ref X = new/{...}' initializers per file (0.1.13 Windows compile limit).
$StaticInitializerPins = [ordered]@{
 'ai-global-skills/EXPGS/EGS_Module.c' = 1
 'ai-surrender/EXPSR/ESR_SurrenderModule.c' = 1
 'ambient-civilians/EXPAC/EAC_SchedulerStats.c' = 1
 'ambient-destruction/EXPAD/EAD_BuildingReplay.c' = 1
 'time-weather/EXPTW/ETW_TimeSkipModule.c' = 1
 'time-weather/EXPTW/ETW_WeatherModule.c' = 1
 'unit-caching/EXPBG/EBG_CacheVisuals.c' = 3
 'unit-caching/EXPBG/EBG_CacheZone.c' = 1
 'unit-caching/EXPBG/EBG_OptimizerController.c' = 1
}
$StaticInitializerLimit = 11
# G6: Replication.BumpMe() in a frame hook or timer callback without an earlier '!=' or
# 'changed' in the same body. Key 'path|method'.
$UngatedBumpAllowed = [ordered]@{
}

# ---------------------------------------------------------------------------------------
# Source model: comments and string contents blanked (same length, same lines).
# ---------------------------------------------------------------------------------------
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
function Get-BraceMap([string]$Code) {
 $close = [Collections.Generic.Dictionary[int,int]]::new()
 $stack = [Collections.Generic.Stack[int]]::new()
 foreach ($m in [regex]::Matches($Code, '[{}]')) {
  if ($m.Value -eq '{') { $stack.Push($m.Index) } elseif ($stack.Count) { $close[$stack.Pop()] = $m.Index }
 }
 return ,$close
}
# Index of the ')' matching the '(' at $Open, or -1.
function Get-ParenClose([string]$Code, [int]$Open) {
 $depth = 0
 for ($i = $Open; $i -lt $Code.Length; $i++) {
  if ($Code[$i] -eq '(') { $depth++ } elseif ($Code[$i] -eq ')') { $depth--; if ($depth -eq 0) { return $i } }
 }
 return -1
}
function Split-Arguments([string]$Code, [int]$Open, [int]$Close) {
 $parts = [Collections.Generic.List[string]]::new(); $depth = 0; $start = $Open + 1
 for ($i = $Open + 1; $i -lt $Close; $i++) {
  $c = $Code[$i]
  if ($c -eq '(' -or $c -eq '[' -or $c -eq '{') { $depth++ }
  elseif ($c -eq ')' -or $c -eq ']' -or $c -eq '}') { $depth-- }
  elseif ($c -eq ',' -and $depth -eq 0) { $parts.Add($Code.Substring($start, $i - $start).Trim()); $start = $i + 1 }
 }
 $parts.Add($Code.Substring($start, $Close - $start).Trim())
 return ,$parts
}
function Get-Line([string]$Code, [int]$Index) { [regex]::Matches($Code.Substring(0, $Index), "`n").Count + 1 }
# Declarations named $Name: NAME(...) followed by '{'. Returns body spans (Open/Close braces).
function Get-MethodBodies($Source, [string]$Name) {
 $code = $Source.Code
 foreach ($m in [regex]::Matches($code, '(?<![\w.])' + [regex]::Escape($Name) + '\s*\(')) {
  $open = $m.Index + $m.Length - 1
  $close = Get-ParenClose $code $open
  if ($close -lt 0) { continue }
  $after = [regex]::Match($code.Substring($close + 1), '^\s*(const\s*)?\{')
  if (!$after.Success) { continue }
  $brace = $close + $after.Length
  if (!$Source.Braces.ContainsKey($brace)) { continue }
  [pscustomobject]@{ Name = $Name; Open = $brace; Close = $Source.Braces[$brace] }
 }
}
function Get-Class($Source, [int]$Index) {
 $best = $null
 foreach ($class in $Source.Classes) { if ($class.Open -lt $Index -and $Index -lt $class.Close -and (!$best -or $class.Open -gt $best.Open)) { $best = $class } }
 return $best
}
# Statement header before $Index inside a body: text after the previous ';', '{' or '}'.
function Get-Header([string]$Code, [int]$Floor, [int]$Index) {
 $i = $Index - 1
 while ($i -gt $Floor -and ';{}'.IndexOf($Code[$i]) -lt 0) { $i-- }
 return $Code.Substring($i + 1, $Index - $i - 1)
}
$LogGate = 'Debug|Diag|Level|Warned|Logged|Rate'
function Test-GateHeader([string]$Header) { $Header -cmatch '\b(if|while)\s*\(' -and $Header -cmatch $LogGate }
# True when the statement at $Index sits under an if/while whose condition names a log gate:
# its own braceless prefix, or the header of any enclosing block inside the body.
function Test-Gated([string]$Code, $Body, [int]$Index) {
 if (Test-GateHeader (Get-Header $Code $Body.Open $Index)) { return $true }
 $depth = 0
 for ($j = $Index - 1; $j -gt $Body.Open; $j--) {
  if ($Code[$j] -eq '}') { $depth++ }
  elseif ($Code[$j] -eq '{') {
   if ($depth -gt 0) { $depth-- } elseif (Test-GateHeader (Get-Header $Code $Body.Open $j)) { return $true }
  }
 }
 return $false
}
function Find-Allowance($Table, [string]$Key) {
 if ($Table.Contains($Key)) { return $Table[$Key] }
 foreach ($pattern in $Table.Keys) { if ($pattern.Contains('*') -and $Key -like $pattern) { return $Table[$pattern] } }
 return $null
}
function Add-Site($Inventory, [string]$Key, [string]$Site) {
 if (!$Inventory.Contains($Key)) { $Inventory[$Key] = [Collections.Generic.List[string]]::new() }
 $Inventory[$Key].Add($Site)
}

function New-Source([string]$Path, [string]$Text) {
 $code = Get-Code $Text
 $braces = Get-BraceMap $code
 $classes = foreach ($m in [regex]::Matches($code, '\b(?:modded\s+)?class\s+(\w+)(?:\s*(?::|extends)\s*(\w+))?[^{;]*\{')) {
  $open = $m.Index + $m.Length - 1
  if ($braces.ContainsKey($open)) { [pscustomobject]@{ Name = $m.Groups[1].Value; Base = $m.Groups[2].Value; Open = $open; Close = $braces[$open] } }
 }
 [pscustomobject]@{ Path = $Path; Text = $Text; Code = $code; Braces = $braces; Classes = @($classes) }
}

$frameHooks = 'EOnFrame|EOnPostFrame|EOnSimulate|EOnPostSimulate|EOnFixedFrame|EOnPostFixedFrame|OnPrepareControls|OnMenuUpdate|UpdateValues'
$frameFlags = 'FRAME|POSTFRAME|SIMULATE|POSTSIMULATE|FIXEDFRAME|POSTFIXEDFRAME'
$scanApis = [ordered]@{ QueryEntitiesBySphere = '\bQueryEntitiesBySphere\b'; QueryEntitiesByAABB = '\bQueryEntitiesByAABB\b'; GetAIAgents = '\bGetAIAgents\s*\('; GetLoadedAddons = '\bGetLoadedAddons\s*\('; GetAllHitZones = '\bGetAllHitZones\s*\(' }

function Get-Inventory($Sources) {
 $inv = @{ PerFrame = [ordered]@{}; Timers = [ordered]@{}; TimerNotes = @{}; Scans = [ordered]@{}; Prints = [ordered]@{}
  UngatedPrints = [ordered]@{}; Statics = [ordered]@{}; ConstArrays = [ordered]@{}; UngatedBumps = [ordered]@{} }
 foreach ($source in $Sources) {
  $code = $source.Code; $path = $source.Path
  $hotBodies = [Collections.Generic.List[object]]::new()
  # G1: frame event masks, frame hook declarations, Update(float) and zero-delay repeats.
  foreach ($m in [regex]::Matches($code, '\bSetEventMask\s*\(')) {
   $open = $m.Index + $m.Length - 1; $close = Get-ParenClose $code $open
   if ($close -lt 0) { continue }
   $flags = @([regex]::Matches($code.Substring($open, $close - $open), "EntityEvent\.($frameFlags)\b") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
   if ($flags.Count) { Add-Site $inv.PerFrame "$path|SetEventMask($($flags -join '|'))" ("line " + (Get-Line $code $m.Index)) }
  }
  foreach ($m in [regex]::Matches($code, "\bvoid\s+($frameHooks)\s*\(")) {
   Add-Site $inv.PerFrame "$path|$($m.Groups[1].Value)" ("line " + (Get-Line $code $m.Index))
   foreach ($body in Get-MethodBodies $source $m.Groups[1].Value) { if ($body.Open -gt $m.Index -and !($hotBodies | Where-Object Open -EQ $body.Open)) { $hotBodies.Add($body) } }
  }
  foreach ($m in [regex]::Matches($code, '\bvoid\s+Update\s*\(\s*float\b')) { Add-Site $inv.PerFrame "$path|Update(float)" ("line " + (Get-Line $code $m.Index)) }
  # G2: repeating timers. The delay is a literal or a 'const int NAME = N' of the same file.
  foreach ($m in [regex]::Matches($code, '\bCallLater\s*\(')) {
   $open = $m.Index + $m.Length - 1; $close = Get-ParenClose $code $open
   if ($close -lt 0) { continue }
   $arguments = Split-Arguments $code $open $close
   if ($arguments.Count -lt 3 -or $arguments[2] -cne 'true') { continue }
   $callback = $arguments[0]; $delayText = $arguments[1]
   $method = ($callback -split '\.')[-1].Trim()
   $delay = $null
   if ($delayText -match '^\d+$') { $delay = [int]$delayText }
   elseif ($delayText -match '^(?:\w+\.)?(\w+)$') {
    $constant = [regex]::Match($code, '\bconst\s+int\s+' + $Matches[1] + '\s*=\s*(\d+)\s*;')
    if ($constant.Success) { $delay = [int]$constant.Groups[1].Value }
   }
   # One timer per component or entity instance: a bare, non-static method of such a class.
   $class = Get-Class $source $m.Index
   $perInstance = $false
   if ($class -and $callback -match '^(this\.)?\w+$' -and ($class.Name -match '(Component|Entity|Character)$' -or $class.Base -match '(Component|Entity|Character)$')) {
    $classCode = $code.Substring($class.Open, $class.Close - $class.Open)
    $perInstance = ![regex]::IsMatch($classCode, '\bstatic\b[^;{}()]*\b' + [regex]::Escape($method) + '\s*\(')
   }
   $line = Get-Line $code $m.Index
   $reasons = @()
   if ($null -eq $delay) { $reasons += "delay '$delayText' unresolved" } elseif ($delay -lt 1000) { $reasons += "every $delay ms" }
   if ($perInstance) { $reasons += "one timer per $($class.Name) instance" }
   if ($reasons) {
    Add-Site $inv.Timers "$path|$method" "line $line ($($reasons -join ', '))"
    $inv.TimerNotes["$path|$method"] = "CallLater($callback, $delayText, true): $($reasons -join ', ')"
   }
   if ($delay -eq 0) { Add-Site $inv.PerFrame "$path|CallLater($callback, 0, true)" "line $line" }
   foreach ($body in Get-MethodBodies $source $method) { if (!($hotBodies | Where-Object Open -EQ $body.Open)) { $hotBodies.Add($body) } }
  }
  # G3: world scans per file and API.
  foreach ($api in $scanApis.Keys) {
   $count = [regex]::Matches($code, $scanApis[$api]).Count
   if ($count) { $inv.Scans["$path|$api"] = $count }
  }
  # G4: log calls per file; inside frame hooks and timer callbacks they must be gated.
  $printCount = [regex]::Matches($code, '\bPrint(Format)?\s*\(').Count
  if ($printCount) { $inv.Prints[$path] = $printCount }
  foreach ($body in $hotBodies) {
   $bodyCode = $code.Substring($body.Open, $body.Close - $body.Open)
   foreach ($m in [regex]::Matches($bodyCode, '\bPrint(Format)?\s*\(')) {
    $at = $body.Open + $m.Index
    if (Test-Gated $code $body $at) { continue }
    $label = [regex]::Match($source.Text.Substring($at), '^\w+\s*\(\s*("(?:\\.|[^"\\\r\n])*")?').Groups[1].Value
    if ($label.Length -gt 50) { $label = $label.Substring(0, 50) }
    Add-Site $inv.UngatedPrints "$path|$($body.Name)|$label" ("line " + (Get-Line $code $at))
   }
   # G6: a replication bump in a hot body must follow a change test in the same body.
   foreach ($m in [regex]::Matches($bodyCode, '\bReplication\.BumpMe\s*\(')) {
    if ($bodyCode.Substring(0, $m.Index) -match '!=|changed') { continue }
    Add-Site $inv.UngatedBumps "$path|$($body.Name)" ("line " + (Get-Line $code ($body.Open + $m.Index)))
   }
  }
  # G5: static collection initializers and static const arrays.
  $staticCount = [regex]::Matches($code, 'static\s+ref\s+[^;=]+=\s*(new|\{)').Count
  if ($staticCount) { $inv.Statics[$path] = $staticCount }
  foreach ($m in [regex]::Matches($code, 'static\s+const\s+array<')) { Add-Site $inv.ConstArrays $path ("line " + (Get-Line $code $m.Index)) }
 }
 return $inv
}

# Permitted-set check of an inventory (key -> list of sites) against an allowlist.
function Test-Inventory($Failures, [string]$Guard, $Inventory, $Table, [string]$Advice) {
 foreach ($key in $Inventory.Keys) {
  $sites = $Inventory[$key]
  $allowance = Find-Allowance $Table $key
  if (!$allowance) { $Failures.Add("$Guard new entry '$key' at $($sites -join ', '). $Advice"); continue }
  if ($sites.Count -gt $allowance[0]) { $Failures.Add("$Guard '$key' has $($sites.Count) sites (permitted $($allowance[0])): $($sites -join ', ').") }
 }
}
function Test-Pins($Failures, [string]$Guard, $Counts, $Pins, [string]$What, [string]$Advice) {
 foreach ($key in $Counts.Keys) {
  $pin = 0; if ($Pins.Contains($key)) { $pin = $Pins[$key] }
  if ($Counts[$key] -gt $pin) { $Failures.Add("$Guard '$key' has $($Counts[$key]) $What (pinned $pin). $Advice") }
 }
}
function Get-GuardFailures($Inv) {
 $failures = [Collections.Generic.List[string]]::new()
 Test-Inventory $failures 'G1' $Inv.PerFrame $PerFrameAllowed 'Per-frame work needs a reason in $PerFrameAllowed; prefer a budgeted timer or an event.'
 Test-Inventory $failures 'G2' $Inv.Timers $TimerAllowed 'Repeat at 1000 ms or more from one shared (static or manager) callback, or add a reason to $TimerAllowed.'
 Test-Pins $failures 'G3' $Inv.Scans $WorldScanPins 'world scans' 'Reuse one scan per pass or cache the result.'
 Test-Pins $failures 'G4' $Inv.Prints $PrintPins 'Print/PrintFormat calls' 'Gate new logs behind debug or rate-limit them.'
 Test-Inventory $failures 'G4' $Inv.UngatedPrints $UngatedPrintAllowed "A log in a frame hook or timer callback must sit under an if/while naming $LogGate."
 $total = ($Inv.Statics.Values | Measure-Object -Sum).Sum
 if ($total -gt $StaticInitializerLimit) { $failures.Add("G5 $total static collection initializers (limit $StaticInitializerLimit). Create collections on first use (EXPBG_LazyStatics_<Class>).") }
 Test-Pins $failures 'G5' $Inv.Statics $StaticInitializerPins 'static collection initializers' 'Create them on first use (EXPBG_LazyStatics_<Class>).'
 foreach ($key in $Inv.ConstArrays.Keys) { $failures.Add("G5 static const array in '$key' at $($Inv.ConstArrays[$key] -join ', '). Build it on first use instead.") }
 Test-Inventory $failures 'G6' $Inv.UngatedBumps $UngatedBumpAllowed 'Bump only when a replicated value changed (compare with != first).'
 return ,$failures
}

# Self-test: a synthetic component with one violation per guard, next to the compliant forms
# (gated log, change-gated bump, one-shot timer, commented code, string text) that must pass.
$probe = @'
// CallLater(Commented, 10, true); override void EOnFrame(IEntity owner, float timeSlice) {}
class EST_ProbeComponentClass : ScriptComponentClass {}
class EST_ProbeComponent : ScriptComponent
{
 static const int FAST_MS = 200;
 static ref array<int> s_Probe = {};
 static const array<int> PROBE_LIST = {1, 2};
 protected int m_Value;
 protected int m_Sent;
 protected int m_DebugLevel;
 override void EOnInit(IEntity owner)
 {
  SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
  GetGame().GetCallqueue().CallLater(Sample, 1000, true);
  GetGame().GetCallqueue().CallLater(Fast, FAST_MS, true);
  GetGame().GetCallqueue().CallLater(Shared, m_Value, true);
  GetGame().GetCallqueue().CallLater(Once, 10, false);
  GetGame().GetCallqueue().CallLater(Calm, 2000, true);
  Print("CallLater(Hidden, 10, true) QueryEntitiesBySphere", LogLevel.NORMAL);
  GetGame().GetWorld().QueryEntitiesBySphere(vector.Zero, 1, null);
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_DebugLevel > 0) Print("gated braceless");
  if (m_DebugLevel > 1) { PrintFormat("gated block %1", 1); }
  Print("ungated frame log");
 }
 protected void Sample()
 {
  Replication.BumpMe();
 }
 protected void Fast()
 {
  if (m_Value != m_Sent) { m_Sent = m_Value; Replication.BumpMe(); }
 }
 static void Shared() {}
 static void Calm() {}
 protected void Once() {}
}
'@
$probePath = 'selftest/EXPST/EST_Probe.c'
$probeFailures = Get-GuardFailures (Get-Inventory @(New-Source $probePath $probe))
$expected = @(
 "G1 new entry '$probePath|SetEventMask(FRAME)'", "G1 new entry '$probePath|EOnFrame'",
 "G2 new entry '$probePath|Sample' at line 14 (one timer per EST_ProbeComponent instance)",
 "G2 new entry '$probePath|Fast' at line 15 (every 200 ms, one timer per EST_ProbeComponent instance)",
 "G2 new entry '$probePath|Shared' at line 16 (delay 'm_Value' unresolved)",
 "G3 '$probePath|QueryEntitiesBySphere' has 1 world scans", "G4 '$probePath' has 4 Print/PrintFormat calls",
 "G4 new entry '$probePath|EOnFrame|`"ungated frame log`"'", "G5 '$probePath' has 1 static collection initializers",
 "G5 static const array in '$probePath'", "G6 new entry '$probePath|Sample'"
)
foreach ($needle in $expected) {
 if (!($probeFailures | Where-Object { $_.StartsWith($needle) })) { throw "FAIL: guard self-test missed: $needle`nGot:`n$($probeFailures -join "`n")" }
}
if ($probeFailures.Count -ne $expected.Count) { throw "FAIL: guard self-test reported unexpected entries:`n$($probeFailures -join "`n")" }

# tools/Measure-LogRate.ps1 on a synthetic ten-minute console.log that crosses midnight:
# [NOISY #] logs 10 lines/min (flagged), [QUIET] 1 line/min, untagged engine lines 20/min
# (reported, not flagged), plus one debug-check mismatch (always a problem).
$measure = Join-Path $repo 'tools/Measure-LogRate.ps1'
function New-LogLines($Events) {
 @('---------------------------------------------', 'Log console.log started at 2026-10-07 23:57:00 (synthetic)') + @($Events | Sort-Object { $_[0] } | ForEach-Object {
  $clock = (86220 + $_[0]) % 86400
  '{0:00}:{1:00}:{2:00}.{3:000} {4}' -f [int][Math]::Floor($clock / 3600), [int][Math]::Floor($clock % 3600 / 60), [int][Math]::Floor($clock % 60), [int](($clock % 1) * 1000), $_[1]
  if ($_[1].Contains('[NOISY') -and $_[0] -eq 306) { '   continued detail of the same record' }
 })
}
$events = [Collections.Generic.List[object]]::new()
for ($t = 0; $t -lt 600; $t += 6) { $events.Add(@($t, 'SCRIPT       : [NOISY 12] tick')) }
for ($t = 0; $t -lt 600; $t += 60) { $events.Add(@(($t + 1), 'SCRIPT       : [QUIET] minute')) }
for ($t = 2; $t -lt 600; $t += 3) { $events.Add(@($t, 'ENGINE       : untagged engine line')) }
$mismatchEvent = @(420.5, 'SCRIPT    (W): [EBG DEBUGCHECK MISMATCH] rows 5')
$groups = @(& $measure -Lines (New-LogLines (@($events) + , $mismatchEvent)) -PassThru)
$noisy = $groups | Where-Object Tag -EQ '[NOISY #]'
$quiet = $groups | Where-Object Tag -EQ '[QUIET]'
$engine = $groups | Where-Object Tag -EQ '(untagged ENGINE)'
$mismatchGroup = $groups | Where-Object Tag -EQ '[EBG DEBUGCHECK MISMATCH]'
if (!$noisy -or !$noisy.Flagged -or $noisy.Records -ne 100 -or $noisy.Lines -ne 101 -or $noisy.PerMinute -lt 10 -or $noisy.PerMinute -gt 10.3) { throw "FAIL: Measure-LogRate missed the noisy tag or the midnight wrap: $($noisy | Out-String)" }
if (!$quiet -or $quiet.Flagged -or $quiet.Records -ne 10) { throw "FAIL: Measure-LogRate flagged or lost the quiet tag: $($quiet | Out-String)" }
if (!$engine -or $engine.Flagged -or $engine.Records -ne 200) { throw "FAIL: Measure-LogRate flagged or lost untagged engine lines: $($engine | Out-String)" }
if (!$mismatchGroup -or $mismatchGroup.Warnings -ne 1) { throw 'FAIL: Measure-LogRate lost the debug-check mismatch line.' }
$quietOnly = New-LogLines @($events | Where-Object { $_[1].Contains('[QUIET]') })
$null = & $measure -Lines $quietOnly -FailOnFlag
$refused = $false
try { $null = & $measure -Lines (New-LogLines (@($events | Where-Object { $_[1].Contains('[QUIET]') }) + , $mismatchEvent)) -FailOnFlag } catch { $refused = $_.Exception.Message -match 'Log rate check failed: 1 problem' }
if (!$refused) { throw 'FAIL: Measure-LogRate -FailOnFlag accepted a debug-check mismatch.' }

# Index cross-checks: production never enables them; the unit-caching fixtures do and
# require zero mismatches; the soak times with them off and counts mismatches with them on.
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
$debugChecks = [IO.File]::ReadAllText((Join-Path $addonRoot 'unit-caching/Scripts/Game/EXPBG/EBG_DebugChecks.c'))
foreach ($needle in 'class EBG_DebugChecks', 'static bool Enabled;', 'static int Mismatches;', 'static void Mismatch(string what)', 'Mismatches++;', '"[EBG DEBUGCHECK MISMATCH] "') {
 Assert $debugChecks.Contains($needle) "EBG_DebugChecks lost: $needle"
}
foreach ($file in Get-ChildItem -LiteralPath $addonRoot -Recurse -Filter '*.c' -File) {
 Assert (![regex]::IsMatch((Get-Code ([IO.File]::ReadAllText($file.FullName))), 'EBG_DebugChecks\.Enabled\s*=')) "addon code must never set EBG_DebugChecks.Enabled (fixtures only): $($file.Name)"
}
$fixtureResults = [ordered]@{
 'EBG_UnitCleanupGameplay.c' = '[EBG CLEANUP TEST RESULT]'; 'EBG_LocalCacheGameplay.c' = '[EBG LOCAL CACHE RESULT]'
 'EBG_WakeBudgetGameplay.c' = '[EBG WAKE TEST RESULT]'; 'EBG_CacheRecoveryGameplay.c' = '[EBG RECOVERY TEST RESULT]'
 'EUS_CacheHoldGameplay.c' = '[EUS CACHE HOLD RESULT]'; 'EUD_DialogCacheGameplay.c' = '[EXPG DIALOG CACHE RESULT]'
}
foreach ($name in $fixtureResults.Keys) {
 $fixture = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name))
 $init = Get-Code $fixture
 Assert ([regex]::IsMatch($init, '(?s)override\s+void\s+EOnInit\s*\([^)]*\)\s*\{\s*if\s*\(!Replication\.IsServer\(\)\)\s*\{[^}]*\}\s*EBG_DebugChecks\.Enabled\s*=\s*true;')) "$name must enable the index cross-checks at driver start"
 Assert $fixture.Contains('Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");') "$name must fail on any index mismatch"
 Assert ([regex]::IsMatch($fixture, 'PrintFormat\("' + [regex]::Escape($fixtureResults[$name]) + '[^"]* mismatches=%\d",[^;]*EBG_DebugChecks\.Mismatches\);')) "$name RESULT line must carry mismatches="
 Assert ([regex]::IsMatch($fixture, "-ExpectResult '[^']*mismatches=0[^']*'") -or $name -eq 'EBG_UnitCleanupGameplay.c') "$name runner command must require mismatches=0"
}
$soak = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'EBG_PerfSoakGameplay.c'))
foreach ($needle in @(
 "-FixturePath tests/EBG_PerfSoakGameplay.c -ExpectResult '\[EBG PERF SOAK RESULT\] checks=[1-9]\d* failures=0 mismatches=0 reason=complete' -TimeoutSeconds 600",
 'class EXPG_GarrisonGameplayClass : GenericEntityClass {}', 'class EXPG_GarrisonGameplay : GenericEntity',
 '"[EBG PERF] pumps=%1 pump_ms_total=%2 pump_ms_max=%3 tick_ms_total=%4 tick_ms_max=%5 transfer_calls=%6 transfer_ms_total=%7 mismatches=%8',
 '"[EBG PERF SOAK RESULT] checks=%1 failures=%2 mismatches=%3 reason=%4"', 'override protected void UpdatePlayers()', 'override void Pump()', 'override void Tick()',
 'override void CheckGroupTransfers(EBG_CacheGroup record)', 'modded class EXPG_GarrisonManager', 'override protected void Pump()',
 'EBG_DebugChecks.Enabled = false;', 'EBG_DebugChecks.Enabled = true;', 'TryMoveItemToStorage(', 'Resource resource = Resource.Load(prefab);')) {
 Assert $soak.Contains($needle) "soak fixture lost: $needle"
}
# Enforce gotchas in the new sources.
foreach ($path in @((Join-Path $addonRoot 'unit-caching/Scripts/Game/EXPBG/EBG_DebugChecks.c'), (Join-Path $PSScriptRoot 'EBG_PerfSoakGameplay.c'))) {
 $code = Get-Code ([IO.File]::ReadAllText($path))
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($code, 'Math\.RandomFloat\b|\bif\s*\([^;{}]*\)\s*return\s+[^;\s][^;]*;')) "RandomFloat or a one-line conditional value return in $path"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "static collection initializer in $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "non-ASCII byte or CR in $path"
}

# The addon.
$sources = foreach ($file in Get-ChildItem -LiteralPath $addonRoot -Recurse -Filter '*.c' -File | Sort-Object FullName) {
 New-Source $file.FullName.Substring($addonRoot.Length + 1).Replace('\', '/').Replace('/Scripts/Game/', '/') ([IO.File]::ReadAllText($file.FullName))
}
if (@($sources).Count -lt 100) { throw "FAIL: expected the addon scripts under $addonRoot" }
$inventory = Get-Inventory $sources

if ($Report) {
 function Show-Table([string]$Name, $Entries, $Table, [switch]$Pins) {
  "`$$Name = [ordered]@{"
  foreach ($key in $Entries.Keys) {
   if ($Pins) { " '$key' = $($Entries[$key])"; continue }
   $allowance = Find-Allowance $Table $key
   $reason = 'TODO'; if ($allowance) { $reason = $allowance[1] }
   $note = ''; if ($inventory.TimerNotes.ContainsKey($key)) { $note = "  # $($inventory.TimerNotes[$key])" }
   " '$key' = @($($Entries[$key].Count), '$($reason.Replace("'", "''"))')$note"
  }
  '}'
 }
 Show-Table 'PerFrameAllowed' $inventory.PerFrame $PerFrameAllowed
 Show-Table 'TimerAllowed' $inventory.Timers $TimerAllowed
 Show-Table 'WorldScanPins' $inventory.Scans $WorldScanPins -Pins
 Show-Table 'PrintPins' $inventory.Prints $PrintPins -Pins
 Show-Table 'UngatedPrintAllowed' $inventory.UngatedPrints $UngatedPrintAllowed
 Show-Table 'StaticInitializerPins' $inventory.Statics $StaticInitializerPins -Pins
 Show-Table 'UngatedBumpAllowed' $inventory.UngatedBumps $UngatedBumpAllowed
 "# static const array<: $($inventory.ConstArrays.Count) file(s)"
 return
}

$failures = Get-GuardFailures $inventory
if ($failures.Count) { throw ("FAIL: performance guards`n - " + ($failures -join "`n - ")) }
$timerCount = ($inventory.Timers.Values | ForEach-Object Count | Measure-Object -Sum).Sum
$staticCount = ($inventory.Statics.Values | Measure-Object -Sum).Sum
"PASS: performance guards G1-G6 over $(@($sources).Count) addon scripts (self-test caught all $($expected.Count) probe violations; Measure-LogRate self-check, debug-check and soak fixture wiring passed): $($inventory.PerFrame.Count) per-frame hooks, $timerCount allowlisted timer sites, $($inventory.Scans.Count) world-scan and $($inventory.Prints.Count) log pins, $staticCount static initializers. Native engines were not launched."
