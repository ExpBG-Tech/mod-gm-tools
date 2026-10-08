#requires -Version 7.0
# Portable guard for several Ambient Civilians modules in one mission (0.1.18).
# Production report on 0.1.17 (2026-10-08): "I placed a second module and it's not spawning any
# civs". The server log said "[EAC] a second Ambient Civilians module is placed; only the first
# placed module admits residents": EOnInit marked every further module a duplicate, all
# admission entry points refused it (GetActive() == this), and the population area, the house
# index and the discovery ring were the first module's disc only.
# Now every placed module is a population area with its own disc, civilian limit, neighbourhood
# limits, residents per house and theme; the first placed module coordinates the one shared
# scheduler. This test pins that wiring and runs a model of the old and new admission rules.
# Source checks plus a model only. No engine is launched. The native two-module fixture is
# tests/EAC_MultiModuleGameplay.c (wired here, not run).
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
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::new($Signature).Match($Text)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index + $match.Length); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced block for $Signature"
}
function Squash([string]$Text) { [regex]::Replace($Text, '\s+', ' ').Trim() }
function Count([string]$Text, [string]$Pattern) { [regex]::Matches($Text, $Pattern).Count }
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = [regex]::Match($Text, $First); $b = [regex]::Match($Text, $Second)
 return $a.Success -and $b.Success -and $a.Index -lt $b.Index
}

# ---------------------------------------------------------------------------------------
# EAC_AmbientModule.c: no duplicate refusal; areas, union, accounting, fair split, notices
# ---------------------------------------------------------------------------------------
$module = Read-Code 'EAC_AmbientModule.c'
Assert (!$module.Contains('only the first placed module admits residents')) 'the 0.1.17 duplicate refusal notice must be gone'
Assert (!(Count $module '\bm_Duplicate\b|\bMarkDuplicate\b|\bIsDuplicated\b|inactive duplicate module')) 'no module may be marked or reported as an inactive duplicate'
$init = Squash (Get-Body $module 'override\s+void\s+EOnInit\s*\(\s*IEntity\s+owner\s*\)')
$afterInsert = $init.Substring($init.IndexOf('s_Modules.Insert(this);'))
Assert ($afterInsert.Contains('s_NextAreaId++; m_AreaId = s_NextAreaId;')) 'every server module gets its own area id when it registers'
Assert ($afterInsert.Contains('if (coordinator && coordinator != this) { coordinator.m_ConfigDirty = true; ReportArea("added:')) 'a further module is reported as an added area, not refused'
Assert (!(Count $afterInsert '\breturn\b')) 'EOnInit must not stop a further module after it registers'

$find = Squash (Get-Body $module 'static\s+EAC_AmbientModule\s+FindPopulationArea\s*\(\s*vector\s+position\s*\)')
Assert ($find.StartsWith('EXPBG_LazyStatics_EAC_AmbientModule(); if (!Replication.IsServer() || !GetGame()) return null;', [StringComparison]::Ordinal)) 'FindPopulationArea is server-only and lazy-static safe'
Assert ($find.Contains('foreach (EAC_AmbientModule module : s_Modules)') -and $find.Contains('if (served >= MAX_POPULATION_AREAS) break;')) 'FindPopulationArea walks the placed modules, bounded by MAX_POPULATION_AREAS'
Assert ($find.Contains('if (radius <= 0 || distanceSq > radius * radius) continue; if (!found || distanceSq < bestDistance)')) 'FindPopulationArea picks the nearest centre among the discs that hold the position'
Assert ([regex]::IsMatch($module, 'static\s+const\s+int\s+MAX_POPULATION_AREAS\s*=\s*32\s*;') -and [regex]::IsMatch($module, 'static\s+const\s+int\s+WORLD_POPULATION_CEILING\s*=\s*200\s*;')) 'the area count and mission ceiling constants are pinned'
$contains = Squash (Get-Body $module 'bool\s+ContainsPopulationPosition\s*\(\s*vector\s+position\s*\)')
Assert ($contains -ceq 'if (FindPopulationArea(position)) return true; return ContainsOwnArea(position);') 'the coordinator area test is the union of every module disc'
$outside = Squash (Get-Body $module 'static\s+bool\s+IsOutsidePopulationArea\s*\(\s*vector\s+position\s*\)')
Assert ($outside -ceq 'EAC_AmbientModule module = GetActive(); return module && !module.ContainsPopulationPosition(position);') 'IsOutsidePopulationArea keeps its controller-gap rule and asks the union'

