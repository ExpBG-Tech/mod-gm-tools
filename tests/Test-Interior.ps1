#requires -Version 7.0
# Portable guard for Garrison interior behaviour (live test 2026-10-07: a guard
# stood right behind a house's front door so it could not be opened; overflow
# soldiers should patrol inside, take free windows or watch points in a
# firefight and not bunch up). Checks the source invariants: one door keep-out
# test applied wherever a guard can stand or stop, door posts set back 2-4 m,
# interior patrol stops with claims, the event-driven bounded alarm, the settle
# gate before caching, the 4 ms analysis budget, Enforce gotchas, and the
# wiring of tests/EXPG_InteriorGameplay.c. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/garrison/Scripts/Game/EXPG'
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
function Assert-Before([string]$Text, [string]$First, [string]$Second, [string]$Message) {
 $a = $Text.IndexOf($First); $b = $Text.IndexOf($Second)
 Assert ($a -ge 0 -and $b -ge 0 -and $a -lt $b) $Message
}

$planPath = Join-Path $scripts 'EXPG_BuildingPlan.c'
$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$patrolPath = Join-Path $scripts 'EXPG_PatrolControl.c'
$plan = Read-Text $planPath
$manager = Read-Text $managerPath
$patrol = Read-Text $patrolPath

# One door keep-out test: swing disc on both sides plus the doorway passage, on the leaf's floor.
$zone = Get-Body $plan 'static\s+bool\s+InDoorZone\s*\('
Assert ($zone.Contains('rise < -0.6 || rise > 1.2') -and $zone.Contains('span + 0.6') -and $zone.Contains('span + 0.3') -and $zone.Contains('crosswise < 1.5')) 'InDoorZone must test the swing disc (width + 0.6 m), the passage (span + 0.3 m, 1.5 m deep) and the floor band'
$openings = Get-Body $plan 'protected\s+void\s+FindOpenings\s*\('
Assert-Before $openings 'if (door) AddDoorLeaf(part, door);' 'if (m_Openings.Count() >= 64) continue;' 'every door leaf must be recorded before the opening cap'
Assert-Before $openings 'if (door) AddDoorLeaf(part, door);' 'if (known) continue;' 'the second leaf of a double door must be recorded before the shared frame is skipped'
Assert ((Get-Body $plan 'protected\s+void\s+AddDoorLeaf\s*\(').Contains('GetDoorPivotPointWS()')) 'door zones must start at the native hinge'

# Applied wherever a guard can stand or stop.
$score = Get-Body $plan 'protected\s+void\s+ScoreOne\s*\('
Assert-Before $score 'node.DoorBlock = InAnyDoorZone(node.Position);' 'if (!node.Reachable || node.Stair)' 'ScoreOne must mark door zones for every node first'
Assert-Before $score 'if (node.DoorBlock) { return; }' 'if (node.Entrance)' 'a node in a door zone must never become a post, entrance post or watch point'
Assert ($score.Contains('horizontal < 4 || horizontal > 16') -and $score.Contains('vector.Dot(inward, opening.Inward) < 1.2') -and $score.Contains('rank += 1.0')) 'door posts must stand 2-4 m from the doorway, 1.2 m deep, diagonal spots first'
Assert ((Get-Body $plan 'protected\s+void\s+Pool\s*\(').Contains('node.DoorBlock')) 'Pool must skip door zones'
Assert (!(Get-Body $plan 'protected\s+void\s+SelectSlots\s*\(').Contains('priorities.Insert(0)')) 'score-0 nodes must become patrol stops, not fixed slots'
$claim = Get-Body $plan 'bool\s+TryClaimStop\s*\(\s*IEntity\s+owner,\s*int\s+node\s*\)'
Assert ($claim.Contains('target.DoorBlock') -and $claim.Contains('StopFree(node, owner)') -and $claim.Contains('ClearBody(') -and $claim.Contains('ReserveNode(owner, node)')) 'TryClaimStop must refuse door zones, crowded stops and occupied floor, one claim per owner'
Assert ((Get-Body $plan 'bool\s+StopFree\s*\(').Contains('POST_SPACING')) 'claims must keep the 1.5 m post spacing'
$walk = Get-Body $plan 'protected\s+void\s+BuildWalk\s*\('
Assert ($walk.Contains('candidate.DoorBlock') -and $walk.Contains('gap < POST_SPACING * POST_SPACING') -and $walk.Contains('MAX_ALERT_NODES')) 'roam candidates, windows and watch points must exclude door zones and keep 1.5 m from slots'
Assert ($plan -match 'static\s+const\s+int\s+MAX_ROAM\s*=\s*96;') 'at most 96 roam stops'
$roam = Get-Body $plan 'protected\s+void\s+RoamStep\s*\('
Assert ($roam.Contains('m_RoamCursor + 256') -and $roam.Contains('POST_SPACING * POST_SPACING')) 'roam stop selection must be bounded per analysis operation and keep 1.5 m'
Assert ((Get-Body $plan 'protected\s+void\s+WatchStep\s*\(').Contains('Done = true;')) 'the plan is done only after the watch directions'
$step = Get-Body $plan 'void\s+Step\s*\(\s*int\s+work'
Assert ($step.Contains('SelectSlots(); BuildWalk(); m_Phase = 5;') -and $step.Contains('RoamStep();') -and $step.Contains('WatchStep();') -and !$step.Contains('Done = true')) 'analysis phases 4-6 must run inside Step'
Assert ((Get-Body $plan 'void\s+DistancesFrom\s*\(').Contains('4096')) 'breadth-first passes must be bounded'
Assert (!(Get-Body $plan 'int\s+NearestNode\s*\(').Contains('Trace')) 'the indoor floor lookup must not trace'
$starts = Get-Body $plan 'protected\s+void\s+AppendPatrolStarts\s*\('
Assert ($starts.Contains('KeepFree(group)') -and $starts.Contains('FixedSlots.Insert(false)')) 'patrol starts follow the free-room rule and are not fixed'

