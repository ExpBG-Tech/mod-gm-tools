#requires -Version 7.0
# Portable guard for the garrison core performance work (0.1.15 plan WP4, findings
# garrison-1, -2 (a), -3, -5, -6 (1)(2), -8, -9 and -10). Results stay the same; only
# the cost changes:
#  - Tick collects the guards and checks Unit Caching support (EBG_SimulationCache.
#    Unsupported) only after the awake-by-design return, with the same hold text: a
#    guard's support problem wins only when he comes before the first other reason;
#  - Reserves tests the garrisons' own squads before any guard; DescribeCache answers
#    a squad match before its guards and stops at the first matching guard;
#  - SyncExclusion decides portability and collects owned guards in one member pass;
#    the Pump runs it every 5 s and keeps the before-save hook installed in between;
#  - Near reads every player origin and every guard point once per call;
#  - the analysis budget is checked after every operation (Step(1)); ClearBody passes
#    the traversable doors without a copy when there is nothing to merge;
#  - the CDF-without-companion sleep refusal reads the addon list once per mission
#    (not through the CdfLoaded test seam);
#  - a Full-cached garrison skips the per-tick floor trace;
#  - the player list is refreshed only while there is a garrison to tick.
# A model check proves the deferred hold equals the eager one, and a self-test proves
# every check fires on a regressed copy. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/garrison/Scripts/Game/EXPG'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 if (!$match.Success) { return '' }
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 return ''
}
function Count([string]$Text, [string]$Literal) { ([regex]::Matches($Text, [regex]::Escape($Literal))).Count }
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = $Text.IndexOf($First, [StringComparison]::Ordinal)
 $b = $Text.IndexOf($Second, [StringComparison]::Ordinal)
 return ($a -ge 0 -and $b -ge 0 -and $a -lt $b)
}