$frame = Squash (Get-Body $module 'override\s+void\s+EOnFrame\s*\(\s*IEntity\s+owner\s*,\s*float\s+timeSlice\s*\)')
Assert ($frame.StartsWith('if (GetActive() != this || !s_HomeIndex) return;', [StringComparison]::Ordinal)) 'the coordinator alone runs the shared scheduler'
Assert ($frame.Contains('RefreshPopulationAreas(); bool themeReady = PrepareTheme();')) 'the coordinator refreshes every area before its own theme each tick'
Assert ($frame.Contains('s_HomeIndex.SetPopulationAreas(m_AreaCentres, m_AreaRadii);') -and !$frame.Contains('SetPopulationArea(GetOrigin()')) 'the house index covers every module disc, not the coordinator disc only'
Assert ($frame.Contains('if (ChimeraCharacter.Cast(character)) discovered = DiscoverAreasNear(character.GetOrigin());')) 'house discovery serves every area near the picked player'
Assert (!$frame.Contains('DiscoverNearPlayer(playerId')) 'discovery cursors are keyed by area, not by player'

$discover = Squash (Get-Body $module 'protected\s+bool\s+DiscoverAreasNear\s*\(\s*vector\s+position\s*\)')
Assert ($discover.Contains('m_NextArea = m_NextArea % areaCount; EAC_AmbientModule areaModule = m_AreaModules[m_NextArea]; m_NextArea++;')) 'areas are visited in rotation so no town starves another'
Assert ($discover.Contains('if (vector.DistanceXZ(position, centre) > radius + WakeDistance) continue;')) 'an area is indexed only while a player is within its wake reach'
Assert ($discover.Contains('int focusKey = EAC_HomeIndex.AREA_FOCUS_BASE + areaModule.m_AreaId; if (s_HomeIndex.DiscoverNearPlayer(focusKey, centre, radius, areaModule.SmallHouseResidents, areaModule.LargeHouseResidents)) return true;')) 'each area keeps its own scan cursor and its own residents per house'

