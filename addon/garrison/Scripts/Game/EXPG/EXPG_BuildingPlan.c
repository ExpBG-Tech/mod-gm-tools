// Original EXPBG building sampling. No positions or implementation imported from GME.
class EXPG_BuildingNode
{
 vector Position;
 vector Look;
 // Window and door posts: metres to the watched opening (or to the open view's exit;
 // plus 1 for a door post in line with its doorway), lower ranks first.
 float Range = 1000;
 int Column;
 int Score;
 // Watched opening: 0 none, OPENING_WINDOW or OPENING_DOOR; Watch indexes the
 // plan's openings (-1 for an open view or none).
 int Opening;
 int Watch = -1;
 // Floor level, 0 = the lowest indoor floor (EXPG_BuildingPlan.BuildStoreys).
 int Storey;
 bool Entrance;
 bool Reachable;
 bool Interior;
 // Walls on at least six of eight bearings at eye height (EXPG_BuildingPlan.Enclosed).
 bool Enclosed;
 // Stair tread or ramp: links floors, never a post, patrol stop or extra position.
 bool Stair;
 // In a door leaf's swing or its doorway (EXPG_BuildingPlan.InDoorZone): never a
 // post, patrol stop or watch point, so every door can still be opened; walkable.
 bool DoorBlock;
 // Indoor floor a patrol may walk: an enclosed interior node, or a stair or aside
 // node whose stair-connected group touches only such nodes (BuildWalk).
 bool IndoorWalk;
 // Connected indoor walking area of IndoorWalk nodes; -1 for none.
 int WalkGroup = -1;
 // Interior patrol stop (EXPG_BuildingPlan.RoamStops) and the hallway or doorway
 // a patroller there watches.
 bool RoamStop;
 vector WatchLook;
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
 // A door between an enclosed room and the outside; Inward points into the room.
 bool Exterior;
 vector Inward;
 // Doors: level unit vector across the doorway (along the frame) and half the
 // frame's width, so a door post can prefer a spot diagonal to the opening.
 vector Side;
 float HalfWidth;
}

// One door leaf's keep-out zone (EXPG_BuildingPlan.InDoorZone): the hinge, the level
// unit vector from the hinge across the doorway towards the frame centre, the leaf
// width and the floor height under the leaf.
class EXPG_DoorLeaf
{
 vector Hinge;
 vector Along;
 float LeafSpan;
 float FloorY;
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

// A live guard's post off the sampled plan (no free node within 0.5 m, or a post
// around the building): no node reservation, yet claims keep their spacing from it.
class EXPG_HeldPost
{
 IEntity Owner;
 vector Position;
}

class EXPG_BuildingPlan
{
 static const float GRID = 0.75;
 static const int MAX_NODES = 8192;
 static const int MAX_SLOTS = 32;
 // Post priority: authored sentinels, then windows, then the inside of outer
 // doors, then entrance approaches and the tops/bottoms of stairs, then patrols.
 static const int SCORE_SENTINEL = 100;
 static const int SCORE_WINDOW = 90;
 static const int SCORE_DOOR = 80;
 static const int SCORE_ENTRANCE = 70;
 static const int SCORE_STAIRS = 60;
 static const int OPENING_WINDOW = 1;
 static const int OPENING_DOOR = 2;
 // Every post keeps POST_SPACING from the others on its floor; the first post of
 // each kind is spread WIDE_SPACING apart before posts are packed closer.
 static const float POST_SPACING = 1.5;
 static const float WIDE_SPACING = 2.5;
 // A body clears this much floor rise without touching it (a stair tread).
 static const float STEP_HEIGHT = 0.45;
 // Interior patrol stops, alarm windows and alarm watch points kept per building.
 static const int MAX_ROAM = 96;
 static const int MAX_ALERT_NODES = 256;
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
 // Last time a Game Master waited for this analysis (EXPG_GarrisonManager.Wait).
 float WaitedAt = -1000;
 // Interior patrol stops (farthest first, storeys taking turns, 1.5 m apart and
 // from every fixed slot), alarm windows and alarm watch points (doors, stairs,
 // entrances); none lies in a door zone.
 ref array<int> RoamStops = {};
 ref array<int> WindowNodes = {};
 ref array<int> WatchNodes = {};
 // Building-wide alarm shared by every garrison of the building (seconds of
 // world time, EXPG_GarrisonManager.ServiceAlert).
 float LastAlert = -1000;
 float ThreatAt = -1000;
 vector ThreatPos;
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
 protected ref array<ref EXPG_HeldPost> m_Held = {};
 protected ref array<float> m_StoreyBases = {};
 protected int m_Turn;
 protected ref array<ref EXPG_DoorLeaf> m_DoorLeaves = {};
 // Roam stops per walk group; breadth-first scratch (hops per node, touched nodes).
 protected ref array<int> m_GroupStops = {};
 protected ref array<int> m_Hops = {};
 protected ref array<int> m_Touched = {};
 // Roam stop selection (phase 5): candidates, their squared floor gap to the
 // nearest slot or stop, how many stops each gap already counts, full storeys.
 protected ref array<int> m_RoamCandidates = {};
 protected ref array<float> m_RoamGap = {};
 protected ref array<int> m_RoamSeen = {};
 protected ref array<bool> m_RoamFull = {};
 protected int m_RoamCursor;
 protected int m_RoamBest = -1;
 protected float m_RoamBestGap = -1;
 protected int m_RoamStorey;
 protected int m_WatchCursor;

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

 // Also drops the owner's off-plan post (HoldOffPlan).
 void ReleaseReservation(IEntity owner)
 {
  for (int i = m_Reservations.Count() - 1; i >= 0; i--)
  {
   if (!m_Reservations[i].Owner || m_Reservations[i].Owner == owner) { m_Reservations.RemoveOrdered(i); }
  }
  for (int h = m_Held.Count() - 1; h >= 0; h--)
  {
   if (!m_Held[h].Owner || m_Held[h].Owner == owner) { m_Held.RemoveOrdered(h); }
  }
 }

