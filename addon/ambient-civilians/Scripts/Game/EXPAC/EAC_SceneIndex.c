// Verified routine positions, surveyed once per home ahead of demand.
//
// The system this replaces probed the world at the moment a resident wanted a
// position: up to sixteen acceptance ladders plus sixteen geometry traces inside
// a single tick, per admission. Measured across campaigns 12-24 that bought 0%
// of corner anchors, 2.5% of overhead and 9% of low edge. Surveying amortises
// the same ladders over one home and reuses the result.
//
// Nothing here reimplements a gate. Acceptance runs EAC_RoutineAnchors.Usable
// verbatim, so exclusion zones, road lanes, navmesh validity and body clearance
// keep exactly one definition. Stage 1 produces spots and publishes telemetry;
// it does not yet serve them to routines.
class EAC_SceneSpot
{
 IEntity Furniture;
 vector FurnitureTransform[4];
 vector PoseTransform[4];
 vector SeatSurface;
 vector Transform[4];
 int Kind;
 int AnimMask;
 int Source;        // 0 lattice, 1 prop, 2 real door pivot
 int OccupantId;    // resident Id holding it, 0 free
 float HeldUntil;   // self-healing lease
 float ReleasedAt;
 int Failures;
 int Uses;
}

class EAC_SceneFace
{
 ref array<ref EAC_SceneSpot> Spots = {};
}

class EAC_SceneRecord
{
 int HomeId;
 int Stamp;
 vector Centre;
 int State;          // 0 unknown, 1 surveying, 2 verified, 3 barren
 float SurveyedAt, RetryAt;
 int SpotCount, Deferred;
 ref array<ref EAC_SceneFace> Faces = {};
}

class EAC_SceneJob
{
 IEntity Home;
 EAC_SceneRecord Record;
 int Phase;          // 0 query, 1 doors, 2 props, 3 lattice, 4 done
 int Cursor;
 int TileWaits;
 ref array<ref EAC_ScenePropSample> Props = {};
 ref array<vector> Doors = {};
}

class EAC_SceneIndex
{
 static const int MAX_RECORDS = 192;
 static const int MAX_SPOTS_PER_FACE = 3;
 static const int MAX_SPOTS_PER_HOME = 10;
 static const int MAX_SPOTS_TOTAL = 1920;   // exactly MAX_RECORDS * MAX_SPOTS_PER_HOME
 static const int MAX_PROPS_PER_HOME = 10;
 // Real seats keep their own per-home budget. Sharing the ten general slots let a
 // furnished interior full of chairs evict every bench, well and sunshade around
 // the house, because the props are kept in query order.
 static const int MAX_SEAT_SAMPLES_PER_HOME = 4;
 // Bounds dumps are for measuring seat height in one campaign, not for logging a
 // town. Sixty-four lines per mission is enough to measure every family.
 static const int MAX_SEAT_PRINTS = 64;
 static const int MAX_SCENE_CALLBACKS = 4096;
 static const int LATTICE_SAMPLES = 28;
 static const int MAX_TILE_WAITS = 4;
 static const int SURVEY_ADVANCES = 32;
 static const int FACES = 4;
 static const float PROP_MATCH = 1.6;
 static const float BARREN_RETRY = 900.0;
 // Records indexed by a 128 m cell so the itinerary's 140 m leash touches a
 // neighbourhood, not the map. 128 is chosen so the widest legal reach
 // (RoutineRange 160 - 20 = 140) spans at most two buckets in each direction.
 static const int RECORD_BUCKET = 128;
 // Hard cap so a pathological cluster cannot restore the old global cost.
 static const int MAX_BUCKET_RECORDS = 128;

 protected static BaseWorld s_World;
 protected static ref map<int, ref EAC_SceneRecord> s_Records = new map<int, ref EAC_SceneRecord>();
 protected static ref map<string, EAC_SceneSpot> s_FurnitureSlots = new map<string, EAC_SceneSpot>();
 protected static ref map<int, EAC_SceneSpot> s_HeldSeats = new map<int, EAC_SceneSpot>();
 protected static ref map<string, ref array<int>> s_RecordCells = new map<string, ref array<int>>();
 protected static ref EAC_SceneJob s_Job;
 protected static ref array<ref EAC_ScenePropSample> s_Collected;
 protected static ref array<vector> s_CollectedDoors;
 protected static int s_Callbacks, s_Stamp, s_SpotTotal, s_PeakCallbacks;
 protected static int s_Queries, s_Saturated, s_PropsFound, s_Samples, s_Traces;
 protected static int s_Deferred, s_BarrenHomes, s_Refused, s_NoPath, s_HomeFull, s_DoorsFound;
 protected static int s_Hit, s_Miss, s_RevalidateFail, s_Held, s_Substituted;
 protected static int s_SurveyCursor;
 protected static int s_PropSamples, s_SeatSamples;      // current home only
 protected static int s_RealSeats, s_SeatFull, s_SeatPrints, s_SurveyDebug;
 protected static ref array<int> s_KindSpots = {};