$refresh = Squash (Get-Body $module 'void\s+RefreshPopulationAreas\s*\(\s*\)')
Assert ($refresh.Contains('if (m_AreaModules.Count() >= MAX_POPULATION_AREAS) { module.SetAreaState(0, true, true, true); continue; }')) 'a module beyond the served count is marked ignored (reported), never silently dropped'
Assert ($refresh.Contains('module.m_AreaPopulation = 0; requested += module.PopulationLimit;')) 'each area is recounted every tick and contributes its own limit'
Assert ($refresh.Contains('m_WorldPopulationLimit = Math.Min(requested, WORLD_POPULATION_CEILING); if (s_Claims && s_IndexWorld == currentWorld) s_Claims.ChargePopulationAreas();')) 'the mission limit is the capped sum and every claim is charged to its area'
Assert ($refresh.Contains('if (capped && requested > 0) limit = areaModule.PopulationLimit * WORLD_POPULATION_CEILING / requested;')) 'the 200 ceiling is split in proportion to each module limit'
Assert ($refresh.Contains('if (areaModule != this) { areaModule.EAC_FinishSessionSettingsLoad(); areaModule.PrepareTheme(); }')) 'every further area restores its saved settings and prepares its own theme on the coordinator tick'
$state = Squash (Get-Body $module 'void\s+SetAreaState\s*\(')
Assert ($state.Contains('if (capped == m_AreaCapped && ignored == m_AreaIgnored) return;') -and (Count $state 'ReportArea\(') -eq 3) 'capped/ignored/restored notices fire on a state change only'
Assert ((Squash (Get-Body $module 'int\s+GetAreaPopulationLimit\s*\(\s*\)')) -ceq 'if (m_AreaIgnored) return 0; if (!m_AreaShared) return PopulationLimit; return m_AreaLimit;') 'a single module keeps its own live PopulationLimit'
Assert ((Squash (Get-Body $module 'bool\s+HasAreaCapacity\s*\(\s*\)')) -ceq 'if (EAC_SessionSettingsBlocked()) return false; return m_AreaPopulation < GetAreaPopulationLimit();') 'an area admits while under its own limit'
Assert ((Squash (Get-Body $module 'int\s+GetWorldPopulationLimit\s*\(\s*\)')) -ceq 'if (!m_AreaShared) return PopulationLimit; return m_WorldPopulationLimit;') 'the mission limit equals the single module limit until a second module exists'
Assert ($module.Contains('return s_Budget.TryReserve(partyId, residents, GetWorldPopulationLimit());') -and $module.Contains('return s_Claims.Begin(home, resident, GetWorldPopulationLimit());')) 'the shared budget is charged against the mission limit'
$report = Squash (Get-Body $module 'protected\s+void\s+ReportArea\s*\(\s*string\s+state\s*\)')
Assert ($report.Contains('PrintFormat("[EAC area] %1 area=%2 position=%3 radius=%4 civilian_limit=%5/%6 modules=%7", state, areaId, origin, radius, limit, wanted, areas);')) 'one area notice line with hoisted locals'
Assert ((Count $module '\bPrint(Format)?\s*\(') -eq 17) 'EAC_AmbientModule.c keeps its 17 log calls (G4 pin): the area notice replaced the duplicate notice'
$summary = Squash (Get-Body $module 'string\s+BuildDebugSummary\s*\(\s*\)')
Assert ($summary.Contains('summary += " areas=" + areaCount.ToString();') -and !$summary.Contains('duplicate=')) 'the console summary reports areas instead of duplicate='
$overlay = Squash (Get-Body $module 'string\s+BuildOverlayStats\s*\(\s*\)')
Assert ($overlay.Contains('if (GetActive() != this) return DescribeArea();') -and $overlay.Contains('if (areaModule) stats += "\n" + areaModule.DescribeArea();')) 'the GM overlay shows every area, capped or ignored'

# ---------------------------------------------------------------------------------------
# Spawner, claims, index and diagnostics
# ---------------------------------------------------------------------------------------
$spawner = Read-Code 'EAC_PedestrianSpawner.c'
Assert (!(Count $spawner 'module\.PopulationLimit')) 'the spawner reads the mission and area limits, never the coordinator PopulationLimit alone'
$begin = Squash (Get-Body $spawner 'bool\s+Begin\s*\(\s*EAC_AmbientModule\s+module\s*,')
Assert ($begin.Contains('EAC_AmbientModule areaModule = EAC_AmbientModule.FindPopulationArea(home.BuildingEntity.GetOrigin()); if (!areaModule) return Reject(EAC_ESpawnReason.ISOLATED_HOME, position);')) 'Begin resolves the module that owns the household'
Assert ($begin.Contains('ResourceName freshPrefab = areaModule.PickAreaCharacter(); if (freshPrefab == "") freshPrefab = module.GetHomeIndex().GetRegistry().PickCharacter(resident.CharacterPrefab);')) 'a resident wears its own module theme/faction'
Assert ($begin.Contains('if (module.GetReservedPopulation() >= module.GetWorldPopulationLimit()) return Reject(EAC_ESpawnReason.BUDGET, position); if (!areaModule.HasAreaCapacity()) return Reject(EAC_ESpawnReason.AREA_BUDGET, position);')) 'Begin checks the mission limit, then the area limit'
Assert ($begin.Contains('if (!HasLocalCapacity(areaModule, position)) return Reject(EAC_ESpawnReason.LOCAL_BUDGET, position);')) 'the neighbourhood ceiling is the area module own'
Assert ($begin.Contains('if (!claim) return Reject(EAC_ESpawnReason.RESIDENT_INELIGIBLE, position); areaModule.ChargeAreaResident();')) 'an admission is charged to its area at once'
$select = Squash (Get-Body $spawner 'void\s+SelectNext\s*\(\s*EAC_AmbientModule\s+module\s*,\s*array<IEntity>\s+observers\s*\)')
Assert ($select.Contains('if (module.GetReservedPopulation() >= module.GetWorldPopulationLimit()) { m_Diagnostics.Record(EAC_ESpawnReason.BUDGET); return; }')) 'only the mission limit ends a selection tick'
Assert ($select.Contains('EAC_AmbientModule areaModule = EAC_AmbientModule.FindPopulationArea(neighbourhood);') -and $select.Contains('if (!areaModule.HasAreaCapacity()) { m_Diagnostics.Record(EAC_ESpawnReason.AREA_BUDGET); m_SlotCursor = 0; m_HomeCursor++; continue; }')) 'a full area skips its own households and selection continues with the other areas'
Assert (Before $select 'areaModule\.HasAreaCapacity\(\)' 'int\s+localStop') 'the area limit is checked before the neighbourhood count'

