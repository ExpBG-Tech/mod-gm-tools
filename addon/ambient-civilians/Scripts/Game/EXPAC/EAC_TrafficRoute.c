// Bounded road endpoints; native vehicle AI owns routing between them.
class EAC_TrafficRoute
{
 static bool SupportedCar(ResourceName prefab)
 {
  // The authored S105 profiles match the conservative spawn envelope and boarding fixture.
  string path = prefab.GetPath(); path.Replace("\\", "/"); path.ToLower();
  // Every colour of the S105 is the same body; the S1203 minibus fits the same
  // 5.6 m clearance box. Randomised and base prefabs stay outside the validated
  // vehicle capability profiles.
  if (path.Contains("_base") || path.Contains("randomized")) return false;
  return path.StartsWith("prefabs/vehicles/wheeled/s105/s105_") || path.StartsWith("prefabs/vehicles/wheeled/s1203/s1203_transport_");
 }
 // `kerb` parks the point at the right-hand edge of the road instead of in the
 // middle of the lane: the body of a 1.7 m wide car then sits just inside the edge.
 static bool ProjectLane(array<vector> points, float width, vector near, vector goal, out vector position, out vector direction, bool kerb = false)
 {
  if (!points || points.Count() < 2 || points.Count() > 512 || width < 3 || width > 30) return false;
  float closest = float.MAX;
  for (int i = 1; i < points.Count(); i++)
  {
   vector delta = points[i] - points[i - 1]; delta[1] = 0;
   if (delta.LengthSq() < 0.01) continue;
   vector offset = near - points[i - 1]; offset[1] = 0;
   vector projected = points[i - 1] + delta * Math.Clamp(vector.Dot(offset, delta) / delta.LengthSq(), 0, 1);
   float distance = vector.DistanceSq(projected, near);
   if (distance >= closest) continue;
   closest = distance; position = projected; direction = delta.Normalized();
  }
  if (closest == float.MAX) return false;
  if (vector.Dot(direction, goal - position) < 0) direction = -direction;
  float lateral = Math.Min(1.5, width * 0.25);
  if (kerb) lateral = Math.Max(lateral, width * 0.5 - 1.0);
  position += Vector(direction[2], 0, -direction[0]) * lateral;
  return true;
 }

 static bool Find(BaseWorld world, vector from, vector to, out vector start, out vector destination, out vector direction, out string reason, out string measurements, bool kerb = false)
 {
  reason = "road_ai"; measurements = "";
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!ai) return false;
  reason = "road_manager";
  RoadNetworkManager roads = ai.GetRoadNetworkManager();
  if (!roads) return false;
  BaseRoad road; float distance;
  roads.GetClosestRoad(from, road, distance, true);
  reason = "road_nearest"; measurements = string.Format("road_distance=%1", distance);
  if (!road || distance > 120) return false;
  array<vector> points = {}; road.GetPoints(points);
  reason = "road_lane";
  measurements += string.Format(" points=%1 width=%2 goal_m=%3", points.Count(), road.GetWidth(), vector.Distance(from, to));
  if (!ProjectLane(points, road.GetWidth(), from, to, start, direction, kerb)) return false;
  reason = "road_reachable";
  if (!roads.GetReachableWaypointInRoad(start, to, 100, destination)) return false;
  reason = "road_length";
  measurements += string.Format(" reachable_m=%1 goal_error_m=%2 start=%3 end=%4 goal=%5", vector.Distance(start, destination), vector.Distance(destination, to), start, destination, to);
  if (vector.Distance(start, destination) < 250 || vector.Distance(start, destination) > 5000) return false;
  start[1] = world.GetSurfaceY(start[0], start[2]);
  destination[1] = world.GetSurfaceY(destination[0], destination[2]);
  reason = "road_exclusion";
  if (!EAC_ExclusionZone.IsPopulationAllowed(start) || !EAC_ExclusionZone.IsPopulationAllowed(destination) || !EAC_ExclusionZone.IsTransitAllowed(start, destination)) return false;
  reason = "road_ok";
  return true;
 }

 // Capped diagnostic count for the DebugLevel >= 2 clearance trace below.
 static int s_ClearTraces;

 static bool Clear(BaseWorld world, vector position, IEntity exclude = null)
 {
  if (!world || ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, position)) return false;
  // Callers pass a terrain-height position (GetSurfaceY or a cached car origin).
  // Where the road surface sits above the terrain - embankments, bridges, most
  // built-up roads - a box whose floor is 0.2 m above the terrain intersects the
  // road itself and every spot on that road reads "blocked": campaign 95's
  // cached party failed wake_clearance at its own cached spot and at every lane
  // projection for the whole of phase 7 (2026-09-17). Snap the floor to the
  // physical surface first, accepting only a hit within 0.6 m of the terrain so a
  // parked car's roof cannot masquerade as ground.
  vector ground = position;
  TraceParam ray = new TraceParam();
  ray.Start = position + Vector(0, 3, 0); ray.End = position - Vector(0, 3, 0);
  ray.Flags = TraceFlags.WORLD | TraceFlags.ENTS; ray.Exclude = exclude;
  float fraction = world.TraceMove(ray, null);
  if (fraction < 1)
  {
   float hit = ray.Start[1] - 6 * fraction;
   if (hit > position[1] && hit - position[1] <= 0.6) ground[1] = hit;
  }
  // Floor at 0.7 m: campaign 97's trace showed every refusal hitting
  // GenericTerrainEntity with zero lift, i.e. the 5.6 m wide box's uphill corners
  // dipping into ordinary road camber and slope (0.2 m over 2.8 m is a 4 degree
  // grade). 0.7 m tolerates a 14 degree grade and still crosses any car body,
  // character, fence or wall standing on the spot.
  TraceBox trace = new TraceBox();
  trace.Start = ground; trace.Mins = "-2.8 0.7 -2.8"; trace.Maxs = "2.8 2.3 2.8";
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = exclude;
  bool clear = world.TracePosition(trace, null) >= 0;
  if (!clear && s_ClearTraces < 40 && EAC_AmbientModule.GetDebugLevelMirror() >= 2)
  {
   s_ClearTraces++;
   string blocker = "world";
   if (trace.TraceEnt) blocker = trace.TraceEnt.ClassName();
   float lift = ground[1] - position[1];
   PrintFormat("[EAC TRAFFIC CLEAR] blocked at=%1 lift_m=%2 hit=%3", ground, lift, blocker);
  }
  return clear;
 }

 static bool Healthy(IEntity entity)
 {
  if (!entity) return false;
  DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
  if (!damage || damage.IsDestroyed() || damage.GetHealthScaled() < 0.999) return false;
  array<HitZone> zones = {}; damage.GetAllHitZonesInHierarchy(zones);
  if (zones.Count() > 128) return false;
  foreach (HitZone zone : zones)
   if (!zone || zone.GetHealth() < zone.GetMaxHealth() - 0.01) return false;
  return true;
 }
}
