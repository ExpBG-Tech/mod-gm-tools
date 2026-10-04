// Original EXPBG building sampling. No positions or implementation imported from GME.
class EXPG_BuildingNode
{
 vector Position;
 vector Look;
 int Column;
 int Score;
 bool Entrance;
 bool Reachable;
 ref array<int> Links = {};
}

class EXPG_BuildingColumn
{
 ref array<int> Nodes = {};
}

class EXPG_BuildingReservation
{
 IEntity Owner;
 int From;
 int To;
}

class EXPG_BuildingPlan
{
 static const float GRID = 0.75;
 static const int MAX_NODES = 4096;
 IEntity Building;
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
 protected ref array<ref EXPG_BuildingReservation> m_Reservations = {};

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
  EXPG_BuildingReservation owned;
  foreach (EXPG_BuildingReservation reservation : m_Reservations)
  {
   if (reservation.Owner == owner) { owned = reservation; continue; }
   if (RoutesConflict(Nodes[from].Position, Nodes[to].Position, Nodes[reservation.From].Position, Nodes[reservation.To].Position)) { return false; }
  }
  if (!owned)
  {
   if (m_Reservations.Count() >= 32) { return false; }
   owned = new EXPG_BuildingReservation();
   owned.Owner = owner;
   m_Reservations.Insert(owned);
  }
  owned.From = from;
  owned.To = to;
  return true;
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
  Building = building;
  if (!building) { Fail("Building no longer exists"); return; }
  Origin = building.GetOrigin();
  Angles = building.GetAngles();
  building.GetBounds(Mins, Maxs);
  m_Width = Math.Ceil((Maxs[0] - Mins[0]) / GRID);
  m_Depth = Math.Ceil((Maxs[2] - Mins[2]) / GRID);
  if (m_Width < 1 || m_Depth < 1 || m_Width * m_Depth > 2048 || Maxs[1] - Mins[1] > 40)
  { Fail("Building exceeds the supported sampling bounds"); return; }
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
  m_Height = Maxs[1] + 1;
 }

 void Fail(string reason) { Error = reason; Done = true; }

 bool Valid()
 {
  return Building && vector.DistanceSq(Origin, Building.GetOrigin()) < 0.01 && vector.DistanceSq(Angles, Building.GetAngles()) < 0.01;
 }

 bool IsBuilding(IEntity entity)
 {
  return entity && Building && entity.GetRootParent() == Building.GetRootParent();
 }

 bool Inside(vector point, float margin = 0)
 {
  if (!Building) { return false; }
  vector local = Building.CoordToLocal(point);
  return local[0] >= Mins[0] + margin && local[0] <= Maxs[0] - margin && local[2] >= Mins[2] + margin && local[2] <= Maxs[2] - margin && local[1] >= Mins[1] - 0.1 && local[1] <= Maxs[1];
 }

 bool ClearBody(vector from, vector to, IEntity exclude = null, array<IEntity> excludeEntities = null)
 {
  TraceBox trace = new TraceBox();
  trace.Start = from + "0 0.45 0";
  trace.End = to + "0 0.45 0";
  trace.Mins = "-0.23 0 -0.23";
  trace.Maxs = "0.23 1.35 0.23";
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (excludeEntities) { trace.ExcludeArray = excludeEntities; }
  else { trace.Exclude = exclude; }
  return GetGame().GetWorld().TraceMove(trace, null) >= 0.999;
 }

 bool Supported(vector position, float tolerance = 0.2, IEntity exclude = null, array<IEntity> excludeEntities = null)
 {
  TraceParam trace = new TraceParam();
  trace.Start = position + Vector(0, tolerance, 0);
  trace.End = position - Vector(0, tolerance + 0.2, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (excludeEntities) { trace.ExcludeArray = excludeEntities; }
  else { trace.Exclude = exclude; }
  float hit = GetGame().GetWorld().TraceMove(trace, null);
  return hit < 0.999 && IsBuilding(trace.TraceEnt) && trace.TraceNorm[1] > 0.65;
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
  if (Math.AbsFloat(from[1] - to[1]) > 0.48 || !ClearBody(from, to)) { return false; }
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
   vector direction = vector.FromYaw(i * 90 + Angles[0]);
   vector outside = point + direction * 2.25;
   if (Inside(outside)) { continue; }
   float ground = GetGame().GetWorld().GetSurfaceY(outside[0], outside[2]);
   if (Math.AbsFloat(ground - point[1]) > 0.45) { continue; }
   outside[1] = ground + 0.05;
   if (!ClearBody(outside, point)) { continue; }
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

 void Step(int work = 8)
 {
  if (Done) { return; }
  if (!Valid()) { Fail("Building changed while its positions were being checked"); return; }
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
  floor.Start = Building.CoordToParent(local);
  local[1] = Mins[1] - 0.3;
  floor.End = Building.CoordToParent(local);
  floor.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  float fraction = GetGame().GetWorld().TraceMove(floor, null);
  if (fraction >= 0.999) { NextColumn(); return; }
  vector point = vector.Lerp(floor.Start, floor.End, fraction);
  m_Height = Building.CoordToLocal(point)[1] - 0.15;
  if (m_Height <= Mins[1]) { NextColumn(); }
  if (!IsBuilding(floor.TraceEnt) || floor.TraceNorm[1] < 0.65) { return; }
  point[1] = point[1] + 0.05;
  if (!Inside(point) || !ClearBody(point, point + "0 0.01 0")) { return; }
  TraceParam ceiling = new TraceParam();
  ceiling.Start = point + "0 1.8 0";
  ceiling.End = point + "0 12 0";
  ceiling.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(ceiling, null) >= 0.999 || !IsBuilding(ceiling.TraceEnt)) { return; }
  if (Nodes.Count() >= MAX_NODES) { Fail("Building has too many interior samples"); return; }
  EXPG_BuildingNode node = new EXPG_BuildingNode();
  node.Position = point;
  // Sample belongs to the column before NextColumn advanced it.
  node.Column = z * m_Width + x;
  node.Entrance = Entrance(point);
  Columns[node.Column].Nodes.Insert(Nodes.Count());
  Nodes.Insert(node);
 }

 protected void NextColumn() { m_Column++; m_Height = Maxs[1] + 1; }

 protected void LinkOne()
 {
  if (m_Node >= Nodes.Count()) { m_Phase = 2; return; }
  EXPG_BuildingNode node = Nodes[m_Node];
  int otherColumn = node.Column + 1;
  if (m_Edge == 0 && node.Column % m_Width == m_Width - 1) { m_Edge = 1; }
  if (m_Edge == 1) { otherColumn = node.Column + m_Width; }
  if (otherColumn >= Columns.Count() || m_Other >= Columns[otherColumn].Nodes.Count())
  {
   m_Other = 0;
   m_Edge++;
   if (m_Edge >= 2) { m_Node++; m_Edge = 0; }
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
  node.Look = vector.FromYaw(Angles[0]);
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
  for (int yaw = 0; yaw < 360; yaw += 45)
  {
   vector direction = vector.FromYaw(yaw);
   TraceParam sight = new TraceParam();
   sight.Start = node.Position + "0 1.5 0";
   sight.End = sight.Start + direction * 4;
   sight.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   if (Inside(sight.End)) { continue; }
   if (GetGame().GetWorld().TraceMove(sight, null) < 0.999) { continue; }
   if (node.Score < 80) { node.Score = 80; node.Look = direction; }
  }
 }

 protected void SelectSlots()
 {
  // At most 32 places; greedy spacing over a bounded graph avoids a large solver.
  array<int> priorities = {100, 80, 70, 60, 0};
  foreach (int priority : priorities)
  {
   for (int i = 0; i < Nodes.Count() && Slots.Count() < 32; i++)
   {
    EXPG_BuildingNode node = Nodes[i];
    if (!node.Reachable || node.Score != priority) { continue; }
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
