#requires -Version 7.0
# Portable guard for the WP8 civilian module performance fixes (0.1.15 performance plan).
#  civilians-a-04: clients and edit-mode worlds clear FRAME|POSTFRAME in EAC_AmbientModule.EOnInit;
#   both handlers already returned at once there (GetActive() is server-only and never returns a
#   module that stopped at that guard). The server and a listen-server host keep the mask.
#  civilians-a-03: a weak registry of every module replica (play mode, before the server return)
#   feeds AnyDebugOverlayRequested (DebugLevel > 0 or DebugDraw == 1). The GM debug view sends its
#   1 s EAC_RequestDebug only while that is true, on the same cadence, and returns before Clear()
#   while nothing is shown.
#  civilians-a-06: EAC_HomeScanCursor.CleanProbes skips the 64-probe ring loop once every cell of
#   the ring is known to be indexed; DiscoverCellAt(position) still runs first.
#  civilians-a-07: m_CellHomes is keyed by the packed int CellKey instead of an "x:z" string.
#  civilians-b-03: the scene vocabulary memo holds 4096 paths, with an unmemoised-Resolve counter.
#  civilians-b-04: EAC_SessionGroupExclusive tests the claim's own actor before the helper walk.
#  civilians-b-11: ReservesGroup runs a pointer-compare pass over claim.Group first.
# Source checks plus small models of the old and new logic. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/ambient-civilians/Scripts/Game/EXPAC'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
# Source with comments removed (string literals kept) and LF line ends.
function Read-Code([string]$Name) {
 $text = [IO.File]::ReadAllText((Join-Path $scripts $Name)).Replace("`r`n", "`n")
 [regex]::Replace($text, '"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return $m.Value }
  return ''
 })
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
function Squash([string]$Text) { [regex]::Replace($Text, '\s+', ' ').Trim() }

# ---------------------------------------------------------------------------------------
# EAC_AmbientModule.c: client frame mask (a-04) and the replica registry (a-03)
# ---------------------------------------------------------------------------------------
$module = Read-Code 'EAC_AmbientModule.c'
Assert ([regex]::IsMatch($module, '(?m)^\s*protected\s+static\s+ref\s+array<EAC_AmbientModule>\s+s_KnownModules\s*;')) 's_KnownModules must be a protected static ref array<EAC_AmbientModule> declared without an initializer (0.1.13 lazy statics)'
Assert (!(Count $module 'static\s+[^;(]*=\s*(new\b|\{)')) 'EAC_AmbientModule.c must add no static initializer with a value'
$lazy = Get-Block $module 'protected\s+static\s+void\s+EXPBG_LazyStatics_EAC_AmbientModule\s*\(\s*\)'
Assert ([regex]::IsMatch($lazy.Body, 'if\s*\(\s*!\s*s_KnownModules\s*\)\s*s_KnownModules\s*=\s*new\s+array<EAC_AmbientModule>\s*\(\s*\)\s*;')) 's_KnownModules must be created in EXPBG_LazyStatics_EAC_AmbientModule'
Assert ((Count $module 's_KnownModules\s*=') -eq 1) 's_KnownModules may only be assigned in the lazy-statics function'

