#requires -Version 7.0
# Portable guard for the 0.1.15 Ambient Destruction performance work (plan WP14, findings
# ambient-and-small-1, -2, -5, -6 and -7). Same outcomes, less work:
#  - an occupied zone with no player near asks for occupancy at most every 10 s (sleep may
#    come up to 10 s later; wake is unchanged);
#  - Reconcile's suppressed-slot scan runs only while a suppression is pending;
#  - SupportStep returns at once for a zone with no live props, leaving the wall cursor
#    exactly where the full pass would;
#  - prefab path classification (StructuralWall, BuildingContext, RejectedPath) is memoised
#    per distinct path text, capped at 4096 entries, with every per-entity check still live;
#  - slot counts are broadcast once per changed zone per world tick, not once per slot.
# The memoised path classes are proven against the 0.1.14 path tests below, and the
# occupancy back-off and wall cursor are simulated against the 0.1.14 logic. No engine is
# launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/ambient-destruction/Scripts/Game/EXPAD'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
# Comments blanked (same length); string literals kept, because the path tests live in them.
function Remove-Comments([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return $m.Value }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
# Index just past the '}' closing the first '{' at or after $From.
function Get-BlockEnd([string]$Text, [int]$From) {
 $open = $Text.IndexOf('{', $From); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $i + 1 } }
 }
 throw 'FAIL: unbalanced block'
}

$zonePath = Join-Path $scripts 'EAD_Zone.c'
$worldPath = Join-Path $scripts 'EAD_World.c'
$placementPath = Join-Path $scripts 'EAD_Placement.c'
$buildingsPath = Join-Path $scripts 'EAD_Buildings.c'
$zone = Remove-Comments (Read-Text $zonePath)
$world = Remove-Comments (Read-Text $worldPath)
$placement = Remove-Comments (Read-Text $placementPath)
$buildings = Remove-Comments (Read-Text $buildingsPath)

# ---------------------------------------------------------------------------------------
# ambient-and-small-1: occupancy back-off. Wake path, 30 s grace and the query are unchanged.
# ---------------------------------------------------------------------------------------
Assert ($zone -match 'protected\s+float\s+m_NextOccupancyCheck;') 'EAD_Zone must declare m_NextOccupancyCheck (no initializer)'
$proximity = Get-Body $zone 'void\s+Proximity\s*\(\s*array<IEntity>\s+observers,\s*float\s+now\s*\)'
Assert ($proximity -match 'if\s*\(\s*nearWake\s*\)\s*\{\s*m_Wanted\s*=\s*true;\s*m_EmptySince\s*=\s*-1;\s*\}') 'Proximity: the wake path must stay unchanged'
Assert ($proximity -match 'else\s+m_EmptySince\s*=\s*-1;') 'Proximity: the sleep band must still reset the empty period'
Assert ($proximity -match 'if\s*\(\s*m_EmptySince\s*<\s*0\s*\)\s*\{\s*m_EmptySince\s*=\s*now;\s*m_NextOccupancyCheck\s*=\s*0;\s*\}') 'Proximity: a new empty period must re-arm the occupancy check'
Assert ($proximity -match 'if\s*\(\s*m_Wanted\s*&&\s*now\s*-\s*m_EmptySince\s*>=\s*30\s*&&\s*now\s*>=\s*m_NextOccupancyCheck\s*\)\s*\{\s*if\s*\(\s*Occupied\(\)\s*\)\s*m_NextOccupancyCheck\s*=\s*now\s*\+\s*10;\s*else\s+m_Wanted\s*=\s*false;\s*\}') 'Proximity: after the 30 s grace an occupied zone must wait 10 s before asking again, an unoccupied one must sleep'
Assert ([regex]::Matches($proximity, '\bOccupied\s*\(\s*\)').Count -eq 1) 'Proximity must ask for occupancy in exactly one place'
Assert ([regex]::Matches($zone, 'm_EmptySince\s*=\s*-1;').Count -eq 6) 'the initial value and every reset of m_EmptySince (wake, sleep band, Enabled, generation ready, snapshot import) must stay; each reset re-arms the back-off'
$occupied = Get-Body $zone 'bool\s+Occupied\s*\(\s*\)'
Assert ($occupied -match 'QueryEntitiesBySphere\(GetOrigin\(\),\s*Radius\s*\+\s*15,\s*Occupant,\s*null,\s*EQueryEntitiesFlags\.DYNAMIC\)') 'Occupied must keep its radius (Radius + 15) and the dynamic filter'
$occupant = Get-Body $zone 'protected\s+bool\s+Occupant\s*\(\s*IEntity\s+entity\s*\)'
Assert ($occupant -match 'm_OccupancyVisits\s*>\s*512' -and $occupant -match 'ChimeraCharacter\.Cast\(entity\)\s*\|\|\s*Vehicle\.Cast\(entity\)') 'Occupant must keep the 512-visit cap and the character/vehicle test (both mean occupied)'

