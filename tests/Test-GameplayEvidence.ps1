#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$tokens = $null; $parseErrors = $null
$runner = [Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot/Run-Gameplay.ps1", [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Runner syntax is invalid.' }
$function = $runner.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-GameplayEvidence' }, $true)
if (!$function) { throw 'Missing gameplay evidence verifier.' }
# Load only the pure verifier; never execute the native runner in portable tests.
. ([scriptblock]::Create($function.Extent.Text))
# 0.1.11: a fresh twelve-man squad on nine planned posts is never trimmed; all twelve deploy.
function Evidence([int]$Trim, [int]$Count, [int]$Capacity = $Count) {
 $requested = 4
 if ($Trim) { $requested = 12 }
 @"
[EXPG CAPACITY CASE] freshTrim=$Trim requested=$requested expected=$Count capacity=$Capacity
[EXPG TRIM RESULT] admitted=12 before=12 retained=12 acknowledged=0 deleted=0 leaderPreserved=1 originals=1
[EXPG FULL CYCLE] cycle=1 living=$Count transformParity=1 assignments=1
[EXPG FULL CYCLE] cycle=2 living=$($Count-1) transformParity=1 assignments=1
[EXPG GAMEPLAY RESULT] phase=10 checks=40 failures=0 actors=$Count fixedPosts=1 reason=completed
Game destroyed
"@
}
$full = Evidence 0 4
$trim = Evidence 1 12 9
if (!(Test-GameplayEvidence $full $false) -or !(Test-GameplayEvidence $trim $true)) { throw 'Valid complete case rejected.' }
if (Test-GameplayEvidence $trim $false) { throw 'Trim case disguised as full-house pass.' }
if (Test-GameplayEvidence (Evidence 1 4) $true) { throw 'Four-man roster disguised as the twelve-man overflow case.' }
if (Test-GameplayEvidence (Evidence 1 9 9) $true) { throw 'Trimmed roster (nine of twelve) accepted.' }
if (Test-GameplayEvidence (Evidence 1 12 12) $true) { throw 'A house with room for all twelve accepted as the overflow case.' }
if (Test-GameplayEvidence ($trim.Replace('cycle=2','cycle=3')) $true) { throw 'Missing second cycle accepted.' }
if (Test-GameplayEvidence ($trim.Replace('living=11','living=12')) $true) { throw 'Resurrected casualty accepted.' }
if (Test-GameplayEvidence ($trim.Replace('requested=12','requested=4')) $true) { throw 'Fabricated larger roster accepted.' }
if (Test-GameplayEvidence ($trim.Replace('acknowledged=0','acknowledged=3')) $true) { throw 'A trimmed soldier accepted.' }
if (Test-GameplayEvidence ($trim.Replace('retained=12 acknowledged=0 deleted=0','retained=9 acknowledged=3 deleted=3')) $true) { throw 'Trimming accepted.' }
if (Test-GameplayEvidence ($trim.Replace('leaderPreserved=1','leaderPreserved=0')) $true) { throw 'Leader loss accepted.' }
if (Test-GameplayEvidence ($trim.Replace('originals=1','originals=0')) $true) { throw 'Invented survivor roster accepted.' }
if (Test-GameplayEvidence ($trim.Replace('transformParity=1','transformParity=0')) $true) { throw 'Wrong transforms accepted.' }
if (Test-GameplayEvidence ($trim + "`nSCRIPT (E): failure") $true) { throw 'Script error accepted.' }
if (Test-GameplayEvidence ($trim.Replace('Game destroyed','')) $true) { throw 'Incomplete shutdown accepted.' }
$teardown = "SCRIPT    (E): 'SCR_BaseResupplySupportStationComponent' needs a entity catalog manager!"
if (!(Test-GameplayEvidence ($trim + "`n" + $teardown) $true)) { throw 'Stock teardown line after the result rejected.' }
if (Test-GameplayEvidence ($teardown + "`n" + $trim) $true) { throw 'Stock line before the result accepted.' }
if (Test-GameplayEvidence ($trim + "`n" + $teardown + "`nSCRIPT (E): failure") $true) { throw 'Other teardown error accepted.' }
$bodyFunction = $runner.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-BodyClearanceEvidence' }, $true)
if (!$bodyFunction) { throw 'Missing body-clearance verifier.' }
. ([scriptblock]::Create($bodyFunction.Extent.Text))
$body = @'
[EXPG BODY CHECK] pass=1 production body clearance rejects real solid wall
[EXPG BODY CHECK] pass=1 native wall deletion acknowledged
[EXPG BODY CHECK] pass=1 same point and geometry clear after wall deletion
[EXPG BODY RESULT] checks=8 failures=0 attempts=1 verifiedBlock=1 reason=completed
Game destroyed
'@
if (!(Test-BodyClearanceEvidence $body)) { throw 'Complete body-clearance proof rejected.' }
foreach ($invalid in @($body.Replace('verifiedBlock=1','verifiedBlock=0'), $body.Replace('Game destroyed',''), $body.Replace('deletion acknowledged','deletion refused'), ($body + "`nSCRIPT (E): failure"))) {
 if (Test-BodyClearanceEvidence $invalid) { throw 'Incomplete body-clearance proof accepted.' }
}
if ((Test-BodyClearanceEvidence $full) -or (Test-GameplayEvidence $body $false)) { throw 'Distinct fixture kinds accepted as one another.' }
$cleanupFunction = $runner.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-UnitCleanupEvidence' }, $true)
if (!$cleanupFunction) { throw 'Missing unit-cleanup verifier.' }
. ([scriptblock]::Create($cleanupFunction.Extent.Text))
$cleanup = @'
[EBG CLEANUP TEST DELETED] case=sim-partial-awake bodies=1 byEbg=1 state=0 whileCached=0 afterLeave=40 afterDeath=43 cachedAt=-1
[EBG CLEANUP TEST DELETED] case=sim-partial-cached bodies=1 byEbg=1 state=1 whileCached=1 afterLeave=152 afterDeath=155 cachedAt=97
[EBG CLEANUP TEST DELETED] case=full-partial-cached bodies=1 byEbg=1 state=2 whileCached=1 afterLeave=153 afterDeath=156 cachedAt=99
[EBG CLEANUP TEST DELETED] case=full-partial-awake-then-cache bodies=1 byEbg=1 state=0 whileCached=0 afterLeave=40 afterDeath=43 cachedAt=-1
[EBG CLEANUP TEST SNAPSHOT] case=sim-death-while-cached heldWhileCached=1 restored=1 deletedByEbg=1
[EBG CLEANUP TEST FOREIGN] case=foreign-item owned=24 present=0 firstByEbg=1 secondByEbg=1 foreignGone=1 afterEligible=31
[EBG CLEANUP TEST ATOMIC] case=us-etool-atomic owned=31 etool=1 gone=1 outerDeletes=2 sameTick=1 partialStrips=0
[EBG CLEANUP TEST IDENTITYLESS] case=identityless-gear vestStripped=1 riders=4 blocking=0 durable=1 exported=1 issueFree=1 bodyRows=1 riderRows=0
[EBG CLEANUP TEST IDENTITYLESS DELETED] case=identityless-gear bodyByEbg=1 weaponGone=1 partGone=1 vestGone=1 present=0 sameTick=1 proven=1 lineage=0
[EBG CLEANUP TEST RHS] case=rhs-mg-team available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'
[EBG CLEANUP TEST CLOTH SLOT] case=cloth-slot-accessory available=1 accessories=1 unlisted=1 held=1 wearerHolder=1 protectedChain=0 blocking=0 owned=27
[EBG CLEANUP TEST CLOTH SLOT DELETED] case=cloth-slot-accessory bodies=1 byEbg=1 accessoriesGone=1 clothGone=1 present=0 proven=1 lineage=0
[EBG CLEANUP TEST CLOTH SLOT] case=rhs-usmc-recon available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'
[EBG CLEANUP TEST RESULT] checks=90 failures=0 cases=14 reason=complete
Game destroyed
'@
if (!(Test-UnitCleanupEvidence $cleanup)) { throw 'Complete unit-cleanup proof rejected.' }
# A world without durable zone/group identities still proves the shared save decision.
if (!(Test-UnitCleanupEvidence $cleanup.Replace('durable=1 exported=1 issueFree=1 bodyRows=1', 'durable=0 exported=0 issueFree=0 bodyRows=0'))) { throw 'Non-durable identity-less save proof rejected.' }
# -Rhs runs must carry the passing RHS: Status Quo case; it is optional only without -Rhs.
$rhsAbsent = "[EBG CLEANUP TEST RHS] case=rhs-mg-team available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'"
$rhsPassing = "[EBG CLEANUP TEST RHS] case=rhs-mg-team available=1 riders=9 blocking=0 durable=1 exported=1 issueFree=1 bodyRows=2 riderRows=0 owned=58`n[EBG CLEANUP TEST RHS DELETED] case=rhs-mg-team bodies=2 byEbg=2 owned=58 present=0 proven=1 lineage=0"
$usmcAbsent = "[EBG CLEANUP TEST CLOTH SLOT] case=rhs-usmc-recon available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'"
$usmcPassing = "[EBG CLEANUP TEST CLOTH SLOT] case=rhs-usmc-recon available=1 accessories=2 unlisted=2 held=2 wearerHolder=2 protectedChain=0 blocking=0 owned=61`n[EBG CLEANUP TEST CLOTH SLOT DELETED] case=rhs-usmc-recon bodies=2 byEbg=2 accessoriesGone=1 clothGone=1 present=0 proven=1 lineage=0"
$cleanupRhs = $cleanup.Replace($rhsAbsent, $rhsPassing).Replace($usmcAbsent, $usmcPassing)
if (!(Test-UnitCleanupEvidence $cleanupRhs $true) -or !(Test-UnitCleanupEvidence $cleanupRhs)) { throw 'Complete RHS unit-cleanup proof rejected.' }
if (Test-UnitCleanupEvidence $cleanup $true) { throw 'Unloaded RHS case accepted for an -Rhs run.' }
if (Test-UnitCleanupEvidence $cleanup.Replace($rhsAbsent, $rhsPassing) $true) { throw 'Unloaded RHS USMC recon case accepted for an -Rhs run.' }
if (Test-UnitCleanupEvidence $cleanup.Replace($usmcAbsent, $usmcPassing) $true) { throw 'Unloaded RHS machine-gun case accepted for an -Rhs run.' }
foreach ($invalid in @(
 $cleanupRhs.Replace('available=1 riders=9', 'available=1 riders=0'),
 $cleanupRhs.Replace('riders=9 blocking=0', 'riders=9 blocking=1'),
 $cleanupRhs.Replace('bodyRows=2 riderRows=0', 'bodyRows=2 riderRows=3'),
 $cleanupRhs.Replace('bodyRows=2', 'bodyRows=1'),
 $cleanupRhs.Replace('issueFree=1 bodyRows=2', 'issueFree=0 bodyRows=2'),
 $cleanupRhs.Replace('bodies=2 byEbg=2', 'bodies=2 byEbg=1'),
 $cleanupRhs.Replace('owned=58 present=0', 'owned=58 present=4'),
 $cleanupRhs.Replace('present=0 proven=1 lineage=0', 'present=0 proven=1 lineage=2'),
 ($cleanupRhs -replace '(?m)^\[EBG CLEANUP TEST RHS DELETED\].*\r?\n', ''),
 $cleanupRhs.Replace('accessories=2 unlisted=2 held=2 wearerHolder=2', 'accessories=1 unlisted=1 held=1 wearerHolder=1'),
 $cleanupRhs.Replace('unlisted=2 held=2 wearerHolder=2', 'unlisted=2 held=0 wearerHolder=2'),
 $cleanupRhs.Replace('held=2 wearerHolder=2 protectedChain=0', 'held=2 wearerHolder=0 protectedChain=2'),
 $cleanupRhs.Replace('bodies=2 byEbg=2 accessoriesGone=1', 'bodies=2 byEbg=2 accessoriesGone=0'),
 ($cleanupRhs -replace '(?m)^\[EBG CLEANUP TEST CLOTH SLOT DELETED\] case=rhs-usmc-recon.*\r?\n', ''),
 ($cleanupRhs + "`n[EBG CLEANUP PROVENANCE HOLD] phase=provenance reason='Cleanup item without native identity is not a verified model leaf' prefab='{300D5162D08D6CAF}Prefabs/Weapons/Attachments/Grips/Grip_PKP_Plastic/RHS_PKP_PistolGrip_base.et' nativeId=00000000-0000-0000-0000-000000000000"))) {
 if (Test-UnitCleanupEvidence $invalid $true) { throw 'Incomplete RHS unit-cleanup proof accepted.' }
}
# The runner concatenates stdout, logs/console.log and logs/script.log: each repeats the result.
if (!(Test-UnitCleanupEvidence ($cleanup + "`n" + $cleanup + "`n" + $cleanup))) { throw 'Unit-cleanup proof repeated across stdout, console.log and script.log rejected.' }
if (!(Test-UnitCleanupEvidence ($cleanup.Replace('Game destroyed', $teardown + "`nGame destroyed")))) { throw 'Stock teardown line after the cleanup result rejected.' }
foreach ($invalid in @(
 $cleanup.Replace('failures=0', 'failures=1'),
 $cleanup.Replace('reason=complete', 'reason=timeout'),
 $cleanup.Replace('cases=14', 'cases=12'),
 $cleanup.Replace('sameTick=1', 'sameTick=0'),
 $cleanup.Replace('vestStripped=1 riders=4', 'vestStripped=1 riders=0'),
 $cleanup.Replace('riders=4 blocking=0', 'riders=4 blocking=2'),
 $cleanup.Replace('bodyRows=1 riderRows=0', 'bodyRows=1 riderRows=1'),
 $cleanup.Replace('exported=1 issueFree=1 bodyRows=1', 'exported=1 issueFree=0 bodyRows=1'),
 $cleanup.Replace('weaponGone=1', 'weaponGone=0'),
 $cleanup.Replace('partGone=1', 'partGone=0'),
 $cleanup.Replace('vestGone=1', 'vestGone=0'),
 $cleanup.Replace('proven=1 lineage=0', 'proven=0 lineage=0'),
 $cleanup.Replace('proven=1 lineage=0', 'proven=1 lineage=1'),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST IDENTITYLESS\].*\r?\n', ''),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST IDENTITYLESS DELETED\].*\r?\n', ''),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST RHS\].*\r?\n', ''),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST CLOTH SLOT\] case=rhs-usmc-recon.*\r?\n', ''),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST CLOTH SLOT\] case=cloth-slot-accessory.*\r?\n', ''),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST CLOTH SLOT DELETED\].*\r?\n', ''),
 $cleanup.Replace('accessories=1 unlisted=1 held=1 wearerHolder=1', 'accessories=0 unlisted=0 held=0 wearerHolder=0'),
 $cleanup.Replace('unlisted=1 held=1', 'unlisted=0 held=1'),
 $cleanup.Replace('unlisted=1 held=1 wearerHolder=1', 'unlisted=1 held=0 wearerHolder=1'),
 $cleanup.Replace('wearerHolder=1 protectedChain=0', 'wearerHolder=0 protectedChain=0'),
 $cleanup.Replace('protectedChain=0', 'protectedChain=1'),
 $cleanup.Replace('protectedChain=0 blocking=0', 'protectedChain=0 blocking=1'),
 $cleanupRhs.Replace('wearerHolder=2 protectedChain=0 blocking=0', 'wearerHolder=2 protectedChain=0 blocking=1'),
 $cleanup.Replace('accessoriesGone=1 clothGone=1', 'accessoriesGone=0 clothGone=1'),
 $cleanup.Replace('accessoriesGone=1 clothGone=1', 'accessoriesGone=1 clothGone=0'),
 $cleanup.Replace('clothGone=1 present=0 proven=1 lineage=0', 'clothGone=1 present=0 proven=1 lineage=1'),
 ($cleanup + "`n[EBG CLEANUP KEEP] group=3 member=7 root=0x1 blocker='Cleanup held: keep-protected or valuable intel casualty'"),
 ($cleanup + "`n[EBG CLEANUP PROVENANCE HOLD] phase=provenance reason='Cleanup item without native identity is not a verified model leaf' prefab='{AD045AFAFFC1AB6E}Prefabs/Weapons/Attachments/Stocks/Stock_VZ58/Stock_VZ58_folding.et' nativeId=00000000-0000-0000-0000-000000000000"),
 $cleanup.Replace('etool=1 gone=1', 'etool=1 gone=0'),
 $cleanup.Replace('present=0 firstByEbg', 'present=3 firstByEbg'),
 $cleanup.Replace('firstByEbg=1', 'firstByEbg=0'),
 $cleanup.Replace('secondByEbg=1', 'secondByEbg=0'),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST ATOMIC\].*\r?\n', ''),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST FOREIGN\].*\r?\n', ''),
 ($cleanup + "`n[EBG CLEANUP TEST PARTIAL STRIP] id=0x4 wearer=0x5 prefab='x'"),
 $cleanup.Replace('state=2 whileCached=1', 'state=0 whileCached=0'),
 $cleanup.Replace('state=1 whileCached=1', 'state=0 whileCached=0'),
 $cleanup.Replace('state=0 whileCached=0', 'state=1 whileCached=1'),
 $cleanup.Replace('state=2 whileCached=1', 'state=1 whileCached=1'),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST DELETED\] case=full-partial-awake-then-cache.*\r?\n', ''),
 $cleanup.Replace('heldWhileCached=1', 'heldWhileCached=0'),
 ($cleanup -replace '(?m)^\[EBG CLEANUP TEST SNAPSHOT\].*\r?\n', ''),
 ($cleanup + "`n[EBG CLEANUP TEST SURVIVOR DELETE] id=0x2 wearer=0x3"),
 ($cleanup + "`n[EBG CLEANUP TEST RESULT] checks=1 failures=1 cases=9 reason=timeout"),
 ("SCRIPT (E): failure`n" + $cleanup),
 $cleanup.Replace('Game destroyed', ''))) {
 if (Test-UnitCleanupEvidence $invalid) { throw 'Incomplete unit-cleanup proof accepted.' }
}
if ((Test-GameplayEvidence $cleanup $false) -or (Test-BodyClearanceEvidence $cleanup) -or (Test-UnitCleanupEvidence $full) -or (Test-UnitCleanupEvidence $body)) { throw 'Unit-cleanup and other fixture kinds accepted as one another.' }
'PASS: complete Full/no-trim overflow, body-clearance and unit-cleanup evidence required (identity-less gear, storage-less cloth-slot accessories and, with -Rhs, both RHS: Status Quo cases); wrong case, casualty, cache state, survivor deletion, partial casualty strip, parked casualties, provenance holds on identity-less riders, errors and incomplete runs rejected. No engine launched.'
