#requires -Version 7.0
# Portable guard for three small 0.1.15 fixes found in the 2026-10-07 op logs:
#  1 The unread "GM-created" latch is gone: no CreateEntityServer/SpawnGroupMember depth
#    wrappers, no GMSpawnDepth, no EBG_IsGMCreated (they put EXPBG in every placement stack
#    and a throw inside super would have latched the counter for good).
#  2 EBG_CacheSnapshot.CanSave names a mission end/change as such instead of "loading", and
#    refuses it only while the Optimizer holds state or this world's cleanup has begun.
#  3 EXPBG Reconnect prints the disconnect cause as group/reason and names the check that
#    refused a reservation.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$caching = Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG'
$reconnectPath = Join-Path $repo 'addon/persistent-battlefield/Scripts/Game/GameMode/Components/EXPBG_ReconnectComponent.c'
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

# 1 No GM-created latch anywhere in the pack.
$sources = Get-ChildItem -LiteralPath (Join-Path $repo 'addon') -Recurse -File -Filter '*.c'
foreach ($file in $sources) {
 $text = Read-Text $file.FullName
 Assert (![regex]::IsMatch($text, '\b(GMSpawnDepth|GMSpawnInProgress|EBG_IsGMCreated|m_EBG_GMCreated)\b')) "GM-created latch symbol left in $($file.FullName.Substring($repo.Length + 1))"
}
$history = Read-Text (Join-Path $caching 'EBG_PlayerHistory.c')
Assert (!$history.Contains('modded class SCR_PlacingEditorComponent')) 'EBG_PlayerHistory.c no longer wraps SCR_PlacingEditorComponent.CreateEntityServer'
Assert (![regex]::IsMatch($history, '\bSpawnGroupMember\s*\(')) 'EBG_PlayerHistory.c no longer wraps SCR_AIGroup.SpawnGroupMember'
Assert (![regex]::IsMatch($history, 'void\s+SCR_ChimeraCharacter\s*\(|void\s+SCR_AIGroup\s*\(')) 'EBG_PlayerHistory.c adds no constructors to SCR_ChimeraCharacter or SCR_AIGroup'
Assert ($history.Contains('bool EBG_IsMarkedPlayer()') -and $history.Contains('void EBG_MarkLeftSquad()') -and $history.Contains('override void OnEmpty()')) 'the player-history latch, left-squad seam and empty-group policy stay'

# 2 CanSave: teardown is named as such and gates only Optimizer state.
$snapshot = Read-Text (Join-Path $caching 'EBG_CacheSnapshot.c')
$canSave = Get-Body $snapshot 'static\s+bool\s+CanSave\s*\(\s*out\s+string\s+reason\s*\)'
$server = $canSave.IndexOf('if (!Replication.IsServer())')
$world = $canSave.IndexOf('if (!EBG_CacheManager.IsPortableWorldReady() && (EBG_CacheManager.IsCleaningUpCurrentWorld() || HasOptimizerState()))')
$loading = $canSave.IndexOf('if (Loading || EBG_FullCacheGroup.IsNativeOperationBusy() || SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks())')
Assert ($server -ge 0 -and $world -gt $server -and $loading -gt $world) 'CanSave checks server, then mission end/change (Optimizer state or cleanup only), then loading/native restoration'
$worldReason = [regex]::Match($canSave.Substring($world), 'reason\s*=\s*"([^"]+)"').Groups[1].Value
Assert ($worldReason -match 'mission is ending' -and $worldReason -notmatch 'loading|restoration') "the mission-end refusal names the mission end, not loading (got '$worldReason')"
$loadingReason = [regex]::Match($canSave.Substring($loading), 'reason\s*=\s*"([^"]+)"').Groups[1].Value
Assert ($loadingReason -eq 'Optimizer loading or native restoration is in progress') 'loading and native restoration keep their own refusal'
Assert (([regex]::Matches($canSave, 'IsPortableWorldReady\(\)')).Count -eq 1) 'world readiness is tested once, in its own branch'
$state = Get-Body $snapshot 'static\s+bool\s+HasOptimizerState\s*\(\s*\)'
foreach ($needle in 'Loading', 's_ImportPrepared', '!EBG_CacheZone.Zones.IsEmpty()', 'EBG_FullCacheGroup.IsNativeOperationBusy()', 'SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks()', '!manager.Records.IsEmpty()') {
 Assert $state.Contains($needle) "HasOptimizerState must count: $needle"
}
$manager = Read-Text (Join-Path $caching 'EBG_CacheManager.c')
$cleaning = Get-Body $manager 'static\s+bool\s+IsCleaningUpCurrentWorld\s*\(\s*\)'
Assert ($cleaning.Contains('if (!Unloading || !GetGame() || s_UnloadingWorld != GetGame().GetWorld())') -and $cleaning.Contains('return !mode || mode == s_UnloadingMode;')) 'IsCleaningUpCurrentWorld is true only between this world''s cleanup and the next world'