$ctor = Get-Block $module 'void\s+EAC_AmbientModule\s*\(\s*IEntitySource\s+src\s*,\s*IEntity\s+parent\s*\)'
Assert ((Squash $ctor.Body) -ceq 'SetEventMask(EntityEvent.INIT | EntityEvent.FRAME | EntityEvent.POSTFRAME);') 'the constructor must still set INIT|FRAME|POSTFRAME on every machine'
$init = Get-Block $module 'override\s+void\s+EOnInit\s*\(\s*IEntity\s+owner\s*\)'
$initHead = 'EXPBG_LazyStatics_EAC_AmbientModule(); super.EOnInit(owner); if (GetGame().InPlayMode() && !s_KnownModules.Contains(this)) s_KnownModules.Insert(this); if (!GetGame().InPlayMode() || !Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME | EntityEvent.POSTFRAME); return; } NormalizeSettings(); s_Modules.Insert(this);'
Assert ((Squash $init.Body).StartsWith($initHead, [StringComparison]::Ordinal)) 'EOnInit must register the replica in play mode, then clear FRAME|POSTFRAME and return on clients and edit-mode worlds, before the server-only registration'
Assert ((Count $module '\bClearEventMask\s*\(') -eq 1) 'ClearEventMask may only appear in the EOnInit client/edit-mode guard'
Assert ((Count $module 's_Modules\.Insert\s*\(') -eq 1) 's_Modules must stay server-only (one insert, after the guard)'
# Exactness precondition: both handlers return at once unless GetActive() is this module,
# and GetActive() only ever returns a server, play-mode module from s_Modules.
$post = Get-Block $module 'override\s+void\s+EOnPostFrame\s*\(\s*IEntity\s+owner\s*,\s*float\s+timeSlice\s*\)'
Assert ((Squash $post.Body).StartsWith('if (GetActive() != this || !s_Traffic) return;', [StringComparison]::Ordinal)) 'EOnPostFrame must still return first unless this is the active module'
$frame = Get-Block $module 'override\s+void\s+EOnFrame\s*\(\s*IEntity\s+owner\s*,\s*float\s+timeSlice\s*\)'
Assert ((Squash $frame.Body).StartsWith('if (GetActive() != this || !s_HomeIndex) return;', [StringComparison]::Ordinal)) 'EOnFrame must still return first unless this is the active module'
$active = Get-Block $module 'static\s+EAC_AmbientModule\s+GetActive\s*\(\s*\)'
Assert ((Squash $active.Body) -ceq 'EXPBG_LazyStatics_EAC_AmbientModule(); if (!Replication.IsServer() || !GetGame()) return null; foreach (EAC_AmbientModule module : s_Modules) if (module && module.GetWorld() == GetGame().GetWorld()) return module; return null;') 'GetActive must be unchanged (server-only, from s_Modules)'
$dtor = Get-Block $module 'void\s+~EAC_AmbientModule\s*\(\s*\)'
Assert ([regex]::IsMatch($dtor.Body, 's_Modules\.RemoveItem\(\s*this\s*\)\s*;\s*s_KnownModules\.RemoveItem\(\s*this\s*\)\s*;')) 'the destructor must remove the module from both registries'
$any = Get-Block $module 'static\s+bool\s+AnyDebugOverlayRequested\s*\(\s*\)'
Assert ((Squash $any.Body) -ceq 'EXPBG_LazyStatics_EAC_AmbientModule(); for (int i = s_KnownModules.Count() - 1; i >= 0; i--) { EAC_AmbientModule module = s_KnownModules[i]; if (!module) { s_KnownModules.Remove(i); continue; } if (module.DebugLevel > 0 || module.DebugDraw == 1) return true; } return false;') 'AnyDebugOverlayRequested must prune nulls and be true exactly when a known module has DebugLevel > 0 or DebugDraw == 1'
Assert ((Count $module '\bPrint(Format)?\s*\(') -eq 17) 'EAC_AmbientModule.c must keep its 17 log calls (G4 pin)'
$summary = Get-Block $module 'string\s+BuildDebugSummary\s*\(\s*\)'
Assert ([regex]::IsMatch($summary.Body, 'if\s*\(\s*DebugLevel\s*>=\s*3\s*\)\s*summary\s*\+=\s*"\\n"\s*\+\s*EAC_SceneIndex\.Describe\(\)\s*;\s*if\s*\(\s*DebugLevel\s*>=\s*3\s*\)\s*summary\s*\+=\s*" "\s*\+\s*EAC_SceneVocabulary\.Describe\(\)\s*;')) 'the vocabulary memo counters must be appended to the DebugLevel 3 scene line only'

