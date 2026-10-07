#requires -Version 7.0
# Portable guard for the AI Surrender commander grenade (Unreleased): two GM settings,
# "Commander: grenade suicide instead of surrender (%)" (key 10, default 0) and
# "Commander must carry a grenade" (key 11, default ON), replicated, saved and restored
# like the other ten; esrVersion 1 saves (ten values) and esrVersion 2 saves (twelve, before
# the intel setting of tests/Test-IntelReveal.ps1) still load. The grenade roll happens
# once for the squad leader, only when he would surrender; he crouches with his AI off,
# a vanilla frag (his own, or RGD-5/M67 when "must carry" is off) is placed at his feet
# and set live through the vanilla BaseTriggerComponent.SetLive() one second later.
# Also checks tests/ESR_CommanderGrenadeGameplay.c is wired to the runner and its RESULT
# line matches its -ExpectResult regex. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Join-Path $repo 'addon/ai-surrender'
$scripts = Join-Path $module 'Scripts/Game/EXPSR'
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

$settingsPath = Join-Path $scripts 'ESR_Settings.c'
$modulePath = Join-Path $scripts 'ESR_SurrenderModule.c'
$attributesPath = Join-Path $scripts 'ESR_Attributes.c'
$managerPath = Join-Path $scripts 'ESR_SurrenderManager.c'
$listPath = Join-Path $module 'Configs/Editor/AttributeLists/Edit.conf'
$fixturePath = Join-Path $repo 'tests/ESR_CommanderGrenadeGameplay.c'
$settings = Read-Text $settingsPath
$moduleText = Read-Text $modulePath
$attributes = Read-Text $attributesPath
$manager = Read-Text $managerPath
$list = Read-Text $listPath
$fixture = Read-Text $fixturePath

# Settings: keys, count, defaults, switch.
Assert ($settings -match 'static const int GRENADE = 10;' -and $settings -match 'static const int GRENADE_CARRY = 11;') 'commander settings must use keys 10 and 11'
Assert ($settings -match 'static const int COUNT_V2 = 12;' -and $settings -match 'static const int COUNT_V1 = 10;') 'twelve settings in esrVersion 2 saves, ten in esrVersion 1 saves'
$default = Get-Body $settings 'static\s+int\s+Default\s*\(\s*int\s+key\s*\)'
Assert ($default -match 'if \(key == GRENADE\) return 0;' -and $default -match 'if \(key == GRENADE_CARRY\) return 1;') 'commander grenade defaults to 0% and must carry to ON'
Assert ($default -match 'if \(key == REVEAL\) return 40;' -and $default -match 'if \(key == IDENTITY\) return 40;' -and $default -match 'if \(key == CHANCE\) return 30;') 'existing defaults unchanged'
Assert ($settings -match 'static bool IsBoolean\(int key\) \{ return key == ENABLED \|\| key == DIAGNOSTICS \|\| key == GRENADE_CARRY; \}') 'must carry must be a switch'
Assert ((Get-Body $settings 'static\s+void\s+Adopt\s*\(') -match 'if \(values\.IsIndexValid\(key\)\) value = values\[key\];') 'Adopt must fill missing values with defaults'