 // A live guard whose post has no node reservation (EXPG_GarrisonMember.Anchor fell
 // back to the spot he stands on, or a post around the building): StopFree keeps
 // every claim POST_SPACING from it. One entry per guard, dropped with his
 // reservation (ReleaseReservation).
 void HoldOffPlan(IEntity owner, vector point)
 {
  if (!owner) { return; }
  foreach (EXPG_HeldPost existing : m_Held)
  {
   if (existing.Owner != owner) { continue; }
   existing.Position = point;
   return;
  }
  EXPG_HeldPost held = new EXPG_HeldPost();
  held.Owner = owner;
  held.Position = point;
  m_Held.Insert(held);
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

 // A door's keep-out zone on its floor (0.6 m below to 1.2 m above the leaf's
 // floor): the leaf's swing disc, width plus 0.6 m (body and margin) around the
 // hinge, on both sides because native doors open either way, and the doorway
 // passage, 0.3 m beyond the leaf's span and 1.5 m deep on each side. Scalar only.
 static bool InDoorZone(vector point, vector hinge, vector along, float span, float floorY)
 {
  float rise = point[1] - floorY;
  if (rise < -0.6 || rise > 1.2) return false;
  float dx = point[0] - hinge[0];
  float dz = point[2] - hinge[2];
  float reach = span + 0.6;
  if (dx * dx + dz * dz < reach * reach) return true;
  float lengthwise = dx * along[0] + dz * along[2];
  float crosswise = Math.AbsFloat(dx * along[2] - dz * along[0]);
  return lengthwise >= -0.3 && lengthwise <= span + 0.3 && crosswise < 1.5;
 }

 bool InAnyDoorZone(vector point)
 {
  foreach (EXPG_DoorLeaf leaf : m_DoorLeaves)
  {
   if (InDoorZone(point, leaf.Hinge, leaf.Along, leaf.LeafSpan, leaf.FloorY)) return true;
  }
  return false;
 }

 int DoorLeafCount()
 {
  return m_DoorLeaves.Count();
 }

 // No other guard's post (on the plan or off it), stop or destination (and no
 // parked post of a Full-cached garrison) within POST_SPACING on the same floor.
 bool StopFree(int node, IEntity exceptOwner = null)
 {
  if (node < 0 || node >= Nodes.Count()) return false;
  vector point = Nodes[node].Position;
  foreach (EXPG_BuildingReservation reservation : m_Reservations)
  {
   if (!reservation.Owner || reservation.Owner == exceptOwner) continue;
   if (Crowded(point, Nodes[reservation.From].Position, POST_SPACING) || Crowded(point, Nodes[reservation.To].Position, POST_SPACING)) return false;
  }
  foreach (EXPG_HeldPost held : m_Held)
  {
   if (!held.Owner || held.Owner == exceptOwner) continue;
   if (Crowded(point, held.Position, POST_SPACING)) return false;
  }
  foreach (EXPG_ParkedPost parked : m_Parked)
  {
   if (Crowded(point, parked.Position, POST_SPACING)) return false;
  }
  return true;
 }

 // A patroller's single claim: his current stop or his destination, never both.
 // Whoever claims first wins; a claimed stop keeps POST_SPACING from every other
 // claim and parked post, lies outside every door zone and fits a standing body.
 bool TryClaimStop(IEntity owner, int node)
 {
  if (!owner || node < 0 || node >= Nodes.Count()) return false;
  EXPG_BuildingNode target = Nodes[node];
  if (!target.Reachable || !target.IndoorWalk || target.Stair || target.DoorBlock) return false;
  if (!StopFree(node, owner)) return false;
  if (!ClearBody(target.Position, target.Position + "0 0.01 0", owner)) return false;
  return ReserveNode(owner, node);
 }

 bool NearParked(vector point, float reach)
 {
  foreach (EXPG_ParkedPost parked : m_Parked)
  {
   if (Math.AbsFloat(parked.Position[1] - point[1]) < 2.0 && vector.DistanceXZ(parked.Position, point) < reach) return true;
  }
  return false;
 }

 // Another living soldier of this building's garrisons standing within reach of the
 // point (horizontally, under 1.5 m up or down): a guard, or a patroller who is not
 // walking. Every live guard has a reservation or an off-plan post, so no world
 // query; called on a patroller's arrival or failed walk only.
 bool NearStanding(vector point, IEntity exceptOwner, float reach)
 {
  foreach (EXPG_BuildingReservation reservation : m_Reservations)
  {
   if (StandsNear(reservation.Owner, exceptOwner, point, reach)) return true;
  }
  foreach (EXPG_HeldPost held : m_Held)
  {
   if (StandsNear(held.Owner, exceptOwner, point, reach)) return true;
  }
  return false;
 }

 protected static bool StandsNear(IEntity owner, IEntity exceptOwner, vector point, float reach)
 {
  if (!owner || owner == exceptOwner) return false;
  vector at = owner.GetOrigin();
  if (Math.AbsFloat(at[1] - point[1]) >= 1.5 || vector.DistanceXZ(at, point) >= reach) return false;
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(owner);
  if (!actor) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  if (!controller || controller.IsDead()) return false;
  EXPG_PatrolControl patrol = controller.EXPG_GetPatrolControl();
  return !patrol || !patrol.IsMoving();
 }

 // The nearest indoor walking node within reach (horizontal, 0.8 m vertical) in
 // the 3 x 3 grid columns around the point: O(1), no traces. Standing excludes
 // stair and aside nodes and door zones.
 int NearestNode(vector point, float reach, bool standing)
 {
  if (!Structure || m_Width < 1 || m_Depth < 1) return -1;
  vector local = Structure.CoordToLocal(point);
  int x = Math.Floor((local[0] - Mins[0]) / GRID);
  int z = Math.Floor((local[2] - Mins[2]) / GRID);
  int best = -1;
  float bestDistance = reach * reach;
  for (int dz = -1; dz <= 1; dz++)
  {
   for (int dx = -1; dx <= 1; dx++)
   {
    int columnX = x + dx;
    int columnZ = z + dz;
    if (columnX < 0 || columnZ < 0 || columnX >= m_Width || columnZ >= m_Depth) continue;
    foreach (int index : Columns[columnZ * m_Width + columnX].Nodes)
    {
     EXPG_BuildingNode node = Nodes[index];
     if (!node.IndoorWalk || Math.AbsFloat(node.Position[1] - point[1]) > 0.8) continue;
     if (standing && (node.Stair || node.DoorBlock)) continue;
     float distance = vector.DistanceSqXZ(node.Position, point);
     if (distance > bestDistance) continue;
     best = index;
     bestDistance = distance;
    }
   }
  }
  return best;
 }

 // On the indoor floor: within 0.7 m (about one grid step) of an indoor walking node.
 bool OnIndoorFloor(vector point)
 {
  return NearestNode(point, 0.7, false) >= 0;
 }

 // A patrol's containment test for its native path and its next step.
 bool Contained(vector point)
 {
  return Inside(point, 0.25) && OnIndoorFloor(point);
 }

 // Breadth-first steps over indoor walking nodes from one node, at most maxHops
 // steps and 4096 nodes; Hops reads the result until the next call. Resets only
 // the entries the previous call touched.
 void DistancesFrom(int from, int maxHops)
 {
  if (m_Hops.Count() != Nodes.Count())
  {
   m_Hops.Clear();
   for (int i = 0; i < Nodes.Count(); i++) { m_Hops.Insert(-1); }
   m_Touched.Clear();
  }
  foreach (int touched : m_Touched) { m_Hops[touched] = -1; }
  m_Touched.Clear();
  if (from < 0 || from >= Nodes.Count()) return;
  m_Hops[from] = 0;
  m_Touched.Insert(from);
  for (int cursor = 0; cursor < m_Touched.Count() && m_Touched.Count() < 4096; cursor++)
  {
   int current = m_Touched[cursor];
   int hops = m_Hops[current];
   if (hops >= maxHops) continue;
   foreach (int next : Nodes[current].Links)
   {
    if (m_Hops[next] >= 0 || !Nodes[next].IndoorWalk) continue;
    m_Hops[next] = hops + 1;
    m_Touched.Insert(next);
   }
  }
 }

 int Hops(int node)
 {
  if (node < 0 || node >= m_Hops.Count()) return -1;
  return m_Hops[node];
 }

 int WalkGroupCount()
 {
  return m_GroupStops.Count();
 }

 // Free-room rule: patrollers join a walk group only while at least this many of
 // its stops stay free, max(2, a third of its stops).
 int KeepFree(int group)
 {
  if (group < 0 || group >= m_GroupStops.Count()) return 1000;
  int keep = Math.Ceil(m_GroupStops[group] / 3.0);
  if (keep < 2) keep = 2;
  return keep;
 }

 int GroupStopCount(int group)
 {
  if (group < 0 || group >= m_GroupStops.Count()) return 0;
  return m_GroupStops[group];
 }

 // Keeps the lowest scores (ascending, at most keep entries) of a bounded list.
 static void KeepBest(notnull array<int> nodes, notnull array<float> scores, int node, float score, int keep)
 {
  int at = 0;
  while (at < scores.Count() && scores[at] <= score) { at++; }
  if (at >= keep) return;
  nodes.InsertAt(node, at);
  scores.InsertAt(score, at);
  if (nodes.Count() > keep)
  {
   nodes.RemoveOrdered(keep);
   scores.RemoveOrdered(keep);
  }
 }

 // A patrol leg: a free stop of the same indoor area 5-20 steps away, seven times
 // in ten on the same storey (otherwise 2-40 steps on any storey), one of the four
 // farthest from every other claim, picked at random and claimed for the owner.
 int ClaimRoamStop(IEntity owner, int from, notnull array<int> avoid)
 {
  if (!owner || from < 0 || from >= Nodes.Count()) return -1;
  DistancesFrom(from, 40);
  bool sameStorey = Math.RandomFloat01() < 0.7;
  array<int> picks = {};
  array<float> scores = {};
  for (int pass = 0; pass < 2 && picks.IsEmpty(); pass++)
  {
   foreach (int stop : RoamStops)
   {
    int hops = Hops(stop);
    if (stop == from || hops < 2 || avoid.Contains(stop)) continue;
    if (pass == 0 && (hops < 5 || hops > 20 || (sameStorey && Nodes[stop].Storey != Nodes[from].Storey))) continue;
    if (!StopFree(stop, owner)) continue;
    float clearance = 10;
    foreach (EXPG_BuildingReservation reservation : m_Reservations)
    {
     if (!reservation.Owner || reservation.Owner == owner) continue;
     clearance = Math.Min(clearance, vector.Distance(Nodes[stop].Position, Nodes[reservation.To].Position));
    }
    KeepBest(picks, scores, stop, -clearance, 4);
   }
  }
  while (!picks.IsEmpty())
  {
   int choice = Math.RandomInt(0, picks.Count());
   int pick = picks[choice];
   if (TryClaimStop(owner, pick)) return pick;
   picks.RemoveOrdered(choice);
  }
  return -1;
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
   if (part == Structure) continue;
   DoorComponent door = DoorComponent.Cast(part.FindComponent(DoorComponent));
   // Every leaf (both leaves of a double door) keeps its own keep-out zone.
   if (door) AddDoorLeaf(part, door);
   if (m_Openings.Count() >= 64) continue;
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
   // Both leaves of a double door share one frame: one opening, watched once.
   bool known = false;
   foreach (EXPG_BuildingOpening listed : m_Openings) { if (listed.Part == opening.Part) { known = true; break; } }
   if (known) continue;
   vector mins, maxs; opening.Part.GetBounds(mins, maxs);
   opening.Position = opening.Part.CoordToParent((mins + maxs) * 0.5);
   opening.Door = door != null;
   m_Openings.Insert(opening);
  }
 }

