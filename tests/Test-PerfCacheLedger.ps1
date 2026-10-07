#requires -Version 7.0
# Portable guard for the unit-caching cleanup ledger indexes (0.1.15 performance plan, WP1a):
#  1. Record-scoped scans walk a per-record row index (RecordRows) instead of the whole
#     ledger, in the same ledger order, and prune through RemoveRow in descending order.
#  2. CanRetire uses that index; PlayerPossession walks a per-member row index, and the
#     bind version moves only on a live insert or rebind (not on a row a removal moved).
#  3. The released negatives (ids, lineage members, origins) have set mirrors, every insert
#     and lookup goes through one helper, and ImportPersistentNegatives skips a state it has
#     already imported.
# The index must stay exact wherever a row is inserted, removed or changes Group. A seeded
# model of the ledger checks that the record-scoped loops visit and remove exactly what the
# old full scans did. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG/EBG_CacheCleanup.c'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Get-Body([string]$Text, [string]$Signature) {
 $found = [regex]::Matches($Text, $Signature)
 Assert ($found.Count -eq 1) "signature must match once ($($found.Count)): $Signature"
 $open = $Text.IndexOf('{', $found[0].Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Before([string]$Body, [string]$First, [string]$Second, [string]$Message) {
 $a = $Body.IndexOf($First); $b = $Body.IndexOf($Second)
 Assert ($a -ge 0 -and $b -ge 0 -and $a -lt $b) $Message
}
$text = [IO.File]::ReadAllText($path).Replace("`r", '')
# Code without line comments, for counting statements.
$code = (($text -split "`n") | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"

# --- 1. Record-scoped scans use RecordRows --------------------------------------------------
$recordScoped = [ordered]@{
 'PlayerNear' = 'protected\s+bool\s+PlayerNear\s*\('
 'CanRetire' = '(?m)^\s*bool\s+CanRetire\s*\('
 'CountCasualtyRows' = 'protected\s+int\s+CountCasualtyRows\s*\('
 'CollectCasualtyTargets' = 'protected\s+IEntity\s+CollectCasualtyTargets\s*\('
 'ReleaseCasualty' = 'protected\s+void\s+ReleaseCasualty\s*\('
 'ForgetPrefabMember' = 'void\s+ForgetPrefabMember\s*\('
 'ReleaseRemovedMember' = 'void\s+ReleaseRemovedMember\s*\('
 'ReleaseGroup' = 'void\s+ReleaseGroup\s*\('
 'RememberPersistentMaps' = 'protected\s+void\s+RememberPersistentMaps\s*\('
 'CollectProvenanceRiders' = 'protected\s+void\s+CollectProvenanceRiders\s*\('
 'CountSaveProvenance' = '(?m)^\s*int\s+CountSaveProvenance\s*\('
}
foreach ($name in $recordScoped.Keys) {
 $body = Get-Body $text $recordScoped[$name]
 Assert ($body -match 'RecordRows\(record,\s*recordRows\);') "$name must walk RecordRows(record, recordRows)"
 Assert ($body -notmatch ':\s*m_Objects\)' -and $body -notmatch 'm_Objects\.Count\(\)') "$name must not scan the whole ledger (m_Objects)"
 Assert (!($body -match 'foreach\s*\(\s*EBG_CleanupObject\s+object\s*:\s*m_Objects\s*\)' -and $body -match 'object\.Group\s*!=\s*record')) "$name still filters the global ledger by record"
}
# Prune loops: descending over the record's rows, removing at the maintained position.
foreach ($name in 'ForgetPrefabMember', 'ReleaseRemovedMember', 'ReleaseGroup') {
 $body = Get-Body $text $recordScoped[$name]
 Assert ($body -match 'for\s*\(\s*int\s+k\s*=\s*recordRows\.Count\(\)\s*-\s*1;\s*k\s*>=\s*0;\s*k--\s*\)') "$name must prune in descending ledger order"
 Assert ($body -match 'RemoveRow\(' -and $body -notmatch 'RemoveObject\(') "$name must remove through RemoveRow"
}
# CheckGroupTransfers: the null record keeps the full sweep; a record walks only its rows.
$transfers = Get-Body $text '(?m)^\s*void\s+CheckGroupTransfers\s*\(\s*EBG_CacheGroup\s+record\s*\)'
$split = $transfers.IndexOf('record.CleanupNextAttempt = 0;')
Assert ($split -gt 0) 'CheckGroupTransfers must reset CleanupNextAttempt for a record'
$sweep = $transfers.Substring(0, $split); $scoped = $transfers.Substring($split)
Assert ($sweep -match 'if\s*\(\s*!record\s*\)' -and $sweep -match 'CheckObjectTransfer\(object\)' -and $sweep -match 'MaintainNativeProtection\(object\)' -and $sweep -match 'TransferPrunable\(m_Objects\[i\]\)\)\s*RemoveObject\(i\)') 'the null-record sweep must keep the three full passes'
Assert ([regex]::Matches($scoped, 'RecordRows\(record,\s*recordRows\);').Count -eq 3) 'each record pass must read its rows afresh (three RecordRows calls)'
Assert ($scoped -notmatch 'm_Objects') 'the record passes must not scan the whole ledger'
Assert ($scoped -match 'CheckObjectTransfer\(' -and $scoped -match 'MaintainNativeProtection\(' -and $scoped -match 'for\s*\(\s*int\s+k\s*=\s*recordRows\.Count\(\)\s*-\s*1;\s*k\s*>=\s*0;\s*k--\s*\)\s*if\s*\(\s*TransferPrunable\(recordRows\[k\]\)\)\s*RemoveRow\(recordRows\[k\]\);') 'the record passes must be transfer, protection, then a descending prune'
$prune = Get-Body $text 'protected\s+bool\s+TransferPrunable\s*\('
foreach ($needle in 'removed.Entity', 'removed.Corpse', 'removed.FullDetached', 'removed.Group.Full', 'removed.Group.FullCleanup', 'IsInertDeathLeafProof(removed)') {
 Assert $prune.Contains($needle) "TransferPrunable lost a prune condition: $needle"
}

# --- Index bookkeeping ----------------------------------------------------------------------
Assert ($text -match 'class\s+EBG_CleanupObject\s*\{[^}]*\bint\s+LedgerIndex\s*=\s*-1;') 'EBG_CleanupObject must carry LedgerIndex = -1'
Assert ($text -match '(?m)^\s*static\s+int\s+s_EBG_BindVersion;\s*$') 's_EBG_BindVersion must be a plain static int without an initializer (contract C2)'
Assert ($text -match 'protected\s+ref\s+map<int,\s*ref\s+array<EBG_CleanupObject>>\s+m_RowsByGroup\s*=') 'the per-record index must be an instance map keyed by record Id'
Assert ($text -notmatch 'static\s+ref\s+map<int,\s*ref\s+array<EBG_CleanupObject>>') 'the per-record index must not be static (validation ledgers keep their own)'
$insert = Get-Body $text 'protected\s+void\s+InsertObject\s*\('
Assert ($insert -match '(?s)m_Objects\.Insert\(object\);\s*object\.LedgerIndex\s*=\s*m_Objects\.Count\(\)\s*-\s*1;\s*GroupIndexAdd\(object\);\s*MemberIndexAdd\(object\);\s*IndexObject\(object\);') 'InsertObject must set LedgerIndex and index the row by record and by member'
$remove = Get-Body $text 'protected\s+void\s+RemoveObject\s*\('
Before $remove 'GroupIndexRemove(row);' 'm_Objects.Remove(index);' 'RemoveObject must drop the row from its record index before removing it'
Before $remove 'MemberIndexRemove(row);' 'm_Objects.Remove(index);' 'RemoveObject must drop the row from its member index before removing it'
Before $remove 'row.LedgerIndex = -1;' 'm_Objects.Remove(index);' 'RemoveObject must clear the removed row''s LedgerIndex'
Assert ($remove -match '(?s)m_Objects\.Remove\(index\);.*if\s*\(\s*moved\s*\)\s*\{\s*moved\.LedgerIndex\s*=\s*index;\s*IndexObject\(moved\);') 'RemoveObject must give the swapped-in row its new LedgerIndex'
# The bind version moves on a live insert or rebind only: the re-index of a row that a removal
# merely moved (RemoveObject -> IndexObject(moved)) binds nothing, so it must not rerun every
# player's possession pass; validation ledgers never bump.
$index = Get-Body $text 'protected\s+void\s+IndexObject\s*\('
Assert ($index -notmatch 's_EBG_BindVersion') 'IndexObject must not bump s_EBG_BindVersion (a removal re-indexes the moved row through it)'
Assert ($insert -match '(?s)IndexObject\(object\);\s*if\s*\(\s*!m_ValidationOnly\s*\)\s*s_EBG_BindVersion\+\+;') 'InsertObject must bump s_EBG_BindVersion for the live ledger'
$rebind = Get-Body $text 'protected\s+void\s+RebindObject\s*\('
Assert ($rebind -match '(?s)IndexObject\(object\);\s*if\s*\(\s*!m_ValidationOnly\s*\)\s*s_EBG_BindVersion\+\+;') 'RebindObject must re-index and bump s_EBG_BindVersion for the live ledger'
Assert ($remove -notmatch 's_EBG_BindVersion') 'RemoveObject must not bump s_EBG_BindVersion'
Assert ([regex]::Matches($code, 's_EBG_BindVersion\+\+').Count -eq 2) 's_EBG_BindVersion is bumped only in InsertObject and RebindObject'
$removeRow = Get-Body $text 'protected\s+void\s+RemoveRow\s*\('
Assert ($removeRow -match 'm_Objects\[index\]\s*!=\s*object' -and $removeRow -match 'RemoveObject\(index\);') 'RemoveRow must check the maintained position before removing'
$rows = Get-Body $text 'protected\s+void\s+RecordRows\s*\('
Assert ($rows -match '(?s)if\s*\(\s*!record\s*\)\s*\{\s*foreach\s*\(\s*EBG_CleanupObject\s+unowned\s*:\s*m_Objects\s*\)\s*if\s*\(\s*!unowned\.Group\s*\)') 'RecordRows(null) must answer with the full scan (rows without a record are not indexed)'
Assert ($rows -match 'row\.Group\s*==\s*record' -and $rows -match 'm_Objects\[row\.LedgerIndex\]\s*==\s*row') 'RecordRows must keep only rows that are still at their position and belong to this record'
Assert ($rows -match 'positions\.Sort\(\);') 'RecordRows must return rows in ledger order'
Assert ($rows -match '(?s)if\s*\(\s*!EBG_DebugChecks\.Enabled\s*\)\s*return;.*foreach\s*\(\s*EBG_CleanupObject\s+expected\s*:\s*m_Objects\s*\).*EBG_DebugChecks\.Mismatch\(') 'RecordRows must cross-check against the full scan when EBG_DebugChecks.Enabled'
Assert ($rows -notmatch 'EBG_DebugChecks\.Enabled\s*=') 'RecordRows must only read EBG_DebugChecks.Enabled'
Assert ((Get-Body $text 'protected\s+void\s+GroupIndexAdd\s*\(') -match 'm_RowsByGroup\.Set\(object\.Group\.Id,\s*rows\);') 'GroupIndexAdd must file the row under its record Id'
Assert ((Get-Body $text 'protected\s+void\s+GroupIndexRemove\s*\(') -match 'rows\.RemoveItem\(object\);') 'GroupIndexRemove must remove the exact row'

# Every Group assignment: before InsertObject, or re-indexed (CommitRegroup). The token is no row.
$assignments = [ordered]@{
 'ImportPersistentGroupCore' = @('protected\s+bool\s+ImportPersistentGroupCore\s*\(', 'object.Group = record;', 'InsertObject(object);')
 'Hold' = @('protected\s+void\s+Hold\s*\(', 'object.Group = group;', 'InsertObject(object);')
 'RegisterInitialMember' = @('protected\s+void\s+RegisterInitialMember\s*\(', 'proof.Group = record;', 'InsertObject(proof);')
 'CaptureValidationNode' = @('protected\s+bool\s+CaptureValidationNode\s*\(', 'object.Group = record;', 'InsertObject(object);')
 'CaptureFull' = @('EBG_CleanupFullTransfer\s+CaptureFull\s*\(', 'token.Group = record;', $null)
}
foreach ($name in $assignments.Keys) {
 $signature, $assignment, $insertCall = $assignments[$name]
 $body = Get-Body $text $signature
 Assert ([regex]::Matches($body, [regex]::Escape($assignment)).Count -eq 1) "$name must assign Group exactly once: $assignment"
 if ($insertCall) { Before $body $assignment $insertCall "$name must set Group before $insertCall" }
}
$regroup = Get-Body $text '(?m)^\s*void\s+CommitRegroup\s*\('
Assert ($regroup -match '\{\s*GroupIndexRemove\(object\);\s*object\.Group\s*=\s*destination;\s*GroupIndexAdd\(object\);\s*break;\s*\}') 'CommitRegroup must move the row between record indexes around object.Group = destination'
$groupWrites = [regex]::Matches($code, '\.Group\s*=(?!=)').Count
Assert ($groupWrites -eq 6) "unexpected Group assignment in EBG_CacheCleanup.c ($groupWrites, expected 6): a new site must keep m_RowsByGroup exact"
$shutdown = Get-Body $text 'static\s+void\s+ShutdownForWorldCleanup\s*\('
Assert ($shutdown -match 'm_RowsByGroup\.Clear\(\);' -and $shutdown -match 'm_RowsByMember\.Clear\(\);' -and $shutdown -match 'm_ReleasedIdSet\.Clear\(\);') 'world teardown must clear the indexes and the released-id mirror with their sources'

# Member index for PlayerPossession: a row's Member is set once, before InsertObject.
Assert ($text -match 'protected\s+ref\s+map<EBG_CacheMember,\s*ref\s+array<EBG_CleanupObject>>\s+m_RowsByMember\s*=') 'the per-member index must be an instance map keyed by the member'
$memberWrites = [regex]::Matches($code, '\.Member\s*=(?!=)').Count
Assert ($memberWrites -eq 4) "unexpected Member assignment in EBG_CacheCleanup.c ($memberWrites, expected 4): a new site must keep m_RowsByMember exact"
foreach ($site in @(@('protected\s+bool\s+ImportPersistentGroupCore\s*\(', 'object.Member = record.Members[row.MemberIndex];', 'InsertObject(object);'), @('protected\s+void\s+Hold\s*\(', 'object.Member = member;', 'InsertObject(object);'), @('protected\s+void\s+RegisterInitialMember\s*\(', 'proof.Member = member;', 'InsertObject(proof);'), @('protected\s+bool\s+CaptureValidationNode\s*\(', 'object.Member = member;', 'InsertObject(object);'))) {
 $signature, $assignment, $insertCall = $site
 Before (Get-Body $text $signature) $assignment $insertCall "a row's Member must be set before $insertCall ($assignment)"
}
Assert ((Get-Body $text 'protected\s+void\s+MemberIndexAdd\s*\(') -match 'm_RowsByMember\.Set\(object\.Member,\s*rows\);') 'MemberIndexAdd must file the row under its member'
Assert ((Get-Body $text 'protected\s+void\s+MemberIndexRemove\s*\(') -match 'rows\.RemoveItem\(object\);') 'MemberIndexRemove must remove the exact row'
$possession = Get-Body $text '(?m)^\s*void\s+PlayerPossession\s*\(\s*IEntity\s+entity\s*\)'
Assert ($possession -match 'foreach\s*\(\s*EBG_CacheMember\s+member,\s*array<EBG_CleanupObject>\s+memberRows\s*:\s*m_RowsByMember\s*\)' -and $possession -match 'member\.Entity\s*!=\s*entity\)\s*continue;' -and $possession -match 'positions\.Sort\(\);') 'PlayerPossession must visit only the rows of members bound to the entity, in ledger order'
Assert ($possession -match 'candidate\.Held\s*&&\s*candidate\.Member\s*&&\s*candidate\.Member\.Entity\s*==\s*entity' -and $possession -match 'object\.ReleaseReason\s*=\s*"Original member became player-controlled";\s*ReleaseObject\(object,\s*true\);') 'PlayerPossession must keep the old row filter, reason and permanent release'
$fullScan = $possession.IndexOf(': m_Objects)')
Assert ($fullScan -gt $possession.IndexOf('if (EBG_DebugChecks.Enabled)') -and $possession.IndexOf(': m_Objects)', $fullScan + 1) -lt 0 -and $possession -match 'EBG_DebugChecks\.Mismatch\(') 'PlayerPossession may walk the whole ledger only as the EBG_DebugChecks cross-check'

# --- 3. Released negatives ------------------------------------------------------------------
foreach ($pair in @(@('m_ReleasedIds', 'm_ReleasedIdSet', 'AddReleasedId', 'KnownReleasedId', 'UUID'), @('m_ReleasedLineageMembers', 'm_LineageSet', 'AddLineageMember', 'KnownLineageMember', 'UUID'), @('m_ReleasedOrigins', 'm_OriginSet', 'AddOrigin', 'KnownOrigin', 'string'))) {
 $array, $mirror, $add, $known, $type = $pair
 Assert ($text -match "protected\s+ref\s+set<string>\s+$mirror\s*=") "$mirror must be an instance set<string>"
 $addBody = Get-Body $text "protected\s+void\s+$add\s*\(\s*$type\s"
 Assert ($addBody -match "(?s)if\s*\(\s*$known\(\w+\)\s*\)\s*return;\s*$array\.Insert\(\w+\);\s*$mirror\.Insert\(\w+\);") "$add must append once to $array and $mirror together"
 $knownBody = Get-Body $text "protected\s+bool\s+$known\s*\(\s*$type\s"
 Assert ($knownBody -match "bool\s+known\s*=\s*$mirror\.Contains\(" -and $knownBody -match "EBG_DebugChecks\.Enabled\s*&&\s*known\s*!=\s*$array\.Contains\(") "$known must answer from $mirror and cross-check $array only under EBG_DebugChecks"
 # Outside the two helpers (and teardown and export reads), nobody touches the array directly.
 $outside = $code.Replace($addBody, '').Replace($knownBody, '')
 Assert ([regex]::Matches($outside, "\b$array\.(Insert|Contains|InsertAt|Remove\w*)\(").Count -eq 0) "$array must only change and be searched through $add and $known"
}
$import = Get-Body $text '(?m)^\s*void\s+ImportPersistentNegatives\s*\('
Before $import 'AddReleasedId(id);' 'state.Ensure();' 'ImportPersistentNegatives must import the ids before reading the state'
Before $import 'state.Ensure();' 'state == m_ImportedState' 'the import early-out must follow state.Ensure()'
Assert ($import -match 'if\s*\(\s*state\s*==\s*m_ImportedState\s*&&\s*state\.DeserializeCalls\s*==\s*m_ImportedCalls\s*&&\s*state\.ReleasedLineageMembers\.Count\(\)\s*==\s*m_ImportedLineageCount\s*&&\s*state\.ReleasedOrigins\.Count\(\)\s*==\s*m_ImportedOriginCount\s*\)\s*return;') 'ImportPersistentNegatives must skip an already imported state (instance, DeserializeCalls and both counts)'
Assert ($import -match '(?s)AddOrigin\(origin\);\s*m_ImportedState\s*=\s*state;\s*m_ImportedCalls\s*=\s*state\.DeserializeCalls;\s*m_ImportedLineageCount\s*=\s*state\.ReleasedLineageMembers\.Count\(\);\s*m_ImportedOriginCount\s*=\s*state\.ReleasedOrigins\.Count\(\);') 'ImportPersistentNegatives must record the imported state after importing'
$export = Get-Body $text '(?m)^\s*bool\s+ExportPersistentNegatives\s*\('
Assert ($export -notmatch 'ids\.Contains\(|state\.ReleasedLineageMembers\.Contains\(|state\.ReleasedOrigins\.Contains\(') 'ExportPersistentNegatives must not search its targets linearly'
Assert ([regex]::Matches($export, 'present\.Contains\(').Count -eq 3) 'ExportPersistentNegatives must deduplicate all three targets through one set each'
Assert ($export -match 'return\s+ids\.Count\(\)\s*<=\s*65536\s*&&\s*state\.ReleasedOrigins\.Count\(\)\s*<=\s*65536\s*&&\s*state\.ReleasedLineageMembers\.Count\(\)\s*<=\s*65536;') 'ExportPersistentNegatives must keep its 65536 limits'
# Wave 2 (WP1a item 4, the budgeted transfer sweep) adds its own assertion when it lands.

# --- Model check: indexed record loops equal the old full scans ------------------------------
# Two ledgers receive the same random operations. Ledger A uses the old full scans; ledger B
# keeps the per-record index with maintained LedgerIndex and walks RecordRows. Enforce's
# array.Remove(i) moves the last element into i, which the model reproduces.
$random = [Random]::new(20261007)
$a = [Collections.Generic.List[object]]::new(); $b = [Collections.Generic.List[object]]::new()
$index = @{}; $nextId = 0
function Remove-At([Collections.Generic.List[object]]$Ledger, [int]$At, [bool]$Indexed) {
 $row = $Ledger[$At]
 if ($Indexed) { if ($row.Group -gt 0) { [void]$index[$row.Group].Remove($row) }; $row.LedgerIndex = -1 }
 $last = $Ledger.Count - 1
 if ($At -lt $last) { $moved = $Ledger[$last]; $Ledger[$At] = $moved; if ($Indexed) { $moved.LedgerIndex = $At } }
 $Ledger.RemoveAt($last)
}
function Get-RecordRows([int]$Group) {
 if ($Group -le 0) { return @($b | Where-Object { $_.Group -eq 0 }) }
 if (!$index.ContainsKey($Group)) { return @() }
 @($index[$Group] | Sort-Object LedgerIndex | ForEach-Object { $b[$_.LedgerIndex] })
}
function Ids($Rows) { (@($Rows) | ForEach-Object { $_.Id }) -join ',' }
for ($step = 0; $step -lt 3000; $step++) {
 $op = $random.Next(10)
 $group = $random.Next(0, 7) # 0 is a row without a record
 if ($op -lt 5 -or $a.Count -lt 4) {
  $nextId++
  $a.Add([pscustomobject]@{ Id = $nextId; Group = $group })
  $row = [pscustomobject]@{ Id = $nextId; Group = $group; LedgerIndex = $b.Count }
  $b.Add($row)
  if ($group -gt 0) { if (!$index.ContainsKey($group)) { $index[$group] = [Collections.Generic.List[object]]::new() }; $index[$group].Add($row) }
 }
 elseif ($op -lt 8) {
  # Record prune: the old reverse full scan against the descending record rows.
  $salt = $random.Next(3)
  $visitedA = [Collections.Generic.List[int]]::new(); $visitedB = [Collections.Generic.List[int]]::new()
  for ($i = $a.Count - 1; $i -ge 0; $i--) {
   if ($a[$i].Group -ne $group) { continue }
   $visitedA.Add($a[$i].Id)
   if (($a[$i].Id + $salt) % 3 -eq 0) { Remove-At $a $i $false }
  }
  $rowsB = @(Get-RecordRows $group)
  for ($k = $rowsB.Count - 1; $k -ge 0; $k--) {
   $row = $rowsB[$k]; $visitedB.Add($row.Id)
   if (($row.Id + $salt) % 3 -eq 0) { Assert ($b[$row.LedgerIndex] -eq $row) "model: stale LedgerIndex at step $step"; Remove-At $b $row.LedgerIndex $true }
  }
  Assert (($visitedA -join ',') -eq ($visitedB -join ',')) "model: prune visit order differs at step $step"
 }
 elseif ($op -lt 9) {
  # Regroup: rows of one record move to another (CommitRegroup).
  $to = $random.Next(1, 7)
  foreach ($row in $a) { if ($row.Group -eq $group -and $group -gt 0) { $row.Group = $to } }
  foreach ($row in $b) { if ($row.Group -eq $group -and $group -gt 0) { [void]$index[$group].Remove($row); $row.Group = $to; if (!$index.ContainsKey($to)) { $index[$to] = [Collections.Generic.List[object]]::new() }; $index[$to].Add($row) } }
 }
 else {
  # Any other removal by ledger position (the null-record sweep).
  $at = $random.Next($a.Count)
  Remove-At $a $at $false; Remove-At $b $at $true
 }
 Assert ((Ids $a) -eq (Ids $b)) "model: ledgers diverge at step $step"
 if ($step % 50 -eq 0 -or $op -ge 5) {
  foreach ($g in 0..6) {
   Assert ((Ids (Get-RecordRows $g)) -eq (Ids ($a | Where-Object { $_.Group -eq $g }))) "model: RecordRows($g) differs from the full scan at step $step"
  }
 }
}
'PASS: cleanup ledger record and member indexes (PlayerPossession walks only the possessed member''s rows), bind version bumped on live inserts and rebinds only, retire check and released-negative sets keep the old scan results and order.'
