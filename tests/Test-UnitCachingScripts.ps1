#requires -Version 7.0
# Portable guard for Unit Caching Simulation of EXPBG Unit Scripts soldiers (Hold, Freeze
# and the vanilla-loiter animations). Before: Unit Scripts reserved every scripted squad
# (EBG_CacheManager.IsReserved, never enrolled) and held an enrolled one awake
# (KeepAwakeReason), for Simulation as well as Full, so zones set to Simulation only for
# animated soldiers never cached them. Now a Simulation zone enrolls and pauses such a squad
# when the per-soldier scripts are its only hold, the soldier keeps his bound control
# (Freeze/Hold) and pose (an animation is restarted through Unit Scripts' own ApplyUnit if
# it ended while paused), and Full stays refused with a named reason. Source pins protect
# the implementation and the Unit Scripts API it relies on (read-only); the native fixture
# tests/EBG_ScriptedSimulationGameplay.c carries the behaviour. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG'
$unitScripts = Join-Path $repo 'addon/unit-scripts/Scripts/Game/EXPUS'
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
# Comments and string contents blanked, so code checks never match prose.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
function Get-Ordered([string]$Text, [string[]]$Needles, [string]$Label) {
 $at = -1
 foreach ($needle in $Needles) {
  $next = $Text.IndexOf($needle, $at + 1)
  Assert ($next -gt $at) "$Label order lost at: $needle"
  $at = $next
 }
}

$scriptedPath = Join-Path $scripts 'EBG_ScriptedUnits.c'
$simulationPath = Join-Path $scripts 'EBG_SimulationCache.c'
$managerPath = Join-Path $scripts 'EBG_CacheManager.c'
$fixturePath = Join-Path $repo 'tests/EBG_ScriptedSimulationGameplay.c'
$scripted = Read-Text $scriptedPath
$scriptedCode = Get-Code $scripted
$simulation = Read-Text $simulationPath
$manager = Read-Text $managerPath

# Enrollment: Unit Scripts' reservation is lifted for Simulation zones only, and only
# when it is the sole reservation (the chain is asked again with the scripted count masked).
$only = Get-Body $scripted 'static\s+bool\s+SimulationOnly\s*\(\s*EBG_CacheManager\s+manager,\s*SCR_AIGroup\s+group\s*\)'
Assert ($only.Contains('group.EUS_Discipline != EUS_Codes.DISCIPLINE_OFF')) 'night discipline is never masked: it stays a hold'
Get-Ordered $only @('int count = group.EUS_Scripted;', 'if (count <= 0)', 'group.EUS_Scripted = 0;', 'bool other = manager.IsReserved(group);', 'group.EUS_Scripted = count;', 'return !other;') 'SimulationOnly'
Assert (([regex]::Matches((Get-Code $only), '\breturn\b')).Count -eq 3 -and !(Get-Code $only).Contains('BumpMe')) 'the scripted count is restored before the only value-carrying return and never replicated'
$codeAll = Get-Code ((Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File | ForEach-Object { Read-Text $_.FullName }) -join "`n")
Assert ([regex]::Matches($codeAll, 'EUS_Scripted\s*=(?!=)').Count -eq 2 -and ![regex]::IsMatch($codeAll, 'EUS_Script\s*=(?!=)') -and ![regex]::IsMatch($codeAll, 'EUS_Discipline\s*=(?!=)') -and ![regex]::IsMatch($codeAll, 'EUS_Set(Script|Scripted|Discipline)\s*\(')) 'Unit Caching writes no Unit Scripts state except the masked-and-restored squad count'
$refresh = Get-Body $manager 'void\s+Refresh\s*\(\s*EBG_CacheZone\s+zone'
$refreshCode = Get-Code $refresh
Assert ($refreshCode.Contains('if (!EBG_ScriptedUnits.SimulationOnly(this, group))')) 'every zone lifts the Unit Scripts reservation when scripts are the only hold (a Full zone caches the squad in Simulation)'
Get-Ordered $refreshCode @('else if (IsReserved(group))', 'DescribeExternalCache(group, externalModule, externalState)', 'EBG_ScriptedUnits.SimulationOnly(this, group)', 'string holder = KeepAwakeReason(group);', 'if (holder.IsEmpty())', 'if (skip.IsEmpty())', 'if (group.EBG_Exclude)', 'skip = MemberSkip(members, enrollmentPersistence);') 'Refresh'
Assert ([regex]::IsMatch($refreshCode, '(?s)if \(skip\.IsEmpty\(\)\)\s*\{\s*if \(group\.EBG_Exclude\).*?else skip = MemberSkip\(members, enrollmentPersistence\);\s*\}')) 'a lifted reservation still runs every ordinary enrollment check (Exclude, spawning, players, commander, proximity, members)'