 // The hinge is the same whether the door is open or closed; the doorway's
 // direction comes from the frame (the leaf itself when it has none, assumed
 // closed), signed from the hinge towards the frame centre.
 protected void AddDoorLeaf(IEntity part, DoorComponent door)
 {
  if (m_DoorLeaves.Count() >= 64) return;
  IEntity frameEntity = part;
  if (part.GetParent() && part.GetParent() != Structure) frameEntity = part.GetParent();
  vector frame[4]; frameEntity.GetWorldTransform(frame);
  vector mins, maxs; frameEntity.GetBounds(mins, maxs);
  vector across = frame[0];
  if (maxs[0] - mins[0] < maxs[2] - mins[2]) across = frame[2];
  across[1] = 0;
  if (across.Length() < 0.1) return;
  across.Normalize();
  EXPG_DoorLeaf leaf = new EXPG_DoorLeaf();
  leaf.Hinge = door.GetDoorPivotPointWS();
  vector leafMins, leafMaxs; part.GetBounds(leafMins, leafMaxs);
  // A closed leaf lies across the doorway from its hinge; an open one points
  // into a room, so the frame centre decides then.
  vector centre = part.CoordToParent((leafMins + leafMaxs) * 0.5);
  float side = (centre[0] - leaf.Hinge[0]) * across[0] + (centre[2] - leaf.Hinge[2]) * across[2];
  if (Math.AbsFloat(side) < 0.1)
  {
   centre = frameEntity.CoordToParent((mins + maxs) * 0.5);
   side = (centre[0] - leaf.Hinge[0]) * across[0] + (centre[2] - leaf.Hinge[2]) * across[2];
  }
  if (side < 0) across = across * -1;
  leaf.Along = across;
  leaf.LeafSpan = Math.Clamp(Math.Max(leafMaxs[0] - leafMins[0], leafMaxs[2] - leafMins[2]), 0.5, 1.8);
  vector worldMins, worldMaxs; part.GetWorldBounds(worldMins, worldMaxs);
  leaf.FloorY = worldMins[1];
  m_DoorLeaves.Insert(leaf);
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

 // Walls, windows or doors of this building on at least six of eight
 // building-aligned bearings. A porch, the entrance steps or the ground under the
 // eaves can lie inside a generous authored interior volume and under the roof,
 // but three or more bearings there leave the house without meeting it.
 bool Enclosed(vector eye)
 {
  if (!Structure || !Inside(eye)) return false;
  float reach = vector.Distance(Mins, Maxs) + 1;
  int walls = 0;
  for (int bearing = 0; bearing < 8; bearing++)
  {
   TraceParam ray = new TraceParam();
   ray.Start = eye;
   ray.End = eye + vector.FromYaw(bearing * 45 + Angles[1]) * reach;
   ray.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   if (GetGame().GetWorld().TraceMove(ray, null) < 0.999 && IsBuilding(ray.TraceEnt)) walls++;
   if (walls + 7 - bearing < 6) return false;
  }
  return walls >= 6;
 }

 // Standing floor (building floor or indoor ground) below a point, as a body meets it.
 protected bool FloorAt(vector spot, float above, float below, out float height)
 {
  TraceParam foot = new TraceParam();
  foot.Start = spot + Vector(0, above, 0);
  foot.End = spot - Vector(0, below, 0);
  foot.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  foot.LayerMask = EPhysicsLayerDefs.CharacterAI;
  float hit = GetGame().GetWorld().TraceMove(foot, null);
  if (hit >= 0.999 || foot.TraceNorm[1] < 0.65) return false;
  vector floorPoint = vector.Lerp(foot.Start, foot.End, hit);
  if (!IndoorFloor(foot.TraceEnt, floorPoint)) return false;
  height = floorPoint[1];
  return true;
 }

 // The floor itself rises or falls by a step within 0.3 m (stair treads, ramps,
 // a raised threshold), unlike a flat floor next to low furniture.
 protected bool SteppedFloor(vector point)
 {
  float level = point[1] - 0.05;
  for (int side = 0; side < 4; side++)
  {
   float height;
   if (!FloorAt(point + vector.FromYaw(side * 90 + Angles[1]) * 0.3, 0.6, 0.6, height)) continue;
   float rise = Math.AbsFloat(height - level);
   if (rise >= 0.06 && rise <= 0.55) return true;
  }
  return false;
 }

 // Standing volume above step height: what a body on a stair flight occupies.
 protected bool ClearAbove(vector point, float lift)
 {
  TraceBox trace = new TraceBox();
  trace.Start = point + Vector(0, lift, 0);
  trace.End = trace.Start;
  trace.Mins = "-0.23 0 -0.23";
  trace.Maxs = Vector(0.23, 1.8 - lift, 0.23);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  trace.LayerMask = EPhysicsLayerDefs.CharacterAI;
  return GetGame().GetWorld().TracePosition(trace, null) >= 0;
 }

 // An outer door has an enclosed room on one side only (probed 1.2 m out along
 // the frame's thin axis). Door posts watch it from that side, never the steps.
 protected void ClassifyDoors()
 {
  foreach (EXPG_BuildingOpening opening : m_Openings)
  {
   if (!opening.Door || !opening.Part) continue;
   vector frame[4]; opening.Part.GetWorldTransform(frame);
   vector mins, maxs; opening.Part.GetBounds(mins, maxs);
   vector normal = frame[2];
   if (maxs[0] - mins[0] < maxs[2] - mins[2]) normal = frame[0];
   normal[1] = 0;
   if (normal.Length() < 0.1) continue;
   normal.Normalize();
   opening.Side = Vector(normal[2], 0, -normal[0]);
   opening.HalfWidth = Math.Clamp(0.5 * Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]), 0.3, 1.5);
   bool front = Enclosed(opening.Position + normal * 1.2);
   bool back = Enclosed(opening.Position - normal * 1.2);
   if (front == back) continue;
   opening.Exterior = true;
   opening.Inward = normal;
   if (back) opening.Inward = normal * -1;
  }
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
  // The trace only reads its exclusion list: the doors are passed as they are, and
  // a merged copy is made only when another entity must be excluded too.
  array<IEntity> exclusions;
  if (planning && !m_TraversableDoors.IsEmpty())
  {
   if (!exclude && !excludeEntities) { trace.ExcludeArray = m_TraversableDoors; }
   else
   {
    exclusions = new array<IEntity>();
    exclusions.Copy(m_TraversableDoors);
    if (exclude) exclusions.Insert(exclude);
    if (excludeEntities) exclusions.InsertAll(excludeEntities);
    trace.ExcludeArray = exclusions;
   }
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
  float rise = Math.AbsFloat(from[1] - to[1]);
  if (rise > 0.48) { return StairEdge(from, to, rise); }
  if (!ClearBody(from, to, null, null, true)) { return false; }
  for (int i = 1; i < 3; i++)
  {
   vector sample = vector.Lerp(from, to, i / 3.0);
   if (!Supported(sample, 0.25)) { return false; }
  }
  return true;
 }

