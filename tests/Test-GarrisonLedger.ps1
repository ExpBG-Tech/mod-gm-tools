#requires -Version 7.0
# Portable guard for the garrison save bridge (0.1.11): garrisons are saved by their own
# ledger in native saves and, with the EXPBG CDF Compat bridge, in CDF saves; garrison-
# owned entities stay out of both; a load creates the garrisons Full cached, remaps
# posts by position and pins AI until bound; casualties are never respawned; the first
# squad is never trimmed (overflow takes the reinforcement order). Checks source
# invariants, the native registration, Enforce gotchas and the ledger fixture wiring.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$garrison = Join-Path $repo 'addon/garrison'
$scripts = Join-Path $garrison 'Scripts/Game/EXPG'
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
$snapshotPath = Join-Path $scripts 'EXPG_Snapshot.c'
$exclusionPath = Join-Path $scripts 'EXPG_SaveExclusion.c'
$persistencePath = Join-Path $scripts 'EXPG_Persistence.c'
$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$fullPath = Join-Path $scripts 'EXPG_FullCache.c'
$safetyPath = Join-Path $scripts 'EXPG_SaveSafety.c'
$snapshot = Read-Text $snapshotPath
$exclusion = Read-Text $exclusionPath
$persistence = Read-Text $persistencePath
$manager = Read-Text $managerPath
$full = Read-Text $fullPath
$safety = Read-Text $safetyPath

# Ledger format: versioned, building identity, rows with casualties, reserved generator id.
Assert ($snapshot -match 'static\s+const\s+int\s+VERSION\s*=\s*1;' -and $snapshot.Contains('"expgGarrisonVersion"') -and $snapshot.Contains('"world"')) 'the ledger is versioned and bound to its world'
foreach ($field in @('string Token;', 'string GeneratedBy;', 'int OrderIndex;', 'ref EXPG_BuildingRef Site', 'int CacheMode', 'float WakeDistance', 'float SleepDistance', 'int CacheState;', 'bool ReleaseRequested;', 'ref EBG_CacheGroupSnapshot Squad;', 'ref array<ref EXPG_MemberSnapshot> Members')) {
 Assert ($snapshot.Contains($field)) "garrison snapshot must keep $field"
}
foreach ($field in @('bool Dead;', 'bool Fixed', 'int PostKind;', 'int NodeHint', 'vector LocalPost;', 'vector LocalLook;', 'vector LocalLast;', 'vector WorldLast;', 'ResourceName Prefab;', 'ref EBG_CacheAuthor Author')) {
 Assert ($snapshot.Contains($field)) "member snapshot must keep $field"
}
$find = Get-Body $snapshot 'IEntity\s+Find\s*\(\s*out\s+string\s+reason\s*\)'
$visit = Get-Body $snapshot 'protected\s+bool\s+Visit\s*\(\s*IEntity\s+entity\s*\)'
Assert ($find.Contains('"2 2 2"') -and $find.Contains('m_Matches > 1') -and $snapshot -match 'MAX_VISITS\s*=\s*512;' -and $visit.Contains('0.0025') -and $visit.Contains('SCR_DestructibleBuildingEntity')) 'buildings are found with the Ambient Destruction identity rule (2 m box, 512 visits, 5 cm rows, ambiguity fails closed)'
Assert ($snapshot -match 'MAX_MEMBERS\s*=\s*32;' -and $snapshot -match 'MAX_BUILDINGS\s*=\s*64;' -and $snapshot -match 'MAX_JSON\s*=\s*16000000;') 'ledger limits: 32 members, 64 buildings, 16 MB'
Assert ((Get-Body $snapshot 'static\s+bool\s+Validate\s*\(').Contains('PrefabType(SCR_ChimeraCharacter)') -and (Get-Body $snapshot 'static\s+bool\s+Validate\s*\(').Contains('duplicate garrison token')) 'validation checks soldier prefabs and unique tokens before anything changes'
Assert ($snapshot.Contains('Squad.Tolerant = true;')) 'the squad snapshot is read tolerantly (orders it could not own were never written)'

# Exclusion: native tracking and the CDF flag, re-stopped before every native save, handed back.
Assert ($exclusion.Contains('GetOnBeforeSave().Insert(OnBeforeSave)') -and $exclusion.Contains('StopTracking(entity)') -and $exclusion.Contains('StartTracking(entity)')) 'owned entities leave native tracking before every save and are handed back'
Assert ($exclusion.Contains('EEditableEntityFlag.NON_SERIALIZABLE') -and $exclusion.Contains('if (editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE)) { return; }')) 'only a NON_SERIALIZABLE flag set here is ever cleared'
$sync = Get-Body $manager 'void\s+SyncExclusion\s*\(\s*\)'
Assert ($sync.Contains('MODE_CDF_BRIDGED') -and $sync.Contains('group.GetWaypoints(orders);') -and $sync.Contains('OwnsActor(record, member)') -and $sync.Contains('EXPG_SaveExclusion.EndSync();')) 'exclusion covers squads, waypoints and living guards; the CDF flag only with the bridge'

