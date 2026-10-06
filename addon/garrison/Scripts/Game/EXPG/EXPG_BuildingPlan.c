// Original EXPBG building sampling. No positions or implementation imported from GME.
class EXPG_BuildingNode
{
 vector Position;
 vector Look;
 // Window posts: metres to the watched window (or to the open view's exit).
 float Range = 1000;
 int Column;
 int Score;
 bool Entrance;
 bool Reachable;
 bool Interior;
 ref array<int> Links = {};
}

class EXPG_BuildingColumn
{
 ref array<int> Nodes = {};
}

class EXPG_BuildingOpening
{
 IEntity Part;
 vector Position;
 bool Door;
}

class EXPG_BuildingReservation
{
 IEntity Owner;
 int From;
 int To;
}

// A Full-cached guard has no body and no reservation, yet wakes on this spot.
class EXPG_ParkedPost
{
 EXPG_GarrisonRecord Owner;
 vector Position;
}

class EXPG_BuildingPlan
{
 static const float GRID = 0.75;
 static const int MAX_NODES = 8192;
 IEntity Structure;
 vector Origin;
 vector Angles;
 vector Mins;
 vector Maxs;
 ref array<ref EXPG_BuildingNode> Nodes = {};
 ref array<ref EXPG_BuildingColumn> Columns = {};
 ref array<int> Slots = {};
 ref array<bool> FixedSlots = {};
 bool Done;
 string Error;
 float LastUsed;
 protected int m_Width;
 protected int m_Depth;
 protected int m_Column;
 protected float m_Height;
 protected int m_Phase;
 protected int m_Node;
 protected int m_Edge;
 protected int m_Other;
 protected ref array<vector> m_Sentinels = {};
 protected ref array<vector> m_SentinelLooks = {};
 protected ref array<ref EXPG_BuildingOpening> m_Openings = {};
 protected ref array<IEntity> m_TraversableDoors = {};
 protected ref array<ref SCR_InteriorBoundingBox> m_InteriorBounds;
 protected ref array<ref EXPG_BuildingReservation> m_Reservations = {};
 protected ref array<ref EXPG_ParkedPost> m_Parked = {};

 // Fixed guards and stopped patrols occupy a node. One entry per live actor.
 bool ReserveNode(IEntity owner, int node)
 {
  return SetReservation(owner, node, node);
 }

 // Atomic replacement: failure preserves the owner's previous node/edge.
 bool TryReserveEdge(IEntity owner, int from, int to)
 {
  if (from < 0 || from >= Nodes.Count() || !Nodes[from].Links.Contains(to)) { return false; }
  return SetReservation(owner, from, to);
 }

 void ReleaseReservation(IEntity owner)
 {
  for (int i = m_Reservations.Count() - 1; i >= 0; i--)
  {
   if (!m_Reservations[i].Owner || m_Reservations[i].Owner == owner) { m_Reservations.RemoveOrdered(i); }
  }
 }

 protected bool SetReservation(IEntity owner, int from, int to)
 {
  if (!owner || from < 0 || to < 0 || from >= Nodes.Count() || to >= Nodes.Count() || !Nodes[from].Reachable || !Nodes[to].Reachable) { return false; }
  ReleaseReservation(null);
  // Parks keep moving patrols (edges) off a Full-cached garrison's wake spots.
  // A node claim (post, stop or wake) was spaced from them when it was chosen;
  // rechecking it lets slight drift hold a wake until that neighbour wakes.
  if (from != to)
  {
   foreach (EXPG_ParkedPost parked : m_Parked)
   {
    if (RoutesConflict(Nodes[from].Position, Nodes[to].Position, parked.Position, parked.Position)) { return false; }
   }
  }
  EXPG_BuildingReservation existingReservation;
  foreach (EXPG_BuildingReservation reservation : m_Reservations)
  {
   if (reservation.Owner == owner) { existingReservation = reservation; continue; }
   if (RoutesConflict(Nodes[from].Position, Nodes[to].Position, Nodes[reservation.From].Position, Nodes[reservation.To].Position)) { return false; }
  }
  if (!existingReservation)
  {
   // One entry per live guard. Every Add Garrison on a building shares this
   // plan, so the building may hold more than one squad's 32 guards.
   existingReservation = new EXPG_BuildingReservation();
   existingReservation.Owner = owner;
   m_Reservations.Insert(existingReservation);
  }
  existingReservation.From = from;
  existingReservation.To = to;
  return true;
 }