# Keep-awake: dropped in any zone only for exactly Unit Scripts' own hold with discipline
# off; the record then sleeps in Simulation (a Full zone's fallback).
$keep = Get-Body $scripted 'static\s+string\s+KeepAwake\s*\(\s*EBG_CacheGroup\s+record,\s*string\s+reason\s*\)'
Assert (!$keep.Contains('Zone.Mode')) 'the keep-awake filter is the same in Full and Simulation zones'
Get-Ordered $keep @('if (CountScripted(record.Group) == 0)', 'record.Group.EUS_Discipline == EUS_Codes.DISCIPLINE_OFF && reason == EUS_Manager.HoldReason(record.Group)', 'return string.Empty;') 'KeepAwake'
Assert ($manager.Contains('string keepAwake = EBG_ScriptedUnits.KeepAwake(record, KeepAwakeReason(record.Group));') -and ([regex]::Matches((Get-Code $manager), 'KeepAwakeReason\(record\.Group\)')).Count -eq 1) 'UpdateRecord reads the keep-awake reason through the Unit Scripts filter only'
Assert ($scripted.Contains('static const string FALLBACK_NOTE = "Simulation cached instead of Full: Unit Scripts soldiers') -and !$scripted.Substring($scripted.IndexOf('FALLBACK_NOTE = ')).Split("`n")[0].Contains('scripted')) 'the Simulation fallback is named (and never says "scripted", which the cache-hold fixture reserves for the hold count)'
$uses = Get-Body $scripted 'static\s+bool\s+UsesSimulation\s*\(\s*EBG_CacheGroup\s+record\s*\)'
Assert ($uses.Contains('return record.Zone.Mode == 0 || CountScripted(record.Group) > 0;')) 'Simulation for a Simulation zone, and for a Full zone''s squad with Unit Scripts soldiers'
Assert ($manager.Contains('if (SleepsInSimulation(record))') -and !$manager.Contains('if (zone.Mode == 0)')) 'the scheduler picks Simulation per record (Full zone fallback), not per zone'
Assert ($manager.Contains('if (zone.Mode == 1) record.Reason = SimulationFallbackNote(record);') -and $manager.Contains('return EBG_ScriptedUnits.FALLBACK_NOTE;')) 'a Full zone''s Simulation-cached squad says why'
Assert ($manager.Contains('owner.HasPendingSettings() || !SleepsInSimulation(dormant);') -and !$manager.Contains('owner.Mode != 0')) 'a Full zone keeps its Simulation-cached Unit Scripts squads asleep (only a squad that no longer uses Simulation wakes for Full)'
Assert (!(Get-Code (Get-Body (Read-Text (Join-Path $scripts 'EBG_CacheFullCoordinator.c')) 'static\s+bool\s+Sleep\s*\(')).Contains('EBG_ScriptedUnits')) 'Full capture itself is unchanged (the hold refuses it before Sleep runs)'

