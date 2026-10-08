#requires -Version 7.0
# Portable guard for the per-squad and per-soldier overrides (Unreleased):
# - AI Surrender: surrender chance and the reveal squad / identity / intel interrogation
#   chances on AI squads and AI soldiers ("EXPBG Surrender & Intel" tab), "use the module
#   setting" by default; soldier > squad > module, resolved at decision time; the squad's
#   values cached per group against the module settings revision; prisoners take their
#   squad's values at surrender and keep their own on them.
# - AI Global Skills: the squad ROE moved to an "EXPBG Rules of Engagement" tab (renamed
#   "EXPBG AI Skill & ROE" when the squad and soldier skill joined it) with a new per-soldier
#   ROE; the soldier answers the vanilla combat-mode query himself.
# Visibility without a module, the squad and soldier skill and the script fallback for the
# attribute registration are pinned by Test-SquadSoldierAttributes.ps1.
# - No replication for the new values; native saves (ESR_OverridesState, EGS state
#   version 2 with version 1 compatibility), attribute-based saves (serializable / saved
#   attributes), Unit Caching and Garrison Full caching (survivor carry, group snapshot,
#   portable snapshot keys read only when present); bounded loops; diagnostics lines.
# Also checks tests/ESR_OverrideGameplay.c is wired to the runner and its RESULT line
# matches its -ExpectResult regex. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$esr = Join-Path $repo 'addon/ai-surrender'
$egs = Join-Path $repo 'addon/ai-global-skills'
$esrScripts = Join-Path $esr 'Scripts/Game/EXPSR'
$egsScripts = Join-Path $egs 'Scripts/Game/EXPGS'
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

$paths = [ordered]@{
 overrides = Join-Path $esrScripts 'ESR_Overrides.c'
 esrAttributes = Join-Path $esrScripts 'ESR_OverrideAttributes.c'
 esrPersistence = Join-Path $esrScripts 'ESR_OverridePersistence.c'
 esrCache = Join-Path $esrScripts 'ESR_OverrideCache.c'
 unitRoe = Join-Path $egsScripts 'EGS_UnitRoe.c'
 egsCache = Join-Path $egsScripts 'EGS_OverrideCache.c'
 fixture = Join-Path $repo 'tests/ESR_OverrideGameplay.c'
}
$newFiles = @($paths.Values)
$text = @{}
foreach ($key in $paths.Keys) { Assert (Test-Path -LiteralPath $paths[$key]) "missing $($paths[$key])"; $text[$key] = Read-Text $paths[$key] }
$settings = Read-Text (Join-Path $esrScripts 'ESR_Settings.c')
$manager = Read-Text (Join-Path $esrScripts 'ESR_SurrenderManager.c')
$esrList = Read-Text (Join-Path $esr 'Configs/Editor/AttributeLists/Edit.conf')
$esrPersistConf = Read-Text (Join-Path $esr 'Configs/Systems/Persistence/GameMode/GameMaster.conf')
$egsManager = Read-Text (Join-Path $egsScripts 'EGS_Manager.c')
$egsCombat = Read-Text (Join-Path $egsScripts 'EGS_CombatComponent.c')
$egsGroup = Read-Text (Join-Path $egsScripts 'EGS_Group.c')
$egsPersistence = Read-Text (Join-Path $egsScripts 'EGS_Persistence.c')
$egsSaved = Read-Text (Join-Path $egsScripts 'EGS_SavedAttributes.c')
$egsList = Read-Text (Join-Path $egs 'Configs/Editor/AttributeLists/Edit.conf')

