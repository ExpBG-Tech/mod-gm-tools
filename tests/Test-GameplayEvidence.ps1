#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$tokens = $null; $parseErrors = $null
$runner = [Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot/Run-Gameplay.ps1", [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Runner syntax is invalid.' }
$function = $runner.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-GameplayEvidence' }, $true)
if (!$function) { throw 'Missing gameplay evidence verifier.' }
# Load only the pure verifier; never execute the native runner in portable tests.
. ([scriptblock]::Create($function.Extent.Text))
function Evidence([int]$Trim, [int]$Count) {
 $requested = 4
 if ($Trim) { $requested = 12 }
 @"
[EXPG CAPACITY CASE] freshTrim=$Trim requested=$requested expected=$Count capacity=$Count
[EXPG TRIM RESULT] admitted=12 before=12 retained=9 acknowledged=3 deleted=3 leaderPreserved=1 originals=1
[EXPG FULL CYCLE] cycle=1 living=$Count transformParity=1 assignments=1
[EXPG FULL CYCLE] cycle=2 living=$($Count-1) transformParity=1 assignments=1
[EXPG GAMEPLAY RESULT] phase=10 checks=40 failures=0 actors=$Count fixedPosts=1 reason=completed
Game destroyed
"@
}
$full = Evidence 0 4
$trim = Evidence 1 9
if (!(Test-GameplayEvidence $full $false) -or !(Test-GameplayEvidence $trim $true)) { throw 'Valid complete case rejected.' }
if (Test-GameplayEvidence $trim $false) { throw 'Trim case disguised as full-house pass.' }
if (Test-GameplayEvidence (Evidence 1 4) $true) { throw 'Untrimmed roster disguised as trim coverage.' }
if (Test-GameplayEvidence ($trim.Replace('cycle=2','cycle=3')) $true) { throw 'Missing second cycle accepted.' }
if (Test-GameplayEvidence ($trim.Replace('living=8','living=9')) $true) { throw 'Resurrected casualty accepted.' }
if (Test-GameplayEvidence ($trim.Replace('requested=12','requested=4')) $true) { throw 'Fabricated larger roster accepted.' }
if (Test-GameplayEvidence ($trim.Replace('acknowledged=3','acknowledged=0')) $true) { throw 'Unacknowledged deletion accepted.' }
if (Test-GameplayEvidence ($trim.Replace('deleted=3','deleted=0')) $true) { throw 'Missing native deletion accepted.' }
if (Test-GameplayEvidence ($trim.Replace('leaderPreserved=1','leaderPreserved=0')) $true) { throw 'Leader loss accepted.' }
if (Test-GameplayEvidence ($trim.Replace('originals=1','originals=0')) $true) { throw 'Invented survivor roster accepted.' }
if (Test-GameplayEvidence ($trim.Replace('transformParity=1','transformParity=0')) $true) { throw 'Wrong transforms accepted.' }
if (Test-GameplayEvidence ($trim + "`nSCRIPT (E): failure") $true) { throw 'Script error accepted.' }
if (Test-GameplayEvidence ($trim.Replace('Game destroyed','')) $true) { throw 'Incomplete shutdown accepted.' }
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
'PASS: complete Full/trim evidence required; wrong case, casualty, transforms, errors and incomplete runs rejected. No engine launched.'