 // Several garrisons can share a building. While one is Full-cached, patrols of
 // the others keep off its posts so the survivors can wake there.
 void Park(EXPG_GarrisonRecord owner, vector point)
 {
  if (!owner) { return; }
  EXPG_ParkedPost parked = new EXPG_ParkedPost();
  parked.Owner = owner;
  parked.Position = point;
  m_Parked.Insert(parked);
 }

 void Unpark(EXPG_GarrisonRecord owner)
 {
  for (int i = m_Parked.Count() - 1; i >= 0; i--)
  {
   if (!m_Parked[i].Owner || m_Parked[i].Owner == owner) { m_Parked.RemoveOrdered(i); }
  }
 }

 // An added squad's post must not overlap a live guard's node or patrol edge.
 bool ReservationConflict(vector point)
 {
  foreach (EXPG_BuildingReservation reservation : m_Reservations)
  {
   if (!reservation.Owner) { continue; }
   if (RoutesConflict(point, point, Nodes[reservation.From].Position, Nodes[reservation.To].Position)) { return true; }
  }
  return false;
 }

 // Conservative swept bounds include body width and allowed patrol drift.
 // ponytail: rotated/diagonal edges may wait unnecessarily; use exact swept
 // segment distance only if native acceptance shows this harms indoor traffic.
 static bool RoutesConflict(vector aFrom, vector aTo, vector bFrom, vector bTo)
 {
  if (Math.Max(aFrom[0], aTo[0]) + 0.8 < Math.Min(bFrom[0], bTo[0]) || Math.Max(bFrom[0], bTo[0]) + 0.8 < Math.Min(aFrom[0], aTo[0])) { return false; }
  if (Math.Max(aFrom[2], aTo[2]) + 0.8 < Math.Min(bFrom[2], bTo[2]) || Math.Max(bFrom[2], bTo[2]) + 0.8 < Math.Min(aFrom[2], aTo[2])) { return false; }
  return Math.Max(aFrom[1], aTo[1]) + 2.0 >= Math.Min(bFrom[1], bTo[1]) && Math.Max(bFrom[1], bTo[1]) + 2.0 >= Math.Min(aFrom[1], aTo[1]);
 }

 void Begin(IEntity building)
 {
  Structure = building;
  if (!building) { Fail("Structure no longer exists"); return; }
  Origin = building.GetOrigin();
  Angles = building.GetAngles();
  building.GetBounds(Mins, Maxs);
  m_Width = Math.Ceil((Maxs[0] - Mins[0]) / GRID);
  m_Depth = Math.Ceil((Maxs[2] - Mins[2]) / GRID);
  if (m_Width < 1 || m_Depth < 1 || m_Width * m_Depth > 6144 || Maxs[1] - Mins[1] > 60)
  { Fail("Structure exceeds the supported sampling bounds"); return; }
  for (int i = 0; i < m_Width * m_Depth; i++) { Columns.Insert(new EXPG_BuildingColumn()); }
  array<Managed> sentinels = {};
  building.FindComponents(SCR_AISmartActionSentinelComponent, sentinels);
  foreach (Managed component : sentinels)
  {
   SCR_AISmartActionSentinelComponent sentinel = SCR_AISmartActionSentinelComponent.Cast(component);
   if (!sentinel) { continue; }
   m_Sentinels.Insert(building.CoordToParent(sentinel.GetActionOffset()));
   m_SentinelLooks.Insert(building.CoordToParent(sentinel.GetLookPosition()));
  }
  FindOpenings();
  SCR_DestructibleBuildingComponent damage = SCR_DestructibleBuildingComponent.Cast(building.FindComponent(SCR_DestructibleBuildingComponent));
  if (damage)
  {
   SCR_DestructibleBuildingComponentClass data = SCR_DestructibleBuildingComponentClass.Cast(damage.GetComponentData(building));
   if (data) m_InteriorBounds = data.m_aInteriorQueryBoundingBoxes;
  }
  m_Height = Maxs[1] + 1;
 }

