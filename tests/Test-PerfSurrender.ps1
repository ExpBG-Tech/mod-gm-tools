#requires -Version 7.0
# Portable guard for the WP12 AI Surrender performance fixes (0.1.15 performance plan).
#  ai-modules-01: a dedicated server (System.IsConsoleApp) clears the interrogation point's
#   frame event in EOnInit but keeps its collider; ESR_SurrenderManager.Upkeep (5 s, one
#   shot) calls Point.Follow() before the unchanged "> 1 m from his face" respawn test, and
#   also for an unconscious or possessed prisoner it otherwise skips. Clients and a
#   listen-server host keep EOnFrame: 0.1 s while the local controlled entity is within
#   20 m of the point, 1 s otherwise. Follow keeps the head bone index per prisoner entity
#   (looked up again for a new entity, never kept while missing); FacePosition, used by the
#   manager, still looks it up and computes the face with the same math.
#  ai-modules-04: PublishMarker records each marker id in a lazily created FIFO
#   (s_aMarkerIds); past MAX_MARKERS = 128 ids it drops the oldest id and removes that
#   marker through the RemoveMarker body (DropMarker) only if it still exists. Constant
#   work per marker; cleared in OnFirstModule (ids are numbered per world).
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/ai-surrender/Scripts/Game/EXPSR'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
# Source without // comments (neither file has // inside a string literal; asserted below).
function Read-Code([string]$Name) {
 $text = [IO.File]::ReadAllText((Join-Path $scripts $Name))
 Assert (![regex]::IsMatch($text, '"[^"\n]*//')) "$Name has '//' inside a string; this guard's comment stripping would misread it"
 [regex]::Replace($text, '(?m)//.*$', '')
}
# Returns @{ Start; Open; Close; Body } for the first brace block after $Signature (from $From).
function Get-Block([string]$Text, [string]$Signature, [int]$From = 0) {
 $match = [regex]::new($Signature).Match($Text, $From)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index + $match.Length); $depth = 0
 Assert ($open -ge 0) "no block after: $Signature"
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return @{ Start = $match.Index; Open = $open; Close = $i; Body = $Text.Substring($open + 1, $i - $open - 1) } } }
 }
 throw "FAIL: unbalanced block for $Signature"
}
function Count([string]$Text, [string]$Pattern) { [regex]::Matches($Text, $Pattern).Count }
function Const([string]$Text, [string]$Type, [string]$Name) {
 $match = [regex]::Match($Text, "static\s+const\s+$Type\s+$Name\s*=\s*([0-9.]+)\s*;")
 Assert $match.Success "constant $Name not found"
 [double]$match.Groups[1].Value
}
# A new non-void method keeps every return on its own line (never "if (x) return y;").
function Assert-ReturnsOnOwnLine([string]$Body, [string]$Name) {
 Assert (!([regex]::IsMatch($Body, '(?m)^\s*(if|else)\b[^\n]*\breturn\b'))) "$Name must keep each return on its own line"
}

$point = Read-Code 'ESR_InterrogationPoint.c'
$manager = Read-Code 'ESR_SurrenderManager.c'

# ---------------------------------------------------------------------------------------
# ai-modules-01: interrogation point follow
# ---------------------------------------------------------------------------------------
$pointClass = (Get-Block $point 'class\s+ESR_InterrogationPoint\s*:\s*GenericEntity').Body
Assert ((Get-Block $pointClass 'void\s+ESR_InterrogationPoint\s*\(').Body -match 'SetEventMask\(EntityEvent\.INIT \| EntityEvent\.FRAME\);') 'every point still registers INIT and FRAME (clients need the frame follow)'

