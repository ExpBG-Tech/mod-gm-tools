#requires -Version 7.0
# Portable guard for Garrison post holding (live report 2026-10-07, 0.1.9 + ACE +
# EXPBG RO AI on a dedicated server: guards left their spots and ran around, and
# garrison caching stopped). Any one guard displaced more than 1.5 m, possessed,
# regrouped or deleted released the whole squad silently. Checks the source
# invariants: displacement re-anchors and never releases, BindControls never stops
# at the first failure, one guard's problem is handled for that guard alone, every
# whole-garrison release names its reason, caching holds say why, Enforce gotchas,
# and the wiring of tests/EXPG_HoldGameplay.c. No engine is launched.
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
function Count([string]$Text, [string]$Literal) { ([regex]::Matches($Text, [regex]::Escape($Literal))).Count }

$postPath = Join-Path $scripts 'EXPG_PostControl.c'
$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$fullPath = Join-Path $scripts 'EXPG_FullCache.c'
$attributesPath = Join-Path $scripts 'EXPG_Attributes.c'
$post = Read-Text $postPath
$manager = Read-Text $managerPath
$full = Read-Text $fullPath
$attributes = Read-Text $attributesPath

# Post control: displacement re-anchors where he came to rest; only lost ownership releases.
Assert ($post -match 'static\s+const\s+float\s+DRIFT_SQ\s*=\s*0\.25;' -and $post -match 'static\s+const\s+float\s+BIND_SQ\s*=\s*2\.25;') 'post control keeps a 0.5 m drift and a 1.5 m bind tolerance'
$bind = Get-Body $post 'bool\s+Bind\s*\(\s*SCR_ChimeraCharacter\s+actor'
Assert ($bind.Contains('actor.GetOrigin()) > BIND_SQ') -and $bind.Contains('m_Moved = true;')) 'Bind accepts a guard within 1.5 m and holds him where he stands (never snaps back)'
$tick = Get-Body $post 'bool\s+Tick\s*\(\s*\)'
Assert ((Count $tick 'Release();') -eq 1) 'post Tick releases only on lost ownership, never after a displacement'
Assert ($tick.IndexOf('Release();') -lt $tick.IndexOf('m_Actor.SetSpeedLimit(this, 0, true);')) 'post Tick re-applies the native speed cap after the ownership test'
Assert ($tick.Contains('m_Controller.IsUnconscious() || Ragdolled()') -and $tick.Contains('> DRIFT_SQ && Still()') -and $tick.Contains('m_Position = m_Actor.GetOrigin();') -and $tick.Contains('m_Moved = true;')) 'post Tick waits out unconsciousness and ragdoll, then takes the spot he came to rest on'
Assert ((Get-Body $post 'protected\s+bool\s+Ragdolled\s*\(').Contains('IsRagdollActive()') -and (Get-Body $post 'protected\s+bool\s+Still\s*\(').Contains('GetVelocity()')) 'ragdoll and rest use the native animation and velocity'
Assert ((Get-Body $post 'bool\s+TakeMoved\s*\(\s*out\s+vector\s+anchor\s*\)').Contains('m_Moved = false;')) 'the manager takes a moved post once'

