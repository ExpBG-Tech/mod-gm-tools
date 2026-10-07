#requires -Version 7.0
# Portable guard for the WP1b unit-caching scheduler and history work (0.1.15 performance
# plan). Same outcomes, less repeated work:
#  1 possession: UpdatePlayers reruns a player's PlayerPossession only when his entity
#    changes, after any record/member/ledger binding change (BumpRoster, the cleanup
#    ledger's s_EBG_BindVersion) and on his round-robin turn. OnControlledByPlayer still
#    handles live possession at once.
#  2 player history: O(1) lookups (flag shortcut, s_IdIndex, EverContains mirror).
#  3 released negatives, persistence part: HasUnresolvedRelease mirror, ValidIds dedupe.
#  4 protection: pumps recompute protection only when marked dirty; tick invalidation stays;
#    cached verdicts carry the zone settings they were computed with.
#  5 enrollment: one candidate list per tick pass, shared by every zone; FindGroup,
#    IsReserved and the skip reasons stay live.
#  6 soldiers-only: first group per zone named in full, then a count line at most every
#    300 s; deleted groups pruned from the set.
# Every index or cache keeps its old path as an EBG_DebugChecks cross-check. No engine is
# launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
# Comments and string contents blanked (same length, same lines), as in Test-PerformanceGuards.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
function Read-Source([string]$Name) {
 $text = [IO.File]::ReadAllText((Join-Path $scripts $Name))
 [pscustomobject]@{ Name = $Name; Text = $Text; Code = (Get-Code $text) }
}
# Body of the first declaration matching $Signature at or after $From: @{ Code; Text; Open; Close }.
function Get-Body($Source, [string]$Signature, [int]$From = 0) {
 $match = [regex]::new($Signature).Match($Source.Code, $From)
 Assert $match.Success "$($Source.Name): signature not found: $Signature"
 $open = $Source.Code.IndexOf('{', $match.Index + $match.Length - 1); $depth = 0
 for ($i = $open; $i -lt $Source.Code.Length; $i++) {
  if ($Source.Code[$i] -eq '{') { $depth++ }
  elseif ($Source.Code[$i] -eq '}') {
   $depth--
   if ($depth -eq 0) { return @{ Code = $Source.Code.Substring($open + 1, $i - $open - 1); Text = $Source.Text.Substring($open + 1, $i - $open - 1); Open = $open; Close = $i } }
  }
 }
 throw "FAIL: $($Source.Name): unbalanced body for $Signature"
}
function Count([string]$Text, [string]$Pattern) { [regex]::Matches($Text, $Pattern).Count }
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = $Text.IndexOf($First); $b = $Text.IndexOf($Second)
 return $a -ge 0 -and $b -ge 0 -and $a -lt $b
}
$oneLineReturn = '\bif\s*\([^;{}]*\)\s*return\s+[^;\s][^;]*;'

$manager = Read-Source 'EBG_CacheManager.c'
$coordinator = Read-Source 'EBG_CacheFullCoordinator.c'
$regroup = Read-Source 'EBG_CacheRegroup.c'
$persistence = Read-Source 'EBG_MissionPersistence.c'
$history = Read-Source 'EBG_PlayerHistory.c'
$callbacks = Read-Source 'EBG_MissionGroupCallbacks.c'

# ---------------------------------------------------------------------------------------
# Contracts C3/C4: seams other modules and six fixtures override keep their signatures.
# ---------------------------------------------------------------------------------------
foreach ($signature in 'void\s+Refresh\s*\(\s*EBG_CacheZone\s+zone\s*,\s*array<AIAgent>\s+sharedAgents\s*=\s*null\s*\)', 'protected\s+void\s+UpdatePlayers\s*\(\s*\)', 'bool\s+IsReserved\s*\(\s*SCR_AIGroup\s+group\s*\)', 'EBG_CacheMember\s+FindMember\s*\(\s*IEntity\s+entity\s*\)', 'bool\s+DescribeExternalCache\s*\(\s*SCR_AIGroup\s+group\s*,\s*out\s+string\s+moduleName\s*,\s*out\s+string\s+cacheState\s*\)') {
 Assert ([regex]::IsMatch($manager.Code, $signature)) "published seam signature changed: $signature"
}

