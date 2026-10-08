#requires -Version 7.0
# Portable guard for the WP6 civilian pedestrian performance fixes (0.1.15 performance plan).
#  civilians-a-01: one EAC_ExclusionZone.IsRouteAllowed verdict per route sweep (walker,
#   activity approach, shelter/emerge RouteAllowed) instead of IsTransitAllowed per point;
#   the per-segment test runs only when an automatic disc (not-ready counts) or a
#   transit-blocking zone touches the route's bounding box. AnyTransitBlocked,
#   IsTransitAllowed, IsJourneyTransitAllowed and the traffic sweep are unchanged.
#  civilians-a-02: EAC_CivilianDanger.Monitor hands its proven controller to a 4-argument
#   EAC_CivilianShelter.Monitor that drops the guards Danger just proved and keeps
#   GetVehicleIn; HasCombatThreat and the shelter use the character's own components with
#   the FindComponent fallback; Activity.Monitor asks OwnsActor once.
#  civilians-b-07: EAC_PedestrianActivation caches the bound character's controller,
#   affiliation, AI control and movement components (Bind/Detach); a 3-argument
#   HasCivilianControl/IsCivilian evaluates every live condition over them; IsCivilian
#   and Walk.Step reuse scratch arrays.
#  civilians-b-05: GetHiddenReason tests the minimum distance of every observer before any
#   trace and traces the nearest observer first. No distance cap.
#  civilians-b-06: the far order sweep is staggered by resident id over four passes.
#  civilians-b-12: KnownObservers once per cache-sampler tick; SelectNext's neighbourhood
#   count stops one past the larger of LocalPopulationLimit and MinLocalPopulation.
# Also checks the native equivalence fixture tests/EAC_RouteEquivalenceTest.c is wired
# (it is not run here). No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/ambient-civilians/Scripts/Game/EXPAC'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
$files = 'EAC_ExclusionZone.c', 'EAC_AutoExclusions.c', 'EAC_PedestrianWalk.c', 'EAC_CivilianActivity.c', 'EAC_CivilianShelter.c',
 'EAC_CivilianDanger.c', 'EAC_RoutineEmerge.c', 'EAC_PedestrianSpawner.c', 'EAC_PedestrianActivation.c', 'EAC_PedestrianCaching.c'