 // Neighbouring samples on a stair flight (0.75 m apart) differ by more than one
 // step. Accept up to 45 degrees when the floor between them climbs in steps of at
 // most 0.3 m close to the straight line (never a ledge, sill or wall top) and a
 // standing body clears the whole way above step height.
 protected bool StairEdge(vector from, vector to, float rise)
 {
  if (rise > vector.DistanceXZ(from, to) + 0.05) { return false; }
  float previous = from[1] - 0.05;
  for (int i = 1; i < 5; i++)
  {
   vector sample = vector.Lerp(from, to, i / 5.0);
   float height;
   if (!FloorAt(sample, STEP_HEIGHT, 0.6, height)) { return false; }
   if (Math.AbsFloat(height - sample[1] + 0.05) > 0.35 || Math.AbsFloat(height - previous) > 0.3) { return false; }
   previous = height;
  }
  if (Math.AbsFloat(to[1] - 0.05 - previous) > 0.3) { return false; }
  return ClearBody(from, to, null, null, true);
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
   if (!opening.Door || !opening.Part) continue;
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
   else if (m_Phase == 2) { MarkReachable(); ClassifyDoors(); m_Phase = 3; m_Node = 0; }
   else if (m_Phase == 3) { ScoreOne(); }
   else if (m_Phase == 4) { SelectSlots(); BuildWalk(); m_Phase = 5; }
   else if (m_Phase == 5) { RoamStep(); }
   else { WatchStep(); }
  }
 }