# ---------------------------------------------------------------------------------------
# 1 Possession only on change
# ---------------------------------------------------------------------------------------
$players = (Get-Body $manager 'protected\s+void\s+UpdatePlayers\s*\(\s*\)').Code
Assert ($players.TrimStart().StartsWith('Players.Clear();')) 'UpdatePlayers must clear Players first (fixtures call super, then add presence)'
foreach ($needle in 'm_PossessionSeen', 'sweep', 'm_RosterVersion', 'EBG_CacheCleanup.s_EBG_BindVersion', 'Players.Insert(player.GetOrigin());', 'PlayerPossession(player);', 'm_PossessionSeen.Set(id, player);', 'm_PossessionSeen.Remove(id);') {
 Assert $players.Contains($needle) "UpdatePlayers lost: $needle"
}
Assert ([regex]::IsMatch($players, 'if\s*\(\s*i\s*==\s*sweep\s*\|\|\s*m_PossessionSeen\.Get\(id\)\s*!=\s*player\s*\)')) 'a player reruns possession on his round-robin turn or when his controlled entity changed'
Assert ([regex]::IsMatch($players, '(?s)if\s*\(\s*m_SeenRoster\s*!=\s*m_RosterVersion\s*\|\|\s*m_SeenBind\s*!=\s*EBG_CacheCleanup\.s_EBG_BindVersion.*?\)\s*\{\s*m_PossessionSeen\.Clear\(\);')) 'a roster or ledger binding change must clear the possession memory (every player reruns)'
Assert (Before $players 'Players.Insert(player.GetOrigin());' 'PlayerPossession(player);') 'every living player still feeds Players before his possession pass'
Assert ([regex]::IsMatch($players, 'else\s+if\s*\(\s*EBG_DebugChecks\.Enabled\s*\)\s*CheckPossession\(player\);')) 'a skipped pass must be cross-checked under EBG_DebugChecks'
# Player seam: fixtures stand spawned characters in for players; production reads the player manager.
Assert ($players.Contains('EBG_CollectPlayers(m_PlayerIds);') -and $players.Contains('SCR_ChimeraCharacter.Cast(EBG_PlayerEntity(id));') -and !$players.Contains('GetPlayerManager()')) 'UpdatePlayers must read players through the EBG_CollectPlayers and EBG_PlayerEntity seam'
Assert ([regex]::IsMatch((Get-Body $manager 'protected\s+void\s+EBG_CollectPlayers\s*\(\s*notnull\s+array<int>\s+ids\s*\)').Code, '^\s*GetGame\(\)\.GetPlayerManager\(\)\.GetPlayers\(ids\);\s*$')) 'EBG_CollectPlayers must only read the connected players'
Assert ([regex]::IsMatch((Get-Body $manager 'protected\s+IEntity\s+EBG_PlayerEntity\s*\(\s*int\s+id\s*\)').Code, '^\s*return\s+GetGame\(\)\.GetPlayerManager\(\)\.GetPlayerControlledEntity\(id\);\s*$')) 'EBG_PlayerEntity must only return the controlled entity'
$possession = (Get-Body $manager 'void\s+PlayerPossession\s*\(\s*IEntity\s+entity\s*\)').Code
foreach ($needle in 'character.EBG_MarkPlayerControlled();', 'FindMember(entity);', 'member.WasPlayer = true;', 'RestoreRecord(record);', 'EBG_CacheCleanup.Instance.PlayerPossession(entity);') {
 Assert $possession.Contains($needle) "PlayerPossession itself must stay unchanged: $needle"
}
Assert ((Get-Body $manager 'override\s+protected\s+void\s+OnControlledByPlayer\s*\(').Code.Contains('EBG_CacheManager.Instance.PlayerPossession(owner);')) 'live possession stays event-driven'
$bump = (Get-Body $manager 'void\s+BumpRoster\s*\(\s*\)').Code
Assert ($bump.Contains('m_RosterVersion++;') -and $bump.Contains('MarkProtectionDirty();')) 'BumpRoster must advance the roster version and mark protection dirty'
# Every Records mutation is followed (same line or within two lines) by BumpRoster().
function Assert-Bumped($Source, [string]$Pattern) {
 $lines = $Source.Code -split "`n"
 $sites = 0
 for ($i = 0; $i -lt $lines.Count; $i++) {
  $m = [regex]::Match($lines[$i], $Pattern)
  if (!$m.Success) { continue }
  $sites++
  $window = $lines[$i].Substring($m.Index) + "`n" + (($lines[($i + 1)..([Math]::Min($i + 2, $lines.Count - 1))]) -join "`n")
  Assert ($window -match 'BumpRoster\(\)') "$($Source.Name):$($i + 1) changes Records without BumpRoster(): $($lines[$i].Trim())"
 }
 return $sites
}
$managerSites = Assert-Bumped $manager '(?<![\w.])(?:Instance\.)?Records\.(?:Insert|Remove|RemoveItem|Clear)\('
$regroupSites = Assert-Bumped $regroup '\bmanager\.Records\.(?:Insert|Remove|RemoveItem|Clear)\('
Assert ($managerSites -ge 10) "expected the manager's Records mutation sites (found $managerSites)"
Assert ($regroupSites -ge 2) "expected the regroup's Records removals (found $regroupSites)"
# Member bindings of pre-existing entities also bump (regroup newcomers, saved-mission binding).
Assert ((Get-Body $manager 'EBG_CacheMember\s+AddRegroupMember\s*\(').Code -match 'member\.Entity\s*=\s*entity;[\s\S]*BumpRoster\(\);') 'AddRegroupMember binds an existing entity: BumpRoster'
$prepare = (Get-Body $manager 'EBG_CacheGroup\s+PreparePersistentGroup\s*\(').Code
Assert ($prepare -match 'member\.Entity\s*=\s*SCR_ChimeraCharacter\.Cast\(system\.FindById\(source\.Id\)\);[\s\S]*BumpRoster\(\);\s*if\s*\(\s*!record\.PersistentResolutionLogged') 'PreparePersistentGroup re-binds zone, group and members: BumpRoster after the member loop'
Assert ((Get-Body $regroup 'bool\s+RetireLeftSquad\s*\(').Code -match 'owner\.Members\.RemoveItem\(member\);\s*manager\.BumpRoster\(\);') 'RetireLeftSquad changes a roster outside the tick: BumpRoster'

