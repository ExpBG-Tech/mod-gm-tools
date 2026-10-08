#requires -Version 7.0
# Portable guard for the squad and soldier Game Master attributes of AI Global Skills and AI
# Surrender (production report on 0.1.17: the AI skill override and the surrender / intel
# chances did not show up for a selected AI squad or soldier, RHS included):
# - Visibility: the "EXPBG AI Skill & ROE" and "EXPBG Surrender & Intel" squad and soldier
#   attributes answer for every AI squad and AI soldier of any faction or mod, with or without
#   a module (0.1.17 hid them until a module was placed); squad attributes also answer through
#   one of the squad's soldiers in the dialog (like the vanilla Group tab), never in saves.
# - Registration: the attributes stay in the merged Edit.conf (CDF Game Master Save reads that
#   list) and are also appended in script (modded SCR_AttributesManagerEditorComponentClass)
#   from each module's own list when another mod's Edit list override dropped them; the
#   fallback lists are byte-identical copies of the Edit.conf entries.
# - Skill per squad and per soldier: soldier > squad > module, applied with or without a
#   module, kept by native saves (version 3 only when set), session saves and caching.
# - Rules of engagement: a squad's or soldier's own choice applies without a module too.
# Also checks tests/EGS_SquadSoldierGameplay.c is wired to the runner and its RESULT line
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
 $open = $Text.IndexOf('{', $match.Index + $match.Length - 1); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = $Text.IndexOf($First); $b = $Text.IndexOf($Second)
 return $a -ge 0 -and $b -ge 0 -and $a -lt $b
}
function Get-Entry([string]$List, [string]$Class) {
 $m = [regex]::Match($List, "(?s)\n(  $Class \{.*?\n  \}\n)")
 Assert $m.Success "$Class entry not found"
 return $m.Groups[1].Value
}

$paths = [ordered]@{
 egsInjection = Join-Path $egsScripts 'EGS_AttributeInjection.c'
 esrInjection = Join-Path $esrScripts 'ESR_AttributeInjection.c'
 skill = Join-Path $egsScripts 'EGS_SkillOverrides.c'
 egsAttributes = Join-Path $egsScripts 'EGS_Attributes.c'
 unitRoe = Join-Path $egsScripts 'EGS_UnitRoe.c'
 manager = Join-Path $egsScripts 'EGS_Manager.c'
 settings = Join-Path $egsScripts 'EGS_Settings.c'
 persistence = Join-Path $egsScripts 'EGS_Persistence.c'
 cache = Join-Path $egsScripts 'EGS_OverrideCache.c'
 esrAttributes = Join-Path $esrScripts 'ESR_OverrideAttributes.c'
 fixture = Join-Path $repo 'tests/EGS_SquadSoldierGameplay.c'
}
$t = @{}
foreach ($key in $paths.Keys) { $t[$key] = Read-Text $paths[$key] }
$egsEdit = Read-Text (Join-Path $egs 'Configs/Editor/AttributeLists/Edit.conf')
$esrEdit = Read-Text (Join-Path $esr 'Configs/Editor/AttributeLists/Edit.conf')
$egsListPath = Join-Path $egs 'Configs/Editor/AttributeLists/EXPGS/EGS_SquadSoldier.conf'
$esrListPath = Join-Path $esr 'Configs/Editor/AttributeLists/EXPSR/ESR_SquadSoldier.conf'
$egsList = Read-Text $egsListPath
$esrList = Read-Text $esrListPath
$egsClasses = 'EGS_GroupSkillAttribute', 'EGS_UnitSkillAttribute', 'EGS_GroupRoeAttribute', 'EGS_UnitRoeAttribute'
$esrClasses = 'ESR_GroupSurrenderAttribute', 'ESR_GroupRevealAttribute', 'ESR_GroupIdentityAttribute', 'ESR_GroupIntelAttribute', 'ESR_UnitSurrenderAttribute', 'ESR_UnitRevealAttribute', 'ESR_UnitIdentityAttribute', 'ESR_UnitIntelAttribute'