# Simulation against the 0.1.14 rule: same wake passes, never an earlier sleep, at most 10 s
# later when occupancy ends, and far fewer sphere queries while a zone stays occupied.
function Step-Proximity($State, [int]$Now, [bool]$NearWake, [bool]$NearSleep, [bool]$Occupied, [bool]$BackOff) {
 if ($NearWake) { $State.Wanted = $true; $State.EmptySince = -1 }
 elseif (!$NearSleep) {
  if ($State.EmptySince -lt 0) { $State.EmptySince = $Now; $State.NextCheck = 0 }
  if ($State.Wanted -and $Now - $State.EmptySince -ge 30 -and (!$BackOff -or $Now -ge $State.NextCheck)) {
   $State.Queries++
   if (!$Occupied) { $State.Wanted = $false }
   elseif ($BackOff) { $State.NextCheck = $Now + 10 }
  }
 }
 else { $State.EmptySince = -1 }
}
# Player near until $Leave, in the sleep band until $Far, away afterwards (optionally back
# near during [$Return, $Return2)); the zone is occupied until $FreeAt.
$scenarios = @(
 @{ Leave = 0; Far = 0; FreeAt = 0; Return = -1; Return2 = -1 }
 @{ Leave = 10; Far = 25; FreeAt = 31; Return = -1; Return2 = -1 }
 @{ Leave = 0; Far = 0; FreeAt = 37; Return = -1; Return2 = -1 }
 @{ Leave = 5; Far = 5; FreeAt = 333; Return = -1; Return2 = -1 }
 @{ Leave = 0; Far = 40; FreeAt = 1000; Return = 300; Return2 = 360 }
 @{ Leave = 20; Far = 60; FreeAt = 500; Return = 200; Return2 = 205 }
)
foreach ($s in $scenarios) {
 $old = @{ Wanted = $true; EmptySince = -1; NextCheck = 0; Queries = 0; Wakes = @(); Sleep = -1 }
 $new = @{ Wanted = $true; EmptySince = -1; NextCheck = 0; Queries = 0; Wakes = @(); Sleep = -1 }
 for ($t = 0; $t -lt 1200; $t++) {
  $returned = $s.Return -ge 0 -and $t -ge $s.Return -and $t -lt $s.Return2
  $nearWake = $t -lt $s.Leave -or $returned
  $nearSleep = $t -lt $s.Far -or $returned
  foreach ($pair in @(@($old, $false), @($new, $true))) {
   $state = $pair[0]; $before = $state.Wanted
   Step-Proximity $state $t $nearWake $nearSleep ($t -lt $s.FreeAt) $pair[1]
   if (!$before -and $state.Wanted) { $state.Wakes += $t }
   if ($before -and !$state.Wanted) { $state.Sleep = $t }
  }
 }
 $label = "scenario leave=$($s.Leave) far=$($s.Far) free=$($s.FreeAt) return=$($s.Return)"
 Assert (($old.Wakes -join ',') -eq ($new.Wakes -join ',')) "back-off must not change wake passes ($label)"
 Assert ($old.Sleep -ge 0 -and $new.Sleep -ge $old.Sleep -and $new.Sleep -le $old.Sleep + 10) "back-off sleep must come at most 10 s after the 0.1.14 sleep (${label}: $($old.Sleep) -> $($new.Sleep))"
 Assert ($new.Queries -le $old.Queries) "back-off must never ask for occupancy more often ($label)"
 if ($old.Queries -ge 100) { Assert ($new.Queries * 5 -le $old.Queries) "a long-occupied zone must query at least five times less often (${label}: $($old.Queries) -> $($new.Queries))" }
}

