// Per-tick allowance shared by every Random Garrison zone.
class EXPG_RGBudget
{
 int Deletes;
 // System tick (ms) after which no further zone starts its step this tick.
 int Deadline;
}

// One server call queue (100 ms) for every Random Garrison zone of the current world:
// at most 64 zones, round robin, about 2 ms of zone script per tick (at least one
// zone per tick). Shared limits: one squad spawn every 1.5 s, at most two squads still
// spawning their soldiers, at most three building analyses at a time (the manager's
// pump runs them; no zone ever steps a plan), one zone per building (claims). It also
// deletes what deleted zones leave behind (8 entities per tick), keeps squads that
// have not taken their posts, and everything waiting to be deleted, out of saves, and
// stops pending work for a legacy CDF Prepare for Save.
class EXPG_RandomGarrisonDirector
{
 static const int TICK_MS = 100;
 static const int MAX_ZONES = 64;
 static const int BUDGET_MS = 2;
 static const float SPAWN_INTERVAL = 1.5;
 static const int MAX_SPAWNING = 2;
 static const int MAX_ANALYSES = 3;
 static const int DELETES_PER_TICK = 8;
 protected static ref array<EXPG_RandomGarrisonModule> s_Zones;
 protected static ref array<int> s_ZoneIds;
 // Entities of deleted zones and dismissed garrisons, kept out of saves until deleted.
 protected static ref EXPG_RetireList s_Janitor;
 protected static ref array<ref EXPG_GarrisonRecord> s_Dismiss;
 protected static ref map<IEntity, int> s_Claims;
 protected static ref EXPG_RGBudget s_Budget;
 protected static BaseWorld s_World;
 protected static bool s_Scheduled;
 protected static bool s_SaveHooked;
 protected static bool s_CapWarned;
 protected static float s_NextSpawn;
 protected static int s_Cursor;
 protected static int s_NextZoneId;

