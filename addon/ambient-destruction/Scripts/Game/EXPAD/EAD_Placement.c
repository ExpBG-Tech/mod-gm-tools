class EAD_Placement
{
 static bool Ground(BaseWorld world, vector point, out vector ground)
 {
  TraceParam trace = new TraceParam();
  float height = world.GetSurfaceY(point[0], point[2]);
  trace.Start = Vector(point[0], height + 20, point[2]);
  trace.End = Vector(point[0], height - 3, point[2]);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  float fraction = world.TraceMove(trace, null);
  if (fraction >= 1 || fraction < 0) return false;
  if (trace.TraceEnt && SCR_DestructibleBuildingEntity.Cast(trace.TraceEnt)) return false;
  ground = trace.Start + (trace.End - trace.Start) * fraction;
  return !ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, ground - "0 0.05 0");
 }
 // Road-dependent props fail closed on maps/areas without a road network.
 // One nearest road and at most 512 segments per existing generation token.
 static bool RoadPoint(vector point, float extent, int lane, out vector position, out vector direction)
 {
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!ai || !ai.GetRoadNetworkManager()) return false;
  BaseRoad road;
  float distance;
  ai.GetRoadNetworkManager().GetClosestRoad(point, road, distance, true);
  if (!road || distance > 90) return false;
  array<vector> points = {};
  road.GetPoints(points);
  if (points.Count() < 2 || points.Count() > 513) return false;
  float best = 8100;
  bool found;
  for (int i = 1; i < points.Count(); i++)
  {
   vector delta = points[i] - points[i - 1]; delta[1] = 0;
   float lengthSq = delta.LengthSq();
   if (lengthSq < 0.01) continue;
   vector offset = point - points[i - 1]; offset[1] = 0;
   float along = Math.Clamp(vector.Dot(offset, delta) / lengthSq, 0, 1);
   vector candidate = points[i - 1] + delta * along;
   float separation = EAD_Policy.DistanceSq(point, candidate);
   if (separation >= best) continue;
   best = separation; position = candidate;
   direction = delta.Normalized(); found = true;
  }
  if (!found) return false;
  // Every third candidate is on the road; others use either shoulder.
  if (lane != 0)
  {
   float side = 1;
   if (lane == 2) side = -1;
   position += Vector(direction[2], 0, -direction[0]) * side * (road.GetWidth() * 0.5 + extent * 0.45);
  }
  return true;
 }
 static EAD_PropRecord Candidate(EAD_Zone zone, EAD_Random random, int asset, bool grouped = false, vector groupOrigin = vector.Zero, int lane = 0)
 {
  BaseWorld world = zone.GetWorld();
  float angle = random.Next() * Math.PI2;
  float distance = Math.Sqrt(random.Next()) * zone.Radius;
  vector point = zone.GetOrigin() + Vector(Math.Cos(angle) * distance, 0, Math.Sin(angle) * distance);
  float extent = EAD_Catalog.Extent(asset);
  bool wreck = EAD_Catalog.IsWreck(asset) || asset == 34 || asset == 35;
  vector direction, roadPosition;
  bool roadAligned;
  if (wreck)
  {
   roadAligned = RoadPoint(point, extent, lane, roadPosition, direction);
   if (!roadAligned) return null;
   point = roadPosition;
  }
  else if (grouped)
  {
   distance = 3 + random.Next() * 2;
   point = groupOrigin + Vector(Math.Cos(angle) * distance, 0, Math.Sin(angle) * distance);
  }
  vector ground;
  if (!Ground(world, point, ground)) return null;
  if (EAD_Catalog.IsSeated(asset))
  {
   EAD_PropRecord seated = WallCandidate(zone, asset, ground, angle);
   if (seated && grouped && EAD_Policy.DistanceSq(seated.Transform[3], groupOrigin) > 36) return null;
   return seated;
  }
  // Prone/burned bodies also need scene context; a group anchor alone is not support.
  if ((asset == 5 || asset == 7) && !BodyContext(zone, ground, angle)) return null;
  return GroundRecord(zone, asset, ground, direction, roadAligned, random.Next() * 360);
 }
 static bool BodyContext(EAD_Zone zone, vector ground, float angle)
 {
  // Planned static wrecks count even while cached. Test physical edges, not just origins.
  foreach (EAD_PropRecord record : zone.Records)
  {
   if (record.Suppressed || !EAD_Catalog.IsWreck(record.Asset)) continue;
   float reach = EAD_Catalog.Extent(record.Asset) + 6;
   if (EAD_Policy.DistanceSq(ground, record.Transform[3]) <= reach * reach) return true;
  }
  // Bounded rays find a real structure, wall or ruin within six metres.
  for (int ray = 0; ray < 8; ray++)
  {
   float heading = angle + ray * Math.PI2 / 8;
   TraceParam trace = new TraceParam();
   trace.Start = ground + "0 0.6 0";
   trace.End = trace.Start + Vector(Math.Cos(heading), 0, Math.Sin(heading)) * 6;
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = zone;
   float hit = zone.GetWorld().TraceMove(trace, null);
   if (hit > 0 && hit < 1 && StructuralWall(trace.TraceEnt)) return true;
  }
  return false;
 }
 static EAD_PropRecord GroundRecord(EAD_Zone zone, int asset, vector ground, vector direction, bool aligned, float angle, EAD_PropRecord companion = null)
 {
  BaseWorld world = zone.GetWorld();
  float extent = EAD_Catalog.Extent(asset);
  if (Math.Sqrt(EAD_Policy.DistanceSq(ground, zone.GetOrigin())) + extent > zone.Radius) return null;
  EAD_PropRecord record = new EAD_PropRecord();
  record.Asset = asset;
  Math3D.AnglesToMatrix(Vector(angle, 0, 0), record.Transform);
  if (aligned) Math3D.DirectionAndUpMatrix(direction, vector.Up, record.Transform);
  vector mins, maxs;
  EAD_Catalog.Bounds(asset, mins, maxs);
  record.Transform[3] = ground - Vector(0, mins[1], 0);
  vector support;
  for (int i = 0; i < 4; i++)
  {
   float x = mins[0]; float z = mins[2];
   if (i >= 2) x = maxs[0];
   if (Math.Mod(i, 2) == 1) z = maxs[2];
   vector offset = record.Transform[0] * x + record.Transform[2] * z;
   if (!Ground(world, ground + offset, support) || Math.AbsFloat(support[1] - ground[1]) > 0.3) return null;
  }
  TraceOBB box = new TraceOBB();
  box.Start = record.Transform[3]; box.Mins = mins; box.Maxs = maxs;
  box.Mins[1] = mins[1] + 0.1;
  box.Maxs[1] = Math.Max(maxs[1], box.Mins[1] + 0.1);
  for (int axis = 0; axis < 3; axis++) box.Mat[axis] = record.Transform[axis];
  box.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  box.Exclude = zone;
  if (world.TracePosition(box, null) < 0) return null;
  if (EAD_World.Reserved(ground, extent, companion)) return null;
  return record;
 }
 static bool StructuralWall(IEntity entity)
 {
  if (!entity || ChimeraCharacter.Cast(entity) || Vehicle.Cast(entity)) return false;
  Physics physics = entity.GetPhysics();
  if (!physics || physics.IsDynamic()) return false;
  EntityPrefabData data = entity.GetPrefabData();
  if (!data) return false;
  string path = data.GetPrefabName(); path.ToLower();
  if (path.Contains("fence") || path.Contains("railing") || path.Contains("gate") || path.Contains("/buildingparts/")) return false;
  return SCR_DestructibleBuildingEntity.Cast(entity) || path.Contains("/structures/") || path.Contains("/walls/");
 }
 // Building sites exclude perimeter fences, individual fittings and street props.
 static bool BuildingContext(IEntity entity)
 {
  if (!entity || !entity.GetPrefabData() || !StructuralWall(entity)) return false;
  string path = entity.GetPrefabData().GetPrefabName(); path.ToLower();
  if (path.Contains("fence") || path.Contains("/walls/") || path.Contains("/buildingparts/") || path.Contains("/buildingaddons/") || path.Contains("/furniture/")) return false;
  IEntity parent = entity.GetParent();
  if (parent && SCR_DestructibleBuildingEntity.Cast(parent)) return false;
  return SCR_DestructibleBuildingEntity.Cast(entity) || path.Contains("/houses/") || path.Contains("/commercial/") || path.Contains("/industrial/") || path.Contains("/military/");
 }
 static EAD_PropRecord AtBuilding(EAD_Zone zone, EAD_Random random, int asset, EAD_BodySite site)
 {
  float along = random.Next();
  float gap = 1.5 + random.Next() * 2.5;
  int side = Math.Min(3, Math.Floor(random.Next() * 4));
  vector point = site.Min;
  if (side < 2)
  {
   point[0] = point[0] + (site.Max[0] - site.Min[0]) * along;
   point[2] = point[2] - gap;
   if (side == 1) point[2] = site.Max[2] + gap;
  }
  else
  {
   point[2] = point[2] + (site.Max[2] - site.Min[2]) * along;
   point[0] = point[0] - gap;
   if (side == 3) point[0] = site.Max[0] + gap;
  }
  vector ground;
  if (!Ground(zone.GetWorld(), point, ground)) return null;
  float angle = random.Next() * Math.PI2;
  if (EAD_Catalog.IsSeated(asset)) return WallCandidate(zone, asset, ground, angle);
  if (!BodyContext(zone, ground, angle)) return null;
  return GroundRecord(zone, asset, ground, vector.Zero, false, random.Next() * 360);
 }
 // Two model families have opposite authored facing axes. Bounds come from their XOB headers.
 static void SeatedBounds(int asset, out vector mins, out vector maxs, out float facing)
 {
  mins = "-0.331 -0.010 -0.996"; maxs = "0.487 0.848 0.298"; facing = -1;
  if (asset == 4 || asset == 6)
  {
   mins = "-0.349 -0.018 -0.185"; maxs = "0.401 0.852 1.027"; facing = 1;
  }
 }
 // At most eight short rays per token; no wall means no seated soldier.
 static EAD_PropRecord WallCandidate(EAD_Zone zone, int asset, vector point, float angle)
 {
  BaseWorld world = zone.GetWorld();
  for (int ray = 0; ray < 8; ray++)
  {
   float heading = angle + ray * Math.PI2 / 8;
   vector toward = Vector(Math.Cos(heading), 0, Math.Sin(heading));
   TraceParam wall = new TraceParam();
   wall.Start = point + "0 0.6 0"; wall.End = wall.Start + toward * 16;
   wall.Flags = TraceFlags.WORLD | TraceFlags.ENTS; wall.Exclude = zone;
   float hit = world.TraceMove(wall, null);
   if (hit <= 0 || hit >= 1 || !StructuralWall(wall.TraceEnt) || Math.AbsFloat(wall.TraceNorm[1]) > 0.15) continue;
   if (EAD_World.WallMayCollapse(wall.TraceEnt)) continue;
   vector normal = wall.TraceNorm; normal[1] = 0; normal.Normalize();
   if (vector.Dot(normal, toward) > 0) normal = -normal;
   vector contact = wall.Start + (wall.End - wall.Start) * hit;
   vector mins, maxs; float facing;
   SeatedBounds(asset, mins, maxs, facing);
   float back = -mins[2];
   if (facing < 0) back = maxs[2];
   vector ground;
   if (!Ground(world, contact + normal * (back + 0.06), ground)) continue;
   EAD_PropRecord record = new EAD_PropRecord(); record.Asset = asset;
   Math3D.DirectionAndUpMatrix(normal * facing, vector.Up, record.Transform);
   record.Transform[3] = ground - Vector(0, mins[1], 0);
   if (Math.Sqrt(EAD_Policy.DistanceSq(ground, zone.GetOrigin())) + EAD_Catalog.Extent(asset) > zone.Radius) continue;
   if (EAD_World.Reserved(ground, EAD_Catalog.Extent(asset))) continue;
   // A 3 x 3 patch rejects rail fences, holes, poles and unsupported shoulders.
   bool supported = true;
   for (int probe = 0; probe < 9; probe++)
   {
    TraceParam support = new TraceParam();
    vector side = record.Transform[0] * ((Math.Mod(probe, 3) - 1) * 0.3);
    float height = 0.25 + (probe / 3) * 0.25;
    support.Start = ground + Vector(0, height, 0) + side + normal * 0.1;
    support.End = support.Start - normal * (back + 0.3);
    support.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    float fraction = world.TraceMove(support, null);
    if (fraction <= 0 || fraction >= 1 || support.TraceEnt != wall.TraceEnt || vector.Dot(support.TraceNorm, normal) < 0.95) { supported = false; break; }
   }
   if (!supported) continue;
   // Probe the foot/seat footprint in its actual orientation, staying outside the wall.
   for (int corner = 0; corner < 4; corner++)
   {
    float x = mins[0]; float z = mins[2];
    if (corner >= 2) x = maxs[0];
    if (Math.Mod(corner, 2) == 1) z = maxs[2];
    vector sample = ground + record.Transform[0] * x + record.Transform[2] * z;
    vector floor;
    if (!Ground(world, sample, floor) || Math.AbsFloat(floor[1] - ground[1]) > 0.12) { supported = false; break; }
   }
   if (!supported) continue;
   TraceOBB volume = new TraceOBB();
   for (int axis = 0; axis < 3; axis++) volume.Mat[axis] = record.Transform[axis];
   volume.Start = record.Transform[3]; volume.Mins = mins; volume.Mins[1] = 0.12; volume.Maxs = maxs;
   volume.Flags = TraceFlags.WORLD | TraceFlags.ENTS; volume.Exclude = zone;
   if (world.TracePosition(volume, null) < 0) continue;
   return record;
  }
  return null;
 }
 static EAD_PropRecord Dressing(EAD_Zone zone, EAD_Random random, EAD_PropRecord body)
 {
  int asset = 36;
  if (random.Next() < 0.5) asset = 37 + Math.Min(3, Math.Floor(random.Next() * 4));
  vector mins, maxs; float facing;
  SeatedBounds(body.Asset, mins, maxs, facing);
  float side = 1;
  if (random.Next() < 0.5) side = -1;
  vector point = body.Transform[3] + body.Transform[0] * side * 1.8 + body.Transform[2] * facing * 1.5;
  vector ground;
  if (!Ground(zone.GetWorld(), point, ground)) return null;
  return GroundRecord(zone, asset, ground, vector.Zero, false, random.Next() * 360, body);
 }
 static bool WallStillPresent(BaseWorld world, EAD_PropRecord record)
 {
  return WallSupport(world, record) == 1;
 }
 // -1 means a transient obstruction; it is not evidence that the wall vanished.
 static int WallSupport(BaseWorld world, EAD_PropRecord record, IEntity exclude = null)
 {
  vector mins, maxs; float facing;
  SeatedBounds(record.Asset, mins, maxs, facing);
  vector normal = record.Transform[2] * facing;
  float back = -mins[2];
  if (facing < 0) back = maxs[2];
  for (int probe = 0; probe < 9; probe++)
  {
   TraceParam wall = new TraceParam();
   wall.Exclude = exclude;
   float height = mins[1] + 0.25 + (probe / 3) * 0.25;
   wall.Start = record.Transform[3] + Vector(0, height, 0) + record.Transform[0] * ((Math.Mod(probe, 3) - 1) * 0.3) + normal * 0.1;
   wall.End = wall.Start - normal * (back + 0.3);
   wall.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   float fraction = world.TraceMove(wall, null);
   if (wall.TraceEnt)
   {
    Physics physics = wall.TraceEnt.GetPhysics();
    if (ChimeraCharacter.Cast(wall.TraceEnt) || Vehicle.Cast(wall.TraceEnt) || (physics && physics.IsDynamic())) return -1;
   }
   if (fraction <= 0 || fraction >= 1 || !StructuralWall(wall.TraceEnt) || vector.Dot(wall.TraceNorm, normal) < 0.95) return 0;
  }
  return 1;
 }
}