function Get-Failures([string]$Manager, [string]$Plan, [string]$Exclusion) {
 $failures = [Collections.Generic.List[string]]::new()
 function Check([bool]$Condition, [string]$Message) { if (!$Condition) { $failures.Add($Message) } }

 # garrison-1: Unit Caching support is checked only once caching is due, same hold text.
 $tick = Get-Body $Manager 'protected\s+void\s+Tick\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
 $awake = 'if (record.CacheMode == 0 || Near(record, record.SleepDistance) || Now() - record.Created < 15)'
 Check ($tick.Contains($awake)) 'Tick keeps its awake-by-design return'
 Check ((Count $tick 'EBG_SimulationCache.Unsupported(') -eq 1 -and (Before $tick $awake 'EBG_SimulationCache.Unsupported(')) 'Tick calls EBG_SimulationCache.Unsupported once, only after the awake return'
 Check ((Before $tick 'm_SupportActors.Clear();' 'foreach (EXPG_GarrisonMember member : record.Members)') -and $tick.Contains('m_SupportIds.Clear();') -and $tick.Contains('int holdAt = -1;')) 'Tick clears the support candidates before its member loop'
 Check ($tick.Contains('m_SupportActors.Insert(actor);') -and $tick.Contains('m_SupportIds.Insert(cached.Id);') -and (Before $tick 'm_SupportActors.Insert(actor);' $awake)) 'the member loop collects each eligible guard in member order'
 $holds = Count $tick 'if (hold.IsEmpty()) { hold = '
 Check ($holds -ge 6 -and $holds -eq (Count $tick 'holdAt = m_SupportActors.Count(); }')) 'every first hold reason records how many guards came before it'
 Check ($tick.Contains('if (!record.AlertActive)') -and $tick.Contains('if (holdAt >= 0) { limit = holdAt; }') -and $tick.Contains('hold = string.Format("guard %1: %2", m_SupportIds[supportIndex], problem);') -and $tick.Contains('bool preserve = CacheModeInUse(record.Group) == 1;')) 'the deferred check names the first unsupported guard before the first other reason, skipped under the alarm'
 Check ((Before $tick 'EBG_SimulationCache.Unsupported(' 'if (record.AlertActive || unsafe)')) 'the deferred check runs before the hold decision'
 Check ($Manager -match 'protected\s+ref\s+array<SCR_ChimeraCharacter>\s+m_SupportActors\s*=\s*\{\};' -and $Manager -match 'protected\s+ref\s+array<int>\s+m_SupportIds\s*=\s*\{\};') 'support candidates are reusable instance members (no static initializer)'

 # garrison-2 (a): own squads first, no guard visited for them; first matching guard only.
 $reserves = Get-Body $Manager 'static\s+bool\s+Reserves\s*\(\s*SCR_AIGroup\s+group\s*\)'
 Check ((Before $reserves 'owner.Group == group) { return true; }' 'GetCharacterGroup()')) 'Reserves tests the garrisons'' own squads before any guard'
 $describe = Get-Body $Manager 'static\s+string\s+DescribeCache\s*\(\s*SCR_AIGroup\s+group\s*\)'
 Check ((Before $describe 'if (record.Group == group) { return s_Instance.CacheState(record); }' 'GetCharacterGroup()') -and $describe.Contains('{ held = true; break; }')) 'DescribeCache answers a squad match before its guards and stops at the first matching guard'

 # garrison-3: one member pass per record; the full sync every 5 s, the hook every second.
 $sync = Get-Body $Manager 'void\s+SyncExclusion\s*\(\s*\)'
 Check ((Count $sync 'OwnsActor(record, member)') -eq 1 -and !$sync.Contains('Portable(record)')) 'SyncExclusion visits each guard once (no separate Portable pass)'
 Check ($sync.Contains('MODE_CDF_BRIDGED') -and $sync.Contains('group.GetWaypoints(orders);') -and $sync.Contains('orders.Clear();') -and $sync.Contains('EXPG_SaveExclusion.EndSync();')) 'SyncExclusion keeps the squad, its waypoints and the CDF flag rule'
 Check ($sync.Contains('if (!record.Full && (!group || !EBG_PrefabFullCache.CanDeleteFullEntity(group))) { continue; }') -and $sync.Contains('if (!record.Full && !EBG_PrefabFullCache.CanDeleteFullEntity(member.CacheMember.Entity)) { portable = false; break; }')) 'portability stays: a Full transaction, or a deletable squad whose owned guards are all deletable'
 Check ((Before $sync 'EXPG_SaveExclusion.Keep(group, flag);' 'foreach (IEntity guard : guards) { EXPG_SaveExclusion.Keep(guard, flag); }')) 'owned guards are kept after the squad and its waypoints, in member order'
 $pump = Get-Body $Manager 'protected\s+void\s+Pump\s*\('
 Check ((Count $pump 'SyncExclusion();') -eq 1 -and $pump.Contains('if (Now() >= m_NextExclusionSync) { SyncExclusion(); m_NextExclusionSync = Now() + 5; }') -and $pump.Contains('else { EXPG_SaveExclusion.IsSaveHooked(); }')) 'the Pump runs SyncExclusion behind m_NextExclusionSync (5 s) and keeps the save hook in between'
 Check ((Before $pump 'if (!m_Importing)' 'SyncExclusion();') -and (Before $pump 'SyncExclusion();' 'ServiceNativeImport();') -and $pump.Contains('m_NextExclusion = Now() + 1;') -and $pump.Contains('EXPG_GarrisonNotice.Deliver();')) 'no sync while importing; native import and notices stay once a second'
 $hooked = Get-Body $Exclusion 'static\s+bool\s+IsSaveHooked\s*\(\s*\)'
 Check ($hooked.Contains('CheckWorld();') -and $hooked.Contains('HookSaves();')) 'IsSaveHooked installs the before-save hook'

 # garrison-5: player origins once per call, guard points once, Dead before the point.
 $near = Get-Body $Manager 'protected\s+bool\s+Near\s*\(\s*EXPG_GarrisonRecord\s+record,\s*float\s+distance\s*\)'
 Check ((Count $near 'player.GetOrigin()') -eq 1 -and (Count $near '.GetOrigin()') -eq 2) 'Near reads each player origin and each guard point once'
 Check ((Before $near 'm_NearOrigins.Insert(origin);' 'foreach (EXPG_GarrisonMember member : record.Members)') -and (Before $near 'if (member.CacheMember.Dead) { continue; }' 'member.CacheMember.Entity.GetOrigin()') -and $near.Contains('foreach (vector seen : m_NearOrigins)')) 'Near tests the cached origins against each living guard'
 Check ($near.Contains('float limitSq = distance * distance;') -and $near.Contains('vector.DistanceSq(origin, record.Plan.Origin) <= limitSq') -and $near.Contains('vector.DistanceSq(seen, point) <= limitSq')) 'Near keeps the same distance tests'

 # garrison-6 (1)(2): the budget after every operation; no door copy without a merge.
 Check ($pump -match 'while\s*\(analysing\s*&&\s*!analysing\.Done\s*&&\s*System\.GetTickCount\(\)\s*-\s*analysisStart\s*<\s*4\)\s*\{\s*analysing\.Step\(1\);\s*\}' -and !($pump -match 'Step\(\s*[02-9]')) 'Step(1) inside the 4 ms budget loop'
 $clear = Get-Body $Plan 'bool\s+ClearBody\s*\('
 Check ($clear.Contains('if (!exclude && !excludeEntities) { trace.ExcludeArray = m_TraversableDoors; }') -and !$clear.Contains('array<IEntity> exclusions = {};') -and (Before $clear 'array<IEntity> exclusions;' 'exclusions = new array<IEntity>();')) 'ClearBody copies the traversable doors only to merge; the merged list lives for the whole call'

 # garrison-8: the addon list once per mission, not through the CdfLoaded test seam.
 $sleep = Get-Body $Manager 'protected\s+void\s+TrySleep\s*\('
 Check (!$sleep.Contains('GetLoadedAddons') -and $sleep.Contains('if (CdfWithoutCompanion())') -and !$sleep.Contains('CdfLoaded()')) 'TrySleep reads the cached CDF-without-companion answer'
 $companion = Get-Body $Manager 'protected\s+bool\s+CdfWithoutCompanion\s*\(\s*\)'
 Check ($companion.Contains('if (m_CdfWithoutCompanion < 0)') -and $companion.Contains('addons.Contains("6A1876F37D65AB09") && !addons.Contains("07BC942D90324CD9")')) 'CdfWithoutCompanion caches the same addon test'
 Check ((Get-Body $Manager 'protected\s+bool\s+CdfLoaded\s*\(') -match '"6A1876F37D65AB09"') 'CdfLoaded keeps its own addon read (fixture seam)'

 # garrison-9: no floor trace for a Full-cached garrison; the cursor is not advanced.
 Check ($tick.Contains('if (!record.Members.IsEmpty() && record.Plan.Structure && !(record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED))') -and (Count $tick 'record.SafetyCursor++') -eq 1) 'the floor check skips Full-cached garrisons and resumes where it stopped'

 # garrison-10: players refreshed only while records exist, right before they are ticked.
 Check ((Count $pump 'Players();') -eq 1 -and $pump.Contains('if (!m_Records.IsEmpty() && Now() >= m_NextPlayers) { Players(); m_NextPlayers = Now() + 1; }') -and (Before $pump 'ServiceWaiters();' 'Players();') -and (Before $pump 'Players();' 'int serviceCount')) 'the player list is refreshed once a second while there is a garrison, before the records are ticked'
 return $failures
}

