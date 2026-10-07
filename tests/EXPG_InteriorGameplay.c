// TEST ONLY. Interior behaviour of garrisons on a two-storey town house. Live test
// 2026-10-07: 17 guards in a two-storey Morton house held their posts and won four
// firefights, but one guard stood right behind the front door so it could not be
// opened; the Game Master asked for guards farther from doors and for overflow
// soldiers to patrol inside, take free windows (or watch a hallway or door) when a
// firefight starts, and not bunch up.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_InteriorGameplay.c -ExpectResult '\[EXPG INTERIOR RESULT\] checks=[1-9]\d* failures=0 leaves=[1-9]\d* zoned=0 doorGuards=0 doorsOpened=\d+ doorsBlocked=0 guards=[1-9]\d* rovers=[3-9]\d* roamMoved=[3-9]\d* roamOutside=0 stacked=0 overlap=0 alertWindows=\d+ alertWatch=\d+ alertOutside=0 alertStacked=0 calmReturned=1 killed=1 fullRovers=[2-9]\d* simRovers=[2-9]\d* replenished=0 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Uses a House_Town_E_2I01 of the Everon map itself (Saint-Philippe first; its floors
// are in the baked navmesh, which a house spawned at run time lacks upstairs; it
// spawns one only if none is found) and lets the production planner analyse it. Door clearance: no fixed slot, patrol stop, alarm
// window or watch point lies in a door zone (the planner's own test and an
// independent one: within leaf width + 0.5 m of the hinge, or 0.8 m of the closed
// leaf's centre, on its floor), every door that opens on the empty house still
// opens fully once the guards stand (three tries), and no standing guard is in a
// door zone. US fire teams are added through the editor's production calls
// (CanFit, fresh roster, AdoptFresh; caching Off) until at least three soldiers
// patrol inside (at most twelve adds; overflow beyond the fixed posts patrols). Patrol, 60 s sampled every 0.5 s: every
// patroller travels at least 3 m over at least two claimed stops, never outside
// (independent roof and sixteen-bearing test) or off the indoor floor (no indoor
// walking node within 1.5 m); standing
// soldiers stay 1.2 m apart, any two 0.5 m; claims stay unique and 1.5 m apart.
// Alarm: ThreatBulletImpact(5) on one fixed guard (the real threat-state event):
// within 30 s every patroller holds a free window, a distinct watch point or his
// stop, none outside, no stacking, at least one window when a free one was within
// the alarm's 40-step reach of a patroller. Outside: no house overhead (props
// such as wall lamps passed through), or six open bearings beyond the house's own
// bounds (open doors and windows open bearings inside). Calm:
// all back to patrol, one walking again. Then one patroller is killed; a Full and
// a Simulation cache cycle must bring the survivors back as patrollers near their
// claimed stops, the casualty stays dead, nobody is replenished. No players, GM UI,
// real combat or save/load. Deadline 540 s.
class EXPG_InteriorAdd
{
 SCR_AIGroup Group;
 ref EXPG_GarrisonRecord Record;
}

// One door leaf of the house, found by the fixture itself (not by the planner).
class EXPG_InteriorLeaf
{
 IEntity Part;
 vector Hinge;
 vector Centre;
 float LeafSpan;
 float FloorY;
 bool Opened;
}

// One patroller under watch.
class EXPG_InteriorRover
{
 ref EXPG_GarrisonMember Member;
 vector Last;
 float Travelled;
 ref array<int> Claims = {};
}

// Any refusal, normal-AI retention or survivor release fails the run.
modded class EXPG_GarrisonRecord
{
 static int EXPG_TestRefusals;
 override void Report(string message)
 {
  bool changed = Status != message;
  super.Report(message);
  if (!changed) return;
  if (message.Contains("retained") || message.Contains("refused") || message.Contains("releasing survivors") || message.Contains("cannot") || message.Contains("unavailable"))
  {
   EXPG_TestRefusals++;
   PrintFormat("[EXPG INTERIOR REFUSAL] group=%1 status=%2", Group, message);
  }
 }
}