# ---------------------------------------------------------------------------------------
# EAC_DebugView.c: request gate and blank early return (a-03)
# ---------------------------------------------------------------------------------------
$view = Read-Code 'EAC_DebugView.c'
$tick = Get-Block $view 'void\s+Tick\s*\(\s*SCR_EditorManagerEntity\s+editor\s*\)'
$tickText = Squash $tick.Body
$liveCheck = 'if (m_Expires > 0 && (!m_Module || (m_Drawing && m_Module.DebugDraw != 1) || (!m_Drawing && m_Module.DebugLevel <= 0))) Clear();'
$cadence = 'float now = GetGame().GetWorld().GetWorldTime() * 0.001; if (now >= m_NextRequest) { m_NextRequest = now + 1; if (EAC_AmbientModule.AnyDebugOverlayRequested()) player.EAC_RequestDebug(); } if (IsBlank()) return; if (m_Expires <= now) { Clear(); return; }'
Assert ($tickText.Contains($liveCheck + ' ' + $cadence)) 'Tick must keep the live switch check, then advance the 1 s cadence every second, request only while AnyDebugOverlayRequested, and return while blank right before the expiry Clear()'
Assert ((Count $view '\bEAC_RequestDebug\s*\(') -eq 2) 'EAC_RequestDebug must have one definition and one (gated) call'
$clear = Get-Block $view 'void\s+Clear\s*\(\s*\)'
$blank = Get-Block $view 'protected\s+bool\s+IsBlank\s*\(\s*\)'
Assert ((Squash $blank.Body) -ceq 'if (m_Expires != 0 || m_Summary || m_Ranges || m_Module || m_Drawing) return false; return m_Legend.IsEmpty() && m_Markers.IsEmpty() && m_Positions.IsEmpty() && m_ScreenPoints.IsEmpty();') 'IsBlank must test every field Clear() resets'
$cleared = @([regex]::Matches($clear.Body, '\b(m_\w+)\s*(?:=|\.Clear\s*\()') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
Assert ($cleared.Count -eq 9) "Clear() is expected to reset 9 fields (found $($cleared -join ', '))"
foreach ($field in $cleared) { Assert ([regex]::IsMatch($blank.Body, '\b' + $field + '\b')) "IsBlank must cover $field, which Clear() resets" }
# The client-side output rule the gate relies on (unchanged).
$receive = Get-Block $view 'void\s+Receive\s*\('
Assert ((Squash $receive.Body).StartsWith('Clear(); if (!module) return; if (EditingAttributes()) return; draw = draw && IsDrawingEnabled(module.DebugLevel, module.DebugDraw); if (module.DebugLevel <= 0) level = 0; if (level <= 0 && !draw) return;', [StringComparison]::Ordinal)) 'Receive must still draw only for a replica with DebugLevel > 0 or DebugDraw == 1'
$drawing = Get-Block $view 'static\s+bool\s+IsDrawingEnabled\s*\(\s*int\s+level\s*,\s*int\s+draw\s*\)'
Assert ((Squash $drawing.Body) -ceq 'return draw == 1;') 'IsDrawingEnabled must still be draw == 1'
# Model: whenever no known replica asks for output, any snapshot Receive could get draws nothing.
$skipped = 0
foreach ($replicaLevel in 0..3) { foreach ($replicaDraw in 0..1) { foreach ($serverLevel in 0..3) { foreach ($serverDraw in 0..1) {
 $requested = $replicaLevel -gt 0 -or $replicaDraw -eq 1
 if ($requested) { continue }
 $skipped++
 $draw = ($serverDraw -eq 1) -and ($replicaDraw -eq 1)
 $level = $serverLevel; if ($replicaLevel -le 0) { $level = 0 }
 Assert ($level -le 0 -and !$draw) "a skipped request must never hide output (replica $replicaLevel/$replicaDraw, server $serverLevel/$serverDraw)"
} } } }
Assert ($skipped -eq 8) 'the request-gate model must cover the replica-off cases'

# ---------------------------------------------------------------------------------------
# EAC_HomeIndex.c: int-keyed cell homes (a-07) and the ring-probe skip (a-06)
# ---------------------------------------------------------------------------------------
$index = Read-Code 'EAC_HomeIndex.c'
Assert ([regex]::IsMatch($index, 'protected\s+ref\s+map<int,\s*ref\s+array<int>>\s+m_CellHomes\s*=\s*new\s+map<int,\s*ref\s+array<int>>\s*\(\s*\)\s*;')) 'm_CellHomes must be map<int, ref array<int>>'
Assert (!(Count $index '\.ToString\(\)\s*\+\s*":"')) 'no "x:z" cell key strings may remain'
$neighbours = Get-Block $index 'protected\s+int\s+CountNeighbours\s*\('
Assert ([regex]::IsMatch($neighbours.Body, 'array<int>\s+ids\s*=\s*m_CellHomes\.Get\(\s*CellKey\(\s*cellX\s*,\s*cellZ\s*\)\s*\)\s*;')) 'CountNeighbours must look cells up by CellKey(cellX, cellZ)'
$remember = Get-Block $index 'protected\s+void\s+RememberCell\s*\('
Assert ([regex]::IsMatch($remember.Body, 'int\s+cellX\s*=\s*Math\.Floor\(\s*home\.Position\[0\]\s*/\s*CELL_SIZE\s*\)\s*;\s*int\s+cellZ\s*=\s*Math\.Floor\(\s*home\.Position\[2\]\s*/\s*CELL_SIZE\s*\)\s*;\s*int\s+key\s*=\s*CellKey\(\s*cellX\s*,\s*cellZ\s*\)\s*;')) 'RememberCell must key by CellKey of the same floored cell'
$cellKey = Get-Block $index 'static\s+int\s+CellKey\s*\(\s*int\s+x\s*,\s*int\s+z\s*\)'
Assert ((Squash $cellKey.Body) -ceq 'int cellX = Math.Clamp(x, -16384, 16383); int cellZ = Math.Clamp(z, -16384, 16383); return (cellX + 16384) * 32768 + (cellZ + 16384);') 'CellKey must be unchanged'

$cursor = Get-Block $index 'class\s+EAC_HomeScanCursor\s*(?=\{)'
Assert ([regex]::IsMatch($cursor.Body, '(?m)^\s*int\s+CleanProbes\s*;')) 'EAC_HomeScanCursor must carry int CleanProbes'
$discoverAt = Get-Block $index 'bool\s+DiscoverCellAt\s*\(\s*vector\s+position\s*,\s*int\s+small\s*,\s*int\s+large\s*\)'
$discoverAtText = Squash $discoverAt.Body
Assert ($discoverAtText.StartsWith('if (!Replication.IsServer() || !GetGame() || !EAC_AutoExclusions.IsReady() || small < 0 || small > 20 || large < 0 || large > 20) return false; m_Small = small; m_Large = large; if (!m_Pending.IsEmpty()) {', [StringComparison]::Ordinal)) 'DiscoverCellAt must keep its guard, then the pending drain'
Assert ($discoverAtText.Contains('int x = Math.Floor(position[0] / CELL_SIZE); int z = Math.Floor(position[2] / CELL_SIZE); int key = CellKey(x, z); if (m_Cells.Contains(key)) return false;')) 'DiscoverCellAt must keep its root-cell key test'
$indexed = Get-Block $index 'protected\s+bool\s+IsCellIndexed\s*\(\s*vector\s+position\s*\)'
Assert ((Squash $indexed.Body) -ceq 'int x = Math.Floor(position[0] / CELL_SIZE); int z = Math.Floor(position[2] / CELL_SIZE); return m_Cells.Contains(CellKey(x, z));') 'IsCellIndexed must compute the root-cell key exactly as DiscoverCellAt does'
# Exactness preconditions: m_Cells never loses a key except together with every cursor.
Assert (!(Count $index 'm_Cells\.Remove')) 'm_Cells must never lose a single key'
Assert ((Count $index 'm_Cells\.Clear\(\)') -eq 1 -and [regex]::IsMatch($index, 'm_Cells\.Clear\(\)\s*;\s*m_Focus\.Clear\(\)\s*;')) 'm_Cells may only be cleared together with every scan cursor (SetPopulationArea)'
$near = Get-Block $index 'bool\s+DiscoverNearPlayer\s*\('
$nearText = Squash $near.Body
Assert ($nearText.Contains('if (cursor.X != x || cursor.Z != z || cursor.Radius != radius) { cursor.X = x; cursor.Z = z; cursor.Radius = radius; cursor.Offset = 0; cursor.CleanProbes = 0; }')) 'a ring change must reset CleanProbes with Offset'
Assert ($nearText.Contains('int side = radius * 2 + 1; int total = side * side; if (DiscoverCellAt(position, small, large)) { cursor.CleanProbes = 0; return true; } cursor.ScanLead = !cursor.ScanLead;')) 'DiscoverCellAt(position) must still run first, before the lead scan and the ring skip'
Assert ((Count $near.Body 'return\s+true\s*;') -eq 3 -and (Count $near.Body 'cursor\.CleanProbes\s*=\s*0\s*;\s*return\s+true\s*;') -eq 3) 'every true return must reset CleanProbes'
Assert ($nearText.Contains('if (cursor.CleanProbes >= total) return false; for (int attempt = 0; attempt < 64; attempt++) { int offset = cursor.Offset; cursor.Offset = (cursor.Offset + 1) % total; vector delta = CellOffset(offset); vector cell = Vector((x + delta[0]) * CELL_SIZE, 0, (z + delta[2]) * CELL_SIZE); if (DiscoverCellAt(cell, small, large)) { cursor.CleanProbes = 0; return true; } if (IsCellIndexed(cell)) cursor.CleanProbes++; else cursor.CleanProbes = 0; } return false;')) 'the ring loop must be skipped only after total clean probes, and count only probes of already indexed cells'
Assert ($nearText.IndexOf('if (cursor.CleanProbes >= total) return false;') -gt $nearText.IndexOf('for (int leadAttempt = 0; leadAttempt < 9; leadAttempt++)')) 'the skip must come after the lead scan'

# Model of DiscoverNearPlayer, old and new, driven by the same random events (moves, readiness,
# the cell cap, prewarm inserts and pending children, cursor drops, area resets). Return values,
# the indexed cell set and the pending frontier must match after every event.
class EacCursor { [int]$X; [int]$Z; [int]$Radius; [int]$Offset; [int]$LeadX; [int]$LeadZ; [int]$LeadOffset; [bool]$ScanLead; [int]$CleanProbes }
class EacIndexModel {
 [bool]$New
 [Collections.Generic.HashSet[long]]$Cells = [Collections.Generic.HashSet[long]]::new()
 [Collections.Generic.Dictionary[int,EacCursor]]$Focus = [Collections.Generic.Dictionary[int,EacCursor]]::new()
 [Collections.Generic.List[int]]$Pending = [Collections.Generic.List[int]]::new()
 [int]$MaxCells; [bool]$Ready = $true; [long]$Probes; [long]$Skips
 EacIndexModel([bool]$new, [int]$maxCells) { $this.New = $new; $this.MaxCells = $maxCells }
 static [long] Key([int]$x, [int]$z) { return ([long][Math]::Clamp($x, -16384, 16383) + 16384) * 32768 + ([Math]::Clamp($z, -16384, 16383) + 16384) }
 static [int[]] Offset([int]$offset) {
  if ($offset -le 0) { return @(0, 0) }
  $ring = [int][Math]::Ceiling(([Math]::Sqrt($offset + 1) - 1) * 0.5)
  $side = $ring * 2; $inner = $side - 1; $edge = $offset - $inner * $inner
  if ($edge -lt $side) { return @((-$ring + $edge), (-$ring)) }
  if ($edge -lt $side * 2) { return @($ring, (-$ring + $edge - $side)) }
  if ($edge -lt $side * 3) { return @(($ring - $edge + $side * 2), $ring) }
  return @((-$ring), ($ring - $edge + $side * 3))
 }
 # Root query: every fifth key pushes four subdivision children (as an aborted dense cell would).
 [void] Query([long]$key) { if ($key % 5 -eq 0) { foreach ($child in 1..4) { $this.Pending.Add([int]($key % 1000) * 10 + $child) } } }
 [bool] DiscoverCellAt([int]$x, [int]$z) {
  $this.Probes++
  if (!$this.Ready) { return $false }
  if ($this.Pending.Count -gt 0) { $this.Pending.RemoveAt($this.Pending.Count - 1); return $true }
  $key = [EacIndexModel]::Key($x, $z)
  if ($this.Cells.Contains($key)) { return $false }
  if ($this.Cells.Count -ge $this.MaxCells) { return $false }
  [void]$this.Cells.Add($key); $this.Query($key)
  return $true
 }
 [bool] DiscoverNearPlayer([int]$player, [int]$x, [int]$z, [int]$radius, [bool]$lead, [int]$leadDx, [int]$leadDz) {
  $cursor = $null
  if (!$this.Focus.TryGetValue($player, [ref]$cursor)) { $cursor = [EacCursor]::new(); $this.Focus[$player] = $cursor }
  if ($cursor.X -ne $x -or $cursor.Z -ne $z -or $cursor.Radius -ne $radius) { $cursor.X = $x; $cursor.Z = $z; $cursor.Radius = $radius; $cursor.Offset = 0; $cursor.CleanProbes = 0 }
  $side = $radius * 2 + 1; $total = $side * $side
  if ($this.DiscoverCellAt($x, $z)) { $cursor.CleanProbes = 0; return $true }
  $cursor.ScanLead = !$cursor.ScanLead
  if ($cursor.ScanLead -and $lead) {
   $lx = $x + $leadDx; $lz = $z + $leadDz
   if ($cursor.LeadX -ne $lx -or $cursor.LeadZ -ne $lz) { $cursor.LeadX = $lx; $cursor.LeadZ = $lz; $cursor.LeadOffset = 0 }
   for ($a = 0; $a -lt 9; $a++) {
    $o = [EacIndexModel]::Offset($cursor.LeadOffset); $cursor.LeadOffset = ($cursor.LeadOffset + 1) % 9
    if ($this.DiscoverCellAt($lx + $o[0], $lz + $o[1])) { $cursor.CleanProbes = 0; return $true }
   }
  }
  if ($this.New -and $cursor.CleanProbes -ge $total) { $this.Skips++; return $false }
  for ($attempt = 0; $attempt -lt 64; $attempt++) {
   $offset = $cursor.Offset; $cursor.Offset = ($cursor.Offset + 1) % $total
   $d = [EacIndexModel]::Offset($offset); $cx = $x + $d[0]; $cz = $z + $d[1]
   if ($this.DiscoverCellAt($cx, $cz)) { $cursor.CleanProbes = 0; return $true }
   if ($this.Cells.Contains([EacIndexModel]::Key($cx, $cz))) { $cursor.CleanProbes++ } else { $cursor.CleanProbes = 0 }
  }
  return $false
 }
 [void] Prewarm([int]$x, [int]$z) {
  $key = [EacIndexModel]::Key($x, $z)
  if ($this.Cells.Contains($key) -or $this.Cells.Count -ge $this.MaxCells) { return }
  [void]$this.Cells.Add($key); $this.Query($key)
 }
 [void] SetPopulationArea() { $this.Cells.Clear(); $this.Focus.Clear(); $this.Pending.Clear() }
}
$calls = 0; $skips = 0; $saved = 0
foreach ($run in @(@{ Seed = 11; Max = 100000 }, @{ Seed = 23; Max = 60 }, @{ Seed = 37; Max = 100000 }, @{ Seed = 41; Max = 20 })) {
 $random = [Random]::new($run.Seed)
 $old = [EacIndexModel]::new($false, $run.Max); $new = [EacIndexModel]::new($true, $run.Max)
 $players = @{ 1 = @(0, 0, 1); 2 = @(3, -2, 2); 3 = @(-4, 5, 3) }
 for ($step = 0; $step -lt 700; $step++) {
  $roll = $random.Next(100)
  if ($roll -lt 80) {
   $player = 1 + $random.Next(3); $at = $players[$player]
   $lead = $random.Next(5) -eq 0; $leadDx = $random.Next(-3, 4); $leadDz = $random.Next(-3, 4)
   $a = $old.DiscoverNearPlayer($player, $at[0], $at[1], $at[2], $lead, $leadDx, $leadDz)
   $b = $new.DiscoverNearPlayer($player, $at[0], $at[1], $at[2], $lead, $leadDx, $leadDz)
   $calls++
   Assert ($a -eq $b) "DiscoverNearPlayer model diverged at run $($run.Seed) step $step (old $a, new $b)"
  } elseif ($roll -lt 85) {
   $player = 1 + $random.Next(3); $at = $players[$player]
   $players[$player] = @(($at[0] + $random.Next(-1, 2)), ($at[1] + $random.Next(-1, 2)), [Math]::Clamp($at[2] + $random.Next(-1, 2), 1, 3))
  } elseif ($roll -lt 89) {
   $ready = $random.Next(3) -ne 0; $old.Ready = $ready; $new.Ready = $ready
  } elseif ($roll -lt 95) {
   $px = $random.Next(-8, 9); $pz = $random.Next(-8, 9); $old.Prewarm($px, $pz); $new.Prewarm($px, $pz)
  } elseif ($roll -lt 98) {
   $player = 1 + $random.Next(3); [void]$old.Focus.Remove($player); [void]$new.Focus.Remove($player)
  } elseif ($roll -lt 99) {
   $old.SetPopulationArea(); $new.SetPopulationArea()
  }
  Assert ($old.Cells.SetEquals($new.Cells)) "indexed cells diverged at run $($run.Seed) step $step"
  Assert ([string]::Join(',', $old.Pending) -eq [string]::Join(',', $new.Pending)) "pending frontier diverged at run $($run.Seed) step $step"
 }
 $skips += $new.Skips; $saved += $old.Probes - $new.Probes
}
Assert ($skips -gt 40 -and $saved -gt 0) "the model must exercise the skip (skips $skips, probes saved $saved)"
# Targeted boundary: a 3 x 3 ring with one unindexed cell at offset $hole, probed 64 times while
# DiscoverCellAt's guard refuses. The run of indexed probes at the end of that call is up to
# total - 1 long; once the guard passes, both versions must still find and index the hole.
for ($hole = 1; $hole -lt 9; $hole++) {
 $old = [EacIndexModel]::new($false, 100000); $new = [EacIndexModel]::new($true, 100000)
 for ($offset = 0; $offset -lt 9; $offset++) { if ($offset -ne $hole) { $d = [EacIndexModel]::Offset($offset); $old.Prewarm($d[0], $d[1]); $new.Prewarm($d[0], $d[1]) } }
 $old.Pending.Clear(); $new.Pending.Clear()
 $old.Ready = $false; $new.Ready = $false
 Assert (!$old.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0) -and !$new.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0)) "boundary hole $hole`: a refused call must return false"
 $old.Ready = $true; $new.Ready = $true
 $a = $old.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0); $b = $new.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0)
 Assert ($a -and $b -and $old.Cells.SetEquals($new.Cells)) "boundary hole $hole`: the unindexed cell must still be found (old $a, new $b)"
 $old.Pending.Clear(); $new.Pending.Clear()
 $a = $old.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0); $b = $new.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0)
 Assert (!$a -and !$b) "boundary hole $hole`: a fully indexed ring must return false"
 $a = $old.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0); $b = $new.DiscoverNearPlayer(1, 0, 0, 1, $false, 0, 0)
 Assert (!$a -and !$b -and $new.Skips -ge 1) "boundary hole $hole`: the next call on a fully indexed ring must take the skip"
}