 // Analysis progress from 0 to 1 for the Game Master's hint: sampling counts
 // 45 %, linking 20 %, scoring 30 %, patrol stops and their watch directions the
 // last 4 % of the bar, each by its own position (columns, nodes or stops done of
 // the total), so the value never decreases. It stays below 1 until the plan is
 // done (with or without an error).
 float Progress()
 {
  if (Done) return 1;
  float nodes = Nodes.Count();
  if (m_Phase == 0)
  {
   float columns = Columns.Count();
   if (columns < 1) return 0;
   return 0.45 * Math.Min(1, m_Column * 1.0 / columns);
  }
  if (m_Phase == 1)
  {
   if (nodes < 1) return 0.65;
   return 0.45 + 0.2 * Math.Min(1, m_Node * 1.0 / nodes);
  }
  if (m_Phase == 2) return 0.65;
  if (m_Phase == 3)
  {
   if (nodes < 1) return 0.95;
   return 0.65 + 0.3 * Math.Min(1, m_Node * 1.0 / nodes);
  }
  if (m_Phase == 4) return 0.95;
  if (m_Phase == 5) return 0.95 + 0.02 * Math.Min(1, RoamStops.Count() * 1.0 / MAX_ROAM);
  float stops = RoamStops.Count();
  if (stops < 1) return 0.99;
  return 0.97 + 0.02 * Math.Min(1, m_WatchCursor * 1.0 / stops);
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
  // Sample belongs to the column before NextColumn advanced it.
  int column = z * m_Width + x;
  if (AdmitNode(point, column) != 1) { return; }
  // A narrow stair flight or doorway can fall between grid lines: when a body
  // does not fit at the cell centre, try the same floor a quarter metre aside.
  for (int side = 0; side < 4; side++)
  {
   vector aside = point + vector.FromYaw(side * 90 + Angles[1]) * 0.25;
   float asideFloor;
   if (!FloorAt(aside, 0.5, 0.5, asideFloor)) continue;
   aside[1] = asideFloor + 0.05;
   if (AdmitNode(aside, column, true) != 1) return;
  }
 }

