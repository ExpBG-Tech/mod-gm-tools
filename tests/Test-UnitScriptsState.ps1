#requires -Version 7.0
# Portable guard for EXPBG Unit Scripts persistence, continuous animations and Freeze/Hold
# hold-until-release (production 2026-10-08: frozen and held units broke out, smoke and
# stand-at-ease poses re-started with a gap and drifted, "could not be kept after 4
# attempts" during a cache pause, every CDF load reset all scripts, and a Game Master move
# of a unit in an ACE Captives helper compartment threw in vanilla SetTransform).
# Checks the source invariants: the end-reason enum and the Unit Caching seams
# (SetCachePaused, ScriptsOnly, GetEnd), damage ends only animations, unconsciousness and
# ragdoll pause instead of releasing, poses loop inside one loiter command and every
# re-issue starts on the held spot, the versioned EUS_UnitState API and its restore queue,
# the native save registration, the Game Master texts, Enforce gotchas, and the wiring of
# tests/EUS_StateLoopGameplay.c. Source pins protect the implementation; the native
# fixtures carry the behaviour. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Join-Path $repo 'addon/unit-scripts'
$scripts = Join-Path $module 'Scripts/Game/EXPUS'
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
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}

$controlPath = Join-Path $scripts 'EUS_UnitControl.c'
$managerPath = Join-Path $scripts 'EUS_Manager.c'
$statePath = Join-Path $scripts 'EUS_State.c'
$persistencePath = Join-Path $scripts 'EUS_Persistence.c'
$loopPath = Join-Path $scripts 'EUS_PoseLoop.c'
$codesPath = Join-Path $scripts 'EUS_Codes.c'
$rplPath = Join-Path $scripts 'EUS_Replication.c'
$fixturePath = Join-Path $repo 'tests/EUS_StateLoopGameplay.c'
$control = Read-Text $controlPath
$manager = Read-Text $managerPath
$state = Read-Text $statePath
$persistence = Read-Text $persistencePath
$loop = Read-Text $loopPath
$controlCode = Get-Code $control

# 1. End reasons: stable values for Unit Caching and bridges (append only).
$enum = Get-Body $control 'enum\s+EUS_EEndReason'
$names = @([regex]::Matches((Get-Code $enum), '(\w+)\s*=\s*(\d+)') | ForEach-Object { "$($_.Groups[1].Value)=$($_.Groups[2].Value)" })
Assert (($names -join ',') -ceq 'NONE=0,GAME_MASTER=1,REPLACED=2,DAMAGE=3,DEATH=4,POSSESSED=5,VEHICLE=6,REMOVED=7,POSE_PUSHED=8,POSE_NO_ROOM=9,LOITER_FAILED=10,SILENT=11') "EUS_EEndReason values are stable: $($names -join ',')"
foreach ($signature in 'EUS_EEndReason GetEnd() { return m_End; }', 'bool IsCachePaused() { return m_CachePaused; }', 'void SetCachePaused(bool paused)', 'void ReleaseBy(EUS_EEndReason end, string reason, bool fast = false)', 'static string Readiness(SCR_ChimeraCharacter actor)', 'static void PlaceAt(notnull SCR_ChimeraCharacter actor, vector anchor, vector forward)', 'string GetEndReason() { return m_EndReason; }') {
 Assert $control.Contains($signature) "Unit Scripts control API: $signature"
}
$releaseBy = Get-Body $control 'void\s+ReleaseBy\s*\(\s*EUS_EEndReason\s+end,\s*string\s+reason,\s*bool\s+fast\s*=\s*false\s*\)'
Assert ($releaseBy.Contains('if (m_End == EUS_EEndReason.NONE) m_End = end;') -and $releaseBy.Contains('Release(reason, fast);')) 'ReleaseBy keeps the first end code and releases'
$release = Get-Body $control 'void\s+Release\s*\(\s*string\s+reason,\s*bool\s+fast\s*=\s*false\s*\)'
Assert ($release.Contains('if (reason.IsEmpty()) m_End = EUS_EEndReason.SILENT;') -and $release.Contains('else m_End = EUS_EEndReason.GAME_MASTER;')) 'a plain Release is a Game Master release, or SILENT without a reason'
Assert (!$control.Contains('m_End = EUS_EEndReason.LOITER_FAILED;') -and $control.Contains('m_NextLoiter = now + LOITER_BACKOFF_SECONDS;')) 'a pose entry the graph does not take backs off and retries; the script is never dropped for it'
foreach ($pair in @(@('m_End = EUS_EEndReason.POSE_PUSHED;', 'Release(string.Format("pushed %1 m off its spot'), @('m_End = EUS_EEndReason.DAMAGE;', 'Release("the unit took damage", true);'))) {
 $at = $control.IndexOf($pair[0])
 Assert ($at -ge 0 -and $control.IndexOf($pair[1], $at) -gt $at -and $control.IndexOf($pair[1], $at) - $at -lt 200) "end code set right before its release: $($pair[0])"
}
Assert ((Count $control 'm_End = EUS_EEndReason.POSE_NO_ROOM;') -eq 2) 'both no-room releases (held pose, new entry) carry POSE_NO_ROOM'
Assert ($manager.Contains('existing.ReleaseBy(EUS_EEndReason.GAME_MASTER, "released by the Game Master");') -and $manager.Contains('existing.ReleaseBy(EUS_EEndReason.REPLACED, "replaced by " + EUS_Codes.Describe(code));') -and $manager.Contains('control.ReleaseBy(EUS_EEndReason.REMOVED, ')) 'manager releases carry their end codes'
Assert ($control.Contains('possessed.ReleaseBy(EUS_EEndReason.POSSESSED, "a player took control of the unit");')) 'possession carries POSSESSED'