# Module: replicated attributes, mirrored, read and saved like the existing settings.
Assert ($moduleText -match '\[Attribute\("0", UIWidgets\.Slider, "Commander: grenade suicide instead of surrender \(%\)", "0 100 5", category: "EXPBG AI Surrender"\), RplProp\(\)\]\s*protected int m_iGrenade;') 'module grenade attribute must be a replicated 0-100 step 5 slider defaulting to 0'
Assert ($moduleText -match '\[Attribute\("1", UIWidgets\.CheckBox, "Commander must carry a grenade", category: "EXPBG AI Surrender"\), RplProp\(\)\]\s*protected bool m_bGrenadeCarry;') 'module must-carry attribute must be a replicated checkbox defaulting to ON'
$getSetting = Get-Body $moduleText 'int\s+GetSetting\s*\(\s*int\s+key\s*\)'
Assert ($getSetting -match 'ESR_Settings\.GRENADE\) return m_iGrenade;' -and $getSetting -match 'ESR_Settings\.GRENADE_CARRY\) return ESR_Settings\.FromBool\(m_bGrenadeCarry\);') 'GetSetting must read both commander settings'
$mirror = Get-Body $moduleText 'protected\s+void\s+Mirror\s*\(\s*\)'
Assert ($mirror -match 'm_iGrenade == grenade' -and $mirror -match 'm_bGrenadeCarry == grenadeCarry' -and $mirror -match 'm_iGrenade = grenade; m_bGrenadeCarry = grenadeCarry;' -and $mirror -match 'Replication\.BumpMe\(\);') 'Mirror must replicate both commander settings'
Assert ((Get-Body $moduleText 'bool\s+RestoreSettings\s*\(') -match 'values\.Count\(\) < ESR_Settings\.COUNT_V1 \|\| values\.Count\(\) > ESR_Settings\.COUNT') 'RestoreSettings must accept ten- and twelve-value sets'
Assert ($moduleText -match 'context\.WriteValue\("esrVersion", 3\)') 'native saves must write esrVersion 3 (all thirteen settings)'
$deserialize = Get-Body $moduleText 'override\s+protected\s+bool\s+Deserialize\s*\('
Assert ($deserialize -match 'version == 1 && settings\.Count\(\) == ESR_Settings\.COUNT_V1;' -and $deserialize -match 'version == 2 && settings\.Count\(\) == ESR_Settings\.COUNT_V2;') 'native loads must accept esrVersion 1 and 2'

