#requires -Version 7.0
# Portable guard for the squad attribute fixes of AI Global Skills and AI Surrender
# (Unreleased; the GM reported that Return Fire Only, Warning Shots First and the squad
# surrender chance "didn't really work"):
# - Return Fire Only and armed Warning Shots First follow a strict "fired upon" rule
#   (EGS_Provocation.c) instead of vanilla RETURN_FIRE, which fires once an enemy shot passes
#   within 13 m of the squad leader or goes off within 15 m of him (a hit on another member
#   does not count there): attribute -> override -> applied ROE -> the actual combat mode in
#   SCR_AIGroupUtilityComponent.EvaluateCombatMode -> provoked flag.
# - Vanilla "Set combat mode" and the EXPBG squad ROE agree; the squad's own combat mode
#   survives Unit Caching Full, native saves and session loads.
# - A soldier holding fire under his own ROE stays out of his squad's suppressive fire.
# - Warning shots: vehicle targets with a player, a second shooter, vehicle clearance.
# - AI Surrender: the chance descriptions name the casualty threshold; casualties skipped
#   while a squad's cache state is held are evaluated once it is awake.
# Also checks tests/ESR_SquadAttributesGameplay.c is wired to the runner and its RESULT line
# matches its -ExpectResult regex. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$egs = Join-Path $repo 'addon/ai-global-skills'
$esr = Join-Path $repo 'addon/ai-surrender'
$egsScripts = Join-Path $egs 'Scripts/Game/EXPGS'
$esrScripts = Join-Path $esr 'Scripts/Game/EXPSR'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { Assert (Test-Path -LiteralPath $Path) "missing $Path"; [IO.File]::ReadAllText($Path) }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = $Text.IndexOf($First); $b = $Text.IndexOf($Second)
 return $a -ge 0 -and $b -ge 0 -and $a -lt $b
}

