#requires -Version 7.0
# Portable guard for Unit Scripts position holding (production 2026-10-07: a frozen lone
# officer drifted 0.78 m in 35 s and an 8 m jump was logged as "re-anchored"; with "Sit on
# a chair" next to a GM-placed table the server fell from 240 to 70-145 FPS for about an
# hour, and the pose gave up after 4 attempts because each physics push re-anchored him).
# Checks the source invariants: displacement is never mistaken for a Game Master move,
# Freeze and Hold put the soldier back the way the editor teleports and only an object on
# his spot lets him keep a new one, real Game Master moves (the editor's SetTransform) set
# the new spot, Freeze keeps its look claimed, the chair pose needs room where it is issued
# and a pushed pose ends, every log stays bounded, Enforce gotchas, and the wiring of
# tests/EUS_FreezeLeaderGameplay.c. Source pins protect the implementation; the native
# fixture carries the behaviour. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/unit-scripts/Scripts/Game/EXPUS'
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
# Comments and string contents blanked, so code checks never match prose.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}

$controlPath = Join-Path $scripts 'EUS_UnitControl.c'
$codesPath = Join-Path $scripts 'EUS_Codes.c'
$control = Read-Text $controlPath
$codes = Read-Text $codesPath
$controlCode = Get-Code $control

