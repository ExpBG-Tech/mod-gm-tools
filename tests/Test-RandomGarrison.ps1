#requires -Version 7.0
# Portable guard for the Random Garrison module (addon/random-garrison, Unreleased): a GM
# Systems zone that garrisons random buildings in a radius with random squads. Every
# squad is an ordinary garrison made by the shared spawner (EXPG_GarrisonSpawn.c, also
# used by EXPBG Add Garrison) and carries the zone's token (GeneratedBy) in its record
# and in the garrison ledger. Checks the registration (last module), the shared config
# identities, the attribute list against the module's keys (class names are permanent
# save keys), the module's use of the garrison API (no direct spawn, no plan stepping,
# background analysis), the ledger's backward-compatible GeneratedBy, the native save,
# the rules (with a PowerShell model of the building target), Enforce gotchas, the
# contract and fixture wiring and the docs. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Join-Path $repo 'addon/random-garrison'
$scripts = Join-Path $module 'Scripts/Game/EXPGR'
$garrison = Join-Path $repo 'addon/garrison/Scripts/Game/EXPG'
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

# Registration: the last module of the pack, with its own copies of the merged configs.
$pack = Get-Content -LiteralPath (Join-Path $repo 'tools/pack.json') -Raw | ConvertFrom-Json
$names = @($pack.modules | ForEach-Object name)
Assert ($names[-1] -ceq 'random-garrison' -and @($names | Where-Object { $_ -ceq 'random-garrison' }).Count -eq 1) 'tools/pack.json must register random-garrison once, as the last module'
Assert ((@($pack.modules)[-1]).title -ceq 'Random Garrison') 'the module title is "Random Garrison"'
$shared = @{
 'Configs/Editor/AttributeLists/Edit.conf' = 'F3D6C6D25642352C'
 'Configs/Editor/PlaceableEntities/Systems/Systems.conf' = '3A9124B8692C3F39'
 'Configs/Systems/Persistence/GameMode/GameMaster.conf' = 'B76E7F1AF7A5D00C'
}
foreach ($path in $shared.Keys) {
 Assert ($pack.merge -contains $path) "shared config $path must be merged"
 $meta = Get-MetaGuid (Join-Path $module "$path.meta")
 Assert ($meta.Groups[1].Value -ceq $shared[$path] -and $meta.Groups[2].Value -ceq $path) "$path.meta must keep the vanilla identity {$($shared[$path])}"
}
$expected = @(
 'Scripts/Game/EXPGR/EXPG_RandomGarrisonModule.c', 'Scripts/Game/EXPGR/EXPG_RandomGarrisonDirector.c', 'Scripts/Game/EXPGR/EXPG_RandomGarrisonCensus.c',
 'Scripts/Game/EXPGR/EXPG_SquadPool.c', 'Scripts/Game/EXPGR/EXPG_RandomGarrisonAttributes.c', 'Scripts/Game/EXPGR/EXPG_RandomGarrisonArea.c',
 'Scripts/Game/EXPGR/EXPG_RandomGarrisonPersistence.c', 'Scripts/Game/EXPGR/EXPG_RandomGarrisonRules.c',
 'PrefabsEditable/EXPGR/EXPG_RandomGarrison.et', 'Configs/Editor/AttributeCategories/EXPGR_RandomGarrison.conf',
 'Configs/Systems/Persistence/Configuration/EXPGR/RandomGarrison.conf')
foreach ($relative in $expected) { Assert (Test-Path -LiteralPath (Join-Path $module $relative) -PathType Leaf) "missing $relative" }
$spawnPath = Join-Path $garrison 'EXPG_GarrisonSpawn.c'
Assert (Test-Path -LiteralPath $spawnPath -PathType Leaf) 'the shared spawn API lives in addon/garrison/Scripts/Game/EXPG/EXPG_GarrisonSpawn.c'