# Tabs: two new EXPBG attribute categories with their own resource ids.
$esrCategory = Join-Path $esr 'Configs/Editor/AttributeCategories/EXPBG_SurrenderIntel.conf'
$egsCategory = Join-Path $egs 'Configs/Editor/AttributeCategories/EGS_Roe.conf'
Assert ((Read-Text $esrCategory) -match 'SCR_EditorAttributeCategory' -and (Read-Text $esrCategory) -match 'Name "EXPBG Surrender & Intel"' -and (Read-Text $esrCategory) -match 'EXPBG_Badge_UI\.edds') 'EXPBG Surrender & Intel category'
Assert ((Read-Text $egsCategory) -match 'Name "EXPBG AI Skill & ROE"' -and (Read-Text $egsCategory) -match 'EXPBG_Badge_UI\.edds') 'EXPBG AI Skill & ROE category (squad and soldier skill and rules of engagement)'
Assert ((Read-Text ($esrCategory + '.meta')) -match 'Name "\{5F9AC55BEEA3F6FA\}Configs/Editor/AttributeCategories/EXPBG_SurrenderIntel\.conf"') 'surrender category metadata'
Assert ((Read-Text ($egsCategory + '.meta')) -match 'Name "\{CB1FB5533BC95820\}Configs/Editor/AttributeCategories/EGS_Roe\.conf"') 'ROE category metadata'

