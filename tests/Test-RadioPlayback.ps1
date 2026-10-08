#requires -Version 7.0
# Portable guard for finite Ambient Sounds playback (placed radios, TVs, crowds, placed sounds) in
# EAS_RadioRuntime and EAS_RadioState: the client keeps radio chatter going in busy scenes.
# - Eviction: an engine end at least 2 s before the recording's end, heard in range, is an eviction
#   (reason=evicted) when the real clock agrees (world time can lag the audio after a hitch). It
#   retries in 3 s, doubling to 30 s while evictions follow each other, but never later than the
#   recording's own end plus its pause (the schedule before eviction handling; 0.1.14 lost 166 s of
#   a 178 s recording). A recording heard to its end, or a voice that lasted 60 s, ends the streak.
#   The eviction path never re-arms a one-shot or restarts the module.
# - Edge: starts keep a 10% margin inside the audible range (27 of 30 m); playing voices keep the
#   full range. A refused start below audibility (AudioSystem.IsAudible < 0, or the listener at the
#   edge) is not a failure and retries in 3 s; other refusals back off 5 ... 30 s and never park.
#   Only invalid recording metadata parks a module, after three tries.
# - Logs: refused starts (and an inaudible streak reaching ten) spend a budget of two normal lines
#   per placement (first and third notice); only a settings change (Restart) refills it.
# - Voices: up to four finite sources play at once (several radios together), oldest due first.
# - tests/EAS_RadioStateGameplay.c (22 checks) and tests/EAS_RadioRetryGameplay.c (12 checks) prove
#   the same state rules natively (Run-Gameplay -FixturePath).
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/ambient-sounds/Scripts/Game/EXPAS'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
# Line comments removed; string contents kept (reasons and log formats are checked).
function Read-Code([string]$Path) { [regex]::Replace([IO.File]::ReadAllText($Path), '(?m)^\s*//.*$|(?<=[;{}])\s*//[^\r\n"]*$', '') }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Squash([string]$Text) { [regex]::Replace($Text, '\s+', ' ') }

# State rules.
$state = Read-Code (Join-Path $scripts 'EAS_RadioState.c')
Assert ((Squash (Get-Body $state 'static\s+float\s+StartRange\s*\(\s*float\s+range\s*\)')).Trim() -ceq 'return range - Math.Max(range * 0.1, 1);') 'StartRange must keep a 10% (at least 1 m) margin inside the audible range'
Assert ((Squash (Get-Body $state 'static\s+bool\s+EndedEarly\s*\(\s*float\s+now\s*,\s*float\s+recordingEnd\s*\)')).Trim() -ceq 'return now < recordingEnd - 2;') 'EndedEarly must treat an end 2 s or more before the recording as early'
Assert ((Squash (Get-Body $state 'bool\s+EndedEarlyHeard\s*\(\s*float\s+heard\s*\)')).Trim() -ceq 'return m_StartedAt >= 0 && m_RecordingEnd >= 0 && EndedEarly(m_StartedAt + heard, m_RecordingEnd);') 'EndedEarlyHeard must apply EndedEarly to the real time heard since the start, and only while a recording is playing'
$canStart = Get-Body $state 'bool\s+CanStart\s*\(\s*float\s+now\s*,\s*int\s+voices\s*\)'
Assert ($canStart -cmatch '\bvoices\s*<\s*4\b' -and $canStart -cmatch '\bm_StartFailures\s*<\s*3\b') 'CanStart must keep the four-voice budget and the metadata park'
Assert ([regex]::Matches($state, 'm_StartFailures\s*\+\+').Count -eq 1 -and (Get-Body $state 'int\s+FailedStart\s*\(\s*float\s+now\s*\)') -cmatch 'm_StartFailures\s*\+\+') 'only FailedStart (invalid metadata) may count towards the park'
$refused = Squash (Get-Body $state 'int\s+Refused\s*\(\s*float\s+now\s*\)')
Assert ($refused -cmatch 'm_Refusals\+\+;' -and $refused -cmatch 'NextDue = now \+ Math\.Min\(5 \* m_Refusals, 30\);' -and $refused -notmatch 'm_StartFailures|m_Notices') 'Refused must back off 5 ... 30 s without parking'
$inaudible = Squash (Get-Body $state 'int\s+Inaudible\s*\(\s*float\s+now\s*\)')
Assert ($inaudible -cmatch 'NextDue = now \+ 3;' -and $inaudible -notmatch 'm_StartFailures|m_Refusals|m_Notices') 'Inaudible must retry in 3 s without counting a failure'
$evicted = (Squash (Get-Body $state 'float\s+Evicted\s*\(\s*float\s+now\s*\)')).Trim()
$evictedWanted = 'if (m_StartedAt < 0 || now - m_StartedAt >= 60) m_EvictionRetry = 0; m_EvictionRetry = Math.Clamp(m_EvictionRetry * 2, 3, 30); NextDue = now + m_EvictionRetry; if (m_RecordingEnd >= 0) NextDue = Math.Min(NextDue, m_RecordingEnd + PauseSeconds); m_RecordingEnd = -1; return NextDue - now;'
Assert ($evicted -ceq $evictedWanted) "Evicted must retry in 3 s, doubling to 30 s, never later than the recording's end plus its pause, with a new streak after 60 s (got: $evicted)"
Assert ($evicted -notmatch 'm_Played|m_StartFailures|m_Notices') 'Evicted must not re-arm a one-shot, touch the metadata park or refill the log budget'
Assert ((Squash (Get-Body $state 'void\s+Completed\s*\(\s*\)')).Trim() -ceq 'm_EvictionRetry = 0;') 'Completed must end the eviction streak'
$notice = (Squash (Get-Body $state 'bool\s+Notice\s*\(\s*\)')).Trim()
Assert ($notice -ceq 'if (m_Notices >= 3) return false; m_Notices++; return m_Notices != 2;') 'Notice must allow only the first and third normal log line'
Assert ([regex]::Matches($state, 'm_Notices\s*=\s*0\s*;').Count -eq 1 -and [regex]::Matches($state, 'm_Notices\s*\+\+').Count -eq 1) 'only Restart may refill the log budget and only Notice may spend it'
$started = Squash (Get-Body $state 'void\s+Started\s*\(')
foreach ($reset in 'm_StartFailures = 0;', 'm_Refusals = 0;', 'm_InaudibleStreak = 0;', 'm_StartedAt = now;') { Assert $started.Contains($reset) "Started must set $reset" }
Assert ($started -notmatch 'm_Notices|m_EvictionRetry') 'a start must neither refill the log budget nor end the eviction streak'
$restart = Squash (Get-Body $state 'void\s+Restart\s*\(')
foreach ($reset in 'm_StartFailures = 0;', 'm_Refusals = 0;', 'm_InaudibleStreak = 0;', 'm_Notices = 0;', 'm_StartedAt = -1;', 'm_EvictionRetry = 0;') { Assert $restart.Contains($reset) "Restart must set $reset" }