# 2. Freeze and Hold hold until the Game Master releases them.
$bind = Get-Body $control 'bool\s+Bind\s*\(\s*SCR_ChimeraCharacter\s+actor,\s*int\s+code,\s*float\s+now,\s*out\s+string\s+reason\s*\)'
Assert ($bind.Contains('if (m_Damage && EUS_Codes.IsAnimation(code)) m_Damage.GetOnDamage().Insert(OnDamage);') -and (Count $bind 'GetOnDamage().Insert') -eq 1) 'only an animation subscribes to damage'
$onDamage = Get-Body $control 'void\s+OnDamage\s*\(\s*BaseDamageContext\s+damageContext\s*\)'
Assert ($onDamage.Contains('!EUS_Codes.IsAnimation(m_Code)')) 'damage never ends Freeze or Hold'
$lost = Get-Body $control 'protected\s+string\s+Lost\s*\(\s*out\s+EUS_EEndReason\s+end\s*\)'
Assert (!$lost.Contains('GetLifeState') -and $lost.Contains('m_Controller.IsDead()') -and $lost.Contains('m_Controller.IsPlayerControlled()') -and $lost.Contains('m_Actor.IsInVehicle()') -and $lost.Contains('control.GetAIAgent() != m_Agent || m_Agent.GetControlledEntity() != m_Actor')) 'only death, possession, a vehicle or compartment and a lost agent end a script; unconsciousness does not'
foreach ($code in 'EUS_EEndReason.DEATH', 'EUS_EEndReason.POSSESSED', 'EUS_EEndReason.VEHICLE', 'EUS_EEndReason.REMOVED') { Assert $lost.Contains("end = $code;") "Lost names its end code: $code" }
Assert ($lost.Contains('"the unit entered a vehicle or compartment (" + Compartment() + ")"')) 'a compartment release names the vehicle or helper (ACE captive helpers too)'
$down = Get-Body $control 'protected\s+bool\s+Down\s*\(\s*\)'
Assert ($down.Contains('m_Controller.GetLifeState() != ECharacterLifeState.ALIVE') -and $down.Contains('animation.IsRagdollActive()')) 'unconscious or ragdolled is down'
$tick = Get-Body $control 'bool\s+Tick\s*\(\s*float\s+now,\s*notnull\s+array<IEntity>\s+players\s*\)'
$order = @('string lost = Lost(end);', 'ReleaseBy(end, lost);', 'if (Paused())', 'm_WasPaused = true;', 'if (m_WasPaused)', 'Unpause(now);', 'if (Down())', 'if (m_SettleUntil >= 0)', 'HoldSpot(now)', 'KeepLoiter(now)')
$from = 0
foreach ($step in $order) { $at = $tick.IndexOf($step, $from); Assert ($at -ge 0) "Tick lost or reordered: $step"; $from = $at + $step.Length }
$filter = Get-Body $control 'SCR_AICombatMoveRequestBase\s+Filter\s*\(\s*notnull\s+SCR_AICombatMoveRequestBase\s+request\s*\)'
Assert ($filter.Contains('string lost = Lost(end);') -and $filter.Contains('ReleaseBy(end, lost);')) 'the combat move filter releases with the same end codes'