# ---------------------------------------------------------------------------------------
# ambient-and-small-2 (1): the suppressed-slot scan runs only while a suppression is pending.
# ---------------------------------------------------------------------------------------
Assert ($zone -match 'protected\s+bool\s+m_SuppressPending;') 'EAD_Zone must declare m_SuppressPending (no initializer)'
$suppressRecord = Get-Body $zone 'protected\s+void\s+SuppressRecord\s*\(\s*int\s+index\s*\)'
Assert ($suppressRecord -match 'Records\[index\]\.Suppressed\s*=\s*true;\s*m_SuppressPending\s*=\s*true;\s*Rpc\(RPC_Suppress,\s*m_Revision,\s*index\);') 'SuppressRecord must flag a pending suppression and still send RPC_Suppress'
$rpcSuppress = Get-Body $zone 'protected\s+void\s+RPC_Suppress\s*\(\s*int\s+revision,\s*int\s+index\s*\)'
Assert ($rpcSuppress -match 'Records\[index\]\.Suppressed\s*=\s*true;\s*m_SuppressPending\s*=\s*true;\s*if\s*\(\s*index\s*<\s*Live\.Count\(\)\s*&&\s*Live\[index\]\s*\)\s*SCR_EntityHelper\.DeleteEntityAndChildren\(Live\[index\]\);') 'RPC_Suppress must flag a pending suppression before its direct delete (a deferred delete is retried by Reconcile)'
# Only those two can suppress a record that may already be live; other paths fill an empty Live.
Assert ([regex]::Matches($zone, '\.Suppressed\s*=\s*true').Count -eq 2) 'only SuppressRecord and RPC_Suppress may set Suppressed on a zone record'
foreach ($file in Get-ChildItem -LiteralPath $scripts -Filter '*.c' -File | Where-Object Name -NE 'EAD_Zone.c') {
 Assert (!((Remove-Comments (Read-Text $file.FullName)) -match '\.Suppressed\s*=(?!=)')) "$($file.Name) must not change a record's Suppressed flag"
 Assert (!((Remove-Comments (Read-Text $file.FullName)) -match '\bLive\s*\.\s*(Insert|Set)\s*\(|\bLive\s*\[[^\]]+\]\s*=(?!=)')) "$($file.Name) must not put entities into a zone's Live slots"
}
$reconcile = Get-Body $zone 'bool\s+Reconcile\s*\(\s*float\s+now,\s*bool\s+allowSpawn\s*\)'
$loop = [regex]::Match($reconcile, 'int\s+slots;\s*if\s*\(\s*m_SuppressPending\s*\)\s*slots\s*=\s*Math\.MinInt\(Live\.Count\(\),\s*Records\.Count\(\)\);\s*for\s*\(\s*int\s+suppressed\s*=\s*0;\s*suppressed\s*<\s*slots;\s*suppressed\+\+\s*\)')
Assert $loop.Success 'Reconcile must scan min(Live, Records) suppressed slots only while m_SuppressPending is set'
$loopEnd = Get-BlockEnd $reconcile ($loop.Index + $loop.Length)
$loopBody = $reconcile.Substring($loop.Index + $loop.Length, $loopEnd - $loop.Index - $loop.Length)
Assert ($reconcile.Substring($loopEnd) -match '^\s*m_SuppressPending\s*=\s*false;\s*int\s+wanted\s*=\s*m_Target;') 'm_SuppressPending must be cleared only after a full pass that returned nothing'
Assert ($loopBody -match 'if\s*\(\s*!Records\[suppressed\]\.Suppressed\s*\|\|\s*!Live\[suppressed\]\s*\)\s*continue;' -and $loopBody -match 'SCR_EntityHelper\.DeleteEntityAndChildren\(lost\);\s*if\s*\(\s*lost\s*\)\s*return\s+true;' -and $loopBody -match 'Live\[suppressed\]\s*=\s*null;') 'the suppressed-slot loop body must stay as in 0.1.14 (deferred deletes keep the flag set)'
Assert (!($loopBody -match '\bbreak\s*;|\.(Insert|Remove\w*|Clear|Resize)\s*\(')) 'the loop must not resize Live or Records nor break out: the hoisted bound must equal the 0.1.14 per-iteration bound'
Assert ([regex]::Matches($loopBody, '\breturn\s+true\s*;').Count -eq 2) 'every exit from the loop body must be a return (or continue)'
# Live entities only ever come from the spawn path, which tombstones suppressed records.
Assert ([regex]::Matches($zone, '\bLive\.Insert\(').Count -eq 2 -and $reconcile -match 'Live\.Insert\(null\);' -and $reconcile -match 'Live\.Insert\(created\);') 'Live slots must be added only as a tombstone or a freshly spawned prop'
Assert ($reconcile.IndexOf('if (next.Suppressed)') -ge 0 -and $reconcile.IndexOf('if (next.Suppressed)') -lt $reconcile.IndexOf('Live.Insert(created);')) 'a suppressed record must become a tombstone before any spawn'