# Native carrier registered in the merged GameMaster.conf with its own GUIDs.
$conf = Read-Text (Join-Path $garrison 'Configs/Systems/Persistence/GameMode/GameMaster.conf')
Assert ($conf.Contains('Serializer EXPG_GarrisonPersistenceSerializer') -and $conf.Contains('EXPG_GarrisonPersistenceState') -and $conf.Contains('PersistentStates + {')) 'the native garrison state and serializer are registered'
$ids = @([regex]::Matches($conf, '"\{([A-F0-9]{16})\}"') | ForEach-Object { $_.Groups[1].Value })
foreach ($other in Get-ChildItem -LiteralPath (Join-Path $repo 'addon') -Recurse -Filter '*.conf' -File | Where-Object { $_.FullName -ne (Join-Path $garrison 'Configs/Systems/Persistence/GameMode/GameMaster.conf') }) {
 $text = Read-Text $other.FullName
 foreach ($id in $ids) { if ($id -ne '6586D01E488CA578') { Assert (!$text.Contains($id)) "GUID $id of the garrison persistence config is reused in $($other.Name)" } }
}
Assert ((Read-Text (Join-Path $repo 'tools/pack.json')).Contains('"Configs/Systems/Persistence/GameMode/GameMaster.conf"')) 'GameMaster.conf is a merged config'
$serializer = Get-Body $persistence 'class\s+EXPG_GarrisonPersistenceSerializer\s*:\s*ScriptedStateSerializer'
Assert ($serializer.Contains('EDeserializeFailHandling.IGNORE') -and $serializer.Contains('SerializeNative(context)') -and $serializer.Contains('DeserializeNative(context)')) 'an unreadable native ledger never fails the whole native load'
Assert ((Get-Body $persistence 'static\s+ESerializeResult\s+SerializeNative\s*\(').Contains('ESerializeResult.DEFAULT')) 'a mission without garrisons writes nothing'

# Public API for the CDF bridge, handshake as an instance method.
foreach ($api in @('static bool CanExport(out string reason)', 'static bool Export(notnull array<ref EXPG_GarrisonSnapshot> ledger, out string reason)', 'static bool ExportJson(out string json, out string reason)', 'static bool OwnsForSave(IEntity entity)', 'static void SyncSaveExclusion()', 'static bool Importing()', 'static bool BeginImport(out string reason)', 'static void DiscardForImport(string why)', 'static bool QueueImport(array<ref EXPG_GarrisonSnapshot> ledger, out string reason)', 'static void FinishImport()', 'static void EndImport()', 'static bool ParseJson(string json, array<ref EXPG_GarrisonSnapshot> ledger, out string reason)', 'static void Notify(string message)')) {
 Assert ($persistence.Contains($api)) "EXPG_GarrisonPersistence must expose $api"
}
Assert ($persistence -match 'static\s+const\s+int\s+BRIDGE_API\s*=\s*1;') 'bridge API 1'
Assert ((Get-Body $manager 'protected\s+int\s+CdfBridgeVersion\s*\(\s*\)') -match 'return\s+0;') 'CdfBridgeVersion is a protected instance method returning 0 (the bridge overrides it)'
Assert ($manager -match 'protected\s+void\s+TryFullSleep\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)') 'TryFullSleep keeps its signature (CDF Compat 0.1.5 overrides it)'

# Save safety: native saves never refused for an active garrison; legacy keeps the 0.1.8 rules.
Assert (!$safety.Contains('EBG_MissionPersistenceSerializer') -and !$safety.Contains('Refused active garrison ownership')) 'the native save refusal is gone'
Assert ($safety -match 'if\s*\(\s*EXPG_GarrisonPersistence\.Legacy\(\)\s*\)\s*\{\s*EXPG_GarrisonManager\.RequestReleaseAll\(\);' -and $safety.Contains('EXPG_GarrisonPersistence.CanExport(reason)')) 'Prepare for Save releases garrisons only in CDF legacy mode; CanSave asks the ledger'
$allowed = Get-Body $manager 'bool\s+LedgerAllowed\s*\('
Assert ($allowed.Contains('MODE_CDF_LEGACY && HasActive()') -and $allowed.Contains('m_Importing && !m_PendingImport')) 'a save is refused only in legacy mode with active garrisons, or while a load replaces them'

