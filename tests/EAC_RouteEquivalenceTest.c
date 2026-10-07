// TEST ONLY. EXPBG Ambient Civilians native scalar fixture for the 0.1.15 performance plan,
// WP6 item 1 (civilians-a-01): EAC_ExclusionZone.IsRouteAllowed against the per-segment
// loop it replaced in the walker, the activity approach and the shelter/emerge route.
// NOT RUN by the portable suite (tests/Test-PerfCivilians.ps1 checks its wiring only).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAC_RouteEquivalenceTest.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[EAC ROUTE RESULT\] checks=[1-9]\d* failures=0 routes=[1-9]\d* differences=0 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// The runner's -ExpectResult verdict accepts exactly one "[EAC ROUTE RESULT] ... failures=0
// ... differences=0 reason=complete" line, no script errors and "Game destroyed".
// Geometry is synthetic: automatic exclusion discs injected through a fixture-only seam,
// real production EAC_ExclusionZone entities (three transit-blocking, one population-only)
// and a real EAC_AmbientModule for the population-area half. Cases, each compared route by
// route with ReferenceRouteAllowed (the old loop, verbatim):
//  notready  automatic exclusions not ready (every non-empty route must be refused)
//  nodiscs   ready, no discs, zones and module
//  discs     ready, ten discs (radius 15-123 m), zones and module
//  edges     routes tangent to every disc and zone at -0.01 .. +2 m, and the area boundary
//  nomodule  the module deleted: no area half, the edge routes again, then discs and zones
// EBG_DebugChecks is enabled throughout, so IsRouteAllowed's own cross-check must stay at 0.
// 10000 random routes of 0-64 points in total, plus the not-ready and edge routes.
modded class EAC_AutoExclusions
{
 // Fixture seam: replace the prepared discs for this world. `ready` false leaves them
 // not ready, the state in which every transit test refuses.
 static void EAC_FixtureSetAreas(array<vector> centres, array<float> radii, bool ready)
 {
  EXPBG_LazyStatics_EAC_AutoExclusions();
  s_World = GetGame().GetWorld();
  s_Centres.Clear();
  s_Radii.Clear();
  for (int i = 0; i < centres.Count(); i++)
  {
   s_Centres.Insert(centres[i]);
   s_Radii.Insert(radii[i]);
  }
  s_Complete = ready;
  s_Failed = false;
 }
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const int ROUTES_PER_FRAME = 250;
 static const int NOTREADY_ROUTES = 300;
 static const int NODISC_ROUTES = 2500;
 static const int DISC_ROUTES = 5000;
 static const int NOMODULE_ROUTES = 2500;
 static const int SETTLEMENT_RADIUS = 450;
 static const ResourceName MODULE_PREFAB = "{CA1A000000000010}PrefabsEditable/EXPAC/EAC_AmbientModule.et";
 static const ResourceName ZONE_PREFAB = "{CA1A000000000020}PrefabsEditable/EXPAC/EAC_ExclusionZone.et";
 int m_Phase;
 int m_Checks;
 int m_Failures;
 int m_Routes;
 int m_Differences;
 int m_Allowed;
 int m_Refused;
 int m_BoxClear;
 int m_CaseRoutes;
 int m_CaseRefused;
 int m_CaseNonEmpty;
 float m_Started;
 float m_PhaseAt;
 bool m_Finished;
 vector m_Centre;
 EAC_AmbientModule m_Module;
 ref array<EAC_ExclusionZone> m_Zones = {};
 ref array<vector> m_Centres = {};
 ref array<float> m_Radii = {};
 ref array<vector> m_NoCentres = {};
 ref array<float> m_NoRadii = {};
 ref array<vector> m_Route = {};

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Math.Randomize(20261007);
  // IsRouteAllowed's own debug cross-check runs the old loop too; it must agree.
  EBG_DebugChecks.Enabled = true;
  EBG_DebugChecks.Mismatches = 0;
  m_Started = Now();
  m_PhaseAt = m_Started + 3;
  m_Centre = GetOrigin();
  PrintFormat("[EAC ROUTE BEGIN] deadline=%1 centre=%2", FIXTURE_SECONDS, m_Centre);
 }

 bool Check(bool ok, string label)
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[EAC ROUTE CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (m_Finished) return;
  m_Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EAC ROUTE RESULT] checks=%1 failures=%2 routes=%3 differences=%4 reason=%5", m_Checks, m_Failures, m_Routes, m_Differences, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase, float delay = 0)
 {
  m_Phase = phase;
  m_PhaseAt = Now() + delay;
  m_CaseRoutes = 0;
  m_CaseRefused = 0;
  m_CaseNonEmpty = 0;
 }

 IEntity SpawnAt(ResourceName prefab, vector position)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid())
   return null;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = position;
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }

 // Uniform in [low, high] from RandomInt (never above 32767).
 float Draw(float low, float high)
 {
  float unit = Math.RandomInt(0, 32767);
  unit = unit / 32766;
  return low + (high - low) * unit;
 }

 // The per-segment loop IsRouteAllowed replaced (EAC_CivilianShelter.RouteAllowed's,
 // the same verdict as the walker's and the activity approach's).
 static bool ReferenceRouteAllowed(vector origin, array<vector> route)
 {
  vector previous = origin;
  foreach (vector point : route)
  {
   if (!EAC_ExclusionZone.IsTransitAllowed(previous, point))
    return false;
   previous = point;
  }
  return true;
 }

 // Whether IsRouteAllowed's bounding-box pre-test lets this route skip the per-segment
 // test, so the result line can show both paths were exercised.
 bool BoxClear(vector origin)
 {
  if (m_Route.IsEmpty())
   return false;
  float minX = origin[0];
  float maxX = origin[0];
  float minZ = origin[2];
  float maxZ = origin[2];
  foreach (vector corner : m_Route)
  {
   if (corner[0] < minX) minX = corner[0];
   if (corner[0] > maxX) maxX = corner[0];
   if (corner[2] < minZ) minZ = corner[2];
   if (corner[2] > maxZ) maxZ = corner[2];
  }
  if (EAC_AutoExclusions.AnyDiscTouchesBox(minX, minZ, maxX, maxZ))
   return false;
  return !EAC_ExclusionZone.AnyBlockingZoneTouchesBox(minX, minZ, maxX, maxZ);
 }

 void Compare(vector origin, string label)
 {
  bool expected = ReferenceRouteAllowed(origin, m_Route);
  bool actual = EAC_ExclusionZone.IsRouteAllowed(origin, m_Route);
  m_Routes++;
  m_CaseRoutes++;
  if (!m_Route.IsEmpty()) m_CaseNonEmpty++;
  if (expected) m_Allowed++;
  else
  {
   m_Refused++;
   m_CaseRefused++;
  }
  if (BoxClear(origin)) m_BoxClear++;
  if (expected == actual) return;
  m_Differences++;
  if (m_Differences <= 5) PrintFormat("[EAC ROUTE DIFFERENCE] case=%1 origin=%2 points=%3 first=%4 reference=%5 candidate=%6", label, origin, m_Route.Count(), FirstPoint(), expected, actual);
 }

 vector FirstPoint()
 {
  if (m_Route.IsEmpty())
   return vector.Zero;
  return m_Route[0];
 }

 // 0-64 points from a random origin within 600 m; one step in sixteen repeats the
 // previous point (a zero-length segment).
 vector RandomRoute()
 {
  m_Route.Clear();
  vector origin = m_Centre + Vector(Draw(-600, 600), 0, Draw(-600, 600));
  int count = Math.RandomInt(0, 65);
  vector point = origin;
  for (int i = 0; i < count; i++)
  {
   if (Math.RandomInt(0, 16) != 0) point = point + Vector(Draw(-40, 40), 0, Draw(-40, 40));
   m_Route.Insert(point);
  }
  return origin;
 }

 void RandomBatch(string label, int target)
 {
  for (int n = 0; n < ROUTES_PER_FRAME && m_CaseRoutes < target; n++)
  {
   vector origin = RandomRoute();
   Compare(origin, label);
  }
 }

 // A two-point and a kinked three-point route along the vertical line x = centre + reach.
 void Tangent(vector centre, float reach, string label)
 {
  vector start = centre + Vector(reach, 0, -30);
  m_Route.Clear();
  m_Route.Insert(centre + Vector(reach, 0, 30));
  Compare(start, label);
  m_Route.Clear();
  m_Route.Insert(centre + Vector(reach, 0, 0));
  m_Route.Insert(centre + Vector(reach + 20, 0, 25));
  Compare(start, label);
 }

 void EdgeRoutes()
 {
  array<float> offsets = {};
  offsets.Insert(-0.01); offsets.Insert(0); offsets.Insert(0.0005); offsets.Insert(0.01);
  offsets.Insert(0.5); offsets.Insert(0.99); offsets.Insert(1.01); offsets.Insert(2);
  for (int disc = 0; disc < m_Centres.Count(); disc++)
  {
   foreach (float offset : offsets) Tangent(m_Centres[disc], m_Radii[disc] + offset, "edges-disc");
  }
  foreach (EAC_ExclusionZone zone : m_Zones)
  {
   if (!zone) continue;
   float zoneRadius = zone.RadiusMeters;
   foreach (float zoneOffset : offsets) Tangent(zone.GetOrigin(), zoneRadius + zoneOffset, "edges-zone");
  }
  float area = SETTLEMENT_RADIUS;
  foreach (float areaOffset : offsets) Tangent(m_Centre, area + areaOffset, "edges-area");
  m_Route.Clear();
  Compare(m_Centre, "edges-empty");
 }

 void BuildDiscs()
 {
  m_Centres.Clear();
  m_Radii.Clear();
  for (int i = 0; i < 10; i++)
  {
   m_Centres.Insert(m_Centre + Vector(Draw(-500, 500), 0, Draw(-500, 500)));
   float radius = 15 + i * 12;
   m_Radii.Insert(radius);
  }
 }

 void SpawnZone(vector offset, int radius, int block)
 {
  EAC_ExclusionZone zone = EAC_ExclusionZone.Cast(SpawnAt(ZONE_PREFAB, m_Centre + offset));
  if (!zone) return;
  zone.SetSetting(0, radius);
  zone.SetSetting(1, block);
  m_Zones.Insert(zone);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_Finished) return;
  float now = Now();
  if (now - m_Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", m_Phase)); Finish("timeout"); return; }
  if (now < m_PhaseAt) return;
  if (m_Phase == 0)
  {
   m_Module = EAC_AmbientModule.Cast(SpawnAt(MODULE_PREFAB, m_Centre));
   if (!Check(m_Module != null, "Ambient Civilians module spawned")) { Finish("module"); return; }
   m_Module.SettlementRadius = SETTLEMENT_RADIUS;
   SpawnZone("200 0 -150", 15, 1);
   SpawnZone("-250 0 220", 60, 1);
   SpawnZone("50 0 300", 150, 1);
   SpawnZone("-100 0 -320", 100, 0);
   Check(m_Zones.Count() == 4, string.Format("four production exclusion zones spawned (%1)", m_Zones.Count()));
   BuildDiscs();
   Advance(1, 1);
   return;
  }
  if (m_Phase == 1)
  {
   Check(EAC_AmbientModule.GetActive() == m_Module, "the spawned module is the active one");
   bool registered = true;
   foreach (EAC_ExclusionZone zone : m_Zones)
   {
    if (!zone || !EAC_ExclusionZone.IsManualPopulationExcluded(zone.GetOrigin())) registered = false;
   }
   Check(registered, "every zone is registered with the exclusion predicates");
   EAC_AutoExclusions.EAC_FixtureSetAreas(m_Centres, m_Radii, false);
   for (int n = 0; n < NOTREADY_ROUTES; n++)
   {
    vector origin = RandomRoute();
    Compare(origin, "notready");
   }
   m_Route.Clear();
   Compare(m_Centre, "notready-empty");
   Check(m_CaseRefused == m_CaseNonEmpty, string.Format("not ready: every non-empty route refused (%1/%2)", m_CaseRefused, m_CaseNonEmpty));
   Advance(2);
   return;
  }
  if (m_Phase == 2)
  {
   EAC_AutoExclusions.EAC_FixtureSetAreas(m_NoCentres, m_NoRadii, true);
   RandomBatch("nodiscs", NODISC_ROUTES);
   if (m_CaseRoutes >= NODISC_ROUTES) Advance(3);
   return;
  }
  if (m_Phase == 3)
  {
   EAC_AutoExclusions.EAC_FixtureSetAreas(m_Centres, m_Radii, true);
   RandomBatch("discs", DISC_ROUTES);
   if (m_CaseRoutes >= DISC_ROUTES) Advance(4);
   return;
  }
  if (m_Phase == 4)
  {
   EAC_AutoExclusions.EAC_FixtureSetAreas(m_Centres, m_Radii, true);
   EdgeRoutes();
   SCR_EntityHelper.DeleteEntityAndChildren(m_Module);
   Advance(5, 1);
   return;
  }
  if (m_Phase == 5)
  {
   Check(!EAC_AmbientModule.GetActive(), "no active module after deleting it");
   // The same edge routes with no area half, so the disc and zone edges decide.
   EAC_AutoExclusions.EAC_FixtureSetAreas(m_Centres, m_Radii, true);
   EdgeRoutes();
   Advance(6);
   return;
  }
  if (m_Phase == 6)
  {
   EAC_AutoExclusions.EAC_FixtureSetAreas(m_Centres, m_Radii, true);
   RandomBatch("nomodule", NOMODULE_ROUTES);
   if (m_CaseRoutes < NOMODULE_ROUTES) return;
   PrintFormat("[EAC ROUTE SUMMARY] routes=%1 allowed=%2 refused=%3 box_clear=%4 differences=%5 mismatches=%6", m_Routes, m_Allowed, m_Refused, m_BoxClear, m_Differences, EBG_DebugChecks.Mismatches);
   Check(m_Differences == 0, string.Format("IsRouteAllowed equals the per-segment loop on every route (%1 differences in %2)", m_Differences, m_Routes));
   Check(EBG_DebugChecks.Mismatches == 0, string.Format("IsRouteAllowed's debug cross-check reported no mismatch (%1)", EBG_DebugChecks.Mismatches));
   Check(m_Allowed > 0 && m_Refused > 0, string.Format("both verdicts exercised (allowed %1, refused %2)", m_Allowed, m_Refused));
   Check(m_BoxClear > 0 && m_BoxClear < m_Routes, string.Format("both the box pre-test and the per-segment path exercised (box clear %1 of %2)", m_BoxClear, m_Routes));
   Finish("complete");
  }
 }
}