# AI Surrender attribute list: eight new classes in the new tab, spinboxes, after Prisoners.
$classes = 'ESR_GroupSurrenderAttribute','ESR_GroupRevealAttribute','ESR_GroupIdentityAttribute','ESR_GroupIntelAttribute','ESR_UnitSurrenderAttribute','ESR_UnitRevealAttribute','ESR_UnitIdentityAttribute','ESR_UnitIntelAttribute'
$slots = 'SURRENDER','REVEAL','IDENTITY','INTEL','SURRENDER','REVEAL','IDENTITY','INTEL'
$order = [regex]::Matches($esrList, '(?m)^  (ESR_\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value }
$prisonersAt = [array]::IndexOf([string[]]$order, 'ESR_PrisonersAttribute')
Assert (($order[($prisonersAt + 1)..($prisonersAt + 8)] -join ',') -eq ($classes -join ',')) "override attributes follow Prisoners in order: $($order -join ',')"
for ($n = 0; $n -lt $classes.Count; $n++) {
 $entry = [regex]::Match($esrList, "(?s)  $($classes[$n]) \{(.*?)\n  \}").Groups[1].Value
 Assert ($entry -match 'EXPBG_SurrenderIntel\.conf' -and $entry -match 'AttributePrefab_Spinbox\.layout' -and $entry -notmatch 'm_Key') "$($classes[$n]) entry: new tab, spinbox"
 $parent = if ($n -lt 4) { 'ESR_GroupOverrideAttribute' } else { 'ESR_UnitOverrideAttribute' }
 $body = Get-Body $text.esrAttributes "class\s+$($classes[$n])\s*:\s*$parent"
 Assert ($body -match "return ESR_Overrides\.$($slots[$n]);") "$($classes[$n]) edits slot $($slots[$n])"
}
$names = [regex]::Matches($esrList, '(?s)  ESR_(Group|Unit)\w+Attribute \{\s*m_UIInfo SCR_EditorAttributeUIInfo \{ Name "([^"]+)"') | ForEach-Object { $_.Groups[2].Value }
Assert (($names -join '|') -eq 'Squad surrender chance (%)|Squad interrogation: reveal squad (%)|Squad interrogation: identity (%)|Squad interrogation: reveal intel items (%)|Soldier surrender chance (%)|Soldier interrogation: reveal squad (%)|Soldier interrogation: identity (%)|Soldier interrogation: reveal intel items (%)') "override names follow the module settings, marked squad or soldier (both show when a soldier is edited): $($names -join '|')"

# Attribute behaviour: entries in code (0 = no override, then 0-100 % in steps of 5),
# serializable, set values only in saves, GM edits only from an unlimited Edit-mode editor,
# shown for every AI squad and soldier with or without a module, squad/soldier targets kept apart.
$base = Get-Body $text.esrAttributes 'class\s+ESR_OverrideAttribute\s*:\s*SCR_BaseEditorAttribute'
Assert ($base -match 'override bool IsSerializable\(\)' -and $base -match 'return ESR_Overrides\.IsSlot\(Slot\(\)\);') 'override attributes are serializable'
Assert ($text.overrides -match 'static const int STEP = 5;' -and $text.overrides -match 'static const int ENTRY_COUNT = 22;') 'spinbox: 22 entries in steps of 5'
Assert ((Get-Body $text.overrides 'static\s+int\s+FromEntry\s*\(') -match 'if \(entry <= 0\)\s*return UNSET;' -and (Get-Body $text.overrides 'static\s+int\s+ToEntry\s*\(') -match 'if \(value < 0\)\s*return 0;') 'entry 0 means no override'
$read = Get-Body $base 'override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\('
Assert ($read -match 'bool dialog = manager != null;' -and $read -match 'if \(!dialog && value < 0\)\s*return null;' -and $read -notmatch 'ActiveCount') 'reads: saves only set values; the dialog for every AI squad and soldier, module or not'
$write = Get-Body $base 'override\s+void\s+WriteVariable\s*\('
Assert ($write -match 'Replication\.IsServer\(\)' -and $write -match 'if \(playerID != -1\)' -and $write -match 'editor\.IsLimited\(\)' -and $write -match 'EEditorMode\.EDIT' -and $write -match 'editor\.GetPlayerID\(\) != playerID') 'writes: server, session-load contract or the editing Game Master'
Assert ((Get-Body $text.esrAttributes 'static\s+SCR_AIGroup\s+GroupTarget\s*\(') -match 'EEditableEntityType\.GROUP' -and (Get-Body $text.esrAttributes 'static\s+SCR_AIGroup\s+GroupTarget\s*\(') -match 'IsPlayable\(\)') 'squad attributes: AI groups only'
Assert ((Get-Body $text.esrAttributes 'static\s+SCR_ChimeraCharacter\s+UnitTarget\s*\(') -match 'EEditableEntityType\.CHARACTER' -and (Get-Body $text.esrAttributes 'static\s+SCR_ChimeraCharacter\s+UnitTarget\s*\(') -match 'IsPlayerCharacter') 'soldier attributes: AI soldiers only'

# Resolution: soldier > squad (cached per group) > module, at decision time.
$resolve = Get-Body $text.overrides 'static\s+int\s+Resolve\s*\('
Assert ($resolve.IndexOf('character.ESR_GetOverride(slot)') -ge 0 -and $resolve.IndexOf('character.ESR_GetOverride(slot)') -lt $resolve.IndexOf('squad.ESR_SquadValue(slot, source)') -and $resolve.IndexOf('squad.ESR_SquadValue(slot, source)') -lt $resolve.IndexOf('ESR_Settings.Get(SettingKey(slot))')) 'resolution order: soldier, squad, module'
$squadValue = Get-Body $text.overrides 'int\s+ESR_SquadValue\s*\('
Assert ($squadValue -match 'int revision = ESR_Settings\.Revision\(\);' -and $squadValue -match 'm_iESR_EffectiveRevision != revision' -and $squadValue -match 'for \(int key = 0; key < ESR_Overrides\.COUNT; key\+\+\)') 'squad values cached per group against the settings revision'
Assert ((Get-Body $text.overrides '(?s)modded class SCR_AIGroup.*?bool\s+ESR_SetOverride\s*\(') -match 'm_iESR_EffectiveRevision = -1;') 'a squad override change clears its cache'
Assert ((Get-Body $settings 'static\s+void\s+Adopt\s*\(') -match 's_iRevision\+\+;' -and (Get-Body $settings 'static\s+bool\s+Set\s*\(') -match 's_iRevision\+\+;') 'module setting changes bump the revision'
$forPrisoner = Get-Body $text.overrides 'static\s+int\s+ForPrisonerSource\s*\('
Assert ($forPrisoner.IndexOf('prisoner.Character.ESR_GetOverride(slot)') -lt $forPrisoner.IndexOf('prisoner.SquadOverrides[slot]') -and $forPrisoner -match 'return moduleValue;') 'prisoner: his own value, then his former squad''s, then the module'

# Surrender and interrogation use the effective values.
$evaluate = Get-Body $manager 'protected\s+static\s+void\s+Evaluate\s*\('
Assert ($evaluate -match 'float chance = ESR_Overrides\.Resolve\(null, group, ESR_Overrides\.SURRENDER, squadSource\);' -and $evaluate -match 'int own = candidate\.ESR_GetOverride\(ESR_Overrides\.SURRENDER\);') 'surrender roll: squad value, then each soldier''s own'
Assert ($evaluate -match 'else if \(squadSource == ESR_Overrides\.SOURCE_MODULE\) rolled = Math\.Clamp\(chance \+ ESR_Jitter\(spread\), 0, 100\);' -and $evaluate -match 'if \(rolled < 100 && Math\.RandomFloat\(0, 100\) >= rolled\) continue;') 'overrides are exact; the random factor spreads only the module chance; 100% never rolls'
Assert ($evaluate.IndexOf('if (ratio < record.Threshold) return;') -lt $evaluate.IndexOf('ESR_Overrides.Resolve(')) 'the casualty threshold still decides whether the squad breaks'
Assert ($evaluate -match 'Trace\(string\.Format\("squad %1 surrender chance=%2 source=%3 soldierOverrides=%4') 'evaluation diagnostics line'
$surrender = Get-Body $manager 'static\s+bool\s+Surrender\s*\('
Assert ($surrender.IndexOf('ESR_Overrides.CaptureSquad(prisoner, group);') -ge 0 -and $surrender.IndexOf('ESR_Overrides.CaptureSquad(prisoner, group);') -lt $surrender.IndexOf('group.RemoveAgent(agent)')) 'squad overrides are captured before he leaves the squad'
Assert ($manager -match 'ref array<int> SquadOverrides = \{\};') 'prisoner record holds his squad''s overrides'
$interrogate = Get-Body $manager 'static\s+int\s+Interrogate\s*\('
Assert ($interrogate -match 'float reveal = ESR_Overrides\.ForPrisoner\(prisoner, ESR_Overrides\.REVEAL, ESR_Settings\.Get\(ESR_Settings\.REVEAL\)\);' -and $interrogate -match 'float identity = ESR_Overrides\.ForPrisoner\(prisoner, ESR_Overrides\.IDENTITY, ESR_Settings\.Get\(ESR_Settings\.IDENTITY\)\);' -and $interrogate -match 'ESR_Overrides\.DescribePrisoner\(prisoner\)') 'interrogation uses the prisoner''s effective chances and logs them'
Assert ((Get-Body $manager 'protected\s+static\s+void\s+RevealIntel\s*\(') -match 'int chance = ESR_Overrides\.ForPrisoner\(prisoner, ESR_Overrides\.INTEL, ESR_Settings\.Get\(ESR_Settings\.INTEL\)\);') 'intel roll uses the prisoner''s effective chance'
foreach ($writer in 'SetGroup', 'SetUnit') {
 $body = Get-Body $text.overrides "static\s+bool\s+$writer\s*\("
 Assert ($body -match 'Replication\.IsServer\(\)' -and $body -match 'ESR_SurrenderManager\.Trace\(' -and $body -match 'Track\(') "$writer is server-only, tracked and traced"
}

# Native saves: a state of its own, nothing written without overrides, bounded binding.
$serialize = Get-Body $text.esrPersistence 'override\s+protected\s+ESerializeResult\s+Serialize\s*\('
Assert ($serialize -match 'if \(groupIds\.IsEmpty\(\) && unitIds\.IsEmpty\(\)\)\s*return ESerializeResult\.DEFAULT;') 'missions without overrides save nothing new'
$deserialize = Get-Body $text.esrPersistence 'override\s+protected\s+bool\s+Deserialize\s*\('
Assert ($deserialize -match 'groupIds\.Count\(\) \* ESR_Overrides\.COUNT' -and $deserialize -match 'ESR_Overrides\.MAX_TRACKED') 'loads are validated and bounded'
Assert ($esrPersistConf -match 'StatePersistenceConfig "\{438CEF17F4C9AF45\}"' -and $esrPersistConf -match 'Serializer ESR_OverridesSerializer "\{C9ADAC18D15A7744\}"' -and $esrPersistConf -match '(?s)PersistentStates \+ \{\s*ESR_OverridesState "\{336619E1EF808904\}"' -and $esrPersistConf -match 'SurrenderModule\.conf') 'persistence config registers the override state next to the module'
$bind = Get-Body $text.overrides 'static\s+void\s+BindPending\s*\('
Assert ($bind -match 'RemoveOrdered' -and $bind -notmatch 's_aPending\w+\.Remove\(' -and $bind -match 's_bBindQueued' -and $bind -match 'BIND_RETRY_MS' -and $bind -match 's_fBindUntil') 'binding keeps the parallel lists aligned, one retry timer, a deadline'
Assert ($text.overrides -match 'static const int MAX_TRACKED = 2048;' -and (Get-Body $text.overrides 'protected\s+static\s+void\s+Track\s*\(') -match 'MAX_TRACKED') 'save registry bounded'

# Caching: survivor carry, group snapshot, portable keys read only when present.
foreach ($cache in @(@{ t = $text.esrCache; squad = 'esrSquadOverrides'; soldiers = 'esrSurvivorOverrides' }, @{ t = $text.egsCache; squad = 'egsRoe'; soldiers = 'egsSurvivorRoe' })) {
 $t = $cache.t
 Assert ($t -match 'modded class EBG_SurvivorCarry' -and $t -match 'override void Capture\(SCR_ChimeraCharacter entity\)' -and $t -match 'override void Apply\(SCR_ChimeraCharacter entity\)') "survivor carry captures and applies ($($cache.squad))"
 Assert ($t -match 'modded class EBG_CacheGroupSnapshot' -and $t -match 'override bool CaptureGroup\(SCR_AIGroup group\)' -and $t -match 'override bool Apply\(SCR_AIGroup group\)') "group snapshot captures and applies ($($cache.squad))"
 $snapRead = Get-Body $t 'override\s+bool\s+Read\s*\(\s*LoadContext\s+context\s*\)'
 Assert ($snapRead -match "if \(!context\.DoesKeyExist\(`"$($cache.squad)`"\)\)\s*return true;") "old group snapshots without $($cache.squad) still load"
 $fullRead = Get-Body $t 'override\s+bool\s+ReadSnapshot\s*\('
 Assert ($fullRead -match "if \(!context\.DoesKeyExist\(`"$($cache.soldiers)`"\)\)\s*return true;" -and $fullRead -match 'm_Survivors\.Count\(\)') "old portable snapshots without $($cache.soldiers) still load"
 $fullWrite = Get-Body $t 'override\s+bool\s+WriteSnapshot\s*\('
 Assert ($fullWrite -match 'if \(!any\)\s*return true;' -and $fullWrite -match "WriteValue\(`"$($cache.soldiers)`"") "portable snapshots gain $($cache.soldiers) only when set"
}

# AI Global Skills: soldier ROE resolved when vanilla asks, events recompute it.
$getMode = Get-Body $egsCombat 'override\s+EAIGroupCombatMode\s+GetCombatMode\s*\('
Assert ($getMode -match '(?s)^\s*if \(m_iEGS_UnitRoe == EGS_UnitRoe\.FOLLOW\)\s*return super\.GetCombatMode\(\);' -and $getMode -match 'group\.EGS_IsManaged\(\)' -and $getMode -match 'group\.EGS_ReturnFireMode\(\)' -and $getMode -match 'group\.EGS_VanillaMode\(\)') 'GetCombatMode: vanilla unless the soldier has his own ROE in a managed AI squad'
$evaluateTarget = Get-Body $egsCombat 'override\s+void\s+EvaluateWeaponAndTarget\s*\('
Assert ($evaluateTarget -match '(?s)if \(m_iEGS_UnitRoe != EGS_UnitRoe\.FOLLOW\)\s*\{.*EGS_OnUnitHostileSelected.*return;\s*\}' ) 'a soldier with his own ROE never shoots warnings for his squad'
Assert ((Get-Body $egsCombat 'void\s+EGS_EndWarningShots\s*\(') -match 'else if \(notifyGroup && unitShots\)\s*EGS_OnUnitWarningDone') 'his own warning burst ends his own sequence'
Assert ((Get-Body $egsCombat 'void\s+EGS_OnUnitTimer\s*\(') -match 'EGS_Manager\.TIMER_LETHAL' -and $egsManager -match 'static void ScheduleUnit\(SCR_AICombatComponent combat' -and $egsManager -match 'timer\.m_Combat\.EGS_OnUnitTimer\(') 'soldier warning timers ride the shared bounded tick'
Assert ((Get-Body $text.unitRoe 'bool\s+EGS_IsEndangered\s*\(') -match 'Math\.MinInt\(m_Perception\.m_aTargetClusters\.Count\(\), EGS_MAX_CLUSTERS\)') 'endangered check bounded'
Assert ($egsGroup -match 'EAIGroupCombatMode EGS_ReturnFireMode\(\)' -and $egsGroup -match 'EAIGroupCombatMode EGS_VanillaMode\(\)') 'group helpers for soldier ROE'
$applyUnit = Get-Body $egsManager 'protected\s+static\s+void\s+ApplyUnit\s*\('
Assert ($applyUnit -match 'ResetUnit\(combat\);' -and (Get-Body $egsManager 'protected\s+static\s+void\s+ResetUnit\s*\(') -match 'combat\.EGS_SetUnitRoe\(EGS_UnitRoe\.FOLLOW\);' -and $applyUnit -match 'combat\.EGS_SetUnitRoe\(EGS_UnitRoe\.Effective\(unitOverride\)\);') 'soldier ROE recomputed with the unit (module, settings, membership, attribute events)'
$setUnit = Get-Body $egsManager 'static\s+void\s+SetUnitRoe\s*\('
Assert ($setUnit -match 'MAX_OVERRIDE_UNITS' -and $setUnit -match 'ApplyUnit\(soldier\);' -and $setUnit -match 'PrintFormat\("\[EXPBG AI SKILLS\] unit=%1 roeOverride=%2"') 'soldier ROE writer: bounded registry, applied at once, logged'
Assert ($egsPersistence -match 'protected static const int VERSION = 2;' -and $egsPersistence -match 'VERSION_GROUPS_ONLY = 1;' -and $egsPersistence -match '\(version != VERSION && version != VERSION_GROUPS_ONLY && version != VERSION_SKILLS\)' -and $egsPersistence -match 'if \(version >= VERSION\)' -and $egsPersistence -match 'EGS_Manager\.ImportUnitOverrides\(unitIds, unitRoe\);' -and $egsPersistence -match '(?s)if \(unitIds\.IsEmpty\(\)\)\s*version = VERSION_GROUPS_ONLY;') 'EGS state version 2 with soldier overrides, written as version 1 without them; version 1 still loads'
Assert ($egsSaved -match 'class EGS_SavedUnitRoeAttribute : EGS_SavedAttribute' -and (Get-Body $egsSaved 'class\s+EGS_SavedUnitRoeAttribute') -match 'IsSessionLoad\(manager, playerID, var\)') 'attribute-based saves keep soldier ROE'
$groupRoe = [regex]::Match($egsList, '(?s)  EGS_GroupRoeAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($groupRoe -match 'Name "Rules of engagement \(squad\)"' -and $groupRoe -match 'EGS_Roe\.conf' -and $groupRoe -notmatch 'Group\.conf') 'squad ROE moved to the EXPBG AI Skill & ROE tab'
$unitList = [regex]::Match($egsList, '(?s)  EGS_UnitRoeAttribute \{(.*?)\n  \}\n  EGS_').Groups[1].Value
Assert ($unitList -match 'Name "Rules of engagement \(soldier\)"' -and $unitList -match 'EGS_Roe\.conf' -and $unitList -match 'm_sEntryName "Use squad setting"' -and $unitList -match 'm_sEntryName "Exempt \(vanilla\)"') 'soldier ROE in the same tab with the squad choices'
Assert ($egsList -match '(?m)^  EGS_SavedUnitRoeAttribute \{') 'saved soldier ROE attribute listed'
Assert ((Get-Body $text.unitRoe 'override\s+bool\s+IsSerializable\s*\(') -match 'return false;') 'the soldier ROE dialog attribute is not saved twice'

# Minimal replication: the new values are plain server fields.
foreach ($key in 'overrides', 'esrAttributes', 'esrCache', 'unitRoe', 'egsCache') { Assert ($text[$key] -notmatch 'RplProp|Replication\.BumpMe') "no replication in $($paths[$key])" }

# Enforce gotchas, ASCII and LF in the new files.
foreach ($path in $newFiles) {
 $t = Read-Text $path
 Assert (![regex]::IsMatch($t, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep|Wait)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($t, '\b(int|float|bool|string|vector|IEntity|Managed)\s+(Node|Faction|Group|set|map|array)\b')) "variable named like a vanilla type in $path"
 Assert (![regex]::IsMatch($t, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 Assert (![regex]::IsMatch($t, '(?m)^[ \t]*(static[ \t]+|override[ \t]+|protected[ \t]+)*(int|vector|bool|float|string|IEntity|EAIGroupCombatMode|SCR_\w+|array<int>)[ \t]+\w+[ \t]*\([^)\n]*\)[ \t]*\{[ \t]*return')) "non-void returns stay on separate lines: $path"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
 Assert (!$t.Contains("`r")) "CR line ending in $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = $text.fixture
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'override fixture keeps the runner driver class names'
$regex = '\[ESR OVERRIDE RESULT\] checks=[1-9]\d* failures=0 surrendered=4 heldOut=1 control=0 interrogation=3 intel=1 roe=1 cached=1 reason=complete'
Assert ($fixture.Contains("-FixturePath tests/ESR_OverrideGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'override fixture header carries its runner command'
$head = [regex]::Match($fixture, 'Print\(string\.Format\("(\[ESR OVERRIDE RESULT\][^"]*)"').Groups[1].Value
$tail = [regex]::Match($fixture, 'string tail = string\.Format\("([^"]*)"').Groups[1].Value
Assert ($head -eq '[ESR OVERRIDE RESULT] checks=%1 failures=%2 surrendered=%3 heldOut=%4 control=%5 interrogation=%6 intel=%7' -and $tail -eq ' roe=%1 cached=%2 reason=%3') "override fixture RESULT format changed: $head|$tail"
function Fill([string]$Format, [string[]]$Values) { $line = $Format; for ($n = $Values.Count; $n -ge 1; $n--) { $line = $line.Replace("%$n", $Values[$n - 1]) }; $line }
function Line([string[]]$A, [string[]]$B) { (Fill $head $A) + (Fill $tail $B) }
Assert ((Line @('60', '0', '4', '1', '0', '3', '1') @('1', '1', 'complete')) -match $regex) 'override fixture RESULT line matches its -ExpectResult regex'
Assert (!((Line @('60', '1', '4', '1', '0', '3', '1') @('1', '1', 'complete')) -match $regex)) 'a failing RESULT line does not match'
Assert (!((Line @('60', '0', '5', '0', '0', '3', '1') @('1', '1', 'complete')) -match $regex)) 'a holdout who surrendered does not match'
Assert (!((Line @('60', '0', '4', '1', '0', '3', '1') @('1', '0', 'complete')) -match $regex)) 'a lost cache value does not match'
Assert ($fixture -match 'ESR_SurrenderManager\.Interrogate\(FirstPrisoner\.Point, FirstPrisoner\.Point, 0\)' -and $fixture -match 'EBG_DurableFullCache copy = new EBG_DurableFullCache\(\);' -and $fixture -match 'full\.WriteSnapshot\(save\)' -and $fixture -match 'EGS_Manager\.ImportUnitOverrides\(SavedUnitIds, SavedUnitRoe\);' -and $fixture -match 'ESR_Overrides\.Import\(groupIds, groupValues, unitIds, unitValues\);') 'override fixture drives production interrogation, portable snapshots and save import'
Assert ($fixture -match 'Resource resource = Resource\.Load\(prefab\);') 'override fixture keeps Resource.Load results in locals'
'PASS: per-squad and per-soldier surrender/intel overrides and soldier ROE: new EXPBG tabs, soldier > squad > module at decision time with per-group cache, prisoners carry their squad''s values, no replication, native/CDF save and Full-cache survival with old-snapshot compatibility, bounded loops, diagnostics; override fixture wired.'
