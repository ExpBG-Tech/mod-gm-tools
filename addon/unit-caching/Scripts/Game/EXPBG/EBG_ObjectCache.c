// EXPBG Unit Caching: objects and effect modules (owner request 2026-10-10, issue #39).
// A cache zone with "Objects and effects" ON also removes, while nobody is near, the
// single props placed in its affected radius and the endless effect modules there (fire,
// smoke and similar, from any mod: detected at runtime, no dependency), and puts each back
// on its exact transform, with its editor attributes and Game Master author, when a
// player comes near, the zone is disabled or a save is prepared. Vehicles, characters,
// compositions, containers and every EXPBG module are never touched.
//
// Rules, per zone, on the existing 1 s cache tick (no per-frame work):
//  - sleep: zone Enabled, not held for editing or a save, Objects and effects ON, no player
//    character inside the zone's sleep radius for its sleep delay. One sphere query of the
//    affected radius takes the eligible entities (at most CAPTURE_LIMIT per zone), then
//    every SCAN_SECONDS while asleep for objects placed meanwhile;
//  - wake: a player inside the wake radius, the zone disabled, held, or the option OFF;
//    objects come back RESTORE_PER_PUMP at a time every PUMP_MS (no burst);
//  - native saves: the first capture takes the Full save gate (EBG_FullSaveGate), which
//    keeps native saving paused until every cached object is back;
//  - CDF: the zone's portable snapshot carries its cached objects ("cachedObjects", read
//    as optional), so a save never loses them; after a load zones start disabled and
//    everything comes back.
// Logs only while the zone has Debug messages on.
class EBG_CachedObject
{
 ResourceName Prefab;
 vector Transform[4];
 float Scale = 1;
 ref EBG_CacheAuthor Author = new EBG_CacheAuthor();
 // Editor attributes as the save systems keep them: "ClassName#occurrence" and value.
 ref array<string> AttributeKeys = {};
 ref array<vector> AttributeValues = {};

 bool Write(SaveContext context)
 {
  array<vector> matrix = {Transform[0], Transform[1], Transform[2], Transform[3]};
  return context.WriteValue("prefab", Prefab) && context.WriteValue("matrix", matrix) && context.WriteValue("scale", Scale) && Author.Write(context) && context.WriteValue("attributeKeys", AttributeKeys) && context.WriteValue("attributeValues", AttributeValues);
 }

 bool Read(LoadContext context)
 {
  array<vector> matrix = {};
  if (!context.ReadValue("prefab", Prefab) || !context.ReadValue("matrix", matrix) || matrix.Count() != 4 || !context.ReadValue("scale", Scale) || !Author.Read(context) || !context.ReadValue("attributeKeys", AttributeKeys) || !context.ReadValue("attributeValues", AttributeValues))
  {
   return false;
  }
  if (Prefab.IsEmpty() || AttributeKeys.Count() != AttributeValues.Count() || AttributeKeys.Count() > 256 || !(Scale > 0.01 && Scale < 100))
  {
   return false;
  }
  for (int axis = 0; axis < 4; axis++) Transform[axis] = matrix[axis];
  return true;
 }
}

class EBG_ObjectZone
{
 EBG_CacheZone Zone;
 ref array<ref EBG_CachedObject> Cached = {};
 bool Asleep;
 float ClearSince = -1;
 float NextScan;
}

class EBG_ObjectCache
{
 static const int CAPTURE_LIMIT = 512;
 static const int RESTORE_PER_PUMP = 8;
 static const int PUMP_MS = 250;
 static const float SCAN_SECONDS = 30;
 static const ResourceName ATTRIBUTE_LIST = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf";
 // Effect modules are recognised by their prefab path (lower case): endless emitters.
 protected static ref array<string> s_aEffectWords;
 protected static ref array<ref EBG_ObjectZone> s_aZones;
 protected static ref SCR_EditorAttributeList s_List;
 protected static ref array<string> s_aKeys;
 protected static BaseGameMode s_ListMode;
 protected static bool s_bPumping;
 protected static bool s_bFailureLogged;
 protected static World s_World;
 protected static ref array<IEntity> s_aFound;