$buildingPost = Get-Body $manager 'protected\s+int\s+TryBuildingPost\s*\('
Assert ($buildingPost.Contains('node.DoorBlock')) 'added building posts must keep out of door zones'
$aroundPost = Get-Body $manager 'protected\s+int\s+TryAroundPost\s*\('
Assert ($aroundPost.Contains('plan.InAnyDoorZone(point)') -and $aroundPost.Contains('plan.InAnyDoorZone(stand)')) 'posts around the building must keep off the steps in front of doors'
$reinforce = Get-Body $manager 'protected\s+void\s+ChooseReinforcement\s*\('
Assert ($reinforce.Contains('if (!plan.FixedSlots[slotOrder]) { continue; }') -and !$reinforce.Contains('scores.Insert(0)') -and $reinforce.Contains('candidate.DoorBlock')) 'reinforcements take free fixed posts and watch positions, never patrol starts or door zones, as fixed posts'
Assert-Before $reinforce 'AddPatrollers(plan, originals, occupied, placements, count);' 'for (int ring = 0;' 'overflow soldiers patrol inside before anyone stands around the building'
Assert-Before $reinforce 'AddPatrollers(plan, originals, occupied, placements, count);' 'EXPG_Placement.BUILDING' 'overflow beyond the free fixed posts patrols inside (free-room rule) before extra watch posts'
Assert ($reinforce.Contains('foreach (int roamStop : plan.RoamStops)') -and $reinforce.Contains('EXPG_Placement.BUILDING, originals, keepClear, placements') -and $reinforce.Contains('ring < 20 &&')) 'extra watch posts keep clear of every patrol stop; around posts stay within 24.75 m of the walls'
$patrollers = Get-Body $manager 'protected\s+void\s+AddPatrollers\s*\('
Assert ($patrollers.Contains('plan.KeepFree(walkGroup)') -and $patrollers.Contains('EXPG_Placement.ROAM') -and $patrollers.Contains('traced >= 96')) 'added patrollers follow the free-room rule, bounded'
Assert ($manager -match 'static\s+const\s+int\s+ROAM\s*=\s*4;' -and ([regex]::Matches($manager, 'array<int>\s+kinds\s*=\s*\{0,\s*0,\s*0,\s*0,\s*0\};').Count -eq 1)) 'placement kinds must count the patrol kind'