# 3. Unit Caching seams.
$paused = Get-Body $control 'protected\s+bool\s+Paused\s*\(\s*\)'
Assert ($paused.Contains('m_CachePaused') -and $paused.Contains('m_Actor.EBG_IsSimulationCached()')) 'paused by SetCachePaused or a Simulation-cached actor'
$set = Get-Body $control 'void\s+SetCachePaused\s*\(\s*bool\s+paused\s*\)'
Assert ($set.Contains('if (m_CachePaused == paused)') -and $set.Contains('Unpause(EUS_Codes.WorldSeconds());')) 'SetCachePaused is idempotent and un-pauses at once'
$unpause = Get-Body $control 'protected\s+void\s+Unpause\s*\(\s*float\s+now\s*\)'
foreach ($needle in 'm_LoiterAttempts = 0;', 'm_PendingSince = -1;', 'm_NextLoiter = now;', 'm_JustCorrected = false;', 'StartLoiter(now);', 'm_Controller.IsLoitering()', 'input.m_iLoiteringType >= 0') { Assert $unpause.Contains($needle) "un-pause gives a fresh allowance and restarts only an ended pose: $needle" }
$scriptsOnly = Get-Body $manager 'static\s+bool\s+ScriptsOnly\s*\(\s*SCR_AIGroup\s+group\s*\)'
Assert ($scriptsOnly.Contains('return group.EUS_Discipline == EUS_Codes.DISCIPLINE_OFF && group.EUS_Scripted > 0;') -and $scriptsOnly.Contains('!Current()')) 'ScriptsOnly: scripted members and no night discipline, same inputs as Reserves'

# 4. Continuous poses.
$keep = Get-Body $control 'protected\s+bool\s+KeepLoiter\s*\(\s*float\s+now\s*\)'
$playing = [regex]::Match($keep, '(?s)if \(m_Controller\.IsLoitering\(\)\)\s*\{(.*?)\n\s*\}')
Assert ($playing.Success -and $playing.Groups[1].Value.Contains('m_LoiterAttempts = 0;') -and !$control.Contains('m_LoiterSince')) 'a pose seen playing never counts as a failed entry (only entries that never start count)'
$start = Get-Body $control 'protected\s+bool\s+StartLoiter\s*\(\s*float\s+now\s*\)'
$snap = $start.IndexOf('> POSE_SNAP || vector.Dot(EUS_Codes.Forward(m_Actor), m_Forward) < POSE_TURN_DOT) Restore(true);')
Assert ($snap -gt 0 -and $snap -lt $start.IndexOf('string blocked = RoomHere(index);') -and $start.Contains('m_Controller.IsChangingStance() || m_Controller.IsFalling()')) 'every entry starts on the held spot and heading (no drift over cycles), never mid-fall'
Assert ($control.Contains('static const float POSE_SNAP = 0.05;') -and $control.Contains('static const float POSE_TURN_DOT = 0.995;')) 'pose snap limits'
Assert ($loop -match 'modded\s+class\s+SCR_CharacterCommandLoiter') 'the loiter command loops Unit Scripts poses'
$pre = Get-Body $loop 'override\s+void\s+PrePhysUpdate\s*\(\s*float\s+pDt\s*\)'
Assert ($pre.Contains('if (m_eState == ELoiterCommandState.LOITERING)') -and $pre.Contains('if (!m_pCharAnimComponent.IsPrimaryTag(m_pStaticTable.m_IsLoiteringTag)) EUS_Cycle();') -and $pre.Trim().EndsWith('super.PrePhysUpdate(pDt);')) 'only the sequence''s own end (tag drop in LOITERING) loops, then vanilla runs (fall, platform, stop requests unchanged)'
$cycle = Get-Body $loop 'protected\s+void\s+EUS_Cycle\s*\(\s*\)'
Assert ($cycle.Contains('SwitchState(ELoiterCommandState.LOITERING);') -and $cycle.Contains('character.EUS_PoseCycles++;') -and $cycle.IndexOf('EUS_LoopedCharacter()') -lt $cycle.IndexOf('SwitchState')) 'a looped pose re-issues its gesture inside the same command'
$looped = Get-Body $loop 'protected\s+SCR_ChimeraCharacter\s+EUS_LoopedCharacter\s*\(\s*\)'
Assert ($looped.Contains('EUS_Codes.IsAnimation(character.EUS_Script)') -and $looped.Contains('m_pScrInputCtx.m_iLoiteringType != EUS_AnimationCatalog.Type(character.EUS_Script - EUS_Codes.ANIMATION)')) 'only the pose the replicated Unit Scripts animation asked for loops (proxies too)'
$wait = Get-Body $loop 'protected\s+void\s+EUS_Wait\s*\(\s*float\s+pDt\s*\)'
Assert ($loop.Contains('static const int EUS_REISSUES = 3;') -and $wait.Contains('m_iEUS_Reissues < EUS_REISSUES') -and $wait.Contains('if (m_rplComponent && m_rplComponent.IsOwner()) m_pCommandHandler.StopLoitering(false);')) 'an untaken gesture is re-sent a bounded number of times, then the owner ends the command (never an endless pose-less loiter)'
Assert ((Read-Text $rplPath).Contains(' int EUS_PoseCycles;') -and !(Read-Text $rplPath).Contains('[RplProp(), NonSerialized()] int EUS_PoseCycles')) 'pose cycles are a local diagnostic, not replicated'