# ---------------------------------------------------------------------------------------
# 2 Player history in O(1)
# ---------------------------------------------------------------------------------------
Assert ((Get-Body $history 'bool\s+EBG_IsMarkedPlayer\s*\(\s*\)').Code -match '^\s*return\s+m_EBG_EverPlayerControlled;\s*$') 'EBG_IsMarkedPlayer returns the latch alone'
Assert ((Get-Body $history 'void\s+EBG_MarkPlayerControlled\s*\(\s*\)').Code -match 'm_EBG_EverPlayerControlled\s*=\s*true;\s*EBG_MissionPlayerHistory\.Mark\(this\);') 'the latch is set before the history lists the character (the shortcut relies on it)'
$markCallers = @(Get-ChildItem -LiteralPath (Join-Path $repo 'addon') -Recurse -Filter '*.c' -File | Where-Object { (Get-Code ([IO.File]::ReadAllText($_.FullName))) -match 'EBG_MissionPlayerHistory\.Mark\(' })
Assert ($markCallers.Count -eq 1 -and $markCallers[0].Name -eq 'EBG_PlayerHistory.c') 'only EBG_MarkPlayerControlled may call EBG_MissionPlayerHistory.Mark'
$historyClass = Get-Body $persistence 'class\s+EBG_MissionPlayerHistory\b'
Assert ([regex]::IsMatch($historyClass.Code, 'protected\s+static\s+ref\s+map<UUID,\s*bool>\s+s_IdIndex\s*;')) 's_IdIndex is a lazy static map (no initializer)'
Assert ([regex]::IsMatch((Get-Body $persistence 'protected\s+static\s+void\s+Ensure\s*\(\s*\)' $historyClass.Open).Code, 's_Ids\s*=\s*\{\s*\}\s*;\s*s_IdIndex\s*=\s*new\s+map<UUID,\s*bool>\(\);')) 'Ensure creates s_IdIndex together with s_Ids'
Assert ((Count $historyClass.Code 's_Ids\.Insert\(') -eq 1 -and (Count $historyClass.Code 's_IdIndex\.(Insert|Set)\(') -eq 1 -and (Get-Body $persistence 'protected\s+static\s+void\s+AddId\s*\(' $historyClass.Open).Code -match 's_Ids\.Insert\(id\);\s*s_IdIndex\.Set\(id,\s*true\);') 's_Ids and s_IdIndex grow together, only in AddId'
$contains = (Get-Body $persistence 'static\s+bool\s+Contains\s*\(\s*IEntity\s+entity\s*\)' $historyClass.Open).Code
Assert ([regex]::IsMatch($contains, '\(\s*!character\s*\|\|\s*character\.EBG_IsMarkedPlayer\(\)\s*\)\s*&&\s*s_Observed\.Contains\(entity\)')) 'Contains scans the observed list only for a flagged (or non-character) entity'
Assert ($contains -match 'bool\s+known\s*=\s*s_IdIndex\.Contains\(id\);' -and $contains -match 'return\s+EverContains\(state,\s*id\);') 'Contains uses s_IdIndex and EverContains'
Assert (!($contains -match 'EverPlayerIds\.Contains\(')) 'Contains must not scan EverPlayerIds'
$export = (Get-Body $persistence 'static\s+bool\s+Export\s*\(\s*array<UUID>\s+ids\s*\)' $historyClass.Open).Code
Assert (!($export -match '\bids\.Contains\(') -and $export -match 'map<UUID,\s*bool>\s+present') 'Export dedupes through one map built from ids'
Assert ((Count $export 'ids\.Insert\(observed\);') -eq 1 -and (Before $export 'foreach (UUID existing : ids)' 'foreach (UUID observed : s_Ids)')) 'Export appends the missing ids in s_Ids order'
foreach ($source in $callbacks, $persistence) {
 Assert (!($source.Code -match 'EverPlayerIds\.Contains\(')) "$($source.Name) must use EBG_MissionPlayerHistory.EverContains, not EverPlayerIds.Contains"
}
Assert ((Count $callbacks.Code 'EBG_MissionPlayerHistory\.EverContains\(state,\s*(row\.MemberId|original\.Id)\)') -eq 2) 'both callback preflight checks use EverContains'
Assert ($persistence.Code -match 'EBG_MissionPlayerHistory\.EverContains\(state,\s*member\.Id\)') 'PreserveOriginalGroupDuringSetup uses EverContains'
$ever = (Get-Body $persistence 'static\s+bool\s+EverContains\s*\(\s*EBG_MissionPersistenceState\s+state\s*,\s*UUID\s+id\s*\)').Code
Assert ($ever -match 's_EverMirror\.Contains\(state,\s*state\.EverPlayerIds,\s*id\)') 'EverContains reads the EverPlayerIds mirror'