$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$planPath = Join-Path $scripts 'EXPG_BuildingPlan.c'
$exclusionPath = Join-Path $scripts 'EXPG_SaveExclusion.c'
$manager = Read-Text $managerPath
$plan = Read-Text $planPath
$exclusion = Read-Text $exclusionPath

$found = Get-Failures $manager $plan $exclusion
Assert ($found.Count -eq 0) ("garrison core performance: " + ($found -join '; '))

# Model: the deferred support check names the same hold and gives the same unsafe value
# as the eager per-guard check, for every order of reasons (deterministic sample).
function Get-Hold([object[]]$Guards, [string[]]$After, [bool]$Deferred) {
 $hold = ''; $unsafe = $false; $holdAt = -1
 $candidates = [Collections.Generic.List[object]]::new()
 foreach ($guard in $Guards) {
  # Skip: no controller or possessed (a reason), or deleted (unsafe, no reason); never a candidate.
  if ($guard.Skip) { if ($guard.Silent) { $unsafe = $true }; if ($guard.Reason) { $unsafe = $true; if (!$hold) { $hold = $guard.Reason; $holdAt = $candidates.Count } }; continue }
  if ($guard.Reason) { $unsafe = $true; if (!$hold) { $hold = $guard.Reason; $holdAt = $candidates.Count } }
  if ($guard.Silent) { $unsafe = $true }
  if ($Deferred) { $candidates.Add($guard) }
  elseif ($guard.Problem) { $unsafe = $true; if (!$hold) { $hold = "guard $($guard.Id): $($guard.Problem)" } }
 }
 foreach ($reason in $After) { if ($reason) { $unsafe = $true; if (!$hold) { $hold = $reason; $holdAt = $candidates.Count } } }
 if ($Deferred) {
  $limit = $candidates.Count; if ($holdAt -ge 0) { $limit = $holdAt }
  for ($i = 0; $i -lt $limit; $i++) { if ($candidates[$i].Problem) { $unsafe = $true; $hold = "guard $($candidates[$i].Id): $($candidates[$i].Problem)"; break } }
 }
 return "$unsafe|$hold"
}
$random = [Random]::new(4242)
for ($case = 0; $case -lt 4000; $case++) {
 $guards = @()
 $size = $random.Next(0, 8)
 for ($id = 1; $id -le $size; $id++) {
  $kind = $random.Next(0, 10)
  $guards += [pscustomobject]@{
   Id = $id
   Skip = $kind -le 1
   Reason = $(if ($kind -eq 0 -or $kind -eq 2 -or $kind -eq 5) { "reason-$id" } else { '' })
   Silent = $kind -eq 1 -or $kind -eq 6
   Problem = $(if ($kind -ge 4 -and $random.Next(0, 2) -eq 1) { "problem-$id" } else { '' })
  }
 }
 $after = @($(if ($random.Next(0, 4) -eq 0) { 'stranger' } else { '' }), $(if ($random.Next(0, 4) -eq 0) { 'unbound' } else { '' }))
 Assert ((Get-Hold $guards $after $false) -eq (Get-Hold $guards $after $true)) "deferred hold differs from the eager hold (case $case)"
}