# 5. Versioned state API.
Assert ($state.Contains('static const int VERSION = 1;') -and $state.Contains('static const float RESTORE_RADIUS = 3;')) 'state version and restore radius'
$write = Get-Body $state 'bool\s+Write\s*\(\s*SaveContext\s+context\s*\)'
foreach ($key in '"version", VERSION', '"code", Code', '"ax", Anchor[0]', '"ay", Anchor[1]', '"az", Anchor[2]', '"fx", Forward[0]', '"fz", Forward[2]') { Assert $write.Contains("WriteValue($key)") "saved key stays: $key" }
$read = Get-Body $state 'bool\s+Read\s*\(\s*LoadContext\s+context\s*\)'
Assert ($read.Contains('version != VERSION') -and $read.Contains('Forward = Vector(fx, 0, fz);')) 'reading refuses another version and keeps the heading horizontal'
$validate = Get-Body $state 'string\s+Validate\s*\(\s*\)'
Assert ($validate.Contains('!EUS_Codes.IsScript(Code)') -and $validate.Contains('Anchor[axis] > -WORLD_LIMIT && Anchor[axis] < WORLD_LIMIT') -and $validate.Contains('length > 0.81 && length < 1.21')) 'validation: known script, a world position (NaN fails), a horizontal unit heading'
$capture = Get-Body $state 'static\s+EUS_UnitState\s+Capture\s*\(\s*IEntity\s+entity\s*\)'
Assert ($capture.Contains('EUS_Manager.Current()') -and !$capture.Contains('EUS_Manager.Get()') -and $capture.Contains('control.GetAnchor()') -and $capture.Contains('control.GetForward()')) 'capture reads the bound control and never creates a manager'
$restore = Get-Body $state 'bool\s+Restore\s*\(\s*IEntity\s+entity,\s*string\s+source,\s*out\s+string\s+reason\s*\)'
Assert ($restore.IndexOf('reason = Validate();') -lt $restore.IndexOf('manager.QueueRestore(actor, this, source, reason)')) 'restore validates, then queues with the manager'
foreach ($needle in 'static const int RESTORE_BUDGET = 4;', 'static const int RESTORE_LIMIT = 512;', 'static const float RESTORE_WAIT = 30;', 'TickRestores(now);', 'if (m_Units.IsEmpty() && m_Groups.IsEmpty() && m_Restores.IsEmpty()) Stop();') { Assert $manager.Contains($needle) "restore queue: $needle" }
$try = Get-Body $manager 'protected\s+int\s+TryRestore\s*\(\s*EUS_PendingRestore\s+pending,\s*float\s+now\s*\)'
Assert ($try.IndexOf('EUS_UnitControl.Readiness(actor)') -lt $try.IndexOf('ApplyUnit(actor, state.Code, report)') -and $try.Contains('if (now < pending.Deadline)') -and $try.Contains('<= EUS_UnitState.RESTORE_RADIUS') -and $try.IndexOf('EUS_UnitControl.PlaceAt(') -lt $try.IndexOf('ApplyUnit(')) 'a restore waits (bounded) for the AI, puts him on the saved spot when near it, then binds like a Game Master request (no refused-log spam while waiting)'
$tickRestores = Get-Body $manager 'protected\s+void\s+TickRestores\s*\(\s*float\s+now\s*\)'
Assert ((Count $tickRestores 'EUS_UnitControl.Log(') -eq 1 -and $tickRestores.Contains('for (int step = 0; step < budget; step++)')) 'one summary per drained batch, budgeted per pump'
$queue = Get-Body $manager 'bool\s+QueueRestore\s*\(\s*SCR_ChimeraCharacter\s+actor,\s*EUS_UnitState\s+state,\s*string\s+source,\s*out\s+string\s+reason\s*\)'
Assert ($queue.Contains('m_Restores.Count() >= RESTORE_LIMIT') -and $queue.Contains('if (queued.Actor != actor) continue;') -and $queue.Contains('Wake();')) 'the queue is bounded, one row per character, and wakes the pump'