 // Use the building's actual door/window parts, not a library of house-specific
 // coordinates. Bounded hierarchy traversal never scans unrelated world entities.
 protected void FindOpenings()
 {
  array<IEntity> parts = {Structure};
  for (int cursor = 0; cursor < parts.Count() && cursor < 512; cursor++)
  {
   IEntity part = parts[cursor];
   for (IEntity child = part.GetChildren(); child && parts.Count() < 512; child = child.GetSibling()) parts.Insert(child);
   if (part == Structure || m_Openings.Count() >= 64) continue;
   DoorComponent door = DoorComponent.Cast(part.FindComponent(DoorComponent));
   NavmeshCustomLinkComponent link = NavmeshCustomLinkComponent.Cast(part.FindComponent(NavmeshCustomLinkComponent));
   if (door && link && link.HasLinkOfNavmeshType("Soldiers") && Math.AbsFloat(door.GetAngleRange()) > 1)
   {
    m_TraversableDoors.Insert(part);
    // Glass panes and handles swing with the leaf (observed blocking glazed doors).
    array<IEntity> leaf = {part};
    for (int c = 0; c < leaf.Count() && leaf.Count() < 16; c++)
     for (IEntity sub = leaf[c].GetChildren(); sub && leaf.Count() < 16; sub = sub.GetSibling()) { leaf.Insert(sub); m_TraversableDoors.Insert(sub); }
   }
   ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(part);
   bool window = Building.Cast(part) && (prefab.Contains("/Windows/") || prefab.Contains("/windows/"));
   if (!door && !window) continue;
   EXPG_BuildingOpening opening = new EXPG_BuildingOpening();
   opening.Part = part;
   // A swinging leaf's centre moves; its frame remains the watch target.
   if (door && part.GetParent() && part.GetParent() != Structure) opening.Part = part.GetParent();
   vector mins, maxs; opening.Part.GetBounds(mins, maxs);
   opening.Position = opening.Part.CoordToParent((mins + maxs) * 0.5);
   opening.Door = door != null;
   m_Openings.Insert(opening);
  }
 }