# ---------------------------------------------------------------------------------------
# EAC_SceneVocabulary.c: memo bound (b-03)
# ---------------------------------------------------------------------------------------
$vocabulary = Read-Code 'EAC_SceneVocabulary.c'
Assert ([regex]::IsMatch($vocabulary, 'static\s+const\s+int\s+MAX_MEMO\s*=\s*4096\s*;')) 'MAX_MEMO must be 4096'
Assert ([regex]::IsMatch($vocabulary, '(?m)^\s*protected\s+static\s+int\s+s_Unmemoised\s*;')) 's_Unmemoised must be a plain static int'
$classify = Get-Block $vocabulary 'static\s+int\s+Classify\s*\(\s*ResourceName\s+resource\s*\)'
Assert ((Squash $classify.Body) -ceq 'EXPBG_LazyStatics_EAC_SceneVocabulary(); CheckWorld(); string path = resource; if (path == "") return 0; int cached; if (s_Memo.Find(path, cached)) return cached; int packed = Resolve(path); if (s_Memo.Count() < MAX_MEMO) s_Memo.Insert(path, packed); else if (s_Unmemoised < EAC_Diagnostics.COUNTER_LIMIT) s_Unmemoised++; return packed;') 'Classify must memoise up to MAX_MEMO and only count the Resolve calls it could not memoise'
$check = Get-Block $vocabulary 'static\s+void\s+CheckWorld\s*\(\s*\)'
Assert ([regex]::IsMatch($check.Body, 's_Memo\.Clear\(\)\s*;\s*s_World\s*=\s*world\s*;\s*s_Unmemoised\s*=\s*0\s*;')) 'a new world must reset the memo and its counter together'
$describe = Get-Block $vocabulary 'static\s+string\s+Describe\s*\(\s*\)'
Assert ($describe.Body.Contains('"scene_vocabulary memo="') -and $describe.Body.Contains('" unmemoised="')) 'Describe must report the memo fill and the unmemoised count'
Assert (!(Count $vocabulary '\bPrint(Format)?\s*\(')) 'EAC_SceneVocabulary.c must stay free of log calls (G4)'

