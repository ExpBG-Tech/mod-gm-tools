// Per-tick work allowance shared by every protest zone.
class EAU_Budget
{
 int Spawns;
 int Deletes;
 int Gestures;
 int Probes;

 void Reset()
 {
  Spawns = EAU_Director.SPAWNS_PER_TICK;
  Deletes = EAU_Director.DELETES_PER_TICK;
  Gestures = EAU_Director.GESTURES_PER_TICK;
  Probes = EAU_Director.PROBES_PER_TICK;
 }
}

// One server call queue for every protest zone in the current world. Spawning,
// removal and gestures are bounded per tick across all zones; the queue sleeps
// while no zone has work and wakes on a GM setting change or a deleted zone.
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
 static const int MAX_ORPHANS = 512;
 protected static ref array<EAU_ProtestZone> s_Zones = {};
 // Entities of deleted zones, removed a few per tick.
 protected static ref array<IEntity> s_Orphans = {};
 protected static ref EAU_Budget s_Budget = new EAU_Budget();
 protected static BaseWorld s_World;
 protected static bool s_Scheduled;
 protected static bool s_Ended;
 protected static bool s_GameEndHooked;
 protected static bool s_CapWarned;
 protected static int s_Cursor;

 // Static records never outlive their world; the entities went with it.
 protected static void CheckWorld()
 {
  if (!GetGame()) return;
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  GetGame().GetCallqueue().Remove(Tick);
  s_Zones.Clear();
  s_Orphans.Clear();
  s_World = world;
  s_Scheduled = false; s_Ended = false; s_GameEndHooked = false; s_CapWarned = false;
  s_Cursor = 0;
 }

 static bool Register(EAU_ProtestZone zone)
 {
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
  Wake();
  return true;
 }

 static void Unregister(EAU_ProtestZone zone, array<IEntity> leftovers)
 {
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
  for (int i = s_Zones.Count() - 1; i >= 0; i--)
  {
   if (!s_Zones[i]) s_Zones.RemoveOrdered(i);
  }
 }

 protected static void Tick()
 {
  if (!GetGame()) return;
  if (!s_World || GetGame().GetWorld() != s_World) { CheckWorld(); return; }
  Compact();
  // A world-authored zone can initialise before the game mode exists.
  if (!s_GameEndHooked) HookGameEnd();
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

 static bool HasCrowdCapacity()
 {
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
  if (!group || !Replication.IsServer()) return false;
  foreach (EAU_ProtestZone zone : s_Zones)
  {
   if (zone && zone.OwnsGroup(group)) return true;
  }
  return s_Orphans.Contains(group);
 }

 static int GetZoneCount() { Compact(); return s_Zones.Count(); }
 static int GetOrphanCount() { return s_Orphans.Count(); }
 static bool IsScheduled() { return s_Scheduled; }
}