$paths = [ordered]@{
 group = Join-Path $egsScripts 'EGS_Group.c'
 unitRoe = Join-Path $egsScripts 'EGS_UnitRoe.c'
 provocation = Join-Path $egsScripts 'EGS_Provocation.c'
 manager = Join-Path $egsScripts 'EGS_Manager.c'
 combat = Join-Path $egsScripts 'EGS_CombatComponent.c'
 attributes = Join-Path $egsScripts 'EGS_Attributes.c'
 cache = Join-Path $egsScripts 'EGS_OverrideCache.c'
 persistence = Join-Path $egsScripts 'EGS_Persistence.c'
 surrender = Join-Path $esrScripts 'ESR_SurrenderManager.c'
 fixture = Join-Path $repo 'tests/ESR_SquadAttributesGameplay.c'
}
$t = @{}
foreach ($key in $paths.Keys) { $t[$key] = Read-Text $paths[$key] }
$egsList = Read-Text (Join-Path $egs 'Configs/Editor/AttributeLists/Edit.conf')
$esrList = Read-Text (Join-Path $esr 'Configs/Editor/AttributeLists/Edit.conf')
$snapshot = Read-Text (Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG/EBG_CacheSnapshot.c')
$skillsTest = Read-Text (Join-Path $repo 'tests/EGS_SkillsTest.c')

# --- A. Strict "fired upon" rule -------------------------------------------------------------
# The squad attribute is a Game Master write that reaches the applied ROE at once.
$roeWrite = Get-Body $t.attributes '(?s)class\s+EGS_GroupRoeAttribute\s*:\s*SCR_BaseEditorAttribute.*?override\s+void\s+WriteVariable\s*\('
Assert ($roeWrite -match 'EGS_Attributes\.IsEditingGameMaster\(manager, playerID\)' -and $roeWrite -match 'EGS_Manager\.SetGroupRoe\(EGS_Manager\.ResolveGroup\(SCR_EditableEntityComponent\.Cast\(item\)\), var\.GetInt\(\), true\);') 'squad ROE attribute: editing Game Master only, a Game Master save'
$setGroup = Get-Body $t.manager 'static\s+void\s+SetGroupRoe\s*\(\s*SCR_AIGroup\s+group,\s*int\s+value,\s*bool\s+gameMaster\s*=\s*false\s*\)'
Assert ($setGroup -match 'group\.EGS_SetRoeOverride\(value\);' -and $setGroup -match 'group\.EGS_StampGameMasterRoe\(\);' -and $setGroup -match 'ApplyGroup\(group, true, gameMaster\);') 'SetGroupRoe stores the override, stamps a Game Master save and applies at once'
$applyGroup = Get-Body $t.manager 'protected\s+static\s+void\s+ApplyGroup\s*\('
Assert ($applyGroup -match 'group\.EGS_ApplyRoe\(EGS_Settings\.EffectiveRoe\(IsActive\(\), group\.EGS_GetRoeOverride\(\), EGS_Settings\.GetRoe\(\)\), reassert\);') 'ApplyGroup applies the effective ROE (soldier > squad > module) with the reassert flag'
$applyRoe = Get-Body $t.group 'void\s+EGS_ApplyRoe\s*\(\s*int\s+roe,\s*bool\s+reassert\s*=\s*false\s*\)'
Assert ($applyRoe -match '(?s)if \(roe == m_iEGS_AppliedRoe\)\s*\{\s*if \(reassert\)\s*EGS_ReassertMode\(\);\s*return;\s*\}') 'an unchanged ROE re-asserts the EXPBG mode only for a Game Master save'
Assert ($applyRoe -match 'm_bEGS_Provoked = false;' -and $applyRoe -match 'm_iEGS_ProvokeToken\+\+;' -and $applyRoe -match 'utility\.SetCombatMode\(EAIGroupCombatMode\.RETURN_FIRE\);') 'a new ROE starts unprovoked; Return Fire Only and Warning Shots First keep RETURN_FIRE as the external mode'
# The decision point: the group's actual combat mode.
$evaluate = Get-Body $t.unitRoe 'override\s+void\s+EvaluateCombatMode\s*\(\s*\)'
Assert ($evaluate -match 'm_eCombatModeExternal == EAIGroupCombatMode\.RETURN_FIRE && m_Owner && m_Owner\.EGS_UsesStrictReturnFire\(\)' -and $evaluate -match 'm_eCombatModeActual = m_Owner\.EGS_ReturnFireMode\(\);' -and $evaluate -match 'super\.EvaluateCombatMode\(\);') 'EvaluateCombatMode: the strict rule for EXPBG RETURN_FIRE squads, vanilla for everything else'
$strict = Get-Body $t.group 'bool\s+EGS_UsesStrictReturnFire\s*\(\s*\)'
Assert ($strict -match 'if \(!m_bEGS_RoeTouched\)\s*return false;' -and $strict -match 'm_iEGS_AppliedRoe == EGS_Settings\.ROE_RETURN_FIRE' -and $strict -match 'm_iEGS_AppliedRoe == EGS_Settings\.ROE_WARNING_SHOTS && m_iEGS_WarnState != EGS_WARN_LETHAL') 'strict rule: EXPBG Return Fire Only, Warning Shots First until lethal; never an Exempt or vanilla squad'
$returnFire = Get-Body $t.group 'EAIGroupCombatMode\s+EGS_ReturnFireMode\s*\(\s*\)'
Assert ($returnFire -match '(?s)if \(m_bEGS_Provoked\)\s*return EAIGroupCombatMode\.FIRE_AT_WILL;\s*return EAIGroupCombatMode\.HOLD_FIRE;') 'Return Fire Only holds fire until provoked'
$endangered = Get-Body $t.unitRoe 'bool\s+EGS_IsEndangered\s*\('
Assert ($endangered -match '!\(m_Owner && m_Owner\.EGS_UsesStrictReturnFire\(\)\)' -and $endangered -match 'EGS_MAX_CLUSTERS') 'the vanilla rule (Exempt soldiers) still reads the endangered clusters of a strict squad'
$vanillaMode = Get-Body $t.group 'EAIGroupCombatMode\s+EGS_VanillaMode\s*\(\s*\)'
Assert ($vanillaMode -match 'utility\.EGS_IsEndangered\(\)' -and $vanillaMode -notmatch 'EGS_ReturnFireMode') 'Exempt (vanilla) soldier keeps the vanilla return-fire rule'
# Provocation: when, how long, logged once.
$provoke = Get-Body $t.group 'void\s+EGS_Provoke\s*\('
Assert ($provoke -match 'Replication\.IsServer\(\)' -and $provoke -match 'EGS_WatchesProvocation\(\)' -and (Before $provoke 'm_fEGS_ProvokedAt = EGS_Manager.Now();' 'if (m_bEGS_Provoked)') -and (Before $provoke 'return;' 'EGS_Log(') -and $provoke -match 'EGS_Manager\.ScheduleGroup\(this, EGS_Manager\.TIMER_CALM, EGS_Manager\.REARM_DELAY_S, m_iEGS_ProvokeToken\);') 'EGS_Provoke: server, watched squads, extends a running contact, one log and one calm-down timer per contact'
$onTimer = Get-Body $t.group 'void\s+EGS_OnTimer\s*\('
Assert (Before $onTimer 'EGS_Manager.TIMER_CALM' 'token != m_iEGS_Token') 'the calm-down timer is handled before the warning-shot token guard'
$calm = Get-Body $t.group 'protected\s+void\s+EGS_OnCalmTimer\s*\('
Assert ($calm -match 'token != m_iEGS_ProvokeToken' -and $calm -match 'EGS_Manager\.Now\(\) - m_fEGS_ProvokedAt < EGS_Manager\.REARM_DELAY_S \|\| EGS_HasTarget\(\)' -and $calm -match 'EGS_Manager\.REARM_RETRY_S' -and $calm -match 'm_bEGS_Provoked = false;') 'contact over: REARM_DELAY_S after the last provocation and no member with a target, else retried every REARM_RETRY_S'
Assert ($t.manager -match 'static const int TIMER_CALM = 4;' -and $t.manager -match '(?m)^\tstatic float Now\(\)') 'TIMER_CALM on the shared tick; Now() public'
$watch = Get-Body $t.group 'bool\s+EGS_WatchesProvocation\s*\('
Assert ($watch -match 'if \(m_bEGS_OwnRoeMember && EGS_IsManaged\(\)\)' -and $watch -match 'ROE_RETURN_FIRE' -and $watch -match 'ROE_WARNING_SHOTS') 'watched: EXPBG Return Fire Only or Warning Shots First squads and AI squads with such a soldier (never a player''s group: no provocation logs there)'
$applyUnit = Get-Body $t.manager 'protected\s+static\s+void\s+ApplyUnit\s*\('
Assert ($applyUnit -match 'combat\.EGS_SetUnitRoe\(EGS_UnitRoe\.Effective\(unitOverride\)\);' -and $applyUnit -match 'squad\.EGS_SetOwnRoeMember\(true\);') 'a soldier with his own Return Fire Only or Warning Shots First makes his squad watched'
Assert ($applyGroup -match '(?s)if \(!IsActive\(\)\)\s*group\.EGS_SetOwnRoeMember\(false\);') 'no module: nothing is watched'
# The hooks: super first, the result unchanged, decorators kept.
foreach ($reaction in @(@{ c = 'SCR_AIDangerReaction_WeaponFired'; h = 'EGS_Provocation.OnWeaponFired(utility, AIDangerEventWeaponFire.Cast(dangerEvent));' }, @{ c = 'SCR_AIDangerReaction_DamageTaken'; h = 'EGS_Provocation.OnDamageTaken(utility, dangerEvent);' }, @{ c = 'SCR_AIDangerReaction_Explosion'; h = 'EGS_Provocation.OnExplosion(utility, dangerEvent);' })) {
 Assert ($t.provocation -match "\[BaseContainerProps\(\)\]\r?\nmodded class $($reaction.c)\r?\n") "$($reaction.c) keeps its config decorator"
 $body = Get-Body $t.provocation "modded class $($reaction.c)"
 Assert ((Before $body 'bool handled = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);' $reaction.h) -and $body -match 'return handled;') "$($reaction.c): super first, its result returned, then the provocation hook"
}
$lifeState = Get-Body $t.provocation '(?s)modded class SCR_AIGroupUtilityComponent.*?override\s+protected\s+void\s+OnAgentLifeStateChanged\s*\('
Assert ((Before $lifeState 'super.OnAgentLifeStateChanged(' 'EGS_Provocation.OnMemberDown(m_Owner, incapacitatedAgent);') -and $lifeState -match 'lifeState != ECharacterLifeState\.ALIVE') 'a member killed or knocked out is checked after vanilla'
Assert ($t.provocation -match 'static const float NEAR_MISS_RADIUS = 4\.0;' -and $t.provocation -match 'static const float EXPLOSION_RADIUS = 10\.0;') 'near miss 4 m, explosion 10 m'
$nearMiss = Get-Body $t.provocation 'static\s+bool\s+IsNearMiss\s*\('
Assert ($nearMiss -match '(?s)float along = vector\.Dot\(point - muzzle, unit\);\s*if \(along <= 0\)\s*return false;' -and $nearMiss -match 'radius \* radius') 'near miss: only in front of the muzzle, squared distance to the path'
$watching = Get-Body $t.provocation 'static\s+SCR_AIGroup\s+WatchingGroup\s*\('
Assert ($watching -match 'group\.EGS_WatchesProvocation\(\)') 'every hook returns at once for squads nobody watches'
Assert ((Get-Body $t.provocation 'static\s+void\s+OnWeaponFired\s*\(') -match 'IsEnemyOf\(utility, shooter\)' -and (Get-Body $t.provocation 'static\s+void\s+OnDamageTaken\s*\(') -match 'dangerEvent\.GetVictim\(\) != utility\.m_OwnerEntity' -and (Get-Body $t.provocation 'static\s+void\s+OnMemberDown\s*\(') -match 'damage\.GetInstigator\(\)' -and (Get-Body $t.provocation 'static\s+void\s+OnMemberDown\s*\(') -match 'IsEnemyOfGroup\(group, killer\)') 'only enemies provoke: shooter, damage source, killer (a Game Master kill has none)'

# Model of the near-miss rule against vanilla's 13 m flyby radius (vanilla measures it around
# the squad leader only; the EXPBG rule around every member).
$radius = [double]([regex]::Match($t.provocation, 'NEAR_MISS_RADIUS = ([\d.]+);').Groups[1].Value)
function Near([double[]]$Point, [double[]]$Muzzle, [double[]]$Dir, [double]$R, [bool]$FrontOnly) {
 $len = [Math]::Sqrt($Dir[0] * $Dir[0] + $Dir[1] * $Dir[1] + $Dir[2] * $Dir[2])
 $along = 0.0
 for ($k = 0; $k -lt 3; $k++) { $along += ($Point[$k] - $Muzzle[$k]) * ($Dir[$k] / $len) }
 if ($FrontOnly -and $along -le 0) { return $false }
 $squared = 0.0
 for ($k = 0; $k -lt 3; $k++) { $gap = $Muzzle[$k] + ($Dir[$k] / $len) * $along - $Point[$k]; $squared += $gap * $gap }
 return $squared -le $R * $R
}
$muzzle = @(0.0, 1.5, 0.0); $dir = @(0.0, 0.0, 1.0)
Assert ((Near @(10.0, 1.0, 60.0) $muzzle $dir $radius $true) -eq $false -and (Near @(10.0, 1.0, 60.0) $muzzle $dir 13 $false)) 'model: a burst at a target 10 m from the soldier is a vanilla flyby, not an EXPBG near miss'
Assert (Near @(3.0, 1.0, 60.0) $muzzle $dir $radius $true) 'model: a round 3 m from the soldier provokes'
Assert ((Near @(2.0, 1.0, -30.0) $muzzle $dir $radius $true) -eq $false -and (Near @(2.0, 1.0, -30.0) $muzzle $dir $radius $false)) 'model: a shooter firing away from the soldier provokes nothing (only the path in front of the muzzle counts)'

# --- B. Vanilla "Set combat mode" and the EXPBG squad ROE agree ----------------------------
Assert ($t.attributes -match '\[BaseContainerProps\(\), SCR_BaseEditorAttributeCustomTitle\(\)\]\r?\nmodded class SCR_AIGroupCombatModeAttribute') 'modded vanilla combat mode attribute keeps its decorator'
$vanillaWrite = Get-Body $t.attributes '(?s)modded class SCR_AIGroupCombatModeAttribute.*?override\s+void\s+WriteVariable\s*\('
Assert ((Before $vanillaWrite 'super.WriteVariable(item, var, manager, playerID);' 'ConvertIndexToValue(var.GetInt(), written)') -and $vanillaWrite -match '(?s)if \(!manager && playerID == -1\)\s*group\.EGS_AdoptVanillaMode\(\);\s*else if \(EGS_Attributes\.IsEditingGameMaster\(manager, playerID\)\)\s*EGS_Manager\.OnGameMasterCombatMode\(group\);') 'vanilla Set combat mode: vanilla writes first; a session load keeps the EXPBG mode on top; a Game Master save syncs the EXPBG ROE'
$gmMode = Get-Body $t.manager 'static\s+void\s+OnGameMasterCombatMode\s*\('
Assert ($gmMode -match 'IsActive\(\)' -and $gmMode -match 'group\.EGS_IsManaged\(\)' -and (Before $gmMode 'group.EGS_GameMasterRoeThisSave()' 'group.EGS_KeepGameMasterMode()') -and $gmMode -match 'group\.EGS_AdoptVanillaMode\(\);' -and (Before $gmMode 'group.EGS_KeepGameMasterMode()' 'SetGroupRoe(group, EGS_Settings.GROUP_ROE_EXEMPT);')) 'a Game Master Set combat mode on a steered squad becomes its own mode and switches it to Exempt; in the same save as an EXPBG ROE, that ROE stays'
# "The same save" is a save number closed on the next call-queue tick, not world time (which
# stands still during a single-player Game Master pause, so a later save would count as the same).
$stamp = Get-Body $t.group 'void\s+EGS_StampGameMasterRoe\s*\('
$thisSave = Get-Body $t.group 'bool\s+EGS_GameMasterRoeThisSave\s*\('
Assert ($stamp -match 'm_iEGS_RoeSave = EGS_Manager\.OpenGameMasterSave\(\);' -and $thisSave -match 'EGS_Manager\.IsGameMasterSaveOpen\(m_iEGS_RoeSave\)' -and $t.group -notmatch 'GetWorldTime\(\)' -and $t.group -notmatch 'EGS_GameMasterRoeThisFrame') 'same-save test: the save number, never world time'
$openSave = Get-Body $t.manager 'static\s+int\s+OpenGameMasterSave\s*\('
Assert ($openSave -match '(?s)if \(!s_bGameMasterSaveOpen && GetGame\(\)\)\s*\{\s*s_bGameMasterSaveOpen = true;\s*GetGame\(\)\.GetCallqueue\(\)\.CallLater\(CloseGameMasterSave, 0\);' -and (Get-Body $t.manager 'protected\s+static\s+void\s+CloseGameMasterSave\s*\(') -match 's_bGameMasterSaveOpen = false;\s*s_iGameMasterSave\+\+;' -and (Get-Body $t.manager 'protected\s+static\s+bool\s+Ensure\s*\(') -match 'GetCallqueue\(\)\.Remove\(CloseGameMasterSave\);') 'one non-repeating zero-delay call per save closes it; a new world drops it'
$keep = Get-Body $t.group 'bool\s+EGS_KeepGameMasterMode\s*\('
Assert ($keep -match 'if \(!utility \|\| !m_bEGS_RoeTouched\)\s*return false;' -and $keep -match 'm_eEGS_OriginalMode = utility\.GetCombatModeExternal\(\);') 'only a squad EXPBG steers is switched'
$reassert = Get-Body $t.group 'protected\s+void\s+EGS_ReassertMode\s*\('
Assert ($reassert -match 'if \(current == expected\)\s*return;' -and $reassert -match 'm_eEGS_OriginalMode = current;' -and $reassert -match 'utility\.SetCombatMode\(expected\);') 'a mode changed outside EXPBG is kept as the squad''s own and the EXPBG mode put back'
$expected = Get-Body $t.group 'EAIGroupCombatMode\s+EGS_ExpectedMode\s*\('
Assert ($expected -match 'ROE_FIRE_ON_SIGHT' -and $expected -match 'm_iEGS_WarnState == EGS_WARN_LETHAL' -and $expected -match 'return EAIGroupCombatMode\.RETURN_FIRE;') 'expected EXPBG mode per ROE and warning state'
Assert ($egsList -match '(?s)^SCR_EditorAttributeList \{\s*m_aAttributes \+ \{') 'EXPBG attributes are appended after vanilla Set combat mode (one save writes vanilla first)'
$squadDesc = [regex]::Match($egsList, '(?s)  EGS_GroupRoeAttribute \{.*?Description "([^"]+)"').Groups[1].Value
Assert ($squadDesc -match 'within about 4 m' -and $squadDesc -match 'switches this setting to Exempt \(vanilla\)' -and $squadDesc -match 'on foot or in a vehicle') 'squad ROE description: the fired-upon rule, the vanilla sync, vehicle targets'
Assert ([regex]::Match($egsList, '(?s)  EGS_UnitRoeAttribute \{.*?Description "([^"]+)"').Groups[1].Value -match 'suppressive fire included') 'soldier ROE description: no suppressive fire either'

# --- C. The squad's own combat mode across caching, native saves and session loads ---------
$capture = Get-Body $t.cache 'override\s+bool\s+CaptureGroup\s*\(\s*SCR_AIGroup\s+group\s*\)'
Assert ((Before $capture 'bool captured = super.CaptureGroup(group);' 'Settings[EGS_COMBAT_SETTING] = original;') -and $capture -match 'group\.EGS_GetOriginalMode\(original\)' -and $capture -match 'Settings\.IsIndexValid\(EGS_COMBAT_SETTING\)') 'Full snapshot holds the squad''s own combat mode, not the EXPBG one'
Assert ($t.cache -match 'protected static const int EGS_COMBAT_SETTING = 15;') 'combat mode index in the group snapshot'
$settingsInit = [regex]::Match($snapshot, 'Settings = \{(group\.[^}]*)\};').Groups[1].Value -split ','
Assert ($settingsInit.Count -eq 19 -and $settingsInit[15].Trim() -eq 'combat' -and $snapshot -match 'int combat;\s*if \(utility\) combat = utility\.GetCombatModeExternal\(\);' -and $snapshot -match 'utility\.SetCombatMode\(Settings\[15\]\);') 'Unit Caching group snapshot: setting 15 is the external combat mode, written back on wake'
$wake = Get-Body $t.cache '(?s)modded class EBG_CacheGroupSnapshot.*?override\s+bool\s+Apply\s*\(\s*SCR_AIGroup\s+group\s*\)'
Assert ((Before $wake 'group.EGS_AdoptVanillaMode();' 'EGS_Manager.SetGroupRoe(group, m_iEGS_Roe);') -and $wake -notmatch 'm_iEGS_Roe != EGS_Settings\.GROUP_ROE_DEFAULT') 'wake: a steered squad keeps its EXPBG mode over the restored own mode; the ROE (module default included) is applied at once'
$adopt = Get-Body $t.group 'void\s+EGS_AdoptVanillaMode\s*\('
Assert ($adopt -match 'if \(!utility \|\| !m_bEGS_RoeTouched\)\s*return;' -and $adopt -match 'm_eEGS_OriginalMode = utility\.GetCombatModeExternal\(\);' -and $adopt -match 'utility\.SetCombatMode\(EGS_ExpectedMode\(\)\);') 'adopt: the written mode is the squad''s own, the EXPBG mode stays'
$serialize = Get-Body $t.persistence '(?s)modded class SCR_AIGroupUtilityComponentSerializer.*?override\s+protected\s+ESerializeResult\s+Serialize\s*\('
Assert ((Before $serialize 'utility.SetCombatMode(original);' 'ESerializeResult result = super.Serialize(owner, component, context);') -and (Before $serialize 'ESerializeResult result = super.Serialize(owner, component, context);' 'utility.SetCombatMode(current);') -and $serialize -match 'group\.EGS_GetOriginalMode\(original\)') 'native save writes the squad''s own mode and restores the EXPBG one'
$deserialize = Get-Body $t.persistence '(?s)modded class SCR_AIGroupUtilityComponentSerializer.*?override\s+protected\s+bool\s+Deserialize\s*\('
Assert ($deserialize -match '!group\.EGS_IsRoeTouched\(\)' -and (Before $deserialize 'utility.SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);' 'bool loaded = super.Deserialize(owner, component, context);') -and (Before $deserialize 'bool loaded = super.Deserialize(owner, component, context);' 'group.EGS_AdoptVanillaMode();')) 'native load onto a steered squad keeps the loaded mode as its own'
# EXPBG CDF Compat reads these fields (EGS_CDFGetOriginalCombatMode): renaming them breaks it.
Assert ($t.group -match 'protected bool m_bEGS_RoeTouched;' -and $t.group -match 'protected EAIGroupCombatMode m_eEGS_OriginalMode;') 'fields read by EXPBG CDF Compat keep their names'

# --- D. Soldier ROE: no suppressive fire while he holds fire --------------------------------
$suppress = Get-Body $t.unitRoe 'modded class SCR_AISuppressGroupClusterBehavior'
Assert ((Before $suppress 'float priority = super.CustomEvaluate();' 'EGS_GetUnitRoe() == EGS_UnitRoe.FOLLOW') -and $suppress -match '(?s)if \(m_CombatComponent\.GetCombatMode\(\) == EAIGroupCombatMode\.HOLD_FIRE\)\s*return 0;') 'a soldier holding fire under his own ROE never runs his squad''s suppressive fire; others keep vanilla'
$getMode = Get-Body $t.combat 'override\s+EAIGroupCombatMode\s+GetCombatMode\s*\('
Assert ($getMode -match 'group\.EGS_ReturnFireMode\(\)') 'soldier Return Fire Only uses the strict squad rule'

# --- E. Warning shots ------------------------------------------------------------------------
$hostile = Get-Body $t.group 'void\s+EGS_OnHostileSelected\s*\('
Assert ($hostile -match 'm_bEGS_Provoked \|\| !EGS_Manager\.IsPlayerTarget\(target\)' -and $hostile -match 'shooter = EGS_FindWarningShooter\(spotter, target, token\);' -and $hostile -match 'EGS_Log\(string\.Format\("warning shots group=') 'squad warning: player or player vehicle, no warning once fired upon, a second shooter when the spotter cannot fire'
$finder = Get-Body $t.group 'protected\s+SCR_AICombatComponent\s+EGS_FindWarningShooter\s*\('
Assert ($finder -match 'Math\.MinInt\(agents\.Count\(\), EGS_MAX_SHOOTER_TRIES\)' -and $finder -match 'seen\.GetTargetEntity\(\) != target' -and $finder -match 'EGS_GetUnitRoe\(\) != EGS_UnitRoe\.FOLLOW') 'second shooter: bounded, same target, never a soldier with his own ROE'
Assert ((Get-Body $t.combat 'protected\s+void\s+EGS_OnUnitHostileSelected\s*\(') -match 'EGS_Manager\.IsPlayerTarget\(target\)' -and (Get-Body $t.combat 'protected\s+void\s+EGS_OnUnitHostileSelected\s*\(') -match 'group\.EGS_IsProvoked\(\)') 'soldier warning: player or player vehicle, none once his squad is fired upon'
$playerTarget = Get-Body $t.manager 'static\s+bool\s+IsPlayerTarget\s*\('
Assert ($playerTarget -match 'IsPlayerControlled\(target\)' -and $playerTarget -match 'Math\.MinInt\(seats\.Count\(\), MAX_TARGET_SEATS\)' -and $playerTarget -match 'seat\.GetOccupant\(\)') 'player target: bounded seat loop of a vehicle'
Assert ((Get-Body $t.combat 'bool\s+EGS_StartWarningShots\s*\(') -match 'EGS_Manager\.WarningPoint\(owner\.GetOrigin\(\), target\.GetOrigin\(\), EGS_Manager\.TargetClearance\(target\)\)') 'warning burst keeps a vehicle''s size clear'
Assert ($t.manager -match 'static vector WarningOffset\(vector shooter, vector target, bool leftSide, float extraClearance = 0\)' -and $skillsTest -match 'EGS_Manager\.WarningOffset\(shooter, target, true\)') 'WarningOffset stays callable with three arguments (EGS_SkillsTest.c)'
$timer = Get-Body $t.group 'void\s+EGS_OnTimer\s*\('
Assert ($timer -match ': lethal now' -and $timer -match ': re-armed') 'the switch to lethal and the re-arm are logged once each'
Assert ([regex]::Matches($t.group, '\bPrint(Format)?\s*\(').Count -eq 1 -and $t.group -match 'void EGS_Log\(string line\)') 'group logs go through the one EGS_Log line (G4 pin)'

# --- F. AI Surrender ------------------------------------------------------------------------
$chances = @([regex]::Matches($esrList, 'Name "Surrender chance \(%\)" Description "([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
Assert ($chances.Count -eq 3) 'three surrender chance attributes (module, squad, soldier)'
Assert ($chances[0] -match 'Squad casualty threshold' -and $chances[0] -match 'Nothing happens before that') 'module chance: nothing before the squad breaks'
Assert ($chances[1] -match 'Nothing happens until the squad breaks' -and $chances[1] -match 'Squad casualty threshold' -and $chances[1] -match 'not on first contact') 'squad chance: threshold first, 100% is not on contact'
Assert ($chances[2] -match 'Squad casualty threshold' -and $chances[2] -match 'not on first contact') 'soldier chance: threshold first'
$evaluateSurrender = Get-Body $t.surrender 'protected\s+static\s+void\s+Evaluate\s*\('
Assert ($evaluateSurrender -match '(?s)if \(EBG_CacheManager\.IsCacheHeld\(group\)\)\s*\{\s*record\.HeldRecheck = true;\s*ScheduleHeldRecheck\(\);' -and $evaluateSurrender -match 'record\.HeldRecheck = false;') 'a casualty skipped while the cache state is held is remembered'
$recheck = Get-Body $t.surrender 'protected\s+static\s+void\s+RecheckHeld\s*\('
Assert ($recheck -match 'if \(!s_bListening\) return;' -and $recheck -match 'EBG_CacheManager\.IsCacheHeld\(record\.Group\)' -and $recheck -match 'Queue\(record\.Group\);' -and $recheck -match 'if \(waiting\) ScheduleHeldRecheck\(\);') 'held squads are queued again once awake; the check repeats only while such squads exist'
Assert ((Get-Body $t.surrender 'protected\s+static\s+void\s+ScheduleHeldRecheck\s*\(') -match 'CallLater\(ESR_SurrenderManager\.RecheckHeld, HELD_RECHECK_MS, false\);' -and $t.surrender -match 'static const int HELD_RECHECK_MS = 10000;' -and (Get-Body $t.surrender 'static\s+void\s+OnFirstModule\s*\(') -match 'queue\.Remove\(ESR_SurrenderManager\.RecheckHeld\);') 'one shared 10 s one-shot timer, dropped with a new world'

# --- Enforce gotchas, ASCII and LF in the changed and new files -----------------------------
foreach ($key in $paths.Keys) {
 $text = $t[$key]; $path = $paths[$key]
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep|Wait)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|IEntity|Managed)\s+(Node|Faction|Group|set|map|array)\b')) "variable named like a vanilla type in $path"
 Assert (![regex]::IsMatch($text, 'static\s+ref\s+[^;=]+=\s*(new|\{)')) "static initializer in $path (EXPBG_LazyStatics pattern)"
 Assert (!$text.Contains("`r")) "CR line ending in $path"
 Assert ((([IO.File]::ReadAllBytes($path)) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
foreach ($key in 'provocation', 'fixture') {
 Assert (![regex]::IsMatch($t[$key], '(?m)^[ \t]*(static[ \t]+|override[ \t]+|protected[ \t]+)*(int|vector|bool|float|string|IEntity|EAIGroupCombatMode|Faction|SCR_\w+)[ \t]+\w+[ \t]*\([^)\n]*\)[ \t]*\{[ \t]*return')) "non-void returns stay on separate lines: $($paths[$key])"
}

# --- Native fixture wiring (executed only by the orchestrator through Run-Gameplay) ---------
$fixture = $t.fixture
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'fixture keeps the runner driver class names'
$regex = '\[ESR SQUAD ATTR RESULT\] checks=[1-9]\d* failures=0 surrender100=5 surrender0=0 holdShots=0 farBurst=1 provoked=1 warning=1 lethal=1 rearm=1 leak=0 sync=1 cached=1 reason=complete'
Assert ($fixture.Contains("-FixturePath tests/ESR_SquadAttributesGameplay.c -TimeoutSeconds 540 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'fixture header carries its runner command'
$head = [regex]::Match($fixture, 'Print\(string\.Format\("(\[ESR SQUAD ATTR RESULT\][^"]*)"').Groups[1].Value
$tail = [regex]::Match($fixture, 'string tail = string\.Format\("([^"]*)"').Groups[1].Value
Assert ($head -eq '[ESR SQUAD ATTR RESULT] checks=%1 failures=%2 surrender100=%3 surrender0=%4 holdShots=%5 farBurst=%6 provoked=%7 warning=%8 lethal=%9' -and $tail -eq ' rearm=%1 leak=%2 sync=%3 cached=%4 reason=%5') "fixture RESULT format changed: $head|$tail"
function Fill([string]$Format, [string[]]$Values) { $line = $Format; for ($n = $Values.Count; $n -ge 1; $n--) { $line = $line.Replace("%$n", $Values[$n - 1]) }; $line }
function Line([string[]]$A, [string[]]$B) { (Fill $head $A) + (Fill $tail $B) }
$pass = @('80', '0', '5', '0', '0', '1', '1', '1', '1')
Assert ((Line $pass @('1', '0', '1', '1', 'complete')) -match $regex) 'fixture RESULT line matches its -ExpectResult regex'
Assert (!((Line @('80', '1', '5', '0', '0', '1', '1', '1', '1') @('1', '0', '1', '1', 'complete')) -match $regex)) 'a failing RESULT line does not match'
Assert (!((Line @('80', '0', '5', '0', '2', '1', '1', '1', '1') @('1', '0', '1', '1', 'complete')) -match $regex)) 'rounds fired before provocation do not match'
Assert (!((Line $pass @('1', '1', '1', '1', 'complete')) -match $regex)) 'a soldier ROE leak does not match'
Assert (!((Line $pass @('1', '0', '1', '1', 'timeout')) -match $regex)) 'a timeout does not match'
$listMeta = Read-Text (Join-Path $egs 'Configs/Editor/AttributeLists/Edit.conf.meta')
$listGuid = [regex]::Match($listMeta, 'Name "(\{[0-9A-F]{16}\})').Groups[1].Value
Assert ($fixture.Contains("ATTRIBUTE_LIST = `"$listGuid" + 'Configs/Editor/AttributeLists/Edit.conf"')) 'fixture loads the merged vanilla attribute list the EXPBG list extends'
Assert ($fixture -match '(?s)modded class EGS_Attributes\s*\{\s*override static bool IsEditingGameMaster\(SCR_AttributesManagerEditorComponent manager, int playerID\)' -and $fixture -match '(?s)modded class EGS_Manager\s*\{\s*override static bool IsPlayerControlled\(IEntity entity\)' -and $fixture -match '(?s)modded class EGS_Manager\s*\{.*override static bool IsPlayerTarget\(IEntity target\)\s*\{\s*if \(EXPG_GarrisonGameplay\.IsStandIn\(target\)\) return true;\s*return super\.IsPlayerTarget\(target\);') 'fixture seams: stand-in Game Master and stand-in player only (the player check overridden at the entry point production calls, too)'
# Fixture reliability: fire team E (case R) must stay whole until R's last phase.
Assert ($fixture -match '(?s)S100 = SpawnGroup\(USSR_SQUAD, Origin \+ SiteS\);.*SetVanillaMode\(S100, EAIGroupCombatMode\.HOLD_FIRE\);' -and $fixture -match 'S0 = SpawnGroup\(USSR_SQUAD, Origin \+ SiteS0\);\s*SetVanillaMode\(S0, EAIGroupCombatMode\.HOLD_FIRE\);' -and $fixture -match 'V = SpawnGroup\(USSR_SQUAD, Origin \+ SiteV\);\s*SetVanillaMode\(V, EAIGroupCombatMode\.RETURN_FIRE\);') 'fixture: the squads near E never fire at will from spawn'
Assert ($fixture -match 'vector SiteF = "-600 0 600";' -and (Get-Body $fixture 'void\s+EndF\s*\(') -match 'SetVanillaMode\(F, EAIGroupCombatMode\.HOLD_FIRE\);' -and [regex]::Matches((Get-Body $fixture 'void\s+StepF\s*\('), 'FDone = true').Count -eq 1) 'fixture: Fire on Sight squad F fights far from E and holds fire once its case ends'
$stepB = Get-Body $fixture 'void\s+StepB\s*\('
Assert ($stepB -notmatch 'ROE_FIRE_ON_SIGHT' -and (Get-Body $fixture 'void\s+NextB\s*\(') -match 'SetVanillaMode\(S0, EAIGroupCombatMode\.HOLD_FIRE\);') 'fixture: B never leaves S0 in fire at will; a failed step puts it on hold fire'
Assert ($fixture -match 'Watch\(E, EOpenShots, null\);\s*SetVanillaMode\(E, EAIGroupCombatMode\.FIRE_AT_WILL\);' -and $fixture -match 'float eFirst = FirstShot\(EOpenShots\);' -and $fixture -match 'eFirst >= EOpenedAt') 'fixture: "provoked after E''s first round" counts only rounds after E opens fire (not the far burst)'
Assert ($fixture -match 'GmWrite\(SquadRoe, R, EGS_Settings\.ROE_RETURN_FIRE\);' -and $fixture -match 'GmWrite\(VanillaMode, S0, ENTRY_HOLD\);' -and $fixture -match 'GmWrite\(SoldierRoe, Holdout, EGS_Settings\.ROE_RETURN_FIRE\);' -and $fixture -match 'SessionWrite\(SquadSurrender, S100, ESR_Overrides\.ToEntry\(100\)\);') 'fixture writes through the production attribute classes from the list'
Assert ($fixture -match 'Resource resource = Resource\.Load\(prefab\);' -and $fixture -match 'SCR_AISuppressBehavior behavior = new SCR_AISuppressBehavior\(') 'fixture keeps loaded resources and new behaviours in locals'

# --- Optional: EXPBG_RO_AI overrides of the same methods call super first (any load order) ---
$roAi = Join-Path (Split-Path -Parent $repo) 'mod-ro-ai/addon/EXPBG_RO_AI/scripts/Game/AI'
if (Test-Path -LiteralPath $roAi) {
 foreach ($file in 'Reaction/EXPBG_AIDangerReaction_WeaponFired.c', 'Reaction/EXPBG_AIDangerReaction_DamageTaken.c', 'Tactics/EXPBG_AISuppressGroupClusterBehavior.c', 'Components/RO_AIGroupUtilityComponent.c') {
  $roText = Read-Text (Join-Path $roAi $file)
  Assert ($roText -match 'super\.(PerformReaction|CustomEvaluate|OnAgentLifeStateChanged)\(') "EXPBG_RO_AI $file chains to super"
 }
 Assert ((Read-Text (Join-Path $roAi 'Components/RO_AIGroupUtilityComponent.c')) -notmatch 'EvaluateCombatMode') 'EXPBG_RO_AI does not override EvaluateCombatMode'
}
'PASS: squad attributes: Return Fire Only / armed Warning Shots First use the strict fired-upon rule at the actual-mode decision point (hit, kill, 4 m near miss, 10 m explosion; logged once per contact, calm-down on the shared tick), vanilla Set combat mode and the EXPBG ROE agree (Exempt switch, same-save order, re-assert), the squad''s own mode survives Full caching, native saves and session loads, no suppressive fire for a soldier holding fire, warning shots for player vehicles with a second shooter, surrender texts name the threshold and held squads are re-evaluated; fixture wired.'
