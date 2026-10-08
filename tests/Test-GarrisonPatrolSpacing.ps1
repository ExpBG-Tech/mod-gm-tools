#requires -Version 7.0
# Portable guard for Garrison patrol spacing around live guards (native interior
# fixture, 0.1.14 and the 0.1.15 candidate: "standing soldiers 1.2 m apart and claims
# 1.5 m apart" failed 5 of 5 runs). Two modes were seen:
#  - a walker shoved a fixed guard, the guard re-anchored where he came to rest
#    (Anchor), and the walker's claim stayed within 1.5 m of the new post until his
#    leg ran out; an off-plan anchor had no reservation, so StopFree did not see it;
#  - a walker whose leg failed beside a standing guard dwelt there, 0.8 m from him.
# Checks the source invariants: a re-anchored post makes nearby patrol claims yield
# (skip the stop, walk to another free one, keep it only when none is free; never in
# an alarm or a cache settle; guards never moved), off-plan posts are seen by every
# claim, arrival and failed walks never dwell within 1.2 m of a standing soldier,
# event-driven work only (no per-frame or world-query cost, no new log), StartLeg is
# never re-entered, Enforce gotchas, and the interior fixture still measures it.
# A self-test proves every check fires on a regressed copy. No engine is launched.
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
# Local declarations of one body (typed locals, loop counters and foreach variables).
function Get-Locals([string]$Body) {
 @([regex]::Matches($Body, '(?<![\w.])(?:int|float|bool|vector|string|IEntity|EXPG_\w+|SCR_\w+)\s+(\w+)\s*(?:=|;|:(?!:))') | ForEach-Object { $_.Groups[1].Value })
}