# ---------------------------------------------------------------------------------------
# EAC_ResidentClaims.c: ReservesGroup (b-11) and EAC_SessionGroupExclusive (b-04)
# ---------------------------------------------------------------------------------------
$claims = Read-Code 'EAC_ResidentClaims.c'
$reserves = Get-Block $claims 'bool\s+ReservesGroup\s*\(\s*SCR_AIGroup\s+group\s*,\s*EAC_ResidentClaim\s+except\s*=\s*null\s*\)'
Assert ((Squash $reserves.Body) -ceq 'if (!group || !Replication.IsServer() || group.GetWorld() != m_World) return false; foreach (int groupId, EAC_ResidentClaim groupClaim : m_Claims) { if (groupClaim != except && groupClaim.Group == group) return true; } foreach (int id, EAC_ResidentClaim claim : m_Claims) { if (claim == except) continue; if (claim.Group == group) return true; SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(claim.Character); if (!actor && claim.OptimizerMember) actor = claim.OptimizerMember.Entity; if (actor && actor.GetCharacterGroup() == group) return true; } return false;') 'ReservesGroup must run the pointer pass over claim.Group first and keep the original loop unchanged'
$exclusive = Get-Block $claims 'protected\s+bool\s+EAC_SessionGroupExclusive\s*\('
Assert ((Squash $exclusive.Body) -ceq 'if (claim.Group.GetAgentsCount() > 1 || claim.Group.GetPlayerCount() > 0 || claim.Group.GetChildren()) return false; EAC_PedestrianSpawner spawner = EAC_AmbientModule.EAC_GetSessionSpawner(); for (int i = 0; i < editable.GetChildrenCount(true); i++) { SCR_EditableEntityComponent child = editable.GetChild(i); if (!child) continue; if (child.GetOwner() == claim.Character || (claim.OptimizerMember && child.GetOwner() == claim.OptimizerMember.Entity)) continue; if (spawner && spawner.EAC_ContainsSessionHelper(child, claim)) continue; return false; } return true;') 'EAC_SessionGroupExclusive must test the claim actor before the helper walk and otherwise be unchanged'
# Exactness precondition: the helper walk only compares owners (no side effects).
$spawnerText = Read-Code 'EAC_PedestrianSpawner.c'
$helper = Get-Block $spawnerText 'bool\s+EAC_ContainsSessionHelper\s*\(\s*SCR_EditableEntityComponent\s+candidate\s*,\s*EAC_ResidentClaim\s+ownerClaim\s*=\s*null\s*\)'
Assert (!(Count $helper.Body '(?<![=!<>])=(?!=)|\+\+|--|\.(Insert|Remove|Clear|Set)\w*\(')) 'EAC_ContainsSessionHelper must stay a read-only walk'