# Manager: one guard's problem is that guard's alone; whole releases name their reason.
Assert ((Count $manager 'ReleaseRequested = true') -eq 1 -and (Get-Body $manager 'void\s+RequestRelease\s*\(\s*string\s+reason\s*\)').Contains('ReleaseRequested = true')) 'every whole-garrison release goes through RequestRelease'
Assert ((Count $full 'ReleaseRequested') -eq 0) 'a Full restore never releases the whole garrison for one guard'
foreach ($file in Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File) {
 if ($file.Name -eq 'EXPG_GarrisonManager.c') { continue }
 Assert (!(Read-Text $file.FullName).Contains('ReleaseRequested = true')) "$($file.Name) must request releases through RequestRelease"
}
$finish = Get-Body $manager 'void\s+FinishRelease\s*\('
Assert ($finish.Contains('PrintFormat("[EXPG Garrison] group=%1 released (%2)", Group, reason);') -and $finish.Contains('Group.EXPG_Status = "Released: " + reason;')) 'every release prints its reason and keeps it in the status'
$anchor = Get-Body $manager 'void\s+Anchor\s*\(\s*vector\s+at\s*\)'
Assert ($anchor.Contains('NearestNode(at, 0.5, true)') -and $anchor.Contains('ReserveNode(CacheMember.Entity, node)') -and $anchor.Contains('NodeIndex = -1;') -and !$anchor.Contains('SetWorldTransform') -and !$anchor.Contains('SetTransform')) 'Anchor keeps a node within 0.5 m or an off-plan post and never moves him'
$bindControls = Get-Body $manager 'bool\s+BindControls\s*\(\s*\)'
Assert (!$bindControls.Contains('return false;') -and $bindControls.Contains('return complete;') -and $bindControls.Contains('complete = false;')) 'BindControls never stops at the first failure'
Assert ($bindControls.Contains('EXPG_PostControl.BIND_SQ') -and $bindControls.Contains('member.Anchor(origin);') -and $bindControls.Contains('controller.IsPlayerControlled()') -and $bindControls.Contains('member.UnboundSince') -and $bindControls.Contains('member.Fixed = true;')) 'BindControls anchors far or displaced guards, leaves a possessed one to the player and turns a stuck patroller into a fixed guard'
$managerTick = Get-Body $manager 'protected\s+void\s+Tick\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
Assert ((Count $managerTick 'FinishRelease()') -eq (Count $managerTick 'if (record.ReleaseRequested) { record.FinishRelease(); return; }')) 'Tick finishes a release only when one was requested with a reason'
Assert ($managerTick.Contains('if (!record.BindControls())') -and $managerTick.IndexOf('if (!record.BindControls())') -lt $managerTick.IndexOf('ServiceAlert(record);')) 'an unbound guard only holds caching; the alarm still runs after binding'
Assert ($managerTick.Contains('member.Post.TakeMoved(anchor)') -and $managerTick.Contains('member.Anchor(anchor);') -and $managerTick.Contains('holds where he stands')) 'Tick copies a moved post and logs it'
Assert ($bindControls.Contains('if (!member.Arrived)') -and $bindControls.Contains('member.ResendMove(actor, now, Group);') -and $bindControls.IndexOf('if (!member.Arrived)') -lt $bindControls.IndexOf('member.Anchor(origin);')) 'a guard is bound or anchored only once he stands on the post he was moved to'
Assert ((Get-Body $manager 'protected\s+bool\s+Initialize\s*\(').Contains('member.Arrived = false;')) 'placement waits for the editor move to land before binding'
$plausible = Get-Body $manager 'bool\s+Plausible\s*\(\s*vector\s+at,\s*IEntity\s+actor\s*\)'
Assert ($plausible.Contains('MAX_DRIFT') -and $plausible.Contains('Plan.Contained(at)') -and $plausible.Contains('Plan.Supported(at, 0.3, actor)') -and $plausible.Contains('Plan.GroundSupported(at, 0.3, actor)')) 'a resting spot becomes a post only near the given post, on the indoor floor (or walkable ground outside)'
Assert ($bindControls.Contains('member.Plausible(origin, actor)') -and $bindControls.Contains('member.SendBack(actor, now);') -and $managerTick.Contains('member.Plausible(anchor, actor)') -and $managerTick.Contains('EXPG_GarrisonMember.Teleport(actor, member.PostPoint(), member.PostLook);') -and $managerTick.Contains('EXPG_GarrisonMember.AtRest(actor)')) 'an implausible resting spot never becomes a post: he is sent back once conscious and still'
Assert ($managerTick.Contains('if (member.Post && !member.Post.Tick()) { member.Post = null; unsafe = true; }') -and $managerTick.Contains('if (!member.Patrol.Tick()) { member.Patrol = null; unsafe = true; }')) 'a lost control is rebound for that guard alone'
Assert ($managerTick.Contains('ForgetLeavers(record);') -and $managerTick.Contains('cached.Dead = true;') -and $managerTick.Contains('never respawned') -and $managerTick.Contains('cached.WasPlayer = true;')) 'leavers are forgotten, a deleted guard is dead and never respawned, a possessed guard is his matter only'
Assert ($managerTick.Contains('record.RequestRelease("Squad deleted");') -and $managerTick.Contains('record.RequestRelease("Building moved or was replaced");') -and $managerTick.Contains('record.RequestRelease("No surviving guards");')) 'the remaining whole-garrison releases name their reasons'
Assert (!$managerTick.Contains('releasing survivors')) 'a floor probe never releases the survivors'
Assert ($managerTick.Contains('record.NoteHold(hold, Now());') -and $managerTick.Contains('record.ClearHold();') -and $managerTick.Contains('record.AlertActive ||')) 'a held cache names its reason; an alarm keeps the garrison awake'
$leavers = Get-Body $manager 'protected\s+void\s+ForgetLeavers\s*\('
Assert ($leavers.Contains('IsPlayerControlled()') -and $leavers.Contains('record.Members.RemoveOrdered(leftIndex);') -and $leavers.Contains('the other guards keep their posts')) 'a living guard in another squad is forgotten alone, never a possessed one'
$noteHold = Get-Body $manager 'void\s+NoteHold\s*\('
Assert ($noteHold.Contains('string message = "Cache held: " + reason;') -and $noteHold.Contains('Report(message);') -and $noteHold.Contains('now - HoldSince < 30') -and $noteHold.Contains('now - HoldShownAt < 10')) 'a cache hold is shown once caching was held 30 s, a changed reason at most every 10 s'
$wake = Get-Body $manager 'protected\s+bool\s+Wake\s*\(\s*EXPG_GarrisonRecord\s+record\s*\)'
Assert (!$wake.Contains('ReleaseRequested')) 'a Simulation wake never releases the garrison for one guard'
$wakeFull = Get-Body $manager 'protected\s+bool\s+WakeFull\s*\('
Assert ($wakeFull.Contains('ForgetLeavers(record);') -and $wakeFull.Contains('member.Forced = true;') -and $wakeFull.Contains('member.CacheMember.Dead = true;') -and $wakeFull.Contains('if (!record.BindControls())')) 'a Full wake forgets leavers, never respawns a missing guard, waits at most 10 s on an unsafe spot, and stays gated by the bind'
$fullSleep = Get-Body $manager 'protected\s+void\s+TryFullSleep\s*\('
Assert ($fullSleep.Contains('SquadProblem(group)') -and $fullSleep.Contains('SurvivorProblem(cache, group)') -and !$fullSleep.Contains('survivor ownership changed') -and !$fullSleep.Contains('group has external ownership')) 'a held Full sleep names the squad or guard reason'
Assert ($fullSleep -match 'record\.BindControls\(\);\s*\r?\n\s*record\.Report\("Full cache held: " \+ failure\);') 'an abandoned Full transaction rebinds without releasing'
Assert ((Get-Body $manager 'protected\s+void\s+TrySleep\s*\(').Contains('"Cache held: a Unit Caching save or load is in progress"')) 'a save or load in progress is a named hold'
Assert ((Get-Body $attributes 'protected\s+SCR_AIGroup\s+GetReleased\s*\(').Contains('StartsWith("Released: ")')) 'the Game Master status keeps showing a released garrison''s reason'

