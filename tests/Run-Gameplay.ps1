#requires -Version 7.0
param(
 [Parameter(Mandatory)][string]$SourceSnapshot,
 [switch]$OrchestratorSlotGranted,
 [switch]$FreshTrim,
 [switch]$BodyClearance,
 [switch]$UnitCleanup,
 # Unit cleanup or a custom fixture with -ExpectResult: also load RHS: Status Quo and both content packs, linked (never
 # copied, about 8 GB) by GUID from the installed addons, for the rhs-mg-team and
 # rhs-usmc-recon cases.
 [switch]$Rhs,
 [string]$FixturePath = '',
 # For fixtures with their own result line: a regex for exactly one passing result line,
 # e.g. '\[EUS TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed'.
 [string]$ExpectResult = '',
 [ValidateRange(300,600)][int]$TimeoutSeconds = 360
)
$ErrorActionPreference = 'Stop'
function Test-GameplayEvidence([string]$Text, [bool]$Trim) {
 # The base game's resupply stations log this when GM_Eden tears down after the
 # fixture finished. Tolerate only that exact stock line after the result marker.
 $result = $Text.IndexOf('[EXPG GAMEPLAY RESULT]')
 $checked = $Text
 if ($result -ge 0) { $checked = $Text.Substring(0, $result) + ($Text.Substring($result) -replace "(?m)^.*SCRIPT\s+\(E\): 'SCR_BaseResupplySupportStationComponent' needs a entity catalog manager!\r?$", '') }
 if ($Text -notmatch 'Game destroyed' -or $checked -match 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed') { return $false }
 $mode = [int]$Trim
 $requested = 4
 if ($Trim) { $requested = 12 }
 $case = [regex]::Match($Text, "\[EXPG CAPACITY CASE\] freshTrim=$mode requested=$requested expected=(\d+) capacity=(\d+)")
 if (!$case.Success) { return $false }
 $count = [int]$case.Groups[1].Value
 $capacity = [int]$case.Groups[2].Value
 if ($Trim) {
  if ($count -ne 9 -or $capacity -ne 9 -or $Text -notmatch '\[EXPG TRIM RESULT\] admitted=12 before=12 retained=9 acknowledged=3 deleted=3 leaderPreserved=1 originals=1') { return $false }
 } elseif ($count -ne 4 -or $capacity -lt 4) { return $false }
 $survivors = $count - 1
 return $Text -match "\[EXPG GAMEPLAY RESULT\] phase=10 checks=\d+ failures=0 actors=$count fixedPosts=[1-9]\d* reason=completed" -and
  $Text -match "\[EXPG FULL CYCLE\] cycle=1 living=$count transformParity=1 assignments=1" -and
  $Text -match "\[EXPG FULL CYCLE\] cycle=2 living=$survivors transformParity=1 assignments=1"
}
function Test-BodyClearanceEvidence([string]$Text) {
 if ($Text -notmatch 'Game destroyed' -or $Text -match 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed') { return $false }
 return $Text -match '\[EXPG BODY RESULT\] checks=\d+ failures=0 attempts=[1-9]\d* verifiedBlock=1 reason=completed' -and
  $Text -match '\[EXPG BODY CHECK\] pass=1 production body clearance rejects real solid wall' -and
  $Text -match '\[EXPG BODY CHECK\] pass=1 native wall deletion acknowledged' -and
  $Text -match '\[EXPG BODY CHECK\] pass=1 same point and geometry clear after wall deletion'
}
function Test-UnitCleanupEvidence([string]$Text, [bool]$Rhs = $false) {
 # Same stock GM_Eden teardown tolerance as Test-GameplayEvidence, only after the first result marker.
 # stdout, console.log and script.log each repeat the result; require one distinct result line.
 $results = @([regex]::Matches($Text, '\[EBG CLEANUP TEST RESULT\][^\r\n]*') | ForEach-Object { $_.Value.TrimEnd() } | Sort-Object -Unique)
 if ($results.Count -ne 1) { return $false }
 $result = $Text.IndexOf('[EBG CLEANUP TEST RESULT]')
 $checked = $Text.Substring(0, $result) + ($Text.Substring($result) -replace "(?m)^.*SCRIPT\s+\(E\): 'SCR_BaseResupplySupportStationComponent' needs a entity catalog manager!\r?$", '')
 if ($Text -notmatch 'Game destroyed' -or $checked -match 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed') { return $false }
 if ($Text -match '\[EBG CLEANUP TEST SURVIVOR DELETE\]|\[EBG CLEANUP TEST PARTIAL STRIP\]') { return $false }
 # Identity-less riders (RHS preset vests and weapon parts, their vanilla stand-ins) are left
 # out of the save; a provenance hold naming one of them is the 0.1.5 RHS regression.
 if ($Text -match '\[EBG CLEANUP PROVENANCE HOLD\][^\r\n]*(Stock_VZ58|Vest_Ratin6B45|RHS_PKP_|RHS_AK74M_|Handguard_AK100)') { return $false }
 # No fixture casualty carries a keep component or intel: a park is the B1 regression
 # (a storage-less cloth-slot accessory read as a mission-protected ownership chain).
 if ($Text -match '\[EBG CLEANUP KEEP\]') { return $false }
 # Storage-less cloth-slot accessories stay the casualty's own rows: held after a transfer
 # check, owned by their wearer, unprotected, and gone with the body (no lineage).
 $cloth = 'available=1 accessories=({0}) unlisted=\1 held=\1 wearerHolder=\1 protectedChain=0 blocking=0 owned=\d+'
 $usmcPassed = $Text -match ('\[EBG CLEANUP TEST CLOTH SLOT\] case=rhs-usmc-recon ' + ($cloth -f '[2-9]|[1-9]\d+')) -and
  $Text -match '\[EBG CLEANUP TEST CLOTH SLOT DELETED\] case=rhs-usmc-recon bodies=2 byEbg=2 accessoriesGone=1 clothGone=1 present=0 proven=1 lineage=0'
 $save = 'durable=(1 exported=1 issueFree=1 bodyRows={0}|0 exported=0 issueFree=0 bodyRows=0) riderRows=0'
 $rhsPassed = $Text -match ('\[EBG CLEANUP TEST RHS\] case=rhs-mg-team available=1 riders=[1-9]\d* blocking=0 ' + ($save -f 2) + ' owned=\d+') -and
  $Text -match '\[EBG CLEANUP TEST RHS DELETED\] case=rhs-mg-team bodies=2 byEbg=2 owned=([3-9]|[1-9]\d+) present=0 proven=1 lineage=0'
 # Without -Rhs the RHS cases only log that RHS is not loaded; with -Rhs they must pass.
 if ($Rhs -and (!$rhsPassed -or !$usmcPassed)) { return $false }
 if (!$Rhs -and !$rhsPassed -and $Text -notmatch '\[EBG CLEANUP TEST RHS\] case=rhs-mg-team available=0') { return $false }
 if (!$Rhs -and !$usmcPassed -and $Text -notmatch '\[EBG CLEANUP TEST CLOTH SLOT\] case=rhs-usmc-recon available=0') { return $false }
 # Per-casualty cleanup: each casualty (body + dropped items) goes whole in one tick, foreign content included.
 return $Text -match '\[EBG CLEANUP TEST RESULT\] checks=[1-9]\d* failures=0 cases=14 reason=complete' -and
  $Text -match ('\[EBG CLEANUP TEST CLOTH SLOT\] case=cloth-slot-accessory ' + ($cloth -f '[1-9]\d*')) -and
  $Text -match '\[EBG CLEANUP TEST CLOTH SLOT DELETED\] case=cloth-slot-accessory bodies=1 byEbg=1 accessoriesGone=1 clothGone=1 present=0 proven=1 lineage=0' -and
  $Text -match ('\[EBG CLEANUP TEST IDENTITYLESS\] case=identityless-gear vestStripped=[01] riders=[1-9]\d* blocking=0 ' + ($save -f 1)) -and
  $Text -match '\[EBG CLEANUP TEST IDENTITYLESS DELETED\] case=identityless-gear bodyByEbg=1 weaponGone=1 partGone=1 vestGone=1 present=0 sameTick=1 proven=1 lineage=0' -and
  $Text -match '\[EBG CLEANUP TEST ATOMIC\] case=us-etool-atomic owned=([2-9]|[1-9]\d+) etool=1 gone=1 outerDeletes=[1-9]\d* sameTick=1 partialStrips=0' -and
  $Text -match '\[EBG CLEANUP TEST FOREIGN\] case=foreign-item owned=([2-9]|[1-9]\d+) present=0 firstByEbg=1 secondByEbg=1 foreignGone=1' -and
  $Text -match '\[EBG CLEANUP TEST DELETED\] case=sim-partial-awake .*state=0 whileCached=0' -and
  $Text -match '\[EBG CLEANUP TEST DELETED\] case=sim-partial-cached .*state=1 whileCached=1' -and
  $Text -match '\[EBG CLEANUP TEST DELETED\] case=full-partial-cached .*state=2 whileCached=1' -and
  $Text -match '\[EBG CLEANUP TEST DELETED\] case=full-partial-awake-then-cache .*state=0 whileCached=0' -and
  ($Text -match '\[EBG CLEANUP TEST SNAPSHOT\] case=sim-death-while-cached heldWhileCached=1 restored=1 deletedByEbg=1' -or
   $Text -match '\[EBG CLEANUP TEST SNAPSHOT\] case=sim-death-while-cached knownLimitation=death-not-confirmed-while-suspended deletedByEbg=0')
}
function Test-CustomResultEvidence([string]$Text, [string]$Pattern) {
 # Generic verdict for module fixtures: one distinct passing result line, clean shutdown,
 # no script errors before it (same stock GM_Eden teardown tolerance after the result).
 $lines = @([regex]::Matches($Text, $Pattern) | ForEach-Object { $_.Value.TrimEnd() } | Sort-Object -Unique)
 if ($lines.Count -ne 1) { return $false }
 $result = [regex]::Match($Text, $Pattern).Index
 $checked = $Text.Substring(0, $result) + ($Text.Substring($result) -replace "(?m)^.*SCRIPT\s+\(E\): 'SCR_BaseResupplySupportStationComponent' needs a entity catalog manager!\r?$", '')
 return $Text -match 'Game destroyed' -and $checked -notmatch 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed'
}
if (!$OrchestratorSlotGranted) { throw 'Explicit orchestrator native-slot handoff required. This launches a diagnostic server.' }
if ($ExpectResult -and ($FreshTrim -or $BodyClearance -or $UnitCleanup)) { throw 'ExpectResult is for custom fixtures only.' }
if ($ExpectResult -and !$FixturePath) { throw 'ExpectResult requires -FixturePath.' }
if (([int][bool]$FreshTrim + [int][bool]$BodyClearance + [int][bool]$UnitCleanup) -gt 1) { throw 'Select one fixture kind.' }
if ($Rhs -and !$UnitCleanup -and !$ExpectResult) { throw 'Rhs is for the unit-cleanup fixture or a custom fixture with -ExpectResult.' }
# The unit-cleanup fixture runs 400 s of world time plus GM_Eden startup and shutdown.
if ($UnitCleanup -and !$PSBoundParameters.ContainsKey('TimeoutSeconds')) { $TimeoutSeconds = 540 }
if ($UnitCleanup -and $TimeoutSeconds -lt 480) { throw 'Unit-cleanup fixture needs -TimeoutSeconds 480 or more to keep its result and diagnostics.' }
$repo = Split-Path -Parent $PSScriptRoot
$config = & "$repo/tools/Get-LocalConfig.ps1"
$project = & "$repo/tools/Get-ProjectConfig.ps1"
$nativeNames = @('ArmaReforgerWorkbenchSteamDiag','ArmaReforgerSteam','ArmaReforgerSteamDiag','ArmaReforgerServer','ArmaReforgerServerDiag')
function Assert-NativeSlot {
 if (Get-Process -Name $nativeNames -ErrorAction SilentlyContinue) { throw 'Native slot occupied; no existing process will be stopped.' }
}
Assert-NativeSlot
if (!$FixturePath) {
 $FixturePath = Join-Path $PSScriptRoot 'EXPG_GarrisonGameplay.c'
 if ($BodyClearance) { $FixturePath = Join-Path $PSScriptRoot 'EXPG_BodyClearance.c' }
 if ($UnitCleanup) { $FixturePath = Join-Path $PSScriptRoot 'EBG_UnitCleanupGameplay.c' }
}
$FixturePath = (Resolve-Path -LiteralPath $FixturePath).Path
if (!(Test-Path -LiteralPath $FixturePath -PathType Leaf)) { throw 'FixturePath must identify a frozen gameplay script.' }
$source = (Resolve-Path -LiteralPath $SourceSnapshot).Path
if (!(Test-Path -LiteralPath "$source/resourceDatabase.rdb" -PathType Leaf)) { throw 'Use a built/indexed addon snapshot.' }
$sourceProject = Join-Path $source $project.addon.project
if (!(Test-Path -LiteralPath $sourceProject) -or (Get-Content -LiteralPath $sourceProject -Raw) -notmatch ('GUID\s+"?' + $project.addon.id + '\b')) { throw 'Snapshot must match this project identity.' }
$engine = Join-Path $config.ServerRoot 'ArmaReforgerServerDiag.exe'
if (!(Test-Path -LiteralPath $engine -PathType Leaf)) { throw 'Configure ServerRoot with the native diagnostic server installation.' }
# RHS: Status Quo needs both content packs. Resolve each installed folder by its project GUID.
$rhsSources = [ordered]@{}
if ($Rhs) {
 foreach ($id in @('595F2BF2F44836FB', '1337C0DE5DABBEEF', 'BADC0DEDABBEDA5E')) {
  $found = @(Get-ChildItem -LiteralPath $config.InstalledAddonsRoot -Directory | Where-Object {
   $projects = @(Get-ChildItem -LiteralPath $_.FullName -Filter '*.gproj' -File)
   $projects.Count -eq 1 -and (Get-Content -LiteralPath $projects[0].FullName -Raw) -cmatch ('\bGUID\s+"?' + $id + '\b')
  })
  if ($found.Count -ne 1) { throw "Install RHS addon $id (RHS: Status Quo and both content packs) in InstalledAddonsRoot before an -Rhs run." }
  $rhsSources[$id] = $found[0].FullName
 }
}
$rhsLinks = [Collections.Generic.List[string]]::new()
function Remove-RhsLinks {
 # Junctions only: removing the link never touches the installed RHS files.
 foreach ($link in $rhsLinks) {
  $item = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
  if ($item -and $item.LinkType -eq 'Junction') { $item.Delete() }
 }
}
$run = Join-Path $repo ('build/gameplay-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$addons = Join-Path $run 'addons'
New-Item -ItemType Directory -Path $run | Out-Null
& "$repo/tools/Copy-AddonDependencies.ps1" -Destination $addons -InstalledAddonsRoot $config.InstalledAddonsRoot
Copy-Item -LiteralPath $source -Destination (Join-Path $addons $project.addon.name) -Recurse
$fixture = Join-Path $addons 'EXPG_GameplayFixture'
$logs = Join-Path $run 'logs'
New-Item -ItemType Directory -Path "$fixture/Scripts/Game","$fixture/Worlds/Garrison_Layers",$logs | Out-Null
Copy-Item -LiteralPath $FixturePath -Destination "$fixture/Scripts/Game/EXPG_GarrisonGameplay.c"
if ($FreshTrim) {
 $prefabs = Join-Path $fixture 'Prefabs/Tests'
 New-Item -ItemType Directory -Path $prefabs | Out-Null
 foreach ($file in @('EXPG_TrimTwelve.et', 'EXPG_TrimTwelve.et.meta')) {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Prefabs/Tests/$file") -Destination $prefabs
 }
 $fixturePath = "$fixture/Scripts/Game/EXPG_GarrisonGameplay.c"
 $fixtureText = Get-Content -LiteralPath $fixturePath -Raw
 $marker = 'static const bool EXPG_TEST_FRESH_TRIM = false;'
 if ([regex]::Matches($fixtureText, [regex]::Escape($marker)).Count -ne 1) { throw 'Expected one fresh-trim fixture switch; no native process started.' }
 $fixtureText.Replace($marker, 'static const bool EXPG_TEST_FRESH_TRIM = true;') | Set-Content -LiteralPath $fixturePath -Encoding utf8NoBOM
}
# The fixture depends on the base game, any installed dependencies and this project.
$fixtureDependencies = (@('58D0FB3206B6F859') + @($project.addon.installedDependencies.Keys | Sort-Object) + @($project.addon.id) + @($rhsSources.Keys) | ForEach-Object { "  `"$_`"" }) -join "`n"
@"
GameProject {
 ID "EXPG_GameplayFixture"
 GUID "67CC618B744C46A1"
 TITLE "EXPBG Garrison local gameplay fixture"
 Dependencies {
$fixtureDependencies
 }
}
"@ | Set-Content -LiteralPath "$fixture/EXPG_GameplayFixture.gproj" -Encoding utf8NoBOM
@'
SubScene {
 Parent "{BEF094A5F7F3211B}worlds/GameMaster/GM_Eden.ent"
}
'@ | Set-Content -LiteralPath "$fixture/Worlds/Garrison.ent" -Encoding utf8NoBOM
@'
EXPG_GarrisonGameplay GarrisonDriver {
 coords 4773 169 7094
}
'@ | Set-Content -LiteralPath "$fixture/Worlds/Garrison_Layers/default.layer" -Encoding utf8NoBOM
'MetaFileClass { Name "{69C0B9B168C54EAB}Worlds/Garrison.ent" Configurations { ENTResourceClass PC {} ENTResourceClass HEADLESS : PC {} } }' | Set-Content -LiteralPath "$fixture/Worlds/Garrison.ent.meta" -Encoding utf8NoBOM
# Evidence hashes include exact candidate, dependencies and fixture, not just Git HEAD.
@(Get-ChildItem -LiteralPath $addons -Recurse -File | ForEach-Object {
 [ordered]@{path=$_.FullName.Substring($addons.Length + 1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}) | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$run/inputs.json"
if ($Rhs) {
 # The linked RHS payload is identified by its Workshop manifests, not hashed whole.
 @($rhsSources.Keys | ForEach-Object {
  $id = $_
  [ordered]@{id=$id;source=$rhsSources[$id];manifests=@(Get-ChildItem -LiteralPath $rhsSources[$id] -Filter '*manifest.json' -File | Sort-Object Name | ForEach-Object { [ordered]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} })}
 }) | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/rhs.json"
}
$arguments = @('-disableCrashReporter','-addonsDir',$addons,'-addons','67CC618B744C46A1','-profile',"$run/profile",'-logsDir',$logs,'-server','Worlds/Garrison.ent','-worldSystemsConfig','{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf','-maxFPS','60')
$start = [Diagnostics.ProcessStartInfo]::new($engine)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WorkingDirectory = $config.ServerRoot
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
Assert-NativeSlot
foreach ($id in $rhsSources.Keys) {
 $link = Join-Path $addons $id
 New-Item -ItemType Junction -Path $link -Target $rhsSources[$id] | Out-Null
 $rhsLinks.Add($link)
}
$processStarted = $false
try { $processStarted = $process.Start() } finally { if (!$processStarted) { Remove-RhsLinks } }
if (!$processStarted) { throw 'Diagnostic server failed to start.' }
$started = $process.StartTime.ToUniversalTime()
$receipt = [ordered]@{source=$source;run=$run;freshTrim=[bool]$FreshTrim;bodyClearance=[bool]$BodyClearance;unitCleanup=[bool]$UnitCleanup;rhs=[bool]$Rhs;pid=$process.Id;startedUtc=$started.ToString('o');executable=$engine;arguments=$arguments;timeoutSeconds=$TimeoutSeconds;timedOut=$false;ownedProcessStopped=$false;nativeExitCode=$null;passed=$false}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/run.json"
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try {
 if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
  $receipt.timedOut = $true
  # Only the process launched above may be stopped. Fail closed on identity drift.
  $current = Get-Process -Id $process.Id -ErrorAction Stop
  if ($current.StartTime.ToUniversalTime().Ticks -ne $started.Ticks -or [IO.Path]::GetFullPath($current.Path) -ine [IO.Path]::GetFullPath($engine)) { throw 'Timeout, but process ownership could not be verified; no process was stopped.' }
  $process.Kill($true)
  $receipt.ownedProcessStopped = $true
  if (!$process.WaitForExit(10000)) { throw 'Owned diagnostic process did not exit after timeout.' }
 }
 $receipt.nativeExitCode = $process.ExitCode
 $output = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
 [IO.File]::WriteAllText("$run/native-output.log", $output)
 $text = $output + "`n" + ((Get-ChildItem -LiteralPath $run -Recurse -Filter '*.log' -File | Where-Object Name -ne 'native-output.log' | Get-Content -Raw) -join "`n")
 $evidencePassed = Test-GameplayEvidence $text ([bool]$FreshTrim)
 if ($BodyClearance) { $evidencePassed = Test-BodyClearanceEvidence $text }
 if ($UnitCleanup) { $evidencePassed = Test-UnitCleanupEvidence $text ([bool]$Rhs) }
 if ($ExpectResult) { $evidencePassed = Test-CustomResultEvidence $text $ExpectResult }
 $receipt.passed = !$receipt.timedOut -and $process.ExitCode -eq 0 -and $evidencePassed
 $text -split "`n" | Where-Object { $_ -match '\[EXPG|\[EBG CLEANUP|SCRIPT\s+\(E\)|Can.t compile' } | Write-Output
} finally {
 # Never unlink under a running engine; an unverified timeout leaves the links for inspection.
 if ($process.HasExited) { Remove-RhsLinks }
 $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/result.json"
}
if (!$receipt.passed) { throw "Gameplay fixture not passed; inspect $run" }
if ($UnitCleanup -and $Rhs) { "PASS: bounded native Unit Caching cleanup fixture with RHS: Status Quo units (rhs-mg-team, rhs-usmc-recon; injected presence, no connected player, no GM UI, no save/load). Evidence: $run"; return }
if ($UnitCleanup) { "PASS: bounded native Unit Caching cleanup fixture only (injected presence, no connected player, no GM UI, no save/load; RHS cases not loaded, use -Rhs). Evidence: $run"; return }
"PASS: bounded native server smoke only. No GM UI, player-distance crossing, combat, multiplayer/JIP or persistence proof. Evidence: $run"
