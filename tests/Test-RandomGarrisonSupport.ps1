#requires -Version 7.0
# Portable guard for the Random Garrison support squad filter ("Exclude support squads",
# Unreleased). A catalog squad is a support squad (medical, logistics, ammo, crew or
# essential) by the labels of its group, else by the labels of every soldier, else by
# its file name (EXPG_RGRules.SupportName), classified once while the faction's catalog
# is read, never per pick; a mission maker's Squad prefabs list is used as given.
# Checks the wiring in EXPG_SquadPool.c, the catalog log line, the keyword list against
# false positives (vanilla and installed mod squad names, folders), and a PowerShell
# model of the whole rule over the vanilla USSR, US and FIA GROUP catalogs plus the
# REAPER helicopter crews. The model reads its label and keyword lists from the source,
# so a change to them is checked against the label table below (effective authored and
# auto labels, inheritance resolved, read from the retail data paks; ENTITYTYPE_,
# FACTION_, GROUPSIZE_ and SIZE_ labels left out). No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/random-garrison/Scripts/Game/EXPGR'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
# Comments blanked, strings kept.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\n])*"|//[^\n]*', { param($m) if ($m.Value.StartsWith('//')) { '' } else { $m.Value } })
}
# Braces inside strings (SupportName's "}") do not count.
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $masked = [regex]::Replace($Text, '"(?:\\.|[^"\\\n])*"', { param($m) '"' + (' ' * ($m.Value.Length - 2)) + '"' })
 $open = $masked.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $masked.Length; $i++) {
  if ($masked[$i] -eq '{') { $depth++ } elseif ($masked[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Get-Labels([string]$Body, [string]$Call) {
 @([regex]::Matches($Body, [regex]::Escape($Call) + '\(EEditableEntityLabel\.(\w+)\)') | ForEach-Object { $_.Groups[1].Value })
}

$pool = Get-Code (Read-Text (Join-Path $scripts 'EXPG_SquadPool.c'))
$rules = Get-Code (Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonRules.c'))
$moduleText = Get-Code (Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonModule.c'))

# File name words: lower case, file name only, a Contains chain (no list per call).
$supportName = Get-Body $rules 'static\s+bool\s+SupportName\s*\(\s*string\s+prefabPath\s*\)'
Assert ($supportName -match 'string name = prefabPath;\s*name\.ToLower\(\);' -and $supportName -match 'int cut = name\.LastIndexOf\("/"\);\s*if \(cut < 0\) \{ cut = name\.LastIndexOf\("\}"\); \}\s*if \(cut >= 0\) \{ name = name\.Substring\(cut \+ 1, name\.Length\(\) - cut - 1\); \}') 'SupportName compares the lower-case file name only (after the last / or the GUID)'
Assert (!$supportName.Contains('array<') -and !$supportName.Contains('foreach') -and $rules -notmatch 'static\s+const\s+array<') 'SupportName allocates nothing per call and no static const array is added'
$keywords = @([regex]::Matches($supportName, 'name\.Contains\("([^"]+)"\)') | ForEach-Object { $_.Groups[1].Value })
Assert (($keywords -join ',') -ceq 'medic,ammo,ammunition,suppl,logistic,crew,pilot') "SupportName words changed (justify each in its comment and here): $($keywords -join ',')"
foreach ($word in $keywords) { Assert ($word -ceq $word.ToLowerInvariant() -and $word.Length -ge 4) "support word '$word' must be lower case and at least 4 letters" }
function Test-SupportName([string]$Path, [string[]]$Words = $keywords) {
 $name = $Path.ToLowerInvariant()
 $cut = $name.LastIndexOf('/'); if ($cut -lt 0) { $cut = $name.LastIndexOf('}') }
 if ($cut -ge 0) { $name = $name.Substring($cut + 1) }
 foreach ($word in $Words) { if ($name.Contains($word)) { return $true } }
 $false
}
$supportNames = @(
 '{B10210712040A7C9}Prefabs/Groups/OPFOR/REAPER_USSR_HelicopterCrew.et', '{0000000000000000}Prefabs/Groups/OPFOR/REAPER_USSR_Pilots.et',
 'Prefabs/Groups/BLUFOR/REAPER_US_HelicopterCrew.et', 'Prefabs/Groups/BLUFOR/REAPER_US_Pilots.et', '{0000000000000000}PREFABS/GROUPS/REAPER_USSR_HELICOPTERCREW.ET',
 '{0000000000000000}Group_X_AmmoTeam.et', 'Group_RHS_RF_MSV_VKPO_S_AmmoTeam.et', 'Group_RHS_RF_MSV_Flora_MedicalSection.et', 'Group_USAF_USMC_MEF_D_MedicalSection.et',
 'Group_MEI_AmmoTeam.et', 'Group_X_SupplyTeam.et', 'Group_X_SuppliesTeam.et', 'Group_X_AmmunitionTeam.et', 'Group_X_LogisticsTeam.et', 'Group_X_TankCrew.et', 'Group_X_HeliPilots.et', 'Group_X_Medics.et')
foreach ($path in $supportNames) { Assert (Test-SupportName $path) "a support squad name must match: $path" }
# Infantry names of installed mods (RHS, USMC, MEI; the vanilla ones are in the model
# below) and folder traps: the words never match a folder or an infantry squad.
$infantryNames = @(
 'Group_RHS_RF_MSV_VKPO_DS_RadioReconTeam', 'Group_RHS_RF_MSV_VKPO_S_Team_Suppress', 'Group_RHS_RF_MSV_Flora_ManeuverGroup', 'Group_RHS_ION_COY_QuickReactionForce',
 'Group_RHS_ION_COY_CloseProtectionTeam', 'Group_RHS_ION_COY_SpecialProjectsTeam', 'Group_RHS_ION_BLACK_STATIC_Static_Cell', 'Group_USAF_USMC_FORECON_Squad',
 'Group_USAF_USMC_MARSOC_SniperTeam', 'Group_USAF_USMC_MEF_D_Team_AT', 'Group_MEI_SharpshooterTeam', 'Group_MEI_Defenders', 'Group_US_USMC_Defenders')
foreach ($path in @($infantryNames | ForEach-Object { "Prefabs/Groups/$_.et" }) + @('{0000000000000000}Prefabs/Groups/Crew/Medics/Group_X_FireTeam.et', 'Prefabs/Groups/Pilots_Ammo_Supply/Group_X_RifleSquad.et', 'Prefabs/Groups/OPFOR/Group_USSR_Transport.et')) {
 Assert (!(Test-SupportName $path)) "an infantry squad name (or a folder) must not match: $path"
}

# Group labels: medical, logistics, essential, vehicle or helicopter crew; never rearming.
$supportLabels = Get-Body $pool 'protected\s+static\s+bool\s+SupportLabels\s*\(\s*SCR_EntityCatalogEntry\s+entry\s*\)'
$groupLabels = Get-Labels $supportLabels 'entry.HasEditableEntityLabel'
Assert (($groupLabels -join ',') -ceq 'TRAIT_MEDICAL,TRAIT_LOGISTICS,TRAIT_ESSENTIAL,GROUPTYPE_ESSENTIAL,TRAIT_VEHICLE_CREW,TRAIT_HELI_CREW') "group support labels changed: $($groupLabels -join ',')"
Assert (!$supportLabels.Contains('TRAIT_REARMING')) 'TRAIT_REARMING is no group support label (vanilla rifle squads, machine gun and AT teams carry it)'
$beginFaction = Get-Body $pool 'void\s+BeginFaction\s*\('
Assert ($beginFaction -match 'AddCandidate\(entry\.GetPrefab\(\), SupportLabels\(entry\)\);' -and (Get-Body $pool 'void\s+BeginExplicit\s*\(') -match 'AddCandidate\(prefab, false\);') 'catalog entries take their group labels; a Squad prefabs list is never classified'
# Soldier labels: medic, ammo bearer, vehicle or helicopter crew, logistics; read once each.
$supportMember = Get-Body $pool 'protected\s+bool\s+SupportMember\s*\(\s*ResourceName\s+soldier\s*\)'
$memberLabels = Get-Labels $supportMember 'info.HasEntityLabel'
Assert (($memberLabels -join ',') -ceq 'TRAIT_MEDICAL,ROLE_MEDIC,TRAIT_REARMING,ROLE_AMMOBEARER,TRAIT_VEHICLE_CREW,TRAIT_HELI_CREW,TRAIT_LOGISTICS') "soldier support labels changed: $($memberLabels -join ',')"
Assert ($supportMember -match 'if \(m_MemberSupport\.Find\(path, support\)\)\s*\{\s*return support;' -and $supportMember.IndexOf('m_MemberSupport.Find(') -lt $supportMember.IndexOf('Resource.Load(') -and $supportMember -match 'm_MemberSupport\.Insert\(path, support\);\s*return support;') 'a soldier prefab is loaded once per catalog and its verdict cached'
Assert ($supportMember -match 'SCR_EditableEntityComponentClass\.GetEditableEntitySource\(resource\)' -and $supportMember -match 'infoSource = editableSource\.GetObject\("m_UIInfo"\);' -and $supportMember -match 'info = SCR_EditableEntityUIInfo\.Cast\(BaseContainerTools\.CreateInstanceFromContainer\(infoSource\)\);' -and $supportMember -match 'if \(info\)' -and !$pool.Contains('ExtractEditableUIInfoFromPrefab') -and !$pool.Contains('GetInfo(')) 'soldier labels come from the editable component UI info, null-safe (no GetInfo, which calls into a UI info of another class; no ExtractEditableUIInfoFromPrefab, which logs errors)'
Assert ($pool -match '(?m)^ protected ref map<string, bool> m_MemberSupport = new map<string, bool>\(\);' -and $pool -notmatch 'static[^;\n]*m_MemberSupport') 'the soldier cache is an instance field (no static initializer)'
# Every soldier, never any: one soldier without a support label keeps the squad.
$roster = Get-Body $pool 'protected\s+bool\s+SupportRoster\s*\(\s*ResourceName\s+prefab\s*\)'
Assert ($roster -match 'source\.Get\("m_aUnitPrefabSlots", slots\)' -and $roster -match 'slots\.IsEmpty\(\)\)\s*\{\s*return false;' -and $roster -match 'foreach \(ResourceName slot : slots\)\s*\{\s*if \(!SupportMember\(slot\)\)\s*\{\s*return false;\s*\}\s*\}\s*return true;\s*$') 'SupportRoster: support only when every soldier has a support label'
# Once per squad while the catalog is read (in Step's deadline budget), never per pick.
$step = Get-Body $pool 'bool\s+Step\s*\(\s*int\s+deadline\s*\)'
$classify = 'if (!Explicit && !entry.Support) { entry.Support = SupportRoster(prefab) || EXPG_RGRules.SupportName(path); }'
Assert ($step.Contains('entry.Support = m_Support.Get(path);') -and $step.Contains($classify) -and $step.IndexOf('EXPG_SquadPrefab.Validate(') -lt $step.IndexOf($classify) -and $step.IndexOf($classify) -lt $step.IndexOf('Entries.Insert(entry);')) 'Step classifies each validated catalog squad: group labels, then every soldier, then the file name'
Assert ($step -match 'if \(entry\.Support\) \{ SupportCount\+\+; \}' -and $step -match 'Ready = true;\s*m_MemberSupport\.Clear\(\);') 'Step counts the support squads and drops the soldier cache when the catalog is ready'
Assert ([regex]::Matches($pool, '\bSupportRoster\(').Count -eq 2 -and [regex]::Matches($pool, '\bSupportName\(').Count -eq 1 -and [regex]::Matches($pool, '\bSupportMember\(').Count -eq 2 -and [regex]::Matches($pool, '\bSupportLabels\(').Count -eq 2) 'the classifier is called only from BeginFaction and Step'
foreach ($reader in 'protected\s+bool\s+Usable\s*\(', 'int\s+Collect\s*\(', 'bool\s+Fits\s*\(', 'int\s+CountUsable\s*\(') {
 Assert ((Get-Body $pool $reader) -notmatch 'Support(Roster|Name|Member|Labels)\(|Resource\.Load|HasEditableEntityLabel') "no classification per pick: $reader"
}
Assert ($moduleText -notmatch 'Support(Roster|Name|Member|Labels)\(|SupportCount') 'the module reads only EXPG_SquadEntry.Support'
# One catalog log line, with the support count.
Assert ([regex]::Matches($pool, '\bPrint(Format)?\s*\(').Count -eq 1 -and $step.Contains('string supportText = string.Format("%1 of them support squads (skipped while Exclude support squads is on)", SupportCount);') -and $step.Contains('if (Explicit) { supportText = "support squads not classified (explicit list)"; }') -and $step.Contains('PrintFormat("[EXPG RANDOM] squad catalog %1: %2 of %3 squads usable, %4", Describe(), Entries.Count(), m_Paths.Count(), supportText);')) 'one catalog log line: usable squads and how many of them are support squads'

# Model of the whole rule over the vanilla GROUP catalogs (USSR, US, FIA; the NotSpawned
# ambient patrols left out: they wait for a script to spawn their soldiers), the REAPER
# helicopter crews (no support labels at all) and three made-up crews of vanilla soldiers.
$soldiers = @{}
foreach ($row in @(
 ,@('TRAIT_VEHICLE_CREW TRAIT_LOGISTICS', 'Campaign_USSR_Player_Driver Campaign_US_Player_Driver')
 ,@('TRAIT_REARMING ROLE_AMMOBEARER', 'Character_FIA_AAT Character_FIA_AMG Character_FIA_Ammo Character_USSR_AAT Character_USSR_AAT_Guard Character_USSR_AAT_KLMK Character_USSR_AMG Character_USSR_AMG_KLMK Character_USSR_Ammo Character_USSR_Ammo_KLMK Character_USSR_NI_AAT Character_USSR_NI_AMG Character_USSR_NI_Ammo Character_US_AMG Character_US_Ammo')
 ,@('TRAIT_ARMORPIERCING ROLE_ANTITANK', 'Character_FIA_AT Character_FIA_LAT Character_USSR_AT Character_USSR_AT_Guard Character_USSR_AT_KLMK Character_USSR_LAT Character_USSR_LAT_KLMK Character_USSR_NI_AT Character_USSR_NI_LAT Character_US_LAT Character_US_LAT_Guard')
 ,@('TRAIT_SUPPRESSIVE ROLE_MACHINEGUNNER', 'Character_FIA_MG Character_USSR_AR Character_USSR_AR_Guard Character_USSR_AR_KLMK Character_USSR_MG Character_USSR_MG_KLMK Character_USSR_NI_AR Character_USSR_NI_MG Character_US_AR Character_US_AR_Guard Character_US_MG')
 ,@('TRAIT_MEDICAL ROLE_MEDIC', 'Character_FIA_Medic Character_USSR_Medic Character_USSR_Medic_KLMK Character_USSR_NI_Medic Character_US_Medic')
 ,@('ROLE_LEADER', 'Character_FIA_PL Character_FIA_SL Character_USSR_NI_PL Character_USSR_NI_Sergeant Character_USSR_PL Character_USSR_PL_KLMK Character_USSR_Sergeant Character_USSR_Sergeant_KLMK Character_US_PL Character_US_SF_SL Character_US_SF_SL_S Character_US_SL Character_US_Sergeant Character_US_TL Character_US_TL_Guard')
 ,@('TRAIT_RADIO ROLE_RADIOOPERATOR', 'Character_FIA_RTO Character_USSR_NI_RTO Character_USSR_RTO Character_USSR_RTO_KLMK Character_US_RTO')
 ,@('ROLE_RIFLEMAN', 'Character_FIA_Rifleman Character_USSR_NI_Rifleman Character_USSR_NI_SR Character_USSR_Rifleman Character_USSR_Rifleman_KLMK Character_USSR_SF Character_USSR_SF_S Character_USSR_SF_SL Character_USSR_SF_SL_S Character_USSR_SR Character_USSR_SR_KLMK Character_US_Rifleman Character_US_SF Character_US_SF_S Character_US_SF_Sharpshooter Character_US_SF_Sharpshooter_S')
 ,@('ROLE_SAPPER TRAIT_EXPLOSIVE', 'Character_FIA_Sapper Character_USSR_Engineer Character_USSR_NI_Sapper Character_USSR_SF_Sapper Character_USSR_SF_Sapper_S Character_USSR_Sapper Character_USSR_Sapper_KLMK Character_US_Engineer Character_US_SF_Sapper Character_US_SF_Sapper_S Character_US_Sapper')
 ,@('ROLE_SCOUT', 'Character_FIA_Scout Character_USSR_Scout Character_US_Scout')
 ,@('TRAIT_OPTICS ROLE_SHARPSHOOTER', 'Character_FIA_Sharpshooter Character_USSR_NI_Sharpshooter Character_USSR_Sharpshooter Character_USSR_Sharpshooter_KLMK')
 ,@('TRAIT_EXPLOSIVE ROLE_GRENADIER', 'Character_USSR_GL Character_USSR_GL_KLMK Character_USSR_NI_GL Character_US_GL Character_US_GL_Guard')
 ,@('ROLE_LEADER TRAIT_EXPLOSIVE', 'Character_USSR_NI_SL Character_USSR_SL Character_USSR_SL_Guard Character_USSR_SL_KLMK')
 ,@('ROLE_MACHINEGUNNER', 'Character_USSR_SF_LMG Character_US_SF_LMG')
 ,@('ROLE_MEDIC', 'Character_USSR_SF_Medic Character_USSR_SF_Medic_S Character_US_SF_Medic Character_US_SF_Medic_S')
 ,@('ROLE_RADIOOPERATOR', 'Character_USSR_SF_RTO Character_USSR_SF_RTO_S Character_US_SF_RTO Character_US_SF_RTO_S')
 ,@('ROLE_SHARPSHOOTER', 'Character_USSR_SF_Sharpshooter Character_USSR_SF_Sharpshooter_S')
 ,@('ROLE_SCOUT ROLE_RADIOOPERATOR', 'Character_USSR_Scout_RTO Character_US_Scout_RTO')
 ,@('TRAIT_HELI_CREW', 'Character_USSR_HeliPilot Character_USSR_HeliCrew Character_US_HeliPilot')
 ,@('TRAIT_VEHICLE_CREW', 'Character_USSR_Crew Character_US_Crew')
 ,@('', 'REAPER_USSR_Pilot REAPER_USSR_Pilot2')
)) { foreach ($soldier in $row[1] -split ' ') { $soldiers[$soldier] = @($row[0] -split ' ' | Where-Object { $_ }) } }
# Squad file name, its group labels, its soldiers.
$squads = @(
 ,@('Group_USSR_RifleSquad', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE TRAIT_REARMING', 'Character_USSR_SL Character_USSR_AR Character_USSR_AT Character_USSR_AAT Character_USSR_SR Character_USSR_LAT')
 ,@('Group_USSR_FireGroup', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_USSR_SL Character_USSR_AR Character_USSR_AT Character_USSR_AAT')
 ,@('Group_USSR_FireGroup_Guard', 'TRAIT_ESSENTIAL TRAIT_GUARD GROUPTYPE_ESSENTIAL', 'Character_USSR_SL_Guard Character_USSR_AR_Guard Character_USSR_AT_Guard Character_USSR_AAT_Guard')
 ,@('Group_USSR_ManeuverGroup', 'TRAIT_EXPLOSIVE', 'Character_USSR_SR Character_USSR_LAT')
 ,@('Group_USSR_LightFireTeam', '', 'Character_USSR_SR Character_USSR_Rifleman Character_USSR_Rifleman Character_USSR_Rifleman')
 ,@('Group_USSR_Team_GL', 'TRAIT_EXPLOSIVE TRAIT_SUPPRESSIVE', 'Character_USSR_SR Character_USSR_AR Character_USSR_GL Character_USSR_GL')
 ,@('Group_USSR_Team_LAT', 'TRAIT_ARMORPIERCING', 'Character_USSR_SR Character_USSR_AR Character_USSR_LAT Character_USSR_LAT')
 ,@('Group_USSR_Team_AT', 'TRAIT_ARMORPIERCING TRAIT_REARMING', 'Character_USSR_SR Character_USSR_AT Character_USSR_AT Character_USSR_AAT')
 ,@('Group_USSR_Team_Suppress', 'TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE', 'Character_USSR_SR Character_USSR_AR Character_USSR_AR Character_USSR_GL')
 ,@('Group_USSR_MachineGunTeam', 'TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_USSR_MG Character_USSR_AMG')
 ,@('Group_USSR_SentryTeam', '', 'Character_USSR_Rifleman Character_USSR_Rifleman')
 ,@('Group_USSR_PlatoonHQ', '', 'Character_USSR_PL Character_USSR_Sergeant Character_USSR_RTO Character_USSR_Medic Character_USSR_Sharpshooter')
 ,@('Group_USSR_MedicalSection', 'TRAIT_MEDICAL', 'Character_USSR_Medic Character_USSR_Medic')
 ,@('Group_USSR_AmmoTeam', '', 'Character_USSR_AMG Character_USSR_Ammo Character_USSR_Ammo Character_USSR_AAT')
 ,@('Group_USSR_SapperTeam', '', 'Character_USSR_Sapper Character_USSR_Sapper')
 ,@('Group_USSR_RifleSquad_KLMK', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE TRAIT_REARMING', 'Character_USSR_SL_KLMK Character_USSR_AR_KLMK Character_USSR_AT_KLMK Character_USSR_AAT_KLMK Character_USSR_SR_KLMK Character_USSR_LAT_KLMK')
 ,@('Group_USSR_FireGroup_KLMK', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_USSR_SL_KLMK Character_USSR_AR_KLMK Character_USSR_AT_KLMK Character_USSR_AAT_KLMK')
 ,@('Group_USSR_ManeuverGroup_KLMK', 'TRAIT_EXPLOSIVE', 'Character_USSR_SR_KLMK Character_USSR_LAT_KLMK')
 ,@('Group_USSR_LightFireTeam_KLMK', '', 'Character_USSR_SR_KLMK Character_USSR_Rifleman_KLMK Character_USSR_Rifleman_KLMK Character_USSR_Rifleman_KLMK')
 ,@('Group_USSR_Team_GL_KLMK', 'TRAIT_EXPLOSIVE TRAIT_SUPPRESSIVE', 'Character_USSR_SR_KLMK Character_USSR_AR_KLMK Character_USSR_GL_KLMK Character_USSR_GL_KLMK')
 ,@('Group_USSR_Team_LAT_KLMK', 'TRAIT_ARMORPIERCING', 'Character_USSR_SR_KLMK Character_USSR_AR_KLMK Character_USSR_LAT_KLMK Character_USSR_LAT_KLMK')
 ,@('Group_USSR_Team_AT_KLMK', 'TRAIT_ARMORPIERCING TRAIT_REARMING', 'Character_USSR_SR_KLMK Character_USSR_AT_KLMK Character_USSR_AT_KLMK Character_USSR_AAT_KLMK')
 ,@('Group_USSR_Team_Suppress_KLMK', 'TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE', 'Character_USSR_SR_KLMK Character_USSR_AR_KLMK Character_USSR_AR_KLMK Character_USSR_GL_KLMK')
 ,@('Group_USSR_MachineGunTeam_KLMK', 'TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_USSR_MG_KLMK Character_USSR_AMG_KLMK')
 ,@('Group_USSR_SentryTeam_KLMK', '', 'Character_USSR_Rifleman_KLMK Character_USSR_Rifleman_KLMK')
 ,@('Group_USSR_ReconTeam', '', 'Character_USSR_Scout Character_USSR_Scout_RTO')
 ,@('Group_USSR_PlatoonHQ_KLMK', '', 'Character_USSR_PL_KLMK Character_USSR_Sergeant_KLMK Character_USSR_RTO_KLMK Character_USSR_Medic_KLMK Character_USSR_Sharpshooter_KLMK')
 ,@('Group_USSR_MedicalSection_KLMK', 'TRAIT_MEDICAL', 'Character_USSR_Medic_KLMK Character_USSR_Medic_KLMK')
 ,@('Group_USSR_AmmoTeam_KLMK', '', 'Character_USSR_AMG_KLMK Character_USSR_Ammo_KLMK Character_USSR_Ammo_KLMK Character_USSR_AAT_KLMK')
 ,@('Group_USSR_SapperTeam_KLMK', '', 'Character_USSR_Sapper_KLMK Character_USSR_Sapper_KLMK')
 ,@('Group_USSR_RifleSquad_NI', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE TRAIT_REARMING', 'Character_USSR_NI_SL Character_USSR_NI_AR Character_USSR_NI_AT Character_USSR_NI_AAT Character_USSR_NI_SR Character_USSR_NI_LAT')
 ,@('Group_USSR_FireGroup_NI', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_USSR_NI_SL Character_USSR_NI_AR Character_USSR_NI_AT Character_USSR_NI_AAT')
 ,@('Group_USSR_ManeuverGroup_NI', 'TRAIT_EXPLOSIVE', 'Character_USSR_NI_SR Character_USSR_NI_LAT')
 ,@('Group_USSR_LightFireTeam_NI', '', 'Character_USSR_NI_SR Character_USSR_NI_Rifleman Character_USSR_NI_Rifleman Character_USSR_NI_Rifleman')
 ,@('Group_USSR_Team_GL_NI', 'TRAIT_EXPLOSIVE TRAIT_SUPPRESSIVE', 'Character_USSR_NI_SR Character_USSR_NI_AR Character_USSR_NI_GL Character_USSR_NI_GL')
 ,@('Group_USSR_Team_LAT_NI', 'TRAIT_ARMORPIERCING', 'Character_USSR_NI_SR Character_USSR_NI_AR Character_USSR_NI_LAT Character_USSR_NI_LAT')
 ,@('Group_USSR_Team_AT_NI', 'TRAIT_ARMORPIERCING TRAIT_REARMING', 'Character_USSR_NI_SR Character_USSR_NI_AT Character_USSR_NI_AT Character_USSR_NI_AAT')
 ,@('Group_USSR_Team_Suppress_NI', 'TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE', 'Character_USSR_NI_SR Character_USSR_NI_AR Character_USSR_NI_AR Character_USSR_NI_GL')
 ,@('Group_USSR_MachineGunTeam_NI', 'TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_USSR_NI_MG Character_USSR_NI_AMG')
 ,@('Group_USSR_SentryTeam_NI', '', 'Character_USSR_NI_Rifleman Character_USSR_NI_Rifleman')
 ,@('Group_USSR_PlatoonHQ_NI', '', 'Character_USSR_NI_PL Character_USSR_NI_Sergeant Character_USSR_NI_RTO Character_USSR_NI_Medic Character_USSR_NI_Sharpshooter')
 ,@('Group_USSR_MedicalSection_NI', 'TRAIT_MEDICAL', 'Character_USSR_NI_Medic Character_USSR_NI_Medic')
 ,@('Group_USSR_AmmoTeam_NI', '', 'Character_USSR_NI_AMG Character_USSR_NI_Ammo Character_USSR_NI_Ammo Character_USSR_NI_AAT')
 ,@('Group_USSR_SapperTeam_NI', '', 'Character_USSR_NI_Sapper Character_USSR_NI_Sapper')
 ,@('Group_USSR_Spetsnaz_SentryTeam', '', 'Character_USSR_SF Character_USSR_SF')
 ,@('Group_USSR_Spetsnaz_Squad', '', 'Character_USSR_SF_SL Character_USSR_SF_LMG Character_USSR_SF_Sharpshooter Character_USSR_SF_Sapper Character_USSR_SF_Medic Character_USSR_SF_RTO')
 ,@('Group_USSR_Spetsnaz_ReconTeam', '', 'Character_USSR_SF_S Character_USSR_SF_S')
 ,@('Group_USSR_Spetsnaz_ReconSquad', '', 'Character_USSR_SF_SL_S Character_USSR_SF_S Character_USSR_SF_Sharpshooter_S Character_USSR_SF_Sapper_S Character_USSR_SF_Medic_S Character_USSR_SF_RTO_S')
 ,@('Group_USSR_Transport', 'TRAIT_ESSENTIAL TRAIT_LOGISTICS GROUPTYPE_ESSENTIAL', 'Campaign_USSR_Player_Driver Campaign_USSR_Player_Driver')
 ,@('Group_USSR_EngineerTeam', '', 'Character_USSR_Engineer Character_USSR_Engineer')
 ,@('Group_US_RifleSquad', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE', 'Character_US_SL Character_US_TL Character_US_TL Character_US_AR Character_US_AR Character_US_GL Character_US_GL Character_US_LAT Character_US_LAT')
 ,@('Group_US_FireTeam', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE', 'Character_US_TL Character_US_AR Character_US_GL Character_US_LAT')
 ,@('Group_US_FireTeam_Guard', 'TRAIT_ESSENTIAL TRAIT_GUARD GROUPTYPE_ESSENTIAL', 'Character_US_TL_Guard Character_US_AR_Guard Character_US_GL_Guard Character_US_LAT_Guard')
 ,@('Group_US_LightFireTeam', '', 'Character_US_TL Character_US_Rifleman Character_US_Rifleman Character_US_Rifleman')
 ,@('Group_US_Team_GL', 'TRAIT_EXPLOSIVE TRAIT_SUPPRESSIVE', 'Character_US_TL Character_US_AR Character_US_GL Character_US_GL')
 ,@('Group_US_Team_LAT', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE', 'Character_US_TL Character_US_AR Character_US_LAT Character_US_LAT')
 ,@('Group_US_Team_Suppress', 'TRAIT_SUPPRESSIVE TRAIT_EXPLOSIVE', 'Character_US_TL Character_US_AR Character_US_AR Character_US_GL')
 ,@('Group_US_MachineGunTeam', 'TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_US_MG Character_US_AMG')
 ,@('Group_US_SentryTeam', '', 'Character_US_Rifleman Character_US_Rifleman')
 ,@('Group_US_ReconTeam', '', 'Character_US_Scout Character_US_Scout_RTO')
 ,@('Group_US_PlatoonHQ', '', 'Character_US_PL Character_US_Sergeant Character_US_RTO Character_US_Medic')
 ,@('Group_US_MedicalSection', 'TRAIT_MEDICAL', 'Character_US_Medic Character_US_Medic')
 ,@('Group_US_AmmoTeam', '', 'Character_US_AMG Character_US_AMG Character_US_Ammo Character_US_Ammo')
 ,@('Group_US_SapperTeam', '', 'Character_US_Sapper Character_US_Sapper')
 ,@('Group_US_GreenBeret_SentryTeam', '', 'Character_US_SF Character_US_SF')
 ,@('Group_US_GreenBeret_Squad', '', 'Character_US_SF_SL Character_US_SF_LMG Character_US_SF_Sharpshooter Character_US_SF_Sapper Character_US_SF_RTO Character_US_SF_Medic')
 ,@('Group_US_GreenBeret_ReconTeam', '', 'Character_US_SF_S Character_US_SF_S')
 ,@('Group_US_GreenBeret_ReconSquad', '', 'Character_US_SF_SL_S Character_US_SF_S Character_US_SF_Sharpshooter_S Character_US_SF_Sapper_S Character_US_SF_Medic_S Character_US_SF_RTO_S')
 ,@('Group_US_Transport', 'TRAIT_ESSENTIAL TRAIT_LOGISTICS GROUPTYPE_ESSENTIAL', 'Campaign_US_Player_Driver Campaign_US_Player_Driver')
 ,@('Group_US_EngineerTeam', '', 'Character_US_Engineer Character_US_Engineer')
 ,@('Group_FIA_RifleSquad', 'TRAIT_ARMORPIERCING TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_FIA_SL Character_FIA_MG Character_FIA_AMG Character_FIA_AT Character_FIA_Rifleman Character_FIA_Rifleman Character_FIA_LAT')
 ,@('Group_FIA_FireTeam', 'TRAIT_ARMORPIERCING', 'Character_FIA_SL Character_FIA_AT Character_FIA_Rifleman Character_FIA_Rifleman Character_FIA_LAT')
 ,@('Group_FIA_LightFireTeam', '', 'Character_FIA_Rifleman Character_FIA_Rifleman Character_FIA_Rifleman Character_FIA_Rifleman')
 ,@('Group_FIA_Team_LAT', 'TRAIT_ARMORPIERCING', 'Character_FIA_Rifleman Character_FIA_Rifleman Character_FIA_LAT Character_FIA_LAT')
 ,@('Group_FIA_Team_AT', '', 'Character_FIA_Rifleman Character_FIA_AT Character_FIA_AT Character_FIA_AAT')
 ,@('Group_FIA_MachineGunTeam', 'TRAIT_SUPPRESSIVE TRAIT_REARMING', 'Character_FIA_MG Character_FIA_AMG')
 ,@('Group_FIA_SentryTeam', '', 'Character_FIA_Rifleman Character_FIA_Rifleman')
 ,@('Group_FIA_ReconTeam', 'TRAIT_RADIO', 'Character_FIA_Scout Character_FIA_RTO')
 ,@('Group_FIA_SharpshooterTeam', 'TRAIT_OPTICS', 'Character_FIA_Scout Character_FIA_Sharpshooter')
 ,@('Group_FIA_PlatoonHQ', 'TRAIT_RADIO TRAIT_MEDICAL', 'Character_FIA_PL Character_FIA_RTO Character_FIA_Medic')
 ,@('Group_FIA_MedicalSection', '', 'Character_FIA_Medic Character_FIA_Medic')
 ,@('Group_FIA_AmmoTeam', '', 'Character_FIA_AMG Character_FIA_Ammo Character_FIA_Ammo Character_FIA_AAT')
 ,@('Group_FIA_SapperTeam', '', 'Character_FIA_Sapper Character_FIA_Sapper')
 ,@('REAPER_USSR_HelicopterCrew', '', 'REAPER_USSR_Pilot REAPER_USSR_Pilot REAPER_USSR_Pilot2 REAPER_USSR_Pilot2')
 ,@('REAPER_USSR_Pilots', '', 'REAPER_USSR_Pilot REAPER_USSR_Pilot')
 ,@('Group_X_Aviators', '', 'Character_USSR_HeliPilot Character_USSR_HeliCrew')
 ,@('Group_X_Armour', '', 'Character_US_Crew Character_US_Crew Character_US_Crew')
 ,@('Group_X_Escort', '', 'Character_USSR_HeliPilot Character_USSR_Rifleman')
)
$expected = @(
 'Group_USSR_FireGroup_Guard', 'Group_USSR_MedicalSection', 'Group_USSR_AmmoTeam', 'Group_USSR_MedicalSection_KLMK', 'Group_USSR_AmmoTeam_KLMK',
 'Group_USSR_MedicalSection_NI', 'Group_USSR_AmmoTeam_NI', 'Group_USSR_Transport', 'Group_US_FireTeam_Guard', 'Group_US_MedicalSection', 'Group_US_AmmoTeam',
 'Group_US_Transport', 'Group_FIA_PlatoonHQ', 'Group_FIA_MedicalSection', 'Group_FIA_AmmoTeam', 'REAPER_USSR_HelicopterCrew', 'REAPER_USSR_Pilots',
 'Group_X_Aviators', 'Group_X_Armour')
$byName = @('REAPER_USSR_HelicopterCrew', 'REAPER_USSR_Pilots')
function Test-Support($Squad, [string[]]$Words) {
 $labels = @($Squad[1] -split ' ' | Where-Object { $_ })
 if (@($labels | Where-Object { $groupLabels -ccontains $_ }).Count) { return $true }
 $members = @($Squad[2] -split ' ' | Where-Object { $_ })
 $roster = $members.Count -gt 0
 foreach ($member in $members) {
  Assert $soldiers.ContainsKey($member) "label table misses $member"
  if (!@($soldiers[$member] | Where-Object { $memberLabels -ccontains $_ }).Count) { $roster = $false }
 }
 if ($roster) { return $true }
 Test-SupportName "Prefabs/Groups/$($Squad[0]).et" $Words
}
$support = @()
foreach ($squad in $squads) {
 $verdict = Test-Support $squad $keywords
 $dataOnly = Test-Support $squad @()
 Assert ($verdict -eq ($expected -ccontains $squad[0])) "$($squad[0]) must be $(if ($expected -ccontains $squad[0]) { 'a support squad' } else { 'infantry' })"
 # Labels alone catch every vanilla support squad; the file name decides only for the REAPER crews.
 Assert ($dataOnly -eq ($verdict -and $byName -cnotcontains $squad[0])) "$($squad[0]): the file name may decide only for squads without support labels"
 if ($verdict) { $support += $squad[0] }
}
$ussr = @($squads | Where-Object { $_[0] -clike 'Group_USSR_*' -or $_[0] -clike 'REAPER_USSR_*' })
$ussrSupport = @($support | Where-Object { $_ -clike 'Group_USSR_*' -or $_ -clike 'REAPER_USSR_*' })
Assert ($ussr.Count -eq 52 -and $ussrSupport.Count -eq 10) "the production USSR catalog (50 vanilla + 2 REAPER) logs 10 support squads: $($ussrSupport.Count) of $($ussr.Count)"
foreach ($kept in 'Group_USSR_RifleSquad', 'Group_USSR_MachineGunTeam', 'Group_USSR_Team_AT', 'Group_USSR_Team_Suppress', 'Group_USSR_Spetsnaz_Squad', 'Group_USSR_ReconTeam', 'Group_FIA_SharpshooterTeam', 'Group_USSR_FireGroup_NI', 'Group_USSR_FireGroup_KLMK', 'Group_US_PlatoonHQ', 'Group_USSR_SapperTeam', 'Group_US_EngineerTeam') {
 Assert ($support -cnotcontains $kept) "$kept stays a garrison squad"
}

# Settings and wording: the key and its saved value are unchanged; the texts name the kinds.
$list = Read-Text (Join-Path $repo 'addon/random-garrison/Configs/Editor/AttributeLists/Edit.conf')
$exclude = [regex]::Match($list, '(?s)  EXPG_RGExcludeSupportAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($exclude -match 'm_Key 2\b' -and $moduleText -match 'static const int KEY_EXCLUDE_SUPPORT = 2;' -and $moduleText -match '\[Attribute\("1", UIWidgets\.CheckBox, "Exclude support squads: medical, logistics, ammo, crew and essential squads[^"]*"' -and $moduleText -match 'protected bool m_bExcludeSupport;') 'Exclude support squads keeps key 2, default on and its saved 0/1 value'
Assert ($exclude -match 'Name "Exclude support squads"' -and $exclude -match 'Description "Skip medical, logistics, ammo and crew squads[^"]*"') 'the attribute text names medical, logistics, ammo and crew squads'
$readme = Read-Text (Join-Path $repo 'README.md')
Assert ($readme -match 'Support squads \(medical, logistics,\s+ammo and crew squads') 'README names the support squad kinds'
$changelog = Read-Text (Join-Path $repo 'CHANGELOG.md')
# The section of the release that shipped the filter (0.1.15) describes it.
$shipped = [regex]::Match($changelog, '(?s)\n## 0\.1\.15\n(.*?)(\n## |\z)')
Assert ($shipped.Success -and $shipped.Groups[1].Value -match 'Random Garrison' -and $shipped.Groups[1].Value -match 'Exclude support squads') 'the CHANGELOG section 0.1.15 (the release that shipped it) describes the support squad filter'
$contract = Read-Text (Join-Path $repo 'tests/EXPG_RandomGarrisonTest.c')
Assert ($contract -match 'static\s+bool\s+SupportNames\s*\(\s*\)' -and (Get-Body $contract 'static\s+bool\s+Rules\s*\(\s*\)') -match 'if \(!SupportNames\(\)\)\s*\{\s*return false;\s*\}\s*return Usable\(\);') 'the native Rules contract runs SupportNames'
foreach ($path in $PSCommandPath, (Join-Path $repo 'tests/EXPG_RandomGarrisonTest.c')) {
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0 -and !($bytes -contains 13)) "ASCII with LF: $path"
}
"PASS: Random Garrison support squads: group labels ($($groupLabels.Count)), every soldier's labels ($($memberLabels.Count)), then the file name ($($keywords -join ', ')); classified once per catalog squad, explicit lists as given; model over $($squads.Count) squads ($($support.Count) support; USSR production catalog $($ussrSupport.Count) of $($ussr.Count)); no false positive in $($infantryNames.Count) mod infantry names or folders. No engine was launched."