# Runtime wiring.
$runtimeText = Read-Code (Join-Path $scripts 'EAS_RadioRuntime.c')
$tick = Get-Body $runtimeText 'protected\s+void\s+Tick\s*\(\s*\)'
$flat = Squash $tick
Assert $flat.Contains('bool outside = distance < 0 || distance > module.AudibleRange();') 'a playing voice must keep its full audible range'
Assert $flat.Contains('if (range < 0 || range > EAS_RadioState.StartRange(candidate.AudibleRange())) continue;') 'starts must stay a margin inside the audible range'
$evictionHeader = 'if (!outside && EAS_RadioState.EndedEarly(now, module.End) && module.State.EndedEarlyHeard(System.GetTickCount(module.StartTick) * 0.001))'
Assert $flat.Contains($evictionHeader) 'an eviction must be an early end heard in range on both the world and the real clock'
$eviction = (Squash (Get-Body $tick 'if\s*\(\s*!outside\s*&&\s*EAS_RadioState\.EndedEarly\s*\(')).Trim()
Assert ($eviction -cmatch '^Stop\(module, false, "evicted"\); float retry = module\.State\.Evicted\(now\);' -and $eviction -cmatch 'continue;$') 'an eviction must stop with reason=evicted, call State.Evicted and skip the finished path'
Assert ($eviction -notmatch 'Restart\(|Interrupted\(|Completed\(|m_Played|FailedStart\(|Refused\(') 'the eviction path must not restart the module, re-arm a one-shot, end the streak or count a failure'
$finishedAt = $flat.IndexOf('string reason = "finished";')
Assert ($flat.IndexOf($evictionHeader) -ge 0 -and $flat.IndexOf($evictionHeader) -lt $finishedAt) 'the eviction test must run before the finished/inaudible path'
Assert ($flat -cmatch 'string reason = "finished"; if \(outside\) reason = "inaudible"; Stop\(module, outside, reason\); if \(!outside\) module\.State\.Completed\(\); module\.State\.NextDue = Math\.Max\(module\.State\.NextDue, now\);') 'a recording heard to its end (not left behind) must end the eviction streak'
Assert ([regex]::Matches($tick, '\.Completed\s*\(').Count -eq 1) 'only the finished path may end the eviction streak'
Assert $flat.Contains('radio.End = now + duration + 0.25; radio.StartTick = callStarted; radio.State.Started(now, duration, recording);') 'a start must record its real-clock tick for the eviction test'
$module = Read-Code (Join-Path $scripts 'EAS_RadioModule.c')
Assert ($module -cmatch '(?m)^\s*int StartTick;') 'EAS_RadioModule must keep the start tick next to Handle and End'
$refusal = Get-Body $tick 'if\s*\(\s*radio\.Handle\s*==\s*AudioHandle\.Invalid\s*\)'
$refusalFlat = Squash $refusal
Assert ($refusalFlat.Contains('bool belowAudibility = failedDistance < 0 || failedDistance > EAS_RadioState.StartRange(radio.AudibleRange()) || AudioSystem.IsAudible(radio.AudioProject(), eventName, transform[3]) < 0;')) 'a refusal must be classified as inaudible by IsAudible or the start edge'
Assert ($refusalFlat.Contains('if (belowAudibility) streak = radio.State.Inaudible(now); else streak = radio.State.Refused(now);')) 'an inaudible refusal must not be counted by Refused; other refusals use Refused'
Assert ([regex]::Matches($refusal, '\.Inaudible\s*\(').Count -eq 1 -and [regex]::Matches($refusal, '\.Refused\s*\(').Count -eq 1 -and $refusal -notmatch 'FailedStart|Restart\(') 'a refusal must never use the metadata park or restart the module'
Assert ([regex]::Matches($tick, '\.FailedStart\s*\(').Count -eq 1) 'only the invalid-metadata path may call FailedStart'
Assert ($refusalFlat -cmatch 'bool notice = !belowAudibility \|\| streak == 10; if \(notice && radio\.State\.Notice\(\)\) PrintFormat\("\[EAS\] Radio start failed: event=%1 distance=%2 attempt=') 'refused starts, or ten inaudible refusals in a row, log only within the per-placement budget'
Assert ($refusalFlat -cmatch 'else if \(\(notice \|\| streak == 1\) && EAS_Diagnostics\.Enabled\(radio\)\)') 'further refusals go to the debug trace only'
Assert ($refusalFlat.TrimEnd() -cmatch 'continue;$') 'a refused start must not fall through to the start bookkeeping'
Assert ([regex]::Matches($runtimeText, '\bPrint(Format)?\s*\(').Count -eq 1) 'EAS_RadioRuntime keeps a single log call'
Assert $flat.Contains('for (int attempt = 0; attempt < 4 && voices < 4; attempt++)') 'several finite sources must be able to start in one tick within the four-voice budget'
Assert ($flat -cmatch 'if \(!radio \|\| candidate\.State\.NextDue < radio\.State\.NextDue\) radio = candidate;') 'the oldest due source must win a free voice'
Assert ($flat.Contains('if (m_Diagnostics) m_Diagnostics.Evicted++;')) 'evictions must be counted in the diagnostics window'
$diagnostics = Read-Code (Join-Path $scripts 'EAS_Diagnostics.c')
Assert ($diagnostics -cmatch 'stops=%6 rejected=%7 evicted=%8"' -and $diagnostics -cmatch 'Evicted = 0;') 'the finite summary must report and reset evictions'