# --- A. Registration: Edit.conf for the dialog and CDF, script fallback for list overrides ----
foreach ($set in @(@{ list = $egsList; edit = $egsEdit; classes = $egsClasses; path = $egsListPath; meta = 'A4BB2BF355CA2A33'; rel = 'Configs/Editor/AttributeLists/EXPGS/EGS_SquadSoldier.conf'; code = $t.egsInjection; prefix = 'EGS' },
                   @{ list = $esrList; edit = $esrEdit; classes = $esrClasses; path = $esrListPath; meta = '867D8E3D247F48BF'; rel = 'Configs/Editor/AttributeLists/EXPSR/ESR_SquadSoldier.conf'; code = $t.esrInjection; prefix = 'ESR' })) {
 $listed = @([regex]::Matches($set.list, '(?m)^  (\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value })
 Assert (($listed -join ',') -eq ($set.classes -join ',')) "$($set.prefix) fallback list holds exactly the squad and soldier dialog attributes: $($listed -join ',')"
 Assert ($set.list.StartsWith("SCR_EditorAttributeList {`n m_aAttributes {`n") -and $set.list.EndsWith(" }`n}`n")) "$($set.prefix) fallback list is a plain attribute list (no inheritance from the overridable Edit list)"
 foreach ($class in $set.classes) {
  Assert ((Get-Entry $set.edit $class) -ceq (Get-Entry $set.list $class)) "$class is identical in Edit.conf and the $($set.prefix) fallback list"
 }
 Assert ((Read-Text ($set.path + '.meta')) -match ('Name "\{' + $set.meta + '\}' + [regex]::Escape($set.rel) + '"')) "$($set.prefix) fallback list metadata"
 $code = $set.code
 Assert ($code -match '(?m)^modded class SCR_AttributesManagerEditorComponentClass\r?$') "$($set.prefix): modded attribute manager class (script registration)"
 Assert ($code.Contains('"{' + $set.meta + '}' + $set.rel + '"')) "$($set.prefix): the fallback resource name matches its metadata"
 $inject = Get-Body $code "protected\s+void\s+$($set.prefix)_InjectSquadSoldier\s*\(\s*\)"
 Assert ((Before $inject "m_b$($set.prefix)_Injected = true;" "$($set.prefix)_IsEditList()") -and (Before $inject "$($set.prefix)_IsEditList()" 'BaseContainerTools.LoadContainer(') -and $inject -match "$($set.prefix)_HasAttributeType\(attribute\.Type\(\)\)" -and $inject -match 'm_aAttributes\.Insert\(attribute\);') "$($set.prefix): appended once, only for the Edit list, only classes the list lacks"
 Assert ($inject -match 'Resource resource = BaseContainerTools\.LoadContainer\(' -and $code -match "protected ref SCR_EditorAttributeList m_$($set.prefix)_SquadSoldierList;") "$($set.prefix): the loaded resource stays in a local; the class owns the appended attributes"
 $isEdit = Get-Body $code "protected\s+bool\s+$($set.prefix)_IsEditList\s*\(\s*\)"
 Assert ($isEdit -match 'SCR_AIGroupCombatModeAttribute\.Cast\(attribute\)') "$($set.prefix): the Edit list is recognised by the vanilla Set combat mode attribute (Admin and Photo lists untouched)"
 foreach ($getter in @(@{ s = 'override\s+SCR_BaseEditorAttribute\s+GetAttribute\s*\(\s*int\s+index\s*\)'; c = 'return super.GetAttribute(index);' }, @{ s = 'override\s+int\s+GetAttributesCount\s*\(\s*\)'; c = 'return super.GetAttributesCount();' }, @{ s = 'override\s+int\s+FindAttribute\s*\(\s*SCR_BaseEditorAttribute\s+attribute\s*\)'; c = 'return super.FindAttribute(attribute);' })) {
  $body = Get-Body $code $getter.s
  Assert (Before $body "$($set.prefix)_InjectSquadSoldier();" $getter.c) "$($set.prefix): $($getter.c) runs after the fallback (every reader of the index list)"
 }
 foreach ($class in $set.classes) { Assert ($set.edit -match "(?m)^  $class \{") "$class stays in Edit.conf (CDF Game Master Save reads that list)" }
}
# Model of the fallback: only missing classes are appended, in list order, never twice.
function Inject([string[]]$Existing, [string[]]$Fallback, [bool]$EditList) {
 $out = [Collections.Generic.List[string]]::new(); foreach ($e in $Existing) { $out.Add($e) }
 if (!$EditList) { return ,$out }
 foreach ($f in $Fallback) { if (!$out.Contains($f)) { $out.Add($f) } }
 return ,$out
}
$vanilla = @('SCR_AIGroupFactionEditorAttribute', 'SCR_AIGroupCombatModeAttribute')
Assert ((Inject ($vanilla + $esrClasses) $esrClasses $true).Count -eq 10) 'model: an Edit list that carries the attributes gets nothing appended (indexes unchanged)'
Assert (((Inject $vanilla $esrClasses $true) -join ',') -eq (($vanilla + $esrClasses) -join ',')) 'model: an Edit list a foreign override replaced gets every attribute appended in list order'
Assert ((Inject @('SCR_PlayerFactionAttribute') $esrClasses $false).Count -eq 1) 'model: Admin and Photo lists are left alone'

# --- B. Visibility: any AI squad or soldier, module or not ---------------------------------
$groupRoeRead = Get-Body $t.egsAttributes '(?s)class\s+EGS_GroupRoeAttribute\s*:\s*SCR_BaseEditorAttribute.*?override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\('
$unitRoeRead = Get-Body $t.unitRoe '(?s)class\s+EGS_UnitRoeAttribute\s*:\s*SCR_BaseEditorAttribute.*?override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\('
$groupSkillRead = Get-Body $t.skill '(?s)class\s+EGS_GroupSkillAttribute\s*:\s*EGS_SkillOverrideAttribute.*?override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\('
$unitSkillRead = Get-Body $t.skill '(?s)class\s+EGS_UnitSkillAttribute\s*:\s*EGS_SkillOverrideAttribute.*?override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\('
$esrRead = Get-Body $t.esrAttributes '(?s)class\s+ESR_OverrideAttribute\s*:\s*SCR_BaseEditorAttribute.*?override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\('
foreach ($read in @(@{ b = $groupRoeRead; n = 'squad ROE' }, @{ b = $unitRoeRead; n = 'soldier ROE' }, @{ b = $groupSkillRead; n = 'squad skill' }, @{ b = $unitSkillRead; n = 'soldier skill' }, @{ b = $esrRead; n = 'surrender and intel' })) {
 Assert ($read.b -notmatch 'HasAny|ActiveCount|IsActive') "$($read.n) attributes answer with or without a module"
}
Assert ($groupRoeRead -match 'EGS_Attributes\.DialogGroup\(item\)' -and $groupSkillRead -match 'EGS_Attributes\.DialogGroup\(item\)' -and $groupRoeRead -match 'if \(!manager\)\s*return null;') 'squad ROE and skill: dialog only, through DialogGroup'
$dialogGroup = Get-Body $t.egsAttributes 'static\s+SCR_AIGroup\s+DialogGroup\s*\('
Assert ($dialogGroup -match 'EEditableEntityType\.GROUP' -and $dialogGroup -match 'EEditableEntityType\.CHARACTER' -and $dialogGroup -match 'return EGS_Manager\.ResolveGroup\(editable\);') 'DialogGroup: the squad itself or the squad of an edited soldier, AI squads only (ResolveGroup)'
$resolveGroup = Get-Body $t.manager 'static\s+SCR_AIGroup\s+ResolveGroup\s*\('
Assert ($resolveGroup -match 'editable\.GetAIGroup\(\)' -and $resolveGroup -match 'group\.EGS_IsManaged\(\)' -and $resolveGroup -notmatch 'GetPrefab|ResourceName|FactionKey') 'ResolveGroup: any AI squad without players, no faction or prefab filter'
$soldier = Get-Body $t.unitRoe 'static\s+SCR_ChimeraCharacter\s+ResolveSoldier\s*\('
Assert ($soldier -match 'EEditableEntityType\.CHARACTER' -and $soldier -match 'SCR_AICombatComponent' -and $soldier -notmatch 'GetPrefab|ResourceName|FactionKey') 'ResolveSoldier: any AI character with the vanilla AI combat component (RHS and other mods inherit it), no prefab filter'
$groupTarget = Get-Body $t.esrAttributes 'static\s+SCR_AIGroup\s+GroupTarget\s*\('
Assert ($groupTarget -match 'bool viaSoldier' -or $t.esrAttributes -match 'GroupTarget\(Managed item, bool viaSoldier = false\)') 'surrender squad target takes viaSoldier'
Assert ($groupTarget -match '(?s)else if \(viaSoldier && editable\.GetEntityType\(\) == EEditableEntityType\.CHARACTER\)' -and $groupTarget -match 'editable\.GetAIGroup\(\)' -and $groupTarget -notmatch 'GetPrefab|ResourceName') 'surrender squad target: the squad itself, or through a soldier when asked'
Assert ($esrRead -match 'bool dialog = manager != null;' -and $esrRead -match 'ReadOverride\(item, dialog, value\)' -and (Get-Body $t.esrAttributes '(?s)class\s+ESR_GroupOverrideAttribute.*?override\s+protected\s+bool\s+ReadOverride\s*\(') -match 'GroupTarget\(item, dialog\)') 'squad values through a soldier in the dialog only; session saves keep to the squad itself'
Assert ((Get-Body $t.esrAttributes 'override\s+void\s+WriteVariable\s*\(') -match 'WriteOverride\(item, ESR_Overrides\.FromEntry\(var\.GetInt\(\)\), origin, manager != null\);') 'writes resolve the same target as the read'
foreach ($name in 'Squad surrender chance (%)', 'Soldier surrender chance (%)', 'Squad interrogation: reveal intel items (%)', 'Soldier interrogation: reveal intel items (%)') { Assert ($esrEdit.Contains("Name `"$name`"")) "surrender attribute named $name (squad and soldier side by side when a soldier is edited)" }
Assert ($esrEdit -notmatch 'Shown while an EXPBG AI Surrender module is placed' -and $egsEdit -notmatch 'while an EXPBG AI Global Skills module exists') 'texts no longer say the attributes need a module to show'

# --- C. Skill per squad and per soldier -----------------------------------------------------
foreach ($class in 'EGS_GroupSkillAttribute', 'EGS_UnitSkillAttribute') {
 $entry = Get-Entry $egsEdit $class
 $names = @([regex]::Matches($entry, 'm_sEntryName "([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
 $values = @([regex]::Matches($entry, 'm_fEntryFloatValue (\d+)') | ForEach-Object { [int]$_.Groups[1].Value })
 Assert ($entry -match 'EGS_Roe\.conf' -and $entry -match 'AttributePrefab_Spinbox\.layout' -and $names.Count -eq 6 -and ($names[1..5] -join ',') -eq 'Novice,Rookie,Regular,Veteran,Expert' -and ($values -join ',') -eq '0,1,2,3,4,5') "$class`: spinbox in the EXPBG AI Skill & ROE tab, follow then Novice..Expert (stored 0-5)"
}
Assert ((Get-Entry $egsEdit 'EGS_GroupSkillAttribute') -match 'Name "AI skill \(squad\)"' -and (Get-Entry $egsEdit 'EGS_UnitSkillAttribute') -match 'Name "AI skill \(soldier\)"' -and (Get-Entry $egsEdit 'EGS_GroupSkillAttribute') -match 'm_sEntryName "Use module setting"' -and (Get-Entry $egsEdit 'EGS_UnitSkillAttribute') -match 'm_sEntryName "Use squad or module setting"') 'skill attribute names and defaults'
Assert ($egsEdit -match '(?m)^  EGS_SavedGroupSkillAttribute \{' -and $egsEdit -match '(?m)^  EGS_SavedUnitSkillAttribute \{') 'session-save skill attributes listed (CDF Game Master Save)'
Assert ((Read-Text (Join-Path $egs 'Configs/Editor/AttributeCategories/EGS_Roe.conf')) -match 'Name "EXPBG AI Skill & ROE"') 'the tab is named EXPBG AI Skill & ROE'
Assert ((Get-Body $t.skill 'class\s+EGS_SkillOverrideAttribute\s*:\s*SCR_BaseEditorAttribute') -match '(?s)override bool IsSerializable\(\)\s*\{\s*return false;') 'dialog skill attributes are not saved twice'
foreach ($saved in 'EGS_SavedGroupSkillAttribute', 'EGS_SavedUnitSkillAttribute') {
 $body = Get-Body $t.skill "class\s+$saved\s*:\s*EGS_SavedAttribute"
 Assert ($body -match 'if \(manager' -and $body -match 'EGS_SkillOverrides\.FOLLOW' -and $body -match 'IsSessionLoad\(manager, playerID, var\)') "$saved`: saves only set values, restores through the session-load contract"
}
Assert ((Get-Body $t.skill '(?s)class\s+EGS_SavedGroupSkillAttribute.*?override\s+SCR_BaseEditorAttributeVar\s+ReadVariable\s*\(') -match 'EEditableEntityType\.GROUP') 'saved squad skill: squads only (each squad saved once)'
$pick = Get-Body $t.skill 'static\s+int\s+Pick\s*\('
Assert ((Before $pick 'if (unitValue > FOLLOW)' 'if (squadValue > FOLLOW)') -and $pick -match 'return FOLLOW;') 'resolution: soldier, then squad, then the module'
Assert ((Get-Body $t.skill 'static\s+int\s+Resolve\s*\(') -match 'squad && squad\.EGS_IsManaged\(\)') 'a squad value counts only for AI squads'
$apply = Get-Body $t.manager 'protected\s+static\s+void\s+ApplyUnit\s*\('
Assert ($apply -match 'int ownSkill = EGS_SkillOverrides\.Resolve\(scripted, squad\);' -and (Before $apply 'int skill = ownSkill;' 'skill = EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_SKILL') -and $apply -match '(?s)if \(skill == EGS_SkillOverrides\.FOLLOW\)\s*skill = EGS_Settings\.ResolveIndex') 'ApplyUnit: own skill (soldier > squad) before the module''s faction and role skill'
Assert ($apply -match '(?s)if \(!active && ownSkill == EGS_SkillOverrides\.FOLLOW && unitOverride == EGS_Settings\.GROUP_ROE_DEFAULT\)\s*\{\s*ResetUnit\(combat\);\s*return;' -and $apply -match '(?s)if \(active && scripted\)\s*values = EGS_Settings\.GetFactionValues' -and $apply -match 'combat\.EGS_SetAmmoPolicy\(EGS_Settings\.AMMO_VANILLA, 0\);') 'no module: only own skill and own ROE apply; faction values and ammunition stay vanilla'
Assert ((Get-Body $t.manager 'static\s+void\s+OnMemberChanged\s*\(') -match 'if \(!IsActive\(\) && !HasOverrides\(\)\)') 'membership changes are followed while any squad or soldier override exists'
Assert ((Get-Body $t.skill 'static\s+void\s+SetGroup\s*\(') -match 'EGS_Manager\.QueueMembers\(squad\);' -and (Get-Body $t.skill 'static\s+void\s+SetUnit\s*\(') -match 'EGS_Manager\.ApplyUnitNow\(soldier\);') 'a squad skill change re-applies its members (budgeted queue); a soldier''s at once'
Assert ($t.skill -match 'static const int MAX_TRACKED = 2048;' -and (Get-Body $t.skill 'protected\s+static\s+void\s+Track\s*\(') -match 'MAX_TRACKED' -and (Get-Body $t.skill 'static\s+void\s+BindPending\s*\(') -match 'RemoveOrdered' -and (Get-Body $t.skill 'static\s+void\s+BindPending\s*\(') -match 's_bBindQueued') 'save registry bounded; binding keeps parallel lists aligned with one retry timer'
# Native save: version 3 only when a skill is set, so 0.1.17 still loads missions without one.
$serialize = Get-Body $t.persistence 'override\s+protected\s+ESerializeResult\s+Serialize\s*\('
$deserialize = Get-Body $t.persistence 'override\s+protected\s+bool\s+Deserialize\s*\('
Assert ($t.persistence -match 'protected static const int VERSION_SKILLS = 3;' -and $serialize -match '(?s)if \(hasSkills\)\s*version = VERSION_SKILLS;' -and $serialize -match 'version >= VERSION_SKILLS && \(!context\.WriteValue\("skillGroupIds"' -and $serialize -match '!hasSkills && EGS_Settings\.IsVanilla\(\)') 'native save: skills in version 3, written only when set'
Assert ($deserialize -match 'version != VERSION_SKILLS' -and $deserialize -match '(?s)if \(version >= VERSION_SKILLS\)\s*\{' -and $deserialize -match 'skillGroupIds\.Count\(\) != skillGroupValues\.Count\(\)' -and $deserialize -match 'EGS_SkillOverrides\.Import\(') 'native load: version 1, 2 and 3; skill arrays validated'
# Caching: keys read only when present.
Assert ((Get-Body $t.cache '(?s)modded class EBG_CacheGroupSnapshot.*?protected\s+void\s+EGS_ReadSkill\s*\(') -match 'if \(!context\.DoesKeyExist\("egsSkill"\)\)\s*return;') 'group snapshots without egsSkill still load'
Assert ((Get-Body $t.cache 'protected\s+void\s+EGS_ReadSurvivorSkills\s*\(') -match 'if \(!context\.DoesKeyExist\("egsSurvivorSkill"\)\)\s*return;' -and (Get-Body $t.cache 'protected\s+void\s+EGS_ReadSurvivorSkills\s*\(') -match 'm_Survivors\.Count\(\)') 'portable snapshots without egsSurvivorSkill still load'
Assert ((Get-Body $t.cache '(?s)modded class EXPG_MemberSnapshot.*?protected\s+void\s+EGS_ReadSkill\s*\(') -match 'if \(!context\.DoesKeyExist\("egsSkill"\)\)\s*return;') 'garrison rows without egsSkill still load'
Assert ($t.cache -match 'EGS_SkillOverrides\.SetUnit\(entity, m_iEGS_Skill, "cache"\);' -and $t.cache -match 'EGS_SkillOverrides\.SetGroup\(group, m_iEGS_Skill, "cache"\);') 'a woken squad and respawned soldiers get their skill back'

# --- D. Rules of engagement without a module (model of EGS_Settings.EffectiveRoe) ----------
$effective = Get-Body $t.settings 'static\s+int\s+EffectiveRoe\s*\('
Assert ((Before $effective 'if (groupOverride == GROUP_ROE_EXEMPT)' 'if (groupOverride >= ROE_RETURN_FIRE && groupOverride <= ROE_WARNING_SHOTS)') -and (Before $effective 'if (groupOverride >= ROE_RETURN_FIRE && groupOverride <= ROE_WARNING_SHOTS)' 'if (!active)')) 'EffectiveRoe: exempt, then the squad''s own choice, then (module only) the default'
function Effective([bool]$Active, [int]$Override, [int]$Global) {
 if ($Override -eq 4) { return 0 }
 if ($Override -ge 1 -and $Override -le 3) { return $Override }
 if (!$Active) { return 0 }
 return [Math]::Min([Math]::Max($Global, 0), 3)
}
Assert ((Effective $false 1 0) -eq 1 -and (Effective $false 3 0) -eq 3 -and (Effective $false 0 3) -eq 0 -and (Effective $false 4 3) -eq 0) 'model: without a module a squad''s Return Fire Only / Warning Shots First apply, the module default does not'
Assert ((Effective $true 0 1) -eq 1 -and (Effective $true 2 1) -eq 2 -and (Effective $true 4 1) -eq 0 -and (Effective $true 0 17) -eq 3) 'model: with a module unchanged'
Assert ((Get-Body $t.manager 'static\s+void\s+OnGameMasterCombatMode\s*\(') -match 'if \(!IsActive\(\) && group\.EGS_GetRoeOverride\(\) == EGS_Settings\.GROUP_ROE_DEFAULT\)') 'vanilla Set combat mode switches a squad with its own EXPBG ROE to Exempt, module or not'

# --- E. Native fixture wired ----------------------------------------------------------------
$regex = '\[EGS SQUAD SOLDIER RESULT\] checks=[1-9]\d* failures=0 squads=[12] registered=12 reason=complete'
Assert ($t.fixture.Contains("-FixturePath tests/EGS_SquadSoldierGameplay.c -TimeoutSeconds 240 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'fixture header carries its runner command'
Assert ($t.fixture -match 'class EXPG_GarrisonGameplayClass : GenericEntityClass \{\}' -and $t.fixture -match 'class EXPG_GarrisonGameplay : GenericEntity') 'fixture keeps the runner''s fixed class names'
$format = [regex]::Match($t.fixture, 'PrintFormat\("(\[EGS SQUAD SOLDIER RESULT\][^"]*)"').Groups[1].Value
Assert ($format -eq '[EGS SQUAD SOLDIER RESULT] checks=%1 failures=%2 squads=%3 registered=%4 reason=%5') "fixture RESULT format changed: $format"
function Line([string[]]$Values) { $line = $format; for ($i = 0; $i -lt $Values.Count; $i++) { $line = $line.Replace("%$($i + 1)", $Values[$i]) }; return $line }
Assert ((Line @('40', '0', '1', '12', 'complete')) -match $regex -and (Line @('60', '0', '2', '12', 'complete')) -match $regex) 'fixture RESULT line matches its -ExpectResult regex (vanilla only, and with -Rhs)'
Assert (!((Line @('40', '1', '1', '12', 'complete')) -match $regex) -and !((Line @('40', '0', '1', '11', 'complete')) -match $regex)) 'a failing or partly registered RESULT line does not match'
Assert ($t.fixture -match 'if \(RhsLoaded\(\)\)' -and $t.fixture -match 'Resource resource = Resource\.Load\(prefab\);' -and $t.fixture -match '!EGS_Module\.HasAny\(\) && ESR_SurrenderModule\.ActiveCount\(\) == 0') 'fixture: no module, RHS only when loaded, Resource.Load kept in a local'

# --- F. Enforce gotchas, ASCII and LF -------------------------------------------------------
foreach ($key in $paths.Keys) {
 $text = $t[$key]; $path = $paths[$key]
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep|Wait)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|IEntity|Managed)\s+(Node|Faction|Group|set|map|array)\b')) "variable named like a vanilla type in $path"
 Assert (![regex]::IsMatch($text, '(?m)^[ \t]*(static[ \t]+|override[ \t]+|protected[ \t]+)*(int|vector|bool|float|string|IEntity|EAIGroupCombatMode|SCR_\w+|array<int>)[ \t]+\w+[ \t]*\([^)\n]*\)[ \t]*\{[ \t]*return')) "non-void returns stay on separate lines: $path"
 Assert (![regex]::IsMatch($text, 'SpawnEntityPrefab\(\s*Resource\.Load\(')) "Resource.Load result kept in a local before spawning: $path"
 if ($key -ne 'fixture') {
  Assert (![regex]::IsMatch($text, '(?m)^[\t ](?![\t ])(protected\s+|private\s+)?(static\s+)?(ref\s+)?(int|bool|float|string|vector|array<[^>]+>|SCR_\w+|ResourceName)\s+(?!m_|s_)\w+\s*;')) "member fields keep m_/s_ prefixes in $path"
 }
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
 Assert (!$text.Contains("`r")) "CR line ending in $path"
}
foreach ($conf in $egsListPath, $esrListPath) { Assert (!(Read-Text $conf).Contains("`r")) "CR line ending in $conf" }

'PASS: squad and soldier skill, rules of engagement and surrender/intel attributes answer for every AI squad and soldier of any faction or mod with or without a module (squad values also through a soldier in the dialog), stay in Edit.conf and are appended in script from identical per-module lists when another mod''s Edit list override drops them, skill soldier > squad > module applied with or without a module and kept by native saves (version 3 only when set), session saves and caching, own ROE without a module; no-module fixture wired.'