# Prefab, browser name (no EXPBG prefix), Systems list and native persistence.
$prefab = Read-Text (Join-Path $module 'PrefabsEditable/EXPGR/EXPG_RandomGarrison.et')
Assert ($prefab -match '(?m)^EXPG_RandomGarrisonModule \{' -and $prefab -match 'm_EntityType SYSTEM' -and $prefab -match 'Name "#EXPBG-RandomGarrison_Name"' -and $prefab -match 'Description "#EXPBG-RandomGarrison_Description"') 'prefab: EXPG_RandomGarrisonModule, SYSTEM, string-table name and description'
Assert ($prefab -match 'SpatialRelevancy 0' -and $prefab -match 'Streamable Disabled' -and $prefab -match 'ENTITYTYPE_SYSTEM 157026' -and $prefab -match 'EXPBG_Badge_UI\.edds' -and $prefab.Contains('{5ACF724CA7564555}UI/Textures/EXPBG_Garrison/EXPG_Card.edds')) 'prefab: always relevant, EXPBG label and badge, garrison card'
Assert ($prefab -match 'EXPG_RandomGarrisonAreaComponent "\{[0-9A-F]{16}\}" \{' -and $prefab -match 'm_bFollowTerrain 1' -and $prefab -match 'EXPG_RandomGarrisonVisibilityComponent "\{[0-9A-F]{16}\}" \{\s*m_State RENDERED') 'prefab: terrain-following radius mesh shown only while the editor renders the zone'
$language = Join-Path $repo 'addon/unit-caching/Language'
$source = Read-Text (Join-Path $language 'EXPBG_Localization.st')
$runtime = Read-Text (Join-Path $language 'EXPBG_Localization.en_us.conf')
Assert ($source -match 'Id "EXPBG-RandomGarrison_Name"\s*\n\s*Target_en_us "Random Garrison"' -and $runtime.Contains('"EXPBG-RandomGarrison_Name"') -and $runtime.Contains('"Random Garrison"')) 'the entity is named "Random Garrison" (never EXPBG ...)'
Assert ($source.Contains('Id "EXPBG-RandomGarrison_Description"') -and $runtime.Contains('"EXPBG-RandomGarrison_Description"')) 'description key registered in both tables'
$prefabMeta = Get-MetaGuid (Join-Path $module 'PrefabsEditable/EXPGR/EXPG_RandomGarrison.et.meta')
$prefabRef = '"{' + $prefabMeta.Groups[1].Value + '}' + $prefabMeta.Groups[2].Value + '"'
Assert ((Read-Text (Join-Path $module 'Configs/Editor/PlaceableEntities/Systems/Systems.conf')).Contains($prefabRef)) 'Systems.conf must list the prefab with its GUID'
$gameMaster = Read-Text (Join-Path $module 'Configs/Systems/Persistence/GameMode/GameMaster.conf')
$configMeta = Get-MetaGuid (Join-Path $module 'Configs/Systems/Persistence/Configuration/EXPGR/RandomGarrison.conf.meta')
Assert ($gameMaster.Contains('"{' + $configMeta.Groups[1].Value + '}' + $configMeta.Groups[2].Value + '"') -and $gameMaster -match 'Collection "\{6624C4351F719B74\}"' -and $gameMaster -match 'PersistenceConfigGroup EXPGR') 'GameMaster.conf registers the zone config in the editable-entity collection'
$config = Read-Text (Join-Path $module 'Configs/Systems/Persistence/Configuration/EXPGR/RandomGarrison.conf')
Assert ($config -match 'EntityClass "EXPG_RandomGarrisonModule"' -and $config -match 'EntitySerializer EXPG_RandomGarrisonSerializer ' -and $config -match 'Priority 20001' -and $config -match 'ParentHandling "Ignore always"') 'the zone is saved by EXPG_RandomGarrisonSerializer'
$category = Read-Text (Join-Path $module 'Configs/Editor/AttributeCategories/EXPGR_RandomGarrison.conf')
Assert ($category -match 'Name "EXPBG Random Garrison"') 'attribute tab "EXPBG Random Garrison"'