# ---------------------------------------------------------------------------------------
# 3 Persisted id mirrors and ValidIds
# ---------------------------------------------------------------------------------------
$mirror = (Get-Body $persistence 'bool\s+Contains\s*\(\s*EBG_MissionPersistenceState\s+state\s*,\s*array<UUID>\s+source\s*,\s*UUID\s+id\s*\)').Code
Assert ([regex]::IsMatch($mirror, 'if\s*\(\s*state\s*!=\s*m_State\s*\|\|\s*source\s*!=\s*m_Source\s*\|\|\s*state\.DeserializeCalls\s*!=\s*m_Calls\s*\|\|\s*source\.Count\(\)\s*!=\s*m_Count\s*\)')) 'the mirror rebuilds on a new state, list instance, load or length'
Assert ($mirror -match 'if\s*\(\s*EBG_DebugChecks\.Enabled\s*&&\s*found\s*!=\s*source\.Contains\(id\)\s*\)') 'the mirror is cross-checked against the list under EBG_DebugChecks'
$unresolved = (Get-Body $persistence 'static\s+bool\s+HasUnresolvedRelease\s*\(\s*UUID\s+id\s*\)').Code
Assert ($unresolved -match 's_LineageMirror\.Contains\(state,\s*state\.ReleasedLineageMembers,\s*id\)' -and !($unresolved -match 'ReleasedLineageMembers\.Contains\(')) 'HasUnresolvedRelease reads the ReleasedLineageMembers mirror'
Assert ((Before $unresolved 'state.Ensure();' 'id.IsNull()')) 'HasUnresolvedRelease still ensures the state before testing the id'
$valid = (Get-Body $persistence 'static\s+bool\s+ValidIds\s*\(\s*array<UUID>\s+ids\s*\)').Code
Assert ($valid -match 'map<UUID,\s*bool>\s+seen' -and !($valid -match 'array<UUID>\s+seen')) 'ValidIds dedupes through a map'
Assert ($valid -match 'ids\.Count\(\)\s*>\s*65536' -and $valid -match 'id\.IsNull\(\)\s*\|\|\s*seen\.Contains\(id\)') 'ValidIds keeps the 65536 cap and refuses null or repeated ids'
$deserialize = (Get-Body $persistence 'override\s+protected\s+bool\s+Deserialize\s*\(').Code
Assert ($deserialize -match 'state\.ReleasedOrigins\.Clear\(\);\s*EBG_MissionPersistence\.ForgetIdMirrors\(\);') 'every load forgets the id mirrors right after clearing the lists'
Assert (Before $deserialize 'EBG_MissionPersistence.ForgetIdMirrors();' 'state.DeserializeCalls++;') 'the mirrors are forgotten before the load completes'