// Diagnostics only: why a patrol walk failed or a native path was refused.
modded class EXPG_PatrolControl
{
 override protected void Fail(float now)
 {
  vector at;
  if (m_Actor) at = m_Actor.GetOrigin();
  bool inside = m_Plan && m_Plan.Inside(at, 0.25);
  PrintFormat("[EXPG INTERIOR WALK FAIL] actor=%1 state=%2 node=%3 dest=%4 at=%5 inside=%6 late=%7 pathCount=%8 pathOk=%9", m_Actor, m_State, m_Node, m_Dest, at, inside, now > m_MoveDeadline, m_PathCount, m_PathOk);
  super.Fail(now);
 }
 override protected bool PathInside()
 {
  bool ok = super.PathInside();
  if (ok || !m_Plan) return ok;
  vector bad;
  string why = "count";
  if (m_Path.Count() <= 64)
  {
   for (int i = 1; i < m_Path.Count(); i++)
   {
    vector point = m_Path[i];
    if (!m_Plan.Inside(point, 0.25)) { bad = point; why = "outsideBounds"; break; }
    if (!m_Plan.OnIndoorFloor(point)) { bad = point; why = "offFloor"; break; }
    if (m_Plan.NearParked(point, 0.6)) { bad = point; why = "parked"; break; }
    vector previous = m_Path[i - 1];
    int steps = Math.Ceil(vector.DistanceXZ(previous, point) / 0.5);
    bool found = false;
    for (int s = 1; s < steps; s++)
    {
     vector sample = vector.Lerp(previous, point, s * 1.0 / steps);
     if (!m_Plan.Contained(sample)) { bad = sample; why = "segment"; found = true; break; }
    }
    if (found) break;
   }
  }
  vector at;
  if (m_Actor) at = m_Actor.GetOrigin();
  PrintFormat("[EXPG INTERIOR PATH REFUSED] actor=%1 why=%2 point=%3 count=%4 at=%5 dest=%6 end=%7", m_Actor, why, bad, m_Path.Count(), at, m_Dest, m_PathEnd);
  return ok;
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 540;
 static const int SQUAD = 4;
 static const int MAX_ADDS = 12;
 static const float PATROL_SECONDS = 60;
 IEntity Structure;
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 ref array<ref EXPG_InteriorAdd> Adds = {};
 ref array<ref EXPG_InteriorLeaf> Leaves = {};
 ref array<ref EXPG_InteriorRover> Rovers = {};
 ref array<IEntity> Soldiers = {};
 ref EXPG_GarrisonMember Casualty;
 ObserversSystem Observers;
 int ObserverKey;
 int Phase;
 int Checks;
 int Failures;
 int Zoned;
 int DoorGuards;
 int DoorsOpened;
 int DoorsBlocked;
 int DoorTries;
 int Guards;
 int RoamMoved;
 int RoamOutside;
 int Stacked;
 int Overlap;
 int FreeWindows;
 int AlertWindows;
 int AlertWatch;
 int AlertHold;
 int AlertOutside;
 int AlertStacked;
 int CalmReturned;
 int Killed;
 int FullRovers;
 int SimRovers;
 int Replenished;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;
 vector Point = "4773.46 0 7094.57";
 // A House_Town_E_2I01 of the map itself (its floors are in the baked navmesh;
 // a house spawned at run time has navmesh only on the terrain under it, so
 // patrollers upstairs found no path). Spawned at Point only if none is found.
 IEntity MapHouse;
 bool Baked;
 bool AddMapHouse(IEntity entity)
 {
  if (MapHouse || !SCR_DestructibleBuildingEntity.Cast(entity)) return true;
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  if (!prefab.Contains("House_Town_E_2I01")) return true;
  MapHouse = entity;
  return false;
 }

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 15;
  ObserverKey = "EXPG_Interior.Observer".Hash();
  PrintFormat("[EXPG INTERIOR BEGIN] squad=%1 maxAdds=%2 deadline=%3 connectedPlayer=0", SQUAD, MAX_ADDS, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG INTERIOR CHECK] pass=%1 %2", value, label);
  return value;
 }
 void RemoveObserver()
 {
  if (Observers && GetGame() && GetGame().GetWorld() && GetGame().GetWorld().FindSystem(ObserversSystem) == Observers)
   Observers.RemoveObserverSP(ObserverKey);
 }
 void ~EXPG_GarrisonGameplay() { RemoveObserver(); }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  RemoveObserver();
  ClearEventMask(EntityEvent.FRAME);
  string first = string.Format("checks=%1 failures=%2 leaves=%3 zoned=%4 doorGuards=%5 doorsOpened=%6 doorsBlocked=%7", Checks, Failures, Leaves.Count(), Zoned, DoorGuards, DoorsOpened, DoorsBlocked);
  string second = string.Format("guards=%1 rovers=%2 roamMoved=%3 roamOutside=%4 stacked=%5 overlap=%6", Guards, Rovers.Count(), RoamMoved, RoamOutside, Stacked, Overlap);
  string third = string.Format("alertWindows=%1 alertWatch=%2 alertOutside=%3 alertStacked=%4 calmReturned=%5", AlertWindows, AlertWatch, AlertOutside, AlertStacked, CalmReturned);
  string fourth = string.Format("killed=%1 fullRovers=%2 simRovers=%3 replenished=%4 reason=%5", Killed, FullRovers, SimRovers, Replenished, reason);
  PrintFormat("[EXPG INTERIOR RESULT] %1 %2 %3 %4", first, second, third, fourth);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG INTERIOR PHASE] phase=%1 elapsed=%2 adds=%3 rovers=%4", Phase, Now() - Started, Adds.Count(), Rovers.Count());
 }
 EntitySpawnParams Params(vector point, float elevation = 0)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + elevation;
  spawn.Transform[3] = point;
  return spawn;
 }
 // Durable Full (0.1.11): a Full-cached garrison has no squad until it wakes; the
 // record recreates it and add.Group follows add.Record.Group (EOnFrame).
 bool Active(EXPG_InteriorAdd add)
 {
  if (!add.Record || add.Record.Finished || add.Record.ReleaseRequested) return false;
  if (add.Record.Full && !add.Record.Group) return true;
  return add.Group && add.Group.EXPG_Active && Manager.Find(add.Group) == add.Record;
 }
 // Diagnostics: why Outside judged a spot outside.
 string OutsideWhy(vector post)
 {
  TraceParam roof = new TraceParam();
  roof.Start = post + "0 1.9 0";
  roof.End = post + "0 40 0";
  roof.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (!Soldiers.IsEmpty()) roof.ExcludeArray = Soldiers;
  float roofHit = GetGame().GetWorld().TraceMove(roof, null);
  string roofPrefab;
  if (roof.TraceEnt) roofPrefab = SCR_ResourceNameUtils.GetPrefabName(roof.TraceEnt);
  string open = "";
  for (int bearing = 0; bearing < 16; bearing++)
  {
   vector direction = vector.FromYaw(bearing * 22.5);
   bool met = false;
   for (int level = 0; level < 2; level++)
   {
    TraceParam ray = new TraceParam();
    ray.Start = post + Vector(0, 1.2 + 0.6 * level, 0);
    ray.End = ray.Start + direction * 40;
    ray.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    if (!Soldiers.IsEmpty()) ray.ExcludeArray = Soldiers;
    if (GetGame().GetWorld().TraceMove(ray, null) < 0.999 && Plan.IsBuilding(ray.TraceEnt)) met = true;
   }
   if (!met) open += bearing.ToString() + ",";
  }
  return string.Format("roofHit=%1 roofBuilding=%2 roofEnt=%3 roofed=%4 withinHouse=%5 open=%6", roofHit, Plan.IsBuilding(roof.TraceEnt), roofPrefab, Roofed(post), WithinHouse(post), open);
 }
 // Off the indoor floor: no indoor walking node within 1.5 m (the planner's floor
 // test allows 0.7 m; a patroller drifting into a door frame, which has no node,
 // is still on the floor; one on a roof, a ledge or outside is not).
 bool OffFloor(vector position)
 {
  return Plan.NearestNode(position, 1.5, false) < 0;
 }
 // Overhead along a vertical ray: the house (props such as wall lamps are passed
 // through, at most three).
 bool Roofed(vector post)
 {
  vector from = post + "0 1.9 0";
  for (int hop = 0; hop < 4; hop++)
  {
   TraceParam roof = new TraceParam();
   roof.Start = from;
   roof.End = post + "0 40 0";
   roof.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   if (!Soldiers.IsEmpty()) roof.ExcludeArray = Soldiers;
   float hit = GetGame().GetWorld().TraceMove(roof, null);
   if (hit >= 0.999) return false;
   if (Plan.IsBuilding(roof.TraceEnt)) return true;
   from = vector.Lerp(roof.Start, roof.End, hit) + "0 0.05 0";
  }
  return false;
 }
 // Within the house model's own bounds (not the planner's), inset 0.5 m.
 bool WithinHouse(vector post)
 {
  vector mins, maxs;
  Structure.GetBounds(mins, maxs);
  vector local = Structure.CoordToLocal(post);
  return local[0] > mins[0] + 0.5 && local[0] < maxs[0] - 0.5 && local[2] > mins[2] + 0.5 && local[2] < maxs[2] - 0.5;
 }
 // Independent of the planner: no house overhead is outside; six of sixteen
 // bearings open at 1.2 m and 1.8 m (neither ray meets the house within 40 m) is
 // outside unless the spot lies within the house's own bounds (an open door or
 // window opens bearings for a spot inside).
 bool Outside(vector post)
 {
  if (!Roofed(post)) return true;
  int openBearings = 0;
  for (int bearing = 0; bearing < 16; bearing++)
  {
   vector direction = vector.FromYaw(bearing * 22.5);
   bool met = false;
   for (int level = 0; level < 2; level++)
   {
    TraceParam ray = new TraceParam();
    ray.Start = post + Vector(0, 1.2 + 0.6 * level, 0);
    ray.End = ray.Start + direction * 40;
    ray.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    if (!Soldiers.IsEmpty()) ray.ExcludeArray = Soldiers;
    if (GetGame().GetWorld().TraceMove(ray, null) < 0.999 && Plan.IsBuilding(ray.TraceEnt)) met = true;
   }
   if (!met) openBearings++;
  }
  return openBearings >= 6 && !WithinHouse(post);
 }
 // The house's door leaves, from its own hierarchy (bounded like the planner's).
 void CollectLeaves()
 {
  array<IEntity> parts = {Structure};
  for (int cursor = 0; cursor < parts.Count() && cursor < 512; cursor++)
  {
   IEntity part = parts[cursor];
   for (IEntity child = part.GetChildren(); child && parts.Count() < 512; child = child.GetSibling()) parts.Insert(child);
   if (part == Structure) continue;
   DoorComponent door = DoorComponent.Cast(part.FindComponent(DoorComponent));
   if (!door) continue;
   EXPG_InteriorLeaf leaf = new EXPG_InteriorLeaf();
   leaf.Part = part;
   leaf.Hinge = door.GetDoorPivotPointWS();
   vector mins, maxs;
   part.GetBounds(mins, maxs);
   leaf.Centre = part.CoordToParent((mins + maxs) * 0.5);
   leaf.LeafSpan = Math.Clamp(Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]), 0.5, 1.8);
   vector worldMins, worldMaxs;
   part.GetWorldBounds(worldMins, worldMaxs);
   leaf.FloorY = worldMins[1];
   Leaves.Insert(leaf);
  }
 }
 // Within leaf width + 0.5 m of a hinge, or 0.8 m of a closed leaf's centre, on
 // that leaf's floor (0.6 m below to 1.2 m above). A subset of the planner's zone.
 bool FixtureZone(vector point)
 {
  foreach (EXPG_InteriorLeaf leaf : Leaves)
  {
   float rise = point[1] - leaf.FloorY;
   if (rise < -0.6 || rise > 1.2) continue;
   if (vector.DistanceXZ(point, leaf.Hinge) < leaf.LeafSpan + 0.5 || vector.DistanceXZ(point, leaf.Centre) < 0.8) return true;
  }
  return false;
 }
 bool AnyZone(vector point)
 {
  return Plan.InAnyDoorZone(point) || FixtureZone(point);
 }
 DoorComponent LeafDoor(EXPG_InteriorLeaf leaf)
 {
  if (!leaf.Part) return null;
  return DoorComponent.Cast(leaf.Part.FindComponent(DoorComponent));
 }
 // Open every leaf (or only those that opened on the empty house), or close all.
 void SetDoors(float control, bool openedOnly)
 {
  foreach (EXPG_InteriorLeaf leaf : Leaves)
  {
   if (openedOnly && !leaf.Opened) continue;
   DoorComponent door = LeafDoor(leaf);
   if (door) door.SetControlValue(control, RplId.Invalid());
  }
 }
 bool LeafOpen(EXPG_InteriorLeaf leaf)
 {
  DoorComponent door = LeafDoor(leaf);
  return door && Math.AbsFloat(door.GetNormalizedDoorState()) >= 0.9;
 }
 // Every slot, patrol stop, alarm window and watch point of the plan.
 bool ReportPlan()
 {
  if (!Check(Plan.Error.IsEmpty() && !Plan.Slots.IsEmpty(), "production plan found slots: " + Plan.Error)) return false;
  int fixedSlots = 0;
  foreach (bool isFixed : Plan.FixedSlots) if (isFixed) fixedSlots++;
  PrintFormat("[EXPG INTERIOR PLAN] nodes=%1 slots=%2 fixed=%3 roamStops=%4 windows=%5 watch=%6 walkGroups=%7 planLeaves=%8 storeys=%9", Plan.Nodes.Count(), Plan.Slots.Count(), fixedSlots, Plan.RoamStops.Count(), Plan.WindowNodes.Count(), Plan.WatchNodes.Count(), Plan.WalkGroupCount(), Plan.DoorLeafCount(), Plan.StoreyCount());
  CollectLeaves();
  PrintFormat("[EXPG INTERIOR LEAVES] fixture=%1 plan=%2", Leaves.Count(), Plan.DoorLeafCount());
  if (!Check(!Leaves.IsEmpty() && Plan.DoorLeafCount() >= Leaves.Count(), string.Format("the planner keeps a zone for every door leaf: %1 of %2", Plan.DoorLeafCount(), Leaves.Count()))) return false;
  array<int> places = {};
  places.InsertAll(Plan.Slots);
  places.InsertAll(Plan.RoamStops);
  places.InsertAll(Plan.WindowNodes);
  places.InsertAll(Plan.WatchNodes);
  foreach (int node : places)
  {
   vector spot = Plan.Nodes[node].Position;
   if (!Plan.Nodes[node].DoorBlock && !AnyZone(spot)) continue;
   Zoned++;
   PrintFormat("[EXPG INTERIOR ZONED] node=%1 position=%2 doorBlock=%3 planZone=%4 fixtureZone=%5", node, spot, Plan.Nodes[node].DoorBlock, Plan.InAnyDoorZone(spot), FixtureZone(spot));
  }
  Check(Zoned == 0, string.Format("no post, patrol stop, alarm window or watch point in a door zone: %1", Zoned));
  Check(!Plan.RoamStops.IsEmpty(), string.Format("interior patrol stops exist: %1", Plan.RoamStops.Count()));
  return true;
 }
 bool AddSquad()
 {
  int number = Adds.Count() + 1;
  string reason;
  bool garrisoned = Manager.HasGarrison(Structure);
  bool fits = Manager.CanFit(Structure, SQUAD, reason);
  if (!Check(fits && reason.IsEmpty() && garrisoned == (number > 1), string.Format("add %1 accepted by CanFit, building garrisoned=%2: %3", number, garrisoned, reason))) return false;
  ResourceName team = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(team);
  SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point + Vector(0, 0, 0.5 * number), 0.3)));
  bool fresh = group && group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(group, Structure, 0, SQUAD);
  if (!Check(adopted, string.Format("add %1: freshly spawned fire team adopted by the building", number))) return false;
  // Caching Off until the cache phase.
  group.EXPG_CacheMode = 0;
  EXPG_InteriorAdd add = new EXPG_InteriorAdd();
  add.Group = group;
  add.Record = Manager.Find(group);
  Adds.Insert(add);
  return Check(add.Record != null, string.Format("add %1: garrison record created", number));
 }
 bool VerifyAdd(EXPG_InteriorAdd add, int number)
 {
  if (!Check(Active(add) && add.Record.Members.Count() == SQUAD && add.Group.GetAgentsCount() == SQUAD, string.Format("add %1: all %2 soldiers placed in their own group", number, SQUAD))) return false;
  foreach (EXPG_GarrisonMember member : add.Record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (actor && !Soldiers.Contains(actor)) Soldiers.Insert(actor);
   vector post = member.PostPoint();
   PrintFormat("[EXPG INTERIOR POST] add=%1 member=%2 kind=%3 node=%4 fixed=%5 post=%6", number, member.CacheMember.Id, member.PostKind, member.NodeIndex, member.Fixed, post);
   if (member.PostKind != EXPG_Placement.ROAM) continue;
   bool valid = !member.Fixed && member.Patrol && member.NodeIndex >= 0 && Plan.Nodes[member.NodeIndex].IndoorWalk && !Plan.Nodes[member.NodeIndex].DoorBlock;
   if (!Check(valid, string.Format("add %1 member %2: patroller bound on an indoor stop", number, member.CacheMember.Id))) return false;
   EXPG_InteriorRover rover = new EXPG_InteriorRover();
   rover.Member = member;
   Rovers.Insert(rover);
  }
  return true;
 }
 // Every living guard of the record under garrison control (post or patrol).
 bool Bound(EXPG_GarrisonRecord record)
 {
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead) continue;
   if (member.Fixed && !member.Post) return false;
   if (!member.Fixed && !member.Patrol) return false;
  }
  return true;
 }
 bool Living(EXPG_GarrisonMember member)
 {
  SCR_ChimeraCharacter actor = member.CacheMember.Entity;
  return !member.CacheMember.Dead && actor && !EXPG_GarrisonManager.IsDeadActor(actor);
 }
 bool Standing(EXPG_GarrisonMember member)
 {
  if (member.Fixed) return true;
  return member.Patrol && !member.Patrol.IsMoving();
 }
 // Every living soldier of every add.
 void LivingMembers(notnull array<ref EXPG_GarrisonMember> members)
 {
  members.Clear();
  foreach (EXPG_InteriorAdd add : Adds)
  {
   if (!add.Record) continue;
   foreach (EXPG_GarrisonMember member : add.Record.Members)
   {
    if (Living(member)) members.Insert(member);
   }
  }
 }
 // Standing soldiers in a door zone (walkers may cross doorways).
 int CountDoorGuards()
 {
  array<ref EXPG_GarrisonMember> members = {};
  LivingMembers(members);
  int count = 0;
  foreach (EXPG_GarrisonMember member : members)
  {
   if (!Standing(member)) continue;
   vector position = member.CacheMember.Entity.GetOrigin();
   if (!AnyZone(position)) continue;
   count++;
   PrintFormat("[EXPG INTERIOR DOOR GUARD] member=%1 kind=%2 position=%3", member.CacheMember.Id, member.PostKind, position);
  }
  return count;
 }
 // Claims unique and 1.5 m from each other and from fixed posts; returns pairs too close.
 int CrowdedClaims()
 {
  array<ref EXPG_GarrisonMember> members = {};
  LivingMembers(members);
  int crowded = 0;
  for (int a = 0; a < members.Count(); a++)
  {
   for (int b = a + 1; b < members.Count(); b++)
   {
    if (members[a].Fixed && members[b].Fixed) continue;
    vector first = members[a].PostPoint();
    vector second = members[b].PostPoint();
    if (members[a].NodeIndex == members[b].NodeIndex && members[a].NodeIndex >= 0) { crowded++; continue; }
    if (EXPG_BuildingPlan.Crowded(first, second, EXPG_BuildingPlan.POST_SPACING - 0.01)) crowded++;
   }
  }
  return crowded;
 }
 void SamplePatrol()
 {
  array<ref EXPG_GarrisonMember> members = {};
  LivingMembers(members);
  foreach (EXPG_InteriorRover rover : Rovers)
  {
   if (!Living(rover.Member) || !rover.Member.Patrol) continue;
   vector position = rover.Member.CacheMember.Entity.GetOrigin();
   if (vector.DistanceSq(rover.Last, vector.Zero) > 0.01) rover.Travelled += vector.Distance(position, rover.Last);
   rover.Last = position;
   int claim = rover.Member.Patrol.ClaimedNode();
   if (claim >= 0 && !rover.Claims.Contains(claim)) rover.Claims.Insert(claim);
   if (Outside(position) || OffFloor(position))
   {
    RoamOutside++;
    PrintFormat("[EXPG INTERIOR OUTSIDE] member=%1 position=%2 moving=%3 outside=%4 indoorFloor=%5 inside=%6 %7", rover.Member.CacheMember.Id, position, rover.Member.Patrol.IsMoving(), Outside(position), Plan.OnIndoorFloor(position), Plan.Inside(position, 0.25), OutsideWhy(position));
   }
  }
  for (int a = 0; a < members.Count(); a++)
  {
   for (int b = a + 1; b < members.Count(); b++)
   {
    vector first = members[a].CacheMember.Entity.GetOrigin();
    vector second = members[b].CacheMember.Entity.GetOrigin();
    if (Math.AbsFloat(first[1] - second[1]) >= 1.5) continue;
    float distance = vector.DistanceXZ(first, second);
    if (distance < 0.5)
    {
     Overlap++;
     PrintFormat("[EXPG INTERIOR OVERLAP] a=%1 b=%2 distance=%3", first, second, distance);
    }
    if (distance < 1.2 && Standing(members[a]) && Standing(members[b]))
    {
     Stacked++;
     PrintFormat("[EXPG INTERIOR STACKED] a=%1 b=%2 distance=%3", first, second, distance);
    }
   }
  }
  int crowdedClaims = CrowdedClaims();
  if (crowdedClaims > 0)
  {
   Stacked += crowdedClaims;
   PrintFormat("[EXPG INTERIOR CLAIMS] crowded=%1", crowdedClaims);
  }
 }
 int LivingRovers()
 {
  int count = 0;
  foreach (EXPG_InteriorRover rover : Rovers) if (Living(rover.Member)) count++;
  return count;
 }
 bool AllRovers(bool alerted, bool settled)
 {
  foreach (EXPG_InteriorRover rover : Rovers)
  {
   if (!Living(rover.Member)) continue;
   EXPG_PatrolControl patrol = rover.Member.Patrol;
   if (!patrol || patrol.InAlert() != alerted) return false;
   if (settled && patrol.IsMoving()) return false;
  }
  return true;
 }
 void EvaluateAlert()
 {
  foreach (EXPG_InteriorRover rover : Rovers)
  {
   if (!Living(rover.Member) || !rover.Member.Patrol) continue;
   int claim = rover.Member.Patrol.ClaimedNode();
   vector position = rover.Member.CacheMember.Entity.GetOrigin();
   if (Plan.WindowNodes.Contains(claim)) AlertWindows++;
   else if (Plan.WatchNodes.Contains(claim)) AlertWatch++;
   else AlertHold++;
   if (Outside(position) || OffFloor(position)) AlertOutside++;
   PrintFormat("[EXPG INTERIOR ALERT] member=%1 claim=%2 window=%3 watch=%4 alerted=%5 moving=%6 position=%7", rover.Member.CacheMember.Id, claim, Plan.WindowNodes.Contains(claim), Plan.WatchNodes.Contains(claim), rover.Member.Patrol.InAlert(), rover.Member.Patrol.IsMoving(), position);
  }
  AlertStacked = CrowdedClaims();
  Check(AllRovers(true, false), "every patroller took an alarm post (window, watch point or his stop)");
  Check(AlertOutside == 0, string.Format("no patroller outside during the alarm: %1", AlertOutside));
  Check(AlertStacked == 0, string.Format("alarm claims unique and 1.5 m apart: %1 pairs closer", AlertStacked));
  if (FreeWindows > 0) Check(AlertWindows >= 1, string.Format("a free window was taken: windows=%1 watch=%2 hold=%3 free=%4", AlertWindows, AlertWatch, AlertHold, FreeWindows));
 }
 bool Asleep(EXPG_GarrisonRecord record, bool full)
 {
  if (full) return record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED;
  return record.Simulation && record.Simulation.Suspended;
 }
 bool Awake(EXPG_GarrisonRecord record) { return !record.Full && !record.Simulation; }
 void SetCacheMode(int mode)
 {
  // A Full-cached garrison has no squad to edit: the record's setting stands in for it.
  foreach (EXPG_InteriorAdd add : Adds)
  {
   if (add.Record && add.Record.Group) add.Record.Group.EXPG_SetSetting(0, mode);
   else if (add.Record) add.Record.CacheMode = mode;
  }
 }
 bool AllAsleep(bool full)
 {
  foreach (EXPG_InteriorAdd add : Adds) if (!Active(add) || !Asleep(add.Record, full)) return false;
  return true;
 }
 // Awake, every survivor bound to his control in his own group.
 bool AllAwake()
 {
  foreach (EXPG_InteriorAdd add : Adds)
  {
   if (!Active(add) || !Awake(add.Record)) return false;
   foreach (EXPG_GarrisonMember member : add.Record.Members)
   {
    if (member.CacheMember.Dead) continue;
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    if (!actor || actor.GetCharacterGroup() != add.Group) return false;
    if (member.Fixed && !member.Post) return false;
    if (!member.Fixed && !member.Patrol) return false;
   }
  }
  return true;
 }
 // Survivors back as patrollers near their claimed stops; the casualty stays dead
 // and no add has more soldiers than survivors.
 int CheckRestored(string label)
 {
  int rovers = 0;
  foreach (EXPG_InteriorRover rover : Rovers)
  {
   EXPG_GarrisonMember member = rover.Member;
   if (member == Casualty)
   {
    Check(member.CacheMember.Dead, label + ": the killed patroller stays dead");
    continue;
   }
   if (member.CacheMember.Dead) continue;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   EXPG_PatrolControl patrol = member.Patrol;
   bool back = actor && patrol && !member.Fixed && member.PostKind == EXPG_Placement.ROAM && patrol.ClaimedNode() >= 0;
   if (back)
   {
    EXPG_BuildingNode node = Plan.Nodes[patrol.ClaimedNode()];
    back = node.IndoorWalk && !node.DoorBlock && vector.DistanceXZ(actor.GetOrigin(), node.Position) <= 1.5;
   }
   PrintFormat("[EXPG INTERIOR RESTORED] cycle=%1 member=%2 patroller=%3", label, member.CacheMember.Id, back);
   if (back) rovers++;
  }
  foreach (EXPG_InteriorAdd add : Adds)
  {
   int living = 0;
   foreach (EXPG_GarrisonMember counted : add.Record.Members) if (!counted.CacheMember.Dead) living++;
   if (add.Group.GetAgentsCount() > living || add.Record.Members.Count() != SQUAD) Replenished++;
  }
  Check(rovers == LivingRovers(), string.Format("%1: every surviving patroller is back as a patroller near his stop: %2 of %3", label, rovers, LivingRovers()));
  Check(Replenished == 0, string.Format("%1: nobody replenished: %2", label, Replenished));
  return rovers;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  foreach (EXPG_InteriorAdd tracked : Adds) { if (tracked.Record && tracked.Record.Group) tracked.Group = tracked.Record.Group; }
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestRefusals > 0) { Check(false, "no garrison refused, retained as normal AI or released its survivors"); Finish("refusal"); return; }
  foreach (int i, EXPG_InteriorAdd watched : Adds)
  {
   if (!watched.Record || !watched.Record.Ready || Active(watched)) continue;
   Check(false, string.Format("garrison %1 released during the run: %2", i + 1, watched.Record.Status));
   Finish("premature release");
   return;
  }
  if (Phase == 0)
  {
   array<int> players = {};
   GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty() && EBG_CacheZone.Zones.IsEmpty(), "isolated server with no connected players or Unit Caching zones")) { Finish("setup"); return; }
   // Town centres from the GM map grid: Saint-Philippe, Levie, Morton, Montignac.
   GetGame().GetWorld().QueryEntitiesBySphere("4570 0 10700", 700, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("7270 0 4700", 500, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("5040 0 3950", 500, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("4800 0 7000", 900, AddMapHouse);
   ResourceName house = "{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et";
   Resource houseResource = Resource.Load(house);
   if (MapHouse)
   {
    Structure = MapHouse;
    Baked = true;
    Point = MapHouse.GetOrigin();
   }
   else Structure = GetGame().SpawnEntityPrefab(houseResource, GetGame().GetWorld(), Params(Point));
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   PrintFormat("[EXPG INTERIOR HOUSE] map=%1 origin=%2", Baked, Point);
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "vanilla two-storey town house spawned")) { Finish("building"); return; }
   // A house spawned at run time has no baked navmesh on its upper floors (map
   // houses do): rebuild it like the editor does, so patrollers can find paths.
   SCR_AIWorld aiWorld = SCR_AIWorld.Cast(GetGame().GetAIWorld());
   if (aiWorld && !Baked) aiWorld.RequestNavmeshRebuildEntity(Structure);
   Manager.Prepare(Structure);
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   Plan = Manager.FindPlan(Structure);
   if (!Plan || !Plan.Done)
   {
    if (Now() - PhaseStarted > 150) { Check(false, "production analysis finished within 150 seconds"); Finish("analysis"); }
    return;
   }
   // The navmesh rebuild runs asynchronously: give it 20 s from the spawn.
   if (Now() - PhaseStarted < 20) return;
   if (!ReportPlan()) { Finish("plan"); return; }
   // Baseline on the empty house: which leaves open fully at all.
   SetDoors(1, false);
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (Now() - PhaseStarted < 4) return;
   foreach (EXPG_InteriorLeaf leaf : Leaves)
   {
    leaf.Opened = LeafOpen(leaf);
    if (leaf.Opened) DoorsOpened++;
   }
   PrintFormat("[EXPG INTERIOR DOORS] baseline opened=%1 of %2", DoorsOpened, Leaves.Count());
   SetDoors(0, false);
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseStarted < 3) return;
   if (!AddSquad()) { Finish("add"); return; }
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   EXPG_InteriorAdd last = Adds[Adds.Count() - 1];
   // Placed and bound: the editor's move lands a frame or more later, and a guard
   // is bound only once he stands on his post or stop.
   if (!last.Record.Ready || !Bound(last.Record))
   {
    if (Now() - PhaseStarted > 30) { Check(false, string.Format("add %1 placed and bound within 30 seconds: %2", Adds.Count(), last.Record.Status)); Finish("placement timeout"); }
    return;
   }
   if (!VerifyAdd(last, Adds.Count())) { Finish("placement"); return; }
   if (Rovers.Count() < 3 && Adds.Count() < MAX_ADDS)
   {
    if (!AddSquad()) { Finish("add"); return; }
    Advance(4);
    return;
   }
   if (!Check(Rovers.Count() >= 3, string.Format("at least three soldiers patrol inside after %1 adds: %2", Adds.Count(), Rovers.Count()))) { Finish("rovers"); return; }
   DoorGuards = CountDoorGuards();
   Check(DoorGuards == 0, string.Format("no standing guard in a door zone: %1", DoorGuards));
   SetDoors(1, true);
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseStarted < 4) return;
   int blocked = 0;
   foreach (EXPG_InteriorLeaf opened : Leaves) if (opened.Opened && !LeafOpen(opened)) blocked++;
   DoorTries++;
   PrintFormat("[EXPG INTERIOR DOORS] try=%1 blocked=%2 of %3", DoorTries, blocked, DoorsOpened);
   SetDoors(0, false);
   if (blocked > 0 && DoorTries < 3) { Advance(6); return; }
   DoorsBlocked = blocked;
   Check(DoorsBlocked == 0, string.Format("every door that opened on the empty house opens fully with the guards in place: %1 blocked", DoorsBlocked));
   Guards = Soldiers.Count();
   Advance(7);
   return;
  }
  if (Phase == 6)
  {
   if (Now() - PhaseStarted < 3) return;
   SetDoors(1, true);
   Advance(5);
   return;
  }
  if (Phase == 7)
  {
   SamplePatrol();
   if (Now() - PhaseStarted < PATROL_SECONDS) return;
   foreach (EXPG_InteriorRover rover : Rovers)
   {
    PrintFormat("[EXPG INTERIOR ROVER] member=%1 travelled=%2 stops=%3", rover.Member.CacheMember.Id, rover.Travelled, rover.Claims.Count());
    if (rover.Travelled >= 3 && rover.Claims.Count() >= 2) RoamMoved++;
   }
   Check(RoamMoved == LivingRovers(), string.Format("every patroller walked at least 3 m over two stops in %1 s: %2 of %3", PATROL_SECONDS, RoamMoved, LivingRovers()));
   Check(RoamOutside == 0, string.Format("no patroller sample outside or off the indoor floor: %1", RoamOutside));
   Check(Stacked == 0, string.Format("standing soldiers 1.2 m apart and claims 1.5 m apart: %1", Stacked));
   Check(Overlap == 0, string.Format("no two soldiers within 0.5 m: %1", Overlap));
   // Alarm through the real threat-state event on one fixed guard.
   // Free windows a patroller can take: free and within the alarm's 40 steps
   // of a patroller (PlaceAlert's breadth-first reach); others are reported only.
   array<int> reachable = {};
   foreach (EXPG_InteriorRover reacher : Rovers)
   {
    if (!Living(reacher.Member) || !reacher.Member.Patrol) continue;
    int reachFrom = Plan.NearestNode(reacher.Member.CacheMember.Entity.GetOrigin(), 1.5, false);
    if (reachFrom < 0) reachFrom = reacher.Member.Patrol.ClaimedNode();
    Plan.DistancesFrom(reachFrom, 40);
    foreach (int windowNode : Plan.WindowNodes)
    {
     if (Plan.Hops(windowNode) >= 0 && !reachable.Contains(windowNode)) reachable.Insert(windowNode);
    }
   }
   int unreachable = 0;
   foreach (int freeWindow : Plan.WindowNodes)
   {
    if (!Plan.StopFree(freeWindow)) continue;
    if (reachable.Contains(freeWindow)) FreeWindows++;
    else unreachable++;
   }
   PrintFormat("[EXPG INTERIOR WINDOWS] free=%1 beyondReach=%2", FreeWindows, unreachable);
   SCR_AIUtilityComponent target;
   array<ref EXPG_GarrisonMember> members = {};
   LivingMembers(members);
   foreach (EXPG_GarrisonMember fixedGuard : members)
   {
    if (!fixedGuard.Fixed) continue;
    target = EXPG_GarrisonMember.FindUtility(fixedGuard.CacheMember.Entity);
    if (target && target.m_ThreatSystem) break;
   }
   if (!Check(target && target.m_ThreatSystem, "a fixed guard's native threat system found")) { Finish("alarm setup"); return; }
   target.m_ThreatSystem.ThreatBulletImpact(5);
   PrintFormat("[EXPG INTERIOR ALARM] freeWindows=%1 rovers=%2", FreeWindows, LivingRovers());
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   bool placed = AllRovers(true, true);
   if (!placed && Now() - PhaseStarted < 30) return;
   EvaluateAlert();
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (!AllRovers(false, false))
   {
    if (Now() - PhaseStarted > 150) { Check(false, "every patroller back on patrol within 150 s of the alarm"); Finish("calm"); }
    return;
   }
   foreach (EXPG_InteriorRover calm : Rovers)
   {
    if (Living(calm.Member) && calm.Member.Patrol && calm.Member.Patrol.IsMoving()) CalmReturned = 1;
   }
   if (CalmReturned == 0 && Now() - PhaseStarted < 185) return;
   Check(CalmReturned == 1, "after the calm a patroller walks again");
   Advance(10);
   return;
  }
  if (Phase == 10)
  {
   foreach (EXPG_InteriorRover victim : Rovers)
   {
    if (!Living(victim.Member)) continue;
    Casualty = victim.Member;
    break;
   }
   if (!Check(Casualty != null, "a living patroller to kill")) { Finish("casualty"); return; }
   SCR_ChimeraCharacter casualtyActor = Casualty.CacheMember.Entity;
   casualtyActor.GetDamageManager().Kill(Instigator.CreateInstigator(casualtyActor));
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   if (!Casualty.CacheMember.Dead)
   {
    if (Now() - PhaseStarted > 10) { Check(false, "the garrison noticed the casualty within 10 s"); Finish("casualty state"); }
    return;
   }
   Killed = 1;
   SetCacheMode(2);
   Advance(12);
   return;
  }
  if (Phase == 12)
  {
   if (!AllAsleep(true))
   {
    if (Now() - PhaseStarted > 120) { Check(false, "every garrison Full-cached within 120 s"); foreach (EXPG_InteriorAdd sleepy : Adds) PrintFormat("[EXPG INTERIOR SLEEP] group=%1 status=%2", sleepy.Group, sleepy.Record.Status); Finish("full sleep"); }
    return;
   }
   Check(true, "every garrison Full-cached after its patrollers settled");
   SetCacheMode(0);
   Advance(13);
   return;
  }
  if (Phase == 13)
  {
   if (!AllAwake())
   {
    if (Now() - PhaseStarted > 60) { Check(false, "every garrison woke from Full within 60 s"); Finish("full wake"); }
    return;
   }
   FullRovers = CheckRestored("Full");
   SetCacheMode(1);
   Advance(14);
   return;
  }
  if (Phase == 14)
  {
   if (!AllAsleep(false))
   {
    if (Now() - PhaseStarted > 120) { Check(false, "every garrison Simulation-cached within 120 s"); foreach (EXPG_InteriorAdd paused : Adds) PrintFormat("[EXPG INTERIOR SLEEP] group=%1 status=%2", paused.Group, paused.Record.Status); Finish("simulation sleep"); }
    return;
   }
   Check(true, "every garrison Simulation-cached after its patrollers settled");
   SetCacheMode(0);
   Advance(15);
   return;
  }
  if (Phase == 15)
  {
   if (!AllAwake())
   {
    if (Now() - PhaseStarted > 60) { Check(false, "every garrison woke from Simulation within 60 s"); Finish("simulation wake"); }
    return;
   }
   SimRovers = CheckRestored("Simulation");
   Finish("completed");
  }
 }
}