 // Static records never outlive their world; the entities went with it.
 protected static void CheckWorld()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  if (!GetGame())
  {
   return;
  }
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World)
  {
   return;
  }
  GetGame().GetCallqueue().Remove(Tick);
  s_Zones.Clear();
  s_ZoneIds.Clear();
  s_Janitor.Clear();
  s_Dismiss.Clear();
  s_Claims.Clear();
  s_World = world;
  s_Scheduled = false;
  s_SaveHooked = false;
  s_CapWarned = false;
  s_NextSpawn = 0;
  s_Cursor = 0;
 }

 // The zone's id (0: refused, the cap of 64 zones is reached).
 static int Register(EXPG_RandomGarrisonModule zone)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  if (!zone || !GetGame() || !Replication.IsServer())
  {
   return 0;
  }
  CheckWorld();
  if (!s_World || zone.GetWorld() != s_World)
  {
   return 0;
  }
  int known = s_Zones.Find(zone);
  if (known >= 0)
  {
   return s_ZoneIds[known];
  }
  Compact();
  if (s_Zones.Count() >= MAX_ZONES)
  {
   if (!s_CapWarned) { PrintFormat("[EXPG RANDOM] Random Garrison zone cap reached (%1); additional zones stay inert", MAX_ZONES, level: LogLevel.WARNING); }
   s_CapWarned = true;
   return 0;
  }
  s_NextZoneId++;
  s_Zones.Insert(zone);
  s_ZoneIds.Insert(s_NextZoneId);
  HookSaves();
  Wake();
  return s_NextZoneId;
 }

 // A deleted zone: its claims are released, what it leaves is deleted a few per tick,
 // and with "Delete garrisons" its garrisons are dismissed one per tick (the captured
 // records, never a token lookup).
 static void Unregister(EXPG_RandomGarrisonModule zone, int zoneId, EXPG_RetireList leftovers, array<ref EXPG_GarrisonRecord> records)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  int index = s_Zones.Find(zone);
  if (index >= 0)
  {
   s_Zones.RemoveOrdered(index);
   s_ZoneIds.RemoveOrdered(index);
  }
  ReleaseClaims(zoneId);
  // Nothing is touched while the world goes away (its entities go with it).
  if (!WorldStays())
  {
   return;
  }
  if (leftovers) { leftovers.MoveTo(s_Janitor); }
  if (records)
  {
   foreach (EXPG_GarrisonRecord record : records)
   {
    if (record && !record.Finished) { s_Dismiss.Insert(record); }
   }
  }
  Wake();
 }

 static void Wake()
 {
  if (s_Scheduled || !GetGame() || !Replication.IsServer())
  {
   return;
  }
  s_Scheduled = true;
  GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
 }

 protected static void Doze()
 {
  if (GetGame()) { GetGame().GetCallqueue().Remove(Tick); }
  s_Scheduled = false;
 }

 protected static void Compact()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  for (int i = s_Zones.Count() - 1; i >= 0; i--)
  {
   if (s_Zones[i]) { continue; }
   ReleaseClaims(s_ZoneIds[i]);
   s_Zones.RemoveOrdered(i);
   s_ZoneIds.RemoveOrdered(i);
  }
 }

 protected static void Tick()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  if (!GetGame())
  {
   return;
  }
  if (!s_World || GetGame().GetWorld() != s_World || !GetGame().InPlayMode())
  {
   Doze();
   CheckWorld();
   return;
  }
  Compact();
  if (!s_SaveHooked) { HookSaves(); }
  float now = s_World.GetWorldTime() * 0.001;
  int start = System.GetTickCount();
  s_Budget.Deletes = DELETES_PER_TICK;
  s_Budget.Deadline = start + BUDGET_MS;
  bool busy = ServiceJanitor();
  int count = s_Zones.Count();
  if (count > 0)
  {
   // Zones are visited round robin; a zone with no work costs a status check only.
   busy = true;
   int first = s_Cursor % count;
   int visited;
   while (visited < count)
   {
    if (visited > 0 && System.GetTickCount() >= s_Budget.Deadline) { break; }
    EXPG_RandomGarrisonModule zone = s_Zones[(first + visited) % count];
    if (zone) { zone.Step(now, s_Budget); }
    visited++;
   }
   s_Cursor = (first + visited) % count;
  }
  if (!busy) { Doze(); }
 }

 protected static bool ServiceJanitor()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  bool busy;
  if (!s_Dismiss.IsEmpty())
  {
   busy = true;
   if (!EXPG_GarrisonManager.SaveInProgress())
   {
    EXPG_GarrisonRecord record = s_Dismiss[0];
    s_Dismiss.RemoveOrdered(0);
    EXPG_GarrisonSpawner.Dismiss(record, s_Janitor, "Random Garrison deleted with Delete garrisons");
   }
  }
  s_Budget.Deletes -= s_Janitor.DeleteSome(s_Budget.Deletes);
  if (!s_Janitor.IsEmpty()) { busy = true; }
  return busy;
 }

 static int JanitorCount()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  return s_Janitor.Count() + s_Dismiss.Count();
 }

 //------------------------------------------------------------------------------------------------
 // Shared limits
 //------------------------------------------------------------------------------------------------
 static bool CanSpawn(float now)
 {
  if (now < s_NextSpawn)
  {
   return false;
  }
  return SpawningSquads() < MAX_SPAWNING;
 }

 static void NoteSpawn(float now)
 {
  s_NextSpawn = now + SPAWN_INTERVAL;
 }

 static int SpawningSquads()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  int spawning;
  foreach (EXPG_RandomGarrisonModule zone : s_Zones)
  {
   if (zone) { spawning += zone.SpawningSquads(); }
  }
  return spawning;
 }

 static int AnalysesRunning()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  int running;
  foreach (EXPG_RandomGarrisonModule zone : s_Zones)
  {
   if (zone) { running += zone.AnalysesRunning(); }
  }
  return running;
 }

 //------------------------------------------------------------------------------------------------
 // Building claims: one zone per building while it generates there.
 //------------------------------------------------------------------------------------------------
 static bool Claim(IEntity structure, int zoneId)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  if (!structure || zoneId < 1)
  {
   return false;
  }
  int owner;
  if (s_Claims.Find(structure, owner) && owner != zoneId)
  {
   return false;
  }
  s_Claims.Set(structure, zoneId);
  return true;
 }

 static void ReleaseClaim(IEntity structure, int zoneId)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  if (!structure)
  {
   return;
  }
  int owner;
  if (s_Claims.Find(structure, owner) && owner == zoneId) { s_Claims.Remove(structure); }
 }

 static bool ClaimedByOther(IEntity structure, int zoneId)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  int owner;
  if (!structure || !s_Claims.Find(structure, owner))
  {
   return false;
  }
  return owner != zoneId;
 }

 protected static void ReleaseClaims(int zoneId)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  array<IEntity> released = {};
  foreach (IEntity structure, int owner : s_Claims)
  {
   if (owner == zoneId || !structure) { released.Insert(structure); }
  }
  foreach (IEntity gone : released) { s_Claims.Remove(gone); }
 }

 //------------------------------------------------------------------------------------------------
 // Saves and teardown
 //------------------------------------------------------------------------------------------------
 // A save is being prepared or written, or a load is replacing the garrisons.
 static bool SavePreparing()
 {
  return EBG_OptimizerControl.Preparing || EXPG_GarrisonManager.SaveInProgress();
 }

 // A deleted zone does nothing while the world unloads or a load replaces the scene
 // (those garrisons are being re-imported).
 static bool Teardown()
 {
  if (!GetGame() || !GetGame().InPlayMode() || !GetGame().GetWorld())
  {
   return true;
  }
  if (s_World && GetGame().GetWorld() != s_World)
  {
   return true;
  }
  if (!EBG_CacheManager.IsPortableWorldReady() || EBG_CacheSnapshot.Loading)
  {
   return true;
  }
  return EXPG_GarrisonManager.SaveInProgress();
 }

 // The world of the registered zones goes on, also while a save or a load runs: what a
 // deleted zone keeps out of saves can still be handed to the janitor and deleted.
 static bool WorldStays()
 {
  if (!GetGame() || !s_World || GetGame().GetWorld() != s_World)
  {
   return false;
  }
  return EBG_CacheManager.IsPortableWorldReady();
 }

 // CDF legacy Prepare for Save releases every garrison: nothing pending is left behind.
 static void AbortForSave()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  Compact();
  foreach (EXPG_RandomGarrisonModule zone : s_Zones)
  {
   if (zone) { zone.AbortForSave(); }
  }
 }

 protected static void HookSaves()
 {
  if (s_SaveHooked || !s_World)
  {
   return;
  }
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByCurrentWorld();
  if (!persistence)
  {
   return;
  }
  persistence.GetOnBeforeSave().Insert(OnBeforeSave);
  s_SaveHooked = true;
 }

 // Right before a native save reads its data: squads that have not taken their posts,
 // and whatever waits to be deleted, stay out of it, even when tracking began again.
 protected static void OnBeforeSave(ESaveGameType saveType)
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  if (!GetGame() || !s_World || GetGame().GetWorld() != s_World || !Replication.IsServer())
  {
   return;
  }
  Compact();
  s_Janitor.KeepOut(true);
  foreach (EXPG_RandomGarrisonModule zone : s_Zones)
  {
   if (zone) { zone.KeepPendingOutOfSaves(); }
  }
 }

 static int ZoneCount()
 {
		EXPBG_LazyStatics_EXPG_RandomGarrisonDirector();
  Compact();
  return s_Zones.Count();
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EXPG_RandomGarrisonDirector()
	{
		if (!s_Zones)
			s_Zones = new array<EXPG_RandomGarrisonModule>();
		if (!s_ZoneIds)
			s_ZoneIds = new array<int>();
		if (!s_Janitor)
			s_Janitor = new EXPG_RetireList();
		if (!s_Dismiss)
			s_Dismiss = new array<ref EXPG_GarrisonRecord>();
		if (!s_Claims)
			s_Claims = new map<IEntity, int>();
		if (!s_Budget)
			s_Budget = new EXPG_RGBudget();
	}
}

// CDF without the EXPBG CDF Compat bridge: Prepare for Save releases every garrison,
// so a generation in progress stops first and its squads that never deployed go.
modded class EBG_OptimizerControl
{
 override static void BeginPreparation()
 {
  if (Replication.IsServer() && EXPG_GarrisonPersistence.Legacy()) { EXPG_RandomGarrisonDirector.AbortForSave(); }
  super.BeginPreparation();
 }
}
