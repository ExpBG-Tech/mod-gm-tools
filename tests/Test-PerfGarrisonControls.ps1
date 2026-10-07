#requires -Version 7.0
# Portable guard for the garrison control performance work (0.1.15 plan WP5, findings
# garrison-4 and garrison-7). The per-frame input gate keeps its result; only its cost
# changes:
#  - the RplComponent is found once at Bind/Start, never in IsOwnedActor (every frame);
#  - AllowInput tests ownership once per frame and InspectCurrentPath(true) skips the
#    repeat; the record Tick keeps the full test;
#  - IsOwnedActor tests the cheap native flags before the agent and group identity, and
#    the patrol keeps m_Plan.Valid() in the per-frame test (a moved building releases at
#    once);
#  - a calm look is re-issued at most once a second for the same target (each request
#    restarts the native 2 s look), at once for a new target or after an interruption.
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

function Get-Failures([string]$Post, [string]$Patrol) {
 $failures = [Collections.Generic.List[string]]::new()
 function Check([bool]$Condition, [string]$Message) { if (!$Condition) { $failures.Add($Message) } }

 # garrison-4 (1): the replication component is cached once, never searched per frame.
 $postOwned = Get-Body $Post 'bool\s+IsOwnedActor\s*\(\s*\)'
 Check ($postOwned.Length -gt 0 -and !$postOwned.Contains('FindComponent(') -and $postOwned.Contains('if (m_Rpl && m_Rpl.IsProxy())')) 'post IsOwnedActor (every frame) uses the RplComponent cached at Bind, no FindComponent'
 Check ((Count $Post 'FindComponent(RplComponent)') -eq 1) 'post control looks its RplComponent up once (Bind)'
 $bind = Get-Body $Post 'bool\s+Bind\s*\(\s*SCR_ChimeraCharacter\s+actor'
 Check (Before $bind 'm_Rpl = RplComponent.Cast(actor.FindComponent(RplComponent));' 'if (!IsOwnedActor())') 'post Bind caches the RplComponent before its first ownership test'
 $postRelease = Get-Body $Post 'void\s+Release\s*\(\s*\)'
 Check ($postRelease.Contains('m_Rpl = null;') -and $postRelease.Contains('m_LookIssued = -1000;')) 'post Release clears the cached component and the look throttle'
 $patrolOwned = Get-Body $Patrol 'bool\s+IsOwnedActor\s*\(\s*\)'
 Check ($patrolOwned.Length -gt 0 -and !$patrolOwned.Contains('FindComponent(') -and $patrolOwned.Contains('if (m_Rpl && m_Rpl.IsProxy())')) 'patrol IsOwnedActor (every frame) uses the RplComponent cached at Start, no FindComponent'
 Check ((Count $Patrol 'FindComponent(RplComponent)') -eq 1) 'patrol control looks its RplComponent up once (Start)'
 $start = Get-Body $Patrol 'bool\s+Start\s*\(\s*SCR_ChimeraCharacter\s+actor'
 Check (Before $start 'm_Rpl = RplComponent.Cast(actor.FindComponent(RplComponent));' 'if (!IsOwnedActor()') 'patrol Start caches the RplComponent before its first ownership test'
 $patrolRelease = Get-Body $Patrol 'void\s+Release\s*\(\s*\)'
 Check ($patrolRelease.Contains('m_Rpl = null;') -and $patrolRelease.Contains('m_LookIssued = -1000;')) 'patrol Release clears the cached component and the look throttle'

 # garrison-4 (3): cheap native flags first; the patrol's building test stays per frame.
 Check ((Before $postOwned 'controller.IsDead()' 'control.GetAIAgent() == m_Agent') -and (Before $postOwned 'm_Actor.IsInVehicle()' 'm_Agent.GetParentGroup() == m_Group')) 'post IsOwnedActor tests the native flags before the agent and group identity'
 Check ($patrolOwned.Contains('m_Plan.Valid()') -and (Before $patrolOwned '!m_Plan' 'm_Plan.Valid()') -and (Before $patrolOwned 'm_Controller.IsDead()' 'm_Plan.Valid()') -and (Before $patrolOwned 'm_Actor.IsInVehicle()' 'control.GetAIAgent() == m_Agent')) 'patrol IsOwnedActor keeps m_Plan.Valid() per frame, after the native flags and its null test'

 # garrison-4 (2): one ownership test per frame for a walking patroller.
 $inspect = Get-Body $Patrol 'bool\s+InspectCurrentPath\s*\('
 Check ($Patrol -match 'bool\s+InspectCurrentPath\s*\(\s*bool\s+ownerChecked\s*=\s*false\s*\)' -and $inspect.Contains('(!ownerChecked && !IsOwnedActor())')) 'InspectCurrentPath tests ownership unless its caller already did in the same call'
 $allow = Get-Body $Patrol 'bool\s+AllowInput\s*\('
 Check ($allow.Contains('InspectCurrentPath(true)') -and (Count $allow 'IsOwnedActor()') -eq 1 -and (Before $allow 'if (!m_Speed || !IsOwnedActor()) { Release(); return true; }' 'InspectCurrentPath(true)')) 'AllowInput tests ownership once at its top and passes that to InspectCurrentPath'
 Check ((Count $Patrol 'InspectCurrentPath(true)') -eq 1) 'only AllowInput skips the repeated ownership test'
 $patrolTick = Get-Body $Patrol 'bool\s+Tick\s*\(\s*\)'
 Check ($patrolTick.Contains('else { InspectCurrentPath(); }')) 'the record Tick keeps the full ownership test for the path'
 $prepare = Get-Body $Patrol 'protected\s+override\s+void\s+OnPrepareControls\s*\('
 Check ($prepare.Contains('if (player || !m_EXPG_PostControl.IsOwnedActor()) { m_EXPG_PostControl.Release(); }') -and $prepare.Contains('m_EXPG_PatrolControl.AllowInput(dt, direction, inputScale)')) 'the per-frame input gate still tests ownership every frame (possession releases at once)'

 # garrison-7: the same calm look at most once a second; new targets and interruptions at once.
 foreach ($pair in @(@('post', $Post), @('patrol', $Patrol))) {
  Check ($pair[1] -match 'protected\s+float\s+m_LookIssued\s*=\s*-1000;' -and $pair[1] -match 'protected\s+vector\s+m_LookIssuedAt;') "$($pair[0]) control keeps its last look request"
  Check ((Count $pair[1] 'LookAt(') -eq 1 -and $pair[1].Contains('m_Utility.LookAt(lookTarget, 2.0);') -and $pair[1].Contains('GetWorldTime() * 0.001')) "$($pair[0]) control issues its 2 s look in one place, timed in world seconds"
 }
 $postTick = Get-Body $Post 'bool\s+Tick\s*\(\s*\)'
 Check ($postTick.Contains('if (now - m_LookIssued >= 1.0 || vector.DistanceSq(lookTarget, m_LookIssuedAt) > 0.01)') -and (Before $postTick 'm_Utility.LookAt(lookTarget, 2.0);' 'm_LookIssued = now;') -and $postTick.Contains('m_LookIssuedAt = lookTarget;')) 'post Tick re-issues the same calm look at most once a second, a new target at once'
 Check ((Count $postTick 'm_LookIssued = -1000;') -eq 2 -and (Before $postTick 'Ragdolled())' 'm_LookIssued = -1000;')) 'post Tick re-arms the look at once after knock-out, ragdoll, leaving calm or losing the direction'
 $lookOut = Get-Body $Patrol 'protected\s+void\s+LookOut\s*\(\s*\)'
 Check ($lookOut.Contains('if (now - m_LookIssued < 1.0 && vector.DistanceSq(lookTarget, m_LookIssuedAt) <= 0.01)') -and (Before $lookOut 'm_Utility.LookAt(lookTarget, 2.0);' 'm_LookIssued = now;') -and $lookOut.Contains('m_LookIssuedAt = lookTarget;') -and (Count $lookOut 'm_LookIssued = -1000;') -eq 2) 'patrol LookOut re-issues the same calm look at most once a second, a new target at once'
 Check ((Count $patrolTick 'm_LookIssued = -1000;') -eq 2) 'patrol Tick re-arms the look while he is knocked out or walking'

 # Enforce gotchas and the 0.1.13 lazy-static rule in both sources.
 foreach ($pair in @(@('EXPG_PostControl.c', $Post), @('EXPG_PatrolControl.c', $Patrol))) {
  $text = $pair[1]
  Check (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external)\b')) "reserved Enforce name used as a variable in $($pair[0])"
  Check (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $($pair[0])"
  Check (![regex]::IsMatch($text, '\b(?:bool|int|float|vector|IEntity|RplComponent)\s+(?:Component|Shape|Color|Widget|Animation|Physics|Material|Resource|Sound|Decal|Particles|Replication|RplComponent)\s*[;=]')) "field or local named like a vanilla type in $($pair[0])"
  Check (![regex]::IsMatch($text, '(?m)^\s*(?:protected\s+|private\s+)?static\s+(?:ref\s+|const\s+)?(?:array|map|set)<')) "static collection (0.1.13 lazy-static rule) in $($pair[0])"
 }
 Check ((Count $Patrol 'Math.RandomFloat(') -le 6 -and (Count $Post 'Math.RandomFloat(') -eq 0) 'no new Math.RandomFloat in the garrison controls'
 return , $failures
}

$postPath = Join-Path $scripts 'EXPG_PostControl.c'
$patrolPath = Join-Path $scripts 'EXPG_PatrolControl.c'
$post = Read-Text $postPath
$patrol = Read-Text $patrolPath
$failures = Get-Failures $post $patrol
Assert ($failures.Count -eq 0) ("garrison controls:`n - " + ($failures -join "`n - "))
foreach ($path in @($postPath, $patrolPath, $PSCommandPath)) {
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
Assert (([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -eq 13 }).Count -eq 0) "new files use LF line endings: $PSCommandPath"

# Self-test: each regression of the plan must be caught. A mutation replaces the Nth
# occurrence (default the first) of a text that must exist.
$mutations = @(
 @('post', 'if (m_Rpl && m_Rpl.IsProxy())', 'RplComponent rpl = RplComponent.Cast(m_Actor.FindComponent(RplComponent)); if (rpl && rpl.IsProxy())', 'per-frame FindComponent in post IsOwnedActor'),
 @('patrol', 'if (m_Rpl && m_Rpl.IsProxy())', 'RplComponent rpl = RplComponent.Cast(m_Actor.FindComponent(RplComponent)); if (rpl && rpl.IsProxy())', 'per-frame FindComponent in patrol IsOwnedActor'),
 @('patrol', 'if (!InspectCurrentPath(true))', 'if (!InspectCurrentPath())', 'second ownership test per frame'),
 @('patrol', 'else { InspectCurrentPath(); }', 'else { InspectCurrentPath(true); }', 'Tick skipping its ownership test'),
 @('patrol', ' && m_Plan.Valid();', ';', 'building test moved out of the frame'),
 @('patrol', '		m_Rpl = null;', '', 'patrol Release keeping the cached component'),
 @('post', 'now - m_LookIssued >= 1.0', 'now - m_LookIssued >= 0.0', 'post look unthrottled'),
 @('patrol', 'now - m_LookIssued < 1.0', 'now - m_LookIssued < 1.5', 'patrol look throttled past the 2 s look'),
 @('post', 'm_LookIssued = -1000;', 'm_LookIssued = m_LookIssued;', 'post look not re-armed after a knock-out', 2),
 @('post', 'm_LookIssued = -1000;', 'm_LookIssued = m_LookIssued;', 'post look not re-armed after leaving calm', 3),
 @('patrol', 'm_LookIssued = -1000;', 'm_LookIssued = m_LookIssued;', 'patrol look not re-armed after walking', 3),
 @('patrol', 'protected float m_LookIssued = -1000;', 'protected float m_LookIssued;', 'patrol look throttle starting armed')
)
foreach ($mutation in $mutations) {
 $target = if ($mutation[0] -eq 'post') { $post } else { $patrol }
 $occurrence = if ($mutation.Count -gt 4) { $mutation[4] } else { 1 }
 $index = -1
 for ($n = 0; $n -lt $occurrence; $n++) { $index = $target.IndexOf($mutation[1], $index + 1, [StringComparison]::Ordinal); if ($index -lt 0) { break } }
 Assert ($index -ge 0) "self-test text not found ($($mutation[3])): $($mutation[1]) #$occurrence"
 $mutated = $target.Substring(0, $index) + $mutation[2] + $target.Substring($index + $mutation[1].Length)
 $caught = if ($mutation[0] -eq 'post') { Get-Failures $mutated $patrol } else { Get-Failures $post $mutated }
 Assert ($caught.Count -gt 0) "self-test: the guard missed a regression ($($mutation[3]))"
}
"PASS: garrison controls cache the RplComponent at Bind/Start, test ownership once per frame (cheap flags first, patrol building test kept per frame) and re-issue a calm look at most once a second; $($mutations.Count) self-test regressions caught."