# Source with // comments removed; string literals are kept intact.
function Read-Code([string]$Path) {
 $text = [IO.File]::ReadAllText($Path)
 [regex]::Replace($text, '"(?:[^"\\\n]|\\.)*"|//[^\n]*', { param($m) if ($m.Value.StartsWith('"')) { $m.Value } else { '' } })
}
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index + $match.Length); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Count([string]$Text, [string]$Pattern) { [regex]::Matches($Text, $Pattern).Count }
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = [regex]::Match($Text, $First); $b = [regex]::Match($Text, $Second)
 return ($a.Success -and $b.Success -and $a.Index -lt $b.Index)
}
# Enforce gotchas in the methods this package wrote.
function Assert-Enforce([string]$Body, [string]$Where) {
 foreach ($line in $Body -split "`n") {
  Assert (!($line -match '\breturn\s+[^;\s]' -and $line -notmatch '^\s*return\b')) "$Where must keep every value return on its own line: $($line.Trim())"
 }
 Assert (![regex]::IsMatch($Body, 'Math\.RandomFloat\b')) "$Where must not use Math.RandomFloat"
 Assert (![regex]::IsMatch($Body, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait)\b')) "$Where uses a reserved Enforce name"
}

# Line endings: every file this package edits was LF throughout and must stay so.
foreach ($name in $files) {
 $raw = [IO.File]::ReadAllText((Join-Path $scripts $name))
 Assert (!$raw.Contains("`r")) "$name must keep LF line endings on every line"
}
$code = @{}
foreach ($name in $files) { $code[$name] = Read-Code (Join-Path $scripts $name) }
$zoneCode = $code['EAC_ExclusionZone.c']; $auto = $code['EAC_AutoExclusions.c']; $walk = $code['EAC_PedestrianWalk.c']
$activity = $code['EAC_CivilianActivity.c']; $shelter = $code['EAC_CivilianShelter.c']; $danger = $code['EAC_CivilianDanger.c']
$emerge = $code['EAC_RoutineEmerge.c']; $spawner = $code['EAC_PedestrianSpawner.c']; $activation = $code['EAC_PedestrianActivation.c']
$caching = $code['EAC_PedestrianCaching.c']

# ---------------------------------------------------------------------------------------
# civilians-a-01: one route verdict
# ---------------------------------------------------------------------------------------
Assert ([regex]::IsMatch($zoneCode, '(?m)^\s*static\s+const\s+float\s+ROUTE_BOX_MARGIN\s*=\s*1\s*;')) 'EAC_ExclusionZone must keep the 1 m box margin constant'
$isRoute = Get-Body $zoneCode 'static\s+bool\s+IsRouteAllowed\s*\(\s*vector\s+origin\s*,\s*notnull\s+array<vector>\s+route\s*\)'
Assert-Enforce $isRoute 'IsRouteAllowed'
Assert ([regex]::IsMatch($isRoute, '^\s*bool\s+allowed\s*=\s*RouteVerdict\(\s*origin\s*,\s*route\s*\)\s*;\s*if\s*\(\s*EBG_DebugChecks\.Enabled\s*&&\s*allowed\s*!=\s*SegmentRouteAllowed\(\s*origin\s*,\s*route\s*\)\s*\)\s*EBG_DebugChecks\.Mismatch\([^;]*\)\s*;\s*return\s+allowed\s*;\s*$')) 'IsRouteAllowed must return the regrouped verdict and cross-check it only under EBG_DebugChecks.Enabled'
Assert (!(Count $zoneCode 'EBG_DebugChecks\.Enabled\s*=(?!=)')) 'EAC_ExclusionZone may only read EBG_DebugChecks.Enabled'
$segmentRoute = Get-Body $zoneCode 'protected\s+static\s+bool\s+SegmentRouteAllowed\s*\(\s*vector\s+origin\s*,\s*array<vector>\s+route\s*\)'
Assert ([regex]::IsMatch($segmentRoute, '^\s*vector\s+previous\s*=\s*origin\s*;\s*foreach\s*\(\s*vector\s+point\s*:\s*route\s*\)\s*\{\s*if\s*\(\s*!IsTransitAllowed\(\s*previous\s*,\s*point\s*\)\s*\)\s*return\s+false\s*;\s*previous\s*=\s*point\s*;\s*\}\s*return\s+true\s*;\s*$')) 'the debug cross-check must be the old per-segment loop'
$route = Get-Body $zoneCode 'protected\s+static\s+bool\s+RouteVerdict\s*\(\s*vector\s+origin\s*,\s*array<vector>\s+route\s*\)'
Assert-Enforce $route 'RouteVerdict'
Assert ([regex]::IsMatch($route, '^\s*if\s*\(\s*route\.IsEmpty\(\)\s*\)\s*return\s+true\s*;')) 'the route verdict must pass an empty route first (the old loop ran zero times)'
Assert ((Count $route 'EAC_AmbientModule\.GetActive\(\)') -eq 1) 'the route verdict must look the active module up once'
Assert ([regex]::IsMatch($route, 'if\s*\(\s*active\s*\)\s*\{\s*if\s*\(\s*!active\.ContainsPopulationPosition\(\s*origin\s*\)\s*\)\s*return\s+false\s*;\s*foreach\s*\(\s*vector\s+(\w+)\s*:\s*route\s*\)\s*\{\s*if\s*\(\s*!active\.ContainsPopulationPosition\(\s*\1\s*\)\s*\)\s*return\s+false\s*;\s*\}\s*\}')) 'the area half must test the origin and every point, only with an active module'
foreach ($term in 'if\s*\(\s*corner\[0\]\s*<\s*minX\s*\)\s*minX\s*=\s*corner\[0\]', 'if\s*\(\s*corner\[0\]\s*>\s*maxX\s*\)\s*maxX\s*=\s*corner\[0\]', 'if\s*\(\s*corner\[2\]\s*<\s*minZ\s*\)\s*minZ\s*=\s*corner\[2\]', 'if\s*\(\s*corner\[2\]\s*>\s*maxZ\s*\)\s*maxZ\s*=\s*corner\[2\]') {
 Assert ([regex]::IsMatch($route, $term)) "the route box must be built over every point: $term"
}
Assert ([regex]::IsMatch($route, 'float\s+minX\s*=\s*origin\[0\]\s*;\s*float\s+maxX\s*=\s*origin\[0\]\s*;\s*float\s+minZ\s*=\s*origin\[2\]\s*;\s*float\s+maxZ\s*=\s*origin\[2\]\s*;')) 'the route box must start at the origin'
Assert ([regex]::IsMatch($route, 'if\s*\(\s*!EAC_AutoExclusions\.AnyDiscTouchesBox\(\s*minX\s*,\s*minZ\s*,\s*maxX\s*,\s*maxZ\s*\)\s*&&\s*!AnyBlockingZoneTouchesBox\(\s*minX\s*,\s*minZ\s*,\s*maxX\s*,\s*maxZ\s*\)\s*\)\s*return\s+true\s*;')) 'only a box clear of every disc and blocking zone may skip the segment test'
Assert ([regex]::IsMatch($route, 'vector\s+previous\s*=\s*origin\s*;\s*foreach\s*\(\s*vector\s+next\s*:\s*route\s*\)\s*\{\s*if\s*\(\s*!IsJourneyTransitAllowed\(\s*previous\s*,\s*next\s*\)\s*\)\s*return\s+false\s*;\s*previous\s*=\s*next\s*;\s*\}\s*return\s+true\s*;\s*$')) 'the fallback must run IsJourneyTransitAllowed over every segment from the origin'
Assert (!(Count $route '\bIsTransitAllowed\s*\(')) 'the route verdict must not call IsTransitAllowed (its area half is the per-point test above)'
$zoneBox = Get-Body $zoneCode 'static\s+bool\s+AnyBlockingZoneTouchesBox\s*\(\s*float\s+minX\s*,\s*float\s+minZ\s*,\s*float\s+maxX\s*,\s*float\s+maxZ\s*\)'
Assert-Enforce $zoneBox 'AnyBlockingZoneTouchesBox'
Assert (Before $zoneBox 'if\s*\(\s*s_Zones\.IsEmpty\(\)\s*\)\s*return\s+false\s*;' 'if\s*\(\s*!GetGame\(\)\s*\|\|\s*!Replication\.IsServer\(\)\s*\)\s*return\s+true\s*;') 'an empty zone list passes before the refusing states answer true'
Assert ([regex]::IsMatch($zoneBox, 'if\s*\(\s*!world\s*\)\s*return\s+true\s*;')) 'no world must send the route to the refusing per-segment test'
Assert ([regex]::IsMatch($zoneBox, 'if\s*\(\s*!zone\s*\|\|\s*zone\.GetWorld\(\)\s*!=\s*world\s*\|\|\s*zone\.BlockTransit\s*!=\s*1\s*\)\s*continue\s*;')) 'the zone filter must match IsJourneyTransitAllowed'
Assert ([regex]::IsMatch($zoneBox, 'Math\.AbsFloat\(\s*radius\s*\)\s*\+\s*ROUTE_BOX_MARGIN')) 'zone reach must be |radius| plus the margin'
Assert ([regex]::IsMatch($zoneBox, 'Math\.Clamp\(\s*origin\[0\]\s*,\s*minX\s*,\s*maxX\s*\)') -and [regex]::IsMatch($zoneBox, 'Math\.Clamp\(\s*origin\[2\]\s*,\s*minZ\s*,\s*maxZ\s*\)')) 'zone test must clamp the centre into the box'
$discBox = Get-Body $auto 'static\s+bool\s+AnyDiscTouchesBox\s*\(\s*float\s+minX\s*,\s*float\s+minZ\s*,\s*float\s+maxX\s*,\s*float\s+maxZ\s*\)'
Assert-Enforce $discBox 'AnyDiscTouchesBox'
Assert ([regex]::IsMatch($discBox, '^\s*EXPBG_LazyStatics_EAC_AutoExclusions\(\)\s*;\s*if\s*\(\s*!IsReady\(\)\s*\)\s*return\s+true\s*;')) 'not-ready auto exclusions must send every route to the refusing segment test'
Assert ([regex]::IsMatch($discBox, 'Math\.AbsFloat\(\s*s_Radii\[i\]\s*\)\s*\+\s*EAC_ExclusionZone\.ROUTE_BOX_MARGIN')) 'disc reach must be |radius| plus the margin'
Assert ([regex]::IsMatch($discBox, 'Math\.Clamp\(\s*centre\[0\]\s*,\s*minX\s*,\s*maxX\s*\)') -and [regex]::IsMatch($discBox, 'Math\.Clamp\(\s*centre\[2\]\s*,\s*minZ\s*,\s*maxZ\s*\)')) 'disc test must clamp the centre into the box'
# The unchanged predicates.
Assert ([regex]::IsMatch((Get-Body $zoneCode 'static\s+bool\s+AnyTransitBlocked\s*\(\s*\)'), '^\s*if\s*\(\s*EAC_AmbientModule\.GetActive\(\)\s*\)\s*return\s+true\s*;\s*return\s+AnyJourneyTransitBlocked\(\)\s*;\s*$')) 'AnyTransitBlocked must stay unchanged'
Assert ([regex]::IsMatch((Get-Body $zoneCode 'static\s+bool\s+IsTransitAllowed\s*\(\s*vector\s+from\s*,\s*vector\s+to\s*\)'), '^\s*if\s*\(\s*EAC_AmbientModule\.IsOutsidePopulationArea\(\s*from\s*\)\s*\|\|\s*EAC_AmbientModule\.IsOutsidePopulationArea\(\s*to\s*\)\s*\)\s*return\s+false\s*;\s*return\s+IsJourneyTransitAllowed\(\s*from\s*,\s*to\s*\)\s*;\s*$')) 'IsTransitAllowed must stay unchanged'
Assert ((Count (Get-Body $zoneCode 'static\s+bool\s+IsJourneyTransitAllowed\s*\(\s*vector\s+from\s*,\s*vector\s+to\s*\)') 'SegmentIntersectsDisc\(\s*from\s*,\s*to\s*,\s*zone\.GetOrigin\(\)\s*,\s*zone\.RadiusMeters\s*\)') -eq 1) 'IsJourneyTransitAllowed must keep its per-zone segment test'
# Callers.
$walkMonitor = Get-Body $walk 'void\s+Monitor\s*\(\s*EAC_AmbientModule\s+module\s*,\s*EAC_PedestrianActivation\s+activation\s*,\s*float\s+now\s*\)'
Assert (!(Count $walkMonitor 'for\s*\(\s*int\s+segment')) 'Walk.Monitor must not sweep segments itself'
Assert ([regex]::IsMatch($walkMonitor, 'if\s*\(\s*EAC_ExclusionZone\.AnyTransitBlocked\(\)\s*\)\s*\{\s*if\s*\(\s*allowed\s*\)\s*allowed\s*=\s*EAC_ExclusionZone\.IsRouteAllowed\(\s*actor\.GetOrigin\(\)\s*,\s*m_ScratchRoute\s*\)\s*;\s*\}')) 'Walk.Monitor must take one IsRouteAllowed verdict behind the unchanged AnyTransitBlocked gate'
Assert ([regex]::IsMatch($walkMonitor, 'EAC_ExclusionZone\.IsTransitAllowed\(\s*actor\.GetOrigin\(\)\s*,\s*m_Waypoint\.GetOrigin\(\)\s*\)')) 'Walk.Monitor must keep its straight-line test'
$activityMonitor = Get-Body $activity 'bool\s+Monitor\s*\(\s*EAC_AmbientModule\s+module\s*,\s*float\s+now\s*\)'
Assert ([regex]::IsMatch($activityMonitor, 'if\s*\(\s*sweepRoute\s*&&\s*!m_Stopping\s*&&\s*!EAC_ExclusionZone\.IsRouteAllowed\(\s*m_Actor\.GetOrigin\(\)\s*,\s*m_ScratchRoute\s*\)\s*\)\s*\{\s*AbortStop\(\s*EAC_RoutineStats\.ABORT_ROUTE\s*\)\s*;\s*\}')) 'Activity.Monitor must abort once on a refused route, never once a stop is requested'
Assert (!(Count $activityMonitor 'IsTransitAllowed\(\s*previous')) 'Activity.Monitor must not sweep segments itself'
Assert ([regex]::IsMatch($activityMonitor, 'sweepRoute\s*&&\s*!EAC_ExclusionZone\.IsTransitAllowed\(\s*m_Actor\.GetOrigin\(\)\s*,\s*m_Point\.GetOrigin\(\)\s*\)')) 'Activity.Monitor must keep its straight-line test'
Assert (Before $activityMonitor 'ABORT_DISTANT' 'IsRouteAllowed\(') 'the 64-segment distance abort must still come before the route verdict'
$routeAllowed = Get-Body $shelter 'static\s+bool\s+RouteAllowed\s*\(\s*vector\s+from\s*,\s*vector\s+destination\s*,\s*array<vector>\s+route\s*\)'
Assert ([regex]::IsMatch($routeAllowed, 'if\s*\(\s*!EAC_ExclusionZone\.AnyTransitBlocked\(\)\s*\)\s*return\s+true\s*;\s*return\s+EAC_ExclusionZone\.IsRouteAllowed\(\s*from\s*,\s*route\s*\)\s*;\s*$')) 'Shelter.RouteAllowed must end with one IsRouteAllowed verdict behind the unchanged gate'
Assert (Before $routeAllowed 'route\.Count\(\)\s*>\s*64' 'AnyTransitBlocked') 'RouteAllowed must keep its count, destination and straight-line checks first'
Assert (!(Count $routeAllowed '\bforeach\b')) 'Shelter.RouteAllowed must not sweep points itself'
$emergeMonitor = Get-Body $emerge 'void\s+Monitor\s*\(\s*EAC_AmbientModule\s+module\s*,\s*EAC_PedestrianActivation\s+activation\s*,\s*float\s+now\s*\)'
Assert ([regex]::IsMatch($emergeMonitor, 'EAC_CivilianShelter\.RouteAllowed\(\s*actor\.GetOrigin\(\)\s*,\s*m_Destination\s*,\s*m_ScratchRoute\s*\)')) 'Emerge.Monitor must reach IsRouteAllowed through Shelter.RouteAllowed'
foreach ($name in $files) {
 $permitted = 0; if ($name -eq 'EAC_ExclusionZone.c') { $permitted = 1 }
 Assert ((Count $code[$name] 'IsTransitAllowed\(\s*previous\s*,') -eq $permitted) "$name must not sweep IsTransitAllowed point by point (only the debug cross-check may)"
}

# Model of the regrouped verdict in single-precision arithmetic (the engine's float): the
# per-segment disc/zone test against the box pre-test plus fallback, on random routes at map
# coordinates and on routes tangent to every disc. Proves the margin makes the box test a
# superset of the segment test (no route skips a segment that touches a disc).
Add-Type -TypeDefinition @'
using System;
public static class EacRouteModel {
 static bool Segment(float fx, float fz, float tx, float tz, float ox, float oz, float r) {
  float sx = tx - fx; float sz = tz - fz; float len = sx * sx + sz * sz; float k = 0f;
  if (len > 0f) { k = ((ox - fx) * sx + (oz - fz) * sz) / len; k = Math.Clamp(k, 0f, 1f); }
  float cx = fx + k * sx; float cz = fz + k * sz; float dx = cx - ox; float dz = cz - oz;
  return dx * dx + dz * dz <= r * r;
 }
 static bool Touches(float minX, float minZ, float maxX, float maxZ, float ox, float oz, float r, float margin) {
  float dx = Math.Clamp(ox, minX, maxX) - ox; float dz = Math.Clamp(oz, minZ, maxZ) - oz; float reach = Math.Abs(r) + margin;
  return dx * dx + dz * dz <= reach * reach;
 }
 // Returns the number of verdict differences; counts allowed routes and box-clear routes.
 public static int Compare(float[] xs, float[] zs, int n, float[] ox, float[] oz, float[] rr, float margin, ref int allowed, ref int clear) {
  bool reference = true;
  for (int i = 1; i < n && reference; i++)
   for (int d = 0; d < ox.Length && reference; d++)
    if (Segment(xs[i - 1], zs[i - 1], xs[i], zs[i], ox[d], oz[d], rr[d])) reference = false;
  float minX = xs[0], maxX = xs[0], minZ = zs[0], maxZ = zs[0];
  for (int i = 1; i < n; i++) { minX = Math.Min(minX, xs[i]); maxX = Math.Max(maxX, xs[i]); minZ = Math.Min(minZ, zs[i]); maxZ = Math.Max(maxZ, zs[i]); }
  bool touch = false;
  for (int d = 0; d < ox.Length && !touch; d++) touch = Touches(minX, minZ, maxX, maxZ, ox[d], oz[d], rr[d], margin);
  bool candidate = true;
  if (n > 1 && touch) {
   for (int i = 1; i < n && candidate; i++)
    for (int d = 0; d < ox.Length && candidate; d++)
     if (Segment(xs[i - 1], zs[i - 1], xs[i], zs[i], ox[d], oz[d], rr[d])) candidate = false;
  }
  if (reference) allowed++;
  if (n > 1 && !touch) clear++;
  return reference == candidate ? 0 : 1;
 }
 public static int Run(int seed, int routes, float margin, out int allowed, out int clear, out int tangents) {
  var random = new Random(seed); allowed = 0; clear = 0; tangents = 0; int differences = 0;
  float baseX = 11873.37f, baseZ = 4211.91f;
  int discs = 12; var ox = new float[discs]; var oz = new float[discs]; var rr = new float[discs];
  for (int d = 0; d < discs; d++) { ox[d] = baseX + (float)(random.NextDouble() * 1000 - 500); oz[d] = baseZ + (float)(random.NextDouble() * 1000 - 500); rr[d] = d % 3 == 0 ? 350f : (float)(5 + random.NextDouble() * 120); }
  var xs = new float[65]; var zs = new float[65];
  for (int r = 0; r < routes; r++) {
   int n = 1 + random.Next(0, 65);
   xs[0] = baseX + (float)(random.NextDouble() * 1200 - 600); zs[0] = baseZ + (float)(random.NextDouble() * 1200 - 600);
   for (int i = 1; i < n; i++) {
    bool repeat = random.Next(0, 16) == 0;
    xs[i] = repeat ? xs[i - 1] : xs[i - 1] + (float)(random.NextDouble() * 80 - 40);
    zs[i] = repeat ? zs[i - 1] : zs[i - 1] + (float)(random.NextDouble() * 80 - 40);
   }
   differences += Compare(xs, zs, n, ox, oz, rr, margin, ref allowed, ref clear);
  }
  // Routes tangent to each disc: vertical lines at centre + radius + offset, incl. one ulp either side.
  foreach (float baseOffset in new float[] { -0.01f, 0f, 0.0005f, 0.01f, 0.5f, 0.99f, 1.01f, 2f }) {
   for (int d = 0; d < discs; d++) {
    float x = ox[d] + rr[d] + baseOffset;
    foreach (float lineX in new float[] { MathF.BitDecrement(x), x, MathF.BitIncrement(x) }) {
     xs[0] = lineX; zs[0] = oz[d] - 30f; xs[1] = lineX; zs[1] = oz[d] + 30f;
     differences += Compare(xs, zs, 2, ox, oz, rr, margin, ref allowed, ref clear); tangents++;
     xs[0] = ox[d] - 30f; zs[0] = oz[d] + rr[d] + baseOffset; xs[1] = ox[d] + 30f; zs[1] = zs[0];
     differences += Compare(xs, zs, 2, ox, oz, rr, margin, ref allowed, ref clear); tangents++;
    }
   }
  }
  return differences;
 }
}
'@
$allowed = 0; $clear = 0; $tangents = 0
$modelDifferences = [EacRouteModel]::Run(20261007, 20000, 1.0, [ref]$allowed, [ref]$clear, [ref]$tangents)
Assert ($modelDifferences -eq 0) "single-precision model: the box pre-test with the 1 m margin changed $modelDifferences route verdicts"
Assert ($allowed -gt 0 -and $clear -gt 0 -and $clear -lt 20000) "single-precision model must exercise both verdicts and both paths (allowed $allowed, box clear $clear)"

# ---------------------------------------------------------------------------------------
# civilians-a-02: Danger hands its proof to the shelter
# ---------------------------------------------------------------------------------------
$dangerMonitor = Get-Body $danger 'static\s+void\s+Monitor\s*\(\s*EAC_AmbientModule\s+module\s*,\s*EAC_PedestrianActivation\s+activation\s*,\s*float\s+now\s*\)'
Assert ([regex]::IsMatch($dangerMonitor, 'activation\.Shelter\.Monitor\(\s*module\s*,\s*activation\s*,\s*now\s*,\s*controller\s*\)\s*;\s*$')) 'Danger.Monitor must end with the 4-argument shelter call'
Assert ((Count $dangerMonitor 'SCR_PossessingManagerComponent\.GetPlayerIdFromControlledEntity\(\s*claim\.Character\s*\)\s*!=\s*0') -eq 1) 'Danger.Monitor must still ask the possession query'
Assert ([regex]::IsMatch($dangerMonitor, 'module\.GetResidentActivation\(\s*claim\.Home\s*,\s*claim\.Resident\s*\)\s*!=\s*claim') -and [regex]::IsMatch($dangerMonitor, 'controller\.IsPlayerControlled\(\)') -and [regex]::IsMatch($dangerMonitor, 'module\s*!=\s*EAC_AmbientModule\.GetActive\(\)')) 'Danger.Monitor must keep the guards the shelter overload relies on'
Assert ([regex]::IsMatch($dangerMonitor, 'HasCivilianControl\(\s*claim\.Character\s*,\s*claim\.Group\s*,\s*activation\s*\)') -and [regex]::IsMatch($dangerMonitor, 'CharacterControllerComponent\s+controller\s*=\s*activation\.CachedController\(\s*claim\.Character\s*\)\s*;')) 'Danger.Monitor must use the activation cache'
$shelterOld = Get-Body $shelter 'void\s+Monitor\s*\(\s*EAC_AmbientModule\s+module\s*,\s*EAC_PedestrianActivation\s+activation\s*,\s*float\s+now\s*\)'
foreach ($guard in 'module\s*!=\s*EAC_AmbientModule\.GetActive\(\)', 'GetResidentActivation\(', 'HasCivilianControl\(\s*claim\.Character\s*,\s*claim\.Group\s*\)', 'GetPlayerIdFromControlledEntity\(\s*actor\s*\)', 'controller\.IsPlayerControlled\(\)', 'CompartmentAccessComponent\.GetVehicleIn\(\s*actor\s*\)') {
 Assert ([regex]::IsMatch($shelterOld, $guard)) "the 3-argument Shelter.Monitor must keep its guard: $guard"
}
Assert ([regex]::IsMatch($shelterOld, 'Respond\(\s*module\s*,\s*activation\s*,\s*now\s*,\s*claim\s*,\s*actor\s*,\s*controller\s*\)\s*;\s*$')) 'the 3-argument Shelter.Monitor must respond after its guards'
$shelterNew = Get-Body $shelter 'void\s+Monitor\s*\(\s*EAC_AmbientModule\s+module\s*,\s*EAC_PedestrianActivation\s+activation\s*,\s*float\s+now\s*,\s*CharacterControllerComponent\s+controller\s*\)'
foreach ($dropped in 'GetActive\(', 'GetResidentActivation\(', 'HasCivilianControl\(', 'GetPlayerIdFromControlledEntity\(', 'FindComponent\(') {
 Assert (!(Count $shelterNew $dropped)) "the 4-argument Shelter.Monitor must not repeat: $dropped"
}
foreach ($kept in '!controller', 'CompartmentAccessComponent\.GetVehicleIn\(\s*actor\s*\)', 'claim\.OptimizerMember\.WasPlayer', 'claim\.Cache', 'activation\.PlayerTouched', '!claim\.Committed') {
 Assert ([regex]::IsMatch($shelterNew, $kept)) "the 4-argument Shelter.Monitor must keep: $kept"
}
Assert ([regex]::IsMatch($shelterNew, 'Respond\(\s*module\s*,\s*activation\s*,\s*now\s*,\s*claim\s*,\s*actor\s*,\s*controller\s*\)\s*;\s*$')) 'the 4-argument Shelter.Monitor must respond after its guards'
$respond = Get-Body $shelter 'protected\s+void\s+Respond\s*\('
Assert ([regex]::IsMatch($respond, 'CompartmentAccessComponent\s+access\s*=\s*FindAccess\(\s*actor\s*\)\s*;') -and [regex]::IsMatch($respond, 'activation\.CachedMovement\(\s*actor\s*\)')) 'Respond must use the character components'
$findAccess = Get-Body $shelter 'protected\s+static\s+CompartmentAccessComponent\s+FindAccess\s*\(\s*IEntity\s+actor\s*\)'
Assert-Enforce $findAccess 'FindAccess'
Assert ([regex]::IsMatch($findAccess, 'GetCompartmentAccessComponent\(\)') -and [regex]::IsMatch($findAccess, 'return\s+CompartmentAccessComponent\.Cast\(\s*actor\.FindComponent\(\s*CompartmentAccessComponent\s*\)\s*\)\s*;\s*$')) 'FindAccess must fall back to FindComponent'
$threat = Get-Body $danger 'static\s+bool\s+HasCombatThreat\s*\(\s*IEntity\s+actor\s*\)'
Assert ([regex]::IsMatch($threat, 'control\s*=\s*character\.GetAIControlComponent\(\)\s*;') -and [regex]::IsMatch($threat, 'if\s*\(\s*!control\s*\)\s*control\s*=\s*AIControlComponent\.Cast\(\s*actor\.FindComponent\(\s*AIControlComponent\s*\)\s*\)\s*;')) 'HasCombatThreat must fall back to FindComponent for AI control'
Assert ([regex]::IsMatch($threat, 'damage\s*=\s*SCR_CharacterDamageManagerComponent\.Cast\(\s*character\.GetDamageManager\(\)\s*\)\s*;') -and [regex]::IsMatch($threat, 'if\s*\(\s*!damage\s*\)\s*damage\s*=\s*SCR_CharacterDamageManagerComponent\.Cast\(\s*actor\.FindComponent\(\s*SCR_CharacterDamageManagerComponent\s*\)\s*\)\s*;')) 'HasCombatThreat must fall back to FindComponent for damage'
Assert ([regex]::IsMatch($threat, 'if\s*\(\s*!control\s*\|\|\s*!control\.GetAIAgent\(\)\s*\)\s*return\s+true\s*;') -and [regex]::IsMatch($threat, 'return\s+!utility\s*\|\|\s*!utility\.m_ThreatSystem\s*\|\|\s*!damage\s*\|\|\s*CombatAlarm\(')) 'HasCombatThreat must keep "missing component means threat"'
Assert ((Count $activityMonitor '\bOwnsActor\(\)') -eq 1) 'Activity.Monitor must ask OwnsActor once'

# ---------------------------------------------------------------------------------------
# civilians-b-07: cached components and scratch arrays
# ---------------------------------------------------------------------------------------
$cached = @{ Controller = 'CharacterControllerComponent'; Affiliation = 'FactionAffiliationComponent'; AIControl = 'AIControlComponent'; Movement = 'AICharacterMovementComponent' }
Assert ([regex]::IsMatch($activation, '(?m)^\s*protected\s+IEntity\s+m_CachedActor\s*;')) 'the activation must remember the bound actor without an initializer'
$bind = Get-Body $activation 'bool\s+Bind\s*\(\s*\)'
$detach = Get-Body $activation 'void\s+Detach\s*\(\s*\)'
Assert ([regex]::IsMatch($bind, 'm_CachedActor\s*=\s*Claim\.Character\s*;')) 'Bind must record the bound actor'
Assert ([regex]::IsMatch($detach, 'm_CachedActor\s*=\s*null\s*;')) 'Detach must drop the bound actor'
foreach ($key in $cached.Keys) {
 $type = $cached[$key]
 Assert ([regex]::IsMatch($activation, "(?m)^\s*protected\s+$type\s+m_Cached$key\s*;")) "the activation must declare m_Cached$key without an initializer"
 Assert ([regex]::IsMatch($bind, "m_Cached$key\s*=\s*$type\.Cast\(\s*Claim\.Character\.FindComponent\(\s*$type\s*\)\s*\)\s*;")) "Bind must resolve m_Cached$key with the same FindComponent"
 Assert ([regex]::IsMatch($detach, "m_Cached$key\s*=\s*null\s*;")) "Detach must drop m_Cached$key"
 $accessor = Get-Body $activation "$type\s+Cached$key\s*\(\s*IEntity\s+actor\s*\)"
 Assert-Enforce $accessor "Cached$key"
 Assert ([regex]::IsMatch($accessor, "^\s*if\s*\(\s*!actor\s*\)\s*return\s+null\s*;\s*if\s*\(\s*actor\s*==\s*m_CachedActor\s*&&\s*m_Cached$key\s*\)\s*return\s+m_Cached$key\s*;\s*return\s+$type\.Cast\(\s*actor\.FindComponent\(\s*$type\s*\)\s*\)\s*;\s*$")) "Cached$key must serve only the bound actor and fall back to FindComponent"
}
# The 3-argument HasCivilianControl evaluates exactly the live conditions of the original.
$controlOld = Get-Body $spawner 'static\s+bool\s+HasCivilianControl\s*\(\s*IEntity\s+actor\s*,\s*SCR_AIGroup\s+group\s*\)'
$controlNew = Get-Body $spawner 'static\s+bool\s+HasCivilianControl\s*\(\s*IEntity\s+actor\s*,\s*SCR_AIGroup\s+group\s*,\s*EAC_PedestrianActivation\s+activation\s*\)'
Assert-Enforce $controlNew 'HasCivilianControl(activation)'
Assert ([regex]::IsMatch($controlNew, '^\s*if\s*\(\s*!activation\s*\)\s*return\s+HasCivilianControl\(\s*actor\s*,\s*group\s*\)\s*;')) 'without an activation the overload must be the original'
function Get-Conditions([string]$Body) { @([regex]::Matches($Body, 'if\s*\((.*?)\)\s*(?:\r?\n\s*)?return\s+false\s*;') | ForEach-Object { ($_.Groups[1].Value -replace '\s+', ' ').Trim() }) }
$oldConditions = Get-Conditions $controlOld; $newConditions = Get-Conditions $controlNew
Assert ($oldConditions.Count -eq 3 -and $newConditions.Count -eq 3) 'both HasCivilianControl forms must keep three refusing conditions'
for ($i = 0; $i -lt 3; $i++) { Assert ($oldConditions[$i] -ceq $newConditions[$i]) "HasCivilianControl condition $i must be identical in both forms: '$($oldConditions[$i])' vs '$($newConditions[$i])'" }
Assert (!(Count $controlNew 'FindComponent\(')) 'the 3-argument HasCivilianControl must take its components from the activation'
foreach ($key in 'Controller', 'Affiliation', 'AIControl') { Assert ([regex]::IsMatch($controlNew, "activation\.Cached$key\(\s*actor\s*\)")) "the 3-argument HasCivilianControl must use Cached$key" }
Assert ((Count $spawner 'static\s+bool\s+IsCivilian\s*\(') -eq 2) 'IsCivilian keeps its 2-argument form and gains the activation form'
Assert ([regex]::IsMatch((Get-Body $spawner 'static\s+bool\s+IsCivilian\s*\(\s*IEntity\s+actor\s*,\s*SCR_AIGroup\s+group\s*\)'), 'HasCivilianControl\(\s*actor\s*,\s*group\s*\)[\s\S]*return\s+CarriesNoWeapon\(\s*actor\s*\)\s*;\s*$')) 'IsCivilian must keep control before equipment'
Assert ([regex]::IsMatch((Get-Body $spawner 'static\s+bool\s+IsCivilian\s*\(\s*IEntity\s+actor\s*,\s*SCR_AIGroup\s+group\s*,\s*EAC_PedestrianActivation\s+activation\s*\)'), 'HasCivilianControl\(\s*actor\s*,\s*group\s*,\s*activation\s*\)[\s\S]*return\s+CarriesNoWeapon\(\s*actor\s*\)\s*;\s*$')) 'the activation IsCivilian must keep control before equipment'
$weapon = Get-Body $spawner 'protected\s+static\s+bool\s+CarriesNoWeapon\s*\(\s*IEntity\s+actor\s*\)'
Assert-Enforce $weapon 'CarriesNoWeapon'
Assert ([regex]::IsMatch($weapon, '^\s*EXPBG_LazyStatics_EAC_PedestrianSpawner\(\)\s*;')) 'CarriesNoWeapon must create its scratch array lazily'
Assert ((Before $weapon 'GetGrenadesCount\(\)' 's_CarriedScratch\.Clear\(\)\s*;\s*weapons\.GetWeaponsList\(\s*s_CarriedScratch\s*\)') -and (Before $weapon 'GetWeaponsList' 'GetItems\(\s*s_CarriedScratch') -and (Before $weapon 'GetItems' 'FindComponent\(\s*BaseWeaponComponent\s*\)')) 'CarriesNoWeapon must keep the original order: grenades, weapons, items, weapon components'
Assert ([regex]::IsMatch($weapon, 's_CarriedScratch\.Clear\(\)\s*;\s*return\s+unarmed\s*;\s*$')) 'CarriesNoWeapon must clear the scratch array before returning'
Assert ([regex]::IsMatch($spawner, '(?m)^\s*protected\s+static\s+ref\s+array<IEntity>\s+s_CarriedScratch\s*;')) 's_CarriedScratch must be declared without an initializer'
Assert ([regex]::IsMatch((Get-Body $spawner 'protected\s+static\s+void\s+EXPBG_LazyStatics_EAC_PedestrianSpawner\s*\(\s*\)'), 'if\s*\(\s*!s_CarriedScratch\s*\)\s*s_CarriedScratch\s*=\s*new\s+array<IEntity>\(\)\s*;')) 's_CarriedScratch must be created on first use'
$walkStep = Get-Body $walk 'void\s+Step\s*\(\s*EAC_AmbientModule\s+module\s*,\s*EAC_PedestrianActivation\s+activation\s*\)'
Assert ([regex]::IsMatch($walkStep, 'EAC_PedestrianSpawner\.IsCivilian\(\s*actor\s*,\s*claim\.Group\s*,\s*activation\s*\)') -and [regex]::IsMatch($walkStep, 'activation\.CachedAIControl\(\s*actor\s*\)')) 'Walk.Step must use the activation cache'
Assert ([regex]::IsMatch($walkStep, 'm_ScratchOrders\.Clear\(\)\s*;\s*claim\.Group\.GetWaypoints\(\s*m_ScratchOrders\s*\)\s*;') -and !(Count $walkStep 'array<AIWaypoint>\s+\w+\s*=\s*\{\s*\}')) 'Walk.Step must reuse its waypoint scratch array'
Assert ([regex]::IsMatch($walk, '(?m)^\s*protected\s+ref\s+array<AIWaypoint>\s+m_ScratchOrders\s*=\s*\{\s*\}\s*;')) 'the walker must own the waypoint scratch array'
Assert ([regex]::IsMatch($walkMonitor, 'HasCivilianControl\(\s*actor\s*,\s*claim\.Group\s*,\s*activation\s*\)') -and [regex]::IsMatch($walkMonitor, 'activation\.CachedMovement\(\s*actor\s*\)')) 'Walk.Monitor must use the activation cache'
Assert ([regex]::IsMatch($emergeMonitor, 'HasCivilianControl\(\s*actor\s*,\s*claim\.Group\s*,\s*activation\s*\)') -and [regex]::IsMatch($emergeMonitor, 'activation\.CachedMovement\(\s*actor\s*\)') -and [regex]::IsMatch($emergeMonitor, 'activation\.CachedAIControl\(\s*actor\s*\)')) 'Emerge.Monitor must use the activation cache'
Assert ([regex]::IsMatch($activityMonitor, 'if\s*\(\s*m_Activation\s*\)\s*movement\s*=\s*m_Activation\.CachedMovement\(\s*m_Actor\s*\)\s*;\s*else\s+movement\s*=\s*AICharacterMovementComponent\.Cast\(\s*m_Actor\.FindComponent\(\s*AICharacterMovementComponent\s*\)\s*\)\s*;')) 'Activity.Monitor must use the cache or the original lookup'

# ---------------------------------------------------------------------------------------
# civilians-b-05: distance pass, then nearest observer first
# ---------------------------------------------------------------------------------------
$hidden = Get-Body $spawner 'static\s+int\s+GetHiddenReason\s*\('
Assert-Enforce $hidden 'GetHiddenReason'
Assert (Before $hidden 'return\s+EAC_ESpawnReason\.TOO_NEAR' 'HiddenFrom\(') 'every minimum-distance test must run before the first trace'
Assert ([regex]::IsMatch($hidden, 'if\s*\(\s*distance\s*<\s*minimumDistance\s*\)\s*return\s+EAC_ESpawnReason\.TOO_NEAR\s*;')) 'the minimum-distance rule must be unchanged'
Assert ([regex]::IsMatch($hidden, 'float\s+distance\s*=\s*vector\.Distance\(\s*watcher\.GetOrigin\(\)\s*,\s*position\s*\)\s*;')) 'the distance must be the same 3D distance as before'
Assert ([regex]::IsMatch($hidden, 'if\s*\(\s*nearest\s*>=\s*0\s*&&\s*!HiddenFrom\(\s*world\s*,\s*position\s*,\s*observers\[nearest\]\s*,\s*exclude\s*\)\s*\)\s*return\s+EAC_ESpawnReason\.VISIBLE\s*;')) 'the nearest observer must be traced first'
Assert ([regex]::IsMatch($hidden, 'for\s*\(\s*int\s+other\s*=\s*0\s*;\s*other\s*<\s*observers\.Count\(\)\s*;\s*other\+\+\s*\)\s*\{\s*if\s*\(\s*other\s*==\s*nearest\s*\)\s*continue\s*;\s*if\s*\(\s*!HiddenFrom\(\s*world\s*,\s*position\s*,\s*observers\[other\]\s*,\s*exclude\s*\)\s*\)\s*return\s+EAC_ESpawnReason\.VISIBLE\s*;\s*\}\s*return\s+EAC_ESpawnReason\.NONE\s*;\s*$')) 'every other observer must still be traced, in list order'
Assert (!(Count $hidden 'TraceMove|REACTIVE_RADIUS|WakeDistance')) 'GetHiddenReason must not trace itself or cap the sight distance'
$hiddenFrom = Get-Body $spawner 'protected\s+static\s+bool\s+HiddenFrom\s*\('
Assert-Enforce $hiddenFrom 'HiddenFrom'
Assert ((Count $hiddenFrom 'world\.TraceMove\(\s*s_HiddenTrace\s*,\s*null\s*\)\s*>=\s*1') -eq 1 -and [regex]::IsMatch($hiddenFrom, 'for\s*\(\s*int\s+sample\s*=\s*0\s*;\s*sample\s*<\s*3\s*;') -and [regex]::IsMatch($hiddenFrom, 'TraceFlags\.WORLD\s*\|\s*TraceFlags\.ENTS\s*\|\s*TraceFlags\.VISIBILITY\s*\|\s*TraceFlags\.ANY_CONTACT')) 'HiddenFrom must keep the three original traces'
Assert ((Count $spawner 's_HiddenTrace\s*,\s*null') -eq 1) 'only HiddenFrom may fire the visibility traces'

# ---------------------------------------------------------------------------------------
# civilians-b-06: staggered far sweep
# ---------------------------------------------------------------------------------------
Assert (!(Count $spawner 'm_NextFarSweep')) 'the far-sweep time gate must be gone'
Assert ([regex]::IsMatch($spawner, '(?m)^\s*protected\s+int\s+m_MonitorTick\s*;')) 'the pass counter must be a plain int without an initializer'
$orders = Get-Body $spawner 'void\s+MonitorOrders\s*\(\s*EAC_AmbientModule\s+module\s*,\s*array<IEntity>\s+observers\s*\)'
Assert ((Count $orders 'm_MonitorTick\s*=\s*\(\s*m_MonitorTick\s*\+\s*1\s*\)\s*%\s*4\s*;') -eq 1) 'MonitorOrders must advance the pass counter once per pass, modulo 4'
Assert (Before $orders 'm_NextOrderMonitor\s*=\s*now\s*\+\s*0\.5' 'm_MonitorTick\s*=') 'the counter advances only on a pass that runs'
Assert ([regex]::IsMatch($orders, 'if\s*\(\s*monitored\s*&&\s*!FarTurn\(\s*activation\s*\)\s*&&\s*!NearObserver\(\s*observers\s*,\s*monitored\.GetOrigin\(\)\s*\)\s*\)\s*continue\s*;')) 'only a far resident with a character may skip a pass'
$farTurn = Get-Body $spawner 'protected\s+bool\s+FarTurn\s*\(\s*EAC_PedestrianActivation\s+activation\s*\)'
Assert-Enforce $farTurn 'FarTurn'
Assert ([regex]::IsMatch($farTurn, 'phase\s*=\s*activation\.Claim\.Resident\.Id\s*%\s*4\s*;') -and [regex]::IsMatch($farTurn, 'if\s*\(\s*phase\s*<\s*0\s*\)\s*phase\s*\+=\s*4\s*;') -and [regex]::IsMatch($farTurn, 'return\s+\(\s*phase\s*\+\s*m_MonitorTick\s*\)\s*%\s*4\s*==\s*0\s*;')) 'FarTurn must visit each far resident on one pass in four by id'
Assert ([regex]::IsMatch($spawner, 'if\s*\(\s*now\s*<\s*m_NextService\s*\)\s*return\s*;\s*m_NextService\s*=\s*now\s*\+\s*1\s*;') -and [regex]::IsMatch($spawner, 'if\s*\(\s*now\s*<\s*m_NextForce\s*\)\s*return\s*;\s*m_NextForce\s*=\s*now\s*\+\s*1\s*;')) 'ServiceBatch and ForceIdle keep their shared 1 Hz phase'

# ---------------------------------------------------------------------------------------
# civilians-b-12: observer verdict once per tick, capped neighbourhood count
# ---------------------------------------------------------------------------------------
$distant3 = Get-Body $caching 'static\s+bool\s+Distant\s*\(\s*EAC_AmbientModule\s+module\s*,\s*IEntity\s+actor\s*,\s*array<IEntity>\s+observers\s*\)'
Assert ([regex]::IsMatch($distant3, 'return\s+Distant\(\s*module\s*,\s*actor\s*,\s*observers\s*,\s*KnownObservers\(\s*module\.GetWorld\(\)\s*,\s*observers\s*\)\s*\)\s*;\s*$')) 'the 3-argument Distant must be the 4-argument one over KnownObservers'
$distant4 = Get-Body $caching 'static\s+bool\s+Distant\s*\(\s*EAC_AmbientModule\s+module\s*,\s*IEntity\s+actor\s*,\s*array<IEntity>\s+observers\s*,\s*bool\s+observersKnown\s*\)'
Assert-Enforce $distant4 'Distant(observersKnown)'
Assert (!(Count $distant4 'KnownObservers\(') -and [regex]::IsMatch($distant4, 'if\s*\(\s*!module\s*\|\|\s*!actor\s*\|\|\s*!observersKnown\s*\)\s*return\s+false\s*;')) 'the 4-argument Distant must take the verdict, not recompute it'
$step = Get-Body $spawner 'void\s+Step\s*\(\s*EAC_AmbientModule\s+module\s*,\s*array<IEntity>\s+observers\s*\)'
Assert ((Count $step 'KnownObservers\(') -eq 1 -and (Before $step 'bool\s+observersKnown\s*=\s*EAC_PedestrianCaching\.KnownObservers\(\s*module\.GetWorld\(\)\s*,\s*observers\s*\)\s*;' 'foreach\s*\(\s*EAC_PedestrianActivation\s+active\s*:\s*m_Tracked\s*\)')) 'Step must evaluate KnownObservers once, before the sampler loop'
Assert ([regex]::IsMatch($step, 'EAC_PedestrianCaching\.Distant\(\s*module\s*,\s*active\.Claim\.Character\s*,\s*observers\s*,\s*observersKnown\s*\)')) 'the sampler must pass the tick verdict'
$count = Get-Body $spawner 'int\s+CountLocalOccupants\s*\(\s*EAC_AmbientModule\s+module\s*,\s*vector\s+position\s*,\s*int\s+stopAt\s*=\s*-1\s*\)'
Assert-Enforce $count 'CountLocalOccupants'
Assert ((Count $count 'if\s*\(\s*stopAt\s*>=\s*0\s*&&\s*used\s*>=\s*stopAt\s*\)\s*return\s+used\s*;') -eq 2) 'CountLocalOccupants must stop only once the count reaches stopAt'
$select = Get-Body $spawner 'void\s+SelectNext\s*\(\s*EAC_AmbientModule\s+module\s*,\s*array<IEntity>\s+observers\s*\)'
# 0.1.18 (several modules): the neighbourhood ceiling and floor are those of the module whose
# area holds the household (areaModule); the debug switch stays the coordinator's (module).
Assert ([regex]::IsMatch($select, 'int\s+localStop\s*=\s*areaModule\.LocalPopulationLimit\s*;\s*if\s*\(\s*areaModule\.MinLocalPopulation\s*>\s*localStop\s*\)\s*localStop\s*=\s*areaModule\.MinLocalPopulation\s*;\s*localStop\+\+\s*;\s*if\s*\(\s*module\.DebugLevel\s*>\s*0\s*\)\s*localStop\s*=\s*-1\s*;\s*int\s+localUsed\s*=\s*CountLocalOccupants\(\s*areaModule\s*,\s*neighbourhood\s*,\s*localStop\s*\)\s*;')) 'SelectNext must stop counting one past the larger of the ceiling and the floor (exact count with debug on, for the local_floor summary)'
Assert ([regex]::IsMatch($select, 'HasLocalCapacityFor\(\s*areaModule\s*,\s*localUsed\s*,\s*1\s*\)') -and [regex]::IsMatch($select, 'IsBelowLocalFloor\(\s*areaModule\s*,\s*localUsed\s*\)')) 'the capped count must feed only the ceiling and the floor'
Assert ([regex]::IsMatch((Get-Body $spawner 'bool\s+HasLocalCapacity\s*\(\s*EAC_AmbientModule\s+module\s*,\s*vector\s+position\s*,\s*int\s+needed\s*=\s*1\s*\)'), 'int\s+used\s*=\s*CountLocalOccupants\(\s*module\s*,\s*position\s*\)\s*;')) "Begin's recount (HasLocalCapacity) must stay uncapped"
Assert ([regex]::IsMatch((Get-Body $spawner 'static\s+bool\s+HasLocalCapacityFor\s*\('), 'return\s+used\s*\+\s*needed\s*<=\s*module\.LocalPopulationLimit\s*;') -and [regex]::IsMatch((Get-Body $spawner 'static\s+bool\s+IsBelowLocalFloor\s*\('), 'return\s+used\s*<\s*module\.MinLocalPopulation\s*;')) 'the ceiling and floor the cap is proved against must be unchanged'

# ---------------------------------------------------------------------------------------
# Lazy statics: no new static initializers in these files.
# ---------------------------------------------------------------------------------------
foreach ($name in $files) {
 Assert (!(Count $code[$name] 'static\s+ref\s+[^;=]+=\s*(new|\{)') -and !(Count $code[$name] 'static\s+const\s+array<')) "$name must not add static initializers"
}

# ---------------------------------------------------------------------------------------
# Native equivalence fixture wiring (not run here).
# ---------------------------------------------------------------------------------------
$fixturePath = Join-Path $PSScriptRoot 'EAC_RouteEquivalenceTest.c'
Assert (Test-Path -LiteralPath $fixturePath -PathType Leaf) 'tests/EAC_RouteEquivalenceTest.c must exist'
$fixtureRaw = [IO.File]::ReadAllText($fixturePath)
$fixture = Read-Code $fixturePath
Assert ([regex]::IsMatch($fixtureRaw, "(?m)^// pwsh -File tests/Run-Gameplay\.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAC_RouteEquivalenceTest\.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\\\[EAC ROUTE RESULT\\\] checks=\[1-9\]\\d\* failures=0 routes=\[1-9\]\\d\* differences=0 reason=complete'")) 'the fixture header must carry its runner command and expected result'
$expect = [regex]::Match($fixtureRaw, "-ExpectResult '([^']+)'").Groups[1].Value
Assert ([regex]::IsMatch('[EAC ROUTE RESULT] checks=14 failures=0 routes=10901 differences=0 reason=complete', $expect)) 'the expected result must accept a passing line'
Assert (![regex]::IsMatch('[EAC ROUTE RESULT] checks=14 failures=0 routes=10901 differences=3 reason=complete', $expect)) 'the expected result must reject differences'
Assert ([regex]::IsMatch($fixture, 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') -and [regex]::IsMatch($fixture, 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass')) 'the runner copies the fixture to EXPG_GarrisonGameplay.c, so the driver class name is fixed'
Assert ([regex]::IsMatch($fixture, 'PrintFormat\("\[EAC ROUTE RESULT\] checks=%1 failures=%2 routes=%3 differences=%4 reason=%5"')) 'the fixture must print the result line the header expects'
$reference = Get-Body $fixture 'static\s+bool\s+ReferenceRouteAllowed\s*\(\s*vector\s+origin\s*,\s*array<vector>\s+route\s*\)'
Assert ([regex]::IsMatch($reference, '^\s*vector\s+previous\s*=\s*origin\s*;\s*foreach\s*\(\s*vector\s+point\s*:\s*route\s*\)\s*\{\s*if\s*\(\s*!EAC_ExclusionZone\.IsTransitAllowed\(\s*previous\s*,\s*point\s*\)\s*\)\s*return\s+false\s*;\s*previous\s*=\s*point\s*;\s*\}\s*return\s+true\s*;\s*$')) 'the reference must be the old per-segment loop'
Assert ([regex]::IsMatch((Get-Body $fixture 'void\s+Compare\s*\(\s*vector\s+origin\s*,\s*string\s+label\s*\)'), 'bool\s+expected\s*=\s*ReferenceRouteAllowed\(\s*origin\s*,\s*m_Route\s*\)\s*;\s*bool\s+actual\s*=\s*EAC_ExclusionZone\.IsRouteAllowed\(\s*origin\s*,\s*m_Route\s*\)\s*;')) 'the fixture must compare the reference with IsRouteAllowed on the same route'
$random = 0
foreach ($name in 'NODISC_ROUTES', 'DISC_ROUTES', 'NOMODULE_ROUTES') { $random += [int][regex]::Match($fixture, "static\s+const\s+int\s+$name\s*=\s*(\d+)\s*;").Groups[1].Value }
Assert ($random -ge 10000) "the fixture must compare at least 10000 random routes ($random)"
Assert ([regex]::IsMatch($fixture, 'int\s+count\s*=\s*Math\.RandomInt\(\s*0\s*,\s*65\s*\)\s*;')) 'random routes must have 0-64 points'
Assert ([regex]::IsMatch($fixture, 'EAC_FixtureSetAreas\(\s*m_Centres\s*,\s*m_Radii\s*,\s*false\s*\)') -and [regex]::IsMatch($fixture, 'Check\(\s*m_CaseRefused\s*==\s*m_CaseNonEmpty')) 'the fixture must cover the not-ready case and require every non-empty route refused'
Assert ([regex]::IsMatch($fixture, 'Check\(\s*m_Differences\s*==\s*0')) 'the fixture must require zero differences'
Assert (!(Count $fixture 'Math\.RandomFloat') -and !(Count $fixture 'Math\.RandomInt\(\s*\d+\s*,\s*(3276[89]|327[7-9]\d|32[89]\d\d|3[3-9]\d{3}|[4-9]\d{4}|\d{6,})\s*\)')) 'the fixture must draw only RandomInt up to 32767'
Assert-Enforce $fixture 'EAC_RouteEquivalenceTest.c'

'PASS: WP6 civilian pedestrian performance guards (one route verdict, Danger-to-Shelter proof hand-off, cached components, distance-first visibility, staggered far sweep, capped neighbourhood count; equivalence fixture wired, not run). Native engines were not launched.'
