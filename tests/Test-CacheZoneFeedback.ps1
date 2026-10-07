#requires -Version 7.0
# Portable guard for Unit Caching's zone explanations (why a cache zone enrolls or caches
# nothing). Enrollment names the first refusal of every group inside the affected radius
# and counts soldiers who left their squad; the scheduler folds that, player characters
# inside the sleep radius, a Full refusal and old standalone EXPBG mods into the zone
# status, and tells the Game Master who saved the zone once. The text is diagnostics
# only: no cache, wake or ownership decision may read it. Also checks the native fixture
# tests/EBG_LocalCacheGameplay.c is wired to the runner. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG'
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

$manager = Read-Text (Join-Path $scripts 'EBG_CacheManager.c')
$refresh = Get-Body $manager 'void\s+Refresh\s*\(\s*EBG_CacheZone\s+zone'
# Every enrollment refusal is a literal reason that reaches the tally before the group is skipped.
$reasons = @([regex]::Matches($refresh, '\bskip\s*=\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
Assert ($reasons.Count -ge 8) "Refresh must name each enrollment refusal (found $($reasons.Count))"
Assert ($refresh -match 'skip\s*=\s*MemberSkip\(') 'Refresh must name member-level refusals through MemberSkip'
Assert ($refresh -match '(?s)if\s*\(\s*!skip\.IsEmpty\(\)\s*\)\s*\{\s*tally\.Add\(skip\);\s*continue;\s*\}') 'a named refusal must be tallied and skip the group'
Assert ($refresh -match 'if\s*\(\s*!EBG_EnrollmentTally\.Nearby\(') 'groups without a living soldier inside the radius stay unreported'
$tail = $refresh.Substring($refresh.LastIndexOf('Records.Insert(record);'))
foreach ($needle in 'tally.CountLeftSquad(zoneOrigin, affectedSq);', 'zone.EnrollmentNote = tally.Note(zone.Affected, managed);', 'zone.EnrollmentPasses++;') {
 Assert $tail.Contains($needle) "Refresh must finish its pass with: $needle"
}
$memberSkip = Get-Body $manager 'string\s+MemberSkip\s*\('
foreach ($needle in 'EBG_WasPlayerControlled()', 'EBG_HasLeftSquad()', 'EBG_MissionPersistence.MayEnroll', 'HasUnresolvedRelease', 'FindMember(character)', 'Regroup.ReservesMember(character)') {
 Assert $memberSkip.Contains($needle) "MemberSkip lost an enrollment check: $needle"
}

# Soldiers who left their squad are counted from the server list, not the AI agent list
# (AI Surrender switches their AI off).
$history = Read-Text (Join-Path $scripts 'EBG_PlayerHistory.c')
Assert ((Get-Body $history 'void\s+EBG_MarkLeftSquad\s*\(') -match 'EBG_EnrollmentTally\.TrackLeftSquad\(this\)') 'EBG_MarkLeftSquad must register the soldier for the zone status'
$count = Get-Body $manager 'void\s+CountLeftSquad\s*\(\s*vector'
Assert ($count -match 's_LeftSquad\.Remove\(i\)' -and $count -match 'GetCharacterGroup\(\)') 'CountLeftSquad must prune dead entries and skip regrouped soldiers'

# Scheduler: the status names the reasons; the notice goes to the requesting GM once.
$tick = Get-Body $manager 'void\s+Tick\s*\(\s*\)'
Assert ($tick -match 'string\s+fullUnavailable\s*=\s*ZoneNotes\(zone\);') 'Tick must fold ZoneNotes into every zone status'
Assert ($tick -match 'int\s+noticePlayer\s*=\s*zone\.EBG_TakeNotice\(\);\s*if\s*\(\s*noticePlayer\s*>\s*0\s*\)\s*EBG_ZoneFeedback\.Send\(noticePlayer,\s*ZoneNoticeText\(zone\)\);') 'Tick must send the pending zone notice'
Assert ($tick -match 'zone\.PlayerAwakeCount\+\+;') 'Tick must count groups a player character keeps awake'
# The players x members distance scan runs only for its readers (pending notice, debug).
Assert ($tick -match '(?s)if\s*\(\s*zone\.DebugMessages\s*>\s*0\s*\|\|\s*EBG_CacheDebug\.Level\s*>\s*0\s*\|\|\s*zone\.EBG_NoticePending\(\)\s*\)\s*\{\s*float\s+playerDistance\s*=\s*NearestPlayerDistance\(record\);') 'NearestPlayerDistance must run only while a notice is pending or debug is on'
$notes = Get-Body $manager 'protected\s+string\s+ZoneNotes\s*\('
foreach ($needle in 'zone.EnrollmentNote', 'PlayerAwakeNote(zone, false)', 'FullUnavailable(zone)', 'StandaloneNote()') {
 Assert $notes.Contains($needle) "ZoneNotes must report: $needle"
}
# The replicated status carries no player distance: a moving player would re-replicate
# every zone's status every few seconds. Distance goes to the notice and debug panel only.
Assert (!$notes.Contains('PlayerAwakeNote(zone)')) 'the replicated zone status must not carry the player distance'
Assert ((Get-Body $manager 'static\s+string\s+PlayerAwakeNote\s*\(') -match 'if\s*\(\s*withDistance\s*&&') 'PlayerAwakeNote must add the distance only on request'
$notice = Get-Body $manager 'string\s+ZoneNoticeText\s*\('
foreach ($needle in 'zone.EnrollmentNote', 'PlayerAwakeNote(zone)', 'FullUnavailable(zone)', 'StandaloneNote()', 'zone.SleepDelay') {
 Assert $notice.Contains($needle) "ZoneNoticeText must report: $needle"
}
Assert ((Get-Body $manager 'static\s+string\s+PlayerAwakeNote\s*\(') -match 'sleep radius') 'the awake note must name the sleep radius'
Assert ((Get-Body $manager 'static\s+string\s+StandaloneNote\s*\(') -match 'EBG_StandaloneConflict\.Loaded\(') 'the standalone note must use the one loaded-addon read of EBG_StandaloneConflict'

# Full refusal text uses exactly the runtime rules the Full gate applies.
$gate = Read-Text (Join-Path $scripts 'EBG_FullSaveGate.c')
$available = Get-Body $gate 'static\s+bool\s+Available\s*\('
Assert ($available -match 'SupportedRuntime\(reason\)\s*&&\s*CanCaptureForCDF\(reason\)') 'Available must apply the same SupportedRuntime and CDF rules as TryAcquire'
Assert ((Get-Body $gate 'static\s+bool\s+TryAcquire\s*\(') -match '!SupportedRuntime\(reason\)\s*\|\|\s*!CanCaptureForCDF\(reason\)') 'TryAcquire rules changed; keep Available in step'
Assert ((Get-Body $gate 'protected\s+static\s+bool\s+SupportedRuntime\s*\(') -match 'GameMasterSystems systems config, which this session does not run') 'the missing systems config refusal must say this session lacks it'

# Requests: a saved zone attribute and a global zone switch ask for one notice.
Assert ((Get-Body (Read-Text (Join-Path $scripts 'EBG_CacheAttribute.c')) 'override\s+void\s+WriteVariable\s*\(') -match 'zone\.EBG_RequestNotice\(playerID\);') 'interactive attribute saves must request a notice'
Assert ((Get-Body (Read-Text (Join-Path $scripts 'EBG_OptimizerController.c')) 'static\s+void\s+Execute\s*\(') -match 'zone\.EBG_RequestNotice\(ActingPlayer\);') 'global zone switches must request a notice'
$feedback = Read-Text (Join-Path $scripts 'EBG_ZoneFeedback.c')
Assert ((Get-Body $feedback 'static\s+void\s+Send\s*\(') -match '!Replication\.IsServer\(\)') 'EBG_ZoneFeedback.Send must stay server-only'
Assert ($feedback -match '\[RplRpc\(RplChannel\.Reliable,\s*RplRcver\.Owner\)\]') 'a remote Game Master receives the notice through an owner RPC'
Assert ((Get-Body $feedback 'void\s+EBG_ZoneNotify\s*\(') -match 'if\s*\(\s*IsOwner\(\)\s*\)') 'the host and single player show their own notice without an RPC'

# Diagnostics only: the explanation fields are read by status, notice, debug panel and map label only.
$diagnostic = 'EnrollmentNote|PlayerAwakeCount|PlayerAwakeDistance|EBG_TakeNotice|EBG_NoticePending|ZoneNoticeText|StandaloneNote'
$allowed = @('EBG_CacheManager.c', 'EBG_CacheZone.c', 'EBG_CacheDebug.c', 'EBG_ZoneFeedback.c')
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo 'addon') -Recurse -Filter '*.c' -File) {
 if ([regex]::IsMatch((Read-Text $file.FullName), "\b($diagnostic)\b")) {
  Assert ($allowed -contains $file.Name -and $file.FullName.Replace('\', '/') -match '/unit-caching/') "zone explanation read outside the status path: $($file.FullName)"
 }
}
foreach ($decision in 'IsProtected', 'InVolume', 'UpdateRecord', 'IsReserved', 'KeepAwakeReason', 'CanSleep') {
 $signature = "\b(bool|void|string)\s+$decision\s*\("
 if ([regex]::IsMatch($manager, $signature)) {
  Assert (![regex]::IsMatch((Get-Body $manager $signature), "\b($diagnostic)\b")) "cache decision $decision reads zone explanation text"
 }
}

# Enforce gotchas in the touched sources and the fixture.
$fixturePath = Join-Path $repo 'tests/EBG_LocalCacheGameplay.c'
foreach ($path in @((Join-Path $scripts 'EBG_CacheManager.c'), (Join-Path $scripts 'EBG_CacheZone.c'), (Join-Path $scripts 'EBG_ZoneFeedback.c'), (Join-Path $scripts 'EBG_PlayerHistory.c'), (Join-Path $scripts 'EBG_FullSaveGate.c'), $fixturePath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'fixture must keep the runner driver class names'
Assert ($fixture -match [regex]::Escape("-FixturePath tests/EBG_LocalCacheGameplay.c -ExpectResult '\[EBG LOCAL CACHE RESULT\] checks=[1-9]\d* failures=0 reason=complete'")) 'fixture header must carry its runner command'
Assert ($fixture -match 'PrintFormat\("\[EBG LOCAL CACHE RESULT\] checks=%1 failures=%2 reason=%3"') 'fixture must print the RESULT line the runner expects'
Assert ($fixture -match 'modded\s+class\s+EBG_CacheManager' -and $fixture -match 'override\s+protected\s+void\s+UpdatePlayers\(\)') 'fixture must inject the host character through UpdatePlayers'
Assert ($fixture -match 'Resource\s+resource\s*=\s*Resource\.Load\(prefab\);') 'fixture must keep Resource.Load results in a local'

# Squads a garrison caches itself are named with their state, never counted as not
# enrolled; Garrison runs Full in Simulation while CDF is loaded without the EXPBG CDF
# Compat garrison bridge (CacheModeInUse, legacy mode).
Assert ($refresh -match '(?s)else\s+if\s*\(\s*IsReserved\(group\)\s*\)\s*\{.*?if\s*\(\s*DescribeExternalCache\(group,\s*externalModule,\s*externalState\)\s*\)\s*\{\s*tally\.AddExternal\(externalModule,\s*externalState\);\s*continue;\s*\}.*?KeepAwakeReason\(group\)') 'Refresh must report a squad another module caches itself before the generic held-by text'
Assert ((Get-Body $manager 'string\s+Note\s*\(\s*int\s+affected') -match 'ExternalNote\(\)') 'the enrollment note must include squads cached by another module'
Assert ((Get-Body $manager 'bool\s+DescribeExternalCache\s*\(') -match 'return\s+false;') 'DescribeExternalCache defaults to no claim'
$garrison = Read-Text (Join-Path $repo 'addon/garrison/Scripts/Game/EXPG/EXPG_GarrisonManager.c')
Assert ($garrison -match 'override\s+bool\s+DescribeExternalCache\s*\(\s*SCR_AIGroup\s+group,\s*out\s+string\s+moduleName,\s*out\s+string\s+cacheState\s*\)') 'Garrison must describe the squads it caches to the zone status'
$trySleep = Get-Body $garrison 'protected\s+void\s+TrySleep\s*\('
Assert ($trySleep -match 'if\s*\(\s*!fallback\s*&&\s*CacheModeInUse\(record\.Group\)\s*==\s*2\s*\)\s*\{\s*TryFullSleep\(record\);' -and $trySleep.Contains('"Simulation cached (CDF loaded)"') -and $trySleep.Contains('"Simulation cached (Full refused: "')) 'Garrison Full must fall back to Simulation with a named status (CDF legacy, or a refused Full capture)'
Assert ((Get-Body $garrison 'int\s+CacheModeInUse\s*\(') -match 'mode\s*==\s*2\s*&&\s*CdfLoaded\(\)\s*&&\s*PersistenceMode\(\)\s*==\s*EXPG_GarrisonPersistence\.MODE_CDF_LEGACY') 'CacheModeInUse must map Full to Simulation only while CDF is loaded without the bridge'
Assert ((Get-Body $garrison 'protected\s+bool\s+CdfLoaded\s*\(') -match '"6A1876F37D65AB09"') 'CdfLoaded must read the CDF Game Master Save identity'
$cdfFixture = Read-Text (Join-Path $repo 'tests/EXPG_CdfFallbackGameplay.c')
Assert ($cdfFixture -match [regex]::Escape("-FixturePath tests/EXPG_CdfFallbackGameplay.c -ExpectResult '\[EXPG CDF FALLBACK RESULT\] checks=[1-9]\d* failures=0 guards=4 fullCycle=1 simulationCycle=1 cdfLines=1 zoneNote=1 reason=completed'")) 'CDF fallback fixture header must carry its runner command'
Assert ($cdfFixture -match 'PrintFormat\("\[EXPG CDF FALLBACK RESULT\] checks=%1 failures=%2 guards=%3 fullCycle=%4 simulationCycle=%5 cdfLines=%6 zoneNote=%7 reason=%8"') 'CDF fallback fixture must print the RESULT line its regex expects'
Assert ($cdfFixture -match 'override\s+protected\s+bool\s+CdfLoaded\(\)' -and $cdfFixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'CDF fallback fixture must use the CdfLoaded seam and the runner driver class'
Assert (![regex]::IsMatch($cdfFixture, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|external)\b')) 'reserved Enforce name in the CDF fallback fixture'
'PASS: Unit Caching zone explanations (enrollment refusals, left-squad soldiers, player characters inside the sleep radius, Full refusal, standalone mods, GM notice, squads a garrison caches itself) stay diagnostics only; Garrison Full falls back to Simulation under CDF without its bridge; local-cache and CDF fallback fixtures wired.'