$init = (Get-Block $pointClass 'override\s+void\s+EOnInit\s*\(\s*IEntity\s+owner\s*\)').Body
$playMode = Get-Block $init 'if\s*\(\s*!\s*GetGame\(\)\.InPlayMode\(\)\s*\)'
Assert ($playMode.Body -match 'ClearEventMask\(EntityEvent\.FRAME\);' -and $playMode.Body -match 'return;') 'outside play mode the frame event is still cleared before anything else'
$console = [regex]::Match($init, 'if\s*\(\s*System\.IsConsoleApp\(\)\s*\)\s*ClearEventMask\(\s*EntityEvent\.FRAME\s*\)\s*;')
Assert $console.Success 'EOnInit must clear the frame event on a dedicated server: if (System.IsConsoleApp()) ClearEventMask(EntityEvent.FRAME);'
Assert ($console.Index -gt $playMode.Close) 'the dedicated-server clear comes after the play-mode check'
$collider = $init.IndexOf('Physics.CreateStaticEx(this, geoms)')
Assert ($collider -gt $console.Index) 'the dedicated server still creates the interaction collider (the clear must not return early)'
Assert (!([regex]::IsMatch($init.Substring($console.Index + $console.Length, $collider - $console.Index - $console.Length), 'IsConsoleApp[^;]*return'))) 'no dedicated-server return before the collider'

$frame = (Get-Block $pointClass 'override\s+void\s+EOnFrame\s*\(\s*IEntity\s+owner\s*,\s*float\s+timeSlice\s*\)').Body
Assert ([regex]::IsMatch($frame, '^\s*m_fFollowIn\s*-=\s*timeSlice\s*;\s*if\s*\(\s*m_fFollowIn\s*>\s*0\s*\)\s*return\s*;\s*m_fFollowIn\s*=\s*FollowInterval\(\)\s*;\s*Follow\(\)\s*;\s*$')) 'EOnFrame keeps its countdown and takes the next interval from FollowInterval()'
$interval = (Get-Block $pointClass 'protected\s+float\s+FollowInterval\s*\(\s*\)').Body
Assert ($interval -match 'IEntity\s+(\w+)\s*=\s*SCR_PlayerController\.GetLocalControlledEntity\(\)\s*;') 'the interval is measured from the local controlled entity'
$viewer = $Matches[1]
Assert ([regex]::IsMatch($interval, "if\s*\(\s*$viewer\s*&&\s*vector\.DistanceSq\(\s*$viewer\.GetOrigin\(\)\s*,\s*GetOrigin\(\)\s*\)\s*<\s*NEAR_DISTANCE\s*\*\s*NEAR_DISTANCE\s*\)\s*return\s+FOLLOW_INTERVAL\s*;\s*return\s+FAR_FOLLOW_INTERVAL\s*;\s*$")) 'near (DistanceSq < NEAR_DISTANCE^2) follows at FOLLOW_INTERVAL, otherwise (or without an entity) at FAR_FOLLOW_INTERVAL'
Assert-ReturnsOnOwnLine $interval 'FollowInterval'
$near = Const $pointClass 'float' 'FOLLOW_INTERVAL'
$far = Const $pointClass 'float' 'FAR_FOLLOW_INTERVAL'
$nearDistance = Const $pointClass 'float' 'NEAR_DISTANCE'
$range = Const $point 'float' 'RANGE'
Assert ($near -eq 0.1 -and $far -eq 1 -and $nearDistance -eq 20) "intervals 0.1 s / 1 s and the 20 m radius (found $near / $far / $nearDistance)"
$prefab = [IO.File]::ReadAllText((Join-Path $repo 'addon/ai-surrender/Prefabs/EXPSR/ESR_InterrogationPoint.et'))
$visibility = [regex]::Match($prefab, 'VisibilityRange\s+([0-9.]+)')
Assert $visibility.Success 'the point prefab keeps its VisibilityRange'
$reach = [Math]::Max($range, [double]$visibility.Groups[1].Value)
# A sprinting player (under 8 m/s) needs longer to close NEAR_DISTANCE to the interaction reach
# than one far interval, so a point is back on 0.1 s before anyone can aim at it.
Assert ((($nearDistance - $reach) / 8) -gt $far) "a far point must switch to the fast follow before a sprinting player reaches it (reach $reach m)"