# 6. Native save registration.
foreach ($needle in 'class EUS_ScriptPersistenceState : PersistentState', 'class EUS_ScriptPersistenceSerializer : ScriptedStateSerializer', 'return EDeserializeFailHandling.IGNORE;', 'return ESerializeResult.DEFAULT;', 'system.WhenAvailable(pending.Id, task, AVAILABLE_WAIT);', 'row.State.Restore(entity, "native save", reason)', 'count > EUS_Manager.RESTORE_LIMIT') {
 Assert $persistence.Contains($needle) "native persistence: $needle"
}
$deserialize = Get-Body $persistence 'override\s+protected\s+bool\s+Deserialize\s*\(\s*notnull\s+Managed\s+instance,\s*notnull\s+LoadContext\s+context\s*\)'
Assert ($deserialize.IndexOf('ids.Contains(row.Id)') -lt $deserialize.IndexOf('WhenAvailable')) 'the whole native record is validated before any restore is scheduled'
$confPath = Join-Path $module 'Configs/Systems/Persistence/GameMode/GameMaster.conf'
$conf = Read-Text $confPath
Assert ($conf.Contains('Serializer EUS_ScriptPersistenceSerializer') -and $conf.Contains('EUS_ScriptPersistenceState "{') -and $conf.Contains('Collection "{6586D01E488CA578}"') -and $conf.Contains('PersistentStates + {')) 'the native unit script state and serializer are registered'
Assert ((Read-Text "$confPath.meta").Contains('Name "{B76E7F1AF7A5D00C}Configs/Systems/Persistence/GameMode/GameMaster.conf"')) 'GameMaster.conf keeps the vanilla identity'
$ids = @([regex]::Matches($conf, '"\{([A-F0-9]{16})\}"') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -ne '6586D01E488CA578' })
Assert ($ids.Count -eq 3) 'three own GUIDs in the persistence config'
foreach ($other in Get-ChildItem -LiteralPath (Join-Path $repo 'addon') -Recurse -File -Include '*.conf', '*.et', '*.meta' | Where-Object { $_.FullName -ne $confPath }) {
 $text = Read-Text $other.FullName
 foreach ($id in $ids) { Assert (!$text.Contains($id)) "GUID $id of the unit script persistence config is reused in $($other.FullName)" }
}
Assert ((Read-Text (Join-Path $repo 'tools/pack.json')).Contains('"Configs/Systems/Persistence/GameMode/GameMaster.conf"')) 'GameMaster.conf is a merged config'

# 7. Game Master texts: scripts saved, night discipline mission-only.
foreach ($needle in 'static const string SAVED = " Saved with the mission (native saves; CDF saves with EXPBG CDF Compat).";', 'static const string NOT_SAVED = " Mission-only; not saved.";', 'return text + "." + Persistence;') { Assert $manager.Contains($needle) "report text: $needle" }
Assert ($manager.Contains('Report(playerId, "EXPBG Unit script: " + EUS_Codes.Describe(code), EUS_Report.SAVED);') -and $manager.Contains('Report(playerId, "EXPBG Night discipline: " + EUS_Codes.DescribeDiscipline(mode), EUS_Report.NOT_SAVED);') -and !$manager.Contains('". Mission-only; not saved."')) 'unit script replies say saved, night discipline replies say mission-only'
$edit = Read-Text (Join-Path $module 'Configs/Editor/AttributeLists/Edit.conf')
Assert ((Count $edit 'Mission-only; not saved.') -eq 1 -and $edit.Contains('Terror tactics') -and (Count $edit 'Saved with the mission (native saves; CDF saves with EXPBG CDF Compat).') -eq 2) 'unit and squad script descriptions say saved; only night discipline stays mission-only'
Assert ($edit.Contains('Hold and Freeze stay until set back to Normal AI, also through damage and unconsciousness') -and !$edit.Contains('Hold, Freeze and animations end when the unit takes damage')) 'the description matches hold-until-release'

