// TEST ONLY. EXPBG Ambient Destruction (addon/ambient-destruction) native server fixture:
// road wreck layout spread, footprint overlap, solid ground, snapshot round trip and
// seed reproducibility.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAD_RoadWrecksGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[EXPG EAD ROAD RESULT\] checks=[1-9]\d* failures=0 wrecks=([6-9]|[1-9]\d+) reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// Accept on exactly one passing result line, no script errors and "Game destroyed".
// A real EAD_Zone (production prefab, scheduler and generator) in Morton, GM_Eden, at the
// origin and radius of the 0.1.7 live report: Wrecks 100, Bodies 0, Destruction 0, so the
// layout holds road wrecks only and no building changes between generations. Road
// offsets and headings are measured against the native road network independently of
// the production helpers. No players, no GM UI, no CDF: the snapshot check uses the
// base-game EAD_Snapshot format that the CDF companion stores verbatim.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const float PHASE_SECONDS = 90;
 static const ResourceName ZONE_PREFAB = "{EAD1000000000010}PrefabsEditable/EXPAD/EAD_Zone.et";
 static const vector ZONE_POINT = "4981.42 0 4026.65";
 static const int SEED_A = 472685;
 static const int SEED_B = 91;
 int m_Phase;
 int m_Checks;
 int m_Failures;
 int m_Wrecks;
 int m_Revision;
 float m_Started;
 float m_PhaseAt;
 float m_PhaseStarted;
 bool m_Finished;
 EAD_Zone m_Zone;
 ref array<ref EAD_PropRecord> m_First = {};
 ref array<ref EAD_PropRecord> m_Other = {};

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  m_Started = Now();
  m_PhaseAt = m_Started + 5;
  m_PhaseStarted = m_PhaseAt;
  PrintFormat("[EXPG EAD ROAD BEGIN] deadline=%1 zone=%2 seedA=%3 seedB=%4", FIXTURE_SECONDS, ZONE_POINT, SEED_A, SEED_B);
 }

 bool Check(bool ok, string label)
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[EXPG EAD ROAD CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (m_Finished) return;
  m_Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG EAD ROAD RESULT] checks=%1 failures=%2 wrecks=%3 reason=%4", m_Checks, m_Failures, m_Wrecks, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  m_Phase = phase;
  m_PhaseAt = Now();
  m_PhaseStarted = m_PhaseAt;
 }

 IEntity SpawnAt(ResourceName prefab, vector position)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = position;
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_Finished) return;
  float now = Now();
  if (now - m_Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", m_Phase)); Finish("timeout"); return; }
  if (now < m_PhaseAt) return;
  if (m_Phase > 0 && now - m_PhaseStarted > PHASE_SECONDS) { Check(false, string.Format("phase %1 finished within %2 s", m_Phase, PHASE_SECONDS)); Finish("phase-timeout"); return; }
  if (m_Phase == 0) { Setup(); return; }
  if (m_Phase == 1) { FirstLayout(); return; }
  if (m_Phase == 2) { Inspect(); return; }
  if (m_Phase == 3) { OtherSeed(); return; }
  if (m_Phase == 4) { SameSeed(); return; }
 }

 bool LayoutReady()
 {
  if (!m_Zone) return false;
  return m_Zone.GetRevision() != m_Revision && m_Zone.IsReady();
 }

 int LiveProps()
 {
  int live = 0;
  foreach (IEntity entity : m_Zone.Live)
  {
   if (entity) live++;
  }
  return live;
 }

 void CopyRecords(array<ref EAD_PropRecord> into)
 {
  into.Clear();
  foreach (EAD_PropRecord source : m_Zone.Records)
  {
   EAD_PropRecord copy = new EAD_PropRecord();
   copy.Asset = source.Asset;
   copy.Suppressed = source.Suppressed;
   for (int axis = 0; axis < 4; axis++) copy.Transform[axis] = source.Transform[axis];
   into.Insert(copy);
  }
 }

 // Index of the first differing record, -1 when both layouts match within tolerance.
 int FirstDifference(array<ref EAD_PropRecord> a, array<ref EAD_PropRecord> b, float position, float rotation)
 {
  int shorter = a.Count();
  if (b.Count() < shorter) shorter = b.Count();
  if (a.Count() != b.Count()) return shorter;
  for (int index = 0; index < a.Count(); index++)
  {
   if (a[index].Asset != b[index].Asset) return index;
   for (int axis = 0; axis < 4; axis++)
   {
    float tolerance = rotation;
    if (axis == 3) tolerance = position;
    if (vector.Distance(a[index].Transform[axis], b[index].Transform[axis]) > tolerance) return index;
   }
  }
  return -1;
 }

 void Setup()
 {
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  bool network = ai != null && ai.GetRoadNetworkManager() != null;
  if (!Check(network, "GM_Eden AI world has a road network")) { Finish("roads"); return; }
  BaseRoad road;
  float distance = 1000;
  ai.GetRoadNetworkManager().GetClosestRoad(ZONE_POINT, road, distance, true);
  if (!Check(road != null && distance <= 90, string.Format("a road within 90 m of the zone centre (%1 m)", distance))) { Finish("roads"); return; }
  vector origin = ZONE_POINT;
  origin[1] = GetGame().GetWorld().GetSurfaceY(origin[0], origin[2]);
  m_Zone = EAD_Zone.Cast(SpawnAt(ZONE_PREFAB, origin));
  if (!Check(m_Zone != null, "production EAD_Zone prefab spawned")) { Finish("zone"); return; }
  m_Zone.SetSetting(9, 1);
  m_Zone.SetSetting(0, 100);
  m_Zone.SetSetting(1, 0);
  m_Zone.SetSetting(3, 100);
  m_Zone.SetSetting(4, 0);
  m_Zone.SetSetting(7, SEED_A);
  m_Revision = m_Zone.GetRevision();
  m_Zone.SetSetting(8, 1);
  Advance(1);
 }

 void FirstLayout()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_First);
  m_Revision = m_Zone.GetRevision();
  // Cache the props so ground traces meet only the world, never a live wreck.
  m_Zone.SetSetting(8, 0);
  Advance(2);
 }

 void Inspect()
 {
  if (LiveProps() > 0) return;
  Analyse();
  RoundTrip();
  m_Zone.SetSetting(7, SEED_B);
  m_Zone.SetSetting(8, 1);
  Advance(3);
 }

 void OtherSeed()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_Other);
  int difference = FirstDifference(m_First, m_Other, 1, 0.05);
  Check(difference >= 0, string.Format("another seed gives another layout (%1 vs %2 records, first difference %3)", m_Other.Count(), m_First.Count(), difference));
  m_Revision = m_Zone.GetRevision();
  m_Zone.SetSetting(7, SEED_A);
  Advance(4);
 }

 void SameSeed()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_Other);
  int difference = FirstDifference(m_First, m_Other, 0.001, 0.001);
  if (difference >= 0 && difference < m_First.Count() && difference < m_Other.Count())
   PrintFormat("[EXPG EAD ROAD REPLAY] index=%1 first=%2 asset=%3 again=%4 asset=%5", difference, m_First[difference].Transform[3], m_First[difference].Asset, m_Other[difference].Transform[3], m_Other[difference].Asset);
  Check(difference < 0, string.Format("the same seed regenerates the identical layout (%1 vs %2 records, first difference %3)", m_Other.Count(), m_First.Count(), difference));
  Finish("complete");
 }

 // Independent measurement: closest native road, signed offset from its centreline
 // (right of the polyline direction is positive), that segment's direction, half width.
 bool RoadFrame(vector point, out float lateral, out vector along, out float half)
 {
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!ai || !ai.GetRoadNetworkManager()) return false;
  BaseRoad road;
  float distance = 0;
  ai.GetRoadNetworkManager().GetClosestRoad(point, road, distance, true);
  if (!road) return false;
  array<vector> points = {};
  road.GetPoints(points);
  float best = 1000000;
  bool found = false;
  vector closest = vector.Zero;
  for (int i = 1; i < points.Count(); i++)
  {
   vector delta = points[i] - points[i - 1];
   delta[1] = 0;
   float lengthSq = delta.LengthSq();
   if (lengthSq < 0.0001) continue;
   vector offset = point - points[i - 1];
   offset[1] = 0;
   float t = Math.Clamp(vector.Dot(offset, delta) / lengthSq, 0, 1);
   vector candidate = points[i - 1] + delta * t;
   vector gap = point - candidate;
   gap[1] = 0;
   float separation = gap.LengthSq();
   if (separation >= best) continue;
   best = separation;
   closest = candidate;
   along = delta * (1 / Math.Sqrt(lengthSq));
   found = true;
  }
  if (!found) return false;
  vector away = point - closest;
  away[1] = 0;
  lateral = vector.Dot(away, Vector(along[2], 0, -along[0]));
  half = road.GetWidth() * 0.5;
  return true;
 }

 // Footprint projection on a ground-plane axis: centre and half length.
 void Project(EAD_PropRecord record, vector axis, out float centre, out float radius)
 {
  vector mins, maxs;
  EAD_Catalog.Bounds(record.Asset, mins, maxs);
  vector right = record.Transform[0];
  right[1] = 0;
  vector forward = record.Transform[2];
  forward[1] = 0;
  vector middle = record.Transform[3] + right * ((mins[0] + maxs[0]) * 0.5) + forward * ((mins[2] + maxs[2]) * 0.5);
  middle[1] = 0;
  centre = vector.Dot(middle, axis);
  radius = (maxs[0] - mins[0]) * 0.5 * Math.AbsFloat(vector.Dot(right, axis)) + (maxs[2] - mins[2]) * 0.5 * Math.AbsFloat(vector.Dot(forward, axis));
 }

 // Separating-axis test of two oriented footprints (2 cm contact allowance).
 bool Overlap(EAD_PropRecord a, EAD_PropRecord b)
 {
  for (int test = 0; test < 4; test++)
  {
   vector axis = a.Transform[0];
   if (test == 1) axis = a.Transform[2];
   if (test == 2) axis = b.Transform[0];
   if (test == 3) axis = b.Transform[2];
   axis[1] = 0;
   if (axis.LengthSq() < 0.0001) continue;
   axis.Normalize();
   float centreA = 0;
   float radiusA = 0;
   float centreB = 0;
   float radiusB = 0;
   Project(a, axis, centreA, radiusA);
   Project(b, axis, centreB, radiusB);
   if (Math.AbsFloat(centreB - centreA) >= radiusA + radiusB - 0.02) return false;
  }
  return true;
 }

 // Centre and four inset corners: a real surface 1 m below a point that production
 // proved clear, within 0.35 m of the wreck's base, not a building, not water.
 bool OnSolidGround(BaseWorld world, EAD_PropRecord record, out float worst)
 {
  vector mins, maxs;
  EAD_Catalog.Bounds(record.Asset, mins, maxs);
  vector origin = record.Transform[3];
  float floorY = origin[1] + mins[1];
  worst = 0;
  for (int sample = 0; sample < 5; sample++)
  {
   float x = (mins[0] + maxs[0]) * 0.5;
   float z = (mins[2] + maxs[2]) * 0.5;
   if (sample > 0)
   {
    x = mins[0] + 0.2;
    if (sample >= 3) x = maxs[0] - 0.2;
    z = mins[2] + 0.2;
    if (sample == 2 || sample == 4) z = maxs[2] - 0.2;
   }
   vector point = origin + record.Transform[0] * x + record.Transform[2] * z;
   TraceParam trace = new TraceParam();
   trace.Start = Vector(point[0], floorY + 1, point[2]);
   trace.End = Vector(point[0], floorY - 1, point[2]);
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   float fraction = world.TraceMove(trace, null);
   if (fraction <= 0 || fraction >= 1) return false;
   if (trace.TraceEnt && SCR_DestructibleBuildingEntity.Cast(trace.TraceEnt)) return false;
   vector hit = trace.Start + (trace.End - trace.Start) * fraction;
   if (ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, hit - "0 0.05 0")) return false;
   float drop = Math.AbsFloat(hit[1] - floorY);
   worst = Math.Max(worst, drop);
   if (drop > 0.35) return false;
  }
  return true;
 }

 void Analyse()
 {
  BaseWorld world = GetGame().GetWorld();
  array<int> wrecks = {};
  for (int index = 0; index < m_First.Count(); index++)
  {
   if (EAD_Catalog.IsWreck(m_First[index].Asset)) wrecks.Insert(index);
  }
  m_Wrecks = wrecks.Count();
  Check(m_Wrecks == m_First.Count(), string.Format("Bodies 0 layout holds wrecks only (%1 of %2 records)", m_Wrecks, m_First.Count()));
  if (!Check(m_Wrecks >= 6, string.Format("at least six road wrecks placed (%1)", m_Wrecks))) return;
  array<float> skews = {};
  float lateralSum = 0;
  float lateralSq = 0;
  float skewSum = 0;
  int measured = 0;
  int centreline = 0;
  int aligned = 0;
  int crossways = 0;
  int against = 0;
  int offRoad = 0;
  int grounded = 0;
  foreach (int slot : wrecks)
  {
   EAD_PropRecord record = m_First[slot];
   float lateral = 0;
   float half = 0;
   vector along = vector.Zero;
   float worst = 0;
   bool solid = OnSolidGround(world, record, worst);
   if (solid) grounded++;
   if (!RoadFrame(record.Transform[3], lateral, along, half))
   {
    PrintFormat("[EXPG EAD ROAD WRECK] slot=%1 asset=%2 road=0 ground=%3 drop=%4", slot, record.Asset, solid, worst);
    continue;
   }
   measured++;
   vector forward = record.Transform[2];
   forward[1] = 0;
   float length = forward.Length();
   float skew = 90;
   if (length > 0.01)
   {
    float dot = vector.Dot(forward, along) / length;
    if (dot < 0) against++;
    skew = Math.Acos(Math.Clamp(Math.AbsFloat(dot), 0, 1)) * Math.RAD2DEG;
   }
   skews.Insert(skew);
   skewSum += skew;
   lateralSum += lateral;
   lateralSq += lateral * lateral;
   if (Math.AbsFloat(lateral) < 0.25) centreline++;
   if (skew < 2) aligned++;
   if (skew > 55) crossways++;
   float reach = Math.Max(half, 3) + EAD_Placement.ROAD_SHOULDER + 0.75;
   if (Math.AbsFloat(lateral) > reach) offRoad++;
   PrintFormat("[EXPG EAD ROAD WRECK] slot=%1 asset=%2 lateral=%3 half=%4 skew=%5 ground=%6 drop=%7", slot, record.Asset, lateral, half, skew, solid, worst);
  }
  if (!Check(measured * 10 >= m_Wrecks * 9, string.Format("native road frame measured for at least 90 percent of wrecks (%1/%2)", measured, m_Wrecks))) return;
  float lateralMean = lateralSum / measured;
  float lateralStd = Math.Sqrt(Math.Max(0, lateralSq / measured - lateralMean * lateralMean));
  float skewMean = skewSum / measured;
  skews.Sort();
  float skewMedian = skews[measured / 2];
  int overlaps = 0;
  float nearestSum = 0;
  float nearestSq = 0;
  int nearestCount = 0;
  for (int i = 0; i < wrecks.Count(); i++)
  {
   float nearest = 1000000;
   for (int j = 0; j < wrecks.Count(); j++)
   {
    if (i == j) continue;
    EAD_PropRecord first = m_First[wrecks[i]];
    EAD_PropRecord second = m_First[wrecks[j]];
    if (j > i && Overlap(first, second))
    {
     overlaps++;
     PrintFormat("[EXPG EAD ROAD OVERLAP] slot=%1 asset=%2 slot=%3 asset=%4", wrecks[i], first.Asset, wrecks[j], second.Asset);
    }
    nearest = Math.Min(nearest, Math.Sqrt(EAD_Policy.DistanceSq(first.Transform[3], second.Transform[3])));
   }
   if (nearest < 1000000)
   {
    nearestSum += nearest;
    nearestSq += nearest * nearest;
    nearestCount++;
   }
  }
  float spacingCv = 0;
  if (nearestCount >= 2 && nearestSum > 0)
  {
   float nearestMean = nearestSum / nearestCount;
   spacingCv = Math.Sqrt(Math.Max(0, nearestSq / nearestCount - nearestMean * nearestMean)) / nearestMean;
  }
  PrintFormat("[EXPG EAD ROAD LAYOUT] wrecks=%1 lateralStd=%2 centreline=%3 meanSkew=%4 medianSkew=%5 aligned=%6 crossways=%7 against=%8 spacingCv=%9", m_Wrecks, lateralStd, centreline, skewMean, skewMedian, aligned, crossways, against, spacingCv);
  Check(lateralStd > 0.8, string.Format("lateral offsets spread across the road (std %1 m > 0.8)", lateralStd));
  Check(centreline * 10 <= measured * 4, string.Format("at most 40 percent of wrecks on the centreline (%1/%2 within 0.25 m)", centreline, measured));
  Check(skewMean > 10, string.Format("headings skew off the road direction (mean %1 deg > 10)", skewMean));
  Check(skewMedian > 8, string.Format("typical heading skews off the road direction (median %1 deg > 8)", skewMedian));
  Check(aligned * 4 <= measured, string.Format("at most a quarter of wrecks parallel to the road (%1/%2 within 2 deg)", aligned, measured));
  Check(offRoad == 0, string.Format("every wreck centre on the road or its shoulder (%1 beyond)", offRoad));
  Check(overlaps == 0, string.Format("no two wreck footprints overlap (%1 pairs)", overlaps));
  Check(grounded == m_Wrecks, string.Format("every wreck on solid ground, not water or a building (%1/%2)", grounded, m_Wrecks));
  Check(spacingCv > 0.15, string.Format("irregular spacing, no equal intervals (nearest-neighbour CV %1 > 0.15)", spacingCv));
 }

 // The exact transforms survive the base-game snapshot format that saves and CDF restore use.
 void RoundTrip()
 {
  string payload;
  bool written = EAD_Snapshot.WriteZone(m_Zone, payload);
  Check(written, "zone snapshot written from the ready layout");
  EAD_ZoneSnapshot data = EAD_Snapshot.ReadZone(payload);
  if (!Check(data != null, "zone snapshot reads back (orthonormal transforms, unique origins)")) return;
  int difference = FirstDifference(m_First, data.Records, 0.05, 0.002);
  Check(difference < 0, string.Format("snapshot keeps every wreck transform (%1 vs %2 records, first difference %3)", data.Records.Count(), m_First.Count(), difference));
 }
}