# Simulation: pause only a playing pose, capture the script, resume it after presentation.
$unsupported = Get-Body $simulation 'static\s+string\s+Unsupported\s*\(\s*SCR_ChimeraCharacter\s+character'
Get-Ordered $unsupported @('EBG_StaticEmplacement.Unsupported(character);', 'string scripted = EBG_ScriptedUnits.Unsettled(character);', 'if (!scripted.IsEmpty()) return scripted;', 'HasActiveOperation(controller)') 'Unsupported'
$unsettled = Get-Body $scripted 'static\s+string\s+Unsettled\s*\(\s*SCR_ChimeraCharacter\s+character\s*\)'
Assert ($unsettled.Contains('!EUS_Codes.IsAnimation(character.EUS_Script)') -and $unsettled.Contains('if (controller.IsLoitering())')) 'only an animation that is not playing yet delays the pause; Freeze and Hold never do'
$suspend = Get-Body $simulation 'static\s+EBG_SimulationState\s+Suspend\s*\(\s*SCR_AIGroup\s+group'
Get-Ordered $suspend @('member.Emplacement.Capture(character)', 'member.Devices.Capture(character)', 'member.Script = EBG_ScriptedMember.Capture(character);', 'state.Members.Insert(member);', 'EBG_CaptureSimulationTree(member.Character)', 'member.Suspend();') 'Suspend'
Assert ($simulation.Contains('ref EBG_ScriptedMember Script;')) 'each Simulation member owns its Unit Scripts snapshot'
$restore = Get-Body $simulation 'static\s+bool\s+Restore\s*\(\s*EBG_SimulationState\s+state,\s*out\s+string\s+reason\s*\)'
Get-Ordered $restore @('bool changedOwner = ', 'member.Restore();', 'member.Character.EBG_SetSimulationCached(false);', 'member.Restored = true;', 'if (member.Script && !changedOwner) member.Script.Resume(member.Character);') 'Restore'
Assert ($restore.Contains('else if (!state.PreparationFailed) reason += EBG_ScriptedUnits.Summary(state);')) 'the wake reason counts kept, restarted and not resumed scripts'
$capture = Get-Body $scripted 'static\s+EBG_ScriptedMember\s+Capture\s*\(\s*SCR_ChimeraCharacter\s+character\s*\)'
Assert ($capture.Contains('character.EUS_Script == EUS_Codes.NONE') -and $capture.Contains('EUS_Manager.Current()') -and !$capture.Contains('EUS_Manager.Get()') -and $capture.Contains('manager.FindControl(character)')) 'capture reads the bound control without creating a manager'

# Wake: never re-binds a released Freeze/Hold or a script the GM, damage, death or
# possession ended; an animation restarts only after it ended while paused.
$resume = Get-Body $scripted 'void\s+Resume\s*\(\s*SCR_ChimeraCharacter\s+character\s*\)'
$resumeCode = Get-Code $resume
Get-Ordered $resume @('controller.IsDead() || character.EBG_WasPlayerControlled()', 'bool bound = Control && Control.IsBound() && Control.GetActor() == character;', 'if (bound && Control.GetCode() != Code)', 'if (!EUS_Codes.IsAnimation(Code))', 'if (bound && controller.IsLoitering())', 'input.m_iLoiteringType >= 0', 'if (!bound)', 'ended.StartsWith("the animation could not be kept")', 'EUS_Manager.Current()', 'manager.ApplyUnit(character, Code, null)') 'Resume'
Assert (([regex]::Matches($resumeCode, '\bApplyUnit\s*\(')).Count -eq 1 -and $resumeCode.IndexOf('ApplyUnit') -gt $resumeCode.IndexOf('IsAnimation(Code)')) 'ApplyUnit is reached only for an animation'
Assert (([regex]::Matches($scriptedCode, '\bApplyUnit\s*\(')).Count -eq 1 -and !$scriptedCode.Contains('Release(') -and !$scriptedCode.Contains('StartLoitering') -and !$scriptedCode.Contains('StopLoitering')) 'Unit Caching never drives the loiter or releases a script itself'
$summary = Get-Body $scripted 'static\s+string\s+Summary\s*\(\s*EBG_SimulationState\s+state\s*\)'
Assert ($summary.Contains('outcome.StartsWith("kept")') -and $summary.Contains('outcome == "animation restarted"')) 'the summary reads the outcomes Resume writes'
foreach ($outcome in '"kept; script changed while cached"', '"kept"', '"kept; animation playing"', '"kept; animation entry pending"', '"animation restarted"') { Assert $resume.Contains($outcome) "Resume outcome lost: $outcome" }

# The hold reason names the real state: an animation is a loiter, never a vehicle.
$update = Get-Body $manager 'protected\s+void\s+UpdateRecord\s*\(\s*EBG_CacheGroup\s+record\s*\)'
Assert (!$manager.Contains('"Unsupported vehicle, medical or movement state"') -and $update.Contains('record.Reason = "Unsupported vehicle state: " + mount;') -and $update.Contains('"Unsupported medical or movement state (unconscious, falling, swimming or climbing)"')) 'UpdateRecord names a compartment refusal apart from a medical or movement state'