# Native fixtures for the same rules.
# Expected [EAS TEST CHECK] pass=1 lines on a completed run: Check calls other than the two
# Check(false, ...) guards (settings apply, deadline).
$fixtures = [ordered]@{
 'EAS_RadioRetryGameplay.c' = @{ Checks = 12; Calls = @('.Evicted(', '.Completed(', '.EndedEarlyHeard(', '.Notice(', '.Restart(', 'EndedEarly(') }
 'EAS_RadioStateGameplay.c' = @{ Checks = 22; Calls = @('StartRange(', 'EndedEarly(', '.Evicted(', '.Inaudible(', '.Refused(', '.FailedStart(', '.CanStart(') }
}
foreach ($name in $fixtures.Keys) {
 $fixture = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name))
 Assert ($fixture -cmatch 'class EXPG_GarrisonGameplayClass : GenericEntityClass' -and $fixture -cmatch 'class EXPG_GarrisonGameplay : GenericEntity') "$name must use the fixed fixture class names"
 Assert ([regex]::Matches($fixture, [regex]::Escape('"[EAS TEST RESULT] checks=%1 failures=%2 reason=%3"')).Count -eq 1) "$name must print one [EAS TEST RESULT] line"
 foreach ($call in $fixtures[$name].Calls) { Assert $fixture.Contains($call) "$name must exercise $call" }
 $code = Read-Code (Join-Path $PSScriptRoot $name)
 $checks = [regex]::Matches($code, '(?m)^\s*Check\(').Count - [regex]::Matches($code, '(?m)^\s*Check\(false,').Count
 Assert ($checks -eq $fixtures[$name].Checks -and [regex]::Matches($code, '(?m)^\s*Check\(false,').Count -eq 2) "$name must keep $($fixtures[$name].Checks) checks on a completed run (found $checks)"
}
Assert ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'EAS_RadioRetryGameplay.c')).Contains('[EAS TEST RESULT] line (12 checks)')) 'EAS_RadioRetryGameplay.c must state its check count'

'PASS: finite playback retries evicted voices in 3-30 s but never later than the recording end plus pause, ends the streak on a complete play, needs the real clock to agree, starts 10% inside the audible range, does not count inaudible refusals, logs two refused starts per placement, never parks on engine refusals (metadata only) and keeps four voices for several radios; native fixtures present.'
