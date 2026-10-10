// TEST ONLY. Unit Caching "Objects and effects" (EBG_ObjectCache.c, issue #39).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_ObjectCacheGameplay.c -ExpectResult '\[EBG OBJECT CACHE RESULT\] checks=[1-9]\d* failures=0 cached=3 restored=3 reason=complete' -TimeoutSeconds 420 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
//  1. Authored props (2 fire extinguishers), an authored vanilla smoke effect module and an
//     authored UAZ in a cache zone with Objects and effects ON; no players.
//  2. The zone removes the two props and the smoke module, never the vehicle or itself.
//  3. The zone's portable snapshot carries the three cached objects.
//  4. Injected presence: all three come back on their exact transforms with their author.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const string PROP = "{13CB3FDDDB139026}PrefabsEditable/Auto/Props/Civilian/E_FireExtinguisher_01.et";
 static const string SMOKE = "{1F7300EC0835D898}PrefabsEditable/EffectsModules/Smoke/EffectModule_Smoke_Red.et";
 static const string VEHICLE = "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
 static ref array<vector> s_Presence;
 EBG_CacheZone Zone;
 IEntity Uaz;
 ref array<IEntity> Objects = {};
 ref array<string> Prefabs = {};
 ref array<vector> Spots = {};
 ref array<vector> Ahead = {};
 int Phase;
 int Checks;
 int Failures;
 int Cached;
 int Restored;
 float Started;
 float Next;
 float PhaseAt;
 bool Finished;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_Presence = {};
  Started = Now();
  Next = Started + 10;
  Print("[EBG OBJECT CACHE BEGIN]");
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG OBJECT CACHE CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EBG OBJECT CACHE RESULT] checks=%1 failures=%2 cached=%3 restored=%4 reason=%5", Checks, Failures, Cached, Restored, reason);
  GetGame().RequestClose();
 }
 void Advance(int phase) { Phase = phase; PhaseAt = Now(); }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 vector Ground(vector p, float lift)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift;
  return p;
 }
 IEntity Spawn(string prefab, vector offset, float yaw)
 {
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform);
  params.Transform[3] = Ground(Origin + offset, 0.05);
  Resource resource = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  if (spawned) Author(spawned);
  return spawned;
 }
 void Author(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) return;
  SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
  author.Initialize("object-cache-fixture", "", 0, -1);
  editable.SetAuthor(author);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 protected array<IEntity> m_Near;
 bool Collect(IEntity entity)
 {
  if (m_Near) m_Near.Insert(entity);
  return true;
 }
 // The entity of this prefab standing within 0.05 m of the spot, or null.
 IEntity At(string prefab, vector spot)
 {
  array<IEntity> near = {};
  m_Near = near;
  GetGame().GetWorld().QueryEntitiesBySphere(spot, 1, Collect);
  m_Near = null;
  foreach (IEntity entity : near)
  {
   if (entity && !entity.IsDeleted() && SCR_ResourceNameUtils.GetPrefabName(entity) == prefab && vector.Distance(entity.GetOrigin(), spot) < 0.05) return entity;
  }
  return null;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "deadline (phase " + Phase.ToString() + ")"); Finish("timeout"); return; }
  Step();
 }
 void Step()
 {
  if (Phase == 0)
  {
   Zone = EBG_CacheZone.Cast(Spawn(ZONE_PREFAB, vector.Zero, 0));
   if (!Check(Zone != null, "cache zone spawned")) { Finish("setup"); return; }
   // Simulation mode, 60 m zone, wake 100 m, sleep 300 m, 5 s sleep delay, debug on,
   // Objects and effects ON, enabled.
   Zone.SetValue(1, 0); Zone.SetValue(2, 0); Zone.SetValue(3, 60);
   Zone.SetValue(4, 100); Zone.SetValue(5, 300); Zone.SetValue(12, 5);
   Zone.SetValue(21, 1); Zone.SetValue(24, 1); Zone.SetValue(0, 1);
   Objects.Insert(Spawn(PROP, Vector(6, 0, 3), 30)); Prefabs.Insert(PROP);
   Objects.Insert(Spawn(PROP, Vector(-5, 0, 8), 140)); Prefabs.Insert(PROP);
   Objects.Insert(Spawn(SMOKE, Vector(12, 0, -7), 0)); Prefabs.Insert(SMOKE);
   Uaz = Spawn(VEHICLE, Vector(-14, 0, -10), 0);
   foreach (IEntity spawned : Objects)
   {
    if (!Check(spawned != null, "object spawned")) { Finish("setup"); return; }
   }
   Check(Uaz != null, "UAZ spawned");
   Check(EBG_ObjectCache.Eligible(Objects[0]) && EBG_ObjectCache.Eligible(Objects[2]), "a prop and the smoke module are eligible");
   Check(!EBG_ObjectCache.Eligible(Uaz) && !EBG_ObjectCache.Eligible(Zone), "the vehicle and the EXPBG zone are never eligible");
   Advance(1); return;
  }
  if (Phase == 1)
  {
   if (Now() - PhaseAt < 3) return;
   foreach (IEntity placed : Objects)
   {
    vector transform[4];
    placed.GetWorldTransform(transform);
    Spots.Insert(transform[3]);
    Ahead.Insert(transform[2]);
   }
   Advance(2); return;
  }
  if (Phase == 2)
  {
   if (EBG_ObjectCache.CachedCount(Zone) < 3) { Waited(60, string.Format("the zone cached its three objects (%1)", EBG_ObjectCache.CachedCount(Zone))); return; }
   Cached = EBG_ObjectCache.CachedCount(Zone);
   int left;
   for (int i = 0; i < Spots.Count(); i++) if (At(Prefabs[i], Spots[i])) left++;
   Check(left == 0, string.Format("the props and the smoke module were removed (%1 left)", left));
   Check(Uaz && !Uaz.IsDeleted() && Zone && !Zone.IsDeleted(), "the vehicle and the zone stay");
   string json;
   Check(EBG_CacheSnapshot.WriteZone(Zone, json) && json.Contains("\"cachedObjects\":3"), "the zone snapshot carries the three cached objects");
   s_Presence.Insert(Ground(Origin, 1.8));
   Advance(3); return;
  }
  if (Phase == 3)
  {
   int back;
   int authored;
   int facing;
   for (int i = 0; i < Spots.Count(); i++)
   {
    IEntity entity = At(Prefabs[i], Spots[i]);
    if (!entity) continue;
    back++;
    SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
    if (editable && editable.GetAuthorUID() == "object-cache-fixture") authored++;
    vector transform[4];
    entity.GetWorldTransform(transform);
    if (vector.Dot(transform[2], Ahead[i]) > 0.999) facing++;
   }
   if (back < 3 || EBG_ObjectCache.CachedCount(Zone) > 0) { Waited(30, string.Format("presence restored the three objects (%1 back, %2 cached)", back, EBG_ObjectCache.CachedCount(Zone))); return; }
   Restored = back;
   Check(authored == 3, string.Format("every restored object keeps its Game Master author (%1)", authored));
   Check(facing == 3, string.Format("every restored object keeps its facing (%1)", facing));
   Finish("complete"); return;
  }
 }
}
// Presence seam, as in the scripted Simulation fixture.
modded class EBG_CacheManager
{
 override protected void UpdatePlayers()
 {
  super.UpdatePlayers();
  if (EXPG_GarrisonGameplay.s_Presence)
  {
   foreach (vector presence : EXPG_GarrisonGameplay.s_Presence) Players.Insert(presence);
  }
 }
}