# Enforce gotchas in the touched sources and the fixture.
$fixturePath = Join-Path $repo 'tests/EXPG_HoldGameplay.c'
foreach ($path in @($postPath, $managerPath, $fullPath, $attributesPath, $fixturePath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|external)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 Assert (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $path"
 Assert (![regex]::IsMatch($text, '\b(?:bool|int|float|vector|IEntity|CharacterAnimationComponent)\s+(?:Component|Shape|Color|Widget|Animation|Physics|Material|Resource|Sound|Decal|Particles)\s*[;=]')) "field named like a vanilla type in $path"
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
foreach ($path in @($fixturePath, $PSCommandPath)) {
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG HOLD RESULT\] checks=[1-9]\d* failures=0 guards=4 fixed=[34] pushHeld=1 knockedOut=1 knockHeld=1 othersKept=1 deleted=1 respawned=0 fullRestored=3 simRestored=3 premature=0 released=1 reasons=1 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EXPG_HoldGameplay.c -ExpectResult '$expect' -TimeoutSeconds 600")) 'hold fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'hold fixture must keep the runner driver class names'
Assert ($fixture.Contains('PrintFormat("[EXPG HOLD RESULT] %1 %2", first, second);') -and $fixture.Contains('"checks=%1 failures=%2 guards=%3 fixed=%4 pushHeld=%5 knockedOut=%6 knockHeld=%7 othersKept=%8 deleted=%9"') -and $fixture.Contains('"respawned=%1 fullRestored=%2 simRestored=%3 premature=%4 released=%5 reasons=%6 reason=%7"')) 'hold fixture must print the RESULT line its regex expects'
Assert ($fixture.Contains('Manager.AdoptFresh(Group, Structure, 0, SQUAD)') -and $fixture.Contains('editable.SetTransform(transform)') -and $fixture.Contains('SetUnconscious(true)') -and $fixture.Contains('editable.Delete(false, false)') -and $fixture.Contains('SetCacheMode(2)') -and $fixture.Contains('SetCacheMode(1)') -and $fixture.Contains('Manager.Release(Group)')) 'hold fixture must drive Add Garrison, a Game Master move, unconsciousness, deletion, both cache modes and the Release'
Assert ($fixture -match 'Resource\s+houseResource\s*=\s*Resource\.Load\(house\);' -and $fixture -match 'Resource\s+teamResource\s*=\s*Resource\.Load\(team\);') 'hold fixture must keep Resource.Load results in a local'
Assert ((Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')).Contains($expect)) 'tests/GAMEPLAY.md must document the hold fixture regex'
'PASS: displaced guards hold where they came to rest, one guard''s problem never releases the others, BindControls never stops at the first failure, every release names its reason, caching holds say why; hold fixture wired.'