# Tolerances and cadences.
foreach ($needle in 'static const float FREEZE_TOLERANCE = 0.35;', 'static const float HOLD_TOLERANCE = 1.5;', 'static const float ANIMATION_TOLERANCE = 1.5;',
 'static const float FREEZE_TURN_DOT = 0.25;', 'static const float FALL_HEIGHT = 1;', 'static const float SETTLE_SECONDS = 1.5;', 'static const float NOTE_SECONDS = 300;',
 'static const float ROOM_SECONDS = 10;', 'static const float FREEZE_LOOK_DOT = 0.5;', 'static const float FIGHT_RADIUS = 0.25;', 'static const float PLATFORM_SPEED_SQ = 0.04;',
 'static const float MOVE_NOTE_SECONDS = 10;') {
 Assert $control.Contains($needle) "unit control lost: $needle"
}
# The turn and look limits as the source declares them, not as this guard assumes them.
function Get-Constant([string]$Name) {
 $match = [regex]::Match($controlCode, 'static\s+const\s+float\s+' + $Name + '\s*=\s*([0-9]*\.?[0-9]+)\s*;')
 Assert $match.Success "constant not found: $Name"
 return [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
}
$turnDegrees = [Math]::Acos((Get-Constant 'FREEZE_TURN_DOT')) * 180 / [Math]::PI
$lookDegrees = [Math]::Acos((Get-Constant 'FREEZE_LOOK_DOT')) * 180 / [Math]::PI
Assert ($turnDegrees -gt $lookDegrees + 10) "the Freeze turn limit ($turnDegrees deg) stays clearly beyond the head cone ($lookDegrees deg), so it never fights its own head tracking"
Assert ($turnDegrees -lt 90) "the Freeze turn limit ($turnDegrees deg) still holds a heading (under 90 degrees)"

# Tick: no distance guess. The spot moves only through settling, Game Master moves, a drop or an occupied spot.
$tick = Get-Body $control 'bool\s+Tick\s*\(\s*float\s+now,\s*notnull\s+array<IEntity>\s+players\s*\)'
Assert (!$control.Contains('re-anchored') -and !$tick.Contains('DistanceSqXZ') -and !$tick.Contains('m_Anchor =')) 'Tick never re-anchors on distance (a displacement is not a Game Master move)'
Assert ($tick.Contains('if (m_SettleUntil >= 0)') -and $tick.Contains('if (!EUS_Codes.IsAnimation(m_Code)) TakeSpot();') -and $tick.Contains('else if (!HoldSpot(now))')) 'Tick settles after the bind or a Game Master move, then holds'
Assert ($tick.Contains('if (!EUS_Codes.IsAnimation(m_Code)) m_SpotObstacle = SpotObstacle();') -and $tick.IndexOf('m_SpotObstacle = SpotObstacle();') -gt $tick.IndexOf('m_SettleUntil = -1;')) 'the settled spot records the furniture already on it, once per bind or move'
Assert ($tick.IndexOf('IsOwnedActor()') -lt $tick.IndexOf('HoldSpot(now)')) 'ownership is tested before the spot is held'
Assert ((Count $controlCode 'm_Anchor =') -eq 3 -and (Count $controlCode 'm_Anchor = m_Actor.GetOrigin();') -eq 1 -and (Count $controlCode 'm_Anchor = actor.GetOrigin();') -eq 1 -and (Count $controlCode 'm_Anchor = transform[3];') -eq 1) 'the spot is written only at bind, by TakeSpot and by a Game Master move'
$takeSpotCallers = [regex]::Matches($controlCode, '\bTakeSpot\(\)\s*;').Count
Assert ($takeSpotCallers -eq 2) "TakeSpot is called only while settling and when the spot dropped or is occupied (found $takeSpotCallers)"

$hold = Get-Body $control 'protected\s+bool\s+HoldSpot\s*\(\s*float\s+now\s*\)'
Assert ($hold.Contains('if (EUS_Codes.IsAnimation(m_Code)) return KeepPose(now, drift);')) 'an animation is never snapped back'
Assert ($hold.Contains('m_Controller.IsFalling() || m_Controller.IsChangingStance()')) 'a falling or stance-changing soldier is judged once he has landed'
Assert ($hold.Contains('tolerance = FREEZE_TOLERANCE;') -and $hold.Contains('vector.Dot(EUS_Codes.Forward(m_Actor), m_Forward) < FREEZE_TURN_DOT') -and $hold.Contains('float tolerance = HOLD_TOLERANCE;')) 'Freeze holds 0.35 m and its heading, Hold 1.5 m and never its heading'
Assert ($hold.Contains('m_Anchor[1] - origin[1] > FALL_HEIGHT') -and $hold.Contains('IEntity platform = MovingPlatform();')) 'after a drop or on a moving platform he keeps the new spot'
Assert ($hold.Contains('bool pushed = drift > tolerance;') -and $hold.Contains('if (!pushed && !turned)')) 'a correction is either a push beyond the tolerance or a Freeze turn'
# Only an object on the spot holds him off it; a turn or a push with nothing on the spot is always put back.
$pushedBranch = [regex]::Match($hold, '(?s)else if \(pushed\)\s*\{(.*?)\n\s*\}')
Assert $pushedBranch.Success 'the spot trace and both object rules sit in the pushed branch'
Assert ($pushedBranch.Groups[1].Value.Contains('IEntity obstacle = SpotObstacle();')) 'the spot is traced once per push correction, never for a turn alone'
Assert ($pushedBranch.Groups[1].Value.Contains('if (obstacle && obstacle != m_SpotObstacle) kept = "was pushed off by "')) 'only an object that was not on the settled spot (not the officer''s own desk) makes him keep a new spot at once'
Assert ($pushedBranch.Groups[1].Value.Contains('else if (obstacle && m_JustCorrected && vector.DistanceXZ(origin, m_PushedTo) < FIGHT_RADIUS) kept = "keeps being pushed off by "')) 'found again where the previous correction found him, he keeps that spot only while an object stands on it (a steady mover in the open never walks the spot along)'
Assert ((Count $hold 'kept = ') -eq 4 -and $hold.IndexOf('else if (pushed)') -lt $hold.IndexOf('kept = "was pushed off by "') -and $hold.IndexOf('else if (pushed)') -lt $hold.IndexOf('kept = "keeps being pushed off by "')) 'a spot is kept only after a drop, on a moving platform or for an object on it; a turn alone is never kept'
Assert ($hold.IndexOf('SpotObstacle()') -lt $hold.IndexOf('Restore(turned);') -and $hold.IndexOf('kept = "dropped from"') -lt $hold.IndexOf('Restore(turned);')) 'the world checks run before any snap back'
$after = $hold.Substring($hold.IndexOf('Restore(turned);'))
Assert ($after.Contains('m_JustCorrected = true;') -and $after.Contains('m_PushedTo = origin;')) 'a correction remembers where it found him'
Assert ($after.Contains('if (turned) m_Turns++;') -and $after.Contains('(%3 turned back), largest drift %4 m (not a Game Master move)') -and $after.Contains('m_Turns = 0;')) 'the held note tells turn corrections apart from pushes'
$within = $hold.Substring($hold.IndexOf('if (!pushed && !turned)'), 70)
Assert ($within.Contains('m_JustCorrected = false;')) 'a tick on the spot ends a correction streak (slow drift is always put back)'
$keptBody = $hold.Substring($hold.IndexOf('if (!kept.IsEmpty())'))
Assert ($keptBody.Contains('TakeSpot();') -and $keptBody.Contains('m_SpotObstacle = SpotObstacle();') -and $keptBody.Contains('m_JustCorrected = false;')) 'a kept spot records its own furniture and ends the streak'
$spotObstacle = Get-Body $control 'protected\s+IEntity\s+SpotObstacle\s*\(\s*\)'
Assert ($spotObstacle.Contains('PoseAt(m_Anchor, m_Forward, pose);') -and $spotObstacle.Contains('Blocked(pose, Vector(-0.2, 0.4, -0.2), Vector(0.2, 1.6, 0.2), obstacle);') -and $spotObstacle.Contains('return obstacle;')) 'the spot box sits on the held spot, knee to head height, 0.4 m square'
$platform = Get-Body $control 'protected\s+IEntity\s+MovingPlatform\s*\(\s*\)'
Assert ($platform.Contains('animation.PhysicsIsLinked()') -and $platform.Contains('animation.GetLinkedEntity()') -and $platform.Contains('GetVelocity().LengthSq() <= PLATFORM_SPEED_SQ) return null;')) 'only a moving linked platform carries the spot (a kinematic GM floor does not)'
Assert ([regex]::Matches($controlCode, '\bSpotObstacle\s*\(\s*\)\s*;').Count -eq 3) 'spot traces: settle end, one per push correction, one per kept spot'
Assert ($hold.Contains('if (now >= m_NextNote)') -and $hold.Contains('if (now < m_NextNote) return true;') -and (Count $hold 'm_NextNote = now + NOTE_SECONDS;') -eq 2) 'hold notes are rate-limited per unit (first at once, then at most one per NOTE_SECONDS)'
Assert ($hold.Contains('(not a Game Master move)')) 'hold notes say the displacement was not a Game Master move'

$restore = Get-Body $control 'protected\s+void\s+Restore\s*\(\s*bool\s+turn\s*\)'
foreach ($needle in 'm_Actor.GetWorldTransform(transform);', 'transform[3] = m_Anchor;', 'm_Actor.Teleport(transform);', 'physics.SetVelocity(vector.Zero);', 'physics.SetAngularVelocity(vector.Zero);', 'rpl.ForceNodeMovement(previous);', 'if (turn) PoseAt(m_Anchor, m_Forward, transform);') {
 Assert $restore.Contains($needle) "Restore copies the editor's owner teleport (SetTransformOwner): $needle"
}
Assert (!$restore.Contains('Update()') -and !$restore.Contains('SetWorldTransform')) 'Restore never calls Update or SetWorldTransform on a character (falls through the ground, bypasses the controller)'
$poseAt = Get-Body $control 'protected\s+void\s+PoseAt\s*\(\s*vector\s+origin,\s*vector\s+forward,\s*out\s+vector\s+pose\[4\]\s*\)'
Assert ($poseAt.Contains('pose[0] = Vector(forward[2], 0, -forward[0]);') -and $poseAt.Contains('pose[1] = Vector(0, 1, 0);') -and $poseAt.Contains('pose[2] = forward;') -and $poseAt.Contains('pose[3] = origin;')) 'the pose is upright and right-handed (right = up x forward)'
Assert (!$controlCode.Contains('SpotPose')) 'no pose is built from the held spot behind the caller''s back (each caller names its origin and heading)'
Assert ($control.Contains('vector GetForward() { return m_Forward; }')) 'the held heading is readable (the native fixture checks a raw turn never changes it)'

# Game Master moves: the editor's own server path, nothing guessed from distance.
$hook = [regex]::Match($control, '(?s)modded\s+class\s+SCR_EditableCharacterComponent\s*\{(.*)\}\s*$')
Assert $hook.Success 'the editor transform hook is the last class of EUS_UnitControl.c'
$setTransform = Get-Body $hook.Groups[1].Value 'override\s+bool\s+SetTransform\s*\(\s*vector\s+transform\[4\],\s*bool\s+changedByUser\s*=\s*false\s*\)'
Assert ($setTransform.Contains('bool moved = super.SetTransform(transform, changedByUser);') -and $setTransform.Contains('if (moved) EUS_UnitControl.EditorMoved(GetOwner(), transform);') -and $setTransform.Contains('return moved;')) 'SetTransform runs vanilla first and reports only a move the editor accepted'
Assert ($setTransform.IndexOf('super.SetTransform') -lt $setTransform.IndexOf('EditorMoved')) 'vanilla runs before the hook'
# The flag is only passed through: the hook never filters on it, so squad member moves
# (vanilla passes changedByUser false for them) count as Game Master moves too.
Assert ((Count (Get-Code $setTransform) 'changedByUser') -eq 1 -and !$setTransform.Contains('EditorMoved(GetOwner(), transform, changedByUser')) 'squad moves (changedByUser false for members) count as Game Master moves'
$editorMoved = Get-Body $control 'static\s+void\s+EditorMoved\s*\(\s*IEntity\s+owner,\s*vector\s+transform\[4\]\s*\)'
Assert ($editorMoved.Contains('actor.EUS_Script == EUS_Codes.NONE') -and $editorMoved.Contains('controller.EUS_GetControl()') -and $editorMoved.Contains('EUS_Manager.Current()') -and !$editorMoved.Contains('EUS_Manager.Get()')) 'the hook costs one field test for unscripted characters and never creates a manager'
$onMoved = Get-Body $control 'void\s+OnEditorMoved\s*\(\s*vector\s+transform\[4\],\s*float\s+now\s*\)'
Assert ($onMoved.Contains('m_JustCorrected = false;') -and $onMoved.Contains('EUS_AnimationCatalog.NeedsRoom(m_Code - EUS_Codes.ANIMATION) && m_Controller.IsLoitering()) m_Controller.StopLoitering(true);')) 'a Game Master move re-seats the chair pose at the new spot (its chair does not follow him)'
Assert ($onMoved.Contains('m_Anchor = transform[3];') -and $onMoved.Contains('m_SettleUntil = now + SETTLE_SECONDS;') -and $onMoved.Contains('m_LoiterAttempts = 0;') -and $onMoved.Contains('m_NextRoom = now;') -and $onMoved.Contains('Note(string.Format("moved by the Game Master to %1 (%2 move(s) since the last note)", m_Anchor, m_Moves));')) 'a Game Master move sets the spot, settles, gives a pose a fresh allowance and room check, and says so'
# Another mod may call the editable's SetTransform on him repeatedly: the state always
# follows, the line is rate-limited per unit.
$gate = $onMoved.IndexOf('if (now < m_NextMoveNote) return;')
Assert ($gate -gt 0 -and $onMoved.Contains('m_Moves++;') -and $onMoved.Contains('m_NextMoveNote = now + MOVE_NOTE_SECONDS;') -and $onMoved.Contains('m_Moves = 0;') -and (Count $onMoved 'Note(') -eq 1 -and $onMoved.IndexOf('Note(') -gt $gate) 'the Game Master move line is rate-limited per unit (first at once, then at most one per MOVE_NOTE_SECONDS with a count)'
foreach ($state in 'm_Anchor = transform[3];', 'm_SettleUntil = now + SETTLE_SECONDS;', 'm_JustCorrected = false;', 'm_LoiterAttempts = 0;', 'm_NextRoom = now;', 'm_Controller.StopLoitering(true);', 'm_Moves++;') {
 Assert ($onMoved.IndexOf($state) -ge 0 -and $onMoved.IndexOf($state) -lt $gate) "every Game Master move updates the state before the log gate: $state"
}

# Freeze look: claimed, re-sent only when it ended or was taken over.
$track = Get-Body $control 'protected\s+void\s+TrackHead\s*\(\s*notnull\s+array<IEntity>\s+players\s*\)'
Assert ($track.Contains('if (vector.DistanceSq(look.m_vPosition, wanted) < 0.01) return;') -and $track.Contains('look.LookAt(wanted, EUS_Codes.LOOK_PRIORITY, 1.5);') -and (Count $track 'LookAt(') -eq 1) 'Freeze re-claims its look only when it ended or another behaviour took it'
Assert ($track.Contains('wanted = m_Anchor + m_Forward * 10;') -and $track.Contains('wanted[1] = eye[1];') -and $track.Contains('if (vector.DistanceSq(look.m_vPosition, wanted) < 0.01) return;')) 'the look ahead is fixed to the spot at his own eye height; breathing (under 0.1 m) never re-sends it'
Assert ($codes.Contains('static const float LOOK_PRIORITY = 60;')) 'look priority stays above idle glances, danger events and unknown targets, below enemies and the commander'

# Animation: room for the chair pose, pushed poses end, no retry loop on a push.
$pose = Get-Body $control 'protected\s+bool\s+KeepPose\s*\(\s*float\s+now,\s*float\s+drift\s*\)'
Assert ($pose.Contains('if (drift > ANIMATION_TOLERANCE)') -and $pose.Contains('Release(string.Format("pushed %1 m off its spot while animating (no room for this pose here)"') -and $pose.Contains('m_NextRoom = now + ROOM_SECONDS;')) 'a pushed pose ends at once (no retries), and a pose that needs room is re-checked every ROOM_SECONDS'
Assert ($pose.Contains('string blocked = PoseRoom(m_Code - EUS_Codes.ANIMATION, m_Anchor, m_Forward);')) 'the periodic re-check of a seated pose looks at the held spot (its box covers his spot and the seat), not where root motion carried him'
$room = Get-Body $control 'protected\s+string\s+PoseRoom\s*\(\s*int\s+index,\s*vector\s+origin,\s*vector\s+forward\s*\)'
Assert ($room.Contains('if (!EUS_AnimationCatalog.NeedsRoom(index)) return string.Empty;') -and $room.Contains('PoseAt(origin, forward, pose);') -and $room.Contains('Blocked(pose, Vector(-0.45, 0.3, -0.2), Vector(0.45, 1.3, 1.05), obstacle)') -and $room.Contains('no room for')) 'the chair pose needs a clear 0.9 x 1.25 x 1 m box at the origin and heading it is asked about'
$needsRoom = Get-Body $codes 'static\s+bool\s+NeedsRoom\s*\(\s*int\s+index\s*\)'
Assert ($needsRoom.Trim() -eq 'return Item(index) == SCR_ELoiterItemID.CHAIR;') 'only the chair pose (the preset chair item) needs room'
# RoomFor, Bind and every loiter attempt check the same place: where the pose is issued.
$roomHere = Get-Body $control 'protected\s+string\s+RoomHere\s*\(\s*int\s+index\s*\)'
Assert ($roomHere.Trim() -eq 'return PoseRoom(index, m_Actor.GetOrigin(), EUS_Codes.Forward(m_Actor));') 'room is checked where the unit stands and faces now (a held Freeze or Hold may stand off its spot and face elsewhere)'
Assert ([regex]::Matches($controlCode, '\bPoseRoom\s*\(').Count -eq 3 -and [regex]::Matches($controlCode, '\bRoomHere\s*\(').Count -eq 4) 'PoseRoom is asked only by RoomHere and the seated re-check; RoomHere by RoomFor, Bind and StartLoiter'
$bind = Get-Body $control 'bool\s+Bind\s*\(\s*SCR_ChimeraCharacter\s+actor,\s*int\s+code,\s*float\s+now,\s*out\s+string\s+reason\s*\)'
Assert ($bind.Contains('reason = RoomHere(code - EUS_Codes.ANIMATION);') -and $bind.IndexOf('m_Actor = actor;') -lt $bind.IndexOf('RoomHere(') -and $bind.IndexOf('RoomHere(') -lt $bind.IndexOf('AddCharacterSetting(m_Speed')) 'a pose without room is refused before any native state changes, checked exactly as RoomFor checks it'
Assert ($bind.Contains('if (EUS_Codes.IsAnimation(code) && !StartLoiter(now))') -and $bind.Contains('m_SettleUntil = now + SETTLE_SECONDS;')) 'the bind settles and stops when the first loiter is refused'
$start = Get-Body $control 'protected\s+bool\s+StartLoiter\s*\(\s*float\s+now\s*\)'
Assert ($start.Contains('string blocked = RoomHere(index);') -and $start.IndexOf('RoomHere(index)') -lt $start.IndexOf('m_Actor.GetWorldTransform(transform);') -and $start.IndexOf('m_Actor.GetWorldTransform(transform);') -lt $start.IndexOf('m_Controller.StartLoitering(') -and $start.Contains('Release(blocked);')) 'every loiter attempt checks room first, where the loiter is issued (his current transform)'
$manager = Read-Text (Join-Path $scripts 'EUS_Manager.c')
$apply = Get-Body $manager 'bool\s+ApplyUnit\s*\(\s*SCR_ChimeraCharacter\s+actor,\s*int\s+code,\s*EUS_Report\s+report\s*\)'
Assert ($apply.Contains('if (existing) reason = existing.RoomFor(code);') -and $apply.IndexOf('existing.RoomFor(code)') -lt $apply.IndexOf('existing.Release("replaced by "')) 'a pose without room is refused before the running script is replaced (the soldier keeps it)'
Assert ((Count $apply 'PrintFormat(') -eq 1 -and [regex]::Matches((Get-Code $manager), '\bPrint(Format)?\s*\(').Count -eq 1) 'the manager keeps its single refused log'
$roomFor = Get-Body $control 'string\s+RoomFor\s*\(\s*int\s+code\s*\)'
Assert ($roomFor.Contains('if (!m_Bound || !m_Actor || !EUS_Codes.IsAnimation(code)) return string.Empty;') -and $roomFor.Contains('return RoomHere(code - EUS_Codes.ANIMATION);')) 'RoomFor checks a running unit where Bind will check him next, so the two always agree'
$keep = Get-Body $control 'protected\s+bool\s+KeepLoiter\s*\(\s*float\s+now\s*\)'
Assert ($keep.Contains('return StartLoiter(now);') -and $keep.Contains('if (m_LoiterAttempts >= LOITER_ATTEMPTS)')) 'retries stay bounded and a refused retry releases'
$blocked = Get-Body $control 'protected\s+bool\s+Blocked\s*\(\s*vector\s+pose\[4\],\s*vector\s+mins,\s*vector\s+maxs,\s*out\s+IEntity\s+obstacle\s*\)'
foreach ($needle in 'TraceOBB trace = new TraceOBB();', 'trace.Flags = TraceFlags.ENTS;', 'trace.LayerMask = EPhysicsLayerPresets.Character;', 'trace.Exclude = m_Actor;', 'm_Actor.GetWorld().TracePosition(trace, IgnoreCharacters);', 'obstacle = trace.TraceEnt;', 'return obstacle != null;') {
 Assert $blocked.Contains($needle) "Blocked is one OBB position trace against what characters collide with: $needle"
}
Assert (![regex]::IsMatch((Get-Code $blocked), 'TracePosition\([^;]*\)\s*[<>=!]')) 'Blocked decides by the hit entity, never by the sign of TracePosition (a touching hit can return 0)'
Assert ((Get-Body $control 'protected\s+bool\s+IgnoreCharacters\s*\(\s*notnull\s+IEntity\s+e,\s*vector\s+start\s*=\s*"0 0 0",\s*vector\s+dir\s*=\s*"0 0 0"\s*\)').Trim() -eq 'return !ChimeraCharacter.Cast(e.GetRootParent());') 'the trace ignores characters and their gear (vanilla TraceFilterCallback signature)'
$traceSites = [regex]::Matches($controlCode, '\bBlocked\s*\(\s*pose').Count
Assert ($traceSites -eq 2) "traces run only on a needed correction and for the chair pose (found $traceSites call sites)"

# Logs: three call sites in the unit control, every Note caller bounded.
Assert ([regex]::Matches($controlCode, '\bPrint(Format)?\s*\(').Count -eq 3) 'EUS_UnitControl.c logs only at bind, through Note and at release'
Assert ((Get-Body $control 'protected\s+void\s+Note\s*\(\s*string\s+text\s*\)').Trim() -eq 'PrintFormat("[EUS] unit=%1 %2", m_Actor, text);') 'Note keeps the [EUS] unit= line format'
$noteCallers = [regex]::Matches($controlCode, '\bNote\s*\(').Count - 1
Assert ($noteCallers -eq 4) "Note callers: Game Master move, loiter attempt, occupied/dropped spot, held summary (found $noteCallers)"
$release = Get-Body $control 'void\s+Release\s*\(\s*string\s+reason,\s*bool\s+fast\s*=\s*false\s*\)'
Assert ($release.Contains('PrintFormat("[EUS] released unit=%1 script=''%2'' reason=''%3''%4", m_Actor, EUS_Codes.Describe(m_Code), reason, held);') -and $release.Contains('if (reason.IsEmpty()) return;')) 'release keeps its line and adds how often he was put back'
Assert ($release.IndexOf('if (reason.IsEmpty()) return;') -gt $release.IndexOf('m_Actor.EUS_SetScript(EUS_Codes.NONE);')) 'a silent release (destructor) still undoes every native effect first'

# Enforce gotchas in the touched sources and the fixture.
$fixturePath = Join-Path $repo 'tests/EUS_FreezeLeaderGameplay.c'
foreach ($path in @($controlPath, $codesPath, (Join-Path $scripts 'EUS_Manager.c'), (Join-Path $scripts 'EUS_ContextActions.c'), $fixturePath)) {
 $text = Read-Text $path
 $code = Get-Code $text
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<|static\s+const\s+vector\b')) "static initializer in $path (use EXPBG_LazyStatics_<Class>)"
 Assert (![regex]::IsMatch($code, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "non-ASCII byte or CR in $path"
 # Enforce rejects a second declaration of a name anywhere in the same method.
 foreach ($method in [regex]::Matches($code, '(?m)^\s*(?:(?:static|protected|override|private)\s+)*(?:void|bool|int|float|string|vector|IEntity|\w+)\s+(\w+)\s*\([^;{]*\)\s*\{')) {
  $open = $method.Index + $method.Length - 1; $depth = 0; $end = $open
  for ($i = $open; $i -lt $code.Length; $i++) { if ($code[$i] -eq '{') { $depth++ } elseif ($code[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } } }
  $body = $code.Substring($open, $end - $open)
  $declared = @([regex]::Matches($body, '(?m)(?:^\s*|[;{(]\s*|\bforeach\s*\(\s*)(?:ref\s+)?(?:int|float|bool|string|vector|IEntity|[A-Z]\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:\[\d+\])?\s*(?:=|;|:)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin 'return', 'new' })
  $dupes = @($declared | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Assert ($dupes.Count -eq 0) "variable declared twice in $($method.Groups[1].Value) of ${path}: $($dupes -join ', ')"
 }
}

Assert (@([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'this guard is ASCII with LF line endings'

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
$expect = '\[EUS FREEZE TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EUS_FreezeLeaderGameplay.c -TimeoutSeconds 420 -ExpectResult '$expect' -OrchestratorSlotGranted")) 'freeze fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'freeze fixture must keep the runner driver class names'
Assert ($fixture.Contains('PrintFormat("[EUS FREEZE TEST RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);') -and $fixture.Contains('Finish("completed");')) 'freeze fixture must print the RESULT line its regex expects'
foreach ($needle in 'Manager.ApplyUnit(Leader, EUS_Codes.FREEZE, Report)', 'Group.GetLeaderEntity()', 'Group.AddWaypoint(Waypoint);', 'Group.RemoveWaypoint(Waypoint);', 'editable.SetTransform(transform, true)', 'actor.Teleport(transform);',
 'Manager.ApplyUnit(Seated, EUS_Codes.ANIMATION + 1, Report)', 'Manager.ApplyUnit(Blocked, EUS_Codes.ANIMATION + 1, Report)', 'Report.LastReason.Contains("no room")', 'Pose.GetEndReason().Contains("pushed")', 'LeaderMax <= 0.5', 'EditorMove(Seated, ChairTarget)', 'Check(Loitering(Seated), "the chair pose sits down again at the new spot");',
 'LeaderForward = Frozen.GetForward();', 'Check(TurnRunMax <= TURN_SAMPLES,', 'Check(ordered <= LOOP_CORRECTIONS,', 'Check(idle <= LOOP_CORRECTIONS,', 'RawTurn(Leader);', 'Check(turnedBack,', 'vector.Dot(Frozen.GetForward(), HeldForward) > 0.999',
 'static const float TABLE_SECONDS = 18;', 'static const int TABLE_CORRECTIONS = 2;', 'int total = Desk.GetCorrections() - CorrectionsBefore;', 'Check(total <= TABLE_CORRECTIONS,', 'Check(CorrectionsHalf >= 0 && late == 0,') {
 Assert $fixture.Contains($needle) "freeze fixture must drive: $needle"
}
# The fixture asserts what the code guarantees (no stricter turn than the code corrects),
# reads the limit from the code, and prints the largest turn for the native run.
$fixtureCode = Get-Code $fixture
Assert ($fixtureCode.Contains('float TurnLimit() { return Math.Acos(EUS_UnitControl.FREEZE_TURN_DOT) * Math.RAD2DEG; }') -and $fixtureCode.Contains('if (turn > TurnLimit()) TurnRun++;')) 'the fixture''s turn check uses the code''s own Freeze turn limit'
Assert (![regex]::IsMatch($fixtureCode, 'LeaderTurn\s*<')) 'the fixture never asserts a tighter turn than the code corrects (the largest turn is printed, not checked)'
Assert ([regex]::Matches($fixture, 'leaderTurn=%2 turnSamples=%3 limit=%4').Count -eq 2) 'the order and idle lines print the largest turn, the longest turn run and the limit'
# The table window catches a slow loop: 18 s with a mid-point count, not a 2 s glance.
$tableWindow = [regex]::Match($fixtureCode, '(?s)if \(Phase == 13\)\s*\{(.*?)Advance\(14\);')
Assert ($tableWindow.Success -and $tableWindow.Groups[1].Value.Contains('if (CorrectionsHalf < 0 && Now() - TableAt >= TABLE_SECONDS * 0.5) CorrectionsHalf = Desk.GetCorrections();') -and $tableWindow.Groups[1].Value.Contains('if (Now() - TableAt < TABLE_SECONDS) return;')) 'the table window samples corrections at its midpoint and ends TABLE_SECONDS after the table appears'
Assert ($fixture -match 'Resource\s+squad\s*=\s*Resource\.Load\(SQUAD\);' -and $fixture -match 'Resource\s+tableResource\s*=\s*Resource\.Load\(TABLE\);' -and $fixture -match 'Resource\s+waypointResource\s*=\s*Resource\.Load\(MOVE\);') 'freeze fixture must keep Resource.Load results in a local'
'PASS: Freeze and Hold put the soldier back like the editor''s owner teleport and keep a new spot only for a drop, a moving platform or an object on it, only Game Master moves (editor SetTransform) set a new spot, Freeze keeps its look, the chair pose needs room where it is issued and a pushed pose ends, logs bounded; freeze fixture wired.'