# Event-driven, bounded alarm.
Assert ($manager -match 'class\s+EXPG_AlertListener\s*:\s*Managed') 'the alarm listener must be Managed'
Assert ($manager.Contains('GetOnThreatStateChanged().Insert(ThreatHook.OnThreat)') -and $manager.Contains('GetOnThreatStateChanged().Remove(ThreatHook.OnThreat)')) 'threat-state listeners must be bound and removed'
Assert ($manager.Contains('GetOnEnemyDetectedFiltered().Insert(SquadHook.OnEnemyDetected)') -and $manager.Contains('GetOnEnemyDetectedFiltered().Remove(SquadHook.OnEnemyDetected)')) 'enemy-detection listeners must be bound and removed'
Assert ($manager -notmatch 'SCR_AIThreatSystem\s+\w+\s*;') 'never keep a pointer to the (unmanaged) threat system'
Assert ((Get-Body $manager 'void\s+FinishRelease\s*\(').Contains('UnbindSquadHook();')) 'release must unbind the squad listener'
$alarm = Get-Body $manager 'protected\s+void\s+ServiceAlert\s*\('
Assert ($alarm.Contains('handled >= 2') -and $alarm.Contains('polled >= 32') -and $alarm.Contains('Math.RandomFloat(3, 8)') -and $alarm.Contains('patrol.DelayRetry(5)')) 'the alarm handles at most two patrollers and 32 threat reads per Tick, retries every 5 s, releases holders every 3-8 s'
Assert ($manager -match 'static\s+const\s+float\s+CALM_SECONDS\s*=\s*60;') 'patrol resumes 60 s after the last alarm'
$place = Get-Body $manager 'protected\s+void\s+PlaceAlert\s*\('
Assert-Before $place 'plan.WindowNodes' 'plan.WatchNodes' 'windows come before watch points'
Assert ($place.Contains('watchHops > 12') -and $place.Contains('patrol.HoldHere();')) 'watch points within 12 steps, else hold the stop'
$tick = Get-Body $manager 'protected\s+void\s+Tick\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
Assert-Before $tick 'if (!record.BindControls())' 'ServiceAlert(record);' 'the alarm runs after the controls are bound'
Assert ($tick.Contains('record.AlertActive ||') -and $tick.Contains('member.NodeIndex = member.Patrol.ClaimedNode();')) 'an alarm keeps the garrison awake; a patroller''s post follows his claim'
$sleep = Get-Body $manager 'protected\s+void\s+TrySleep\s*\('
Assert-Before $sleep 'if (!PatrolsSettled(record)) { return; }' 'TryFullSleep(record);' 'caching waits until every patroller dwells at a stop'
$settled = Get-Body $manager 'protected\s+bool\s+PatrolsSettled\s*\('
Assert ($settled.Contains('>= 20') -and $settled.Contains('ForceSettle()') -and !$settled.Contains('Report(')) 'the settle gate is silent and forces a stop after 20 s'
Assert ((Get-Body $manager 'protected\s+void\s+Pump\s*\(') -match 'System\.GetTickCount\(\)\s*-\s*analysisStart\s*<\s*4\)') 'the analysis budget must stay 4 ms per pump'

# Patrol movement: walk on patrol, run on alarm, contained paths and steps, dwell 10-30 s.
Assert ((Get-Body $patrol 'override\s+EMovementType\s+GetSpeed\s*\(').Contains('return MaxSpeed;')) 'the patrol speed setting caps at its walk or run speed'
Assert ((Get-Body $patrol 'protected\s+void\s+StartLeg\s*\(').Contains('EMovementType.WALK, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL')) 'patrol legs walk at normal priority'
$alert = Get-Body $patrol 'bool\s+Alert\s*\(\s*int\s+node'
Assert ($alert.Contains('m_Plan.TryClaimStop(m_Actor, node)') -and $alert.Contains('EMovementType.RUN, SCR_AIActionBase.PRIORITY_LEVEL_PLAYER')) 'alarm moves claim first and run at player level'
Assert ([regex]::Matches($patrol, 'Math\.RandomFloat\(10000,\s*30000\)').Count -ge 2) 'patrollers dwell 10-30 s'
$inputGuard = Get-Body $patrol 'bool\s+AllowInput\s*\('
Assert ($inputGuard.Contains('m_Plan.Contained(projected)') -and $inputGuard.Contains('m_Plan.Contained(inertial)') -and !$inputGuard.Contains('Trace')) 'the per-frame guard checks the next step and momentum with O(1) lookups only'
$pathInside = Get-Body $patrol 'protected\s+bool\s+PathInside\s*\('
Assert ($pathInside.Contains('> 64') -and $pathInside.Contains('samples < 128') -and $pathInside.Contains('NearParked')) 'native paths are inspected with bounded work and keep off parked posts'
foreach ($name in 'Settled', 'InAlert', 'HoldsWindow', 'Calm', 'HoldHere', 'ForceSettle', 'SetSettle', 'ClaimedNode') {
 Assert ($patrol -match "\b$name\s*\(") "patrol control must expose $name"
}
Assert ($patrol -notmatch 'InEdgeCorridor') 'the edge corridor patrol is replaced'
$contract = Read-Text (Join-Path $repo 'tests/EXPG_PatrolControlTest.c')
Assert ($contract.Contains('EXPG_BuildingPlan.InDoorZone(')) 'the corridor contract checks the door zone geometry'
Assert ((Read-Text (Join-Path $repo 'tests/EXPG_BuildingPlanTest.c')).Contains('node.Score = EXPG_BuildingPlan.SCORE_STAIRS;')) 'the slot contract uses fixed scores'