# 8. Enforce gotchas in every Unit Scripts source and the fixture.
$sources = @(Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File | ForEach-Object FullName) + @($fixturePath)
$newFiles = @($statePath, $persistencePath, $loopPath, $fixturePath)
foreach ($path in $sources) {
 $text = Read-Text $path
 $code = Get-Code $text
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external|native)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<|static\s+const\s+vector\b')) "static initializer in $path"
 Assert (!$code.Contains('Math.RandomFloat(')) "Math.RandomFloat in $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "non-ASCII byte or CR in $path"
 foreach ($method in [regex]::Matches($code, '(?m)^\s*(?:(?:static|protected|override|private)\s+)*(?:void|bool|int|float|string|vector|IEntity|\w+)\s+(\w+)\s*\([^;{]*\)\s*\{')) {
  $open = $method.Index + $method.Length - 1; $depth = 0; $end = $open
  for ($i = $open; $i -lt $code.Length; $i++) { if ($code[$i] -eq '{') { $depth++ } elseif ($code[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } } }
  $body = $code.Substring($open, $end - $open)
  $declared = @([regex]::Matches($body, '(?m)(?:^\s*|[;{(]\s*|\bforeach\s*\(\s*)(?:ref\s+)?(?:int|float|bool|string|vector|IEntity|[A-Z]\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:\[\d+\])?\s*(?:=|;|:)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin 'return', 'new' })
  $dupes = @($declared | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Assert ($dupes.Count -eq 0) "variable declared twice in $($method.Groups[1].Value) of ${path}: $($dupes -join ', ')"
 }
}
foreach ($path in $newFiles) {
 $code = Get-Code (Read-Text $path)
 Assert (![regex]::IsMatch($code, '\bif\s*\([^\r\n]*\)\s*return\b')) "each return on its own line in $path (no 'if (x) return y;')"
}
# No vanilla type name reused as a member, method or local in the new sources.
foreach ($path in $newFiles + @($controlPath, $managerPath)) {
 $code = Get-Code (Read-Text $path)
 Assert (![regex]::IsMatch($code, '\b(?:Vehicle|Physics|Resource|Faction|Animation|Character)\s*=')) "a name shadows a vanilla type in $path"
}

Assert (@([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'this guard is ASCII with LF line endings'

# 9. Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
$expect = '\[EUS STATE TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EUS_StateLoopGameplay.c -TimeoutSeconds 480 -ExpectResult '$expect' -OrchestratorSlotGranted")) 'state fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'state fixture keeps the runner driver class names'
Assert ($fixture.Contains('PrintFormat("[EUS STATE TEST RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);') -and $fixture.Contains('Finish("completed");')) 'state fixture prints the RESULT line its regex expects'
foreach ($needle in 'Manager.ApplyUnit(Smoker, EUS_Codes.ANIMATION + 2, Report)', 'Manager.ApplyUnit(Stander, EUS_Codes.ANIMATION + 3, Report)', 'SmokerGaps == 0', 'StanderGaps == 0', 'Smoker.EUS_PoseCycles', 'Hit(Frozen);', 'Hit(Holder);', 'FrozenControl.GetEnd() == EUS_EEndReason.NONE', 'StanderControl.SetCachePaused(true);', 'StanderControl.SetCachePaused(false);', 'EUS_Manager.ScriptsOnly(Group)', 'EUS_UnitState.Capture(actor)', 'EUS_UnitState.Decode(text, reason)', 'Saved.Restore(Fresh, "fixture", refused)', 'StanderControl.GetEnd() == EUS_EEndReason.DAMAGE', 'HolderControl.GetEnd() == EUS_EEndReason.GAME_MASTER') {
 Assert $fixture.Contains($needle) "state fixture must drive: $needle"
}
Assert ($fixture -match 'Resource\s+squad\s*=\s*Resource\.Load\(SQUAD\);') 'state fixture keeps Resource.Load results in a local'
'PASS: Unit Scripts state: stable end reasons and Unit Caching seams (SetCachePaused, ScriptsOnly, GetEnd), Freeze and Hold hold through damage and unconsciousness, poses loop inside one loiter command and restart on the held spot, versioned EUS_UnitState API with a bounded restore queue, native save registered with own GUIDs, saved/mission-only texts, Enforce gotchas; state fixture wired.'