$face = (Get-Block $pointClass 'static\s+bool\s+FacePosition\s*\(\s*IEntity\s+character\s*,\s*out\s+vector\s+face\s*\)').Body
Assert ([regex]::IsMatch($face, 'FaceFromBone\(\s*character\s*,\s*animation\s*,\s*animation\.GetBoneIndex\(\s*HEAD_BONE\s*\)\s*,\s*candidate\s*\)')) 'FacePosition (manager PointPosition) still looks the head bone up on every call'
Assert ($face -match 'if \(!character\) return false;' -and $face -match 'Animation animation = character\.GetAnimation\(\);' -and $face -match 'if \(!animation\) return false;') 'FacePosition keeps its null checks'
$fromBone = (Get-Block $pointClass 'protected\s+static\s+bool\s+FaceFromBone\s*\(\s*IEntity\s+character\s*,\s*Animation\s+animation\s*,\s*TNodeId\s+bone\s*,\s*out\s+vector\s+face\s*\)').Body
foreach ($line in @(
  'if\s*\(\s*bone\s*<\s*0\s*\)\s*return\s+false\s*;',
  'vector\s+head\[4\]\s*;\s*if\s*\(\s*!\s*animation\.GetBoneMatrix\(\s*bone\s*,\s*head\s*\)\s*\)\s*return\s+false\s*;',
  'vector\s+world\[4\]\s*;\s*character\.GetWorldTransform\(\s*world\s*\)\s*;\s*Math3D\.MatrixMultiply4\(\s*world\s*,\s*head\s*,\s*head\s*\)\s*;',
  'vector\s+candidate\s*=\s*head\[3\]\s*\+\s*head\[1\]\s*\*\s*FACE_UP\s*-\s*head\[2\]\s*\*\s*FACE_FORWARD\s*;',
  'float\s+distanceSq\s*=\s*vector\.DistanceSq\(\s*candidate\s*,\s*character\.GetOrigin\(\)\s*\)\s*;\s*if\s*\(\s*!\s*\(\s*distanceSq\s*<=\s*MAX_FACE_DISTANCE\s*\*\s*MAX_FACE_DISTANCE\s*\)\s*\)\s*return\s+false\s*;',
  'face\s*=\s*candidate\s*;\s*return\s+true\s*;\s*$')) {
 Assert ([regex]::IsMatch($fromBone, $line)) "the face math is unchanged: $line"
}
Assert-ReturnsOnOwnLine $fromBone 'FaceFromBone'

Assert ([regex]::IsMatch($pointClass, '(?m)^\s*protected\s+SCR_ChimeraCharacter\s+m_HeadOwner\s*;') -and [regex]::IsMatch($pointClass, '(?m)^\s*protected\s+TNodeId\s+m_iHeadBone\s*;')) 'the cached bone is keyed on a weak prisoner reference (no ref, no initializer)'
$prisonerFace = (Get-Block $pointClass 'protected\s+bool\s+PrisonerFace\s*\(\s*out\s+vector\s+face\s*\)').Body
Assert ([regex]::IsMatch($prisonerFace, 'SCR_ChimeraCharacter\s+prisoner\s*=\s*GetPrisoner\(\)\s*;\s*if\s*\(\s*!\s*prisoner\s*\)\s*return\s+false\s*;\s*Animation\s+animation\s*=\s*prisoner\.GetAnimation\(\)\s*;\s*if\s*\(\s*!\s*animation\s*\)\s*return\s+false\s*;')) 'Follow resolves the prisoner and his animation on every call'
$resolve = Get-Block $prisonerFace 'if\s*\(\s*prisoner\s*!=\s*m_HeadOwner\s*\)'
Assert ([regex]::IsMatch($resolve.Body, '^\s*TNodeId\s+bone\s*=\s*animation\.GetBoneIndex\(\s*HEAD_BONE\s*\)\s*;\s*if\s*\(\s*bone\s*<\s*0\s*\)\s*return\s+false\s*;\s*m_iHeadBone\s*=\s*bone\s*;\s*m_HeadOwner\s*=\s*prisoner\s*;\s*$')) 'the bone is looked up again for a new prisoner entity and kept only once found'
Assert ((Count $pointClass 'm_HeadOwner\s*=') -eq 1 -and (Count $pointClass 'm_iHeadBone\s*=') -eq 1) 'the cache is written in one place only'
Assert ([regex]::IsMatch($prisonerFace.Substring($resolve.Close - $resolve.Start), 'FaceFromBone\(\s*prisoner\s*,\s*animation\s*,\s*m_iHeadBone\s*,\s*candidate\s*\)')) 'Follow computes the face from the cached bone with the shared math'
Assert-ReturnsOnOwnLine $prisonerFace 'PrisonerFace'