# Enforce gotchas in the touched sources and fixtures.
$fixturePath = Join-Path $repo 'tests/EXPG_InteriorGameplay.c'
$touched = @($planPath, $managerPath, $patrolPath, $fixturePath, (Join-Path $repo 'tests/EXPG_RepeatGarrisonGameplay.c'), (Join-Path $repo 'tests/EXPG_PatrolControlTest.c'), (Join-Path $repo 'tests/EXPG_BuildingPlanTest.c'))
foreach ($path in $touched) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 Assert (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $path"
 Assert (![regex]::IsMatch($text, '\b(?:bool|int|float|vector|IEntity)\s+(?:Component|Shape|Color|Widget|Animation|Physics|Material|Resource|Sound|Decal|Particles)\s*[;=]')) "field named like a vanilla type in $path"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
foreach ($path in @($fixturePath, $PSCommandPath)) {
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG INTERIOR RESULT\] checks=[1-9]\d* failures=0 leaves=[1-9]\d* zoned=0 doorGuards=0 doorsOpened=\d+ doorsBlocked=0 guards=[1-9]\d* rovers=[3-9]\d* roamMoved=[3-9]\d* roamOutside=0 stacked=0 overlap=0 alertWindows=\d+ alertWatch=\d+ alertOutside=0 alertStacked=0 calmReturned=1 killed=1 fullRovers=[2-9]\d* simRovers=[2-9]\d* replenished=0 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EXPG_InteriorGameplay.c -ExpectResult '$expect' -TimeoutSeconds 600")) 'interior fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'interior fixture must keep the runner driver class names'
Assert ($fixture.Contains('PrintFormat("[EXPG INTERIOR RESULT] %1 %2 %3 %4", first, second, third, fourth);') -and $fixture.Contains('"checks=%1 failures=%2 leaves=%3 zoned=%4 doorGuards=%5 doorsOpened=%6 doorsBlocked=%7"') -and $fixture.Contains('"guards=%1 rovers=%2 roamMoved=%3 roamOutside=%4 stacked=%5 overlap=%6"') -and $fixture.Contains('"alertWindows=%1 alertWatch=%2 alertOutside=%3 alertStacked=%4 calmReturned=%5"') -and $fixture.Contains('"killed=%1 fullRovers=%2 simRovers=%3 replenished=%4 reason=%5"')) 'interior fixture must print the RESULT line its regex expects'
Assert ($fixture.Contains('m_ThreatSystem.ThreatBulletImpact(5)') -and $fixture.Contains('Manager.AdoptFresh(group, Structure, 0, SQUAD)') -and $fixture.Contains('SetCacheMode(2)') -and $fixture.Contains('SetCacheMode(1)')) 'interior fixture must drive the production threat event, Add Garrison and both cache modes'
Assert ($fixture -match 'Resource\s+houseResource\s*=\s*Resource\.Load\(house\);' -and $fixture -match 'Resource\s+teamResource\s*=\s*Resource\.Load\(team\);') 'interior fixture must keep Resource.Load results in a local'
Assert ((Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')).Contains($expect)) 'tests/GAMEPLAY.md must document the interior fixture regex'
'PASS: one door keep-out test for every post, stop and watch point, door posts set back 2-4 m, interior patrol stops with unique spaced claims, bounded event-driven alarm, settle gate before caching, 4 ms budget kept; interior fixture wired.'