# GM attribute list: interrogation settings together (the intel chance after identity);
# the commander two before Prisoners; the squad and soldier overrides (Test-SquadOverrides.ps1) after it.
Assert ($attributes -match 'class ESR_CommanderGrenadeAttribute : ESR_Attribute \{\}' -and $attributes -match 'class ESR_GrenadeCarryAttribute : ESR_Attribute \{\}') 'attribute classes must exist'
$order = [regex]::Matches($list, '(?m)^  (ESR_\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value }
$expected = 'ESR_EnabledAttribute','ESR_ChanceAttribute','ESR_ThresholdAttribute','ESR_RandomAttribute','ESR_RevealChanceAttribute','ESR_IdentityChanceAttribute','ESR_IntelChanceAttribute','ESR_RevealRadiusAttribute','ESR_AttemptsAttribute','ESR_MarkerLifetimeAttribute','ESR_DiagnosticsAttribute','ESR_CommanderGrenadeAttribute','ESR_GrenadeCarryAttribute','ESR_PrisonersAttribute','ESR_GroupSurrenderAttribute','ESR_GroupRevealAttribute','ESR_GroupIdentityAttribute','ESR_GroupIntelAttribute','ESR_UnitSurrenderAttribute','ESR_UnitRevealAttribute','ESR_UnitIdentityAttribute','ESR_UnitIntelAttribute'
Assert (($order -join ',') -eq ($expected -join ',')) "attribute order changed: $($order -join ',')"
$grenadeEntry = [regex]::Match($list, '(?s)ESR_CommanderGrenadeAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($grenadeEntry -match 'm_Key 10' -and $grenadeEntry -match 'Name "Commander: grenade suicide instead of surrender \(%\)"' -and $grenadeEntry -match 'AttributePrefab_Slider\.layout' -and $grenadeEntry -match 'm_fMin 0 m_fMax 100 m_fStep 5 m_iDecimals 0') 'grenade attribute entry must be key 10, slider 0-100 step 5'
$carryEntry = [regex]::Match($list, '(?s)ESR_GrenadeCarryAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($carryEntry -match 'm_Key 11' -and $carryEntry -match 'Name "Commander must carry a grenade"' -and $carryEntry -match 'AttributePrefab_Checkbox\.layout') 'must-carry attribute entry must be key 11, checkbox'

# Manager: rolled once for the leader, only when he would surrender; bounded; vanilla arming.
$evaluate = Get-Body $manager 'protected\s+static\s+void\s+Evaluate\s*\('
Assert ($evaluate -match 'SCR_ChimeraCharacter commander = SquadCommander\(group, candidates\);') 'Evaluate must choose the leader before anyone leaves the squad'
Assert ($evaluate -match '(?s)if \(rolled < 100 && Math\.RandomFloat\(0, 100\) >= rolled\) continue;\s*// [^\n]*\n\s*if \(candidate == commander && ChoosesGrenade\(candidate, group, record\)\)') 'the grenade roll must follow a successful surrender roll for the leader only'
Assert ($evaluate -match '!FindPrisoner\(member\) && !FindCommander\(member\)') 'a leader with a grenade must never be a surrender candidate'
$chooses = Get-Body $manager 'protected\s+static\s+bool\s+ChoosesGrenade\s*\('
Assert ($chooses -match 'chance <= 0' -and $chooses -match 'record\.RolledCommander == commander' -and $chooses -match 'record\.RolledCommander = commander;') 'the grenade chance must be skipped at 0% and rolled once per leader'
$commander = Get-Body $manager 'static\s+SCR_ChimeraCharacter\s+SquadCommander\s*\('
Assert ($commander -match 'group\.GetLeaderEntity\(\)' -and $commander -match 'SCR_CharacterRankComponent\.GetCharacterRank\(candidate\)') 'leader: the group leader, else the highest-ranking candidate'
$start = Get-Body $manager 'static\s+bool\s+StartGrenade\s*\('
Assert ($start -match 'FindPrisoner\(character\)' -and $start -match 'MAX_COMMANDERS' -and $start -match 'CommanderBlocked\(character\)' -and $start -match 'if \(mustCarry\)' -and $start -match 'control\.DeactivateAI\(\);' -and $start -match 'STANCECHANGE_TOCROUCH') 'StartGrenade must refuse prisoners, respect the cap and must-carry, and stop and crouch him'
Assert ($start -match 'Replication\.IsServer\(\)') 'StartGrenade must be server-only'
$blocked = Get-Body $manager 'protected\s+static\s+string\s+CommanderBlocked\s*\('
Assert ($blocked -match 'IsPlayerCharacter\(character\)' -and $blocked -match 'FindPrisoner\(character\)' -and $blocked -match 'IsInVehicle\(\)' -and $blocked -match 'GetPermanentLOD\(\) >= 0') 'player-controlled, prisoners, vehicle crew and cached soldiers never take a grenade'
$place = Get-Body $manager 'protected\s+static\s+void\s+PlaceGrenade\s*\('
Assert ($place -match 'inventory\.TryDeleteItem\(carried\)' -and $place -match 'CallLater\(ESR_SurrenderManager\.LiveGrenade, GRENADE_ARM_MS') 'PlaceGrenade must take his own grenade and arm one second later'
$spawn = Get-Body $manager 'protected\s+static\s+IEntity\s+SpawnGrenade\s*\('
Assert ($spawn -match 'Resource resource = Resource\.Load\(prefab\);' -and $spawn -match 'SpawnEntityPrefab\(resource,' -and $spawn -match 'trigger\.SetInstigator\(Instigator\.CreateInstigator\(character\)\);') 'SpawnGrenade must keep Resource.Load in a local and credit the leader'
Assert (!$manager.Contains('EnableSimulation')) 'the placed grenade is never launched as a projectile'
$spot = Get-Body $manager 'protected\s+static\s+vector\s+GrenadeSpot\s*\('
Assert ($spawn -match 'spawn\.Transform\[3\] = GrenadeSpot\(character\);' -and $spot -match 'SCR_TerrainHelper\.SnapToGeometry\(snapped, spot \+ vector\.Up \* GRENADE_SNAP_HEIGHT, exclude,' -and $spot -match 'Math\.AbsFloat\(snapped\[1\] - feet\[1\]\) <= GRENADE_SNAP_HEIGHT' -and $spot -match 'spot = feet;') 'the physics-less grenade must sit on the traced floor or ground in front of him, else at his feet'
$live = Get-Body $manager 'protected\s+static\s+void\s+LiveGrenade\s*\('
Assert ($live -match 'trigger\.SetLive\(\);' -and $live -match 'TimerTriggerComponent\.Cast\(trigger\)' -and $live -match 'CallLater\(ESR_SurrenderManager\.AfterBlast,') 'LiveGrenade must arm through the vanilla trigger and check after the fuse'
$armAt = $live.IndexOf('trigger.SetLive();')
Assert ($live.IndexOf('item.GetParentSlot()') -ge 0 -and $live.IndexOf('item.GetParentSlot()') -lt $armAt -and $live.IndexOf('IsPlayerCharacter(commander.Character)') -ge 0 -and $live.IndexOf('IsPlayerCharacter(commander.Character)') -lt $armAt) 'a grenade picked up, or under a possessed leader, must never be set live'
Assert ($manager -match 'static const ResourceName GRENADE_RGD5 = "\{645C73791ECA1698\}Prefabs/Weapons/Grenades/Grenade_RGD5\.et";' -and $manager -match 'static const ResourceName GRENADE_M67 = "\{E8F00BF730225B00\}Prefabs/Weapons/Grenades/Grenade_M67\.et";') 'vanilla RGD-5 and M67 prefabs'
Assert ((Get-Body $manager 'static\s+bool\s+Surrender\s*\(') -match 'FindCommander\(character\)') 'Surrender must refuse a leader with a grenade'

# Enforce gotchas in the touched sources and the fixture; new files are ASCII with LF.
foreach ($path in @($settingsPath, $modulePath, $attributesPath, $managerPath, $fixturePath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
 Assert (!$text.Contains("`r")) "CR line ending in $path"
}
Assert ($manager -match 'Math\.RandomFloat\(GRENADE_PREP_MIN, GRENADE_PREP_MAX\)' -and $manager -match 'static const float GRENADE_PREP_MIN = 1\.0;' -and $manager -match 'static const float GRENADE_PREP_MAX = 2\.5;') 'the preparation delay range must never be empty'

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'grenade fixture must keep the runner driver class names'
$regex = '\[ESR GRENADE RESULT\] checks=[1-9]\d* failures=0 own=1 spawned=1 mustCarry=1 control=1 reason=complete'
Assert ($fixture.Contains("-FixturePath tests/ESR_CommanderGrenadeGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'grenade fixture header must carry its runner command'
$format = [regex]::Match($fixture, 'PrintFormat\("(\[ESR GRENADE RESULT\][^"]*)"').Groups[1].Value
Assert ($format -eq '[ESR GRENADE RESULT] checks=%1 failures=%2 own=%3 spawned=%4 mustCarry=%5 control=%6 reason=%7') "grenade fixture RESULT format changed: $format"
$sample = $format.Replace('%1', '31').Replace('%2', '0').Replace('%3', '1').Replace('%4', '1').Replace('%5', '1').Replace('%6', '1').Replace('%7', 'complete')
Assert ($sample -match $regex) 'grenade fixture RESULT line must match its -ExpectResult regex'
Assert (!($format.Replace('%1', '31').Replace('%2', '1').Replace('%3', '0').Replace('%4', '1').Replace('%5', '1').Replace('%6', '1').Replace('%7', 'own') -match $regex)) 'a failing RESULT line must not match the regex'
Assert ($fixture -match 'Resource modulePrefab = Resource\.Load\(MODULE\);' -and $fixture -match 'Resource squad = Resource\.Load\(SQUAD\);') 'grenade fixture must keep Resource.Load results in locals'
Assert ($fixture -match 'Configure\(100, true\);' -and $fixture -match 'Configure\(100, false\);' -and $fixture -match 'Configure\(0, true\);') 'grenade fixture must cover own, spawned, must-carry and control settings'
'PASS: AI Surrender commander grenade settings (keys 10/11, defaults 0% and ON) replicated and saved with esrVersion 1 compatibility, leader-only roll after a successful surrender roll, vanilla SetLive arming; grenade fixture wired.'