$claims = Read-Code 'EAC_ResidentClaims.c'
$charge = Squash (Get-Body $claims 'void\s+ChargePopulationAreas\s*\(\s*\)')
Assert ($charge -ceq 'foreach (int id, EAC_ResidentClaim claim : m_Claims) { if (!claim || !claim.Home) continue; EAC_AmbientModule areaModule = EAC_AmbientModule.FindPopulationArea(claim.Home.Position); if (areaModule) areaModule.ChargeAreaResident(); }') 'every claim (live, pending or cached) is charged to the area holding its home'

$index = Read-Code 'EAC_HomeIndex.c'
Assert (!(Count $index '\bSetPopulationArea\s*\(|m_AreaCentre\b|m_AreaRadius\b')) 'the single-disc index area is gone'
$setAreas = Squash (Get-Body $index 'void\s+SetPopulationAreas\s*\(\s*array<vector>\s+centres\s*,\s*array<int>\s+radii\s*\)')
Assert ($setAreas.Contains('if (SameAreas(centres, radii)) return;') -and $setAreas.EndsWith('m_Cells.Clear(); m_Focus.Clear(); m_Pending.Clear();', [StringComparison]::Ordinal)) 'an unchanged area set clears nothing; any change re-opens every skipped cell'
Assert ((Squash (Get-Body $index 'protected\s+void\s+QueryCell\s*\(\s*vector\s+mins\s*,\s*int\s+size\s*\)')).StartsWith('if (!CellTouchesArea(mins, size)) return;', [StringComparison]::Ordinal)) 'a cell is queried when it touches any module disc'
$touch = Squash (Get-Body $index 'protected\s+bool\s+CellTouchesArea\s*\(')
Assert ($touch.StartsWith('if (m_AreaCentres.IsEmpty()) return true;', [StringComparison]::Ordinal) -and $touch.Contains('if (EAC_AmbientModule.ContainsPopulationPoint(closestX, closestZ, centre[0], centre[2], radius)) return true;')) 'the cell test is the old closest-point test, over every disc'
$retain = Squash (Get-Body $index 'void\s+RetainPlayers\s*\(\s*array<int>\s+players\s*\)')
Assert ($retain.Contains('if (focusKey >= AREA_FOCUS_BASE) continue;')) 'area scan cursors survive the player prune'
Assert ([regex]::IsMatch($index, 'static\s+const\s+int\s+AREA_FOCUS_BASE\s*=\s*1048576\s*;')) 'area cursor keys never collide with player ids'
$reconcile = Squash (Get-Body $index 'void\s+Reconcile\s*\(\s*int\s+small\s*,\s*int\s+large\s*,\s*bool\s+allowGrowth\s*=\s*true\s*\)')
Assert ($reconcile.Contains('EAC_AmbientModule areaModule = EAC_AmbientModule.FindPopulationArea(home.Position); if (areaModule) { occupancy = areaModule.SmallHouseResidents; if (home.Large) occupancy = areaModule.LargeHouseResidents; }')) 'a household takes its own module residents per house'

