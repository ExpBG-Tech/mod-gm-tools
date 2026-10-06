#requires -Version 7.0
# Portable guard for the deferred Garrison squad picker (0.1.8 live report: squad
# choices refused for about 90 s while a two-storey house was still analysed). The
# picker opens only when the building's plan is ready; until then the server sends
# the Game Master the analysis progress through owner RPCs. Every request joins the
# building's one plan, the oldest waiting request is served first, finished plans
# never take an analysis turn and the 4 ms analysis budget is unchanged. Also checks
# tests/EXPG_ScanProgressGameplay.c is wired to the runner. No engine is launched.
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

$editorPath = Join-Path $scripts 'EXPG_Editor.c'
$managerPath = Join-Path $scripts 'EXPG_GarrisonManager.c'
$planPath = Join-Path $scripts 'EXPG_BuildingPlan.c'
$editor = Read-Text $editorPath
$manager = Read-Text $managerPath
$plan = Read-Text $planPath

# Client: the request waits; only the server's ready RPC opens the content browser.
$openPicker = Get-Body $editor 'void\s+EXPG_OpenPicker\s*\('
Assert (!$openPicker.Contains('OpenBrowserLabelConfigInstance')) 'EXPG_OpenPicker must not open the squad picker before the plan is ready'
Assert ($openPicker -match 'Rpc\(EXPG_BeginServer,\s*m_EXPG_WaitNonce') 'EXPG_OpenPicker must send the waiting request to the server'
Assert ($editor -match '\[RplRpc\(RplChannel\.Reliable,\s*RplRcver\.Owner\)\]\s*protected\s+void\s+EXPG_OpenOwner\s*\(\s*int\s+nonce\s*\)') 'the squad picker must open through an owner RPC'
Assert ((Get-Body $editor 'protected\s+void\s+EXPG_OpenOwner\s*\(') -match 'OpenBrowserLabelConfigInstance\(browser\)') 'EXPG_OpenOwner must open the squad picker'
Assert ($editor -match '\[RplRpc\(RplChannel\.Reliable,\s*RplRcver\.Owner\)\]\s*protected\s+void\s+EXPG_ProgressOwner\s*\(\s*int\s+nonce,\s*int\s+percent,\s*bool\s+queued\s*\)') 'analysis progress must reach the Game Master through an owner RPC'

# Server: every request joins the building's plan; the selection ticket starts when it is ready.
$begin = Get-Body $editor 'protected\s+void\s+EXPG_BeginServer\s*\('
Assert ($begin -match 'manager\.Wait\(waiter,\s*reason\)' -and !$begin.Contains('m_EXPG_Ticket.Start')) 'EXPG_BeginServer must wait for the plan instead of starting the selection ticket'
$ready = Get-Body $editor 'bool\s+EXPG_WaitReady\s*\('
Assert ($ready.Contains('m_EXPG_Ticket.Start(nonce') -and $ready.Contains('Rpc(EXPG_OpenOwner, nonce)')) 'EXPG_WaitReady must start the ticket and open the picker'
Assert ($ready -match 's_EXPG_Pickers\.Find\(m_EXPG_BuildingID,\s*other\)\s*&&\s*other\s*&&\s*other\s*!=\s*this\)\s*return\s+false;') 'another Game Master choosing for the building must keep the request waiting'
Assert ((Get-Body $manager 'EXPG_BuildingPlan\s+Prepare\s*\(\s*IEntity\s+building\s*\)') -match 'EXPG_BuildingPlan\s+existing\s*=\s*FindPlan\(building\);') 'Prepare must reuse the building''s plan'
Assert ((Get-Body $manager 'protected\s+EXPG_BuildingPlan\s+NextAnalysis\s*\(') -match 'if\s*\(\s*plan\.Done\s*\)\s*\{\s*continue;\s*\}') 'finished plans must not take an analysis turn'
$service = Get-Body $manager 'protected\s+void\s+ServiceWaiters\s*\('
Assert ($service -match 'int\s+i\s*=\s*0;' -and $service -notmatch 'i--') 'waiting requests must be served oldest first'
Assert ((Get-Body $manager 'protected\s+void\s+Pump\s*\(') -match 'System\.GetTickCount\(\)\s*-\s*analysisStart\s*<\s*4\)') 'the analysis budget must stay 4 ms per pump'
Assert ($plan -match 'float\s+Progress\s*\(\s*\)') 'the plan must expose its progress'

# Enforce gotchas in the touched sources and the fixture.
$fixturePath = Join-Path $repo 'tests/EXPG_ScanProgressGameplay.c'
foreach ($path in @($editorPath, $managerPath, $planPath, $fixturePath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
$fixture = Read-Text $fixturePath
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'scan fixture must keep the runner driver class names'
Assert ($fixture -match [regex]::Escape("-FixturePath tests/EXPG_ScanProgressGameplay.c -ExpectResult '\[EXPG SCAN RESULT\] checks=[1-9]\d* failures=0 samples=[1-9]\d* decreases=0 plans=1 joined=1 readyA=1 readyB=1 queuedB=1 readyD=1 cancelled=0 cachedReady=1 cachedProgress=0 failedF=1 reason=completed' -TimeoutSeconds 420")) 'scan fixture header must carry its runner command'
Assert ($fixture.Contains('string.Format("checks=%1 failures=%2 samples=%3 decreases=%4 plans=%5 joined=%6"') -and $fixture.Contains('string.Format("readyA=%1 readyB=%2 queuedB=%3 readyD=%4 cancelled=%5 cachedReady=%6"') -and $fixture.Contains('string.Format("cachedProgress=%1 failedF=%2 reason=%3"') -and $fixture.Contains('PrintFormat("[EXPG SCAN RESULT] %1 %2 %3", first, second, third);')) 'scan fixture must print the RESULT line its regex expects'
Assert ($fixture -match 'Manager\.Wait\(waiter,\s*reason\)' -and $fixture -match 'class\s+EXPG_ScanWaiter\s*:\s*EXPG_PlanWaiter') 'scan fixture must drive the production Wait seam'
Assert ($fixture -match 'Resource\s+houseResource\s*=\s*Resource\.Load\(house\);') 'scan fixture must keep Resource.Load results in a local'
'PASS: the Garrison squad picker waits for the building''s analysis with owner-RPC progress, every request joins the one plan oldest first, finished plans never take an analysis turn, 4 ms budget kept; scan fixture wired.'
