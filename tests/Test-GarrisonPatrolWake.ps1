#requires -Version 7.0
# Portable guard for Garrison patrollers waking from a cache sleep (native interior
# fixture since 0.1.14: "Simulation: every surviving patroller woke as a patroller
# near his stop and still patrols: 2 of 3"). Seen in runs 2026-10-08 00:51,
# 2026-10-08 04:04 and 2026-10-09 00:49: one patroller's leg failed beside a
# standing guard (no native path for 15 s), the walk back failed too, he dwelt
# 3.3-4.5 m from the stop he still claimed, the settle gate accepted the dwell, and
# the Simulation wake only restarted his dwell, so he woke metres from his claim.
# Checks the source invariants: a patroller stopped short claims the nearest free
# stop within 1.5 m where he stands (failed walk, forced settle, Simulation wake),
# bounded and event-driven (no per-frame call, no world query beyond the claim's
# body test, no log); a Simulation wake with no free stop near him walks him back
# to his claim; the wake resumes patrols only after the restore gave the actors
# back; Enforce gotchas; and the interior fixture still measures it. A self-test
# proves every check fires on a regressed copy. No engine is launched.
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
function Get-Locals([string]$Body) {
 @([regex]::Matches($Body, '(?<![\w.])(?:int|float|bool|vector|string|IEntity|EXPG_\w+|SCR_\w+)\s+(\w+)\s*(?:=|;|:(?!:))') | ForEach-Object { $_.Groups[1].Value })
}