# ---------------------------------------------------------------------------------------
# 4 Protection recomputed only when dirty
# ---------------------------------------------------------------------------------------
$pump = (Get-Body $manager 'void\s+Pump\s*\(\s*\)').Code
Assert ([regex]::IsMatch($pump, 'if\s*\(\s*m_ProtectionDirty\s*\)\s*InvalidateProtection\(\);')) 'Pump invalidates protection only when dirty'
Assert ((Count $pump 'InvalidateProtection\(\)') -eq 1) 'Pump has no other invalidation'
Assert ([regex]::IsMatch($pump, 'if\s*\(\s*EBG_DebugChecks\.Enabled\s*&&\s*!m_ProtectionDirty\s*\)\s*CheckProtectionCache\(\);')) 'a kept cache is cross-checked under EBG_DebugChecks'
$tick = (Get-Body $manager 'void\s+Tick\s*\(\s*\)').Code
Assert ((Count $tick '(?<![\w.])InvalidateProtection\(\);') -eq 2) 'Tick keeps both of its invalidations'
$invalidate = (Get-Body $manager 'void\s+InvalidateProtection\s*\(\s*\)').Code
foreach ($needle in 'm_Protection.Clear();', 'm_ProtectionDirty = false;', 'm_ProtectionVersion++;') { Assert $invalidate.Contains($needle) "InvalidateProtection lost: $needle" }
Assert ($manager.Code -match 'protected\s+bool\s+m_ProtectionDirty\s*=\s*true;') 'protection starts dirty'
$sample = Get-Body $manager 'class\s+EBG_ZoneProtection\b'
$matchBody = (Get-Body $manager 'bool\s+Matches\s*\(\s*EBG_CacheZone\s+zone\s*\)' $sample.Open).Code
foreach ($field in 'Strategy', 'ZoneWake', 'ZoneSleep', 'GroupWake', 'GroupSleep', 'Height', 'Above', 'Below', 'HeightMargin') {
 Assert ([regex]::IsMatch($matchBody, "\b$field\s*==\s*zone\.$field\b")) "a cached verdict must be recomputed when $field changes"
 Assert ([regex]::IsMatch((Get-Body $manager 'void\s+Capture\s*\(\s*EBG_CacheZone\s+zone\s*\)' $sample.Open).Code, "\b$field\s*=\s*zone\.$field;")) "Capture must record $field"
}
Assert ($matchBody -match 'Origin\s*==\s*zone\.GetOrigin\(\)') 'a moved zone recomputes its verdicts'
$protected = (Get-Body $manager 'bool\s+IsProtected\s*\(\s*EBG_CacheGroup\s+record\s*,\s*bool\s+sleep\s*\)').Code
Assert ([regex]::IsMatch($protected, 'record\.ProtectionVersion\s*!=\s*m_ProtectionVersion\s*\|\|\s*!sample\.Matches\(zone\)')) 'per-group verdicts are valid for one invalidation version and the same settings'
Assert ([regex]::IsMatch($protected, '!sample\s*\|\|\s*!sample\.Matches\(zone\)\s*\)\s*\{\s*sample\s*=\s*new\s+EBG_ZoneProtection\(\);\s*sample\.Capture\(zone\);\s*m_Protection\.Set\(zone,\s*sample\);')) 'whole-zone verdicts are recomputed for a moved or re-set zone'
Assert ($protected -match 'protectedArea\s*=\s*InVolume\(record,\s*sleep\);' -and $protected -match 'protectedArea\s*=\s*ZoneInVolume\(zone,\s*sleep\);') 'IsProtected computes the same per-group and whole-zone verdicts'
$dirtySites = @(
 @($manager, 'void\s+ConfirmDeath\s*\(\s*IEntity\s+entity\s*\)', 'a death between ticks'),
 @($manager, 'void\s+Release\s*\(\s*EBG_CacheZone\s+zone\s*\)', 'a zone release'),
 @($manager, 'EBG_CacheMember\s+AddCachedMember\s*\(', 'a cached member'),
 @($coordinator, 'static\s+bool\s+Sleep\s*\(', 'a Full capture'),
 @($coordinator, 'static\s+bool\s+BindAndRelease\s*\(', 'a Full rebind'),
 @($coordinator, 'static\s+void\s+CompleteRelease\s*\(', 'a completed Full restore'),
 @($regroup, 'protected\s+bool\s+Commit\s*\(', 'a regroup commit'))