Assert ([regex]::IsMatch($pointClass, '(?m)^\s*bool\s+Follow\s*\(\s*\)')) 'Follow() stays public for the manager upkeep'
$follow = (Get-Block $pointClass '(?m)^\s*bool\s+Follow\s*\(\s*\)').Body
Assert ([regex]::IsMatch($follow, 'vector\s+face\s*;\s*if\s*\(\s*!\s*PrisonerFace\(\s*face\s*\)\s*\)\s*return\s+false\s*;')) 'Follow reads the face through the cached-bone path'
Assert ($follow -match 'vector\.DistanceSq\(previous, face\) <= FOLLOW_TOLERANCE \* FOLLOW_TOLERANCE\) return true;' -and $follow -match 'SetWorldTransform\(transform\);' -and $follow -match 'Update\(\);' -and $follow -match 'if \(Replication\.IsServer\(\)\) Restream\(previous\);') 'Follow keeps the 2 cm tolerance, the collider commit and the server restream'

$upkeep = (Get-Block $manager 'protected\s+static\s+void\s+Upkeep\s*\(\s*\)').Body
Assert ($manager -match 'static const int UPKEEP_MS = 5000;' -and (Get-Block $manager 'protected\s+static\s+void\s+StartUpkeep\s*\(\s*\)').Body -match 'CallLater\(ESR_SurrenderManager\.Upkeep, UPKEEP_MS, false\);') 'the upkeep stays a 5 s one-shot timer'
$skip = Get-Block $upkeep 'if\s*\(\s*controller\.GetLifeState\(\)\s*!=\s*ECharacterLifeState\.ALIVE\s*\|\|\s*controller\.IsUnconscious\(\)\s*\|\|\s*IsPlayerCharacter\(prisoner\.Character\)\s*\)'
Assert ([regex]::IsMatch($skip.Body, 'if\s*\(\s*prisoner\.Point\s*\)\s*prisoner\.Point\.Follow\(\)\s*;\s*continue\s*;\s*$')) 'an unconscious or possessed prisoner keeps his point on his face (server streaming relevance)'
$respawn = [regex]::Match($upkeep, 'vector\s+face\s*=\s*PointPosition\(\s*prisoner\s*\)\s*;\s*if\s*\(\s*!\s*prisoner\.Point\s*\)\s*SpawnPoint\(\s*prisoner\s*\)\s*;\s*else\s*\{\s*prisoner\.Point\.Follow\(\)\s*;\s*if\s*\(\s*vector\.DistanceSq\(\s*prisoner\.Point\.GetOrigin\(\)\s*,\s*face\s*\)\s*>\s*1\s*\)\s*\{\s*DeletePoint\(\s*prisoner\s*\)\s*;\s*SpawnPoint\(\s*prisoner\s*\)\s*;\s*\}\s*\}')
Assert $respawn.Success 'Upkeep calls prisoner.Point.Follow() before the unchanged > 1 m respawn test (mandatory: otherwise a carried prisoner point is respawned every 5 s)'
Assert ((Count $upkeep 'Point\.Follow\(\)') -eq 2) 'Upkeep follows each living prisoner point exactly once per pass'

# ---------------------------------------------------------------------------------------
# ai-modules-04: marker FIFO cap
# ---------------------------------------------------------------------------------------
$managerClass = (Get-Block $manager 'class\s+ESR_SurrenderManager\s*\r?\n').Body
Assert ((Const $managerClass 'int' 'MAX_MARKERS') -eq 128) 'the marker cap is 128'
Assert ([regex]::IsMatch($managerClass, '(?m)^\s*protected\s+static\s+ref\s+array<int>\s+s_aMarkerIds\s*;')) 's_aMarkerIds is declared without an initializer (0.1.13 lazy statics)'
Assert (!(Count $manager 'static\s+[^;(]*=\s*(new\b|\{)') -and !(Count $point 'static\s+[^;(]*=\s*(new\b|\{)')) 'no static initializer with a value in either file (0.1.13 Windows compile limit)'
$lazy = (Get-Block $managerClass 'protected\s+static\s+void\s+EXPBG_LazyStatics_ESR_SurrenderManager\s*\(\s*\)').Body
Assert ([regex]::IsMatch($lazy, 'if\s*\(\s*!\s*s_aMarkerIds\s*\)\s*s_aMarkerIds\s*=\s*new\s+array<int>\(\)\s*;') -and (Count $manager 's_aMarkerIds\s*=\s*new\b') -eq 1) 'the FIFO is created only in EXPBG_LazyStatics_ESR_SurrenderManager'