# ---------------------------------------------------------------------------------------
# ambient-and-small-5 (a): SupportStep returns for a zone with no live props.
# ---------------------------------------------------------------------------------------
$support = Get-Body $zone 'void\s+SupportStep\s*\(\s*\)'
Assert ($support -match '^\s*if\s*\(\s*!Replication\.IsServer\(\)\s*\|\|\s*EAD_Snapshot\.Loading\s*\|\|\s*Enabled\s*==\s*0\s*\|\|\s*!m_Ready\s*\|\|\s*m_Rebuild\s*\)\s*return;\s*if\s*\(\s*Live\.IsEmpty\(\)\s*\)\s*\{\s*int\s+recordCount\s*=\s*Records\.Count\(\);\s*if\s*\(\s*recordCount\s*>\s*0\s*&&\s*\(\s*m_WallCursor\s*<=\s*0\s*\|\|\s*m_WallCursor\s*>=\s*recordCount\s*\)\s*\)\s*m_WallCursor\s*=\s*recordCount;\s*return;\s*\}') 'SupportStep must return early (after its guard) for an empty Live, leaving the cursor where the full pass would'
Assert ($support -match 'for\s*\(\s*int\s+visited\s*=\s*0;\s*visited\s*<\s*Records\.Count\(\);\s*visited\+\+\s*\)\s*\{\s*if\s*\(\s*m_WallCursor\s*>=\s*Records\.Count\(\)\s*\)\s*m_WallCursor\s*=\s*0;\s*int\s+index\s*=\s*m_WallCursor\+\+;') 'SupportStep must keep its round-robin wall cursor loop'
Assert ($support -match 'index\s*>=\s*Live\.Count\(\)\s*\|\|\s*!Live\[index\]\s*\)\s*continue;') 'with Live empty every visit of the full pass continues, so only the cursor moves'
# The 0.1.14 pass over an empty Live versus the early return, for every cursor and size.
for ($count = 0; $count -le 9; $count++) {
 for ($cursor = 0; $cursor -le 13; $cursor++) {
  $full = $cursor
  for ($visited = 0; $visited -lt $count; $visited++) { if ($full -ge $count) { $full = 0 }; $full++ }
  $early = $cursor
  if ($count -gt 0 -and ($early -le 0 -or $early -ge $count)) { $early = $count }
  Assert ($full -eq $early) "SupportStep early return must leave the cursor where the full pass does (records=$count cursor=${cursor}: $full vs $early)"
 }
}

