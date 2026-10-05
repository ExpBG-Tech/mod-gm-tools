// Doorsteps must be real doors. A bounding-box face is not an opening: campaign
// integrated-qa-0915-13 showed residents reliably reaching a wall face after the
// footprint fix (door=40/104) and then failing to walk out through it
// (emerged=1/8), because there was no door where they were standing.
//
// Doors are discovered once per home with a single bounded native query and
// cached for the mission. This is not a world scan: at most one query per
// household, at most one per shared tick, bounded result count, and the cache is
// dropped when the world changes.
class EAC_DoorIndex
{
 protected static const int MAX_HOMES = 512;
 protected static const int MAX_DOORS = 8;
 protected static const float CHARACTER_WIDTH = 0.6;
 protected static ref map<IEntity, ref array<vector>> s_Doors = new map<IEntity, ref array<vector>>();
 protected static BaseWorld s_World;
 protected static ref array<vector> s_Collected;
 protected static float s_NextQuery;
 protected static int s_Evicted;

 protected static void CheckWorld()
 {
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  // Drop previous mission records only; never touch entities belonging to it.
  s_Doors.Clear(); s_World = world; s_Collected = null; s_NextQuery = 0; s_Evicted = 0;
 }

 // Native query callback. Returning false ends the query once the bounded
 // result list is full.
 protected static bool Collect(IEntity entity)
 {
  if (!s_Collected) return false;
  if (s_Collected.Count() >= MAX_DOORS) return false;
  if (!entity) return true;
  BaseDoorComponent door = BaseDoorComponent.Cast(entity.FindComponent(BaseDoorComponent));
  if (!door) return true;
  // A window or a jammed door is not an exit.
  if (!door.CanCharacterPass(CHARACTER_WIDTH)) return true;
  s_Collected.Insert(door.GetDoorPivotPointWS());
  return true;
 }

 // Returns the cached door pivots for this home, discovering them at most once.
 // Null means "not known yet"; an empty array means "queried, none found".
 static array<vector> Get(IEntity home, float now)
 {
  if (!home || !GetGame()) return null;
  CheckWorld();
  array<vector> cached = s_Doors.Get(home);
  if (cached) return cached;
  if (s_Doors.Count() >= MAX_HOMES) return null;
  // One discovery per shared tick across the whole mission.
  if (now < s_NextQuery) return null;
  s_NextQuery = now + 0.5;
  BaseWorld world = home.GetWorld();
  if (!world) return null;
  vector mins, maxs; home.GetBounds(mins, maxs);
  float radius = Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]) * 0.5 + 4;
  radius = Math.Clamp(radius, 5, 40);
  s_Collected = {};
  world.QueryEntitiesBySphere(home.GetOrigin(), radius, Collect, null, EQueryEntitiesFlags.ALL);
  array<vector> found = s_Collected;
  s_Collected = null;
  s_Doors.Insert(home, found);
  return found;
 }

 // The old hard stop at MAX_HOMES routed every home past the cap to the
 // bounding-box fallback for the rest of the session - the degradation this
 // file's own header blames for poor emergence. Drop entries whose building has
 // been removed, then the ones no observer is near.
 // The scene survey's sphere is a superset of this file's, so it can hand over
 // the pivots it already found and spare this index its own query entirely.
 // Filtered to this file's own radius so a neighbour's door is never attributed.
 static void Publish(IEntity home, array<vector> doors)
 {
  if (!Replication.IsServer() || !home || !doors) return;
  CheckWorld();
  if (s_Doors.Contains(home) || s_Doors.Count() >= MAX_HOMES) return;
  vector mins, maxs; home.GetBounds(mins, maxs);
  float radius = Math.Clamp(Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]) * 0.5 + 4, 5, 40);
  array<vector> kept = {};
  foreach (vector pivot : doors)
  {
   if (kept.Count() >= MAX_DOORS) break;
   if (vector.Distance(pivot, home.GetOrigin()) <= radius) kept.Insert(pivot);
  }
  s_Doors.Insert(home, kept);
 }

 static int Evict(array<IEntity> observers, int sleepDistance)
 {
  CheckWorld();
  array<IEntity> stale = {};
  foreach (IEntity home, array<vector> doors : s_Doors)
  {
   if (!home) { stale.Insert(home); continue; }
   if (!observers || observers.IsEmpty()) continue;
   bool near = false;
   foreach (IEntity observer : observers)
   {
    if (!observer) { near = true; break; }
    if (vector.Distance(observer.GetOrigin(), home.GetOrigin()) <= sleepDistance) { near = true; break; }
   }
   if (!near) stale.Insert(home);
  }
  foreach (IEntity drop : stale) s_Doors.Remove(drop);
  s_Evicted += stale.Count();
  return stale.Count();
 }

 static int GetEvictedCount() { CheckWorld(); return s_Evicted; }

 static int CountKnownHomes() { CheckWorld(); return s_Doors.Count(); }
}
