// Per-tick work allowance shared by every protest zone.
class EAU_Budget
{
 int Spawns;
 int Deletes;
 int Gestures;
 int Probes;
 int Faces;

 void Reset()
 {
  Spawns = EAU_Director.SPAWNS_PER_TICK;
  Deletes = EAU_Director.DELETES_PER_TICK;
  Gestures = EAU_Director.GESTURES_PER_TICK;
  Probes = EAU_Director.PROBES_PER_TICK;
  Faces = EAU_Director.FACES_PER_TICK;
 }
}

// One server call queue for every protest zone in the current world. Spawning,
// removal, gestures and facing checks are bounded per tick across all zones; the
// queue sleeps while no zone has work and wakes on a GM setting change or a
// deleted zone. It also keeps the one player list every zone reads.
class EAU_Director
{
 static const int TICK_MS = 250;
 static const int MAX_ZONES = 16;
 // Shared ceiling on tracked protesters (living and fallen) across all zones.
 static const int MAX_PROTESTERS = 160;
 static const int SPAWNS_PER_TICK = 1;
 static const int DELETES_PER_TICK = 4;
 static const int GESTURES_PER_TICK = 8;
 static const int PROBES_PER_TICK = 8;
 // Protesters choosing whom to face per tick; each pick traces at most
 // EAU_Facing.MAX_SIGHT_CHECKS times.
 static const int FACES_PER_TICK = 3;
 static const float PLAYER_REFRESH_SECONDS = 1;
 static const int MAX_ORPHANS = 512;
 protected static ref array<EAU_ProtestZone> s_Zones;
 // Entities of deleted zones, removed a few per tick.
 protected static ref array<IEntity> s_Orphans;
 protected static ref EAU_Budget s_Budget;
 // Living player characters (plus fixture observers), refreshed at most once a second.
 protected static ref array<IEntity> s_Players;
 protected static ref array<IEntity> s_TestObservers;
 protected static float s_NextPlayers;
 protected static BaseWorld s_World;
 protected static bool s_Scheduled;
 protected static bool s_Ended;
 protected static bool s_GameEndHooked;
 protected static bool s_SaveHooked;
 protected static bool s_CapWarned;
 protected static int s_Cursor;

 // Static records never outlive their world; the entities went with it.
 protected static void CheckWorld()
 {
		EXPBG_LazyStatics_EAU_Director();
  if (!GetGame()) return;
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  GetGame().GetCallqueue().Remove(Tick);
  s_Zones.Clear();
  s_Orphans.Clear();
  s_Players.Clear();
  s_TestObservers.Clear();
  s_NextPlayers = 0;
  s_World = world;
  s_Scheduled = false; s_Ended = false; s_GameEndHooked = false; s_SaveHooked = false; s_CapWarned = false;
  s_Cursor = 0;
 }

 static bool Register(EAU_ProtestZone zone)
 {
		EXPBG_LazyStatics_EAU_Director();
  if (!zone || !GetGame() || !Replication.IsServer()) return false;
  CheckWorld();
  if (!s_World || zone.GetWorld() != s_World) return false;
  if (s_Zones.Contains(zone)) return true;
  Compact();
  if (s_Zones.Count() >= MAX_ZONES)
  {
   if (!s_CapWarned) PrintFormat("[EAU] Protest zone cap reached (%1); additional zones stay inert", MAX_ZONES, level: LogLevel.WARNING);
   s_CapWarned = true;
   return false;
  }
  s_Zones.Insert(zone);
  HookGameEnd();
  HookSaves();
  Wake();
  return true;
 }

 static void Unregister(EAU_ProtestZone zone, array<IEntity> leftovers)
 {
		EXPBG_LazyStatics_EAU_Director();
  s_Zones.RemoveItem(zone);
  if (!GetGame() || !s_World || GetGame().GetWorld() != s_World || !leftovers) return;
  foreach (IEntity entity : leftovers)
  {
   if (entity && s_Orphans.Count() < MAX_ORPHANS) s_Orphans.Insert(entity);
  }
  if (!s_Orphans.IsEmpty()) Wake();
 }

 static bool HasEnded() { return s_Ended; }

 static void Wake()
 {
  if (s_Scheduled || !GetGame() || !Replication.IsServer()) return;
  s_Scheduled = true;
  GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
 }