$diagnostics = Read-Code 'EAC_Diagnostics.c'
Assert ([regex]::IsMatch($diagnostics, 'RUINED_HOME\s*,\s*AREA_BUDGET\s*,\s*COUNT\s*\}')) 'AREA_BUDGET is appended immediately before COUNT so no recorded ordinal moves'
Assert ($diagnostics.Contains('case EAC_ESpawnReason.AREA_BUDGET: return "area_budget";')) 'the area refusal has a named counter'

# Enforce gotchas on every function this change added: value returns on their own line, no
# Math.RandomFloat, no reserved names (owned/Sleep/Wait), no implicit bool->int.
function Assert-Enforce([string]$Body, [string]$Where) {
 foreach ($line in $Body -split "`n") {
  Assert (!($line -match '\breturn\s+[^;\s]' -and $line -notmatch '^\s*return\b')) "$Where must keep every value return on its own line: $($line.Trim())"
 }
 Assert (![regex]::IsMatch($Body, 'Math\.RandomFloat\b')) "$Where must not use Math.RandomFloat"
 Assert (![regex]::IsMatch($Body, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait)\b')) "$Where uses a reserved Enforce name"
 Assert (![regex]::IsMatch($Body, '\bint\s+\w+\s*=\s*(m_Area(Capped|Ignored|Shared)|capped|ignored|areasShared|areaCapped)\s*;')) "$Where must not convert a bool to an int implicitly"
}
foreach ($signature in 'bool\s+ContainsPopulationPosition\s*\(', 'bool\s+ContainsOwnArea\s*\(', 'static\s+EAC_AmbientModule\s+FindPopulationArea\s*\(', 'void\s+RefreshPopulationAreas\s*\(', 'void\s+SetAreaState\s*\(', 'void\s+ChargeAreaResident\s*\(', 'int\s+GetAreaPopulation\s*\(', 'int\s+GetAreaId\s*\(', 'bool\s+IsAreaCapped\s*\(', 'bool\s+IsAreaIgnored\s*\(', 'int\s+GetAreaPopulationLimit\s*\(', 'bool\s+HasAreaCapacity\s*\(', 'int\s+GetWorldPopulationLimit\s*\(', 'ResourceName\s+PickAreaCharacter\s*\(', 'protected\s+bool\s+DiscoverAreasNear\s*\(', 'protected\s+void\s+ReportArea\s*\(', 'string\s+DescribeArea\s*\(') {
 Assert-Enforce (Get-Body $module $signature) "EAC_AmbientModule $signature"
}
foreach ($signature in 'void\s+SetPopulationAreas\s*\(', 'int\s+GetPopulationAreaCount\s*\(', 'protected\s+bool\s+SameAreas\s*\(', 'protected\s+bool\s+CellTouchesArea\s*\(', 'void\s+RetainPlayers\s*\(') {
 Assert-Enforce (Get-Body $index $signature) "EAC_HomeIndex $signature"
}
Assert-Enforce $charge 'EAC_ResidentClaims.ChargePopulationAreas'
$beginRaw = Get-Body $spawner 'bool\s+Begin\s*\(\s*EAC_AmbientModule\s+module\s*,'
foreach ($line in $beginRaw -split "`n") {
 if ($line -match 'areaModule|GetWorldPopulationLimit') { Assert (!($line -match '\breturn\s+[^;\s]' -and $line -notmatch '^\s*return\b')) "Begin's changed gates keep their returns on their own lines: $($line.Trim())" }
}
# Names that shadow vanilla types are avoided: locals and fields of this change are prefixed.
foreach ($bad in 'EAC_AmbientModule\s+area\b', 'EAC_ThemeSelection\s+selection\b', 'EAC_WeightedPool\s+pool\b', '\bbool\s+shared\b') {
 Assert (![regex]::IsMatch($module + $spawner + $index + $claims, $bad)) "no local named like a vanilla type or keyword: $bad"
}

# Every former coordinator-only gate stays a coordinator gate (the scheduler is shared).
foreach ($file in 'EAC_CivilianActivity.c', 'EAC_PedestrianWalk.c', 'EAC_CivilianShelter.c', 'EAC_RoutineEmerge.c') {
 Assert ((Read-Code $file).Contains('EAC_AmbientModule.GetActive()')) "$file keeps its coordinator guard"
}

# ---------------------------------------------------------------------------------------
# Model: old (0.1.17) and new (0.1.18) admission across several modules
# ---------------------------------------------------------------------------------------
$Ceiling = 200; $MaxAreas = 32
function Get-Shares([int[]]$Limits) {
 $served = @($Limits | Select-Object -First $MaxAreas)
 $requested = ($served | Measure-Object -Sum).Sum
 if ($null -eq $requested) { $requested = 0 }
 $capped = $requested -gt $Ceiling
 $shares = foreach ($limit in $served) { if ($capped -and $requested -gt 0) { [Math]::Floor($limit * $Ceiling / $requested) } else { $limit } }
 [pscustomobject]@{ Shares = @($shares); World = [Math]::Min($requested, $Ceiling); Capped = $capped; Shared = $served.Count -gt 1; Ignored = [Math]::Max(0, $Limits.Count - $MaxAreas) }
}
$random = [Random]::new(20261009)
for ($run = 0; $run -lt 4000; $run++) {
 $count = $random.Next(1, 41)
 $limits = [int[]]@(for ($i = 0; $i -lt $count; $i++) { $random.Next(0, 201) })
 $model = Get-Shares $limits
 $sum = ($model.Shares | Measure-Object -Sum).Sum
 Assert ($sum -le $model.World) "run $run`: shares $sum exceed the mission limit $($model.World)"
 Assert ($model.World -le $Ceiling) "run $run`: mission limit above the ceiling"
 for ($i = 0; $i -lt $model.Shares.Count; $i++) {
  Assert ($model.Shares[$i] -le $limits[$i]) "run $run`: area $i share above its own limit"
  if (!$model.Capped) { Assert ($model.Shares[$i] -eq $limits[$i]) "run $run`: an uncapped area must keep its own limit" }
 }
 if ($count -eq 1) { Assert ($model.World -eq $limits[0] -and $model.Shares[0] -eq $limits[0] -and !$model.Shared) "run $run`: a single module must behave exactly as before" }
 Assert ($model.Ignored -eq [Math]::Max(0, $count - $MaxAreas)) "run $run`: modules beyond $MaxAreas are reported as ignored"
}
$split = Get-Shares @(150, 150)
Assert ($split.Capped -and $split.Shares[0] -eq 100 -and $split.Shares[1] -eq 100 -and $split.World -eq 200) 'two modules at 150 split the ceiling 100/100'
$restored = Get-Shares @(50, 150)
Assert (!$restored.Capped -and $restored.Shares[0] -eq 50 -and $restored.Shares[1] -eq 150) 'lowering one module restores both own limits'

# Admission over ticks: towns A and B (limits 24 each). Players stand in town A for 60 ticks,
# then everybody walks to town B. 0.1.17 refuses B outright; 0.1.18 fills B up to its own
# limit while A keeps what it has (a full A never holds B back).
function Invoke-Admission([bool]$New) {
 $limits = @(24, 24); $population = @(0, 0); $reasons = @{}
 for ($tick = 0; $tick -lt 200; $tick++) {
  $town = if ($tick -lt 60) { 0 } else { 1 }
  $world = if ($New) { (Get-Shares $limits).World } else { $limits[0] }
  # 0.1.17: town B is outside the only (first) module's disc, and the duplicate module admits nothing.
  if (!$New -and $town -ne 0) { $reasons['outside_area']++; continue }
  if (($population[0] + $population[1]) -ge $world) { $reasons['budget']++; continue }
  $share = if ($New) { (Get-Shares $limits).Shares[$town] } else { $limits[0] }
  if ($population[$town] -ge $share) { $reasons['area_budget']++; continue }
  $population[$town]++
 }
 [pscustomobject]@{ A = $population[0]; B = $population[1]; Reasons = $reasons }
}
$old = Invoke-Admission $false
$new = Invoke-Admission $true
Assert ($old.A -eq 24 -and $old.B -eq 0 -and $old.Reasons['outside_area'] -gt 0) "model of 0.1.17 must reproduce the report: second town empty (A=$($old.A) B=$($old.B))"
Assert ($new.A -eq 24 -and $new.B -eq 24) "0.1.18 must populate both towns to their own limits (A=$($new.A) B=$($new.B))"

# ---------------------------------------------------------------------------------------
# Native two-module fixture wiring (not run here)
# ---------------------------------------------------------------------------------------
$fixturePath = Join-Path $PSScriptRoot 'EAC_MultiModuleGameplay.c'
Assert (Test-Path -LiteralPath $fixturePath -PathType Leaf) 'the two-module native fixture exists'
$fixture = [IO.File]::ReadAllText($fixturePath)
Assert ($fixture.Contains('class EXPG_GarrisonGameplay : GenericEntity')) 'the fixture driver class is the name Run-Gameplay.ps1 copies to'
$expect = [regex]::Match($fixture, "-ExpectResult '([^']+)'")
Assert $expect.Success 'the fixture documents its -ExpectResult pattern'
Assert ($fixture.Contains('PrintFormat("[EAC MULTI RESULT] checks=%1 failures=%2 areas=2 areaA=%3 areaB=%4 independent=%5 split=%6 reason=%7"')) 'the fixture prints one result line'
$sample = '[EAC MULTI RESULT] checks=17 failures=0 areas=2 areaA=2 areaB=1 independent=1 split=1 reason=complete'
Assert ([regex]::IsMatch($sample, $expect.Groups[1].Value)) 'the documented pattern accepts a passing result'
foreach ($bad in '[EAC MULTI RESULT] checks=17 failures=0 areas=2 areaA=2 areaB=0 independent=1 split=1 reason=complete', '[EAC MULTI RESULT] checks=17 failures=1 areas=2 areaA=2 areaB=1 independent=1 split=1 reason=complete', '[EAC MULTI RESULT] checks=17 failures=0 areas=2 areaA=2 areaB=1 independent=0 split=1 reason=complete', '[EAC MULTI RESULT] checks=17 failures=0 areas=2 areaA=2 areaB=1 independent=1 split=1 reason=timeout') {
 Assert (![regex]::IsMatch($bad, $expect.Groups[1].Value)) "the documented pattern must refuse: $bad"
}
foreach ($call in 'EAC_AmbientModule.FindPopulationArea(', 'RefreshPopulationAreas()', 'SetPopulationAreas(m_Centres, m_Radii)', 'EAC_HomeIndex.AREA_FOCUS_BASE + m_ModuleB.GetAreaId()', 'spawner.SelectNext(m_ModuleA, m_Observers)', 'HasAreaCapacity()', 'IsAreaCapped()') {
 Assert ($fixture.Contains($call)) "the fixture exercises $call"
}
Assert ([regex]::Matches($fixture, 'Resource\s+\w+\s*=\s*Resource\.Load\(').Count -eq [regex]::Matches($fixture, 'Resource\.Load\(').Count) 'the fixture keeps every Resource.Load result in a local'
Assert-Enforce ([regex]::Replace($fixture.Replace("`r`n", "`n"), '//[^\n]*', '')) 'EAC_MultiModuleGameplay.c'
Assert (!$fixture.Contains("`r")) 'the fixture keeps LF line endings'

"PASS: several Ambient Civilians modules: every module is its own population area (no duplicate refusal), the index and discovery cover every disc with one cursor per area, admission is charged per area with the 200 ceiling split proportionally and capped/ignored areas reported; model: 0.1.17 leaves town B empty (A=$($old.A) B=$($old.B)), 0.1.18 fills both (A=$($new.A) B=$($new.B)), 4000 random splits within limits; two-module native fixture wired, not run. Native engines were not launched."
