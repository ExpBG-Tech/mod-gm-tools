// TEST ONLY. Garrison post spread on a two-storey town house. Live report after
// 0.1.7: on House_Town_E_2I01 the soldiers of three Add Garrison squads bunched in
// the ground-floor hall by the front door, two stood outside on the entrance steps,
// none were upstairs or at the windows.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_PostSpreadGameplay.c -ExpectResult '\[EXPG SPREAD RESULT\] checks=[1-9]\d* failures=0 slots=[1-9]\d* storeys=[2-9] windowSlots=[1-9]\d* doorSlots=\d+ guards=12 postStoreys=[2-9] outside=0 around=0 crowded=0 reason=completed' -TimeoutSeconds 420 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Spawns the vanilla House_Town_E_2I01 prefab on the generated GM_Eden world, lets
// the production planner analyse it and logs every slot (storey, height above the
// house origin, watched opening, inside/outside). Then three US fire teams are added
// through the production calls the editor makes (CanFit, fresh roster, AdoptFresh).
// Checks: slots and posts on at least two storeys, window posts first (the first
// squad holds window posts only), no post outside the walls, post spacing kept,
// no two soldiers within 0.8 m. Caching stays Off; no players, no GM UI.
// The inside/outside test here is independent of the planner's own test.
class EXPG_SpreadAdd
{
 SCR_AIGroup Group;
 ref EXPG_GarrisonRecord Record;
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
   PrintFormat("[EXPG SPREAD REFUSAL] group=%1 status=%2", Group, message);
  }
 }
}