# 3 Reconnect: readable cause and the deciding check.
$reconnect = Read-Text $reconnectPath
$disconnect = Get-Body $reconnect 'override\s+bool\s+HandlePlayerDisconnect\s*\(\s*int\s+playerId,\s*KickCauseCode\s+cause\s*\)'
Assert (![regex]::IsMatch($disconnect, ',\s*cause\s*\)')) 'HandlePlayerDisconnect never formats the raw KickCauseCode handle'
Assert (([regex]::Matches($disconnect, 'EXPBG_DescribeCause\(cause\)')).Count -eq 3) 'every HandlePlayerDisconnect log line describes the cause'
Assert ($disconnect.Contains('"[EXPBG Reconnect] Reservation rejected: %1 for playerId=%2 cause=%3", EXPBG_DescribeIrrelevantData(data)')) 'the IsDataRelevant refusal names its check'
Assert ($disconnect.Contains('"[EXPBG Reconnect] Reserved living character for playerId=%1 cause=%2"')) 'the reservation line keeps the prefix the E2E runbook looks for'
Assert ($disconnect.Contains('if (!IsDataRelevant(data))')) 'vanilla IsDataRelevant still decides'
$cause = Get-Body $reconnect 'protected\s+string\s+EXPBG_DescribeCause\s*\(\s*KickCauseCode\s+cause\s*\)'
Assert ($cause.Contains('if (!cause)') -and $cause.Contains('GetGame().GetFullKickReason(cause, groupInt, reasonInt, group, reason);') -and $cause.Contains('string.Format("%1/%2 (%3/%4)", group, reason, groupInt, reasonInt)')) 'the cause is printed as group/reason names with their numbers'
$check = Get-Body $reconnect 'protected\s+string\s+EXPBG_DescribeIrrelevantData\s*\(\s*notnull\s+SCR_ReconnectData\s+data\s*\)'
foreach ($needle in '"no controlled entity"', '"controlled entity is not a character"', '"character is dead"') {
 Assert $check.Contains($needle) "EXPBG_DescribeIrrelevantData must name: $needle"
}
Assert (([regex]::Matches($reconnect, '\bPrint(Format)?\s*\(')).Count -le 5) 'Reconnect keeps its five log sites'

# Enforce gotchas in the touched sources.
foreach ($path in (Join-Path $caching 'EBG_PlayerHistory.c'), (Join-Path $caching 'EBG_MissionPersistence.c'), (Join-Path $caching 'EBG_CacheSnapshot.c'), (Join-Path $caching 'EBG_CacheManager.c'), $reconnectPath) {
 $text = Read-Text $path
 $name = Split-Path -Leaf $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep)\b')) "reserved Enforce name declared in $name"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $name"
}
'PASS: no GM-created latch (no placement or squad-member wrappers in EXPBG stacks); CDF/native capture readiness names a mission end as such and gates it only while the Optimizer holds state or the world is cleaning up; EXPBG Reconnect prints the disconnect cause as group/reason and names the refusing check. Native engines were not launched.'