# Self-test: each check fires on a regressed copy.
$regressions = @(
 @('manager', 'm_SupportActors.Insert(actor);', 'EBG_SimulationCache.Unsupported(actor, false); m_SupportActors.Insert(actor);'),
 @('manager', 'if (hold.IsEmpty()) { hold = UnboundGuard(record); holdAt = m_SupportActors.Count(); }', 'if (hold.IsEmpty()) { hold = UnboundGuard(record); }'),
 @('manager', 'if (holdAt >= 0) { limit = holdAt; }', ''),
 @('manager', 'if (!owner.Finished && owner.Group == group) { return true; }', ''),
 @('manager', '{ held = true; break; }', '{ held = true; }'),
 @('manager', 'if (record.Finished || !record.Ready) { continue; }', 'if (record.Finished || !record.Ready || !Portable(record)) { continue; }'),
 @('manager', 'if (Now() >= m_NextExclusionSync) { SyncExclusion(); m_NextExclusionSync = Now() + 5; }', 'SyncExclusion();'),
 @('manager', 'm_NearOrigins.Insert(origin);', 'm_NearOrigins.Insert(player.GetOrigin());'),
 @('manager', 'analysing.Step(1);', 'analysing.Step(8);'),
 @('manager', 'if (CdfWithoutCompanion())', 'if (CdfLoaded() && !CdfWithoutCompanion())'),
 @('manager', ' && !(record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED))', ')'),
 @('manager', 'if (!m_Records.IsEmpty() && Now() >= m_NextPlayers)', 'if (Now() >= m_NextPlayers)'),
 @('plan', 'if (!exclude && !excludeEntities) { trace.ExcludeArray = m_TraversableDoors; }', 'if (false) { }'),
 @('exclusion', 'HookSaves();' + "`n" + '  return s_SaveHooked;', 'return s_SaveHooked;')
)
foreach ($regression in $regressions) {
 $m = $manager; $p = $plan; $e = $exclusion
 switch ($regression[0]) {
  'manager' { Assert ($m.Contains($regression[1])) "self-test anchor missing: $($regression[1])"; $m = $m.Replace($regression[1], $regression[2]) }
  'plan' { Assert ($p.Contains($regression[1])) "self-test anchor missing: $($regression[1])"; $p = $p.Replace($regression[1], $regression[2]) }
  'exclusion' { Assert ($e.Contains($regression[1])) "self-test anchor missing: $($regression[1])"; $e = $e.Replace($regression[1], $regression[2]) }
 }
 Assert ((Get-Failures $m $p $e).Count -gt 0) "self-test: a regressed copy passed ($($regression[1]))"
}

# Enforce gotchas and encoding in the touched sources.
foreach ($path in @($managerPath, $planPath, $exclusionPath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'static\s+ref\s+[^;=]+=\s*(new|\{)') -and ![regex]::IsMatch($text, 'static\s+const\s+array<')) "static initializer in $path (create collections on first use)"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 Assert (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $path"
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
Assert (([IO.File]::ReadAllBytes($managerPath) | Where-Object { $_ -eq 13 }).Count -eq 0) 'EXPG_GarrisonManager.c keeps LF line endings'
Assert (([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $PSCommandPath"
'PASS: garrison core performance (support checked only when caching is due with the same hold, own squads before guards, one-pass exclusion every 5 s with the save hook kept, Near reads each origin once, budget per operation, no door copy without a merge, addon list once, no floor trace while Full cached, players only with garrisons); hold model and self-test verified.'