# Attributes: dialog order, keys equal to the module's KEY_* constants, durable values.
$moduleText = Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonModule.c')
$keys = @{}
foreach ($m in [regex]::Matches($moduleText, 'static const int (KEY_\w+) = (\d+);')) { $keys[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
Assert ($keys.Count -eq 16 -and $moduleText -match 'static const int INT_SETTINGS = 14;') 'sixteen setting keys, fourteen of them saved as integers'
$list = Read-Text (Join-Path $module 'Configs/Editor/AttributeLists/Edit.conf')
$order = @([regex]::Matches($list, '(?m)^  (EXPG_\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value })
$listed = [ordered]@{
 'EXPG_RGStatusAttribute' = $null; 'EXPG_RGRunAttribute' = $null; 'EXPG_RGRadiusAttribute' = 'KEY_RADIUS'; 'EXPG_RGFactionAttribute' = 'KEY_FACTION'
 'EXPG_RGSecondFactionAttribute' = 'KEY_SECOND_FACTION'; 'EXPG_RGSizesAttribute' = 'KEY_SIZES'; 'EXPG_RGExcludeSupportAttribute' = 'KEY_EXCLUDE_SUPPORT'
 'EXPG_RGBuildingsAttribute' = 'KEY_BUILDINGS'; 'EXPG_RGShareAttribute' = 'KEY_SHARE'; 'EXPG_RGSquadsMinAttribute' = 'KEY_SQUADS_MIN'; 'EXPG_RGSquadsMaxAttribute' = 'KEY_SQUADS_MAX'
 'EXPG_RGPlayerDistanceAttribute' = 'KEY_PLAYER_DISTANCE'; 'EXPG_RGAllowGarrisonedAttribute' = 'KEY_ALLOW_GARRISONED'; 'EXPG_RGSeedAttribute' = 'KEY_SEED'
 'EXPG_RGCacheModeAttribute' = 'KEY_CACHE_MODE'; 'EXPG_RGWakeAttribute' = 'KEY_WAKE'; 'EXPG_RGSleepAttribute' = 'KEY_SLEEP'; 'EXPG_RGOnDeleteAttribute' = 'KEY_ON_DELETE'
 'EXPG_RGSavedStateAttribute' = $null
}
Assert (($order -join ',') -ceq (@($listed.Keys) -join ',')) "attribute list order changed: $($order -join ',')"
$attributes = Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonAttributes.c')
foreach ($class in $listed.Keys) {
 Assert ($attributes -match "\[BaseContainerProps\(\), SCR_BaseEditorAttributeCustomTitle\(\)\]\s*class\s+$class\s*:\s*(EXPG_RandomGarrisonAttribute|EXPG_RGFactionAttribute)\b") "$class must be an EXPG_RandomGarrisonAttribute (the dialog filter keeps only those)"
 $block = [regex]::Match($list, "(?s)  $class \{(.*?)\n  \}").Groups[1].Value
 Assert ($block -match 'm_CategoryConfig "\{FEF745598F49F211\}Configs/Editor/AttributeCategories/EXPGR_RandomGarrison\.conf"') "$class must use the EXPBG Random Garrison tab"
 if ($listed[$class]) {
  $key = [regex]::Match($block, 'm_Key (\d+)')
  Assert ($key.Success -and [int]$key.Groups[1].Value -eq $keys[$listed[$class]]) "$class m_Key must equal $($listed[$class]) ($($keys[$listed[$class]]))"
 }
}
foreach ($other in Get-ChildItem -LiteralPath (Join-Path $repo 'addon') -Recurse -Filter 'Edit.conf' -File | Where-Object { $_.FullName -notlike "$module*" }) {
 Assert (!(Read-Text $other.FullName).Contains('EXPG_RG')) "Random Garrison attributes belong to its own Edit.conf, not $($other.FullName)"
}
$values = { param($class) @([regex]::Matches([regex]::Match($list, "(?s)  $class \{(.*?)\n  \}").Groups[1].Value, 'm_fEntryFloatValue (\d+)') | ForEach-Object { [int]$_.Groups[1].Value }) -join ',' }
Assert ((& $values 'EXPG_RGSizesAttribute') -eq '2,6,4,12,3,15') 'squad size presets: fire teams 2, fire teams and squads 6, squads 4, squads and large 12, small teams and fire teams 3, any 15'
Assert ((& $values 'EXPG_RGRunAttribute') -eq '0,1,2,3,4' -and (& $values 'EXPG_RGCacheModeAttribute') -eq '0,1,2' -and (& $values 'EXPG_RGOnDeleteAttribute') -eq '0,1') 'action, cache mode and delete rows'
Assert ([regex]::Match($list, '(?s)  EXPG_RGStatusAttribute \{(.*?)\n  \}').Groups[1].Value -match 'm_bIsServer 0') 'the status row is local'
foreach ($slider in @(@('EXPG_RGRadiusAttribute', 'm_fMin 25\s+m_fMax 1000\s+m_fStep 25'), @('EXPG_RGBuildingsAttribute', 'm_fMin 1\s+m_fMax 32'), @('EXPG_RGShareAttribute', 'm_fMin 1\s+m_fMax 100'), @('EXPG_RGSquadsMinAttribute', 'm_fMin 1\s+m_fMax 4'), @('EXPG_RGPlayerDistanceAttribute', 'm_fMin 0\s+m_fMax 1000\s+m_fStep 25'), @('EXPG_RGSeedAttribute', 'm_fMin 0\s+m_fMax 9999'))) {
 Assert ([regex]::Match($list, "(?s)  $($slider[0]) \{(.*?)\n  \}").Groups[1].Value -match $slider[1]) "$($slider[0]) range"
}
foreach ($class in 'EXPG_RGStatusAttribute', 'EXPG_RGRunAttribute') {
 Assert ((Get-Body $attributes "class\s+$class\s*:") -match 'override bool IsSerializable\(\)\s*\{\s*return false;') "$class is never saved"
}
Assert ((Get-Body $attributes 'class\s+EXPG_RGSavedStateAttribute\s*:') -match 'if \(manager\)\s*\{\s*return null;') 'the saved state is never shown in the dialog'
Assert ((Get-Body $attributes 'class\s+EXPG_RGFactionAttribute\s*:') -match 'CreateVector\(EXPG_RGRules\.PackKey\(' -and (Get-Body $attributes 'class\s+EXPG_RGFactionAttribute\s*:') -match 'EXPG_RGRules\.SameKey\(packed') 'factions are saved as key hashes, never rows'
$allowed = Get-Body $attributes 'static\s+bool\s+AllowedWrite\s*\('
Assert ($allowed -match 'return playerID == -1;' -and $allowed -match 'editor\.IsLimited\(\)' -and $allowed -match 'EEditorMode\.EDIT' -and $allowed -match 'core\.GetEditorManager\(playerID\) == editor') 'writes: this player''s registered unlimited editor in Edit mode, or a restore (no manager, player -1)'
Assert ($attributes -match 'modded class SCR_AttributesManagerEditorComponent' -and (Get-Body $attributes 'override\s+protected\s+int\s+GetVariables\s*\(') -match 'EXPG_RandomGarrisonAttribute\.Cast\(data\.GetAttribute') 'dialog filter for zone-only selections'

# The module uses the garrison API: no direct spawn, no plan stepping, background analysis.
$moduleSources = @(Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File | ForEach-Object FullName)
foreach ($path in $moduleSources) {
 $text = Read-Text $path
 Assert (!$text.Contains('SpawnEntityPrefab')) "the zone never spawns directly (EXPG_GarrisonSpawner.Spawn only): $path"
 Assert (![regex]::IsMatch($text, '\.Step\(\d') -and !$text.Contains('Plan.Step(') -and !$text.Contains('plan.Step(')) "the zone never steps a building plan: $path"
 Assert (!$text.Contains('OnCreatedServer')) "the zone never reserves a Game Master's budget: $path"
}
Assert ($moduleText -match 'EXPG_GarrisonSpawner\.Spawn\(request, reason\)' -and $moduleText -match 'request\.GeneratedBy = m_sToken;' -and $moduleText -match 'request\.DeleteOnRefusal = true;' -and $moduleText -match 'request\.CheckAILimit = true;') 'squads are made by the shared spawner with the zone token'
Assert ((Get-Body $moduleText 'class\s+EXPG_RGAnalysis\s*:\s*EXPG_PlanWaiter') -match 'Background = true;') 'zone analyses are background requests'
Assert ($moduleText -match 'static const int PLAN_HEADROOM = 56;' -and $moduleText -match 'static const int MAX_ANALYSES_PER_ZONE = 2;' -and $moduleText -match 'static const float PENDING_TIMEOUT = 60;' -and $moduleText -match 'static const float PLAYER_DEFER_LIMIT = 120;' -and $moduleText -match 'static const float AI_LIMIT_WAIT = 60;') 'limits: 56 plans, 2 analyses per zone, 60 s pending, 120 s player wait, 60 s AI limit'
$director = Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonDirector.c')
Assert ($director -match 'TICK_MS = 100;' -and $director -match 'MAX_ZONES = 64;' -and $director -match 'SPAWN_INTERVAL = 1\.5;' -and $director -match 'MAX_SPAWNING = 2;' -and $director -match 'MAX_ANALYSES = 3;' -and $director -match 'DELETES_PER_TICK = 8;') 'director: 100 ms, 64 zones, one spawn per 1.5 s, two spawning, three analyses, 8 deletions per tick'
Assert ($director -match 'GetOnBeforeSave\(\)\.Insert\(OnBeforeSave\)' -and (Get-Body $director 'protected\s+static\s+void\s+OnBeforeSave\s*\(') -match 'KeepPendingOutOfSaves\(\)') 'squads that have not taken their posts stay out of native saves'
Assert ((Get-Body $director 'override\s+static\s+void\s+BeginPreparation\s*\(') -match 'EXPG_GarrisonPersistence\.Legacy\(\)\)\s*\{\s*EXPG_RandomGarrisonDirector\.AbortForSave\(\);\s*\}\s*super\.BeginPreparation\(\);') 'legacy CDF Prepare for Save stops the generation first'
$teardown = Get-Body $director 'static\s+bool\s+Teardown\s*\('
Assert ($teardown -match 'IsPortableWorldReady\(\)' -and $teardown -match 'EBG_CacheSnapshot\.Loading' -and $teardown -match 'SaveInProgress\(\)') 'a zone deleted during unload or a load replacing the scene does nothing'
$census = Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonCensus.c')
Assert ($census -match 'CELL_SIZE = 64;' -and $census -match 'MAX_CALLBACKS = 512;' -and $census -match 'MAX_HITS = 4096;' -and $census -match 'MAX_ELIGIBLE = 2048;' -and $census -match 'CELLS_PER_TICK = 6;' -and $census -match 'CHECKS_PER_TICK = 32;' -and $census -match 'MAX_COLUMNS = 6144;') 'census: 64 m cells, 512 callbacks with quadrant split, 4096 hits, 2048 eligible, 6 cells and 32 checks per tick'
$structural = Get-Body $census 'static\s+bool\s+Structural\s*\('
Assert ($structural -match 'EAC_HomeIndex\.IsRuined\(entity\)' -and $structural -match 'EAD_World\.WallMayCollapse\(entity\)' -and $structural -match 'GetAncestor\(\)' -and $structural -match 'HasInterior\(structure\)') 'eligibility: intact, not collapsing, prefab and ancestors, doors or interior'
foreach ($part in '/dst/', 'ruin', 'destroyed', '/furniture/', '/buildingparts/', '/buildingaddons/', '/cemeteries/', '/walls/', '/piers/', 'hotbed', 'calvar', 'deerstand') { Assert ($census.Contains('"' + $part + '"')) "rejected prefab path part $part" }
$pool = Read-Text (Join-Path $scripts 'EXPG_SquadPool.c')
Assert ($pool -match 'GetFactionEntityCatalogOfType\(EEntityCatalogType\.GROUP, false\)' -and $pool -match 'IsValidInEditorMode\(EEditorMode\.EDIT\)' -and $pool -match 'EXPG_SquadPrefab\.Validate\(prefab, members, reason, FactionId\)' -and $pool -match 'm_Paths\.Sort\(\);') 'squad catalog: Edit-mode group entries, shared validator, sorted by path'
Assert ($pool -match 'TRAIT_MEDICAL' -and $pool -match 'TRAIT_LOGISTICS' -and $pool -match 'TRAIT_ESSENTIAL' -and $pool -match 'GROUPTYPE_ESSENTIAL' -and $pool -match 'MAX_FACTIONS = 24;') 'support labels and at most 24 factions'

# Garrison API: additive manager hooks, the shared spawner and the editor using it.
$manager = Read-Text (Join-Path $garrison 'EXPG_GarrisonManager.c')
Assert ((Get-Body $manager 'class\s+EXPG_PlanWaiter') -match 'bool Background;') 'EXPG_PlanWaiter.Background'
$service = Get-Body $manager 'protected\s+void\s+ServiceWaiter\s*\('
Assert ($service -match 'plan\.LastUsed = Now\(\);\s*if \(!waiter\.Background\) \{ plan\.WaitedAt = Now\(\); \}') 'a background request keeps its plan in use but never takes a Game Master''s analysis turn'
foreach ($api in 'int PlanCount()', 'int CollectGenerated(string generatedBy, notnull array<ref EXPG_GarrisonRecord> outRecords)', 'int AssignedSoldiers(IEntity building)', 'bool Discard(EXPG_GarrisonRecord record, string why)') { Assert ($manager.Contains($api)) "EXPG_GarrisonManager must expose $api" }
Assert ((Get-Body $manager 'int\s+CollectGenerated\s*\(') -match '!record\.Finished && record\.GeneratedBy == generatedBy') 'CollectGenerated returns unfinished records of the token'
$discard = Get-Body $manager 'bool\s+Discard\s*\(\s*EXPG_GarrisonRecord'
Assert ($discard -match 'record\.Full\.Abandon\(\);' -and $discard -match 'record\.FinishRelease\(\);' -and !$discard.Contains('Wake(')) 'Discard forgets one garrison without waking or respawning anyone'
$spawner = Read-Text $spawnPath
$spawn = Get-Body $spawner 'static\s+SCR_AIGroup\s+Spawn\s*\('
Assert ($spawn.IndexOf('manager.CanFit(') -lt $spawn.IndexOf('SpawnEntityPrefab(') -and $spawn.IndexOf('EXPG_BeginFreshRoster(') -lt $spawn.IndexOf('manager.AdoptFresh(') -and $spawn.IndexOf('manager.AdoptFresh(') -lt $spawn.IndexOf('record.GeneratedBy = request.GeneratedBy;')) 'spawn order: CanFit, spawn, fresh roster, AdoptFresh, then GeneratedBy'
Assert ($spawn -match 'Resource resource = Resource\.Load\(request\.Prefab\);' -and !$spawn.Contains('OnCreatedServer')) 'the spawner keeps Resource.Load in a local and never reserves a Game Master budget itself'
$validate = Get-Body $spawner 'static\s+bool\s+Validate\s*\('
Assert ($validate -match 'EEditableEntityType\.GROUP' -and $validate -match 'IsInherited\(SCR_AIGroup\)' -and $validate -match '"m_bSpawnImmediately"' -and $validate -match 'slots\.Count\(\) > MAX_MEMBERS' -and $validate -match 'EEditableEntityType\.CHARACTER' -and $validate -match '"m_faction"') 'validator: editable group, SCR_AIGroup class, spawns its members, 1-32 editable characters, faction'
$editor = Read-Text (Join-Path $garrison 'EXPG_Editor.c')
$select = Get-Body $editor 'protected\s+void\s+EXPG_SelectServer\s*\('
Assert ($select -match 'EXPG_SquadPrefab\.Validate\(prefab, memberCount, invalid\)' -and $select -match 'EXPG_GarrisonSpawner\.Spawn\(request, spawnFailure\)' -and !$select.Contains('SpawnEntityPrefab') -and !$editor.Contains('EXPG_ReinforcementSpawn')) 'EXPBG Add Garrison uses the shared validator and spawner'
Assert ($select.IndexOf('CanPlaceEntityServer(') -lt $select.IndexOf('m_EXPG_Ticket.Consume(') -and $select.IndexOf('m_EXPG_Ticket.Consume(') -lt $select.IndexOf('EXPG_GarrisonSpawner.Spawn(') -and $select -match 'GetOnPlaceEntityServer\(\)\.Invoke\(prefabID, callbacks\.Editable, playerId\)') 'Add Garrison keeps its budget checks, ticket and placement event'
$after = Get-Body $editor 'void\s+EXPG_AfterSpawnServer\s*\('
Assert ($after.IndexOf('EOnEditorPlace(') -lt $after.IndexOf('SetAuthor(') -and $after.IndexOf('SetAuthor(') -lt $after.IndexOf('OnCreatedServer(this)') -and $after.IndexOf('OnCreatedServer(this)') -lt $after.IndexOf('OnEntityCreatedServer(created)')) 'the editor callbacks keep the vanilla placement order'

# The ledger carries GeneratedBy; ledgers written without it still load.
$snapshot = Read-Text (Join-Path $garrison 'EXPG_Snapshot.c')
$write = Get-Body $snapshot 'bool\s+Write\s*\(\s*SaveContext\s+context\s*\)\s*\{\s*if\s*\(!Squad'
$read = Get-Body $snapshot 'bool\s+Read\s*\(\s*LoadContext\s+context,\s*out\s+string\s+reason\s*\)'
Assert ($write.Contains('context.WriteValue("generatedBy", GeneratedBy)') -and $read.Contains('context.ReadValueDefault("generatedBy", GeneratedBy, string.Empty)') -and !$read.Contains('ReadValue("generatedBy"')) 'GeneratedBy is always written and read with a default (older ledgers load)'
Assert ($snapshot -match 'static\s+const\s+int\s+VERSION\s*=\s*1;') 'the ledger schema stays 1 (0.1.11 still reads new saves)'
Assert ((Get-Body $manager 'protected\s+EXPG_GarrisonSnapshot\s+ExportRecord\s*\(').Contains('saved.GeneratedBy = record.GeneratedBy;') -and (Get-Body $manager 'protected\s+EXPG_GarrisonRecord\s+ImportRecord\s*\(').Contains('record.GeneratedBy = saved.GeneratedBy;')) 'export and import keep GeneratedBy'

# Native zone save.
$persistence = Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonPersistence.c')
Assert ($persistence -match 'class EXPG_RandomGarrisonSerializer : GenericEntitySerializer' -and $persistence -match 'EEntityDeserializeEvent\.AFTER_FINALIZE' -and $persistence -match 'EDeserializeFailHandling\.IGNORE' -and $persistence -match 'static const int VERSION = 1;') 'zone serializer: after finalize, never fails the load'
foreach ($field in '"rgVersion"', '"settings"', '"factions"', '"token"', '"generated"', '"lastSeed"', '"outcomes"') { Assert ($persistence.Contains("context.WriteValue($field") -and $persistence.Contains("context.ReadValue($field")) "the zone save keeps $field" }
Assert ((Get-Body $moduleText 'bool\s+RestoreSettings\s*\(') -match 'ValidSetting\(key, values\[key\]\)' -and (Get-Body $moduleText 'bool\s+RestoreSaved\s*\(') -match 'm_iState = STATE_STOPPED;') 'settings restore all or nothing; a loaded zone never resumes by itself'
# A save made during a run: the ledger saves every Ready garrison of the zone, so the zone says generated.
$isGenerated = Get-Body $moduleText 'bool\s+IsGenerated\s*\(\s*\)'
Assert ($isGenerated -match 'manager\.CollectGenerated\(m_sToken, records\);' -and $isGenerated -match 'record\.Ready') 'IsGenerated is true once a squad of the zone has taken its posts, also while it runs'
Assert ((Get-Body $moduleText 'vector\s+SavedState\s*\(') -match 'if \(IsGenerated\(\)\)' -and $persistence -match 'bool generated = zone\.IsGenerated\(\);') 'both zone saves (native serializer, CDF saved-state attribute) use IsGenerated'

# Entities waiting to be deleted (Clear, Stop, orphans, deleted zones) stay out of every save until deleted.
$retireList = Get-Body $spawner 'class\s+EXPG_RetireList\s*\{'
Assert ($retireList -match 'SetEntityFlag\(EEditableEntityFlag\.NON_SERIALIZABLE, true\)' -and $retireList -match 'persistence\.StopTracking\(entity\);' -and $retireList -match 'void\s+KeepOut\s*\(bool force\)' -and $retireList -match 'if \(EXPG_GarrisonSpawner\.DeleteOwned\(entity\)\)') 'EXPG_RetireList keeps every queued entity out of saves and hands back only what DeleteOwned refuses'
$dismiss = Get-Body $spawner 'static\s+int\s+Dismiss\s*\('
Assert ($spawner.Contains('static int Dismiss(EXPG_GarrisonRecord record, notnull EXPG_RetireList leftovers, string why)') -and $dismiss.IndexOf('manager.Discard(') -ge 0 -and $dismiss.IndexOf('manager.Discard(') -lt $dismiss.IndexOf('Retire(leftovers, actor);')) 'Dismiss retires the discarded garrison''s squad and soldiers through a retire list'
Assert ((Get-Body $spawner 'protected\s+static\s+void\s+Retire\s*\(') -match 'EXPG_SaveExclusion\.HandOver\(entity, flagged, untracked\);\s*leftovers\.Add\(entity, flagged, untracked\);') 'the save exclusion hands a dismissed entity over in the same call (no later sync hands it back)'
$handOver = Get-Body (Read-Text (Join-Path $garrison 'EXPG_SaveExclusion.c')) 'static\s+bool\s+HandOver\s*\('
Assert ($handOver -match 's_Entities\.Remove\(index\);' -and !$handOver.Contains('StartTracking') -and !$handOver.Contains('SetEntityFlag')) 'EXPG_SaveExclusion.HandOver leaves the entity excluded'
Assert ($moduleText.Contains('protected ref EXPG_RetireList m_Retire = new EXPG_RetireList();') -and !$moduleText.Contains('m_aRetire') -and !$moduleText.Contains('CollectSquad(pending.Squad, m_')) 'the zone deletes only through its retire list'
$stopWork = Get-Body $moduleText 'protected\s+void\s+StopWork\s*\('
Assert ($stopWork -match 'if \(PendingReady\(pending\)\)\s*\{\s*ReturnFlagged\(pending\);' -and $stopWork -match 'RetirePending\(pending, m_Retire\);') 'Stop hands back only deployed squads; the others stay out of saves until deleted'
Assert ((Get-Body $moduleText 'protected\s+void\s+ServicePending\s*\(') -match 'RetirePending\(pending, m_Retire\);' -and (Get-Body $moduleText 'protected\s+void\s+RetirePending\s*\(') -match 'retire\.Add\(entity, pending\.FlagSet\[index\], pending\.WasTracked\[index\]\);') 'orphans move to the retire list with what the zone changed'
Assert ((Get-Body $moduleText 'void\s+KeepPendingOutOfSaves\s*\(') -match 'm_Retire\.KeepOut\(true\);' -and (Get-Body $director 'protected\s+static\s+void\s+OnBeforeSave\s*\(') -match 's_Janitor\.KeepOut\(true\);' -and $director -match 'protected static ref EXPG_RetireList s_Janitor') 'right before a native save the retire lists and the janitor keep their entities out again'

# Settings edited during a run wait for Regenerate: the run reads the copies BeginGenerate made.
$beginGenerate = Get-Body $moduleText 'protected\s+void\s+BeginGenerate\s*\('
foreach ($copy in 'm_sRunFaction = ResolveFaction\(0\);', 'm_sRunSecondFaction = ResolveFaction\(1\);', 'm_iRunSizes = m_iSizes;', 'm_iRunSquadsMin = m_iSquadsMin;', 'm_iRunSquadsMax = m_iSquadsMax;', 'm_iRunPlayerDistance = m_iPlayerDistance;', 'm_iRunBuildings = m_iBuildings;', 'm_iRunShare = m_iShare;', 'm_bRunExcludeSupport = m_bExcludeSupport;', 'm_bRunAllowGarrisoned = m_bAllowGarrisoned;') {
 Assert ($beginGenerate -match $copy) "BeginGenerate copies the generation input: $copy"
}
foreach ($reader in 'protected\s+void\s+StepCensus\s*\(', 'protected\s+void\s+StepCatalog\s*\(', 'protected\s+void\s+Select\s*\(', 'protected\s+bool\s+PromoteNext\s*\(', 'protected\s+string\s+Unavailable\s*\(', 'protected\s+void\s+Draw\s*\(', 'protected\s+bool\s+NearPlayers\s*\(', 'protected\s+void\s+ServiceSpawns\s*\(', 'protected\s+EXPG_SquadEntry\s+PickSquad\s*\(', 'protected\s+string\s+CatalogText\s*\(', 'protected\s+string\s+RunningText\s*\(') {
 Assert (!((Get-Body $moduleText $reader) -match '\b(m_iSizes|m_bExcludeSupport|m_iSquadsMin|m_iSquadsMax|m_iPlayerDistance|m_bAllowGarrisoned|m_iBuildings|m_iShare)\b|ResolveFaction\(')) "a running generation reads only its copies: $reader"
}

# The catalog check matches the squad picker: enabled sizes and the drawn faction, before any analysis.
Assert ($pool.Contains('int CountUsable(int sizes, bool excludeSupport, string factionId)') -and (Get-Body $pool 'int\s+CountUsable\s*\(') -match '\(sizes & entry\.Bucket\) == 0') 'CountUsable applies the size mask, support and faction filters of Usable'
Assert ((Get-Body $moduleText 'protected\s+void\s+StepCatalog\s*\(') -match 'CountUsable\(m_iRunSizes, m_bRunExcludeSupport, drawFaction\) > 0' -and (Get-Body $moduleText 'protected\s+EXPG_SquadEntry\s+PickSquad\s*\(') -match 'no squad of the chosen sizes for faction') 'every drawable faction is checked before any analysis; a catalog gap is not reported as too small'
Assert ((Read-Text (Join-Path $repo 'tests/EXPG_RandomGarrisonTest.c')) -match 'return Usable\(\);') 'the Rules contract covers CountUsable'

# One faction per building, also with Allow garrisoned.
Assert ($manager.Contains('int GarrisonFactions(IEntity building, notnull array<string> outFactions)') -and (Get-Body $manager 'int\s+GarrisonFactions\s*\(') -match 'LedgerSquad\(\)\.FactionName') 'EXPG_GarrisonManager.GarrisonFactions (a Full-cached squad by its snapshot)'
$unavailable = Get-Body $moduleText 'protected\s+string\s+Unavailable\s*\('
Assert ($unavailable -match 'manager\.GarrisonFactions\(site\.Structure, held\);' -and $unavailable -match '"garrisoned by several factions"' -and $unavailable -match 'heldKey != m_sRunFaction && heldKey != m_sRunSecondFaction') 'a building held by another faction, or by several, is never taken'
Assert ((Get-Body $moduleText 'protected\s+void\s+Draw\s*\(') -match 'GarrisonFactions\(site\.Structure, held\) == 1\) \{ site\.FactionId = held\[0\]; \}' -and (Get-Body $moduleText 'protected\s+void\s+ServiceSpawns\s*\(') -match 'SkipSite\(site, "garrisoned by another faction"\);') 'a garrisoned building keeps its faction; another faction arriving meanwhile skips it'

# Rules: buckets, presets, target (with a PowerShell model), token and key packing.
$rules = Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonRules.c')
$bucket = Get-Body $rules 'static\s+int\s+Bucket\s*\('
Assert ($bucket -match 'members <= 3\)\s*\{\s*return BUCKET_TEAM;' -and $bucket -match 'members <= 5\)\s*\{\s*return BUCKET_FIRETEAM;' -and $bucket -match 'members <= 9\)\s*\{\s*return BUCKET_SQUAD;' -and $bucket -match 'return BUCKET_LARGE;') 'buckets: 1-3, 4-5, 6-9, 10-32 soldiers'
Assert ((Get-Body $rules 'static\s+int\s+Target\s*\(') -match 'int byShare = \(eligible \* share \+ 99\) / 100;' -and $rules -match 'MAX_TARGET = 32;' -and $rules -match 'TOKEN_MAX = 16777215;') 'target min(buildings, ceil(eligible x share / 100), 32); 24-bit token halves'
function Model-Target([int]$Buildings, [int]$Share, [int]$Eligible) {
 if ($Eligible -le 0 -or $Buildings -le 0 -or $Share -le 0) { return 0 }
 [math]::Min([math]::Min($Buildings, [int][math]::Floor(($Eligible * $Share + 99) / 100)), 32)
}
Assert ((Model-Target 4 100 25) -eq 4 -and (Model-Target 32 10 25) -eq 3 -and (Model-Target 10 50 7) -eq 4 -and (Model-Target 40 100 100) -eq 32 -and (Model-Target 4 100 0) -eq 0) 'target model'
Assert ((Get-Body $rules 'static\s+void\s+Shuffle\s*\(') -match 'generator\.SetSeed\(seed\);' -and (Get-Body $rules 'static\s+void\s+Shuffle\s*\(') -match 'generator\.RandInt\(0, i \+ 1\)') 'seeded Fisher-Yates'
Assert ((Get-Body $rules 'static\s+vector\s+PackKey\s*\(') -match '\(hash >> 16\) & 65535' -and (Get-Body $rules 'static\s+string\s+Token\s*\(') -match '"rg:%1-%2"') 'faction keys pack as (hi16, lo16, 1); tokens read rg:<hi>-<lo>'

# Enforce gotchas in every new or touched source; new files are ASCII with LF.
$fixturePath = Join-Path $repo 'tests/EXPG_RandomGarrisonGameplay.c'
$contractPath = Join-Path $repo 'tests/EXPG_RandomGarrisonTest.c'
$checked = $moduleSources + @($spawnPath, $fixturePath, $contractPath)
foreach ($path in $checked) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep|Wait|external|native)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, '\b(?:int|float|bool|string|vector|IEntity|ref\s+\w+|\w+)\s+(?:Building|World|Faction|Node|Span|Group|Player|Shape)\s*(?:;|=|\[)')) "a field or variable is named like a vanilla type in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 foreach ($line in $text -split "`n") {
  $code = ($line -replace '"(?:[^"\\]|\\.)*"', '""') -replace '//.*$', ''
  Assert (!($code -match '\S.*\breturn\s+[^;\s]' -and $code -notmatch '^\s*return\b')) "non-void return must be on its own line in ${path}: $($line.Trim())"
 }
}
foreach ($file in @(Get-ChildItem -LiteralPath $module -Recurse -File) + @(Get-Item -LiteralPath $spawnPath, $fixturePath, $contractPath, $PSCommandPath)) {
 $bytes = [IO.File]::ReadAllBytes($file.FullName)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $($file.FullName)"
 Assert (!($bytes -contains 13)) "new files use LF line endings: $($file.FullName)"
}

# Contract and native fixture wiring (executed only by the orchestrator).
$plugin = Read-Text (Join-Path $repo 'tests/EXPG_ContractPlugin.c')
Assert ($plugin.Contains('PrintFormat("[EXPG RANDOM CONTRACT RESULT] rules=%1 shuffle=%2 packing=%3", randomRules, randomShuffle, randomPacking);') -and $plugin -match '&& randomRules && randomShuffle && randomPacking\)') 'the contract plugin runs and reports the Random Garrison contracts'
Assert ((Read-Text (Join-Path $repo 'tests/Run-Contracts.ps1')).Contains("`$text -match '\[EXPG RANDOM CONTRACT RESULT\] rules=1 shuffle=1 packing=1'")) 'Run-Contracts requires the Random Garrison contract line'
$contract = Read-Text $contractPath
foreach ($method in 'Rules', 'Shuffle', 'Packing') {
 Assert ($contract -match "static\s+bool\s+$method\s*\(\s*\)") "contract EXPG_RandomGarrisonTest.$method"
 Assert ($plugin -match "bool random$method = EXPG_RandomGarrisonTest\.$method\(\);") "the contract plugin runs EXPG_RandomGarrisonTest.$method"
}
$fixture = Read-Text $fixturePath
$regex = '\[EXPG RANDOM RESULT\] checks=[1-9]\d* failures=0 eligible=([6-9]|[1-9]\d+) excludedPlayers=[1-9]\d* buildings=4 squads=[4-8] distinct=1 inside=1 settings=1 nearPlayer=0 analysingSeen=1 cleared=1 leftovers=0 sameBuildings=1 samePrefabs=1 keptOnDelete=1 orphans=0 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EXPG_RandomGarrisonGameplay.c -ExpectResult '$regex' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'fixture must keep the runner driver class names'
Assert ($fixture -match 'Resource zoneResource = Resource\.Load\(ZONE_PREFAB\);' -and $fixture.Contains($prefabRef)) 'fixture spawns the real prefab by its GUID, Resource.Load kept in a local'
Assert ($fixture -match 'modded class EXPG_RandomGarrisonModule' -and $fixture -match 'override protected void ObserverPositions\(' -and $fixture -match 'modded class EXPG_GarrisonRecord') 'fixture seams: the fake player and the refusal counter'
$first = [regex]::Match($fixture, 'string first = string\.Format\("([^"]*)"').Groups[1].Value
$second = [regex]::Match($fixture, 'string second = string\.Format\("([^"]*)"').Groups[1].Value
$third = [regex]::Match($fixture, 'string third = string\.Format\("([^"]*)"').Groups[1].Value
Assert ($fixture.Contains('PrintFormat("[EXPG RANDOM RESULT] %1 %2 %3", first, second, third);') -and $first -and $second -and $third) 'fixture prints the RESULT line in three parts'
function Format-Result([string[]]$A, [string[]]$B, [string[]]$C) {
 $one = $first; for ($i = $A.Count; $i -ge 1; $i--) { $one = $one.Replace("%$i", $A[$i - 1]) }
 $two = $second; for ($i = $B.Count; $i -ge 1; $i--) { $two = $two.Replace("%$i", $B[$i - 1]) }
 $three = $third; for ($i = $C.Count; $i -ge 1; $i--) { $three = $three.Replace("%$i", $C[$i - 1]) }
 "[EXPG RANDOM RESULT] $one $two $three"
}
$pass = Format-Result @('41', '0', '17', '3', '4', '6', '1') @('1', '1', '0', '1', '1', '0') @('1', '1', '1', '0', 'completed')
Assert ($pass -match $regex) "a passing RESULT line must match the regex: $pass"
Assert (!((Format-Result @('41', '1', '17', '3', '4', '6', '1') @('1', '1', '0', '1', '1', '0') @('1', '1', '1', '0', 'completed')) -match $regex)) 'a failing RESULT line must not match'
Assert (!((Format-Result @('41', '0', '17', '0', '4', '6', '1') @('1', '1', '0', '1', '1', '0') @('1', '1', '1', '0', 'completed')) -match $regex)) 'no player exclusion must not match'
Assert (!((Format-Result @('41', '0', '17', '3', '3', '6', '1') @('1', '1', '0', '1', '1', '0') @('1', '1', '1', '0', 'completed')) -match $regex)) 'three buildings must not match'
Assert ((Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')).Contains($regex)) 'tests/GAMEPLAY.md must carry the fixture regex'

# Docs: one Unreleased section with the module; README module table.
$changelog = Read-Text (Join-Path $repo 'CHANGELOG.md')
$unreleased = [regex]::Matches($changelog, '(?m)^## Unreleased')
Assert ($unreleased.Count -le 1 -and $changelog.IndexOf('## 0.1.12') -ge 0 -and $changelog.IndexOf('## 0.1.12') -lt $changelog.IndexOf('## 0.1.11')) 'CHANGELOG keeps at most one Unreleased section and a 0.1.12 section above 0.1.11'
Assert ([regex]::Match($changelog, '(?s)## 0\.1\.12\b(.*?)(\n## |\z)').Groups[1].Value.Contains('Random Garrison')) 'the 0.1.12 section describes Random Garrison'
$readme = Read-Text (Join-Path $repo 'README.md')
Assert ($readme.Contains('| Random Garrison | `addon/random-garrison` |') -and $readme.Contains('**Random Garrison**')) 'README lists and describes the module'
'PASS: Random Garrison registered last (own Edit.conf, Systems.conf and GameMaster.conf with the vanilla identities), named without EXPBG, attribute keys equal the module keys (durable values, faction key hashes), shared validator and spawner for the zone and EXPBG Add Garrison, background analysis, GeneratedBy in the ledger (older ledgers load), native zone save, census and catalog bounds, Enforce gotchas, contract and fixture wired.'