# Unit Scripts API this relies on (read-only; owned by the unit-scripts module).
$eusManager = Read-Text (Join-Path $unitScripts 'EUS_Manager.c')
$eusControl = Read-Text (Join-Path $unitScripts 'EUS_UnitControl.c')
$eusCodes = Read-Text (Join-Path $unitScripts 'EUS_Codes.c')
$eusRpl = Read-Text (Join-Path $unitScripts 'EUS_Replication.c')
foreach ($signature in 'static\s+EUS_Manager\s+Current\s*\(\s*\)', 'static\s+string\s+HoldReason\s*\(\s*SCR_AIGroup\s+group\s*\)', 'EUS_UnitControl\s+FindControl\s*\(\s*SCR_ChimeraCharacter\s+actor\s*\)', 'bool\s+ApplyUnit\s*\(\s*SCR_ChimeraCharacter\s+actor,\s*int\s+code,\s*EUS_Report\s+report\s*\)') {
 Assert ([regex]::IsMatch($eusManager, $signature)) "Unit Scripts manager API changed: $signature"
}
Assert ((Get-Body $eusManager 'static\s+bool\s+Reserves\s*\(\s*SCR_AIGroup\s+group\s*\)').Contains('return group.EUS_Discipline != EUS_Codes.DISCIPLINE_OFF || group.EUS_Scripted > 0;')) 'masking relies on Reserves reading only discipline and the scripted count'
Assert ((Get-Body $eusManager 'override\s+bool\s+IsReserved\s*\(\s*SCR_AIGroup\s+group\s*\)').Contains('if (EUS_Manager.Reserves(group)) return true;') -and (Get-Body $eusManager 'override\s+string\s+KeepAwakeReason\s*\(\s*SCR_AIGroup\s+group\s*\)').Contains('string held = EUS_Manager.HoldReason(group);')) 'Unit Scripts still answers the two seams through Reserves and HoldReason'
$apply = Get-Body $eusManager 'bool\s+ApplyUnit\s*\(\s*SCR_ChimeraCharacter\s+actor,\s*int\s+code,\s*EUS_Report\s+report\s*\)'
Assert (!([regex]::IsMatch((Get-Code $apply), '(?<!if \(report\) )\breport\.'))) 'ApplyUnit accepts a null report (every report use is guarded)'
foreach ($getter in 'bool IsBound()', 'int GetCode()', 'SCR_ChimeraCharacter GetActor()', 'string GetEndReason()', 'vector GetAnchor()') { Assert $eusControl.Contains($getter) "Unit Scripts control getter changed: $getter" }
Assert ($eusControl.Contains('Release(string.Format("the animation could not be kept after %1 attempts", LOITER_ATTEMPTS));')) 'the only release Resume undoes is the loiter-retry release'
Assert ($eusControl.Contains('m_Actor.IsInVehicle()') -and $eusControl.Contains('m_Controller.StartLoitering(')) 'an animation is a loiter, and a bound script ends if he is ever in a vehicle'
Assert ($eusRpl.Contains('[RplProp(), NonSerialized()] int EUS_Script;') -and $eusRpl.Contains('[RplProp(), NonSerialized()] int EUS_Scripted;') -and $eusRpl.Contains('[RplProp(), NonSerialized()] int EUS_Discipline;')) 'replicated Unit Scripts state fields'
Assert ($eusCodes.Contains('static bool IsAnimation(int code)') -and $eusCodes.Contains('static const int DISCIPLINE_OFF = 0;')) 'Unit Scripts codes'