 protected static void Doze()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(Tick);
  s_Scheduled = false;
 }

 protected static void Compact()
 {
		EXPBG_LazyStatics_EAU_Director();
  for (int i = s_Zones.Count() - 1; i >= 0; i--)
  {
   if (!s_Zones[i]) s_Zones.RemoveOrdered(i);
  }
 }

 protected static void Tick()
 {
		EXPBG_LazyStatics_EAU_Director();
  if (!GetGame()) return;
  if (!s_World || GetGame().GetWorld() != s_World) { CheckWorld(); return; }
  Compact();
  // A world-authored zone can initialise before the game mode or persistence exists.
  if (!s_GameEndHooked) HookGameEnd();
  if (!s_SaveHooked) HookSaves();
  s_Budget.Reset();
  float now = s_World.GetWorldTime() * 0.001;
  bool busy;
  while (!s_Orphans.IsEmpty() && s_Budget.Deletes > 0)
  {
   IEntity orphan = s_Orphans[0];
   s_Orphans.RemoveOrdered(0);
   if (Delete(orphan)) s_Budget.Deletes--;
  }
  int count = s_Zones.Count();
  if (count > 0)
  {
   // Rotate the first visit so one zone cannot keep the single spawn allowance.
   int start = s_Cursor % count;
   for (int visit = 0; visit < count; visit++)
   {
    EAU_ProtestZone zone = s_Zones[(start + visit) % count];
    if (zone && zone.Step(now, s_Budget)) busy = true;
   }
   s_Cursor = (start + 1) % count;
  }
  if (!busy && s_Orphans.IsEmpty()) Doze();
 }

 //------------------------------------------------------------------------------------------------
 // Players
 // A Game Master counts only through the character they control; the editor
 // camera never wakes a crowd. Dead bodies do not count.
 static array<IEntity> Players(float now)
 {
		EXPBG_LazyStatics_EAU_Director();
  if (now < s_NextPlayers) return s_Players;
  s_NextPlayers = now + PLAYER_REFRESH_SECONDS;
  s_Players.Clear();
  PlayerManager manager = GetGame().GetPlayerManager();
  if (manager)
  {
   array<int> ids = {};
   manager.GetPlayers(ids);
   foreach (int id : ids)
   {
    SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(manager.GetPlayerControlledEntity(id));
    if (!character) continue;
    CharacterControllerComponent controller = character.GetCharacterController();
    if (!controller || controller.IsDead() || controller.GetLifeState() == ECharacterLifeState.DEAD) continue;
    s_Players.Insert(character);
   }
  }
  foreach (IEntity observer : s_TestObservers)
  {
   if (observer) s_Players.Insert(observer);
  }
  return s_Players;
 }

 // Horizontal distance to the nearest player character, or -1 when there is none.
 static float NearestPlayerDistance(vector origin, float now)
 {
  float nearest = -1;
  array<IEntity> players = Players(now);
  foreach (IEntity player : players)
  {
   if (!player) continue;
   float distance = vector.DistanceXZ(player.GetOrigin(), origin);
   if (nearest < 0 || distance < nearest) nearest = distance;
  }
  return nearest;
 }

 // Fixture seam: these entities count as player characters until replaced. The
 // module never calls it; null or an empty list removes them.
 static void SetTestObservers(array<IEntity> observers)
 {
		EXPBG_LazyStatics_EAU_Director();
  s_TestObservers.Clear();
  if (observers)
  {
   foreach (IEntity observer : observers)
   {
    if (observer) s_TestObservers.Insert(observer);
   }
  }
  s_NextPlayers = 0;
 }

 static bool HasCrowdCapacity()
 {
		EXPBG_LazyStatics_EAU_Director();
  int tracked;
  foreach (EAU_ProtestZone zone : s_Zones)
  {
   if (zone) tracked += zone.GetTrackedCount();
  }
  return tracked < MAX_PROTESTERS;
 }

 //------------------------------------------------------------------------------------------------
 // World end
 protected static void HookGameEnd()
 {
  if (s_GameEndHooked) return;
  SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  if (!gameMode) return;
  gameMode.GetOnGameEnd().Insert(OnGameEnd);
  s_GameEndHooked = true;
 }

 protected static void OnGameEnd()
 {
		EXPBG_LazyStatics_EAU_Director();
  if (!GetGame() || !s_World || GetGame().GetWorld() != s_World) return;
  s_Ended = true;
  // The session is over: remove every crowd now instead of a few per tick.
  foreach (EAU_ProtestZone zone : s_Zones)
  {
   if (zone) zone.EndSession();
  }
  foreach (IEntity orphan : s_Orphans) Delete(orphan);
  s_Orphans.Clear();
  Doze();
  Print("[EAU] Game ended: protest crowds removed");
 }

 //------------------------------------------------------------------------------------------------
 // Saves
 // Vanilla 1.8 persistence saves what IsTracked reports; GetId can still answer
 // for an entity StopTracking released, so it proves nothing. Right before each
 // save reads its data, nothing a zone answers for stays tracked, even when
 // tracking began late or another system started it again, and every zone is.
 protected static void HookSaves()
 {
  if (s_SaveHooked || !s_World) return;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByCurrentWorld();
  if (!persistence) return;
  persistence.GetOnBeforeSave().Insert(OnPersistenceBeforeSave);
  s_SaveHooked = true;
 }

 protected static void OnPersistenceBeforeSave(ESaveGameType saveType)
 {
		EXPBG_LazyStatics_EAU_Director();
  if (!GetGame() || !s_World || GetGame().GetWorld() != s_World || !Replication.IsServer()) return;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByCurrentWorld();
  if (!persistence) return;
  Compact();
  // Bounded: MAX_PROTESTERS members, a group and a sound per zone, MAX_ORPHANS.
  array<IEntity> crowd = {};
  foreach (EAU_ProtestZone zone : s_Zones)
  {
   if (zone) zone.GetCrowdEntities(crowd);
  }
  foreach (IEntity orphan : s_Orphans)
  {
   if (orphan) crowd.Insert(orphan);
  }
  int released, refused;
  foreach (IEntity entity : crowd)
  {
   if (!entity || IsPlayerCharacter(entity)) continue;
   if (persistence.IsTracked(entity)) released++;
   KeepOutOfSaves(entity);
   if (persistence.IsTracked(entity)) refused++;
  }
  int zonesTracked;
  if (persistence.GetState() == EPersistenceSystemState.ACTIVE)
  {
   foreach (EAU_ProtestZone kept : s_Zones)
   {
    if (kept && !persistence.IsTracked(kept) && persistence.StartTracking(kept, false)) zonesTracked++;
   }
  }
  string type = typename.EnumToString(ESaveGameType, saveType);
  if (refused > 0) PrintFormat("[EAU] Save %1: %2 protest crowd entities refused to stop tracking", type, refused, level: LogLevel.WARNING);
  if (released > refused || zonesTracked > 0) PrintFormat("[EAU] Save %1: %2 late-tracked crowd entities kept out, %3 zones tracked", type, released - refused, zonesTracked);
 }

 //------------------------------------------------------------------------------------------------
 // Entity helpers
 // A possessed or player-controlled body is never this module's to delete.
 static bool IsPlayerCharacter(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character) return false;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (controller && controller.IsPlayerControlled()) return true;
  return SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(character) != 0;
 }

 static bool Delete(IEntity entity)
 {
  if (!entity || IsPlayerCharacter(entity)) return false;
  SCR_EntityHelper.DeleteEntityAndChildren(entity);
  return true;
 }

 // Transient crowd entities stay out of Game Master saves (CDF reads the editable
 // flag) and out of vanilla 1.8 persistence (which tracks entities, not flags).
 // The zone itself is saved with its settings and rebuilds a fresh crowd on load.
 // OnPersistenceBeforeSave repeats this for every crowd entity before each save.
 static void KeepOutOfSaves(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (editable && !editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE)) editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (persistence) persistence.StopTracking(entity);
 }

 // A civilian a Game Master took over becomes an ordinary saved entity again.
 static void ReturnToSaves(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (editable && editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE)) editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false);
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (persistence) persistence.StartTracking(entity);
 }

 // Unit Caching must never enroll, cache or regroup a protest crowd.
 static bool ReservesGroup(SCR_AIGroup group)
 {
		EXPBG_LazyStatics_EAU_Director();
  if (!group || !Replication.IsServer()) return false;
  foreach (EAU_ProtestZone zone : s_Zones)
  {
   if (zone && zone.OwnsGroup(group)) return true;
  }
  return s_Orphans.Contains(group);
 }

 static int GetZoneCount() {
		EXPBG_LazyStatics_EAU_Director(); Compact(); return s_Zones.Count(); }
 static int GetOrphanCount() {
		EXPBG_LazyStatics_EAU_Director(); return s_Orphans.Count(); }
 static bool IsScheduled() { return s_Scheduled; }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAU_Director()
	{
		if (!s_Zones)
			s_Zones = new array<EAU_ProtestZone>();
		if (!s_Orphans)
			s_Orphans = new array<IEntity>();
		if (!s_Budget)
			s_Budget = new EAU_Budget();
		if (!s_Players)
			s_Players = new array<IEntity>();
		if (!s_TestObservers)
			s_TestObservers = new array<IEntity>();
	}
}