foreach ($site in $dirtySites) {
 Assert ((Get-Body $site[0] $site[1]).Code -match 'MarkProtectionDirty\(\)') "$($site[2]) must mark protection dirty ($($site[0].Name))"
}
$coordinatorTick = (Get-Body $coordinator 'static\s+bool\s+Tick\s*\(\s*EBG_CacheManager\s+manager\s*\)').Code
Assert ($coordinatorTick -match 'if\s*\(\s*pending\s*\)\s*\{\s*manager\.MarkProtectionDirty\(\);' -and $coordinatorTick -match 'manager\.MarkProtectionDirty\(\);\s*if\s*\(\s*!selected\.Full\.BeginWake\(\)\s*\)') 'a Full transition in flight keeps protection recomputed every pump'

# ---------------------------------------------------------------------------------------
# 5 One enrollment candidate list per pass
# ---------------------------------------------------------------------------------------
$refresh = Get-Body $manager 'void\s+Refresh\s*\(\s*EBG_CacheZone\s+zone'
Assert ($refresh.Code -match 'EnrollmentCandidates\(agents,\s*sharedAgents\s*!=\s*null\)' -and $refresh.Code -match 'foreach\s*\(\s*EBG_EnrollmentCandidate\s+candidate\s*:\s*candidates\s*\)') 'Refresh walks the pass candidates'
Assert ([regex]::IsMatch($refresh.Code, 'if\s*\(\s*!EBG_EnrollmentTally\.Nearby\(candidate,\s*zoneOrigin,\s*affectedSq\)\)\s*continue;')) 'Refresh tests the candidate with the same Nearby verdict'
Assert (!($refresh.Code -match '\.GetAgents\(')) 'Refresh no longer reads each group per zone'
Assert ([regex]::IsMatch($refresh.Code, 'if\s*\(\s*EBG_DebugChecks\.Enabled\s*\)\s*CheckCandidate\(candidate,\s*zoneOrigin,\s*affectedSq\);')) 'a shared candidate is cross-checked against a live read under EBG_DebugChecks'
foreach ($needle in 'FindGroup(group)', 'IsReserved(group)', 'MemberSkip(members, enrollmentPersistence)', 'EBG_MissionPersistence.Reserves(') { Assert $refresh.Code.Contains($needle) "Refresh must keep the live check: $needle" }
$candidates = (Get-Body $manager 'protected\s+array<ref\s+EBG_EnrollmentCandidate>\s+EnrollmentCandidates\s*\(').Code
Assert ([regex]::IsMatch($candidates, 'if\s*\(\s*shared\s*&&\s*m_Candidates\s*&&\s*agents\s*==\s*m_CandidateAgents\s*&&\s*m_CandidatesTick\s*==\s*m_Ticks\s*\)')) 'shared candidates are reused only for the same agent list and tick'
Assert ($candidates -match 'if\s*\(\s*visited\.Contains\(group\)\s*\)\s*continue;\s*visited\.Insert\(group,\s*true\);' -and $candidates -match 'if\s*\(\s*!group\s*\)\s*group\s*=\s*SCR_AIGroup\.Cast\(agent\.GetParentGroup\(\)\);') 'candidates keep first-seen agent order, one per group'
Assert ((Before $tick 'Refresh(zone, enrollmentAgents);' 'm_Candidates = null;') -and (Before $tick 'm_Candidates = null;' 'foreach (EBG_CacheGroup record : Records) UpdateRecord(record);')) 'Tick releases the pass candidates after its zone loop'
$candidateSample = (Get-Body $manager 'void\s+Sample\s*\(\s*SCR_AIGroup\s+group\s*\)').Code
$nearbyOld = (Get-Body $manager 'static\s+bool\s+Nearby\s*\(\s*SCR_AIGroup\s+group').Code
foreach ($read in 'if (!member) continue;', 'SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(member.GetControlledEntity());', 'if (!character) continue;', 'CharacterControllerComponent controller = character.GetCharacterController();', 'if (!controller || controller.IsDead()) continue;') {
 Assert ($candidateSample.Contains($read) -and $nearbyOld.Contains($read)) "the candidate must make the same member reads as Nearby: $read"
}
$nearbyNew = (Get-Body $manager 'static\s+bool\s+Nearby\s*\(\s*EBG_EnrollmentCandidate\s+candidate').Code
Assert ($nearbyNew -match 'return\s+candidate\.Living\.IsEmpty\(\)\s*&&\s*!candidate\.SpawnDone\s*&&\s*EBG_CacheGeometry\.DistanceSq\(candidate\.GroupOrigin,\s*origin\)\s*<=\s*radiusSq;' -and $nearbyOld -match 'return\s+!living\s*&&\s*!group\.EBG_HasCompletedInitialSpawn\(\)\s*&&\s*EBG_CacheGeometry\.DistanceSq\(group\.GetOrigin\(\),\s*origin\)\s*<=\s*radiusSq;') 'the candidate Nearby keeps the spawning-group rule'
Assert ((Count $manager.Code '\bGetAIAgents\s*\(') -le 3) 'no new world agent scan in the manager'