# Enforce gotchas in the new and touched sources and the fixture.
foreach ($path in @($scriptedPath, $simulationPath, $managerPath, $fixturePath)) {
 $text = Read-Text $path
 $code = Get-Code $text
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($code, 'Math\.RandomFloat\b')) "Math.RandomFloat in $path"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<|static\s+const\s+vector\b')) "static initializer in $path (use EXPBG_LazyStatics_<Class>)"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
foreach ($path in @($scriptedPath, $fixturePath)) {
 $code = Get-Code (Read-Text $path)
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -eq 13 }).Count -eq 0) "CR in $path"
 Assert (![regex]::IsMatch($code, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $path"
 Assert (![regex]::IsMatch($code, '\b(int|float)\s+\w+\s*=\s*[^;]*\b(IsLoitering|IsBound|IsAnimation|IsEmpty|Contains)\s*\(')) "implicit bool to int in $path"
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
Assert (![regex]::IsMatch($scriptedCode, '\bif\s*\([^;{}]*\)\s*return\s+[^;\s][^;]*;')) 'every value return of EBG_ScriptedUnits.c is on its own line'
Assert (![regex]::IsMatch($scriptedCode, '\bPrint(Format)?\s*\(') -and !$scriptedCode.Contains('CallLater') -and !$scriptedCode.Contains('EOnFrame')) 'EBG_ScriptedUnits adds no log, timer or frame hook (scheduler-bound)'

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
$fixtureCode = Get-Code $fixture
Assert ($fixture.Contains("-FixturePath tests/EBG_ScriptedSimulationGameplay.c -ExpectResult '\[EBG SCRIPTED SIM RESULT\] checks=[1-9]\d* failures=0 scripted=5 restarted=\d+ reason=complete mismatches=0' -TimeoutSeconds 420 -OrchestratorSlotGranted")) 'fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'fixture must keep the runner driver class names'
Assert ($fixture.Contains('PrintFormat("[EBG SCRIPTED SIM RESULT] checks=%1 failures=%2 scripted=%3 restarted=%4 reason=%5 mismatches=%6", Checks, Failures, Actors.Count(), Restarted, reason, EBG_DebugChecks.Mismatches);') -and $fixture.Contains('Finish("complete");')) 'fixture must print the RESULT line its regex expects'
Assert ([regex]::IsMatch($fixtureCode, '(?s)override\s+void\s+EOnInit\s*\([^)]*\)\s*\{\s*if\s*\(!Replication\.IsServer\(\)\)\s*\{[^}]*\}\s*EBG_DebugChecks\.Enabled\s*=\s*true;') -and $fixture.Contains('Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");')) 'fixture enables the index cross-checks and fails on a mismatch'
Assert ($fixture -match 'Resource\s+resource\s*=\s*Resource\.Load\(prefab\);') 'fixture must keep Resource.Load results in a local'
Assert ($fixture -match 'modded\s+class\s+EBG_CacheManager' -and $fixture -match 'override\s+protected\s+void\s+UpdatePlayers\(\)') 'fixture injects presence through UpdatePlayers'
foreach ($needle in 'Manager.ApplyUnit(member, code, Report)', 'scripts.Insert(EUS_Codes.FREEZE);', 'scripts.Insert(EUS_Codes.HOLD);', 'scripts.Insert(EUS_Codes.ANIMATION + 2);', 'scripts.Insert(EUS_Codes.ANIMATION);', 'scripts.Insert(EUS_Codes.ANIMATION + 1);',
 'zone.SetValue(1, 1);', 'EBG_ScriptedUnits.UsesSimulation(Record)', 'Record.Reason == EBG_ScriptedUnits.FALLBACK_NOTE', 'Held = Record.Simulation;', 'cached.Script.Loitering', 'Record.KeepAwake.IsEmpty()',
 'Now() - PhaseAt < CACHED_SECONDS', 'Presence(Origin);', 'woken.Script.Outcome', 'vector.DistanceXZ(Actors[wokenIndex].GetOrigin(), woken.Position) <= 0.25', 'Check(Drift[awakeIndex] <= Limits[awakeIndex],',
 'if (Codes[spotIndex] == EUS_Codes.FREEZE) Limits[spotIndex] = 0.5;', 'Spots[spotIndex] = control.GetAnchor();', 'Check(Playing(Actors[awakeIndex]), "animation plays again after the wake: "') {
 Assert $fixture.Contains($needle) "fixture must drive: $needle"
}
Assert ($fixture.Contains('static const float CACHED_SECONDS = 30;')) 'the paused window outlasts Unit Scripts'' four loiter retries'
'PASS: Unit Caching Simulation pauses and resumes EXPBG Unit Scripts soldiers (Hold, Freeze, loiter animations) on the same actors, lifts the Unit Scripts reservation when it is the sole hold, Full zones fall back to Simulation for such squads (named in their status), restarts an animation only after it ended while paused; Unit Scripts API pinned; scripted Simulation fixture wired.'