 protected bool SeesOpening(vector eye, EXPG_BuildingOpening opening)
 {
  TraceParam sight = new TraceParam();
  sight.Start = eye; sight.End = opening.Position;
  sight.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(sight, null) >= 0.999) return true;
  // Glass or a closed leaf is the target; furniture/walls before it still block.
  IEntity hit = sight.TraceEnt;
  for (int depth = 0; hit && depth < 8; depth++)
  {
   if (hit == opening.Part) return true;
   if (hit == Structure) return false;
   hit = hit.GetParent();
  }
  return false;
 }

 void Fail(string reason) { Error = reason; Done = true; }

 bool Valid()
 {
  return Structure && vector.DistanceSq(Origin, Structure.GetOrigin()) < 0.01 && vector.DistanceSq(Angles, Structure.GetAngles()) < 0.01;
 }

 bool IsBuilding(IEntity entity)
 {
  return entity && Structure && entity.GetRootParent() == Structure.GetRootParent();
 }

 bool StructuralFloor(IEntity entity)
 {
  if (!IsBuilding(entity)) return false;
  if (entity == Structure) return true;
  bool structural = false;
  for (int depth = 0; entity && entity != Structure && depth < 12; depth++)
  {
   ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
   // Attached furnishings share the house root; they are not walkable floors.
   if (prefab.Contains("/Furniture/") || prefab.Contains("/furniture/")) return false;
   if (Building.Cast(entity) && !prefab.Contains("/Windows/") && !prefab.Contains("/windows/")) structural = true;
   if (prefab.Contains("/Stairs/") || prefab.Contains("/stairs/")) structural = true;
   entity = entity.GetParent();
  }
  return structural;
 }

 // Building floors, or bare ground inside an authored interior volume (barns,
 // sheds). Terrain outside the house is never an indoor floor.
 bool IndoorFloor(IEntity entity, vector point)
 {
  if (!Structure) return false;
  if (StructuralFloor(entity)) return true;
  return GenericTerrainEntity.Cast(entity) && m_InteriorBounds && !m_InteriorBounds.IsEmpty() && InteriorPoint(point + "0 0.1 0", 0);
 }

 bool InteriorPoint(vector point, float margin = 0.23)
 {
  if (!Structure) return false;
  vector local = Structure.CoordToLocal(point);
  if (m_InteriorBounds && !m_InteriorBounds.IsEmpty())
  {
   foreach (SCR_InteriorBoundingBox bounds : m_InteriorBounds)
   {
    vector mins, maxs; bounds.GetBounds(mins, maxs);
    if (local[0] >= mins[0] + margin && local[0] <= maxs[0] - margin && local[2] >= mins[2] + margin && local[2] <= maxs[2] - margin && local[1] >= mins[1] && local[1] <= maxs[1]) return true;
   }
   return false;
  }
  // No authored interior volume: require surrounding house walls in three
  // directions, in addition to the independently verified floor and ceiling.
  int walls = 0;
  for (int side = 0; side < 4; side++)
  {
   TraceParam wall = new TraceParam();
   wall.Start = point + "0 0.9 0";
   wall.End = wall.Start + vector.FromYaw(side * 90 + Angles[1]) * 40;
   wall.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   if (GetGame().GetWorld().TraceMove(wall, null) < 0.999 && IsBuilding(wall.TraceEnt)) walls++;
  }
  return walls >= 3;
 }

 // Patrols use only short graph edges entirely inside the house. The extra
 // margin includes allowed path drift; entrance/porch nodes remain graph roots
 // for reachability but never become patrol destinations or shortcuts.
 bool InteriorEdge(vector from, vector to)
 {
  float distance = vector.Distance(from, to);
  if (distance > 1.2) return false;
  int steps = Math.Max(1, Math.Ceil(distance / 0.2));
  for (int step = 0; step <= steps; step++)
   if (!InteriorPoint(vector.Lerp(from, to, step * 1.0 / steps), 0.4)) return false;
  return true;
 }

 bool Inside(vector point, float margin = 0)
 {
  if (!Structure) { return false; }
  vector local = Structure.CoordToLocal(point);
  return local[0] >= Mins[0] + margin && local[0] <= Maxs[0] - margin && local[2] >= Mins[2] + margin && local[2] <= Maxs[2] - margin && local[1] >= Mins[1] - 0.1 && local[1] <= Maxs[1];
 }

 bool ClearBody(vector from, vector to, IEntity exclude = null, array<IEntity> excludeEntities = null, bool planning = false)
 {
  TraceBox trace = new TraceBox();
  trace.Start = from + "0 0.45 0";
  trace.End = to + "0 0.45 0";
  trace.Mins = "-0.23 0 -0.23";
  trace.Maxs = "0.23 1.35 0.23";
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  // Match the native AI character collision matrix. An all-layer query also
  // hits nonblocking gear on casualties and can strand an exact cache restore.
  trace.LayerMask = EPhysicsLayerDefs.CharacterAI;
  array<IEntity> exclusions = {};
  if (planning && !m_TraversableDoors.IsEmpty())
  {
   exclusions.Copy(m_TraversableDoors);
   if (exclude) exclusions.Insert(exclude);
   if (excludeEntities) exclusions.InsertAll(excludeEntities);
   trace.ExcludeArray = exclusions;
  }
  else if (excludeEntities) { trace.ExcludeArray = excludeEntities; }
  else { trace.Exclude = exclude; }
  // Sweep tests do not establish initial occupancy. Include lower legs for
  // standing admission; walking sweeps retain step-height clearance separately.
  if (vector.DistanceSq(from, to) < 0.0004)
  {
   trace.Start = from + "0 0.05 0";
   trace.End = to + "0 0.05 0";
   trace.Maxs = "0.23 1.75 0.23";
   if (GetGame().GetWorld().TracePosition(trace, null) < 0) return false;
  }
  return GetGame().GetWorld().TraceMove(trace, null) >= 0.999;
 }

 bool Supported(vector position, float tolerance = 0.2, IEntity exclude = null, array<IEntity> excludeEntities = null)
 {
  TraceParam trace = new TraceParam();
  trace.Start = position + Vector(0, tolerance, 0);
  trace.End = position - Vector(0, tolerance + 0.2, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  // Same native AI character collision matrix as ClearBody: a casualty's helmet
  // landing on a neighbouring post is not floor and must not release the squad.
  trace.LayerMask = EPhysicsLayerDefs.CharacterAI;
  if (excludeEntities) { trace.ExcludeArray = excludeEntities; }
  else { trace.Exclude = exclude; }
  float hit = GetGame().GetWorld().TraceMove(trace, null);
  return hit < 0.999 && IndoorFloor(trace.TraceEnt, vector.Lerp(trace.Start, trace.End, hit)) && trace.TraceNorm[1] > 0.65;
 }

 // Posts around the building (extra squads once the building is full) stand on
 // any walkable surface, not only on an indoor floor; never in water.
 bool GroundSupported(vector position, float tolerance = 0.2, IEntity exclude = null, array<IEntity> excludeEntities = null)
 {
  TraceParam trace = new TraceParam();
  trace.Start = position + Vector(0, tolerance, 0);
  trace.End = position - Vector(0, tolerance + 0.2, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  trace.LayerMask = EPhysicsLayerDefs.CharacterAI;
  if (excludeEntities) { trace.ExcludeArray = excludeEntities; }
  else { trace.Exclude = exclude; }
  float hit = GetGame().GetWorld().TraceMove(trace, null);
  if (hit >= 0.999 || trace.TraceNorm[1] <= 0.65) { return false; }
  return !ChimeraWorldUtils.TryGetWaterSurfaceSimple(GetGame().GetWorld(), position + "0 0.5 0");
 }

 bool ValidateSlots(int count, array<IEntity> excludeEntities = null)
 {
  if (!Valid() || count < 1 || count > Slots.Count()) { return false; }
  for (int i = 0; i < count; i++)
  {
   vector point = Nodes[Slots[i]].Position;
   if (!Supported(point, 0.2, null, excludeEntities) || !ClearBody(point, point + "0 0.01 0", null, excludeEntities)) { return false; }
  }
  return true;
 }

 // A route edge must fit a standing body and have supported intermediate steps.
 // This conservative graph can reject tight/stale-navmesh buildings; it never
 // treats a sampled rooftop as connected merely because it lies on navmesh.
 bool WalkEdge(vector from, vector to)
 {
  if (Math.AbsFloat(from[1] - to[1]) > 0.48 || !ClearBody(from, to, null, null, true)) { return false; }
  for (int i = 1; i < 3; i++)
  {
   vector sample = vector.Lerp(from, to, i / 3.0);
   if (!Supported(sample, 0.25)) { return false; }
  }
  return true;
 }

 // The only root admission is a body-clear connection from low interior floor
 // to surrounding terrain. Upper floors need a chain of supported stair edges.
 bool Entrance(vector point)
 {
  for (int i = 0; i < 4; i++)
  {
   vector direction = vector.FromYaw(i * 90 + Angles[1]);
   vector outside = point + direction * 2.25;
   if (Inside(outside)) { continue; }
   float ground = GetGame().GetWorld().GetSurfaceY(outside[0], outside[2]);
   if (Math.AbsFloat(ground - point[1]) > 0.45) { continue; }
   outside[1] = ground + 0.05;
   if (!ClearBody(outside, point, null, null, true)) { continue; }
   bool supported = true;
   float previousY = ground;
   for (int step = 1; step <= 8; step++)
   {
    vector sample = vector.Lerp(outside, point, step / 8.0);
    TraceParam foot = new TraceParam();
    foot.Start = sample + "0 0.25 0";
    foot.End = sample - "0 0.35 0";
    foot.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    float hit = GetGame().GetWorld().TraceMove(foot, null);
    if (hit >= 0.999 || foot.TraceNorm[1] < 0.65) { supported = false; break; }
    vector floorPoint = vector.Lerp(foot.Start, foot.End, hit);
    if (Math.AbsFloat(floorPoint[1] - previousY) > 0.25) { supported = false; break; }
    previousY = floorPoint[1];
   }
   if (supported) { return true; }
  }
  return false;
 }

 // AI enter most houses through native doors, often over a raised floor or
 // entry steps that the terrain probe above rejects. A node near the inside
 // approach of a door on its floor is a root when a standing body fits straight
 // through the doorway (along the frame's thin axis) and, past any porch that
 // the interior volume still covers, reaches terrain at most 1.5 m below the
 // floor over supported steps of at most 0.5 m. Upper-floor balconies fail.
 bool DoorEntrance(vector point)
 {
  foreach (EXPG_BuildingOpening opening : m_Openings)
  {
   if (!opening.Door) continue;
   vector door = opening.Position;
   if (door[1] < point[1] + 0.2 || door[1] > point[1] + 2.5 || vector.DistanceXZ(door, point) > 2.0) continue;
   vector frame[4]; opening.Part.GetWorldTransform(frame);
   vector mins, maxs; opening.Part.GetBounds(mins, maxs);
   vector normal = frame[2];
   if (maxs[0] - mins[0] < maxs[2] - mins[2]) normal = frame[0];
   normal[1] = 0;
   if (normal.Length() < 0.1) continue;
   normal.Normalize();
   vector threshold = Vector(door[0], point[1], door[2]);
   float side = vector.Dot(point - threshold, normal);
   if (Math.AbsFloat(side) < 0.05) continue;
   if (side < 0) normal = normal * -1;
   vector inner = threshold + normal * 0.7;
   bool approach = vector.DistanceXZ(point, inner) <= 1.2;
   if (approach && vector.DistanceXZ(point, inner) > 0.05) approach = ClearBody(point, inner, null, null, true);
   if (approach && WalkOut(inner, normal * -1)) return true;
   // Fallback: straight from the node through the door centre (small sheds).
   vector flat = Vector(door[0] - point[0], 0, door[2] - point[2]);
   float distance = flat.Length();
   if (distance < 0.1 || distance > 1.5) continue;
   vector beyond = Vector(door[0], point[1], door[2]) + flat * (1.25 / distance);
   if (Inside(beyond, 0.1) && InteriorPoint(beyond, 0)) continue;
   float drop = point[1] - GetGame().GetWorld().GetSurfaceY(beyond[0], beyond[2]);
   if (drop <= 1.5 && drop >= -0.5 && ClearBody(point, beyond, null, null, true)) return true;
  }
  return false;
 }

 // Open gateways and ramps without door components (barns, sheds): a node
 // within 2 m of the footprint edge may walk straight out along a building axis.
 bool PerimeterEntrance(vector point)
 {
  vector local = Structure.CoordToLocal(point);
  float edge = Math.Min(Math.Min(local[0] - Mins[0], Maxs[0] - local[0]), Math.Min(local[2] - Mins[2], Maxs[2] - local[2]));
  if (edge > 2.0) return false;
  for (int side = 0; side < 4; side++)
   if (WalkOut(point, vector.FromYaw(side * 90 + Angles[1]))) return true;
  return false;
 }

 // Walk outward from a door's inside approach in 0.5 m steps (at most 4 m).
 protected bool WalkOut(vector from, vector outward)
 {
  vector previous = from;
  for (int step = 1; step <= 8; step++)
  {
   vector next = from + outward * (0.25 + 0.5 * step);
   TraceParam foot = new TraceParam();
   foot.Start = Vector(next[0], previous[1] + 0.3, next[2]);
   foot.End = Vector(next[0], previous[1] - 0.6, next[2]);
   foot.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   foot.LayerMask = EPhysicsLayerDefs.CharacterAI;
   float hit = GetGame().GetWorld().TraceMove(foot, null);
   if (hit >= 0.999 || foot.TraceNorm[1] < 0.65) return false;
   next[1] = foot.Start[1] + (foot.End[1] - foot.Start[1]) * hit + 0.05;
   if (!ClearBody(previous, Vector(next[0], Math.Max(next[1], previous[1]), next[2]), null, null, true)) return false;
   previous = next;
   if (Inside(next, 0.1) && InteriorPoint(next, 0)) continue;
   // Outside the interior volume: succeed only once the footing is back on terrain,
   // so railed or edged terraces and upper-floor balconies never become roots.
   float ground = GetGame().GetWorld().GetSurfaceY(next[0], next[2]);
   float drop = from[1] - ground;
   if (drop > 1.5 || drop < -0.5) return false;
   if (next[1] - ground <= 0.45) return true;
  }
  return false;
 }

 void Step(int work = 8)
 {
  if (Done) { return; }
  if (!Valid()) { Fail("Structure changed while its positions were being checked"); return; }
  for (int operation = 0; operation < work && !Done; operation++)
  {
   if (m_Phase == 0) { SampleColumn(); }
   else if (m_Phase == 1) { LinkOne(); }
   else if (m_Phase == 2) { MarkReachable(); m_Phase = 3; m_Node = 0; }
   else if (m_Phase == 3) { ScoreOne(); }
   else { SelectSlots(); Done = true; }
  }
 }

 protected void SampleColumn()
 {
  if (m_Column >= Columns.Count()) { m_Phase = 1; return; }
  int x = m_Column % m_Width;
  int z = m_Column / m_Width;
  vector local = Vector(Mins[0] + (x + 0.5) * GRID, m_Height, Mins[2] + (z + 0.5) * GRID);
  TraceParam floor = new TraceParam();
  floor.Start = Structure.CoordToParent(local);
  local[1] = Mins[1] - 0.3;
  floor.End = Structure.CoordToParent(local);
  floor.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  float fraction = GetGame().GetWorld().TraceMove(floor, null);
  if (fraction >= 0.999) { NextColumn(); return; }
  vector point = vector.Lerp(floor.Start, floor.End, fraction);
  m_Height = Structure.CoordToLocal(point)[1] - 0.15;
  if (m_Height <= Mins[1]) { NextColumn(); }
  if (!IndoorFloor(floor.TraceEnt, point) || floor.TraceNorm[1] < 0.65) { return; }
  point[1] = point[1] + 0.05;
  if (!Inside(point) || !ClearBody(point, point + "0 0.01 0")) { return; }
  TraceParam ceiling = new TraceParam();
  ceiling.Start = point + "0 1.8 0";
  ceiling.End = point + "0 30 0";
  ceiling.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(ceiling, null) >= 0.999 || !IsBuilding(ceiling.TraceEnt)) { return; }
  if (Nodes.Count() >= MAX_NODES) { Fail("Structure has too many interior samples"); return; }
  EXPG_BuildingNode node = new EXPG_BuildingNode();
  node.Position = point;
  // Sample belongs to the column before NextColumn advanced it.
  node.Column = z * m_Width + x;
  node.Entrance = Entrance(point) || DoorEntrance(point) || PerimeterEntrance(point);
  node.Interior = InteriorPoint(point);
  Columns[node.Column].Nodes.Insert(Nodes.Count());
  Nodes.Insert(node);
 }

 protected void NextColumn() { m_Column++; m_Height = Maxs[1] + 1; }

 // Four forward offsets create all eight undirected neighbors without repeats.
 // Every diagonal still needs the full swept-body and intermediate-floor proof.
 static int ForwardColumn(int column, int width, int count, int edge)
 {
  if (width < 1 || column < 0 || column >= count || edge < 0 || edge > 3) return -1;
  int x = column % width;
  if ((edge == 0 || edge == 2) && x == width - 1) return -1;
  if (edge == 3 && x == 0) return -1;
  int target = column + width;
  if (edge == 0) target = column + 1;
  else if (edge == 2) target++;
  else if (edge == 3) target--;
  if (target >= count) return -1;
  return target;
 }

 protected void LinkOne()
 {
  if (m_Node >= Nodes.Count()) { m_Phase = 2; return; }
  EXPG_BuildingNode node = Nodes[m_Node];
  int otherColumn = ForwardColumn(node.Column, m_Width, Columns.Count(), m_Edge);
  if (otherColumn < 0 || m_Other >= Columns[otherColumn].Nodes.Count())
  {
   m_Other = 0;
   m_Edge++;
   if (m_Edge >= 4) { m_Node++; m_Edge = 0; }
   return;
  }
  int index = Columns[otherColumn].Nodes[m_Other++];
  if (!WalkEdge(node.Position, Nodes[index].Position)) { return; }
  node.Links.Insert(index);
  Nodes[index].Links.Insert(m_Node);
 }

 void MarkReachable()
 {
  array<int> queue = {};
  foreach (EXPG_BuildingNode clear : Nodes) { clear.Reachable = false; }
  for (int i = 0; i < Nodes.Count(); i++)
  {
   if (!Nodes[i].Entrance) { continue; }
   Nodes[i].Reachable = true;
   queue.Insert(i);
  }
  for (int cursor = 0; cursor < queue.Count(); cursor++)
  {
   foreach (int next : Nodes[queue[cursor]].Links)
   {
    if (Nodes[next].Reachable) { continue; }
    Nodes[next].Reachable = true;
    queue.Insert(next);
   }
  }
 }

 protected void ScoreOne()
 {
  if (m_Node >= Nodes.Count()) { m_Phase = 4; return; }
  EXPG_BuildingNode node = Nodes[m_Node++];
  if (!node.Reachable) { return; }
  node.Look = vector.FromYaw(Angles[1]);
  if (node.Entrance) { node.Score = 70; }
  foreach (int link : node.Links)
  {
   if (Math.AbsFloat(Nodes[link].Position[1] - node.Position[1]) > 0.15)
   { node.Score = Math.Max(node.Score, 60); node.Look = vector.Direction(node.Position, Nodes[link].Position).Normalized(); }
  }
  for (int s = 0; s < m_Sentinels.Count(); s++)
  {
   if (vector.DistanceSq(node.Position, m_Sentinels[s]) > 0.5) { continue; }
   node.Score = 100;
   node.Look = vector.Direction(node.Position, m_SentinelLooks[s]).Normalized();
   return;
  }
  // Watch a window from 0.4-3 m (the nearest window wins; SelectSlots takes the
  // nearest window posts first), or a door from roughly 2 m inside. Only sampled
  // nodes qualify: each already passed floor, standing-body clearance, ceiling,
  // interior and entrance-reachability checks, so no position is synthesized.
  // 0.4 m from the pane centre is about a body against a thin wall; it keeps the
  // muzzle near the window plane and the yaw well defined.
  foreach (EXPG_BuildingOpening opening : m_Openings)
  {
   vector eye = node.Position + "0 1.5 0";
   vector delta = opening.Position - eye;
   float horizontal = delta[0] * delta[0] + delta[2] * delta[2];
   float maximum = 9;
   float minimum = 0.16;
   if (opening.Door) { minimum = 2.25; maximum = 6.25; }
   if (horizontal < minimum || horizontal > maximum || Math.AbsFloat(delta[1]) > 0.75) continue;
   int score = 80;
   if (opening.Door) score = 90;
   if (node.Score > score || (node.Score == score && (opening.Door || horizontal >= node.Range * node.Range))) continue;
   if (!SeesOpening(eye, opening)) continue;
   node.Score = score;
   if (opening.Door) { node.Look = delta.Normalized(); continue; }
   // Level gaze: at close range the pane centre is often well above or below the eye.
   delta[1] = 0;
   node.Look = delta.Normalized();
   node.Range = Math.Sqrt(horizontal);
  }
  if (node.Score >= 80) return;
  // Open window holes: the nearest clear outward view wins. Its range is where
  // the view leaves the building bounds (eaves add about the same per building).
  vector viewer = node.Position + "0 1.5 0";
  float nearest = 1000;
  for (int yaw = 0; yaw < 360; yaw += 45)
  {
   vector direction = vector.FromYaw(yaw);
   float reach = 0.25;
   while (reach < 4 && Inside(viewer + direction * reach)) reach += 0.25;
   if (reach >= nearest || Inside(viewer + direction * reach)) { continue; }
   TraceParam sight = new TraceParam();
   sight.Start = viewer;
   sight.End = sight.Start + direction * 4;
   sight.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   if (GetGame().GetWorld().TraceMove(sight, null) < 0.999) { continue; }
   nearest = reach;
   node.Score = 80;
   node.Look = direction;
   node.Range = reach;
  }
 }

 protected void SelectSlots()
 {
  // At most 32 places; greedy spacing over a bounded graph avoids a large solver.
  // Window posts (80) are taken nearest-first in three range bands, so the 1.2 m
  // spacing keeps the guard at the window rather than the first grid hit behind it.
  array<int> priorities = {100, 90, 80, 70, 60, 0};
  array<float> bands = {1.25, 2.0, 1000.0};
  foreach (int priority : priorities)
  {
   foreach (int band, float limit : bands)
   {
    if (priority != 80 && band < 2) { continue; }
    for (int i = 0; i < Nodes.Count() && Slots.Count() < 32; i++)
    {
     EXPG_BuildingNode node = Nodes[i];
     if (!node.Reachable || !node.Interior || node.Score != priority || node.Range > limit) { continue; }
     // Also skips nodes already selected in an earlier band (distance zero).
     bool occupied;
     foreach (int selected : Slots)
     {
      if (vector.DistanceSq(node.Position, Nodes[selected].Position) < 1.44) { occupied = true; break; }
     }
     if (occupied) { continue; }
     Slots.Insert(i);
     FixedSlots.Insert(priority > 0);
    }
   }
  }
  if (Slots.IsEmpty()) { Error = "No connected, clear indoor positions were found"; }
 }

 bool Route(int from, int to, notnull array<int> path)
 {
  path.Clear();
  if (from < 0 || to < 0 || from >= Nodes.Count() || to >= Nodes.Count() || !Nodes[from].Reachable || !Nodes[to].Reachable) { return false; }
  array<int> parents = {};
  parents.Resize(Nodes.Count());
  for (int i = 0; i < parents.Count(); i++) { parents[i] = -1; }
  array<int> queue = {from};
  parents[from] = from;
  for (int cursor = 0; cursor < queue.Count() && parents[to] == -1; cursor++)
  {
   foreach (int next : Nodes[queue[cursor]].Links)
   {
    if (!Nodes[next].Reachable || parents[next] != -1) { continue; }
    parents[next] = queue[cursor];
    queue.Insert(next);
   }
  }
  if (parents[to] == -1) { return false; }
  int node = to;
  while (node != from) { path.InsertAt(node, 0); node = parents[node]; }
  path.InsertAt(from, 0);
  return true;
 }
}