# ---------------------------------------------------------------------------------------
# 6 Soldiers-only log and set
# ---------------------------------------------------------------------------------------
$note = Get-Body $manager 'protected\s+void\s+NoteSoldiersOnly\s*\('
Assert ($note.Text.Contains('PrintFormat("[EBG] Soldiers only: zone left group %1 alone (faction ''%2'', utility=%3). Switch ''Soldiers only'' off on the zone to cache it.", group, group.GetFactionName(), group.FindComponent(SCR_AIGroupUtilityComponent) != null);')) 'the first skipped group of a zone keeps its full log line'
Assert ([regex]::IsMatch($note.Code, 'if\s*\(\s*note\s*\)\s*\{\s*note\.Skipped\+\+;\s*return;\s*\}')) 'later groups of a zone are only counted'
$flush = Get-Body $manager 'protected\s+void\s+FlushSoldiersOnly\s*\('
Assert ($flush.Text.Contains('PrintFormat("[EBG] Soldiers only: %1 more civilian groups skipped near %2", note.Skipped, zone.GetOrigin());')) 'the summary line names the count and the zone'
Assert ($flush.Code -match 'if\s*\(\s*now\s*<\s*note\.NextSummary\s*\)\s*return;' -and $flush.Code -match 'note\.NextSummary\s*=\s*now\s*\+\s*300;' -and $note.Code -match 'note\.NextSummary\s*=\s*Now\(\)\s*\+\s*300;') 'at most one summary per zone every 300 s'
Assert ($flush.Code -match 'if\s*\(\s*!note\s*\|\|\s*note\.Skipped\s*==\s*0\s*\)\s*return;') 'no summary while nothing new was skipped'
Assert ($refresh.Code -match '(?s)PruneSoldiersOnlySkipped\(\);.*?if\s*\(\s*!m_SoldiersOnlySkipped\.Contains\(group\)\s*\)\s*\{\s*m_SoldiersOnlySkipped\.Insert\(group\);\s*NoteSoldiersOnly\(zone,\s*group\);') 'deleted groups are pruned before the set is read and grown'
Assert ($refresh.Code -match 'FlushSoldiersOnly\(zone\);') 'each enrollment pass may flush the zone summary'
Assert ((Get-Body $manager 'protected\s+void\s+PruneSoldiersOnlySkipped\s*\(').Code -match 'if\s*\(\s*!m_SoldiersOnlySkipped\.Get\(i\)\s*\)\s*m_SoldiersOnlySkipped\.Remove\(i\);') 'the prune drops null entries in a reverse loop'