function Get-Failures([string]$Plan, [string]$Manager, [string]$Patrol) {
 $failures = [Collections.Generic.List[string]]::new()
 function Check([bool]$Condition, [string]$Message) { if (!$Condition) { $failures.Add($Message) } }

 # Plan: the nearest free stop within reach, claimed for the owner, bounded.
 $near = Get-Body $Plan 'int\s+ClaimNearStop\s*\(\s*IEntity\s+owner,\s*vector\s+point,\s*float\s+reach\s*\)'
 Check ($near.Length -gt 0 -and ([regex]::Matches($Plan, 'int\s+ClaimNearStop\s*\(')).Count -eq 1) 'the plan declares ClaimNearStop(owner, point, reach) once'
 Check ($near.Contains('int span = Math.Ceil(reach / GRID);') -and $near.Contains('for (int dz = -span; dz <= span; dz++)') -and $near.Contains('for (int dx = -span; dx <= span; dx++)')) 'ClaimNearStop scans only the grid columns that cover its reach'
 Check ($near.Contains('!node.Reachable || !node.IndoorWalk || node.Stair || node.DoorBlock || Math.AbsFloat(node.Position[1] - point[1]) > 0.8')) 'ClaimNearStop takes standing indoor walking nodes on his floor only (no stair, no door zone)'
 Check ($near.Contains('if (distance > limit || !StopFree(index, owner)) continue;') -and (Before $near 'distance > limit' 'StopFree(index, owner)')) 'ClaimNearStop keeps POST_SPACING from every other claim (distance test first)'
 Check ($near.Contains('KeepBest(picks, scores, index, distance, 4);') -and $near.Contains('if (TryClaimStop(owner, pick))')) 'nearest first, at most four claim attempts through TryClaimStop (door zone, spacing and body tests)'
 Check ($near -notmatch 'Trace|QueryEntities|GetAIAgents|Print') 'ClaimNearStop uses no trace of its own, world query or log'

 # Patrol: near his claim, or the nearest free stop where he stands.
 $nearClaim = Get-Body $Patrol 'protected\s+bool\s+NearClaim\s*\(\s*\)'
 Check ($nearClaim.Contains('vector.DistanceXZ(origin, stop) <= 1.5 && Math.AbsFloat(origin[1] - stop[1]) <= 1.0')) 'near his claim means Start''s acceptance (1.5 m across, 1 m up or down)'
 $where = Get-Body $Patrol 'protected\s+bool\s+ClaimWhereHeStands\s*\(\s*\)'
 Check ($where.Contains('if (!m_Reserved || !m_Actor || !m_Plan || m_Node < 0)') -and (Before $where 'if (NearClaim())' 'm_Plan.ClaimNearStop(m_Actor, m_Actor.GetOrigin(), 1.5)')) 'only a bound patroller off his claim looks for a stop within 1.5 m'
 Check ($where.Contains('m_Node = nearStop;') -and $where.Contains('m_Look = m_Plan.Nodes[nearStop].WatchLook;')) 'the stop near him becomes his single claim and look'
 Check ($where -notmatch 'BeginMove|StartLeg|SetOrigin|SetTransform|SetWorldTransform|Teleport|Print') 'claiming where he stands never moves him, walks or logs'

 # Failed walk: no dwell metres from the claim.
 $fail = Get-Body $Patrol 'protected\s+void\s+Fail\s*\(\s*float\s+now\s*\)'
 Check ((Before $fail 'm_DwellUntil = now + Math.RandomFloat(5000, 10000);' 'ClaimWhereHeStands();') -and (Before $fail 'ClaimWhereHeStands();' 'LeaveIfCrowded(now);') -and (Count $fail 'ClaimWhereHeStands(') -eq 1) 'a failed walk whose walk back is spent claims the stop where he stands before the crowd test'
 Check ((Before $fail 'BeginMove(m_Plan.Nodes[target].Position' 'ClaimWhereHeStands();') -and (Before $fail 'm_State = STATE_HOLD;' 'ClaimWhereHeStands();')) 'alarms and the one walk back per leg are unchanged'

 # Forced settle: the same claim near him.
 $force = Get-Body $Patrol 'void\s+ForceSettle\s*\(\s*\)'
 Check ((Before $force 'if (m_Move) { Block(); }' 'ClaimWhereHeStands();') -and (Before $force 'ClaimWhereHeStands();' 'm_State = STATE_DWELL;')) 'a forced settle stops him, then claims the stop where he stands'

 # Simulation wake: dwell again, claim where he stands, else walk back to the claim.
 $restart = Get-Body $Patrol 'void\s+RestartDwell\s*\(\s*\)'
 Check ($restart.Contains('if (m_State != STATE_DWELL || m_Move)') -and $restart.Contains('Math.RandomFloat(10000, 30000)')) 'a woken dweller starts a fresh 10-30 s dwell'
 Check ($restart.Contains('if (!m_Speed || !IsOwnedActor() || ClaimWhereHeStands() || m_Node < 0)') -and (Before $restart 'Math.RandomFloat(10000, 30000)' 'ClaimWhereHeStands()')) 'a woken patroller off his claim, still owned, claims the stop where he stands'
 Check ((Before $restart 'ClaimWhereHeStands()' 'BeginMove(m_Plan.Nodes[m_Node].Position, EMovementType.WALK, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL, 15000);') -and (Before $restart 'm_Backtrack = true;' 'BeginMove(') -and (Before $restart 'BeginMove(' 'm_State = STATE_LEG;')) 'with no free stop near him he walks back to his claim at once (one walk back, walk speed, 15 s)'
 $wake = Get-Body $Manager 'protected\s+bool\s+Wake\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
 Check ((Before $wake 'EBG_SimulationCache.Restore(record.Simulation, reason)' 'resumed.Patrol.RestartDwell();') -and (Before $wake 'resumed.Patrol.RestartDwell();' 'ReleaseSettle(record);')) 'patrols resume only after the Simulation restore gave the actors back, before the settle is released'
 Check ((Count $Manager 'RestartDwell(') -eq 1 -and (Count $Patrol 'ClaimWhereHeStands(') -eq 4) 'one Simulation wake call; the claim near him runs on a failed walk, a forced settle and a wake only'

 # Event-driven: never per frame.
 $tick = Get-Body $Patrol 'bool\s+Tick\s*\(\s*\)'
 $allow = Get-Body $Patrol 'bool\s+AllowInput\s*\('
 $inspect = Get-Body $Patrol 'bool\s+InspectCurrentPath\s*\('
 $prepare = Get-Body $Patrol 'protected\s+override\s+void\s+OnPrepareControls\s*\('
 foreach ($body in @($tick, $allow, $inspect, $prepare)) { Check ($body.Length -gt 0 -and $body -notmatch 'ClaimWhereHeStands|ClaimNearStop|NearClaim|RestartDwell') 'no per-frame claim search (Tick, AllowInput, InspectCurrentPath, OnPrepareControls)' }

 # Enforce gotchas in the new bodies.
 foreach ($body in @($near, $nearClaim, $where, $restart)) {
  $twice = @(Get-Locals $body | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Check ($twice.Count -eq 0) "a local is declared twice in one method: $($twice -join ', ')"
  Check ($body -notmatch '(?m)\breturn\b[^\r\n]*;[^\r\n]*\breturn\b' -and $body -notmatch '(?m)\{[^\r\n{}]*\breturn\b[^\r\n]*\}') 'each return stands on its own line in the new bodies'
  Check ($body -notmatch '\b(?:int|float|bool|string|vector|auto|IEntity)\s+(?:owned|Sleep|vanilla)\b') 'no reserved or vanilla-named local in the new bodies'
 }
 foreach ($pair in @(@('EXPG_BuildingPlan.c', $Plan), @('EXPG_PatrolControl.c', $Patrol))) {
  Check (![regex]::IsMatch($pair[1], '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|vanilla)\b')) "reserved Enforce name used as a variable in $($pair[0])"
 }
 Check ((Count $Patrol 'Math.RandomFloat(') -le 6) 'no new Math.RandomFloat in the patrol control'
 return , $failures
}

$planPath = Join-Path $scripts 'EXPG_BuildingPlan.c'
$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$patrolPath = Join-Path $scripts 'EXPG_PatrolControl.c'
$plan = Read-Text $planPath
$manager = Read-Text $managerPath
$patrol = Read-Text $patrolPath
$failures = Get-Failures $plan $manager $patrol
Assert ($failures.Count -eq 0) ("garrison patrol wake:`n - " + ($failures -join "`n - "))
foreach ($path in @($planPath, $managerPath, $patrolPath, $PSCommandPath)) {
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
Assert (([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $PSCommandPath"

# The native interior fixture still judges every survivor at his own wake (executed only by the orchestrator).
$interior = Read-Text (Join-Path $repo 'tests/EXPG_InteriorGameplay.c')
Assert ($interior.Contains('sampled.SampledBack = wokeDistance <= 1.5;') -and $interior.Contains('every surviving patroller woke as a patroller near his stop and still patrols: %2 of %3') -and $interior.Contains('SimRovers = CheckRestored("Simulation");')) 'the interior fixture still fails a patroller who wakes more than 1.5 m from his claim'
Assert ($interior.Contains('simRovers=[2-9]\d* replenished=0 reason=completed')) 'the interior fixture still expects every Simulation survivor back'

# Self-test: each regression must be caught. A mutation replaces the Nth occurrence
# (default the first) of a text that must exist.
$mutations = @(
 @('plan', 'if (distance > limit || !StopFree(index, owner)) continue;', 'if (distance > limit) continue;', 'a claim near him beside another claim'),
 @('plan', ' || node.DoorBlock || Math.AbsFloat(node.Position[1] - point[1]) > 0.8', ' || Math.AbsFloat(node.Position[1] - point[1]) > 0.8', 'a claim near him in a door zone'),
 @('plan', 'KeepBest(picks, scores, index, distance, 4);', 'KeepBest(picks, scores, index, distance, 400);', 'unbounded claim attempts'),
 @('plan', 'int span = Math.Ceil(reach / GRID);', 'int span = 1;', 'a claim search blind beyond one grid column'),
 @('patrol', 'if (NearClaim())', 'if (true)', 'a patroller off his claim never re-anchored'),
 @('patrol', 'vector.DistanceXZ(origin, stop) <= 1.5', 'vector.DistanceXZ(origin, stop) <= 5.0', 'near his claim at 5 m'),
 @('patrol', 'm_Node = nearStop;', '', 'a claim made but not taken'),
 @('patrol', 'ClaimWhereHeStands();', '', 'a failed walk dwelling metres from his claim'),
 @('patrol', 'ClaimWhereHeStands();', '', 'a forced settle metres from his claim', 2),
 @('patrol', ' || ClaimWhereHeStands() || m_Node < 0', ' || m_Node < 0', 'a Simulation wake metres from his claim'),
 @('patrol', 'm_Backtrack = true;', '', 'a walk back that may walk back again', 2),
 @('patrol', 'if (AtPoint(m_Dest)) { Arrive(now); }', 'if (AtPoint(m_Dest)) { NearClaim(); Arrive(now); }', 'a per-frame claim search'),
 @('manager', 'if (resumed.Patrol) { resumed.Patrol.RestartDwell(); }', '', 'a Simulation wake that never resumes the patrol')
)
foreach ($mutation in $mutations) {
 $target = switch ($mutation[0]) { 'plan' { $plan } 'manager' { $manager } default { $patrol } }
 $occurrence = if ($mutation.Count -gt 4) { $mutation[4] } else { 1 }
 $index = -1
 for ($n = 0; $n -lt $occurrence; $n++) { $index = $target.IndexOf($mutation[1], $index + 1, [StringComparison]::Ordinal); if ($index -lt 0) { break } }
 Assert ($index -ge 0) "self-test text not found ($($mutation[3])): $($mutation[1]) #$occurrence"
 $mutated = $target.Substring(0, $index) + $mutation[2] + $target.Substring($index + $mutation[1].Length)
 $caught = switch ($mutation[0]) { 'plan' { Get-Failures $mutated $manager $patrol } 'manager' { Get-Failures $plan $mutated $patrol } default { Get-Failures $plan $manager $mutated } }
 Assert ($caught.Count -gt 0) "self-test: the guard missed a regression ($($mutation[3]))"
}
"PASS: garrison patrol wake (a patroller stopped short claims the nearest free stop within 1.5 m on a failed walk, a forced settle and a Simulation wake, else walks back to his claim on waking; bounded, event-driven, no log; patrols resume after the restore); interior fixture still measures it; $($mutations.Count) self-test regressions caught."