# ---------------------------------------------------------------------------------------
# ambient-and-small-5 (c) and -7: memoised prefab path classes, proven against 0.1.14.
# ---------------------------------------------------------------------------------------
$enum = [regex]::Match($placement, 'enum\s+EAD_PathClass\s*\{([^}]*)\}')
Assert $enum.Success 'EAD_PathClass enum not found'
$bits = [ordered]@{}
foreach ($m in [regex]::Matches($enum.Groups[1].Value, '(\w+)\s*=\s*(\d+)')) { $bits[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
# The 0.1.14 path tests, verbatim: StructuralWall, BuildingContext and EAD_Buildings.RejectedPath.
$expected = [ordered]@{
 WALL_EXCLUDED = @('fence', 'railing', 'gate', '/buildingparts/')
 STRUCTURAL = @('/structures/', '/walls/')
 CONTEXT_EXCLUDED = @('fence', '/walls/', '/buildingparts/', '/buildingaddons/', '/furniture/')
 CONTEXT_INCLUDED = @('/houses/', '/commercial/', '/industrial/', '/military/')
 REJECTED = @('/dst/', 'ruin', 'destroyed', 'rubble', 'debris', '/buildingparts/', '/buildingaddons/', '/furniture/')
 TEMPLATE = @('_base.et')
}
Assert ((@($bits.Keys) -join ',') -eq (@($expected.Keys) -join ',')) "EAD_PathClass must define exactly $(@($expected.Keys) -join ', ')"
$seen = 0
foreach ($name in $bits.Keys) {
 $value = $bits[$name]
 Assert ($value -gt 0 -and ($value -band ($value - 1)) -eq 0 -and ($seen -band $value) -eq 0) "EAD_PathClass.$name must be a distinct single bit"
 $seen = $seen -bor $value
}
$pathClasses = Get-Body $placement 'static\s+int\s+PathClasses\s*\(\s*string\s+resource\s*\)'
Assert ($pathClasses -match '^\s*EXPBG_LazyStatics_EAD_Placement\(\);\s*int\s+classes;\s*if\s*\(\s*s_PathClasses\.Find\(resource,\s*classes\)\s*\)\s*return\s+classes;\s*string\s+path\s*=\s*resource;\s*path\.ToLower\(\);\s*classes\s*=\s*0;') 'PathClasses must create the memo lazily, return a stored entry, and otherwise classify the lower-cased path from zero'
Assert ($pathClasses -match 'const\s+int\s+memoLimit\s*=\s*4096;\s*if\s*\(\s*s_PathClasses\.Count\(\)\s*<\s*memoLimit\s*\)\s*s_PathClasses\.Insert\(resource,\s*classes\);\s*return\s+classes;\s*$') 'PathClasses must store at most 4096 paths and classify the rest without storing'
Assert ($placement -match 'protected\s+static\s+ref\s+map<string,\s*int>\s+s_PathClasses;') 'the memo must be a lazily created static map<string, int> keyed by the path text'
$lazy = Get-Body $placement 'protected\s+static\s+void\s+EXPBG_LazyStatics_EAD_Placement\s*\(\s*\)'
Assert ($lazy -match 'if\s*\(\s*!s_PathClasses\s*\)\s*s_PathClasses\s*=\s*new\s+map<string,\s*int>\(\);') 'EXPBG_LazyStatics_EAD_Placement must create the memo on first use'
$parsed = [ordered]@{}
foreach ($m in [regex]::Matches($pathClasses, 'if\s*\((?<cond>[^;{}]*?)\)\s*classes\s*\|=\s*EAD_PathClass\.(?<bit>\w+);')) {
 $bit = $m.Groups['bit'].Value
 Assert (!$parsed.Contains($bit)) "PathClasses sets EAD_PathClass.$bit twice"
 $terms = foreach ($term in ($m.Groups['cond'].Value -split '\|\|')) {
  $contains = [regex]::Match($term.Trim(), '^path\.Contains\("([^"]+)"\)$')
  Assert $contains.Success "PathClasses: EAD_PathClass.$bit may only test path.Contains literals: '$($term.Trim())'"
  $contains.Groups[1].Value
 }
 $parsed[$bit] = @($terms)
}
foreach ($name in $expected.Keys) {
 Assert $parsed.Contains($name) "PathClasses never sets EAD_PathClass.$name"
 Assert ((@($parsed[$name] | Sort-Object -Unique) -join '|') -ceq (@($expected[$name] | Sort-Object -Unique) -join '|')) "EAD_PathClass.$name must test exactly the 0.1.14 fragments: $(@($expected[$name]) -join ', ') (found $(@($parsed[$name]) -join ', '))"
}
Assert ($parsed.Count -eq $expected.Count) 'PathClasses must set only the EAD_PathClass bits'
Assert ([regex]::Matches($placement, '\.ToLower\(\)').Count -eq 1 -and [regex]::Matches($placement, '\.Contains\(').Count -eq [regex]::Matches($pathClasses, '\.Contains\(').Count) 'EAD_Placement must lower-case and test prefab paths only inside PathClasses'
Assert (!($buildings -match '\.ToLower\(\)')) 'EAD_Buildings must classify prefab paths through EAD_Placement.PathClasses'

# Per-entity checks stay live and in their 0.1.14 order.
$static = Get-Body $placement 'protected\s+static\s+int\s+StaticPathClasses\s*\(\s*IEntity\s+entity\s*\)'
Assert ($static -match '^\s*if\s*\(\s*!entity\s*\|\|\s*ChimeraCharacter\.Cast\(entity\)\s*\|\|\s*Vehicle\.Cast\(entity\)\s*\)\s*return\s+-1;\s*Physics\s+physics\s*=\s*entity\.GetPhysics\(\);\s*if\s*\(\s*!physics\s*\|\|\s*physics\.IsDynamic\(\)\s*\)\s*return\s+-1;\s*EntityPrefabData\s+data\s*=\s*entity\.GetPrefabData\(\);\s*if\s*\(\s*!data\s*\)\s*return\s+-1;\s*return\s+PathClasses\(data\.GetPrefabName\(\)\);\s*$') 'StaticPathClasses must keep the character/vehicle, physics and prefab-data checks before the path'
$structural = Get-Body $placement 'protected\s+static\s+bool\s+IsStructural\s*\(\s*IEntity\s+entity,\s*int\s+classes\s*\)'
Assert ($structural -match '^\s*if\s*\(\s*classes\s*<\s*0\s*\|\|\s*\(\s*classes\s*&\s*EAD_PathClass\.WALL_EXCLUDED\s*\)\s*!=\s*0\s*\)\s*return\s+false;\s*return\s+SCR_DestructibleBuildingEntity\.Cast\(entity\)\s*\|\|\s*\(\s*classes\s*&\s*EAD_PathClass\.STRUCTURAL\s*\)\s*!=\s*0;\s*$') 'IsStructural must reject excluded paths, then accept destructible buildings or structural paths'
Assert ((Get-Body $placement 'static\s+bool\s+StructuralWall\s*\(\s*IEntity\s+entity\s*\)') -match '^\s*return\s+IsStructural\(entity,\s*StaticPathClasses\(entity\)\);\s*$') 'StructuralWall must be IsStructural over the entity path classes'
$context = Get-Body $placement 'static\s+bool\s+BuildingContext\s*\(\s*IEntity\s+entity\s*\)'
Assert ($context -match '^\s*if\s*\(\s*!entity\s*\|\|\s*!entity\.GetPrefabData\(\)\s*\)\s*return\s+false;\s*int\s+classes\s*=\s*StaticPathClasses\(entity\);\s*if\s*\(\s*!IsStructural\(entity,\s*classes\)\s*\|\|\s*\(\s*classes\s*&\s*EAD_PathClass\.CONTEXT_EXCLUDED\s*\)\s*!=\s*0\s*\)\s*return\s+false;\s*IEntity\s+parent\s*=\s*entity\.GetParent\(\);\s*if\s*\(\s*parent\s*&&\s*SCR_DestructibleBuildingEntity\.Cast\(parent\)\s*\)\s*return\s+false;\s*return\s+SCR_DestructibleBuildingEntity\.Cast\(entity\)\s*\|\|\s*\(\s*classes\s*&\s*EAD_PathClass\.CONTEXT_INCLUDED\s*\)\s*!=\s*0;\s*$') 'BuildingContext must keep its order: prefab data, structural wall, context exclusion, destructible parent, then destructible or context path'
$rejected = Get-Body $buildings 'protected\s+static\s+bool\s+RejectedPath\s*\(\s*ResourceName\s+resource,\s*bool\s+leaf\s*\)'
Assert ($rejected -match '^\s*int\s+classes\s*=\s*EAD_Placement\.PathClasses\(resource\);\s*if\s*\(\s*\(\s*classes\s*&\s*EAD_PathClass\.REJECTED\s*\)\s*!=\s*0\s*\)\s*return\s+true;\s*return\s+leaf\s*&&\s*\(\s*classes\s*&\s*EAD_PathClass\.TEMPLATE\s*\)\s*!=\s*0;\s*$') 'RejectedPath must reject ruin/debris/part paths, and _base.et templates for the leaf only'
$supported = Get-Body $buildings 'static\s+bool\s+IsSupported\s*\(\s*IEntity\s+entity\s*\)'
Assert ($supported -match 'RejectedPath\(data\.GetPrefabName\(\),\s*true\)' -and $supported -match 'for\s*\(\s*int\s+depth\s*=\s*0;\s*prefab\s*&&\s*depth\s*<\s*16;\s*depth\+\+\s*\)\s*\{\s*if\s*\(\s*RejectedPath\(prefab\.GetResourceName\(\),\s*false\)\s*\)\s*return\s+false;') 'IsSupported must still test the prefab and each of up to 16 ancestors'
Assert ($buildings -match 'static\s+const\s+int\s+MAX_CALLBACKS\s*=\s*512;' -and $buildings -match 'static\s+const\s+int\s+CELL_SIZE\s*=\s*64;') 'the building scan keeps 64 m cells and 512 callbacks (seeded layouts depend on the visit order)'

# Equivalence over a path corpus: the 0.1.14 decisions versus the memoised classes.
function Test-Any([string]$Path, [string[]]$Fragments) { foreach ($f in $Fragments) { if ($Path.Contains($f)) { return $true } }; return $false }
function Get-Classes([string]$Resource) {
 $path = $Resource.ToLowerInvariant(); $classes = 0
 foreach ($name in $parsed.Keys) { if (Test-Any $path $parsed[$name]) { $classes = $classes -bor $bits[$name] } }
 return $classes
}
$fragments = @($expected.Values | ForEach-Object { $_ } | Sort-Object -Unique) + @('', 'prefabs/props/', 'wall_01', 'house_', 'Ruins', 'FENCE', '/Structures/', '/Military/', '_Base.et', 'base.et', '/rocks/')
$corpus = [Collections.Generic.List[string]]::new()
foreach ($a in $fragments) { foreach ($b in $fragments) { $corpus.Add("{0123456789ABCDEF}Prefabs/$a/Item_$b.et"); $corpus.Add(("{FEDCBA9876543210}PREFABS/$a$b/X.ET").ToUpperInvariant()) } }
$checked = 0
foreach ($resource in $corpus) {
 $path = $resource.ToLowerInvariant()
 $classes = Get-Classes $resource
 foreach ($destructible in @($false, $true)) {
  $oldWall = !(Test-Any $path $expected.WALL_EXCLUDED) -and ($destructible -or (Test-Any $path $expected.STRUCTURAL))
  $newWall = !(($classes -band $bits.WALL_EXCLUDED) -ne 0) -and ($destructible -or ($classes -band $bits.STRUCTURAL) -ne 0)
  Assert ($oldWall -eq $newWall) "StructuralWall differs from 0.1.14 for '$resource' (destructible=$destructible)"
  foreach ($parentDestructible in @($false, $true)) {
   $oldContext = $oldWall -and !(Test-Any $path $expected.CONTEXT_EXCLUDED) -and !$parentDestructible -and ($destructible -or (Test-Any $path $expected.CONTEXT_INCLUDED))
   $newContext = $newWall -and ($classes -band $bits.CONTEXT_EXCLUDED) -eq 0 -and !$parentDestructible -and ($destructible -or ($classes -band $bits.CONTEXT_INCLUDED) -ne 0)
   Assert ($oldContext -eq $newContext) "BuildingContext differs from 0.1.14 for '$resource'"
   $checked++
  }
 }
 foreach ($leaf in @($false, $true)) {
  $oldRejected = (Test-Any $path $expected.REJECTED) -or ($leaf -and $path.Contains('_base.et'))
  $newRejected = ($classes -band $bits.REJECTED) -ne 0 -or ($leaf -and ($classes -band $bits.TEMPLATE) -ne 0)
  Assert ($oldRejected -eq $newRejected) "RejectedPath differs from 0.1.14 for '$resource' (leaf=$leaf)"
 }
}

# ---------------------------------------------------------------------------------------
# ambient-and-small-6: one slot-count broadcast per changed zone per world tick.
# ---------------------------------------------------------------------------------------
Assert ($zone -match 'protected\s+bool\s+m_CountDirty;') 'EAD_Zone must declare m_CountDirty (no initializer)'
Assert (!($reconcile -match '\bPublishCount\s*\(')) 'Reconcile must not broadcast a count per slot change'
Assert ([regex]::Matches($reconcile, 'if\s*\(\s*authority\s*\)\s*m_CountDirty\s*=\s*true;').Count -eq 3) 'Reconcile must mark the count dirty after each delete, tombstone and spawn (server only)'
Assert ([regex]::Matches($zone, '\bPublishCount\s*\(\s*\)\s*;').Count -eq 1) 'PublishCount must be called only from FlushCount'
Assert ((Get-Body $zone 'void\s+FlushCount\s*\(\s*\)') -match '^\s*if\s*\(\s*!m_CountDirty\s*\)\s*return;\s*m_CountDirty\s*=\s*false;\s*PublishCount\(\);\s*$') 'FlushCount must publish once and only when a slot changed'
Assert ((Get-Body $zone 'protected\s+void\s+PublishCount\s*\(\s*\)') -match '^\s*m_Target\s*=\s*Live\.Count\(\);\s*Rpc\(RPC_Count,\s*m_Revision,\s*m_Target\);\s*$') 'PublishCount must still send the live slot count for the current revision'
Assert ((Get-Body $zone 'override\s+bool\s+RplSave\s*\(') -match 'writer\.WriteInt\(Live\.Count\(\)\);') 'join-in-progress must still receive the exact live slot count'
Assert ((Get-Body $zone 'protected\s+void\s+AddRecord\s*\(') -match 'Rpc\(RPC_Record,' -and $zone -match 'Rpc\(RPC_Reset,\s*m_Revision\);') 'records and resets must stay unbatched'
$tick = Get-Body $world 'static\s+void\s+Tick\s*\(\s*\)'
$flush = [regex]::Match($tick, 'if\s*\(\s*authority\s*\)\s*\{\s*foreach\s*\(\s*EAD_Zone\s+(\w+)\s*:\s*s_Zones\s*\)\s*\{\s*if\s*\(\s*\1\s*\)\s*\1\.FlushCount\(\);\s*\}\s*\}')
Assert $flush.Success 'EAD_World.Tick must flush every zone count, on the server only'
Assert ([regex]::Matches($tick, '\bFlushCount\s*\(').Count -eq 1) 'EAD_World.Tick must flush counts once per tick'
$reconcileCall = $tick.IndexOf('zone.Reconcile(now, live < MAX_LIVE)')
$supportCall = $tick.IndexOf('supportZone.SupportStep();')
$buildingCall = $tick.IndexOf('buildingZone.BuildingsStep();')
Assert ($supportCall -ge 0 -and $reconcileCall -gt $supportCall -and $flush.Index -gt $reconcileCall -and $flush.Index -lt $buildingCall) 'the count flush must follow every reconcile call of the tick'
Assert ($world -match 'CallLater\(Tick,\s*100,\s*true\)' -and $world -match 'static\s+const\s+int\s+MAX_LIVE\s*=\s*1000;' -and $tick -match 'work\s*<\s*8\s*&&\s*zone\.Reconcile') 'the 100 ms world tick and its eight work items per tick are unchanged'

# ---------------------------------------------------------------------------------------
# Source hygiene for the four touched files.
# ---------------------------------------------------------------------------------------
# Imported addon sources keep their exact bytes (.gitattributes: addon/** -text): each file
# keeps the line endings it had in 0.1.14.
$lineEndings = @{ 'EAD_Zone.c' = 'LF'; 'EAD_World.c' = 'LF'; 'EAD_Placement.c' = 'LF'; 'EAD_Buildings.c' = 'CRLF' }
foreach ($path in @($zonePath, $worldPath, $placementPath, $buildingsPath)) {
 $raw = Read-Text $path
 $code = Remove-Comments $raw
 $name = Split-Path -Leaf $path
 if ($lineEndings[$name] -eq 'CRLF') { Assert (!($raw -match '(?<!\r)\n')) "$name must keep CRLF line endings" }
 else { Assert (!$raw.Contains("`r")) "$name must keep LF line endings" }
 Assert (([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $name"
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep|Wait)\b')) "reserved Enforce name used as a variable in $name"
 Assert (!($code -match 'Math\.RandomFloat\(')) "Math.RandomFloat must not be used: $name"
 Assert (!($code -match 'static\s+ref\s+[^;=]+=\s*(new|\{)' -or $code -match 'static\s+const\s+array<')) "$name must not add static collection initializers (0.1.13 Windows compile limit)"
}
# New non-void helpers keep each return on its own line.
foreach ($body in @($pathClasses, $static, $structural, $context, $rejected)) {
 foreach ($line in $body -split "`n") {
  if ($line -match '\bif\s*\(.*\)\s*return\s+[^;\s]' -and $line -notmatch 'parent\s*&&\s*SCR_DestructibleBuildingEntity\.Cast\(parent\)') { throw "FAIL: a non-void return must sit on its own line: $($line.Trim())" }
 }
}
Assert ([regex]::Matches($zone, '\bQueryEntitiesBySphere\b').Count -eq 1) 'EAD_Zone keeps one occupancy query'
"PASS: Ambient Destruction performance (WP14): occupancy back-off (same wake, sleep at most 10 s later, $($scenarios.Count) simulated timelines), suppressed-slot scan only while pending, SupportStep early return with an exact cursor, memoised path classes equal to 0.1.14 over $($corpus.Count) paths ($checked context cases), one count broadcast per changed zone per tick."