# Models: the reordered tests give the same boolean for every combination.
$random = [Random]::new(5)
for ($trial = 0; $trial -lt 3000; $trial++) {
 # ReservesGroup: each claim has a group id (0 = none) and an actor group id (0 = no actor).
 $count = $random.Next(0, 7); $group = 1 + $random.Next(4)
 $list = @(for ($c = 0; $c -lt $count; $c++) { @{ Group = $random.Next(5); ActorGroup = $random.Next(5) } })
 $except = if ($count -gt 0 -and $random.Next(2) -eq 0) { $list[$random.Next($count)] } else { $null }
 $single = $false
 foreach ($claim in $list) { if ([object]::ReferenceEquals($claim, $except)) { continue }; if ($claim.Group -eq $group -or ($claim.ActorGroup -ne 0 -and $claim.ActorGroup -eq $group)) { $single = $true; break } }
 $two = $false
 foreach ($claim in $list) { if (![object]::ReferenceEquals($claim, $except) -and $claim.Group -eq $group) { $two = $true; break } }
 if (!$two) { foreach ($claim in $list) { if ([object]::ReferenceEquals($claim, $except)) { continue }; if ($claim.Group -eq $group -or ($claim.ActorGroup -ne 0 -and $claim.ActorGroup -eq $group)) { $two = $true; break } } }
 Assert ($single -eq $two) "ReservesGroup model diverged on trial $trial"
 # EAC_SessionGroupExclusive: each child is null, the character, the optimizer entity, a helper or foreign.
 $children = @(for ($c = 0; $c -lt $random.Next(0, 4); $c++) { @('null', 'character', 'optimizer', 'helper', 'foreign')[$random.Next(5)] })
 $hasOptimizer = $random.Next(2) -eq 0; $hasSpawner = $random.Next(4) -ne 0
 $before = $true; $after = $true
 foreach ($child in $children) {
  if ($child -eq 'null') { continue }
  $isHelper = $hasSpawner -and $child -eq 'helper'
  $isActor = $child -eq 'character' -or ($hasOptimizer -and $child -eq 'optimizer')
  if ($isHelper) { continue }; if (!$isActor) { $before = $false; break }
 }
 foreach ($child in $children) {
  if ($child -eq 'null') { continue }
  $isHelper = $hasSpawner -and $child -eq 'helper'
  $isActor = $child -eq 'character' -or ($hasOptimizer -and $child -eq 'optimizer')
  if ($isActor) { continue }; if ($isHelper) { continue }; $after = $false; break
 }
 Assert ($before -eq $after) "EAC_SessionGroupExclusive model diverged on trial $trial"
}

"PASS: civilian module frame mask cleared on clients, debug snapshots requested only while a module asks for output ($skipped gated cases), ring probes skipped once indexed ($calls model calls, $skips skips, $saved probes saved, identical results), int-keyed cell homes, a 4096-path vocabulary memo, and reordered claim checks with identical answers. Native engines were not launched."