function Get-Failures([string]$Plan, [string]$Manager, [string]$Patrol) {
 $failures = [Collections.Generic.List[string]]::new()
 function Check([bool]$Condition, [string]$Message) { if (!$Condition) { $failures.Add($Message) } }

 # Off-plan posts: one entry per live guard, dropped with his reservation, seen by every claim.
 Check ($Plan -match 'class\s+EXPG_HeldPost\s*\{\s*IEntity\s+Owner;\s*vector\s+Position;\s*\}' -and $Plan -match 'protected\s+ref\s+array<ref\s+EXPG_HeldPost>\s+m_Held\s*=\s*\{\};') 'the plan keeps live off-plan posts (owner entity, position)'
 $hold = Get-Body $Plan 'void\s+HoldOffPlan\s*\(\s*IEntity\s+owner,\s*vector\s+point\s*\)'
 Check ($hold.Contains('if (existing.Owner != owner) { continue; }') -and $hold.Contains('existing.Position = point;') -and $hold.Contains('m_Held.Insert(held);')) 'HoldOffPlan keeps one entry per guard'
 $release = Get-Body $Plan 'void\s+ReleaseReservation\s*\(\s*IEntity\s+owner\s*\)'
 Check ($release.Contains('if (!m_Held[h].Owner || m_Held[h].Owner == owner) { m_Held.RemoveOrdered(h); }')) 'ReleaseReservation drops the owner''s off-plan post (and those of deleted actors)'
 $stopFree = Get-Body $Plan 'bool\s+StopFree\s*\(\s*int\s+node'
 Check ($stopFree.Contains('foreach (EXPG_HeldPost held : m_Held)') -and $stopFree.Contains('if (!held.Owner || held.Owner == exceptOwner) continue;') -and $stopFree.Contains('Crowded(point, held.Position, POST_SPACING)')) 'StopFree (TryClaimStop, ClaimRoamStop, alarm posts) keeps POST_SPACING from off-plan posts'

 # Standing soldiers near a point: reservations and off-plan posts only, walkers excluded.
 $near = Get-Body $Plan 'bool\s+NearStanding\s*\(\s*vector\s+point,\s*IEntity\s+exceptOwner,\s*float\s+reach\s*\)'
 Check ($near.Contains('foreach (EXPG_BuildingReservation reservation : m_Reservations)') -and $near.Contains('foreach (EXPG_HeldPost held : m_Held)') -and (Count $near 'StandsNear(') -eq 2) 'NearStanding looks at every live guard of the building (reservations and off-plan posts)'
 $stands = Get-Body $Plan 'protected\s+static\s+bool\s+StandsNear\s*\('
 Check ($stands.Contains('owner == exceptOwner') -and $stands.Contains('Math.AbsFloat(at[1] - point[1]) >= 1.5') -and $stands.Contains('vector.DistanceXZ(at, point) >= reach') -and $stands.Contains('controller.IsDead()') -and $stands.Contains('return !patrol || !patrol.IsMoving();')) 'a standing soldier is a living guard or a patroller who is not walking, horizontally within reach on his floor'
 Check ((Before $stands 'vector.DistanceXZ(at, point) >= reach' 'SCR_ChimeraCharacter.Cast(owner)')) 'the distance test comes before the controller lookup'
 foreach ($body in @($near, $stands)) { Check ($body.Length -gt 0 -and $body -notmatch 'Trace|QueryEntities|GetAIAgents|Print') 'NearStanding uses no trace, world query or log' }

 # Manager: a re-anchor yields nearby claims; off-plan posts are registered.
 $anchor = Get-Body $Manager 'void\s+Anchor\s*\(\s*vector\s+at\s*\)'
 Check ($anchor.Contains('if (node >= 0 && Plan.ReserveNode(CacheMember.Entity, node)) { NodeIndex = node; }') -and $anchor.Contains('else { Plan.HoldOffPlan(CacheMember.Entity, at); }')) 'Anchor keeps a node within 0.5 m or registers the off-plan post'
 Check ((Before $anchor 'Plan.ReserveNode(CacheMember.Entity, node)' 'EXPG_GarrisonManager.YieldClaims(Plan, PostPoint());') -and (Count $anchor 'YieldClaims(') -eq 1) 'every re-anchor (TakeMoved, BindControls) yields the claims near the new post, after it is reserved'
 Check ($anchor -notmatch 'SetTransform|SetWorldTransform|Teleport') 'Anchor never moves the guard'
 $bind = Get-Body $Manager 'bool\s+BindControls\s*\(\s*\)'
 Check ((Before $bind 'if (member.NodeIndex < 0) { Plan.HoldOffPlan(actor, member.PostPosition); }' 'member.Post = new EXPG_PostControl();')) 'a guard bound on an off-plan post (around the building, an earlier anchor) registers it'
 $yieldClaims = Get-Body $Manager 'static\s+void\s+YieldClaims\s*\(\s*EXPG_BuildingPlan\s+plan,\s*vector\s+post\s*\)'
 Check ($yieldClaims.Contains('if (!plan || !HasActive()) { return; }') -and $yieldClaims.Contains('foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)') -and $yieldClaims.Contains('if (record.Finished || record.Plan != plan || record.Simulation) { continue; }')) 'YieldClaims visits the awake garrisons of the same building only'
 Check ($yieldClaims.Contains('EXPG_BuildingPlan.Crowded(post, plan.Nodes[claim].Position, EXPG_BuildingPlan.POST_SPACING)') -and (Before $yieldClaims 'EXPG_BuildingPlan.POST_SPACING' 'patrol.Yield();') -and $yieldClaims.Contains('if (patrol.ClaimedNode() >= 0) { walker.NodeIndex = patrol.ClaimedNode(); }')) 'only claims within POST_SPACING of the new post yield, and the patroller''s post follows his new claim at once'
 Check ($yieldClaims -notmatch 'walker\.Post\b|Teleport|SetTransform|Print|QueryEntities') 'YieldClaims never touches a guard, logs or queries the world'
 $tick = Get-Body $Manager 'protected\s+void\s+Tick\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
 Check ($tick.Contains('member.Post.TakeMoved(anchor)') -and $tick.Contains('member.Anchor(anchor);') -and $tick.Contains('PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) moved %4 m; holds where he stands", record.Group, cached.Id, actor, shift);')) 'TakeMoved still anchors (and so yields) with its log unchanged'

 # Patrol: Yield, never dwell beside a standing soldier, no re-entry.
 $yield = Get-Body $Patrol 'void\s+Yield\s*\(\s*\)'
 Check ($yield.Contains('if (!m_Speed || !m_Plan || m_Node < 0 || InAlert() || m_Settle || !IsOwnedActor() || m_Controller.IsUnconscious()) { return; }')) 'Yield never acts during an alarm, a cache settle or a knock-out'
 Check ((Before $yield 'SkipStop(kept, now);' 'StartLeg(now);') -and $yield.Contains('if (m_Node == kept) { m_DwellUntil = dwellEnd; }')) 'Yield skips the crowded stop, walks to another free one, keeps claim and dwell when none is free'
 $skip = Get-Body $Patrol 'protected\s+void\s+SkipStop\s*\(\s*int\s+node,\s*float\s+now\s*\)'
 Check ($skip.Contains('int at = m_Skipped.Find(node);') -and (Count $skip 'now + 120000') -eq 2) 'a skipped stop is no destination for two minutes (no oscillation), one entry per stop'
 $leave = Get-Body $Patrol 'protected\s+void\s+LeaveIfCrowded\s*\(\s*float\s+now\s*\)'
 Check ($leave.Contains('if (m_Settle || m_Node < 0 || !m_Actor || !m_Controller || m_Controller.IsUnconscious()) { return; }') -and $leave.Contains('if (!m_Plan.NearStanding(m_Actor.GetOrigin(), m_Actor, 1.2)) { return; }')) 'only a patroller beside a standing soldier (1.2 m) moves on; never while a cache sleep settles'
 Check ((Before $leave 'NearStanding(' 'SkipStop(m_Node, now);') -and (Before $leave 'SkipStop(m_Node, now);' 'StartLeg(now);')) 'the crowded stop is skipped before the next leg starts'
 $arrive = Get-Body $Patrol 'protected\s+void\s+Arrive\s*\(\s*float\s+now\s*\)'
 Check ($arrive.TrimEnd().EndsWith('LeaveIfCrowded(now);') -and (Before $arrive 'm_State = STATE_HOLD;' 'LeaveIfCrowded(now);') -and (Count $arrive 'LeaveIfCrowded(') -eq 1) 'an arrival never dwells beside a standing soldier; an alarm arrival holds as before'
 $fail = Get-Body $Patrol 'protected\s+void\s+Fail\s*\(\s*float\s+now\s*\)'
 Check ($fail.TrimEnd().EndsWith('LeaveIfCrowded(now);') -and (Before $fail 'm_State = STATE_HOLD;' 'LeaveIfCrowded(now);') -and (Before $fail 'BeginMove(m_Plan.Nodes[target].Position' 'LeaveIfCrowded(now);') -and (Count $fail 'LeaveIfCrowded(') -eq 1) 'a failed walk never dwells beside a standing soldier; alarms and the walk back are unchanged'
 $startLeg = Get-Body $Patrol 'protected\s+void\s+StartLeg\s*\(\s*float\s+now\s*\)'
 $beginMove = Get-Body $Patrol 'protected\s+void\s+BeginMove\s*\('
 $stopMoving = Get-Body $Patrol 'protected\s+void\s+StopMoving\s*\('
 foreach ($body in @($startLeg, $beginMove, $stopMoving)) { Check ($body.Length -gt 0 -and $body -notmatch '(?<![\w.])(LeaveIfCrowded|Yield|Arrive|Fail|StartLeg)\s*\(') 'StartLeg, BeginMove and StopMoving never call back into StartLeg (no re-entry; the native action''s own Fail is not ours)' }
 $allow = Get-Body $Patrol 'bool\s+AllowInput\s*\('
 $patrolTick = Get-Body $Patrol 'bool\s+Tick\s*\(\s*\)'
 $pathInside = Get-Body $Patrol 'protected\s+bool\s+PathInside\s*\('
 Check ($allow -notmatch 'NearStanding|LeaveIfCrowded' -and $patrolTick -notmatch 'NearStanding|LeaveIfCrowded' -and $pathInside -notmatch 'NearStanding' -and $pathInside.Contains('NearParked')) 'no per-frame crowd test: only arrivals and failed walks look for standing soldiers'
 Check ($Patrol -match 'protected\s+void\s+Fail\s*\(\s*float\s+now\s*\)' -and $Patrol -match 'protected\s+bool\s+PathInside\s*\(\s*\)') 'the interior fixture''s Fail and PathInside overrides still match'

 # Enforce gotchas: unique names, no local declared twice, no static initializer.
 foreach ($pair in @(@('Yield', $Patrol, 'void\s+Yield\s*\('), @('SkipStop', $Patrol, 'protected\s+void\s+SkipStop\s*\('), @('LeaveIfCrowded', $Patrol, 'protected\s+void\s+LeaveIfCrowded\s*\('), @('HoldOffPlan', $Plan, 'void\s+HoldOffPlan\s*\('), @('NearStanding', $Plan, 'bool\s+NearStanding\s*\('), @('StandsNear', $Plan, 'bool\s+StandsNear\s*\('), @('YieldClaims', $Manager, 'void\s+YieldClaims\s*\('))) {
  Check (([regex]::Matches($pair[1], $pair[2])).Count -eq 1) "$($pair[0]) is declared once"
 }
 foreach ($body in @($yield, $skip, $leave, $hold, $release, $stopFree, $near, $stands, $yieldClaims, $anchor)) {
  $locals = Get-Locals $body
  $twice = @($locals | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Check ($twice.Count -eq 0) "a local is declared twice in one method: $($twice -join ', ')"
 }
 foreach ($pair in @(@('EXPG_BuildingPlan.c', $Plan), @('EXPG_GarrisonManager.c', $Manager), @('EXPG_PatrolControl.c', $Patrol))) {
  $text = $pair[1]
  Check (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|external)\b')) "reserved Enforce name used as a variable in $($pair[0])"
  Check (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $($pair[0])"
  Check (![regex]::IsMatch($text, 'static\s+ref\s+[^;=]+=\s*(new|\{)') -and ![regex]::IsMatch($text, '(?m)^\s*(?:protected\s+|private\s+)?static\s+(?:ref\s+|const\s+)?(?:array|map|set)<')) "static collection initializer (0.1.13 lazy-static rule) in $($pair[0])"
 }
 return , $failures
}

$planPath = Join-Path $scripts 'EXPG_BuildingPlan.c'
$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$patrolPath = Join-Path $scripts 'EXPG_PatrolControl.c'
$plan = Read-Text $planPath
$manager = Read-Text $managerPath
$patrol = Read-Text $patrolPath
$failures = Get-Failures $plan $manager $patrol
Assert ($failures.Count -eq 0) ("garrison patrol spacing:`n - " + ($failures -join "`n - "))
foreach ($path in @($planPath, $managerPath, $patrolPath, $PSCommandPath)) {
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
Assert (([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $PSCommandPath"

# The native fixtures still measure what this guards (executed only by the orchestrator).
$interior = Read-Text (Join-Path $repo 'tests/EXPG_InteriorGameplay.c')
Assert ($interior.Contains('if (distance < 1.2 && Standing(members[a]) && Standing(members[b]))') -and $interior.Contains('EXPG_BuildingPlan.Crowded(first, second, EXPG_BuildingPlan.POST_SPACING - 0.01)') -and $interior.Contains('stacked=0 overlap=0')) 'the interior fixture still fails on soldiers standing within 1.2 m or claims within 1.5 m'
Assert ($interior -match 'override\s+protected\s+void\s+Fail\s*\(\s*float\s+now\s*\)' -and $interior -match 'override\s+protected\s+bool\s+PathInside\s*\(\s*\)') 'the interior fixture keeps its walk diagnostics'
$hold = Read-Text (Join-Path $repo 'tests/EXPG_HoldGameplay.c')
Assert ($hold -match 'override\s+void\s+Anchor\s*\(\s*vector\s+at\s*\)' -and $manager -match '(?m)^\s*void\s+Anchor\s*\(\s*vector\s+at\s*\)') 'the hold fixture''s Anchor override still matches'

# Self-test: each regression must be caught. A mutation replaces the Nth occurrence
# (default the first) of a text that must exist.
$mutations = @(
 @('plan', 'Crowded(point, held.Position, POST_SPACING)', 'false', 'StopFree ignoring off-plan posts'),
 @('plan', 'if (!m_Held[h].Owner || m_Held[h].Owner == owner) { m_Held.RemoveOrdered(h); }', '', 'an off-plan post outliving its guard'),
 @('plan', 'if (existing.Owner != owner) { continue; }', '', 'two off-plan posts for one guard'),
 @('plan', 'return !patrol || !patrol.IsMoving();', 'return true;', 'walking patrollers counted as standing'),
 @('plan', 'foreach (EXPG_HeldPost held : m_Held)', 'foreach (EXPG_HeldPost held : m_Unused)', 'NearStanding blind to off-plan guards', 2),
 @('manager', 'else { Plan.HoldOffPlan(CacheMember.Entity, at); }', '', 'an off-plan anchor invisible to claims'),
 @('manager', 'EXPG_GarrisonManager.YieldClaims(Plan, PostPoint());', '', 'a re-anchor that leaves crowded claims'),
 @('manager', 'if (member.NodeIndex < 0) { Plan.HoldOffPlan(actor, member.PostPosition); }', '', 'an off-plan post forgotten on rebind'),
 @('manager', 'patrol.Yield();', '', 'YieldClaims that never yields'),
 @('manager', ' || record.Simulation', '', 'Simulation-cached patrollers disturbed'),
 @('manager', 'if (patrol.ClaimedNode() >= 0) { walker.NodeIndex = patrol.ClaimedNode(); }', '', 'a stale post after a yield'),
 @('manager', 'EXPG_BuildingPlan.Crowded(post, plan.Nodes[claim].Position, EXPG_BuildingPlan.POST_SPACING)', 'true', 'every claim of the building yielding'),
 @('patrol', ' || InAlert() || m_Settle', '', 'a yield during an alarm or a cache settle'),
 @('patrol', 'SkipStop(kept, now);', '', 'a yielded stop claimed again at once (oscillation)'),
 @('patrol', 'if (m_Node == kept) { m_DwellUntil = dwellEnd; }', '', 'a yield with no free stop restarting the dwell'),
 @('patrol', 'LeaveIfCrowded(now);', '', 'an arrival dwelling beside a standing soldier'),
 @('patrol', 'LeaveIfCrowded(now);', '', 'a failed walk dwelling beside a standing soldier', 2),
 @('patrol', 'SkipStop(m_Node, now);', '', 'a crowded stop not skipped (oscillation)'),
 @('patrol', 'if (m_Settle || m_Node < 0', 'if (m_Node < 0', 'a new walk while a cache sleep settles'),
 @('patrol', 'm_Actor, 1.2)', 'm_Actor, 0.5)', 'a crowding radius below the fixture''s 1.2 m'),
 @('patrol', 'int hops = m_Plan.Hops(destination);', 'int hops = m_Plan.Hops(destination); LeaveIfCrowded(now);', 'StartLeg re-entered from itself'),
 @('patrol', 'float dwellEnd = m_DwellUntil;', 'float dwellEnd = m_DwellUntil; float now = 0;', 'a local declared twice'),
 @('patrol', 'else if (now > m_MoveDeadline', 'else if (m_Plan.NearStanding(m_Dest, m_Actor, 1.2) || now > m_MoveDeadline', 'a per-frame crowd test')
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
"PASS: garrison patrol spacing (a re-anchored guard makes patrol claims within 1.5 m yield to another free stop, never in an alarm or a cache settle and never moving a guard; off-plan posts are seen by every claim; arrivals and failed walks never dwell within 1.2 m of a standing soldier; event-driven only, no re-entry, no new log); fixtures still measure it; $($mutations.Count) self-test regressions caught."