# ---------------------------------------------------------------------------------------
# Enforce gotchas in the touched sources
# ---------------------------------------------------------------------------------------
foreach ($source in $manager, $coordinator, $regroup, $persistence, $history, $callbacks) {
 Assert (![regex]::IsMatch($source.Code, '\b(int|float|bool|string|vector|auto|IEntity|array<[^>]+>|map<[^>]+>|set<[^>]+>)\s+(owned|Sleep|Wait)\b\s*[;=,)]')) "reserved Enforce name declared in $($source.Name)"
 Assert (!($source.Code -match 'Math\.RandomFloat\b')) "Math.RandomFloat in $($source.Name)"
 Assert (![regex]::IsMatch($source.Code, 'static\s+(const\s+)?ref\s+[^;=(]+=|static\s+const\s+array<')) "static collection initializer in $($source.Name) (0.1.13 lazy statics)"
 Assert (@([IO.File]::ReadAllBytes((Join-Path $scripts $source.Name)) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $($source.Name)"
}
# The WP1b methods keep every value return on its own line.
$newBodies = @(
 @($manager, 'void\s+BumpRoster\s*\('), @($manager, 'void\s+MarkProtectionDirty\s*\('), @($manager, 'void\s+Capture\s*\(\s*EBG_CacheZone'),
 @($manager, 'bool\s+Matches\s*\(\s*EBG_CacheZone'), @($manager, 'void\s+Sample\s*\(\s*SCR_AIGroup'), @($manager, 'static\s+bool\s+Nearby\s*\(\s*EBG_EnrollmentCandidate'),
 @($manager, 'protected\s+void\s+PruneSoldiersOnlySkipped\s*\('), @($manager, 'protected\s+void\s+NoteSoldiersOnly\s*\('), @($manager, 'protected\s+void\s+FlushSoldiersOnly\s*\('),
 @($manager, 'EnrollmentCandidates\s*\(\s*array<AIAgent>'), @($manager, 'protected\s+void\s+CheckCandidate\s*\('), @($manager, 'protected\s+void\s+UpdatePlayers\s*\('),
 @($manager, 'protected\s+void\s+CheckPossession\s*\('), @($manager, 'void\s+InvalidateProtection\s*\('), @($manager, 'protected\s+bool\s+ZoneInVolume\s*\('),
 @($manager, 'protected\s+void\s+CheckProtectionCache\s*\('), @($history, 'bool\s+EBG_IsMarkedPlayer\s*\('), @($persistence, 'protected\s+static\s+void\s+AddId\s*\('),
 @($persistence, 'static\s+bool\s+EverContains\s*\('), @($persistence, 'static\s+void\s+ForgetEverMirror\s*\('), @($persistence, 'class\s+EBG_IdMirror\b'),
 @($persistence, 'static\s+void\s+ForgetIdMirrors\s*\('))
foreach ($entry in $newBodies) {
 Assert (![regex]::IsMatch((Get-Body $entry[0] $entry[1]).Code, $oneLineReturn)) "one-line conditional value return in $($entry[0].Name): $($entry[1])"
}

'PASS: Unit Caching scheduler and history (WP1b): possession reruns only on change, roster/ledger bumps or a round-robin turn; player history, EverPlayerIds and released lineage use hash mirrors with debug cross-checks; pumps recompute protection only when dirty with settings-checked verdicts; one shared enrollment candidate list per pass; soldiers-only logging summarised per zone. Native engines were not launched.'