# Durable Full, AI pinned in the spawning call, missing squad tolerated, squad rebound.
Assert ((Get-Body $full 'override\s+protected\s+bool\s+CapturesNativeGroup\s*\(') -match 'return\s+true;') 'Garrison Full is durable (the squad is captured and recreated)'
$spawned = Get-Body $full 'override\s+protected\s+void\s+OnSurvivorSpawned\s*\('
Assert ($spawned.Contains('KeepNewborn(row.Entity)') -and $spawned.Contains('Hold(row.Member, row.Entity)')) 'a respawned survivor is excluded and pinned in his spawning call'
$prefab = Read-Text (Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG/EBG_PrefabFullCache.c')
Assert ($prefab.IndexOf('OnSurvivorSpawned(row);') -gt 0 -and $prefab.IndexOf('OnSurvivorSpawned(row);') -lt $prefab.IndexOf('if (!m_Group.AddAIEntityToGroup(row.Entity)')) 'the spawn seam runs before the survivor joins the squad'
Assert ((Get-Body $full 'override\s+void\s+Poll\s*\(').Contains('m_Owner.AdoptRestoredGroup(m_Group)')) 'the recreated squad is rebound to the garrison in the spawning call'
$tick = Get-Body $manager 'protected\s+void\s+Tick\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
Assert ($tick.Contains('if (!record.Group && !record.Full) { record.RequestRelease("Squad deleted"); }') -and $tick.Contains('RecordModeInUse(record)') -and $tick.Contains('Near(record, record.WakeDistance)')) 'a Full-cached garrison without a squad is normal; settings live in the record'
Assert ((Get-Body $manager 'protected\s+bool\s+WakeFull\s*\(').Contains('full.GroupLost()')) 'a recreated squad deleted by a Game Master ends the garrison instead of wedging it'
$fullSleep = Get-Body $manager 'protected\s+void\s+TryFullSleep\s*\('
Assert ($fullSleep.Contains('record.DetachGroup();') -and $fullSleep.Contains('record.FullRefused = failure;')) 'a refused squad capture falls back to Simulation'

# Import: Full cached, plan first, remap by position, never a casualty.
$import = Get-Body $manager 'protected\s+EXPG_GarrisonRecord\s+ImportRecord\s*\('
Assert ($import.Contains('existing.Token == saved.Token') -and $import.Contains('if (row.Dead)') -and $import.Contains('ImportCached(saved.Squad, rows)') -and !$import.Contains('SpawnEntityPrefab')) 'an import is deduplicated by token, keeps casualties as rows and spawns nothing'
$remap = Get-Body $manager 'protected\s+void\s+Remap\s*\('
Assert ($remap.Contains('<= 0.01') -and $remap.Contains('NearestNode(post, 0.25, true)') -and $remap.Contains('member.Fixed = true;') -and $remap.Contains('record.ParkPosts();')) 'posts: saved node within 0.1 m, nearest within 0.25 m, else off-plan; a lost stop becomes a fixed post'
Assert ((Get-Body $manager 'protected\s+bool\s+ServiceLoaded\s*\(').Contains('plan.Structure && !plan.Done')) 'nobody spawns before the building is analysed'
Assert ((Get-Body $manager 'bool\s+CanFit\s*\(').Contains('IsImporting()') -and (Get-Body $manager 'static\s+bool\s+SaveInProgress\s*\(').Contains('IsImporting()')) 'Add Garrison is refused while garrisons load'
Assert ((Get-Body $manager 'protected\s+void\s+Pump\s*\(').Contains('if (m_Importing) { serviceCount = 0; }')) 'no record ticks while a carrier replaces the garrisons'

# Never trimmed: the first squad deploys in full; overflow takes the reinforcement order.
$initialize = Get-Body $manager 'protected\s+bool\s+Initialize\s*\('
Assert (!$initialize.Contains('Delete(') -and !$initialize.Contains('EXPG_ExpectFreshRemoval') -and $initialize.Contains('PlacementCount(agents.Count(), agents.Count())') -and $initialize.Contains('ChooseReinforcement(record, originals, placements); }')) 'the first squad is never trimmed; soldiers beyond the plan take the reinforcement order'
Assert ((Get-Body $manager 'protected\s+void\s+ChooseReinforcement\s*\(').Contains('foreach (EXPG_Placement chosen : placements) { occupied.Insert(chosen.Position); }')) 'overflow keeps clear of the posts the squad already took'
Assert (!(Read-Text (Join-Path $scripts 'EXPG_Editor.c')).Contains("use the building's safe capacity")) 'the Add Garrison reply no longer promises trimming'

# Unit Caching: tolerant ledger capture; eliminated loaded records forgotten, not held.
$cacheSnapshot = Read-Text (Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG/EBG_CacheSnapshot.c')
Assert ($cacheSnapshot.Contains('bool CaptureForLedger(SCR_AIGroup group, int lod, bool active)') -and $cacheSnapshot.Contains('if (!Tolerant) return CaptureRefusal("Full capture holds: " + order.CaptureProblem);')) 'ledger capture skips what it cannot own; Full capture still refuses'
$mission = Read-Text (Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG/EBG_MissionPersistence.c')
Assert ($mission.Contains('if (reason != "" && Eliminated(group))') -and $mission.Contains('manager.ForgetEliminated(record);')) 'an eliminated loaded Unit Caching record no longer holds every save'

# Per-soldier overrides ride in the ledger rows.
Assert ((Read-Text (Join-Path $repo 'addon/ai-surrender/Scripts/Game/EXPSR/ESR_OverrideCache.c')).Contains('modded class EXPG_MemberSnapshot') -and (Read-Text (Join-Path $repo 'addon/ai-surrender/Scripts/Game/EXPSR/ESR_OverrideCache.c')).Contains('"esrOverrides"')) 'AI Surrender soldier overrides are saved with garrison rows'
Assert ((Read-Text (Join-Path $repo 'addon/ai-global-skills/Scripts/Game/EXPGS/EGS_OverrideCache.c')).Contains('modded class EXPG_MemberSnapshot') -and (Read-Text (Join-Path $repo 'addon/ai-global-skills/Scripts/Game/EXPGS/EGS_OverrideCache.c')).Contains('"egsRoe"')) 'AI Global Skills soldier ROE is saved with garrison rows'

# Release All Garrisons context action.
$tempEdit = Read-Text (Join-Path $garrison 'Configs/Editor/ActionLists/Context/TempEdit.conf')
Assert ($tempEdit.Contains('EXPG_ReleaseAllGarrisonsContextAction') -and $tempEdit.Contains('m_bIsServer 1')) 'Release All Garrisons is a server context action'

# Enforce gotchas and encoding of the new and touched sources.
$fixturePath = Join-Path $repo 'tests/EXPG_LedgerGameplay.c'
$new = @($snapshotPath, $exclusionPath, $persistencePath, $fixturePath, $PSCommandPath)
foreach ($path in @($snapshotPath, $exclusionPath, $persistencePath, $managerPath, $fullPath, $safetyPath, $fixturePath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|external|native)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, '\b(?:int|float|bool|string|vector|IEntity|ref\s+\w+|\w+)\s+(?:Building|World|Faction|Node|Span|Group)\s*(?:;|=|\[)') -or $path -eq $managerPath -or $path -eq $fixturePath) "field named like a vanilla type in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 Assert (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $path"
 if ($path -in @($snapshotPath, $exclusionPath, $persistencePath)) { Assert (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\breturn\b[^\r\n]*\}\s*$')) "non-void method with its return on the signature line: $path" }
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
foreach ($path in $new) { Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $path" }

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG LEDGER RESULT\] checks=[1-9]\d* failures=0 garrisons=3 buildings=2 awake=1 simulation=1 full=1 posts=1 patrollers=[1-9]\d* respawnedDead=0 duplicates=0 aiBeforeBind=0 overrides=1 excluded=1 nativeLedger=1 nativeSave=(1|na) fullWoke=1 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EXPG_LedgerGameplay.c -ExpectResult '$expect' -TimeoutSeconds 600")) 'ledger fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'ledger fixture must keep the runner driver class names'
Assert ($fixture.Contains('PrintFormat("[EXPG LEDGER RESULT] %1 %2", first, second);') -and $fixture.Contains('EXPG_GarrisonPersistence.DiscardForImport("fixture scene clear");') -and $fixture.Contains('EXPG_GarrisonPersistence.DeserializeNative(load)') -and $fixture.Contains('SampleHolds();')) 'ledger fixture must clear, load through the native serializer path and sample AI holds every frame'
Assert ((Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')).Contains($expect)) 'tests/GAMEPLAY.md must document the ledger fixture regex'
'PASS: garrison ledger (versioned, building identity, casualty rows, generator id reserved), native and CDF exclusion with hand-back, native state registered, bridge API seam, legacy-only release, durable Full with spawn-call AI pins and squad rebind, import by token with plan-first remap, no trimming, ledger fixture wired.'