modded class EXPG_BuildingPlan
{
 int EXPG_SpreadOpenings(bool doors)
 {
  int count = 0;
  foreach (EXPG_BuildingOpening opening : m_Openings)
  {
   if (opening.Door == doors) count++;
  }
  return count;
 }

 int EXPG_SpreadExteriorDoors()
 {
  int count = 0;
  foreach (EXPG_BuildingOpening opening : m_Openings)
  {
   if (opening.Door && opening.Exterior) count++;
  }
  return count;
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const int SQUAD = 4;
 static const int ADDS = 3;
 IEntity Structure;
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 ref array<ref EXPG_SpreadAdd> Adds = {};
 ref array<IEntity> Soldiers = {};
 ObserversSystem Observers;
 int ObserverKey;
 int Phase;
 int Checks;
 int Failures;
 int SlotStoreys;
 int WindowSlots;
 int DoorSlots;
 int Guards;
 int PostStoreys;
 int OutsidePosts;
 int AroundPosts;
 int CrowdedPairs;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;
 vector Point = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 15;
  ObserverKey = "EXPG_PostSpread.Observer".Hash();
  PrintFormat("[EXPG SPREAD BEGIN] squad=%1 adds=%2 deadline=%3 connectedPlayer=0", SQUAD, ADDS, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG SPREAD CHECK] pass=%1 %2", value, label);
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
  int slots = 0;
  if (Plan) slots = Plan.Slots.Count();
  string first = string.Format("checks=%1 failures=%2 slots=%3 storeys=%4 windowSlots=%5 doorSlots=%6", Checks, Failures, slots, SlotStoreys, WindowSlots, DoorSlots);
  string second = string.Format("guards=%1 postStoreys=%2 outside=%3 around=%4 crowded=%5 reason=%6", Guards, PostStoreys, OutsidePosts, AroundPosts, CrowdedPairs, reason);
  PrintFormat("[EXPG SPREAD RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG SPREAD PHASE] phase=%1 elapsed=%2 adds=%3", Phase, Now() - Started, Adds.Count());
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
 bool Active(EXPG_SpreadAdd add)
 {
  return add.Group && add.Group.EXPG_Active && add.Record && Manager.Find(add.Group) == add.Record && !add.Record.ReleaseRequested;
 }
 // Independent of the planner's eight-bearing test: sixteen bearings at 1.2 m and
 // 1.8 m above the post; a bearing is open when neither ray meets the house within
 // 40 m. No roof overhead, or six or more open bearings (a 135-degree open side),
 // is outside. Garrison soldiers are excluded from every ray.
 bool Outside(vector post)
 {
  TraceParam roof = new TraceParam();
  roof.Start = post + "0 1.9 0";
  roof.End = post + "0 40 0";
  roof.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (!Soldiers.IsEmpty()) roof.ExcludeArray = Soldiers;
  if (GetGame().GetWorld().TraceMove(roof, null) >= 0.999 || !Plan.IsBuilding(roof.TraceEnt)) return true;
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
  return openBearings >= 6;
 }
 float Height(vector point)
 {
  return point[1] - Plan.Origin[1];
 }
 int CountDistinct(array<int> values)
 {
  array<int> seen = {};
  foreach (int value : values)
  {
   if (!seen.Contains(value)) seen.Insert(value);
  }
  return seen.Count();
 }
 // Plan summary, height profile, every slot, and the plan-level checks.
 bool ReportPlan()
 {
  int reachable = 0;
  int stairs = 0;
  int stairsReachable = 0;
  float lowest = 100000;
  foreach (EXPG_BuildingNode node : Plan.Nodes)
  {
   if (node.Reachable) reachable++;
   if (node.Stair) stairs++;
   if (node.Stair && node.Reachable) stairsReachable++;
   lowest = Math.Min(lowest, node.Position[1]);
  }
  PrintFormat("[EXPG SPREAD PLAN] nodes=%1 reachable=%2 stairs=%3 stairsReachable=%4 storeys=%5 slots=%6 windows=%7 doors=%8 exteriorDoors=%9", Plan.Nodes.Count(), reachable, stairs, stairsReachable, Plan.StoreyCount(), Plan.Slots.Count(), Plan.EXPG_SpreadOpenings(false), Plan.EXPG_SpreadOpenings(true), Plan.EXPG_SpreadExteriorDoors());
  PrintFormat("[EXPG SPREAD PLAN] origin=%1 mins=%2 maxs=%3 error=%4", Plan.Origin, Plan.Mins, Plan.Maxs, Plan.Error);
  // One line per metre of height with samples: shows whether an upper floor was
  // sampled but not connected (reachable=0) or not sampled at all.
  for (int metre = 0; metre < 16; metre++)
  {
   int sampled = 0;
   int linked = 0;
   int treads = 0;
   int indoor = 0;
   int walled = 0;
   foreach (EXPG_BuildingNode sample : Plan.Nodes)
   {
    float above = sample.Position[1] - lowest;
    if (above < metre || above >= metre + 1) continue;
    sampled++;
    if (sample.Reachable) linked++;
    if (sample.Stair) treads++;
    if (sample.Interior) indoor++;
    if (sample.Enclosed) walled++;
   }
   if (sampled > 0) PrintFormat("[EXPG SPREAD LEVEL] metre=%1 nodes=%2 reachable=%3 stairs=%4 indoor=%5 enclosed=%6", metre, sampled, linked, treads, indoor, walled);
  }
  if (!Check(Plan.Error.IsEmpty() && !Plan.Slots.IsEmpty(), "production plan found slots: " + Plan.Error)) return false;
  array<int> storeys = {};
  array<int> windowStoreys = {};
  int lastWindow = -1;
  int firstDoor = 1000;
  int lastDoor = -1;
  int firstOther = 1000;
  int firstUnauthored = -1;
  int outsideSlots = 0;
  int crowdedSlots = 0;
  foreach (int order, int nodeIndex : Plan.Slots)
  {
   EXPG_BuildingNode slot = Plan.Nodes[nodeIndex];
   bool inside = !Outside(slot.Position);
   if (!inside) outsideSlots++;
   storeys.Insert(slot.Storey);
   if (slot.Score != EXPG_BuildingPlan.SCORE_SENTINEL && firstUnauthored < 0) firstUnauthored = order;
   if (slot.Opening == EXPG_BuildingPlan.OPENING_WINDOW)
   {
    WindowSlots++;
    lastWindow = order;
    windowStoreys.Insert(slot.Storey);
   }
   else if (slot.Opening == EXPG_BuildingPlan.OPENING_DOOR)
   {
    DoorSlots++;
    if (order < firstDoor) firstDoor = order;
    lastDoor = order;
   }
   else if (slot.Score != EXPG_BuildingPlan.SCORE_SENTINEL && order < firstOther) firstOther = order;
   for (int other = 0; other < order; other++)
   {
    if (EXPG_BuildingPlan.Crowded(slot.Position, Plan.Nodes[Plan.Slots[other]].Position, EXPG_BuildingPlan.POST_SPACING - 0.01)) crowdedSlots++;
   }
   PrintFormat("[EXPG SPREAD SLOT] order=%1 node=%2 storey=%3 height=%4 opening=%5 score=%6 range=%7 fixed=%8 inside=%9", order, nodeIndex, slot.Storey, Height(slot.Position), slot.Opening, slot.Score, slot.Range, Plan.FixedSlots[order], inside);
  }
  SlotStoreys = CountDistinct(storeys);
  Check(SlotStoreys >= 2, string.Format("slots on at least two storeys: %1 (plan storeys %2)", SlotStoreys, Plan.StoreyCount()));
  Check(WindowSlots > 0 && firstUnauthored >= 0 && Plan.Nodes[Plan.Slots[firstUnauthored]].Opening == EXPG_BuildingPlan.OPENING_WINDOW, string.Format("window posts exist and the first planned post watches a window: windows=%1", WindowSlots));
  Check(lastWindow < firstDoor && lastDoor < firstOther && lastWindow < firstOther, string.Format("windows, then doors, then the rest: lastWindow=%1 firstDoor=%2 lastDoor=%3 firstOther=%4", lastWindow, firstDoor, lastDoor, firstOther));
  Check(outsideSlots == 0, string.Format("no slot outside the walls: %1", outsideSlots));
  Check(crowdedSlots == 0, string.Format("slots keep %1 m on a floor: %2 pairs closer", EXPG_BuildingPlan.POST_SPACING, crowdedSlots));
  PrintFormat("[EXPG SPREAD WINDOWS] windowStoreys=%1", CountDistinct(windowStoreys));
  return true;
 }
 int WindowStoreys()
 {
  array<int> storeys = {};
  foreach (int nodeIndex : Plan.Slots)
  {
   if (Plan.Nodes[nodeIndex].Opening == EXPG_BuildingPlan.OPENING_WINDOW) storeys.Insert(Plan.Nodes[nodeIndex].Storey);
  }
  return CountDistinct(storeys);
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
  SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point + Vector(22, 0, 4 * number), 0.3)));
  bool fresh = group && group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(group, Structure, 0, SQUAD);
  if (!Check(adopted, string.Format("add %1: freshly spawned fire team adopted by the building", number))) return false;
  // Caching Off: the squads must hold their posts for the whole run.
  group.EXPG_CacheMode = 0;
  EXPG_SpreadAdd add = new EXPG_SpreadAdd();
  add.Group = group;
  add.Record = Manager.Find(group);
  Adds.Insert(add);
  return Check(add.Record != null, string.Format("add %1: garrison record created", number));
 }
 bool VerifyAdd(int index)
 {
  EXPG_SpreadAdd add = Adds[index];
  int number = index + 1;
  if (!Check(Active(add) && add.Record.Members.Count() == SQUAD && add.Group.GetAgentsCount() == SQUAD, string.Format("add %1: all %2 soldiers placed in their own group", number, SQUAD))) return false;
  array<int> storeys = {};
  int windows = 0;
  foreach (EXPG_GarrisonMember member : add.Record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (actor && !Soldiers.Contains(actor)) Soldiers.Insert(actor);
  }
  foreach (EXPG_GarrisonMember placed : add.Record.Members)
  {
   vector post = placed.PostPoint();
   int storey = -1;
   int opening = 0;
   if (placed.NodeIndex >= 0)
   {
    storey = Plan.Nodes[placed.NodeIndex].Storey;
    opening = Plan.Nodes[placed.NodeIndex].Opening;
    storeys.Insert(storey);
   }
   if (opening == EXPG_BuildingPlan.OPENING_WINDOW) windows++;
   bool inside = !Outside(post);
   PrintFormat("[EXPG SPREAD POST] add=%1 member=%2 kind=%3 node=%4 storey=%5 height=%6 opening=%7 inside=%8 post=%9", number, placed.CacheMember.Id, placed.PostKind, placed.NodeIndex, storey, Height(post), opening, inside, post);
   if (placed.PostKind == EXPG_Placement.AROUND) { AroundPosts++; continue; }
   if (!Check(placed.PostKind != EXPG_Placement.SPAWN, string.Format("add %1 member %2: not left at the spawn point", number, placed.CacheMember.Id))) return false;
   if (!inside) OutsidePosts++;
   Check(inside && placed.NodeIndex >= 0 && Plan.Nodes[placed.NodeIndex].Interior && !Plan.Nodes[placed.NodeIndex].Stair, string.Format("add %1 member %2: building post inside the walls", number, placed.CacheMember.Id));
  }
  if (index == 0)
  {
   if (WindowSlots >= SQUAD) Check(windows == SQUAD, string.Format("first squad holds window posts only: %1 of %2", windows, SQUAD));
   if (WindowStoreys() >= 2) Check(CountDistinct(storeys) >= 2, string.Format("first squad spread over two storeys: %1", CountDistinct(storeys)));
  }
  return true;
 }
 // After the last add: every soldier alive on his post, posts spaced, nobody
 // within 0.8 m of another soldier, posts on at least two storeys.
 void FinalChecks()
 {
  array<vector> posts = {};
  array<int> storeys = {};
  array<IEntity> actors = {};
  int held = 0;
  Guards = 0;
  foreach (EXPG_SpreadAdd add : Adds)
  {
   if (!add.Record) continue;
   foreach (EXPG_GarrisonMember member : add.Record.Members)
   {
    Guards++;
    posts.Insert(member.PostPoint());
    if (member.NodeIndex >= 0) storeys.Insert(Plan.Nodes[member.NodeIndex].Storey);
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    bool alive = !member.CacheMember.Dead && actor && actor.GetCharacterGroup() == add.Group && !EXPG_GarrisonManager.IsDeadActor(actor);
    if (alive) actors.Insert(actor);
    // The post control's own tolerance: EXPG_PostControl.Tick releases a guard pushed more than 1.5 m.
    if (alive && (!member.Fixed || vector.DistanceSq(actor.GetOrigin(), member.PostPoint()) <= 2.25)) held++;
   }
  }
  PostStoreys = CountDistinct(storeys);
  int crowdedPosts = 0;
  for (int a = 0; a < posts.Count(); a++)
  {
   for (int b = a + 1; b < posts.Count(); b++)
   {
    if (EXPG_BuildingPlan.Crowded(posts[a], posts[b], EXPG_BuildingPlan.POST_SPACING - 0.01)) crowdedPosts++;
   }
  }
  CrowdedPairs = 0;
  for (int c = 0; c < actors.Count(); c++)
  {
   for (int d = c + 1; d < actors.Count(); d++)
   {
    vector first = actors[c].GetOrigin();
    vector second = actors[d].GetOrigin();
    if (Math.AbsFloat(first[1] - second[1]) < 1.5 && vector.DistanceXZ(first, second) < 0.8)
    {
     CrowdedPairs++;
     PrintFormat("[EXPG SPREAD CROWDED] a=%1 b=%2 distance=%3", first, second, vector.DistanceXZ(first, second));
    }
   }
  }
  Check(Guards == SQUAD * ADDS && held == Guards, string.Format("all %1 soldiers alive and holding their posts: %2", Guards, held));
  Check(crowdedPosts == 0, string.Format("all posts keep %1 m on a floor: %2 pairs closer", EXPG_BuildingPlan.POST_SPACING, crowdedPosts));
  Check(CrowdedPairs == 0, string.Format("no two soldiers within 0.8 m: %1 pairs", CrowdedPairs));
  Check(PostStoreys >= 2, string.Format("posts on at least two storeys: %1", PostStoreys));
  Check(OutsidePosts == 0, string.Format("no building post outside the walls: %1", OutsidePosts));
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestRefusals > 0) { Check(false, "no garrison refused, retained as normal AI or released its survivors"); Finish("refusal"); return; }
  foreach (int i, EXPG_SpreadAdd watched : Adds)
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
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   ResourceName house = "{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et";
   Resource houseResource = Resource.Load(house);
   Structure = GetGame().SpawnEntityPrefab(houseResource, GetGame().GetWorld(), Params(Point));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "vanilla two-storey town house spawned")) { Finish("building"); return; }
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
   if (!ReportPlan()) { Finish("plan"); return; }
   if (!AddSquad()) { Finish("add"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   EXPG_SpreadAdd last = Adds[Adds.Count() - 1];
   if (!last.Record.Ready)
   {
    if (Now() - PhaseStarted > 30) { Check(false, string.Format("add %1 placed within 30 seconds: %2", Adds.Count(), last.Record.Status)); Finish("placement timeout"); }
    return;
   }
   if (!VerifyAdd(Adds.Count() - 1)) { Finish("placement"); return; }
   if (Adds.Count() < ADDS)
   {
    if (!AddSquad()) { Finish("add"); return; }
    Advance(2);
    return;
   }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseStarted < 5) return;
   FinalChecks();
   Finish("completed");
  }
 }
}
