#requires -Version 7.0
# Portable guard for Random Garrison reaching its building target (Unreleased): on
# Chernarus Minus (Krasnostav, RHS AFRF, fire teams and squads) a zone set to 4
# buildings garrisoned one, because a failed building was replaced only up to the
# target plus 8 tries and sheds, one-room houses and wooden huts used them up.
# Now: up to EXPG_RGRules.AttemptCap tries (3 x target, at least target + 16, never
# more than the eligible buildings); other buildings of a type that failed for its
# posts or rooms (and never took a squad) wait for a second pass; the first squad is
# judged as soon as the analysis is ready; and when a generation ends the status, the
# log and the Game Master who started it get the failures by reason.
# Checks the source wiring, a model of the try limit, and a behaviour model of the
# queue (0.1.14 against this rule) on the 49 eligible buildings around Krasnostav
# (types and counts from the Chernarus Minus layers; posts per type are assumptions
# from the runs: sheds too small, wooden 1I01 without rooms, house_yellow and House 08
# take a 4-man team), with the event order shuffled to prove that a seed repeats its
# generation whatever analysis finishes first. Also: a stop inside the step (the AI
# limit) stays Stopped with its reason and one notice instead of turning into Done; a
# Generate while clearing is told to wait (Stop is refused then); a Game Master who
# stops another one's generation is told too; and the native stop fixture is wired.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/random-garrison/Scripts/Game/EXPGR'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
# Comments blanked, strings kept.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\n])*"|//[^\n]*', { param($m) if ($m.Value.StartsWith('//')) { '' } else { $m.Value } })
}
# Braces inside strings do not count.
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $masked = [regex]::Replace($Text, '"(?:\\.|[^"\\\n])*"', { param($m) '"' + (' ' * ($m.Value.Length - 2)) + '"' })
 $open = $masked.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $masked.Length; $i++) {
  if ($masked[$i] -eq '{') { $depth++ } elseif ($masked[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}

$modulePath = Join-Path $scripts 'EXPG_RandomGarrisonModule.c'
$moduleRaw = Read-Text $modulePath
$module = Get-Code $moduleRaw
$rules = Get-Code (Read-Text (Join-Path $scripts 'EXPG_RandomGarrisonRules.c'))
$plan = Read-Text (Join-Path $repo 'addon/garrison/Scripts/Game/EXPG/EXPG_BuildingPlan.c')

# The try limit: 3 x target, at least target + 16, at most the eligible buildings.
$capBody = Get-Body $rules 'static\s+int\s+AttemptCap\s*\(\s*int\s+target,\s*int\s+eligible\s*\)'
Assert ($capBody -match 'if \(target <= 0 \|\| eligible <= 0\)\s*\{\s*return 0;' -and $capBody -match 'int cap = target \* (\d+);' -and $capBody -match 'if \(cap < target \+ (\d+)\)\s*\{\s*cap = target \+ \d+;' -and $capBody -match 'if \(cap > eligible\)\s*\{\s*cap = eligible;' -and $capBody -match 'return cap;') 'AttemptCap: 0 without target or buildings, else max(3 x target, target + 16) capped at the eligible count'
$factor = [int][regex]::Match($capBody, 'int cap = target \* (\d+);').Groups[1].Value
$extra = [int][regex]::Match($capBody, 'if \(cap < target \+ (\d+)\)').Groups[1].Value
Assert ($factor -eq 3 -and $extra -eq 16 -and $capBody -match "cap = target \+ $extra;") 'AttemptCap uses 3 x target and target + 16'
function Model-AttemptCap([int]$Target, [int]$Eligible) {
 if ($Target -le 0 -or $Eligible -le 0) { return 0 }
 [math]::Min([math]::Max($Target * $factor, $Target + $extra), $Eligible)
}
Assert ((Model-AttemptCap 4 49) -eq 20 -and (Model-AttemptCap 1 49) -eq 17 -and (Model-AttemptCap 32 2048) -eq 96 -and (Model-AttemptCap 4 10) -eq 10 -and (Model-AttemptCap 0 10) -eq 0 -and (Model-AttemptCap 4 0) -eq 0) 'try limit model: 20 for 4 of 49, 17 for 1, 96 for 32, never more than the eligible buildings'
function Legacy-AttemptCap([int]$Target) { $Target + [math]::Max($Target, 8) }

# Selection: the limit from the rules, the target owed to the queue, no fixed +8.
$select = Get-Body $module 'protected\s+void\s+Select\s*\(\s*\)'
Assert ($select -match 'm_iAttemptCap = EXPG_RGRules\.AttemptCap\(m_iTarget, m_aSites\.Count\(\)\);' -and $select -match 'm_iOwed = m_iTarget;\s*FillQueue\(\);' -and $select -notmatch 'extra < 8' -and $select -match 'm_aDeferred\.Clear\(\);' -and $select -match 'm_mTypes\.Clear\(\);' -and $select -match 'm_iDeferredCursor = 0;') 'Select: AttemptCap, a fresh second pass and type verdicts, the target owed to FillQueue'
$fill = Get-Body $module 'protected\s+void\s+FillQueue\s*\(\s*\)'
Assert ($fill -match 'while \(m_iOwed > 0 && PromoteNext\(\)\) \{ m_iOwed--; \}' -and $fill -match 'if \(!m_bPromoteWait\) \{ m_iOwed = 0; \}') 'FillQueue queues while owed and forgets what can never be queued (keeps it only while a decision waits)'

# PromoteNext: in order; a failed type goes to the second pass; an unknown type with an
# earlier building still analysed waits without moving the cursor; then the second pass.
$promote = Get-Body $module 'protected\s+bool\s+PromoteNext\s*\(\s*\)'
Assert ($promote -match '^\s*m_bPromoteWait = false;' -and $promote -match 'if \(m_bStopping \|\| m_iAttempts >= m_iAttemptCap\)\s*\{\s*return false;') 'PromoteNext clears the wait flag and stops at the try limit'
$first = $promote.IndexOf('while (m_iOrderCursor < m_aOrder.Count())')
$second = $promote.IndexOf('while (m_iDeferredCursor < m_aDeferred.Count())')
Assert ($first -ge 0 -and $second -gt $first) 'PromoteNext walks the seeded order, then the put-back buildings'
$pass = $promote.Substring($first, $second - $first)
$at = @{}
foreach ($needle in 'Draw(site);', 'int verdict = m_mTypes.Get(site.TypeKey);', 'if (verdict != TYPE_GOOD && TypeUnresolved(site.TypeKey))', 'm_bPromoteWait = true;', 'm_iOrderCursor++;', 'if (verdict == TYPE_FAILED)', 'm_aDeferred.Insert(site);', 'if (QueueSite(site, manager))') {
 $at[$needle] = $pass.IndexOf($needle)
 Assert ($at[$needle] -ge 0) "PromoteNext first pass lost: $needle"
}
$skipReserve = $pass.IndexOf('m_iOrderCursor++;')
Assert ($pass -match 'if \(!site \|\| site\.Stage != EXPG_RGSite\.RESERVE\)\s*\{\s*m_iOrderCursor\+\+;\s*continue;') 'PromoteNext steps over buildings that are no longer reserves'
$advance = $pass.IndexOf('m_iOrderCursor++;', $at['m_bPromoteWait = true;'])
Assert ($at['Draw(site);'] -lt $at['int verdict = m_mTypes.Get(site.TypeKey);'] -and $at['int verdict = m_mTypes.Get(site.TypeKey);'] -lt $at['if (verdict != TYPE_GOOD && TypeUnresolved(site.TypeKey))'] -and $at['m_bPromoteWait = true;'] -lt $advance -and $advance -lt $at['if (verdict == TYPE_FAILED)'] -and $at['if (verdict == TYPE_FAILED)'] -lt $at['m_aDeferred.Insert(site);'] -and $at['m_aDeferred.Insert(site);'] -lt $at['if (QueueSite(site, manager))']) 'PromoteNext order: draw (type key), wait while an earlier building of an unknown type is analysed (cursor kept), then advance, put back a failed type, else queue'
Assert ($pass -match 'm_bPromoteWait = true;\s*return false;' -and $pass -match 'site\.Deferred = true;\s*m_aDeferred\.Insert\(site\);\s*continue;') 'a waiting decision returns at once; a put-back building costs no try'
Assert ($promote.Substring($second) -match 'm_iDeferredCursor\+\+;\s*if \(later && later\.Stage == EXPG_RGSite\.RESERVE && QueueSite\(later, manager\)\)') 'the second pass takes the put-back reserves in order'
$queue = Get-Body $module 'protected\s+bool\s+QueueSite\s*\('
Assert ($queue -match 'Unavailable\(site, manager\)' -and $queue -match 'NearPlayers\(site\)' -and $queue -match 'EXPG_RandomGarrisonDirector\.Claim\(site\.Structure, m_iZoneId\)' -and $queue -match 'site\.Stage = EXPG_RGSite\.QUEUED;\s*m_aActive\.Insert\(site\);\s*m_iAttempts\+\+;\s*return true;') 'QueueSite keeps the free, away-from-players and claim checks; a try is counted only when queued'
Assert ((Get-Body $module 'protected\s+bool\s+TypeUnresolved\s*\(') -match 'site\.TypeKey == typeKey && \(site\.Stage == EXPG_RGSite\.QUEUED \|\| site\.Stage == EXPG_RGSite\.ANALYSING\)') 'a type is unresolved while a building of it is queued or analysed'
Assert ((Get-Body $module 'protected\s+void\s+Draw\s*\(') -match 'site\.TypeKey = site\.PrefabName \+ "\|" \+ site\.FactionId;') 'the type key is the prefab and the drawn faction'
$noteType = Get-Body $module 'protected\s+void\s+NoteType\s*\('
Assert ($noteType -match 'if \(good\)\s*\{\s*m_mTypes\.Set\(site\.TypeKey, TYPE_GOOD\);\s*return;' -and $noteType -match 'if \(m_mTypes\.Get\(site\.TypeKey\) != TYPE_GOOD\) \{ m_mTypes\.Set\(site\.TypeKey, TYPE_FAILED\); \}') 'a type that took a squad stays good; otherwise a layout failure marks it failed'

# Layout failures are the planner's own words.
$typeFailure = Get-Body $module 'static\s+bool\s+TypeFailure\s*\('
$words = @([regex]::Matches($typeFailure, 'reason == "([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
Assert (($words -join '|') -ceq 'No connected, clear indoor positions were found|Structure has too many interior samples|Structure exceeds the supported sampling bounds') "TypeFailure names the three layout failures: $($words -join '|')"
foreach ($word in $words) { Assert ($plan.Contains("Fail(`"$word`")") -or $plan.Contains("Error = `"$word`"")) "EXPG_BuildingPlan no longer fails with '$word' (keep TypeFailure in step)" }
# The status and notice name the layout failures in short words (no comma inside one
# reason of the comma-separated list); the log and the saved outcomes keep the
# planner's words.
$label = Get-Body $module 'static\s+string\s+ReasonLabel\s*\('
$labelled = @([regex]::Matches($label, 'if \(reason == "([^"]+)"\)\s*\{\s*return "([^"]+)";') | ForEach-Object { [pscustomobject]@{ From = $_.Groups[1].Value; To = $_.Groups[2].Value } })
Assert ((@($labelled | ForEach-Object From) -join '|') -ceq ($words -join '|')) "ReasonLabel words exactly the layout failures TypeFailure names: $(@($labelled | ForEach-Object From) -join '|')"
foreach ($pair in $labelled) { Assert (!$pair.To.Contains(',') -and !$pair.To.Contains(';') -and $pair.To -ceq $pair.To.ToLowerInvariant() -and $pair.To.Length -le 32) "a status label is short, lower case, without comma or semicolon: $($pair.To)" }
Assert ($label -match 'if \(reason\.Length\(\) > 1 && reason\.EndsWith\("\."\)\)\s*\{\s*return reason\.Substring\(0, reason\.Length\(\) - 1\);\s*\}\s*return reason;\s*$') 'any other reason keeps its words and loses only a closing full stop'
function Model-ReasonLabel([string]$Reason) {
 foreach ($pair in $labelled) { if ($Reason -ceq $pair.From) { return $pair.To } }
 if ($Reason.Length -gt 1 -and $Reason.EndsWith('.')) { return $Reason.Substring(0, $Reason.Length - 1) }
 $Reason
}
Assert ((Model-ReasonLabel 'No connected, clear indoor positions were found') -ceq 'without usable rooms' -and (Model-ReasonLabel 'too small for the chosen squad sizes') -ceq 'too small for the chosen squad sizes' -and (Model-ReasonLabel 'The engine could not spawn that squad.') -ceq 'The engine could not spawn that squad' -and (Model-ReasonLabel '.') -ceq '.') 'label model: rooms in short words, PickSquad''s words kept, a spawner refusal without its full stop'
Assert (([regex]::Matches($module, '\bReasonLabel\(')).Count -eq 2 -and (Get-Body $module 'protected\s+void\s+BuildSummary\s*\(\s*\)') -match 'named \+= string\.Format\("%1 %2", counts\[best\], ReasonLabel\(reasons\[best\]\)\);') 'only the summary uses the labels'
Assert ((Get-Body $module 'protected\s+void\s+EndSite\s*\(') -match 'PrintFormat\([^;]*\bnote\);' -and (Get-Body $module 'protected\s+void\s+BuildOutcomes\s*\(') -match 'outcome = "failed: " \+ site\.Note;') 'the failed log line and the saved outcomes keep the reason as given'

# The first squad is judged when the analysis is ready, with no generator draw.
$analysed = Get-Body $module 'protected\s+void\s+Analysed\s*\('
Assert ($analysed -match 'NoteAnalysed\(site\);\s*site\.Stage = EXPG_RGSite\.READY;' -and $analysed -match 'site\.Posts = plan\.Slots\.Count\(\);' -and $analysed -match 'int assigned = manager\.AssignedSoldiers\(site\.Structure\);' -and $analysed -match 'if \(!TooSmall\(site, site\.Posts - assigned\)\)\s*\{\s*NoteType\(site, true\);\s*return;' -and $analysed -match 'if \(assigned == 0\) \{ NoteType\(site, false\); \}\s*EndSite\(site, "too small for the chosen squad sizes"\);') 'Analysed: posts known, a building no squad fits fails at once, only an empty one marks its type'
Assert ($analysed -match 'if \(site\.Placed > 0\)\s*\{\s*return;' -and $analysed -notmatch '\.Rng\.') 'a re-analysed building with squads is not judged again, and no draw is made'
Assert ((Get-Body $module 'protected\s+EXPG_SquadEntry\s+PickSquad\s*\(') -match 'why = "too small for the chosen squad sizes";') 'the early verdict uses the words of PickSquad'
$tooSmall = Get-Body $module 'protected\s+bool\s+TooSmall\s*\('
Assert ($tooSmall -match 'CountUsable\(m_iRunSizes, m_bRunExcludeSupport, site\.FactionId\) == 0' -and $tooSmall -match 'CanDrawSquad\(site, entry, budget\)' -and $tooSmall -notmatch '\b(m_iSizes|m_bExcludeSupport)\b') 'TooSmall: PickSquad''s filters (CanDrawSquad), a catalog gap left to PickSquad, run copies only'
$failedAnalysis = Get-Body $module 'protected\s+void\s+AnalysisFailed\s*\('
Assert ($failedAnalysis -match 'NoteAnalysed\(site\);\s*if \(TypeFailure\(reason\)\) \{ NoteType\(site, false\); \}\s*EndSite\(site, reason\);') 'a layout failure of the analysis marks the type'
foreach ($caller in 'protected\s+void\s+ServiceAnalyses\s*\(', 'protected\s+bool\s+StartAnalysis\s*\(') {
 $body = Get-Body $module $caller
 Assert ($body -match 'Analysed\(site, manager\);' -and $body -match 'AnalysisFailed\(site, analysis\.Failure\);' -and $body -notmatch 'site\.Stage = EXPG_RGSite\.READY;') "every ready or failed analysis goes through Analysed or AnalysisFailed: $caller"
}
Assert (([regex]::Matches($module, 'Stage = EXPG_RGSite\.READY;')).Count -eq 1) 'only Analysed makes a building ready'

# Every failure or skip owes the queue one building; a waiting decision is retried each tick.
foreach ($ender in 'protected\s+void\s+EndSite\s*\(', 'protected\s+void\s+SkipSite\s*\(') {
 $body = Get-Body $module $ender
 Assert ($body -match 'm_iOwed\+\+;\s*FillQueue\(\);\s*$' -and $body -notmatch 'PromoteNext\(') "$ender owes one building to the queue"
}
Assert (([regex]::Matches($module, 'PromoteNext\(\)')).Count -eq 2) 'PromoteNext is called only by FillQueue (and declared once)'
$running = Get-Body $module 'protected\s+void\s+StepRunning\s*\('
Assert ($running.IndexOf('ServiceAnalyses(now, manager);') -lt $running.IndexOf('if (m_iOwed > 0) { FillQueue(); }') -and $running.IndexOf('if (m_iOwed > 0) { FillQueue(); }') -lt $running.IndexOf('ServiceSpawns(now, manager);') -and $running.IndexOf('ServiceSpawns(now, manager);') -lt $running.IndexOf('if (AllSettled())')) 'StepRunning retries a waiting decision after the analyses and before spawns and the settle check'
Assert ((Get-Body $module 'protected\s+bool\s+AllSettled\s*\(') -match 'if \(!m_aPending\.IsEmpty\(\) \|\| \(m_iOwed > 0 && m_bPromoteWait\)\)') 'a waiting decision keeps the generation running'
# A stop inside the step (the AI-limit wait of ServiceSpawns) ends the step: the zone
# stays Stopped with its reason and one notice, never Done (and a second notice) on top.
$guard = $running.IndexOf('if (m_iState != STATE_RUNNING) { return; }')
Assert ($guard -gt $running.IndexOf('ServiceSpawns(now, manager);') -and $guard -lt $running.IndexOf('if (AllSettled())') -and ([regex]::Matches($running, 'FinishGeneration\(')).Count -eq 1 -and $running.IndexOf('FinishGeneration(') -gt $guard) 'StepRunning checks the state after ServiceSpawns, before AllSettled/FinishGeneration'
$stopCallers = 'bool\s+Run\s*\(\s*int\s+action', 'void\s+AbortForSave\s*\(', 'protected\s+void\s+StepCatalog\s*\(', 'protected\s+void\s+WaitForAILimit\s*\('
$stopCalls = 0
foreach ($caller in $stopCallers) { $stopCalls += ([regex]::Matches((Get-Body $module $caller), '\bStopGeneration\(')).Count }
Assert (([regex]::Matches($module, '\bStopGeneration\(')).Count -eq $stopCalls + 1) 'StopGeneration is called only from Run, AbortForSave, StepCatalog and WaitForAILimit (the only one inside the step)'
Assert (([regex]::Matches($module, '\bWaitForAILimit\(')).Count -eq 3 -and (Get-Body $module 'protected\s+void\s+ServiceSpawns\s*\(') -match 'WaitForAILimit\(now, limit\);\s*return;' -and (Get-Body $module 'protected\s+void\s+SpawnSquad\s*\(') -match 'WaitForAILimit\(now, reason\);\s*return;' -and ([regex]::Matches($module, '\bSpawnSquad\(')).Count -eq 2 -and (Get-Body $module 'protected\s+void\s+ServiceSpawns\s*\(') -match 'SpawnSquad\(site, entry, now, manager\);\s*return;') 'the AI-limit stop is reached only from ServiceSpawns, which returns at once'
foreach ($phase in 'protected\s+void\s+ServicePending\s*\(', 'protected\s+void\s+ServiceAnalyses\s*\(', 'protected\s+void\s+FillQueue\s*\(', 'protected\s+bool\s+PromoteNext\s*\(', 'protected\s+void\s+EndSite\s*\(', 'protected\s+void\s+SkipSite\s*\(') {
 Assert ((Get-Body $module $phase) -notmatch '\b(StopGeneration|FinishGeneration|WaitForAILimit)\(') "no stop or finish before the state check: $phase"
}
$stopWork = Get-Body $module 'protected\s+void\s+StopWork\s*\('
Assert ($stopWork -match 'm_aActive\.Clear\(\);\s*m_iOwed = 0;\s*m_bPromoteWait = false;\s*m_bStopping = false;') 'StopWork leaves nothing owed to the queue and no waiting decision'
# Refusals point to what works: a clear cannot be stopped; the starter and the Game Master
# who pressed Stop are both told.
$run = Get-Body $module 'bool\s+Run\s*\(\s*int\s+action'
$clearBusy = $run.IndexOf('if (m_iState == STATE_CLEARING)')
$runBusy = $run.IndexOf('if (m_iState == STATE_CENSUS || m_iState == STATE_CATALOG || m_iState == STATE_RUNNING)')
Assert ($clearBusy -ge 0 -and $runBusy -gt $clearBusy -and $run -match 'if \(m_iState == STATE_CLEARING\)\s*\{\s*Refuse\("Busy \(" \+ StateText\(\) \+ "\): wait until it has finished", playerId\);\s*return false;' -and $run -match 'if \(m_iState == STATE_CENSUS \|\| m_iState == STATE_CATALOG \|\| m_iState == STATE_RUNNING\)\s*\{\s*Refuse\("Busy \(" \+ StateText\(\) \+ "\): wait for Done or use Stop first", playerId\);\s*return false;' -and ([regex]::Matches($run, 'wait for Done or use Stop first')).Count -eq 1) 'Generate while clearing says to wait (Stop is refused then); while generating, to wait or Stop'
Assert ($run -match 'Refuse\("Clearing: wait until it has finished", playerId\);') 'Stop is still refused while clearing'
Assert ($run -match 'StopGeneration\("stopped by the Game Master"\);\s*if \(playerId != m_iRunPlayer\) \{ Tell\(playerId, DoneText\("Stopped \(" \+ m_sStopReason \+ "\)"\)\); \}\s*return true;') 'a Game Master who stops another one''s generation is told too, with the same text'
$reset = Get-Body $module 'protected\s+void\s+ResetWork\s*\('
foreach ($field in 'm_aDeferred.Clear();', 'm_mTypes.Clear();', 'm_iDeferredCursor = 0;', 'm_iOwed = 0;', 'm_bPromoteWait = false;', 'm_sSummary = "";') { Assert $reset.Contains($field) "ResetWork resets $field" }
$siteClass = Get-Body $module 'class\s+EXPG_RGSite\s*\{'
Assert ($siteClass -match 'int Posts = -1;' -and $siteClass -match 'string TypeKey;' -and $siteClass -match 'bool Deferred;') 'EXPG_RGSite carries posts, type key and the put-back flag'

# Reporting: the summary in the status, the log and a notice to the Game Master.
$summary = Get-Body $module 'protected\s+void\s+BuildSummary\s*\(\s*\)'
Assert ($module -match 'static const int MAX_REASONS = 8;' -and $module -match 'static const int STATUS_REASONS = 4;' -and $summary -match 'reasons\.Count\(\) < MAX_REASONS' -and $summary -match 'pick < STATUS_REASONS' -and $summary -match '"%1 failed: %2"' -and $summary -match '", %1 other"') 'failures counted by reason (8 kept), the 4 most frequent named, the rest as other'
Assert ($summary -match 'if \(PlacedCount\(\) < m_iTarget && m_iAttemptCap > 0\)' -and $summary -match '"tried %1 of %2 eligible buildings"' -and $summary -match '", none left"' -and $summary -match '"(?:, )?limit of %1 tries reached"' -and $summary -match 'untried of types that had failed' -and $summary -match 'took fewer squads than drawn') 'a missed target says how many buildings were tried and why no more were'
$done = Get-Body $module 'protected\s+string\s+DoneText\s*\('
Assert ($done -match '"%1: %2 of %3 buildings, %4 squads \(seed %5\)"' -and $done -match 'text \+= "; " \+ m_sSummary;' -and $module -notmatch 'FirstFailure') 'DoneText: "x of target buildings" when short, then the summary (no first failure only)'
foreach ($ender in @(@('protected\s+void\s+FinishGeneration\s*\(', 'Tell\(m_iRunPlayer, DoneText\("Done"\)\);'), @('protected\s+void\s+StopGeneration\s*\(', 'Tell\(m_iRunPlayer, DoneText\("Stopped \(" \+ reason \+ "\)"\)\);'))) {
 $body = Get-Body $module $ender[0]
 Assert ($body -match 'BuildOutcomes\(\);\s*BuildSummary\(\);' -and $body -match $ender[1] -and $body -match 'm_sSummary|tail') "$($ender[0]) builds the summary, logs it and tells the Game Master"
}
Assert ((Get-Body $module 'protected\s+void\s+Refuse\s*\(') -match 'Tell\(playerId, message\);') 'refusals use the same notice'
$tell = Get-Body $module 'protected\s+void\s+Tell\s*\('
Assert ($tell -match 'if \(playerId <= 0\)\s*\{\s*return;' -and $tell -match 'core\.GetEditorManager\(playerId\)' -and $tell -match 'editor\.EXPG_Notice\("Random Garrison: " \+ message \+ "\."\);') 'Tell: one EXPG_Notice to a connected Game Master, none for mission start'
$outcomes = Get-Body $module 'protected\s+void\s+BuildOutcomes\s*\(\s*\)'
Assert ($outcomes -match 'for \(int pass = 0; pass < 2; pass\+\+\)' -and $outcomes -match 'if \(pass == 0 && site\.Placed == 0\) \{ continue; \}') 'saved outcomes list the garrisoned buildings first (more tries must not crowd them out of 32)'
Assert ($moduleRaw.Contains('failed (posts %4, tries %5/%6): %7') -and $moduleRaw.Contains('deployed in %3 (%4/%5, posts %6)') -and $moduleRaw.Contains('tried=%5/%6 tries=%7')) 'per-building log lines carry the planned posts; the done line the tries'
$prints = ([regex]::Matches($module, '\bPrint(Format)?\(')).Count
Assert ($prints -le 18) "the module keeps its 18 log calls (Test-PerformanceGuards pin): $prints"
Assert (!$moduleRaw.Contains("`r") -and @([Text.Encoding]::UTF8.GetBytes($moduleRaw) | Where-Object { $_ -gt 127 }).Count -eq 0) 'module stays ASCII with LF'

# Behaviour model of one generation. Stages: R reserve, Q queued or analysing, Y ready,
# D done, F failed. The scheduler picks the next analysis or spawn at random, so the
# test sees every order in which analyses and spawns can finish.
$Smallest = 4
function New-State([object[]]$Mix, [int]$Target, [int]$Seed, [bool]$Legacy) {
 $sites = [Collections.Generic.List[object]]::new()
 foreach ($entry in $Mix) { for ($k = 0; $k -lt $entry[1]; $k++) { $sites.Add([pscustomobject]@{ Id = $sites.Count; Type = $entry[0]; Posts = $entry[2]; Stage = 'R'; Deferred = $false }) } }
 $order = [int[]](0..($sites.Count - 1))
 $rng = [Random]::new($Seed)
 for ($i = $order.Count - 1; $i -gt 0; $i--) { $j = $rng.Next($i + 1); $held = $order[$i]; $order[$i] = $order[$j]; $order[$j] = $held }
 $cap = if ($Legacy) { Legacy-AttemptCap $Target } else { Model-AttemptCap $Target $sites.Count }
 @{ Sites = $sites; Order = $order; Cursor = 0; Deferred = [Collections.Generic.List[object]]::new(); DCursor = 0; Types = @{}; Owed = 0; Wait = $false
  Attempts = 0; Cap = $cap; Active = [Collections.Generic.List[object]]::new(); Legacy = $Legacy; Tried = [Collections.Generic.List[int]]::new() }
}
function Model-Queue($S, $Site) { $Site.Stage = 'Q'; $S.Active.Add($Site); $S.Attempts++; $S.Tried.Add($Site.Id) }
function Model-Unresolved($S, [string]$Type) { foreach ($site in $S.Active) { if ($site.Type -ceq $Type -and $site.Stage -eq 'Q') { return $true } }; $false }
function Model-PromoteNext($S) {
 $S.Wait = $false
 if ($S.Attempts -ge $S.Cap) { return $false }
 while ($S.Cursor -lt $S.Order.Count) {
  $site = $S.Sites[$S.Order[$S.Cursor]]
  if ($site.Stage -ne 'R') { $S.Cursor++; continue }
  if (!$S.Legacy) {
   $verdict = $S.Types[$site.Type]
   if ($verdict -ne 'good' -and (Model-Unresolved $S $site.Type)) { $S.Wait = $true; return $false }
   $S.Cursor++
   if ($verdict -eq 'failed') { $site.Deferred = $true; $S.Deferred.Add($site); continue }
  } else { $S.Cursor++ }
  Model-Queue $S $site
  return $true
 }
 while ($S.DCursor -lt $S.Deferred.Count) {
  $later = $S.Deferred[$S.DCursor]; $S.DCursor++
  if ($later.Stage -eq 'R') { Model-Queue $S $later; return $true }
 }
 $false
}
function Model-FillQueue($S) {
 while ($S.Owed -gt 0 -and (Model-PromoteNext $S)) { $S.Owed-- }
 if (!$S.Wait) { $S.Owed = 0 }
}
function Model-NoteType($S, $Site, [bool]$Good) {
 if ($Good) { $S.Types[$Site.Type] = 'good'; return }
 if ($S.Types[$Site.Type] -ne 'good') { $S.Types[$Site.Type] = 'failed' }
}
function Model-End($S, $Site) { $Site.Stage = 'F'; [void]$S.Active.Remove($Site); $S.Owed++; Model-FillQueue $S }
# 0.1.14 judged the first squad only when the building's spawn turn came.
function Model-Analysed($S, $Site) {
 if ($Site.Posts -eq 0) { if (!$S.Legacy) { Model-NoteType $S $Site $false }; Model-End $S $Site; return }
 if ($Site.Posts -lt $Smallest -and !$S.Legacy) { Model-NoteType $S $Site $false; Model-End $S $Site; return }
 if (!$S.Legacy) { Model-NoteType $S $Site $true }
 $Site.Stage = 'Y'
}
function Model-Spawn($S, $Site) {
 if ($Site.Posts -lt $Smallest) { Model-End $S $Site; return }
 $Site.Stage = 'D'; [void]$S.Active.Remove($Site)
}
function Invoke-Generation([object[]]$Mix, [int]$Target, [int]$Seed, [int]$Schedule, [bool]$Legacy) {
 $S = New-State $Mix $Target $Seed $Legacy
 $events = [Random]::new($Schedule)
 $S.Owed = $Target; Model-FillQueue $S
 for ($step = 0; ; $step++) {
  Assert ($step -lt 5000) 'the model generation must end'
  $open = @($S.Active | Where-Object { $_.Stage -eq 'Q' -or $_.Stage -eq 'Y' })
  if ($open.Count -eq 0) {
   Assert (!($S.Owed -gt 0 -and $S.Wait)) 'a decision never waits without a building of its type being analysed'
   break
  }
  $pick = $open[$events.Next($open.Count)]
  if ($pick.Stage -eq 'Q') { Model-Analysed $S $pick } else { Model-Spawn $S $pick }
  if ($S.Owed -gt 0) { Model-FillQueue $S }
 }
 $placed = @($S.Sites | Where-Object Stage -eq 'D' | ForEach-Object Id | Sort-Object)
 [pscustomobject]@{ Placed = $placed.Count; Key = ($placed -join ','); Tried = (@($S.Tried | Sort-Object) -join ','); Attempts = $S.Attempts; Cap = $S.Cap }
}

# The 49 eligible buildings within 150 m of 11100,12300 on Chernarus Minus (type, count, posts).
$krasnostav = @(
 @('Shed_01', 12, 2), @('Shed_02_square', 3, 2), @('Shed_02_line', 1, 2), @('ShedMilitary_E_01', 1, 3), @('House_Wooden_USSR_1I01_panels', 2, 0),
 @('House 05', 4, 3), @('House A Red and Green', 3, 3), @('house_yellow', 3, 5), @('House 08', 3, 5), @('House11_g', 2, 3), @('house10_r', 2, 3),
 @('FarmHouse_USSR_1L01', 2, 6), @('Pub Rural', 2, 8), @('House2floors_01', 2, 8), @('Church Rural 01', 1, 6), @('House 02', 1, 3),
 @('House_Green_2s', 1, 6), @('House_Town_E_2I03', 1, 9), @('MunicipalOffice_E_01_beige', 1, 12), @('House 03', 1, 3), @('House 07', 1, 3))
$eligible = ($krasnostav | ForEach-Object { $_[1] } | Measure-Object -Sum).Sum
$fitting = ($krasnostav | Where-Object { $_[2] -ge $Smallest } | ForEach-Object { $_[1] } | Measure-Object -Sum).Sum
Assert ($eligible -eq 49 -and $fitting -eq 16) "Krasnostav model: 49 eligible, 16 fit a 4-man team ($eligible, $fitting)"
$seeds = 1..120
$legacyShort = 0; $legacyOne = 0; $newShort = 0; $maxTries = 0
foreach ($seed in $seeds) {
 $old = Invoke-Generation $krasnostav 4 $seed 1 $true
 Assert ($old.Attempts -le 12) "0.1.14 tried at most 12 buildings for 4 (seed ${seed}: $($old.Attempts))"
 if ($old.Placed -lt 4) { $legacyShort++ }
 if ($old.Placed -le 1) { $legacyOne++ }
 $runs = @(1, 7, 99 | ForEach-Object { Invoke-Generation $krasnostav 4 $seed $_ $false })
 Assert (@($runs | Select-Object -ExpandProperty Key -Unique).Count -eq 1 -and @($runs | Select-Object -ExpandProperty Tried -Unique).Count -eq 1) "seed ${seed} must repeat its buildings whatever analysis finishes first: $($runs.Key -join ' / ')"
 $now = $runs[0]
 if ($now.Placed -lt 4) { $newShort++ }
 Assert ($now.Attempts -le $now.Cap -and $now.Cap -eq 20) "at most 20 tries for 4 of 49 (seed ${seed}: $($now.Attempts)/$($now.Cap))"
 if ($now.Attempts -gt $maxTries) { $maxTries = $now.Attempts }
}
Assert ($newShort -eq 0) "every seed reaches 4 buildings around Krasnostav ($newShort short)"
Assert ($legacyShort -ge 12 -and $legacyOne -ge 1) "the model reproduces the 0.1.14 shortfall ($legacyShort of $($seeds.Count) seeds short, $legacyOne with one building or none)"
Assert ($maxTries -le 16) "with types put back, Krasnostav needs at most 12 failed types plus 4 tries ($maxTries)"

# Nothing fits: the zone stops at the limit with every try reported, and never waits forever.
$sheds = @(@('Shed_01', 30, 2), @('Shed_02_square', 10, 2), @('House_Wooden_USSR_1I01_panels', 9, 0))
foreach ($seed in 1..10) {
 $none = Invoke-Generation $sheds 4 $seed 3 $false
 Assert ($none.Placed -eq 0 -and $none.Attempts -eq 20) "nothing fits: 20 tries, no building (seed ${seed}: $($none.Attempts))"
}
# Plenty of room: no extra try, one of each type waits only for its first verdict.
$houses = @(@('House A', 20, 8), @('House B', 20, 6))
foreach ($seed in 1..10) {
 $full = Invoke-Generation $houses 4 $seed 5 $false
 Assert ($full.Placed -eq 4 -and $full.Attempts -eq 4) "fitting buildings: exactly 4 tries (seed ${seed}: $($full.Attempts))"
}
# A large target: at most 96 tries, the target reached when enough buildings fit.
$town = @(@('Shed_01', 120, 2), @('Shed_02_square', 40, 2), @('House_Wooden_USSR_1I01_panels', 20, 0), @('house_yellow', 40, 5), @('Pub Rural', 30, 8))
foreach ($seed in 1..5) {
 $big = Invoke-Generation $town 32 $seed 11 $false
 Assert ($big.Placed -eq 32 -and $big.Attempts -le 96) "32 buildings in a shed-heavy town (seed ${seed}: $($big.Placed) in $($big.Attempts) tries)"
}

# Native fixture wiring (executed only by the orchestrator): GM_Eden town, three seeds of
# fire teams and squads reach 4 buildings, the first seed repeats, squads only explain a miss.
$fixturePath = Join-Path $repo 'tests/EXPG_RandomGarrisonFillGameplay.c'
$fixture = Read-Text $fixturePath
$expect = '\[EXPG RANDOM FILL RESULT\] checks=[1-9]\d* failures=0 seeds=3 reached=3 repeated=1 reported=1 explained=1 maxTries=\d+ maxFailed=\d+ reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EXPG_RandomGarrisonFillGameplay.c -ExpectResult '$expect' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'fill fixture header must carry its runner command'
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity' -and $fixture -match 'Resource zoneResource = Resource\.Load\(ZONE_PREFAB\);') 'fill fixture keeps the runner driver class names and loads the real prefab'
Assert ($fixture -match 'cap == EXPG_RGRules\.AttemptCap\(TARGET, eligible\) && tries <= cap' -and $fixture -match 'status\.Contains\(string\.Format\("%1 failed: ", failed\)\)' -and $fixture -match 'status\.Contains\("none left"\) \|\| status\.Contains\("tries reached"\)') 'fill fixture checks the limit, the reasons in the status and the explanation of a miss'
$resultFirst = [regex]::Match($fixture, 'string first = string\.Format\("([^"]*)"').Groups[1].Value
$resultSecond = [regex]::Match($fixture, 'string second = string\.Format\("([^"]*)"').Groups[1].Value
Assert ($fixture.Contains('PrintFormat("[EXPG RANDOM FILL RESULT] %1 %2", first, second);') -and $resultFirst -and $resultSecond) 'fill fixture prints its RESULT line in two parts'
function Format-FillResult([string[]]$A, [string[]]$B) {
 $one = $resultFirst; for ($i = $A.Count; $i -ge 1; $i--) { $one = $one.Replace("%$i", $A[$i - 1]) }
 $two = $resultSecond; for ($i = $B.Count; $i -ge 1; $i--) { $two = $two.Replace("%$i", $B[$i - 1]) }
 "[EXPG RANDOM FILL RESULT] $one $two"
}
Assert ((Format-FillResult @('31', '0', '3', '3', '1') @('1', '1', '9', '5', 'completed')) -match $expect) 'a passing fill RESULT line matches the regex'
Assert (!((Format-FillResult @('31', '0', '3', '2', '1') @('1', '1', '20', '16', 'completed')) -match $expect) -and !((Format-FillResult @('31', '0', '3', '3', '0') @('1', '1', '9', '5', 'completed')) -match $expect) -and !((Format-FillResult @('31', '1', '3', '3', '1') @('1', '1', '9', '5', 'completed')) -match $expect)) 'a short, unrepeated or failing fill run must not match'
$fixtureBytes = [IO.File]::ReadAllBytes($fixturePath)
Assert (@($fixtureBytes | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'fill fixture is ASCII with LF'
foreach ($line in (Get-Code $fixture) -split "`n") {
 $code = $line -replace '"(?:[^"\\]|\\.)*"', '""'
 Assert (!($code -match '\S.*\breturn\s+[^;\s]' -and $code -notmatch '^\s*return\b')) "non-void return must be on its own line in the fill fixture: $($line.Trim())"
}
Assert (![regex]::IsMatch((Get-Code $fixture), '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|short|Building|World|Faction|Group|Player)\b')) 'fill fixture avoids reserved and vanilla type names'

# Native stop fixture wiring: Busy wording, Stop by another Game Master, and the AI-limit
# stop that stays Stopped with one notice (the AI limit set below the next squad).
$stopPath = Join-Path $repo 'tests/EXPG_RandomGarrisonStopGameplay.c'
$stopFixture = Read-Text $stopPath
$stopCode = Get-Code $stopFixture
$stopExpect = '\[EXPG RANDOM STOP RESULT\] checks=[1-9]\d* failures=0 busyRun=1 busyClear=1 stopTold=1 stopSelf=1 aiStopped=1 aiStayed=1 reason=completed'
Assert ($stopFixture.Contains("-FixturePath tests/EXPG_RandomGarrisonStopGameplay.c -ExpectResult '$stopExpect' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'stop fixture header must carry its runner command'
Assert ($stopCode -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $stopCode -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity' -and $stopCode -match 'Resource zoneResource = Resource\.Load\(ZONE_PREFAB\);') 'stop fixture keeps the runner driver class names and loads the real prefab'
Assert ($module -match 'protected void Tell\(int playerId, string message\)' -and $stopCode -match 'override protected void Tell\(int playerId, string message\)\s*\{[^}]*\}\s*super\.Tell\(playerId, message\);' -and $stopCode -match 'm_aEXPGTestTold\.Count\(\) < 64') 'stop fixture records every notice through the module''s Tell signature (bounded) and still gives it'
foreach ($needle in 'STOPPED_BY_GM = "Stopped (stopped by the Game Master)"', 'STOPPED_BY_LIMIT = "Stopped (the AI limit was reached"', '"): wait for Done or use Stop first"', '"): wait until it has finished"') { Assert $stopCode.Contains($needle) "stop fixture expects the module's words: $needle" }
Assert ($moduleRaw.Contains('StopGeneration("stopped by the Game Master");') -and $moduleRaw.Contains('StopGeneration("the AI limit was reached (" + limit + ")");') -and $moduleRaw.Contains('DoneText("Stopped (" + reason + ")")')) 'the module still words the stops as the stop fixture expects'
Assert ($stopCode -match 'Zone\.Run\(EXPG_RandomGarrisonModule\.ACTION_STOP, OTHER_GM\)' -and $stopCode -match 'told == 2 && Zone\.EXPG_TestToldTo\(0\) == STARTER && Zone\.EXPG_TestToldTo\(1\) == OTHER_GM' -and $stopCode -match 'notices == 1 && Zone\.EXPG_TestToldTo\(0\) == STARTER') 'stop fixture: a Stop by another Game Master tells both, a Stop by the starter tells one'
Assert ($stopCode -match 'WorldAI\.SetLimitOfActiveAIs\(active \+ 1\);' -and $stopCode -match 'if \(state == EXPG_RandomGarrisonModule\.STATE_DONE\)\s*\{\s*Check\(false' -and $stopCode -match 'Zone\.EXPG_TestToldCount\(\) == 1 && Zone\.EXPG_TestTold\(0\)\.StartsWith\(STOPPED_BY_LIMIT\)' -and $stopCode -match 'if \(Now\(\) - PhaseStarted < 3\) \{ return; \}') 'stop fixture: under the AI limit the zone ends Stopped (Done fails), with one notice, and is still Stopped 3 s later'
Assert ((Get-Body $stopCode 'void\s+Finish\s*\(') -match 'RestoreLimit\(\);' -and (Get-Body $stopCode 'void\s+RestoreLimit\s*\(') -match 'WorldAI\.SetLimitOfActiveAIs\(OriginalLimit\);') 'stop fixture restores the AI limit whatever the outcome'
$stopFirst = [regex]::Match($stopFixture, 'string first = string\.Format\("([^"]*)"').Groups[1].Value
$stopSecond = [regex]::Match($stopFixture, 'string second = string\.Format\("([^"]*)"').Groups[1].Value
Assert ($stopFixture.Contains('PrintFormat("[EXPG RANDOM STOP RESULT] %1 %2", first, second);') -and $stopFirst -and $stopSecond) 'stop fixture prints its RESULT line in two parts'
function Format-StopResult([string[]]$A, [string[]]$B) {
 $one = $stopFirst; for ($i = $A.Count; $i -ge 1; $i--) { $one = $one.Replace("%$i", $A[$i - 1]) }
 $two = $stopSecond; for ($i = $B.Count; $i -ge 1; $i--) { $two = $two.Replace("%$i", $B[$i - 1]) }
 "[EXPG RANDOM STOP RESULT] $one $two"
}
Assert ((Format-StopResult @('24', '0', '1', '1') @('1', '1', '1', '1', 'completed')) -match $stopExpect) 'a passing stop RESULT line matches the regex'
Assert (!((Format-StopResult @('24', '0', '1', '1') @('1', '1', '0', '1', 'completed')) -match $stopExpect) -and !((Format-StopResult @('24', '0', '1', '1') @('1', '1', '1', '0', 'completed')) -match $stopExpect) -and !((Format-StopResult @('24', '1', '1', '1') @('1', '1', '1', '1', 'completed')) -match $stopExpect) -and !((Format-StopResult @('24', '0', '1', '1') @('1', '1', '0', '0', 'done')) -match $stopExpect)) 'an AI-limit stop that ends Done, gives two notices or fails a check must not match'
$stopBytes = [IO.File]::ReadAllBytes($stopPath)
Assert (@($stopBytes | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'stop fixture is ASCII with LF'
foreach ($line in $stopCode -split "`n") {
 $code = $line -replace '"(?:[^"\\]|\\.)*"', '""'
 Assert (!($code -match '\S.*\breturn\s+[^;\s]' -and $code -notmatch '^\s*return\b')) "non-void return must be on its own line in the stop fixture: $($line.Trim())"
}
Assert (![regex]::IsMatch($stopCode, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|short|Building|World|Faction|Group|Player)\b')) 'stop fixture avoids reserved and vanilla type names'

"PASS: Random Garrison fill: up to AttemptCap tries (20 for 4 of 49, 96 for 32), failed building types put back for a second pass, the first squad judged when the analysis is ready, reproducible whatever analysis finishes first; Krasnostav model: 0.1.14 short on $legacyShort of $($seeds.Count) seeds ($legacyOne with one building or none), now 0 short in at most $maxTries tries; status, log and Game Master notice carry the failures by reason; an AI-limit stop stays Stopped with one notice; Busy and Stop notices point to what works; native fill and stop fixtures wired. No engine was launched."
