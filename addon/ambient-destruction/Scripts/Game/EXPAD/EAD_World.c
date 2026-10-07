// One call queue for every zone, on each machine. Props never schedule work.
class EAD_World
{
 protected static ref array<EAD_Zone> s_Zones;
 protected static ref array<int> s_Players;
 protected static ref array<IEntity> s_Observers;
 protected static BaseWorld s_World;
 protected static bool s_Scheduled;
 protected static float s_NextProximity, s_NextBuilding;
 protected static int s_Cursor, s_BuildingCursor, s_SupportCursor;
 protected static int s_TickLastMs, s_TickPeakMs;
 static const int MAX_ZONES = 64;
 static const int MAX_LIVE = 1000;
 static void Register(EAD_Zone zone)
 {
		EXPBG_LazyStatics_EAD_World();
  if (!zone || !GetGame()) return;
  if (s_World != zone.GetWorld())
  {
   GetGame().GetCallqueue().Remove(Tick);
   s_Zones.Clear(); s_Players.Clear(); s_Observers.Clear();
   s_Scheduled = false; s_NextProximity = 0; s_NextBuilding = 0;
   s_Cursor = 0; s_BuildingCursor = 0; s_SupportCursor = 0; s_World = zone.GetWorld();
   s_TickLastMs = 0; s_TickPeakMs = 0;
   if (EAD_Snapshot.ImportWorld != s_World) EAD_Snapshot.Loading = false;
   EAD_Buildings.EnsureWorld(s_World);
  }
  if (s_Zones.Contains(zone) || s_Zones.Count() >= MAX_ZONES) return;
  s_Zones.Insert(zone);
  if (!s_Scheduled)
  {
   s_Scheduled = true;
   GetGame().GetCallqueue().CallLater(Tick, 100, true);
  }
 }
 static void Unregister(EAD_Zone zone)
 {
		EXPBG_LazyStatics_EAD_World();
  s_Zones.RemoveItem(zone);
  if (s_Zones.IsEmpty() && GetGame())
  {
   GetGame().GetCallqueue().Remove(Tick);
   s_Scheduled = false;
  }
 }
 static int ZoneCount() {
		EXPBG_LazyStatics_EAD_World(); return s_Zones.Count(); }
 static void GetZones(array<EAD_Zone> zones) {
		EXPBG_LazyStatics_EAD_World(); zones.Copy(s_Zones); }
 static int TickLastMs() { return s_TickLastMs; }
 static int TickPeakMs() { return s_TickPeakMs; }
 static bool WallMayCollapse(IEntity entity)
 {
		EXPBG_LazyStatics_EAD_World();
  for (int depth = 0; entity && depth < 16; depth++)
  {
   if (EAD_Buildings.IsSupported(entity))
   {
    foreach (EAD_Zone zone : s_Zones)
    {
     if (zone && EAD_Buildings.MayCollapse(entity, zone)) return true;
    }
    return false;
   }
   entity = entity.GetParent();
  }
  return false;
 }
 static int LiveCount()
 {
		EXPBG_LazyStatics_EAD_World();
  int count;
  foreach (EAD_Zone zone : s_Zones) { if (zone) count += zone.Live.Count(); }
  return count;
 }
 static bool Reserved(vector position, float extent, EAD_PropRecord companion = null)
 {
		EXPBG_LazyStatics_EAD_World();
  foreach (EAD_Zone zone : s_Zones)
  {
   if (!zone || !EAD_Policy.Near(position, zone.GetOrigin(), zone.Radius, extent + 4)) continue;
   foreach (EAD_PropRecord record : zone.Records)
   {
    if (record == companion) continue;
    float gap = extent + EAD_Catalog.Extent(record.Asset) + 0.5;
    if (EAD_Policy.DistanceSq(position, record.Transform[3]) < gap * gap) return true;
   }
  }
  return false;
 }
 static void Tick()
 {
		EXPBG_LazyStatics_EAD_World();
  if (!GetGame() || !s_World) return;
  for (int dead = s_Zones.Count() - 1; dead >= 0; dead--)
  {
   if (!s_Zones[dead]) s_Zones.RemoveOrdered(dead);
  }
  if (s_Zones.IsEmpty())
  {
   GetGame().GetCallqueue().Remove(Tick); s_Scheduled = false;
   return;
  }
  float now = s_World.GetWorldTime() * 0.001;
  int tickStarted = System.GetTickCount();
  bool authority = Replication.IsServer();
  if (authority)
  {
   if (s_SupportCursor >= s_Zones.Count()) s_SupportCursor = 0;
   EAD_Zone supportZone = s_Zones[s_SupportCursor++];
   if (supportZone) supportZone.SupportStep();
  }
  if (authority && now >= s_NextProximity)
  {
   s_NextProximity = now + 1;
   s_Players.Clear(); s_Observers.Clear();
   PlayerManager players = GetGame().GetPlayerManager();
   if (players)
   {
    players.GetPlayers(s_Players);
    foreach (int id : s_Players)
    {
     IEntity character = players.GetPlayerControlledEntity(id);
     if (character) s_Observers.Insert(character);
    }
   }
   foreach (EAD_Zone zone : s_Zones) { if (zone) zone.Proximity(s_Observers, now); }
  }
  int live = LiveCount();
  // At most eight reconciliation work items and four placement attempts globally.
  // Failed native operations consume work; idle/cooldown visits do not.
  int work, attempts;
  int nextWorkCursor = -1;
  int visits = s_Zones.Count();
  for (int visit = 0; visit < visits; visit++)
  {
   if (s_Cursor >= s_Zones.Count()) s_Cursor = 0;
   EAD_Zone zone = s_Zones[s_Cursor++];
   if (!zone) continue;
   zone.DiagnosticTick(now);
   if (authority && attempts < 4)
   {
    if (zone.Generate(now)) attempts++;
   }
   if (work < 8 && zone.Reconcile(now, live < MAX_LIVE))
   {
    work++; live = LiveCount();
    if (work == 8) nextWorkCursor = s_Cursor;
   }
  }
  // Finish diagnostics/generation for every zone, but resume reconciliation after
  // the last admitted item so persistent failures cannot starve later zones.
  if (nextWorkCursor >= 0) s_Cursor = nextWorkCursor;
  if (authority && now >= s_NextBuilding)
  {
   s_NextBuilding = now + 0.25;
   if (s_BuildingCursor >= s_Zones.Count()) s_BuildingCursor = 0;
   EAD_Zone buildingZone = s_Zones[s_BuildingCursor++];
   if (buildingZone) buildingZone.BuildingsStep();
  }
  s_TickLastMs = System.GetTickCount() - tickStarted;
  s_TickPeakMs = Math.Max(s_TickPeakMs, s_TickLastMs);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAD_World()
	{
		if (!s_Zones)
			s_Zones = new array<EAD_Zone>();
		if (!s_Players)
			s_Players = new array<int>();
		if (!s_Observers)
			s_Observers = new array<IEntity>();
	}
}