$first = (Get-Block $managerClass 'static\s+void\s+OnFirstModule\s*\(\s*\)').Body
Assert ([regex]::IsMatch($first, '^\s*EXPBG_LazyStatics_ESR_SurrenderManager\(\)\s*;') -and $first -match 's_aMarkerIds\.Clear\(\);') 'OnFirstModule clears the FIFO (marker ids are numbered per world)'

$publish = (Get-Block $managerClass 'protected\s+static\s+int\s+PublishMarker\s*\(').Body
Assert ([regex]::IsMatch($publish, 'markers\.OnAddSynchedMarker\(marker\)\s*;\s*markers\.OnAskAddStaticMarker\(marker\)\s*;')) 'the marker is published as before'
Assert ([regex]::IsMatch($publish, 'int\s+markerId\s*=\s*marker\.GetMarkerID\(\)\s*;\s*TrackMarker\(\s*markerId\s*\)\s*;\s*return\s+markerId\s*;\s*$')) 'PublishMarker records the id it returns, after the lifetime timer'
Assert ((Count $managerClass 'TrackMarker\(') -eq 2) 'TrackMarker is called from PublishMarker only'

$track = (Get-Block $managerClass 'protected\s+static\s+void\s+TrackMarker\s*\(\s*int\s+markerId\s*\)').Body
Assert ([regex]::IsMatch($track, '^\s*EXPBG_LazyStatics_ESR_SurrenderManager\(\)\s*;\s*if\s*\(\s*markerId\s*<\s*0\s*\)\s*return\s*;\s*s_aMarkerIds\.Insert\(\s*markerId\s*\)\s*;\s*if\s*\(\s*s_aMarkerIds\.Count\(\)\s*<=\s*MAX_MARKERS\s*\)\s*return\s*;\s*int\s+(\w+)\s*=\s*s_aMarkerIds\[0\]\s*;\s*s_aMarkerIds\.RemoveOrdered\(\s*0\s*\)\s*;\s*if\s*\(\s*DropMarker\(\s*\1\s*\)\s*\)\s*Trace\(')) 'TrackMarker: insert, and past the cap drop the oldest id and remove its marker only if it still exists'
Assert (!([regex]::IsMatch($track, '\b(for|foreach|while)\b'))) 'TrackMarker does constant work (no loop over the FIFO)'
Assert (!(Count $track '\bPrint(Format)?\s*\(') -and (Count $manager '\bPrint(Format)?\s*\(') -eq 7) 'the cap logs through the diagnostics Trace only (G4 pin: 7 log calls in the manager)'

$remove = (Get-Block $managerClass 'protected\s+static\s+void\s+RemoveMarker\s*\(\s*int\s+markerId\s*,\s*float\s+due\s*\)').Body
Assert ([regex]::IsMatch($remove, '^\s*if\s*\(\s*Now\(\)\s*\+\s*1\s*<\s*due\s*\|\|\s*markerId\s*<\s*0\s*\)\s*return\s*;\s*if\s*\(\s*DropMarker\(\s*markerId\s*\)\s*\)\s*Trace\(string\.Format\("intel marker %1 expired", markerId\)\)\s*;\s*$')) 'RemoveMarker keeps its earlier-world guard and its trace, and removes through DropMarker'
$drop = (Get-Block $managerClass 'protected\s+static\s+bool\s+DropMarker\s*\(\s*int\s+markerId\s*\)').Body
Assert ([regex]::IsMatch($drop, 'SCR_MapMarkerManagerComponent\s+markers\s*=\s*SCR_MapMarkerManagerComponent\.GetInstance\(\)\s*;\s*if\s*\(\s*!\s*markers\s*\)\s*return\s+false\s*;\s*if\s*\(\s*!\s*markers\.GetStaticMarkerByID\(\s*markerId\s*\)\s*&&\s*!\s*markers\.GetDisabledMarkerByID\(\s*markerId\s*\)\s*\)\s*return\s+false\s*;\s*markers\.OnRemoveSynchedMarker\(\s*markerId\s*\)\s*;\s*markers\.OnAskRemoveStaticMarker\(\s*markerId\s*\)\s*;\s*return\s+true\s*;\s*$')) 'DropMarker is the old RemoveMarker body: only an existing marker is removed, on the server and every client'
Assert-ReturnsOnOwnLine $drop 'DropMarker'