 //------------------------------------------------------------------------------------------------
 protected static void Ensure()
 {
  World world;
  if (GetGame()) world = GetGame().GetWorld();
  if (s_aZones && world == s_World) return;
  s_World = world;
  s_aZones = {};
  s_bPumping = false;
  if (!s_aEffectWords) s_aEffectWords = {"fire", "smoke", "flame", "burn", "steam", "spark", "flare", "fog", "ember", "blaze"};
 }

 //------------------------------------------------------------------------------------------------
 protected static EBG_ObjectZone Find(EBG_CacheZone zone, bool create)
 {
  Ensure();
  foreach (EBG_ObjectZone state : s_aZones)
  {
   if (state && state.Zone == zone) return state;
  }
  if (!create) return null;
  EBG_ObjectZone added = new EBG_ObjectZone();
  added.Zone = zone;
  s_aZones.Insert(added);
  return added;
 }

 //------------------------------------------------------------------------------------------------
 //! Objects currently removed by any zone (also those of a deleted zone still coming back).
 static int CachedCount()
 {
  if (!s_aZones) return 0;
  int count;
  foreach (EBG_ObjectZone state : s_aZones)
  {
   if (state) count += state.Cached.Count();
  }
  return count;
 }

 //------------------------------------------------------------------------------------------------
 static int CachedCount(EBG_CacheZone zone)
 {
  EBG_ObjectZone state = Find(zone, false);
  if (!state) return 0;
  return state.Cached.Count();
 }

 //------------------------------------------------------------------------------------------------
 protected static void Log(EBG_CacheZone zone, string text)
 {
  if (zone && zone.DebugMessages > 0) PrintFormat("[EBG OBJECTS] zone=%1 %2", zone.GetID(), text);
 }