 // 0 admitted, 1 no room for a body here, 2 rejected for another reason.
 protected int AdmitNode(vector point, int column, bool aside = false)
 {
  if (!Inside(point)) return 2;
  bool stair = false;
  if (!ClearBody(point, point + "0 0.01 0"))
  {
   // Stair treads and ramps rise into the lower-leg box. Where the floor itself
   // steps and a body fits above step height, keep a transit-only stair node so
   // upper floors connect; it never becomes a post.
   if (!ClearAbove(point, STEP_HEIGHT) || !SteppedFloor(point)) return 1;
   stair = true;
  }
  TraceParam ceiling = new TraceParam();
  ceiling.Start = point + "0 1.8 0";
  ceiling.End = point + "0 30 0";
  ceiling.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(ceiling, null) >= 0.999 || !IsBuilding(ceiling.TraceEnt)) return 2;
  if (Nodes.Count() >= MAX_NODES) { Fail("Structure has too many interior samples"); return 2; }
  EXPG_BuildingNode node = new EXPG_BuildingNode();
  node.Position = point;
  node.Column = column;
  // A body beside a spot where none fit stands pressed against the obstruction
  // (live: a window post the wall pushed the guard off, so he could not crouch);
  // like a stair node it only links floors and doorways, never a post or stop.
  node.Stair = stair || aside;
  node.Entrance = Entrance(point) || DoorEntrance(point) || PerimeterEntrance(point);
  node.Interior = !node.Stair && InteriorPoint(point);
  Columns[column].Nodes.Insert(Nodes.Count());
  Nodes.Insert(node);
  return 0;
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
  // Nodes x leaves of scalar math; walking through stays allowed.
  node.DoorBlock = InAnyDoorZone(node.Position);
  if (!node.Reachable || node.Stair) { return; }
  node.Look = vector.FromYaw(Angles[1]);
  vector eye = node.Position + "0 1.5 0";
  node.Enclosed = node.Interior && Enclosed(eye);
  // In a door's swing or doorway: never a post, entrance post, patrol stop or
  // watch point (a guard there blocked the front door in a live test).
  if (node.DoorBlock) { return; }
  if (node.Entrance) { node.Score = SCORE_ENTRANCE; }
  foreach (int link : node.Links)
  {
   if (Math.AbsFloat(Nodes[link].Position[1] - node.Position[1]) > 0.15)
   { node.Score = Math.Max(node.Score, SCORE_STAIRS); node.Look = vector.Direction(node.Position, Nodes[link].Position).Normalized(); }
  }
  for (int s = 0; s < m_Sentinels.Count(); s++)
  {
   if (vector.DistanceSq(node.Position, m_Sentinels[s]) > 0.5) { continue; }
   node.Score = SCORE_SENTINEL;
   node.Look = vector.Direction(node.Position, m_SentinelLooks[s]).Normalized();
   return;
  }
  // Watch a window from 0.4-3 m (the nearest window wins; SelectSlots takes the
  // nearest watcher of each window first), or an outer door from 2-4 m on its room
  // side, at least 1.2 m deep and outside every door zone (above), facing the
  // doorway; a spot in line with the doorway ranks 1 m behind a diagonal one.
  // A window outranks a door; interior doors are not watched.
  // Only sampled nodes qualify: each already passed floor, standing-body
  // clearance, ceiling, interior and entrance-reachability checks, so no position
  // is synthesized. 0.4 m from the pane centre is about a body against a thin
  // wall; it keeps the muzzle near the window plane and the yaw well defined.
  foreach (int index, EXPG_BuildingOpening opening : m_Openings)
  {
   vector delta = opening.Position - eye;
   float horizontal = delta[0] * delta[0] + delta[2] * delta[2];
   if (Math.AbsFloat(delta[1]) > 0.75) continue;
   int score = SCORE_WINDOW;
   float rank = Math.Sqrt(horizontal);
   if (opening.Door)
   {
    if (!opening.Exterior || horizontal < 4 || horizontal > 16) continue;
    vector inward = node.Position - opening.Position;
    inward[1] = 0;
    if (vector.Dot(inward, opening.Inward) < 1.2) continue;
    if (Math.AbsFloat(vector.Dot(inward, opening.Side)) <= opening.HalfWidth + 0.5) rank += 1.0;
    score = SCORE_DOOR;
   }
   else if (horizontal < 0.16 || horizontal > 9) continue;
   if (node.Score > score || (node.Score == score && rank >= node.Range)) continue;
   if (!SeesOpening(eye, opening)) continue;
   node.Score = score;
   node.Watch = index;
   node.Range = rank;
   node.Opening = OPENING_WINDOW;
   if (opening.Door) node.Opening = OPENING_DOOR;
   // Level gaze: at close range the opening's centre is often well above or below
   // the eye; a pitched look would aim at the ceiling or the ground beyond it.
   delta[1] = 0;
   node.Look = delta.Normalized();
  }
  if (node.Score >= SCORE_DOOR) return;
  // Open window holes: the nearest clear outward view wins. Its range is where
  // the view leaves the building bounds (eaves add about the same per building).
  vector viewer = eye;
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
   node.Score = SCORE_WINDOW;
   node.Opening = OPENING_WINDOW;
   node.Watch = -1;
   node.Look = direction;
   node.Range = reach;
  }
 }

 // At most MAX_SLOTS places; greedy spacing over a bounded graph avoids a large
 // solver. Fixed kinds in priority order: sentinels, windows, outer doors,
 // entrance approaches, stair watch. Each kind is taken first WIDE_SPACING apart
 // with one watcher per window or door, then POST_SPACING apart with up to two.
 // Patrol starts (interior roam stops) are appended after the last phase
 // (AppendPatrolStarts). A fresh squad takes Slots in this order, so it spreads
 // over every floor.
 protected void SelectSlots()
 {
  RequireEnclosure();
  BuildStoreys();
  m_Turn = 0;
  array<int> openingUse = {};
  for (int o = 0; o < m_Openings.Count(); o++) { openingUse.Insert(0); }
  array<bool> wide = {};
  array<bool> dense = {};
  for (int n = 0; n < Nodes.Count(); n++) { wide.Insert(false); dense.Insert(false); }
  array<int> priorities = {};
  priorities.Insert(SCORE_SENTINEL);
  priorities.Insert(SCORE_WINDOW);
  priorities.Insert(SCORE_DOOR);
  priorities.Insert(SCORE_ENTRANCE);
  priorities.Insert(SCORE_STAIRS);
  array<int> pool = {};
  foreach (int priority : priorities)
  {
   Pool(priority, pool);
   PickSlots(pool, wide, openingUse, 1, wide, dense);
   PickSlots(pool, dense, openingUse, 2, wide, dense);
  }
 }

 // Indoor walking floor, its connected areas, the alarm windows and watch points,
 // and the roam stop candidates (phase 4, one bounded pass over the nodes).
 protected void BuildWalk()
 {
  foreach (EXPG_BuildingNode clear : Nodes)
  {
   clear.IndoorWalk = clear.Reachable && clear.Interior && !clear.Stair;
   clear.WalkGroup = -1;
   clear.RoamStop = false;
  }
  // Indoor staircases and squeezed doorway nodes; entrance steps touch outdoors.
  array<bool> seen = {};
  for (int n = 0; n < Nodes.Count(); n++) { seen.Insert(false); }
  array<int> flight = {};
  for (int start = 0; start < Nodes.Count(); start++)
  {
   if (seen[start] || !Nodes[start].Stair || !Nodes[start].Reachable) continue;
   flight.Clear();
   flight.Insert(start);
   seen[start] = true;
   bool indoor = true;
   for (int cursor = 0; cursor < flight.Count(); cursor++)
   {
    foreach (int next : Nodes[flight[cursor]].Links)
    {
     EXPG_BuildingNode neighbour = Nodes[next];
     if (!neighbour.Stair)
     {
      if (!neighbour.IndoorWalk) indoor = false;
      continue;
     }
     if (seen[next] || !neighbour.Reachable) continue;
     seen[next] = true;
     flight.Insert(next);
    }
   }
   if (!indoor) continue;
   foreach (int tread : flight) { Nodes[tread].IndoorWalk = true; }
  }
  int groups = 0;
  array<int> queue = {};
  for (int seed = 0; seed < Nodes.Count(); seed++)
  {
   if (!Nodes[seed].IndoorWalk || Nodes[seed].WalkGroup >= 0) continue;
   queue.Clear();
   queue.Insert(seed);
   Nodes[seed].WalkGroup = groups;
   for (int head = 0; head < queue.Count(); head++)
   {
    foreach (int link : Nodes[queue[head]].Links)
    {
     if (!Nodes[link].IndoorWalk || Nodes[link].WalkGroup >= 0) continue;
     Nodes[link].WalkGroup = groups;
     queue.Insert(link);
    }
   }
   groups++;
  }
  m_GroupStops.Clear();
  for (int g = 0; g < groups; g++) { m_GroupStops.Insert(0); }
  RoamStops.Clear();
  WindowNodes.Clear();
  WatchNodes.Clear();
  m_RoamCandidates.Clear();
  m_RoamGap.Clear();
  m_RoamSeen.Clear();
  foreach (int index, EXPG_BuildingNode candidate : Nodes)
  {
   if (!candidate.IndoorWalk || !candidate.Interior || candidate.Stair || candidate.DoorBlock) continue;
   if (candidate.Score == SCORE_WINDOW)
   {
    if (WindowNodes.Count() < MAX_ALERT_NODES) WindowNodes.Insert(index);
   }
   else if (candidate.Score >= SCORE_STAIRS && WatchNodes.Count() < MAX_ALERT_NODES) WatchNodes.Insert(index);
   float gap = 1000000;
   foreach (int slot : Slots) { gap = Math.Min(gap, FloorGap(candidate.Position, Nodes[slot].Position)); }
   if (gap < POST_SPACING * POST_SPACING) continue;
   m_RoamCandidates.Insert(index);
   m_RoamGap.Insert(gap);
   m_RoamSeen.Insert(0);
  }
  m_RoamFull.Clear();
  for (int s = 0; s < StoreyCount(); s++) { m_RoamFull.Insert(false); }
  m_RoamCursor = 0;
  m_RoamBest = -1;
  m_RoamBestGap = -1;
  m_RoamStorey = 0;
  m_WatchCursor = 0;
 }

 // Squared horizontal distance on about the same floor (as Crowded); far otherwise.
 static float FloorGap(vector a, vector b)
 {
  if (Math.AbsFloat(a[1] - b[1]) >= 2.0) return 1000000;
  return vector.DistanceSqXZ(a, b);
 }

 // Phase 5, bounded work per operation (256 candidates): one pass over the
 // candidates for one storey picks the one farthest from every slot and stop
 // (at least POST_SPACING); storeys take turns. A storey with no such candidate
 // stays full (gaps only shrink). At most MAX_ROAM stops.
 protected void RoamStep()
 {
  int storeys = m_RoamFull.Count();
  if (storeys < 1 || RoamStops.Count() >= MAX_ROAM || m_RoamCandidates.IsEmpty() || !m_RoamFull.Contains(false)) { FinishRoam(); return; }
  if (m_RoamFull[m_RoamStorey]) { m_RoamStorey = (m_RoamStorey + 1) % storeys; return; }
  int end = Math.Min(m_RoamCursor + 256, m_RoamCandidates.Count());
  while (m_RoamCursor < end)
  {
   int at = m_RoamCursor;
   m_RoamCursor++;
   EXPG_BuildingNode node = Nodes[m_RoamCandidates[at]];
   if (node.RoamStop || node.Storey != m_RoamStorey) continue;
   float gap = m_RoamGap[at];
   for (int s = m_RoamSeen[at]; s < RoamStops.Count(); s++) { gap = Math.Min(gap, FloorGap(node.Position, Nodes[RoamStops[s]].Position)); }
   m_RoamSeen[at] = RoamStops.Count();
   m_RoamGap[at] = gap;
   if (gap < POST_SPACING * POST_SPACING || gap <= m_RoamBestGap) continue;
   m_RoamBestGap = gap;
   m_RoamBest = m_RoamCandidates[at];
  }
  if (m_RoamCursor < m_RoamCandidates.Count()) return;
  if (m_RoamBest >= 0)
  {
   Nodes[m_RoamBest].RoamStop = true;
   RoamStops.Insert(m_RoamBest);
  }
  else m_RoamFull[m_RoamStorey] = true;
  m_RoamStorey = (m_RoamStorey + 1) % storeys;
  m_RoamCursor = 0;
  m_RoamBest = -1;
  m_RoamBestGap = -1;
 }

 protected void FinishRoam()
 {
  for (int g = 0; g < m_GroupStops.Count(); g++) { m_GroupStops[g] = 0; }
  foreach (int stop : RoamStops)
  {
   int group = Nodes[stop].WalkGroup;
   if (group >= 0 && group < m_GroupStops.Count()) m_GroupStops[group] = m_GroupStops[group] + 1;
  }
  m_RoamCandidates.Clear();
  m_RoamGap.Clear();
  m_RoamSeen.Clear();
  m_WatchCursor = 0;
  m_Phase = 6;
 }

 // Phase 6: one stop's watch direction per operation, then the patrol starts.
 protected void WatchStep()
 {
  if (m_WatchCursor < RoamStops.Count())
  {
   EXPG_BuildingNode stop = Nodes[RoamStops[m_WatchCursor]];
   m_WatchCursor++;
   stop.WatchLook = LongestView(stop.Position + "0 1.5 0");
   return;
  }
  AppendPatrolStarts();
  if (Slots.IsEmpty()) { Error = "No connected, clear indoor positions were found"; }
  Done = true;
 }

 // The longest of eight building-aligned rays at eye height that stays inside the
 // building, at most 15 m: a patroller at a stop faces a hallway or doorway.
 protected vector LongestView(vector eye)
 {
  vector best = vector.FromYaw(Angles[1]);
  float longest = -1;
  for (int bearing = 0; bearing < 8; bearing++)
  {
   vector direction = vector.FromYaw(bearing * 45 + Angles[1]);
   TraceParam ray = new TraceParam();
   ray.Start = eye;
   ray.End = eye + direction * 15;
   ray.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   float reach = GetGame().GetWorld().TraceMove(ray, null) * 15;
   if (reach <= longest || !Inside(eye + direction * reach)) continue;
   longest = reach;
   best = direction;
  }
  return best;
 }

 // Patrol starts for a building's first squad, in roam stop order (spread over
 // the storeys), under the free-room rule (KeepFree): no fixed post, never in a
 // door zone, 1.5 m from every slot.
 protected void AppendPatrolStarts()
 {
  array<int> used = {};
  for (int g = 0; g < m_GroupStops.Count(); g++) { used.Insert(0); }
  foreach (int stop : RoamStops)
  {
   if (Slots.Count() >= MAX_SLOTS) break;
   int group = Nodes[stop].WalkGroup;
   if (group < 0 || group >= used.Count()) continue;
   if (m_GroupStops[group] - used[group] - 1 < KeepFree(group)) continue;
   used[group] = used[group] + 1;
   Slots.Insert(stop);
   FixedSlots.Insert(false);
  }
 }

 // Posts closer than spacing on about the same floor (under 2 m apart in height).
 static bool Crowded(vector a, vector b, float spacing)
 {
  return Math.AbsFloat(a[1] - b[1]) < 2.0 && vector.DistanceXZ(a, b) < spacing;
 }

 int StoreyCount()
 {
  return Math.Max(1, m_StoreyBases.Count());
 }

 // Houses: once most of the indoor floor is enclosed by walls, porches, entrance
 // steps and ground under the eaves no longer count as indoors (no post, patrol
 // stop or extra position there). Open sheds, barns and piers keep their roofed
 // floor when too little of it is enclosed.
 protected void RequireEnclosure()
 {
  int sheltered = 0;
  int enclosed = 0;
  foreach (EXPG_BuildingNode node : Nodes)
  {
   if (!node.Reachable || !node.Interior || node.Stair) continue;
   sheltered++;
   if (node.Enclosed) enclosed++;
  }
  if (enclosed < 4 || enclosed * 4 < sheltered) return;
  foreach (EXPG_BuildingNode outdoor : Nodes)
  {
   if (!outdoor.Enclosed) outdoor.Interior = false;
  }
 }

 // Floor levels of the indoor candidates: a storey starts more than 2 m above the
 // lowest floor of the one below, so a raised room or a stair landing stays in it.
 protected void BuildStoreys()
 {
  m_StoreyBases.Clear();
  array<float> heights = {};
  foreach (EXPG_BuildingNode node : Nodes)
  {
   if (node.Reachable && node.Interior && !node.Stair) heights.Insert(node.Position[1]);
  }
  heights.Sort();
  foreach (float height : heights)
  {
   if (m_StoreyBases.IsEmpty() || height - m_StoreyBases[m_StoreyBases.Count() - 1] > 2.0) m_StoreyBases.Insert(height);
  }
  foreach (EXPG_BuildingNode level : Nodes)
  {
   level.Storey = 0;
   for (int s = m_StoreyBases.Count() - 1; s > 0; s--)
   {
    if (level.Position[1] + 0.05 >= m_StoreyBases[s]) { level.Storey = s; break; }
   }
  }
 }

 // Indoor candidates of one priority outside every door zone; an opening's
 // nearest watcher comes first.
 protected void Pool(int priority, notnull array<int> pool)
 {
  pool.Clear();
  array<int> keys = {};
  foreach (int index, EXPG_BuildingNode node : Nodes)
  {
   if (!node.Reachable || !node.Interior || node.Stair || node.DoorBlock || node.Score != priority) continue;
   int centimetres = Math.Round(node.Range * 100);
   if (centimetres > 100000) centimetres = 100000;
   keys.Insert(centimetres * 10000 + index);
  }
  keys.Sort();
  foreach (int key : keys) { pool.Insert(key % 10000); }
 }

 // One kind at one spacing. Storeys take turns; on a storey the candidate farthest
 // from every post already chosen wins, each window or door represented by its
 // nearest free watcher, so posts spread over floors and facades instead of
 // filling the first room the grid scanned.
 protected void PickSlots(notnull array<int> pool, notnull array<bool> blocked, notnull array<int> openingUse, int perOpening, notnull array<bool> wide, notnull array<bool> dense)
 {
  int storeys = StoreyCount();
  while (Slots.Count() < MAX_SLOTS)
  {
   int pick = -1;
   for (int turn = 0; turn < storeys && pick < 0; turn++)
   {
    pick = Farthest(pool, (m_Turn + turn) % storeys, blocked, openingUse, perOpening);
   }
   if (pick < 0) return;
   m_Turn = (Nodes[pick].Storey + 1) % storeys;
   AddSlot(pick, openingUse, wide, dense);
  }
 }

 protected int Farthest(array<int> pool, int storey, array<bool> blocked, array<int> openingUse, int perOpening)
 {
  array<int> watched = {};
  int best = -1;
  float bestClearance = -1;
  int loose = 0;
  foreach (int index : pool)
  {
   EXPG_BuildingNode node = Nodes[index];
   if (blocked[index] || node.Storey != storey) continue;
   if (node.Watch >= 0 && node.Watch < openingUse.Count())
   {
    if (openingUse[node.Watch] >= perOpening || watched.Contains(node.Watch)) continue;
    watched.Insert(node.Watch);
   }
   else
   {
    // Bounded work: only the first 64 free unwatched candidates of this storey
    // (pool order) are compared; each pick blocks its neighbours, so later
    // candidates come up in later picks.
    loose++;
    if (loose > 64) continue;
   }
   float clearance = 1000000;
   foreach (int slot : Slots)
   {
    clearance = Math.Min(clearance, vector.DistanceSq(node.Position, Nodes[slot].Position));
   }
   if (clearance > bestClearance)
   {
    best = index;
    bestClearance = clearance;
   }
  }
  return best;
 }

 protected void AddSlot(int index, array<int> openingUse, array<bool> wide, array<bool> dense)
 {
  EXPG_BuildingNode chosen = Nodes[index];
  Slots.Insert(index);
  FixedSlots.Insert(chosen.Score > 0);
  if (chosen.Watch >= 0 && chosen.Watch < openingUse.Count()) openingUse[chosen.Watch] = openingUse[chosen.Watch] + 1;
  foreach (int i, EXPG_BuildingNode other : Nodes)
  {
   if (!wide[i] && Crowded(other.Position, chosen.Position, WIDE_SPACING)) wide[i] = true;
   if (!dense[i] && Crowded(other.Position, chosen.Position, POST_SPACING)) dense[i] = true;
  }
 }

 // Reorders node indices so the storeys take turns (stable within a storey).
 void InterleaveStoreys(notnull array<int> nodes)
 {
  int storeys = StoreyCount();
  int total = nodes.Count();
  array<ref array<int>> levels = {};
  for (int s = 0; s < storeys; s++) { levels.Insert(new array<int>()); }
  foreach (int index : nodes) { levels[Math.ClampInt(Nodes[index].Storey, 0, storeys - 1)].Insert(index); }
  nodes.Clear();
  for (int cursor = 0; nodes.Count() < total; cursor++)
  {
   foreach (array<int> level : levels)
   {
    if (cursor < level.Count()) nodes.Insert(level[cursor]);
   }
  }
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