 static void CheckWorld()
 {
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World && s_KindSpots.Count() == EAC_RoutineAnchors.KINDS) return;
  s_World = world;
  // s_RecordCells must be cleared here, beside s_Records. Miss it and the bucket
  // map survives a world change holding ids that no longer resolve.
  s_Records.Clear(); s_RecordCells.Clear(); s_Job = null; s_Collected = null; s_CollectedDoors = null;
  s_FurnitureSlots.Clear(); s_HeldSeats.Clear();
  s_Callbacks = 0; s_Stamp = 0; s_SpotTotal = 0; s_PeakCallbacks = 0;
  s_Queries = 0; s_Saturated = 0; s_PropsFound = 0; s_Samples = 0; s_Traces = 0;
  s_Deferred = 0; s_BarrenHomes = 0; s_Refused = 0; s_NoPath = 0; s_HomeFull = 0; s_DoorsFound = 0;
  s_Hit = 0; s_Miss = 0; s_RevalidateFail = 0; s_Held = 0; s_Substituted = 0;
  s_SurveyCursor = 0;
  s_PropSamples = 0; s_SeatSamples = 0;
  s_RealSeats = 0; s_SeatFull = 0; s_SeatPrints = 0; s_SurveyDebug = 0;
  s_KindSpots.Clear(); s_KindSpots.Resize(EAC_RoutineAnchors.KINDS);
 }

 static int CountRecords() { CheckWorld(); return s_Records.Count(); }
 static int CountSpots() { CheckWorld(); return s_SpotTotal; }

 static EAC_SceneRecord Find(int homeId)
 {
  CheckWorld();
  return s_Records.Get(homeId);
 }

 // Native query callback. The bound lives here, where every entity passes,
 // rather than in the filter slot where rejections would never count it.
 protected static bool Collect(IEntity entity)
 {
  s_Callbacks++;
  if (s_Callbacks > s_PeakCallbacks) s_PeakCallbacks = s_Callbacks;
  if (s_Callbacks >= MAX_SCENE_CALLBACKS) { s_Saturated++; return false; }
  if (!entity) return true;
  BaseDoorComponent door = BaseDoorComponent.Cast(entity.FindComponent(BaseDoorComponent));
  if (door)
  {
   if (door.CanCharacterPass(0.6) && s_CollectedDoors && s_CollectedDoors.Count() < 8) s_CollectedDoors.Insert(door.GetDoorPivotPointWS());
   return true;
  }
  if (!s_Collected) return true;
  int props = s_PropSamples;
  int seats = s_SeatSamples;
  if (props >= MAX_PROPS_PER_HOME && seats >= MAX_SEAT_SAMPLES_PER_HOME) return true;
  EntityPrefabData data = entity.GetPrefabData();
  if (!data) return true;
  ResourceName prefab = data.GetPrefabName();
  int packed = EAC_SceneVocabulary.Classify(prefab);
  int kind = EAC_SceneVocabulary.KindOf(packed);
  if (kind < 0) return true;
  bool realSeat = kind == EAC_EAnchorKind.SEAT_REAL;
  if (realSeat && seats >= MAX_SEAT_SAMPLES_PER_HOME) { s_SeatFull++; return true; }
  if (!realSeat && props >= MAX_PROPS_PER_HOME) return true;
  // Capture geometry now; no entity handle is retained, so a prop that is later
  // destroyed cannot leave a dangling pointer. The position simply degrades.
  EAC_ScenePropSample sample = new EAC_ScenePropSample();
  if ((EAC_SceneVocabulary.MaskOf(packed) & (1 << EAC_ERoutineAnim.SEATED)) != 0) sample.Furniture = entity;
  entity.GetBounds(sample.Mins, sample.Maxs);
  entity.GetWorldTransform(sample.Transform);
  sample.Kind = kind;
  sample.Mask = EAC_SceneVocabulary.MaskOf(packed);
  sample.Spots = EAC_SceneVocabulary.SpotsOf(packed);
  s_Collected.Insert(sample);
  if (realSeat)
  {
   s_SeatSamples++;
   s_RealSeats++;
   DumpSeat(prefab, sample);
   return true;
  }
  s_PropSamples++;
  return true;
 }

 // Seat heights have to be measured before anything can sit on one, and the only
 // place the bounds exist is here, where the entity is already in hand. Debug
 // level 3 and a mission-wide print budget, so a town cannot flood the log.
 protected static void DumpSeat(ResourceName prefab, EAC_ScenePropSample sample)
 {
  int level = s_SurveyDebug;
  if (level < 3) return;
  if (s_SeatPrints >= MAX_SEAT_PRINTS) return;
  s_SeatPrints++;
  PrintFormat("[EAC scene seat] family=%1 prefab=%2 mins=%3 maxs=%4 origin=%5", EAC_SceneVocabulary.FamilyOf(prefab), prefab, sample.Mins, sample.Maxs, sample.Transform[3]);
 }

 // One cell index for one axis. Float division, never '%': the modulo operator
 // is not usable in a float expression in this compiler. Math.Floor returns a
 // float, so the narrowing to int happens once, here, in a named local.
 protected static int BucketIndex(float axis)
 {
  float cell = axis / RECORD_BUCKET;
  int index = Math.Floor(cell);
  return index;
 }

 // One spelling of a cell key. The insert side and the query side must never be
 // able to drift apart, so neither of them formats a key itself. One '+=' per
 // term: long '+' chains do not compile in this dialect.
 protected static string BucketKeyAt(int bx, int bz)
 {
  string key = bx.ToString();
  key += ":";
  key += bz.ToString();
  return key;
 }

 // Which 128 m bucket a world position belongs to.
 // Public because the engine-free fixture proves the boundary cases directly.
 static string BucketKey(vector position)
 {
  int bx = BucketIndex(position[0]);
  int bz = BucketIndex(position[2]);
  return BucketKeyAt(bx, bz);
 }

 // How many buckets in each direction a radius can reach. Clamped at two, which
 // is exactly what the widest legal leash (140 m) needs.
 static int BucketSpan(float radius)
 {
  int reach = Math.Ceil(radius * 1.0 / RECORD_BUCKET);
  return Math.Clamp(reach, 1, 2);
 }

 protected static void BucketInsert(int homeId, vector centre)
 {
  string key = BucketKey(centre);
  array<int> bucket = s_RecordCells.Get(key);
  if (!bucket)
  {
   array<int> created = {};
   s_RecordCells.Insert(key, created);
   bucket = created;
  }
  if (!bucket.Contains(homeId)) bucket.Insert(homeId);
 }

 // Called wherever s_Records.Remove is called, so a bucket never keeps an id the
 // record map has dropped.
 protected static void BucketRemove(int homeId, vector centre)
 {
  array<int> bucket = s_RecordCells.Get(BucketKey(centre));
  if (!bucket) return;
  int at = bucket.Find(homeId);
  if (at >= 0) bucket.Remove(at);
 }

 protected static EAC_SceneRecord Open(EAC_HouseholdRecord home, float now)
 {
  EAC_SceneRecord record = new EAC_SceneRecord();
  record.HomeId = home.Id;
  s_Stamp++;
  record.Stamp = s_Stamp;
  record.Centre = home.BuildingEntity.GetOrigin();
  record.State = 1;
  record.SurveyedAt = now;
  for (int face = 0; face < FACES; face++) record.Faces.Insert(new EAC_SceneFace());
  s_Records.Insert(home.Id, record);
  BucketInsert(home.Id, record.Centre);
  return record;
 }

 // Test-only seat for the engine-free equivalence proof in
 // tests/Enforce/EAC_RoutineTests.c. Open() cannot be reached without a real
 // EAC_HouseholdRecord and a building entity, so a fixture has no other way to
 // build a deterministic record set. It writes exactly what Open() writes and it
 // never replaces an existing record, so it can only ever add fixture rows beside
 // real ones; the fixture uses ids far above the registry's range and removes
 // every row it inserted through EAC_QARemoveRecord before it returns.
 static bool EAC_QAInsertRecord(int homeId, vector centre, int spotCount)
 {
  CheckWorld();
  if (homeId == 0 || s_Records.Contains(homeId)) return false;
  EAC_SceneRecord record = new EAC_SceneRecord();
  record.HomeId = homeId;
  record.Centre = centre;
  record.SpotCount = spotCount;
  record.State = 2;
  if (spotCount <= 0) record.State = 3;
  for (int face = 0; face < FACES; face++) record.Faces.Insert(new EAC_SceneFace());
  s_Records.Insert(homeId, record);
  BucketInsert(homeId, centre);
  return true;
 }

 // Test-only companion to EAC_QAInsertRecord. Gives a fixture record one spot and
 // hands it straight to `residentId`, so tests/Enforce/EAC_RoutineTests.c can
 // prove what Release and ReleaseAllFor leave behind without needing a surveyed
 // home, a building entity and a live pathfinding component. It deliberately
 // touches neither s_SpotTotal nor s_Held: this is a hand-built row rather than
 // surveyed supply, and EAC_QARemoveRecord drops the record whole.
 static bool EAC_QAHoldFixtureSpot(int homeId, int residentId, vector position)
 {
  CheckWorld();
  EAC_SceneRecord record = s_Records.Get(homeId);
  if (!record || record.Faces.IsEmpty() || residentId == 0) return false;
  EAC_SceneSpot spot = new EAC_SceneSpot();
  spot.Transform[0] = "1 0 0";
  spot.Transform[1] = "0 1 0";
  spot.Transform[2] = "0 0 1";
  spot.Transform[3] = position;
  spot.Kind = EAC_EAnchorKind.YARD_OPEN;
  spot.AnimMask = -1;
  spot.OccupantId = residentId;
  spot.HeldUntil = GetGame().GetWorld().GetWorldTime() * 0.001 + 600;
  record.Faces[0].Spots.Insert(spot);
  // Audit S10. The record's advertised spot count must follow the spots actually
  // on it, or a fixture record reports one spot while holding two and every
  // count read off it is wrong. s_SpotTotal is still deliberately untouched: this
  // is a hand-built row, not surveyed supply, and EAC_QARemoveRecord drops the
  // record whole without adjusting the mission total either.
  record.SpotCount++;
  return true;
 }

 // The latest anti-repeat stamp on any spot of a record. Read-only; -1 when the
 // record does not exist. Test-only, same seat as EAC_QAHoldFixtureSpot.
 static float EAC_QASpotReleasedAt(int homeId)
 {
  CheckWorld();
  EAC_SceneRecord record = s_Records.Get(homeId);
  if (!record) return -1;
  float latest = -1;
  foreach (EAC_SceneFace face : record.Faces)
   foreach (EAC_SceneSpot spot : face.Spots)
    if (spot.ReleasedAt > latest) latest = spot.ReleasedAt;
  return latest;
 }

 static void EAC_QARemoveRecord(int homeId)
 {
  CheckWorld();
  EAC_SceneRecord record = s_Records.Get(homeId);
  if (!record) return;
  BucketRemove(homeId, record.Centre);
  s_Records.Remove(homeId);
 }

 protected static EAC_SceneSpot Store(EAC_SceneRecord record, int faceIndex, int kind, int mask, int source, vector transform[4])
 {
  if (!record || record.SpotCount >= MAX_SPOTS_PER_HOME || s_SpotTotal >= MAX_SPOTS_TOTAL) return null;
  if (faceIndex < 0 || faceIndex >= record.Faces.Count()) faceIndex = 0;
  EAC_SceneFace face = record.Faces[faceIndex];
  if (face.Spots.Count() >= MAX_SPOTS_PER_FACE) return null;
  EAC_SceneSpot spot = new EAC_SceneSpot();
  for (int axis = 0; axis < 4; axis++) spot.Transform[axis] = transform[axis];
  spot.Kind = kind; spot.AnimMask = mask; spot.Source = source;
  face.Spots.Insert(spot);
  record.SpotCount++;
  s_SpotTotal++;
  if (kind >= 0 && kind < s_KindSpots.Count()) s_KindSpots[kind] = s_KindSpots[kind] + 1;
  return spot;
 }

 // Which of the four faces a world position belongs to, in the home's own basis.
 protected static int FaceOf(IEntity home, vector position)
 {
  vector local = home.CoordToLocal(position);
  if (Math.AbsFloat(local[0]) >= Math.AbsFloat(local[2]))
  {
   if (local[0] >= 0) return 1;
   return 3;
  }
  if (local[2] >= 0) return 0;
  return 2;
 }

 // A surveyed spot inherits a nearby classified prop's identity without spending
 // a single trace. This is the whole point of the design.
 protected static bool MatchProp(vector settled, out int kind, out int mask)
 {
  kind = -1; mask = 0;
  if (!s_Job) return false;
  foreach (EAC_ScenePropSample prop : s_Job.Props)
  {
   // A nearby point is not the seat itself. Furniture poses use their measured slots only.
   if ((prop.Mask & (1 << EAC_ERoutineAnim.SEATED)) != 0) continue;
   if (vector.DistanceXZ(prop.Transform[3], settled) > PROP_MATCH) continue;
   kind = prop.Kind; mask = prop.Mask;
   return true;
  }
  return false;
 }

 // Decide what a position is, cheapest discriminator first. At most eight traces,
 // and the survey chooses the facing rather than satisfying a fixed request, so
 // four low directions suffice where the request-time probe needed eight.
 protected static bool Discriminate(BaseWorld world, IEntity home, vector settled, vector outward, bool cornerSample, out int kind, out int mask, out vector transform[4])
 {
  kind = -1; mask = 0;
  Math3D.MatrixIdentity4(transform);
  vector chest = settled + Vector(0, 1.2, 0);
  IEntity cover;
  s_Traces++;
  if (EAC_RoutineAnchors.Above(world, settled, null, cover) && cover && cover != home)
  {
   kind = EAC_EAnchorKind.OVERHEAD;
   mask = EAC_SceneVocabulary.AnimMaskForKind(kind, false);
   EAC_RoutineAnchors.Face(-outward, settled, transform);
   return true;
  }
  vector low = settled + Vector(0, 0.5, 0);
  vector head = settled + Vector(0, 1.6, 0);
  IEntity blocker;
  for (int side = 0; side < 4; side++)
  {
   vector direction = Vector(Math.Cos(side * Math.PI / 2), 0, Math.Sin(side * Math.PI / 2));
   s_Traces++;
   if (EAC_RoutineAnchors.HitDistance(world, low, direction, 1.5, null, blocker) > 1.2 || blocker == home) continue;
   s_Traces++;
   if (!EAC_RoutineAnchors.Clear(world, head, direction, 1.4, null)) continue;
   kind = EAC_EAnchorKind.LOW_EDGE;
   mask = EAC_SceneVocabulary.AnimMaskForKind(kind, false);
   EAC_RoutineAnchors.Face(direction, settled, transform);
   return true;
  }
  s_Traces++;
  float wall = EAC_RoutineAnchors.HitDistance(world, chest, -outward, 1.8, null, blocker);
  if (wall >= 0.2 && wall <= 1.4 && blocker == home)
  {
   s_Traces++;
   if (EAC_RoutineAnchors.Clear(world, chest, outward, 2.0, null))
   {
    kind = EAC_EAnchorKind.WALL_BACK;
    mask = EAC_SceneVocabulary.AnimMaskForKind(kind, false);
    EAC_RoutineAnchors.Face(outward, settled, transform);
    return true;
   }
  }
  if (cornerSample)
  {
   vector inward = home.GetOrigin() - settled; inward[1] = 0;
   if (inward.LengthSq() > 0.0001)
   {
    inward = inward.Normalized();
    s_Traces++;
    if (EAC_RoutineAnchors.HitDistance(world, chest, inward, 4.0, null, blocker) <= 3.5 && blocker == home && EAC_RoutineAnchors.Clear(world, chest, outward, 1.8, null))
    {
     kind = EAC_EAnchorKind.CORNER;
     mask = EAC_SceneVocabulary.AnimMaskForKind(kind, false);
     EAC_RoutineAnchors.Face(outward, settled, transform);
     return true;
    }
   }
  }
  for (int quarter = 0; quarter < 4; quarter++)
  {
   vector open = Vector(Math.Cos(quarter * Math.PI / 2), 0, Math.Sin(quarter * Math.PI / 2));
   s_Traces++;
   if (!EAC_RoutineAnchors.Clear(world, chest, open, 2.0, null)) return false;
  }
  kind = EAC_EAnchorKind.YARD_OPEN;
  mask = EAC_SceneVocabulary.AnimMaskForKind(kind, false);
  EAC_RoutineAnchors.Face(-outward, settled, transform);
  return true;
 }

 // Deterministic lattice in the home's own basis. Skin and corners reuse the
 // exact candidate generators that turned wall=0/72 into a working anchor;
 // rings 1 and 2 are the yard and the outer ground, including behind the house.
 protected static bool LatticeCandidate(IEntity home, int sample, out vector candidate, out vector outward, out bool cornerSample)
 {
  cornerSample = false;
  if (sample < 8) return EAC_RoutineAnchors.FaceCandidate(home, sample, 0.7, candidate, outward);
  if (sample < 12)
  {
   cornerSample = true;
   return EAC_RoutineAnchors.CornerCandidate(home, sample - 8, 0.8, candidate, outward);
  }
  if (sample < 20) return EAC_RoutineAnchors.FaceCandidate(home, sample - 12, 4.0, candidate, outward);
  return EAC_RoutineAnchors.FaceCandidate(home, sample - 20, 12.0, candidate, outward);
 }

 protected static void Close(EAC_SceneRecord record, float now)
 {
  if (!record) return;
  if (record.SpotCount > 0) { record.State = 2; record.SurveyedAt = now; return; }
  // A record whose every sample hit a pending tile was never actually surveyed.
  // Marking it barren locks a whole neighbourhood out for BARREN_RETRY on the
  // strength of streaming latency, which is exactly the case on first entry to a
  // town. Discard it instead so it can be opened again on the next pass.
  if (record.Deferred > 0 && record.SpotCount == 0)
  {
   BucketRemove(record.HomeId, record.Centre);
   s_Records.Remove(record.HomeId);
   s_Deferred++;
   return;
  }
  record.State = 3; record.RetryAt = now + BARREN_RETRY; s_BarrenHomes++;
  record.SurveyedAt = now;
 }

 // One bounded unit of survey work per idle tick.
 static void Step(EAC_AmbientModule module, array<IEntity> observers, float now)
 {
  if (!Replication.IsServer() || !module || module.SceneSurvey <= 0) return;
  CheckWorld();
  if (s_Job) { Advance(module, now); return; }
  Begin(module, observers, now);
 }

 protected static void Begin(EAC_AmbientModule module, array<IEntity> observers, float now)
 {
  EAC_HomeIndex index = module.GetHomeIndex();
  if (!index) return;
  EAC_HouseholdRegistry registry = index.GetRegistry();
  if (!registry || registry.GetHomeCount() == 0) return;
  if (s_Records.Count() >= MAX_RECORDS || s_SpotTotal >= MAX_SPOTS_TOTAL) { s_Refused++; return; }
  for (int attempt = 0; attempt < SURVEY_ADVANCES; attempt++)
  {
   s_SurveyCursor = s_SurveyCursor % registry.GetHomeCount();
   EAC_HouseholdRecord home = registry.GetHome(s_SurveyCursor++);
   // A household the isolation policy declined has no residents, so surveying it
   // spends the record and spot budget on scenes nobody can ever occupy.
   if (!home || !home.BuildingEntity || home.Residents.IsEmpty()) continue;
   if (!module.ContainsPopulationPosition(home.BuildingEntity.GetOrigin())) continue;
   EAC_SceneRecord existing = s_Records.Get(home.Id);
   if (existing)
   {
    if (existing.State != 3 || now < existing.RetryAt) continue;
    BucketRemove(home.Id, existing.Centre);
    s_Records.Remove(home.Id);
    s_SpotTotal -= existing.SpotCount;
   }
   if (!Relevant(module, home.BuildingEntity.GetOrigin(), observers)) continue;
   s_Job = new EAC_SceneJob();
   s_Job.Home = home.BuildingEntity;
   s_Job.Record = Open(home, now);
   s_Job.Phase = 0;
   return;
  }
 }

 protected static bool Relevant(EAC_AmbientModule module, vector position, array<IEntity> observers)
 {
  if (!observers || observers.IsEmpty() || observers.Count() > 64) return false;
  foreach (IEntity observer : observers)
  {
   // One dropped observer in the list is not "nobody is near": skip it and keep
   // asking the others.
   if (!observer) continue;
   if (vector.Distance(observer.GetOrigin(), position) <= module.WakeDistance) return true;
  }
  return false;
 }

 protected static void Advance(EAC_AmbientModule module, float now)
 {
  if (!s_Job.Home || !s_Job.Record) { s_Job = null; return; }
  BaseWorld world = module.GetWorld();
  if (s_Job.Phase == 0)
  {
   s_Collected = {}; s_CollectedDoors = {}; s_Callbacks = 0;
   // Per-home budgets and the callback's view of the debug level, read once here
   // rather than per entity inside the query callback.
   s_PropSamples = 0; s_SeatSamples = 0;
   s_SurveyDebug = module.DebugLevel;
   vector mins, maxs; s_Job.Home.GetBounds(mins, maxs);
   float radius = Math.Clamp(Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]) * 0.5 + 16, 12, 45);
   world.QueryEntitiesBySphere(s_Job.Home.GetOrigin(), radius, Collect, null, EQueryEntitiesFlags.ALL);
   s_Queries++;
   s_Job.Props = s_Collected; s_Job.Doors = s_CollectedDoors;
   s_PropsFound += s_Job.Props.Count();
   s_DoorsFound += s_Job.Doors.Count();
   s_Collected = null; s_CollectedDoors = null;
   // One query, two caches: the door pivots this found spare EAC_DoorIndex its
   // own query for this home entirely.
   EAC_DoorIndex.Publish(s_Job.Home, s_Job.Doors);
   s_Job.Phase = 1; s_Job.Cursor = 0;
   return;
  }
  AIPathfindingComponent path;
  EAC_PedestrianSpawner spawner = module.GetSpawner();
  if (spawner) path = spawner.BorrowPathfinding();
  if (!path) { s_NoPath++; return; }
  int budget = module.SceneSurveyRate;
  for (int unit = 0; unit < budget; unit++)
  {
   if (!s_Job) return;
   if (s_Job.Record.SpotCount >= MAX_SPOTS_PER_HOME) { s_HomeFull++; Close(s_Job.Record, now); s_Job = null; return; }
   if (s_Job.Phase == 1) { DoorUnit(world, path, now); continue; }
   if (s_Job.Phase == 2) { PropUnit(world, path, now); continue; }
   if (s_Job.Phase == 3) { LatticeUnit(world, path, now); continue; }
   Close(s_Job.Record, now); s_Job = null; return;
  }
 }

 protected static void PropUnit(BaseWorld world, AIPathfindingComponent path, float now)
 {
  int total = 0;
  foreach (EAC_ScenePropSample prop : s_Job.Props) total += prop.Spots;
  if (s_Job.Cursor >= total) { s_Job.Phase = 3; s_Job.Cursor = 0; return; }
  int walked = 0;
  foreach (EAC_ScenePropSample candidate : s_Job.Props)
  {
   for (int index = 0; index < candidate.Spots; index++)
   {
    if (walked != s_Job.Cursor) { walked++; continue; }
    s_Job.Cursor++;
    s_Samples++;
    vector transform[4];
    vector pose[4]; vector surface;
    bool furniture = (candidate.Mask & (1 << EAC_ERoutineAnim.SEATED)) != 0;
    string seatKey;
    if (furniture)
    {
     if (!candidate.Furniture || !EAC_FurnitureSeat.Geometry(candidate.Furniture, index, transform, pose, surface)) return;
     seatKey = string.Format("%1/%2", candidate.Furniture.GetID(), index);
     // Neighbouring home surveys share physical seats, never duplicate their leases.
     if (s_FurnitureSlots.Contains(seatKey)) return;
    }
    else if (!EAC_SceneVocabulary.SpotTransform(candidate, index, world, s_Job.Home.GetOrigin(), transform)) return;
    vector settled; string reason;
    if (!EAC_RoutineAnchors.Usable(world, null, path, transform[3], settled, reason))
    {
     if (reason == "tile pending" && s_Job.TileWaits < MAX_TILE_WAITS) { s_Job.TileWaits++; s_Job.Record.Deferred++; s_Deferred++; s_Job.Cursor--; }
     else s_Job.TileWaits = 0;
     return;
    }
    s_Job.TileWaits = 0;
    vector spot[4];
    for (int axis = 0; axis < 4; axis++) spot[axis] = transform[axis];
    spot[3] = settled;
    EAC_SceneSpot stored = Store(s_Job.Record, FaceOf(s_Job.Home, settled), candidate.Kind, candidate.Mask, 1, spot);
    if (stored && furniture)
    {
     stored.Furniture = candidate.Furniture;
     Math3D.MatrixCopy(candidate.Transform, stored.FurnitureTransform);
     Math3D.MatrixCopy(pose, stored.PoseTransform);
     stored.SeatSurface = surface;
     s_FurnitureSlots.Insert(seatKey, stored);
    }
    return;
   }
  }
 }

 protected static void LatticeUnit(BaseWorld world, AIPathfindingComponent path, float now)
 {
  if (s_Job.Cursor >= LATTICE_SAMPLES) { Close(s_Job.Record, now); s_Job = null; return; }
  int sample = s_Job.Cursor;
  vector candidate, outward; bool cornerSample;
  if (!LatticeCandidate(s_Job.Home, sample, candidate, outward, cornerSample)) { s_Job.Cursor++; return; }
  candidate[1] = world.GetSurfaceY(candidate[0], candidate[2]) + 0.1;
  s_Samples++;
  vector settled; string reason;
  if (!EAC_RoutineAnchors.Usable(world, null, path, candidate, settled, reason))
  {
   // A streaming tile must not consume the sample, or a freshly approached town
   // closes at zero spots and permanently pays the full probe sweep instead.
   if (reason == "tile pending" && s_Job.TileWaits < MAX_TILE_WAITS) { s_Job.TileWaits++; s_Job.Record.Deferred++; s_Deferred++; return; }
   s_Job.TileWaits = 0; s_Job.Cursor++;
   return;
  }
  s_Job.TileWaits = 0; s_Job.Cursor++;
  int kind, mask;
  vector transform[4];
  if (MatchProp(settled, kind, mask))
  {
   EAC_RoutineAnchors.Face(-outward, settled, transform);
   Store(s_Job.Record, FaceOf(s_Job.Home, settled), kind, mask, 1, transform);
   return;
  }
  if (!Discriminate(world, s_Job.Home, settled, outward, cornerSample, kind, mask, transform)) return;
  Store(s_Job.Record, FaceOf(s_Job.Home, settled), kind, mask, 0, transform);
 }

 // Doorsteps come only from real door pivots, never from a bounding-box face.
 protected static void DoorUnit(BaseWorld world, AIPathfindingComponent path, float now)
 {
  if (!s_Job.Doors || s_Job.Cursor >= s_Job.Doors.Count() * 2) { s_Job.Phase = 2; s_Job.Cursor = 0; return; }
  int doorIndex = s_Job.Cursor / 2;
  int step = s_Job.Cursor % 2;
  s_Job.Cursor++;
  if (doorIndex >= s_Job.Doors.Count()) return;
  vector pivot = s_Job.Doors[doorIndex];
  vector away = pivot - s_Job.Home.GetOrigin(); away[1] = 0;
  if (away.LengthSq() < 0.0001) return;
  away = away.Normalized();
  float push = 1.6;
  if (step == 1) push = 2.4;
  vector candidate = pivot + away * push;
  candidate[1] = world.GetSurfaceY(candidate[0], candidate[2]) + 0.1;
  s_Samples++;
  vector settled; string reason;
  if (!EAC_RoutineAnchors.Usable(world, null, path, candidate, settled, reason))
  {
   if (reason == "tile pending" && s_Job.TileWaits < MAX_TILE_WAITS) { s_Job.TileWaits++; s_Job.Record.Deferred++; s_Deferred++; s_Job.Cursor--; }
   else s_Job.TileWaits = 0;
   return;
  }
  s_Job.TileWaits = 0;
  s_Traces++;
  if (EAC_PedestrianSpawner.InsideHouse(world, s_Job.Home, settled, null)) return;
  vector transform[4];
  EAC_RoutineAnchors.Face(away, settled, transform);
  Store(s_Job.Record, FaceOf(s_Job.Home, settled), EAC_EAnchorKind.DOORSTEP, EAC_SceneVocabulary.AnimMaskForKind(EAC_EAnchorKind.DOORSTEP, false), 2, transform);
 }

 // Volatile conditions only. Everything stable was settled at survey time; these
 // are the things a GM or another resident can change underneath a stored spot.
 protected static bool Revalidate(EAC_SceneSpot spot, BaseWorld world, AIPathfindingComponent path, IEntity actor)
 {
  if (!spot || !world || !path) return false;
  if ((spot.AnimMask & (1 << EAC_ERoutineAnim.SEATED)) != 0 && !EAC_FurnitureSeat.SurfaceClear(spot, actor)) return false;
  vector settled; string reason;
  if (!EAC_RoutineAnchors.Usable(world, actor, path, spot.Transform[3], settled, reason)) return false;
  if (vector.DistanceXZ(settled, spot.Transform[3]) > 1.0) return false;
  return true;
 }

 // Kinds that read as each other closely enough to stand in. A wall serves a
 // corner routine's lean; a bench serves a kerb routine. DOORSTEP is never
 // substituted - it has to be a real opening.
 protected static bool Substitutable(int wanted, int offered)
 {
  if (wanted == offered) return true;
  if (wanted == EAC_EAnchorKind.WALL_BACK && offered == EAC_EAnchorKind.CORNER) return true;
  if (wanted == EAC_EAnchorKind.CORNER && offered == EAC_EAnchorKind.WALL_BACK) return true;
  if (wanted == EAC_EAnchorKind.LOW_EDGE && offered == EAC_EAnchorKind.SEAT) return true;
  if (wanted == EAC_EAnchorKind.SEAT && offered == EAC_EAnchorKind.LOW_EDGE) return true;
  // A real seat is also a place to stand beside, and every position a real seat
  // produces today is exactly that: no routine asks for SEAT_REAL yet.
  if (wanted == EAC_EAnchorKind.SEAT && offered == EAC_EAnchorKind.SEAT_REAL) return true;
  if (wanted == EAC_EAnchorKind.SEAT_REAL && offered == EAC_EAnchorKind.SEAT) return true;
  if (wanted == EAC_EAnchorKind.OVERHEAD && offered == EAC_EAnchorKind.PROP_FACE) return true;
  if (wanted == EAC_EAnchorKind.PROP_FACE && offered == EAC_EAnchorKind.OVERHEAD) return true;
  return false;
 }

 // Records inside the leash, entered at a rotating offset so residents of one
 // household do not all walk to the same neighbour.
 //
 // This used to walk every record in the index on every call, twice, and
 // allocate an array sized to the whole map - once per leg start, forever. It
 // now visits only the buckets the radius can reach: at most (2*span+1)^2 = 25
 // buckets, capped at MAX_BUCKET_RECORDS examined records. The rotation, the
 // distance filter and the limit are unchanged, so the only records excluded are
 // ones that would have failed the distance test anyway.
 static void CollectWithin(vector centre, float radius, int offset, int limit, out array<EAC_SceneRecord> found)
 {
  found = {};
  CheckWorld();
  if (s_Records.Count() == 0) return;
  int span = BucketSpan(radius);
  // Exactly the expression BucketKey insert-side uses, through the same two
  // helpers, so a record can never be filed under a key this walk cannot build.
  int bx = BucketIndex(centre[0]);
  int bz = BucketIndex(centre[2]);
  array<EAC_SceneRecord> all = {};
  for (int dx = -span; dx <= span; dx++)
  {
   for (int dz = -span; dz <= span; dz++)
   {
    int keyX = bx + dx;
    int keyZ = bz + dz;
    string key = BucketKeyAt(keyX, keyZ);
    array<int> bucket = s_RecordCells.Get(key);
    if (!bucket) continue;
    foreach (int id : bucket)
    {
     if (all.Count() >= MAX_BUCKET_RECORDS) break;
     EAC_SceneRecord record = s_Records.Get(id);
     if (record && record.State == 2 && record.SpotCount > 0) all.Insert(record);
    }
   }
  }
  if (all.IsEmpty()) return;
  int start = offset % all.Count();
  if (start < 0) start = 0;
  for (int step = 0; step < all.Count(); step++)
  {
   if (found.Count() >= limit) return;
   int at = (start + step) % all.Count();
   EAC_SceneRecord candidate = all[at];
   // Hoisted: an instance read inside a class-qualified static call's argument
   // list is not safe in this dialect.
   vector candidateCentre = candidate.Centre;
   if (vector.DistanceXZ(candidateCentre, centre) > radius) continue;
   found.Insert(candidate);
  }
 }

 // Hand a resident a verified position inside one surveyed record. Exact kind
 // first, then a close substitute, then anything that supports the animation.
 // Audit S11. The faceOut/spotOut out-parameters are gone: their only consumer
 // was EAC_SceneItinerary.Commit, writing them to two record fields nothing read.
 // Which spot a resident holds is owned here, by spot.OccupantId.
 static bool ReserveFrom(EAC_SceneRecord record, EAC_AmbientModule module, EAC_ResidentClaim claim, EAC_RoutineDefinition routine, float now, out vector transform[4])
 {
  Math3D.MatrixIdentity4(transform);
  if (!Replication.IsServer() || !module || !record || record.State != 2) return false;
  if (!claim || !claim.Character || !claim.Group || !routine) return false;
  CheckWorld();
  AIPathfindingComponent path = AIPathfindingComponent.Cast(claim.Group.FindComponent(AIPathfindingComponent));
  if (!path) { s_Miss++; return false; }
  BaseWorld world = module.GetWorld();
  int wantedAnim = 1 << routine.Anim;
  int wantedKind = routine.Anchor;
  int residentId = claim.Resident.Id;
  int faceCount = record.Faces.Count();
  // Three passes of decreasing fussiness, so a rare exact match is preferred but
  // a usable position is never refused for being the wrong label.
  for (int pass = 0; pass < 3; pass++)
  {
   for (int faceStep = 0; faceStep < faceCount; faceStep++)
   {
    EAC_SceneFace face = record.Faces[(residentId + faceStep) % faceCount];
    for (int index = 0; index < face.Spots.Count(); index++)
    {
     EAC_SceneSpot spot = face.Spots[index];
     if (spot.Failures >= 3) continue;
     if (spot.OccupantId != 0 && spot.OccupantId != residentId && now < spot.HeldUntil) continue;
     if ((spot.AnimMask & wantedAnim) == 0) continue;
     if (now < spot.ReleasedAt) continue;
     if (pass == 0 && spot.Kind != wantedKind) continue;
     if (pass == 1 && !Substitutable(wantedKind, spot.Kind)) continue;
     if (!Revalidate(spot, world, path, claim.Character)) { spot.Failures++; s_RevalidateFail++; continue; }
     spot.Failures = 0;
     spot.OccupantId = residentId;
     spot.HeldUntil = now + 600;
     spot.Uses++;
     if (spot.Furniture) s_HeldSeats.Set(residentId, spot);
     spot.ReleasedAt = now + REPEAT_COOLDOWN;
     for (int axis = 0; axis < 4; axis++) transform[axis] = spot.Transform[axis];
     if (pass > 0) s_Substituted++;
     s_Hit++; s_Held++;
     return true;
    }
   }
  }
  s_Miss++;
  return false;
 }

 static EAC_SceneSpot HeldSeat(int residentId) { CheckWorld(); return s_HeldSeats.Get(residentId); }

 // How long a just-released position stays out of the offer. ReserveFrom stamps
 // the same span when it hands a spot out, and both releases below now stamp it
 // again instead of zeroing it (audit item 5): zeroing it meant the very next
 // call was offered the identical position, so EAC_CivilianActivity.OutdoorAnchor's
 // three-attempt loop asked three times and got one spot three times over.
 static const float REPEAT_COOLDOWN = 20;

 // The clock every release stamps against. The two releases are static and carry
 // no `now`, so it is read here exactly as HeldCount reads it.
 protected static float ReleaseStamp()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001 + REPEAT_COOLDOWN;
 }

 // TEST-ONLY (audit S12). Every runtime release goes through ReleaseAllFor,
 // which does not need the caller to know which record holds the position; this
 // by-record form is kept because tests/Enforce/EAC_RoutineTests.c proves the
 // anti-repeat stamp through it. Identity checked and idempotent, so a stale
 // handle can never free a position somebody else is standing on.
 static void Release(int homeId, int residentId)
 {
  s_HeldSeats.Remove(residentId);
  if (!Replication.IsServer() || residentId == 0) return;
  CheckWorld();
  EAC_SceneRecord record = s_Records.Get(homeId);
  if (!record) return;
  float cooldown = ReleaseStamp();
  foreach (EAC_SceneFace face : record.Faces)
   foreach (EAC_SceneSpot spot : face.Spots)
    if (spot.OccupantId == residentId)
    {
     spot.OccupantId = 0; spot.HeldUntil = 0;
     if (cooldown > spot.ReleasedAt) spot.ReleasedAt = cooldown;
     if (s_Held > 0) s_Held--;
    }
 }

 // A dead, cached or player-touched resident must not keep holding a position.
 static void ReleaseAllFor(int residentId)
 {
  s_HeldSeats.Remove(residentId);
  if (!Replication.IsServer() || residentId == 0) return;
  CheckWorld();
  float cooldown = ReleaseStamp();
  foreach (int id, EAC_SceneRecord record : s_Records)
   foreach (EAC_SceneFace face : record.Faces)
    foreach (EAC_SceneSpot spot : face.Spots)
     if (spot.OccupantId == residentId)
     {
      spot.OccupantId = 0; spot.HeldUntil = 0;
      if (cooldown > spot.ReleasedAt) spot.ReleasedAt = cooldown;
      if (s_Held > 0) s_Held--;
     }
 }

 // A native move to a position this resident holds failed with no path. Push the
 // spot past the Failures >= 3 line ReserveFrom already skips, so it is never
 // offered again this mission; a spot that passes Revalidate but cannot be
 // pathed to would otherwise be re-offered every time its 20 s cooldown lapsed.
 // Called before the lease is released, while OccupantId still names the holder.
 static int FailHeld(int residentId)
 {
  if (!Replication.IsServer() || residentId == 0) return 0;
  CheckWorld();
  int retired;
  foreach (int id, EAC_SceneRecord record : s_Records)
   foreach (EAC_SceneFace face : record.Faces)
    foreach (EAC_SceneSpot spot : face.Spots)
     if (spot.OccupantId == residentId) { spot.Failures = spot.Failures + 3; retired++; }
  return retired;
 }

 // How many surveyed positions are held by a live lease right now. Read-only: it
 // reserves nothing, releases nothing and does no survey work, and it walks the
 // existing records once, bounded by MAX_RECORDS * FACES * MAX_SPOTS_PER_FACE.
 // Called from the QA fixture only, never from a scheduler path. Chaining turns
 // one reserve/release per routine into two to four, and the 600 s HeldUntil
 // lease is self-healing but far slower than a campaign, so a leak would
 // otherwise hide.
 static int HeldCount()
 {
  CheckWorld();
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  int held = 0;
  foreach (int id, EAC_SceneRecord record : s_Records)
   foreach (EAC_SceneFace face : record.Faces)
    foreach (EAC_SceneSpot spot : face.Spots)
     if (spot.OccupantId != 0 && spot.HeldUntil > now) held++;
  return held;
 }

 static string DescribeKinds()
 {
  CheckWorld();
  int verified = 0;
  foreach (int id, EAC_SceneRecord record : s_Records)
   if (record.State == 2) verified++;
  string result = "scenes homes=" + verified.ToString() + "/" + s_Records.Count().ToString();
  result += " spots=" + s_SpotTotal.ToString() + "/" + MAX_SPOTS_TOTAL.ToString();
  result += " kinds";
  for (int kind = 0; kind < EAC_RoutineAnchors.KINDS; kind++)
   result += " " + EAC_RoutineAnchors.ShortName(kind) + "=" + s_KindSpots[kind].ToString();
  return result;
 }

 static string Describe()
 {
  CheckWorld();
  string result = "scene_index queries=" + s_Queries.ToString() + " saturated=" + s_Saturated.ToString();
  result += " props=" + s_PropsFound.ToString() + " samples=" + s_Samples.ToString();
  result += " traces=" + s_Traces.ToString() + " deferred=" + s_Deferred.ToString();
  result += " barren=" + s_BarrenHomes.ToString() + " refused=" + s_Refused.ToString();
  result += " no_path=" + s_NoPath.ToString() + " home_full=" + s_HomeFull.ToString();
  result += " peak_callbacks=" + s_PeakCallbacks.ToString() + " doors=" + s_DoorsFound.ToString();
  result += " hit=" + s_Hit.ToString() + " miss=" + s_Miss.ToString();
  result += " revalidate_fail=" + s_RevalidateFail.ToString() + " held=" + s_Held.ToString();
  result += " substituted=" + s_Substituted.ToString();
  result += " real_seats=" + s_RealSeats.ToString() + " seat_cap=" + MAX_SEAT_SAMPLES_PER_HOME.ToString();
  result += " seat_full=" + s_SeatFull.ToString() + " seat_prints=" + s_SeatPrints.ToString();
  result += " door_evicted=" + EAC_DoorIndex.GetEvictedCount().ToString();
  result += " record_buckets=" + s_RecordCells.Count().ToString();
  return result;
 }

 static void AppendDebugDrawData(array<vector> positions, array<int> kinds)
 {
  CheckWorld();
  if (!positions || !kinds) return;
  int shown = 0;
  foreach (int id, EAC_SceneRecord record : s_Records)
  {
   if (shown >= 16) return;
   foreach (EAC_SceneFace face : record.Faces)
   {
    foreach (EAC_SceneSpot spot : face.Spots)
    {
     if (shown >= 16) return;
     EAC_Diagnostics.AddDrawPoint(positions, kinds, spot.Transform[3], EAC_Diagnostics.SCENE);
     shown++;
    }
   }
  }
 }
}