 //------------------------------------------------------------------------------------------------
 //! A placed single prop or an endless effect module this cache may remove.
 static bool Eligible(IEntity entity)
 {
  if (!entity || entity.IsDeleted()) return false;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable || editable.GetOwner() != entity || editable.GetParentEntity()) return false;
  if (editable.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE) || editable.HasEntityFlag(EEditableEntityFlag.LOCAL)) return false;
  set<SCR_EditableEntityComponent> children = editable.GetChildrenRef();
  if (children && !children.IsEmpty()) return false;
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  if (prefab.IsEmpty()) return false;
  string path = prefab;
  path.ToLower();
  // Every EXPBG module and prop (PrefabsEditable/EXPBG, EXPTW, EXPAU, Props/EXPBG...).
  if (path.Contains("/exp")) return false;
  if (entity.FindComponent(InventoryStorageManagerComponent) || entity.FindComponent(BaseCompartmentManagerComponent) || entity.FindComponent(SCR_EditableVehicleComponent)) return false;
  EEditableEntityType type = editable.GetEntityType();
  if (type == EEditableEntityType.GENERIC) return true;
  if (type != EEditableEntityType.SYSTEM) return false;
  if (path.Contains("firing")) return false;
  foreach (string word : s_aEffectWords)
  {
   if (path.Contains(word)) return true;
  }
  return false;
 }

 //------------------------------------------------------------------------------------------------
 //! The Game Master Edit attribute list, initialised for this game mode (as the save systems do).
 protected static SCR_EditorAttributeList List()
 {
  BaseGameMode mode = GetGame().GetGameMode();
  if (s_List && s_ListMode == mode) return s_List;
  if (!s_List)
  {
   Resource resource = Resource.Load(ATTRIBUTE_LIST);
   if (!resource || !resource.IsValid()) return null;
   s_List = SCR_EditorAttributeList.Cast(BaseContainerTools.CreateInstanceFromContainer(resource.GetResource().ToBaseContainer()));
   if (!s_List) return null;
   s_aKeys = {};
   map<string, int> seen = new map<string, int>();
   for (int i = 0, count = s_List.GetAttributesCount(); i < count; i++)
   {
    SCR_BaseEditorAttribute attribute = s_List.GetAttribute(i);
    string name = "none";
    if (attribute) name = attribute.ClassName();
    int occurrence;
    seen.Find(name, occurrence);
    seen.Set(name, occurrence + 1);
    s_aKeys.Insert(name + "#" + occurrence.ToString());
   }
  }
  for (int index = 0, total = s_List.GetAttributesCount(); index < total; index++)
  {
   SCR_BaseEditorAttribute initialised = s_List.GetAttribute(index);
   if (initialised) initialised.Initialize();
  }
  s_ListMode = mode;
  return s_List;
 }

 //------------------------------------------------------------------------------------------------
 protected static EBG_CachedObject Capture(IEntity entity)
 {
  EBG_CachedObject row = new EBG_CachedObject();
  row.Prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  entity.GetWorldTransform(row.Transform);
  row.Scale = entity.GetScale();
  row.Author.Capture(entity);
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  SCR_EditorAttributeList list = List();
  if (list && editable)
  {
   for (int i = 0, count = list.GetAttributesCount(); i < count; i++)
   {
    SCR_BaseEditorAttribute attribute = list.GetAttribute(i);
    if (!attribute || !attribute.IsServer() || !attribute.IsSerializable()) continue;
    SCR_BaseEditorAttributeVar var = attribute.ReadVariable(editable, null);
    if (!var) continue;
    row.AttributeKeys.Insert(s_aKeys[i]);
    row.AttributeValues.Insert(var.GetVector());
   }
  }
  return row;
 }

 //------------------------------------------------------------------------------------------------
 protected static bool Restore(EBG_CachedObject row)
 {
  Resource resource = Resource.Load(row.Prefab);
  if (!resource || !resource.IsValid()) return false;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  params.Transform = row.Transform;
  params.Scale = row.Scale;
  IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  if (!entity) return false;
  row.Author.Apply(entity);
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  SCR_EditorAttributeList list = List();
  if (!list || !editable || row.AttributeKeys.IsEmpty()) return true;
  for (int i = 0; i < row.AttributeKeys.Count(); i++)
  {
   int index = s_aKeys.Find(row.AttributeKeys[i]);
   if (index < 0) continue;
   SCR_BaseEditorAttribute attribute = list.GetAttribute(index);
   if (!attribute || !attribute.IsSerializable()) continue;
   attribute.WriteVariable(editable, SCR_BaseEditorAttributeVar.CreateVector(row.AttributeValues[i]), null, -1);
  }
  return true;
 }

 //------------------------------------------------------------------------------------------------
 protected static bool CollectFound(IEntity entity)
 {
  if (s_aFound && s_aFound.Count() < CAPTURE_LIMIT && Eligible(entity)) s_aFound.Insert(entity);
  return true;
 }

 //------------------------------------------------------------------------------------------------
 //! Removes the zone's eligible objects; false when the save gate refuses.
 protected static bool Sleep(EBG_CacheManager manager, EBG_ObjectZone state)
 {
  EBG_CacheZone zone = state.Zone;
  s_aFound = {};
  GetGame().GetWorld().QueryEntitiesBySphere(zone.GetOrigin(), zone.Affected, CollectFound);
  array<IEntity> found = s_aFound;
  s_aFound = null;
  if (found.IsEmpty()) return true;
  string reason;
  if (!EBG_FullSaveGate.TryAcquire(manager, reason))
  {
   Log(zone, "not cached: " + reason);
   return false;
  }
  int removed;
  foreach (IEntity entity : found)
  {
   if (state.Cached.Count() >= CAPTURE_LIMIT) break;
   EBG_CachedObject row = Capture(entity);
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
   if (!editable || !editable.Delete(false, false)) continue;
   state.Cached.Insert(row);
   removed++;
  }
  if (removed > 0) Log(zone, string.Format("cached %1 objects (%2 held)", removed, state.Cached.Count()));
  return true;
 }

 //------------------------------------------------------------------------------------------------
 //! Called after each Unit Caching tick (EBG_CacheManager.Tick, about once per second).
 static void Tick(EBG_CacheManager manager)
 {
  if (!manager || !Replication.IsServer() || EBG_CacheSnapshot.Loading) return;
  Ensure();
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  // Zones with the option or with cached objects; deleted zones give everything back.
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (zone && zone.CacheObjects) Find(zone, true);
  }
  bool restoring;
  foreach (EBG_ObjectZone state : s_aZones)
  {
   if (!state) continue;
   EBG_CacheZone zone = state.Zone;
   bool allowed = zone && zone.Enabled && !zone.Editing && zone.CacheObjects && !EBG_OptimizerControl.Preparing && !EBG_OptimizerControl.Disabling && !EBG_FullSaveGate.IsRestoreRequested();
   bool near = zone && EBG_CacheGeometry.AnyPlayer(manager.Players, zone.GetOrigin(), zone.ZoneWake, 0, 0, false, 0, 0, 0);
   bool inSleepRadius = zone && EBG_CacheGeometry.AnyPlayer(manager.Players, zone.GetOrigin(), zone.ZoneSleep, 0, 0, false, 0, 0, 0);
   if (!allowed || near)
   {
    state.Asleep = false;
    state.ClearSince = -1;
    if (!state.Cached.IsEmpty()) restoring = true;
    continue;
   }
   if (inSleepRadius)
   {
    state.ClearSince = -1;
    continue;
   }
   if (state.ClearSince < 0) state.ClearSince = now;
   if (now - state.ClearSince < zone.SleepDelay) continue;
   if (!state.Asleep || now >= state.NextScan)
   {
    if (Sleep(manager, state)) state.Asleep = true;
    state.NextScan = now + SCAN_SECONDS;
   }
  }
  if (restoring && !s_bPumping)
  {
   s_bPumping = true;
   GetGame().GetCallqueue().CallLater(Pump, PUMP_MS, true);
  }
 }

 //------------------------------------------------------------------------------------------------
 //! Paced restore: RESTORE_PER_PUMP objects every PUMP_MS across all waking zones.
 protected static void Pump()
 {
  if (!GetGame() || !s_aZones || GetGame().GetWorld() != s_World)
  {
   Stop();
   return;
  }
  int budget = RESTORE_PER_PUMP;
  bool left;
  for (int z = s_aZones.Count() - 1; z >= 0; z--)
  {
   EBG_ObjectZone state = s_aZones[z];
   if (!state) continue;
   if (state.Asleep && state.Zone)
   {
    continue;
   }
   while (budget > 0 && !state.Cached.IsEmpty())
   {
    EBG_CachedObject row = state.Cached[state.Cached.Count() - 1];
    state.Cached.Remove(state.Cached.Count() - 1);
    budget--;
    if (!Restore(row) && !s_bFailureLogged)
    {
     // Once per session: an object whose prefab is no longer loaded cannot come back.
     s_bFailureLogged = true;
     PrintFormat("[EBG OBJECTS] restore failed: %1 (prefab unavailable); later failures are not logged", row.Prefab);
    }
   }
   if (!state.Cached.IsEmpty()) left = true;
   else if (!state.Zone) s_aZones.Remove(z);
   else Log(state.Zone, "all objects restored");
  }
  if (!left) Stop();
 }

 //------------------------------------------------------------------------------------------------
 protected static void Stop()
 {
  s_bPumping = false;
  if (GetGame() && GetGame().GetCallqueue()) GetGame().GetCallqueue().Remove(Pump);
 }

 //------------------------------------------------------------------------------------------------
 //! Portable (CDF) snapshot of a zone: its cached objects (optional key).
 static bool WriteZone(EBG_CacheZone zone, SaveContext context)
 {
  EBG_ObjectZone state = Find(zone, false);
  int count;
  if (state) count = state.Cached.Count();
  if (!context.WriteValue("cachedObjects", count)) return false;
  for (int i = 0; i < count; i++)
  {
   if (!context.StartObject("object" + i.ToString()) || !state.Cached[i].Write(context) || !context.EndObject()) return false;
  }
  return true;
 }

 //------------------------------------------------------------------------------------------------
 //! Reads a zone's cached objects back; a snapshot without them reads as none. A row that
 //! does not read back is dropped with a warning (never the whole snapshot).
 static void ReadZone(EBG_CacheZone zone, LoadContext context)
 {
  int count;
  if (!context.ReadValue("cachedObjects", count) || count <= 0) return;
  EBG_ObjectZone state = Find(zone, true);
  count = Math.Min(count, CAPTURE_LIMIT);
  for (int i = 0; i < count; i++)
  {
   if (!context.StartObject("object" + i.ToString())) continue;
   EBG_CachedObject row = new EBG_CachedObject();
   if (row.Read(context)) state.Cached.Insert(row);
   else Print("[EBG OBJECTS] a saved cached object did not read back and is dropped", LogLevel.WARNING);
   context.EndObject();
  }
  state.Asleep = true;
 }
}

modded class EBG_CacheManager
{
 override void Tick()
 {
  super.Tick();
  EBG_ObjectCache.Tick(this);
 }
}