# Model of TrackMarker as written: never more than MAX_MARKERS of our markers alive, nothing
# removed while at most MAX_MARKERS were published, only the oldest tracked id is ever removed
# and only when its marker still exists, and never a marker this module did not publish.
function New-Model { @{ Fifo = [System.Collections.Generic.List[int]]::new(); Live = [System.Collections.Generic.HashSet[int]]::new(); Removed = [System.Collections.Generic.List[int]]::new() } }
function Publish($Model, [int]$Id) {
 [void]$Model.Live.Add($Id)
 $Model.Fifo.Add($Id)
 if ($Model.Fifo.Count -le 128) { return }
 $oldest = $Model.Fifo[0]
 $Model.Fifo.RemoveAt(0)
 if ($Model.Live.Remove($oldest)) { $Model.Removed.Add($oldest) }
}
$m = New-Model
1..128 | ForEach-Object { Publish $m $_ }
Assert ($m.Removed.Count -eq 0 -and $m.Live.Count -eq 128) 'model: nothing is removed up to the cap'
Publish $m 129
Assert ($m.Removed.Count -eq 1 -and $m.Removed[0] -eq 1 -and $m.Live.Count -eq 128) 'model: the 129th marker removes the oldest one'
$m = New-Model
1..128 | ForEach-Object { Publish $m $_ }
[void]$m.Live.Remove(1)
Publish $m 129
Assert ($m.Removed.Count -eq 0 -and $m.Live.Count -eq 128 -and $m.Fifo.Count -eq 128) 'model: an id deleted from the map is dropped for free'
$m = New-Model
$random = [Random]::new(12)
for ($id = 1; $id -le 3000; $id++) {
 # Players delete some markers from the map; the lifetime timer expires others.
 if ($m.Live.Count -gt 0 -and $random.Next(3) -eq 0) { [void]$m.Live.Remove(@($m.Live)[$random.Next($m.Live.Count)]) }
 $front = -1
 if ($m.Fifo.Count -gt 0) { $front = $m.Fifo[0] }
 $full = $m.Fifo.Count -eq 128
 $removedBefore = $m.Removed.Count
 Publish $m $id
 Assert ($m.Live.Count -le 128 -and $m.Fifo.Count -le 128) 'model: at most 128 tracked ids and 128 of our markers on the map'
 if ($m.Removed.Count -gt $removedBefore) {
  Assert ($full -and $m.Removed[-1] -eq $front) 'model: only the oldest tracked marker is removed, and only once 128 ids are tracked'
 }
}
Assert ($m.Removed.Count -gt 0) 'model: the random run reaches the cap'

# ---------------------------------------------------------------------------------------
# Enforce gotchas in both sources; ASCII with LF.
# ---------------------------------------------------------------------------------------
foreach ($name in @('ESR_InterrogationPoint.c', 'ESR_SurrenderManager.c')) {
 $path = Join-Path $scripts $name
 $text = [IO.File]::ReadAllText($path)
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName|TNodeId|Animation)\s+(owned|Sleep|Wait|local)\b')) "reserved Enforce name used as a variable in $name"
 Assert (!$text.Contains("`r")) "CR line ending in $name"
 Assert ((([IO.File]::ReadAllBytes($path)) | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $name"
}
foreach ($body in @($interval, $fromBone, $prisonerFace, $track, $drop)) {
 Assert (!($body -match 'Math\.RandomFloat|Math\.RandomInt')) 'the new methods draw no random numbers'
}
Write-Host 'PASS: AI Surrender WP12: a dedicated server clears the interrogation point frame event (collider kept) and Upkeep calls Point.Follow() before the unchanged 1 m respawn test (and for skipped prisoners); clients follow at 0.1 s within 20 m of the local entity and 1 s farther; the head bone is cached per prisoner entity with the unchanged face math; published markers are capped at 128 through a lazily created FIFO that removes only the oldest still-existing marker via the old RemoveMarker body and is cleared per world. No engine was launched.'
